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

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>

#include "os.h"
#include "util.h"
#include "memutil.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_cache.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"
#include "wm2.h"
#include "telog.h"

#include "target.h"
#include "wm2_dpp.h"
#include "wm2_target.h"
#include "wm2_l2uf.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define WM2_RECALC_DELAY_SECONDS            30
#define WM2_DFS_FALLBACK_GRACE_PERIOD_SECONDS 10
#define WM2_MAX_BSSID_LEN 18
#define WM2_MAX_NAS_ID_LEN 49
#define WM2_MAX_FT_KEY_LEN 65
#define WM2_MAX_IP_ADDR_LEN 40
#define WM2_MAX_RADIUS_SECRET_LEN 128
#define WM2_MAX_RADIUS_KEY_LEN (WM2_MAX_IP_ADDR_LEN + 8)
#define WM2_MAX_NBORS_KEY_LEN (WM2_MAX_BSSID_LEN + 4)
#define WM2_DEFAULT_FT_KEY "8261b033613b35b373761cbde421250e67cd21d44737fe5e5deed61869b4b397"

/* Support of more than 3 servers per VIF require extending
 * buffer used to print out MIB in hostapd. Otherwise State
 * don't report full list of configured RADIUS servers */
#define WM2_AUTH_RADIUS_SUPPORTED_NUM 3
#define WM2_ACC_RADIUS_SUPPORTED_NUM 3
#define WM2_RADIUS_SUPPORTED_NUM (WM2_AUTH_RADIUS_SUPPORTED_NUM + WM2_ACC_RADIUS_SUPPORTED_NUM)
#define WM2_FT_NEIGHBORS_SUPPORTED_NUM 24
#define REQUIRE(ctx, cond) if (!(cond)) { LOGW("%s: %s: failed check: %s", ctx, __func__, #cond); return; }
#define OVERRIDE(ctx, lv, rv) if (lv != rv) { lv = rv; LOGW("%s: overriding '%s' - this is target impl bug", ctx, #lv); }
#define bitcount __builtin_popcount

#define TELOG_STA_STATE(vstate, status) \
        TELOG_STEP("WIFI_LINK", (vstate)->if_name, status, "multi_ap=%s ssid=%s bssid=%s", \
                   (vstate)->multi_ap, (vstate)->ssid, (vstate)->parent);

#define TELOG_STA_ROAM(vconf, n_cconfs) \
        TELOG_STEP("WIFI_LINK", (vconf)->if_name, "roaming", "multi_ap=%s ssid=%s bssid=%s cconfs=%d", \
                   (vconf)->multi_ap, (vconf)->ssid, (vconf)->parent, n_cconfs)

#define TELOG_WDS_STATE(vstate, status) \
        TELOG_STEP("WDS_LINK", (vstate)->if_name, status, "sta=%s", \
                   (vstate)->ap_vlan_sta_addr)

struct wm2_delayed {
    ev_timer timer;
    struct ds_dlist_node list;
    char ifname[32];
    void (*fn)(const char *ifname, bool force);
    char workname[256];
};

enum wm2_aux_type {
    WM2_AUX_NEIGHBOR_CONFIG  = 0,
    WM2_AUX_NEIGHBOR_STATE,
    WM2_AUX_RADIUS_CONFIG,
    WM2_AUX_RADIUS_STATE
};

struct wm2_aux_confstate {
    char ifname[32];
    enum wm2_aux_type type;
    struct ds_tree_node node;
    struct ds_tree tree;
};

enum wm2_nbors_type {
    WM2_NBOR_R0KH = 0,
    WM2_NBOR_R1KH
};

struct wm2_nbors_confstate {
    char bssid[WM2_MAX_BSSID_LEN];
    char nas_id[WM2_MAX_NAS_ID_LEN];
    char ft_key[WM2_MAX_FT_KEY_LEN];
    enum wm2_nbors_type type;
    struct ds_tree_node node;
};

enum wm2_radius_type {
    WM2_RADIUS_TYPE_A = 0,
    WM2_RADIUS_TYPE_AA,
    WM2_RADIUS_TYPE_UNKNOWN
};

struct wm2_radius_confstate {
    char ip[WM2_MAX_IP_ADDR_LEN];
    int port;
    char secret[WM2_MAX_RADIUS_SECRET_LEN];
    enum wm2_radius_type type;
    struct ds_tree_node node;
};

static struct ds_tree g_aux[] = {
    [WM2_AUX_NEIGHBOR_CONFIG] = DS_TREE_INIT(ds_str_cmp, struct wm2_aux_confstate, node),
    [WM2_AUX_NEIGHBOR_STATE] = DS_TREE_INIT(ds_str_cmp, struct wm2_aux_confstate, node),
    [WM2_AUX_RADIUS_CONFIG] = DS_TREE_INIT(ds_str_cmp, struct wm2_aux_confstate, node),
    [WM2_AUX_RADIUS_STATE] = DS_TREE_INIT(ds_str_cmp, struct wm2_aux_confstate, node)
};

ovsdb_table_t table_Wifi_Radio_Config;
ovsdb_table_t table_Wifi_Radio_State;
ovsdb_table_t table_Wifi_VIF_Config;
ovsdb_table_t table_Wifi_VIF_State;
ovsdb_table_t table_Wifi_VIF_Neighbors;
ovsdb_table_t table_Wifi_Credential_Config;
ovsdb_table_t table_Wifi_Associated_Clients;
ovsdb_table_t table_Wifi_Master_State;
ovsdb_table_t table_Openflow_Tag;
ovsdb_table_t table_RADIUS;

static ds_dlist_t delayed_list = DS_DLIST_INIT(struct wm2_delayed, list);

static bool
wm2_lookup_rstate_by_freq_band(struct schema_Wifi_Radio_State *rstate,
                               const char *freqband);
/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/
static struct wm2_delayed *
wm2_delayed_lookup(void (*fn)(const char *ifname, bool force),
                   const char *ifname)
{
    struct wm2_delayed *i;
    ds_dlist_foreach(&delayed_list, i)
        if (i->fn == fn && !strcmp(i->ifname, ifname))
            return i;
    return NULL;
}

static void
wm2_delayed_cancel(void (*fn)(const char *ifname, bool force),
                   const char *ifname)
{
    struct wm2_delayed *i;
    if (!(i = wm2_delayed_lookup(fn, ifname)))
        return;
    LOGD("%s: cancelling scheduled work %s", ifname, i->workname);
    ds_dlist_remove(&delayed_list, i);
    ev_timer_stop(EV_DEFAULT, &i->timer);
    FREE(i);
}

static void
wm2_delayed_cb(struct ev_loop *loop, ev_timer *timer, int revents)
{
    struct wm2_delayed *i;
    struct wm2_delayed j;
    i = (void *)timer;
    j = *i;
    LOGD("%s: running scheduled work %s", i->ifname, i->workname);
    ds_dlist_remove(&delayed_list, i);
    ev_timer_stop(EV_DEFAULT, &i->timer);
    FREE(i);
    j.fn(j.ifname, false);
}

static void
wm2_delayed(void (*fn)(const char *ifname, bool force),
            const char *ifname, unsigned int seconds,
            const char *workname)
{
    struct wm2_delayed *i;
    if ((i = wm2_delayed_lookup(fn, ifname)))
        return;
    i = MALLOC(sizeof(*i));
    STRSCPY(i->ifname, ifname);
    STRSCPY(i->workname, workname);
    i->fn = fn;
    ev_timer_init(&i->timer, wm2_delayed_cb, seconds, 0);
    ev_timer_start(EV_DEFAULT, &i->timer);
    ds_dlist_insert_tail(&delayed_list, i);
    LOGI("%s: scheduling delayed (%us) work %s", ifname, seconds, workname);
}

#define wm2_delayed_recalc(fn, ifname) (wm2_delayed((fn), (ifname), WM2_RECALC_DELAY_SECONDS, #fn))
#define wm2_delayed_recalc_cancel wm2_delayed_cancel

#define wm2_timer(fn, ifname, timeout) (wm2_delayed((fn), (ifname), (timeout), #fn))
#define wm2_timer_cancel wm2_delayed_cancel

static bool
wm2_sta_has_connected(const struct schema_Wifi_VIF_State *oldstate,
                      const struct schema_Wifi_VIF_State *newstate)
{
    return (!strcmp(oldstate->mode, "sta") ||
            !strcmp(newstate->mode, "sta")) &&
           strcmp(newstate->parent, oldstate->parent) &&
           !strlen(oldstate->parent) &&
           strlen(newstate->parent);
}

static bool
wm2_sta_has_reconnected(const struct schema_Wifi_VIF_State *oldstate,
                        const struct schema_Wifi_VIF_State *newstate)
{
    return (!strcmp(oldstate->mode, "sta") ||
            !strcmp(newstate->mode, "sta")) &&
           strcmp(newstate->parent, oldstate->parent) &&
           strlen(oldstate->parent) &&
           strlen(newstate->parent);
}

static bool
wm2_sta_has_disconnected(const struct schema_Wifi_VIF_State *oldstate,
                         const struct schema_Wifi_VIF_State *newstate)
{
    return (!strcmp(oldstate->mode, "sta") ||
            !strcmp(newstate->mode, "sta")) &&
           strcmp(newstate->parent, oldstate->parent) &&
           strlen(oldstate->parent) &&
           !strlen(newstate->parent);
}

struct wm2_fallback_parent {
    int channel;
    char bssid[64];
    char radio_name[128];
    char freq_band[128];
};

static unsigned int
wm2_get_fallback_parents(struct wm2_fallback_parent *parents,
                         unsigned int parent_max)
{
    const struct schema_Wifi_Radio_State *rs;
    struct wm2_fallback_parent *parent;
    unsigned int parent_num;
    void *buf;
    int n;
    int i;

    parent_num = 0;

    buf = ovsdb_table_select_where(&table_Wifi_Radio_State, NULL, &n);
    if (!buf)
        return parent_num;

    for (n--; n >= 0; n--) {
        rs = buf + (n * table_Wifi_Radio_State.schema_size);

        for (i = 0; i < rs->fallback_parents_len; i++) {
            if (parent_num >= parent_max) {
                LOGW("%s: %s we exceed parent max", rs->if_name, __func__);
                break;
            }

            parent = &parents[parent_num];
            parent->channel = rs->fallback_parents[i];
            STRSCPY_WARN(parent->bssid, rs->fallback_parents_keys[i]);
            STRSCPY_WARN(parent->radio_name, rs->if_name);
            STRSCPY_WARN(parent->freq_band, rs->freq_band);
            parent_num++;

            LOGD("%s: %s add fallback parent %s %d", parent->freq_band, parent->radio_name,
                 parent->bssid, parent->channel);
        }
    }

    FREE(buf);

    return parent_num;
}

static void
wm2_parent_change(void)
{
    const char *parentchange = strfmta("%s/parentchange.sh", target_bin_dir());
    struct schema_Wifi_Radio_State rstate;
    struct wm2_fallback_parent parents[8];
    const struct wm2_fallback_parent *parent;
    unsigned int parents_num;
    unsigned int i;

    parents_num = wm2_get_fallback_parents(parents, ARRAY_SIZE(parents));
    if (!parents_num) {
        LOGW("%s seems no fallback parents found, restart managers", __func__);
        target_device_restart_managers();
        return;
    }

    parent = &parents[0];

    /* Simply choose 2.4 parent with our channel if possible */
    if (wm2_lookup_rstate_by_freq_band(&rstate, "2.4G")) {
        for (i = 0; i < parents_num; i++ ) {
            if (rstate.channel == parents[i].channel) {
                parent = &parents[i];
                break;
            }
        }
    }

    LOGN("%s run parentchange.sh %s %s %d", parent->freq_band, parent->radio_name,
         parent->bssid, parent->channel);
    WARN_ON(!strexa(parentchange, parent->radio_name, parents->bssid, strfmta("%d", parents->channel)));
    return;
}

static void
wm2_dfs_disconnect_check(const char *ifname, bool force)
{
    LOGN("%s %s called", ifname, __func__);
    wm2_parent_change();
}

