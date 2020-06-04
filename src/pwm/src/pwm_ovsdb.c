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

#include <stdio.h>

#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include <pwm_ovsdb.h>
#include <pwm_utils.h>
#include <pwm_tunnel.h>
#include <pwm_bridge.h>
#include <pwm_wifi.h>
#include <pwm.h>


#include "os.h"

#define ADDRESS_TMP_FILE "/tmp/address"

/**
 * Globals
 */
/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID			LOG_MODULE_ID_OVSDB
#define OPT_WAIT_TIMEOUT 30
#define REM_ENDPOINT_IP_TABLE_SIZE 20

struct ovsdb_table table_PublicWiFi;
struct ovsdb_table table_Wifi_Inet_Config;
struct ovsdb_table table_Wifi_Inet_State;
struct ovsdb_table table_DHCP_Option;
struct ovsdb_table table_IP_Interface;
struct ovsdb_table table_IPv6_Address;
struct ovsdb_table table_Netfilter;
struct ovsdb_table table_DHCPv4_Client;
struct ovsdb_table table_DHCPv6_Client;

static ev_timer			opt_wait_timer;

static bool SNM_H_enable = false;
static char v4_domain_nm[128];
static char v6_domain_nm[128];
static char rem_endpoint_ip_addr_table[REM_ENDPOINT_IP_TABLE_SIZE][INET6_ADDRSTRLEN];

static void set_v6_domain_name(const char *name)
{
	strncpy(v6_domain_nm,name,sizeof(v6_domain_nm)-1);
	v6_domain_nm[sizeof(v6_domain_nm)-1] = '\0';
	return;
}

static void set_v4_domain_name(const char *name)
{
	strncpy(v4_domain_nm,name,sizeof(v4_domain_nm)-1);
	v4_domain_nm[sizeof(v4_domain_nm)-1] = '\0';
	return;
}

static const char * get_v6_domain_name(void)
{
	return v6_domain_nm;
}

static const char * get_v4_domain_name(void)
{
	return v4_domain_nm;
}

static void set_SNM_H_enable(bool val)
{
	SNM_H_enable = val;
	return;
}

bool is_SNM_H_enabled(void)
{
	return SNM_H_enable;
}

static bool is_v4_opt15(int opt ,const char *ver , const char *type)
{
	return (opt == 15 && (0 == strncmp(ver,"v4",sizeof("v4"))) && (0 == strncmp(type,"rx",sizeof("rx"))));

}

static bool is_v6_opt24(int opt ,const char *ver , const char *type)
{
	return (opt == 24 && (0 == strncmp(ver,"v6",sizeof("v6"))) && (0 == strncmp(type,"rx",sizeof("rx"))));
}

static bool resolve_DNS(const char * domain , int family)
{
	int					ret;
	int				i=0;
	struct addrinfo		*res_ptr;
	struct addrinfo		*res_ptr_tmp;
	void				*ptr = NULL;
	struct addrinfo	hints;
    LOGD(" [%s] [%s]", __FUNCTION__, domain);

	memset(&hints, 0, sizeof(struct addrinfo));
	memset(&rem_endpoint_ip_addr_table,'\0',sizeof(rem_endpoint_ip_addr_table[0][0]) * REM_ENDPOINT_IP_TABLE_SIZE * INET6_ADDRSTRLEN);

	hints.ai_family  = family;
	hints.ai_flags  |= AI_ADDRCONFIG;

	ret = getaddrinfo( domain, "domain", &hints, &res_ptr );
	if( ret == 0 )
	{
		res_ptr_tmp = res_ptr;
		while( res_ptr_tmp )
		{
			switch( res_ptr_tmp->ai_family )
			{
			case AF_INET:
				ptr = &((struct sockaddr_in*)res_ptr_tmp->ai_addr)->sin_addr;
				break;
			case AF_INET6:
				ptr = &((struct sockaddr_in6*)res_ptr_tmp->ai_addr)->sin6_addr;
				break;
			default:
				break;
			}
			if(!inet_ntop( res_ptr_tmp->ai_family, ptr, rem_endpoint_ip_addr_table[i], INET6_ADDRSTRLEN ))
			{
				LOGE("Couldn't convert resolved GRE remote endpoint IP adress to text");
			}

			LOGD( "[%s] DNS lookup OK [%s] -> [%s]", __FUNCTION__, domain, rem_endpoint_ip_addr_table[i] );
			res_ptr_tmp = res_ptr_tmp->ai_next;
			i++;
		}
		freeaddrinfo(res_ptr);
		return true;
	}

	return false;
}

