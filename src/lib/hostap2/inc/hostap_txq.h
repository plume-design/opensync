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

#ifndef HOSTAP_TXQ_H_INCLUDED
#define HOSTAP_TXQ_H_INCLUDED

#include <hostap_conn.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

struct hostap_txq;
struct hostap_txq_req;

typedef void
hostap_txq_req_completed_fn_t(struct hostap_txq_req *req,
                              void *priv);

struct hostap_txq *
hostap_txq_alloc(struct hostap_conn *conn);

void
hostap_txq_free(struct hostap_txq *q);

struct hostap_txq_req *
hostap_txq_request(struct hostap_txq *q,
                   const char *cmd,
                   hostap_txq_req_completed_fn_t *completed_fn,
                   void *priv);

bool
hostap_txq_req_get_reply(struct hostap_txq_req *req,
                         const char **reply,
                         size_t *reply_len);

bool
hostap_txq_req_reply_starts_with(const struct hostap_txq_req *req,
                                 const char *str);

bool
hostap_txq_req_is_reply_ok(struct hostap_txq_req *req);

bool
hostap_txq_req_is_reply_fail(struct hostap_txq_req *req);

struct hostap_conn *
hostap_txq_get_conn(struct hostap_txq *txq);

void
hostap_txq_req_free(struct hostap_txq_req *req);

#endif /* HOSTAP_TXQ_H_INCLUDED */
