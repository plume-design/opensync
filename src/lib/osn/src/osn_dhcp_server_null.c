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

#include "osn_dhcp.h"

struct osn_dhcp_server
{
    void  *ds_data;
};

osn_dhcp_server_t *osn_dhcp_server_new(const char *ifname)
{
    (void)ifname;

    osn_dhcp_server_t *self = calloc(1, sizeof(osn_dhcp_server_t));

    return self;
}

bool osn_dhcp_server_del(osn_dhcp_server_t *self)
{
    free(self);

    return true;
}

bool osn_dhcp_server_apply(osn_dhcp_server_t *self)
{
    (void)self;

    return true;
}

bool osn_dhcp_server_option_set(
        osn_dhcp_server_t *self,
        enum osn_dhcp_option opt,
        const char *value)
{
    (void)self;
    (void)opt;
    (void)value;

    return true;
}

bool osn_dhcp_server_cfg_set(osn_dhcp_server_t *self, struct osn_dhcp_server_cfg *cfg)
{
    (void)self;
    (void)cfg;

    return true;
}

bool osn_dhcp_server_range_add(osn_dhcp_server_t *self, osn_ip_addr_t start, osn_ip_addr_t stop)
{
    (void)self;
    (void)start;
    (void)stop;

    return true;
}

bool osn_dhcp_server_range_del(osn_dhcp_server_t *self, osn_ip_addr_t start, osn_ip_addr_t stop)
{
    (void)self;
    (void)start;
    (void)stop;

    return true;
}

bool osn_dhcp_server_reservation_add(
        osn_dhcp_server_t *self,
        osn_mac_addr_t macaddr,
        osn_ip_addr_t ipaddr,
        const char *hostname)
{
    (void)self;
    (void)macaddr;
    (void)ipaddr;
    (void)hostname;

    return true;
}

bool osn_dhcp_server_reservation_del(osn_dhcp_server_t *self, osn_mac_addr_t macaddr)
{
    (void)self;
    (void)macaddr;

    return true;
}

void osn_dhcp_server_status_notify(osn_dhcp_server_t *self, osn_dhcp_server_status_fn_t *fn)
{
    (void)self;
    (void)fn;
}

void osn_dhcp_server_error_notify(osn_dhcp_server_t *self, osn_dhcp_server_error_fn_t *fn)
{
    (void)self;
    (void)fn;
}

void osn_dhcp_server_data_set(osn_dhcp_server_t *self, void *data)
{
    self->ds_data = data;
}

void *osn_dhcp_server_data_get(osn_dhcp_server_t *self)
{
    return self->ds_data;
}

