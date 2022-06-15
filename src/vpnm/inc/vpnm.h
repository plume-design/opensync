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

#ifndef VPNM_H_INCLUDED
#define VPNM_H_INCLUDED

#include "osn_vpn.h"

/**
 * Initialize the VPN_Tunnel handling.
 */
bool vpnm_tunnel_init(void);

/**
 * Initialize the IPsec VPN type handling.
 */
bool vpnm_ipsec_init(void);

/**
 * Initialize the Tunnel_Interface handling.
 */
bool vpnm_tunnel_iface_init(void);

/**
 * Is this VPN tunnel administratively enabled?
 *
 * @param tunnel_name[in]    tunnel name
 *
 * @return true if this VPN tunnel is configured as enabled
 */
bool vpnm_tunnel_is_enabled(const char *tunnel_name);

/**
 * Tunnel config change callback function.
 *
 * This function simply notifies the specific VPN type about a change in
 * VPN configuration.
 *
 * Then it is up to the VPN type to query the config.
 *
 * @param tunnel_name[in]   tunnel name
 *
 */
typedef void notify_vpn_tunnel_chg_fn_t(const char *tunnel_name);

/**
 * Register a specific (for instance IPsec or other) tunnel config.
 *
 * Should be called when a specific VPN type config is created.
 *
 * @param[in]  tunnel_name      name of the specific tunnel config
 * @param[in]  tunnel_chg_cg    tunnel change callback -- called when VPN_Tunnel
 *                              configuration changes to inform the specific
 *                              VPN type configs about it (for instance
 *                              enabling/disabling the VPN to enable/disable
 *                              the VPN config).
 */
void vpnm_tunnel_cfg_register(const char *tunnel_name, notify_vpn_tunnel_chg_fn_t *tunnel_chg_cb);

/**
 * Deregister the specific tunnel config.
 *
 * Should be called when a specific VPN type config is deleted.
 *
 * @param[in] tunnel_name   name of the specific tunnel config
 */
void vpnm_tunnel_cfg_deregister(const char *tunnel_name);

/**
 * Update the VPN_Tunnel status.
 *
 * Should be called by the specific VPN type (for instance IPsec) to set or
 * update the VPN tunnel connection status whenever it changes.
 *
 * @param[in] tunnel_name          the name of the tunnel
 * @param[in] vpn_conn_state       the new tunnel connection state
 */
void vpnm_tunnel_status_update(const char *tunnel_name, enum osn_vpn_conn_state vpn_conn_state);

/**
 * Resolve a name to an IP address.
 *
 * Note: If multiple IP addresses of the requested address family are on the
 * resolve list then the first IP address is returned. To force a new DNS
 * resolution (where the returned IP addresses may be in a different order
 * i.e. to potentially try a different IP address you can invoke this function
 * again)
 *
 * @param[out] ip_addr             The resolved IP address with the requested
 *                                 address family type.
 *
 * @param[in]  name                The name to resolve. FQDN.
 *                                 Or if name is already an IP address it will
 *                                 simply resolve to the same IP address, i.e.
 *                                 no DNS lookup will be done.
 *
 * @param[in]  addr_family         The requested address family: AF_INET for
 *                                 IPv4 or AF_INET6 for IPv6.
 *
 * @return   true on success i.e. if DNS resolution was successsful and at
 *           least one IP address of the requested address family was on the
 *           resolve list.
 */
bool vpnm_resolve(osn_ipany_addr_t *ip_addr, const char *name, int addr_family);

#endif /* VPNM_H_INCLUDED */
