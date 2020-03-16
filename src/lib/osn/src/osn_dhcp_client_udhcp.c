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

#include "udhcp_client.h"

struct osn_dhcp_client
{
    udhcp_client_t                      dc_udhcp;
    void                               *dc_data;
    osn_dhcp_client_opt_notify_fn_t    *dc_opt_notify_fn;
    osn_dhcp_client_error_fn_t         *dc_error_notify_fn;
};

static udhcp_client_opt_notify_fn_t osn_dhcp_client_opt_udhcp;
static udhcp_client_error_fn_t osn_dhcp_client_error_udhcp;

osn_dhcp_client_t *osn_dhcp_client_new(const char *ifname)
{
    osn_dhcp_client_t *self = calloc(1, sizeof(osn_dhcp_client_t));

    if (!udhcp_client_init(&self->dc_udhcp, ifname))
    {
        LOG(ERR, "dhcp_client: %s: Error creating udhcp client object.", ifname);
        free(self);
        return NULL;
    }

    return self;
}

bool osn_dhcp_client_del(osn_dhcp_client_t *self)
{
    bool retval = true;

    if (!udhcp_client_fini(&self->dc_udhcp))
    {
        LOG(ERR, "dhcp_client: %s: Error destroying udhcp client object.",
                 self->dc_udhcp.uc_ifname);
        retval = false;
    }

    free(self);

    return retval;
}

bool osn_dhcp_client_start(osn_dhcp_client_t *self)
{
    return udhcp_client_start(&self->dc_udhcp);
}

bool osn_dhcp_client_stop(osn_dhcp_client_t *self)
{
    return udhcp_client_stop(&self->dc_udhcp);
}

bool osn_dhcp_client_opt_request(osn_dhcp_client_t *self, enum osn_dhcp_option opt, bool request)
{
    return udhcp_client_opt_request(&self->dc_udhcp, opt, request);
}

bool osn_dhcp_client_opt_set(osn_dhcp_client_t *self, enum osn_dhcp_option opt, const char *value)
{
    return udhcp_client_opt_set(&self->dc_udhcp, opt, value);
}

bool osn_dhcp_client_opt_get(osn_dhcp_client_t *self, enum osn_dhcp_option opt, bool *request, const char **value)
{
    return udhcp_client_opt_get(&self->dc_udhcp, opt, request, value);
}

bool osn_dhcp_client_vendorclass_set(osn_dhcp_client_t *self, const char *vendorspec)
{
    (void)self;
    (void)vendorspec;
    /* Not supported */
    return false;
}

bool osn_dhcp_client_state_get(osn_dhcp_client_t *self, bool *enabled)
{
    return udhcp_client_state_get(&self->dc_udhcp, enabled);
}

bool osn_dhcp_client_opt_udhcp(
        udhcp_client_t *udhcp,
        enum osn_notify hint,
        const char *key,
        const char *value)
{
    osn_dhcp_client_t *self = CONTAINER_OF(udhcp, osn_dhcp_client_t, dc_udhcp);

    if (self->dc_opt_notify_fn != NULL)
        return self->dc_opt_notify_fn(self, hint, key, value);

    return true;
}

/* Set the option reporting callback */
bool osn_dhcp_client_opt_notify_set(osn_dhcp_client_t *self, osn_dhcp_client_opt_notify_fn_t *fn)
{
    self->dc_opt_notify_fn = fn;
    udhcp_client_opt_notify(&self->dc_udhcp, fn == NULL ? NULL : osn_dhcp_client_opt_udhcp);
    return true;
}

void osn_dhcp_client_error_udhcp(udhcp_client_t *udhcp)
{
    osn_dhcp_client_t *self = CONTAINER_OF(udhcp, osn_dhcp_client_t, dc_udhcp);
    if (self->dc_error_notify_fn != NULL) self->dc_error_notify_fn(self);
}

/* Error callback, called whenever an error occurs on the dhcp client (sudden termination or otherwise) */
bool osn_dhcp_client_error_fn_set(osn_dhcp_client_t *self, osn_dhcp_client_error_fn_t *fn)
{
    self->dc_error_notify_fn = fn;
    udhcp_client_error_notify(&self->dc_udhcp, fn == NULL ? NULL : osn_dhcp_client_error_udhcp);

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
