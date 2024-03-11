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

#include <util.h>
#include <os.h>
#include <log.h>
#include <memutil.h>
#include <const.h>
#include <module.h>
#include <osw_ut.h>
#include <osw_conf.h>
#include <osw_state.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_confsync.h>
#include <osw_drv_dummy.h>
#include <osw_mux.h>
#include <osw_module.h>

#define OSW_CONFSYNC_RETRY_SECONDS_DEFAULT 30.0

/* Starting up interfaces in most cases is not
 * instantaneous. If a mutator happens to invalidate its
 * config while a configuration is being applied that is
 * bringing an interface up, it can interrupt that. In worst
 * case this mutator is coupled with partial state updates
 * of the bringup itself. If that happens osw_confsync could
 * enter a live-lock where it keeps on reconfiguring and
 * nothing gets a chance to finish configuring.
 *
 * As such this tries to provide a grace period.
 *
 * Why this particular time value? When hostapd is starting
 * up it can perform a "country update" which has a timeout
 * of 5s. Various kernel drivers may need to talk to a
 * remote WLAN CPU which can also take 1s to do its job when
 * starting up an AP, especially if it needs to (re)load the
 * microcode. To provide extra leeway the 10s wait period
 * seems like a safe bet.
 *
 * This _does not_ intend to cover CAC periods although it
 * probably could. If the need arises this should be fairly
 * easy to rework.
 */
#define OSW_CONFSYNC_ENABLE_PERIOD_SEC 10

#define LOG_PREFIX(fmt, ...) "osw: confsync: " fmt, ## __VA_ARGS__
#define LOG_PREFIX_DEFER(defer, fmt, ...) \
    LOG_PREFIX("defer: %s%s%s: " fmt, \
        defer->name, \
        defer->deferred ? " deferred" : "", \
        osw_timer_is_armed(&defer->expiry) ? "" : " expired", \
        ## __VA_ARGS__)

/* FIXME: This is temporary solution to avoid changing too
 * many things at once.  Before OSW_MODULE can be fully
 * utilized a way to mock-up for UT is necessarry.
 */
typedef struct ds_tree *osw_confsync_build_conf_fn_t(void);

struct osw_confsync {
    struct ds_dlist changed_fns;
    enum osw_confsync_state state;
    struct osw_state_observer state_obs;
    struct osw_conf_observer conf_obs;
    osw_confsync_build_conf_fn_t *build_conf;
    ev_idle work;
    ev_timer retry;
    bool attached;
    bool settled;
    struct ds_tree defers;
};

struct osw_confsync_defer {
    struct osw_confsync *confsync;
    struct ds_tree_node node;
    char *name;
    struct osw_timer expiry;
    bool deferred;
};

struct osw_confsync_arg {
    struct osw_confsync *confsync;
    struct osw_drv_conf *drv_conf;
    const struct osw_drv_phy_state *sphy;
    struct osw_drv_phy_config *dphy;
    struct osw_conf_phy *cphy;
    struct osw_conf_vif *cvif;
    struct ds_tree *phy_tree;
    bool debug;
};

struct osw_confsync_changed {
    struct osw_confsync *cs;
    struct ds_dlist_node node;
    char *name;
    osw_confsync_changed_fn_t *fn;
    void *fn_priv;
};

static void
osw_confsync_set_state(struct osw_confsync *cs,
                       enum osw_confsync_state s);

static bool
osw_confsync_defer_is_pending(struct osw_confsync *cs)
{
    struct osw_confsync_defer *defer;
    ds_tree_foreach(&cs->defers, defer) {
        return true;
    }
    return false;
}

static void
osw_confsync_defer_drop(struct osw_confsync_defer *defer)
{
    LOGD(LOG_PREFIX_DEFER(defer, "dropping"));
    ds_tree_remove(&defer->confsync->defers, defer);
    osw_timer_disarm(&defer->expiry);
    FREE(defer->name);
    FREE(defer);
}

static void
osw_confsync_defer_expiry_cb(struct osw_timer *timer)
{
    struct osw_confsync_defer *defer = container_of(timer, struct osw_confsync_defer, expiry);
    LOGI(LOG_PREFIX_DEFER(defer, "expired"));
    if (defer->deferred) {
        LOGD(LOG_PREFIX_DEFER(defer, "requesting because was deferred before"));
        osw_confsync_set_state(defer->confsync, OSW_CONFSYNC_REQUESTING);
    }
}

enum osw_confsync_defer_state {
    OSW_CONFSYNC_DEFER_STARTED,
    OSW_CONFSYNC_DEFER_RUNNING,
    OSW_CONFSYNC_DEFER_EXPIRED,
};

static enum osw_confsync_defer_state
osw_confsync_defer_start_with_time(struct osw_confsync *cs,
                                   const char *name,
                                   uint64_t now_mono)
{
    ASSERT(cs != NULL, "");
    ASSERT(name != NULL, "");

    struct osw_confsync_defer *defer = ds_tree_find(&cs->defers, name);
    const uint64_t expire_at = now_mono
                             + OSW_TIME_SEC(OSW_CONFSYNC_ENABLE_PERIOD_SEC);

    if (defer != NULL) {
        if (defer->deferred == false) {
            LOGI(LOG_PREFIX_DEFER(defer, "deferring request, will try later"));
            defer->deferred = true;
        }
        if (osw_timer_is_armed(&defer->expiry)) {
            return OSW_CONFSYNC_DEFER_RUNNING;
        }
        else {
            return OSW_CONFSYNC_DEFER_EXPIRED;
        }
    }

    defer = CALLOC(1, sizeof(*defer));
    defer->confsync = cs;
    defer->name = STRDUP(name);
    osw_timer_init(&defer->expiry, osw_confsync_defer_expiry_cb);
    osw_timer_arm_at_nsec(&defer->expiry, expire_at);
    ds_tree_insert(&cs->defers, defer, defer->name);
    LOGD(LOG_PREFIX_DEFER(defer, "started"));
    return OSW_CONFSYNC_DEFER_STARTED;
}

static enum osw_confsync_defer_state
osw_confsync_defer_start(struct osw_confsync *cs,
                         const char *name)
{
    return osw_confsync_defer_start_with_time(cs, name, osw_time_mono_clk());
}

static void
osw_confsync_defer_disarm(struct osw_confsync *cs,
                          const char *name)
{
    struct osw_confsync_defer *defer = ds_tree_find(&cs->defers, name);
    if (defer == NULL) return;
    if (defer->deferred) {
        LOGI(LOG_PREFIX_DEFER(defer, "disarming because configuration finished"));
    }
    osw_confsync_defer_drop(defer);
}

static void
osw_confsync_defer_flush(struct osw_confsync *cs)
{
    struct osw_confsync_defer *defer;
    while ((defer = ds_tree_head(&cs->defers)) != NULL) {
        if (defer->deferred) {
            LOGI(LOG_PREFIX_DEFER(defer, "flushing, dropping request"));
        }
        else {
            LOGD(LOG_PREFIX_DEFER(defer, "flushing"));
        }
        osw_confsync_defer_drop(defer);
    }
}

static void
osw_confsync_defer_flush_expired(struct osw_confsync *cs)
{
    struct osw_confsync_defer *defer;
    struct osw_confsync_defer *tmp;
    ds_tree_foreach_safe(&cs->defers, defer, tmp) {
        if (osw_timer_is_armed(&defer->expiry) == false) {
            osw_confsync_defer_drop(defer);
        }
    }
}

static enum osw_confsync_defer_state
osw_confsync_defer_vif_enable_start(struct osw_confsync *cs,
                                    const char *vif_name)
{
    return osw_confsync_defer_start(cs, strfmta("vif:%s", vif_name));
}

static void
osw_confsync_defer_vif_enable_stop(struct osw_confsync *cs,
                                   const struct osw_state_vif_info *vif)
{
    if (vif->drv_state->status != OSW_VIF_ENABLED) return;
    osw_confsync_defer_disarm(cs, strfmta("vif:%s", vif->vif_name));
}

static void
osw_confsync_build_phy_debug(const struct osw_drv_phy_config *cmd,
                             const struct osw_conf_phy *conf,
                             const struct osw_drv_phy_state *state)
{
    const char *phy = cmd->phy_name;
    bool notified = false;

    if (cmd->enabled_changed) {
        LOGI("osw: confsync: %s: enabled: %d -> %d",
             phy, state->enabled, conf->enabled);
        notified = true;
    }

    if (cmd->tx_chainmask_changed) {
        LOGI("osw: confsync: %s: tx_chainmask: 0x%04x -> 0x%04x",
             phy, state->tx_chainmask, conf->tx_chainmask);
        notified = true;
    }

    if (cmd->radar_changed) {
        const char *from = osw_radar_to_str(state->radar);
        const char *to = osw_radar_to_str(conf->radar);
        LOGI("osw: confsync: %s: radar: %s -> %s", phy, from, to);
        notified = true;
    }

    if (cmd->reg_domain_changed) {
        LOGI("osw: confsync: %s: radar: "OSW_REG_DOMAIN_FMT" -> "OSW_REG_DOMAIN_FMT,
             phy,
             OSW_REG_DOMAIN_ARG(&state->reg_domain),
             OSW_REG_DOMAIN_ARG(&conf->reg_domain));
        notified = true;
    }

    if (cmd->mbss_tx_vif_name_changed) {
        LOGI("osw: confsync: %s: mbss_tx_vif_name: "OSW_IFNAME_FMT" -> "OSW_IFNAME_FMT,
             phy,
             OSW_IFNAME_ARG(&state->mbss_tx_vif_name),
             OSW_IFNAME_ARG(&conf->mbss_tx_vif_name));
        notified = true;
    }

    if (cmd->changed && !notified) {
        LOGW("osw: confsync: %s: changed, but missing specific attribute printout", phy);
    }
}

static void
osw_confsync_build_vif_ap_debug(const char *phy,
                                const char *vif,
                                const struct osw_drv_vif_config_ap *cmd,
                                const struct osw_conf_vif_ap *conf,
                                const struct osw_drv_vif_state_ap *state,
                                bool *notified)
{
    if (cmd->bridge_if_name_changed) {
        const int max = ARRAY_SIZE(conf->bridge_if_name.buf);
        LOGI("osw: confsync: %s/%s: bridge_if_name: '%.*s' -> '%.*s'",
             phy, vif,
             max, state->bridge_if_name.buf,
             max, conf->bridge_if_name.buf);
        *notified = true;
    }

    if (cmd->beacon_interval_tu_changed) {
        LOGI("osw: confsync: %s/%s: beacon_interval_tu: %d -> %d",
             phy, vif, state->beacon_interval_tu, conf->beacon_interval_tu);
        *notified = true;
    }

    if (cmd->isolated_changed) {
        // FIXME: isolate vs isolated
        LOGI("osw: confsync: %s/%s: isolate: %d -> %d",
             phy, vif, state->isolated, conf->isolated);
        *notified = true;
    }

    if (cmd->ssid_hidden_changed) {
        LOGI("osw: confsync: %s/%s: ssid_hidden: %d -> %d",
             phy, vif, state->ssid_hidden, conf->ssid_hidden);
        *notified = true;
    }

    if (cmd->mcast2ucast_changed) {
        LOGI("osw: confsync: %s/%s: mcast2ucast: %d -> %d",
             phy, vif, state->mcast2ucast, conf->mcast2ucast);
        *notified = true;
    }

    if (cmd->wps_pbc_changed) {
        LOGI("osw: confsync: %s/%s: wps_pbc: %d -> %d",
             phy, vif, state->wps_pbc, conf->wps_pbc);
        *notified = true;
    }

    if (cmd->multi_ap_changed) {
        char *from = osw_multi_ap_into_str(&state->multi_ap);
        char *to = osw_multi_ap_into_str(&conf->multi_ap);
        LOGI("osw: confsync: %s/%s: multi_ap: %s -> %s",
             phy, vif, from, to);
        FREE(from);
        FREE(to);
        *notified = true;
    }

    if (cmd->channel_changed) {
        struct osw_channel s = state->channel;
        struct osw_channel c = conf->channel;
        LOGI("osw: confsync: %s/%s: channel: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
             phy, vif,
             OSW_CHANNEL_ARG(&s),
             OSW_CHANNEL_ARG(&c));
        *notified = true;
    }

    if (cmd->mode_changed) {
        char from[128];
        char to[128];
        osw_ap_mode_to_str(from, sizeof(from), &state->mode);
        osw_ap_mode_to_str(to, sizeof(to), &conf->mode);
        LOGI("osw: confsync: %s/%s: mode: %s -> %s", phy, vif, from, to);
        *notified = true;
    }

    if (cmd->acl_policy_changed) {
        const char *from = osw_acl_policy_to_str(state->acl_policy);
        const char *to = osw_acl_policy_to_str(conf->acl_policy);
        LOGI("osw: confsync: %s/%s: acl policy: %s -> %s", phy, vif, from, to);
        *notified = true;
    }

    if (cmd->ssid_changed) {
        LOGI("osw: confsync: %s/%s: ssid: "OSW_SSID_FMT" -> "OSW_SSID_FMT,
             phy, vif,
             OSW_SSID_ARG(&state->ssid),
             OSW_SSID_ARG(&conf->ssid));
        *notified = true;
    }

    if (cmd->wpa_changed) {
        char from[128];
        char to[128];
        osw_wpa_to_str(from, sizeof(from), &state->wpa);
        osw_wpa_to_str(to, sizeof(to), &conf->wpa);
        LOGI("osw: confsync: %s/%s: wpa: %s -> %s", phy, vif, from, to);
        *notified = true;
    }

    if (cmd->acl_changed) {
        size_t i;

        for (i = 0; i < cmd->acl_add.count; i++) {
            const struct osw_hwaddr *mac = &cmd->acl_add.list[i];
            LOGI("osw: confsync: %s/%s: acl: adding: "OSW_HWADDR_FMT,
                 phy, vif, OSW_HWADDR_ARG(mac));
        }

        for (i = 0; i < cmd->acl_del.count; i++) {
            const struct osw_hwaddr *mac = &cmd->acl_del.list[i];
            LOGI("osw: confsync: %s/%s: acl: removing: "OSW_HWADDR_FMT,
                 phy, vif, OSW_HWADDR_ARG(mac));
        }

        *notified = true;
    }

    if (cmd->psk_list_changed) {
        char from[1024];
        char to[1024];
        osw_ap_psk_list_to_str(from, sizeof(from), &state->psk_list);
        osw_conf_ap_psk_tree_to_str(to, sizeof(to), &conf->psk_tree);
        LOGI("osw: confsync: %s/%s: psk: %s -> %s", phy, vif, from, to);
        *notified = true;
    }

    if (cmd->neigh_list_changed) {
        char add[1024];
        char mod[1024];
        char del[1024];
        char from[1024];
        char to[1024];
        osw_neigh_list_to_str(add, sizeof(add), &cmd->neigh_add_list);
        osw_neigh_list_to_str(mod, sizeof(mod), &cmd->neigh_mod_list);
        osw_neigh_list_to_str(del, sizeof(del), &cmd->neigh_del_list);
        osw_neigh_list_to_str(from, sizeof(from), &state->neigh_list);
        osw_conf_neigh_tree_to_str(to, sizeof(to), &conf->neigh_tree);
        LOGI("osw: confsync: %s/%s: neigh: %s -> %s", phy, vif, from, to);
        if (strlen(add) > 0) LOGI("osw: confsync: %s/%s: neigh: add: %s", phy, vif, add);
        if (strlen(mod) > 0) LOGI("osw: confsync: %s/%s: neigh: mod: %s", phy, vif, mod);
        if (strlen(del) > 0) LOGI("osw: confsync: %s/%s: neigh: del: %s", phy, vif, del);
        *notified = true;
    }

    if (cmd->wps_cred_list_changed) {
        char from[1024];
        char to[1024];
        osw_wps_cred_list_to_str(from, sizeof(from), &state->wps_cred_list);
        osw_conf_ap_wps_cred_list_to_str(to, sizeof(to), &conf->wps_cred_list);
        LOGI("osw: confsync: %s/%s: wps_cred_list: %s -> %s",
              phy,
              vif,
              from,
              to);
    }
}

static const char *
osw_confsync_sta_op_to_str(const enum osw_drv_vif_config_sta_operation op)
{
    switch (op) {
        case OSW_DRV_VIF_CONFIG_STA_NOP: return NULL;
        case OSW_DRV_VIF_CONFIG_STA_CONNECT: return "connect";
        case OSW_DRV_VIF_CONFIG_STA_RECONNECT: return "reconnect";
        case OSW_DRV_VIF_CONFIG_STA_DISCONNECT: return "disconnect";
    }
    return NULL;
}

static void
osw_confsync_net_to_str(char *buf, size_t len,
                        const struct osw_drv_vif_sta_network *net)
{
    memset(buf, 0, len);

    while (net != NULL) {
        /* FIXME: could dump more info, but it might be too obscure */
        csnprintf(&buf, &len, OSW_SSID_FMT, OSW_SSID_ARG(&net->ssid));
        if (osw_hwaddr_is_zero(&net->bssid) == false) {
            csnprintf(&buf, &len, " "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&net->bssid));
        }
        if (strlen(net->psk.str) > 0) {
            csnprintf(&buf, &len, " %zu", strlen(net->psk.str));
        }
        char wpa_str[64] = {0};
        osw_wpa_to_str(wpa_str, sizeof(wpa_str), &net->wpa);
        if (strlen(wpa_str) > 0) {
            csnprintf(&buf, &len, " %s", wpa_str);
        }
        const char *bridge = net->bridge_if_name.buf;
        const size_t bridge_max_len = sizeof(net->bridge_if_name.buf);
        const size_t bridge_len = strnlen(bridge, bridge_max_len);
        WARN_ON(bridge_len == bridge_max_len);
        if (bridge_len > 0 && bridge_len < bridge_max_len) {
            csnprintf(&buf, &len, " br:%s", bridge);
        }
        if (net->multi_ap) {
            csnprintf(&buf, &len, " map");
        }
        if (net->next != NULL) {
            csnprintf(&buf, &len, ",");
        }
        net = net->next;
    }
}