static bool update_GRE_tunnel_status(int status, const char *ip)
{
	struct schema_PublicWiFi pw_rec;
	pjs_errmsg_t       perr;
	bool rc = false;
	json_t *where = NULL;

	if(NULL == ip){
		LOGE("[%s]: Invalid end point",__func__);
		return false;
	}

	memset(&pw_rec, 0 , sizeof(pw_rec));
	pw_rec._partial_update = true;

	if(is_SNM_H_enabled())
		where = ovsdb_where_simple_typed(SCHEMA_COLUMN(PublicWiFi,enable), (const void *)true, OCLM_BOOL);

	else
		where = ovsdb_where_simple_typed(SCHEMA_COLUMN(PublicWiFi,enable), (const void *)false, OCLM_BOOL);

	if(NULL == where)
		{
			LOGE("Update public WiFi: Could not get public WiFi table record");
			return false;
		}

	switch (status)
	{
	case GRE_tunnel_DOWN:
		SCHEMA_SET_STR(pw_rec.GRE_tunnel_status,"DOWN");
		break;

	case GRE_tunnel_DOWN_FQDN:
		SCHEMA_SET_STR(pw_rec.GRE_tunnel_status,"DOWN: FQDN couldn't be resolved");
		break;

	case GRE_tunnel_DOWN_PING:
		SCHEMA_SET_STR(pw_rec.GRE_tunnel_status,"DOWN: Remote endpoint not reachable");
		break;

	case GRE_tunnel_UP:
		SCHEMA_SET_STR(pw_rec.GRE_tunnel_status,"UP");
		break;

	default:
		SCHEMA_SET_STR(pw_rec.GRE_tunnel_status,"");
		break;
	}

	SCHEMA_SET_STR(pw_rec.remote_endpoint,ip);

	rc = ovsdb_sync_upsert_where("PublicWiFi",where, schema_PublicWiFi_to_json(&pw_rec, perr),NULL);
	if(!rc )
	{
		LOGE("Update public WiFi: Could not update public WiFi table");
		return false;
	}
	return true;
}


static bool get_domain_valid_IP(const char *domain)
{
	bool ret = false;

	if(! strlen(domain))
	{
		LOGE("[%s] Invalid Argument",__func__);
		return false;
	}
	//If IPv6 could not be resolved try IPv4
	ret = resolve_DNS(domain,AF_INET6);
	if(!ret)
		ret = resolve_DNS(domain,AF_INET);

	return ret;
}


static bool check_remote_endpoint_alive(const char *remote_endpoint)
{
	int err,ret;
	char cmd[156];

	ret = pwm_get_addr_family(remote_endpoint);

	if(AF_INET6 == ret)
	{
		snprintf(cmd, sizeof(cmd) - 1, "ping6 -c 1 -W 1 %s",remote_endpoint);
	}
	else if(AF_INET == ret)
	{
		snprintf(cmd, sizeof(cmd) - 1, "ping -c 1 -W 1 %s",remote_endpoint);
	}
	else
	{
		LOGE("Invalid inet family");
		return false;
	}
	cmd[sizeof(cmd) - 1] = '\0';
	err = cmd_log(cmd);
	if (err) {
		LOGE("Ping to Public WiFi end-point %s failed", remote_endpoint);
		return false;
	}
	else
		return true;
}

//Resolve AAAA first, if AAAA is not available resolve A, in any case preferred one is AAAA if available.
//IPv6 is preferred over IPv4 when both options are available.
static bool configure_endpoint(void)
{
	bool ret;
	int i=0;

	ret = get_domain_valid_IP(get_v6_domain_name());
	if(!ret)
	{
		ret = get_domain_valid_IP(get_v4_domain_name());
	}

	if(!ret)
	{
		update_GRE_tunnel_status(GRE_tunnel_DOWN_FQDN,"");
		return false;
	}

	while(rem_endpoint_ip_addr_table[i][0] && i < REM_ENDPOINT_IP_TABLE_SIZE)
	{
		if(check_remote_endpoint_alive(rem_endpoint_ip_addr_table[i]))
		{
			update_GRE_tunnel_status(GRE_tunnel_UP, rem_endpoint_ip_addr_table[i]);
			return true;
		}
		i++;
	};

	LOGE( "[%s] No GRE remote endpoints available",__func__);
	update_GRE_tunnel_status(GRE_tunnel_DOWN_PING,"");
	return false;
}

