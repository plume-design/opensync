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
#include <ds_tree.h>
#include <osw_util.h>
#include <osw_types.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_diag.h>
#include <osw_sta_assoc.h>
#include <osw_sta_snr.h>
#include <osw_sta_idle.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_sta.h"
#include "ow_steer_policy_snr_xing.h"

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

enum ow_steer_policy_snr_xing_change {
    OW_STEER_POLICY_SNR_XING_CHANGE_NONE,
    OW_STEER_POLICY_SNR_XING_CHANGE_UP,
    OW_STEER_POLICY_SNR_XING_CHANGE_DOWN,
    OW_STEER_POLICY_SNR_XING_CHANGE_RESET,
};

enum ow_steer_policy_snr_xing_level {
    OW_STEER_POLICY_SNR_XING_LEVEL_NONE,
    OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE,
    OW_STEER_POLICY_SNR_XING_LEVEL_BELOW,
};

struct ow_steer_policy_snr_xing_link {
    ds_tree_node_t node;
    struct osw_hwaddr bssid;
    struct osw_hwaddr addr;
    osw_sta_snr_observer_t *snr_obs;
    uint8_t snr_db;
    bool snr_valid;
    struct ow_steer_policy_snr_xing *xing;
};

struct ow_steer_policy_snr_xing {
    struct ow_steer_policy *base;
    struct ow_steer_policy_snr_xing_config *config;
    osw_sta_assoc_observer_t *sta_obs;
    osw_sta_idle_observer_t *idle_obs;
    ds_tree_t links;
    bool rrm;
    bool btm;
    bool idle;
    struct osw_timer enforce;
    bool latched;
    enum ow_steer_policy_snr_xing_level level;
    osw_sta_assoc_t *m_sta;
    osw_sta_idle_t *m_idle;
    osw_sta_snr_t *m_snr;
};

static const char *
ow_steer_policy_snr_xing_level_get_cstr(enum ow_steer_policy_snr_xing_level level)
{
    switch (level) {
        case OW_STEER_POLICY_SNR_XING_LEVEL_NONE: return "none";
        case OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE: return "above";
        case OW_STEER_POLICY_SNR_XING_LEVEL_BELOW: return "below";
    }
    return "";
}

static void
ow_steer_policy_snr_xing_set_latched(struct ow_steer_policy_snr_xing *xing,
                                     bool latched)
{
    if (xing->latched == latched) return;
    LOGI(LOG_WITH_POLICY_PREFIX(xing->base, "latched: %d -> %d",
                                xing->latched,
                                latched));
    xing->latched = latched;
}

static void
ow_steer_policy_snr_xing_enforce_finish(struct ow_steer_policy_snr_xing *xing)
{
    LOGI(LOG_WITH_POLICY_PREFIX(xing->base, "enforce: finishing"));
    ow_steer_policy_dismiss_executor(xing->base);
    ow_steer_policy_schedule_stack_recalc(xing->base);
}

static void
ow_steer_policy_snr_xing_enforce_try_start(struct ow_steer_policy_snr_xing *xing)
{
    if (xing->latched == true) return;
    if (osw_timer_is_armed(&xing->enforce) == true) return;
    LOGI(LOG_WITH_POLICY_PREFIX(xing->base, "enforce: starting"));
    const uint64_t duration_nsec = OSW_TIME_SEC(OSW_STEER_POLICY_SNR_XING_ENFORCE_PERIOD_SEC);
    const uint64_t tstamp_nsec = osw_time_mono_clk() + duration_nsec;
    osw_timer_arm_at_nsec(&xing->enforce, tstamp_nsec);
    ow_steer_policy_trigger_executor(xing->base);
    ow_steer_policy_schedule_stack_recalc(xing->base);
    ow_steer_policy_snr_xing_set_latched(xing, true);
}

static void
ow_steer_policy_snr_xing_enforce_try_stop(struct ow_steer_policy_snr_xing *xing)
{
    if (osw_timer_is_armed(&xing->enforce) == false) return;
    LOGI(LOG_WITH_POLICY_PREFIX(xing->base, "enforce: stopping"));
    osw_timer_disarm(&xing->enforce);
    ow_steer_policy_snr_xing_enforce_finish(xing);
}

