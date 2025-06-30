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

#include "nfe_conntrack.h"
#include "nfe_flow.h"
#include "nfe_conn.h"
#include "nfe_list.h"
#include "nfe_ipaddr.h"
#include "nfe_priv.h"
#include "nfe.h"
#include "nfe_icmp.h"
#include "nfe_tcp.h"
#include "nfe_udp.h"
#include "nfe_ether.h"

struct nfe_conn *
nfe_conn_alloc(struct nfe_packet *packet, bool invert)
{
    struct nfe_conn *conn;
    struct nfe_tuple temp, *conn_tuple;

    if (invert)
        conn_tuple = nfe_tuple_copy(&temp, &packet->tuple, true);
    else
        conn_tuple = &packet->tuple;

    conn = nfe_ext_conn_alloc(sizeof(*conn), conn_tuple);
    if (conn) {
        __builtin_memset(conn, 0, sizeof(*conn));
        nfe_list_init(&conn->list);
        nfe_list_init(&conn->lru);
        nfe_tuple_copy(&conn->tuple, conn_tuple, false);
    }
    return conn;
}

struct nfe_conn *
nfe_conn_lookup_next(struct nfe_conntrack *ct, struct nfe_packet *packet)
{
    switch (packet->next) {
        case NEXT_LOOKUP_TCP:
            return nfe_tcp_lookup(ct, packet);
        case NEXT_LOOKUP_UDP:
            return nfe_udp_lookup(ct, packet);
        case NEXT_LOOKUP_ICMP:
            return nfe_icmp_lookup(ct, packet);
        case NEXT_BYPASS_ICMP:
            return nfe_icmp_bypass(ct, packet);
        case NEXT_BYPASS_ETH:
            return nfe_ether_lookup(ct, packet);
        default:
            return NULL;
    }
}
