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
#include <stdbool.h>
#include <memutil.h>
#include <log.h>

#include "osn_adaptive_qos.h"

/**
 * NULL dummy Adaptive QoS implementation.
 */

struct osn_adaptive_qos
{
    /* Nothing here. */
};

osn_adaptive_qos_t *osn_adaptive_qos_new(const char *DL_ifname, const char *UL_ifname)
{
    osn_adaptive_qos_t *self = CALLOC(1, sizeof(osn_adaptive_qos_t));

    return self;
}

bool osn_adaptive_qos_DL_shaper_adjust_set(osn_adaptive_qos_t *self, bool shaper_adjust)
{
    (void)self;
    (void)shaper_adjust;

    return true;
}

bool osn_adaptive_qos_DL_shaper_params_set(osn_adaptive_qos_t *self, int min_rate, int base_rate, int max_rate)
{
    (void)self;
    (void)min_rate;
    (void)base_rate;
    (void)max_rate;

    return true;
}

bool osn_adaptive_qos_UL_shaper_adjust_set(osn_adaptive_qos_t *self, bool shaper_adjust)
{
    (void)self;
    (void)shaper_adjust;

    return true;
}

bool osn_adaptive_qos_UL_shaper_params_set(osn_adaptive_qos_t *self, int min_rate, int base_rate, int max_rate)
{
    (void)self;
    (void)min_rate;
    (void)base_rate;
    (void)max_rate;

    return true;
}

bool osn_adaptive_qos_reflector_add(osn_adaptive_qos_t *self, const osn_ipany_addr_t *ip_addr)
{
    (void)self;
    (void)ip_addr;

    return true;
}

bool osn_adaptive_qos_reflector_list_add(osn_adaptive_qos_t *self, const osn_ipany_addr_t ip_addr_list[], int num)
{
    (void)self;
    (void)ip_addr_list;
    (void)num;

    return true;
}

void osn_adaptive_qos_reflectors_list_clear(osn_adaptive_qos_t *self)
{
    (void)self;
}

bool osn_adaptive_qos_reflectors_randomize_set(osn_adaptive_qos_t *self, bool randomize)
{
    (void)self;
    (void)randomize;
    return true;
}

bool osn_adaptive_qos_reflectors_ping_interval_set(osn_adaptive_qos_t *self, int ping_interval)
{
    (void)self;
    (void)ping_interval;
    return true;
}

bool osn_adaptive_qos_num_pingers_set(osn_adaptive_qos_t *self, int num_pingers)
{
    (void)self;
    (void)num_pingers;
    return true;
}

bool osn_adaptive_qos_active_threshold_set(osn_adaptive_qos_t *self, int threshold)
{
    (void)self;
    (void)threshold;
    return true;
}

bool osn_adaptive_qos_other_config_set(osn_adaptive_qos_t *self, const ds_map_str_t *other_config)
{
    (void)self;
    (void)other_config;
    return true;
}

bool osn_adaptive_qos_apply(osn_adaptive_qos_t *self)
{
    (void)self;

    return true;
}

bool osn_adaptive_qos_del(osn_adaptive_qos_t *self)
{
    FREE(self);

    return true;
}
