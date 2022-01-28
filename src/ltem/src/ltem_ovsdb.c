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
#include "log.h"
#include "os.h"
#include "qm_conn.h"
#include "dppline.h"
#include "network_metadata.h"
#include "lte_info.h"
#include "ltem_mgr.h"

/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB


#define LTE_NODE_MODULE "lte"
//#define LTE_NODE_STATE_MEM_KEY "max_mem"

ovsdb_table_t table_Lte_Config;
ovsdb_table_t table_Lte_State;
ovsdb_table_t table_Wifi_Inet_Config;
ovsdb_table_t table_Wifi_Inet_State;
ovsdb_table_t table_Connection_Manager_Uplink;
ovsdb_table_t table_Wifi_Route_State;
ovsdb_table_t table_DHCP_leased_IP;
ovsdb_table_t table_AWLAN_Node;
ovsdb_table_t table_Wifi_Route_Config;

void
ltem_disable_band_40(ltem_mgr_t *mgr)
{
    char lte_bands[32];
    char u_bands_str[32];
    char l_bands_str[32];
    char *ptr;
    uint64_t upper_bands;
    uint64_t lower_bands;
    osn_lte_modem_info_t *modem_info;

    MEMZERO(lte_bands);
    MEMZERO(u_bands_str);
    MEMZERO(l_bands_str);

    modem_info = mgr->modem_info;
    STRSCPY(lte_bands, modem_info->lte_band_val);
    u_bands_str[0] = lte_bands[0]; /* If bands above 68 are ever used, this needs to change */
    strncpy(l_bands_str, &lte_bands[1], sizeof(l_bands_str));

    upper_bands = strtol(u_bands_str, &ptr, 16);
    lower_bands = strtol(l_bands_str, &ptr, 16);

    if (!(lower_bands & (1ULL << 39))) /* Band 40 is bit 39 */
    {
        LOGD("%s: bit 40 not set", __func__);
        return;
    }

    lower_bands &= ~(1ULL << 39); /* Band 40 is bit 39 */

    MEMZERO(lte_bands);
    sprintf(lte_bands, "%01llx%016llx", (long long unsigned int)upper_bands, (long long unsigned int)lower_bands);
    LOGD("%s: Setting LTE bands[%s]", __func__, lte_bands);
    osn_lte_set_bands(lte_bands);
}

int
ltem_update_conf(struct schema_Lte_Config *lte_conf)
{
    ltem_mgr_t *mgr = ltem_get_mgr();
    lte_config_info_t *conf = mgr->lte_config_info;

    if (conf == NULL) return -1;
    STRSCPY(conf->if_name, lte_conf->if_name);
    conf->manager_enable = lte_conf->manager_enable;
    conf->lte_failover_enable = lte_conf->lte_failover_enable;
    conf->ipv4_enable = lte_conf->ipv4_enable;
    conf->ipv6_enable = lte_conf->ipv6_enable;
    conf->force_use_lte = lte_conf->force_use_lte;
    conf->active_simcard_slot = lte_conf->active_simcard_slot;
    conf->modem_enable = lte_conf->modem_enable;
    conf->enable_persist = lte_conf->enable_persist;
    if (conf->modem_enable)
    {
        if (osn_lte_read_modem())
        {
            LOGE("%s: osn_lte_read_modem(): failed", __func__);
        }
        else
        {
            /* Disable band 40 */
            ltem_disable_band_40(mgr);
        }
    }

    if (lte_conf->report_interval == 0)
    {
        conf->report_interval = mgr->mqtt_interval;
    }
    else
    {
        mgr->mqtt_interval = conf->report_interval = lte_conf->report_interval;
    }
    LOGD("%s: report_interval[%d]", __func__, conf->report_interval);
    STRSCPY(conf->apn, lte_conf->apn);
    STRSCPY(conf->lte_bands, lte_conf->lte_bands_enable);

    return 0;
}

