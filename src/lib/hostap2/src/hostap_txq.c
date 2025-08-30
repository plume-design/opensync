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

/* libc */
#include <sys/socket.h>

/* opensync */
#include <ds_dlist.h>
#include <memutil.h>
#include <log.h>

/* unit */
#include <hostap_sock.h>
#include <hostap_conn.h>
#include <hostap_txq.h>

/* private */
struct hostap_txq {
    struct hostap_conn_ref *ref;
    struct ds_dlist cmds_queued;
    struct ds_dlist cmds_sent;
    bool running;
    bool stopping;
    bool full;
};

struct hostap_txq_req {
    struct ds_dlist_node node;
    struct ds_dlist *list;
    struct hostap_txq *q;
    char *request;
    size_t request_len;
    char *reply;
    size_t reply_len;
    hostap_txq_req_completed_fn_t *completed_fn;
    void *priv;
    bool freeing;
};

#define LOG_PREFIX_TXQ(txq, ...) LOG_PREFIX_REF(txq->ref, ##__VA_ARGS__)
#define LOG_PREFIX_REF(ref, fmt, ...) \
    "hostap: txq: %s: " fmt, \
    hostap_sock_get_path( \
    hostap_conn_get_sock( \
    hostap_conn_ref_get_conn(ref))), \
    ##__VA_ARGS__

