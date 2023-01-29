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

#ifndef NL_CONN_I_H_INCLUDED
#define NL_CONN_I_H_INCLUDED

#include <ds_tree.h>
#include <ds_dlist.h>

#include <nl_conn.h>
#include "nl_cmd_i.h"

struct nl_conn {
    struct nl_sock *sock;
    struct ds_tree seqno_tree;
    struct ds_dlist cmd_list;
    struct ds_dlist pending_queue;
    struct ds_dlist subscription_list;
    struct ds_dlist block_list;
    uint32_t cancelled_seqno;
};

struct nl_conn_subscription {
    struct nl_conn *owner;
    struct ds_dlist_node node;
    nl_conn_subscription_started_fn_t *started_fn;
    nl_conn_subscription_stopped_fn_t *stopped_fn;
    nl_conn_subscription_overrun_fn_t *overrun_fn;
    nl_conn_subscription_event_fn_t *event_fn;
    void *started_fn_priv;
    void *stopped_fn_priv;
    void *overrun_fn_priv;
    void *event_fn_priv;
};

struct nl_conn_block {
    struct nl_conn *owner;
    struct ds_dlist_node node;
    char *name;
};

void
nl_conn_cancel_cmd(struct nl_cmd *cmd);

void
nl_conn_free_cmd(struct nl_cmd *cmd);

bool
nl_conn_rx_wait(struct nl_conn *conn,
                struct timeval *tv);

void
nl_conn_tx(struct nl_conn *conn);

#endif /* NL_CONN_I_H_INCLUDED */
