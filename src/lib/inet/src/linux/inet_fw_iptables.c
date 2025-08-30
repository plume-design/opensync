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

#include "log.h"
#include "const.h"
#include "util.h"
#include "memutil.h"
#include "execsh.h"
#include "kconfig.h"
#include "ds_tree.h"

#include "inet_fw.h"
#include "os_nif.h"

#define FW_RULE_IPV4                (1 << 0)
#define FW_RULE_IPV6                (1 << 1)
#define FW_RULE_ALL                 (FW_RULE_IPV4 | FW_RULE_IPV6)

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

/**
 * fw_rule_*() macros -- pass a dummy argv[0] value ("false") -- it will
 * be overwritten with the real path to iptables in the __fw_rule*() function.
 */
static bool fw_rule_add_a(inet_fw_t *self, int type, char *argv[]);
#define fw_rule_add(self, type, table, chain, ...) \
        fw_rule_add_a(self, type, C_VPACK("false", table, chain, __VA_ARGS__))

static bool fw_rule_del_a(inet_fw_t *self, int type, char *argv[]);
#define fw_rule_del(self, type, table, chain, ...) \
        fw_rule_del_a(self, type, C_VPACK("false", table, chain, __VA_ARGS__))

static bool fw_nat_start(inet_fw_t *self);
static bool fw_nat_stop(inet_fw_t *self);

static bool fw_portforward_start(inet_fw_t *self);
static bool fw_portforward_stop(inet_fw_t *self);

static ds_key_cmp_t fw_portforward_cmp;

/*
 * ===========================================================================
 *  Public interface
 * ===========================================================================
 */
bool inet_fw_init(inet_fw_t *self, const char *ifname)
{

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
    inet_fw_t *self = MALLOC(sizeof(inet_fw_t));

    if (!inet_fw_init(self, ifname))
    {
        FREE(self);
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

    FREE(self);

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

    LOG(INFO, "fw: %s: PORT FORWARD SET: %d -> "PRI_osn_ip_addr":%d NAT loopback %d",
            self->fw_ifname,
            pf->pf_src_port,
            FMT_osn_ip_addr(pf->pf_dst_ipaddr),
            pf->pf_dst_port,
            pf->pf_nat_loopback);

    pe = ds_tree_find(&self->fw_portfw_list, (void *)pf);
    if (pe != NULL)
    {
        /* Unflag deletion */
        pe->pf_pending = false;
        return true;
    }

    pe = CALLOC(1, sizeof(struct fw_portfw_entry));

    memcpy(&pe->pf_data, pf, sizeof(pe->pf_data));

    ds_tree_insert(&self->fw_portfw_list, pe, &pe->pf_data);

    return true;
}


/* Delete port forwarding entry -- a firewall restart is required for the change to take effect */
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

        retval &= fw_rule_add(self,
                FW_RULE_IPV4, "nat", "NM_NAT",
                "-o", self->fw_ifname,
                "-m", "physdev", "--physdev-is-bridged",
                "-j", "ACCEPT");
        retval &= fw_rule_add(self,
                FW_RULE_IPV4, "nat", "NM_NAT",
                "-o", self->fw_ifname, "-j", "MASQUERADE");

        /* MSS clamping rules for both IPv4/6 */
        retval &= fw_rule_add(self,
                FW_RULE_ALL,
                "filter",
                "NM_MSS_CLAMP",
                "-o", self->fw_ifname,
                "-p", "tcp",
                "--tcp-flags", "SYN,RST", "SYN",
                "-j", "TCPMSS",
                "--clamp-mss-to-pmtu");

        /* Regardless of NAT enabled, always allow input on the LAN interface */
        if (strcmp(self->fw_ifname, CONFIG_TARGET_LAN_BRIDGE_NAME) == 0)
        {
            retval &= fw_rule_add(self,
                    FW_RULE_ALL, "filter", "NM_INPUT",
                    "-i", self->fw_ifname, "-j", "ACCEPT");

            retval &= fw_rule_add(self,
                FW_RULE_ALL, "filter", "NM_FORWARD",
                "-i", self->fw_ifname, "-j", "ACCEPT");
        }
    }
    else
    {
        LOG(INFO, "fw: %s: Installing LAN rules.", self->fw_ifname);

        retval &= fw_rule_add(self,
                FW_RULE_ALL, "filter", "NM_INPUT",
                "-i", self->fw_ifname, "-j", "ACCEPT");

        retval &= fw_rule_add(self,
                FW_RULE_ALL, "filter", "NM_FORWARD",
                "-i", self->fw_ifname, "-j", "ACCEPT");
    }

    return retval;
}

