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
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <arpa/inet.h>
#include <asm/byteorder.h>

#include "lnx_map.h"
#include "lnx_map_mapt.h"
#include "lnx_map_mape.h"

#include "osn_types.h"
#include "osn_netif.h"
#include "memutil.h"
#include "execsh.h"
#include "util.h"
#include "log.h"
#include "ds.h"

static void lnx_map_netif_update(osn_netif_t *self, struct osn_netif_status *status);
static bool lnx_map_ndp_proxy_configure(lnx_map_t *self, bool enable);

bool lnx_map_init(lnx_map_t *self, const char *name)
{
    if (name == NULL || *name == 0)
    {
        return false;
    }

    memset(self, 0, sizeof(*self));

    self->lm_psid_len = -1;
    self->lm_psid = -1;

    self->lm_if_name = STRDUP(name);

    self->lm_netif = osn_netif_new(name);
    osn_netif_data_set(self->lm_netif, self);
    osn_netif_status_notify(self->lm_netif, lnx_map_netif_update);

    return true;
}

bool lnx_map_fini(lnx_map_t *self)
{
    bool rv = true;

    lnx_map_ndp_proxy_configure(self, false);

    if (self->lm_type == OSN_MAP_TYPE_MAP_T)
    {
        rv &= lnx_map_mapt_config_del(self);
    }
    if (self->lm_type == OSN_MAP_TYPE_MAP_E)
    {
        rv &= lnx_map_mape_config_del(self);
    }

    FREE(self->lm_if_name);
    FREE(self->lm_enduser_ipv6_prefix);
    FREE(self->lm_map_ipv4_addr);
    FREE(self->lm_map_ipv6_addr);
    FREE(self->lm_uplink_if_name);

    osn_map_rulelist_del(self->lm_map_rules);

    osn_netif_status_notify(self->lm_netif, NULL);
    osn_netif_del(self->lm_netif);

    return rv;
}

bool lnx_map_type_set(lnx_map_t *self, enum osn_map_type map_type)
{
    LOG(TRACE, "%s: map_type=%d", __func__, map_type);

    self->lm_type = map_type;
    return true;
}

bool lnx_map_rule_list_set(lnx_map_t *self, const osn_map_rulelist_t *rule_list)
{
    osn_map_rule_t *map_rule;

    /* Delete any current rule list first: */
    osn_map_rulelist_del(self->lm_map_rules);

    /* Set the map rule list: */
    self->lm_map_rules = osn_map_rulelist_copy(rule_list);

    /* Unmark the BMR: */
    self->lm_bmr = NULL;

    if (osn_map_rulelist_is_empty(self->lm_map_rules))
    {
        LOG(DEBUG, "lnx_map: %s: Rule list: Empty MAP rule list configured", self->lm_if_name);
        return true;
    }

    /* DEBUG logs: */
    osn_map_rulelist_foreach(self->lm_map_rules, map_rule)
    {
        LOG(DEBUG, "lnx_map: %s: Rule list: MAP rule configured: "
            "ipv6prefix=%s,ipv4prefix=%s,ea_len=%d,psid_offset=%d,dmr=%s,psid=%d,psid_len=%d [fmr_flag=%s]",
            self->lm_if_name,
            FMT_osn_ip6_addr(map_rule->om_ipv6prefix),
            FMT_osn_ip_addr(map_rule->om_ipv4prefix),
            map_rule->om_ea_len,
            map_rule->om_psid_offset,
            FMT_osn_ip6_addr(map_rule->om_dmr),
            map_rule->om_psid,
            map_rule->om_psid_len,
            map_rule->om_is_fmr ? "true" : "false");
    }

    return true;
}

bool lnx_map_rule_set(lnx_map_t *self, const osn_map_rule_t *bmr)
{
    if (bmr == NULL)
    {
        return false;
    }

    osn_map_rulelist_t *rule_list = osn_map_rulelist_new();
    osn_map_rulelist_add_rule(rule_list, bmr);

    lnx_map_rule_list_set(self, rule_list);

    osn_map_rulelist_del(rule_list);

    return true;
}

