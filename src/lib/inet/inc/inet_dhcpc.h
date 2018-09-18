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

#ifndef INET_DHCPC_H_INCLUDED
#define INET_DHCPC_H_INCLUDED

#include <stdbool.h>
#include "inet.h"

typedef struct __inet_dhcpc inet_dhcpc_t;
typedef void inet_dhcpc_error_fn_t(inet_dhcpc_t *self);

extern inet_dhcpc_t *inet_dhcpc_new(const char *ifname);
extern bool inet_dhcpc_del(inet_dhcpc_t *self);
extern bool inet_dhcpc_start(inet_dhcpc_t *self);
extern bool inet_dhcpc_stop(inet_dhcpc_t *self);

/* Add this option to the server request options, if none is specified a default set will be sent */
extern bool inet_dhcpc_opt_request(inet_dhcpc_t *self, enum inet_dhcp_option opt, bool request);
/* Set a DHCP client option -- these will be sent to the server */
extern bool inet_dhcpc_opt_set(inet_dhcpc_t *self, enum inet_dhcp_option opt, const char *value);
/* Retrieve DHCP option request status and set value (if any) */
extern bool inet_dhcpc_opt_get(inet_dhcpc_t *self, enum inet_dhcp_option opt, bool *request, const char **value);
/* Error callback, called whenever an error occurss on the dhcp client (suddent termination or otherwise) */
extern bool inet_dhcpc_error_fn_set(inet_dhcpc_t *self, inet_dhcpc_error_fn_t *fn);
/* Set the vendor class */
extern bool inet_dhcpc_vendorclass_set(inet_dhcpc_t *self, const char *vendorspec);
/* Get the current active state of the DHCP client */
extern bool inet_dhcpc_state_get(inet_dhcpc_t *self, bool *enabled);

#endif /* INET_DHCPC_H_INCLUDED */