/**
 * Flush all NAT/LAN rules
 */
bool fw_nat_stop(inet_fw_t *self)
{
    bool retval = true;

    LOG(INFO, "fw: %s: Flushing NAT/LAN related rules.", self->fw_ifname);

    /* Flush out NAT rules */
    retval &= fw_rule_del(self, FW_RULE_IPV4,
            "nat", "NM_NAT", "-o", self->fw_ifname, "-j", "MASQUERADE");
    retval &= fw_rule_del(self,
            FW_RULE_IPV4, "nat", "NM_NAT",
            "-o", self->fw_ifname,
            "-m", "physdev", "--physdev-is-bridged",
            "-j", "ACCEPT");

    /* Flush clamping rules */
    retval &= fw_rule_del(self,
            FW_RULE_ALL,
            "filter",
            "NM_MSS_CLAMP",
            "-o", self->fw_ifname,
            "-p", "tcp",
            "--tcp-flags", "SYN,RST", "SYN",
            "-j", "TCPMSS",
            "--clamp-mss-to-pmtu");

    /* Flush out LAN rules */
    retval &= fw_rule_del(self, FW_RULE_ALL,
            "filter", "NM_INPUT", "-i", self->fw_ifname, "-j", "ACCEPT");

    retval &= fw_rule_del(self, FW_RULE_ALL,
            "filter", "NM_FORWARD", "-i", self->fw_ifname, "-j", "ACCEPT");

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
    char *proto;
    char src_port[C_INT32_LEN];
    char dst_port[C_INT32_LEN];
    char to_dest[C_IP4ADDR_LEN + C_INT32_LEN + 1];
    char dest[C_IP4ADDR_LEN];
    char dest_subnet[C_IP4ADDR_LEN + C_INT32_LEN + 1];
    osn_ip_addr_t dest_subnet_ipaddr = pf->pf_dst_ipaddr;
    os_ipaddr_t addr;
    bool result = false;
    int ia_prefix = 0, i;

    if (pf->pf_proto == INET_PROTO_UDP)
        proto = "udp";
    else if (pf->pf_proto == INET_PROTO_TCP)
        proto = "tcp";
    else
        return false;

    result = os_nif_netmask_get(CONFIG_TARGET_LAN_BRIDGE_NAME, &addr);
    if (!result) {
        LOG(ERROR, "%s: os_nif_netmask_get failed", __func__);
    } else {
        LOG(DEBUG, "%s: %s netmask %s ", __func__, CONFIG_TARGET_LAN_BRIDGE_NAME, inet_ntoa(*(struct in_addr*)&addr));
    }
    for (i=0; i<4; i++)
    {
        while (addr.addr[i] != 0)
        {
            ia_prefix += addr.addr[i] & 1;
            addr.addr[i] >>= 1;
        }
    }

    dest_subnet_ipaddr.ia_prefix = ia_prefix;
    dest_subnet_ipaddr = osn_ip_addr_subnet(&dest_subnet_ipaddr);
    LOG(DEBUG, "%s: LAN bridge ia prefix %d", __func__, dest_subnet_ipaddr.ia_prefix);

    if (snprintf(src_port, sizeof(src_port), "%u", pf->pf_src_port) >= (int)sizeof(src_port))
        return false;

    if (snprintf(dst_port, sizeof(dst_port), "%u", pf->pf_dst_port) >= (int)sizeof(dst_port))
        return false;

    if (snprintf(dest, sizeof(dest), PRI_osn_ip_addr, FMT_osn_ip_addr(pf->pf_dst_ipaddr)) >= (int)sizeof(dest))
    {
        return false;
    }

    if (snprintf(dest_subnet, sizeof(dest_subnet), PRI_osn_ip_addr, FMT_osn_ip_addr(dest_subnet_ipaddr))
                 >= (int)sizeof(dest_subnet))
    {
        return false;
    }

    if (snprintf(to_dest, sizeof(to_dest), PRI_osn_ip_addr":%u",
                 FMT_osn_ip_addr(pf->pf_dst_ipaddr), pf->pf_dst_port)
                      >= (int)sizeof(to_dest))
    {
            return false;
    }

    // NAT loopback handling
    if (remove || (!remove && pf->pf_nat_loopback == false))
    {
        if (!fw_rule_del(
                    self,
                    FW_RULE_IPV4,
                    "nat",
                    "NM_PORT_FORWARD",
                    "-i", CONFIG_TARGET_LAN_BRIDGE_NAME,
                    "-p", proto,
                    "--dport", src_port,
                    "-j", "DNAT",
                    "--to-destination", to_dest))
        {
            return false;
        }

        if (!fw_rule_del(
                    self,
                    FW_RULE_IPV4,
                    "nat",
                    "NM_NAT",
                    "--src", dest_subnet,
                    "--dst", dest,
                    "-o", CONFIG_TARGET_LAN_BRIDGE_NAME,
                    "-p", proto,
                    "--dport", dst_port,
                    "-j", "MASQUERADE"))
        {
            return false;
        }
    }
    else if (!remove && pf->pf_nat_loopback == true)
    {
        if (!fw_rule_add(
                    self,
                    FW_RULE_IPV4, "nat", "NM_PORT_FORWARD",
                    "-i", CONFIG_TARGET_LAN_BRIDGE_NAME,
                    "-p", proto,
                    "--dport", src_port,
                    "-j", "DNAT",
                    "--to-destination", to_dest))
        {
            return false;
        }

        if (!fw_rule_add(
                    self,
                    FW_RULE_IPV4, "nat", "NM_NAT",
                    "--src", dest_subnet,
                    "--dst", dest,
                    "-o", CONFIG_TARGET_LAN_BRIDGE_NAME,
                    "-p", proto,
                    "--dport", dst_port,
                    "-j", "MASQUERADE"))
        {
            return false;
        }
    }

    if (remove)
    {
        if (pf->pf_proto == INET_PROTO_TCP)
        {
            if (!fw_rule_del(
                        self,
                        FW_RULE_IPV4, "filter", "NM_PORT_FORWARD",
                        "-i", self->fw_ifname,
                        "-p", proto,
                        "--dport", dst_port,
                        "--dst", dest,
                        "--syn",
                        "-m", "conntrack",
                        "--ctstate", "NEW",
                        "-j", "ACCEPT"))
            {
                return false;
            }
        }
        else
        {
            if (!fw_rule_del(
                        self,
                        FW_RULE_IPV4, "filter", "NM_PORT_FORWARD",
                        "-i", self->fw_ifname,
                        "-p", proto,
                        "--dport", dst_port,
                        "--dst", dest,
                        "-m", "conntrack",
                        "--ctstate", "NEW",
                        "-j", "ACCEPT"))
            {
                return false;
            }
        }

        if (!fw_rule_del(
                    self,
                    FW_RULE_IPV4,
                    "nat",
                    "NM_PORT_FORWARD",
                    "-i", self->fw_ifname,
                    "-p", proto,
                    "--dport", src_port,
                    "-j", "DNAT",
                    "--to-destination", to_dest))
        {
            return false;
        }
   }
    else
    {
        if (!fw_rule_add(
                    self,
                    FW_RULE_IPV4, "nat", "NM_PORT_FORWARD",
                    "-i", self->fw_ifname,
                    "-p", proto,
                    "--dport", src_port,
                    "-j", "DNAT",
                    "--to-destination", to_dest))
        {
            return false;
        }

        if (pf->pf_proto == INET_PROTO_TCP)
        {
            if (!fw_rule_add(
                        self,
                        FW_RULE_IPV4, "filter", "NM_PORT_FORWARD",
                        "-i", self->fw_ifname,
                        "-p", proto,
                        "--dport", dst_port,
                        "--dst", dest,
                        "--syn",
                        "-m", "conntrack",
                        "--ctstate", "NEW",
                        "-j", "ACCEPT"))
            {
                return false;
            }
        }
        else
        {
            if (!fw_rule_add(
                        self,
                        FW_RULE_IPV4, "filter", "NM_PORT_FORWARD",
                        "-i", self->fw_ifname,
                        "-p", proto,
                        "--dport", dst_port,
                        "--dst", dest,
                        "-m", "conntrack",
                        "--ctstate", "NEW",
                        "-j", "ACCEPT"))
            {
                return false;
            }

        }
    }

    return true;
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
            FREE(pe);
        }
    }

    return retval;
}

