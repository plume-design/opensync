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

#include <stdbool.h>
#include <stdint.h>
#include <log.h>
#include <const.h>
#include <util.h>
#include <memutil.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_conf.h>
#include <osw_module.h>
#include <telog.h>
#include <timevt.h>

#define OW_TELOG_SIZE_OF_NETWORKS 8

enum ow_telog_event
{
    OW_TELOG_REPORT_ROAM,
    OW_TELOG_NONE,
};

struct ow_telog_vif_conf_sta
{
    struct osw_hwaddr bssids[OW_TELOG_SIZE_OF_NETWORKS];
    struct osw_ssid ssids[OW_TELOG_SIZE_OF_NETWORKS];
    bool multi_aps[OW_TELOG_SIZE_OF_NETWORKS];
    size_t len;
};

struct ow_telog_vif_conf
{
    enum osw_vif_type vif_type;
    struct ow_telog_vif_conf_sta sta;
};

struct ow_telog_vif_state_ap_vlan
{
    struct osw_hwaddr_list sta_addrs;
};

struct ow_telog_vif_state_sta_link
{
    struct osw_hwaddr bssid;
    struct osw_ssid ssid;
    bool multi_ap;
};

struct ow_telog_vif_state_sta
{
    struct ow_telog_vif_state_sta_link link;
};

struct ow_telog_vif_state
{
    struct osw_hwaddr mac_addr;
    enum osw_vif_type vif_type;
    union
    {
        struct ow_telog_vif_state_ap_vlan ap_vlan;
        struct ow_telog_vif_state_sta sta;
    } u;
};

struct ow_telog_vif
{
    char *vif_name;
    struct ow_telog_vif_conf vif_conf;
    struct ow_telog_vif_state vif_state;
    struct ds_tree_node node;
};

struct ow_telog
{
    struct ds_tree vifs;
    struct osw_state_observer state_obs;
    struct osw_conf_observer conf_obs;
};

#define TELOG_STA_STATE(vif, sta_link, status)                         \
    TELOG_STEP(                                                        \
            "WIFI_LINK",                                               \
            (vif)->vif_name,                                           \
            status,                                                    \
            "multi_ap=%d ssid=" OSW_SSID_FMT " bssid=" OSW_HWADDR_FMT, \
            (sta_link)->multi_ap,                                      \
            OSW_SSID_ARG(&(sta_link)->ssid),                           \
            OSW_HWADDR_ARG(&(sta_link)->bssid));

#define TELOG_STA_ROAM(vif, i, net_conf, n_cconf)                                   \
    TELOG_STEP(                                                                     \
            "WIFI_LINK",                                                            \
            (vif)->vif_name,                                                        \
            "roaming",                                                              \
            "multi_ap=%d ssid=" OSW_SSID_FMT " bssid=" OSW_HWADDR_FMT " cconfs=%d", \
            (net_conf)->multi_aps[i],                                               \
            OSW_SSID_ARG(&(net_conf)->ssids[i]),                                    \
            OSW_HWADDR_ARG(&(net_conf)->bssids[i]),                                 \
            n_cconf);

#define TELOG_WDS_STATE(vif, ap_vlan_sta_addrs, status) \
    TELOG_STEP("WDS_LINK", (vif)->vif_name, status, "sta=%s", (ap_vlan_sta_addrs));

#define ARRDUP(src, dst, memb_ptr, memb_len)                       \
    do                                                             \
    {                                                              \
        (dst)->memb_ptr = NULL;                                    \
        (dst)->memb_len = 0;                                       \
        if ((src)->memb_ptr != NULL && (src)->memb_len > 0)        \
        {                                                          \
            size_t n = sizeof(*(dst)->memb_ptr) * (src)->memb_len; \
            (dst)->memb_ptr = MALLOC(n);                           \
            (dst)->memb_len = (src)->memb_len;                     \
            memcpy((dst)->memb_ptr, (src)->memb_ptr, n);           \
        }                                                          \
    } while (0)

static struct ow_telog_vif *ow_telog_vif_get(struct ow_telog *telog, const char *vif_name)
{
    struct ds_tree *tree = &telog->vifs;
    struct ow_telog_vif *telog_vif = ds_tree_find(tree, vif_name);

