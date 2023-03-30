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

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <memutil.h>
#include <const.h>
#include <util.h>
#include <log.h>
#include <ds_dlist.h>
#include <osw_util.h>
#include <osw_types.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_state.h>
#include <osw_stats.h>
#include <osw_stats_defs.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_sta.h"
#include "ow_steer_policy_snr_xing.h"

#define OSW_STEER_POLICY_SNR_XING_SNR_BUF_SIZE 3
#define OSW_STEER_POLICY_SNR_XING_DELTA_BYTES_BUF_SIZE 2
#define OSW_STEER_POLICY_SNR_XING_ENFORCE_PERIOD_SEC 5

enum ow_steer_policy_snr_xing_chnage {
    OW_STEER_POLICY_SNR_XING_CHANGE_UP,
    OW_STEER_POLICY_SNR_XING_CHANGE_DOWN,
    OW_STEER_POLICY_SNR_XING_CHANGE_NONE,
};

enum ow_steer_policy_snr_txrx_state {
    OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE,
    OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE,
};

struct ow_steer_policy_snr_xing_state {
    unsigned int snr[OSW_STEER_POLICY_SNR_XING_SNR_BUF_SIZE];
    struct osw_circ_buf snr_buf;

    uint64_t delta_bytes[OSW_STEER_POLICY_SNR_XING_DELTA_BYTES_BUF_SIZE];
    struct osw_circ_buf delta_bytes_buf;

    bool rrm_neighbor_bcn_act_meas;
    bool wnm_bss_trans;
    struct osw_timer enforce_timer;
    bool enforce_pending;
    enum ow_steer_policy_snr_txrx_state txrx_state;
};

struct ow_steer_policy_snr_xing {
    struct ow_steer_policy *base;
    struct ow_steer_policy_snr_xing_config *next_config;
    struct ow_steer_policy_snr_xing_config *config;
    struct ow_steer_policy_snr_xing_state state;
    struct osw_timer reconf_timer;
    struct osw_state_observer state_observer;
    const struct osw_state_sta_info *sta_info;
};

static const char *g_policy_name = "snr_xing";

static void
ow_steer_policy_snr_xing_state_reset(struct ow_steer_policy_snr_xing *xing_policy)
{
    ASSERT(xing_policy != NULL, "");

    struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;
    const bool enforce_period = osw_timer_is_armed(&state->enforce_timer) == true;

    if (enforce_period == true)
        LOGI("%s enforce period stopped", ow_steer_policy_get_prefix(xing_policy->base));

    osw_circ_buf_init(&state->snr_buf, OSW_STEER_POLICY_SNR_XING_SNR_BUF_SIZE);
    osw_circ_buf_init(&state->delta_bytes_buf, OSW_STEER_POLICY_SNR_XING_DELTA_BYTES_BUF_SIZE);
    state->rrm_neighbor_bcn_act_meas = false;
    state->wnm_bss_trans = false;
    osw_timer_disarm(&state->enforce_timer);
    state->enforce_pending = false;
    state->txrx_state = OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE;

    if (enforce_period == true)
        ow_steer_policy_dismiss_executor(xing_policy->base);
}

static const char*
ow_steer_policy_snr_xing_mode_to_cstr(enum ow_steer_policy_snr_xing_mode mode) {
    switch (mode) {
        case OW_STEER_POLICY_SNR_XING_MODE_HWM:
            return "hmw";
        case OW_STEER_POLICY_SNR_XING_MODE_LWM:
            return "lwm";
        case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
            return "bottom_lwm";
    }

    return "unknown";
}

static const char*
ow_steer_policy_snr_xing_chnage_to_cstr(enum ow_steer_policy_snr_xing_chnage xing_change)
{
    switch (xing_change) {
        case OW_STEER_POLICY_SNR_XING_CHANGE_UP:
            return "up";
        case OW_STEER_POLICY_SNR_XING_CHANGE_DOWN:
            return "down";
        case OW_STEER_POLICY_SNR_XING_CHANGE_NONE:
            return "none";
    }

    return "unknown";
}

