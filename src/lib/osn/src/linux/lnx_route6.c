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

#include <regex.h>
#include <unistd.h>

#include "ds.h"
#include "evx.h"
#include "kconfig.h"
#include "log.h"
#include "memutil.h"
#include "os_regex.h"
#include "os_util.h"
#include "util.h"

#include "lnx_route6.h"
#include "lnx_route6_state.h"

/* Debounce period - 100ms */
#define LNX_ROUTE6_POLL_DEBOUNCE_MS 100

/*
 * Routing table state cache -- this structure is indexed by rsc_state
 */
struct lnx_route6_state_cache
{
    struct osn_route6_status rsc_state; /* Current route state */
    bool rsc_valid;                     /* Invalid cache entries will be deleted by lnx_route6_flush() */
    ds_tree_node_t rsc_tnode;           /* Red-black tree node */
};

/*
 * Private functions
 */
static void lnx_route6_netlink_event_cb(lnx_netlink_t *nl, uint64_t event, const char *ifname);
static void lnx_route6_poll(void);
static void lnx_route6_cache_reset(void);
static void lnx_route6_cache_update(const char *ifname, struct osn_route6_status *rts);
static void lnx_route6_cache_flush(void);
static void lnx_route6_cache_free(lnx_route6_t *rt, struct lnx_route6_state_cache *rsc);
static ds_key_cmp_t lnx_route6_state_cmp;
static void lnx_route6_state_poll_ev(struct ev_loop *loop, struct ev_debounce *ev, int revent);

/* Global list of route objects, the primary key is the interface name */
static ds_tree_t lnx_route6_list = DS_TREE_INIT(ds_str_cmp, lnx_route6_t, rt_tnode);

/* Global EV debounce object for route polling */
static ev_debounce lnx_ev_debounce;

/*
 * ===========================================================================
 *  Public interface
 * ===========================================================================
 */

/*
 * Create a new instance of the osn_route6 object
 */
bool lnx_route6_init(lnx_route6_t *self, const char *ifname)
{
    TRACE("%s", ifname);
    static bool debounce_inited = false;

    if (strscpy(self->rt_ifname, ifname, sizeof(self->rt_ifname)) < 0)
    {
        LOG(ERR, "route6: %s: Interface name too long.", ifname);
        goto error;
    }

    /* Initialize the routing table cache for this entry */
    ds_tree_init(&self->rt_cache, lnx_route6_state_cmp, struct lnx_route6_state_cache, rsc_tnode);

    /* Add this instance to the global list */
    ds_tree_insert(&lnx_route6_list, self, self->rt_ifname);

    lnx_netlink_init(&self->rt_nl, lnx_route6_netlink_event_cb);
    lnx_netlink_set_ifname(&self->rt_nl, self->rt_ifname);
    lnx_netlink_set_events(&self->rt_nl, LNX_NETLINK_IP6ROUTE | LNX_NETLINK_IP6NEIGH);
    lnx_netlink_start(&self->rt_nl);

    /* Initialize debouncing for route state polling (to poll only once if
     * multiple subsequent route state updades triggered in short time frame): */
    if (!debounce_inited)
    {
        ev_debounce_init(&lnx_ev_debounce, lnx_route6_state_poll_ev, LNX_ROUTE6_POLL_DEBOUNCE_MS / 1000);

        debounce_inited = true;
    }

    /* Initial poll: */
    lnx_route6_poll();

    return self;

error:
    return false;
}

/*
 * Delete a osn_route6 object
 */
bool lnx_route6_fini(lnx_route6_t *self)
{
    ds_tree_iter_t iter;
    struct lnx_route6_state_cache *rsc;

    lnx_netlink_fini(&self->rt_nl);

    /* Remove all cache entries associated with this object */
    ds_tree_foreach_iter(&self->rt_cache, rsc, &iter)
    {
        ds_tree_iremove(&iter);
        lnx_route6_cache_free(self, rsc);
    }

    ds_tree_remove(&lnx_route6_list, self);

    if (ds_tree_len(&lnx_route6_list) == 0)
    {
        ev_debounce_stop(EV_DEFAULT, &lnx_ev_debounce);
    }

    return true;
}

