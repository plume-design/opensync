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

#include <ovsdb_sync.h>
#include <ox_log.h>
#include <ox_ovsdb_type.h>
#include <ox_router.h>
#include <ox_table_ctx.h>
#include <ox_table_instance.h>
#include <util.h>

static ox_table_instance_t *ox_table_ctx_find_parent_instance(ox_table_ctx_t *table_ctx, const char *uuid)
{
    const ox_route_table_t *route = table_ctx->route;
    ox_table_ctx_t *parent_table_ctx = ox_router_find_longest_parent_table_route(table_ctx->router, route);
    if (parent_table_ctx == NULL)
    {
        LOGW(LOG_PREFIX_TABLE(
                table_ctx,
                "cannot find parent instance: no parent route found with ovsdb_parent_table prefix '%s'",
                route->ovsdb_parent_table));
        return NULL;
    }
    const ox_route_table_t *parent_route = parent_table_ctx->route;

    const char *parent_table = route->ovsdb_parent_table;   /* eg. Wifi_VIF_State */
    const char *parent_column = route->ovsdb_parent_column; /* eg. "associated_clients" */

    json_t *where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json(parent_column, OFUNC_INC, ovsdb_tran_uuid_json(uuid));
    json_array_append_new(where_owned, cond_owned);
    json_t *rows_owned = ovsdb_sync_select_where2(parent_table, where_owned);
    json_t *first_row_borrowed = json_array_get(rows_owned, 0);

    if (parent_route->ovsdb_sibling_table != NULL && parent_route->ovsdb_sibling_column != NULL)
    {
        const char *sibling_table = parent_route->ovsdb_table;
        const char *sibling_column_name = parent_route->ovsdb_sibling_column;
        json_t *sibling_column_value = json_incref(json_object_get(first_row_borrowed, sibling_column_name));
        json_decref(rows_owned);
        rows_owned = NULL;
        first_row_borrowed = NULL;

        json_t *where_owned = json_array();
        json_t *cond_owned = ovsdb_tran_cond_single_json(sibling_column_name, OFUNC_EQ, sibling_column_value);
        json_array_append_new(where_owned, cond_owned);
        rows_owned = ovsdb_sync_select_where2(sibling_table, where_owned);
        first_row_borrowed = json_array_get(rows_owned, 0);
    }

    json_t *parent_uuid_json_borrowed = json_object_get(first_row_borrowed, OX_OVSDB_UUID);
    const char *parent_uuid = ox_ovsdb_type_borrow_uuid(parent_uuid_json_borrowed);
    if (parent_uuid == NULL)
    {
        LOGD(LOG_PREFIX_TABLE(
                table_ctx,
                "cannot find parent instance: no parent uuid found in column '%s' of table '%s' for uuid %s",
                parent_column,
                parent_table,
                uuid ?: ""));
        json_decref(rows_owned);
        return NULL;
    }

    ox_table_instance_t *parent_instance = ds_tree_find(&parent_table_ctx->table_instances_by_uuid, parent_uuid);
    if (parent_instance == NULL)
    {
        LOGW(LOG_PREFIX_TABLE(
                table_ctx,
                "cannot find parent instance: no instance found for parent uuid %s",
                parent_uuid ?: ""));
        json_decref(rows_owned);
        return NULL;
    }

    json_decref(rows_owned);
    return parent_instance;
}

/* For routes with ovsdb_parent_same_row: the child and parent are the same
 * OVSDB row (same uuid), just projected into a deeper TR-181 nesting level -
 * no OVSDB query is needed, unlike ox_table_ctx_find_parent_instance() above.
 * The parent route is found the same way (longest tr181_table prefix match),
 * then the child's own uuid is looked up directly in the parent's instances. */
