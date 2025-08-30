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

#include <ev.h>
#include <const.h>
#include <util.h>
#include <os.h>
#include <memutil.h>
#include <iso3166.h>
#include <osw_drv_dummy.h>
#include <osw_state.h>
#include <osw_ut.h>
#include <osw_module.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_etc.h>
#include "ow_conf.h"
#include "ow_ovsdb_ms.h"
#include "ow_ovsdb_mld_onboard.h"
#include "ow_ovsdb_wps.h"
#include "ow_ovsdb_steer.h"
#include "ow_ovsdb_cconf.h"
#include "ow_ovsdb_stats.h"
#include "ow_ovsdb_hs.h"
#include "ow_mld_redir.h"
#include <ovsdb.h>
#include <ovsdb_table.h>
#include <ovsdb_cache.h>
#include <ovsdb_sync.h>
#include <schema_consts.h>
#include <schema_compat.h>

#define OW_OVSDB_RETRY_SECONDS 5.0
#define OW_OVSDB_VIF_FLAG_SEEN 0x01
#define OW_OVSDB_CM_NEEDS_PORT_STATE_BLIP 1
#define OW_OVSDB_PHY_LAST_CHANNEL_EXPIRE_SEC 5

/* FIXME: This will likely need legacy workarounds, eg. to not add
 * objects to State tables unless they are in Config table first,
 * unless it's AP_VLAN.
 */

static ovsdb_table_t table_Wifi_Radio_Config;
static ovsdb_table_t table_Wifi_Radio_State;
static ovsdb_table_t table_Wifi_VIF_Config;
static ovsdb_table_t table_Wifi_VIF_State;
static ovsdb_table_t table_Wifi_VIF_Neighbors;
static ovsdb_table_t table_Wifi_Associated_Clients;
static ovsdb_table_t table_Openflow_Tag;
static ovsdb_table_t table_RADIUS;
static ovsdb_table_t table_Passpoint_Config;
static ovsdb_table_t table_Passpoint_OSU_Providers;

struct ow_ovsdb {
    struct ds_tree phy_tree;
    struct ds_tree vif_tree;
    struct ds_tree sta_tree;
    struct ds_tree mld_tree;
    struct ow_ovsdb_ms_root ms;
    struct ow_ovsdb_wps_ops *wps;
    struct ow_ovsdb_wps_changed *wps_changed;
    struct ow_ovsdb_steer *steering;
    ow_ovsdb_mld_onboard_t *mld_onboard;
    ow_mld_redir_t *ow_mld_redir;
    ow_mld_redir_observer_t *mld_redir_obs;
    bool idle;
};

struct ow_ovsdb_phy {
    struct ds_tree_node node;
    struct ow_ovsdb *root;
    const struct osw_state_phy_info *info;
    struct osw_channel radar_channel;
    time_t radar_time;
    struct schema_Wifi_Radio_Config config;
    struct schema_Wifi_Radio_State state_cur;
    struct schema_Wifi_Radio_State state_new;
    struct osw_channel last_channel;
    struct osw_timer last_channel_expiry;
    char *phy_name;
    ev_timer work;
    ev_timer deadline;
};

struct ow_ovsdb_vif {
    struct ds_tree_node node;
    struct ow_ovsdb *root;
    const struct osw_state_vif_info *info;
    struct schema_Wifi_VIF_Config config;
    struct schema_Wifi_VIF_State state_cur;
    struct schema_Wifi_VIF_State state_new;
    char *phy_name;
    char *vif_name;
    ev_timer work;
    ev_timer deadline;
};

struct ow_ovsdb_sta_mld_oftag {
    struct ds_tree_node node;
    char *oftag;
    bool keep;
};

struct ow_ovsdb_sta_mld {
    struct ds_tree_node node;
    struct ds_tree links; /* ow_ovsdb_sta::node_mld */
    struct ds_tree oftags; /* ow_ovsdb_sta_mld_oftag::node */
    struct ow_ovsdb *root;
    struct osw_hwaddr mld_addr;
    ev_timer work;
    ev_timer deadline;
};

struct ow_ovsdb_sta {
    struct ds_tree_node node;
    struct ds_tree_node node_mld;
    struct ow_ovsdb_sta_mld *mld;
    struct ow_ovsdb *root;
    const struct osw_state_sta_info *info;
    struct schema_Wifi_Associated_Clients state_cur;
    struct schema_Wifi_Associated_Clients state_new;
    struct osw_hwaddr sta_addr;
    char *vif_name;
    char *oftag;
    time_t connected_at;
    ev_timer work;
    ev_timer deadline;
};

static int
ow_ovsdb_sta_cmp(const void *a, const void *b)
{
    const struct osw_hwaddr *x = a;
    const struct osw_hwaddr *y = b;
    return memcmp(x, y, sizeof(*x));
}

static struct ow_ovsdb g_ow_ovsdb = {
    .phy_tree = DS_TREE_INIT(ds_str_cmp, struct ow_ovsdb_phy, node),
    .vif_tree = DS_TREE_INIT(ds_str_cmp, struct ow_ovsdb_vif, node),
    .sta_tree = DS_TREE_INIT(ow_ovsdb_sta_cmp, struct ow_ovsdb_sta, node),
    .mld_tree = DS_TREE_INIT((ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_ovsdb_sta_mld, node),
};

static bool
ow_ovsdb_vif_config_state_rows_decoupled(void)
{
    /* This changes semantics of what Wifi_VIF_Config rows
     * (non-)existence implies. This change requires the
     * OVSDB controller to be updated in tandem to this
     * option being enabled.
     */
    return osw_etc_get("OW_OVSDB_VIF_CONFIG_STATE_ROWS_DECOUPLED");
}

static bool
ow_ovsdb_vif_treat_deleted_as_disabled(enum osw_vif_type vif_type)
{
    if (ow_ovsdb_vif_config_state_rows_decoupled()) {
        /* do not treat deleted as disabled */
        return false;
    }

    /* Current Wifi_VIF_Config row semantics are a bit
     * hairy. Row removal is considered implicitly as a
     * desire to disable an interface (and remove it).
     * However removal of interfaces is strictly
     * implementation specific - both historically and in
     * OSW. It only makes sense to disable interfaces.
     * However row existence should ideally be considered as
     * intent to override state of a matching if_name row in
     * Wifi_VIF_State. Until that is fixed in the entire
     * stack (all the way to the controller) the implicit
     * delete-is-disable needs to be upheld.
     */
    switch (vif_type) {
        case OSW_VIF_AP: return true;
        case OSW_VIF_AP_VLAN: return false;
        case OSW_VIF_STA: return true;
        case OSW_VIF_UNDEFINED: return false;
    }
    return true;
}

static bool
ow_ovsdb_vif_report_only_configured(enum osw_vif_type vif_type)
{
    if (ow_ovsdb_vif_config_state_rows_decoupled()) {
        /* allow any state to be seen regardless of config */
        return false;
    }

    /* This is tied to reporting Wifi_VIF_State rows only if
     * there's an associated Wifi_VIF_Config row of a given
     * if_name. The deleted as disabled is tightly coupled
     * to that, but for clarity of intent this is kept as a
     * separate helper.
     */
    switch (vif_type) {
        case OSW_VIF_AP: return true;
        case OSW_VIF_AP_VLAN: return false;
        case OSW_VIF_STA: return true;
        case OSW_VIF_UNDEFINED: return false;
    }
    return false;
}

static bool
ow_ovsdb_vif_is_deleted(const struct ow_ovsdb_vif *vif)
{
    return strlen(vif->config.if_name) == 0;
}

static int
ow_ovsdb_freq_to_chan(int freq)
{
    /* FIXME: This could be moved to a common helper to be shared with other
     * components. This isn't specific to ow_ovsdb or its tasks. */

    if (freq >= 2412 && freq <= 2472)
        return (freq - 2407) / 5;
    else if (freq == 2484)
        return 14;
    else if (freq >= 5180 && freq <= 5885)
        return (freq - 5000) / 5;
    else if (freq == 5935)
        return 2;
    else if (freq >= 5955 && freq <= 7115)
        return (freq - 5950) / 5;

    //WARN_ON(1);
    return 0;
}

const char *
ow_ovsdb_width_to_htmode(enum osw_channel_width w)
{
    switch (w) {
        case OSW_CHANNEL_20MHZ: return "HT20";
        case OSW_CHANNEL_40MHZ: return "HT40";
        case OSW_CHANNEL_80MHZ: return "HT80";
        case OSW_CHANNEL_160MHZ: return "HT160";
        case OSW_CHANNEL_80P80MHZ: return "HT80+80";
        case OSW_CHANNEL_320MHZ: return "HT320";
    }
    return NULL;
}

static const char *
ow_ovsdb_acl_policy_to_str(enum osw_acl_policy p)
{
    switch (p) {
        case OSW_ACL_NONE: return "none";
        case OSW_ACL_ALLOW_LIST: return "whitelist";
        case OSW_ACL_DENY_LIST: return "blacklist";
    }
    return NULL;
}

static enum osw_acl_policy
ow_ovsdb_acl_policy_from_str(const char *type)
{
    if (strcmp(type, "none") == 0) return OSW_ACL_NONE;
    if (strcmp(type, "whitelist") == 0) return OSW_ACL_ALLOW_LIST;
    if (strcmp(type, "blacklist") == 0) return OSW_ACL_DENY_LIST;
    return OSW_ACL_NONE;
}

enum ow_ovsdb_min_hw_mode {
    OW_OVSDB_MIN_HW_MODE_UNSPEC,
    OW_OVSDB_MIN_HW_MODE_11B,
    OW_OVSDB_MIN_HW_MODE_11A,
    OW_OVSDB_MIN_HW_MODE_11G,
    OW_OVSDB_MIN_HW_MODE_11N,
    OW_OVSDB_MIN_HW_MODE_11AC,
};

static const char *
ow_ovsdb_min_hw_mode_to_str(const enum ow_ovsdb_min_hw_mode mode)
{
    switch (mode) {
        case OW_OVSDB_MIN_HW_MODE_UNSPEC: return "";
        case OW_OVSDB_MIN_HW_MODE_11B: return "11b";
        case OW_OVSDB_MIN_HW_MODE_11A: return "11a";
        case OW_OVSDB_MIN_HW_MODE_11G: return "11g";
        case OW_OVSDB_MIN_HW_MODE_11N: return "11n";
        case OW_OVSDB_MIN_HW_MODE_11AC: return "11ac";
    }
    WARN_ON(1);
    return "";
}

static enum ow_ovsdb_min_hw_mode
ow_ovsdb_min_hw_mode_from_str(const char *str)
{
    if (strcmp(str, "11a") == 0) return OW_OVSDB_MIN_HW_MODE_11A;
    if (strcmp(str, "11b") == 0) return OW_OVSDB_MIN_HW_MODE_11B;
    if (strcmp(str, "11g") == 0) return OW_OVSDB_MIN_HW_MODE_11G;
    if (strcmp(str, "11n") == 0) return OW_OVSDB_MIN_HW_MODE_11N;
    if (strcmp(str, "11ac") == 0) return OW_OVSDB_MIN_HW_MODE_11AC;
    return OW_OVSDB_MIN_HW_MODE_UNSPEC;
}

static void
ow_ovsdb_min_hw_mode_to_ap_mode(struct osw_ap_mode *ap_mode,
                                const enum ow_ovsdb_min_hw_mode min_hw_mode)
{
    switch (min_hw_mode) {
        case OW_OVSDB_MIN_HW_MODE_UNSPEC:
            return;
        case OW_OVSDB_MIN_HW_MODE_11B:
            ap_mode->supported_rates = osw_rate_legacy_cck();
            ap_mode->basic_rates = osw_rate_legacy_cck_basic();
            ap_mode->beacon_rate = *osw_beacon_rate_cck();
            return;
        case OW_OVSDB_MIN_HW_MODE_11AC:
            ap_mode->vht_required = true;
            ap_mode->supported_rates = osw_rate_legacy_ofdm();
            ap_mode->basic_rates = osw_rate_legacy_ofdm_basic();
            ap_mode->beacon_rate = *osw_beacon_rate_ofdm();
            return;
        case OW_OVSDB_MIN_HW_MODE_11N:
            ap_mode->ht_required = true;
            ap_mode->supported_rates = osw_rate_legacy_ofdm();
            ap_mode->basic_rates = osw_rate_legacy_ofdm_basic();
            ap_mode->beacon_rate = *osw_beacon_rate_ofdm();
            return;
        case OW_OVSDB_MIN_HW_MODE_11A:
        case OW_OVSDB_MIN_HW_MODE_11G:
            ap_mode->supported_rates = osw_rate_legacy_ofdm();
            ap_mode->basic_rates = osw_rate_legacy_ofdm_basic();
            ap_mode->beacon_rate = *osw_beacon_rate_ofdm();
            return;
    }
}

static enum ow_ovsdb_min_hw_mode
ow_ovsdb_min_hw_mode_from_ap_mode(const struct osw_ap_mode *ap_mode,
                                  const enum osw_band band)
{
    const size_t size = sizeof(ap_mode->beacon_rate);
    const bool is_cck = ap_mode->supported_rates == osw_rate_legacy_cck()
                     && ap_mode->basic_rates == osw_rate_legacy_cck_basic()
                     && (memcmp(&ap_mode->beacon_rate, osw_beacon_rate_cck(), size) == 0);
    const bool is_ofdm = ap_mode->supported_rates == osw_rate_legacy_ofdm()
                      && ap_mode->basic_rates == osw_rate_legacy_ofdm_basic()
                      && (memcmp(&ap_mode->beacon_rate, osw_beacon_rate_ofdm(), size) == 0);
    const bool is_2ghz = (band == OSW_BAND_2GHZ);
    const bool is_not_2ghz = !is_2ghz;

    if (is_cck) {
        if (ap_mode->vht_required) return OW_OVSDB_MIN_HW_MODE_UNSPEC;
        if (ap_mode->ht_required) return OW_OVSDB_MIN_HW_MODE_UNSPEC;
        if (is_not_2ghz) return OW_OVSDB_MIN_HW_MODE_UNSPEC;

        return OW_OVSDB_MIN_HW_MODE_11B;
    }
    else if (is_ofdm) {
        if (ap_mode->vht_required) return OW_OVSDB_MIN_HW_MODE_11AC;
        if (ap_mode->ht_required) return OW_OVSDB_MIN_HW_MODE_11N;

        if (is_2ghz) return OW_OVSDB_MIN_HW_MODE_11G;
        if (is_not_2ghz) return OW_OVSDB_MIN_HW_MODE_11A;
    }

    return OW_OVSDB_MIN_HW_MODE_UNSPEC;
}

static const char *
ow_ovsdb_sta_multi_ap_to_cstr(const bool multi_ap)
{
    if (multi_ap) {
        return SCHEMA_CONSTS_MULTI_AP_BACKHAUL_STA;
    }
    else {
        return SCHEMA_CONSTS_MULTI_AP_NONE;
    }
}

static bool
ow_ovsdb_sta_multi_ap_from_cstr(const char *str)
{
    if (strcmp(str, SCHEMA_CONSTS_MULTI_AP_BACKHAUL_STA) == 0) {
        return true;
    }
    else if (strcmp(str, SCHEMA_CONSTS_MULTI_AP_NONE) == 0) {
        return false;
    }
    else {
        WARN_ON(1);
        return false;
    }
}

static const char *
ow_ovsdb_ap_multi_ap_to_cstr(const struct osw_multi_ap *multi_ap)
{
    if (multi_ap->fronthaul_bss && multi_ap->backhaul_bss) {
        return SCHEMA_CONSTS_MULTI_AP_FRONTHAUL_BACKHAUL_BSS;
    }
    else if (multi_ap->fronthaul_bss && !multi_ap->backhaul_bss) {
        return SCHEMA_CONSTS_MULTI_AP_FRONTHAUL_BSS;
    }
    else if (!multi_ap->fronthaul_bss && multi_ap->backhaul_bss) {
        return SCHEMA_CONSTS_MULTI_AP_BACKHAUL_BSS;
    }
    else {
        return SCHEMA_CONSTS_MULTI_AP_NONE;
    }
}

static void
ow_ovsdb_ap_multi_ap_from_cstr(const char *str,
                               struct osw_multi_ap *multi_ap)
{
    if (strcmp(str, SCHEMA_CONSTS_MULTI_AP_NONE) == 0) {
        multi_ap->fronthaul_bss = false;
        multi_ap->backhaul_bss = false;
    }
    else if (strcmp(str, SCHEMA_CONSTS_MULTI_AP_FRONTHAUL_BSS) == 0) {
        multi_ap->fronthaul_bss = true;
        multi_ap->backhaul_bss = false;
    }
    else if (strcmp(str, SCHEMA_CONSTS_MULTI_AP_FRONTHAUL_BACKHAUL_BSS) == 0) {
        multi_ap->fronthaul_bss = true;
        multi_ap->backhaul_bss = true;
    }
    else if (strcmp(str, SCHEMA_CONSTS_MULTI_AP_BACKHAUL_BSS) == 0) {
        multi_ap->fronthaul_bss = false;
        multi_ap->backhaul_bss = true;
    }
    else {
        WARN_ON(1);
    }
}

static enum osw_acl_policy
ow_ovsdb_ap_get_acl_policy(const struct osw_drv_vif_state_ap *ap,
                        const struct schema_Wifi_VIF_Config *vconf)
{
    if (vconf == NULL) return ap->acl_policy;
    if (ap->acl.count > 0) return ap->acl_policy;

    // if config is valid, and ACL is empty, then blacklist == none
    // so report whatever config has to satisfy the cloud controller
    switch (ap->acl_policy) {
        case OSW_ACL_NONE:
            if (strcmp(vconf->mac_list_type, "blacklist") == 0) return OSW_ACL_DENY_LIST;
            break;
        case OSW_ACL_DENY_LIST:
            if (strcmp(vconf->mac_list_type, "none") == 0) return OSW_ACL_NONE;
            break;
        case OSW_ACL_ALLOW_LIST:
            break;
    }

    return ap->acl_policy;
}

/* FIXME: These get helpers could be replaced with
 * ovsdb_cache lookups to be more efficient  */

static bool
ow_ovsdb_get_rstate(struct schema_Wifi_Radio_State *rstate,
                   const char *phy_name)
{
    char arg[32];
    int n;
    STRSCPY_WARN(arg, phy_name);
    memset(rstate, 0, sizeof(*rstate));
    void *p = ovsdb_table_select(&table_Wifi_Radio_State,
                                 SCHEMA_COLUMN(Wifi_Radio_State, if_name),
                                 arg,
                                 &n);
    if (p) memcpy(rstate, p, sizeof(*rstate));
    free(p);
    return p && n == 1;
}

static bool
ow_ovsdb_get_vstate(struct schema_Wifi_VIF_State *vstate,
                   const char *vif_name)
{
    char arg[32];
    int n;
    STRSCPY_WARN(arg, vif_name);
    memset(vstate, 0, sizeof(*vstate));
    void *p = ovsdb_table_select(&table_Wifi_VIF_State,
                                 SCHEMA_COLUMN(Wifi_VIF_State, if_name),
                                 arg,
                                 &n);
    if (p) memcpy(vstate, p, sizeof(*vstate));
    free(p);
    return p && n == 1;
}

static void
ow_ovsdb_phystate_get_bcn_int_iter_cb(const struct osw_state_vif_info *info,
                                      void *priv)
{
    int *bcn_int = priv;
    if (info->drv_state->status != OSW_VIF_ENABLED) return;
    if (info->drv_state->vif_type != OSW_VIF_AP) return;
    if (*bcn_int == 0) *bcn_int = info->drv_state->u.ap.beacon_interval_tu;
    if (*bcn_int != info->drv_state->u.ap.beacon_interval_tu) *bcn_int = -1;
}

static void
ow_ovsdb_phystate_fill_bcn_int(struct schema_Wifi_Radio_State *schema,
                               const struct osw_state_phy_info *phy)
{
    int bcn_int = 0;
    osw_state_vif_get_list(ow_ovsdb_phystate_get_bcn_int_iter_cb,
                           phy->phy_name,
                           &bcn_int);
    if (bcn_int > 0)
        SCHEMA_SET_INT(schema->bcn_int, bcn_int);
}

static void
ow_ovsdb_phystate_get_channel_h_chan(const struct osw_channel *i,
                                     void *priv)
{
    struct osw_channel *c = priv;

    if (c->control_freq_mhz == 0) *c = *i;
    if (memcmp(c, i, sizeof(*c)) != 0) c->control_freq_mhz = -1;
}

static void
ow_ovsdb_phystate_get_channel_h_ap(const struct osw_state_vif_info *info,
                                   void *priv)
{
    const struct osw_channel *i = &info->drv_state->u.ap.channel;

    if (info->drv_state->status != OSW_VIF_ENABLED) return;
    if (info->drv_state->vif_type != OSW_VIF_AP) return;
    ow_ovsdb_phystate_get_channel_h_chan(i, priv);
}

static void
ow_ovsdb_phystate_get_channel_h_sta(const struct osw_state_vif_info *info,
                                    void *priv)
{
    const struct osw_channel *i = &info->drv_state->u.sta.link.channel;

    if (info->drv_state->status != OSW_VIF_ENABLED) return;
    if (info->drv_state->vif_type != OSW_VIF_STA) return;
    if (info->drv_state->u.sta.link.status != OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) return;
    ow_ovsdb_phystate_get_channel_h_chan(i, priv);
}

static void
ow_ovsdb_phystate_get_channel_iter_cb(const struct osw_state_vif_info *info,
                                      void *priv)
{
    ow_ovsdb_phystate_get_channel_h_ap(info, priv);
    ow_ovsdb_phystate_get_channel_h_sta(info, priv);
}

static void
ow_ovsdb_sync_sched(struct ev_timer *work, struct ev_timer *deadline)
{
    if (ev_is_active(work)) {
        ev_timer_stop(EV_DEFAULT_ work);
    }
    else {
        ev_timer_stop(EV_DEFAULT_ deadline);
        ev_timer_set(deadline, 3, 0);
        ev_timer_start(EV_DEFAULT_ deadline);
    }

    ev_timer_set(work, 0.1, 5);
    ev_timer_start(EV_DEFAULT_ work);
}

static bool
ow_ovsdb_sync_allowed(struct ow_ovsdb *root, struct ev_timer *deadline)
{
    const bool deadline_reached = !ev_is_active(deadline);
    return root->idle || deadline_reached;
}

static void
ow_ovsdb_phy_work_sched(struct ow_ovsdb_phy *phy)
{
    ow_ovsdb_sync_sched(&phy->work, &phy->deadline);
}

static void
ow_ovsdb_phy_last_channel_expiry_cb(struct osw_timer *t)
{
    struct ow_ovsdb_phy *phy = container_of(t, struct ow_ovsdb_phy, last_channel_expiry);
    LOGD("ow: ovsdb: phy: %s: last_channel: "OSW_CHANNEL_FMT": expired",
          phy->phy_name,
          OSW_CHANNEL_ARG(&phy->last_channel));
    MEMZERO(phy->last_channel);
    ow_ovsdb_phy_work_sched(phy);
}

static void
ow_ovsdb_phy_last_channel_expire_sched(struct ow_ovsdb_phy *phy)
{
    if (osw_timer_is_armed(&phy->last_channel_expiry)) return;
    const uint64_t at_nsec = osw_time_mono_clk()
                           + OSW_TIME_SEC(OW_OVSDB_PHY_LAST_CHANNEL_EXPIRE_SEC);
    osw_timer_arm_at_nsec(&phy->last_channel_expiry, at_nsec);
    LOGD("ow: ovsdb: phy: %s: last_channel: "OSW_CHANNEL_FMT": scheduling expiry",
          phy->phy_name,
          OSW_CHANNEL_ARG(&phy->last_channel));
}

static void
ow_ovsdb_phy_last_channel_set(struct ow_ovsdb_phy *phy,
                              const struct osw_channel *c)
{
    if (osw_timer_is_armed(&phy->last_channel_expiry)) {
        LOGD("ow: ovsdb: phy: %s: last_channel: "OSW_CHANNEL_FMT": disarming",
              phy->phy_name,
              OSW_CHANNEL_ARG(&phy->last_channel));
        osw_timer_disarm(&phy->last_channel_expiry);
    }
    struct osw_channel zero;
    MEMZERO(zero);
    if (c == NULL) c = &zero;
    if (memcmp(c, &phy->last_channel, sizeof(*c)) == 0) return;
    LOGD("ow: ovsdb: phy: %s: last_channel: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
          phy->phy_name,
          OSW_CHANNEL_ARG(&phy->last_channel),
          OSW_CHANNEL_ARG(c));
    phy->last_channel = *c;
}

static void
ow_ovsdb_phy_last_channel_fix(struct ow_ovsdb_phy *phy,
                              struct osw_channel *c)
{
    if (c->control_freq_mhz > 0) {
        ow_ovsdb_phy_last_channel_set(phy, c);
        return;
    }

    if (phy->last_channel.control_freq_mhz == 0) {
        return;
    }

