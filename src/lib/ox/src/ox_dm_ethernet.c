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

#include "os_tr181_types.h"
#include <dm_schema.h>
#include <jansson.h>
#include <memutil.h>
#include <ovsdb_sync.h>
#include <ox_log.h>
#include <ox_ovsdb_type.h>
#include <ox_route.h>
#include <ox_router.h>
#include <ox_table_instance.h>
#include <ox_types.h>
#include <string.h>
#include <util.h>

/*
 * Device.Ethernet.Interface. / Device.Ethernet.Link.
 *
 * Both tables are sourced from Wifi_Inet_Config/Wifi_Inet_State, gated on
 * if_type=="eth" - a full inventory of physical Ethernet ports.
 *
 * Both routes use the identical gating condition against the same OVSDB
 * table, so a given uuid either yields both an Interface and a paired Link
 * instance, or neither - mirroring the existing Device.WiFi.SSID./
 * Device.WiFi.AccessPoint. pairing in ox_dm_wifi.c.
 */

static json_t *ox_dm_ethernet_status_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;
    (void)table_instance;

    const char *status_up = "Up";
    const char *status_down = "Down";
    const char *status_unknown = "Unknown";

    const char *status = json_is_boolean(value_borrowed)
                                 ? (json_boolean_value(value_borrowed) ? status_up : status_down)
                                 : status_unknown;

    return json_string(status);
}

static json_t *ox_dm_ethernet_duplex_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;
    (void)table_instance;

    const char *value_str = json_string_value(value_borrowed) ?: "";
    /* Wifi_Inet_State::link_duplex is one of "full"/"half"/"unknown". TR-181
     * CurrentDuplexMode only defines "Half"/"Full" - fall back to an empty
     * string (not-yet-determined) rather than guessing when unknown. */
    const char *duplex = strcmp(value_str, "full") == 0 ? "Full" : strcmp(value_str, "half") == 0 ? "Half" : "";
    return json_string(duplex);
}

/* Resolve this instance's own if_name directly from Wifi_Inet_Config by uuid.
 * Needed by ox_dm_ethernet_get_upstream() to cross-reference
 * Connection_Manager_Uplink, which is not itself tracked as an ox_table_ctx. */
static char *ox_dm_ethernet_instance_if_name(const ox_table_instance_t *table_instance)
{
    if (table_instance == NULL || table_instance->uuid == NULL) return NULL;

    json_t *where_owned = json_array();
    json_t *cond_owned =
            ovsdb_tran_cond_single_json(OX_OVSDB_UUID, OFUNC_EQ, ovsdb_tran_uuid_json(table_instance->uuid));
    json_array_append_new(where_owned, cond_owned);
    cond_owned = NULL;  // moved to where_owned
    json_t *rows_owned = ovsdb_sync_select_where2("Wifi_Inet_Config", where_owned);
    where_owned = NULL;  // moved to ovsdb_sync_select_where2
    json_t *first_row_borrowed = json_array_get(rows_owned, 0);
    const char *if_name_borrowed = json_string_value(json_object_get(first_row_borrowed, "if_name"));
    char *if_name_owned = if_name_borrowed != NULL ? STRDUP(if_name_borrowed) : NULL;
    json_decref(rows_owned);
    return if_name_owned;
}

/*
 * Device.Ethernet.Interface.{i}.Upstream
 *
 * Sourced from Connection_Manager_Uplink.if_name/is_used
 */
static os_tr181_error_t ox_dm_ethernet_get_upstream(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    char *if_name_owned = ox_dm_ethernet_instance_if_name(table_instance);
    if (if_name_owned == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE_PARAM(table_instance, param_path, "failed to resolve if_name for Upstream"));
        return os_val_set_bool(tr181_value, false);
    }

    json_t *where_owned = json_array();
    json_t *cond_if_name_owned = ovsdb_tran_cond_single_json("if_name", OFUNC_EQ, json_string(if_name_owned));
    json_array_append_new(where_owned, cond_if_name_owned);
    cond_if_name_owned = NULL;  // moved to where_owned
    json_t *cond_is_used_owned = ovsdb_tran_cond_single_json("is_used", OFUNC_EQ, json_true());
    json_array_append_new(where_owned, cond_is_used_owned);
    cond_is_used_owned = NULL;  // moved to where_owned
    json_t *rows_owned = ovsdb_sync_select_where2("Connection_Manager_Uplink", where_owned);
    where_owned = NULL;  // moved to ovsdb_sync_select_where2
    const bool is_upstream = json_array_size(rows_owned) > 0;
    json_decref(rows_owned);
    FREE(if_name_owned);

    return os_val_set_bool(tr181_value, is_upstream);
}