int
ltem_ovsdb_create_lte_state(ltem_mgr_t *mgr)
{
    struct schema_Lte_State lte_state;
    lte_config_info_t *lte_config;
    lte_state_info_t *lte_state_info;
    osn_lte_modem_info_t *modem_info;
    const char *if_name;
    char *sim_status;
    char *net_state;
    int rc;

    MEMZERO(lte_state);

    if (mgr == NULL) return -1;

    lte_config = mgr->lte_config_info;
    if (lte_config == NULL)
    {
        LOGE("%s: lte_config_info NULL", __func__);
        return -1;
    }
    if_name = lte_config->if_name;
    if (!if_name[0])
    {
        LOGI("%s: invalid if_name[%s]", __func__, if_name);
        return -1;
    }

    lte_state_info = mgr->lte_state_info;
    if (lte_state_info == NULL)
    {
        LOGE("%s: lte_state_info NULL", __func__);
        return -1;
    }

    rc = ovsdb_table_select_one(&table_Lte_State,
                                SCHEMA_COLUMN(Lte_State, if_name), if_name, &lte_state);
    if (rc) return 0;

    LOGI("%s: Insert Lte_State: if_name=[%s]", __func__, if_name);
    lte_state._partial_update = true;
    // Config from Lte_Config table
    SCHEMA_SET_STR(lte_state.if_name, if_name);
    SCHEMA_SET_INT(lte_state.manager_enable, lte_config->manager_enable);
    SCHEMA_SET_INT(lte_state.lte_failover_enable, lte_config->lte_failover_enable);
    SCHEMA_SET_INT(lte_state.ipv4_enable, lte_config->ipv4_enable);
    SCHEMA_SET_INT(lte_state.ipv6_enable, lte_config->ipv6_enable);
    SCHEMA_SET_INT(lte_state.force_use_lte, lte_config->force_use_lte);
    SCHEMA_SET_INT(lte_state.modem_enable, lte_config->modem_enable);
    SCHEMA_SET_INT(lte_state.active_simcard_slot, lte_config->active_simcard_slot);
    SCHEMA_SET_INT(lte_state.report_interval, lte_config->report_interval);
    SCHEMA_SET_STR(lte_state.apn, lte_config->apn);

    // State info
    modem_info = mgr->modem_info;
    SCHEMA_SET_INT(lte_state.modem_present, modem_info->modem_present);
    SCHEMA_SET_STR(lte_state.iccid, modem_info->iccid);
    SCHEMA_SET_STR(lte_state.imei, modem_info->imei);
    SCHEMA_SET_STR(lte_state.imsi, modem_info->imsi);
    switch(modem_info->sim_status)
    {
        case LTE_SIM_STATUS_REMOVED:
            sim_status = "Removed";
            break;
        case LTE_SIM_STATUS_INSERTED:
            sim_status = "Inserted";
            break;
        case LTE_SIM_STATUS_BAD:
            sim_status = "Bad";
            break;
        default:
            sim_status = "Unknown";
            break;
    }
    SCHEMA_SET_STR(lte_state.sim_status, sim_status);
    SCHEMA_SET_STR(lte_state.service_provider_name, modem_info->operator);
    SCHEMA_SET_INT(lte_state.mcc, modem_info->srv_cell.mcc);
    SCHEMA_SET_INT(lte_state.mnc, modem_info->srv_cell.mnc);
    SCHEMA_SET_INT(lte_state.tac, modem_info->srv_cell.tac);

    switch (modem_info->reg_status)
    {
        case LTE_NET_REG_STAT_NOTREG:
            net_state = "not_registered_not_searching";
            break;

        case LTE_NET_REG_STAT_REG:
            net_state = "registered_home_network";
            break;

        case LTE_NET_REG_STAT_SEARCH:
            net_state = "not_registered_searching";
            break;

        case LTE_NET_REG_STAT_DENIED:
            net_state = "registration_denied";
            break;

        case LTE_NET_REG_STAT_ROAMING:
            net_state = "registered_roaming";
            break;

        default:
            net_state = "unknown";
            break;
    }
    SCHEMA_SET_STR(lte_state.lte_net_state, net_state);

    SCHEMA_SET_STR(lte_state.lte_bands_enable, modem_info->lte_band_val);

    if (!ovsdb_table_insert(&table_Lte_State, &lte_state))
    {
        LOG(ERR, "%s: Error Inserting Lte_State", __func__);
        return -1;
    }

    return 0;
}
int
ltem_ovsdb_update_lte_state(ltem_mgr_t *mgr)
{
    struct schema_Lte_State lte_state;
    lte_config_info_t *lte_config;
    lte_state_info_t *lte_state_info;
    osn_lte_modem_info_t *modem_info;
    const char *if_name;
    char *sim_status;
    char *net_state;
    int res;
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Lte_State, manager_enable),
                       SCHEMA_COLUMN(Lte_State, lte_failover_enable),
                       SCHEMA_COLUMN(Lte_State, ipv4_enable),
                       SCHEMA_COLUMN(Lte_State, ipv6_enable),
                       SCHEMA_COLUMN(Lte_State, force_use_lte),
                       SCHEMA_COLUMN(Lte_State, active_simcard_slot),
                       SCHEMA_COLUMN(Lte_State, modem_enable),
                       SCHEMA_COLUMN(Lte_State, report_interval),
                       SCHEMA_COLUMN(Lte_State, apn),
                       SCHEMA_COLUMN(Lte_State, modem_present),
                       SCHEMA_COLUMN(Lte_State, iccid),
                       SCHEMA_COLUMN(Lte_State, imei),
                       SCHEMA_COLUMN(Lte_State, imsi),
                       SCHEMA_COLUMN(Lte_State, sim_status),
                       SCHEMA_COLUMN(Lte_State, service_provider_name),
                       SCHEMA_COLUMN(Lte_State, mcc),
                       SCHEMA_COLUMN(Lte_State, mnc),
                       SCHEMA_COLUMN(Lte_State, tac),
                       SCHEMA_COLUMN(Lte_State, lte_net_state),
                       SCHEMA_COLUMN(Lte_State, lte_bands_enable),
                       NULL };

    if (mgr == NULL) return -1;
    lte_config = mgr->lte_config_info;
    if (lte_config == NULL) return -1;
    lte_state_info = mgr->lte_state_info;
    if (lte_state_info == NULL) return -1;
    modem_info = mgr->modem_info;
    if (modem_info == NULL) return -1;
    if_name = lte_config->if_name;
    if (!if_name[0]) return -1;
    modem_info = osn_get_modem_info();

    res = ovsdb_table_select_one(&table_Lte_State,
                                 SCHEMA_COLUMN(Lte_State, if_name), if_name, &lte_state);
    if (!res)
    {
        LOGI("%s: %s not found in Lte_State", __func__, if_name);
        return -1;
    }

    MEMZERO(lte_state);

    if_name = mgr->lte_config_info->if_name;
    if(!if_name[0]) return -1;

    LOGD("%s: update %s Lte_State settings", __func__, if_name);
    lte_state._partial_update = true;
    // Config from Lte_Config table
    SCHEMA_SET_STR(lte_state.if_name, if_name);
    SCHEMA_SET_INT(lte_state.manager_enable, lte_config->manager_enable);
    SCHEMA_SET_INT(lte_state.lte_failover_enable, lte_config->lte_failover_enable);
    SCHEMA_SET_INT(lte_state.ipv4_enable, lte_config->ipv4_enable);
    SCHEMA_SET_INT(lte_state.ipv6_enable, lte_config->ipv6_enable);
    SCHEMA_SET_INT(lte_state.force_use_lte, lte_config->force_use_lte);
    SCHEMA_SET_INT(lte_state.modem_enable, lte_config->modem_enable);
    SCHEMA_SET_INT(lte_state.active_simcard_slot, lte_config->active_simcard_slot);
    SCHEMA_SET_INT(lte_state.report_interval, lte_config->report_interval);
    SCHEMA_SET_STR(lte_state.apn, lte_config->apn);

    // State info
    SCHEMA_SET_INT(lte_state.modem_present, modem_info->modem_present);
    SCHEMA_SET_STR(lte_state.iccid, modem_info->iccid);
    SCHEMA_SET_STR(lte_state.imei, modem_info->imei);
    SCHEMA_SET_STR(lte_state.imsi, modem_info->imsi);
    switch(modem_info->sim_status)
    {
        case LTE_SIM_STATUS_REMOVED:
            sim_status = "Removed";
            break;
        case LTE_SIM_STATUS_INSERTED:
            sim_status = "Inserted";
            break;
        case LTE_SIM_STATUS_BAD:
            sim_status = "Bad";
            break;
        default:
            sim_status = "Unknown";
            break;
    }
    SCHEMA_SET_STR(lte_state.sim_status, sim_status);
    SCHEMA_SET_STR(lte_state.service_provider_name, modem_info->operator);
    SCHEMA_SET_INT(lte_state.mcc, modem_info->srv_cell.mcc);
    SCHEMA_SET_INT(lte_state.mnc, modem_info->srv_cell.mnc);
    SCHEMA_SET_INT(lte_state.tac, modem_info->srv_cell.tac);

    switch (modem_info->reg_status)
    {
        case LTE_NET_REG_STAT_NOTREG:
            net_state = "not_registered_not_searching";
            break;

        case LTE_NET_REG_STAT_REG:
            net_state = "registered_home_network";
            break;

        case LTE_NET_REG_STAT_SEARCH:
            net_state = "not_registered_searching";
            break;

        case LTE_NET_REG_STAT_DENIED:
            net_state = "registration_denied";
            break;

        case LTE_NET_REG_STAT_ROAMING:
            net_state = "registered_roaming";
            break;

        default:
            net_state = "unknown";
            break;
    }
    SCHEMA_SET_STR(lte_state.lte_net_state, net_state);

    SCHEMA_SET_STR(lte_state.lte_bands_enable, modem_info->lte_band_val);

    res = ovsdb_table_update_where_f(&table_Lte_State,
                                     ovsdb_where_simple(SCHEMA_COLUMN(Lte_State, if_name), if_name),
                                     &lte_state, filter);
    if (!res)
    {
        LOGW("%s: Update %s Lte_State table failed", __func__, if_name);
        return -1;
    }
    return 0;
}

