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

#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>

#include "log.h"
#include "util.h"
#include "execsh.h"
#include "memutil.h"
#include "os_time.h"

#include "lnx_ip6.h"

/*
 * Specify the increment by which dynamic arrays are grown each time they
 * require more space
 */
#define LNX_IP6_REALLOC_GROW    16

/*
 * This value specifies the minimum time interval between calls to
 * `ip -6 neigh show` and `ip -6 addr show`
 */
#define LNX_IP6_POLL_TIME 0.5

/*
 * structure used to cache IPv6 neighbors (ip -6 neigh show)
 */
struct lnx_ip6_neigh
{
    char                    ifname[C_IFNAME_LEN];       /* Interface name */
    osn_mac_addr_t          macaddr;                    /* MAC address */
    osn_ip6_addr_t          ip6addr;                    /* IPv6 address */
};

/*
 * Structure used to cache IPv6 addresses (ip -6 addr show)
 */
struct lnx_ip6_addr
{
    char                    ifname[C_IFNAME_LEN];       /* Interface name */
    osn_ip6_addr_t          ip6addr;                    /* IPv6 address */
};

struct lnx_ip6_addr_node
{
    osn_ip6_addr_t          addr;                       /* IPv6 address */
    bool                    enabled;                    /* IP address should be added, otherwise removed */
    bool                    active;                     /* IP address is applied to the system */
    ds_tree_node_t          tnode;                      /* Tree node */
};

static bool lnx_ip6_addr_flush(lnx_ip6_t *self);
static bool lnx_ip6_ipaddr_parse(void *_self, int type, const char *line);
static void lnx_ip6_status_ipaddr_update(lnx_ip6_t *self);
static bool lnx_ip6_neigh_parse(void *_self, int type, const char *line);
static void lnx_ip6_status_neigh_update(lnx_ip6_t *self);
static lnx_netlink_fn_t lnx_ip6_nl_fn;

static struct lnx_ip6_neigh *lnx_ip6_neigh_table = NULL;
static struct lnx_ip6_neigh *lnx_ip6_neigh_table_e = NULL;

static struct lnx_ip6_addr *lnx_ip6_addr_table = NULL;
static struct lnx_ip6_addr *lnx_ip6_addr_table_e = NULL;


/*
 * Initialize new Linux IPv6 object
 */
bool lnx_ip6_init(lnx_ip6_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));

    if (STRSCPY(self->ip6_ifname, ifname) < 0)
    {
        LOG(ERR, "ip6: Interface name too long: %s", ifname);
        goto error;
    }

    ds_tree_init(
            &self->ip6_addr_list,
            osn_ip6_addr_cmp,
            struct lnx_ip6_addr_node, tnode);

    ds_tree_init(
            &self->ip6_dns_list,
            osn_ip6_addr_cmp,
            struct lnx_ip6_addr_node, tnode);

    /* Initialize the netlink event object */
    lnx_netlink_init(&self->ip6_nl, lnx_ip6_nl_fn);
    lnx_netlink_set_events(&self->ip6_nl, LNX_NETLINK_IP6ADDR | LNX_NETLINK_IP6NEIGH);
    lnx_netlink_set_ifname(&self->ip6_nl, ifname);

    /* Start listening to netlink events */
    lnx_netlink_start(&self->ip6_nl);

    return self;

error:
    if (self != NULL) free(self);

    return NULL;
}

/*
 * Destroy Linux IPv6 object
 */
bool lnx_ip6_fini(lnx_ip6_t *self)
{
    struct lnx_ip6_addr_node *node;
    ds_tree_iter_t iter;

    /* Stop the netlink event listener */
    lnx_netlink_stop(&self->ip6_nl);

    /* Free status structure */
    if (self->ip6_status.is6_addr != NULL)
    {
        free(self->ip6_status.is6_addr);
    }

    if (self->ip6_status.is6_neigh != NULL)
    {
        free(self->ip6_status.is6_neigh);
    }

    if (self->ip6_status.is6_dns != NULL)
    {
        free(self->ip6_status.is6_dns);
    }

    /* Remove all active addresses */
    lnx_ip6_addr_flush(self);

    /* Free list of IPv6 address */
    ds_tree_foreach_iter(&self->ip6_addr_list, node, &iter)
    {
        ds_tree_iremove(&iter);
        free(node);
    }

    /* Free list of DNSv6 addresses */
    ds_tree_foreach_iter(&self->ip6_dns_list, node, &iter)
    {
        ds_tree_iremove(&iter);
        free(node);
    }

    return true;
}

