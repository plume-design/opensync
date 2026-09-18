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

/*
 * WANO STA pipeline -- see wano_sta_ppline.h for the module description.
 *
 * L2/L3 state evaluation follows the same sources the generic WANO pipeline
 * uses:
 *  - has_L2: Wifi_Master_State:port_state == "active"
 *  - has_L3: has_L2 and a valid IPv4 address in Wifi_Inet_State:inet_addr
 */

#include <string.h>

#include "log.h"
#include "memutil.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "schema.h"
#include "ds_tree.h"

#include "wano.h"
#include "wano_sta_ppline.h"

#define MODULE_ID LOG_MODULE_ID_MISC

/* Routing table for default routes received by the DHCP client on the STA WAN link */
#define WANO_STA_DHCP_ROUTING_TABLE 100
/* Priority of the policy routing rules selecting WANO_STA_DHCP_ROUTING_TABLE */
#define WANO_STA_DHCP_ROUTING_TABLE_PRIORITY 100

/* Name prefixes and priority for the policy routing rule(s) for Connectivity_Check
 * probes running via the STA uplink */
#define WANO_STA_PPLINE_PRR_CC_NAME_PREFIX "policy.sta_uplink.cc."
#define WANO_STA_PPLINE_PRR_CC_PRIORITY    (WANO_STA_DHCP_ROUTING_TABLE_PRIORITY - 2)

/* Connectivity_Check row parameters for the STA uplink internet probe */
#define WANO_STA_PPLINE_CC_NAME_PREFIX "device.backup_wan."
#define WANO_STA_PPLINE_CC_TARGET      "8.8.8.8"
#define WANO_STA_PPLINE_CC_INTERVAL    5
#define WANO_STA_PPLINE_CC_TIMEOUT     10

struct wano_sta_ppline
{
    char spl_if_name[C_IFNAME_LEN];
    char spl_if_type[C_IFNAME_LEN];
    char spl_inet_role_str[64 + 1]; /* Wifi_Inet_Config:role to configure (empty = not set) */
    bool spl_test_connection;       /* Test connections do not insert CMU rows */
    wano_sta_ppline_event_cb_fn_t *spl_event_cb;

    bool spl_started;
    bool spl_port_active;  /* Wifi_Master_State:port_state == "active" */
    bool spl_ipaddr_valid; /* Wifi_Inet_State:inet_addr is a valid address */
    bool spl_cc_status_ok; /* Connectivity_Check:status == "ok" */
    bool spl_has_l2;
    bool spl_has_l3;
    bool spl_conn_check_ok;      /* Evaluated: spl_cc_status_ok while L2/L3 up */
    bool spl_cmu_inserted;       /* Connection_Manager_Uplink row inserted by us */
    bool spl_conn_check_started; /* Connectivity_Check row inserted by us */
    bool spl_prr_configured;     /* Policy routing rules installed for us */

    ds_tree_node_t spl_tnode;
};

static ds_tree_t g_sta_ppline_list = DS_TREE_INIT(ds_str_cmp, struct wano_sta_ppline, spl_tnode);
static bool g_sta_ppline_initialized = false;

static ovsdb_table_t table_Wifi_Inet_State;
static ovsdb_table_t table_Wifi_Master_State;
static ovsdb_table_t table_Wifi_Inet_Config;
static ovsdb_table_t table_Policy_Routing_Rule;
static ovsdb_table_t table_Connectivity_Check;

static bool wano_sta_ppline_init(void);
static void wano_sta_ppline_eval(struct wano_sta_ppline *spl);
static void wano_sta_ppline_cmu_sync(struct wano_sta_ppline *spl);
static bool wano_sta_ppline_connectivity_check_start(struct wano_sta_ppline *spl);
static bool wano_sta_ppline_connectivity_check_stop(struct wano_sta_ppline *spl);
static void wano_sta_ppline_wis_cb(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Inet_State *old,
        struct schema_Wifi_Inet_State *new);
static void wano_sta_ppline_wms_cb(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Master_State *old,
        struct schema_Wifi_Master_State *new);
static void wano_sta_ppline_cc_cb(
        ovsdb_update_monitor_t *mon,
        struct schema_Connectivity_Check *old,
        struct schema_Connectivity_Check *new);

