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

#ifndef HOSTAP_CONN_H_INCLUDED
#define HOSTAP_CONN_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <sys/time.h>

struct hostap_conn;
struct hostap_conn_ref;

typedef void
hostap_conn_ref_msg_fn_t(struct hostap_conn_ref *ref,
                         const void *msg,
                         size_t msg_len,
                         bool is_event,
                         void *priv);

typedef void
hostap_conn_ref_opened_fn_t(struct hostap_conn_ref *ref,
                            void *priv);

typedef void
hostap_conn_ref_closed_fn_t(struct hostap_conn_ref *ref,
                            void *priv);

typedef void
hostap_conn_ref_stopping_fn_t(struct hostap_conn_ref *ref,
                              void *priv);

struct hostap_conn_ref_ops {
    hostap_conn_ref_msg_fn_t *msg_fn;
    hostap_conn_ref_opened_fn_t *opened_fn;
    hostap_conn_ref_closed_fn_t *closed_fn;
    hostap_conn_ref_stopping_fn_t *stopping_fn;
};

struct hostap_conn *
hostap_conn_alloc(const char *ctrl_path);

struct hostap_sock *
hostap_conn_get_sock(struct hostap_conn *conn);

bool
hostap_conn_is_opened(struct hostap_conn *conn);

bool
hostap_conn_wait_ready(struct hostap_conn *conn,
                       struct timeval *tv);

void
hostap_conn_poll(struct hostap_conn *conn);

void
hostap_conn_reset(struct hostap_conn *conn);

void
hostap_conn_free(struct hostap_conn *conn);

struct hostap_conn_ref *
hostap_conn_register_ref(struct hostap_conn *conn,
                         const struct hostap_conn_ref_ops *ops,
                         void *priv);

struct hostap_conn *
hostap_conn_ref_get_conn(struct hostap_conn_ref *ref);

void
hostap_conn_ref_unregister(struct hostap_conn_ref *ref);

#endif /* HOSTAP_CONN_H_INCLUDED */
