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

#include <stdint.h>
#include <util.h>
#include <log.h>
#include <ds_dlist.h>
#include <memutil.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_conf.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_executor_action.h"
#include "ow_steer_executor_action_i.h"
#include "ow_steer_executor_action_priv.h"


struct ow_steer_executor_action*
ow_steer_executor_action_create(const char *name,
                                const struct osw_hwaddr *sta_addr,
                                const struct ow_steer_executor_action_ops *ops,
                                const struct ow_steer_executor_action_mediator *mediator,
                                void *priv)
{
    ASSERT(name != NULL, "");
    ASSERT(sta_addr != NULL, "");
    ASSERT(ops != NULL, "");
    ASSERT(mediator != NULL, "");

    struct ow_steer_executor_action *action = CALLOC(1, sizeof(*action));

    action->name = name;
    action->log_prefix = strfmt("ow: steer: executor: action: %s: sta: "OSW_HWADDR_FMT" ", name, OSW_HWADDR_ARG(sta_addr));
    memcpy(&action->sta_addr, sta_addr, sizeof(action->sta_addr));
    memcpy(&action->ops, ops, sizeof(action->ops));
    memcpy(&action->mediator, mediator, sizeof(action->mediator));
    action->priv = priv;

    return action;
}

void
ow_steer_executor_action_free(struct ow_steer_executor_action *action)
{
    ASSERT(action != NULL, "");
    FREE(action->log_prefix);
    FREE(action);
}

void
ow_steer_executor_action_sched_recall(struct ow_steer_executor_action *action)
{
    ASSERT(action != NULL, "");

    ASSERT(action->mediator.sched_recall_fn != NULL, "");
    action->mediator.sched_recall_fn(action, action->mediator.priv);
}

void
ow_steer_executor_action_notify_going_busy(struct ow_steer_executor_action *action)
{
    ASSERT(action != NULL, "");

    ASSERT(action->mediator.notify_going_busy_fn != NULL, "");
    action->mediator.notify_going_busy_fn(action, action->mediator.priv);
}

void
ow_steer_executor_action_notify_data_sent(struct ow_steer_executor_action *action)
{
    ASSERT(action != NULL, "");

    ASSERT(action->mediator.notify_data_sent_fn != NULL, "");
    action->mediator.notify_data_sent_fn(action, action->mediator.priv);
}

void
ow_steer_executor_action_notify_going_idle(struct ow_steer_executor_action *action)
{
    ASSERT(action != NULL, "");

    ASSERT(action->mediator.notify_going_idle_fn != NULL, "");
    action->mediator.notify_going_idle_fn(action, action->mediator.priv);
}

void*
ow_steer_executor_action_get_priv(struct ow_steer_executor_action *action)
{
    ASSERT(action != NULL, "");
    return action->priv;
}

const char*
ow_steer_executor_action_get_name(struct ow_steer_executor_action *action)
{
    ASSERT(action != NULL, "");
    return action->name;
}

static const char *
ow_steer_executor_action_kick_action_to_cstr(enum ow_steer_executor_action_kick_action kick_action)
{
    switch (kick_action) {
        case OW_STEER_EXECUTOR_ACTION_KICK_HARD_BLOCK: return "kick (hard block)";
        case OW_STEER_EXECUTOR_ACTION_KICK_BETTER_METRIC: return "kick (better metric)";
        case OW_STEER_EXECUTOR_ACTION_SKIP_DISCONNECTED: return "skip (disconnected)";
        case OW_STEER_EXECUTOR_ACTION_SKIP_STA_MISSING_VIF: return "skip (sta missing vif)";
        case OW_STEER_EXECUTOR_ACTION_SKIP_STA_VIF_MISSING_PHY: return "skip (sta vif missing phy)";
        case OW_STEER_EXECUTOR_ACTION_SKIP_NO_CANDIDATES_LEFT: return "skip (no candidates left)";
        case OW_STEER_EXECUTOR_ACTION_SKIP_LINK_TO_NON_CANDIDATE: return "skip (link to non-candidate)";
        case OW_STEER_EXECUTOR_ACTION_SKIP_LINK_TO_OUT_OF_SCOPE: return "skip (link to out-of-scope)";
        case OW_STEER_EXECUTOR_ACTION_SKIP_LINK_TO_NO_PREFERENCE: return "skip (link to none-preference)";
        case OW_STEER_EXECUTOR_ACTION_SKIP_LINK_GOOD_ENOUGH: return "skip (link good enough)";
    }
    return "";
}

static bool
ow_steer_executor_action_kick_action_to_bool(enum ow_steer_executor_action_kick_action kick_action)
{
    switch (kick_action) {
        case OW_STEER_EXECUTOR_ACTION_KICK_HARD_BLOCK: return true;
        case OW_STEER_EXECUTOR_ACTION_KICK_BETTER_METRIC: return true;
        case OW_STEER_EXECUTOR_ACTION_SKIP_DISCONNECTED: return false;
        case OW_STEER_EXECUTOR_ACTION_SKIP_STA_MISSING_VIF: return false;
        case OW_STEER_EXECUTOR_ACTION_SKIP_STA_VIF_MISSING_PHY: return false;
        case OW_STEER_EXECUTOR_ACTION_SKIP_NO_CANDIDATES_LEFT: return false;
        case OW_STEER_EXECUTOR_ACTION_SKIP_LINK_TO_NON_CANDIDATE: return false;
        case OW_STEER_EXECUTOR_ACTION_SKIP_LINK_TO_OUT_OF_SCOPE: return false;
        case OW_STEER_EXECUTOR_ACTION_SKIP_LINK_TO_NO_PREFERENCE: return false;
        case OW_STEER_EXECUTOR_ACTION_SKIP_LINK_GOOD_ENOUGH: return false;
    }
    return false;
}

