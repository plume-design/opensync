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

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <unistd.h>

/* Prevent netlink symbol collisions when including linux net/if.h */
#define _LINUX_IF_H

#include <netlink/cache.h>
#include <netlink/netlink.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>
#include <netlink/route/route.h>

#include <net/if.h>

#include "lnx_route6_state.h"

#include "ds.h"
#include "ev.h"
#include "evx.h"
#include "log.h"
#include "memutil.h"
#include "osn_routes6.h"
#include "osn_types.h"

/**
 *
 * Linux route state polling using netlink.
 *
 */

struct lnx_route6_state
{
    lnx_route6_state_update_fn_t *rs_cache_update_fn;
};

static void lnx_route6_state_poll_nl(lnx_route6_state_t *self);
static void route6_cache_filter_cb(struct nl_object *object, void *arg);

lnx_route6_state_t *lnx_route6_state_new(lnx_route6_state_update_fn_t *rt_cache_update_fn)
{
    lnx_route6_state_t *self = CALLOC(1, sizeof(lnx_route6_state_t));

    self->rs_cache_update_fn = rt_cache_update_fn;

    return self;
}

void lnx_route6_state_del(lnx_route6_state_t *self)
{
    FREE(self);
}

void lnx_route6_state_poll(lnx_route6_state_t *self)
{
    lnx_route6_state_poll_nl(self);
}

/* Convert nl_addr to osn_ip6_addr_t */
static bool util_nl_addr_to_osn_ip6_addr(osn_ip6_addr_t *out, struct nl_addr *nl_addr)
{
    if (nl_addr_get_family(nl_addr) != AF_INET6) return false;

    *out = OSN_IP6_ADDR_INIT;
    if (nl_addr_iszero(nl_addr))
    {
        out->ia6_prefix = 0;
        return true;
    }
    if (nl_addr_get_len(nl_addr) != sizeof(out->ia6_addr)) return false;

    memcpy(&out->ia6_addr, nl_addr_get_binary_addr(nl_addr), sizeof(out->ia6_addr));

    if (nl_addr_get_prefixlen(nl_addr) != (8 * nl_addr_get_len(nl_addr)))
    {
        out->ia6_prefix = nl_addr_get_prefixlen(nl_addr);
    }

    return true;
}

