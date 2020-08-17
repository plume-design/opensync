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

/*
 * Band Steering Manager - Interface Pair
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <stdarg.h>
#include <linux/types.h>

#include "target.h"
#include "bm.h"

/*****************************************************************************/

#define MODULE_ID LOG_MODULE_ID_MAIN

/*****************************************************************************/

static ovsdb_table_t table_Wifi_VIF_Config;
static ovsdb_table_t table_Wifi_Radio_Config;

static ovsdb_update_monitor_t   bm_group_ovsdb_update;
static ds_tree_t                bm_groups = DS_TREE_INIT((ds_key_cmp_t *)strcmp,
                                                         bm_group_t,
                                                         dst_node);

static c_item_t map_debug_levels[] = {
    C_ITEM_VAL(0,       BM_LOG_LEVEL_0),
    C_ITEM_VAL(1,       BM_LOG_LEVEL_1),
    C_ITEM_VAL(2,       BM_LOG_LEVEL_2)
};

/*****************************************************************************/

static bool
bm_ifconfig_from_ovsdb(const char *ifname,
                       radio_type_t radio_type,
                       bool bs_allowed,
                       struct schema_Band_Steering_Config *bsconf,
                       bm_group_t *group)
{
    bm_ifconfig_t *ifcfg;

    if (group->ifcfg_num >= ARRAY_SIZE(group->ifcfg)) {
        LOGW("%s: %s ifcfg_num exceed max value", __func__, ifname);
        return false;
    }

    ifcfg = &group->ifcfg[group->ifcfg_num];
    STRSCPY(ifcfg->ifname, ifname);
    ifcfg->radio_type = radio_type;
    ifcfg->bs_allowed = bs_allowed;
    group->ifcfg_num++;
    LOGN("Added %s with radio_type %d and bs_allowed %d", ifcfg->ifname,
         ifcfg->radio_type, ifcfg->bs_allowed);

    STRSCPY(ifcfg->bsal.ifname, ifname);
    ifcfg->bsal.chan_util_check_sec = bsconf->chan_util_check_sec;
    ifcfg->bsal.chan_util_avg_count = bsconf->chan_util_avg_count;
    ifcfg->bsal.inact_check_sec = bsconf->inact_check_sec;
    ifcfg->bsal.inact_tmout_sec_normal = bsconf->inact_tmout_sec_normal;
    ifcfg->bsal.inact_tmout_sec_overload = bsconf->inact_tmout_sec_overload;
    ifcfg->bsal.def_rssi_inact_xing = bsconf->def_rssi_inact_xing;
    ifcfg->bsal.def_rssi_low_xing = bsconf->def_rssi_low_xing;
    ifcfg->bsal.def_rssi_xing = bsconf->def_rssi_xing;
    ifcfg->bsal.debug.raw_chan_util = false;
    ifcfg->bsal.debug.raw_rssi = false;

    /* active/inactive detection timeout */
    group->inact_check_sec = bsconf->inact_check_sec;
    group->inact_tmout_sec_normal = bsconf->inact_tmout_sec_normal;

    bm_main_set_timer(group->inact_check_sec);

    return true;
}

static bool
bm_ifconfigs_from_ovsdb(struct schema_Band_Steering_Config *bsconf, bm_group_t *group)
{
    char *target_name;
    radio_type_t radio_type;
    bool bs_allowed;
    int i;

    for (i = 0; i < bsconf->ifnames_len; i++) {
        target_name = target_map_ifname(bsconf->ifnames_keys[i]);
        if (!target_name) {
            LOGE("Failed to map %s ifname", bsconf->ifnames_keys[i]);
            return false;
        }

        radio_type = radio_get_type_from_str(bsconf->ifnames[i]);
        if (radio_type == RADIO_TYPE_NONE) {
            LOGE("Failed to get radio type for %s from '%s'",
                 bsconf->ifnames_keys[i], bsconf->ifnames[i]);
            return false;
        }

        if (strstr(bsconf->ifnames[i], "bs_allow")) {
            bs_allowed = true;
        } else {
            bs_allowed = false;
        }

        if (!bm_ifconfig_from_ovsdb(bsconf->ifnames_keys[i], radio_type, bs_allowed, bsconf, group)) {
            return false;
        }
    }

    return true;
}

