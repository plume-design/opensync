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

#include <jansson.h>
#include <memutil.h>
#include <os_tr181_types.h>
#include <os_tr181_val.h>
#include <os_tr181_val_json.h>
#include <ovsdb_sync.h>
#include <ox_log.h>
#include <ox_ovsdb_monitor.h>
#include <ox_ovsdb_type.h>
#include <ox_route.h>
#include <ox_table_instance.h>
#include <ox_types.h>
#include <ox_util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FIXME: this does not belong here?
static json_t *ox_ovsdb_sync_get_column(
        const char *table,
        const char *column,
        const char *where_column,
        json_t *where_value_owned)
{
    json_t *where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json(where_column, OFUNC_EQ, where_value_owned);
    json_array_append_new(where_owned, cond_owned);
    cond_owned = NULL;  // moved to where_owned
    json_t *rows_owned = ovsdb_sync_select_where(table, where_owned);
    where_owned = NULL;  // moved to ovsdb_sync_select_where
    {
        char *rows_str_owned = json_dumps(rows_owned, JSON_COMPACT | JSON_ENCODE_ANY);
        LOGD(LOG_PREFIX_FUNC("table=%s column=%s where_column=%s rows=%s"),
             table,
             column,
             where_column,
             rows_str_owned);
        FREE(rows_str_owned);
    }
    json_t *first_borrowed = json_array_get(rows_owned, 0);
    json_t *value_borrowed = json_object_get(first_borrowed, column);
    json_t *value_owned = json_incref(value_borrowed);
    json_decref(rows_owned);
    rows_owned = NULL;
    first_borrowed = NULL;  // was borrowed from rows_owned
    value_borrowed = NULL;  // was borrowed from first_borrowed
    return value_owned;
}

void ox_table_instance_set_uuid(ox_table_instance_t *table_instance, const char *uuid)
{
    if (WARN_ON(table_instance == NULL)) return;
    if (ox_cstr_changed_null_safe(table_instance->uuid, uuid) == false) return;

    ox_table_ctx_t *table_ctx = table_instance->table_ctx;
    LOGD(LOG_PREFIX_INSTANCE(table_instance, "uuid: %s -> %s", table_instance->uuid ?: "NULL", uuid ?: "NULL"));

    if (table_instance->uuid != NULL)
    {
        if (table_ctx != NULL)
        {
            ds_tree_remove(&table_ctx->table_instances_by_uuid, table_instance);
        }
        FREE(table_instance->uuid);
        table_instance->uuid = NULL;
    }

    if (uuid != NULL)
    {
        table_instance->uuid = STRDUP(uuid);
        if (table_ctx != NULL)
        {
            ds_tree_insert(&table_ctx->table_instances_by_uuid, table_instance, table_instance->uuid);
        }
    }
}

void ox_table_instance_set_path(ox_table_instance_t *table_instance, const char *path)
{
    if (WARN_ON(table_instance == NULL)) return;
    if (WARN_ON(table_instance->table_ctx == NULL)) return;
    if (ox_cstr_changed_null_safe(table_instance->tr181_path, path) == false) return;

    ox_router_t *router = table_instance->table_ctx->router;
    LOGD(LOG_PREFIX_INSTANCE(table_instance, "path: %s -> %s", table_instance->tr181_path ?: "NULL", path ?: "NULL"));

    if (table_instance->tr181_path != NULL)
    {
        if (router != NULL)
        {
            ds_tree_remove(&router->table_instances_by_path, table_instance);
        }
        FREE(table_instance->tr181_path);
        table_instance->tr181_path = NULL;
    }

    if (path != NULL)
    {
        table_instance->tr181_path = STRDUP(path);
        if (router != NULL)
        {
            ds_tree_insert(&router->table_instances_by_path, table_instance, table_instance->tr181_path);
        }
    }
}

void ox_table_instance_set_parent(ox_table_instance_t *table_instance, ox_table_instance_t *parent)
{
    if (WARN_ON(table_instance == NULL)) return;
    if (table_instance->parent == parent) return;

    LOGD(LOG_PREFIX_INSTANCE(table_instance, "parent: %p -> %p", table_instance->parent, parent));

    if (table_instance->parent != NULL)
    {
        ds_tree_remove(&table_instance->parent->children_by_ptr, table_instance);
        table_instance->parent = NULL;
    }

    if (parent != NULL)
    {
        table_instance->parent = parent;
        ds_tree_insert(&parent->children_by_ptr, table_instance, table_instance);
    }
}

