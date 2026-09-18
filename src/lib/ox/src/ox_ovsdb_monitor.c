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
#include <ox_ovsdb_monitor.h>
#include <ox_ovsdb_row.h>
#include <ox_ovsdb_type.h>
#include <ox_route.h>
#include <ox_table_ctx.h>
#include <ox_table_instance.h>
#include <ox_types.h>
#include <ox_util.h>
#include <string.h>

static void ox_ovsdb_monitor_notify(
        ox_table_ctx_t *table_ctx,
        json_t *row_changed_borrowed,
        json_t *row_before_borrowed,
        json_t *row_after_borrowed,
        const char *table_name,
        const char *primary_uuid)
{
    ox_router_t *router = table_ctx->router;

    json_t *notified = json_object();

    const char *column_name;
    json_t *column_value_borrowed;
    json_object_foreach(row_changed_borrowed, column_name, column_value_borrowed)
    {
        // FIXME: this could be optimized with a reverse lookup by
        // pre-cooking column_name -> param tree
        ox_param_ctx_t *param_ctx;
        ds_tree_foreach (&table_ctx->param_ctxs, param_ctx)
        {
            if (param_ctx->route == NULL) continue;
            if (param_ctx->route->ovsdb_column == NULL) continue;

            const bool already_notified = json_object_get(notified, param_ctx->route->tr181_param) != NULL;
            if (already_notified)
            {
                LOGD(LOG_PREFIX_PARAM(
                        param_ctx,
                        "already notified for param %s, skipping",
                        param_ctx->route->tr181_param));
                continue;
            }

            const bool affected = ox_util_csv_contains(param_ctx->route->ovsdb_column, column_name);
            if (affected == false) continue;

            // FIXME: it would be convenient to finding the sibling's main table instances through uuids too
            // but for now, and expensive brute-force scan is done
            ox_table_instance_t *table_instance = ds_tree_find(&table_ctx->table_instances_by_uuid, primary_uuid);
            if (table_instance == NULL) continue;

            const bool table_has_sibling = table_ctx->route->ovsdb_sibling_column != NULL;
            const bool table_name_is_sibling =
                    table_has_sibling && strcmp(table_name, table_ctx->route->ovsdb_sibling_table) == 0;

            const bool sibling_update_so_ignore = table_name_is_sibling && param_ctx->route->ovsdb_no_sibling;
            const bool primary_update_so_ignore = !table_name_is_sibling && param_ctx->route->ovsdb_sibling_only;
            const bool ignore_when_sibling_exists =
                    table_has_sibling && !table_name_is_sibling && !param_ctx->route->ovsdb_no_sibling;
            if (sibling_update_so_ignore) continue;
            if (primary_update_so_ignore) continue;
            if (ignore_when_sibling_exists)
            {
                // FIXME: This is expensive (calls OVSDB sync call). This could
                // be moved outside of this immediate inner loop, or just
                // relied from internal structures. This is fine functionally
                // for control plane for now.
                const bool sibling_exists = ox_table_instance_sibling_row_exists(table_instance);
                if (sibling_exists) continue;
            }

            const ox_route_param_t *route = param_ctx->route;
            char *param_path = ox_route_param_path_for_instance(route, table_instance);
            if (param_path == NULL) continue;

            json_t *value_before_owned = ox_table_instance_param_get_value(row_before_borrowed, route, table_instance);
            json_t *value_after_owned = ox_table_instance_param_get_value(row_after_borrowed, route, table_instance);

            char *value_before_str = json_dumps(value_before_owned, JSON_COMPACT | JSON_ENCODE_ANY);
            char *value_after_str = json_dumps(value_after_owned, JSON_COMPACT | JSON_ENCODE_ANY);
            const bool changed = strcmp(value_before_str ?: "", value_after_str ?: "") != 0;

            LOGD(LOG_PREFIX_INSTANCE_PARAM(
                    table_instance,
                    param_ctx->route->tr181_param,
                    "sibling table %s column %s param %s path %s %schanged: %s -> %s",
                    table_name,
                    column_name,
                    param_ctx->name,
                    param_path,
                    changed ? "" : "not ",
                    value_before_str ?: "",
                    value_after_str ?: ""));

            os_tr181_val_t tr181_value_before = OS_VAL_INIT();
            os_tr181_val_t tr181_value_after = OS_VAL_INIT();

            const os_tr181_error_t before_err = os_val_from_json(&tr181_value_before, value_before_owned);
            const os_tr181_error_t after_err = os_val_from_json(&tr181_value_after, value_after_owned);
            if (before_err != OS_TR181_SUCCESS || after_err != OS_TR181_SUCCESS)
            {
                LOGW(LOG_PREFIX_INSTANCE_PARAM(
                        table_instance,
                        param_ctx->route->tr181_param,
                        "failed to convert json to tr181 value for sibling table %s column %s param %s change: %s -> "
                        "%s: before_err=%s after_err=%s",
                        table_name,
                        column_name,
                        param_ctx->name,
                        value_before_str ?: "",
                        value_after_str ?: "",
                        os_tr181_error_string(before_err),
                        os_tr181_error_string(after_err)));
            }
            else if (changed)
            {
                const os_tr181_error_t notify_err = os_tr181_notify_changed(
                        router->tr181_handle,
                        param_path,
                        &tr181_value_before,
                        &tr181_value_after);
                if (notify_err != OS_TR181_SUCCESS)
                {
                    LOGW(LOG_PREFIX_INSTANCE_PARAM(
                            table_instance,
                            param_ctx->route->tr181_param,
                            "failed to send notification for sibling table %s column %s param %s change: %s -> %s: %s",
                            table_name,
                            column_name,
                            param_ctx->name,
                            value_before_str ?: "",
                            value_after_str ?: "",
                            os_tr181_error_string(notify_err)));
                }
                else
                {
                    json_object_set_new(notified, param_ctx->route->tr181_param, json_true());
                }
            }

            FREE(value_before_str);
            FREE(value_after_str);
            FREE(param_path);
            json_decref(value_before_owned);
            json_decref(value_after_owned);
            os_val_free(&tr181_value_before);
            os_val_free(&tr181_value_after);
        }
    }

    json_decref(notified);
}

