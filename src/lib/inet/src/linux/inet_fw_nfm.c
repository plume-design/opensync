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

#include <sys/wait.h>
#include <errno.h>

#include "const.h"
#include "ds_tree.h"
#include "execsh.h"
#include "log.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "util.h"

#include "inet_fw.h"

struct __inet_fw
{
    char                        fw_ifname[C_IFNAME_LEN];
    bool                        fw_enabled;
    bool                        fw_nat_enabled;
    ds_tree_t                   fw_portfw_list;
};

/* Wrapper around fw_portfw_entry so we can have it in a tree */
struct fw_portfw_entry
{
    struct inet_portforward     pf_data;            /* Port forwarding structure */
    bool                        pf_pending;         /* Pending deletion */
    ds_tree_node_t              pf_node;            /* Tree node */
};

#define FW_PORTFW_ENTRY_INIT (struct fw_portfw_entry)   \
{                                                       \
    .pf_data = INET_PORTFORWARD_INIT,                   \
}

#define NFM_ID(...)         C_XPACK(const char *[], NULL, __VA_ARGS__)
#define NFM_RULE(...)       C_XPACK(const char *[], NULL, __VA_ARGS__)

static bool nfm_rule_add(
        const char *nfm_id[],
        int priority,
        const char *family,
        const char *table,
        const char *chain,
        const char *rule[],
        const char *target);

static bool nfm_rule_del(
        const char *nfm_id[]);

static bool fw_nat_start(inet_fw_t *self);
static bool fw_nat_stop(inet_fw_t *self);

static bool fw_portforward_start(inet_fw_t *self);
static bool fw_portforward_stop(inet_fw_t *self);

static ds_key_cmp_t fw_portforward_cmp;

static ovsdb_table_t table_Netfilter;

/*
 * ===========================================================================
 *  Public interface
 * ===========================================================================
 */
bool inet_fw_init(inet_fw_t *self, const char *ifname)
{
    static bool global_init = false;

    if (!global_init)
    {
        OVSDB_TABLE_INIT_NO_KEY(Netfilter);
        global_init = true;
    }

    memset(self, 0, sizeof(*self));

    ds_tree_init(&self->fw_portfw_list, fw_portforward_cmp, struct fw_portfw_entry, pf_node);

    if (strscpy(self->fw_ifname, ifname, sizeof(self->fw_ifname)) < 0)
    {
        LOG(ERR, "fw: Interface name %s is too long.", ifname);
        return false;
    }

    /* Start by flushing NAT/LAN rules */
    (void)fw_nat_stop(self);

    return true;
}

bool inet_fw_fini(inet_fw_t *self)
{
    bool retval = inet_fw_stop(self);

    return retval;
}

inet_fw_t *inet_fw_new(const char *ifname)
{
    inet_fw_t *self = malloc(sizeof(inet_fw_t));

    if (!inet_fw_init(self, ifname))
    {
        free(self);
        return NULL;
    }

    return self;
}

bool inet_fw_del(inet_fw_t *self)
{
    bool retval = inet_fw_fini(self);
    if (!retval)
    {
        LOG(WARN, "nat: Error stopping FW on interface: %s", self->fw_ifname);
    }

    free(self);

    return retval;
}

/**
 * Start the FW service on interface
 */
bool inet_fw_start(inet_fw_t *self)
{
    if (self->fw_enabled) return true;

    if (!fw_nat_start(self))
    {
        LOG(WARN, "fw: %s: Failed to apply NAT/LAN rules.", self->fw_ifname);
    }

    if (!fw_portforward_start(self))
    {
        LOG(WARN, "fw: %s: Failed to apply port-forwarding rules.", self->fw_ifname);
    }

    self->fw_enabled = true;

    return true;
}

/**
 * Stop the FW service on interface
 */
bool inet_fw_stop(inet_fw_t *self)
{
    bool retval = true;

    if (!self->fw_enabled) return true;

    retval &= fw_nat_stop(self);
    retval &= fw_portforward_stop(self);

    self->fw_enabled = false;

    return retval;
}

bool inet_fw_nat_set(inet_fw_t *self, bool enable)
{
    self->fw_nat_enabled = enable;

    return true;
}

bool inet_fw_state_get(inet_fw_t *self, bool *nat_enabled)
{
    *nat_enabled = self->fw_enabled && self->fw_nat_enabled;

    return true;
}