    /* This attempts at maintaining a non-empty channel
     * in Wifi_Radio_State for some time hoping that the
     * emptiness is a brief/transient state that'll
     * converge on a new channel soon enough.
     *
     * This offloads the controller from needing to
     * state track and debounce events, having the
     * device do that instead.
     */
    *c = phy->last_channel;
    LOGD("ow: ovsdb: phy: %s: last_channel: "OSW_CHANNEL_FMT": temporarily overriding",
            phy->phy_name,
            OSW_CHANNEL_ARG(c));
    ow_ovsdb_phy_last_channel_expire_sched(phy);
}

static void
ow_ovsdb_phystate_fill_channel(struct ow_ovsdb_phy *owo_phy,
                               struct schema_Wifi_Radio_State *schema,
                               const struct osw_state_phy_info *phy)
{
    struct osw_channel channel = {0};
    osw_state_vif_get_list(ow_ovsdb_phystate_get_channel_iter_cb,
                           phy->phy_name,
                           &channel);
    ow_ovsdb_phy_last_channel_fix(owo_phy, &channel);
    if (channel.control_freq_mhz > 0) {
        int num = ow_ovsdb_freq_to_chan(channel.control_freq_mhz);
        if (num > 0) SCHEMA_SET_INT(schema->channel, num);

        const char *ht_mode = ow_ovsdb_width_to_htmode(channel.width);
        if (ht_mode != NULL) SCHEMA_SET_STR(schema->ht_mode, ht_mode);

        if (channel.center_freq0_mhz != 0) {
            const int c = osw_freq_to_chan(channel.center_freq0_mhz);
            SCHEMA_SET_INT(schema->center_freq0_chan, c);
        }

        if (phy->drv_state->puncture_supported) {
            SCHEMA_SET_INT(schema->puncture_bitmap,
                           channel.puncture_bitmap);
        }
    }
}

static void
ow_ovsdb_phystate_get_tx_power_dbm_iter_cb(const struct osw_state_vif_info *info,
                                           void *priv)
{
    int *tx_power_dbm = priv;
    const bool vifs_with_mismatched_power = (*tx_power_dbm == -1);
    if (vifs_with_mismatched_power) return;

    const int vif_power_dbm = info->drv_state->tx_power_dbm;
    if (info->drv_state->tx_power_dbm == 0) return;

    if (*tx_power_dbm == -1) {
        /* unreachable */
    }
    else if (*tx_power_dbm == 0) {
        *tx_power_dbm = vif_power_dbm;
    }
    else if (*tx_power_dbm != vif_power_dbm) {
        *tx_power_dbm = -1;
    }
    else {
        *tx_power_dbm = vif_power_dbm;
    }
}

static void
ow_ovsdb_phystate_fill_tx_power(struct schema_Wifi_Radio_State *schema,
                                const struct osw_state_phy_info *phy)
{
    int tx_power_dbm = 0;
    osw_state_vif_get_list(ow_ovsdb_phystate_get_tx_power_dbm_iter_cb,
                           phy->phy_name,
                           &tx_power_dbm);

    if (tx_power_dbm <= 0) return;
    SCHEMA_SET_INT(schema->tx_power, tx_power_dbm);
}

static void
ow_ovsdb_phystate_get_mode_iter_cb(const struct osw_state_vif_info *info,
                                   void *priv)
{
    const struct osw_ap_mode *i = &info->drv_state->u.ap.mode;
    struct osw_ap_mode *m = priv;

    if (info->drv_state->status != OSW_VIF_ENABLED) return;
    if (info->drv_state->vif_type != OSW_VIF_AP) return;

    m->ht_enabled |= i->ht_enabled;
    m->vht_enabled |= i->vht_enabled;
    m->he_enabled |= i->he_enabled;
    m->eht_enabled |= i->eht_enabled;
}

static void
ow_ovsdb_phystate_fill_hwmode(struct schema_Wifi_Radio_State *schema,
                              const char *freq_band,
                              const struct osw_state_phy_info *phy)
{
    struct osw_ap_mode mode;
    MEMZERO(mode);

    osw_state_vif_get_list(ow_ovsdb_phystate_get_mode_iter_cb,
                           phy->phy_name,
                           &mode);

    if (mode.eht_enabled)
        SCHEMA_SET_STR(schema->hw_mode, "11be");
    else if (mode.he_enabled)
        SCHEMA_SET_STR(schema->hw_mode, "11ax");
    else if (mode.vht_enabled)
        SCHEMA_SET_STR(schema->hw_mode, "11ac");
    else if (mode.ht_enabled)
        SCHEMA_SET_STR(schema->hw_mode, "11n");
    else if (freq_band != NULL) {
        if (strcmp(freq_band, "6G") == 0 ||
                strcmp(freq_band, "5G") == 0 ||
                strcmp(freq_band, "5GL") == 0 ||
                strcmp(freq_band, "5GU") == 0)
            SCHEMA_SET_STR(schema->hw_mode, "11a");
        else if (strcmp(freq_band, "2.4G") == 0)
            SCHEMA_SET_STR(schema->hw_mode, "11g");
    }

    /* FIXME: 11b would need to rely on checking if basic rateset has been
     * limited. Although that's per-BSS, not per-PHY in practice, so flattening
     * this out here is another ugly thing to do. This is not that important
     * so..
     */
}

static void
ow_ovsdb_phystate_fill_allowed_channels(struct schema_Wifi_Radio_State *schema,
                                        const struct osw_state_phy_info *phy)
{
    const struct osw_channel_state *arr = phy->drv_state->channel_states;
    size_t n = phy->drv_state->n_channel_states;
    size_t i;

    if (arr == NULL) return;

    for (i = 0; i < n; i++) {
        const struct osw_channel *c = &arr[i].channel;
        int num = ow_ovsdb_freq_to_chan(c->control_freq_mhz);
        if (num > 0) SCHEMA_VAL_APPEND_INT(schema->allowed_channels, num);
    }
}

static const char *
ow_ovsdb_chan_dfs_state(const enum osw_channel_state_dfs s)
{
    switch (s) {
        case OSW_CHANNEL_NON_DFS: return "{\"state\": \"allowed\"}";
        case OSW_CHANNEL_DFS_CAC_POSSIBLE: return "{\"state\": \"nop_finished\"}";
        case OSW_CHANNEL_DFS_CAC_IN_PROGRESS: return "{\"state\": \"cac_started\"}";
        case OSW_CHANNEL_DFS_CAC_COMPLETED: return "{\"state\": \"cac_completed\"}";
        case OSW_CHANNEL_DFS_NOL: return "{\"state\": \"nop_started\"}";
    }
    LOGW("%s: unknown state: %d", __func__, s);
    return NULL;
}

static void
ow_ovsdb_phystate_fill_channels(struct schema_Wifi_Radio_State *schema,
                                const struct osw_state_phy_info *phy)
{
    const struct osw_channel_state *arr = phy->drv_state->channel_states;
    size_t n = phy->drv_state->n_channel_states;
    size_t i;

    if (arr == NULL) return;

    for (i = 0; i < n; i++) {
        const struct osw_channel_state *cs = &arr[i];
        const struct osw_channel *c = &cs->channel;
        int num = ow_ovsdb_freq_to_chan(c->control_freq_mhz);
        char k[32];
        const char *v = ow_ovsdb_chan_dfs_state(cs->dfs_state);
        snprintf(k, sizeof(k), "%d", num);
        if (num > 0) SCHEMA_KEY_VAL_APPEND(schema->channels, k, v);
    }
}

static const char *
ow_ovsdb_phystate_get_freq_band(const struct osw_state_phy_info *phy)
{
    bool has_2g = false;
    bool has_5gu = false;
    bool has_5gl = false;
    bool has_5g = false;
    bool has_6g = false;
    unsigned int bands = 0;
    const struct osw_channel_state *arr = phy->drv_state->channel_states;
    size_t n = phy->drv_state->n_channel_states;
    size_t i;

    for (i = 0; i < n; i++) {
        const struct osw_channel *c = &arr[i].channel;
        const int freq = c->control_freq_mhz;
        const int num = ow_ovsdb_freq_to_chan(freq);
        if (freq < 5935) {
            if (num >= 1 && num <= 14) has_2g = true;
            if (num >= 36 && num <= 64) has_5gl = true;
            if (num >= 100) has_5gu = true;
        } else {
            has_6g = true;
        }
    }

    has_5g = has_5gl || has_5gu;
    bands = has_2g + has_5g + has_6g;

    if (WARN_ON(bands != 1)) return NULL;
    if (has_6g) return "6G";
    if (has_2g) return "2.4G";
    if (has_5gl && has_5gu) return "5G";
    if (has_5gl) return "5GL";
    if (has_5gu) return "5GU";

    return NULL;
}

static void
ow_ovsdb_phystate_fix_no_vifs(struct schema_Wifi_Radio_State *rstate,
                              const struct schema_Wifi_Radio_Config *rconf)
{
    if (rconf == NULL) return;
    if (rconf->vif_configs_len > 0) return;

    LOGI("ow: ovsdb: %s: fixing up rstate due to no vifs", rconf->if_name);
    SCHEMA_CPY_INT(rstate->channel, rconf->channel);
    SCHEMA_CPY_STR(rstate->ht_mode, rconf->ht_mode);
    SCHEMA_CPY_STR(rstate->hw_mode, rconf->hw_mode);
}

static void
ow_ovsdb_phystate_fill_regulatory(struct schema_Wifi_Radio_State *schema,
                                  const struct schema_Wifi_Radio_Config *rconf,
                                  const struct osw_state_phy_info *phy)
{
    const char *cc = phy->drv_state->reg_domain.ccode;
    int num = phy->drv_state->reg_domain.iso3166_num;
    const int rev = phy->drv_state->reg_domain.revision;
    const struct iso3166_entry *by_alpha2 = iso3166_lookup_by_alpha2(cc);
    const struct iso3166_entry *by_num = iso3166_lookup_by_num(num);

    LOGT("ow: ovsdb: %s: regulatory: cc='%3s' num=%d rev=%d alpha2p=%p nump=%p",
         phy->phy_name, cc, num, rev, by_alpha2, by_num);

    if (by_alpha2 == NULL) {
        if (by_num != NULL) {
            LOGD("ow: ovsdb: %s: iso3166: inferring alpha2 from num: %d -> '%s'",
                 phy->phy_name,
                 by_num->num,
                 by_num->alpha2);
            cc = by_num->alpha2;
        }
    }
    else if (by_num == NULL) {
        if (by_alpha2 != NULL) {
            LOGD("ow: ovsdb: %s: iso3166: inferring num from alpha2: '%s' -> %d",
                 phy->phy_name,
                 by_alpha2->alpha2,
                 by_alpha2->num);
            num = by_alpha2->num;
        }
    }

    SCHEMA_SET_STR(schema->country, cc);

    const char *hw_type = (rconf ? rconf->hw_type : "");
    const bool is_qca_chip = (strstr(hw_type, "qca") == hw_type)
                          || (strstr(hw_type, "qcn") == hw_type);

    if (is_qca_chip) {
        char num_str[32];
        snprintf(num_str, sizeof(num_str), "%u", num);

        char rev_str[32];
        snprintf(rev_str, sizeof(rev_str), "%d", rev);

        SCHEMA_KEY_VAL_APPEND(schema->hw_params, "country_id", num_str);
        SCHEMA_KEY_VAL_APPEND(schema->hw_params, "reg_domain", rev_str);
    }
}

#define OW_OVSDB_INVALID_RATE 15
#define OW_OVSDB_INVALID_RATE_BIT (1 << OW_OVSDB_INVALID_RATE)

static void
ow_ovsdb_phystate_get_rates_iter_cb(const struct osw_state_vif_info *info,
                                    void *priv)
{
    if (info->drv_state->status != OSW_VIF_ENABLED) return;
    if (info->drv_state->vif_type != OSW_VIF_AP) return;

    const struct osw_ap_mode *ap_mode = &info->drv_state->u.ap.mode;
    struct osw_ap_mode *phy_mode = priv;

    if (phy_mode->supported_rates == 0) phy_mode->supported_rates = ap_mode->supported_rates;
    if (phy_mode->basic_rates == 0) phy_mode->basic_rates = ap_mode->basic_rates;
    if (phy_mode->beacon_rate.type == OSW_BEACON_RATE_UNSPEC) phy_mode->beacon_rate = ap_mode->beacon_rate;
    if (phy_mode->mcast_rate == OSW_RATE_UNSPEC) phy_mode->mcast_rate = ap_mode->mcast_rate;
    if (phy_mode->mgmt_rate == OSW_RATE_UNSPEC) phy_mode->mgmt_rate = ap_mode->mgmt_rate;

    if (phy_mode->supported_rates != ap_mode->supported_rates) {
        phy_mode->supported_rates |= OW_OVSDB_INVALID_RATE_BIT;
    }

    if (phy_mode->basic_rates != ap_mode->basic_rates) {
        phy_mode->basic_rates = OW_OVSDB_INVALID_RATE_BIT;
    }

    if (memcmp(&phy_mode->beacon_rate, &ap_mode->beacon_rate, sizeof(phy_mode->beacon_rate)) != 0) {
        phy_mode->beacon_rate.type = OW_OVSDB_INVALID_RATE;
    }

    if (phy_mode->mcast_rate != ap_mode->mcast_rate) {
        phy_mode->mcast_rate = OW_OVSDB_INVALID_RATE;
    }

    if (phy_mode->mgmt_rate != ap_mode->mgmt_rate) {
        phy_mode->mgmt_rate = OW_OVSDB_INVALID_RATE;
    }
}

static const char *
ow_ovsdb_enum2rate(enum osw_rate_legacy rate)
{
    switch (rate) {
        case OSW_RATE_CCK_1_MBPS: return "1";
        case OSW_RATE_CCK_2_MBPS: return "2";
        case OSW_RATE_CCK_5_5_MBPS: return "5.5";
        case OSW_RATE_CCK_11_MBPS: return "11";

        case OSW_RATE_OFDM_6_MBPS: return "6";
        case OSW_RATE_OFDM_9_MBPS: return "9";
        case OSW_RATE_OFDM_12_MBPS: return "12";
        case OSW_RATE_OFDM_18_MBPS: return "18";
        case OSW_RATE_OFDM_24_MBPS: return "24";
        case OSW_RATE_OFDM_36_MBPS: return "36";
        case OSW_RATE_OFDM_48_MBPS: return "48";
        case OSW_RATE_OFDM_54_MBPS: return "54";

        case OSW_RATE_UNSPEC: break;
        case OSW_RATE_COUNT: break;
    }
    return NULL;
}

static enum osw_rate_legacy
ow_ovsdb_rate2enum(const char *rate)
{
    return strcmp(rate, "1") == 0 ? OSW_RATE_CCK_1_MBPS :
           strcmp(rate, "2") == 0 ? OSW_RATE_CCK_2_MBPS :
           strcmp(rate, "5.5") == 0 ? OSW_RATE_CCK_5_5_MBPS :
           strcmp(rate, "11") == 0 ? OSW_RATE_CCK_11_MBPS :

           strcmp(rate, "6") == 0 ? OSW_RATE_OFDM_6_MBPS :
           strcmp(rate, "9") == 0 ? OSW_RATE_OFDM_9_MBPS :
           strcmp(rate, "12") == 0 ? OSW_RATE_OFDM_12_MBPS :
           strcmp(rate, "18") == 0 ? OSW_RATE_OFDM_18_MBPS :
           strcmp(rate, "24") == 0 ? OSW_RATE_OFDM_24_MBPS :
           strcmp(rate, "36") == 0 ? OSW_RATE_OFDM_36_MBPS :
           strcmp(rate, "48") == 0 ? OSW_RATE_OFDM_48_MBPS :
           strcmp(rate, "54") == 0 ? OSW_RATE_OFDM_54_MBPS :

           OSW_RATE_UNSPEC;
}

static void
ow_ovsdb_phystate_fill_rates(struct schema_Wifi_Radio_State *schema,
                            const struct osw_state_phy_info *phy)
{
    struct osw_ap_mode mode = {0};
    const char *phy_name = phy->phy_name;

    osw_state_vif_get_list(ow_ovsdb_phystate_get_rates_iter_cb,
                           phy_name,
                           &mode);

    if (mode.supported_rates & OW_OVSDB_INVALID_RATE_BIT) {
        mode.supported_rates = 0;
    }

    if (mode.basic_rates & OW_OVSDB_INVALID_RATE_BIT) {
        mode.basic_rates = 0;
    }

    size_t i;
    for (i = 0; i < OSW_RATE_COUNT; i++) {
        if (mode.supported_rates & osw_rate_legacy_bit(i)) {
            const char *str = ow_ovsdb_enum2rate(i);
            if (WARN_ON(str == NULL)) continue;
            SCHEMA_VAL_APPEND(schema->supported_rates, str);
        }

        if (mode.basic_rates & osw_rate_legacy_bit(i)) {
            const char *str = ow_ovsdb_enum2rate(i);
            if (WARN_ON(str == NULL)) continue;
            SCHEMA_VAL_APPEND(schema->basic_rates, str);
        }
    }

    if (mode.beacon_rate.type != OW_OVSDB_INVALID_RATE) {
        switch (mode.beacon_rate.type) {
            case OSW_BEACON_RATE_UNSPEC:
                break;
            case OSW_BEACON_RATE_ABG:
                {
                    const char *str = ow_ovsdb_enum2rate(mode.beacon_rate.u.legacy);
                    WARN_ON(str == NULL);
                    if (str != NULL) {
                        SCHEMA_SET_STR(schema->beacon_rate, str);
                    }
                }
                break;
            case OSW_BEACON_RATE_HT:
                break;
            case OSW_BEACON_RATE_VHT:
                break;
            case OSW_BEACON_RATE_HE:
                break;
        }
    }

    if (mode.mcast_rate != OW_OVSDB_INVALID_RATE) {
        const char *str = ow_ovsdb_enum2rate(mode.mcast_rate);
        if (str != NULL) {
            SCHEMA_SET_STR(schema->mcast_rate, str);
        }
    }

    if (mode.mgmt_rate != OW_OVSDB_INVALID_RATE) {
        const char *str = ow_ovsdb_enum2rate(mode.mgmt_rate);
        if (str != NULL) {
            SCHEMA_SET_STR(schema->mgmt_rate, str);
        }
    }
}

static void
ow_ovsdb_phystate_to_schema(struct ow_ovsdb_phy *owo_phy,
                            struct schema_Wifi_Radio_State *schema,
                            const struct schema_Wifi_Radio_Config *rconf,
                            const struct osw_state_phy_info *phy)
{
    const char *freq_band = ow_ovsdb_phystate_get_freq_band(phy);
    char mac_str[18];

    SCHEMA_SET_STR(schema->freq_band, "2.4G");

    if (freq_band != NULL)
        SCHEMA_SET_STR(schema->freq_band, freq_band);

    /* FIXME: Some parameters don't really are driver-driven
     * states, but will be handled internally in OW, eg.
     * fallback_parents, so there can be just copied.
     * However some arguably should be derived from the
     * state, eg. freq_band.
     */
    if (rconf != NULL) {
        int i;

        if (rconf->_uuid_exists == true)
            SCHEMA_SET_UUID(schema->radio_config, rconf->_uuid.uuid);

        SCHEMA_CPY_STR(schema->channel_mode, rconf->channel_mode);
        SCHEMA_CPY_STR(schema->hw_type, rconf->hw_type);
        SCHEMA_CPY_STR(schema->zero_wait_dfs, rconf->zero_wait_dfs);

        for (i = 0; i < rconf->hw_config_len; i++) {
            SCHEMA_KEY_VAL_APPEND(schema->hw_config,
                                  rconf->hw_config_keys[i],
                                  rconf->hw_config[i]);
        }

        for (i = 0; i < rconf->fallback_parents_len; i++) {
            SCHEMA_KEY_VAL_APPEND_INT(schema->fallback_parents,
                                      rconf->fallback_parents_keys[i],
                                      rconf->fallback_parents[i]);
        }

        SCHEMA_CPY_INT(schema->thermal_tx_chainmask, rconf->thermal_tx_chainmask);
    }

    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             phy->drv_state->mac_addr.octet[0],
             phy->drv_state->mac_addr.octet[1],
             phy->drv_state->mac_addr.octet[2],
             phy->drv_state->mac_addr.octet[3],
             phy->drv_state->mac_addr.octet[4],
             phy->drv_state->mac_addr.octet[5]);

    SCHEMA_SET_STR(schema->if_name, phy->phy_name);
    SCHEMA_SET_STR(schema->mac, mac_str);
    SCHEMA_SET_INT(schema->tx_chainmask, phy->drv_state->tx_chainmask);
    SCHEMA_SET_BOOL(schema->enabled, phy->drv_state->enabled);
    ow_ovsdb_phystate_fill_bcn_int(schema, phy);
    ow_ovsdb_phystate_fill_channel(owo_phy, schema, phy);
    ow_ovsdb_phystate_fill_tx_power(schema, phy);
    ow_ovsdb_phystate_fill_hwmode(schema, freq_band, phy);
    ow_ovsdb_phystate_fill_allowed_channels(schema, phy);
    ow_ovsdb_phystate_fill_channels(schema, phy);
    ow_ovsdb_phystate_fill_regulatory(schema, rconf, phy);
    ow_ovsdb_phystate_fill_rates(schema, phy);
    ow_ovsdb_phystate_fix_no_vifs(schema, rconf);

    // FIXME: channels[], needed for DFS
    // FIXME: tx_power
    // FIXME: radar[] ?

    switch (phy->drv_state->radar) {
        case OSW_RADAR_UNSUPPORTED:
            SCHEMA_SET_BOOL(schema->dfs_demo, false);
            break;
        case OSW_RADAR_DETECT_ENABLED:
            SCHEMA_SET_BOOL(schema->dfs_demo, false);
            break;
        case OSW_RADAR_DETECT_DISABLED:
            SCHEMA_SET_BOOL(schema->dfs_demo, true);
            break;
    }
}

static void
ow_ovsdb_vifstate_fix_up(struct schema_Wifi_VIF_State *schema)
{
    struct schema_Wifi_VIF_Config *vconf;
    vconf = ovsdb_cache_find_by_key(&table_Wifi_VIF_Config, schema->if_name);
    if (vconf == NULL) return;

    SCHEMA_SET_UUID(schema->vif_config, vconf->_uuid.uuid);
    if (vconf->vif_radio_idx_exists == true)
        SCHEMA_SET_INT(schema->vif_radio_idx, vconf->vif_radio_idx);

    schema_vstate_unify(schema);
    schema_vstate_sync_to_vconf(schema, vconf);
}

static enum osw_vif_type
ow_ovsdb_mode_to_type(const char *mode)
{
    if (strcmp(mode, "ap") == 0) return OSW_VIF_AP;
    if (strcmp(mode, "ap_vlan") == 0) return OSW_VIF_AP_VLAN;
    if (strcmp(mode, "sta") == 0) return OSW_VIF_STA;
    return OSW_VIF_UNDEFINED;
}

static const char *
pmf_to_str(enum osw_pmf pmf)
{
    switch (pmf) {
        case OSW_PMF_DISABLED: return SCHEMA_CONSTS_SECURITY_PMF_DISABLED;
        case OSW_PMF_OPTIONAL: return SCHEMA_CONSTS_SECURITY_PMF_OPTIONAL;
        case OSW_PMF_REQUIRED: return SCHEMA_CONSTS_SECURITY_PMF_REQUIRED;
    }
    return SCHEMA_CONSTS_SECURITY_PMF_DISABLED;
}

static enum osw_pmf
pmf_from_str(const char *str)
{
    if (str == NULL) return OSW_PMF_DISABLED;
    if (strcmp(str, SCHEMA_CONSTS_SECURITY_PMF_DISABLED) == 0) return OSW_PMF_DISABLED;
    if (strcmp(str, SCHEMA_CONSTS_SECURITY_PMF_OPTIONAL) == 0) return OSW_PMF_OPTIONAL;
    if (strcmp(str, SCHEMA_CONSTS_SECURITY_PMF_REQUIRED) == 0) return OSW_PMF_REQUIRED;
    return OSW_PMF_DISABLED;
}

static void
ow_ovsdb_vifstate_fill_akm(struct schema_Wifi_VIF_State *schema,
                            const struct osw_wpa *wpa)
{
    /* FIXME: This isn't totally accurate, but good enough for now. */

    if (wpa->akm_eap)            SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP);
    if (wpa->akm_eap_sha256)     SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP_SHA256);
    if (wpa->akm_eap_sha384)     SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP_SHA384);
    if (wpa->akm_eap_suite_b)    SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP_B);
    if (wpa->akm_eap_suite_b192) SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP_B_192);
    if (wpa->akm_ft_eap)         SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_EAP);
    if (wpa->akm_ft_eap_sha384)  SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_EAP_SHA384);
    if (wpa->akm_ft_psk)         SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_PSK);
    if (wpa->akm_ft_sae)         SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_SAE);
    if (wpa->akm_ft_sae_ext)     SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_SAE_EXT);
    if (wpa->akm_psk)            SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK);
    if (wpa->akm_psk_sha256)     SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK_SHA256);
    if (wpa->akm_sae)            SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_SAE);
    if (wpa->akm_sae_ext)        SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_SAE_EXT);

    SCHEMA_SET_BOOL(schema->wpa_pairwise_tkip, wpa->wpa && wpa->pairwise_tkip);
    SCHEMA_SET_BOOL(schema->wpa_pairwise_ccmp, wpa->wpa && wpa->pairwise_ccmp);
    SCHEMA_SET_BOOL(schema->rsn_pairwise_tkip, wpa->rsn && wpa->pairwise_tkip);
    SCHEMA_SET_BOOL(schema->rsn_pairwise_ccmp, wpa->rsn && wpa->pairwise_ccmp);
    SCHEMA_SET_BOOL(schema->rsn_pairwise_ccmp256, wpa->rsn && wpa->pairwise_ccmp256);
    SCHEMA_SET_BOOL(schema->rsn_pairwise_gcmp, wpa->rsn && wpa->pairwise_gcmp);
    SCHEMA_SET_BOOL(schema->rsn_pairwise_gcmp256, wpa->rsn && wpa->pairwise_gcmp256);
    SCHEMA_SET_STR(schema->pmf, pmf_to_str(wpa->pmf));
    SCHEMA_SET_BOOL(schema->beacon_protection, wpa->beacon_protection);
}

