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

#ifndef LNX_MAP_H_INCLUDED
#define LNX_MAP_H_INCLUDED

#include "osn_map.h"
#include "osn_types.h"
#include "ds_dlist.h"
#include "osn_netif.h"

typedef struct lnx_map lnx_map_t;

/* MAP object */
struct lnx_map
{
    const char             *lm_if_name;             /* MAP interface name */

    enum osn_map_type       lm_type;                /* MAP type */

    osn_map_rulelist_t     *lm_map_rules;           /* List of MAP rules */

    osn_ip6_addr_t         *lm_enduser_ipv6_prefix; /* MAP End-user IPv6 prefix */

    osn_map_rule_t         *lm_bmr;                 /* BMR; Pointer to matched MAP rule */

    /* Derived values: */
    int                     lm_psid_len;            /* PSID length */
    int                     lm_psid;                /* PSID */

    osn_ip_addr_t          *lm_map_ipv4_addr;       /* MAP IPv4 address */
    osn_ip6_addr_t         *lm_map_ipv6_addr;       /* MAP IPv6 address */

    struct osn_map_portset  lm_portsets[OSN_MAP_PORT_SETS_MAX];  /* Port sets */
    unsigned                lm_portsets_num;                     /* Number of port sets */

    bool                    lm_legacy_map_draft3;   /* Use legacy MAP RFC Draft 03 */

    const char             *lm_uplink_if_name;      /* Uplink interface name */

    bool                    lm_cfg_applied;

    osn_netif_t            *lm_netif;   /* For MAP interface UP/DOWN notification */
    bool                    lm_if_up;   /* Cached MAP interface UP/DOWN state */

};

bool lnx_map_init(lnx_map_t *self, const char *name);

bool lnx_map_fini(lnx_map_t *self);

bool lnx_map_type_set(lnx_map_t *self, enum osn_map_type map_type);

bool lnx_map_rule_list_set(lnx_map_t *self, const osn_map_rulelist_t *rule_list);

bool lnx_map_rule_set(lnx_map_t *self, const osn_map_rule_t *bmr);

bool lnx_map_enduser_IPv6_prefix_set(lnx_map_t *self, const osn_ip6_addr_t *ipv6_prefix);

bool lnx_map_use_legacy_map_draft3(lnx_map_t *self, bool use_draft3);

bool lnx_map_uplink_set(lnx_map_t *self, const char *uplink_if_name);

bool lnx_map_apply(lnx_map_t *self);

bool lnx_map_rule_matched_get(lnx_map_t *self, osn_map_rule_t *bmr);

bool lnx_map_psid_get(lnx_map_t *self, int *psid_len, int *psid);

bool lnx_map_ipv4_addr_get(lnx_map_t *self, osn_ip_addr_t *map_ipv4_addr);

bool lnx_map_ipv6_addr_get(lnx_map_t *self, osn_ip6_addr_t *map_ipv6_addr);

bool lnx_map_port_sets_get(lnx_map_t *self, struct osn_map_portset *portsets, unsigned *num);

#endif /* LNX_MAP_H_INCLUDED */