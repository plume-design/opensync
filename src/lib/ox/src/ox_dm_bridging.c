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
 * Device.Bridging.Bridge. / Device.Bridging.Bridge.{i}.Port.
 *
 * Sourced directly from the native OVS Bridge/Port/Interface tables, so
 * that bridge/port membership reflects the actual runtime bridge state
 * rather than the configured interface set. Port membership is unfiltered:
 * every uuid in Bridge.ports becomes a Port instance, including
 * OpenSync-internal service ports and the bridge's own self-referencing
 * member (identified as ManagementPort below).
 */

/* Select a single OVSDB row by uuid. Returns an owned row (caller must
 * json_decref()), or NULL if not found. Used throughout this file to read
 * columns not tracked as an ox_table_ctx (Bridge/Port/Interface are only
 * ever accessed here via direct queries, never via ox_table_instance_param_get). */
static json_t *ox_dm_bridging_row_by_uuid(const char *table, const char *uuid)
{
    if (uuid == NULL) return NULL;

    json_t *where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json(OX_OVSDB_UUID, OFUNC_EQ, ovsdb_tran_uuid_json(uuid));
    json_array_append_new(where_owned, cond_owned);
    cond_owned = NULL;  // moved to where_owned
    json_t *rows_owned = ovsdb_sync_select_where2(table, where_owned);
    where_owned = NULL;  // moved to ovsdb_sync_select_where2
    json_t *row_owned = json_incref(json_array_get(rows_owned, 0));
    json_decref(rows_owned);
    return row_owned;
}

/* Extract the first uuid out of an OVSDB uuid-valued column, whether it
 * arrives as a bare singleton ["uuid","..."] or wrapped in a set
 * ["set",[["uuid","..."],...]] - Port.interfaces/Bridge.ports can be either,
 * depending on member count. Returned string is borrowed from value_borrowed. */
static const char *ox_dm_bridging_first_uuid_from_column(json_t *value_borrowed)
{
    json_t *set_borrowed = ox_ovsdb_type_borrow_set(value_borrowed);
    json_t *first_borrowed = set_borrowed != NULL ? json_array_get(set_borrowed, 0) : value_borrowed;
    return ox_ovsdb_type_borrow_uuid(first_borrowed);
}

/* Resolve this Port instance's sole member Interface row (Port.interfaces is
 * min:1, and no bonding is observed on this platform, so the first uuid is
 * taken as the member). Returns an owned row (caller must json_decref()), or
 * NULL if unresolvable. */
static json_t *ox_dm_bridging_port_interface_row(const ox_table_instance_t *table_instance)
{
    if (WARN_ON(table_instance == NULL)) return NULL;
    if (WARN_ON(table_instance->uuid == NULL)) return NULL;

    json_t *port_row_owned = ox_dm_bridging_row_by_uuid("Port", table_instance->uuid);
    if (port_row_owned == NULL) return NULL;

    json_t *interfaces_borrowed = json_object_get(port_row_owned, "interfaces");
    const char *interface_uuid = ox_dm_bridging_first_uuid_from_column(interfaces_borrowed);
    json_t *interface_row_owned = ox_dm_bridging_row_by_uuid("Interface", interface_uuid);
    json_decref(port_row_owned);
    return interface_row_owned;
}

/*
 * Device.Bridging.Bridge.{i}.Enable / .Status
 *
 * The native Bridge table itself carries no usable enable/up-down signal
 * (Bridge.status is an empty map on every device seen so far), so this
 * cross-references the Wifi_Inet_Config/State row for the same bridge
 * (if_name==Bridge.name, if_type=="bridge") and reads its "enabled" column -
 * the same fallback source already relied on for Device.Ethernet.Interface.
 * No matching row (unexpected, but not fatal) defaults to enabled/Up: the
 * Bridge instance existing at all implies the bridge is operational.
 */