static ox_table_instance_t *ox_table_ctx_find_parent_instance_same_row(ox_table_ctx_t *table_ctx, const char *uuid)
{
    const ox_route_table_t *route = table_ctx->route;
    ox_table_ctx_t *parent_table_ctx = ox_router_find_longest_parent_table_route(table_ctx->router, route);
    if (parent_table_ctx == NULL)
    {
        LOGW(LOG_PREFIX_TABLE(
                table_ctx,
                "cannot find parent instance: no parent route found for ovsdb_parent_same_row table '%s'",
                route->tr181_table));
        return NULL;
    }

    ox_table_instance_t *parent_instance = ds_tree_find(&parent_table_ctx->table_instances_by_uuid, uuid);
    if (parent_instance == NULL)
    {
        LOGW(LOG_PREFIX_TABLE(table_ctx, "cannot find parent instance: no instance found for uuid %s", uuid ?: ""));
        return NULL;
    }

    return parent_instance;
}

static bool ox_table_ctx_prepare_initial_stashed_values(
        ox_table_ctx_t *table_ctx,
        ox_table_instance_t *instance,
        json_t *new_values_borrowed,
        os_tr181_val_t *initial_values)
{
    size_t error_count = 0;
    ox_param_ctx_t *param_ctx;
    ds_tree_foreach (&table_ctx->param_ctxs, param_ctx)
    {
        if (param_ctx->route->tr181_stash_as == NULL) continue;

        json_t *value_owned = ox_table_instance_param_get_value(new_values_borrowed, param_ctx->route, instance);
        if (value_owned == NULL) continue;

        char *value_str_owned = json_dumps(value_owned, JSON_COMPACT | JSON_ENCODE_ANY);
        LOGD(LOG_PREFIX_INSTANCE_PARAM(instance, param_ctx->name, "stashing initial value for param %s: %s"),
             param_ctx->name,
             value_str_owned ?: "(null)");
        FREE(value_str_owned);

        os_tr181_val_t stash_value = OS_VAL_INIT();
        const os_tr181_error_t err_json = os_val_from_json(&stash_value, value_owned);
        json_decref(value_owned);
        value_owned = NULL;
        if (err_json != OS_TR181_SUCCESS)
        {
            error_count++;
            LOGW(LOG_PREFIX_PARAM(
                    param_ctx,
                    "failed to get stash value for initial values: %s",
                    os_tr181_error_string(err_json)));
            os_val_free(&stash_value);
            continue;
        }

        const os_tr181_error_t err_set = os_val_dict_set(initial_values, param_ctx->name, &stash_value);
        /* stash_value is deep-copied into initial_values, so we must free it here */
        os_val_free(&stash_value);
        if (err_set != OS_TR181_SUCCESS)
        {
            error_count++;
            LOGW(LOG_PREFIX_PARAM(
                    param_ctx,
                    "failed to set stash value into initial values: %s",
                    os_tr181_error_string(err_set)));
            continue;
        }
    }

    if (error_count > 0)
    {
        LOGD(LOG_PREFIX_TABLE(table_ctx, "%zu stash values failed to be added to initial values", error_count));
        return false;
    }

    return true;
}