static const char*
ow_steer_policy_snr_txrx_state_to_cstr(enum ow_steer_policy_snr_txrx_state txrx_state)
{
    switch (txrx_state) {
        case OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE:
            return "active";
        case OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE:
            return "idle";
    }

    return "unknown";
}

static enum ow_steer_policy_snr_xing_chnage
ow_steer_policy_snr_xing_eval_xing_change(const struct ow_steer_policy_snr_xing *xing_policy)
{
    ASSERT(xing_policy != NULL, "");
    ASSERT(xing_policy->config != NULL, "");

    const struct ow_steer_policy_snr_xing_config *config = xing_policy->config;
    const struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;

    if (osw_circ_buf_is_full(&state->snr_buf) == false)
        return OW_STEER_POLICY_SNR_XING_CHANGE_NONE;

    const size_t prev_i = osw_circ_buf_head(&state->snr_buf);
    const size_t cur_i = osw_circ_buf_next(&state->snr_buf, prev_i);
    const unsigned int prev_snr = state->snr[prev_i];
    const unsigned int cur_snr = state->snr[cur_i];
    const unsigned int threshold = config->snr;

    if (prev_snr < threshold && cur_snr < threshold)
        return OW_STEER_POLICY_SNR_XING_CHANGE_NONE;
    else if (prev_snr >= threshold && cur_snr >= threshold)
        return OW_STEER_POLICY_SNR_XING_CHANGE_NONE;
    else if (prev_snr < threshold && cur_snr >= threshold)
        return OW_STEER_POLICY_SNR_XING_CHANGE_UP;
    else if (prev_snr >= threshold && cur_snr < threshold)
        return OW_STEER_POLICY_SNR_XING_CHANGE_DOWN;
    else
        return WARN_ON(OW_STEER_POLICY_SNR_XING_CHANGE_NONE);
}

static enum ow_steer_policy_snr_txrx_state
ow_steer_policy_snr_xing_eval_txrx_state(const struct ow_steer_policy_snr_xing *xing_policy)
{
    ASSERT(xing_policy != NULL, "");
    ASSERT(xing_policy->config != NULL, "");

    const struct ow_steer_policy_snr_xing_config *config = xing_policy->config;
    const struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;

    uint64_t threshold_delta = 0;
    switch (config->mode) {
        case OW_STEER_POLICY_SNR_XING_MODE_HWM:
            if (config->mode_config.hwm.txrx_bytes_limit.active == false)
                return OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE;
            threshold_delta = config->mode_config.hwm.txrx_bytes_limit.delta;
            break;
        case OW_STEER_POLICY_SNR_XING_MODE_LWM:
            if (config->mode_config.lwm.txrx_bytes_limit.active == false)
                return OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE;
            threshold_delta = config->mode_config.lwm.txrx_bytes_limit.delta;
            break;
        case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
            return OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE;
    }

    if (osw_circ_buf_is_full(&state->delta_bytes_buf) == false)
        return OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE;

    const size_t cur_i = osw_circ_buf_head(&state->delta_bytes_buf);
    const uint64_t cur_delta_bytes = state->delta_bytes[cur_i];
    if (cur_delta_bytes < threshold_delta)
        return OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE;
    else
        return OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE;
}

static bool
ow_steer_policy_snr_xing_check_sta_caps(struct ow_steer_policy_snr_xing *xing_policy)
{
    ASSERT(xing_policy != NULL, "");
    ASSERT(xing_policy->config != NULL, "");

    const struct ow_steer_policy_snr_xing_config *config = xing_policy->config;
    const struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;

    bool sta_caps_ok = false;
    switch (config->mode) {
        case OW_STEER_POLICY_SNR_XING_MODE_HWM:
            sta_caps_ok = true;
            break;
        case OW_STEER_POLICY_SNR_XING_MODE_LWM:
            sta_caps_ok = true;
            break;
        case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
            if (state->rrm_neighbor_bcn_act_meas == false && state->wnm_bss_trans == false)
                sta_caps_ok = true;
            break;
    }

    return sta_caps_ok;
}