static bool
ow_steer_policy_snr_xing_sta_is_legacy(struct ow_steer_policy_snr_xing *xing)
{
    return (xing->rrm == false) && (xing->btm == false);
}

static bool
ow_steer_policy_snr_xing_links_are_enforcable(struct ow_steer_policy_snr_xing *xing)
{
    size_t i;
    for (i = 0; i < xing->config->bssids.count; i++) {
        const struct osw_hwaddr *bssid = &xing->config->bssids.list[i];
        const struct ow_steer_policy_snr_xing_link *link = ds_tree_find(&xing->links, bssid);
        if (link != NULL)
            return true;
    }
    return false;
}

static bool
ow_steer_policy_snr_xing_enforce_is_needed(struct ow_steer_policy_snr_xing *xing)
{
    const struct ow_steer_policy_snr_xing_config *config = xing->config;
    if (config == NULL)
        return false;

    const bool bowm = (config->mode == OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM);
    const bool active = (xing->idle == false);
    const bool legacy = ow_steer_policy_snr_xing_sta_is_legacy(xing);

    if (bowm && !legacy)
        return false;

    if (legacy && active)
        return false;

    if (ow_steer_policy_snr_xing_links_are_enforcable(xing) == false)
        return false;

    switch (xing->level) {
        case OW_STEER_POLICY_SNR_XING_LEVEL_NONE:
            return false;
        case OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE:
            switch (config->mode) {
                case OW_STEER_POLICY_SNR_XING_MODE_HWM:
                    return true;
                case OW_STEER_POLICY_SNR_XING_MODE_LWM:
                    return false;
                case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
                    return false;
            }
            break;
        case OW_STEER_POLICY_SNR_XING_LEVEL_BELOW:
            switch (config->mode) {
                case OW_STEER_POLICY_SNR_XING_MODE_HWM:
                    return false;
                case OW_STEER_POLICY_SNR_XING_MODE_LWM:
                    return true;
                case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
                    return true;
            }
            break;
    }

    return false;
}

static void
ow_steer_policy_snr_xing_update_enforce(struct ow_steer_policy_snr_xing *xing)
{
    if (ow_steer_policy_snr_xing_enforce_is_needed(xing)) {
        ow_steer_policy_snr_xing_enforce_try_start(xing);
    }
    else {
        ow_steer_policy_snr_xing_enforce_try_stop(xing);
    }
}

static void
ow_steer_policy_snr_xing_set_level(struct ow_steer_policy_snr_xing *xing,
                                   enum ow_steer_policy_snr_xing_level level)
{
    if (xing->level == level) return;
    LOGI(LOG_WITH_POLICY_PREFIX(xing->base, "level: %s -> %s",
                                ow_steer_policy_snr_xing_level_get_cstr(xing->level),
                                ow_steer_policy_snr_xing_level_get_cstr(level)));
    xing->level = level;
    ow_steer_policy_snr_xing_set_latched(xing, false);
    ow_steer_policy_snr_xing_update_enforce(xing);
}

static void
ow_steer_policy_snr_xing_set_idle(struct ow_steer_policy_snr_xing *xing,
                                  bool idle)
{
    if (xing->idle == idle) return;
    LOGI(LOG_WITH_POLICY_PREFIX(xing->base, "idle: %d -> %d", xing->idle, idle));
    xing->idle = idle;
    ow_steer_policy_snr_xing_update_enforce(xing);
}

static const char *
ow_steer_policy_snr_xing_mode_to_cstr(enum ow_steer_policy_snr_xing_mode mode)
{
    switch (mode) {
        case OW_STEER_POLICY_SNR_XING_MODE_HWM: return "hmw";
        case OW_STEER_POLICY_SNR_XING_MODE_LWM: return "lwm";
        case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM: return "bottom_lwm";
    }
    return "unknown";
}

