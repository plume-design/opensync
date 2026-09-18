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

#ifndef OX_TYPES_H_INCLUDED
#define OX_TYPES_H_INCLUDED

#include <os_tr181.h>
#include <os_tr181_types.h>
#include <ovsdb_update.h>
#include <ds_tree.h>

// FIXME: These don't belong here?
#define OX_PARAM_UUID "X_PLUME_uuid"
#define OX_OVSDB_UUID "_uuid"

enum ox_err
{
    OX_SUCCESS,
    OX_ERROR,
};
typedef enum ox_err ox_err_t;

struct ox_router;
typedef struct ox_router ox_router_t;

struct ox_route;
typedef struct ox_route ox_route_t;

struct ox_route_table;
typedef struct ox_route_table ox_route_table_t;

struct ox_route_param;
typedef struct ox_route_param ox_route_param_t;

struct ox_route_object;
typedef struct ox_route_object ox_route_object_t;

struct ox_table_instance;
typedef struct ox_table_instance ox_table_instance_t;

struct ox_table_ctx;
typedef struct ox_table_ctx ox_table_ctx_t;

struct ox_param_ctx;
typedef struct ox_param_ctx ox_param_ctx_t;

struct ox_ovsdb_monitor;
typedef struct ox_ovsdb_monitor ox_ovsdb_monitor_t;

struct ox_ovsdb_row;
typedef struct ox_ovsdb_row ox_ovsdb_row_t;

typedef os_tr181_error_t (
        *ox_route_add_fn_t)(ox_table_instance_t *table_instance, const os_tr181_val_t *initial_values);

typedef os_tr181_error_t (*ox_route_del_fn_t)(ox_table_instance_t *table_instance);

typedef os_tr181_error_t (*ox_route_get_fn_t)(
        os_tr181_val_t *value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance);

typedef os_tr181_error_t (*ox_route_set_fn_t)(
        const os_tr181_val_t *value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance);

/**
 * The caller only borrows json_t. The callee must _not_ free it. If the callee
 * wants to re-use it, it can use json_incref() to make sure it isn't freed by
 * the caller at some later point.
 */
typedef json_t *(
        *ox_route_xlate_fn_t)(json_t *value, const ox_route_param_t *route, const ox_table_instance_t *route_instance);

/**
 * This is intended to return an OVSDB style row json_t object with initial
 * OVSDB column values.
 *
 * For example, for lists, a [set,[]] is expected. Same for dicts:
 * [map,[[k,v],...]] is expected.
 */
typedef json_t *(*ox_route_init_row_fn_t)(const ox_route_table_t *route);

/**
 * Represents an instantiated table route in os_tr181.
 *
 * This is intended to be coupled with a single OVSDB row most of the time. The
 * uuid is typically set up within ox_route_add_fn_t and then used within
 * ox_route_get_fn_t, ox_route_set_fn_t, ox_route_del_fn_t.
 *
 * It is possible to have multiple instances pointing to the same OVSDB row
 * (same table+uuid). One example is Device.WiFi.AccessPoint.{i} and
 * Device.WiFi.SSID.{i}. OVSDB does not distinguish between the two.
 */
struct ox_table_instance
{
    ds_tree_node_t router_node_by_path;    /* keyed via this->tr181_path */
    ds_tree_node_t table_ctx_node_by_uuid; /* keyed via this->uuid */
    ds_tree_node_t instance_node_by_ptr;
    ox_router_t *router;
    ox_table_ctx_t *table_ctx;
    ox_table_instance_t *parent; /* eg. pointer to instance associated with
                                    Device.WiFi.AccessPoint.{i} for
                                    Device.WiFi.AccessPoint.{i}.AssociatedDevice.{i}
                                    */
    json_t *stash;               /* for use by callbacks, eg. to stash values that can't be
                                    stored in OVSDB, but can be used to (eventually) program
                                    OVSDB. One example is Wi-Fi 320MHz center freq signalling. */
    ds_tree_t children_by_ptr;
    char *tr181_path; /* eg. "Device.WiFi.Radio.1." */
    char *uuid;       /* eg. "123e4567-e89b-12d3-a456-426614174000" */
};