/* Initialize wano_sta_ppline module. */
static bool wano_sta_ppline_init(void)
{
    OVSDB_TABLE_INIT(Wifi_Inet_State, if_name);
    OVSDB_TABLE_INIT(Wifi_Master_State, if_name);
    OVSDB_TABLE_INIT(Wifi_Inet_Config, if_name);
    OVSDB_TABLE_INIT(Policy_Routing_Rule, name);
    OVSDB_TABLE_INIT(Connectivity_Check, name);

    if (!ovsdb_table_monitor(&table_Wifi_Inet_State, table_cb_cast_Wifi_Inet_State(wano_sta_ppline_wis_cb), true))
    {
        LOG(ERR, "wano_sta_ppline: Error monitoring Wifi_Inet_State");
        return false;
    }

    if (!ovsdb_table_monitor(&table_Wifi_Master_State, table_cb_cast_Wifi_Master_State(wano_sta_ppline_wms_cb), true))
    {
        LOG(ERR, "wano_sta_ppline: Error monitoring Wifi_Master_State");
        return false;
    }

    if (!ovsdb_table_monitor(&table_Connectivity_Check, table_cb_cast_Connectivity_Check(wano_sta_ppline_cc_cb), true))
    {
        LOG(ERR, "wano_sta_ppline: Error monitoring Connectivity_Check");
        return false;
    }

    LOG(INFO, "wano_sta_ppline: Module initialized");
    return true;
}

/* Create a new wano_sta_ppline object. */
wano_sta_ppline_t *wano_sta_ppline_new(const char *if_name, const char *if_type)
{
    struct wano_sta_ppline *spl;

    if (!g_sta_ppline_initialized)
    {
        if (!wano_sta_ppline_init()) return NULL;
        g_sta_ppline_initialized = true;
    }

    if (ds_tree_find(&g_sta_ppline_list, (void *)if_name) != NULL)
    {
        LOG(WARN, "wano_sta_ppline: %s: Pipeline already exists", if_name);
        return NULL;
    }

    spl = CALLOC(1, sizeof(*spl));
    STRSCPY(spl->spl_if_name, if_name);
    STRSCPY(spl->spl_if_type, if_type);
    spl->spl_test_connection = false;

    ds_tree_insert(&g_sta_ppline_list, spl, spl->spl_if_name);

    return spl;
}

/* Register an optional event reporting callback. Call before _start(). */
void wano_sta_ppline_event_cb_init(wano_sta_ppline_t *ppline, wano_sta_ppline_event_cb_fn_t *cb)
{
    ppline->spl_event_cb = cb;
}

/* Set the Wifi_Inet_Config:role to configure on the STA uplink interface. Call before _start(). */
void wano_sta_ppline_set_inet_role(wano_sta_ppline_t *ppline, const char *inet_role)
{
    if (inet_role == NULL)
    {
        ppline->spl_inet_role_str[0] = '\0';
        return;
    }

    STRSCPY(ppline->spl_inet_role_str, inet_role);
}

/* Mark this pipeline as a "test connection". Call before _start(). */
void wano_sta_ppline_set_test_connection(wano_sta_ppline_t *ppline, bool test_connection)
{
    ppline->spl_test_connection = test_connection;
}

/* Unregister the pipeline (stopping it first if active) and free it. */
void wano_sta_ppline_del(wano_sta_ppline_t *ppline)
{
    if (ppline == NULL) return;

    wano_sta_ppline_stop(ppline);

    ds_tree_remove(&g_sta_ppline_list, ppline);
    FREE(ppline);
}

const char *wano_sta_ppline_if_name_get(const wano_sta_ppline_t *ppline)
{
    return ppline->spl_if_name;
}

const char *wano_sta_ppline_event_str(enum wano_sta_ppline_event event)
{
    switch (event)
    {
        case WANO_STA_PPLINE_L2_UP:
            return "L2_UP";
        case WANO_STA_PPLINE_L3_UP:
            return "L3_UP";
        case WANO_STA_PPLINE_CONN_CHECK_OK:
            return "CONN_CHECK_OK";
        case WANO_STA_PPLINE_CONN_CHECK_NOK:
            return "CONN_CHECK_NOK";
        case WANO_STA_PPLINE_L3_DOWN:
            return "L3_DOWN";
        case WANO_STA_PPLINE_L2_DOWN:
            return "L2_DOWN";
    }
    return "UNKNOWN";
}

