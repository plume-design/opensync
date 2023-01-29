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

#include <ds_tree.h>
#include <memutil.h>
#include <const.h>
#include <util.h>
#include <log.h>
#include <module.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_time.h>
#include <osw_timer.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_policy_i.h"
#include "ow_steer_policy_force_kick.h"

#define OW_STEER_POLICY_FORCE_KICK_ENFORCE_PERIOD_SEC 20

struct ow_steer_policy_force_kick {
    struct ow_steer_policy *base;
    struct ow_steer_policy_force_kick_config *next_config;
    struct ow_steer_policy_force_kick_config *config;
    const struct osw_state_sta_info *sta_info;
    struct osw_timer reconf_timer;
    struct osw_timer enforce_timer;
    struct osw_state_observer state_observer;
};

static const char *g_policy_name = "force_kick";

static void
ow_steer_policy_force_kick_reset(struct ow_steer_policy_force_kick *force_policy)
{
    ASSERT(force_policy != NULL, "");

    const bool enforce_period = osw_timer_is_armed(&force_policy->enforce_timer);

    if (enforce_period == true)
        LOGI("%s enforce period stopped", ow_steer_policy_get_prefix(force_policy->base));

    osw_timer_disarm(&force_policy->enforce_timer);
    ow_steer_policy_force_kick_set_oneshot_config(force_policy, NULL);

    if (enforce_period == true)
        ow_steer_policy_dismiss_executor(force_policy->base);
}

static void
ow_steer_policy_force_kick_recalc_cb(struct ow_steer_policy *policy,
                                     struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(policy != NULL, "");
    ASSERT(candidate_list != NULL, "");

    struct ow_steer_policy_force_kick *force_policy = ow_steer_policy_get_priv(policy);

    const struct ow_steer_policy_force_kick_config *config = force_policy->config;
    if (config == NULL)
        return;

    const bool enforce_period = osw_timer_is_armed(&force_policy->enforce_timer);
    if (enforce_period == false)
        return;

    if (WARN_ON(force_policy->sta_info == NULL)) {
        ow_steer_policy_force_kick_reset(force_policy);
        return;
    }

    const struct osw_hwaddr *vif_bssid = &force_policy->sta_info->vif->drv_state->mac_addr;
    struct ow_steer_candidate *blocked_candidate = ow_steer_candidate_list_lookup(candidate_list, vif_bssid);
    const bool blocked_candidate_exists = blocked_candidate != NULL;
    if (blocked_candidate_exists == false)
        LOGI("%s bssid: "OSW_HWADDR_FMT" preference: (nil), doesnt exist", ow_steer_policy_get_prefix(policy), OSW_HWADDR_ARG(vif_bssid));

    const enum ow_steer_candidate_preference blocked_candidate_pref = ow_steer_candidate_get_preference(blocked_candidate);
    bool block_cancicdate_blockable = true;
    if (blocked_candidate_exists == true) {
        switch (blocked_candidate_pref) {
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                block_cancicdate_blockable = false;
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                block_cancicdate_blockable = true;
                break;
        }
    }

    bool any_available_candidates = false;
    size_t i = 0;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        const enum ow_steer_candidate_preference candidate_pref = ow_steer_candidate_get_preference(candidate);

        if (blocked_candidate == candidate)
            continue;

        switch (candidate_pref) {
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
                any_available_candidates = false;
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                any_available_candidates = true;
                break;
        }

        if (any_available_candidates == true)
            break;
    }

    if (any_available_candidates == false)
        LOGI("%s no other candidates available", ow_steer_policy_get_prefix(policy));

    const bool continue_enforce = blocked_candidate_exists == true &&
                                  block_cancicdate_blockable == true &&
                                  any_available_candidates == true;
    if (continue_enforce == false) {
        ow_steer_policy_force_kick_reset(force_policy);
        return;
    }

    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
        const enum ow_steer_candidate_preference candidate_pref = ow_steer_candidate_get_preference(candidate);

        if (candidate_pref != OW_STEER_CANDIDATE_PREFERENCE_NONE)
            continue;

        if (blocked_candidate == candidate)
            ow_steer_candidate_set_preference(candidate, OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
        else
            ow_steer_candidate_set_preference(candidate, OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

        LOGD("%s bssid: "OSW_HWADDR_FMT" preference: %s", ow_steer_policy_get_prefix(policy), OSW_HWADDR_ARG(bssid),
             ow_steer_candidate_preference_to_cstr(ow_steer_candidate_get_preference(candidate)));
    }
}

