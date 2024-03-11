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
#include <osw_throttle.h>
#include <osw_btm.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_bss_map.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_executor_action.h"
#include "ow_steer_executor_action_priv.h"
#include "ow_steer_executor_action_btm.h"

#define OW_STEER_EXECUTOR_ACTION_BTM_RETRY_INTERVAL_SEC 3

/* FIXME Some defauls */
#define OW_STEER_EXECUTOR_ACTION_BTM_NEIGHS_LIMIT 6
#define OW_STEER_EXECUTOR_ACTION_BTM_VALID_INT 255
#define OW_STEER_EXECUTOR_ACTION_BTM_DISASSOC_IMMINENT 1
#define OW_STEER_EXECUTOR_ACTION_BTM_DISASSOC_TIMER 0
#define OW_STEER_EXECUTOR_ACTION_BTM_AIRBRIDGED 1
#define OW_STEER_EXECUTOR_ACTION_BTM_BSS_TERM 0

#define OW_STEER_BM_BTM_DEFAULT_NEIGH_BSS_INFO 0x8F /* Reachable, secure, key scope */

struct ow_steer_executor_action_btm {
    struct ow_steer_executor_action *base;
    struct osw_btm_sta_observer btm_desc_observer;
    struct osw_btm_desc *btm_desc;
    struct osw_throttle *throttle;
    struct osw_timer throttle_timer;
    ow_steer_executor_action_btm_get_shutdown_deadline_nsec_fn_t *get_shutdown_deadline_nsec_fn;
    ow_steer_executor_action_btm_get_shutdown_deadline_nsec_fn_t *get_shutdown_deadline_nsec_fn_priv;
    bool *disassoc_imminent;
    bool enabled;
};

static void
ow_steer_executor_action_btm_req_tx_complete_cb(struct osw_btm_sta_observer *observer)
{
    struct ow_steer_executor_action_btm *btm_action = container_of(observer, struct ow_steer_executor_action_btm, btm_desc_observer);
    ow_steer_executor_action_sched_recall(btm_action->base);
    LOGD("%s request submitted", ow_steer_executor_action_get_prefix(btm_action->base));
}

static void
ow_steer_executor_action_btm_req_tx_error_cb(struct osw_btm_sta_observer *observer)
{
    struct ow_steer_executor_action_btm *btm_action = container_of(observer, struct ow_steer_executor_action_btm, btm_desc_observer);
    ow_steer_executor_action_sched_recall(btm_action->base);
    LOGD("%s request failed to submit", ow_steer_executor_action_get_prefix(btm_action->base));
}

static bool
ow_steer_executor_action_btm_get_disassoc_imminent(const struct ow_steer_executor_action_btm *btm_action)
{
    if (btm_action->disassoc_imminent) {
        return *btm_action->disassoc_imminent;
    }

    return OW_STEER_EXECUTOR_ACTION_BTM_DISASSOC_IMMINENT;
}

void
ow_steer_executor_action_btm_set_disassoc_imminent(struct ow_steer_executor_action_btm *btm_action,
                                                   const bool *b)
{
    FREE(btm_action->disassoc_imminent);
    btm_action->disassoc_imminent = NULL;
    if (b == NULL) return;
    btm_action->disassoc_imminent = MEMNDUP(b, sizeof(*b));
}

static uint64_t
ow_steer_executor_action_btm_get_beacon_interval(struct ow_steer_executor_action_btm *btm_action)
{
    const struct osw_hwaddr *sta_addr = ow_steer_executor_action_get_sta_addr(btm_action->base);
    if (sta_addr == NULL) return 0;

    const struct osw_state_sta_info *sta_info = osw_state_sta_lookup_newest(sta_addr);
    if (sta_info == NULL) return 0;

    const struct osw_state_vif_info *vif_info = sta_info->vif;
    if (vif_info == NULL) return 0;

    switch (vif_info->drv_state->vif_type) {
        case OSW_VIF_UNDEFINED:
        case OSW_VIF_AP_VLAN:
        case OSW_VIF_STA:
            return 0;
        case OSW_VIF_AP:
            break;
    }

    const struct osw_drv_vif_state_ap *ap = &vif_info->drv_state->u.ap;
    const uint64_t beacon_interval_tu = ap->beacon_interval_tu;

    return beacon_interval_tu;
}

static uint16_t
ow_steer_executor_action_btm_get_disassoc_timer(struct ow_steer_executor_action_btm *btm_action,
                                                double remaining_nsec)
{
    const uint64_t beacon_interval_tu = ow_steer_executor_action_btm_get_beacon_interval(btm_action);
    if (beacon_interval_tu == 0) return 0;

    const uint64_t beacon_interval_usec = 1024 * beacon_interval_tu;
    const uint64_t beacon_interval_nsec = 1000 * beacon_interval_usec;
    const uint64_t remaining_tbtts = remaining_nsec / beacon_interval_nsec;

    return remaining_tbtts;
}