bool ox_table_ctx_insert(ox_table_ctx_t *table_ctx, const char *uuid, json_t *new_values_borrowed)
{
    if (WARN_ON(table_ctx == NULL)) return false;
    if (WARN_ON(uuid == NULL)) return false;

    const ox_route_table_t *route = table_ctx->route;
    ox_table_instance_t *existing_instance = ds_tree_find(&table_ctx->table_instances_by_uuid, uuid);
    if (existing_instance != NULL)
    {
        LOGD(LOG_PREFIX_TABLE(table_ctx, "instance with uuid %s already exists, skipping insert", uuid));
        return true;
    }

    char object_path[OS_TR181_PATH_MAX];
    snprintf(object_path, sizeof(object_path), "%s", route->tr181_table);

    if (route->ovsdb_gating_column_name != NULL
        && (route->ovsdb_gating_column_value != NULL || route->ovsdb_gating_column_values != NULL))
    {
        json_t *gating_column_value_json_borrowed =
                json_object_get(new_values_borrowed, route->ovsdb_gating_column_name);
        if (gating_column_value_json_borrowed == NULL)
        {
            LOGE(LOG_PREFIX_TABLE(
                    table_ctx,
                    "cannot insert: gating column '%s' is missing in new values for uuid %s",
                    route->ovsdb_gating_column_name,
                    uuid ?: ""));
            return false;
        }

        const char *column_str_value = json_string_value(gating_column_value_json_borrowed);
        if (column_str_value == NULL)
        {
            LOGE(LOG_PREFIX_TABLE(
                    table_ctx,
                    "cannot insert: gating column '%s' value is not a string in new values for uuid %s",
                    route->ovsdb_gating_column_name,
                    uuid ?: ""));
            return false;
        }

        bool gating_match = false;
        if (route->ovsdb_gating_column_value != NULL)
        {
            gating_match = strcmp(column_str_value, route->ovsdb_gating_column_value) == 0;
        }
        else
        {
            char **accepted_values = (char **)route->ovsdb_gating_column_values;
            gating_match = is_inarray(column_str_value, count_nt_array(accepted_values), accepted_values);
        }

        if (!gating_match)
        {
            char expected_buf[128];
            if (route->ovsdb_gating_column_value != NULL)
            {
                char *single_value[] = {(char *)route->ovsdb_gating_column_value, NULL};
                strfmt_nt_array(expected_buf, sizeof(expected_buf), single_value);
            }
            else
            {
                strfmt_nt_array(expected_buf, sizeof(expected_buf), (char **)route->ovsdb_gating_column_values);
            }
            LOGD(LOG_PREFIX_TABLE(
                    table_ctx,
                    "skipping insert because gating column '%s' value '%s' does not match expected %s for uuid %s",
                    route->ovsdb_gating_column_name,
                    column_str_value,
                    expected_buf,
                    uuid ?: ""));
            /* not an actual error */
            return true;
        }
    }

    const bool needs_parent =
            (route->ovsdb_parent_table != NULL && route->ovsdb_parent_column != NULL) || route->ovsdb_parent_same_row;
    ox_table_instance_t *parent_instance = NULL;
    if (needs_parent)
    {
        parent_instance = route->ovsdb_parent_same_row ? ox_table_ctx_find_parent_instance_same_row(table_ctx, uuid)
                                                       : ox_table_ctx_find_parent_instance(table_ctx, uuid);
        if (parent_instance == NULL)
        {
            LOGD(LOG_PREFIX_TABLE(table_ctx, "cannot insert: failed to find parent instance for uuid %s", uuid ?: ""));
            return false;
        }

        const char *table_path = parent_instance->table_ctx->route->tr181_table; /* Device.WiFi.AccessPoint. */
        char from[OS_TR181_PATH_MAX]; /* needs to match Device.WiFi.AccessPoint.{i}. */
        snprintf(from, sizeof(from), "%s{i}.", table_path);
        const char *to = parent_instance->tr181_path;
        const size_t size = sizeof(object_path);
        str_replace_fixed(object_path, size, from, to);
    }

    const bool contains_placeholders = (strstr(object_path, ".{i}.") != NULL);
    if (contains_placeholders)
    {
        LOGE(LOG_PREFIX_TABLE(
                table_ctx,
                "cannot insert: object path still contains placeholders after replacement: %s",
                object_path));
        return false;
    }

    /* param_path is not known yet because the instance number is auto-assigned
     * and only known after os_tr181_add_instance_ex() is called, so we pass
     * NULL here and it'll be set later in ox_route_handle_add() when we get
     * the param_path as an argument
     */
    const char *param_path = NULL;
    ox_table_instance_t *instance = ox_table_instance_get(table_ctx, param_path, uuid);
    if (instance == NULL)
    {
        LOGE(LOG_PREFIX_TABLE(table_ctx, "failed to get or create route instance for uuid %s", uuid ?: ""));
        return false;
    }

    ox_table_instance_set_uuid(instance, uuid);
    ox_table_instance_set_parent(instance, parent_instance);

    os_tr181_val_t uuid_val = OS_VAL_INIT();
    os_tr181_error_t err;
    err = os_val_set_str_dup(&uuid_val, uuid);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX_TABLE(table_ctx, "failed to set id value: %s: %s", uuid ?: "", os_tr181_error_string(err)));
        ox_table_instance_free(instance);
        return false;
    }

    os_tr181_val_t initial_values = OS_VAL_INIT();
    os_val_set_dict(&initial_values);

    err = os_val_dict_set(&initial_values, OX_PARAM_UUID, &uuid_val);
    /* uuid_val is deep-copied into initial_values, so we must free it here */
    os_val_free(&uuid_val);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX_TABLE(
                table_ctx,
                "failed to set initial values: %s: %s",
                uuid ?: "",
                os_tr181_error_string(err)));
        os_val_free(&initial_values);
        ox_table_instance_free(instance);
        return false;
    }

    const bool stashed_ok =
            ox_table_ctx_prepare_initial_stashed_values(table_ctx, instance, new_values_borrowed, &initial_values);
    if (stashed_ok == false)
    {
        LOGW(LOG_PREFIX_TABLE(
                table_ctx,
                "failed to prepare some stashed values for initial values for uuid %s",
                uuid ?: ""));
        os_val_free(&initial_values);
        ox_table_instance_free(instance);
        return false;
    }

    os_tr181_handle_t *handle = table_ctx->router->tr181_handle;
    const int requested_index = 0; /* =0 means automatic assignment */
    const char *alias = "";
    int assigned_index = 0;
    err = os_tr181_add_instance_ex(handle, object_path, requested_index, alias, &initial_values, &assigned_index);
    /* os_tr181_add_instance_ex only borrows it, so we must free it here */
    os_val_free(&initial_values);
    /* if add() fails then the instance is not guaranteed to live
     * through it. It might've been freed by now. Re-lookup it.
     */
    instance = ds_tree_find(&table_ctx->table_instances_by_uuid, uuid);
    if (err)
    {
        LOGE(LOG_PREFIX_TABLE(table_ctx, "failed to add instance for %s: %s", uuid ?: "", os_tr181_error_string(err)));
        ox_table_instance_free(instance);
        return false;
    }

    LOGI(LOG_PREFIX_TABLE(table_ctx, "ovsdb: spawned instance for uuid %s at index %d", uuid, assigned_index));
    return true;
}