static void
ow_steer_policy_force_kick_sigusr1_dump_cb(struct ow_steer_policy *policy)
{
    ASSERT(policy != NULL, "");

    struct ow_steer_policy_force_kick *force_policy = ow_steer_policy_get_priv(policy);
    const struct ow_steer_policy_force_kick_config *config = force_policy->config;

    LOGI("ow: steer:         config: %s", config != NULL ? "" : "(nil)");

    const uint64_t now_nsec = osw_time_mono_clk();
    const char *enforce_timer_buf = osw_timer_is_armed(&force_policy->enforce_timer) == true?
        strfmta("%.2lf sec remaining", OSW_TIME_TO_DBL(osw_timer_get_remaining_nsec(&force_policy->enforce_timer, now_nsec))) : "inactive";

    LOGI("ow: steer:         enforce_timer: %s", enforce_timer_buf);
}

static void
ow_steer_policy_force_kick_reconf_timer_cb(struct osw_timer *timer)
{
    ASSERT(timer != NULL, "");

    struct ow_steer_policy_force_kick *force_policy = container_of(timer, struct ow_steer_policy_force_kick, reconf_timer);

    bool unregister_observer = false;
    bool register_observer = false;

    if (force_policy->config == NULL && force_policy->next_config == NULL) {
        /* nop */
        return;
    }
    else if (force_policy->config == NULL && force_policy->next_config != NULL) {
        LOGI("%s config added", ow_steer_policy_get_prefix(force_policy->base));
        register_observer = true;
    }
    else if (force_policy->config != NULL && force_policy->next_config == NULL) {
        LOGI("%s config removed", ow_steer_policy_get_prefix(force_policy->base));
        unregister_observer = true;
    }
    else if (force_policy->config != NULL && force_policy->next_config != NULL) {
        LOGI("%s config changed", ow_steer_policy_get_prefix(force_policy->base));
        unregister_observer = true;
        register_observer = true;
    }
    else {
        ASSERT(false, "");
    }

    FREE(force_policy->config);
    force_policy->config = NULL;
    if (unregister_observer == true)
        osw_state_unregister_observer(&force_policy->state_observer);

    force_policy->config = force_policy->next_config;
    force_policy->next_config = NULL;
    if (register_observer == true) {
        osw_state_register_observer(&force_policy->state_observer);

        ow_steer_policy_trigger_executor(force_policy->base);

        const uint64_t enforce_period_nsec = osw_time_mono_clk() + OSW_TIME_SEC(OW_STEER_POLICY_FORCE_KICK_ENFORCE_PERIOD_SEC);
        osw_timer_arm_at_nsec(&force_policy->enforce_timer, enforce_period_nsec);
        LOGI("%s enforce period started", ow_steer_policy_get_prefix(force_policy->base));

        if (force_policy->sta_info == NULL) {
            LOGI("%s aborted force kick, sta disconnected", ow_steer_policy_get_prefix(force_policy->base));
            ow_steer_policy_force_kick_reset(force_policy);
            return;
        }
    }

    ow_steer_policy_schedule_stack_recalc(force_policy->base);
}

static void
ow_steer_policy_force_kick_enforce_timer_cb(struct osw_timer *timer)
{
    ASSERT(timer != NULL, "");

    struct ow_steer_policy_force_kick *force_policy = container_of(timer, struct ow_steer_policy_force_kick, enforce_timer);

    LOGI("%s enforce period finished", ow_steer_policy_get_prefix(force_policy->base));

    ow_steer_policy_force_kick_reset(force_policy);
    ow_steer_policy_dismiss_executor(force_policy->base);
}

static void
ow_steer_policy_force_kick_sta_connected_cb(struct osw_state_observer *observer,
                                            const struct osw_state_sta_info *sta_info)
{
    struct ow_steer_policy_force_kick *force_policy = container_of(observer, struct ow_steer_policy_force_kick, state_observer);

    const struct osw_hwaddr *policy_sta_addr = ow_steer_policy_get_sta_addr(force_policy->base);
    if (osw_hwaddr_cmp(sta_info->mac_addr, policy_sta_addr) != 0)
        return;

    force_policy->sta_info = sta_info;

    const struct ow_steer_policy_force_kick_config *config = force_policy->config;
    const struct osw_hwaddr *vif_bssid = &sta_info->vif->drv_state->mac_addr;
    if (config == NULL) {
        LOGD("%s sta connected to bssid: "OSW_HWADDR_FMT", (no conf)", ow_steer_policy_get_prefix(force_policy->base), OSW_HWADDR_ARG(vif_bssid));
    }
    else {
        LOGI("%s sta connected from bssid: "OSW_HWADDR_FMT, ow_steer_policy_get_prefix(force_policy->base), OSW_HWADDR_ARG(vif_bssid));
    }
}