void
ow_steer_executor_action_btm_set_get_shutdown_deadline_nsec_fn(struct ow_steer_executor_action_btm *btm_action,
                                                               ow_steer_executor_action_btm_get_shutdown_deadline_nsec_fn_t *fn,
                                                               void *fn_priv)
{
    btm_action->get_shutdown_deadline_nsec_fn = fn;
    btm_action->get_shutdown_deadline_nsec_fn_priv = fn_priv;
}

static void
ow_steer_executor_action_btm_handle_shutdown_deadline(struct ow_steer_executor_action_btm *btm_action,
                                                      struct osw_btm_req_params *btm_req_params)
{
    if (btm_action->get_shutdown_deadline_nsec_fn == NULL) return;

    const uint64_t remaining_nsec = btm_action->get_shutdown_deadline_nsec_fn(btm_action->get_shutdown_deadline_nsec_fn_priv);
    if (remaining_nsec == 0) return;

    btm_req_params->disassoc_imminent = true;
    btm_req_params->disassoc_timer = ow_steer_executor_action_btm_get_disassoc_timer(btm_action, remaining_nsec);
}

static void
ow_steer_executor_action_btm_fix_disassoc_timer(struct osw_btm_req_params *btm_req_params)
{
    if (btm_req_params->disassoc_imminent == false) return;
    if (btm_req_params->disassoc_timer > 0) return;

    /* At least one known roaming implementation -
     * wpa_supplicant - is looking at the disassoc_timer
     * field and acts on it. It makes sure it kicks off a
     * scan. Given this goes hand in hand with
     * disassoc_imminent it makes sense to make sure the
     * client scans to increase the chance of successful
     * roaming.
     */

    btm_req_params->disassoc_timer = 1;
}

static struct osw_btm_req_params*
ow_steer_executor_action_btm_req_create_params(struct ow_steer_executor_action_btm *btm_action,
                                               const struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(btm_action != NULL, "");
    ASSERT(candidate_list != NULL, "");

    struct osw_btm_req_params btm_req_params;
    memset(&btm_req_params, 0, sizeof(btm_req_params));
    size_t neigh_i = 0;

    size_t cand_i = 0;
    uint8_t btmpreference = 255;
    for (cand_i = 0; cand_i < ow_steer_candidate_list_get_length(candidate_list); cand_i++) {
        if (neigh_i >= OW_STEER_EXECUTOR_ACTION_BTM_NEIGHS_LIMIT)
            break;
        if (btmpreference < 1)
            break;

        const struct ow_steer_candidate *candidate = ow_steer_candidate_list_const_get(candidate_list, cand_i);
        const enum ow_steer_candidate_preference preference = ow_steer_candidate_get_preference(candidate);
        switch (preference) {
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
                /* skip candidate */
                continue;
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                WARN_ON(true);
                continue;
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
                /* skip candidate */
                continue;
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                /* process candidate */
                break;
        };

        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
        const struct osw_channel *channel = osw_bss_get_channel(bssid);
        if (channel == NULL) {
            LOGD("%s bssid: "OSW_HWADDR_FMT" skipped, due to channel: (nil)", ow_steer_executor_action_get_prefix(btm_action->base),
                 OSW_HWADDR_ARG(bssid));
            continue;
        }
        const uint8_t *op_class = osw_bss_get_op_class(bssid);
        if (op_class == NULL) {
            LOGD("%s bssid: "OSW_HWADDR_FMT" skipped, due to op_class: (nil)", ow_steer_executor_action_get_prefix(btm_action->base),
                 OSW_HWADDR_ARG(bssid));
            continue;
        }

        struct osw_btm_req_neigh *btm_neighbor = &btm_req_params.neigh[neigh_i];
        memcpy(&btm_neighbor->bssid, bssid, sizeof(*bssid));
        btm_neighbor->op_class = *op_class;
        btm_neighbor->btmpreference = btmpreference;
        btm_neighbor->channel = osw_freq_to_chan(channel->control_freq_mhz);
        btm_neighbor->bssid_info = OW_STEER_BM_BTM_DEFAULT_NEIGH_BSS_INFO;
        /* TODO Set btm_neighbor->phy_type */

        neigh_i++;
        btmpreference--;
        continue;
    }

    btm_req_params.neigh_len = neigh_i;
    btm_req_params.valid_int = OW_STEER_EXECUTOR_ACTION_BTM_VALID_INT;
    btm_req_params.abridged = OW_STEER_EXECUTOR_ACTION_BTM_AIRBRIDGED;
    btm_req_params.disassoc_imminent = ow_steer_executor_action_btm_get_disassoc_imminent(btm_action);
    btm_req_params.disassoc_timer = OW_STEER_EXECUTOR_ACTION_BTM_DISASSOC_TIMER;
    btm_req_params.bss_term = OW_STEER_EXECUTOR_ACTION_BTM_BSS_TERM;

    ow_steer_executor_action_btm_handle_shutdown_deadline(btm_action, &btm_req_params);
    ow_steer_executor_action_btm_fix_disassoc_timer(&btm_req_params);

    return MEMNDUP(&btm_req_params, sizeof(btm_req_params));
}