/* Test if the port forwarding entry @pf already exists in the port forwarding list */
bool inet_fw_portforward_get(inet_fw_t *self, const struct inet_portforward *pf)
{
    struct fw_portfw_entry *pe;

    pe = ds_tree_find(&self->fw_portfw_list, (void *)pf);
    /* Entry not found */
    if (pe == NULL) return false;
    /* Entry was found, but is scheduled for deletion ... return false */
    if (pe->pf_pending) return false;

    return true;
}

/*
 * Add the @p pf entry to the port forwarding list. A firewall restart is required
 * for the change to take effect.
 */
bool inet_fw_portforward_set(inet_fw_t *self, const struct inet_portforward *pf)
{
    struct fw_portfw_entry *pe;

    LOG(INFO, "fw: %s: PORT FORWARD SET: %d -> "PRI_osn_ip_addr":%d",
            self->fw_ifname,
            pf->pf_src_port,
            FMT_osn_ip_addr(pf->pf_dst_ipaddr),
            pf->pf_dst_port);

    pe = ds_tree_find(&self->fw_portfw_list, (void *)pf);
    if (pe != NULL)
    {
        /* Unflag deletion */
        pe->pf_pending = false;
        return true;
    }

    pe = calloc(1, sizeof(struct fw_portfw_entry));
    if (pe == NULL)
    {
        LOG(ERR, "fw: %s: Unable to allocate port forwarding entry.", self->fw_ifname);
        return false;
    }

    memcpy(&pe->pf_data, pf, sizeof(pe->pf_data));

    ds_tree_insert(&self->fw_portfw_list, pe, &pe->pf_data);

    return true;
}


/* Delete port forwarding entry -- a firewall restart is requried for the change to take effect */
bool inet_fw_portforward_del(inet_fw_t *self, const struct inet_portforward *pf)
{
    struct fw_portfw_entry *pe = NULL;

    LOG(INFO, "fw: %s: PORT FORWARD DEL: %d -> "PRI_osn_ip_addr":%d",
            self->fw_ifname,
            pf->pf_src_port,
            FMT_osn_ip_addr(pf->pf_dst_ipaddr),
            pf->pf_dst_port);

    pe = ds_tree_find(&self->fw_portfw_list, (void *)pf);
    if (pe == NULL)
    {
        LOG(ERR, "fw: %s: Error removing port forwarding entry: %d -> "PRI_osn_ip_addr":%d",
                self->fw_ifname,
                pf->pf_src_port,
                FMT_osn_ip_addr(pf->pf_dst_ipaddr),
                pf->pf_dst_port);
        return false;
    }

    /* Flag for deletion */
    pe->pf_pending = true;

    return true;
}

/*
 * ===========================================================================
 *  NAT - Private functions
 * ===========================================================================
 */

/**
 * Either enable NAT or LAN rules on the interface
 */
bool fw_nat_start(inet_fw_t *self)
{
    bool retval = true;

    if (self->fw_nat_enabled)
    {
        LOG(INFO, "fw: %s: Installing NAT rules.", self->fw_ifname);

        retval &= nfm_rule_add(
                NFM_ID(self->fw_ifname, "ipv4", "nat"),
                100,
                "ipv4",
                "nat",
                "NM_NAT",
                NFM_RULE("-o", self->fw_ifname),
                "MASQUERADE");

        /* Plant miniupnpd rules for port forwarding via upnp */
        retval &= nfm_rule_add(
                NFM_ID(self->fw_ifname, "ipv4", "miniupnpd"),
                100,
                "ipv4",
                "nat",
                "NM_PORT_FORWARD",
                NFM_RULE("-i", self->fw_ifname),
                "MINIUPNPD");
    }
    else
    {
        LOG(INFO, "fw: %s: Installing LAN rules.", self->fw_ifname);

        retval &= nfm_rule_add(
                NFM_ID(self->fw_ifname, "ipv4", "input"),
                100,
                "ipv4",
                "filter",
                "NM_INPUT",
                NFM_RULE("-i", self->fw_ifname),
                "ACCEPT");

        retval &= nfm_rule_add(
                NFM_ID(self->fw_ifname, "ipv6", "input"),
                100,
                "ipv6",
                "filter",
                "NM_INPUT",
                NFM_RULE("-i", self->fw_ifname),
                "ACCEPT");

        retval &= nfm_rule_add(
                NFM_ID(self->fw_ifname, "ipv6", "forward"),
                100,
                "ipv6",
                "filter",
                "NM_FORWARD",
                NFM_RULE("-i", self->fw_ifname),
                "ACCEPT");
    }

    return true;
}

