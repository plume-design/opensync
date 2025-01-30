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

#include <string.h>
#include <jansson.h>

#include "ff_provider_ps.h"
#include "log.h"
#include "memutil.h"
#include "module.h"
#include "osp_ps.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"
#include "schema.h"

#define OVSDB_FF_KEY "feature_flags"
#define PM_MODULE_NAME "PM"
#define LOG_PREFIX "[PM:FF] "


static ff_provider_ps_t *ff_provider_ps = NULL;
static ovsdb_table_t table_Node_Config;
static ovsdb_table_t table_Node_State;

static void node_state_set(const char *key, const char *value, bool persist)
{
    struct schema_Node_State node_state;
    json_t *where;

    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("module", OFUNC_EQ, PM_MODULE_NAME));
    json_array_append_new(where, ovsdb_tran_cond_single("key", OFUNC_EQ, (char *)key));

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, PM_MODULE_NAME);
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
}

static void update_flags(struct schema_Node_Config *conf)
{
    bool table_select_result_ok;
    struct schema_Node_Config nconfig;

    MEMZERO(nconfig);

    table_select_result_ok =
        ovsdb_table_select_one(&table_Node_Config, SCHEMA_COLUMN(Node_Config, key),
                               "feature_flags", &nconfig);
    if (!table_select_result_ok)
    {
        LOGE(LOG_PREFIX "Failed to fetch feature flags from OVSDB Node Config!");
        return;
    }

    if (ff_provider_ps->set_flags_fn(ff_provider_ps, conf->value))
        node_state_set(OVSDB_FF_KEY, conf->value, (conf->persist_exists && conf->persist));
}

static void callback_Node_Config(
    ovsdb_update_monitor_t *mon,
    struct schema_Node_Config *old_rec,
    struct schema_Node_Config *conf)
{
    switch(mon->mon_type)
    {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW(LOG_PREFIX "mon upd error: %d", mon->mon_type);
            return;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
        case OVSDB_UPDATE_DEL:
            if(strcmp(OVSDB_FF_KEY, conf->key) == 0)
                update_flags(conf);
    }
}

static void pm_ff_ovsdb_ps_init(void *data)
{
    char *list_of_flags = NULL;
    (void)data;

    ff_provider_ps = ff_provider_ps_get();

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);

    list_of_flags = ff_provider_ps->get_flags_fn(ff_provider_ps);
    node_state_set(OVSDB_FF_KEY, list_of_flags, false);
    ff_provider_ps->free_flags_data_fn(&list_of_flags);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Node_Config, false);
}

static void pm_ff_ovsdb_ps_fini(void *data)
{
    (void)data;
}

MODULE(ff_update_flags_from_ovsdb, pm_ff_ovsdb_ps_init, pm_ff_ovsdb_ps_fini)
