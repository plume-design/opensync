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

/* opensync */
#include <log.h>
#include <const.h>
#include <memutil.h>
#include <ds_tree.h>
#include <os.h>

/* osw */
#include <osw_module.h>
#include <osw_state.h>
#include <osw_mux.h>
#include <osw_types.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_l2uf.h>
#include <osw_etc.h>
#include <osw_sta_assoc.h>
#include <osw_ut.h>

/* unit */

/**
 * Purpose:
 *
 * When clients roam between APs within the same broadcast
 * domain (or ESS in 802.11-speak) APs generate special
 * broadcast frames to implicitly update forwarding tables
 * on other switching devices on the network (including
 * other APs, switches, etc).
 *
 * Some clients, when roaming, fail to fully deauthenticate
 * from the (old) AP leading to stale entries being reported
 * by the driver. This has 2 problems:
 *
 *  - duplicate client reports network wide
 *
 *  - re-association to the (old) AP may transiently fail
 *    and eventually take longer than necessary, eg. when
 *    PMF and SA queries are involved.
 *
 * To mitigate this the L2UF frames are used as a signal to
 * make sure a client with matching SA does not exist
 * locally on the AP.
 *
 * The logic to evict stations is 2-staged:
 *
 *  - delete, ie, state machine reset
 *
 *  - deauth, as a fallback
 *
 * Some drivers may not properly handle deletion, even if
 * they think they do. To make life easier the code attempts
 * to do both methods.
 *
 * The deauth is not preferred. Depending on the underlying
 * WLAN driver it can generate on the air frames. This
 * wastes airtime as well as introduces interoperability
 * issues with some clients.
 *
 * The module also performs stale link clean ups. This works
 * even (and especially) when L2UFs do not appear.
 *
 * A legit case where L2UFs may not appear is MLD combined
 * with hairpinning. For multiple APs there is a single
 * net_device and for which AP1 -> AP2 (both same-MLD
 * Affiliatd APs) roaming will result in no L2UF. Even if
 * the wireless stack on the system happens to handle this
 * gracefully already, doing this here again is harmless as
 * it'll disarm itself.
 *
 * Gotchas:
 *
 * Some platforms with their datapath acceleration
 * implementation may suffer performance degradation when
 * certain pcap filters are installed. If performance seems
 * to be hit when L2UF outbound capture is enabled and the
 * WLAN driver does not automatically perform the
 * L2UF-driven kickouts, then either the acceleration
 * implementation needs to be fixed, or WLAN driver needs to
 * be patched to perform this internally.
 */

#define OW_L2UF_KICK_DEAUTH_EXPIRE_SEC 5
#define OW_L2UF_KICK_DELETE_EXPIRE_SEC 5
#define OW_L2UF_KICK_WORK_SEC 3

#define LOG_PREFIX(fmt, ...) \
    "ow: l2uf: " fmt, \
        ##__VA_ARGS__

