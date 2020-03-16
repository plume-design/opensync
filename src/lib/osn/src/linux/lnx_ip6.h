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

#ifndef LXN_IP6_H_INCLUDED
#define LXN_IP6_H_INCLUDED

#include "ds_tree.h"
#include "const.h"
#include "osn_inet6.h"
#include "osn_types.h"

#include "lnx_netlink.h"

typedef struct lnx_ip6 lnx_ip6_t;
typedef void lnx_ip6_status_fn_t(lnx_ip6_t *self, struct osn_ip6_status *status);

struct lnx_ip6
{
    char                    ip6_ifname[C_IFNAME_LEN];   /* Interface name */
    ds_tree_t               ip6_addr_list;              /* List of IPv6 addresses */
    ds_tree_t               ip6_dns_list;               /* List of DNS addresses */
    lnx_netlink_t           ip6_nl;                     /* Netlink event object */
    struct osn_ip6_status   ip6_status;                 /* Current interface status */
    lnx_ip6_status_fn_t    *ip6_status_fn;              /* Status callback */
};

bool lnx_ip6_init(lnx_ip6_t *self, const char *ifname);
bool lnx_ip6_fini(lnx_ip6_t *self);
bool lnx_ip6_apply(lnx_ip6_t *self);
bool lnx_ip6_addr_add(lnx_ip6_t *self, const osn_ip6_addr_t *addr);
bool lnx_ip6_addr_del(lnx_ip6_t *self, const osn_ip6_addr_t *dns);
bool lnx_ip6_dns_add(lnx_ip6_t *self, const osn_ip6_addr_t *dns);
bool lnx_ip6_dns_del(lnx_ip6_t *self, const osn_ip6_addr_t *dns);
void lnx_ip6_status_notify(lnx_ip6_t *self, lnx_ip6_status_fn_t *fn);

#endif /* LXN_IP6_H_INCLUDED */