bool lnx_map_enduser_IPv6_prefix_set(lnx_map_t *self, const osn_ip6_addr_t *ipv6_prefix)
{
    LOG(TRACE, "%s: ipv6_prefix="PRI_osn_ip6_addr, __func__,
            ipv6_prefix != NULL ? FMT_osn_ip6_addr(*ipv6_prefix) : "null");

    if (ipv6_prefix != NULL && ipv6_prefix->ia6_prefix == -1)
    {
        return false;
    }

    FREE(self->lm_enduser_ipv6_prefix);

    self->lm_enduser_ipv6_prefix = ipv6_prefix != NULL ? MEMNDUP(ipv6_prefix, sizeof(*ipv6_prefix)) : NULL;
    return true;
}

bool lnx_map_use_legacy_map_draft3(lnx_map_t *self, bool use_draft3)
{
    self->lm_legacy_map_draft3 = use_draft3;
    return true;
}

bool lnx_map_uplink_set(lnx_map_t *self, const char *uplink_if_name)
{
    LOG(TRACE, "%s: uplink_if_name='%s'", __func__, uplink_if_name != NULL ? uplink_if_name : "null");

    FREE(self->lm_uplink_if_name);

    self->lm_uplink_if_name = uplink_if_name != NULL ? STRDUP(uplink_if_name) : NULL;

    return true;
}

static int mem_compare_bits(const void *_a, const void *_b, size_t nbits)
{
    const uint8_t *a = _a;
    const uint8_t *b = _b;
    size_t nbytes;
    int rv;

    nbytes = nbits / 8;   // number of whole bytes
    nbits = nbits % 8;    // number of remaining bits

    rv = memcmp(a, b, nbytes);
    if (rv != 0) return rv;

    if (nbits > 0)
    {
        rv = (a[nbytes] >> (8 - nbits)) - (b[nbytes] >> (8 - nbits));
    }
    return rv;
}

static void mem_copy_bits(void *_a, const void *_b, size_t nbits)
{
    uint8_t *a = _a;
    const uint8_t *b = _b;
    size_t nbytes;

    nbytes = nbits / 8;    // number of whole bytes
    nbits = nbits % 8;     // number of remaining bits

    memcpy(a, b, nbytes);

    if (nbits > 0)
    {
        uint8_t mask = 0xff >> nbits;

        a[nbytes] &= mask;
        a[nbytes] |= b[nbytes] & (~mask);
    }
}

static void mem_copy_bits_from(void *_a, const void *_b, size_t frombits, size_t nbits)
{
    uint64_t buf = 0;
    const uint8_t *b = _b;
    size_t frombyte;
    size_t tobyte;

    frombyte = frombits / 8;
    tobyte = (frombits + nbits) / 8;

    memcpy(&buf, &b[frombyte], tobyte - frombyte + 1);
    buf = __be64_to_cpu(buf) << (frombits % 8);   // in the whole byte, cut off any bits left to frombits
    buf = __cpu_to_be64(buf);

    mem_copy_bits(_a, &buf, nbits);
}

static bool prefix_matches(const struct in6_addr *enduser_ipv6prefix, int enduser_ipv6prefix_len,
                           const struct in6_addr *rule_ipv6prefix, int rule_ipv6prefix_len)
{
    if (enduser_ipv6prefix_len >= rule_ipv6prefix_len
            && mem_compare_bits(enduser_ipv6prefix, rule_ipv6prefix, rule_ipv6prefix_len) == 0)
    {
        return true;
    }
    return false;
}

