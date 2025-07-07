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

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>

/* Prevent netlink symbol collisions when including linux net/if.h */
#define _LINUX_IF_H

#include <net/if.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <linux/ip.h>
#include <linux/if_addr.h>
#include <linux/if_link.h>
#include <linux/if_tunnel.h>
#include <linux/ip6_tunnel.h>
#include <linux/version.h>

#include <netlink/netlink.h>
#include <netlink/object.h>
#include <netlink/socket.h>
#include <netlink/attr.h>
#include <netlink/msg.h>

#include "osn_types.h"
#include "lnx_map_mape.h"
#include "lnx_map.h"

#include "memutil.h"
#include "execsh.h"
#include "os_nif.h"
#include "util.h"
#include "log.h"

/**
 *
 * The MAP-E backend implementation using ip6tnl.
 *
 */

/*
 * Configure FMR rules (if any and if platform implementation supports them)
 * on the MAP-E ip6tnl interface.
 */
static bool lnx_map_mape_ip6tnl_fmr_list_configure(lnx_map_t *self, struct nl_msg *nlm)
{
    osn_map_rule_t *map_rule;
    unsigned numfmrs = 0;

    /* Check if there are any MAP rules marked as FMR rules: */
    osn_map_rulelist_foreach(self->lm_map_rules, map_rule)
    {
        if (map_rule->om_is_fmr) numfmrs++;
    }
    if (numfmrs == 0)
    {
        LOG(INFO, "lnx_map_mape: %s: No FMR rules defined", self->lm_if_name);
        return true;
    }

    /* At least 1 FMR rule, the applying of FMR rules depends on the underlying implementation: */

#ifdef IFLA_IPTUN_FMR_MAX   /* MAP-E FMR kernel implementation (originaly OpenWrt patch) */

    /* Should legacy MAP RFC Draft03 be used for MAP IPv6 address calculation: */
    if (self->lm_legacy_map_draft3)
    {
#ifdef _IFLA_IPTUN_HAS_DRAFT03_
        nla_put_u8(nlm, IFLA_IPTUN_DRAFT03, 1);
#else
    LOG(ERR, "lnx_map_mape: %s: Attempted to configure MAP RFC draft03, but the underlying kernel "
                "implementation does not indicate support for it", self->lm_if_name);
#endif
    }

    unsigned fmrcnt = 0;

    struct nlattr *fmrs = nla_nest_start(nlm, IFLA_IPTUN_FMRS);
    osn_map_rulelist_foreach(self->lm_map_rules, map_rule)
    {
        /* In the MAP rule list, 0 or more rules may be flagged as FMR rules. Skip non-FMR rules here */
        if (!map_rule->om_is_fmr)
        {
            continue;
        }

        struct nlattr *rule = nla_nest_start(nlm, ++fmrcnt);
        if (rule == NULL)
            continue;

        LOG(INFO, "lnx_map_mape: %s: Configuring FMR rule: "
            "ipv6prefix=%s,ipv4prefix=%s,ea_len=%d,psid_offset=%d",
            self->lm_if_name,
            FMT_osn_ip6_addr(map_rule->om_ipv6prefix),
            FMT_osn_ip_addr(map_rule->om_ipv4prefix),
            map_rule->om_ea_len,
            map_rule->om_psid_offset);

        nla_put(nlm, IFLA_IPTUN_FMR_IP6_PREFIX, sizeof(map_rule->om_ipv6prefix), &map_rule->om_ipv6prefix);
        nla_put(nlm, IFLA_IPTUN_FMR_IP4_PREFIX, sizeof(map_rule->om_ipv4prefix), &map_rule->om_ipv4prefix);
        nla_put_u8(nlm, IFLA_IPTUN_FMR_IP6_PREFIX_LEN, map_rule->om_ipv6prefix.ia6_prefix);
        nla_put_u8(nlm, IFLA_IPTUN_FMR_IP4_PREFIX_LEN, map_rule->om_ipv4prefix.ia_prefix);
        nla_put_u8(nlm, IFLA_IPTUN_FMR_EA_LEN, map_rule->om_ea_len);
        nla_put_u8(nlm, IFLA_IPTUN_FMR_OFFSET, map_rule->om_psid_offset);

        nla_nest_end(nlm, rule);
    }
    nla_nest_end(nlm, fmrs);

    return true;
#endif /* IFLA_IPTUN_FMR_MAX */

    LOG(WARN, "lnx_map_mape: %s: No suitable platform FMR MAP rules implementation. "
                "FMR rules will be ignored. Direct CE-2-CE communication will not work", self->lm_if_name);
    return false;
}

