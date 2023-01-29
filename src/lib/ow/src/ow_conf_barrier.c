/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* libc */
#include <time.h> /* clock_gettime() */
#include <pthread.h> /* pthread_* */
#include <errno.h> /* EAGAIN */
#include <unistd.h> /* usleep() */

/* 3rd party */
#include <ev.h> /* ev_* */

/* opensync */
#include <ds_dlist.h> /* ds_dlist_* */
#include <log.h> /* LOG*() */
#include <const.h> /* container_of() */
#include <osw_ut.h> /* osw_ut_register() */
#include <osw_module.h> /* OSW_MODULE()() */
#include <osw_confsync.h>

struct ow_conf_barrier_waiter {
    struct ds_dlist_node node;
    pthread_cond_t cond;
    pthread_condattr_t attr;
    bool settled;
};

struct ow_conf_barrier {
    struct osw_confsync *cs;
    struct osw_confsync_changed *csc;
    struct ds_dlist waiters;
    ev_idle idle;
    ev_async work;
    pthread_mutex_t lock;
    bool settled;
};

static size_t
ow_conf_barrier_waiters_count(struct ow_conf_barrier *b)
{
    struct ow_conf_barrier_waiter *w;
    size_t n = 0;

    pthread_mutex_lock(&b->lock);
    ds_dlist_foreach(&b->waiters, w)
        n++;
    pthread_mutex_unlock(&b->lock);

    return n;
}

static void
ow_conf_barrier_waiters_notify(struct ow_conf_barrier *b)
{
    struct ow_conf_barrier_waiter *w;

    pthread_mutex_lock(&b->lock);
    ds_dlist_foreach(&b->waiters, w) {
        LOGD("ow: conf barrier: notifying: %p", w);
        w->settled = true;
        WARN_ON(pthread_cond_signal(&w->cond) != 0);
    }
    pthread_mutex_unlock(&b->lock);
}

static void
ow_conf_barrier_confsync_changed(struct ow_conf_barrier *b,
                                 enum osw_confsync_state s)
{
    switch (s) {
    case OSW_CONFSYNC_IDLE:
        b->settled = true;
        ow_conf_barrier_waiters_notify(b);
        break;
    case OSW_CONFSYNC_REQUESTING:
    case OSW_CONFSYNC_WAITING:
    case OSW_CONFSYNC_VERIFYING:
        b->settled = false;
        break;
    }
}
static void
ow_conf_barrier_confsync_changed_cb(struct osw_confsync *cs, void *priv)
{
    struct ow_conf_barrier *b = priv;
    enum osw_confsync_state s = osw_confsync_get_state(cs);
    ow_conf_barrier_confsync_changed(b, s);
}

static void
ow_conf_barrier_idle_cb(EV_P_ ev_idle *arg, int events)
{
    struct ow_conf_barrier *b = container_of(arg, struct ow_conf_barrier, idle);
    ev_idle_stop(EV_A_ arg);
    if (b->settled == true)
        ow_conf_barrier_waiters_notify(b);
}

static void
ow_conf_barrier_work_cb(EV_P_ ev_async *arg, int events)
{
    struct ow_conf_barrier *b = container_of(arg, struct ow_conf_barrier, work);
    ev_idle_start(EV_A_ &b->idle);
}

static void
ow_conf_barrier_init(struct ow_conf_barrier *b)
{
    ev_idle_init(&b->idle, ow_conf_barrier_idle_cb);
    ev_async_init(&b->work, ow_conf_barrier_work_cb);
}

static void
ow_conf_barrier_start(struct ow_conf_barrier *b)
{
    b->csc = osw_confsync_register_changed_fn(b->cs,
                                              __FILE__,
                                              ow_conf_barrier_confsync_changed_cb,
                                              b);
    ev_async_start(EV_DEFAULT_ &b->work);
    ev_unref(EV_DEFAULT);
}

static void
ow_conf_barrier_ts_add_msec(struct timespec *ts, int msec)
{
    int sec = (msec / 1000);
    int nsec = (msec % 1000) * 1000000;
    ts->tv_sec += sec;
    ts->tv_nsec += nsec;
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec += 1;
        ts->tv_nsec %= 1000000000;
    }
}

