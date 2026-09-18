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

#include <ctype.h>
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
 * Sentinel absolute timestamp (0001-01-01T00:00:00Z, epoch seconds) - the
 * TR-181-defined value for "not known", used as-is for
 * PreferredLifetime/ValidLifetime (see ox_dm_ip_get_lifetime() below
 * for why we cannot compute a real value here).
 */
#define OX_DM_IP_LIFETIME_UNKNOWN_SEC (-62135596800LL)

/*
 * Sentinel absolute timestamp (9999-12-31T23:59:59Z, epoch seconds) - the
 * TR-181-defined value for "infinite lifetime".
 */
#define OX_DM_IP_LIFETIME_INFINITE_SEC (253402300799LL)

/*
 * Raw OVSDB value meaning "infinite": the same "-1" sentinel lnx_ip6.c uses
 * for the kernel's own "infinite" lifetime string. Any other raw value
 * (including empty/absent) is treated as "unknown" instead.
 */
#define OX_DM_IP_LIFETIME_INFINITE_OVSDB_STR "-1"

/*
 * Device.IP.Interface. / Device.IP.Interface.{i}.IPv4Address.
 *
 * Both are sourced from Wifi_Inet_Config/Wifi_Inet_State, gated on
 * if_type=="eth" or if_type=="bridge" - a full inventory of physical
 * Ethernet ports and bridges.
 *
 * IPv4Address is a nested table synthesized from the very same OVSDB row as
 * its IP.Interface parent (ovsdb_parent_same_row - see ox_types.h), rather
 * than a real child OVSDB table, since IPv4_Address is unused in OpenSync.
 * It always has exactly one instance per IP.Interface instance.
 */

static const char *const ox_dm_ip_if_types[] = {"eth", "bridge", NULL};

/*
 * Device.IP.Interface.{i}.Status
 *
 * Wifi_Inet_State::enabled (bool) -> Up/Down/Unknown, mirroring
 * ox_dm_ethernet_status_from_ovsdb()'s convention in ox_dm_ethernet.c.
 */
static json_t *ox_dm_ip_status_from_ovsdb(
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

/*
 * Device.IP.Interface.{i}.Type
 *
 * Always "Normal": if_type is gated to "eth"/"bridge" only (ox_dm_ip_if_types
 * above), so the "Loopback"/"Tunnel"/"Tunneled"/"LANTrunk" TR-181 Type values
 * never apply here.
 */
static os_tr181_error_t ox_dm_ip_get_type(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)param_path;
    (void)route;
    (void)table_instance;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;

    os_val_set_str_dup(tr181_value, "Normal");
    return OS_TR181_SUCCESS;
}

/* Resolve this instance's own if_name directly from Wifi_Inet_Config by
 * uuid. Needed by ox_dm_ip_get_lowerlayers() to cross-reference the native
 * Bridge table (a separate OVSDB namespace, keyed by name rather than by
 * this row's uuid) for bridge-type instances. */
static char *ox_dm_ip_instance_if_name(const ox_table_instance_t *table_instance)
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

/* Cross-reference a bridge-type instance's if_name against the native
 * Bridge table's own "name" column. Unlike Ethernet.Link, Bridge is a
 * separate OVSDB table with its own uuid namespace (ox_dm_bridging.c), so
 * this joins on name rather than uuid. */
static ox_table_instance_t *ox_dm_ip_find_bridge_instance(ox_router_t *router, const char *if_name)
{
    json_t *where_owned = json_array();
    json_t *cond_owned = ovsdb_tran_cond_single_json("name", OFUNC_EQ, json_string(if_name));
    json_array_append_new(where_owned, cond_owned);
    cond_owned = NULL;  // moved to where_owned
    json_t *rows_owned = ovsdb_sync_select_where2("Bridge", where_owned);
    where_owned = NULL;  // moved to ovsdb_sync_select_where2
    const char *bridge_uuid = ox_ovsdb_type_borrow_uuid(json_object_get(json_array_get(rows_owned, 0), OX_OVSDB_UUID));
    ox_table_instance_t *bridge_instance =
            bridge_uuid != NULL
                    ? ox_router_find_table_instance_by_uuid(router, DM_SCHEMA(Device.Bridging.Bridge), bridge_uuid)
                    : NULL;
    json_decref(rows_owned);
    return bridge_instance;
}

