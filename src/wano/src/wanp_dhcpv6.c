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

#include "module.h"
#include "osa_assert.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"

#include "wano.h"

#include "wanp_dhcpv6_stam.h"
#include "memutil.h"
#include "const.h"

#include <unistd.h>

/*
 * Structure representing a single DHCPv6 plug-in instance
 */
struct wanp_dhcpv6
{
    wano_plugin_handle_t        wd6_handle;
    wano_plugin_status_fn_t    *wd6_status_fn;
    wanp_dhcpv6_state_t         wd6_state;
    osn_ip6_addr_t              wd6_ip6addr;
    osn_ip6_addr_t              wd6_ip_unnumbered_addr;
    bool                        wd6_has_global_ip;
    bool                        wd6_is_ip_unnumbered; // Is in IP unnumbered mode?
    bool                        wd6_ovsdb_subscribed;
    ds_tree_node_t              wd6_tnode;
};

static void wanp_dhcpv6_module_start(void *data);
static void wanp_dhcpv6_module_stop(void *data);
static wano_plugin_ops_init_fn_t wanp_dhcpv6_init;
static wano_plugin_ops_run_fn_t wanp_dhcpv6_run;
static wano_plugin_ops_fini_fn_t wanp_dhcpv6_fini;
static void wanp_dhcpv6_ovsdb_reset(struct wanp_dhcpv6 *self);
static bool wanp_dhcpv6_ovsdb_enable(struct wanp_dhcpv6 *self);
static bool wanp_dhcpv6_ip_unnumbered_ovsdb_enable(struct wanp_dhcpv6 *self);

void callback_IP_Interface(
        ovsdb_update_monitor_t *self,
        struct schema_IP_Interface *old,
        struct schema_IP_Interface *new);

static ovsdb_table_t table_IP_Interface;
static ovsdb_table_t table_DHCPv6_Client;
static ovsdb_table_t table_IPv6_Address;

/* List of handles subscribed to OVSDB events */
static ds_tree_t wanp_dhcpv6_ovsdb_list = DS_TREE_INIT(ds_str_cmp, struct wanp_dhcpv6, wd6_tnode);

static struct wano_plugin wanp_dhcpv6 = WANO_PLUGIN_INIT(
        "dhcpv6",
        100,
        WANO_PLUGIN_MASK_IPV6,
        wanp_dhcpv6_init,
        wanp_dhcpv6_run,
        wanp_dhcpv6_fini);


/*
 * ===========================================================================
 *  Plugin implementation
 * ===========================================================================
 */
wano_plugin_handle_t *wanp_dhcpv6_init(
        const struct wano_plugin *wp,
        const char *ifname,
        wano_plugin_status_fn_t *status_fn)
{
    struct wanp_dhcpv6 *self;

    self = CALLOC(1, sizeof(struct wanp_dhcpv6));

    self->wd6_handle.wh_plugin = wp;
    STRSCPY(self->wd6_handle.wh_ifname, ifname);
    self->wd6_status_fn = status_fn;

    return &self->wd6_handle;
}

void wanp_dhcpv6_run(wano_plugin_handle_t *wh)
{
    struct wanp_dhcpv6 *self = CONTAINER_OF(wh, struct wanp_dhcpv6, wd6_handle);
    wano_wan_t *wan = wano_wan_from_plugin_handle(wh);

    if (!wano_wan_is_last_config(wan))
    {
        LOG(INFO, "wanp_dhcpv6: %s: WAN config not exhausted yet, skipping.", wh->wh_ifname);
        self->wd6_status_fn(wh, &WANO_PLUGIN_STATUS(WANP_SKIP));
        return;
    }


    wanp_dhcpv6_state_do(&self->wd6_state, wanp_dhcpv6_do_INIT, NULL);
}

