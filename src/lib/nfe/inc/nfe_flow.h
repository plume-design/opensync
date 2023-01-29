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

#ifndef NFE_FLOW_H
#define NFE_FLOW_H

#include "nfe_priv.h"
#include "nfe_ipaddr.h"

static inline bool
nfe_tuple_equal(const struct nfe_tuple *lhs, const struct nfe_tuple *rhs)
{
    if (lhs->domain == rhs->domain && lhs->proto == rhs->proto && lhs->vlan == rhs->vlan) {
        if (ipaddr_equal(&lhs->addr[0], &rhs->addr[0]) && ipaddr_equal(&lhs->addr[1], &rhs->addr[1])) {
            if (lhs->port[0] == rhs->port[0] && lhs->port[1] == rhs->port[1])
                return 1;
        }
        if (ipaddr_equal(&lhs->addr[0], &rhs->addr[1]) && ipaddr_equal(&lhs->addr[1], &rhs->addr[0])) {
            if (lhs->port[0] == rhs->port[1] && lhs->port[1] == rhs->port[0])
                return 1;
        }
    }
    return 0;
}

struct nfe_tuple *nfe_tuple_copy(struct nfe_tuple *dst, const struct nfe_tuple *src, bool invert);
uint32_t nfe_tuple_hash(const struct nfe_tuple *dst);

#endif
