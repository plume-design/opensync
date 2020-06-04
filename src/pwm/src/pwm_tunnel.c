/*
* Copyright (c) 2020, Charter, Inc.
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

#include <ovsdb_table.h>
#include <ovsdb_sync.h>
#include <log.h>
#include <const.h>
#include <schema.h>
#include <string.h>

#include <pwm_tunnel.h>
#include <pwm_firewall.h>
#include <pwm_bridge.h>
#include <pwm_utils.h>
#include <pwm_ping.h>
#include <pwm.h>
#include <pwm_ovsdb.h>
#include <pwm_wifi.h>


#define MODULE_ID LOG_MODULE_ID_MISC

static bool pwm_tunnel_created = false;

static bool pwm_get_local_ipv4_addr(const char *ifname, char *addr, size_t size)
{
	bool errcode;
	struct schema_Wifi_Inet_State rec;

	if (!ifname || !addr || !size) {
		LOGE("Get local IPv4 address: invalid argument");
		return false;
	}

	errcode = ovsdb_table_select_one(&table_Wifi_Inet_State,
			SCHEMA_COLUMN(Wifi_Inet_State, if_name), ifname, &rec);
	if (!errcode) {
		LOGE("Get local IPv4 address: cannot get OVSDB entry");
		return false;
	}

	strncpy(addr, rec.inet_addr, size - 1);
	addr[size - 1] = '\0';
	return true;
}

static bool pwm_get_local_ipv6_addr(const char *ifname, char *addr, size_t size)
{
	int errcode;
	struct schema_IP_Interface ipintf;
	json_t *where;
	struct schema_IPv6_Address ipv6addr;
	int i;
	char addr_full_str[INET6_ADDRSTRLEN + 10];
	const char *addr_str = NULL;

	if (!ifname || !addr || !size) {
		LOGE("Get local IPv6 address: invalid argument");
		return false;
	}

	errcode = ovsdb_table_select_one(&table_IP_Interface,
			SCHEMA_COLUMN(IP_Interface, if_name), ifname, &ipintf);
	if (!errcode) {
		LOGE("Get local IPv6 address: cannot get IP interface OVSDB entry");
		return false;
	}

	for (i = 0; (i < ipintf.ipv6_addr_len) && (i < ((int) ARRAY_SIZE(ipintf.ipv6_addr))); i++) {
		where = ovsdb_where_uuid("_uuid", ipintf.ipv6_addr[i].uuid);
		if (!where) {
			LOGE("Get local IPv6 address: cannot create OVSDB filter");
			return false;
		}
		errcode = ovsdb_table_select_one_where(&table_IPv6_Address, where, &ipv6addr);
		if (!errcode) {
			LOGE("Get local IPv6 address: cannot get IPv6 address OVSDB entry");
			return false;
		} else if (!ipv6addr.enable) {
			continue;
		} else if (pwm_is_ipv6_gua_addr(ipv6addr.address)) {
			strncpy(addr_full_str, ipv6addr.address, sizeof(addr_full_str) - 1);
			addr_full_str[sizeof(addr_full_str) - 1] = '\0';
			addr_str = strtok(addr_full_str, "/");
			if (!addr_str) {
				LOGE("Get local IPv6 address: invalid address %s", ipv6addr.address);
				continue;
			}
			strncpy(addr, addr_str, size - 1);
			addr[size - 1] = '\0';
			return true;
		}
	}
	return false;
}

static bool pwm_get_local_addr(const char *ifname, char* addr, size_t size, int family)
{
	bool errcode = false;

	switch (family) {
	case AF_INET:
		errcode = pwm_get_local_ipv4_addr(ifname, addr, size);
		break;

	case AF_INET6:
		errcode = pwm_get_local_ipv6_addr(ifname, addr, size);
		break;

	default:
		LOGE("Get local GRE tunnel address: invalid family %d", family);
		break;
	}
	return errcode;
}

static bool pwm_create_tunnel_ovsdb_entry(const char *remote_endpoint)
{
	bool errcode;
	struct schema_Wifi_Inet_Config entry;
	char local_endpoint[INET6_ADDRSTRLEN];
	int family;

	if (!remote_endpoint || !remote_endpoint[0]) {
		LOGE("Create Public WiFi tunnel: invalid parameter");
		return false;
	}

	memset(&entry, 0, sizeof(entry));
	schema_Wifi_Inet_Config_mark_all_present(&entry);
	SCHEMA_SET_INT(entry.enabled, true);
	SCHEMA_SET_STR(entry.if_name, PWM_GRE_IF_NAME);
	SCHEMA_SET_INT(entry.network, 1);
	SCHEMA_SET_STR(entry.gre_ifname, PWM_WAN_IF_NAME);
	SCHEMA_SET_STR(entry.gre_remote_inet_addr, remote_endpoint);
	SCHEMA_SET_STR(entry.ip_assign_scheme, "none");
	SCHEMA_SET_INT(entry.mtu, 1562);

	family = pwm_get_addr_family(remote_endpoint);
	switch (family) {
	case AF_INET:
		SCHEMA_SET_STR(entry.if_type, "gre");
		break;
	case AF_INET6:
		SCHEMA_SET_STR(entry.if_type, "gre6");
		break;
	default:
		LOGE("Create Public WiFi tunnel: invalid tunnel family %d", family);
		return false;
		break;
	}

	errcode = pwm_get_local_addr(entry.gre_ifname, local_endpoint, sizeof(local_endpoint), family);
	if (!errcode || !local_endpoint[0]) {
		LOGE("Create Public WiFi tunnel: get local IP address failed");
		return false;
	}
	SCHEMA_SET_STR(entry.gre_local_inet_addr, local_endpoint);

	errcode = ovsdb_table_insert(&table_Wifi_Inet_Config, &entry);
	if (!errcode ) {
		LOGE("Create Public WiFi tunnel: insert OVSDB entry failed");
		return false;
	}
	return true;
}

static bool pwm_delete_tunnel_ovsdb_entry(void)
{
	bool errcode;
	json_t *where;

	where = ovsdb_where_simple_typed(SCHEMA_COLUMN(Wifi_Inet_Config, if_name),
			PWM_GRE_IF_NAME, OCLM_STR);
	if (!where) {
		LOGE("Delete Public WiFi tunnel: cannot create filter for %s", PWM_GRE_IF_NAME);
		return false;
	}

	errcode = ovsdb_table_delete_where(&table_Wifi_Inet_Config, where);
	if (!errcode) {
			LOGE("Delete Public WiFi tunnel: delete OVSDB entry failed");
			return false;
	}
	return true;
}

static bool pwm_create_tunnel(const char *remote_endpoint)
{
	bool errcode;

	if (pwm_tunnel_created) {
		return true;
	}

	errcode = pwm_create_tunnel_ovsdb_entry(remote_endpoint);
	if (!errcode) {
		LOGE("Create Public WiFi tunnel: add OVSDB entry failed");
		return false;
	}

	errcode = pwm_add_port_to_bridge(PWM_GRE_IF_NAME, PWM_PORT_TYPE_OPERATOR);
	if (!errcode) {
		LOGE("Create Public WiFi tunnel: add %s port to the Public WiFi bridge failed", PWM_GRE_IF_NAME);
		return false;
	}

	errcode = pwm_add_fw_rules(remote_endpoint);
	if (!errcode) {
		LOGE("Create Public WiFi tunnel: add firewall rules failed");
		return false;
	}

	pwm_tunnel_created = true;
	LOGD("Public WiFi tunnel added for %s", remote_endpoint);
	return true;
}

static bool pwm_delete_tunnel(void)
{
	bool errcode;

	if (!pwm_tunnel_created) {
		return true;
	}

	errcode = pwm_del_fw_rules();
	if (!errcode) {
		LOGE("Delete Public WiFi tunnel: delete firewall rules failed");
		return false;
	}

	errcode = pwm_delete_port_from_bridge(PWM_GRE_IF_NAME);
	if (!errcode) {
		LOGE("Delete Public WiFi tunnel: delete %s port from the Public WiFi bridge failed", PWM_GRE_IF_NAME);
		return false;
	}

	errcode = pwm_delete_tunnel_ovsdb_entry();
	if (!errcode) {
		LOGE("Delete Public WiFi tunnel: delete OVSDB entry failed");
		return false;
	}

	pwm_tunnel_created = false;
	LOGD("Public WiFi tunnel deleted");
	return true;
}

bool pwm_update_tunnel( struct schema_PublicWiFi* m_conf )
{
	bool errcode;
	int inet = 0;
	if (m_conf && m_conf->enable && m_conf->remote_endpoint[0]) {
		errcode = pwm_create_tunnel(m_conf->remote_endpoint);
		if (!errcode) {
			LOGE("Update Public WiFi tunnel: create tunnel failed");
			return false;
		}

		inet = pwm_get_addr_family(m_conf->remote_endpoint);
		errcode = init_keep_alive(inet,m_conf->remote_endpoint);
		if (!errcode) {
			LOGE("Update Public WiFi tunnel: keep alive initialize failed ");
			return false;
		}

		if(strlen(m_conf->ssid))
		{
			pwm_set_wifi( m_conf->ssid,
					        m_conf->radius_server_ip_address,
						    m_conf->radius_server_ip_port,
						    m_conf->radius_server_password );
		}

	} else {
		pwm_del_wifi();

		if (!stop_keep_alive()) {
			LOGE("Update Public WiFi tunnel: stop keep alive failed ");
		}

		errcode = pwm_delete_tunnel();
		if (!errcode) {
			LOGE("Update Public WiFi tunnel: delete tunnel failed");
			return false;
		}

	}
	return true;
}

