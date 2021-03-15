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

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>

#include "os.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"
#include "target.h"
#include "target_common.h"
#include "cm2.h"
#include "kconfig.h"

#include <arpa/inet.h>


#define MODULE_ID LOG_MODULE_ID_OVSDB

#define OVSDB_FILTER_LEN                25

/* BLE definitions */
#define CM2_BLE_INTERVAL_VALUE_DEFAULT  0
#define CM2_BLE_TXPOWER_VALUE_DEFAULT   0
#define CM2_BLE_MODE_OFF                "off"
#define CM2_BLE_MODE_ON                 "on"
#define CM2_BLE_MSG_ONBOARDING          "on_boarding"
#define CM2_BLE_MSG_DIAGNOSTIC          "diagnostic"
#define CM2_BLE_MSG_LOCATE              "locate"

#define CM2_BASE64_ARGV_MAX             64

#define CM2_VIF_MULTI_AP_STA_PARAM      "backhaul_sta"

#define CM2_PM_MODULE_NAME              "PM"
#define CM2_PM_GW_OFFLINE_CFG           "gw_offline_cfg"
#define CM2_PM_GW_OFFLINE_CFG_EN        "true"
#define CM2_PM_GW_OFFLINE               "gw_offline"
#define CM2_PM_GW_OFFLINE_ON            "true"
#define CM2_PM_GW_OFFLINE_OFF           "false"
#define CM2_PM_GW_OFFLINE_STATUS        "gw_offline_status"
#define CM2_PM_GW_OFFLINE_STATUS_READY  "ready"
#define CM2_PM_GW_OFFLINE_STATUS_ACTIVE "active"

static
bool cm2_ovsdb_connection_remove_uplink(char *if_name);

ovsdb_table_t table_Open_vSwitch;
ovsdb_table_t table_Manager;
ovsdb_table_t table_SSL;
ovsdb_table_t table_AWLAN_Node;
ovsdb_table_t table_Wifi_Master_State;
ovsdb_table_t table_Connection_Manager_Uplink;
ovsdb_table_t table_AW_Bluetooth_Config;
ovsdb_table_t table_Wifi_Inet_Config;
ovsdb_table_t table_Wifi_Inet_State;
ovsdb_table_t table_Wifi_VIF_Config;
ovsdb_table_t table_Wifi_VIF_State;
ovsdb_table_t table_Port;
ovsdb_table_t table_Bridge;
ovsdb_table_t table_IP_Interface;
ovsdb_table_t table_IPv6_Address;
ovsdb_table_t table_DHCPv6_Client;
ovsdb_table_t table_Wifi_Route_State;
ovsdb_table_t table_Node_Config;
ovsdb_table_t table_Node_State;

void callback_AWLAN_Node(ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old_rec,
        struct schema_AWLAN_Node *awlan)
{
    bool valid;

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        g_state.have_awlan = false;
    }
    else
    {
        g_state.have_awlan = true;
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, manager_addr)))
    {
        // manager_addr changed
        valid = cm2_set_addr(CM2_DEST_MANAGER, awlan->manager_addr);
        g_state.addr_manager.updated = valid;
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, redirector_addr)))
    {
        // redirector_addr changed
        valid = cm2_set_addr(CM2_DEST_REDIR, awlan->redirector_addr);
        g_state.addr_redirector.updated = valid;
    }

    if (    ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, device_mode))
         || ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, factory_reset))
       )
    {
        target_device_config_set(awlan);
    }

    if (    ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, min_backoff))
         || ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, max_backoff))
       )
    {
        g_state.min_backoff = awlan->min_backoff;
        g_state.max_backoff = awlan->max_backoff;
    }

    cm2_update_state(CM2_REASON_AWLAN);
}

void callback_Manager(ovsdb_update_monitor_t *mon,
        struct schema_Manager *old_rec,
        struct schema_Manager *manager)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        g_state.have_manager = false;
        g_state.connected = false;
    }
    else
    {
        g_state.have_manager = true;
        g_state.connected = manager->is_connected;
        if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Manager, is_connected)))
        {
            // is_connected changed
            LOG(DEBUG, "Manager.is_connected = %s", str_bool(manager->is_connected));
        }
    }

    cm2_update_state(CM2_REASON_MANAGER);
}

static inline const char* bool2string(const bool a)
{
    return a ? "enabled" : "disabled";
}

static bool
cm2_util_get_link_is_used(struct schema_Connection_Manager_Uplink *uplink)
{
    if (!strcmp(uplink->if_name, g_state.link.if_name) &&
        g_state.link.is_used)
        return true;

    return false;
}

static bool
cm2_util_is_ifname_on_stalist(const char *ifname)
{
#ifndef CONFIG_OVSDB_BOOTSTRAP_WIFI_STA_LIST
    return false;
#else
    char stalist[256];
    char *iface;
    char *pair;

    STRSCPY_WARN(stalist, CONFIG_OVSDB_BOOTSTRAP_WIFI_STA_LIST);
    pair = strtok (stalist," ");
    while (pair != NULL) {
        iface = strstr(pair, ":");
        if (iface && strcmp(iface + 1, ifname) == 0)
            return true;
        pair = strtok(NULL, " ");
    }
    
    return false;
#endif
}

static bool
cm2_util_vif_is_sta(const char *ifname)
{
    struct schema_Wifi_VIF_Config vconf;
    struct schema_Wifi_VIF_State vstate;

    MEMZERO(vconf);
    MEMZERO(vstate);

    /* This function can be called after underlying wifi
     * interface configuration and state were removed.
     *
     * In such case we fall back to checking raw interface
     * names hoping to get it right.
     *
     * When we assign these names explicitly
     * there's no need for any extra checks.
     *
     * On some platforms it's not possible to rename
     * interfaces so we're stuck with wl%d and wl%d.%d.
     * Moreover extenders are expected (due to apparent
     * wl driver limitation) to use wl%d primary interfaces
     * for station role.
     */

    if (!cm2_is_extender())
        return false;

    if (ovsdb_table_select_one(&table_Wifi_VIF_Config,
                SCHEMA_COLUMN(Wifi_VIF_Config, if_name), ifname, &vconf))
        return !strcmp(vconf.mode, "sta");

    if (ovsdb_table_select_one(&table_Wifi_VIF_State,
                SCHEMA_COLUMN(Wifi_VIF_State, if_name), ifname, &vstate))
        return !strcmp(vstate.mode, "sta");

    LOGI("%s: %s: unable to find in ovsdb, checking interfaces",
         __func__, ifname);

    if (cm2_util_is_ifname_on_stalist(ifname))
        return true;

    LOGI("%s: %s: either not a sta, or unable to infer", __func__, ifname);
    return false;
}

static int cm2_util_set_defined_priority(char *if_type) {
    int priority;

    if (!strcmp(if_type, VLAN_TYPE_NAME))
        priority = 3;
    else if (!strcmp(if_type, ETH_TYPE_NAME))
        priority = 2;
    else
        priority = 1;

    return priority;
}

static void cm2_util_ifname2gre(char *gre_ifname, int gre_size, char *ifname) {
    tsnprintf(gre_ifname, gre_size, "g-%s", ifname);
}

/*
 * Helper functions for WM, NM to translate Wifi_Master_State to Connection_Manager_Uplink table
 */

bool
cm2_ovsdb_set_Wifi_Inet_Config_interface_enabled(bool state, char *ifname)
{
    struct schema_Wifi_Inet_Config icfg;
    int                            ret;

    memset(&icfg, 0, sizeof(icfg));

    if (strlen(ifname) == 0)
        return 0;

    LOGI("%s change interface state: %d", ifname, state);

    icfg.enabled = state;
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Wifi_Inet_Config, enabled),
                       NULL };

    ret = ovsdb_table_update_where_f(&table_Wifi_Inet_Config,
                 ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), ifname),
                 &icfg, filter);

    return ret == 1;
}

static void
cm2_util_set_local_ip_cfg(struct schema_Wifi_Inet_State *istate, cm2_ip *ip)
{
    if (istate->ip_assign_scheme_exists) {
        if (!strcmp(istate->ip_assign_scheme, "static"))
            ip->assign_scheme = CM2_IP_STATIC;
        else if (!strcmp(istate->ip_assign_scheme, "none"))
            ip->assign_scheme = CM2_IP_NONE;
        else if (!strcmp(istate->ip_assign_scheme, "dhcp"))
            ip->assign_scheme = CM2_IPV4_DHCP;
    } else {
        ip->assign_scheme = CM2_IP_NOT_SET;
    }

    if (!istate->inet_addr_exists ||
        (istate->inet_addr_exists && (strlen(istate->inet_addr) <= 0 ||
         !strcmp(istate->inet_addr, "0.0.0.0")))) {
        ip->is_ip = false;
    } else {
        ip->is_ip = true;
    }
}

static int
cm2_util_get_ip_inet_state_cfg(char *if_name, cm2_ip *ip)
{
    struct schema_Wifi_Inet_State istate;
    int                           ret;

    MEMZERO(istate);

    ret = ovsdb_table_select_one(&table_Wifi_Inet_State,
                SCHEMA_COLUMN(Wifi_Inet_Config, if_name), if_name, &istate);
    if (!ret) {
        LOGI("%s: %s: Failed to get Wifi_Inet_State item", __func__, if_name);
        return -1;
    }

    LOGI("%s IP address info: inet_addr = %s assign_scheme = %s", if_name, istate.inet_addr, istate.ip_assign_scheme);
    cm2_util_set_local_ip_cfg(&istate, ip);

    return 0;
}

void
cm2_ovsdb_set_dhcp_client(const char *if_name, bool enabled)
{
    struct schema_Wifi_Inet_Config iconf;
    char                           *ip_assign;
    char                           *filter[] = { "+",
                                                 SCHEMA_COLUMN(Wifi_Inet_Config, ip_assign_scheme),
                                                 SCHEMA_COLUMN(Wifi_Inet_Config, enabled),
                                                 SCHEMA_COLUMN(Wifi_Inet_Config, network),
                                                 NULL };
    int                            ret;

    MEMZERO(iconf);

    LOGI("%s: Updating DHCP settings [%d]", if_name, enabled);
    ip_assign = enabled ? "dhcp" : "none";
    STRSCPY(iconf.ip_assign_scheme, ip_assign);
    iconf.ip_assign_scheme_exists = true;
    iconf.enabled = true;
    iconf.network = true;

    ret = ovsdb_table_update_where_f(&table_Wifi_Inet_Config,
                 ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), if_name),
                 &iconf, filter);
    if (!ret)
        LOGW("%s: Update dhcp client failed", if_name);
}

static
bool cm2_ovsdb_is_dhcpv6_running(const char *ifname)
{
    struct schema_DHCPv6_Client  dhcpv6_client;
    struct schema_IP_Interface   ip_interface;
    bool   ret;

    memset(&ip_interface, 0, sizeof(ip_interface));

    if (!ovsdb_table_select_one(&table_IP_Interface, "if_name", ifname, &ip_interface)) {
        LOGI("%s Interface not available", ifname);
        return false;
    }

    memset(&dhcpv6_client, 0, sizeof(dhcpv6_client));

    ret = ovsdb_table_select_one_where(&table_DHCPv6_Client,
                                       ovsdb_where_uuid("ip_interface", ip_interface._uuid.uuid),
                                       &dhcpv6_client);
    LOGI("DHCP running state = %d", ret);
    return ret;
}

static
bool cm2_ovsdb_dhcpv6_enable(char *ifname)
{
    struct schema_DHCPv6_Client  dhcpv6_client;
    struct schema_IP_Interface   ip_interface;

    if (cm2_ovsdb_is_dhcpv6_running(ifname)) {
        LOGI("DHCP IPv6 client is enabled");
        return true;
    }

    LOGI("%s: Enabling DHCP IPv6 client", ifname);

    memset(&ip_interface, 0, sizeof(ip_interface));

    if (!ovsdb_table_select_one(&table_IP_Interface, "if_name", ifname, &ip_interface)) {
        memset(&ip_interface, 0, sizeof(ip_interface));
        ip_interface._partial_update = true;

        SCHEMA_SET_STR(ip_interface.name, ifname);
        SCHEMA_SET_STR(ip_interface.if_name, ifname);
        SCHEMA_SET_INT(ip_interface.enable, true);
        SCHEMA_SET_STR(ip_interface.status, "up");

        if (!ovsdb_table_upsert_simple(
                &table_IP_Interface,
                "name",
                ifname,
                &ip_interface,
                true)) {
            LOGE("%s: Error upserting IP_Interface", ifname);
            return false;
        }

    }

    memset(&dhcpv6_client, 0, sizeof(dhcpv6_client));
    dhcpv6_client._partial_update = true;
    SCHEMA_SET_UUID(dhcpv6_client.ip_interface, ip_interface._uuid.uuid);
    SCHEMA_SET_INT(dhcpv6_client.enable, true);
    SCHEMA_SET_INT(dhcpv6_client.renew, true);
    SCHEMA_SET_INT(dhcpv6_client.request_address, true);
    SCHEMA_SET_INT(dhcpv6_client.request_prefixes, true);

    if (!ovsdb_table_upsert_where(
            &table_DHCPv6_Client,
            ovsdb_where_uuid("ip_interface", ip_interface._uuid.uuid),
            &dhcpv6_client,
            false)) {
        LOGE("%s: Error upserting DHCPv6_Client",ifname);
        return false;
    }

    return true;
}

