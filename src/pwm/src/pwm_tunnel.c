/*
 * Copyright (c) 2021, Sagemcom.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>

#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "log.h"
#include "const.h"
#include "schema.h"

#include "pwm_tunnel.h"
#include "pwm_firewall.h"
#include "pwm_bridge.h"
#include "pwm_utils.h"
#include "pwm_ping.h"
#include "pwm_ovsdb.h"
#include "pwm_wifi.h"


#define MODULE_ID LOG_MODULE_ID_MISC
#define CMD_LEN 512
#define PWM_TUNNEL_CREATED_OVSDB            0x01
#define PWM_TUNNEL_CREATED_VLAN_OVSDB       0x02
#define PWM_TUNNEL_CREATED_ADD_TO_BRIDGE    0x04
#define PWM_TUNNEL_CREATED_FW_RULES         0x08
#define PWM_GRE_VLAN_IFNAME 64

extern struct ovsdb_table table_Wifi_Inet_State;
extern struct ovsdb_table table_IPv6_Address;
extern struct ovsdb_table table_Wifi_Inet_Config;
extern struct ovsdb_table table_IP_Interface;

static int  g_pwm_tunnel_created = 0;
static char g_pwm_tunnel_ifname[PWM_GRE_VLAN_IFNAME];
static char g_pwm_tunnel_vlan_ifname[PWM_GRE_VLAN_IFNAME];
static char g_pwm_tunnel_far_ip[INET6_ADDRSTRLEN];

static bool pwm_tunnel_get_local_ipv4_addr(const char *ifname, char *addr, size_t size)
{
    bool errcode;
    struct schema_Wifi_Inet_State rec;

    if (!ifname || !addr || !size) {
        LOGE("Get local IPv4 address: invalid argument");
        return false;
    }

    errcode = ovsdb_table_select_one(&table_Wifi_Inet_State,
                                     SCHEMA_COLUMN(Wifi_Inet_State, if_name),
                                     ifname, &rec);
    if (!errcode) {
        LOGE("Get local IPv4 address: cannot get OVSDB entry");
        return false;
    }

    strscpy(addr, rec.inet_addr, size);

    return true;
}

static bool pwm_tunnel_get_local_ipv6_addr(const char *ifname, char *addr, size_t size)
{
    int errcode;
    struct schema_IP_Interface ipintf;
    json_t *where;
    struct schema_IPv6_Address ipv6addr;
    int i;
    char *addr_str = NULL;

    if (!ifname || !addr || !size) {
        LOGE("Get local IPv6 address: invalid argument");
        return false;
    }

    errcode = ovsdb_table_select_one(&table_IP_Interface,
                                     SCHEMA_COLUMN(IP_Interface, if_name),
                                     ifname, &ipintf);
    if (!errcode) {
        LOGE("Get local IPv6 address: cannot get IP interface OVSDB entry");
        return false;
    }

    for (i = 0;
        (i < ipintf.ipv6_addr_len) && (i < ((int) ARRAY_SIZE(ipintf.ipv6_addr)));
         i++)
    {
        where = ovsdb_where_uuid("_uuid", ipintf.ipv6_addr[i].uuid);
        if (!where) {
            LOGE("Get local IPv6 address: cannot create OVSDB filter");
            return false;
        }
        errcode = ovsdb_table_select_one_where(&table_IPv6_Address, where, &ipv6addr);
        if (!errcode)
        {
            LOGE("Get local IPv6 address: cannot get IPv6 address OVSDB entry");
            return false;
        }
        else if (!ipv6addr.enable)
        {
            continue;
        }
        else
        {
            addr_str = pwm_utils_is_ipv6_gua_addr(ipv6addr.address);
            if (addr_str == NULL)
            {
                continue;
            }
            strscpy(addr, addr_str, size);
            free(addr_str);
            return true;
        }
    }
    free(addr_str);

    return false;
}

static bool pwm_tunnel_get_local_addr(const char *ifname, char *addr, size_t size, int family)
{
    bool errcode = false;

    switch (family)
    {
        case AF_INET:
            errcode = pwm_tunnel_get_local_ipv4_addr(ifname, addr, size);
            break;

        case AF_INET6:
            errcode = pwm_tunnel_get_local_ipv6_addr(ifname, addr, size);
            break;

        default:
            LOGE("Get local GRE tunnel address: invalid family %d", family);
            break;
    }

    return errcode;
}

static bool pwm_tunnel_create_gre_ovsdb_entry(const char *remote_endpoint, const char *gre_if_name)
{
    bool success = false;
    struct schema_Wifi_Inet_Config entry;
    char local_endpoint[INET6_ADDRSTRLEN];
    int family;

    if (!remote_endpoint || !remote_endpoint[0]) {
        LOGE("Create PWM tunnel: invalid parameter - remote_endpoint");
        return false;
    }

    if (!gre_if_name || !gre_if_name[0]) {
        LOGE("Create PWM tunnel: invalid parameter - gre_if_name");
        return false;
    }

    MEM_SET(&entry, 0, sizeof(entry));
    schema_Wifi_Inet_Config_mark_all_present(&entry);
    SCHEMA_SET_INT(entry.enabled, true);
    SCHEMA_SET_STR(entry.if_name, gre_if_name);
    SCHEMA_SET_INT(entry.network, true);
    SCHEMA_SET_STR(entry.gre_ifname, CONFIG_PWM_WAN_IF_NAME);
    SCHEMA_SET_STR(entry.gre_remote_inet_addr, remote_endpoint);
    SCHEMA_SET_STR(entry.ip_assign_scheme, "none");
    SCHEMA_SET_INT(entry.mtu, 1562);

    family = pwm_utils_get_addr_family(remote_endpoint);
    switch (family)
    {
        case AF_INET:
            SCHEMA_SET_STR(entry.if_type, "gre");
            break;
        case AF_INET6:
            SCHEMA_SET_STR(entry.if_type, "gre6");
            break;
        default:
            LOGE("Create PWM tunnel: invalid tunnel family %d", family);
            return false;
            break;
    }

    success = pwm_tunnel_get_local_addr(entry.gre_ifname, local_endpoint, sizeof(local_endpoint), family);
    if ((success != true) || !local_endpoint[0]) {
        LOGE("Create PWM tunnel: get local IP address failed");
        return false;
    }
    SCHEMA_SET_STR(entry.gre_local_inet_addr, local_endpoint);

    success = ovsdb_table_insert(&table_Wifi_Inet_Config, &entry);
    if (success != true) {
        LOGE("Create PWM tunnel: insert OVSDB entry failed");
        return false;
    }

    return true;
}

static bool pwm_tunnel_create_gre_vlan_ovsdb_entry(const char *parent_ifname, const char *gre_vlan_if_name, const int gre_vlan_id)
{
    bool success = false;
    struct schema_Wifi_Inet_Config entry;

    if (!parent_ifname || !parent_ifname[0]) {
        LOGE("Create PWM tunnel: invalid parameter - parent_ifname");
        return false;
    }

    if (!gre_vlan_if_name || !gre_vlan_if_name[0]) {
        LOGE("Create PWM tunnel: invalid parameter - gre_vlan_if_name");
        return false;
    }

    if (gre_vlan_id < 1) {
        LOGE("Create PWM tunnel: invalid parameter - gre_vlan_id=[%d]", gre_vlan_id);
        return false;
    }

    LOGD("Create PWM tunnel: setting gre_vlan_if_name=[%s] gre_vlan_id=[%d]", gre_vlan_if_name, gre_vlan_id );

    MEM_SET(&entry, 0, sizeof(entry));
    schema_Wifi_Inet_Config_mark_all_present(&entry);
    SCHEMA_SET_INT(entry.enabled, true);
    SCHEMA_SET_STR(entry.if_type, "vlan");
    SCHEMA_SET_STR(entry.parent_ifname, parent_ifname);
    SCHEMA_SET_STR(entry.if_name, gre_vlan_if_name);
    SCHEMA_SET_INT(entry.vlan_id, gre_vlan_id);
    SCHEMA_SET_INT(entry.network, true);

    success = ovsdb_table_insert(&table_Wifi_Inet_Config, &entry);
    if (success != true) {
        LOGE("Create PWM tunnel: insert OVSDB entry failed");
        return false;
    }

    return true;
}

static bool pwm_tunnel_delete_ovsdb_entry(const char *if_name_to_delete)
{
    bool errcode;
    json_t *where;

    if (!if_name_to_delete || !if_name_to_delete[0]) {
        LOGE("Delete PWM tunnel: invalid parameter - parent_ifname");
        return false;
    }

    where = ovsdb_where_simple_typed(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), if_name_to_delete, OCLM_STR);
    if (!where) {
        LOGE("Delete PWM tunnel: cannot create filter for %s", g_pwm_tunnel_ifname);
        return false;
    }

    errcode = ovsdb_table_delete_where(&table_Wifi_Inet_Config, where);
    if (!errcode) {
        LOGE("Delete PWM tunnel: delete OVSDB entry failed");
        return false;
    }

    return true;
}

static bool pwm_tunnel_wait_create_ovsdb_entry(const char *wait_if_name)
{
    bool errcode;
    int retry = 0;
    struct schema_Wifi_Inet_State rec;

    while (retry < 5)
    {
        errcode = ovsdb_table_select_one(&table_Wifi_Inet_State,
                                         SCHEMA_COLUMN(Wifi_Inet_State, if_name),
                                         wait_if_name, &rec);
        if (!errcode) {
            LOGI("failed to get if_name [%s] interface from Wifi Inet State table", wait_if_name);
        }
        else if (rec.enabled == false) {
            LOGI("Create PWM tunnel : if_name [%s] is still disable", wait_if_name);
        }
        else if (rec.enabled == true) {
            return true;
        }
        retry++;
        sleep(1);
    }

    return false;
}

static bool pwm_tunnel_create(struct schema_Public_Wifi_Config *m_conf, char *remote_endpoint)
{
    bool errcode;

    if (g_pwm_tunnel_created) {
        return true;
    }

    errcode = pwm_tunnel_create_gre_ovsdb_entry(remote_endpoint, m_conf->tunnel_ifname);
    if (!errcode) {
        LOGE("Create PWM tunnel: add OVSDB entry failed");
        g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN_CREATION_ERROR);
        return false;
    }
    STRSCPY_WARN(g_pwm_tunnel_ifname, m_conf->tunnel_ifname);
    g_pwm_tunnel_created |= PWM_TUNNEL_CREATED_OVSDB;

    errcode = pwm_tunnel_wait_create_ovsdb_entry(g_pwm_tunnel_ifname);
    if (errcode == false)
    {
        LOGE("Create PWM tunnel: timeout has occured");
        g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN_CREATION_ERROR);
        return false;
    }

    snprintf(g_pwm_tunnel_vlan_ifname, (sizeof(g_pwm_tunnel_vlan_ifname) - 4),
             "%s.%d",
             g_pwm_tunnel_ifname,
             m_conf->vlan_id);

    errcode = pwm_tunnel_create_gre_vlan_ovsdb_entry(g_pwm_tunnel_ifname,
                                                     g_pwm_tunnel_vlan_ifname,
                                                     m_conf->vlan_id);
    if (!errcode) {
        LOGE("Create PWM tunnel: add vlan OVSDB entry failed");
        g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN_CREATION_ERROR);
        return false;
    }
    g_pwm_tunnel_created |= PWM_TUNNEL_CREATED_VLAN_OVSDB;

    errcode = pwm_tunnel_wait_create_ovsdb_entry(g_pwm_tunnel_vlan_ifname);
    if (errcode == false)
    {
        LOGE("Create PWM tunnel: vlan timeout has occured");
        g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN_CREATION_ERROR);
        return false;
    }

    errcode = pwm_bridge_add_port(g_pwm_tunnel_vlan_ifname, PWM_PORT_TYPE_OPERATOR, m_conf->gre_of_port);
    if (!errcode)
    {
        LOGE("Create PWM tunnel: add %s port to the PWM bridge failed", g_pwm_tunnel_ifname);
        g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN_CREATION_ERROR);
        return false;
    }
    g_pwm_tunnel_created |= PWM_TUNNEL_CREATED_ADD_TO_BRIDGE;

    errcode = pwm_firewall_add_rules(remote_endpoint);
    if (!errcode)
    {
        LOGE("Create PWM tunnel: add firewall rules failed");
        g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN_CREATION_ERROR);
        return false;
    }
    g_pwm_tunnel_created |= PWM_TUNNEL_CREATED_FW_RULES;

    LOGD("PWM tunnel added for %s", remote_endpoint);

    return true;
}

static bool pwm_tunnel_delete(void)
{
    bool errcode;

    LOGD("Delete PWM tunnel: pwm_tunnel_created=[0x%08X]", g_pwm_tunnel_created);

    if (g_pwm_tunnel_created & PWM_TUNNEL_CREATED_FW_RULES)
    {
        errcode = pwm_firewall_del_rules();
        if (!errcode) {
            LOGE("Delete PWM tunnel: delete firewall rules failed");
        }
    }

    if (g_pwm_tunnel_created & PWM_TUNNEL_CREATED_ADD_TO_BRIDGE)
    {
        errcode = pwm_bridge_delete_port(g_pwm_tunnel_vlan_ifname);
        if (!errcode) {
            LOGE("Delete PWM tunnel: delete %s port from the PWM bridge failed", g_pwm_tunnel_vlan_ifname);
        }
    }

    if (g_pwm_tunnel_created & PWM_TUNNEL_CREATED_VLAN_OVSDB)
    {
        errcode = pwm_tunnel_delete_ovsdb_entry(g_pwm_tunnel_vlan_ifname);
        if (!errcode) {
            LOGE("Delete PWM tunnel: delete OVSDB entry [%s] failed", g_pwm_tunnel_vlan_ifname);
        }
    }

    if (g_pwm_tunnel_created & PWM_TUNNEL_CREATED_OVSDB)
    {
        errcode = pwm_tunnel_delete_ovsdb_entry(g_pwm_tunnel_ifname);
        if (!errcode) {
            LOGE("Delete PWM tunnel: delete OVSDB entry [%s] failed", g_pwm_tunnel_ifname);
        }
    }

    MEM_SET(g_pwm_tunnel_vlan_ifname, 0, sizeof(g_pwm_tunnel_vlan_ifname));
    MEM_SET(g_pwm_tunnel_ifname, 0, sizeof(g_pwm_tunnel_ifname));

    g_pwm_tunnel_created = 0;
    LOGD("PWM tunnel deleted");

    return true;
}

void pwm_tunnel_far_ip_set(char *ip)
{
    STRSCPY_WARN(g_pwm_tunnel_far_ip, ip);
}

char *pwm_tunnel_far_ip_get(void)
{
    return g_pwm_tunnel_far_ip;
}

void pwm_tunnel_far_ip_clean(void)
{
    MEM_SET(g_pwm_tunnel_far_ip, 0, sizeof(g_pwm_tunnel_far_ip));
}

bool pwm_tunnel_update(struct schema_Public_Wifi_Config *m_conf)
{
    bool errcode;
    int inet = 0;

    if (m_conf && m_conf->enabled)
    {
        LOGD("Update PWM tunnel: creating");

        if (m_conf->gre_endpoint[0])
        {
            g_gre_endpoint = m_conf->gre_endpoint;
            LOGD("Update PWM tunnel: gre endpoint Public_Wifi_Config [%s]", g_gre_endpoint);
        }
        else if (g_pwm_tunnel_far_ip[0])
        {
            g_gre_endpoint = g_pwm_tunnel_far_ip;
            LOGD("Update PWM tunnel: gre endpoint FQDN [%s]", g_gre_endpoint);

        }
        else
        {
            LOGE("Update PWM tunnel: no gre endpoint FQDN avalable");
            g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN_FQDN);
            return false;
        }

        if (pwm_ovsdb_check_remote_endpoint_alive(g_gre_endpoint) == 0)
        {
            g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN_PING);
            LOGE("Update PWM tunnel: PWM endpoint %s not alive", g_gre_endpoint);
            return false;
        }

        errcode = pwm_tunnel_create(m_conf, g_gre_endpoint);
        if (!errcode) {
            LOGE("Update PWM tunnel: create tunnel failed");
            return false;
        }

        if (m_conf->keepalive_interval > 0)
        {
            inet = pwm_utils_get_addr_family(g_gre_endpoint);
            errcode = pwm_ping_init_keep_alive(inet, m_conf->keepalive_interval, g_gre_endpoint);
            if (!errcode) {
                LOGE("Update PWM tunnel: keep alive initialize failed ");
                g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN_PING);
                return false;
            }
        }
        else
        {
            LOGI("Update PWM tunnel: keep alive disabled: interval=[%d]", m_conf->keepalive_interval);
        }

        pwm_wifi_set(m_conf);

        g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_UP);
    }
    else
    {
        LOGD("Update PWM tunnel: deleting");
        pwm_tunnel_far_ip_clean();
        pwm_wifi_disable();
        if (!pwm_ping_stop_keep_alive()) {
            LOGE("Update PWM tunnel: stop keep alive failed ");
        }

        errcode = pwm_tunnel_delete();
        if (!errcode) {
            LOGE("Update PWM tunnel: delete tunnel failed");
        }

        g_tunnel_gre_status_msg = pwm_ovsdb_update_gre_tunnel_status(GRE_tunnel_DOWN);
    }
    return true;
}
