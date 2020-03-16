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

#include "log.h"

#include "osn_netif.h"
#include "lnx_netif.h"

struct osn_netif
{
    lnx_netif_t             ni_lnx;
    void                   *ni_data;
    osn_netif_status_fn_t  *ni_status_fn;
};

static void osn_netif_status_lnx(lnx_netif_t *lnx, struct osn_netif_status *status);

osn_netif_t* osn_netif_new(const char *ifname)
{
    osn_netif_t *self = calloc(1, sizeof(*self));

    if (!lnx_netif_init(&self->ni_lnx, ifname))
    {
        LOG(ERR, "netif: Error initializing Linux netif object.");
        free(self);
        return NULL;
    }

    return self;
}

bool osn_netif_del(osn_netif_t *self)
{
    bool retval = true;

    if (!lnx_netif_fini(&self->ni_lnx))
    {
        LOG(ERR, "netif: Error destroying Linux netif object.");
        retval = false;
    }

    free(self);
    return retval;
}

bool osn_netif_state_set(osn_netif_t *self, bool up)
{
    return lnx_netif_state_set(&self->ni_lnx, up);
}

bool osn_netif_mtu_set(osn_netif_t *self, int mtu)
{
    return lnx_netif_mtu_set(&self->ni_lnx, mtu);
}

bool osn_netif_hwaddr_set(osn_netif_t *self, osn_mac_addr_t hwaddr)
{
    return lnx_netif_hwaddr_set(&self->ni_lnx, hwaddr);
}

bool osn_netif_apply(osn_netif_t *self)
{
    return lnx_netif_apply(&self->ni_lnx);
}

void osn_netif_status_lnx(lnx_netif_t *lnx, struct osn_netif_status *status)
{
    osn_netif_t *self = CONTAINER_OF(lnx, osn_netif_t, ni_lnx);

    if (self->ni_status_fn != NULL) self->ni_status_fn(self, status);
}

void osn_netif_status_notify(osn_netif_t *self, osn_netif_status_fn_t *fn)
{
    self->ni_status_fn = fn;

    lnx_netif_status_notify(&self->ni_lnx, fn == NULL ? NULL : osn_netif_status_lnx);
}

/*
 * Set user data
 */
void osn_netif_data_set(osn_netif_t *self, void *data)
{
    self->ni_data = data;
}

/*
 * Get user data
 */
void *osn_netif_data_get(osn_netif_t *self)
{
    return self->ni_data;
}