static bool lnx_map_find_matched_BMR(lnx_map_t *self)
{
    osn_map_rule_t *rule;
    struct in6_addr enduser_ipv6prefix;
    struct in6_addr rule_ipv6prefix;
    int enduser_ipv6prefix_len;
    int rule_ipv6prefix_len;
    int longest_match_len = 0;

    if (osn_map_rulelist_is_empty(self->lm_map_rules))
    {
        LOG(ERR, "lnx_map: %s: No MAP rules configured", self->lm_if_name);
        return false;
    }
    if (self->lm_enduser_ipv6_prefix == NULL)
    {
        LOG(ERR, "lnx_map: %s: No End-user IPv6 prefix configured", self->lm_if_name);
        return false;
    }

    self->lm_bmr = NULL; /* Init as matched rule not found */

    enduser_ipv6prefix = self->lm_enduser_ipv6_prefix->ia6_addr;
    enduser_ipv6prefix_len = self->lm_enduser_ipv6_prefix->ia6_prefix;

    /* For each configured MAP rule on the list... */
    osn_map_rulelist_foreach(self->lm_map_rules, rule)
    {
        rule_ipv6prefix = rule->om_ipv6prefix.ia6_addr;
        rule_ipv6prefix_len = rule->om_ipv6prefix.ia6_prefix;

        /* ...check if the MAP rule IPv6 prefix matches the End-user IPv6 prefix: */
        if (prefix_matches(&enduser_ipv6prefix, enduser_ipv6prefix_len, &rule_ipv6prefix, rule_ipv6prefix_len)
                &&  rule_ipv6prefix_len > longest_match_len)
        {
            self->lm_bmr = rule; /* Mark the matched rule */

            longest_match_len = rule_ipv6prefix_len; /* Mark the current longest match */

            LOG(DEBUG, "lnx_map: %s: rule_ipv6prefix: %s: matches",
                    self->lm_if_name, FMT_osn_ip6_addr(rule->om_ipv6prefix));
        }
    }

    if (self->lm_bmr != NULL)
    {
        LOG(DEBUG, "lnx_map: %s: rule_ipv6prefix: %s: longest match",
                self->lm_if_name, FMT_osn_ip6_addr(self->lm_bmr->om_ipv6prefix));

        return true;
    }
    else
    {
        return false; /* No matched rule found */
    }
}

