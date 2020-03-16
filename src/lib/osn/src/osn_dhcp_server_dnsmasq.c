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
#include "dnsmasq_server.h"

static void osn_dhcp_server_error_dnsmasq(dnsmasq_server_t *dnsmasq);
static void osn_dhcp_server_status_dnsmasq(dnsmasq_server_t *dnsmasq, struct osn_dhcp_server_status *status);

struct osn_dhcp_server
{
    dnsmasq_server_t                ds_dnsmasq;
    void                           *ds_data;
    osn_dhcp_server_status_fn_t    *ds_status_fn;
    osn_dhcp_server_error_fn_t     *ds_error_fn;
};

osn_dhcp_server_t *osn_dhcp_server_new(const char *ifname)
{
    osn_dhcp_server_t *self = calloc(1, sizeof(osn_dhcp_server_t));

    if (!dnsmasq_server_init(&self->ds_dnsmasq, ifname))
    {
        LOG(ERR, "dhcpv4_server: Error creating dnsmasq server object.");
        free(self);
        return NULL;
    }

    return self;
}

bool osn_dhcp_server_del(osn_dhcp_server_t *self)
{
    bool retval = true;

    if (!dnsmasq_server_fini(&self->ds_dnsmasq))
    {
        LOG(ERR, "dhcpv4_server: Error destroying dnsmasq server object.");
        retval = false;
    }

    free(self);

    return retval;
}

bool osn_dhcp_server_apply(osn_dhcp_server_t *self)
{
    (void)self;

    /* Issue a server restart */
    dnsmasq_server_apply();

    return true;
}

bool osn_dhcp_server_option_set(
        osn_dhcp_server_t *self,
        enum osn_dhcp_option opt,
        const char *value)
{
    return dnsmasq_server_option_set(&self->ds_dnsmasq, opt, value);
}

bool osn_dhcp_server_cfg_set(osn_dhcp_server_t *self, struct osn_dhcp_server_cfg *cfg)
{
    return dnsmasq_server_cfg_set(&self->ds_dnsmasq, cfg);
}

bool osn_dhcp_server_range_add(osn_dhcp_server_t *self, osn_ip_addr_t start, osn_ip_addr_t stop)
{
    return dnsmasq_server_range_add(&self->ds_dnsmasq, start, stop);
}

bool osn_dhcp_server_range_del(osn_dhcp_server_t *self, osn_ip_addr_t start, osn_ip_addr_t stop)
{
    return dnsmasq_server_range_del(&self->ds_dnsmasq, start, stop);
}

bool osn_dhcp_server_reservation_add(
        osn_dhcp_server_t *self,
        osn_mac_addr_t macaddr,
        osn_ip_addr_t ipaddr,
        const char *hostname)
{
    return dnsmasq_server_reservation_add(&self->ds_dnsmasq, macaddr, ipaddr, hostname);
}

bool osn_dhcp_server_reservation_del(osn_dhcp_server_t *self, osn_mac_addr_t macaddr)
{
    return dnsmasq_server_reservation_del(&self->ds_dnsmasq, macaddr);
}

void osn_dhcp_server_status_dnsmasq(dnsmasq_server_t *dnsmasq, struct osn_dhcp_server_status *status)
{
    osn_dhcp_server_t *self = CONTAINER_OF(dnsmasq, osn_dhcp_server_t, ds_dnsmasq);
    if (self->ds_status_fn != NULL) self->ds_status_fn(self, status);
}

void osn_dhcp_server_status_notify(osn_dhcp_server_t *self, osn_dhcp_server_status_fn_t *fn)
{
    self->ds_status_fn = fn;
    dnsmasq_server_status_notify(&self->ds_dnsmasq, fn == NULL ? NULL : osn_dhcp_server_status_dnsmasq);
}

void osn_dhcp_server_error_dnsmasq(dnsmasq_server_t *dnsmasq)
{
    osn_dhcp_server_t *self = CONTAINER_OF(dnsmasq, osn_dhcp_server_t, ds_dnsmasq);
    if (self->ds_error_fn != NULL) self->ds_error_fn(self);
}

void osn_dhcp_server_error_notify(osn_dhcp_server_t *self, osn_dhcp_server_error_fn_t *fn)
{
    self->ds_error_fn = fn;
    dnsmasq_server_error_notify(&self->ds_dnsmasq, fn == NULL ? NULL : osn_dhcp_server_error_dnsmasq);
}

void osn_dhcp_server_data_set(osn_dhcp_server_t *self, void *data)
{
    self->ds_data = data;
}

void *osn_dhcp_server_data_get(osn_dhcp_server_t *self)
{
    return self->ds_data;
}