/* FIXME: This will eventually need to be garbage-collection based in some way
 * to handle ovsdb-only instances during tr181 path remapings events.
 */
void ox_table_instance_free(ox_table_instance_t *table_instance)
{
    if (table_instance == NULL) return;

    LOGD(LOG_PREFIX_INSTANCE(table_instance, "freeing"));

    json_decref(table_instance->stash);
    ox_table_instance_set_path(table_instance, NULL);
    ox_table_instance_set_uuid(table_instance, NULL);
    ox_table_instance_set_parent(table_instance, NULL);
    {
        ox_table_instance_t *child;
        while ((child = ds_tree_head(&table_instance->children_by_ptr)) != NULL)
        {
            ox_table_instance_free(child);
        }
    }
    FREE(table_instance);
}

json_t *ox_table_instance_xlate_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    json_t *converted_owned = route->from_ovsdb != NULL ? route->from_ovsdb(value_borrowed, route, table_instance)
                                                        : json_incref(value_borrowed);

    char *as_str_before_owned = json_dumps(value_borrowed, JSON_COMPACT | JSON_ENCODE_ANY);
    char *as_str_after_owned = json_dumps(converted_owned, JSON_COMPACT | JSON_ENCODE_ANY);
    LOGD(LOG_PREFIX_INSTANCE_PARAM(table_instance, route->tr181_param, "from_ovsdb: %s: %s -> %s"),
         route->ovsdb_column,
         as_str_before_owned,
         as_str_after_owned);
    FREE(as_str_before_owned);
    FREE(as_str_after_owned);

    return converted_owned;
}

json_t *ox_table_instance_xlate_to_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    json_t *converted_owned =
            route->to_ovsdb ? route->to_ovsdb(value_borrowed, route, table_instance) : json_incref(value_borrowed);

    char *as_str_before_owned = json_dumps(value_borrowed, JSON_COMPACT | JSON_ENCODE_ANY);
    char *as_str_after_owned = json_dumps(converted_owned, JSON_COMPACT | JSON_ENCODE_ANY);
    LOGD(LOG_PREFIX_INSTANCE_PARAM(table_instance, route->tr181_param, "to_ovsdb: %s: %s -> %s"),
         route->ovsdb_column,
         as_str_before_owned,
         as_str_after_owned);
    FREE(as_str_before_owned);
    FREE(as_str_after_owned);

    return converted_owned;
}

static const char *ox_table_instance_get_ovsdb_table(
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    if (route != NULL && route->ovsdb_table != NULL)
    {
        return route->ovsdb_table;
    }
    else if (table_instance != NULL && table_instance->table_ctx != NULL)
    {
        if (WARN_ON(table_instance->table_ctx->route == NULL)) return NULL;
        return table_instance->table_ctx->route->ovsdb_table;
    }
    else
    {
        return NULL;
    }
}

static json_t *ox_table_instance_param_get_row_from_sibling(
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    const ox_route_table_t *route_table = table_instance->table_ctx->route;

    /* Eg. this finds Wifi_Radio_Config by uuid, gets the if_name from it,
     * and then uses that if_name to finds uuid for if_name mathcing entry
     * in Wifi_Radio_State */

    // FIXME: This needs clean up WRT which table name to use and when?

    json_t *config_sibling_column_value_owned = ox_ovsdb_sync_get_column(
            ox_table_instance_get_ovsdb_table(route, table_instance),
            route_table->ovsdb_sibling_column,
            OX_OVSDB_UUID,
            ovsdb_tran_uuid_json(table_instance->uuid));
    if (config_sibling_column_value_owned == NULL)
    {
        return NULL;
    }

    json_t *state_uuid_owned = ox_ovsdb_sync_get_column(
            route_table->ovsdb_sibling_table,
            OX_OVSDB_UUID,
            route_table->ovsdb_sibling_column,
            config_sibling_column_value_owned);
    if (state_uuid_owned == NULL)
    {
        return NULL;
    }

    json_t *where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json(OX_OVSDB_UUID, OFUNC_EQ, state_uuid_owned);
    json_array_append_new(where_owned, cond_owned);
    cond_owned = NULL;  // moved to where_owned
    json_t *rows_owned = ovsdb_sync_select_where2(route_table->ovsdb_sibling_table, where_owned);
    where_owned = NULL;  // moved to ovsdb_sync_select_where2
    json_t *first_row_owned = json_incref(json_array_get(rows_owned, 0));
    json_decref(rows_owned);
    return first_row_owned;
}