static
void cm2_ovsdb_dhcpv6_disable(char *ifname)
{
    struct schema_IP_Interface ip_interface;

    LOGI("%s: Disabling DHCP IPv6 client", ifname);

    memset(&ip_interface, 0, sizeof(ip_interface));

    if (!ovsdb_table_select_one(&table_IP_Interface, "if_name", ifname, &ip_interface)) {
        LOGI("%s: IP_Interface no entry for interface name", ifname);
        return;
    }

    if (ovsdb_table_delete_where(&table_DHCPv6_Client, ovsdb_where_uuid("ip_interface", ip_interface._uuid.uuid)) < 0) {
        LOGI("%s: Error deleting DHCPv6_Client row.", ifname);
    }

    if (ovsdb_table_delete_where(&table_IP_Interface, ovsdb_where_uuid("_uuid", ip_interface._uuid.uuid)) < 0) {
        LOGI("%s: Error deleting IP_Interface row.", ifname);
    }
}

bool cm2_ovsdb_set_dhcpv6_client(char *ifname, bool enable)
{
    if (enable)
        return cm2_ovsdb_dhcpv6_enable(ifname);

    cm2_ovsdb_dhcpv6_disable(ifname);

    return true;
}

int cm2_update_main_link_ip(cm2_main_link_t *link)
{
    char *uplink;

    uplink = cm2_get_uplink_name();
    if (cm2_ovsdb_is_ipv6_global_link(uplink)) {
        link->ipv6.is_ip = true;
        link->ipv6.assign_scheme = CM2_IPV6_DHCP;
        return 0;
    }

    return cm2_util_get_ip_inet_state_cfg(uplink, &link->ipv4);
}

static int
cm2_ovsdb_copy_dhcp_ipv4_configuration(char *up_src, char *up_dst)
{
    struct  schema_Wifi_Inet_Config ups_iconf;
    struct  schema_Wifi_Inet_Config upd_iconf;
    char    *filter[8];
    int     idx;
    int     dns_idx;
    int     ret;

    MEMZERO(ups_iconf);
    ret = ovsdb_table_select_one(&table_Wifi_Inet_Config,
                SCHEMA_COLUMN(Wifi_Inet_Config, if_name), up_src, &ups_iconf);
    if (!ret) {
        LOGI("%s: %s: Failed to get interface config", __func__, up_src);
        return -1;
    }

    if (!ups_iconf.ip_assign_scheme_exists ||
        !strcmp(ups_iconf.ip_assign_scheme, "none")) {
        cm2_ovsdb_set_dhcp_client(up_dst, true);
        return 0;
    }

    MEMZERO(upd_iconf);
    ret = ovsdb_table_select_one(&table_Wifi_Inet_Config,
                SCHEMA_COLUMN(Wifi_Inet_Config, if_name), up_dst, &upd_iconf);
    if (!ret) {
        LOGI("%s: %s: Failed to get interface config", __func__, up_dst);
        return -1;
    }

    idx = 0;
    filter[idx++] = "+";

    if (ups_iconf.ip_assign_scheme_exists) {
        STRSCPY(upd_iconf.ip_assign_scheme, ups_iconf.ip_assign_scheme);
        upd_iconf.ip_assign_scheme_exists = true;
        filter[idx++] = SCHEMA_COLUMN(Wifi_Inet_Config, ip_assign_scheme);
    }

    if (ups_iconf.gateway_exists) {
        STRSCPY(upd_iconf.gateway, ups_iconf.gateway);
        upd_iconf.gateway_exists = true;
        filter[idx++] = SCHEMA_COLUMN(Wifi_Inet_Config, gateway);
    }

    if (ups_iconf.inet_addr_exists) {
        STRSCPY(upd_iconf.inet_addr, ups_iconf.inet_addr);
        upd_iconf.inet_addr_exists = true;
        filter[idx++] = SCHEMA_COLUMN(Wifi_Inet_Config, inet_addr);
    }

    if (ups_iconf.netmask_exists) {
        STRSCPY(upd_iconf.netmask, ups_iconf.netmask);
        upd_iconf.netmask_exists = true;
        filter[idx++] = SCHEMA_COLUMN(Wifi_Inet_Config, netmask);
    }

    for (dns_idx = 0; dns_idx < ups_iconf.dns_len; dns_idx++) {
        STRSCPY(upd_iconf.dns[dns_idx], ups_iconf.dns[dns_idx]);
        STRSCPY(upd_iconf.dns_keys[dns_idx], ups_iconf.dns_keys[dns_idx]);
    }
    upd_iconf.dns_len = ups_iconf.dns_len;
    filter[idx++] = SCHEMA_COLUMN(Wifi_Inet_Config, dns);

    upd_iconf.network_exists = true;
    upd_iconf.network = true;
    filter[idx++] = SCHEMA_COLUMN(Wifi_Inet_Config, network);

    upd_iconf.enabled_exists = true;
    upd_iconf.enabled = true;
    filter[idx++] = SCHEMA_COLUMN(Wifi_Inet_Config, enabled);

    LOGI("%s: Updating DHCP configuration: %s", up_dst, upd_iconf.ip_assign_scheme);

    cm2_ovsdb_set_dhcp_client(up_src, false);

    /* Enable new configuration for dest uplink */
    ret = ovsdb_table_update_where_f(&table_Wifi_Inet_Config,
                 ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), up_dst),
                 &upd_iconf, filter);
    if (!ret) {
        LOGW("%s: Update DHCP for destination uplink failed", up_dst);
        return -1;
    }

    return 0;
}

static bool
cm2_util_set_dhcp_ipv4_cfg(char *if_name, char *inet_addr, bool refresh)
{
    struct schema_Wifi_Inet_Config iconf;
    bool                           dhcp_active;
    bool                           dhcp_static;
    bool                           dhcp_enabled;
    bool                           empty_addr;
    int                            ret;

    MEMZERO(iconf);
    dhcp_enabled = false;

    ret = ovsdb_table_select_one(&table_Wifi_Inet_Config,
                SCHEMA_COLUMN(Wifi_Inet_Config, if_name), if_name, &iconf);
    if (!ret)
        LOGI("%s: %s: Failed to get interface config", __func__, if_name);

    LOGI("%s Set dhcp: inet_addr = %s assign_scheme = %s refresh = %d",
         if_name, inet_addr, iconf.ip_assign_scheme, refresh);

    dhcp_active = !strcmp(iconf.ip_assign_scheme, "dhcp");
    dhcp_static = !strcmp(iconf.ip_assign_scheme, "static");

    empty_addr = !((strlen(inet_addr) > 0) && strcmp(inet_addr, "0.0.0.0") != 0);
    if (dhcp_static) {
        if (!empty_addr) {
            char gre_ifname[IFNAME_SIZE];

            LOGI("%s: Use static static IP address: %s", if_name, inet_addr);
            cm2_dhcpc_stop_dryrun(if_name);
            cm2_util_ifname2gre(gre_ifname, sizeof(gre_ifname), if_name);
            cm2_ovsdb_connection_update_L3_state(gre_ifname, CM2_PAR_TRUE);
         }
         return false;
    }

    /* Expected IP assignment */
    if (!refresh && dhcp_active && !empty_addr)
        return true;

    /* Waiting for pending configuration */
    if ((refresh && !dhcp_active && !empty_addr) ||
        (refresh && dhcp_active && empty_addr) ||
        (!refresh && dhcp_active && empty_addr))
        return false;

    if (refresh && dhcp_active)
        dhcp_enabled = false;
    else
        dhcp_enabled = true;

    cm2_ovsdb_set_dhcp_client(if_name, dhcp_enabled);
    return false;
}

static bool cm2_util_set_dhcp_ipv4_cfg_from_master(struct schema_Wifi_Master_State *master, bool refresh)
{
    if (!strcmp(master->port_state, "inactive"))
        return false;

    return cm2_util_set_dhcp_ipv4_cfg(master->if_name, master->inet_addr, refresh);
}

static bool cm2_util_set_dhcp_ipv4_cfg_from_inet(struct schema_Wifi_Inet_State *inet, bool refresh)
{
    return cm2_util_set_dhcp_ipv4_cfg(inet->if_name, inet->inet_addr, refresh);
}

void
cm2_ovsdb_refresh_dhcp(char *if_name)
{
    struct schema_Wifi_Master_State  mstate;
    int                              ret;

    if (!cm2_is_extender())
        return;

    LOGI("%s: Trigger refresh dhcp", if_name);

    if (g_state.link.ipv6.assign_scheme == CM2_IPV6_DHCP) {
        cm2_ovsdb_set_dhcpv6_client(if_name, false);
        cm2_ovsdb_set_dhcpv6_client(if_name, true);
    }
    else {
        MEMZERO(mstate);

        ret = ovsdb_table_select_one(&table_Wifi_Master_State,
                    SCHEMA_COLUMN(Wifi_Master_State, if_name), if_name, &mstate);
        if (!ret) {
            LOGW("%s: %s: Failed to get master row", __func__, mstate.if_name);
            return;
        }
        cm2_util_set_dhcp_ipv4_cfg_from_master(&mstate, true);
    }
}

static char*
cm2_util_get_gateway_ip(char *ip_addr, char *netmask)
{
     struct in_addr remote_addr;
     struct in_addr netmask_addr;

     inet_aton(ip_addr, &remote_addr);
     inet_aton(netmask, &netmask_addr);

     remote_addr.s_addr &= netmask_addr.s_addr;
     remote_addr.s_addr |= htonl(0x00000001U);

     return inet_ntoa(remote_addr);
}

/* Function triggers creating GRE interfaces
 * In the second phase of implementation need to be moved to WM2 */
static int
cm2_ovsdb_insert_Wifi_Inet_Config(struct schema_Wifi_Master_State *master)
{
    struct schema_Wifi_Inet_Config icfg;
    int                            ret;

    memset(&icfg, 0, sizeof(icfg));

    STRSCPY(icfg.if_type, GRE_TYPE_NAME);

    icfg.gre_local_inet_addr_exists = true;
    STRSCPY(icfg.gre_local_inet_addr, master->inet_addr);

    icfg.enabled_exists = true;
    icfg.enabled = true;

    icfg.network_exists = true;
    icfg.network = true;

    icfg.mtu_exists = true;
    icfg.mtu = CONFIG_CM2_MTU_ON_GRE;

    icfg.gre_remote_inet_addr_exists = true;
    STRSCPY(icfg.gre_remote_inet_addr, cm2_util_get_gateway_ip(master->inet_addr, master->netmask));

    icfg.gre_ifname_exists = true;
    STRSCPY(icfg.gre_ifname, master->if_name);

    icfg.if_name_exists = true;
    cm2_util_ifname2gre(icfg.if_name, sizeof(icfg.if_name), master->if_name);

    icfg.ip_assign_scheme_exists = true;
    STRSCPY(icfg.ip_assign_scheme, "none");

    LOGI("%s: Creating new gre iface: %s local addr: %s, remote_addr: %s, netmask: %s",
         master->if_name, icfg.if_name, icfg.gre_local_inet_addr,
         icfg.gre_remote_inet_addr, master->netmask);

    ret = ovsdb_table_upsert_simple(&table_Wifi_Inet_Config,
                                    SCHEMA_COLUMN(Wifi_Inet_Config, if_name),
                                    icfg.if_name,
                                    &icfg,
                                    NULL);
    if (!ret)
        LOGD("%s update creating GRE failed %s", __func__, master->if_name);

    return ret;
}

/* Function removing GRE interface */
static void
cm2_ovsdb_remove_Wifi_Inet_Config(char *if_name, bool gre) {
    char iface[IFNAME_SIZE];
    int  ret;

    if (!gre)
        cm2_util_ifname2gre(iface, sizeof(iface), if_name);
    else
        STRSCPY(iface, if_name);

    LOGN("%s: Remove gre if_name = %s", if_name, iface);
    ret = ovsdb_table_delete_simple(&table_Wifi_Inet_Config,
                                   SCHEMA_COLUMN(Wifi_Inet_Config, if_name),
                                   iface);
    if (!ret)
        LOGI("%s Remove row failed %s", __func__, iface);
}

