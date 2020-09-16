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
 * Band Steering Manager - Clients
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
#include <math.h>

#include "bm.h"
#include "util.h"


/*****************************************************************************/
#define MODULE_ID LOG_MODULE_ID_CLIENT

/*****************************************************************************/
static ovsdb_update_monitor_t   bm_client_ovsdb_mon;
static ds_tree_t                bm_clients = DS_TREE_INIT((ds_key_cmp_t *)strcmp,
                                                          bm_client_t,
                                                          dst_node);

static c_item_t map_bsal_bands[] = {
    C_ITEM_STR(RADIO_TYPE_NONE,                      "none"),
    C_ITEM_STR(RADIO_TYPE_2G,                        "2.4G"),
    C_ITEM_STR(RADIO_TYPE_5G,                        "5G"),
    C_ITEM_STR(RADIO_TYPE_5GL,                       "5GL"),
    C_ITEM_STR(RADIO_TYPE_5GU,                       "5GU")
};

static c_item_t map_state_names[] = {
    C_ITEM_STR(BM_CLIENT_STATE_DISCONNECTED,    "DISCONNECTED"),
    C_ITEM_STR(BM_CLIENT_STATE_CONNECTED,       "CONNECTED"),
    C_ITEM_STR(BM_CLIENT_STATE_STEERING,        "STEERING"),
    C_ITEM_STR(BM_CLIENT_STATE_BACKOFF,         "BACKOFF")
};

static c_item_t map_ovsdb_reject_detection[] = {
    C_ITEM_STR(BM_CLIENT_REJECT_NONE,           "none"),
    C_ITEM_STR(BM_CLIENT_REJECT_PROBE_ALL,      "probe_all"),
    C_ITEM_STR(BM_CLIENT_REJECT_PROBE_NULL,     "probe_null"),
    C_ITEM_STR(BM_CLIENT_REJECT_PROBE_DIRECT,   "probe_direct"),
    C_ITEM_STR(BM_CLIENT_REJECT_AUTH_BLOCKED,   "auth_block")
};

static c_item_t map_ovsdb_kick_type[] = {
    C_ITEM_STR(BM_CLIENT_KICK_NONE,             "none"),
    C_ITEM_STR(BM_CLIENT_KICK_DISASSOC,         "disassoc"),
    C_ITEM_STR(BM_CLIENT_KICK_DEAUTH,           "deauth"),
    C_ITEM_STR(BM_CLIENT_KICK_BSS_TM_REQ,       "bss_tm_req"),
    C_ITEM_STR(BM_CLIENT_KICK_RRM_BR_REQ,       "rrm_br_req"),
    C_ITEM_STR(BM_CLIENT_KICK_BTM_DISASSOC,     "btm_disassoc"),
    C_ITEM_STR(BM_CLIENT_KICK_BTM_DEAUTH,       "btm_deauth"),
    C_ITEM_STR(BM_CLIENT_KICK_RRM_DISASSOC,     "rrm_disassoc"),
    C_ITEM_STR(BM_CLIENT_KICK_RRM_DEAUTH,       "rrm_deauth")
};

static c_item_t map_cs_modes[] = {
    C_ITEM_STR(BM_CLIENT_CS_MODE_OFF,           "off"),
    C_ITEM_STR(BM_CLIENT_CS_MODE_HOME,          "home"),
    C_ITEM_STR(BM_CLIENT_CS_MODE_AWAY,          "away")
};

static c_item_t map_cs_states[] = {
    C_ITEM_STR(BM_CLIENT_CS_STATE_NONE,             "none"),
    C_ITEM_STR(BM_CLIENT_CS_STATE_STEERING,         "steering"),
    C_ITEM_STR(BM_CLIENT_CS_STATE_EXPIRED,          "expired"),
    C_ITEM_STR(BM_CLIENT_CS_STATE_FAILED,           "failed"),
    C_ITEM_STR(BM_CLIENT_CS_STATE_XING_LOW,         "xing_low"),
    C_ITEM_STR(BM_CLIENT_CS_STATE_XING_HIGH,        "xing_high"),
    C_ITEM_STR(BM_CLIENT_CS_STATE_XING_DISABLED,    "xing_disabled")
};

static c_item_t map_ovsdb_pref_allowed[] = {
    C_ITEM_STR(BM_CLIENT_PREF_ALLOWED_NEVER,              "never" ),
    C_ITEM_STR(BM_CLIENT_PREF_ALLOWED_HWM,                "hwm"   ),
    C_ITEM_STR(BM_CLIENT_PREF_ALLOWED_ALWAYS,             "always"),
    C_ITEM_STR(BM_CLIENT_PREF_ALLOWED_NON_DFS,            "nonDFS")
};

static c_item_t map_ovsdb_force_kick[] = {
    C_ITEM_STR(BM_CLIENT_FORCE_KICK_NONE,       "none"),
    C_ITEM_STR(BM_CLIENT_SPECULATIVE_KICK,      "speculative"),
    C_ITEM_STR(BM_CLIENT_DIRECTED_KICK,         "directed"),
    C_ITEM_STR(BM_CLIENT_GHOST_DEVICE_KICK,     "ghost_device")
};

/*****************************************************************************/
static bool     bm_client_to_bsal_conf_bs(bm_client_t *client,
                                          bm_group_t *group,
                                          radio_type_t band,
                                          bsal_client_config_t *dest);
static bool     bm_client_add_to_group(bm_client_t *client, bm_group_t *group);
static bool     bm_client_update_group(bm_client_t *client, bm_group_t *group);
static bool     bm_client_remove_from_group(bm_client_t *client, bm_group_t *group);
static bool     bm_client_add_to_all_groups(bm_client_t *client);
static bool     bm_client_update_all_groups(bm_client_t *client);
static bool     bm_client_remove_from_all_groups(bm_client_t *client);
static bool     bm_client_from_ovsdb(struct schema_Band_Steering_Clients *bscli,
                                                                 bm_client_t *client);
static void     bm_client_remove(bm_client_t *client);
static void     bm_client_ovsdb_update_cb(ovsdb_update_monitor_t *self);
static void     bm_client_backoff(bm_client_t *client, bool enable);
static void     bm_client_disable_steering(bm_client_t *client);
static void     bm_client_task_backoff(void *arg);
static void     bm_client_state_change(bm_client_t *client,
                                                 bm_client_state_t state, bool force);
static void     bm_client_activity_action(bm_client_t *client, bool is_active);


/*****************************************************************************/

static bool
bm_client_to_cs_bsal_conf( bm_client_t *client, bsal_client_config_t *dest, bool block )
{
    dest->blacklist             = false;
    dest->rssi_inact_xing       = 0;
    dest->auth_reject_reason    = client->cs_auth_reject_reason;

    if( client->cs_mode == BM_CLIENT_CS_MODE_AWAY ) {
        if( client->cs_probe_block ) {
            dest->rssi_probe_hwm    = BM_CLIENT_MIN_HWM;
            dest->rssi_probe_lwm    = BM_CLIENT_MAX_LWM;
        } else {
            dest->rssi_probe_hwm    = 0;
            dest->rssi_probe_lwm    = 0;
        }

        if( client->cs_auth_block ) {
            dest->rssi_auth_hwm     = BM_CLIENT_MIN_HWM;
            dest->rssi_auth_lwm     = BM_CLIENT_MAX_LWM;
        } else {
            dest->rssi_auth_hwm     = 0;
            dest->rssi_auth_lwm     = 0;
        }

        dest->rssi_high_xing        = 0;
        dest->rssi_low_xing         = 0;
    } else {
        if( block ) {
            dest->rssi_probe_hwm    = BM_CLIENT_MIN_HWM;
            dest->rssi_probe_lwm    = BM_CLIENT_MAX_LWM;

            dest->rssi_auth_hwm     = BM_CLIENT_MIN_HWM;
            dest->rssi_auth_lwm     = BM_CLIENT_MAX_LWM;

            dest->rssi_high_xing    = 0;
            dest->rssi_low_xing     = 0;
        } else {
            dest->rssi_probe_hwm    = 0;
            dest->rssi_probe_lwm    = 0;

            dest->rssi_auth_hwm     = 0;
            dest->rssi_auth_lwm     = 0;

            dest->rssi_high_xing    = client->cs_hwm;
            dest->rssi_low_xing     = client->cs_lwm;
        }
    }

    LOGD("cs %s block %d (probe %d-%d, auth %d-%d, xing %d-%d", client->mac_addr, block,
         dest->rssi_probe_lwm, dest->rssi_probe_hwm,
	 dest->rssi_auth_lwm, dest->rssi_auth_hwm,
	 dest->rssi_low_xing, dest->rssi_high_xing);

    return true;
}

static bool
bm_client_to_bsal_conf_bs(bm_client_t *client, bm_group_t *group, radio_type_t radio_type, bsal_client_config_t *dest)
{
    if( client->lwm == BM_KICK_MAGIC_NUMBER ) {
        dest->rssi_low_xing = 0;
    } else {
        dest->rssi_low_xing = client->lwm;
    }

    if (!bm_group_radio_type_allowed(group, radio_type) && client->state != BM_CLIENT_STATE_BACKOFF) {
        /* Block client based on HWM */
        dest->blacklist             = false;

        if( client->pref_allowed == BM_CLIENT_PREF_ALLOWED_ALWAYS ) {
            dest->rssi_probe_hwm    = BM_CLIENT_MIN_HWM;
        } else if( client->pref_allowed == BM_CLIENT_PREF_ALLOWED_HWM ) {
            dest->rssi_probe_hwm    = client->hwm;
        } else if (client->pref_allowed == BM_CLIENT_PREF_ALLOWED_NON_DFS) {
            if (bm_group_only_dfs_channels(group)) {
                dest->rssi_probe_hwm = 0;
            } else {
                dest->rssi_probe_hwm = BM_CLIENT_MIN_HWM;
            }
        } else {
            dest->rssi_probe_hwm    = 0;
        }
        LOGT( "Client '%s': Setting hwm to '%hhu'",
                                client->mac_addr, dest->rssi_probe_hwm );

        dest->rssi_probe_lwm        = client->lwm;
        dest->rssi_high_xing        = client->hwm;
        dest->rssi_inact_xing       = 0;

        if (client->pref_allowed == BM_CLIENT_PREF_ALLOWED_NON_DFS &&
            bm_group_only_dfs_channels(group)) {
            dest->rssi_probe_lwm = 0;
            dest->rssi_high_xing = 0;
        }

        if (client->pref_allowed == BM_CLIENT_PREF_ALLOWED_HWM)
            dest->rssi_probe_lwm = 0;

        if (group->gw_only)
            dest->rssi_low_xing = 0;

        if( client->pre_assoc_auth_block ) {
            LOGT( "Client '%s': Blocking auth requests for"
                  " pre-assocation band steering", client->mac_addr );
            // This value should always mirror dest->rssi_probe_hwm
            dest->rssi_auth_hwm     = dest->rssi_probe_hwm;
        } else {
            dest->rssi_auth_hwm     = 0;
        }

        dest->rssi_auth_lwm         = 0;
        dest->auth_reject_reason    = 0;
    }
    else {
        /* Don't block client */
        dest->blacklist             = false;
        dest->rssi_probe_hwm        = 0;
        dest->rssi_probe_lwm        = 0;
        dest->rssi_high_xing        = 0;
        dest->rssi_inact_xing       = 0;

        dest->rssi_auth_hwm         = 0;
        dest->rssi_auth_lwm         = 0;
        dest->auth_reject_reason    = 0;

        if (client->state == BM_CLIENT_STATE_BACKOFF &&  client->steer_during_backoff) {
           LOGD("bs %s radio_type %d steer during backoff", client->mac_addr, radio_type);
           dest->rssi_high_xing = client->hwm;
        }
    }

    LOGD("bs %s radio_type %d (probe %d-%d, auth %d-%d, xing %d-%d", client->mac_addr, radio_type,
         dest->rssi_probe_lwm, dest->rssi_probe_hwm,
	 dest->rssi_auth_lwm, dest->rssi_auth_hwm,
	 dest->rssi_low_xing, dest->rssi_high_xing);

    return true;
}

static bool
bm_client_to_bsal_conf_cs(bm_client_t *client, radio_type_t radio_type, bsal_client_config_t *dest)
{
    bool block = false;

    switch (client->cs_mode) {
        case BM_CLIENT_CS_MODE_HOME:
            if (client->cs_radio_type != radio_type)
                block = true;
            break;
        case BM_CLIENT_CS_MODE_AWAY:
            block = true;
            break;
        default:
            LOGW("%s unknown cs_mode: %d", __func__, client->cs_mode);
            break;
    }

    LOGD("CS Client '%s': cs_radio_type %s radio_type %s - %s", client->mac_addr,
         c_get_str_by_key(map_bsal_bands, client->cs_radio_type),
         c_get_str_by_key(map_bsal_bands, radio_type),
         block ? "block" : "pass");

    if (!bm_client_to_cs_bsal_conf(client, dest, block)) {
        LOGE("Failed to convert client '%s' to blocked BSAL config", client->mac_addr);
        return false;
    }

    return true;
}

static bool
bm_client_to_bsal_conf(bm_client_t *client, bm_group_t *group, radio_type_t radio_type, bsal_client_config_t *dest)
{
    bool status = false;

    switch (client->steering_state) {
        case BM_CLIENT_CLIENT_STEERING:
            LOGT("Client '%s' radio_type %s: Applying Client Steering BSAL configuration",
                 client->mac_addr, c_get_str_by_key(map_bsal_bands, radio_type));
            status = bm_client_to_bsal_conf_cs(client, radio_type, dest);
            break;
        case BM_CLIENT_STEERING_NONE:
        case BM_CLIENT_BAND_STEERING:
            LOGT("Client '%s' radio_type %s: Applying Band Steering BSAL configuration",
                 client->mac_addr, c_get_str_by_key(map_bsal_bands, radio_type));
            status = bm_client_to_bsal_conf_bs(client, group, radio_type, dest);
            break;
        default:
            LOGW("%s unknown steering_state %d", __func__, client->steering_state);
            break;
    }

    return status;
}

