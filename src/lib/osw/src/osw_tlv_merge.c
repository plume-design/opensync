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

/* libc */
#include <string.h>

/* opensync */
#include <log.h>

/* osw */
#include <osw_tlv_merge.h>

struct osw_tlv_merge_op {
    struct osw_tlv *dest_tlv;
    struct osw_tlv *prev_tlv;
    const struct osw_tlv_hdr **dest;
    const struct osw_tlv_hdr **prev;
    const struct osw_tlv_hdr *src;
    const bool diff_on_first;
    const struct osw_tlv_policy *tpolicy;
    const struct osw_tlv_merge_policy *mpolicy;
};

typedef void osw_tlv_merge_add_fn_t(void *d, const void *s);
typedef void osw_tlv_merge_sub_fn_t(void *d, const void *s);
typedef void osw_tlv_merge_op_fn_t(const struct osw_tlv_merge_op *op);

void
osw_tlv_merge_repack(struct osw_tlv *dst,
                     const struct osw_tlv_hdr **tb,
                     size_t tb_size)
{
    struct osw_tlv tmp = {0};
    size_t i;
    for (i = 0; i < tb_size; i++) {
        if (tb[i] == NULL) continue;
        osw_tlv_put_copy(&tmp, tb[i]);
    }
    osw_tlv_fini(dst);
    *dst = tmp;
}

void
osw_tlv_merge_clone(struct osw_tlv *dst,
                    const struct osw_tlv_hdr *src)
{
    if (src == NULL) return;
    assert(dst->used == 0);
    memcpy(osw_tlv_reserve(dst, src->len), osw_tlv_get_data(src), src->len);
}

void
osw_tlv_merge_replace(struct osw_tlv *dst,
                      const struct osw_tlv_hdr **hdr,
                      const struct osw_tlv_hdr *ref,
                      struct osw_tlv *src)
{
    if (hdr[0] == NULL) {
        if (src->used > 0) {
            hdr[0] = osw_tlv_put_same(dst, ref, src->data, src->used);
            hdr[0]--;
        }
    }
    else {
        if (hdr[0]->len != src->used) {
            hdr[0] = osw_tlv_put_same(dst, ref, src->data, src->used);
            hdr[0]--;
        }
        else {
            memcpy(osw_tlv_get_data(hdr[0]), src->data, src->used);
        }
    }
    osw_tlv_fini(src);
}

static void
osw_tlv_merge_op_none_cb(const struct osw_tlv_merge_op *op)
{
    (void)op;
}

static void
osw_tlv_merge_op_overwrite_cb(const struct osw_tlv_merge_op *op)
{
    if (*op->dest == NULL || (*op->dest)->len != op->src->len) {
        void *data = osw_tlv_put(op->dest_tlv, op->src->id, op->src->type, op->src->len);
        *op->dest = osw_tlv_get_hdr(data);
    }
    memcpy(osw_tlv_get_data(*op->dest),
           osw_tlv_get_data(op->src),
           op->src->len);
}

static void
osw_stats_type_u32_add_cb(void *d, const void *s)
{
    *(uint32_t *)d = *(uint32_t *)d + *(uint32_t *)s;
}

static void
osw_stats_type_u32_sub_cb(void *d, const void *s)
{
    *(uint32_t *)d = *(uint32_t *)d - *(uint32_t *)s;
}

static void
osw_stats_type_float_add_cb(void *d, const void *s)
{
    *(float *)d = *(float *)d + *(float *)s;
}

static void
osw_stats_type_float_sub_cb(void *d, const void *s)
{
    *(float *)d = *(float *)d - *(float *)s;
}

static osw_tlv_merge_add_fn_t *
osw_tlv_merge_add_fn_lookup(const enum osw_tlv_type t)
{
    switch (t) {
        case OSW_TLV_U32: return osw_stats_type_u32_add_cb;
        case OSW_TLV_FLOAT: return osw_stats_type_float_add_cb;
        case OSW_TLV_HWADDR: return NULL;
        case OSW_TLV_STRING: return NULL;
        case OSW_TLV_UNSPEC: return NULL;
        case OSW_TLV_NESTED: return NULL;
    }
    assert(0);
    return NULL;
}