static bool ox_dm_bridging_bridge_enabled(const ox_table_instance_t *table_instance)
{
    json_t *bridge_row_owned = ox_dm_bridging_row_by_uuid("Bridge", table_instance->uuid);
    const char *bridge_name = json_string_value(json_object_get(bridge_row_owned, "name"));
    if (bridge_name == NULL)
    {
        json_decref(bridge_row_owned);
        return true;
    }
    char *bridge_name_owned = STRDUP(bridge_name);
    json_decref(bridge_row_owned);

    json_t *where_owned = json_array();
    json_array_append_new(
            where_owned,
            ovsdb_tran_cond_single_json("if_name", OFUNC_EQ, json_string(bridge_name_owned)));
    json_array_append_new(where_owned, ovsdb_tran_cond_single_json("if_type", OFUNC_EQ, json_string("bridge")));
    json_t *rows_owned = ovsdb_sync_select_where2("Wifi_Inet_Config", where_owned);
    where_owned = NULL;  // moved to ovsdb_sync_select_where2
    FREE(bridge_name_owned);

    json_t *row_borrowed = json_array_get(rows_owned, 0);
    json_t *enabled_borrowed = json_object_get(row_borrowed, "enabled");
    const bool enabled = json_is_boolean(enabled_borrowed) ? json_boolean_value(enabled_borrowed) : true;
    json_decref(rows_owned);
    return enabled;
}

static os_tr181_error_t ox_dm_bridging_get_bridge_enable(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)param_path;
    (void)route;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    return os_val_set_bool(tr181_value, ox_dm_bridging_bridge_enabled(table_instance));
}

static os_tr181_error_t ox_dm_bridging_get_bridge_status(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)param_path;
    (void)route;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    return os_val_set_str_dup(tr181_value, ox_dm_bridging_bridge_enabled(table_instance) ? "Up" : "Down");
}

/*
 * Device.Bridging.Bridge.{i}.Standard
 *
 * Not derivable from OVSDB; hardcoded for now.
 */
static os_tr181_error_t ox_dm_bridging_get_bridge_standard(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)param_path;
    (void)route;
    (void)table_instance;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;

    return os_val_set_str_dup(tr181_value, "802.1Q-2011");
}

/*
 * Device.Bridging.Bridge.{i}.Port.{i}.Enable / .Status / .PortState
 *
 * Interface.admin_state/link_state are optional; link_state has only been
 * observed populated for the bridge's own self-interface. Status/PortState
 * check admin_state first (a non-"up" value means down), then link_state
 * if present; absent both, it defaults to up.
 */
static bool ox_dm_bridging_port_enabled(const ox_table_instance_t *table_instance)
{
    json_t *interface_row_owned = ox_dm_bridging_port_interface_row(table_instance);
    const char *admin_state = json_string_value(json_object_get(interface_row_owned, "admin_state"));
    const bool enabled = admin_state != NULL ? (strcmp(admin_state, "up") == 0) : true;  // presence fallback
    json_decref(interface_row_owned);
    return enabled;
}

static bool ox_dm_bridging_port_up(const ox_table_instance_t *table_instance)
{
    json_t *interface_row_owned = ox_dm_bridging_port_interface_row(table_instance);
    const char *admin_state = json_string_value(json_object_get(interface_row_owned, "admin_state"));
    const char *link_state = json_string_value(json_object_get(interface_row_owned, "link_state"));
    bool up;
    if (admin_state != NULL && strcmp(admin_state, "up") != 0)
        up = false;
    else if (link_state != NULL)
        up = strcmp(link_state, "up") == 0;
    else
        up = true;  // admin_state already confirmed not "down" at this point
    json_decref(interface_row_owned);
    return up;
}

static os_tr181_error_t ox_dm_bridging_port_get_enable(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)param_path;
    (void)route;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    return os_val_set_bool(tr181_value, ox_dm_bridging_port_enabled(table_instance));
}

static os_tr181_error_t ox_dm_bridging_port_get_status(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)param_path;
    (void)route;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    return os_val_set_str_dup(tr181_value, ox_dm_bridging_port_up(table_instance) ? "Up" : "Down");
}

static os_tr181_error_t ox_dm_bridging_port_get_portstate(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)param_path;
    (void)route;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    return os_val_set_str_dup(tr181_value, ox_dm_bridging_port_up(table_instance) ? "Forwarding" : "Disabled");
}

/*
 * Device.Bridging.Bridge.{i}.Port.{i}.Type
 *
 * Hardcoded: VLAN support is out of scope for now,
 * so every port is a plain, VLAN-unaware bridge member
 */
static os_tr181_error_t ox_dm_bridging_port_get_type(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)param_path;
    (void)route;
    (void)table_instance;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;

    return os_val_set_str_dup(tr181_value, "VLANUnawarePort");
}