#define LOG_PREFIX_EVICT(e, fmt, ...) \
    LOG_PREFIX( \
        "evict: %s/%s/"OSW_HWADDR_FMT": " fmt, \
        (e)->phy_name, \
        (e)->vif_name, \
        OSW_HWADDR_ARG(&(e)->sta_addr), \
        ##__VA_ARGS__)

#define LOG_PREFIX_MLD(mld, fmt, ...) \
    LOG_PREFIX( \
        "mld: %s: " fmt, \
        (mld)->mld_name, \
        ##__VA_ARGS__)

#define LOG_PREFIX_NETDEV(netdev, fmt, ...) \
    LOG_PREFIX( \
        "netdev: %s: " fmt, \
        (netdev)->if_name, \
        ##__VA_ARGS__)

#define LOG_PREFIX_VIF(vif, fmt, ...) \
    LOG_PREFIX( \
        "vif: %s/%s: " fmt, \
        (vif)->phy_name, \
        (vif)->vif_name, \
        ##__VA_ARGS__)

#define LOG_PREFIX_STA(sta, fmt, ...) \
    LOG_PREFIX( \
        "sta: "OSW_HWADDR_FMT": " fmt, \
        OSW_HWADDR_ARG(&(sta)->sta_addr), \
        ##__VA_ARGS__)

typedef bool
ow_l2uf_kick_mux_deauth_fn_t(const char *phy_name,
                             const char *vif_name,
                             const struct osw_hwaddr *mac_addr,
                             int dot11_reason_code);

typedef bool
ow_l2uf_kick_mux_delete_fn_t(const char *phy_name,
                             const char *vif_name,
                             const struct osw_hwaddr *mac_addr);

struct ow_l2uf_kick_stats {
    uint32_t delete;
    uint32_t deauth;
    uint32_t fail;
};

struct ow_l2uf_kick {
    struct osw_state_observer obs;
    struct ds_tree vifs_by_name;
    struct ds_tree vifs_by_addr;
    struct ds_tree stas;
    struct ds_tree mlds;
    struct ds_tree netdevs;
    struct osw_l2uf *l2uf;
    osw_sta_assoc_t *sta_assoc;
    osw_sta_assoc_observer_t *sta_assoc_obs;
    ow_l2uf_kick_mux_deauth_fn_t *mux_deauth_fn;
    ow_l2uf_kick_mux_delete_fn_t *mux_delete_fn;
    struct ow_l2uf_kick_stats stats;
};

struct ow_l2uf_kick_netdev {
    struct ds_tree_node node;
    struct ow_l2uf_kick *m;
    struct osw_l2uf_if *i;
    char *if_name;
    struct ow_l2uf_kick_vif *vif;
    struct ow_l2uf_kick_mld *mld;
};

struct ow_l2uf_kick_mld {
    struct ds_tree_node node;
    struct ow_l2uf_kick *m;
    struct ow_l2uf_kick_netdev *netdev;
    char *mld_name;
    struct ds_tree vifs_by_name;
};

struct ow_l2uf_kick_vif {
    struct ds_tree_node node_by_name;
    struct ds_tree_node node_by_addr;
    struct ds_tree_node node_mld_by_name;
    struct ow_l2uf_kick *m;
    struct ow_l2uf_kick_netdev *netdev;
    struct ow_l2uf_kick_mld *mld;
    struct osw_hwaddr addr;
    enum osw_vif_type vif_type;
    char *phy_name;
    char *vif_name;
};

struct ow_l2uf_kick_evict {
    struct ds_tree_node node;
    struct ow_l2uf_kick_sta *sta;
    struct osw_timer deauth_expire;
    struct osw_timer delete_expire;
    struct osw_hwaddr ap_addr;
    struct osw_hwaddr sta_addr;
    char *phy_name;
    char *vif_name;
};

struct ow_l2uf_kick_sta {
    struct ds_tree_node node;
    struct ow_l2uf_kick *m;
    struct osw_hwaddr sta_addr;
    osw_sta_assoc_links_t active;
    osw_sta_assoc_links_t stale;
    struct ds_tree evicts;
    struct osw_hwaddr_list l2uf_seen_on;
    bool recently_connected;
    struct osw_timer work;
};

static void
ow_l2uf_kick_evict_deauth(struct ow_l2uf_kick_evict *e)
{
    struct ow_l2uf_kick_sta *sta = e->sta;
    struct ow_l2uf_kick *m = sta->m;
    const char *phy_name = e->phy_name;
    const char *vif_name = e->vif_name;
    const struct osw_hwaddr *sta_addr = &e->sta_addr;
    const int reason_unspec = 1;

    LOGI(LOG_PREFIX_STA(sta, "attempting to deauth"));
    const bool ok = m->mux_deauth_fn(phy_name, vif_name, sta_addr, reason_unspec);
    const bool probably_not_implemented = !ok;
    WARN_ON(probably_not_implemented);

    struct osw_timer *deauth = &e->deauth_expire;
    const uint64_t now = osw_time_mono_clk();
    const uint64_t at = now + OSW_TIME_SEC(OW_L2UF_KICK_DEAUTH_EXPIRE_SEC);
    osw_timer_arm_at_nsec(deauth, at);
}

static void
ow_l2uf_kick_evict_delete(struct ow_l2uf_kick_evict *e)
{
    struct ow_l2uf_kick_sta *sta = e->sta;
    struct ow_l2uf_kick *m = sta->m;
    const char *phy_name = e->phy_name;
    const char *vif_name = e->vif_name;
    const struct osw_hwaddr *sta_addr = &e->sta_addr;

    osw_timer_disarm(&e->deauth_expire);

    LOGI(LOG_PREFIX_EVICT(e, "attempting to delete"));
    const bool ok = m->mux_delete_fn(phy_name, vif_name, sta_addr);
    const bool probably_not_implemented = !ok;
    WARN_ON(probably_not_implemented);

    struct osw_timer *t = &e->delete_expire;
    const uint64_t now = osw_time_mono_clk();
    const uint64_t at = now + OSW_TIME_SEC(OW_L2UF_KICK_DELETE_EXPIRE_SEC);
    osw_timer_arm_at_nsec(t, at);
}

static void
ow_l2uf_kick_evict_drop(struct ow_l2uf_kick_evict *e)
{
    if (e == NULL) return;
    LOGD(LOG_PREFIX_EVICT(e, "dropping"));
    osw_timer_disarm(&e->deauth_expire);
    osw_timer_disarm(&e->delete_expire);
    ds_tree_remove(&e->sta->evicts, e);
    FREE(e->phy_name);
    FREE(e->vif_name);
    FREE(e);
}

static void
ow_l2uf_kick_evict_deauth_expire_cb(struct osw_timer *t)
{
    struct ow_l2uf_kick_evict *e = container_of(t, typeof(*e), deauth_expire);
    LOGW(LOG_PREFIX_EVICT(e, "failed to delete sta: timed out"));
    e->sta->m->stats.fail++;
    ow_l2uf_kick_evict_drop(e);
}

static void
ow_l2uf_kick_evict_delete_expire_cb(struct osw_timer *t)
{
    struct ow_l2uf_kick_evict *e = container_of(t, typeof(*e), delete_expire);
    LOGD(LOG_PREFIX_EVICT(e, "failed to delete sta: timed out; possible osw or wlan driver bug, falling back to deauth"));
    ow_l2uf_kick_evict_deauth(e);
}

static int
ow_l2uf_kick_evict_cmp(const void *a, const void *b)
{
    const struct ow_l2uf_kick_evict *e1 = a;
    const struct ow_l2uf_kick_evict *e2 = b;
    const int r1 = osw_hwaddr_cmp(&e1->ap_addr, &e2->ap_addr);
    const int r2 = osw_hwaddr_cmp(&e1->sta_addr, &e2->sta_addr);
    if (r1) return r1;
    if (r2) return r2;
    return 0;
}

static struct ow_l2uf_kick_evict *
ow_l2uf_kick_evict_alloc(struct ow_l2uf_kick_sta *sta,
                         const struct osw_hwaddr *ap_addr,
                         const struct osw_hwaddr *sta_addr)
{
    struct ow_l2uf_kick_vif *vif = ds_tree_find(&sta->m->vifs_by_addr, ap_addr);
    if (WARN_ON(vif == NULL)) return NULL;

    struct ow_l2uf_kick_evict *e = CALLOC(1, sizeof(*e));
    e->sta = sta;
    e->ap_addr = *ap_addr;
    e->sta_addr = *sta_addr;
    e->phy_name = STRDUP(vif->phy_name);
    e->vif_name = STRDUP(vif->vif_name);
    osw_timer_init(&e->deauth_expire, ow_l2uf_kick_evict_deauth_expire_cb);
    osw_timer_init(&e->delete_expire, ow_l2uf_kick_evict_delete_expire_cb);
    ds_tree_insert(&sta->evicts, e, e);
    ow_l2uf_kick_evict_delete(e);
    LOGD(LOG_PREFIX_EVICT(e, "allocated"));
    return e;
}

static struct ow_l2uf_kick_evict *
ow_l2uf_kick_sta_spawn_evict(struct ow_l2uf_kick_sta *sta,
                             const struct osw_hwaddr *ap_addr,
                             const struct osw_hwaddr *sta_addr)
{
    const struct ow_l2uf_kick_evict template = {
        .ap_addr = *ap_addr,
        .sta_addr = *sta_addr,
    };
    return ds_tree_find(&sta->evicts, &template) ?: ow_l2uf_kick_evict_alloc(sta, ap_addr, sta_addr);
}

static void
ow_l2uf_kick_sta_evict_links(struct ow_l2uf_kick_sta *sta,
                             const osw_sta_assoc_links_t *l)
{
    size_t n = l->count;
    size_t i;
    for (i = 0; i < n; i++) {
        const struct osw_hwaddr *ap_addr = &l->links[i].local_sta_addr;
        const struct osw_hwaddr *sta_addr =  &l->links[i].remote_sta_addr;
        LOGD(LOG_PREFIX_STA(sta, "evicting "OSW_HWADDR_FMT" on "OSW_HWADDR_FMT,
                    OSW_HWADDR_ARG(sta_addr),
                    OSW_HWADDR_ARG(ap_addr)));
        ow_l2uf_kick_sta_spawn_evict(sta, ap_addr, sta_addr);
    }
}

static void
ow_l2uf_kick_sta_drop_evicts(struct ow_l2uf_kick_sta *sta)
{
    struct ow_l2uf_kick_evict *e;
    while ((e = ds_tree_head(&sta->evicts)) != NULL) {
        ow_l2uf_kick_evict_drop(e);
    }
}

static void
ow_l2uf_kick_sta_drop(struct ow_l2uf_kick_sta *sta)
{
    if (sta == NULL) return;
    LOGT(LOG_PREFIX_STA(sta, "dropping"));
    ow_l2uf_kick_sta_drop_evicts(sta);
    osw_timer_disarm(&sta->work);
    osw_hwaddr_list_flush(&sta->l2uf_seen_on);
    ds_tree_remove(&sta->m->stas, sta);
    FREE(sta);
}

static void
ow_l2uf_kick_sta_gc(struct ow_l2uf_kick_sta *sta)
{
    if (sta->active.count > 0) return;
    if (osw_timer_is_armed(&sta->work)) return;
    if (ds_tree_is_empty(&sta->evicts) == false) return;
    ow_l2uf_kick_sta_drop(sta);
}

static void
ow_l2uf_kick_sta_work(struct ow_l2uf_kick_sta *sta)
{
    struct osw_hwaddr_list l2uf_seen_on = sta->l2uf_seen_on;
    const bool recently_connected = sta->recently_connected;
    const bool l2uf_seen = (l2uf_seen_on.count > 0);
    const bool connected_or_stale = (sta->active.count > 0) || (sta->stale.count > 0);

    MEMZERO(sta->l2uf_seen_on);
    sta->recently_connected = false;

    if (connected_or_stale) {
        if (l2uf_seen == true && recently_connected == false) {
            bool seen_on_active = false;
            size_t i;
            for (i = 0; i < l2uf_seen_on.count; i++)
            {
                const struct osw_hwaddr *ap_addr = &l2uf_seen_on.list[i];
                const osw_sta_assoc_link_t *active = osw_sta_assoc_links_lookup(&sta->active, ap_addr, NULL);
                if (active)
                {
                    seen_on_active = true;
                }
            }

            if (seen_on_active)
            {
                LOGD(LOG_PREFIX_STA(sta, "evicting all links"));
                ow_l2uf_kick_sta_evict_links(sta, &sta->active);
                ow_l2uf_kick_sta_evict_links(sta, &sta->stale);
            }
            else
            {
                LOGD(LOG_PREFIX_STA(sta, "evicting stale links only (l2uf not seen for active links)"));
                ow_l2uf_kick_sta_evict_links(sta, &sta->stale);
            }
        }
        else if (sta->stale.count > 0) {
            LOGD(LOG_PREFIX_STA(sta, "evicting stale links only"));
            ow_l2uf_kick_sta_evict_links(sta, &sta->stale);
        }
        else if (l2uf_seen == true && recently_connected == true) {
            LOGD(LOG_PREFIX_STA(sta, "l2uf coincided with a (re-)association with no stale links, ignoring"));
        }
    }

    ow_l2uf_kick_sta_gc(sta);
}

static void
ow_l2uf_kick_sta_work_cb(struct osw_timer *t)
{
    struct ow_l2uf_kick_sta *sta = container_of(t, typeof(*sta), work);
    ow_l2uf_kick_sta_work(sta);
}

static struct ow_l2uf_kick_sta *
ow_l2uf_kick_sta_alloc(struct ow_l2uf_kick *m,
                       const struct osw_hwaddr *sta_addr)
{
    struct ow_l2uf_kick_sta *sta = CALLOC(1, sizeof(*sta));
    sta->m = m;
    sta->sta_addr = *sta_addr;
    osw_timer_init(&sta->work, ow_l2uf_kick_sta_work_cb);
    ds_tree_init(&sta->evicts, ow_l2uf_kick_evict_cmp, struct ow_l2uf_kick_evict, node);
    ds_tree_insert(&m->stas, sta, &sta->sta_addr);
    LOGT(LOG_PREFIX_STA(sta, "allocated"));
    return sta;
}

static struct ow_l2uf_kick_sta *
ow_l2uf_kick_sta_get(struct ow_l2uf_kick *m,
                     const struct osw_hwaddr *sta_addr)
{
    return ds_tree_find(&m->stas, sta_addr) ?: ow_l2uf_kick_sta_alloc(m, sta_addr);
}

static void
ow_l2uf_kick_sta_work_sched(struct ow_l2uf_kick_sta *sta)
{
    const uint64_t now = osw_time_mono_clk();
    const uint64_t at = now + OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC);
    osw_timer_arm_at_nsec(&sta->work, at);
}

static void
ow_l2uf_kick_sta_set_recently_connected(struct ow_l2uf_kick_sta *sta)
{
    if (sta->recently_connected) return;
    LOGD(LOG_PREFIX_STA(sta, "recently (re-)associated"));
    sta->recently_connected = true;
    ow_l2uf_kick_sta_work_sched(sta);
}

static const char *
ow_l2uf_kick_evict_get_type(const struct ow_l2uf_kick_evict *e)
{
    if (osw_timer_is_armed(&e->delete_expire)) return "delete";
    if (osw_timer_is_armed(&e->deauth_expire)) return "deauth";
    return "unknown";
}

static void
ow_l2uf_kick_sta_check_eviction_status(struct ow_l2uf_kick_sta *sta)
{
    struct ow_l2uf_kick_evict *e;
    struct ow_l2uf_kick_evict *tmp;
    ds_tree_foreach_safe(&sta->evicts, e, tmp) {
        const osw_sta_assoc_link_t *active = osw_sta_assoc_links_lookup(&sta->active, &e->ap_addr, &e->sta_addr);
        const osw_sta_assoc_link_t *stale = osw_sta_assoc_links_lookup(&sta->stale, &e->ap_addr, &e->sta_addr);
        const bool evicted = (active == NULL) && (stale == NULL);
        if (evicted) {
            const char *type = ow_l2uf_kick_evict_get_type(e);
            LOGN(LOG_PREFIX_STA(sta, "evicted ghost station "OSW_HWADDR_FMT" from %s through %s", OSW_HWADDR_ARG(&e->sta_addr), e->vif_name, type));
            if (osw_timer_is_armed(&e->delete_expire)) sta->m->stats.delete++;
            if (osw_timer_is_armed(&e->deauth_expire)) sta->m->stats.deauth++;
            ow_l2uf_kick_evict_drop(e);
        }
    }
}

static void
ow_l2uf_kick_sta_notify_cb(void *priv, const osw_sta_assoc_entry_t *e, osw_sta_assoc_event_e ev)
{
    struct ow_l2uf_kick *m = priv;
    const struct osw_hwaddr *addr = osw_sta_assoc_entry_get_addr(e);
    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(m, addr);

    sta->active = *osw_sta_assoc_entry_get_active_links(e);
    sta->stale = *osw_sta_assoc_entry_get_stale_links(e);

    switch (ev) {
        case OSW_STA_ASSOC_CONNECTED:
        case OSW_STA_ASSOC_RECONNECTED:
            ow_l2uf_kick_sta_set_recently_connected(sta);
            break;
        case OSW_STA_ASSOC_UNDEFINED:
        case OSW_STA_ASSOC_DISCONNECTED:
            break;
    }

    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
}

static void
ow_l2uf_kick_sta_set_l2uf_seen_on(struct ow_l2uf_kick_sta *sta, const struct osw_hwaddr *ap_addr)
{
    if (osw_hwaddr_list_contains(sta->l2uf_seen_on.list, sta->l2uf_seen_on.count, ap_addr)) return;
    LOGD(LOG_PREFIX_STA(sta, "l2uf: appending "OSW_HWADDR_FMT, OSW_HWADDR_ARG(ap_addr)));
    osw_hwaddr_list_append(&sta->l2uf_seen_on, ap_addr);
    if (sta->l2uf_seen_on.count == 1) ow_l2uf_kick_sta_work_sched(sta);
}

static void
ow_l2uf_kick_netdev_report_sa(struct ow_l2uf_kick_netdev *netdev,
                              const struct osw_hwaddr *sa_addr)
{
    const struct osw_hwaddr *sta_addr = sa_addr;
    struct ow_l2uf_kick_sta *sta = ds_tree_find(&netdev->m->stas, sta_addr);

    if (sta == NULL) {
        LOGD(LOG_PREFIX_NETDEV(netdev, "sa_addr "OSW_HWADDR_FMT" not associated, ignoring",
                               OSW_HWADDR_ARG(sa_addr)));
        return;
    }

    if (netdev->vif != NULL)
    {
        ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &netdev->vif->addr);
    }

    if (netdev->mld != NULL)
    {
        struct ow_l2uf_kick_vif *vif;
        ds_tree_foreach(&netdev->mld->vifs_by_name, vif)
        {
            ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &vif->addr);
        }
    }
}

static enum osw_vif_type
ow_l2uf_kick_netdev_get_vif_type(struct ow_l2uf_kick_netdev *n)
{
    if (n->vif != NULL) {
        return n->vif->vif_type;
    }

    if (n->mld != NULL) {
        struct ow_l2uf_kick_vif *vif;
        ds_tree_foreach(&n->mld->vifs_by_name, vif) {
            return vif->vif_type;
        }
    }

    return OSW_VIF_UNDEFINED;
}

static void
ow_l2uf_kick_netdev_seen_cb(struct osw_l2uf_if *i,
                            const struct osw_hwaddr *sa_addr)
{
    struct ow_l2uf_kick_netdev *netdev = osw_l2uf_if_get_data(i);
    const enum osw_vif_type vif_type = ow_l2uf_kick_netdev_get_vif_type(netdev);

    switch (vif_type) {
        case OSW_VIF_AP:
            ow_l2uf_kick_netdev_report_sa(netdev, sa_addr);
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            break;
        case OSW_VIF_UNDEFINED:
            break;
    }
}

static struct ow_l2uf_kick_netdev *
ow_l2uf_kick_netdev_alloc(struct ow_l2uf_kick *m,
                          const char *if_name)
{
    struct ow_l2uf_kick_netdev *n = CALLOC(1, sizeof(*n));
    n->m = m;
    n->if_name = STRDUP(if_name);
    n->i = osw_l2uf_if_alloc(m->l2uf, if_name);
    osw_l2uf_if_set_data(n->i, n);
    osw_l2uf_if_set_seen_fn(n->i, ow_l2uf_kick_netdev_seen_cb);
    LOGD(LOG_PREFIX_NETDEV(n, "allocated"));
    return n;
}

static struct ow_l2uf_kick_netdev *
ow_l2uf_kick_netdev_get(struct ow_l2uf_kick *m,
                        const char *if_name)
{
    return ds_tree_find(&m->netdevs, if_name) ?: ow_l2uf_kick_netdev_alloc(m, if_name);
}

static void
ow_l2uf_kick_netdev_drop(struct ow_l2uf_kick_netdev *n)
{
    if (n == NULL) return;
    LOGD(LOG_PREFIX_NETDEV(n, "dropping"));
    WARN_ON(n->vif != NULL);
    WARN_ON(n->mld != NULL);
    osw_l2uf_if_free(n->i);
    ds_tree_remove(&n->m->netdevs, n);
    FREE(n->if_name);
    FREE(n);
}

static void
ow_l2uf_kick_netdev_gc(struct ow_l2uf_kick_netdev *n)
{
    if (n->vif != NULL) return;
    if (n->mld != NULL) return;
    ow_l2uf_kick_netdev_drop(n);
}

static void
ow_l2uf_kick_netdev_set_vif(struct ow_l2uf_kick_netdev *n, struct ow_l2uf_kick_vif *vif)
{
    n->vif = vif;
    ow_l2uf_kick_netdev_gc(n);
}

static void
ow_l2uf_kick_netdev_set_mld(struct ow_l2uf_kick_netdev *n, struct ow_l2uf_kick_mld *mld)
{
    n->mld = mld;
    ow_l2uf_kick_netdev_gc(n);
}

static struct ow_l2uf_kick_mld *
ow_l2uf_kick_mld_alloc(struct ow_l2uf_kick *m,
                       const char *mld_name)
{
    struct ow_l2uf_kick_mld *mld = CALLOC(1, sizeof(*mld));
    mld->m = m;
    mld->mld_name = STRDUP(mld_name);
    mld->netdev = ow_l2uf_kick_netdev_get(m, mld_name);
    ow_l2uf_kick_netdev_set_mld(mld->netdev, mld);
    ds_tree_init(&mld->vifs_by_name, ds_str_cmp, struct ow_l2uf_kick_vif, node_mld_by_name);
    ds_tree_insert(&m->mlds, mld, mld->mld_name);
    LOGD(LOG_PREFIX_MLD(mld, "allocated"));
    return mld;
}

static struct ow_l2uf_kick_mld *
ow_l2uf_kick_mld_get(struct ow_l2uf_kick *m, const char *mld_name)
{
    if (mld_name == NULL) return NULL;
    return ds_tree_find(&m->mlds, mld_name) ?: ow_l2uf_kick_mld_alloc(m, mld_name);
}

static void
ow_l2uf_kick_mld_drop(struct ow_l2uf_kick_mld *mld)
{
    if (mld == NULL) return;
    if (WARN_ON(mld->m == NULL)) return;
    if (WARN_ON(ds_tree_is_empty(&mld->vifs_by_name) == false)) return;
    LOGD(LOG_PREFIX_MLD(mld, "dropping"));
    ow_l2uf_kick_netdev_set_mld(mld->netdev, NULL);
    ds_tree_remove(&mld->m->mlds, mld);
    FREE(mld->mld_name);
    FREE(mld);
}

static void
ow_l2uf_kick_mld_gc(struct ow_l2uf_kick_mld *mld)
{
    if (mld == NULL) return;
    if (ds_tree_is_empty(&mld->vifs_by_name) == false) return;
    ow_l2uf_kick_mld_drop(mld);
}

static void
ow_l2uf_kick_mld_detach_vif(struct ow_l2uf_kick_mld *mld,
                            struct ow_l2uf_kick_vif *vif)
{
    if (mld == NULL) return;
    if (vif == NULL) return;
    LOGD(LOG_PREFIX_MLD(mld, "detaching %s", vif->vif_name));
    ds_tree_remove(&mld->vifs_by_name, vif);
    ow_l2uf_kick_mld_gc(mld);
}

static void
ow_l2uf_kick_mld_attach_vif(struct ow_l2uf_kick_mld *mld,
                            struct ow_l2uf_kick_vif *vif)
{
    if (mld == NULL) return;
    if (vif == NULL) return;
    LOGD(LOG_PREFIX_MLD(mld, "attaching %s", vif->vif_name));
    ds_tree_insert(&mld->vifs_by_name, vif, vif->vif_name);
}

static struct ow_l2uf_kick_vif *
ow_l2uf_kick_vif_alloc(struct ow_l2uf_kick *m,
                       const char *phy_name,
                       const char *vif_name)
{
    struct ow_l2uf_kick_vif *vif = CALLOC(1, sizeof(*vif));
    vif->m = m;
    vif->phy_name = STRDUP(phy_name);
    vif->vif_name = STRDUP(vif_name);
    vif->netdev = ow_l2uf_kick_netdev_get(m, vif_name);
    ow_l2uf_kick_netdev_set_vif(vif->netdev, vif);
    ds_tree_insert(&m->vifs_by_name, vif, vif->vif_name);
    LOGD(LOG_PREFIX_VIF(vif, "allocated"));
    return vif;
}

static void
ow_l2uf_kick_vif_set_mld(struct ow_l2uf_kick_vif *vif,
                         struct ow_l2uf_kick_mld *mld_new)
{
    struct ow_l2uf_kick_mld *mld_old = vif->mld;
    if (mld_old == mld_new) return;
    ow_l2uf_kick_mld_detach_vif(mld_old, vif);
    ow_l2uf_kick_mld_attach_vif(mld_new, vif);
    vif->mld = mld_new;
}

static void
ow_l2uf_kick_vif_set_addr(struct ow_l2uf_kick_vif *vif,
                          const struct osw_hwaddr *addr)
{
    addr = addr ?: osw_hwaddr_zero();
    if (osw_hwaddr_is_equal(&vif->addr, addr))
    {
        return;
    }

    if (osw_hwaddr_is_zero(&vif->addr) == false)
    {
        ds_tree_remove(&vif->m->vifs_by_addr, vif);
    }

    LOGD(LOG_PREFIX_VIF(vif, "addr: "OSW_HWADDR_FMT" -> "OSW_HWADDR_FMT,
                OSW_HWADDR_ARG(&vif->addr),
                OSW_HWADDR_ARG(addr)));
    vif->addr = *addr;

    if (osw_hwaddr_is_zero(&vif->addr) == false)
    {
        ds_tree_insert(&vif->m->vifs_by_addr, vif, &vif->addr);
    }
}

static void
ow_l2uf_kick_vif_drop(struct ow_l2uf_kick_vif *vif)
{
    if (vif == NULL) return;
    LOGD(LOG_PREFIX_VIF(vif, "dropping"));
    ow_l2uf_kick_vif_set_mld(vif, NULL);
    ow_l2uf_kick_vif_set_addr(vif, NULL);
    ow_l2uf_kick_netdev_set_vif(vif->netdev, NULL);
    ds_tree_remove(&vif->m->vifs_by_name, vif);
    FREE(vif->phy_name);
    FREE(vif->vif_name);
    FREE(vif);
}

static void
ow_l2uf_kick_vif_update(struct ow_l2uf_kick *m,
                        const struct osw_state_vif_info *info,
                        const bool exists)
{
    const char *phy_name = info->phy->phy_name;
    const char *vif_name = info->vif_name;
    struct ow_l2uf_kick_vif *vif = ds_tree_find(&m->vifs_by_name, vif_name);
    const bool allocated = (vif != NULL);
    const bool want = (exists);
    const bool need_alloc = (want && !allocated);
    const bool need_free = (allocated && !want);

    WARN_ON(need_alloc && need_free);

    if (need_alloc) {
        vif = ow_l2uf_kick_vif_alloc(m, phy_name, vif_name);
    }

    if (vif != NULL) {
        const struct osw_drv_mld_state *mld_state = osw_drv_vif_state_get_mld_state(info->drv_state);
        const char *mld_name = osw_drv_mld_state_get_name(mld_state);
        struct ow_l2uf_kick_mld *mld = ow_l2uf_kick_mld_get(m, mld_name);

        vif->vif_type = info->drv_state->vif_type;
        ow_l2uf_kick_vif_set_addr(vif, &info->drv_state->mac_addr);
        ow_l2uf_kick_vif_set_mld(vif, mld);
    }

    if (need_free) {
        ow_l2uf_kick_vif_drop(vif);
    }
}

static void
ow_l2uf_kick_vif_added_cb(struct osw_state_observer *obs,
                          const struct osw_state_vif_info *info)
{
    struct ow_l2uf_kick *m = container_of(obs, struct ow_l2uf_kick, obs);
    ow_l2uf_kick_vif_update(m, info, true);
}

static void
ow_l2uf_kick_vif_changed_cb(struct osw_state_observer *obs,
                            const struct osw_state_vif_info *info)
{
    struct ow_l2uf_kick *m = container_of(obs, struct ow_l2uf_kick, obs);
    ow_l2uf_kick_vif_update(m, info, true);
}

static void
ow_l2uf_kick_vif_removed_cb(struct osw_state_observer *obs,
                            const struct osw_state_vif_info *info)
{
    struct ow_l2uf_kick *m = container_of(obs, struct ow_l2uf_kick, obs);
    ow_l2uf_kick_vif_update(m, info, false);
}

static bool
ow_l2uf_kick_mux_deauth_no_op(const char *phy_name,
                                const char *vif_name,
                                const struct osw_hwaddr *sta_addr,
                                int dot11_reason_code)
{
    return true;
}

static bool
ow_l2uf_kick_mux_delete_no_op(const char *phy_name,
                                const char *vif_name,
                                const struct osw_hwaddr *sta_addr)
{
    return true;
}

static void
ow_l2uf_kick_init(struct ow_l2uf_kick *m)
{
    m->obs.name = __FILE__;
    m->obs.vif_added_fn = ow_l2uf_kick_vif_added_cb;
    m->obs.vif_changed_fn = ow_l2uf_kick_vif_changed_cb;
    m->obs.vif_removed_fn = ow_l2uf_kick_vif_removed_cb;
    ds_tree_init(&m->vifs_by_name, ds_str_cmp, struct ow_l2uf_kick_vif, node_by_name);
    ds_tree_init(&m->vifs_by_addr, (ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_l2uf_kick_vif, node_by_addr);
    ds_tree_init(&m->mlds, ds_str_cmp, struct ow_l2uf_kick_mld, node);
    ds_tree_init(&m->stas, (ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_l2uf_kick_sta, node);
    ds_tree_init(&m->netdevs, ds_str_cmp, struct ow_l2uf_kick_netdev, node);
    m->mux_deauth_fn = ow_l2uf_kick_mux_deauth_no_op;
    m->mux_delete_fn = ow_l2uf_kick_mux_delete_no_op;
}

static osw_sta_assoc_observer_t *
ow_l2uf_kick_alloc_sta_assoc_obs(struct ow_l2uf_kick *m)
{
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p, ow_l2uf_kick_sta_notify_cb, m);
    return osw_sta_assoc_observer_alloc(m->sta_assoc, p);
}

static void
ow_l2uf_kick_attach(struct ow_l2uf_kick *m)
{
    const bool delete_enabled = osw_etc_get("OW_L2UF_KICK_DELETE_DISABLE") ? false : true;

    m->mux_deauth_fn = osw_mux_request_sta_deauth;

    if (delete_enabled) {
        m->mux_delete_fn = osw_mux_request_sta_delete;
    }

    m->l2uf = OSW_MODULE_LOAD(osw_l2uf);
    m->sta_assoc = OSW_MODULE_LOAD(osw_sta_assoc);
    m->sta_assoc_obs = ow_l2uf_kick_alloc_sta_assoc_obs(m);
    osw_state_register_observer(&m->obs);
}

OSW_MODULE(ow_l2uf_kick)
{
    const bool is_disabled = (osw_etc_get("OW_L2UF_KICK_DISABLED") != NULL);
    const bool is_enabled = !is_disabled;
    static struct ow_l2uf_kick m;
    ow_l2uf_kick_init(&m);
    if (is_enabled) ow_l2uf_kick_attach(&m);
    LOGD(LOG_PREFIX("%s", is_enabled ? "enabled" : "disabled"));
    return &m;
}

OSW_UT(ow_l2uf_kick_non_mlo_no_l2uf)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const struct osw_hwaddr ap2 = { .octet = { 0, 0, 0, 0, 0, 3 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    const osw_sta_assoc_link_t link2 = { .local_sta_addr = ap2, .remote_sta_addr = sta1 };
    struct ow_l2uf_kick_evict *e;

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    struct ow_l2uf_kick_vif *vif2 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap2");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);
    ow_l2uf_kick_vif_set_addr(vif2, &ap2);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &sta1);
    sta->active.links[0] = link1;
    sta->active.count = 1;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    sta->active.links[0] = link2;
    sta->active.count = 1;
    sta->stale.links[0] = link1;
    sta->stale.count = 1;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC));
    assert(ds_tree_len(&m.stas) == 1);
    e = ds_tree_head(&sta->evicts);
    assert(e != NULL);
    assert(osw_hwaddr_is_equal(&e->ap_addr, &ap1));
    assert(osw_hwaddr_is_equal(&e->sta_addr, &sta1));
    assert(osw_timer_is_armed(&e->delete_expire));
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_DELETE_EXPIRE_SEC));
    e = ds_tree_head(&sta->evicts);
    assert(e != NULL);
    assert(osw_hwaddr_is_equal(&e->ap_addr, &ap1));
    assert(osw_hwaddr_is_equal(&e->sta_addr, &sta1));
    assert(osw_timer_is_armed(&e->deauth_expire));

    sta->active.count = 1;
    sta->stale.count = 0;
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);
    e = ds_tree_head(&sta->evicts);
    assert(e == NULL);
    assert(m.stats.delete == 0);
    assert(m.stats.deauth == 1);
    assert(m.stats.fail == 0);
}