static void
ow_ovsdb_vifstate_fill_akm_sta(struct schema_Wifi_VIF_State *schema,
                               const struct osw_drv_vif_state_sta *vsta)
{
    /* The report is expected to match the _configured_
     * parameters, not the resulting parameters.  Station
     * link can, and typically will be, a subset of the
     * allowed network configuration. Eg. WPA3-T allows
     * either PSK or SAE, and depends on the parent AP
     * capabilities.
     */

    const struct osw_drv_vif_sta_network *network = osw_drv_vif_sta_network_get(vsta);
    if (network) {
        const struct osw_wpa *wpa = &network->wpa;
        if (wpa->akm_eap)            SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP);
        if (wpa->akm_eap_sha256)     SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP_SHA256);
        if (wpa->akm_eap_sha384)     SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP_SHA384);
        if (wpa->akm_eap_suite_b)    SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP_B);
        if (wpa->akm_eap_suite_b192) SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP_B_192);
        if (wpa->akm_ft_eap)         SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_EAP);
        if (wpa->akm_ft_eap_sha384)  SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_EAP_SHA384);
        if (wpa->akm_ft_psk)         SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_PSK);
        if (wpa->akm_ft_sae)         SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_SAE);
        if (wpa->akm_ft_sae_ext)     SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_SAE_EXT);
        if (wpa->akm_psk)            SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK);
        if (wpa->akm_psk_sha256)     SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK_SHA256);
        if (wpa->akm_sae)            SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_SAE);
        if (wpa->akm_sae_ext)        SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_SAE_EXT);

        SCHEMA_SET_BOOL(schema->wpa_pairwise_tkip, wpa->wpa && wpa->pairwise_tkip);
        SCHEMA_SET_BOOL(schema->wpa_pairwise_ccmp, wpa->wpa && wpa->pairwise_ccmp);
        SCHEMA_SET_BOOL(schema->rsn_pairwise_tkip, wpa->rsn && wpa->pairwise_tkip);
        SCHEMA_SET_BOOL(schema->rsn_pairwise_ccmp, wpa->rsn && wpa->pairwise_ccmp);
        SCHEMA_SET_BOOL(schema->rsn_pairwise_ccmp256, wpa->rsn && wpa->pairwise_ccmp256);
        SCHEMA_SET_BOOL(schema->rsn_pairwise_gcmp, wpa->rsn && wpa->pairwise_gcmp);
        SCHEMA_SET_BOOL(schema->rsn_pairwise_gcmp256, wpa->rsn && wpa->pairwise_gcmp256);

        SCHEMA_SET_STR(schema->pmf, pmf_to_str(wpa->pmf));
        SCHEMA_SET_BOOL(schema->beacon_protection, wpa->beacon_protection);
    }
}

static int
ow_ovsdb_str2keyid(const char *key_id)
{
    int i;
    if (strcmp(key_id, "key") == 0)
        return 0;
    if (sscanf(key_id, "key-%d", &i) == 1)
        return i;
    return -1;
}

static void
ow_ovsdb_keyid2str(char *buf, const size_t len, const int key_id)
{
    if (key_id == 0)
        snprintf(buf, len, "key");
    else
        snprintf(buf, len, "key-%d", key_id);
}

static void
ow_ovsdb_vifstate_fill_ap_psk(struct schema_Wifi_VIF_State *schema,
                              const struct osw_ap_psk_list *psks)
{
    size_t i;
    for (i = 0; i < psks->count; i++) {
        const struct osw_ap_psk *psk = &psks->list[i];
        char key_id[64];

        if (WARN_ON(i >= ARRAY_SIZE(schema->wpa_psks)))
            break;

        ow_ovsdb_keyid2str(key_id, sizeof(key_id), psk->key_id);
        SCHEMA_KEY_VAL_APPEND(schema->wpa_psks, key_id, psk->psk.str);
    }
}

static bool
ow_ovsdb_vconf_maclist_has_acl(const struct schema_Wifi_VIF_Config *vconf,
                               const char *mac)
{
    int i;
    for (i = 0; vconf != NULL && i < vconf->mac_list_len; i++) {
        const bool match = (strcasecmp(vconf->mac_list[i], mac) == 0);
        if (match) return true;
    }
    return false;
}

static void
ow_ovsdb_vifstate_fill_ap_acl(struct schema_Wifi_VIF_State *schema,
                              const struct schema_Wifi_VIF_Config *vconf,
                              const struct osw_hwaddr_list *acl)
{
    size_t i;
    for (i = 0; i < acl->count; i++) {
        struct osw_hwaddr_str buf;
        const char *str = osw_hwaddr2str(&acl->list[i], &buf);

        /* ACLs can be dynamic and come from other parts of
         * the system, eg. steering. These must not be
         * included in the state report because the
         * configuring entity may want to know if
         * Wifi_VIF_Config == Wifi_VIF_State.
         *
         * This performs an intersection between mac_list in
         * Wifi_VIF_Config and what the OSW drivers are
         * reporting.
         */
        if (ow_ovsdb_vconf_maclist_has_acl(vconf, str) == false)
            continue;

        if (WARN_ON(i >= ARRAY_SIZE(schema->mac_list)))
            break;

        SCHEMA_VAL_APPEND(schema->mac_list, str);
    }
}

static bool
ow_ovsdb_radius_is_equal(const struct schema_RADIUS *a,
                         const struct osw_radius *b)
{
    if (a->ip_addr_exists == true &&
        a->secret_exists == true &&
        a->port_exists == true &&
        strcmp(a->ip_addr, b->server) == 0 &&
        strcmp(a->secret, b->passphrase) == 0 &&
        a->port == b->port)
        return true;
    return false;
}

void
ow_ovsdb_vifstate_fill_ap_radius(struct schema_Wifi_VIF_State *schema,
                                 const struct osw_radius_list *radlist,
                                 const struct osw_radius_list *acctlist)
{
    size_t i;
    ovsdb_cache_row_t *rrow;

    for (i = 0; i < radlist->count; i++) {
        const struct osw_radius *rad = &radlist->list[i];
        ds_tree_foreach(&table_RADIUS.rows, rrow) {
            const struct schema_RADIUS *radius = (const void*)rrow->record;
            if (ow_ovsdb_radius_is_equal(radius, rad) == true) {
                if (i == 0) {
                    SCHEMA_SET_UUID(schema->primary_radius, radius->_uuid.uuid);
                } else {
                    SCHEMA_APPEND_UUID(schema->secondary_radius, radius->_uuid.uuid);
                }
            }
        }
    }

    for (i = 0; i < acctlist->count; i++) {
        const struct osw_radius *rad = &acctlist->list[i];
        ds_tree_foreach(&table_RADIUS.rows, rrow) {
            const struct schema_RADIUS *radius = (const void*)rrow->record;
            if (ow_ovsdb_radius_is_equal(radius, rad) == true) {
                if (i == 0) {
                    SCHEMA_SET_UUID(schema->primary_accounting, radius->_uuid.uuid);
                } else {
                    SCHEMA_APPEND_UUID(schema->secondary_accounting, radius->_uuid.uuid);
                }
            }
        }
    }
}

static bool
ow_ovsdb_min_hw_mode_is_not_supported(void)
{
    return osw_etc_get("OW_OVSDB_MIN_HW_MODE_NOT_SUPPORTED") != NULL;
}

static bool
ow_ovsdb_min_hw_mode_is_supported(void)
{
    return !ow_ovsdb_min_hw_mode_is_not_supported();
}

static void
ow_ovsdb_vifstate_fill_min_hw_mode(struct schema_Wifi_VIF_State *vstate,
                                   const struct schema_Wifi_VIF_Config *vconf,
                                   const struct osw_ap_mode *ap_mode,
                                   const enum osw_band band)
{
    SCHEMA_UNSET_FIELD(vstate->min_hw_mode);

    if (ow_ovsdb_min_hw_mode_is_supported()) {
        const enum ow_ovsdb_min_hw_mode min_hw_mode = ow_ovsdb_min_hw_mode_from_ap_mode(ap_mode, band);
        const char *str = ow_ovsdb_min_hw_mode_to_str(min_hw_mode);
        const bool valid = (strlen(str) > 0);

        if (valid) {
            SCHEMA_SET_STR(vstate->min_hw_mode, str);
        }
    }
    else if (vconf != NULL) {
        SCHEMA_CPY_STR(vstate->min_hw_mode, vconf->min_hw_mode);
    }
}

static void
ow_ovsdb_vifstate_fill_mld(struct schema_Wifi_VIF_State *schema,
                           const struct osw_drv_mld_state *mld)
{
    if (osw_hwaddr_is_zero(&mld->addr)) return;

    struct osw_hwaddr_str mld_str;
    osw_hwaddr2str(&mld->addr, &mld_str);
    SCHEMA_SET_STR(schema->mld_addr, mld_str.buf);

    if (strlen(mld->if_name.buf) > 0) {
        SCHEMA_SET_STR(schema->mld_if_name, mld->if_name.buf);
    }
}

static void
ow_ovsdb_vifstate_fill_ft_encr_key(struct schema_Wifi_VIF_State *schema,
                                   const struct osw_drv_vif_state_ap *ap)
{
    if (strlen(ap->ft_encr_key.buf) > 0) {
        SCHEMA_SET_STR(schema->ft_encr_key, ap->ft_encr_key.buf);
    }
}

static enum osw_drv_vif_state_sta_link_status
ow_ovsdb_vistate_get_link_status(struct ow_ovsdb *m,
                                 const char *vif_name,
                                 const struct osw_drv_vif_state_sta *vsta)
{
    switch (vsta->link.status) {
        case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN:
        case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING:
        case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED:
            break;
        case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED:
            {
                ow_mld_redir_t *redir = m->ow_mld_redir;
                const char *redir_vif_name = ow_mld_redir_get_mld_redir_vif_name(redir, vsta->mld.if_name.buf);
                const bool is_part_of_mld = (osw_hwaddr_is_zero(&vsta->mld.addr) == false)
                                         && (strlen(vsta->mld.if_name.buf) > 0);
                const bool is_link_non_mlo = osw_hwaddr_is_zero(&vsta->link.mld_addr);
                const bool is_redir_target = (redir_vif_name != NULL) && (strcmp(redir_vif_name, vif_name) == 0);
                const bool report_as_not_connected = (is_part_of_mld && is_link_non_mlo && !is_redir_target);
                if (report_as_not_connected) {
                    return OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED;
                }
            }
            break;
    }
    return vsta->link.status;
}

static void
ow_ovsdb_vifstate_to_schema(struct schema_Wifi_VIF_State *schema,
                            const struct schema_Wifi_VIF_Config *vconf,
                            const struct osw_state_vif_info *vif)
{
    struct ow_ovsdb *m = &g_ow_ovsdb;
    const struct osw_drv_vif_state_ap *ap = &vif->drv_state->u.ap;
    const struct osw_drv_vif_state_ap_vlan *ap_vlan = &vif->drv_state->u.ap_vlan;
    const struct osw_drv_vif_state_sta *vsta = &vif->drv_state->u.sta;
    struct osw_hwaddr_str mac;
    int c;

    SCHEMA_SET_STR(schema->if_name, vif->vif_name);
    switch (vif->drv_state->status) {
        case OSW_VIF_UNKNOWN:
        case OSW_VIF_BROKEN:
            schema->enabled_present = true;
            break;
        case OSW_VIF_ENABLED:
            SCHEMA_SET_BOOL(schema->enabled, true);
            break;
        case OSW_VIF_DISABLED:
            SCHEMA_SET_BOOL(schema->enabled, false);
            break;
    }
    SCHEMA_SET_STR(schema->mac, osw_hwaddr2str(&vif->drv_state->mac_addr, &mac));

    switch (vif->drv_state->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            SCHEMA_SET_STR(schema->mode, "ap");
            SCHEMA_SET_STR(schema->ssid, ap->ssid.buf);
            SCHEMA_SET_STR(schema->ssid_broadcast, ap->ssid_hidden ? "disabled" : "enabled");
            SCHEMA_SET_STR(schema->bridge, ap->bridge_if_name.buf);
            if (strlen(ap->nas_identifier.buf) > 1) {
                SCHEMA_SET_STR(schema->nas_identifier, ap->nas_identifier.buf);
            } else {
                SCHEMA_UNSET_FIELD(schema->nas_identifier);
            }
            SCHEMA_SET_INT(schema->ft_mobility_domain, ap->ft_mobility_domain);
            SCHEMA_SET_BOOL(schema->wpa, ap->wpa.wpa || ap->wpa.rsn);
            SCHEMA_SET_BOOL(schema->ap_bridge, ap->isolated ? false : true);
            SCHEMA_SET_BOOL(schema->btm, ap->mode.wnm_bss_trans);
            SCHEMA_SET_BOOL(schema->rrm, ap->mode.rrm_neighbor_report);
            SCHEMA_SET_BOOL(schema->wps, ap->mode.wps);
            SCHEMA_SET_BOOL(schema->uapsd_enable, ap->mode.wmm_uapsd_enabled);
            SCHEMA_SET_BOOL(schema->mcast2ucast, ap->mcast2ucast);
            SCHEMA_SET_BOOL(schema->dynamic_beacon, false); // FIXME
            SCHEMA_SET_STR(schema->multi_ap, ow_ovsdb_ap_multi_ap_to_cstr(&ap->multi_ap));
            SCHEMA_SET_BOOL(schema->ft_over_ds, ap->ft_over_ds);
            SCHEMA_SET_BOOL(schema->ft_pmk_r1_push, ap->ft_pmk_r1_push);
            SCHEMA_SET_BOOL(schema->ft_psk_generate_local, ap->ft_psk_generate_local);
            SCHEMA_SET_INT(schema->ft_pmk_r0_key_lifetime_sec, ap->ft_pmk_r0_key_lifetime_sec);
            SCHEMA_SET_INT(schema->ft_pmk_r1_max_key_lifetime_sec, ap->ft_pmk_r1_max_key_lifetime_sec);
            /* oce parameter is 'hidden' from OVSDB and for this reason
             * state of either one shall impact mbo in the state table */
            if (ap->mbo != ap->oce) {
                SCHEMA_UNSET_FIELD(schema->mbo);
            } else {
                SCHEMA_SET_BOOL(schema->mbo, ap->mbo);
            }
            if (ap->oce != true) {
                SCHEMA_UNSET_FIELD(schema->oce_min_rssi_dbm);
                SCHEMA_UNSET_FIELD(schema->oce_retry_delay_sec);
            } else {
                SCHEMA_SET_INT(schema->oce_min_rssi_dbm, ap->oce_min_rssi_dbm);
                SCHEMA_SET_INT(schema->oce_retry_delay_sec, ap->oce_retry_delay_sec);
            }
            SCHEMA_SET_INT(schema->max_sta, ap->max_sta);
            /* FIXME - passpoint is being read from config, not state in state report! */
            const char *passpoint_ref = ow_conf_vif_get_ap_passpoint_ref(vif->vif_name);
            if (passpoint_ref != NULL) SCHEMA_SET_UUID (schema->passpoint_config, passpoint_ref);

            {
                const enum osw_acl_policy acl_policy = ow_ovsdb_ap_get_acl_policy(ap, vconf);
                const char *acl_policy_str = ow_ovsdb_acl_policy_to_str(acl_policy);
                if (acl_policy_str != NULL)
                    SCHEMA_SET_STR(schema->mac_list_type, acl_policy_str);
            }

            if (ap->wpa.group_rekey_seconds != OSW_WPA_GROUP_REKEY_UNDEFINED)
                SCHEMA_SET_INT(schema->group_rekey, ap->wpa.group_rekey_seconds);

            c = ow_ovsdb_freq_to_chan(ap->channel.control_freq_mhz);
            if (c != 0)
                SCHEMA_SET_INT(schema->channel, c);

            ow_ovsdb_vifstate_fill_akm(schema, &ap->wpa);
            ow_ovsdb_vifstate_fill_ap_psk(schema, &ap->psk_list);
            ow_ovsdb_vifstate_fill_ap_acl(schema, vconf, &ap->acl);
            ow_ovsdb_vifstate_fill_ap_radius(schema, &ap->radius_list, &ap->acct_list);
            ow_ovsdb_wps_op_fill_vstate(g_ow_ovsdb.wps, schema);

            const uint32_t freq = ap->channel.control_freq_mhz;
            const enum osw_band band = osw_freq_to_band(freq);
            ow_ovsdb_vifstate_fill_min_hw_mode(schema, vconf, &ap->mode, band);
            ow_ovsdb_vifstate_fill_mld(schema, &ap->mld);
            ow_ovsdb_vifstate_fill_ft_encr_key(schema, ap);
            break;
        case OSW_VIF_AP_VLAN:
            SCHEMA_SET_STR(schema->mode, "ap_vlan");
            SCHEMA_SET_BOOL(schema->wds, true);
            if (ap_vlan->sta_addrs.count > 0) {
                WARN_ON(ap_vlan->sta_addrs.count > 1);
                const struct osw_hwaddr *first = &ap_vlan->sta_addrs.list[0];
                struct osw_hwaddr_str buf;
                const char *mac_str = osw_hwaddr2str(first, &buf);
                SCHEMA_SET_STR(schema->ap_vlan_sta_addr, mac_str);
            }
            break;
        case OSW_VIF_STA:
            SCHEMA_SET_STR(schema->mode, "sta");
            switch (ow_ovsdb_vistate_get_link_status(m, vif->vif_name, vsta)) {
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED:
                    SCHEMA_SET_STR(schema->ssid, vsta->link.ssid.buf);
                    if (osw_hwaddr_is_zero(&vsta->link.bssid) == false) {
                        SCHEMA_SET_STR(schema->parent, osw_hwaddr2str(&vsta->link.bssid, &mac));
                    }
                    SCHEMA_SET_BOOL(schema->wpa, vsta->link.wpa.wpa || vsta->link.wpa.rsn);
                    if (strlen(vsta->link.psk.str) > 0) {
                        SCHEMA_KEY_VAL_APPEND(schema->wpa_psks, "key", vsta->link.psk.str);
                    }
                    ow_ovsdb_vifstate_fill_akm_sta(schema, vsta);
                    ow_ovsdb_vifstate_fill_mld(schema, &vsta->mld);

                    SCHEMA_SET_STR(schema->bridge,vsta->link.bridge_if_name.buf);
                    SCHEMA_SET_STR(schema->multi_ap, ow_ovsdb_sta_multi_ap_to_cstr(vsta->link.multi_ap));

                    c = ow_ovsdb_freq_to_chan(vsta->link.channel.control_freq_mhz);
                    if (c != 0)
                        SCHEMA_SET_INT(schema->channel, c);

                    /* FIXME: something in the above is hard-required by controller; otherwise it removes valid entries.. */
                    SCHEMA_SET_BOOL(schema->ap_bridge, true);
                    SCHEMA_SET_BOOL(schema->dynamic_beacon, false);
                    SCHEMA_SET_BOOL(schema->mcast2ucast, false);
                    SCHEMA_SET_INT(schema->ft_psk, 0);
                    SCHEMA_SET_INT(schema->rrm, 0);
                    SCHEMA_SET_STR(schema->wps_pbc_key_id, "");
                    SCHEMA_SET_STR(schema->dpp_connector, "");
                    SCHEMA_SET_STR(schema->dpp_csign_hex, "");
                    SCHEMA_SET_STR(schema->dpp_netaccesskey_hex, "");
                    SCHEMA_SET_STR(schema->mac_list_type, "none");
                    SCHEMA_SET_STR(schema->ssid_broadcast, "enabled");
                    SCHEMA_SET_BOOL(schema->uapsd_enable, false);
                    SCHEMA_SET_BOOL(schema->wds, vsta->link.multi_ap);
                    break;
                case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN:
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING:
                case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED:
                    SCHEMA_UNSET_FIELD(schema->ssid);
                    SCHEMA_UNSET_FIELD(schema->parent);
                    ow_ovsdb_vifstate_fill_mld(schema, &vsta->mld);
                    break;
            }
            break;
    }

    ow_ovsdb_vifstate_fix_up(schema);
}

static void
ow_ovsdb_phystate_fix_vif_states(struct ow_ovsdb_phy *phy)
{
    struct schema_Wifi_Radio_State *rstate = &phy->state_new;
    struct ds_tree *tree = &phy->root->vif_tree;
    struct ow_ovsdb_vif *vif;
    const char *phy_name = phy->phy_name;

    ds_tree_foreach(tree, vif) {
        if (vif->phy_name == NULL) continue;
        if (strcmp(vif->phy_name, phy_name) != 0) continue;
        if (strlen(vif->state_cur._uuid.uuid) == 0) continue;
        if (WARN_ON((size_t)rstate->vif_states_len >= ARRAY_SIZE(rstate->vif_states))) continue;

        ovs_uuid_t *uuid = &rstate->vif_states[rstate->vif_states_len];
        rstate->vif_states_len++;
        *uuid = vif->state_cur._uuid;
    }
}

static void
ow_ovsdb_phy_gc(struct ow_ovsdb_phy *phy)
{
    struct ds_tree *tree = &phy->root->phy_tree;

    if (strlen(phy->config.if_name) > 0) return;
    if (strlen(phy->state_cur.if_name) > 0) return;
    if (phy->info != NULL) return;

    ow_ovsdb_phy_last_channel_set(phy, NULL);
    ds_tree_remove(tree, phy);
    FREE(phy->phy_name);
    FREE(phy);
}

static void
ow_ovsdb_phy_fill_schema_radar(struct ow_ovsdb_phy *phy)
{
    if (phy->radar_time == 0) return;

    const int freq_mhz = phy->radar_channel.control_freq_mhz;
    const int chan_num = osw_freq_to_chan(freq_mhz);
    char chan_num_str[32];
    snprintf(chan_num_str, sizeof(chan_num_str), "%d", chan_num);

    /* This is probably the most portable way to turn time_t into a string. */
    const double ts = difftime(phy->radar_time, (time_t)0);
    char ts_str[32];
    snprintf(ts_str, sizeof(ts_str), "%.0f", ts);

    SCHEMA_KEY_VAL_APPEND(phy->state_new.radar, "last_channel", chan_num_str);
    SCHEMA_KEY_VAL_APPEND(phy->state_new.radar, "num_detected", "1");
    SCHEMA_KEY_VAL_APPEND(phy->state_new.radar, "time", ts_str);
}

static void
ow_ovsdb_phy_fill_schema(struct ow_ovsdb_phy *phy)
{
    const struct schema_Wifi_Radio_Config *rconf = NULL;

    memset(&phy->state_new, 0, sizeof(phy->state_new));
    if (strlen(phy->state_cur._uuid.uuid) > 0)
        STRSCPY(phy->state_new._uuid.uuid, phy->state_cur._uuid.uuid);

    if (strlen(phy->config.if_name) > 0)
        rconf = &phy->config;

    ow_ovsdb_phystate_to_schema(phy, &phy->state_new, rconf, phy->info); // FIXME
    ow_ovsdb_phy_fill_schema_radar(phy);
    ow_ovsdb_phystate_fix_vif_states(phy);
}

static void /* forward decl */
ow_ovsdb_vif_work_sched(struct ow_ovsdb_vif *vif);

static void
ow_ovsdb_phy_sync_vifs(struct ow_ovsdb_phy *phy)
{
    struct ow_ovsdb *root = &g_ow_ovsdb;
    struct ow_ovsdb_vif *vif;

    ds_tree_foreach(&root->vif_tree, vif) {
        if (vif->phy_name == NULL)
            continue;
        if (strcmp(vif->phy_name, phy->phy_name) != 0)
            continue;
        ow_ovsdb_vif_work_sched(vif);
    }
}

static bool
ow_ovsdb_phy_sync(struct ow_ovsdb_phy *phy)
{
    if (ow_ovsdb_sync_allowed(phy->root, &phy->deadline) == false) return false;

    if (phy->info == NULL) {
        if (strlen(phy->state_cur.if_name) == 0) return true;

        LOGI("ow: ovsdb: phy: %s: deleting", phy->phy_name);

        ovsdb_table_delete(&table_Wifi_Radio_State, &phy->state_cur);
        memset(&phy->state_cur, 0, sizeof(phy->state_cur));
        return true;
    }
    else {
        ow_ovsdb_phy_fill_schema(phy);

        if (memcmp(&phy->state_cur, &phy->state_new, sizeof(phy->state_cur)) == 0)
            return true;

        LOGI("ow: ovsdb: phy: %s: upserting", phy->phy_name);

        bool ok = ovsdb_table_upsert(&table_Wifi_Radio_State, &phy->state_new, true);
        if (ok == false)
            return false;

        phy->state_cur = phy->state_new;
        ow_ovsdb_phy_sync_vifs(phy);
        return true;
    }
}

static void
ow_ovsdb_phy_work_cb(EV_P_ ev_timer *arg, int events)
{
    struct ow_ovsdb_phy *phy = arg->data;
    if (ow_ovsdb_phy_sync(phy) == false) return;
    ev_timer_stop(EV_A_ &phy->work);
    ev_timer_stop(EV_A_ &phy->deadline);
    ow_ovsdb_phy_gc(phy);
}

static void
ow_ovsdb_phy_init(struct ow_ovsdb *root,
                  struct ow_ovsdb_phy *phy,
                  const char *phy_name)
{
    phy->root = root;
    phy->phy_name = STRDUP(phy_name);
    ev_timer_init(&phy->work, ow_ovsdb_phy_work_cb, 0, 0);
    ev_timer_init(&phy->deadline, ow_ovsdb_phy_work_cb, 0, 0);
    phy->work.data = phy;
    phy->deadline.data = phy;
    osw_timer_init(&phy->last_channel_expiry, ow_ovsdb_phy_last_channel_expiry_cb);
    ds_tree_insert(&root->phy_tree, phy, phy->phy_name);
}

static struct ow_ovsdb_phy *
ow_ovsdb_phy_get(const char *phy_name)
{
    struct ow_ovsdb *root = &g_ow_ovsdb;
    struct ow_ovsdb_phy *phy = ds_tree_find(&root->phy_tree, phy_name);
    if (phy == NULL) {
        phy = CALLOC(1, sizeof(*phy));
        ow_ovsdb_phy_init(root, phy, phy_name);
    }
    return phy;
}

static void
ow_ovsdb_phy_set_info(const char *phy_name,
                      const struct osw_state_phy_info *info)
{
    struct ow_ovsdb_phy *phy = ow_ovsdb_phy_get(phy_name);
    phy->info = info;
    ow_ovsdb_phy_work_sched(phy);
}