static bool
wm2_sta_has_dfs_channel(const struct schema_Wifi_Radio_State *rstate,
                        const struct schema_Wifi_VIF_State *vstate)
{
    bool status = false;
    int i;

    if (!rstate->channel_exists)
         return status;

    /* TODO check all channels base on number/width */
    for (i = 0; i < rstate->channels_len; i++) {
        if (rstate->channel != atoi(rstate->channels_keys[i]))
            continue;

        LOGI("%s: check channel %d (%d) %s", vstate->if_name, rstate->channel, vstate->channel, rstate->channels[i]);
        if (!strstr(rstate->channels[i], "allowed"))
            status = true;

        break;
    }

    return status;
}

static void
wm2_sta_handle_dfs_link_change(const struct schema_Wifi_Radio_State *rstate,
                               const struct schema_Wifi_VIF_State *oldstate,
                               const struct schema_Wifi_VIF_State *newstate)
{
    static const struct schema_Wifi_VIF_State empty;

    if (!newstate)
        newstate = &empty;

    if (wm2_sta_has_connected(oldstate, newstate) ||
        wm2_sta_has_reconnected(oldstate, newstate) ||
        !newstate->vif_config_exists)
        wm2_timer_cancel(wm2_dfs_disconnect_check, oldstate->if_name);

    /* This must be a different-radio parent change. In that
     * case don't attempt arming the fallback parent timer.
     */
    if (!newstate->vif_config_exists)
        return;

    if (wm2_sta_has_disconnected(oldstate, newstate) &&
        wm2_sta_has_dfs_channel(rstate, oldstate)) {
            LOGN("%s: sta: dfs: disconnected from %s - arm fallback parents timer", oldstate->if_name, oldstate->parent);
            wm2_timer(wm2_dfs_disconnect_check, oldstate->if_name, WM2_DFS_FALLBACK_GRACE_PERIOD_SECONDS);
    }
}

static void
wm2_sta_print_status(const struct schema_Wifi_VIF_State *oldstate,
                     const struct schema_Wifi_VIF_State *newstate)
{
    static const struct schema_Wifi_VIF_State empty;

    if (!newstate)
        newstate = &empty;

    if (wm2_sta_has_connected(oldstate, newstate)) {
        TELOG_STA_STATE(newstate, "connected");
        LOGN("%s: sta: connected to %s on channel %d",
             newstate->if_name, newstate->parent, newstate->channel);
    }

    if (wm2_sta_has_reconnected(oldstate, newstate)) {
        TELOG_STA_STATE(oldstate, "disconnected");
        TELOG_STA_STATE(newstate, "connected");
        LOGN("%s: sta: re-connected from %s to %s on channel %d",
             oldstate->if_name, oldstate->parent, newstate->parent, newstate->channel);
    }

    if (wm2_sta_has_disconnected(oldstate, newstate)) {
        TELOG_STA_STATE(oldstate, "disconnected");
        LOGN("%s: sta: disconnected from %s", oldstate->if_name, oldstate->parent);
    }
}

void wm2_radio_update_port_state(const char *cloud_vif_ifname)
{
    struct schema_Wifi_Master_State mstate;
    struct schema_Wifi_VIF_State vstate;
    struct schema_Wifi_VIF_Config vconf;
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Wifi_Master_State,
                                     port_state),
                       NULL };
    bool active;
    int num;

    memset(&mstate, 0, sizeof(mstate));
    memset(&vstate, 0, sizeof(vstate));
    memset(&vconf, 0, sizeof(vconf));

    /* Note: I'm not comfortable relying on a provided
     *       vstate from caller because it may be from a
     *       partial update with filters and may not include
     *       all the necessary bits.
     *
     *       Don't care if this fails. If it does
     *       it means interface went away and is
     *       no longer active.
     */
    ovsdb_table_select_one_where(&table_Wifi_VIF_State,
                                 ovsdb_where_simple("if_name",
                                                    cloud_vif_ifname),
                                 &vstate);
    ovsdb_table_select_one_where(&table_Wifi_VIF_Config,
                                 ovsdb_where_simple("if_name",
                                                    cloud_vif_ifname),
                                 &vconf);

    /* FIXME: This is a band-aid for the time being before
     *        CM2 and Wifi_Master_State logic is reworked to
     *        be more flexible and possibly cloud-controlled
     *        to allow overlapping parent changing,
     *        fallbacks, priorities, etc.
     *
     *        This just tries to piggyback on the original
     *        design of Wifi_Master_State where CM1 (and CM2
     *        will too, for now) relies on port_state to
     *        decide whether a link is usable for
     *        onboarding.
     */

    /* The idea is for libtarget to update "parent" column
     * only when it is connected to parent AP in sta mode
     * and data-traffic ready (i.e. authorized, after
     * EAPOL exchanges).
     */
    active = false;

    if (vstate.parent_exists && vconf.parent_exists &&
        strlen(vstate.parent) && strlen(vconf.parent) &&
        !strcmp(vstate.parent, vconf.parent))
        active = true;

    if (vstate.parent_exists && !vconf.parent_exists && strlen(vstate.parent))
        active = true;

    if (vstate.mode_exists && !strcmp(vstate.mode, "ap"))
        active = true;

    if (!vstate.enabled_exists)
        active = false;

    if (!vstate.enabled)
        active = false;

    mstate.port_state_exists = true;
    snprintf(mstate.port_state,
             sizeof(mstate.port_state),
             active ? "active" : "inactive");

    num = ovsdb_table_update_where_f(&table_Wifi_Master_State,
                                     ovsdb_where_simple("if_name",
                                                        cloud_vif_ifname),
                                     &mstate,
                                     filter);

    LOGD("%s: updated (%d) master state to '%s'",
         cloud_vif_ifname, num, mstate.port_state);
}

static void
wm2_radio_update_port_state_set_inactive(const char *ifname)
{
    struct schema_Wifi_Master_State mstate;
    json_t *w;
    int n;

    memset(&mstate, 0, sizeof(mstate));
    mstate._partial_update = true;
    SCHEMA_SET_STR(mstate.port_state, "inactive");
    if (WARN_ON(!(w = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Master_State, if_name), ifname))))
        return;

    n = ovsdb_table_update_where(&table_Wifi_Master_State, w, &mstate);
    LOGD("%s: port state set to inactive: n=%d", ifname, n);
}

#define CHANGED_MAP_STRSTR(conf, state, name, force) \
    (force || schema_changed_map(conf->name##_keys, \
                                 conf->name, \
                                 state->name##_keys, \
                                 state->name, \
                                 conf->name##_len, \
                                 state->name##_len, \
                                 sizeof(*conf->name##_keys), \
                                 sizeof(*conf->name), \
                                 (smap_cmp_fn_t)strncmp, \
                                 (smap_cmp_fn_t)strncmp))

#define CHANGED_MAP_INTSTR(conf, state, name, force) \
    (force || schema_changed_map(conf->name##_keys, \
                                 conf->name, \
                                 state->name##_keys, \
                                 state->name, \
                                 conf->name##_len, \
                                 state->name##_len, \
                                 sizeof(*conf->name##_keys), \
                                 sizeof(*conf->name),\
                                 (smap_cmp_fn_t)memcmp, \
                                 (smap_cmp_fn_t)strncmp ))

#define CHANGED_MAP_STRINT(conf, state, name, force) \
    (force || schema_changed_map(conf->name##_keys, \
                                 conf->name, \
                                 state->name##_keys, \
                                 state->name, \
                                 conf->name##_len, \
                                 state->name##_len, \
                                 sizeof(*conf->name##_keys), \
                                 sizeof(*conf->name),\
                                 (smap_cmp_fn_t)strncmp, \
                                 (smap_cmp_fn_t)memcmp ))

#define CHANGED_SET(conf, state, name, force) \
    (force || schema_changed_set(conf->name, \
                                 state->name, \
                                 conf->name##_len, \
                                 state->name##_len, \
                                 sizeof(*conf->name), \
                                 strncmp))

#define CHANGED_SET_CASE(conf, state, name, force) \
    (force || schema_changed_set(conf->name, \
                                 state->name, \
                                 conf->name##_len, \
                                 state->name##_len, \
                                 sizeof(*conf->name), \
                                 strncasecmp))

#define CHANGED_SUBSET(conf, state, name, force) \
    (force || schema_changed_subset(conf->name, \
                                    state->name, \
                                    conf->name##_len, \
                                    state->name##_len, \
                                    sizeof(*conf->name), \
                                    strncmp))

#define CHANGED_INT(conf, state, name, force) \
    (conf->name##_exists && ((state->name##_exists && (conf->name != state->name)) || \
                             !state->name##_exists || \
                             force))

#define CHANGED_STR(conf, state, name, force) \
    (conf->name##_exists && ((state->name##_exists && strcmp(conf->name, state->name)) || \
                             !state->name##_exists || \
                             force))

#define CHANGED_STR_CASE(conf, state, name, force) \
    (conf->name##_exists && ((state->name##_exists && strcasecmp(conf->name, state->name)) || \
                             !state->name##_exists || \
                             force))