static void
osw_confsync_build_vif_sta_debug(const char *phy,
                                 const char *vif,
                                 const struct osw_drv_vif_config_sta *cmd,
                                 const struct osw_conf_vif_sta *conf,
                                 const struct osw_drv_vif_state_sta *state,
                                 bool *notified)
{
    const char *op_str = osw_confsync_sta_op_to_str(cmd->operation);

    if (cmd->network_changed) {
        char from[1024];
        char to[1024];
        osw_confsync_net_to_str(from, sizeof(from), state->network);
        osw_confsync_net_to_str(to, sizeof(to), cmd->network);
        LOGI("osw: confsync: %s/%s: net list: %s -> %s", phy, vif, from, to);
        *notified = true;
    }

    if (op_str != NULL) {
        LOGI("osw: confsync: %s/%s: op: %s", phy, vif, op_str);
        *notified = true;
    }
}

static void
osw_confsync_build_vif_debug(const struct osw_drv_vif_config *cmd,
                             const struct osw_conf_vif *conf,
                             const struct osw_drv_vif_state *state)
{
    const char *phy = conf->phy->phy_name;
    const char *vif = conf->vif_name;
    bool notified = false;

    if (cmd->enabled_changed) {
        /* actual print handled by the caller */
        notified = true;
    }

    if (cmd->vif_type_changed) {
        const char *from = osw_vif_type_to_str(state->vif_type);
        const char *to = osw_vif_type_to_str(conf->vif_type);
        LOGI("osw: confsync: %s/%s: vif_type: %s -> %s",
             phy, vif, from, to);
        notified = true;
    }

    if (cmd->tx_power_dbm_changed) {
        LOGI("osw: confsync: %s/%s: tx_power_dbm: %d -> %d",
             phy, vif, state->tx_power_dbm, conf->tx_power_dbm);
        notified = true;
    }

    if (cmd->vif_type_changed == false) {
        switch (cmd->vif_type) {
            case OSW_VIF_UNDEFINED:
                break;
            case OSW_VIF_AP:
                osw_confsync_build_vif_ap_debug(phy, vif,
                                                &cmd->u.ap,
                                                &conf->u.ap,
                                                &state->u.ap,
                                                &notified);
                break;
            case OSW_VIF_AP_VLAN:
                break;
            case OSW_VIF_STA:
                osw_confsync_build_vif_sta_debug(phy, vif,
                                                 &cmd->u.sta,
                                                 &conf->u.sta,
                                                 &state->u.sta,
                                                 &notified);
                break;
        }
    }

    if (cmd->changed && !notified) {
        LOGI("osw: confsync: %s/%s: changed", phy, vif);
    }
}