static char ip6_del_cmd[] = _S(ip -6 address del "$2/$3" dev "$1");
static char ip6_add_cmd[] = _S(ip -6 address add "$2/$3" dev "$1");

/*
 * Flush all configured IPv6 interfaces
 *
 * Note: A simple ip -6 addr flush would in theory work. The problem with that command is
 * that it flushes, amongst others, the link-local address. In order to work around that
 * we have to keep track of IP addresses that are to be removed.
 */
bool lnx_ip6_addr_flush(lnx_ip6_t *self)
{
    struct lnx_ip6_addr_node *node;
    char saddr[OSN_IP6_ADDR_LEN];
    char spref[C_INT32_LEN];
    ds_tree_iter_t iter;
    int rc;

    ds_tree_foreach_iter(&self->ip6_addr_list, node, &iter)
    {
        /*
         *  active |  enabled |  action
         *  -------------------------------
         *  true      true       remove ip,active=false
         *  true      false      remove ip,remove from list
         *  false     true       nop
         *  false     false      remove from list
         */
        if (node->active)
        {
            /* Remove IP from the system */
            //snprintf(saddr, sizeof(saddr),  PRI(in6_addr), FMT(in6_addr, node->addr.ia6_addr));
            if (inet_ntop(AF_INET6, &node->addr.ia6_addr, saddr, sizeof(saddr)) == NULL)
            {
                LOG(ERR, "ip6: %s: Unable to convert IPv6 address, skipping delete.", self->ip6_ifname);
            }

            snprintf(spref, sizeof(spref), "%d", node->addr.ia6_prefix);

            rc = execsh_log(LOG_SEVERITY_DEBUG, ip6_del_cmd, self->ip6_ifname, saddr, spref);
            if (rc != 0)
            {
                LOG(WARN, "ip6: %s: Unable to remove IPv6 address: "PRI_osn_ip6_addr,
                        self->ip6_ifname,
                        FMT_osn_ip6_addr(node->addr));
            }

            node->active = false;
        }

        /* Remove element from the list */
        if (!node->enabled)
        {
            ds_tree_iremove(&iter);
            free(node);
        }
    }

    return true;
}

/*
 * Apply configuration to system
 */
bool lnx_ip6_apply(lnx_ip6_t *self)
{
    struct lnx_ip6_addr_node *node;
    char saddr[C_IPV6ADDR_LEN];
    char spref[C_INT32_LEN];
    int rc;

    /* Start by issuing a flush */
    lnx_ip6_addr_flush(self);

    ds_tree_foreach(&self->ip6_addr_list, node)
    {
        if (inet_ntop(AF_INET6, &node->addr.ia6_addr, saddr, sizeof(saddr)) == NULL)
        {
            LOG(ERR, "ip6: %s: Unable to convert IPv6 address, skipping add.", self->ip6_ifname);
            continue;
        }

        snprintf(spref, sizeof(spref), "%d", node->addr.ia6_prefix);

        rc = execsh_log(LOG_SEVERITY_DEBUG, ip6_add_cmd, self->ip6_ifname, saddr, spref);
        if (rc != 0)
        {
            LOG(WARN, "ip6: %s: Unable to add IPv6 address: "PRI_osn_ip6_addr,
                    self->ip6_ifname,
                    FMT_osn_ip6_addr(node->addr));
        }

        node->active = true;
    }

    return true;
}

bool lnx_ip6_addr_add(lnx_ip6_t *self, const osn_ip6_addr_t *addr)
{
    struct lnx_ip6_addr_node *node;

    node = ds_tree_find(&self->ip6_addr_list, (void *)&addr->ia6_addr);
    if (node == NULL)
    {
        node = calloc(1, sizeof(*node));
        node->addr = *addr;
        ds_tree_insert(&self->ip6_addr_list, node, &node->addr.ia6_addr);
    }
    else
    {
        node->addr = *addr;
    }

    node->enabled = true;

    return true;
}

