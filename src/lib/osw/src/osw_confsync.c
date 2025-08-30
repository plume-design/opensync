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

#include "osw_types.h"
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
#include <osw_etc.h>

#define OSW_CONFSYNC_RETRY_SECONDS_DEFAULT 30.0
#define OSW_CONFSYNC_DEADLINE_SECONDS_DEFAULT 10.0
#define OSW_CONFSYNC_PHY_TREE_TIMEOUT_SECONDS_DEFAULT 60.0

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
 * utilized a way to mock-up for UT is necessary.
 */
typedef struct ds_tree *osw_confsync_build_conf_fn_t(void);

struct osw_confsync {
    struct ds_dlist changed_fns;
    enum osw_confsync_state state;
    struct osw_state_observer state_obs;
    struct osw_conf_observer conf_obs;
    osw_confsync_build_conf_fn_t *build_conf;
    ev_idle work;
    ev_timer deadline;
    ev_timer retry;
    bool attached;
    bool settled;
    struct ds_tree defers;
    struct ds_tree phys;
    struct ds_tree *last_phy_tree;
    ev_timer last_phy_tree_timeout;
};

struct osw_confsync_defer {
    struct osw_confsync *confsync;
    struct ds_tree_node node;
    char *name;
    struct osw_timer expiry;
    bool deferred;
};

struct osw_confsync_phy {
    struct osw_confsync *cs;
    char *phy_name;
    struct ds_tree_node node;
    struct osw_timer cac_timeout;
    struct osw_timer mbss_timeout;
};

