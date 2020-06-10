/*
* Copyright (c) 2019, Sagemcom.
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

#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
#include "ovsdb_cache.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"
#include "wano_nat.h"
#include "wano_ovsdb.h"
#include "wano_mapt.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB
#define RA_DEFAULT_LIFE_TIME 90
#define ADDRESS_TMP_FILE "/tmp/address"
# define DHCP6_PROC "odhcp6c"

#define PREFIX_LEN_64 64
#define CHILD_PREFIX_BYTE 0x01
static bool wano_mode_mapt = false;

struct ovsdb_table table_Wifi_Inet_State;
struct ovsdb_table table_Netfilter;
struct ovsdb_table table_Wifi_Inet_Config;
struct ovsdb_table table_Interface;
struct ovsdb_table table_IP_Interface;
struct ovsdb_table table_IPv6_RouteAdv;
struct ovsdb_table table_IPv6_Prefix;
struct ovsdb_table table_DHCP_Option;
struct ovsdb_table table_DHCPv6_Server;
struct ovsdb_table table_IPv6_Address;
struct schema_IPv6_Prefix rec;

static bool wano_delete_ipv6_prefix_rec(char *uuid);

static void callback_Wifi_Inet_State(ovsdb_update_monitor_t *mon, struct schema_Wifi_Inet_State *old,
		struct schema_Wifi_Inet_State *record)
{

	bool errcode = true;

	if (!mon || !record) {
		LOGE("NM2 OVSDB event: invalid parameters");
		return;
	}

	switch (mon->mon_type) {
	case OVSDB_UPDATE_NEW:
		break;

	case OVSDB_UPDATE_DEL:
		break;

	case OVSDB_UPDATE_MODIFY:
		errcode = wano_nat_nm2_is_modified(record);
		if (!errcode) {
			LOGE("NM2 OVSDB event: update NAT");
		}
		break;

	default:
		LOGE("NM2s OVSDB event: unknown type %d", mon->mon_type);
		break;
	}
}

static void callback_Netfilter(ovsdb_update_monitor_t *mon, struct schema_Netfilter *old,
		struct schema_Netfilter *record)
{
	if (!mon || !record) {
		LOGE("Netfilter OVSDB event: invalid parameters");
		return;
	}

	switch (mon->mon_type) {
	case OVSDB_UPDATE_NEW:
		break;

	case OVSDB_UPDATE_DEL:
		break;

	case OVSDB_UPDATE_MODIFY:
		break;

	default:
		LOGE("Netfilter OVSDB event: unknown type %d", mon->mon_type);
		break;
	}
}

static bool iface_wifi_inet_update(char* link_state)
{
	struct schema_Wifi_Inet_Config rec;
	int rc=0;
	json_t *where=NULL;
	bool enable=true;

	if(!link_state)
	{
		LOG(ERR, "Interface: Error link_state is invalid.");
        return false;
	}

	memset(&rec, 0, sizeof(rec));
	rec._partial_update = true;

	if(!strncmp(link_state,"up",sizeof("up")))
	{
		enable = true;
	}
	else
	{
		enable = false;
	}

	SCHEMA_SET_INT(rec.enabled,enable);
	where = ovsdb_where_simple_typed(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), "br-wan", OCLM_STR);

	if(where == NULL)
	{
		LOG(ERR,"Interface: where is NULL unable to locate br-wan obj");
		return false;
	}

	rc = ovsdb_table_update_where( &table_Wifi_Inet_Config, where, &rec);

	if(!rc)
	{
		LOG(ERR,"Interface: unexpected result [%d]", rc);
		return false;
	}
	else
	{
		LOGD("Interface: Wifi_Inet_Config is updated with return code %d ", rc);
		return true;
	}

}

static void callback_Interface(ovsdb_update_monitor_t *mon, struct schema_Interface *old,
		struct schema_Interface *record)
{
	if (!mon || !record) {
		LOGE("Interface OVSDB event: invalid parameters");
		return;
	}

	switch (mon->mon_type) {
	case OVSDB_UPDATE_NEW:
		if(!strncmp(record->name,"eth0",sizeof("eth0")))
		{
			LOGD("Interface: Modified interface: %s with %s", record->name, record->link_state);

			if(!iface_wifi_inet_update(record->link_state))
			{
				LOGE("Interface: wifi_inet br-wan update failed for %s with state %s", record->name, record->link_state);
			}
		}
		break;

	case OVSDB_UPDATE_DEL:
		break;

	case OVSDB_UPDATE_MODIFY:
		if(!strncmp(record->name,"eth0",sizeof("eth0")))
		{
			LOGD("Interface: Modified interface: %s with %s", record->name, record->link_state);

			if(strncmp(record->link_state,"up",sizeof("up"))) {
				char cmd[128]={0};
				snprintf(cmd, sizeof(cmd)-1, "ifconfig %s 0.0.0.0", WANO_IFC_WAN);
				LOGI("clear WAN bridge (%s) ip address", WANO_IFC_WAN);
				cmd_log(cmd);
			}
			if(!iface_wifi_inet_update(record->link_state))
			{
				LOGE("Interface: wifi_inet br-wan update failed for %s with state %s", record->name, record->link_state);
			}
		}

		break;

	default:
		LOGE("Netfilter OVSDB event: unknown type %d", mon->mon_type);
		break;
	}
}

//This method is to check if br-wan has valid ipv6 address assigned or not
static bool is_ipv6_address_exist()
{
	struct ifaddrs *ifaddr, *ifa;
	char host[NI_MAXHOST];
	int rc;

	if (getifaddrs(&ifaddr) == -1) {
		LOGE("failed to get getifaddrs");
		return false;
	}

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)ifa->ifa_addr;

		if (ifa->ifa_addr == NULL)
			continue;
		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		rc = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6),
				host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
		if (rc != 0) {
			LOGE("getnameinfo() failed: %s\n", gai_strerror(rc));
			return false;
		}
		if( ( 0 == strncmp(ifa->ifa_name,"br-wan",sizeof("br-wan") ) ) && ( 0 == in6->sin6_scope_id ) )
		{
			LOGD("Interface: %-8s address: %s scope %d ",
							ifa->ifa_name, host, in6->sin6_scope_id);
			freeifaddrs(ifaddr);
			return true;
		}

	}
	freeifaddrs(ifaddr);
	return false;
}

static void callback_IP_Interface(ovsdb_update_monitor_t *mon, struct schema_IP_Interface *old,
		struct schema_IP_Interface *record)
{
	struct schema_IPv6_RouteAdv ra;
	json_t *where = NULL;
	int life = 0;

	memset(&ra, 0, sizeof(ra));
	if (!mon || !record) {
		LOGE("IP_Interface OVSDB event: invalid parameters");
		return;
	}

	switch (mon->mon_type)
	{
	case OVSDB_UPDATE_NEW:
		break;

	case OVSDB_UPDATE_DEL:
		break;

	case OVSDB_UPDATE_MODIFY:
		if(0 == strncmp(WANO_IFC_WAN,record->if_name, sizeof(WANO_IFC_WAN)))
		{
			if(ovsdb_table_select_one(&table_IPv6_RouteAdv,"preferred_router","high",&ra))
			{
				int rc = 0;
				struct schema_IPv6_Prefix prefix_rec;

				if(is_ipv6_address_exist())
					life = RA_DEFAULT_LIFE_TIME;
				else
				{
					//If br-wan does not have valid IPv6 global address, prefix is invalid hence cleanup.
					rc = ovsdb_table_select_one(&table_IPv6_Prefix,"origin","ra", &prefix_rec);
					if(rc == 1)
						wano_delete_ipv6_prefix_rec(prefix_rec._uuid.uuid);
				}

				ra._partial_update = true;
				ra.default_lifetime_exists =true;
				ra.default_lifetime=life;

				where = ovsdb_where_simple_typed(SCHEMA_COLUMN(IPv6_RouteAdv, preferred_router),"high", OCLM_STR);
				if(ovsdb_table_update_where( &table_IPv6_RouteAdv, where, &ra ))
					LOGD("Updated ra_lifetime: %d ",ra.default_lifetime);
				else
					LOGE("Failed to update ra_lifetime");

			}else
				LOGE("Ovsdb ra record select FAILED");

		}
		break;

	default:
		LOGE("IP_Interface OVSDB event: unknown type %d", mon->mon_type);
		break;
	}
}

static void callback_IPv6_RouteAdv(ovsdb_update_monitor_t *mon, struct schema_IPv6_RouteAdv *old,
		struct schema_IPv6_RouteAdv *record)
{
	switch (mon->mon_type) {
		case OVSDB_UPDATE_NEW:
			break;

		case OVSDB_UPDATE_DEL:
			break;

		case OVSDB_UPDATE_MODIFY:
			break;

		default:
			LOGE("IPv6_RouteAdv OVSDB event: unknown type %d", mon->mon_type);
	}
}

static bool wano_get_ipv6_prefix(struct schema_DHCP_Option *record)
{
	if(!record)
	{
		LOGE("Update IPv6_Prefix: invalid parameter");
		return false;
	}

	char buffer[512];
	char* flag= NULL;

	snprintf(buffer,sizeof(buffer)-1,"%s",record->value);

	flag = strtok(buffer,",");

	if(flag != NULL)
	{
		flag = strtok(NULL, ",");
		if(flag != NULL)
		{
			SCHEMA_SET_STR(rec.preferred_lifetime,flag);
			flag = strtok(NULL, ",");
			if(flag != NULL)
			{
				SCHEMA_SET_STR(rec.valid_lifetime,flag);
			}
		}
	}
	return true;
}

static bool wano_manage_ipv6_addres(const char* value)
{
	if(!value)
	{
		LOGE("Manage ipv6 address: invalid parameter");
		return false;
	}

	char buffer[512];
	char cmd[1024];
	char line[512];
	char address[INET6_ADDRSTRLEN+10]="";
	int err = 0;
	FILE* f;
	char* flag = NULL;

	snprintf(buffer,sizeof(buffer)-1,"%s",value);

	flag = strtok(buffer,",");
	if(flag!=NULL)
	{
		snprintf(address,sizeof(address)-1,"%s",flag);
	}

	snprintf(cmd, sizeof(cmd)-1,"ip -6 addr show br-wan|grep inet6 |awk '{print $2;}' > %s", ADDRESS_TMP_FILE);

	err=cmd_log(cmd);

	if(err)
	{
		LOGE("Get ipv6 address from WAN interface failed(%d): %s", err, cmd);
	}

	f = fopen(ADDRESS_TMP_FILE,"r");

	if(NULL == f)
	{
		LOGD("Read ipv6 address: failed/file cannot be opened");
		return false;
	}

	while(fgets(line, sizeof(line), f) != NULL)
	{
		if(strstr(line,address) != NULL)
		{
			fclose(f);
			return true;
		}
	}

	fclose(f);
	return false;

}

static bool wano_delete_ipv6_prefix_rec(char *uuid)
{
	struct schema_DHCPv6_Server server_rec;

	/* Remove the IPv6_Prefix by mutating the uuidset */
	ovsdb_table_select_one(&table_DHCPv6_Server,"status","enabled", &server_rec);

	ovsdb_sync_mutate_uuid_set(
			SCHEMA_TABLE(DHCPv6_Server),
			ovsdb_where_uuid("_uuid", server_rec._uuid.uuid),
			SCHEMA_COLUMN(DHCPv6_Server, prefixes),
			OTR_DELETE,
			uuid);

	return true;
}

