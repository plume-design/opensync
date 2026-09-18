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
#include <ox_router.h>
#include <ox_table_ctx.h>
#include <string.h>

void ox_ovsdb_row_spawn(ox_router_t *router, const char *table_name, const char *uuid)
{
    if (WARN_ON(router == NULL)) return;
    if (WARN_ON(table_name == NULL)) return;
    if (WARN_ON(uuid == NULL)) return;

    const bool already_exists = ds_tree_find(&router->ovsdb_rows, uuid) != NULL;
    if (WARN_ON(already_exists)) return;

    ox_ovsdb_row_t *row = CALLOC(1, sizeof(*row));
    row->router = router;
    row->table_name = STRDUP(table_name);
    row->uuid = STRDUP(uuid);
    ds_tree_insert(&router->ovsdb_rows, row, row->uuid);
    LOGD(LOG_PREFIX_ROW(row, "spawned"));
}

static void ox_ovsdb_row_free(ox_ovsdb_row_t *row, const char *reason_str)
{
    if (row == NULL) return;

    LOGD(LOG_PREFIX_ROW(row, "freeing (%s)", reason_str ?: "no reason"));
    if (row->router != NULL)
    {
        ds_tree_remove(&row->router->ovsdb_rows, row);
        row->router = NULL;
    }
    FREE(row->table_name);
    FREE(row->uuid);
    FREE(row);
}

void ox_ovsdb_row_despawn(ox_router_t *router, const char *table_name, const char *uuid)
{
    if (WARN_ON(router == NULL)) return;
    if (WARN_ON(table_name == NULL)) return;
    if (WARN_ON(uuid == NULL)) return;

    ox_ovsdb_row_t *row = ds_tree_find(&router->ovsdb_rows, uuid);
    if (row == NULL) return;

    LOGD(LOG_PREFIX_ROW(row, "despawning"));
    ox_ovsdb_row_free(row, "despawn");
}

static void ox_ovsdb_row_re_evaluate(ox_ovsdb_row_t *row)
{
    if (WARN_ON(row == NULL)) return;
    if (WARN_ON(row->router == NULL)) return;
    if (WARN_ON(row->table_name == NULL)) return;
    if (WARN_ON(row->uuid == NULL)) return;

    json_t *where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json(OX_OVSDB_UUID, OFUNC_EQ, ovsdb_tran_uuid_json(row->uuid));
    json_array_append_new(where_owned, cond_owned);
    json_t *rows_owned = ovsdb_sync_select_where2(row->table_name, where_owned);
    json_t *first_row_borrowed = json_array_get(rows_owned, 0);
    if (first_row_borrowed == NULL)
    {
        /* possibly a race, ovsdb managed to delete this before our local
         * socket delivered and processed the OVSDB_UPDATE_DEL */
        LOGD(LOG_PREFIX_ROW(
                row,
                "failed to re-evaluate, already removed probably due to a race, expecting despawn soon"));
        json_decref(rows_owned);
        return;
    }

    ox_router_t *router = row->router;
    const char *table_name = row->table_name;
    const char *uuid = row->uuid;

    // FIXME: This could re-use ox_route_ovsdb_process_update_cb() somehow.
    ox_table_ctx_t *table_ctx;
    ds_tree_foreach (&router->table_ctxs, table_ctx)
    {
        const ox_route_table_t *route = table_ctx->route;

        if (strcmp(route->ovsdb_table, table_name) != 0) continue;

        const bool ok = ox_table_ctx_insert(table_ctx, uuid, first_row_borrowed);
        if (ok)
        {
            ox_ovsdb_row_free(row, "re-evaluate success, no longer needed");
            break;
        }
    }

    json_decref(rows_owned);
}

void ox_ovsdb_row_re_evaluate_all(ox_router_t *router)
{
    if (WARN_ON(router == NULL)) return;

    bool changed;
    do
    {
        const size_t count_before = ds_tree_len(&router->ovsdb_rows);
        ox_ovsdb_row_t *row;
        ox_ovsdb_row_t *tmp;
        ds_tree_foreach_safe (&router->ovsdb_rows, row, tmp)
        {
            ox_ovsdb_row_re_evaluate(row);
        }
        const size_t count_after = ds_tree_len(&router->ovsdb_rows);
        changed = (count_before != count_after);
    } while (changed);
}