static void
ow_steer_executor_action_btm_throttle_timer_cb(struct osw_timer *timer)
{
    struct ow_steer_executor_action_btm *btm_action = container_of(timer, struct ow_steer_executor_action_btm, throttle_timer);
    ow_steer_executor_action_sched_recall(btm_action->base);
}

void
ow_steer_executor_action_btm_set_enabled(struct ow_steer_executor_action_btm *btm_action,
                                         bool enabled)
{
    if (btm_action->enabled == enabled) return;

    LOGD("%s enabled set from %d to %d",
         ow_steer_executor_action_get_prefix(btm_action->base),
         btm_action->enabled,
         enabled);

    btm_action->enabled = enabled;
}

static bool
ow_steer_executor_action_btm_call_fn(struct ow_steer_executor_action *action,
                                     const struct ow_steer_candidate_list *candidate_list,
                                     struct osw_conf_mutator *mutator)
{
    struct ow_steer_executor_action_btm *btm_action = ow_steer_executor_action_get_priv(action);
    const bool need_kick = ow_steer_executor_action_check_kick_needed(action, candidate_list);
    if (need_kick == true) {
        if (btm_action->enabled == false) {
            LOGI("%s kick needed, but btm not supported", ow_steer_executor_action_get_prefix(btm_action->base));
            return true;
        }
        uint64_t next_attempt_tstamp_nsec = 0;
        const bool can_issue_req = osw_throttle_tap(btm_action->throttle, &next_attempt_tstamp_nsec);
        if (can_issue_req == true) {
            struct osw_btm_req_params* btm_req_params = ow_steer_executor_action_btm_req_create_params(btm_action, candidate_list);
            osw_btm_sta_log_req_params(btm_req_params);
            const bool params_set = osw_btm_desc_set_req_params(btm_action->btm_desc, btm_req_params);
            if (params_set == true) {
                LOGI("%s submitting btm req", ow_steer_executor_action_get_prefix(btm_action->base));
                ow_steer_executor_action_notify_data_sent(action);
            }
            else
                LOGI("%s failed to submit btm req", ow_steer_executor_action_get_prefix(btm_action->base));

            FREE(btm_req_params);

            ow_steer_executor_action_sched_recall(btm_action->base);
        }
        else {
            osw_timer_arm_at_nsec(&btm_action->throttle_timer, next_attempt_tstamp_nsec);
            const uint64_t delay_remaining_nsec = osw_timer_get_remaining_nsec(&btm_action->throttle_timer, osw_time_mono_clk());
            LOGD("%s scheduled btm req, remaining delay: %.2lf sec", ow_steer_executor_action_get_prefix(btm_action->base),
                 OSW_TIME_TO_DBL(delay_remaining_nsec));
        }
    }
    else {
        osw_timer_disarm(&btm_action->throttle_timer);
    }
    return true;
}

struct ow_steer_executor_action_btm*
ow_steer_executor_action_btm_create(const struct osw_hwaddr *sta_addr,
                                    const struct ow_steer_executor_action_mediator *mediator)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(mediator != NULL, "");

    const struct ow_steer_executor_action_ops ops = {
        .call_fn = ow_steer_executor_action_btm_call_fn,
    };
    struct ow_steer_executor_action_btm *btm_action = CALLOC(1, sizeof(*btm_action));
    btm_action->btm_desc_observer.req_tx_complete_fn = ow_steer_executor_action_btm_req_tx_complete_cb;
    btm_action->btm_desc_observer.req_tx_error_fn = ow_steer_executor_action_btm_req_tx_error_cb;
    btm_action->btm_desc = osw_btm_get_desc(sta_addr, &btm_action->btm_desc_observer);
    btm_action->throttle = osw_throttle_new_rate_limit(1, OSW_TIME_SEC(OW_STEER_EXECUTOR_ACTION_BTM_RETRY_INTERVAL_SEC));
    osw_timer_init(&btm_action->throttle_timer, ow_steer_executor_action_btm_throttle_timer_cb);

    btm_action->base = ow_steer_executor_action_create("btm", sta_addr, &ops, mediator, btm_action);

    return btm_action;
}

void
ow_steer_executor_action_btm_free(struct ow_steer_executor_action_btm *btm_action)
{
    ASSERT(btm_action != NULL, "");

    ow_steer_executor_action_notify_going_idle(btm_action->base);

    ow_steer_executor_action_btm_set_disassoc_imminent(btm_action, NULL);
    osw_btm_desc_free(btm_action->btm_desc);
    osw_throttle_free(btm_action->throttle);
    osw_timer_disarm(&btm_action->throttle_timer);
    ow_steer_executor_action_free(btm_action->base);
    FREE(btm_action);
}

struct ow_steer_executor_action*
ow_steer_executor_action_btm_get_base(struct ow_steer_executor_action_btm *btm_action)
{
    ASSERT(btm_action != NULL, "");
    return btm_action->base;
}