/*
 * Device.IP.Interface.{i}.LowerLayers
 *
 * Type-based, static rule:
 *
 * For an "eth"-type IP.Interface instance, points at the pairedDevice.Ethernet.Link.{j}.
 * instance found via the shared uuid (both IP.Interface and Ethernet.Link
 * are gated against the same Wifi_Inet_Config/State row for physical ports -
 * IP.Interface via ox_dm_ip_if_types above, Ethernet.Link via if_type=="eth"
 * in ox_dm_ethernet.c - so a physical port's IP.Interface uuid is always
 * also an Ethernet.Link instance's uuid). Mirrors
 * ox_dm_ethernet_link_reference_interface()'s same-uuid cross-table lookup
 * pattern in ox_dm_ethernet.c.
 *
 * For a "bridge"-type IP.Interface instance, that same-uuid lookup naturally
 * fails (Ethernet.Link is only gated on "eth"), so this falls back to
 * joining the native Bridge table by if_name and pointing at the matching
 * Device.Bridging.Bridge.{k}. instance (see ox_dm_bridging.c).
 */
static os_tr181_error_t ox_dm_ip_get_lowerlayers(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;

    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;
    if (WARN_ON(table_instance == NULL)) return OS_TR181_ERROR;

    const char *uuid_str = table_instance->uuid;
    if (uuid_str == NULL)
    {
        LOGD(LOG_PREFIX_INSTANCE_PARAM(table_instance, param_path, "instance uuid is NULL"));
        return OS_TR181_ERROR;
    }

    ox_router_t *router = table_instance->table_ctx->router;
    const char *route_path = DM_SCHEMA(Device.Ethernet.Link);
    ox_table_instance_t *link_instance = ox_router_find_table_instance_by_uuid(router, route_path, uuid_str);
    if (link_instance == NULL)
    {
        /* Expected for bridge-type IP.Interface instances (see doc comment
         * above) - try the native Bridge table instead; also reached if
         * Ethernet was disabled via --disable-ethernet, in which case the
         * Bridge lookup below will likewise simply not find anything. */
        char *if_name_owned = ox_dm_ip_instance_if_name(table_instance);
        link_instance = if_name_owned != NULL ? ox_dm_ip_find_bridge_instance(router, if_name_owned) : NULL;
        FREE(if_name_owned);
        if (link_instance == NULL) return os_val_set_str_dup(tr181_value, "");
    }

    char path[OS_TR181_PATH_MAX];
    STRSCPY(path, link_instance->tr181_path);
    char *last_dot = strrchr(path, '.');
    char *last_char = path + strlen(path) - 1;
    if (last_dot != NULL && last_char != NULL && *last_char == '.' && last_dot == last_char)
    {
        *last_dot = '\0';
    }

    return os_val_set_str_dup(tr181_value, path);
}

/*
 * Device.IP.Interface.{i}.IPv4Address.{i}.Status
 *
 * Wifi_Inet_State::inet_addr non-empty and not "0.0.0.0" -> "Enabled", else
 * "Disabled" - a plain live-address-presence check. inet_addr is an
 * optional (min:0/max:1) OVSDB
 * column, so an unset value arrives here as a non-string (["set",[]])
 * rather than an empty string - json_is_string() below deliberately covers
 * both cases. "0.0.0.0" is the observed sentinel for "no address" on
 * unconfigured interfaces (eg. ip_assign_scheme=="none"), so it must be
 * treated the same as absent.
 */
static json_t *ox_dm_ip_ipv4_address_status_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;
    (void)table_instance;

    const char *inet_addr = json_is_string(value_borrowed) ? json_string_value(value_borrowed) : "";
    const bool has_address = strlen(inet_addr) > 0 && strcmp(inet_addr, "0.0.0.0") != 0;
    return json_string(has_address ? "Enabled" : "Disabled");
}

/*
 * Device.IP.Interface.{i}.IPv4Address.{i}.AddressingType
 *
 * Wifi_Inet_State::ip_assign_scheme ("none"/"dhcp"/"static") -> DHCP/Static.
 * TR-181 AddressingType has no enum value for "unconfigured"/"none" (its
 * declared enumeration is DHCP/IKEv2/AutoIP/IPCP/Static only), so "none" is
 * mapped to "Static" alongside the real "static" case.
 */
static json_t *ox_dm_ip_addressing_type_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;
    (void)table_instance;

    const char *scheme = json_string_value(value_borrowed) ?: "";
    const char *addressing_type = strcmp(scheme, "dhcp") == 0 ? "DHCP" : "Static";
    return json_string(addressing_type);
}