void wanp_dhcpv6_fini(wano_plugin_handle_t *wh)
{
    struct wanp_dhcpv6 *self = CONTAINER_OF(wh, struct wanp_dhcpv6, wd6_handle);

    if (self->wd6_ovsdb_subscribed)
    {
        ds_tree_remove(&wanp_dhcpv6_ovsdb_list, self);
        self->wd6_ovsdb_subscribed = false;
    }

    wanp_dhcpv6_ovsdb_reset(self);

    FREE(self);
}

/*
 * ===========================================================================
 *  OVSDB Interface
 * ===========================================================================
 */

void callback_IP_Interface(
        ovsdb_update_monitor_t *mon,
        struct schema_IP_Interface *old,
        struct schema_IP_Interface *new)
{
    struct schema_IPv6_Address ipv6_address;
    struct wanp_dhcpv6 *self;
    osn_ip6_addr_t global_addr;
    bool has_global_ip;
    int ii;

    const char *ifname = (mon->mon_type == OVSDB_UPDATE_DEL) ? old->if_name : new->if_name;

    self = ds_tree_find(&wanp_dhcpv6_ovsdb_list, (void *)ifname);
    if (self == NULL
        && !(strcmp(CONFIG_OSN_ODHCP6_MODE_PREFIX_NO_ADDRESS, "IP_UNNUMBERED") == 0
                && strcmp(ifname, CONFIG_TARGET_LAN_BRIDGE_NAME) == 0))
    {
        return;
    }

    has_global_ip = false;
    if (mon->mon_type != OVSDB_UPDATE_DEL)
    {
        /*
         * Scan the IP_Interface:ipv6_addr uuid set and check if there are any
         * global IPv6 Addresses. If there are, assume RA/DHCPv6 succeeded in
         * provisioning the WAN IPv6 link.
         */
        for (ii = 0; ii < new->ipv6_addr_len; ii++)
        {
            osn_ip6_addr_t addr;

            if (!ovsdb_table_select_one_where(
                        &table_IPv6_Address,
                        ovsdb_where_uuid("_uuid", new->ipv6_addr[ii].uuid),
                        &ipv6_address))
            {
                LOG(WARN, "wanp_dhcpv6: %s: Unable to find IPv6_Address row with uuid: %s",
                       ifname, new->ipv6_addr[ii].uuid);
                continue;
            }

            if (strcmp(ipv6_address.origin, "auto_configured") != 0)
            {
                continue;
            }

            if (!osn_ip6_addr_from_str(&addr, ipv6_address.address))
            {
                LOG(WARN, "wanp_dhcpv6: %s: Invalid IPv6 address: %s",
                        ifname,
                        ipv6_address.address);
                continue;
            }

            if (osn_ip6_addr_type(&addr) != OSN_IP6_ADDR_GLOBAL)
            {
                LOG(DEBUG, "wanp_dhcpv6: %s: Not a global IPv6 address: %s",
                        ifname,
                        ipv6_address.address);
                continue;
            }

            has_global_ip = true;
            global_addr = addr;
            break;
        }
    }