#define CMP(cmp, name) \
    (changed |= (changedf->name = ((cmp(conf, state, name, changedf->_uuid)) && \
                                   (LOGD("%s: '%s' changed", conf->if_name, #name), 1))))

static bool
wm2_vconf_changed(const struct schema_Wifi_VIF_Config *conf,
                  const struct schema_Wifi_VIF_State *state,
                  struct schema_Wifi_VIF_Config_flags *changedf)
{
    int changed = 0;

    memset(changedf, 0, sizeof(*changedf));
    changed |= (changedf->_uuid = strcmp(conf->_uuid.uuid, state->vif_config.uuid));
    CMP(CHANGED_INT, enabled);
    CMP(CHANGED_INT, ap_bridge);
    CMP(CHANGED_INT, vif_radio_idx);
    CMP(CHANGED_INT, uapsd_enable);
    CMP(CHANGED_INT, group_rekey);
    CMP(CHANGED_INT, vlan_id);
    CMP(CHANGED_INT, wds);
    CMP(CHANGED_INT, rrm);
    CMP(CHANGED_INT, btm);
    CMP(CHANGED_INT, dynamic_beacon);
    CMP(CHANGED_INT, mcast2ucast);
    CMP(CHANGED_INT, dpp_cc);
    CMP(CHANGED_STR, multi_ap);
    CMP(CHANGED_STR, bridge);
    CMP(CHANGED_STR, mac_list_type);
    CMP(CHANGED_STR, mode);
    CMP(CHANGED_STR_CASE, parent);
    CMP(CHANGED_STR, ssid);
    CMP(CHANGED_STR, ssid_broadcast);
    CMP(CHANGED_STR, min_hw_mode);
    if (conf->mac_list_type_exists)
        CMP(CHANGED_SET_CASE, mac_list);
    CMP(CHANGED_MAP_STRSTR, security);
    CMP(CHANGED_INT, wps);
    CMP(CHANGED_INT, wps_pbc);
    CMP(CHANGED_STR, wps_pbc_key_id);
    CMP(CHANGED_INT, wpa);
    CMP(CHANGED_INT, wpa_pairwise_tkip);
    CMP(CHANGED_INT, wpa_pairwise_ccmp);
    CMP(CHANGED_INT, rsn_pairwise_tkip);
    CMP(CHANGED_INT, rsn_pairwise_ccmp);
    /* The way wpa_key_mgmt is checked depends on VIF mode (STA or AP) */
    if (strcmp(conf->mode, "sta") == 0 && strcmp(conf->mode, state->mode) == 0)
        CMP(CHANGED_SUBSET, wpa_key_mgmt);
    else
        CMP(CHANGED_SET, wpa_key_mgmt);
    CMP(CHANGED_MAP_STRSTR, wpa_psks);
    CMP(CHANGED_STR, radius_srv_addr);
    CMP(CHANGED_INT, radius_srv_port);
    CMP(CHANGED_STR, radius_srv_secret);
    CMP(CHANGED_STR, dpp_connector);
    CMP(CHANGED_STR, dpp_netaccesskey_hex);
    CMP(CHANGED_STR, dpp_csign_hex);
    CMP(CHANGED_INT, min_rssi);
    CMP(CHANGED_INT, max_sta);
    CMP(CHANGED_STR, airtime_precedence);
    CMP(CHANGED_STR, pmf);
    CMP(CHANGED_INT, ft_psk);
    CMP(CHANGED_INT, ft_mobility_domain);
    CMP(CHANGED_INT, ft_over_ds);
    CMP(CHANGED_INT, ft_pmk_r0_key_lifetime_sec);
    CMP(CHANGED_INT, ft_pmk_r1_max_key_lifetime_sec);
    CMP(CHANGED_INT, ft_pmk_r1_push);
    CMP(CHANGED_INT, ft_psk_generate_local);
    CMP(CHANGED_STR, nas_identifier);

    if (changed)
        LOGD("%s: changed (forced=%d)", conf->if_name, changedf->_uuid);

    return changed;
}

static bool
wm2_rconf_changed(const struct schema_Wifi_Radio_Config *conf,
                  const struct schema_Wifi_Radio_State *state,
                  struct schema_Wifi_Radio_Config_flags *changedf)
{
    int changed = 0;

    memset(changedf, 0, sizeof(*changedf));
    changed |= (changedf->_uuid = strcmp(conf->_uuid.uuid, state->radio_config.uuid));
    CMP(CHANGED_INT, channel);
    CMP(CHANGED_INT, center_freq0_chan);
    CMP(CHANGED_INT, channel_sync);
    CMP(CHANGED_INT, enabled);
    CMP(CHANGED_INT, thermal_shutdown);
    CMP(CHANGED_INT, thermal_integration);
    CMP(CHANGED_INT, thermal_downgrade_temp);
    CMP(CHANGED_INT, thermal_upgrade_temp);
    CMP(CHANGED_INT, tx_chainmask);
    CMP(CHANGED_INT, tx_power);
    CMP(CHANGED_INT, bcn_int);
    CMP(CHANGED_INT, dfs_demo);
    CMP(CHANGED_STR, channel_mode);
    CMP(CHANGED_STR, country);
    CMP(CHANGED_STR, freq_band);
    CMP(CHANGED_STR, ht_mode);
    CMP(CHANGED_STR, hw_mode);
    CMP(CHANGED_MAP_STRSTR, hw_config);
    CMP(CHANGED_MAP_INTSTR, temperature_control);
    CMP(CHANGED_MAP_STRINT, fallback_parents);
    CMP(CHANGED_STR, zero_wait_dfs);

    if (changed)
        LOGD("%s: changed (forced=%d)", conf->if_name, changedf->_uuid);

    return changed;
}

#undef CHANGED_STR
#undef CHANGED_INT
#undef CHANGED_SET
#undef CHANGED_MAP_STRSTR
#undef CHANGED_MAP_STRINT
#undef CHANGED_MAP_INTSTR
#undef CMP

static void
wm2_vconf_init_del(struct schema_Wifi_VIF_Config *vconf, const char *ifname)
{
    memset(vconf, 0, sizeof(*vconf));
    STRSCPY(vconf->if_name, ifname);
    vconf->if_name_present = true;
    vconf->if_name_exists = true;
    vconf->enabled_present = true;
    vconf->enabled_changed = true;
    vconf->enabled_exists = true;
}

static void
wm2_vstate_init(struct schema_Wifi_VIF_State *vstate, const char *ifname)
{
    memset(vstate, 0, sizeof(*vstate));
    SCHEMA_SET_STR(vstate->if_name, ifname);
}

static bool
wm2_lookup_rconf_by_vif_ifname(struct schema_Wifi_Radio_Config *rconf,
                               const char *ifname)
{
    const struct schema_Wifi_Radio_Config *rc;
    const struct schema_Wifi_Radio_State *rs;
    struct schema_Wifi_VIF_Config vconf;
    struct schema_Wifi_VIF_State vstate;
    json_t *where;
    void *buf;
    int n;
    int i;

    memset(rconf, 0, sizeof(*rconf));

    if (!(where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_Config, if_name), ifname)))
        return false;
    if (ovsdb_table_select_one_where(&table_Wifi_VIF_Config, where, &vconf)) {
        if ((buf = ovsdb_table_select_where(&table_Wifi_Radio_Config, NULL, &n))) {
            for (n--; n >= 0; n--) {
                rc = buf + (n * table_Wifi_Radio_Config.schema_size);
                for (i = 0; i < rc->vif_configs_len; i++) {
                    if (!strcmp(rc->vif_configs[i].uuid, vconf._uuid.uuid)) {
                        memcpy(rconf, rc, sizeof(*rc));
                        FREE(buf);
                        LOGD("%s: found radio %s via vif config", ifname, rconf->if_name);
                        return true;
                    }
                }
            }
            FREE(buf);
        }
    }

    if (!(where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_State, if_name), ifname)))
        return false;
    if (ovsdb_table_select_one_where(&table_Wifi_VIF_State, where, &vstate)) {
        if ((buf = ovsdb_table_select_where(&table_Wifi_Radio_State, NULL, &n))) {
            for (n--; n >= 0; n--) {
                rs = buf + (n * table_Wifi_Radio_State.schema_size);
                for (i = 0; i < rs->vif_states_len; i++) {
                    if (!strcmp(rs->vif_states[i].uuid, vstate._uuid.uuid)) {
                        if (!(where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), rs->if_name)))
                            continue;
                        if (ovsdb_table_select_one_where(&table_Wifi_Radio_Config, where, rconf)) {
                            FREE(buf);
                            LOGD("%s: found radio %s via vif state", ifname, rconf->if_name);
                            return true;
                        }
                    }
                }
            }
            FREE(buf);
        }
    }

    return false;
}

static bool
wm2_lookup_rconf_by_ifname(struct schema_Wifi_Radio_Config *rconf,
                           const char *ifname)
{
    json_t *where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), ifname);
    if (!where)
        return false;
    return ovsdb_table_select_one_where(&table_Wifi_Radio_Config, where, rconf);
}

static bool
wm2_lookup_vconf_by_ifname(struct schema_Wifi_VIF_Config *vconf,
                           const char *ifname)
{
    json_t *where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_Config, if_name), ifname);
    if (!where)
        return false;
    return ovsdb_table_select_one_where(&table_Wifi_VIF_Config, where, vconf);
}

static bool
wm2_lookup_rstate_by_freq_band(struct schema_Wifi_Radio_State *rstate,
                               const char *freqband)
{
    json_t *where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_State, freq_band), freqband);
    if (!where)
        return false;
    return ovsdb_table_select_one_where(&table_Wifi_Radio_State, where, rstate);
}

static int
wm2_cconf_get(const struct schema_Wifi_VIF_Config *vconf,
              struct schema_Wifi_Credential_Config *cconfs,
              int size)
{
    json_t *where;
    int n;

    memset(cconfs, 0, sizeof(*cconfs) * size);

    for (n = 0; n < vconf->credential_configs_len && size > 0; n++) {
        if (!(where = ovsdb_where_uuid("_uuid", vconf->credential_configs[n].uuid)))
            continue;
        if (!ovsdb_table_select_one_where(&table_Wifi_Credential_Config, where, cconfs)) {
            LOGW("%s: failed to retrieve credential config %s",
                 vconf->if_name, vconf->credential_configs[n].uuid);
            continue;
        }
        cconfs++, size--;
    }

    if (size == 0 && n < vconf->credential_configs_len) {
        LOGW("%s: credential config truncated to %d entries, "
             "please adjust the size at compile time!",
             vconf->if_name, n);
    }

    return n;
}

static int
wm2_append_radius_by_uuid(const struct schema_Wifi_VIF_Config *vconf,
                          struct schema_RADIUS *radius_list,
                          const char *uuid)
{
    json_t *where;

    if (!(where = ovsdb_where_uuid("_uuid", uuid)))
        return 1;

    if (!ovsdb_table_select_one_where(&table_RADIUS, where, radius_list)) {
        LOGW("%s: failed to retrieve RADIUS with UUID %s",
             vconf->if_name, uuid);
        return 1;
    }
    return 0;
}

static bool
wm2_vconf_key_mgmt_contains(const struct schema_Wifi_VIF_Config *vconf,
                            const char *key_mgmt_part)
{
    int n = vconf->wpa_key_mgmt_len;

    for (n--; n>=0; n--) {
        if (strstr(vconf->wpa_key_mgmt[n], key_mgmt_part))
            return true;
    }
    return false;
}

static enum wm2_radius_type
wm2_radius_str2type(const char *str_type)
{
    if (!strcmp(str_type, "AA"))
        return WM2_RADIUS_TYPE_AA;
    if (!strcmp(str_type, "A"))
        return WM2_RADIUS_TYPE_A;
    return WM2_RADIUS_TYPE_UNKNOWN;
}

static int
wm2_aux_radius_confstate_cmp(const void *a, const void *b)
{
    int ret;
    const struct wm2_radius_confstate *ra = a;
    const struct wm2_radius_confstate *rb = b;

    if ((ret = ra->type - rb->type)    != 0) return ret;
    if ((ret = strcmp(ra->ip, rb->ip)) != 0) return ret;
    if ((ret = ra->port - rb->port)    != 0) return ret;
    return strcmp(ra->secret, rb->secret);
}

static int
wm2_aux_nbors_confstate_cmp(const void *a, const void *b)
{
    int ret;
    const struct wm2_nbors_confstate *na = a;
    const struct wm2_nbors_confstate *nb = b;

    if ((ret = na->type - nb->type)              != 0) return ret;
    if ((ret = strcasecmp(na->bssid, nb->bssid)) != 0) return ret;
    if ((ret = strcmp(na->nas_id, nb->nas_id))   != 0) return ret;
    return strcmp(na->ft_key, nb->ft_key);
}

static struct wm2_aux_confstate *
wm2_aux_confstate_create(const char *ifname,
                         enum wm2_aux_type type)
{
    struct wm2_aux_confstate *ptr;

    ptr = CALLOC(1, sizeof(*ptr));
    STRSCPY_WARN(ptr->ifname, ifname);
    ptr->type = type;

    switch (ptr->type) {
        case WM2_AUX_NEIGHBOR_CONFIG:
        case WM2_AUX_NEIGHBOR_STATE:
            ds_tree_init(&ptr->tree, wm2_aux_nbors_confstate_cmp,
                         struct wm2_nbors_confstate, node);
            break;
        case WM2_AUX_RADIUS_CONFIG:
        case WM2_AUX_RADIUS_STATE:
            ds_tree_init(&ptr->tree, wm2_aux_radius_confstate_cmp,
                         struct wm2_radius_confstate, node);
            break;
    }
    ds_tree_insert(&g_aux[type], ptr, ptr->ifname);
    LOGD("%s: created aux config/state tree (%d)", ifname, type);

    return ptr;
}

static void
wm2_aux_confstate_subtree_free(struct wm2_aux_confstate *cs)
{
    void *ptr;

    while ((ptr = ds_tree_remove_head(&cs->tree)) != NULL)
        FREE(ptr);
}

static void
wm2_aux_confstate_gc_spec(enum wm2_aux_type type)
{
    struct wm2_aux_confstate *cs;
    struct wm2_aux_confstate *tmp;
    struct ds_tree *tree = &g_aux[type];

    ds_tree_foreach_safe(tree, cs, tmp) {
        if (ds_tree_is_empty(&cs->tree)) {
            ds_tree_remove(tree, cs);
            FREE(cs);
        }
    }
}

static void
wm2_aux_confstate_gc(void)
{
    wm2_aux_confstate_gc_spec(WM2_AUX_NEIGHBOR_CONFIG);
    wm2_aux_confstate_gc_spec(WM2_AUX_NEIGHBOR_STATE);
    wm2_aux_confstate_gc_spec(WM2_AUX_RADIUS_CONFIG);
    wm2_aux_confstate_gc_spec(WM2_AUX_RADIUS_STATE);
}

static struct wm2_aux_confstate *
wm2_aux_confstate_lookup(const char *ifname,
                         enum wm2_aux_type type)
{
    struct wm2_aux_confstate *i;

