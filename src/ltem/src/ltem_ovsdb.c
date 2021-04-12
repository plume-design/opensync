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

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "log.h"
#include "os.h"
#include "qm_conn.h"
#include "dppline.h"
#include "network_metadata.h"
#include "ltem_mgr.h"

/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB

// ovsdb_table_t table_Connection_Manager_Uplink;
// struct schema_Connection_Manager_Uplink con;


#define LTE_NODE_MODULE "lte"
//#define LTE_NODE_STATE_MEM_KEY "max_mem"

ovsdb_table_t table_Lte_Config;
ovsdb_table_t table_Lte_State;
ovsdb_table_t table_Wifi_Inet_Config;
ovsdb_table_t table_Wifi_Inet_State;
ovsdb_table_t table_Connection_Manager_Uplink;
ovsdb_table_t table_Wifi_Route_State;
ovsdb_table_t table_DHCP_leased_IP;

void
ltem_update_conf(struct schema_Lte_Config *lte_conf)
{
    ltem_mgr_t *mgr = ltem_get_mgr();
    lte_config_info_t *conf = mgr->lte_config_info;


    strncpy(conf->if_name, lte_conf->if_name, sizeof(conf->if_name));
    conf->lte_failover = lte_conf->lte_failover;
    conf->v4_enable = lte_conf->v4_enable;
    conf->v6_enable = lte_conf->v6_enable;
    conf->force_use_lte = lte_conf->force_use_lte;
    strncpy(conf->esim_download, lte_conf->esim_download, sizeof(conf->esim_download));
    strncpy(conf->esim_active, lte_conf->esim_active, sizeof(conf->esim_active));
    strncpy(conf->simcard_slot, lte_conf->simcard_slot, sizeof(conf->simcard_slot));
    conf->modem_enable = lte_conf->modem_enable;
}

int
ltem_ovsdb_cmu_create_lte(ltem_mgr_t *mgr)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    char *if_type = "eth";
    const char *if_name;
    int rc;

    MEMZERO(cm_conf);

    if_name = mgr->lte_config_info->if_name;

    if (!if_name[0])
    {
        LOGI("%s: invalid if_name[%s]", __func__, if_name);
        return -1;
    }

    rc = ovsdb_table_select_one(&table_Connection_Manager_Uplink,
                                SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name, &cm_conf);
    if (rc) return 0;

    LOG(INFO, "%s: Insert Connection_Manager_Uplink: if_name=[%s], if_type[%s]", __func__, if_name, if_type);
    cm_conf._partial_update = true;
    SCHEMA_SET_STR(cm_conf.if_name, if_name);
    SCHEMA_SET_STR(cm_conf.if_type, if_type);
    SCHEMA_SET_INT(cm_conf.has_L2, true);
    SCHEMA_SET_INT(cm_conf.has_L3, true);
    SCHEMA_SET_INT(cm_conf.priority, 2);
    if (!ovsdb_table_insert(&table_Connection_Manager_Uplink, &cm_conf))
    {
        LOG(ERR, "%s: Error Inserting Lte_Config", __func__);
        return -1;
    }

    return 0;
}

int
ltem_ovsdb_cmu_disable_lte(ltem_mgr_t *mgr)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2),
                       SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3),
                       SCHEMA_COLUMN(Connection_Manager_Uplink, priority),
                       NULL };
    const char *if_name = "wwan0";
    int res;

    res = ovsdb_table_select_one(&table_Connection_Manager_Uplink,
                                 SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name, &cm_conf);
    if (!res)
    {
        LOGI("%s: %s not found in Connection_Manager_Uplink", __func__, if_name);
        return 0;
    }

    MEMZERO(cm_conf);

    if_name = mgr->lte_config_info->if_name;
    if(!if_name[0]) return 0;

    LOGI("%s: update %s LTE CM settings", __func__, if_name);
    cm_conf.has_L2 = false;
    cm_conf.has_L3 = false;
    cm_conf.priority = 0;

    res = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                     ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                     &cm_conf, filter);
    if (!res)
    {
        LOGW("%s: Update %s CM table failed", __func__, if_name);
        return -1;
    }
    return 0;
}

void
ltem_ovsdb_cmu_check_lte (ltem_mgr_t *mgr)
{
    int ret;
    struct schema_Connection_Manager_Uplink uplink;

    ret = ovsdb_table_select_one(&table_Connection_Manager_Uplink,
                                 SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), mgr->lte_config_info->if_name, &uplink);
    if (!ret)
    {
        LOGI("%s: %s not found in Connection_Manager_Uplink", __func__, mgr->lte_config_info->if_name);
        return;
    }

    LOGI("%s: if_name=%s, if_type=%s, has_L2=%d, has_L3=%d, priority=%d",
         __func__, uplink.if_name, uplink.if_type, uplink.has_L2, uplink.has_L3, uplink.priority);
}

