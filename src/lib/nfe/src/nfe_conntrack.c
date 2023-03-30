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

#include "nfe_list.h"
#include "nfe_conn.h"
#include "nfe_flow.h"
#include "nfe_conntrack.h"
#include "nfe_priv.h"

/* Private lookup
 *
 * Returns the nfe_conn for the given tuple. Out @param b is always
 * set to point to the hashed bucket.
 *
 * The returned nfe_conn will have the lockref incremented by 1 with
 * each successful lookup.
 */
static inline struct nfe_conn *
ct_lookup_hash(struct nfe_conntrack *ct, const struct nfe_tuple *tuple, uint32_t hash, struct nfe_hash_bucket **b)
{
    struct nfe_conn *conn;

    *b = &ct->bucket[hash & (ct->size-1)];

    nfe_list_for_each_entry(conn, &(*b)->list, list) {
        if (nfe_tuple_equal(&conn->tuple, tuple)) {
            conn->lockref++;
            return conn;
        }
    }

    return NULL;
}

struct nfe_conn *
nfe_conntrack_lookup_hash(struct nfe_conntrack *ct, const struct nfe_tuple *tuple, uint32_t hash)
{
    struct nfe_hash_bucket *b;
    return ct_lookup_hash(ct, tuple, hash, &b);
}

struct nfe_conn *
nfe_conntrack_lookup(struct nfe_conntrack *ct, struct nfe_packet *packet, int alloc_policy)
{
    struct nfe_hash_bucket *b;
    struct nfe_conn *conn = ct_lookup_hash(ct, &packet->tuple, packet->hash, &b);
    if (!conn) {
        if (alloc_policy != NFE_ALLOC_POLICY_NONE) {
            conn = nfe_conn_alloc(packet, alloc_policy == NFE_ALLOC_POLICY_INVERT);
            if (conn) {
                nfe_list_insert(&b->list, &conn->list);
                conn->lockref++;
            }
        }
    }
    return conn;
}

void
nfe_conntrack_lru_expire(struct nfe_conntrack *ct, int lru, uint64_t timestamp)
{
    struct nfe_conn *conn, *tmp;
    nfe_list_for_each_entry_safe(conn, tmp, &ct->lru[lru].list, lru) {
        nfe_assert(conn->lockref);
        if (timestamp < conn->timestamp ||
                (timestamp - conn->timestamp) < ct->lru[lru].expiry) {
            break;
        }
        nfe_conn_release(conn);
    }
}

void
nfe_conntrack_lru_update(struct nfe_conntrack *ct, int lru, struct nfe_list_head *item)
{
    nfe_list_remove(item);
    nfe_list_insert(&ct->lru[lru].list, item);
}