bm_rrm_req_t *
bm_client_get_rrm_req(bm_client_t *client, uint8_t channel)
{
    bm_rrm_req_t *req;
    unsigned int i;

    req = NULL;

    for (i = 0; i < ARRAY_SIZE(client->rrm_req); i++) {
        if (client->rrm_req[i].rrm_params.channel == channel) {
            req = &client->rrm_req[i];
            req->client = client;
            break;
        }
    }

    if (req) {
        LOGD("%s found queued rrm task %i for channel %u, cancel it", client->mac_addr, i, channel);
        evsched_task_cancel(req->rrm_task);
        return req;
    }

    for (i = 0; i < ARRAY_SIZE(client->rrm_req); i++) {
        if (!client->rrm_req[i].rrm_task) {
            req = &client->rrm_req[i];
            req->client = client;
            break;
        }
    }

    if (req)
        LOGD("%s using %u/%zu rrm request", client->mac_addr, i, ARRAY_SIZE(client->rrm_req));

    return req;
}

void
bm_client_put_rrm_req(bm_rrm_req_t *req)
{
    memset(req, 0, sizeof(*req));
}

static void
bm_client_send_rrm_req_task(void *arg)
{
    bm_rrm_req_t *req = arg;
    bm_client_t *client = req->client;

    LOGI("%s %s issue rrm for channel %d", client->ifname, client->mac_addr, req->rrm_params.channel);
    if (target_bsal_rrm_beacon_report_request(client->ifname, client->macaddr.addr, &req->rrm_params) < 0)
        LOGE("RRM Beacon Report request failed for client %s", client->mac_addr);

    bm_client_put_rrm_req(req);
}

void
bm_client_send_rrm_req(bm_client_t *client, bm_client_rrm_req_type_t rrm_req_type, int delay)
{
    bm_rrm_req_t *req;
    uint8_t channels[8];
    int num_channels;
    uint32_t delay_ms;
    int i;

    if (!client->connected)
        return;
    if (!strlen(client->ifname))
        return;
    if (!client->group)
        return;
    if (!client->info->is_RRM_supported)
        return;

    bm_client_reset_rrm_neighbors(client);
    num_channels = bm_neighbor_get_channels(client, rrm_req_type, channels, sizeof(channels), 0);

    for (i = 0; i < num_channels; i++) {
        if (bm_client_is_dfs_channel(channels[i])) {
            if (!client->info->rrm_caps.bcn_rpt_passive) {
                LOGD("%s skip rrm_req while DFS and !rrm_passive", client->mac_addr);
                continue;
            }
        } else {
            if (!client->info->rrm_caps.bcn_rpt_active) {
                LOGD("%s skip rrm_req while non-DFS and !rrm_active", client->mac_addr);
                continue;
            }
        }

        req = bm_client_get_rrm_req(client, channels[i]);
        if (!req) {
            LOGW("%s: could not get rrm_req slot", client->mac_addr);
            break;
        }

        if (!bm_kick_get_rrm_params(&req->rrm_params)) {
            LOGW("Client %s: Failed to get RRM params", client->mac_addr);
            break;
        }

        /* Check only current SSID */
        req->rrm_params.req_ssid = 1;
        req->rrm_params.meas_dur = BM_CLIENT_RRM_ACTIVE_MEASUREMENT_DURATION;

        req->rrm_params.channel = channels[i];
        req->rrm_params.op_class = bm_neighbor_get_op_class(channels[i]);

        if (bm_client_is_dfs_channel(channels[i])) {
            req->rrm_params.meas_mode = 0;
            req->rrm_params.meas_dur = BM_CLIENT_RRM_PASIVE_MEASUREMENT_DURATION;
        }

        delay_ms = EVSCHED_SEC(delay) + i * 3 * req->rrm_params.meas_dur;

        if (!delay_ms) {
            bm_client_send_rrm_req_task(req);
            continue;
        }

        req->rrm_task = evsched_task(bm_client_send_rrm_req_task, req, delay_ms);
    }
}

void
bm_client_update_rrm_neighbors(void)
{
    bm_client_t *client;
    int delay = 3;

    ds_tree_foreach(&bm_clients, client) {
        if (!client->send_rrm_after_assoc)
            continue;
        if (!client->connected)
            continue;
        if (!client->info->is_RRM_supported)
            continue;
        if (!strlen(client->ifname))
            continue;
        if (!client->group)
            continue;
        bm_client_send_rrm_req(client, BM_CLIENT_RRM_OWN_BAND_ONLY, delay);
        delay += 3;
    }
}

static void
bm_client_print_client_caps( bm_client_t *client )
{
    if (WARN_ON(!client->info))
        return;

    LOGD( " ~~~ Client '%s' ~~~", client->mac_addr );
    LOGD( " isBTMSupported        : %s", client->info->is_BTM_supported ? "Yes":"No" );
    LOGD( " isRRMSupported        : %s", client->info->is_RRM_supported ? "Yes":"No" );
    LOGD( " Supports 2G           : %s", client->info->band_cap_2G ? "Yes":"No" );
    LOGD( " Supports 5G           : %s", client->info->band_cap_5G ? "Yes":"No" );

    LOGD( "   ~~~Datarate Information~~~    " );
    LOGD( " Max Channel Width     : %hhu", client->info->datarate_info.max_chwidth );
    LOGD( " Max Streams           : %hhu", client->info->datarate_info.max_streams );
    LOGD( " PHY Mode              : %hhu", client->info->datarate_info.phy_mode );
    LOGD( " Max MCS               : %hhu", client->info->datarate_info.max_MCS );
    LOGD( " Max TX power          : %hhu", client->info->datarate_info.max_txpower );
    LOGD( " Is Static SMPS?       : %s", client->info->datarate_info.is_static_smps ? "Yes":"No" );
    LOGD( " Suports MU-MIMO       : %s", client->info->datarate_info.is_mu_mimo_supported? "Yes":"No" );

    LOGD( "   ~~~RRM Capabilites~~~     " );
    LOGD( " Link measurement      : %s", client->info->rrm_caps.link_meas ? "Yes":"No" );
    LOGD( " Neighbor report       : %s", client->info->rrm_caps.neigh_rpt ? "Yes":"No" );
    LOGD( " Beacon Report Passive : %s", client->info->rrm_caps.bcn_rpt_passive ? "Yes":"No" );
    LOGD( " Beacon Report Active  : %s", client->info->rrm_caps.bcn_rpt_active ? "Yes":"No" );
    LOGD( " Beacon Report Table   : %s", client->info->rrm_caps.bcn_rpt_table ? "Yes":"No" );
    LOGD( " LCI measurement       : %s", client->info->rrm_caps.lci_meas ? "Yes":"No");
    LOGD( " FTM Range report      : %s", client->info->rrm_caps.ftm_range_rpt ? "Yes":"No" );

    LOGD( "   ~~~RRM Capabilites~~~     " );
    LOGD( " ASSOC IE length       : %u", client->info->assoc_ies_len);

    LOGD( " ~~~~~~~~~~~~~~~~~~~~ " );

    return;
}

static void bm_client_report_caps(bm_client_t *client, const char *ifname, bsal_client_info_t *info)
{
    bsal_event_t event;

    memset(&event, 0, sizeof(event));
    STRSCPY(event.ifname, ifname);
    event.type = BSAL_EVENT_CLIENT_CONNECT;
    memcpy(&event.data.connect.client_addr,
           &client->macaddr,
           sizeof(event.data.connect.client_addr));

    event.data.connect.is_BTM_supported = info->is_BTM_supported;
    event.data.connect.is_RRM_supported = info->is_RRM_supported;
    event.data.connect.band_cap_2G = info->band_cap_2G | client->band_cap_2G;
    event.data.connect.band_cap_5G = info->band_cap_5G | client->band_cap_5G;
    event.data.connect.assoc_ies_len = info->assoc_ies_len <= ARRAY_SIZE(event.data.connect.assoc_ies)
                                     ? info->assoc_ies_len
                                     : ARRAY_SIZE(event.data.connect.assoc_ies);
    memcpy(&event.data.connect.datarate_info, &info->datarate_info, sizeof(event.data.connect.datarate_info));
    memcpy(&event.data.connect.rrm_caps, &info->rrm_caps, sizeof(event.data.connect.rrm_caps));
    memcpy(&event.data.connect.assoc_ies, info->assoc_ies, event.data.connect.assoc_ies_len);

    bm_stats_add_event_to_report(client, &event, CLIENT_CAPABILITIES, false);
}

static bool bm_client_caps_changed(bm_client_t *client, const char *ifname, bsal_client_info_t *info)
{
    unsigned int i;
    bsal_client_info_t *cur_info;

    for (i = 0; i < client->ifcfg_num; i++) {
        if (!strcmp(client->ifcfg[i].ifname, ifname))
            break;
    }

    if (WARN_ON(i == client->ifcfg_num))
        return false;

    cur_info = &client->ifcfg[i].info;

    // Check if BSS TM capability changed
    if (info->is_BTM_supported != cur_info->is_BTM_supported) {
        LOGT( "Client '%s': BSS TM capability changed, notifying", client->mac_addr );
        return true;
    }

    // Check if RRM capability changed
    if (info->is_RRM_supported != cur_info->is_RRM_supported ) {
        LOGT( "Client '%s': RRM capability changed, notifying", client->mac_addr );
        return true;
    }

    // Check if datarate information changed
    if (memcmp(&info->datarate_info, &cur_info->datarate_info, sizeof(info->datarate_info))) {
        LOGT( "Client '%s': Datarate information changed, notifying", client->mac_addr );
        return true;
    }

    // Check if RRM capabilites changed
    if (memcmp(&info->rrm_caps, &cur_info->rrm_caps, sizeof(info->rrm_caps))) {
        LOGT( "Client '%s': RRM Capabilities changed, notifying", client->mac_addr );
        return true;
    }

    if (cur_info->assoc_ies_len != info->assoc_ies_len) {
        LOGT("%s: assoc_ies_len changed, notifying", client->mac_addr);
        return true;
    }

    if (memcmp(cur_info->assoc_ies, info->assoc_ies, sizeof(cur_info->assoc_ies))) {
        LOGT("%s: assoc_ies[] changed, notifying", client->mac_addr);
        return true;
    }

    return false;
}

static void bm_client_record_client_caps(bm_client_t *client, const char *ifname, bsal_client_info_t *info)
{
    unsigned int i;

    for (i = 0; i < client->ifcfg_num; i++) {
        if (!strcmp(client->ifcfg[i].ifname, ifname))
            break;
    }

    if (WARN_ON(i == client->ifcfg_num))
        return;

    memcpy(&client->ifcfg[i].info, info, sizeof(client->ifcfg[i].info));
    client->info = &client->ifcfg[i].info;
}

static void bm_client_caps_recalc(bm_client_t *client, const char *ifname, bsal_client_info_t *info)
{
    info->band_cap_2G |= client->band_cap_2G;
    info->band_cap_5G |= client->band_cap_5G;

    if (bm_client_caps_changed(client, ifname, info))
        bm_client_report_caps(client, ifname, info);

    bm_client_record_client_caps(client, ifname, info);
}

void bm_client_check_connected(bm_client_t *client, bm_group_t *group, const char *ifname)
{
    bsal_client_info_t          info;
    radio_type_t                radio_type;

    if (target_bsal_client_info(ifname, client->macaddr.addr, &info)) {
        LOGD("%s: Client %s no client info.", ifname, client->mac_addr);
        return;
    }

    if (!info.connected) {
        LOGD("%s: Client %s not connected.", ifname, client->mac_addr);
        return;
    }

    LOGI("%s: Client %s connected.", ifname, client->mac_addr);

    radio_type = bm_group_find_radio_type_by_ifname(ifname);
    switch (radio_type) {
        case RADIO_TYPE_2G:
            client->band_cap_2G = true;
            break;
        case RADIO_TYPE_5G:
        case RADIO_TYPE_5GL:
        case RADIO_TYPE_5GU:
            client->band_cap_5G = true;
            break;
        default:
            break;
    }

    /* Check assoc IEs */
    bm_client_parse_assoc_ies(client, info.assoc_ies, info.assoc_ies_len);

    /* Recalc client capabilities */
    bm_client_caps_recalc(client, ifname, &info);

    bm_kick_cancel_btm_retry_task( client );
    bm_client_preassoc_backoff_recalc(group, client, ifname);

    if (client->steering_state == BM_CLIENT_CLIENT_STEERING) {
        bm_client_cs_connect(client, ifname);
    } else {
        bm_client_bs_connect(group, client, ifname);
    }

    bm_client_connected(client, group, ifname);

    /* Setup self neighbor - channel/bssid/op_class */
    memset(&client->self_neigh, 0, sizeof(client->self_neigh));
    WARN_ON(!bm_neighbor_get_self_neighbor(ifname, &client->self_neigh));

    /* Check how client see our PODs */
    if (client->send_rrm_after_assoc)
        bm_client_send_rrm_req(client, BM_CLIENT_RRM_OWN_BAND_ONLY, 5);
}

static bool
bm_client_add_to_group(bm_client_t *client, bm_group_t *group)
{
    bsal_client_config_t        cli_conf;
    unsigned int                i;

    if (!group->enabled) {
        return true;
    }

    for (i = 0; i < group->ifcfg_num; i++) {
        /* Adding this as band steering by default */
        if (!bm_client_to_bsal_conf_bs(client, group, group->ifcfg[i].radio_type, &cli_conf)) {
            LOGE("Failed to convert (add) client '%s' to BSAL %s config",
                 client->mac_addr,
                 c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type));
            return false;
        }

        if (target_bsal_client_add(group->ifcfg[i].bsal.ifname, client->macaddr.addr, &cli_conf) < 0) {
            LOGE("Failed to add client '%s' band %s to BSAL iface %s",
                 client->mac_addr,
                 c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type),
                 group->ifcfg[i].bsal.ifname);
            return false;
        }

        if (!bm_client_ifcfg_set(group, client, group->ifcfg[i].ifname,
                                 group->ifcfg[i].radio_type,
                                 group->ifcfg[i].bs_allowed, &cli_conf)) {
            LOGE("Failed to add client '%s' band %s to client ifcfg[] %s",
                 client->mac_addr,
                 c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type),
                 group->ifcfg[i].ifname);
            return false;
        }


        LOGI("Client '%s' band %s added to BSAL:%s (probe %d-%d auth %d-%d xing %d-%d-%d)",
             client->mac_addr,
             c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type),
             group->ifcfg[i].bsal.ifname,
             cli_conf.rssi_probe_lwm, cli_conf.rssi_probe_hwm,
             cli_conf.rssi_auth_lwm, cli_conf.rssi_auth_hwm,
             cli_conf.rssi_inact_xing, cli_conf.rssi_low_xing, cli_conf.rssi_high_xing);


        bm_client_check_connected(client, group, group->ifcfg[i].ifname);
    }

    return true;
}

