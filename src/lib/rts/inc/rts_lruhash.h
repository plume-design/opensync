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

#ifndef RTS_LRUHASH_H
#define RTS_LRUHASH_H

#include "rts_list.h"

struct rts_lruhash_item {
    struct rts_list_head hash;
    struct rts_list_head lru;
    uint64_t touched;
    uint32_t ttl;
};

struct rts_lruhash {
    bool (*equalto)(const void *, const struct rts_lruhash_item *);
    uint32_t expiry;
    uint32_t mask;
    struct rts_list_head lru;
    struct rts_list_head bucket[0];
};

#define sizeof_rts_lruhash(size) \
    (sizeof(struct rts_lruhash) + ((size) * sizeof(struct rts_list_head)))

static inline void
rts_lruhash_init(struct rts_lruhash *h,
    bool (*equalto)(const void *, const struct rts_lruhash_item *),
    uint32_t expiry, uint32_t size)
{
    uint32_t i;

    h->equalto = equalto;

    for (i = 0; i < size; i++)
        rts_list_init(&h->bucket[i]);

    rts_list_init(&h->lru);

    h->expiry = expiry;
    h->mask = size - 1;
}

static inline void
rts_lruhash_for_each(struct rts_lruhash *h, void (*f)(struct rts_lruhash_item *))
{
    int i = 0;
    struct rts_lruhash_item *ptr;
    struct rts_list_head *list;
    do {
        list = &h->bucket[i];
        rts_list_for_each_entry(ptr, list, hash) {
            f(ptr);
        }
        i = ((i + 1) & h->mask);
    } while (i);
}

static inline struct rts_lruhash_item *
rts_lruhash_find(struct rts_lruhash *h, const void *key, uint32_t hashval, uint64_t timestamp)
{
    struct rts_lruhash_item *ptr;
    struct rts_list_head *list = &h->bucket[hashval & h->mask];
    rts_list_for_each_entry(ptr, list, hash) {
        if (h->equalto(key, ptr) && ((timestamp - ptr->touched) <= ptr->ttl)) {
            rts_list_remove(&ptr->lru);
            rts_list_insert(&h->lru, &ptr->lru);
            ptr->touched = timestamp;
            return ptr;
        }
    }
    return 0;
}

static inline void
rts_lruhash_insert(struct rts_lruhash *h, struct rts_lruhash_item *item, uint32_t hashval, uint32_t ttl, uint64_t timestamp)
{
    rts_list_insert(h->bucket[hashval & h->mask].next, &item->hash);
    rts_list_insert(&h->lru, &item->lru);
    item->touched = timestamp;
    item->ttl     = ttl;
}

static inline void
rts_lruhash_remove(struct rts_lruhash_item *item)
{
    rts_list_remove(&item->hash);
    rts_list_remove(&item->lru);
}

/* Returns an expired entry or null when the no entries are expired. */
static inline struct rts_lruhash_item *
rts_lruhash_expire(struct rts_lruhash *h, uint64_t timestamp)
{
    uint32_t elapsed;
    struct rts_lruhash_item *ptr;
    if (!rts_list_empty(&h->lru)) {
next:
        ptr = rts_container_of(h->lru.next, struct rts_lruhash_item, lru);
        elapsed = timestamp - ptr->touched;
        if (elapsed > h->expiry) {
            if (ptr->ttl <= elapsed) {
                ptr->ttl = 0;
                rts_lruhash_remove(ptr);
                return ptr;
            } else {
                ptr->ttl -= elapsed;
                rts_list_remove(&ptr->lru);
                rts_list_insert(&h->lru, &ptr->lru);
                ptr->touched = timestamp;
                goto next;
            }
        }
    }
    return 0;
}

#endif
