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

struct osn_ip6_radv
{
    void    *ra_data;
};

osn_ip6_radv_t *osn_ip6_radv_new(const char *ifname)
{
    (void)ifname;

    osn_ip6_radv_t *self = calloc(1, sizeof(*self));

    return self;
}

bool osn_ip6_radv_del(osn_ip6_radv_t *self)
{
    free(self);

    return true;
}

bool osn_ip6_radv_set(osn_ip6_radv_t *self, const struct osn_ip6_radv_options *opts)
{
    (void)self;
    (void)opts;

    return true;
}

bool osn_ip6_radv_add_prefix(
        osn_ip6_radv_t *self,
        const osn_ip6_addr_t *prefix,
        bool autonomous,
        bool onlink)
{
    (void)self;
    (void)prefix;
    (void)autonomous;
    (void)onlink;

    return true;
}

bool osn_ip6_radv_add_rdnss(osn_ip6_radv_t *self, const osn_ip6_addr_t *dns)
{
    (void)self;
    (void)dns;

    return true;
}

bool osn_ip6_radv_add_dnssl(osn_ip6_radv_t *self, char *sl)
{
    (void)self;
    (void)sl;

    return true;
}

bool osn_ip6_radv_apply(osn_ip6_radv_t *self)
{
    (void)self;

    return true;
}