/**
 * Describes a single route, which can be either a table or a parameter.
 *
 * The type of a route is determined by the presence of table or param fields,
 * see ox_route_is_table() and ox_route_is_param().
 *
 * The table field is intended for routes that are primarily about spawning
 * TR-181 instances based on OVSDB rows.
 *
 * The ovsdb_parent_table and ovsdb_parent_column are intended for cases where
 * the route is dependant on another table. An example of that is
 * Device.WiFi.AccessPoint.{i}.AssociatedDevice.{i} which is dependant on both
 * Wifi_VIF_State (and by proxy Wifi_VIF_Config) and Wifi_Associated_Clients.
 *
 * Parameters that happen to be within a table will carry on the same
 * ox_route_instance_t, when appropriate/available.
 *
 * For _Config and _State OVSDB split there is ovsdb_table_state,
 * ovsdb_table_state_sibling_column, ovsdb_table_state_referencing_column.
 *
 * The table_state_sibling_column is typically something like eg. if_name, that
 * is shared across the _Config and _State tables.
 *
 * The table_state_referencing_column is typically something like eg.
 * vif_config (Wifi_VIF_State::vif_config pointing back to
 * Wifi_VIF_Config::_uuid), where uuid refernce exists between _Config and
 * _State.
 *
 * The ovsdb_gating_column_name, ovsdb_gating_column_value is intended for
 * cases where not all rows in the OVSDB table are relevant for the route.
 *
 * For example Wifi_Inet_Config has rows for both "eth", "bridge", etc. Some of
 * them will need to be diverted to Device.Ethernet, and some to
 * Device.Bridging.Bridge.{i}.
 *
 * ovsdb_gating_column_values is an alternative to ovsdb_gating_column_value
 * for routes that need to accept more than one value for the gating column
 * (eg. Device.IP.Interface. spanning both "eth" and "bridge" rows). It is a
 * NULL-terminated array of accepted values and is mutually exclusive with
 * ovsdb_gating_column_value - set at most one of the two.
 *
 * ovsdb_parent_same_row is for nested tables whose "child" instances are not
 * separate OVSDB rows referenced from the parent row via a set/array column
 * (that case is ovsdb_parent_table/ovsdb_parent_column), but are instead the
 * very same OVSDB row as the parent, simply projected into a deeper TR-181
 * nesting level. For example Device.IP.Interface.{i}.IPv4Address. is
 * synthesized directly from the same Wifi_Inet_Config/State row as its
 * Device.IP.Interface.{i} parent (no separate IPv4Address OVSDB table is in
 * use), so the child's parent instance is found by looking up the child's own
 * uuid directly in the parent route's table, without an OVSDB query. Mutually
 * exclusive with ovsdb_parent_table/ovsdb_parent_column.
 */
struct ox_route_table
{
    const char *tr181_table;                       /* eg. "Device.WiFi.AccessPoint." */
    const char *ovsdb_table;                       /* eg. Wifi_VIF_Config */
    const char *ovsdb_sibling_table;               /* eg. Wifi_VIF_State */
    const char *ovsdb_sibling_column;              /* eg. "if_name" */
    const char *ovsdb_sibling_ref_column;          /* eg. "vif_config" (for ovsdb_sibling_table=Wifi_VIF_State) */
    const char *ovsdb_parent_table;                /* eg. Wifi_VIF_State */
    const char *ovsdb_parent_column;               /* eg. "associated_clients" */
    const char *ovsdb_gating_column_name;          /* eg. "vif_type", "mode" */
    const char *ovsdb_gating_column_value;         /* eg. "eth", "ap" */
    const char *const *ovsdb_gating_column_values; /* eg. {"eth", "bridge", NULL} */
    bool ovsdb_parent_same_row;                    /* see doc comment above */
    ox_route_add_fn_t add_cb;
    ox_route_del_fn_t del_cb;
    ox_route_init_row_fn_t init_row_cb;
};

struct ox_route_param
{
    const char *tr181_param;    /* eg. "Device.WiFi.SSID.{i}.SSID" */
    const char *tr181_stash_as; /* store cached value in ox_table_instance::stash[tr181_stash_as] */
    os_tr181_param_type_t tr181_type;