/* Wifi_Inet_Config: DHCP configure/deconfigure of the STA uplink interface */
static bool wano_sta_ppline_inet_dhcp_configure(struct wano_sta_ppline *spl)
{
    struct schema_Wifi_Inet_Config iconf;
    static const int dhcp_req[] = {1, 3, 6, 15, 28, 43};

    MEMZERO(iconf);
    iconf._partial_update = true;
    SCHEMA_SET_STR(iconf.if_name, spl->spl_if_name);
    SCHEMA_SET_STR(iconf.if_type, spl->spl_if_type);
    SCHEMA_SET_BOOL(iconf.enabled, true);
    SCHEMA_SET_BOOL(iconf.network, true);
    SCHEMA_SET_BOOL(iconf.NAT, true);
    SCHEMA_SET_STR(iconf.ip_assign_scheme, "dhcp");
    for (size_t di = 0; di < ARRAY_LEN(dhcp_req); di++)
        iconf.dhcp_req[iconf.dhcp_req_len++] = dhcp_req[di];
    iconf.dhcp_req_present = true;
    // Routes to be installed into dedicated routing table for STA uplink
    SCHEMA_SET_INT(iconf.dhcp_route_table, WANO_STA_DHCP_ROUTING_TABLE);

    /* Controller-provisioned interface role (WAN_Config other_config:inet_role), verbatim */
    if (spl->spl_inet_role_str[0] != '\0')
    {
        SCHEMA_SET_STR(iconf.role, spl->spl_inet_role_str);
    }

    if (!ovsdb_table_upsert_simple(
                &table_Wifi_Inet_Config,
                SCHEMA_COLUMN(Wifi_Inet_Config, if_name),
                (char *)spl->spl_if_name,
                &iconf,
                false))
    {
        LOG(ERR, "wano_sta_ppline: %s: Failed to upsert Wifi_Inet_Config", spl->spl_if_name);
        return false;
    }

    LOG(INFO,
        "wano_sta_ppline: %s: Set WAN inet intent (enabled, network, NAT, dhcp, route table %d) + dhcp_req",
        spl->spl_if_name,
        WANO_STA_DHCP_ROUTING_TABLE);
    return true;
}

/* Revert the Inet_Config of the interface to plain backhaul mode. */
static void wano_sta_ppline_inet_dhcp_deconfigure(struct wano_sta_ppline *spl)
{
    struct schema_Wifi_Inet_Config iconf;

    MEMZERO(iconf);
    iconf._partial_update = true;
    SCHEMA_SET_STR(iconf.if_name, spl->spl_if_name);
    SCHEMA_SET_BOOL(iconf.network, false);
    SCHEMA_SET_BOOL(iconf.NAT, false);
    SCHEMA_SET_STR(iconf.ip_assign_scheme, "none");
    // TODO: hardcoded dhcp_req, assuming OpenSync default config,
    // we should probably be saving and restoring the original, whatever it was
    iconf.dhcp_req[0] = 1;  // subnet mask
    iconf.dhcp_req_len = 1;
    iconf.dhcp_req_present = true;
    SCHEMA_UNSET_FIELD(iconf.dhcp_route_table);
    SCHEMA_UNSET_FIELD(iconf.role);

    ovsdb_table_upsert_simple(
            &table_Wifi_Inet_Config,
            SCHEMA_COLUMN(Wifi_Inet_Config, if_name),
            (char *)spl->spl_if_name,
            &iconf,
            false);

    LOG(INFO, "wano_sta_ppline: %s: Reverted inet dhcp config to backhaul mode", spl->spl_if_name);
}

/* =========================================================================
 * Policy routing rules for switching general traffic over to the STA uplink.
 *
 * Essentially, we need something as follows (for both addr families):
 *
 * 0:      from all lookup local
 * 99:     from all lookup main suppress_prefixlength 0
 * 100:    from all lookup 100
 * 32766:  from all lookup main
 * 32767:  from all lookup default
 * ======================================================================= */
static const struct
{
    const char *name;
    const char *addr_family;
    int priority;
    int lookup_table;
    int suppress_preflen;
} wano_sta_prr_switchover_rules[] = {
    {"policy.sta_uplink.ipv4_0", "ipv4", WANO_STA_DHCP_ROUTING_TABLE_PRIORITY - 1, 0, 0},
    {"policy.sta_uplink.ipv6_0", "ipv6", WANO_STA_DHCP_ROUTING_TABLE_PRIORITY - 1, 0, 0},
    {"policy.sta_uplink.ipv4_1", "ipv4", WANO_STA_DHCP_ROUTING_TABLE_PRIORITY, WANO_STA_DHCP_ROUTING_TABLE, -1},
    {"policy.sta_uplink.ipv6_1", "ipv6", WANO_STA_DHCP_ROUTING_TABLE_PRIORITY, WANO_STA_DHCP_ROUTING_TABLE, -1},
};

/*
 * Make sure that we have configured policy routing rules to
 * lookup the dedicated routing table for STA uplink DHCP routes.
 */
