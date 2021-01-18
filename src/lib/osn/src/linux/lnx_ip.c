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
#include <inttypes.h>

#include "log.h"
#include "util.h"
#include "execsh.h"

#include "lnx_ip.h"

#define LNX_IP_REALLOC_GROW    16

struct lnx_ip_addr_node
{
    osn_ip_addr_t           addr;
    ds_tree_node_t          tnode;                          /* Tree node */
};

struct lnx_ip_route_gw_node
{
    osn_ip_addr_t           src;                            /* Source subnet */
    osn_ip_addr_t           gw;                             /* Destination gateway */
    ds_tree_node_t          tnode;
};

static bool lnx_ip_addr_flush(lnx_ip_t *self);
static bool lnx_ip_route_flush(lnx_ip_t *self);
static void lnx_ip_status_poll(lnx_ip_t *self);

/* execsh commands */
static char lnx_ip_addr_add_cmd[] = _S(ip address add "$2/$3" broadcast "+" dev "$1");
static char lnx_ip_addr_flush_cmd[] = _S([ ! -e "/sys/class/net/$1" ] || ip -4 address flush dev "$1");

static char lnx_ip_route_gw_add_cmd[] = _S(route add "$2" gw "$3" dev "$1");

/* Scope global doesn't flush "local" or "link" routes */
static char lnx_ip_route_gw_flush_cmd[] = _S([ ! -e "/sys/class/net/$1" ] || ip -4 route flush dev "$1" scope global);

static lnx_netlink_fn_t lnx_ip_nl_fn;
static execsh_fn_t lnx_ip_addr_parse;

/*
 * Initialize Linux IP object instance
 */
bool lnx_ip_init(lnx_ip_t *self, const char *ifname)
{
    memset(self, 0, sizeof(*self));
    if (STRSCPY(self->ip_ifname, ifname) < 0)
    {
        LOG(ERR, "ip: Interface name too long: %s", ifname);
        return false;
    }

    ds_tree_init(
            &self->ip_addr_list,
            osn_ip_addr_cmp,
            struct lnx_ip_addr_node, tnode);

    ds_tree_init(
            &self->ip_dns_list,
            osn_ip_addr_cmp,
            struct lnx_ip_addr_node, tnode);

    ds_tree_init(
            &self->ip_route_gw_list,
            osn_ip_addr_cmp,
            struct lnx_ip_route_gw_node, tnode);

    if (!lnx_netlink_init(&self->ip_nl, lnx_ip_nl_fn))
    {
        LOG(ERR, "ip: %s: Unable to initialize netlink object.", self->ip_ifname);
        return false;
    }

    /* Install netlink filters */
    lnx_netlink_set_events(&self->ip_nl, LNX_NETLINK_IP4ADDR);
    lnx_netlink_set_ifname(&self->ip_nl, self->ip_ifname);

    return true;
}

/*
 * Destroy Linux IP object instance
 */
bool lnx_ip_fini(lnx_ip_t *self)
{
    struct lnx_ip_addr_node *node;
    struct lnx_ip_route_gw_node *rnode;
    ds_tree_iter_t iter;

    bool retval = true;

    /* Destroy the netlink object */
    if (!lnx_netlink_fini(&self->ip_nl))
    {
        LOG(WARN, "ip: %s: Unable to destroy the netlink object.", self->ip_ifname);
        retval = false;
    }

    /* Flush routes */
    lnx_ip_route_flush(self);

    /* Remove all active addresses */
    lnx_ip_addr_flush(self);

    /* Free list of IPv4 address */
    ds_tree_foreach_iter(&self->ip_addr_list, node, &iter)
    {
        ds_tree_iremove(&iter);
        free(node);
    }

    /* Free list of DNSv4 addresses */
    ds_tree_foreach_iter(&self->ip_dns_list, node, &iter)
    {
        ds_tree_iremove(&iter);
        free(node);
    }

    /* Free list of gateway routes */
    ds_tree_foreach_iter(&self->ip_route_gw_list, rnode, &iter)
    {
        ds_tree_iremove(&iter);
        free(rnode);
    }

    free(self->ip_status.is_addr);

    return retval;
}

/*
 * Flush all configured IPv4 routes
 */
bool lnx_ip_route_flush(lnx_ip_t *self)
{
    int rc;

    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_ip_route_gw_flush_cmd, self->ip_ifname);
    if (rc != 0)
    {
        LOG(WARN, "ip: %s: Unable to flush IPv4 routes.", self->ip_ifname);
        return false;
    }

    return true;
}


/*
 * Flush all configured IPv4 addresses
 */
bool lnx_ip_addr_flush(lnx_ip_t *self)
{
    int rc;

    rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_ip_addr_flush_cmd, self->ip_ifname);
    if (rc != 0)
    {
        LOG(WARN, "ip: %s: Unable to flush IPv4 addresses.", self->ip_ifname);
        return false;
    }

    return true;
}

