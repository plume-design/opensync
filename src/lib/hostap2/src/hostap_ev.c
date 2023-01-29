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
#include <memutil.h>
#include <log.h>

/* unit */
#include <hostap_sock.h>
#include <hostap_conn.h>

/* private */
#define HOSTAP_EV_DEFER_SEC 0.0
#define HOSTAP_EV_RETRY_SEC 5.0

#define HOSTAP_EV_LOG(ev, fmt, ...) \
    "hostap: ev: %s: " fmt, \
    hostap_sock_get_path( \
        hostap_conn_get_sock( \
            hostap_conn_ref_get_conn(ev->ref))), \
    ##__VA_ARGS__

struct hostap_ev {
    struct hostap_conn_ref *ref;
    struct ev_loop *loop;
    ev_timer retry;
    ev_stat stat;
    ev_io io;
};

static void
hostap_ev_retry_stop(struct hostap_ev *ev)
{
    LOGT(HOSTAP_EV_LOG(ev, "retry: stopping"));
    ev_timer_stop(ev->loop, &ev->retry);
    ev_stat_stop(ev->loop, &ev->stat);
}

static void
hostap_ev_retry_start(struct hostap_ev *ev)
{
    LOGT(HOSTAP_EV_LOG(ev, "retry: starting"));

    /* First try needs to be done through a timer because
     * ev_stat doesn't fire on start. All subsequent retry
     * will actually go through ev_stat.
     */
    ev_timer_set(&ev->retry, HOSTAP_EV_DEFER_SEC, 0);
    ev_timer_start(ev->loop, &ev->retry);
    ev_stat_start(ev->loop, &ev->stat);
}

static void
hostap_ev_io_stop(struct hostap_ev *ev)
{
    LOGT(HOSTAP_EV_LOG(ev, "io: stopping"));
    ev_io_stop(ev->loop, &ev->io);
    ev->io.fd = -1;
}

static void
hostap_ev_io_start(struct hostap_ev *ev)
{
    struct hostap_conn *conn = hostap_conn_ref_get_conn(ev->ref);
    struct hostap_sock *sock = hostap_conn_get_sock(conn);
    const int fd = hostap_sock_get_fd(sock);

    if (sock == NULL) return;
    LOGT(HOSTAP_EV_LOG(ev, "io: starting"));
    ev->io.fd = fd;
    ev_io_start(ev->loop, &ev->io);
}

static void
hostap_ev_stop(struct hostap_ev *ev)
{
    LOGT(HOSTAP_EV_LOG(ev, "stopping"));

    struct hostap_conn_ref *ref = ev->ref;
    if (ref == NULL) return;

    ev->ref = NULL;
    hostap_conn_ref_unregister(ref);
    hostap_ev_retry_stop(ev);
}

static void
hostap_ev_opened_cb(struct hostap_conn_ref *ref,
                    void *priv)
{
    struct hostap_ev *ev = priv;
    LOGT(HOSTAP_EV_LOG(ev, "ref: opened"));
    hostap_ev_retry_stop(ev);
    hostap_ev_io_start(ev);
}

static void
hostap_ev_closed_cb(struct hostap_conn_ref *ref,
                    void *priv)
{
    struct hostap_ev *ev = priv;
    LOGT(HOSTAP_EV_LOG(ev, "ref: closed"));
    hostap_ev_io_stop(ev);
    hostap_ev_retry_start(ev);
}

static void
hostap_ev_stopping_cb(struct hostap_conn_ref *ref,
                      void *priv)
{
    struct hostap_ev *ev = priv;
    LOGT(HOSTAP_EV_LOG(ev, "ref: stopping"));
    hostap_ev_stop(ev);
}

static void
hostap_ev_start(struct hostap_ev *ev,
                struct hostap_conn *conn)
{
    static const struct hostap_conn_ref_ops ops = {
        .opened_fn = hostap_ev_opened_cb,
        .closed_fn = hostap_ev_closed_cb,
        .stopping_fn = hostap_ev_stopping_cb,
    };

    LOGT(HOSTAP_EV_LOG(ev, "starting"));
    hostap_ev_retry_start(ev);
    ev->ref = hostap_conn_register_ref(conn, &ops, ev);
}

static void
hostap_ev_retry_cb(struct ev_loop *loop,
                   ev_timer *arg,
                   int events)
{
    struct hostap_ev *ev = arg->data;
    struct hostap_conn *conn = hostap_conn_ref_get_conn(ev->ref);
    LOGT(HOSTAP_EV_LOG(ev, "retrying"));
    hostap_conn_poll(conn);
}

static void
hostap_ev_stat_cb(struct ev_loop *loop,
                  ev_stat *arg,
                  int events)
{
    struct hostap_ev *ev = arg->data;
    struct hostap_conn *conn = hostap_conn_ref_get_conn(ev->ref);
    LOGT(HOSTAP_EV_LOG(ev, "retrying (stat)"));
    hostap_conn_poll(conn);
}

static void
hostap_ev_io_cb(struct ev_loop *loop,
                ev_io *arg,
                int events)
{
    struct hostap_ev *ev = arg->data;
    struct hostap_conn *conn = hostap_conn_ref_get_conn(ev->ref);
    LOGT(HOSTAP_EV_LOG(ev, "polling"));
    hostap_conn_poll(conn);
}

/* public */
struct hostap_ev *
hostap_ev_alloc(struct hostap_conn *conn,
                struct ev_loop *loop)
{
    assert(conn != NULL);
    if (loop == NULL) loop = EV_DEFAULT;

    const struct hostap_sock *sock = hostap_conn_get_sock(conn);
    const char *path = hostap_sock_get_path(sock);
    WARN_ON(path == NULL);

    struct hostap_ev *ev = CALLOC(1, sizeof(*ev));
    ev_timer_init(&ev->retry, hostap_ev_retry_cb, 0, 0);
    ev_io_init(&ev->io, hostap_ev_io_cb, -1, EV_READ);
    ev_stat_init(&ev->stat, hostap_ev_stat_cb, path, HOSTAP_EV_RETRY_SEC);
    ev->loop = loop;
    ev->retry.data = ev;
    ev->io.data = ev;
    ev->stat.data = ev;
    LOGT(HOSTAP_EV_LOG(ev, "allocated"));
    hostap_ev_start(ev, conn);
    return ev;
}

void
hostap_ev_free(struct hostap_ev *ev)
{
    LOGT(HOSTAP_EV_LOG(ev, "freeing"));
    hostap_ev_stop(ev);
    FREE(ev);
}
