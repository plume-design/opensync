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

#ifndef DNSMASQ6_SERVER_H_INCLUDED
#define DNSMASQ6_SERVER_H_INCLUDED

#include "ds_dlist.h"
#include "ds_tree.h"
#include "const.h"

#include "osn_types.h"
#include "osn_dhcpv6.h"

typedef struct dnsmasq6_server dnsmasq6_server_t;
typedef struct dnsmasq6_radv dnsmasq6_radv_t;

typedef void dnsmasq6_server_status_fn_t(
        dnsmasq6_server_t *d6s,
        struct osn_dhcpv6_server_status *status);

/*
 * There's a single global instance of dnsmasq, we need to connect the various
 * configuration structures so we can create a single config file.
 *
 * This is a combined structure for Router Advertisement and DHCPv6 server
 */
struct dnsmasq6_server
{
    /* DHCPv6 fields */
    char                        d6s_ifname[C_IFNAME_LEN];
    bool                        d6s_prefix_delegation;
    ds_tree_t                   d6s_prefixes;
    char                       *d6s_options[OSN_DHCP_OPTIONS_MAX];
    struct osn_dhcpv6_server_status
                                d6s_status;         /* Cached server status */
    dnsmasq6_server_status_fn_t
                               *d6s_notify_fn;      /* Notification callback */
    ds_tree_t                   d6s_leases;
    ds_tree_node_t              d6s_tnode;
};

/*
 * Router Advertisement structure
 */
struct dnsmasq6_radv
{
    char                        ra_ifname[C_IFNAME_LEN];
    struct osn_ip6_radv_options ra_opts;
    ds_dlist_t                  ra_prefixes;
    ds_dlist_t                  ra_rdnss;
    ds_dlist_t                  ra_dnssl;
    ds_tree_node_t              ra_tnode;
};

/*
 * DHCPv6 API
 */
bool dnsmasq6_server_init(dnsmasq6_server_t *self, const char *ifname);
bool dnsmasq6_server_fini(dnsmasq6_server_t *self);
bool dnsmasq6_server_apply(dnsmasq6_server_t *self);
bool dnsmasq6_server_set(dnsmasq6_server_t *self, bool prefix_delegation);
bool dnsmasq6_server_prefix_add(dnsmasq6_server_t *self, struct osn_dhcpv6_server_prefix *prefix);
bool dnsmasq6_server_prefix_del(dnsmasq6_server_t *self, struct osn_dhcpv6_server_prefix *prefix);
bool dnsmasq6_server_option_send(dnsmasq6_server_t *self, int tag, const char *data);
bool dnsmasq6_server_lease_add(dnsmasq6_server_t *self, struct osn_dhcpv6_server_lease *lease);
bool dnsmasq6_server_lease_del(dnsmasq6_server_t *self, struct osn_dhcpv6_server_lease *lease);
bool dnsmasq6_server_status_notify(dnsmasq6_server_t *self, dnsmasq6_server_status_fn_t *fn);

/*
 * Router Advertisement API
 */
bool dnsmasq6_radv_init(dnsmasq6_radv_t *self, const char *ifname);
bool dnsmasq6_radv_fini(dnsmasq6_radv_t *self);
bool dnsmasq6_radv_apply(dnsmasq6_radv_t *self);
bool dnsmasq6_radv_set(dnsmasq6_radv_t *self, const struct osn_ip6_radv_options *opts);
bool dnsmasq6_radv_add_prefix(dnsmasq6_radv_t *self, const osn_ip6_addr_t *prefix, bool autonomous, bool onlink);
bool dnsmasq6_radv_add_rdnss(dnsmasq6_radv_t *self, const osn_ip6_addr_t *rdnss);
bool dnsmasq6_radv_add_dnssl(dnsmasq6_radv_t *self, char *dnssl);

#endif /* DNSMASQ6_SERVER_H_INCLUDED */
