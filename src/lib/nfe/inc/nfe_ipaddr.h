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

#ifndef NFE_IPADDR_H
#define NFE_IPADDR_H

#include "nfe_priv.h"
#include <stdbool.h>

static inline bool
ipaddr_unspec(struct nfe_ipaddr *addr)
{
    return addr->addr64[0] == 0 && addr->addr64[1] == 0;
}

static inline bool
ipaddr_v4_multicast(struct nfe_ipaddr *addr)
{
    return (addr->addr8[0] & 0xf0) == 0xe0;
}

static inline bool
ipaddr_v6_multicast(struct nfe_ipaddr *addr)
{
    return addr->addr8[0] == 0xff;
}

static inline bool
ipaddr_v4_unicast(struct nfe_ipaddr *addr)
{
    return !(ipaddr_v4_multicast(addr) || ipaddr_unspec(addr));
}

static inline bool
ipaddr_v6_unicast(struct nfe_ipaddr *addr)
{
    return !(ipaddr_v6_multicast(addr) || ipaddr_unspec(addr));
}

static inline void
ipaddr_copy_in4(struct nfe_ipaddr *dst, const void *in4)
{
    dst->addr64[0] = 0;
    dst->addr64[1] = 0;
    __builtin_memcpy(&dst->addr32[0], in4, 4);
}

static inline void
ipaddr_copy_in6(struct nfe_ipaddr *dst, const void *in6)
{
    __builtin_memcpy(&dst->addr64[0], in6, 16);
}

static inline void
ipaddr_copy(struct nfe_ipaddr *dst, const struct nfe_ipaddr *src)
{
    ipaddr_copy_in6(dst, &src->addr8[0]);
}

static inline bool
ipaddr_equal(const struct nfe_ipaddr *lhs, const struct nfe_ipaddr *rhs)
{
    return lhs->addr64[0] == rhs->addr64[0] &&
        lhs->addr64[1] == rhs->addr64[1];
}

#endif

