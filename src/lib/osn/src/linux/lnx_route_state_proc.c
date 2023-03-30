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

#include <net/route.h>

#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>
#include <regex.h>

#include "lnx_route_state.h"

#include "osn_routes.h"
#include "osn_types.h"
#include "os_util.h"
#include "os_regex.h"
#include "memutil.h"
#include "const.h"
#include "util.h"
#include "log.h"
#include "ds.h"

#define LNX_ROUTE_PROC_NET_ROUTE   "/proc/net/route"

/**
 *
 * Linux route state polling by parsing /proc/net/route.
 *
 */

struct lnx_route_state
{
    lnx_route_state_update_fn_t   *rs_cache_update_fn;

};

static void lnx_route_state_poll_proc(lnx_route_state_t *self);

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

lnx_route_state_t *lnx_route_state_new(lnx_route_state_update_fn_t *rt_cache_update_fn)
{
    lnx_route_state_t *self = CALLOC(1, sizeof(lnx_route_state_t));

    self->rs_cache_update_fn = rt_cache_update_fn;

    return self;
}

void lnx_route_state_del(lnx_route_state_t *self)
{
    FREE(self);
}

void lnx_route_state_poll(lnx_route_state_t *self)
{
    lnx_route_state_poll_proc(self);
}

static bool route_osn_ip_addr_from_hexstr(osn_ip_addr_t *ip, const char *str)
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

/*
 * Poll for routes by parsing /proc/net/route.
 *
 * Note: /proc/net/route is generally supported on any Linux system, but
 * it has limitations:
 * - reports only routes from the main routing table
 * - no other route attributes such as the preferred source address reported
 *
 * If you need the above route attributes reported consider using the
 * netlink/libnl3 implementation of route state reporting.
 */
static void lnx_route_state_poll_proc(lnx_route_state_t *self)
{
    lnx_route_state_update_fn_t *cache_update_fn;

    cache_update_fn = self->rs_cache_update_fn;

    FILE *frt = NULL;

    char buf[256];
    regex_t re_route;

    LOG(DEBUG, "route: Poll.");

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

        if (!route_osn_ip_addr_from_hexstr(&rts.rts_route.dest, r_dest))
        {
            LOG(ERR, "route: Invalid destination address: %s -- %s", r_dest, buf);
            continue;
        }

        osn_ip_addr_t mask;
        if (!route_osn_ip_addr_from_hexstr(&mask, r_mask))
        {
            LOG(ERR, "route: Invalid netmask address: %s -- %s", r_mask, buf);
            continue;
        }
        rts.rts_route.dest.ia_prefix = osn_ip_addr_to_prefix(&mask);

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
        rts.rts_route.metric = (int)metric;

        if (flags & RTF_GATEWAY)
        {
            /*
             * This route has a gateway, parse the IP and try to resolve the MAC address.
             */
            rts.rts_route.gw_valid = route_osn_ip_addr_from_hexstr(&rts.rts_route.gw, r_gateway);
            if (!rts.rts_route.gw_valid)
            {
                LOG(ERR, "route: Invalid gateway address %s.", r_gateway);
                continue;
            }
        }

        if (cache_update_fn != NULL)
        {
            cache_update_fn(r_ifname, &rts);
        }
    }

error:
    regfree(&re_route);
    if (frt != NULL) fclose(frt);

    return;
}
