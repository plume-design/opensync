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

#include "nfe.h"
#include "nfe_priv.h"
#include "nfe_list.h"
#include "nfe_ether.h"
#include "nfe_input.h"
#include "nfe_flow.h"
#include "nfe_conn.h"
#include "nfe_conntrack.h"
#include "nfe_config.h"

#ifdef KERNEL
#include <linux/errno.h>
#else
#include <errno.h>
#endif

#include <stdio.h>
#include <ctype.h>

static uint32_t
clp2(uint32_t n)
{
    --n;
    n |= (n >> 1);
    n |= (n >> 2);
    n |= (n >> 4);
    n |= (n >> 8);
    n |= (n >> 16);
    return ++n;
}

EXPORT int
nfe_conntrack_create(nfe_conntrack_t *h, uint32_t size)
{
    size_t sz;
    uint32_t i;
    struct nfe_conntrack *ct;

    size = clp2(size);
    sz = sizeof(*ct) + (sizeof(*ct->bucket) * size);

    if (!h || !size)
        return -EINVAL;

    if (!(ct = nfe_ext_alloc(sz)))
        return -ENOMEM;

    ct->size = size;

    ct->lru[LRU_PROTO_ICMP].expiry = nfe_conntrack_icmp_timeout * 1000;
    nfe_list_init(&ct->lru[LRU_PROTO_ICMP].list);

    ct->lru[LRU_PROTO_TCP_SYN].expiry = nfe_conntrack_tcp_timeout_syn * 1000;
    nfe_list_init(&ct->lru[LRU_PROTO_TCP_SYN].list);

    ct->lru[LRU_PROTO_TCP_EST].expiry = nfe_conntrack_tcp_timeout_est * 1000;
    nfe_list_init(&ct->lru[LRU_PROTO_TCP_EST].list);

    ct->lru[LRU_PROTO_UDP].expiry = nfe_conntrack_udp_timeout * 1000;
    nfe_list_init(&ct->lru[LRU_PROTO_UDP].list);

    for (i = 0; i < size; i++) {
        nfe_list_init(&ct->bucket[i].list);
    }

    *h = ct;
    return 0;
}

EXPORT int
nfe_conntrack_destroy(struct nfe_conntrack *ct)
{
    unsigned i;
    struct nfe_conn *conn, *tmp;

    if (!ct)
        return -EINVAL;

    for (i = 0; i < LRU_PROTO_MAX; i++) {
        nfe_list_for_each_entry_safe(conn, tmp, &ct->lru[i].list, lru) {
            nfe_list_remove(&conn->lru);
            nfe_list_remove(&conn->list);
            nfe_conn_release(conn);
        }
    }
    nfe_ext_free(ct);
    return 0;
}

EXPORT int
nfe_packet_hash(struct nfe_packet *p, uint16_t ethertype, const uint8_t *data, size_t len, uint64_t timestamp)
{
    int err;

    if (!p || (!data && len > 0))
        return -EINVAL;

    p->timestamp = timestamp;
    p->direction = 0;
    p->type = NFE_PACKET_TYPE_UNKNOWN;
    p->next = 0;
    p->hash = 0;
    p->user = NULL;
    p->head = data;
    p->data = data;
    p->tail = data + len;
    p->prot = NULL;
    __builtin_memset(&p->tuple, 0, sizeof(p->tuple));

    if ((size_t)(p->tail - p->data) < sizeof(struct ethhdr))
        return -1;

    if (ethertype == 0) {
        if (p->data[0] & 0x01) {
            if (p->data[0] == 0xff) {
                p->type = NFE_PACKET_TYPE_BROADCAST;
            } else {
                p->type = NFE_PACKET_TYPE_MULTICAST;
            }
        } else {
            p->type = NFE_PACKET_TYPE_HOST;
        }

        ethertype = read16(p->data + offsetof(struct ethhdr, type));
        p->data += sizeof(struct ethhdr);
    }

    err = nfe_input_handoff(ethertype)(p);
    if (!err)
        p->hash = nfe_tuple_hash(&p->tuple);
    return err;
}

EXPORT nfe_conn_t
nfe_conn_lookup(nfe_conntrack_t conntrack, struct nfe_packet *packet)
{
    if (!conntrack || !packet)
        return NULL;

    if (packet->next) {
        if (packet->type != NFE_PACKET_TYPE_BROADCAST) {
            return nfe_conn_lookup_next(conntrack, packet);
        } else {
            return nfe_conn_alloc(packet, false);
        }
    }
    return NULL;
}

EXPORT nfe_conn_t
nfe_conn_lookup_by_tuple(nfe_conntrack_t conntrack,
    const struct nfe_tuple *tuple, uint64_t timestamp, int *dir)
{
    struct nfe_conn *conn;
    int lru;

    if (!conntrack || !tuple)
        return NULL;

    switch (tuple->proto) {
        case IPPROTO_ICMP:
            lru = LRU_PROTO_ICMP;
            break;
        case IPPROTO_TCP:
            nfe_conntrack_lru_expire(conntrack, LRU_PROTO_TCP_SYN, timestamp);
            lru = LRU_PROTO_TCP_EST;
            break;
        case IPPROTO_UDP:
            lru = LRU_PROTO_UDP;
            break;
        default:
            return NULL;
    }

    nfe_conntrack_lru_expire(conntrack, lru, timestamp);

    conn = nfe_conntrack_lookup_hash(conntrack, tuple, nfe_tuple_hash(tuple));
    if (conn) {
        nfe_conntrack_lru_update(conntrack, lru, &conn->lru);
        if (dir) {
            *dir = __builtin_memcmp(&conn->tuple.addr[0], &tuple->addr[0],
                domain_len(tuple->domain)) ? 1 : 0;
        }
    }
    return conn;
}

EXPORT void
nfe_conn_release(nfe_conn_t conn)
{
    if (!conn)
        return;

    nfe_assert(conn->lockref >= 1);

    if (--(conn->lockref) == 0) {
        nfe_list_remove(&conn->list);
        nfe_list_remove(&conn->lru);
        nfe_ext_conn_free(conn, &conn->tuple);
    }
}

#ifndef KERNEL
EXPORT __attribute__((weak)) void *
nfe_ext_alloc(size_t size) {
    return malloc(size);
}

EXPORT __attribute__((weak)) void
nfe_ext_free(void *p) {
    free(p);
}

#ifndef OPTION_NWEAK
EXPORT __attribute__((weak)) void *
nfe_ext_conn_alloc(size_t size, const struct nfe_tuple *tuple) {
    (void) tuple;
    return nfe_ext_alloc(size);
}

EXPORT __attribute__((weak)) void
nfe_ext_conn_free(void *p, const struct nfe_tuple *tuple) {
    (void) tuple;
    nfe_ext_free(p);
}
#endif
#endif
