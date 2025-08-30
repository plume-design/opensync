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
#include "cellm_mgr.h"

/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB

#define LTE_NODE_MODULE "lte"
// #define LTE_NODE_STATE_MEM_KEY "max_mem"

ovsdb_table_t table_Lte_Config;
ovsdb_table_t table_Lte_State;
ovsdb_table_t table_Wifi_Inet_Config;
ovsdb_table_t table_Wifi_Inet_State;
ovsdb_table_t table_Connection_Manager_Uplink;
ovsdb_table_t table_DHCP_leased_IP;
ovsdb_table_t table_AWLAN_Node;
ovsdb_table_t table_Wifi_Route_Config;
ovsdb_table_t table_IPv6_RouteAdv;
ovsdb_table_t table_IPv6_Prefix;
ovsdb_table_t table_Manager;

void cellm_ltem_disable_band_40(cellm_mgr_t *mgr)
{
    char lte_bands[32];
    char u_bands_str[32];
    char l_bands_str[32];
    char *ptr;
    uint64_t upper_bands;
    uint64_t lower_bands;
    osn_cell_modem_info_t *modem_info;

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
    SPRINTF(lte_bands, "%01llx%016llx", (long long unsigned int)upper_bands, (long long unsigned int)lower_bands);
    LOGD("%s: Setting LTE bands[%s]", __func__, lte_bands);
    osn_cell_set_bands(lte_bands);
}

