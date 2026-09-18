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

#include <memutil.h>
#include <ovsdb_sync.h>
#include <ox_log.h>
#include <ox_ovsdb_type.h>
#include <ox_route.h>
#include <ox_table_instance.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool ox_route_is_table(const ox_route_t *route)
{
    return route != NULL && route->table.tr181_table != NULL && route->table.add_cb != NULL
           && route->table.del_cb != NULL && route->table.ovsdb_table != NULL;
}

bool ox_route_is_param(const ox_route_t *route)
{
    return route != NULL && route->param.tr181_param != NULL
           && (route->param.set_cb != NULL || route->param.get_cb != NULL);
}

bool ox_route_is_object(const ox_route_t *route)
{
    return route != NULL && route->object.tr181_object != NULL;
}

const ox_route_table_t *ox_route_as_table(const ox_route_t *route)
{
    return ox_route_is_table(route) ? &route->table : NULL;
}

const ox_route_param_t *ox_route_as_param(const ox_route_t *route)
{
    return ox_route_is_param(route) ? &route->param : NULL;
}

const ox_route_object_t *ox_route_as_object(const ox_route_t *route)
{
    return ox_route_is_object(route) ? &route->object : NULL;
}

bool ox_route_is_valid(const ox_route_t *route)
{
    int num_valid_types = 0;
    num_valid_types += ox_route_is_table(route) ? 1 : 0;
    num_valid_types += ox_route_is_param(route) ? 1 : 0;
    num_valid_types += ox_route_is_object(route) ? 1 : 0;
    return num_valid_types == 1;
}

void ox_route_param_path_to_instance_path(const char *param_path, char *instance_path, size_t instance_path_size)
{
    snprintf(instance_path, instance_path_size, "%s", param_path);
    char *last_dot = strrchr(instance_path, '.');
    if (last_dot != NULL)
    {
        /* Leave the dot */
        last_dot[1] = '\0';
    }
}

char *ox_route_param_extract_instance_base_path(const ox_route_param_t *route_param, const char *param_path)
{
    size_t parts = 0;
    const char *placeholder_str = ".{i}";
    char *route_path = STRDUP(route_param->tr181_param);
    char *part;
    while ((part = strrchr(route_path, '.')) != NULL)
    {
        LOGD(LOG_PREFIX("checking part '%s' against placeholder '%s'"), part, placeholder_str);
        if (strcmp(part, placeholder_str) == 0)
        {
            break;
        }
        parts++;
        *part = '\0';
    }
    FREE(route_path);

    LOGD(LOG_PREFIX("param_path=%s, route_param=%s, parts=%zu"), param_path, route_param->tr181_param, parts);
    if (parts == 0)
    {
        return NULL;
    }

    char *instance_base_path = STRDUP(param_path);
    while (parts-- > 0)
    {
        char *last_dot = strrchr(instance_base_path, '.');
        if (last_dot == NULL)
        {
            FREE(instance_base_path);
            return NULL;
        }
        LOGD(LOG_PREFIX("trimming instance base path '%s' to remove part after dot"), instance_base_path);
        last_dot[0] = '\0';
    }

    char *after_last_char = instance_base_path + strlen(instance_base_path);
    after_last_char[0] = '.';
    after_last_char[1] = '\0';
    LOGD(LOG_PREFIX("extracted instance base path '%s' from param_path '%s' using route_param '%s'"),
         instance_base_path,
         param_path,
         route_param->tr181_param);
    return instance_base_path;
}

os_tr181_error_t ox_route_handle_get(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    ox_param_ctx_t *param_ctx = user_data;
    ox_router_t *router = param_ctx->router;
    const ox_route_param_t *route = param_ctx->route;
    char *instance_path = ox_route_param_extract_instance_base_path(route, param_path);
    const bool is_singleton = strstr(route->tr181_param, "{i}") == NULL;
    LOGD(LOG_PREFIX_FUNC("param_path=%s, instance_path=%s"), param_path, instance_path ?: "");
    ox_table_instance_t *table_instance = ds_tree_find(&router->table_instances_by_path, instance_path ?: "");
    FREE(instance_path);
    ox_route_get_fn_t get_cb = route->get_cb;
    if (get_cb == NULL)
    {
        return OS_TR181_ERROR_NOT_IMPLEMENTED;
    }
    if (!is_singleton && table_instance == NULL)
    {
        return OS_TR181_ERROR_NOT_FOUND;
    }
    return get_cb(value, param_path, route, table_instance);
}

os_tr181_error_t ox_route_handle_set(const char *param_path, const os_tr181_val_t *value, void *user_data)
{
    ox_param_ctx_t *param_ctx = user_data;
    ox_router_t *router = param_ctx->router;
    const ox_route_param_t *route = param_ctx->route;
    char *instance_path = ox_route_param_extract_instance_base_path(route, param_path);
    const bool is_singleton = strstr(route->tr181_param, "{i}") == NULL;
    LOGD(LOG_PREFIX_FUNC("param_path=%s, instance_path=%s"), param_path, instance_path ?: "");
    ox_table_instance_t *table_instance = ds_tree_find(&router->table_instances_by_path, instance_path ?: "");
    FREE(instance_path);
    ox_route_set_fn_t set_cb = route->set_cb;
    if (set_cb == NULL)
    {
        return OS_TR181_ERROR_NOT_IMPLEMENTED;
    }
    if (!is_singleton && table_instance == NULL)
    {
        return OS_TR181_ERROR_NOT_FOUND;
    }
    return set_cb(value, param_path, route, table_instance);
}