static void opt_wait_timer_cb(struct ev_loop *loop, ev_timer *watcher, int revents)
{
	if(!configure_endpoint())
	{
		LOGE( "[%s]Failed to configure domain name ",__func__);
	}
	return;
}

static bool pwm_fetch_domain_option()
{
	struct schema_DHCPv4_Client *dh4c;
	struct schema_DHCPv6_Client *dh6c;
	struct schema_DHCP_Option dh_op;
	json_t *where;

	bool enable = true;
	int rc = 0;

	/* Get DHCPv6_Client record  */
	dh6c = ovsdb_table_select_typed(&table_DHCPv6_Client, "enable", OCLM_BOOL, &enable, &rc);
	if(NULL == dh6c)
	{
		LOGE("DHCPv6 Client record fetch failed");
		return false;
	}

	//Iterate through all received options records to fetch DHCP_Option records
	for(int i = 0; i< dh6c->received_options_len; i++)
	{
		memset(&dh_op,0,sizeof(dh_op));
		/* Get DHCP Option record of received options*/
		where = ovsdb_where_uuid("_uuid",dh6c->received_options[i].uuid);

		if(NULL == where)
		{
			LOGE("DHCPv6 option record condition failed");
			break;
		}

		if(!ovsdb_table_select_one_where(&table_DHCP_Option,where, &dh_op))
		{
			LOGE("DHCPv6 option record fetch failed");
			break;
		}

		if(is_v6_opt24(dh_op.tag,dh_op.version,dh_op.type))
		{
			set_v6_domain_name(dh_op.value);
			break;
		}
	}
	free(dh6c);

	/* Get DHCPv4_Client record  */
	dh4c = ovsdb_table_select_typed(&table_DHCPv4_Client, "enable", OCLM_BOOL, &enable, &rc);
	if(NULL == dh4c)
	{
		LOGE("DHCPv4 Client record fetch failed");
		return false;
	}

	//Iterate through all received options records of DHCP_Options table
	for(int i = 0; i< dh4c->received_options_len; i++)
	{
		memset(&dh_op,0,sizeof(dh_op));
		/* Get DHCP Option record of received options*/
		where = ovsdb_where_uuid("_uuid",dh4c->received_options[i].uuid);

		if(NULL == where)
		{
			LOGE("DHCPv4 option record condition failed");
			break;
		}

		if(!ovsdb_table_select_one_where(&table_DHCP_Option,where, &dh_op))
		{
			LOGE("DHCPv4 option record fetch failed");
			break;
		}

		if(is_v4_opt15(dh_op.tag,dh_op.version ,dh_op.type))
		{
			set_v4_domain_name(dh_op.value);
			break;
		}
	}

	free(dh4c);
	return true;
}

static bool pwm_check_domain_option()
{
	bool ret = true;
	const char *v4_domain, *v6_domain;

	v6_domain = get_v6_domain_name();
	v4_domain = get_v4_domain_name();

	if((0 == strlen(v4_domain)) && (0== strlen(v6_domain)))
	{
		ret = pwm_fetch_domain_option();
		if(!ret)
		{
			LOGE("Fetch domain option failed");
			return false;
		}
	}
	return true;
}

bool pwm_reset_tunnel()
{
	bool ret = true;

	if(!is_SNM_H_enabled())
		return true;

	ret = update_GRE_tunnel_status(GRE_tunnel_DOWN_PING, "");
	if (!ret) {
		LOGE("Update GRE tunnel status failed");
		return false;
	}

	ret = pwm_check_domain_option();
	if (!ret) {
		LOGE("Check domain option failed");
		return false;
	}

	ret=configure_endpoint();
	if(!ret)
	{
		LOGE( "Failed to configure end point ");
		return false;
	}

	return true;
}

