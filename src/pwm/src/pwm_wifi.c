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

#include "const.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"


#include "os.h"
#include "pwm_ovsdb.h"
#include "pwm.h"
#include "pwm_bridge.h"
#include "pwm_firewall.h"
#include "pwm_utils.h"
#include "pwm_wifi.h"

#define MODULE_ID LOG_MODULE_ID_MISC

/******************************************************************************/
/******************************************************************************/
static  int    pwm_wifi_up = 0;

/******************************************************************************/
/******************************************************************************/
static int pwm_insert_Wifi_VIF_Config( char*	radio_phy_name,
										 char*	wifi_if_name,
										 char*	ssid,
										 char*	rs_ip_addr,
										 char*  rs_ip_port,
										 char*  rs_password,
										 int	v_radio_idx )
{
	int								ret_int;
	bool							ret_bool;
	const char*						crypto;
	json_t*							parent_where;
	json_t*							child_where;
	json_t*							jaddr;
	pjs_errmsg_t					perr;
	struct schema_Wifi_VIF_Config	new_wifi;

	LOGD("Wifi_VIF_Config INSERT: r_phy=[%s] wifi=[%s] ssid=[%s] r_ip_addr[%s] rs_password=[%s] r_idx=[%d]",
						radio_phy_name,
						wifi_if_name,
						ssid,
						rs_ip_addr,
						rs_password,
						v_radio_idx );

	memset( &new_wifi, 0, sizeof(struct schema_Wifi_VIF_Config) );

	schema_Wifi_VIF_Config_mark_all_present(&new_wifi);

	SCHEMA_SET_INT(        new_wifi.enabled,         1);
	SCHEMA_SET_INT(        new_wifi.ap_bridge,       0);
	SCHEMA_SET_STR(        new_wifi.bridge,          PWM_BR_IF_NAME);
	SCHEMA_SET_STR(        new_wifi.if_name,         wifi_if_name);
	SCHEMA_SET_STR(        new_wifi.mode,           "ap");

	SCHEMA_KEY_VAL_APPEND( new_wifi.security,       "encryption", "WPA2-802.1x");
	SCHEMA_KEY_VAL_APPEND( new_wifi.security,       "key",        rs_password);
	SCHEMA_KEY_VAL_APPEND( new_wifi.security,       "rs_ip",      rs_ip_addr);
	SCHEMA_KEY_VAL_APPEND( new_wifi.security,       "rs_port",    rs_ip_port);
	SCHEMA_KEY_VAL_APPEND( new_wifi.security,       "rs_rekey",   PW_RS_REKEY_INTERVAL);
	SCHEMA_SET_STR(        new_wifi.if_type,           "public");

	crypto = SCHEMA_KEY_VAL(new_wifi.security, "encryption");
	if( crypto != NULL )
	{
		if( strcmp( crypto, "WPA2-802.1x" ) == 0 )
		{
			ret_int = pwm_get_addr_family( rs_ip_addr );
			{
				if( ret_int != AF_INET )
				{
					LOGE("Wifi_VIF_Config SET: ERR: Connection to Radius Server IP address [%s] not supported", rs_ip_addr );
					return( 1 );
				}
			}
		}
	}

	SCHEMA_SET_STR( new_wifi.ssid,            ssid );
	SCHEMA_SET_STR( new_wifi.ssid_broadcast, "enabled");
	SCHEMA_SET_INT( new_wifi.uapsd_enable,    1 );
	SCHEMA_SET_INT( new_wifi.vif_radio_idx,   v_radio_idx );
	SCHEMA_SET_INT( new_wifi.group_rekey,     0);
	SCHEMA_SET_INT( new_wifi.ft_psk,          0);
	SCHEMA_SET_INT( new_wifi.rrm,             0);
	SCHEMA_SET_INT( new_wifi.btm,             0);
	SCHEMA_SET_INT( new_wifi.maxassoc,         25);

	parent_where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), radio_phy_name);
	child_where  = ovsdb_where_simple_typed(wifi_if_name, new_wifi.if_name, OCLM_STR);

	jaddr = schema_Wifi_VIF_Config_to_json(&new_wifi, perr);

	ret_bool = ovsdb_sync_upsert_with_parent(  "Wifi_VIF_Config",
												child_where,
												jaddr,
												NULL,
												"Wifi_Radio_Config",
												parent_where,
												"vif_configs" );

	if( ret_bool == false )
	{
		LOGE("Wifi_VIF_Config INSERT: r_phy=[%s] wifi=[%s] ssid=[%s] r_idx=[%d]", radio_phy_name, wifi_if_name, ssid, v_radio_idx );
		return( 2 );
	}

	return( 0 );
}

