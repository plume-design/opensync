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

#ifndef CR_NL_CMD_H_INCLUDED
#define CR_NL_CMD_H_INCLUDED

#include <cr.h>
#include <netlink/genl/genl.h>
#include <netlink/netlink.h>
#include <stdbool.h>

enum cr_nl_cmd_status
{
    CR_NL_CMD_NO_SOCKET,
    CR_NL_CMD_NO_LINK,
    CR_NL_CMD_SEND_FAILED,
    CR_NL_CMD_RECV_FAILED,
    CR_NL_CMD_FAILED,
    CR_NL_CMD_RUNNING,
    CR_NL_CMD_ACKED,
};

struct cr_nl_cmd;
typedef struct cr_nl_cmd cr_nl_cmd_t;

cr_nl_cmd_t *cr_nl_cmd(cr_context_t *context, int protocol, struct nl_msg *msg);

void cr_nl_cmd_drop(cr_nl_cmd_t **data);

bool cr_nl_cmd_run(cr_nl_cmd_t *data);

struct nl_msg *cr_nl_cmd_resp(cr_nl_cmd_t *cmd);

struct nl_msg **cr_nl_cmd_resps(cr_nl_cmd_t *cmd);

bool cr_nl_cmd_is_ok(cr_nl_cmd_t *data);

void cr_nl_cmd_log(cr_nl_cmd_t *data, char *buf, size_t buf_len);

void cr_nl_cmd_set_name(cr_nl_cmd_t *cmd, const char *name);

static inline cr_task_t *cr_nl_cmd_into_task(cr_nl_cmd_t **cmd)
{
    if (cmd == NULL) return NULL;
    if (*cmd == NULL) return NULL;
    cr_nl_cmd_t *owned = *cmd;
    *cmd = NULL;
    return cr_task(owned, (cr_run_fn_t *)cr_nl_cmd_run, (cr_drop_fn_t *)cr_nl_cmd_drop);
}

#endif /* CR_NL_CMD_H_INCLUDED */