    if ((i = ds_tree_find(&g_aux[type], ifname)) != NULL)
        return i;

    return wm2_aux_confstate_create(ifname, type);

}

static void
wm2_aux_radius_confstate_add(struct ds_tree *tree,
                             const char *ip,
                             const int port,
                             const char *secret,
                             enum wm2_radius_type type)
{
    struct wm2_radius_confstate *rad;

    rad = CALLOC(1, sizeof(*rad));
    STRSCPY_WARN(rad->ip, ip);
    STRSCPY_WARN(rad->secret, secret);
    rad->port = port;
    rad->type = type;

    ds_tree_insert(tree, rad, rad);
}

static void
wm2_aux_nbors_confstate_add(struct ds_tree *tree,
                            const char *bssid,
                            const char *nas_id,
                            const char *ft_key,
                            enum wm2_nbors_type type)
{
    struct wm2_nbors_confstate *nbor;

    nbor = CALLOC(1, sizeof(*nbor));
    STRSCPY_WARN(nbor->bssid, bssid);
    STRSCPY_WARN(nbor->nas_id, nas_id);
    STRSCPY_WARN(nbor->ft_key, ft_key);
    nbor->type = type;

    ds_tree_insert(tree, nbor, nbor);
}

static bool
wm2_radius_config_changed(const char *ifname)
{
    struct wm2_aux_confstate *a, *b;
    struct wm2_radius_confstate *ra, *rb;

    /* Lookup will always return non-NULL */
    a = wm2_aux_confstate_lookup(ifname, WM2_AUX_RADIUS_CONFIG);
    b = wm2_aux_confstate_lookup(ifname, WM2_AUX_RADIUS_STATE);

    ds_tree_foreach(&a->tree, ra) {
        if (!ds_tree_find(&b->tree, ra))
            return true;
    }
    ds_tree_foreach(&b->tree, rb) {
        if (!ds_tree_find(&a->tree, rb))
            return true;
    }
    return false;
}

static bool
wm2_nbors_config_changed(const char *ifname)
{
    struct wm2_aux_confstate *a, *b;
    struct wm2_nbors_confstate *na, *nb;

    /* Lookup will always return non-NULL */
    a = wm2_aux_confstate_lookup(ifname, WM2_AUX_NEIGHBOR_CONFIG);
    b = wm2_aux_confstate_lookup(ifname, WM2_AUX_NEIGHBOR_STATE);

    ds_tree_foreach(&a->tree, na) {
        if (!ds_tree_find(&b->tree, na))
            return true;
    }
    ds_tree_foreach(&b->tree, nb) {
        if (!ds_tree_find(&a->tree, nb))
            return true;
    }
    return false;
}

static int
wm2_radius_get(const struct schema_Wifi_VIF_Config *vconf,
               struct schema_RADIUS *radius_list,
               int size)
{
    int i;
    int n = 0;
    struct wm2_aux_confstate *config;
    struct ds_tree *tree;

    memset(radius_list, 0, sizeof(*radius_list) * size);

    if (!vconf->primary_radius_exists) {
        LOGD("%s: primary RADIUS does not exist. "
             "skip RADIUS configuration", vconf->if_name);
        return 0;
    }

    config = wm2_aux_confstate_lookup(vconf->if_name, WM2_AUX_RADIUS_CONFIG);
    tree = &config->tree;
    wm2_aux_confstate_subtree_free(config);

    /* primary RADIUS server */
    if (wm2_append_radius_by_uuid(vconf, radius_list, vconf->primary_radius.uuid)) {
        LOGW("%s: resolving primary RADIUS server failed", vconf->if_name);
        goto trunc;
    }

    wm2_aux_radius_confstate_add(tree, radius_list->ip_addr, radius_list->port,
                                 radius_list->secret, wm2_radius_str2type(radius_list->type));
    radius_list++, size--, n++;

    /* secondary RADIUS servers (list) */
    for (i = 0; i < vconf->secondary_radius_len && size > 0; i++) {
        if (wm2_append_radius_by_uuid(vconf, radius_list, vconf->secondary_radius[i].uuid))
            continue;
        wm2_aux_radius_confstate_add(tree, radius_list->ip_addr, radius_list->port,
                                     radius_list->secret, wm2_radius_str2type(radius_list->type));
        radius_list++, size--, n++;
    }

    if (!vconf->primary_accounting_exists) {
        LOGI("%s: accounting RADIUS servers not configured", vconf->if_name);
        goto trunc;
    }

    if (size < 1)
        goto trunc;

    /* primary RADIUS accounting server */
    if (wm2_append_radius_by_uuid(vconf, radius_list, vconf->primary_accounting.uuid)) {
        LOGW("%s: resolving primary RADIUS accounting server failed",
                vconf->if_name);
    }
    wm2_aux_radius_confstate_add(tree, radius_list->ip_addr, radius_list->port,
                                 radius_list->secret, wm2_radius_str2type(radius_list->type));
    radius_list++, size--, n++;

    /* secondary RADIUS accounting servers (list) */
    for (i = 0; i < vconf->secondary_accounting_len && size > 0; i++) {
        if (wm2_append_radius_by_uuid(vconf, radius_list, vconf->secondary_accounting[i].uuid))
            continue;
        wm2_aux_radius_confstate_add(tree, radius_list->ip_addr, radius_list->port,
                                     radius_list->secret, wm2_radius_str2type(radius_list->type));
        radius_list++, size--, n++;
    }

trunc:
    if (size == 0 && (n < (2 + vconf->secondary_radius_len
                             + vconf->secondary_accounting_len))) {
        LOGW("%s: radius list truncated to %d entries, "
             "please adjust the size at compile time!",
             vconf->if_name, n);
    }

    return n;
}

static int
wm2_neighbors_get(struct schema_Wifi_VIF_Config *vconf,
                  struct schema_Wifi_VIF_Neighbors *nbors_list,
                  int max_size)
{
    struct schema_Wifi_VIF_Neighbors *ovs_nbors;
    struct schema_Wifi_VIF_Neighbors *nbor;
    struct schema_Wifi_VIF_State *vifstate;
    struct wm2_aux_confstate *config;
    struct ds_tree *tree;
    int n, count = 0;
    json_t *where;

    /* Skip configuring Neighbors on non-ft enabled interfaces */
    if (!wm2_vconf_key_mgmt_contains(vconf, "ft-"))
        return 0;

    memset(nbors_list, 0, sizeof(*nbors_list) * max_size);
    config = wm2_aux_confstate_lookup(vconf->if_name, WM2_AUX_NEIGHBOR_CONFIG);
    tree = &config->tree;
    wm2_aux_confstate_subtree_free(config);

    if ((ovs_nbors = ovsdb_table_select_where(&table_Wifi_VIF_Neighbors, NULL, &n))) {
        for (nbor = ovs_nbors; n > 0 && count < max_size; n--, nbor++) {
            /* The current implementation assumes controller is not aware of the new
             * 'ft_enabled' field in VIF_Neighbors table. It then adds all neighboring
             * access points to the RxKH list. When controller implements this feature
             * the list of RxKHs will be limitted to only required APs */
            if ((nbor->ft_enabled_exists && nbor->ft_enabled == true) ||
                !nbor->ft_enabled_exists) {
                memcpy(nbors_list, nbor, sizeof(*nbor));
                wm2_aux_nbors_confstate_add(tree, nbor->bssid, nbor->nas_identifier_exists ?
                                            nbor->nas_identifier : nbor->bssid, nbor->ft_encr_key_exists ?
                                            nbor->ft_encr_key : WM2_DEFAULT_FT_KEY, WM2_NBOR_R0KH);

                /* Put duplicated entries for R1KHs. nas_identifier is obsolete. */
                wm2_aux_nbors_confstate_add(tree, nbor->bssid, nbor->bssid, nbor->ft_encr_key_exists ?
                                            nbor->ft_encr_key : WM2_DEFAULT_FT_KEY, WM2_NBOR_R1KH);
                nbors_list++; count++;
            }
        }
        FREE(ovs_nbors);
    }

    /* Add local interfaces, even 'this' */
    if ((where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_State, ssid), vconf->ssid))) {
        if ((vifstate = ovsdb_table_select_where(&table_Wifi_VIF_State, where, &n))) {
            for (n--; n>=0 && count<max_size; n--) {
                SCHEMA_SET_STR(nbors_list->bssid, vifstate[n].mac);
                SCHEMA_SET_STR(nbors_list->nas_identifier, vifstate[n].nas_identifier_exists ?
                               vifstate[n].nas_identifier : vifstate[n].mac);
                SCHEMA_SET_STR(nbors_list->ft_encr_key, vifstate[n].ft_encr_key_exists ?
                               vifstate[n].ft_encr_key : WM2_DEFAULT_FT_KEY);

                wm2_aux_nbors_confstate_add(tree, nbors_list->bssid, nbors_list->nas_identifier,
                                            nbors_list->ft_encr_key, WM2_NBOR_R0KH);
                /* Put duplicated entries for R1KHs */
                wm2_aux_nbors_confstate_add(tree, nbors_list->bssid, nbors_list->bssid,
                                            nbors_list->ft_encr_key, WM2_NBOR_R1KH);
                nbors_list++, count++;
            }
            FREE(vifstate);
        }
    }

    return count;
}

static bool
wm2_vstate_sta_is_connected(const char *ifname)
{
    struct schema_Wifi_VIF_State vstate = {0};
    bool ok;

    ok = ovsdb_table_select_one(&table_Wifi_VIF_State,
                                SCHEMA_COLUMN(Wifi_VIF_State, if_name),
                                ifname,
                                &vstate);
    if (!ok)
        return false;
    if (strcmp(vstate.mode, SCHEMA_CONSTS_VIF_MODE_STA))
        return false;

    return strlen(vstate.parent) > 0;
}

static bool
wm2_vconf_is_sta_changing_parent(const struct schema_Wifi_VIF_Config *vconf,
                                 const struct schema_Wifi_VIF_Config_flags *vchanged,
                                 const struct schema_Wifi_VIF_State *vstate,
                                 int n_cconfs)
{
    if (strcmp(vconf->mode, "sta"))
        return false;

    if (vchanged->parent)
        return true;

    if (vchanged->ssid && strlen(vconf->ssid))
        return true;

    if (n_cconfs > 0 && strlen(vstate->parent) == 0)
        return true;

    return false;
}

static bool
wm2_rstate_dfs_no_channels(const struct schema_Wifi_Radio_State *rstate)
{
    int i;
    if (rstate->channels_len == 0)
        return false;

    for (i = 0; i < rstate->channels_len; i++) {
        bool available = (strstr(rstate->channels[i], "nop_started") == NULL);
        if (available)
            return false;
    }
    return true;
}

static bool
wm2_rconf_lookup_sta(struct schema_Wifi_VIF_State *vstate,
                     const struct schema_Wifi_Radio_State *rstate)
{
    struct schema_Wifi_VIF_State *vstates;
    int i;
    int n;

    memset(vstate, 0, sizeof(*vstate));
    if (!(vstates = ovsdb_table_select_where(&table_Wifi_VIF_State, json_array(), &n)))
        return false;
    while (n--)
        if (!strcmp(vstates[n].mode, "sta"))
            for (i = 0; i < rstate->vif_states_len; i++)
                if (!strcmp(rstate->vif_states[i].uuid, vstates[n]._uuid.uuid))
                    memcpy(vstate, &vstates[n], sizeof(*vstate));
    FREE(vstates);
    return strlen(vstate->if_name) > 0;
}