static bool wano_sta_ppline_policy_routing_switchover_configure(void)
{
    bool success = true;

    for (size_t i = 0; i < ARRAY_LEN(wano_sta_prr_switchover_rules); i++)
    {
        struct schema_Policy_Routing_Rule rule;
        json_t *rows;

        /* Skip rules that are already configured */
        rows = ovsdb_sync_select(
                SCHEMA_TABLE(Policy_Routing_Rule),
                SCHEMA_COLUMN(Policy_Routing_Rule, name),
                wano_sta_prr_switchover_rules[i].name);
        if (rows != NULL && json_array_size(rows) > 0)
        {
            json_decref(rows);
            continue;
        }
        json_decref(rows);

        MEMZERO(rule);
        SCHEMA_SET_STR(rule.name, wano_sta_prr_switchover_rules[i].name);
        SCHEMA_SET_STR(rule.addr_family, wano_sta_prr_switchover_rules[i].addr_family);
        SCHEMA_SET_INT(rule.priority, wano_sta_prr_switchover_rules[i].priority);
        if (wano_sta_prr_switchover_rules[i].lookup_table > 0)
        {
            SCHEMA_SET_INT(rule.action_lookup_table, wano_sta_prr_switchover_rules[i].lookup_table);
        }
        if (wano_sta_prr_switchover_rules[i].suppress_preflen >= 0)
        {
            SCHEMA_SET_INT(rule.action_suppress_preflen, wano_sta_prr_switchover_rules[i].suppress_preflen);
        }

        if (!ovsdb_table_insert(&table_Policy_Routing_Rule, &rule))
        {
            LOG(ERR, "wano_sta_ppline: Failed to insert Policy_Routing_Rule %s", wano_sta_prr_switchover_rules[i].name);
            success = false;
        }
    }

    if (!success) return false;

    LOG(NOTICE,
        "wano_sta_ppline: Policy routing rules for STA uplink switchover installed"
        " (priority %d, lookup table %d, ipv4 + ipv6)",
        WANO_STA_DHCP_ROUTING_TABLE_PRIORITY,
        WANO_STA_DHCP_ROUTING_TABLE);
    return true;
}

/* Remove STA uplink switchover policy routing rules. */
static void wano_sta_ppline_policy_routing_switchover_deconfigure(void)
{
    for (size_t i = 0; i < ARRAY_LEN(wano_sta_prr_switchover_rules); i++)
    {
        ovsdb_sync_delete_where(
                SCHEMA_TABLE(Policy_Routing_Rule),
                ovsdb_where_simple(SCHEMA_COLUMN(Policy_Routing_Rule, name), wano_sta_prr_switchover_rules[i].name));
    }

    LOG(NOTICE, "wano_sta_ppline: Policy routing rules for STA uplink switchover removed");
}

/* Address families the STA uplink Connectivity_Check policy routing rules are installed for */
static const char *wano_sta_prr_cc_addr_families[] = {"ipv4", "ipv6"};

/* Name of the STA uplink Connectivity_Check policy routing rule owned by this pipeline */
static void wano_sta_ppline_prr_cc_name(
        const struct wano_sta_ppline *spl,
        const char *addr_family,
        char *buf,
        size_t buf_len)
{
    snprintf(buf, buf_len, WANO_STA_PPLINE_PRR_CC_NAME_PREFIX "%s.%s", spl->spl_if_name, addr_family);
}

/*
 * Install policy routing rules for Connectivity_Check probes over the STA uplink.
 *
 * These are needed so that a Connectivity_Check probe via the STA uplink
 * works regardless if we have switched general traffic over to the STA uplink
 * or not (i.e. in the case of test connections).
 *
 * Example:
 * ip rule add pref 98 oif <backup sta uplink> lookup 100
 */
