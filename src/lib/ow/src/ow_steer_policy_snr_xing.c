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
#include <limits.h>
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
#include <osw_diag.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_sta.h"
#include "ow_steer_policy_snr_xing.h"

#define OSW_STEER_POLICY_SNR_XING_SNR_BUF_SIZE 3
#define OSW_STEER_POLICY_SNR_XING_DELTA_BYTES_BUF_SIZE 2
#define OSW_STEER_POLICY_SNR_XING_ENFORCE_PERIOD_SEC 5

#define LOG_PREFIX(fmt, ...) "ow: steer: " fmt, ##__VA_ARGS__
#define LOG_WITH_PREFIX(prefix, fmt, ...) \
    LOG_PREFIX(                           \
        "%s" fmt,                        \
        prefix,                           \
        ##__VA_ARGS__)

#define LOG_WITH_POLICY_PREFIX(policy, fmt, ...) \
    LOG_WITH_PREFIX(                             \
        ow_steer_policy_get_prefix(policy),      \
        fmt,                                     \
        ##__VA_ARGS__)


enum ow_steer_policy_snr_xing_chnage {
    OW_STEER_POLICY_SNR_XING_CHANGE_UP,
    OW_STEER_POLICY_SNR_XING_CHANGE_DOWN,
    OW_STEER_POLICY_SNR_XING_CHANGE_NONE,
};

enum ow_steer_policy_snr_txrx_state {
    OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE,
    OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE,
};

/* Once a client is seen as active keep it as such
 * for at least X seconds. This mitigates possible
 * flapping when client comes in and out of
 * activity with periodic, but bursty traffic.
 */
#define OW_STEER_POLICY_SNR_XING_ACTIVITY_GRACE_SEC 5

typedef void ow_steer_policy_snr_xing_activity_fn_t(void *priv);

enum ow_steer_policy_snr_xing_activity_state {
    OW_STEER_POLICY_SNR_XING_ACTIVITY_NOT_CONFIGURED,
    OW_STEER_POLICY_SNR_XING_ACTIVITY_NOT_READY_YET,
    OW_STEER_POLICY_SNR_XING_ACTIVITY_IDLE,
    OW_STEER_POLICY_SNR_XING_ACTIVITY_ACTIVE,
};

/* This is designed so that once activity
 * threshold is exceeded, it starts a timer. The
 * timer is re-armed if another exceed event
 * happens before timer expiry. This helps
 * avoiding flapping of idle/active when a device
 * has bursty traffic.
 */
struct ow_steer_policy_snr_xing_activity {
    struct osw_timer active;

    /* This accumulates bytes within a collect
     * period. In practice this tracks "bytes per
     * second".
     */
    struct osw_timer collect;
    int bytes;

    ow_steer_policy_snr_xing_activity_fn_t *idle_cb;
    ow_steer_policy_snr_xing_activity_fn_t *active_cb;
    void *priv;
    int grace_seconds;
    int threshold;
};

struct ow_steer_policy_snr_xing_state {
    unsigned int snr[OSW_STEER_POLICY_SNR_XING_SNR_BUF_SIZE];
    struct osw_circ_buf snr_buf;

    struct ow_steer_policy_snr_xing_activity activity;

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

static void
int_saturating_add_u64(int *x, uint64_t y)
{
    if (y > INT_MAX) y = INT_MAX;
    *x += y;
    if (y > 0 && *x < 0) *x = INT_MAX;
}

static void
ow_steer_policy_snr_xing_activity_feed(struct ow_steer_policy_snr_xing_activity *a,
                                       uint64_t bytes)
{
    int_saturating_add_u64(&a->bytes, bytes);
    if (osw_timer_is_armed(&a->collect)) return;
    const uint64_t at = osw_time_mono_clk() + OSW_TIME_SEC(1);
    osw_timer_arm_at_nsec(&a->collect, at);
}

static void
ow_steer_policy_snr_xing_activity_arm_active(struct ow_steer_policy_snr_xing_activity *a)
{
    if (osw_timer_is_armed(&a->active) == false) {
        a->active_cb(a->priv);
    }
    const uint64_t at = osw_time_mono_clk() + OSW_TIME_SEC(a->grace_seconds);
    osw_timer_arm_at_nsec(&a->active, at);
}

static void
ow_steer_policy_snr_xing_activity_collect_cb(struct osw_timer *t)
{
    struct ow_steer_policy_snr_xing_activity *a = container_of(t, struct ow_steer_policy_snr_xing_activity, collect);
    const bool starting_up = (a->bytes < 0);
    const bool threshold_is_configured = (a->threshold >= 0);

    if (threshold_is_configured && (a->bytes >= a->threshold)) {
        ow_steer_policy_snr_xing_activity_arm_active(a);
    }
    else if (starting_up) {
        a->idle_cb(a->priv);
    }

    a->bytes = 0;
}

static const char *
ow_steer_policy_snr_xing_activity_state_to_cstr(enum ow_steer_policy_snr_xing_activity_state s)
{
    switch (s) {
        case OW_STEER_POLICY_SNR_XING_ACTIVITY_NOT_CONFIGURED: return "not configured";
        case OW_STEER_POLICY_SNR_XING_ACTIVITY_NOT_READY_YET: return "not ready yet";
        case OW_STEER_POLICY_SNR_XING_ACTIVITY_IDLE: return "idle";
        case OW_STEER_POLICY_SNR_XING_ACTIVITY_ACTIVE: return "active";
    }
    return "";
}

static enum ow_steer_policy_snr_xing_activity_state
ow_steer_policy_snr_xing_activity_get_state(const struct ow_steer_policy_snr_xing_activity *a)
{
    if (a->threshold < 0) return OW_STEER_POLICY_SNR_XING_ACTIVITY_NOT_CONFIGURED;
    if (a->bytes < 0) return OW_STEER_POLICY_SNR_XING_ACTIVITY_NOT_READY_YET;
    if (osw_timer_is_armed(&a->active)) return OW_STEER_POLICY_SNR_XING_ACTIVITY_ACTIVE;
    return OW_STEER_POLICY_SNR_XING_ACTIVITY_IDLE;
}

static int
ow_steer_policy_snr_xing_activity_get_threshold(const struct ow_steer_policy_snr_xing_activity *a)
{
    return a->threshold;
}

static int
ow_steer_policy_snr_xing_activity_get_grace_seconds(const struct ow_steer_policy_snr_xing_activity *a)
{
    return a->grace_seconds;
}

static void
ow_steer_policy_snr_xing_activity_active_cb(struct osw_timer *t)
{
    struct ow_steer_policy_snr_xing_activity *a = container_of(t, struct ow_steer_policy_snr_xing_activity, active);
    a->idle_cb(a->priv);
}

static void
ow_steer_policy_snr_xing_activity_set_threshold(struct ow_steer_policy_snr_xing_activity *a,
                                                int threshold)
{
    a->threshold = threshold;
}

static void
ow_steer_policy_snr_xing_activity_init(struct ow_steer_policy_snr_xing_activity *a,
                                       ow_steer_policy_snr_xing_activity_fn_t *idle_cb,
                                       ow_steer_policy_snr_xing_activity_fn_t *active_cb,
                                       void *priv)
{
    osw_timer_init(&a->active, ow_steer_policy_snr_xing_activity_active_cb);
    osw_timer_init(&a->collect, ow_steer_policy_snr_xing_activity_collect_cb);
    a->idle_cb = idle_cb;
    a->active_cb = active_cb;
    a->priv = priv;
    a->bytes = -1;
    a->grace_seconds = OW_STEER_POLICY_SNR_XING_ACTIVITY_GRACE_SEC;
}

static void
ow_steer_policy_snr_xing_activity_fini(struct ow_steer_policy_snr_xing_activity *a)
{
    osw_timer_disarm(&a->active);
    osw_timer_disarm(&a->collect);
    a->bytes = -1;
}

static const char *g_policy_name = "snr_xing";

static void
ow_steer_policy_snr_xing_state_reset(struct ow_steer_policy_snr_xing *xing_policy)
{
    ASSERT(xing_policy != NULL, "");

    struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;
    const bool enforce_period = osw_timer_is_armed(&state->enforce_timer) == true;

    if (enforce_period == true)
        LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "enforce period stopped"));

    osw_circ_buf_init(&state->snr_buf, OSW_STEER_POLICY_SNR_XING_SNR_BUF_SIZE);
    ow_steer_policy_snr_xing_activity_fini(&state->activity);
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

    const struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;

    switch (ow_steer_policy_snr_xing_activity_get_state(&state->activity)) {
        case OW_STEER_POLICY_SNR_XING_ACTIVITY_NOT_CONFIGURED: return OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE;
        case OW_STEER_POLICY_SNR_XING_ACTIVITY_NOT_READY_YET: return OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE;
        case OW_STEER_POLICY_SNR_XING_ACTIVITY_IDLE: return OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE;
        case OW_STEER_POLICY_SNR_XING_ACTIVITY_ACTIVE: return OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE;
    }

    return OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE;
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
        LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "enforce pending: %s -> %s",
             prev_enforce_pending == true ? "true" : "false", state->enforce_pending == true ? "true" : "false"));

    const enum ow_steer_policy_snr_txrx_state prev_txrx_state = state->txrx_state;
    state->txrx_state = ow_steer_policy_snr_xing_eval_txrx_state(xing_policy);

    const bool txrx_state_changed = prev_txrx_state != state->txrx_state;
    if (txrx_state_changed == true)
        LOGD(LOG_WITH_POLICY_PREFIX(xing_policy->base, "txrx state: %s -> %s",
             ow_steer_policy_snr_txrx_state_to_cstr(prev_txrx_state), ow_steer_policy_snr_txrx_state_to_cstr(state->txrx_state)));

    switch (config->mode) {
        case OW_STEER_POLICY_SNR_XING_MODE_HWM:
            if (state->txrx_state == OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE)
                return;
            break;
        case OW_STEER_POLICY_SNR_XING_MODE_LWM:
            /* Workaround: Original intention for
             * activity-based deferal was targeting
             * deauth-type kicks only. BTM should not be
             * affected by this.
             *
             * However OW design is to keep policies and
             * executors separate. A proper fix needs to
             * move activity deferral to the executor
             * itself, along with Bottom LWM which works as
             * an exception against the deferral. The fix is
             * on the way, but carries a risk of regression
             * that can't be taken now.
             *
             * This assumes that if a client is found to
             * supoprt BTM then BTM executor will actually
             * be run. This is always true right now,
             * although it doesn't need to be in the future.
             */
            if (state->wnm_bss_trans)
                break;

            /* Otherwise (client does not support BTM and
             * needs Deauth to be done to move it away)
             * defer policing if client is active and it's
             * desired for it to not be active.
             */
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
        LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "enforce period started"));
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
        LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "bssid: "OSW_HWADDR_FMT" preference: (nil), doesnt exist", OSW_HWADDR_ARG(&config->bssid)));

    bool block_cancicdate_blockable = true;
    if (blocked_candidate_exists == true) {
        const enum ow_steer_candidate_preference blocked_candidate_pref = ow_steer_candidate_get_preference(blocked_candidate);
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
        if (block_cancicdate_blockable == false) {
            LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "bssid: "OSW_HWADDR_FMT" preference: %s, cannot block candidate", OSW_HWADDR_ARG(&config->bssid),
                 ow_steer_candidate_preference_to_cstr(blocked_candidate_pref)));
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
        LOGI(LOG_WITH_POLICY_PREFIX(policy, "no other candidates available"));

    const bool continue_enforce = blocked_candidate_exists == true &&
                                  block_cancicdate_blockable == true &&
                                  any_available_candidates == true;

    if (continue_enforce == false) {
        if (enforce_period == true)
            LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "enforce period stopped"));
        if (state->enforce_pending == true)
            LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "pending enforce period cancelled"));

        osw_timer_disarm(&state->enforce_timer);
        return;
    }

    for (i = 0; i < ow_steer_candidate_list_get_length(candidate_list); i++) {
        struct ow_steer_candidate *candidate = ow_steer_candidate_list_get(candidate_list, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(candidate);
        const enum ow_steer_candidate_preference candidate_pref = ow_steer_candidate_get_preference(candidate);

        if (candidate_pref != OW_STEER_CANDIDATE_PREFERENCE_NONE)
            continue;

        const char *reason = ow_steer_policy_get_name(policy);
        if (blocked_candidate == candidate)
            ow_steer_candidate_set_preference(candidate, reason, OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
        else
            ow_steer_candidate_set_preference(candidate, reason, OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

        LOGD(LOG_WITH_POLICY_PREFIX(xing_policy->base, "bssid: "OSW_HWADDR_FMT" preference: %s", OSW_HWADDR_ARG(bssid),
             ow_steer_candidate_preference_to_cstr(ow_steer_candidate_get_preference(candidate))));
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
        LOGD(LOG_WITH_POLICY_PREFIX(xing_policy->base, "sta connected to vif bssid: "OSW_HWADDR_FMT", (no conf)", OSW_HWADDR_ARG(vif_bssid)));
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

    LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "sta connected, rrm_neighbor_bcn_act_meas: %s, wnm_bss_trans: %s%s",
         state->rrm_neighbor_bcn_act_meas == true ? "true" : "false", state->wnm_bss_trans == true ? "true" : "false",
         extra_log));
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
        LOGD(LOG_WITH_POLICY_PREFIX(xing_policy->base, "sta disconnected from vif bssid: "OSW_HWADDR_FMT", (no conf)", OSW_HWADDR_ARG(vif_bssid)));
        return;
    }

    const struct osw_hwaddr *policy_bssid = &config->bssid;
    if (osw_hwaddr_cmp(vif_bssid, policy_bssid) != 0)
        return;

    WARN_ON(xing_policy->sta_info != sta_info);
    xing_policy->sta_info = NULL;

    ow_steer_policy_snr_xing_state_reset(xing_policy);
    ow_steer_policy_schedule_stack_recalc(xing_policy->base);

    LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "sta disconnected"));
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
            LOGD(LOG_WITH_POLICY_PREFIX(xing_policy->base, "xing changed: %s", ow_steer_policy_snr_xing_chnage_to_cstr(xing_change)));
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

    ow_steer_policy_snr_xing_activity_feed(&state->activity, data_vol_bytes);
}