static bool
osw_confsync_vif_ap_ssid_changed(const struct osw_ssid *a,
                                 const struct osw_ssid *b)
{
    size_t max = ARRAY_SIZE(a->buf);
    size_t n = a->len;
    size_t m = b->len;
    const char *x = a->buf;
    const char *y = b->buf;
    size_t len = MIN(MIN(max, n), m);
    WARN_ON(n >= max);
    WARN_ON(m >= max);
    if (n != m || memcmp(x, y, len) != 0)
        return true;

    else
        return false;
}

static bool
osw_confsync_vif_ap_channel_changed(const struct osw_channel *a,
                                    const struct osw_channel *b)
{
    if (memcmp(a, b, sizeof(*a)) == 0)
        return false;
    else
        return true;
}

static bool
osw_confsync_vif_ap_psk_tree_changed(struct ds_tree *a,
                                     const struct osw_ap_psk_list *b,
                                     const struct osw_wpa *wpa)
{
    const size_t n = ds_tree_len(a);
    size_t i;

    if (n != b->count)
        return true;

    /* FIXME: This isn't great, but it's the most feasible
     * way now. Anything involving SAE can't really rely on
     * key_ids. SAE does support id_str which acts like a
     * login, but that'd need to be a string, not an int,
     * and can't really be used as a substitute for
     * multi-PSK due to incompatible cryptographic
     * properties. As such consider single passphrase with
     * SAE involved as a special case.
     */
    if (wpa->akm_sae) {
        WARN_ON(n > 1);
        if (n == 1) {
            const struct osw_ap_psk *p = &b->list[0];
            const struct osw_conf_psk *q = ds_tree_head(a);
            size_t l = sizeof(p->psk.str);
            const char *x = p->psk.str;
            const char *y = q->ap_psk.psk.str;
            if (strncmp(x, y, l) == 0) return false;
        }
    }

    for (i = 0; i < b->count; i++) {
        const struct osw_ap_psk *p = &b->list[i];
        const struct osw_conf_psk *q = ds_tree_find(a, &p->key_id);
        if (q == NULL) return true;

        const char *x = p->psk.str;
        const char *y = q->ap_psk.psk.str;
        size_t l = sizeof(p->psk.str);
        if (strncmp(x, y, l) != 0) return true;
    }

    return false;
}

static bool
osw_confsync_vif_ap_acl_tree_changed(struct ds_tree *a, const struct osw_hwaddr_list *b)
{
    const size_t n = ds_tree_len(a);
    size_t i;

    if (n != b->count)
        return true;

    for (i = 0; i < b->count; i++) {
        const struct osw_hwaddr *p = &b->list[i];
        const struct osw_conf_acl *q = ds_tree_find(a, p);
        if (q == NULL) return true;
    }

    return false;
}

static bool
osw_confsync_vif_ap_neigh_tree_changed(struct ds_tree *a, const struct osw_neigh_list *b)
{
    const size_t n = ds_tree_len(a);
    size_t i;

    if (n != b->count)
        return true;

    for (i = 0; i < b->count; i++) {
        const struct osw_neigh *p = &b->list[i];
        const struct osw_conf_neigh *q = ds_tree_find(a, p);
        if (q == NULL) return true;
        if (p->bssid_info != q->neigh.bssid_info) return true;
        if (p->op_class != q->neigh.op_class) return true;
        if (p->channel != q->neigh.channel) return true;
        if (p->phy_type != q->neigh.phy_type) return true;
    }

    return false;
}

static bool
osw_confsync_vif_ap_mode_changed(struct osw_ap_mode state,
                                 struct osw_ap_mode conf)
{
    const size_t size = sizeof(state.beacon_rate);

    if (conf.supported_rates == 0) {
        state.supported_rates = 0;
    }

    if (conf.basic_rates == 0) {
        state.basic_rates = 0;
    }

    if (conf.beacon_rate.type == OSW_BEACON_RATE_UNSPEC) {
        memset(&state.beacon_rate, 0, size);
    }

    /* The following condition handles cases where state
     * report is missing valid data - assume that given
     * operational parameters are not supported so reset
     * configuration assuming that it is synced. This
     * obviously will lead to degraded operation, but
     * _settled_ operation: no use setting the unsettable.
     */
    if (state.supported_rates == 0
     && state.basic_rates == 0
     && state.beacon_rate.type == OSW_BEACON_RATE_UNSPEC) {
        conf.supported_rates = 0;
        conf.basic_rates = 0;
        memset(&conf.beacon_rate, 0, size);
    }

    const bool changed = (memcmp(&conf, &state, sizeof(conf)) != 0);
    return changed;
}

static bool
osw_confsync_vif_ap_wps_cred_list_changed(struct ds_dlist *a, const struct osw_wps_cred_list *b)
{
    const size_t n = ds_dlist_len(a);
    size_t i;

    if (n != b->count)
        return true;

    for (i = 0; i < b->count; i++) {
        const struct osw_wps_cred *p = &b->list[i];
        struct osw_conf_wps_cred *q = NULL;

        ds_dlist_foreach(a, q)
            if (strcmp(p->psk.str, q->cred.psk.str) == 0)
                break;

        if (q == NULL)
            return true;
    }

    return false;
}

static void
osw_confsync_vif_ap_mark_changed(struct osw_drv_vif_config *dvif,
                                 const struct osw_drv_phy_state *sphy,
                                 const struct osw_drv_vif_state *svif,
                                 struct osw_conf_vif *cvif,
                                 const bool all)
{
    dvif->u.ap.ssid_changed = all || osw_confsync_vif_ap_ssid_changed(&cvif->u.ap.ssid, &svif->u.ap.ssid);
    dvif->u.ap.psk_list_changed = all || osw_confsync_vif_ap_psk_tree_changed(&cvif->u.ap.psk_tree, &svif->u.ap.psk_list, &dvif->u.ap.wpa);
    dvif->u.ap.neigh_list_changed = all || osw_confsync_vif_ap_neigh_tree_changed(&cvif->u.ap.neigh_tree, &svif->u.ap.neigh_list);
    dvif->u.ap.wps_cred_list_changed = all || osw_confsync_vif_ap_wps_cred_list_changed(&cvif->u.ap.wps_cred_list, &svif->u.ap.wps_cred_list);
    dvif->u.ap.acl_changed = all || osw_confsync_vif_ap_acl_tree_changed(&cvif->u.ap.acl_tree, &svif->u.ap.acl);
    dvif->u.ap.channel_changed = all || osw_confsync_vif_ap_channel_changed(&cvif->u.ap.channel, &svif->u.ap.channel);
    dvif->u.ap.beacon_interval_tu_changed = all || (svif->u.ap.beacon_interval_tu != cvif->u.ap.beacon_interval_tu);
    dvif->u.ap.isolated_changed = all || (svif->u.ap.isolated != cvif->u.ap.isolated);
    dvif->u.ap.ssid_hidden_changed = all || (svif->u.ap.ssid_hidden != cvif->u.ap.ssid_hidden);
    dvif->u.ap.mcast2ucast_changed = all || (svif->u.ap.mcast2ucast != cvif->u.ap.mcast2ucast);
    dvif->u.ap.acl_policy_changed = all || (svif->u.ap.acl_policy != cvif->u.ap.acl_policy);
    dvif->u.ap.wpa_changed = all || (memcmp(&svif->u.ap.wpa, &cvif->u.ap.wpa, sizeof(svif->u.ap.wpa)) != 0);
    dvif->u.ap.mode_changed = all || (osw_confsync_vif_ap_mode_changed(svif->u.ap.mode, cvif->u.ap.mode));
    dvif->u.ap.bridge_if_name_changed = all || (strcmp(svif->u.ap.bridge_if_name.buf, cvif->u.ap.bridge_if_name.buf) != 0);
    dvif->u.ap.wps_pbc_changed = all || (svif->u.ap.wps_pbc != cvif->u.ap.wps_pbc);
    dvif->u.ap.multi_ap_changed = all || (memcmp(&svif->u.ap.multi_ap, &cvif->u.ap.multi_ap, sizeof(svif->u.ap.multi_ap)) != 0);

    dvif->changed |= dvif->u.ap.beacon_interval_tu_changed;
    dvif->changed |= dvif->u.ap.isolated_changed;
    dvif->changed |= dvif->u.ap.ssid_hidden_changed;
    dvif->changed |= dvif->u.ap.mcast2ucast_changed;
    dvif->changed |= dvif->u.ap.acl_policy_changed;
    dvif->changed |= dvif->u.ap.wpa_changed;
    dvif->changed |= dvif->u.ap.mode_changed;
    dvif->changed |= dvif->u.ap.bridge_if_name_changed;
    dvif->changed |= dvif->u.ap.wps_pbc_changed;
    dvif->changed |= dvif->u.ap.multi_ap_changed;
    dvif->changed |= dvif->u.ap.psk_list_changed;
    dvif->changed |= dvif->u.ap.neigh_list_changed;
    dvif->changed |= dvif->u.ap.wps_cred_list_changed;
    dvif->changed |= dvif->u.ap.acl_changed;
    dvif->changed |= dvif->u.ap.ssid_changed;
    dvif->changed |= dvif->u.ap.channel_changed;

    if (all == false && dvif->enabled && dvif->u.ap.channel.control_freq_mhz != 0 && svif->status == OSW_VIF_ENABLED) {
        const struct osw_channel_state *cs = sphy->channel_states;
        const size_t n_cs = sphy->n_channel_states;
        const struct osw_channel *c = &svif->u.ap.channel;
        const enum osw_channel_state_dfs state1 = OSW_CHANNEL_DFS_CAC_IN_PROGRESS;
        const enum osw_channel_state_dfs state2 = OSW_CHANNEL_DFS_CAC_POSSIBLE;

        /* FIXME: This could be a little nicer if the
         * primary channel is _not_ in CAC and/or zero-wait
         * DFS is supported.
         *
         */
        const bool cac_running = osw_cs_chan_intersects_state(cs, n_cs, c, state1);

        /* Some drivers may end up in a buggy state, so if any of the current
         * channel segments is in need of CAC, but it isn't running it, then
         * it's best to prevent CSA as well.
         */
        const bool cac_bugged = osw_cs_chan_intersects_state(cs, n_cs, c, state2);

        const bool csa_eligible = (dvif->u.ap.channel_changed == true)
                               && (dvif->u.ap.mode_changed == false);

        if (csa_eligible && (cac_running || cac_bugged)) {
            /* This shouldn't be a common occurance, so keep it
             * at INFO so it's visible when it happens.
             */
            const char *vif_name = cvif->vif_name;
            LOGI("osw: confsync: %s: csa impossible due to cac (running=%d bugged=%d)",
                 vif_name,
                 cac_running,
                 cac_bugged);
        }

        dvif->u.ap.csa_required = csa_eligible && !(cac_running || cac_bugged);
    }
}