static bool lnx_map_calculate(lnx_map_t *self)
{
    struct in6_addr enduser_ipv6prefix;
    struct in6_addr BMR_ipv6prefix;
    struct in_addr BMR_ipv4prefix;
    int enduser_ipv6prefix_len;
    int BMR_ipv6prefix_len;
    int BMR_ipv4prefix_len;
    int ea_len;
    int psid_offset;
    int psid_len;
    int psid;
    uint16_t psid_be16 = 0;
    struct in_addr map_ipv4addr;
    struct in6_addr map_ipv6addr;

    /* BMR and End-user IPv6 prefix must be set at this point: */
    if (self->lm_bmr == NULL || self->lm_enduser_ipv6_prefix == NULL)
    {
        return false;
    }

    enduser_ipv6prefix = self->lm_enduser_ipv6_prefix->ia6_addr;
    enduser_ipv6prefix_len = self->lm_enduser_ipv6_prefix->ia6_prefix;

    BMR_ipv6prefix = self->lm_bmr->om_ipv6prefix.ia6_addr;
    BMR_ipv6prefix_len = self->lm_bmr->om_ipv6prefix.ia6_prefix;

    BMR_ipv4prefix = self->lm_bmr->om_ipv4prefix.ia_addr;
    BMR_ipv4prefix_len = self->lm_bmr->om_ipv4prefix.ia_prefix;

    ea_len = self-> lm_bmr->om_ea_len;
    psid_offset = self->lm_bmr->om_psid_offset;

    psid_len = self->lm_bmr->om_psid_len;
    psid = self->lm_bmr->om_psid;

    if (!(ea_len >= 0 && ea_len <= 48))
    {
        LOG(ERR, "lnx_map: %s: calculate: EA bits length: not set or invalid value: %d",
                self->lm_if_name, ea_len);
        return false;
    }

    /* If psid_offset not set, default to 6 (exclude system ports 0-1023) */
    if (psid_offset < 0)
    {
        LOG(NOTICE, "lnx_map: %s: calculate: PSID offset not set: Defaulting to PSID_offset=6",
                self->lm_if_name);
        psid_offset = 6;
    }

    /* Check if End-user IPv6 prefix matches the MAP rule IPv6 prefix: */
    if (!prefix_matches(&enduser_ipv6prefix, enduser_ipv6prefix_len, &BMR_ipv6prefix, BMR_ipv6prefix_len))
    {
        enduser_ipv6prefix_len = -1;
        LOG(WARN, "lnx_map: %s: calculate: End-user IPv6 prefix %s does not match the BMR IPv6 prefix %s",
                    self->lm_if_name,
                    FMT_osn_ip6_addr(*self->lm_enduser_ipv6_prefix),
                    FMT_osn_ip6_addr(self->lm_bmr->om_ipv6prefix));

        return false;
    }

    /* ----- CALCULATION starts here: ----- */

    /* Calculate PSID len, if not explicitly set */
    if (psid_len <= 0)
    {
        psid_len = ea_len - (32 - BMR_ipv4prefix_len);
        if (psid_len < 0) psid_len = 0;

        psid = -1; // explicit PSID_len NOT set --> assume explicit PSID not set, i.e. to be calculated
    }

    /* Basic validation: */
    if (BMR_ipv4prefix_len < 0 || BMR_ipv6prefix_len < 0 || ea_len < 0 || psid_len > 16 || ea_len < psid_len)
    {
        LOG(ERR, "lnx_map: %s: calculate: Invalid MAP configuration", self->lm_if_name);
        return false;
    }

    /* Derive PSID: */
    if (psid < 0 && psid_len >= 0 && psid_len <= 16 && enduser_ipv6prefix_len >= 0 && ea_len >= psid_len)
    {
        mem_copy_bits_from(&psid_be16, &enduser_ipv6prefix, BMR_ipv6prefix_len + ea_len - psid_len, psid_len);
        psid = __be16_to_cpu(psid_be16);
    }
    if (psid_len > 0)
    {
        psid_be16 = __cpu_to_be16(psid >> (16 - psid_len));
    }

    /* Calculate MAP IPv4 address: */
    if ((enduser_ipv6prefix_len >= 0 || ea_len == psid_len) && ea_len >= psid_len)
    {
        uint32_t suffix = 0;

        memset(&map_ipv4addr, 0, sizeof(map_ipv4addr));

        mem_copy_bits_from(&suffix, &enduser_ipv6prefix, BMR_ipv6prefix_len, ea_len - psid_len);  // get IPv4 suffix from EA-bits
        map_ipv4addr.s_addr = htonl(ntohl(suffix) >> BMR_ipv4prefix_len);     // set IPv4 suffix
        mem_copy_bits(&map_ipv4addr, &BMR_ipv4prefix, BMR_ipv4prefix_len);    // copy IPv4 prefix from BMR
    }

    /* Calculate MAP IPv6 address: */
    if (enduser_ipv6prefix_len >= 0)
    {
        size_t v4offset = 10;

        if (self->lm_legacy_map_draft3) // affects the interface-identifier part of MAP IPv6 addr
        {
            v4offset = 9;
        }

        memset(&map_ipv6addr, 0, sizeof(map_ipv6addr));

        memcpy(&map_ipv6addr.s6_addr[v4offset], &map_ipv4addr, 4);
        memcpy(&map_ipv6addr.s6_addr[v4offset + 4], &psid_be16, 2);
        mem_copy_bits(&map_ipv6addr, &enduser_ipv6prefix,
                (BMR_ipv6prefix_len + ea_len) < enduser_ipv6prefix_len ?
                    (BMR_ipv6prefix_len + ea_len) : enduser_ipv6prefix_len);
    }

    /* Calculate port sets: */
    self->lm_portsets_num = 0;
    if (psid_len > 0 && psid >= 0)
    {
        for (int k = (psid_offset) ? 1 : 0; k < (1 << psid_offset); k++)
        {
            int start = (k << (16 - psid_offset)) | (psid >> psid_offset);
            int end = start + (1 << (16 - psid_offset - psid_len)) - 1;

            if (start == 0)
            {
                start = 1;
            }

            if (start <= end)
            {
                self->lm_portsets[self->lm_portsets_num].op_from = start;
                self->lm_portsets[self->lm_portsets_num].op_to = end;

                self->lm_portsets_num++;

                if (self->lm_portsets_num == OSN_MAP_PORT_SETS_MAX)
                {
                    LOG(WARN, "lnx_map: calculate: %s: Max number of port sets reached",
                            self->lm_if_name);
                    break;
                }
            }
        }
    }

    /* Set the rest of the calculated values: */
    self->lm_psid_len = psid_len;
    self->lm_psid = psid >> (16 - psid_len);

    FREE(self->lm_map_ipv4_addr);
    self->lm_map_ipv4_addr = CALLOC(1, sizeof(*self->lm_map_ipv4_addr));
    osn_ip_addr_from_in_addr(self->lm_map_ipv4_addr, &map_ipv4addr);

    FREE(self->lm_map_ipv6_addr);
    self->lm_map_ipv6_addr = CALLOC(1, sizeof(*self->lm_map_ipv6_addr));
    *self->lm_map_ipv6_addr = OSN_IP6_ADDR_INIT;
    self->lm_map_ipv6_addr->ia6_addr = map_ipv6addr;

    return true;
}

