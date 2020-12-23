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

#ifndef INET_ETH_H_INCLUDED
#define INET_ETH_H_INCLUDED

#include "osn_netif.h"
#include "osn_inet.h"

#include "inet.h"
#include "inet_base.h"

typedef struct __inet_eth inet_eth_t;

struct __inet_eth
{
    /* Subclass inet_base; expose the inet_t class so we can have convenient access to in_ifname */
    union
    {
        inet_t                  inet;
        inet_base_t             base;
    };

    osn_ip_t                   *in_ip;              /* IPv4 Configuration */
    osn_netif_t                *in_netif;           /* L2 Configuration */
    ev_async                    in_netif_async;     /* Some netif events needs to be dealt with from the main loop */
    bool                        in_noflood_set;     /* Whether the noflood option is set */
    bool                        in_noflood;         /* OVS no flood option */
};

extern inet_t *inet_eth_new(const char *ifname);
extern bool inet_eth_init(inet_eth_t *self, const char *ifname);
extern bool inet_eth_fini(inet_eth_t *self);

extern bool inet_eth_mtu_set(inet_t *super, int mtu);
extern bool inet_eth_service_commit(inet_base_t *super, enum inet_base_services srv, bool enable);

#endif /* INET_ETH_H_INCLUDED */
