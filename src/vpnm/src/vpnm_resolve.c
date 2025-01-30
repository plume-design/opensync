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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "vpnm.h"
#include "osn_types.h"
#include "target.h"
#include "const.h"
#include "ds_tree.h"
#include "util.h"
#include "log.h"

const char *vpnm_addr_family_desc_str(int addr_family)
{
    switch (addr_family)
    {
        case AF_INET: return "IPv4";
        case AF_INET6: return "IPv6";
        case AF_UNSPEC: return "IPv4 or IPv6";
        default: return "UNK";
    }
}

/**
 * vpnm_resolve()
 * addr_family accepted values are:
 * - AF_INET - resolve IPv4 address
 * - AF_INET6 - resolve IPv6 address
 * - AF_UNSPEC - resolve either IPv4 or IPv6 address
 */

bool vpnm_resolve(osn_ipany_addr_t *ip_addr, const char *name, int addr_family)
{
    struct addrinfo hints;
    struct addrinfo *result;
    struct addrinfo *rp;
    bool rv = false;
    int rc;

    if (!(addr_family == AF_INET || addr_family == AF_INET6 || addr_family == AF_UNSPEC))
    {
        LOG(ERROR, "vpnm_resolve: %s: Invalid address family: %d", name, addr_family);
        return false;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = addr_family;
    hints.ai_socktype = SOCK_DGRAM;

    LOG(TRACE, "vpnm_resolve: Resolving %s (addr_family=%s)",
            name, vpnm_addr_family_desc_str(addr_family));

    rc = getaddrinfo(name, NULL, &hints, &result);
    if (rc != 0)
    {
        LOG(ERROR, "vpnm_resolve: %s: DNS lookup failure: %s", name, gai_strerror(rc));
        return false;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        bool found = false;
        if (rp->ai_family == AF_INET)
        {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
            ip_addr->addr_type = AF_INET;
            ip_addr->addr.ip4 = OSN_IP_ADDR_INIT;
            ip_addr->addr.ip4.ia_addr = ipv4->sin_addr;
            found = true;
        }
        else if (rp->ai_family == AF_INET6)
        {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
            ip_addr->addr_type = AF_INET6;
            ip_addr->addr.ip6 = OSN_IP6_ADDR_INIT;
            ip_addr->addr.ip6.ia6_addr = ipv6->sin6_addr;
            found = true;
        }

        /* Take the first IP address of the requested family on the resolve list: */
        if (found && (rp->ai_family == addr_family || addr_family == AF_UNSPEC))
        {
            LOG(DEBUG, "vpnm_resolve: %s: resolved to: "PRI_osn_ipany_addr,
                    name, FMT_osn_ipany_addr(*ip_addr));
            rv = true;
            break;
        }
    }
    freeaddrinfo(result);

    return rv;
}