bool lnx_map_apply(lnx_map_t *self)
{
    self->lm_cfg_applied = false;

    /* Validate configuration parameters: */
    if (!(self->lm_type == OSN_MAP_TYPE_MAP_T || self->lm_type == OSN_MAP_TYPE_MAP_E))
    {
        LOG(ERR, "lnx_map: %s: Applying MAP: MAP type not set or unsupported MAP type", self->lm_if_name);
        return false;
    }

    /* At least 1 MAP rule must be configured: */
    if (osn_map_rulelist_is_empty(self->lm_map_rules))
    {
        LOG(WARN, "lnx_map: %s: Applying MAP: No MAP rules configured. MAP will be disabled!", self->lm_if_name);
        return false;
    }

    /* End-user IPv6 prefix must be configured: */
    if (self->lm_enduser_ipv6_prefix == NULL)
    {
        LOG(ERR, "lnx_map: %s: Applying MAP: No End-user IPv6 prefix configured", self->lm_if_name);
        return false;
    }

    /* From configured MAP rules, determine the BMR rule: */
    if (!lnx_map_find_matched_BMR(self) || self->lm_bmr == NULL)
    {
        LOG(ERR, "lnx_map: %s: Applying MAP: No matched MAP rule found", self->lm_if_name);
        return false;
    }

    LOG(INFO, "lnx_map: %s: Applying MAP: Using MAP rule as BMR: "
            "ipv6prefix=%s,ipv4prefix=%s,ea_len=%d,psid_offset=%d,dmr=%s,psid=%d,psid_len=%d",
            self->lm_if_name,
            FMT_osn_ip6_addr(self->lm_bmr->om_ipv6prefix),
            FMT_osn_ip_addr(self->lm_bmr->om_ipv4prefix),
            self->lm_bmr->om_ea_len,
            self->lm_bmr->om_psid_offset,
            FMT_osn_ip6_addr(self->lm_bmr->om_dmr),
            self->lm_bmr->om_psid,
            self->lm_bmr->om_psid_len);

    LOG(INFO, "lnx_map: %s: Applying MAP: Using End-user IPv6 prefix: %s",
            self->lm_if_name, FMT_osn_ip6_addr(*self->lm_enduser_ipv6_prefix));

    /* Calculate derived MAP parameters: */
    if (!lnx_map_calculate(self))
    {
        LOG(ERR, "lnx_map: %s: Applying MAP: Error calculating MAP parameters ", self->lm_if_name);
        return false;
    }
    LOG(INFO, "lnx_map: %s: Applying MAP: MAP parameters calculated.", self->lm_if_name);

    /* If MAP-T then configure MAP-T backend: */
    if (self->lm_type == OSN_MAP_TYPE_MAP_T)
    {
        if (!lnx_map_mapt_config_apply(self))
        {
            LOG(ERR, "lnx_map: %s: Error configuring MAP-T backend", self->lm_if_name);
            return false;
        }
        LOG(INFO, "lnx_map: %s: Configured MAP-T backend", self->lm_if_name);
    }

    /* If MAP-E then configure MAP-E backend: */
    if (self->lm_type == OSN_MAP_TYPE_MAP_E)
    {
        if (!lnx_map_mape_config_apply(self))
        {
            LOG(ERR, "lnx_map: %s: Error configuring MAP-E backend", self->lm_if_name);
            return false;
        }
        LOG(INFO, "lnx_map: %s: Configured MAP-E backend", self->lm_if_name);
    }

    /* Configure NDP proxy: */
    lnx_map_ndp_proxy_configure(self, true);

    LOG(INFO, "lnx_map: %s: MAP config applied", self->lm_if_name);
    self->lm_cfg_applied = true;

    return true;
}

