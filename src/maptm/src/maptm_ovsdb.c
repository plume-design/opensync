/* Copyright (c) 2020 Charter, Inc.
 *
 * This module contains unpublished, confidential, proprietary
 * material. The use and dissemination of this material are
 * governed by a license. The above copyright notice does not
 * evidence any actual or intended publication of this material.
 *
 * Created: 05 February 2020
 *
 */
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include <stdio.h>

#include "maptm.h"

#ifdef MAPTM_DEBUG
#undef LOGI
#define LOGI	printf
#endif 

/**
 * Globals
 */
/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB
#define MAPTM_IFC_WAN "br-wan"

ovsdb_table_t table_Netfilter;
ovsdb_table_t table_Node_Config;
struct ovsdb_table table_Node_State;
static ovsdb_table_t table_Interface;
struct ovsdb_table table_IP_Interface;
struct ovsdb_table table_Wifi_Inet_Config;
struct ovsdb_table table_DHCPv6_Client;
struct ovsdb_table table_DHCP_Client;
struct ovsdb_table table_IPv6_Address;
/* To change to Enum  */
int WanConfig =0; 

bool maptm_update_mapt(bool enable)
{
	int rc=0;
	json_t *where = NULL;
	struct schema_Node_Config rec_config;
	memset(&rec_config, 0, sizeof(rec_config));
	rec_config._partial_update = true;

	where = ovsdb_where_multi(
		ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_Config, module),"WANO", OCLM_STR),
		ovsdb_where_simple_typed(SCHEMA_COLUMN(Node_Config, key),"maptParams", OCLM_STR),
		NULL);
	if(!where)
	{
		LOGE("%s check current mode Dual-Stack",__func__);
		goto exit;
	}
	if (enable)
	{
		SCHEMA_SET_STR(rec_config.value, "{\"support\":\"true\",\"interface\":\"br-wan\"}");
	}	
	else
	{
		SCHEMA_SET_STR(rec_config.value, "{\"support\":\"false\",\"interface\":\"br-wan\"}");
	}
	rec_config.value_exists = true;

	rc = ovsdb_table_update_where(&table_Node_Config, where, &rec_config);
	if(rc != 1 )
	{
		LOGE("%s Could not update mapt table", __func__);
		goto exit;
	}

	LOGD("%s Update mapt table", __func__);
	return true;
	exit:
	return false;

}


bool maptm_persistent()
{
	bool ret = false ;
	char mapt_support[10];
	if (osp_ps_exists("MAPT_SUPPORT") )
	{
		if (osp_ps_get("MAPT_SUPPORT", mapt_support, 5) != true)
		{
			LOGE("%s Cannot get MAPT_SUPPORT Value", __func__ );
			return false;
		}
	}
	else
	{
		snprintf(mapt_support,sizeof(mapt_support)-1, "%s", "true");
		if ((osp_ps_set("MAPT_SUPPORT", mapt_support)) != true)
		{
			LOGE("%s Cannot save mapt support through osp API",__func__);
			return false;
		}
	}
	LOGD("%s MAPT_Support= %s",__func__,mapt_support);

	if(!strncmp(mapt_support,"true",sizeof("true")-1))
	{
		ret = maptm_update_mapt(true);
	}
	else
	{
		ret = maptm_update_mapt(false);
	}

	return ret;
}


