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

#include <unistd.h>
#include <regex.h>

#include "log.h"
#include "util.h"
#include "os_util.h"
#include "os_regex.h"
#include "evx.h"

#include "lnx_route.h"

/* Debounce period - 100ms */
#define LNX_ROUTE_POLL_DEBOUNCE_MS 100
#define LNX_ROUTE_PROC_NET_ROUTE   "/proc/net/route"
#define LNX_ROUTE_PROC_NET_ARP     "/proc/net/arp"

static const char lnx_route_regex[] = "^"
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

static const char lnx_route_arp_regex[] = "^"
    RE_GROUP(RE_IPADDR) RE_SPACE        /* IP address */
    RE_XNUM RE_SPACE                    /* HW type */
    RE_GROUP(RE_XNUM) RE_SPACE          /* Flags */
    RE_GROUP(RE_MAC) RE_SPACE           /* HW address */
    "\\*" RE_SPACE                      /* Mask */
    RE_GROUP(RE_IFNAME);

/*
 * Routing table state cache -- this structure is indexed by rsc_state
 */
struct lnx_route_state_cache
{
    struct osn_route_status     rsc_state;                  /* Current route state */
    bool                        rsc_valid;                  /* Invalid cache entries will be deleted by lnx_route_flush() */
    ds_tree_node_t              rsc_tnode;                  /* Red-black tree node */
};

/*
 * ARP cache structure -- this structure is indexed by arp_ipaddr and arp_ifname
 */
struct lnx_route_arp_cache
{
    char                        arp_ifname[C_IFNAME_LEN];
    ds_tree_node_t              arp_tnode;
    osn_ip_addr_t               arp_ipaddr;
    osn_mac_addr_t              arp_hwaddr;
};

#define LNX_ROUTE_ARP_CACHE_INIT (struct lnx_route_arp_cache)   \
{                                                               \
    .arp_hwaddr = OSN_MAC_ADDR_INIT,                            \
    .arp_ipaddr = OSN_IP_ADDR_INIT,                             \
}

/*
 * Private functions
 */
static void lnx_route_netlink_poll(lnx_netlink_t *nl, uint64_t event, const char *ifname);
static void lnx_route_poll(void);
static void lnx_route_cache_reset(void);
static void lnx_route_cache_update(const char *ifname, struct osn_route_status *rts);
static void lnx_route_cache_flush(void);
static void lnx_route_arp_refresh(void);
static ds_key_cmp_t lnx_route_state_cmp;
static ds_key_cmp_t lnx_route_arp_cmp;
static bool route_osn_ip_addr_from_hexstr(osn_ip_addr_t *ip, const char *str);

/* Global list of route objects, the primary key is the interface name */
static ds_tree_t lnx_route_list = DS_TREE_INIT(ds_str_cmp, lnx_route_t, rt_tnode);
static ds_tree_t lnx_route_arp_cache = DS_TREE_INIT(lnx_route_arp_cmp, struct lnx_route_arp_cache, arp_tnode);

/*
 * ===========================================================================
 *  Public interface
 * ===========================================================================
 */

/*
 * Create a new instance of the osn_route object
 */
bool lnx_route_init(lnx_route_t *self, const char *ifname)
{
    if (strscpy(self->rt_ifname, ifname, sizeof(self->rt_ifname)) < 0)
    {
        LOG(ERR, "route: %s: Interface name too long.", ifname);
        goto error;
    }

    /* Initialize the routing table cache for this entry */
    ds_tree_init(&self->rt_cache, lnx_route_state_cmp, struct lnx_route_state_cache, rsc_tnode);

    /* Add this instance to the global list */
    ds_tree_insert(&lnx_route_list, self, self->rt_ifname);

    lnx_netlink_init(&self->rt_nl, lnx_route_netlink_poll);
    lnx_netlink_set_ifname(&self->rt_nl, self->rt_ifname);
    lnx_netlink_set_events(&self->rt_nl, LNX_NETLINK_IP4ROUTE | LNX_NETLINK_IP4NEIGH);
    lnx_netlink_start(&self->rt_nl);

    lnx_route_poll();

    return self;

error:
    return false;
}