os_tr181_error_t ox_route_handle_add(
        const char *object_path,
        int instance_num,
        const os_tr181_val_t *initial_values,
        void *user_data)
{
    char param_path[OS_TR181_PATH_MAX];
    snprintf(param_path, sizeof(param_path), "%s%d.", object_path, instance_num);

    const os_tr181_val_t *uuid_val = os_val_dict_get(initial_values, OX_PARAM_UUID);
    const char *uuid_str = os_val_get_str_or(uuid_val, NULL);

    ox_table_ctx_t *table_ctx = user_data;
    const ox_route_table_t *route = table_ctx->route;
    ox_route_add_fn_t add_cb = route->add_cb;
    ox_table_instance_t *table_instance = ox_table_instance_get(table_ctx, param_path, uuid_str);
    if (table_instance == NULL)
    {
        return OS_TR181_ERROR;
    }

    ox_table_instance_set_path(table_instance, param_path);

    if (add_cb != NULL)
    {
        const os_tr181_error_t err = add_cb(table_instance, initial_values);
        if (err != OS_TR181_SUCCESS)
        {
            ox_table_instance_free(table_instance);
            return err;
        }
    }

    return OS_TR181_SUCCESS;
}

os_tr181_error_t ox_route_handle_del(const char *param_path, int instance_num, void *user_data)
{
    char instance_path[OS_TR181_PATH_MAX];
    snprintf(instance_path, sizeof(instance_path), "%s%d.", param_path, instance_num);

    ox_table_ctx_t *table_ctx = user_data;
    const ox_route_table_t *route = table_ctx->route;
    ox_route_del_fn_t del_cb = route->del_cb;
    ox_table_instance_t *table_instance = ds_tree_find(&table_ctx->router->table_instances_by_path, instance_path);
    if (table_instance == NULL)
    {
        return OS_TR181_ERROR_NOT_FOUND;
    }

    if (del_cb != NULL)
    {
        const os_tr181_error_t err = del_cb(table_instance);
        if (err != OS_TR181_SUCCESS)
        {
            return err;
        }
    }

    ox_table_instance_free(table_instance);
    return OS_TR181_SUCCESS;
}

os_tr181_error_t ox_route_handle_get_uuid(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    ox_table_ctx_t *table_ctx = user_data;
    if (table_ctx == NULL)
    {
        return OS_TR181_ERROR;
    }

    char instance_path[OS_TR181_PATH_MAX];
    ox_route_param_path_to_instance_path(param_path, instance_path, sizeof(instance_path));

    ox_table_instance_t *table_instance = ds_tree_find(&table_ctx->router->table_instances_by_path, instance_path);
    if (table_instance == NULL)
    {
        return OS_TR181_ERROR_NOT_FOUND;
    }

    return os_val_set_str_dup(value, table_instance->uuid);
}

char *ox_route_sibling_row_to_primary_uuid(const ox_route_table_t *route_table, json_t *sibling_row_borrowed)
{
    if (WARN_ON(route_table == NULL)) return NULL;
    if (WARN_ON(sibling_row_borrowed == NULL)) return NULL;
    if (WARN_ON(route_table->ovsdb_sibling_column == NULL)) return NULL;

    const char *sibling_column = route_table->ovsdb_sibling_column;
    const char *main_table = route_table->ovsdb_table;
    json_t *sibling_column_value = json_incref(json_object_get(sibling_row_borrowed, sibling_column));
    json_t *where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json(sibling_column, OFUNC_EQ, sibling_column_value);
    json_array_append_new(where_owned, cond_owned);
    json_t *rows_owned = ovsdb_sync_select_where2(main_table, where_owned);
    where_owned = NULL;  // moved to ovsdb_sync_select_where2
    json_t *first_row_borrowed = json_array_get(rows_owned, 0);
    json_t *main_uuid_json_borrowed = json_object_get(first_row_borrowed, OX_OVSDB_UUID);
    const char *main_uuid_borrowed = ox_ovsdb_type_borrow_uuid(main_uuid_json_borrowed);
    char *main_uuid = main_uuid_borrowed ? STRDUP(main_uuid_borrowed) : NULL;
    json_decref(rows_owned);
    return main_uuid;
}

json_t *ox_route_row_from_uuid(const ox_route_table_t *route_table, const char *uuid)
{
    if (WARN_ON(route_table == NULL)) return NULL;
    if (WARN_ON(uuid == NULL)) return NULL;

    const char *table = route_table->ovsdb_table;
    json_t *where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json(OX_OVSDB_UUID, OFUNC_EQ, ovsdb_tran_uuid_json(uuid));
    json_array_append_new(where_owned, cond_owned);
    json_t *rows_owned = ovsdb_sync_select_where2(table, where_owned);
    where_owned = NULL;  // moved to ovsdb_sync_select_where2
    json_t *first_row_borrowed = json_array_get(rows_owned, 0);
    json_t *row_owned = json_incref(first_row_borrowed);
    json_decref(rows_owned);
    return row_owned;
}

char *ox_route_param_path_for_instance(const ox_route_param_t *param_route, ox_table_instance_t *table_instance)
{
    if (param_route == NULL) return NULL;
    if (table_instance == NULL) return NULL;
    if (table_instance->table_ctx == NULL) return NULL;

    const ox_route_table_t *route_table = table_instance->table_ctx->route;
    if (route_table == NULL) return NULL;

    const char *template = param_route->tr181_param;
    const char *from = strfmta("%s{i}.", route_table->tr181_table);
    const char *to = table_instance->tr181_path;

    return str_replace_with(template, from, to);
}