static bool
bm_client_update_group(bm_client_t *client, bm_group_t *group)
{
    bsal_client_config_t        cli_conf;
    unsigned int                i;

    if (!group->enabled) {
        return false;
    }

    for (i = 0; i < group->ifcfg_num; i++) {
        /* Updating this as band steering or client steering */
        if (!bm_client_to_bsal_conf(client, group, group->ifcfg[i].radio_type, &cli_conf)) {
            LOGE("Failed to convert (update) client '%s' to BSAL %s config",
                 client->mac_addr,
                 c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type));
            return false;
        }

        if (target_bsal_client_update(group->ifcfg[i].bsal.ifname, client->macaddr.addr, &cli_conf) < 0) {
            LOGE("Failed to update client '%s' band %s to BSAL iface %s",
                 client->mac_addr,
                 c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type),
                 group->ifcfg[i].bsal.ifname);
            return false;
        }

        if (!bm_client_ifcfg_set(group, client, group->ifcfg[i].ifname,
                                 group->ifcfg[i].radio_type,
                                 group->ifcfg[i].bs_allowed, &cli_conf)) {
            LOGE("Failed to update client '%s' band %s to client ifcfg[] %s",
                 client->mac_addr,
                 c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type),
                 group->ifcfg[i].ifname);
            return false;
        }

        LOGI("Client '%s' band %s updated for BSAL:%s (probe %d-%d auth %d-%d xing %d-%d-%d)",
             client->mac_addr,
             c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type),
             group->ifcfg[i].bsal.ifname,
             cli_conf.rssi_probe_lwm, cli_conf.rssi_probe_hwm,
             cli_conf.rssi_auth_lwm, cli_conf.rssi_auth_hwm,
             cli_conf.rssi_inact_xing, cli_conf.rssi_low_xing, cli_conf.rssi_high_xing);
    }

    return true;
}

static bool
bm_client_remove_from_group(bm_client_t *client, bm_group_t *group)
{
    unsigned int                i;

    if (!group->enabled) {
        return false;
    }

    for (i = 0; i < group->ifcfg_num; i++) {
        if (target_bsal_client_remove(group->ifcfg[i].bsal.ifname, client->macaddr.addr) < 0) {
            LOGE("Failed to remove client '%s' band %s from BSAL:%s",
                 client->mac_addr,
                 c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type),
                 group->ifcfg[i].bsal.ifname);
            continue;
        }

        if (!bm_client_ifcfg_remove(client, group->ifcfg[i].ifname)) {
            LOGE("Failed to remove client '%s' band %s from client ifcfg[] %s",
                 client->mac_addr,
                 c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type),
                 group->ifcfg[i].ifname);
        }

        LOGI("Client '%s' band %s removed from BSAL:%s",
             client->mac_addr,
             c_get_str_by_key(map_bsal_bands, group->ifcfg[i].radio_type),
             group->ifcfg[i].bsal.ifname);
    }

    client->group = NULL;

    return true;
}

static bool
bm_client_add_to_all_groups(bm_client_t *client)
{
    ds_tree_t       *groups;
    bm_group_t       *group;
    bool            success = true;

    if (!(groups = bm_group_get_tree())) {
        LOGE("bm_client_update_all_groups() failed to get group tree");
        return false;
    }

    ds_tree_foreach(groups, group) {
        if (bm_client_add_to_group(client, group) == false) {
            success = false;
        }
    }

    return success;
}

static bool
bm_client_update_all_groups(bm_client_t *client)
{
    ds_tree_t       *groups;
    bm_group_t       *group;
    bool            success = true;

    if (!(groups = bm_group_get_tree())) {
        LOGE("bm_client_update_all_groups() failed to get group tree");
        return false;
    }

    ds_tree_foreach(groups, group) {
        if (bm_client_update_group(client, group) == false) {
            success = false;
        }
    }

    return success;
}

static bool
bm_client_remove_from_all_groups(bm_client_t *client)
{
    ds_tree_t       *groups;
    bm_group_t       *group;
    bool            success = true;

    if (!(groups = bm_group_get_tree())) {
        LOGE("bm_client_update_all_groups() failed to get group tree");
        return false;
    }

    ds_tree_foreach(groups, group) {
        if (bm_client_remove_from_group(client, group) == false) {
            success = false;
        }
    }

    return success;
}

static void
bm_client_remove(bm_client_t *client)
{
    unsigned int i;

    if (!bm_client_remove_from_all_groups(client)) {
        LOGW("Client '%s' failed to remove from one or more groups", client->mac_addr);
    }

    while (evsched_task_cancel_by_find(NULL, client, EVSCHED_FIND_BY_ARG))
        ;

    for (i = 0; i < ARRAY_SIZE(client->rrm_req); i++)
        evsched_task_cancel(client->rrm_req[i].rrm_task);

    bm_kick_cleanup_by_client(client);
    free(client);

    return;
}

static void
bm_client_cs_task( void *arg )
{
    bm_client_t     *client = arg;
    bsal_event_t    event;
    unsigned int    i;

    LOGN( "Client steering enforce period completed for client '%s'", client->mac_addr );

    client->cs_state = BM_CLIENT_CS_STATE_EXPIRED;

    bm_kick_cancel_btm_retry_task(client);
    bm_client_disable_client_steering( client );

    memset( &event, 0, sizeof( event ) );
    if( client->cs_mode == BM_CLIENT_CS_MODE_AWAY ) {
        for (i = 0; i < client->ifcfg_num; i++) {
            STRSCPY(event.ifname, client->ifcfg[i].ifname);
            bm_stats_add_event_to_report(client, &event, CLIENT_STEERING_EXPIRED, false);
        }
    } else if( client->cs_mode == BM_CLIENT_CS_MODE_HOME ) {
        for (i = 0; i < client->ifcfg_num; i++) {
            if (client->ifcfg[i].radio_type != client->cs_radio_type)
                continue;

            STRSCPY(event.ifname, client->ifcfg[i].ifname);
            bm_stats_add_event_to_report(client, &event, CLIENT_STEERING_EXPIRED, false);
        }
    }

    return;
}

static void
bm_client_trigger_client_steering( bm_client_t *client )
{
    char            *modestr = c_get_str_by_key( map_cs_modes, client->cs_mode );
    bsal_event_t    event;
    unsigned int    i;

    if( client->cs_mode == BM_CLIENT_CS_MODE_AWAY ||
        ( client->cs_mode == BM_CLIENT_CS_MODE_HOME &&
          client->cs_radio_type != RADIO_TYPE_NONE ) ) {
        // Set client steering state
        client->steering_state = BM_CLIENT_CLIENT_STEERING;
        LOGN( "Setting state to CLIENT STEERING for '%s'", client->mac_addr );

        if( client->cs_state != BM_CLIENT_CS_STATE_STEERING ) {
            memset( &event, 0, sizeof( event ) );
            if( client->cs_mode == BM_CLIENT_CS_MODE_AWAY ) {
                for (i = 0; i < client->ifcfg_num; i++) {
                    STRSCPY(event.ifname, client->ifcfg[i].ifname);
                    bm_stats_add_event_to_report( client, &event, CLIENT_STEERING_STARTED, false );
                }
            } else if( client->cs_mode == BM_CLIENT_CS_MODE_HOME ) {
                for (i = 0; i < client->ifcfg_num; i++) {
                    if (client->ifcfg[i].radio_type != client->cs_radio_type)
                        continue;
                    STRSCPY(event.ifname, client->ifcfg[i].ifname);
                    bm_stats_add_event_to_report( client, &event, CLIENT_STEERING_STARTED, false );
                }
            }
        }

        // Change cs state to STEERING
        client->cs_state = BM_CLIENT_CS_STATE_STEERING;
        bm_client_update_cs_state( client );

        LOGN( "Triggering client steering for client '%s' and mode '%s'"\
              " and steering state: %d", client->mac_addr, modestr, client->steering_state );
    } else {
        // Band is unspecified. Apply band steering configuration for
        // this client
        LOGN( " Band unspecified client '%s', applying band steering" \
              " configuration", client->mac_addr );

        client->steering_state = BM_CLIENT_STEERING_NONE;
    }

    // Cancel any instances of the timer running
    evsched_task_cancel_by_find( bm_client_cs_task, client,
                               ( EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG ) );
    client->cs_task = evsched_task( bm_client_cs_task,
                                    client,
                                    EVSCHED_SEC( client->cs_enforce_period ) );

    return;
}

/*****************************************************************************/

static const char *
bm_client_get_rrm_bcn_rpt_param( struct schema_Band_Steering_Clients *bscli, char *key )
{
    int i;

    for( i = 0; i < bscli->rrm_bcn_rpt_params_len; i++ ) {
        const char *params_key = bscli->rrm_bcn_rpt_params_keys[i];
        const char *params_val = bscli->rrm_bcn_rpt_params[i];

        if( !strcmp( key, params_key ) ) {
            return params_val;
        }
    }

    return NULL;
}

static bool
bm_client_get_rrm_bcn_rpt_params( struct schema_Band_Steering_Clients *bscli,
                                  bm_client_t *client )
{
    const char      *val;

    if( !( val = bm_client_get_rrm_bcn_rpt_param( bscli, "enable_scan" ))) {
        client->enable_ch_scan = false;
    } else {
        if( !strcmp( val, "true" ) ) {
            client->enable_ch_scan = true;
        } else {
            client->enable_ch_scan = false;
        }
    }

    if( !( val = bm_client_get_rrm_bcn_rpt_param( bscli, "scan_interval" ))) {
        client->ch_scan_interval = RRM_BCN_RPT_DEFAULT_SCAN_INTERVAL;
    } else {
        client->ch_scan_interval = atoi( val );
    }

    return true;
}

static const char *
bm_client_get_cs_param( struct schema_Band_Steering_Clients *bscli, char *key )
{
    int i;

    for( i = 0; i < bscli->cs_params_len; i++ )
    {
        const char *cs_params_key = bscli->cs_params_keys[i];
        const char *cs_params_val = bscli->cs_params[i];

        if( !strcmp( key, cs_params_key ) )
        {
            return cs_params_val;
        }
    }

    return NULL;
}

static bool 
bm_client_get_cs_params( struct schema_Band_Steering_Clients *bscli, bm_client_t *client )
{
    c_item_t                *item;
    const char              *val;

    if( !(val = bm_client_get_cs_param( bscli, "hwm" ))) {
        client->cs_hwm = 0;
    } else {
        client->cs_hwm = atoi( val );
    }

    if( !(val = bm_client_get_cs_param( bscli, "lwm" ))) {
        client->cs_lwm = 0;
    } else {
        client->cs_lwm = atoi( val );
    }

    if( !(val = bm_client_get_cs_param( bscli, "cs_max_rejects" ))) {
        client->cs_max_rejects = 0;
    } else {
        client->cs_max_rejects = atoi( val );
    }

    if( !(val = bm_client_get_cs_param( bscli, "cs_max_rejects_period" ))) {
        client->cs_max_rejects_period = 0;
    } else {
        client->cs_max_rejects_period = atoi( val );
    }

    if( !(val = bm_client_get_cs_param( bscli, "cs_enforce_period" ))) {
        client->cs_enforce_period = 0;
    } else {
        client->cs_enforce_period = atoi( val );
    }

    if( !(val = bm_client_get_cs_param( bscli, "cs_reject_detection" ))) {
        client->cs_reject_detection = BM_CLIENT_REJECT_NONE;
    } else {
        item = c_get_item_by_str(map_ovsdb_reject_detection, val);
        if (!item) {
            LOGE("Client %s - unknown reject detection '%s'",
                    client->mac_addr, val);
            return false;
        }
        client->cs_reject_detection = (bm_client_reject_t)item->key;
    }

    if( !(val = bm_client_get_cs_param( bscli, "band" ))) {
        client->cs_radio_type = RADIO_TYPE_NONE;
    } else {
        item = c_get_item_by_str(map_bsal_bands, val);
        if( !item ) {
            LOGE(" Client %s - unknown band '%s'", client->mac_addr, val );
            return false;
        }
        client->cs_radio_type = ( radio_type_t )item->key;
    }

    /*
     * So far we need this before cloud will set this correctly.
     * Today 5G could be 5G or 5GL/5GU for SP
     */
    if (client->cs_radio_type == RADIO_TYPE_5G) {
        radio_type_t radio_type = client->cs_radio_type;
        unsigned int i;

        for (i = 0; i < client->ifcfg_num; i++) {
            if (client->ifcfg[i].radio_type == RADIO_TYPE_2G)
                continue;
            radio_type = client->ifcfg[i].radio_type;
            break;
        }

        LOGI("overwrite cs_radio_type from %d to %d", client->cs_radio_type, radio_type);
        client->cs_radio_type = radio_type;
    }

    if( !bscli->cs_mode_exists ) {
        client->cs_mode = BM_CLIENT_CS_MODE_OFF;
    } else {
        item = c_get_item_by_str(map_cs_modes, bscli->cs_mode);
        if( !item ) {
            LOGE(" Client %s - unknown Client Steering mode '%s'", client->mac_addr, val );
            return false;
        }
        client->cs_mode = (bm_client_cs_mode_t)item->key;
    }

    if( !bscli->cs_state_exists ) {
        client->cs_state = BM_CLIENT_CS_STATE_NONE;
    } else {
        item = c_get_item_by_str(map_cs_states, bscli->cs_state);
        if( !item ) {
            LOGE(" Client %s - unknown Client Steering state '%s'", client->mac_addr, val );
            return false;
        }
        client->cs_state = (bm_client_cs_state_t)item->key;
    }

    if( !(val = bm_client_get_cs_param( bscli, "cs_probe_block" ))) {
        client->cs_probe_block = false;
    } else {
        if( !strcmp( val, "true" ) ) {
            client->cs_probe_block = true;
        } else {
            client->cs_probe_block = false;
        }
    }

    if( !(val = bm_client_get_cs_param( bscli, "cs_auth_block" ))) {
        client->cs_auth_block = false;
    } else {
        if( !strcmp( val, "true" ) ) {
            client->cs_auth_block = true;
        } else {
            client->cs_auth_block = false;
        }
    }

    if( !(val = bm_client_get_cs_param( bscli, "cs_auth_reject_reason" ))) {
        // Value 0 is used for blocking authentication requests.
        // Hence, use -1 as default
        client->cs_auth_reject_reason = -1;
    } else {
        client->cs_auth_reject_reason = atoi( val );
    }

    // This value is true by default
    if( !(val = bm_client_get_cs_param( bscli, "cs_auto_disable" ))) {
        client->cs_auto_disable = true;
    } else {
        if( !strcmp( val, "false" ) ) {
            client->cs_auto_disable = false;
        } else {
            client->cs_auto_disable = true;
        }
    }


    return true;
}