int
ltem_ovsdb_cmu_update_lte(ltem_mgr_t *mgr)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2),
                       SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3),
                       SCHEMA_COLUMN(Connection_Manager_Uplink, priority),
                       NULL };
    const char *if_name;
    char *null_inet_addr = "0.0.0.0";
    int res;

    if_name = mgr->lte_config_info->if_name;
    if(!if_name[0])
    {
        LOGI("%s: if_name[%s]", __func__, if_name);
        return 0;
    }

    res = ovsdb_table_select_one(&table_Connection_Manager_Uplink,
                                 SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name, &cm_conf);
    if (!res)
    {
        LOGI("%s: %s not found in Connection_Manager_Uplink", __func__, if_name);
        return 0;
    }

    MEMZERO(cm_conf);

    LOGD("%s: update %s LTE CM settings", __func__, if_name);
    cm_conf._partial_update = true;
    SCHEMA_SET_INT(cm_conf.has_L2, true);
    SCHEMA_SET_INT(cm_conf.has_L3, false);
    res = strncmp(mgr->lte_route->lte_ip_addr, null_inet_addr, strlen(mgr->lte_route->lte_ip_addr));
    if (res)
    {
        SCHEMA_SET_INT(cm_conf.has_L3, true);
    }
    SCHEMA_SET_INT(cm_conf.priority, LTE_CMU_DEFAULT_PRIORITY);

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