/*
 * Set the callback that will receive route table change notifications.
 *
 * This is currently the only supported method -- this may change in the future
 */
bool lnx_route6_status_notify(lnx_route6_t *self, lnx_route6_status_fn_t *func)
{
    self->rt_fn = func;
    return true;
}

/*
 * ===========================================================================
 *  Private functions
 * ===========================================================================
 */

/* In the EV debounce event we do the actual routes polling. */
static void lnx_route6_state_poll_ev(struct ev_loop *loop, struct ev_debounce *ev, int revent)
{
    static lnx_route6_state_t *route_state;

    if (route_state == NULL)
    {
        /* Get lnx route state object. */
        route_state = lnx_route6_state_new(lnx_route6_cache_update);
    }

    /* Invalidate currently cached route entries: */
    lnx_route6_cache_reset();

    /* Poll for route states. Existing routes present in our cache will be simply
     * flagged as valid again and no further action taken. New routes will be
     * added into the cache and reported upstream via notification callback. */
    lnx_route6_state_poll(route_state);

    /* Flush stale entries. Routes that have been deleted from the system will
     * have the validity flag set to false at this point and for those we delete
     * the cached entry and send a delete notification callback upstream. */
    lnx_route6_cache_flush();
}

/*
 * Netlink event callback. Called when there's a route state change on the system.
 *
 * In the netlink event callback we trigger the actual polling of routes.
 */
void lnx_route6_netlink_event_cb(lnx_netlink_t *nl, uint64_t event, const char *ifname)
{
    (void)nl;
    (void)event;
    (void)ifname;

    lnx_route6_poll();
}

void lnx_route6_poll(void)
{
    ev_debounce_start(EV_DEFAULT, &lnx_ev_debounce);
}

/*
 * Flag all route cache entries for deletion
 */
void lnx_route6_cache_reset(void)
{
    lnx_route6_t *rt;
    struct lnx_route6_state_cache *rsc;

    /* Traverse osn_route6 objects */
    ds_tree_foreach (&lnx_route6_list, rt)
    {
        /* Traverse each object's cache */
        ds_tree_foreach (&rt->rt_cache, rsc)
        {
            /* Invalidate the entry */
            rsc->rsc_valid = false;
        }
    }
}

/*
 * Convert IPv6 EUI64 to MAC according to RFC2373
 */
bool lnx_route6_ipv6_eui64_to_mac(osn_ip6_addr_t *ip6, osn_mac_addr_t *mac)
{
    struct in6_addr in6 = ip6->ia6_addr;
    // bytes: 8 + 0 1 2  3  4 5 6 7
    // .......... u:m:m:FF:FE:m:m:m
    if (in6.s6_addr[8 + 3] != 0xFF) return false;
    if (in6.s6_addr[8 + 4] != 0xFE) return false;
    // universal/local flag: byte 8+0, bit 1 (7th from left)
    // has to be 1 and has to be inverted when converting to MAC
    if ((in6.s6_addr[8 + 4] & 0x02) != 0x02) return false;
    // EUI64 flags match, convert to MAC
    mac->ma_addr[0] = in6.s6_addr[8 + 0] ^ 0x02;  // invert bit 1
    mac->ma_addr[1] = in6.s6_addr[8 + 1];
    mac->ma_addr[2] = in6.s6_addr[8 + 2];
    mac->ma_addr[3] = in6.s6_addr[8 + 5];
    mac->ma_addr[4] = in6.s6_addr[8 + 6];
    mac->ma_addr[5] = in6.s6_addr[8 + 7];
    return true;
}

/*
 * Find the osn_route6 object by @p ifname and update its route state cache.
 * If the route state is a new entry, emit a callback.
 *
 * If the route has a GW, lookup its MAC address in EUI64.
 */