static bool
wm2_rconf_recalc_fixup_channel(struct schema_Wifi_Radio_Config *rconf,
                               const struct schema_Wifi_Radio_State *rstate)
{
    struct schema_Wifi_Radio_Config_flags rchanged;
    struct schema_Wifi_VIF_Config_flags vchanged;
    struct schema_Wifi_VIF_Config vconf;
    struct schema_Wifi_VIF_State vstate;

    if (!wm2_rconf_changed(rconf, rstate, &rchanged))
        return false;
    if (!rchanged.channel)
        return false;
    if (!wm2_rconf_lookup_sta(&vstate, rstate))
        return false;
    if (!ovsdb_table_select_one(&table_Wifi_VIF_Config,
                                SCHEMA_COLUMN(Wifi_VIF_Config, if_name),
                                vstate.if_name,
                                &vconf))
        return false;
    if (!vstate.channel_exists)
        return false;
    if (wm2_vconf_changed(&vconf, &vstate, &vchanged) && vchanged.parent)
        return false;

    /* FIXME: This will not work with multiple sta vaps for
     * fallbacks. This needs to be solved with a new
     * Radio_Config column expressing sta/ap channel policy,
     * i.e. when radio_config channel is more important than
     * sta vif channel in cases of sta csa, sta (re)assoc.
     */
    LOGN("%s: ignoring channel change %d -> %d because sta vif %s is connected on %d, see CAES-600",
            rconf->if_name, rstate->channel, rconf->channel,
            vstate.if_name, vstate.channel);
    rconf->channel = vstate.channel;
    return true;
}

static void
wm2_rconf_recalc_fixup_op_mode(struct schema_Wifi_Radio_Config *rconf,
                               const struct schema_Wifi_Radio_State *rstate)
{
    struct schema_Wifi_VIF_State vstate;

    if (!wm2_rconf_lookup_sta(&vstate, rstate))
        return;
    if (!vstate.parent_exists)
        return;
    if (strlen(vstate.parent) == 0)
        return;

    /* Many drivers don't support mixing operation modes and
     * if they act as a repeater they will inherit parameters
     * from the parent AP onto local APs. Therefore it makes
     * no sense to enforce hw_mode/ht_mode because it'd just
     * result in trying to do the impossible and churn
     * through reconnects.
     */

    LOGD("%s: inheriting parent ap ht_mode and hw_mode", rconf->if_name);

    if (rstate->hw_mode_exists)
        SCHEMA_SET_STR(rconf->hw_mode, rstate->hw_mode);

    if (rstate->ht_mode_exists)
        SCHEMA_SET_STR(rconf->ht_mode, rstate->ht_mode);
}

static void
wm2_rconf_recalc_fixup_tx_chainmask(struct schema_Wifi_Radio_Config *rconf)
{
    if (!rconf->tx_chainmask_exists && !rconf->thermal_tx_chainmask_exists)
        return;
    if (!rconf->thermal_tx_chainmask_exists)
        return;
    if (rconf->tx_chainmask_exists && bitcount(rconf->tx_chainmask) < bitcount(rconf->thermal_tx_chainmask))
        return;

    rconf->tx_chainmask = rconf->thermal_tx_chainmask;
    rconf->tx_chainmask_exists = true;
}

static void
wm2_vconf_recalc(const char *ifname, bool force)
{
    struct schema_Wifi_Radio_Config rconf;
    struct schema_Wifi_Radio_State rstate;
    struct schema_Wifi_VIF_Config vconf;
    struct schema_Wifi_VIF_State vstate;
    struct schema_Wifi_Credential_Config cconfs[8];
    struct schema_Wifi_VIF_Config_flags vchanged;
    struct schema_Wifi_VIF_Neighbors nbors_list[WM2_FT_NEIGHBORS_SUPPORTED_NUM];
    struct schema_RADIUS radius_list[WM2_RADIUS_SUPPORTED_NUM];
    int num_cconfs;
    int num_nbors_list = 0;
    int num_radius_list = 0;
    bool changed = false;
    bool ft_enabled;
    bool eap_enabled;
    bool dpp_enabled;
    bool want;
    bool has;

    LOGD("%s: recalculating", ifname);

    memset(&rconf, 0, sizeof(rconf));

    if (!(want = ovsdb_table_select_one(&table_Wifi_VIF_Config,
                                        SCHEMA_COLUMN(Wifi_VIF_Config, if_name),
                                        ifname,
                                        &vconf)))
        wm2_vconf_init_del(&vconf, ifname);

    if (!(has = ovsdb_table_select_one(&table_Wifi_VIF_State,
                                       SCHEMA_COLUMN(Wifi_VIF_State, if_name),
                                       ifname,
                                       &vstate)))
        wm2_vstate_init(&vstate, ifname);

    if (want == true) {
        wm2_l2uf_if_enable(ifname);
        if (strcmp(vconf.mode, "sta") == 0)
            /*
             * Monitor STA interfaces in passive mode, due to quirky behavior
             * seen with wlan drivers using L2UF for loop-detection purposes
             * in DBDC/Bonding like operations.  Passive mode allows us to
             * see the fact that the driver is exhibiting this behavior
             */
            wm2_l2uf_if_set_passive(ifname, true);
    }
    if (want == false) wm2_l2uf_if_disable(ifname);

    /* This is workaround to deal with unpatched controller.
     * Having this on device side prevents it from saner 3rd
     * party gw integrations where currently State needs to
     * be copied over to Config to satisfy the architecture.
     *
     * This workaround is intended to not be applied on
     * ap_vlan (wds) interfaces which inherently will almost
     * always have only Wifi_VIF_State rows but no
     * Wifi_VIF_Config rows associated.
     */
    if (!want && has && strcmp(vstate.mode, "ap_vlan") && vstate.enabled) {
        LOGN("%s: config is gone, but non-ap_vlan bss is running - workaround to put it down", ifname);
        SCHEMA_SET_INT(vconf.enabled, false);
        want = true;

        /* This will invalidate chirping job if any. wm2_dpp_recalc() and
         * wm2_dpp_job_submit() will soon call deferred recalcs, including this
         * vif's one.
         */
        wm2_dpp_ifname_set(ifname, false);
        wm2_dpp_interrupt();
    }

    if (!want)
        return;

    if (!wm2_lookup_rconf_by_vif_ifname(&rconf, vconf.if_name)) {
        LOGI("%s: no radio config found, will retry later", vconf.if_name);
        return;
    }

    if (!ovsdb_table_select_one(&table_Wifi_Radio_State,
                                SCHEMA_COLUMN(Wifi_Radio_State, if_name),
                                rconf.if_name,
                                &rstate)) {
        /* This essentialy handles the initial config setup
         * with 3rd party middleware.
         */
        LOGI("%s: no radio state found, will retry later", vconf.if_name);
        return;
    }

#if 0
    /* I originally intended to defer vconf configuration
     * until after radio is fully configred too. However
     * this presents a chicken-egg problem where target
     * implementation may not be able to set up certain
     * rconf knobs, e.g. channel, until there's at least 1
     * vif created. But if rconf/rstate don't match how
     * would first vif be ever created then?
     *
     * Forcing target implementation to fake rstate to match
     * rconf is asking for trouble and breaks the very idea
     * of desired config and system state.
     *
     * I'm leaving this in case someone ever thinks of
     * comparing rconf and rstate.
     */
    if (wm2_rconf_changed(&rconf, &rstate, &rchanged)) {
        LOGI("%s: radio config doesn't match radio state, will retry later", vconf.if_name);
        return;
    }
#endif

    wm2_aux_confstate_gc();

    num_cconfs = wm2_cconf_get(&vconf, cconfs, sizeof(cconfs)/sizeof(cconfs[0]));

    if ((ft_enabled = wm2_vconf_key_mgmt_contains(&vconf, "ft-")))
        num_nbors_list = wm2_neighbors_get(&vconf, nbors_list, ARRAY_SIZE(nbors_list));

    if ((eap_enabled = wm2_vconf_key_mgmt_contains(&vconf, "-eap")))
        num_radius_list = wm2_radius_get(&vconf, radius_list, ARRAY_SIZE(radius_list));

    if (has && strlen(SCHEMA_KEY_VAL(vconf.security, "key")) < 8 && !vconf.wpa_exists && !strcmp(vconf.mode, "sta")) {
        LOGD("%s: overriding 'ssid' and 'security' for onboarding", ifname);
        vconf.ssid_exists = vstate.ssid_exists;
        STRSCPY(vconf.ssid, vstate.ssid);
        memcpy(vconf.security, vstate.security, sizeof(vconf.security));
    }

    if (want && vconf.security_len > 0 && vconf.wpa_exists) {
        LOGN("%s: Both `security` and `wpa` in Wifi_VIF_Config are set, ignoring `security`",
             vconf.if_name);
        memset(vconf.security, 0, sizeof(vconf.security));
        memset(vconf.security_keys, 0, sizeof(vconf.security_keys));
        vconf.security_len = 0;
    }

    if (want && has && !rconf.enabled) {
        LOGI("%s: disabling because radio %s is disabled too", vconf.if_name, rconf.if_name);
        SCHEMA_SET_INT(vconf.enabled, false);
    }

    if (want && !vconf.enabled && (!has || !vstate.enabled))
        return;

    if (has && wm2_rstate_dfs_no_channels(&rstate)) {
        /*
         * The easiest solution would be to simply deconfigure the interfaces
         * here. However with the current semantics that would not work for all
         * targets. Some would remove the interfaces instead of just 'downing'
         * them. Also, if they were all 'downed', the DFS state might be lost
         * due to a microcode reset. That could result in regulatory violation.
         * Therefore settle by not touching the interfaces.
         *
         * See wm2_rconf_recalc() for the other part of this behavior.
         */
        LOGI("%s: ignoring config, no channels available due to dfs", vconf.if_name);
        wm2_delayed_recalc(wm2_vconf_recalc, ifname);
        return;
    }

    if (want && wm2_dpp_in_progress(ifname) && wm2_dpp_is_chirping()) {
        LOGI("%s: skipping recalc, dpp onboarding attempt in progress", ifname);
        wm2_delayed_recalc(wm2_vconf_recalc, ifname);
        return;
    }

    /* Compare config with state to see if vif requires recalc */
    changed |= wm2_vconf_changed(&vconf, &vstate, &vchanged);
    changed |= (eap_enabled && wm2_radius_config_changed(ifname));
    changed |= (ft_enabled && wm2_nbors_config_changed(ifname));
    changed |= force;
    /* In case nothing has changed - simply return */
    if (changed == false)
        return;

    wm2_rconf_recalc_fixup_channel(&rconf, &rstate);
    wm2_rconf_recalc_fixup_nop_channel(&rconf, &rstate);
    wm2_rconf_recalc_fixup_tx_chainmask(&rconf);
    wm2_rconf_recalc_fixup_op_mode(&rconf, &rstate);

    if (vconf.dynamic_beacon_exists && vconf.dynamic_beacon &&
        vconf.ssid_broadcast_exists &&
        !strcmp("enabled", vconf.ssid_broadcast)) {
            LOGW("%s: failed to configure, dynamic beacon can be set only for hidden networks",
                 vconf.if_name);
    }

    wm2_radio_update_port_state(vconf.if_name);

    LOGI("%s@%s: changed, configuring", ifname, rconf.if_name);

    if (vchanged.parent)
        LOGN("%s: topology change: parent '%s' -> '%s'", ifname, vstate.parent, vconf.parent);

    if (vchanged.ssid && strlen(vconf.ssid) > 0 && !strcmp(vconf.mode, "sta"))
        LOGN("%s: topology change: ssid '%s' -> '%s'", ifname, vstate.ssid, vconf.ssid);

    if (wm2_vconf_is_sta_changing_parent(&vconf, &vchanged, &vstate, num_cconfs))
        TELOG_STA_ROAM(&vconf, num_cconfs);

    if (!wm2_target_vif_config_set3(&vconf, &rconf, cconfs, &vchanged,
                                    nbors_list, radius_list, num_cconfs,
                                    num_nbors_list, num_radius_list)) {
        LOGW("%s: failed to configure, will retry later", ifname);
        wm2_delayed_recalc(wm2_vconf_recalc, ifname);
        return;
    }

    dpp_enabled = true;
    dpp_enabled &= vconf.enabled;
    dpp_enabled &= strlen(vconf.parent) == 0;

    wm2_dpp_ifname_set(ifname, dpp_enabled);
    wm2_dpp_interrupt();

    if (!has) {
        /* If the target implementation delays (debounces) calling of op_vstate()
         * there's a chance client is reported by the target right after
         * interface is brought up and before a matching VIF_State record exists.
         * In that case the client would not be registered. To prevent that
         * an initial VIF_State record is immediately created here with only
         * the key field 'if_name' set (by wm2_vstate_init). This record will
         * be updated properly when the target api calls op_vstate()
         */
        vstate._partial_update = true;
        WARN_ON(!ovsdb_table_upsert(&table_Wifi_VIF_State, &vstate, false));
    }

    wm2_delayed_recalc_cancel(wm2_vconf_recalc, ifname);
}