static uint32_t gen_local_port(void)
{
    static unsigned pcnt = 0;
    uint32_t port = (~getpid() & 0x00FFFF) << 8;
    return port + (++pcnt & 0x00FF);
}


/*
 * Create MAP-E ip6tnl interface.
 *
 * If any MAP rules marked as FMR, apply FMR rules as well (if the underlying implementation supports them).
 */
static bool lnx_map_mape_ip6tnl_create(lnx_map_t *self)
{
    struct nl_sock *sock = NULL;
    struct nl_msg *nlm = NULL;
    struct ifinfomsg ifi = { .ifi_family = AF_UNSPEC };
    unsigned link_dev_ind = 0;
    bool rv = false;
    int rc;

    if (self->lm_bmr == NULL || self->lm_map_ipv6_addr == NULL)
    {
        LOG(ERR, "lnx_map_mape: %s: Config not set or invalid. Cannot configure MAP-E ip6tnl backend",
                self->lm_if_name);
        return false;
    }
    if (self->lm_uplink_if_name == NULL)
    {
        LOG(ERR, "lnx_map_mape: %s: Uplink interface not defined", self->lm_if_name);
        return false;
    }

    /*
     * Establish netlink socket: It will be used to:
     *   - create ip6tnl MAP interface with local/remote endpoints and uplink dev as per BMR and DMR rules,
     *   - and at the same time configure FMR rules (if any and if the underlying implementation supports them).
     */
    sock = nl_socket_alloc();
    if (sock == NULL)
    {
        LOG(ERR, "lnx_map_mape: %s: Error allocating netlink socket", self->lm_if_name);
        return false;
    }

    /*
     * libnl3 auto port is conflicting with already open socket port ("-6 (Object exists)")
     * Use our own local port number generator.
     */
    uint32_t port = gen_local_port();
    nl_socket_set_local_port(sock, port);

    rc = nl_connect(sock, NETLINK_ROUTE);
    if (rc != 0)
    {
        LOG(ERR, "lnx_map_mape: %s: nl_connect failed: %d (%s)", self->lm_if_name, rc, nl_geterror(rc));
        goto out;
    }

    /* Constructing netlink message: */
    nlm = nlmsg_alloc_simple(RTM_NEWLINK, NLM_F_REQUEST | NLM_F_REPLACE | NLM_F_CREATE);
    if (!nlm)
    {
        LOG(ERR, "Error allocating netlink message");
        goto out;
    }

    nlmsg_append(nlm, &ifi, sizeof(ifi), 0);

    /* Tunnel interface name: */
    nla_put_string(nlm, IFLA_IFNAME, self->lm_if_name);

    /* Physical uplink of the tunnel: */
    link_dev_ind = if_nametoindex(self->lm_uplink_if_name);
    if (link_dev_ind)
    {
        nla_put_u32(nlm, IFLA_LINK, link_dev_ind);
    }

    struct nlattr *linkinfo = nla_nest_start(nlm, IFLA_LINKINFO);
    if (linkinfo == NULL) goto out;

    nla_put_string(nlm, IFLA_INFO_KIND, "ip6tnl");

    struct nlattr *infodata = nla_nest_start(nlm, IFLA_INFO_DATA);
    if (infodata == NULL) goto out;

    /* Physical uplink of the tunnel: */
    if (link_dev_ind)
    {
        nla_put_u32(nlm, IFLA_IPTUN_LINK, link_dev_ind);
    }

    nla_put_u8(nlm, IFLA_IPTUN_PROTO, IPPROTO_IPIP);
    nla_put_u8(nlm, IFLA_IPTUN_TTL, 64);

    if (CONFIG_OSN_LINUX_MAPE_IP6TNL_ENCAPLIMIT >= 0)
    {
        nla_put_u8(nlm, IFLA_IPTUN_ENCAP_LIMIT, CONFIG_OSN_LINUX_MAPE_IP6TNL_ENCAPLIMIT);
    }
    else
    {
        nla_put_u32(nlm, IFLA_IPTUN_FLAGS, IP6_TNL_F_IGN_ENCAP_LIMIT);
    }


    LOG(INFO, "lnx_map_mape: %s: Creating MAP-E ip6tnl interface: local=%s, remote=%s, link=%s",
            self->lm_if_name,
            FMT_osn_ip6_addr(*self->lm_map_ipv6_addr),
            FMT_osn_ip6_addr(self->lm_bmr->om_dmr),
            self->lm_uplink_if_name);

    /* Tunnel local endpoint: This CE's MAP IPv6 address: */
    nla_put(nlm, IFLA_IPTUN_LOCAL, sizeof(*self->lm_map_ipv6_addr), self->lm_map_ipv6_addr);

    /* Tunnel remote endpoint: The address of the BR (border relay): */
    nla_put(nlm, IFLA_IPTUN_REMOTE, sizeof(self->lm_bmr->om_dmr), &self->lm_bmr->om_dmr);

    /*
     * If there are any FMR rules (and the platform implementation supports them),
     * apply FMR rules as well.
     *
     * Note: FMR rules are used for direct CE to CE communication.
     */
    lnx_map_mape_ip6tnl_fmr_list_configure(self, nlm);

    nla_nest_end(nlm, infodata);
    nla_nest_end(nlm, linkinfo);

    /* Send netlink message: */
    rc = nl_send_auto_complete(sock, nlm);
    if (rc < 0)
    {
        LOG(ERR, "lnx_map_mape: %s: nl_send_auto_complete failed: %d (%s)", self->lm_if_name, rc, nl_geterror(rc));
        goto out;
    }
    rc = nl_wait_for_ack(sock);
    if (rc != 0)
    {
        LOG(ERR, "lnx_map_mape: %s: nl_wait_for_ack failed: %d (%s)", self->lm_if_name, rc, nl_geterror(rc));
        goto out;
    }

    rv = true;
out:
    if (nlm != NULL) nlmsg_free(nlm);
    if (sock != NULL) nl_socket_free(sock);

    return rv;
}