    if (telog_vif == NULL)
    {
        telog_vif = CALLOC(1, sizeof(*telog_vif));
        telog_vif->vif_name = STRDUP(vif_name);
        ds_tree_insert(&telog->vifs, telog_vif, vif_name);
    }

    return telog_vif;
}

static void ow_telog_vif_sta_state_update(
        struct ow_telog_vif_state *telog_vif_state,
        const struct osw_drv_vif_state_sta *state_sta)
{
    const struct osw_drv_vif_state_sta_link *sta_link = &state_sta->link;
    struct ow_telog_vif_state_sta_link *telog_link = &telog_vif_state->u.sta.link;

    telog_link->bssid = sta_link->bssid;
    telog_link->ssid = sta_link->ssid;
    telog_link->multi_ap = sta_link->multi_ap;
}

static void ow_telog_vif_ap_vlan_state_update(
        struct ow_telog_vif_state *telog_vif_state,
        const struct osw_drv_vif_state_ap_vlan *osw_ap_vlan_state)
{
    struct ow_telog_vif_state_ap_vlan *dst_ap_vlan = &telog_vif_state->u.ap_vlan;
    ARRDUP(osw_ap_vlan_state, dst_ap_vlan, sta_addrs.list, sta_addrs.count);
}

static void ow_telog_vif_state_update(
        struct ow_telog *telog,
        struct ow_telog_vif *telog_vif,
        const struct osw_state_vif_info *osw_state_vif_info)
{
    struct ow_telog_vif_state *telog_vif_state = &telog_vif->vif_state;
    const struct osw_drv_vif_state *vif_drv_state = osw_state_vif_info->drv_state;

    memcpy(&telog_vif_state->mac_addr, &vif_drv_state->mac_addr, sizeof(vif_drv_state->mac_addr));

    telog_vif_state->vif_type = vif_drv_state->vif_type;

    switch (telog_vif_state->vif_type)
    {
        case OSW_VIF_AP_VLAN:
            ow_telog_vif_ap_vlan_state_update(telog_vif_state, &vif_drv_state->u.ap_vlan);
            break;
        case OSW_VIF_STA:
            ow_telog_vif_sta_state_update(telog_vif_state, &vif_drv_state->u.sta);
            break;
        case OSW_VIF_AP:
        case OSW_VIF_UNDEFINED:
            break;
    }
}

static void ow_telog_ap_vlan_added(struct ow_telog_vif *telog_vif)
{
    struct ow_telog_vif_state_ap_vlan *ap_vlan = &telog_vif->vif_state.u.ap_vlan;
    char ap_vlan_sta_addrs[1024];
    osw_hwaddr_list_to_str(ap_vlan_sta_addrs, sizeof(ap_vlan_sta_addrs), &ap_vlan->sta_addrs);

    TELOG_WDS_STATE(telog_vif, ap_vlan_sta_addrs, "created");
}

static void ow_telog_ap_vlan_removed(struct ow_telog_vif *telog_vif)
{
    struct ow_telog_vif_state_ap_vlan *ap_vlan = &telog_vif->vif_state.u.ap_vlan;
    char ap_vlan_sta_addrs[1024];
    osw_hwaddr_list_to_str(ap_vlan_sta_addrs, sizeof(ap_vlan_sta_addrs), &ap_vlan->sta_addrs);

    TELOG_WDS_STATE(telog_vif, ap_vlan_sta_addrs, "destroyed");
}

static void ow_telog_vif_free(struct ow_telog *telog, char *vif_name)
{
    /* When interface is removed from state, there is no need to keep it
     * in the ow_telog->vifs list.
     * Since conf is based on state, if an interface disappears
     * from state, it should also be removed from conf, so there will
     * be no more subsequent config entries for this interface.
     * Even if the telog_vif was created implicitly on conf_completed,
     * this relation makes it possible to delete it when it's state
     * is removed.
     */
    struct ds_tree *tree = &telog->vifs;
    struct ow_telog_vif *telog_vif = ds_tree_find(tree, vif_name);

    if (WARN_ON(telog_vif == NULL)) return;
    FREE(telog_vif->vif_name);

    ds_tree_remove(&telog->vifs, telog_vif);
    FREE(telog_vif);
}

