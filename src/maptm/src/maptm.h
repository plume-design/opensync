/*
* Copyright (c) 2020, Sagemcom.
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

#ifndef MAPTM_H_INCLUDED
#define MAPTM_H_INCLUDED

#include "ds_dlist.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "osp.h"
#include "osp_ps.h"

#define MAPT_MODULE_NAME    "MAPT"

#define MAPT_PS_STORE_NAME  MAPT_MODULE_NAME  /**< store name == module name */
#define MAPT_PS_KEY_NAME    "enabled"

#define MAPT_IFC_WAN "br-wan"
#define MAPT_IFC_LAN "BR_LAN"

extern int maptm_ovsdb_init(void);
extern bool maptm_ovsdb_nfm_add_rules(void);
extern bool maptm_ovsdb_nfm_del_rules(void);
extern void maptm_disableAccess(void);
extern void maptm_eligibilityStart(int WanConfig);
extern bool config_mapt(void);
extern bool stop_mapt(void);
extern void Parse_95_option(void);
extern void maptm_eligibilityStop(void);
extern void maptm_wan_mode(void);
extern int maptm_dhcp_option_init(void);
extern bool maptm_dhcp_option_update_15_option(bool maptSupport);
extern bool maptm_dhcp_option_update_95_option(bool maptSupport);
int maptm_main(int argc, char **argv);
#define MAPTM_NO_ELIGIBLE_NO_IPV6 0x00
#define MAPTM_NO_ELIGIBLE_IPV6 0x01
#define MAPTM_ELIGIBLE_NO_IPV6 0x10
#define MAPTM_ELIGIBLE_IPV6 0x11
#define MAPTM_ELIGIBILITY_ENABLE    0x10
#define MAPTM_IPV6_ENABLE    0x01

#define OVSDB_UUID_LEN    40
struct maptm_MAPT
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
#define IPV4_ADDRESS_SIZE       32
#define IPV6_PREFIX_MAX_SIZE    64

struct mapt
{
    uint8_t ealen;
    uint8_t prefix4len;
    uint8_t prefix6len;
    char *ipv4prefix;
    char *ipv6prefix;
    uint8_t offset;
    uint8_t psidlen;
    uint16_t psid;
    char *dmr;
    uint16_t domain_psid;
    uint16_t domain_psid_len;
    uint16_t ratio;
    char ipv4PublicAddress[100];
};
struct list_rules
{
    char *value;
    ds_dlist_t d_node;
};

extern struct maptm_MAPT strucWanConfig;
extern ovsdb_table_t table_DHCPv6_Client;
extern ovsdb_table_t table_DHCP_Client;
extern ovsdb_table_t table_Wifi_Inet_Config;
extern ovsdb_table_t table_mapt;
extern ovsdb_table_t table_Node_State;
extern ovsdb_table_t table_Netfilter;
extern ovsdb_table_t table_IPv6_Address;
bool maptm_persistent(void);
bool maptm_ovsdb_tables_ready(void);
bool maptm_ps_set(const char *key, char *value);

#endif /* MAPTM_H_INCLUDED */