bool lnx_ip6_addr_del(lnx_ip6_t *ip6, const osn_ip6_addr_t *dns)
{
    struct lnx_ip6_addr_node *node;

    node = ds_tree_find(&ip6->ip6_addr_list, (void *)&dns->ia6_addr);
    if (node == NULL)
    {
        LOG(ERR, "ip6: %s: Unable to remove IPv6 address: "PRI_osn_ip6_addr". Not found.",
                ip6->ip6_ifname,
                FMT_osn_ip6_addr(*dns));
        return false;
    }

    node->enabled = false;

    return true;
}

bool lnx_ip6_dns_add(lnx_ip6_t *ip6, const osn_ip6_addr_t *dns)
{
    (void)ip6;
    (void)dns;

    LOG(NOTICE, "ip6: Unsupported DNSv6 server. Cannot add: "PRI_osn_ip6_addr,
            FMT_osn_ip6_addr(*dns));

    return false;
}

bool lnx_ip6_dns_del(lnx_ip6_t *ip6, const osn_ip6_addr_t *addr)
{
    (void)ip6;
    (void)addr;

    LOG(NOTICE, "ip6: Unsupported DNSv6 server. Cannot del:" PRI_osn_ip6_addr,
            FMT_osn_ip6_addr(*addr));

    return false;
}

/**
 * Parse a single line of a "ip -o -6 addr show dev IF" output.
 */
bool lnx_ip6_ipaddr_parse(void *_self, int type, const char *line)
{
    struct lnx_ip6_addr *paddr;
    osn_ip6_addr_t ip6addr;
    char *stok;
    char *ifname;
    char buf[256];
    char *pbuf;
    int lft;

    lnx_ip6_t *self = _self;

    LOG(DEBUG, "ip6: %s: ip_addr_show%s %s",
            self->ip6_ifname,
            type == EXECSH_PIPE_STDOUT ? ">" : "|",
            line);

    if (type != EXECSH_PIPE_STDOUT) return true;

    STRSCPY(buf, line);

    /*
     * Example output:
     *
     * # ip -o -6 addr show dev br-wan
     * 20: br-wan    inet6 2a00:ee2:1704:9901::2000/128 scope global dynamic \       valid_lft 237sec preferred_lft 124sec
     * 20: br-wan    inet6 fe80::62b4:f7ff:fef0:15c8/64 scope link \       valid_lft forever preferred_lft forever
     */

    /* Skip interface index */
    strtok_r(buf, " ", &pbuf);

    ifname = strtok_r(NULL, " ", &pbuf);

    /* Skip "inet6" */
    stok = strtok_r(NULL, " ", &pbuf);
    if (strcmp(stok, "inet6") != 0)
    {
        LOG(WARN, "ip6: Expected \"inet6\" token not found on line: %s", line);
        return true;
    }

    /* IPv6 address, includes the prefix */
    stok = strtok_r(NULL, " ", &pbuf);
    if (stok == NULL || !osn_ip6_addr_from_str(&ip6addr, stok))
    {
        LOG(WARN, "ip6: Unable to parse ipv6 address on line: %s", line);
        return true;
    }

    /*
     * It seems that there's a variable amount of keywords after the IPv6 address
     * Skip all keywords until we encounter the "valid_lft" string
     */
    while ((stok = strtok_r(NULL, " ", &pbuf)) != NULL)
    {
        if (strcmp(stok, "valid_lft") == 0) break;
    }

    /* Parse the valid_lft value */
    stok = strtok_r(NULL, " ", &pbuf);
    if (stok == NULL)
    {
        LOG(WARN, "ip6: Unable to find valid_lft value on line: %s", line);
        return true;
    }
    else if (strcmp(stok, "infinite") == 0)
    {
        ip6addr.ia6_valid_lft = -1;
    }
    else
    {
        lft = atol(stok);
        if (lft == 0) lft = INT_MIN;
        ip6addr.ia6_valid_lft = lft;
    }

    stok = strtok_r(NULL, " ", &pbuf);
    if (stok == NULL || strcmp(stok, "preferred_lft") != 0)
    {
        LOG(WARN, "ip6: Expected \"preferred_lft\" token not found: %s", line);
        return true;
    }

    /* Skip "preferred_lft" */
    stok = strtok_r(NULL, " ", &pbuf);
    if (stok == NULL)
    {
        LOG(WARN, "ip6: Unable to find preferred_lft value on line: %s", line);
    }
    else if (strcmp(stok, "infinite") == 0)
    {
        ip6addr.ia6_pref_lft = -1;
    }
    else
    {
        lft = atol(stok);
        if (lft == 0) lft = INT_MIN;
        ip6addr.ia6_pref_lft = lft;
    }

    paddr = MEM_APPEND(&lnx_ip6_addr_table, &lnx_ip6_addr_table_e, sizeof(struct lnx_ip6_addr));
    STRSCPY(paddr->ifname, ifname);
    paddr->ip6addr = ip6addr;

    return true;
}