static bsal_btm_params_t *
bm_client_get_btm_params_by_type( bm_client_t *client, bm_client_btm_params_type_t type )
{
    switch( type )
    {
        case BM_CLIENT_BTM_PARAMS_STEERING:
            return &client->steering_btm_params;

        case BM_CLIENT_BTM_PARAMS_STICKY:
            return &client->sticky_btm_params;

        case BM_CLIENT_BTM_PARAMS_SC:
            return &client->sc_btm_params;

        default:
            return NULL;
    }

    return NULL;
}

#define _bm_client_get_btm_param(bscli, type, key, val) \
    do { \
        int     i; \
        val = NULL; \
        for(i = 0;i < bscli->type##_btm_params_len;i++) { \
            if (!strcmp(key, bscli->type##_btm_params_keys[i])) { \
                val = bscli->type##_btm_params[i]; \
            } \
        } \
    } while(0)

static const char *
bm_client_get_btm_param( struct schema_Band_Steering_Clients *bscli, 
                        bm_client_btm_params_type_t type, char *key )
{
    char    *val = NULL;

    switch( type )
    {
        case BM_CLIENT_BTM_PARAMS_STEERING:
            _bm_client_get_btm_param( bscli, steering, key, val );
            break;

        case BM_CLIENT_BTM_PARAMS_STICKY:
            _bm_client_get_btm_param( bscli, sticky, key, val );
            break;

        case BM_CLIENT_BTM_PARAMS_SC:
            _bm_client_get_btm_param( bscli, sc, key, val );
            break;

        default:
            LOGW( "Unknown btm_params_type '%d'", type );
            break;
    }

    return val;
}

static bool
bm_client_get_btm_params( struct schema_Band_Steering_Clients *bscli,
                         bm_client_t *client, bm_client_btm_params_type_t type )
{
    bsal_btm_params_t           *btm_params  = NULL;
    bsal_neigh_info_t           *neigh       = NULL;
    os_macaddr_t                bssid;
    const char                  *val;
    char                        mac_str[18]  = { 0 };

    btm_params = bm_client_get_btm_params_by_type( client, type );
    if( !btm_params ) {
        LOGE( "Client %s - error getting BTM params type '%d'", client->mac_addr, type );
        return false;
    }
    memset(btm_params, 0, sizeof(*btm_params));

    // Process base BTM parameters
    if ((val = bm_client_get_btm_param( bscli, type, "valid_int" ))) {
        btm_params->valid_int = atoi( val );
    }
    else {
        btm_params->valid_int = BTM_DEFAULT_VALID_INT;
    }

    if ((val = bm_client_get_btm_param( bscli, type, "abridged" ))) {
        btm_params->abridged = atoi(val);
    }
    else {
        btm_params->abridged = BTM_DEFAULT_ABRIDGED;
    }

    if ((val = bm_client_get_btm_param( bscli, type, "pref" ))) {
        btm_params->pref = atoi( val );
    }
    else {
        btm_params->pref = BTM_DEFAULT_PREF;
    }

    if ((val = bm_client_get_btm_param( bscli, type, "disassoc_imminent" ))) {
        btm_params->disassoc_imminent = atoi( val );
    }
    else {
        btm_params->disassoc_imminent = BTM_DEFAULT_DISASSOC_IMMINENT;
    }

    if ((val = bm_client_get_btm_param( bscli, type, "bss_term" ))) {
        btm_params->bss_term = atoi( val );
    }
    else {
        btm_params->bss_term = BTM_DEFAULT_BSS_TERM;
    }

    if ((val = bm_client_get_btm_param( bscli, type, "btm_max_retries" ))) {
        btm_params->max_tries = atoi( val ) + 1;
    } else {
        btm_params->max_tries = BTM_DEFAULT_MAX_RETRIES + 1;
    }

    if ((val = bm_client_get_btm_param( bscli, type, "btm_retry_interval" ))) {
        btm_params->retry_interval = atoi( val );
    } else {
        btm_params->retry_interval = BTM_DEFAULT_RETRY_INTERVAL;
    }

    if( !( val = bm_client_get_btm_param( bscli, type, "inc_neigh" ))) {
        btm_params->inc_neigh = false;
    } else {
        if( !strcmp( val, "true" ) ) {
            btm_params->inc_neigh = true;
        } else {
            btm_params->inc_neigh = false;
        }
    }

    if( !( val = bm_client_get_btm_param( bscli, type, "inc_self" ))) {
        /* Base on disassoc imminent here */
        if (btm_params->disassoc_imminent)
            btm_params->inc_self = false;
        else
            btm_params->inc_self = true;
    } else {
        if( !strcmp( val, "true" ) ) {
            btm_params->inc_self = true;
        } else {
            btm_params->inc_self = false;
        }
    }

    // Check for static neighbor parameters
    if ((val = bm_client_get_btm_param( bscli, type, "bssid" ))) {
        neigh = &btm_params->neigh[0];
        STRSCPY(mac_str, val);
        if(strlen(mac_str) > 0) {
            if(!os_nif_macaddr_from_str( &bssid, mac_str)) {
                LOGE("bm_client_get_btm_params: Failed to parse bssid '%s'", mac_str);
                return false;
            }
            memcpy(&neigh->bssid, &bssid, sizeof(bssid));
            btm_params->num_neigh = 1;
        }

        if ((val = bm_client_get_btm_param( bscli, type, "bssid_info" ))) {
            neigh->bssid_info = atoi( val );
        }
        else {
            neigh->bssid_info = BTM_DEFAULT_NEIGH_BSS_INFO;
        }

        if ((val = bm_client_get_btm_param( bscli, type, "phy_type" ))) {
            neigh->phy_type = atoi( val );
        }

        if ((val = bm_client_get_btm_param( bscli, type, "channel" ))) {
            neigh->channel = atoi( val );

            if ((val = bm_client_get_btm_param( bscli, type, "op_class" ))) {
                neigh->op_class = atoi( val );
            }
        }

        /* If not set, derive op_class and phy_type from channel */
        if (neigh->channel && type == BM_CLIENT_BTM_PARAMS_SC) {
            if (!neigh->op_class) {
                neigh->op_class = bm_neighbor_get_op_class(neigh->channel);
                LOGD("%s: steering kick, setup op_class %d base on %d channel", client->mac_addr,
                     neigh->op_class, neigh->channel);
            }

            if (!neigh->phy_type) {
                neigh->phy_type = bm_neighbor_get_phy_type(neigh->channel);
                LOGD("%s: steering kick, setup phy_type %d base on %d channel", client->mac_addr,
                     neigh->phy_type, neigh->channel);
            }
        }
    }

    return true;
}

static bool
bm_client_from_ovsdb(struct schema_Band_Steering_Clients *bscli, bm_client_t *client)
{
    c_item_t                *item;
    char                    *pref_allowed;

    STRSCPY(client->mac_addr, bscli->mac);

    if (!os_nif_macaddr_from_str(&client->macaddr, bscli->mac)) {
        LOGE("Failed to parse mac address '%s'", bscli->mac);
        return false;
    }

    if (!bscli->reject_detection_exists) {
        client->reject_detection = BM_CLIENT_REJECT_NONE;
    } else {
        item = c_get_item_by_str(map_ovsdb_reject_detection, bscli->reject_detection);
        if (!item) {
            LOGE("Client %s - unknown reject detection '%s'",
                                                client->mac_addr, bscli->reject_detection);
            return false;
        }
        client->reject_detection = (bm_client_reject_t)item->key;
    }

    if (!bscli->kick_type_exists) {
        client->kick_type = BM_CLIENT_KICK_NONE;
    } else {
        item = c_get_item_by_str(map_ovsdb_kick_type, bscli->kick_type);
        if (!item) {
            LOGE("Client %s - unknown kick type '%s'", client->mac_addr, bscli->kick_type);
            return false;
        }
        client->kick_type                   = (bm_client_kick_t)item->key;
    }

    if (!bscli->sc_kick_type_exists) {
        client->sc_kick_type = BM_CLIENT_KICK_NONE;
    } else {
        item = c_get_item_by_str(map_ovsdb_kick_type, bscli->sc_kick_type);
        if (!item) {
            LOGE("Client %s - unknown sc client kick type '%s'", client->mac_addr, bscli->sc_kick_type);
            return false;
        }
        client->sc_kick_type                = (bm_client_kick_t)item->key;
    }

    if (!bscli->sticky_kick_type_exists) {
        client->sticky_kick_type = BM_CLIENT_KICK_NONE;
    } else {
        item = c_get_item_by_str(map_ovsdb_kick_type, bscli->sticky_kick_type);
        if (!item) {
            LOGE("Client %s - unknown sticky client kick type '%s'", client->mac_addr, bscli->sc_kick_type);
            return false;
        }
        client->sticky_kick_type            = (bm_client_kick_t)item->key;
    }

    if (bscli->pref_bs_allowed_exists) {
        pref_allowed = bscli->pref_bs_allowed;
    } else if (bscli->pref_5g_exists) {
        pref_allowed = bscli->pref_5g;
    } else {
        pref_allowed = NULL;
    }

    if (!pref_allowed) {
        client->pref_allowed = BM_CLIENT_PREF_ALLOWED_NEVER;
    } else {
        item = c_get_item_by_str(map_ovsdb_pref_allowed, pref_allowed);
        if (!item) {
            LOGE("Client %s - unknown pref_allowed value '%s'", client->mac_addr, pref_allowed);
            return false;
        }
        client->pref_allowed                = (bm_client_pref_allowed)item->key;
    }

    if (!bscli->force_kick_exists) {
        client->force_kick_type = BM_CLIENT_FORCE_KICK_NONE;
    } else {
        item = c_get_item_by_str(map_ovsdb_force_kick, bscli->force_kick);
        if (!item) {
            LOGE("Client %s - unknown force_kick value '%s'", client->mac_addr, bscli->force_kick);
            return false;
        }
        client->force_kick_type             = (bm_client_force_kick_t)item->key;
    }

    client->kick_reason                 = bscli->kick_reason;
    client->sc_kick_reason              = bscli->sc_kick_reason;
    client->sticky_kick_reason          = bscli->sticky_kick_reason;

    client->hwm                         = bscli->hwm;
    client->lwm                         = bscli->lwm;

    client->max_rejects                 = bscli->max_rejects;
    client->max_rejects_period          = bscli->rejects_tmout_secs;
    client->backoff_period              = bscli->backoff_secs;

    if (!bscli->backoff_exp_base_exists) {
        client->backoff_exp_base = BM_CLIENT_DEFAULT_BACKOFF_EXP_BASE;
    } else {
        client->backoff_exp_base = bscli->backoff_exp_base;
    }

    if (!bscli->steer_during_backoff_exists) {
        client->steer_during_backoff = BM_CLIENT_DEFAULT_STEER_DURING_BACKOFF;
    } else {
        client->steer_during_backoff = bscli->steer_during_backoff;
    }

    if (!bscli->sticky_kick_guard_time_exists) {
        client->sticky_kick_guard_time = BM_CLIENT_STICKY_KICK_GUARD_TIME;
    } else {
        client->sticky_kick_guard_time = bscli->sticky_kick_guard_time;
    }

    if (!bscli->steering_kick_guard_time_exists) {
        client->steering_kick_guard_time = BM_CLIENT_STEERING_KICK_GUARD_TIME;
    } else {
        client->steering_kick_guard_time = bscli->steering_kick_guard_time;
    }

    if (!bscli->sticky_kick_backoff_time_exists) {
        client->sticky_kick_backoff_time = BM_CLIENT_STICKY_KICK_BACKOFF_TIME;
    } else {
        client->sticky_kick_backoff_time = bscli->sticky_kick_backoff_time;
    }

    if (!bscli->steering_kick_backoff_time_exists) {
        client->steering_kick_backoff_time = BM_CLIENT_STEERING_KICK_BACKOFF_TIME;
    } else {
        client->steering_kick_backoff_time = bscli->steering_kick_backoff_time;
    }

    if (!bscli->settling_backoff_time_exists) {
        client->settling_backoff_time = BM_CLIENT_SETTLING_BACKOFF_TIME;
    } else {
        client->settling_backoff_time = bscli->settling_backoff_time;
    }

    // If the kick_debounce_period or sc_kick_debounce_period was
    // changed, reset the last_kick time
    if( ( client->kick_debounce_period != bscli->kick_debounce_period ) ||
        ( client->sc_kick_debounce_period != bscli->kick_debounce_period ) )
    {
        client->times.last_kick = 0;
    }

    client->kick_debounce_period        = bscli->kick_debounce_period;
    client->sc_kick_debounce_period     = bscli->sc_kick_debounce_period;
    client->sticky_kick_debounce_period = bscli->sticky_kick_debounce_period;

    client->kick_upon_idle              = bscli->kick_upon_idle;
    client->pre_assoc_auth_block        = bscli->pre_assoc_auth_block;

    if (bscli->preq_snr_thr_exists) {
        client->preq_snr_thr = bscli->preq_snr_thr;
    } else {
        client->preq_snr_thr = BM_CLIENT_DEFAULT_PREQ_SNR_THR;
    }

    // Fetch all Client Steering parameters
    if( !bm_client_get_cs_params( bscli, client ) ) {
        LOGE( "Client %s - error getting client steering parameters",
                                                        client->mac_addr );
        return false;
    }

    // Fetch post-association transition management parameters
    if( !bm_client_get_btm_params( bscli, client, BM_CLIENT_BTM_PARAMS_STEERING )) {
        LOGE( "Client %s - error getting steering tm parameters", client->mac_addr );
        return false;
    }

    // Fetch sticky client transition management parameters
    if( !bm_client_get_btm_params( bscli, client, BM_CLIENT_BTM_PARAMS_STICKY ) ) {
        LOGE( "Client %s - error getting sticky tm parameters", client->mac_addr );
        return false;
    }

    // Fetch cloud-assisted(force kick) transition management parameters
    if( !bm_client_get_btm_params( bscli, client, BM_CLIENT_BTM_PARAMS_SC ) ) {
        LOGE( "Client %s - error getting sc tm parameters", client->mac_addr );
        return false;
    }

    // Fetch RRM Beacon Rpt Request parameters
    if( !bm_client_get_rrm_bcn_rpt_params( bscli, client ) ) {
        LOGE( "Client %s - error getting rrm_bcn_rpt params", client->mac_addr );
        return false;
    }

    if (!bscli->send_rrm_after_assoc_exists) {
        client->send_rrm_after_assoc = true;
    } else {
        client->send_rrm_after_assoc = bscli->send_rrm_after_assoc;
    }

    if (!bscli->send_rrm_after_xing_exists) {
        client->send_rrm_after_xing = true;
    } else {
        client->send_rrm_after_xing = bscli->send_rrm_after_xing;
    }

    if (!bscli->rrm_better_factor_exists) {
        client->rrm_better_factor = 3;
    } else {
        client->rrm_better_factor = bscli->rrm_better_factor;
    }

    if (!bscli->rrm_age_time_exists) {
        client->rrm_age_time = 15;
    } else {
        client->rrm_age_time = bscli->rrm_age_time;
    }

    if (!bscli->active_treshold_bps_exists) {
        client->active_treshold_bps = BM_CLIENT_DEFAULT_ACTIVITY_BPS_TH;
    } else {
        client->active_treshold_bps = bscli->active_treshold_bps;
    }

    return true;
}

/*
 * Check to see if OVSDB update transaction is from locally
 * initiated update or not.  Currently device only updates
 * the cs_state column.
 *
 * The cloud will only ever clear or set "none" to cs_state.
 */
static bool
bm_client_ovsdb_update_from_me(ovsdb_update_monitor_t *self,
                               struct schema_Band_Steering_Clients *bscli)
{
    c_item_t        *item;

    // Check if cs_state column was updated
    if (!json_object_get(self->mon_json_old,
                                SCHEMA_COLUMN(Band_Steering_Clients, cs_state))) {
        // It was NOT update: Cannot be my update
        return false;
    }

    // Check if new value is empty
    if (strlen(bscli->cs_state) == 0) {
        // It is empty: Cannot be my update
        return false;
    }

    // Decode new state value
    if (!bscli->cs_state_exists) {
        return false;
    } else if (!(item = c_get_item_by_str(map_cs_states, bscli->cs_state))) {
        // Could not decode state value, assume it's from cloud
        return false;
    }

    // Check if new state value is NONE
    if (item->key == BM_CLIENT_CS_STATE_NONE) {
        // It is, must be from cloud
        return false;
    }

    // Looks like it's our own update
    return true;
}

static bool
bm_client_lwm_toggled( uint8_t prev_lwm, bm_client_t *client, bool enable )
{
    if( enable ) {
        if( prev_lwm != BM_KICK_MAGIC_NUMBER &&
            client->lwm == BM_KICK_MAGIC_NUMBER ) {
            return true;
        }
    } else {
        if( prev_lwm == BM_KICK_MAGIC_NUMBER &&
            client->lwm != BM_KICK_MAGIC_NUMBER ) {
            return true;
        }
    }

    return false;
}

static bool
bm_client_force_kick_type_toggled( bm_client_force_kick_t prev_kick,
                                   bm_client_t *client, bool enable )
{
    if( enable ) {
        if( prev_kick == BM_CLIENT_FORCE_KICK_NONE &&
            client->force_kick_type != BM_CLIENT_FORCE_KICK_NONE ) {
            return true;
        }
    } else {
        if( prev_kick != BM_CLIENT_FORCE_KICK_NONE &&
            client->force_kick_type == BM_CLIENT_FORCE_KICK_NONE ) {
            return true;
        }
    }

    return false;
}

static void
bm_client_ovsdb_update_cb(ovsdb_update_monitor_t *self)
{
    struct schema_Band_Steering_Clients     bscli;
    pjs_errmsg_t                            perr;
    bm_client_t                             *client;
    bsal_event_t                            event;

    uint8_t                                 prev_lwm;
    bm_client_force_kick_t                  prev_force_kick;
    unsigned int                            i;

    switch(self->mon_type) {

    case OVSDB_UPDATE_NEW:
        if (!schema_Band_Steering_Clients_from_json(&bscli,
                                                    self->mon_json_new, false, perr)) {
            LOGE("Failed to parse new Band_Steering_Clients row: %s", perr);
            return;
        }

        if ((client = bm_client_find_by_macstr(bscli.mac))) {
            LOGE("Ignoring duplicate client '%s' (orig uuid=%s, new uuid=%s)",
                                           client->mac_addr, client->uuid, bscli._uuid.uuid);
            return;
        }

        client = calloc(1, sizeof(*client));
        STRSCPY(client->uuid, bscli._uuid.uuid);

        if (!bm_client_from_ovsdb(&bscli, client)) {
            LOGE("Failed to convert row to client (uuid=%s)", client->uuid);
            free(client);
            return;
        }

        if( client->cs_mode != BM_CLIENT_CS_MODE_OFF ) {
            bm_client_trigger_client_steering( client );
        } else {
            if( client->steering_state == BM_CLIENT_CLIENT_STEERING ) {
                client->steering_state = BM_CLIENT_STEERING_NONE;
            }

            evsched_task_cancel_by_find( bm_client_cs_task, client,
                                       ( EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG ) );
        }

        if (!bm_client_add_to_all_groups(client)) {
            LOGW("Client '%s' failed to add to one or more groups", client->mac_addr);
        }

        ds_tree_insert(&bm_clients, client, client->mac_addr);
        LOGN("Added client %s (hwm=%u, lwm=%u, reject=%s, max_rejects=%d/%d sec)",
                                    client->mac_addr,
                                    client->hwm, client->lwm,
                                    c_get_str_by_key(map_ovsdb_reject_detection,
                                                     client->reject_detection),
                                    client->max_rejects, client->max_rejects_period);

        break;

    case OVSDB_UPDATE_MODIFY:
        if (!(client = bm_client_find_by_uuid(self->mon_uuid))) {
            LOGE("Unable to find client for modify with uuid=%s", self->mon_uuid);
            return;
        }

        if (!schema_Band_Steering_Clients_from_json(&bscli,
                                                    self->mon_json_new, true, perr)) {
            LOGE("Failed to parse modified Band_Steering_Clients row uuid=%s: %s",
                                                                    self->mon_uuid, perr);
            return;
        }

        /* Check to see if this is our own update of cs_state */
        /* NOTE: So this the sequence of events that the controller does when it sets client steering:
           ( each step is a separate ovsdb transaction )
            1. clear out cs_mode and other cs_params
            2. enable client steering, by setting cs_params, and cs_mode
            3. set lwm:=1 or set force_kick value
            4. set lwm:=0 or unset force_kick value

            The need for the below code comes because, after step 2, BM modifies
            the cs_state variable to tell the controller that it has set the cs_state to steering for
            this client. This write results in another ovsdb transaction. Since, this is a single variable
            change, it generally occurs at the same time as the kick value. OVSDB has been observed
            to combine the two into a single transaction, thereby resulting in the force being ignored.
        */
        if ( bm_client_ovsdb_update_from_me(self, &bscli) &&
           ( ( strlen( bscli.force_kick ) == 0 && bscli.lwm != BM_KICK_MAGIC_NUMBER ) ||
             ( !strcmp( bscli.force_kick, "none" )))) {
            LOGT("Ignoring my own client update");
            return;
        }

        // Get the existing values of force_kick and lwm for this client
        prev_lwm        = client->lwm;
        prev_force_kick = client->force_kick_type;

        if (!bm_client_from_ovsdb(&bscli, client)) {
            LOGE("Failed to convert row to client for modify (uuid=%s)", client->uuid);
            return;
        }

        if( bm_client_lwm_toggled( prev_lwm, client, true ) ||
            bm_client_force_kick_type_toggled( prev_force_kick, client, true ) )
        {
            // Force kick the client
            LOGN( "Client '%s': Force kicking due to cloud request", client->mac_addr );
            bm_kick(client, BM_FORCE_KICK, 0);
            return;
        } else if( bm_client_lwm_toggled( prev_lwm, client, false ) ||
                   bm_client_force_kick_type_toggled( prev_force_kick, client, false ) ) {
            LOGN( "Client '%s': Kicking mechanism toggled back, ignoring all" \
                  " groups update", client->mac_addr );
            return;
        }

        if( client->cs_mode != BM_CLIENT_CS_MODE_OFF ) {
            bm_client_trigger_client_steering( client );
        } else {
            if( client->steering_state == BM_CLIENT_CLIENT_STEERING ) {
                client->steering_state = BM_CLIENT_STEERING_NONE;

                client->cs_state = BM_CLIENT_CS_STATE_EXPIRED;
                bm_client_update_cs_state( client );

                // NB: If the controller turns of client steering before cs_enforcement_period,
                //     the device does not if the cs_mode was set to home or away. Hence, send
                //     CLIENT_STEERING_DISABLED event on both bands
                memset( &event, 0, sizeof( event ) );

                for (i = 0; i < client->ifcfg_num; i++) {
                    STRSCPY(event.ifname, client->ifcfg[i].ifname);
                    bm_stats_add_event_to_report( client, &event, CLIENT_STEERING_DISABLED, false );
                }

            }

            evsched_task_cancel_by_find( bm_client_cs_task, client,
                                       ( EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG ) );
        }

        if (!bm_client_update_all_groups(client)) {
            LOGW("Client '%s' failed to update one or more groups", client->mac_addr);
        }

        LOGN("Updated client %s (hwm=%u, lwm=%u, max_rejects=%d/%d sec)",
                                            client->mac_addr, client->hwm, client->lwm,
                                            client->max_rejects, client->max_rejects_period);

        break;

    case OVSDB_UPDATE_DEL:
        if (!(client = bm_client_find_by_uuid(self->mon_uuid))) {
            LOGE("Unable to find client for delete with uuid=%s", self->mon_uuid);
            return;
        }

        LOGN("Removing client %s", client->mac_addr);

        // Remove the client from the Band Steering report list
        bm_stats_remove_client_from_report( client );

        ds_tree_remove(&bm_clients, client);
        bm_client_remove(client);

        break;

    default:
        break;

    }
}

static void
bm_client_backoff(bm_client_t *client, bool enable)
{
    int connect_counter;
    int exp;

    client->backoff = enable;

    if (client->state == BM_CLIENT_STATE_BACKOFF) {
        // State cannot be backoff for enable or disable
        return;
    }

    if (enable) {
        bm_client_set_state(client, BM_CLIENT_STATE_BACKOFF);
        bm_client_update_all_groups(client);


        connect_counter = client->backoff_connect_counter;
        if (connect_counter > 10)
            connect_counter = 10;

        exp = pow(client->backoff_exp_base, connect_counter);
        client->backoff_period_used = client->backoff_period * exp;

        LOGI("%s using pre-assoc backoff period %dsec * %d (%d)", client->mac_addr,
             client->backoff_period, exp, client->backoff_connect_counter);

        client->backoff_task = evsched_task(bm_client_task_backoff,
                                            client,
                                            EVSCHED_SEC(client->backoff_period_used));
    }
    else if (client->num_rejects != 0) {
        LOGI("%s disable pre-assoc backoff", client->mac_addr);
        client->num_rejects = 0;
        client->backoff_connect_calculated = false;
        evsched_task_cancel(client->backoff_task);
        bm_client_update_all_groups(client);
    } else {
        LOGI("%s pre-assoc backoff(%d) num_rejects %d", client->mac_addr, enable, client->num_rejects);
    }

    return;
}

void
bm_client_disable_client_steering( bm_client_t *client )
{
    // Stop enforcing the client steering parameters
    evsched_task_cancel_by_find( bm_client_cs_task, client,
                               ( EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG ) );

    // Update the cs_state to OVSDB
    bm_client_update_cs_state( client );

    client->steering_state = BM_CLIENT_STEERING_NONE;

    // Disable client steering for this client, and reenable band steering
    bm_client_update_all_groups( client );

    return;
}

static void
bm_client_disable_steering(bm_client_t *client) 
{
    // Disable band steering for this client
    client->hwm = 0;
    bm_client_update_all_groups(client);
    return;
}

static void
bm_client_task_backoff(void *arg)
{
    bm_client_t         *client = arg;
    bsal_event_t        event;
    unsigned int        i;

    LOGN("'%s' pre-assoc backoff period has expired, re-enabling steering", client->mac_addr);

    // If the client has connected during backoff period:
    // - 0N 5G  : bm_client_state_change() disables backoff immediately and
    //            re-enables steering. As a result, the code never arrives here.
    // - ON 2.4G: state is changed to steering, but backoff timer is finish
    //            gracefully. State change to DISCONNECTED should not be done,
    //            and steering should be re-enabled
    if( client->state != BM_CLIENT_STATE_CONNECTED ) {
        bm_client_state_change(client, BM_CLIENT_STATE_DISCONNECTED, true);
    }

    bm_client_backoff(client, false);

    memset(&event, 0, sizeof(event));
    for (i = 0; i < client->ifcfg_num; i++) {
        if (client->ifcfg[i].bs_allowed)
            continue;

        STRSCPY(event.ifname, client->ifcfg[i].ifname);
        bm_stats_add_event_to_report( client, &event, BACKOFF, false );
    }

    return;
}

static void
bm_client_state_task( void *arg )
{
    bm_client_t         *client = arg;

    evsched_task_cancel_by_find( bm_client_state_task, client,
                               ( EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_FUNC ) );

    if( client->state == BM_CLIENT_STATE_CONNECTED ) {
        LOGT( "Client '%s' connected, client state machine in proper state",
                                                        client->mac_addr );
        return;
    }

    // Reset the client's state machine to DISCONNECTED state
    client->connected = false;
    STRSCPY(client->ifname, "");
    bm_client_set_state( client, BM_CLIENT_STATE_DISCONNECTED );

    return;
}


static void
bm_client_state_change(bm_client_t *client, bm_client_state_t state, bool force)
{
    // The stats report interval is used to reset the client's state machine
    // from STEERING_5G to DISCONNECTED so that the client is not stuck in
    // in STEERING_5G for long periods of time.
    uint16_t      interval = bm_stats_get_stats_report_interval();
    unsigned int  i;
    bsal_event_t event;

    if (!force && client->state == BM_CLIENT_STATE_BACKOFF) {
        if (state != BM_CLIENT_STATE_CONNECTED) {
            // Ignore state changes not forced while in backoff
            return;
        }
    }

    memset(&event, 0, sizeof(event));

    // Add backoff event to stats report only if the client was in
    // BM_CLIENT_STATE_BACKOFF
    if( client->state == BM_CLIENT_STATE_BACKOFF &&
        state == BM_CLIENT_STATE_CONNECTED &&
        /* TODO check logic here */
        bm_client_bs_ifname_allowed(client, client->ifname)) {
        for (i = 0; i < client->ifcfg_num; i++) {
            if (client->ifcfg[i].bs_allowed)
                continue;

            STRSCPY(event.ifname, client->ifcfg[i].ifname);
            bm_stats_add_event_to_report(client, &event, BACKOFF, false);
        }
    }

    if (client->state != state) {
        LOGI("'%s' changed state %s -> %s",
                        client->mac_addr,
                        c_get_str_by_key(map_state_names, client->state),
                        c_get_str_by_key(map_state_names, state));
        client->state = state;
        client->times.last_state_change = time(NULL);

        switch(client->state)
        {
            case BM_CLIENT_STATE_CONNECTED:
            {
                // If the client has connected during backoff, only disable band
                // steering immediately if the client connects on 5GHz.
                if( bm_client_bs_ifname_allowed(client, client->ifname)) {
                    bm_client_backoff(client, false);
                }
                break;
            }

            case BM_CLIENT_STATE_STEERING:
            {
                for (i = 0; i < client->ifcfg_num; i++) {
                    /*
                     * At the moment BAND_STEERING_ATTEMPT is allowed only for
                     * 2.4 to 5 GHz. Cloud expects BAND_STEERING_ATTEMPT to convey
                     * information about "from" band (2.4 GHz), therefore we look
                     * for iface with disable bs_allowed.
                     */
                    if (client->ifcfg[i].bs_allowed)
                        continue;

                    STRSCPY(event.ifname, client->ifcfg[i].ifname);
                    bm_stats_add_event_to_report(client, &event, BAND_STEERING_ATTEMPT, false);
                }

                evsched_task_cancel_by_find( bm_client_state_task, client,
                                           ( EVSCHED_FIND_BY_ARG | EVSCHED_FIND_BY_FUNC ) );

                client->state_task = evsched_task( bm_client_state_task,
                                                   client,
                                                   EVSCHED_SEC( interval ) );
                break;
            }

            default:
            {
                break;
            }

        }
    }

    return;
}

static void
bm_client_cs_task_rssi_xing( void *arg )
{
    bm_client_t     *client = arg;
    bsal_event_t    event;
    unsigned int    i;

    if( !client ) {
        LOGT( "Client arg is NULL" );
        return;
    }

    client->cs_state = BM_CLIENT_CS_STATE_XING_DISABLED;

    if( client->cs_auto_disable ) {
        LOGT( "Client '%s': Disabling client steering"
              " because of rssi xing", client->mac_addr );

        bm_client_disable_client_steering( client );

        memset( &event, 0, sizeof( event ) );
        if( client->cs_mode == BM_CLIENT_CS_MODE_AWAY ) {
            for (i = 0; i < client->ifcfg_num; i++) {
                STRSCPY(event.ifname, client->ifcfg[i].ifname);
                bm_stats_add_event_to_report(client, &event, CLIENT_STEERING_DISABLED, false);
            }
        } else if( client->cs_mode == BM_CLIENT_CS_MODE_HOME ) {
            for (i = 0; i < client->ifcfg_num; i++) {
                if (client->ifcfg[i].radio_type != client->cs_radio_type)
                    continue;
 
                STRSCPY(event.ifname, client->ifcfg[i].ifname);
                bm_stats_add_event_to_report(client, &event, CLIENT_STEERING_DISABLED, false);
            }
        }
    } else {
        LOGT( "Client '%s': Updating the cs_state to XING_DISABLED",
                                                client->mac_addr );
        bm_client_update_cs_state( client );
    }

    return;
}

/*****************************************************************************/
bool
bm_client_init(void)
{
    LOGI("Client Initializing");

    // Start OVSDB monitoring
    if (!ovsdb_update_monitor(&bm_client_ovsdb_mon,
                              bm_client_ovsdb_update_cb,
                              SCHEMA_TABLE(Band_Steering_Clients),
                              OMT_ALL)) {
        LOGE("Failed to monitor OVSDB table '%s'", SCHEMA_TABLE(Band_Steering_Clients));
        return false;
    }

    return true;
}

bool
bm_client_cleanup(void)
{
    ds_tree_iter_t  iter;
    bm_client_t     *client;

    LOGI("Client cleaning up");

    client = ds_tree_ifirst(&iter, &bm_clients);
    while(client) {
        ds_tree_iremove(&iter);

        bm_client_remove(client);

        client = ds_tree_inext(&iter);
    }

    return true;
}

void
bm_client_set_state(bm_client_t *client, bm_client_state_t state)
{
    bm_client_state_change(client, state, false);
    return;
}

void
bm_client_connected(bm_client_t *client, bm_group_t *group, const char *ifname)
{
    bsal_event_t                event;
    bm_client_stats_t           *stats;
    bm_client_times_t           *times;
    time_t                      now = time(NULL);

    client->group = group;

    memset(&event, 0, sizeof(event));
    STRSCPY(event.ifname, ifname);
    event.type = BSAL_EVENT_CLIENT_CONNECT;

    if (client->state == BM_CLIENT_STATE_CONNECTED) {
        if (strcmp(client->ifname, ifname))
            bm_stats_add_event_to_report(client, &event, CONNECT, false);
    } else {
        STRSCPY(client->ifname, ifname);
        bm_client_set_state(client, BM_CLIENT_STATE_CONNECTED);
        bm_stats_add_event_to_report(client, &event, CONNECT, false);
    }

    STRSCPY(client->ifname, ifname); 
    client->connected = true;
    client->prev_xing_snr = 0;

    stats = bm_client_get_stats(client, ifname);
    if (WARN_ON(!stats))
        return;

    stats->connects++;

    times = &client->times;
    times->last_connect = now;

    bm_client_print_client_caps( client );

    client->bytes_report_time = now;
    client->tx_bytes = 0;
    client->rx_bytes = 0;

    /* By default set active */
    bm_client_activity_action(client, true);

    return;
}

void
bm_client_disconnected(bm_client_t *client)
{
    client->connected = false;
    client->prev_xing_snr = 0;

    if (client->state == BM_CLIENT_STATE_CONNECTED) {
        bm_client_set_state(client, BM_CLIENT_STATE_DISCONNECTED);
    }
    return;
}

void
bm_client_rejected(bm_client_t *client, bsal_event_t *event)
{
    bm_client_stats_t       *stats;
    bm_client_times_t       *times              = &client->times;
    int                     max_rejects         = client->max_rejects;
    int                     max_rejects_period  = client->max_rejects_period;
    time_t                  now                 = time(NULL);

    bsal_event_t            stats_event;
    unsigned int            i;

    stats = bm_client_get_stats(client, event->ifname);
    if (WARN_ON(!stats))
        return;

    if( client->cs_state == BM_CLIENT_CS_STATE_STEERING ) {
        max_rejects         = client->cs_max_rejects;
        max_rejects_period  = client->cs_max_rejects_period;
    }

    if (client->num_rejects > 0) {
        if ((now - times->reject.first) > max_rejects_period) {
            client->num_rejects = 0;
        }
    }

    stats->rejects++;
    client->num_rejects++;
    client->num_rejects_copy++;

    if( event->type == BSAL_EVENT_AUTH_FAIL ) {
        LOGD("'%s' auth reject %d/%d detected within %u seconds",
                                client->mac_addr, client->num_rejects,
                                max_rejects, max_rejects_period);
    } else {
        LOGD("'%s' reject %d/%d detected within %u seconds",
                                client->mac_addr, client->num_rejects,
                                max_rejects, max_rejects_period);
    }

    times->reject.last = now;
    if (client->num_rejects == 1) {
        times->reject.first = now;

        // If client is under CS_STATE_STEERING, this is the first probe request
        // that is blocked. Inform the cloud of the CS ATTEMPT
        if( client->cs_state == BM_CLIENT_CS_STATE_STEERING ) {
            bm_stats_add_event_to_report( client, event, CLIENT_STEERING_ATTEMPT, false );
        }
    }

    if (client->num_rejects == max_rejects) {
        stats->steering_fail_cnt++;

        if( client->steering_state == BM_CLIENT_CLIENT_STEERING ) {
            LOGW( "'%s' failed to client steer, disabling client steering ",
                            client->mac_addr );

            client->num_rejects = 0;

            // Update the cs_state to OVSDB
            client->cs_state = BM_CLIENT_CS_STATE_FAILED;
            bm_client_disable_client_steering( client );

            memset( &stats_event, 0, sizeof( stats_event ) );
            if( client->cs_mode == BM_CLIENT_CS_MODE_AWAY ) {
                for (i = 0; i < client->ifcfg_num; i++) {
                    STRSCPY(stats_event.ifname, client->ifcfg[i].ifname);
                    bm_stats_add_event_to_report( client, &stats_event, CLIENT_STEERING_FAILED, false );
                }
            } else if( client->cs_mode == BM_CLIENT_CS_MODE_HOME ) {
                 for (i = 0; i < client->ifcfg_num; i++) {
                     if (client->ifcfg[i].radio_type != client->cs_radio_type)
                         continue;

                     STRSCPY(stats_event.ifname, client->ifcfg[i].ifname);
                     bm_stats_add_event_to_report( client, &stats_event, CLIENT_STEERING_FAILED, false );
                }
            }
        } else {
            LOGN("'%s' (total: %u times), %s...",
                    client->mac_addr,
                    stats->steering_fail_cnt,
                    client->backoff_period ?
                    "backing off" : "disabling");

            if (client->backoff_period) {
                bm_client_backoff(client, true);
                for (i = 0; i < client->ifcfg_num; i++) {
                    if (client->ifcfg[i].bs_allowed)
                        continue;

                    memset( &stats_event, 0, sizeof( stats_event ) );
                    STRSCPY(stats_event.ifname, client->ifcfg[i].ifname);
                    bm_stats_add_event_to_report(client, &stats_event, BACKOFF, true);
                }
            }
            else {
                bm_client_disable_steering(client);
            }

        }
    }

    return;
}

void
bm_client_success(bm_client_t *client, const char *ifname)
{
    radio_type_t            radio_type = bm_group_find_radio_type_by_ifname(ifname);
    char                    *bandstr = c_get_str_by_key(map_bsal_bands, radio_type);
    bm_client_stats_t       *stats;

    stats = bm_client_get_stats(client, ifname);
    if (WARN_ON(!stats))
        return;

    stats->steering_success_cnt++;
    LOGN("'%s' successfully steered to %s %s (total: %u times)",
                        client->mac_addr, ifname, bandstr, stats->steering_success_cnt);

    return;
}

void
bm_client_preassoc_backoff_recalc(bm_group_t *group, bm_client_t *client, const char *ifname)
{
    radio_type_t radio_type;
    char *bandstr;
    bool bs_ifname_allowed;

    radio_type = bm_group_find_radio_type_by_ifname(ifname);
    bandstr = c_get_str_by_key(map_bsal_bands, radio_type);
    bs_ifname_allowed = bm_client_bs_ifname_allowed(client, ifname);

    if (client->backoff && !bs_ifname_allowed && !client->backoff_connect_calculated) {
        client->backoff_connect_counter++;
        client->backoff_connect_calculated = true;
        LOGI("[%s] %s recalc pre-assoc backoff - counter %d", bandstr, ifname,
             client->backoff_connect_counter);
    }

    if (bs_ifname_allowed) {
        LOGI("[%s] %s connected to bs_allowed (%s) - back to orignal pre-assoc backoff timeout", bandstr, client->mac_addr, ifname);
        client->backoff_connect_counter = 0;
    }
}

static void
bm_client_postassoc_backoff_recalc(bm_group_t *group, bm_client_t *client, const char *ifname)
{
    time_t now;

    now = time(NULL);

    if (now - client->times.last_sticky_kick < client->sticky_kick_guard_time) {
        LOGT("%s back early after sticky kick (%lds)", client->mac_addr, now - client->times.last_sticky_kick);
        if (group->gw_only || !bm_neighbor_number(client)) {
            if (bm_client_bs_ifname_allowed(client, ifname)) {
                LOGI("%s (gw_only) connected back to %s skip sticky kick for %ds",
                     client->mac_addr, ifname, client->sticky_kick_backoff_time);
                client->skip_sticky_kick_till = now + client->sticky_kick_backoff_time;
            }
        } else {
            LOGI("%s (multi AP) connected back to %s skip sticky kick for %ds",
                 client->mac_addr, ifname, client->sticky_kick_backoff_time);
            client->skip_sticky_kick_till = now + client->sticky_kick_backoff_time;
        }
    }

    if (now - client->times.last_steering_kick < client->steering_kick_guard_time) {
        LOGT("%s back early after steering kick (%lds)", client->mac_addr, now - client->times.last_steering_kick);
        if (!bm_client_bs_ifname_allowed(client, ifname)) {
            LOGI("%s connected back to %s skip steering kick for %ds",
                 client->mac_addr, ifname, client->steering_kick_backoff_time);
            client->skip_steering_kick_till = now + client->steering_kick_backoff_time;
        }
    }
}

static void
bm_client_settling_backoff_recalc(bm_group_t *group, bm_client_t *client, const char *ifname)
{
    time_t now;

    now = time(NULL);

    if (!client->settling_backoff_time)
        return;

    LOGI("%s settling backoff recalc, skip xing for %d", client->mac_addr, client->settling_backoff_time);
    client->settling_skip_xing_till = now + client->settling_backoff_time;
}

void
bm_client_bs_connect(bm_group_t *group, bm_client_t *client, const char *ifname)
{
    bm_client_stats_t *stats;
    time_t last_blocked = 0;
    time_t now;
    radio_type_t radio_type;
    char *bandstr;
    unsigned int i;

    now = time(NULL);
    radio_type = bm_group_find_radio_type_by_ifname(ifname);
    bandstr = c_get_str_by_key(map_bsal_bands, radio_type);

    if (!group) {
        LOGD("%s: client %s ifname %s band %s !group", __func__, client->mac_addr, ifname, bandstr);
        return;
    }

    bm_client_settling_backoff_recalc(group, client, ifname);
    bm_client_postassoc_backoff_recalc(group, client, ifname);

    if (!bm_client_bs_ifname_allowed(client, ifname)) {
        LOGD("%s: client %s ifname %s band %s !bs_allowed", __func__, client->mac_addr, ifname, bandstr);
        return;
    }

    if (client->state != BM_CLIENT_STATE_STEERING) {
        LOGD("%s: client %s ifname %s band %s !steering", __func__, client->mac_addr, ifname, bandstr);
        return;
    }

    /* Find last blocked inside the group */
    for (i = 0; i < group->ifcfg_num; i++) {
        if (group->ifcfg[i].bs_allowed)
            continue;
        stats = bm_client_get_stats(client, group->ifcfg[i].ifname);
        if (WARN_ON(!stats))
            continue;
        last_blocked = MAX(last_blocked, stats->probe.last_blocked);
    }

    if ((now - last_blocked) <= group->success_threshold) {
        bm_client_success(client, ifname);
    }
}

void
bm_client_cs_connect(bm_client_t *client, const char *ifname)
{
    radio_type_t    radio_type = bm_group_find_radio_type_by_ifname(ifname);;
    char            *bandstr = c_get_str_by_key(map_bsal_bands, radio_type);
    bsal_event_t    event;

    memset( &event, 0, sizeof( event ) );

    if( client->cs_mode == BM_CLIENT_CS_MODE_HOME ) {
        LOGN( "Client steering successful for '%s' and %s band", client->mac_addr, bandstr );

        // Should this be success state?
        client->cs_state = BM_CLIENT_CS_STATE_EXPIRED;

        // NB: No need to send a CLIENT_STEERING_EXPIRED event here. When a client connects
        //     to the pod in the HOME mode, client steering is not stopped immediately and
        //     is stopped at the end of the enforecement period(when the CLIENT_STEERING_
        //     _EXPIRED event is sent automatically)
            
    } else if( client->cs_mode == BM_CLIENT_CS_MODE_AWAY ) {
        LOGN( "Client '%s' connected on %s band in AWAY mode", client->mac_addr, bandstr );

        client->cs_state = BM_CLIENT_CS_STATE_FAILED;

        STRSCPY(event.ifname, ifname);
        bm_stats_add_event_to_report( client, &event, CLIENT_STEERING_FAILED, false );
    }

    return;
}

bool
bm_client_update_cs_state( bm_client_t *client )
{
    struct      schema_Band_Steering_Clients bscli;
    c_item_t    *item;
    json_t      *js;
    char        *filter[] = { "+", SCHEMA_COLUMN( Band_Steering_Clients, cs_state ), NULL };

    // Reset the structure
    memset( &bscli, 0, sizeof( bscli ) );

    item = c_get_item_by_key( map_cs_states, client->cs_state );
    if( !item ) {
        LOGE( "Client '%s' - unknown CS state %d",
                            client->mac_addr, client->cs_state );
        return false;
    }

    STRSCPY(bscli.cs_state, (char *)item->data);
    bscli.cs_state_exists = true;

    js = schema_Band_Steering_Clients_to_json( &bscli, NULL );
    if( !js ) {
        LOGE( "Client '%s' failed to convert to schema", client->mac_addr );
        return false;
    }

    js = ovsdb_table_filter_row( js, filter );
    ovsdb_sync_update( SCHEMA_TABLE( Band_Steering_Clients ),
                       SCHEMA_COLUMN( Band_Steering_Clients, mac ),
                       client->mac_addr,
                       js );

    LOGT( "Client '%s' CS state updated to '%s'", client->mac_addr, (char *)item->data );

    return true;
}

void
bm_client_cs_check_rssi_xing( bm_client_t *client, bsal_event_t *event )
{
    bool                    rssi_xing = false;
    radio_type_t            radio_type;
    char                    *bandstr;
    evsched_task_t          task;

    radio_type = bm_group_find_radio_type_by_ifname(event->ifname);
    bandstr  = c_get_str_by_key(map_bsal_bands, radio_type);

    // This function should be called only when BSAL_EVENT_PROBE_REQ
    // is received.
    if( event->type != BSAL_EVENT_PROBE_REQ ) {
        return;
    }

    if( client->steering_state != BM_CLIENT_CLIENT_STEERING ) {
        LOGT( "Client '%s' not in 'client steering' mode", client->mac_addr );
        return;
    }

    if( client->cs_mode != BM_CLIENT_CS_MODE_AWAY ) {
        LOGT( "Client '%s' not in client steering 'AWAY' mode", client->mac_addr );
        return;
    }

    LOGT( "[%s] %s: RSSI: %2u hwm:%2u lwm:%2u", bandstr, client->mac_addr,
                                                event->data.probe_req.rssi,
                                                client->cs_hwm, client->cs_lwm );

    if( event->data.probe_req.rssi < BM_CLIENT_ROGUE_SNR_LEVEL ) {
        LOGD( "Client '%s' sent probe_req below acceptable signal"
              " strength, ignoring...", client->mac_addr );
        return;
    }

    if( client->cs_hwm && event->data.probe_req.rssi > client->cs_hwm ) {

        // client performed a high_xing higher;
        LOGT( "Client '%s' crossed HWM while in away mode", client->mac_addr );

        // Don't update the client->cs_state if its already in the required
        // state. Update it only if going from steering --> xing_high or
        // xing_low. Hopefully, the client does not go from xing_high to
        // xing_low or vice versa suddenly.
        if( client->cs_state != BM_CLIENT_CS_STATE_XING_HIGH ) {
            client->cs_state = BM_CLIENT_CS_STATE_XING_HIGH;
            rssi_xing = true;
        }

    } else if( client->cs_lwm && event->data.probe_req.rssi < client->cs_lwm ) {

        // client performed a low_xing lower;
        LOGT( "Client '%s' crossed LWM while in away mode", client->mac_addr );

        // Same logic as xing_high
        if( client->cs_state != BM_CLIENT_CS_STATE_XING_LOW ) {
            client->cs_state = BM_CLIENT_CS_STATE_XING_LOW;
            rssi_xing = true;
        }

    } else {

        // The client did not do any crossing.  
        LOGT( "Client '%s' within RSSI range, cancelling timer if running",
                                                        client->mac_addr );
        evsched_task_cancel_by_find( bm_client_cs_task_rssi_xing, client,
                                   ( EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG ) );
        return;
    }

    if( rssi_xing ) {
        task = evsched_task_find( bm_client_cs_task_rssi_xing, client,
                                ( EVSCHED_FIND_BY_FUNC | EVSCHED_FIND_BY_ARG ) );

        if( !task ) {
            LOGT( "Client '%s' has no rssi_xing task, firing one", client->mac_addr );
            client->rssi_xing_task = evsched_task( bm_client_cs_task_rssi_xing, client,
                                                   EVSCHED_SEC( BM_CLIENT_RSSI_HYSTERESIS ) );
        }
    }

    return;
}

bm_client_reject_t
bm_client_get_reject_detection( bm_client_t *client )
{
    bm_client_reject_t  reject_detection = BM_CLIENT_REJECT_NONE;

    if( !client ) {
        return reject_detection;
    }

    if( client->steering_state == BM_CLIENT_CLIENT_STEERING ) {
        reject_detection = client->cs_reject_detection;
    } else {
        reject_detection = client->reject_detection;
    }

    return reject_detection;
}

ds_tree_t *
bm_client_get_tree(void)
{
    return &bm_clients;
}

bm_client_t *
bm_client_find_by_uuid(const char *uuid)
{
    bm_client_t       *client;

    ds_tree_foreach(&bm_clients, client) {
        if (!strcmp(client->uuid, uuid)) {
            return client;
        }
    }

    return NULL;
}

bm_client_t *
bm_client_find_by_macstr(char *mac_str)
{
    return (bm_client_t *)ds_tree_find(&bm_clients, (char *)mac_str);
}

bm_client_t *
bm_client_find_by_macaddr(os_macaddr_t mac_addr)
{
    char              mac_str[MAC_STR_LEN];

    sprintf(mac_str, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, mac_addr));
    return bm_client_find_by_macstr(mac_str);
}

