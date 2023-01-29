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

#ifndef OSW_TLV_MERGE_H_INCLUDED
#define OSW_TLV_MERGE_H_INCLUDED

/**
 * Provides helpers to aggregate osw tlv payloads. Intended
 * mostly for statistics.
 */

#include <osw_tlv.h>

enum osw_tlv_merge_op_type {
    OSW_TLV_OP_NONE,
    OSW_TLV_OP_OVERWRITE,
    // TODO: OSW_TLV_OP_AVERAGE
    OSW_TLV_OP_ACCUMULATE,
    OSW_TLV_OP_MERGE,
};

enum osw_tlv_merge_first_policy {
    OSW_TLV_INHERIT_FIRST,
    OSW_TLV_DELTA_AGAINST_ZERO,
    OSW_TLV_TWO_SAMPLES_MINIMUM,
};

struct osw_tlv_merge_policy {
    enum osw_tlv_merge_op_type type;
    enum osw_tlv_merge_first_policy first;
    const struct osw_tlv_merge_policy *nested;
    size_t tb_size;
};

void
osw_tlv_merge(struct osw_tlv *dest_tlv,
              struct osw_tlv *prev_tlv,
              const void *data,
              size_t len,
              bool diff_on_first,
              const struct osw_tlv_policy *tpolicy,
              const struct osw_tlv_merge_policy *mpolicy,
              const size_t tb_size);

#endif /* OSW_TLV_MERGE_H_INCLUDED */
