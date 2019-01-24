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

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/route.h>
#include <net/if_arp.h>

#include <stdbool.h>
#include <regex.h>

#include <const.h>
#include <ds_tree.h>
#include <evx.h>

#include "inet_route.h"
#include "os_regex.h"

/* Debounce period - 100ms */
#define INET_ROUTE_POLL_DEBOUNCE_MS 100
#define INET_ROUTE_PROC_NET_ROUTE   "/proc/net/route"
#define INET_ROUTE_PROC_NET_ARP     "/proc/net/arp"

static const char inet_route_regex[] = "^"
        RE_GROUP(RE_IFNAME) RE_SPACE    /* Interface name */
        RE_GROUP(RE_XIPADDR) RE_SPACE   /* Destination */
        RE_GROUP(RE_XIPADDR) RE_SPACE   /* Gateway */
        RE_GROUP(RE_NUM) RE_SPACE       /* Flags -- skip */
        RE_NUM RE_SPACE                 /* RefCnt -- skip */
        RE_NUM RE_SPACE                 /* "Use" */
        RE_GROUP(RE_NUM) RE_SPACE       /* Metric */
        RE_GROUP(RE_XIPADDR) RE_SPACE   /* Netmask */
        RE_NUM RE_SPACE                 /* MTU */
        RE_NUM RE_SPACE                 /* Window */
        RE_NUM;                         /* IRTT */

static const char inet_arp_regex[] = "^"
    RE_GROUP(RE_IPADDR) RE_SPACE        /* IP address */
    RE_XNUM RE_SPACE                    /* HW type */
    RE_GROUP(RE_XNUM) RE_SPACE          /* Flags */
    RE_GROUP(RE_MAC) RE_SPACE           /* HW address */
    "\\*" RE_SPACE                      /* Mask */
    RE_GROUP(RE_IFNAME);

/*
 * Route object, mainly used for keeping a global list of registered router interfaces
 */
struct inet_route
{
    char                        rt_ifname[C_IFNAME_LEN];    /* Interface name */
    inet_route_notify_fn_t     *rt_fn;                      /* Route status notify callback */
    void                       *rt_data;                    /* Data associated with the callback */
    ds_tree_t                   rt_cache;                   /* Cached entries for this interface */
    ds_tree_node_t              rt_tnode;                   /* Red-black tree node */
};

/*
 * Routing table state cache -- this structure is indexed by rsc_state
 */
struct route_state_cache
{
    struct inet_route_state     rsc_state;                  /* Current route state */
    bool                        rsc_valid;                  /* Invalid cache entries will be deleted by inet_route_flusH() */
    ds_tree_node_t              rsc_tnode;                  /* Red-black tree node */
};

/*
 * ARP cache structure -- this structure is indexed by arp_ipaddr and arp_ifname
 */
struct route_arp_cache
{
    char                        arp_ifname[C_IFNAME_LEN];
    inet_ip4addr_t              arp_ipaddr;
    inet_macaddr_t              arp_hwaddr;
    ds_tree_node_t              arp_tnode;
};

/*
 * Private functions
 */
static bool route_init(void);
static void route_poll(void);
static void route_poll_delayed(void);
static void route_cache_reset(void);
static void route_cache_update(const char *ifname, struct inet_route_state *rts);
static void route_cache_flush(void);
static void route_arp_refresh(void);
static ds_key_cmp_t route_state_cmp;
static ds_key_cmp_t route_arp_cmp;

/* Global list of route objects, the primary key is the interface name */
static ds_tree_t inet_route_list = DS_TREE_INIT(ds_str_cmp, struct inet_route, rt_tnode);
static ds_tree_t route_arp_cache = DS_TREE_INIT(route_arp_cmp, struct route_arp_cache, arp_tnode);

/*
 * ===========================================================================
 *  Public interface
 * ===========================================================================
 */

/*
 * Create a new instance of the inet_route object
 */