/*
 * Delete a osn_route object
 */
bool lnx_route_fini(lnx_route_t *self)
{
    ds_tree_iter_t iter;
    struct lnx_route_state_cache *rsc;

    lnx_netlink_fini(&self->rt_nl);

    /* Remove all cache entries associated with this object */
    ds_tree_foreach_iter(&self->rt_cache, rsc, &iter)
    {
        ds_tree_iremove(&iter);
        free(rsc);
    }

    ds_tree_remove(&lnx_route_list, self);

    return true;
}

/*
 * Set the callback that will receive route table change notifications.
 *
 * This is currently the only supported method -- this may change in the future
 */
bool lnx_route_status_notify(lnx_route_t *self, lnx_route_status_fn_t *func)
{
    self->rt_fn = func;
    return true;
}

/*
 * ===========================================================================
 *  Private functions
 * ===========================================================================
 */
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
void lnx_route_netlink_poll(lnx_netlink_t *nl, uint64_t event, const char *ifname)
{
    (void)nl;
    (void)event;
    (void)ifname;

    /* Trigger polling */
    lnx_route_poll();
}

bool route_osn_ip_addr_from_hexstr(osn_ip_addr_t *ip, const char *str)
{
    char s_addr[OSN_IP_ADDR_LEN];
    long l_addr;

    if (!os_strtoul((char *)str, &l_addr, 16))
    {
        return false;
    }

    l_addr = ntohl(l_addr);

    snprintf(s_addr, sizeof(s_addr), "%ld.%ld.%ld.%ld",
            (l_addr >> 24) & 0xFF,
            (l_addr >> 16) & 0xFF,
            (l_addr >> 8) & 0xFF,
            (l_addr >> 0) & 0xFF);

    /*
     * This route has a gateway, parse the IP and try to resolve the MAC address
     */
    return osn_ip_addr_from_str(ip, s_addr);
}

void lnx_route_poll(void)
{
    FILE *frt = NULL;

    char buf[256];
    regex_t re_route;

    LOG(DEBUG, "route: Poll.");

    /*
     * Refresh ARP cache
     */
    lnx_route_arp_refresh();

    /* Compile the regex */
    if (regcomp(&re_route, lnx_route_regex, REG_EXTENDED) != 0)
    {
        LOG(ERR, "route: Error compiling regex: %s", lnx_route_regex);
        return;
    }

    frt = fopen(LNX_ROUTE_PROC_NET_ROUTE, "r");
    if (frt == NULL)
    {
        LOG(WARN, "route: Error opening %s", LNX_ROUTE_PROC_NET_ROUTE);
        goto error;
    }

    /* Skip the header */
    if (fgets(buf, sizeof(buf), frt) == NULL)
    {
        LOG(NOTICE, "route: Premature end of %s", LNX_ROUTE_PROC_NET_ROUTE);
        goto error;
    }

    lnx_route_cache_reset();

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

        struct osn_route_status rts = OSN_ROUTE_STATUS_INIT;

        if (regexec(&re_route, buf, ARRAY_LEN(rem), rem, 0) != 0)
        {
            LOG(WARN, "route: Regular expression exec fail on string: %s", buf);
            continue;
        }

        os_reg_match_cpy(r_ifname, sizeof(r_ifname), buf, rem[1]);
        os_reg_match_cpy(r_dest, sizeof(r_dest), buf, rem[2]);
        os_reg_match_cpy(r_gateway, sizeof(r_gateway), buf, rem[3]);
        os_reg_match_cpy(r_flags, sizeof(r_flags), buf, rem[4]);
        os_reg_match_cpy(r_metric, sizeof(r_metric), buf, rem[5]);
        os_reg_match_cpy(r_mask, sizeof(r_mask), buf, rem[6]);

        if (!route_osn_ip_addr_from_hexstr(&rts.rts_dst_ipaddr, r_dest))
        {
            LOG(ERR, "route: Invalid destination address: %s -- %s", r_dest, buf);
            continue;
        }

        if (!route_osn_ip_addr_from_hexstr(&rts.rts_dst_mask, r_mask))
        {
            LOG(ERR, "route: Invalid netmask address: %s -- %s", r_mask, buf);
            continue;
        }

        if (!os_strtoul(r_flags, &flags, 0))
        {
            LOG(ERR, "route: Flags are invalid: %s -- %s", r_flags, buf);
            continue;
        }

        if (!os_strtoul(r_metric, &metric, 0))
        {
            LOG(ERR, "route: Metric is invalid: %s -- %s", r_metric, buf);
            continue;
        }

        rts.rts_gw_ipaddr = OSN_IP_ADDR_INIT;
        if (flags & RTF_GATEWAY)
        {
            /*
             * This route has a gateway, parse the IP and try to resolve the MAC address
             */
            if (!route_osn_ip_addr_from_hexstr(&rts.rts_gw_ipaddr, r_gateway))
            {
                LOG(ERR, "route: Invalid gateway address %s.", r_gateway);
                continue;
            }
        }

        lnx_route_cache_update(r_ifname, &rts);
    }

    /* Flush stale entries */
    lnx_route_cache_flush();