void lnx_ip6_addr_table_update(void)
{
    static double last_update = 0.0;
    int rc;

    if ((clock_mono_double() - last_update) < LNX_IP6_POLL_TIME)
    {
        return;
    }

    FREE(lnx_ip6_addr_table);
    lnx_ip6_addr_table = NULL;
    lnx_ip6_addr_table_e = NULL;

    rc = execsh_fn(lnx_ip6_ipaddr_parse, NULL, _S(ip -6 -o addr show));
    if (rc != 0)
    {
        LOG(DEBUG, "ip6: \"ip -6 addr show\" returned error %d. IPv6 address list may be incomplete.", rc);
    }

    last_update = clock_mono_double();
}


void lnx_ip6_status_ipaddr_update(lnx_ip6_t *self)
{
    struct lnx_ip6_addr *pa;
    osn_ip6_addr_t *ip6_addr;

    osn_ip6_addr_t *is6_addr_end = NULL;

    FREE(self->ip6_status.is6_addr);
    self->ip6_status.is6_addr = NULL;

    lnx_ip6_addr_table_update();
    for (pa = lnx_ip6_addr_table; pa < lnx_ip6_addr_table_e; pa++)
    {
        if (strcmp(self->ip6_ifname, pa->ifname) != 0) continue;

        ip6_addr = MEM_APPEND(&self->ip6_status.is6_addr, &is6_addr_end, sizeof(*ip6_addr));
        *ip6_addr = pa->ip6addr;
    }

    self->ip6_status.is6_addr_len = is6_addr_end - self->ip6_status.is6_addr;

    LOG(INFO, "ip6: %s: Found %zu IPv6 address(es).", self->ip6_ifname, self->ip6_status.is6_addr_len);
}

/**
 * Parse a single line of a "ip -6 neigh show" output.
 */
bool lnx_ip6_neigh_parse(void *ctx, int type, const char *line)
{
    (void)ctx;

    struct lnx_ip6_neigh *neigh;
    osn_ip6_addr_t ip6addr;
    osn_mac_addr_t mac;
    char buf[256];
    char *sifname;
    char *pbuf;
    char *sp;

    LOG(DEBUG, "ip6: ip_neigh_show%s %s",
            type == EXECSH_PIPE_STDOUT ? ">" : "|",
            line);

    if (type != EXECSH_PIPE_STDOUT) return true;

    STRSCPY(buf, line);

    /*
     * Example output:
     *
     * fe80::ae1f:6bff:feb0:55ef dev eth0 lladdr ac:1f:6b:b0:55:ef router STALE
     */
    sp = strtok_r(buf, " ", &pbuf);
    if (sp == NULL)
    {
        return true;
    }
    else if (!osn_ip6_addr_from_str(&ip6addr, sp))
    {
        LOG(WARN, "ip6: Error parsing ip6 neigh table, invalid ipv6 address %s: %s", sp, line);
        return true;
    }

    sp = strtok_r(NULL, " ", &pbuf);
    if (strcmp(sp, "dev") != 0)
    {
        /* Skip this entry */
        LOG(WARN, "ip6: Error parsing ip6 neigh table, expecting \"dev\" keyword, but got %s: %s", sp, line);
        return true;
    }

    sifname = strtok_r(NULL, " ", &pbuf);

    /*
     * The next string may contain "lladdr" or "FAILED" -- skip this whole line
     * if the next entry is not "lladdr"
     */
    sp = strtok_r(NULL, " ", &pbuf);
    if (strcmp(sp, "lladdr") != 0)
    {
        /* lladdr is missing -- may be an invalid or stale entry, skip it */
        return true;
    }

    /* Parse the MAC */
    sp = strtok_r(NULL, " ", &pbuf);
    if (strcmp(sp, "FAILED") == 0)
    {
        /* ip -6 neigh flush may trigger this condition, skip this entry */
        return true;
    }
    else if (!osn_mac_addr_from_str(&mac, sp))
    {
        LOG(WARN, "ip6: Error parsing ip6 neigh table, invalid MAC address: %s: %s", sp, line);
        return true;
    }

    neigh = MEM_APPEND(&lnx_ip6_neigh_table, &lnx_ip6_neigh_table_e, sizeof(struct lnx_ip6_neigh));
    STRSCPY(neigh->ifname, sifname);
    neigh->macaddr = mac;
    neigh->ip6addr = ip6addr;

    return true;
}

