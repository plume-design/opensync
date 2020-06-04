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

#include <pwm_firewall.h>
#include <pwm_ovsdb.h>
#include <pwm_utils.h>
#include <log.h>
#include <schema.h>
#include <ovsdb_table.h>
#include <ovsdb_sync.h>
#include <ovsdb.h>
#include <string.h>
#include <stdio.h>

#define MODULE_ID LOG_MODULE_ID_MISC

static bool pwm_add_fw_rule(const char *name, const char *chain, const char *endpoint)
{
	bool errcode;
	struct schema_Netfilter entry;
	int family;
	char target[1024];

	if (!name || !chain || !endpoint) {
		LOGE("Add firewall rule failed: invalid arguments");
		return false;
	}

	memset(&entry, 0, sizeof(entry));
	SCHEMA_SET_INT(entry.enable, true);
	SCHEMA_SET_STR(entry.name, name);
	SCHEMA_SET_STR(entry.table, "filter");
	SCHEMA_SET_STR(entry.chain, chain);
	SCHEMA_SET_INT(entry.priority, 128);
	SCHEMA_SET_STR(entry.target, "ACCEPT");

	family = pwm_get_addr_family(endpoint);
	switch (family) {
	case AF_INET:
		SCHEMA_SET_STR(entry.protocol, "ipv4");
		break;

	case AF_INET6:
		SCHEMA_SET_STR(entry.protocol, "ipv6");
		break;

	default:
		LOGE("Add Public WiFi firewall rule: invalid family %d", family);
		return false;
		break;
	}

	if (!strncmp(chain, "INPUT", sizeof("INPUT"))) {
		snprintf(target, sizeof(target) - 1, "-p 47 -s %s", endpoint);
		target[sizeof(target) - 1] = '\0';
	} else if (!strncmp(chain, "OUTPUT", sizeof("INPUT"))) {
		snprintf(target, sizeof(target) - 1, "-p 47 -d %s", endpoint);
		target[sizeof(target) - 1] = '\0';
	} else {
		LOGE("Add Public WiFi firewall rule: invalid chain %s", chain);
		return false;
	}
	SCHEMA_SET_STR(entry.rule, target);

	errcode = ovsdb_table_insert(&table_Netfilter, &entry);
	if (!errcode) {
		LOGE("Add Public WiFi firewall rule: insert OVSDB entry failed");
		return false;
	}
	return true;
}

static bool pwm_del_fw_rule(const char *name)
{
	bool errcode;
	json_t *where;

	if (!name) {
		LOGE("Delete Public WiFi firewall rule: invalid argument");
		return false;
	}

	where = ovsdb_where_simple_typed(SCHEMA_COLUMN(Netfilter, name), name, OCLM_STR);
	if (!where) {
		LOGE("Delete Public WiFi firewall rule: create filter on %s failed", name);
		return false;
	}

	errcode = ovsdb_table_delete_where(&table_Netfilter, where);
	if (!errcode) {
		LOGE("Delete Public WiFi firewall rule: delete OVSDB entry failed");
		return false;
	}

	return true;
}

bool pwm_add_fw_rules(const char *endpoint)
{
	bool errcode;

	errcode = pwm_add_fw_rule(PWM_NETFILER_GRE_RX, "INPUT", endpoint);
	if (!errcode) {
		LOGE("Add Public WiFi firewall rules: add %s rule failed", PWM_NETFILER_GRE_RX);
		return false;
	}

	errcode = pwm_add_fw_rule(PWM_NETFILER_GRE_TX, "OUTPUT", endpoint);
	if (!errcode) {
		LOGE("Add Public WiFi firewall rules: add %s rule failed", PWM_NETFILER_GRE_TX);
		return false;
	}

	LOGD("Public WiFi firewall rules added for %s", endpoint);
	return true;
}

bool pwm_del_fw_rules(void)
{
	bool errcode;

	errcode = pwm_del_fw_rule(PWM_NETFILER_GRE_RX);
	if (!errcode) {
		LOGE("Delete Public WiFi firewall rules: add %s rule failed", PWM_NETFILER_GRE_RX);
		return false;
	}

	errcode = pwm_del_fw_rule(PWM_NETFILER_GRE_TX);
	if (!errcode) {
		LOGE("Delete Public WiFi firewall rules: add %s rule failed", PWM_NETFILER_GRE_TX);
		return false;
	}

	LOGD("Public WiFi firewall rules deleted");
	return true;
}