static enum ow_steer_executor_action_kick_action
ow_steer_executor_action_get_kick_action(const struct ow_steer_executor_action *action,
                                         const struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(action != NULL, "");
    ASSERT(candidate_list != NULL, "");

    /* Check if STA is connected to pod */
    const struct osw_state_sta_info *sta_info = osw_state_sta_lookup_newest(&action->sta_addr);
    const bool sta_is_disconnected = sta_info == NULL;
    if (sta_is_disconnected == true) {
        return OW_STEER_EXECUTOR_ACTION_SKIP_DISCONNECTED;
    }
    if (WARN_ON(sta_info->vif == NULL)) {
        return OW_STEER_EXECUTOR_ACTION_SKIP_STA_MISSING_VIF;
    }
    if (WARN_ON(sta_info->vif->phy == NULL)) {
        return OW_STEER_EXECUTOR_ACTION_SKIP_STA_VIF_MISSING_PHY;
    }

    const struct osw_hwaddr *vif_bssid = &sta_info->vif->drv_state->mac_addr;
    const struct ow_steer_candidate *vif_candidate = ow_steer_candidate_list_const_lookup(candidate_list, vif_bssid);
    if (vif_candidate == NULL) {
        return OW_STEER_EXECUTOR_ACTION_SKIP_LINK_TO_NON_CANDIDATE;
    }

    /* Validate candidate list */
    size_t i = 0;
    unsigned int available_candidates_cnt = 0;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        const struct ow_steer_candidate *candidate = ow_steer_candidate_list_const_get(candidate_list, i);
        const enum ow_steer_candidate_preference preference = ow_steer_candidate_get_preference(candidate);
        if (preference == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE)
            available_candidates_cnt++;
    }

    if (available_candidates_cnt == 0) {
        return OW_STEER_EXECUTOR_ACTION_SKIP_NO_CANDIDATES_LEFT;
    }

    /* Check if STA has to be kicked regardless of metric */
    const enum ow_steer_candidate_preference vif_preference = ow_steer_candidate_get_preference(vif_candidate);
    switch (vif_preference) {
        case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            return OW_STEER_EXECUTOR_ACTION_SKIP_LINK_TO_OUT_OF_SCOPE;
        case OW_STEER_CANDIDATE_PREFERENCE_NONE:
            WARN_ON(true);
            return OW_STEER_EXECUTOR_ACTION_SKIP_LINK_TO_NO_PREFERENCE;
        case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
            return OW_STEER_EXECUTOR_ACTION_KICK_HARD_BLOCK;
        case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
        case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
            break;
    };

    /* Check if there's a avaiable candidate with higher metric */
    const unsigned int vif_candidate_metric = ow_steer_candidate_get_metric(vif_candidate);
    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        const struct ow_steer_candidate *candidate = ow_steer_candidate_list_const_get(candidate_list, i);
        const enum ow_steer_candidate_preference preference = ow_steer_candidate_get_preference(candidate);
        switch (preference) {
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
                continue;
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                WARN_ON(true);
                continue;
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
                continue;
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                break;
        };

        const unsigned int candidate_metric = ow_steer_candidate_get_metric(vif_candidate);
        if (candidate_metric > vif_candidate_metric) {
            const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
            LOGD("%s "OSW_HWADDR_FMT" has better metric than "OSW_HWADDR_FMT" (%d > %d), needs action",
                 ow_steer_executor_action_get_prefix(action),
                 OSW_HWADDR_ARG(bssid),
                 OSW_HWADDR_ARG(vif_bssid),
                 candidate_metric,
                 vif_candidate_metric);
            return OW_STEER_EXECUTOR_ACTION_KICK_BETTER_METRIC;
        }
    }

    return OW_STEER_EXECUTOR_ACTION_SKIP_LINK_GOOD_ENOUGH;
}

bool
ow_steer_executor_action_check_kick_needed(struct ow_steer_executor_action *action,
                                           const struct ow_steer_candidate_list *candidate_list)
{
    const enum ow_steer_executor_action_kick_action kick_action = ow_steer_executor_action_get_kick_action(action, candidate_list);
    const bool kick = ow_steer_executor_action_kick_action_to_bool(kick_action);
    if (kick_action != action->last_kick_action) {
        LOGI("%s kick: %s -> %s",
             ow_steer_executor_action_get_prefix(action),
             ow_steer_executor_action_kick_action_to_cstr(action->last_kick_action),
             ow_steer_executor_action_kick_action_to_cstr(kick_action));
        action->last_kick_action = kick_action;
    }
    return kick;
}

const struct osw_hwaddr*
ow_steer_executor_action_get_sta_addr(const struct ow_steer_executor_action *action)
{
    ASSERT(action != NULL, "");
    return &action->sta_addr;
}

const char*
ow_steer_executor_action_get_prefix(const struct ow_steer_executor_action *action)
{
    ASSERT(action != NULL, "");
    return action->log_prefix;
}