bool lnx_map_rule_matched_get(lnx_map_t *self, osn_map_rule_t *bmr)
{
    if (!self->lm_cfg_applied || self->lm_bmr == NULL)
    {
        return false;
    }

    *bmr = *self->lm_bmr;
    return true;
}

bool lnx_map_psid_get(lnx_map_t *self, int *psid_len, int *psid)
{
    LOG(TRACE, "%s: psid_len=%u, psid=%u", __func__, self->lm_psid_len, self->lm_psid);

    if (!self->lm_cfg_applied)
    {
        return false;
    }

    *psid_len = self->lm_psid_len;
    *psid = self->lm_psid;

    return true;
}

bool lnx_map_ipv4_addr_get(lnx_map_t *self, osn_ip_addr_t *map_ipv4_addr)
{
    LOG(TRACE, "%s: map_ipv4_addr="PRI_osn_ip_addr, __func__,
        self->lm_map_ipv4_addr != NULL ? FMT_osn_ip_addr(*self->lm_map_ipv4_addr) : "null");

    if (!self->lm_cfg_applied)
    {
        return false;
    }
    if (self->lm_map_ipv4_addr == NULL)
    {
        return false;
    }

    *map_ipv4_addr = *self->lm_map_ipv4_addr;
    return true;
}

bool lnx_map_ipv6_addr_get(lnx_map_t *self, osn_ip6_addr_t *map_ipv6_addr)
{
    LOG(TRACE, "%s: map_ipv6_addr="PRI_osn_ip6_addr, __func__,
        self->lm_map_ipv6_addr != NULL ? FMT_osn_ip6_addr(*self->lm_map_ipv6_addr) : "null");

    if (!self->lm_cfg_applied)
    {
        return false;
    }

    if (self->lm_map_ipv6_addr == NULL)
    {
        return false;
    }

    *map_ipv6_addr = *self->lm_map_ipv6_addr;
    return true;
}

bool lnx_map_port_sets_get(lnx_map_t *self, struct osn_map_portset *portsets, unsigned *num)
{
    unsigned i;

    if (!self->lm_cfg_applied)
    {
        return false;
    }

    for (i = 0; i < self->lm_portsets_num; i++)
    {
        portsets[i].op_from = self->lm_portsets[i].op_from;
        portsets[i].op_to = self->lm_portsets[i].op_to;
    }
    *num = self->lm_portsets_num;

    return true;
}

