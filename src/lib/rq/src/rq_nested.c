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

/* opensync */
#include <const.h>

/* unit */
#include <rq.h>
#include <rq_nested.h>

/* private */
static void
rq_nested_empty_cb(struct rq *q,
                   void *priv)
{
    struct rq_nested *n = container_of(q, struct rq_nested, q);
    rq_task_complete(&n->task);
}

static void
rq_nested_run_cb(struct rq_task *task)
{
    struct rq_nested *n = container_of(task, struct rq_nested, task);

    rq_resume(&n->q);

    /* If there are no pending tasks in the queue then
     * empty_fn will never fire. Make sure to complete the
     * task.
     */
    if (ds_dlist_is_empty(&n->q.pending)) {
        rq_task_complete(&n->task);
        return;
    }
}

static void
rq_nested_cancel_cb(struct rq_task *task)
{
    struct rq_nested *n = container_of(task, struct rq_nested, task);
    rq_cancel(&n->q);
}

static void
rq_nested_kill_cb(struct rq_task *task)
{
    struct rq_nested *n = container_of(task, struct rq_nested, task);
    rq_kill(&n->q);
}

/* public */
void
rq_nested_init(struct rq_nested *n,
               struct ev_loop *loop)
{
    static const struct rq_task_ops ops = {
        .run_fn = rq_nested_run_cb,
        .cancel_fn = rq_nested_cancel_cb,
        .kill_fn = rq_nested_kill_cb,
    };

    n->task.ops = &ops;
    n->q.empty_fn = rq_nested_empty_cb;

    rq_init(&n->q, loop);
}

void
rq_nested_add_nested(struct rq *q,
                     struct rq_nested *n)
{
    rq_add_task(q, &n->task);
}