static void
ow_steer_policy_snr_xing_recalc(struct ow_steer_policy_snr_xing *xing_policy)
{
    ASSERT(xing_policy != NULL, "");

    if (xing_policy->sta_info == NULL)
        return;

    const struct ow_steer_policy_snr_xing_config *config = xing_policy->config;
    if (config == NULL)
        return;

    struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;
    const bool enforce_in_progress = osw_timer_is_armed(&state->enforce_timer);
    if (enforce_in_progress == true)
        return;

    const bool sta_caps_ok = ow_steer_policy_snr_xing_check_sta_caps(xing_policy);
    if (sta_caps_ok == false)
        return;

    const bool prev_enforce_pending = state->enforce_pending;
    const enum ow_steer_policy_snr_xing_chnage xing_change = ow_steer_policy_snr_xing_eval_xing_change(xing_policy);

    switch (xing_change) {
        case OW_STEER_POLICY_SNR_XING_CHANGE_UP:
            switch (config->mode) {
                case OW_STEER_POLICY_SNR_XING_MODE_HWM:
                    state->enforce_pending = true;
                    break;
                case OW_STEER_POLICY_SNR_XING_MODE_LWM:
                    state->enforce_pending = false;
                    break;
                case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
                    state->enforce_pending = false;
                    break;
            }
            break;
        case OW_STEER_POLICY_SNR_XING_CHANGE_DOWN:
            switch (config->mode) {
                case OW_STEER_POLICY_SNR_XING_MODE_HWM:
                    state->enforce_pending = false;
                    break;
                case OW_STEER_POLICY_SNR_XING_MODE_LWM:
                    state->enforce_pending = true;
                    break;
                case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
                    state->enforce_pending = true;
                    break;
            }
            break;
        case OW_STEER_POLICY_SNR_XING_CHANGE_NONE:
            /* nop */
            break;
    }

    const bool enforce_pending_changed = prev_enforce_pending != state->enforce_pending;
    if (enforce_pending_changed == true)
        LOGI("%s enforce pending: %s -> %s", ow_steer_policy_get_prefix(xing_policy->base),
             prev_enforce_pending == true ? "true" : "false", state->enforce_pending == true ? "true" : "false");

    const enum ow_steer_policy_snr_txrx_state prev_txrx_state = state->txrx_state;
    state->txrx_state = ow_steer_policy_snr_xing_eval_txrx_state(xing_policy);

    const bool txrx_state_changed = prev_txrx_state != state->txrx_state;
    if (txrx_state_changed == true)
        LOGD("%s txrx state: %s -> %s", ow_steer_policy_get_prefix(xing_policy->base),
             ow_steer_policy_snr_txrx_state_to_cstr(prev_txrx_state), ow_steer_policy_snr_txrx_state_to_cstr(state->txrx_state));

    switch (config->mode) {
        case OW_STEER_POLICY_SNR_XING_MODE_HWM:
            if (state->txrx_state == OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE)
                return;
            break;
        case OW_STEER_POLICY_SNR_XING_MODE_LWM:
            if (state->txrx_state == OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE)
                return;
            break;
        case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
            /* no TX/RX delta limit */
            break;
    }

    if (state->enforce_pending == true) {
        state->enforce_pending = false;

        const uint64_t tstamp_nsec = osw_time_mono_clk() + OSW_TIME_SEC(OSW_STEER_POLICY_SNR_XING_ENFORCE_PERIOD_SEC);
        osw_timer_arm_at_nsec(&state->enforce_timer, tstamp_nsec);
        ow_steer_policy_trigger_executor(xing_policy->base);
        ow_steer_policy_schedule_stack_recalc(xing_policy->base);
        LOGI("%s enforce period started", ow_steer_policy_get_prefix(xing_policy->base));
    }
}