/**
 * Flush all NAT/LAN rules
 */
bool fw_nat_stop(inet_fw_t *self)
{
    bool retval = true;

    LOG(INFO, "fw: %s: Flushing NAT/LAN related rules.", self->fw_ifname);

    /* Flush out NAT rules */
    retval &= nfm_rule_del(NFM_ID(self->fw_ifname, "ipv4", "nat"));

    retval &= nfm_rule_del(NFM_ID(self->fw_ifname, "ipv4", "miniupnpd"));

    /* Flush out LAN rules */
    retval &= nfm_rule_del(NFM_ID(self->fw_ifname, "ipv4", "input"));

    retval &= nfm_rule_del(NFM_ID(self->fw_ifname, "ipv6", "input"));

    retval &= nfm_rule_del(NFM_ID(self->fw_ifname, "ipv6", "forward"));

    return retval;
}

/*
 * ===========================================================================
 *  Port forwarding - Private functions
 * ===========================================================================
 */
/*
 * Port-forwarding stuff
 */
bool fw_portforward_rule(inet_fw_t *self, const struct inet_portforward *pf, bool remove)
{
    char *proto = 0;
    char src_port[8] = { 0 };
    char to_dest[32] = { 0 };


    if (pf->pf_proto == INET_PROTO_UDP)
        proto = "udp";
    else if (pf->pf_proto == INET_PROTO_TCP)
        proto = "tcp";
    else
        return false;


    if (snprintf(src_port, sizeof(src_port), "%u", pf->pf_src_port)
                  >= (int)sizeof(src_port))
        return false;

    if (snprintf(to_dest, sizeof(to_dest), PRI_osn_ip_addr":%u",
                 FMT_osn_ip_addr(pf->pf_dst_ipaddr), pf->pf_dst_port)
                      >= (int)sizeof(to_dest))
    {
            return false;
    }

    if (!remove)
    {
        return nfm_rule_add(
                NFM_ID(self->fw_ifname, "ipv4", "port_forward", proto, src_port),
                200,
                "ipv4",
                "nat",
                "NM_PORT_FORWARD",
                NFM_RULE("-i", self->fw_ifname, "-p", proto, "--dport", src_port, "--to-destination", to_dest),
                "DNAT");
    }
    else
    {
        return nfm_rule_del(NFM_ID(self->fw_ifname, "ipv4", "port_forward", proto, src_port));
    }
}

/* Apply the current portforwarding configuration */
bool fw_portforward_start(inet_fw_t *self)
{
    bool retval = true;

    struct fw_portfw_entry *pe;

    ds_tree_foreach(&self->fw_portfw_list, pe)
    {
        /* Skip entries flagged for deletion */
        if (pe->pf_pending) continue;

        if (!fw_portforward_rule(self, &pe->pf_data, false))
        {
            LOG(ERR, "fw: %s: Error adding port forwarding rule: %d -> "PRI_osn_ip_addr":%d",
                    self->fw_ifname,
                    pe->pf_data.pf_src_port,
                    FMT_osn_ip_addr(pe->pf_data.pf_dst_ipaddr),
                    pe->pf_data.pf_dst_port);
            retval = false;
        }
    }

    return retval;
}

bool fw_portforward_stop(inet_fw_t *self)
{
    struct fw_portfw_entry *pe;
    ds_tree_iter_t iter;

    bool retval = true;

    ds_tree_foreach_iter(&self->fw_portfw_list, pe, &iter)
    {
        if (!fw_portforward_rule(self, &pe->pf_data, true))
        {
            LOG(ERR, "fw: %s: Error deleting port forwarding rule: %d -> "PRI_osn_ip_addr":%d",
                    self->fw_ifname,
                    pe->pf_data.pf_src_port,
                    FMT_osn_ip_addr(pe->pf_data.pf_dst_ipaddr),
                    pe->pf_data.pf_dst_port);
            retval = false;
        }

        /* Flush port forwarding entry */
        if (pe->pf_pending)
        {
            ds_tree_iremove(&iter);
            memset(pe, 0, sizeof(*pe));
            free(pe);
        }
    }

    return retval;
}

/*
 * ===========================================================================
 *  Static functions and utilities
 * ===========================================================================
 */

