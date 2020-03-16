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

#ifndef ODHCP6_CLIENT_H_INCLUDED
#define ODHCP6_CLIENT_H_INCLUDED

#include <stdbool.h>

#include "evx.h"
#include "daemon.h"
#include "osn_dhcpv6.h"

typedef struct odhcp6_client odhcp6_client_t;
typedef void odhcp6_client_status_fn_t(
        odhcp6_client_t *self,
        struct osn_dhcpv6_client_status *status);

struct odhcp6_client
{
    char        oc_ifname[C_IFNAME_LEN];                    /* Interface name */
    daemon_t    oc_daemon;                                  /* Service object */
    bool        oc_request_address;                         /* Request address */
    bool        oc_request_prefixes;                        /* Request prefixes */
    bool        oc_rapid_commit;                            /* Rapid commit */
    bool        oc_renew;                                   /* Renew address */
    bool        oc_option_request[OSN_DHCP_OPTIONS_MAX];    /* Options requested from server */
    char       *oc_option_send[OSN_DHCP_OPTIONS_MAX];       /* Options sent to server */
    ev_stat     oc_opts_ev;                                 /* Options file watcher */
    ev_debounce oc_debounce;                                /* Debouncer for the file watcher */
    char        oc_opts_file[C_MAXPATH_LEN];                /* Path to the options file */
    char        oc_pid_file[C_MAXPATH_LEN];                 /* Path to the options file */

    odhcp6_client_status_fn_t
               *oc_notify_fn;                               /* Notify function */

    struct osn_dhcpv6_client_status                         /* Status reporting */
                oc_status;
};

bool odhcp6_client_init(odhcp6_client_t *self, const char *ifname);
bool odhcp6_client_fini(odhcp6_client_t *self);
bool odhcp6_client_apply(odhcp6_client_t *self);

bool odhcp6_client_set(
        odhcp6_client_t *self,
        bool request_address,
        bool request_prefixes,
        bool rapid_commit,
        bool renew);

bool odhcp6_client_option_request(odhcp6_client_t *self, int tag);
bool odhcp6_client_option_send(odhcp6_client_t *self, int tag, const char *value);
void odhcp6_client_status_notify(odhcp6_client_t *self, odhcp6_client_status_fn_t *fn);

#endif /* ODHCP6_CLIENT_H_INCLUDED */
