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
#include <inttypes.h>
#include <errno.h>

/* 3rd party */
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>

/* opensync */
#include <log.h>
#include <memutil.h>
#include <os.h>

/* unit */
#include "nl_conn_i.h"
#include "nl_cmd_i.h"

/* private */
#define SEQNO_EVENT 0
#define SEQNO_UNSPEC 0

static void
nl_conn_free_cmds(struct nl_conn *conn)
{
    const bool stale_commands = (ds_dlist_is_empty(&conn->cmd_list) == false);

    WARN_ON(stale_commands);

    struct nl_cmd *cmd;
    while ((cmd = ds_dlist_remove_head(&conn->cmd_list)) != NULL) {
        nl_cmd_flush(cmd);

        /* Can't really free them. Can unlink them at best and
         * hope their owners free them later.
         */
        nl_conn_free_cmd(cmd);
    }
}

static void
nl_conn_free_subscriptions(struct nl_conn *conn)
{
    const bool stale_subscriptions = (ds_dlist_is_empty(&conn->subscription_list) == false);
    WARN_ON(stale_subscriptions);

    struct nl_conn_subscription *sub;
    while ((sub = ds_dlist_remove_head(&conn->subscription_list)) != NULL) {
        ds_dlist_remove(&conn->subscription_list, sub);
        sub->owner = NULL;
    }
}

static void
nl_conn_block_tx_unlink(struct nl_conn_block *block)
{
    if (block->owner == NULL) return;
    ds_dlist_remove(&block->owner->block_list, block);
    const bool was_last = ds_dlist_is_empty(&block->owner->block_list);
    LOGI("nl: conn: tx: unblocked by: %s", block->name);
    if (was_last) {
        LOGI("nl: conn: tx: resuming");
        nl_conn_tx(block->owner);
    }
}

static void
nl_conn_free_blocks(struct nl_conn *conn)
{
    const bool stale_blocks = (ds_dlist_is_empty(&conn->block_list) == false);
    WARN_ON(stale_blocks);

    struct nl_conn_block *block;
    while ((block = ds_dlist_remove_head(&conn->block_list)) != NULL) {
        nl_conn_block_tx_unlink(block);
    }
}

static void
nl_conn_subscription_notify_init(struct nl_conn_subscription *sub)
{
    if (sub->owner == NULL) return;
    if (sub->started_fn == NULL) return;
    if (sub->owner->sock == NULL) return;

    sub->started_fn(sub, sub->started_fn_priv);
}

static void
nl_conn_subscription_notify_fini(struct nl_conn_subscription *sub)
{
    if (sub->owner == NULL) return;
    if (sub->stopped_fn == NULL) return;
    if (sub->owner->sock == NULL) return;

    sub->stopped_fn(sub, sub->stopped_fn_priv);
}

static void
nl_conn_notify_event(struct nl_conn *conn,
                     struct nl_msg *msg)
{
    struct nl_conn_subscription *i;
    ds_dlist_foreach(&conn->subscription_list, i) {
        if (i->event_fn != NULL) {
            i->event_fn(i, msg, i->event_fn_priv);
        }
    }
}

static void
nl_conn_notify_overrun(struct nl_conn *conn)
{
    struct nl_conn_subscription *i;
    ds_dlist_foreach(&conn->subscription_list, i) {
        if (i->overrun_fn != NULL) {
            i->overrun_fn(i, i->overrun_fn_priv);
        }
    }
}

static void
nl_conn_notify_started(struct nl_conn *conn)
{
    struct nl_conn_subscription *i;
    ds_dlist_foreach(&conn->subscription_list, i) {
        if (i->started_fn != NULL) {
            i->started_fn(i, i->started_fn_priv);
        }
    }
}

static void
nl_conn_notify_stopped(struct nl_conn *conn)
{
    struct nl_conn_subscription *i;
    ds_dlist_foreach(&conn->subscription_list, i) {
        if (i->stopped_fn != NULL) {
            i->stopped_fn(i, i->stopped_fn_priv);
        }
    }
}

