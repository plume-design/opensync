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

#include "rts_buffer.h"

static bool
rts_buffer_realloc(struct rts_buffer *b, size_t size, struct rts_pool *mp)
{
    size_t alloc_size;
    struct rts_buffer_data *data;

    size += sizeof(*data);

    if (!b->data) {
        data = rts_pool_realloc(mp, NULL, size, &alloc_size);
        if (!data)
            return false;
        data->ref = 1;
    } else if (rts_buffer_shared(b)) {
        unsigned short len = b->len;
        data = rts_pool_realloc(mp, NULL, size, &alloc_size);
        if (!data)
            return false;
        __builtin_memcpy(data->memory, b->data->data + b->off, b->len);
        data->ref = 1;
        rts_buffer_put(b, mp);
        b->len = len;
    } else {
        rts_assert(b->data->data == b->data->memory);
        data = rts_pool_realloc(mp, b->data, size, &alloc_size);
        if (!data)
            return false;
    }

    rts_assert(size <= alloc_size);

    data->data = data->memory;

    b->cap = alloc_size;
    b->data = data;
    return true;
}

bool
rts_buffer_reserve(struct rts_buffer *b, size_t size, struct rts_pool *mp)
{
    return rts_buffer_realloc(b, size, mp);
}

static inline bool
rts_buffer_push_capacity(struct rts_buffer *b, struct rts_pool *mp)
{
    if (b->data == 0)
        return rts_buffer_reserve(b, 1, mp);

    if (b->len + 1 > (int)rts_buffer_capacity(b))
        return rts_buffer_reserve(b, b->len + 1, mp);
        
    return true;
}

bool
rts_buffer_clone(struct rts_buffer *dst, const struct rts_buffer *src, struct rts_pool *mp)
{
    rts_buffer_put(dst, mp);

    if (rts_buffer_empty(src))
        return true;

    if (!rts_buffer_realloc(dst, rts_buffer_size(src), mp))
        return false;

    __builtin_memcpy(dst->data->data + dst->off, src->data->data + src->off, src->len);
    dst->len = src->len;
    return true;
}

bool
rts_buffer_write(struct rts_buffer *dst, const void *src, size_t len, struct rts_pool *mp)
{
    if (len == 0)
        return true;

    if (rts_buffer_shared(dst) || (rts_buffer_capacity(dst) < rts_buffer_size(dst) + len)) {
        if (!rts_buffer_reserve(dst, rts_buffer_size(dst) + len, mp))
            return false;
    }
    __builtin_memcpy(&dst->data->data[dst->len], src, len);
    dst->len += len;
    return true;
}

bool
rts_buffer_append(struct rts_buffer *dst, struct rts_buffer *src, struct rts_pool *mp)
{
    if (rts_buffer_empty(src)) {
        return true;

    } else if (rts_buffer_empty(dst)) {
        rts_buffer_copy(dst, src, mp);
        return true;
    }

    if (rts_buffer_shared(dst)) {
        if (!rts_buffer_reserve(dst, dst->len + src->len, mp))
            return false;
    } else if (rts_buffer_capacity(dst) <= dst->len + src->len) {
        if (!rts_buffer_reserve(dst, dst->len + src->len, mp))
            return false;
    }

    rts_assert(dst->off == 0);

    __builtin_memcpy(&dst->data->data[dst->len], src->data->data + src->off, src->len);
    dst->len += src->len;
    return true;
}

bool
rts_buffer_push(struct rts_buffer *b, unsigned char byte, struct rts_pool *mp)
{
    if (rts_buffer_shared(b) && !rts_buffer_reserve(b, b->len + 1, mp))
        return false;

    else if (!rts_buffer_push_capacity(b, mp))
        return false;

    b->data->data[b->len++] = byte;
    return true;
}

bool
rts_buffer_sync(struct rts_buffer *dst, struct rts_pool *mp)
{
    unsigned len = dst->len;
    struct rts_buffer sync;
    rts_buffer_init(&sync);
    if (!rts_buffer_reserve(&sync, dst->len, mp))
        return false;
    __builtin_memcpy(sync.data->data, &dst->data->data[dst->off], dst->len);
    rts_buffer_put(dst, mp);
    dst->len = len;
    dst->off = 0;
    dst->cap = sync.cap;
    dst->data = sync.data;
    return true;
}

void
rts_buffer_copy(struct rts_buffer *dst, struct rts_buffer *src, struct rts_pool *mp)
{
    rts_buffer_put(dst, mp);
    rts_buffer_get(src);
    dst->data = src->data;
    dst->cap = src->cap;
    dst->len = src->len;
    dst->off = src->off;
}

void
rts_buffer_clear(struct rts_buffer *b, struct rts_pool *mp)
{
    if (rts_buffer_shared(b)) {
        rts_buffer_put(b, mp);
        rts_buffer_init(b);
    } else if (!rts_buffer_empty(b)) {
        b->len = 0;
        b->off = 0;
    }
}
