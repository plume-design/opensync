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

#ifndef LNX_NETLINK_H_INCLUDED
#define LNX_NETLINK_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "const.h"
#include "ds_tree.h"
#include "ds_dlist.h"

#define LNX_NETLINK_LINK        (1 << 0)    /* Interface L2 events */
#define LNX_NETLINK_IP4ADDR     (1 << 1)    /* IPv4 interface events */
#define LNX_NETLINK_IP6ADDR     (1 << 2)    /* IPv6 interface events */
#define LNX_NETLINK_IP4ROUTE    (1 << 3)    /* IPv4 route events */
#define LNX_NETLINK_IP6ROUTE    (1 << 4)    /* IPv6 route events */
#define LNX_NETLINK_IP4NEIGH    (1 << 5)    /* IPv4 neighbor report */
#define LNX_NETLINK_IP6NEIGH    (1 << 6)    /* IPv6 neighbor report */
#define LNX_NETLINK_ALL         UINT64_MAX

typedef struct lnx_netlink lnx_netlink_t;

typedef void lnx_netlink_fn_t(lnx_netlink_t *nl, uint64_t event, const char *ifname);

struct lnx_netlink
{
    bool                nl_active;                  /* True if this object has been started */
    uint64_t            nl_pending;                 /* List of pending events */
    uint64_t            nl_events;                  /* Subscribed events */
    char                nl_ifname[C_IFNAME_LEN];    /* Filter events for this interface */
    lnx_netlink_fn_t   *nl_fn;                      /* Callback */
    ds_tree_node_t      nl_tnode;
};

/**
 * Initialize lnx_netlink_t structure. Each time a NETLINK event is received,
 * the fn callback is invoked.
 */
bool lnx_netlink_init(lnx_netlink_t *self, lnx_netlink_fn_t *fn);

/**
 * Destroy netlink object
 */
bool lnx_netlink_fini(lnx_netlink_t *self);

/* Subscribe to events */
void lnx_netlink_set_events(lnx_netlink_t *self, uint64_t events);

/**
 * Subscribe to interface
 */
void lnx_netlink_set_ifname(lnx_netlink_t *self, const char *ifname);

/**
 * Start receiving events
 */
bool lnx_netlink_start(lnx_netlink_t *self);

/**
 * Stop receiving events
 */
bool lnx_netlink_stop(lnx_netlink_t *self);

#endif /* LNX_NETLINK_H_INCLUDED */