/* Function required as a workaround for CAES-599 */
void cm2_ovsdb_remove_unused_gre_interfaces(void) {
    struct schema_Connection_Manager_Uplink *uplink;
    void   *uplink_p;
    int    count;
    int    i;

    uplink_p = ovsdb_table_select(&table_Connection_Manager_Uplink,
                                  SCHEMA_COLUMN(Connection_Manager_Uplink, if_type),
                                  "gre",
                                  &count);

    LOGD("%s Available gre links count = %d", __func__, count);

    if (uplink_p) {
        for (i = 0; i < count; i++) {
            uplink = (struct schema_Connection_Manager_Uplink *) (uplink_p + table_Connection_Manager_Uplink.schema_size * i);
            LOGD("%s link = %s type = %s is_used = %d",
                 __func__, uplink->if_name, uplink->if_name, uplink->is_used);

            if (strstr(uplink->if_name, "g-") != uplink->if_name)
                continue;

            if (uplink->is_used)
                continue;

            cm2_ovsdb_remove_Wifi_Inet_Config(uplink->if_name, true);
        }
        free(uplink_p);
    }
}

int
cm2_ovsdb_update_mac_reporting(char *ifname, bool state)
{
    struct schema_Wifi_Inet_Config icfg;
    int                            ret;

    memset(&icfg, 0, sizeof(icfg));

    if (strlen(ifname) == 0)
        return -1;

    LOGI("%s: Update mac_reporting state: %d", ifname, state);

    icfg.mac_reporting = state;
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Wifi_Inet_Config, mac_reporting ),
                       NULL };

    ret = ovsdb_table_update_where_f(&table_Wifi_Inet_Config,
                 ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), ifname),
                 &icfg, filter);

    return ret;
}

bool
cm2_ovsdb_set_Wifi_Inet_Config_network_state(bool state, char *ifname)
{
    struct schema_Wifi_Inet_Config icfg;
    int                            ret;

    memset(&icfg, 0, sizeof(icfg));

    if (strlen(ifname) == 0)
        return 0;

    LOGI("%s: Set new network state: %d", ifname, state);

    icfg.network = state;
    char *filter[] = { "+",
                       SCHEMA_COLUMN(Wifi_Inet_Config, network),
                       NULL };

    ret = ovsdb_table_update_where_f(&table_Wifi_Inet_Config,
                 ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), ifname),
                 &icfg, filter);

    return ret == 1;
}

static void
cm2_ovsdb_util_translate_master_ip(struct schema_Wifi_Master_State *master)
{
   bool is_sta;
   int  ret;

   LOGI("%s: Detected ip change: %s uplink: %s", master->if_name, master->inet_addr, cm2_get_uplink_name());

   is_sta = !strcmp(master->if_type, VIF_TYPE_NAME) && cm2_util_vif_is_sta(master->if_name);
   if (strcmp(master->if_name, cm2_get_uplink_name()) && !is_sta)
       return;

   if (!(cm2_util_set_dhcp_ipv4_cfg_from_master(master, false)))
       return;

   if (is_sta) {
       if (strcmp(master->port_state, "inactive") == 0) {
           LOGI("%s: Skip creating GRE for inactive station",
                master->if_name);
           return;
       }

       if (g_state.link.is_used &&
           strstr(g_state.link.if_name, master->if_name) == NULL) {
           LOGI("%s: Link is used, skip creating new gre", master->if_name);
           return;
       }

       LOGN("%s Trigger creating gre", master->if_name);

       cm2_ovsdb_remove_Wifi_Inet_Config(master->if_name, false);
       ret = cm2_ovsdb_insert_Wifi_Inet_Config(master);
       if (!ret)
           LOGW("%s: %s Failed to insert GRE", __func__, master->if_name);
   }
}

static bool
cm2_util_is_gre_station(const char *if_name)
{
    if (strlen(if_name) < 3)
        return false;

    return !strncmp(if_name, "g-", 2);
}

static bool
cm2_util_is_wds_station(const char *if_name)
{
    struct schema_Wifi_VIF_State vstate;
    int                          ret;
    int                          len;

    MEMZERO(vstate);

    ret = ovsdb_table_select_one(&table_Wifi_VIF_State,
                                 SCHEMA_COLUMN(Wifi_VIF_State, if_name), if_name, &vstate);
    if (!ret) {
        LOGI("%s: VIF interface not ready", if_name);
        return false;
    }

    len = strlen(vstate.multi_ap);
    return (len > 0 && !strncmp(vstate.multi_ap, CM2_VIF_MULTI_AP_STA_PARAM, len)) ? true : false;
}

static void
cm2_ovsdb_util_handle_master_sta_port_state(struct schema_Wifi_Master_State *master,
                                            bool port_state,
                                            char *gre_ifname,
                                            bool wds)
{
    cm2_ip ipv4;
    int    ret;

    if (port_state) {
        ret = cm2_ovsdb_set_Wifi_Inet_Config_interface_enabled(true, master->if_name);
        if (!ret)
            LOGW("%s: %s: Failed to set interface enabled", __func__, master->if_name);

        if (!wds)
            cm2_util_set_dhcp_ipv4_cfg_from_master(master, true);
    } else {
        if (g_state.link.is_used && g_state.link.is_bridge) {
            bool br_update = true;

            if (!strcmp(g_state.link.if_name, master->if_name) && !strcmp(master->if_type, VIF_TYPE_NAME)) {
                /* WDS link lost */
                cm2_ovsdb_connection_remove_uplink(master->if_name);
            } else if (!strcmp(g_state.link.if_name, gre_ifname) && !strcmp(master->if_type, GRE_TYPE_NAME)) {
                /* GRE link lost */
                cm2_ovsdb_connection_remove_uplink(gre_ifname);
            } else {
                br_update = false;
            }

            if (br_update)
                cm2_update_bridge_cfg(g_state.link.bridge_name, g_state.link.if_name, false,
                                      CM2_PAR_NOT_SET, true);
        }

        ret = cm2_util_get_ip_inet_state_cfg(master->if_name, &ipv4);
        if (!ret && ipv4.assign_scheme != CM2_IP_STATIC)
            cm2_ovsdb_remove_Wifi_Inet_Config(gre_ifname, true);
    }

    ret = cm2_ovsdb_set_Wifi_Inet_Config_network_state(port_state, master->if_name);
    if (!ret)
        LOGW("%s: %s: Failed to set network state %d", __func__, master->if_name, port_state);
}

static bool
cm2_util_is_supported_main_link(const char *if_name, const char *if_type)
{
    return  cm2_is_eth_type(if_type) ||
            (cm2_is_wifi_type(if_type) &&
            (cm2_util_is_gre_station(if_name) || cm2_util_is_wds_station(if_name)));
}

static void
cm2_ovsdb_util_translate_master_port_state(struct schema_Wifi_Master_State *master)
{
    struct schema_Connection_Manager_Uplink con;
    char                                    gre_ifname[IFNAME_SIZE];
    bool                                    port_state;
    bool                                    con_exist;
    bool                                    wds;
    bool                                    update;

    memset(&con, 0, sizeof(con));
    wds = false;
    update = true;

    if (strlen(master->port_state) <= 0)
        return;

    LOGN("%s: Detected new port_state = %s", master->if_name, master->port_state);

    port_state = strcmp(master->port_state, "active") == 0 ? true : false;

    if (!strcmp(master->if_type, VIF_TYPE_NAME) && cm2_util_vif_is_sta(master->if_name)) {
        wds = cm2_util_is_wds_station(master->if_name);
        cm2_util_ifname2gre(gre_ifname, sizeof(gre_ifname), master->if_name);
        cm2_ovsdb_util_handle_master_sta_port_state(master, port_state, gre_ifname, wds);
       /* Update Connection_Manager_Uplink for wds, skip legacy station */
        update = wds;
    }

    con_exist = cm2_ovsdb_connection_get_connection_by_ifname(master->if_name, &con);

    if (wds && !con_exist && !port_state)
        return;

    if (!wds &&
        !cm2_util_is_supported_main_link(master->if_name, master->if_type)) {
        LOGI("%s Skip setting interface as ready ifname = %s iftype = %s",
                 __func__, master->if_name, master->if_type);
        update = false;
    }

    if (!update)
        return;

    LOGI("%s: Add/update uplink in Connection Manager Uplink table", master->if_name);

    STRSCPY(con.if_name, master->if_name);
    con.if_name_exists = true;
    STRSCPY(con.if_type, master->if_type);
    con.if_type_exists = true;
    con.has_L2_exists = true;
    con.has_L2 = port_state;

    WARN_ON(!ovsdb_table_upsert_simple(&table_Connection_Manager_Uplink,
                                       SCHEMA_COLUMN(Connection_Manager_Uplink, if_name),
                                       con.if_name,
                                       &con,
                                       NULL));
}

static void
cm2_ovsdb_util_translate_master_priority(struct schema_Wifi_Master_State *master) {
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+", SCHEMA_COLUMN(Connection_Manager_Uplink, priority), NULL };

    LOGI("%s: Detected new priority = %d", master->if_name, master->uplink_priority);

    if (!cm2_util_is_supported_main_link(master->if_name, master->if_type))
        return;

    memset(&con, 0, sizeof(con));
    if (master->uplink_priority_exists) {
        con.priority_exists = true;
        con.priority = master->uplink_priority;
    }

    int ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                         ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name),
                                         master->if_name),
                                         &con, filter);

    if (!ret)
        LOGE("%s: %s Update priority %d failed", __func__, master->if_name, con.priority);
}
/**** End helper functions*/

static bool
cm2_ovsdb_get_port_by_uuid(struct schema_Port *port, char *port_uuid)
{
    json_t *where;

    where = ovsdb_where_uuid("_uuid", port_uuid);
    if (!where) {
        LOGW("%s: where is NULL", __func__);
        return -1;
    }
    return ovsdb_table_select_one_where(&table_Port, where, port);
}

bool
cm2_ovsdb_is_ipv6_global_link(const char *if_name)
{
    struct schema_IPv6_Address ipv6_addr;
    struct schema_IP_Interface ip;
    int                        ret;
    int                        i;
    json_t                     *where;

    ret = ovsdb_table_select_one(&table_IP_Interface, SCHEMA_COLUMN(IP_Interface, if_name), if_name, &ip);
    if (!ret)
        return false;


    for (i = 0; i < ip.ipv6_addr_len; i++) {
        if (!(where = ovsdb_where_uuid("_uuid", ip.ipv6_addr[i].uuid)))
            continue;

        ret = ovsdb_table_select_one_where(&table_IPv6_Address, where, &ipv6_addr);
        if (!ret)
            continue;

        if (ipv6_addr.address_exists) {
            LOGD("%s: ipv6 addrr: %s", if_name, ipv6_addr.address);
            if (!cm2_osn_is_ipv6_global_link(if_name, ipv6_addr.address))
                continue;

            return true;
        }
    }
    return false;
}

/* GW offline functionality */
static bool
cm2_ovsdb_set_gw_offline_config(bool gw_offline)
{
    struct schema_Node_Config  nconfig;
    json_t                     *where, *con;
    char                       *gw_offline_val;
    bool                       ret;

    memset(&nconfig, 0, sizeof(nconfig));

    nconfig.module_exists = true;
    STRSCPY(nconfig.module, CM2_PM_MODULE_NAME);

    nconfig.key_exists = true;
    STRSCPY(nconfig.key, CM2_PM_GW_OFFLINE);

    nconfig.value_exists = true;
    gw_offline_val = gw_offline ? CM2_PM_GW_OFFLINE_ON : CM2_PM_GW_OFFLINE_OFF;
    STRSCPY(nconfig.value, gw_offline_val);

    where = json_array();

    con = ovsdb_tran_cond_single("module", OFUNC_EQ, CM2_PM_MODULE_NAME);
    json_array_append_new(where, con);
    con = ovsdb_tran_cond_single("key", OFUNC_EQ, CM2_PM_GW_OFFLINE);
    json_array_append_new(where, con);

    ret = ovsdb_table_upsert_where(
            &table_Node_Config,
            where,
            &nconfig,
            false);

    if (!ret) {
        LOGE("%s Insert new row into Node_Config failed", __func__);
        return false;
    }
    return true;
}