static enum ow_steer_policy_snr_xing_level
ow_steer_policy_snr_xing_next_level(struct ow_steer_policy_snr_xing *xing)
{
    const struct ow_steer_policy_snr_xing_config *config = xing->config;
    if (config == NULL)
        return OW_STEER_POLICY_SNR_XING_LEVEL_NONE;

    const bool is_hwm = (xing->config->mode == OW_STEER_POLICY_SNR_XING_MODE_HWM);
    const bool is_lwm = (xing->config->mode == OW_STEER_POLICY_SNR_XING_MODE_LWM);
    const uint8_t hyst_below = (xing->config->snr_hysteresis >= xing->config->snr) ? 1 : xing->config->snr_hysteresis;
    const uint8_t hyst_above = xing->config->snr_hysteresis;
    const uint8_t below_thr = config->snr - (is_hwm ? hyst_below : 0);
    const uint8_t above_thr = config->snr + (is_lwm ? hyst_above : 0);

    size_t n = 0;
    size_t below = 0;
    size_t above = 0;
    size_t none = 0;
    struct ow_steer_policy_snr_xing_link *link;
    bool connected = false;
    ds_tree_foreach(&xing->links, link) {
        connected = true;
        if (link->snr_valid) {
            n++;
            if (link->snr_db < below_thr) below++;
            else if (link->snr_db > above_thr) above++;
            else none++;
        }
    }

    switch (xing->level) {
        case OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE:
            above += none;
            none = 0;
            break;
        case OW_STEER_POLICY_SNR_XING_LEVEL_BELOW:
            below += none;
            none = 0;
            break;
        case OW_STEER_POLICY_SNR_XING_LEVEL_NONE:
            break;
    }

    if (above && above == n) {
        if (is_hwm && xing->level != OW_STEER_POLICY_SNR_XING_LEVEL_BELOW)
            return xing->level;
        else
            return OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE;
    }
    else if (below && below == n) {
        if (is_lwm && xing->level != OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE)
            return xing->level;
        else
            return OW_STEER_POLICY_SNR_XING_LEVEL_BELOW;
    }
    else if (!connected)
        return OW_STEER_POLICY_SNR_XING_LEVEL_NONE;
    else
        return xing->level;
}

static void
ow_steer_policy_snr_xing_update_level(struct ow_steer_policy_snr_xing *xing)
{
    ow_steer_policy_snr_xing_set_level(xing, ow_steer_policy_snr_xing_next_level(xing));
}

static bool
ow_steer_policy_snr_xing_bssid_is_blocked(struct ow_steer_policy_snr_xing *xing,
                                          const struct osw_hwaddr *bssid)
{
    struct ow_steer_policy_snr_xing_config *config = xing->config;
    if (config == NULL)
        return false;

    struct ow_steer_policy_snr_xing_link *link = ds_tree_find(&xing->links, bssid);
    if (link == NULL) return false;

    return osw_hwaddr_list_contains(config->bssids.list, config->bssids.count, bssid);
}

static bool
ow_steer_policy_snr_xing_alternatives_avalable(struct ow_steer_policy_snr_xing *xing,
                                               struct ow_steer_candidate_list *candidates)
{
    struct ow_steer_policy_snr_xing_config *config = xing->config;
    if (config == NULL)
        return false;

    size_t others = 0;
    size_t i;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidates); i++) {
        struct ow_steer_candidate *c = ow_steer_candidate_list_get(candidates, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(c);
        const enum ow_steer_candidate_preference pref = ow_steer_candidate_get_preference(c);
        switch (pref) {
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                if (ow_steer_policy_snr_xing_bssid_is_blocked(xing, bssid) == false)
                    others++;
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
                others++;
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
                break;
        }
    }
    return others > 0;
}

static bool
ow_steer_policy_snr_xing_blockable(struct ow_steer_policy_snr_xing *xing,
                                   struct ow_steer_candidate_list *candidates)
{
    struct ow_steer_policy_snr_xing_config *config = xing->config;
    if (config == NULL)
        return false;

    size_t blocked = 0;
    size_t blockable = 0;
    size_t non_blockable = 0;
    size_t i;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidates); i++) {
        struct ow_steer_candidate *c = ow_steer_candidate_list_get(candidates, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(c);
        const enum ow_steer_candidate_preference pref = ow_steer_candidate_get_preference(c);
        if (ow_steer_policy_snr_xing_bssid_is_blocked(xing, bssid) == false) continue;
        blocked++;
        switch (pref) {
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
                blockable++;
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
                non_blockable++;
                break;
        }
    }

    return (non_blockable == 0) && (blockable > 0) && (blockable == blocked);
}