inet_route_t *inet_route_new(const char *ifname)
{
    inet_route_t *self = NULL;

    /* Call the inet_route_init() functions exactly once */
    if (!route_init())
    {
        LOG(ERR, "inet_route: %s: Error initializing route global data.", ifname);
        goto error;
    }

    self = calloc(1, sizeof(inet_route_t));
    if (self == NULL)
    {
        LOG(ERR, "inet_route: %s: Constructor failed to allocate.", ifname);
        goto error;
    }

    if (strscpy(self->rt_ifname, ifname, sizeof(self->rt_ifname)) < 0)
    {
        LOG(ERR, "inet_route: %s: Interface name too long.", ifname);
        goto error;
    }

    /* Initialize the routing table cache for this entry */
    ds_tree_init(&self->rt_cache, route_state_cmp, struct route_state_cache, rsc_tnode);

    /* Add this instance to the global list */
    ds_tree_insert(&inet_route_list, self, self->rt_ifname);

    route_poll_delayed();

    return self;

error:
    if (self != NULL) free(self);
    return false;
}

/*
 * Delete a inet_route object
 */
bool inet_route_del(inet_route_t *self)
{
    ds_tree_iter_t iter;
    struct route_state_cache *rsc;

    /* Remove all cache entries associated with this object */
    ds_tree_foreach_iter(&self->rt_cache, rsc, &iter)
    {
        ds_tree_iremove(&iter);
        free(rsc);
    }

    free(self);

    return true;
}

/*
 * Set the callback that will receive route table change notifications.
 *
 * This is currently the only supported method -- this may change in the future
 */
bool inet_route_notify_set(inet_route_t *self, inet_route_notify_fn_t *func, void *data)
{
    self->rt_fn = func;
    self->rt_data = data;
    return true;
}

/*
 * ===========================================================================
 *  Private functions
 * ===========================================================================
 */

/* Netlink socket file descriptor */
static int rt_nlsock = -1;
/* Netlink socket watcher */
static ev_io rt_nlsock_ev;

/* Initiate a delayed poll -- the majority of the work is done in __route_poll() */
void __route_poll_delayed(struct ev_loop *loop, ev_debounce *ev, int revent)
{
    route_poll();
}

void route_poll_delayed(void)
{
    static ev_debounce poll_ev;
    static bool poll_init = false;

    if (!poll_init)
    {
        ev_debounce_init(&poll_ev, __route_poll_delayed, INET_ROUTE_POLL_DEBOUNCE_MS / 1000.0);
        poll_init = true;
    }

    /* Re-start the debounce timer */
    ev_debounce_start(EV_DEFAULT, &poll_ev);
}

/*
 * Global route initialization.
 *
 * Since we're using Linux specifics to parse the routing table (/proc/net/route) we
 * might as well use netlink sockets to improve performance a bit.
 *
 * Netlink is used for route state change notifications. The netlink protocol itself
 * is used as it is a beast of its own -- netlink messages are just used as a trigger
 * for the actual polling of the /proc/net/route file.
 */
void __inet_route_netlink_poll(struct ev_loop *loop, ev_io *w, int revent)
{
    size_t  bufsz = getpagesize();
    uint8_t buf[bufsz];

    LOG(DEBUG, "inet_route: Netlink event received.");

    if (revent & EV_ERROR)
    {
        LOG(EMERG, "inet_route: Netlink socket watcher error.");
        ev_io_stop(loop, w);
        return;
    }

    /* Discard all data */
    if (recv(rt_nlsock, buf, bufsz, 0) < 0)
    {
        LOG(EMERG, "inet_route: Netlink socket read error. Further polling will be disabled.");
        ev_io_stop(loop, w);
        return;
    }

    /* Trigger polling */
    route_poll_delayed();
}

#define RTNLGRP(x)        ((RTNLGRP_ ## x) > 0 ? 1 << ((RTNLGRP_ ## x) - 1) : 0)