int cellm_lte_update_conf(struct schema_Lte_Config *lte_conf)
{
    cellm_mgr_t *mgr = cellm_get_mgr();
    cellm_config_info_t *conf = mgr->cellm_config_info;

    if (conf == NULL) return -1;
    STRSCPY(conf->if_name, lte_conf->if_name);
    conf->manager_enable = lte_conf->manager_enable;
    conf->cellm_failover_enable = lte_conf->lte_failover_enable;
    conf->ipv4_enable = lte_conf->ipv4_enable;
    conf->ipv6_enable = lte_conf->ipv6_enable;
    conf->force_use_lte = lte_conf->force_use_lte;
    conf->active_simcard_slot = lte_conf->active_simcard_slot;
    conf->modem_enable = lte_conf->modem_enable;
    conf->enable_persist = lte_conf->enable_persist;
    if (conf->modem_enable)
    {
        if (osn_cell_read_modem())
        {
            LOGE("%s: osn_cell_read_modem(): failed", __func__);
        }
        else
        {
            /* Disable band 40 */
            cellm_ltem_disable_band_40(mgr);
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
    STRSCPY(conf->lte_bands, lte_conf->lte_bands_enable);
    STRSCPY(conf->esim_activation_code, lte_conf->esim_activation_code);

    return 0;
}

int cellm_ovsdb_create_lte_state(cellm_mgr_t *mgr)
{
    struct schema_Lte_State lte_state;
    cellm_config_info_t *lte_config;
    cellm_state_info_t *lte_state_info;
    osn_cell_modem_info_t *modem_info;
    const char *if_name;
    char *sim_status;
    char *net_state;
    char *net_mode;
    int rc;

    MEMZERO(lte_state);

    if (mgr == NULL) return -1;

    lte_config = mgr->cellm_config_info;
    if (lte_config == NULL)
    {
        LOGE("%s: cellm_config_info NULL", __func__);
        return -1;
    }
    if_name = lte_config->if_name;
    if (!if_name[0])
    {
        LOGI("%s: invalid if_name[%s]", __func__, if_name);
        return -1;
    }

    lte_state_info = mgr->cellm_state_info;
    if (lte_state_info == NULL)
    {
        LOGE("%s: lte_state_info NULL", __func__);
        return -1;
    }

    rc = ovsdb_table_select_one(&table_Lte_State, SCHEMA_COLUMN(Lte_State, if_name), if_name, &lte_state);
    if (rc) return 0;

    modem_info = mgr->modem_info;
    LOGD("%s: Insert Lte_State: if_name=[%s]", __func__, if_name);
    lte_state._partial_update = true;
    // Config from Lte_Config table
    SCHEMA_SET_STR(lte_state.if_name, if_name);
    SCHEMA_SET_INT(lte_state.manager_enable, lte_config->manager_enable);
    SCHEMA_SET_INT(lte_state.lte_failover_enable, lte_config->cellm_failover_enable);
    SCHEMA_SET_INT(lte_state.ipv4_enable, lte_config->ipv4_enable);
    SCHEMA_SET_INT(lte_state.ipv6_enable, lte_config->ipv6_enable);
    SCHEMA_SET_INT(lte_state.force_use_lte, lte_config->force_use_lte);
    SCHEMA_SET_INT(lte_state.modem_enable, lte_config->modem_enable);
    SCHEMA_SET_INT(lte_state.active_simcard_slot, modem_info->active_simcard_slot);
    SCHEMA_SET_INT(lte_state.report_interval, lte_config->report_interval);
    SCHEMA_SET_INT(lte_state.enable_persist, true);
    if (lte_config->apn[0] == '\0')
    {
        SCHEMA_UNSET_FIELD(lte_state.apn);
        LOGI("%s: Empty APN[%s]", __func__, lte_state.apn);
    }
    else
    {
        SCHEMA_SET_STR(lte_state.apn, lte_config->apn);
    }

    // State info
    modem_info = mgr->modem_info;
    SCHEMA_SET_INT(lte_state.modem_present, modem_info->modem_present);
    SCHEMA_SET_STR(lte_state.iccid, modem_info->iccid);
    SCHEMA_SET_STR(lte_state.imei, modem_info->imei);
    SCHEMA_SET_STR(lte_state.imsi, modem_info->imsi);
    switch (modem_info->sim_status)
    {
        case CELL_SIM_STATUS_REMOVED:
            sim_status = "Removed";
            break;
        case CELL_SIM_STATUS_INSERTED:
            sim_status = "Inserted";
            break;
        case CELL_SIM_STATUS_BAD:
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
        case CELL_NET_REG_STAT_NOTREG:
            net_state = "not_registered_not_searching";
            break;

        case CELL_NET_REG_STAT_REG:
            if (modem_info->srv_cell.mode == CELL_MODE_LTE)
            {
                net_state = "registered_home_network";
            }
            else
            {
                net_state = "registration_denied";
            }
            break;

        case CELL_NET_REG_STAT_SEARCH:
            net_state = "not_registered_searching";
            break;

        case CELL_NET_REG_STAT_DENIED:
            net_state = "registration_denied";
            break;

        case CELL_NET_REG_STAT_ROAMING:
            if (modem_info->srv_cell.mode == CELL_MODE_LTE)
            {
                net_state = "registered_roaming";
            }
            else
            {
                net_state = "registration_denied";
            }
            break;

        default:
            net_state = "unknown";
            break;
    }
    SCHEMA_SET_STR(lte_state.lte_net_state, net_state);

    switch (modem_info->srv_cell.mode)
    {
        case CELL_MODE_LTE:
            net_mode = "lte";
            break;

        case CELL_MODE_WCDMA:
            net_mode = "wcdma";
            break;

        default:
            net_mode = "unknown";
            break;
    }
    SCHEMA_SET_STR(lte_state.lte_net_mode, net_mode);

    MEMZERO(modem_info->lte_band_val);
    SCHEMA_SET_STR(lte_state.lte_bands_enable, modem_info->lte_band_val);

    if (strlen(modem_info->modem_fw_ver))
    {
        SCHEMA_SET_STR(lte_state.modem_firmware_version, modem_info->modem_fw_ver);
    }

    if (!ovsdb_table_insert(&table_Lte_State, &lte_state))
    {
        LOG(ERR, "%s: Error Inserting Lte_State", __func__);
        return -1;
    }

    return 0;
}
int cellm_ovsdb_update_lte_state(cellm_mgr_t *mgr)
{
    struct schema_Lte_State lte_state;
    cellm_config_info_t *lte_config;
    cellm_state_info_t *lte_state_info;
    osn_cell_modem_info_t *modem_info;
    const char *if_name;
    char *sim_status;
    char *net_state;
    char *net_mode;
    int res;
    char *filter[] = {
        "+",
        SCHEMA_COLUMN(Lte_State, manager_enable),
        SCHEMA_COLUMN(Lte_State, lte_failover_enable),
        SCHEMA_COLUMN(Lte_State, ipv4_enable),
        SCHEMA_COLUMN(Lte_State, ipv6_enable),
        SCHEMA_COLUMN(Lte_State, enable_persist),
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
        SCHEMA_COLUMN(Lte_State, lte_net_mode),
        SCHEMA_COLUMN(Lte_State, lte_bands_enable),
        NULL};

    if (mgr == NULL) return -1;
    lte_config = mgr->cellm_config_info;
    if (lte_config == NULL) return -1;
    lte_state_info = mgr->cellm_state_info;
    if (lte_state_info == NULL) return -1;
    modem_info = mgr->modem_info;
    if (modem_info == NULL) return -1;
    if_name = lte_config->if_name;
    if (!if_name[0]) return -1;
    modem_info = osn_cell_get_modem_info();

    res = ovsdb_table_select_one(&table_Lte_State, SCHEMA_COLUMN(Lte_State, if_name), if_name, &lte_state);
    if (!res)
    {
        LOGI("%s: %s not found in Lte_State", __func__, if_name);
        return -1;
    }

    MEMZERO(lte_state);

    if_name = mgr->cellm_config_info->if_name;
    if (!if_name[0]) return -1;

    LOGD("%s: update %s Lte_State settings", __func__, if_name);
    lte_state._partial_update = true;
    // Config from Lte_Config table
    SCHEMA_SET_STR(lte_state.if_name, if_name);
    SCHEMA_SET_INT(lte_state.manager_enable, lte_config->manager_enable);
    SCHEMA_SET_INT(lte_state.lte_failover_enable, lte_config->cellm_failover_enable);
    SCHEMA_SET_INT(lte_state.ipv4_enable, lte_config->ipv4_enable);
    SCHEMA_SET_INT(lte_state.ipv6_enable, lte_config->ipv6_enable);
    SCHEMA_SET_INT(lte_state.enable_persist, true);
    SCHEMA_SET_INT(lte_state.force_use_lte, lte_config->force_use_lte);
    SCHEMA_SET_INT(lte_state.modem_enable, lte_config->modem_enable);
    SCHEMA_SET_INT(lte_state.active_simcard_slot, modem_info->active_simcard_slot);
    SCHEMA_SET_INT(lte_state.report_interval, lte_config->report_interval);
    if (lte_config->apn[0] == '\0')
    {
        SCHEMA_UNSET_FIELD(lte_state.apn);
        LOGI("%s: Empty APN[%s]", __func__, lte_state.apn);
    }
    else
    {
        SCHEMA_SET_STR(lte_state.apn, lte_config->apn);
    }

    // State info
    SCHEMA_SET_INT(lte_state.modem_present, modem_info->modem_present);
    SCHEMA_SET_STR(lte_state.iccid, modem_info->iccid);
    SCHEMA_SET_STR(lte_state.imei, modem_info->imei);
    SCHEMA_SET_STR(lte_state.imsi, modem_info->imsi);
    switch (modem_info->sim_status)
    {
        case CELL_SIM_STATUS_REMOVED:
            sim_status = "Removed";
            break;
        case CELL_SIM_STATUS_INSERTED:
            sim_status = "Inserted";
            break;
        case CELL_SIM_STATUS_BAD:
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
        case CELL_NET_REG_STAT_NOTREG:
            net_state = "not_registered_not_searching";
            break;

        case CELL_NET_REG_STAT_REG:
            net_state = "registered_home_network";
            break;

        case CELL_NET_REG_STAT_SEARCH:
            net_state = "not_registered_searching";
            break;

        case CELL_NET_REG_STAT_DENIED:
            net_state = "registration_denied";
            break;

        case CELL_NET_REG_STAT_ROAMING:
            net_state = "registered_roaming";
            break;

        default:
            net_state = "unknown";
            break;
    }
    SCHEMA_SET_STR(lte_state.lte_net_state, net_state);

    switch (modem_info->srv_cell.mode)
    {
        case CELL_MODE_LTE:
            net_mode = "lte";
            break;

        case CELL_MODE_WCDMA:
            net_mode = "wcdma";
            break;

        default:
            net_mode = "unknown";
            break;
    }
    SCHEMA_SET_STR(lte_state.lte_net_mode, net_mode);

    MEMZERO(modem_info->lte_band_val);
    SCHEMA_SET_STR(lte_state.lte_bands_enable, modem_info->lte_band_val);

    res = ovsdb_table_update_where_f(
            &table_Lte_State,
            ovsdb_where_simple(SCHEMA_COLUMN(Lte_State, if_name), if_name),
            &lte_state,
            filter);
    if (!res)
    {
        LOGW("%s: Update %s Lte_State table failed", __func__, if_name);
        return -1;
    }
    return 0;
}

int cellm_ovsdb_cmu_update_lte(cellm_mgr_t *mgr)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    char *filter[] = {
        "+",
        SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2),
        SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3),
        SCHEMA_COLUMN(Connection_Manager_Uplink, priority),
        NULL};
    const char *if_name;
    int res;

    if_name = mgr->cellm_config_info->if_name;
    if (!if_name[0])
    {
        LOGI("%s: if_name[%s]", __func__, if_name);
        return 0;
    }

    res = ovsdb_table_select_one(
            &table_Connection_Manager_Uplink,
            SCHEMA_COLUMN(Connection_Manager_Uplink, if_name),
            if_name,
            &cm_conf);
    if (!res)
    {
        LOGI("%s: %s not found in Connection_Manager_Uplink", __func__, if_name);
        return 0;
    }

    MEMZERO(cm_conf);

    LOGI("%s: update %s LTE CM settings", __func__, if_name);
    cm_conf._partial_update = true;
    SCHEMA_SET_INT(cm_conf.has_L2, true);
    SCHEMA_SET_INT(cm_conf.has_L3, mgr->cellm_route->has_L3);
    SCHEMA_SET_INT(cm_conf.priority, LTE_CMU_DEFAULT_PRIORITY);

    res = ovsdb_table_update_where_f(
            &table_Connection_Manager_Uplink,
            ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
            &cm_conf,
            filter);
    if (!res)
    {
        LOGW("%s: Update %s CM table failed", __func__, if_name);
        return -1;
    }
    return 0;
}