static bool wano_delete_ipv6_address_rec(char *uuid)
{
	struct schema_IP_Interface ip_intf_rec;

	/* Remove the IPv6_Address by mutating the uuidset */
	ovsdb_table_select_one(&table_IP_Interface,"name","BR_LAN", &ip_intf_rec);

	ovsdb_sync_mutate_uuid_set(
			SCHEMA_TABLE(IP_Interface),
			ovsdb_where_uuid("_uuid", ip_intf_rec._uuid.uuid),
			SCHEMA_COLUMN(IP_Interface, ipv6_addr),
			OTR_DELETE,
			uuid);

	return true;
}

static bool wano_add_ipv6_prefix_rec(char *value,bool enableChildPrefix , char* childPrefix)
{
	struct schema_IPv6_Prefix prefix;
	json_t *jaddr;
	pjs_errmsg_t perr;
	char* sub_str= NULL;
	char buf[512] = {'\0'};
	snprintf(buf,sizeof(buf)-1,"%s",value);

	memset(&prefix, 0, sizeof(prefix));
	prefix.autonomous = false;
	prefix.enable = true;
	prefix.on_link = false;

	STRSCPY(prefix.origin,"ra");
	prefix.origin_exists = true;

	STRSCPY(prefix.prefix_status,"preferred");
	prefix.prefix_status_exists = true;

	STRSCPY(prefix.static_type,"static");

	sub_str = strtok(buf,",");

	if(sub_str){
		if(enableChildPrefix)
			SCHEMA_SET_STR(prefix.address,childPrefix);
		else
			SCHEMA_SET_STR(prefix.address,sub_str);

		sub_str = strtok(NULL, ",");
		if(sub_str != NULL)
		{
			SCHEMA_SET_STR(prefix.preferred_lifetime,sub_str);

			sub_str = strtok(NULL, ",");
			if(sub_str != NULL){
				SCHEMA_SET_STR(prefix.valid_lifetime,sub_str);
			}
		}
	}
	jaddr = schema_IPv6_Prefix_to_json(&prefix, perr);

	ovs_uuid_t _uuid;
	struct schema_DHCPv6_Server server_rec;
	ovsdb_table_select_one(&table_DHCPv6_Server,"status","enabled", &server_rec);
	/*
	 * Insert with parent
	 */
	LOG(TRACE, "Insert_prefix_rec: Addr: %s Pref_lf:%s  parent_uuid: %s",
			prefix.address,
			prefix.preferred_lifetime,
			server_rec._uuid.uuid);

	int rc = ovsdb_sync_insert_with_parent(
			SCHEMA_TABLE(IPv6_Prefix),
			jaddr,
			&_uuid,
			SCHEMA_TABLE(DHCPv6_Server),
			ovsdb_where_uuid("_uuid", server_rec._uuid.uuid),
			SCHEMA_COLUMN(DHCPv6_Server,prefixes ));
	if (!rc)
	  LOG(ERR, "Prefix_rec: %s: Failed to insert IPv6 Prefix record ",prefix.address);

	return true;
}