OSW_UT(ow_l2uf_kick_non_mlo_l2uf)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    struct ow_l2uf_kick_evict *e;

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &sta1);
    sta->active.links[0] = link1;
    sta->active.count = 1;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &ap1);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC));
    e = ds_tree_head(&sta->evicts);
    assert(e != NULL);
    assert(osw_hwaddr_is_equal(&e->ap_addr, &ap1));
    assert(osw_hwaddr_is_equal(&e->sta_addr, &sta1));
    assert(osw_timer_is_armed(&e->delete_expire));

    sta->active.count = 0;
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 0);
    e = ds_tree_head(&sta->evicts);
    assert(e == NULL);
    assert(m.stats.delete == 1);
    assert(m.stats.deauth == 0);
    assert(m.stats.fail == 0);
}

OSW_UT(ow_l2uf_kick_non_mlo_l2uf_reconnect)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    struct ow_l2uf_kick_evict *e;

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &sta1);
    sta->active.links[0] = link1;
    sta->active.count = 1;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &ap1);
    ow_l2uf_kick_sta_set_recently_connected(sta);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC));
    e = ds_tree_head(&sta->evicts);
    assert(e == NULL);
}

OSW_UT(ow_l2uf_kick_non_mlo_stale_disarm)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const struct osw_hwaddr ap2 = { .octet = { 0, 0, 0, 0, 0, 3 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    const osw_sta_assoc_link_t link2 = { .local_sta_addr = ap2, .remote_sta_addr = sta1 };
    struct ow_l2uf_kick_evict *e;

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    struct ow_l2uf_kick_vif *vif2 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap2");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);
    ow_l2uf_kick_vif_set_addr(vif2, &ap2);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &sta1);
    sta->active.links[0] = link1;
    sta->active.count = 1;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    sta->active.links[0] = link2;
    sta->active.count = 1;
    sta->stale.links[0] = link1;
    sta->stale.count = 1;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC) / 2);
    assert(ds_tree_len(&m.stas) == 1);
    e = ds_tree_head(&sta->evicts);
    assert(e == NULL);

    sta->stale.count = 0;
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC) / 2);
    assert(ds_tree_len(&m.stas) == 1);
    e = ds_tree_head(&sta->evicts);
    assert(e == NULL);
}

