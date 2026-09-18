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

#ifndef OX_DM_ETHERNET_H_INCLUDED
#define OX_DM_ETHERNET_H_INCLUDED

#include <stdbool.h>
#include <ox_route.h>
#include <ox_types.h>

/**
 * Build the pre-defined Device.Ethernet.Interface. / Device.Ethernet.Link.
 * OVSDB route table and register it with the router.
 *
 * Both tables are sourced from Wifi_Inet_Config/Wifi_Inet_State, gated on
 * if_type=="eth", so every physical Ethernet port yields exactly one
 * Ethernet.Interface instance and one paired Ethernet.Link instance sharing
 * the same underlying OVSDB row (and thus the same uuid) - mirroring the
 * existing Device.WiFi.SSID./Device.WiFi.AccessPoint. pairing pattern in
 * ox_dm_wifi.c.
 */
bool ox_dm_ethernet_add_routes(ox_router_t *router);

#endif /* OX_DM_ETHERNET_H_INCLUDED */