static void
nl_conn_discard_in_flight_msg(struct nl_conn *conn)
{
    struct nl_cmd *in_flight = ds_tree_head(&conn->seqno_tree);

    if (in_flight != NULL) {
        struct nlmsgerr err;
        MEMZERO(err);
        err.error = -ENOBUFS;

        LOGI("nl: conn: discarding in flight command");
        nl_cmd_failed(in_flight, &err);
        assert(ds_tree_is_empty(&conn->seqno_tree));
    }

    conn->cancelled_seqno = SEQNO_UNSPEC;
}

static void
nl_conn_handle_overrun(struct nl_conn *conn)
{
    /* There's no way of knowing if the data lost was the
     * Ack, or one of the responses. Just discard any
     * in-flight messages as failed ones.
     */
    LOGI("nl: conn: overrun detected, trying to recover");
    nl_conn_discard_in_flight_msg(conn);
    nl_conn_notify_overrun(conn);
}

static bool
nl_conn_seq_is_ok(struct nl_conn *conn,
                  struct nl_msg *msg)
{
    const uint32_t seqno = nlmsg_hdr(msg)->nlmsg_seq;
    const bool is_event = (seqno == SEQNO_EVENT);
    struct nl_cmd *cmd = ds_tree_find(&conn->seqno_tree, &seqno);
    const bool is_in_flight_cmd = (cmd != NULL);
    const bool is_cancelled_seqno = conn->cancelled_seqno != SEQNO_UNSPEC
                                 && conn->cancelled_seqno == seqno;
    if (is_event) return true;
    if (is_in_flight_cmd) return true;
    if (is_cancelled_seqno) return true;
    return false;
}

static int
nl_conn_seq_cb(struct nl_msg *msg,
               void *priv)
{
    if (nl_conn_seq_is_ok(priv, msg)) return NL_OK;
    return NL_SKIP;
}

static int
nl_conn_msg_cb(struct nl_msg *msg,
               void *priv)
{
    const uint32_t seqno = nlmsg_hdr(msg)->nlmsg_seq;
    const bool is_event = (seqno == SEQNO_EVENT);
    struct nl_conn *conn = priv;
    struct nl_cmd *cmd = ds_tree_find(&conn->seqno_tree, &seqno);
    if (cmd != NULL) {
        nl_cmd_receive(cmd, msg);
    }
    else if (is_event) {
        nl_conn_notify_event(conn, msg);
    }
    return NL_SKIP;
}

static int
nl_conn_complete_cb(struct nl_msg *msg,
                    void *priv)
{
    struct nl_conn *conn = priv;
    const uint32_t seqno = nlmsg_hdr(msg)->nlmsg_seq;
    const bool is_cancelled_seqno = conn->cancelled_seqno != SEQNO_UNSPEC
                                 && conn->cancelled_seqno == seqno;
    struct nl_cmd *cmd = ds_tree_find(&conn->seqno_tree, &seqno);
    if (cmd) {
        nl_cmd_complete(cmd, false);
    }
    else if (is_cancelled_seqno) {
        conn->cancelled_seqno = SEQNO_UNSPEC;
    }
    return NL_SKIP;
}

static int
nl_conn_overrun_cb(struct nl_msg *msg,
                   void *priv)
{
    struct nl_conn *conn = priv;
    nl_conn_handle_overrun(conn);
    return NL_SKIP;
}

static int
nl_conn_err_cb(struct sockaddr_nl *nla,
               struct nlmsgerr *err,
               void *priv)
{
    const uint32_t seqno = err->msg.nlmsg_seq;
    struct nl_conn *conn = priv;
    struct nl_cmd *cmd = ds_tree_find(&conn->seqno_tree, &seqno);
    if (cmd != NULL) {
        nl_cmd_failed(cmd, err);
    }
    return NL_SKIP;
}

static bool
nl_conn_is_tx_blocked(struct nl_conn *conn)
{
    /* FIXME: Netlink is not able to process multiple dump
     * commands at the same time. Other non-dump commands
     * should work fine in parallel though. To keep tx
     * scheduling simple allow only 1 in-flight command.
     *
     * When implementing dump-aware tx scheduling in-flight
     * command cancellation needs to be carefully take into
     * account.
     */
    const bool in_flight = (ds_tree_is_empty(&conn->seqno_tree) == false);
    if (in_flight) return true;

    const bool waiting_for_cancelled_seqno = (conn->cancelled_seqno != SEQNO_UNSPEC);
    if (waiting_for_cancelled_seqno) return true;

    const bool blocks_are_grabbed = (ds_dlist_is_empty(&conn->block_list) == false);
    if (blocks_are_grabbed) return true;

    return false;
}

