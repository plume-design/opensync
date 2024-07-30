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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>  //getpid()

// Remove netlink symbol collisions when including linux net/if.h
// Ref: https://github.com/thom311/libnl commit 50a7699
#define _LINUX_IF_H

#include <netlink/cache.h>
#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <netlink/route/route.h>

#include <net/if.h>

#include "log.h"
#include "memutil.h"
#include "osn_inet.h"
#include "osn_routes6.h"
#include "util.h"

struct osn_route6_cfg
{
    // interface name string buffer, to be expanded dynamically to fit entire string
    char if_name[0];
};

osn_route6_cfg_t *lnx_route6_cfg_new(const char *if_name)
{
    struct osn_route6_cfg *self = NULL;
    int size = sizeof(*self) + strlen(if_name) + 1; /*null*/
    self = MALLOC(size);
    strscpy(self->if_name, if_name, size);
    return self;
}

static uint32_t gen_local_port(void)
{
    static unsigned pcnt = 0;
    uint32_t port = (~getpid() & 0x00FFFF) << 8;
    return port + (++pcnt & 0x00FF);
}

static struct nl_sock *nl_route_socket(void)
{
    struct nl_sock *sock = nl_socket_alloc();
    if (!sock)
    {
        LOG(DEBUG, "lnx_route6: nl_socket_alloc() failed");
        return NULL;
    }

    // use own local port number gen because libnl3 auto
    // port is conflicting with already open socket port
    uint32_t port = gen_local_port();
    nl_socket_set_local_port(sock, port);

    int err;
    if ((err = nl_connect(sock, NETLINK_ROUTE)) < 0)
    {
        nl_socket_free(sock);
        LOG(DEBUG, "lnx_route6: nl_connect() on %u port failed. Err (%d) %s", port, err, nl_geterror(err));
        return NULL;
    }
    return sock;
}

static struct nl_addr *addr_parse(const char *str, int family)
{
    struct nl_addr *addr;
    int err;

    if ((err = nl_addr_parse(str, family, &addr)) < 0)
    {
        return NULL;
    }
    return addr;
}

static int route_add_dst(struct rtnl_route *route, const char *dst)
{
    struct nl_addr *addr = addr_parse(dst, rtnl_route_get_family(route));
    if (addr == NULL) return -1;

    int err = rtnl_route_set_dst(route, addr);

    nl_addr_put(addr);
    return err < 0 ? err : 0;
}

static int route_add_next_hop(struct rtnl_route *route, const char *dev, const char *gw)
{
    int err = -1;
    struct rtnl_nexthop *nh = rtnl_route_nh_alloc();
    if (nh != NULL)
    {
        if (dev)
        {
            unsigned idx = if_nametoindex(dev);
            if (idx > 0)
            {
                rtnl_route_nh_set_ifindex(nh, (int)idx);
                err = 0;
            }
        }

        if (gw)
        {
            struct nl_addr *addr = addr_parse(gw, rtnl_route_get_family(route));
            if (addr != NULL)
            {
                rtnl_route_nh_set_gateway(nh, addr);
                nl_addr_put(addr);
                err = 0;
            }
        }

        if (!err)
        {
            rtnl_route_add_nexthop(route, nh);
        }
        else
        {
            rtnl_route_nh_free(nh);
        }
    }
    return err;
}

static int route_add_pref_src(struct rtnl_route *route, const char *pref_src)
{
    struct nl_addr *addr = addr_parse(pref_src, rtnl_route_get_family(route));
    if (addr == NULL) return -1;

    int err = rtnl_route_set_pref_src(route, addr);

    nl_addr_put(addr);
    return err < 0 ? err : 0;
}

static struct rtnl_route *nl_create_route(
        int protocol,
        const char *dst,
        const char *dev,
        const char *gw,
        int metric,
        const char *pref_src,
        uint32_t table)
{
    struct rtnl_route *route = rtnl_route_alloc();
    if (!route) return NULL;

    // support only IPv6 routes
    rtnl_route_set_family(route, AF_INET6);
    rtnl_route_set_type(route, RTN_UNICAST);
    if (protocol != RTPROT_UNSPEC)
    {
        rtnl_route_set_protocol(route, (uint8_t)protocol);
    }

    if (0 != route_add_dst(route, dst)) goto err;
    if (0 != route_add_next_hop(route, dev, gw)) goto err;
    if (metric >= 0)
    {
        rtnl_route_set_priority(route, (uint32_t)metric);
    }

    if (pref_src != NULL)
    {
        if (route_add_pref_src(route, pref_src) != 0) goto err;
    }
    if (table > 0)
    {
        rtnl_route_set_table(route, table);
    }

    return route;

err:
    rtnl_route_put(route);
    return NULL;
}

bool lnx_route6_cfg_del(osn_route6_cfg_t *self)
{
    FREE(self);
    return true;
}

