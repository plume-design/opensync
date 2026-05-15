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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include "os.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_cache.h"
#include "schema.h"
#include "ds.h"
#include "log.h"
#include "target.h"
#include "kconfig.h"
#include "memutil.h"

#include "pm_erp.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

#define TM_OVSDBG_MAX_KEY_LEN 32

#define TM_OVSDBG_PREFIX      "SPFAN"
#define TM_OVSDBG_STATE       "state"
#define TM_OVSDBG_WIFI        "wifi"
#define TM_OVSDBG_TXCHAINMASK "txchainmask"
#define TM_OVSDBG_RXCHAINMASK "rxchainmask"
#define TM_OVSDBG_TEMPERATURE "temp"
#define TM_OVSDBG_FAN_RPM     "fanrpm"

static int erp_mode = 1;
static int erp_mask = 0;
static int erp_mode_enabled = 0;
static int n_clients = 0;
static char uplink[16];
static int curr_clnt_state = 0;
static int prev_clnt_state = 0;
static struct osp_erp_ctx *ctx_ptr;

static ovsdb_table_t table_Wifi_Radio_State;
static ovsdb_table_t table_Wifi_Radio_Config;
static ovsdb_table_t table_AWLAN_Node;
static ovsdb_table_t table_Node_Config;
static ovsdb_table_t table_Node_State;
static ovsdb_table_t table_Wifi_Associated_Clients;
static ovsdb_table_t table_OVS_MAC_Learning;
static ovsdb_table_t table_Connection_Manager_Uplink;

void pm_erp_set_node_state(const char *module, const char *key, const char *value)
{
    struct schema_Node_State node_state;
    json_t *where;
    json_t *cond;

    where = json_array();

    cond = ovsdb_tran_cond_single("module", OFUNC_EQ, (char *)module);
    json_array_append_new(where, cond);

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, module);
    SCHEMA_SET_STR(node_state.key, key);
    SCHEMA_SET_STR(node_state.value, value);
    ovsdb_table_upsert_where(&table_Node_State, where, &node_state, false);

    return;
}

static int parse_flags(const char *input)
{
    int mask = 0;

    if (input == NULL || *input == '\0') return mask;

    char buf[128];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *token = strtok(buf, ",");
    while (token != NULL)
    {
        if (strcmp(token, "disabled") == 0)
        {
            mask = 0;
        }
        else if (strcmp(token, "enabled") == 0)
        {
            mask |= (RX_CHAINMASK | ETH_PORT);
        }
        else if (strcmp(token, "rx_chainmask") == 0)
        {
            mask |= RX_CHAINMASK;
        }
        else if (strcmp(token, "eth_port") == 0)
        {
            mask |= ETH_PORT;
        }
        token = strtok(NULL, ",");
    }
    return mask;
}

int pm_erp_ovsdb_get_radio_interfaces(struct iface_list *if_list)
{
    struct schema_Wifi_Radio_Config *radio_config;
    void *wrc_node_p;

    int count;
    int i;

    wrc_node_p = ovsdb_table_select_where(&table_Wifi_Radio_Config, NULL, &count);
    LOGD("ERP: Wifi_Radio_Config has %d rows", count);
    if_list->entries = CALLOC(count, sizeof(struct iface_chainmask));
    if (if_list->entries == NULL) return -1;
    if_list->count = count;
    radio_config = (struct schema_Wifi_Radio_Config *)wrc_node_p;
    for (i = 0; i < count; i++)
    {
        strcpy(if_list->entries[i].if_name, radio_config[i].if_name);
        if_list->entries[i].chainmask = radio_config[i].tx_chainmask;
        LOGD("ERP: ovs if_name=%s tX_mask=%d", radio_config[i].if_name, radio_config[i].tx_chainmask);
    }

    FREE(radio_config);  // Or SCHEMA_FREE, depending on your codebase
    return 0;
}