static bool
cm2_ovsdb_is_gw_offline_status(char *status)
{
    struct schema_Node_State   nstate;
    json_t                     *where;
    json_t                     *con;
    bool                       ret;

    where = json_array();

    con = ovsdb_tran_cond_single("module", OFUNC_EQ, CM2_PM_MODULE_NAME);
    json_array_append_new(where, con);

    con = ovsdb_tran_cond_single("key", OFUNC_EQ, CM2_PM_GW_OFFLINE_STATUS);
    json_array_append_new(where, con);

    memset(&nstate, 0, sizeof(nstate));
    ret = ovsdb_table_select_one_where(&table_Node_State, where, &nstate);
    if (!ret) {
        LOGI("GW offline status not set");
        return false;
    }

    LOGD("GW offline: status: %s", nstate.value);
    if (strcmp(nstate.value, status) != 0)
        return false;

    return true;
}

bool
cm2_ovsdb_is_gw_offline_enabled(void)
{
    struct schema_Node_State   nstate;
    json_t                     *where;
    json_t                     *con;
    bool                       ret;

    where = json_array();

    con = ovsdb_tran_cond_single("module", OFUNC_EQ, CM2_PM_MODULE_NAME);
    json_array_append_new(where, con);

    con = ovsdb_tran_cond_single("key", OFUNC_EQ, CM2_PM_GW_OFFLINE_CFG);
    json_array_append_new(where, con);

    memset(&nstate, 0, sizeof(nstate));
    ret = ovsdb_table_select_one_where(&table_Node_State, where, &nstate);
    if (!ret) {
        LOGI("GW offline not configured");
        return false;
    }

    LOGD("GW offline: EN: %s", nstate.value);

    if (strcmp(nstate.value, CM2_PM_GW_OFFLINE_CFG_EN) != 0)
        return false;

    return true;
}

bool cm2_ovsdb_is_gw_offline_ready(void)
{
    return cm2_ovsdb_is_gw_offline_status(CM2_PM_GW_OFFLINE_STATUS_READY);
}

bool cm2_ovsdb_is_gw_offline_active(void)
{
    return cm2_ovsdb_is_gw_offline_status(CM2_PM_GW_OFFLINE_STATUS_ACTIVE);
}

bool cm2_ovsdb_enable_gw_offline_conf(void)
{
    return cm2_ovsdb_set_gw_offline_config(true);
}

bool cm2_ovsdb_disable_gw_offline_conf(void)
{
    return cm2_ovsdb_set_gw_offline_config(false);
}

static bool
cm2_ovsdb_get_port_by_name(struct schema_Port *port, char *name)
{
    return ovsdb_table_select_one(&table_Port, SCHEMA_COLUMN(Port, name), name, port);
}

static bool
cm2_ovsdb_get_bridge_by_name(struct schema_Bridge *bridge, char *name)
{
    return ovsdb_table_select_one(&table_Bridge, SCHEMA_COLUMN(Bridge, name), name, bridge);
}

static bool
cm2_ovsdb_is_port_in_bridge(struct schema_Bridge *bridge, struct schema_Port *port)
{
    bool found = false;
    int  i;

    for (i = 0; i < bridge->ports_len; i++ ) {
        LOGD("%s: port uuid: %s", bridge->name, bridge->ports[i].uuid);
        if (!strcmp(bridge->ports[i].uuid, port->_uuid.uuid)) {
            LOGD("Port found in bridge");
            found = true;
            break;
        }
    }
    return found;
}

bool
cm2_ovsdb_connection_update_L3_state(const char *if_name, cm2_par_state_t l3state)
{
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+", SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3), NULL };
    int ret;

    LOGI("%s: Set has_L3 state: %d", if_name, l3state);

    memset(&con, 0, sizeof(con));
    con.has_L3_exists = l3state == CM2_PAR_NOT_SET ? false : true;
    con.has_L3 = l3state == CM2_PAR_TRUE ? true : false;

    ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                     ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                     &con, filter);
    return ret;
}

static bool
cm2_ovsdb_connection_update_priority(const char *if_name, int prio)
{
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+", SCHEMA_COLUMN(Connection_Manager_Uplink, priority), NULL };

    memset(&con, 0, sizeof(con));
    con.priority_exists = true;
    con.priority = prio;

    return ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                      ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                      &con, filter);
}

static bool
cm2_ovsdb_connection_update_used_state(char *if_name, bool state)
{
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+", SCHEMA_COLUMN(Connection_Manager_Uplink, is_used), NULL };

    memset(&con, 0, sizeof(con));
    con.is_used_exists = true;
    con.is_used = state;

    int ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                         ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                         &con, filter);
    return ret;
}

static bool
cm2_ovsdb_connection_update_bridge_state(char *if_name, const char *bridge)
{
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+", SCHEMA_COLUMN(Connection_Manager_Uplink, bridge), NULL };

    memset(&con, 0, sizeof(con));
    con.bridge_exists = true;
    STRSCPY(con.bridge, bridge);

    int ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                         ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                         &con, filter);
    return ret;
}

bool
cm2_ovsdb_connection_update_loop_state(const char *if_name, bool state)
{
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+", SCHEMA_COLUMN(Connection_Manager_Uplink, loop), NULL };
    int ret;

    memset(&con, 0, sizeof(con));
    con.loop_exists = true;
    con.loop = state;

    ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                     ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                     &con, filter);
    return ret;
}

bool cm2_ovsdb_connection_get_connection_by_ifname(const char *if_name, struct schema_Connection_Manager_Uplink *uplink) {
    return ovsdb_table_select_one(&table_Connection_Manager_Uplink, SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name, uplink);
}

void cm2_ovsdb_connection_update_ble_phy_link(void) {
    struct schema_Connection_Manager_Uplink *uplink;
    void                                    *uplink_p;
    bool                                    state;
    int                                     wifi_cnt;
    int                                     eth_cnt;
    int                                     count;
    int                                     i;

    uplink_p = ovsdb_table_select_typed(&table_Connection_Manager_Uplink,
                                        SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2),
                                        OCLM_BOOL,
                                        (void *) &state,
                                        &count);

    LOGI("BLE active phy links: %d",  count);

    wifi_cnt = 0;
    eth_cnt  = 0;

    if (uplink_p) {
        for (i = 0; i < count; i++) {
            uplink = (struct schema_Connection_Manager_Uplink *) (uplink_p + table_Connection_Manager_Uplink.schema_size * i);
            LOGI("Link %d: ifname = %s iftype = %s active state= %d", i, uplink->if_name, uplink->if_type, uplink->has_L2);

            if (cm2_is_eth_type(uplink->if_type))
                eth_cnt++;
            else
                wifi_cnt++;
        }
        free(uplink_p);
    }

    state = eth_cnt > 0 ? true : false;
    cm2_ble_onboarding_set_status(state, BLE_ONBOARDING_STATUS_ETHERNET_LINK);
    state = wifi_cnt > 0 ? true : false;
    cm2_ble_onboarding_set_status(state, BLE_ONBOARDING_STATUS_WIFI_LINK);
    cm2_ble_onboarding_apply_config();
}

bool cm2_ovsdb_connection_update_ntp_state(const char *if_name, bool state) {
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+",  SCHEMA_COLUMN(Connection_Manager_Uplink, ntp_state), NULL};
    int ret;

    memset(&con, 0, sizeof(con));

    con.ntp_state_exists = true;
    con.ntp_state = state;

    ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                     ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                     &con, filter);
    return ret;
}

bool cm2_ovsdb_connection_update_unreachable_link_counter(const char *if_name, int counter) {
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+",  SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_link_counter), NULL};
    int ret;

    memset(&con, 0, sizeof(con));

    con.unreachable_link_counter_exists = true;
    con.unreachable_link_counter = counter;

    ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                     ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                     &con, filter);
    return ret;
}

bool cm2_ovsdb_connection_update_unreachable_router_counter(const char *if_name, int counter) {
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+",  SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_router_counter), NULL};
    int ret;

    memset(&con, 0, sizeof(con));

    con.unreachable_router_counter_exists = true;
    con.unreachable_router_counter = counter;

    ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                     ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                     &con, filter);
    return ret;
}

bool cm2_ovsdb_connection_update_unreachable_internet_counter(const char *if_name, int counter) {
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+",  SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_internet_counter), NULL};
    int ret;

    memset(&con, 0, sizeof(con));

    con.unreachable_internet_counter_exists = true;
    con.unreachable_internet_counter = counter;

    ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                     ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                     &con, filter);
    return ret;
}

bool cm2_ovsdb_connection_update_unreachable_cloud_counter(const char *if_name, int counter) {
    struct schema_Connection_Manager_Uplink con;
    char *filter[] = { "+",  SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_cloud_counter), NULL};
    int ret;

    memset(&con, 0, sizeof(con));

    con.unreachable_cloud_counter_exists = true;
    con.unreachable_cloud_counter = counter;

    ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                         ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                         &con, filter);
    return ret;
}

static
bool cm2_ovsdb_connection_remove_uplink(char *if_name) {
    int ret;

    ret = ovsdb_table_delete_simple(&table_Connection_Manager_Uplink,
                                   SCHEMA_COLUMN(Connection_Manager_Uplink, if_name),
                                   if_name);
    if (!ret)
        LOGI("%s Remove row failed %s", __func__, if_name);

    return ret == 1;
}

static bool cm2_util_block_udhcpc_on_gre(char *if_name, char *if_type)
{
    struct schema_Wifi_Inet_Config icfg;
    bool                           dhcp_static;
    bool                           empty_addr;

    if (strcmp(if_type, GRE_TYPE_NAME) != 0)
        return false;

    if (!kconfig_enabled(CONFIG_CM2_USE_DRYRUN_ON_GRE))
        return true;

    memset(&icfg, 0, sizeof(icfg));
    if (!ovsdb_table_select_one(&table_Wifi_Inet_Config, SCHEMA_COLUMN(Wifi_Inet_Config, if_name), if_name, &icfg)) {
        LOGW("%s: Get item from Wifi_Inet_Config_failed", if_name);
        return false;
    }
    dhcp_static = !strcmp(icfg.ip_assign_scheme, "static");
    empty_addr = !((strlen(icfg.inet_addr) > 0) && strcmp(icfg.inet_addr, "0.0.0.0") != 0);
    /* Skip GRE configuration for static IP */
    if (dhcp_static && !empty_addr)
        return true;

    return false;
}

static void cm2_util_skip_gre_configuration(char *if_name)
{
    int ret;

    LOGI("%s: Skipping GRE configuration", if_name);

    ret = cm2_ovsdb_connection_update_L3_state(if_name, CM2_PAR_TRUE);
    if (!ret)
        LOGW("%s: %s: Update L3 state failed ret = %d",
             __func__, if_name, ret);
}

void cm2_connection_set_L3(struct schema_Connection_Manager_Uplink *uplink) {
    if (!uplink->has_L2)
        return;

    cm2_ovsdb_connection_update_L3_state(uplink->if_name, CM2_PAR_NOT_SET);

    if (cm2_is_eth_type(uplink->if_type))
        cm2_update_bridge_cfg(CONFIG_TARGET_LAN_BRIDGE_NAME, uplink->if_name, false,
                              CM2_PAR_FALSE, false);

    if (cm2_util_block_udhcpc_on_gre(uplink->if_name, uplink->if_type))
        cm2_util_skip_gre_configuration(uplink->if_name);
    else
        cm2_dhcpc_start_dryrun(uplink->if_name, uplink->if_type, 0);
}

bool cm2_connection_get_used_link(struct schema_Connection_Manager_Uplink *uplink) {
    return ovsdb_table_select_one(&table_Connection_Manager_Uplink, SCHEMA_COLUMN(Connection_Manager_Uplink, is_used), "true", uplink);
}

static void cm2_connection_clear_used(void)
{
    cm2_par_state_t macrep;
    int             ret;

    if (g_state.link.is_used) {
        LOGN("%s: Remove old used link.", g_state.link.if_name);
        if (g_state.link.is_bridge) {
            cm2_ovsdb_set_dhcpv6_client(g_state.link.bridge_name, false);
            macrep = cm2_is_eth_type(g_state.link.if_type) ? CM2_PAR_TRUE : CM2_PAR_NOT_SET;
            cm2_update_bridge_cfg(g_state.link.bridge_name, g_state.link.if_name, false,
                                  macrep, true);
        }

        ret = cm2_ovsdb_connection_update_used_state(g_state.link.if_name, false);
        if (!ret)
            LOGI("%s: %s: Failed to clear used state", __func__, g_state.link.if_name);

        g_state.link.is_bridge = false;
        g_state.link.is_used = false;
        g_state.link.priority = -1;
    }
}

