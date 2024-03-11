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

#include <cr.h>
#include <cr_goto.h>
#include <cr_nl_cmd.h>
#include <cr_sleep.h>
#include <errno.h>
#include <log.h>
#include <memutil.h>
#include <util.h>

#define CR_NL_CMD_TIMEOUT_MSEC (10 * 1000)

struct cr_nl_cmd
{
    cr_poll_t *poll;
    cr_sleep_t *timeout;
    cr_context_t *context;
    cr_state_t state_timeout;
    cr_state_t state;
    char *name;
    struct nl_msg *msg;
    struct nl_msg **msgs;
    struct nl_sock *sock;
    struct nl_cb *cb;
    bool finished;
    bool ok;
    int protocol;
    int error;
    int connect_err;
    int send_err;
    int recv_err;
    int budget_max;
    int budget;
};

static int cr_nl_cmd_error_cb(struct sockaddr_nl *nla, struct nlmsgerr *err, void *priv)
{
    cr_nl_cmd_t *cmd = priv;
    cmd->finished = true;
    cmd->error = err->error;
    return NL_SKIP;
}

static int cr_nl_cmd_finish_cb(struct nl_msg *msg, void *priv)
{
    cr_nl_cmd_t *cmd = priv;
    cmd->finished = true;
    cmd->ok = true;
    return NL_SKIP;
}

static int cr_nl_cmd_msg_cb(struct nl_msg *msg, void *priv)
{
    cr_nl_cmd_t *cmd = priv;
    nlmsg_get(msg);
    struct nl_msg **msgs = cmd->msgs;
    size_t n = 0;
    if (msgs != NULL)
    {
        while (msgs[0] != NULL && msgs++)
        {
            n++;
        }
    }
    cmd->msgs = REALLOC(cmd->msgs, sizeof(cmd->msgs[0]) * (n + 2));
    cmd->msgs[n + 0] = msg;
    cmd->msgs[n + 1] = NULL;
    return NL_SKIP;
}

static void cr_nl_cmd_open(cr_nl_cmd_t *cmd)
{
    if (cmd->sock != NULL) return;

    cmd->sock = nl_socket_alloc();
    if (cmd->sock == NULL) return;

    cmd->connect_err = nl_connect(cmd->sock, cmd->protocol);
    if (cmd->connect_err < 0) return;
}

static void cr_nl_cmd_send(cr_nl_cmd_t *cmd)
{
    if (cmd->sock == NULL) return;

    cmd->cb = nl_cb_alloc(NL_CB_DEFAULT);
    nl_cb_err(cmd->cb, NL_CB_CUSTOM, cr_nl_cmd_error_cb, cmd);
    nl_cb_set(cmd->cb, NL_CB_ACK, NL_CB_CUSTOM, cr_nl_cmd_finish_cb, cmd);
    nl_cb_set(cmd->cb, NL_CB_FINISH, NL_CB_CUSTOM, cr_nl_cmd_finish_cb, cmd);
    nl_cb_set(cmd->cb, NL_CB_VALID, NL_CB_CUSTOM, cr_nl_cmd_msg_cb, cmd);
    nl_socket_set_nonblocking(cmd->sock);
    cmd->send_err = nl_send_auto_complete(cmd->sock, cmd->msg);
}

static bool cr_nl_cmd_run_notimeout(cr_nl_cmd_t *cmd)
{
    CR_BEGIN(&cmd->state);

    cr_nl_cmd_open(cmd);
    cr_nl_cmd_send(cmd);

    if (cmd->send_err <= 0) goto end;

    while (cmd->finished == false)
    {
        for (cmd->budget = cmd->budget_max; cmd->budget > 0; cmd->budget--)
        {
            cmd->recv_err = nl_recvmsgs(cmd->sock, cmd->cb);

            const bool not_ready = (cmd->recv_err == -NLE_AGAIN);
            const bool overrun = (cmd->recv_err == -NLE_NOMEM) && (errno == ENOBUFS);
            const bool error = (cmd->recv_err < 0);
            WARN_ON(overrun);

            if (not_ready)
            {
                const int fd = nl_socket_get_fd(cmd->sock);
                cr_poll_drop(&cmd->poll);
                cmd->poll = cr_poll_read(cmd->context, fd);
                while (cr_poll_run(cmd->poll) == false)
                {
                    CR_YIELD(&cmd->state);
                }
            }
            else if (overrun)
            {
                /* oops */
            }
            else if (error)
            {
                cmd->finished = true;
                break;
            }
        }
    }

end:
    CR_END(&cmd->state);
}

