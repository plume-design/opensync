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

#ifndef INET_TAP_H_INCLUDED
#define INET_TAP_H_INCLUDED

#include "inet.h"
#include "inet_base.h"
#include "inet_eth.h"

#include "osn_netif.h"
#include "osn_tap.h"

typedef struct __inet_tap inet_tap_t;

/* Subclass inet_eth */
struct __inet_tap
{
    /* Subclass inet_base; expose the inet_t class so we can have convenient access to in_tap_ifname */
    union
    {
        inet_t inet;
        inet_base_t base;
        inet_eth_t eth;
    };

    char in_tap_ifname[C_IFNAME_LEN]; /* interface name */
    osn_tap_t *in_tap_osn;            /* osn tap interface object */
};

inet_t *inet_tap_new(const char *ifname);
bool inet_tap_init(inet_tap_t *self, const char *ifname);
bool inet_tap_fini(inet_tap_t *self);

#endif /* INET_TAP_H_INCLUDED */