int
ltem_ovsdb_cmu_insert_lte(ltem_mgr_t *mgr)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    char *if_type = "lte";
    const char *if_name;
    char *null_inet_addr = "0.0.0.0";
    int rc, res;

    MEMZERO(cm_conf);

    if_name = mgr->lte_config_info->if_name;

    if (!if_name[0])
    {
        LOGI("%s: invalid if_name[%s]", __func__, if_name);
        return -1;
    }

    rc = ovsdb_table_select_one(&table_Connection_Manager_Uplink,
                                SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name, &cm_conf);
    if (rc)
    {
        LOGI("%s: Entry for %s exists, update Connection_Manager_Uplink table", __func__, if_name);
        return ltem_ovsdb_cmu_update_lte(mgr);
    }

    LOGI("%s: Insert Connection_Manager_Uplink: if_name=[%s], if_type[%s]", __func__, if_name, if_type);
    cm_conf._partial_update = true;
    SCHEMA_SET_STR(cm_conf.if_name, if_name);
    SCHEMA_SET_STR(cm_conf.if_type, if_type);
    SCHEMA_SET_INT(cm_conf.has_L2, true);
    SCHEMA_SET_INT(cm_conf.has_L3, false);
    res = strncmp(mgr->lte_route->lte_ip_addr, null_inet_addr, strlen(mgr->lte_route->lte_ip_addr));
    if (res)
    {
        SCHEMA_SET_INT(cm_conf.has_L3, true);
    }
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

    LOGD("%s: update %s LTE CM settings", __func__, if_name);
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

int
ltem_ovsdb_cmu_update_lte_priority(ltem_mgr_t *mgr, uint32_t priority)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Connection_Manager_Uplink, priority),
                       NULL };
    const char *if_name;
    int res;

    if_name = mgr->lte_config_info->if_name;
    if(!if_name[0])
    {
        LOGI("%s: if_name[%s]", __func__, if_name);
        return 0;
    }

    res = ovsdb_table_select_one(&table_Connection_Manager_Uplink,
                                 SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name, &cm_conf);
    if (!res)
    {
        LOGI("%s: %s not found in Connection_Manager_Uplink", __func__, if_name);
        return 0;
    }

    MEMZERO(cm_conf);

    LOGD("%s: update %s LTE CM settings", __func__, if_name);
    cm_conf._partial_update = true;
    SCHEMA_SET_INT(cm_conf.priority, priority);

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

    LOGD("%s: if_name=%s, if_type=%s, has_L2=%d, has_L3=%d, priority=%d",
         __func__, uplink.if_name, uplink.if_type, uplink.has_L2, uplink.has_L3, uplink.priority);
}

/**
 * @brief update os_persist field on wifi_inet_table
 *
 * @param persist_state flag reflects enable_persist state
 * @param if_name Lte ifname
 * @return true when wifi_inet_config ovsdb table updated successfully
 *         false otherwise
 */