static void cm2_util_switch_role(struct schema_Connection_Manager_Uplink *uplink)
{
    if (!g_state.connected) {
        LOGI("Device is not connected to the Cloud");
        return;
    }

    if (cm2_is_wifi_type(g_state.link.if_type) &&
        cm2_is_eth_type(uplink->if_type)) {
        LOGI("Device switch from Leaf to GW, trigger restart managers");
        target_device_restart_managers();
    }
}

static bool cm2_connection_set_is_used(struct schema_Connection_Manager_Uplink *uplink)
{
    int ret;

    if (!uplink->has_L2 || !uplink->has_L3)
        return false;

    if (g_state.link.is_used && uplink->priority <= g_state.link.priority)
        return false;

    cm2_util_switch_role(uplink);
    cm2_connection_clear_used();

    STRSCPY(g_state.link.if_name, uplink->if_name);
    STRSCPY(g_state.link.if_type, uplink->if_type);
    g_state.link.is_used = true;
    g_state.link.priority = uplink->priority;
    if (uplink->bridge_exists) {
        g_state.link.is_bridge = true;
        STRSCPY(g_state.link.bridge_name, uplink->bridge);
    }

    LOGN("%s: Set new used link", uplink->if_name);

    ret = cm2_ovsdb_connection_update_used_state(uplink->if_name, true);
    if (!ret) {
        LOGW("%s: %s: Failed to set used state", __func__, uplink->if_name);
        return false;
    }
    return true;
}

void cm2_check_master_state_links(void) {
    struct schema_Wifi_Master_State *link;
    void   *link_p;
    int    count;
    int    ret;
    int    i;

    link_p = ovsdb_table_select(&table_Wifi_Master_State,
                                  SCHEMA_COLUMN(Wifi_Master_State, port_state),
                                  "active",
                                  &count);

    LOGI("%s: Available active links in Wifi_Master_State = %d", __func__, count);

    if (!link_p)
        return;

    for (i = 0; i < count; i++) {
        link = (struct schema_Wifi_Master_State *) (link_p + table_Wifi_Master_State.schema_size * i);

        if (cm2_util_is_wds_station(link->if_name))
            continue;

        if (cm2_util_vif_is_sta(link->if_name) &&
            link->inet_addr_exists &&
            strcmp(link->inet_addr, "0.0.0.0") != 0) {

            LOGN("%s: Trigger creating gre", link->if_name);

            ret = cm2_ovsdb_insert_Wifi_Inet_Config(link);
            if (!ret)
               LOGW("%s: %s Failed to insert GRE", __func__, link->if_name);
        }
    }
    free(link_p);
}

void cm2_connection_recalculate_used_link(void) {
    struct schema_Connection_Manager_Uplink *uplink;
    void *uplink_p;
    int count, i;
    int priority = -1;
    int index = 0;
    bool state = true;
    bool check_master = true;

    uplink_p = ovsdb_table_select_typed(&table_Connection_Manager_Uplink,
                                        SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3),
                                        OCLM_BOOL,
                                        (void *) &state,
                                        &count);

    LOGN("%s: Recalculating link. Available links %d", __func__, count);

    if (uplink_p) {
        for (i = 0; i < count; i++) {
            uplink = (struct schema_Connection_Manager_Uplink *) (uplink_p + table_Connection_Manager_Uplink.schema_size * i);
            LOGI("Link %d: ifname = %s priority = %d active state= %d", i, uplink->if_name, uplink->priority, uplink->has_L3);
            if (uplink->priority > priority) {
                index = i;
                priority = uplink->priority;
            }
        }
        uplink = (struct schema_Connection_Manager_Uplink *) (uplink_p + table_Connection_Manager_Uplink.schema_size * index);
        cm2_connection_set_is_used(uplink);
        free(uplink_p);
        check_master = false;
    }

    if (check_master)
        cm2_check_master_state_links();
}

void cm2_ovsdb_connection_clean_link_counters(char *if_name)
{
    struct schema_Connection_Manager_Uplink uplink;
    char   *filter[] = { "+",
                         SCHEMA_COLUMN(Connection_Manager_Uplink, ntp_state),
                         SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_link_counter),
                         SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_router_counter),
                         SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_cloud_counter),
                         SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_internet_counter),
                         NULL };
    int    ret;

    memset(&uplink, 0, sizeof(uplink));

    LOGN("%s: Clean up link counters", if_name);

    uplink.ntp_state = false;
    uplink.ntp_state_exists = true;

    uplink.unreachable_link_counter = -1;
    uplink.unreachable_link_counter_exists = true;

    uplink.unreachable_router_counter = -1;
    uplink.unreachable_router_counter_exists = true;

    uplink.unreachable_cloud_counter = -1;
    uplink.unreachable_cloud_counter_exists = true;

    uplink.unreachable_internet_counter = -1;
    uplink.unreachable_internet_counter_exists = true;

    ret = ovsdb_table_update_where_f(&table_Connection_Manager_Uplink,
                                     ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), if_name),
                                     &uplink, filter);
    if (!ret)
        LOGW("%s Update row failed for %s", __func__, if_name);
}

int cm2_ovsdb_ble_config_update(uint8_t ble_status)
{
    struct schema_AW_Bluetooth_Config ble;
    char   *filter[] = { "+",
                         SCHEMA_COLUMN(AW_Bluetooth_Config, mode),
                         SCHEMA_COLUMN(AW_Bluetooth_Config, command),
                         SCHEMA_COLUMN(AW_Bluetooth_Config, payload),
                         SCHEMA_COLUMN(AW_Bluetooth_Config, interval_millis),
                         SCHEMA_COLUMN(AW_Bluetooth_Config, txpower),
                         NULL };
    int    ret;

    memset(&ble, 0, sizeof(ble));

    SCHEMA_SET_STR(ble.mode, CM2_BLE_MODE_ON);
    SCHEMA_SET_STR(ble.command, CM2_BLE_MSG_ONBOARDING);
    SCHEMA_SET_INT(ble.interval_millis, CM2_BLE_INTERVAL_VALUE_DEFAULT);
    SCHEMA_SET_INT(ble.txpower, CM2_BLE_TXPOWER_VALUE_DEFAULT);
    snprintf(ble.payload, sizeof(ble.payload), "%02x:00:00:00:00:00", ble_status);
    ble.payload_exists = true;
    ble.payload_present = true;

    ret = ovsdb_table_upsert_simple_f(&table_AW_Bluetooth_Config,
                                      SCHEMA_COLUMN(AW_Bluetooth_Config, command),
                                      ble.command,
                                      &ble,
                                      NULL,
                                      filter);
    if (!ret)
        LOGE("%s Insert new row failed for %s", __func__, ble.command);

    return ret == 1;
}

int
cm2_ovsdb_ble_set_connectable(bool state)
{
    struct schema_AW_Bluetooth_Config ble;
    char   *filter[] = { "+",
                       SCHEMA_COLUMN(AW_Bluetooth_Config, connectable),
                       NULL };

    memset(&ble, 0, sizeof(ble));
    SCHEMA_SET_INT(ble.connectable, state);

    LOGI("Changing ble connectable state: %d", state);

    return  ovsdb_table_update_where_f(&table_AW_Bluetooth_Config,
                 ovsdb_where_simple(SCHEMA_COLUMN(AW_Bluetooth_Config, command), CM2_BLE_MSG_ONBOARDING),
                 &ble, filter);
}

void cm2_set_ble_onboarding_link_state(bool state, char *if_type, char *if_name)
{
    if (g_state.connected)
        return;
    if (cm2_is_eth_type(if_type)) {
        cm2_ble_onboarding_set_status(state, BLE_ONBOARDING_STATUS_ETHERNET_LINK);
    } else if (cm2_is_wifi_type(if_type)) {
        cm2_ble_onboarding_set_status(state, BLE_ONBOARDING_STATUS_WIFI_LINK);
    }
    cm2_ble_onboarding_apply_config();
}

void callback_Wifi_Master_State(ovsdb_update_monitor_t *mon,
                                struct schema_Wifi_Master_State *old_rec,
                                struct schema_Wifi_Master_State *master)
{
    int ret;

    LOGD("%s calling %s", __func__, master->if_name);

    if (mon->mon_type == OVSDB_UPDATE_DEL) {
        char gre_ifname[IFNAME_SIZE];

        LOGI("%s: Remove row detected in Master State", master->if_name);

        if (cm2_util_vif_is_sta(master->if_name) && !strcmp(master->if_type, VIF_TYPE_NAME)) {
            cm2_util_ifname2gre(gre_ifname, sizeof(gre_ifname), master->if_name);
            ret = cm2_ovsdb_connection_remove_uplink(gre_ifname);
            if (!ret)
                LOGI("%s Remove uplink %s failed", __func__, gre_ifname);
        }

        if (strstr(master->if_name, "g-") == master->if_name &&
            cm2_util_vif_is_sta(master->if_name + strlen("g-"))) {
            ret = cm2_ovsdb_connection_remove_uplink(master->if_name);
            if (!ret)
                LOGI("%s Remove uplink %s failed", __func__, master->if_name);
        }

        return;
    }

    if (!cm2_is_wan_link_management() && cm2_is_eth_type(master->if_type))
        return;

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, port_state)))
            cm2_ovsdb_util_translate_master_port_state(master);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, network_state)))
    {
        LOGI("%s: Detected network_state change = %s", master->if_name, master->network_state);
        if (g_state.link.is_bridge &&
            !strncmp(master->if_name, g_state.link.bridge_name, strlen(master->if_name))) {
            ret = cm2_ovsdb_set_Wifi_Inet_Config_network_state(true, master->if_name);
            if (!ret)
                LOGW("%s: %s: Failed to set network enabled", __func__, master->if_name);
        }
    }

    /* Creating GRE interfaces */
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, inet_addr)))
        cm2_ovsdb_util_translate_master_ip(master);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, uplink_priority)))
        cm2_ovsdb_util_translate_master_priority(master);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, netmask)))
        LOGI("%s: netmask changed %s", master->if_name, master->netmask);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, if_name)))
        LOGD("%s if_name = %s changed not handled ", __func__, master->if_name);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, if_type)))
        LOGD("%s if_type = %s changed not handled", __func__, master->if_type);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, if_uuid)))
        LOGD("%s if_uuid changed", __func__);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, dhcpc)))
        LOGD("%s dhcpc changed not handled", __func__);
}

static void
cm2_util_set_not_used_link(void)
{
    cm2_connection_clear_used();
    cm2_update_state(CM2_REASON_LINK_NOT_USED);
    cm2_connection_recalculate_used_link();
}

static
void cm2_util_sync_limp_state(char *br, char *port, bool state)
{
    bool u;

    if (cm2_is_wan_bridge()) {
        if (strstr(port, "patch-h2w")){
            LOGI("Patch port detected, added [%d]", state);
            g_state.dev_type = state ? CM2_DEVICE_BRIDGE : CM2_DEVICE_ROUTER;
        }
    } else {
        u = state &&
            g_state.link.is_bridge &&
            !strcmp(g_state.link.bridge_name, br) &&
            !strcmp(g_state.link.if_name, port);

        if (!u)
            u = !state &&
                !g_state.link.is_bridge &&
                !strcmp(g_state.link.if_name, port);

        if (u) {
            LOGI("%s: Limp state updated: %d", port, !state);
            g_state.dev_type = state ? CM2_DEVICE_BRIDGE : CM2_DEVICE_ROUTER;
        }
    }
}

static int
cm2_util_update_bridge_conf(char *up_src, char *up_dst, char *up_raw, cm2_par_state_t macrep)
{
    int     ret;

    LOGI("Updating bridge configuration: %s -> %s", up_src, up_dst);
    ret = cm2_ovsdb_copy_dhcp_ipv4_configuration(up_src, up_dst);
    if (ret < 0) {
        LOGI("%s: Failed to update IPv4 configuration from %s to %s", __func__,
             up_src, up_dst);
        return -1;
    }

    cm2_ovsdb_set_dhcpv6_client(up_src, false);

    /* Put main raw interface into dest uplink */
    if (up_raw)
        cm2_update_bridge_cfg(up_dst, up_raw, true, macrep, true);

    cm2_ovsdb_set_dhcpv6_client(g_state.link.bridge_name, true);

    return 0;
}

static void
cm2_util_update_bridge_handle(
        struct schema_Connection_Manager_Uplink *old_uplink,
        struct schema_Connection_Manager_Uplink *uplink)
{
    cm2_par_state_t   macrep;
    char              s_uplink[64];
    char              d_uplink[64];
    char              r_uplink[64];
    char              *r_p;

    if (!cm2_util_get_link_is_used(uplink)) {
        LOGI("%s: bridge [%s] updated for not main link", uplink->if_name, uplink->bridge);
        return;
    }

