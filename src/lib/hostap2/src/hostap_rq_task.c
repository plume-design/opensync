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
#include <memutil.h>
#include <log.h>
#include <const.h>

/* unit */
#include <hostap_txq.h>
#include <hostap_sock.h>
#include <hostap_rq_task.h>

/* private */
#define HOSTAP_RQ_TASK_RUN_TIMEOUT_MSEC 10000
#define LOG_PREFIX_TASK(task, fmt, ...) \
    "hostap: rq: task: %s: %s: " fmt, \
    hostap_sock_get_path( \
    hostap_conn_get_sock( \
    hostap_txq_get_conn(task->txq))), \
    task->request, \
    ##__VA_ARGS__

static void
hostap_rq_task_cleanup(struct hostap_rq_task *task)
{
    hostap_txq_req_free(task->req);
    task->reply = NULL;
    task->req = NULL;
}

static void
hostap_rq_task_completed_cb(struct hostap_txq_req *req,
                            void *priv)
{
    struct hostap_rq_task *t = priv;
    t->reply = NULL;
    t->reply_len = 0;
    hostap_txq_req_get_reply(req, &t->reply, &t->reply_len);
    LOGT(LOG_PREFIX_TASK(t, "completing: %*s, (len=%zu)",
                         (int)t->reply_len,
                         (const char *)t->reply ?: "",
                         t->reply_len));
    rq_task_complete(&t->task);
}

static void
hostap_rq_task_run_cb(struct rq_task *task)
{
    struct hostap_rq_task *t = container_of(task, struct hostap_rq_task, task);
    hostap_rq_task_cleanup(t);
    LOGT(LOG_PREFIX_TASK(t, "requesting"));
    t->req = hostap_txq_request(t->txq, t->request, hostap_rq_task_completed_cb, t);
}

static void
hostap_rq_task_cancel_cb(struct rq_task *task)
{
    struct hostap_rq_task *t = container_of(task, struct hostap_rq_task, task);
    LOGT(LOG_PREFIX_TASK(t, "cancelling"));
    hostap_rq_task_cleanup(t);
    rq_task_complete(&t->task);
}

static void
hostap_rq_task_kill_cb(struct rq_task *task)
{
    struct hostap_rq_task *t = container_of(task, struct hostap_rq_task, task);
    LOGT(LOG_PREFIX_TASK(t, "killing"));
    hostap_rq_task_cleanup(t);
    rq_task_complete(&t->task);
}

/* public */
void
hostap_rq_task_init(struct hostap_rq_task *task,
                    struct hostap_txq *txq,
                    const char *request)
{
    static const struct rq_task_ops ops = {
        .run_fn = hostap_rq_task_run_cb,
        .cancel_fn = hostap_rq_task_cancel_cb,
        .kill_fn = hostap_rq_task_kill_cb,
    };

    task->request = STRDUP(request);
    task->txq = txq;
    task->task.ops = &ops;

    /* Automatically cancel if rq_task_complete() is not
     * called within timeout after run_fn().
     */
    task->task.run_timeout_msec = HOSTAP_RQ_TASK_RUN_TIMEOUT_MSEC;
}

void
hostap_rq_task_fini(struct hostap_rq_task *task)
{
    hostap_rq_task_cleanup(task);
    FREE(task->request);
    memset(task, 0, sizeof(*task));
}
