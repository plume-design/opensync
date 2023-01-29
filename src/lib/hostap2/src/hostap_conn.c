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
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

/* opensync */
#include <ds_dlist.h>
#include <memutil.h>
#include <util.h>
#include <log.h>

/* unit */
#include <hostap_sock.h>
#include <hostap_conn.h>

/* private */
struct hostap_conn {
    struct hostap_sock *sock;
    struct ds_dlist refs;
    int rx_budget;
    bool stopping;
};

struct hostap_conn_ref {
    struct ds_dlist_node node;
    struct hostap_conn *conn;
    const struct hostap_conn_ref_ops *ops;
    void *priv;
};

#define HOSTAP_CONN_RX_BUDGET_DEFAULT 32

#define HOSTAP_CONN_REF_NOTIFY(ref, fn, ...) \
    do { \
        if ((ref)->ops->fn != NULL) \
            (ref)->ops->fn((ref), ##__VA_ARGS__, (ref)->priv); \
    } while (0)

#define HOSTAP_CONN_NOTIFY(conn, fn, ...) \
    do { \
        struct hostap_conn_ref *ref; \
        ds_dlist_foreach(&(conn)->refs, ref) \
            HOSTAP_CONN_REF_NOTIFY(ref, fn, ##__VA_ARGS__); \
    } while (0)

#define HOSTAP_CONN_LOG(conn, fmt, ...) \
    "hostap: conn: %s: " fmt, \
    hostap_sock_get_path(conn->sock), \
    ##__VA_ARGS__

static void
hostap_conn_try_open(struct hostap_conn *conn)
{
    if (conn->stopping) return;
    if (hostap_sock_is_opened(conn->sock)) return;

    const int err = hostap_sock_open(conn->sock);
    const bool ok = !err;
    if (ok) {
        HOSTAP_CONN_NOTIFY(conn, opened_fn);
    }
}

static void
hostap_conn_close(struct hostap_conn *conn)
{
    if (hostap_sock_is_closed(conn->sock)) return;

    const int err = hostap_sock_close(conn->sock);
    const bool ok = !err;
    if (ok) {
        HOSTAP_CONN_NOTIFY(conn, closed_fn);
    }
}

static void
hostap_conn_try_close(struct hostap_conn *conn)
{
    if (conn->stopping == false) return;

    hostap_conn_close(conn);
}

static bool
hostap_conn_is_terminating(const char *msg)
{
    if (msg[0] != '<') return false;
    msg = strchr(msg, '>');
    if (msg == NULL) return false;
    msg++;
    return strstr(msg, "CTRL-EVENT-TERMINATING") == msg;
}

static bool
hostap_conn_try_rx_one(struct hostap_conn *conn)
{
    if (hostap_sock_is_closed(conn->sock)) return true;

    size_t len;
    bool is_event;
    char *msg;
    const bool ok = hostap_sock_get_msg(conn->sock, &msg, &len, &is_event);
    const bool no_data = !ok;
    if (no_data) return true;

    HOSTAP_CONN_NOTIFY(conn, msg_fn, msg, len, is_event);

    if (is_event && hostap_conn_is_terminating(msg)) {
        hostap_conn_close(conn);
    }

    FREE(msg);
    return false;
}

static void
hostap_conn_try_rx(struct hostap_conn *conn)
{
    int budget = conn->rx_budget;
    while (budget-- > 0) {
        const bool done = hostap_conn_try_rx_one(conn);
        if (done) break;
    }
}

static void
hostap_conn_ref_attach(struct hostap_conn_ref *ref,
                       struct hostap_conn *conn)
{
    assert(ref->conn == NULL);

    ref->conn = conn;
    ds_dlist_insert_tail(&conn->refs, ref);

    if (hostap_sock_is_opened(conn->sock)) {
        if (ref->ops->opened_fn != NULL) {
            ref->ops->opened_fn(ref, ref->priv);
        }
    }
}

static void
hostap_conn_ref_detach(struct hostap_conn_ref *ref)
{
    struct hostap_conn *conn = ref->conn;
    if (conn == NULL) return;

    ref->conn = NULL;
    ds_dlist_remove(&conn->refs, ref);
    HOSTAP_CONN_REF_NOTIFY(ref, stopping_fn);

    if (hostap_sock_is_opened(conn->sock)) {
        if (ref->ops->closed_fn != NULL) {
            ref->ops->closed_fn(ref, ref->priv);
        }
    }
}

static void
hostap_conn_ref_orphan(struct hostap_conn *conn)
{
    struct hostap_conn_ref *ref;
    while ((ref = ds_dlist_head(&conn->refs)) != NULL) {
        hostap_conn_ref_detach(ref);
    }
}

/* public */
struct hostap_conn *
hostap_conn_alloc(const char *ctrl_path)
{
    struct hostap_conn *conn = CALLOC(1, sizeof(*conn));
    ds_dlist_init(&conn->refs, struct hostap_conn_ref, node);
    conn->rx_budget = HOSTAP_CONN_RX_BUDGET_DEFAULT;
    conn->sock = hostap_sock_alloc(ctrl_path);
    LOGT(HOSTAP_CONN_LOG(conn, "allocated"));
    return conn;
}

void
hostap_conn_free(struct hostap_conn *conn)
{
    if (conn == NULL) return;
    if (conn->stopping) return;

    LOGT(HOSTAP_CONN_LOG(conn, "freeing"));
    conn->stopping = true;
    HOSTAP_CONN_NOTIFY(conn, stopping_fn);
    hostap_conn_poll(conn);
    hostap_conn_ref_orphan(conn);
    hostap_sock_free(conn->sock);
    FREE(conn);
}

struct hostap_sock *
hostap_conn_get_sock(struct hostap_conn *conn)
{
    if (conn == NULL) return NULL;
    return conn->sock;
}

bool
hostap_conn_is_opened(struct hostap_conn *conn)
{
    return hostap_sock_is_opened(hostap_conn_get_sock(conn));
}

void
hostap_conn_poll(struct hostap_conn *conn)
{
    if (conn == NULL) return;

    LOGT(HOSTAP_CONN_LOG(conn, "polling"));
    hostap_conn_try_open(conn);
    hostap_conn_try_rx(conn);
    hostap_conn_try_close(conn);
}

void
hostap_conn_reset(struct hostap_conn *conn)
{
    if (conn == NULL) return;

    LOGT(HOSTAP_CONN_LOG(conn, "resetting"));
    hostap_conn_close(conn);
}

struct hostap_conn_ref *
hostap_conn_register_ref(struct hostap_conn *conn,
                         const struct hostap_conn_ref_ops *ops,
                         void *priv)
{
    if (conn == NULL) return NULL;
    if (conn->stopping) return NULL;

    struct hostap_conn_ref *ref = CALLOC(1, sizeof(*ref));
    ref->ops = ops;
    ref->priv = priv;
    hostap_conn_ref_attach(ref, conn);
    return ref;
}

struct hostap_conn *
hostap_conn_ref_get_conn(struct hostap_conn_ref *ref)
{
    if (ref == NULL) return NULL;
    return ref->conn;
}

void
hostap_conn_ref_unregister(struct hostap_conn_ref *ref)
{
    if (ref == NULL) return;

    hostap_conn_ref_detach(ref);
    FREE(ref);
}
