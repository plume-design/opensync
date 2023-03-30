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
#include <rq.h>
#include <const.h>
#include <log.h>

/* unit */
#include <nl_cmd_task.h>

/* private */
#define task_to_t(task) container_of(task, struct nl_cmd_task, task);

static void
nl_cmd_task_run_cb(struct rq_task *task)
{
    struct nl_cmd_task *t = task_to_t(task);

    if (WARN_ON(t->cmd == NULL)) {
        rq_task_complete(task);
        return;
    }

    if (WARN_ON(t->tmpl == NULL)) {
        rq_task_complete(task);
        return;
    }

    WARN_ON(nl_cmd_is_completed(t->cmd) == false);

    struct nlmsghdr *hdr = nlmsg_hdr(t->tmpl);
    struct nl_msg *msg = nlmsg_convert(hdr);
    nl_cmd_set_msg(t->cmd, msg);
}

static void
nl_cmd_task_cancel_cb(struct rq_task *task)
{
    struct nl_cmd_task *t = task_to_t(task);

    nl_cmd_set_msg(t->cmd, NULL);
}

static void
nl_cmd_task_kill_cb(struct rq_task *task)
{
    nl_cmd_task_cancel_cb(task);
}

static void
nl_cmd_task_done_cb(struct nl_cmd *cmd,
                    void *priv)
{
    struct nl_cmd_task *t = priv;
    struct rq_task *task = &t->task;

    rq_task_complete(task);
}

/* public */
void
nl_cmd_task_init(struct nl_cmd_task *t,
                 struct nl_cmd *cmd,
                 struct nl_msg *tmpl)
{
    static const struct rq_task_ops ops = {
        .run_fn = nl_cmd_task_run_cb,
        .cancel_fn = nl_cmd_task_cancel_cb,
        .kill_fn = nl_cmd_task_kill_cb,
    };
    nl_cmd_completed_fn_t *done_cb = nl_cmd_task_done_cb;

    t->task.ops = &ops;
    t->tmpl = tmpl;
    t->cmd = cmd;
    nl_cmd_set_completed_fn(cmd, done_cb, t);
}

void
nl_cmd_task_fini(struct nl_cmd_task *t)
{
    if (t == NULL) {
        return;
    }

    if (t->tmpl != NULL) {
        nlmsg_free(t->tmpl);
        t->tmpl = NULL;
    }

    if (t->task.ops != NULL) {
        rq_task_kill(&t->task);
        t->task.ops = NULL;
    }

    if (t->cmd != NULL) {
        nl_cmd_free(t->cmd);
        t->cmd = NULL;
    }
}