bm_client_t *
bm_client_find_or_add_by_macaddr(os_macaddr_t *mac_addr)
{
    bm_client_t *client = bm_client_find_by_macaddr(*mac_addr);
    if (client) return client;
    // add new
    char mac_str[MAC_STR_LEN];
    sprintf(mac_str, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, *mac_addr));
    client = calloc(1, sizeof(*client));
    STRSCPY(client->mac_addr, mac_str);
    if (!os_nif_macaddr_from_str(&client->macaddr, mac_str)) {
        LOGE("%s Failed to parse mac address '%s'", __func__, mac_str);
        free(client);
        return NULL;
    }
    ds_tree_insert(&bm_clients, client, client->mac_addr);
    LOGN("Added client %s", client->mac_addr);
    return client;
}

bool
bm_client_add_all_to_group(bm_group_t *group)
{
    bm_client_t     *client;
    bool            success = true;

    ds_tree_foreach(&bm_clients, client) {
        if (bm_client_add_to_group(client, group) == false) {
            success = false;
        }
    }

    return success;
}

bool
bm_client_update_all_from_group(bm_group_t *group)
{
    bm_client_t     *client;
    bool            success = true;

    ds_tree_foreach(&bm_clients, client) {
        if (bm_client_update_group(client, group) == false) {
            success = false;
        }
    }

    return success;
}