static void
ow_steer_policy_snr_xing_recalc_cb(struct ow_steer_policy *policy,
                                   struct ow_steer_candidate_list *candidates)
{
    ASSERT(policy != NULL, "");
    ASSERT(candidates != NULL, "");

    struct ow_steer_policy_snr_xing *xing = ow_steer_policy_get_priv(policy);
    struct ow_steer_policy_snr_xing_config *config = xing->config;
    const char *reason = ow_steer_policy_get_name(policy);

    if (config == NULL)
        return;

    if (osw_timer_is_armed(&xing->enforce) == false)
        return;

    if (ow_steer_policy_snr_xing_alternatives_avalable(xing, candidates) == false)
        return;

    if (ow_steer_policy_snr_xing_blockable(xing, candidates) == false)
        return;

    size_t i;
    for (i = 0; i < ow_steer_candidate_list_get_length(candidates); i++) {
        struct ow_steer_candidate *c = ow_steer_candidate_list_get(candidates, i);
        const struct osw_hwaddr *bssid = ow_steer_candidate_get_bssid(c);
        const enum ow_steer_candidate_preference pref = ow_steer_candidate_get_preference(c);
        const enum ow_steer_candidate_preference wanted_pref = ow_steer_policy_snr_xing_bssid_is_blocked(xing, bssid)
                                                             ? OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED
                                                             : OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE;
        switch (pref) {
            case OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE:
            case OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED:
            case OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE:
            case OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED:
                break;
            case OW_STEER_CANDIDATE_PREFERENCE_NONE:
                ow_steer_candidate_set_preference(c, reason, wanted_pref);
                break;
        }
    }
}

static void
ow_steer_policy_snr_xing_sigusr1_dump_cb(osw_diag_pipe_t *pipe,
                                         struct ow_steer_policy *policy)
{
    ASSERT(policy != NULL, "");

    struct ow_steer_policy_snr_xing *xing = ow_steer_policy_get_priv(policy);
    struct ow_steer_policy_snr_xing_link *link;
    const struct ow_steer_policy_snr_xing_config *config = xing->config;

    osw_diag_pipe_writef(pipe, "ow: steer:         sta_obs: %p", xing->sta_obs);
    osw_diag_pipe_writef(pipe, "ow: steer:         links: %zu", ds_tree_len(&xing->links));
    ds_tree_foreach(&xing->links, link) {
        const char *snr_str = link->snr_valid ? strfmta("%hhu dB", link->snr_db) : "unknown";
        osw_diag_pipe_writef(pipe, "ow: steer:           bssid: "OSW_HWADDR_FMT" addr: "OSW_HWADDR_FMT" snr: %s",
                             OSW_HWADDR_ARG(&link->bssid),
                             OSW_HWADDR_ARG(&link->addr),
                             snr_str);
    }
    osw_diag_pipe_writef(pipe, "ow: steer:         btm: %d", xing->btm);
    osw_diag_pipe_writef(pipe, "ow: steer:         rrm: %d", xing->rrm);
    osw_diag_pipe_writef(pipe, "ow: steer:         idle_obs: %p", xing->idle_obs);
    osw_diag_pipe_writef(pipe, "ow: steer:           idle: %d", xing->idle);
    osw_diag_pipe_writef(pipe, "ow: steer:         level: %s", ow_steer_policy_snr_xing_level_get_cstr(xing->level));
    osw_diag_pipe_writef(pipe, "ow: steer:         latched: %s", xing->latched ? "yes" : "no");
    osw_diag_pipe_writef(pipe, "ow: steer:         enforcing: %s", osw_timer_is_armed(&xing->enforce) ? "yes" : "no");
    osw_diag_pipe_writef(pipe, "ow: steer:         config: %s", config != NULL ? "" : "(nil)");

