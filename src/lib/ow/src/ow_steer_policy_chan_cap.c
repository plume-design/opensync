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

#include <log.h>
#include <memutil.h>

#include <osw_module.h>
#include <osw_types.h>
#include <osw_bss_map.h>
#include <osw_sta_chan_cap.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_i.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_policy_chan_cap.h"

#define LOG_PREFIX(policy, fmt, ...) \
    "%s: " fmt, \
    ow_steer_policy_get_prefix((policy)->base), \
    ## __VA_ARGS__

struct ow_steer_policy_chan_cap {
    struct ow_steer_policy *base;
    osw_sta_chan_cap_obs_t *obs;
};

static void
ow_steer_policy_chan_cap_recalc_cb(struct ow_steer_policy *base,
                                   struct ow_steer_candidate_list *candidate_list)
{
    struct ow_steer_policy_chan_cap *policy = ow_steer_policy_get_priv(base);
    const struct osw_hwaddr *sta_addr = ow_steer_policy_get_sta_addr(policy->base);
    const size_t n = ow_steer_candidate_list_get_length(candidate_list);
    size_t i;
    for (i = 0; i < n; i++) {
        struct ow_steer_candidate *c = ow_steer_candidate_list_get(candidate_list, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(c);
        const struct osw_channel *ch = osw_bss_get_channel(bssid);
        if (ch == NULL) continue;
        const enum osw_sta_chan_cap_status status = osw_sta_chan_cap_supports(policy->obs, sta_addr, ch->control_freq_mhz);
        switch (status) {
            case OSW_STA_CHAN_CAP_MAYBE:
            case OSW_STA_CHAN_CAP_SUPPORTED:
                break;
            case OSW_STA_CHAN_CAP_NOT_SUPPORTED:
                {
                    const enum ow_steer_candidate_preference p = ow_steer_candidate_get_preference(c);
                    const char *p_str = ow_steer_candidate_preference_to_cstr(p);
                    const char *reason = ow_steer_policy_get_name(base);
                    switch (p) {
                        case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
                        case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
                            LOGT(LOG_PREFIX(policy, OSW_HWADDR_FMT" is not reachable on "
                                                    OSW_CHANNEL_FMT", but is marked as %s already, should work fine",
                                                    OSW_HWADDR_ARG(bssid),
                                                    OSW_CHANNEL_ARG(ch),
                                                    p_str));
                            break;
                        case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
                        case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                            LOGI(LOG_PREFIX(policy, OSW_HWADDR_FMT" is not reachable on "
                                                    OSW_CHANNEL_FMT", but is marked as %s already, expect issues",
                                                    OSW_HWADDR_ARG(bssid),
                                                    OSW_CHANNEL_ARG(ch),
                                                    p_str));
                            break;
                        case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                            LOGT(LOG_PREFIX(policy, OSW_HWADDR_FMT" is not reachable on "
                                                    OSW_CHANNEL_FMT", marking as out-of-scope",
                                                    OSW_HWADDR_ARG(bssid),
                                                    OSW_CHANNEL_ARG(ch)));
                            ow_steer_candidate_set_preference(c, reason, OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);
                            break;
                    }
                }
                break;
        }
    }
}

static void
ow_steer_policy_chan_cap_changed_cb(void *priv,
                                    const struct osw_hwaddr *sta_addr,
                                    int freq_mhz)
{
    struct ow_steer_policy_chan_cap *policy = priv;
    ow_steer_policy_schedule_stack_recalc(policy->base);
}

static void
ow_steer_policy_chan_cap_sigusr1_dump_freq_cb(void *priv,
                                              const struct osw_hwaddr *sta_addr,
                                              int freq_mhz)
{
    osw_diag_pipe_t *pipe = priv;
    osw_diag_pipe_writef(pipe, "ow: steer:          - %dMHz", freq_mhz);
}

static void
ow_steer_policy_chan_cap_sigusr1_dump_cb(osw_diag_pipe_t *pipe,
                                         struct ow_steer_policy *base)
{
    static const struct osw_sta_chan_cap_ops ops = {
        .added_fn = ow_steer_policy_chan_cap_sigusr1_dump_freq_cb,
    };
    const struct osw_hwaddr *sta_addr = ow_steer_policy_get_sta_addr(base);
    osw_sta_chan_cap_t *m = OSW_MODULE_LOAD(osw_sta_chan_cap);

    osw_diag_pipe_writef(pipe, "ow: steer:         freqs:");
    osw_sta_chan_cap_obs_t *obs = osw_sta_chan_cap_register(m, sta_addr, &ops, pipe);
    osw_sta_chan_cap_obs_unregister(obs);
}

struct ow_steer_policy *
ow_steer_policy_chan_cap_get_base(struct ow_steer_policy_chan_cap *policy)
{
    return policy->base;
}

struct ow_steer_policy_chan_cap *
ow_steer_policy_chan_cap_alloc(const char *name,
                               const struct osw_hwaddr *sta_addr,
                               const struct ow_steer_policy_mediator *mediator)
{
    static const struct ow_steer_policy_ops policy_ops = {
        .sigusr1_dump_fn = ow_steer_policy_chan_cap_sigusr1_dump_cb,
        .recalc_fn = ow_steer_policy_chan_cap_recalc_cb,
    };
    static const struct osw_sta_chan_cap_ops cap_ops = {
        .added_fn = ow_steer_policy_chan_cap_changed_cb,
        .removed_fn = ow_steer_policy_chan_cap_changed_cb,
    };
    osw_sta_chan_cap_t *m = OSW_MODULE_LOAD(osw_sta_chan_cap);
    struct ow_steer_policy_chan_cap *policy = CALLOC(1, sizeof(*policy));
    policy->base = ow_steer_policy_create(name, sta_addr, &policy_ops, mediator, policy);
    policy->obs = osw_sta_chan_cap_register(m, sta_addr, &cap_ops, policy);
    return policy;
}

void
ow_steer_policy_chan_cap_free(struct ow_steer_policy_chan_cap *policy)
{
    osw_sta_chan_cap_obs_unregister(policy->obs);
    ow_steer_policy_free(policy->base);
    FREE(policy);
}

