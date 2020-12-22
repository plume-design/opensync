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

#include <ev.h>

#include "ds_tree.h"
#include "oms.h"
#include "os.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_utils.h"
#include "schema.h"
#include "log.h"
#include "util.h"

static ovsdb_table_t table_OMS_Config;
static ovsdb_table_t table_Object_Store_State;
static ovsdb_table_t table_AWLAN_Node;

/**
 * @brief add a config entry in the ovsdb object config table
 *
 * @param entry the entry to add
 */
int
oms_add_config_entry(struct oms_config_entry *entry)
{
    struct schema_OMS_Config config;
    const char *key;
    json_t *where;
    json_t *cond;
    bool rc;
    int ret;

    if (entry == NULL) return -1;
    if (entry->object == NULL) return -1;
    if (entry->version == NULL) return -1;

    /* Select ovsdb target based on the object id and version */
    where = json_array();
    cond = ovsdb_tran_cond_single("object_name", OFUNC_EQ, entry->object);
    json_array_append_new(where, cond);
    cond = ovsdb_tran_cond_single("version", OFUNC_EQ, entry->version);
    json_array_append_new(where, cond);

    MEMZERO(config);

    /* Object name */
    key = entry->object;
    if (key != NULL) SCHEMA_SET_STR(config.object_name, key);

    /* Version */
    key = entry->version;
    if (key != NULL) SCHEMA_SET_STR(config.version, key);

    /* other_config, to be processed */

    rc = ovsdb_table_upsert_where(&table_OMS_Config, where, &config, false);
    json_decref(where);

    ret = rc ? 0 : -1;

    return ret;
}


/**
 * @brief delete a config entry from the ovsdb object config table
 *
 * @param entry the entry to delete
 */
int
oms_delete_config_entry(struct oms_config_entry *entry)
{
    json_t *where;
    json_t *cond;
    bool rc;
    int ret;

    if (entry == NULL) return -1;
    if (entry->object == NULL) return -1;
    if (entry->version == NULL) return -1;

    /* Select ovsdb target based on the object id and version */
    where = json_array();
    cond = ovsdb_tran_cond_single("object_name", OFUNC_EQ, entry->object);
    json_array_append_new(where, cond);
    cond = ovsdb_tran_cond_single("version", OFUNC_EQ, entry->version);
    json_array_append_new(where, cond);

    rc = ovsdb_table_delete_where(&table_OMS_Config, where);
    json_decref(where);

    ret = rc ? 0 : -1;

    return ret;
}

/**
 * @brief modify a state entry in the ovsdb object state table based on update_only
 *
 * @param entry the entry to add
 * @param update_only if true only update the state, don't update name, version and fw_integrated
 */

int
oms_modify_state_entry(struct oms_state_entry *entry, bool update_only)
{
    struct schema_Object_Store_State state;
    const char *key;
    json_t *where;
    json_t *cond;
    bool rc;
    int ret;

    if (entry == NULL) return -1;
    if (entry->object == NULL) return -1;
    if (entry->version == NULL) return -1;

    /* Select ovsdb target based on the object id and version */
    where = json_array();
    cond = ovsdb_tran_cond_single("name", OFUNC_EQ, entry->object);
    json_array_append_new(where, cond);
    cond = ovsdb_tran_cond_single("version", OFUNC_EQ, entry->version);
    json_array_append_new(where, cond);

    MEMZERO(state);
    state._partial_update = true;

    /* Populate State */
    key = entry->state;
    if (key != NULL) SCHEMA_SET_STR(state.status, key);

    /* New enty only */
    if (update_only == false)
    {
        /* Object name */
        key = entry->object;
        if (key != NULL) SCHEMA_SET_STR(state.name, key);

        /* Version */
        key = entry->version;
        if (key != NULL) SCHEMA_SET_STR(state.version, key);

        /* Fw integrated */
        SCHEMA_SET_INT(state.fw_integrated, entry->fw_integrated);

        /* Upset new entry */
        rc = ovsdb_table_upsert_where(&table_Object_Store_State, where, &state, false);
    }
    else
    {
        /*
         * Only update not upsert. In case that this entry is already gone
         * usert will triggere error of missing name and version. Avoid with
         * using update instead of upsert.
         */
        rc = ovsdb_table_update_where(&table_Object_Store_State, where, &state);
    }

    json_decref(where);

    ret = rc ? 0 : -1;

    return ret;
}


/**
 * @brief add a state entry in the ovsdb object state table
 *
 * @param entry the entry to add
 */
int
oms_add_state_entry(struct oms_state_entry *entry)
{
    return oms_modify_state_entry(entry, false);
}