static bool
bm_ifconfigs_from_ovsdb_old(struct schema_Band_Steering_Config *bsconf, bm_group_t *group)
{
    struct schema_Wifi_Radio_Config rconf;
    struct schema_Wifi_VIF_Config vconf;
    radio_type_t radio_type;
    const char *column;
    json_t *where;
    int ret;

    column = SCHEMA_COLUMN(Wifi_VIF_Config, if_name);
    where = ovsdb_tran_cond(OCLM_STR, column, OFUNC_EQ, bsconf->if_name_5g);
    if (!where) {
        LOGW("%s: Failed alloc ovsdb cond (5G VAP in Wifi_VIF_Config)",
             bsconf->if_name_5g);
        return false;
    }

    ret = ovsdb_table_select_one_where(&table_Wifi_VIF_Config, where, &vconf);
    if (!ret) {
        LOGW("%s: 5G VAP not found in in Wifi_VIF_Config", bsconf->if_name_5g);
        return false;
    }

    column = SCHEMA_COLUMN(Wifi_Radio_Config, vif_configs);
    where = ovsdb_tran_cond(OCLM_UUID, column, OFUNC_INC, vconf._uuid.uuid);
    if (!where) {
        LOGW("%s: Failed alloc ovsdb cond (5G VAP's UUID in Wifi_Radio_Config)",
             bsconf->if_name_5g);
        return false;
    }

    ret = ovsdb_table_select_one_where(&table_Wifi_Radio_Config, where, &rconf);
    if (!ret) {
        LOGW("%s: 5G VAP's UUID not found in in table_Wifi_Radio_Config", bsconf->if_name_5g);
        return false;
    }

    /* This could be handled on the Cloud */
    char *target_name_2g = target_map_ifname(bsconf->if_name_2g);
    if (!target_name_2g) {
        LOGE("Failed to map 2.4G ifname %s)", bsconf->if_name_2g);
        return false;
    }
    char *target_name_5g = target_map_ifname(bsconf->if_name_5g);
    if (!target_name_5g) {
        LOGE("Failed to map 5G ifname %s)", bsconf->if_name_5g);
        return false;
    }

    /* bs_allowed - false */
    if (!bm_ifconfig_from_ovsdb(target_name_2g, RADIO_TYPE_2G, false, bsconf, group)) {
        return false;
    }

    /*
     * Previously this code looked for "l50"/"u50" in ifname we used to distinguish
     * between 5GL and 5GU. This was SP-specific.
     * We will not need this when cloud config ::ifnames
     */
    if (strcmp(rconf.freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_5GU) == 0)
        radio_type = RADIO_TYPE_5GU;
    else if (strcmp(rconf.freq_band, SCHEMA_CONSTS_RADIO_TYPE_STR_5GL) == 0)
        radio_type = RADIO_TYPE_5GL;
    else
        radio_type = RADIO_TYPE_5G;

    /* bs_allowed - true */
    if (!bm_ifconfig_from_ovsdb(target_name_5g, radio_type, true, bsconf, group)) {
        return false;
    }

    return true;
}

static bool
bm_group_from_ovsdb(struct schema_Band_Steering_Config *bsconf, bm_group_t *group)
{
    uint32_t    log_severity;

    memset(&group->ifcfg, 0, sizeof(group->ifcfg));
    group->ifcfg_num = 0;

    /* setup group->ifcfg[] */
    if (bsconf->ifnames_len) {
        if (!bm_ifconfigs_from_ovsdb(bsconf, group))
            return false;
    } else {
        if (!bm_ifconfigs_from_ovsdb_old(bsconf, group))
            return false;
    }

    // Channel Utilization water marks (thresholds)
    group->chan_util_hwm         = bsconf->chan_util_hwm;
    group->chan_util_lwm         = bsconf->chan_util_lwm;
    group->stats_report_interval = bsconf->stats_report_interval;

    // Kick debouce threshold
    if (bsconf->kick_debounce_thresh > 0) {
        group->kick_debounce_thresh = bsconf->kick_debounce_thresh;
    }
    else {
        group->kick_debounce_thresh = BM_DEF_KICK_DEBOUNCE_THRESH;
    }

    // Kick debouce period
    if (bsconf->kick_debounce_period > 0) {
        group->kick_debounce_period = bsconf->kick_debounce_period;
    }
    else {
        group->kick_debounce_period = BM_DEF_KICK_DEBOUNCE_PERIOD;
    }

    // Success threshold
    if (bsconf->success_threshold_secs > 0) {
        group->success_threshold = bsconf->success_threshold_secs;
    }
    else {
        group->success_threshold = BM_DEF_SUCCESS_TMOUT_SECS;
    }

    // This could be added to LOG level OVSDB table for all managers!
    if (bsconf->debug_level) {
        if (bsconf->debug_level >= ARRAY_LEN(map_debug_levels)) {
            group->debug_level = ARRAY_LEN(map_debug_levels) - 1;
        }
        else {
            group->debug_level = bsconf->debug_level;
        }

        if (c_get_value_by_key(map_debug_levels, group->debug_level, &log_severity)) {
            log_severity_set(log_severity);
        }
    }

    group->gw_only = bsconf->gw_only;

    return true;
}

