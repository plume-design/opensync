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

#ifndef RTS_BITSET_H
#define RTS_BITSET_H

#include <inttypes.h>
#include <endian.h>

static inline unsigned int
popcountll(uint64_t x)
{
    x = (x & 0x5555555555555555ULL) + ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x & 0x0F0F0F0F0F0F0F0FULL) + ((x >> 4) & 0x0F0F0F0F0F0F0F0FULL);
    return (x * 0x0101010101010101ULL) >> 56;
}

struct bitset {
    uint64_t bits[8];
};

static inline void
rts_bitset_bswap(struct bitset *set)
{
    int i;
    for (i = 0; i < 8; i++) {
        set->bits[i] = be64toh(set->bits[i]);
    }
}

static inline void
rts_bitset_init(struct bitset *set)
{
    set->bits[0] = 0;
    set->bits[1] = 0;
    set->bits[2] = 0;
    set->bits[3] = 0;
    set->bits[4] = 0;
    set->bits[5] = 0;
    set->bits[6] = 0;
    set->bits[7] = 0;
}

static inline unsigned int
rts_bitset_pop(struct bitset *set)
{
    return
        popcountll(set->bits[0]) +
        popcountll(set->bits[1]) +
        popcountll(set->bits[2]) +
        popcountll(set->bits[3]) +
        popcountll(set->bits[4]) +
        popcountll(set->bits[5]) +
        popcountll(set->bits[6]) +
        popcountll(set->bits[7]);
}

static inline int
rts_bitset_empty(struct bitset *set)
{
    return !(set->bits[0] || set->bits[1] ||
        set->bits[2] || set->bits[3] ||
        set->bits[4] || set->bits[5] ||
        set->bits[6] || set->bits[7]);
}

static inline void
rts_bitset_copy(struct bitset *dst, struct bitset *src)
{
    __builtin_memcpy(dst, src, sizeof(*src));
}

static inline int
rts_bitset_equal(struct bitset *lhs, struct bitset *rhs)
{
    return (
        lhs->bits[0] == rhs->bits[0] &&
        lhs->bits[1] == rhs->bits[1] && 
        lhs->bits[2] == rhs->bits[2] &&
        lhs->bits[3] == rhs->bits[3] &&
        lhs->bits[4] == rhs->bits[4] &&
        lhs->bits[5] == rhs->bits[5] && 
        lhs->bits[6] == rhs->bits[6] &&
        lhs->bits[7] == rhs->bits[7]
    );
}

static inline int
rts_bitset_contains(struct bitset *set, int bit)
{
    uint64_t curval;
    curval = set->bits[(bit >> 6)];
    return ((curval >> (bit & 63)) & (uint64_t)1) == 1;
}

static inline unsigned int
rts_bitset_popcount_nth(struct bitset *set, int bit)
{
    int c, i = bit >> 6;
    c = popcountll(set->bits[i] & (((uint64_t)1 << (bit & 63)) - 1));
    while (i)
        c += popcountll(set->bits[--i]);
    return c;
}

static inline void 
rts_bitset_add(struct bitset *set, int bit)
{
    uint64_t oldval, newval;
    oldval = set->bits[(bit >> 6)];
    newval = oldval | ((uint64_t)1 << (bit & 63));
    set->bits[(bit >> 6)] = newval;
}

static inline void
rts_bitset_del(struct bitset *set, int bit)
{
    uint64_t oldval, newval;
    oldval = set->bits[(bit >> 6)];
    newval = oldval & ~((uint64_t)1 << (bit & 63));
    set->bits[(bit >> 6)] = newval;
}

static inline void
rts_bitset_intersection(struct bitset *r, struct bitset *a, struct bitset *b)
{
    r->bits[0] = a->bits[0] & b->bits[0];
    r->bits[1] = a->bits[1] & b->bits[1];
    r->bits[2] = a->bits[2] & b->bits[2];
    r->bits[3] = a->bits[3] & b->bits[3];
    r->bits[4] = a->bits[4] & b->bits[4];
    r->bits[5] = a->bits[5] & b->bits[5];
    r->bits[6] = a->bits[6] & b->bits[6];
    r->bits[7] = a->bits[7] & b->bits[7];
}

static inline void
rts_bitset_union(struct bitset *r, struct bitset *a, struct bitset *b)
{
    r->bits[0] = a->bits[0] | b->bits[0];
    r->bits[1] = a->bits[1] | b->bits[1];
    r->bits[2] = a->bits[2] | b->bits[2];
    r->bits[3] = a->bits[3] | b->bits[3];
    r->bits[4] = a->bits[4] | b->bits[4];
    r->bits[5] = a->bits[5] | b->bits[5];
    r->bits[6] = a->bits[6] | b->bits[6];
    r->bits[7] = a->bits[7] | b->bits[7];
}

static inline void
rts_bitset_compliment(struct bitset *set)
{
    set->bits[0] = ~set->bits[0];
    set->bits[1] = ~set->bits[1];
    set->bits[2] = ~set->bits[2];
    set->bits[3] = ~set->bits[3];
    set->bits[4] = ~set->bits[4];
    set->bits[5] = ~set->bits[5];
    set->bits[6] = ~set->bits[6];
    set->bits[7] = ~set->bits[7];
}

#endif