static void
ow_ovsdb_phy_set_config(const char *phy_name,
                        const struct schema_Wifi_Radio_Config *config)
{
    struct ow_ovsdb_phy *phy = ow_ovsdb_phy_get(phy_name);

    memset(&phy->config, 0, sizeof(phy->config));
    if (config != NULL)
        phy->config = *config;

    ow_ovsdb_phy_work_sched(phy);
}

static void
ow_ovsdb_phy_set_radar_log(struct ow_ovsdb_phy *phy,
                           const struct osw_channel *channel)
{
    const char *phy_name = phy->phy_name;
    const time_t now = time(NULL);
    const size_t size = sizeof(*channel);
    const double time_delta = difftime(now, phy->radar_time);
    const bool diff_chan = (memcmp(channel, &phy->radar_channel, size) != 0);

    if (diff_chan) {
        LOGI("ow: ovsdb: phy: %s: radar report: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
             phy_name,
             OSW_CHANNEL_ARG(&phy->radar_channel),
             OSW_CHANNEL_ARG(channel));
    }
    else if (time_delta > 2) {
        LOGI("ow: ovsdb: phy: %s: radar report: "OSW_CHANNEL_FMT" again after %.0f seconds",
             phy_name,
             OSW_CHANNEL_ARG(channel),
             time_delta);
    }
    else {
        LOGD("ow: ovsdb: phy: %s: radar report: "OSW_CHANNEL_FMT" again after %.0f seconds, probably duplicate",
             phy_name,
             OSW_CHANNEL_ARG(channel),
             time_delta);
    }
}

static void
ow_ovsdb_phy_set_radar(const char *phy_name,
                       const struct osw_channel *channel)
{
    struct ow_ovsdb_phy *phy = ow_ovsdb_phy_get(phy_name);
    ow_ovsdb_phy_set_radar_log(phy, channel);
    phy->radar_channel = *channel;
    phy->radar_time = time(NULL);
    ow_ovsdb_phy_work_sched(phy);
}

static bool
ow_ovsdb_vif_is_phy_ready(struct ow_ovsdb_vif *vif)
{
    struct ow_ovsdb *root = &g_ow_ovsdb;
    struct ow_ovsdb_phy *phy;

    if (vif->phy_name == NULL)
        return false;

    phy = ds_tree_find(&root->phy_tree, vif->phy_name);
    if (phy == NULL)
        return false;

    if (ev_is_active(&phy->work) == true)
        return false;

    if (strlen(phy->state_cur.if_name) == 0)
        return false;

    return true;
}

static void
ow_ovsdb_vif_gc(struct ow_ovsdb_vif *vif)
{
    struct ds_tree *tree = &vif->root->vif_tree;

    if (strlen(vif->config.if_name) > 0) return;
    if (strlen(vif->state_cur.if_name) > 0) return;
    if (vif->info != NULL) return;

    ds_tree_remove(tree, vif);
    FREE(vif->phy_name);
    FREE(vif->vif_name);
    FREE(vif);
}

static void
ow_ovsdb_vif_fill_schema(struct ow_ovsdb_vif *vif)
{
    const struct schema_Wifi_VIF_Config *vconf = NULL;
    const char *uuid = vif->state_cur._uuid.uuid;

    memset(&vif->state_new, 0, sizeof(vif->state_new));

    schema_Wifi_VIF_State_mark_all_present(&vif->state_new);
    vif->state_new._partial_update = true;
    vif->state_new.associated_clients_present = false;

    if (strlen(uuid) > 0)
        STRSCPY(vif->state_new._uuid.uuid, uuid);
    if (strlen(vif->config.if_name) > 0)
        vconf = &vif->config;

    ow_ovsdb_vifstate_to_schema(&vif->state_new, vconf, vif->info);
}

static void /* forward decl */
ow_ovsdb_sta_work_sched(struct ow_ovsdb_sta *sta);

static void
ow_ovsdb_vif_sync_stas(struct ow_ovsdb_vif *vif)
{
    struct ow_ovsdb *root = &g_ow_ovsdb;
    struct ow_ovsdb_sta *sta;

    ds_tree_foreach(&root->sta_tree, sta) {
        if (sta->info == NULL)
            continue;
        if (strcmp(vif->vif_name, sta->info->vif->vif_name) != 0)
            continue;
        ow_ovsdb_sta_work_sched(sta);
    }
}

static bool
ow_ovsdb_vif_state_is_wanted(struct ow_ovsdb_vif *vif)
{
    if (vif->info == NULL) return false;

    const enum osw_vif_type vif_type = vif->info->drv_state->vif_type;
    const bool is_configured = (ow_ovsdb_vif_is_deleted(vif) == false);
    const bool is_running = (vif->info->drv_state->status == OSW_VIF_ENABLED)
                         || (vif->info->drv_state->status == OSW_VIF_BROKEN);
    const bool always_wanted = (ow_ovsdb_vif_report_only_configured(vif_type) == false);

    return always_wanted ? true : (is_configured || is_running);
}

static void
ow_ovsdb_vif_sync_deleted_as_disabled(struct ow_ovsdb_vif *vif)
{
    if (ow_ovsdb_vif_is_deleted(vif) == false) return;

    const enum osw_vif_type vif_type = vif->info != NULL
                                     ? vif->info->drv_state->vif_type
                                     : OSW_VIF_UNDEFINED;

    if (ow_ovsdb_vif_treat_deleted_as_disabled(vif_type) && vif->phy_name != NULL) {
        const bool x = false;
        ow_conf_vif_clear(vif->vif_name);
        ow_conf_vif_set_enabled(vif->vif_name, &x);
        ow_conf_vif_set_phy_name(vif->vif_name, vif->phy_name);
    }
    else {
        ow_conf_vif_unset(vif->vif_name);
    }
}

static bool
ow_ovsdb_vif_sync(struct ow_ovsdb_vif *vif)
{
    if (ow_ovsdb_sync_allowed(vif->root, &vif->deadline) == false) return false;

    ow_ovsdb_vif_sync_deleted_as_disabled(vif);

    if (ow_ovsdb_vif_state_is_wanted(vif) == false) {
        if (strlen(vif->state_cur.if_name) == 0) return true;

        LOGI("ow: ovsdb: vif: %s: deleting", vif->vif_name);

        ovsdb_table_delete(&table_Wifi_VIF_State, &vif->state_cur);
        memset(&vif->state_cur, 0, sizeof(vif->state_cur));
        return true;
    }
    else {
        if (ow_ovsdb_vif_is_phy_ready(vif) == false)
            return false;

        ow_ovsdb_vif_fill_schema(vif);

        if (memcmp(&vif->state_cur, &vif->state_new, sizeof(vif->state_cur)) == 0)
            return true;

        ovsdb_table_t *t = &table_Wifi_VIF_State;
        char *pt = SCHEMA_TABLE(Wifi_Radio_State);
        char *pc = SCHEMA_COLUMN(Wifi_Radio_State, vif_states);
        char *pk = SCHEMA_COLUMN(Wifi_Radio_State, if_name);
        json_t *w = ovsdb_where_simple(pk, vif->phy_name);

        if (w == NULL)
            return false;

        LOGI("ow: ovsdb: vif: %s: upserting", vif->vif_name);

        bool ok = ovsdb_table_upsert_with_parent(t, &vif->state_new, true, NULL,
                                                 pt, w, pc);
        if (ok == false)
            return false;

        vif->state_cur = vif->state_new;
        ow_ovsdb_vif_sync_stas(vif);
        return true;
    }
}

static void
ow_ovsdb_vif_work_cb(EV_P_ ev_timer *arg, int events)
{
    struct ow_ovsdb_vif *vif = arg->data;
    if (ow_ovsdb_vif_sync(vif) == false) return;
    ev_timer_stop(EV_A_ &vif->work);
    ev_timer_stop(EV_A_ &vif->deadline);
    ow_ovsdb_vif_gc(vif);
}

static void
ow_ovsdb_vif_work_sched(struct ow_ovsdb_vif *vif)
{
    if (vif == NULL) return;

    ow_ovsdb_sync_sched(&vif->work, &vif->deadline);

    if (vif->phy_name != NULL) {
        struct ow_ovsdb *root = &g_ow_ovsdb;
        struct ow_ovsdb_phy *phy = ds_tree_find(&root->phy_tree, vif->phy_name);
        if (phy == NULL) return;

        ow_ovsdb_phy_work_sched(phy);
    }
}

static void
ow_ovsdb_vif_init(struct ow_ovsdb *root,
                  struct ow_ovsdb_vif *vif,
                  const char *vif_name)
{
    vif->root = root;
    vif->vif_name = STRDUP(vif_name);
    ev_timer_init(&vif->work, ow_ovsdb_vif_work_cb, 0, 0);
    ev_timer_init(&vif->deadline, ow_ovsdb_vif_work_cb, 0, 0);
    vif->work.data = vif;
    vif->deadline.data = vif;
    ds_tree_insert(&root->vif_tree, vif, vif->vif_name);
}

static struct ow_ovsdb_vif *
ow_ovsdb_vif_get(const char *vif_name)
{
    struct ow_ovsdb *root = &g_ow_ovsdb;
    struct ow_ovsdb_vif *vif = ds_tree_find(&root->vif_tree, vif_name);
    if (vif == NULL) {
        vif = CALLOC(1, sizeof(*vif));
        ow_ovsdb_vif_init(root, vif, vif_name);
    }
    return vif;
}

static void
ow_ovsdb_vif_set_info(const char *vif_name,
                      const struct osw_state_vif_info *info)
{
    struct ow_ovsdb_vif *vif = ow_ovsdb_vif_get(vif_name);

    vif->info = info;
    if (vif->phy_name == NULL)
        vif->phy_name = STRDUP(info->phy->phy_name);

    ow_ovsdb_vif_work_sched(vif);
}

static void
ow_ovsdb_vif_set_config(const char *vif_name,
                        const struct schema_Wifi_VIF_Config *config)
{
    struct ow_ovsdb_vif *vif = ow_ovsdb_vif_get(vif_name);

    memset(&vif->config, 0, sizeof(vif->config));
    if (config != NULL)
        vif->config = *config;

    if ((vif->config.wpa_psks_changed == true) ||
        (vif->config.wpa_oftags_changed == true))
        ow_ovsdb_vif_sync_stas(vif);

    ow_ovsdb_vif_work_sched(vif);
}

static void
ow_ovsdb_sta_gc(struct ow_ovsdb_sta *sta)
{
    struct ds_tree *tree = &sta->root->sta_tree;

    if (sta->info != NULL) return;
    if (sta->vif_name != NULL) return;
    if (sta->oftag != NULL) return;
    if (strlen(sta->state_cur.mac) > 0) return;
    if (sta->mld != NULL) return;

    ds_tree_remove(tree, sta);
    FREE(sta);
}

static bool
ow_ovsdb_sta_is_vif_ready(struct ow_ovsdb_sta *sta)
{
    struct ow_ovsdb *root = &g_ow_ovsdb;
    struct ow_ovsdb_vif *vif;

    if (sta->info == NULL)
        return false;

    vif = ds_tree_find(&root->vif_tree, sta->info->vif->vif_name);
    if (vif == NULL)
        return false;

    if (ev_is_active(&vif->work) == true)
        return false;

    if (strlen(vif->state_cur.if_name) == 0)
        return false;

    return true;
}

static void
ow_ovsdb_keyid_fixup(char *key_id,
                     size_t key_id_len,
                     const struct schema_Wifi_VIF_Config *vconf)
{
    if (vconf->wpa_psks_len == 1) {
        LOGT("ow: ovsdb: sta: Resolving oftag: only one psk"
             "configured on interface '%s'!", vconf->if_name);
        strscpy(key_id, vconf->wpa_psks_keys[0], key_id_len);
    }
}

static const char *
ow_ovsdb_sta_derive_oftag(struct ow_ovsdb_sta *sta)
{
    struct ow_ovsdb *root = &g_ow_ovsdb;
    struct ow_ovsdb_vif *vif;
    char key1[32];
    char key2[64];
    const char *oftag;

    if (sta->info == NULL)
        return NULL;

    vif = ds_tree_find(&root->vif_tree, sta->info->vif->vif_name ?: "");
    if (vif == NULL)
        return NULL;

    if (sta->info == NULL)
        return NULL;

    ow_ovsdb_keyid2str(key1, sizeof(key1), sta->info->drv_state->key_id);
    ow_ovsdb_keyid_fixup(key1, ARRAY_SIZE(key1), &vif->config);
    oftag = SCHEMA_KEY_VAL_NULL(vif->config.wpa_oftags, key1);
    if (oftag != NULL)
        return oftag;

    if (sta->info->drv_state->key_id == 0)
        snprintf(key2, sizeof(key2), "oftag");
    else
        snprintf(key2, sizeof(key2), "oftag-%s", key1);
    oftag = SCHEMA_KEY_VAL_NULL(vif->config.security, key2);
    if (oftag != NULL)
        return oftag;

    if (vif->config.default_oftag_exists)
        return vif->config.default_oftag;

    return NULL;
}

static const char *
ow_ovsdb_akm_into_cstr(const enum osw_akm akm)
{
    switch (akm) {
        case OSW_AKM_UNSPEC: return NULL;

        /* RSN */
        case OSW_AKM_RSN_EAP: return SCHEMA_CONSTS_KEY_WPA_EAP;
        case OSW_AKM_RSN_PSK: return SCHEMA_CONSTS_KEY_WPA_PSK;
        case OSW_AKM_RSN_FT_EAP: return SCHEMA_CONSTS_KEY_FT_EAP;
        case OSW_AKM_RSN_FT_PSK: return SCHEMA_CONSTS_KEY_FT_PSK;
        case OSW_AKM_RSN_EAP_SHA256: return SCHEMA_CONSTS_KEY_WPA_EAP_SHA256;
        case OSW_AKM_RSN_EAP_SHA384: return SCHEMA_CONSTS_KEY_WPA_EAP_SHA384;
        case OSW_AKM_RSN_PSK_SHA256: return SCHEMA_CONSTS_KEY_WPA_PSK_SHA256;
        case OSW_AKM_RSN_SAE: return SCHEMA_CONSTS_KEY_SAE;
        case OSW_AKM_RSN_SAE_EXT: return SCHEMA_CONSTS_KEY_SAE_EXT;
        case OSW_AKM_RSN_FT_SAE: return SCHEMA_CONSTS_KEY_FT_SAE;
        case OSW_AKM_RSN_FT_SAE_EXT: return SCHEMA_CONSTS_KEY_FT_SAE_EXT;
        case OSW_AKM_RSN_EAP_SUITE_B: return SCHEMA_CONSTS_KEY_WPA_EAP_B;
        case OSW_AKM_RSN_EAP_SUITE_B_192: return SCHEMA_CONSTS_KEY_WPA_EAP_B_192;
        case OSW_AKM_RSN_FT_EAP_SHA384: return SCHEMA_CONSTS_KEY_FT_EAP_SHA384;

        /* WFA */
        case OSW_AKM_WFA_DPP: return SCHEMA_CONSTS_KEY_DPP;

        /* Undefined in OVSDB schema */
        case OSW_AKM_WPA_NONE: return NULL;
        case OSW_AKM_RSN_FT_PSK_SHA384: return NULL;
        case OSW_AKM_RSN_PSK_SHA384: return NULL;

        /* Undefined in OVSDB schema, questionable, but close enough */
        case OSW_AKM_WPA_8021X: return SCHEMA_CONSTS_KEY_WPA_PSK;
        case OSW_AKM_WPA_PSK: return SCHEMA_CONSTS_KEY_WPA_PSK;
    }
    return NULL;
}

static const char *
ow_ovsdb_cipher_into_cstr(const enum osw_cipher cipher)
{
    switch (cipher) {
        case OSW_CIPHER_UNSPEC: return NULL;

        /* RSN */
        case OSW_CIPHER_RSN_NONE: return SCHEMA_CONSTS_CIPHER_RSN_NONE;
        case OSW_CIPHER_RSN_WEP_40: return SCHEMA_CONSTS_CIPHER_RSN_WEP;
        case OSW_CIPHER_RSN_TKIP: return SCHEMA_CONSTS_CIPHER_RSN_TKIP;
        case OSW_CIPHER_RSN_CCMP_128: return SCHEMA_CONSTS_CIPHER_RSN_CCMP;
        case OSW_CIPHER_RSN_CCMP_256: return SCHEMA_CONSTS_CIPHER_RSN_CCMP_256;
        case OSW_CIPHER_RSN_GCMP_128: return SCHEMA_CONSTS_CIPHER_RSN_GCMP;
        case OSW_CIPHER_RSN_GCMP_256: return SCHEMA_CONSTS_CIPHER_RSN_GCMP_256;
        case OSW_CIPHER_RSN_BIP_CMAC_128: return SCHEMA_CONSTS_CIPHER_RSN_BIP_CMAC;

        /* WPA */
        case OSW_CIPHER_WPA_NONE: return SCHEMA_CONSTS_CIPHER_WPA_NONE;
        case OSW_CIPHER_WPA_TKIP: return SCHEMA_CONSTS_CIPHER_WPA_TKIP;
        case OSW_CIPHER_WPA_CCMP: return SCHEMA_CONSTS_CIPHER_WPA_CCMP;

        /* Undefined in OVSDB schema */
        case OSW_CIPHER_RSN_BIP_GMAC_128: return NULL;
        case OSW_CIPHER_RSN_BIP_GMAC_256: return NULL;

        /* Undefined in OVSDB schema, questionable, but close enough */
        case OSW_CIPHER_WPA_WEP_40: return SCHEMA_CONSTS_CIPHER_RSN_WEP;
        case OSW_CIPHER_WPA_WEP_104: return SCHEMA_CONSTS_CIPHER_RSN_WEP;
        case OSW_CIPHER_RSN_WEP_104: return SCHEMA_CONSTS_CIPHER_RSN_WEP;
        case OSW_CIPHER_RSN_BIP_CMAC_256: return SCHEMA_CONSTS_CIPHER_RSN_BIP_CMAC;
    }
    return NULL;
}

static void
ow_ovsdb_sta_fill_schema(struct ow_ovsdb_sta *sta)
{
    struct schema_Wifi_Associated_Clients *schema = &sta->state_new;
    const struct osw_drv_sta_state *state = sta->info->drv_state;
    const char *oftag = ow_ovsdb_sta_derive_oftag(sta);
    const char *uuid = sta->state_cur._uuid.uuid;
    const struct osw_hwaddr *mld_addr = NULL;
    struct osw_hwaddr_str mac_str;
    struct osw_hwaddr_str mld_str;
    struct ow_ovsdb *root = &g_ow_ovsdb;
    struct ow_ovsdb_vif *vif;
    const char *pairwise_cipher = ow_ovsdb_cipher_into_cstr(state->pairwise_cipher);
    const char *akm = ow_ovsdb_akm_into_cstr(state->akm);
    const bool pmf = state->pmf;
    char key_id[64];
    bool is_mld = false;

    memset(schema, 0, sizeof(*schema));

    if (strlen(uuid) > 0)
        STRSCPY(schema->_uuid.uuid, uuid);

    if (sta->info == NULL)
        return;

    vif = ds_tree_find(&root->vif_tree, sta->info->vif->vif_name ?: "");
    if (vif == NULL)
        return;

    mld_addr = &sta->info->drv_state->mld_addr;
    is_mld = osw_hwaddr_is_zero(mld_addr) ? false : true;

    osw_hwaddr2str(&sta->sta_addr, &mac_str);
    osw_hwaddr2str(mld_addr, &mld_str);
    ow_ovsdb_keyid2str(key_id, sizeof(key_id), sta->info->drv_state->key_id);
    ow_ovsdb_keyid_fixup(key_id, ARRAY_SIZE(key_id), &vif->config);

    SCHEMA_SET_STR(schema->mac, mac_str.buf);
    SCHEMA_SET_STR(schema->state, "active");
    SCHEMA_SET_STR(schema->key_id, key_id);
    SCHEMA_SET_BOOL(schema->pmf, pmf);
    if (is_mld) SCHEMA_SET_STR(schema->mld_addr, mld_str.buf);
    if (akm != NULL) SCHEMA_SET_STR(schema->wpa_key_mgmt, akm);
    if (pairwise_cipher != NULL) SCHEMA_SET_STR(schema->pairwise_cipher, pairwise_cipher);

    if (oftag != NULL)
        SCHEMA_SET_STR(schema->oftag, oftag);
}

static int
ow_ovsdb_sta_tag_mutate(const struct osw_hwaddr *addr,
                        const char *oftag,
                        const char *action)
{
    const char *table = SCHEMA_TABLE(Openflow_Tag);
    const char *column = SCHEMA_COLUMN(Openflow_Tag, name);
    struct osw_hwaddr_str mac_str;
    osw_hwaddr2str(addr, &mac_str);

    /* FIXME: This imperative approach to mutating doesn't
     * guarantee consistency in case of process restart,
     * etc. This doesn't clean up the ovsdb state that it
     * gets to see on boot. A more robust solution would be
     * to monitor Openflow_Tag in its entirety and re-sync
     * only when the entries of interest are out of sync.
     */

    json_t *where = ovsdb_tran_cond(OCLM_STR, column, OFUNC_EQ, oftag);
    if (where == NULL) {
        return -1;
    }

    json_t *row = ovsdb_mutation("device_value",
                                 json_string(action), /* "insert" or "delete" */
                                 json_string(mac_str.buf));
    if (row == NULL) {
        free(where);
        return -1;
    }

    json_t *rows = json_array();
    if (rows == NULL) {
        free(row);
        free(where);
        return -1;
    }
    json_array_append_new(rows, row);

    json_t *res = ovsdb_tran_call_s(table, OTR_MUTATE, where, rows);
    if (res == NULL) {
        return -1;
    }

    int cnt = ovsdb_get_update_result_count(res, table, "mutate");

    return cnt;
}

static bool
ow_ovsdb_sta_mld_sync(struct ow_ovsdb_sta_mld *mld)
{
    if (ow_ovsdb_sync_allowed(mld->root, &mld->deadline) == false) return false;

    struct ow_ovsdb_sta *sta;
    struct ow_ovsdb_sta_mld_oftag *oftag;
    struct ow_ovsdb_sta_mld_oftag *tmp;
    bool something_failed = false;

    ds_tree_foreach(&mld->oftags, oftag) {
        oftag->keep = false;
    }

    ds_tree_foreach(&mld->links, sta) {
        const bool no_oftag = (sta->oftag == NULL);
        if (no_oftag) continue;

        oftag = ds_tree_find(&mld->oftags, sta->oftag);
        const bool already_set = (oftag != NULL);
        if (already_set) {
            LOGD("ow: ovsdb: mld: "OSW_HWADDR_FMT": oftag: %s: already set", OSW_HWADDR_ARG(&mld->mld_addr), sta->oftag);
            oftag->keep = true;
            continue;
        }

        LOGI("ow: ovsdb: mld: "OSW_HWADDR_FMT": oftag: %s: setting", OSW_HWADDR_ARG(&mld->mld_addr), sta->oftag);
        const int num_updates = ow_ovsdb_sta_tag_mutate(&mld->mld_addr, sta->oftag, "insert");
        if (num_updates != 1) {
            something_failed = true;
            continue;
        }

        oftag = CALLOC(1, sizeof(*oftag));
        oftag->oftag = STRDUP(sta->oftag);
        oftag->keep = true;
        ds_tree_insert(&mld->oftags, oftag, oftag->oftag);
    }

    ds_tree_foreach_safe(&mld->oftags, oftag, tmp) {
        if (oftag->keep) continue;

        LOGI("ow: ovsdb: mld: "OSW_HWADDR_FMT": oftag: %s: clearing", OSW_HWADDR_ARG(&mld->mld_addr), oftag->oftag);
        ow_ovsdb_sta_tag_mutate(&mld->mld_addr, oftag->oftag, "delete");
        ds_tree_remove(&mld->oftags, oftag);
        FREE(oftag->oftag);
        FREE(oftag);
    }

    if (something_failed) return false;
    return true;
}

static void
ow_ovsdb_sta_mld_drop(struct ow_ovsdb_sta_mld *mld)
{
    if (mld == NULL) return;
    ds_tree_remove(&mld->root->mld_tree, mld);
    FREE(mld);
}

static void
ow_ovsdb_sta_mld_gc(struct ow_ovsdb_sta_mld *mld)
{
    if (ds_tree_is_empty(&mld->links) == false) return;
    if (ds_tree_is_empty(&mld->oftags) == false) return;
    ow_ovsdb_sta_mld_drop(mld);
}

static void
ow_ovsdb_sta_mld_work_cb(EV_P_ ev_timer *arg, int events)
{
    struct ow_ovsdb_sta_mld *mld = arg->data;
    if (ow_ovsdb_sta_mld_sync(mld) == false) return;
    ev_timer_stop(EV_A_ &mld->work);
    ev_timer_stop(EV_A_ &mld->deadline);
    ow_ovsdb_sta_mld_gc(mld);
}

static struct ow_ovsdb_sta_mld *
ow_ovsdb_sta_mld_alloc(struct ow_ovsdb *root, const struct osw_hwaddr *mld_addr)
{
    struct ow_ovsdb_sta_mld *mld = CALLOC(1, sizeof(*mld));
    mld->mld_addr = *mld_addr;
    mld->root = root;
    ds_tree_init(&mld->oftags, ds_str_cmp, struct ow_ovsdb_sta_mld_oftag, node);
    ds_tree_init(&mld->links, ds_void_cmp, struct ow_ovsdb_sta, node_mld);
    ev_timer_init(&mld->work, ow_ovsdb_sta_mld_work_cb, 0, 0);
    ev_timer_init(&mld->deadline, ow_ovsdb_sta_mld_work_cb, 0, 0);
    mld->work.data = mld;
    mld->deadline.data = mld;
    ds_tree_insert(&root->mld_tree, mld, &mld->mld_addr);
    return mld;
}