static bool wano_add_ipv6_addr_rec(char *value ,bool enableChildPrefix , char* childPrefix)
{
	struct schema_IPv6_Address addr6;
	json_t *jaddr;
	pjs_errmsg_t perr;
	char* sub_str= NULL;
	char buf[256] = {'\0'};
	char addr[128] = {'\0'};
	int pref_len = 0;

	snprintf(buf,sizeof(buf)-1,"%s",value);
	memset(&addr6, 0, sizeof(addr6));
	addr6.enable = true;

	snprintf(addr6.address_status, sizeof(addr6.address_status), "preferred");
	addr6.address_status_exists = true;

	snprintf(addr6.origin, sizeof(addr6.origin), "static");
	addr6.origin_exists = true;
	snprintf(addr6.status, sizeof(addr6.status), "enabled");

	sub_str = strtok(buf,",");
	if(sub_str){
		if(enableChildPrefix)
			SCHEMA_SET_STR(addr6.prefix,childPrefix);
		else
			SCHEMA_SET_STR(addr6.prefix,sub_str);

		sub_str = strtok(NULL, ",");
		if(sub_str != NULL){

			SCHEMA_SET_STR(addr6.preferred_lifetime,sub_str);

			sub_str = strtok(NULL, ",");
			if(sub_str != NULL){

				SCHEMA_SET_STR(addr6.valid_lifetime,sub_str);

			}
		}
	}

	//Calculate address from prefix
	if(enableChildPrefix)
	{
		sub_str = strtok(childPrefix,"/");
		if(sub_str == NULL)
			return false;


		char* sub_int = strtok(NULL,"/");
		if(sub_int == NULL)
			return false;


		memset(&addr, 0, sizeof(addr));
		snprintf(addr,sizeof(addr),"%s1/%s",sub_str,sub_int);
	}
	else
	{
		sub_str = strtok(buf,"/");
		if(sub_str){
			pref_len = atoi(sub_str+strlen(sub_str)+1);
			if(0 != pref_len%16){
				pref_len = ((pref_len/16) + 1) * 16;
				memset(&addr, 0, sizeof(addr));
				snprintf(addr,sizeof(addr),"%s1/%d",sub_str,pref_len);
			}
		}
	}
	SCHEMA_SET_STR(addr6.address,addr);


	jaddr = schema_IPv6_Address_to_json(&addr6, perr);
	ovs_uuid_t _uuid;
	struct schema_IP_Interface intf_rec;
	ovsdb_table_select_one(&table_IP_Interface,"name","BR_LAN", &intf_rec);
	/*
	 * Insert with parent
	 */
	LOG(TRACE, "Insert_addr6_rec: Addr: %s Pref_lf:%s  parent_uuid: %s",
			addr6.address,
			addr6.preferred_lifetime,
			intf_rec._uuid.uuid);

	int rc = ovsdb_sync_insert_with_parent(
			SCHEMA_TABLE(IPv6_Address),
			jaddr,
			&_uuid,
			SCHEMA_TABLE(IP_Interface),
			ovsdb_where_uuid("_uuid", intf_rec._uuid.uuid),
			SCHEMA_COLUMN(IP_Interface, ipv6_addr));
	if (!rc)
		LOG(ERR, "IPv6 Address: %s: Failed to insert IPv6 address record ",addr6.address);

	return true;
}