    if (config != NULL) {
        osw_diag_pipe_writef(pipe, "ow: steer:           bssids: %zu", config->bssids.count);
        size_t i;
        for (i = 0; i < config->bssids.count; i++) {
            const struct osw_hwaddr *bssid = &config->bssids.list[i];
            osw_diag_pipe_writef(pipe, "ow: steer:             %zu: "OSW_HWADDR_FMT, i, OSW_HWADDR_ARG(bssid));
        }
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

}

static void
ow_steer_policy_snr_xing_idle_changed_cb(void *priv, bool idle)
{
    struct ow_steer_policy_snr_xing *xing = priv;
    ow_steer_policy_snr_xing_set_idle(xing, idle);
}

static osw_sta_idle_observer_t *
ow_steer_policy_snr_xing_alloc_idle_obs_with_bps(struct ow_steer_policy_snr_xing *xing,
                                                 const struct osw_hwaddr *sta_addr,
                                                 uint64_t bytes_per_second)
{
    osw_sta_idle_params_t *p = osw_sta_idle_params_alloc();
    osw_sta_idle_params_set_sta_addr(p, sta_addr);
    osw_sta_idle_params_set_bytes_per_sec(p, bytes_per_second);
    osw_sta_idle_params_set_notify_fn(p, ow_steer_policy_snr_xing_idle_changed_cb, xing);
    return osw_sta_idle_observer_alloc(xing->m_idle, p);
}

static const uint64_t *
ow_steer_policy_snr_xing_get_bps(struct ow_steer_policy_snr_xing *xing)
{
    if (xing == NULL)
        return NULL;

    struct ow_steer_policy_snr_xing_config *config = xing->config;
    if (config == NULL)
        return NULL;

    switch (xing->config->mode) {
        case OW_STEER_POLICY_SNR_XING_MODE_HWM:
             if (config->mode_config.hwm.txrx_bytes_limit.active == false)
                 return NULL;
             return &config->mode_config.hwm.txrx_bytes_limit.delta;
        case OW_STEER_POLICY_SNR_XING_MODE_LWM:
             if (config->mode_config.lwm.txrx_bytes_limit.active == false)
                 return NULL;
             return &config->mode_config.lwm.txrx_bytes_limit.delta;
        case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
             break;
    }

    return NULL;
}

static osw_sta_idle_observer_t *
ow_steer_policy_snr_xing_alloc_idle_obs(struct ow_steer_policy_snr_xing *xing)
{
    const struct osw_hwaddr *sta_addr = ow_steer_policy_get_sta_addr(xing->base) ?: osw_hwaddr_zero();
    if (osw_hwaddr_is_zero(sta_addr))
        return NULL;

    const uint64_t *bps = ow_steer_policy_snr_xing_get_bps(xing);
    if (bps == NULL) return NULL;

    return ow_steer_policy_snr_xing_alloc_idle_obs_with_bps(xing, sta_addr, *bps);
}

static void
ow_steer_policy_snr_xing_update_idle(struct ow_steer_policy_snr_xing *xing)
{
    osw_sta_idle_observer_drop(xing->idle_obs);
    xing->idle_obs = ow_steer_policy_snr_xing_alloc_idle_obs(xing);
    ow_steer_policy_snr_xing_set_idle(xing, false);
}

static bool
ow_steer_policy_snr_xing_config_is_equal(const struct ow_steer_policy_snr_xing_config *a,
                                         const struct ow_steer_policy_snr_xing_config *b)
{
    if (a == NULL && b == NULL) return true;
    if (a == NULL && b != NULL) return false;
    if (a != NULL && b == NULL) return false;
    if (osw_hwaddr_list_is_equal(&a->bssids, &b->bssids) == false) return false;
    if (a->mode != b->mode) return false;
    if (a->snr != b->snr) return false;
    switch (a->mode) { /* b->mode is guaranteed identical already */
        case OW_STEER_POLICY_SNR_XING_MODE_HWM:
            if (a->mode_config.hwm.txrx_bytes_limit.delta !=
                b->mode_config.hwm.txrx_bytes_limit.delta) return false;
            break;
        case OW_STEER_POLICY_SNR_XING_MODE_LWM:
            if (a->mode_config.lwm.txrx_bytes_limit.delta !=
                b->mode_config.lwm.txrx_bytes_limit.delta) return false;
            break;
        case OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM:
            break;
    }
    return true;
}

static void
ow_steer_policy_snr_xing_enforce_cb(struct osw_timer *t)
{
    struct ow_steer_policy_snr_xing *xing = container_of(t, typeof(*xing), enforce);
    ow_steer_policy_snr_xing_enforce_finish(xing);
}

static struct ow_steer_policy_snr_xing_link *
ow_steer_policy_snr_xing_alloc_link(struct ow_steer_policy_snr_xing *xing,
                                    const struct osw_hwaddr *bssid)
{
    struct ow_steer_policy_snr_xing_link *link = CALLOC(1, sizeof(*link));
    link->xing = xing;
    link->bssid = *bssid;
    ds_tree_insert(&xing->links, link, &link->bssid);
    return link;
}

static void
ow_steer_policy_snr_xing_link_drop(struct ow_steer_policy_snr_xing_link *link)
{
    if (WARN_ON(link == NULL)) return;
    if (WARN_ON(link->xing == NULL)) return;
    ds_tree_remove(&link->xing->links, link);
    ow_steer_policy_snr_xing_update_level(link->xing);
    FREE(link);
}

static void
ow_steer_policy_snr_xing_link_snr_set(struct ow_steer_policy_snr_xing_link *link, const uint8_t *snr_db)
{
    const uint8_t *current_snr_db = link->snr_valid ? &link->snr_db : NULL;
    if (current_snr_db == NULL && snr_db == NULL) return;
    if (current_snr_db != NULL && snr_db != NULL && (*current_snr_db == *snr_db)) return;
    LOGD(LOG_WITH_POLICY_PREFIX(link->xing->base, OSW_HWADDR_FMT": "OSW_HWADDR_FMT": snr: %d -> %d",
                                OSW_HWADDR_ARG(&link->bssid),
                                OSW_HWADDR_ARG(&link->addr),
                                link->snr_valid ? (int)link->snr_db : -1,
                                snr_db ? (int)*snr_db : -1));
    link->snr_valid = snr_db != NULL;
    link->snr_db = snr_db ? *snr_db : 0;
    ow_steer_policy_snr_xing_update_level(link->xing);
}

static void
ow_steer_policy_snr_xing_link_snr_changed_cb(void *priv, const uint8_t *snr_db)
{
    struct ow_steer_policy_snr_xing_link *link = priv;
    ow_steer_policy_snr_xing_link_snr_set(link, snr_db);
}

static osw_sta_snr_observer_t *
ow_steer_policy_snr_xing_link_alloc_snr(struct ow_steer_policy_snr_xing_link *link)
{
    if (link->xing->config == NULL) return NULL;
    if (osw_hwaddr_is_zero(&link->addr)) return NULL;
    osw_sta_snr_params_t *p = osw_sta_snr_params_alloc();
    osw_sta_snr_params_set_sta_addr(p, &link->addr);
    osw_sta_snr_params_set_vif_addr(p, &link->bssid);
    osw_sta_snr_params_set_ageout_sec(p, link->xing->config->snr_ageout_sec);
    osw_sta_snr_params_set_notify_fn(p, ow_steer_policy_snr_xing_link_snr_changed_cb, link);
    return osw_sta_snr_observer_alloc(link->xing->m_snr, p);
}

static struct ow_steer_policy_snr_xing_link *
ow_steer_policy_snr_xing_get_link(struct ow_steer_policy_snr_xing *xing,
                                  const struct osw_hwaddr *bssid)
{
    return ds_tree_find(&xing->links, bssid) ?: ow_steer_policy_snr_xing_alloc_link(xing, bssid);
}

static struct ow_steer_policy_snr_xing_link *
ow_steer_policy_snr_xing_set_link(struct ow_steer_policy_snr_xing *xing,
                                  const struct osw_hwaddr *bssid,
                                  const struct osw_hwaddr *addr)
{
    if (WARN_ON(osw_hwaddr_is_zero(bssid))) return NULL;

    struct ow_steer_policy_snr_xing_link *link = ow_steer_policy_snr_xing_get_link(xing, bssid);

    const bool changed = (osw_hwaddr_is_equal(&link->addr, addr) == false);
    LOGD(LOG_WITH_POLICY_PREFIX(xing->base, "link: "OSW_HWADDR_FMT": "OSW_HWADDR_FMT" -> "OSW_HWADDR_FMT,
                                OSW_HWADDR_ARG(bssid),
                                OSW_HWADDR_ARG(&link->addr),
                                OSW_HWADDR_ARG(addr)));
    link->addr = *addr;

    if (changed) {
        osw_sta_snr_observer_drop(link->snr_obs);
        link->snr_obs = ow_steer_policy_snr_xing_link_alloc_snr(link);
        ow_steer_policy_snr_xing_set_idle(link->xing, false);
        ow_steer_policy_snr_xing_update_level(link->xing);
    }

    if (osw_hwaddr_is_zero(&link->addr)) {
        ow_steer_policy_snr_xing_link_drop(link);
        return NULL;
    }

    return link;
}

static void
ow_steer_policy_snr_xing_update_links(struct ow_steer_policy_snr_xing *xing,
                                      const osw_sta_assoc_links_t *links)
{
    /* This adds or updates existing entries. The case for
     * updates is when client stays on the same BSSID(s) but
     * re-randomizes link addresses on re-association.
     */
    if (links != NULL) {
        size_t i;
        for (i = 0; i < links->count; i++) {
            const osw_sta_assoc_link_t *link = &links->links[i];
            const struct osw_hwaddr *bssid = &link->local_sta_addr;
            const struct osw_hwaddr *addr = &link->remote_sta_addr;
            ow_steer_policy_snr_xing_set_link(xing, bssid, addr);
        }
    }

    /* This removes links that are no longer present in the
     * association.
     */
    struct ow_steer_policy_snr_xing_link *tmp;
    struct ow_steer_policy_snr_xing_link *link;
    ds_tree_foreach_safe(&xing->links, link, tmp) {
        const osw_sta_assoc_link_t *match = osw_sta_assoc_links_lookup(links, &link->bssid, &link->addr);
        if (match == NULL) {
            /* virtually drops it */
            ow_steer_policy_snr_xing_set_link(xing, &link->bssid, osw_hwaddr_zero());
        }
    }
}

static void
ow_steer_policy_snr_xing_set_rrm(struct ow_steer_policy_snr_xing *xing, const bool rrm)
{
    if (xing->rrm == rrm) return;
    LOGD(LOG_WITH_POLICY_PREFIX(xing->base, "rrm: %d -> %d", xing->rrm, rrm));
    xing->rrm = rrm;
    ow_steer_policy_snr_xing_update_enforce(xing);
}

static void
ow_steer_policy_snr_xing_set_btm(struct ow_steer_policy_snr_xing *xing, const bool btm)
{
    if (xing->btm == btm) return;
    LOGD(LOG_WITH_POLICY_PREFIX(xing->base, "btm: %d -> %d", xing->btm, btm));
    xing->btm = btm;
    ow_steer_policy_snr_xing_update_enforce(xing);
}

static void
ow_steer_policy_snr_xing_update_sta_caps(struct ow_steer_policy_snr_xing *xing,
                                         const void *ies,
                                         const size_t ies_len)
{
    struct osw_assoc_req_info info;
    const bool parsed = osw_parse_assoc_req_ies(ies, ies_len, &info);
    const bool btm = parsed && info.wnm_bss_trans;
    const bool rrm = parsed && info.rrm_neighbor_bcn_act_meas;
    ow_steer_policy_snr_xing_set_btm(xing, btm);
    ow_steer_policy_snr_xing_set_rrm(xing, rrm);
}
static void
ow_steer_policy_snr_xing_assoc_changed_cb(void *priv,
                                          const osw_sta_assoc_entry_t *entry,
                                          osw_sta_assoc_event_e ev)
{
    struct ow_steer_policy_snr_xing *xing = priv;
    const osw_sta_assoc_links_t *links = osw_sta_assoc_entry_get_active_links(entry);
    const void *ies = osw_sta_assoc_entry_get_assoc_ies_data(entry);
    const size_t ies_len = osw_sta_assoc_entry_get_assoc_ies_len(entry);
    ow_steer_policy_snr_xing_update_sta_caps(xing, ies, ies_len);
    ow_steer_policy_snr_xing_update_links(xing, links);
}

static osw_sta_assoc_observer_t *
ow_steer_policy_snr_xing_alloc_sta_obs(struct ow_steer_policy_snr_xing *xing,
                                       const struct osw_hwaddr *sta_addr)
{
    if (osw_hwaddr_is_zero(sta_addr)) return NULL;
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p, ow_steer_policy_snr_xing_assoc_changed_cb, xing);
    osw_sta_assoc_observer_params_set_addr(p, sta_addr);
    return osw_sta_assoc_observer_alloc(xing->m_sta, p);
}

