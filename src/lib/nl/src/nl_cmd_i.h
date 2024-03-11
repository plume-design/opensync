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

#ifndef NL_CMD_I_H_INCLUDED
#define NL_CMD_I_H_INCLUDED

#include <nl_cmd.h>
#include "nl_conn_i.h"

struct nl_cmd {
    struct nl_conn *owner;
    struct ds_tree_node seqno_node;
    struct ds_tree_node pending_tnode;
    struct ds_dlist_node pending_lnode;
    struct ds_dlist_node cmd_node;
    struct nl_msg *in_flight;
    struct nl_msg *pending;
    nl_cmd_response_fn_t *response_fn;
    nl_cmd_completed_fn_t *completed_fn;
    nl_cmd_failed_fn_t *failed_fn;
    void *response_fn_priv;
    void *completed_fn_priv;
    void *failed_fn_priv;
    bool failed;
    char *name;
};

void
nl_cmd_complete(struct nl_cmd *cmd,
                bool cancelling);

void
nl_cmd_receive(struct nl_cmd *cmd,
               struct nl_msg *msg);

void
nl_cmd_failed(struct nl_cmd *cmd,
              struct nlmsgerr *err);

void
nl_cmd_flush(struct nl_cmd *cmd);

struct nl_cmd *
nl_cmd_alloc(void);

#endif /* NL_CMD_I_H_INCLUDED */