char* wano_ChildPrefix(char* prefix,uint8_t childPrefixBits , int childPrefixLen)
{
	char child[INET6_ADDRSTRLEN];
	char* childPrefix =NULL;
	char* sub_str =NULL;
	struct in6_addr addr;

	sub_str = strtok(prefix,"/");
	if(sub_str == NULL)
		return NULL ;

	childPrefix = (char *)malloc(128*sizeof(char));
	if(childPrefix ==NULL )
		return NULL ;

	inet_pton(AF_INET6, sub_str, &addr);
	addr.s6_addr[7] |= childPrefixBits;
	inet_ntop(AF_INET6, &addr, child , INET6_ADDRSTRLEN);
	snprintf(childPrefix,127, "%s/%d",(const char*)child,childPrefixLen);

	return childPrefix;
}
static void callback_DHCP_Option(
        ovsdb_update_monitor_t *mon,
        struct schema_DHCP_Option *old,
        struct schema_DHCP_Option *new)
{

	bool errcode=true;
	int err = 1;
	char cmd[1024];
	char* prefix = NULL;
	char* sub_str= NULL;
	int rc= 0;
	char buffer[512];

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
			snprintf(buffer,sizeof(buffer)-1,"%s",new->value);
			if(new->tag == 26 && new->enable == true && !strncmp(new->type,"rx",sizeof("rx")))
			{
				errcode=wano_get_ipv6_prefix(new);
				if(!errcode)
				{
					LOGE("DHCP option OVSDB event: failed to update ipv6 prefix lifetime");
				}

				prefix = (char *)malloc(128*sizeof(char));
				if(prefix){
					sub_str = strtok(buffer,",");
					if(sub_str == NULL)
					{
						free(prefix);
						return;
					}

					char* childPrefix = wano_ChildPrefix(buffer, CHILD_PREFIX_BYTE , PREFIX_LEN_64);

					if(childPrefix == NULL)
					{
						free(prefix);
						return;
					}
					strncpy(prefix,sub_str,127);

					struct schema_IPv6_Prefix prefix_rec;
					struct schema_IPv6_Address addr6_rec;

          //Check if there is already Prefix exist,if this one is new prefix? indicates newtwork changed
          //remove stale prefix from IPv6_Prefix and add new prefix record.
					rc = ovsdb_table_select_one(&table_IPv6_Prefix,"origin","ra", &prefix_rec);
					if(rc == 1)
					{
						if(   ((!wano_mode_mapt) && strncmp(prefix,prefix_rec.address,sizeof(prefix_rec.address))) 
							|| (wano_mode_mapt && strncmp(childPrefix,prefix_rec.address,sizeof(prefix_rec.address))))
						{
							wano_delete_ipv6_prefix_rec(prefix_rec._uuid.uuid);
							wano_add_ipv6_prefix_rec(new->value,wano_mode_mapt,childPrefix);
						}
						rc=0;
					}
					else
					{
						/*Add the New Address*/	
						wano_add_ipv6_prefix_rec(new->value,wano_mode_mapt,childPrefix);
					}
          //Check if there is already Prefix exist,if this one is new prefix? indicates newtwork changed
          //remove stale prefix from IPv6_Address table and new prefix record.
					rc = ovsdb_table_select_one(&table_IPv6_Address,"address_status","preferred", &addr6_rec);
					if(rc)
					{
						if(  ((!wano_mode_mapt) && strncmp(prefix,addr6_rec.prefix,sizeof(addr6_rec.prefix)))
							|| (wano_mode_mapt && strncmp(childPrefix,addr6_rec.prefix,sizeof(addr6_rec.prefix))))
						{
							wano_delete_ipv6_address_rec(addr6_rec._uuid.uuid);
							wano_add_ipv6_addr_rec(new->value,wano_mode_mapt,childPrefix);
						}
					}
					else
					{
						/*Add the New Address*/	
						wano_add_ipv6_addr_rec(new->value,wano_mode_mapt,childPrefix);
					}
					/*Add the New Address*/	
					free(childPrefix);
					free(prefix);
				}//End of prefix
			}

