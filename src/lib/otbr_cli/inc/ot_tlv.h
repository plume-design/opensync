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

#ifndef OT_TLV_H_INCLUDED
#define OT_TLV_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "openthread/netdiag.h"

/** Structure representing Thread TLV encoded value */
typedef struct ot_tlv_s ot_tlv_t;

/** Get the type of the Thread TLV value */
uint8_t ot_tlv_get_type(const ot_tlv_t *tlv) __attribute__((__const__, __nonnull__(1)));

/**
 * Get the next Thread TLV structure located in the buffer of TLV structures
 *
 * @param[in,out] buffer      Pointer to the array of the TLV structures.
 *                            Will be moved to point to the byte after the returned TLV structure.
 *                            Note that this can point to the byte after the last TLV, out of the
 *                            buffer bounds, when this functions sets `*buffer_len` to 0.
 * @param[in,out] buffer_len  Length of the (remaining) TLV structures in `*buffer`.
 *                            Will be decremented by the length of the returned TLV structure,
 *                            if this function returns non-NULL, down to 0 when there are no
 *                            more TLV structures in the buffer. Additionally, if the buffer
 *                            contains an invalid TLV structure, this function will set
 *                            `*buffer_len` to 0 to indicate that the buffer is invalid.
 *
 * @return Pointer to the TLV structure located at `*buffer` passed to this function, or NULL
 *         if there are no more TLV structures or the buffer is invalid (logged internally).
 */
const ot_tlv_t *ot_tlv_get_next(const uint8_t **buffer, size_t *buffer_len) __attribute__((__nonnull__(1, 2)));

/**
 * Parse Thread Network Diagnostic TLV
 *
 * @param[in]  raw_tlv  Pointer to the raw TLV structure.
 * @param[out] tlv      Pointer to the structure to fill with the parsed TLV data.
 *
 * @return true if the TLV structure was parsed successfully, false otherwise (logged internally).
 */
bool ot_tlv_parse_network_diagnostic_tlv(const ot_tlv_t *raw_tlv, otNetworkDiagTlv *tlv);

#endif /* OT_TLV_H_INCLUDED */
