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
#include "osn_inet.h"
#include "memutil.h"

#include "lnx_route.h"

struct osn_route
{
    lnx_route_t             rt_lnx;
    osn_route_status_fn_t  *rt_status_fn;
    void                   *rt_data;
};

static lnx_route_status_fn_t osn_route_status_lnx;

osn_route_t* osn_route_new(const char *ifname)
{
    osn_route_t *self = CALLOC(1, sizeof(*self));

    memset(self, 0, sizeof(*self));

    if (!lnx_route_init(&self->rt_lnx, ifname))
    {
        LOG(ERR, "route: Error initializing Linux route object.");
        FREE(self);
        return NULL;
    }

    return self;
}


bool osn_route_del(osn_route_t *self)
{
    bool retval = true;

    if (!lnx_route_fini(&self->rt_lnx))
    {
        LOG(ERR, "route: Error destroying Linux route object.");
        retval = false;
    }

    FREE(self);

    return retval;
}

void osn_route_status_lnx(lnx_route_t *lnx, struct osn_route_status *status, bool remove)
{
    osn_route_t *self = CONTAINER_OF(lnx, osn_route_t, rt_lnx);

    if (self->rt_status_fn != NULL) (void)self->rt_status_fn(self, status, remove);
}

bool osn_route_status_notify(osn_route_t *self, osn_route_status_fn_t *fn)
{
    self->rt_status_fn = fn;

    return lnx_route_status_notify(&self->rt_lnx, fn == NULL ? NULL : osn_route_status_lnx);
}

void osn_route_data_set(osn_route_t *self, void *data)
{
    self->rt_data = data;
}

void* osn_route_data_get(osn_route_t *self)
{
    return self->rt_data;
}

bool osn_route_add(osn_route4_cfg_t *self, const osn_route4_t *route)
{
    return lnx_route_add(self, route);
}

bool osn_route_remove(osn_route4_cfg_t *self, const osn_route4_t *route)
{
    return lnx_route_remove(self, route);
}

bool osn_route_find_dev(osn_ip_addr_t addr, char *buf, size_t bufSize)
{
    return lnx_route_find_dev(addr, buf, bufSize);
}

osn_route4_cfg_t *osn_route4_cfg_new(const char *if_name)
{
    return lnx_route4_cfg_new(if_name);
}

bool osn_route4_cfg_del(osn_route4_cfg_t *self)
{
    return lnx_route4_cfg_del(self);
}

const char* osn_route4_cfg_name(const osn_route4_cfg_t *self)
{
    return lnx_route4_cfg_name(self);
}

bool osn_route_apply(osn_route4_cfg_t *self)
{
    return lnx_route_apply(self);
}
