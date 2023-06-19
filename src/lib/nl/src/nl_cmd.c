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
#include <log.h>
#include <memutil.h>

/* unit */
#include "nl_cmd_i.h"
#include "nl_conn_i.h"

/* protected */
void
nl_cmd_complete(struct nl_cmd *cmd,
                const bool cancelling)
{
    if (nl_cmd_is_completed(cmd)) return;

    LOGT("nl: cmd: %p: completing", cmd);

    if (cmd->in_flight != NULL) {
        if (cancelling) {
            nl_conn_cancel_cmd(cmd);
        }

        ds_tree_remove(&cmd->owner->seqno_tree, cmd);
        nlmsg_free(cmd->in_flight);
        cmd->in_flight = NULL;
    }

    if (cmd->pending != NULL) {
        ds_dlist_remove(&cmd->owner->pending_queue, cmd);
        nlmsg_free(cmd->pending);
        cmd->pending = NULL;
    }

    if (cmd->completed_fn != NULL) {
        cmd->completed_fn(cmd, cmd->completed_fn_priv);
    }
}

void
nl_cmd_receive(struct nl_cmd *cmd,
               struct nl_msg *msg)
{
    if (WARN_ON(cmd->in_flight == NULL)) return;

    LOGT("nl: cmd: %p: response received", cmd);

    if (cmd->response_fn != NULL) {
        cmd->response_fn(cmd, msg, cmd->response_fn_priv);
    }
}

void
nl_cmd_failed(struct nl_cmd *cmd,
              struct nlmsgerr *err)
{
    LOGT("nl: cmd: %p: failed: error=%d", cmd, err->error);

    if (cmd->failed_fn != NULL) {
        cmd->failed_fn(cmd, err, cmd->failed_fn_priv);
    }
    else {
        LOGI("nl: cmd: failed: error=%d", err->error);
    }

    cmd->failed = true;
    nl_cmd_complete(cmd, false);
}

bool
nl_cmd_is_failed(const struct nl_cmd *cmd)
{
    if (WARN_ON(cmd == NULL)) return true;
    return cmd->failed;
}

void
nl_cmd_flush(struct nl_cmd *cmd)
{
    nl_cmd_set_msg(cmd, NULL);
}

struct nl_cmd *
nl_cmd_alloc(void)
{
    struct nl_cmd *cmd = CALLOC(1, sizeof(*cmd));
    return cmd;
}

/* public */
void
nl_cmd_free(struct nl_cmd *cmd)
{
    if (cmd == NULL) return;

    nl_cmd_flush(cmd);
    nl_conn_free_cmd(cmd);
    FREE(cmd);
}

bool
nl_cmd_is_pending(const struct nl_cmd *cmd)
{
    if (cmd == NULL) return false;
    return cmd->pending != NULL;
}

bool
nl_cmd_is_in_flight(const struct nl_cmd *cmd)
{
    if (cmd == NULL) return false;
    return cmd->in_flight != NULL;
}

bool
nl_cmd_is_completed(const struct nl_cmd *cmd)
{
    if (nl_cmd_is_pending(cmd)) return false;
    if (nl_cmd_is_in_flight(cmd)) return false;
    return true;
}

void
nl_cmd_set_msg(struct nl_cmd *cmd,
               struct nl_msg *msg)
{
    /* This cancels and completes any pending or in_flight
     * message. Does nothing otherwise.
     */
    nl_cmd_complete(cmd, true);

    if (msg == NULL) return;

    if (WARN_ON(cmd->owner == NULL)) {
        if (cmd->completed_fn != NULL) {
            cmd->completed_fn(cmd, cmd->completed_fn_priv);
        }
        nlmsg_free(msg);
        return;
    }

    ds_dlist_insert_tail(&cmd->owner->pending_queue, cmd);
    cmd->pending = msg;
    cmd->failed = false;
    nl_conn_tx(cmd->owner);
}

void
nl_cmd_set_response_fn(struct nl_cmd *cmd,
                       nl_cmd_response_fn_t *fn,
                       void *priv)
{
    cmd->response_fn = fn;
    cmd->response_fn_priv = priv;
}

void
nl_cmd_set_completed_fn(struct nl_cmd *cmd,
                        nl_cmd_completed_fn_t *fn,
                        void *priv)
{
    cmd->completed_fn = fn;
    cmd->completed_fn_priv = priv;
}

void
nl_cmd_set_failed_fn(struct nl_cmd *cmd,
                     nl_cmd_failed_fn_t *fn,
                     void *priv)
{
    cmd->failed_fn = fn;
    cmd->failed_fn_priv = priv;
}

bool
nl_cmd_wait(struct nl_cmd *cmd,
            struct timeval *tv)
{
    for (;;) {
        struct nl_conn *conn = cmd->owner;
        if (WARN_ON(conn == NULL)) return false;
        if (nl_cmd_is_completed(cmd)) return true;
        nl_conn_rx_wait(conn, tv);
        const bool more = nl_conn_poll(conn);
        const bool done = (more == false);
        if (done) break;
    }
    return true;
}