/*
 * Device.IP.Interface.{i}.IPv6Address.{i}.Status
 * Device.IP.Interface.{i}.IPv6Prefix.{i}.Status
 *
 * IPv6_Address::status / IPv6_Prefix::status (enum: disabled/enabled/error)
 * -> Disabled/Enabled/Error, shared by both tables since both OVSDB tables
 * declare the same three-value enum. This column is not implemented by
 * OpenSync NM, so when it's empty we fall back to the row's own "enable"
 * column instead of asserting "Disabled".
 */
static os_tr181_error_t ox_dm_ip_get_v6_status(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;

    ox_route_param_t status_route = *route;
    status_route.tr181_type = OS_TR181_TYPE_STRING;
    os_tr181_val_t status_value = OS_VAL_INIT();
    ox_table_instance_param_get(&status_value, param_path, &status_route, table_instance);
    const char *status = os_val_get_str_or(&status_value, "");

    const char *tr181_status;
    if (strcmp(status, "enabled") == 0)
    {
        tr181_status = "Enabled";
    }
    else if (strcmp(status, "error") == 0)
    {
        tr181_status = "Error";
    }
    else if (strcmp(status, "disabled") == 0)
    {
        tr181_status = "Disabled";
    }
    else
    {
        ox_route_param_t enable_route = *route;
        enable_route.tr181_type = OS_TR181_TYPE_BOOL;
        enable_route.ovsdb_column = "enable";
        os_tr181_val_t enable_value = OS_VAL_INIT();
        ox_table_instance_param_get(&enable_value, param_path, &enable_route, table_instance);
        tr181_status = os_val_get_bool_or(&enable_value, false) ? "Enabled" : "Disabled";
        os_val_free(&enable_value);
    }

    os_val_free(&status_value);
    return os_val_set_str_dup(tr181_value, tr181_status);
}

/*
 * Device.IP.Interface.{i}.IPv6Address.{i}.IPAddressStatus
 *
 * IPv6_Address::address_status (enum: preferred/deprecated/invalid/
 * inaccessible/unknown/tentative/duplicate/optimistic) -> the TR-181
 * IPAddressStatus enum, which uses the identical value set - simple
 * capitalize.
 */
static json_t *ox_dm_ip_v6_address_status_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;
    (void)table_instance;

    const char *status = json_string_value(value_borrowed) ?: "";
    char capitalized[32] = "Unknown";
    if (status[0] != '\0' && strlen(status) < sizeof(capitalized))
    {
        STRSCPY(capitalized, status);
        capitalized[0] = (char)toupper((unsigned char)capitalized[0]);
    }
    return json_string(capitalized);
}

/*
 * Device.IP.Interface.{i}.IPv6Prefix.{i}.PrefixStatus
 *
 * IPv6_Prefix::prefix_status (enum: preferred/deprecated/invalid/
 * inaccessible/unknown) -> the TR-181 PrefixStatus enum, same value set -
 * simple capitalize (confirmed: "Invalid").
 */
static json_t *ox_dm_ip_v6_prefix_status_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    return ox_dm_ip_v6_address_status_from_ovsdb(value_borrowed, route, table_instance);
}

/*
 * Device.IP.Interface.{i}.IPv6Address.{i}.Origin
 *
 * IPv6_Address::origin (enum: auto_configured/dhcp/ikev2/map/well_known/
 * static) -> AutoConfigured/DHCPv6/IKEv2/MAP/WellKnown/Static, per the
 * TR-181-declared enum naming.
 */
static json_t *ox_dm_ip_v6_address_origin_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;
    (void)table_instance;

    const char *origin = json_string_value(value_borrowed) ?: "";
    const char *tr181_origin;
    if (strcmp(origin, "dhcp") == 0)
        tr181_origin = "DHCPv6";
    else if (strcmp(origin, "ikev2") == 0)
        tr181_origin = "IKEv2";
    else if (strcmp(origin, "map") == 0)
        tr181_origin = "MAP";
    else if (strcmp(origin, "well_known") == 0)
        tr181_origin = "WellKnown";
    else if (strcmp(origin, "static") == 0)
        tr181_origin = "Static";
    else
        tr181_origin = "AutoConfigured";
    return json_string(tr181_origin);
}

/*
 * Device.IP.Interface.{i}.IPv6Prefix.{i}.Origin
 *
 * IPv6_Prefix::origin (enum: auto_configured/prefix_delegation/ra/well_known/
 * static/child) -> AutoConfigured/PrefixDelegation/RouterAdvertisement/
 * WellKnown/Static/Child, per the TR-181-declared enum naming.
 */