bool ox_table_ctx_delete(ox_table_ctx_t *table_ctx, const char *uuid)
{
    if (WARN_ON(table_ctx == NULL)) return false;
    if (WARN_ON(uuid == NULL)) return false;

    ox_table_instance_t *table_instance = ds_tree_find(&table_ctx->table_instances_by_uuid, uuid);
    if (table_instance == NULL)
    {
        /* Gated columns can prevent some rows from being mapped to instances,
         * so it's possible that we get an OVSDB delete for a row that was
         * never mapped to an instance. So just log and ignore this. */
        return true;
    }

    /* FIXME: This should probably walk through and destroy dependant
     * instances, eg. if Device.WiFi.AccessPoint.{i} is deleted, then all
     * instances of Device.WiFi.AccessPoint.{i}.AssociatedDevice.{i} should
     * also be deleted too, including the ovsdb rows.
     *
     * This needs to be hooked at del_cb() as well. Maybe only there?
     */

    ox_table_instance_set_uuid(table_instance, NULL);
    LOGD(LOG_PREFIX_INSTANCE(table_instance, "deleting"));

    os_tr181_handle_t *handle = table_ctx->router->tr181_handle;
    const char *instance_path = table_instance->tr181_path;
    const os_tr181_error_t err = os_tr181_delete_instance(handle, instance_path);
    table_instance = NULL; /* possibly dangling after os_tr181_delete_instance() */
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX_TABLE(
                table_ctx,
                "failed to delete instance for %s: %s",
                uuid ?: "",
                os_tr181_error_string(err)));
        return false;
    }

    return true;
}