static void
ow_steer_policy_snr_xing_sigusr1_dump_cb(osw_diag_pipe_t *pipe,
                                         struct ow_steer_policy *policy)
{
    ASSERT(policy != NULL, "");

    struct ow_steer_policy_snr_xing *xing_policy = ow_steer_policy_get_priv(policy);
    const struct ow_steer_policy_snr_xing_config *config = xing_policy->config;
    const struct ow_steer_policy_snr_xing_state *state = &xing_policy->state;
    size_t i = 0;
    char *buf = NULL;

    osw_diag_pipe_writef(pipe, "ow: steer:         sta_info: %s", xing_policy->sta_info != NULL ? "set" : "not set");
    osw_diag_pipe_writef(pipe, "ow: steer:         config: %s", config != NULL ? "" : "(nil)");

    if (config != NULL) {
        osw_diag_pipe_writef(pipe, "ow: steer:           bssid: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&config->bssid));
        osw_diag_pipe_writef(pipe, "ow: steer:           mode: %s", ow_steer_policy_snr_xing_mode_to_cstr(config->mode));
        osw_diag_pipe_writef(pipe, "ow: steer:           snr: %u", config->snr);

        switch (config->mode) {
            case OW_STEER_POLICY_SNR_XING_MODE_HWM:
                osw_diag_pipe_writef(pipe, "ow: steer:           mode_config:");
                osw_diag_pipe_writef(pipe, "ow: steer:             hwm:");
                osw_diag_pipe_writef(pipe, "ow: steer:               txrx_bytes_limit");
                osw_diag_pipe_writef(pipe, "ow: steer:                 active: %s", config->mode_config.hwm.txrx_bytes_limit.active == true ? "true" : "false");
                osw_diag_pipe_writef(pipe, "ow: steer:                 delta: %"PRIu64, config->mode_config.hwm.txrx_bytes_limit.delta);
                break;
            case OW_STEER_POLICY_SNR_XING_MODE_LWM:
                osw_diag_pipe_writef(pipe, "ow: steer:           mode_config:");
                osw_diag_pipe_writef(pipe, "ow: steer:             lwm:");
                osw_diag_pipe_writef(pipe, "ow: steer:               txrx_bytes_limit");
                osw_diag_pipe_writef(pipe, "ow: steer:                 active: %s", config->mode_config.lwm.txrx_bytes_limit.active == true ? "true" : "false");
                osw_diag_pipe_writef(pipe, "ow: steer:                 delta: %"PRIu64, config->mode_config.lwm.txrx_bytes_limit.delta);
                break;
            case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
                osw_diag_pipe_writef(pipe, "ow: steer:           mode_config:");
                osw_diag_pipe_writef(pipe, "ow: steer:             bottom_lwm:");
                break;
        }
    }

    osw_diag_pipe_writef(pipe, "ow: steer:         state:");

    OSW_CIRC_BUF_FOREACH(&state->snr_buf, i)
        strgrow(&buf, "%s%u", buf != NULL ? " " : "", state->snr[i]);

    osw_diag_pipe_writef(pipe, "ow: steer:           snr: %s", buf);
    FREE(buf);
    buf = NULL;

    osw_diag_pipe_writef(pipe, "ow: steer:           rrm_neighbor_bcn_act_meas: %s", state->rrm_neighbor_bcn_act_meas == true ? "true" : "false");
    osw_diag_pipe_writef(pipe, "ow: steer:           wnm_bss_trans: %s", state->wnm_bss_trans == true ? "true" : "false");
    osw_diag_pipe_writef(pipe, "ow: steer:           activity:");
    osw_diag_pipe_writef(pipe, "ow: steer:             state: %s", ow_steer_policy_snr_xing_activity_state_to_cstr(ow_steer_policy_snr_xing_activity_get_state(&state->activity)));
    osw_diag_pipe_writef(pipe, "ow: steer:             threshold: %d", ow_steer_policy_snr_xing_activity_get_threshold(&state->activity));
    osw_diag_pipe_writef(pipe, "ow: steer:             grace_seconds: %d", ow_steer_policy_snr_xing_activity_get_grace_seconds(&state->activity));
}

static int
ow_steer_policy_snr_xing_config_get_activity_threshold(const struct ow_steer_policy_snr_xing_config *config)
{
    if (config == NULL) {
        return -1;
    }
    switch (config->mode) {
        case OW_STEER_POLICY_SNR_XING_MODE_HWM:
            return config->mode_config.hwm.txrx_bytes_limit.active
                 ? (int)config->mode_config.hwm.txrx_bytes_limit.delta
                 : -1;
        case OW_STEER_POLICY_SNR_XING_MODE_LWM:
            return config->mode_config.lwm.txrx_bytes_limit.active
                 ? (int)config->mode_config.lwm.txrx_bytes_limit.delta
                 : -1;
        case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
            return -1;
    }
    return -1;
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
        LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "config added"));
        register_observer = true;
    }
    else if (xing_policy->config != NULL && xing_policy->next_config == NULL) {
        LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "config removed"));
        unregister_observer = true;
    }
    else if (xing_policy->config != NULL && xing_policy->next_config != NULL) {
        if (memcmp(xing_policy->config, xing_policy->next_config, sizeof(*xing_policy->config)) == 0) {
            FREE(xing_policy->next_config);
            xing_policy->next_config = NULL;
            return;
        }

        LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "config changed"));
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

    const int threshold = ow_steer_policy_snr_xing_config_get_activity_threshold(xing_policy->config);
    ow_steer_policy_snr_xing_activity_set_threshold(&xing_policy->state.activity, threshold);

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
    LOGI(LOG_WITH_POLICY_PREFIX(xing_policy->base, "enforce period finished"));
    ow_steer_policy_dismiss_executor(xing_policy->base);
    ow_steer_policy_schedule_stack_recalc(xing_policy->base);
}

struct ow_steer_policy_snr_xing*
ow_steer_policy_snr_xing_create(const char *name,
                                const struct osw_hwaddr *sta_addr,
                                const struct ow_steer_policy_mediator *mediator,
                                const char *log_prefix)
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

    xing_policy->base = ow_steer_policy_create(strfmta("%s_%s", g_policy_name, name), sta_addr, &policy_ops, mediator, log_prefix, xing_policy);

    ow_steer_policy_snr_xing_activity_init(&state->activity,
            (ow_steer_policy_snr_xing_activity_fn_t *)ow_steer_policy_snr_xing_recalc,
            (ow_steer_policy_snr_xing_activity_fn_t *)ow_steer_policy_snr_xing_recalc,
            xing_policy);

    return xing_policy;
}

void
ow_steer_policy_snr_xing_free(struct ow_steer_policy_snr_xing *xing_policy)
{
    if (xing_policy == NULL)
        return;

    const bool unregister_observer = xing_policy->config != NULL;

    ow_steer_policy_snr_xing_state_reset(xing_policy);
    osw_timer_disarm(&xing_policy->reconf_timer);
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