			if(new->tag == 5 && new->enable == true && !strncmp(new->type,"rx",sizeof("rx")) && !strncmp(new->version,"v6",sizeof("v6")))
			{
				errcode=wano_manage_ipv6_addres(new->value);
				if(!errcode)
				{
					/** Address is received from the DHCPV6 server but it is not configured on the wan interface in this case we have to restart dhcpv6 client. */
					snprintf(cmd,sizeof(cmd)-1,"killall -s SIGUSR2 %s",DHCP6_PROC);
					cmd[sizeof(cmd)-1] = '\0';
					err = cmd_log(cmd);
					if(err)
					{
						LOGE("Restart dhcpv6 client failed(%d): %s", err, cmd);
					}
				}
				else
				{
					LOGD("Address is applied successfully with errorcode %d", errcode);
				}
			}
			if(new->tag == 95 && new->enable == true && !strncmp(new->type,"rx",sizeof("rx")) && !strncmp(new->version,"v6",sizeof("v6")))
			{
				struct schema_IPv6_Prefix prefix;
				struct schema_IPv6_Address addr6;

				bool update_addr = false;
				char option[512]="";
				char *childPrefix =NULL;

				/*Set that we are in MAP-T Mode*/
				wano_mode_mapt = true;

				rc = ovsdb_table_select_one(&table_IPv6_Prefix,"origin","ra", &prefix);
				if(rc == 1)
				{
					childPrefix = wano_ChildPrefix(prefix.address, CHILD_PREFIX_BYTE , PREFIX_LEN_64);
					if(childPrefix == NULL)
						return;

					/* Check prefix child*/
					if(strncmp(childPrefix,prefix.address,sizeof(prefix.address)))
					{
						snprintf(option, sizeof(option)-1,"%s,%s,%s" ,prefix.address,prefix.preferred_lifetime,prefix.valid_lifetime);
						wano_delete_ipv6_prefix_rec(prefix._uuid.uuid);
						wano_add_ipv6_prefix_rec(option,wano_mode_mapt,childPrefix);
						update_addr = true;
					}
					rc=0;
				}
				if(update_addr)
				{
					rc = ovsdb_table_select_one(&table_IPv6_Address,"address_status","preferred", &addr6);
					if(rc)
					{
						if(strncmp(childPrefix,addr6.prefix,sizeof(addr6.prefix)))
						{
							wano_delete_ipv6_address_rec(addr6._uuid.uuid);
							wano_add_ipv6_addr_rec(option,wano_mode_mapt,childPrefix);
						}
					}
				}
				if(childPrefix !=NULL)
					free(childPrefix);
			}
			break;

        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
			snprintf(buffer,sizeof(buffer)-1,"%s",old->value);
			if( old->tag == 26 && old->enable == true && !strncmp(old->type,"rx",sizeof("rx")))
			{
				prefix = (char *)malloc(128*sizeof(char));
				if(prefix == NULL)
					return;
				
				

				sub_str = strtok(buffer,",");
				if(sub_str == NULL)
				{
					free(prefix);
					return ;
				}
				char* childPrefix = wano_ChildPrefix(buffer, CHILD_PREFIX_BYTE , PREFIX_LEN_64);
				if(childPrefix ==NULL)
				{
					free(prefix);
					return;
				}
				
				strncpy(prefix,sub_str,127);
				struct schema_IPv6_Prefix prefix_rec;
				struct schema_IPv6_Address addr6_rec;
				//Check if there is already Prefix exist,then remove it
				rc = ovsdb_table_select_one(&table_IPv6_Prefix,"origin","ra", &prefix_rec);
				if(rc)
				{
					if(!strncmp(prefix,prefix_rec.address,sizeof(prefix_rec.address))
						|| !strncmp(childPrefix,prefix_rec.address,sizeof(prefix_rec.address)))
					{
						wano_delete_ipv6_prefix_rec(prefix_rec._uuid.uuid);
					}
					rc=0;
				}
				rc = ovsdb_table_select_one(&table_IPv6_Address,"address_status","preferred", &addr6_rec);
				if(rc)
				{
					if(!strncmp(prefix,addr6_rec.prefix,sizeof(addr6_rec.prefix))
						|| !strncmp(childPrefix,addr6_rec.prefix,sizeof(addr6_rec.prefix)))
					{
						wano_delete_ipv6_address_rec(addr6_rec._uuid.uuid);
					}
				}
				free(prefix);
				free(childPrefix);
			}
			if(old->tag == 95 && old->enable == true && !strncmp(old->type,"rx",sizeof("rx")))
			{
				wano_mode_mapt = false;
			}
			break;

