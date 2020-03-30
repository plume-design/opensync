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
#include "osn_inet.h"
#include "lnx_netlink.h"

typedef struct lnx_route lnx_route_t;
typedef void lnx_route_status_fn_t(
        lnx_route_t *self,
        struct osn_route_status *rts,
        bool remove);

/*
 * Route object, mainly used for keeping a global list of registered router interfaces
 */
struct lnx_route
{
    char                        rt_ifname[C_IFNAME_LEN];    /* Interface name */
    lnx_route_status_fn_t      *rt_fn;                      /* Route status notify callback */
    lnx_netlink_t               rt_nl;                      /* Netlink object */
    ds_tree_t                   rt_cache;                   /* Cached entries for this interface */
    ds_tree_node_t              rt_tnode;                   /* Red-black tree node */
};

bool lnx_route_init(lnx_route_t *slef, const char *ifname);
bool lnx_route_fini(lnx_route_t *self);
bool lnx_route_status_notify(lnx_route_t *self, lnx_route_status_fn_t *func);

#endif /* LNX_ROUTE_H_INCLUDED */
