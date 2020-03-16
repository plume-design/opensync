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

#include "pm_tm.h"

#define MODULE_ID               LOG_MODULE_ID_OVSDB

#define TM_OVSDBG_MAX_KEY_LEN   32

#define TM_OVSDBG_PREFIX        "SPFAN"
#define TM_OVSDBG_STATE         "state"
#define TM_OVSDBG_WIFI          "wifi"
#define TM_OVSDBG_TXCHAINMASK   "txchainmask"
#define TM_OVSDBG_TEMPERATURE   "temp"
#define TM_OVSDBG_FAN_RPM       "fanrpm"

static ovsdb_table_t table_Wifi_Radio_State;
static ovsdb_table_t table_Wifi_Radio_Config;
static ovsdb_table_t table_AWLAN_Node;
static ovsdb_table_t table_Node_Config;
static ovsdb_table_t table_Node_State;

static void callback_Wifi_Radio_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Radio_State *old_rec,
        struct schema_Wifi_Radio_State *rec,
        ovsdb_cache_row_t *row)
{
    return;
}

static void callback_Node_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Node_Config *old_rec,
        struct schema_Node_Config *rec,
        ovsdb_cache_row_t *row)
{
    return;
}

bool pm_tm_ovsdb_is_radio_enabled(const char *if_name)
{
    struct schema_Wifi_Radio_State *state;

    state = ovsdb_cache_find_by_key(&table_Wifi_Radio_State, if_name);
    if (state == NULL)
    {
        return false;
    }

    if (state->enabled != true)
    {
        return false;
    }

    return true;
}

int pm_tm_ovsdb_set_radio_txchainmask(const char *if_name, unsigned int txchainmask)
{
    int rv;
    struct schema_Wifi_Radio_Config radio_config;

    char *filter[] = {"+", SCHEMA_COLUMN(Wifi_Radio_Config, thermal_tx_chainmask), NULL};

    MEMZERO(radio_config);
    radio_config.thermal_tx_chainmask = txchainmask;
    radio_config.thermal_tx_chainmask_exists = true;

    rv = ovsdb_table_update_where_f(
            &table_Wifi_Radio_Config,
            ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), if_name),
            &radio_config,
            filter);
    if (rv != 1)
    {
        LOGE("TM: Could not update txchainmask: %s:%d", if_name, txchainmask);
        return -1;
    }

    return 0;
}

int pm_tm_ovsdb_init(struct osp_tm_ctx *ctx)
{
    OVSDB_TABLE_INIT(Wifi_Radio_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Radio_State, if_name);

    // init table for saving LED state
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);

    // init OVSDB monitor callbacks
    OVSDB_CACHE_MONITOR(Wifi_Radio_State, false);

    // init tables for debugging/testing purposes
    OVSDB_TABLE_INIT(Node_Config, key);
    OVSDB_TABLE_INIT(Node_State, key);
    OVSDB_CACHE_MONITOR(Node_Config, false);

    return 0;
}

int pm_tm_ovsdb_thermtbl_get_radio_temp(unsigned int state, unsigned int radio_idx, int *temp)
{
    struct schema_Node_Config *node_config;
    char key[TM_OVSDBG_MAX_KEY_LEN];

    snprintf(key, TM_OVSDBG_MAX_KEY_LEN, "%s_%s%d_%s%d_%s",
            TM_OVSDBG_PREFIX, TM_OVSDBG_STATE, state, TM_OVSDBG_WIFI, radio_idx, TM_OVSDBG_TEMPERATURE);

    node_config = ovsdb_cache_find_by_key(&table_Node_Config, key);
    if (node_config == NULL)
    {
        return -1;
    }

    if (sscanf(node_config->value, "%d", temp) != 1)
    {
        return -1;
    }

    return 0;
}

