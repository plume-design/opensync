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

#ifndef OW_STEER_POLICY_H
#define OW_STEER_POLICY_H

#include <osw_diag.h>

struct ow_steer_policy;

typedef void
ow_steer_policy_sigusr1_dump_fn_t(osw_diag_pipe_t *pipe,
                                  struct ow_steer_policy *policy);

typedef void
ow_steer_policy_recalc_fn_t(struct ow_steer_policy *policy,
                            struct ow_steer_candidate_list *candidate_list);

typedef void
ow_steer_policy_sta_snr_change_fn_t(struct ow_steer_policy *policy,
                                    const struct osw_hwaddr *bssid,
                                    uint32_t snr_db);

typedef void
ow_steer_policy_sta_data_vol_change_fn_t(struct ow_steer_policy *policy,
                                         const struct osw_hwaddr *bssid,
                                         uint64_t data_vol_bytes);

typedef void
ow_steer_policy_mediator_sched_recalc_stack_fn_t(struct ow_steer_policy *policy,
                                                 void *mediator_priv);

typedef bool
ow_steer_policy_mediator_trigger_executor_fn_t(struct ow_steer_policy *policy,
                                               void *mediator_priv);

typedef void
ow_steer_policy_mediator_dismiss_executor_fn_t(struct ow_steer_policy *policy,
                                               void *mediator_priv);

typedef void
ow_steer_policy_mediator_notify_backoff_fn_t(struct ow_steer_policy *policy,
                                             void *mediator_priv,
                                             const bool enabled,
                                             const unsigned int period);

typedef void
ow_steer_policy_mediator_notify_steering_attempt_fn_t(struct ow_steer_policy *policy,
                                                      const char *vif_name,
                                                      void *mediator_priv);

struct ow_steer_policy_ops {
    ow_steer_policy_sigusr1_dump_fn_t *sigusr1_dump_fn;
    ow_steer_policy_recalc_fn_t *recalc_fn;

    /* osw stats */
    ow_steer_policy_sta_snr_change_fn_t *sta_snr_change_fn;
    ow_steer_policy_sta_data_vol_change_fn_t *sta_data_vol_change_fn;
};

struct ow_steer_policy_mediator {
    ow_steer_policy_mediator_sched_recalc_stack_fn_t *sched_recalc_stack_fn;
    ow_steer_policy_mediator_trigger_executor_fn_t *trigger_executor_fn;
    ow_steer_policy_mediator_dismiss_executor_fn_t *dismiss_executor_fn;
    ow_steer_policy_mediator_notify_backoff_fn_t *notify_backoff_fn;
    ow_steer_policy_mediator_notify_steering_attempt_fn_t *notify_steering_attempt_fn;
    void *priv;
};

struct ow_steer_policy*
ow_steer_policy_create(const char *name,
                       const struct osw_hwaddr *sta_addr,
                       const struct ow_steer_policy_ops *ops,
                       const struct ow_steer_policy_mediator *mediator,
                       const char *log_prefix,
                       void *priv);

void
ow_steer_policy_free(struct ow_steer_policy *policy);

void*
ow_steer_policy_get_priv(struct ow_steer_policy *policy);

const struct osw_hwaddr*
ow_steer_policy_get_sta_addr(const struct ow_steer_policy *policy);

const struct osw_hwaddr*
ow_steer_policy_get_bssid(const struct ow_steer_policy *policy);

void
ow_steer_policy_set_bssid(struct ow_steer_policy *policy,
                          const struct osw_hwaddr *bssid);

const char*
ow_steer_policy_get_name(const struct ow_steer_policy *policy);

#endif /* OW_STEER_POLICY_H */