/******************************************************************************/
/* Add Radius Server Firewall rules                                           */
/******************************************************************************/
bool pwm_add_rs_fw_rules(const char *rs_ip_address, const char *rs_ip_port)
{
	bool						errcode;
	struct schema_Netfilter	entry;
	char						target[1024];

	LOGD("Public WiFi RADIUS SERVER FW ADD: ip_ad=[%s] ip_port=[%s]", rs_ip_address, rs_ip_port);

	if( (rs_ip_address    == NULL) ||
		(rs_ip_port       == NULL) ||
		(rs_ip_address[0] == 0)    ||
		(rs_ip_port[0]    == 0)       )
	{
		LOGE("Public WiFi RADIUS SERVER FW ADD: invalid arguments: ip_ad=[%s] ip_port=[%s]", rs_ip_address, rs_ip_port);
		return( false );
	}

	// table Netfilter
	// Based on Simon The INPUT Direction is not neaded because
	// -A INPUT -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT

	memset(&entry, 0, sizeof(entry));
	SCHEMA_SET_INT(entry.enable,    true);
	SCHEMA_SET_STR(entry.name,      PWM_NETFILER_RS_TX);
	SCHEMA_SET_STR(entry.table,    "filter");
	SCHEMA_SET_STR(entry.chain,    "OUTPUT");
	SCHEMA_SET_INT(entry.priority,  128);
	SCHEMA_SET_STR(entry.target,   "ACCEPT");
	SCHEMA_SET_STR(entry.protocol, "ipv4");

	snprintf(target, (sizeof(target)-1), "-d %s/32 -p udp -m udp --dport %s", rs_ip_address, rs_ip_port );
	LOGD("Public WiFi RADIUS SERVER FW ADD: TX target=[%s]", target);
	SCHEMA_SET_STR(entry.rule, target);

	errcode = ovsdb_table_insert(&table_Netfilter, &entry);
	if( !errcode )
	{
		LOGE("Public WiFi RADIUS SERVER FW ADD: insert OVSDB entry failed");
		return( false );
	}

	return( true );
}

/******************************************************************************/
/* DEL Radius Server Firewall rules                                           */
/******************************************************************************/
bool pwm_del_rs_fw_rules( void )
{
	bool		errcode;
	const char	*name;
	json_t		*where;

	LOGD("Public WiFi RADIUS SERVER FW DELL");

	name  = PWM_NETFILER_RS_TX;
	where = ovsdb_where_simple_typed(SCHEMA_COLUMN(Netfilter, name), name, OCLM_STR);
	if( where != NULL )
	{
		errcode = ovsdb_table_delete_where( &table_Netfilter, where );
		if( !errcode )
		{
			LOGE("Public WiFi RADIUS SERVER FW DELL: delete OVSDB entry failed");
		}
	}
	else
	{
		LOGE("Public WiFi RADIUS SERVER FW DELL: delete OVSDB entry failed");
	}

	return( true );
}

/******************************************************************************/
/* Add pre routing packets to queue firewall rule                             */
/******************************************************************************/
bool pwm_add_pktroute_to_q_rule(const char *name, const char *intf)
{
	bool						errcode;
	struct schema_Netfilter	entry;
	char						rule[256];

	if( (NULL == name ) ||
		(NULL == intf ) )
	{
		LOGE("Public WiFi DHCP opt82 rule: invalid arguments");
		return( false );
	}

	memset(&entry, 0, sizeof(entry));

	SCHEMA_SET_INT(entry.enable, true);
	SCHEMA_SET_STR(entry.name, name);
	SCHEMA_SET_STR(entry.table, "mangle");
	SCHEMA_SET_STR(entry.chain, "PREROUTING");
	SCHEMA_SET_INT(entry.priority, 128);
	SCHEMA_SET_STR(entry.target, "NFQUEUE --queue-num 0");
	SCHEMA_SET_STR(entry.protocol, "ipv4");

	snprintf(rule, (sizeof(rule)-1), "-i %s -p udp --dport 67 ",intf );

	LOGD("Public WiFi DHCP packet route rule: rule; %s", rule);
	SCHEMA_SET_STR(entry.rule, rule);

	errcode = ovsdb_table_insert(&table_Netfilter, &entry);
	if( !errcode )
	{
		LOGE("Public WiFi DHCP packet route rule add: insert OVSDB entry failed");
		return( false );
	}
    return true;
}

/******************************************************************************/
/* Delete pre routing packets to queue firewall rule                          */
/******************************************************************************/
bool pwm_del_pktroute_to_q_rule( const char *name )
{
	bool		errcode;
	json_t		*where;

	LOGI("Public WiFi: %s,",__func__);

	where = ovsdb_where_simple_typed(SCHEMA_COLUMN(Netfilter, name), name, OCLM_STR);
	if( where != NULL )
	{
		errcode = ovsdb_table_delete_where( &table_Netfilter, where );
		if( !errcode )
		{
			LOGE("Public WiFi HCP packet route rule del: delete OVSDB entry failed");
			return false;
		}
	}
	else
	{
		LOGE("Public WiFi RHCP packet route rule: delete OVSDB entry failed");
		return false;
	}

	return( true );
}