bool
bm_client_remove_all_from_group(bm_group_t *group)
{
    bm_client_t     *client;
    bool            success = true;

    ds_tree_foreach(&bm_clients, client) {
        if (bm_client_remove_from_group(client, group) == false) {
            success = false;
        }
    }

    return success;
}

void bm_client_ifcfg_clean(bm_client_t *client)
{
    memset(client->ifcfg, 0, sizeof(client->ifcfg));
    LOGD("%s clean ifcfg[]", client->mac_addr);
}

static bool
bm_client_ifcfg_add(bm_group_t *group, bm_client_t *client,
                    const char *ifname, radio_type_t radio_type,
                    bool bs_allowed, bsal_client_config_t *conf)
{
    bm_client_ifcfg_t *ifcfg;
    unsigned int i;

    if (WARN_ON(!client))
        return false;

    if (WARN_ON(client->ifcfg_num >= ARRAY_SIZE(client->ifcfg)))
        return false;

    /* ifname is unique - only one entry per ifname */
    for (i = 0; i < client->ifcfg_num; i++) {
        if (WARN_ON(!strcmp(client->ifcfg[i].ifname, ifname)))
            return false;
    }

    ifcfg = &client->ifcfg[client->ifcfg_num];
    STRSCPY(ifcfg->ifname, ifname);
    ifcfg->radio_type= radio_type;
    ifcfg->bs_allowed = bs_allowed;
    ifcfg->group = group;
    memcpy(&ifcfg->conf, conf, sizeof(*conf));

    LOGD("%s: add ifcfg[%d]: ifname %s band %d allowed %d", client->mac_addr,
         client->ifcfg_num, ifcfg->ifname, ifcfg->radio_type, ifcfg->bs_allowed);

    client->ifcfg_num++;

    return true;
}