		default:
            LOG(ERR, "dhcp_option OVSDB event: unkown type %d", mon->mon_type);
            return;
    }
}

static bool wano_update_ipv6_prefix(struct schema_IPv6_Prefix *new)
{
	if(!new)
	{
		LOGE("Update ipv6 prefix: invalid parameter");
	}

	int rc=0;
	json_t *where = NULL;
	rec._partial_update = true;
	rec.preferred_lifetime_exists = true;
	rec.valid_lifetime_exists = true;


	where = ovsdb_where_simple_typed(SCHEMA_COLUMN(IPv6_Prefix,origin),"ra", OCLM_STR);
	if(!where)
	{
		LOGE("Update IPv6_Prefix: Could not get prefix table record");
		return false;
	}

	rc = ovsdb_table_update_where(&table_IPv6_Prefix, where, &rec);
	if(rc != 1 )
	{
		LOGE("Update IPv6_Prefix: Could not update ipv6 prefix table");
		return false;
	}
	return true;
}

void callback_IPv6_Prefix(
        ovsdb_update_monitor_t *mon,
        struct schema_IPv6_Prefix *old,
        struct schema_IPv6_Prefix *new)
{

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
			if(rec.preferred_lifetime != NULL && rec.valid_lifetime != NULL)
			{
				wano_update_ipv6_prefix(new);
			}
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
			break;