static bool wano_sta_ppline_policy_routing_cc_configure(struct wano_sta_ppline *spl)
{
    bool success = true;

    for (size_t i = 0; i < ARRAY_LEN(wano_sta_prr_cc_addr_families); i++)
    {
        struct schema_Policy_Routing_Rule rule;
        char rule_name[sizeof(WANO_STA_PPLINE_PRR_CC_NAME_PREFIX) + C_IFNAME_LEN + sizeof(".ipv6")];
        json_t *rows;

        wano_sta_ppline_prr_cc_name(spl, wano_sta_prr_cc_addr_families[i], rule_name, sizeof(rule_name));

        /* Skip rules that are already configured */
        rows = ovsdb_sync_select(
                SCHEMA_TABLE(Policy_Routing_Rule),
                SCHEMA_COLUMN(Policy_Routing_Rule, name),
                rule_name);
        if (rows != NULL && json_array_size(rows) > 0)
        {
            json_decref(rows);
            continue;
        }
        json_decref(rows);

        MEMZERO(rule);
        SCHEMA_SET_STR(rule.name, rule_name);
        SCHEMA_SET_STR(rule.addr_family, wano_sta_prr_cc_addr_families[i]);
        SCHEMA_SET_INT(rule.priority, WANO_STA_PPLINE_PRR_CC_PRIORITY);
        SCHEMA_SET_STR(rule.selector_output_intf, spl->spl_if_name);  // match oif <sta intf>
        SCHEMA_SET_INT(rule.action_lookup_table, WANO_STA_DHCP_ROUTING_TABLE);

        if (!ovsdb_table_insert(&table_Policy_Routing_Rule, &rule))
        {
            LOG(ERR, "wano_sta_ppline: %s: Failed to insert Policy_Routing_Rule %s", spl->spl_if_name, rule_name);
            success = false;
        }
    }

    if (!success) return false;

    LOG(NOTICE,
        "wano_sta_ppline: %s: Connectivity check policy routing rules installed"
        " (priority %d, oif %s, lookup table %d, ipv4 + ipv6)",
        spl->spl_if_name,
        WANO_STA_PPLINE_PRR_CC_PRIORITY,
        spl->spl_if_name,
        WANO_STA_DHCP_ROUTING_TABLE);
    return true;
}

/* Remove this pipeline's Connectivity_Check policy routing rules. */
static void wano_sta_ppline_policy_routing_cc_deconfigure(struct wano_sta_ppline *spl)
{
    for (size_t i = 0; i < ARRAY_LEN(wano_sta_prr_cc_addr_families); i++)
    {
        char rule_name[sizeof(WANO_STA_PPLINE_PRR_CC_NAME_PREFIX) + C_IFNAME_LEN + sizeof(".ipv6")];

        wano_sta_ppline_prr_cc_name(spl, wano_sta_prr_cc_addr_families[i], rule_name, sizeof(rule_name));

        ovsdb_sync_delete_where(
                SCHEMA_TABLE(Policy_Routing_Rule),
                ovsdb_where_simple(SCHEMA_COLUMN(Policy_Routing_Rule, name), rule_name));
    }

    LOG(NOTICE, "wano_sta_ppline: %s: Connectivity check policy routing rules removed", spl->spl_if_name);
}

/* =========================================================================
 * Connectivity check ("internet" probe test) on the STA uplink
 * ======================================================================= */

/* Name of the Connectivity_Check row owned by this pipeline */
static void wano_sta_ppline_cc_name(const struct wano_sta_ppline *spl, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, WANO_STA_PPLINE_CC_NAME_PREFIX "%s", spl->spl_if_name);
}

/* Start Connectivity_Check probing the internet over the STA uplink */
static bool wano_sta_ppline_connectivity_check_start(struct wano_sta_ppline *spl)
{
    struct schema_Connectivity_Check ccheck;
    char cc_name[C_IFNAME_LEN + sizeof(WANO_STA_PPLINE_CC_NAME_PREFIX)];

    /* Configure only once per pipeline lifetime */
    if (spl->spl_conn_check_started) return true;

    /*
     * Make sure probes leaving the STA uplink use its routing table.
     */
    if (!wano_sta_ppline_policy_routing_cc_configure(spl))
    {
        LOG(WARN,
            "wano_sta_ppline: %s: Error configuring connectivity check policy routing,"
            " the connectivity check may not work",
            spl->spl_if_name);
    }

    wano_sta_ppline_cc_name(spl, cc_name, sizeof(cc_name));

    MEMZERO(ccheck);
    SCHEMA_SET_STR(ccheck.name, cc_name);
    SCHEMA_SET_BOOL(ccheck.enable, true);
    SCHEMA_SET_STR(ccheck.if_name, spl->spl_if_name);
    SCHEMA_SET_STR(ccheck.type, "icmp");
    SCHEMA_SET_STR(ccheck.target, WANO_STA_PPLINE_CC_TARGET);
    SCHEMA_SET_INT(ccheck.interval, WANO_STA_PPLINE_CC_INTERVAL);
    SCHEMA_SET_INT(ccheck.timeout, WANO_STA_PPLINE_CC_TIMEOUT);
    /* Signal to the controller that the device/OpenSync owns this row */
    STRSCPY(ccheck.other_config_keys[0], "owner");
    STRSCPY(ccheck.other_config[0], "device");
    ccheck.other_config_len = 1;
    ccheck.other_config_present = true;

    if (!ovsdb_table_upsert_simple(
                &table_Connectivity_Check,
                SCHEMA_COLUMN(Connectivity_Check, name),
                cc_name,
                &ccheck,
                false))
    {
        LOG(ERR, "wano_sta_ppline: %s: Failed to upsert Connectivity_Check %s", spl->spl_if_name, cc_name);
        return false;
    }

    spl->spl_conn_check_started = true;

    LOG(NOTICE,
        "wano_sta_ppline: %s: Started connectivity check %s (icmp %s, interval %d, timeout %d)",
        spl->spl_if_name,
        cc_name,
        WANO_STA_PPLINE_CC_TARGET,
        WANO_STA_PPLINE_CC_INTERVAL,
        WANO_STA_PPLINE_CC_TIMEOUT);
    return true;
}