void lnx_route6_cache_update(const char *ifname, struct osn_route6_status *rts)
{
    lnx_route6_t *rt;
    struct lnx_route6_state_cache *rsc;

    rt = ds_tree_find(&lnx_route6_list, (void *)ifname);
    if (rt == NULL)
    {
        /* No lnx_route6 object, skip this entry */
        LOG(DEBUG,
            "route6: %s: No match: " PRI_osn_ip6_addr " -> " PRI_osn_ip6_addr " (table=%u, metric=%d)",
            ifname,
            FMT_osn_ip6_addr(rts->rts_route.dest),
            FMT_osn_ip6_addr(rts->rts_route.gw),
            rts->rts_route.table,
            rts->rts_route.metric);
        return;
    }

    bool notify = false;

    /* Find the cache entry in the inet_route object */
    rsc = ds_tree_find(&rt->rt_cache, rts);
    if (rsc == NULL)
    {
        rsc = CALLOC(1, sizeof(struct lnx_route6_state_cache));
        memcpy(&rsc->rsc_state, rts, sizeof(rsc->rsc_state));
        ds_tree_insert(&rt->rt_cache, rsc, &rsc->rsc_state);

        LOG(DEBUG,
            "route6: %s: New: " PRI_osn_ip6_addr " -> " PRI_osn_ip6_addr " (table=%u, metric=%d)",
            ifname,
            FMT_osn_ip6_addr(rsc->rsc_state.rts_route.dest),
            FMT_osn_ip6_addr(rsc->rsc_state.rts_route.gw),
            rts->rts_route.table,
            rts->rts_route.metric);

        /* Lookup the MAC address */
        rsc->rsc_state.rts_gw_hwaddr = OSN_MAC_ADDR_INIT;
        if (rts->rts_route.gw_valid)
        {
            lnx_route6_ipv6_eui64_to_mac(&rts->rts_route.gw, &rsc->rsc_state.rts_gw_hwaddr);
        }

        notify = true;
    }

    rsc->rsc_valid = true;

    /* Send out notifications */
    if (notify && rt->rt_fn != NULL)
    {
        rt->rt_fn(rt, &rsc->rsc_state, false);
    }
}

static void lnx_route6_cache_free(lnx_route6_t *rt, struct lnx_route6_state_cache *rsc)
{
    LOG(DEBUG,
        "route6: %s: Del: " PRI_osn_ip6_addr " -> " PRI_osn_ip6_addr " (table=%u, metric=%d)",
        rt->rt_ifname,
        FMT_osn_ip6_addr(rsc->rsc_state.rts_route.dest),
        FMT_osn_ip6_addr(rsc->rsc_state.rts_route.gw),
        rsc->rsc_state.rts_route.table,
        rsc->rsc_state.rts_route.metric);

    /* Send delete notifications */
    if (rt->rt_fn != NULL)
    {
        rt->rt_fn(rt, &rsc->rsc_state, true);
    }

    FREE(rsc);
}

/*
 * Remove all invalid route entries. Send out notifications.
 */
void lnx_route6_cache_flush(void)
{
    ds_tree_iter_t iter;
    lnx_route6_t *rt;
    struct lnx_route6_state_cache *rsc;

    /* Traverse route objects */
    ds_tree_foreach (&lnx_route6_list, rt)
    {
        /* Traverse each object's cache */
        ds_tree_foreach_iter(&rt->rt_cache, rsc, &iter)
        {
            if (rsc->rsc_valid) continue;

            ds_tree_iremove(&iter);
            lnx_route6_cache_free(rt, rsc);
        }
    }
}

/* Index osn_route6_state by ip/mask and gateway ip address as well as
 * routing table ID and metric. */
int lnx_route6_state_cmp(const void *_a, const void *_b)
{
    int rc;

    const struct osn_route6_status *a = _a;
    const struct osn_route6_status *b = _b;

    if (a->rts_route.table < b->rts_route.table)
    {
        return -1;
    }
    else if (a->rts_route.table > b->rts_route.table)
    {
        return 1;
    }

    rc = osn_ip6_addr_nolft_cmp(&a->rts_route.dest, &b->rts_route.dest);
    if (rc != 0) return rc;

    rc = osn_ip6_addr_nolft_cmp(&a->rts_route.gw, &b->rts_route.gw);
    if (rc != 0) return rc;

    rc = a->rts_route.metric - b->rts_route.metric;
    if (rc != 0) return rc;

    return 0;
}