static json_t *ox_table_instance_param_get_row(const ox_route_param_t *route, const ox_table_instance_t *table_instance)
{
    const ox_route_table_t *route_table = table_instance->table_ctx->route;

    if (route_table->ovsdb_sibling_table != NULL && route->ovsdb_no_sibling == false)
    {
        json_t *row_owned = ox_table_instance_param_get_row_from_sibling(route, table_instance);
        if (row_owned != NULL)
        {
            return row_owned;
        }

        if (route->ovsdb_sibling_only)
        {
            return NULL;
        }
    }

    const char *table = ox_table_instance_get_ovsdb_table(route, table_instance);
    const char *uuid_str = table_instance->uuid;
    json_t *uuid_owned = ovsdb_tran_uuid_json(uuid_str);
    json_t *where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json(OX_OVSDB_UUID, OFUNC_EQ, uuid_owned);
    uuid_owned = NULL;  // moved to cond_owned
    json_array_append_new(where_owned, cond_owned);
    cond_owned = NULL;  // moved to where_owned
    json_t *rows_owned = ovsdb_sync_select_where2(table, where_owned);
    where_owned = NULL;  // moved to ovsdb_sync_select_where2
    json_t *first_row_owned = json_incref(json_array_get(rows_owned, 0));
    json_decref(rows_owned);
    return first_row_owned;
}

static json_t *ox_table_instance_param_get_columns(json_t *row_borrowed, const char *columns_csv)
{
    char *columns_buf_owned = STRDUP(columns_csv);
    const char *separator = ",";  // TODO: allow configuring this if needed?
    char *columns_ptr_borrowed = columns_buf_owned;
    char *column;
    json_t *columns_owned = json_array();
    while ((column = strsep(&columns_ptr_borrowed, separator)) != NULL)
    {
        json_t *value_borrowed = json_object_get(row_borrowed, column);
        json_array_append(columns_owned, value_borrowed);
    }
    FREE(columns_buf_owned);
    return columns_owned;
}

json_t *ox_table_instance_param_get_value(
        json_t *row_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    if (WARN_ON(route == NULL)) return NULL;
    if (WARN_ON(route->ovsdb_column == NULL)) return NULL;
    if (WARN_ON(table_instance == NULL)) return NULL;

    json_t *columns_owned = ox_table_instance_param_get_columns(row_borrowed, route->ovsdb_column);
    json_t *value_borrowed = json_array_size(columns_owned) == 1 ? json_array_get(columns_owned, 0) : columns_owned;
    json_t *null_owned = json_null();
    if (route->ovsdb_map_key != NULL)
    {
        value_borrowed = ox_ovsdb_type_map_borrow_by_key(value_borrowed, route->ovsdb_map_key) ?: null_owned;
    }
    json_t *converted_owned = ox_table_instance_xlate_from_ovsdb(value_borrowed, route, table_instance);
    json_decref(columns_owned);
    json_decref(null_owned);
    return converted_owned;
}