static osw_tlv_merge_sub_fn_t *
osw_tlv_merge_sub_fn_lookup(const enum osw_tlv_type t)
{
    switch (t) {
        case OSW_TLV_U32: return osw_stats_type_u32_sub_cb;
        case OSW_TLV_FLOAT: return osw_stats_type_float_sub_cb;
        case OSW_TLV_HWADDR: return NULL;
        case OSW_TLV_STRING: return NULL;
        case OSW_TLV_UNSPEC: return NULL;
        case OSW_TLV_NESTED: return NULL;
    }
    assert(0);
    return NULL;
}

static void
osw_tlv_merge_op_accumulate_cb(const struct osw_tlv_merge_op *op)
{
    const bool need_diff = ((op->src->flags & OSW_TLV_F_DELTA) == 0);
    const bool skip_null = (need_diff == true && op->diff_on_first == false);
    const enum osw_tlv_type type = op->src->type;
    osw_tlv_merge_add_fn_t *add_fn = osw_tlv_merge_add_fn_lookup(type);
    osw_tlv_merge_sub_fn_t *sub_fn = osw_tlv_merge_sub_fn_lookup(type);

    if (WARN_ON(add_fn == NULL)) return;
    if (WARN_ON(sub_fn == NULL)) return;
    if (op->prev[0] == NULL && skip_null == true) goto store;
    if (op->dest[0] == NULL || op->dest[0]->len != op->src->len) {
        void *data = osw_tlv_put(op->dest_tlv, op->src->id, op->src->type, op->src->len);
        op->dest[0] = osw_tlv_get_hdr(data);
        memset(data, 0, op->src->len);
    }
    const void *s = osw_tlv_get_data(op->src);
    const void *p = op->prev[0] ? osw_tlv_get_data(*op->prev) : NULL;
    void *d = osw_tlv_get_data(op->dest[0]);
    add_fn(d, s);
    if (p != NULL) sub_fn(d, p);
store:
    if (need_diff == false) return;
    if (op->prev[0] == NULL) {
        op->prev[0] = osw_tlv_get_hdr(osw_tlv_put_copy(op->prev_tlv, op->src));
    }
    else {
        memcpy(osw_tlv_get_data(op->prev[0]),
               osw_tlv_get_data(op->src),
               op->src->len);
    }
}

#if 0
static void
osw_tlv_merge_op_average_cb(const struct osw_tlv_merge_op *op)
{
}
#endif

static bool
osw_tlv_merge_first_to_bool(const bool inherited,
                            const enum osw_tlv_merge_first_policy f)
{
    switch (f) {
        case OSW_TLV_INHERIT_FIRST: return inherited;
        case OSW_TLV_DELTA_AGAINST_ZERO: return true;
        case OSW_TLV_TWO_SAMPLES_MINIMUM: return false;
    }
    assert(0);
    return true;
}

static void
osw_tlv_merge_op_merge_cb(const struct osw_tlv_merge_op *op)
{
    const uint32_t id = op->src->id;
    const enum osw_tlv_type type = op->src->type;

    if (op->tpolicy == NULL) return;
    if (op->mpolicy == NULL) return;
    if (op->tpolicy[id].tb_size == 0) return;
    if (op->tpolicy[id].tb_size != op->mpolicy[id].tb_size) return;
    if (op->mpolicy[id].nested == NULL) return;
    if (WARN_ON(type != OSW_TLV_NESTED)) return;

    const struct osw_tlv_policy *tpolicy = op->tpolicy[id].nested;
    const struct osw_tlv_merge_policy *mpolicy = op->mpolicy[id].nested;
    const size_t tb_size = op->tpolicy[id].tb_size;
    const bool diff_on_first = osw_tlv_merge_first_to_bool(op->diff_on_first,
                                                           op->mpolicy[id].first);
    struct osw_tlv dest_tmp = {0};
    struct osw_tlv prev_tmp = {0};
    osw_tlv_merge_clone(&dest_tmp, *op->dest);
    osw_tlv_merge_clone(&prev_tmp, *op->prev);

    osw_tlv_merge(&dest_tmp,
                  &prev_tmp,
                  osw_tlv_get_data(op->src),
                  op->src->len,
                  diff_on_first,
                  tpolicy,
                  mpolicy,
                  tb_size);

    osw_tlv_merge_replace(op->dest_tlv, op->dest, op->src, &dest_tmp);
    osw_tlv_merge_replace(op->prev_tlv, op->prev, op->src, &prev_tmp);
}

