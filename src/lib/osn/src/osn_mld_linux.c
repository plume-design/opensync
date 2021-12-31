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
 *  Linux OSN MLD backend
 * ===========================================================================
 */

#include "log.h"
#include "lnx_mcast.h"
#include "memutil.h"
#include "osn_mld.h"

struct osn_mld
{
    lnx_mld_t          *ig_lnx;
};

osn_mld_t *osn_mld_new()
{
    osn_mld_t *self = CALLOC(1, sizeof(*self));

    self->ig_lnx = lnx_mld_new();
    if (self->ig_lnx == NULL)
    {
        LOG(ERR, "mld: Unable to initialize Linux MLD object.");
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

bool osn_mld_del(osn_mld_t *self)
{
    bool retval = true;

    if (!lnx_mld_del(self->ig_lnx))
    {
        LOG(ERR, "mld: Error destroying Linux MLD object.");
        retval = false;
    }

    FREE(self);

    return retval;
}

bool osn_mld_apply(osn_mld_t *self)
{
    return lnx_mld_apply(self->ig_lnx);
}

bool osn_mld_snooping_set(
        osn_mld_t *self,
        struct osn_mld_snooping_config *config)
{
    return lnx_mld_snooping_set(self->ig_lnx, config);
}

bool osn_mld_proxy_set(
        osn_mld_t *self,
        struct osn_mld_proxy_config *config)
{
    return lnx_mld_proxy_set(self->ig_lnx, config);
}

bool osn_mld_querier_set(
        osn_mld_t *self,
        struct osn_mld_querier_config *config)
{
    return lnx_mld_querier_set(self->ig_lnx, config);
}

bool osn_mld_other_config_set(
        osn_mld_t *self,
        const struct osn_mcast_other_config *other_config)
{
    return lnx_mld_other_config_set(self->ig_lnx, other_config);
}

bool osn_mld_update_iface_status(
        osn_mld_t *self,
        char *ifname,
        bool enable)
{
    return lnx_mld_update_iface_status(self->ig_lnx, ifname, enable);
}
