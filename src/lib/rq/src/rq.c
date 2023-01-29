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

#include <rq.h>
#include <assert.h>

#define RQ_CALL(func, ...) do { if ((func) != NULL) (func)(__VA_ARGS__); } while (0)

/* private */
static void
rq_task_timeout_cb(struct ev_loop *loop, ev_timer *arg, int events)
{
    struct rq_task *t = arg->data;
    if (t->cancelled) {
        t->cancel_timed_out = true;
        rq_task_kill(t);
    }
    else {
        t->timed_out = true;
        rq_task_cancel(t);
    }
}

static void
rq_task_set_timeout(struct rq_task *t, int msec)
{
    if (t->q == NULL) {
    }
    else {
        ev_timer_stop(t->q->loop, &t->timeout);

        if (msec > 0) {
            const float sec = (float)msec / 1000.0;
            ev_timer_init(&t->timeout, rq_task_timeout_cb, sec, 0);
            t->timeout.data = t;
            ev_timer_start(t->q->loop, &t->timeout);
        }
    }
}

static bool
rq_is_full(struct rq *q)
{
    const bool is_unlimited = (q->max_running == 0);
    if (is_unlimited) return false;
    if (q->num_running < q->max_running) return false;
    return true;
}

static bool
rq_can_start_next(struct rq *q)
{
    if (q->stopped) return false;
    if (q->empty) return false;
    if (ds_dlist_is_empty(&q->pending)) return false;
    if (rq_is_full(q)) return false;
    return true;
}

static void
rq_start_next(struct rq *q)
{
    struct rq_task *t = ds_dlist_remove_head(&q->pending);
    assert(t->q == q);
    ds_dlist_insert_tail(&q->running, t);
    t->running = true;
    q->num_running++;
    rq_task_set_timeout(t, t->run_timeout_msec);
    if (t->ops != NULL) RQ_CALL(t->ops->run_fn, t);
}

static bool
rq_became_empty(struct rq *q)
{
    if (q->empty) return false;
    if (ds_dlist_is_empty(&q->running) == false) return false;
    if (ds_dlist_is_empty(&q->pending) == false) return false;
    return true;
}

static void
rq_report_empty(struct rq *q)
{
    if (rq_became_empty(q) == false) return;
    q->empty = true;
    RQ_CALL(q->empty_fn, q, q->priv);
}

static void
rq_run(struct rq *q)
{
    while (rq_can_start_next(q)) {
        rq_start_next(q);
    }
    rq_report_empty(q);
}

static void
rq_run_cb(struct ev_loop *loop, ev_timer *arg, int events)
{
    struct rq *q = arg->data;
    rq_run(q);
}

static void
rq_cancel_list(struct ds_dlist *q)
{
    struct rq_task *t;
    struct rq_task *tmp;
    ds_dlist_foreach_safe(q, t, tmp) {
        rq_task_cancel(t);
    }
}

static void
rq_kill_list(struct ds_dlist *q)
{
    struct rq_task *t;
    struct rq_task *tmp;
    ds_dlist_foreach_safe(q, t, tmp) {
        rq_task_kill(t);
    }
}

static void
rq_schedule(struct rq *q)
{
    assert(q->loop != NULL);
    ev_timer_stop(q->loop, &q->run);
    if (q->stopped) return;
    ev_timer_start(q->loop, &q->run);
}

/* public */

void
rq_init(struct rq *q, struct ev_loop *loop)
{
    ds_dlist_init(&q->pending, struct rq_task, node);
    ds_dlist_init(&q->running, struct rq_task, node);
    ev_timer_init(&q->run, rq_run_cb, 0, 0);
    q->run.data = q;
    q->loop = loop;
    q->empty = true;
}

void
rq_fini(struct rq *q)
{
    rq_stop(q);
    rq_kill(q);
    rq_schedule(q);
    rq_report_empty(q);
}

void
rq_cancel_pending(struct rq *q)
{
    rq_cancel_list(&q->pending);
}

void
rq_cancel_running(struct rq *q)
{
    rq_cancel_list(&q->running);
}

void
rq_cancel(struct rq *q)
{
    rq_cancel_pending(q);
    rq_cancel_running(q);
}

void
rq_kill(struct rq *q)
{
    rq_kill_list(&q->running);
    rq_cancel_pending(q);
}

void
rq_stop(struct rq *q)
{
    q->stopped = true;
}

void
rq_resume(struct rq *q)
{
    q->stopped = false;
    rq_schedule(q);
}

void
rq_add_task(struct rq *q, struct rq_task *t)
{
    if (q->stopped) {
        /* nop */
    }
    else {
        if (t->queued) {
            /* nop */
        }
        else {
            ds_dlist_insert_tail(&q->pending, t);
            t->q = q;
            t->queued = true;
            t->running = false;
            t->cancelled = false;
            t->killed = false;
            t->timed_out = false;
            t->cancel_timed_out = false;
            q->empty = false;
            rq_schedule(q);
        }
    }
}

void
rq_task_cancel(struct rq_task *t)
{
    if (t->queued) {
        assert(t->q != NULL);

        if (t->running) {
            t->cancelled = true;
            rq_task_set_timeout(t, t->cancel_timeout_msec);
            if (t->ops != NULL) RQ_CALL(t->ops->cancel_fn, t);
        }
        else {
            rq_task_complete(t);
        }
    }
    else {
        /* nop */
    }
}

void
rq_task_kill(struct rq_task *t)
{
    if (t->queued) {
        if (t->running) {
            t->killed = true;
            if (t->ops != NULL) RQ_CALL(t->ops->kill_fn, t);
        }
        else {
            /* nop */
        }
        rq_task_complete(t);
    }
    else {
        /* nop */
    }
}

void
rq_task_complete(struct rq_task *t)
{
    if (t->queued) {
        struct rq *q = t->q;
        assert(q != NULL);

        if (t->running) {
            q->num_running--;
            ds_dlist_remove(&t->q->running, t);
        }
        else {
            ds_dlist_remove(&t->q->pending, t);
        }

        rq_task_set_timeout(t, 0);
        t->queued = false;
        t->running = false;
        t->q = NULL;
        RQ_CALL(t->completed_fn, t, t->priv);
        rq_schedule(q);
    }
    else {
        /* nop */
    }
}