static void ow_telog_vif_added_cb(struct osw_state_observer *obs, const struct osw_state_vif_info *osw_state_vif_info)
{
    struct ow_telog *telog = container_of(obs, struct ow_telog, state_obs);
    struct ow_telog_vif *telog_vif = ow_telog_vif_get(telog, osw_state_vif_info->vif_name);
    ow_telog_vif_state_update(telog, telog_vif, osw_state_vif_info);

    switch (telog_vif->vif_state.vif_type)
    {
        case OSW_VIF_AP_VLAN:
            ow_telog_ap_vlan_added(telog_vif);
            break;
        case OSW_VIF_AP:
        case OSW_VIF_STA:
        case OSW_VIF_UNDEFINED:
            break;
    }
}

static void ow_telog_vif_removed_cb(struct osw_state_observer *obs, const struct osw_state_vif_info *osw_state_vif_info)
{
    struct ow_telog *telog = container_of(obs, struct ow_telog, state_obs);
    struct ow_telog_vif *telog_vif = ow_telog_vif_get(telog, osw_state_vif_info->vif_name);

    switch (telog_vif->vif_state.vif_type)
    {
        case OSW_VIF_AP_VLAN:
            ow_telog_ap_vlan_removed(telog_vif);
            break;
        case OSW_VIF_STA:
        case OSW_VIF_AP:
        case OSW_VIF_UNDEFINED:
            break;
    }

    ow_telog_vif_free(telog, telog_vif->vif_name);
}

static void ow_telog_sta_vif_connected(struct ow_telog_vif *telog_vif, struct ow_telog_vif_state_sta *telog_sta)
{
    struct ow_telog_vif_state_sta_link *link = &telog_sta->link;
    TELOG_STA_STATE(telog_vif, link, "connected");
}

static void ow_telog_sta_vif_connected_cb(
        struct osw_state_observer *obs,
        const struct osw_state_sta_info *osw_state_sta_info)
{
    struct ow_telog *telog = container_of(obs, struct ow_telog, state_obs);
    const struct osw_state_vif_info *osw_state_vif_info = osw_state_sta_info->vif;
    struct ow_telog_vif *telog_vif = ow_telog_vif_get(telog, osw_state_vif_info->vif_name);

    switch (telog_vif->vif_state.vif_type)
    {
        case OSW_VIF_STA:
            ow_telog_vif_state_update(telog, telog_vif, osw_state_vif_info);
            ow_telog_sta_vif_connected(telog_vif, &telog_vif->vif_state.u.sta);
            break;
        case OSW_VIF_AP:
        case OSW_VIF_AP_VLAN:
        case OSW_VIF_UNDEFINED:
            break;
    }
}

static void ow_telog_clear_bssids(struct ow_telog_vif *telog_vif)
{
    struct ow_telog_vif_conf_sta *telog_conf_sta = &telog_vif->vif_conf.sta;

    memset(&telog_conf_sta->bssids, 0, sizeof(telog_conf_sta->bssids));
    memset(&telog_conf_sta->ssids, 0, sizeof(telog_conf_sta->ssids));
    memset(&telog_conf_sta->multi_aps, 0, sizeof(telog_conf_sta->multi_aps));
    telog_conf_sta->len = 0;
}

static void ow_telog_sta_vif_disconnected(struct ow_telog_vif *telog_vif, struct ow_telog_vif_state_sta *telog_sta)
{
    struct ow_telog_vif_state_sta_link *link = &telog_sta->link;

    TELOG_STA_STATE(telog_vif, link, "disconnected");
}

static void ow_telog_sta_vif_disconnected_cb(
        struct osw_state_observer *obs,
        const struct osw_state_sta_info *osw_state_sta_info)
{
    struct ow_telog *telog = container_of(obs, struct ow_telog, state_obs);
    const struct osw_state_vif_info *osw_state_vif_info = osw_state_sta_info->vif;
    struct ow_telog_vif *telog_vif = ow_telog_vif_get(telog, osw_state_vif_info->vif_name);

    switch (telog_vif->vif_state.vif_type)
    {
        case OSW_VIF_STA:
            ow_telog_vif_state_update(telog, telog_vif, osw_state_vif_info);
            ow_telog_sta_vif_disconnected(telog_vif, &telog_vif->vif_state.u.sta);
            ow_telog_clear_bssids(telog_vif);
            break;
        case OSW_VIF_AP:
        case OSW_VIF_AP_VLAN:
        case OSW_VIF_UNDEFINED:
            break;
    }
}