OSW_UT(ow_l2uf_kick_non_mlo_l2uf_disarm)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    struct ow_l2uf_kick_evict *e;

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &sta1);
    sta->active.links[0] = link1;
    sta->active.count = 1;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &ap1);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);
    e = ds_tree_head(&sta->evicts);
    assert(e == NULL);

    sta->active.count = 0;
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 0);
}

OSW_UT(ow_l2uf_kick_non_mlo_fail)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    struct ow_l2uf_kick_evict *e;

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &sta1);
    sta->active.links[0] = link1;
    sta->active.count = 1;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &ap1);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC));
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_DELETE_EXPIRE_SEC));
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_DEAUTH_EXPIRE_SEC));
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);
    e = ds_tree_head(&sta->evicts);
    assert(e == NULL);
    assert(m.stats.delete == 0);
    assert(m.stats.deauth == 0);
    assert(m.stats.fail == 1);
}

OSW_UT(ow_l2uf_kick_mlo_no_l2uf)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr mld1 = { .octet = { 0, 0, 0, 0, 1, 0 } };
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr sta2 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const struct osw_hwaddr sta3 = { .octet = { 0, 0, 0, 0, 0, 3 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 0, 4 } };
    const struct osw_hwaddr ap2 = { .octet = { 0, 0, 0, 0, 0, 5 } };
    const struct osw_hwaddr ap3 = { .octet = { 0, 0, 0, 0, 0, 6 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    const osw_sta_assoc_link_t link2 = { .local_sta_addr = ap2, .remote_sta_addr = sta2 };
    const osw_sta_assoc_link_t link3 = { .local_sta_addr = ap3, .remote_sta_addr = sta3 };

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    struct ow_l2uf_kick_vif *vif2 = ow_l2uf_kick_vif_alloc(&m, "phy2", "ap2");
    struct ow_l2uf_kick_vif *vif3 = ow_l2uf_kick_vif_alloc(&m, "phy3", "ap3");
    struct ow_l2uf_kick_mld *mld = ow_l2uf_kick_mld_alloc(&m, "mld1");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);
    ow_l2uf_kick_vif_set_addr(vif2, &ap2);
    ow_l2uf_kick_vif_set_addr(vif3, &ap3);
    ow_l2uf_kick_vif_set_mld(vif1, mld);
    ow_l2uf_kick_vif_set_mld(vif2, mld);
    ow_l2uf_kick_vif_set_mld(vif3, mld);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &mld1);
    sta->active.links[0] = link1;
    sta->active.links[1] = link2;
    sta->active.count = 2;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    sta->active.links[0] = link3;
    sta->active.count = 1;
    sta->stale.links[0] = link1;
    sta->stale.links[1] = link2;
    sta->stale.count = 2;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC));
    assert(ds_tree_len(&m.stas) == 1);
    assert(ds_tree_len(&sta->evicts) == 2);

    sta->stale.count = 0;
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);
    assert(ds_tree_len(&sta->evicts) == 0);
    assert(m.stats.delete == 2);
    assert(m.stats.deauth == 0);
    assert(m.stats.fail == 0);
}