    // FIXME drop the ovsdb_table? no - cant do that, but could skip a number of cases
    const char *ovsdb_table;   /* eg. Wifi_VIF_Config */
    const char *ovsdb_column;  /* eg. "ssid", comma separated: "freq_band,channel" */
    const char *ovsdb_map_key; /* eg. "key" to address a specific key in a map column, like Wifi_VIF_Config::security */
    bool ovsdb_no_sibling;   /* to force use of ovsdb_table for parameters that exist in both, eg. _Config and _State */
    bool ovsdb_sibling_only; /* to force use of sibling table for parameters that exist in both, eg. _Config and _State
                              */
    ox_route_get_fn_t get_cb;
    ox_route_set_fn_t set_cb;
    ox_route_xlate_fn_t from_ovsdb; /* given get_cb() implemention may use this */
    ox_route_xlate_fn_t to_ovsdb;   /* given set_cb() implemention may use this */
};

struct ox_route_object
{
    const char *tr181_object;
};

struct ox_route
{
    ox_route_table_t table;
    ox_route_param_t param;
    ox_route_object_t object;
};

/**
 * Context for a single route. This is spawned for each ox_route_t and is passed
 * as user_data in os_tr181, see eg. os_tr181_register_table() calls. This is
 * done in order to maintain const ox_route_t but have runtime state associated
 * with it.
 */
struct ox_table_ctx  // probably split to ox_route_table_ctx and ox_route_param_ctx
{
    ds_tree_node_t router_node;
    ds_tree_node_t ovsdb_monitor_node;
    ds_tree_node_t ovsdb_monitor_sibling_node;
    ds_tree_node_t parent_table_ctx_node;
    ox_router_t *router;
    ox_table_ctx_t *parent_table_ctx; /* eg. ox_table_ctx for Device.WiFi.AccessPoint. is parent of
                                         Device.WiFi.AccessPoint.{i}.AssociatedDevice. */
    const ox_route_table_t *route;
    ds_tree_t table_instances_by_uuid;
    ds_tree_t param_ctxs; /* via ox_param_ctx_t::table_ctx_node, keyed by ox_param_ctx_t::name */
    ds_tree_t table_ctxs; /* via ox_table_ctx_t::parent_table_ctx_node, keyed ox_table_ctx_t::route->tr181_path */
};

struct ox_param_ctx
{
    ds_tree_node_t router_node;
    ds_tree_node_t table_ctx_node; /* for param that is part of a table, link to the table_ctx */
    ox_router_t *router;
    ox_table_ctx_t *table_ctx;
    const ox_route_param_t *route;
    char *name; /* eg. "Channel", "Name" (the last part of a path found in route->tr181_param */
};

/**
 * Keeps track of OVSDB monitors for set of routes. Multiple routes can (and
 * will) rely on the same table for event sourcing.
 */
struct ox_ovsdb_monitor
{
    ds_tree_node_t router_node;
    ox_router_t *router;
    ds_tree_t table_ctxs;         /* ox_table_ctx::ovsdb_monitor_node, key=ox_table_ctx::route->tr181_path */
    ds_tree_t sibling_table_ctxs; /* ox_table_ctx::ovsdb_monitor_sibling_node, key=ox_table_ctx::route->tr181_path */
    ovsdb_update_monitor_t mon;
};

/**
 * Keeps track of OVSDB rows that were reported, but could not be tracked as
 * ox_table_instance, or stopped being eligible to be tracked.
 *
 * This allows handling corner cases like:
 *  - initial values fail any match any ovsdb_gating_column_value
 *  - initial values fail to resolve ovsdb_parent_table
 *  - parent instance gets detached (eg. due to gating rename)
 */
struct ox_ovsdb_row
{
    ds_tree_node_t router_node;
    ox_router_t *router;
    char *table_name; /* eg. Wifi_VIF_Config */
    char *uuid;       /* eg. "123e4567-e89b-12d3-a456-426614174000" */
};

/**
 * The main/root container that manages the TR-181 handle, registered routes,
 * and OVSDB monitors.
 *
 * The user is expected to supplement paths with custom calls against
 * os_tr181_handle, eg. to add methods, or more complex parameters.
 */
struct ox_router
{
    os_tr181_handle_t *tr181_handle;
    ds_tree_t table_ctxs;
    ds_tree_t table_instances_by_path;
    ds_tree_t param_ctxs;
    ds_tree_t ovsdb_monitors;
    ds_tree_t ovsdb_rows;
};

#endif /* OX_TYPES_H_INCLUDED */