/******************************************************************************/
/******************************************************************************/
static void pwm_delete_Wifi_VIF_Config( char* radio_phy_name, char* wifi_if_name )
{
	int			ret_int;
	json_t*		parent_where;
	json_t*		child_where;

	LOGD("Wifi_VIF_Config DELETE: r_phy=[%s] wifi=[%s]", radio_phy_name, wifi_if_name );

	parent_where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), radio_phy_name);
	child_where  = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_Config, if_name),   wifi_if_name);

	ret_int = ovsdb_sync_delete_with_parent( "Wifi_VIF_Config",
											  child_where,
											 "Wifi_Radio_Config",
											  parent_where,
											 "vif_configs" );

	if( ret_int < 1 )
	{
		LOGE("Wifi_VIF_Config DELETE: r_phy=[%s] wifi=[%s] ret_int=[%d]", radio_phy_name, wifi_if_name, ret_int );
	}
}

/******************************************************************************/
/******************************************************************************/
void pwm_del_wifi( void )
{
	bool  ret_bool;

	LOGD("Wifi_VIF_Config DELETE:");

	if( pwm_wifi_up )
	{
		pwm_del_rs_fw_rules();

		ret_bool = pwm_delete_port_from_bridge( PW_WIFI_5G_IFNAME );
		if( !ret_bool )
		{
			LOGE("Wifi_VIF_Config DELETE: dell %s port to the Public WiFi bridge failed", PW_WIFI_5G_IFNAME);
		}

		ret_bool = pwm_delete_port_from_bridge( PW_WIFI_2G_IFNAME );
		if( !ret_bool )
		{
			LOGE("Wifi_VIF_Config DELETE: dell %s port to the Public WiFi bridge failed", PW_WIFI_2G_IFNAME);
		}

		pwm_delete_Wifi_VIF_Config( PW_RADIO_5G_PHY, PW_WIFI_5G_IFNAME );
		pwm_delete_Wifi_VIF_Config( PW_RADIO_2G_PHY, PW_WIFI_2G_IFNAME );

		pwm_wifi_up = 0;
	}
	else
	{
		LOGI("Wifi_VIF_Config DELETE: ALREADY deleted");
	}
}

/******************************************************************************/
/******************************************************************************/
void pwm_set_wifi( char* ssid, char* rs_ip_addr, int rs_ip_port, char* rs_password )
{
	bool	ret_bool;
	char	port_string[32];

	LOGD("Wifi_VIF_Config SET: ssid=[%s] rs_ip_addr=[%s] rs_ip_port=[%d] rs_password=[%s] ", ssid, rs_ip_addr, rs_ip_port, rs_password  );

	if( (ssid           == NULL) ||
		(rs_ip_addr     == NULL) ||
		(rs_password    == NULL) ||
		(ssid[0]        == 0)    ||
		(rs_ip_addr[0]  == 0)    ||
		(rs_password[0] == 0)    ||
		(rs_ip_port      < 0)    ||
		(rs_ip_port      > 0xFFFF)    )
	{
		LOGE("Wifi_VIF_Config SET: INPUT ERROR: ssid=[%s] rs_ip_addr=[%s] rs_ip_port=[%d] rs_password=[%s] ", ssid, rs_ip_addr, rs_ip_port, rs_password  );
		return;
	}

	if( pwm_wifi_up)
	{
		pwm_delete_Wifi_VIF_Config( PW_RADIO_5G_PHY, PW_WIFI_5G_IFNAME );
		pwm_delete_Wifi_VIF_Config( PW_RADIO_2G_PHY, PW_WIFI_2G_IFNAME );
		LOGD("PW OLD WIFI CONFIG DELETED");
	}

	snprintf( port_string, (sizeof(port_string)-1), "%d", rs_ip_port );


	if( pwm_insert_Wifi_VIF_Config( PW_RADIO_5G_PHY,
									PW_WIFI_5G_IFNAME,
									ssid,
									rs_ip_addr,
									port_string,
									rs_password,
									PW_WIFI_5G_INDEX    ) == 0 )
	{
		ret_bool = pwm_add_port_to_bridge( PW_WIFI_5G_IFNAME, PWM_PORT_TYPE_CUSTOMER );
		if( !ret_bool )
		{
			LOGE("Wifi_VIF_Config SET:: add %s port to the Public WiFi bridge failed", PW_WIFI_5G_IFNAME);
		}
	}

	if( pwm_insert_Wifi_VIF_Config( PW_RADIO_2G_PHY,
									PW_WIFI_2G_IFNAME,
									ssid,
									rs_ip_addr,
									port_string,
									rs_password,
									PW_WIFI_2G_INDEX    )  == 0 )
	{
		ret_bool = pwm_add_port_to_bridge( PW_WIFI_2G_IFNAME, PWM_PORT_TYPE_CUSTOMER );
		if( !ret_bool )
		{
			LOGE("Wifi_VIF_Config SET:: add %s port to the Public WiFi bridge failed", PW_WIFI_2G_IFNAME);
		}
	}

	pwm_add_rs_fw_rules( rs_ip_addr, port_string );

	pwm_wifi_up = 1;
}