static struct ow_ovsdb_sta_mld *
ow_ovsdb_sta_mld_get(const struct osw_state_sta_info *info)
{
    if (info == NULL) return NULL;
    if (info->drv_state->connected == false) return NULL;
    const struct osw_hwaddr *mld_addr = &info->drv_state->mld_addr;
    if (osw_hwaddr_is_zero(mld_addr)) return NULL;
    struct ow_ovsdb *root = &g_ow_ovsdb;
    return ds_tree_find(&root->mld_tree, mld_addr)
        ?: ow_ovsdb_sta_mld_alloc(root, mld_addr);
}

static void
ow_ovsdb_sta_mld_work_sched(struct ow_ovsdb_sta_mld *mld)
{
    if (mld == NULL) return;

    ow_ovsdb_sync_sched(&mld->work, &mld->deadline);
}

static bool
ow_ovsdb_sta_set_tag(struct ow_ovsdb_sta *sta, const char *oftag)
{
    if (WARN_ON(sta->oftag != NULL)) return true;

    int c = ow_ovsdb_sta_tag_mutate(&sta->sta_addr, oftag, "insert");
    if (c != 1) return false;

    sta->oftag = STRDUP(oftag);
    ow_ovsdb_sta_mld_work_sched(sta->mld);
    return true;
}

static bool
ow_ovsdb_sta_unset_tag(struct ow_ovsdb_sta *sta)
{
    if (WARN_ON(sta->oftag == NULL)) return true;

    ow_ovsdb_sta_tag_mutate(&sta->sta_addr, sta->oftag, "delete");
    FREE(sta->oftag);
    sta->oftag = NULL;
    ow_ovsdb_sta_mld_work_sched(sta->mld);
    return true;
}

static bool
ow_ovsdb_sta_sync_tag(struct ow_ovsdb_sta *sta)
{
    const char *oftag = ow_ovsdb_sta_derive_oftag(sta);
    bool ok = true;

    if (oftag != NULL && sta->oftag == NULL) {
        LOGI("ow: ovsdb: sta: "OSW_HWADDR_FMT": setting oftag: '%s'",
             OSW_HWADDR_ARG(&sta->sta_addr), oftag);
        ok &= ow_ovsdb_sta_set_tag(sta, oftag);
    }
    else if (oftag == NULL && sta->oftag != NULL) {
        LOGI("ow: ovsdb: sta: "OSW_HWADDR_FMT": clearing oftag: '%s'",
             OSW_HWADDR_ARG(&sta->sta_addr), sta->oftag);
        ok &= ow_ovsdb_sta_unset_tag(sta);
    }
    else if (oftag != NULL && sta->oftag != NULL && strcmp(oftag, sta->oftag) != 0) {
        LOGI("ow: ovsdb: sta: "OSW_HWADDR_FMT": changing oftag: '%s' -> '%s'",
             OSW_HWADDR_ARG(&sta->sta_addr), sta->oftag, oftag);
        ok &= ow_ovsdb_sta_unset_tag(sta);
        ok &= ow_ovsdb_sta_set_tag(sta, oftag);
    }


    /* FIXME: This can't easily return false because it is
     * not guaranteed that Openflow_Tag entry will ever
     * exist. If this returned false it would risk running a
     * re-try job forever. So instead just print a message
     * for now.
     */
    if (ok != true) {
        LOGI("ow: ovsdb: sta: %s: failed to set oftag, ignoring",
             sta->state_cur.mac);
    }

    return true;
}

static bool
ow_ovsdb_sta_upsert(struct ow_ovsdb_sta *sta)
{
    if (ow_ovsdb_sta_is_vif_ready(sta) == false)
        return false;

    ow_ovsdb_sta_fill_schema(sta);

    if (memcmp(&sta->state_cur, &sta->state_new, sizeof(sta->state_cur)) == 0)
        return true;

    ovsdb_table_t *t = &table_Wifi_Associated_Clients;
    char *pt = SCHEMA_TABLE(Wifi_VIF_State);
    char *pc = SCHEMA_COLUMN(Wifi_VIF_State, associated_clients);
    char *pk = SCHEMA_COLUMN(Wifi_VIF_State, if_name);
    json_t *w = ovsdb_where_simple(pk, sta->info->vif->vif_name);

    if (w == NULL)
        return false;

    LOGI("ow: ovsdb: sta: %s: upserting: vif='%s' oftag='%s'",
         sta->state_new.mac, sta->info->vif->vif_name, sta->state_new.oftag);

    bool ok = ovsdb_table_upsert_with_parent(t, &sta->state_new, true, NULL,
                                             pt, w, pc);
    if (ok == false)
        return false;

    FREE(sta->vif_name);
    sta->vif_name = STRDUP(sta->info->vif->vif_name);
    sta->connected_at = sta->info->connected_at;
    sta->state_cur = sta->state_new;

    return true;
}

static bool
ow_ovsdb_sta_delete(struct ow_ovsdb_sta *sta)
{
    if (strlen(sta->state_cur.mac) == 0) return true;

    LOGI("ow: ovsdb: sta: %s: deleting: vif='%s' oftag='%s'",
         sta->state_cur.mac, sta->vif_name, sta->state_cur.oftag);

    ovsdb_table_delete(&table_Wifi_Associated_Clients, &sta->state_cur);
    memset(&sta->state_cur, 0, sizeof(sta->state_cur));
    FREE(sta->vif_name);
    sta->vif_name = NULL;
    sta->connected_at = 0;

    return true;
}

static bool
ow_ovsdb_sta_roamed(struct ow_ovsdb_sta *sta)
{
    if (sta->vif_name == NULL)
        return false;

    if (sta->info == NULL)
        return false;

    if (strcmp(sta->vif_name, sta->info->vif->vif_name) == 0)
        return false;

    if (sta->connected_at == sta->info->connected_at)
        return false;

    return true;
}

static bool
ow_ovsdb_sta_sync(struct ow_ovsdb_sta *sta)
{
    if (ow_ovsdb_sync_allowed(sta->root, &sta->deadline) == false) return false;

    bool ok = true;
    bool roamed = ow_ovsdb_sta_roamed(sta);

    if (roamed) {
        LOGI("ow: ovsdb: sta: %s: roamed from '%s' to '%s'",
             sta->state_cur.mac,
             sta->vif_name,
             sta->info->vif->vif_name);
    }

    if (roamed == true || sta->info == NULL)
        ok &= ow_ovsdb_sta_delete(sta);

    if (sta->info != NULL)
        ok &= ow_ovsdb_sta_upsert(sta);

    if (ok == true)
        ok &= ow_ovsdb_sta_sync_tag(sta);

    return ok;
}

static void
ow_ovsdb_sta_work_cb(EV_P_ ev_timer *arg, int events)
{
    struct ow_ovsdb_sta *sta = arg->data;
    if (ow_ovsdb_sta_sync(sta) == false) return;
    ev_timer_stop(EV_A_ &sta->work);
    ev_timer_stop(EV_A_ &sta->deadline);
    ow_ovsdb_sta_gc(sta);
}

static void
ow_ovsdb_sta_work_sched(struct ow_ovsdb_sta *sta)
{
    ow_ovsdb_sync_sched(&sta->work, &sta->deadline);

    if (sta->info != NULL) {
        struct ow_ovsdb *root = &g_ow_ovsdb;
        struct ow_ovsdb_vif *vif = ds_tree_find(&root->vif_tree, sta->info->vif->vif_name);
        if (vif == NULL) return;

        ow_ovsdb_vif_work_sched(vif);
    }
}

static void
ow_ovsdb_sta_init(struct ow_ovsdb *root,
                  struct ow_ovsdb_sta *sta,
                  const struct osw_hwaddr *sta_addr)
{
    sta->root = root;
    sta->sta_addr = *sta_addr;
    ev_timer_init(&sta->work, ow_ovsdb_sta_work_cb, 0, 0);
    ev_timer_init(&sta->deadline, ow_ovsdb_sta_work_cb, 0, 0);
    sta->work.data = sta;
    sta->deadline.data = sta;
    ds_tree_insert(&root->sta_tree, sta, &sta->sta_addr);
}

static struct ow_ovsdb_sta *
ow_ovsdb_sta_get(const struct osw_hwaddr *sta_addr)
{
    struct ow_ovsdb *root = &g_ow_ovsdb;
    struct ow_ovsdb_sta *sta = ds_tree_find(&root->sta_tree, sta_addr);
    if (sta == NULL) {
        sta = CALLOC(1, sizeof(*sta));
        ow_ovsdb_sta_init(root, sta, sta_addr);
    }
    return sta;
}

static void
ow_ovsdb_sta_clear_mld(struct ow_ovsdb_sta *sta)
{
    if (WARN_ON(sta == NULL)) return;
    if (sta->mld == NULL) return;
    ow_ovsdb_sta_mld_work_sched(sta->mld);
    ds_tree_remove(&sta->mld->links, sta);
    sta->mld = NULL;
    ow_ovsdb_sta_work_sched(sta);
}

static void
ow_ovsdb_sta_set_mld(struct ow_ovsdb_sta *sta, struct ow_ovsdb_sta_mld *mld)
{
    if (mld == NULL) return;
    if (WARN_ON(sta == NULL)) return;
    if (WARN_ON(sta->mld != NULL)) ow_ovsdb_sta_clear_mld(sta);
    ds_tree_insert(&mld->links, sta, sta);
    sta->mld = mld;
    ow_ovsdb_sta_mld_work_sched(mld);
    ow_ovsdb_sta_work_sched(sta);
}

static void
ow_ovsdb_sta_update_mld(struct ow_ovsdb_sta *sta)
{
    struct ow_ovsdb_sta_mld *mld = ow_ovsdb_sta_mld_get(sta->info);
    if (sta->mld == mld) return;
    LOGI("ow: ovsdb: sta: "OSW_HWADDR_FMT": mld: "OSW_HWADDR_FMT" -> "OSW_HWADDR_FMT,
         OSW_HWADDR_ARG(&sta->sta_addr),
         OSW_HWADDR_ARG(sta->mld ? &sta->mld->mld_addr : osw_hwaddr_zero()),
         OSW_HWADDR_ARG(mld ? &mld->mld_addr : osw_hwaddr_zero()));
    ow_ovsdb_sta_clear_mld(sta);
    ow_ovsdb_sta_set_mld(sta, mld);
}

static void
ow_ovsdb_sta_set_info(const struct osw_hwaddr *sta_addr,
                      const struct osw_state_sta_info *info)
{
    struct ow_ovsdb_sta *sta = ow_ovsdb_sta_get(sta_addr);

    sta->info = osw_state_sta_lookup_newest(info->mac_addr);
    if (sta->info && sta->info->drv_state->connected == false)
        sta->info = NULL;

    ow_ovsdb_sta_update_mld(sta);
    ow_ovsdb_sta_work_sched(sta);
}

static void
ow_ovsdb_retry_all_work(struct ow_ovsdb *root)
{
    struct ow_ovsdb_phy *phy;
    ds_tree_foreach(&root->phy_tree, phy) {
        if (ev_is_active(&phy->work)) {
            ow_ovsdb_phy_work_sched(phy);
        }
    }

    struct ow_ovsdb_vif *vif;
    ds_tree_foreach(&root->vif_tree, vif) {
        if (ev_is_active(&vif->work)) {
            ow_ovsdb_vif_work_sched(vif);
        }
    }

    struct ow_ovsdb_sta *sta;
    ds_tree_foreach(&root->sta_tree, sta) {
        if (ev_is_active(&sta->work)) {
            ow_ovsdb_sta_work_sched(sta);
        }
    }

    struct ow_ovsdb_sta_mld *sta_mld;
    ds_tree_foreach(&root->mld_tree, sta_mld) {
        if (ev_is_active(&sta_mld->work)) {
            ow_ovsdb_sta_mld_work_sched(sta_mld);
        }
    }
}

static void
ow_ovsdb_idle_cb(struct osw_state_observer *self)
{
    struct ow_ovsdb *root = &g_ow_ovsdb;
    root->idle = true;
    ow_ovsdb_retry_all_work(root);
}

static void
ow_ovsdb_busy_cb(struct osw_state_observer *self)
{
    struct ow_ovsdb *root = &g_ow_ovsdb;
    root->idle = false;
}

static void
ow_ovsdb_phy_added_cb(struct osw_state_observer *self,
                      const struct osw_state_phy_info *info)
{
    ow_ovsdb_phy_set_info(info->phy_name, info);
}

static void
ow_ovsdb_phy_changed_cb(struct osw_state_observer *self,
                        const struct osw_state_phy_info *info)
{
    ow_ovsdb_phy_set_info(info->phy_name, info);
}

static void
ow_ovsdb_phy_removed_cb(struct osw_state_observer *self,
                        const struct osw_state_phy_info *info)
{
    ow_ovsdb_phy_set_info(info->phy_name, NULL);
}

static void
ow_ovsdb_vif_added_cb(struct osw_state_observer *self,
                      const struct osw_state_vif_info *info)
{
    ow_ovsdb_vif_set_info(info->vif_name, info);
    ow_ovsdb_ms_set_vif(&g_ow_ovsdb.ms, info);
}

static void
ow_ovsdb_vif_changed_cb(struct osw_state_observer *self,
                      const struct osw_state_vif_info *info)
{
    ow_ovsdb_vif_set_info(info->vif_name, info);
    ow_ovsdb_ms_set_vif(&g_ow_ovsdb.ms, info);
}

static void
ow_ovsdb_vif_removed_cb(struct osw_state_observer *self,
                      const struct osw_state_vif_info *info)
{
    ow_ovsdb_vif_set_info(info->vif_name, NULL);
    ow_ovsdb_ms_set_vif(&g_ow_ovsdb.ms, info);
}

static void
ow_ovsdb_vif_radar_detected_cb(struct osw_state_observer *self,
                               const struct osw_state_vif_info *info,
                               const struct osw_channel *channel)
{
    ow_ovsdb_phy_set_radar(info->phy->phy_name, channel);
}

static void
ow_ovsdb_sta_connected_cb(struct osw_state_observer *self,
                          const struct osw_state_sta_info *info)
{
    ow_ovsdb_sta_set_info(info->mac_addr, info);
    if (OW_OVSDB_CM_NEEDS_PORT_STATE_BLIP) {
        ow_ovsdb_ms_set_sta_disconnected(&g_ow_ovsdb.ms, info);
    }
}

static void
ow_ovsdb_sta_changed_cb(struct osw_state_observer *self,
                        const struct osw_state_sta_info *info)
{
    ow_ovsdb_sta_set_info(info->mac_addr, info);
}

static void
ow_ovsdb_sta_disconnected_cb(struct osw_state_observer *self,
                             const struct osw_state_sta_info *info)
{
    ow_ovsdb_sta_set_info(info->mac_addr, info);
    if (OW_OVSDB_CM_NEEDS_PORT_STATE_BLIP) {
        ow_ovsdb_ms_set_sta_disconnected(&g_ow_ovsdb.ms, info);
    }
}

static struct osw_state_observer g_ow_ovsdb_osw_state_obs = {
    .name = "ow_ovsdb",
    .idle_fn = ow_ovsdb_idle_cb,
    .busy_fn = ow_ovsdb_busy_cb,
    .phy_added_fn = ow_ovsdb_phy_added_cb,
    .phy_changed_fn = ow_ovsdb_phy_changed_cb,
    .phy_removed_fn = ow_ovsdb_phy_removed_cb,
    .vif_added_fn = ow_ovsdb_vif_added_cb,
    .vif_changed_fn = ow_ovsdb_vif_changed_cb,
    .vif_removed_fn = ow_ovsdb_vif_removed_cb,
    .vif_radar_detected_fn = ow_ovsdb_vif_radar_detected_cb,
    .sta_connected_fn = ow_ovsdb_sta_connected_cb,
    .sta_changed_fn = ow_ovsdb_sta_changed_cb,
    .sta_disconnected_fn = ow_ovsdb_sta_disconnected_cb,
};

static void
ow_ovsdb_link_phy_vif(void)
{
    ovsdb_cache_row_t *rrow;
    ovsdb_cache_row_t *vrow;
    int i;

    /* FIXME: This could be optimized and moved to be called
     * through async debounce, instead of being called back
     * directly through callback_*().
     */

    ds_tree_foreach(&table_Wifi_VIF_Config.rows, vrow)
        vrow->user_flags &= ~OW_OVSDB_VIF_FLAG_SEEN;

    ds_tree_foreach(&table_Wifi_Radio_Config.rows, rrow) {
        const struct schema_Wifi_Radio_Config *rconf = (const void *)rrow->record;
        for (i = 0; i < rconf->vif_configs_len; i++) {
            ovsdb_cache_row_t *vrow = ovsdb_cache_find_row_by_uuid(&table_Wifi_VIF_Config, rconf->vif_configs[i].uuid);
            if (vrow == NULL) continue;
            const struct schema_Wifi_VIF_Config *vconf = (const void *)vrow->record;
            ow_conf_vif_set_phy_name(vconf->if_name, rconf->if_name);
            vrow->user_flags |= OW_OVSDB_VIF_FLAG_SEEN;
        }
    }

    ds_tree_foreach(&table_Wifi_VIF_Config.rows, vrow) {
        const struct schema_Wifi_VIF_Config *vconf = (const void *)vrow->record;
        if (vrow->user_flags & OW_OVSDB_VIF_FLAG_SEEN) continue;
        ow_conf_vif_set_phy_name(vconf->if_name, NULL);
    }
}

static void
callback_Wifi_VIF_State(ovsdb_update_monitor_t *mon,
                           struct schema_Wifi_VIF_State *old,
                           struct schema_Wifi_VIF_State *rconf,
                           ovsdb_cache_row_t *row)
{
    /* Nothing to do here. This is only needed to satisfy
     * OVSDB_CACHE_MONITOR() and collect vstate cache for
     * quick access at runtime.
     */
}

static int
ow_ovsdb_ch2freq(int band_num, int chan)
{
    switch (band_num) {
        case 2: return 2407 + (chan * 5);
        case 5: return 5000 + (chan * 5);
        case 6: if (chan == 2) return 5935;
                else return 5950 + (chan * 5);
    }
    return 0;
}

static int
ow_ovsdb_band2num(const char *freq_band)
{
    return strcmp(freq_band, "5G") == 0 ? 5 :
           strcmp(freq_band, "5GL") == 0 ? 5 :
           strcmp(freq_band, "5GU") == 0 ? 5 :
           strcmp(freq_band, "6G") == 0 ? 6 :
           2;
}

enum osw_channel_width
ow_ovsdb_htmode2width(const char *ht_mode)
{
    return strcmp(ht_mode, "HT20") == 0 ? OSW_CHANNEL_20MHZ :
           strcmp(ht_mode, "HT40") == 0 ? OSW_CHANNEL_40MHZ :
           strcmp(ht_mode, "HT80") == 0 ? OSW_CHANNEL_80MHZ :
           strcmp(ht_mode, "HT160") == 0 ? OSW_CHANNEL_160MHZ :
           strcmp(ht_mode, "HT320") == 0 ? OSW_CHANNEL_320MHZ :
           OSW_CHANNEL_20MHZ;
}

static int
ow_ovsdb_phy_get_max_2g_chan(const char *phy_name)
{
    int max = 11;
    const struct osw_state_phy_info *info = osw_state_phy_lookup(phy_name);
    if (info != NULL) {
        const struct osw_channel_state *arr = info->drv_state->channel_states;
        const size_t n = info->drv_state->n_channel_states;
        size_t i;
        for (i = 0; i < n; i++) {
            const struct osw_channel *c = &arr[i].channel;
            const int freq = c->control_freq_mhz;
            const enum osw_band b = osw_freq_to_band(freq);
            const int cn = osw_freq_to_chan(freq);
            switch (b) {
                case OSW_BAND_2GHZ:
                    if (cn > max) max = cn;
                    break;
                case OSW_BAND_UNDEFINED:
                case OSW_BAND_5GHZ:
                case OSW_BAND_6GHZ:
                    break;
            }
        }
    }
    return max;
}

static void
ow_ovsdb_rconf_to_ow_conf(const struct schema_Wifi_Radio_Config *rconf,
                          const bool is_new)
{
    if (is_new == true || rconf->enabled_changed == true) {
        if (rconf->enabled_exists == true) {
            ow_conf_phy_set_enabled(rconf->if_name, &rconf->enabled);
        }
        else {
            ow_conf_phy_set_enabled(rconf->if_name, NULL);
        }
    }

    if (is_new == true || rconf->tx_chainmask_changed == true) {
        if (rconf->tx_chainmask_exists == true) {
            ow_conf_phy_set_tx_chainmask(rconf->if_name, &rconf->tx_chainmask);
        }
        else {
            ow_conf_phy_set_tx_chainmask(rconf->if_name, NULL);
        }
    }

    if (is_new == true || rconf->tx_power_changed == true) {
        if (rconf->tx_power_exists == true) {
            ow_conf_phy_set_tx_power_dbm(rconf->if_name, &rconf->tx_power);
        }
        else {
            ow_conf_phy_set_tx_power_dbm(rconf->if_name, NULL);
        }
    }

    if (is_new == true || rconf->thermal_tx_chainmask_changed == true) {
        if (rconf->thermal_tx_chainmask_exists == true) {
            ow_conf_phy_set_thermal_tx_chainmask(rconf->if_name, &rconf->thermal_tx_chainmask);
        }
        else {
            ow_conf_phy_set_thermal_tx_chainmask(rconf->if_name, NULL);
        }
    }

    if (is_new == true ||
            rconf->center_freq0_chan_changed == true ||
            rconf->channel_changed == true ||
            rconf->puncture_bitmap_changed == true ||
            rconf->ht_mode_changed == true ||
            rconf->freq_band_changed == true) {
        if (rconf->channel_exists == true &&
                rconf->ht_mode_exists == true &&
                rconf->freq_band_exists == true) {
            const int max_2g_chan = ow_ovsdb_phy_get_max_2g_chan(rconf->if_name);
            const int band_num = ow_ovsdb_band2num(rconf->freq_band);
            const int cn = rconf->channel;
            const int freq = ow_ovsdb_ch2freq(band_num, cn);
            const enum osw_band band = osw_freq_to_band(freq);
            const enum osw_channel_width w = ow_ovsdb_htmode2width(rconf->ht_mode);
            const int w_mhz = osw_channel_width_to_mhz(w);
            const int *chans = (w < OSW_CHANNEL_320MHZ)
                             ? osw_channel_sidebands(band, cn, w_mhz, max_2g_chan)
                             : NULL;
            const int ccfs0_auto = (w == OSW_CHANNEL_20MHZ)
                                 ? freq
                                 : osw_chan_to_freq(band, osw_chan_avg(chans));
            const int ccfs0 = rconf->center_freq0_chan_exists
                            ? osw_chan_to_freq(band, rconf->center_freq0_chan)
                            : ccfs0_auto;
            const uint16_t puncture = rconf->puncture_bitmap_exists
                                    ? rconf->puncture_bitmap
                                    : 0x0000;
            const struct osw_channel c = {
                .control_freq_mhz = freq,
                .center_freq0_mhz = ccfs0,
                .width = w,
                .puncture_bitmap = puncture,
            };
            if (WARN_ON(c.center_freq0_mhz == 0)) {
                ow_conf_phy_set_ap_channel(rconf->if_name, NULL);
            }
            else {
                ow_conf_phy_set_ap_channel(rconf->if_name, &c);
            }
        }
        else {
            ow_conf_phy_set_ap_channel(rconf->if_name, NULL);
        }
    }

    if (is_new == true || rconf->bcn_int_changed == true) {
        if (rconf->bcn_int_exists == true) {
            ow_conf_phy_set_ap_beacon_interval_tu(rconf->if_name, &rconf->bcn_int);
        }
        else {
            ow_conf_phy_set_ap_beacon_interval_tu(rconf->if_name, NULL);
        }
    }

    if (is_new == true || rconf->hw_mode_changed == true) {
        if (rconf->hw_mode_exists == true) {
            bool ht = false;
            bool vht = false;
            bool he = false;
            bool eht = false;
            bool wmm = false;

            if (strcmp(rconf->hw_mode, "11be") == 0) {
                eht = true;
                he = true;
                if (strcmp(rconf->freq_band, "6G") != 0) {
                    vht = true;
                    ht = true;
                }
            }
            else if (strcmp(rconf->hw_mode, "11ax") == 0) {
                he = true;
                if (strcmp(rconf->freq_band, "6G") != 0) {
                    vht = true;
                    ht = true;
                }
            }
            else if (strcmp(rconf->hw_mode, "11ac") == 0) {
                /* FIXME: Technically 11ac is not allowed on 2.4G except
                 * vendor extensions.
                 */
                vht = true;
                ht = true;
            }
            else if (strcmp(rconf->hw_mode, "11n") == 0) {
                ht = true;
            }

            if (ht || he) {
                wmm = true;
            }

            ow_conf_phy_set_ap_ht_enabled(rconf->if_name, &ht);
            ow_conf_phy_set_ap_vht_enabled(rconf->if_name, &vht);
            ow_conf_phy_set_ap_he_enabled(rconf->if_name, &he);
            ow_conf_phy_set_ap_eht_enabled(rconf->if_name, &eht);
            ow_conf_phy_set_ap_wmm_enabled(rconf->if_name, &wmm);
        }
        else {
            ow_conf_phy_set_ap_ht_enabled(rconf->if_name, NULL);
            ow_conf_phy_set_ap_vht_enabled(rconf->if_name, NULL);
            ow_conf_phy_set_ap_he_enabled(rconf->if_name, NULL);
            ow_conf_phy_set_ap_eht_enabled(rconf->if_name, NULL);
            ow_conf_phy_set_ap_wmm_enabled(rconf->if_name, NULL);
        }
    }

    if (is_new == true || rconf->basic_rates_changed == true) {
        uint16_t mask = 0;
        {
            int i;
            for (i = 0; i < rconf->basic_rates_len; i++) {
                const enum osw_rate_legacy rate = ow_ovsdb_rate2enum(rconf->basic_rates[i]);
                if (WARN_ON(rate == OSW_RATE_UNSPEC)) continue;
                mask |= osw_rate_legacy_bit(rate);
            }
        }
        WARN_ON(rconf->basic_rates_len > 0 && mask == 0);
        ow_conf_phy_set_ap_basic_rates(rconf->if_name, mask == 0 ? NULL : &mask);
    }

    if (is_new == true || rconf->supported_rates_changed == true) {
        uint16_t mask = 0;
        {
            int i;
            for (i = 0; i < rconf->supported_rates_len; i++) {
                const enum osw_rate_legacy rate = ow_ovsdb_rate2enum(rconf->supported_rates[i]);
                if (WARN_ON(rate == OSW_RATE_UNSPEC)) continue;
                mask |= osw_rate_legacy_bit(rate);
            }
        }
        WARN_ON(rconf->supported_rates_len > 0 && mask == 0);
        ow_conf_phy_set_ap_supp_rates(rconf->if_name, mask == 0 ? NULL : &mask);
    }

    if (is_new == true || rconf->beacon_rate_changed == true) {
        const enum osw_rate_legacy rate = rconf->beacon_rate_exists
                                        ? ow_ovsdb_rate2enum(rconf->beacon_rate)
                                        : OSW_RATE_UNSPEC;
        WARN_ON(rconf->beacon_rate_exists && rate == OSW_RATE_UNSPEC);
        ow_conf_phy_set_ap_beacon_rate(rconf->if_name, rate == OSW_RATE_UNSPEC ? NULL : &rate);
    }

    if (is_new == true || rconf->mcast_rate_changed == true) {
        const enum osw_rate_legacy rate = rconf->mcast_rate_exists
                                        ? ow_ovsdb_rate2enum(rconf->mcast_rate)
                                        : OSW_RATE_UNSPEC;
        WARN_ON(rconf->mcast_rate_exists && rate == OSW_RATE_UNSPEC);
        ow_conf_phy_set_ap_mcast_rate(rconf->if_name, rate == OSW_RATE_UNSPEC ? NULL : &rate);
    }

    if (is_new == true || rconf->mgmt_rate_changed == true) {
        const enum osw_rate_legacy rate = rconf->mgmt_rate_exists
                                        ? ow_ovsdb_rate2enum(rconf->mgmt_rate)
                                        : OSW_RATE_UNSPEC;
        WARN_ON(rconf->mgmt_rate_exists && rate == OSW_RATE_UNSPEC);
        ow_conf_phy_set_ap_mgmt_rate(rconf->if_name, rate == OSW_RATE_UNSPEC ? NULL : &rate);
    }
}