static json_t *ox_dm_ip_v6_prefix_origin_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;
    (void)table_instance;

    const char *origin = json_string_value(value_borrowed) ?: "";
    const char *tr181_origin;
    if (strcmp(origin, "prefix_delegation") == 0)
        tr181_origin = "PrefixDelegation";
    else if (strcmp(origin, "ra") == 0)
        tr181_origin = "RouterAdvertisement";
    else if (strcmp(origin, "well_known") == 0)
        tr181_origin = "WellKnown";
    else if (strcmp(origin, "static") == 0)
        tr181_origin = "Static";
    else if (strcmp(origin, "child") == 0)
        tr181_origin = "Child";
    else
        tr181_origin = "AutoConfigured";
    return json_string(tr181_origin);
}

/*
 * Device.IP.Interface.{i}.IPv6Prefix.{i}.StaticType
 *
 * IPv6_Prefix::static_type (enum: static/inapplicable/prefix_delegation/
 * child) -> Static/Inapplicable/PrefixDelegation/Child, per the
 * TR-181-declared enum naming.
 */
static json_t *ox_dm_ip_v6_prefix_static_type_from_ovsdb(
        json_t *value_borrowed,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    (void)route;
    (void)table_instance;

    const char *static_type = json_string_value(value_borrowed) ?: "";
    const char *tr181_static_type;
    if (strcmp(static_type, "static") == 0)
        tr181_static_type = "Static";
    else if (strcmp(static_type, "prefix_delegation") == 0)
        tr181_static_type = "PrefixDelegation";
    else if (strcmp(static_type, "child") == 0)
        tr181_static_type = "Child";
    else
        tr181_static_type = "Inapplicable";
    return json_string(tr181_static_type);
}

/*
 * Device.IP.Interface.{i}.IPv6Address.{i}.PreferredLifetime / ValidLifetime
 * Device.IP.Interface.{i}.IPv6Prefix.{i}.PreferredLifetime / ValidLifetime
 *
 * These are absolute-dateTime in TR-181, but OVSDB's preferred_lifetime/
 * valid_lifetime columns are a relative RFC 4862 remaining-seconds count
 * with no acquisition/renewal timestamp to anchor a conversion, and no
 * reliable renewal signal (routers typically re-advertise the same
 * duration on every RA, and OVSDB's _version doesn't change for a
 * content-identical write). So we can't compute a real value: this always
 * returns the TR-181 "not known" sentinel (0001-01-01T00:00:00Z), except
 * for the one raw value we can trust - the literal string "-1", the same
 * "infinite" sentinel lnx_ip6.c uses at the kernel/OSN layer - which maps
 * to the TR-181 "infinite" sentinel (9999-12-31T23:59:59Z).
 */
static os_tr181_error_t ox_dm_ip_get_lifetime(
        os_tr181_val_t *tr181_value,
        const char *param_path,
        const ox_route_param_t *route,
        const ox_table_instance_t *table_instance)
{
    if (WARN_ON(tr181_value == NULL)) return OS_TR181_ERROR;

    ox_route_param_t raw_route = *route;
    raw_route.tr181_type = OS_TR181_TYPE_STRING;
    os_tr181_val_t raw_value = OS_VAL_INIT();
    const os_tr181_error_t err = ox_table_instance_param_get(&raw_value, param_path, &raw_route, table_instance);

    bool is_infinite = false;
    if (err == OS_TR181_SUCCESS)
    {
        const char *raw_str = os_val_get_str_or(&raw_value, "");
        is_infinite = strcmp(raw_str, OX_DM_IP_LIFETIME_INFINITE_OVSDB_STR) == 0;
    }
    os_val_free(&raw_value);

    const os_tr181_timestamp_t ts = {
        .sec = is_infinite ? OX_DM_IP_LIFETIME_INFINITE_SEC : OX_DM_IP_LIFETIME_UNKNOWN_SEC,
        .offset = 0,
    };
    return os_val_set_datetime(tr181_value, ts);
}