int cellm_ovsdb_cmu_insert_lte(cellm_mgr_t *mgr)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    char *if_type = LTE_TYPE_NAME;
    const char *if_name;
    int rc;

    MEMZERO(cm_conf);

    if_name = mgr->cellm_config_info->if_name;

    if (!if_name[0])
    {
        LOGI("%s: invalid if_name[%s]", __func__, if_name);
        return -1;
    }

    rc = ovsdb_table_select_one(
            &table_Connection_Manager_Uplink,
            SCHEMA_COLUMN(Connection_Manager_Uplink, if_name),
            if_name,
            &cm_conf);
    if (rc)
    {
        LOGI("%s: Entry for %s exists, update Connection_Manager_Uplink table", __func__, if_name);
        return cellm_ovsdb_cmu_update_lte(mgr);
    }

    LOGI("%s: Insert Connection_Manager_Uplink: if_name=[%s], if_type[%s]", __func__, if_name, if_type);
    cm_conf._partial_update = true;
    SCHEMA_SET_STR(cm_conf.if_name, if_name);
    SCHEMA_SET_STR(cm_conf.if_type, if_type);
    SCHEMA_SET_INT(cm_conf.has_L2, true);
    SCHEMA_SET_INT(cm_conf.has_L3, mgr->cellm_route->has_L3);
    SCHEMA_SET_INT(cm_conf.priority, 2);
    if (!ovsdb_table_insert(&table_Connection_Manager_Uplink, &cm_conf))
    {
        LOG(ERR, "%s: Error Inserting Lte_Config", __func__);
        return -1;
    }

    return 0;
}

int cellm_ovsdb_cmu_disable_lte(cellm_mgr_t *mgr)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    char *filter[] = {
        "+",
        SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2),
        SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3),
        SCHEMA_COLUMN(Connection_Manager_Uplink, priority),
        NULL};
    const char *if_name;
    int res;

    if_name = mgr->cellm_config_info->if_name;
    res = ovsdb_table_select_one(
            &table_Connection_Manager_Uplink,
            SCHEMA_COLUMN(Connection_Manager_Uplink, if_name),
            if_name,
            &cm_conf);
    if (!res)
    {
        LOGI("%s: %s not found in Connection_Manager_Uplink", __func__, if_name);
        return 0;
    }

    MEMZERO(cm_conf);

    if_name = mgr->cellm_config_info->if_name;
    if (!if_name[0]) return 0;

    LOGD("%s: update %s LTE CM settings", __func__, if_name);
    cm_conf.has_L2 = false;
    cm_conf.has_L3 = false;
    cm_conf.priority = 0;

    res = ovsdb_table_update_where_f(
            &table_Connection_Manager_Uplink,
            ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
            &cm_conf,
            filter);
    if (!res)
    {
        LOGW("%s: Update %s CM table failed", __func__, if_name);
        return -1;
    }
    return 0;
}

int cellm_ovsdb_check_lte_l3_state(cellm_mgr_t *mgr)
{
    struct schema_Wifi_Route_Config route_config;
    const char *if_name;
    char *null_gateway = "0.0.0.0";
    int res;

    if (mgr->cellm_route->has_L3) return 0;

    if_name = mgr->cellm_config_info->if_name;

    res = ovsdb_table_select_one(
            &table_Wifi_Route_Config,
            SCHEMA_COLUMN(Wifi_Route_Config, if_name),
            if_name,
            &route_config);
    if (!res)
    {
        LOGI("%s: %s not found in Wifi_Route_Config", __func__, if_name);
        if (mgr->cellm_state == CELLM_STATE_UP)
        {
            LOGI("%s: Set lte_state to DOWN", __func__);
            cellm_set_state(CELLM_STATE_DOWN);
        }

        return -1;
    }

    res = strncmp(route_config.gateway, null_gateway, strlen(route_config.gateway));
    if (res)
    {
        cellm_set_state(CELLM_STATE_UP);
    }

    return 0;
}

uint32_t cellm_ovsdb_cmu_get_wan_priority(cellm_mgr_t *mgr)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    const char *if_name;
    uint32_t wan_priority;
    int ret;

    if_name = mgr->cellm_route->wan_if_name;

    ret = ovsdb_table_select_one(
            &table_Connection_Manager_Uplink,
            SCHEMA_COLUMN(Connection_Manager_Uplink, if_name),
            if_name,
            &cm_conf);
    if (!ret)
    {
        LOGI("%s: Failed to get Connection_Manager_Uplink if_name[%s]", __func__, if_name);
        return -1;
    }
    wan_priority = cm_conf.priority;
    return wan_priority;
}