static os_tr181_error_t ox_table_instance_param_set_json(
        json_t *value_owned,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    if (WARN_ON(route == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    const char *table = ox_table_instance_get_ovsdb_table(route, table_instance);

    if (route->tr181_stash_as != NULL)
    {
        char *value_str_owned = json_dumps(value_owned, JSON_COMPACT | JSON_ENCODE_ANY);
        LOGD(LOG_PREFIX_INSTANCE_PARAM(
                table_instance,
                route->tr181_stash_as,
                "stashing value for later use: %s",
                value_str_owned ?: "(null)"));
        FREE(value_str_owned);
        json_object_set(table_instance->stash, route->tr181_stash_as, value_owned);
    }

    if (route->ovsdb_map_key != NULL)
    {
        json_t *map_owned = json_array();
        json_t *map_pairs = json_array();
        json_t *map_pair = json_array();
        json_array_append_new(map_pair, json_string(route->ovsdb_map_key));
        json_array_append_new(map_pair, value_owned);
        value_owned = NULL;  // moved to map_pair
        json_array_append_new(map_pairs, map_pair);
        map_pair = NULL;  // moved to map_pairs
        json_array_append_new(map_owned, json_string("map"));
        json_array_append_new(map_owned, map_pairs);
        value_owned = map_owned;
        map_owned = NULL;  // moved to value_owned
    }

    json_t *converted_owned = ox_table_instance_xlate_to_ovsdb(value_owned, route, table_instance);
    json_decref(value_owned);
    value_owned = converted_owned;
    converted_owned = NULL;  // moved to value_owned

    if (value_owned == NULL)
    {
        return OS_TR181_ERROR_INVALID;
    }

    char *columns_buf = STRDUP(route->ovsdb_column);
    char *columns_ptr = columns_buf;
    const char *column;
    json_t *row_owned = json_object();
    while ((column = strsep(&columns_ptr, ",")) != NULL)
    {
        json_t *column_value_borrowed =
                json_is_object(value_owned) ? json_object_get(value_owned, column) : value_owned;
        json_object_set(row_owned, column, column_value_borrowed);
        json_object_del(value_owned, column);
    }
    FREE(columns_buf);
    const bool remaining_value_keys = (json_object_size(value_owned) > 0);
    if (remaining_value_keys)
    {
        char *value_str_owned = json_dumps(value_owned, JSON_COMPACT | JSON_ENCODE_ANY);
        LOGW(LOG_PREFIX_INSTANCE_PARAM(table_instance, route->tr181_param, "extra keys in value for column %s: %s"),
             route->ovsdb_column,
             value_str_owned);
        FREE(value_str_owned);
        json_decref(row_owned);
        json_decref(value_owned);
        return OS_TR181_ERROR_INVALID;
    }

    json_decref(value_owned);
    value_owned = NULL;

    json_t *where_owned = json_array();
    json_t *cond_owned =
            ovsdb_tran_cond_single_json(OX_OVSDB_UUID, OFUNC_EQ, ovsdb_tran_uuid_json(table_instance->uuid));
    json_array_append_new(where_owned, cond_owned);
    cond_owned = NULL;  // moved to where_owned

    const int count = ovsdb_sync_update_where(table, where_owned, row_owned);
    row_owned = NULL;    // moved to ovsdb_sync_update_where
    where_owned = NULL;  // moved to ovsdb_sync_update_where

    const bool too_many = (count > 1);
    const bool nothing_updated = (count == 0);

    if (too_many)
    {
        LOG(ERR, "Updated %d rows in table '%s' where uuid=%s", count, table, table_instance->uuid);
        return OS_TR181_ERROR_INVALID;
    }
    else if (nothing_updated)
    {
        LOG(ERR, "No rows updated in table '%s' where uuid=%s", table, table_instance->uuid);
        return OS_TR181_ERROR_NOT_FOUND;
    }
    else
    {
        return OS_TR181_SUCCESS;
    }
}

os_tr181_error_t ox_table_instance_param_get(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    json_t *row_owned = ox_table_instance_param_get_row(route, table_instance);
    json_t *value_owned = ox_table_instance_param_get_value(row_owned, route, table_instance);
    json_decref(row_owned);
    if (value_owned == NULL)
    {
        value_owned = json_null();
    }
    if (json_is_array(value_owned))
    {
        json_t *set_borrowed = ox_ovsdb_type_borrow_set(value_owned);
        json_t *map_borrowed = ox_ovsdb_type_borrow_map(value_owned);

        if (set_borrowed)
        {
            json_t *set_owned = json_incref(set_borrowed);
            json_decref(value_owned);
            set_borrowed = NULL;  // was borrowed from value_owned
            value_owned = set_owned;
            set_owned = NULL;  // moved to value_owned

            if (json_array_size(value_owned) == 0)
            {
                json_decref(value_owned);
                value_owned = json_null();
            }
        }
        else if (map_borrowed)
        {
            /* it's a list of arrays, each with 2 values - key-value */
            size_t i;
            json_t *kv_store_owned = json_object();
            json_t *kv_pair;
            json_array_foreach(map_borrowed, i, kv_pair)
            {
                json_t *key_borrowed = json_array_get(kv_pair, 0);
                json_t *val_borrowed = json_array_get(kv_pair, 1);
                const char *key_str_borrowed = json_string_value(key_borrowed);
                if (key_str_borrowed == NULL) continue;
                if (val_borrowed == NULL) continue;

                json_object_set(kv_store_owned, key_str_borrowed, val_borrowed);
            }

            json_decref(value_owned);
            map_borrowed = NULL;  // was borrowed from value_owned
            value_owned = kv_store_owned;
        }
    }
    switch (route->tr181_type)
    {
        case OS_TR181_TYPE_STRING:
            if (json_is_null(value_owned))
            {
                json_decref(value_owned);
                value_owned = json_string("");
            }
            else if (!json_is_string(value_owned))
            {
                char *json_str = json_dumps(value_owned, JSON_COMPACT | JSON_ENCODE_ANY);
                json_decref(value_owned);
                value_owned = json_string(json_str);
                FREE(json_str);
            }
            break;
        case OS_TR181_TYPE_BOOL:
            if (json_is_null(value_owned))
            {
                json_decref(value_owned);
                value_owned = json_boolean(false);
            }
            // FIXME: could have default values defined?
            /* FIXME: more best-effort handling is possible */
            break;

        case OS_TR181_TYPE_INT:
        case OS_TR181_TYPE_UINT:
        case OS_TR181_TYPE_INT64:
        case OS_TR181_TYPE_UINT64:
        case OS_TR181_TYPE_NONE:
        case OS_TR181_TYPE_DOUBLE:
        case OS_TR181_TYPE_DATETIME:
        case OS_TR181_TYPE_BASE64:
        case OS_TR181_TYPE_DICT:
        case OS_TR181_TYPE_LIST:
        case OS_TR181_TYPE_OBJECT:
        case OS_TR181_TYPE_TABLE:
        case OS_TR181_TYPE_INSTANCE:
        case OS_TR181_TYPE_METHOD:
        case OS_TR181_TYPE_EVENT:
        case OS_TR181_TYPE_PROPERTY:
            /* FIXME: more best-effort handling is possible */
            break;
    }
    const os_tr181_error_t err = os_val_from_json(tr181_value, value_owned);
    json_decref(value_owned);
    return err;
}

os_tr181_error_t ox_table_instance_param_set(
        const os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    json_t *json_value_owned = NULL;
    os_tr181_val_t wrapped_value_owned = OS_VAL_INIT();
    const os_tr181_val_t *selected_tr181_value = tr181_value;

    // FIXME:
    //  - DICT (HTABLE) apparently is not registrable
    //  - LIST seems to coerce it into comma-separated string
    // ie. the below if-elseif doesn't do anything useful in pratice, yet

    if (tr181_value->type == OS_TR181_TYPE_DICT)
    {
        os_val_set_list(&wrapped_value_owned);
        os_val_list_append_string(&wrapped_value_owned, "map");
        os_val_iter_t iter;
        const char *key;
        os_tr181_val_t *val;
        os_val_foreach_dict(iter, key, val, ((os_tr181_val_t *)tr181_value))
        {
            os_tr181_val_t kv_pair = OS_VAL_INIT();
            os_val_set_list(&kv_pair);
            os_val_list_append_string(&kv_pair, key);
            os_val_list_append(&kv_pair, val);
            os_val_list_append(&wrapped_value_owned, &kv_pair);
            os_val_free(&kv_pair);
        }
        selected_tr181_value = &wrapped_value_owned;
    }
    else if (tr181_value->type == OS_TR181_TYPE_LIST)
    {
        os_val_set_list(&wrapped_value_owned);
        os_val_list_append_string(&wrapped_value_owned, "set");
        os_val_list_append(&wrapped_value_owned, tr181_value);
        selected_tr181_value = &wrapped_value_owned;
    }

    const os_tr181_error_t err = os_val_to_json(selected_tr181_value, &json_value_owned);
    os_val_free(&wrapped_value_owned);
    if (err != OS_TR181_SUCCESS)
    {
        return err;
    }

    return ox_table_instance_param_set_json(json_value_owned, route, table_instance);
}

static os_tr181_error_t ox_table_instance_add_initial_values(
        ox_table_instance_t *table_instance,
        const os_tr181_val_t *initial_values,
        json_t *row_borrowed)
{
    os_val_iter_t iter;
    const char *key;
    os_tr181_val_t *value_borrowed;
    os_val_foreach_dict(iter, key, value_borrowed, (os_tr181_val_t *)initial_values)
    {
        ox_param_ctx_t *param_ctx = ds_tree_find(&table_instance->table_ctx->param_ctxs, key);
        if (param_ctx == NULL) return OS_TR181_ERROR_INVALID;
        if (param_ctx->route == NULL) return OS_TR181_ERROR_INVALID;
        if (param_ctx->route->to_ovsdb == NULL) return OS_TR181_ERROR_INVALID;

        json_t *json_value_owned = NULL;
        const os_tr181_error_t err = os_val_to_json(value_borrowed, &json_value_owned);
        if (err != OS_TR181_SUCCESS)
        {
            LOGD(LOG_PREFIX_INSTANCE(table_instance, "failed to convert initial value for key '%s' to json: %s"),
                 key,
                 os_tr181_error_string(err));
            json_decref(json_value_owned);
            return OS_TR181_ERROR_INVALID;
        }
        {
            char *json_str = json_dumps(json_value_owned, JSON_COMPACT | JSON_ENCODE_ANY);
            LOGD(LOG_PREFIX_INSTANCE(table_instance, "initial value raw: %s=%s"), key, json_str);
            FREE(json_str);
        }
        json_t *converted_owned = param_ctx->route->to_ovsdb(json_value_owned, param_ctx->route, table_instance);
        json_decref(json_value_owned);
        json_value_owned = NULL;  // dropped by json_decref()
        if (converted_owned == NULL)
        {
            LOGD(LOG_PREFIX_INSTANCE(table_instance, "failed to convert initial value for key '%s' to ovsdb format"),
                 key);
            return OS_TR181_ERROR_INVALID;
        }
        {
            char *json_str = json_dumps(converted_owned, JSON_COMPACT | JSON_ENCODE_ANY);
            LOGD(LOG_PREFIX_INSTANCE(table_instance, "initial value converted: %s=%s"), key, json_str);
            FREE(json_str);
        }
        json_object_set_new(row_borrowed, param_ctx->route->ovsdb_column, converted_owned);
    }
    return OS_TR181_SUCCESS;
}

static os_tr181_error_t ox_table_instance_add_to_parent(
        ox_table_ctx_t *parent_table_ctx,
        ox_table_instance_t *table_instance,
        const char *parent_uuid)
{
    const ox_route_table_t *route = table_instance->table_ctx->route;
    const char *parent_table = parent_table_ctx->route->ovsdb_table;

    json_t *parent_uuid_owned = NULL;
    ox_table_instance_t *parent_instance;
    ds_tree_foreach (&parent_table_ctx->table_instances_by_uuid, parent_instance)
    {
        const bool str_start_matches =
                strstr(table_instance->tr181_path, parent_instance->tr181_path) == table_instance->tr181_path;
        LOGD("looking for parent instance: comparing '%s' vs '%s': str_start_matches=%d",
             table_instance->tr181_path,
             parent_instance->tr181_path,
             str_start_matches);
        if (str_start_matches)
        {
            parent_uuid_owned = ovsdb_tran_uuid_json(parent_instance->uuid);
            break;
        }
    }

    json_t *parent_where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json(OX_OVSDB_UUID, OFUNC_EQ, parent_uuid_owned);
    json_array_append_new(parent_where_owned, cond_owned);
    cond_owned = NULL;  // moved to where_owned

    const int count = ovsdb_sync_mutate_uuid_set(
            parent_table,
            parent_where_owned,
            route->ovsdb_parent_column,
            OTR_INSERT,
            parent_uuid);
    parent_where_owned = NULL;  // moved to ovsdb_sync_mutate_uuid_set

    return (count == 1) ? OS_TR181_SUCCESS : OS_TR181_ERROR;
}

os_tr181_error_t ox_table_instance_add(ox_table_instance_t *table_instance, const os_tr181_val_t *initial_values)
{
    const os_tr181_val_t *id_val = os_val_dict_get(initial_values, OX_PARAM_UUID);
    char *uuid = NULL;

    if (id_val != NULL)
    {
        const char *uuid_str = os_val_get_str_or(id_val, NULL);
        if (uuid_str == NULL)
        {
            LOGD(LOG_PREFIX_INSTANCE(table_instance, "initial values contains '%s' but it's not a string"),
                 OX_PARAM_UUID);
            return OS_TR181_ERROR_INVALID;
        }

        uuid = STRDUP(uuid_str);
    }
    else
    {
        const ox_route_table_t *route = table_instance->table_ctx->route;
        const char *table = ox_table_instance_get_ovsdb_table(NULL, table_instance);
        json_t *initial_row_owned = route->init_row_cb ? route->init_row_cb(route) : json_object();
        json_t *row_owned = initial_row_owned ?: json_object();
        initial_row_owned = NULL;  // moved to row_owned
        const os_tr181_error_t err = ox_table_instance_add_initial_values(table_instance, initial_values, row_owned);
        if (err != OS_TR181_SUCCESS)
        {
            LOGD(LOG_PREFIX_INSTANCE(table_instance, "failed to add initial values to new row"));
            json_decref(row_owned);
            return err;
        }
        if (route->ovsdb_gating_column_name != NULL && route->ovsdb_gating_column_value != NULL)
        {
            json_t *column_value_owned = json_string(route->ovsdb_gating_column_value);
            json_object_set_new(row_owned, route->ovsdb_gating_column_name, column_value_owned);
            column_value_owned = NULL;  // moved to row_owned
        }
        ovs_uuid_t ovs_uuid;
        MEMZERO(ovs_uuid);
        const bool ok = ovsdb_sync_insert(table, row_owned, &ovs_uuid);
        if (ok == false)
        {
            LOGW(LOG_PREFIX_INSTANCE(table_instance, "failed to insert into table '%s'"), table);
            return OS_TR181_ERROR;
        }
        ox_table_ctx_t *parent_table_ctx = table_instance->table_ctx->parent_table_ctx;
        if (parent_table_ctx != NULL)
        {
            const os_tr181_error_t err =
                    ox_table_instance_add_to_parent(parent_table_ctx, table_instance, ovs_uuid.uuid);
            if (err != OS_TR181_SUCCESS)
            {
                json_t *where_owned = json_array();
                json_t *cond_owned =
                        ovsdb_tran_cond_single_json(OX_OVSDB_UUID, OFUNC_EQ, ovsdb_tran_uuid_json(ovs_uuid.uuid));
                json_array_append_new(where_owned, cond_owned);
                const int count_del = ovsdb_sync_delete_where(table, where_owned);
                where_owned = NULL;  // moved to ovsdb_sync_delete_where
                WARN_ON(count_del != 1);
                return OS_TR181_ERROR;
            }
        }

        uuid = STRDUP(ovs_uuid.uuid);
    }

    ox_table_instance_set_uuid(table_instance, uuid);
    FREE(uuid);
    return OS_TR181_SUCCESS;
}

os_tr181_error_t ox_table_instance_del(ox_table_instance_t *table_instance)
{
    if (table_instance->uuid != NULL)
    {
        const char *table = ox_table_instance_get_ovsdb_table(NULL, table_instance);
        json_t *uuid_owned = ovsdb_tran_uuid_json(table_instance->uuid);
        json_t *where_owned = json_array();
        json_t *cond_owned = ovsdb_tran_cond_single_json(OX_OVSDB_UUID, OFUNC_EQ, uuid_owned);
        json_array_append_new(where_owned, cond_owned);
        const int count = ovsdb_sync_delete_where(table, where_owned);
        if (count != 1)
        {
            LOGW(LOG_PREFIX_INSTANCE(
                    table_instance,
                    "failed to delete from table '%s' where uuid=%s: deleted %d rows",
                    table,
                    table_instance->uuid,
                    count));
        }

        /* WARNING: This is recursive. Read carefully.
         *
         * This will recursively first remove children (and their children, ..)
         * ovsdb rows. As soon as last deepest child is done, we'll start
         * freeing instances "bottoms up".
         *
         * Eventually we'll get back to the root (of the call) and it'll clean
         * it's list of children, all of which will be already clean
         * themselves.
         *
         * FIXME: Ideally this should be a single OVSDB transaction, so if it
         * fails, we can easily bail out and rollback. If something goes wrong
         * now, we might end up in a half-deleted state, which is not great.
         */

        ox_table_instance_t *child;
        ds_tree_foreach (&table_instance->children_by_ptr, child)
        {
            ox_table_instance_del(child);
        }

        while ((child = ds_tree_head(&table_instance->children_by_ptr)) != NULL)
        {
            ox_table_instance_free(child);
        }
    }

    ox_table_instance_set_uuid(table_instance, NULL);
    return OS_TR181_SUCCESS;
}

/*
 * Shared add_cb/del_cb for tables whose instances are only created or
 * deleted through OVSDB. External modification is rejected. Both have
 * to be non-NULL (required by ox_route_is_table()).
 * Internal creation/removal of OVSDB rows is detected via table_instance's
 * own uuid (set by ox_table_ctx_insert()/cleared by ox_table_ctx_delete()
 * before add_cb/del_cb run - not to be confused with any X_PLUME_uuid an
 * external caller may supply in initial_values, which is not trustworthy)
 * and is then delegated to the real ox_table_instance_add()/_del().
 */
os_tr181_error_t ox_table_instance_add_reject_external(
        ox_table_instance_t *table_instance,
        const os_tr181_val_t *initial_values)
{
    if (table_instance->uuid == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE(table_instance, "add rejected: instances are not user-creatable"));
        return OS_TR181_ERROR_NOT_IMPLEMENTED;
    }

    return ox_table_instance_add(table_instance, initial_values);
}

os_tr181_error_t ox_table_instance_del_reject_external(ox_table_instance_t *table_instance)
{
    if (table_instance->uuid != NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE(table_instance, "delete rejected: instances are not user-deletable"));
        return OS_TR181_ERROR_NOT_IMPLEMENTED;
    }

    return ox_table_instance_del(table_instance);  // no-op: uuid already cleared, row already gone from OVSDB
}