/*
 * Append string @p src to string @p out. Adjust @p out and @p outsz by the
 * number of bytes appended to the string.
 *
 * Never append more bytes than available as defined by @p outsz. Always
 * terminated @p out with '\0'.
 *
 * @return
 * This function returns the number of bytes appended (excluding the ending '\0')
 *
 * @note
 * outsz will have a value of 1 when the @p out string is full
 */
size_t str_append(char **out, size_t *outsz, const char *src)
{
    size_t plen = strlen(src);

    if (*outsz == 0) return 0;

    if (plen > *outsz  - 1) plen = *outsz - 1;

    memcpy(*out, src, plen);
    (*out)[plen] = '\0';

    *outsz -= plen;
    *out += plen;

    return plen;
}

/*
 * Concatenate a list of strings, use @p delim as the delimiter. The resulting
 * string is always padded with '\0', even if its truncated.
 *
 * @return
 * Return the size of the concatenated string
 */
size_t str_concat(char *out, size_t outsz, const char *delim, const char *str[])
{
    size_t dlen = strlen(delim);
    const char **pstr = str;
    size_t rsz = 0;

    while (*pstr != NULL)
    {
        str_append(&out, &outsz, *pstr);
        rsz += strlen(*pstr);

        pstr++;

        /* Append delimiter */
        if (*pstr != NULL)
        {
            str_append(&out, &outsz, delim);
            rsz += dlen;
        }
    }

    return rsz;
}

/*
 * Calculate a NFM rule id (rule name) -> "NM.nfm_id[0].nfm_id[1]..."
 */
void nfm_rule_id(char *id, size_t idsz, const char *nfm_id[])
{
    strscpy(id, "NM.", idsz);

    /* Rule names must be unique */
    str_concat(id + strlen(id), idsz - strlen(id), ".", nfm_id);
}

bool nfm_rule_add(
        const char *nfm_id[],
        int priority,
        const char *family,
        const char *table,
        const char *chain,
        const char *rule[],
        const char *target)
{
    (void)family;
    (void)table;
    (void)chain;
    (void)rule;
    (void)target;

    struct schema_Netfilter netfilter;

    memset(&netfilter, 0, sizeof(netfilter));

    /* Rule names must be unique */
    nfm_rule_id(netfilter.name, sizeof(netfilter.name), nfm_id);
    netfilter.name_exists = true;

    str_concat(netfilter.rule, sizeof(netfilter.rule), " ", rule);
    netfilter.rule_exists = true;

    SCHEMA_SET_INT(netfilter.enable, true);
    SCHEMA_SET_INT(netfilter.priority, priority);
    SCHEMA_SET_STR(netfilter.protocol, family);
    SCHEMA_SET_STR(netfilter.table, table);
    SCHEMA_SET_STR(netfilter.chain, chain);
    SCHEMA_SET_STR(netfilter.target, target);

    LOG(INFO, "fw: Adding rule: %s", netfilter.name);

    return ovsdb_table_upsert_simple(
            &table_Netfilter,
            SCHEMA_COLUMN(Netfilter, name),
            netfilter.name,
            &netfilter, true);
}

bool nfm_rule_del(const char *nfm_id[])
{
    int rc;

    struct schema_Netfilter netfilter;

    /* Rule names must be unique */
    nfm_rule_id(netfilter.name, sizeof(netfilter.name), nfm_id);

    LOG(INFO, "fw: Deleting rule: %s", netfilter.name);

    rc = ovsdb_table_delete_simple(
            &table_Netfilter,
            SCHEMA_COLUMN(Netfilter, name),
            netfilter.name);

    return rc >= 0;
}


/* Compare two inet_portforward structures -- used for tree comparator */
int fw_portforward_cmp(void *_a, void *_b)
{
    int rc;

    struct inet_portforward *a = _a;
    struct inet_portforward *b = _b;

    rc = osn_ip_addr_cmp(&a->pf_dst_ipaddr, &b->pf_dst_ipaddr);
    if (rc != 0) return rc;

    if (a->pf_proto > b->pf_proto) return 1;
    if (a->pf_proto < b->pf_proto) return -1;

    if (a->pf_dst_port > b->pf_dst_port) return 1;
    if (a->pf_dst_port < b->pf_dst_port) return -1;

    if (a->pf_src_port > b->pf_src_port) return 1;
    if (a->pf_src_port < b->pf_src_port) return -1;

    return 0;
}
