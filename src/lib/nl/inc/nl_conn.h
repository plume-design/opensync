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

#ifndef NL_CONN_H_INCLUDED
#define NL_CONN_H_INCLUDED

/**
 * @file nl_conn.h
 * @brief nl_socket helper
 *
 * This provides most common socket operations and
 * observations.
 *
 * nl_conn_subscription allows to observe connection getting
 * started and stopped.
 *
 * It must be noted that starting a subscription on top of
 * an already started nl_conn will result in the started_fn
 * getting called. Conversely when stopping subscription
 * while nl_conn is still running, will result in stopped_fn
 * getting called. The intention is to allow symmetrical
 * handling of subscribe-and-get allowing observers to
 * have a single control flow for startup and teardown
 * regardless of the initial state of the observable.
 *
 * nl_conn_poll() is expected to be called to process tx/rx
 * operations. nl_conn_get_sock() can be used to get
 * nl_socket and subsequently the file description.
 *
 * It is recommended to use with nl_ev.
 */

#include <stdbool.h>
#include <nl_cmd.h>

struct nl_conn;
struct nl_conn_subscription;
struct nl_conn_block;

typedef void
nl_conn_subscription_started_fn_t(struct nl_conn_subscription *sub,
                                  void *priv);

typedef void
nl_conn_subscription_stopped_fn_t(struct nl_conn_subscription *sub,
                                  void *priv);

typedef void
nl_conn_subscription_overrun_fn_t(struct nl_conn_subscription *sub,
                                  void *priv);

typedef void
nl_conn_subscription_event_fn_t(struct nl_conn_subscription *sub,
                                struct nl_msg *msg,
                                void *priv);

struct nl_conn *
nl_conn_alloc(void);

void
nl_conn_free(struct nl_conn *conn);

void
nl_conn_stop(struct nl_conn *conn);

bool
nl_conn_start(struct nl_conn *conn);

struct nl_conn_subscription *
nl_conn_subscription_alloc(void);

void
nl_conn_subscription_start(struct nl_conn_subscription *sub,
                           struct nl_conn *conn);

void
nl_conn_subscription_stop(struct nl_conn_subscription *sub);

struct nl_conn *
nl_conn_subscription_get_conn(struct nl_conn_subscription *sub);

void
nl_conn_subscription_free(struct nl_conn_subscription *sub);

void
nl_conn_subscription_set_started_fn(struct nl_conn_subscription *sub,
                                    nl_conn_subscription_started_fn_t *fn,
                                    void *priv);

void
nl_conn_subscription_set_stopped_fn(struct nl_conn_subscription *sub,
                                    nl_conn_subscription_stopped_fn_t *fn,
                                    void *priv);

void
nl_conn_subscription_set_overrun_fn(struct nl_conn_subscription *sub,
                                    nl_conn_subscription_overrun_fn_t *fn,
                                    void *priv);

void
nl_conn_subscription_set_event_fn(struct nl_conn_subscription *sub,
                                  nl_conn_subscription_event_fn_t *fn,
                                  void *priv);

bool
nl_conn_poll(struct nl_conn *conn);

struct nl_cmd *
nl_conn_alloc_cmd(struct nl_conn *conn);

struct nl_sock *
nl_conn_get_sock(const struct nl_conn *conn);

struct nl_conn_block *
nl_conn_block_tx(struct nl_conn *conn,
                 const char *name);

void
nl_conn_block_tx_free(struct nl_conn_block *block);

#endif /* NL_CONN_H_INCLUDED */
