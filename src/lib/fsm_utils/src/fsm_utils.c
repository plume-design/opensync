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

#include <string.h>
#include <arpa/inet.h>
#include "memutil.h"
#include "fsm_utils.h"

/**< @copydoc sockaddr_storage_equals */
bool
sockaddr_storage_equals(struct sockaddr_storage *a, struct sockaddr_storage *b)
{
    struct sockaddr_in6 *a_v6;
    struct sockaddr_in6 *b_v6;
    struct sockaddr_in *a_v4;
    struct sockaddr_in *b_v4;
    bool ret;
    int cmp;

    if (a->ss_family != b->ss_family) return false;

    ret = false;
    switch (a->ss_family)
    {
        case AF_INET:
            a_v4 = (struct sockaddr_in *)a;
            b_v4 = (struct sockaddr_in *)b;
            ret = (a_v4->sin_addr.s_addr == b_v4->sin_addr.s_addr);
            break;
        case AF_INET6:
            a_v6 = (struct sockaddr_in6 *)a;
            b_v6 = (struct sockaddr_in6 *)b;
            cmp = memcmp(&a_v6->sin6_addr, &b_v6->sin6_addr, sizeof(a_v6->sin6_addr));
            ret = (cmp == 0);
            break;
        default:
            ret = false; /* we can't match anything */
    }

    return ret;
}


/**< @copydoc sockaddr_storage_equals_addr */
bool
sockaddr_storage_equals_addr(struct sockaddr_storage *addr, uint8_t *ip_bytes, size_t len)
{

    struct sockaddr_in6 *addr_v6;
    struct sockaddr_in *addr_v4;
    bool ret;
    int cmp;

    ret = false;
    switch (addr->ss_family)
    {
        case AF_INET:
            addr_v4 = (struct sockaddr_in *)addr;
            if (len == sizeof(addr_v4->sin_addr))
            {
                cmp = memcmp(&addr_v4->sin_addr, ip_bytes, len);
                ret = (cmp == 0);
            }
            break;

        case AF_INET6:
            addr_v6 = (struct sockaddr_in6 *)addr;
            if (len == sizeof(addr_v6->sin6_addr))
            {
                cmp = memcmp(&addr_v6->sin6_addr, ip_bytes, len);
                ret = (cmp == 0);
            }
            break;

        default:
            ret = false; /* we can't match anything */
    }

    return ret;
}


/**< @copydoc sockaddr_storage_create */
struct sockaddr_storage *
sockaddr_storage_create(int af, char *ip_str)
{
    struct sockaddr_storage *retval;
    struct sockaddr_in6 *in6;
    struct sockaddr_in *in4;
    struct in6_addr in_ip6;
    struct in_addr in_ip;

    retval = CALLOC(1, sizeof(*retval));
    switch (af)
    {
        case AF_INET:
            inet_pton(AF_INET, ip_str, &in_ip);
            in4 = (struct sockaddr_in *)retval;
            in4->sin_family = AF_INET;
            memcpy(&in4->sin_addr, &in_ip, sizeof(in4->sin_addr));
            break;
        case AF_INET6:
            inet_pton(AF_INET6, ip_str, &in_ip6);
            in6 = (struct sockaddr_in6 *)retval;
            in6->sin6_family = AF_INET6;
            memcpy(&in6->sin6_addr, &in_ip6, sizeof(in6->sin6_addr));
            break;
        default:
            FREE(retval);
            return NULL;
    }

    return retval;

}