/*
 * Apply configuration to system
 */
bool lnx_ip_apply(lnx_ip_t *self)
{
    struct lnx_ip_addr_node *node;
    struct lnx_ip_route_gw_node *rnode;

    char saddr[OSN_IP_ADDR_LEN];
    char sgw[OSN_IP_ADDR_LEN];
    char spref[C_INT32_LEN];
    int rc;

    /* Start by issuing a flush */
    lnx_ip_addr_flush(self);
    lnx_ip_route_flush(self);

    /* First apply IPv4 addresses */
    ds_tree_foreach(&self->ip_addr_list, node)
    {
        if (inet_ntop(AF_INET, &node->addr.ia_addr, saddr, sizeof(saddr)) == NULL)
        {
            LOG(ERR, "ip: %s: Unable to convert IPv4 address, skipping add.", self->ip_ifname);
            continue;
        }

        snprintf(spref, sizeof(spref), "%d", node->addr.ia_prefix);

        rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_ip_addr_add_cmd, self->ip_ifname, saddr, spref);
        if (rc != 0)
        {
            LOG(WARN, "ip: %s: Unable to add IPv4 address: "PRI_osn_ip_addr,
                    self->ip_ifname,
                    FMT_osn_ip_addr(node->addr));
            continue;
        }
    }

    /* Apply IPv4 routes */
    ds_tree_foreach(&self->ip_route_gw_list, rnode)
    {
        if (osn_ip_addr_cmp(&rnode->src, &OSN_IP_ADDR_INIT) == 0)
        {
            STRSCPY(saddr, "default");
        }
        else if (inet_ntop(AF_INET, &rnode->src.ia_addr, saddr, sizeof(saddr)) == NULL)
        {
            LOG(ERR, "ip: %s: Unable to convert IPv4 source address, skipping.", self->ip_ifname);
            continue;
        }

        if (inet_ntop(AF_INET, &rnode->gw.ia_addr, sgw, sizeof(sgw)) == NULL)
        {
            LOG(ERR, "ip: %s: Unable to convert IPv4 gateway address, skipping.", self->ip_ifname);
            continue;
        }

        rc = execsh_log(LOG_SEVERITY_DEBUG, lnx_ip_route_gw_add_cmd, self->ip_ifname, saddr, sgw);
        if (rc != 0)
        {
            LOG(WARN, "ip: %s: Unable to add IPv4 gateway route: "PRI_osn_ip_addr" -> "PRI_osn_ip_addr,
                    self->ip_ifname,
                    FMT_osn_ip_addr(rnode->src),
                    FMT_osn_ip_addr(rnode->gw));
            continue;
        }
    }

    return true;
}

bool lnx_ip_addr_add(lnx_ip_t *self, const osn_ip_addr_t *addr)
{
    struct lnx_ip_addr_node *node;

    node = ds_tree_find(&self->ip_addr_list, (void *)addr);
    if (node == NULL)
    {
        node = calloc(1, sizeof(*node));
        node->addr = *addr;
        ds_tree_insert(&self->ip_addr_list, node, &node->addr);
    }
    else
    {
        node->addr = *addr;
    }

    return true;
}

bool lnx_ip_addr_del(lnx_ip_t *ip, const osn_ip_addr_t *dns)
{
    struct ip_addr_node *node;

    node = ds_tree_find(&ip->ip_addr_list, (void *)dns);
    if (node == NULL)
    {
        LOG(ERR, "ip: %s: Unable to remove IPv4 address: "PRI_osn_ip_addr". Not found.",
                ip->ip_ifname,
                FMT_osn_ip_addr(*dns));
        return false;
    }

    ds_tree_remove(&ip->ip_addr_list, node);

    free(node);

    return true;
}

bool lnx_ip_route_gw_add(lnx_ip_t *ip, const osn_ip_addr_t *src, const osn_ip_addr_t *gw)
{
    struct lnx_ip_route_gw_node *rnode;

    rnode = ds_tree_find(&ip->ip_route_gw_list, (void *)src);
    if (rnode == NULL)
    {
        rnode = malloc(sizeof(struct lnx_ip_route_gw_node));
        rnode->src = *src;
        rnode->gw = *gw;

        ds_tree_insert(&ip->ip_route_gw_list, rnode, &rnode->src);
    }
    else
    {
        /* Update the gateway */
        rnode->gw = *gw;
    }

    return true;
}

bool lnx_ip_route_gw_del(lnx_ip_t *ip, const osn_ip_addr_t *src, const osn_ip_addr_t *gw)
{
    struct lnx_ip_route_gfw_node *rnode;

    rnode = ds_tree_find(&ip->ip_addr_list, (void *)src);
    if (rnode == NULL)
    {
        LOG(ERR, "ip: %s: Unable to remove IPv4 gateway route: "PRI_osn_ip_addr" -> "PRI_osn_ip_addr,
                ip->ip_ifname,
                FMT_osn_ip_addr(*src),
                FMT_osn_ip_addr(*gw));

        return false;
    }

    ds_tree_remove(&ip->ip_route_gw_list, rnode);

    free(rnode);

    return true;
}