ox_table_instance_t *ox_table_instance_get(
        ox_table_ctx_t *table_ctx,
        const char *param_path, /* eg. "Device.WiFi.SSID.1." */
        const char *uuid)
{
    ox_router_t *router = table_ctx->router;
    ox_table_instance_t *table_instance_from_path =
            param_path ? ds_tree_find(&router->table_instances_by_path, param_path) : NULL;
    ox_table_instance_t *table_instance_from_uuid =
            uuid ? ds_tree_find(&table_ctx->table_instances_by_uuid, uuid) : NULL;
    const bool found_by_path = (table_instance_from_path != NULL);
    const bool found_by_uuid = (table_instance_from_uuid != NULL);
    const bool found_both = (found_by_path && found_by_uuid);

    if (found_both && table_instance_from_path != table_instance_from_uuid)
    {
        /* The requested path and uuid resolve to two distinct, already-tracked instances. */
        LOGW(LOG_PREFIX("found route by both path and uuid but they refer to different instances: path=%s uuid=%s"),
             param_path,
             uuid ?: "");
        return NULL;
    }

    if (found_by_uuid && param_path != NULL && table_instance_from_uuid->tr181_path != NULL
        && strcmp(table_instance_from_uuid->tr181_path, param_path) != 0)
    {
        /*
         * The uuid already belongs to a tracked instance registered under a different path.
         * A genuine internal (OVSDB-driven) add never reaches here for an already-tracked uuid
         * (ox_table_ctx_insert() bails out earlier in that case), so this can only be an external
         * caller supplying a real, already-existing instance's uuid (readable via X_PLUME_uuid) to
         * an unrelated add request. Reject rather than relocating the existing instance's path.
         */
        LOGW(LOG_PREFIX("found route instance by uuid but its existing path does not match requested path: %s vs %s"),
             table_instance_from_uuid->tr181_path,
             param_path);
        return NULL;
    }

    if (found_by_uuid) return table_instance_from_uuid;
    if (found_by_path) return table_instance_from_path;

    ox_table_instance_t *table_instance = CALLOC(1, sizeof(*table_instance));
    table_instance->router = router;
    table_instance->table_ctx = table_ctx;
    table_instance->stash = json_object();
    ds_tree_init(&table_instance->children_by_ptr, ds_void_cmp, ox_table_instance_t, instance_node_by_ptr);
    ox_table_instance_set_path(table_instance, param_path);
    return table_instance;
}