static void
callback_Wifi_Radio_Config(ovsdb_update_monitor_t *mon,
                           struct schema_Wifi_Radio_Config *old,
                           struct schema_Wifi_Radio_Config *rconf,
                           ovsdb_cache_row_t *row)
{
    const bool is_new = (mon->mon_type == OVSDB_UPDATE_NEW);
    LOGI("ow: ovsdb: radio config: %s: %s",
         rconf->if_name,
         (mon->mon_type == OVSDB_UPDATE_NEW ? "new" :
          mon->mon_type == OVSDB_UPDATE_MODIFY ? "modify" :
          mon->mon_type == OVSDB_UPDATE_DEL ? "del" :
          mon->mon_type == OVSDB_UPDATE_ERROR ? "error" :
          ""));

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_rconf_to_ow_conf(rconf, is_new);
            ow_ovsdb_phy_set_config(rconf->if_name, rconf);
            break;
        case OVSDB_UPDATE_DEL:
            ow_conf_phy_unset(rconf->if_name);
            ow_ovsdb_phy_set_config(rconf->if_name, NULL);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }

    ow_ovsdb_link_phy_vif();
    ow_ovsdb_cconf_sched();
}

static void
ow_ovsdb_vstate_fix_vif_config(struct schema_Wifi_VIF_Config *vconf)
{
    struct schema_Wifi_VIF_State vstate = {0};
    vstate._partial_update = true;
    SCHEMA_SET_STR(vstate.if_name, vconf->if_name);
    SCHEMA_SET_UUID(vstate.vif_config, vconf->_uuid.uuid);
    ovsdb_table_update(&table_Wifi_VIF_State, &vstate);
}

static void
ow_ovsdb_vconf_to_min_hw_mode(const struct schema_Wifi_VIF_Config *vconf)
{
    const char *vif_name = vconf->if_name;
    const bool is_supported = ow_ovsdb_min_hw_mode_is_supported();

    if (vconf->min_hw_mode_exists && is_supported) {
        const enum ow_ovsdb_min_hw_mode min_hw_mode = ow_ovsdb_min_hw_mode_from_str(vconf->min_hw_mode);
        struct osw_ap_mode ap_mode = {0};

        ow_ovsdb_min_hw_mode_to_ap_mode(&ap_mode, min_hw_mode);

        ow_conf_vif_set_ap_ht_required(vif_name, &ap_mode.ht_required);
        ow_conf_vif_set_ap_vht_required(vif_name, &ap_mode.vht_required);
        ow_conf_vif_set_ap_supp_rates(vif_name, &ap_mode.supported_rates);
        ow_conf_vif_set_ap_basic_rates(vif_name, &ap_mode.basic_rates);
        ow_conf_vif_set_ap_beacon_rate(vif_name, &ap_mode.beacon_rate);
    }
    else {
        ow_conf_vif_set_ap_ht_required(vif_name, NULL);
        ow_conf_vif_set_ap_vht_required(vif_name, NULL);
        ow_conf_vif_set_ap_supp_rates(vif_name, NULL);
        ow_conf_vif_set_ap_basic_rates(vif_name, NULL);
        ow_conf_vif_set_ap_beacon_rate(vif_name, NULL);
    }
}