static bool
ltem_wifi_inet_os_persist_update(bool persist_state, char *if_name)
{
    struct schema_Wifi_Inet_Config icfg;
    int ret;

    ret = ovsdb_table_select_one(&table_Wifi_Inet_Config,
                SCHEMA_COLUMN(Wifi_Inet_Config, if_name), if_name, &icfg);

    if (!ret)
    {
        LOGE("%s: %s: Failed to get interface config", __func__, if_name);
        return false;
    }

    /* return true if os_persist is already set, update otherwise */
    if (icfg.os_persist == persist_state) return true;

    MEMZERO(icfg);
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Wifi_Inet_Config, os_persist),
                       NULL };
    SCHEMA_SET_INT(icfg.os_persist, persist_state);
    ret = ovsdb_table_update_where_f(&table_Wifi_Inet_Config,
                 ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), if_name),
                 &icfg, filter);

    if (!ret)
    {
        LOGE("%s: %s: Failed to update interface config", __func__, if_name);
        return false;
    }

    return true;
}

/**
 * @brief called when os_persist field needs to be updated for
 * Lte_config table
 *
 * @param persist_state flag reflects enable_persist state
 * @param if_name Lte if_name
 * @return true when lte_config ovsdb table updated successfully
 *         false otherwise
 */
static bool
ltem_lte_config_os_persist_update(bool persist_state, char *if_name)
{
    struct schema_Lte_Config lte_cfg;
    int ret;

    ret = ovsdb_table_select_one(&table_Lte_Config,
                SCHEMA_COLUMN(Lte_Config, if_name), if_name, &lte_cfg);

    if (!ret)
    {
        LOGE("%s: %s: Failed to get interface config", __func__, if_name);
        return false;
    }

    /* return true if os_persist is already set, update otherwise */
    if (lte_cfg.os_persist == persist_state) return true;

    /* update the os_persist field */
    MEMZERO(lte_cfg);
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Lte_Config, os_persist),
                       NULL };
    SCHEMA_SET_INT(lte_cfg.os_persist, persist_state);
    ret = ovsdb_table_update_where_f(&table_Lte_Config,
                 ovsdb_where_simple(SCHEMA_COLUMN(Lte_Config, if_name), if_name),
                 &lte_cfg, filter);

    if (!ret)
    {
        LOGE("%s: %s: Failed to update interface config", __func__, if_name);
        return false;
    }

    return true;
}

/**
 * @brief called when os_persist field needs to be updated for
 * Lte_state table
 *
 * @param persist_state flag reflects enable_persist state
 * @param if_name Lte if_name
 * @return none
 */
static void
ltem_lte_state_update_persist(bool state, char *if_name)
{
    struct schema_Lte_State lte_state;
    int ret;
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Lte_State, enable_persist),
                       NULL };

    ret = ovsdb_table_select_one(&table_Lte_State,
                SCHEMA_COLUMN(Lte_State, if_name), if_name, &lte_state);

    if (!ret)
    {
        LOGE("%s: %s: Failed to get interface config", __func__, if_name);
        return;
    }
    if (lte_state.enable_persist == state) return;

    SCHEMA_SET_INT(lte_state.enable_persist, state);
    ret = ovsdb_table_update_where_f(&table_Lte_State,
                 ovsdb_where_simple(SCHEMA_COLUMN(Lte_State, if_name), if_name),
                 &lte_state, filter);
    if (!ret)
    {
            LOGE("%s: Failed to update Lte_State table entry", __func__);
    }
}

/**
 * @brief update os_persist field on Wifi_Inet and Lte_Config
 * based on enable_persist flag
 *
 * @param persist_state flag reflects enable_persist state
 * @param if_name Lte ifname
 * @return none
 */
static void
ltem_update_enable_persist(bool persist_state, char *if_name)
{
    /* Update enable_persist field in Lte_state table only when Lte_config and
       Wifi_Init_config updates  successfully */
    if ( ltem_wifi_inet_os_persist_update(persist_state, if_name) &&
         ltem_lte_config_os_persist_update(persist_state, if_name) )
    {
            ltem_lte_state_update_persist(persist_state, if_name);
    }
}

void
callback_Lte_Config(ovsdb_update_monitor_t *mon,
                    struct schema_Lte_Config *old_lte_conf,
                    struct schema_Lte_Config *lte_conf)
{
    ltem_mgr_t *mgr = ltem_get_mgr();
    int rc;

    LOGI("%s: if_name=%s, enable=%d, lte_failover_enable=%d, ipv4_enable=%d, ipv6_enable=%d,"
         " force_use_lte=%d active_simcard_slot=%d, modem_enable=%d, report_interval=%d",
         __func__, lte_conf->if_name, lte_conf->manager_enable, lte_conf->lte_failover_enable, lte_conf->ipv4_enable,
         lte_conf->ipv6_enable, lte_conf->force_use_lte, lte_conf->active_simcard_slot, lte_conf->modem_enable,
         lte_conf->report_interval);

