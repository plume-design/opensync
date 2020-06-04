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

#ifndef PWM_INCLUDE_OVSDB
#define PWM_INCLUDE_OVSDB

#include <stdbool.h>

#define	PW_RADIO_2G_PHY       "wl1"
#define	PW_WIFI_2G_IFNAME     "wl1.4"
#define	PW_WIFI_2G_INDEX       4

#define	PW_RADIO_5G_PHY       "wl0"
#define	PW_WIFI_5G_IFNAME     "wl0.4"
#define	PW_WIFI_5G_INDEX       4

bool pwm_ovsdb_init(void);
bool is_SNM_H_enabled(void);
bool pwm_reset_tunnel(void);

extern struct ovsdb_table table_PublicWiFi;
extern struct ovsdb_table table_Wifi_Inet_Config;
extern struct ovsdb_table table_Wifi_Inet_State;
extern struct ovsdb_table table_DHCP_Option;
extern struct ovsdb_table table_IP_Interface;
extern struct ovsdb_table table_IPv6_Address;
extern struct ovsdb_table table_Netfilter;

enum GRE_tunnel_status_type {
    GRE_tunnel_DOWN,
    GRE_tunnel_DOWN_PING,
    GRE_tunnel_DOWN_FQDN,
    GRE_tunnel_UP,
};

#endif