static bool
nl_conn_tx_try(struct nl_conn *conn)
{
    struct nl_sock *sock = conn->sock;
    if (sock == NULL) {
        return false;
    }

    if (nl_conn_is_tx_blocked(conn)) {
        return false;
    }

    struct nl_cmd *cmd = ds_dlist_remove_head(&conn->pending_queue);
    if (cmd == NULL) {
        return false;
    }

    struct nl_msg *msg = cmd->pending;
    if (WARN_ON(msg == NULL)) {
        return true;
    }

    uint32_t *seqno = &(nlmsg_hdr(msg)->nlmsg_seq);
    WARN_ON(*seqno != NL_AUTO_SEQ);

    for (;;) {
        *seqno = NL_AUTO_SEQ;
        nl_complete_msg(sock, msg);
        const bool is_event = (*seqno == 0);
        const bool no_collision = (ds_tree_find(&conn->seqno_tree, seqno) == NULL);
        if (is_event) break;
        if (no_collision) break;
        LOGI("nl: conn: seqno: %"PRIu32" collided, trying next", *seqno);
    }

    const int err = nl_send(sock, msg);
    const bool failed = (err < 0);
    if (failed) {
        LOGI("nl: conn: failed to send: err=%d", err);
        const bool busy = (err == NLE_AGAIN);
        const bool fatal = (busy == false);
        ds_dlist_insert_tail(&conn->pending_queue, cmd);
        *seqno = 0;
        if (WARN_ON(fatal)) {
            nl_cmd_complete(cmd, true);
        }
        if (busy) {
            return false;
        }
    }

    cmd->in_flight = msg;
    cmd->pending = NULL;
    ds_tree_insert(&conn->seqno_tree, cmd, seqno);

    return true;
}

/* protected */
void
nl_conn_cancel_cmd(struct nl_cmd *cmd)
{
    if (cmd == NULL) return;
    if (cmd->owner == NULL) return;
    if (cmd->in_flight == NULL) return;

    WARN_ON(cmd->owner->cancelled_seqno != SEQNO_UNSPEC);

    const uint32_t seqno = nlmsg_hdr(cmd->in_flight)->nlmsg_seq;
    cmd->owner->cancelled_seqno = seqno;
}

void
nl_conn_free_cmd(struct nl_cmd *cmd)
{
    if (WARN_ON(cmd->owner == NULL)) return;
    ds_dlist_remove(&cmd->owner->cmd_list, cmd);
    cmd->owner = NULL;
}

bool
nl_conn_rx_wait(struct nl_conn *conn,
                struct timeval *tv)
{
    const int fd = nl_socket_get_fd(conn->sock);
    const int max_fd = fd + 1;
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        const int select_err = select(max_fd, &rfds, NULL, NULL, tv);
        const bool again = (select_err < 0 && errno == EINTR)
                        || (select_err < 0 && errno == EAGAIN);
        const bool timed_out = (select_err == 0)
                            && (tv->tv_sec == 0)
                            && (tv->tv_usec == 0);
        const bool fatal = select_err < 0 && !again;
        //const bool ready = !fatal && !again && !timed_out;
        const bool ready = FD_ISSET(fd, &rfds);
        if (fatal) return false;
        if (timed_out) return false;
        if (ready) return true;
        if (again) continue;

        WARN_ON(1);
        break;
    }
    return false;
}

void
nl_conn_tx(struct nl_conn *conn)
{
    for (;;) {
        const bool sent = nl_conn_tx_try(conn);
        const bool done = (sent == false);
        if (done) break;
    }
}

/* public */
struct nl_conn *
nl_conn_alloc(void)
{
    struct nl_conn *conn = CALLOC(1, sizeof(*conn));

    ds_tree_init(&conn->seqno_tree, ds_u32_cmp, struct nl_cmd, seqno_node);
    ds_dlist_init(&conn->cmd_list, struct nl_cmd, cmd_node);
    ds_dlist_init(&conn->pending_queue, struct nl_cmd, pending_lnode);
    ds_dlist_init(&conn->subscription_list, struct nl_conn_subscription, node);
    ds_dlist_init(&conn->block_list, struct nl_conn_block, node);

    return conn;
}