/* Remove the Connectivity_Check row inserted by this pipeline */
static bool wano_sta_ppline_connectivity_check_stop(struct wano_sta_ppline *spl)
{
    char cc_name[C_IFNAME_LEN + sizeof(WANO_STA_PPLINE_CC_NAME_PREFIX)];
    bool deleted;

    if (!spl->spl_conn_check_started) return true;

    wano_sta_ppline_cc_name(spl, cc_name, sizeof(cc_name));

    deleted = ovsdb_sync_delete_where(
            SCHEMA_TABLE(Connectivity_Check),
            ovsdb_where_simple(SCHEMA_COLUMN(Connectivity_Check, name), cc_name));
    if (!deleted)
    {
        LOG(WARN, "wano_sta_ppline: %s: Failed to delete Connectivity_Check %s", spl->spl_if_name, cc_name);
    }

    spl->spl_conn_check_started = false;
    spl->spl_cc_status_ok = false;

    wano_sta_ppline_policy_routing_cc_deconfigure(spl);

    LOG(NOTICE, "wano_sta_ppline: %s: Stopped connectivity check %s", spl->spl_if_name, cc_name);
    return deleted;
}

/* =========================================================================
 * Start/stop
 * ======================================================================= */

/* True if a non-empty inet_addr string is a valid, non-zero IPv4 address */
static bool wano_sta_ppline_inet_addr_valid(const char *inet_addr)
{
    return inet_addr != NULL && inet_addr[0] != '\0' && strcmp(inet_addr, "0.0.0.0") != 0;
}

/*
 * Seed the L2/L3 state from the current OVSDB contents. Needed because rows
 * for this interface may have been seen by the monitors before the pipeline
 * was started (or existed).
 */
static void wano_sta_ppline_state_seed(struct wano_sta_ppline *spl)
{
    struct schema_Wifi_Master_State mstate;
    struct schema_Wifi_Inet_State istate;

    MEMZERO(mstate);
    spl->spl_port_active = ovsdb_table_select_one(
                                   &table_Wifi_Master_State,
                                   SCHEMA_COLUMN(Wifi_Master_State, if_name),
                                   spl->spl_if_name,
                                   &mstate)
                           && mstate.port_state_exists && (strcmp(mstate.port_state, "active") == 0);

    MEMZERO(istate);
    spl->spl_ipaddr_valid = ovsdb_table_select_one(
                                    &table_Wifi_Inet_State,
                                    SCHEMA_COLUMN(Wifi_Inet_State, if_name),
                                    spl->spl_if_name,
                                    &istate)
                            && istate.inet_addr_exists && wano_sta_ppline_inet_addr_valid(istate.inet_addr);

    wano_sta_ppline_eval(spl);
}

bool wano_sta_ppline_start(wano_sta_ppline_t *ppline)
{
    if (ppline->spl_started) return true;

    if (!wano_sta_ppline_inet_dhcp_configure(ppline))
    {
        LOG(ERR, "wano_sta_ppline: %s: Error configuring DHCP, not starting", ppline->spl_if_name);
        return false;
    }

    /* Install policy routing rules to switch default traffic to the STA uplink
     * (unless this is a test connection). */
    if (!ppline->spl_test_connection)
    {
        if (!wano_sta_ppline_policy_routing_switchover_configure())
        {
            LOG(ERR, "wano_sta_ppline: %s: Error configuring policy routing, not starting", ppline->spl_if_name);

            wano_sta_ppline_inet_dhcp_deconfigure(ppline);
            return false;
        }
        ppline->spl_prr_configured = true;
    }

    ppline->spl_started = true;

    LOG(NOTICE,
        "wano_sta_ppline: %s: Started STA pipeline (if_type=%s, test_connection=%d)",
        ppline->spl_if_name,
        ppline->spl_if_type,
        ppline->spl_test_connection);

    wano_sta_ppline_state_seed(ppline);

    return true;
}