enum osw_confsync_vif_ap_neigh_action {
    OSW_CONFSYNC_VIF_AP_NEIGH_ADD,
    OSW_CONFSYNC_VIF_AP_NEIGH_DEL,
    OSW_CONFSYNC_VIF_AP_NEIGH_MOD,
    OSW_CONFSYNC_VIF_AP_NEIGH_NOP,
};

static struct osw_neigh *
osw_confsync_vif_ap_neigh_find(struct osw_neigh_list *list,
                               const struct osw_neigh *n)
{
    size_t i;
    for (i = 0; i < list->count; i++) {
        struct osw_neigh *ni = &list->list[i];
        const bool match = (osw_hwaddr_cmp(&n->bssid, &ni->bssid) == 0);
        if (match) return ni;
    }
    return NULL;
}

static struct osw_neigh *
osw_confsync_vif_ap_neigh_alloc(struct osw_neigh_list *list)
{
    const size_t new_count = list->count + 1;
    const size_t new_size = new_count * sizeof(list->list[0]);
    list->list = REALLOC(list->list, new_size);
    list->count = new_count;
    return &list->list[list->count - 1];
}

static void
osw_confsync_vif_ap_neigh_append(struct osw_neigh_list *list,
                                 const struct osw_neigh *n)
{
    /* This may look suboptimal: O(n) and O(n^2) considering
     * how it's used by filter_del() and filter_add().
     * This will however only really be called when config
     * != state which is intended to be a short-lived
     * runtime condition. Optimizing this will likely lead
     * to unnecessary complexity.
     */
    struct osw_neigh *ni = osw_confsync_vif_ap_neigh_find(list, n)
                        ?: osw_confsync_vif_ap_neigh_alloc(list);
    *ni = *n;
}

static void
osw_confsync_vif_ap_neigh_filter_del(struct osw_neigh_list *output,
                                     struct ds_tree *conf,
                                     const struct osw_neigh_list *state,
                                     const enum osw_confsync_vif_ap_neigh_action match_action)
{
    size_t i;
    for (i = 0; i < state->count; i++) {
        const struct osw_neigh *sn = &state->list[i];
        const struct osw_conf_neigh *ccn = ds_tree_find(conf, &sn->bssid);
        const struct osw_neigh *cn = ccn ? &ccn->neigh : NULL;

        const bool missing_in_conf = (cn == NULL);
        const bool modified = (cn != NULL && (memcmp(sn, cn, sizeof(*sn)) != 0));
        enum osw_confsync_vif_ap_neigh_action action;
        const struct osw_neigh *n = sn;

        if (missing_in_conf) {
            action = OSW_CONFSYNC_VIF_AP_NEIGH_DEL;
        }
        else if (modified) {
            action = OSW_CONFSYNC_VIF_AP_NEIGH_MOD;
            n = cn;
        }
        else {
            action = OSW_CONFSYNC_VIF_AP_NEIGH_NOP;
        }

        if (action == match_action) {
            osw_confsync_vif_ap_neigh_append(output, n);
        }
    }
}

static void
osw_confsync_vif_ap_neigh_filter_add(struct osw_neigh_list *output,
                                     struct ds_tree *conf,
                                     const struct osw_neigh_list *state,
                                     const enum osw_confsync_vif_ap_neigh_action match_action)
{
    struct osw_conf_neigh *ccn;
    ds_tree_foreach(conf, ccn) {
        const struct osw_neigh *cn = &ccn->neigh;
        const struct osw_neigh *sn = NULL;
        size_t i;

        for (i = 0; i < state->count; i++) {
            sn = &state->list[i];
            const bool match = (osw_hwaddr_cmp(&sn->bssid, &cn->bssid) == 0);
            if (match) break;
            sn = NULL;
        }

        const bool missing_in_state = (sn == NULL);
        const bool modified = (sn != NULL && (memcmp(sn, cn, sizeof(*sn)) != 0));
        enum osw_confsync_vif_ap_neigh_action action;

        if (missing_in_state) {
            action = OSW_CONFSYNC_VIF_AP_NEIGH_ADD;
        }
        else if (modified) {
            action = OSW_CONFSYNC_VIF_AP_NEIGH_MOD;
        }
        else {
            action = OSW_CONFSYNC_VIF_AP_NEIGH_NOP;
        }

        if (action == match_action) {
            osw_confsync_vif_ap_neigh_append(output, cn);
        }
    }
}

static void
osw_confsync_vif_ap_neigh_filter(struct osw_neigh_list *output,
                                 struct ds_tree *conf,
                                 const struct osw_neigh_list *state,
                                 const enum osw_confsync_vif_ap_neigh_action match_action)
{
    osw_confsync_vif_ap_neigh_filter_del(output, conf, state, match_action);
    osw_confsync_vif_ap_neigh_filter_add(output, conf, state, match_action);
}

static void
osw_confsync_build_drv_conf_vif_ap_acl_add(struct osw_drv_vif_config *dvif,
                                           const struct osw_drv_vif_state *svif,
                                           struct osw_conf_vif *cvif)
{
    struct osw_conf_acl *acl;
    ds_tree_foreach(&cvif->u.ap.acl_tree, acl) {
        const struct osw_hwaddr *mac = &acl->mac_addr;
        const bool found = osw_hwaddr_list_contains(svif->u.ap.acl.list,
                                                    svif->u.ap.acl.count,
                                                    mac);
        const bool in_config_but_not_in_state = (found == false);
        if (in_config_but_not_in_state) {
            osw_hwaddr_list_append(&dvif->u.ap.acl_add, mac);
        }
    }
}

static void
osw_confsync_build_drv_conf_vif_ap_acl_del(struct osw_drv_vif_config *dvif,
                                           const struct osw_drv_vif_state *svif,
                                           struct osw_conf_vif *cvif)
{
    size_t i;
    for (i = 0; i < svif->u.ap.acl.count; i++) {
        const struct osw_hwaddr *mac = &svif->u.ap.acl.list[i];
        struct osw_conf_acl *acl = ds_tree_find(&cvif->u.ap.acl_tree, mac);
        const bool in_state_but_not_in_config = (acl == NULL);
        if (in_state_but_not_in_config) {
            osw_hwaddr_list_append(&dvif->u.ap.acl_del, mac);
        }
    }
}

static void
osw_confsync_build_drv_conf_vif_ap(struct osw_drv_vif_config *dvif,
                                   const struct osw_drv_phy_state *sphy,
                                   const struct osw_drv_vif_state *svif,
                                   struct osw_conf_vif *cvif,
                                   const bool allow_changed)
{
    dvif->u.ap.bridge_if_name = cvif->u.ap.bridge_if_name;
    dvif->u.ap.beacon_interval_tu = cvif->u.ap.beacon_interval_tu;
    dvif->u.ap.channel = cvif->u.ap.channel;
    dvif->u.ap.isolated = cvif->u.ap.isolated;
    dvif->u.ap.ssid_hidden = cvif->u.ap.ssid_hidden;
    dvif->u.ap.mcast2ucast = cvif->u.ap.mcast2ucast;
    dvif->u.ap.mode = cvif->u.ap.mode;
    dvif->u.ap.acl_policy = cvif->u.ap.acl_policy;
    dvif->u.ap.ssid = cvif->u.ap.ssid;
    dvif->u.ap.wpa = cvif->u.ap.wpa;
    dvif->u.ap.wps_pbc = cvif->u.ap.wps_pbc;
    dvif->u.ap.multi_ap = cvif->u.ap.multi_ap;

    if (dvif->enabled && dvif->u.ap.channel.control_freq_mhz != 0) {
        ASSERT(dvif->u.ap.channel.center_freq0_mhz != 0, "center freq required");
    }

    {
        struct osw_ap_psk *psks;
        struct osw_conf_psk *psk;
        int n = 0;
        int i = 0;

        ds_tree_foreach(&cvif->u.ap.psk_tree, psk)
            n++;

        psks = CALLOC(n, sizeof(*psks));
        ds_tree_foreach(&cvif->u.ap.psk_tree, psk) {
            psks[i].key_id = psk->ap_psk.key_id;
            STRSCPY_WARN(psks[i].psk.str, psk->ap_psk.psk.str);
            i++;
        }

        dvif->u.ap.psk_list.list = psks;
        dvif->u.ap.psk_list.count = n;
    }
    {
        struct osw_hwaddr *addrs;
        struct osw_conf_acl *acl;
        int n = 0;
        int i = 0;

        ds_tree_foreach(&cvif->u.ap.acl_tree, acl)
            n++;

        addrs = CALLOC(n, sizeof(*addrs));
        ds_tree_foreach(&cvif->u.ap.acl_tree, acl) {
            addrs[i] = acl->mac_addr;
            i++;
        }

        dvif->u.ap.acl.list = addrs;
        dvif->u.ap.acl.count = n;
    }
    {
        struct osw_neigh *neighs;
        struct osw_conf_neigh *neigh;
        int n = 0;
        int i = 0;

        ds_tree_foreach(&cvif->u.ap.neigh_tree, neigh)
            n++;

        neighs = CALLOC(n, sizeof(*neighs));
        ds_tree_foreach(&cvif->u.ap.neigh_tree, neigh) {
            const struct osw_neigh *cn = &neigh->neigh;
            struct osw_neigh *dn = &neighs[i];
            *dn = *cn;
            i++;
        }

        dvif->u.ap.neigh_list.list = neighs;
        dvif->u.ap.neigh_list.count = n;
    }
    {
        struct osw_wps_cred *wps_creds;
        struct osw_conf_wps_cred *wps_cred;
        int n = 0;
        int i = 0;

        ds_dlist_foreach(&cvif->u.ap.wps_cred_list, wps_cred)
            n++;

        wps_creds = CALLOC(n, sizeof(*wps_creds));
        ds_dlist_foreach(&cvif->u.ap.wps_cred_list, wps_cred) {
            const struct osw_wps_cred *cn = &wps_cred->cred;
            struct osw_wps_cred *dn = &wps_creds[i];
            *dn = *cn;
            i++;
        }

        dvif->u.ap.wps_cred_list.list = wps_creds;
        dvif->u.ap.wps_cred_list.count = n;
    }
    // FIXME: radius

    osw_confsync_build_drv_conf_vif_ap_acl_add(dvif, svif, cvif);
    osw_confsync_build_drv_conf_vif_ap_acl_del(dvif, svif, cvif);

    if (allow_changed) {
        const bool all_changed = (allow_changed && dvif->vif_type_changed == true);
        osw_confsync_vif_ap_mark_changed(dvif, sphy, svif, cvif, all_changed);
    }

    if (dvif->u.ap.neigh_list_changed) {
        osw_confsync_vif_ap_neigh_filter(&dvif->u.ap.neigh_add_list,
                                         &cvif->u.ap.neigh_tree,
                                         &svif->u.ap.neigh_list,
                                         OSW_CONFSYNC_VIF_AP_NEIGH_ADD);

        osw_confsync_vif_ap_neigh_filter(&dvif->u.ap.neigh_mod_list,
                                         &cvif->u.ap.neigh_tree,
                                         &svif->u.ap.neigh_list,
                                         OSW_CONFSYNC_VIF_AP_NEIGH_MOD);

        osw_confsync_vif_ap_neigh_filter(&dvif->u.ap.neigh_del_list,
                                         &cvif->u.ap.neigh_tree,
                                         &svif->u.ap.neigh_list,
                                         OSW_CONFSYNC_VIF_AP_NEIGH_DEL);
    }
}