static void
wm2_rstate_init(struct schema_Wifi_Radio_State *rstate, const char *phy)
{
    struct schema_Wifi_Radio_Config rconf;

    memset(rstate, 0, sizeof(*rstate));
    rstate->_partial_update = true;
    SCHEMA_SET_STR(rstate->if_name, phy);
    SCHEMA_SET_STR(rstate->freq_band, "2.4G"); /* satisfy ovsdb constraint */

    /* best effort to fixup dummy freq_band, will converge eventually anyway */
    if (ovsdb_table_select_one(&table_Wifi_Radio_Config,
                               SCHEMA_COLUMN(Wifi_Radio_Config, if_name),
                               phy,
                               &rconf))
        SCHEMA_SET_STR(rstate->freq_band, rconf.freq_band);
}

static void
wm2_rconf_recalc_vifs(const struct schema_Wifi_Radio_Config *rconf)
{
    struct schema_Wifi_VIF_Config vconf;
    json_t *where;
    int i;

    /* For VIF_Config to be properly configured (created)
     * the associated parent Radio_Config must be known.
     * However OVSDB events can come in reverse order such
     * as vif_configs don't contain VIF_Config uuid at a
     * given time.
     *
     * This makes sure all vifs are in-sync VIF_Config vs
     * VIF_State and by proxy handle the described corner
     * case by hooking up to Radio_Config recalculations
     * which will eventually contain updated vif_configs.
     */
    for (i = 0; i < rconf->vif_configs_len; i++) {
        if (!(where = ovsdb_where_uuid("_uuid", rconf->vif_configs[i].uuid)))
            continue;
        if (ovsdb_table_select_one_where(&table_Wifi_VIF_Config, where, &vconf))
            wm2_vconf_recalc(vconf.if_name, false);
    }
}

static void
wm2_rconf_init_del(struct schema_Wifi_Radio_Config *rconf, const char *ifname)
{
    memset(rconf, 0, sizeof(*rconf));
    STRSCPY(rconf->if_name, ifname);
    rconf->if_name_present = true;
    rconf->if_name_exists = true;
    rconf->enabled_present = true;
    rconf->enabled_changed = true;
    rconf->enabled_exists = true;
}

static int
wm2_rstate_clip_chwidth(const struct schema_Wifi_Radio_State *rstate,
                        int channel,
                        const char *ht_mode)
{
    const char *width_str = strpbrk(ht_mode, "1234567890");
    int orig_width = atoi(width_str);
    int width = orig_width;
    int i;

    LOGD("%s: dfs: nol: clipping: %d %s -> %d %s",
         rstate->if_name,
         rstate->channel, rstate->ht_mode,
         channel, ht_mode);

    for (;;) {
        const int *chans = unii_5g_chan2list(channel, width);
        if (chans == NULL) {
            LOGI("%s: dfs: nol: %d %s: cannot be used at any width",
                 rstate->if_name, channel, ht_mode);
            return 0;
        }

        bool usable = true;
        int j;
        for (i = 0; chans[i]; i++) {
            for (j = 0; j < rstate->channels_len; j++) {
                if (atoi(rstate->channels_keys[j]) == chans[i]) break;
            }
            const bool found = (j < rstate->channels_len);
            const bool blocked = found && (strstr(rstate->channels[j], "nop_started") != NULL);

            if (found == false) {
                LOGI("%s: dfs: nol: %d %s: at HT%d: channel: %d not supported",
                     rstate->if_name, channel, ht_mode, width, chans[i]);
                usable = false;
                break;
            }

            if (blocked == true) {
                LOGI("%s: dfs: nol: %d %s: at HT%d: channel %d: radar blocked",
                     rstate->if_name, channel, ht_mode, width, chans[i]);
                usable = false;
                break;
            }
        }

        if (usable == true) {
            break;
        }

        LOGD("%s: dfs: nol: %d %s: considering HT%d",
             rstate->if_name, channel, ht_mode, width);

        width /= 2;
    }

    if (width < orig_width) {
        LOGI("%s: dfs: nol: %d %s: downgraded to HT%d",
             rstate->if_name, channel, ht_mode, width);
    }

    return width;
}

void
wm2_rconf_recalc_fixup_nop_channel(struct schema_Wifi_Radio_Config *rconf,
                                   const struct schema_Wifi_Radio_State *rstate)
{
    const char *band_5g = SCHEMA_CONSTS_RADIO_TYPE_STR_5G;
    const char *band_5gl = SCHEMA_CONSTS_RADIO_TYPE_STR_5GL;
    const char *band_5gu = SCHEMA_CONSTS_RADIO_TYPE_STR_5GU;
    const bool is_5ghz = (strcmp(rconf->freq_band, band_5g) == 0)
                      || (strcmp(rconf->freq_band, band_5gl) == 0)
                      || (strcmp(rconf->freq_band, band_5gu) == 0);
    const bool is_not_5ghz = !is_5ghz;

    /* FIXME: This function could be extended to support
     * other bands as well to simply handle "unsupported
     * channels". That'll require wm2_rstate_clip_chwidth()
     * to fix call to unii_5g_chan2list().
     */
    if (is_not_5ghz)
        return;

    if (rstate->channels_len == 0)
        return;

    const char *desired_ht_mode = rconf->ht_mode_exists
                                ? rconf->ht_mode
                                : (rstate->ht_mode_exists
                                   ? rstate->ht_mode
                                   : NULL);
    const bool no_desired_ht_mode = (desired_ht_mode == NULL);
    if (no_desired_ht_mode)
        return;

    if (rconf->channel_exists) {
        const int new_width = wm2_rstate_clip_chwidth(rstate, rconf->channel, desired_ht_mode);
        if (new_width > 0) {
            const char *ht_mode = strfmta("HT%d", new_width);
            SCHEMA_SET_STR(rconf->ht_mode, ht_mode);
            return;
        }
    }

    if (rstate->channel_exists) {
        const int new_width = wm2_rstate_clip_chwidth(rstate, rstate->channel, desired_ht_mode);
        if (new_width > 0) {
            const char *ht_mode = strfmta("HT%d", new_width);
            LOGI("%s: dfs: nol: staying on current channel: %d %s",
                 rstate->if_name,
                 rstate->channel,
                 ht_mode);
            SCHEMA_SET_INT(rconf->channel, rstate->channel);
            SCHEMA_SET_STR(rconf->ht_mode, ht_mode);
            return;
        }
    }

    LOGI("%s: dfs: nol: no channel available", rstate->if_name);
    SCHEMA_CPY_INT(rconf->channel, rstate->channel);
    SCHEMA_CPY_STR(rconf->ht_mode, rstate->ht_mode);
}

static void
wm2_rconf_recalc(const char *ifname, bool force)
{
    struct schema_Wifi_Radio_Config rconf;
    struct schema_Wifi_Radio_State rstate;
    struct schema_Wifi_Radio_Config_flags changed;
    bool want;
    bool has;

    LOGD("%s: recalculating", ifname);

    if (!(want = ovsdb_table_select_one(&table_Wifi_Radio_Config,
                                        SCHEMA_COLUMN(Wifi_Radio_Config, if_name),
                                        ifname,
                                        &rconf)))
        wm2_rconf_init_del(&rconf, ifname);

    if (!(has = ovsdb_table_select_one(&table_Wifi_Radio_State,
                                       SCHEMA_COLUMN(Wifi_Radio_State, if_name),
                                       ifname,
                                       &rstate)))
        memset(&rstate, 0, sizeof(rstate));

    if (rconf.channel_sync_exists) {
        if (rconf.channel_sync) {
            LOGW("%s: forcing reconfig", rconf.if_name);
            force = true;
        }
        rconf.channel_sync_exists = false;
    }

    if (want)
        if (wm2_rconf_recalc_fixup_channel(&rconf, &rstate))
            wm2_delayed_recalc(wm2_rconf_recalc, ifname);

    if (want && rconf.vif_configs_len == 0) {
        LOGD("%s: ignoring rconf channel, ht_mode and hw_mode: no VIFs configured", rconf.if_name);
        rconf.channel_exists = false;
        rconf.ht_mode_exists = false;
        rconf.hw_mode_exists = false;
    }

    if (want) {
        wm2_rconf_recalc_fixup_nop_channel(&rconf, &rstate);
        wm2_rconf_recalc_fixup_tx_chainmask(&rconf);
        wm2_rconf_recalc_fixup_op_mode(&rconf, &rstate);
    }

    if (has && wm2_rstate_dfs_no_channels(&rstate)) {
        /* This is intended to have a side-effect - on some platforms
         * - to re-create vifs when dfs nol is cleared and some
         * channels become available again.
         */
        LOGI("%s: no channels available due to dfs, disabling", rconf.if_name);
        SCHEMA_SET_BOOL(rconf.enabled, false);
    }

    if (want && !rconf.enabled && (!has || !rstate.enabled))
        return;

    if (want && wm2_dpp_is_chirping()) {
        LOGI("%s: skipping recalc, dpp onboarding attempt in progress", ifname);
        wm2_delayed_recalc(wm2_rconf_recalc, ifname);
        return;
    }

    if (!wm2_rconf_changed(&rconf, &rstate, &changed) && !force)
        goto recalc;

    LOGI("%s: changed, configuring", ifname);

    if ((changed.channel || changed.ht_mode) && rconf.channel) {
        LOGN("%s: topology change: channel %d @ %s -> %d @ %s",
             ifname,
             rstate.channel, rstate.ht_mode,
             rconf.channel, rconf.ht_mode);
    }

    if (!wm2_target_radio_config_set2(&rconf, &changed)) {
        LOGW("%s: failed to configure, will retry later", ifname);
        wm2_delayed_recalc(wm2_rconf_recalc, ifname);
        return;
    }

    wm2_dpp_interrupt();

    if (has && !want) {
        ovsdb_table_delete_simple(&table_Wifi_Radio_State,
                                  SCHEMA_COLUMN(Wifi_Radio_State, if_name),
                                  ifname);
    }

    wm2_delayed_recalc_cancel(wm2_rconf_recalc, ifname);
recalc:
    wm2_rconf_recalc_vifs(&rconf);
}

static void
wm2_op_vconf(const struct schema_Wifi_VIF_Config *vconf,
             const char *phy)
{
    struct schema_Wifi_VIF_Config tmp;
    json_t *where;

    memcpy(&tmp, vconf, sizeof(tmp));
    LOGI("%s @ %s: updating vconf", vconf->if_name, phy);
    REQUIRE(vconf->if_name, strlen(vconf->if_name) > 0);
    REQUIRE(vconf->if_name, vconf->_partial_update);
    if (!(where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), phy)))
        return;
    REQUIRE(vconf->if_name, ovsdb_table_upsert_with_parent(&table_Wifi_VIF_Config,
                                                           &tmp,
                                                           false, // uuid not needed
                                                           NULL,
                                                           // parent:
                                                           SCHEMA_TABLE(Wifi_Radio_Config),
                                                           where,
                                                           SCHEMA_COLUMN(Wifi_Radio_Config, vif_configs)));
    LOGI("%s @ %s: updated vconf", vconf->if_name, phy);
    wm2_delayed_recalc(wm2_vconf_recalc, vconf->if_name);
}

