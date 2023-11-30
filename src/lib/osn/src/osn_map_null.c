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

};

osn_map_t *osn_map_new(const char *if_name)
{
    osn_map_t *self = CALLOC(1, sizeof(osn_map_t));

    return self;
}

bool osn_map_type_set(osn_map_t *self, enum osn_map_type map_type)
{
    (void)self;
    (void)map_type;
    return true;
}

bool osn_map_rule_list_set(osn_map_t *self, osn_map_rulelist_t *rule_list)
{
    (void)self;
    (void)rule_list;
    return true;
}

bool osn_map_rule_set(osn_map_t *self, const osn_map_rule_t *bmr)
{
    (void)self;
    (void)bmr;
    return true;
}

bool osn_map_enduser_IPv6_prefix_set(osn_map_t *self, const osn_ip6_addr_t *ipv6_prefix)
{
    (void)self;
    (void)ipv6_prefix;
    return true;
}

bool osn_map_use_legacy_map_draft3(osn_map_t *self, bool use_draft3)
{
    (void)self;
    (void)use_draft3;
    return true;
}

bool osn_map_uplink_set(osn_map_t *self, const char *uplink_if_name)
{
    (void)self;
    (void)uplink_if_name;
    return true;
}

bool osn_map_apply(osn_map_t *self)
{
    (void)self;
    return true;
}

bool osn_map_rule_matched_get(osn_map_t *self, osn_map_rule_t *bmr)
{
    (void)self;
    (void)bmr;
    return false;
}

bool osn_map_psid_get(osn_map_t *self, int *psid_len, int *psid)
{
    (void)self;
    (void)psid_len;
    (void)psid;
    return false;
}

bool osn_map_ipv4_addr_get(osn_map_t *self, osn_ip_addr_t *map_ipv4_addr)
{
    (void)self;
    (void)map_ipv4_addr;
    return false;
}

bool osn_map_ipv6_addr_get(osn_map_t *self, osn_ip6_addr_t *map_ipv6_addr)
{
    (void)self;
    (void)map_ipv6_addr;
    return false;
}

bool osn_map_port_sets_get(osn_map_t *self, struct osn_map_portset *portsets, unsigned *num)
{
    (void)self;
    (void)portsets;
    (void)num;
    return false;
}

bool osn_map_del(osn_map_t *self)
{
    FREE(self);
    return true;
}

osn_map_rulelist_t *osn_map_rulelist_new()
{
    return NULL;
}

void osn_map_rulelist_add_rule(osn_map_rulelist_t *rule_list, const osn_map_rule_t *map_rule)
{
    (void)rule_list;
    (void)map_rule;
}

void osn_map_rulelist_del(osn_map_rulelist_t *rule_list)
{
    (void)rule_list;
}

bool osn_map_rulelist_is_empty(const osn_map_rulelist_t *rule_list)
{
    (void)rule_list;
    return true;
}
