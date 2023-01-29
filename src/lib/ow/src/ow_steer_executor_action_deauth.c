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
#include <const.h>
#include <memutil.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_conf.h>
#include <osw_mux.h>
#include <osw_time.h>
#include <osw_timer.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_executor_action.h"
#include "ow_steer_executor_action_priv.h"
#include "ow_steer_executor_action_deauth.h"

#define OW_STEER_EXECUTOR_ACTION_DEAUTH_DELAY_SEC 10
#define DOT11_DEAUTH_REASON_CODE_UNSPECIFIED 1

struct ow_steer_executor_action_deauth {
    struct ow_steer_executor_action *base;
    struct osw_timer delay_timer;
};

static void
ow_steer_executor_action_deauth_call_fn(struct ow_steer_executor_action *action,
                                        const struct ow_steer_candidate_list *candidate_list,
                                        struct osw_conf_mutator *mutator)
{
    struct ow_steer_executor_action_deauth *deauth_action = ow_steer_executor_action_get_priv(action);
    const bool need_kick = ow_steer_executor_action_check_kick_needed(action, candidate_list);
    const bool deauth_pending = osw_timer_is_armed(&deauth_action->delay_timer);
    if (need_kick == true) {
        if (deauth_pending == true) {
            const uint64_t delay_remaining_nsec = osw_timer_get_remaining_nsec(&deauth_action->delay_timer, osw_time_mono_clk());
            LOGD("%s deauth already scheduled, remaining delay: %.2lf sec", ow_steer_executor_action_get_prefix(deauth_action->base), OSW_TIME_TO_DBL(delay_remaining_nsec));
        }
        else {
            const uint64_t delay_nsec = OSW_TIME_SEC(OW_STEER_EXECUTOR_ACTION_DEAUTH_DELAY_SEC);
            LOGI("%s scheduled deauth, delay: %.2lf sec", ow_steer_executor_action_get_prefix(deauth_action->base), OSW_TIME_TO_DBL(delay_nsec));
            osw_timer_arm_at_nsec(&deauth_action->delay_timer, osw_time_mono_clk() + delay_nsec);
        }
    }
    else {
        if (deauth_pending == true)
            LOGI("%s canceled scheduled deauth", ow_steer_executor_action_get_prefix(deauth_action->base));

        osw_timer_disarm(&deauth_action->delay_timer);
    }
}

static void
ow_steer_executor_action_deauth_delay_timer_cb(struct osw_timer *timer)
{
    struct ow_steer_executor_action_deauth *deauth_action = container_of(timer, struct ow_steer_executor_action_deauth, delay_timer);
    const struct osw_hwaddr *sta_addr = ow_steer_executor_action_get_sta_addr(deauth_action->base);
    const struct osw_state_sta_info *sta_info = osw_state_sta_lookup_newest(sta_addr);
    if (WARN_ON(sta_info == NULL))
        return;
    if (WARN_ON(sta_info->vif == NULL))
        return;
    if (WARN_ON(sta_info->vif->phy == NULL))
        return;

    const char *phy_name = sta_info->vif->phy->phy_name;
    const char *vif_name = sta_info->vif->vif_name;

    const bool deauth_success = osw_mux_request_sta_deauth(phy_name, vif_name, sta_addr, DOT11_DEAUTH_REASON_CODE_UNSPECIFIED);
    if (deauth_success == true)
        LOGI("%s issued deauth", ow_steer_executor_action_get_prefix(deauth_action->base));
    else
        LOGW("%s failed to deauth", ow_steer_executor_action_get_prefix(deauth_action->base));
}

struct ow_steer_executor_action_deauth*
ow_steer_executor_action_deauth_create(const struct osw_hwaddr *sta_addr,
                                       const struct ow_steer_executor_action_mediator *mediator)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(mediator != NULL, "");

    const struct ow_steer_executor_action_ops ops = {
        .call_fn = ow_steer_executor_action_deauth_call_fn,
    };
    struct ow_steer_executor_action_deauth *deauth_action = CALLOC(1, sizeof(*deauth_action));
    osw_timer_init(&deauth_action->delay_timer, ow_steer_executor_action_deauth_delay_timer_cb);
    deauth_action->base = ow_steer_executor_action_create("deauth", sta_addr, &ops, mediator, deauth_action);

    return deauth_action;
}

void
ow_steer_executor_action_deauth_free(struct ow_steer_executor_action_deauth *deauth_action)
{
    ASSERT(deauth_action != NULL, "");
    osw_timer_disarm(&deauth_action->delay_timer);
    ow_steer_executor_action_free(deauth_action->base);
    FREE(deauth_action);
}

struct ow_steer_executor_action*
ow_steer_executor_action_deauth_get_base(struct ow_steer_executor_action_deauth *deauth_action)
{
    ASSERT(deauth_action != NULL, "");
    return deauth_action->base;
}
