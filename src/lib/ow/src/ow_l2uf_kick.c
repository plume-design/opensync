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

/* osw */
#include <osw_module.h>
#include <osw_state.h>
#include <osw_mux.h>
#include <osw_types.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_l2uf.h>

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

#define OW_L2UF_KICK_DEAUTH_EXPIRE_SEC 3
#define OW_L2UF_KICK_DELETE_EXPIRE_SEC 1

#define LOG_PREFIX(fmt, ...) \
    "ow: l2uf: " fmt, \
        ##__VA_ARGS__

#define LOG_PREFIX_VIF(vif, fmt, ...) \
    LOG_PREFIX( \
        "%s/%s: " fmt, \
        (vif)->phy_name, \
        (vif)->vif_name, \
        ##__VA_ARGS__)

#define LOG_PREFIX_SA(vif, sa, fmt, ...) \
    LOG_PREFIX_VIF( \
        vif, \
        OSW_HWADDR_FMT": " fmt, \
        OSW_HWADDR_ARG(sa), \
        ##__VA_ARGS__)

#define LOG_PREFIX_STA(sta, fmt, ...) \
    LOG_PREFIX_SA((sta)->vif, &(sta)->sta_addr, fmt, ##__VA_ARGS__)

typedef bool
ow_l2uf_kick_mux_deauth_fn_t(const char *phy_name,
                             const char *vif_name,
                             const struct osw_hwaddr *mac_addr,
                             int dot11_reason_code);

typedef bool
ow_l2uf_kick_mux_delete_fn_t(const char *phy_name,
                             const char *vif_name,
                             const struct osw_hwaddr *mac_addr);

struct ow_l2uf_kick {
    struct osw_state_observer obs;
    struct ds_tree vifs;
    ow_l2uf_kick_mux_deauth_fn_t *mux_deauth_fn;
    ow_l2uf_kick_mux_delete_fn_t *mux_delete_fn;
};

enum ow_l2uf_kick_policy {
    OW_L2UF_KICK_UNSPEC,
    OW_L2UF_KICK_STA_DELETE,
};

struct ow_l2uf_kick_vif {
    struct ow_l2uf_kick *m;
    char *phy_name;
    char *vif_name;
    struct osw_l2uf_if *i;
    struct ds_tree_node node;
    enum ow_l2uf_kick_policy policy;
    struct ds_tree stas;
};

struct ow_l2uf_kick_sta {
    struct ow_l2uf_kick_vif *vif;
    struct osw_hwaddr sta_addr;
    struct ds_tree_node node;
    struct osw_timer deauth_expiry;
    struct osw_timer delete_expiry;
    time_t connected_at;
};

static enum ow_l2uf_kick_policy
ow_l2uf_kick_get_policy(const struct osw_state_vif_info *info)
{
    const struct osw_drv_vif_state *state = info->drv_state;
    switch (state->vif_type) {
        case OSW_VIF_AP: return OW_L2UF_KICK_STA_DELETE;
        case OSW_VIF_AP_VLAN: return OW_L2UF_KICK_UNSPEC;
        case OSW_VIF_STA: return OW_L2UF_KICK_UNSPEC;
        case OSW_VIF_UNDEFINED: return OW_L2UF_KICK_UNSPEC;
    }
    WARN_ON(1);
    return OW_L2UF_KICK_UNSPEC;
}

static void
ow_l2uf_kick_sta_deauth(struct ow_l2uf_kick_sta *sta)
{
    struct ow_l2uf_kick_vif *vif = sta->vif;
    struct ow_l2uf_kick *m = vif->m;
    const char *phy_name = vif->phy_name;
    const char *vif_name = vif->vif_name;
    const struct osw_hwaddr *sta_addr = &sta->sta_addr;
    const int reason_unspec = 1;

    LOGD(LOG_PREFIX_STA(sta, "attempting to deauth"));
    const bool ok = m->mux_deauth_fn(phy_name, vif_name, sta_addr, reason_unspec);
    const bool failed = !ok;
    const bool probably_not_implemented = failed;
    if (WARN_ON(probably_not_implemented)) return;

    struct osw_timer *deauth = &sta->deauth_expiry;
    const uint64_t now = osw_time_mono_clk();
    const uint64_t at = now + OSW_TIME_SEC(OW_L2UF_KICK_DEAUTH_EXPIRE_SEC);
    osw_timer_arm_at_nsec(deauth, at);
}

static void
ow_l2uf_kick_sta_delete(struct ow_l2uf_kick_sta *sta)
{
    struct ow_l2uf_kick_vif *vif = sta->vif;
    struct ow_l2uf_kick *m = vif->m;
    const char *phy_name = vif->phy_name;
    const char *vif_name = vif->vif_name;
    const struct osw_hwaddr *sta_addr = &sta->sta_addr;

    osw_timer_disarm(&sta->deauth_expiry);

    LOGD(LOG_PREFIX_STA(sta, "attempting to delete"));
    const bool ok = m->mux_delete_fn(phy_name, vif_name, sta_addr);
    const bool failed = !ok;
    const bool probably_not_implemented = failed;
    if (WARN_ON(probably_not_implemented)) return;

    struct osw_timer *t = &sta->delete_expiry;
    const uint64_t now = osw_time_mono_clk();
    const uint64_t at = now + OSW_TIME_SEC(OW_L2UF_KICK_DELETE_EXPIRE_SEC);
    osw_timer_arm_at_nsec(t, at);
}

static void
ow_l2uf_kick_sta_deauth_expiry_cb(struct osw_timer *t)
{
    struct ow_l2uf_kick_sta *sta = container_of(t, struct ow_l2uf_kick_sta, deauth_expiry);
    LOGW(LOG_PREFIX_STA(sta, "failed to delete sta: timed out"));
}

static void
ow_l2uf_kick_sta_delete_expiry_cb(struct osw_timer *t)
{
    struct ow_l2uf_kick_sta *sta = container_of(t, struct ow_l2uf_kick_sta, delete_expiry);
    LOGD(LOG_PREFIX_STA(sta, "failed to delete sta: timed out; possible osw or wlan driver bug, falling back to deauth"));
    ow_l2uf_kick_sta_deauth(sta);
}

static const char *
ow_l2uf_kick_sta_get_eviction_type(const struct ow_l2uf_kick_sta *sta)
{
    if (osw_timer_is_armed(&sta->delete_expiry)) return "delete";
    if (osw_timer_is_armed(&sta->deauth_expiry)) return "deauth";
    return NULL;
}

static void
ow_l2uf_kick_sta_disarm(struct ow_l2uf_kick_sta *sta)
{
    const char *type = ow_l2uf_kick_sta_get_eviction_type(sta);
    if (type != NULL) {
        LOGI(LOG_PREFIX_STA(sta, "successfully evicted ghost station through %s", type));
    }

    osw_timer_disarm(&sta->deauth_expiry);
    osw_timer_disarm(&sta->delete_expiry);
}

static void
ow_l2uf_kick_sta_update(struct ow_l2uf_kick_vif *vif,
                        const struct osw_state_sta_info *info,
                        const bool exists)
{
    if (WARN_ON(vif == NULL)) return;

    struct ds_tree *stas = &vif->stas;
    const struct osw_hwaddr *sta_addr = info->mac_addr;
    struct ow_l2uf_kick_sta *sta = ds_tree_find(stas, sta_addr);
    const bool allocated = (sta != NULL);
    const bool want = (exists);
    const bool need_alloc = (want && !allocated);
    const bool need_free = (allocated && !want);
    const bool reconnected = (allocated && (sta->connected_at != info->connected_at));

    LOGT(LOG_PREFIX_VIF(vif, "updating "OSW_HWADDR_FMT" exists=%d sta=%p alloc=%d free=%d reconn=%d",
                        OSW_HWADDR_ARG(sta_addr),
                        exists,
                        sta,
                        need_alloc,
                        need_free,
                        reconnected));

    if (need_alloc) {
        WARN_ON(sta != NULL);

        sta = CALLOC(1, sizeof(*sta));
        sta->vif = vif;
        sta->sta_addr = *sta_addr;
        osw_timer_init(&sta->deauth_expiry,
                       ow_l2uf_kick_sta_deauth_expiry_cb);
        osw_timer_init(&sta->delete_expiry,
                       ow_l2uf_kick_sta_delete_expiry_cb);
        ds_tree_insert(stas, sta, &sta->sta_addr);
        LOGT(LOG_PREFIX_STA(sta, "allocated"));
    }

    if (sta != NULL) {
        sta->connected_at = info->connected_at;
    }

    if (reconnected) {
        WARN_ON(sta == NULL);
        ow_l2uf_kick_sta_disarm(sta);
    }

    if (need_free) {
        WARN_ON(sta == NULL);

        LOGT(LOG_PREFIX_STA(sta, "freeing"));
        ow_l2uf_kick_sta_disarm(sta);
        ds_tree_remove(stas, sta);
        FREE(sta);
    }
}

static void
ow_l2uf_kick_sta_delete_cb(struct osw_l2uf_if *i,
                           const struct osw_hwaddr *sa_addr)
{
    const struct osw_hwaddr *sta_addr = sa_addr;
    struct ow_l2uf_kick_vif *vif = osw_l2uf_if_get_data(i);
    struct ds_tree *stas = &vif->stas;
    struct ow_l2uf_kick_sta *sta = ds_tree_find(stas, sta_addr);
    if (sta == NULL) {
        LOGD(LOG_PREFIX_VIF(vif, "sa_addr "OSW_HWADDR_FMT" not associated, ignoring",
                            OSW_HWADDR_ARG(sa_addr)));
        return;
    }

    ow_l2uf_kick_sta_delete(sta);
}

static osw_l2uf_seen_fn_t *
ow_l2uf_kick_policy_into_seen_fn(enum ow_l2uf_kick_policy policy)
{
    switch (policy) {
        case OW_L2UF_KICK_UNSPEC: return NULL;
        case OW_L2UF_KICK_STA_DELETE: return ow_l2uf_kick_sta_delete_cb;
    }
    WARN_ON(1);
    return NULL;
}

static void
ow_l2uf_kick_vif_update(struct ow_l2uf_kick *m,
                        const struct osw_state_vif_info *info,
                        const bool exists)
{
    const char *phy_name = info->phy->phy_name;
    const char *vif_name = info->vif_name;
    struct ds_tree *vifs = &m->vifs;
    struct ow_l2uf_kick_vif *vif = ds_tree_find(vifs, vif_name);
    const bool allocated = (vif != NULL);
    const enum ow_l2uf_kick_policy policy = ow_l2uf_kick_get_policy(info);
    const bool want = (exists);
    const bool need_alloc = (want && !allocated);
    const bool need_free = (allocated && !want);
    osw_l2uf_seen_fn_t *seen_fn = ow_l2uf_kick_policy_into_seen_fn(policy);

    LOGT(LOG_PREFIX("updating %s exists=%d vif=%p alloc=%d free=%d want=%d policy=%d",
                    vif_name,
                    exists,
                    vif,
                    need_alloc,
                    need_free,
                    want,
                    policy));

    WARN_ON(need_alloc && need_free);

    if (need_alloc) {
        WARN_ON(vif != NULL);

        struct osw_l2uf_if *i = osw_l2uf_if_alloc(vif_name);
        if (WARN_ON(i == NULL)) return;

        vif = CALLOC(1, sizeof(*vif));
        vif->m = m;
        vif->phy_name = STRDUP(phy_name);
        vif->vif_name = STRDUP(vif_name);
        vif->i = i;

        osw_l2uf_if_set_data(i, vif);
        ds_tree_init(&vif->stas,
                     (ds_key_cmp_t *)osw_hwaddr_cmp,
                     struct ow_l2uf_kick_sta,
                     node);
        ds_tree_insert(vifs, vif, vif->vif_name);
        LOGD(LOG_PREFIX_VIF(vif, "allocated"));
    }

    if (vif != NULL) {
        osw_l2uf_if_set_seen_fn(vif->i, seen_fn);
    }

    if (need_free) {
        WARN_ON(vif == NULL);
        WARN_ON(ds_tree_is_empty(&vif->stas) == false);

        LOGD(LOG_PREFIX_VIF(vif, "freeing"));
        osw_l2uf_if_free(vif->i);
        ds_tree_remove(vifs, vif);
        FREE(vif->phy_name);
        FREE(vif->vif_name);
        FREE(vif);
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

static void
ow_l2uf_kick_sta_connected_cb(struct osw_state_observer *obs,
                              const struct osw_state_sta_info *info)
{
    struct ow_l2uf_kick *m = container_of(obs, struct ow_l2uf_kick, obs);
    struct ds_tree *vifs = &m->vifs;
    const char *vif_name = info->vif->vif_name;
    struct ow_l2uf_kick_vif *vif = ds_tree_find(vifs, vif_name);
    ow_l2uf_kick_sta_update(vif, info, true);
}

static void
ow_l2uf_kick_sta_changed_cb(struct osw_state_observer *obs,
                            const struct osw_state_sta_info *info)
{
    struct ow_l2uf_kick *m = container_of(obs, struct ow_l2uf_kick, obs);
    struct ds_tree *vifs = &m->vifs;
    const char *vif_name = info->vif->vif_name;
    struct ow_l2uf_kick_vif *vif = ds_tree_find(vifs, vif_name);
    ow_l2uf_kick_sta_update(vif, info, true);
}

static void
ow_l2uf_kick_sta_disconnected_cb(struct osw_state_observer *obs,
                                 const struct osw_state_sta_info *info)
{
    struct ow_l2uf_kick *m = container_of(obs, struct ow_l2uf_kick, obs);
    struct ds_tree *vifs = &m->vifs;
    const char *vif_name = info->vif->vif_name;
    struct ow_l2uf_kick_vif *vif = ds_tree_find(vifs, vif_name);
    ow_l2uf_kick_sta_update(vif, info, false);
}

static bool
ow_l2uf_kick_mux_deauth_stub_cb(const char *phy_name,
                                const char *vif_name,
                                const struct osw_hwaddr *sta_addr,
                                int dot11_reason_code)
{
    return false;
}

static bool
ow_l2uf_kick_mux_delete_stub_cb(const char *phy_name,
                                const char *vif_name,
                                const struct osw_hwaddr *sta_addr)
{
    return false;
}

static void
ow_l2uf_kick_init(struct ow_l2uf_kick *m)
{
    m->obs.name = __FILE__;
    m->obs.vif_added_fn = ow_l2uf_kick_vif_added_cb;
    m->obs.vif_changed_fn = ow_l2uf_kick_vif_changed_cb;
    m->obs.vif_removed_fn = ow_l2uf_kick_vif_removed_cb;
    m->obs.sta_connected_fn = ow_l2uf_kick_sta_connected_cb;
    m->obs.sta_changed_fn = ow_l2uf_kick_sta_changed_cb;
    m->obs.sta_disconnected_fn = ow_l2uf_kick_sta_disconnected_cb;
    ds_tree_init(&m->vifs, ds_str_cmp, struct ow_l2uf_kick_vif, node);
    m->mux_deauth_fn = ow_l2uf_kick_mux_deauth_stub_cb;
    m->mux_delete_fn = ow_l2uf_kick_mux_delete_stub_cb;
}

static void
ow_l2uf_kick_attach(struct ow_l2uf_kick *m)
{
    OSW_MODULE_LOAD(osw_l2uf);
    osw_state_register_observer(&m->obs);
    m->mux_deauth_fn = osw_mux_request_sta_deauth;
    m->mux_delete_fn = osw_mux_request_sta_delete;
}

OSW_MODULE(ow_l2uf_kick)
{
    const bool is_disabled = (getenv("OW_L2UF_KICK_DISABLED") != NULL);
    const bool is_enabled = !is_disabled;
    static struct ow_l2uf_kick m;
    ow_l2uf_kick_init(&m);
    if (is_enabled) ow_l2uf_kick_attach(&m);
    LOGD(LOG_PREFIX("%s", is_enabled ? "enabled" : "disabled"));
    return &m;
}
