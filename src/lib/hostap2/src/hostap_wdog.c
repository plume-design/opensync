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

/* 3rd party */
#include <ev.h>

/* opensync */
#include <log.h>
#include <memutil.h>

/* unit */
#include <hostap_sock.h>
#include <hostap_conn.h>
#include <hostap_txq.h>

/* private */
#define HOSTAP_WDOG_TIMEOUT_SEC 5.0
#define HOSTAP_WDOG_PING_SEC 60.0
#define HOSTAP_WDOG_LOG(wdog, fmt, ...) \
    "hostap: wdog: %s: " fmt, \
    hostap_sock_get_path( \
        hostap_conn_get_sock( \
            hostap_conn_ref_get_conn(wdog->ref))), \
    ##__VA_ARGS__

struct hostap_wdog {
    struct hostap_txq *txq;
    struct hostap_txq_req *req;
    struct hostap_conn_ref *ref;
    struct ev_loop *loop;
    ev_timer ping;
    ev_timer timeout;
};

static void
hostap_wdog_free_req(struct hostap_wdog *wdog)
{
    hostap_txq_req_free(wdog->req);
    wdog->req = NULL;
}

static void
hostap_wdog_panic(struct hostap_wdog *wdog)
{
    struct hostap_txq *txq = wdog->txq;
    struct hostap_conn *conn = hostap_txq_get_conn(txq);

    /* This is hoping hostap_ev is used and it'll re-open
     * the socket as soon as it's ev_io calls back after
     * socket is closed.
     */
    LOGI(HOSTAP_WDOG_LOG(wdog, "panicking, resetting conn"));
    hostap_conn_reset(conn);
}

static void
hostap_wdog_timeout_cb(struct ev_loop *loop,
                       struct ev_timer *arg,
                       int events)
{
    struct hostap_wdog *wdog = arg->data;
    LOGI(HOSTAP_WDOG_LOG(wdog, "timed out waiting for pong"));
    hostap_wdog_panic(wdog);
}

static void
hostap_wdog_pong_cb(struct hostap_txq_req *req,
                    void *priv)
{
    struct hostap_wdog *wdog = priv;
    const bool is_pong = hostap_txq_req_reply_starts_with(req, "PONG");
    ev_timer_stop(wdog->loop, &wdog->timeout);
    LOGT(HOSTAP_WDOG_LOG(wdog, "pong called back, disarmed timeout"));
    if (is_pong) {
        hostap_wdog_free_req(wdog);
        LOGT(HOSTAP_WDOG_LOG(wdog, "pong receiving"));
    }
    else {
        const char *reply = NULL;
        size_t reply_len = 0;
        const bool ok = hostap_txq_req_get_reply(wdog->req, &reply, &reply_len);
        /* In an unlikely event of a socket buffer overrun a
         * non-"PONG" response will be received because the
         * request-response pairs would shift. That is a
         * reason to panic and re-start the txq.
         */
        LOGI(HOSTAP_WDOG_LOG(wdog, "pong malformed ('%.*s' len=%zu ok=%d), panicking",
                             (int)reply_len,
                             reply ?: "",
                             reply_len,
                             ok ? 1 : 0));
        hostap_wdog_free_req(wdog);
        hostap_wdog_panic(wdog);
    }
}

static void
hostap_wdog_ping_cb(struct ev_loop *loop,
                    struct ev_timer *arg,
                    int events)
{
    struct hostap_wdog *wdog = arg->data;
    struct hostap_txq *txq = wdog->txq;
    const bool prev_not_finished = (wdog->req != NULL);
    const float at = HOSTAP_WDOG_TIMEOUT_SEC;
    const float repeat = 0;
    ev_timer_stop(wdog->loop, &wdog->timeout);
    if (prev_not_finished) {
        /* This should not happen under norml circumstances
         * with pong timeout being shorter than ping
         * interval. Perhaps if mainloop gets stalled really
         * bandly?
         */
        LOGI(HOSTAP_WDOG_LOG(wdog, "previous ping still ongoing, panicking"));
        hostap_wdog_free_req(wdog);
        hostap_wdog_panic(wdog);
        return;
    }
    LOGT(HOSTAP_WDOG_LOG(wdog, "queuing ping, arming timeout"));
    wdog->req = hostap_txq_request(txq, "PING", hostap_wdog_pong_cb, wdog);
    ev_timer_set(&wdog->timeout, at, repeat);
    ev_timer_start(wdog->loop, &wdog->timeout);
}

static void
hostap_wdog_ref_opened_cb(struct hostap_conn_ref *ref,
                          void *priv)
{
    struct hostap_wdog *wdog = priv;
    const float at = HOSTAP_WDOG_PING_SEC;
    const float repeat = HOSTAP_WDOG_PING_SEC;
    LOGT(HOSTAP_WDOG_LOG(wdog, "socket opened, arming ping timer"));
    ev_timer_set(&wdog->ping, at, repeat);
    ev_timer_start(wdog->loop, &wdog->ping);
}

static void
hostap_wdog_ref_closed_cb(struct hostap_conn_ref *ref,
                          void *priv)
{
    struct hostap_wdog *wdog = priv;
    LOGT(HOSTAP_WDOG_LOG(wdog, "socket closed, disarming everything"));
    hostap_wdog_free_req(wdog);
    ev_timer_stop(wdog->loop, &wdog->ping);
    ev_timer_stop(wdog->loop, &wdog->timeout);
}

static void
hostap_wdog_ref_stopping_cb(struct hostap_conn_ref *ref,
                            void *priv)
{
    struct hostap_wdog *wdog = priv;
    LOGT(HOSTAP_WDOG_LOG(wdog, "socket stopping, unregistering"));
    ref = wdog->ref;
    wdog->ref = NULL;
    hostap_conn_ref_unregister(ref);
}

/* public */
struct hostap_wdog *
hostap_wdog_alloc(struct hostap_txq *txq,
                  struct ev_loop *loop)
{
    static const struct hostap_conn_ref_ops ops = {
        .opened_fn = hostap_wdog_ref_opened_cb,
        .closed_fn = hostap_wdog_ref_closed_cb,
        .stopping_fn = hostap_wdog_ref_stopping_cb,
    };
    struct hostap_wdog *wdog = CALLOC(1, sizeof(*wdog));
    struct hostap_conn *conn = hostap_txq_get_conn(txq);

    ev_init(&wdog->ping, hostap_wdog_ping_cb);
    ev_init(&wdog->timeout, hostap_wdog_timeout_cb);
    wdog->loop = loop;
    wdog->txq = txq;
    wdog->ref = hostap_conn_register_ref(conn, &ops, wdog);
    wdog->ping.data = wdog;
    wdog->timeout.data = wdog;
    LOGT(HOSTAP_WDOG_LOG(wdog, "allocated"));

    return wdog;
}

void
hostap_wdog_free(struct hostap_wdog *wdog)
{
    if (wdog == NULL) return;

    LOGT(HOSTAP_WDOG_LOG(wdog, "freeing"));
    hostap_wdog_free_req(wdog);
    struct hostap_conn_ref *ref = wdog->ref;
    wdog->ref = NULL;
    hostap_conn_ref_unregister(ref);
    FREE(wdog);
}