struct osw_confsync_arg {
    struct osw_confsync *confsync;
    struct osw_drv_conf *drv_conf;
    const struct osw_drv_phy_state *sphy;
    struct osw_drv_phy_config *dphy;
    struct osw_conf_phy *cphy;
    struct osw_conf_vif *cvif;
    struct ds_tree *phy_tree;
    bool channel_changed;
    bool skip_sta;
    bool cac_planned;
    bool cac_ongoing;
    bool mbss_ongoing;
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

static bool
osw_confsync_any_phy_has_cac_running(struct osw_confsync *cs)
{
    struct osw_confsync_phy *phy;
    ds_tree_foreach(&cs->phys, phy) {
        if (osw_timer_is_armed(&phy->cac_timeout)) {
            return true;
        }
    }
    return false;
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

    if (cmd->radar_next_channel_changed) {
        LOGI("osw: confsync: %s: radar_next_channel_changed: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
             phy,
             OSW_CHANNEL_ARG(&(state->radar_next_channel)),
             OSW_CHANNEL_ARG(&(conf->radar_next_channel)));
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

    if (cmd->changed && !notified) {
        LOGW("osw: confsync: %s: changed, but missing specific attribute printout", phy);
    }
}

static void
osw_confsync_debug_passpoint_list(const char *phy, const char *vif,
                                  char **state, const size_t state_len,
                                  char **cmd, const size_t cmd_len, char name[])
{
    size_t i, len;
    if (osw_passpoint_str_list_is_equal(state, cmd, state_len, cmd_len)) return;

    len = (state_len > cmd_len)
          ? state_len
          : cmd_len;

    for (i = 0; i < len; i++) {
        LOGI("osw: confsync: %s/%s: passpoint_config: %s[%zu] \'%s\' -> \'%s\'",
             phy, vif, name, i,
             (state_len > i) ? state[i] : "",
             (cmd_len > i) ? cmd[i] : "");
    }
}

static void
osw_confsync_debug_passpoint_list_int(const char *phy, const char *vif,
                                      int *state, const size_t state_len,
                                      int *cmd, const size_t cmd_len, char name[])
{
    size_t i, len;
    if (state_len == cmd_len && memcmp(state, cmd, sizeof(*state) * state_len) == 0)
        return;

    len = (state_len > cmd_len)
          ? state_len
          : cmd_len;

    for (i = 0; i < len; i++) {
        LOGI("osw: confsync: %s/%s: passpoint_config: %s[%zu] \'%d\' -> \'%d\'",
             phy, vif, name, i,
             (state_len > i) ? state[i] : 0,
             (cmd_len > i) ? cmd[i] : 0);
    }
}

static void
osw_confsync_build_vif_ap_neigh_ft_debug(const char *phy,
                                         const char *vif,
                                         const struct osw_neigh_ft_list *from,
                                         const struct ds_tree *to)
{
    size_t i;

    for (i = 0; i < from->count; i++) {
        const struct osw_neigh_ft *fi = &from->list[i];
        const struct osw_conf_neigh_ft *ti = ds_tree_find((struct ds_tree *)to, &fi->bssid);
        const bool removed = (ti == NULL);
        if (removed) {
            LOGI("osw: confsync: %s/%s: neigh_ft: "OSW_NEIGH_FT_FMT": removed",
                 phy,
                 vif,
                 OSW_NEIGH_FT_ARG(fi));
        }
    }

    struct osw_conf_neigh_ft *ti;
    ds_tree_foreach((struct ds_tree *)to, ti) {
        const struct osw_neigh_ft *fi = osw_neigh_ft_list_lookup(from, &ti->neigh_ft.bssid);
        const bool added = (fi == NULL);
        const bool changed = (fi != NULL)
                          && (osw_neigh_ft_cmp(fi, &ti->neigh_ft) != 0);
        if (added) {
            LOGI("osw: confsync: %s/%s: neigh_ft: "OSW_NEIGH_FT_FMT": added",
                 phy,
                 vif,
                 OSW_NEIGH_FT_ARG(&ti->neigh_ft));
        }
        if (changed) {
            LOGI("osw: confsync: %s/%s: neigh_ft: "OSW_NEIGH_FT_FMT" -> "OSW_NEIGH_FT_FMT,
                 phy,
                 vif,
                 OSW_NEIGH_FT_ARG(fi),
                 OSW_NEIGH_FT_ARG(&ti->neigh_ft));
        }
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

    if (cmd->nas_identifier_changed) {
        const int max = ARRAY_SIZE(conf->nas_identifier.buf);
        LOGI("osw: confsync: %s/%s: nas_identifier: '%.*s' -> '%.*s'",
             phy, vif,
             max, state->nas_identifier.buf,
             max, conf->nas_identifier.buf);
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

    if (cmd->mbo_changed) {
        LOGI("osw: confsync: %s/%s: mbo: %d -> %d",
             phy, vif, state->mbo, conf->mbo);
        *notified = true;
    }

    if (cmd->oce_changed) {
        LOGI("osw: confsync: %s/%s: oce: %d -> %d",
             phy, vif, state->oce, conf->oce);
        *notified = true;
    }

    if (cmd->oce_min_rssi_dbm_changed) {
        LOGI("osw: confsync: %s/%s: oce_min_rssi_dbm: %d -> %d",
             phy, vif, state->oce_min_rssi_dbm, conf->oce_min_rssi_dbm);
        *notified = true;
    }

    if (cmd->oce_min_rssi_enable_changed) {
        LOGI("osw: confsync: %s/%s: oce_min_rssi_enable: %d -> %d",
             phy, vif, state->oce_min_rssi_enable, conf->oce_min_rssi_enable);
        *notified = true;
    }

    if (cmd->oce_retry_delay_sec_changed) {
        LOGI("osw: confsync: %s/%s: oce_retry_delay_sec: %d -> %d",
             phy, vif, state->oce_retry_delay_sec, conf->oce_retry_delay_sec);
        *notified = true;
    }

    if (cmd->max_sta_changed) {
        LOGI("osw: confsync: %s/%s: max_sta: %d -> %d",
             phy, vif, state->max_sta, conf->max_sta);
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

    if (cmd->neigh_ft_list_changed) {
        osw_confsync_build_vif_ap_neigh_ft_debug(phy,
                                                 vif,
                                                 &state->neigh_ft_list,
                                                 &conf->neigh_ft_tree);
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

    if (cmd->mbss_mode_changed) {
        const char *from = osw_mbss_vif_ap_mode_to_str(state->mbss_mode);
        const char *to = osw_mbss_vif_ap_mode_to_str(conf->mbss_mode);
        LOGI("osw: confsync: %s/%s: mbss_mode: %s -> %s",
              phy,
              vif,
              from,
              to);
        *notified = true;
    }

    if (cmd->mbss_group_changed) {
        LOGI("osw: confsync: %s/%s: mbss_group: %d -> %d",
              phy,
              vif,
              state->mbss_group,
              conf->mbss_group);
        *notified = true;
    }

    if (cmd->radius_list_changed) {
        char from[512];
        char to[512];
        osw_radius_list_to_str(from, sizeof(from), &state->radius_list);
        osw_radius_list_to_str(to, sizeof(to), &cmd->radius_list);
        LOGI("osw: confsync: %s/%s: radius_list: %s -> %s",
             phy, vif, from, to);
        *notified = true;
    }

    if (cmd->acct_list_changed) {
        char from[512];
        char to[512];
        osw_radius_list_to_str(from, sizeof(from), &state->acct_list);
        osw_radius_list_to_str(to, sizeof(to), &cmd->acct_list);
        LOGI("osw: confsync: %s/%s: acct_list: %s -> %s",
             phy, vif, from, to);
        *notified = true;
    }

    if (cmd->passpoint_changed) {
        if (!osw_hwaddr_is_equal(&state->passpoint.hessid, &cmd->passpoint.hessid))
            LOGI("osw: confsync: %s/%s: passpoint_config: hessid \'"OSW_HWADDR_FMT"\' -> \'"OSW_HWADDR_FMT"\'",
             phy, vif, OSW_HWADDR_ARG(&state->passpoint.hessid), OSW_HWADDR_ARG(&cmd->passpoint.hessid));
        if (state->passpoint.hs20_enabled != cmd->passpoint.hs20_enabled)
            LOGI("osw: confsync: %s/%s: passpoint_config: hs20 \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.hs20_enabled, cmd->passpoint.hs20_enabled);
        if (state->passpoint.adv_wan_status != cmd->passpoint.adv_wan_status)
            LOGI("osw: confsync: %s/%s: passpoint_config: adv_wan_status \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.adv_wan_status, cmd->passpoint.adv_wan_status);
        if (state->passpoint.adv_wan_symmetric != cmd->passpoint.adv_wan_symmetric)
            LOGI("osw: confsync: %s/%s: passpoint_config: adv_wan_symmetric \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.adv_wan_symmetric, cmd->passpoint.adv_wan_symmetric);
        if (state->passpoint.adv_wan_at_capacity != cmd->passpoint.adv_wan_at_capacity)
            LOGI("osw: confsync: %s/%s: passpoint_config: adv_wan_at_capacity \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.adv_wan_at_capacity, cmd->passpoint.adv_wan_at_capacity);
        if (state->passpoint.osen != cmd->passpoint.osen)
            LOGI("osw: confsync: %s/%s: passpoint_config: osen \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.osen, cmd->passpoint.osen);
        if (state->passpoint.asra != cmd->passpoint.asra)
            LOGI("osw: confsync: %s/%s: passpoint_config: asra \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.asra, cmd->passpoint.asra);
        if (state->passpoint.ant != cmd->passpoint.ant)
            LOGI("osw: confsync: %s/%s: passpoint_config: ant \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.ant, cmd->passpoint.ant);
        if (state->passpoint.venue_group != cmd->passpoint.venue_group)
            LOGI("osw: confsync: %s/%s: passpoint_config: venue_group \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.venue_group, cmd->passpoint.venue_group);
        if (state->passpoint.venue_type != cmd->passpoint.venue_type)
            LOGI("osw: confsync: %s/%s: passpoint_config: venue_type \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.venue_type, cmd->passpoint.venue_type);
        if (state->passpoint.anqp_domain_id != cmd->passpoint.anqp_domain_id)
            LOGI("osw: confsync: %s/%s: passpoint_config: anqp_domain_id \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.anqp_domain_id, cmd->passpoint.anqp_domain_id);
        if (state->passpoint.pps_mo_id != cmd->passpoint.pps_mo_id)
            LOGI("osw: confsync: %s/%s: passpoint_config: pps_mo_id \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.pps_mo_id, cmd->passpoint.pps_mo_id);
        if (state->passpoint.t_c_timestamp != cmd->passpoint.t_c_timestamp)
            LOGI("osw: confsync: %s/%s: passpoint_config: t_c_timestamp \'%d\' -> \'%d\'",
             phy, vif, state->passpoint.t_c_timestamp, cmd->passpoint.t_c_timestamp);
        if (osw_ssid_cmp(&state->passpoint.osu_ssid, &cmd->passpoint.osu_ssid) != 0)
            LOGI("osw: confsync: %s/%s: passpoint_config: osu_ssid \'%s\' -> \'%s\'",
             phy, vif, state->passpoint.osu_ssid.buf, cmd->passpoint.osu_ssid.buf);
        if (STRSCMP(state->passpoint.t_c_filename, cmd->passpoint.t_c_filename) != 0)
            LOGI("osw: confsync: %s/%s: passpoint_config: t_c_filename \'%s\' -> \'%s\'",
             phy, vif, state->passpoint.t_c_filename, cmd->passpoint.t_c_filename);
        if (STRSCMP(state->passpoint.anqp_elem, cmd->passpoint.anqp_elem) != 0)
            LOGI("osw: confsync: %s/%s: passpoint_config: anqp_elem \'%s\' -> \'%s\'",
             phy, vif, state->passpoint.anqp_elem, cmd->passpoint.anqp_elem);
        /* TODO debug lists */
        osw_confsync_debug_passpoint_list(phy, vif, state->passpoint.domain_list, state->passpoint.domain_list_len,
                                          cmd->passpoint.domain_list, cmd->passpoint.domain_list_len,
                                          "domain_list");
        osw_confsync_debug_passpoint_list(phy, vif, state->passpoint.nairealm_list, state->passpoint.nairealm_list_len,
                                          cmd->passpoint.nairealm_list, cmd->passpoint.nairealm_list_len,
                                          "nairealm_list");
        osw_confsync_debug_passpoint_list(phy, vif, state->passpoint.roamc_list, state->passpoint.roamc_list_len,
                                          cmd->passpoint.roamc_list, cmd->passpoint.roamc_list_len,
                                          "roamc_list");
        osw_confsync_debug_passpoint_list(phy, vif, state->passpoint.oper_fname_list, state->passpoint.oper_fname_list_len,
                                          cmd->passpoint.oper_fname_list, cmd->passpoint.oper_fname_list_len,
                                          "oper_fname_list");
        osw_confsync_debug_passpoint_list(phy, vif, state->passpoint.venue_name_list, state->passpoint.venue_name_list_len,
                                          cmd->passpoint.venue_name_list, cmd->passpoint.venue_name_list_len,
                                          "venue_name_list");
        osw_confsync_debug_passpoint_list(phy, vif, state->passpoint.venue_url_list, state->passpoint.venue_url_list_len,
                                          cmd->passpoint.venue_url_list, cmd->passpoint.venue_url_list_len,
                                          "venue_url_list");
        osw_confsync_debug_passpoint_list(phy, vif, state->passpoint.list_3gpp_list, state->passpoint.list_3gpp_list_len,
                                          cmd->passpoint.list_3gpp_list, cmd->passpoint.list_3gpp_list_len,
                                          "3gpp_list");
        osw_confsync_debug_passpoint_list_int(phy, vif, state->passpoint.net_auth_type_list,
                                              state->passpoint.net_auth_type_list_len,
                                              cmd->passpoint.net_auth_type_list,
                                              cmd->passpoint.net_auth_type_list_len,
                                              "net_auth_type_list");
        *notified = true;
    }

    if (cmd->ft_encr_key_changed) {
        LOGI("osw: confsync: %s/%s: ft_encr_key: "OSW_FT_ENCR_KEY_FMT" -> "OSW_FT_ENCR_KEY_FMT,
             phy, vif,
             OSW_FT_ENCR_KEY_ARG(&state->ft_encr_key),
             OSW_FT_ENCR_KEY_ARG(&conf->ft_encr_key));
        *notified = true;
    }

    if (cmd->ft_over_ds_changed) {
        LOGI("osw: confsync: %s/%s: ft_over_ds: %d -> %d",
             phy, vif, state->ft_over_ds, conf->ft_over_ds);
        *notified = true;
    }

    if (cmd->ft_pmk_r1_push_changed) {
        LOGI("osw: confsync: %s/%s: ft_pmk_r1_push: %d -> %d",
             phy, vif, state->ft_pmk_r1_push, conf->ft_pmk_r1_push);
        *notified = true;
    }

    if (cmd->ft_psk_generate_local_changed) {
        LOGI("osw: confsync: %s/%s: ft_psk_generate_local: %d -> %d",
             phy, vif, state->ft_psk_generate_local, conf->ft_psk_generate_local);
        *notified = true;
    }

    if (cmd->ft_pmk_r0_key_lifetime_sec_changed) {
        LOGI("osw: confsync: %s/%s: ft_pmk_r0_key_lifetime_sec: %d -> %d",
             phy, vif, state->ft_pmk_r0_key_lifetime_sec, conf->ft_pmk_r0_key_lifetime_sec);
        *notified = true;
    }

    if (cmd->ft_pmk_r1_max_key_lifetime_sec_changed) {
        LOGI("osw: confsync: %s/%s: ft_pmk_r1_max_key_lifetime_sec: %d -> %d",
             phy, vif, state->ft_pmk_r1_max_key_lifetime_sec, conf->ft_pmk_r1_max_key_lifetime_sec);
        *notified = true;
    }

    if (cmd->ft_mobility_domain_changed) {
        LOGI("osw: confsync: %s/%s: ft_mobility_domain: %d -> %d",
             phy, vif, state->ft_mobility_domain, conf->ft_mobility_domain);
        *notified = true;
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
         if (net->priority) {
            csnprintf(&buf, &len, " p:%d", net->priority);
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
osw_confsync_vif_ap_acl_policy_changed(const enum osw_acl_policy *a_policy,
                                       const enum osw_acl_policy *b_policy,
                                       const size_t acl_count)
{
    if (*a_policy != *b_policy) {
        if ((*a_policy == OSW_ACL_NONE && *b_policy == OSW_ACL_DENY_LIST) ||
            (*a_policy == OSW_ACL_DENY_LIST && *b_policy == OSW_ACL_NONE)) {
            if (acl_count == 0) {
                return false;
            } else {
                return true;
            }
        } else {
            return true;
        }
    }
    return false;
}

static bool
osw_confsync_vif_ap_radius_list_changed(struct ds_dlist *a, const struct osw_radius_list *b)
{
    size_t i = 0;
    struct osw_conf_radius *rad;

    if (ds_dlist_len(a) != b->count) return true;

    ds_dlist_foreach(a, rad) {
        if (osw_radius_is_equal(&rad->radius, &b->list[i++]) != true)
            return true;
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
osw_confsync_vif_ap_neigh_ft_tree_changed(struct ds_tree *a, const struct osw_neigh_ft_list *b)
{
    const size_t n = ds_tree_len(a);
    size_t i;

    if (n != b->count)
        return true;

    for (i = 0; i < b->count; i++) {
        const struct osw_neigh_ft *p = &b->list[i];
        const struct osw_conf_neigh_ft *q = ds_tree_find(a, p);
        if (q == NULL) return true;
        if (osw_ft_encr_key_is_equal(&p->ft_encr_key, &q->neigh_ft.ft_encr_key) != true) return true;
        if (osw_nas_id_is_equal(&p->nas_identifier, &q->neigh_ft.nas_identifier) != true) return true;
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

    if (conf.mcast_rate == OSW_RATE_UNSPEC) {
        state.mcast_rate = OSW_RATE_UNSPEC;
    }

    if (conf.mgmt_rate == OSW_RATE_UNSPEC) {
        state.mgmt_rate = OSW_RATE_UNSPEC;
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
     && state.beacon_rate.type == OSW_BEACON_RATE_UNSPEC
     && state.mcast_rate == OSW_RATE_UNSPEC
     && state.mgmt_rate == OSW_RATE_UNSPEC) {
        conf.supported_rates = 0;
        conf.basic_rates = 0;
        memset(&conf.beacon_rate, 0, size);
        conf.mcast_rate = OSW_RATE_UNSPEC;
        conf.mgmt_rate = OSW_RATE_UNSPEC;
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
    /* Currently CAC is not signalled in vif state reports.
     * Interfaces that aren't CAC-ed aren't really operating
     * yet so they shouldn't be bothered updating their own
     * neighbor list, as well as their neighbors shouldn't
     * be updated.
     *
     * As it is, this also prevents needless MLO
     * reconfigurations that can introduce additional
     * reconfiguration storm(s) on some platforms.
     */
    const bool neigh_changed = osw_confsync_any_phy_has_cac_running(osw_confsync_get())
                             ? false
                             : osw_confsync_vif_ap_neigh_tree_changed(&cvif->u.ap.neigh_tree, &svif->u.ap.neigh_list);

    const size_t acl_count = svif->u.ap.acl.count ?: ds_tree_len(&cvif->u.ap.acl_tree);

    dvif->u.ap.ssid_changed = all || osw_confsync_vif_ap_ssid_changed(&cvif->u.ap.ssid, &svif->u.ap.ssid);
    dvif->u.ap.psk_list_changed = all || osw_confsync_vif_ap_psk_tree_changed(&cvif->u.ap.psk_tree, &svif->u.ap.psk_list, &dvif->u.ap.wpa);
    dvif->u.ap.neigh_list_changed = all || neigh_changed;
    dvif->u.ap.neigh_ft_list_changed = all || osw_confsync_vif_ap_neigh_ft_tree_changed(&cvif->u.ap.neigh_ft_tree, &svif->u.ap.neigh_ft_list);
    dvif->u.ap.wps_cred_list_changed = all || osw_confsync_vif_ap_wps_cred_list_changed(&cvif->u.ap.wps_cred_list, &svif->u.ap.wps_cred_list);
    dvif->u.ap.acl_changed = all || osw_confsync_vif_ap_acl_tree_changed(&cvif->u.ap.acl_tree, &svif->u.ap.acl);
    dvif->u.ap.channel_changed = all || osw_confsync_vif_ap_channel_changed(&cvif->u.ap.channel, &svif->u.ap.channel);
    dvif->u.ap.beacon_interval_tu_changed = all || (svif->u.ap.beacon_interval_tu != cvif->u.ap.beacon_interval_tu);
    dvif->u.ap.isolated_changed = all || (svif->u.ap.isolated != cvif->u.ap.isolated);
    dvif->u.ap.ssid_hidden_changed = all || (svif->u.ap.ssid_hidden != cvif->u.ap.ssid_hidden);
    dvif->u.ap.mcast2ucast_changed = all || (svif->u.ap.mcast2ucast != cvif->u.ap.mcast2ucast);
    dvif->u.ap.acl_policy_changed = all || osw_confsync_vif_ap_acl_policy_changed(&cvif->u.ap.acl_policy, &svif->u.ap.acl_policy, acl_count);
    dvif->u.ap.wpa_changed = all || (memcmp(&svif->u.ap.wpa, &cvif->u.ap.wpa, sizeof(svif->u.ap.wpa)) != 0);
    dvif->u.ap.mode_changed = all || (osw_confsync_vif_ap_mode_changed(svif->u.ap.mode, cvif->u.ap.mode));
    dvif->u.ap.bridge_if_name_changed = all || (strcmp(svif->u.ap.bridge_if_name.buf, cvif->u.ap.bridge_if_name.buf) != 0);
    dvif->u.ap.nas_identifier_changed = all || (strcmp(svif->u.ap.nas_identifier.buf, cvif->u.ap.nas_identifier.buf) != 0);
    dvif->u.ap.ft_encr_key_changed = all || (strcmp(svif->u.ap.ft_encr_key.buf, cvif->u.ap.ft_encr_key.buf) != 0);
    dvif->u.ap.wps_pbc_changed = all || (svif->u.ap.wps_pbc != cvif->u.ap.wps_pbc);
    dvif->u.ap.multi_ap_changed = all || (memcmp(&svif->u.ap.multi_ap, &cvif->u.ap.multi_ap, sizeof(svif->u.ap.multi_ap)) != 0);
    dvif->u.ap.mbss_mode_changed = all || (svif->u.ap.mbss_mode != cvif->u.ap.mbss_mode);
    dvif->u.ap.mbss_group_changed = all || (svif->u.ap.mbss_group != cvif->u.ap.mbss_group);
    dvif->u.ap.radius_list_changed = all || osw_confsync_vif_ap_radius_list_changed(&cvif->u.ap.radius_list, &svif->u.ap.radius_list);
    dvif->u.ap.acct_list_changed = all || osw_confsync_vif_ap_radius_list_changed(&cvif->u.ap.accounting_list, &svif->u.ap.acct_list);
    dvif->u.ap.passpoint_changed = all || ((osw_passpoint_is_equal(&cvif->u.ap.passpoint, &svif->u.ap.passpoint)) != true);
    dvif->u.ap.ft_over_ds_changed = all || (svif->u.ap.ft_over_ds != cvif->u.ap.ft_over_ds);
    dvif->u.ap.ft_pmk_r0_key_lifetime_sec_changed = all || (svif->u.ap.ft_pmk_r0_key_lifetime_sec != cvif->u.ap.ft_pmk_r0_key_lifetime_sec);
    dvif->u.ap.ft_pmk_r1_max_key_lifetime_sec_changed = all || (svif->u.ap.ft_pmk_r1_max_key_lifetime_sec != cvif->u.ap.ft_pmk_r1_max_key_lifetime_sec);
    dvif->u.ap.ft_pmk_r1_push_changed = all || (svif->u.ap.ft_pmk_r1_push != cvif->u.ap.ft_pmk_r1_push);
    dvif->u.ap.ft_psk_generate_local_changed = all || (svif->u.ap.ft_psk_generate_local != cvif->u.ap.ft_psk_generate_local);
    dvif->u.ap.ft_mobility_domain_changed = all || (svif->u.ap.ft_mobility_domain != cvif->u.ap.ft_mobility_domain);
    dvif->u.ap.mbo_changed = all || (svif->u.ap.mbo != cvif->u.ap.mbo);
    dvif->u.ap.oce_changed = all || (svif->u.ap.oce != cvif->u.ap.oce);
    dvif->u.ap.oce_min_rssi_dbm_changed = all || (cvif->u.ap.oce && (svif->u.ap.oce_min_rssi_dbm != cvif->u.ap.oce_min_rssi_dbm));
    dvif->u.ap.oce_min_rssi_enable_changed = all || (cvif->u.ap.oce && (svif->u.ap.oce_min_rssi_enable != cvif->u.ap.oce_min_rssi_enable));
    dvif->u.ap.oce_retry_delay_sec_changed = all || (cvif->u.ap.oce && (svif->u.ap.oce_retry_delay_sec != cvif->u.ap.oce_retry_delay_sec));
    dvif->u.ap.max_sta_changed = all || (svif->u.ap.max_sta != cvif->u.ap.max_sta);

    dvif->changed |= dvif->u.ap.beacon_interval_tu_changed;
    dvif->changed |= dvif->u.ap.isolated_changed;
    dvif->changed |= dvif->u.ap.ssid_hidden_changed;
    dvif->changed |= dvif->u.ap.mcast2ucast_changed;
    dvif->changed |= dvif->u.ap.acl_policy_changed;
    dvif->changed |= dvif->u.ap.wpa_changed;
    dvif->changed |= dvif->u.ap.mode_changed;
    dvif->changed |= dvif->u.ap.bridge_if_name_changed;
    dvif->changed |= dvif->u.ap.nas_identifier_changed;
    dvif->changed |= dvif->u.ap.ft_encr_key_changed;
    dvif->changed |= dvif->u.ap.wps_pbc_changed;
    dvif->changed |= dvif->u.ap.multi_ap_changed;
    dvif->changed |= dvif->u.ap.psk_list_changed;
    dvif->changed |= dvif->u.ap.neigh_list_changed;
    dvif->changed |= dvif->u.ap.neigh_ft_list_changed;
    dvif->changed |= dvif->u.ap.wps_cred_list_changed;
    dvif->changed |= dvif->u.ap.acl_changed;
    dvif->changed |= dvif->u.ap.ssid_changed;
    dvif->changed |= dvif->u.ap.channel_changed;
    dvif->changed |= dvif->u.ap.mbss_mode_changed;
    dvif->changed |= dvif->u.ap.mbss_group_changed;
    dvif->changed |= dvif->u.ap.radius_list_changed;
    dvif->changed |= dvif->u.ap.acct_list_changed;
    dvif->changed |= dvif->u.ap.passpoint_changed;
    dvif->changed |= dvif->u.ap.ft_over_ds_changed;
    dvif->changed |= dvif->u.ap.ft_pmk_r0_key_lifetime_sec_changed;
    dvif->changed |= dvif->u.ap.ft_pmk_r1_max_key_lifetime_sec_changed;
    dvif->changed |= dvif->u.ap.ft_pmk_r1_push_changed;
    dvif->changed |= dvif->u.ap.ft_psk_generate_local_changed;
    dvif->changed |= dvif->u.ap.ft_mobility_domain_changed;
    dvif->changed |= dvif->u.ap.mbo_changed;
    dvif->changed |= dvif->u.ap.oce_changed;
    dvif->changed |= dvif->u.ap.oce_min_rssi_dbm_changed;
    dvif->changed |= dvif->u.ap.oce_min_rssi_enable_changed;
    dvif->changed |= dvif->u.ap.oce_retry_delay_sec_changed;
    dvif->changed |= dvif->u.ap.max_sta_changed;

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
            /* This shouldn't be a common occurrence, so keep it
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
    dvif->u.ap.nas_identifier = cvif->u.ap.nas_identifier;
    dvif->u.ap.beacon_interval_tu = cvif->u.ap.beacon_interval_tu;
    dvif->u.ap.channel = cvif->u.ap.channel;
    dvif->u.ap.isolated = cvif->u.ap.isolated;
    dvif->u.ap.ssid_hidden = cvif->u.ap.ssid_hidden;
    dvif->u.ap.mcast2ucast = cvif->u.ap.mcast2ucast;
    dvif->u.ap.mode = cvif->u.ap.mode;
    dvif->u.ap.acl_policy = cvif->u.ap.acl_policy;
    dvif->u.ap.ssid = cvif->u.ap.ssid;
    dvif->u.ap.wpa = cvif->u.ap.wpa;
    dvif->u.ap.mbo = cvif->u.ap.mbo;
    dvif->u.ap.oce = cvif->u.ap.oce;
    dvif->u.ap.oce_min_rssi_dbm = cvif->u.ap.oce_min_rssi_dbm;
    dvif->u.ap.oce_min_rssi_enable = cvif->u.ap.oce_min_rssi_enable;
    dvif->u.ap.oce_retry_delay_sec = cvif->u.ap.oce_retry_delay_sec;
    dvif->u.ap.max_sta = cvif->u.ap.max_sta;

    dvif->u.ap.wps_pbc = cvif->u.ap.wps_pbc;
    dvif->u.ap.multi_ap = cvif->u.ap.multi_ap;
    dvif->u.ap.mbss_mode = cvif->u.ap.mbss_mode;
    dvif->u.ap.mbss_group = cvif->u.ap.mbss_group;
    dvif->u.ap.ft_encr_key = cvif->u.ap.ft_encr_key;
    dvif->u.ap.ft_over_ds = cvif->u.ap.ft_over_ds;
    dvif->u.ap.ft_pmk_r0_key_lifetime_sec = cvif->u.ap.ft_pmk_r0_key_lifetime_sec;
    dvif->u.ap.ft_pmk_r1_max_key_lifetime_sec = cvif->u.ap.ft_pmk_r1_max_key_lifetime_sec;
    dvif->u.ap.ft_pmk_r1_push = cvif->u.ap.ft_pmk_r1_push;
    dvif->u.ap.ft_psk_generate_local = cvif->u.ap.ft_psk_generate_local;
    dvif->u.ap.ft_mobility_domain = cvif->u.ap.ft_mobility_domain;

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
        struct osw_neigh_ft *neighs_ft;
        struct osw_conf_neigh_ft *neigh_ft;
        int n = 0;
        int i = 0;

        ds_tree_foreach(&cvif->u.ap.neigh_ft_tree, neigh_ft)
            n++;
        neighs_ft = CALLOC(n, sizeof(*neighs_ft));
        ds_tree_foreach(&cvif->u.ap.neigh_ft_tree, neigh_ft) {
            const struct osw_neigh_ft *cn_ft = &neigh_ft->neigh_ft;
            struct osw_neigh_ft *dn_ft = &neighs_ft[i];
            *dn_ft = *cn_ft;
            i++;
        }

        dvif->u.ap.neigh_ft_list.list = neighs_ft;
        dvif->u.ap.neigh_ft_list.count = n;
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
    {
        struct osw_radius *radii;
        struct osw_conf_radius *radius;
        int n = 0;
        int i = 0;

        ds_dlist_foreach(&cvif->u.ap.radius_list, radius)
            n++;

        radii = CALLOC(n, sizeof(*radii));
        ds_dlist_foreach(&cvif->u.ap.radius_list, radius) {
            struct osw_radius *r = &radii[i];
            r->server = STRDUP(radius->radius.server);
            r->passphrase = STRDUP(radius->radius.passphrase);
            r->port = radius->radius.port;
            i++;
        }
        dvif->u.ap.radius_list.list = radii;
        dvif->u.ap.radius_list.count = i;
    }
    {
        struct osw_radius *accts;
        struct osw_conf_radius *acct;
        int n = 0;
        int i = 0;

        ds_dlist_foreach(&cvif->u.ap.accounting_list, acct)
            n++;

        accts = CALLOC(n, sizeof(*accts));
        ds_dlist_foreach(&cvif->u.ap.accounting_list, acct) {
            struct osw_radius *r = &accts[i];
            r->server = STRDUP(acct->radius.server);
            r->passphrase = STRDUP(acct->radius.passphrase);
            r->port = acct->radius.port;
            i++;
        }
        dvif->u.ap.acct_list.list = accts;
        dvif->u.ap.acct_list.count = i;
    }
    {
        const struct osw_passpoint *cpass = &cvif->u.ap.passpoint;
        struct osw_passpoint *dpass = &dvif->u.ap.passpoint;

        osw_passpoint_copy(cpass, dpass);
    }

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
                                       bool channel_changed,
                                       bool network_changed)
{
    const struct osw_drv_vif_state_sta *ssta = &svif->u.sta;
    struct osw_drv_vif_config_sta *dsta = &dvif->u.sta;
    const enum osw_drv_vif_state_sta_link_status desired = (dsta->network == NULL)
                                                         ? OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED
                                                         : OSW_DRV_VIF_STATE_STA_LINK_CONNECTED;

    switch (desired) {
        case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN:
            WARN_ON(1);
            return OSW_DRV_VIF_CONFIG_STA_NOP;
        case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED:
            switch (ssta->link.status) {
                case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN:
                    return OSW_DRV_VIF_CONFIG_STA_NOP;
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED:
                    /* subsequent check will verify if the network matches */
                    break;
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING:
                    if (channel_changed) {
                        return OSW_DRV_VIF_CONFIG_STA_DISCONNECT;
                    }
                    if (network_changed) {
                        return OSW_DRV_VIF_CONFIG_STA_CONNECT;
                    }
                    else {
                        return OSW_DRV_VIF_CONFIG_STA_NOP;
                    }
                case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED:
                    if (channel_changed) {
                        LOGI("osw: confsync: %s: postponing connect (from disconnected) due to channel change",
                             dvif->vif_name);
                        /* This is intentionally doing DISCONNECT
                         * instead of NOP in order to allow the tandem
                         * MLO re-connect.
                         */
                        return OSW_DRV_VIF_CONFIG_STA_DISCONNECT;
                    }
                    else {
                        return OSW_DRV_VIF_CONFIG_STA_CONNECT;
                    }
            }
            break;
        case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING:
            WARN_ON(1);
            return OSW_DRV_VIF_CONFIG_STA_NOP;
        case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED:
            switch (ssta->link.status) {
                case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN:
                    return OSW_DRV_VIF_CONFIG_STA_NOP;
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED:
                    return OSW_DRV_VIF_CONFIG_STA_DISCONNECT;
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING:
                    return OSW_DRV_VIF_CONFIG_STA_DISCONNECT;
                case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED:
                    return OSW_DRV_VIF_CONFIG_STA_NOP;
            }
            return OSW_DRV_VIF_CONFIG_STA_NOP;
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
        const bool ccmp256 = dnet->wpa.pairwise_ccmp256 && ssta->link.wpa.pairwise_ccmp256;
        const bool gcmp = dnet->wpa.pairwise_gcmp && ssta->link.wpa.pairwise_gcmp;
        const bool gcmp256 = dnet->wpa.pairwise_gcmp256 && ssta->link.wpa.pairwise_gcmp256;
        const bool tkip = dnet->wpa.pairwise_tkip && ssta->link.wpa.pairwise_tkip;
        const bool wpa = dnet->wpa.wpa && ssta->link.wpa.wpa;
        const bool rsn = dnet->wpa.rsn && ssta->link.wpa.rsn;
        const bool psk = dnet->wpa.akm_psk && ssta->link.wpa.akm_psk;
        const bool sae = dnet->wpa.akm_sae && ssta->link.wpa.akm_sae;
        const bool sae_ext = dnet->wpa.akm_sae_ext && ssta->link.wpa.akm_sae_ext;
        /* FIXME: FT? */
        const bool crypto_match = (ccmp || tkip || ccmp256 || gcmp || gcmp256)
                               && (wpa || rsn)
                               && (psk || sae || sae_ext);
        /* PMF needs reconnection on these events:
         * ┌───────────────┬───────────┬─────────────┐
         * │ Network block │ STA state │    Action   │
         * │  ieee80211w   │   report  │             │
         * ├───────────────┼───────────┼─────────────┤
         * │       0       │     0     │     OK      │
         * │       0       │     1     │  RECONNECT  │
         * │       0       │     2     │  RECONNECT  │
         * │       1       │     0     │     OK      │
         * │       1       │     1     │     OK      │
         * │       1       │     2     │     OK      │
         * │       2       │     0     │  RECONNECT  │
         * │       2       │     1     │     OK      │
         * │       2       │     2     │     OK      │
         * └───────────────┴───────────┴─────────────┘
         * Beacon protection is not handled here, which means
         * a runtime change of this parameter will not trigger
         * STA reconnection!
         */
        const bool pmf_match = ((dnet->wpa.pmf == OSW_PMF_OPTIONAL) ||
                                (!!dnet->wpa.pmf == !!ssta->link.wpa.pmf));
        const bool net_match = (bssid_valid == true && bssid_match == true)
                            || (bssid_valid == false && ssid_match == true);
        const bool match = net_match
                        && crypto_match
                        && multi_ap_match
                        && bridge_match
                        && pmf_match;

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

    /* This originally was RECONNECT but it must not be.
     *
     * If there's a planned channel + parent then it is
     * possible that the channel change is temporarily
     * stunted by ow_sta_channel_override.
     *
     * ow_sta_channel_override stunts any cloud-driven
     * channel changes as long as there's an active STA
     * link.
     *
     * As such on first pass the derivec configuration will
     * not have the channel changed. However for the parent
     * change to succeed it is necessary to first let go of
     * the current channel before it can be changed.  For
     * that to happen the STA link needs to be dropped.
     *
     * RECONNECT would do that, but it would immediately
     * start connecting. Normally this would result in no
     * link for some time, and channel would eventually be
     * changed before the target parent can actually be
     * found.
     *
     * However for MLO it's different. If a given link isn't
     * becoming available/ getting discovered, the system
     * will try to best-effort associate with whatever
     * subset of links is ready.
     *
     * Doing DISCONNECT gives a chance the channel to be
     * changed before an actual CONNECT is committed to/
     * started. This doesn't _guarantee_ success, but it
     * makes it far more likely to result in full
     * association (all configured MLO links) instead of a
     * subset.
     *
     * The other crucial part of the solution is the
     * "channel_changed" boolean that stunts CONNECT until
     * after channel is finished changing.
     *
     * This works because when STA link is dropped
     * ow_sta_channel_override will invalidate currently
     * derived configuration and confsync will re-evaluate
     * and re-build the osw_drv_conf. When its doing that
     * it'll see the channel being changed. Once channel
     * change finishes, the channel will change via
     * osw_state, invalidate osw_conf again, and re-build
     * the config _again_, this time with CONNECT.
     */
    return OSW_DRV_VIF_CONFIG_STA_DISCONNECT;
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
    const bool same_priority = a->priority == b->priority;
    struct osw_wpa wpa1 = a->wpa;
    struct osw_wpa wpa2 = b->wpa;
    wpa1.group_rekey_seconds = 0;
    wpa2.group_rekey_seconds = 0;
    const bool same_wpa = memcmp(&wpa1, &wpa2, sizeof(wpa1)) == 0;

    return same_bssid && same_ssid && same_psk && same_wpa && same_multi_ap && same_bridge && same_priority;
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
        dnet->priority = cnet->priority;
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
                                    const bool channel_changed,
                                    const bool allow_changed)
{
    const struct osw_drv_vif_state_sta *ssta = &svif->u.sta;
    struct osw_drv_vif_config_sta *dsta = &dvif->u.sta;
    struct osw_conf_vif_sta *csta = &cvif->u.sta;

    dsta->network = osw_confsync_build_drv_conf_vif_sta_net_list(csta);
    dsta->network_changed = osw_confsync_vif_sta_net_list_changed(ssta, dsta);
    dsta->network_changed &= allow_changed;
    dsta->operation = osw_confsync_build_drv_conf_vif_sta_op(dvif, svif, channel_changed, dsta->network_changed);

    dvif->changed |= dsta->network_changed;
    dvif->changed |= (dsta->operation != OSW_DRV_VIF_CONFIG_STA_NOP);
    dvif->changed &= allow_changed;
}

static bool
osw_confsync_cac_is_planned(const struct osw_drv_phy_state *sphy,
                            const struct osw_drv_vif_config *dvif)
{
    if (dvif->enabled == false) return false;
    const struct osw_channel_state *cs = sphy->channel_states;
    const size_t n_cs = sphy->n_channel_states;
    const struct osw_channel *c = &dvif->u.ap.channel;
    const bool cac_running = osw_cs_chan_intersects_state(cs, n_cs, c, OSW_CHANNEL_DFS_CAC_IN_PROGRESS);
    const bool cac_needed = osw_cs_chan_intersects_state(cs, n_cs, c, OSW_CHANNEL_DFS_CAC_POSSIBLE);
    const bool cac_completed = osw_cs_chan_intersects_state(cs, n_cs, c, OSW_CHANNEL_DFS_CAC_COMPLETED);
    const bool cac_discarded = (cac_completed && dvif->changed);
    return cac_running || cac_needed || cac_discarded;
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

static struct osw_confsync_phy *
osw_confsync_phy_new(struct osw_confsync *cs,
                     const char *phy_name);

static void
osw_confsync_mbss_start(struct osw_confsync *cs,
                        const char *phy_name)
{
    struct osw_confsync_phy *cs_phy = ds_tree_find(&cs->phys, phy_name);
    if (cs_phy == NULL) {
        cs_phy = osw_confsync_phy_new(cs, phy_name);
    }
    const bool already_started = osw_timer_is_armed(&cs_phy->mbss_timeout);
    if (already_started) return;

    LOGD("osw: confsync: %s: mbss: started", phy_name);
    const uint64_t expire_at = osw_time_mono_clk()
                             + OSW_TIME_SEC(OSW_CONFSYNC_ENABLE_PERIOD_SEC);
    osw_timer_arm_at_nsec(&cs_phy->mbss_timeout, expire_at);
}

static void
osw_confsync_mbss_reset(struct osw_confsync *cs)
{
    struct osw_confsync_phy *cs_phy;
    ds_tree_foreach(&cs->phys, cs_phy) {
        if (osw_timer_is_armed(&cs_phy->mbss_timeout)) {
            const char *phy_name = cs_phy->phy_name;
            LOGD("osw: confsync: %s: mbss: disarmed", phy_name);
            osw_timer_disarm(&cs_phy->mbss_timeout);
        }
    }
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
    const bool is_sta = (svif->vif_type == OSW_VIF_STA)
                     || (cvif->vif_type == OSW_VIF_STA);

    if (arg->skip_sta == is_sta)
        return;

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
                   || arg->mbss_ongoing
                   || arg->cac_ongoing
                   || arg->cac_planned
                   || deferred;

    dvif->changed = false;
    if (skip == false) {
        const bool enabled_changed = osw_confsync_vif_enabled_changed(svif->status, dvif->enabled);
        dvif->changed |= (dvif->enabled_changed = enabled_changed);
        dvif->changed |= (dvif->vif_type_changed = (cvif->vif_type != svif->vif_type));
        dvif->changed |= (dvif->tx_power_dbm_changed = cvif->tx_power_dbm != svif->tx_power_dbm);
    }

    if (arg->cac_planned) {
        LOGD("osw: confsync: %s/%s: skipping because another vif is planned to do cac",
                cvif->phy->phy_name,
                cvif->vif_name);
    }

    if (arg->cac_ongoing) {
        LOGD("osw: confsync: %s/%s: skipping because phy is already doing cac",
                cvif->phy->phy_name,
                cvif->vif_name);
    }

    switch (cvif->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            osw_confsync_build_drv_conf_vif_ap(dvif, sphy, svif, cvif, !skip);
            if (skip == false && osw_confsync_cac_is_planned(sphy, dvif)) {
                arg->cac_planned = true;
            }
            if (skip == false && dvif->u.ap.mbss_mode_changed) {
                osw_confsync_mbss_start(arg->confsync, cvif->phy->phy_name);
            }
            if (skip == false && dvif->u.ap.channel_changed) {
                arg->channel_changed = true;
            }
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            osw_confsync_build_drv_conf_vif_sta(dvif, svif, cvif, arg->channel_changed, !skip);
            break;
    }

    if (dvif->enabled_changed) {
        LOGI("osw: confsync: %s/%s: enabled: %s -> %s",
             cvif->phy->phy_name,
             cvif->vif_name,
             osw_vif_status_into_cstr(svif->status),
             dvif->enabled ? "enabled" : "disabled");
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
    dphy->radar_next_channel = cphy->radar_next_channel;
    dphy->radar = cphy->radar;
    dphy->reg_domain = cphy->reg_domain;

    const bool skip = (dphy->enabled == false && sphy->enabled == false);

    dphy->changed = false;
    if (skip == false) {
        dphy->changed |= (dphy->enabled_changed = cphy->enabled != sphy->enabled);
        dphy->changed |= (dphy->tx_chainmask_changed = cphy->tx_chainmask != sphy->tx_chainmask);
        dphy->changed |= (dphy->radar_next_channel_changed = !osw_channel_is_equal(&(cphy->radar_next_channel),
                                                                                   &(sphy->radar_next_channel)));
        dphy->changed |= (dphy->radar_changed = cphy->radar != sphy->radar);
        dphy->changed |= (dphy->reg_domain_changed = (memcmp(&cphy->reg_domain,
                                                             &sphy->reg_domain,
                                                             sizeof(cphy->reg_domain)) != 0));
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

static bool
osw_confsync_cac_is_ongoing(const struct osw_state_phy_info *phy)
{
    if (phy->drv_state->exists == false) {
        return false;
    }

    const struct osw_channel_state *cs = phy->drv_state->channel_states;
    const size_t n_cs = phy->drv_state->n_channel_states;
    size_t i;
    for (i = 0; i < n_cs; i++) {
        const struct osw_channel_state *csi = &cs[i];
        if (csi->dfs_state == OSW_CHANNEL_DFS_CAC_IN_PROGRESS) {
            return true;
        }
    }
    return false;
}

static bool
osw_confsync_cac_is_timed_out(struct osw_confsync *cs,
                              const struct osw_state_phy_info *phy)
{
    const char *phy_name = phy->phy_name;
    struct osw_confsync_phy *cs_phy = ds_tree_find(&cs->phys, phy_name);
    return (cs_phy != NULL) && (osw_timer_is_armed(&cs_phy->cac_timeout) == false);
}

static bool
osw_confsync_mbss_is_ongoing(struct osw_confsync *cs,
                             const char *phy_name)
{
    struct osw_confsync_phy *cs_phy = ds_tree_find(&cs->phys, phy_name);
    return (cs_phy != NULL) && osw_timer_is_armed(&cs_phy->mbss_timeout);
}

static void
osw_confsync_build_drv_conf_phy_cb(const struct osw_state_phy_info *phy,
                                   void *priv)
{
    struct osw_confsync_arg *arg = priv;
    struct ds_tree *phy_tree = arg->phy_tree;

    arg->cphy = ds_tree_find(phy_tree, phy->phy_name);
    arg->sphy = phy->drv_state;
    arg->cac_planned = false;
    arg->channel_changed = false;
    arg->mbss_ongoing = osw_confsync_mbss_is_ongoing(arg->confsync, phy->phy_name);
    arg->cac_ongoing = (osw_confsync_cac_is_ongoing(phy) == true)
                    && (osw_confsync_cac_is_timed_out(arg->confsync, phy) == false);
    assert(arg->cphy != NULL);

    osw_confsync_build_drv_conf_phy(arg, phy);

    /* Non-STA interfaces are processed first in order to
     * make sure that if there's a channel switch needed,
     * then it can stunt the CONNECT command until after
     * channel switch is completed.
     */
    arg->skip_sta = true;
    osw_state_vif_get_list(osw_confsync_build_drv_conf_vif_cb, phy->phy_name, arg);

    arg->skip_sta = false;
    osw_state_vif_get_list(osw_confsync_build_drv_conf_vif_cb, phy->phy_name, arg);
}

static void
osw_confsync_build_mld_sta_tandem_op(struct osw_confsync_arg *arg)
{
    size_t n_connect = 0;
    size_t n_disconnect = 0;
    size_t n_reconnect = 0;
    size_t i;
    for (i = 0; i < arg->drv_conf->n_phy_list; i++) {
        struct osw_drv_phy_config *phy = &arg->drv_conf->phy_list[i];
        size_t j;

        for (j = 0; j < phy->vif_list.count; j++) {
            struct osw_drv_vif_config *vif = &phy->vif_list.list[j];
            if (vif->vif_type != OSW_VIF_STA) continue;
            if (vif->enabled == false) continue;

            const char *vif_name = vif->vif_name;
            const struct osw_state_vif_info *vif_info = osw_state_vif_lookup_by_vif_name(vif_name);
            const struct osw_drv_vif_state *svif = vif_info ? vif_info->drv_state : NULL;
            const struct osw_drv_mld_state *mld_state = osw_drv_vif_state_get_mld_state(svif);
            const struct osw_hwaddr *mld_addr = mld_state ? &mld_state->addr : osw_hwaddr_zero();
            const bool not_mld = osw_hwaddr_is_zero(mld_addr);
            if (not_mld) continue;

            switch (vif->u.sta.operation) {
                case OSW_DRV_VIF_CONFIG_STA_NOP: break;
                case OSW_DRV_VIF_CONFIG_STA_CONNECT: n_connect++; break;
                case OSW_DRV_VIF_CONFIG_STA_RECONNECT: n_reconnect++; break;
                case OSW_DRV_VIF_CONFIG_STA_DISCONNECT: n_disconnect++; break;
            }

            size_t k;
            for (k = 0; k < arg->drv_conf->n_phy_list; k++) {
                struct osw_drv_phy_config *other_phy = &arg->drv_conf->phy_list[k];
                if (other_phy == phy) continue;

                size_t l;
                for (l = 0; l < other_phy->vif_list.count; l++) {
                    struct osw_drv_vif_config *other_vif = &other_phy->vif_list.list[l];
                    if (other_vif == vif) continue;
                    if (other_vif->vif_type != OSW_VIF_STA) continue;
                    if (other_vif->enabled == false) continue;

                    const char *other_vif_name = other_vif->vif_name;
                    const struct osw_state_vif_info *other_vif_info = osw_state_vif_lookup_by_vif_name(other_vif_name);
                    const struct osw_drv_vif_state *other_svif = other_vif_info ? other_vif_info->drv_state : NULL;
                    const struct osw_drv_mld_state *other_mld_state = osw_drv_vif_state_get_mld_state(other_svif);
                    const struct osw_hwaddr *other_mld_addr = other_mld_state ? &other_mld_state->addr : osw_hwaddr_zero();
                    const bool different_mld = (osw_hwaddr_is_equal(mld_addr, other_mld_addr) == false);
                    if (different_mld) continue;

                    switch (other_vif->u.sta.operation) {
                        case OSW_DRV_VIF_CONFIG_STA_NOP: break;
                        case OSW_DRV_VIF_CONFIG_STA_CONNECT: n_connect++; break;
                        case OSW_DRV_VIF_CONFIG_STA_RECONNECT: n_reconnect++; break;
                        case OSW_DRV_VIF_CONFIG_STA_DISCONNECT: n_disconnect++; break;
                    }
                }
            }

            const bool need_disconnect = (n_disconnect);
            const bool need_connect = !need_disconnect && (n_connect || n_reconnect);
            const enum osw_drv_vif_config_sta_operation op = need_disconnect ? OSW_DRV_VIF_CONFIG_STA_DISCONNECT :
                                                             need_connect ? OSW_DRV_VIF_CONFIG_STA_CONNECT :
                                                             OSW_DRV_VIF_CONFIG_STA_NOP;

            if (vif->u.sta.operation != op) {
                const char *phy_name = phy->phy_name;
                LOGI("osw: confsync: "OSW_HWADDR_FMT": %s/%s: op: override: %s -> %s",
                     OSW_HWADDR_ARG(mld_addr),
                     phy_name,
                     vif_name,
                     osw_confsync_sta_op_to_str(vif->u.sta.operation) ?: "",
                     osw_confsync_sta_op_to_str(op) ?: "");
                vif->u.sta.operation = op;
            }

            for (k = 0; k < arg->drv_conf->n_phy_list; k++) {
                struct osw_drv_phy_config *other_phy = &arg->drv_conf->phy_list[k];
                if (other_phy == phy) continue;

                size_t l;
                for (l = 0; l < other_phy->vif_list.count; l++) {
                    struct osw_drv_vif_config *other_vif = &other_phy->vif_list.list[l];
                    if (other_vif == vif) continue;
                    if (other_vif->vif_type != OSW_VIF_STA) continue;
                    if (other_vif->enabled == false) continue;

                    const char *other_vif_name = other_vif->vif_name;
                    const struct osw_state_vif_info *other_vif_info = osw_state_vif_lookup_by_vif_name(other_vif_name);
                    const struct osw_drv_vif_state *other_svif = other_vif_info ? other_vif_info->drv_state : NULL;
                    const struct osw_drv_mld_state *other_mld_state = osw_drv_vif_state_get_mld_state(other_svif);
                    const struct osw_hwaddr *other_mld_addr = other_mld_state ? &other_mld_state->addr : osw_hwaddr_zero();
                    const bool different_mld = (osw_hwaddr_is_equal(mld_addr, other_mld_addr) == false);
                    if (different_mld) continue;

                    if (other_vif->u.sta.operation != op) {
                        const char *other_phy_name = other_phy->phy_name;
                        LOGI("osw: confsync: "OSW_HWADDR_FMT": %s/%s: op: override: %s -> %s",
                             OSW_HWADDR_ARG(mld_addr),
                             other_phy_name,
                             other_vif_name,
                             osw_confsync_sta_op_to_str(other_vif->u.sta.operation) ?: "",
                             osw_confsync_sta_op_to_str(op) ?: "");
                        other_vif->u.sta.operation = op;
                    }
                }
            }
        }
    }
}

static struct osw_drv_conf *
osw_confsync_build_drv_conf(struct osw_confsync *cs, const bool debug, struct ds_tree *phy_tree)
{
    struct osw_confsync_arg arg = {
        .confsync = cs,
        .drv_conf = CALLOC(1, sizeof(*arg.drv_conf)),
        .phy_tree = phy_tree,
        .debug = debug,
    };
    osw_state_phy_get_list(osw_confsync_build_drv_conf_phy_cb, &arg);
    osw_confsync_build_mld_sta_tandem_op(&arg);
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
            osw_confsync_mbss_reset(cs);
            if (cs->settled == false) LOGN("osw: confsync: settled");
            cs->settled = true;
            ev_timer_stop(EV_DEFAULT_ &cs->retry);
            ev_idle_stop(EV_DEFAULT_ &cs->work);
            ev_timer_stop(EV_DEFAULT_ &cs->deadline);

            /* clear cached last_phy tree */
            ev_timer_stop(EV_DEFAULT_ &cs->last_phy_tree_timeout);
            osw_conf_free(cs->last_phy_tree);
            cs->last_phy_tree = NULL;
            break;
        case OSW_CONFSYNC_REQUESTING:
            osw_confsync_defer_flush_expired(cs);
            ev_timer_stop(EV_DEFAULT_ &cs->retry);
            ev_timer_again(EV_DEFAULT_ &cs->deadline);
            ev_idle_start(EV_DEFAULT_ &cs->work);
            break;
        case OSW_CONFSYNC_WAITING:
            if (cs->settled == true) LOGN("osw: confsync: unsettled");
            cs->settled = false;
            ev_idle_stop(EV_DEFAULT_ &cs->work);
            ev_timer_stop(EV_DEFAULT_ &cs->deadline);
            ev_timer_again(EV_DEFAULT_ &cs->retry);
            break;
        case OSW_CONFSYNC_VERIFYING:
            ev_idle_start(EV_DEFAULT_ &cs->work);
            ev_timer_again(EV_DEFAULT_ &cs->deadline);
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
    struct ds_tree *phy_tree = cs->build_conf();
    struct osw_drv_conf *conf = osw_confsync_build_drv_conf(cs, debug, phy_tree);
    osw_conf_free(phy_tree);
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
                struct ds_tree *phy_tree = cs->build_conf();
                if (osw_conf_is_equal(cs->last_phy_tree, phy_tree) == true) {
                    LOGI("osw: confsync: last config request is same as previous -> moving to state verifying");
                    osw_conf_free(phy_tree);
                    osw_confsync_set_state(cs, OSW_CONFSYNC_VERIFYING);
                    break;
                }
                LOGT("osw: confsync: current conf differs than last requested config");
                osw_conf_free(cs->last_phy_tree);
                cs->last_phy_tree = phy_tree;

                /* Restart timer */
                ev_timer_stop(EV_DEFAULT_ &cs->last_phy_tree_timeout);
                ev_timer_start(EV_DEFAULT_ &cs->last_phy_tree_timeout);

                struct osw_drv_conf *conf = osw_confsync_build_drv_conf(cs, debug, phy_tree);

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
osw_confsync_deadline_cb(EV_P_  ev_timer *arg, int events)
{
    struct osw_confsync *cs = container_of(arg, struct osw_confsync, deadline);
    LOGN("osw: confsync: work deadline reached, ignoring non-idle mainloop");
    osw_confsync_work(cs);
}

static void
osw_confsync_state_changed(struct osw_confsync *cs)
{
    /* When a configuration command is submitted the system
     * may go through transient state changes before
     * reaching a final state. To avoid needlessly
     * re-issuing configuration commands, and possibly
     * making configuration inadvertently longer, move to
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
osw_confsync_cac_timeout_cb(struct osw_timer *t)
{
    struct osw_confsync_phy *phy = container_of(t, typeof(*phy), cac_timeout);
    const char *phy_name = phy->phy_name;

    LOGN("osw: confsync: %s: cac: timed out", phy_name);
    osw_confsync_state_changed(phy->cs);
}

static void
osw_confsync_mbss_timeout_cb(struct osw_timer *t)
{
    struct osw_confsync_phy *phy = container_of(t, typeof(*phy), mbss_timeout);
    const char *phy_name = phy->phy_name;

    LOGD("osw: confsync: %s: mbss: timed out", phy_name);
    osw_confsync_state_changed(phy->cs);
}

static uint64_t
osw_confsync_cac_get_time(const struct osw_state_phy_info *phy)
{
    const struct osw_channel_state *cs = phy->drv_state->channel_states;
    const size_t n_cs = phy->drv_state->n_channel_states;
    uint64_t max = 0;
    size_t i;
    for (i = 0; i < n_cs; i++) {
        const struct osw_channel_state *csi = &cs[i];
        if (csi->dfs_state == OSW_CHANNEL_DFS_CAC_IN_PROGRESS) {
            const int freq = csi->channel.control_freq_mhz;
            const bool is_weather = (freq >= 5580)
                                 && (freq <= 5660);
            const uint64_t minutes = is_weather ? 10 : 1;
            const uint64_t seconds = minutes * 60;
            if (seconds > max) max = seconds;
        }
    }
    return max;
}

static void
osw_confsync_phy_gc(struct osw_confsync_phy *phy)
{
    if (osw_timer_is_armed(&phy->cac_timeout)) return;
    if (osw_timer_is_armed(&phy->mbss_timeout)) return;
    ds_tree_remove(&phy->cs->phys, phy);
    FREE(phy->phy_name);
    FREE(phy);
}

static struct osw_confsync_phy *
osw_confsync_phy_new(struct osw_confsync *cs,
                     const char *phy_name)
{
    struct osw_confsync_phy *cs_phy = CALLOC(1, sizeof(*cs_phy));
    cs_phy->cs = cs;
    cs_phy->phy_name = STRDUP(phy_name);
    ds_tree_insert(&cs->phys, cs_phy, cs_phy->phy_name);
    osw_timer_init(&cs_phy->cac_timeout, osw_confsync_cac_timeout_cb);
    osw_timer_init(&cs_phy->mbss_timeout, osw_confsync_mbss_timeout_cb);
    return cs_phy;
}

static void
osw_confsync_cac_update(struct osw_confsync *cs,
                        const struct osw_state_phy_info *phy)
{
    struct ds_tree *phys = &cs->phys;
    const char *phy_name = phy->phy_name;
    struct osw_confsync_phy *cs_phy = ds_tree_find(phys, phy_name);
    const uint64_t sec = 2 * osw_confsync_cac_get_time(phy);
    const uint64_t at = osw_time_mono_clk()
                      + OSW_TIME_SEC(sec);

    if (osw_confsync_cac_is_ongoing(phy) == false) {
        if (cs_phy != NULL) {
            if (osw_timer_is_armed(&cs_phy->cac_timeout)) {
                osw_timer_disarm(&cs_phy->cac_timeout);
                LOGI("osw: confsync: %s: cac: completed", phy_name);
            }
            else {
                LOGN("osw: confsync: %s: cac: completed after timeout", phy_name);
            }
            osw_confsync_phy_gc(cs_phy);
        }
    }
    else if (cs_phy == NULL) {
        cs_phy = osw_confsync_phy_new(cs, phy_name);
        osw_timer_arm_at_nsec(&cs_phy->cac_timeout, at);

        LOGN("osw: confsync: %s: cac: started: %"PRIu64" seconds",
             phy_name, sec);
    }
    else if (osw_timer_is_armed(&cs_phy->cac_timeout) == false) {
        osw_timer_arm_at_nsec(&cs_phy->cac_timeout, at);

        LOGN("osw: confsync: %s: cac: started: %"PRIu64" seconds (but previous cac hasn't finished!)",
             phy_name, sec);
    }
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
    osw_confsync_cac_update(cs, phy);
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
    osw_confsync_cac_update(cs, phy);
    osw_confsync_state_changed(cs);
}

static void
osw_confsync_state_phy_removed_cb(struct osw_state_observer *o,
                                  const struct osw_state_phy_info *phy)
{
    struct osw_confsync *cs = container_of(o, struct osw_confsync, state_obs);
    LOGD("osw: confsync: state: %s: removed", phy->phy_name);
    osw_confsync_cac_update(cs, phy);
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

static void osw_confsync_phy_tree_timeout_cb(EV_P_  ev_timer *arg, int events)
{
    struct osw_confsync *cs = container_of(arg, struct osw_confsync, last_phy_tree_timeout);
    LOGT("osw: confsync: last phy tree timer expired, flushing last phy_tree");
    osw_conf_free(cs->last_phy_tree);
    cs->last_phy_tree = NULL;
    ev_timer_stop(EV_DEFAULT_ &cs->last_phy_tree_timeout);
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
    const float deadline = OSW_CONFSYNC_DEADLINE_SECONDS_DEFAULT;
    const float last_phy_tree_timeout = OSW_CONFSYNC_PHY_TREE_TIMEOUT_SECONDS_DEFAULT;

    cs->state_obs = state_obs;
    cs->conf_obs = conf_obs;
    cs->build_conf = osw_conf_build;
    ds_dlist_init(&cs->changed_fns, struct osw_confsync_changed, node);
    ds_tree_init(&cs->defers, ds_str_cmp, struct osw_confsync_defer, node);
    ds_tree_init(&cs->phys, ds_str_cmp, struct osw_confsync_phy, node);
    ev_idle_init(&cs->work, osw_confsync_work_cb);
    ev_timer_init(&cs->retry, osw_confsync_retry_cb, retry, retry);
    ev_timer_init(&cs->deadline, osw_confsync_deadline_cb, deadline, deadline);
    ev_timer_init(&cs->last_phy_tree_timeout, osw_confsync_phy_tree_timeout_cb, last_phy_tree_timeout, last_phy_tree_timeout);
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