static enum osw_drv_vif_config_sta_operation
osw_confsync_build_drv_conf_vif_sta_op(struct osw_drv_vif_config *dvif,
                                       const struct osw_drv_vif_state *svif,
                                       bool network_changed)
{
    const struct osw_drv_vif_state_sta *ssta = &svif->u.sta;
    struct osw_drv_vif_config_sta *dsta = &dvif->u.sta;
    const enum osw_drv_vif_state_sta_link_status desired = (dsta->network == NULL)
                                                         ? OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED
                                                         : OSW_DRV_VIF_STATE_STA_LINK_CONNECTED;

    switch (desired) {
        case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN:
            break;
        case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED:
            switch (ssta->link.status) {
                case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN:
                    break;
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED:
                    /* subsequent check will verify if the network matches */
                    break;
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING:
                    if (network_changed) {
                        return OSW_DRV_VIF_CONFIG_STA_CONNECT;
                    }
                    else {
                        return OSW_DRV_VIF_CONFIG_STA_NOP;
                    }
                case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED:
                    return OSW_DRV_VIF_CONFIG_STA_CONNECT;
            }
            break;
        case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING:
            break;
        case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED:
            switch (ssta->link.status) {
                case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN:
                    break;
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED:
                    return OSW_DRV_VIF_CONFIG_STA_DISCONNECT;
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING:
                    return OSW_DRV_VIF_CONFIG_STA_DISCONNECT;
                case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED:
                    return OSW_DRV_VIF_CONFIG_STA_NOP;
            }
            break;
    }

    struct osw_drv_vif_sta_network *dnet;
    for (dnet = dsta->network; dnet != NULL; dnet = dnet->next) {
        const size_t bssid_len = sizeof(dnet->bssid);
        const size_t bridge_len = sizeof(dnet->bridge_if_name.buf);
        const size_t ssid_max = sizeof(dnet->ssid.buf);
        const bool multi_ap_match = (dnet->multi_ap == ssta->link.multi_ap);
        const bool bridge_match = (strncmp(dnet->bridge_if_name.buf,
                                           ssta->link.bridge_if_name.buf,
                                           bridge_len) == 0);
        const bool bssid_valid = osw_hwaddr_is_zero(&dnet->bssid) == false;
        const bool bssid_match = memcmp(&dnet->bssid, &ssta->link.bssid, bssid_len) == 0;
        const bool ssid_match = strncmp(dnet->ssid.buf, ssta->link.ssid.buf, ssid_max) == 0;
        const bool ccmp = dnet->wpa.pairwise_ccmp && ssta->link.wpa.pairwise_ccmp;
        const bool tkip = dnet->wpa.pairwise_tkip && ssta->link.wpa.pairwise_tkip;
        const bool wpa = dnet->wpa.wpa && ssta->link.wpa.wpa;
        const bool rsn = dnet->wpa.rsn && ssta->link.wpa.rsn;
        const bool psk = dnet->wpa.akm_psk && ssta->link.wpa.akm_psk;
        const bool sae = dnet->wpa.akm_sae && ssta->link.wpa.akm_sae;
        /* FIXME: FT? */
        const bool crypto_match = (ccmp || tkip)
                               && (wpa || rsn)
                               && (psk || sae);
        const bool net_match = (bssid_valid == true && bssid_match == true)
                            || (bssid_valid == false && ssid_match == true);
        const bool match = net_match
                        && crypto_match
                        && multi_ap_match
                        && bridge_match;

        if (match) {
            return OSW_DRV_VIF_CONFIG_STA_NOP;
        }

        /* FIXME: Arguably this could be more involving - it
         * could verify if WPA is matching as well. Right
         * now changing WPA settings, eg. for
         * WPA3-Transition mode parent won't yield WPA2 ->
         * WPA3 reonnect if orchestrated. That's not a valid
         * usecase today so this can be simplified for now.
         */
    }

    return OSW_DRV_VIF_CONFIG_STA_RECONNECT;
}

static bool
osw_confsync_net_is_identical(const struct osw_drv_vif_sta_network *a,
                              const struct osw_drv_vif_sta_network *b)
{
    const size_t bridge_max_len = sizeof(a->bridge_if_name.buf);
    const bool same_bssid = memcmp(&a->bssid, &b->bssid, sizeof(a->bssid)) == 0;
    const bool same_ssid = (a->ssid.len == b->ssid.len) &&
                           memcmp(a->ssid.buf, b->ssid.buf, a->ssid.len) == 0;
    const bool same_multi_ap = (a->multi_ap == b->multi_ap);
    const bool same_bridge = (strncmp(a->bridge_if_name.buf,
                                      b->bridge_if_name.buf,
                                      bridge_max_len) == 0);
    const bool same_psk = strcmp(a->psk.str, b->psk.str) == 0;
    struct osw_wpa wpa1 = a->wpa;
    struct osw_wpa wpa2 = b->wpa;
    wpa1.group_rekey_seconds = 0;
    wpa2.group_rekey_seconds = 0;
    const bool same_wpa = memcmp(&wpa1, &wpa2, sizeof(wpa1)) == 0;

    return same_bssid && same_ssid && same_psk && same_wpa && same_multi_ap && same_bridge;
}

static bool
osw_confsync_vif_sta_state_contains_net(const struct osw_drv_vif_state_sta *ssta,
                                        const struct osw_drv_vif_sta_network *net)
{
    struct osw_drv_vif_sta_network *i;
    for (i = ssta->network; i != NULL; i = i->next) {
        if (osw_confsync_net_is_identical(i, net))
            return true;
    }
    return false;
}

static bool
osw_confsync_vif_sta_config_contains_net(const struct osw_drv_vif_config_sta *dsta,
                                         const struct osw_drv_vif_sta_network *net)
{
    struct osw_drv_vif_sta_network *i;
    for (i = dsta->network; i != NULL; i = i->next) {
        if (osw_confsync_net_is_identical(i, net))
            return true;
    }
    return false;
}

static bool
osw_confsync_vif_sta_net_list_changed(const struct osw_drv_vif_state_sta *ssta,
                                      const struct osw_drv_vif_config_sta *dsta)
{
    struct osw_drv_vif_sta_network *i;
    for (i = dsta->network; i != NULL; i = i->next) {
        if (osw_confsync_vif_sta_state_contains_net(ssta, i) == false)
            return true;
    }
    for (i = ssta->network; i != NULL; i = i->next) {
        if (osw_confsync_vif_sta_config_contains_net(dsta, i) == false)
            return true;
    }
    return false;
}

static struct osw_drv_vif_sta_network *
osw_confsync_build_drv_conf_vif_sta_net_list(struct osw_conf_vif_sta *csta)
{
    struct osw_drv_vif_sta_network *first = NULL;
    struct osw_conf_net *cnet;

    ds_dlist_foreach(&csta->net_list, cnet) {
        struct osw_drv_vif_sta_network *dnet = CALLOC(1, sizeof(*dnet));
        dnet->next = first;
        dnet->multi_ap = cnet->multi_ap;
        memcpy(&dnet->bridge_if_name, &cnet->bridge_if_name, sizeof(cnet->bridge_if_name));
        memcpy(&dnet->ssid, &cnet->ssid, sizeof(cnet->ssid));
        memcpy(&dnet->bssid, &cnet->bssid, sizeof(cnet->bssid));
        memcpy(&dnet->wpa, &cnet->wpa, sizeof(cnet->wpa));
        memcpy(&dnet->psk, &cnet->psk, sizeof(cnet->psk));
        first = dnet;
    }

    return first;
}

