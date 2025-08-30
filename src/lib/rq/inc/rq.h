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

#ifndef RQ_H_INCLUDED
#define RQ_H_INCLUDED

#include <ds_dlist.h>
#include <stdbool.h>
#include <ev.h>

/**
 * Runqueue
 *
 * This helper is intended to help managing groups
 * of async operations that either need to be
 * serialized and/or waited for as a whole.
 */

struct rq;
struct rq_task;

/** Called whenever runqueue runs out of tasks. Can be used
 *  to use as a runqueue completion indication.
 */
typedef void
rq_empty_fn_t(struct rq *rq, void *priv);

/** Called when task is picked up from the pending queue and
 *  started. The task can signal its completion by calling
 *  rq_task_complete(). The task can be stopped by an
 *  automatic timeout (if defined).
 */
typedef void
rq_task_run_fn_t(struct rq_task *task);

/** Called when task needs to be cancelled. This is graceful
 *  stopping procedure and the task is still expected to
 *  call rq_task_complete(). Can happen when explicitly
 *  rq_task_cancel() is called, or when automatic
 *  run_timeout_msec elapses after starting.
 */
typedef void
rq_task_cancel_fn_t(struct rq_task *task);

/** Called when task needs to be killed without waiting for
 *  anything. The task will immediately be
 *  rq_task_complete() too. Can be either explicitly killed
 *  via rq_task_kill() or when cancel_timeout_msec elapses.
 */
typedef void
rq_task_kill_fn_t(struct rq_task *task);

/** Called when task is retired from an rq. This can be
 *  called before run callback is called.
 */
typedef void
rq_task_completed_fn_t(struct rq_task *task, void *priv);

struct rq {
    /* internal */
    struct ds_dlist running;
    struct ds_dlist pending;
    bool stopped;
    bool empty;
    int num_running;
    struct ev_loop *loop;
    ev_timer run;

    /* caller configurable */
    int max_running; /**< max number of concurrent tasks, 0=unlimited */
    rq_empty_fn_t *empty_fn;
    void *priv; /**< pointer passed to callback(s) */
};

/* This is expected to be used as follows:
 *   struct my_task {
 *     struct rq_task task;
 *     bool my_bool;
 *   };
 *   // ...
 *   static void my_task_run_cb(struct rq_task *t) {
 *     struct my_task *mt = container_of(t, struct my_task, task);
 *     mt->my_bool = true;
 *   }
 *   // ...
 *   void my_task_init(struct my_task *mt) {
 *     static const struct rq_task_ops ops = {
 *       .run_cb = my_task_run_cb,
 *       .cancel_cb = my_task_cancel_cb,
 *     };
 *     mt->task.ops = &ops;
 *   }
 */
struct rq_task_ops {
    rq_task_run_fn_t *run_fn;
    rq_task_cancel_fn_t *cancel_fn;
    rq_task_kill_fn_t *kill_fn;
};

struct rq_task {
    /* internal */
    struct rq *q;
    struct ds_dlist_node node;
    ev_timer timeout;
    bool queued;
    bool running;
    bool cancelled;
    bool killed;
    bool completed;
    bool timed_out;
    bool cancel_timed_out;

    /* caller configurable */
    const struct rq_task_ops *ops;
    int run_timeout_msec; /**< 0=no timeout, time to call rq_task_complete() after run_fn() */
    int cancel_timeout_msec; /**< 0=no timeout, time to call rq_task_complete() after cancel_fn() */
    rq_task_completed_fn_t *completed_fn;
    void *priv; /**< pointer passed to callback(s) */
};

void
rq_init(struct rq *q, struct ev_loop *loop);

void
rq_fini(struct rq *q);

void
rq_cancel_pending(struct rq *q);

void
rq_cancel_running(struct rq *q);

void
rq_cancel(struct rq *q);

void
rq_kill(struct rq *q);

void
rq_stop(struct rq *q);

void
rq_resume(struct rq *q);

void
rq_add_task(struct rq *q, struct rq_task *t);

void
rq_task_cancel(struct rq_task *t);

void
rq_task_kill(struct rq_task *t);

void
rq_task_complete(struct rq_task *t);

#endif /* RQ_H_INCLUDED */
