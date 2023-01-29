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

#include "nfe_ether.h"
#include "nfe_icmp.h"
#include "nfe_proto.h"
#include "nfe_conn.h"
#include "nfe_conntrack.h"
#include "nfe_priv.h"
#include "nfe_config.h"
#include "nfe.h"

/* Barebones icmp added so the caller can do dpi using librts. Probably
 * a better solution would be to carefully allocate icmp state and when
 * applicable, establish a 'related-to' relationship with another connection.
 * In the case of icmp echo, this wouldn't apply, but in the case of host
 * or port unreachable it would.
 *
 * In this first hack (done to make DDoS detection possible in rts), the
 * icmp header is not pulled off and rts has full access via @conn->data
 * to the icmp.
 */

struct nfe_conn *
nfe_icmp_bypass(struct nfe_conntrack *conntrack, struct nfe_packet *p)
{
    uint8_t type;

    if ((size_t)(p->tail - p->data) < sizeof (struct icmphdr))
        return NULL;

    type = p->data[0];

    /* react to 'destination unreachable' */
    if (type == 3) {
        uint16_t eth_type;
        p->data += sizeof(struct icmphdr);
        if ((eth_type = eth_type_from_ip(p->data[0])) != 0) {
            struct nfe_packet packet;
            struct nfe_conn *conn;
            nfe_packet_hash(&packet, eth_type, p->data, p->tail - p->data, p->timestamp);
            conn = nfe_conntrack_lookup(conntrack, &packet, NFE_ALLOC_POLICY_NONE);
            if (conn) {
                /* once for the lookup */
                nfe_conn_release(conn);
                /* once to unlink and free */
                nfe_conn_release(conn);
            }
        }
        p->data -= sizeof(struct icmphdr);
    }
    return nfe_conn_alloc(p, false);
}

struct nfe_conn *
nfe_icmp_lookup(struct nfe_conntrack *conntrack, struct nfe_packet *packet)
{
    struct nfe_conn *conn;

    nfe_conntrack_lru_expire(conntrack, LRU_PROTO_ICMP, packet->timestamp);
    conn = nfe_conntrack_lookup(conntrack, packet, NFE_ALLOC_POLICY_CREATE);
    if (conn) {
        conn->timestamp = packet->timestamp;
        nfe_conntrack_lru_update(conntrack, LRU_PROTO_ICMP, &conn->lru);
    }
    return conn;
}

int
nfe_proto_icmp(struct nfe_packet *p)
{
    uint8_t type;

    if ((size_t)(p->tail - p->data) < sizeof (struct icmphdr))
        return -1;

    type = p->data[0];

    /* stateless conn for unreachable, source-quench, redirect */
    if (type >= 3 && type <= 5) {
        p->next = NEXT_BYPASS_ICMP;
    } else {
        p->next = NEXT_LOOKUP_ICMP;
    }

    return 0;
}

int
nfe_proto_icmp6(struct nfe_packet *p)
{
    if ((size_t)(p->tail - p->data) < sizeof (struct icmp6hdr))
        return -1;

    p->next = NEXT_LOOKUP_ICMP;
    return 0;
}