    if (self != NULL) // Uplink interface
    {
        self->wd6_has_global_ip = has_global_ip;  // Does it have global IPv6 addr?
        if (has_global_ip)
        {
            self->wd6_ip6addr = global_addr;
        }

        if (self->wd6_ovsdb_subscribed)
        {
            wanp_dhcpv6_state_do(&self->wd6_state, wanp_dhcpv6_do_OVSDB_UPDATE, NULL);
        }
    }
    else // LAN interface (and IP_UNNUMBERED enabled)
    {
        /*
         * IP unnumbered is a technique where a network interface can use an IP address
         * without configuring a unique IP address on that interface. Instead, the
         * interface borrows the IP address from another interface.
         *
         * If IP_UNNUMBERED enabled and we didn't get any IPv6 address for the WAN, but
         * we got an IA_PD, then the odhcp6c client script will assign the first usable
         * address from that prefix to the LAN interface. The uplink interface will then
         * operate in IP unnumbered mode meaning it will "borrow" the global IPv6 address
         * from another interface (in this case the LAN interface). This allows connectivity
         * in such cases.
         *
         * Go through all the DHCPv6 uplink plugins:
         *   - If we have a global IPv6 address on the LAN interface
         *   - AND this uplink plugin has DHCPv6 options received,
         *       THEN mark this plugin as ip_unnumbered successful.
         */
        ds_tree_foreach(&wanp_dhcpv6_ovsdb_list, self)
        {
            self->wd6_is_ip_unnumbered = false;

            if (has_global_ip)
            {
                char uplink_opts_file[C_MAXPATH_LEN];

                snprintf(
                    uplink_opts_file,
                    sizeof(uplink_opts_file),
                    CONFIG_OSN_ODHCP6_OPTS_FILE,
                    self->wd6_handle.wh_ifname);

                if (access(uplink_opts_file, F_OK) == 0) // DHCPv6 opt file for this uplink exists
                {
                    self->wd6_is_ip_unnumbered = true;
                    self->wd6_ip_unnumbered_addr = global_addr;
                }
            }

            if (self->wd6_ovsdb_subscribed)
            {
                wanp_dhcpv6_state_do(&self->wd6_state, wanp_dhcpv6_do_OVSDB_UPDATE, NULL);
            }
        }
    }
}

bool wanp_dhcpv6_ovsdb_enable(struct wanp_dhcpv6 *self)
{
    struct schema_IP_Interface ip_interface;
    struct schema_DHCPv6_Client dhcpv6_client;
    int selcount;

    selcount = ovsdb_table_select_one(&table_IP_Interface, "name", self->wd6_handle.wh_ifname, &ip_interface);
    memset(&ip_interface, 0, sizeof(ip_interface));
    ip_interface._partial_update = true;
    SCHEMA_SET_STR(ip_interface.if_name, self->wd6_handle.wh_ifname);
    SCHEMA_SET_STR(ip_interface.status, "up");
    SCHEMA_SET_INT(ip_interface.enable, true);
    if (selcount <= 0)
    {
        /*
         * The `name` field is immutable which will cause upserts to will fail
         * on existing rows so include it only if a row for the interface
         * doesn't exist
         */
        SCHEMA_SET_STR(ip_interface.name, self->wd6_handle.wh_ifname);
    }

    if (!ovsdb_table_upsert_simple(
            &table_IP_Interface,
            "name",
            self->wd6_handle.wh_ifname,
            &ip_interface,
            true))
    {
        LOG(ERR, "wanp_dhcpv6: %s: Error upserting IP_Interface.",
                self->wd6_handle.wh_ifname);
        return false;
    }

    memset(&dhcpv6_client, 0, sizeof(dhcpv6_client));
    dhcpv6_client._partial_update = true;
    SCHEMA_SET_INT(dhcpv6_client.enable, true);
    SCHEMA_SET_INT(dhcpv6_client.request_address, true);
    SCHEMA_SET_INT(dhcpv6_client.request_prefixes, true);
    SCHEMA_SET_UUID(dhcpv6_client.ip_interface, ip_interface._uuid.uuid);

    if (!ovsdb_table_upsert_where(
            &table_DHCPv6_Client,
            ovsdb_where_uuid("ip_interface", ip_interface._uuid.uuid),
            &dhcpv6_client,
            false))
    {
        LOG(ERR, "wanp_dhcpv6: %s: Error upserting DHCPv6_Client.",
                self->wd6_handle.wh_ifname);
        return false;
    }

    return true;
}

/* Enable IP unnumbered option: In this case, the WAN DHPCv6 plugin could succeed even if no
 * global IPv6 address (received and assigned) on the interface, but a route-able prefix was
 * received and a global IPv6 address from the prefix was assigned to the LAN interface.
 *
 * To enable that we need an IP_Interface row for the LAN interface to get LAN interface IPv6
 * address updates.
 */