static void ow_telog_sta_conf_update(
        struct ow_telog_vif_conf_sta *telog_sta,
        struct osw_hwaddr *sta_net_bssids,
        struct osw_ssid *sta_net_ssids,
        bool *multi_aps,
        size_t len)
{
    if (WARN_ON(len > OW_TELOG_SIZE_OF_NETWORKS)) return;

    telog_sta->len = len;

    for (size_t i = 0; i < telog_sta->len; i++)
    {
        memcpy(&telog_sta->bssids[i], &sta_net_bssids[i], sizeof(telog_sta->bssids[i]));
        memcpy(&telog_sta->ssids[i], &sta_net_ssids[i], sizeof(telog_sta->ssids[i]));
        telog_sta->multi_aps[i] = multi_aps[i];
    }
}

static void ow_telog_sta_vif_report_roam(struct ow_telog *telog, struct ow_telog_vif *telog_vif)
{
    struct ow_telog_vif_conf_sta *conf_sta = &telog_vif->vif_conf.sta;
    size_t n_cconf = conf_sta->len;

    for (size_t i = 0; i < n_cconf; i++)
    {
        TELOG_STA_ROAM(telog_vif, i, conf_sta, n_cconf);
    }
}

static enum ow_telog_event ow_telog_cmp_telog_sta_net_bssids(
        struct ow_telog_vif *telog_vif,
        struct osw_hwaddr *sta_net_bssids,
        struct osw_ssid *sta_net_ssids,
        size_t bssid_len)
{
    struct ow_telog_vif_conf_sta *telog_sta = &telog_vif->vif_conf.sta;

    for (size_t i = 0; i < bssid_len; i++)
    {
        if (!osw_hwaddr_is_equal(&telog_sta->bssids[i], &sta_net_bssids[i])) return OW_TELOG_REPORT_ROAM;
    }

    return OW_TELOG_NONE;
}

static size_t ow_telog_osw_conf_sta_nets_ssids_to_ssid_arr(struct osw_ssid *dest_array, struct osw_conf_vif_sta *sta)
{
    struct osw_conf_net *sta_net;
    size_t count = 0;

    ds_dlist_foreach (&sta->net_list, sta_net)
    {
        if (WARN_ON(count >= OW_TELOG_SIZE_OF_NETWORKS)) break;
        memcpy(&dest_array[count], &sta_net->ssid, sizeof(dest_array[count]));
        count++;
    }

    return count;
}

static size_t ow_telog_osw_conf_sta_nets_bssids_to_bssid_arr(
        struct osw_hwaddr *dest_array,
        struct osw_conf_vif_sta *sta)
{
    struct osw_conf_net *sta_net;
    size_t count = 0;

    ds_dlist_foreach (&sta->net_list, sta_net)
    {
        if (WARN_ON(count >= OW_TELOG_SIZE_OF_NETWORKS)) break;
        memcpy(&dest_array[count], &sta_net->bssid, sizeof(dest_array[count]));
        count++;
    }

    return count;
}

static size_t ow_telog_osw_conf_sta_nets_multi_aps_to_multi_ap_arr(bool *dest_array, struct osw_conf_vif_sta *sta)
{
    struct osw_conf_net *sta_net;
    size_t count = 0;

    ds_dlist_foreach (&sta->net_list, sta_net)
    {
        if (WARN_ON(count >= OW_TELOG_SIZE_OF_NETWORKS)) break;
        memcpy(&dest_array[count], &sta_net->multi_ap, sizeof(dest_array[count]));
        count++;
    }

    return count;
}

static size_t ow_telog_osw_conf_sta_to_arr(
        struct osw_hwaddr *bssids_dest_array,
        struct osw_ssid *ssids_dest_array,
        bool *multi_aps_dest_array,
        struct osw_conf_vif_sta *sta)
{
    size_t bssid_len = ow_telog_osw_conf_sta_nets_bssids_to_bssid_arr(bssids_dest_array, sta);
    size_t ssid_len = ow_telog_osw_conf_sta_nets_ssids_to_ssid_arr(ssids_dest_array, sta);
    size_t multi_ap_len = ow_telog_osw_conf_sta_nets_multi_aps_to_multi_ap_arr(multi_aps_dest_array, sta);