static void
osw_confsync_build_drv_conf_vif_sta(struct osw_drv_vif_config *dvif,
                                    const struct osw_drv_vif_state *svif,
                                    struct osw_conf_vif *cvif,
                                    const bool allow_changed)
{
    const struct osw_drv_vif_state_sta *ssta = &svif->u.sta;
    struct osw_drv_vif_config_sta *dsta = &dvif->u.sta;
    struct osw_conf_vif_sta *csta = &cvif->u.sta;

    dsta->network = osw_confsync_build_drv_conf_vif_sta_net_list(csta);
    dsta->network_changed = osw_confsync_vif_sta_net_list_changed(ssta, dsta);
    dsta->network_changed &= allow_changed;
    dsta->operation = osw_confsync_build_drv_conf_vif_sta_op(dvif, svif, dsta->network_changed);

    if (dsta->operation == OSW_DRV_VIF_CONFIG_STA_NOP) {
        /* Don't bother reconfiguring interface if it's
         * connected to something that is intended to be a
         * target. This relaxes driver implementations so
         * they don't really need to report network blocks
         * back in state for onboarding purposes in most
         * cases.
         */
        if (getenv("OSW_CONFSYNC_STRICT_NETWORK_CHANGES") == NULL) {
            dsta->network_changed = false;
        }
    }

    dvif->changed |= dsta->network_changed;
    dvif->changed |= (dsta->operation != OSW_DRV_VIF_CONFIG_STA_NOP);
    dvif->changed &= allow_changed;
}

static bool
osw_confsync_vif_enabled_changed(enum osw_vif_status status,
                                 bool enabled)
{
    switch (status) {
        case OSW_VIF_UNKNOWN:
            return false;
        case OSW_VIF_ENABLED:
            return (enabled == false);
        case OSW_VIF_DISABLED:
            return (enabled == true);
        case OSW_VIF_BROKEN:
            return true;
    }
    return false;
}

static void
osw_confsync_build_drv_conf_vif(struct osw_confsync_arg *arg,
                                const struct osw_state_vif_info *vif)
{
    struct osw_conf_phy *cphy = arg->cphy;
    struct osw_conf_vif *cvif = arg->cvif;
    const struct osw_drv_phy_state *sphy = arg->sphy;
    const struct osw_drv_vif_state *svif = vif->drv_state;
    struct osw_drv_phy_config *dphy = arg->dphy;

    struct osw_drv_vif_config *dvif;
    dphy->vif_list.count++;
    dphy->vif_list.list = REALLOC(dphy->vif_list.list, dphy->vif_list.count * sizeof(*dvif));
    dvif = &dphy->vif_list.list[dphy->vif_list.count - 1];
    memset(dvif, 0, sizeof(*dvif));

    const bool enabled = (cvif->enabled && cphy->enabled);

    dvif->vif_name = STRDUP(cvif->vif_name);
    dvif->enabled = enabled;
    dvif->vif_type = cvif->vif_type;
    dvif->tx_power_dbm = cvif->tx_power_dbm;

    const bool enabling = (svif->status != OSW_VIF_ENABLED &&
                           dvif->enabled == true);
    const bool deferred = enabling
                        ? (osw_confsync_defer_vif_enable_start(arg->confsync, dvif->vif_name) == OSW_CONFSYNC_DEFER_RUNNING)
                        : false;
    const bool config_is_disabled = (dvif->enabled == false);
    const bool state_is_disabled = (svif->status == OSW_VIF_DISABLED ||
                                    svif->status == OSW_VIF_UNKNOWN);
    const bool skip = (config_is_disabled && state_is_disabled)
                   || deferred;

    dvif->changed = false;
    if (skip == false) {
        const bool enabled_changed = osw_confsync_vif_enabled_changed(svif->status, dvif->enabled);
        dvif->changed |= (dvif->enabled_changed = enabled_changed);
        dvif->changed |= (dvif->vif_type_changed = (cvif->vif_type != svif->vif_type));
        dphy->changed |= (dvif->tx_power_dbm_changed = cvif->tx_power_dbm != svif->tx_power_dbm);
    }

    switch (cvif->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            osw_confsync_build_drv_conf_vif_ap(dvif, sphy, svif, cvif, !skip);
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            osw_confsync_build_drv_conf_vif_sta(dvif, svif, cvif, !skip);
            break;
    }

    if (dvif->enabled_changed) {
        LOGI("osw: confsync: %s/%s: enabled: %s -> %s",
             cvif->phy->phy_name,
             cvif->vif_name,
             osw_vif_status_into_cstr(svif->status),
             cvif->enabled ? "enabled" : "disabled");
    }

    if (skip) {
        dvif->changed = false;
        return;
    }

    if (arg->debug) osw_confsync_build_vif_debug(dvif, cvif, svif);
}

static void
osw_confsync_build_drv_conf_phy(struct osw_confsync_arg *arg,
                                const struct osw_state_phy_info *phy)
{
    struct osw_drv_conf *drv_conf = arg->drv_conf;
    const struct osw_drv_phy_state *sphy = phy->drv_state;
    struct osw_conf_phy *cphy = arg->cphy;
    const bool tx_chain_supported = (phy->drv_capab->tx_chain == OSW_DRV_PHY_TX_CHAIN_SUPPORTED);

    struct osw_drv_phy_config *dphy;
    drv_conf->n_phy_list++;
    drv_conf->phy_list = REALLOC(drv_conf->phy_list, drv_conf->n_phy_list * sizeof(*dphy));
    dphy = &drv_conf->phy_list[drv_conf->n_phy_list - 1];
    memset(dphy, 0, sizeof(*dphy));

    dphy->phy_name = STRDUP(cphy->phy_name);
    dphy->enabled = cphy->enabled;
    dphy->tx_chainmask = tx_chain_supported || true
                       ? cphy->tx_chainmask
                       : sphy->tx_chainmask;
    dphy->radar = cphy->radar;
    dphy->reg_domain = cphy->reg_domain;

    const bool mbss_tx_vif_name_reported_non_empty = osw_ifname_is_valid(&sphy->mbss_tx_vif_name);
    const bool mbss_is_enabled = mbss_tx_vif_name_reported_non_empty;
    if (mbss_is_enabled) {
        dphy->mbss_tx_vif_name = cphy->mbss_tx_vif_name;
    }

    const bool skip = (dphy->enabled == false && sphy->enabled == false);

    dphy->changed = false;
    if (skip == false) {
        dphy->changed |= (dphy->enabled_changed = cphy->enabled != sphy->enabled);
        dphy->changed |= (dphy->tx_chainmask_changed = cphy->tx_chainmask != sphy->tx_chainmask);
        dphy->changed |= (dphy->radar_changed = cphy->radar != sphy->radar);
        dphy->changed |= (dphy->reg_domain_changed = (memcmp(&cphy->reg_domain,
                                                             &sphy->reg_domain,
                                                             sizeof(cphy->reg_domain)) != 0));
        if (mbss_is_enabled) {
            dphy->changed |= (dphy->mbss_tx_vif_name_changed = (osw_ifname_is_equal(&cphy->mbss_tx_vif_name,
                                                                                    &sphy->mbss_tx_vif_name) == false));
        }
    }

    if (arg->debug) osw_confsync_build_phy_debug(dphy, cphy, sphy);
    arg->dphy = dphy;
}

static void
osw_confsync_build_drv_conf_vif_cb(const struct osw_state_vif_info *vif,
                                   void *priv)
{
    struct osw_confsync_arg *arg = priv;
    struct osw_conf_phy *cphy = arg->cphy;

    arg->cvif = ds_tree_find(&cphy->vif_tree, vif->vif_name);
    assert(arg->cvif != NULL);

    osw_confsync_build_drv_conf_vif(arg, vif);
}

static void
osw_confsync_build_drv_conf_phy_cb(const struct osw_state_phy_info *phy,
                                   void *priv)
{
    struct osw_confsync_arg *arg = priv;
    struct ds_tree *phy_tree = arg->phy_tree;

    arg->cphy = ds_tree_find(phy_tree, phy->phy_name);
    arg->sphy = phy->drv_state;
    assert(arg->cphy != NULL);

    osw_confsync_build_drv_conf_phy(arg, phy);
    osw_state_vif_get_list(osw_confsync_build_drv_conf_vif_cb, phy->phy_name, arg);
}

static struct osw_drv_conf *
osw_confsync_build_drv_conf(struct osw_confsync *cs, const bool debug)
{
    struct osw_confsync_arg arg = {
        .confsync = cs,
        .drv_conf = CALLOC(1, sizeof(*arg.drv_conf)),
        .phy_tree = cs->build_conf(),
        .debug = debug,
    };
    osw_state_phy_get_list(osw_confsync_build_drv_conf_phy_cb, &arg);
    osw_conf_free(arg.phy_tree);
    return arg.drv_conf;
}

static void
osw_confsync_notify_changed(struct osw_confsync *cs)
{
    struct osw_confsync_changed *i;
    ds_dlist_foreach(&cs->changed_fns, i)
        i->fn(cs, i->fn_priv);
}

static void
osw_confsync_set_state(struct osw_confsync *cs, enum osw_confsync_state s)
{
    LOGT("%s: state %d -> %d", __func__, cs->state, s);
    if (cs->state == s) return;
    cs->state = s;
    switch (cs->state) {
        case OSW_CONFSYNC_IDLE:
            osw_confsync_defer_flush(cs);
            if (cs->settled == false) LOGN("osw: confsync: settled");
            cs->settled = true;
            ev_timer_stop(EV_DEFAULT_ &cs->retry);
            ev_idle_stop(EV_DEFAULT_ &cs->work);
            break;
        case OSW_CONFSYNC_REQUESTING:
            osw_confsync_defer_flush_expired(cs);
            ev_timer_stop(EV_DEFAULT_ &cs->retry);
            ev_idle_start(EV_DEFAULT_ &cs->work);
            break;
        case OSW_CONFSYNC_WAITING:
            if (cs->settled == true) LOGN("osw: confsync: unsettled");
            cs->settled = false;
            ev_idle_stop(EV_DEFAULT_ &cs->work);
            ev_timer_start(EV_DEFAULT_ &cs->retry);
            break;
        case OSW_CONFSYNC_VERIFYING:
            ev_idle_start(EV_DEFAULT_ &cs->work);
            break;
    }
    osw_confsync_notify_changed(cs);
}

