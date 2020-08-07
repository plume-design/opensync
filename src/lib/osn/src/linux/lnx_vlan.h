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

#ifndef LNX_VLAN_H_INCLUDED
#define LNX_VLAN_H_INCLUDED

#include <ev.h>

#include "const.h"

typedef struct lnx_vlan lnx_vlan_t;

struct lnx_vlan
{
    char        lv_ifname[C_IFNAME_LEN];        /* Interface name */
    char        lv_pifname[C_IFNAME_LEN];       /* Parent interface name */
    bool        lv_applied;                     /* True if configuration was applied */
    int         lv_vlanid;                      /* VLAN ID */
    ev_async    lv_async;                       /* Async watcher */
};

bool lnx_vlan_init(lnx_vlan_t *self, const char *ifname);
bool lnx_vlan_fini(lnx_vlan_t *self);
bool lnx_vlan_apply(lnx_vlan_t *self);
bool lnx_vlan_parent_ifname_set(lnx_vlan_t *self, const char *parent_ifname);
bool lnx_vlan_vid_set(lnx_vlan_t *self, int vid);

#endif /* LNX_VLAN_H_INCLUDED */
