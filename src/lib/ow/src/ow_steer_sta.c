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
#include <const.h>
#include <log.h>
#include <ds_tree.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_conf.h>
#include <osw_bss_map.h>
#include <osw_timer.h>
#include <osw_stats.h>
#include <osw_stats_defs.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_candidate_assessor.h"
#include "ow_steer_policy.h"
#include "ow_steer_sta.h"
#include "ow_steer_sta_priv.h"
#include "ow_steer_sta_i.h"
#include "ow_steer_policy_stack.h"
#include "ow_steer_executor_action.h"
#include "ow_steer_executor.h"
#include "ow_steer_priv.h"

struct ow_steer_sta_set {
    struct ds_tree sta_tree;
};

static void
ow_steer_sta_bss_set_cb(struct osw_bss_map_observer *observer,
                        const struct osw_hwaddr *bssid,
                        const struct osw_bss_entry *bss_entry)
{
    ASSERT(observer != NULL, "");
    ASSERT(bssid != NULL, "");
    ASSERT(bss_entry != NULL, "");

    struct ow_steer_sta *sta = container_of(observer, struct ow_steer_sta, bss_map_observer);
    const struct osw_channel *channel = osw_bss_get_channel(bssid);

    if (channel != NULL) {
        LOGD("ow: steer: sta: "OSW_HWADDR_FMT" set candidate bssid: "OSW_HWADDR_FMT" channel: "OSW_CHANNEL_FMT,
             OSW_HWADDR_ARG(&sta->mac_addr), OSW_HWADDR_ARG(bssid), OSW_CHANNEL_ARG(channel));
        ow_steer_candidate_list_bss_set(sta->candidate_list, bssid, channel);
    }
    else {
        LOGD("ow: steer: sta: "OSW_HWADDR_FMT" unset candidate bssid: "OSW_HWADDR_FMT", missing channel",
             OSW_HWADDR_ARG(&sta->mac_addr), OSW_HWADDR_ARG(bssid));
        ow_steer_candidate_list_bss_unset(sta->candidate_list, bssid);
    }

    ow_steer_policy_stack_schedule_recalc(sta->policy_stack);
}

static void
ow_steer_sta_bss_unset_cb(struct osw_bss_map_observer *observer,
                          const struct osw_hwaddr *bssid)
{
    ASSERT(observer != NULL, "");
    ASSERT(bssid != NULL, "");

    struct ow_steer_sta *sta = container_of(observer, struct ow_steer_sta, bss_map_observer);

    ow_steer_candidate_list_bss_unset(sta->candidate_list, bssid);
    ow_steer_policy_stack_schedule_recalc(sta->policy_stack);
}

static void
ow_steer_executor_timer_cb(struct osw_timer *timer)
{
    struct ow_steer_sta *sta = container_of(timer, struct ow_steer_sta, executor_timer);

    if (sta->candidate_assessor != NULL) {
        ow_steer_candidate_list_sort(sta->candidate_list, sta->candidate_assessor);
        /* TODO Log (debug) metrics */
    }

    ow_steer_executor_call(sta->executor, &sta->mac_addr, sta->candidate_list, sta->mutator);
}

struct ow_steer_sta*
ow_steer_sta_create(const struct osw_hwaddr *mac_addr)
{
    ASSERT(mac_addr != NULL, "");

    const struct osw_bss_map_observer bss_map_observer = {
        .name = "ow_steer_sta",
        .set_fn = ow_steer_sta_bss_set_cb,
        .unset_fn = ow_steer_sta_bss_unset_cb,
    };
    
    struct ow_steer_sta *sta = CALLOC(1, sizeof(*sta));

    memcpy(&sta->mac_addr, mac_addr, sizeof(sta->mac_addr));
    sta->candidate_list = ow_steer_candidate_list_new();
    sta->policy_stack = ow_steer_policy_stack_create(sta);
    sta->executor = ow_steer_executor_create();
    sta->mutator = ow_steer_get_mutator();
    memcpy(&sta->bss_map_observer, &bss_map_observer, sizeof(sta->bss_map_observer));

    osw_bss_map_register_observer(&sta->bss_map_observer);

    osw_timer_init(&sta->executor_timer, ow_steer_executor_timer_cb);

    ds_dlist_insert_tail(ow_steer_get_sta_list(), sta);

    return sta;
}

void
ow_steer_sta_free(struct ow_steer_sta *sta)
{
    ASSERT(sta != NULL, "");

    ds_dlist_remove(ow_steer_get_sta_list(), sta);

    osw_bss_map_unregister_observer(&sta->bss_map_observer);

    ow_steer_policy_stack_free(sta->policy_stack);
    ow_steer_candidate_list_free(sta->candidate_list);
    ow_steer_executor_free(sta->executor);
    osw_timer_disarm(&sta->executor_timer);
    ow_steer_candidate_assessor_free(sta->candidate_assessor);
    FREE(sta);
}

const struct osw_hwaddr*
ow_steer_sta_get_addr(const struct ow_steer_sta *sta)
{
    ASSERT(sta != NULL, "");
    return &sta->mac_addr;
}

struct ow_steer_candidate_list*
ow_steer_sta_get_candidate_list(struct ow_steer_sta *sta)
{
    ASSERT(sta != NULL, "");
    return sta->candidate_list;
}

struct ow_steer_policy_stack*
ow_steer_sta_get_policy_stack(struct ow_steer_sta *sta)
{
    ASSERT(sta != NULL, "");
    return sta->policy_stack;
}

struct ow_steer_executor*
ow_steer_sta_get_executor(struct ow_steer_sta *sta)
{
    ASSERT(sta != NULL, "");
    return sta->executor;
}

void
ow_steer_sta_set_candidate_assessor(struct ow_steer_sta *sta,
                                    struct ow_steer_candidate_assessor *candidate_assessor)
{
    ASSERT(sta != NULL, "");
    ow_steer_candidate_assessor_free(sta->candidate_assessor);
    sta->candidate_assessor = candidate_assessor;
    ow_steer_sta_schedule_executor_call(sta);
}

void
ow_steer_sta_schedule_executor_call(struct ow_steer_sta *sta)
{
    ASSERT(sta != NULL, "");
    osw_timer_arm_at_nsec(&sta->executor_timer, 0);
}

void
ow_steer_sta_schedule_policy_stack_recalc(struct ow_steer_sta *sta)
{
    ASSERT(sta != NULL, "");
    ow_steer_policy_stack_schedule_recalc(sta->policy_stack);
}

void
ow_steer_sta_conf_mutate(struct ow_steer_sta *sta,
                         struct ds_tree *phy_tree)
{
    ASSERT(sta != NULL, "");
    ASSERT(phy_tree != NULL, "");

    ow_steer_executor_conf_mutate(sta->executor, &sta->mac_addr, sta->candidate_list, phy_tree);
}

void
ow_steer_sta_sigusr1_dump(void)
{
    LOGI("ow: steer: stas:");

    struct ow_steer_sta *sta;
    ds_dlist_foreach(ow_steer_get_sta_list(), sta) {
        LOGI("ow: steer:   sta:");
        LOGI("ow: steer:     ptr: %p", sta);
        LOGI("ow: steer:     mac_addr: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&sta->mac_addr));
        LOGI("ow: steer:     candidate_list:");
        ow_steer_candidate_list_sigusr1_dump(sta->candidate_list);
        LOGI("ow: steer:     policy_stack:");
        ow_steer_policy_stack_sigusr1_dump(sta->policy_stack);
    }
}