static void route6_cache_filter_cb(struct nl_object *object, void *arg)
{
    lnx_route6_state_update_fn_t *cache_update_cb;
    struct osn_route6_status rts;
    struct rtnl_route *route;
    struct rtnl_nexthop *nexthop = NULL;
    struct nl_addr *rt_dest = NULL;
    struct nl_addr *rt_pref_src = NULL;
    struct nl_addr *rt_gw = NULL;
    uint32_t rt_table;
    uint32_t rt_priority;
    int nh_ifindex;
    char rt_nh_ifname[IFNAMSIZ] = {0};

    route = (struct rtnl_route *)object;
    cache_update_cb = (lnx_route6_state_update_fn_t *)arg;

    /* Routing table: */
    rt_table = rtnl_route_get_table(route);
    if (rt_table == RT_TABLE_LOCAL || rt_table == RT_TABLE_DEFAULT)
    {
        /* We're not interested in reporting routes from the local and default
         * routing tables. */
        return;
    }

    rt_dest = rtnl_route_get_dst(route);
    if (rt_dest == NULL)
    {
        return;  // Should not really happen.
    }

    nexthop = rtnl_route_nexthop_n(route, 0);
    if (nexthop != NULL)
    {
        rt_gw = rtnl_route_nh_get_gateway(nexthop);

        nh_ifindex = rtnl_route_nh_get_ifindex(nexthop);
        if (nh_ifindex > 0)
        {
            if (!if_indextoname(nh_ifindex, rt_nh_ifname))
            {
                LOG(ERR, "lnx_route6_state_nl: Error converting ifindex %u to name", nh_ifindex);
                return;
            }
        }
    }
    if (rt_nh_ifname[0] == '\0')
    {
        return;
    }

    rt_priority = rtnl_route_get_priority(route);
    rt_pref_src = rtnl_route_get_pref_src(route);

    rts = OSN_ROUTE6_STATUS_INIT;

    /* Route destination: */
    if (!util_nl_addr_to_osn_ip6_addr(&rts.rts_route.dest, rt_dest))
    {
        LOG(ERR, "lnx_route6_state_nl: Error converting route dest address");
        return;
    }

    /* Route gateway: */
    if (rt_gw != NULL)
    {
        if (!util_nl_addr_to_osn_ip6_addr(&rts.rts_route.gw, rt_gw))
        {
            LOG(ERR, "lnx_route6_state_nl: Error converting route GW address");
            return;
        }

        rts.rts_route.gw_valid = true;
    }

    /* Route preferred source address: */
    if (rt_pref_src != NULL)
    {
        if (!util_nl_addr_to_osn_ip6_addr(&rts.rts_route.pref_src, rt_pref_src))
        {
            LOG(ERR, "lnx_route6_state_nl: Error converting route pref_src address");
            return;
        }

        rts.rts_route.pref_src_set = true;
    }

    /* Route metric: */
    rts.rts_route.metric = rt_priority;

    if (rt_table != RT_TABLE_MAIN)
    {
        /* Routes from the main RT (ID=254) are reported in status with table
         * set to 0 (unset -- meaning the main routing table). */
        rts.rts_route.table = rt_table;
    }

    char logstr[256];
    snprintf(
            logstr,
            sizeof(logstr),
            "%s dev %s gw %s src %s metric %d table %d",
            FMT_osn_ip6_addr(rts.rts_route.dest),
            rt_nh_ifname,
            rts.rts_route.gw_valid ? FMT_osn_ip6_addr(rts.rts_route.gw) : "none",
            rts.rts_route.pref_src_set ? FMT_osn_ip6_addr(rts.rts_route.pref_src) : "none",
            rts.rts_route.metric,
            rt_table);

    // ignore loopback and local-link (fe80::) routes
    enum osn_ip6_addr_type addr_type = osn_ip6_addr_type(&rts.rts_route.dest);
    switch (addr_type)
    {
        case OSN_IP6_ADDR_LOOPBACK:
        case OSN_IP6_ADDR_LOCAL_LINK:
            LOG(TRACE, "%s: ignore local %s", __func__, logstr);
            return;
        default:
            break;
    }

    LOG(TRACE, "lnx_route6_state_nl: Route: %s", logstr);

    if (cache_update_cb != NULL)
    {
        /* Update OpenSync routes cache: */
        cache_update_cb(rt_nh_ifname, &rts);
    }
}

static uint32_t gen_local_port(void)
{
    static unsigned pcnt = 0;
    uint32_t port = (~getpid() & 0x00FFFF) << 8;
    return port + (++pcnt & 0x00FF);
}

/**
 * Poll route states with netlink/libnl3. This should:
 * - Provide additional info such as report routes from non-main routing tables
 *   and report other route attributes (preferred src IP, etc).
 * - Provide better performance.
 */
static void lnx_route6_state_poll_nl(lnx_route6_state_t *self)
{
    lnx_route6_state_update_fn_t *cache_update_fn;
    struct nl_sock *sock = NULL;
    struct nl_cache *cache = NULL;
    struct rtnl_route *filter = NULL;
    int rc;

    cache_update_fn = self->rs_cache_update_fn;

    sock = nl_socket_alloc();
    if (sock == NULL)
    {
        LOG(ERROR, "lnx_route6_state_nl: Error allocating netlink socket");
        return;
    }

    /*
     * libnl3 auto port is conflicting with already open socket port.
     * Use our own local port number generator.
     */
    uint32_t port = gen_local_port();
    nl_socket_set_local_port(sock, port);

    rc = nl_connect(sock, NETLINK_ROUTE);
    if (rc != 0)
    {
        LOG(ERR, "lnx_route6_state_nl: nl_connect failed: %d (%s)", rc, nl_geterror(rc));
        goto out;
    }

    /* Get all AF_INET6 routes: */
    if (rtnl_route_alloc_cache(sock, AF_INET6, 0, &cache) != 0)
    {
        LOG(ERR, "lnx_route6_state_nl: Cannot allocate routing cache.");
        goto out;
    }

    filter = rtnl_route_alloc();
    rtnl_route_set_family(filter, AF_INET6); /* IPv6 routes only. */

    /* Traverse the routes, convert them from netlink representation to
     * our osn representation and call update callback. */
    nl_cache_foreach_filter(cache, (struct nl_object *)filter, route6_cache_filter_cb, cache_update_fn);

out:
    nl_cache_free(cache);
    rtnl_route_put(filter);
    nl_socket_free(sock);
}
