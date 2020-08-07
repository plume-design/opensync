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

#ifndef LNX_IP_H_INCLUDED
#define LNX_IP_H_INCLUDED

#include <stdbool.h>

#include "osn_types.h"
#include "osn_inet.h"
#include "ds_tree.h"
#include "const.h"
#include "lnx_netlink.h"

/*
 * ===========================================================================
 *  Linux backend for the OSN IP API
 * ===========================================================================
 */

typedef struct lnx_ip lnx_ip_t;
typedef void lnx_ip_status_fn_t(lnx_ip_t *ip, struct osn_ip_status *status);

struct lnx_ip
{
    char                    ip_ifname[C_IFNAME_LEN];        /* Interface name */
    ds_tree_t               ip_addr_list;                   /* List of IPv4 addresses */
    ds_tree_t               ip_dns_list;                    /* List of DNS addresses */
    ds_tree_t               ip_route_gw_list;               /* Gateway routes */
    lnx_netlink_t           ip_nl;                          /* Netlink object */
    struct osn_ip_status    ip_status;
    lnx_ip_status_fn_t     *ip_status_fn;                   /* Status update callback */
};

bool lnx_ip_init(lnx_ip_t *, const char *ifname);
bool lnx_ip_fini(lnx_ip_t *ip);
bool lnx_ip_addr_add(lnx_ip_t *ip, const osn_ip_addr_t *addr);
bool lnx_ip_addr_del(lnx_ip_t *ip, const osn_ip_addr_t *addr);
bool lnx_ip_dns_add(lnx_ip_t *ip, const osn_ip_addr_t *dns);
bool lnx_ip_dns_del(lnx_ip_t *ip, const osn_ip_addr_t *dns);
bool lnx_ip_route_gw_add(lnx_ip_t *ip, const osn_ip_addr_t *src, const osn_ip_addr_t *gw);
bool lnx_ip_route_gw_del(lnx_ip_t *ip, const osn_ip_addr_t *src, const osn_ip_addr_t *gw);
void lnx_ip_status_notify(lnx_ip_t *ip, lnx_ip_status_fn_t *fn);
bool lnx_ip_apply(lnx_ip_t *ip);

#endif /* LNX_IP_H_INCLUDED */
