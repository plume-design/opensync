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

/* ===========================================================================
 *  DHCP Server dummy implementation
 * ===========================================================================
 */

#include "inet.h"
#include "inet_dhcps.h"


struct __inet_dhcps
{
};

inet_dhcps_t *inet_dhcps_new(const char *ifname)
{
    (void)ifname;
    LOG(INFO, "inet_dhcps: Platform doesn't support DHCP server.");
    return (void *)0xdeadc0de;
}

bool inet_dhcps_del(inet_dhcps_t *self)
{
    (void)self;

    return true;
}

bool inet_dhcps_start(inet_dhcps_t *self)
{
    (void)self;

    return true;
}

bool inet_dhcps_stop(inet_dhcps_t *self)
{
    (void)self;

    return true;
}

void inet_dhcps_error_fn(inet_dhcps_t *self, inet_dhcps_error_fn_t *fn)
{
    (void)self;
    (void)fn;
}

void inet_dhcps_lease_notify(inet_dhcps_t *self, inet_dhcp_lease_fn_t *fn, inet_t *inet)
{
    (void)self;
    (void)fn;
    (void)inet;
}

bool inet_dhcps_range(
        inet_dhcps_t *self,
        inet_ip4addr_t start,
        inet_ip4addr_t stop)
{
    (void)self;
    (void)start;
    (void)stop;

    return true;
}

bool inet_dhcps_lease(inet_dhcps_t *self, int lease_time_s)
{
    (void)self;
    (void)lease_time_s;

    return true;
}

bool inet_dhcps_option(
        inet_dhcps_t *self,
        enum inet_dhcp_option opt,
        const char *value)
{
    (void)self;
    (void)opt;
    (void)value;

    return true;
}

/* Return the current value of an option */
const char *inet_dhcps_option_get(
        inet_dhcps_t *self,
        enum inet_dhcp_option opt)
{
    (void)self;
    (void)opt;

    return NULL;
}

bool inet_dhcps_rip(inet_dhcps_t *self, inet_macaddr_t macaddr,
                    inet_ip4addr_t ip4addr, const char *hostname)
{
    (void)self;
    (void)macaddr;
    (void)ip4addr;
    (void)hostname;

    return true;
}

bool inet_dhcps_rip_remove(inet_dhcps_t *self, inet_macaddr_t macaddr)
{
    (void)self;
    (void)macaddr;

    return true;
}