bool wanp_dhcpv6_ip_unnumbered_ovsdb_enable(struct wanp_dhcpv6 *self)
{
    struct schema_IP_Interface ip_interface;
    int selcount;

    selcount = ovsdb_table_select_one(&table_IP_Interface, "name", CONFIG_TARGET_LAN_BRIDGE_NAME, &ip_interface);
    memset(&ip_interface, 0, sizeof(ip_interface));
    ip_interface._partial_update = true;
    SCHEMA_SET_STR(ip_interface.if_name, CONFIG_TARGET_LAN_BRIDGE_NAME);
    SCHEMA_SET_STR(ip_interface.status, "up");
    SCHEMA_SET_INT(ip_interface.enable, true);
    if (selcount <= 0)
    {
        /* The `name` field is immutable which will cause upserts to will fail on existing
         * rows so include it only if a row for the interface doesn't exist. */
        SCHEMA_SET_STR(ip_interface.name, CONFIG_TARGET_LAN_BRIDGE_NAME);
    }

    if (!ovsdb_table_upsert_simple(
            &table_IP_Interface,
            "name",
            CONFIG_TARGET_LAN_BRIDGE_NAME,
            &ip_interface,
            true))
    {
        LOG(ERR, "wanp_dhcpv6: %s: Error upserting IP_Interface.", CONFIG_TARGET_LAN_BRIDGE_NAME);
        return false;
    }
    return true;
}

void wanp_dhcpv6_ovsdb_reset(struct wanp_dhcpv6 *self)
{
    struct schema_IP_Interface ip_interface;

    if (!ovsdb_table_select_one(&table_IP_Interface, "if_name", self->wd6_handle.wh_ifname, &ip_interface))
    {
        /* No entry in IP_Interface -- we're good */
        LOG(DEBUG, "wanp_dhcpv6: %s: IP_Interface no entry for if_name.", self->wd6_handle.wh_ifname);
        return;
    }

    if (ovsdb_table_delete_where(&table_DHCPv6_Client, ovsdb_where_uuid("ip_interface", ip_interface._uuid.uuid)) < 0)
    {
        LOG(WARN, "wanp_dhcpv6: %s: Error deleting DHCPv6_Client row.",
                self->wd6_handle.wh_ifname);
    }
}

/*
 * ===========================================================================
 *  State Machine
 * ===========================================================================
 */
enum wanp_dhcpv6_state wanp_dhcpv6_state_INIT(
        wanp_dhcpv6_state_t *state,
        enum wanp_dhcpv6_action action,
        void *data)
{
    (void)state;
    (void)data;

    switch (action)
    {
        case wanp_dhcpv6_do_INIT:
            return wanp_dhcpv6_ENABLE;

        default:
            break;
    }

    return 0;
}

enum wanp_dhcpv6_state wanp_dhcpv6_state_ENABLE(
        wanp_dhcpv6_state_t *state,
        enum wanp_dhcpv6_action action,
        void *data)
{
    (void)state;
    (void)action;
    (void)data;

    struct wanp_dhcpv6 *self = CONTAINER_OF(state, struct wanp_dhcpv6, wd6_state);

    switch (action)
    {
        case wanp_dhcpv6_do_STATE_INIT:
            LOG(INFO, "wanp_dhcpv6: %s: Enabling DHCPv6/RA on interface.",
                    self->wd6_handle.wh_ifname);

            wanp_dhcpv6_ovsdb_reset(self);
            wanp_dhcpv6_ovsdb_enable(self);
            if (strcmp(CONFIG_OSN_ODHCP6_MODE_PREFIX_NO_ADDRESS, "IP_UNNUMBERED") == 0)
            {
                wanp_dhcpv6_ip_unnumbered_ovsdb_enable(self);
            }

            /* Subscribe to OVSDB events */
            self->wd6_ovsdb_subscribed = true;
            ds_tree_insert(&wanp_dhcpv6_ovsdb_list, self, self->wd6_handle.wh_ifname);
            /* FALLTHROUGH */

        case wanp_dhcpv6_do_OVSDB_UPDATE:
            if (!self->wd6_has_global_ip && !self->wd6_is_ip_unnumbered)
            {
                return 0;
            }
            return wanp_dhcpv6_IDLE;

        default:
            break;
    }

    return 0;
}

