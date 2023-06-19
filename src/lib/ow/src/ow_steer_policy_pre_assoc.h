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

#ifndef OW_STEER_POLICY_PRE_ASSOC_H
#define OW_STEER_POLICY_PRE_ASSOC_H

enum ow_steer_policy_pre_assoc_reject_condition_type {
    OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_COUNTER = 0,
    OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER,
};

enum ow_steer_policy_pre_assoc_backoff_condition_type {
    OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_NONE = 0,
    OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_THRESHOLD_SNR,
};

struct ow_steer_policy_pre_assoc_reject_condition_counter {
    unsigned int reject_limit;
    unsigned int reject_timeout_sec;
};

struct ow_steer_policy_pre_assoc_reject_condition_timer {
    unsigned int reject_timeout_msec;
};

struct ow_steer_policy_pre_assoc_reject_condition {
    enum ow_steer_policy_pre_assoc_reject_condition_type type;
    union {
        struct ow_steer_policy_pre_assoc_reject_condition_counter counter;
        struct ow_steer_policy_pre_assoc_reject_condition_timer timer;
    } params;
};

struct ow_steer_policy_pre_assoc_backoff_condition_threshold_snr {
    unsigned int threshold_snr;
};

struct ow_steer_policy_pre_assoc_backoff_condition {
    enum ow_steer_policy_pre_assoc_backoff_condition_type type;
    union {
        struct ow_steer_policy_pre_assoc_backoff_condition_threshold_snr threshold_snr;
    } params;
};

struct ow_steer_policy_pre_assoc_config {
    struct osw_hwaddr bssid;
    bool immediate_backoff_on_auth_req;
    unsigned int backoff_timeout_sec;
    unsigned int backoff_exp_base;
    struct ow_steer_policy_pre_assoc_reject_condition reject_condition;
    struct ow_steer_policy_pre_assoc_backoff_condition backoff_condition;
};

struct ow_steer_policy_pre_assoc*
ow_steer_policy_pre_assoc_create(const struct osw_hwaddr *sta_addr,
                                 const struct ow_steer_policy_mediator *mediator);

void
ow_steer_policy_pre_assoc_set_config(struct ow_steer_policy_pre_assoc *counter_policy,
                                     struct ow_steer_policy_pre_assoc_config *config);

void
ow_steer_policy_pre_assoc_free(struct ow_steer_policy_pre_assoc *counter_policy);

struct ow_steer_policy*
ow_steer_policy_pre_assoc_get_base(struct ow_steer_policy_pre_assoc *counter_policy);

const struct osw_hwaddr *
ow_steer_policy_pre_assoc_get_bssid(const struct ow_steer_policy_pre_assoc *counter_policy);

#endif /* OW_STEER_POLICY_PRE_ASSOC_H */