int pm_erp_ovsdb_get_eth_interfaces(struct iface_list *if_list)
{
    int count = 0;
    int i = 0;
    char *token;
    char buf[128];
    char *saveptr;

    strncpy(buf, CONFIG_MANAGER_WANO_IFACE_LIST, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    // 1. Count
    token = strtok_r(buf, " \t", &saveptr);
    while (token)
    {
        count++;
        token = strtok_r(NULL, " \t", &saveptr);
    }

    LOGD("ERP: Uplinks has %d ports", count);

    if_list->entries = CALLOC(count, sizeof(struct iface_chainmask));
    if (if_list->entries == NULL)
    {
        LOGE("ERP: CALLOC error");
        return -1;
    }
    if_list->count = count;

    // 2. Parse list
    strncpy(buf, CONFIG_MANAGER_WANO_IFACE_LIST, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    token = strtok_r(buf, " \t", &saveptr);
    while (token)
    {
        strncpy(if_list->entries[i].if_name, token, sizeof(if_list->entries[i].if_name) - 1);
        if_list->entries[i].if_name[sizeof(if_list->entries[i].if_name) - 1] = '\0';
        if_list->entries[i].chainmask = 0;

        LOGD("ERP: ovs found if_name=%s", token);

        token = strtok_r(NULL, " \t\n", &saveptr);
        i++;
    }

    return 0;
}

int pm_erp_ovsdb_set_radio_txchainmask(const char *if_name, unsigned int txchainmask)
{
    int rv;
    struct schema_Wifi_Radio_Config radio_config;

    char *filter[] = {"+", SCHEMA_COLUMN(Wifi_Radio_Config, tx_chainmask), NULL};

    MEMZERO(radio_config);
    radio_config.tx_chainmask = txchainmask;
    radio_config.tx_chainmask_exists = true;

    rv = ovsdb_table_update_where_f(
            &table_Wifi_Radio_Config,
            ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), if_name),
            &radio_config,
            filter);
    if (rv != 1)
    {
        LOGE("ERP: Could not update txchainmask: %s:%d", if_name, txchainmask);
        return -1;
    }

    return 0;
}

static void callback_Node_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Node_Config *old_rec,
        struct schema_Node_Config *rec)
{
    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGE("ERP: OVSDB_UPDATE_ERROR in Node_Config CB %d", mon->mon_type);
            return;
        case OVSDB_UPDATE_NEW:
            LOGD("ERP: New Node_Config field rec: %s old: %s.", rec->key, old_rec->key);
            if (strcmp(rec->key, "erp_mode") == 0)
            {
                erp_mode_enabled = 1;
                erp_mask = parse_flags(rec->value);
                if (curr_clnt_state)
                    erp_mode = 0;
                else
                    erp_mode = 1;
            }
            break;
        case OVSDB_UPDATE_MODIFY:
            LOGD("ERP: Modified Node_Config field old: %s rec: %s.", old_rec->key, rec->key);
            if (strcmp(rec->key, "erp_mode") == 0)
            {
                erp_mode_enabled = 1;
                erp_mask = parse_flags(rec->value);
                if (curr_clnt_state)
                    erp_mode = 0;
                else
                    erp_mode = 1;
            }
            break;
        case OVSDB_UPDATE_DEL:
            LOGD("ERP: Deleted Node_Config field  old: %s rec: %s.", old_rec->key, rec->key);
            if (strcmp(old_rec->key, "erp_mode") == 0)
            {
                erp_mode_enabled = 0;
                if (!curr_clnt_state) erp_mode = 0;
            }
            break;
    }
    LOGD("ERP: New ERP mask value=0x%x", erp_mask);
}

static void callback_Wifi_Associated_Clients(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Associated_Clients *old_rec,
        struct schema_Wifi_Associated_Clients *conf)
{
    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGE("ERP: OVSDB_UPDATE_ERROR in WAC %d", mon->mon_type);
            return;
        case OVSDB_UPDATE_NEW:
            LOGD("ERP: New Entry WAC");
            n_clients++;
            break;
        case OVSDB_UPDATE_MODIFY:
            LOGD("ERP: Mod Entry WAC");
            break;
        case OVSDB_UPDATE_DEL:
            LOGD("ERP: Del Entry WAC");
            n_clients--;
            break;
    }
    curr_clnt_state = (n_clients > 0) ? 1 : 0;

    if (prev_clnt_state == 0 && curr_clnt_state == 1)
    {
        LOGI("ERP: (Wifi) Transition 0 -> 1\n");
        erp_mode = 0;
        pm_erp_send_event();
    }
    else if (prev_clnt_state == 1 && curr_clnt_state == 0)
    {
        LOGI("ERP: (Wifi) Transition 1 -> 0\n");
        erp_mode = 1;
    }
    prev_clnt_state = curr_clnt_state;
}

