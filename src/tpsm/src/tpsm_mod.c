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
 * ===========================================================================
 *  Enabling or disabling modules/services via Node_Config
 * ===========================================================================
 */
#include <jansson.h>
#include <stdbool.h>
#include <stdlib.h>

#include "tpsm_mod.h"

#include "ds_tree.h"
#include "log.h"
#include "module.h"
#include "ovsdb.h"
#include "ovsdb_table.h"
#include "util.h"

#define KEY_ENABLE "enable"
#define VALUE_ENABLE "true"

static ovsdb_table_t table_Node_Config;
static ovsdb_table_t table_Node_State;

/* List of registered modules to be handled via Node_Config: */
static ds_tree_t tpsm_mods_list = DS_TREE_INIT(ds_str_cmp, struct tpsm_mod, _mod_node);

/* Register a module. */
void tpsm_mod_register(struct tpsm_mod *p)
{
    LOG(INFO, "tpsm_mod: Registering module: %s", p->mod_name);
    ds_tree_insert(&tpsm_mods_list, p, (void *)p->mod_name);
}

/* Unregister a module. */
void tpsm_mod_unregister(struct tpsm_mod *p)
{
    LOG(INFO, "tpsm_mod: Un-registering module: %s", p->mod_name);
    ds_tree_remove(&tpsm_mods_list, p);
}

static struct tpsm_mod *tpsm_mod_find(const char *name)
{
    return ds_tree_find(&tpsm_mods_list, (void *)name);
}

static void callback_Node_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Node_Config *old_rec,
        struct schema_Node_Config *config)
{
    LOG(INFO, "callback_Node_Config");
    struct tpsm_mod *mod;
    bool rv = true;

    if (mon->mon_type == OVSDB_UPDATE_ERROR) return;
    if (!config->module_exists) return;
    if (!(config->key_exists && strcmp(config->key, KEY_ENABLE) == 0)) return;

    mod = tpsm_mod_find(config->module);  // Find if module with this name registered
    if (mod == NULL)
    {
        return;
    }

    /* Depending on Node_Config OVSDB update params call module's activate
     * function which implements enabling and disabling the module. */
    if (mon->mon_type == OVSDB_UPDATE_NEW || mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        if (strcmp(config->value, VALUE_ENABLE) == 0)
        {
            rv = mod->mod_activate_fn(mod, true);
        }
        else
        {
            rv = mod->mod_activate_fn(mod, false);
        }
    }
    else if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        rv = mod->mod_activate_fn(mod, false);
    }

    if (!rv)
    {
        LOG(ERROR,
            "Error %s module %s",
            strcmp(config->value, VALUE_ENABLE) == 0 ? "enabling" : "disabling",
            config->module);
    }
}

static bool tpsm_mod_node_state_set(const char *module, const char *key, const char *value, bool persist)
{
    struct schema_Node_State node_state;
    json_t *where;

    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("module", OFUNC_EQ, (char *)module));
    json_array_append_new(where, ovsdb_tran_cond_single("key", OFUNC_EQ, (char *)key));

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, module);
    SCHEMA_SET_STR(node_state.key, key);

    if (value != NULL)
    {
        SCHEMA_SET_STR(node_state.value, value);
        if (persist) SCHEMA_SET_BOOL(node_state.persist, persist);

        ovsdb_table_upsert_where(&table_Node_State, where, &node_state, false);
    }
    else
    {
        ovsdb_table_delete_where(&table_Node_State, where);
    }

    return true;
}

/* This function is expected to be called by the module when it reaches enable
 * or disable state (which might not be directly after an enable/disable request
 * since for some modules this might take time) -- to reflect the real state
 * in Node_State. */
bool tpsm_mod_update_state(struct tpsm_mod *p, bool enabled)
{
    return tpsm_mod_node_state_set(p->mod_name, KEY_ENABLE, (enabled ? VALUE_ENABLE : NULL), false);
}

static void tpsm_mod_ovsdb_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);

    OVSDB_TABLE_MONITOR(Node_Config, false);
}

void tpsm_mod_init(void)
{
    tpsm_mod_ovsdb_init();
}