error:
    regfree(&re_route);
    if (frt != NULL) fclose(frt);

    return;
}

/*
 * Flag all route cache entries for deletion
 */
void lnx_route_cache_reset(void)
{
    lnx_route_t *rt;
    struct lnx_route_state_cache *rsc;

    /* Traverse osn_route objects */
    ds_tree_foreach(&lnx_route_list, rt)
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
 * Find the osn_route object by @p ifname and update its route state cache
 * If the route state is a new entry, emit a callback
 */
void lnx_route_cache_update(const char *ifname, struct osn_route_status *rts)
{
    lnx_route_t *rt;
    struct lnx_route_state_cache *rsc;

    rt = ds_tree_find(&lnx_route_list, (void *)ifname);
    if (rt == NULL)
    {
        /* No lnx_route object, skip this entry */
        LOG(DEBUG, "route: %s: No match: "PRI_osn_ip_addr" -> "PRI_osn_ip_addr,
                ifname,
                FMT_osn_ip_addr(rts->rts_dst_ipaddr),
                FMT_osn_ip_addr(rts->rts_gw_ipaddr));
        return;
    }

    bool notify = false;

    /* Find the cache entry in the inet_route object */
    rsc = ds_tree_find(&rt->rt_cache, rts);
    if (rsc == NULL)
    {
        rsc = calloc(1, sizeof(struct lnx_route_state_cache));
        memcpy(&rsc->rsc_state, rts, sizeof(rsc->rsc_state));
        ds_tree_insert(&rt->rt_cache, rsc, &rsc->rsc_state);

        LOG(DEBUG, "route: %s: New: "PRI_osn_ip_addr" -> "PRI_osn_ip_addr,
                ifname,
                FMT_osn_ip_addr(rsc->rsc_state.rts_dst_ipaddr),
                FMT_osn_ip_addr(rsc->rsc_state.rts_gw_ipaddr));

        notify = true;
    }

    rsc->rsc_valid = true;

    /* Lookup the MAC address */
    struct lnx_route_arp_cache akey;
    struct lnx_route_arp_cache *arp;

    osn_mac_addr_t gwaddr;
    strscpy(akey.arp_ifname, ifname, sizeof(akey.arp_ifname));
    memcpy(&akey.arp_ipaddr, &rts->rts_gw_ipaddr, sizeof(akey.arp_ipaddr));

    gwaddr = OSN_MAC_ADDR_INIT;
    /* If we have a valid ARP entry, compare the MAC addresses */
    arp = ds_tree_find(&lnx_route_arp_cache, &akey);
    if (arp != NULL)
    {
        gwaddr = arp->arp_hwaddr;
    }

    if (osn_mac_addr_cmp(&rsc->rsc_state.rts_gw_hwaddr, &gwaddr) != 0)
    {
        LOG(DEBUG, "route: %s: ARP: "PRI_osn_ip_addr" -> "PRI_osn_ip_addr" |----> "PRI_osn_mac_addr" -> "PRI_osn_mac_addr,
                ifname,
                FMT_osn_ip_addr(rsc->rsc_state.rts_dst_ipaddr),
                FMT_osn_ip_addr(rsc->rsc_state.rts_gw_ipaddr),
                FMT_osn_mac_addr(rsc->rsc_state.rts_gw_hwaddr),
                FMT_osn_mac_addr(gwaddr));

        notify = true;
        memcpy(&rsc->rsc_state.rts_gw_hwaddr, &gwaddr, sizeof(rsc->rsc_state.rts_gw_hwaddr));
    }

    /* Send out notifications */
    if (notify && rt->rt_fn != NULL)
    {
        rt->rt_fn(rt, &rsc->rsc_state, false);
    }
}

/*
 * Remove all invalid route entries. Send out notifications.
 */
void lnx_route_cache_flush(void)
{
    ds_tree_iter_t iter;
    lnx_route_t *rt;
    struct lnx_route_state_cache *rsc;

    /* Traverse route objects */
    ds_tree_foreach(&lnx_route_list, rt)
    {
        /* Traverse each object's cache */
        ds_tree_foreach_iter(&rt->rt_cache, rsc, &iter)
        {
            if (rsc->rsc_valid) continue;

            ds_tree_iremove(&iter);

            LOG(DEBUG, "route: %s: Del: "PRI_osn_ip_addr" -> "PRI_osn_ip_addr,
                    rt->rt_ifname,
                    FMT_osn_ip_addr(rsc->rsc_state.rts_dst_ipaddr),
                    FMT_osn_ip_addr(rsc->rsc_state.rts_gw_ipaddr));

            /* Send delete notifications */
            if (rt->rt_fn != NULL)
            {
                rt->rt_fn(rt, &rsc->rsc_state, true);
            }

            free(rsc);
        }
    }
}

/*
 * ARP cache -- refresh the cache from  /proc/net/arp
 */
void lnx_route_arp_refresh(void)
{
    struct lnx_route_arp_cache *arp;
    ds_tree_iter_t iter;
    FILE *fa;
    regex_t re_arp;
    char buf[256];

    /*
     * Free old cache
     */
    ds_tree_foreach_iter(&lnx_route_arp_cache, arp, &iter)
    {
        ds_tree_iremove(&iter);
        free(arp);
    }

    /*
     * Rebuild the cache
     */

    //LOG(DEBUG, "ARP: Regex for %s is: %s", PROC_NET_ARP, net_arp_re);

    /* Compile the regex */
    if (regcomp(&re_arp, lnx_route_arp_regex, REG_EXTENDED) != 0)
    {
        LOG(ERR, "route: Error compiling ARP regex: %s", lnx_route_arp_regex);
        return;
    }

    /* Open /proc/net/arp for parsing */
    fa = fopen(LNX_ROUTE_PROC_NET_ARP, "r");
    if (fa == NULL)
    {
        LOG(ERR, "route: Error opening %s", LNX_ROUTE_PROC_NET_ARP);
        goto error;
    }

    /* Skip the header */
    if (fgets(buf, sizeof(buf),  fa) == NULL)
    {
        LOG(NOTICE, "route: Premature end of %s", LNX_ROUTE_PROC_NET_ARP);
        return;
    }

    while (fgets(buf, sizeof(buf), fa) != NULL)
    {
        regmatch_t rem[16];

        char r_ipaddr[C_IP4ADDR_LEN];
        char r_flags[11];
        char r_hwaddr[C_MACADDR_LEN];
        char r_ifname[C_IFNAME_LEN];
        osn_ip_addr_t ipaddr;
        osn_mac_addr_t hwaddr;
        long flags;

        if (regexec(&re_arp, buf, ARRAY_LEN(rem), rem, 0) != 0)
        {
            LOG(WARN, "route: ARP regular expression exec fail on string: %s", buf);
            continue;
        }

        os_reg_match_cpy(r_ipaddr, sizeof(r_ipaddr), buf, rem[1]);
        os_reg_match_cpy(r_flags, sizeof(r_flags), buf, rem[2]);
        os_reg_match_cpy(r_hwaddr, sizeof(r_hwaddr), buf, rem[3]);
        os_reg_match_cpy(r_ifname, sizeof(r_ifname), buf, rem[4]);

        if (!os_strtoul(r_flags, &flags, 16))
        {
            LOG(WARN, "route: Invalid flags: %s. Ignoring ARP cache entry: %s", r_flags, buf);
            continue;
        }

        /* ATF_COM (0x2) is set when the ARP cache entry was resolved and is complete. We should ignore incomplete entries. */
        if ((flags & ATF_COM) == 0)
        {
            continue;
        }


        if (!osn_ip_addr_from_str(&ipaddr, r_ipaddr))
        {
            LOG(ERR, "route: Invalid MAC address: %s. Ignoring ARP cache entry -- %s", r_hwaddr, buf);
            continue;
        }

        if (!osn_mac_addr_from_str(&hwaddr, r_hwaddr))
        {
            LOG(ERR, "route: Invalid MAC address: %s. Ignoring ARP cache entry -- %s", r_hwaddr, buf);
            continue;
        }

        arp = malloc(sizeof(struct lnx_route_arp_cache));
        *arp = LNX_ROUTE_ARP_CACHE_INIT;

        if (strscpy(arp->arp_ifname, r_ifname, sizeof(arp->arp_ifname)) < 0)
        {
            LOG(ERR, "route: Interface name too long: %s", r_ifname);
            free(arp);
            continue;
        }

        memcpy(&arp->arp_ipaddr, &ipaddr, sizeof(arp->arp_ipaddr));
        memcpy(&arp->arp_hwaddr, &hwaddr, sizeof(arp->arp_hwaddr));

        ds_tree_insert(&lnx_route_arp_cache, arp, arp);
    }

error:
    if (fa != NULL) fclose(fa);

    regfree(&re_arp);
}

/* Index osn_route_state by ip/mask and gateway ip address */
int lnx_route_state_cmp(void *_a, void *_b)
{
    int rc;

    struct osn_route_status *a = _a;
    struct osn_route_status *b = _b;

    rc = osn_ip_addr_cmp(&a->rts_dst_ipaddr, &b->rts_dst_ipaddr);
    if (rc != 0) return rc;

    rc = osn_ip_addr_cmp(&a->rts_dst_mask, &b->rts_dst_mask);
    if (rc != 0) return rc;

    rc = osn_ip_addr_cmp(&a->rts_gw_ipaddr, &b->rts_gw_ipaddr);
    if (rc != 0) return rc;

    return 0;
}

/* Index a struct lnx_route_arp_cache structure by arp_ipaddr and arp_ifname */
int lnx_route_arp_cmp(void *_a, void *_b)
{
    struct lnx_route_arp_cache *a = _a;
    struct lnx_route_arp_cache *b = _b;

    int rc;

    rc = strcmp(a->arp_ifname, b->arp_ifname);
    if (rc != 0) return rc;

    rc = osn_ip_addr_cmp(&a->arp_ipaddr, &b->arp_ipaddr);
    if (rc != 0) return rc;

    return 0;
}