/*
 * Device.Ethernet.Link.{i}.LowerLayers
 *
 * Points at the paired Device.Ethernet.Interface.{k}. instance, found via the
 * shared uuid (both tables are gated identically against the same OVSDB
 * table, so a Link instance's uuid is always also an Interface instance's
 * uuid). Mirrors ox_dm_wifi_vif_reference_ssid()'s same-uuid cross-table
 * lookup pattern in ox_dm_wifi.c.
 */
static os_tr181_error_t ox_dm_ethernet_link_reference_interface(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(param_path == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    const char *uuid_str = table_instance->uuid;
    if (uuid_str == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE_PARAM(table_instance, param_path, "instance uuid is NULL"));
        return OS_TR181_ERROR;
    }

    ox_router_t *router = table_instance->table_ctx->router;
    const char *route_path = DM_SCHEMA(Device.Ethernet.Interface);
    ox_table_instance_t *interface_instance = ox_router_find_table_instance_by_uuid(router, route_path, uuid_str);
    if (interface_instance == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE_PARAM(
                table_instance,
                param_path,
                "failed to find %s instance for uuid %s",
                route_path,
                uuid_str));
        return OS_TR181_ERROR;
    }

    char path[OS_TR181_PATH_MAX]; /* needs to match Device.Ethernet.Interface.{i}. */
    STRSCPY(path, interface_instance->tr181_path);
    char *last_dot = strrchr(path, '.');
    char *last_char = path + strlen(path) - 1;
    if (last_dot != NULL && last_char != NULL && *last_char == '.' && last_dot == last_char)
    {
        *last_dot = '\0';
    }

    os_val_set_str_dup(tr181_value, path);
    return OS_TR181_SUCCESS;
}

bool ox_dm_ethernet_add_routes(ox_router_t *router)
{
    const ox_route_t routes[] = {
        {
            .table =
                    {
                        .tr181_table = DM_SCHEMA_DUP(Device.Ethernet.Interface),
                        .ovsdb_table = "Wifi_Inet_Config",
                        .ovsdb_sibling_table = "Wifi_Inet_State",
                        .ovsdb_sibling_column = "if_name",
                        .ovsdb_gating_column_name = "if_type",
                        .ovsdb_gating_column_value = "eth",
                        .add_cb = ox_table_instance_add_reject_external,
                        .del_cb = ox_table_instance_del_reject_external,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Interface.i.Enable),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .ovsdb_column = "enabled",
                        .ovsdb_no_sibling = true,
                        .get_cb = ox_table_instance_param_get,
                        .set_cb = ox_table_instance_param_set,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Interface.i.Name),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "if_name",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Interface.i.MACAddress),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "hwaddr",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Interface.i.Status),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "enabled",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ethernet_status_from_ovsdb,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Interface.i.CurrentDuplexMode),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "link_duplex",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ethernet_duplex_from_ovsdb,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Interface.i.CurrentBitRate),
                        .tr181_type = OS_TR181_TYPE_UINT,
                        .ovsdb_column = "link_speed",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Interface.i.Upstream),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .get_cb = ox_dm_ethernet_get_upstream,
                    },
        },
        {
            .table =
                    {
                        .tr181_table = DM_SCHEMA_DUP(Device.Ethernet.Link),
                        .ovsdb_table = "Wifi_Inet_Config",
                        .ovsdb_sibling_table = "Wifi_Inet_State",
                        .ovsdb_sibling_column = "if_name",
                        .ovsdb_gating_column_name = "if_type",
                        .ovsdb_gating_column_value = "eth",
                        .add_cb = ox_table_instance_add_reject_external,
                        .del_cb = ox_table_instance_del_reject_external,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Link.i.Enable),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .ovsdb_column = "enabled",
                        .ovsdb_no_sibling = true,
                        .get_cb = ox_table_instance_param_get,
                        .set_cb = ox_table_instance_param_set,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Link.i.Name),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "if_name",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Link.i.MACAddress),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "hwaddr",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Link.i.Status),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "enabled",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ethernet_status_from_ovsdb,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Ethernet.Link.i.LowerLayers),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .get_cb = ox_dm_ethernet_link_reference_interface,
                    },
        },
        {},
    };
    /* Heap-copy the stack array so the router's param_ctx->route pointers remain valid
     * for the process lifetime. Intentionally not freed. */
    ox_route_t *routes_heap = MALLOC(sizeof(routes));
    memcpy(routes_heap, routes, sizeof(routes));
    return ox_router_add_routes(router, routes_heap);
}