static json_t *ox_table_instance_get_sibling_column_value(ox_table_instance_t *table_instance)
{
    if (WARN_ON(table_instance == NULL)) return NULL;
    if (WARN_ON(table_instance->table_ctx == NULL)) return NULL;

    const ox_route_table_t *route = table_instance->table_ctx->route;
    if (WARN_ON(route == NULL)) return NULL;
    if (WARN_ON(route->ovsdb_sibling_column == NULL)) return NULL;

    json_t *row_owned = ox_route_row_from_uuid(route, table_instance->uuid);
    json_t *sibling_column_value_owned = json_incref(json_object_get(row_owned, route->ovsdb_sibling_column));
    json_decref(row_owned);
    return sibling_column_value_owned;
}

static json_t *ox_table_instance_get_sibling_row(ox_table_instance_t *table_instance)
{
    json_t *sibling_column_owned = ox_table_instance_get_sibling_column_value(table_instance);
    if (sibling_column_owned == NULL) return NULL;

    json_t *where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json(
            table_instance->table_ctx->route->ovsdb_sibling_column,
            OFUNC_EQ,
            sibling_column_owned);
    sibling_column_owned = NULL;  // moved to cond_owned
    json_array_append_new(where_owned, cond_owned);
    cond_owned = NULL;  // moved to where_owned
    json_t *rows_owned = ovsdb_sync_select_where2(table_instance->table_ctx->route->ovsdb_sibling_table, where_owned);
    json_t *first_row_borrowed = json_array_get(rows_owned, 0);
    json_t *first_row_owned = json_incref(first_row_borrowed);
    first_row_borrowed = NULL;  // upgraded to owned
    json_decref(rows_owned);
    return first_row_owned;
}

bool ox_table_instance_sibling_row_exists(ox_table_instance_t *table_instance)
{
    json_t *row_owned = ox_table_instance_get_sibling_row(table_instance);
    const bool exists = json_is_object(row_owned);
    json_decref(row_owned);
    return exists;
}
