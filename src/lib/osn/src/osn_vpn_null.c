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

#include <stdbool.h>

#include "memutil.h"
#include "osn_types.h"
#include "osn_vpn.h"

struct osn_vpn
{

};

osn_vpn_t *osn_vpn_new(const char *name)
{
    (void)name;

    osn_vpn_t *self = CALLOC(1, sizeof(struct osn_vpn));

    return self;
}

const char *osn_vpn_name_get(osn_vpn_t *self)
{
    (void)self;

    return "null";
}

bool osn_vpn_enable_set(osn_vpn_t *self, bool enable)
{
    (void)self;
    (void)enable;

    return true;
}

bool osn_vpn_healthcheck_ip_set(osn_vpn_t *self, osn_ipany_addr_t *ip)
{
    (void)self;
    (void)ip;

    return true;
}

bool osn_vpn_healthcheck_interval_set(osn_vpn_t *self, int interval)
{
    (void)self;
    (void)interval;

    return true;
}

bool osn_vpn_healthcheck_timeout_set(osn_vpn_t *self, int timeout)
{
    (void)self;
    (void)timeout;

    return true;
}

bool osn_vpn_healthcheck_src_set(osn_vpn_t *self, const char *src)
{
    (void)self;
    (void)src;

    return true;
}

bool osn_vpn_healthcheck_enable_set(osn_vpn_t *self, bool enable)
{
    (void)self;
    (void)enable;

    return true;
}

bool osn_vpn_healthcheck_notify_status_set(osn_vpn_t *self, osn_vpn_health_status_fn_t *status_cb)
{
    (void)self;
    (void)status_cb;

    return true;
}

bool osn_vpn_healthcheck_apply(osn_vpn_t *self)
{
    (void)self;

    return true;
}

bool osn_vpn_del(osn_vpn_t *self)
{
    FREE(self);

    return true;
}

const char *osn_vpn_conn_state_to_str(enum osn_vpn_conn_state vpn_conn_state)
{
    return "null";
}

const char *osn_vpn_health_status_to_str(enum osn_vpn_health_status vpn_health_state)
{
    return "null";
}
