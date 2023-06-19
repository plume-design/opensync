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
#include <memutil.h>
#include <iso3166.h>
#include <osw_drv_dummy.h>
#include <osw_state.h>
#include <osw_ut.h>
#include <osw_module.h>
#include "ow_conf.h"
#include "ow_ovsdb_ms.h"
#include "ow_ovsdb_wps.h"
#include "ow_ovsdb_steer.h"
#include "ow_ovsdb_cconf.h"
#include "ow_ovsdb_stats.h"
#include <ovsdb.h>
#include <ovsdb_table.h>
#include <ovsdb_cache.h>
#include <ovsdb_sync.h>
#include <schema_consts.h>
#include <schema_compat.h>

#define OW_OVSDB_RETRY_SECONDS 5.0
#define OW_OVSDB_VIF_FLAG_SEEN 0x01
#define OW_OVSDB_CM_NEEDS_PORT_STATE_BLIP 1

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

struct ow_ovsdb {
    struct ds_tree phy_tree;
    struct ds_tree vif_tree;
    struct ds_tree sta_tree;
    struct ow_ovsdb_ms_root ms;
    struct ow_ovsdb_wps_ops *wps;
    struct ow_ovsdb_wps_changed *wps_changed;
    struct ow_ovsdb_steer *steering;
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
    char *phy_name;
    ev_timer work;
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
};

struct ow_ovsdb_sta {
    struct ds_tree_node node;
    struct ow_ovsdb *root;
    const struct osw_state_sta_info *info;
    struct schema_Wifi_Associated_Clients state_cur;
    struct schema_Wifi_Associated_Clients state_new;
    struct osw_hwaddr sta_addr;
    char *vif_name;
    char *oftag;
    time_t connected_at;
    ev_timer work;
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
};

