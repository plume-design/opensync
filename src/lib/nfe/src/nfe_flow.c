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

#include "nfe_flow.h"
#include "nfe_priv.h"
#include "jhash.h"

struct nfe_tuple *
nfe_tuple_copy(struct nfe_tuple *dst, const struct nfe_tuple *src, bool invert)
{
    if (invert) {
        dst->addr[0].addr64[0] = src->addr[1].addr64[0];
        dst->addr[0].addr64[1] = src->addr[1].addr64[1];
        dst->addr[1].addr64[0] = src->addr[0].addr64[0];
        dst->addr[1].addr64[1] = src->addr[0].addr64[1];
        dst->port[0] = src->port[1];
        dst->port[1] = src->port[0];
    } else {
        dst->addr[0].addr64[0] = src->addr[0].addr64[0];
        dst->addr[0].addr64[1] = src->addr[0].addr64[1];
        dst->addr[1].addr64[0] = src->addr[1].addr64[0];
        dst->addr[1].addr64[1] = src->addr[1].addr64[1];
        dst->port[0] = src->port[0];
        dst->port[1] = src->port[1];
    }
    dst->proto  = src->proto;
    dst->domain = src->domain;
    dst->vlan   = src->vlan;
    return dst;
}

uint32_t
nfe_tuple_hash(const struct nfe_tuple *tuple)
{
    struct nfe_tuple rev;

    nfe_static_assert(sizeof(struct nfe_ipaddr) % 4 == 0, "broken nfe_tuple");
    nfe_static_assert(offsetof(struct nfe_tuple, addr[0]) == 0, "broken nfe_tuple");

    if (tuple->port[0] > tuple->port[1] || (tuple->port[0] == tuple->port[1] &&
        __builtin_memcmp(tuple->addr[0].addr8, tuple->addr[1].addr8, domain_len(tuple->domain)) > 0)) {
        nfe_tuple_copy(&rev, tuple, true);
        tuple = &rev;
    }

    return jhash2(&tuple->addr[0].addr32[0],
        (sizeof(tuple->addr) + sizeof(tuple->port))/4,
        ((uint32_t)tuple->domain << 24) | ((uint32_t)tuple->proto << 16) | tuple->vlan);
}