    r_p = NULL;
    macrep = cm2_is_eth_type(uplink->if_type) ? CM2_PAR_FALSE : CM2_PAR_NOT_SET;

    /* Main uplink in bridge */
    if (uplink->bridge_exists) {
        STRSCPY(g_state.link.bridge_name, uplink->bridge);
        g_state.link.is_bridge = true;
        cm2_util_sync_limp_state(uplink->bridge, uplink->if_name, true);

        if (old_uplink->bridge_exists) {
            /* Uplink in the same bridge */
            if (strcmp(uplink->bridge, old_uplink->bridge) == 0)
                return;

            STRSCPY(s_uplink, old_uplink->bridge);
            STRSCPY(d_uplink, uplink->bridge);
            STRSCPY(r_uplink, uplink->if_name);
            /* Remove uplink from previous bridge */
            cm2_update_bridge_cfg(s_uplink, r_uplink, false, macrep, true);
       } else {
            /* New bridge configuration */
            STRSCPY(s_uplink, uplink->if_name);
            STRSCPY(d_uplink, uplink->bridge);
            STRSCPY(r_uplink, uplink->if_name);
       }
       r_p = r_uplink;
    } else {
        g_state.link.is_bridge = false;
        cm2_util_sync_limp_state(uplink->bridge, uplink->if_name, false);
        STRSCPY(s_uplink, old_uplink->bridge);
        STRSCPY(d_uplink, uplink->if_name);
        /* Remove uplink from previous bridge */
        cm2_update_bridge_cfg(s_uplink, d_uplink, false, macrep, true);
    }

    WARN_ON(cm2_util_update_bridge_conf(s_uplink, d_uplink, r_p, macrep));
}

static bool
cm2_uplink_skip_L2_handle(struct schema_Connection_Manager_Uplink *uplink)
{
    if (!cm2_is_wan_link_management() && cm2_is_eth_type(uplink->if_type)) {
        LOGI("%s Uplink skipped", uplink->if_name);
        return true;
    }

    return false;
}

static void
cm2_util_handling_loop_state(struct schema_Connection_Manager_Uplink *uplink)
{
    bool delayed_update;
    int  eth_timeout;

    if (!cm2_is_eth_type(uplink->if_type))
        return;

    delayed_update = uplink->has_L2_exists && uplink->has_L2 &&
                     uplink->has_L3_exists && !uplink->has_L3 &&
                     g_state.link.is_used &&
                     cm2_is_wifi_type(g_state.link.if_type);

    if (delayed_update) {
        LOGI("%s: Detected Leaf with plugged ethernet, connected = %d",
             uplink->if_name, g_state.connected);
        eth_timeout = g_state.connected ? CONFIG_CM2_ETHERNET_SHORT_DELAY : CONFIG_CM2_ETHERNET_LONG_DELAY;
        cm2_delayed_eth_update(uplink->if_name, eth_timeout);
        return;
    }

    if (uplink->loop_exists && uplink->loop)
        cm2_ovsdb_connection_update_loop_state(uplink->if_name, false);

    return;
}

static void
cm2_Connection_Manager_Uplink_handle_update(
        ovsdb_update_monitor_t *mon,
        struct schema_Connection_Manager_Uplink *old_uplink,
        struct schema_Connection_Manager_Uplink *uplink)
{
    bool clean_up_counters = false;
    bool reconfigure       = false;
    char *filter[8];
    int  def_priority;
    int  idx = 0;
    int  ret;

    filter[idx++] = "+";

    /* Configuration part. Setting by NM, WM, Cloud */
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, priority))) {
        LOGN("%s: Uplink table: detected priority change = %d", uplink->if_name, uplink->priority);
        LOGI("%s: Main link priority: %d new priority = %d uplink is used = %d",
             uplink->if_name, g_state.link.priority , uplink->priority , uplink->is_used);
        if ((g_state.link.is_used && uplink->has_L3 && g_state.link.priority < uplink->priority) ||
            cm2_util_get_link_is_used(uplink)) {
           cm2_util_switch_role(uplink);
           reconfigure = true;
        }
    }
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, loop))) {
        LOGN("%s: Uplink table: detected loop change = %d", uplink->if_name, uplink->loop);
        if (!cm2_is_wan_link_management() &&
            uplink->loop_exists && uplink->loop && uplink->loop)
            cm2_util_handling_loop_state(uplink);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, bridge))) {
        LOGN("%s: Uplink table: detected bridge change = %s", uplink->if_name, uplink->bridge);
        cm2_util_update_bridge_handle(old_uplink, uplink);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2))) {
        LOGN("%s: Uplink table: detected has_L2 change = %d", uplink->if_name, uplink->has_L2);

        if (uplink->has_L2) {
            if (!uplink->priority_exists) {
                def_priority = cm2_util_set_defined_priority(uplink->if_type);
                LOGI("%s: Set default priority: %d", uplink->if_name, def_priority);
                cm2_ovsdb_connection_update_priority(uplink->if_name, def_priority);
            }
            cm2_set_ble_onboarding_link_state(true, uplink->if_type, uplink->if_name);
        }

        if (!cm2_uplink_skip_L2_handle(uplink)) {
            if (!uplink->has_L2) {
                cm2_dhcpc_stop_dryrun(uplink->if_name);

                if (cm2_is_eth_type(uplink->if_type))
                    cm2_update_bridge_cfg(CONFIG_TARGET_LAN_BRIDGE_NAME, uplink->if_name, false,
                                          CM2_PAR_FALSE, false);

                if (cm2_util_get_link_is_used(uplink)) {
                    reconfigure = true;
                } else {
                    clean_up_counters = true;
                }

                filter[idx++] = SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3);
                uplink->has_L3_exists = false;
                g_state.link.vtag.state = CM2_VTAG_NOT_USED;

                if (!strcmp(g_state.link.if_name, uplink->if_name) && g_state.link.restart_pending) {
                    g_state.link.restart_pending = false;
                    LOGI("Restart link due to restart pending");
                    ret = cm2_ovsdb_set_Wifi_Inet_Config_network_state(true, g_state.link.if_name);
                    if (!ret)
                        LOGW("Force enable main uplink interface failed");
                }
            } else {
                cm2_connection_set_L3(uplink);
            }
        }
    }
    /* End of configuration part */

    /* Setting state part */
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3))) {
        LOGN("%s: Uplink table: detected has_L3 change = %d", uplink->if_name, uplink->has_L3);

        if (!cm2_connection_set_is_used(uplink) && cm2_is_wan_link_management())
            cm2_util_handling_loop_state(uplink);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, is_used))) {
        LOGN("%s: Uplink table: detected link_usage change = %d",
             uplink->if_name, uplink->is_used);

        if (!uplink->is_used && !cm2_util_get_link_is_used(uplink))
            clean_up_counters = true;

        if (uplink->is_used)
            cm2_update_state(CM2_REASON_LINK_USED);
    }

    if (uplink->is_used) {
        bool ble_update = false;

        if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_router_counter))) {
            cm2_ble_onboarding_set_status(uplink->unreachable_router_counter == 0, BLE_ONBOARDING_STATUS_ROUTER_OK);
            ble_update = true;
        }

        if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_cloud_counter))) {
            cm2_ble_onboarding_set_status(uplink->unreachable_cloud_counter == 0, BLE_ONBOARDING_STATUS_CLOUD_OK);
            ble_update = true;
        }

        if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_internet_counter))) {
            cm2_ble_onboarding_set_status(uplink->unreachable_internet_counter == 0, BLE_ONBOARDING_STATUS_INTERNET_OK);
            ble_update = true;
        }

        if (ble_update)
            cm2_ble_onboarding_apply_config();
    }
    /* End of State part */


    if (clean_up_counters || reconfigure) {
        LOGN("%s: Clean up counters", uplink->if_name);

        filter[idx++] = SCHEMA_COLUMN(Connection_Manager_Uplink, ntp_state);
        uplink->ntp_state = false;
        uplink->ntp_state_exists = true;

        filter[idx++] = SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_link_counter);
        uplink->unreachable_link_counter = -1;
        uplink->unreachable_link_counter_exists = true;

        filter[idx++] = SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_router_counter);
        uplink->unreachable_router_counter = -1;
        uplink->unreachable_router_counter_exists = true;

        filter[idx++] = SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_cloud_counter);
        uplink->unreachable_cloud_counter = -1;
        uplink->unreachable_cloud_counter_exists = true;

        filter[idx++] = SCHEMA_COLUMN(Connection_Manager_Uplink, unreachable_internet_counter);
        uplink->unreachable_internet_counter = -1;
        uplink->unreachable_internet_counter_exists = true;

        filter[idx] = NULL;

        int ret = ovsdb_table_update_f(&table_Connection_Manager_Uplink, uplink, filter);
        if (!ret)
            LOGI("%s Update row failed for %s", __func__, uplink->if_name);
    }

    if (reconfigure) {
        LOGN("%s Reconfigure main link", uplink->if_name);
        cm2_connection_clear_used();
        cm2_update_state(CM2_REASON_LINK_NOT_USED);
        cm2_connection_recalculate_used_link();
    }
}

void callback_Connection_Manager_Uplink(ovsdb_update_monitor_t *mon,
                                        struct schema_Connection_Manager_Uplink *old_row,
                                        struct schema_Connection_Manager_Uplink *uplink)
{
    LOGD("%s mon_type = %d", __func__, mon->mon_type);

    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            if (cm2_util_get_link_is_used(uplink)) {
                cm2_util_set_not_used_link();
            }
            cm2_dhcpc_stop_dryrun(uplink->if_name);

            if (cm2_is_eth_type(uplink->if_type))
                cm2_update_bridge_cfg(CONFIG_TARGET_LAN_BRIDGE_NAME, uplink->if_name, false,
                                      CM2_PAR_FALSE, false);
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            cm2_Connection_Manager_Uplink_handle_update(mon, old_row, uplink);
            break;
    }
}

static void cm2_Wifi_Inet_State_handle_dhcpc(struct schema_Wifi_Inet_State *inet_state)
{
    const char   *tag = NULL;
    size_t       bufsz;
    char         buf[8192];
    char         *pbuf;
    int          vsc;
    int          i;

    LOGD("%s; Update dhcpc column", inet_state->if_name);
    if (strcmp(inet_state->if_name, cm2_get_uplink_name())) {
        LOGD("%s: Skip update, only %s support", inet_state->if_name, cm2_get_uplink_name());
        return;
    }

    memset(&buf, 0, sizeof(buf));

    for (i = 0; i < inet_state->dhcpc_len; i++) {
        LOGD("dhcpc: [%s]: %s", inet_state->dhcpc_keys[i], inet_state->dhcpc[i]);

        if (strcmp(inet_state->dhcpc_keys[i], "vendorspec"))
            continue;

        bufsz = base64_decode(buf, sizeof(buf), inet_state->dhcpc[i]);
        if ((int) bufsz < 0) {
            LOGW("vtag: base64: Error decoding buffer");
            return;
        }

        if (bufsz > sizeof(buf)) {
            LOGW("vtag: base64: Buf overflowed: bufsz = %zu buf size = %zu", bufsz, sizeof(buf));
            return;
        }

        if (strlen(buf) > bufsz) {
            if (bufsz + 1 > sizeof(buf)) {
                LOGW("vtag: base64: Buf is not null terminated and too long: bufsz = %zu buf size = %zu",
                     bufsz, sizeof(buf));
                return;
            }
            buf[bufsz] = '\0';
        }
        vsc = 0;
        pbuf = buf;
        while (pbuf <= buf + bufsz) {
            if (pbuf + strlen(pbuf) > buf + bufsz) {
                LOGW("vtag: base64: Format error");
                return;
            }

            if (vsc >= CM2_BASE64_ARGV_MAX) {
                LOGW("vtag: base64: Too many arguments in buffer");
                return;
            }

            tag = strstr(pbuf, "tag=");
            if (tag) {
                LOGI("vtag: tag detected: %s", tag);
                tag += 4;
                break;
            }
            vsc++;
            pbuf += strlen(pbuf) + 1;
        }

        if (tag == NULL) {
            LOGD("vtag: tag not detected");
            return;
        }

        if (g_state.link.vtag.state == CM2_VTAG_PENDING) {
            LOGI("vtag: state error: already running");
            return;
        }

        g_state.link.vtag.tag = atoi(tag);
        LOGI("vtag: set new tag: %d", g_state.link.vtag.tag);

        cm2_update_state(CM2_REASON_SET_NEW_VTAG);
    }
}