void
callback_Lte_Config(ovsdb_update_monitor_t *mon,
                    struct schema_Lte_Config *old_lte_conf,
                    struct schema_Lte_Config *lte_conf)
{
    ltem_mgr_t *mgr = ltem_get_mgr();

    LOGI("%s: if_name=%s, enable=%d, lte_failover=%d, v4_enable=%d, v6_enable=%d,"
         " force_use_lte=%d esim_download=%s, esim_active=%s, simcard_slot=%s, modem_enable=%d\n",
         __func__, lte_conf->if_name, lte_conf->enable, lte_conf->lte_failover, lte_conf->v4_enable,
         lte_conf->v6_enable, lte_conf->force_use_lte, lte_conf->esim_download,
         lte_conf->esim_active, lte_conf->simcard_slot, lte_conf->modem_enable);

    if (mon->mon_type != OVSDB_UPDATE_ERROR)
    {
        ltem_update_conf(lte_conf);
    }
    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: OVSDB_UPDATE_ERROR", __func__);
            return;
        case OVSDB_UPDATE_NEW:
            LOGI("%s mon_type = OVSDB_UPDATE_NEW", __func__);
            break;
        case OVSDB_UPDATE_MODIFY:
            LOGI("%s mon_type = OVSDB_UPDATE_MODIFY", __func__);
            if (!lte_conf->enable && !lte_conf->force_use_lte)
            {
                ltem_ovsdb_cmu_disable_lte(mgr);
            }
            if (lte_conf->force_use_lte)
            {
                ltem_set_wan_state(LTEM_WAN_STATE_DOWN);
            }
            else if (old_lte_conf->force_use_lte && !lte_conf->force_use_lte)
            {
                ltem_set_lte_state(LTEM_WAN_STATE_UP);
            }
            break;
        case OVSDB_UPDATE_DEL:
            LOGI("%s mon_type = OVSDB_UPDATE_DEL", __func__);
            ltem_set_lte_state(LTEM_LTE_STATE_DOWN);
            break;
    }
}

void
callback_Lte_State(ovsdb_update_monitor_t *mon,
                   struct schema_Lte_State *old_lte_state,
                   struct schema_Lte_State *lte_state)
{
    LOGI("%s mon_type = %d", __func__, mon->mon_type);
    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            break;
        case OVSDB_UPDATE_DEL:
            break;
    }
}

void
ltem_handle_nm_update_wan(struct schema_Wifi_Inet_State *old_inet_state,
                          struct schema_Wifi_Inet_State *inet_state)
{
    int i;
    char *dns = "dns";
    char *gw = "gateway";
    char key[64];
    char value[64];
    char *delim = " ";
    char *token;
    int dns_count = 0;
    ltem_mgr_t *mgr = ltem_get_mgr();

    for (i = 0; i < inet_state->dhcpc_len; i++)
    {
        STRSCPY(key, inet_state->dhcpc_keys[i]);
        LOGI("%s: key=%s", __func__, key);
        if (!strncmp(key, dns, strlen(dns)))
        {
            STRSCPY(value, inet_state->dhcpc[i]);
            LOGI("%s: key:%s value:%s", __func__, key, value);
            token = strtok(value, delim);
            while (token)
            {
                dns_count++;
                LOGI("%s: dns%d:%s", __func__, dns_count, token);
                if (dns_count == 1)
                {
                    strncpy(mgr->lte_route->wan_dns1, token, sizeof(mgr->lte_route->wan_dns1));
                }
                else if (dns_count == 2)
                {
                    strncpy(mgr->lte_route->wan_dns2, token, sizeof(mgr->lte_route->wan_dns2));
                }
                token = strtok(NULL, delim);
            }
        }
        else if (!strncmp(key, gw, strlen(gw)))
        {
            STRSCPY(value, inet_state->dhcpc[i]);
            LOGI("%s: key:%s value:%s", __func__, key, value);
            strncpy(mgr->lte_route->wan_gw, value, sizeof(mgr->lte_route->wan_gw));
        }

    }
    if (inet_state->dhcpc_len)
    {
        LOGI("%s: dhcpc_len: %d", __func__, inet_state->dhcpc_len);
        ltem_set_wan_state(LTEM_WAN_STATE_UP);
        return;
    }

    memset(mgr->lte_route->wan_gw, 0, sizeof(mgr->lte_route->wan_gw));
}

void
ltem_handle_nm_update_wwan0(struct schema_Wifi_Inet_State *old_inet_state,
                            struct schema_Wifi_Inet_State *inet_state)
{
    char *null_inet_addr = "0.0.0.0";
    int res;

    LOGI("%s: old: if_name=%s, enabled=%d inet_addr=%s, new: if_name=%s, enabled=%d, inet_addr=%s",
         __func__, old_inet_state->if_name, old_inet_state->enabled, old_inet_state->inet_addr, inet_state->if_name, inet_state->enabled, inet_state->inet_addr);
    res = strncmp(inet_state->inet_addr, null_inet_addr, strlen(inet_state->inet_addr));
    if (!res)
    {
        LOGI("%s: no ip addr: %s", __func__, inet_state->inet_addr);
        return;
    }