    if (mon->mon_type != OVSDB_UPDATE_ERROR)
    {
        ltem_update_conf(lte_conf);
    }
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        rc = ltem_ovsdb_create_lte_state(mgr);
        if (rc)
        {
            LOGE("%s: Failed to create Lte_State table entry", __func__);
            return;
        }
    }
    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: OVSDB_UPDATE_ERROR", __func__);
            return;
        case OVSDB_UPDATE_NEW:
            LOGI("%s mon_type = OVSDB_UPDATE_NEW", __func__);
        case OVSDB_UPDATE_MODIFY:
            LOGI("%s mon_type = OVSDB_UPDATE_MODIFY", __func__);
            ltem_ovsdb_update_lte_state(mgr);
            rc = strncmp(lte_conf->apn, "", strlen(lte_conf->apn));
            if (rc)
            {
                osn_lte_set_pdp_context_params(PDP_CTXT_APN, lte_conf->apn);
            }
            osn_lte_set_sim_slot(lte_conf->active_simcard_slot);

            rc = strncmp(lte_conf->lte_bands_enable, "", strlen(lte_conf->lte_bands_enable));
            if (rc)
            {
                osn_lte_set_bands(lte_conf->lte_bands_enable);
            }
            if (!lte_conf->manager_enable && !lte_conf->force_use_lte)
            {
                ltem_ovsdb_cmu_disable_lte(mgr);
            }
            if (lte_conf->force_use_lte && lte_conf->lte_failover_enable)
            {
                ltem_set_wan_state(LTEM_WAN_STATE_DOWN);
            }
            else if (old_lte_conf->force_use_lte && !lte_conf->force_use_lte)
            {
                LOGI("%s: force_lte[%d]", __func__, lte_conf->force_use_lte);
                ltem_set_wan_state(LTEM_WAN_STATE_UP);
            }
            /* Try to catch a user error where force_lte is enabled and active and they disable LTE */
            else if (lte_conf->force_use_lte && !lte_conf->lte_failover_enable)
            {
                LOGI("%s: lte_failover_enable[%d], force_lte[%d]",
                     __func__, lte_conf->lte_failover_enable, lte_conf->force_use_lte);
                ltem_set_wan_state(LTEM_WAN_STATE_UP);
            }
            if (lte_conf->enable_persist != old_lte_conf->enable_persist)
            {
                LOGI("%s: enable_persist[%d]", __func__, lte_conf->enable_persist);
                ltem_update_enable_persist(lte_conf->enable_persist, lte_conf->if_name);
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
ltem_handle_nm_update_wwan0(struct schema_Wifi_Inet_State *old_inet_state,
                            struct schema_Wifi_Inet_State *inet_state)
{
    char *null_inet_addr = "0.0.0.0";
    ltem_mgr_t *mgr = ltem_get_mgr();
    int res;

    LOGI("%s: old: if_name=%s, enabled=%d inet_addr=%s, new: if_name=%s, enabled=%d, inet_addr=%s",
         __func__, old_inet_state->if_name, old_inet_state->enabled, old_inet_state->inet_addr, inet_state->if_name, inet_state->enabled, inet_state->inet_addr);
    if (!mgr->lte_route)
    {
        LOGE("%s: lte_route NULL", __func__);
        return;
    }

    STRSCPY(mgr->lte_route->lte_ip_addr, inet_state->inet_addr);
    ltem_ovsdb_cmu_insert_lte(mgr);
    res = strncmp(inet_state->inet_addr, null_inet_addr, strlen(inet_state->inet_addr));
    if (!res)
    {
        ltem_set_lte_state(LTEM_LTE_STATE_DOWN);
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
            rc = strncmp(old_inet_state->if_name, "wwan0", strlen(old_inet_state->if_name));
            if (!rc)
            {
                LOGI("%s: OVSDB_UDATE_DEL: %s", __func__, old_inet_state->if_name);
                ltem_set_lte_state(LTEM_LTE_STATE_DOWN);
            }
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            rc = strncmp(inet_state->if_name, "wwan0", strlen(inet_state->if_name));
            if (!rc) ltem_handle_nm_update_wwan0(old_inet_state, inet_state);

            break;
    }
}

void
ltem_handle_cm_update_lte(struct schema_Connection_Manager_Uplink *old_uplink,
                          struct schema_Connection_Manager_Uplink *uplink)
{

    LOGD("%s: if_name=%s if_type=%s, has_L2=%d, has_L3=%d, priority=%d, is_used=%d",
         __func__, uplink->if_name, uplink->if_type, uplink->has_L2, uplink->has_L3, uplink->priority, uplink->is_used);
    if (uplink->is_used)
    {
        ltem_set_wan_state(LTEM_WAN_STATE_DOWN);
        LOGD("%s: wwan0 is_used[true]", __func__);
    }
    else
    {
        ltem_set_wan_state(LTEM_WAN_STATE_UP);
        LOGD("%s: wwan0 is_used[false]", __func__);
    }
}

void
ltem_handle_cm_update_wan_priority(struct schema_Connection_Manager_Uplink *uplink)
{
    ltem_mgr_t *mgr;
    lte_route_info_t *route;
    int res;

    mgr = ltem_get_mgr();
    route = mgr->lte_route;
    if (!route) return;

    if (route->wan_gw[0])
    {
        res = strncmp(uplink->if_name, route->wan_gw, strlen(uplink->if_name));
        if (res == 0)
        {
            LOGI("%s: Update WAN priority [%d]", __func__, uplink->priority);
            route->wan_priority = uplink->priority;
        }
    }

    return;
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
                ltem_handle_cm_update_lte(old_uplink, uplink);
            }
            break;

        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            res = strncmp(uplink->if_name, "wwan0", strlen(uplink->if_name));
            if (res == 0)
            {
                LOGI("%s: if_name[%s], has_L2[%d], has_L3[%d], priority[%d] is_used[%d]",
                     __func__, uplink->if_name, uplink->has_L2, uplink->has_L3, uplink->priority, uplink->is_used);
                ltem_handle_cm_update_lte(old_uplink, uplink);
            }

            /* Check to see if we need to update our WAN proirity */
            ltem_handle_cm_update_wan_priority(uplink);

            break;
    }
}