static void
ow_steer_policy_snr_xing_recalc_cb(struct ow_steer_policy *policy,
                                   struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(policy != NULL, "");
    ASSERT(candidate_list != NULL, "");

    struct ow_steer_policy_snr_xing *xing_policy = ow_steer_policy_get_priv(policy);

    /* No config -> nop */
    const struct ow_steer_policy_snr_xing_config *config = xing_policy->config;
    if (config == NULL)
        return;

    if (xing_policy->sta_info == NULL)
        return;

    struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;
    const bool enforce_period = osw_timer_is_armed(&state->enforce_timer) == true;
    if (enforce_period == false)
        return;

    struct ow_steer_candidate *blocked_candidate = ow_steer_candidate_list_lookup(candidate_list, &config->bssid);
    const bool blocked_candidate_exists = blocked_candidate != NULL;
    if (blocked_candidate_exists == false)
        LOGI("%s bssid: "OSW_HWADDR_FMT" preference: (nil), doesnt exist", ow_steer_policy_get_prefix(policy), OSW_HWADDR_ARG(&config->bssid));

    const enum ow_steer_candidate_preference blocked_candidate_pref = ow_steer_candidate_get_preference(blocked_candidate);
    bool block_cancicdate_blockable = true;
    if (blocked_candidate_exists == true) {
        switch (blocked_candidate_pref) {
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                block_cancicdate_blockable = false;
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                block_cancicdate_blockable = true;
                break;
        }
    }

    if (block_cancicdate_blockable == false)
        LOGI("%s bssid: "OSW_HWADDR_FMT" preference: %s, cannot block candidate", ow_steer_policy_get_prefix(policy), OSW_HWADDR_ARG(&config->bssid),
             ow_steer_candidate_preference_to_cstr(blocked_candidate_pref));

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
        if (enforce_period == true)
            LOGI("%s enforce period stopped", ow_steer_policy_get_prefix(xing_policy->base));
        if (state->enforce_pending == true)
            LOGI("%s pending enforce period cancelled", ow_steer_policy_get_prefix(xing_policy->base));

        osw_timer_disarm(&state->enforce_timer);
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
ow_steer_policy_snr_xing_sta_connected_cb(struct osw_state_observer *observer,
                                          const struct osw_state_sta_info *sta_info)
{
    struct ow_steer_policy_snr_xing *xing_policy = container_of(observer, struct ow_steer_policy_snr_xing, state_observer);

    const struct osw_hwaddr *policy_sta_addr = ow_steer_policy_get_sta_addr(xing_policy->base);
    if (osw_hwaddr_cmp(sta_info->mac_addr, policy_sta_addr) != 0)
        return;

    /* No config -> nop */
    const struct ow_steer_policy_snr_xing_config *config = xing_policy->config;
    const struct osw_hwaddr *vif_bssid = &sta_info->vif->drv_state->mac_addr;
    if (config == NULL) {
        LOGD("%s sta connected to vif bssid: "OSW_HWADDR_FMT", (no conf)", ow_steer_policy_get_prefix(xing_policy->base), OSW_HWADDR_ARG(vif_bssid));
        return;
    }

    const struct osw_hwaddr *policy_bssid = &config->bssid;
    if (osw_hwaddr_cmp(vif_bssid, policy_bssid) != 0)
        return;

    xing_policy->sta_info = sta_info;

    struct osw_assoc_req_info assoc_req_info;
    const char *extra_log = "";
    const bool parsed = osw_parse_assoc_req_ies(sta_info->assoc_req_ies, sta_info->assoc_req_ies_len, &assoc_req_info);
    if (parsed == false)
        extra_log = ", failed to parse assoc req ies, assume non-rrm_neighbor_bcn_act_meas non-wnm_bss_trans";

    struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;
    if (assoc_req_info.wnm_bss_trans == true)
        state->wnm_bss_trans = true;
    if (assoc_req_info.rrm_neighbor_bcn_act_meas == true)
        state->rrm_neighbor_bcn_act_meas = true;

    LOGI("%s sta connected, rrm_neighbor_bcn_act_meas: %s wnm_bss_trans: %s%s", ow_steer_policy_get_prefix(xing_policy->base),
         state->rrm_neighbor_bcn_act_meas == true ? "true" : "false", state->wnm_bss_trans == true ? "true" : "false",
         extra_log);
}

static void
ow_steer_policy_snr_xing_sta_disconnected_cb(struct osw_state_observer *observer,
                                             const struct osw_state_sta_info *sta_info)
{
    struct ow_steer_policy_snr_xing *xing_policy = container_of(observer, struct ow_steer_policy_snr_xing, state_observer);

    const struct osw_hwaddr *policy_sta_addr = ow_steer_policy_get_sta_addr(xing_policy->base);
    if (osw_hwaddr_cmp(sta_info->mac_addr, policy_sta_addr) != 0)
        return;

    /* No config -> nop */
    const struct ow_steer_policy_snr_xing_config *config = xing_policy->config;
    const struct osw_hwaddr *vif_bssid = &sta_info->vif->drv_state->mac_addr;
    if (config == NULL) {
        LOGD("%s sta disconnected from vif bssid: "OSW_HWADDR_FMT", (no conf)", ow_steer_policy_get_prefix(xing_policy->base), OSW_HWADDR_ARG(vif_bssid));
        return;
    }

    const struct osw_hwaddr *policy_bssid = &config->bssid;
    if (osw_hwaddr_cmp(vif_bssid, policy_bssid) != 0)
        return;

    WARN_ON(xing_policy->sta_info != sta_info);
    xing_policy->sta_info = NULL;

    ow_steer_policy_snr_xing_state_reset(xing_policy);
    ow_steer_policy_schedule_stack_recalc(xing_policy->base);

    LOGI("%s sta disconnected", ow_steer_policy_get_prefix(xing_policy->base));
}

static void
ow_steer_policy_snr_xing_sta_snr_change_cb(struct ow_steer_policy *policy,
                                           const struct osw_hwaddr *bssid,
                                           uint32_t snr_db)
{
    ASSERT(policy != NULL, "");
    ASSERT(bssid != NULL, "");

    struct ow_steer_policy_snr_xing *xing_policy = ow_steer_policy_get_priv(policy);
    struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;

    const size_t i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = snr_db;

    if (osw_circ_buf_is_full(&state->snr_buf) == true) {
        const enum ow_steer_policy_snr_xing_chnage xing_change = ow_steer_policy_snr_xing_eval_xing_change(xing_policy);
        if (xing_change != OW_STEER_POLICY_SNR_XING_CHANGE_NONE)
            LOGD("%s xing changed: %s", ow_steer_policy_get_prefix(xing_policy->base), ow_steer_policy_snr_xing_chnage_to_cstr(xing_change));
    }

    ow_steer_policy_snr_xing_recalc(xing_policy);
}

static void
ow_steer_policy_snr_xing_sta_data_vol_change_cb(struct ow_steer_policy *policy,
                                                const struct osw_hwaddr *bssid,
                                                uint64_t data_vol_bytes)
{
    ASSERT(policy != NULL, "");
    ASSERT(bssid != NULL, "");

    struct ow_steer_policy_snr_xing *xing_policy = ow_steer_policy_get_priv(policy);
    struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;

    const size_t i = osw_circ_buf_push_rotate(&state->delta_bytes_buf);
    state->delta_bytes[i] = data_vol_bytes;

    ow_steer_policy_snr_xing_recalc(xing_policy);
}

static void
ow_steer_policy_snr_xing_sigusr1_dump_cb(struct ow_steer_policy *policy)
{
    ASSERT(policy != NULL, "");

    struct ow_steer_policy_snr_xing *xing_policy = ow_steer_policy_get_priv(policy);
    const struct ow_steer_policy_snr_xing_config *config = xing_policy->config;
    const struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;
    size_t i = 0;
    char *buf = NULL;

    LOGI("ow: steer:         sta_info: %s", xing_policy->sta_info != NULL ? "set" : "not set");
    LOGI("ow: steer:         config: %s", config != NULL ? "" : "(nil)");

    if (config != NULL) {
        LOGI("ow: steer:           bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&config->bssid));
        LOGI("ow: steer:           mode: %s", ow_steer_policy_snr_xing_mode_to_cstr(config->mode));
        LOGI("ow: steer:           snr: %u", config->snr);

        switch (config->mode) {
            case OW_STEER_POLICY_SNR_XING_MODE_HWM:
                LOGI("ow: steer:           mode_config:");
                LOGI("ow: steer:             hwm:");
                LOGI("ow: steer:               txrx_bytes_limit");
                LOGI("ow: steer:                 active: %s", config->mode_config.hwm.txrx_bytes_limit.active == true ? "true" : "false");
                LOGI("ow: steer:                 delta: %"PRIu64, config->mode_config.hwm.txrx_bytes_limit.delta);
                break;
            case OW_STEER_POLICY_SNR_XING_MODE_LWM:
                LOGI("ow: steer:           mode_config:");
                LOGI("ow: steer:             lwm:");
                LOGI("ow: steer:               txrx_bytes_limit");
                LOGI("ow: steer:                 active: %s", config->mode_config.lwm.txrx_bytes_limit.active == true ? "true" : "false");
                LOGI("ow: steer:                 delta: %"PRIu64, config->mode_config.lwm.txrx_bytes_limit.delta);
                break;
            case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
                LOGI("ow: steer:           mode_config:");
                LOGI("ow: steer:             bottom_lwm:");
                break;
        }
    }

    LOGI("ow: steer:         state:");

    OSW_CIRC_BUF_FOREACH(&state->snr_buf, i)
        strgrow(&buf, "%s%u", buf != NULL ? " " : "", state->snr[i]);

    LOGI("ow: steer:           snr: %s", buf);
    FREE(buf);
    buf = NULL;

    LOGI("ow: steer:           rrm_neighbor_bcn_act_meas: %s", state->rrm_neighbor_bcn_act_meas == true ? "true" : "false");
    LOGI("ow: steer:           wnm_bss_trans: %s", state->wnm_bss_trans == true ? "true" : "false");

    OSW_CIRC_BUF_FOREACH(&state->snr_buf, i)
        strgrow(&buf, "%s%"PRIu64, buf != NULL ? " " : "", state->delta_bytes[i]);

    LOGI("ow: steer:           delta_bytes: %s", buf);
    FREE(buf);
    buf = NULL;
}

static void
ow_steer_policy_snr_xing_reconf_timer_cb(struct osw_timer *timer)
{
    ASSERT(timer != NULL, "");

    struct ow_steer_policy_snr_xing *xing_policy = container_of(timer, struct ow_steer_policy_snr_xing, reconf_timer);

    bool unregister_observer = false;
    bool register_observer = false;

    if (xing_policy->config == NULL && xing_policy->next_config == NULL) {
        /* nop */
        return;
    }
    else if (xing_policy->config == NULL && xing_policy->next_config != NULL) {
        LOGI("%s config added", ow_steer_policy_get_prefix(xing_policy->base));
        register_observer = true;
    }
    else if (xing_policy->config != NULL && xing_policy->next_config == NULL) {
        LOGI("%s config removed", ow_steer_policy_get_prefix(xing_policy->base));
        unregister_observer = true;
    }
    else if (xing_policy->config != NULL && xing_policy->next_config != NULL) {
        if (memcmp(xing_policy->config, xing_policy->next_config, sizeof(*xing_policy->config)) == 0) {
            FREE(xing_policy->next_config);
            xing_policy->next_config = NULL;
            return;
        }

        LOGI("%s config changed", ow_steer_policy_get_prefix(xing_policy->base));
        unregister_observer = true;
        register_observer = true;
    }
    else {
        ASSERT(false, "");
    }

    const bool reset_policy_state = unregister_observer || register_observer;
    if (reset_policy_state == true) {
        ow_steer_policy_snr_xing_state_reset(xing_policy);
        ow_steer_policy_schedule_stack_recalc(xing_policy->base);
    }

    FREE(xing_policy->config);
    xing_policy->config = NULL;
    if (unregister_observer == true)
        osw_state_unregister_observer(&xing_policy->state_observer);

    xing_policy->config = xing_policy->next_config;
    xing_policy->next_config = NULL;

    const struct osw_hwaddr *bssid = xing_policy->config != NULL ? &xing_policy->config->bssid : NULL;
    ow_steer_policy_set_bssid(xing_policy->base, bssid);

    if (register_observer == true)
        osw_state_register_observer(&xing_policy->state_observer);
}

static void
ow_steer_policy_snr_xing_enforce_timer_cb(struct osw_timer *timer)
{
    struct ow_steer_policy_snr_xing *xing_policy = container_of(timer, struct ow_steer_policy_snr_xing, state.enforce_timer);

    ow_steer_policy_snr_xing_state_reset(xing_policy);
    LOGI("%s enforce period finished", ow_steer_policy_get_prefix(xing_policy->base));
    ow_steer_policy_dismiss_executor(xing_policy->base);
    ow_steer_policy_schedule_stack_recalc(xing_policy->base);
}

struct ow_steer_policy_snr_xing*
ow_steer_policy_snr_xing_create(unsigned int priority,
                                const char *name,
                                const struct osw_hwaddr *sta_addr,
                                const struct ow_steer_policy_mediator *mediator)
{
    ASSERT(name != NULL, "");
    ASSERT(sta_addr != NULL, "");
    ASSERT(mediator != NULL, "");

    const struct ow_steer_policy_ops policy_ops = {
        .sigusr1_dump_fn = ow_steer_policy_snr_xing_sigusr1_dump_cb,
        .recalc_fn = ow_steer_policy_snr_xing_recalc_cb,
        .sta_snr_change_fn = ow_steer_policy_snr_xing_sta_snr_change_cb,
        .sta_data_vol_change_fn = ow_steer_policy_snr_xing_sta_data_vol_change_cb,
    };
    const struct osw_state_observer state_observer = {
        .name = g_policy_name,
        .sta_connected_fn = ow_steer_policy_snr_xing_sta_connected_cb,
        .sta_disconnected_fn = ow_steer_policy_snr_xing_sta_disconnected_cb,
    };

    struct ow_steer_policy_snr_xing *xing_policy = CALLOC(1, sizeof(*xing_policy));
    memcpy(&xing_policy->state_observer, &state_observer, sizeof(xing_policy->state_observer));
    osw_timer_init(&xing_policy->reconf_timer, ow_steer_policy_snr_xing_reconf_timer_cb);

    struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;
    osw_timer_init(&state->enforce_timer, ow_steer_policy_snr_xing_enforce_timer_cb);

    xing_policy->base = ow_steer_policy_create(strfmta("%s_%s", g_policy_name, name), priority, sta_addr, &policy_ops, mediator, xing_policy);

    return xing_policy;
}

void
ow_steer_policy_snr_xing_free(struct ow_steer_policy_snr_xing *xing_policy)
{
    if (xing_policy == NULL)
        return;

    const bool unregister_observer = xing_policy->config != NULL;

    ow_steer_policy_snr_xing_state_reset(xing_policy);
    FREE(xing_policy->next_config);
    xing_policy->next_config = NULL;
    FREE(xing_policy->config);
    xing_policy->config = NULL;
    if (unregister_observer == true)
        osw_state_unregister_observer(&xing_policy->state_observer);
    ow_steer_policy_free(xing_policy->base);
    FREE(xing_policy);
}

struct ow_steer_policy*
ow_steer_policy_snr_xing_get_base(struct ow_steer_policy_snr_xing *xing_policy)
{
    ASSERT(xing_policy != NULL, "");
    return xing_policy->base;
}

void
ow_steer_policy_snr_xing_set_config(struct ow_steer_policy_snr_xing *xing_policy,
                                    struct ow_steer_policy_snr_xing_config *config)
{
    ASSERT(xing_policy != NULL, "");

    FREE(xing_policy->next_config);
    xing_policy->next_config = config;

    osw_timer_arm_at_nsec(&xing_policy->reconf_timer, 0);
}

#include "ow_steer_policy_snr_xing_ut.c"
