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

#include "dnsmasq6_server.h"

struct osn_dhcpv6_server
{
    dnsmasq6_server_t               d6s_dnsmasq;
    void                           *d6s_data;
    osn_dhcpv6_server_status_fn_t  *d6s_status_fn;
};

static dnsmasq6_server_status_fn_t osn_dhcpv6_server_status_dnsmasq6;

osn_dhcpv6_server_t* osn_dhcpv6_server_new(const char *iface)
{
    osn_dhcpv6_server_t *self = calloc(1, sizeof(*self));

    if (!dnsmasq6_server_init(&self->d6s_dnsmasq, iface))
    {
        LOG(ERR, "dhcpv6_server: %s: Error initializing dnsmasq6 server object.", iface);
        goto error;
    }

    return self;

error:
    if (self != NULL) free(self);
    return NULL;
}

bool osn_dhcpv6_server_del(osn_dhcpv6_server_t *self)
{
    bool retval = true;

    if (!dnsmasq6_server_fini(&self->d6s_dnsmasq))
    {
        LOG(ERR, "dhcpv6_server: %s: Error destroying dnsmasq6 server object.", self->d6s_dnsmasq.d6s_ifname);
        retval = false;
    }

    free(self);

    return retval;
}

bool osn_dhcpv6_server_apply(osn_dhcpv6_server_t *self)
{
    return dnsmasq6_server_apply(&self->d6s_dnsmasq);
}

bool osn_dhcpv6_server_prefix_add(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_prefix *prefix)
{
    return dnsmasq6_server_prefix_add(&self->d6s_dnsmasq, prefix);
}

bool osn_dhcpv6_server_prefix_del(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_prefix *prefix)
{
    return dnsmasq6_server_prefix_del(&self->d6s_dnsmasq, prefix);
}

bool osn_dhcpv6_server_option_send(
        osn_dhcpv6_server_t *self,
        int tag,
        const char *value)
{
    return dnsmasq6_server_option_send(&self->d6s_dnsmasq, tag, value);
}

bool osn_dhcpv6_server_lease_add(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_lease *lease)
{
    return dnsmasq6_server_lease_add(&self->d6s_dnsmasq, lease);
}

bool osn_dhcpv6_server_lease_del(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_lease *lease)
{
    return dnsmasq6_server_lease_del(&self->d6s_dnsmasq, lease);
}

void osn_dhcpv6_server_status_dnsmasq6(
        dnsmasq6_server_t *dnsmasq,
        struct osn_dhcpv6_server_status *status)
{
    osn_dhcpv6_server_t *self = CONTAINER_OF(dnsmasq, osn_dhcpv6_server_t, d6s_dnsmasq);

    if (self->d6s_status_fn != NULL) self->d6s_status_fn(self, status);
}

bool osn_dhcpv6_server_status_notify(
        osn_dhcpv6_server_t *self,
        osn_dhcpv6_server_status_fn_t *fn)
{
    self->d6s_status_fn = fn;

    return dnsmasq6_server_status_notify(
            &self->d6s_dnsmasq, fn == NULL ? NULL : osn_dhcpv6_server_status_dnsmasq6);
}

void osn_dhcpv6_server_data_set(osn_dhcpv6_server_t *self, void *data)
{
    self->d6s_data = data;
}

void *osn_dhcpv6_server_data_get(osn_dhcpv6_server_t *self)
{
    return self->d6s_data;
}