struct ow_steer_policy_snr_xing *
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
    };

    const char *pname = strfmta("%s_%s", "snr_xing", name);
    struct ow_steer_policy_snr_xing *xing = CALLOC(1, sizeof(*xing));
    osw_timer_init(&xing->enforce, ow_steer_policy_snr_xing_enforce_cb);
    ds_tree_init(&xing->links, (ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_steer_policy_snr_xing_link, node);
    xing->m_sta = OSW_MODULE_LOAD(osw_sta_assoc);
    xing->m_snr = OSW_MODULE_LOAD(osw_sta_snr);
    xing->m_idle = OSW_MODULE_LOAD(osw_sta_idle);
    xing->base = ow_steer_policy_create(pname, sta_addr, &policy_ops, mediator, log_prefix, xing);

    return xing;
}

static void
ow_steer_policy_snr_xing_free_config(struct ow_steer_policy_snr_xing_config *config)
{
    if (config == NULL)
        return;

    osw_hwaddr_list_flush(&config->bssids);
    FREE(config);
}

void
ow_steer_policy_snr_xing_free(struct ow_steer_policy_snr_xing *xing)
{
    if (xing == NULL)
        return;

    ow_steer_policy_snr_xing_set_config(xing, NULL);
    ow_steer_policy_free(xing->base);
    ASSERT(ds_tree_is_empty(&xing->links), "should be emptied by osw_sta_assoc_observer_drop()");
    ASSERT(xing->idle_obs == NULL, "should be emptied by ow_steer_policy_snr_xing_set_config()");
    ASSERT(xing->sta_obs == NULL, "should be emptied by ow_steer_policy_snr_xing_set_config()");
    ASSERT(xing->config == NULL, "should be emptied by ow_steer_policy_snr_xing_set_config()");
    FREE(xing);
}

