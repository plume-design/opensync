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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "osp_led.h"
#include "osp_tm.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"
#include "schema.h"
#include "util.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

#define TM_OVSDBG_MAX_KEY_LEN 32
#define TM_OVSDBG_PREFIX "SPFAN"
#define TM_OVSDBG_STATE "state"

static ovsdb_table_t table_Node_State;
static ovsdb_table_t table_AWLAN_Node;

bool osp_tm_ovsdb_get_thermal_state(int *thermal_state)
{
    static bool init_node_state = false;

    struct schema_Node_State node_state;
    json_t *where;
    json_t *condition;
    char key[TM_OVSDBG_MAX_KEY_LEN];

    if (!init_node_state)
    {
        OVSDB_TABLE_INIT_NO_KEY(Node_State);
        init_node_state = true;
    }

    MEMZERO(node_state);

    where = json_array();

    snprintf(key, sizeof(key), "%s_%s", TM_OVSDBG_PREFIX, TM_OVSDBG_STATE);

    condition = ovsdb_tran_cond_single("key", OFUNC_EQ, key);
    json_array_append_new(where, condition);

    if (!ovsdb_table_select_one_where(&table_Node_State, where, &node_state))
    {
        LOGI("osp_tm: Thermal state value not set; missing '%s' key in 'Node_State'", key);
        *thermal_state = -1;
        return false;
    }

    *thermal_state = atoi(node_state.value);

    return true;
}

bool osp_tm_get_fan_rpm_from_thermal_state(const int state, int *fan_rpm)
{
    int therm_states_cnt;
    const struct osp_tm_therm_state *thermal_states;

    therm_states_cnt = osp_tm_get_therm_states_cnt();

    if ((int)state < 0 || (int)state >= therm_states_cnt)
    {
        LOGI("osp_tm: Invalid thermal state '%d'; value must be between 0 and %d", state, therm_states_cnt);
        *fan_rpm = -1;
        return false;
    }

    thermal_states = osp_tm_get_therm_tbl();
    *fan_rpm = (int)thermal_states[state].fan_rpm;

    return true;
}

bool osp_tm_get_led_state(int *led_state)
{
    static bool init_awlan_node = false;

    struct schema_AWLAN_Node awlan_node;

    if (!init_awlan_node)
    {
        OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
        init_awlan_node = true;
    }

    MEMZERO(awlan_node);

    if (!ovsdb_table_select_one_where(&table_AWLAN_Node, json_array(), &awlan_node))
    {
        LOGI("osp_tm: Cannot get led_state - AWLAN_Node table empty");
        *led_state = -1;
        return false;
    }

    *led_state = (int)osp_led_str_to_state((char *)awlan_node.led_config);

    return true;
}