        default:
            LOG(ERR, "ipv6_prefix OVSDB event: unkown type %d", mon->mon_type);
            return;
    }
 }

void callback_DHCPv6_Server(
        ovsdb_update_monitor_t *mon,
        struct schema_DHCPv6_Server *old,
        struct schema_DHCPv6_Server *new)
{
    switch (mon->mon_type)
    {
    case OVSDB_UPDATE_NEW:
    case OVSDB_UPDATE_MODIFY:
    case OVSDB_UPDATE_DEL:
	    break;
    default:
      return;
    }
}


void callback_IPv6_Address(
        ovsdb_update_monitor_t *mon,
        struct schema_IPv6_Address *old,
        struct schema_IPv6_Address *new)
{
	switch (mon->mon_type)
	{
		case OVSDB_UPDATE_NEW:
		if(new->enable && !strncmp(new->address,"fe80",sizeof("fe80")-1))
		{
			strucWanConfig.mapt_EnableIpv6=true;
		}
		break;
		case OVSDB_UPDATE_MODIFY:
		if((new->enable ==true) && (old->enable == false) && !strncmp(new->address,"fe80",sizeof("fe80")-1))
		{
			strucWanConfig.mapt_EnableIpv6=true;
		}
		else if((old->enable ==true) && (new->enable == false) && !strncmp(new->address,"fe80",sizeof("fe80")-1))
		{
			strucWanConfig.mapt_EnableIpv6=false;
		}
			break;
		case OVSDB_UPDATE_DEL:
			break;
		default:
			return;
	}

}

