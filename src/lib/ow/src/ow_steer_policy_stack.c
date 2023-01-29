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

#include <assert.h>
#include <log.h>
#include <const.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <memutil.h>
#include <module.h>
#include <osw_timer.h>
#include <osw_state.h>
#include <osw_bss_map.h>
#include <osw_conf.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_i.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_sta.h"
#include "ow_steer_sta_priv.h"
#include "ow_steer_policy_stack.h"

struct ow_steer_policy_stack {
    struct ds_dlist policy_list;
    struct ow_steer_sta *sta;
    struct osw_timer work;
};

static void
ow_steer_policy_stack_work_cb(struct osw_timer *timer)
{
    struct ow_steer_policy_stack *stack = container_of(timer, struct ow_steer_policy_stack, work);
    const struct osw_hwaddr *mac_addr = ow_steer_sta_get_addr(stack->sta);
    struct ow_steer_candidate_list *candidate_list = ow_steer_sta_get_candidate_list(stack->sta);
    struct ow_steer_candidate_list *candidate_list_copy = ow_steer_candidate_list_copy(candidate_list);
    size_t i;

    LOGD("ow: steer: policy_stack: sta: "OSW_HWADDR_FMT" recalc candidates", OSW_HWADDR_ARG(mac_addr));
    ow_steer_candidate_list_clear(candidate_list);

    struct ow_steer_policy *policy;
    ds_dlist_foreach(&stack->policy_list, policy) {
        if (WARN_ON(policy->ops.recalc_fn == NULL))
            continue;

        policy->ops.recalc_fn(policy, candidate_list);
    }

    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
        const enum ow_steer_candidate_preference preference = ow_steer_candidate_get_preference(candidate);

        if (preference == OW_STEER_CANDIDATE_PREFERENCE_NONE)
            ow_steer_candidate_set_preference(candidate, OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

        LOGD("ow: steer: policy_stack: sta: "OSW_HWADDR_FMT" bssid: "OSW_HWADDR_FMT" preference: %s", OSW_HWADDR_ARG(mac_addr),
             OSW_HWADDR_ARG(bssid), ow_steer_candidate_preference_to_cstr(ow_steer_candidate_get_preference(candidate)));
     }

    if (ow_steer_candidate_list_cmp(candidate_list, candidate_list_copy) == false)
        ow_steer_sta_schedule_executor_call(stack->sta);

    ow_steer_candidate_list_free(candidate_list_copy);
}

struct ow_steer_policy_stack*
ow_steer_policy_stack_create(struct ow_steer_sta *sta)
{
    assert(sta != NULL);

    struct ow_steer_policy_stack *stack = CALLOC(1, sizeof(*stack));

    ds_dlist_init(&stack->policy_list, struct ow_steer_policy, stack_node);
    stack->sta = sta;
    osw_timer_init(&stack->work, ow_steer_policy_stack_work_cb);

    return stack;
}

void
ow_steer_policy_stack_free(struct ow_steer_policy_stack *stack)
{
    assert(stack != NULL);

    osw_timer_disarm(&stack->work);
    assert(ds_dlist_is_empty(&stack->policy_list) == true);
    FREE(stack);
}

void
ow_steer_policy_stack_add(struct ow_steer_policy_stack *stack,
                          struct ow_steer_policy *policy)
{
    assert(stack != NULL);
    assert(policy != NULL);

    struct ow_steer_policy *entry;

    ds_dlist_foreach(&stack->policy_list, entry)
        if (policy->priority <= entry->priority)
            break;

    if (entry == NULL)
        ds_dlist_insert_tail(&stack->policy_list, policy);
    else
        ds_dlist_insert_before(&stack->policy_list, entry, policy);

    ow_steer_policy_stack_schedule_recalc(stack);
}

void
ow_steer_policy_stack_remove(struct ow_steer_policy_stack *stack,
                             struct ow_steer_policy *policy)
{
    assert(stack != NULL);
    assert(policy != NULL);

    ds_dlist_remove(&stack->policy_list, policy);
    ow_steer_policy_stack_schedule_recalc(stack);
}

bool
ow_steer_policy_stack_is_empty(struct ow_steer_policy_stack *stack)
{
    assert(stack != NULL);
    return ds_dlist_is_empty(&stack->policy_list);
}

void
ow_steer_policy_stack_schedule_recalc(struct ow_steer_policy_stack *stack)
{
    assert(stack != NULL);
    osw_timer_arm_at_nsec(&stack->work, 0);
}

void
ow_steer_policy_stack_sigusr1_dump(struct ow_steer_policy_stack *stack)
{
    assert(stack != NULL);

    struct ow_steer_policy *policy;
    ds_dlist_foreach(&stack->policy_list, policy) {
        LOGI("ow: steer:       policy: name: %s", policy->name);
        LOGI("ow: steer:         priority: %u", ow_steer_policy_get_priority(policy));
        LOGI("ow: steer:         bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(ow_steer_policy_get_bssid(policy)));
        if (policy->ops.sigusr1_dump_fn != NULL)
            policy->ops.sigusr1_dump_fn(policy);
    }
}

void
ow_steer_policy_stack_sta_snr_change(struct ow_steer_policy_stack *stack,
                                     const struct osw_hwaddr *sta_addr,
                                     const struct osw_hwaddr *bssid,
                                     uint32_t snr_db)
{
    assert(stack != NULL);
    assert(sta_addr != NULL);
    assert(bssid != NULL);

    const struct osw_hwaddr wildcard_bssid = OSW_HWADDR_BROADCAST;
    struct ow_steer_policy *policy;

    ds_dlist_foreach(&stack->policy_list, policy) {
        if (policy->ops.sta_snr_change_fn == NULL)
            continue;

        const struct osw_hwaddr *policy_bssid = ow_steer_policy_get_bssid(policy);
        if (osw_hwaddr_cmp(policy_bssid, bssid) != 0 && osw_hwaddr_cmp(policy_bssid, &wildcard_bssid) != 0)
            continue;

        policy->ops.sta_snr_change_fn(policy, bssid, snr_db);
    }
}

void
ow_steer_policy_stack_sta_data_vol_change(struct ow_steer_policy_stack *stack,
                                          const struct osw_hwaddr *sta_addr,
                                          const struct osw_hwaddr *bssid,
                                          uint64_t data_vol_bytes)
{
    assert(stack != NULL);
    assert(sta_addr != NULL);
    assert(bssid != NULL);

    const struct osw_hwaddr wildcard_bssid = OSW_HWADDR_BROADCAST;
    struct ow_steer_policy *policy;

    ds_dlist_foreach(&stack->policy_list, policy) {
        if (policy->ops.sta_data_vol_change_fn == NULL)
            continue;

        const struct osw_hwaddr *policy_bssid = ow_steer_policy_get_bssid(policy);
        if (osw_hwaddr_cmp(policy_bssid, bssid) != 0 && osw_hwaddr_cmp(policy_bssid, &wildcard_bssid) != 0)
            continue;

        policy->ops.sta_data_vol_change_fn(policy, bssid, data_vol_bytes);
    }
}

#include "ow_steer_policy_stack_ut.c"