/* Stop the pipeline */
void wano_sta_ppline_stop(wano_sta_ppline_t *ppline)
{
    struct wano_sta_ppline *spl;
    bool any_prr_configured = false;

    if (!ppline->spl_started) return;

    ppline->spl_started = false;

    /* Tear down in reverse order: the connectivity check goes first */
    wano_sta_ppline_connectivity_check_stop(ppline);

    /* Reset link state; stop is caller-initiated so no events are emitted */
    ppline->spl_port_active = false;
    ppline->spl_ipaddr_valid = false;
    ppline->spl_cc_status_ok = false;
    ppline->spl_has_l2 = false;
    ppline->spl_has_l3 = false;
    ppline->spl_conn_check_ok = false;

    /* Remove the Connection_Manager_Uplink row, if it is there */
    if (wano_connmgr_uplink_delete(ppline->spl_if_name))
    {
        LOG(INFO, "wano_sta_ppline: %s: Removed Connection_Manager_Uplink row", ppline->spl_if_name);
    }
    ppline->spl_cmu_inserted = false;

    wano_sta_ppline_inet_dhcp_deconfigure(ppline);

    /*
     * Remove the policy routing rules when the last pipeline using them stops.
     * Test connections never install them, so they never remove them either.
     */
    if (ppline->spl_prr_configured)
    {
        ppline->spl_prr_configured = false;

        ds_tree_foreach (&g_sta_ppline_list, spl)
        {
            if (spl->spl_prr_configured)
            {
                /* Some other pipeline has and needs the policy routing rules still configured */
                any_prr_configured = true;
            }
        }
        if (!any_prr_configured)
        {
            wano_sta_ppline_policy_routing_switchover_deconfigure();
        }
    }

    LOG(NOTICE, "wano_sta_ppline: %s: Stopped STA pipeline", ppline->spl_if_name);
}

/* =========================================================================
 * L2/L3 state evaluation
 * ======================================================================= */

/* Keep the Connection_Manager_Uplink row in sync with the evaluated state */
static void wano_sta_ppline_cmu_sync(struct wano_sta_ppline *spl)
{
    /* Test connections do not insert CMU rows */
    if (spl->spl_test_connection) return;

    if (!spl->spl_cmu_inserted)
    {
        /* Insert the row only once the uplink is fully up */
        if (!(spl->spl_has_l2 && spl->spl_has_l3)) return;

        if (!WANO_CONNMGR_UPLINK_UPDATE(
                    spl->spl_if_name,
                    .if_type = spl->spl_if_type,
                    .has_L2 = WANO_TRI_TRUE,
                    .has_L3 = WANO_TRI_TRUE,
                    .loop = WANO_TRI_FALSE))
        {
            LOG(WARN, "wano_sta_ppline: %s: Error upserting Connection_Manager_Uplink", spl->spl_if_name);
            return;
        }
        spl->spl_cmu_inserted = true;

        LOG(NOTICE,
            "wano_sta_ppline: %s: Upserted Connection_Manager_Uplink"
            " (if_type=%s, has_L2=true, has_L3=true, loop=false)",
            spl->spl_if_name,
            spl->spl_if_type);
        return;
    }

    /* Row present: keep has_L2/has_L3 up to date */
    if (!WANO_CONNMGR_UPLINK_UPDATE(
                spl->spl_if_name,
                .has_L2 = spl->spl_has_l2 ? WANO_TRI_TRUE : WANO_TRI_FALSE,
                .has_L3 = spl->spl_has_l3 ? WANO_TRI_TRUE : WANO_TRI_FALSE))
    {
        LOG(WARN, "wano_sta_ppline: %s: Error updating Connection_Manager_Uplink", spl->spl_if_name);
    }
}

static void wano_sta_ppline_event_emit(struct wano_sta_ppline *spl, enum wano_sta_ppline_event event)
{
    struct wano_sta_ppline_status status = {
        .has_L2 = spl->spl_has_l2,
        .has_L3 = spl->spl_has_l3,
        .conn_check_ok = spl->spl_conn_check_ok,
    };

    if (spl->spl_event_cb == NULL) return;
    spl->spl_event_cb(spl, event, status);
}