void
ltem_handle_wan_rs_update(struct schema_Wifi_Route_State *old_route_state, struct schema_Wifi_Route_State *route_state)
{
    ltem_mgr_t *mgr = ltem_get_mgr();
    char *default_mask = "0.0.0.0";
    int res;

    LOGI("%s: if_name=%s, dest_addr=%s, dest_gw=%s, dest_mask=%s", __func__, route_state->if_name, route_state->dest_addr,
         route_state->gateway, route_state->dest_mask);
    res = strncmp(route_state->dest_mask, default_mask, strlen(route_state->dest_mask));
    if (res == 0)
    {
        ltem_update_wan_route(mgr, route_state->if_name, route_state->dest_addr, route_state->gateway, route_state->dest_mask);
    }
}

void
ltem_handle_lte_rs_update(struct schema_Wifi_Route_State *old_route_state, struct schema_Wifi_Route_State *route_state)
{
    ltem_mgr_t *mgr = ltem_get_mgr();
    char *default_mask = "0.0.0.0";
    char *null_gateway = "0.0.0.0";
    int res;

    LOGI("%s: if_name=%s, dest_addr=%s, dest_gw=%s, dest_mask=%s", __func__, route_state->if_name, route_state->dest_addr,
         route_state->gateway, route_state->dest_mask);
    res = strncmp(route_state->dest_mask, default_mask, strlen(route_state->dest_mask));
    if (res == 0)
    {
        ltem_update_lte_route(mgr, route_state->if_name, route_state->dest_addr, route_state->gateway, route_state->dest_mask);
    }

    res = strncmp(route_state->gateway, null_gateway, strlen(route_state->gateway));
    if (res)
    {
        ltem_set_lte_state(LTEM_LTE_STATE_UP);
    }
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
            LOGI("%s: OVSDB_UPDATE_DEL: if_name=%s, dest_addr=%s, dest_gw=%s, dest_mask=%s", __func__,
                 old_route_state->if_name, old_route_state->dest_addr, old_route_state->gateway, old_route_state->dest_mask);
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            res = strncmp(route_state->if_name, "eth0", strlen(route_state->if_name));
            if (res == 0)
            {
                ltem_handle_wan_rs_update(old_route_state, route_state);
            }
            res = strncmp(route_state->if_name, "eth1", strlen(route_state->if_name));
            if (res == 0)
            {
                ltem_handle_wan_rs_update(old_route_state, route_state);
            }
            res = strncmp(route_state->if_name, "wwan0", strlen(route_state->if_name));
            if (res == 0)
            {
                ltem_handle_lte_rs_update(old_route_state, route_state);
            }
            break;
    }
}