/*
 * Device.Bridging.Bridge.{i}.Port.{i}.ManagementPort
 *
 * true if this Port's name equals its parent Bridge's name - the bridge's
 * own self-referencing member (e.g. Port/Interface "br-home", a genuine
 * native member of Bridge.ports on every device dump seen so far) is the
 * ManagementPort; no synthetic row is needed.
 */
static bool ox_dm_bridging_port_is_management(const ox_table_instance_t *table_instance)
{
    if (table_instance->parent == NULL) return false;

    json_t *port_row_owned = ox_dm_bridging_row_by_uuid("Port", table_instance->uuid);
    json_t *bridge_row_owned = ox_dm_bridging_row_by_uuid("Bridge", table_instance->parent->uuid);
    const char *port_name = json_string_value(json_object_get(port_row_owned, "name"));
    const char *bridge_name = json_string_value(json_object_get(bridge_row_owned, "name"));
    const bool is_management = port_name != NULL && bridge_name != NULL && strcmp(port_name, bridge_name) == 0;
    json_decref(port_row_owned);
    json_decref(bridge_row_owned);
    return is_management;
}

static os_tr181_error_t ox_dm_bridging_port_get_management(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)param_path;
    (void)route;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    return os_val_set_bool(tr181_value, ox_dm_bridging_port_is_management(table_instance));
}

/*
 * Device.Bridging.Bridge.{i}.Port.{i}.LowerLayers
 *
 * For the ManagementPort=true row (required by spec whenever ManagementPort
 * is true): aggregates every *other* Port instance under the same Bridge
 * parent, via table_instance->parent->children_by_ptr - the framework's
 * existing parent/children linkage (used elsewhere for cascading deletes,
 * ox_table_instance.c).
 *
 * For every other row: best-effort cross-reference of the member
 * Interface's name against Wifi_Inet_Config (if_type=="eth") ->
 * Device.Ethernet.Interface, else Wifi_VIF_Config -> Device.WiFi.SSID.
 * Internal service ports (br-home.dhcp/.dpi/.l2uf/.tx/.ethc, ...) match
 * neither and are left with LowerLayers="".
 */
static os_tr181_error_t ox_dm_bridging_port_get_lowerlayers_management(
        os_tr181_val_t *tr181_value,
        const ox_table_instance_t *table_instance)
{
    char *csv_owned = NULL;

    ox_table_instance_t *sibling;
    ds_tree_foreach (&table_instance->parent->children_by_ptr, sibling)
    {
        if (sibling == table_instance) continue;

        char path[OS_TR181_PATH_MAX];
        STRSCPY(path, sibling->tr181_path);
        char *last_dot = strrchr(path, '.');
        char *last_char = path + strlen(path) - 1;
        if (last_dot != NULL && last_char != NULL && *last_char == '.' && last_dot == last_char)
        {
            *last_dot = '\0';
        }
        strgrow(&csv_owned, "%s,", path);
    }
    strchomp(csv_owned, ",");

    os_val_set_str_dup(tr181_value, csv_owned ?: "");
    FREE(csv_owned);
    return OS_TR181_SUCCESS;
}