/* Re-evaluate has_L2/has_L3/conn_check_ok, sync the CMU row and report transitions */
static void wano_sta_ppline_eval(struct wano_sta_ppline *spl)
{
    const bool has_l2 = spl->spl_port_active;
    const bool has_l3 = spl->spl_port_active && spl->spl_ipaddr_valid;

    /* cc_status_ok implies L2 and L3; still check all the flags to make
     * sure we don't decide on any stale rows, or if L2/L3 events would
     * arrive sooner than connectivity check would react */
    const bool conn_check_ok = spl->spl_cc_status_ok && has_l2 && has_l3;
    const bool l2_changed = (has_l2 != spl->spl_has_l2);
    const bool l3_changed = (has_l3 != spl->spl_has_l3);
    const bool cc_changed = (conn_check_ok != spl->spl_conn_check_ok);

    if (!l2_changed && !l3_changed && !cc_changed) return;

    spl->spl_has_l2 = has_l2;
    spl->spl_has_l3 = has_l3;
    spl->spl_conn_check_ok = conn_check_ok;

    LOG(INFO,
        "wano_sta_ppline: %s: Link state: has_L2=%d has_L3=%d conn_check_ok=%d",
        spl->spl_if_name,
        has_l2,
        has_l3,
        conn_check_ok);

    wano_sta_ppline_cmu_sync(spl);

    /* Up transitions report L2 before L3 before conn check; down transitions in reverse */
    if (l2_changed && has_l2) wano_sta_ppline_event_emit(spl, WANO_STA_PPLINE_L2_UP);
    if (l3_changed && has_l3) wano_sta_ppline_event_emit(spl, WANO_STA_PPLINE_L3_UP);
    if (cc_changed && conn_check_ok) wano_sta_ppline_event_emit(spl, WANO_STA_PPLINE_CONN_CHECK_OK);
    if (cc_changed && !conn_check_ok) wano_sta_ppline_event_emit(spl, WANO_STA_PPLINE_CONN_CHECK_NOK);
    if (l3_changed && !has_l3) wano_sta_ppline_event_emit(spl, WANO_STA_PPLINE_L3_DOWN);
    if (l2_changed && !has_l2) wano_sta_ppline_event_emit(spl, WANO_STA_PPLINE_L2_DOWN);

    /* Only start probing for internet connectivity once the uplink gets L3 */
    if (l3_changed && has_l3)
    {
        wano_sta_ppline_connectivity_check_start(spl);
    }
}

/* =========================================================================
 * OVSDB monitors
 * ======================================================================= */
static void wano_sta_ppline_wis_cb(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Inet_State *old,
        struct schema_Wifi_Inet_State *new)
{
    struct wano_sta_ppline *spl;
    const char *if_name = (mon->mon_type == OVSDB_UPDATE_DEL) ? old->if_name : new->if_name;

    spl = ds_tree_find(&g_sta_ppline_list, (void *)if_name);
    if (spl == NULL || !spl->spl_started) return;

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        spl->spl_ipaddr_valid = false;
    }
    else
    {
        spl->spl_ipaddr_valid = new->inet_addr_exists &&wano_sta_ppline_inet_addr_valid(new->inet_addr);
    }

    wano_sta_ppline_eval(spl);
}

static void wano_sta_ppline_wms_cb(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Master_State *old,
        struct schema_Wifi_Master_State *new)
{
    struct wano_sta_ppline *spl;
    const char *if_name = (mon->mon_type == OVSDB_UPDATE_DEL) ? old->if_name : new->if_name;

    spl = ds_tree_find(&g_sta_ppline_list, (void *)if_name);
    if (spl == NULL || !spl->spl_started) return;

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        spl->spl_port_active = false;
    }
    else
    {
        spl->spl_port_active = new->port_state_exists && (strcmp(new->port_state, "active") == 0);
    }

    wano_sta_ppline_eval(spl);
}

static void wano_sta_ppline_cc_cb(
        ovsdb_update_monitor_t *mon,
        struct schema_Connectivity_Check *old,
        struct schema_Connectivity_Check *new)
{
    struct wano_sta_ppline *spl;
    const struct schema_Connectivity_Check *row = (mon->mon_type == OVSDB_UPDATE_DEL) ? old : new;
    char cc_name[C_IFNAME_LEN + sizeof(WANO_STA_PPLINE_CC_NAME_PREFIX)];

    if (row == NULL || !row->if_name_exists) return;

    spl = ds_tree_find(&g_sta_ppline_list, (void *)row->if_name);
    if (spl == NULL || !spl->spl_started) return;

    /* Double-check this is the Connectivity_Check row we have configured */
    wano_sta_ppline_cc_name(spl, cc_name, sizeof(cc_name));
    if (strcmp(row->name, cc_name) != 0) return;

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        spl->spl_cc_status_ok = false;
    }
    else
    {
        if (!ovsdb_update_changed(mon, SCHEMA_COLUMN(Connectivity_Check, status))) return;

        spl->spl_cc_status_ok = row->status_exists && (strcmp(row->status, "ok") == 0);
    }

    wano_sta_ppline_eval(spl);
}