static int
ow_conf_barrier_wait_priv(struct ow_conf_barrier *b, int timeout_msec)
{
    struct ow_conf_barrier_waiter w = {
        .cond = PTHREAD_COND_INITIALIZER,
    };
    if (WARN_ON(pthread_condattr_init(&w.attr) != 0))
        goto out;
    if (WARN_ON(pthread_condattr_setclock(&w.attr, CLOCK_MONOTONIC) != 0))
        goto out_condattr;
    if (WARN_ON(pthread_cond_init(&w.cond, &w.attr) != 0))
        goto out_condattr;

    LOGD("ow: conf barrier: waiting: %p", &w);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ow_conf_barrier_ts_add_msec(&ts, timeout_msec);

    pthread_mutex_lock(&b->lock);
    ds_dlist_insert_tail(&b->waiters, &w);
    ev_async_send(EV_DEFAULT_ &b->work);
    while (w.settled == false) {
        int err = pthread_cond_timedwait(&w.cond, &b->lock, &ts);
        LOGD("ow: conf barrier: %p: timedwait: %d", &w, err);
        if (err != 0) break;
    }
    LOGD("ow: conf barrier: %p: settled: %d", &w, w.settled);
    ds_dlist_remove(&b->waiters, &w);
    pthread_mutex_unlock(&b->lock);

out_condattr:
    pthread_condattr_destroy(&w.attr);
out:
    LOGD("ow: conf barrier: %p: done", &w);
    /* No need for mutex lock here. Waiter is no longer on
     * barrier list so it can't be modified.
     */
    return w.settled == true ? 0 : EAGAIN;
}

static struct ow_conf_barrier g_ow_conf_barrier = {
    .waiters = DS_DLIST_INIT(struct ow_conf_barrier_waiter, node),
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .settled = false,
};

int
ow_conf_barrier_wait(int timeout_msec)
{
    return ow_conf_barrier_wait_priv(&g_ow_conf_barrier, timeout_msec);
}

struct ow_conf_barrier_ut_t_arg {
    int usleep_before;
    int barrier_wait;
    int expected_err;
};

static void *
ow_conf_barrier_ut_t_cb(void *data)
{
    struct ow_conf_barrier_ut_t_arg *arg = data;
    LOGI("%s: %p: usleeping %d", __func__, arg, arg->usleep_before);
    usleep(arg->usleep_before);
    LOGI("%s: %p: waiting %d", __func__, arg, arg->barrier_wait);
    assert(ow_conf_barrier_wait(arg->barrier_wait) == arg->expected_err);
    LOGI("%s: %p: done", __func__, arg);
    return NULL;
}

OSW_UT(ow_conf_barrier)
{
    struct ow_conf_barrier *b = &g_ow_conf_barrier;
    struct ow_conf_barrier_ut_t_arg a1 = { 0, 100, 0 };
    struct ow_conf_barrier_ut_t_arg a2 = { 0, 200, 0 };
    struct ow_conf_barrier_ut_t_arg a3 = { 0, 100, EAGAIN };
    struct ow_conf_barrier_ut_t_arg a4 = { 0, 100, EAGAIN };
    struct ow_conf_barrier_ut_t_arg a5 = { 0, 900, 0 };
    pthread_t t1;
    pthread_t t2;

    ow_conf_barrier_init(b);
    assert(b->settled == false);

    /* no timeout */
    pthread_create(&t1, NULL, ow_conf_barrier_ut_t_cb, &a1);
    pthread_create(&t2, NULL, ow_conf_barrier_ut_t_cb, &a2);
    while (ow_conf_barrier_waiters_count(b) != 2) {}
    ev_run(EV_DEFAULT_ 0);
    assert(b->settled == false);
    ow_conf_barrier_confsync_changed(b, OSW_CONFSYNC_IDLE);
    while (ow_conf_barrier_waiters_count(b) != 0) {}
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    /* should timeout */
    ow_conf_barrier_confsync_changed(b, OSW_CONFSYNC_REQUESTING);
    pthread_create(&t1, NULL, ow_conf_barrier_ut_t_cb, &a3);
    while (ow_conf_barrier_waiters_count(b) != 1) {}
    ev_run(EV_DEFAULT_ 0);
    assert(b->settled == false);
    usleep(a3.barrier_wait * 1.5 * 1000);
    assert(ow_conf_barrier_waiters_count(b) == 0);
    pthread_join(t1, NULL);

    /* a4 timeout, a5 no timeout */
    ow_conf_barrier_confsync_changed(b, OSW_CONFSYNC_REQUESTING);
    pthread_create(&t1, NULL, ow_conf_barrier_ut_t_cb, &a4);
    pthread_create(&t2, NULL, ow_conf_barrier_ut_t_cb, &a5);
    while (ow_conf_barrier_waiters_count(b) != 2) {}
    ev_run(EV_DEFAULT_ 0);
    assert(b->settled == false);
    usleep(a4.barrier_wait * 1.5 * 1000);
    assert(ow_conf_barrier_waiters_count(b) == 1);
    ow_conf_barrier_confsync_changed(b, OSW_CONFSYNC_IDLE);
    while (ow_conf_barrier_waiters_count(b) != 0) {}
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
}

OSW_MODULE(ow_conf_barrier)
{
    OSW_MODULE_LOAD(osw_confsync);
    struct ow_conf_barrier *b = &g_ow_conf_barrier;
    b->cs = osw_confsync_get();
    ow_conf_barrier_init(b);
    ow_conf_barrier_start(b);
    return NULL;
}