/*
 * ===========================================================================
 *  Static functions and utilities
 * ===========================================================================
 */

char fw_rule_add_cmd[] =
_S(
    IPTABLES="$1";
    tbl="$2";
    chain="$3";
    shift 3;
    if ! "$IPTABLES" -w -t "$tbl" -C "$chain" "$@" 2> /dev/null;
    then
        "$IPTABLES" -w -t "$tbl" -A "$chain" "$@";
    fi;
);

bool fw_rule_add_a(inet_fw_t *self,int type, char *argv[])
{
    int status;

    bool retval = true;

    if (type & FW_RULE_IPV4)
    {
        argv[0] = CONFIG_INET_FW_IPTABLES_PATH;
        status = execsh_log_a(LOG_SEVERITY_DEBUG, fw_rule_add_cmd, argv);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(WARN, "fw: %s: IPv4 rule insertion failed: %s",
                    self->fw_ifname,
                    fw_rule_add_cmd);
            retval = false;
        }
    }

    if (type & FW_RULE_IPV6)
    {
        argv[0] = CONFIG_INET_FW_IP6TABLES_PATH;
        status = execsh_log_a(LOG_SEVERITY_DEBUG, fw_rule_add_cmd,  argv);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(WARN, "fw: %s: IPv6 rule insertion failed: %s",
                    self->fw_ifname,
                    fw_rule_add_cmd);
            retval = false;
        }
    }

    return retval;
}

