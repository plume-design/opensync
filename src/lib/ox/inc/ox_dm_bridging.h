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

#ifndef OX_DM_BRIDGING_H_INCLUDED
#define OX_DM_BRIDGING_H_INCLUDED

#include <stdbool.h>
#include <ox_route.h>
#include <ox_types.h>

/**
 * Build the pre-defined Device.Bridging.Bridge. / Bridge.{i}.Port. OVSDB
 * route table and register it with the router.
 *
 * Unlike Ethernet/IP, both tables are sourced directly from the native OVS
 * Bridge/Port/Interface tables (not Wifi_Inet_Config/State), with Port
 * membership left fully unfiltered - every Bridge.ports member becomes a
 * Port instance, including OpenSync/Plume-internal service ports and the
 * bridge's own self-referencing member (which doubles as the
 * ManagementPort).
 */
bool ox_dm_bridging_add_routes(ox_router_t *router);

#endif /* OX_DM_BRIDGING_H_INCLUDED */
