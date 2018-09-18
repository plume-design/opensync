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

#ifndef INET_TARGET_H_INCLUDED
#define INET_TARGET_H_INCLUDED

#include "target.h"

extern bool inet_target_inet_config_init(ds_dlist_t *inet_ovs);
extern bool inet_target_inet_state_init(ds_dlist_t *inet_ovs);
extern bool inet_target_master_state_init(ds_dlist_t *inet_ovs);
extern bool inet_target_inet_state_register(const char *ifname, void *istate_cb);
extern bool inet_target_master_state_register(const char *ifname, target_master_state_cb_t *mstate_cb);
extern bool inet_target_eth_inet_state_get(const char *ifname, struct schema_Wifi_Inet_State *istate);
extern bool inet_target_eth_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate);
extern bool inet_target_eth_inet_config_set(const char *ifname, struct schema_Wifi_Inet_Config *iconf);
extern bool inet_target_bridge_inet_state_get(const char *ifname, struct schema_Wifi_Inet_State *istate);
extern bool inet_target_bridge_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate);
extern bool inet_target_bridge_inet_config_set(const char *ifname, struct schema_Wifi_Inet_Config *iconf);
extern bool inet_target_vif_inet_state_get(const char *ifname, struct schema_Wifi_Inet_State *istate);
extern bool inet_target_vif_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate);
extern bool inet_target_vif_inet_config_set(const char *ifname, struct schema_Wifi_Inet_Config *iconf);
extern bool inet_target_vlan_inet_state_get(const char *ifname, struct schema_Wifi_Inet_State *istate);
extern bool inet_target_vlan_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate);
extern bool inet_target_vlan_inet_config_set(const char *ifname, struct schema_Wifi_Inet_Config *iconf);
extern bool inet_target_gre_inet_state_get(const char *ifname, char *remote_ip, struct schema_Wifi_Inet_State *istate);
extern bool inet_target_gre_master_state_get(const char *ifname, const char *remote_ip, struct schema_Wifi_Master_State *mstate);
extern bool inet_target_gre_inet_config_set(const char *ifname, char *remote_ip, struct schema_Wifi_Inet_Config *iconf);
extern bool inet_target_tap_inet_state_get(const char *ifname, struct schema_Wifi_Inet_State *istate);
extern bool inet_target_tap_master_state_get(const char *ifname, struct schema_Wifi_Master_State *mstate);
extern bool inet_target_tap_inet_config_set(const char *ifname, struct schema_Wifi_Inet_Config *iconf);
extern bool inet_target_mac_learning_register(void *omac_cb);
extern bool inet_target_dhcp_leased_ip_register(target_dhcp_leased_ip_cb_t *dlip_cb);
extern bool inet_target_dhcp_rip_set(const char *ifname, struct schema_DHCP_reserved_IP *schema_rip);
extern bool inet_target_dhcp_rip_del(const char *ifname, struct schema_DHCP_reserved_IP *schema_rip);

#endif /* INET_TARGET_H_INCLUDED */