struct ow_steer_policy *
ow_steer_policy_snr_xing_get_base(struct ow_steer_policy_snr_xing *xing)
{
    ASSERT(xing != NULL, "");
    return xing->base;
}

void
ow_steer_policy_snr_xing_set_config(struct ow_steer_policy_snr_xing *xing,
                                    struct ow_steer_policy_snr_xing_config *config)
{
    ASSERT(xing != NULL, "");

    const bool changed = (ow_steer_policy_snr_xing_config_is_equal(xing->config, config) == false);
    if (changed == false)
        return;

    LOGI(LOG_WITH_POLICY_PREFIX(xing->base, "reconfiguring"));

    if (config == NULL) {
        osw_sta_assoc_observer_drop(xing->sta_obs);
        xing->sta_obs = NULL;
        ow_steer_policy_snr_xing_update_links(xing, NULL);
    }

    ow_steer_policy_snr_xing_free_config(xing->config);
    xing->config = config;

    if (config != NULL) {
        if (xing->sta_obs == NULL) {
            const struct osw_hwaddr *sta_addr = ow_steer_policy_get_sta_addr(xing->base) ?: osw_hwaddr_zero();
            xing->sta_obs = ow_steer_policy_snr_xing_alloc_sta_obs(xing, sta_addr);
        }
    }

    ow_steer_policy_snr_xing_update_idle(xing);
    ow_steer_policy_snr_xing_update_level(xing);
    ow_steer_policy_schedule_stack_recalc(xing->base);
}

#include "ow_steer_policy_snr_xing_ut.c"