static void callback_OVS_MAC_Learning(
        ovsdb_update_monitor_t *mon,
        struct schema_OVS_MAC_Learning *old_rec,
        struct schema_OVS_MAC_Learning *conf)
{
    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGE("ERP: OVSDB_UPDATE_ERROR in OML %d", mon->mon_type);
            return;
        case OVSDB_UPDATE_NEW:
            LOGD("ERP: New Entry in OML, %s, %s, %s", conf->ifname, conf->brname, conf->hwaddr);
            if (strcmp(conf->ifname, uplink)) n_clients++;
            break;
        case OVSDB_UPDATE_MODIFY:
            LOGD("ERP: Mod Entry in OML");
            break;
        case OVSDB_UPDATE_DEL:
            LOGD("ERP: Del Entry in OML, %s, %s, %s", old_rec->ifname, old_rec->brname, old_rec->hwaddr);
            if (strcmp(conf->ifname, uplink)) n_clients--;
            break;
    }
    curr_clnt_state = (n_clients > 0) ? 1 : 0;
    ctx_ptr->curr_clnt_state = (n_clients > 0) ? 1 : 0;

    if (prev_clnt_state == 0 && curr_clnt_state == 1)
    {
        LOGI("ERP: (Eth) Transition 0 -> 1\n");
        erp_mode = 0;
        pm_erp_send_event();
    }
    else if (prev_clnt_state == 1 && curr_clnt_state == 0)
    {
        LOGI("ERP: (Eth) Transition 1 -> 0\n");
        erp_mode = 1;
    }
    prev_clnt_state = curr_clnt_state;
    ctx_ptr->prev_clnt_state = ctx_ptr->curr_clnt_state;
}

static void callback_Connection_Manager_Uplink(
        ovsdb_update_monitor_t *mon,
        struct schema_Connection_Manager_Uplink *old_rec,
        struct schema_Connection_Manager_Uplink *conf)
{
    switch (mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGE("ERP: OVSDB_UPDATE_ERROR in CMU %d", mon->mon_type);
            return;
        case OVSDB_UPDATE_NEW:
            LOGD("ERP: New Entry in CMU");
            LOGD("ERP: Mod Entry in CMU %d %s %s", conf->is_used, conf->if_type, conf->if_name);
        case OVSDB_UPDATE_MODIFY:
            if ((conf->is_used) && (strcmp(conf->if_type, "eth") == 0)) strcpy(uplink, conf->if_name);
            break;
        case OVSDB_UPDATE_DEL:
            LOGD("ERP: Del Entry in CMU");
            break;
    }
}

int pm_erp_ovsdb_init(struct osp_erp_ctx *ctx)
{
    if (ctx)
    {
        ctx_ptr = ctx;
        OVSDB_TABLE_INIT(Wifi_Radio_Config, if_name);
        OVSDB_TABLE_INIT(Wifi_Radio_State, if_name);
        OVSDB_TABLE_INIT(Node_Config, key);
        OVSDB_TABLE_INIT(Node_State, key);
        OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
        OVSDB_TABLE_INIT_NO_KEY(Wifi_Associated_Clients);
        OVSDB_TABLE_INIT_NO_KEY(OVS_MAC_Learning);
        OVSDB_TABLE_INIT_NO_KEY(Connection_Manager_Uplink);

        // init OVSDB monitor callbacks
        OVSDB_TABLE_MONITOR(Node_Config, false);
        OVSDB_TABLE_MONITOR(Wifi_Associated_Clients, false);
        OVSDB_TABLE_MONITOR(OVS_MAC_Learning, false);
        OVSDB_TABLE_MONITOR(Connection_Manager_Uplink, false);
        return 0;
    }
    LOGE("ERP: pm_erp_ovsdb_init error: ctx not initialized.");
    return -1;
}

int pm_erp_get_mask(void)
{
    return erp_mask;
}

bool pm_erp_ovsdb_erp_mode_enabled(void)
{
    return !!erp_mode_enabled;
}

bool pm_erp_ovsdb_erp_mode_ready(void)
{
    return !!erp_mode;
}

const char *pm_erp_get_uplink(void)
{
    return uplink;
}