static void ox_ovsdb_monitor_update_cb(ovsdb_update_monitor_t *mon)
{
    ox_ovsdb_monitor_t *ox_mon = mon->mon_data;
    ox_router_t *router = ox_mon->router;
    const char *table_name = mon->mon_table;
    const char *uuid = mon->mon_uuid;

    json_t *row_after_borrowed = mon->mon_json_new;
    json_t *row_before_owned = json_object();
    json_object_update(row_before_owned, mon->mon_json_new);
    json_object_update(row_before_owned, mon->mon_json_old);

    ox_table_ctx_t *table_ctx;
    ds_tree_foreach (&ox_mon->table_ctxs, table_ctx)
    {
        const ox_route_table_t *route = table_ctx->route;

        if (strcmp(route->ovsdb_table, table_name) != 0) continue;

        switch (mon->mon_type)
        {
            case OVSDB_UPDATE_NEW: {
                const bool ok = ox_table_ctx_insert(table_ctx, uuid, mon->mon_json_new);
                if (ok == false)
                {
                    LOGI(LOG_PREFIX_TABLE(table_ctx, "failed to process insert for uuid %s", uuid ?: ""));
                    ox_ovsdb_row_spawn(router, table_name, uuid);
                }
                else
                {
                    ox_ovsdb_row_despawn(router, table_name, uuid);
                }
            }
            break;
            case OVSDB_UPDATE_MODIFY:
                ox_ovsdb_monitor_notify(
                        table_ctx,
                        mon->mon_json_old,
                        row_before_owned,
                        row_after_borrowed,
                        table_name,
                        uuid);
                // FIXME: this needs to be implemented to handle
                // ovsdb_gating_column_name corner case, where initial value
                // (from OVSDB_UPDATE_NEW) does not map to any table_ctx, or needs
                // to re-map to a different route. However the tricky bit is to
                // handle the recursive instances that may need to be
                // (re)spawned and destroyed. That will need a robust system of
                // queueing "pending" or "dangling" ovsdb-only entries, and
                // re-evaluating all the dangling ones.
                break;
            case OVSDB_UPDATE_DEL: {
                const bool ok = ox_table_ctx_delete(table_ctx, uuid);
                if (ok == false)
                {
                    LOGE(LOG_PREFIX_TABLE(table_ctx, "failed to process delete for uuid %s", uuid ?: ""));
                }
                ox_ovsdb_row_despawn(router, table_name, uuid);
            }
            break;
            case OVSDB_UPDATE_ERROR:
                break;
        }
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_MODIFY:
            ds_tree_foreach (&ox_mon->sibling_table_ctxs, table_ctx)
            {
                if (strcmp(table_ctx->route->ovsdb_sibling_table, table_name) != 0) continue;

                LOGD(LOG_PREFIX_TABLE(table_ctx, "processing (mod) sibling table %s for uuid %s", table_name, uuid));

                char *primary_uuid = ox_route_sibling_row_to_primary_uuid(table_ctx->route, row_before_owned);
                if (primary_uuid != NULL)
                {
                    ox_ovsdb_monitor_notify(
                            table_ctx,
                            mon->mon_json_old,
                            row_before_owned,
                            row_after_borrowed,
                            table_name,
                            primary_uuid);
                    FREE(primary_uuid);
                }
            }
            break;
        case OVSDB_UPDATE_NEW:
            ds_tree_foreach (&ox_mon->sibling_table_ctxs, table_ctx)
            {
                if (strcmp(table_ctx->route->ovsdb_sibling_table, table_name) != 0) continue;

                LOGD(LOG_PREFIX_TABLE(table_ctx, "processing (new) sibling table %s for uuid %s", table_name, uuid));

                char *primary_uuid = ox_route_sibling_row_to_primary_uuid(table_ctx->route, row_after_borrowed);
                if (primary_uuid != NULL)
                {
                    json_t *primary_row_owned = ox_route_row_from_uuid(table_ctx->route, primary_uuid);
                    ox_ovsdb_monitor_notify(
                            table_ctx,
                            mon->mon_json_new,
                            primary_row_owned,
                            row_after_borrowed,
                            table_name,
                            primary_uuid);
                    FREE(primary_uuid);
                    json_decref(primary_row_owned);
                }
            }
            break;
        case OVSDB_UPDATE_DEL:
            // FIXME: Could do the inverse of OVSDB_UPDATE_NEW but it's mostly
            // pointless. If state/sibling row is disappearing then it almost
            // always means everything is disappearing because primary (config)
            // row is disappearing.
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }

    json_decref(row_before_owned);
    ox_ovsdb_row_re_evaluate_all(router);
}