void callback_Wifi_Inet_State(ovsdb_update_monitor_t *mon,
                              struct schema_Wifi_Inet_State *old_row,
                              struct schema_Wifi_Inet_State *inet_state)
{
    LOGD("%s mon_type = %d", __func__, mon->mon_type);

    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_State, inet_addr))) {
                LOGI("%s: inet state update: inet_addr: %s", inet_state->if_name, inet_state->inet_addr);
                if (g_state.link.is_bridge) {
                    if (strcmp(inet_state->if_type, BRIDGE_TYPE_NAME) ||
                        strcmp(inet_state->if_name, g_state.link.bridge_name))
                        break;
                } else if (strcmp(g_state.link.if_name, inet_state->if_name)) {
                    break;
                }

                if (cm2_is_wifi_type(inet_state->if_type))
                    break;

                cm2_util_set_dhcp_ipv4_cfg_from_inet(inet_state, false);
                cm2_util_set_local_ip_cfg(inet_state, &g_state.link.ipv4);
            }

            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_State, dhcpc)))
                cm2_Wifi_Inet_State_handle_dhcpc(inet_state);

            break;
    }
}

bool cm2_ovsdb_WiFi_Inet_State_is_ip(const char *if_name) {
     struct schema_Wifi_Inet_State inet_state;
     bool                          ret;

     ret = false;

     if (ovsdb_table_select_one(&table_Wifi_Inet_State, SCHEMA_COLUMN(Wifi_Inet_State, if_name), if_name, &inet_state))
         ret = strcmp(inet_state.inet_addr, "0.0.0.0") == 0 ? false : true;

     return ret;
}

static void cm2_reconfigure_ethernet_states(bool blocked)
{
    struct schema_Connection_Manager_Uplink *uplink;
    void                                    *uplink_p;
    int                                     count;
    int                                     i;

    uplink_p = ovsdb_table_select(&table_Connection_Manager_Uplink,
                                  SCHEMA_COLUMN(Connection_Manager_Uplink, if_type),
                                  ETH_TYPE_NAME,
                                  &count);
    if (!uplink_p) {
        LOGD("%s: Ethernet uplinks no avaialble", __func__);
        return;
    }

    LOGI("Reconfigure ethernet phy links: %d",  count);

    for (i = 0; i < count; i++) {
        uplink = (struct schema_Connection_Manager_Uplink *) (uplink_p + table_Connection_Manager_Uplink.schema_size * i);
        LOGI("Link %d: ifname = %s iftype = %s has_L2 = %d has_L3 = %d", i, uplink->if_name, uplink->if_type, uplink->has_L2, uplink->has_L3);
        if (!uplink->has_L2 || !uplink->has_L3_exists)
            continue;

        if (cm2_is_iface_in_bridge(CONFIG_TARGET_LAN_BRIDGE_NAME, uplink->if_name)) {
            LOGI("%s: Skip reconfigure iface in %s", uplink->if_name, CONFIG_TARGET_LAN_BRIDGE_NAME);
            continue;
        }

        if (!blocked) {
            LOGI("%s: disable loop on ethernet port", uplink->if_name);
            cm2_ovsdb_connection_update_loop_state(uplink->if_name, false);
            continue;
        }

        if (!uplink->has_L3) {
            LOGI("%s: Ethernet link must be examinated once again", uplink->if_name);
            cm2_dhcpc_start_dryrun(uplink->if_name, uplink->if_type, 0);
            cm2_delayed_eth_update(uplink->if_name, CONFIG_CM2_ETHERNET_LONG_DELAY);
        }
    }
    free(uplink_p);
}

bool cm2_ovsdb_validate_bridge_port_conf(char *bname, char *pname)
{
    struct schema_Bridge  bridge;
    struct schema_Port    port;

    if (!cm2_ovsdb_get_port_by_name(&port, pname)) {
        LOGD("Port %s does not exists", pname);
        return false;
    }
    if (!cm2_ovsdb_get_bridge_by_name(&bridge, bname)) {
        LOGD("Bridge %s does not exists", bname);
        return false;
    }
    if (!cm2_ovsdb_is_port_in_bridge(&bridge, &port)) {
        LOGD("Port [%s] not included in bridge [%s]", pname, bname);
        return false;
    }

    return true;
}

static void cm2_check_bridge_mismatch(struct schema_Bridge *base_bridge,
                                      struct schema_Bridge *bridge,
                                      bool added)
{
    struct schema_Port port;
    bool               mismatch;
    int                i, j;

    for (i = 0; i < base_bridge->ports_len; i++ ) {
        LOGD("Base: %s: uuid: %s", base_bridge->name, base_bridge->ports[i].uuid);
        mismatch = true;

        for (j = 0; j < bridge->ports_len; j++) {
            LOGD("New: %s: uuid: %s", bridge->name, bridge->ports[j].uuid);
            if (!strcmp(base_bridge->ports[i].uuid, bridge->ports[j].uuid)) {
                mismatch = false;
                break;
            }
        }

        if (mismatch) {
            if (!cm2_ovsdb_get_port_by_uuid(&port, base_bridge->ports[i].uuid)) {
                LOGD("Port does not exist, uuid: %s", base_bridge->ports[i].uuid);
                continue;
            }

            cm2_util_sync_limp_state(base_bridge->name, port.name, added);

            if (strstr(port.name, ETH_TYPE_NAME) && added)
                    cm2_dhcpc_stop_dryrun(port.name);
        }
    }
}

void cm2_ovsdb_set_default_wan_bridge(char *if_name, char *if_type)
{
    if (cm2_is_wan_bridge()) {
        cm2_ovsdb_connection_update_bridge_state(if_name, CM2_WAN_BRIDGE_NAME);
    } else if (cm2_is_wifi_type(if_type)) {
        cm2_ovsdb_connection_update_bridge_state(if_name, CONFIG_TARGET_LAN_BRIDGE_NAME);
    }
}

void callback_IP_Interface(ovsdb_update_monitor_t *mon,
                     struct schema_IP_Interface *old_ip,
                     struct schema_IP_Interface *ip)
{
    struct schema_IPv6_Address ipv6_addr;
    json_t                     *where;
    bool                       ipv6_mismatch;
    bool                       ipv6;
    int                        ret;
    int                        i;


    LOGD("%s mon_type = %d", __func__, mon->mon_type);

    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            LOGI("%s: IP interface removed", ip->if_name);
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(IP_Interface, ipv6_addr))) {
                if (strcmp(ip->if_name, cm2_get_uplink_name())) {
                    LOGI("%s: IP interface skip not main link %s", ip->name, cm2_get_uplink_name());
                    return;
                }
                ipv6 = false;
                for (i = 0; i < ip->ipv6_addr_len; i++) {
                    if (!(where = ovsdb_where_uuid("_uuid", ip->ipv6_addr[i].uuid)))
                        continue;

                    ret = ovsdb_table_select_one_where(&table_IPv6_Address, where, &ipv6_addr);
                    if (!ret)
                        continue;

                    if (ipv6_addr.address_exists &&
                        strlen(ipv6_addr.address) >= 4) {
                        LOGI("%s: ipv6 addrr: %s", ip->if_name, ipv6_addr.address);

                        if (!cm2_osn_is_ipv6_global_link(ip->if_name, ipv6_addr.address))
                            continue;

                        ipv6 = true;
                   }
                }

                LOGI("%s Updated IPv6 configuration: old state: %s, new state: %s, ipv6_controller: %s",
                     ip->if_name, bool2string(ipv6), bool2string(g_state.link.ipv6.is_ip),
                     bool2string(g_state.ipv6_manager_con));

                g_state.link.ipv6.is_ip = ipv6;
                g_state.link.ipv6.assign_scheme = ipv6 ? CM2_IPV6_DHCP : CM2_IP_NOT_SET;

                ipv6_mismatch = (g_state.ipv6_manager_con && !ipv6) || (!g_state.ipv6_manager_con && ipv6);
                if (ipv6_mismatch)
                    cm2_update_state(CM2_REASON_OVS_INIT);
            }
            break;
    }
}

void callback_Bridge(ovsdb_update_monitor_t *mon,
                     struct schema_Bridge *old_bridge,
                     struct schema_Bridge *bridge)
{
    bool r;

    LOGD("%s mon_type = %d", __func__, mon->mon_type);

    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            LOGI("%s: Bridge removed", bridge->name);
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Bridge, ports))) {
               if (strcmp(bridge->name, CONFIG_TARGET_LAN_BRIDGE_NAME) &&
                   (!g_state.link.is_bridge ||
                    (g_state.link.is_bridge && strcmp(bridge->name, g_state.link.bridge_name))))
                   break;

               if (g_state.link.is_bridge && !strcmp(bridge->name, g_state.link.bridge_name)) {
                   if (g_state.link.is_used) {
                       r = cm2_ovsdb_validate_bridge_port_conf(g_state.link.bridge_name,
                                                               g_state.link.if_name);
                       if (!r) {
                           LOGI("Main uplink was removed from bridge %s, but still is in active state",
                                g_state.link.if_name);
                       }
                       break;
                   }

                   if (!cm2_is_wan_link_management())
                       break;

                   if (cm2_is_wifi_type(g_state.link.if_type)) {
                       cm2_reconfigure_ethernet_states(true);
                       break;
                   } else if (cm2_is_eth_type(g_state.link.if_type)) {
                       cm2_reconfigure_ethernet_states(false);
                   }
               }
               /* Added new port into bridge */
               cm2_check_bridge_mismatch(bridge, old_bridge, true);
               /* Removed port from bridge */
               cm2_check_bridge_mismatch(old_bridge, bridge, false);
            }
            break;
    }
}

void callback_Wifi_Route_State(ovsdb_update_monitor_t *mon,
                               struct schema_Wifi_Route_State *old_route,
                               struct schema_Wifi_Route_State *route)
{
    LOGD("%s mon_type = %d", __func__, mon->mon_type);

    switch (mon->mon_type) {
        default:
        case OVSDB_UPDATE_ERROR:
            LOGW("%s: mon upd error: %d", __func__, mon->mon_type);
            return;

        case OVSDB_UPDATE_DEL:
            LOGI("%s: Wifi_Route_State removed", route->if_name);
            if (strcmp(route->if_name, cm2_get_uplink_name())) {
                LOGD("%s: Wifi_Route_State skip not main link %s gateway_hwaddr = %s",
                     route->if_name, cm2_get_uplink_name(), route->gateway_hwaddr);
                return;
            }

            if (route->gateway_hwaddr_exists) {
                LOGD("%s: GW hwaddr cached %s, removed = %s",
                route->if_name, g_state.link.gateway_hwaddr, route->gateway_hwaddr);
            }
            break;
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Route_State, gateway_hwaddr))) {
                if (strcmp(route->if_name, cm2_get_uplink_name())) {
                    LOGI("%s: Wifi_Route_State skip not main link %s gateway_hwaddr = %s",
                         route->if_name, cm2_get_uplink_name(), route->gateway_hwaddr);
                    return;
                }

                if (route->gateway_hwaddr_exists) {
                    STRSCPY(g_state.link.gateway_hwaddr, route->gateway_hwaddr);
                    LOGI("%s: GW hwaddr: %s updated", route->if_name, route->gateway_hwaddr);
                }
                else if (old_route->gateway_hwaddr_exists) {
                    LOGI("%s: GW hwaddr: %s cached", route->if_name, g_state.link.gateway_hwaddr);
                }
            }
            break;
    }
}

bool cm2_ovsdb_set_Manager_target(char *target)
{
    struct schema_Manager manager;
    memset(&manager, 0, sizeof(manager));
    STRSCPY(manager.target, target);
    manager.is_connected = false;
    char *filter[] = { "+", SCHEMA_COLUMN(Manager, target), SCHEMA_COLUMN(Manager, is_connected), NULL };
    int ret = ovsdb_table_update_where_f(&table_Manager, NULL, &manager, filter);
    return ret == 1;
}

bool cm2_ovsdb_set_AWLAN_Node_manager_addr(char *addr)
{
    struct schema_AWLAN_Node awlan;
    memset(&awlan, 0, sizeof(awlan));
    STRSCPY(awlan.manager_addr, addr);
    char *filter[] = { "+", SCHEMA_COLUMN(AWLAN_Node, manager_addr), NULL };
    int ret = ovsdb_table_update_where_f(&table_AWLAN_Node, NULL, &awlan, filter);
    return ret == 1;
}

static void
cm2_awlan_state_update_cb(
        struct schema_AWLAN_Node *awlan,
        schema_filter_t          *filter)
{
    bool ret;

    if (!filter || filter->num <= 0) {
       LOGE("Updating awlan_node (no filter fields)");
       return;
    }

    ret = ovsdb_table_update_f(
            &table_AWLAN_Node,
            awlan, filter->columns);
    if (!ret){
        LOGE("Updating awlan_node");
    }
}