static void
ow_ovsdb_vconf_to_ow_conf_ap_wpa(const struct schema_Wifi_VIF_Config *vconf)
{
    int i;
    const enum osw_pmf pmf = pmf_from_str(vconf->pmf);
    const bool tkip = vconf->wpa_pairwise_tkip
                   || vconf->rsn_pairwise_tkip;
    const bool ccmp = vconf->wpa_pairwise_ccmp
                   || vconf->rsn_pairwise_ccmp;
    const bool wpa = vconf->wpa_pairwise_tkip
                  || vconf->wpa_pairwise_ccmp;
    const bool rsn = vconf->rsn_pairwise_tkip
                  || vconf->rsn_pairwise_ccmp;
    const bool ccmp256 = vconf->rsn_pairwise_ccmp256;
    const bool gcmp = vconf->rsn_pairwise_gcmp;
    const bool gcmp256 = vconf->rsn_pairwise_gcmp256;
    bool unsupported = false;
    bool psk = false;
    bool psk_sha256 = false;
    bool sae = false;
    bool sae_ext = false;
    bool eap = false;
    bool eap_sha256 = false;
    bool eap_sha384 = false;
    bool eap_suite_b = false;
    bool eap_suite_b192 = false;
    bool ft_eap = false;
    bool ft_eap_sha384 = false;
    bool ft_psk = false;
    bool ft_sae = false;
    bool ft_sae_ext = false;
    bool beacon_protection = vconf->beacon_protection;

    for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
        const char *akm = vconf->wpa_key_mgmt[i];

             if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_EAP) == 0)        { ft_eap = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_EAP_SHA384) == 0) { ft_eap_sha384 = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_PSK) == 0)        { ft_psk = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_SAE) == 0)        { ft_sae = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_SAE_EXT) == 0)    { ft_sae_ext = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_SAE) == 0)     { sae = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_SAE_EXT) == 0) { sae_ext = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_EAP) == 0)        { eap = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_EAP_B) == 0)      { eap_suite_b = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_EAP_B_192) == 0)  { eap_suite_b192 = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_EAP_SHA256) == 0) { eap_sha256 = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_EAP_SHA384) == 0) { eap_sha384 = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_PSK_SHA256) == 0) { psk_sha256 = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_PSK) == 0)        { psk = true; }
        else { unsupported = true; }
    }

    if (unsupported == true) {
        /* This is intended to allow state to be inherited
         * as config, eg. this should allow eap/radius
         * configurations to work if they were configured
         * prior.
         */
        LOGI("ow: ovsdb: vif: %s: unsupported configuration, ignoring", vconf->if_name);
        ow_conf_vif_set_ap_akm_eap(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_eap_sha256(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_eap_sha384(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_eap_suite_b(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_eap_suite_b192(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_psk(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_psk_sha256(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_sae(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_sae_ext(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_ft_eap(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_ft_eap_sha384(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_ft_psk(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_ft_sae(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_ft_sae_ext(vconf->if_name, NULL);
        ow_conf_vif_set_ap_pairwise_ccmp(vconf->if_name, NULL);
        ow_conf_vif_set_ap_pairwise_ccmp256(vconf->if_name, NULL);
        ow_conf_vif_set_ap_pairwise_tkip(vconf->if_name, NULL);
        ow_conf_vif_set_ap_pairwise_gcmp(vconf->if_name, NULL);
        ow_conf_vif_set_ap_pairwise_gcmp256(vconf->if_name, NULL);
        ow_conf_vif_set_ap_pmf(vconf->if_name, NULL);
        ow_conf_vif_set_ap_rsn(vconf->if_name, NULL);
        ow_conf_vif_set_ap_wpa(vconf->if_name, NULL);
        ow_conf_vif_set_ap_beacon_protection(vconf->if_name, NULL);
        return;
    }

    ow_conf_vif_set_ap_akm_eap(vconf->if_name, &eap);
    ow_conf_vif_set_ap_akm_eap_sha256(vconf->if_name, &eap_sha256);
    ow_conf_vif_set_ap_akm_eap_sha384(vconf->if_name, &eap_sha384);
    ow_conf_vif_set_ap_akm_eap_suite_b(vconf->if_name, &eap_suite_b);
    ow_conf_vif_set_ap_akm_eap_suite_b192(vconf->if_name, &eap_suite_b192);
    ow_conf_vif_set_ap_akm_psk(vconf->if_name, &psk);
    ow_conf_vif_set_ap_akm_psk_sha256(vconf->if_name, &psk_sha256);
    ow_conf_vif_set_ap_akm_sae(vconf->if_name, &sae);
    ow_conf_vif_set_ap_akm_sae_ext(vconf->if_name, &sae_ext);
    ow_conf_vif_set_ap_akm_ft_eap(vconf->if_name, &ft_eap);
    ow_conf_vif_set_ap_akm_ft_eap_sha384(vconf->if_name, &ft_eap_sha384);
    ow_conf_vif_set_ap_akm_ft_psk(vconf->if_name, &ft_psk);
    ow_conf_vif_set_ap_akm_ft_sae(vconf->if_name, &ft_sae);
    ow_conf_vif_set_ap_akm_ft_sae_ext(vconf->if_name, &ft_sae_ext);
    ow_conf_vif_set_ap_pairwise_tkip(vconf->if_name, &tkip);
    ow_conf_vif_set_ap_pairwise_ccmp(vconf->if_name, &ccmp);
    ow_conf_vif_set_ap_pairwise_ccmp256(vconf->if_name, &ccmp256);
    ow_conf_vif_set_ap_pairwise_gcmp(vconf->if_name, &gcmp);
    ow_conf_vif_set_ap_pairwise_gcmp256(vconf->if_name, &gcmp256);
    ow_conf_vif_set_ap_pmf(vconf->if_name, &pmf);
    ow_conf_vif_set_ap_wpa(vconf->if_name, &wpa);
    ow_conf_vif_set_ap_rsn(vconf->if_name, &rsn);
    ow_conf_vif_set_ap_beacon_protection(vconf->if_name, &beacon_protection);
}

static void
ow_ovsdb_vconf_to_ow_conf_ap(const struct schema_Wifi_VIF_Config *vconf,
                             const bool is_new)
{
    if (is_new == true || vconf->ssid_changed == true) {
        if (vconf->ssid_exists == true) {
            struct osw_ssid x;
            MEMZERO(x);
            STRSCPY_WARN(x.buf, vconf->ssid);
            x.len = strlen(x.buf);
            ow_conf_vif_set_ap_ssid(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_ssid(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->ssid_broadcast_changed == true) {
        if (vconf->ssid_broadcast_exists == true) {
            bool x =
                strcmp(vconf->ssid_broadcast, "enabled") == 0 ? false :
                strcmp(vconf->ssid_broadcast, "disabled") == 0 ? true :
                false;
            ow_conf_vif_set_ap_ssid_hidden(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_ssid_hidden(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->bridge_changed == true) {
        if (vconf->bridge_exists == true && strlen(vconf->bridge) > 0) {
            struct osw_ifname x = {0};
            STRSCPY_WARN(x.buf, vconf->bridge);
            ow_conf_vif_set_ap_bridge_if_name(vconf->if_name, &x);
        } else {
            ow_conf_vif_set_ap_bridge_if_name(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->mac_list_changed == true) {
        ow_conf_vif_flush_ap_acl(vconf->if_name);
        int i;
        for (i = 0; i < vconf->mac_list_len; i++) {
            struct osw_hwaddr addr;
            sscanf(vconf->mac_list[i], OSW_HWADDR_FMT, OSW_HWADDR_SARG(&addr));
            ow_conf_vif_add_ap_acl(vconf->if_name, &addr);
        }
    }

    if (is_new == true || vconf->wpa_psks_changed == true) {
        ow_conf_vif_flush_ap_psk(vconf->if_name);
        int i;
        for (i = 0; i < vconf->wpa_psks_len; i++) {
            int key_id = ow_ovsdb_str2keyid(vconf->wpa_psks_keys[i]);
            const char *psk = vconf->wpa_psks[i];
            ow_conf_vif_set_ap_psk(vconf->if_name, key_id, psk);
        }
    }

    if (is_new == true || vconf->mac_list_type_changed == true) {
        if (vconf->mac_list_type_exists == true) {
            const char *t = vconf->mac_list_type;
            enum osw_acl_policy p = ow_ovsdb_acl_policy_from_str(t);
            ow_conf_vif_set_ap_acl_policy(vconf->if_name, &p);
        }
        else {
            ow_conf_vif_set_ap_acl_policy(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->uapsd_enable_changed == true) {
        if (vconf->uapsd_enable_exists == true) {
            bool x = vconf->uapsd_enable;
            bool y = true;
            ow_conf_vif_set_ap_wmm_uapsd(vconf->if_name, &x);
            ow_conf_vif_set_ap_wmm(vconf->if_name, &y);
        }
        else {
            ow_conf_vif_set_ap_wmm_uapsd(vconf->if_name, NULL);
            ow_conf_vif_set_ap_wmm(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->rrm_changed == true) {
        if (vconf->rrm_exists == true) {
            bool x = vconf->rrm;
            ow_conf_vif_set_ap_rrm_neighbor_report(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_rrm_neighbor_report(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->btm_changed == true) {
        if (vconf->btm_exists == true) {
            bool x = vconf->btm;
            ow_conf_vif_set_ap_wnm_bss_trans(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_wnm_bss_trans(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->beacon_protection_changed == true) {
        if (vconf->beacon_protection_exists == true) {
            bool x = vconf->beacon_protection;
            ow_conf_vif_set_ap_beacon_protection(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_beacon_protection(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->mcast2ucast_changed == true) {
        if (vconf->mcast2ucast_exists == true) {
            bool x = vconf->mcast2ucast;
            ow_conf_vif_set_ap_mcast2ucast(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_mcast2ucast(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->group_rekey_changed == true) {
        if (vconf->group_rekey_exists == true) {
            int x = vconf->group_rekey;
            ow_conf_vif_set_ap_group_rekey_seconds(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_group_rekey_seconds(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->ap_bridge_changed == true) {
        if (vconf->ap_bridge_exists == true) {
            bool x = vconf->ap_bridge ? false : true;
            ow_conf_vif_set_ap_isolated(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_isolated(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->ft_mobility_domain_changed == true) {
        if (vconf->ft_mobility_domain_exists == true) {
            int x = vconf->ft_mobility_domain;
            ow_conf_vif_set_ap_ft_mobility_domain(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_ft_mobility_domain(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->wps_changed == true) {
        if (vconf->wps_exists == true) {
            const bool x = vconf->wps;
            ow_conf_vif_set_ap_wps(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_wps(vconf->if_name, NULL);
        }
    }

    /* schema value for MBO controls also OCE internally */
    if (is_new == true || vconf->mbo_changed == true) {
        if (vconf->mbo_exists == true) {
            const bool x = vconf->mbo;
            ow_conf_vif_set_ap_mbo(vconf->if_name, &x);
            ow_conf_vif_set_ap_oce(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_mbo(vconf->if_name, NULL);
            ow_conf_vif_set_ap_oce(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->oce_min_rssi_dbm_changed == true) {
        if (vconf->oce_min_rssi_dbm_exists == true) {
            const int x = vconf->oce_min_rssi_dbm;
            const bool y = true;
            ow_conf_vif_set_ap_oce_min_rssi_dbm(vconf->if_name, &x);
            ow_conf_vif_set_ap_oce_min_rssi_enable(vconf->if_name, &y);
        }
        else {
            ow_conf_vif_set_ap_oce_min_rssi_dbm(vconf->if_name, NULL);
            ow_conf_vif_set_ap_oce_min_rssi_enable(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->oce_retry_delay_sec_changed == true) {
        if (vconf->oce_retry_delay_sec_exists == true) {
            const int x = vconf->oce_retry_delay_sec;
            ow_conf_vif_set_ap_oce_retry_delay_sec(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_oce_retry_delay_sec(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->max_sta_changed == true) {
        if (vconf->max_sta_exists == true) {
            const int x = vconf->max_sta;
            ow_conf_vif_set_ap_max_sta(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_max_sta(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->min_hw_mode_changed == true) {
        ow_ovsdb_vconf_to_min_hw_mode(vconf);
    }

    if (is_new == true || vconf->multi_ap_changed == true) {
        if (vconf->multi_ap_exists == true) {
            struct osw_multi_ap multi_ap;
            ow_ovsdb_ap_multi_ap_from_cstr(vconf->multi_ap, &multi_ap);
            ow_conf_vif_set_ap_multi_ap(vconf->if_name, &multi_ap);
        }
        else {
            ow_conf_vif_set_ap_multi_ap(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->nas_identifier_changed == true) {
        if (vconf->nas_identifier_exists == true) {
            struct osw_nas_id x = {0};
            STRSCPY_WARN(x.buf, vconf->nas_identifier);
            ow_conf_vif_set_ap_nas_identifier(vconf->if_name, &x);
        } else {
            ow_conf_vif_set_ap_nas_identifier(vconf->if_name, NULL);
        }
    }

    if (is_new == true ||
            vconf->primary_radius_changed == true ||
            vconf->secondary_radius_changed == true)
    {
        int i;
        ow_conf_vif_flush_radius_refs(vconf->if_name);

        if (vconf->primary_radius_exists)
            ow_conf_vif_add_radius_ref(vconf->if_name, vconf->primary_radius.uuid);
        for (i = 0; i < vconf->secondary_radius_len; i++)
            ow_conf_vif_add_radius_ref(vconf->if_name, vconf->secondary_radius[i].uuid);
    }

    if (is_new == true ||
            vconf->primary_accounting_changed == true ||
            vconf->secondary_accounting_changed == true)
    {
        int i;
        ow_conf_vif_flush_radius_accounting_refs(vconf->if_name);

        if (vconf->primary_accounting_exists)
            ow_conf_vif_add_radius_accounting_ref(vconf->if_name, vconf->primary_accounting.uuid);
        for (i = 0; i < vconf->secondary_accounting_len; i++)
            ow_conf_vif_add_radius_accounting_ref(vconf->if_name, vconf->secondary_accounting[i].uuid);
    }

    const bool wpa_changed = (vconf->security_changed == true)
                          || (vconf->wpa_changed == true)
                          || (vconf->pmf_changed == true)
                          || (vconf->wpa_key_mgmt_changed == true)
                          || (vconf->wpa_pairwise_tkip_changed == true)
                          || (vconf->wpa_pairwise_ccmp_changed == true)
                          || (vconf->rsn_pairwise_tkip_changed == true)
                          || (vconf->rsn_pairwise_ccmp_changed == true)
                          || (vconf->rsn_pairwise_ccmp256_changed == true)
                          || (vconf->rsn_pairwise_gcmp_changed == true)
                          || (vconf->rsn_pairwise_gcmp256_changed == true);
    if (is_new == true || wpa_changed == true)
        ow_ovsdb_vconf_to_ow_conf_ap_wpa(vconf);

    if (is_new == true || vconf->passpoint_config_changed == true) {
        if (vconf->passpoint_config_exists == true) {
            const char *p = vconf->passpoint_config.uuid;
            ow_conf_vif_set_ap_passpoint_ref(vconf->if_name, p);
        }
        else {
            ow_conf_vif_set_ap_passpoint_ref(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->ft_over_ds_changed == true) {
        if (vconf->ft_over_ds_exists == true) {
            const bool x = vconf->ft_over_ds;
            ow_conf_vif_set_ap_ft_over_ds(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_ft_over_ds(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->ft_pmk_r1_push_changed == true) {
        if (vconf->ft_pmk_r1_push_exists == true) {
            const bool x = vconf->ft_pmk_r1_push;
            ow_conf_vif_set_ap_ft_pmk_r1_push(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_ft_pmk_r1_push(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->ft_psk_generate_local_changed == true) {
        if (vconf->ft_psk_generate_local_exists == true) {
            const bool x = vconf->ft_psk_generate_local;
            ow_conf_vif_set_ap_ft_psk_generate_local(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_ft_psk_generate_local(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->ft_pmk_r0_key_lifetime_sec_changed == true) {
        if (vconf->ft_pmk_r0_key_lifetime_sec_exists == true) {
            int x = vconf->ft_pmk_r0_key_lifetime_sec;
            ow_conf_vif_set_ap_ft_pmk_r0_key_lifetime_sec(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_ft_pmk_r0_key_lifetime_sec(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->ft_pmk_r1_max_key_lifetime_sec_changed == true) {
        if (vconf->ft_pmk_r1_max_key_lifetime_sec_exists == true) {
            int x = vconf->ft_pmk_r1_max_key_lifetime_sec;
            ow_conf_vif_set_ap_ft_pmk_r1_max_key_lifetime_sec(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_ap_ft_pmk_r1_max_key_lifetime_sec(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->ft_encr_key_changed == true) {
        if (vconf->ft_encr_key_exists == true) {
            struct osw_ft_encr_key x = {0};
            STRSCPY_WARN(x.buf, vconf->ft_encr_key);
            ow_conf_vif_set_ap_ft_encr_key(vconf->if_name, &x);
        } else {
            ow_conf_vif_set_ap_ft_encr_key(vconf->if_name, NULL);
        }
    }

    // dynamic beacon
    // vlan_id
    // parent
    // vif_radio_idx
    // vif_dbg_lvl
    // wds
}

static void
ow_ovsdb_vneigh_update_ft(ovsdb_update_monitor_t *mon,
                          struct schema_Wifi_VIF_Neighbors *wvn_old,
                          struct schema_Wifi_VIF_Neighbors *wvn,
                          ovsdb_cache_row_t *row)
{
    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            /* fall through */
        case OVSDB_UPDATE_MODIFY:
            if (mon->mon_type == OVSDB_UPDATE_MODIFY) {
                /* If BSSID or vif_name changes this is virtually just removing
                 * an entry, and then inserting a completely new one, assuming
                 * the new one has a bssid.
                 */
                struct osw_hwaddr bssid;
                const char *bssid_str = ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_Neighbors, bssid))
                                      ? wvn_old->bssid
                                      : wvn->bssid;
                const bool bssid_ok = (osw_hwaddr_from_cstr(bssid_str, &bssid) == true);
                const char *vif_name_old = wvn_old->if_name_exists && strlen(wvn_old->if_name) > 0
                                         ? wvn_old->if_name
                                         : NULL;
                const char *vif_name_new = wvn->if_name_exists && strlen(wvn->if_name) > 0
                                         ? wvn->if_name
                                         : NULL;
                const char *vif_name = ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_Neighbors, if_name))
                                     ? vif_name_old
                                     : vif_name_new;
                if (WARN_ON(vif_name == NULL)) break;
                if (WARN_ON(bssid_ok == false)) break;
                const bool changed = false
                    || ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_Neighbors, bssid))
                    || ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_Neighbors, if_name))
                    ;
                if (changed) {
                    ow_conf_vif_del_ap_neigh_ft(&bssid, vif_name);
                }
            }
            struct osw_hwaddr bssid;
            const bool bssid_ok = (osw_hwaddr_from_cstr(wvn->bssid, &bssid) == true);
            const char *vif_name = wvn->if_name_exists && strlen(wvn->if_name) > 0
                                   ? wvn->if_name
                                   : NULL;
            const bool vif_name_ok = (vif_name != NULL);
            const bool changed = false
                || ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_Neighbors, bssid))
                || ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_Neighbors, ft_enabled))
                || ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_Neighbors, if_name))
                || ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_Neighbors, ft_encr_key))
                || ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_Neighbors, nas_identifier));

            if (bssid_ok && vif_name_ok && changed) {
                ow_conf_vif_set_ap_neigh_ft(&bssid,
                                            vif_name,
                                            wvn->ft_enabled_exists ? wvn->ft_enabled : false,
                                            wvn->ft_encr_key_exists ?  wvn->ft_encr_key : "",
                                            wvn->nas_identifier_exists ? wvn->nas_identifier : "");
            }
            break;
        case OVSDB_UPDATE_DEL:
            {
                struct osw_hwaddr bssid;
                const bool bssid_ok = (osw_hwaddr_from_cstr(wvn->bssid, &bssid) == true);
                const char *vif_name = wvn->if_name_exists && strlen(wvn->if_name) > 0
                                       ? wvn->if_name
                                       : NULL;
                const bool vif_name_ok = (vif_name != NULL);
                if (bssid_ok && vif_name_ok) {
                    ow_conf_vif_del_ap_neigh_ft(&bssid, vif_name);
                }
            }
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void
ow_ovsdb_vneigh_to_ow_conf_ap(const struct schema_Wifi_VIF_Neighbors *vneigh,
                              const bool is_new)
{
    const bool changed = (is_new == true) ||
                         (vneigh->bssid_changed == true) ||
                         (vneigh->op_class_changed == true) ||
                         (vneigh->ft_enabled_changed == true) ||
                         (vneigh->channel_changed == true) ||
                         (vneigh->phy_type_changed == true);

    const bool req_params_set = (vneigh->bssid_exists == true) ||
                                (vneigh->op_class_exists == true) ||
                                (vneigh->channel_exists == true) ||
                                (vneigh->phy_type_exists);


    if (changed == false || req_params_set == false) return;

    const uint32_t ft_mobility = vneigh->ft_enabled ? 0x00000400 : 0;
    const uint32_t default_bssid_info = 0x0000008f
                                      | ft_mobility;
    struct osw_hwaddr bssid;
    osw_hwaddr_from_cstr(vneigh->bssid, &bssid);

    ow_conf_vif_set_ap_neigh(vneigh->if_name,
                             &bssid,
                             default_bssid_info,
                             vneigh->op_class,
                             vneigh->channel,
                             vneigh->phy_type);
}

static void
ow_ovsdb_vconf_to_ow_conf_sta(const struct schema_Wifi_VIF_Config *vconf,
                              const bool is_new)
{
    if (ow_ovsdb_cconf_use_vconf(vconf) == true) {
        struct osw_ssid ssid;
        MEMZERO(ssid);
        struct osw_hwaddr bssid;
        MEMZERO(bssid);
        struct osw_psk psk;
        MEMZERO(psk);
        struct osw_wpa wpa;
        MEMZERO(wpa);
        struct osw_ifname bridge;
        MEMZERO(bridge);
        const bool multi_ap = vconf->multi_ap_exists
                            ? ow_ovsdb_sta_multi_ap_from_cstr(vconf->multi_ap)
                            : false;

        STRSCPY_WARN(psk.str, vconf->wpa_psks[0]);
        STRSCPY_WARN(ssid.buf, vconf->ssid);
        ssid.len = strlen(vconf->ssid);
        osw_hwaddr_from_cstr(vconf->parent, &bssid);

        if (vconf->bridge_exists) {
            STRSCPY_WARN(bridge.buf, vconf->bridge);
        }

        int i;
        for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
            const char *akm = vconf->wpa_key_mgmt[i];

                 if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_EAP) == 0)         { wpa.akm_ft_eap = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_EAP_SHA384) == 0)  { wpa.akm_ft_eap_sha384 = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_PSK) == 0)         { wpa.akm_ft_psk = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_SAE) == 0)         { wpa.akm_ft_sae = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_SAE_EXT) == 0)     { wpa.akm_ft_sae_ext = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_SAE) == 0)            { wpa.akm_sae = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_SAE_EXT) == 0)        { wpa.akm_sae_ext = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_EAP) == 0)        { wpa.akm_eap = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_EAP_B) == 0)      { wpa.akm_eap_suite_b = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_EAP_B_192) == 0)  { wpa.akm_eap_suite_b192 = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_EAP_SHA256) == 0) { wpa.akm_eap_sha256 = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_EAP_SHA384) == 0) { wpa.akm_eap_sha384 = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_PSK) == 0)        { wpa.akm_psk = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_PSK_SHA256) == 0) { wpa.akm_psk_sha256 = true; }
        }

        wpa.wpa = vconf->wpa_pairwise_tkip
               || vconf->wpa_pairwise_ccmp;
        wpa.rsn = vconf->rsn_pairwise_tkip
               || vconf->rsn_pairwise_ccmp;
        wpa.pairwise_tkip = vconf->wpa_pairwise_tkip
                         || vconf->rsn_pairwise_tkip;
        wpa.pairwise_ccmp = vconf->wpa_pairwise_ccmp
                         || vconf->rsn_pairwise_ccmp;
        wpa.pairwise_ccmp256 = vconf->rsn_pairwise_ccmp256;
        wpa.pairwise_gcmp = vconf->rsn_pairwise_gcmp;
        wpa.pairwise_gcmp256 = vconf->rsn_pairwise_gcmp256;
        wpa.pmf = pmf_from_str(vconf->pmf);
        wpa.beacon_protection = vconf->beacon_protection;

        /* FIXME: Optimize to not overwrite it all the time */
        ow_conf_vif_flush_sta_net(vconf->if_name);
        ow_conf_vif_set_sta_net(vconf->if_name, &ssid, &bssid, &psk, &wpa, &bridge, &multi_ap, NULL);
    }
    else {
        ow_ovsdb_cconf_sched();
    }
}

static void
ow_ovsdb_vconf_to_ow_conf(const struct schema_Wifi_VIF_Config *vconf,
                          const bool is_new)
{
    enum osw_vif_type type = ow_ovsdb_mode_to_type(vconf->mode);

    if (is_new == true || vconf->enabled_changed == true) {
        if (vconf->enabled_exists == true) {
            bool x = vconf->enabled;
            ow_conf_vif_set_enabled(vconf->if_name, &x);
        }
        else {
            ow_conf_vif_set_enabled(vconf->if_name, NULL);
        }
    }

    if (is_new == true || vconf->mode_changed == true) {
        if (vconf->mode_exists == true) {
            ow_conf_vif_set_type(vconf->if_name, &type);
        }
        else {
            ow_conf_vif_set_type(vconf->if_name, NULL);
        }
    }

    switch (type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            ow_ovsdb_vconf_to_ow_conf_ap(vconf, is_new);
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            ow_ovsdb_vconf_to_ow_conf_sta(vconf, is_new);
            break;
    }
}

static void
callback_Wifi_VIF_Config(ovsdb_update_monitor_t *mon,
                         struct schema_Wifi_VIF_Config *old,
                         struct schema_Wifi_VIF_Config *vconf,
                         ovsdb_cache_row_t *row)
{
    const bool is_new = (mon->mon_type == OVSDB_UPDATE_NEW);
    LOGI("ow: ovsdb: vif config: %s: %s",
         vconf->if_name,
         (mon->mon_type == OVSDB_UPDATE_NEW ? "new" :
          mon->mon_type == OVSDB_UPDATE_MODIFY ? "modify" :
          mon->mon_type == OVSDB_UPDATE_DEL ? "del" :
          mon->mon_type == OVSDB_UPDATE_ERROR ? "error" :
          ""));

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            /* FIXME: This ow_conf_vif_unset() here
             * shouldn't be really necessary: Instead the
             * new-or-modiy should be idempotent via
             * ow_ovsdb_vif_sync().
             */
            ow_conf_vif_unset(vconf->if_name);

            ow_ovsdb_vstate_fix_vif_config(vconf);
            /* fall through */
        case OVSDB_UPDATE_MODIFY:
            {
                struct schema_Wifi_VIF_Config vconf2;
                memcpy(&vconf2, vconf, sizeof(vconf2));
                schema_vconf_unify(&vconf2);
                ow_ovsdb_vconf_to_ow_conf(&vconf2, is_new);
            }
            ow_ovsdb_vif_set_config(vconf->if_name, vconf);
            break;
        case OVSDB_UPDATE_DEL:
            ow_ovsdb_vif_set_config(vconf->if_name, NULL);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }

    ow_ovsdb_wps_op_handle_vconf_update(g_ow_ovsdb.wps, mon, old, vconf, row);
    ow_ovsdb_link_phy_vif();
}

static void
callback_Wifi_VIF_Neighbors(ovsdb_update_monitor_t *mon,
                            struct schema_Wifi_VIF_Neighbors *old,
                            struct schema_Wifi_VIF_Neighbors *vneigh,
                            ovsdb_cache_row_t *row)
{
    LOGI("ow: ovsdb: vif neighbors: %s: %s",
         vneigh->if_name,
         (mon->mon_type == OVSDB_UPDATE_NEW ? "new" :
          mon->mon_type == OVSDB_UPDATE_MODIFY ? "modify" :
          mon->mon_type == OVSDB_UPDATE_DEL ? "del" :
          mon->mon_type == OVSDB_UPDATE_ERROR ? "error" :
          ""));

    ow_ovsdb_vneigh_update_ft(mon, old, vneigh, row);

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            ow_ovsdb_vneigh_to_ow_conf_ap(vneigh, true);
            break;
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_vneigh_to_ow_conf_ap(vneigh, false);
            break;
        case OVSDB_UPDATE_DEL:
            {
                struct osw_hwaddr bssid;
                const bool ok = (osw_hwaddr_from_cstr(vneigh->bssid, &bssid) == true);

                if (ok == false) {
                    LOGW("ow: ovsdb: vif neighbors: %s invalid bssid", vneigh->bssid);
                    return;
                }

                ow_conf_vif_del_ap_neigh(vneigh->if_name,
                                         &bssid);
            }
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void
callback_RADIUS(ovsdb_update_monitor_t *mon,
                struct schema_RADIUS *old,
                struct schema_RADIUS *radius,
                ovsdb_cache_row_t *row)
{
    LOGD("ow: ovsdb: RADIUS callback: (%s:%d) %s",
         radius->ip_addr, radius->port,
         (mon->mon_type == OVSDB_UPDATE_NEW ? "new" :
          mon->mon_type == OVSDB_UPDATE_MODIFY ? "modify" :
          mon->mon_type == OVSDB_UPDATE_DEL ? "del" :
          mon->mon_type == OVSDB_UPDATE_ERROR ? "error" :
          ""));

    bool is_new = false;
    const char *uuid = radius->_uuid.uuid;
    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            is_new = true;
            /* fall through */
        case OVSDB_UPDATE_MODIFY:
            if (is_new == true || radius->ip_addr_changed == true)
                ow_conf_radius_set_ip_addr(uuid, radius->ip_addr);
            if (is_new == true || radius->secret_changed == true)
                ow_conf_radius_set_secret(uuid, radius->secret);
            if (is_new == true || radius->port_changed == true)
                ow_conf_radius_set_port(uuid, radius->port);
            break;
        case OVSDB_UPDATE_DEL:
            ow_conf_radius_unset(uuid);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void
ow_ovsdb_passpoint_to_ow_conf_ap(const struct schema_Passpoint_Config *pconf,
                                 bool is_new)
{
    const char *ref_id = pconf->_uuid.uuid;

    if (is_new || pconf->enabled_changed)
        ow_conf_passpoint_set_hs20_enabled(ref_id, &pconf->enabled);

    if (is_new || pconf->hessid_changed)
        ow_conf_passpoint_set_hessid(ref_id, pconf->hessid);

    if (is_new || pconf->osu_ssid_changed)
        ow_conf_passpoint_set_osu_ssid(ref_id, pconf->osu_ssid);

    if (is_new || pconf->adv_wan_status_changed)
        ow_conf_passpoint_set_adv_wan_status(ref_id, &pconf->adv_wan_status);

    if (is_new || pconf->adv_wan_symmetric_changed)
        ow_conf_passpoint_set_adv_wan_symmetric(ref_id, &pconf->adv_wan_symmetric);

    if (is_new || pconf->adv_wan_at_capacity_changed)
        ow_conf_passpoint_set_adv_wan_at_capacity(ref_id, &pconf->adv_wan_at_capacity);

    if (is_new || pconf->osen_changed)
        ow_conf_passpoint_set_osen(ref_id, &pconf->osen);

    if (is_new || pconf->asra_changed)
        ow_conf_passpoint_set_asra(ref_id, &pconf->asra);

    if (is_new || pconf->access_network_type_changed)
        ow_conf_passpoint_set_ant(ref_id, &pconf->access_network_type);

    if (is_new || pconf->venue_group_changed)
        ow_conf_passpoint_set_venue_group(ref_id, &pconf->venue_group);

    if (is_new || pconf->venue_type_changed)
        ow_conf_passpoint_set_venue_type(ref_id, &pconf->venue_type);

    if (is_new || pconf->anqp_domain_id_changed)
        ow_conf_passpoint_set_anqp_domain_id(ref_id, &pconf->anqp_domain_id);

    if (is_new || pconf->pps_mo_id_changed)
        ow_conf_passpoint_set_pps_mo_id(ref_id, &pconf->pps_mo_id);

    if (is_new || pconf->t_c_timestamp_changed)
        ow_conf_passpoint_set_t_c_timestamp(ref_id, &pconf->t_c_timestamp);

    if (is_new || pconf->t_c_filename_changed)
        ow_conf_passpoint_set_t_c_filename(ref_id, pconf->t_c_filename);

    if (is_new || pconf->anqp_elem_changed)
        ow_conf_passpoint_set_anqp_elem(ref_id, pconf->anqp_elem);

    if (is_new || pconf->domain_name_changed) {
        if (pconf->domain_name_len > 0) {
            int i;
            char **ptr = MALLOC(sizeof(*ptr) * pconf->domain_name_len);
            for (i = 0; i < pconf->domain_name_len; i++) {
                ptr[i] = STRDUP(pconf->domain_name[i]);
            }
            ow_conf_passpoint_set_domain_list(ref_id, ptr,
                                              pconf->domain_name_len);
        } else {
            ow_conf_passpoint_set_domain_list(ref_id, NULL, 0);
        }
    }

    if (is_new || pconf->nairealm_list_changed) {
        if (pconf->nairealm_list_len > 0) {
            int i;
            char **ptr = MALLOC(sizeof(*ptr) * pconf->nairealm_list_len);
            for (i = 0; i < pconf->nairealm_list_len; i++) {
                ptr[i] = STRDUP(pconf->nairealm_list[i]);
            }
            ow_conf_passpoint_set_nairealm_list(ref_id, ptr,
                                                pconf->nairealm_list_len);
        } else {
            ow_conf_passpoint_set_nairealm_list(ref_id, NULL, 0);
        }
    }

    if (is_new || pconf->roaming_consortium_changed) {
        if (pconf->roaming_consortium_len > 0) {
            int i;
            char **ptr = MALLOC(sizeof(*ptr) * pconf->roaming_consortium_len);
            for (i = 0; i < pconf->roaming_consortium_len; i++) {
                ptr[i] = STRDUP(pconf->roaming_consortium[i]);
            }
            ow_conf_passpoint_set_roamc(ref_id, ptr,
                                        pconf->roaming_consortium_len);
        } else {
            ow_conf_passpoint_set_roamc(ref_id, NULL, 0);
        }
    }

    if (is_new || pconf->operator_friendly_name_changed) {
        if (pconf->operator_friendly_name_len > 0) {
            int i;
            char **ptr = MALLOC(sizeof(*ptr) * pconf->operator_friendly_name_len);
            for (i = 0; i < pconf->operator_friendly_name_len; i++) {
                ptr[i] = STRDUP(pconf->operator_friendly_name[i]);
            }
            ow_conf_passpoint_set_oper_fname_list(ref_id, ptr,
                                                  pconf->operator_friendly_name_len);
        } else {
            ow_conf_passpoint_set_oper_fname_list(ref_id, NULL, 0);
        }
    }

    if (is_new || pconf->venue_name_changed) {
        if (pconf->venue_name_len > 0) {
            int i;
            char **ptr = MALLOC(sizeof(*ptr) * pconf->venue_name_len);
            for (i = 0; i < pconf->venue_name_len; i++) {
                ptr[i] = STRDUP(pconf->venue_name[i]);
            }
            ow_conf_passpoint_set_venue_name_list(ref_id, ptr,
                                                  pconf->venue_name_len);
        } else {
            ow_conf_passpoint_set_venue_name_list(ref_id, NULL, 0);
        }
    }

    if (is_new || pconf->venue_url_changed) {
        if (pconf->venue_url_len > 0) {
            int i;
            char **ptr = MALLOC(sizeof(*ptr) * pconf->venue_url_len);
            for (i = 0; i < pconf->venue_url_len; i++) {
                ptr[i] = STRDUP(pconf->venue_url[i]);
            }
            ow_conf_passpoint_set_venue_url_list(ref_id, ptr,
                                                 pconf->venue_url_len);
        } else {
            ow_conf_passpoint_set_venue_url_list(ref_id, NULL, 0);
        }
    }

    if (is_new || pconf->list_3gpp_changed) {
        if (pconf->list_3gpp_len > 0) {
            int i;
            char **ptr = MALLOC(sizeof(*ptr) * pconf->list_3gpp_len);
            for (i = 0; i < pconf->list_3gpp_len; i++) {
                ptr[i] = STRDUP(pconf->list_3gpp[i]);
            }
            ow_conf_passpoint_set_list_3gpp_list(ref_id, ptr,
                                                 pconf->list_3gpp_len);
        } else {
            ow_conf_passpoint_set_list_3gpp_list(ref_id, NULL, 0);
        }
    }

    if (is_new || pconf->network_auth_type_changed)
            ow_conf_passpoint_set_net_auth_type_list(ref_id, pconf->network_auth_type,
                                                     pconf->network_auth_type_len);
}

static void
callback_Passpoint_Config(ovsdb_update_monitor_t *mon,
                          struct schema_Passpoint_Config *old,
                          struct schema_Passpoint_Config *pconf,
                          ovsdb_cache_row_t *row)
{
    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            ow_ovsdb_passpoint_to_ow_conf_ap(pconf, true);
            break;
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_passpoint_to_ow_conf_ap(pconf, false);
            break;
        case OVSDB_UPDATE_DEL:
            ow_conf_passpoint_unset(pconf->_uuid.uuid);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static struct ev_timer g_ow_ovsdb_retry;

static void
ow_ovsdb_flush(void)
{
    /* FIXME: This shouldn't be necessary. The code should actually sync not
     * only rows, but also (non)existence of rows. One idea would be to employ
     * ovsdb ping and/or ev_idle (to allow real ovsdb transactions to be
     * delivered to the process) and then - based on what rows are reported -
     * remove those that shouldn't be there.
     *
     * With this the controller will see a blip whenever this module (and
     * typically the process) is restarted.
     */
    ovsdb_table_delete_where(&table_Wifi_Associated_Clients, NULL);
    ovsdb_table_delete_where(&table_Wifi_Radio_State, NULL);
    ovsdb_table_delete_where(&table_Wifi_VIF_State, NULL);
}

static void
ow_ovsdb_wps_changed_cb(void *priv,
                        const char *vif_name)
{
    struct ow_ovsdb *m = priv;
    struct ow_ovsdb_vif *vif = ds_tree_find(&m->vif_tree, vif_name);
    if (vif == NULL) return;
    ow_ovsdb_vif_work_sched(vif);
}

static void
ow_ovsdb_reattach_wps(struct ow_ovsdb *m)
{
    ow_ovsdb_wps_op_set_vconf_table(m->wps, &table_Wifi_VIF_Config);
    ow_ovsdb_wps_op_del_changed(m->wps,
                                m->wps_changed);
    m->wps_changed = ow_ovsdb_wps_op_add_changed(m->wps,
                                                 ow_ovsdb_wps_changed_cb,
                                                 m);
}

static void
ow_ovsdb_mld_redir_changed_cb(void *priv, const char *mld_name, const char *vif_redir, char **vifs)
{
    struct ow_ovsdb *m = priv;
    struct ow_ovsdb_vif *vif;
    /* Whenever redirection changes, all affected VIFs within an MLD
     * must be re-evaluated and re-reported. The non-redirected
     * (silenced) VIFs need to be reported as non-associated for
     * controller to make sense out of this.
     *
     * This only applies to non-MLO associations that happen on an
     * MLD. Some platforms require extra work to get these to work as
     * they use the underlying VIFs separately for multiple non-MLO
     * associations.
     */
    while (vifs && *vifs) {
        vif = ds_tree_find(&m->vif_tree, *vifs);
        ow_ovsdb_vif_work_sched(vif);
        vifs++;
    }
}

static void
ow_ovsdb_mld_redir_reattach(struct ow_ovsdb *m)
{
    m->ow_mld_redir = OSW_MODULE_LOAD(ow_mld_redir);
    ow_mld_redir_observer_drop(m->mld_redir_obs);
    m->mld_redir_obs = ow_mld_redir_observer_alloc(m->ow_mld_redir,
                                                   ow_ovsdb_mld_redir_changed_cb,
                                                   m);
}

static void
ow_ovsdb_retry_cb(EV_P_ ev_timer *arg, int events)
{
    if (ovsdb_init_loop(EV_A_ "OW") == false) {
        LOGI("ow: ovsdb: failed to connect, will retry later");
        return;
    }

    OVSDB_CACHE_MONITOR(Wifi_Radio_Config, true);
    OVSDB_CACHE_MONITOR(Wifi_VIF_Config, true);
    OVSDB_CACHE_MONITOR(Wifi_VIF_State, true);
    OVSDB_CACHE_MONITOR(Wifi_VIF_Neighbors, true);
    OVSDB_CACHE_MONITOR(RADIUS, true);
    OVSDB_CACHE_MONITOR(Passpoint_Config, true);

    ow_ovsdb_mld_redir_reattach(&g_ow_ovsdb);
    ow_ovsdb_mld_onboard_drop(g_ow_ovsdb.mld_onboard);
    g_ow_ovsdb.mld_onboard = ow_ovsdb_mld_onboard_alloc();
    ow_ovsdb_ms_init(&g_ow_ovsdb.ms, OW_OVSDB_CM_NEEDS_PORT_STATE_BLIP);
    ow_ovsdb_reattach_wps(&g_ow_ovsdb);
    ow_ovsdb_cconf_init(&table_Wifi_Radio_Config, &table_Wifi_VIF_Config);
    ow_ovsdb_stats_init();
    ow_ovsdb_hs_start(OSW_MODULE_LOAD(ow_ovsdb_hs));
    g_ow_ovsdb.steering = ow_ovsdb_steer_create();
    ow_ovsdb_flush();
    osw_state_register_observer(&g_ow_ovsdb_osw_state_obs);
    ev_timer_stop(EV_A_ arg);

    LOGI("ow: ovsdb: ready");
}

static void
ow_ovsdb_init(void)
{
    static bool initialized;

    if (initialized == true) return;

    OVSDB_TABLE_INIT(Wifi_Radio_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Radio_State, if_name);
    OVSDB_TABLE_INIT(Wifi_VIF_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_VIF_State, if_name);
    OVSDB_TABLE_INIT(Wifi_VIF_Neighbors, if_name);
    OVSDB_TABLE_INIT(Passpoint_Config, hessid);
    OVSDB_TABLE_INIT(Passpoint_OSU_Providers, osu_server_uri);
    OVSDB_TABLE_INIT(Wifi_Associated_Clients, mac);
    OVSDB_TABLE_INIT(Openflow_Tag, name);
    OVSDB_TABLE_INIT(RADIUS, name);

    ev_timer_init(&g_ow_ovsdb_retry, ow_ovsdb_retry_cb, 0, OW_OVSDB_RETRY_SECONDS);
    ev_timer_start(EV_DEFAULT_ &g_ow_ovsdb_retry);

    initialized = true;
}

static bool
ow_ovsdb_enabled(void)
{
    if (osw_etc_get("OW_OVSDB_DISABLED") == NULL)
        return true;
    else
        return false;
}

OSW_MODULE(ow_ovsdb)
{
    if (ow_ovsdb_enabled() == false) {
        LOGI("ow: ovsdb: disabled");
        return NULL;
    }

    ow_ovsdb_init();
    g_ow_ovsdb.wps = OSW_MODULE_LOAD(ow_ovsdb_wps);
    OSW_MODULE_LOAD(ow_conf);

    const bool auto_enable = true;
    ow_conf_ap_vlan_set_enabled(&auto_enable);

    return NULL;
}

struct ow_ovsdb_ut {
    ev_async async;
};

static void
ow_ovsdb_ut_async_cb(EV_P_ ev_async *arg, int events)
{
}

static void
ow_ovsdb_ut_echo_cb(int id, bool is_error, json_t *js, void *data)
{
    struct ow_ovsdb_ut *ut = data;
    ev_async_stop(EV_DEFAULT_ &ut->async);
    ev_unref(EV_DEFAULT);
}

static void
ow_ovsdb_ut_idle_cb(EV_P_ ev_idle *idle, int events)
{
    ev_idle_stop(EV_A_ idle);
}

static bool
ow_ovsdb_ut_is_runnable(void)
{
    if (ovsdb_init("")) {
        ovsdb_stop();
        return true;
    }
    else {
        return false;
    }
}

static bool
ow_ovsdb_ut_is_not_runnable(void)
{
    return !ow_ovsdb_ut_is_runnable();
}

static void
ow_ovsdb_ut_run(void)
{
    struct ow_ovsdb_ut ut;
    char *argv[] = { "ow_ovsdb" };
    struct ev_idle idle;

    ev_idle_init(&idle, ow_ovsdb_ut_idle_cb);
    ev_async_init(&ut.async, ow_ovsdb_ut_async_cb);

    ev_idle_start(EV_DEFAULT_ &idle);
    ev_unref(EV_DEFAULT);
    ev_run(EV_DEFAULT_ 0);
    ev_ref(EV_DEFAULT);

    assert(ovsdb_echo_call_argv(ow_ovsdb_ut_echo_cb, &ut, 1, argv) == true);
    ev_run(EV_DEFAULT_ 0);

    ev_idle_start(EV_DEFAULT_ &idle);
    ev_run(EV_DEFAULT_ 0);

    ev_ref(EV_DEFAULT);
    assert(ow_conf_is_settled() == true);
}

static void
ow_ovsdb_ut_init(void)
{
    osw_module_load_name("osw_drv");
    osw_module_load_name("ow_conf");
    ow_ovsdb_init();
    while (ev_is_active(&g_ow_ovsdb_retry))
        ev_run(EV_DEFAULT_ EVRUN_ONCE);

    ovsdb_table_delete_where(&table_Wifi_Radio_Config, NULL);
    ovsdb_table_delete_where(&table_Wifi_VIF_Config, NULL);
    ovsdb_table_delete_where(&table_Wifi_Radio_State, NULL);
    ovsdb_table_delete_where(&table_Wifi_VIF_State, NULL);
    ovsdb_table_delete_where(&table_Wifi_Associated_Clients, NULL);
    ovsdb_table_delete_where(&table_Openflow_Tag, NULL);
    ow_ovsdb_ut_run();
}

OSW_UT(ow_ovsdb_ut_rconf)
{
    if (ow_ovsdb_ut_is_not_runnable()) return;

    const char *phy_name = "phy1";
    struct schema_Wifi_Radio_Config rconf = {0};
    SCHEMA_SET_STR(rconf.if_name, phy_name);
    SCHEMA_SET_STR(rconf.freq_band, "2.4G");
    SCHEMA_SET_BOOL(rconf.enabled, true);
    /* tx_chainmask is left unset */

    ow_ovsdb_ut_init();
    ovsdb_table_insert(&table_Wifi_Radio_Config, &rconf);
    ow_ovsdb_ut_run();

    assert(ow_conf_phy_is_set(phy_name) == true);
    assert(ow_conf_phy_get_enabled(phy_name) != NULL);
    assert(ow_conf_phy_get_tx_chainmask(phy_name) == NULL);
    assert(*ow_conf_phy_get_enabled(phy_name) == rconf.enabled);

    ovsdb_table_delete(&table_Wifi_Radio_Config, &rconf);
    ow_ovsdb_ut_run();
    assert(ow_conf_phy_is_set(phy_name) == false);
}

OSW_UT(ow_ovsdb_ut_vconf)
{
    if (ow_ovsdb_ut_is_not_runnable()) return;

    const char *phy_name = "phy1";
    struct schema_Wifi_Radio_Config rconf = {0};
    SCHEMA_SET_STR(rconf.if_name, phy_name);
    SCHEMA_SET_STR(rconf.freq_band, "2.4G");
    SCHEMA_SET_BOOL(rconf.enabled, true);

    const char *vif_name = "vif1";
    const bool update_uuid = true;
    struct schema_Wifi_VIF_Config vconf = {0};
    void *filter = NULL;
    SCHEMA_SET_STR(vconf.if_name, vif_name);
    SCHEMA_SET_BOOL(vconf.enabled, true);
    SCHEMA_SET_STR(vconf.ssid, "hello_ssid");
    SCHEMA_SET_STR(vconf.mode, "ap");

    ow_ovsdb_ut_init();
    json_t *where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name),
                                       phy_name);
    assert(where != NULL);
    ovsdb_table_insert(&table_Wifi_Radio_Config, &rconf);
    ovsdb_table_upsert_with_parent(&table_Wifi_VIF_Config,
                                   &vconf,
                                   update_uuid,
                                   filter,
                                   SCHEMA_TABLE(Wifi_Radio_Config),
                                   where,
                                   SCHEMA_COLUMN(Wifi_Radio_Config, vif_configs));
    ow_ovsdb_ut_run();

    assert(ow_conf_phy_is_set(phy_name) == true);
    assert(ow_conf_vif_is_set(vif_name) == true);
    assert(ow_conf_vif_get_phy_name(vif_name) != NULL);
    assert(strcmp(ow_conf_vif_get_phy_name(vif_name), phy_name) == 0);
    assert(ow_conf_vif_get_enabled(vif_name) != NULL);
    assert(ow_conf_vif_get_ap_ssid(vif_name) != NULL);
    assert(strcmp(ow_conf_vif_get_ap_ssid(vif_name)->buf, vconf.ssid) == 0);

    ovsdb_table_delete(&table_Wifi_Radio_Config, &rconf);
    ow_ovsdb_ut_run();

    assert(ow_conf_phy_is_set(phy_name) == false);
    assert(ow_conf_vif_is_set(vif_name) == true);
    assert(ow_conf_vif_get_phy_name(vif_name) == NULL);

    rconf.vif_configs_len = 1;
    STRSCPY_WARN(rconf.vif_configs[0].uuid, vconf._uuid.uuid);
    ovsdb_table_insert(&table_Wifi_Radio_Config, &rconf);
    ow_ovsdb_ut_run();
    assert(ow_conf_phy_is_set(phy_name) == true);
    assert(ow_conf_vif_is_set(vif_name) == true);
    assert(ow_conf_vif_get_phy_name(vif_name) != NULL);
    assert(strcmp(ow_conf_vif_get_phy_name(vif_name), phy_name) == 0);
}

OSW_UT(ow_ovsdb_ut_rstate)
{
    if (ow_ovsdb_ut_is_not_runnable()) return;

    struct schema_Wifi_Radio_State rstate = {0};
    const struct osw_state_phy_info phy = {
        .phy_name = "phy1",
        .drv_state = (const struct osw_drv_phy_state []){{
            .exists = true,
            .enabled = true,
            .tx_chainmask = 0x0f,
        }}
    };

    ow_ovsdb_ut_init();
    ow_ovsdb_phy_added_cb(NULL, &phy);
    ow_ovsdb_ut_run();
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);

    assert(rstate.enabled_exists == true);
    assert(rstate.enabled == phy.drv_state->enabled);
    assert(rstate.tx_chainmask_exists == true);
    assert(rstate.tx_chainmask == phy.drv_state->tx_chainmask);
}

OSW_UT(ow_ovsdb_ut_rstate_bcn_int)
{
    if (ow_ovsdb_ut_is_not_runnable()) return;

    struct schema_Wifi_Radio_State rstate = {0};
    const struct osw_state_phy_info phy = {
        .phy_name = "phy1",
        .drv_state = (const struct osw_drv_phy_state []){{
            .exists = true,
            .enabled = true,
        }}
    };
    const struct osw_channel c = {
        .control_freq_mhz = 2412,
        .center_freq0_mhz = 2412,
    };
    const struct osw_state_vif_info vif1_100 = {
        .phy = &phy,
        .vif_name = "vif1",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = c, .beacon_interval_tu = 100 } },
        }}
    };
    const struct osw_state_vif_info vif1_200 = {
        .phy = &phy,
        .vif_name = "vif1",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = c, .beacon_interval_tu = 200 } },
        }}
    };
    const struct osw_state_vif_info vif2 = {
        .phy = &phy,
        .vif_name = "vif2",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = c, .beacon_interval_tu = 200 } },
        }}
    };
    struct osw_drv_dummy dummy = {
        .name = "rstate",
    };
    struct schema_Wifi_VIF_Config vconf1 = {0};
    struct schema_Wifi_VIF_Config vconf2 = {0};

    SCHEMA_SET_STR(vconf1.if_name, vif1_100.vif_name);
    SCHEMA_SET_STR(vconf2.if_name, vif2.vif_name);

    osw_module_load_name("osw_drv");
    osw_drv_dummy_init(&dummy);

    ow_ovsdb_ut_init();
    osw_drv_dummy_set_phy(&dummy, phy.phy_name, (void *)phy.drv_state);
    ow_ovsdb_ut_run();
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    assert(rstate.enabled_exists == true);
    assert(rstate.enabled == phy.drv_state->enabled);

    ow_ovsdb_vif_set_config(vconf1.if_name, &vconf1);
    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif1_100.vif_name, (void *)vif1_100.drv_state);
    ow_ovsdb_ut_run();
    assert(osw_state_vif_lookup(phy.phy_name, vif1_100.vif_name) != NULL);
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    assert(rstate.bcn_int_exists == true);
    assert(rstate.bcn_int == vif1_100.drv_state->u.ap.beacon_interval_tu);

    ow_ovsdb_vif_set_config(vconf2.if_name, &vconf2);
    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif2.vif_name, (void *)vif2.drv_state);
    ow_ovsdb_ut_run();
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    assert(rstate.bcn_int_exists == false);

    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif1_200.vif_name, (void *)vif1_200.drv_state);
    ow_ovsdb_ut_run();
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    assert(rstate.bcn_int_exists == true);
    assert(rstate.bcn_int == vif1_200.drv_state->u.ap.beacon_interval_tu);
}

OSW_UT(ow_ovsdb_ut_vstate)
{
    if (ow_ovsdb_ut_is_not_runnable()) return;

    struct schema_Wifi_Radio_State rstate = {0};
    struct schema_Wifi_VIF_State vstate = {0};
    const struct osw_state_phy_info phy = {
        .phy_name = "phy1",
        .drv_state = (const struct osw_drv_phy_state []){{
            .exists = true,
            .enabled = true,
        }}
    };
    const struct osw_state_vif_info vif = {
        .phy = &phy,
        .vif_name = "vif1",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .ssid = { .buf = "ssid123", .len = 7 } } },
        }}
    };
    struct schema_Wifi_VIF_Config vconf = {0};

    SCHEMA_SET_STR(vconf.if_name, vif.vif_name);

    ow_ovsdb_ut_init();
    ow_ovsdb_phy_added_cb(NULL, &phy);
    ow_ovsdb_vif_added_cb(NULL, &vif);
    ow_ovsdb_vif_set_config(vif.vif_name, &vconf);
    ow_ovsdb_ut_run();
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    ow_ovsdb_get_vstate(&vstate, vif.vif_name);

    assert(vstate.enabled_exists == true);
    assert((vstate.enabled ? OSW_VIF_ENABLED : OSW_VIF_DISABLED) == vif.drv_state->status);
    assert(vstate.ssid_exists == true);
    assert(strcmp(vstate.ssid, vif.drv_state->u.ap.ssid.buf) == 0);
    assert(rstate.vif_states_len == 1);
    assert(strcmp(rstate.vif_states[0].uuid, vstate._uuid.uuid) == 0);

    assert(vstate.vif_config_exists == false);

    struct schema_Wifi_Radio_Config rconf = {0};
    SCHEMA_SET_STR(rconf.if_name, phy.phy_name);
    SCHEMA_SET_STR(rconf.freq_band, "2.4G");
    SCHEMA_SET_BOOL(rconf.enabled, phy.drv_state->enabled);

    const bool update_uuid = false;
    void *filter = NULL;
    memset(&vconf, 0, sizeof(vconf));
    SCHEMA_SET_STR(vconf.if_name, vif.vif_name);
    switch (vif.drv_state->status) {
        case OSW_VIF_UNKNOWN:
        case OSW_VIF_BROKEN:
            break;
        case OSW_VIF_ENABLED:
            SCHEMA_SET_BOOL(vconf.enabled, true);
            break;
        case OSW_VIF_DISABLED:
            SCHEMA_SET_BOOL(vconf.enabled, false);
            break;
    }
    SCHEMA_SET_STR(vconf.mode, "ap");
    SCHEMA_SET_STR(vconf.ssid, vif.drv_state->u.ap.ssid.buf);

    json_t *where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name),
                                       phy.phy_name);
    assert(where != NULL);
    ovsdb_table_insert(&table_Wifi_Radio_Config, &rconf);
    ovsdb_table_upsert_with_parent(&table_Wifi_VIF_Config,
                                   &vconf,
                                   update_uuid,
                                   filter,
                                   SCHEMA_TABLE(Wifi_Radio_Config),
                                   where,
                                   SCHEMA_COLUMN(Wifi_Radio_Config, vif_configs));

    ow_ovsdb_ut_run();
    ow_ovsdb_get_vstate(&vstate, vif.vif_name);
    assert(vstate.vif_config_exists == true);
    ow_ovsdb_vif_removed_cb(NULL, &vif);
    ow_ovsdb_ut_run();
    assert(ow_ovsdb_get_vstate(&vstate, vif.vif_name) == false);
    ow_ovsdb_vif_added_cb(NULL, &vif);
    ow_ovsdb_ut_run();
    assert(ow_ovsdb_get_vstate(&vstate, vif.vif_name) == true);
    assert(vstate.vif_config_exists == true);
}

OSW_UT(ow_ovsdb_ut_rstate_channel)
{
    if (ow_ovsdb_ut_is_not_runnable()) return;

    struct schema_Wifi_Radio_State rstate = {0};
    const struct osw_state_phy_info phy = {
        .phy_name = "phy1",
        .drv_state = (const struct osw_drv_phy_state []){{
            .exists = true,
            .enabled = true,
        }}
    };
    const struct osw_state_vif_info vif1_c1w20 = {
        .phy = &phy,
        .vif_name = "vif1",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = { .control_freq_mhz = 2412, .center_freq0_mhz = 2412, .width = OSW_CHANNEL_20MHZ} } },
        }}
    };
    const struct osw_state_vif_info vif1_c6w20 = {
        .phy = &phy,
        .vif_name = "vif1",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = { .control_freq_mhz = 2437, .center_freq0_mhz = 2437, .width = OSW_CHANNEL_20MHZ} } },
        }}
    };
    const struct osw_state_vif_info vif2_c1w20 = {
        .phy = &phy,
        .vif_name = "vif2",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = { .control_freq_mhz = 2412, .center_freq0_mhz = 2412, .width = OSW_CHANNEL_20MHZ} } },
        }}
    };
    const struct osw_state_vif_info vif2_c6w20 = {
        .phy = &phy,
        .vif_name = "vif2",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = { .control_freq_mhz = 2437, .center_freq0_mhz = 2437, .width = OSW_CHANNEL_20MHZ} } },
        }}
    };
    const struct osw_state_vif_info vif2_c1w40 = {
        .phy = &phy,
        .vif_name = "vif2",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = { .control_freq_mhz = 2412, .center_freq0_mhz = 2422, .width = OSW_CHANNEL_40MHZ} } },
        }}
    };
    struct osw_drv_dummy dummy = {
        .name = "rstate",
    };
    struct schema_Wifi_VIF_Config vconf1 = {0};
    struct schema_Wifi_VIF_Config vconf2 = {0};

    SCHEMA_SET_STR(vconf1.if_name, vif1_c1w20.vif_name);
    SCHEMA_SET_STR(vconf2.if_name, vif2_c1w20.vif_name);

    osw_module_load_name("osw_drv");
    osw_drv_dummy_init(&dummy);
    osw_ut_time_init();

    ow_ovsdb_ut_init();
    osw_drv_dummy_set_phy(&dummy, phy.phy_name, (void *)phy.drv_state);
    ow_ovsdb_ut_run();
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    assert(rstate.enabled_exists == true);
    assert(rstate.enabled == phy.drv_state->enabled);

    ow_ovsdb_vif_set_config(vconf1.if_name, &vconf1);
    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif1_c1w20.vif_name, (void *)vif1_c1w20.drv_state);
    ow_ovsdb_ut_run();
    assert(osw_state_vif_lookup(phy.phy_name, vif1_c1w20.vif_name) != NULL);
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    assert(rstate.channel_exists == true);
    assert(rstate.ht_mode_exists == true);
    assert(rstate.channel == 1);
    assert(strcmp(rstate.ht_mode, "HT20") == 0);

    ow_ovsdb_vif_set_config(vconf2.if_name, &vconf2);
    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif2_c1w20.vif_name, (void *)vif2_c1w20.drv_state);
    ow_ovsdb_ut_run();
    assert(osw_state_vif_lookup(phy.phy_name, vif2_c1w20.vif_name) != NULL);
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    assert(rstate.channel_exists == true);
    assert(rstate.ht_mode_exists == true);
    assert(rstate.channel == 1);
    assert(strcmp(rstate.ht_mode, "HT20") == 0);

    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif2_c1w40.vif_name, (void *)vif2_c1w40.drv_state);
    ow_ovsdb_ut_run();
    assert(osw_state_vif_lookup(phy.phy_name, vif2_c1w40.vif_name) != NULL);
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    ASSERT(rstate.channel_exists == true, "");
    ASSERT(rstate.ht_mode_exists == true, "");
    osw_ut_time_advance(OSW_TIME_SEC(3));
    ow_ovsdb_ut_run();
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    ASSERT(rstate.channel_exists == true, "");
    ASSERT(rstate.ht_mode_exists == true, "");
    osw_ut_time_advance(OSW_TIME_SEC(2 + 1));
    ow_ovsdb_ut_run();
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    ASSERT(rstate.channel_exists == false, "");
    ASSERT(rstate.ht_mode_exists == false, "");

    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif2_c1w20.vif_name, (void *)vif2_c1w20.drv_state);
    ow_ovsdb_ut_run();
    assert(osw_state_vif_lookup(phy.phy_name, vif2_c6w20.vif_name) != NULL);
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    ASSERT(rstate.channel_exists == true, "");
    ASSERT(rstate.ht_mode_exists == true, "");
    ASSERT(rstate.channel == 1, "");

    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif2_c6w20.vif_name, (void *)vif2_c6w20.drv_state);
    ow_ovsdb_ut_run();
    assert(osw_state_vif_lookup(phy.phy_name, vif2_c6w20.vif_name) != NULL);
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    ASSERT(rstate.channel_exists == true, "");
    ASSERT(rstate.ht_mode_exists == true, "");
    ASSERT(rstate.channel == 1, "");
    osw_ut_time_advance(OSW_TIME_SEC(3));
    ow_ovsdb_ut_run();
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    ASSERT(rstate.channel_exists == true, "");
    ASSERT(rstate.ht_mode_exists == true, "");
    ASSERT(rstate.channel == 1, "");

    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif1_c1w20.vif_name, (void *)vif1_c6w20.drv_state);
    osw_ut_time_advance(OSW_TIME_SEC(0));
    ow_ovsdb_ut_run();
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    ASSERT(rstate.channel_exists == true, "");
    ASSERT(rstate.ht_mode_exists == true, "");
    ASSERT(rstate.channel == 6, "");
}