OSW_UT(ow_l2uf_kick_mlo_l2uf)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr mld1 = { .octet = { 0, 0, 0, 0, 1, 0 } };
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr sta2 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 0, 4 } };
    const struct osw_hwaddr ap2 = { .octet = { 0, 0, 0, 0, 0, 5 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    const osw_sta_assoc_link_t link2 = { .local_sta_addr = ap2, .remote_sta_addr = sta2 };

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    struct ow_l2uf_kick_vif *vif2 = ow_l2uf_kick_vif_alloc(&m, "phy2", "ap2");
    struct ow_l2uf_kick_mld *mld = ow_l2uf_kick_mld_alloc(&m, "mld1");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);
    ow_l2uf_kick_vif_set_addr(vif2, &ap2);
    ow_l2uf_kick_vif_set_mld(vif1, mld);
    ow_l2uf_kick_vif_set_mld(vif2, mld);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &mld1);
    sta->active.links[0] = link1;
    sta->active.links[1] = link2;
    sta->active.count = 2;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &ap1);
    ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &ap2);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC));
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);
    assert(ds_tree_len(&sta->evicts) == 2);

    sta->active.count = 1;
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);

    assert(m.stats.delete == 1);
    assert(m.stats.deauth == 0);
    assert(m.stats.fail == 0);

    sta->active.count = 0;
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 0);

    assert(m.stats.delete == 2);
    assert(m.stats.deauth == 0);
    assert(m.stats.fail == 0);
}

