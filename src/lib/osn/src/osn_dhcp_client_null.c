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

struct osn_dhcp_client
{
    void                               *dc_data;
};

osn_dhcp_client_t *osn_dhcp_client_new(const char *ifname)
{
    (void)ifname;
    osn_dhcp_client_t *self = calloc(1, sizeof(osn_dhcp_client_t));
    return self;
}

bool osn_dhcp_client_del(osn_dhcp_client_t *self)
{
    free(self);
    return true;
}

bool osn_dhcp_client_start(osn_dhcp_client_t *self)
{
    (void)self;
    return true;
}

bool osn_dhcp_client_stop(osn_dhcp_client_t *self)
{
    (void)self;
    return true;
}

bool osn_dhcp_client_opt_request(osn_dhcp_client_t *self, enum osn_dhcp_option opt, bool request)
{
    (void)self;
    (void)opt;
    (void)request;

    return true;
}

bool osn_dhcp_client_opt_set(osn_dhcp_client_t *self, enum osn_dhcp_option opt, const char *value)
{
    (void)self;
    (void)opt;
    (void)value;

    return true;
}

bool osn_dhcp_client_opt_get(osn_dhcp_client_t *self, enum osn_dhcp_option opt, bool *request, const char **value)
{
    (void)self;
    (void)opt;
    (void)request;
    (void)value;

    return true;
}

bool osn_dhcp_client_vendorclass_set(osn_dhcp_client_t *self, const char *vendorspec)
{
    (void)self;
    (void)vendorspec;

    return true;
}

bool osn_dhcp_client_state_get(osn_dhcp_client_t *self, bool *enabled)
{
    (void)self;
    (void)enabled;

    return true;
}

/* Set the option reporting callback */
bool osn_dhcp_client_opt_notify_set(osn_dhcp_client_t *self, osn_dhcp_client_opt_notify_fn_t *fn)
{
    (void)self;
    (void)fn;

    return true;
}

/* Error callback, called whenever an error occurs on the dhcp client (sudden termination or otherwise) */
bool osn_dhcp_client_error_fn_set(osn_dhcp_client_t *self, osn_dhcp_client_error_fn_t *fn)
{
    (void)self;
    (void)fn;

    return true;
}

void osn_dhcp_client_data_set(osn_dhcp_client_t *self, void *data)
{
    self->dc_data = data;
}

void* osn_dhcp_client_data_get(osn_dhcp_client_t *self)
{
    return self->dc_data;
}
