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

#include "osn_map.h"
#include "lnx_map.h"
#include "memutil.h"
#include "log.h"

struct osn_map
{
    lnx_map_t    om_map;
};

osn_map_t *osn_map_new(const char *if_name)
{
    osn_map_t *self = CALLOC(1, sizeof(osn_map_t));

    if (!lnx_map_init(&self->om_map, if_name))
    {
        LOG(ERR, "osn_map: %s: Error creating OSN MAP object", if_name);
        FREE(self);
        return NULL;
    }

    return self;
}

bool osn_map_type_set(osn_map_t *self, enum osn_map_type map_type)
{
    return lnx_map_type_set(&self->om_map, map_type);
}

bool osn_map_rule_list_set(osn_map_t *self, osn_map_rulelist_t *rule_list)
{
    return lnx_map_rule_list_set(&self->om_map, rule_list);
}

bool osn_map_rule_set(osn_map_t *self, const osn_map_rule_t *bmr)
{
    return lnx_map_rule_set(&self->om_map, bmr);
}

bool osn_map_enduser_IPv6_prefix_set(osn_map_t *self, const osn_ip6_addr_t *ipv6_prefix)
{
    return lnx_map_enduser_IPv6_prefix_set(&self->om_map, ipv6_prefix);
}

bool osn_map_use_legacy_map_draft3(osn_map_t *self, bool use_draft3)
{
    return lnx_map_use_legacy_map_draft3(&self->om_map, use_draft3);
}

bool osn_map_uplink_set(osn_map_t *self, const char *uplink_if_name)
{
    return lnx_map_uplink_set(&self->om_map, uplink_if_name);
}

bool osn_map_apply(osn_map_t *self)
{
    return lnx_map_apply(&self->om_map);
}

bool osn_map_rule_matched_get(osn_map_t *self, osn_map_rule_t *bmr)
{
    return lnx_map_rule_matched_get(&self->om_map, bmr);
}

bool osn_map_psid_get(osn_map_t *self, int *psid_len, int *psid)
{
    return lnx_map_psid_get(&self->om_map, psid_len, psid);
}

bool osn_map_ipv4_addr_get(osn_map_t *self, osn_ip_addr_t *map_ipv4_addr)
{
    return lnx_map_ipv4_addr_get(&self->om_map, map_ipv4_addr);
}

bool osn_map_ipv6_addr_get(osn_map_t *self, osn_ip6_addr_t *map_ipv6_addr)
{
    return lnx_map_ipv6_addr_get(&self->om_map, map_ipv6_addr);
}

bool osn_map_port_sets_get(osn_map_t *self, struct osn_map_portset *portsets, unsigned *num)
{
    return lnx_map_port_sets_get(&self->om_map, portsets, num);
}

bool osn_map_del(osn_map_t *self)
{
    bool retval = true;

    if (!lnx_map_fini(&self->om_map))
    {
        LOG(ERR, "osn_map: %s: Error destroying OSN MAP object",
                self->om_map.lm_if_name);
        retval = false;
    }
    FREE(self);
    return retval;
}

osn_map_rulelist_t *osn_map_rulelist_new()
{
    osn_map_rulelist_t *rulelist = CALLOC(1, sizeof(osn_map_rulelist_t));

    ds_dlist_init(&rulelist->rl_rule_list, osn_map_rule_t, om_dnode);

    return rulelist;
}

void osn_map_rulelist_add_rule(osn_map_rulelist_t *rule_list, const osn_map_rule_t *map_rule)
{
    ds_dlist_insert_tail(&rule_list->rl_rule_list, MEMNDUP(map_rule, sizeof(*map_rule)));
}

void osn_map_rulelist_del(osn_map_rulelist_t *rule_list)
{
    if (rule_list == NULL) return;

    while (!ds_dlist_is_empty(&rule_list->rl_rule_list))
    {
        osn_map_rule_t *map_rule = ds_dlist_remove_head(&rule_list->rl_rule_list);
        FREE(map_rule);
    }
    FREE(rule_list);
}

bool osn_map_rulelist_is_empty(const osn_map_rulelist_t *rule_list)
{
    if (rule_list == NULL)
    {
        return true;
    }

    return ds_dlist_is_empty((ds_dlist_t *)rule_list); // just call the parent object function
}

osn_map_rulelist_t *osn_map_rulelist_copy(const osn_map_rulelist_t *rule_list)
{
    osn_map_rulelist_t *rule_list_orig = (osn_map_rulelist_t *)rule_list;
    osn_map_rulelist_t *rule_list_new;
    osn_map_rule_t *map_rule;

    if (rule_list == NULL) return NULL;

    rule_list_new = osn_map_rulelist_new();

    osn_map_rulelist_foreach(rule_list_orig, map_rule)
    {
        osn_map_rulelist_add_rule(rule_list_new, map_rule);
    }

    return rule_list_new;
}
