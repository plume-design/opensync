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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "osn_route_rule.h"
#include "lnx_route_rule.h"

#include "kconfig.h"
#include "execsh.h"
#include "log.h"
#include "util.h"
#include "const.h"

#define LNX_ROUTE_RULE_DEF_PRIORITY 30000

static bool run_iproute_rule_cmd(const char *iprule_cmd);
static bool build_iproute_rule_cmd(
        char *buff,
        int size,
        const osn_route_rule_cfg_t *rule_cfg,
        bool enable);

bool lnx_route_rule_init(lnx_route_rule_t *self)
{
    memset(self, 0, sizeof(*self));

    return true;
}

/* According to this policy routing rule configuration build a suitable
 * iproute2 command for adding/deleting such a policy route rule. */
static bool build_iproute_rule_cmd(
        char *buff,
        int size,
        const osn_route_rule_cfg_t *rule_cfg,
        bool enable)
{
    uint32_t priority;
    int n = 0;

    priority = rule_cfg->rc_priority;
    if (priority == 0 && enable)
    {
        priority = LNX_ROUTE_RULE_DEF_PRIORITY;

        LOG(WARN, "lnx_route_rule: Route rule priority not configured. "
                  "Configuring default (low) priority for it. "
                  "Please fix the configuration: Priority should be well "
                  "defined for policy routing rules.");
    }

    n += snprintf(buff + n, size - n, "ip %s rule %s priority %u",
            (rule_cfg->rc_addr_family == AF_INET ? "-4" : "-6"),
            (enable ? "add" : "del"),
            priority);
    if (n >= size) return false;

    n += snprintf(buff + n, size - n, "%s",
            (rule_cfg->rc_selector.rs_negate_rule ? " not" : ""));
    if (n >= size) return false;

    n += snprintf(buff + n, size - n, " from %s",
            (rule_cfg->rc_selector.rs_src_set ? FMT_osn_ipany_addr(rule_cfg->rc_selector.rs_src) : "all"));
    if (n >= size) return false;

    if (rule_cfg->rc_selector.rs_dst_set)
    {
        n += snprintf(buff + n, size - n, " to %s", FMT_osn_ipany_addr(rule_cfg->rc_selector.rs_dst));
        if (n >= size) return false;
    }

    if (rule_cfg->rc_selector.rs_input_if[0])
    {
        n += snprintf(buff + n, size - n, " iif %s", rule_cfg->rc_selector.rs_input_if);
        if (n >= size) return false;
    }

    if (rule_cfg->rc_selector.rs_output_if[0])
    {
        n += snprintf(buff + n, size - n, " oif %s", rule_cfg->rc_selector.rs_output_if);
        if (n >= size) return false;
    }

    if (rule_cfg->rc_selector.rs_fwmark_set)
    {
        n += snprintf(buff + n, size - n, " fwmark %u", rule_cfg->rc_selector.rs_fwmark);
        if (n >= size) return false;

        if (rule_cfg->rc_selector.rs_fwmask_set)
        {
            n += snprintf(buff + n, size - n, "/%u", rule_cfg->rc_selector.rs_fwmask);
            if (n >= size) return false;
        }
    }

    if (rule_cfg->rc_type == OSN_ROUTERULE_TYPE_BLACKHOLE)
    {
        n += snprintf(buff + n, size - n, " blackhole");
        if (n >= size) return false;
    }
    else if (rule_cfg->rc_type == OSN_ROUTERULE_TYPE_UNREACHABLE)
    {
        n += snprintf(buff + n, size - n, " unreachable");
        if (n >= size) return false;
    }
    else if (rule_cfg->rc_type == OSN_ROUTERULE_TYPE_PROHIBIT)
    {
        n += snprintf(buff + n, size - n, " prohibit");
        if (n >= size) return false;
    }
    else // unicast rule
    {
        if (rule_cfg->rc_action.ra_lookup_table != 0)
        {
            n += snprintf(buff + n, size - n, " lookup %u", rule_cfg->rc_action.ra_lookup_table);
        }
        else
        {
            n += snprintf(buff + n, size - n, " lookup main");
        }
        if (n >= size) return false;

        if (rule_cfg->rc_action.ra_suppress_prefixlength_set)
        {
            n += snprintf(buff + n, size - n, " suppress_prefixlength %u",
                    rule_cfg->rc_action.ra_suppress_prefixlength);
            if (n >= size) return false;
        }
    }

    LOG(DEBUG, "lnx_route_rule: Built iproute ip rule command: %s", buff);

    return true;
}