bool lnx_map_mape_config_apply(lnx_map_t *self)
{
    bool map_iface_exists;

    if (self->lm_type != OSN_MAP_TYPE_MAP_E)
    {
        return false;
    }
    /* Check if MAP config set and valid: */
    if (self->lm_bmr == NULL)
    {
        LOG(ERR, "lnx_map_mape: %s: Config not set or invalid. Cannot configure MAP-E ip6tnl",
                self->lm_if_name);
        return false;
    }

    /*
     * If config for this MAP-E MAP interface already applied (MAP-E interface exists),
     * then deconfigure the existing MAP-E config first:
     */
    if (!os_nif_exists((char *)self->lm_if_name, &map_iface_exists) || map_iface_exists)
    {
        LOG(DEBUG, "lnx_map_mape: %s: MAP interface already exists. Deconfigure its MAP config first",
                   self->lm_if_name);
        if (!lnx_map_mape_config_del(self))
        {
            return false;
        }
    }

    /* Create and configure MAP-E ip6tnl interface: */
    if (!lnx_map_mape_ip6tnl_create(self))
    {
        LOG(ERR, "lnx_map_mape: %s: Error creating ip6tnl interface", self->lm_if_name);
        return false;
    }
    LOG(INFO, "lnx_map_mape: %s: MAP-E ip6tnl interface created and configured", self->lm_if_name);
    return true;
}

bool lnx_map_mape_config_del(lnx_map_t *self)
{
    static const char iface_delete[] = _S(
        if [ -e "/sys/class/net/$1" ]; then
            ip link del "$1";
        fi;);
    int rc;

    rc = execsh_log(LOG_SEVERITY_DEBUG, iface_delete, (char *)self->lm_if_name);
    if (rc != 0)
    {
        LOG(ERR, "lnx_map_mape: %s: Error deleting ip6tnl interface", self->lm_if_name);
        return false;
    }

    LOG(INFO, "lnx_map_mape: %s: MAP-E ip6tnl interface deleted", self->lm_if_name);
    return true;
}