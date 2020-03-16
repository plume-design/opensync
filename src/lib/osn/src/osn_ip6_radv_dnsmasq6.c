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
 * Router Advertisement implementation using dnsmasq
 * ===========================================================================
 */
#include "log.h"

#include "dnsmasq6_server.h"

struct osn_ip6_radv
{
    dnsmasq6_radv_t     ra_dnsmasq;
    void               *ra_data;
};

osn_ip6_radv_t *osn_ip6_radv_new(const char *ifname)
{
    osn_ip6_radv_t *self = calloc(1, sizeof(*self));

    if (!dnsmasq6_radv_init(&self->ra_dnsmasq, ifname))
    {
        LOG(ERR, "ip6_radv: %s: Error initializing dnsmasq Router Advertisement object.", ifname);
        goto error;
    }

    return self;

error:
    if (self != NULL) free(self);
    return NULL;
}

bool osn_ip6_radv_del(osn_ip6_radv_t *self)
{
    bool retval = true;

    if (!dnsmasq6_radv_fini(&self->ra_dnsmasq))
    {
        LOG(ERR, "ip6_radv: %s: Error destroying dnsmasq Router Advertisement object.", self->ra_dnsmasq.ra_ifname);
        retval = false;
    }

    free(self);

    return retval;
}

bool osn_ip6_radv_set(osn_ip6_radv_t *self, const struct osn_ip6_radv_options *opts)
{
    return dnsmasq6_radv_set(&self->ra_dnsmasq, opts);
}

bool osn_ip6_radv_add_prefix(
        osn_ip6_radv_t *self,
        const osn_ip6_addr_t *prefix,
        bool autonomous,
        bool onlink)
{
    return dnsmasq6_radv_add_prefix(&self->ra_dnsmasq, prefix, autonomous, onlink);
}

#if 0
bool osn_ip6_radv_del_prefix(osn_ip6_radv_t *self, const osn_ip6_addr_t *prefix)
{
    return dnsmasq6_radv_del_prefix(&self->ra_dnsmasq, prefix);
}
#endif

bool osn_ip6_radv_add_rdnss(osn_ip6_radv_t *self, const osn_ip6_addr_t *dns)
{
    return dnsmasq6_radv_add_rdnss(&self->ra_dnsmasq, dns);
}

#if 0
bool osn_ip6_radv_del_rdnss(osn_ip6_radv_t *self, const osn_ip6_addr_t *dns)
{
    return dnsmasq6_radv_del_rdnss(&self->ra_dnsmasq, dns);
}
#endif

bool osn_ip6_radv_add_dnssl(osn_ip6_radv_t *self, char *sl)
{
    return dnsmasq6_radv_add_dnssl(&self->ra_dnsmasq, sl);
}

#if 0
bool osn_ip6_radv_del_dnssl(osn_ip6_radv_t *self, char *sl)
{
    return dnsmasq6_radv_del_dnssl(&self->ra_dnsmasq, sl);
}
#endif

bool osn_ip6_radv_apply(osn_ip6_radv_t *self)
{
    return dnsmasq6_radv_apply(&self->ra_dnsmasq);
}