bool route_init(void)
{
    struct sockaddr_nl nladdr;

    /* The netlink socket serves a double purpose as initialization guard */
    if (rt_nlsock >= 0) return true;

    /* Initialize netlink, subscribe to router change events */
    rt_nlsock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
    if (rt_nlsock < 0)
    {
        LOG(ERR, "inet_route: Failed to initialize AF_NETLINK socket.");
        goto error;
    }

    memset(&nladdr, 0, sizeof(nladdr));
    nladdr.nl_family = AF_NETLINK;
    nladdr.nl_pid = 0;
    /* Subscribe to IPVX_ROUTE events -- routing table changes */
    nladdr.nl_groups =
            RTNLGRP(IPV4_ROUTE) |
            RTNLGRP(IPV6_ROUTE) |
            RTNLGRP(NEIGH);

    /* Bind the socket -- this entitles the socket for receiving NetLink events */
    if (bind(rt_nlsock, (struct sockaddr *)&nladdr, sizeof(nladdr)) != 0)
    {
        LOG(EMERG, "inet_route: Error binding netlink socket.");
        goto error;
    }

    /* Initialize the netlink socket watcher */
    ev_io_init(&rt_nlsock_ev, __inet_route_netlink_poll, rt_nlsock, EV_READ);
    ev_io_start(EV_DEFAULT, &rt_nlsock_ev);

    LOG(TRACE, "inet_route: Netlink socket init.");

    return true;

error:
    if (rt_nlsock >= 0) close(rt_nlsock);

    rt_nlsock = -1;
    return false;
}

void route_poll(void)
{
    FILE *frt = NULL;

    char buf[256];
    regex_t re_route;

    LOG(DEBUG, "inet_route: Poll.");

    /*
     * Refresh ARP cache
     */
    route_arp_refresh();

    /* Compile the regex */
    if (regcomp(&re_route, inet_route_regex, REG_EXTENDED) != 0)
    {
        LOG(ERR, "inet_route: Error compiling regex: %s", inet_route_regex);
        return;
    }

    frt = fopen(INET_ROUTE_PROC_NET_ROUTE, "r");
    if (frt == NULL)
    {
        LOG(WARN, "inet_route: Error opening %s", INET_ROUTE_PROC_NET_ROUTE);
        goto error;
    }

    /* Skip the header */
    if (fgets(buf, sizeof(buf), frt) == NULL)
    {
        LOG(ERR, "inet_route: Premature end of %s", INET_ROUTE_PROC_NET_ROUTE);
        goto error;
    }

    route_cache_reset();

    while (fgets(buf, sizeof(buf), frt) != NULL)
    {
        regmatch_t rem[16];
        char r_ifname[C_IFNAME_LEN];
        char r_dest[9];
        char r_gateway[9];
        char r_flags[9];
        char r_metric[6];
        char r_mask[9];
        long flags;
        long metric;

        struct inet_route_state rts;
        memset(&rts, 0, sizeof(rts));

        if (regexec(&re_route, buf, ARRAY_LEN(rem), rem, 0) != 0)
        {
            LOG(WARN, "inet_route: Regular expression exec fail on string: %s", buf);
            continue;
        }

        os_reg_match_cpy(r_ifname, sizeof(r_ifname), buf, rem[1]);
        os_reg_match_cpy(r_dest, sizeof(r_dest), buf, rem[2]);
        os_reg_match_cpy(r_gateway, sizeof(r_gateway), buf, rem[3]);
        os_reg_match_cpy(r_flags, sizeof(r_flags), buf, rem[4]);
        os_reg_match_cpy(r_metric, sizeof(r_metric), buf, rem[5]);
        os_reg_match_cpy(r_mask, sizeof(r_mask), buf, rem[6]);

        if (!inet_ip4addr_from_hexstr(&rts.rts_dst_ipaddr, r_dest))
        {
            LOG(ERR, "inet_route: Invalid destination address: %s -- %s", r_dest, buf);
            continue;
        }

        if (!inet_ip4addr_from_hexstr(&rts.rts_dst_mask, r_mask))
        {
            LOG(ERR, "inet_route: Invalid netmask address: %s -- %s", r_mask, buf);
            continue;
        }

        if (!os_strtoul(r_flags, &flags, 0))
        {
            LOG(ERR, "inet_route: Flags are invalid: %s -- %s", r_flags, buf);
            continue;
        }

        /* XXX: Possibly obsolete */
        if (!os_strtoul(r_metric, &metric, 0))
        {
            LOG(ERR, "inet_route: Metric is invalid: %s -- %s", r_metric, buf);
            continue;
        }

        rts.rts_gw_ipaddr = INET_IP4ADDR_ANY;
        if (flags & RTF_GATEWAY)
        {
            /*
             * This route has a gateway, parse the IP and try to resolve the MAC address
             */
            if (!inet_ip4addr_from_hexstr(&rts.rts_gw_ipaddr, r_gateway))
            {
                LOG(ERR, "inet_route: Invalid gateway address: %s -- %s", r_gateway, buf);
                continue;
            }
        }

        route_cache_update(r_ifname, &rts);
    }

    /* Flush stale entries */
    route_cache_flush();

error:
    regfree(&re_route);
    if (frt != NULL) fclose(frt);

    return;
}