static void callback_DHCP_Option(
        ovsdb_update_monitor_t *mon,
        struct schema_DHCP_Option *old,
        struct schema_DHCP_Option *new)
{
	switch (mon->mon_type)
	    {
		case OVSDB_UPDATE_NEW:
		case OVSDB_UPDATE_MODIFY:
			if(is_SNM_H_enabled()){
				//For v4 received option 15
				if(is_v4_opt15(new->tag,new->version,new->type)){

					if(v6_domain_nm[0] == '\0')
						ev_timer_start(EV_DEFAULT, &opt_wait_timer);

					set_v4_domain_name(new->value);
				}
				// For v6 received option 24
				if(is_v6_opt24(new->tag,new->version,new->type)){

					ev_timer_stop(EV_DEFAULT, &opt_wait_timer);
					set_v6_domain_name(new->value);

					if(!configure_endpoint())
					{
						LOGE( "[%s]Failed to configure domain name ",__func__);
					}
				}
			}
			break;
		case OVSDB_UPDATE_DEL:
			if(is_SNM_H_enabled()){
				//Delete domain name for v4 option 15
				if(is_v4_opt15(new->tag,new->version,new->type)) {
					set_v6_domain_name("");
				}
				// Delete domain name v6 option 24
				if (is_v6_opt24(new->tag,new->version,new->type)){
					set_v4_domain_name("");
				}
			}
			break;
		default:
			LOGE("Public WiFi OVSDB event: unknown type %d", mon->mon_type);
			return;
	    }
}

/******************************************************************************/
/******************************************************************************/
void callback_PublicWiFi( ovsdb_update_monitor_t*	mon,
					struct schema_PublicWiFi*	old_rec,
					struct schema_PublicWiFi*	conf )
{
	bool ret=true;

	set_SNM_H_enable(conf->enable);
	switch( mon->mon_type )
	{
		case OVSDB_UPDATE_NEW:
		case OVSDB_UPDATE_MODIFY:
			if((false == old_rec->enable) && (true == conf->enable))
			{
				ret = pwm_check_domain_option();
				if (!ret) {
					LOGE("Public WiFi OVSDB event: check domain option failed");
				}
				ret=configure_endpoint();
				if(!ret)
				{
					LOGE( "[%s]Failed to configure domain name ",__func__);
				}
			}

			if((true == old_rec->enable) && (false == conf->enable))
			{
				update_GRE_tunnel_status(GRE_tunnel_DOWN,"");
			}

			ret = pwm_update_bridge(conf->enable, conf);
			if (!ret) {
				LOGE("Public WiFi OVSDB event: update Public WiFi bridge failed");
			}

			ret = pwm_update_tunnel( conf );
			if (!ret) {
				LOGE("Public WiFi OVSDB event: update Public WiFi tunnel failed");
			}
			break;

		case OVSDB_UPDATE_DEL:
			ret = pwm_update_tunnel( NULL );
			if (!ret) {
				LOGE("Public WiFi OVSDB event: delete Public WiFi tunnel failed");
			}

			ret = pwm_update_bridge(false,NULL);
			if (!ret) {
				LOGE("Public WiFi OVSDB event: delete Public WiFi bridge failed");
			}
			break;
		case OVSDB_UPDATE_ERROR:
		default:
			LOGI("Public WiFi Manager - callback table modify = [%d]", mon->mon_type);
			break;
	}
}

/******************************************************************************/
/******************************************************************************/
bool pwm_ovsdb_init(void)
{
	LOGI("Initializing Public WiFi Manager OVSDB tables...");

	OVSDB_TABLE_INIT_NO_KEY(PublicWiFi);
	OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_Config);
	OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_State);
	OVSDB_TABLE_INIT_NO_KEY(DHCP_Option);
	OVSDB_TABLE_INIT(IP_Interface, name);
	OVSDB_TABLE_INIT_NO_KEY(IPv6_Address);
	OVSDB_TABLE_INIT(Netfilter, name);
	OVSDB_TABLE_INIT_NO_KEY(DHCPv4_Client);
	OVSDB_TABLE_INIT_NO_KEY(DHCPv6_Client);

	OVSDB_TABLE_MONITOR(PublicWiFi, false);
	OVSDB_TABLE_MONITOR(DHCP_Option,false);

	ev_timer_init(&opt_wait_timer, opt_wait_timer_cb, OPT_WAIT_TIMEOUT, 0);
	memset(&v4_domain_nm,'\0',sizeof(v4_domain_nm));
	memset(&v6_domain_nm,'\0',sizeof(v6_domain_nm));
	memset(&rem_endpoint_ip_addr_table,'\0',sizeof(rem_endpoint_ip_addr_table[0][0]) * REM_ENDPOINT_IP_TABLE_SIZE * INET6_ADDRSTRLEN);
	return true;
}

