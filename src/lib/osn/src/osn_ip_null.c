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
#include "osn_inet.h"

struct osn_ip
{
    void                   *ip_data;        /* User data */
};

osn_ip_t *osn_ip_new(const char *ifname)
{
    (void)ifname;

    osn_ip_t *self = calloc(1, sizeof(*self));

    return self;
}

bool osn_ip_del(osn_ip_t *self)
{
    free(self);

    return true;
}

bool osn_ip_addr_add(osn_ip_t *self, const osn_ip_addr_t *addr)
{
    (void)self;
    (void)addr;

    return true;
}

bool osn_ip_addr_del(osn_ip_t *self, const osn_ip_addr_t *addr)
{
    (void)self;
    (void)addr;

    return true;
}

bool osn_ip_dns_add(osn_ip_t *self, const osn_ip_addr_t *addr)
{
    (void)self;
    (void)addr;

    return true;
}

bool osn_ip_dns_del(osn_ip_t *self, const osn_ip_addr_t *addr)
{
    (void)self;
    (void)addr;

    return true;
}

bool osn_ip_route_gw_add(osn_ip_t *self, const osn_ip_addr_t *src, const osn_ip_addr_t *gw)
{
    (void)self;
    (void)src;
    (void)gw;

    return true;
}

bool osn_ip_route_gw_del(osn_ip_t *self, const osn_ip_addr_t *src, const osn_ip_addr_t *gw)
{
    (void)self;
    (void)src;
    (void)gw;

    return true;
}

void osn_ip_status_notify(osn_ip_t *self, osn_ip_status_fn_t *fn)
{
    (void)self;
    (void)fn;
}

void osn_ip_data_set(osn_ip_t *self, void *data)
{
    (void)self;
    (void)data;

    return;
}

void *osn_ip_data_get(osn_ip_t *self)
{
    return self->ip_data;
}

bool osn_ip_apply(osn_ip_t *self)
{
    (void)self;

    return true;
}