enum wanp_dhcpv6_state wanp_dhcpv6_state_IDLE(
        wanp_dhcpv6_state_t *state,
        enum wanp_dhcpv6_action action,
        void *data)
{
    (void)data;

    struct wanp_dhcpv6 *self = CONTAINER_OF(state, struct wanp_dhcpv6, wd6_state);

    switch (action)
    {
        case wanp_dhcpv6_do_STATE_INIT:
            if (self->wd6_has_global_ip)
            {
                LOG(INFO, "wano_dhcpv6: %s: Acquired IPv6 address: "PRI_osn_ip6_addr,
                            self->wd6_handle.wh_ifname,
                            FMT_osn_ip6_addr(self->wd6_ip6addr));
            }
            else if (self->wd6_is_ip_unnumbered)
            {
                LOG(NOTICE, "wano_dhcpv6: %s: IP unnumbered: Borrowing IPv6 address "PRI_osn_ip6_addr
                            " from the LAN intf",
                            self->wd6_handle.wh_ifname,
                            FMT_osn_ip6_addr(self->wd6_ip_unnumbered_addr));
            }

            struct wano_plugin_status ws = WANO_PLUGIN_STATUS(WANP_OK);
            self->wd6_status_fn(&self->wd6_handle, &ws);
            break;

        case wanp_dhcpv6_do_OVSDB_UPDATE:
            if (!self->wd6_has_global_ip)
            {
                LOG(INFO, "wano_dhcpv6: %s: Lost IPv6 address.",
                        self->wd6_handle.wh_ifname);
            }
            else if (self->wd6_has_global_ip)
            {
                LOG(INFO, "wano_dhcpv6: %s: Re-acquired IPv6 address: "PRI_osn_ip6_addr,
                        self->wd6_handle.wh_ifname,
                        FMT_osn_ip6_addr(self->wd6_ip6addr));
            }

            if (!self->wd6_is_ip_unnumbered)
            {
                LOG(INFO, "wano_dhcpv6: %s: Lost IP unnumbered mode",
                        self->wd6_handle.wh_ifname);
            }
            else if (self->wd6_is_ip_unnumbered)
            {
                LOG(INFO, "wano_dhcpv6: %s: Re-acquired IP unnumbered mode: Address: "PRI_osn_ip6_addr,
                        self->wd6_handle.wh_ifname,
                        FMT_osn_ip6_addr(self->wd6_ip_unnumbered_addr));
            }

            break;

        default:
            break;
    }

    return 0;
}

/*
 * ===========================================================================
 *  Module Support
 * ===========================================================================
 */
void wanp_dhcpv6_module_start(void *data)
{

    /* Subscribe to IP_Interface changes */
    OVSDB_TABLE_INIT(IP_Interface, name);
    OVSDB_TABLE_INIT(DHCPv6_Client, _uuid);
    OVSDB_TABLE_INIT(IPv6_Address, _uuid);

    if (!OVSDB_TABLE_MONITOR(IP_Interface, true))
    {
        LOG(ERR, "dhcpv6: Error monitoring IP_Interface. Plug-in won't be available.");
        return;
    }

    wano_plugin_register(&wanp_dhcpv6);
}

void wanp_dhcpv6_module_stop(void *data)
{
    wano_plugin_unregister(&wanp_dhcpv6);
}

MODULE(wanp_dhcpv6, wanp_dhcpv6_module_start, wanp_dhcpv6_module_stop)