bool lnx_route6_apply(osn_route6_cfg_t *self)
{
    /* This implementation has no buffering for the routes, so apply
     * methods has nothing to do, all routes are already set in OS */
    return true;
}

const char *lnx_route6_cfg_name(const osn_route6_cfg_t *self)
{
    return self->if_name;
}

static bool route_action(osn_route6_cfg_t *self, const osn_route6_config_t *route, bool add)
{
    int rv = -1;
    struct nl_sock *sock = nl_route_socket();
    if (!sock)
    {
        LOG(ERR, "lnx_route6: cannot open netlink socket");
        return false;
    }

    LOG(TRACE,
        "lnx_route6: %s route to %s via %s gw %s src %s",
        add ? "add" : "delete",
        FMT_osn_ip6_addr(route->dest),
        self->if_name,
        route->gw_valid ? FMT_osn_ip6_addr(route->gw) : "",
        route->pref_src_set ? FMT_osn_ip6_addr(route->pref_src) : "");

    struct rtnl_route *nlrt = nl_create_route(
            RTPROT_STATIC,
            FMT_osn_ip6_addr(route->dest),
            self->if_name,
            route->gw_valid ? FMT_osn_ip6_addr(route->gw) : NULL,
            route->metric,
            route->pref_src_set ? FMT_osn_ip6_addr(route->pref_src) : NULL,
            route->table);

    if (!nlrt)
    {
        LOG(ERR, "lnx_route6: cannot create netlink route to %s via %s", FMT_osn_ip6_addr(route->dest), self->if_name);
    }
    else
    {
        if (add)
        {  // add new route
            rv = rtnl_route_add(sock, nlrt, NLM_F_EXCL);
            if (rv == -NLE_EXIST)
            {  // allow add the same route multiple times
                rv = 0;
            }
        }
        else
        {  // delete existing route if any
            rv = rtnl_route_delete(sock, nlrt, 0);
        }

        if (rv < 0)
        {
            LOG(NOTICE,
                "lnx_route6: cannot %s the route to %s via %s gw %s src %s : (%d) %s",
                add ? "add" : "delete",
                FMT_osn_ip6_addr(route->dest),
                self->if_name,
                route->gw_valid ? FMT_osn_ip6_addr(route->gw) : "",
                route->pref_src_set ? FMT_osn_ip6_addr(route->pref_src) : "",
                rv,
                nl_geterror(rv));
        }

        rtnl_route_put(nlrt);
    }
    nl_socket_free(sock);
    return (rv >= 0);
}

bool lnx_route6_add(osn_route6_cfg_t *self, const osn_route6_config_t *route)
{
    return route_action(self, route, true);
}

bool lnx_route6_remove(osn_route6_cfg_t *self, const osn_route6_config_t *route)
{
    return route_action(self, route, false);
}

static void parse_cb(struct nl_object *obj, void *arg)
{
    struct rtnl_route *route = (struct rtnl_route *)obj;

    struct rtnl_nexthop *nh = rtnl_route_nexthop_n(route, 0);
    if (nh != NULL)
    {
        int iidx = rtnl_route_nh_get_ifindex(nh);
        *((int *)arg) = iidx;
    }
    else
    {
        *((int *)arg) = -1;
    }
}

static int cb(struct nl_msg *msg, void *arg)
{
    if (nl_msg_parse(msg, &parse_cb, arg) < 0)
    {
        *((int *)arg) = -1;
    }
    return 0;
}

bool lnx_route6_find_dev(osn_ip6_addr_t dst_addr, char *buf, size_t bufSize)
{
    bool rv = false;
    if (!buf || bufSize < IF_NAMESIZE) return rv;

    struct nl_sock *sock = nl_route_socket();
    if (!sock) return rv;

    struct nl_msg *msg = NULL;
    struct nl_addr *dst = addr_parse(FMT_osn_ip6_addr(dst_addr), AF_INET6);
    if (!dst) goto end;

    struct rtmsg rmsg = {
        .rtm_family = nl_addr_get_family(dst),
        .rtm_dst_len = nl_addr_get_prefixlen(dst),
    };

    msg = nlmsg_alloc_simple(RTM_GETROUTE, 0);
    if (!msg) goto end;
    if (nlmsg_append(msg, &rmsg, sizeof(rmsg), NLMSG_ALIGNTO) < 0) goto end;
    if (nla_put_addr(msg, RTA_DST, dst) < 0) goto end;

    if (nl_send_auto(sock, msg) < 0) goto end;

    int iidx = -1;
    nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, cb, &iidx);
    if (nl_recvmsgs_default(sock) >= 0)
    {
        rv = (iidx > 0 ? if_indextoname(iidx, buf) != NULL : false);
    }

end:
    nlmsg_free(msg);
    nl_addr_put(dst);
    nl_socket_free(sock);
    return rv;
}