uint32_t cellm_ovsdb_cmu_get_lte_priority(cellm_mgr_t *mgr)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    const char *if_name;
    uint32_t lte_priority;
    int ret;

    if_name = mgr->cellm_route->cellm_if_name;

    ret = ovsdb_table_select_one(
            &table_Connection_Manager_Uplink,
            SCHEMA_COLUMN(Connection_Manager_Uplink, if_name),
            if_name,
            &cm_conf);
    if (!ret)
    {
        LOGI("%s: Failed to get Connection_Manager_Uplink if_name[%s]", __func__, if_name);
        return -1;
    }
    lte_priority = cm_conf.priority;
    return lte_priority;
}

int cellm_ovsdb_cmu_update_lte_priority(cellm_mgr_t *mgr, uint32_t priority)
{
    struct schema_Connection_Manager_Uplink cm_conf;
    char *filter[] = {"+", SCHEMA_COLUMN(Connection_Manager_Uplink, priority), NULL};
    const char *if_name;
    int res;

    if_name = mgr->cellm_config_info->if_name;
    if (!if_name[0])
    {
        LOGI("%s: if_name[%s]", __func__, if_name);
        return 0;
    }

    res = ovsdb_table_select_one(
            &table_Connection_Manager_Uplink,
            SCHEMA_COLUMN(Connection_Manager_Uplink, if_name),
            if_name,
            &cm_conf);
    if (!res)
    {
        LOGI("%s: %s not found in Connection_Manager_Uplink", __func__, if_name);
        return 0;
    }

    MEMZERO(cm_conf);

    LOGI("%s: update %s LTE CM settings, priority[%d]", __func__, if_name, priority);
    cm_conf._partial_update = true;
    SCHEMA_SET_INT(cm_conf.priority, priority);

    res = ovsdb_table_update_where_f(
            &table_Connection_Manager_Uplink,
            ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
            &cm_conf,
            filter);
    if (!res)
    {
        LOGW("%s: Update %s CM table failed", __func__, if_name);
        return -1;
    }

    LOGD("%s: Set CMU %s priority=%d", __func__, if_name, priority);
    return 0;
}

/**
 * @brief lookup the if_type for an interface
 *
 * @param if_name interface name
 * @return if_type or NULL if not found.
 */
char *cellm_ovsdb_get_if_type(char *if_name)
{
    struct schema_Wifi_Inet_Config icfg;
    char *if_type;
    int ret;

    ret = ovsdb_table_select_one(&table_Wifi_Inet_Config, SCHEMA_COLUMN(Wifi_Inet_Config, if_name), if_name, &icfg);

    if (!ret)
    {
        LOGI("%s: %s: Failed to get interface config", __func__, if_name);
        return NULL;
    }

    if_type = strdup(icfg.if_type);
    return if_type;
}
static void cellm_ovsdb_add_wifi_lte_config(cellm_mgr_t *mgr)
{
    const char *if_name;
    struct schema_Wifi_Inet_Config wifi_conf;

    if_name = mgr->cellm_config_info->if_name;
    if (!if_name[0])
    {
        LOGI("%s: Bad if_name[%s]", __func__, if_name);
        return;
    }

    MEMZERO(wifi_conf);

    LOGI("%s: Insert Wifi_Inet_Config: if_name[%s]", __func__, if_name);
    wifi_conf._partial_update = true;
    SCHEMA_SET_STR(wifi_conf.if_name, if_name);
    SCHEMA_SET_STR(wifi_conf.if_type, LTE_TYPE_NAME);
    SCHEMA_SET_STR(wifi_conf.ip_assign_scheme, "dhcp");
    SCHEMA_SET_INT(wifi_conf.enabled, true);
    SCHEMA_SET_INT(wifi_conf.network, true);
    SCHEMA_SET_INT(wifi_conf.NAT, true);
    SCHEMA_SET_INT(wifi_conf.os_persist, true);

    if (!ovsdb_table_insert(&table_Wifi_Inet_Config, &wifi_conf))
    {
        LOG(ERR, "%s: Error Inserting Wifi config", __func__);
        return;
    }

    LOGI("%s: Inserted Wifi config for LTE", __func__);
    return;
}

static void cellm_ovsdb_add_lte_config(cellm_mgr_t *mgr)
{
    struct schema_Lte_Config lte_conf;
    const char *if_name;
    int res;

    if_name = mgr->cellm_config_info->if_name;
    res = ovsdb_table_select_one(&table_Lte_Config, SCHEMA_COLUMN(Lte_Config, if_name), if_name, &lte_conf);
    if (res)
    {
        LOGI("%s: %s already found in Lte_Config", __func__, if_name);
        return;
    }

    MEMZERO(lte_conf);
    lte_conf._partial_update = true;
    SCHEMA_SET_STR(lte_conf.if_name, if_name);
    SCHEMA_SET_INT(lte_conf.ipv4_enable, true);
    SCHEMA_SET_INT(lte_conf.ipv6_enable, true);
    SCHEMA_SET_INT(lte_conf.lte_failover_enable, true);
    SCHEMA_SET_INT(lte_conf.manager_enable, true);
    SCHEMA_SET_INT(lte_conf.modem_enable, true);
    SCHEMA_SET_INT(lte_conf.enable_persist, true);
    SCHEMA_SET_INT(lte_conf.os_persist, true);
    SCHEMA_SET_INT(lte_conf.report_interval, mgr->mqtt_interval);

    if (!ovsdb_table_insert(&table_Lte_Config, &lte_conf))
    {
        LOG(ERR, "%s: Error Inserting LTE config", __func__);
        return;
    }

    LOGI("%s: Update %s in Lte_Config table succeeded", __func__, if_name);
    return;
}

/**
 * @brief Configure LTE at init
 *
 * @param ltem_mgr_t *
 * @return none
 */
