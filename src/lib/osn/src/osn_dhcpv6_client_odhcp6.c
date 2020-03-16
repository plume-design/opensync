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
 *  OSN osn_dhcpv6_client implementation using odhcp6 as backend
 * ===========================================================================
 */
#include "log.h"

#include "odhcp6_client.h"

struct osn_dhcpv6_client
{
    odhcp6_client_t                 dc_odhcp6;
    osn_dhcpv6_client_status_fn_t  *dc_status_fn;
    void                           *dc_data;
};

static odhcp6_client_status_fn_t osn_dhcpv6_client_status_odhcp6;

osn_dhcpv6_client_t *osn_dhcpv6_client_new(const char *ifname)
{
    osn_dhcpv6_client_t *self = calloc(1, sizeof(*self));

    if (!odhcp6_client_init(&self->dc_odhcp6, ifname))
    {
        LOG(ERR, "dhcpv6_client: %s: Error initializing odhcp6 client object.", ifname);
        goto error;
    }

    return self;

error:
    if (self != NULL) free(self);
    return NULL;
}

bool osn_dhcpv6_client_del(osn_dhcpv6_client_t *self)
{
    bool retval = true;

    if (!odhcp6_client_fini(&self->dc_odhcp6))
    {
        LOG(ERR, "dhcpv6_client: %s: Error destroying odhcp6 client object.",
                 self->dc_odhcp6.oc_ifname);
        retval = false;
    }

    free(self);

    return retval;
}

bool osn_dhcpv6_client_set(
        osn_dhcpv6_client_t *self,
        bool request_address,
        bool request_prefixes,
        bool rapid_commit,
        bool renew)
{
    return odhcp6_client_set(&self->dc_odhcp6, request_address, request_prefixes, rapid_commit, renew);
}

bool osn_dhcpv6_client_option_request(osn_dhcpv6_client_t *self, int tag)
{
    return odhcp6_client_option_request(&self->dc_odhcp6, tag);
}

bool osn_dhcpv6_client_option_send(osn_dhcpv6_client_t *self, int tag, const char *value)
{
    return odhcp6_client_option_send(&self->dc_odhcp6, tag, value);
}

bool osn_dhcpv6_client_apply(osn_dhcpv6_client_t *self)
{
    return odhcp6_client_apply(&self->dc_odhcp6);
}

void osn_dhcpv6_client_status_odhcp6(odhcp6_client_t *odhcp6, struct osn_dhcpv6_client_status *status)
{
    osn_dhcpv6_client_t *self = CONTAINER_OF(odhcp6, osn_dhcpv6_client_t, dc_odhcp6);

    if (self->dc_status_fn != NULL) self->dc_status_fn(self, status);
}

void osn_dhcpv6_client_status_notify(
        osn_dhcpv6_client_t *self,
        osn_dhcpv6_client_status_fn_t *fn)
{
    self->dc_status_fn = fn;

    odhcp6_client_status_notify(&self->dc_odhcp6, fn == NULL ? NULL : osn_dhcpv6_client_status_odhcp6);
}

void osn_dhcpv6_client_data_set(osn_dhcpv6_client_t *self, void *data)
{
    self->dc_data = data;
}

void* osn_dhcpv6_client_data_get(osn_dhcpv6_client_t *self)
{
    return self->dc_data;
}
