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

#ifndef LNX_ROUTE_H_INCLUDED
#define LNX_ROUTE_H_INCLUDED

#include <stdbool.h>

#include "const.h"
#include "ds_tree.h"
#include "lnx_netlink.h"
#include "osn_inet.h"
#include "osn_routes6.h"

typedef struct lnx_route6 lnx_route6_t;
typedef void lnx_route6_status_fn_t(lnx_route6_t *self, struct osn_route6_status *rts, bool remove);

/*
 * Route object, mainly used for keeping a global list of registered router interfaces
 */
struct lnx_route6
{
    char rt_ifname[C_IFNAME_LEN];  /* Interface name */
    lnx_route6_status_fn_t *rt_fn; /* Route status notify callback */
    lnx_netlink_t rt_nl;           /* Netlink object */
    ds_tree_t rt_cache;            /* Cached entries for this interface */
    ds_tree_node_t rt_tnode;       /* Red-black tree node */
};

bool lnx_route6_init(lnx_route6_t *slef, const char *ifname);
bool lnx_route6_fini(lnx_route6_t *self);
bool lnx_route6_status_notify(lnx_route6_t *self, lnx_route6_status_fn_t *func);

bool lnx_route6_add(osn_route6_cfg_t *self, const osn_route6_config_t *route);
bool lnx_route6_remove(osn_route6_cfg_t *self, const osn_route6_config_t *route);
bool lnx_route6_find_dev(osn_ip6_addr_t addr, char *buf, size_t bufSize);

osn_route6_cfg_t *lnx_route6_cfg_new(const char *if_name);
bool lnx_route6_cfg_del(osn_route6_cfg_t *self);
const char *lnx_route6_cfg_name(const osn_route6_cfg_t *self);
bool lnx_route6_apply(osn_route6_cfg_t *self);

#endif /* LNX_ROUTE_H_INCLUDED */