static void
wm2_op_rconf(const struct schema_Wifi_Radio_Config *rconf)
{
    struct schema_Wifi_Radio_Config tmp;
    memcpy(&tmp, rconf, sizeof(tmp));
    LOGI("%s: updating rconf", rconf->if_name);
    REQUIRE(rconf->if_name, strlen(rconf->if_name) > 0);
    REQUIRE(rconf->if_name, rconf->_partial_update);
    OVERRIDE(rconf->if_name, tmp.vif_configs_present, false);
    tmp.vif_configs_len = 0;
    tmp.vif_configs_present = true;
    REQUIRE(rconf->if_name, 1 == ovsdb_table_upsert_f(&table_Wifi_Radio_Config, &tmp, false, NULL));
    LOGI("%s: updated rconf", rconf->if_name);
    wm2_delayed_recalc(wm2_rconf_recalc, rconf->if_name);
}

static void
wm2_vstate_security_fixup(const struct schema_Wifi_VIF_Config *vconf,
                          struct schema_Wifi_VIF_State *vstate)
{
    struct schema_Wifi_VIF_State orig;
    const char *key;
    const char *val;
    bool want_mode;
    bool has_mixed;
    int i;

    want_mode = SCHEMA_KEY_VAL_NULL(vconf->security, "mode") != NULL;
    has_mixed = !strcmp(SCHEMA_KEY_VAL(vstate->security, "mode"), "mixed");

    memcpy(&orig, vstate, sizeof(*vstate));
    memset(vstate->security, 0, sizeof(vstate->security));
    memset(vstate->security_keys, 0, sizeof(vstate->security_keys));
    vstate->security_len = 0;

    for (i = 0; i < orig.security_len; i++) {
        key = orig.security_keys[i];
        val = orig.security[i];

        if (strstr(key, "oftag") == key)
            continue;

        if (!strcmp(key, "mode") && !want_mode && has_mixed)
            continue;

        SCHEMA_KEY_VAL_APPEND(vstate->security, key, val);
    }

    for (i = 0; i < vconf->security_len; i++) {
        key = vconf->security_keys[i];
        val = vconf->security[i];

        if (strstr(key, "oftag") == key)
            SCHEMA_KEY_VAL_APPEND(vstate->security, key, val);
    }

    if (vconf->security_len) {
        SCHEMA_UNSET_FIELD(vstate->wpa);
        SCHEMA_UNSET_MAP(vstate->wpa_key_mgmt);
        SCHEMA_UNSET_MAP(vstate->wpa_psks);
        SCHEMA_UNSET_FIELD(vstate->radius_srv_addr);
        SCHEMA_UNSET_FIELD(vstate->radius_srv_port);
        SCHEMA_UNSET_FIELD(vstate->radius_srv_secret);
    }
    else {
        SCHEMA_UNSET_MAP(vstate->security);
        SCHEMA_UNSET_FIELD(vstate->ft_psk);
    }
}

static void
wm2_vstate_wpa_psks_keys_fixup_empty(struct schema_Wifi_VIF_State *vstate)
{
    const bool is_sta = (strcmp(vstate->mode, "sta") == 0);
    const bool has_one_psk = (vstate->wpa_psks_len == 1);
    const bool has_first_keyid_empty = (strlen(vstate->wpa_psks_keys[9]) == 0);
    if (is_sta && has_one_psk && has_first_keyid_empty) {
        STRSCPY_WARN(vstate->wpa_psks_keys[0], "key");
    }
}

static void
wm2_vstate_wpa_psks_keys_fixup(const struct schema_Wifi_VIF_Config *vconf,
                               struct schema_Wifi_VIF_State *vstate)
{
    if (vconf->wpa_exists == false || vconf->wpa == false) return;
    if (strcmp(vconf->mode, "sta") != 0) return;
    if (vconf->wpa_psks_len != 1) return;
    if (vstate->wpa_psks_len != 1) return;
    if (strcmp(vconf->wpa_psks[0], vstate->wpa_psks[0]) != 0) return;

    STRSCPY(vstate->wpa_psks_keys[0], vconf->wpa_psks_keys[0]);
}

static void
wm2_op_vstate(const struct schema_Wifi_VIF_State *vstate, const char *phy)
{
    struct schema_Wifi_Radio_State rstate;
    struct schema_Wifi_VIF_Config vconf;
    struct schema_Wifi_VIF_State state;
    struct schema_Wifi_VIF_State oldstate;
    json_t *where;
    bool rstate_exists;
    int i;

    memcpy(&state, vstate, sizeof(state));

    LOGD("%s: updating vif state", state.if_name);
    REQUIRE(state.if_name, strlen(state.if_name) > 0);
    REQUIRE(state.if_name, state._partial_update);
    OVERRIDE(state.if_name, state.associated_clients_present, false);
    OVERRIDE(state.if_name, state.vif_config_present, false);

    str_tolower(state.parent);
    for (i = 0; i < state.mac_list_len; i++)
        str_tolower(state.mac_list[i]);

    memset(&oldstate, 0, sizeof(oldstate));
    if ((where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_State, if_name), state.if_name)))
        if (!ovsdb_table_select_one_where(&table_Wifi_VIF_State, where, &oldstate))
            wm2_vstate_init(&oldstate, state.if_name);

    memset(&rstate, 0, sizeof(rstate));
    rstate_exists = false;
    if (phy)
        if ((where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_State, if_name), phy)))
            if (ovsdb_table_select_one_where(&table_Wifi_Radio_State, where, &rstate))
                rstate_exists = true;

    if (!rstate_exists && phy)
        wm2_rstate_init(&rstate, phy);

    /* Workaround for PIR-11008 */
    if (!state.ft_psk_exists) {
        state.ft_psk_exists = true;
        state.ft_psk = 0;
    }

    if (wm2_lookup_vconf_by_ifname(&vconf, state.if_name)) {
        wm2_vstate_security_fixup(&vconf, &state);
        wm2_vstate_wpa_psks_keys_fixup_empty(&state);
        wm2_vstate_wpa_psks_keys_fixup(&vconf, &state);
        state.vif_config_exists = true;
        state.vif_config_present = true;
        memcpy(&state.vif_config, &vconf._uuid, sizeof(vconf._uuid));
        /* This isn't functionally necessary but it does
         * limit the amount of data being sent to the
         * controller. This is tied to the condition on
         * comparing mac_list in wm2_vconf_changed().
         */
        if (!vconf.mac_list_type_exists)
            state.mac_list_len = 0;
    } else if (strcmp(state.mode, "ap_vlan")) {
        /* this will remove all non-wds related interfaces
         * from vif state if there's no corresponding vif
         * config row. this is required to keep master
         * branch working with unpatched controller.
         * unpatched controller ends up timing out
         * onboarding bcm devices which causes all sort sof
         * tests to fail and timeout too.
         *
         * once controller is patched this if-else branch
         * needs to be removed.
         */
        if (state.enabled) {
            /* this avoids a case where this workaround
             * impact the other workaround in vif recalc
             * where disappearing vif configs should
             * deconfigure interface instead of leaving it
             * alone.
             *
             * this doesn't skip the upsert intentionally so
             * that another corner case where state never
             * got to be created in the first place is
             * handled too.
             */
            LOGI("%s: not removing vif state yet, needs a recalc first", state.if_name);
        } else {
            ovsdb_table_delete_simple(&table_Wifi_VIF_State,
                                      SCHEMA_COLUMN(Wifi_VIF_State, if_name),
                                      state.if_name);
            LOGI("%s: removing vif state", state.if_name);
            goto recalc;
        }
    }

    if (state.mac_exists) {
        if (WARN_ON(!phy))
            return;

        if (!(where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_State, if_name), phy)))
            return;

        if (!rstate_exists)
            WARN_ON(!ovsdb_table_insert(&table_Wifi_Radio_State, &rstate));

        if (!strcmp(state.mode, "ap_vlan") && oldstate.mode_exists == false)
            TELOG_WDS_STATE(vstate, "created");

        REQUIRE(state.if_name, ovsdb_table_upsert_with_parent(&table_Wifi_VIF_State,
                                                              &state,
                                                              false, // uuid not needed
                                                              NULL,
                                                              // parent:
                                                              SCHEMA_TABLE(Wifi_Radio_State),
                                                              where,
                                                              SCHEMA_COLUMN(Wifi_Radio_State, vif_states)));
    } else {
        if (!strcmp(state.mode, "ap_vlan"))
            TELOG_WDS_STATE(vstate, "destroyed");

        ovsdb_table_delete_simple(&table_Wifi_VIF_State,
                                  SCHEMA_COLUMN(Wifi_VIF_State, if_name),
                                  state.if_name);
    }
    LOGI("%s: updated vif state", state.if_name);
recalc:
    /* Reconnect workaround is for CAES-771 */
    if (wm2_sta_has_reconnected(&oldstate, &state))
        wm2_radio_update_port_state_set_inactive(state.if_name);
    wm2_sta_print_status(&oldstate, state.enabled_exists ? &state : NULL);
    wm2_sta_handle_dfs_link_change(&rstate, &oldstate, &state);
    wm2_radio_update_port_state(state.if_name);
    wm2_delayed_recalc(wm2_vconf_recalc, state.if_name);
}

static void
wm2_op_rstate(const struct schema_Wifi_Radio_State *rstate)
{
    struct schema_Wifi_Radio_Config rconf;
    struct schema_Wifi_Radio_State state;

    memcpy(&state, rstate, sizeof(state));

    LOGD("%s: updating radio state", state.if_name);
    REQUIRE(state.if_name, strlen(state.if_name) > 0);
    REQUIRE(state.if_name, state._partial_update);
    OVERRIDE(state.if_name, state.radio_config_present, false);
    OVERRIDE(state.if_name, state.vif_states_present, false);
    OVERRIDE(state.if_name, state.channel_sync_present, false);
    OVERRIDE(state.if_name, state.channel_mode_present, false);

    if (!wm2_lookup_rconf_by_ifname(&rconf, state.if_name))
        return;

    memcpy(&state.radio_config, &rconf._uuid, sizeof(rconf._uuid));
    state.radio_config_exists = true;
    state.radio_config_present = true;

    if (rconf.channel_mode_exists) {
        state.channel_mode_exists = true;
        state.channel_mode_present = true;
        STRSCPY(state.channel_mode, rconf.channel_mode);
    }

    REQUIRE(state.if_name, 1 == ovsdb_table_upsert_f(&table_Wifi_Radio_State, &state, false, NULL));
    LOGI("%s: updated radio state", state.if_name);
    wm2_delayed_recalc(wm2_rconf_recalc, state.if_name);
}

static void
wm2_op_client(const struct schema_Wifi_Associated_Clients *client,
              const char *vif,
              bool associated)
{
    struct schema_Wifi_Associated_Clients tmp;
    char ifname[32];
    memcpy(&tmp, client, sizeof(tmp));
    REQUIRE(vif, tmp._partial_update);
    STRSCPY(ifname, vif);
    wm2_clients_update(&tmp, ifname, associated);
}

void
wm2_op_clients(const struct schema_Wifi_Associated_Clients *clients,
               int num,
               const char *vif)
{
    wm2_clients_update_per_vif(clients, num, vif);
}

void
wm2_op_flush_clients(const char *vif)
{
    wm2_clients_update_per_vif(NULL, 0, vif);
}

static bool
wm2_resolve_radius_uuid_from_params(ovs_uuid_t *uuid,
                                    const char *ip,
                                    const int port,
                                    const char *type)
{
    int n;
    json_t *where;
    struct schema_RADIUS *radius_p, *radius_i;

    if ((where = ovsdb_where_simple(SCHEMA_COLUMN(RADIUS, ip_addr), ip))) {
        if ((radius_p = ovsdb_table_select_where(&table_RADIUS, where, &n))) {
            for (radius_i = radius_p; n>0; n--, radius_i++) {
                if (radius_i->port == port && !strcmp(radius_i->type, type)) {
                    STRSCPY_WARN(uuid->uuid, radius_i->_uuid.uuid);
                    FREE(radius_p);
                    return true;
                }
            }
            FREE(radius_p);
        }
    }
    return false;
}