char fw_rule_del_cmd[] =
_S(
    IPTABLES="$1";
    tbl="$2";
    chain="$3";
    shift 3;
    if "$IPTABLES" -w -t "$tbl" -C "$chain" "$@" 2> /dev/null;
    then
        "$IPTABLES" -w -t "$tbl" -D "$chain" "$@";
    fi;
);

bool fw_rule_del_a(inet_fw_t *self, int type, char *argv[])
{
    int status;

    bool retval = true;

    if (type & FW_RULE_IPV4)
    {
        argv[0] = CONFIG_INET_FW_IPTABLES_PATH;
        status = execsh_log_a(LOG_SEVERITY_DEBUG, fw_rule_del_cmd, argv);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(WARN, "fw: %s: IPv4 rule deletion failed: %s",
                    self->fw_ifname,
                    fw_rule_del_cmd);
            retval = false;
        }
    }

    if (type & FW_RULE_IPV6)
    {
        argv[0] = CONFIG_INET_FW_IP6TABLES_PATH;
        status = execsh_log_a(LOG_SEVERITY_DEBUG, fw_rule_del_cmd,  argv);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            LOG(WARN, "fw: %s: IPv6 rule deletion failed: %s",
                    self->fw_ifname,
                    fw_rule_del_cmd);
            retval = false;
        }
    }

    return retval;
}

/* Compare two inet_portforward structures -- used for tree comparator */
int fw_portforward_cmp(const void *_a, const void *_b)
{
    int rc;

    const struct inet_portforward *a = _a;
    const struct inet_portforward *b = _b;

    rc = osn_ip_addr_cmp(&a->pf_dst_ipaddr, &b->pf_dst_ipaddr);
    if (rc != 0) return rc;

    if (a->pf_proto > b->pf_proto) return 1;
    if (a->pf_proto < b->pf_proto) return -1;

    if (a->pf_dst_port > b->pf_dst_port) return 1;
    if (a->pf_dst_port < b->pf_dst_port) return -1;

    if (a->pf_src_port > b->pf_src_port) return 1;
    if (a->pf_src_port < b->pf_src_port) return -1;

    if (a->pf_nat_loopback && !(b->pf_nat_loopback)) return 1;
    if (!(a->pf_nat_loopback) && b->pf_nat_loopback) return -1;

    return 0;
}