bool lnx_ip_dns_add(lnx_ip_t *ip, const osn_ip_addr_t *dns)
{
    (void)ip;
    (void)dns;

    LOG(NOTICE, "ip: Unsupported DNS server configuration. Cannot add: "PRI_osn_ip_addr,
            FMT_osn_ip_addr(*dns));

    return false;
}

bool lnx_ip_dns_del(lnx_ip_t *ip, const osn_ip_addr_t *addr)
{
    (void)ip;
    (void)addr;

    LOG(NOTICE, "ip: Unsupported DNS server configuration. Cannot del:" PRI_osn_ip_addr,
            FMT_osn_ip_addr(*addr));

    return false;
}

void lnx_ip_status_notify(lnx_ip_t *self, lnx_ip_status_fn_t *fn)
{
    self->ip_status_fn = fn;

    if (fn != NULL)
    {
        if (!lnx_netlink_start(&self->ip_nl))
        {
            LOG(WARN, "ip: %s: Unable to start netlink object.", self->ip_ifname);
        }
    }
    else
    {
        if (!lnx_netlink_stop(&self->ip_nl))
        {
            LOG(WARN, "ip: %s: Unable to stop netlink object.", self->ip_ifname);
        }
    }
}

/*
 * Poll current interface status and call the set callback
 */
void lnx_ip_status_poll(lnx_ip_t *self)
{
    int rc;

    if (self->ip_status.is_addr != NULL)
    {
        free(self->ip_status.is_addr);
    }

    self->ip_status.is_addr = NULL;
    self->ip_status.is_addr_len = 0;

    /*
     * Execute the "ip -4 -o addr show IFNAME" command.
     * The -o switch yields a more compact and easier to parse format.
     */
    rc = execsh_fn(lnx_ip_addr_parse, self, _S(ip -4 -o addr show dev "$1"), self->ip_ifname);
    if (rc != 0)
    {
        LOG(DEBUG, "ip: %s: Unable to acquire interface IPv4 address list. Exit code: %d",
                self->ip_ifname,
                rc);
    }

    LOG(INFO, "ip: %s: Found %zu IPv4 address(es).", self->ip_ifname, self->ip_status.is_addr_len);

    if (self->ip_status_fn != NULL)
    {
        self->ip_status_fn(self, &self->ip_status);
    }
}

/**
 * Parse a single line of a "ip -o -4 addr show dev IF" output.
 */
bool lnx_ip_addr_parse(void *data, int type, const char *line)
{
    char buf[256];
    char *pbuf;
    char *sip4;

    lnx_ip_t *self = data;

    LOG(DEBUG, "ip: %s: ip_addr_show%s %s",
            self->ip_ifname,
            type == EXECSH_PIPE_STDOUT ? ">" : "|",
            line);

    if (type != EXECSH_PIPE_STDOUT) return true;

    STRSCPY(buf, line);

    /*
     * Example output:
     *
     * # ip -o -4 addr show
     * 20: br-wan    inet 10.77.204.196/24 brd 10.77.204.255 scope global br-wan\       valid_lft forever preferred_lft forever
     * 21: br-home    inet 192.168.40.1/24 brd 192.168.40.255 scope global br-home\       valid_lft forever preferred_lft forever
     */

    /* Skip interface index */
    strtok_r(buf, " ", &pbuf);

    /* Skip interface name */
    strtok_r(NULL, " ", &pbuf);

    /* Skip "inet" */
    strtok_r(NULL, " ", &pbuf);

    /* IPv4 address, includes the netmask (prefix) */
    sip4 = strtok_r(NULL, " ", &pbuf);

    struct osn_ip_status *is = &self->ip_status;

    /*
     * Resize array in OSN_IP_REALLOC_GROW increments
     */
    if ((is->is_addr_len % LNX_IP_REALLOC_GROW) == 0)
    {
        is->is_addr = realloc(
                is->is_addr,
                (is->is_addr_len + LNX_IP_REALLOC_GROW) * sizeof(is->is_addr[0]));
    }

    if (!osn_ip_addr_from_str(&is->is_addr[is->is_addr_len], sip4))
    {
        LOG(WARN, "ip: %s: Error parsing IPv4 addresses -- invalid IPv4 address \"%s\" on line: %s",
                self->ip_ifname,
                sip4,
                line);

        return true;
    }

    is->is_addr_len++;

    return true;
}

/*
 * Netlink callback -- this will be invoked each time an IPv4 change is detected
 * on the interface
 */
void lnx_ip_nl_fn(lnx_netlink_t *nl, uint64_t event, const char *ifname)
{
    (void)ifname;
    (void)event;

    lnx_ip_t *self = CONTAINER_OF(nl, lnx_ip_t, ip_nl);

    lnx_ip_status_poll(self);
}