void lnx_ip6_neigh_table_update(void)
{
    static double last_update = 0.0;
    int rc;

    if ((clock_mono_double() - last_update) < LNX_IP6_POLL_TIME)
    {
        return;
    }

    FREE(lnx_ip6_neigh_table);
    lnx_ip6_neigh_table = NULL;
    lnx_ip6_neigh_table_e = NULL;

    rc = execsh_fn(lnx_ip6_neigh_parse, NULL, _S(ip -6 neigh show));
    if (rc != 0)
    {
        LOG(DEBUG, "ip6: \"ip -6 neigh show\" returned error %d. Neighbor report may be incomplete.", rc);
    }

    last_update = clock_mono_double();
}

void lnx_ip6_status_neigh_update(lnx_ip6_t *self)
{
    struct lnx_ip6_neigh *np;
    struct osn_ip6_neigh *ip6_neigh;

    struct osn_ip6_neigh *is6_neigh_end = NULL;

    FREE(self->ip6_status.is6_neigh);
    self->ip6_status.is6_neigh = NULL;

    lnx_ip6_neigh_table_update();
    for (np = lnx_ip6_neigh_table; np < lnx_ip6_neigh_table_e; np++)
    {
        if (strcmp(self->ip6_ifname, np->ifname) != 0) continue;

        ip6_neigh = MEM_APPEND(&self->ip6_status.is6_neigh, &is6_neigh_end, sizeof(*ip6_neigh));
        ip6_neigh->i6n_hwaddr = np->macaddr;
        ip6_neigh->i6n_ipaddr = np->ip6addr;
    }

    self->ip6_status.is6_neigh_len = is6_neigh_end - self->ip6_status.is6_neigh;

    LOG(INFO, "ip6: %s: Found %zu neighbor(s).", self->ip6_ifname, self->ip6_status.is6_neigh_len);
}

/**
 * Netlink event callback. This function is subscribed to the following events:
 *      - LNX_NETLINK_IP6ADDR
 *      - LNX_NETLINK_IP6NEIGH
 *
 * This callback actually triggers the status notification callback of the
 * osn_ip6_t object.
 */
void lnx_ip6_nl_fn(lnx_netlink_t *nl, uint64_t event, const char *ifname)
{
    (void)ifname;

    lnx_ip6_t *self = CONTAINER_OF(nl, lnx_ip6_t, ip6_nl);

    if (self->ip6_status_fn == NULL) return;

    if (event & LNX_NETLINK_IP6ADDR)
    {
        /* Update IPv6 addresses */
        LOG(DEBUG, "ip6: %s: Updating IPv6 addresses.", self->ip6_ifname);
        lnx_ip6_status_ipaddr_update(self);
    }

    if (event & LNX_NETLINK_IP6NEIGH)
    {
        /* Update IPv6 neighbors */
        LOG(DEBUG, "ip6: %s: Update IPv6 neighbors.", self->ip6_ifname);
        lnx_ip6_status_neigh_update(self);
    }

    self->ip6_status_fn(self, &self->ip6_status);
}

void lnx_ip6_status_notify(lnx_ip6_t *self, lnx_ip6_status_fn_t *fn)
{
    self->ip6_status_fn = fn;

    if (self->ip6_status_fn == NULL) return;

    /* Update interface status and call the callback right away */
    lnx_ip6_status_ipaddr_update(self);
    lnx_ip6_status_neigh_update(self);

    self->ip6_status_fn(self, &self->ip6_status);
}