bool maptm_get_supportValue(char * value)
{
	char str[64] ;
	memset(str,0,sizeof(str));
	strncpy(str,value,sizeof(str));
	char delim[] = ":";

	char *ptr = strtok(str, delim);

	while (ptr != NULL)
	{
		if(!strncmp(ptr,"\"true\"",sizeof("\"true\"")) )
		{
			return true;
	    }
	    else if(!strncmp(ptr,"\"false\"",sizeof("\"false\"")) )
	    {
			return false;
	    }
		ptr = strtok(NULL, ",");
	}

	return false;
}
void callback_Node_Config(ovsdb_update_monitor_t *mon,
                         struct schema_Node_Config *old_rec,
                         struct schema_Node_Config *conf) {

    if (mon->mon_type == OVSDB_UPDATE_NEW) {
        LOGD("%s: new node config entry: module %s, key: %s, value: %s",
             __func__, conf->module, conf->key, conf->value);
        if(!strncmp(conf->module,"WANO",sizeof("WANO")))
        {
		if(maptm_get_supportValue(conf->value)==true){
			 WanConfig |= MAPTM_ELIGIBILITY_ENABLE ;
		 }
		strucWanConfig.mapt_support=maptm_get_supportValue(conf->value);
		maptm_dhcp_option_update_15_option(strucWanConfig.mapt_support);
		maptm_dhcp_option_update_95_option(strucWanConfig.mapt_support);	    }
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL) {
        LOGD("%s: node config entry deleted: module %s, key: %s, value: %s",
             __func__, old_rec->module, old_rec->key, old_rec->value);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY) {
        LOGD("%s: node config entry updated: \n"
             "old module: %s, old key: %s, old value: %s \n"
             "new module: %s, new key: %s, new value: %s",
             __func__, old_rec->module, old_rec->key, old_rec->value,
             conf->module, conf->key, conf->value);
		if(!strncmp(conf->module,"WANO",sizeof("WANO")))
		{
			strucWanConfig.mapt_support = maptm_get_supportValue(conf->value);
			if(maptm_get_supportValue(conf->value)!=maptm_get_supportValue(old_rec->value))
			{
				maptm_dhcp_option_update_15_option(strucWanConfig.mapt_support);
				maptm_dhcp_option_update_95_option(strucWanConfig.mapt_support);
				
	        }
			if((maptm_get_supportValue(conf->value) == true) && (maptm_get_supportValue(old_rec->value) ==false) )
			{
				WanConfig |= MAPTM_ELIGIBILITY_ENABLE;
			}
        	else if ((maptm_get_supportValue(conf->value) == false) && (maptm_get_supportValue(old_rec->value) ==true) )
        	{
				WanConfig &= MAPTM_IPV6_ENABLE;
			}
        	if(maptm_get_supportValue(conf->value)!=maptm_get_supportValue(old_rec->value) )
        	{
				maptm_eligibilityStart(WanConfig);
				if (osp_ps_set("MAPT_SUPPORT", maptm_get_supportValue(conf->value)?"true":"false") != true)
				{
					LOGE("SGC: Error saving new MAP-T support value");
				}
			}
	    }
    }
}
static void callback_Interface(ovsdb_update_monitor_t *mon, struct schema_Interface *old,
		struct schema_Interface *record)
{
	LOGA("Starting callback_Interface    %s name ", record->name);
	if (!mon || !record) {
		LOGE("Interface OVSDB event: invalid parameters");
		return;
	}

	switch (mon->mon_type) {
		case OVSDB_UPDATE_NEW:
			break;

		case OVSDB_UPDATE_DEL:
			break;

		case OVSDB_UPDATE_MODIFY:
			if(!strncmp(record->name,"br-wan",sizeof("br-wan")))
			{
				if(!strncmp(record->link_state,"up",sizeof("up")))
				{
					strucWanConfig.link_up=true;
					if(record->link_state!= old->link_state)
						maptm_eligibilityStart(WanConfig);
				}
				else if (!strncmp(record->link_state,"down",sizeof("down"))) 
				{
					strucWanConfig.link_up=false;
					maptm_eligibilityStop();
				}

		}

		break;

	default:
		LOGE("Netfilter OVSDB event: unknown type %d", mon->mon_type);
		break;
	}
}
int maptm_ovsdb_init()
{
	// Initialize persistent storage
    if (!osp_ps_init())
    {
        LOGE("Initializing osp ps (failed)");
    }

	OVSDB_TABLE_INIT_NO_KEY(Interface);
	OVSDB_TABLE_INIT(IP_Interface,status);
	OVSDB_TABLE_INIT_NO_KEY(Node_Config);
	OVSDB_TABLE_INIT_NO_KEY(Node_State);
	OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_Config);
	OVSDB_TABLE_INIT_NO_KEY(DHCPv6_Client);
	OVSDB_TABLE_INIT_NO_KEY(Netfilter);
	OVSDB_TABLE_INIT_NO_KEY(IPv6_Address);
    // Initialize OVSDB monitor callbacks
   	OVSDB_TABLE_MONITOR(Interface, false);
	OVSDB_TABLE_MONITOR(Node_Config, false);

    return 0;
}
