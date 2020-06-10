/* Copyright (c) 2019 Charter, Inc.
 *
 * This module contains unpublished, confidential, proprietary
 * material. The use and dissemination of this material are
 * governed by a license. The above copyright notice does not
 * evidence any actual or intended publication of this material.
 *
 * Created: 29 July 2019
 *
 */
#include "ovsdb_table.h"
#include "schema.h"
#include "osp.h"
#ifndef __WANO_MAPT_MANAGER_H__
#define __WANO_MAPT_MANAGER_H__

#define WANO_MAPT_MODULE_NAME 		"WANO_MAPT"

#define WANO_MAPT_TIMEOUT_INTERVAL          5 /* Interval to verify timer condition */
	
extern int wano_mapt_ovsdb_init(void);
extern bool wano_mapt_ovsdb_nfm_add_rules(void);
extern bool wano_mapt_ovsdb_nfm_del_rules(void);
extern void wano_mapt_timerStart();
extern void wano_mapt_timerStop();
extern void wano_mapt_disableAccess();
extern void wano_mapt_eligibilityStart(int WanConfig);
extern bool config_mapt();
extern bool stop_mapt();
extern void Parse_95_option();
extern void wano_mapt_eligibilityStop();
extern void wano_mapt_callback_Timer();
extern int wano_mapt_dhcp_option_init( void );
extern bool wano_mapt_dhcp_option_update_15_option(bool maptSupport);
int wano_mapt_main(int argc, char ** argv);
#define WANO_MAPT_NO_ELIGIBLE_NO_IPV6 0x00 
#define WANO_MAPT_NO_ELIGIBLE_IPV6 0x01 
#define WANO_MAPT_ELIGIBLE_NO_IPV6 0x10 
#define WANO_MAPT_ELIGIBLE_IPV6 0x11
#define WANO_MAPT_ELIGIBILITY_ENABLE   0x10
#define WANO_MAPT_IPV6_ENABLE   0x01

#define OVSDB_UUID_LEN    40
struct wano_mapt_MAPT
{
	int mapt_support;
	bool mapt_95_Option;
	char mapt_95_value[8200];
	char iapd[256];
	int mapt_EnableIpv6;
	char mapt_mode[20];
	bool link_up;
	char option_23[OVSDB_UUID_LEN];
	char option_24[OVSDB_UUID_LEN];
};
#define IPV4_ADDRESS_SIZE      32
#define IPV6_PREFIX_MAX_SIZE   64

struct mapt {
	uint8_t ealen;
	uint8_t prefix4len;
	uint8_t prefix6len;
	char* ipv4prefix;
	char* ipv6prefix;
	uint8_t offset;
	uint8_t psidlen;
	uint16_t psid;
	char* dmr;
   uint16_t domaine_pssid;
   uint16_t domaine_psidlen;
   uint16_t ratio;
   char ipv4PublicAddress[100];
};
struct list_rules {
   char* value;
   ds_dlist_t d_node;
};

struct wano_mapt_MAPT  strucWanConfig ; 
extern struct ovsdb_table table_DHCPv6_Client;
extern struct ovsdb_table table_DHCP_Client;
extern struct ovsdb_table table_Wifi_Inet_Config;
extern struct ovsdb_table table_mapt;
extern struct ovsdb_table table_Node_State;
bool wano_mapt_persistent();

#endif  /* __WANO_MAPT_MANAGER_H__ */
