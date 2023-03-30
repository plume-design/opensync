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

#ifndef OSW_TLV_H_INCLUDED
#define OSW_TLV_H_INCLUDED

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <osw_types.h>

#define OSW_TLV_TAILROOM 4096
#define OSW_TLV_MASK (sizeof(uint32_t) - 1)
#define OSW_TLV_ALIGN_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define OSW_TLV_ALIGN(x) OSW_TLV_ALIGN_MASK(x, OSW_TLV_MASK)
#define OSW_TLV_PTR_ALIGNED(x) assert(((intptr_t)(x) & OSW_TLV_MASK) == 0)

struct osw_tlv {
    void *data;
    size_t size;
    size_t used;
};

enum osw_tlv_type {
    OSW_TLV_UNSPEC,
    OSW_TLV_U32,
    OSW_TLV_FLOAT,
    OSW_TLV_STRING,
    OSW_TLV_NESTED,
    OSW_TLV_HWADDR,
    // OSW_TLV_U64
    // OSW_TLV_S8
    //OSW_TLV_TYPES,
};

//#define OSW_TLV_F_WHITEOUT (1 << 0)
#define OSW_TLV_F_DELTA (1 << 0)

struct osw_tlv_hdr {
    uint32_t id;
    uint32_t type; /* osw_tlv_type */
    uint32_t len;
    uint32_t flags;
} __attribute__((packed));

struct osw_tlv_policy {
    enum osw_tlv_type type;
    uint32_t min_len;
    uint32_t max_len;
    const struct osw_tlv_policy *nested;
    size_t tb_size;
};

struct osw_tlv *osw_tlv_new(void);
void osw_tlv_copy(struct osw_tlv *dst, const struct osw_tlv *src);
void osw_tlv_free(struct osw_tlv *tlv);
void osw_tlv_fini(struct osw_tlv *tlv);
void *osw_tlv_reserve(struct osw_tlv *tlv, size_t len);
void *osw_tlv_put(struct osw_tlv *tlv, uint32_t id, enum osw_tlv_type type, size_t len);
size_t osw_tlv_put_off(struct osw_tlv *tlv, uint32_t id, enum osw_tlv_type type, size_t len);
size_t osw_tlv_parse(const void *data, size_t len,
                     const struct osw_tlv_policy *policy,
                     const struct osw_tlv_hdr **tb,
                     size_t size);
const struct osw_tlv_hdr *osw_tlv_find(const void *data, size_t len, uint32_t id);
// non-const find, or macro?

#define osw_tlv_hdr_len() OSW_TLV_ALIGN(sizeof(struct osw_tlv_hdr))

static inline void *
osw_tlv_get_data(const struct osw_tlv_hdr *hdr)
{
    OSW_TLV_PTR_ALIGNED(hdr);
    return (void *)hdr + OSW_TLV_ALIGN(sizeof(*hdr));
}

static inline struct osw_tlv_hdr *
osw_tlv_get_hdr(const void *data)
{
    OSW_TLV_PTR_ALIGNED(data);
    return (void *)data - OSW_TLV_ALIGN(sizeof(struct osw_tlv_hdr));
}

static inline struct osw_tlv_hdr *
osw_tlv_get_hdr_off(struct osw_tlv *t, size_t off)
{
    void *data = t->data + off;
    OSW_TLV_PTR_ALIGNED(data);
    return (void *)data - OSW_TLV_ALIGN(sizeof(struct osw_tlv_hdr));
}

static inline void *
osw_tlv_set_flags(void *data, const uint32_t flags)
{
    osw_tlv_get_hdr(data)->flags = flags;
    return data;
}

static inline void
osw_tlv_reset(struct osw_tlv *t)
{
    t->used = 0;
    memset(t->data, 0, t->size);
}

const struct osw_tlv_hdr *osw_tlv_next(const struct osw_tlv_hdr *hdr, size_t *remaining);
bool osw_tlv_ok(const struct osw_tlv_hdr *hdr, size_t remaining);

#define osw_tlv_get_u32(hdr) (*(const uint32_t *)osw_tlv_get_data(hdr))
#define osw_tlv_get_float(hdr) (*(const float *)osw_tlv_get_data(hdr))
#define osw_tlv_get_string(hdr) ((const char *)osw_tlv_get_data(hdr))
#define osw_tlv_get_hwaddr(addr, hdr) memcpy((addr)->octet, osw_tlv_get_data(hdr), sizeof((addr)->octet))

#define osw_tlv_put_var(tlv, id, type, ctype, x) memcpy(osw_tlv_put(tlv, id, type, sizeof(ctype)), (ctype[]){x}, sizeof(ctype))
#define osw_tlv_put_u32(tlv, id, x) osw_tlv_put_var(tlv, id, OSW_TLV_U32, uint32_t, x)
#define osw_tlv_put_u32_delta(tlv, id, x) osw_tlv_set_flags(osw_tlv_put_u32(tlv, id, x), OSW_TLV_F_DELTA)
#define osw_tlv_put_float(tlv, id, x) osw_tlv_put_var(tlv, id, OSW_TLV_FLOAT, float, x)
#define osw_tlv_put_float_delta(tlv, id, x) osw_tlv_set_flags(osw_tlv_put_float(tlv, id, x), OSW_TLV_F_DELTA)
#define osw_tlv_put_data(tlv, id, type, x, s) memcpy(osw_tlv_put(tlv, id, type, (s)), (x), (s))
#define osw_tlv_put_buf(tlv, id, x, s) osw_tlv_put_data(tlv, id, OSW_TLV_UNSPEC, x, s)
#define osw_tlv_put_string(tlv, id, x) osw_tlv_put_data(tlv, id, OSW_TLV_STRING, x, strlen(x) + 1)
#define osw_tlv_put_hwaddr(tlv, id, x) osw_tlv_put_data(tlv, id, OSW_TLV_HWADDR, (x)->octet, sizeof((x)->octet))
#define osw_tlv_put_nested(tlv, id) osw_tlv_put_off(tlv, id, OSW_TLV_NESTED, 0)
#define osw_tlv_end_nested(tlv, off) (osw_tlv_get_hdr_off(tlv, off)->len = (tlv)->used - (off))
#define osw_tlv_put_copy(tlv, hdr) memcpy(osw_tlv_put(tlv, (hdr)->id, (hdr)->type, (hdr)->len), (hdr) + 1, (hdr)->len)
#define osw_tlv_put_same(tlv, hdr, x, s)  memcpy(osw_tlv_put(tlv, (hdr)->id, (hdr)->type, s), x, s)

#define OSW_TLV_PARSE(tlv, policy, tb, size) osw_tlv_parse((tlv)->data, (tlv)->used, policy, tb, size)
#define OSW_TLV_FIND(tlv, id) osw_tlv_find((tlv)->data, (tlv)->used, id)

#define osw_tlv_for_each(i, head, rem) \
    for (i = osw_tlv_ok(head, rem) ? head : NULL; \
         osw_tlv_ok(i, rem); \
         i = osw_tlv_next(i, &(rem)))

#endif /* OSW_TLV_H_INCLUDED */