static void
ow_steer_policy_force_kick_sta_disconnected_cb(struct osw_state_observer *observer,
                                               const struct osw_state_sta_info *sta_info)
{
    struct ow_steer_policy_force_kick *force_policy = container_of(observer, struct ow_steer_policy_force_kick, state_observer);

    const struct osw_hwaddr *policy_sta_addr = ow_steer_policy_get_sta_addr(force_policy->base);
    if (osw_hwaddr_cmp(sta_info->mac_addr, policy_sta_addr) != 0)
        return;

    if (force_policy->sta_info == sta_info)
        force_policy->sta_info = NULL;

    const struct ow_steer_policy_force_kick_config *config = force_policy->config;
    const struct osw_hwaddr *vif_bssid = &sta_info->vif->drv_state->mac_addr;
    if (config == NULL) {
        LOGD("%s sta disconnected from bssid: "OSW_HWADDR_FMT", (no conf)", ow_steer_policy_get_prefix(force_policy->base), OSW_HWADDR_ARG(vif_bssid));
    }
    else {
        LOGI("%s sta disconnected from bssid: "OSW_HWADDR_FMT, ow_steer_policy_get_prefix(force_policy->base), OSW_HWADDR_ARG(vif_bssid));
        ow_steer_policy_force_kick_reset(force_policy);
    }
}

struct ow_steer_policy_force_kick*
ow_steer_policy_force_kick_create(unsigned int priority,
                                  const struct osw_hwaddr *sta_addr,
                                  const struct ow_steer_policy_mediator *mediator)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(mediator != NULL, "");

    const struct ow_steer_policy_ops ops = {
        .sigusr1_dump_fn = ow_steer_policy_force_kick_sigusr1_dump_cb,
        .recalc_fn = ow_steer_policy_force_kick_recalc_cb,
    };
    const struct osw_state_observer state_observer = {
        .name = g_policy_name,
        .sta_connected_fn = ow_steer_policy_force_kick_sta_connected_cb,
        .sta_disconnected_fn = ow_steer_policy_force_kick_sta_disconnected_cb,
    };

    struct ow_steer_policy_force_kick *force_policy = CALLOC(1, sizeof(*force_policy));
    osw_timer_init(&force_policy->reconf_timer, ow_steer_policy_force_kick_reconf_timer_cb);
    osw_timer_init(&force_policy->enforce_timer, ow_steer_policy_force_kick_enforce_timer_cb);
    memcpy(&force_policy->state_observer, &state_observer, sizeof(force_policy->state_observer));

    force_policy->base = ow_steer_policy_create(g_policy_name, priority, sta_addr, &ops, mediator, force_policy);

    return force_policy;
}

void
ow_steer_policy_force_kick_free(struct ow_steer_policy_force_kick *force_policy)
{
    if (force_policy == NULL)
        return;

    const bool unregister_observer = force_policy->config != NULL;

    ow_steer_policy_force_kick_reset(force_policy);
    FREE(force_policy->next_config);
    force_policy->next_config = NULL;
    FREE(force_policy->config);
    force_policy->config = NULL;
    if (unregister_observer == true)
        osw_state_unregister_observer(&force_policy->state_observer);
    ow_steer_policy_free(force_policy->base);
    FREE(force_policy);
}

struct ow_steer_policy*
ow_steer_policy_force_kick_get_base(struct ow_steer_policy_force_kick *force_policy)
{
    ASSERT(force_policy != NULL, "");
    return force_policy->base;
}

void
ow_steer_policy_force_kick_set_oneshot_config(struct ow_steer_policy_force_kick *force_policy,
                                              struct ow_steer_policy_force_kick_config *config)
{
    ASSERT(force_policy != NULL, "");

    FREE(force_policy->next_config);
    force_policy->next_config = config;

    osw_timer_arm_at_nsec(&force_policy->reconf_timer, 0);
}

#include "ow_steer_policy_force_kick_ut.c"
