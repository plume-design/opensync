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

#ifndef LNX_TUNNEL_IFACE_H_INCLUDED
#define LNX_TUNNEL_IFACE_H_INCLUDED

#include <stdbool.h>

#include "osn_tunnel_iface.h"
#include "osn_types.h"
#include "const.h"

typedef struct lnx_tunnel_iface lnx_tunnel_iface_t;

struct lnx_tunnel_iface
{
    char                        ti_ifname[C_IFNAME_LEN];
    enum osn_tunnel_iface_type  ti_type;
    osn_ipany_addr_t            ti_local_endpoint;
    osn_ipany_addr_t            ti_remote_endpoint;
    int                         ti_key;
    char                        ti_dev_ifname;
    bool                        ti_enable;
    bool                        ti_applied;
};

bool lnx_tunnel_iface_init(lnx_tunnel_iface_t *self, const char *ifname);
bool lnx_tunnel_iface_type_set(lnx_tunnel_iface_t *self, enum osn_tunnel_iface_type iftype);
bool lnx_tunnel_iface_endpoints_set(
        lnx_tunnel_iface_t *self,
        osn_ipany_addr_t local_endpoint,
        osn_ipany_addr_t remote_endpoint);
bool lnx_tunnel_iface_key_set(lnx_tunnel_iface_t *self, int key);
bool lnx_tunnel_iface_enable_set(lnx_tunnel_iface_t *self, bool enable);
bool lnx_tunnel_iface_apply(lnx_tunnel_iface_t *self);
bool lnx_tunnel_iface_fini(lnx_tunnel_iface_t *self);

#endif /* LNX_TUNNEL_IFACE_H_INCLUDED */
