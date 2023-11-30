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

#ifndef OW_STEER_POLICY_SNR_LEVEL_H_INCLUDED
#define OW_STEER_POLICY_SNR_LEVEL_H_INCLUDED

struct ow_steer_policy_snr_level;

enum ow_steer_policy_snr_level_mode {
    OW_STEER_POLICY_SNR_LEVEL_BLOCK_FROM_BSSIDS_WHEN_ABOVE,
    OW_STEER_POLICY_SNR_LEVEL_BLOCK_FROM_BSSIDS_WHEN_BELOW,
};

struct ow_steer_policy_snr_level *
ow_steer_policy_snr_level_alloc(const char *name,
                                const struct osw_hwaddr *sta_addr,
                                enum ow_steer_policy_snr_level_mode mode,
                                const struct ow_steer_policy_mediator *mediator);

void
ow_steer_policy_snr_level_free(struct ow_steer_policy_snr_level *policy);

struct ow_steer_policy *
ow_steer_policy_snr_level_get_base(struct ow_steer_policy_snr_level *policy);

void
ow_steer_policy_snr_level_set_to_bssids(struct ow_steer_policy_snr_level *policy,
                                        const struct osw_hwaddr_list *to_bssids);

void
ow_steer_policy_snr_level_set_from_bssids(struct ow_steer_policy_snr_level *policy,
                                          const struct osw_hwaddr_list *from_bssids);

void
ow_steer_policy_snr_level_set_sta_threshold_snr(struct ow_steer_policy_snr_level *policy,
                                                const uint32_t *snr);

void
ow_steer_policy_snr_level_set_sta_threshold_bytes(struct ow_steer_policy_snr_level *policy,
                                                  const uint64_t *bytes);

#endif /* OW_STEER_POLICY_SNR_LEVEL_H_INCLUDED */
