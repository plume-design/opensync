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
 *  Linux OSN IP backend
 * ===========================================================================
 */

#include "log.h"
#include "lnx_ip.h"

struct osn_ip
{
    lnx_ip_t                ip_lnx;         /* Linux IP object */
    void                   *ip_data;        /* User data */
    osn_ip_status_fn_t     *ip_status_fn;   /* Status callback */
};

static void osn_ip_status_lnx(lnx_ip_t *lnx, struct osn_ip_status *status);

osn_ip_t *osn_ip_new(const char *ifname)
{
    osn_ip_t *self = calloc(1, sizeof(*self));

    if (!lnx_ip_init(&self->ip_lnx, ifname))
    {
        LOG(ERR, "ip: %s: Unable to initialize Linux IP object.", ifname);
        goto error;
    }

    return self;

error:
    if (self != NULL)
    {
        free(self);
    }

    return NULL;
}

bool osn_ip_del(osn_ip_t *self)
{
    bool retval = true;

    if (!lnx_ip_fini(&self->ip_lnx))
    {
        LOG(ERR, "ip: %s: Error destroying Linux IP object.", self->ip_lnx.ip_ifname);
        retval = false;
    }

    free(self);

    return retval;
}

bool osn_ip_addr_add(osn_ip_t *self, const osn_ip_addr_t *addr)
{
    return lnx_ip_addr_add(&self->ip_lnx, addr);
}

bool osn_ip_addr_del(osn_ip_t *self, const osn_ip_addr_t *addr)
{
    return lnx_ip_addr_del(&self->ip_lnx, addr);
}

bool osn_ip_dns_add(osn_ip_t *self, const osn_ip_addr_t *addr)
{
    return lnx_ip_dns_add(&self->ip_lnx, addr);
}

bool osn_ip_dns_del(osn_ip_t *self, const osn_ip_addr_t *addr)
{
    return lnx_ip_dns_del(&self->ip_lnx, addr);
}

bool osn_ip_route_gw_add(osn_ip_t *self, const osn_ip_addr_t *src, const osn_ip_addr_t *gw)
{
    return lnx_ip_route_gw_add(&self->ip_lnx, src, gw);
}

bool osn_ip_route_gw_del(osn_ip_t *self, const osn_ip_addr_t *src, const osn_ip_addr_t *gw)
{
    return lnx_ip_route_gw_del(&self->ip_lnx, src, gw);
}

void osn_ip_status_lnx(lnx_ip_t *lnx, struct osn_ip_status *status)
{
    osn_ip_t *self = CONTAINER_OF(lnx, osn_ip_t, ip_lnx);

    if (self->ip_status_fn != NULL) self->ip_status_fn(self, status);
}

void osn_ip_status_notify(osn_ip_t *self, osn_ip_status_fn_t *fn)
{
    self->ip_status_fn = fn;

    if (fn == NULL)
    {
        lnx_ip_status_notify(&self->ip_lnx, NULL);
    }
    else
    {
        lnx_ip_status_notify(&self->ip_lnx, osn_ip_status_lnx);
    }
}

void osn_ip_data_set(osn_ip_t *self, void *data)
{
    self->ip_data = data;
}

void *osn_ip_data_get(osn_ip_t *self)
{
    return self->ip_data;
}

bool osn_ip_apply(osn_ip_t *self)
{
    return lnx_ip_apply(&self->ip_lnx);
}