void
nl_conn_free(struct nl_conn *conn)
{
    if (conn == NULL) return;

    nl_conn_stop(conn);
    nl_conn_free_subscriptions(conn);
    nl_conn_free_cmds(conn);
    nl_conn_free_blocks(conn);
    FREE(conn);
}

void
nl_conn_stop(struct nl_conn *conn)
{
    if (conn->sock == NULL) return;

    nl_socket_free(conn->sock);
    conn->sock = NULL;
    nl_conn_notify_stopped(conn);
}

static void
nl_conn_verify_buffer_size(struct nl_sock *s,
                           int expected_size)
{
    const int fd = nl_socket_get_fd(s);
    int rxbuf;
    int txbuf;
    socklen_t rxbuf_size = sizeof(rxbuf);;
    socklen_t txbuf_size = sizeof(txbuf);;

    const int txbuf_rv = getsockopt(fd,
                                     SOL_SOCKET,
                                     SO_SNDBUF,
                                     &txbuf,
                                     &txbuf_size);
    const int rxbuf_rv = getsockopt(fd,
                                     SOL_SOCKET,
                                     SO_RCVBUF,
                                     &rxbuf,
                                     &rxbuf_size);

    const bool txbuf_err = (txbuf_rv < 0);
    const bool rxbuf_err = (rxbuf_rv < 0);
    const bool txbuf_too_small = (txbuf < expected_size);
    const bool rxbuf_too_small = (rxbuf < expected_size);

    WARN_ON(txbuf_err);
    WARN_ON(rxbuf_err);
    if (txbuf_err || rxbuf_err) return;

    if (txbuf_too_small) {
        LOGN("nl: conn: tx buffer size %d < %d, please consider changing /proc/sys/net/core/wmem_max",
             txbuf, expected_size);
    }

    if (rxbuf_too_small) {
        LOGN("nl: conn: rx buffer size %d < %d, please consider changing /proc/sys/net/core/rmem_max",
             rxbuf, expected_size);
    }
}

bool
nl_conn_start(struct nl_conn *conn)
{
    if (conn->sock != NULL) return true;

    conn->sock = nl_socket_alloc();
    if (WARN_ON(conn->sock == NULL)) return false;

    const int connect_err = genl_connect(conn->sock);
    if (connect_err) {
        nl_socket_free(conn->sock);
        conn->sock = NULL;
        return false;
    }

    const int sk_buf_bytes = 2 * 1024 * 1024; /* 2 mega bytes */
    const int sk_rxbuf_bytes = sk_buf_bytes;
    const int sk_txbuf_bytes = sk_buf_bytes;
    const int sk_buf_err = nl_socket_set_buffer_size(conn->sock, sk_rxbuf_bytes, sk_txbuf_bytes);
    if (sk_buf_err < 0) {
        LOGN("nl: conn: failed to set netlink socket buffer size, "
             "expect overrun issues: nl_error=%d errno=%d",
             sk_buf_err,
             errno);
    }

    nl_conn_verify_buffer_size(conn->sock, sk_buf_bytes);

    struct nl_cb *cb = nl_cb_alloc(NL_CB_DEFAULT);
    nl_cb_err(cb, NL_CB_CUSTOM, nl_conn_err_cb, conn);
    nl_cb_set(cb, NL_CB_OVERRUN, NL_CB_CUSTOM, nl_conn_overrun_cb, conn);
    nl_cb_set(cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, nl_conn_seq_cb, conn);
    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, nl_conn_complete_cb, conn);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, nl_conn_complete_cb, conn);
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, nl_conn_msg_cb, conn);
    nl_socket_set_cb(conn->sock, cb);
    nl_cb_put(cb);

    nl_socket_set_nonblocking(conn->sock);
    nl_conn_tx(conn);
    nl_conn_notify_started(conn);

    return true;
}

struct nl_conn_subscription *
nl_conn_subscription_alloc(void)
{
    struct nl_conn_subscription *sub = CALLOC(1, sizeof(*sub));
    return sub;
}

