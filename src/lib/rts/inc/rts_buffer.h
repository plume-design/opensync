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

#ifndef RTS_BUFFER_H
#define RTS_BUFFER_H

#include "rts_priv.h"
#include "rts_slob.h"

static inline void
rts_buffer_get(struct rts_buffer *b)
{
    if (b->data)
        b->data->ref++;
}

static inline void
rts_buffer_put(struct rts_buffer *b, struct rts_pool *mp)
{
    if (b->data) {
        rts_assert(b->data->ref);
        if (--(b->data->ref) == 0)
            rts_pool_free(mp, b->data);
        b->data = 0;
        b->off = 0;
        b->len = 0;
        b->cap = 0;
    }
}

static inline bool
rts_buffer_will_sync(struct rts_buffer *b)
{
    return b->data && b->data->data != b->data->memory;
}

static inline void
rts_buffer_init_data(struct rts_buffer *b, struct rts_buffer_data *data, unsigned off, unsigned len)
{
    b->data = data;
    b->off = off;
    b->len = len;
    b->cap = 0;
    rts_buffer_get(b);
}

static inline void
rts_buffer_init(struct rts_buffer *b)
{
    b->data = 0;
    b->off = 0;
    b->len = 0;
    b->cap = 0;
}

static inline void
rts_buffer_exit(struct rts_buffer *b, struct rts_pool *mp)
{
    rts_buffer_put(b, mp);
}

static inline size_t
rts_buffer_size(const struct rts_buffer *b)
{
    return b->len;
}

static inline bool
rts_buffer_empty(const struct rts_buffer *b)
{
    return b->len == 0;
}

static inline size_t
rts_buffer_capacity(const struct rts_buffer *b)
{
    if (b->data) {
        rts_assert(b->cap >= sizeof(*b->data));
        return b->cap - sizeof(*b->data);
    }
    rts_assert(b->cap == 0);
    return 0;
}

static inline bool
rts_buffer_shared(const struct rts_buffer *b)
{
    return b->data && b->data->ref > 1;
}

static inline unsigned char *
rts_buffer_data(const struct rts_buffer *b, size_t iter)
{
    return b->data ? &b->data->data[b->off + iter] : NULL;
}

static inline unsigned char
rts_buffer_at(const struct rts_buffer *b, size_t iter)
{
    return b->data->data[b->off + iter];
}

static inline bool
rts_buffer_eql(const struct rts_buffer *lhs, const struct rts_buffer *rhs)
{
    if (lhs->data == rhs->data)
        return true;

    if (rts_buffer_empty(lhs) || rts_buffer_empty(rhs))
        return false;

    return lhs->len == rhs->len &&
        !__builtin_memcmp(lhs->data->data + lhs->off, rhs->data->data + rhs->off, rhs->len);
}

static inline bool
rts_buffer_neq(const struct rts_buffer *lhs, const struct rts_buffer *rhs)
{
    return !rts_buffer_eql(lhs, rhs);
}

bool rts_buffer_reserve(struct rts_buffer *dst, size_t size, struct rts_pool *mp);
bool rts_buffer_append(struct rts_buffer *dst, struct rts_buffer *src, struct rts_pool *mp);
bool rts_buffer_push(struct rts_buffer *dst, unsigned char byte, struct rts_pool *mp);
void rts_buffer_copy(struct rts_buffer *dst, struct rts_buffer *src, struct rts_pool *mp);
bool rts_buffer_sync(struct rts_buffer *dst, struct rts_pool *mp);
bool rts_buffer_write(struct rts_buffer *dst, const void *src, size_t len, struct rts_pool *mp);
void rts_buffer_clear(struct rts_buffer *dst, struct rts_pool *mp);
bool rts_buffer_clone(struct rts_buffer *dst, const struct rts_buffer *src, struct rts_pool *mp);

#endif