/*
 * Flag all route cache entries for deletion
 */
void route_cache_reset(void)
{
    inet_route_t *rt;
    struct route_state_cache *rsc;

    /* Traverse inet_route objects */
    ds_tree_foreach(&inet_route_list, rt)
    {
        /* Traverse each object's cache */
        ds_tree_foreach(&rt->rt_cache, rsc)
        {
            /* Invalidate the entry */
            rsc->rsc_valid = false;
        }
    }
}

/*
 * Find the inet_route object by @p ifname and update its route state cache
 * If the route state is a new entry, emit a callback
 */
void route_cache_update(const char *ifname, struct inet_route_state *rts)
{
    inet_route_t *rt;
    struct route_state_cache *rsc;

    rt = ds_tree_find(&inet_route_list, (void *)ifname);
    if (rt == NULL)
    {
        /* No inet_route object, skip this entry */
        LOG(DEBUG, "inet_route: %s: No match: "PRI(inet_ip4addr_t)" -> "PRI(inet_ip4addr_t),
                ifname,
                FMT(inet_ip4addr_t, rts->rts_dst_ipaddr),
                FMT(inet_ip4addr_t, rts->rts_gw_ipaddr));
        return;
    }

    bool notify = false;

    /* Find the cache entry in the inet_route object */
    rsc = ds_tree_find(&rt->rt_cache, rts);
    if (rsc == NULL)
    {
        rsc = calloc(1, sizeof(struct route_state_cache));
        memcpy(&rsc->rsc_state, rts, sizeof(rsc->rsc_state));
        ds_tree_insert(&rt->rt_cache, rsc, &rsc->rsc_state);

        LOG(DEBUG, "inet_route: %s: New: "PRI(inet_ip4addr_t)" -> "PRI(inet_ip4addr_t),
                ifname,
                FMT(inet_ip4addr_t, rsc->rsc_state.rts_dst_ipaddr),
                FMT(inet_ip4addr_t, rsc->rsc_state.rts_gw_ipaddr));

        notify = true;
    }

    rsc->rsc_valid = true;

    /* Lookup the MAC address */
    struct route_arp_cache akey;
    struct route_arp_cache *arp;
    inet_macaddr_t gwaddr;
    strscpy(akey.arp_ifname, ifname, sizeof(akey.arp_ifname));
    memcpy(&akey.arp_ipaddr, &rts->rts_gw_ipaddr, sizeof(akey.arp_ipaddr));

    memset(&gwaddr, 0, sizeof(gwaddr));

    /* If we have a valid ARP entry, compare the MAC addresses */
    arp = ds_tree_find(&route_arp_cache, &akey);
    if (arp != NULL)
    {
        memcpy(&gwaddr, &arp->arp_hwaddr, sizeof(gwaddr));
    }

    if (inet_macaddr_cmp(&rsc->rsc_state.rts_gw_hwaddr, &gwaddr) != 0)
    {
        LOG(DEBUG, "inet_route: %s: ARP: "PRI(inet_ip4addr_t)" -> "PRI(inet_ip4addr_t)" |----> "PRI(inet_macaddr_t)" -> "PRI(inet_macaddr_t),
                ifname,
                FMT(inet_ip4addr_t, rsc->rsc_state.rts_dst_ipaddr),
                FMT(inet_ip4addr_t, rsc->rsc_state.rts_gw_ipaddr),
                FMT(inet_macaddr_t, rsc->rsc_state.rts_gw_hwaddr),
                FMT(inet_macaddr_t, gwaddr));

        notify = true;
        memcpy(&rsc->rsc_state.rts_gw_hwaddr, &gwaddr, sizeof(rsc->rsc_state.rts_gw_hwaddr));
    }

    /* Send out notifications */
    if (notify && rt->rt_fn != NULL)
    {
        rt->rt_fn(rt->rt_data, &rsc->rsc_state, false);
    }
}