/* Run the specified iproute2 command on the system. */
static bool run_iproute_rule_cmd(const char *iprule_cmd)
{
    int rc;

    rc = execsh_log(LOG_SEVERITY_DEBUG, iprule_cmd);

    return (rc == 0);
}

/* Apply this policy routing rule's config: either add or remove the rule. */
static bool lnx_route_rule_cfg_apply(const osn_route_rule_cfg_t *rule_cfg, bool enable)
{
    char ip_rule_cmd[1024];

    /* Validate configuration parameters: */

    if (!(rule_cfg->rc_addr_family == AF_INET || rule_cfg->rc_addr_family == AF_INET6))
    {
        LOG(ERR, "lnx_route_rule: Invalid address family: %d", rule_cfg->rc_addr_family);
        return false;
    }

    if (rule_cfg->rc_selector.rs_src_set && (rule_cfg->rc_selector.rs_src.addr_type != rule_cfg->rc_addr_family))
    {
        LOG(ERR, "lnx_route_rule: The selector SRC IP address family is %d, but route rule address family is %d",
                rule_cfg->rc_selector.rs_src.addr_type, rule_cfg->rc_addr_family);
        return false;
    }

    if (rule_cfg->rc_selector.rs_dst_set && (rule_cfg->rc_selector.rs_dst.addr_type != rule_cfg->rc_addr_family))
    {
        LOG(ERR, "lnx_route_rule: The selector DST IP address family is %d, but route rule address family is %d",
                rule_cfg->rc_selector.rs_src.addr_type, rule_cfg->rc_addr_family);
        return false;
    }

    if (rule_cfg->rc_selector.rs_fwmask_set && !rule_cfg->rc_selector.rs_fwmark_set)
    {
        LOG(ERR, "lnx_route_rule: fwmask set but fwmark not set");
        return false;
    }

    /* Build an iproute2 command to add or delete the rule: */
    if (!build_iproute_rule_cmd(ip_rule_cmd, sizeof(ip_rule_cmd), rule_cfg, enable))
    {
        LOG(ERR, "lnx_route_rule: Error building iproute ip rule command");
        return false;
    }

    /* Run the iproute2 command: */
    if (!run_iproute_rule_cmd(ip_rule_cmd))
    {
        LOG(ERR, "Error %s policy route rule.", (enable ? "adding" : "deleting"));
        return false;
    }

    LOG(INFO, "lnx_route_rule: Policy route rule %s.", enable ? "added" : "deleted");

    return true;
}

bool lnx_route_rule_add(lnx_route_rule_t *self, const osn_route_rule_cfg_t *route_rule_cfg)
{
    (void)self;

    return lnx_route_rule_cfg_apply(route_rule_cfg, true);
}

bool lnx_route_rule_remove(lnx_route_rule_t *self, const osn_route_rule_cfg_t *route_rule_cfg)
{
    (void)self;

    return lnx_route_rule_cfg_apply(route_rule_cfg, false);
}

bool lnx_route_rule_apply(lnx_route_rule_t *self)
{
    (void)self;

    /* This implementation adds/removes policy routing rules directly without
     * any buffering. */

    return true;
}

bool lnx_route_rule_fini(lnx_route_rule_t *self)
{
    (void)self;

    return true;
}