/**
 * @brief update only state field of state entry in the ovsdb object state table
 *
 * @param entry the entry to update
 */
int
oms_update_state_entry(struct oms_state_entry *entry)
{
    return oms_modify_state_entry(entry, true);
}

/**
 * @brief delete a state entry from the ovsdb object state table
 *
 * @param entry the entry to delete
 */
int
oms_delete_state_entry(struct oms_state_entry *entry)
{
    json_t *where;
    json_t *cond;
    bool rc;
    int ret;

    if (entry == NULL) return -1;
    if (entry->object == NULL) return -1;
    if (entry->version == NULL) return -1;

    /* Select ovsdb target based on the object id and version */
    where = json_array();
    cond = ovsdb_tran_cond_single("name", OFUNC_EQ, entry->object);
    json_array_append_new(where, cond);
    cond = ovsdb_tran_cond_single("version", OFUNC_EQ, entry->version);
    json_array_append_new(where, cond);

    rc = ovsdb_table_delete_where(&table_Object_Store_State, where);
    json_decref(where);

    ret = rc ? 0 : -1;

    return ret;
}


/**
 * @brief process an ovsdb add config event
 *
 * @param config the ovsdb entry to process
 * Allocates resources for the entry and stores it.
 */
void
oms_ovsdb_add_config_entry(struct schema_OMS_Config *config)
{
    struct oms_config_entry *entry;
    struct oms_config_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    char *object;
    bool rc;

    mgr = oms_get_mgr();
    rc = true;
    tree = &mgr->config;

    if (!config->object_name_present) return;
    if (!config->version_present) return;

    /* Get the object identifier */
    object = config->object_name;

    /* Check if we are interested in this object id */
    if (mgr->accept_id) rc = mgr->accept_id(object);
    if (!rc) return;

    /* Look up the entry. If found, bail */
    lookup.object = object;
    lookup.version = config->version;
    entry = ds_tree_find(tree, &lookup);
    if (entry != NULL) return;

    /* Allocate and initialize the entry */
    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) return;

    entry->object = strdup(config->object_name);
    if (entry->object == NULL) goto err_free_entry;

    entry->version = strdup(config->version);
    if (entry->version == NULL) goto err_free_object;


    if (config->other_config_present && config->other_config_len)
    {
        entry->other_config = schema2tree(sizeof(config->other_config_keys[0]),
                                          sizeof(config->other_config[0]),
                                          config->other_config_len,
                                          config->other_config_keys,
                                          config->other_config);
        if (entry->other_config == NULL) goto err_free_version;
    }

    /* Store the entry */
    ds_tree_insert(tree, entry, entry);

    /* Notify the manager */
    if (mgr->config_cb) (mgr->config_cb(entry, OVSDB_UPDATE_NEW));

    return;

err_free_version:
    free(entry->version);

err_free_object:
    free(entry->object);

err_free_entry:
    free(entry);

    LOGE("%s: entry creation failed", __func__);
    return;
}


/**
 * @brief process an ovsdb delete config event
 *
 * @param config the ovsdb entry to process
 * Frees resources for the entry and deletes it.
 */
void
oms_ovsdb_del_config_entry(struct schema_OMS_Config *config)
{
    struct oms_config_entry *entry;
    struct oms_config_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    char *object;
    bool rc;

    mgr = oms_get_mgr();
    rc = true;
    tree = &mgr->config;

    /* Get the object identifier */
    object = config->object_name;

    /* Check if we are interested in this object id */
    if (mgr->accept_id) rc = mgr->accept_id(object);
    if (!rc) return;

    /* Look up the entry. If not found, bail */
    lookup.object = object;
    lookup.version = config->version;
    entry = ds_tree_find(tree, &lookup);
    if (entry == NULL) return;

    /* Remove entry */
    ds_tree_remove(tree, entry);

    /* Notify the manager */
    if (mgr->config_cb) (mgr->config_cb(entry, OVSDB_UPDATE_DEL));

    /* Free the entry */
    oms_free_config_entry(entry);

}


/**
 * @brief add or update an oms config entry
 *
 * @param old_rec the previous ovsdb oms config info about the entry
 *        to add/update
 * @param config the ovsdb oms config info about the entry to add/update
 *
 * If the entry's object_id or version is flagged as changed,
 * remove the old entry and add a new one.
 * Else update the entry.
 */
void
oms_ovsdb_update_config_entry(struct schema_OMS_Config *old_rec,
                              struct schema_OMS_Config *config)
{
    if (config->object_name_changed || config->version_changed)
    {
        /* Remove the old record */
        oms_ovsdb_del_config_entry(old_rec);
    }

