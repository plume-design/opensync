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
#include "cake_autorate.h"

/**
 * Adaptive QoS implementation using cake-autorate.
 */

struct osn_adaptive_qos
{
    cake_autorate_t cake_autorate;
};

osn_adaptive_qos_t *osn_adaptive_qos_new(const char *DL_ifname, const char *UL_ifname)
{
    osn_adaptive_qos_t *self = CALLOC(1, sizeof(osn_adaptive_qos_t));

    if (!cake_autorate_init(&self->cake_autorate, DL_ifname, UL_ifname))
    {
        LOG(ERR, "osn_adaptive_qos: %s: %s: Error creating cake-autorate object", DL_ifname, UL_ifname);
        FREE(self);
        return NULL;
    }
    return self;
}

bool osn_adaptive_qos_DL_shaper_adjust_set(osn_adaptive_qos_t *self, bool shaper_adjust)
{
    return cake_autorate_DL_shaper_adjust_set(&self->cake_autorate, shaper_adjust);
}

bool osn_adaptive_qos_DL_shaper_params_set(osn_adaptive_qos_t *self, int min_rate, int base_rate, int max_rate)
{
    return cake_autorate_DL_shaper_params_set(&self->cake_autorate, min_rate, base_rate, max_rate);
}

bool osn_adaptive_qos_UL_shaper_adjust_set(osn_adaptive_qos_t *self, bool shaper_adjust)
{
    return cake_autorate_UL_shaper_adjust_set(&self->cake_autorate, shaper_adjust);
}

bool osn_adaptive_qos_UL_shaper_params_set(osn_adaptive_qos_t *self, int min_rate, int base_rate, int max_rate)
{
    return cake_autorate_UL_shaper_params_set(&self->cake_autorate, min_rate, base_rate, max_rate);
}

bool osn_adaptive_qos_apply(osn_adaptive_qos_t *self)
{
    return cake_autorate_apply(&self->cake_autorate);
}

bool osn_adaptive_qos_del(osn_adaptive_qos_t *self)
{
    bool retval = true;

    if (!cake_autorate_fini(&self->cake_autorate))
    {
        LOG(ERR,
            "osn_adaptive_qos: %s: %s: Error destroying cake-autorate object",
            self->cake_autorate.DL_ifname,
            self->cake_autorate.UL_ifname);
        retval = false;
    }
    FREE(self);
    return retval;
}
