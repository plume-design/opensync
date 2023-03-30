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

#ifndef RTS_FT_H
#define RTS_FT_H

#include "rts_list.h"
#include "rts_lruhash.h"
#include "rts_ipaddr.h"

#define FT_DADDR_WEIGHT 16
#define FT_DPORT_WEIGHT 8
#define FT_SPORT_WEIGHT 4
#define FT_PROTO_WEIGHT 2
#define FT_SADDR_WEIGHT 1

#define FT_SCORE_1 \
    (FT_DADDR_WEIGHT + FT_DPORT_WEIGHT + FT_SPORT_WEIGHT + FT_SADDR_WEIGHT + FT_PROTO_WEIGHT)
#define FT_SCORE_2 \
    (FT_DPORT_WEIGHT + FT_SPORT_WEIGHT + FT_SADDR_WEIGHT + FT_PROTO_WEIGHT)
#define FT_SCORE_3 \
    (FT_SPORT_WEIGHT + FT_SADDR_WEIGHT + FT_PROTO_WEIGHT)
#define FT_SCORE_4 \
    (FT_SADDR_WEIGHT + FT_PROTO_WEIGHT)

struct rts_ftentry {
    struct rts_lruhash_item hash;
    struct rts_ipaddr saddr;
    struct rts_ipaddr daddr;
    uint16_t sport;
    uint16_t dport;
    uint16_t proto;
    uint16_t padding;
    uint32_t pc;
};

static inline uint8_t
rts_ft_score(struct rts_ftentry *entry, int proto, struct rts_ipaddr *saddr,
    uint16_t sport, struct rts_ipaddr *daddr, uint16_t dport)
{
    uint8_t score = 0;

    if (entry->proto != 0) {
        if (entry->proto != proto)
            return 0;
        score += FT_PROTO_WEIGHT;
    }

    if (entry->dport != 0) {
        if (entry->dport != dport)
            return 0;
        score += FT_DPORT_WEIGHT;
    }

    if (entry->sport != 0) {
        if (entry->sport != sport)
            return 0;
        score += FT_SPORT_WEIGHT;
    }

    if (!rts_ipaddr_unspec(&entry->daddr)) {
        if (!rts_ipaddr_equal(&entry->daddr, daddr))
            return 0;
        score += FT_DADDR_WEIGHT;
    }

    if (!rts_ipaddr_unspec(&entry->saddr)) {
        if (!rts_ipaddr_equal(&entry->saddr, saddr))
            return 0;
        score += FT_SADDR_WEIGHT;
    }

    return score;
}

static inline struct rts_ftentry *
__rts_ft_best(struct rts_lruhash *ft, uint32_t hash, uint8_t target,
    int proto, struct rts_ipaddr *saddr, uint16_t sport,
    struct rts_ipaddr *daddr, uint16_t dport, uint64_t timestamp)
{
    uint8_t best_score = 0, curr_score = 0;
    struct rts_lruhash_item *item;
    struct rts_ftentry *e, *best = NULL;
    struct rts_list_head *list;

    list = &ft->bucket[hash & ft->mask];
    rts_list_for_each_entry(item, list, hash) {
        e = rts_container_of(item, struct rts_ftentry, hash);
        if ((timestamp - e->hash.touched) <= e->hash.ttl) {
            curr_score = rts_ft_score(e, proto, saddr, sport, daddr, dport);
            if (best_score < curr_score) {
                if ((best_score = curr_score) >= target)
                    return e;
                best = e;
            }
        }
    }

    return best;
}

static inline struct rts_ftentry *
rts_ft_find(struct rts_lruhash *fm, int proto, struct rts_ipaddr *saddr, uint16_t sport,
    struct rts_ipaddr *daddr, uint16_t dport, uint64_t timestamp)
{
    struct rts_ftentry *p;

    if ((p = __rts_ft_best(fm, daddr->addr32[3], FT_SCORE_1, proto, saddr, sport, daddr, dport, timestamp)) != NULL)
        return p;

    if ((p = __rts_ft_best(fm, dport, FT_SCORE_2, proto, saddr, sport, daddr, dport, timestamp)) != NULL)
        return p;

    if ((p = __rts_ft_best(fm, sport, FT_SCORE_3, proto, saddr, sport, daddr, dport, timestamp)) != NULL)
        return p;

    return __rts_ft_best(fm, saddr->addr32[3], FT_SCORE_4, proto, saddr, sport, daddr, dport, timestamp);
}

static inline void
rts_ft_save(struct rts_lruhash *fm, struct rts_ftentry *e, uint32_t ttl, uint64_t timestamp)
{
    uint32_t hashval;

    if (!rts_ipaddr_unspec(&e->daddr))
        hashval = e->daddr.addr32[3];
    else if (e->dport)
        hashval = e->dport;
    else if (e->sport)
        hashval = e->sport;
    else
        hashval = e->saddr.addr32[3];

    rts_lruhash_insert(fm, &e->hash, hashval, ttl, timestamp);
}

#endif
