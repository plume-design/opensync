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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ev.h>

#include "os.h"
#include "log.h"
#include "kconfig.h"
#include "target.h"
#include "memutil.h"
#include "module.h"
#include "util.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "schema.h"

#include "hw_acc.h"


#define PM_HW_ACC_MODULE            "pm_hw_acc"
#define KEY_HW_ACC_CFG              "enable"
#define KEY_HW_ACC_STATUS           "hw_acc_status"


#define VAL_HW_ACC_OFF              "false"
#define VAL_HW_ACC_ON               "true"

MODULE(pm_hw_acc, pm_hw_acc_init, pm_hw_acc_fini)

static ovsdb_table_t table_Node_Config;
static ovsdb_table_t table_Node_State;

static bool pm_node_state_set(const char *key, const char *value,bool persist)
{
    struct schema_Node_State node_state;
    json_t *where;

    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("module", OFUNC_EQ, PM_HW_ACC_MODULE));
    json_array_append_new(where, ovsdb_tran_cond_single("key", OFUNC_EQ, (char *)key));

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, PM_HW_ACC_MODULE);
    SCHEMA_SET_STR(node_state.key, key);

    if (value != NULL)
    {
        SCHEMA_SET_STR(node_state.value, value);
        if (persist)
            SCHEMA_SET_BOOL(node_state.persist, persist);

        ovsdb_table_upsert_where(&table_Node_State, where, &node_state, false);
    }
    else
    {
        ovsdb_table_delete_where(&table_Node_State, where);
    }
    return true;
}
/* Callback called by PM when there's a Node_Config request to either enable
 * or disable Hardware Acceleration */
static void callback_Node_Config(
    ovsdb_update_monitor_t *mon,
    struct schema_Node_Config *old_rec,
    struct schema_Node_Config *config)
{
    if (mon->mon_type == OVSDB_UPDATE_ERROR)
        return;
    if (config->module_exists && strcmp(config->module, PM_HW_ACC_MODULE) != 0)
        return;

    /* Enabling/Disabling the feature (Cloud): */
    if (strcmp(config->key, KEY_HW_ACC_CFG) == 0)
    {
        if (mon->mon_type == OVSDB_UPDATE_DEL)
            strcpy(config->value, VAL_HW_ACC_OFF);

        // Set enable/disable flag:
        if (strcmp(config->value, VAL_HW_ACC_ON) == 0)
        {
            hw_acc_enable();
            LOG(INFO, "hw_acc: Acceleration enabled.");
            pm_node_state_set(KEY_HW_ACC_CFG, config->value, true);
        }
        else
        {
            hw_acc_disable();
            pm_node_state_set(KEY_HW_ACC_CFG, config->value, false);
            LOG(INFO, "hw_acc: Acceleration disabled.");
        }
    }
}

void pm_hw_acc_init(void *data)
{
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);
    OVSDB_TABLE_MONITOR(Node_Config, true);
    LOG(INFO, "hardware acceleration_cfg: %s()", __func__);
}

void pm_hw_acc_fini(void *data)
{
    LOG(INFO, "Hardware acceleration Finishing: %s", __func__);
}