static void
bm_group_ovsdb_update_cb(ovsdb_update_monitor_t *self)
{
    struct schema_Band_Steering_Config      bsconf;
    pjs_errmsg_t                            perr;
    bm_group_t                              *group;
    unsigned int                            i;

    switch(self->mon_type) {

    case OVSDB_UPDATE_NEW:
        if (!schema_Band_Steering_Config_from_json(&bsconf,
                                                    self->mon_json_new, false, perr)) {
            LOGE("Failed to parse new Band_Steering_Config row: %s", perr);
            return;
        }

        group = calloc(1, sizeof(*group));
        STRSCPY(group->uuid, bsconf._uuid.uuid);

        if (!bm_group_from_ovsdb(&bsconf, group)) {
            LOGE("Failed to convert row to if-config (uuid=%s)", group->uuid);
            free(group);
            return;
        }

        /*
         * XXX: maps radio type (2G, 5G, 5GL, 5GU) through target API using interface
         * names from last pair added.  This is to be replaced in the future when BM
         * has proper support for tri-radio platforms.
         */
        for (i = 0; i < group->ifcfg_num; i++) {
            if (target_bsal_iface_add(&group->ifcfg[i].bsal)) {
                LOGE("Failed to add iface %s (band %s) to BSAL (uuid=%s)",
                     group->ifcfg[i].bsal.ifname,
                     radio_get_name_from_type(group->ifcfg[i].radio_type),
                     group->uuid);
                free(group);
                return;
	    }

            if (!bm_neighbor_get_self_neighbor(group->ifcfg[i].bsal.ifname, &group->ifcfg[i].self_neigh))
                LOGW("Failed to get self neighbor %s", group->ifcfg[i].bsal.ifname);

            LOGN("Added %s ifname (uuid=%s)", group->ifcfg[i].ifname, group->uuid);
        }

        group->enabled = true;
        ds_tree_insert(&bm_groups, group, group->uuid);

        if (!bm_client_add_all_to_group(group)) {
            LOGW("Failed to add one or more clients to if-group");
        }

        bm_neighbor_set_all_to_group(group);
        break;

    case OVSDB_UPDATE_MODIFY:
        if (!(group = bm_group_find_by_uuid((char *)self->mon_uuid))) {
            LOGE("Unable to find if-group for modify with UUID %s", self->mon_uuid);
            return;
        }

        if (!schema_Band_Steering_Config_from_json(&bsconf,
                                                    self->mon_json_new, true, perr)) {
            LOGE("Failed to parse modified Band_Steering_Config row: %s", perr);
            return;
        }

        if (!bm_group_from_ovsdb(&bsconf, group)) {
            LOGE("Failed to convert row to if-config for modify (uuid=%s)", group->uuid);
            return;
        }

        for (i = 0; i < group->ifcfg_num; i++) {
            if (target_bsal_iface_update(&group->ifcfg[i].bsal) != 0) {
                LOGE("Failed to update iface %s (band %s) BSAL config (uuid=%s)",
                     group->ifcfg[i].bsal.ifname,
                     radio_get_name_from_type(group->ifcfg[i].radio_type),
                     group->uuid);
                return;
            }

            if (!bm_neighbor_get_self_neighbor(group->ifcfg[i].bsal.ifname, &group->ifcfg[i].self_neigh))
                LOGW("Failed to get self neighbor %s", group->ifcfg[i].bsal.ifname);

            LOGN("Updated %s ifname (uuid=%s)", group->ifcfg[i].ifname, group->uuid);
        }

        if (!bm_client_update_all_from_group(group)) {
            LOGW("Failed to update one or more clients from if-group");
        }

        bm_neighbor_set_all_to_group(group);
        LOGN("Updated if-group (uuid=%s)", group->uuid);
        break;

    case OVSDB_UPDATE_DEL:
        if (!(group = bm_group_find_by_uuid((char *)self->mon_uuid))) {
            LOGE("Unable to find if-group for delete with UUID %s", self->mon_uuid);
            return;
        }

        if (!bm_client_remove_all_from_group(group)) {
            LOGW("Failed to remove one or more clients from if-group");
        }

        bm_neighbor_remove_all_from_group(group);
        bm_kick_cleanup_by_group(group);

        for (i = 0;  i < group->ifcfg_num; i++) {
            if (target_bsal_iface_remove(&group->ifcfg[i].bsal) != 0) {
                LOGE("Failed to remove iface %s (band %s) (uuid=%s)",
                     group->ifcfg[i].bsal.ifname,
                     radio_get_name_from_type(group->ifcfg[i].radio_type),
                     group->uuid);
            }

            LOGN("Removed %s ifname (uuid=%s)", group->ifcfg[i].ifname, group->uuid);
        }

        LOGN("Removed if-group (uuid=%s)", group->uuid);

        ds_tree_remove(&bm_groups, group);
        free(group);
        break;

    default:
        break;

    }
}

