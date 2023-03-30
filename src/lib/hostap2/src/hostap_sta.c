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
#include <string.h>

/* opensync */
#include <ds_dlist.h>
#include <ds_tree.h>
#include <memutil.h>
#include <const.h>
#include <log.h>
#include <os.h>

/* unit */
#include <hostap_sock.h>
#include <hostap_sta.h>

struct hostap_sta {
    struct hostap_conn_ref *ref;
    struct hostap_txq *txq;
    struct hostap_txq_req *iter;
    struct ds_dlist refs;
    struct ds_tree stas;
};

struct hostap_sta_info_priv {
    struct hostap_sta_info pub;
    struct ds_tree_node node;
    char *buf;
    bool invalid;
};

struct hostap_sta_ref {
    struct ds_dlist_node node;
    struct hostap_sta *sta;
    const struct hostap_sta_ops *ops;
    void *priv;
};

/* private */
#define LOG_PREFIX_STA(sta, fmt, ...) \
    "hostap: sta: %s: " fmt, \
    hostap_sock_get_path( \
    hostap_conn_get_sock( \
    hostap_txq_get_conn(sta->txq))), \
    ##__VA_ARGS__

#define HOSTAP_STA_REF_NOTIFY(ref, fn, ...) \
    do { \
        if ((ref)->ops->fn != NULL) \
            (ref)->ops->fn((ref), ##__VA_ARGS__, (ref)->priv); \
    } while (0)

#define HOSTAP_STA_NOTIFY(sta, fn, ...) \
    do { \
        struct hostap_sta_ref *ref; \
        ds_dlist_foreach(&(sta)->refs, ref) \
            HOSTAP_STA_REF_NOTIFY(ref, fn, ##__VA_ARGS__); \
    } while (0)

static int
os_macaddr_cmp(const void *a, const void *b)
{
    const os_macaddr_t *x = a;
    const os_macaddr_t *y = b;
    return memcmp(x, y, sizeof(*x));
}

static void
hostap_sta_ref_attach(struct hostap_sta_ref *ref,
                      struct hostap_sta *sta)
{
    if (WARN_ON(ref->sta != NULL)) return;

    struct ds_tree *stas = &sta->stas;
    struct ds_dlist *refs = &sta->refs;

    ref->sta = sta;
    ds_dlist_insert_tail(refs, ref);

    struct hostap_sta_info_priv *info;
    ds_tree_foreach(stas, info) {
        HOSTAP_STA_REF_NOTIFY(ref, connected_fn, &info->pub);
    }
}

static void
hostap_sta_ref_detach(struct hostap_sta_ref *ref)
{
    if (ref->sta == NULL) return;

    struct ds_tree *stas = &ref->sta->stas;
    struct ds_dlist *refs = &ref->sta->refs;

    ref->sta = NULL;
    ds_dlist_remove(refs, ref);

    struct hostap_sta_info_priv *info;
    ds_tree_foreach(stas, info) {
        HOSTAP_STA_REF_NOTIFY(ref, disconnected_fn, &info->pub);
    }
}

static bool
hostap_sta_str_to_mac(const char *str, os_macaddr_t *addr)
{
    int n = sscanf(str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                   &addr->addr[0],
                   &addr->addr[1],
                   &addr->addr[2],
                   &addr->addr[3],
                   &addr->addr[4],
                   &addr->addr[5]);
    return (n == 6);
}

static void
hostap_sta_set(struct hostap_sta *sta,
               const os_macaddr_t *addr,
               const char *buf)
{
    struct hostap_sta_info_priv *info = ds_tree_find(&sta->stas, addr);
    const bool connected = (info == NULL && buf != NULL);
    const bool disconnected = (info != NULL && buf == NULL);
    const bool changed = (info != NULL)
                      && (buf != NULL)
                      && (strcmp(info->buf, buf) != 0);

    if (info != NULL) {
        info->invalid = false;
    }

    if (connected) {
        info = CALLOC(1, sizeof(*info));
        info->buf = STRDUP(buf);
        info->pub.addr = *addr;
        info->pub.buf = info->buf;
        ds_tree_insert(&sta->stas, info, &info->pub.addr);

        HOSTAP_STA_NOTIFY(sta, connected_fn, &info->pub);
    }

    if (changed) {
        char *old_buf = info->buf;
        info->buf = STRDUP(buf);
        info->pub.buf = info->buf;

        HOSTAP_STA_NOTIFY(sta, changed_fn, &info->pub, old_buf);

        FREE(old_buf);
    }

    if (disconnected) {
        HOSTAP_STA_NOTIFY(sta, disconnected_fn, &info->pub);

        ds_tree_remove(&sta->stas, info);
        FREE(info->buf);
        FREE(info);
    }
}

static void
hostap_sta_mark_invalid(struct hostap_sta *sta)
{
    struct hostap_sta_info_priv *info;
    ds_tree_foreach(&sta->stas, info) {
        info->invalid = true;
    }
}

static void
hostap_sta_remove_invalid(struct hostap_sta *sta)
{
    struct hostap_sta_info_priv *info;
    struct hostap_sta_info_priv *tmp;

    ds_tree_foreach_safe(&sta->stas, info, tmp) {
        if (info->invalid) {
            hostap_sta_set(sta, &info->pub.addr, NULL);
        }
    }
}

static void
hostap_sta_detach_refs(struct hostap_sta *sta)
{
    struct hostap_sta_ref *ref;
    while ((ref = ds_dlist_remove_head(&sta->refs)) != NULL) {
        hostap_sta_ref_detach(ref);
    }
}

static void
hostap_sta_detach(struct hostap_sta *sta)
{
    struct hostap_conn_ref *ref = sta->ref;

    sta->ref = NULL;
    sta->txq = NULL;

    if (ref != NULL) hostap_conn_ref_unregister(ref);

    hostap_sta_mark_invalid(sta);
    hostap_sta_remove_invalid(sta);
    hostap_sta_detach_refs(sta);
}

static void
hostap_sta_iter_done_cb(struct hostap_txq_req *req,
                        void *priv)
{
    hostap_txq_req_completed_fn_t *cb = hostap_sta_iter_done_cb;
    struct hostap_sta *sta = priv;
    const char *reply = NULL;
    size_t reply_len = 0;
    const bool first = (sta->iter == NULL);
    const bool reply_ok = hostap_txq_req_get_reply(req, &reply, &reply_len);
    os_macaddr_t addr;
    const bool valid_addr = (reply_ok)
                        && (strlen(reply) > 0)
                        && (strstr(reply, "FAIL") != reply)
                        && (hostap_sta_str_to_mac(reply, &addr));
    char buf[64];

    MEMZERO(buf);

    if (first) {
        snprintf(buf, sizeof(buf), "STA-FIRST");
    }
    else if (valid_addr) {
        os_macaddr_t addr;
        hostap_sta_str_to_mac(reply, &addr);
        hostap_sta_set(sta, &addr, reply);
        snprintf(buf, sizeof(buf), "STA-NEXT "PRI(os_macaddr_t), FMT(os_macaddr_t, addr));
    }
    else {
        hostap_sta_remove_invalid(sta);
    }

    const bool more = (strlen(buf) > 0);
    hostap_txq_req_free(sta->iter);
    sta->iter = NULL;

    if (more && sta->txq != NULL) {
        sta->iter = hostap_txq_request(sta->txq, buf, cb, sta);
    }
}

static void
hostap_sta_conn_msg_cb(struct hostap_conn_ref *ref,
                       const void *msg_,
                       size_t msg_len,
                       bool is_event,
                       void *priv)
{
    struct hostap_sta *sta = priv;
    const char *msg = msg_; /* FIXME */

    if (is_event == false) return;

    /* <3>AP-STA-CONNECTED aa:bb:cc:dd:ee:ff ... */
    if (msg[0] != '<') return;
    msg = strchr(msg, '>');
    if (msg == NULL) return;
    msg++;

    const bool need_rebuild = (strstr(msg, "AP-STA-CONNECTED") == msg)
                           || (strstr(msg, "AP-STA-DISCONNECTED") == msg);

    if (need_rebuild) {
        os_macaddr_t addr;

        msg = strchr(msg, ' ');
        if (msg == NULL) return;
        msg++;

        hostap_sta_str_to_mac(msg, &addr);

        /* FIXME: This could be optimized to only fetch one */
        hostap_sta_rebuild(sta);
    }
}

static void
hostap_sta_conn_opened_cb(struct hostap_conn_ref *ref,
                          void *priv)
{
    struct hostap_sta *sta = priv;
    hostap_sta_rebuild(sta);
}

static void
hostap_sta_conn_closed_cb(struct hostap_conn_ref *ref,
                          void *priv)
{
    struct hostap_sta *sta = priv;
    hostap_sta_mark_invalid(sta);
    hostap_sta_remove_invalid(sta);
}

static void
hostap_sta_conn_stopping_cb(struct hostap_conn_ref *ref,
                            void *priv)
{
    struct hostap_sta *sta = priv;
    hostap_sta_detach(sta);
}

/* public */
struct hostap_sta *
hostap_sta_alloc(struct hostap_txq *txq)
{
    static const struct hostap_conn_ref_ops conn_ops = {
        .msg_fn = hostap_sta_conn_msg_cb,
        .opened_fn = hostap_sta_conn_opened_cb,
        .closed_fn = hostap_sta_conn_closed_cb,
        .stopping_fn = hostap_sta_conn_stopping_cb,
    };

    struct hostap_conn *conn = hostap_txq_get_conn(txq);
    struct hostap_sta *sta = CALLOC(1, sizeof(*sta));
    ds_dlist_init(&sta->refs, struct hostap_sta_ref, node);
    ds_tree_init(&sta->stas, os_macaddr_cmp, struct hostap_sta_info_priv, node);
    sta->txq = txq;
    sta->ref = hostap_conn_register_ref(conn, &conn_ops, sta);

    return sta;
}

void
hostap_sta_free(struct hostap_sta *sta)
{
    if (sta == NULL) return;
    hostap_sta_detach(sta);
    FREE(sta);
}

void
hostap_sta_rebuild(struct hostap_sta *sta)
{
    if (sta->iter != NULL) {
        hostap_txq_req_free(sta->iter);
        sta->iter = NULL;
    }

    hostap_sta_mark_invalid(sta);
    hostap_sta_iter_done_cb(NULL, sta);
}

const struct hostap_sta_info *
hostap_sta_get_info(struct hostap_sta *sta,
                    const os_macaddr_t *addr)
{
    struct hostap_sta_info_priv *info = ds_tree_find(&sta->stas, addr);
    if (info == NULL) return NULL;
    return &info->pub;
}

struct hostap_sta_ref *
hostap_sta_register(struct hostap_sta *sta,
                    const struct hostap_sta_ops *ops,
                    void *priv)
{
    struct hostap_sta_ref *ref = CALLOC(1, sizeof(*ref));
    ref->ops = ops;
    ref->priv = priv;
    hostap_sta_ref_attach(ref, sta);
    return ref;
}

void
hostap_sta_unregister(struct hostap_sta_ref *ref)
{
    if (ref == NULL) return;
    hostap_sta_ref_detach(ref);
    FREE(ref);
}
