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

#include "osn_tunnel_iface.h"

#include "memutil.h"

struct osn_tunnel_iface
{

};

osn_tunnel_iface_t *osn_tunnel_iface_new(const char *ifname)
{
    (void)ifname;

    osn_tunnel_iface_t *self = CALLOC(1, sizeof(*self));

    return self;
}

bool osn_tunnel_iface_type_set(osn_tunnel_iface_t *self, enum osn_tunnel_iface_type iftype)
{
    (void)self;
    (void)iftype;

    return true;
}

bool osn_tunnel_iface_mode_set(osn_tunnel_iface_t *self, enum osn_tunnel_iface_mode mode)
{
    (void)self;
    (void)mode;

    return true;
}

bool osn_tunnel_iface_endpoints_set(
        osn_tunnel_iface_t *self,
        osn_ipany_addr_t local_endpoint,
        osn_ipany_addr_t remote_endpoint)
{
    (void)self;
    (void)local_endpoint;
    (void)remote_endpoint;

    return true;
}

bool osn_tunnel_iface_key_set(osn_tunnel_iface_t *self, int key)
{
    (void)self;
    (void)key;

    return true;
}

bool osn_tunnel_iface_dev_set(osn_tunnel_iface_t *self, const char *dev_if_name)
{
    (void)self;
    (void)dev_if_name;

    return true;
}

bool osn_tunnel_iface_enable_set(osn_tunnel_iface_t *self, bool enable)
{
    (void)self;
    (void)enable;

    return true;
}

bool osn_tunnel_iface_apply(osn_tunnel_iface_t *self)
{
    (void)self;

    return true;
}

bool osn_tunnel_iface_del(osn_tunnel_iface_t *self)
{
    FREE(self);

    return true;
}
