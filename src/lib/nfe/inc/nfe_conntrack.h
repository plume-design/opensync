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

#ifndef NFE_CONNTRACK_H
#define NFE_CONNTRACK_H

#include "nfe_priv.h"
#include "nfe_list.h"

enum {
    NFE_ALLOC_POLICY_NONE   = 0,
    NFE_ALLOC_POLICY_CREATE = 1,
    NFE_ALLOC_POLICY_INVERT = 2
};

enum {
    LRU_PROTO_ICMP    = 0,
    LRU_PROTO_TCP_SYN = 1,
    LRU_PROTO_TCP_EST = 2,
    LRU_PROTO_UDP     = 3,
    LRU_PROTO_ETHER  = 4,

    LRU_PROTO_MAX
};

struct nfe_hash_lru {
    uint64_t expiry;
    struct nfe_list_head list;
};

struct nfe_hash_bucket {
    struct nfe_list_head list;
};

struct nfe_conntrack {
    int size;
    struct nfe_hash_lru lru[LRU_PROTO_MAX];
    struct nfe_hash_bucket bucket[0];
};

struct nfe_conn *nfe_conntrack_lookup_hash(struct nfe_conntrack *conntrack, 
    const struct nfe_tuple *tuple, uint32_t hash);

struct nfe_conn *nfe_conntrack_lookup(struct nfe_conntrack *conntrack,
    struct nfe_packet *packet, int alloc_policy);

void nfe_conntrack_lru_expire(struct nfe_conntrack *conntrack, int lru, uint64_t timestamp);
void nfe_conntrack_lru_update(struct nfe_conntrack *conntrack, int lru, struct nfe_list_head *item);

#endif