void
nl_conn_subscription_start(struct nl_conn_subscription *sub,
                           struct nl_conn *conn)
{
    if (sub->owner == conn) return;
    if (WARN_ON(sub->owner != NULL)) return;

    sub->owner = conn;
    ds_dlist_insert_tail(&conn->subscription_list, sub);
    nl_conn_subscription_notify_init(sub);
}

void
nl_conn_subscription_stop(struct nl_conn_subscription *sub)
{
    if (sub->owner == NULL) return;

    nl_conn_subscription_notify_fini(sub);
    ds_dlist_remove(&sub->owner->subscription_list, sub);
    sub->owner = NULL;
}

struct nl_conn *
nl_conn_subscription_get_conn(struct nl_conn_subscription *sub)
{
    return sub->owner;
}

void
nl_conn_subscription_free(struct nl_conn_subscription *sub)
{
    if (sub == NULL) return;
    nl_conn_subscription_stop(sub);
    FREE(sub);
}

void
nl_conn_subscription_set_started_fn(struct nl_conn_subscription *sub,
                                    nl_conn_subscription_started_fn_t *fn,
                                    void *priv)
{
    sub->started_fn = fn;
    sub->started_fn_priv = priv;
}

void
nl_conn_subscription_set_stopped_fn(struct nl_conn_subscription *sub,
                                    nl_conn_subscription_stopped_fn_t *fn,
                                    void *priv)
{
    sub->stopped_fn = fn;
    sub->stopped_fn_priv = priv;
}

void
nl_conn_subscription_set_overrun_fn(struct nl_conn_subscription *sub,
                                    nl_conn_subscription_overrun_fn_t *fn,
                                    void *priv)
{
    sub->overrun_fn = fn;
    sub->overrun_fn_priv = priv;
}

void
nl_conn_subscription_set_event_fn(struct nl_conn_subscription *sub,
                                  nl_conn_subscription_event_fn_t *fn,
                                  void *priv)
{
    sub->event_fn = fn;
    sub->event_fn_priv = priv;
}

bool
nl_conn_poll(struct nl_conn *conn)
{
    if (WARN_ON(conn->sock == NULL)) return false;

    nl_conn_tx(conn);
    assert(conn->polling == false);
    conn->polling = true;
    const int err = nl_recvmsgs_default(conn->sock);
    conn->polling = false;
    /* libnl maps ENOBUFS as NLE_NOMEM. NLE_NOMEM can has a
     * few other reasons it might be reporetd, so errno
     * needs to be checked to be sure if it's truly an
     * overrun.
     */
    const bool overrun = (err == -NLE_NOMEM)
                      && (errno == ENOBUFS);
    if (overrun) {
        nl_conn_handle_overrun(conn);
    }

    nl_conn_tx(conn);

    if (err) return false;
    return true;
}

struct nl_cmd *
nl_conn_alloc_cmd(struct nl_conn *conn)
{
    WARN_ON(conn == NULL);
    if (conn == NULL) return NULL;

    struct nl_cmd *cmd = nl_cmd_alloc();
    cmd->owner = conn;
    ds_dlist_insert_tail(&conn->cmd_list, cmd);
    return cmd;
}

struct nl_sock *
nl_conn_get_sock(const struct nl_conn *conn)
{
    if (conn == NULL) return NULL;

    return conn->sock;
}

struct nl_conn_block *
nl_conn_block_tx(struct nl_conn *conn,
                 const char *name)
{
    struct nl_conn_block *block = CALLOC(1, sizeof(*block));
    const bool is_first = (ds_dlist_is_empty(&conn->block_list) == true);
    LOGI("nl: conn: tx: blocked by: %s", name);
    block->owner = conn;
    block->name = STRDUP(name);
    ds_dlist_insert_tail(&conn->block_list, block);
    if (is_first) {
        LOGI("nl: conn: tx: suspending");
    }
    return block;
}

void
nl_conn_block_tx_free(struct nl_conn_block *block)
{
    if (block == NULL) return;
    WARN_ON(block->owner == NULL);
    nl_conn_block_tx_unlink(block);
    FREE(block->name);
    FREE(block);
}
