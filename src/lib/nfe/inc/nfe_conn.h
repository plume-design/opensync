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

#ifndef NFE_CONN_H
#define NFE_CONN_H

#include "nfe_priv.h"
#include "nfe_list.h"
#include "nfe_tcp.h"
#include "nfe_udp.h"

enum {
    NEXT_BYPASS_ICMP = 1,
    NEXT_LOOKUP_ICMP = 2,
    NEXT_LOOKUP_TCP  = 3,
    NEXT_LOOKUP_UDP  = 4,
    NEXT_BYPASS_ETH  = 5
};

struct nfe_conn {

    struct nfe_tuple tuple;
    struct nfe_list_head list;
    struct nfe_list_head lru;

    union {
        struct nfe_tcp tcp;
        struct nfe_udp udp;
    } cb;

    uint32_t lockref;
    uint32_t flags;
    uint64_t timestamp;
};

struct nfe_conntrack;
struct nfe_packet;

struct nfe_conn *nfe_conn_lookup_next(struct nfe_conntrack *ct, struct nfe_packet *packet);

struct nfe_conn *nfe_conn_alloc(struct nfe_packet *packet, bool invert);

void nfe_conn_release(nfe_conn_t conn);

#endif