static bool
osw_confsync_conf_is_synced(struct osw_confsync *cs)
{
    if (osw_confsync_defer_is_pending(cs)) {
        return false;
    }
    const bool debug = false;
    struct osw_drv_conf *conf = osw_confsync_build_drv_conf(cs, debug);
    bool changed = false;
    size_t i;
    for (i = 0; i < conf->n_phy_list && changed == false; i++) {
        const struct osw_drv_phy_config *pc = &conf->phy_list[i];
        size_t j;
        if (pc->changed) changed = true;
        for (j = 0; j < pc->vif_list.count && changed == false; j++) {
            const struct osw_drv_vif_config *vc = &pc->vif_list.list[j];
            if (vc->changed) changed = true;
        }
    }
    osw_drv_conf_free(conf);
    return !changed;
}

static void
osw_confsync_work(struct osw_confsync *cs)
{
    LOGD("osw: confsync: work");

    switch (cs->state) {
        case OSW_CONFSYNC_IDLE:
            break;
        case OSW_CONFSYNC_REQUESTING:
            {
                const bool debug = true;
                struct osw_drv_conf *conf = osw_confsync_build_drv_conf(cs, debug);
                const bool requested = osw_mux_request_config(conf);
                const enum osw_confsync_state s = (requested == true)
                                                ? OSW_CONFSYNC_WAITING
                                                : (osw_confsync_defer_is_pending(cs)
                                                   ? OSW_CONFSYNC_WAITING
                                                   : OSW_CONFSYNC_IDLE);
                osw_confsync_set_state(cs, s);
            }
            break;
        case OSW_CONFSYNC_WAITING:
            break;
        case OSW_CONFSYNC_VERIFYING:
            {
                const bool done = osw_confsync_conf_is_synced(cs);
                const enum osw_confsync_state s = (done == true)
                                              ? OSW_CONFSYNC_IDLE
                                              : OSW_CONFSYNC_WAITING;
                osw_confsync_set_state(cs, s);
            }
            break;
    }
}

static void
osw_confsync_work_cb(EV_P_  ev_idle *arg, int events)
{
    LOGT("%s", __func__);
    struct osw_confsync *cs = container_of(arg, struct osw_confsync, work);
    osw_confsync_work(cs);
}

static void
osw_confsync_retry_cb(EV_P_  ev_timer *arg, int events)
{
    LOGT("%s", __func__);
    struct osw_confsync *cs = container_of(arg, struct osw_confsync, retry);
    osw_confsync_set_state(cs, OSW_CONFSYNC_REQUESTING);
}

static void
osw_confsync_state_changed(struct osw_confsync *cs)
{
    /* When a configuration command is submitted the system
     * may go through transient state changes before
     * reaching a final state. To avoid needlessly
     * re-issuing configuration commands, and possibly
     * making configuration inadvertantly longer, move to
     * CHECKING only when IDLE.
     */
    switch (cs->state) {
        case OSW_CONFSYNC_IDLE:
            osw_confsync_set_state(cs, OSW_CONFSYNC_REQUESTING);
            return;
        case OSW_CONFSYNC_REQUESTING:
            return;
        case OSW_CONFSYNC_WAITING:
            osw_confsync_set_state(cs, OSW_CONFSYNC_VERIFYING);
            return;
        case OSW_CONFSYNC_VERIFYING:
            return;
    }
}

static void
osw_confsync_conf_changed(struct osw_confsync *cs)
{
    osw_confsync_set_state(cs, OSW_CONFSYNC_REQUESTING);
}

static void
osw_confsync_state_busy_cb(struct osw_state_observer *o)
{
    LOGD("osw: confsync: state: busy");
}

static void
osw_confsync_state_idle_cb(struct osw_state_observer *o)
{
    LOGD("osw: confsync: state: idle");
}

static void
osw_confsync_state_phy_added_cb(struct osw_state_observer *o,
                                const struct osw_state_phy_info *phy)
{
    struct osw_confsync *cs = container_of(o, struct osw_confsync, state_obs);
    LOGD("osw: confsync: state: %s: added", phy->phy_name);
    /* This, and other cases of conf_changed() called for
     * state observer is intentional. When entities
     * appear/disappear they impact fundamentally the way
     * osw_conf will look like. These happen seldom,
     * typically on system start, or hard reconfigs anyway,
     * so it's fine to consider these as conf_changed().
     */
    osw_confsync_conf_changed(cs);
}

static void
osw_confsync_state_phy_changed_cb(struct osw_state_observer *o,
                                  const struct osw_state_phy_info *phy)
{
    struct osw_confsync *cs = container_of(o, struct osw_confsync, state_obs);
    LOGD("osw: confsync: state: %s: changed", phy->phy_name);
    osw_confsync_state_changed(cs);
}

static void
osw_confsync_state_phy_removed_cb(struct osw_state_observer *o,
                                  const struct osw_state_phy_info *phy)
{
    struct osw_confsync *cs = container_of(o, struct osw_confsync, state_obs);
    LOGD("osw: confsync: state: %s: removed", phy->phy_name);
    osw_confsync_conf_changed(cs);
}

static void
osw_confsync_state_vif_added_cb(struct osw_state_observer *o,
                                const struct osw_state_vif_info *vif)
{
    struct osw_confsync *cs = container_of(o, struct osw_confsync, state_obs);
    LOGD("osw: confsync: state: %s/%s: added", vif->phy->phy_name, vif->vif_name);
    osw_confsync_defer_vif_enable_stop(cs, vif);
    osw_confsync_conf_changed(cs);
}

static void
osw_confsync_state_vif_changed_cb(struct osw_state_observer *o,
                                  const struct osw_state_vif_info *vif)
{
    struct osw_confsync *cs = container_of(o, struct osw_confsync, state_obs);
    LOGD("osw: confsync: state: %s/%s: changed", vif->phy->phy_name, vif->vif_name);
    osw_confsync_defer_vif_enable_stop(cs, vif);
    osw_confsync_state_changed(cs);
}

static void
osw_confsync_state_vif_removed_cb(struct osw_state_observer *o,
                                  const struct osw_state_vif_info *vif)
{
    struct osw_confsync *cs = container_of(o, struct osw_confsync, state_obs);
    LOGD("osw: confsync: state: %s/%s: removed", vif->phy->phy_name, vif->vif_name);
    osw_confsync_defer_vif_enable_stop(cs, vif);
    osw_confsync_conf_changed(cs);
}

static void
osw_confsync_conf_mutated_cb(struct osw_conf_observer *o)
{
    struct osw_confsync *cs = container_of(o, struct osw_confsync, conf_obs);
    osw_confsync_conf_changed(cs);
}

static void
osw_confsync_init(struct osw_confsync *cs)
{
    if (cs->state_obs.name != NULL) return;

    const struct osw_state_observer state_obs = {
        .name = __FILE__,
        .idle_fn = osw_confsync_state_idle_cb,
        .busy_fn = osw_confsync_state_busy_cb,
        .phy_added_fn = osw_confsync_state_phy_added_cb,
        .phy_changed_fn = osw_confsync_state_phy_changed_cb,
        .phy_removed_fn = osw_confsync_state_phy_removed_cb,
        .vif_added_fn = osw_confsync_state_vif_added_cb,
        .vif_changed_fn = osw_confsync_state_vif_changed_cb,
        .vif_removed_fn = osw_confsync_state_vif_removed_cb,
    };
    const struct osw_conf_observer conf_obs = {
        .name = __FILE__,
        .mutated_fn = osw_confsync_conf_mutated_cb,
    };
    const float retry = OSW_CONFSYNC_RETRY_SECONDS_DEFAULT;

    cs->state_obs = state_obs;
    cs->conf_obs = conf_obs;
    cs->build_conf = osw_conf_build;
    ds_dlist_init(&cs->changed_fns, struct osw_confsync_changed, node);
    ds_tree_init(&cs->defers, ds_str_cmp, struct osw_confsync_defer, node);
    ev_idle_init(&cs->work, osw_confsync_work_cb);
    ev_timer_init(&cs->retry, osw_confsync_retry_cb, retry, retry);
}

static void
osw_confsync_attach(struct osw_confsync *cs)
{
    if (cs->attached == true) return;
    osw_state_register_observer(&cs->state_obs);
    osw_conf_register_observer(&cs->conf_obs);
    cs->attached = true;
}

static void
osw_confsync_fini(struct osw_confsync *cs)
{
    osw_confsync_set_state(cs, OSW_CONFSYNC_IDLE);
}

static struct osw_confsync g_osw_confsync;

struct osw_confsync *
osw_confsync_get(void)
{
    osw_confsync_init(&g_osw_confsync);
    osw_confsync_attach(&g_osw_confsync);
    return &g_osw_confsync;
}

enum osw_confsync_state
osw_confsync_get_state(struct osw_confsync *cs)
{
    return cs->state;
}

const char *
osw_confsync_state_to_str(enum osw_confsync_state s)
{
    switch (s) {
        case OSW_CONFSYNC_IDLE: return "idle";
        case OSW_CONFSYNC_REQUESTING: return "requesting";
        case OSW_CONFSYNC_WAITING: return "waiting";
        case OSW_CONFSYNC_VERIFYING: return "verifying";
    }
    return "undefined";
}

struct osw_confsync_changed *
osw_confsync_register_changed_fn(struct osw_confsync *cs,
                                 const char *name,
                                 osw_confsync_changed_fn_t *fn,
                                 void *fn_priv)
{
    assert(cs != NULL);
    assert(fn != NULL);
    assert(name != NULL);

    LOGT("%s: cs=%p name=%s fn=%p priv=%p", __func__, cs, name, fn, fn_priv);

    struct osw_confsync_changed *c = CALLOC(1, sizeof(*c));
    c->cs = cs;
    c->name = STRDUP(name);
    c->fn = fn;
    c->fn_priv = fn_priv;
    ds_dlist_insert_tail(&cs->changed_fns, c);
    fn(cs, fn_priv);

    return c;
}

void
osw_confsync_unregister_changed(struct osw_confsync_changed *c)
{
    assert(c->cs != NULL);
    LOGT("%s: cs=%p name=%s fn=%p priv=%p", __func__, c->cs, c->name, c->fn, c->fn_priv);
    ds_dlist_remove(&c->cs->changed_fns, c);
    FREE(c->name); c->name = NULL;
    FREE(c);
}