static bool lnx_map_mapt_add_route6_map_iface_ipv6(lnx_map_t *self)
{
    char cmd_str[256];
    int rc;

    if (self->lm_map_ipv6_addr == NULL)
    {
        return false;
    }

    snprintf(cmd_str, sizeof(cmd_str), "ip -6 route add %s dev %s proto static",
            FMT_osn_ip6_addr(*self->lm_map_ipv6_addr),
            self->lm_if_name);

    rc = execsh_log(LOG_SEVERITY_INFO, cmd_str);
    if (rc != 0)
    {
        LOG(ERR, "lnx_map: %s: Error executing cmd '%s'", self->lm_if_name, cmd_str);
        return false;
    }

    return true;
}

static void lnx_map_netif_update(osn_netif_t *self, struct osn_netif_status *status)
{
    lnx_map_t *lnx_map = osn_netif_data_get(self);

    if (lnx_map->lm_type == OSN_MAP_TYPE_MAP_T && status->ns_up && !lnx_map->lm_if_up)
    {
        LOG(NOTICE, "lnx_map: %s: MAP-T interface state DOWN-->UP: Configure IPv6 route",
             lnx_map->lm_if_name);

        /* MAP IPv6 address should not be assigned to the MAP-T interface for MAP-T to work.
         * However, an IPv6 route is still needed. Currently such a route cannot be configured
         * by controller. Thus we configure such a route here: */
        if (!lnx_map_mapt_add_route6_map_iface_ipv6(lnx_map))
        {
            LOG(WARN, "lnx_map_mapt: %s: Error configuring MAP IPv6 addr route for the MAP-T interface",
                lnx_map->lm_if_name);
        }
    }
    lnx_map->lm_if_up = status->ns_up; // Remember the last iface UP/DOWN state
}

/* Configure NDP proxy. */
static bool lnx_map_ndp_proxy_configure(lnx_map_t *self, bool enable)
{
    char cmd_str[256];
    bool rv = true;
    int rc;

    if (self->lm_map_ipv6_addr == NULL)
    {
        return false;
    }
    if (self->lm_uplink_if_name == NULL)
    {
        LOG(WARN, "lnx_map: %s: Cannot configure NDP proxy:"
            " uplink interface not set", self->lm_if_name);
        return false;
    }

    LOG(INFO, "lnx_map: %s: Configuring NDP proxy: uplink=%s: enable=%d",
            self->lm_if_name, self->lm_uplink_if_name, enable);

    /* For sysctl NDP proxy enablement: We only either enable it
     * as to not clash with any other service: */
    if (enable)
    {
        snprintf(
            cmd_str,
            sizeof(cmd_str),
            "sysctl -w net.ipv6.conf.%s.proxy_ndp=%d; ",
            self->lm_uplink_if_name, enable);
        rc = execsh_log(LOG_SEVERITY_INFO, cmd_str);
        if (rc != 0)
        {
            LOG(ERR, "lnx_map: %s: Error executing cmd '%s'", self->lm_if_name, cmd_str);
            rv = false;
        }
    }

    /* The actual proxy to the MAP IPv6: Either add or delete the proxy: */
    snprintf(
        cmd_str,
        sizeof(cmd_str),
        "ip -6 neigh %s proxy %s dev %s",
        enable ? "add" : "del",
        FMT_osn_ip6_addr(*self->lm_map_ipv6_addr),
        self->lm_uplink_if_name
        );
    rc = execsh_log(LOG_SEVERITY_INFO, cmd_str);
    if (rc != 0)
    {
        LOG(ERR, "lnx_map: %s: Error executing cmd '%s'", self->lm_if_name, cmd_str);
        rv = false;
    }

    return rv;
}