OSW_UT(ow_l2uf_kick_mlo_cross)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr stamld1 = { .octet = { 0, 0, 0, 0, 1, 0 } };
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr sta2 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 0, 3 } };
    const struct osw_hwaddr ap2 = { .octet = { 0, 0, 0, 0, 0, 4 } };
    const struct osw_hwaddr ap3 = { .octet = { 0, 0, 0, 0, 0, 5 } };
    const struct osw_hwaddr ap4 = { .octet = { 0, 0, 0, 0, 0, 6 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    const osw_sta_assoc_link_t link2 = { .local_sta_addr = ap2, .remote_sta_addr = sta2 };
    const osw_sta_assoc_link_t link3 = { .local_sta_addr = ap3, .remote_sta_addr = sta1 };
    const osw_sta_assoc_link_t link4 = { .local_sta_addr = ap4, .remote_sta_addr = sta2 };

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    struct ow_l2uf_kick_vif *vif2 = ow_l2uf_kick_vif_alloc(&m, "phy2", "ap2");
    struct ow_l2uf_kick_vif *vif3 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap3");
    struct ow_l2uf_kick_vif *vif4 = ow_l2uf_kick_vif_alloc(&m, "phy2", "ap4");
    struct ow_l2uf_kick_mld *apmld1 = ow_l2uf_kick_mld_alloc(&m, "apmld1");
    struct ow_l2uf_kick_mld *apmld2 = ow_l2uf_kick_mld_alloc(&m, "apmld2");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);
    ow_l2uf_kick_vif_set_addr(vif2, &ap2);
    ow_l2uf_kick_vif_set_addr(vif3, &ap3);
    ow_l2uf_kick_vif_set_addr(vif4, &ap4);
    ow_l2uf_kick_vif_set_mld(vif1, apmld1);
    ow_l2uf_kick_vif_set_mld(vif2, apmld1);
    ow_l2uf_kick_vif_set_mld(vif3, apmld2);
    ow_l2uf_kick_vif_set_mld(vif4, apmld2);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &stamld1);
    sta->active.links[0] = link1;
    sta->active.links[1] = link2;
    sta->active.count = 2;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    sta->active.links[0] = link3;
    sta->active.links[1] = link4;
    sta->active.count = 2;
    sta->stale.links[0] = link1;
    sta->stale.links[1] = link2;
    sta->stale.count = 2;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC));
    assert(ds_tree_len(&m.stas) == 1);
    assert(ds_tree_len(&sta->evicts) == 2);

    sta->stale.count = 0;
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);

    assert(m.stats.delete == 2);
    assert(m.stats.deauth == 0);
    assert(m.stats.fail == 0);
}