int pm_tm_ovsdb_thermtbl_get_radio_txchainmask(unsigned int state, unsigned int radio_idx, unsigned int *txchainmask)
{
    struct schema_Node_Config *node_config;
    char key[TM_OVSDBG_MAX_KEY_LEN];

    snprintf(key, sizeof(key), "%s_%s%d_%s%d_%s",
            TM_OVSDBG_PREFIX, TM_OVSDBG_STATE, state, TM_OVSDBG_WIFI, radio_idx, TM_OVSDBG_TXCHAINMASK);

    node_config = ovsdb_cache_find_by_key(&table_Node_Config, key);
    if (node_config == NULL)
    {
        return -1;
    }

    if (sscanf(node_config->value, "%u", txchainmask) != 1)
    {
        return -1;
    }

    return 0;
}

int pm_tm_ovsdb_thermtbl_get_fan_rpm(unsigned int state, unsigned int *rpm)
{
    struct schema_Node_Config *node_config;
    char key[TM_OVSDBG_MAX_KEY_LEN];

    snprintf(key, sizeof(key), "%s_%s%d_%s",
            TM_OVSDBG_PREFIX, TM_OVSDBG_STATE, state, TM_OVSDBG_FAN_RPM);

    node_config = ovsdb_cache_find_by_key(&table_Node_Config, key);
    if (node_config == NULL)
    {
        return -1;
    }

    if (sscanf(node_config->value, "%u", rpm) != 1)
    {
        return -1;
    }

    return 0;
}

int pm_tm_ovsdb_set_state(unsigned int state)
{
    int rv;
    struct schema_Node_State node_state;
    char key[TM_OVSDBG_MAX_KEY_LEN];

    snprintf(key, sizeof(key), "%s_%s", TM_OVSDBG_PREFIX, TM_OVSDBG_STATE);

    MEMZERO(node_state);
    strscpy(node_state.module, "TM", sizeof(node_state.module));
    strscpy(node_state.key, key, sizeof(node_state.key));
    snprintf(node_state.value, sizeof(node_state), "%u", state);

    rv = ovsdb_table_upsert_where(
            &table_Node_State,
            ovsdb_where_simple(SCHEMA_COLUMN(Node_State, key), key),
            &node_state,
            false);
    if (rv != 1)
    {
        return rv;
    }

    return 0;
}

int pm_tm_ovsdb_set_led_state(enum osp_led_state led_state, bool clear)
{
    char *filter[] = {"+", SCHEMA_COLUMN(AWLAN_Node, led_config), NULL};
    struct schema_AWLAN_Node awlan_node;

    MEMZERO(awlan_node);

    SCHEMA_KEY_VAL_APPEND(awlan_node.led_config, "state", osp_led_state_to_str(led_state));
    if (clear == true)
    {
        SCHEMA_KEY_VAL_APPEND(awlan_node.led_config, "clear", "true");
    }

    if (!ovsdb_table_update_f(&table_AWLAN_Node, &awlan_node, filter))
    {
        return -1;
    }

    return 0;
}

#ifdef PM_TM_DEBUG

int pm_tm_ovsdb_dbg_get_temperature(unsigned int radio_idx, int *temp)
{
    struct schema_Node_Config *node_config;
    char key[TM_OVSDBG_MAX_KEY_LEN];

    snprintf(key, TM_OVSDBG_MAX_KEY_LEN, "%s_%s%d_%s",
            TM_OVSDBG_PREFIX, TM_OVSDBG_WIFI, radio_idx, TM_OVSDBG_TEMPERATURE);

    node_config = ovsdb_cache_find_by_key(&table_Node_Config, key);
    if (node_config == NULL)
    {
        return -1;
    }

    if (sscanf(node_config->value, "%d", temp) != 1)
    {
        return -1;
    }

    return 0;
}

int pm_tm_ovsdb_dbg_get_fan_rpm(unsigned int *rpm)
{
    struct schema_Node_Config *node_config;
    char key[TM_OVSDBG_MAX_KEY_LEN];

    snprintf(key, sizeof(key), "%s_%s",
            TM_OVSDBG_PREFIX, TM_OVSDBG_FAN_RPM);

    node_config = ovsdb_cache_find_by_key(&table_Node_Config, key);
    if (node_config == NULL)
    {
        return -1;
    }

    if (sscanf(node_config->value, "%u", rpm) != 1)
    {
        return -1;
    }

    return 0;
}

#endif /* PM_TM_DEBUG */
