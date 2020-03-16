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

#ifndef LNX_NETIF_H_INCLUDED
#define LNX_NETIF_H_INCLUDED

#include "osn_netif.h"
#include "lnx_netlink.h"

typedef struct lnx_netif lnx_netif_t;
typedef void lnx_netif_status_fn_t(lnx_netif_t *self, struct osn_netif_status *status);

/*
 * lnx_netif object
 */
struct lnx_netif
{
    char                    ni_ifname[C_IFNAME_LEN];    /* Interface name */
    unsigned int            ni_index;                   /* Interface index or 0 if invalid */
    void                   *ni_data;                    /* User data */
    lnx_netlink_t           ni_netlink;                 /* Netlink socket for receiving interface */
    int                     ni_mtu;                     /* MTU setting, -1 means not set */
    int                     ni_state;                   /* Configured interface state:
                                                         *    -1: Not set
                                                         *     0: Down
                                                         *     1: Up
                                                         */
    osn_mac_addr_t          ni_hwaddr;                  /* MAC address, OSN_MAC_ADDR_INIT means not set */
    lnx_netif_status_fn_t  *ni_status_fn;               /* Status callback */

    struct osn_netif_status
                            ni_status;                  /* Cached status */
};

bool lnx_netif_init(lnx_netif_t *self, const char *ifname);
bool lnx_netif_fini(lnx_netif_t *self);
bool lnx_netif_state_set(lnx_netif_t *self, bool up);
bool lnx_netif_mtu_set(lnx_netif_t *self, int mtu);
bool lnx_netif_hwaddr_set(lnx_netif_t *self, osn_mac_addr_t hwaddr);
bool lnx_netif_apply(lnx_netif_t *self);
void lnx_netif_status_notify(lnx_netif_t *self, lnx_netif_status_fn_t *fn);

#endif /* LNX_NETIF_H_INCLUDED */