static os_tr181_error_t ox_dm_bridging_port_get_lowerlayers_member(
        os_tr181_val_t *tr181_value,
        const ox_table_instance_t *table_instance)
{
    json_t *port_row_owned = ox_dm_bridging_row_by_uuid("Port", table_instance->uuid);
    const char *member_name = json_string_value(json_object_get(port_row_owned, "name"));
    if (member_name == NULL)
    {
        json_decref(port_row_owned);
        return os_val_set_str_dup(tr181_value, "");
    }
    char *member_name_owned = STRDUP(member_name);
    json_decref(port_row_owned);

    ox_router_t *router = table_instance->table_ctx->router;
    ox_table_instance_t *found = NULL;

    json_t *eth_where_owned = json_array();
    json_array_append_new(
            eth_where_owned,
            ovsdb_tran_cond_single_json("if_name", OFUNC_EQ, json_string(member_name_owned)));
    json_array_append_new(eth_where_owned, ovsdb_tran_cond_single_json("if_type", OFUNC_EQ, json_string("eth")));
    json_t *eth_rows_owned = ovsdb_sync_select_where2("Wifi_Inet_Config", eth_where_owned);
    eth_where_owned = NULL;  // moved to ovsdb_sync_select_where2
    const char *eth_uuid = ox_ovsdb_type_borrow_uuid(json_object_get(json_array_get(eth_rows_owned, 0), OX_OVSDB_UUID));
    if (eth_uuid != NULL)
    {
        found = ox_router_find_table_instance_by_uuid(router, DM_SCHEMA(Device.Ethernet.Interface), eth_uuid);
    }
    json_decref(eth_rows_owned);

    if (found == NULL)
    {
        json_t *vif_where_owned = json_array();
        json_array_append_new(
                vif_where_owned,
                ovsdb_tran_cond_single_json("if_name", OFUNC_EQ, json_string(member_name_owned)));
        json_t *vif_rows_owned = ovsdb_sync_select_where2("Wifi_VIF_Config", vif_where_owned);
        vif_where_owned = NULL;  // moved to ovsdb_sync_select_where2
        const char *vif_uuid =
                ox_ovsdb_type_borrow_uuid(json_object_get(json_array_get(vif_rows_owned, 0), OX_OVSDB_UUID));
        if (vif_uuid != NULL)
        {
            found = ox_router_find_table_instance_by_uuid(router, DM_SCHEMA(Device.WiFi.SSID), vif_uuid);
        }
        json_decref(vif_rows_owned);
    }
    FREE(member_name_owned);

    if (found == NULL) return os_val_set_str_dup(tr181_value, "");

    char path[OS_TR181_PATH_MAX];
    STRSCPY(path, found->tr181_path);
    char *last_dot = strrchr(path, '.');
    char *last_char = path + strlen(path) - 1;
    if (last_dot != NULL && last_char != NULL && *last_char == '.' && last_dot == last_char)
    {
        *last_dot = '\0';
    }

    return os_val_set_str_dup(tr181_value, path);
}

static os_tr181_error_t ox_dm_bridging_port_get_lowerlayers(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)param_path;
    (void)route;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance->parent == NULL)) return OS_TR181_ERROR;

    if (ox_dm_bridging_port_is_management(table_instance))
    {
        return ox_dm_bridging_port_get_lowerlayers_management(tr181_value, table_instance);
    }
    return ox_dm_bridging_port_get_lowerlayers_member(tr181_value, table_instance);
}

bool ox_dm_bridging_add_routes(ox_router_t *router)
{
    // clang-format off
    const ox_route_t routes[] = {
        {
            .table =
                    {
                        .tr181_table = DM_SCHEMA_DUP(Device.Bridging.Bridge),
                        .ovsdb_table = "Bridge",
                        .add_cb = ox_table_instance_add_reject_external,
                        .del_cb = ox_table_instance_del_reject_external,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Enable),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .get_cb = ox_dm_bridging_get_bridge_enable,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Status),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .get_cb = ox_dm_bridging_get_bridge_status,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Name),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "name",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Standard),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .get_cb = ox_dm_bridging_get_bridge_standard,
                    },
        },
        {
            .table =
                    {
                        .tr181_table = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Port),
                        .ovsdb_table = "Port",
                        .ovsdb_parent_table = "Bridge",
                        .ovsdb_parent_column = "ports",
                        .add_cb = ox_table_instance_add_reject_external,
                        .del_cb = ox_table_instance_del_reject_external,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Port.i.Name),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "name",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Port.i.Enable),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .get_cb = ox_dm_bridging_port_get_enable,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Port.i.Status),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .get_cb = ox_dm_bridging_port_get_status,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Port.i.PortState),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .get_cb = ox_dm_bridging_port_get_portstate,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Port.i.Type),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .get_cb = ox_dm_bridging_port_get_type,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Port.i.ManagementPort),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .get_cb = ox_dm_bridging_port_get_management,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.Bridging.Bridge.i.Port.i.LowerLayers),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .get_cb = ox_dm_bridging_port_get_lowerlayers,
                    },
        },
        {},
    };
    // clang-format on
    /* Heap-copy the stack array so the router's param_ctx->route pointers remain valid
     * for the process lifetime. Intentionally not freed. */
    ox_route_t *routes_heap = MALLOC(sizeof(routes));
    memcpy(routes_heap, routes, sizeof(routes));
    return ox_router_add_routes(router, routes_heap);
}