bool ox_dm_ip_add_routes(ox_router_t *router)
{
    // clang-format off
    const ox_route_t routes[] = {
        {
            .table =
                    {
                        .tr181_table = DM_SCHEMA_DUP(Device.IP.Interface),
                        .ovsdb_table = "Wifi_Inet_Config",
                        .ovsdb_sibling_table = "Wifi_Inet_State",
                        .ovsdb_sibling_column = "if_name",
                        .ovsdb_gating_column_name = "if_type",
                        .ovsdb_gating_column_values = ox_dm_ip_if_types,
                        .add_cb = ox_table_instance_add_reject_external,
                        .del_cb = ox_table_instance_del_reject_external,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.Enable),
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
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.Status),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "enabled",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ip_status_from_ovsdb,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.Name),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "if_name",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.Type),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .get_cb = ox_dm_ip_get_type,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.LowerLayers),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .get_cb = ox_dm_ip_get_lowerlayers,
                    },
        },
        {
            .table =
                    {
                        .tr181_table = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv4Address),
                        .ovsdb_table = "Wifi_Inet_Config",
                        .ovsdb_sibling_table = "Wifi_Inet_State",
                        .ovsdb_sibling_column = "if_name",
                        .ovsdb_gating_column_name = "if_type",
                        .ovsdb_gating_column_values = ox_dm_ip_if_types,
                        .ovsdb_parent_same_row = true,
                        .add_cb = ox_table_instance_add_reject_external,
                        .del_cb = ox_table_instance_del_reject_external,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv4Address.i.Enable),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .ovsdb_column = "enabled",
                        .ovsdb_no_sibling = true,
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv4Address.i.Status),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "inet_addr",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ip_ipv4_address_status_from_ovsdb,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv4Address.i.IPAddress),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "inet_addr",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv4Address.i.SubnetMask),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "netmask",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param =
                                DM_SCHEMA_DUP(Device.IP.Interface.i.IPv4Address.i.AddressingType),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "ip_assign_scheme",
                        .ovsdb_sibling_only = true,
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ip_addressing_type_from_ovsdb,
                    },
        },
        {
            .table =
                    {
                        .tr181_table = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Address),
                        .ovsdb_table = "IPv6_Address",
                        .ovsdb_parent_table = "IP_Interface",
                        .ovsdb_parent_column = "ipv6_addr",
                        .add_cb = ox_table_instance_add_reject_external,
                        .del_cb = ox_table_instance_del_reject_external,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Address.i.Enable),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .ovsdb_column = "enable",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Address.i.Status),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "status",
                        .get_cb = ox_dm_ip_get_v6_status,
                    },
        },
        {
            .param =
                    {
                        .tr181_param =
                                DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Address.i.IPAddressStatus),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "address_status",
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ip_v6_address_status_from_ovsdb,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Address.i.IPAddress),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "address",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Address.i.Origin),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "origin",
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ip_v6_address_origin_from_ovsdb,
                    },
        },
        {
            .param =
                    {
                        .tr181_param =
                                DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Address.i.PreferredLifetime),
                        .tr181_type = OS_TR181_TYPE_DATETIME,
                        .ovsdb_column = "preferred_lifetime",
                        .get_cb = ox_dm_ip_get_lifetime,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Address.i.ValidLifetime),
                        .tr181_type = OS_TR181_TYPE_DATETIME,
                        .ovsdb_column = "valid_lifetime",
                        .get_cb = ox_dm_ip_get_lifetime,
                    },
        },
        {
            .table =
                    {
                        .tr181_table = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix),
                        .ovsdb_table = "IPv6_Prefix",
                        .ovsdb_parent_table = "IP_Interface",
                        .ovsdb_parent_column = "ipv6_prefix",
                        .add_cb = ox_table_instance_add_reject_external,
                        .del_cb = ox_table_instance_del_reject_external,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.Enable),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .ovsdb_column = "enable",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.Status),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "status",
                        .get_cb = ox_dm_ip_get_v6_status,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.PrefixStatus),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "prefix_status",
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ip_v6_prefix_status_from_ovsdb,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.Prefix),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "address",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.Origin),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "origin",
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ip_v6_prefix_origin_from_ovsdb,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.StaticType),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "static_type",
                        .get_cb = ox_table_instance_param_get,
                        .from_ovsdb = ox_dm_ip_v6_prefix_static_type_from_ovsdb,
                    },
        },
        {
            .param =
                    {
                        .tr181_param =
                                DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.ChildPrefixBits),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_column = "child_prefix_bits",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.OnLink),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .ovsdb_column = "on_link",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.Autonomous),
                        .tr181_type = OS_TR181_TYPE_BOOL,
                        .ovsdb_column = "autonomous",
                        .get_cb = ox_table_instance_param_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param =
                                DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.PreferredLifetime),
                        .tr181_type = OS_TR181_TYPE_DATETIME,
                        .ovsdb_column = "preferred_lifetime",
                        .get_cb = ox_dm_ip_get_lifetime,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.IP.Interface.i.IPv6Prefix.i.ValidLifetime),
                        .tr181_type = OS_TR181_TYPE_DATETIME,
                        .ovsdb_column = "valid_lifetime",
                        .get_cb = ox_dm_ip_get_lifetime,
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