    /* Add/update the new record */
    oms_ovsdb_add_config_entry(config);

    return;
}


/**
 * @brief OMS_Config events callback
 */
static void
callback_OMS_Config(ovsdb_update_monitor_t *mon,
                    struct schema_OMS_Config *old_rec,
                    struct schema_OMS_Config *oms_config)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        oms_ovsdb_add_config_entry(oms_config);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        oms_ovsdb_del_config_entry(oms_config);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        oms_ovsdb_update_config_entry(old_rec, oms_config);
        return;
    }
}


/**
 * @brief process an ovsdb add state event
 *
 * @param state the ovsdb entry to process
 * Allocates resources for the entry and stores it.
 */
void
oms_ovsdb_add_state_entry(struct schema_Object_Store_State *state)
{
    struct oms_config_entry lookup;
    struct oms_state_entry *entry;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    char *object;

    mgr = oms_get_mgr();
    tree = &mgr->state;

    if (!state->name_present) return;
    if (!state->version_present) return;
    if (!state->status_present) return;

    /* Get the object identifier */
    object = state->name;

    /* Look up the entry. If found, bail */
    lookup.object = object;
    lookup.version = state->version;
    entry = ds_tree_find(tree, &lookup);
    if (entry != NULL) return;

    /* Allocate and initialize the entry */
    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) return;

    entry->object = strdup(state->name);
    if (entry->object == NULL) goto err_free_entry;

    entry->version = strdup(state->version);
    if (entry->version == NULL) goto err_free_object;

    entry->state = strdup(state->status);
    if (entry->state == NULL) goto err_free_version;

    /* Store the entry */
    ds_tree_insert(tree, entry, entry);

    /* Notify the manager */
    if (mgr->state_cb) (mgr->state_cb(entry, OVSDB_UPDATE_NEW));

    mgr->num_states++;

    return;

err_free_version:
    free(entry->version);

err_free_object:
    free(entry->object);

err_free_entry:
    free(entry);

    LOGE("%s: entry creation failed", __func__);
    return;
}


/**
 * @brief process an ovsdb delete state event
 *
 * @param state the ovsdb entry to process
 * Frees resources for the entry and deletes it.
 */
void
oms_ovsdb_del_state_entry(struct schema_Object_Store_State *state)
{
    struct oms_state_entry *entry;
    struct oms_state_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;

    mgr = oms_get_mgr();
    tree = &mgr->state;

    /* Get the object identifier */
    if (!state->name_present) return;
    if (!state->version_present) return;

    lookup.object = state->name;
    lookup.version = state->version;

    /* Look up the entry based on its object id. If not found, bail */
    entry = ds_tree_find(tree, &lookup);
    if (entry == NULL) return;

    /* Notify the manager */
    if (mgr->state_cb) (mgr->state_cb(entry, OVSDB_UPDATE_DEL));

    /* remove and free the entry */
    ds_tree_remove(tree, entry);
    oms_free_state_entry(entry);
    mgr->num_states--;
}


void
oms_ovsdb_update_state(struct schema_Object_Store_State *old_rec,
                       struct schema_Object_Store_State *oms_state)
{
    struct oms_state_entry *entry;
    struct oms_state_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;

    mgr = oms_get_mgr();
    tree = &mgr->state;

    /* Get the object identifier */
    if (!oms_state->name_present) return;
    if (!oms_state->version_present) return;

    lookup.object = oms_state->name;
    lookup.version = oms_state->version;

    /* Look up the entry based on its object id. If not found, bail */
    entry = ds_tree_find(tree, &lookup);
    if (entry == NULL) return;

    if (oms_state->status_changed)
    {
        char *previous_state;
        char *new_state;

        /* Get the previous state */
        previous_state = strdup(entry->state);
        if (previous_state == NULL) return;

        /* Get the new state */
        new_state = strdup(oms_state->status);
        if (new_state == NULL) return;

        free(entry->prev_state);
        entry->prev_state = previous_state;

        free(entry->state);
        entry->state = new_state;
    }

    if (mgr->state_cb) (mgr->state_cb(entry, OVSDB_UPDATE_MODIFY));
}


/**
 * @brief add or update an oms state entry
 *
 * @param old_rec the previous ovsdb oms state info about the entry
 *        to add/update
 * @param oms_state the ovsdb oms state info about the entry to add/update
 *
 * If the entry's object_id or version is flagged as changed,
 * remove the old entry and add a new one.
 * Else update the entry.
 */
