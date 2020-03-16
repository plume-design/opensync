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
#include "lnx_ip6.h"

struct osn_ip6
{
    lnx_ip6_t               ip6_lnx;            /* Linux IPv6 Object */
    void                   *ip6_data;           /* User data */
    osn_ip6_status_fn_t    *ip6_status_fn;      /* Status callback */
};

static void osn_ip6_status_lnx(lnx_ip6_t *lnx, struct osn_ip6_status *status);

osn_ip6_t *osn_ip6_new(const char *ifname)
{
    osn_ip6_t *self = calloc(1, sizeof(*self));

    if (!lnx_ip6_init(&self->ip6_lnx, ifname))
    {
        LOG(ERR, "ip6: %s: Error initializing Linux IPv6 object.",
                ifname);
        goto error;
    }

    return self;

error:
    if (self != NULL) free(self);
    return NULL;
}

bool osn_ip6_del(osn_ip6_t *self)
{
    bool retval = true;

    if (!lnx_ip6_fini(&self->ip6_lnx))
    {
        LOG(ERR, "ip6: %s: Error destroying Linux IPv6 object.",
                self->ip6_lnx.ip6_ifname);
        retval = false;
    }

    free(self);

    return retval;
}

bool osn_ip6_apply(osn_ip6_t *self)
{
    return lnx_ip6_apply(&self->ip6_lnx);
}

bool osn_ip6_addr_add(osn_ip6_t *self, const osn_ip6_addr_t *addr)
{
    return lnx_ip6_addr_add(&self->ip6_lnx, addr);
}

bool osn_ip6_addr_del(osn_ip6_t *self, const osn_ip6_addr_t *addr)
{
    return lnx_ip6_addr_del(&self->ip6_lnx, addr);
}

bool osn_ip6_dns_add(osn_ip6_t *self, const osn_ip6_addr_t *dns)
{
    return lnx_ip6_dns_add(&self->ip6_lnx, dns);
}

bool osn_ip6_dns_del(osn_ip6_t *self, const osn_ip6_addr_t *dns)
{
    return lnx_ip6_dns_del(&self->ip6_lnx, dns);
}

void osn_ip6_data_set(osn_ip6_t *self, void *data)
{
    self->ip6_data = data;
}

void *osn_ip6_data_get(osn_ip6_t *self)
{
    return self->ip6_data;
}

void osn_ip6_status_lnx(lnx_ip6_t *lnx, struct osn_ip6_status *status)
{
    osn_ip6_t *self = CONTAINER_OF(lnx, osn_ip6_t, ip6_lnx);

    if (self->ip6_status_fn != NULL) self->ip6_status_fn(self, status);
}

void osn_ip6_status_notify(osn_ip6_t *self, osn_ip6_status_fn_t *fn)
{
    self->ip6_status_fn = fn;

    if (fn == NULL)
    {
        lnx_ip6_status_notify(&self->ip6_lnx, NULL);
    }
    else
    {
        lnx_ip6_status_notify(&self->ip6_lnx, osn_ip6_status_lnx);
    }
}