bool
bm_client_ifcfg_set(bm_group_t *group, bm_client_t *client,
                    const char *ifname, radio_type_t radio_type,
                    bool bs_allowed, bsal_client_config_t *conf)
{
    unsigned int i;

    if (WARN_ON(!client))
        return false;

    for (i = 0; i < client->ifcfg_num; i++) {
        if (!strcmp(client->ifcfg[i].ifname, ifname)) {
		client->ifcfg[i].radio_type = radio_type;
		client->ifcfg[i].group = group;
		client->ifcfg[i].bs_allowed = bs_allowed;
                memcpy(&client->ifcfg[i].conf, conf, sizeof(*conf));
                LOGD("%s: update ifcfg[%d]: ifname %s band %d allowed %d", client->mac_addr,
                     i, client->ifcfg[i].ifname, client->ifcfg[i].radio_type,
                     client->ifcfg[i].bs_allowed);
		return true;
	}
    }

    return bm_client_ifcfg_add(group, client, ifname, radio_type, bs_allowed, conf);
}

bool
bm_client_ifcfg_remove(bm_client_t *client, const char *ifname)
{
    unsigned int i, j;

    if (WARN_ON(!client))
        return false;

    for (i = 0, j = 0; i < client->ifcfg_num; i++) {
       if (i != j) {
           memcpy(&client->ifcfg[j], &client->ifcfg[i], sizeof(client->ifcfg[0]));
       }

       if (!strcmp(client->ifcfg[i].ifname, ifname))
           j++;
    }

    client->ifcfg_num = j;

    LOGD("%s: remove ifcfg[]: ifname %s", client->mac_addr, ifname);
    return true;
}

bool
bm_client_bs_ifname_allowed(bm_client_t *client, const char *ifname)
{
    unsigned int i;

    if (WARN_ON(!client))
        return false;

    for (i = 0; i < client->ifcfg_num; i++) {
        if (!strcmp(client->ifcfg[i].ifname, ifname))
            return client->ifcfg[i].bs_allowed;
    }

    LOGW("%s %s %s not found", __func__, client->mac_addr, ifname);;
    return false;
}

void
bm_client_add_dbg_event(bm_client_t *client,
                        const char *ifname,
                        dpp_bs_client_event_record_t *dpp_event)
{
    bm_event_stat_t *event;

    if (client->d_events_idx >= ARRAY_SIZE(client->d_events))
        client->d_events_idx = 0;

    event = &client->d_events[client->d_events_idx];
    memset(event, 0, sizeof(*event));
    memcpy(&event->dpp_event, dpp_event, sizeof(event->dpp_event));
    event->dpp_event_set = true;
    STRSCPY_WARN(event->ifname, ifname);

    client->d_events_idx++;
}

