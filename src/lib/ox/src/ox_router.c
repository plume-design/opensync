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

#include <ox_log.h>
#include <ox_ovsdb_monitor.h>
#include <ox_router.h>
#include <ox_types.h>
#include <stdio.h>
#include <string.h>

ox_table_ctx_t *ox_router_find_table_for_param(ox_router_t *router, const ox_route_param_t *route_param)
{
    ox_table_ctx_t *table_ctx;
    ox_table_ctx_t *table_ctx_best_match = NULL;
    ds_tree_foreach (&router->table_ctxs, table_ctx)
    {
        const ox_route_table_t *route_table = table_ctx->route;
        const bool prefix_matches =
                strstr(route_param->tr181_param, route_table->tr181_table) == route_param->tr181_param;
        if (prefix_matches)
        {
            if (table_ctx_best_match == NULL)
            {
                table_ctx_best_match = table_ctx;
            }
            else
            {
                const char *best_match_table = table_ctx_best_match->route->tr181_table;
                const bool candidate_is_longer_match = strlen(route_table->tr181_table) > strlen(best_match_table);
                if (candidate_is_longer_match)
                {
                    table_ctx_best_match = table_ctx;
                }
            }
        }
    }

    return table_ctx_best_match;
}
ox_table_ctx_t *ox_router_find_longest_parent_table_route(ox_router_t *router, const ox_route_table_t *route)
{
    ox_table_ctx_t *best_match = NULL;
    ox_table_ctx_t *table_ctx;
    ds_tree_foreach (&router->table_ctxs, table_ctx)
    {
        const ox_route_table_t *candidate_route = table_ctx->route;
        if (candidate_route == route)
        {
            continue;
        }

        const char *haystack = route->tr181_table;
        const char *needle = candidate_route->tr181_table;
        const bool starts_with_same = strstr(haystack, needle) == haystack;
        if (starts_with_same == false)
        {
            continue;
        }

        if (best_match == NULL)
        {
            best_match = table_ctx;
        }
        else
        {
            const char *best_match_table = best_match->route->tr181_table;
            const bool candidate_is_longer_match = strlen(needle) > strlen(best_match_table);
            if (candidate_is_longer_match)
            {
                best_match = table_ctx;
            }
        }
    }

    return best_match;
}

ox_table_instance_t *ox_router_find_table_instance_by_uuid(
        ox_router_t *router,
        const char *tr181_table,
        const char *uuid)
{
    ox_table_ctx_t *table_ctx = ds_tree_find(&router->table_ctxs, tr181_table);
    if (table_ctx == NULL)
    {
        return NULL;
    }

    return ds_tree_find(&table_ctx->table_instances_by_uuid, uuid);
}

static bool ox_router_add_table(ox_router_t *router, ox_table_ctx_t *table_ctx)
{
    if (router == NULL)
    {
        return false;
    }

    if (table_ctx == NULL)
    {
        return false;
    }

    const ox_route_table_t *route_table = table_ctx->route;

    if (route_table->ovsdb_sibling_ref_column != NULL)
    {
        LOGE(LOG_PREFIX_TABLE(table_ctx, "ovsdb_table_state_referencing_column is not supported yet"));
        return false;
    }

    os_tr181_error_t err;
    err = os_tr181_register_table(
            router->tr181_handle,
            route_table->tr181_table,
            ox_route_handle_add,
            ox_route_handle_del,
            table_ctx);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX_TABLE(table_ctx, "failed to register as table: %s", os_tr181_error_string(err)));
        return false;
    }

    char param_path[OS_TR181_PATH_MAX];
    snprintf(param_path, sizeof(param_path), "%s{i}.%s", route_table->tr181_table, OX_PARAM_UUID);
    err = os_tr181_register_parameter(
            router->tr181_handle,
            param_path,
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_READONLY,
            ox_route_handle_get_uuid,
            NULL,
            table_ctx);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX_TABLE(table_ctx, "failed to register UUID parameter: %s", os_tr181_error_string(err)));
        return false;
    }

    return true;
}

static bool ox_router_add_param(ox_router_t *router, ox_param_ctx_t *param_ctx)
{
    if (router == NULL)
    {
        return false;
    }

    if (param_ctx == NULL)
    {
        return false;
    }

    const ox_route_param_t *route_param = param_ctx->route;
    os_tr181_error_t err;

    const bool multiple_columns = route_param->ovsdb_column && strchr(route_param->ovsdb_column, ',') != NULL;
    if (route_param->ovsdb_map_key != NULL && multiple_columns)
    {
        LOGE(LOG_PREFIX_PARAM(param_ctx, "ovsdb_map_key is not supported for parameters with multiple csv columns"));
        return false;
    }

    const uint32_t access_flags = (route_param->set_cb ? OS_TR181_ACCESS_READWRITE : OS_TR181_ACCESS_READONLY);
    err = os_tr181_register_parameter(
            router->tr181_handle,
            route_param->tr181_param,
            route_param->tr181_type,
            access_flags,
            ox_route_handle_get,
            ox_route_handle_set,
            param_ctx);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX_PARAM(param_ctx, "failed to register as parameter: %s", os_tr181_error_string(err)));
        return false;
    }

    return true;
}

static bool ox_router_add_object(ox_router_t *router, const ox_route_object_t *route_object)
{
    if (WARN_ON(router == NULL)) return false;
    if (WARN_ON(route_object == NULL)) return false;

    const os_tr181_error_t err = os_tr181_register_object(router->tr181_handle, route_object->tr181_object);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX("failed to register object '%s': %s", route_object->tr181_object, os_tr181_error_string(err)));
        return false;
    }

    return true;
}