    LOGI("%s: new ip addr: %s", __func__, inet_state->inet_addr);
    ltem_set_lte_state(LTEM_LTE_STATE_UP);
}

void
callback_Wifi_Inet_Config(ovsdb_update_monitor_t *mon,
                         struct schema_Wifi_Inet_Config *old_inet_config,
                         struct schema_Wifi_Inet_Config *inet_config)
{
    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            break;

        case OVSDB_UPDATE_NEW:
            break;

        case OVSDB_UPDATE_MODIFY:
            break;
    }
}

void
callback_Wifi_Inet_State(ovsdb_update_monitor_t *mon,
                         struct schema_Wifi_Inet_State *old_inet_state,
                         struct schema_Wifi_Inet_State *inet_state)
{
    int rc;

    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            rc = strncmp(inet_state->if_name, "wwan0", strlen(inet_state->if_name));
            if (!rc) ltem_handle_nm_update_wwan0(old_inet_state, inet_state);

            rc = strncmp(inet_state->if_name, "eth0", strlen(inet_state->if_name));
            if (!rc) ltem_handle_nm_update_wan(old_inet_state, inet_state);

            rc = strncmp(inet_state->if_name, "eth1", strlen(inet_state->if_name));
            if (!rc) ltem_handle_nm_update_wan(old_inet_state, inet_state);

            break;
    }
}

void
ltem_handle_cm_update_wwan0(struct schema_Connection_Manager_Uplink *old_uplink,
                            struct schema_Connection_Manager_Uplink *uplink)
{
    LOGI("%s: if_name=%s if_type=%s, has_L2=%d, has_L3=%d, priority=%d, is_used=%d",
         __func__, uplink->if_name, uplink->if_type, uplink->has_L2, uplink->has_L3, uplink->priority, uplink->is_used);
}

void
callback_Connection_Manager_Uplink(ovsdb_update_monitor_t *mon,
                                   struct schema_Connection_Manager_Uplink *old_uplink,
                                   struct schema_Connection_Manager_Uplink *uplink)
{
    int res;

    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            res = strncmp(uplink->if_name, "wwan0", strlen(uplink->if_name));
            if (res == 0)
            {
                ltem_handle_cm_update_wwan0(old_uplink, uplink);
            }
            break;

        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            res = strncmp(uplink->if_name, "wwan0", strlen(uplink->if_name));
            if (res == 0)
            {
                ltem_handle_cm_update_wwan0(old_uplink, uplink);
            }
            break;
    }
}

void
ltem_handle_rs_update(struct schema_Wifi_Route_State *old_route_state, struct schema_Wifi_Route_State *route_state)
{
    ltem_mgr_t *mgr = ltem_get_mgr();

    LOGI("%s: if_name=%s, dest_addr=%s, dest_mask=%s", __func__, route_state->if_name, route_state->dest_addr, route_state->dest_mask);
    ltem_update_wan_subnet(mgr, route_state->dest_addr);
    ltem_update_wan_netmask(mgr, route_state->dest_mask);
}

void
callback_Wifi_Route_State(ovsdb_update_monitor_t *mon,
                         struct schema_Wifi_Route_State *old_route_state,
                         struct schema_Wifi_Route_State *route_state)
{
    int res;

    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            res = strncmp(route_state->if_name, "br-home", strlen(route_state->if_name));
            if (res == 0)
            {
                ltem_handle_rs_update(old_route_state, route_state);
            }
            break;
    }
}

void
callback_DHCP_leased_IP(ovsdb_update_monitor_t *mon,
                        struct schema_DHCP_leased_IP *old_lease,
                        struct schema_DHCP_leased_IP *new_lease)
{
    ltem_mgr_t *mgr = ltem_get_mgr();

    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            ltem_client_table_delete(mgr, old_lease);
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            ltem_client_table_update(mgr, new_lease); 
            break;
    }
}

int
ltem_ovsdb_init(void)
{
    LOGI("Initializing LTEM OVSDB tables");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(Lte_Config);
    OVSDB_TABLE_INIT_NO_KEY(Lte_State);
    OVSDB_TABLE_INIT(Wifi_Inet_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Inet_State, if_name);
    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);
    OVSDB_TABLE_INIT(Wifi_Route_State, if_name);
    OVSDB_TABLE_INIT_NO_KEY(DHCP_leased_IP);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Lte_Config, false);
    OVSDB_TABLE_MONITOR(Lte_State, false);
    OVSDB_TABLE_MONITOR(Wifi_Inet_Config, false);
    OVSDB_TABLE_MONITOR(Wifi_Inet_State, false);
    OVSDB_TABLE_MONITOR(Connection_Manager_Uplink, false);
    OVSDB_TABLE_MONITOR(Wifi_Route_State, false);
    OVSDB_TABLE_MONITOR(DHCP_leased_IP, false);

    return 0;
}

