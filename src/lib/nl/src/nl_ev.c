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

/* 3rd party */
#include <ev.h>

/* opensync */
#include <memutil.h>
#include <log.h>

/* unit */
#include <nl_ev.h>
#include <nl_conn.h>
#include "nl_ev_i.h"

/* private */
#define NL_EV_TX_BLOCK_DURATION_SEC 1.0f

static int
nl_ev_get_conn_fd(struct nl_conn *conn)
{
    if (conn == NULL) return -1;
    struct nl_sock *sock = nl_conn_get_sock(conn);
    if (sock == NULL) return -1;
    const int fd = nl_socket_get_fd(sock);
    return fd;
}

static void
nl_ev_tx_unblock(struct nl_ev *ev)
{
    if (ev->io_loop == NULL) return;
    if (ev->tx_block == NULL) return;
    ev_timer_stop(ev->io_loop, &ev->tx_unblock);
    nl_conn_block_tx_free(ev->tx_block);
    ev->tx_block = NULL;
}

static void
nl_ev_tx_block(struct nl_ev *ev)
{
    if (ev->io_loop == NULL) return;
    nl_ev_tx_unblock(ev);
    /* This grabs a blocking reference hoping that quiescing
     * sending commands for a little will allow the receive
     * storm to settle down. Allowing command submission
     * immediately after overrun was reported risks
     * perpetual overruns.
     */
    const float duration = NL_EV_TX_BLOCK_DURATION_SEC;
    ev->tx_block = nl_conn_block_tx(ev->conn, "nl_ev overrun");
    ev_timer_set(&ev->tx_unblock, duration, 0);
    ev_timer_start(ev->io_loop, &ev->tx_unblock);
}

static void
nl_ev_recalc(struct nl_ev *ev)
{
    const int cur_fd = ev->io.fd;
    const int new_fd = nl_ev_get_conn_fd(ev->conn);
    const bool cur_fd_is_valid = (cur_fd >= 0);
    const bool new_fd_is_valid = (new_fd >= 0);
    const bool running = ev->io_loop != NULL && cur_fd_is_valid;
    const bool ready = ev->loop != NULL && new_fd_is_valid;
    const bool synced = (running == ready && ev->loop == ev->io_loop);

    if (synced) {
        return;
    }

    if (running) {
        LOGI("nl: ev: stopping: fd=%d", ev->io.fd);
        ev_io_stop(ev->io_loop, &ev->io);
        nl_ev_tx_unblock(ev);
        ev->io.fd = -1;
        ev->io_loop = NULL;
    }

    if (ready) {
        LOGI("nl: ev: starting: fd=%d", new_fd);
        ev->io_loop = ev->loop;
        ev->io.fd = new_fd;
        ev_io_start(ev->io_loop, &ev->io);
    }
}

static void
nl_ev_io_cb(struct ev_loop *loop,
            struct ev_io *io,
            int flags)
{
    struct nl_ev *ev = io->data;
    struct nl_conn *conn = ev->conn;
    const int cur_fd = nl_ev_get_conn_fd(conn);

    WARN_ON(conn == NULL);
    WARN_ON(cur_fd != io->fd);
    WARN_ON(cur_fd < 0);

    LOGT("nl: ev: polling");
    nl_conn_poll(conn);
}

static void
nl_ev_tx_unblock_cb(struct ev_loop *loop,
                    struct ev_timer *timer,
                    int flags)
{
    struct nl_ev *ev = timer->data;
    nl_ev_tx_unblock(ev);
}

static void
nl_ev_conn_sub_started_cb(struct nl_conn_subscription *sub,
                          void *priv)
{
    struct nl_ev *ev = priv;
    nl_ev_recalc(ev);
}

static void
nl_ev_conn_sub_stopped_cb(struct nl_conn_subscription *sub,
                          void *priv)
{
    struct nl_ev *ev = priv;
    nl_ev_recalc(ev);
}

static void
nl_ev_conn_sub_overrun_cb(struct nl_conn_subscription *sub,
                          void *priv)
{
    struct nl_ev *ev = priv;
    nl_ev_tx_block(ev);
}

static void
nl_ev_conn_unsubscribe(struct nl_ev *ev)
{
    if (ev->conn_sub == NULL) return;

    LOGT("nl: ev: conn: unsubscribing");
    nl_conn_subscription_free(ev->conn_sub);
    ev->conn_sub = NULL;
}

static void
nl_ev_conn_subscribe(struct nl_ev *ev)
{
    if (ev->conn == NULL) return;
    if (WARN_ON(ev->conn_sub != NULL)) nl_ev_conn_unsubscribe(ev);

    LOGT("nl: ev: conn: subscribing");
    ev->conn_sub = nl_conn_subscription_alloc();
    nl_conn_subscription_set_started_fn(ev->conn_sub, nl_ev_conn_sub_started_cb, ev);
    nl_conn_subscription_set_stopped_fn(ev->conn_sub, nl_ev_conn_sub_stopped_cb, ev);
    nl_conn_subscription_set_overrun_fn(ev->conn_sub, nl_ev_conn_sub_overrun_cb, ev);
    nl_conn_subscription_start(ev->conn_sub, ev->conn);
}

/* public */
struct nl_ev *
nl_ev_alloc(void)
{
    struct nl_ev *ev = CALLOC(1, sizeof(*ev));
    ev_io_init(&ev->io, nl_ev_io_cb, -1, EV_READ);
    ev_timer_init(&ev->tx_unblock, nl_ev_tx_unblock_cb, 1.0, 0.0);
    ev->io.data = ev;
    ev->tx_unblock.data = ev;
    return ev;
}

void
nl_ev_free(struct nl_ev *ev)
{
    nl_ev_set_loop(ev, NULL);
    nl_ev_set_conn(ev, NULL);
    FREE(ev);
}

void
nl_ev_set_loop(struct nl_ev *ev,
               struct ev_loop *loop)
{
    ev->loop = loop;
    nl_ev_recalc(ev);
}

void
nl_ev_set_conn(struct nl_ev *ev,
               struct nl_conn *conn)
{
    nl_ev_conn_unsubscribe(ev);
    ev->conn = conn;
    nl_ev_conn_subscribe(ev);
}

struct ev_loop *
nl_ev_get_loop(struct nl_ev *ev)
{
    return ev->loop;
}