#define LOG_PREFIX_REQ(req, fmt, ...) \
    LOG_PREFIX_TXQ(req->q, \
    "request: %s (len=%zu): " fmt, \
    req->request ?: "", \
    req->request_len, \
    ##__VA_ARGS__)

static bool
hostap_txq_req_send(struct hostap_txq_req *req)
{
    struct hostap_txq *q = req->q;
    if (q == NULL) return false;
    if (q->full) return false;
    if (q->stopping) return false;
    const bool stopped = (q->running == false);
    if (stopped) return false;
    struct hostap_conn *conn = hostap_conn_ref_get_conn(q->ref);
    if (conn == NULL) return false;
    const struct hostap_sock *sock = hostap_conn_get_sock(conn);
    if (sock == NULL) return false;
    const int fd = hostap_sock_get_fd(sock);
    const ssize_t err = send(fd, req->request, req->request_len, MSG_DONTWAIT);
    const bool failed = (err < 0);
    if (failed) return false;
    return true;
}

static void
hostap_txq_push(struct hostap_txq *q)
{
    while (q->full == false) {
        struct hostap_txq_req *req = ds_dlist_remove_head(&q->cmds_queued);
        if (req == NULL) break;

        LOGT(LOG_PREFIX_TXQ(q, "sending: %*s (len=%zu)",
                            (int)req->request_len,
                            req->request,
                            req->request_len));
        const bool sent = hostap_txq_req_send(req);
        if (sent) {
            LOGT(LOG_PREFIX_TXQ(q, "sent"));
            req->list = &q->cmds_sent;
            ds_dlist_insert_tail(req->list, req);
            continue;
        }

        LOGT(LOG_PREFIX_TXQ(q, "full"));
        q->full = true;
        ds_dlist_insert_head(req->list, req);
    }
}

static void
hostap_txq_req_detach(struct hostap_txq_req *req)
{
    if (req == NULL) return;

    req->q = NULL;

    if (req->list != NULL) {
        ds_dlist_remove(req->list, req);
        req->list = NULL;
    }
}

static void
hostap_txq_req_free__(struct hostap_txq_req *req)
{
    if (req == NULL) return;

    FREE(req->request);
    FREE(req->reply);
    FREE(req);
}

static void
hostap_txq_req_complete(struct hostap_txq_req *req,
                        const char *reply,
                        size_t reply_len)
{
    struct hostap_txq *q = req->q;
    if (q == NULL) return;
    LOGT(LOG_PREFIX_REQ(req, "completing: %*s (len=%zu)",
                        (int)reply_len,
                        reply ?: "",
                        reply_len));

    if (reply != NULL) {
        req->reply = STRDUP(reply);
        req->reply_len = reply_len;
    }

    if (req->completed_fn != NULL) {
        req->completed_fn(req, req->priv);
    }

    req->q = NULL;

    if (q) {
        q->full = false;
        hostap_txq_push(q);
    }

    hostap_txq_req_detach(req);

    if (req->freeing) {
        hostap_txq_req_free__(req);
    }
}

static void
hostap_txq_complete(struct hostap_txq *txq,
                    struct ds_dlist *list)
{
    struct hostap_txq_req *req;
    while ((req = ds_dlist_remove_head(list)) != NULL) {
        req->list = NULL;
        hostap_txq_req_complete(req, NULL, 0);
    }
}

static void
hostap_txq_opened_cb(struct hostap_conn_ref *ref,
                     void *priv)
{
    struct hostap_txq *q = priv;
    LOGT(LOG_PREFIX_REF(ref, "connection: opened"));
    q->running = true;
    q->full = false;
    hostap_txq_push(q);
}

static void
hostap_txq_requeue_sent(struct hostap_txq *q)
{
    struct hostap_txq_req *req;
    while ((req = ds_dlist_tail(&q->cmds_sent)) != NULL) {
        if (req->freeing) {
            hostap_txq_req_complete(req, NULL, 0);
        }
        else {
            ds_dlist_remove(&q->cmds_sent, req);
            if (q->stopping) {
                LOGT(LOG_PREFIX_REQ(req, "dropping"));
                req->list = NULL;
                hostap_txq_req_complete(req, NULL, 0);
            }
            else {
                LOGT(LOG_PREFIX_REQ(req, "requeuing"));
                req->list = &q->cmds_queued;
                ds_dlist_insert_head(&q->cmds_queued, req);
            }
        }
    }
}

static void
hostap_txq_closed_cb(struct hostap_conn_ref *ref,
                     void *priv)
{
    struct hostap_txq *q = priv;
    LOGT(LOG_PREFIX_REF(ref, "connection: closed"));
    q->running = false;
    hostap_txq_requeue_sent(q);
}

static void
hostap_txq_stopping_cb(struct hostap_conn_ref *ref,
                       void *priv)
{
    struct hostap_txq *q = priv;
    LOGT(LOG_PREFIX_REF(ref, "connection: stopping"));
    q->stopping = true;
    hostap_txq_complete(q, &q->cmds_queued);
}

static void
hostap_txq_msg_cb(struct hostap_conn_ref *ref,
                  const void *reply,
                  size_t reply_len,
                  bool is_event,
                  void *priv)
{
    LOGT(LOG_PREFIX_REF(ref, "connection: %s: %*s (len=%zu)",
                        is_event ? "event" : "reply",
                        (int)reply_len,
                        (const char *)reply ?: "",
                        reply_len));
    struct hostap_txq *q = priv;
    if (is_event) return;
    struct hostap_txq_req *req = ds_dlist_head(&q->cmds_sent);
    if (req == NULL) return;
    hostap_txq_req_complete(req, reply, reply_len);
}

/* public */
struct hostap_txq *
hostap_txq_alloc(struct hostap_conn *conn)
{
    static const struct hostap_conn_ref_ops ops = {
        .opened_fn = hostap_txq_opened_cb,
        .closed_fn = hostap_txq_closed_cb,
        .stopping_fn = hostap_txq_stopping_cb,
        .msg_fn = hostap_txq_msg_cb,
    };
    struct hostap_txq *q = CALLOC(1, sizeof(*q));
    ds_dlist_init(&q->cmds_queued, struct hostap_txq_req, node);
    ds_dlist_init(&q->cmds_sent, struct hostap_txq_req, node);
    q->ref = hostap_conn_register_ref(conn, &ops, q);
    return q;
}

void
hostap_txq_free(struct hostap_txq *q)
{
    if (q == NULL) return;

    struct hostap_conn_ref *ref = q->ref;
    q->ref = NULL;

    if (ref != NULL) hostap_conn_ref_unregister(ref);
    FREE(q);
}

struct hostap_txq_req *
hostap_txq_request(struct hostap_txq *q,
                   const char *request,
                   hostap_txq_req_completed_fn_t *completed_fn,
                   void *priv)
{
    if (q == NULL) return NULL;

    struct hostap_txq_req *req = CALLOC(1, sizeof(*req));
    req->q = q;
    req->request = STRDUP(request);
    req->request_len = strlen(request);
    req->completed_fn = completed_fn;
    req->priv = priv;
    req->list = &q->cmds_queued;
    ds_dlist_insert_tail(req->list, req);
    hostap_txq_push(q);
    return req;
}

bool
hostap_txq_req_wait(struct hostap_txq_req *req,
                    struct timeval *tv)
{
    if (req == NULL) return false;

    for (;;) {
        if (req->q == NULL) break;
        struct hostap_conn *conn = hostap_conn_ref_get_conn(req->q->ref);
        if (conn == NULL) break;
        struct hostap_sock *sock = hostap_conn_get_sock(conn);
        if (sock == NULL) break;

        const bool ready = hostap_sock_wait_ready(sock, tv);
        const bool error = !ready;
        if (error) break;

        hostap_conn_poll(conn);
    }

    return req->q == NULL;
}

bool
hostap_txq_req_get_reply(struct hostap_txq_req *req,
                         const char **reply,
                         size_t *reply_len)
{
    if (req == NULL) return false;
    if (reply != NULL) *reply = req->reply;
    if (reply_len != NULL) *reply_len = req->reply_len;
    const bool reply_exists = (req->reply != NULL);
    return reply_exists;
}

bool
hostap_txq_req_reply_starts_with(const struct hostap_txq_req *req,
                                 const char *str)
{
    if (req == NULL) return false;
    if (req->reply == NULL) return false;
    const size_t str_len = strlen(str);
    if (str_len > req->reply_len) return false;
    return strncmp(req->reply, str, req->reply_len) == 0;
}

bool
hostap_txq_req_is_reply_ok(struct hostap_txq_req *req)
{
    if (req == NULL) return false;
    return hostap_txq_req_reply_starts_with(req, "OK");
}

bool
hostap_txq_req_is_reply_fail(struct hostap_txq_req *req)
{
    if (req == NULL) return true;
    return hostap_txq_req_reply_starts_with(req, "FAIL");
}

struct hostap_conn *
hostap_txq_get_conn(struct hostap_txq *txq)
{
    if (txq == NULL) return NULL;
    return hostap_conn_ref_get_conn(txq->ref);
}

void
hostap_txq_req_free(struct hostap_txq_req *req)
{
    if (req == NULL) return;

    if (req->q != NULL) {
        const bool defer = (req->list == &req->q->cmds_sent);
        if (defer) {
            req->completed_fn = NULL;
            req->freeing = true;
            return;
        }
    }

    hostap_txq_req_detach(req);
    hostap_txq_req_free__(req);
}
