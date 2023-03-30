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

#include "nfe_udp.h"
#include "nfe_proto.h"
#include "nfe_conntrack.h"
#include "nfe_conn.h"
#include "nfe_config.h"

struct nfe_conn *
nfe_udp_lookup(struct nfe_conntrack *conntrack, struct nfe_packet *packet)
{
    struct nfe_conn *conn;

    nfe_conntrack_lru_expire(conntrack, LRU_PROTO_UDP, packet->timestamp);
    conn = nfe_conntrack_lookup(conntrack, packet, NFE_ALLOC_POLICY_CREATE);
    if (conn) {
        conn->timestamp = packet->timestamp;
        packet->direction = __builtin_memcmp(&conn->tuple.addr[0], &packet->tuple.addr[0],
            domain_len(conn->tuple.domain)) ? 1 : 0;

        nfe_conntrack_lru_update(conntrack, LRU_PROTO_UDP, &conn->lru);
    }
    
    packet->next = 0;
    packet->prot = NULL;

    return conn;
}

int
nfe_proto_udp(struct nfe_packet *p)
{
    if (p->tail - p->data < 8)
        return -1;

    nfe_assert(p->data == p->prot);

    __builtin_memcpy(&p->tuple.port[0], p->data + 0, 2);
    __builtin_memcpy(&p->tuple.port[1], p->data + 2, 2);

    p->next = NEXT_LOOKUP_UDP;
    p->data += sizeof(struct udphdr);
    return 0;
}

