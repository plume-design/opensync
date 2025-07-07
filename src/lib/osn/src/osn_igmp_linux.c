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

/*
 * ===========================================================================
 *  Linux OSN IGMP backend
 * ===========================================================================
 */

#include "log.h"
#include "lnx_mcast.h"
#include "memutil.h"
#include "osn_igmp.h"

struct osn_igmp
{
    lnx_igmp_t         *ig_lnx;
};

osn_igmp_t *osn_igmp_new(const char *ifname)
{
    (void)ifname;

    osn_igmp_t *self = CALLOC(1, sizeof(*self));

    self->ig_lnx = lnx_igmp_new();
    if (self->ig_lnx == NULL)
    {
        LOG(ERR, "igmp: Unable to initialize Linux IGMP object.");
        goto error;
    }

    return self;

error:
    if (self != NULL)
    {
        FREE(self);
    }

    return NULL;
}

bool osn_igmp_del(osn_igmp_t *self)
{
    bool retval = true;

    if (!lnx_igmp_del(self->ig_lnx))
    {
        LOG(ERR, "igmp: Error destroying Linux IGMP object.");
        retval = false;
    }

    FREE(self);

    return retval;
}

bool osn_igmp_apply(osn_igmp_t *self)
{
    return lnx_igmp_apply(self->ig_lnx);
}

bool osn_igmp_snooping_set(
        osn_igmp_t *self,
        struct osn_igmp_snooping_config *config)
{
    return lnx_igmp_snooping_set(self->ig_lnx, config);
}

bool osn_igmp_proxy_set(
        osn_igmp_t *self,
        struct osn_igmp_proxy_config *config)
{
    return lnx_igmp_proxy_set(self->ig_lnx, config);
}

bool osn_igmp_querier_set(
        osn_igmp_t *self,
        struct osn_igmp_querier_config *config)
{
    return lnx_igmp_querier_set(self->ig_lnx, config);
}

bool osn_igmp_other_config_set(
        osn_igmp_t *self,
        const struct osn_mcast_other_config *other_config)
{
    return lnx_igmp_other_config_set(self->ig_lnx, other_config);
}

bool osn_igmp_update_iface_status(
        osn_igmp_t *self,
        char *ifname,
        bool enable)
{
    return lnx_igmp_update_iface_status(self->ig_lnx, ifname, enable);
}
