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

#include <memutil.h>
#include <osw_tlv.h>

static size_t
osw_tlv_len(const struct osw_tlv_hdr *hdr)
{
    const size_t hdr_len = osw_tlv_hdr_len();
    const size_t data_len = hdr->len;
    return hdr_len + data_len;
}

bool
osw_tlv_ok(const struct osw_tlv_hdr *hdr, size_t remaining)
{
    if (hdr == NULL) return false;
    return osw_tlv_len(hdr) <= remaining;
}

const struct osw_tlv_hdr *
osw_tlv_next(const struct osw_tlv_hdr *hdr, size_t *remaining)
{
    if (hdr == NULL) return NULL;
    const size_t offset = osw_tlv_len(hdr);
    const size_t padding = OSW_TLV_ALIGN(offset) - offset;
    if (offset > *remaining) return NULL;
    *remaining -= offset;
    if (padding > *remaining) return NULL;
    *remaining -= padding;
    return (const void *)hdr + offset + padding;
}

static bool
osw_tlv_policy_ok(const struct osw_tlv_hdr *hdr,
                  const struct osw_tlv_policy *policy)
{
    static const size_t sizes[] = {
        //[OSW_TLV_S8] = sizeof(int8_t),
        [OSW_TLV_U32] = sizeof(uint32_t),
        //[OSW_TLV_U64] = sizeof(uint64_t),
        [OSW_TLV_FLOAT] = sizeof(float),
        [OSW_TLV_HWADDR] = 6,
    };

    if (policy == NULL) return true;
    policy += hdr->id;

    if (hdr->type != policy->type) return false;

    /* use enum to make compiler verify switch-case exhaustiveness */
    enum osw_tlv_type type = hdr->type;
    switch (type) {
        case OSW_TLV_HWADDR: /* fall-through */
        case OSW_TLV_U32: /* fall-through */
        case OSW_TLV_FLOAT:
            return hdr->len == sizes[type];
        case OSW_TLV_STRING: /* fall-through */
        case OSW_TLV_UNSPEC:
            return ((hdr->len >= policy->min_len) &&
                    (policy->max_len == 0 ||
                     policy->max_len >= hdr->len));
        case OSW_TLV_NESTED:
            return true;
    }
    return false;
}

static void
osw_tlv_grow(struct osw_tlv *tlv, size_t needed)
{
    const size_t remaining = tlv->size - OSW_TLV_ALIGN(tlv->used);
    if (needed <= remaining) return;

    const size_t new_size = tlv->size + OSW_TLV_ALIGN(needed + OSW_TLV_TAILROOM);
    tlv->data = REALLOC(tlv->data, new_size);
    memset(tlv->data + tlv->size, 0, new_size - tlv->size);
    tlv->size = new_size;
}

void *
osw_tlv_reserve(struct osw_tlv *tlv, size_t len)
{
    osw_tlv_grow(tlv, len);
    const size_t aused = OSW_TLV_ALIGN(tlv->used);
    void *data = tlv->data + aused;
    tlv->used = aused + len;
    return data;
}

void
osw_tlv_copy(struct osw_tlv *dst, const struct osw_tlv *src)
{
    osw_tlv_fini(dst);
    memcpy(osw_tlv_reserve(dst, src->used), src->data, src->used);
}

void *
osw_tlv_put(struct osw_tlv *tlv, uint32_t id, enum osw_tlv_type type, size_t len)
{
    struct osw_tlv_hdr *hdr = osw_tlv_reserve(tlv, osw_tlv_hdr_len() + len);
    hdr->id = id;
    hdr->type = type;
    hdr->len = len;
    hdr->flags = 0; // fixme: always provide flags?
    return osw_tlv_get_data(hdr);
}

size_t
osw_tlv_put_off(struct osw_tlv *tlv, uint32_t id, enum osw_tlv_type type, size_t len)
{
    void *data = osw_tlv_put(tlv, id, type, len);
    const size_t off = data - tlv->data;
    return off;
}

struct osw_tlv *
osw_tlv_new(void)
{
    return CALLOC(1, sizeof(struct osw_tlv));
}

void
osw_tlv_fini(struct osw_tlv *tlv)
{
    if (tlv == NULL) return;
    if (tlv->data == NULL) return;

    FREE(tlv->data);
    memset(tlv, 0, sizeof(*tlv));
}

void
osw_tlv_free(struct osw_tlv *tlv)
{
    osw_tlv_fini(tlv);
    FREE(tlv);
}

size_t
osw_tlv_parse(const void *data,
              size_t len,
              const struct osw_tlv_policy *policy,
              const struct osw_tlv_hdr **tb,
              size_t size)
{
    const struct osw_tlv_hdr *hdr;
    osw_tlv_for_each(hdr, data, len) {
        if (hdr->id >= size) continue;
        if (osw_tlv_policy_ok(hdr, policy) == false) continue;
        tb[hdr->id] = hdr;
    }
    return len;
}

const struct osw_tlv_hdr *
osw_tlv_find(const void *data,
             size_t len,
             uint32_t id)
{
    const struct osw_tlv_hdr *hdr;
    osw_tlv_for_each(hdr, data, len) {
        if (hdr->id == id)
            return hdr;
    }
    return NULL;
}

#include "osw_tlv_ut.c.h"