void cellm_ovsdb_config_lte(cellm_mgr_t *mgr)
{
    const char *if_name;
    struct schema_Wifi_Inet_Config wifi_cfg;
    osn_cell_modem_info_t *modem_info;
    int ret;

    LOGI("%s:%d", __func__, __LINE__);

    STRSCPY(mgr->cellm_config_info->if_name, DEFAULT_LTE_NAME);

    if (osn_cell_read_modem())
    {
        LOGI("%s: osn_cell_read_modem(): failed", __func__);
        return;
    }

    if_name = mgr->cellm_config_info->if_name;
    modem_info = mgr->modem_info;
    LOGI("%s: modem_present[%d], sim_inserted[%d]", __func__, modem_info->modem_present, modem_info->sim_inserted);
    if (!modem_info->modem_present) return;
    if (!modem_info->sim_inserted) return;

    ret = ovsdb_table_select_one(&table_Wifi_Inet_Config, SCHEMA_COLUMN(Wifi_Inet_Config, if_name), if_name, &wifi_cfg);

    if (!ret)
    {
        cellm_ovsdb_add_wifi_lte_config(mgr);
    }
    else
    {
        LOGI("%s: Lte Wifi already configured", __func__);
    }

    cellm_ovsdb_add_lte_config(mgr);
    return;
}

void callback_Lte_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Lte_Config *old_lte_conf,
        struct schema_Lte_Config *lte_conf)
{
    cellm_mgr_t *mgr = cellm_get_mgr();
    cellm_config_info_t *conf;
    int rc;

    LOGI("%s: if_name=%s, enable=%d, lte_failover_enable=%d, ipv4_enable=%d, ipv6_enable=%d,"
         " force_use_lte=%d, enable_persist=%d, lte_force_allow=%d, active_simcard_slot=%d, modem_enable=%d, "
         "report_interval=%d,"
         " apn=%s, lte_bands=%s, enable_persist=%d, esim_activation_code=%s",
         __func__,
         lte_conf->if_name,
         lte_conf->manager_enable,
         lte_conf->lte_failover_enable,
         lte_conf->ipv4_enable,
         lte_conf->ipv6_enable,
         lte_conf->force_use_lte,
         lte_conf->enable_persist,
         mgr->cellm_state_info->lte_force_allow,
         lte_conf->active_simcard_slot,
         lte_conf->modem_enable,
         lte_conf->report_interval,
         lte_conf->apn,
         lte_conf->lte_bands_enable,
         lte_conf->enable_persist,
         lte_conf->esim_activation_code);

    conf = mgr->cellm_config_info;
    if (!conf) return;

    if (mon->mon_type != OVSDB_UPDATE_ERROR)
    {
        cellm_lte_update_conf(lte_conf);
    }
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        rc = cellm_ovsdb_create_lte_state(mgr);
        if (rc)
        {
            LOGE("%s: Failed to create Lte_State table entry", __func__);
            return;
        }
        if (lte_conf->manager_enable && lte_conf->modem_enable)
        {
            rc = cellm_init_modem();
            if (!rc)
            {
                LOGE("%s: Failed to init LTE modem", __func__);
                return;
            }
        }
    }
    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: OVSDB_UPDATE_ERROR", __func__);
            return;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            cellm_ovsdb_update_lte_state(mgr);
            if (old_lte_conf->manager_enable && !lte_conf->manager_enable) return;
            if (old_lte_conf->modem_enable && !lte_conf->modem_enable)
            {
                cellm_fini_modem();
                return;
            }
            LOGD("%s mon_type = %s",
                 __func__,
                 mon->mon_type == OVSDB_UPDATE_MODIFY ? "OVSDB_UPDATE_MODIFY" : "OVSDB_UPDATE_NEW");

            /* A NULL APN is valid */
            LOGI("%s: old APN[%s], new APN[%s]", __func__, conf->apn, lte_conf->apn);
            rc = strncmp(conf->apn, lte_conf->apn, sizeof(lte_conf->apn));
            if (rc)
            {
                LOGI("%s: setting new APN[%s]", __func__, lte_conf->apn);
                if (lte_conf->apn[0] == 0)
                {
                    LOGI("%s: APN Empty/NULL", __func__);
                    MEMZERO(conf->apn);
                }
                else
                {
                    LOGI("%s: current APN[%s], new APN[%s]", __func__, conf->apn, lte_conf->apn);
                    STRSCPY(conf->apn, lte_conf->apn);
                }

                osn_cell_set_pdp_context_params(PDP_CTXT_APN, conf->apn);
            }

            if (mgr->modem_info->active_simcard_slot != (uint32_t)lte_conf->active_simcard_slot)
            {
                osn_cell_set_sim_slot(lte_conf->active_simcard_slot);
            }

            rc = strncmp(lte_conf->lte_bands_enable, "", strlen(lte_conf->lte_bands_enable));
            if (rc)
            {
                osn_cell_set_bands(lte_conf->lte_bands_enable);
            }
            if (!lte_conf->manager_enable && !lte_conf->force_use_lte)
            {
                cellm_ovsdb_cmu_disable_lte(mgr);
            }
            if (lte_conf->lte_failover_enable && conf->force_use_lte)
            {
                if (!lte_conf->enable_persist)
                {
                    cellm_set_wan_state(CELLM_WAN_STATE_DOWN);
                }
                else if (mgr->cellm_state_info->lte_force_allow)
                {
                    cellm_set_wan_state(CELLM_WAN_STATE_DOWN);
                }
            }
            else if (old_lte_conf->force_use_lte && !conf->force_use_lte)
            {
                LOGI("%s: force_lte[%d]", __func__, lte_conf->force_use_lte);
                cellm_set_wan_state(CELLM_WAN_STATE_UP);
            }
            /* Try to catch a user error where force_lte is enabled and active and they disable LTE */
            else if (conf->force_use_lte && !lte_conf->lte_failover_enable)
            {
                LOGI("%s: lte_failover_enable[%d], force_lte[%d]",
                     __func__,
                     lte_conf->lte_failover_enable,
                     lte_conf->force_use_lte);
                cellm_set_wan_state(CELLM_WAN_STATE_UP);
            }

            /*
             * We have to wait for a callback triggered by the cloud before we
             * allow force LTE when persist is set
             */
            mgr->cellm_state_info->lte_force_allow = true;
            LOGI("%s: lte_force_allow[%d]", __func__, mgr->cellm_state_info->lte_force_allow);

            if (lte_conf->esim_activation_code[0])
            {
                rc =
                        strncmp(lte_conf->esim_activation_code,
                                old_lte_conf->esim_activation_code,
                                sizeof(lte_conf->esim_activation_code));
                if (rc) /* New activation code */
                {
                    STRSCPY(conf->esim_activation_code, lte_conf->esim_activation_code);
                    rc = cellm_update_esim(mgr);
                    if (rc)
                    {
                        MEMZERO(conf->esim_activation_code);
                    }
                }
            }

            break;
        case OVSDB_UPDATE_DEL:
            LOGI("%s mon_type = OVSDB_UPDATE_DEL, CELLM_STATE_DOWN", __func__);
            cellm_set_state(CELLM_STATE_DOWN);
            cellm_fini_modem();

            break;
    }
}

