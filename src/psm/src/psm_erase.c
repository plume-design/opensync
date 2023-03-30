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

#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "psm.h"
#include "ps_mgmt.h"
#include "schema.h"

#define PSM_MODULE_NAME                 "PSM"
#define KEY_ERASE_WAN_PS                "erase_wan_ps"
#define KEY_ERASE_LTE_PS                "erase_lte_ps"
#define KEY_ERASE_GW_OFF_PS             "erase_gw_offline_ps"
#define KEY_ERASE_ALL_PS                "erase_all_ps"
#define KEY_ERASE_OP_STATUS             "erase_op_status"
#define VAL_ERASE_PS_TRUE               "true"

static ovsdb_table_t table_Node_Config;
static ovsdb_table_t table_Node_State;

static void write_erase_op_status(int error_cnt)
{
    struct schema_Node_State node_state;
    json_t *where;
    char status[32];

    LOG(INFO, "Writing PSM erase status into Node_State. Number of errors: %d", error_cnt);

    /* Delete status in case it already exists */
    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("module", OFUNC_EQ, (char *)PSM_MODULE_NAME));
    json_array_append_new(where, ovsdb_tran_cond_single("key", OFUNC_EQ, (char *)KEY_ERASE_OP_STATUS));

    ovsdb_table_delete_where(&table_Node_State, where);

    /* Insert status of erase operation into Node_State */
    if (error_cnt > 0) {
        snprintf(status, sizeof(status), "false");
    } else {
        snprintf(status, sizeof(status), "true");
    }

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, PSM_MODULE_NAME);
    SCHEMA_SET_STR(node_state.key, KEY_ERASE_OP_STATUS);
    SCHEMA_SET_STR(node_state.value, status);
    ovsdb_table_insert(&table_Node_State, &node_state);
}

void callback_Node_Config(ovsdb_update_monitor_t *mon,
                          struct schema_Node_Config *old,
                          struct schema_Node_Config *config)
{
    int error_cnt = 0;

    if (mon->mon_type == OVSDB_UPDATE_ERROR)
        return;

    if (!(config->module_exists && strcmp(config->module, PSM_MODULE_NAME) == 0))
        return;

    if (strcmp(config->value, VAL_ERASE_PS_TRUE) != 0)
    {
        LOG(DEBUG, "Erase entire persistent storage flag set to value other than true. Returning.");
        return;
    }

    LOG(INFO, "Erasing persistent storage with Node_Config key: %s", config->key);

    /* Erase different categories of persistent storage based on flags pushed by the cloud: */
    if (strcmp(config->key, KEY_ERASE_ALL_PS) == 0 || strcmp(config->key, KEY_ERASE_WAN_PS) == 0)
    {
        /* WAN Config erase function returns -1 on error */
        if (ps_mgmt_erase_wan_config() < 0)
        {
            LOG(ERR, "Failed to erase WAN config from persistent storage.");
            error_cnt++;
        }
    }

    if (strcmp(config->key, KEY_ERASE_ALL_PS) == 0 || strcmp(config->key, KEY_ERASE_LTE_PS) == 0)
        /* LTE Config erase function returns -1 on error */
        if (ps_mgmt_erase_lte_config() < 0)
        {
            LOG(ERR, "Failed to erase LTE config from persistent storage.");
            error_cnt++;
        }

    if (strcmp(config->key, KEY_ERASE_ALL_PS) == 0 || strcmp(config->key, KEY_ERASE_GW_OFF_PS) == 0)
    {
        /* GW Offline erase function returns false on error */
        if (!ps_mgmt_erase_gw_offline_config()) {
            LOG(ERR, "Failed to erase LTE config from persistent storage.");
            error_cnt++;
        }
    }

    write_erase_op_status(error_cnt);
}

void psm_erase_config_init(void)
{
    LOG(INFO, "Initializing PSM erase config monitoring.");
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);
    OVSDB_TABLE_MONITOR(Node_Config, false);
}