void
oms_ovsdb_update_state_entry(struct schema_Object_Store_State *old_rec,
                             struct schema_Object_Store_State *oms_state)
{
    if (oms_state->name_changed || oms_state->version_changed)
    {
        /* Remove the old record */
        oms_ovsdb_del_state_entry(old_rec);

        /* Add/update the new record */
        oms_ovsdb_add_state_entry(oms_state);
    }

    oms_ovsdb_update_state(old_rec, oms_state);
}


/**
 * @brief Object_Store_State events callback
 */
static void
callback_Object_Store_State(ovsdb_update_monitor_t *mon,
                            struct schema_Object_Store_State *old_rec,
                            struct schema_Object_Store_State *oms_state)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        oms_ovsdb_add_state_entry(oms_state);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        oms_ovsdb_del_state_entry(oms_state);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        oms_ovsdb_update_state_entry(old_rec, oms_state);
        return;
    }
}

/**
 * @brief gather mqtt records from AWLAN_Node's mqtt headers table
 *
 * Records the mqtt_headers (locationId, nodeId)
 * @param awlan AWLAN_Node record
 */
void
oms_get_awlan_headers(struct schema_AWLAN_Node *awlan)
{
    char *location = "locationId";
    struct str_pair *pair;
    char *node = "nodeId";
    struct oms_mgr *mgr;
    size_t key_size;
    size_t val_size;
    size_t nelems;

    /* Get the manager */
    mgr = oms_get_mgr();

    /* Free previous headers if any */
    free_str_tree(mgr->mqtt_headers);

    /* Get AWLAN_Node's mqtt_headers element size */
    key_size = sizeof(awlan->mqtt_headers_keys[0]);
    val_size = sizeof(awlan->mqtt_headers[0]);

    /* Get AWLAN_Node's number of elements */
    nelems = awlan->mqtt_headers_len;

    mgr->mqtt_headers = schema2tree(key_size, val_size, nelems,
                                    awlan->mqtt_headers_keys,
                                    awlan->mqtt_headers);
    if (mgr->mqtt_headers == NULL) goto err;

    /* Check the presence of locationId in the mqtt headers */
    pair = ds_tree_find(mgr->mqtt_headers, location);
    if (pair == NULL) goto err;
    mgr->location_id = pair->value;

    /* Check the presence of nodeId in the mqtt headers */
    pair = ds_tree_find(mgr->mqtt_headers, node);
    if (pair == NULL) goto err;

    mgr->node_id = pair->value;

    return;

err:
    free_str_tree(mgr->mqtt_headers);
    mgr->mqtt_headers = NULL;
    mgr->location_id = NULL;
    mgr->node_id = NULL;

    return;
}


/**
 * @brief delete recorded mqtt headers
 */
void
oms_rm_awlan_headers(void)
{
    struct oms_mgr *mgr;

    /* Get the manager */
    mgr = oms_get_mgr();

    /* Free previous headers if any */
    free_str_tree(mgr->mqtt_headers);
    mgr->mqtt_headers = NULL;
    mgr->location_id = NULL;
    mgr->node_id = NULL;
}


/**
 * @brief registered callback for AWLAN_Node events
 */
static void
callback_AWLAN_Node(ovsdb_update_monitor_t *mon,
                    struct schema_AWLAN_Node *old_rec,
                    struct schema_AWLAN_Node *awlan)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) oms_get_awlan_headers(awlan);
    if (mon->mon_type == OVSDB_UPDATE_DEL) oms_rm_awlan_headers();
    if (mon->mon_type == OVSDB_UPDATE_MODIFY) oms_get_awlan_headers(awlan);
}


/**
 * @brief Initialize ovsdb tables
 *
 * @param oms_set the set of configuration parameters
 */
void
oms_ovsdb_init(struct oms_ovsdb_set *oms_set)
{
    struct oms_mgr *mgr;

    OVSDB_TABLE_INIT_NO_KEY(OMS_Config);
    OVSDB_TABLE_INIT_NO_KEY(Object_Store_State);

    if (oms_set->monitor_config)
    {
        OVSDB_TABLE_MONITOR(OMS_Config, false);
    }

    if (oms_set->monitor_state)
    {
        OVSDB_TABLE_MONITOR(Object_Store_State, false);
    }

    if (oms_set->monitor_awlan)
    {
        OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
        OVSDB_TABLE_MONITOR(AWLAN_Node, false);
    }

    mgr = oms_get_mgr();
    mgr->accept_id = oms_set->accept_id;
    mgr->config_cb = oms_set->config_cb;
    mgr->state_cb = oms_set->state_cb;
    mgr->report_cb = oms_set->report_cb;
}