static void
bm_client_dump_dbg_event(const bm_event_stat_t *events, unsigned int idx)
{
    const bm_event_stat_t *event;
    const dpp_bs_client_event_record_t *dpp_event;
    char extra_buf[1024] = {};

    event = &events[idx];
    dpp_event = &event->dpp_event;

    if (!event->dpp_event_set)
        return;

    switch (dpp_event->type) {
    case PROBE:
        snprintf(extra_buf, sizeof(extra_buf), "snr %d bcast %d blocked %d",
                 dpp_event->rssi, dpp_event->probe_bcast, dpp_event->probe_blocked);
        break;
    case DISCONNECT:
        snprintf(extra_buf, sizeof(extra_buf), "src %d type %d reason %d",
                 dpp_event->disconnect_src, dpp_event->disconnect_type,
                 dpp_event->disconnect_reason);
        break;
    case ACTIVITY:
        snprintf(extra_buf, sizeof(extra_buf), "active %d",
                 dpp_event->active);
        break;
    case BACKOFF:
        snprintf(extra_buf, sizeof(extra_buf), "enabled %d period %d",
                 dpp_event->backoff_enabled, dpp_event->backoff_period);
        break;
    case AUTH_BLOCK:
        snprintf(extra_buf, sizeof(extra_buf), "rejected %d",
                 dpp_event->rejected);
        break;
    case CLIENT_CAPABILITIES:
        snprintf(extra_buf, sizeof(extra_buf), "btm %d rrm %d 2G %d 5G %d chwidth %d nss %d phy_mode %d max_mcs %d link_meas %d neigh_rep %d bcn_rpt_passive %d bcn_rpt_active %d bcn_rpt_table %d lci_meas %d ftm_range_rpt %d assoc_ie_len %zu",
                 dpp_event->is_BTM_supported, dpp_event->is_RRM_supported, dpp_event->band_cap_2G, dpp_event->band_cap_5G,
                 dpp_event->max_chwidth, dpp_event->max_streams, dpp_event->phy_mode, dpp_event->max_MCS,
                 dpp_event->rrm_caps_link_meas, dpp_event->rrm_caps_neigh_rpt, dpp_event->rrm_caps_bcn_rpt_passive,
                 dpp_event->rrm_caps_bcn_rpt_active, dpp_event->rrm_caps_bcn_rpt_table, dpp_event->rrm_caps_lci_meas,
                 dpp_event->rrm_caps_ftm_range_rpt, dpp_event->assoc_ies_len);
        break;
    case CLIENT_BTM_STATUS:
        snprintf(extra_buf, sizeof(extra_buf), "status %u",
                 dpp_event->btm_status);
        break;
    default:
        break;
    }

    LOGI("ev[%03d]: %" PRIu64 " %s %s %s", idx, dpp_event->timestamp_ms,
         bm_stats_get_event_to_str(dpp_event->type), event->ifname, extra_buf);
}

static void
bm_client_dump(bm_client_t *client)
{
    unsigned int idx, i;
    bm_rrm_neighbor_t *rrm_n;
    time_t now;

    idx = client->d_events_idx;
    if (idx >= ARRAY_SIZE(client->d_events))
       idx = 0;

    for (i = 0; i < ARRAY_SIZE(client->d_events); i++) {
        bm_client_dump_dbg_event(client->d_events, idx);

        idx++;
        if (idx >= ARRAY_SIZE(client->d_events))
            idx = 0;
    }

    now = time(NULL);

    /* Dump rrm_neighbors */
    LOGI("rrm_neighbors:");
    for (i = 0; i < client->rrm_neighbor_num; i++) {
        rrm_n = &client->rrm_neighbor[i];
        LOGI("[%d]: bssid %s chanel %u rcpi %u rssi %i age %u", i, rrm_n->bssid_str,
             rrm_n->channel, rrm_n->rcpi, rrm_n->rssi, (unsigned int) (now - rrm_n->time));
    }
}

void
bm_client_dump_dbg_events(void)
{
    bm_client_t *client;

    ds_tree_foreach(&bm_clients, client) {
        LOGI("---- %s ----", client->mac_addr);
        bm_client_dump(client);
    }
}

void
bm_client_reset_last_probe_snr(bm_client_t *client)
{
    bm_client_stats_t *stats;
    unsigned int i;

    for (i = 0; i < client->ifcfg_num; i++) {
        stats = bm_client_get_stats(client, client->ifcfg[i].ifname);
        if (stats)
            stats->probe.last_snr = 0;
    }
}

static int
bm_rcpi_to_rssi(unsigned char rcpi)
{
    /* Table 9-154 RCPI values */
    if (rcpi > 219)
        /* P >= 0dBm, resered, measurement not available */
        return 0;

    return rcpi/2 - 110;
}

void
bm_client_reset_rrm_neighbors(bm_client_t *client)
{
    time_t now;
    unsigned int i;
    unsigned int j;

    now = time(NULL);

    for (i = 0, j = 0; i < client->rrm_neighbor_num; i++) {
       if (i != j) {
           memcpy(&client->rrm_neighbor[j], &client->rrm_neighbor[i], sizeof(client->rrm_neighbor[0]));
       }

       if (now - client->rrm_neighbor[i].time < 120)
           j++;
       else
           LOGD("%s remove rrm_neighbor[%u] %s age %u", client->mac_addr, i,
                client->rrm_neighbor[i].bssid_str, (unsigned int) (now - client->rrm_neighbor[i].time));
    }

    client->rrm_neighbor_num = j;
}

bool
bm_client_set_rrm_neighbor(bm_client_t *client,
                           const unsigned char *bssid,
                           unsigned char channel,
                           unsigned char rcpi,
                           unsigned char rsni)
{
    bm_rrm_neighbor_t *rrm_neighbor;
    unsigned int i;

    LOGD("%s %s", client->mac_addr, __func__);

    if (!bm_neighbor_is_our_bssid(client, bssid))
        return true;

    for (i = 0; i < client->rrm_neighbor_num; i++) {
        rrm_neighbor = &client->rrm_neighbor[i];
        if (!memcmp(&rrm_neighbor->bssid, bssid, sizeof(rrm_neighbor->bssid)))
            break;
    }

    if (WARN_ON(client->rrm_neighbor_num >= ARRAY_SIZE(client->rrm_neighbor)))
        return false;

    rrm_neighbor = &client->rrm_neighbor[i];
    /* Check if new entry */
    if (i == client->rrm_neighbor_num)
        client->rrm_neighbor_num++;

    memcpy(&rrm_neighbor->bssid, bssid, sizeof(rrm_neighbor->bssid));
    sprintf(rrm_neighbor->bssid_str, PRI(os_macaddr_lower_t), FMT(os_macaddr_t, *(os_macaddr_t *)bssid));
    rrm_neighbor->rcpi = rcpi;
    rrm_neighbor->rsni = rsni;
    rrm_neighbor->channel = channel;
    rrm_neighbor->rssi = bm_rcpi_to_rssi(rcpi);
    rrm_neighbor->time = time(NULL);

    LOGI("%s set rrm_neighbor[%u] bssid %s channel %u rcpi %u rssi %d",
         client->mac_addr, i, rrm_neighbor->bssid_str,
         rrm_neighbor->channel, rrm_neighbor->rcpi,
         rrm_neighbor->rssi);

    return true;
}

/* Implementation for client active/inactive detection */
static void
bm_client_activity_action(bm_client_t *client, bool is_active)
{
    bm_client_stats_t *stats;
    bm_client_times_t *times;
    time_t now;
    bsal_event_t event;

    LOGI("%s: %s report %s", client->ifname, client->mac_addr, is_active ? "ACTIVE" : "INACTIVE");

    client->is_active = is_active;

    times = &client->times;
    stats = bm_client_get_stats(client, client->ifname);
    if (WARN_ON(!stats))
        return;

    now = time(NULL);
    times->last_activity_change = now;
    stats->activity_changes++;

    if (!client->is_active && client->kick_info.kick_pending) {
        LOGN("Client '%s': Issuing pending steering request", client->mac_addr);
        bm_kick(client, client->kick_info.kick_type, client->kick_info.rssi);

        client->kick_info.rssi         = 0;
        client->kick_info.kick_pending = false;
    }

    if (client->is_active)
       client->is_active_time = now;

    memset(&event, 0, sizeof(event));
    STRSCPY(event.ifname, client->ifname);
    event.type = BSAL_EVENT_CLIENT_ACTIVITY;
    event.data.activity.active = is_active;
    memcpy(&event.data.activity.client_addr,
           &client->macaddr,
           sizeof(event.data.activity.client_addr));

    bm_stats_add_event_to_report(client, &event, ACTIVITY, false);
}

static void
bm_client_activity_recalc(bm_client_t *client, bsal_client_info_t *info)
{
    uint64_t old_bytes;
    uint64_t new_bytes;
    uint64_t current_bps;
    int inact_tmout_sec_normal;
    bool is_active = false;
    time_t now;

    old_bytes = client->tx_bytes + client->rx_bytes;
    new_bytes = info->tx_bytes + info->rx_bytes;
    now = time(NULL);

    inact_tmout_sec_normal = client->group ? client->group->inact_tmout_sec_normal : 60;

    if (!client->tx_bytes && !client->rx_bytes && !info->tx_bytes && !info->rx_bytes)
        LOGI("%s: %s lack of tx/rx bytes - target layer issue?", client->ifname, client->mac_addr);

    if (!(now - client->bytes_report_time))
        return;

    current_bps = ((new_bytes - old_bytes) * 8) / (now - client->bytes_report_time);

    LOGD("%s: %s snr %u tx_bytes %" PRIu64 " rx_bytes %" PRIu64 " bps %" PRIu64, client->ifname, client->mac_addr, info->snr, info->tx_bytes, info->rx_bytes, current_bps);

    if (current_bps > client->active_treshold_bps) {
       is_active = true;
       client->is_active_time = now;
    }

    if (!is_active) {
        LOGD("%s: %s inactive for %u seconds", client->ifname, client->mac_addr, (unsigned int) (now - client->is_active_time));
        if ((int) (now - client->is_active_time) < inact_tmout_sec_normal)
            goto update_bytes;
    }

    if (client->is_active != is_active)
        bm_client_activity_action(client, is_active);

update_bytes:
    client->tx_bytes = info->tx_bytes;
    client->rx_bytes = info->rx_bytes;
    client->bytes_report_time = now;
}

/* handle client xing */
static bsal_rssi_change_t
bm_client_recalc_rssi_change(uint8_t snr, uint8_t watermark)
{
    return (snr < watermark ? BSAL_RSSI_LOWER : BSAL_RSSI_HIGHER);
}

static void
bm_client_xing_recalc(bm_client_t *client, bsal_client_info_t *info)
{
    bm_client_ifcfg_t *ifcfg;
    bsal_rssi_change_t new_xing_low;
    bsal_rssi_change_t new_xing_high;
    bsal_rssi_change_t prev_xing_low;
    bsal_rssi_change_t prev_xing_high;
    uint8_t xing_lwm, xing_hwm;
    bsal_event_t event;
    uint8_t new_snr;
    uint8_t prev_snr;

    ifcfg = bm_client_get_ifcfg(client, client->ifname);
    if (WARN_ON(!ifcfg))
        return;

    new_snr = info->snr;
    prev_snr = client->prev_xing_snr;

    xing_lwm = ifcfg->conf.rssi_low_xing;
    xing_hwm = ifcfg->conf.rssi_high_xing ;

    new_xing_low = bm_client_recalc_rssi_change(new_snr, xing_lwm);
    new_xing_high = bm_client_recalc_rssi_change(new_snr, xing_hwm);
    prev_xing_low = bm_client_recalc_rssi_change(prev_snr, xing_lwm);
    prev_xing_high = bm_client_recalc_rssi_change(prev_snr, xing_hwm);

    LOGD("%s new_snr %u old_snr %u lwm %u hwm %u low %d->%d high %d->%d",
         client->mac_addr, new_snr, prev_snr, xing_lwm, xing_hwm,
         prev_xing_low, new_xing_low,
         prev_xing_high, new_xing_high);

    if (new_xing_low == prev_xing_low &&
        new_xing_high == prev_xing_high)
        return;

    memset(&event, 0, sizeof(event));
    STRSCPY(event.ifname, client->ifname);
    event.data.rssi_change.rssi = new_snr;

    if (new_xing_low == prev_xing_low)
        event.data.rssi_change.low_xing = BSAL_RSSI_UNCHANGED;
    else
        event.data.rssi_change.low_xing = new_xing_low;

    if (new_xing_high == prev_xing_high)
        event.data.rssi_change.high_xing = BSAL_RSSI_UNCHANGED;
    else
        event.data.rssi_change.high_xing = new_xing_high;

    /* While we don't know prev_snr we don't know xing */
    if (!prev_snr) {
        event.data.rssi_change.low_xing = BSAL_RSSI_UNCHANGED;
        event.data.rssi_change.high_xing = BSAL_RSSI_UNCHANGED;
    }

    /* Save this to know (internal) prev state */
    client->prev_xing_snr = new_snr;

    bm_events_handle_rssi_xing(client, &event);
}

void bm_client_sta_info_update_callback(void)
{
    bsal_client_info_t info;
    bm_client_t *client;

    ds_tree_foreach(&bm_clients, client) {
        /* Skip not connected clients */
        if (!client->connected)
            continue;

        if (target_bsal_client_info(client->ifname, client->macaddr.addr, &info)) {
            LOGI("%s: %s no client info.", client->ifname, client->mac_addr);
            continue;
        }

        bm_client_activity_recalc(client, &info);
        bm_client_xing_recalc(client, &info);
    }
}

void bm_client_handle_ext_activity(bm_client_t *client, const char *ifname, bool active)
{
    bsal_client_info_t info;

    if (!client->connected) {
        LOGD("%s %s skip ext activity (%d) while client not connected", ifname, client->mac_addr, active);
        return;
    }

    if (target_bsal_client_info(client->ifname, client->macaddr.addr, &info)) {
        LOGI("%s: %s no client info, base on external activity event", client->ifname, client->mac_addr);
        if (client->is_active != active)
            bm_client_activity_action(client, active);
        return;
    }

    bm_client_activity_recalc(client, &info);
}

void bm_client_handle_ext_xing(bm_client_t *client, const char *ifname, bsal_event_t *event)
{
    bsal_client_info_t info;

    if (!client->connected) {
        LOGD("%s %s skip ext xing while client not connected", ifname, client->mac_addr);
        return;
    }

    if (target_bsal_client_info(client->ifname, client->macaddr.addr, &info)) {
        LOGI("%s: %s no client info, base on external xing event, snr %u", client->ifname, client->mac_addr, event->data.rssi_change.rssi);
        info.snr = event->data.rssi_change.rssi;
    }

    bm_client_xing_recalc(client, &info);
}