static void
osw_confsync_ut_two_drivers_conf1_cb(struct osw_drv *drv, struct osw_drv_conf *conf)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    static bool run;
    LOGI("ow: confsync: ut: conf1");
    assert(run == false);
    run = true;
    assert(conf->n_phy_list == 1);
    assert(strcmp(conf->phy_list[0].phy_name, "phy1") == 0);
    ev_unref(EV_DEFAULT);
    osw_drv_dummy_set_phy(dummy, "phy1", (struct osw_drv_phy_state []) {{ .exists = true, .enabled = true }});
}

static void
osw_confsync_ut_two_drivers_conf2_cb(struct osw_drv *drv, struct osw_drv_conf *conf)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    static bool run;
    LOGI("ow: confsync: ut: conf2");
    assert(run == false);
    run = true;
    assert(conf->n_phy_list == 1);
    assert(strcmp(conf->phy_list[0].phy_name, "phy2") == 0);
    ev_unref(EV_DEFAULT);
    osw_drv_dummy_set_phy(dummy, "phy2", (struct osw_drv_phy_state []) {{ .exists = true, .enabled = true }});
}

static void
osw_confsync_ut_two_drivers_changed_cb(struct osw_confsync *cs, void *priv)
{
    const enum osw_confsync_state s = osw_confsync_get_state(cs);
    LOGI("ow: confsync: ut: changed: %s", osw_confsync_state_to_str(s));
}

struct osw_confsync_ut_two_drivers {
    struct osw_confsync cs;
    struct osw_state_observer obs;
};

static void
osw_confsync_ut_two_drivers_idle_cb(struct osw_state_observer *obs)
{
    struct osw_confsync_ut_two_drivers *ut;
    ut = container_of(obs, struct osw_confsync_ut_two_drivers, obs);
    LOGI("ow: confsync: ut: state: idle");
    ev_unref(EV_DEFAULT);

    ut->cs.build_conf = osw_conf_build_from_state;
    osw_confsync_set_state(&ut->cs, OSW_CONFSYNC_REQUESTING);
}

static struct ds_tree *
osw_confsync_ut_two_drivers_build_conf(void)
{
    struct ds_tree *phy_tree = osw_conf_build_from_state();
    struct osw_conf_phy *phy1 = ds_tree_find(phy_tree, "phy1");
    struct osw_conf_phy *phy2 = ds_tree_find(phy_tree, "phy2");
    phy1->enabled = true;
    phy2->enabled = true;
    return phy_tree;
}

OSW_UT(osw_confsync_ut_two_drivers)
{
    struct osw_drv_dummy dummy1 = {
        .name = "dummy1",
        .request_config_fn = osw_confsync_ut_two_drivers_conf1_cb,
    };
    struct osw_drv_dummy dummy2 = {
        .name = "dummy2",
        .request_config_fn = osw_confsync_ut_two_drivers_conf2_cb,
    };
    struct osw_confsync_ut_two_drivers ut = {
        .obs = {
            .idle_fn = osw_confsync_ut_two_drivers_idle_cb,
            .name = "two_drivers",
        },
    };
    struct osw_confsync_changed *c;

    osw_module_load_name("osw_drv");
    osw_drv_dummy_init(&dummy1);
    osw_drv_dummy_init(&dummy2);
    osw_state_register_observer(&ut.obs);
    osw_confsync_init(&ut.cs);
    assert(osw_confsync_get_state(&ut.cs) == OSW_CONFSYNC_IDLE);
    c = osw_confsync_register_changed_fn(&ut.cs,
                                         __func__,
                                         osw_confsync_ut_two_drivers_changed_cb,
                                         &ut);
    osw_drv_dummy_set_phy(&dummy1, "phy1", (struct osw_drv_phy_state []) {{ .exists = true }});
    osw_drv_dummy_set_phy(&dummy2, "phy2", (struct osw_drv_phy_state []) {{ .exists = true }});
    ev_ref(EV_DEFAULT); /* idle */
    LOGI("ow: confsync: ut: wait idle");
    ev_run(EV_DEFAULT_ 0);
    assert(osw_confsync_get_state(&ut.cs) == OSW_CONFSYNC_IDLE);
    LOGI("ow: confsync: ut: wait done");

    ut.cs.build_conf = osw_confsync_ut_two_drivers_build_conf;
    osw_confsync_set_state(&ut.cs, OSW_CONFSYNC_REQUESTING);
    ev_ref(EV_DEFAULT); /* obs */
    ev_ref(EV_DEFAULT); /* conf1 */
    ev_ref(EV_DEFAULT); /* conf2 */
    LOGI("ow: confsync: ut: wait conf");
    ev_run(EV_DEFAULT_ 0);

    osw_confsync_unregister_changed(c);
    osw_confsync_fini(&ut.cs);
}

static void
osw_confsync_ut_changed_cb(struct osw_confsync *cs, void *priv)
{
    int *i = priv;
    (*i)++;
}

OSW_UT(osw_confsync_changed_fn)
{
    int a = 0;
    int b = 0;
    int c = 0;

    struct osw_confsync cs;
    MEMZERO(cs);
    osw_confsync_init(&cs);

    struct osw_confsync_changed *c1;
    struct osw_confsync_changed *c2;
    struct osw_confsync_changed *c3;

    assert(a == 0);
    assert(b == 0);
    assert(c == 0);
    LOGT("%p %p %p", &a, &b, &c);

    c1 = osw_confsync_register_changed_fn(&cs, "c1", osw_confsync_ut_changed_cb, &a);
    c2 = osw_confsync_register_changed_fn(&cs, "c2", osw_confsync_ut_changed_cb, &b);
    assert(a == 1);
    assert(b == 1);
    assert(c == 0);

    osw_confsync_notify_changed(&cs);
    assert(a == 2);
    assert(b == 2);
    assert(c == 0);

    c3 = osw_confsync_register_changed_fn(&cs, "c3", osw_confsync_ut_changed_cb, &c);
    assert(a == 2);
    assert(b == 2);
    assert(c == 1);

    osw_confsync_notify_changed(&cs);
    assert(a == 3);
    assert(b == 3);
    assert(c == 2);

    osw_confsync_unregister_changed(c2);
    osw_confsync_notify_changed(&cs);
    assert(a == 4);
    assert(b == 3);
    assert(c == 3);

    osw_confsync_unregister_changed(c1);
    osw_confsync_notify_changed(&cs);
    assert(a == 4);
    assert(b == 3);
    assert(c == 4);

    osw_confsync_unregister_changed(c3);
    assert(a == 4);
    assert(b == 3);
    assert(c == 4);

    assert(ds_dlist_is_empty(&cs.changed_fns) == true);
}

OSW_UT(osw_confsync_neigh_filter)
{
    const struct osw_hwaddr n1addr = { .octet = { 1 } };
    const struct osw_hwaddr n2addr = { .octet = { 2 } };
    const struct osw_hwaddr n3addr = { .octet = { 3 } };
    const struct osw_hwaddr n4addr = { .octet = { 4 } };
    const struct osw_neigh n1 = { .bssid = n1addr, };
    const struct osw_neigh n2 = { .bssid = n2addr, };
    const struct osw_neigh n3a = { .bssid = n3addr, .phy_type = 0, };
    const struct osw_neigh n3b = { .bssid = n3addr, .phy_type = 1, };
    const struct osw_neigh n4 = { .bssid = n4addr, };

    struct ds_tree conf;
    ds_tree_init(&conf, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_conf_neigh, node);
    struct osw_conf_neigh ccn1 = { .neigh = n1, };
    struct osw_conf_neigh ccn2 = { .neigh = n3a, };
    struct osw_conf_neigh ccn3 = { .neigh = n4, };
    ds_tree_insert(&conf, &ccn1, &ccn1.neigh.bssid);
    ds_tree_insert(&conf, &ccn2, &ccn2.neigh.bssid);
    ds_tree_insert(&conf, &ccn3, &ccn3.neigh.bssid);

    struct osw_neigh sns[] = { n2, n3b, n4, };
    const struct osw_neigh_list state = { .list = sns, .count = ARRAY_SIZE(sns), };

    struct osw_neigh_list add;
    struct osw_neigh_list del;
    struct osw_neigh_list mod;
    struct osw_neigh_list nop;
    MEMZERO(add);
    MEMZERO(del);
    MEMZERO(mod);
    MEMZERO(nop);

    osw_confsync_vif_ap_neigh_filter(&add, &conf, &state, OSW_CONFSYNC_VIF_AP_NEIGH_ADD);
    osw_confsync_vif_ap_neigh_filter(&mod, &conf, &state, OSW_CONFSYNC_VIF_AP_NEIGH_MOD);
    osw_confsync_vif_ap_neigh_filter(&del, &conf, &state, OSW_CONFSYNC_VIF_AP_NEIGH_DEL);
    osw_confsync_vif_ap_neigh_filter(&nop, &conf, &state, OSW_CONFSYNC_VIF_AP_NEIGH_NOP);

    char buf[1024];
    osw_neigh_list_to_str(buf, sizeof(buf), &add); LOGT("%s: add: %s", __func__, buf);
    osw_neigh_list_to_str(buf, sizeof(buf), &mod); LOGT("%s: mod: %s", __func__, buf);
    osw_neigh_list_to_str(buf, sizeof(buf), &del); LOGT("%s: del: %s", __func__, buf);
    osw_neigh_list_to_str(buf, sizeof(buf), &nop); LOGT("%s: nop: %s", __func__, buf);

    LOGT("%s: %zu/%zu/%zu/%zu",
         __func__,
         add.count,
         mod.count,
         del.count,
         nop.count);

    assert(add.count == 1);
    assert(mod.count == 1);
    assert(del.count == 1);
    assert(nop.count == 1);

    assert(memcmp(&add.list[0], &n1, sizeof(n1)) == 0);
    assert(memcmp(&mod.list[0], &n3a, sizeof(n3a)) == 0);
    assert(memcmp(&del.list[0], &n2, sizeof(n2)) == 0);
    assert(memcmp(&nop.list[0], &n4, sizeof(n4)) == 0);

    FREE(add.list);
    FREE(mod.list);
    FREE(del.list);
    FREE(nop.list);
}

OSW_MODULE(osw_confsync)
{
    OSW_MODULE_LOAD(osw_conf);
    OSW_MODULE_LOAD(osw_state);
    OSW_MODULE_LOAD(osw_mux);
    osw_confsync_init(&g_osw_confsync);
    osw_confsync_attach(&g_osw_confsync);
    return NULL;
}