/*
 * Remove all invalid route entries. Send out notifications.
 */
void route_cache_flush(void)
{
    ds_tree_iter_t iter;
    inet_route_t *rt;
    struct route_state_cache *rsc;

    /* Traverse inet_route objects */
    ds_tree_foreach(&inet_route_list, rt)
    {
        /* Traverse each object's cache */
        ds_tree_foreach_iter(&rt->rt_cache, rsc, &iter)
        {
            if (rsc->rsc_valid) continue;

            ds_tree_iremove(&iter);

            LOG(DEBUG, "inet_route: %s: Del: "PRI(inet_ip4addr_t)" -> "PRI(inet_ip4addr_t),
                    rt->rt_ifname,
                    FMT(inet_ip4addr_t, rsc->rsc_state.rts_dst_ipaddr),
                    FMT(inet_ip4addr_t, rsc->rsc_state.rts_gw_ipaddr));

            /* Send delete notifications */
            if (rt->rt_fn != NULL)
            {
                rt->rt_fn(rt->rt_data, &rsc->rsc_state, true);
            }

            free(rsc);
        }
    }
}

/*
 * ARP cache -- refresh the cache from  /proc/net/arp
 */
void route_arp_refresh(void)
{
    struct route_arp_cache *arp;
    ds_tree_iter_t iter;
    FILE *fa;
    regex_t re_arp;
    char buf[256];

    /*
     * Free old cache
     */
    ds_tree_foreach_iter(&route_arp_cache, arp, &iter)
    {
        ds_tree_iremove(&iter);
        free(arp);
    }

    /*
     * Rebuild the cache
     */

    //LOG(DEBUG, "ARP: Regex for %s is: %s", PROC_NET_ARP, net_arp_re);

    /* Compile the regex */
    if (regcomp(&re_arp, inet_arp_regex, REG_EXTENDED) != 0)
    {
        LOG(ERR, "inet_route: Error compiling ARP regex: %s", inet_arp_regex);
        return;
    }

    /* Open /proc/net/arp for parsing */
    fa = fopen(INET_ROUTE_PROC_NET_ARP, "r");
    if (fa == NULL)
    {
        LOG(ERR, "inet_route: Error opening %s", INET_ROUTE_PROC_NET_ARP);
        goto error;
    }

    /* Skip the header */
    if (fgets(buf, sizeof(buf),  fa) == NULL)
    {
        LOG(ERR, "inet_route: Premature end of %s", INET_ROUTE_PROC_NET_ARP);
        return;
    }

    while (fgets(buf, sizeof(buf), fa) != NULL)
    {
        regmatch_t rem[16];

        char r_ipaddr[C_IP4ADDR_LEN];
        char r_flags[11];
        char r_hwaddr[C_MACADDR_LEN];
        char r_ifname[C_IFNAME_LEN];
        inet_ip4addr_t ipaddr;
        inet_macaddr_t hwaddr;
        long flags;

        if (regexec(&re_arp, buf, ARRAY_LEN(rem), rem, 0) != 0)
        {
            LOG(WARN, "inet_route: ARP regular expression exec fail on string: %s", buf);
            continue;
        }

        os_reg_match_cpy(r_ipaddr, sizeof(r_ipaddr), buf, rem[1]);
        os_reg_match_cpy(r_flags, sizeof(r_flags), buf, rem[2]);
        os_reg_match_cpy(r_hwaddr, sizeof(r_hwaddr), buf, rem[3]);
        os_reg_match_cpy(r_ifname, sizeof(r_ifname), buf, rem[4]);

        if (!os_strtoul(r_flags, &flags, 16))
        {
            LOG(WARN, "inet_route: Invalid flags: %s. Ignoring ARP cache entry: %s", r_flags, buf);
            continue;
        }

        /* ATF_COM (0x2) is set when the ARP cache entry was resolved and is complete. We should ignore incomplete entries. */
        if ((flags & ATF_COM) == 0)
        {
            continue;
        }


        if (!inet_ip4addr_fromstr(&ipaddr, r_ipaddr))
        {
            LOG(ERR, "inet_route: Invalid MAC address: %s. Ignoring ARP cache entry -- %s", r_hwaddr, buf);
            continue;
        }

        if (!inet_macaddr_fromstr(&hwaddr, r_hwaddr))
        {
            LOG(ERR, "inet_route: Invalid MAC address: %s. Ignoring ARP cache entry -- %s", r_hwaddr, buf);
            continue;
        }

        arp = calloc(1, sizeof(struct route_arp_cache));

        if (strscpy(arp->arp_ifname, r_ifname, sizeof(arp->arp_ifname)) < 0)
        {
            LOG(ERR, "inet_route: Interface name too long: %s", r_ifname);
            free(arp);
            continue;
        }

        memcpy(&arp->arp_ipaddr, &ipaddr, sizeof(arp->arp_ipaddr));
        memcpy(&arp->arp_hwaddr, &hwaddr, sizeof(arp->arp_hwaddr));

        ds_tree_insert(&route_arp_cache, arp, arp);
    }

error:
    if (fa != NULL) fclose(fa);

    regfree(&re_arp);
}

/* Index inet_route_state by ip/mask and gateway ip address */
int route_state_cmp(void *_a, void *_b)
{
    int rc;

    struct inet_route_state *a = _a;
    struct inet_route_state *b = _b;

    rc = inet_ip4addr_cmp(&a->rts_dst_ipaddr, &b->rts_dst_ipaddr);
    if (rc != 0) return rc;

    rc = inet_ip4addr_cmp(&a->rts_dst_mask, &b->rts_dst_mask);
    if (rc != 0) return rc;

    rc = inet_ip4addr_cmp(&a->rts_gw_ipaddr, &b->rts_gw_ipaddr);
    if (rc != 0) return rc;

    return 0;
}

/* Index a struct route_arp_cache structure by arp_ipaddr and arp_ifname */
int route_arp_cmp(void *_a, void *_b)
{
    struct route_arp_cache *a = _a;
    struct route_arp_cache *b = _b;

    int rc;

    rc = strcmp(a->arp_ifname, b->arp_ifname);
    if (rc != 0) return rc;

    rc = inet_ip4addr_cmp(&a->arp_ipaddr, &b->arp_ipaddr);
    if (rc != 0) return rc;

    return 0;
}