void wano_ovsdb_init(void)
{
	LOGI("Initializing WAN orchestrator tables");
	OVSDB_TABLE_INIT(Wifi_Inet_State, if_name);
	OVSDB_TABLE_INIT(Netfilter, name);
	OVSDB_TABLE_INIT_NO_KEY(Interface);
	OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_Config);
	OVSDB_TABLE_INIT(IP_Interface,if_name);
	OVSDB_TABLE_INIT_NO_KEY(IPv6_RouteAdv);
	OVSDB_TABLE_INIT_NO_KEY(DHCP_Option);
	OVSDB_TABLE_INIT_NO_KEY(IPv6_Prefix);
	OVSDB_TABLE_INIT_NO_KEY(DHCPv6_Server);
	OVSDB_TABLE_INIT_NO_KEY(IPv6_Address);

	OVSDB_TABLE_MONITOR(Wifi_Inet_State, false);
	OVSDB_TABLE_MONITOR(Netfilter, false);
	OVSDB_TABLE_MONITOR(Interface, false);
	OVSDB_TABLE_MONITOR(IP_Interface, false);
	OVSDB_TABLE_MONITOR(IPv6_RouteAdv,false);
	OVSDB_TABLE_MONITOR(DHCP_Option,false);
	OVSDB_TABLE_MONITOR(IPv6_Prefix,false);
	OVSDB_TABLE_MONITOR(DHCPv6_Server,false);
	OVSDB_TABLE_MONITOR(IPv6_Address,false);
	return;
}