bool cr_nl_cmd_run(cr_nl_cmd_t *cmd)
{
    CR_BEGIN(&cmd->state_timeout);
    while (cr_nl_cmd_run_notimeout(cmd) == false)
    {
        if (cr_sleep_run(cmd->timeout) == false)
        {
            CR_YIELD(&cmd->state_timeout);
        }
        else
        {
            break;
        }
    }
    CR_END(&cmd->state_timeout);
}

static void cr_nl_cmd_drop_ptr(cr_nl_cmd_t *cmd)
{
    if (cmd == NULL) return;
    if (cmd->msgs)
    {
        struct nl_msg **msgs = cmd->msgs;
        while (*msgs != NULL)
        {
            nlmsg_free(*msgs);
            msgs++;
        }
        FREE(cmd->msgs);
    }
    cr_poll_drop(&cmd->poll);
    cr_sleep_drop(&cmd->timeout);
    cr_nl_cmd_set_name(cmd, NULL);
    if (cmd->cb != NULL) nl_cb_put(cmd->cb);
    if (cmd->msg != NULL) nlmsg_free(cmd->msg);
    if (cmd->sock != NULL) nl_socket_free(cmd->sock);
    FREE(cmd);
}

void cr_nl_cmd_drop(cr_nl_cmd_t **cmd)
{
    if (cmd == NULL) return;
    cr_nl_cmd_drop_ptr(*cmd);
    *cmd = NULL;
}

cr_nl_cmd_t *cr_nl_cmd(cr_context_t *context, int protocol, struct nl_msg *msg)
{
    if (msg == NULL) return NULL;
    cr_nl_cmd_t *cmd = CALLOC(1, sizeof(*cmd));
    cr_state_init(&cmd->state);
    cr_state_init(&cmd->state_timeout);
    cmd->context = context;
    cmd->msg = msg;
    cmd->timeout = cr_sleep_msec(context, CR_NL_CMD_TIMEOUT_MSEC);
    cmd->protocol = protocol;
    cmd->budget_max = 64;
    return cmd;
}

bool cr_nl_cmd_is_ok(cr_nl_cmd_t *cmd)
{
    return cmd->ok;
}

void cr_nl_cmd_log(cr_nl_cmd_t *cmd, char *buf, size_t buf_len)
{
    const char *name = cmd->name ?: "";
    if (cmd == NULL)
    {
        if (buf_len > 0) buf[0] = 0;
    }
    else if (cmd->sock == NULL)
    {
        snprintf(buf, buf_len, "%sfailed to open socket", name);
    }
    else if (cmd->connect_err != 0)
    {
        snprintf(buf, buf_len, "%sfailed to connect", name);
    }
    else if (cmd->send_err < 0)
    {
        snprintf(buf, buf_len, "%sfailed to send: %d", name, cmd->send_err);
    }
    else if (cmd->recv_err < 0)
    {
        snprintf(buf, buf_len, "%sfailed to send: %d", name, cmd->send_err);
    }
    else if (cmd->error != 0)
    {
        snprintf(buf, buf_len, "%serror response: %d", name, cmd->error);
    }
    else if (cmd->finished != 0)
    {
        snprintf(buf, buf_len, "%stimed out", name);
    }
    else
    {
        assert(cmd->ok == true);
        size_t n = 0;
        struct nl_msg **msg;
        for (msg = cmd->msgs; msg && *msg; msg++)
            n++;
        snprintf(buf, buf_len, "%sacked, %zu responses", name, n);
    }
}

void cr_nl_cmd_set_name(cr_nl_cmd_t *cmd, const char *name)
{
    FREE(cmd->name);
    cmd->name = NULL;
    if (name != NULL)
    {
        cmd->name = strfmt("%s: ", name);
    }
}

struct nl_msg *cr_nl_cmd_resp(cr_nl_cmd_t *cmd)
{
    if (cmd == NULL) return NULL;
    if (cmd->msgs == NULL) return NULL;
    return cmd->msgs[0];
}

struct nl_msg **cr_nl_cmd_resps(cr_nl_cmd_t *cmd)
{
    if (cmd == NULL) return NULL;
    return cmd->msgs;
}
