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
#include <ev.h>
#include <pthread.h>

/* opensync */
#include <ds_dlist.h>
#include <memutil.h>
#include <log.h>
#include <const.h>
#include <osw_ut.h>

/* onewifi */
#include <ow_core.h>
#include <ow_core_thread.h>

struct ow_core_thread {
    bool started;
    bool ready;
    ev_idle idle;
    ev_async cancel;
    ev_async call;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct ds_dlist calls;
};

struct ow_core_thread_call {
    struct ds_dlist_node node;
    ow_core_thread_call_fn_t *fn;
    void *fn_priv;
    void *fn_ret;
    bool done;
};

static void
ow_core_thread_idle_cb(EV_P_ ev_idle *arg, int events)
{
    struct ow_core_thread *t = container_of(arg, struct ow_core_thread, idle);
    LOGI("ow: core thread: ready");
    ev_idle_stop(EV_A_ arg);
    pthread_mutex_lock(&t->lock);
    t->ready = true;
    pthread_cond_signal(&t->cond);
    pthread_mutex_unlock(&t->lock);
}

static void
ow_core_thread_start_ev(EV_P_ struct ow_core_thread *t)
{
    ev_idle_start(EV_A_ &t->idle);
    ev_async_start(EV_A_ &t->cancel);
    ev_unref(EV_A);
    ev_async_start(EV_A_ &t->call);
    ev_unref(EV_A);
}

static void *
ow_core_thread_cb(void *arg)
{
    struct ow_core_thread *t = arg;

    ow_core_init(EV_DEFAULT);
    ow_core_thread_start_ev(EV_DEFAULT_ t);
    LOGI("ow: core thread: running");
    ow_core_run(EV_DEFAULT);
    LOGI("ow: core thread: stopped");

    return NULL;
}

static struct ow_core_thread g_ow_core_thread = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .calls = DS_DLIST_INIT(struct ow_core_thread_call, node),
};

static void
ow_core_thread_cancel_cb(EV_P_ ev_async *arg, int events)
{
    LOGI("ow: core thread: aborting");
    ev_break(EV_A_ EVBREAK_ALL);
}

static void
ow_core_thread_call_work(struct ow_core_thread *t)
{
    struct ow_core_thread_call *c;
    while ((c = ds_dlist_remove_head(&t->calls)) != NULL) {
        LOGD("ow: core thread: calling %p(%p)", c->fn, c->fn_priv);
        assert(c->fn != NULL);
        c->fn_ret = c->fn(c->fn_priv);
        LOGD("ow: core thread: returned %p", c->fn_ret);
        c->done = true;
    }
    pthread_cond_signal(&t->cond);
}

static void
ow_core_thread_call_cb(EV_P_ ev_async *arg, int events)
{
    struct ow_core_thread *t = container_of(arg, struct ow_core_thread, call);
    LOGI("ow: core thread: calling");
    pthread_mutex_lock(&t->lock);
    ow_core_thread_call_work(t);
    pthread_mutex_unlock(&t->lock);
}

void
ow_core_thread_start(void)
{
    struct ow_core_thread *t = &g_ow_core_thread;

    pthread_mutex_lock(&t->lock);
    if (WARN_ON(t->started == true))
        goto unlock;

    ev_async_init(&t->cancel, ow_core_thread_cancel_cb);
    ev_async_init(&t->call, ow_core_thread_call_cb);
    ev_idle_init(&t->idle, ow_core_thread_idle_cb);

    if (WARN_ON(pthread_create(&t->thread, NULL, ow_core_thread_cb, t) != 0))
        goto unlock;

    t->started = true;
    while (t->ready == false)
        pthread_cond_wait(&t->cond, &t->lock);
unlock:
    pthread_mutex_unlock(&t->lock);
}

static void
ow_core_thread_cancel(void)
{
    struct ow_core_thread *t = &g_ow_core_thread;

    assert(pthread_mutex_lock(&t->lock) == 0);
    if (t->started == false)
        goto unlock;
    ev_async_send(EV_DEFAULT_ &t->cancel);
    assert(pthread_join(t->thread, NULL) == 0);
    t->ready = false;
    t->started = false;
unlock:
    assert(pthread_mutex_unlock(&t->lock) == 0);
}

void *
ow_core_thread_call(ow_core_thread_call_fn_t *fn, void *fn_priv)
{
    struct ow_core_thread *t = &g_ow_core_thread;
    struct ow_core_thread_call *c = CALLOC(1, sizeof(*c));
    void *ret;

    c->fn = fn;
    c->fn_priv = fn_priv;
    pthread_mutex_lock(&t->lock);
    ds_dlist_insert_tail(&t->calls, c);
    ev_async_send(EV_DEFAULT_ &t->call);
    while (c->done == false)
        pthread_cond_wait(&t->cond, &t->lock);
    pthread_mutex_unlock(&t->lock);

    ret = c->fn_ret;
    FREE(c);
    return ret;
}

static void *
ow_core_thread_ut_call_1_cb(void *arg)
{
    unsigned int *i = arg;
    LOGI("%s: calling %p", __func__, arg);
    assert(*i == 0x1337);
    *i = 0xbeef;
    LOGI("%s: returning", __func__);
    return arg;
}

OSW_UT(ow_core_thread_ut)
{
    unsigned int num = 0x1337;
    LOGI("%s: starting\n", __func__);
    ow_core_thread_start();
    LOGI("%s: calling\n", __func__);
    unsigned int *ret = ow_core_thread_call(ow_core_thread_ut_call_1_cb, &num);
    assert(ret == &num);
    assert(num == 0xbeef);
    LOGI("%s: canceling\n", __func__);
    ow_core_thread_cancel();
    LOGI("%s: done\n", __func__);
}