void callback_Lte_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Lte_State *old_lte_state,
        struct schema_Lte_State *lte_state)
{
    LOGD("%s mon_type = %d", __func__, mon->mon_type);
    switch (mon->mon_type)
    {
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

static void cellm_check_lte_mtu(void)
{
    struct schema_Wifi_Inet_Config wifi_conf;
    char *filter[] = {"+", SCHEMA_COLUMN(Wifi_Inet_Config, mtu), NULL};
    int res;

    res = ovsdb_table_select_one(
            &table_Wifi_Inet_Config,
            SCHEMA_COLUMN(Wifi_Inet_Config, if_type),
            LTE_TYPE_NAME,
            &wifi_conf);
    if (!res)
    {
        LOGI("%s: %s not found in Wifi_Inet_Config", __func__, LTE_TYPE_NAME);
        return;
    }

    /*
     * The mtu_exists is false when the MTU has not been configured.
     * This will allow the cloud to set the MTU in the future.
     */
    LOGI("%s: mtu[%d], mtu_present[%d], mtu_exists[%d]",
         __func__,
         wifi_conf.mtu,
         wifi_conf.mtu_present,
         wifi_conf.mtu_exists);
    if (!wifi_conf.mtu_exists)
    {
        wifi_conf._partial_update = true;
        SCHEMA_SET_INT(wifi_conf.mtu, DEFAULT_LTE_MTU);
        res = ovsdb_table_update_where_f(
                &table_Wifi_Inet_Config,
                ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Inet_Config, if_type), LTE_TYPE_NAME),
                &wifi_conf,
                filter);
        if (!res)
        {
            LOGI("%s: update mtu failed, mtu[%d]", __func__, wifi_conf.mtu);
        }
        else
        {
            LOGI("%s: res[%d], mtu[%d]", __func__, res, wifi_conf.mtu);
        }
    }
}

void cellm_handle_nm_update_wwan0(
        struct schema_Wifi_Inet_State *old_inet_state,
        struct schema_Wifi_Inet_State *inet_state)
{
    char *null_inet_addr = "0.0.0.0";
    cellm_mgr_t *mgr = cellm_get_mgr();
    int res;

    LOGI("%s: old: if_name=%s, enabled=%d inet_addr=%s, new: if_name=%s, enabled=%d, inet_addr=%s",
         __func__,
         old_inet_state->if_name,
         old_inet_state->enabled,
         old_inet_state->inet_addr,
         inet_state->if_name,
         inet_state->enabled,
         inet_state->inet_addr);
    if (!mgr->cellm_route)
    {
        LOGE("%s: lte_route NULL", __func__);
        return;
    }

    STRSCPY(mgr->cellm_route->cellm_ip_addr, inet_state->inet_addr);
    cellm_ovsdb_cmu_insert_lte(mgr);
    res = strncmp(inet_state->inet_addr, null_inet_addr, strlen(inet_state->inet_addr));
    if (!res)
    {
        if (mgr->cellm_state != CELLM_STATE_DOWN)
        {
            LOGI("%s: wwan0 IP address[%s], setting CELLM_STATE_DOWN", __func__, inet_state->inet_addr);
            cellm_set_state(CELLM_STATE_DOWN);
        }
    }
    else
    {
        if (mgr->cellm_state != CELLM_STATE_UP)
        {
            LOGI("%s: wwan0 IP address[%s], setting CELLM_STATE_UP", __func__, inet_state->inet_addr);
            cellm_set_state(CELLM_STATE_UP);
        }
    }
}

void callback_Wifi_Inet_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Inet_State *old_inet_state,
        struct schema_Wifi_Inet_State *inet_state)
{
    int rc;

    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            rc = strncmp(old_inet_state->if_type, LTE_TYPE_NAME, strlen(old_inet_state->if_type));
            if (!rc)
            {
                LOGI("%s: OVSDB_UPDATE_DEL: %s, CELLM_STATE_DOWN", __func__, old_inet_state->if_name);
                cellm_set_state(CELLM_STATE_DOWN);
            }
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            rc = strncmp(inet_state->if_type, LTE_TYPE_NAME, strlen(inet_state->if_type));
            if (!rc)
            {
                LOGI("%s: OVSDB_UPDATE_MODIFY", __func__);
                cellm_handle_nm_update_wwan0(old_inet_state, inet_state);
            }

            break;
    }
}

void callback_Wifi_Inet_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Inet_Config *old_inet_config,
        struct schema_Wifi_Inet_Config *inet_config)
{
    int rc;

    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            break;

        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            rc = strncmp(inet_config->if_type, LTE_TYPE_NAME, strlen(inet_config->if_type));
            if (!rc)
            {
                cellm_check_lte_mtu();
            }

            break;
    }
}

void cellm_handle_cm_update_wan_priority(struct schema_Connection_Manager_Uplink *uplink)
{
    cellm_mgr_t *mgr;
    cellm_route_info_t *route;
    int res;

    mgr = cellm_get_mgr();
    route = mgr->cellm_route;
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

void callback_Connection_Manager_Uplink(
        ovsdb_update_monitor_t *mon,
        struct schema_Connection_Manager_Uplink *old_uplink,
        struct schema_Connection_Manager_Uplink *uplink)
{
    int res;

    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            res = strncmp(uplink->if_type, LTE_TYPE_NAME, strlen(uplink->if_name));
            if (res == 0)
            {
                cellm_set_wan_state(CELLM_WAN_STATE_UP);
            }
            break;

        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            /* Check to see if we need to update our WAN priority */
            cellm_handle_cm_update_wan_priority(uplink);

            break;
    }
}

void cellm_handle_wan_rc_update(
        struct schema_Wifi_Route_Config *old_route_config,
        struct schema_Wifi_Route_Config *route_config)
{
    cellm_mgr_t *mgr = cellm_get_mgr();
    char *default_mask = "0.0.0.0";
    int res;