    if (bssid_len != ssid_len || multi_ap_len != ssid_len) return OW_TELOG_SIZE_OF_NETWORKS + 1;

    return bssid_len;
}

static void ow_telog_vif_sta_computed(struct osw_conf_vif *vif, struct ow_telog *telog)
{
    const char *vif_name = vif->vif_name;
    struct osw_conf_vif_sta *sta = &vif->u.sta;

    struct ow_telog_vif *telog_vif = ow_telog_vif_get(telog, vif_name);

    struct osw_hwaddr sta_net_bssids[OW_TELOG_SIZE_OF_NETWORKS];
    struct osw_ssid sta_net_ssids[OW_TELOG_SIZE_OF_NETWORKS];
    bool multi_aps[OW_TELOG_SIZE_OF_NETWORKS];

    size_t len = ow_telog_osw_conf_sta_to_arr(sta_net_bssids, sta_net_ssids, multi_aps, sta);
    if (WARN_ON(len > OW_TELOG_SIZE_OF_NETWORKS)) return;

    const enum ow_telog_event telog_event =
            ow_telog_cmp_telog_sta_net_bssids(telog_vif, sta_net_bssids, sta_net_ssids, len);

    switch (telog_event)
    {
        case OW_TELOG_REPORT_ROAM:
            ow_telog_sta_conf_update(&telog_vif->vif_conf.sta, sta_net_bssids, sta_net_ssids, multi_aps, len);
            ow_telog_sta_vif_report_roam(telog, telog_vif);
            break;
        case OW_TELOG_NONE:
            break;
    }
}

static void ow_telog_vif_computed(struct osw_conf_vif *vif, struct ow_telog *telog)
{
    switch (vif->vif_type)
    {
        case OSW_VIF_STA:
            ow_telog_vif_sta_computed(vif, telog);
            break;
        case OSW_VIF_AP:
        case OSW_VIF_AP_VLAN:
        case OSW_VIF_UNDEFINED:
            break;
    }
}

static void ow_telog_computed_cb(struct osw_conf_observer *obs, struct ds_tree *phy_tree)
{
    struct ow_telog *telog = container_of(obs, struct ow_telog, conf_obs);
    struct osw_conf_phy *phy;

    ds_tree_foreach (phy_tree, phy)
    {
        struct osw_conf_vif *vif;
        ds_tree_foreach (&phy->vif_tree, vif)
        {
            ow_telog_vif_computed(vif, telog);
        }
    }
}

static void ow_telog_init(struct ow_telog *telog)
{
    const struct osw_state_observer state_obs = {
        .name = __FILE__,
        .vif_added_fn = ow_telog_vif_added_cb,
        .vif_removed_fn = ow_telog_vif_removed_cb,
        .sta_connected_fn = ow_telog_sta_vif_connected_cb,
        .sta_disconnected_fn = ow_telog_sta_vif_disconnected_cb,
    };

    const struct osw_conf_observer conf_obs = {
        .name = __FILE__,
        .conf_computed_fn = ow_telog_computed_cb,
    };

    ds_tree_init(&telog->vifs, ds_str_cmp, struct ow_telog_vif, node);
    telog->state_obs = state_obs;
    telog->conf_obs = conf_obs;
}

static void ow_telog_attach(struct ow_telog *telog)
{
    osw_state_register_observer(&telog->state_obs);
    osw_conf_register_observer(&telog->conf_obs);
    te_client_init(NULL);
}

static struct ow_telog *ow_telog_alloc(void)
{
    struct ow_telog *m = CALLOC(1, sizeof(*m));
    ow_telog_init(m);
    ow_telog_attach(m);
    TELOG_STEP("MANAGER", "OW", "start", NULL);
    return m;
}

OSW_MODULE(ow_telog)
{
    OSW_MODULE_LOAD(osw_state);
    OSW_MODULE_LOAD(osw_conf);
    return ow_telog_alloc();
}