OSW_UT(ow_ovsdb_ut_freq_band)
{
    if (ow_ovsdb_ut_is_not_runnable()) return;

    struct osw_drv_phy_state state = {0};
    struct osw_channel_state cs_2g[] = {
            { .channel = { .control_freq_mhz = 2412 } },
    };
    struct osw_channel_state cs_5gl[] = {
            { .channel = { .control_freq_mhz = 5180 } },
    };
    struct osw_channel_state cs_5gu[] = {
            { .channel = { .control_freq_mhz = 5745 } },
    };
    struct osw_channel_state cs_5g[] = {
            { .channel = { .control_freq_mhz = 5180 } },
            { .channel = { .control_freq_mhz = 5600 } },
    };
    struct osw_channel_state cs_6g2[] = {
            { .channel = { .control_freq_mhz = 6935 } },
    };
    struct osw_channel_state cs_6g1[] = {
            { .channel = { .control_freq_mhz = 6955 } },
    };
    struct osw_channel_state cs_2g5g[] = {
            { .channel = { .control_freq_mhz = 2412 } },
            { .channel = { .control_freq_mhz = 5180 } },
            { .channel = { .control_freq_mhz = 5600 } },
    };
    struct osw_state_phy_info info = { .drv_state = &state };
    const char *str;

    state.channel_states = cs_2g;
    state.n_channel_states = ARRAY_SIZE(cs_2g);
    str = ow_ovsdb_phystate_get_freq_band(&info);
    assert(str != NULL);
    assert(strcmp(str, "2.4G") == 0);

    state.channel_states = cs_5gl;
    state.n_channel_states = ARRAY_SIZE(cs_5gl);
    str = ow_ovsdb_phystate_get_freq_band(&info);
    assert(str != NULL);
    assert(strcmp(str, "5GL") == 0);

    state.channel_states = cs_5gu;
    state.n_channel_states = ARRAY_SIZE(cs_5gu);
    str = ow_ovsdb_phystate_get_freq_band(&info);
    assert(str != NULL);
    assert(strcmp(str, "5GU") == 0);

    state.channel_states = cs_5g;
    state.n_channel_states = ARRAY_SIZE(cs_5g);
    str = ow_ovsdb_phystate_get_freq_band(&info);
    assert(str != NULL);
    assert(strcmp(str, "5G") == 0);

    state.channel_states = cs_6g1;
    state.n_channel_states = ARRAY_SIZE(cs_6g1);
    str = ow_ovsdb_phystate_get_freq_band(&info);
    assert(str != NULL);
    assert(strcmp(str, "6G") == 0);

    state.channel_states = cs_6g2;
    state.n_channel_states = ARRAY_SIZE(cs_6g2);
    str = ow_ovsdb_phystate_get_freq_band(&info);
    assert(str != NULL);
    assert(strcmp(str, "6G") == 0);

    state.channel_states = cs_2g5g;
    state.n_channel_states = ARRAY_SIZE(cs_2g5g);
    str = ow_ovsdb_phystate_get_freq_band(&info);
    assert(str == NULL);

    state.channel_states = NULL;
    state.n_channel_states = 0;
    str = ow_ovsdb_phystate_get_freq_band(&info);
    assert(str == NULL);
}

OSW_UT(ow_ovsdb_ut_phy_mem)
{
    if (ow_ovsdb_ut_is_not_runnable()) return;

    struct osw_drv_phy_state pstate = {0};
    struct osw_state_phy_info pinfo = {
        .phy_name = "phy1",
        .drv_state = &pstate,
    };
    struct schema_Wifi_Radio_Config rconf = {0};

    SCHEMA_SET_STR(rconf.if_name, pinfo.phy_name);

    ow_ovsdb_ut_init();
    ow_ovsdb_phy_set_info(pinfo.phy_name, &pinfo);
    ow_ovsdb_ut_run();
    assert(ds_tree_find(&g_ow_ovsdb.phy_tree, pinfo.phy_name) != NULL);

    ow_ovsdb_phy_set_info(pinfo.phy_name, NULL);
    ow_ovsdb_ut_run();
    assert(ds_tree_find(&g_ow_ovsdb.phy_tree, pinfo.phy_name) == NULL);

    ow_ovsdb_phy_set_config(pinfo.phy_name, &rconf);
    ow_ovsdb_ut_run();
    assert(ds_tree_find(&g_ow_ovsdb.phy_tree, pinfo.phy_name) != NULL);

    ow_ovsdb_phy_set_info(pinfo.phy_name, &pinfo);
    ow_ovsdb_ut_run();
    assert(ds_tree_find(&g_ow_ovsdb.phy_tree, pinfo.phy_name) != NULL);

    ow_ovsdb_phy_set_config(pinfo.phy_name, NULL);
    ow_ovsdb_ut_run();
    assert(ds_tree_find(&g_ow_ovsdb.phy_tree, pinfo.phy_name) != NULL);

    ow_ovsdb_phy_set_config(pinfo.phy_name, &rconf);
    ow_ovsdb_ut_run();
    assert(ds_tree_find(&g_ow_ovsdb.phy_tree, pinfo.phy_name) != NULL);

    ow_ovsdb_phy_set_info(pinfo.phy_name, NULL);
    ow_ovsdb_ut_run();
    assert(ds_tree_find(&g_ow_ovsdb.phy_tree, pinfo.phy_name) != NULL);

    ow_ovsdb_phy_set_config(pinfo.phy_name, NULL);
    ow_ovsdb_ut_run();
    assert(ds_tree_find(&g_ow_ovsdb.phy_tree, pinfo.phy_name) == NULL);
}

OSW_UT(ow_ovsdb_ut_sta)
{
    if (ow_ovsdb_ut_is_not_runnable()) return;

    struct osw_channel c = {
        .control_freq_mhz = 2412,
        .center_freq0_mhz = 2412,
    };
    struct osw_channel_state cs_2g[] = {
            { .channel = c },
    };
    const struct osw_state_phy_info phy = {
        .phy_name = "phy1",
        .drv_state = (const struct osw_drv_phy_state []){{
            .exists = true,
            .enabled = true,
            .n_channel_states = ARRAY_SIZE(cs_2g),
            .channel_states = cs_2g,
        }},
    };
    const struct osw_state_vif_info vif1 = {
        .phy = &phy,
        .vif_name = "vif1",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = c, .ssid = { .buf = "ssid1", .len = 5 } } },
        }},
    };
    const struct osw_state_vif_info vif2 = {
        .phy = &phy,
        .vif_name = "vif2",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .status = OSW_VIF_ENABLED,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = c, .ssid = { .buf = "ssid2", .len = 5 } } },
        }},
    };
    const struct osw_hwaddr addr1 = { .octet = { 0, 1, 2, 3, 4, 5 } };
    struct osw_drv_sta_state k1conn = {
        .key_id = 1,
        .connected = true,
    };
    struct osw_drv_sta_state k1disc = {
        .key_id = 1,
        .connected = false,
    };
    struct osw_drv_dummy dummy = {
        .name = "ut_sta",
    };
    struct schema_Wifi_VIF_Config vconf1 = {0};
    struct schema_Wifi_VIF_Config vconf2 = {0};

    SCHEMA_SET_STR(vconf1.if_name, vif1.vif_name);
    SCHEMA_KEY_VAL_APPEND(vconf1.wpa_oftags, "key-1", "hello");
    SCHEMA_KEY_VAL_APPEND(vconf1.security, "oftag-key-2", "world");

    SCHEMA_SET_STR(vconf2.if_name, vif2.vif_name);
    SCHEMA_KEY_VAL_APPEND(vconf1.wpa_oftags, "key-1", "hello");
    SCHEMA_KEY_VAL_APPEND(vconf1.security, "oftag-key-2", "world");

    osw_module_load_name("osw_drv");
    osw_drv_dummy_init(&dummy);

    ow_ovsdb_ut_init();
    osw_drv_dummy_set_phy(&dummy, phy.phy_name, (void *)phy.drv_state);
    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif1.vif_name, (void *)vif1.drv_state);
    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif2.vif_name, (void *)vif2.drv_state);
    osw_drv_dummy_set_sta(&dummy, phy.phy_name, vif1.vif_name, &addr1, &k1conn);
    ow_ovsdb_vif_set_config(vif1.vif_name, &vconf1);
    ow_ovsdb_vif_set_config(vif2.vif_name, &vconf2);
    ow_ovsdb_ut_run();

    osw_drv_dummy_set_sta(&dummy, phy.phy_name, vif1.vif_name, &addr1, &k1disc);
    ow_ovsdb_ut_run();

    osw_drv_dummy_set_sta(&dummy, phy.phy_name, vif1.vif_name, &addr1, &k1conn);
    ow_ovsdb_ut_run();

    osw_drv_dummy_set_sta(&dummy, phy.phy_name, vif2.vif_name, &addr1, &k1conn);
    ow_ovsdb_ut_run();

    osw_drv_dummy_set_sta(&dummy, phy.phy_name, vif1.vif_name, &addr1, &k1disc);
    ow_ovsdb_ut_run();

    osw_drv_dummy_fini(&dummy);
    ow_ovsdb_ut_run();
}