bool cm2_ovsdb_is_port_name(char *port_name)
{
    struct schema_Port port;
    return ovsdb_table_select_one(&table_Port, SCHEMA_COLUMN(Port, name), port_name, &port);
}

bool cm2_ovsdb_update_Port_trunks(const char *ifname, int *trunks, int num_trunks)
{
    struct schema_Port port;
    int                cnt = 0;
    int                ret;

    memset(&port, 0, sizeof(port));

    if(strlen(ifname) == 0)
        return false;

    port._partial_update = true;
    port.trunks_present = true;
    port.trunks_changed = true;
    for (cnt = 0; cnt < num_trunks; cnt++)
    {
        LOGI("%s: trunk = %d", ifname, trunks[cnt]);
        port.trunks[cnt] = trunks[cnt];
    }
    port.trunks_len = num_trunks;

    ret = ovsdb_table_update_where(&table_Port,
            ovsdb_where_simple(SCHEMA_COLUMN(Port, name), ifname),
            &port);
    return (ret == 1);
}

bool cm2_ovsdb_update_Port_tag(const char *if_name, int tag, bool set)
{
    struct schema_Port port;
    int                ret;

    memset(&port, 0, sizeof(port));

    if (strlen(if_name) == 0)
        return 0;

    LOGI("%s: update vtag: %d set = %d", if_name, tag, set);
    port._partial_update = true;

    if (set) {
        SCHEMA_SET_INT(port.tag, tag);
    } else {
        port.tag_present = true;
        port.tag_exists = false;
    }

    ret = ovsdb_table_update_where(&table_Port,
                 ovsdb_where_simple(SCHEMA_COLUMN(Port, name), if_name),
                 &port);

    return ret == 1;
}


static
bool cm2_util_is_dump_master_links(const char *iftype,
                                   const char *ifname)
{
    char stypes[] = "eth vif gre br-wan";

    if (strstr(stypes, iftype) == NULL)
        return false;

    if (strstr(ifname, "pgd"))
        return false;

    if (!strcmp(iftype, VIF_TYPE_NAME) && !cm2_util_vif_is_sta(ifname))
        return false;

    return true;
}

void cm2_ovsdb_dump_debug_data(void)
{
    struct schema_Connection_Manager_Uplink *uplink;
    struct schema_Wifi_Master_State         *link;
    struct schema_Wifi_Inet_Config          *cinet;
    struct schema_Wifi_Inet_State           *sinet;
    struct schema_Wifi_VIF_Config           *cvif;
    struct schema_Wifi_VIF_State            *svif;
    void                                    *link_p;
    int                                     count;
    int                                     i;

    link_p = ovsdb_table_select_where(&table_Connection_Manager_Uplink,
                                      NULL,
                                      &count);
    for (i = 0; i < count; i++) {
        uplink = (struct schema_Connection_Manager_Uplink *) (link_p + table_Connection_Manager_Uplink.schema_size * i);
        LOGI("CMUplink: %s: has_L2: %d, has_L3: %d, is_used: %d",
             uplink->if_name, uplink->has_L2, uplink->has_L3, uplink->is_used);
    }
    if (link_p)
        free(link_p);


    link_p = ovsdb_table_select_where(&table_Wifi_Master_State,
                                      NULL,
                                      &count);
    for (i = 0; i < count; i++) {
        link = (struct schema_Wifi_Master_State *) (link_p + table_Wifi_Master_State.schema_size * i);

        if (!cm2_util_is_dump_master_links(link->if_type, link->if_name))
           continue;

        LOGI("Wifi_MasterState: %s: p_state: %s, n_state: %s, n_addr: %s, n_netmask: %s",
             link->if_name, link->port_state, link->network_state, link->inet_addr, link->netmask);
    }
    if (link_p)
        free(link_p);

    link_p = ovsdb_table_select_where(&table_Wifi_Inet_Config,
                                      NULL,
                                      &count);
    for (i = 0; i < count; i++) {
        cinet = (struct schema_Wifi_Inet_Config *) (link_p + table_Wifi_Inet_Config.schema_size * i);

        if (!cm2_util_is_dump_master_links(cinet->if_type, cinet->if_name))
            continue;

        LOGI("Wifi_Inet_Config: %s: enabled: %d, network: %d, mtu: %d, addr: %s, netmask: %s, gre_ifname = %s, gre_local_addr = %s, gre_remote_addr: %s",
             cinet->if_name, cinet->enabled, cinet->network, cinet->mtu, cinet->inet_addr, cinet->netmask, cinet->gre_ifname, cinet->gre_local_inet_addr, cinet->gre_remote_inet_addr);
    }

    if (link_p)
        free(link_p);

    link_p = ovsdb_table_select_where(&table_Wifi_Inet_State,
                                      NULL,
                                      &count);
    for (i = 0; i < count; i++) {
        sinet = (struct schema_Wifi_Inet_State *) (link_p + table_Wifi_Inet_State.schema_size * i);

        if (!cm2_util_is_dump_master_links(sinet->if_type, sinet->if_name))
            continue;

        LOGI("Wifi_Inet_State: %s: enabled: %d, network: %d, mtu: %d, addr: %s, netmask: %s, gre_ifname = %s, gre_local_addr = %s, gre_remote_addr: %s",
             sinet->if_name, sinet->enabled, sinet->network, sinet->mtu, sinet->inet_addr, sinet->netmask, sinet->gre_ifname, sinet->gre_local_inet_addr, sinet->gre_remote_inet_addr);
    }
    if (link_p)
        free(link_p);

    link_p = ovsdb_table_select(&table_Wifi_VIF_Config,
                                SCHEMA_COLUMN(Wifi_VIF_Config, mode),
                                "sta",
                                &count);
    for (i = 0; i < count; i++) {
        cvif = (struct schema_Wifi_VIF_Config *) (link_p + table_Wifi_VIF_Config.schema_size * i);
        LOGI("Wifi_VIF_Config: %s: parent: %s, enabled: %d", cvif->if_name, cvif->parent, cvif->enabled);
    }

    if (link_p)
        free(link_p);

    link_p = ovsdb_table_select(&table_Wifi_VIF_State,
                                SCHEMA_COLUMN(Wifi_VIF_State, mode),
                                "sta",
                                &count);
    for (i = 0; i < count; i++) {
        svif = (struct schema_Wifi_VIF_State *) (link_p + table_Wifi_VIF_State.schema_size * i);
        LOGI("Wifi_VIF_State: %s: parent: %s, enabled: %d", svif->if_name, svif->parent, svif->enabled);
    }

    if (link_p)
        free(link_p);
}

// Initialize Open_vSwitch, SSL and Manager tables
int cm2_ovsdb_init_tables(void)
{
    // SSL and Manager tables have to be referenced by Open_vSwitch
    // so we use _with_parent() to atomically update (mutate) these references
    struct schema_Open_vSwitch ovs;
    struct schema_Manager manager;
    struct schema_SSL ssl;
    struct schema_AWLAN_Node awlan;
    bool success = false;
    char *ovs_filter[] = {
        "-",
        SCHEMA_COLUMN(Open_vSwitch, bridges),
        SCHEMA_COLUMN(Open_vSwitch, manager_options),
        SCHEMA_COLUMN(Open_vSwitch, other_config),
        NULL
    };
    int retval = 0;

    /* Update redirector address from target storage! */
    LOGD("Initializing CM tables "
            "(Init AWLAN_Node device config changes)");
    target_device_config_register(cm2_awlan_state_update_cb);

    // Open_vSwitch
    LOGD("Initializing CM tables "
            "(Init Open_vSwitch table)");
    memset(&ovs, 0, sizeof(ovs));
    success = ovsdb_table_upsert_f(&table_Open_vSwitch, &ovs,
                                                        false, ovs_filter);
    if (!success) {
        LOGE("Initializing CM tables "
             "(Failed to setup OvS table)");
        retval = -1;
    }

    // Manager
    LOGD("Initializing CM tables "
            "(Init OvS.Manager table)");
    memset(&manager, 0, sizeof(manager));
    manager.inactivity_probe = 30000;
    manager.inactivity_probe_exists = true;
    STRSCPY(manager.target, "");
    success = ovsdb_table_upsert_with_parent_where(&table_Manager,
            NULL, &manager, false, NULL,
            SCHEMA_TABLE(Open_vSwitch), NULL,
            SCHEMA_COLUMN(Open_vSwitch, manager_options));
    if (!success) {
        LOGE("Initializing CM tables "
                     "(Failed to setup Manager table)");
        retval = -1;
    }

    // SSL
    LOGD("Initializing CM tables "
            "(Init OvS.SSL table)");
    memset(&ssl, 0, sizeof(ssl));
    STRSCPY(ssl.ca_cert, target_tls_cacert_filename());
    STRSCPY(ssl.certificate, target_tls_mycert_filename());
    STRSCPY(ssl.private_key, target_tls_privkey_filename());
    success = ovsdb_table_upsert_with_parent(&table_SSL,
            &ssl, false, NULL,
            SCHEMA_TABLE(Open_vSwitch), NULL,
            SCHEMA_COLUMN(Open_vSwitch, ssl));
    if (!success) {
        LOGE("Initializing CM tables "
                     "(Failed to setup SSL table)");
        retval = -1;
    }

    // AWLAN_Node
    g_state.min_backoff = CONFIG_CM2_OVS_MIN_BACKOFF;
    g_state.max_backoff = CONFIG_CM2_OVS_MAX_BACKOFF;
    LOGD("Initializing CM tables "
         "(Init AWLAN_Node table)");
    memset(&awlan, 0, sizeof(awlan));
    awlan.min_backoff = g_state.min_backoff;
    awlan.max_backoff = g_state.max_backoff;
    char *filter[] = { "+",
                       SCHEMA_COLUMN(AWLAN_Node, min_backoff),
                       SCHEMA_COLUMN(AWLAN_Node, max_backoff),
                       NULL };
    success = ovsdb_table_update_where_f(&table_AWLAN_Node, NULL, &awlan, filter);
    if (!success) {
        LOGE("Initializing CM tables "
             "(Failed to setup AWLAN_Node table)");
        retval = -1;
    }
    return retval;
}

/* Connection_Manager_Uplink table is a main table of Connection Manager for
 * setting configuration and keep information about link state.
 * Configuration/state columns set by NM/WM/Cloud:
 *   if_name  - name of ready interface for using
 *   if_type  - type of interface
 *   priority - link priority during link selection
 *   loop     - loop detected on specific interface
 * State columns set only by CM:
 *   Link states: has_L2, has_L3, is_used
 *   Link stability counters: unreachable_link_counter, unreachable_router_counter,
 *                            unreachable_cloud_counter, unreachable_internet_counter
 *   NTP state: ntp state
 */

int cm2_ovsdb_init(void)
{
    LOGI("Initializing CM tables");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(Open_vSwitch);
    OVSDB_TABLE_INIT_NO_KEY(Manager);
    OVSDB_TABLE_INIT_NO_KEY(SSL);
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_INIT(Wifi_Master_State, if_name);
    OVSDB_TABLE_INIT(Wifi_Inet_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Inet_State, if_name);
    OVSDB_TABLE_INIT(Wifi_VIF_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_VIF_State, if_name);
    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);
    OVSDB_TABLE_INIT_NO_KEY(AW_Bluetooth_Config);
    OVSDB_TABLE_INIT_NO_KEY(Port);
    OVSDB_TABLE_INIT_NO_KEY(Bridge);
    OVSDB_TABLE_INIT_NO_KEY(IP_Interface);
    OVSDB_TABLE_INIT_NO_KEY(IPv6_Address);
    OVSDB_TABLE_INIT_NO_KEY(DHCPv6_Client);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Route_State);
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(AWLAN_Node, false);

    // Callback for EXTENDER
    if (cm2_is_extender()) {
        OVSDB_TABLE_MONITOR(Wifi_Master_State, false);
        OVSDB_TABLE_MONITOR(Connection_Manager_Uplink, false);
        OVSDB_TABLE_MONITOR(Wifi_Inet_State, false);
        OVSDB_TABLE_MONITOR(Bridge, false);
        OVSDB_TABLE_MONITOR(IP_Interface, false);
        OVSDB_TABLE_MONITOR(Wifi_Route_State, false);
    }

    char *filter[] = {"-", "_version", SCHEMA_COLUMN(Manager, status), NULL};
    OVSDB_TABLE_MONITOR_F(Manager, filter);

    // Initialize OvS tables
    if (cm2_ovsdb_init_tables())
    {
        LOGE("Initializing CM tables "
             "(Failed to setup tables)");
        return -1;
    }

    return 0;
}