    LOGI("%s: if_name=%s, dest_addr=%s, dest_gw=%s, dest_mask=%s",
         __func__,
         route_config->if_name,
         route_config->dest_addr,
         route_config->gateway,
         route_config->dest_mask);
    res = strncmp(route_config->dest_mask, default_mask, strlen(route_config->dest_mask));
    if (res == 0)
    {
        cellm_update_wan_route(mgr, route_config);
    }
}

void cellm_handle_lte_rc_update(
        struct schema_Wifi_Route_Config *old_route_config,
        struct schema_Wifi_Route_Config *route_config)
{
    cellm_mgr_t *mgr = cellm_get_mgr();
    char *default_mask = "0.0.0.0";
    char *null_gateway = "0.0.0.0";
    int res;

    LOGI("%s: if_name=%s, dest_addr=%s, dest_gw=%s, dest_mask=%s",
         __func__,
         route_config->if_name,
         route_config->dest_addr,
         route_config->gateway,
         route_config->dest_mask);
    res = strncmp(route_config->dest_mask, default_mask, strlen(route_config->dest_mask));
    if (res == 0)
    {
        cellm_update_route(
                mgr,
                route_config->if_name,
                route_config->dest_addr,
                route_config->gateway,
                route_config->dest_mask);
    }

    res = strncmp(route_config->gateway, null_gateway, strlen(route_config->gateway));
    if (res)
    {
        LOGI("%s: GW[%s], CELLM_STATE_UP", __func__, route_config->gateway);
        cellm_set_state(CELLM_STATE_UP);
    }
}

void cellm_ovsdb_update_awlan_node(struct schema_AWLAN_Node *new)
{
    int i;
    cellm_mgr_t *mgr = cellm_get_mgr();

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

void callback_AWLAN_Node(ovsdb_update_monitor_t *mon, struct schema_AWLAN_Node *old, struct schema_AWLAN_Node *new)
{
    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            cellm_ovsdb_update_awlan_node(new);
            break;
    }
}

int cellm_ovsdb_update_wifi_route_config_metric(cellm_mgr_t *mgr, char *if_name, uint32_t metric)
{
    struct schema_Wifi_Route_Config route_config;
    cellm_route_info_t *route_info;
    int res;

    route_info = mgr->cellm_route;
    if (!route_info)
    {
        LOGE("%s: route_info not set", __func__);
        return -1;
    }

    res = ovsdb_table_select_one(
            &table_Wifi_Route_Config,
            SCHEMA_COLUMN(Wifi_Route_Config, if_name),
            if_name,
            &route_config);
    if (!res)
    {
        LOGI("%s: %s not found in Wifi_Route_Config", __func__, if_name);
        return -1;
    }

    LOGI("%s: update %s Wifi_Route_Config settings: subnet[%s], netmask[%s] gw[%s], metric[%d]",
         __func__,
         route_config.if_name,
         route_config.dest_addr,
         route_config.dest_mask,
         route_config.gateway,
         metric);

    route_config._partial_update = true;
    SCHEMA_SET_INT(route_config.metric, metric);
    res = ovsdb_table_upsert_simple(
            &table_Wifi_Route_Config,
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

void callback_Wifi_Route_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Route_Config *old_route_config,
        struct schema_Wifi_Route_Config *route_config)
{
    char *if_type;
    int res;

    if_type = cellm_ovsdb_get_if_type(route_config->if_name);
    LOGI("%s: if_type[%s]", __func__, if_type);
    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            LOGD("%s: OVSDB_UPDATE_DEL: if_name=%s, dest_addr=%s, dest_gw=%s, dest_mask=%s",
                 __func__,
                 old_route_config->if_name,
                 old_route_config->dest_addr,
                 old_route_config->gateway,
                 old_route_config->dest_mask);
            if (if_type == NULL) return;

            res = strncmp(if_type, ETH_TYPE_NAME, strlen(if_type));
            if (res == 0)
            {
                cellm_set_wan_state(CELLM_WAN_STATE_DOWN);
            }
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if (if_type == NULL) return;

            res = strncmp(if_type, ETH_TYPE_NAME, strlen(if_type));
            if (res == 0)
            {
                cellm_handle_wan_rc_update(old_route_config, route_config);
            }

            res = strncmp(if_type, LTE_TYPE_NAME, strlen(if_type));
            if (res == 0)
            {
                cellm_handle_lte_rc_update(old_route_config, route_config);
            }
            break;
    }

    FREE(if_type);
}

void callback_Manager(
        ovsdb_update_monitor_t *mon,
        struct schema_Manager *old_manager_config,
        struct schema_Manager *manager_config)
{
    int i;
    int res;
    int rc;
    cellm_mgr_t *mgr = cellm_get_mgr();
    int status_cnt = 0;
    struct schema_Manager manager;
    char *status;
    char state[64];
    int status_size;

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        return;
    }

    if (manager_config->inactivity_probe > LTEM_INACTIVITY_PROBE)
    {
        rc = ovsdb_table_select_one_where(&table_Manager, NULL, &manager);
        if (rc)
        {
            MEMZERO(manager);
            SCHEMA_SET_INT(manager.inactivity_probe, LTEM_INACTIVITY_PROBE);
            rc = ovsdb_table_update(&table_Manager, &manager);
            if (rc)
            {
                LOGI("%s: Set inactivity_probe to %d", __func__, manager.inactivity_probe);
            }
        }
    }

    MEMZERO(state);
    status_size = ARRAY_SIZE(manager_config->status);
    for (i = 0; i < status_size; i++)
    {
        status = manager_config->status[i];
        if (status[0] == '\0')
        {
            break;
        }
        else
        {
            status_cnt++;
            if (!strncmp(status, "VOID", strlen(status)))
            {
                STRSCPY(state, status);
                break;
            }
            if (!strncmp(status, "BACKOFF", strlen(status)))
            {
                STRSCPY(state, status);
                break;
            }
            if (!strncmp(status, "CONNECTING", strlen(status)))
            {
                STRSCPY(state, status);
                break;
            }
            if (!strncmp(status, "ACTIVE", strlen(status)))
            {
                STRSCPY(state, status);
                break;
            }
            if (!strncmp(status, "IDLE", strlen(status)))
            {
                STRSCPY(state, status);
                if ((mgr->wan_state == CELLM_WAN_STATE_UP) && mgr->cellm_state == CELLM_STATE_UP)
                {
                    res = cellm_wan_healthcheck(mgr);
                    if (res)
                    {
                        if (mgr->wan_failure_count >= WAN_L3_FAIL_LIMIT)
                        {
                            res = cellm_set_route_preferred(mgr);
                            if (res < 0)
                            {
                                LOGI("%s: Set LTE preferred: failed", __func__);
                            }
                            else
                            {
                                LOGI("%s: LTE set as preferred route, fail count[%d]",
                                     __func__,
                                     mgr->wan_failure_count);
                                cellm_set_wan_state(CELLM_WAN_STATE_DOWN);
                            }
                        }
                    }
                }
                break;
            }
        }
    }

    LOGD("%s: status_cnt[%d] state[%s] connected[%d]", __func__, status_cnt, state, manager_config->is_connected);
}

