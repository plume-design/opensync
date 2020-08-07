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

#ifndef LNX_PPPOE_H_INCLUDED
#define LNX_PPPOE_H_INCLUDED

#include <stdbool.h>
#include <ev.h>

#include "const.h"
#include "daemon.h"
#include "osn_pppoe.h"

#include "lnx_netlink.h"

typedef struct lnx_pppoe lnx_pppoe_t;
typedef void lnx_pppoe_status_fn_t(
        lnx_pppoe_t *self,
        struct osn_pppoe_status *status);

struct lnx_pppoe
{
    char                    lp_ifname[C_IFNAME_LEN];        /* PPPoE interface name */
    char                    lp_pifname[C_IFNAME_LEN];       /* Parent interface */
    daemon_t                lp_pppd;                        /* pppd process structure ?*/
    lnx_netlink_t           lp_nl;                          /* Netlink socket */
    ev_async                lp_async;                       /* Async event feeder */
    struct osn_pppoe_status lp_status;                      /* Cached status structure */
    lnx_pppoe_status_fn_t  *lp_status_fn;                   /* Status callback */
};

bool lnx_pppoe_init(lnx_pppoe_t *self, const char *ifname);
bool lnx_pppoe_fini(lnx_pppoe_t *self);
bool lnx_pppoe_apply(lnx_pppoe_t *self);
void lnx_pppoe_status_notify(lnx_pppoe_t *self, lnx_pppoe_status_fn_t *fn);
bool lnx_pppoe_parent_set(lnx_pppoe_t *self, const char *pifname);
bool lnx_pppoe_secret_set(lnx_pppoe_t *self, const char *username, const char *password);

#endif /* LNX_PPPOE_H_INCLUDED */