void
ltem_ovsdb_update_awlan_node(struct schema_AWLAN_Node *new)
{
    int i;
    ltem_mgr_t *mgr = ltem_get_mgr();

    for (i = 0; i < new->mqtt_headers_len; i++)
    {
        if (strcmp(new->mqtt_headers_keys[i], "nodeId") == 0)
        {
            STRSCPY(mgr->node_id, new->mqtt_headers[i]);
            LOGI("%s: new node_id[%s]", __func__, mgr->node_id);
        }
        else if (strcmp(new->mqtt_headers_keys[i], "locationId") == 0)
        {
            STRSCPY(mgr->location_id, new->mqtt_headers[i]);
            LOGI("%s: new location_id[%s]", __func__, mgr->location_id);
        }
    }
    for (i = 0; i < new->mqtt_topics_len; i++)
    {
        if (strcmp(new->mqtt_topics_keys[i], "LteStats") == 0)
        {
            STRSCPY(mgr->topic, new->mqtt_topics[i]);
            LOGI("%s: new topic[%s]", __func__, mgr->topic);
        }
    }
}

void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old,
        struct schema_AWLAN_Node *new)
{
    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            ltem_ovsdb_update_awlan_node(new);
            break;
    }

}

int
ltem_ovsdb_update_wifi_route_config_metric(ltem_mgr_t *mgr, char *if_name, uint32_t metric)
{
    struct schema_Wifi_Route_Config route_config;
    lte_route_info_t *route_info;
    int res;

    route_info = mgr->lte_route;
    if (!route_info)
    {
        LOGE("%s: route_info not set", __func__);
        return -1;
    }

    res = ovsdb_table_select_one(&table_Wifi_Route_Config,
                                 SCHEMA_COLUMN(Wifi_Route_Config, if_name), if_name, &route_config);
    if (!res)
    {
        LOGI("%s: %s not found in Wifi_Route_Config", __func__, if_name);
        return -1;
    }


    LOGI("%s: update %s Wifi_Route_Config settings: subnet[%s], netmask[%s] gw[%s], metric[%d]",
         __func__, route_config.if_name, route_config.dest_addr, route_config.dest_mask, route_config.gateway, metric);

    route_config._partial_update = true;
    SCHEMA_SET_INT(route_config.metric, metric);
    res = ovsdb_table_upsert_simple(&table_Wifi_Route_Config,
                                    SCHEMA_COLUMN(Wifi_Route_Config, if_name),
                                    route_config.if_name,
                                    &route_config,
                                    NULL);
    if (!res)
    {
        LOGW("%s: Update %s Wifi_Route_Config failed", __func__, if_name);
        return -1;
    }
    return 0;
}

void
callback_Wifi_Route_Config(ovsdb_update_monitor_t *mon,
                           struct schema_Wifi_Route_Config *old_route_config,
                           struct schema_Wifi_Route_Config *route_config)
{
    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_NEW:
            LOGI("%s:  OVSDB_UPDATE_NEW route config: if_name[%s], dest_addr[%s], dest_mask[%s], gateway[%s], metric[%d]",
                 __func__, route_config->if_name, route_config->dest_addr, route_config->dest_mask,
                 route_config->gateway, route_config->metric);
            break;
        case OVSDB_UPDATE_MODIFY:
            LOGI("%s: OVSDB_UPDATE_MODIFY route config: if_name[%s], dest_addr[%s], dest_mask[%s], gateway[%s], metric[%d]",
                 __func__, route_config->if_name, route_config->dest_addr, route_config->dest_mask,
                 route_config->gateway, route_config->metric);
            break;
        case OVSDB_UPDATE_DEL:
            LOGI("%s: OVSDB_UPDATE_DEL route config: if_name[%s], dest_addr[%s], dest_mask[%s], gateway[%s], metric[%d]",
                 __func__, route_config->if_name, route_config->dest_addr, route_config->dest_mask,
                 route_config->gateway, route_config->metric);
            break;
    }
}

int
ltem_ovsdb_init(void)
{
    LOGI("Initializing LTEM OVSDB tables");

    static char *filter[] =
    {
        SCHEMA_COLUMN(AWLAN_Node, mqtt_headers),
        SCHEMA_COLUMN(AWLAN_Node, mqtt_topics),
        NULL,
    };

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(Lte_Config);
    OVSDB_TABLE_INIT_NO_KEY(Lte_State);
    OVSDB_TABLE_INIT(Wifi_Inet_State, if_name);
    OVSDB_TABLE_INIT(Wifi_Inet_Config, if_name);
    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);
    OVSDB_TABLE_INIT(Wifi_Route_State, if_name);
    OVSDB_TABLE_INIT(Wifi_Route_Config, if_name);
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Lte_Config, false);
    OVSDB_TABLE_MONITOR(Lte_State, false);
    OVSDB_TABLE_MONITOR(Wifi_Inet_State, false);
    OVSDB_TABLE_MONITOR(Connection_Manager_Uplink, false);
    OVSDB_TABLE_MONITOR(Wifi_Route_State, false);
    OVSDB_TABLE_MONITOR(Wifi_Route_Config, false);
    OVSDB_TABLE_MONITOR_F(AWLAN_Node, filter);

    return 0;
}