/*****************************************************************************/
bool
bm_group_init(void)
{
    LOGI("Interface Pair Initializing");

    // Start OVSDB monitoring
    if (!ovsdb_update_monitor(&bm_group_ovsdb_update,
                              bm_group_ovsdb_update_cb,
                              SCHEMA_TABLE(Band_Steering_Config),
                              OMT_ALL)) {
        LOGE("Failed to monitor OVSDB table '%s'", SCHEMA_TABLE(Band_Steering_Config));
        return false;
    }

    OVSDB_TABLE_INIT(Wifi_VIF_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Radio_Config, if_name);

    return true;
}

bool
bm_group_cleanup(void)
{
    ds_tree_iter_t  iter;
    bm_group_t      *group;
    unsigned int    i;

    LOGI("Interface Pair cleaning up");

    group = ds_tree_ifirst(&iter, &bm_groups);
    while(group) {
        ds_tree_iremove(&iter);

        if (group->enabled) {
            for (i = 0; i < group->ifcfg_num; i++) {
                if (target_bsal_iface_remove(&group->ifcfg[i].bsal) != 0) {
                    LOGW("Failed to remove ifgroup from BSAL");
                }
            }
        }
        free(group);

        group = ds_tree_inext(&iter);
    }

    return true;
}

ds_tree_t *
bm_group_get_tree(void)
{
    return &bm_groups;
}

bm_group_t *
bm_group_find_by_uuid(char *uuid)
{
    return (bm_group_t *)ds_tree_find(&bm_groups, uuid);
}

bm_group_t *
bm_group_find_by_ifname(const char *ifname)
{
    bm_group_t      *group;
    unsigned int    i;

    ds_tree_foreach(&bm_groups, group) {
        for(i = 0; i < group->ifcfg_num; i++) {
            if (!strcmp(group->ifcfg[i].ifname, ifname)) {
                return group;
            }
        }
    }

    return NULL;
}

radio_type_t
bm_group_find_radio_type_by_ifname(const char *ifname)
{
    bm_group_t      *group;
    unsigned int    i;

    ds_tree_foreach(&bm_groups, group) {
        for(i = 0; i < group->ifcfg_num; i++) {
            if (!strcmp(group->ifcfg[i].ifname, ifname)) {
                return group->ifcfg[i].radio_type;
            }
        }
    }

    return RADIO_TYPE_NONE;
}

bool
bm_group_radio_type_allowed(bm_group_t *group, radio_type_t radio_type)
{
    unsigned int i;

    if (WARN_ON(!group))
        return false;

    for (i = 0; i < group->ifcfg_num; i++) {
        if (group->ifcfg[i].radio_type == radio_type)
            return group->ifcfg[i].bs_allowed;
    }

    WARN_ON(1);
    return false;
}

bool
bm_group_only_dfs_channels(bm_group_t *group)
{
    unsigned int i;
    bool only_dfs = true;

    if (WARN_ON(!group))
        return false;

    /* Check all bs_allowed ifaces if DFS channel used */
    for (i = 0; i < group->ifcfg_num; i++) {
        if (!group->ifcfg[i].bs_allowed)
            continue;
        if (bm_client_is_dfs_channel(group->ifcfg[i].self_neigh.channel)) {
            continue;
        }

        only_dfs = false;
        break;
    }

    return only_dfs;
}