static bool
ow_ovsdb_vif_treat_deleted_as_disabled(enum osw_vif_type vif_type)
{
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

static const char *
ow_ovsdb_width_to_htmode(enum osw_channel_width w)
{
    switch (w) {
        case OSW_CHANNEL_20MHZ: return "HT20";
        case OSW_CHANNEL_40MHZ: return "HT40";
        case OSW_CHANNEL_80MHZ: return "HT80";
        case OSW_CHANNEL_160MHZ: return "HT160";
        case OSW_CHANNEL_80P80MHZ: return "HT8080";
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
    if (info->drv_state->enabled == false) return;
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

    if (info->drv_state->enabled == false) return;
    if (info->drv_state->vif_type != OSW_VIF_AP) return;
    ow_ovsdb_phystate_get_channel_h_chan(i, priv);
}

static void
ow_ovsdb_phystate_get_channel_h_sta(const struct osw_state_vif_info *info,
                                    void *priv)
{
    const struct osw_channel *i = &info->drv_state->u.sta.link.channel;

    if (info->drv_state->enabled == false) return;
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
ow_ovsdb_phystate_fill_channel(struct schema_Wifi_Radio_State *schema,
                               const struct osw_state_phy_info *phy)
{
    struct osw_channel channel = {0};
    osw_state_vif_get_list(ow_ovsdb_phystate_get_channel_iter_cb,
                           phy->phy_name,
                           &channel);
    if (channel.control_freq_mhz > 0) {
        int num = ow_ovsdb_freq_to_chan(channel.control_freq_mhz);
        if (num > 0) SCHEMA_SET_INT(schema->channel, num);

        const char *ht_mode = ow_ovsdb_width_to_htmode(channel.width);
        if (ht_mode != NULL) SCHEMA_SET_STR(schema->ht_mode, ht_mode);
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

    if (info->drv_state->enabled == false) return;
    if (info->drv_state->vif_type != OSW_VIF_AP) return;

    m->ht_enabled |= i->ht_enabled;
    m->vht_enabled |= i->vht_enabled;
    m->he_enabled |= i->he_enabled;
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

    if (mode.he_enabled)
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

static void
ow_ovsdb_phystate_to_schema(struct schema_Wifi_Radio_State *schema,
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
     * However some arguably should be dervied from the
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
    ow_ovsdb_phystate_fill_channel(schema, phy);
    ow_ovsdb_phystate_fill_tx_power(schema, phy);
    ow_ovsdb_phystate_fill_hwmode(schema, freq_band, phy);
    ow_ovsdb_phystate_fill_allowed_channels(schema, phy);
    ow_ovsdb_phystate_fill_channels(schema, phy);
    ow_ovsdb_phystate_fill_regulatory(schema, rconf, phy);
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

    if (wpa->akm_psk)    SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK);
    if (wpa->akm_sae)    SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_SAE);
    if (wpa->akm_ft_psk) SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_PSK);
    if (wpa->akm_ft_sae) SCHEMA_VAL_APPEND(schema->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_SAE);

    SCHEMA_SET_BOOL(schema->wpa_pairwise_tkip, wpa->wpa && wpa->pairwise_tkip);
    SCHEMA_SET_BOOL(schema->wpa_pairwise_ccmp, wpa->wpa && wpa->pairwise_ccmp);
    SCHEMA_SET_BOOL(schema->rsn_pairwise_tkip, wpa->rsn && wpa->pairwise_tkip);
    SCHEMA_SET_BOOL(schema->rsn_pairwise_ccmp, wpa->rsn && wpa->pairwise_ccmp);
    SCHEMA_SET_STR(schema->pmf, pmf_to_str(wpa->pmf));
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

static void
ow_ovsdb_vifstate_fill_ap_acl(struct schema_Wifi_VIF_State *schema,
                              const struct osw_hwaddr_list *acl)
{
    size_t i;
    for (i = 0; i < acl->count; i++) {
        struct osw_hwaddr_str buf;
        const char *str = osw_hwaddr2str(&acl->list[i], &buf);

        if (WARN_ON(i >= ARRAY_SIZE(schema->mac_list)))
            break;

        SCHEMA_VAL_APPEND(schema->mac_list, str);
    }
}

static bool
ow_ovsdb_min_hw_mode_is_not_supported(void)
{
    return getenv("OW_OVSDB_MIN_HW_MODE_NOT_SUPPORTED") != NULL;
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
    else {
        SCHEMA_CPY_STR(vstate->min_hw_mode, vconf->min_hw_mode);
    }
}

static void
ow_ovsdb_vifstate_to_schema(struct schema_Wifi_VIF_State *schema,
                            const struct schema_Wifi_VIF_Config *vconf,
                            const struct osw_state_vif_info *vif)
{
    const struct osw_drv_vif_state_ap *ap = &vif->drv_state->u.ap;
    const struct osw_drv_vif_state_ap_vlan *ap_vlan = &vif->drv_state->u.ap_vlan;
    const struct osw_drv_vif_state_sta *vsta = &vif->drv_state->u.sta;
    struct osw_hwaddr_str mac;
    int c;

    SCHEMA_SET_STR(schema->if_name, vif->vif_name);
    SCHEMA_SET_BOOL(schema->enabled, vif->drv_state->enabled);
    SCHEMA_SET_STR(schema->mac, osw_hwaddr2str(&vif->drv_state->mac_addr, &mac));

    switch (vif->drv_state->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            SCHEMA_SET_STR(schema->mode, "ap");
            SCHEMA_SET_STR(schema->ssid, ap->ssid.buf);
            SCHEMA_SET_STR(schema->ssid_broadcast, ap->ssid_hidden ? "disabled" : "enabled");
            SCHEMA_SET_STR(schema->bridge, ap->bridge_if_name.buf);
            SCHEMA_SET_INT(schema->ft_mobility_domain, ap->wpa.ft_mobility_domain);
            SCHEMA_SET_BOOL(schema->wpa, ap->wpa.wpa || ap->wpa.rsn);
            SCHEMA_SET_BOOL(schema->ap_bridge, ap->isolated ? false : true);
            SCHEMA_SET_BOOL(schema->btm, ap->mode.wnm_bss_trans);
            SCHEMA_SET_BOOL(schema->rrm, ap->mode.rrm_neighbor_report);
            SCHEMA_SET_BOOL(schema->wps, ap->mode.wps);
            SCHEMA_SET_BOOL(schema->uapsd_enable, ap->mode.wmm_uapsd_enabled);
            SCHEMA_SET_BOOL(schema->mcast2ucast, ap->mcast2ucast);
            SCHEMA_SET_BOOL(schema->dynamic_beacon, false); // FIXME
            SCHEMA_SET_STR(schema->multi_ap, ow_ovsdb_ap_multi_ap_to_cstr(&ap->multi_ap));

            {
                const char *acl_policy_str = ow_ovsdb_acl_policy_to_str(ap->acl_policy);
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
            ow_ovsdb_vifstate_fill_ap_acl(schema, &ap->acl);
            ow_ovsdb_wps_op_fill_vstate(g_ow_ovsdb.wps, schema);

            const uint32_t freq = ap->channel.control_freq_mhz;
            const enum osw_band band = osw_freq_to_band(freq);
            ow_ovsdb_vifstate_fill_min_hw_mode(schema, vconf, &ap->mode, band);
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
            switch (vsta->link.status) {
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTED:
                    SCHEMA_SET_STR(schema->ssid, vsta->link.ssid.buf);
                    if (osw_hwaddr_is_zero(&vsta->link.bssid) == false) {
                        SCHEMA_SET_STR(schema->parent, osw_hwaddr2str(&vsta->link.bssid, &mac));
                    }
                    SCHEMA_SET_BOOL(schema->wpa, vsta->link.wpa.wpa || vsta->link.wpa.rsn);
                    if (strlen(vsta->link.psk.str) > 0) {
                        SCHEMA_KEY_VAL_APPEND(schema->wpa_psks, "key", vsta->link.psk.str);
                    }
                    ow_ovsdb_vifstate_fill_akm(schema, &vsta->link.wpa);

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
                    SCHEMA_SET_BOOL(schema->wds, false);
                    break;
                case OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN:
                case OSW_DRV_VIF_STATE_STA_LINK_CONNECTING:
                case OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED:
                    SCHEMA_UNSET_FIELD(schema->ssid);
                    SCHEMA_UNSET_FIELD(schema->parent);
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

    ow_ovsdb_phystate_to_schema(&phy->state_new, rconf, phy->info); // FIXME
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
    struct ow_ovsdb_phy *phy = container_of(arg, struct ow_ovsdb_phy, work);
    if (ow_ovsdb_phy_sync(phy) == false) return;
    ev_timer_stop(EV_A_ arg);
    ow_ovsdb_phy_gc(phy);
}

static void
ow_ovsdb_phy_work_sched(struct ow_ovsdb_phy *phy)
{
    ev_timer_stop(EV_DEFAULT_ &phy->work);
    ev_timer_set(&phy->work, 0, 5);
    ev_timer_start(EV_DEFAULT_ &phy->work);
}

static void
ow_ovsdb_phy_init(struct ow_ovsdb *root,
                  struct ow_ovsdb_phy *phy,
                  const char *phy_name)
{
    phy->root = root;
    phy->phy_name = STRDUP(phy_name);
    ev_timer_init(&phy->work, ow_ovsdb_phy_work_cb, 0, 0);
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
    const bool always_wanted = (ow_ovsdb_vif_report_only_configured(vif_type) == false);

    return always_wanted ? true : is_configured;
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
    struct ow_ovsdb_vif *vif = container_of(arg, struct ow_ovsdb_vif, work);
    if (ow_ovsdb_vif_sync(vif) == false) return;
    ev_timer_stop(EV_A_ arg);
    ow_ovsdb_vif_gc(vif);
}

static void
ow_ovsdb_vif_work_sched(struct ow_ovsdb_vif *vif)
{
    ev_timer_stop(EV_DEFAULT_ &vif->work);
    ev_timer_set(&vif->work, 0, 5);
    ev_timer_start(EV_DEFAULT_ &vif->work);

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
        case OSW_AKM_RSN_PSK_SHA256: return SCHEMA_CONSTS_KEY_WPA_PSK_SHA256;
        case OSW_AKM_RSN_SAE: return SCHEMA_CONSTS_KEY_SAE;
        case OSW_AKM_RSN_FT_SAE: return SCHEMA_CONSTS_KEY_FT_SAE;
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
        case OSW_CIPHER_RSN_BIP_CMAC_128: return SCHEMA_CONSTS_CIPHER_RSN_BIP_CMAC;

        /* WPA */
        case OSW_CIPHER_WPA_NONE: return SCHEMA_CONSTS_CIPHER_WPA_NONE;
        case OSW_CIPHER_WPA_TKIP: return SCHEMA_CONSTS_CIPHER_WPA_TKIP;
        case OSW_CIPHER_WPA_CCMP: return SCHEMA_CONSTS_CIPHER_WPA_CCMP;

        /* Undefined in OVSDB schema */
        case OSW_CIPHER_RSN_GCMP_128: return NULL;
        case OSW_CIPHER_RSN_GCMP_256: return NULL;
        case OSW_CIPHER_RSN_CCMP_256: return NULL;
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
    struct osw_hwaddr_str mac_str;
    struct ow_ovsdb *root = &g_ow_ovsdb;
    struct ow_ovsdb_vif *vif;
    const char *pairwise_cipher = ow_ovsdb_cipher_into_cstr(state->pairwise_cipher);
    const char *akm = ow_ovsdb_akm_into_cstr(state->akm);
    const bool pmf = state->pmf;
    char key_id[64];

    memset(schema, 0, sizeof(*schema));

    if (strlen(uuid) > 0)
        STRSCPY(schema->_uuid.uuid, uuid);

    if (sta->info == NULL)
        return;

    vif = ds_tree_find(&root->vif_tree, sta->info->vif->vif_name ?: "");
    if (vif == NULL)
        return;

    osw_hwaddr2str(&sta->sta_addr, &mac_str);
    ow_ovsdb_keyid2str(key_id, sizeof(key_id), sta->info->drv_state->key_id);
    ow_ovsdb_keyid_fixup(key_id, ARRAY_SIZE(key_id), &vif->config);

    SCHEMA_SET_STR(schema->mac, mac_str.buf);
    SCHEMA_SET_STR(schema->state, "active");
    SCHEMA_SET_STR(schema->key_id, key_id);
    SCHEMA_SET_BOOL(schema->pmf, pmf);
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
ow_ovsdb_sta_set_tag(struct ow_ovsdb_sta *sta, const char *oftag)
{
    if (WARN_ON(sta->oftag != NULL)) return true;

    int c = ow_ovsdb_sta_tag_mutate(&sta->sta_addr, oftag, "insert");
    if (c != 1) return false;

    sta->oftag = STRDUP(oftag);
    return true;
}

static bool
ow_ovsdb_sta_unset_tag(struct ow_ovsdb_sta *sta)
{
    if (WARN_ON(sta->oftag == NULL)) return true;

    ow_ovsdb_sta_tag_mutate(&sta->sta_addr, sta->oftag, "delete");
    FREE(sta->oftag);
    sta->oftag = NULL;
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
    struct ow_ovsdb_sta *sta = container_of(arg, struct ow_ovsdb_sta, work);
    if (ow_ovsdb_sta_sync(sta) == false) return;
    ev_timer_stop(EV_A_ arg);
    ow_ovsdb_sta_gc(sta);
}

static void
ow_ovsdb_sta_work_sched(struct ow_ovsdb_sta *sta)
{
    ev_timer_stop(EV_DEFAULT_ &sta->work);
    ev_timer_set(&sta->work, 0, 5);
    ev_timer_start(EV_DEFAULT_ &sta->work);

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
ow_ovsdb_sta_set_info(const struct osw_hwaddr *sta_addr,
                      const struct osw_state_sta_info *info)
{
    struct ow_ovsdb_sta *sta = ow_ovsdb_sta_get(sta_addr);

    sta->info = osw_state_sta_lookup_newest(info->mac_addr);
    if (sta->info && sta->info->drv_state->connected == false)
        sta->info = NULL;

    ow_ovsdb_sta_work_sched(sta);
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

static enum osw_channel_width
ow_ovsdb_htmode2width(const char *ht_mode)
{
    return strcmp(ht_mode, "HT20") == 0 ? OSW_CHANNEL_20MHZ :
           strcmp(ht_mode, "HT40") == 0 ? OSW_CHANNEL_40MHZ :
           strcmp(ht_mode, "HT80") == 0 ? OSW_CHANNEL_80MHZ :
           strcmp(ht_mode, "HT160") == 0 ? OSW_CHANNEL_160MHZ :
           strcmp(ht_mode, "HT320") == 0 ? OSW_CHANNEL_320MHZ :
           OSW_CHANNEL_20MHZ;
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
            rconf->channel_changed == true ||
            rconf->ht_mode_changed == true ||
            rconf->freq_band_changed == true) {
        if (rconf->channel_exists == true &&
                rconf->ht_mode_exists == true &&
                rconf->freq_band_exists == true) {
            const int band_num = ow_ovsdb_band2num(rconf->freq_band);
            const int cn = rconf->channel;
            const enum osw_channel_width w = ow_ovsdb_htmode2width(rconf->ht_mode);
            const struct osw_channel c = {
                .control_freq_mhz = ow_ovsdb_ch2freq(band_num, cn),
                .center_freq0_mhz = 0, /* let osw_confsync compute it */
                .width = w,
            };
            ow_conf_phy_set_ap_channel(rconf->if_name, &c);
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
            bool wmm = false;

            if (strcmp(rconf->hw_mode, "11ax") == 0) {
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
            ow_conf_phy_set_ap_wmm_enabled(rconf->if_name, &wmm);
        }
        else {
            ow_conf_phy_set_ap_ht_enabled(rconf->if_name, NULL);
            ow_conf_phy_set_ap_vht_enabled(rconf->if_name, NULL);
            ow_conf_phy_set_ap_he_enabled(rconf->if_name, NULL);
            ow_conf_phy_set_ap_wmm_enabled(rconf->if_name, NULL);
        }
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
    bool unsupported = false;
    bool psk = false;
    bool sae = false;
    bool ft_psk = false;
    bool ft_sae = false;

    for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
        const char *akm = vconf->wpa_key_mgmt[i];

             if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_PSK) == 0)    { psk = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_SAE    ) == 0)    { sae = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_SAE ) == 0) { ft_psk = true; }
        else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_PSK ) == 0) { ft_sae = true; }
        else { unsupported = true; }
    }

    if (unsupported == true) {
        /* This is intended to allow state to be inherited
         * as config, eg. this should allow eap/radius
         * configurations to work if they were configured
         * prior.
         */
        LOGI("ow: ovsdb: vif: %s: unsupported configuration, ignoring", vconf->if_name);
        ow_conf_vif_set_ap_akm_psk(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_sae(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_ft_psk(vconf->if_name, NULL);
        ow_conf_vif_set_ap_akm_ft_sae(vconf->if_name, NULL);
        ow_conf_vif_set_ap_pairwise_ccmp(vconf->if_name, NULL);
        ow_conf_vif_set_ap_pairwise_tkip(vconf->if_name, NULL);
        ow_conf_vif_set_ap_pmf(vconf->if_name, NULL);
        ow_conf_vif_set_ap_rsn(vconf->if_name, NULL);
        ow_conf_vif_set_ap_wpa(vconf->if_name, NULL);
        return;
    }

    ow_conf_vif_set_ap_akm_psk(vconf->if_name, &psk);
    ow_conf_vif_set_ap_akm_sae(vconf->if_name, &sae);
    ow_conf_vif_set_ap_akm_ft_psk(vconf->if_name, &ft_psk);
    ow_conf_vif_set_ap_akm_ft_sae(vconf->if_name, &ft_sae);
    ow_conf_vif_set_ap_pairwise_tkip(vconf->if_name, &tkip);
    ow_conf_vif_set_ap_pairwise_ccmp(vconf->if_name, &ccmp);
    ow_conf_vif_set_ap_pmf(vconf->if_name, &pmf);
    ow_conf_vif_set_ap_wpa(vconf->if_name, &wpa);
    ow_conf_vif_set_ap_rsn(vconf->if_name, &rsn);
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

    const bool wpa_changed = (vconf->security_changed == true)
                          || (vconf->wpa_changed == true)
                          || (vconf->pmf_changed == true)
                          || (vconf->wpa_key_mgmt_changed == true)
                          || (vconf->wpa_pairwise_tkip_changed == true)
                          || (vconf->wpa_pairwise_ccmp_changed == true)
                          || (vconf->rsn_pairwise_tkip_changed == true)
                          || (vconf->rsn_pairwise_ccmp_changed == true);
    if (is_new == true || wpa_changed == true)
        ow_ovsdb_vconf_to_ow_conf_ap_wpa(vconf);

    // dynamic beacon
    // vlan_id
    // parent
    // vif_radio_idx
    // vif_dbg_lvl
    // wds
}

static void
ow_ovsdb_vneigh_to_ow_conf_ap(const struct schema_Wifi_VIF_Neighbors *vneigh,
                              const bool is_new)
{
    const bool changed = (is_new == true) ||
                         (vneigh->bssid_changed == true) ||
                         (vneigh->op_class_changed == true) ||
                         (vneigh->channel_changed == true) ||
                         (vneigh->phy_type_changed == true);

    const bool req_params_set = (vneigh->bssid_exists == true) ||
                                (vneigh->op_class_exists == true) ||
                                (vneigh->channel_exists == true) ||
                                (vneigh->phy_type_exists);


    if (changed == false || req_params_set == false) return;

    const uint32_t default_bssid_info = 0x0000008f;
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

                 if (strcmp(akm, SCHEMA_CONSTS_KEY_WPA_PSK) == 0) { wpa.akm_psk = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_SAE    ) == 0) { wpa.akm_sae = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_SAE ) == 0) { wpa.akm_ft_psk = true; }
            else if (strcmp(akm, SCHEMA_CONSTS_KEY_FT_PSK ) == 0) { wpa.akm_ft_sae = true; }
        }

        wpa.wpa = vconf->wpa_pairwise_tkip
               || vconf->wpa_pairwise_ccmp;
        wpa.rsn = vconf->rsn_pairwise_tkip
               || vconf->rsn_pairwise_ccmp;
        wpa.pairwise_tkip = vconf->wpa_pairwise_tkip
                         || vconf->rsn_pairwise_tkip;
        wpa.pairwise_ccmp = vconf->wpa_pairwise_ccmp
                         || vconf->rsn_pairwise_ccmp;
        wpa.pmf = pmf_from_str(vconf->pmf);

        /* FIXME: Optimize to not overwrite it all the time */
        ow_conf_vif_flush_sta_net(vconf->if_name);
        ow_conf_vif_set_sta_net(vconf->if_name, &ssid, &bssid, &psk, &wpa, &bridge, &multi_ap);
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

static struct ev_timer g_ow_ovsdb_retry;

static void
ow_ovsdb_flush(void)
{
    /* FIXME: This shouldn't be necessary. The code should actually sync not
     * only rows, but also (non)existance of rows. One idea would be to employ
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

    ow_ovsdb_ms_init(&g_ow_ovsdb.ms);
    ow_ovsdb_reattach_wps(&g_ow_ovsdb);
    ow_ovsdb_cconf_init(&table_Wifi_VIF_Config);
    ow_ovsdb_stats_init();
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
    OVSDB_TABLE_INIT(Wifi_Associated_Clients, mac);
    OVSDB_TABLE_INIT(Openflow_Tag, name);

    ev_timer_init(&g_ow_ovsdb_retry, ow_ovsdb_retry_cb, 0, OW_OVSDB_RETRY_SECONDS);
    ev_timer_start(EV_DEFAULT_ &g_ow_ovsdb_retry);

    initialized = true;
}

static bool
ow_ovsdb_enabled(void)
{
    if (getenv("OW_OVSDB_DISABLED") == NULL)
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
    const struct osw_state_vif_info vif1_100 = {
        .phy = &phy,
        .vif_name = "vif1",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .enabled = true,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .beacon_interval_tu = 100 } },
        }}
    };
    const struct osw_state_vif_info vif1_200 = {
        .phy = &phy,
        .vif_name = "vif1",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .enabled = true,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .beacon_interval_tu = 200 } },
        }}
    };
    const struct osw_state_vif_info vif2 = {
        .phy = &phy,
        .vif_name = "vif2",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .enabled = true,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .beacon_interval_tu = 200 } },
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
            .enabled = true,
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
    assert(vstate.enabled == vif.drv_state->enabled);
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
    SCHEMA_SET_BOOL(vconf.enabled, vif.drv_state->enabled);
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
            .enabled = true,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = { .control_freq_mhz = 2412, .width = OSW_CHANNEL_20MHZ} } },
        }}
    };
    const struct osw_state_vif_info vif2_c1w20 = {
        .phy = &phy,
        .vif_name = "vif2",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .enabled = true,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = { .control_freq_mhz = 2412, .width = OSW_CHANNEL_20MHZ} } },
        }}
    };
    const struct osw_state_vif_info vif2_c6w20 = {
        .phy = &phy,
        .vif_name = "vif2",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .enabled = true,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = { .control_freq_mhz = 2437, .width = OSW_CHANNEL_20MHZ} } },
        }}
    };
    const struct osw_state_vif_info vif2_c1w40 = {
        .phy = &phy,
        .vif_name = "vif2",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .enabled = true,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .channel = { .control_freq_mhz = 2412, .width = OSW_CHANNEL_40MHZ} } },
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
    assert(rstate.channel_exists == false);
    assert(rstate.ht_mode_exists == false);

    osw_drv_dummy_set_vif(&dummy, phy.phy_name, vif2_c6w20.vif_name, (void *)vif2_c6w20.drv_state);
    ow_ovsdb_ut_run();
    assert(osw_state_vif_lookup(phy.phy_name, vif2_c6w20.vif_name) != NULL);
    ow_ovsdb_get_rstate(&rstate, phy.phy_name);
    assert(rstate.channel_exists == false);
    assert(rstate.ht_mode_exists == false);
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

    struct osw_channel_state cs_2g[] = {
            { .channel = { .control_freq_mhz = 2412 } },
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
            .enabled = true,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .ssid = { .buf = "ssid1", .len = 5 } } },
        }},
    };
    const struct osw_state_vif_info vif2 = {
        .phy = &phy,
        .vif_name = "vif2",
        .drv_state = (const struct osw_drv_vif_state []){{
            .exists = true,
            .enabled = true,
            .vif_type = OSW_VIF_AP,
            .u = { .ap = { .ssid = { .buf = "ssid2", .len = 5 } } },
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
