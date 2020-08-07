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

#ifndef INET_PPPOE_H_INCLUDED
#define INET_PPPOE_H_INCLUDED

#include "const.h"
#include "osn_pppoe.h"

#include "inet.h"
#include "inet_base.h"

typedef struct inet_pppoe inet_pppoe_t;

struct inet_pppoe
{
    /* Subclass inet_base_t */
    union
    {
        inet_t          inet;
        inet_base_t     base;
    };
    osn_pppoe_t        *in_pppoe;               /**< Instance of osn_pppoe_t */
    osn_netif_t        *in_parent_netif;        /**< Parent interface monitoring */
    char                in_parent_ifname[C_IFNAME_LEN];
    char                in_ppp_username[128];   /**< The PPP username */
    char                in_ppp_password[128];   /**< The PPP password */
};

inet_t *inet_pppoe_new(const char *ifname);
bool inet_pppoe_dtor(inet_t *super);

bool inet_pppoe_init(inet_pppoe_t *self, const char *ifname);
bool inet_pppoe_fini(inet_pppoe_t *self);

#endif /* INET_PPPOE_H_INCLUDED */