void cellm_ovsdb_set_v6_failover(cellm_mgr_t *mgr)
{
    struct schema_IPv6_RouteAdv ipv6_routeAdv;
    struct schema_IPv6_Prefix ipv6_prefix;
    int rc;

    rc = ovsdb_table_select_one_where(&table_IPv6_RouteAdv, NULL, &ipv6_routeAdv);
    if (rc)
    {
        if (!ipv6_routeAdv.default_lifetime_exists)
        {
            mgr->ipv6_ra_default_lifetime = -1;
        }
        else
        {
            mgr->ipv6_ra_default_lifetime = ipv6_routeAdv.default_lifetime;
        }
        SCHEMA_SET_INT(ipv6_routeAdv.default_lifetime, 0);
        rc = ovsdb_table_update(&table_IPv6_RouteAdv, &ipv6_routeAdv);
        if (!rc)
        {
            LOGI("%s: Set V6 default_lifetime failed", __func__);
        }
        else
        {
            LOGI("%s: status[%s], managed[%d], preferred_router[%s], default_lifetime[%d]",
                 __func__,
                 ipv6_routeAdv.status,
                 ipv6_routeAdv.managed,
                 ipv6_routeAdv.preferred_router,
                 ipv6_routeAdv.default_lifetime);
        }
    }

    rc = ovsdb_table_select_one_where(&table_IPv6_Prefix, NULL, &ipv6_prefix);
    if (rc)
    {
        if (ipv6_prefix.valid_lifetime_exists)
        {
            STRSCPY(mgr->ipv6_prefix_valid_lifetime, ipv6_prefix.valid_lifetime);
            SCHEMA_SET_STR(ipv6_prefix.valid_lifetime, "0");
            rc = ovsdb_table_update(&table_IPv6_Prefix, &ipv6_prefix);
            if (!rc)
            {
                LOGI("%s: Set V6 valid_lifetime failed", __func__);
            }
            else
            {
                LOGI("%s: enable[%d], preferred_lifetime[%s] valid_lifetime[%s]",
                     __func__,
                     ipv6_prefix.enable,
                     ipv6_prefix.preferred_lifetime,
                     ipv6_prefix.valid_lifetime);
            }
        }
        else
        {
            MEMZERO(mgr->ipv6_prefix_valid_lifetime);
        }
    }
}

void cellm_ovsdb_revert_v6_failover(cellm_mgr_t *mgr)
{
    struct schema_IPv6_RouteAdv ipv6_routeAdv;
    struct schema_IPv6_Prefix ipv6_prefix;
    int rc;

    rc = ovsdb_table_select_one_where(&table_IPv6_RouteAdv, NULL, &ipv6_routeAdv);
    if (rc)
    {
        if (mgr->ipv6_ra_default_lifetime == -1)
        {
            SCHEMA_UNSET_FIELD(ipv6_routeAdv.default_lifetime);
        }
        else
        {
            SCHEMA_SET_INT(ipv6_routeAdv.default_lifetime, mgr->ipv6_ra_default_lifetime);
        }
        rc = ovsdb_table_update(&table_IPv6_RouteAdv, &ipv6_routeAdv);
        if (!rc)
        {
            LOGI("%s: Set V6 default_lifetime failed", __func__);
        }
        else
        {
            LOGI("%s: status[%s], managed[%d], preferred_router[%s], default_lifetime[%d]",
                 __func__,
                 ipv6_routeAdv.status,
                 ipv6_routeAdv.managed,
                 ipv6_routeAdv.preferred_router,
                 ipv6_routeAdv.default_lifetime);
        }
    }

    rc = ovsdb_table_select_one_where(&table_IPv6_Prefix, NULL, &ipv6_prefix);
    if (rc)
    {
        if (mgr->ipv6_prefix_valid_lifetime[0])
        {
            SCHEMA_SET_STR(ipv6_prefix.valid_lifetime, mgr->ipv6_prefix_valid_lifetime);
            rc = ovsdb_table_update(&table_IPv6_Prefix, &ipv6_prefix);
            if (!rc)
            {
                LOGI("%s: Set V6 valid_lifetime failed", __func__);
            }
            else
            {
                LOGI("%s: enable[%d], preferred_lifetime[%s] valid_lifetime[%s]",
                     __func__,
                     ipv6_prefix.enable,
                     ipv6_prefix.preferred_lifetime,
                     ipv6_prefix.valid_lifetime);
            }
        }
    }
}

int cellm_ovsdb_init(void)
{
    LOGI("Initializing LTEM OVSDB tables");

    static char *filter[] = {
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
    OVSDB_TABLE_INIT(Wifi_Route_Config, if_name);
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_INIT_NO_KEY(IPv6_RouteAdv);
    OVSDB_TABLE_INIT_NO_KEY(IPv6_Prefix);
    OVSDB_TABLE_INIT_NO_KEY(Manager);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Lte_Config, false);
    OVSDB_TABLE_MONITOR(Lte_State, false);
    OVSDB_TABLE_MONITOR(Wifi_Inet_State, false);
    OVSDB_TABLE_MONITOR(Wifi_Inet_Config, false);
    OVSDB_TABLE_MONITOR(Connection_Manager_Uplink, false);
    OVSDB_TABLE_MONITOR(Wifi_Route_Config, false);
    OVSDB_TABLE_MONITOR_F(AWLAN_Node, filter);
    OVSDB_TABLE_MONITOR(Manager, false);

    return 0;
}