static osw_tlv_merge_op_fn_t *
osw_tlv_merge_op_fn_lookup(enum osw_tlv_merge_op_type t)
{
    switch (t) {
        case OSW_TLV_OP_NONE: return osw_tlv_merge_op_none_cb;
        case OSW_TLV_OP_OVERWRITE: return osw_tlv_merge_op_overwrite_cb;
        case OSW_TLV_OP_ACCUMULATE: return osw_tlv_merge_op_accumulate_cb;
        case OSW_TLV_OP_MERGE: return osw_tlv_merge_op_merge_cb;
    }
    assert(0);
    return NULL;
}

void
osw_tlv_merge(struct osw_tlv *dest_tlv,
              struct osw_tlv *prev_tlv,
              const void *data,
              const size_t len,
              const bool diff_on_first, // FIXME: invert the logic to skip_first_absolute
              const struct osw_tlv_policy *tpolicy,
              const struct osw_tlv_merge_policy *mpolicy,
              const size_t tb_size)
{
    const struct osw_tlv_hdr *dtb[tb_size];
    const struct osw_tlv_hdr *ntb[tb_size];
    const struct osw_tlv_hdr *ptb[tb_size];
    size_t i;
    bool need_dest_repack = false;
    bool need_prev_repack = false;

    if (mpolicy == NULL) return;

    memset(dtb, 0, tb_size * sizeof(*dtb));
    memset(ntb, 0, tb_size * sizeof(*ntb));
    memset(ptb, 0, tb_size * sizeof(*ptb));

    osw_tlv_parse(dest_tlv->data, dest_tlv->used, tpolicy, dtb, tb_size);
    osw_tlv_parse(prev_tlv->data, prev_tlv->used, tpolicy, ptb, tb_size);
    osw_tlv_parse(data, len, tpolicy, ntb, tb_size);

    /* FIXME: This should check if OSW_TLV_OP_ACCUMULATE
     * tags are overflowing in an unexpected way. IOW if
     * something seems to have started to report counters
     * "from zero" then the "last" cache needs to be dropped
     * and the delta from absolute values needs to be
     * counted from scratch on _next_ sample. This would
     * handle things like STA connects gracefully and
     * automatically in most cases.
    */

    for (i = 0; i < tb_size; i++) {
        if (ntb[i] == NULL) continue;
        if (ptb[i] != NULL && ptb[i]->type != ntb[i]->type) continue;

        if (dtb[i] != NULL &&
            (dtb[i]->type != ntb[i]->type ||
             dtb[i]->len != ntb[i]->len)) {
            dtb[i] = NULL;
            need_dest_repack = true;
        }

        const bool first = osw_tlv_merge_first_to_bool(diff_on_first,
                                                       mpolicy[i].first);
        const enum osw_tlv_merge_op_type optype = mpolicy[i].type;
        const struct osw_tlv_hdr *old_dest = dtb[i];
        const struct osw_tlv_hdr *old_prev = ptb[i];
        const struct osw_tlv_merge_op op = {
            .dest_tlv = dest_tlv,
            .prev_tlv = prev_tlv,
            .dest = &dtb[i],
            .diff_on_first = first,
            .src = ntb[i],
            .prev = &ptb[i],
            .tpolicy = tpolicy,
            .mpolicy = mpolicy,
        };
        osw_tlv_merge_op_fn_t *op_fn = osw_tlv_merge_op_fn_lookup(optype);

        if (WARN_ON(op_fn == NULL)) continue;
        op_fn(&op);

        if (old_dest != dtb[i]) need_dest_repack = true;
        if (old_prev != ptb[i]) need_prev_repack = true;
    }

    if (need_dest_repack == true)
        osw_tlv_merge_repack(dest_tlv, dtb, tb_size);

    if (need_prev_repack == true)
        osw_tlv_merge_repack(prev_tlv, ptb, tb_size);
}

#include "osw_tlv_merge_ut.c.h"