OSW_UT(ow_l2uf_kick_non_mlo_diff_vif)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const struct osw_hwaddr ap2 = { .octet = { 0, 0, 0, 0, 0, 3 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    struct ow_l2uf_kick_evict *e;

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    struct ow_l2uf_kick_vif *vif2 = ow_l2uf_kick_vif_alloc(&m, "phy2", "ap2");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);
    ow_l2uf_kick_vif_set_addr(vif2, &ap2);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &sta1);
    sta->active.links[0] = link1;
    sta->active.count = 1;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &ap2);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC));
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_DELETE_EXPIRE_SEC));
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_DEAUTH_EXPIRE_SEC));
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);
    e = ds_tree_head(&sta->evicts);
    assert(e == NULL);
    assert(m.stats.delete == 0);
    assert(m.stats.deauth == 0);
    assert(m.stats.fail == 0);

    ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &ap1);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC));
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);
    assert(ds_tree_len(&sta->evicts) == 1);
    sta->active.count = 0;
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 0);
    assert(m.stats.delete == 1);
    assert(m.stats.deauth == 0);
    assert(m.stats.fail == 0);
}

OSW_UT(ow_l2uf_kick_mlo_partial_vif)
{
    struct ow_l2uf_kick m = {0};
    ow_l2uf_kick_init(&m);
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr sta2 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 1, 1 } };
    const struct osw_hwaddr ap2 = { .octet = { 0, 0, 0, 0, 1, 2 } };
    const struct osw_hwaddr mld1 = { .octet = { 0, 0, 0, 0, 2, 1 } };
    const osw_sta_assoc_link_t link1 = { .local_sta_addr = ap1, .remote_sta_addr = sta1 };
    const osw_sta_assoc_link_t link2 = { .local_sta_addr = ap2, .remote_sta_addr = sta2 };

    struct ow_l2uf_kick_vif *vif1 = ow_l2uf_kick_vif_alloc(&m, "phy1", "ap1");
    struct ow_l2uf_kick_vif *vif2 = ow_l2uf_kick_vif_alloc(&m, "phy2", "ap2");
    ow_l2uf_kick_vif_set_addr(vif1, &ap1);
    ow_l2uf_kick_vif_set_addr(vif2, &ap2);

    struct ow_l2uf_kick_sta *sta = ow_l2uf_kick_sta_get(&m, &mld1);
    sta->active.links[0] = link1;
    sta->active.links[1] = link2;
    sta->active.count = 2;
    ow_l2uf_kick_sta_set_recently_connected(sta);
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    osw_ut_time_advance(OSW_TIME_SEC(10));
    assert(ds_tree_len(&m.stas) == 1);

    ow_l2uf_kick_sta_set_l2uf_seen_on(sta, &ap2);
    osw_ut_time_advance(OSW_TIME_SEC(OW_L2UF_KICK_WORK_SEC));
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 1);
    assert(ds_tree_len(&sta->evicts) == 2);
    sta->active.count = 0;
    ow_l2uf_kick_sta_check_eviction_status(sta);
    ow_l2uf_kick_sta_gc(sta);
    assert(ds_tree_len(&m.stas) == 0);
    assert(m.stats.delete == 2);
    assert(m.stats.deauth == 0);
    assert(m.stats.fail == 0);
}
