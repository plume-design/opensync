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

#ifndef UDHCP_CLIENT_H_INCLUDED
#define UDHCP_CLIENT_H_INCLUDED

#include <stdbool.h>

#include "daemon.h"
#include "evx.h"

#include "osn_dhcp.h"

typedef struct udhcp_client udhcp_client_t;

typedef bool udhcp_client_opt_notify_fn_t(
        udhcp_client_t *self,
        enum osn_notify hint,
        const char *key,
        const char *value);

typedef void udhcp_client_error_fn_t(udhcp_client_t *self);

struct udhcp_client
{
    char                                uc_ifname[C_IFNAME_LEN];
    bool                                uc_started;
    daemon_t                            uc_proc;
    uint8_t                             uc_option_req[C_SET_LEN(DHCP_OPTION_MAX, uint8_t)];
    char                               *uc_option_set[DHCP_OPTION_MAX];
    char                                uc_opt_path[C_MAXPATH_LEN]; /* Option file path */
    ev_stat                             uc_opt_stat;                /* Option file watcher */
    ev_debounce                         uc_opt_debounce;            /* Debounce timer */
    udhcp_client_opt_notify_fn_t       *uc_opt_notify_fn;           /* Option notification callback */
    udhcp_client_error_fn_t            *uc_err_fn;                  /* Error notification callback */
};

bool udhcp_client_init(udhcp_client_t *self, const char *ifname);
bool udhcp_client_fini(udhcp_client_t *self);
bool udhcp_client_start(udhcp_client_t *self);
bool udhcp_client_stop(udhcp_client_t *self);
bool udhcp_client_opt_request(udhcp_client_t *self, enum osn_dhcp_option opt, bool request);
bool udhcp_client_opt_set(udhcp_client_t *self, enum osn_dhcp_option opt, const char *val);
bool udhcp_client_opt_get(udhcp_client_t *self, enum osn_dhcp_option opt, bool *request, const char **value);
bool udhcp_client_state_get(udhcp_client_t *self, bool *enabled);
bool udhcp_client_error_notify(udhcp_client_t *self, udhcp_client_error_fn_t *errfn);
bool udhcp_client_opt_notify(udhcp_client_t *self, udhcp_client_opt_notify_fn_t *fn);

#endif /* UDHCP_CLIENT_H_INCLUDED */
