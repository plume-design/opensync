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
#include "osn_dhcpv6.h"

struct osn_dhcpv6_server
{
    void   *d6s_data;
};

osn_dhcpv6_server_t* osn_dhcpv6_server_new(const char *iface)
{
    (void)iface;

    osn_dhcpv6_server_t *self = calloc(1, sizeof(*self));

    return self;
}

bool osn_dhcpv6_server_del(osn_dhcpv6_server_t *self)
{
    free(self);

    return true;
}

bool osn_dhcpv6_server_apply(osn_dhcpv6_server_t *self)
{
    (void)self;

    return true;
}

bool osn_dhcpv6_server_prefix_add(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_prefix *prefix)
{
    (void)self;
    (void)prefix;

    return true;
}

bool osn_dhcpv6_server_prefix_del(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_prefix *prefix)
{
    (void)self;
    (void)prefix;

    return true;
}

bool osn_dhcpv6_server_option_send(
        osn_dhcpv6_server_t *self,
        int tag,
        const char *value)
{
    (void)self;
    (void)tag;
    (void)value;

    return true;
}

bool osn_dhcpv6_server_lease_add(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_lease *lease)
{
    (void)self;
    (void)lease;

    return true;
}

bool osn_dhcpv6_server_lease_del(
        osn_dhcpv6_server_t *self,
        struct osn_dhcpv6_server_lease *lease)
{
    (void)self;
    (void)lease;

    return true;
}

bool osn_dhcpv6_server_status_notify(
        osn_dhcpv6_server_t *self,
        osn_dhcpv6_server_status_fn_t *fn)
{
    (void)self;
    (void)fn;

    return true;
}

void osn_dhcpv6_server_data_set(osn_dhcpv6_server_t *self, void *data)
{
    self->d6s_data = data;
}

void *osn_dhcpv6_server_data_get(osn_dhcpv6_server_t *self)
{
    return self->d6s_data;
}