static bool ox_router_add_route(ox_router_t *router, const ox_route_t *route)
{
    if (router == NULL)
    {
        return false;
    }

    if (!ox_route_is_valid(route))
    {
        return false;
    }

    const ox_route_table_t *route_table = ox_route_as_table(route);
    const ox_route_param_t *route_param = ox_route_as_param(route);
    const ox_route_object_t *route_object = ox_route_as_object(route);

    if (route_table != NULL && route_table->ovsdb_gating_column_value != NULL
        && route_table->ovsdb_gating_column_values != NULL)
    {
        LOGE(LOG_PREFIX(
                "'%s': ovsdb_gating_column_value and ovsdb_gating_column_values are mutually exclusive",
                route_table->tr181_table));
        return false;
    }

    if (route_table != NULL)
    {
        ox_table_ctx_t *parent_table_ctx = ox_router_find_longest_parent_table_route(router, route_table);
        const char *route_path = route_table->tr181_table;
        ox_table_ctx_t *table_ctx = CALLOC(1, sizeof(*table_ctx));
        table_ctx->router = router;
        table_ctx->route = route_table;
        ds_tree_init(&table_ctx->table_instances_by_uuid, ds_str_cmp, ox_table_instance_t, table_ctx_node_by_uuid);
        ds_tree_init(&table_ctx->param_ctxs, ds_str_cmp, ox_param_ctx_t, table_ctx_node);
        ds_tree_init(&table_ctx->table_ctxs, ds_str_cmp, ox_table_ctx_t, parent_table_ctx_node);
        if (parent_table_ctx != NULL)
        {
            LOGD(LOG_PREFIX_TABLE(
                    table_ctx,
                    "found parent table route '%s' for table route '%s'",
                    parent_table_ctx->route->tr181_table,
                    route_table->tr181_table));
            table_ctx->parent_table_ctx = parent_table_ctx;
            ds_tree_insert(&parent_table_ctx->table_ctxs, table_ctx, route_path);
        }
        ds_tree_insert(&router->table_ctxs, table_ctx, route_path);
        return ox_router_add_table(router, table_ctx);
    }
    else if (route_param != NULL)
    {
        const char *last_part = strrchr(route_param->tr181_param, '.');
        if (last_part == NULL)
        {
            LOGE(LOG_PREFIX("invalid route param path '%s': must contain at least one dot", route_param->tr181_param));
            return false;
        }
        char *name = STRDUP(last_part + 1);
        ox_table_ctx_t *parent_table_ctx = ox_router_find_table_for_param(router, route_param);
        const char *route_path = route_param->tr181_param;
        ox_param_ctx_t *param_ctx = CALLOC(1, sizeof(*param_ctx));
        param_ctx->name = name;
        param_ctx->router = router;
        param_ctx->route = route_param;
        param_ctx->table_ctx = parent_table_ctx;
        if (parent_table_ctx != NULL)
        {
            LOGD(LOG_PREFIX_PARAM(
                    param_ctx,
                    "found parent table route '%s' for param route '%s'",
                    parent_table_ctx->route->tr181_table,
                    route_param->tr181_param));
            ds_tree_insert(&parent_table_ctx->param_ctxs, param_ctx, param_ctx->name);
        }
        ds_tree_insert(&router->param_ctxs, param_ctx, route_path);
        return ox_router_add_param(router, param_ctx);
    }
    else if (route_object != NULL)
    {
        return ox_router_add_object(router, route_object);
    }
    else
    {
        LOGE(LOG_PREFIX("%s: invalid route: neither table nor param",
                    route->table.tr181_table ?:
                    route->param.tr181_param ?:
                    "?"));
        return false;
    }
}

static bool ox_router_register_ovsdb(ox_router_t *router)
{
    ox_table_ctx_t *table_ctx;
    ds_tree_foreach (&router->table_ctxs, table_ctx)
    {
        const bool ok = ox_ovsdb_monitor_register(table_ctx);
        if (ok == false)
        {
            LOGE(LOG_PREFIX_TABLE(table_ctx, "failed to register ovsdb"));
            return false;
        }
    }

    return true;
}

bool ox_router_add_routes(ox_router_t *router, const ox_route_t *routes)
{
    const ox_route_t *route;
    for (route = routes; ox_route_is_valid(route); route++)
    {
        const bool ok = ox_router_add_route(router, route);
        if (ok == false)
        {
            return false;
        }
    }

    LOGD(LOG_PREFIX("all routes added successfully, registering OVSDB monitors"));
    return ox_router_register_ovsdb(router);
}

bool ox_router_init(ox_router_t *router)
{
    const os_tr181_error_t err = os_tr181_init(&router->tr181_handle);
    LOGD(LOG_PREFIX_FUNC("os_tr181_init() -> %s", os_tr181_error_string(err)));
    if (err != OS_TR181_SUCCESS)
    {
        LOGE(LOG_PREFIX("failed to initialize os_tr181 handle: %s", os_tr181_error_string(err)));
        return false;
    }

    ds_tree_init(&router->table_instances_by_path, ds_str_cmp, ox_table_instance_t, router_node_by_path);
    ds_tree_init(&router->ovsdb_monitors, ds_str_cmp, ox_ovsdb_monitor_t, router_node);
    ds_tree_init(&router->ovsdb_rows, ds_str_cmp, ox_ovsdb_row_t, router_node);
    ds_tree_init(&router->table_ctxs, ds_str_cmp, ox_table_ctx_t, router_node);
    ds_tree_init(&router->param_ctxs, ds_str_cmp, ox_param_ctx_t, router_node);

    return true;
}

void ox_router_fini(ox_router_t *router)
{
    os_tr181_close(router->tr181_handle);
    router->tr181_handle = NULL;

    // FIXME: just free local resources, dont bother advertising over tr181, or ovsdb
}