static ox_ovsdb_monitor_t *ox_ovsdb_monitor_new(ox_router_t *router)
{
    ox_ovsdb_monitor_t *mon = CALLOC(1, sizeof(*mon));
    mon->router = router;
    ds_tree_init(&mon->table_ctxs, ds_str_cmp, ox_table_ctx_t, ovsdb_monitor_node);
    ds_tree_init(&mon->sibling_table_ctxs, ds_str_cmp, ox_table_ctx_t, ovsdb_monitor_sibling_node);
    return mon;
}

static ox_ovsdb_monitor_t *ox_ovsdb_monitor_open(ox_router_t *router, const char *mon_table)
{
    ox_ovsdb_monitor_t *mon = ox_ovsdb_monitor_new(router);
    const int mon_flags = 0;

    const bool ok = ovsdb_update_monitor(&mon->mon, ox_ovsdb_monitor_update_cb, (char *)mon_table, mon_flags);
    LOGD(LOG_PREFIX("registering OVSDB monitor for table %s: %s", mon_table, ok ? "success" : "failure"));
    if (ok == false)
    {
        LOGE(LOG_PREFIX("failed to register OVSDB update callback"));
        FREE(mon);
        return NULL;
    }

    mon->mon.mon_data = mon;
    ds_tree_insert(&router->ovsdb_monitors, mon, mon_table);
    return mon;
}

bool ox_ovsdb_monitor_register(ox_table_ctx_t *table_ctx)
{
    ox_router_t *router = table_ctx->router;
    const char *mon_table = table_ctx->route->ovsdb_table;
    ox_ovsdb_monitor_t *mon = ds_tree_find(&router->ovsdb_monitors, mon_table);
    if (mon == NULL)
    {
        mon = ox_ovsdb_monitor_open(router, mon_table);
        if (mon == NULL)
        {
            return false;
        }
    }
    const bool not_registered_yet = ds_tree_find(&mon->table_ctxs, table_ctx->route->tr181_table) == NULL;
    if (not_registered_yet)
    {
        ds_tree_insert(&mon->table_ctxs, table_ctx, table_ctx->route->tr181_table);
    }

    const char *sibling_mon_table = table_ctx->route->ovsdb_sibling_table;
    if (sibling_mon_table != NULL)
    {
        mon = ds_tree_find(&router->ovsdb_monitors, sibling_mon_table);
        if (mon == NULL)
        {
            mon = ox_ovsdb_monitor_open(router, sibling_mon_table);
            if (mon == NULL)
            {
                // FIXME: should clean up the other mon table
                return false;
            }
        }
        const bool not_registered_yet = ds_tree_find(&mon->sibling_table_ctxs, table_ctx->route->tr181_table) == NULL;
        if (not_registered_yet)
        {
            ds_tree_insert(&mon->sibling_table_ctxs, table_ctx, table_ctx->route->tr181_table);
        }
    }

    /* ovsdb_parent_table (eg. IP_Interface for IPv6Address) has no table_ctx
     * of its own, so it needs its own monitor to retry dangling child rows
     * on its own updates. ox_ovsdb_monitor_update_cb() unconditionally calls
     * ox_ovsdb_row_re_evaluate_all() regardless of table_ctx matches, so a
     * bare monitor is sufficient. */
    const char *parent_mon_table = table_ctx->route->ovsdb_parent_table;
    if (parent_mon_table != NULL)
    {
        mon = ds_tree_find(&router->ovsdb_monitors, parent_mon_table);
        if (mon == NULL)
        {
            mon = ox_ovsdb_monitor_open(router, parent_mon_table);
            if (mon == NULL)
            {
                // FIXME: should clean up the other mon table(s)
                return false;
            }
        }
    }

    return true;
}