void
wm2_op_radius_state(const struct schema_RADIUS *radius_list,
                    int num,
                    const char *vif)
{
    struct schema_Wifi_VIF_State vstate;
    struct wm2_aux_confstate *state;
    struct ds_tree *tree;
    enum wm2_radius_type type;
    int i, n_auth = 0, n_acc = 0;
    ovs_uuid_t uuid;
    json_t *where;

    if (!vif || (strlen(vif) == 0) || (num == 0))
        return;

    if (!(where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_State, if_name), vif)))
        return;

    if (!ovsdb_table_select_one_where(&table_Wifi_VIF_State,
                                      where,
                                      &vstate)) {
        LOGW("%s: interface %s not found in OVSDB table", __func__, vif);
        return;
    }

    state = wm2_aux_confstate_lookup(vif, WM2_AUX_RADIUS_STATE);
    tree = &state->tree;
    wm2_aux_confstate_subtree_free(state);

    for (i = 0; (i<num) && (i<WM2_RADIUS_SUPPORTED_NUM); i++) {
        /* Add all returned radiuses to state table, even ones not
         * resolved in ovsdb. (especially those). type field is
         * required if cloud wants to change places of secondary
         * and primary servers */
        type = wm2_radius_str2type(radius_list[i].type);
        wm2_aux_radius_confstate_add(tree,
                                     radius_list[i].ip_addr,
                                     radius_list[i].port,
                                     radius_list[i].secret,
                                     type);
        if (wm2_resolve_radius_uuid_from_params(&uuid,
                                                radius_list[i].ip_addr,
                                                radius_list[i].port,
                                                radius_list[i].type)) {
            if ((type == WM2_RADIUS_TYPE_AA) &&
                (n_auth < WM2_AUTH_RADIUS_SUPPORTED_NUM)) {
                if (n_auth == 0) {
                    SCHEMA_SET_UUID(vstate.primary_radius, uuid.uuid);
                    n_auth++;
                    continue;
                }
                SCHEMA_APPEND_UUID(vstate.secondary_radius, uuid.uuid);
                n_auth++;
            }

            if ((type == WM2_RADIUS_TYPE_A) &&
                (n_acc < WM2_ACC_RADIUS_SUPPORTED_NUM)) {
                if (n_acc == 0) {
                    SCHEMA_SET_UUID(vstate.primary_accounting, uuid.uuid);
                    n_acc++;
                    continue;
                }
                SCHEMA_APPEND_UUID(vstate.secondary_accounting, uuid.uuid);
                n_acc++;
            }
        }
    }

    if (n_auth || n_acc)
        WARN_ON(!ovsdb_table_update(&table_Wifi_VIF_State, &vstate));
}

void
wm2_op_nbors_state(const struct schema_Wifi_VIF_Neighbors *nbors_list,
                   int num_nbors_list,
                   const char *vif)
{
    int i;
    struct wm2_aux_confstate *state;
    struct ds_tree *tree;

    if (num_nbors_list == 0)
        return;

    state = wm2_aux_confstate_lookup(vif, WM2_AUX_NEIGHBOR_STATE);
    tree = &state->tree;
    wm2_aux_confstate_subtree_free(state);

    for (i = 0; i<num_nbors_list && i<WM2_FT_NEIGHBORS_SUPPORTED_NUM; i++) {
        wm2_aux_nbors_confstate_add(tree, nbors_list[i].bssid,
                                    nbors_list[i].nas_identifier,
                                    nbors_list[i].ft_encr_key,
                                    WM2_NBOR_R0KH);

        wm2_aux_nbors_confstate_add(tree, nbors_list[i].bssid,
                                    nbors_list[i].bssid,
                                    nbors_list[i].ft_encr_key,
                                    WM2_NBOR_R1KH);
    }
}

static const struct target_radio_ops rops = {
    .op_vconf = wm2_op_vconf,
    .op_rconf = wm2_op_rconf,
    .op_vstate = wm2_op_vstate,
    .op_rstate = wm2_op_rstate,
    .op_client = wm2_op_client,
    .op_clients = wm2_op_clients,
    .op_flush_clients = wm2_op_flush_clients,
    .op_radius_state = wm2_op_radius_state,
    .op_nbors_state = wm2_op_nbors_state,
    .op_dpp_announcement = wm2_dpp_op_announcement,
    .op_dpp_conf_enrollee = wm2_dpp_op_conf_enrollee,
    .op_dpp_conf_network = wm2_dpp_op_conf_network,
    .op_dpp_conf_failed = wm2_dpp_op_conf_failed,
};

static void
callback_Wifi_Radio_Config(
        ovsdb_update_monitor_t          *mon,
        struct schema_Wifi_Radio_Config *old_rec,
        struct schema_Wifi_Radio_Config *rconf)
{
    LOGD("%s: ovsdb updated", rconf->if_name);
    wm2_rconf_recalc(rconf->if_name, false);
}

static void
callback_Wifi_VIF_Config(
        ovsdb_update_monitor_t          *mon,
        struct schema_Wifi_VIF_Config   *old_rec,
        struct schema_Wifi_VIF_Config   *vconf)
{
    LOGD("%s: ovsdb updated", vconf->if_name);
    wm2_vconf_recalc(vconf->if_name, false);
}

/* Forward declaration */
static void
wm2_radio_config_bump(void);

static void
callback_Wifi_VIF_Neighbors(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_VIF_Neighbors *old_rec,
        struct schema_Wifi_VIF_Neighbors *nbors)
{
    struct schema_Wifi_VIF_Config *ovs_vconfs;
    struct schema_Wifi_VIF_Config *vconf;
    int n;

    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;
        case OVSDB_UPDATE_DEL:
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if ((ovs_vconfs = ovsdb_table_select_where(&table_Wifi_VIF_Config, NULL, &n))) {
                for (vconf = ovs_vconfs; vconf && n > 0; vconf++, n-- ) {
                    if (wm2_vconf_key_mgmt_contains(vconf, "ft-")) {
                        LOGD("Interface to update Neighbors: %s", vconf->if_name);
                        wm2_vconf_recalc(vconf->if_name, false);
                    }
                }
                FREE(ovs_vconfs);
            }
            break;
    }
}

static void
callback_RADIUS(
        ovsdb_update_monitor_t  *mon,
        struct schema_RADIUS    *old_radconf,
        struct schema_RADIUS    *radconf)
{
    struct schema_Wifi_VIF_Config *vconf;
    void *buf;
    int n, i;

    LOGD("%s: radius ovsdb updated", radconf->name);
    // Resolve which VIF to recalculate based on UUID of changed RADIUS entry
    if ((buf = ovsdb_table_select_where(&table_Wifi_VIF_Config, NULL, &n))) {
        for (n--; n>=0; n--) {
            vconf = buf + (n * sizeof(*vconf));
            /* primary server */
            if (vconf->primary_radius_exists &&
                !strcmp(vconf->primary_radius.uuid,
                        radconf->_uuid.uuid)) {
                wm2_vconf_recalc(vconf->if_name, false);
            }
            /* secondary servers */
            for (i = vconf->secondary_radius_len; i>0; i--) {
                if (!strcmp(vconf->secondary_radius[i].uuid,
                            radconf->_uuid.uuid)) {
                    wm2_vconf_recalc(vconf->if_name, false);
                }
            }
        }
    }
}

static void
wm2_radio_config_bump(void)
{
    struct schema_Wifi_Radio_Config *rconf;
    struct schema_Wifi_VIF_Config *vconf;
    void *buf;
    int n;

    if ((buf = ovsdb_table_select_where(&table_Wifi_Radio_Config, NULL, &n))) {
        for (n--; n >= 0; n--) {
            rconf = buf + (n * sizeof(*rconf));
            LOGI("%s: bumping", rconf->if_name);
            wm2_rconf_recalc(rconf->if_name, true);
        }
        FREE(buf);
    }

    if ((buf = ovsdb_table_select_where(&table_Wifi_VIF_Config, NULL, &n))) {
        for (n--; n >= 0; n--) {
            vconf = buf + (n * sizeof(*vconf));
            LOGI("%s: bumping", vconf->if_name);
            wm2_vconf_recalc(vconf->if_name, true);
        }
        FREE(buf);
    }
}

/******************************************************************************
 *  PUBLIC API definitions
 *****************************************************************************/
void
wm2_radio_delayed_soon(void)
{
    struct wm2_delayed *i;
    ds_dlist_foreach(&delayed_list, i) {
        LOGD("%s: %s: remaining %.2lf seconds, rescheduling now",
             i->ifname, i->workname,
             ev_timer_remaining(EV_DEFAULT_ &i->timer));
        ev_timer_stop(EV_DEFAULT_ &i->timer);
        ev_timer_set(&i->timer, 0.0, 0.0);
        ev_timer_start(EV_DEFAULT_ &i->timer);
    }
}

int
wm2_radio_init_kickoff(void)
{
    ovsdb_table_delete_where(&table_Wifi_Associated_Clients, json_array());

    if (wm2_target_radio_config_need_reset()) {
        ovsdb_table_delete_where(&table_Wifi_Radio_Config, json_array());
        ovsdb_table_delete_where(&table_Wifi_Radio_State, json_array());
        ovsdb_table_delete_where(&table_Wifi_VIF_Config, json_array());
        ovsdb_table_delete_where(&table_Wifi_VIF_State, json_array());
        ovsdb_table_delete_where(&table_Wifi_VIF_Neighbors, json_array());
        ovsdb_table_delete_where(&table_RADIUS, json_array());
        if (!wm2_target_radio_config_init2()) {
            LOGE("Failed to initialize radio");
            return -1;
        }
    } else {
        /* This is intended to bump target when WM is
         * restarted, e.g. due to a crash. It may end up
         * calling even if Config matches State. This is
         * intended as to give an opportunity for target
         * implementation to register event/socket listeners
         * it needs to operate.
         *
         * This is intended to be nicer variant of
         * target_radio_has_config() because it doesn't
         * imply complete reconfiguration.
         */
        wm2_radio_config_bump();
    }
    return 0;
}

bool
wm2_radio_onboard_vifs(char *buf, size_t len)
{
    struct schema_Wifi_VIF_Config *vconfs;
    struct schema_Wifi_VIF_Config *vconf;
    int n_sta = 0;
    int n_ap = 0;
    int n_parent = 0;
    int n;

    vconfs = ovsdb_table_select_where(&table_Wifi_VIF_Config, NULL, &n);
    for (vconf = vconfs; vconf && n; vconf++, n--) {
        if (!strcmp(vconf->mode, SCHEMA_CONSTS_VIF_MODE_STA))
            n_sta++;
        if (!strcmp(vconf->mode, SCHEMA_CONSTS_VIF_MODE_AP))
            n_ap++;
        if (vconf->parent_exists && strlen(vconf->parent) > 0)
            n_parent++;
        if (wm2_vstate_sta_is_connected(vconf->if_name))
            n_parent++;

        if (!strcmp(vconf->mode, SCHEMA_CONSTS_VIF_MODE_STA))
            csnprintf(&buf, &len, "%s ", vconf->if_name);
    }
    FREE(vconfs);

    return n_ap == 0 && n_parent == 0 && n_sta > 0;
}

int
wm2_radio_init(void)
{
    LOGD("Initializing radios");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT(Wifi_Radio_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Radio_State, if_name);
    OVSDB_TABLE_INIT(Wifi_VIF_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_VIF_State, if_name);
    OVSDB_TABLE_INIT(Wifi_Credential_Config, _uuid);
    OVSDB_TABLE_INIT(Wifi_Associated_Clients, _uuid);
    OVSDB_TABLE_INIT(Wifi_VIF_Neighbors, _uuid);
    OVSDB_TABLE_INIT(Wifi_Master_State, if_name);
    OVSDB_TABLE_INIT(Openflow_Tag, name);
    OVSDB_TABLE_INIT(RADIUS, _uuid);

    wm2_dpp_init();

    if (WARN_ON(!wm2_target_radio_init(&rops)))
        return -1;

    wm2_radio_init_kickoff();

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Wifi_Radio_Config, true);
    OVSDB_TABLE_MONITOR(Wifi_VIF_Config, true);
    OVSDB_TABLE_MONITOR(RADIUS, true);
    OVSDB_TABLE_MONITOR(Wifi_VIF_Neighbors, true);

    return 0;
}
