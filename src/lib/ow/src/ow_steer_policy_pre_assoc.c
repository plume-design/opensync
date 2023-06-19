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

#include <math.h>
#include <endian.h>
#include <const.h>
#include <ds_tree.h>
#include <memutil.h>
#include <log.h>
#include <util.h>
#include <osw_types.h>
#include <osw_drv_common.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_state.h>
#include "ow_steer_candidate_list.h"
#include "ow_steer_policy.h"
#include "ow_steer_policy_priv.h"
#include "ow_steer_policy_pre_assoc.h"

struct ow_steer_policy_pre_assoc_persistent_state {
    unsigned int backoff_connect_cnt;
    unsigned int active_link_cnt;
    unsigned int auth_bypass_fail_cnt;
};

struct ow_steer_policy_pre_assoc_volatile_state {
    unsigned int reject_cnt;
    struct osw_timer reject_timer;
    struct osw_timer backoff_timer;
    struct osw_timer auth_bypass_timer;
};

struct ow_steer_policy_pre_assoc {
    struct ow_steer_policy *base;
    struct ow_steer_policy_pre_assoc_config *next_config;
    struct ow_steer_policy_pre_assoc_config *config;
    struct ow_steer_policy_pre_assoc_persistent_state pstate;
    struct ow_steer_policy_pre_assoc_volatile_state vstate;
    struct osw_state_observer state_observer;
    struct osw_timer reconf_timer;
};

static const char *g_policy_name = "pre_assoc";

static double
ow_steer_policy_pre_assoc_compute_backoff_timeout(struct ow_steer_policy_pre_assoc *pre_assoc_policy)
{
    ASSERT(pre_assoc_policy != NULL, "");
    ASSERT(pre_assoc_policy->config != NULL, "");

    const struct ow_steer_policy_pre_assoc_config *config = pre_assoc_policy->config;
    const struct ow_steer_policy_pre_assoc_persistent_state *pstate = &pre_assoc_policy->pstate;
    const unsigned int power = MIN(pstate->backoff_connect_cnt, 10u); /* FIXME Old BM had max 10 */
    const unsigned int base = config->backoff_exp_base;

    return config->backoff_timeout_sec * pow(base, power);
}

static double
ow_steer_policy_pre_assoc_compute_auth_bypass_timeout_sec(struct ow_steer_policy_pre_assoc *pre_assoc_policy)
{
    /* Authentications are typically retried every
     * 100-200ms, a few times. So there's probably up to a
     * few seconds for client to actually connect in case it
     * was blocked and it did not send Probe Requests at
     * all. The 5s timeout like a good enough value.
     */
    return 5;
}

static void
ow_steer_policy_pre_assoc_reset_volatile_state(struct ow_steer_policy_pre_assoc *pre_assoc_policy)
{
    ASSERT(pre_assoc_policy != NULL, "");

    struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;
    const bool reject_period = osw_timer_is_armed(&vstate->reject_timer) == true;
    const bool backoff_period = osw_timer_is_armed(&vstate->backoff_timer) == true;
    const bool dismiss_policy = reject_period;

    if (reject_period == true)
        LOGD("%s rejecting period stopped", ow_steer_policy_get_prefix(pre_assoc_policy->base));
    if (backoff_period == true) {
        const double backoff_timeout_sec = ow_steer_policy_pre_assoc_compute_backoff_timeout(pre_assoc_policy);
        ow_steer_policy_notify_backoff(pre_assoc_policy->base,
                                       false,
                                       backoff_timeout_sec);
        LOGD("%s backoff period stopped", ow_steer_policy_get_prefix(pre_assoc_policy->base));
    }

    vstate->reject_cnt = 0;
    osw_timer_disarm(&vstate->reject_timer);
    osw_timer_disarm(&vstate->backoff_timer);
    osw_timer_disarm(&vstate->auth_bypass_timer);

    if (dismiss_policy == true)
        ow_steer_policy_dismiss_executor(pre_assoc_policy->base);
}

static void
ow_steer_policy_pre_assoc_sigusr1_dump_cb(struct ow_steer_policy *policy)
{
    ASSERT(policy != NULL, "");

    const struct ow_steer_policy_pre_assoc *pre_assoc_policy = ow_steer_policy_get_priv(policy);
    const struct ow_steer_policy_pre_assoc_config *config = pre_assoc_policy->config;

    LOGI("ow: steer:         config: %s", config != NULL ? "" : "(nil)");
    if (config != NULL) {
        LOGI("ow: steer:           backoff_timeout_sec: %u", config->backoff_timeout_sec);
        LOGI("ow: steer:           backoff_exp_base: %u", config->backoff_exp_base);

        switch (config->reject_condition.type) {
            case OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_COUNTER:
                LOGI("ow: steer:           reject condition: counter");
                LOGI("ow: steer:             reject_limit: %u", config->reject_condition.params.counter.reject_limit);
                LOGI("ow: steer:             reject_timeout_sec: %u", config->reject_condition.params.counter.reject_timeout_sec);
                break;
            case OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER:
                LOGI("ow: steer:           reject condition: timer");
                LOGI("ow: steer:             reject_timeout_msec: %u", config->reject_condition.params.timer.reject_timeout_msec);
                break;
        }

        switch (config->backoff_condition.type) {
            case OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_NONE:
                LOGI("ow: steer:           backoff_condition: none");
                break;
            case OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_THRESHOLD_SNR:
                LOGI("ow: steer:           backoff_condition: threshold snr");
                LOGI("ow: steer:             threshold_snr: %u", config->backoff_condition.params.threshold_snr.threshold_snr);
                break;
        }
    }

    const struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;
    const uint64_t now_nsec = osw_time_mono_clk();
    const char *reject_timer_buf = osw_timer_is_armed(&vstate->reject_timer) == true ?
        strfmta("%.2lf sec remaining", OSW_TIME_TO_DBL(osw_timer_get_remaining_nsec(&vstate->reject_timer, now_nsec))) : "inactive";
    const char *backoff_timer_buf = osw_timer_is_armed(&vstate->backoff_timer) == true ?
        strfmta("%.2lf sec remaining", OSW_TIME_TO_DBL(osw_timer_get_remaining_nsec(&vstate->backoff_timer, now_nsec))) : "inactive";
    LOGI("ow: steer:         vstate:");
    LOGI("ow: steer:           reject_cnt: %u", vstate->reject_cnt);
    LOGI("ow: steer:           reject_timer: %s", reject_timer_buf);
    LOGI("ow: steer:           backoff_timer: %s", backoff_timer_buf);

    const struct ow_steer_policy_pre_assoc_persistent_state *pstate = &pre_assoc_policy->pstate;
    LOGI("ow: steer:         pstate:");
    LOGI("ow: steer:           active_link_cnt: %u", pstate->active_link_cnt);
    LOGI("ow: steer:           backoff_connect_cnt: %u", pstate->backoff_connect_cnt);
}

static void
ow_steer_policy_pre_assoc_reject_timer_cb(struct osw_timer *timer)
{
    struct ow_steer_policy_pre_assoc *pre_assoc_policy = container_of(timer, struct ow_steer_policy_pre_assoc, vstate.reject_timer);
    ASSERT(pre_assoc_policy->config != NULL, "");

    LOGD("%s rejecting finished", ow_steer_policy_get_prefix(pre_assoc_policy->base));

    struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;
    const double backoff_timeout_sec = ow_steer_policy_pre_assoc_compute_backoff_timeout(pre_assoc_policy);
    osw_timer_arm_at_nsec(&vstate->backoff_timer, osw_time_mono_clk() + OSW_TIME_SEC(backoff_timeout_sec));

    ow_steer_policy_notify_backoff(pre_assoc_policy->base,
                                   true,
                                   backoff_timeout_sec);

    LOGD("%s backoff period started", ow_steer_policy_get_prefix(pre_assoc_policy->base));
    ow_steer_policy_dismiss_executor(pre_assoc_policy->base);
    ow_steer_policy_schedule_stack_recalc(pre_assoc_policy->base);
}

static void
ow_steer_policy_pre_assoc_backoff_timer_cb(struct osw_timer *timer)
{
    struct ow_steer_policy_pre_assoc *pre_assoc_policy = container_of(timer, struct ow_steer_policy_pre_assoc, vstate.backoff_timer);
    ASSERT(pre_assoc_policy->config != NULL, "");

    LOGI("%s backoff finished", ow_steer_policy_get_prefix(pre_assoc_policy->base));

    ow_steer_policy_pre_assoc_reset_volatile_state(pre_assoc_policy);
    ow_steer_policy_schedule_stack_recalc(pre_assoc_policy->base);
}

static void
ow_steer_policy_pre_assoc_auth_bypass_timer_cb(struct osw_timer *timer)
{
    struct ow_steer_policy_pre_assoc *pre_assoc_policy = container_of(timer, struct ow_steer_policy_pre_assoc, vstate.auth_bypass_timer);
    struct ow_steer_policy_pre_assoc_persistent_state *pstate = &pre_assoc_policy->pstate;

    ASSERT(pre_assoc_policy->config != NULL, "");

    /* This is an unlikely case where by the time the ACL
     * was unblocked the client already had given up
     * re-trying. This could happen if the osw_confsync
     * reconfiguration took longer than expected, or if the
     * main loop stalled. In either case log it visibly.
     * This isn't fatal, but pretty severe to warrant a
     * NOTICE.
     */

    const unsigned int prev_cnt = pstate->auth_bypass_fail_cnt;
    pstate->auth_bypass_fail_cnt++;

    LOGN("%s failed to let client in through auth bypass (total occurances: %u -> %u)",
         ow_steer_policy_get_prefix(pre_assoc_policy->base),
         prev_cnt,
         pstate->auth_bypass_fail_cnt);
}

static void
ow_steer_policy_pre_assoc_sta_connected_cb(struct osw_state_observer *observer,
                                           const struct osw_state_sta_info *sta_info)
{
    struct ow_steer_policy_pre_assoc *pre_assoc_policy = container_of(observer, struct ow_steer_policy_pre_assoc, state_observer);

    const struct osw_hwaddr *policy_sta_addr = ow_steer_policy_get_sta_addr(pre_assoc_policy->base);
    if (osw_hwaddr_cmp(sta_info->mac_addr, policy_sta_addr) != 0)
        return;

    struct ow_steer_policy_pre_assoc_persistent_state *pstate = &pre_assoc_policy->pstate;
    pstate->active_link_cnt++;

    /* No config -> nop */
    const struct osw_hwaddr *vif_bssid = &sta_info->vif->drv_state->mac_addr;
    const struct ow_steer_policy_pre_assoc_config *config = pre_assoc_policy->config;
    if (config == NULL) {
        LOGD("%s sta connected to bssid: "OSW_HWADDR_FMT", (no conf)", ow_steer_policy_get_prefix(pre_assoc_policy->base), OSW_HWADDR_ARG(vif_bssid));
        return;
    }

    struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;
    const struct osw_hwaddr *policy_bssid = &config->bssid;
    const bool blocked_vif = osw_hwaddr_cmp(policy_bssid, vif_bssid) == 0;
    const bool backoff_period = osw_timer_is_armed(&vstate->backoff_timer) == true;
    if (backoff_period == true) {
        if (blocked_vif == true)
            LOGD("%s sta connected to blocked vif, during backoff period", ow_steer_policy_get_prefix(pre_assoc_policy->base));
        else
            LOGD("%s sta connected to allowed vif bssid: "OSW_HWADDR_FMT", during backoff period", ow_steer_policy_get_prefix(pre_assoc_policy->base), OSW_HWADDR_ARG(vif_bssid));
    }
    else {
        if (blocked_vif == true)
            LOGN("%s sta connected to blocked vif", ow_steer_policy_get_prefix(pre_assoc_policy->base));
        else
            LOGD("%s sta connected to allowed vif bssid: "OSW_HWADDR_FMT, ow_steer_policy_get_prefix(pre_assoc_policy->base), OSW_HWADDR_ARG(vif_bssid));
    }

    ow_steer_policy_pre_assoc_reset_volatile_state(pre_assoc_policy);
    ow_steer_policy_schedule_stack_recalc(pre_assoc_policy->base);
}

static void
ow_steer_policy_pre_assoc_sta_disconnected_cb(struct osw_state_observer *observer,
                                              const struct osw_state_sta_info *sta_info)
{
    struct ow_steer_policy_pre_assoc *pre_assoc_policy = container_of(observer, struct ow_steer_policy_pre_assoc, state_observer);

    const struct osw_hwaddr *policy_sta_addr = ow_steer_policy_get_sta_addr(pre_assoc_policy->base);
    if (osw_hwaddr_cmp(sta_info->mac_addr, policy_sta_addr) != 0)
        return;

    struct ow_steer_policy_pre_assoc_persistent_state *pstate = &pre_assoc_policy->pstate;
    WARN_ON(pstate->active_link_cnt == 0);
    if (pstate->active_link_cnt > 0)
        pstate->active_link_cnt--;

    /* No config -> nop */
    const struct osw_hwaddr *vif_bssid = &sta_info->vif->drv_state->mac_addr;
    const struct ow_steer_policy_pre_assoc_config *config = pre_assoc_policy->config;
    if (config == NULL) {
        LOGD("%s sta disconnected from vif bssid: "OSW_HWADDR_FMT", (no conf)", ow_steer_policy_get_prefix(pre_assoc_policy->base), OSW_HWADDR_ARG(vif_bssid));
        return;
    }

    const struct osw_hwaddr *policy_bssid = &config->bssid;
    if (osw_hwaddr_cmp(policy_bssid, vif_bssid) == 0)
        LOGD("%s sta disconnected from blocked vif", ow_steer_policy_get_prefix(pre_assoc_policy->base));
    else
        LOGD("%s sta disconnected from allowed vif bssid: "OSW_HWADDR_FMT, ow_steer_policy_get_prefix(pre_assoc_policy->base), OSW_HWADDR_ARG(vif_bssid));

    if (pstate->active_link_cnt > 0)
        return;

    ow_steer_policy_schedule_stack_recalc(pre_assoc_policy->base);
}

static void
ow_steer_policy_pre_assoc_vif_probe_reject_counter(struct ow_steer_policy_pre_assoc *pre_assoc_policy,
                                                   const struct osw_drv_report_vif_probe_req *probe_req,
                                                   const char *vif_name)
{
    ASSERT(pre_assoc_policy->config->reject_condition.type == OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_COUNTER, "");

    const struct ow_steer_policy_pre_assoc_config *config = pre_assoc_policy->config;
    struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;

    const bool backoff_period_in_progress = osw_timer_is_armed(&vstate->backoff_timer);
    if (backoff_period_in_progress == true)
        return;

    /* Consume probe */
    vstate->reject_cnt++;

    /* Reject probe request */
    bool need_reject_period = true;
    bool need_backoff_period = false;

    /* Check backoff connfition */
    const struct ow_steer_policy_pre_assoc_reject_condition_counter *reject_condition = &config->reject_condition.params.counter;
    if (vstate->reject_cnt >= reject_condition->reject_limit) {
        need_reject_period = false;
        need_backoff_period = true;
    }

    const bool reject_period_in_progress = osw_timer_is_armed(&vstate->reject_timer);
    if (need_reject_period != reject_period_in_progress) {
        if (need_reject_period == true) {
            const uint64_t reject_period_end_nsec = osw_time_mono_clk() + OSW_TIME_SEC(reject_condition->reject_timeout_sec);
            osw_timer_arm_at_nsec(&vstate->reject_timer, reject_period_end_nsec);
            ow_steer_policy_notify_steering_attempt(pre_assoc_policy->base, vif_name);
            LOGD("%s reject period started", ow_steer_policy_get_prefix(pre_assoc_policy->base));
            ow_steer_policy_trigger_executor(pre_assoc_policy->base);
        }
        else {
            osw_timer_disarm(&vstate->reject_timer);
            LOGD("%s reject period stopped", ow_steer_policy_get_prefix(pre_assoc_policy->base));
            ow_steer_policy_dismiss_executor(pre_assoc_policy->base);
        }
    }

    if (need_backoff_period != backoff_period_in_progress) {
        if (need_backoff_period == true) {
            const double backoff_timeout_sec = ow_steer_policy_pre_assoc_compute_backoff_timeout(pre_assoc_policy);
            osw_timer_arm_at_nsec(&vstate->backoff_timer, osw_time_mono_clk() + OSW_TIME_SEC(backoff_timeout_sec));
            LOGD("%s backoff period started", ow_steer_policy_get_prefix(pre_assoc_policy->base));
            ow_steer_policy_notify_backoff(pre_assoc_policy->base, true, backoff_timeout_sec);
        }
        else {
            osw_timer_disarm(&vstate->backoff_timer);
            LOGD("%s backoff period stopped", ow_steer_policy_get_prefix(pre_assoc_policy->base));
        }
    }

    const bool need_stack_recalc = ((need_reject_period != reject_period_in_progress) ||
                                    (need_backoff_period != backoff_period_in_progress));
    if (need_stack_recalc == true) {
        ow_steer_policy_schedule_stack_recalc(pre_assoc_policy->base);
    }
}

static void
ow_steer_policy_pre_assoc_vif_probe_reject_timer(struct ow_steer_policy_pre_assoc *pre_assoc_policy,
                                                 const struct osw_drv_report_vif_probe_req *probe_req,
                                                 const char *vif_name)
{
    ASSERT(pre_assoc_policy->config->reject_condition.type == OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER, "");

    const struct ow_steer_policy_pre_assoc_config *config = pre_assoc_policy->config;
    struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;

    const bool backoff_period_in_progress = osw_timer_is_armed(&vstate->backoff_timer);
    if (backoff_period_in_progress == true)
        return;

    /* Consume probe */
    vstate->reject_cnt++;

    const bool reject_period_in_progress = osw_timer_is_armed(&vstate->reject_timer);
    if (reject_period_in_progress == false) {
        const struct ow_steer_policy_pre_assoc_reject_condition_timer *reject_condition = &config->reject_condition.params.timer;
        const uint64_t reject_period_end_nsec = osw_time_mono_clk() + OSW_TIME_MSEC(reject_condition->reject_timeout_msec);
        osw_timer_arm_at_nsec(&vstate->reject_timer, reject_period_end_nsec);
        ow_steer_policy_notify_steering_attempt(pre_assoc_policy->base, vif_name);
        LOGD("%s reject period started", ow_steer_policy_get_prefix(pre_assoc_policy->base));
        ow_steer_policy_trigger_executor(pre_assoc_policy->base);
    }
}

static void
ow_steer_policy_pre_assoc_vif_probe_backoff_threshold_snr(struct ow_steer_policy_pre_assoc *pre_assoc_policy,
                                                          const struct osw_drv_report_vif_probe_req *probe_req)
{
    ASSERT(pre_assoc_policy->config->backoff_condition.type == OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_THRESHOLD_SNR, "");

    const struct ow_steer_policy_pre_assoc_config *config = pre_assoc_policy->config;
    struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;

    const bool reject_period_in_progress = osw_timer_is_armed(&vstate->reject_timer);
    if (reject_period_in_progress == false)
        return;

    const struct ow_steer_policy_pre_assoc_backoff_condition_threshold_snr *backoff_condition = &config->backoff_condition.params.threshold_snr;
    const bool need_backoff_period = (probe_req->snr < backoff_condition->threshold_snr);
    if (need_backoff_period == false)
        return;

    const bool backoff_period_in_progress = osw_timer_is_armed(&vstate->backoff_timer);
    if (backoff_period_in_progress == true)
        return;

    osw_timer_disarm(&vstate->reject_timer);
    LOGD("%s reject period stopped", ow_steer_policy_get_prefix(pre_assoc_policy->base));
    ow_steer_policy_dismiss_executor(pre_assoc_policy->base);

    const double backoff_timeout_sec = ow_steer_policy_pre_assoc_compute_backoff_timeout(pre_assoc_policy);
    osw_timer_arm_at_nsec(&vstate->backoff_timer, osw_time_mono_clk() + OSW_TIME_SEC(backoff_timeout_sec));
    LOGD("%s backoff period started", ow_steer_policy_get_prefix(pre_assoc_policy->base));
    ow_steer_policy_notify_backoff(pre_assoc_policy->base, true, backoff_timeout_sec);

    ow_steer_policy_schedule_stack_recalc(pre_assoc_policy->base);
}

static void
ow_steer_policy_pre_assoc_vif_probe_req_cb(struct osw_state_observer *observer,
                                           const struct osw_state_vif_info *vif_info,
                                           const struct osw_drv_report_vif_probe_req *probe_req)
{
    struct ow_steer_policy_pre_assoc *pre_assoc_policy = container_of(observer, struct ow_steer_policy_pre_assoc, state_observer);

    /* Check probe's src */
    const struct osw_hwaddr *policy_sta_addr = ow_steer_policy_get_sta_addr(pre_assoc_policy->base);
    const struct osw_hwaddr *probe_sta_addr = &probe_req->sta_addr;
    if (osw_hwaddr_cmp(policy_sta_addr, probe_sta_addr) != 0)
        return;

    /* No config -> nop */
    const char *probe_type = probe_req->ssid.len > 0 ? "direct" : "wildcard";
    const struct ow_steer_policy_pre_assoc_config *config = pre_assoc_policy->config;
    if (config == NULL) {
        LOGD("%s probe req type: %s snr: %u, (no conf)", ow_steer_policy_get_prefix(pre_assoc_policy->base), probe_type, probe_req->snr);
        return;
    }

    /* Check probe's dest */
    const struct osw_hwaddr *policy_bssid = &config->bssid;
    const struct osw_hwaddr *probe_bssid = &vif_info->drv_state->mac_addr;
    if (osw_hwaddr_cmp(policy_bssid, probe_bssid) != 0)
        return;

    struct ow_steer_policy_pre_assoc_persistent_state *pstate = &pre_assoc_policy->pstate;
    if (pstate->active_link_cnt > 0) {
        LOGD("%s probe req type: %s snr: %u, ignored, sta is already connected", ow_steer_policy_get_prefix(pre_assoc_policy->base),
             probe_type, probe_req->snr);
        return;
    }

    struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;
    if (osw_timer_is_armed(&vstate->backoff_timer) == true) {
        LOGD("%s probe req type: %s snr: %u, backoff in progress", ow_steer_policy_get_prefix(pre_assoc_policy->base), probe_type, probe_req->snr);
        return;
    }

    LOGD("%s probe req type: %s snr: %u", ow_steer_policy_get_prefix(pre_assoc_policy->base), probe_type, probe_req->snr);

    switch (config->reject_condition.type) {
        case OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_COUNTER:
            ow_steer_policy_pre_assoc_vif_probe_reject_counter(pre_assoc_policy,
                                                               probe_req,
                                                               vif_info->vif_name);
            break;
        case OW_STEER_POLICY_PRE_ASSOC_REJECT_CONDITION_TIMER:
            ow_steer_policy_pre_assoc_vif_probe_reject_timer(pre_assoc_policy,
                                                             probe_req,
                                                             vif_info->vif_name);
            break;
    }

    switch (config->backoff_condition.type) {
        case OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_NONE:
            /* nop */
            break;
        case OW_STEER_POLICY_PRE_ASSOC_BACKOFF_CONDITION_THRESHOLD_SNR:
            ow_steer_policy_pre_assoc_vif_probe_backoff_threshold_snr(pre_assoc_policy, probe_req);
            break;
    }
}

static void
ow_steer_policy_pre_assoc_try_auth_backoff(struct ow_steer_policy_pre_assoc *pre_assoc_policy)
{
    struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;

    if (osw_timer_is_armed(&vstate->backoff_timer)) {
        LOGD("%s ignoring auth frame, already in backoff", ow_steer_policy_get_prefix(pre_assoc_policy->base));
        return;
    }

    if (osw_timer_is_armed(&vstate->reject_timer)) {
        /* This can happen if client scan turnaround time is
         * shorter than the reject timer. If that happens it
         * makes sense to abort reject timer and enter
         * backoff immediately.
         */
        LOGI("%s auth attempt after probe request but before reject stopped",
             ow_steer_policy_get_prefix(pre_assoc_policy->base));

        osw_timer_disarm(&vstate->reject_timer);
        LOGD("%s reject period stopped", ow_steer_policy_get_prefix(pre_assoc_policy->base));
        ow_steer_policy_dismiss_executor(pre_assoc_policy->base);
    }
    else {
        /* This is relatively rare, but possible: a client
         * may rely on its local BSS data cache or receive
         * Beacon frame. In either case it can start
         * Authentication immediately. If so, unblock it.
         */
        LOGI("%s auth attempt before probe request",
             ow_steer_policy_get_prefix(pre_assoc_policy->base));

        /* Simulate the reject timer being started and
         * immediatelly stopped to unify the handling of
         * this corner case.
         */
        ow_steer_policy_trigger_executor(pre_assoc_policy->base);
        ow_steer_policy_dismiss_executor(pre_assoc_policy->base);
    }

    const double backoff_timeout_sec = ow_steer_policy_pre_assoc_compute_backoff_timeout(pre_assoc_policy);
    const uint64_t backoff_at_nsec = osw_time_mono_clk() + OSW_TIME_SEC(backoff_timeout_sec);
    osw_timer_arm_at_nsec(&vstate->backoff_timer, backoff_at_nsec);
    LOGD("%s backoff period started", ow_steer_policy_get_prefix(pre_assoc_policy->base));

    const double bypass_timeout_sec = ow_steer_policy_pre_assoc_compute_auth_bypass_timeout_sec(pre_assoc_policy);
    const uint64_t bypass_at_nsec = osw_time_mono_clk() + OSW_TIME_SEC(bypass_timeout_sec);
    osw_timer_arm_at_nsec(&vstate->auth_bypass_timer, bypass_at_nsec);

    ow_steer_policy_schedule_stack_recalc(pre_assoc_policy->base);
}

static bool
ow_steer_policy_pre_assoc_is_auth_attempt(struct ow_steer_policy_pre_assoc *pre_assoc_policy,
                                          const void *data,
                                          size_t len)
{
    const struct ow_steer_policy_pre_assoc_config *config = pre_assoc_policy->config;

    /* Some clients do not tolerate Authentication attempts
     * being repeatedly rejected. For example iOS will
     * prompt the user the network credentials are invalid
     * and hard-block the network until it is manually
     * connected to by the user through UI.
     *
     * Some clients however simply put the BSS on hold
     * temporarily and will retry that BSS sometime later.
     * For example wpa_supplicant uses 10s timeout (and
     * grows that on subsequent failures).
     */
    if (config->immediate_backoff_on_auth_req == false) return false;

    size_t rem;
    const struct osw_drv_dot11_frame_header *hdr = ieee80211_frame_into_header(data, len, rem);
    const bool probably_not_valid_frame = (hdr == NULL);
    if (WARN_ON(probably_not_valid_frame)) return false;
    (void)rem;

    const uint16_t fc = le16toh(hdr->frame_control);
    const uint16_t subtype = (fc & DOT11_FRAME_CTRL_SUBTYPE_MASK);
    const bool is_not_auth = (subtype != DOT11_FRAME_CTRL_SUBTYPE_AUTH);
    if (is_not_auth) return false;

    const struct osw_hwaddr *policy_sta_addr = ow_steer_policy_get_sta_addr(pre_assoc_policy->base);
    const struct osw_hwaddr *policy_bssid = &config->bssid;
    const struct osw_hwaddr *frame_ta = osw_hwaddr_from_cptr_unchecked(hdr->sa);
    const struct osw_hwaddr *frame_ra = osw_hwaddr_from_cptr_unchecked(hdr->bssid);

    const bool non_policy_sta = (osw_hwaddr_is_equal(policy_sta_addr, frame_ta) == false);
    const bool non_policy_bssid = (osw_hwaddr_is_equal(policy_bssid, frame_ra) == false);
    const bool ignore = non_policy_sta
                     || non_policy_bssid;
    if (ignore) return false;

    return true;
}

static void
ow_steer_policy_pre_assoc_consider_auth_unblock(struct ow_steer_policy_pre_assoc *pre_assoc_policy,
                                                const void *data,
                                                size_t len)
{
    if (ow_steer_policy_pre_assoc_is_auth_attempt(pre_assoc_policy, data, len)) {
        /* This is intended to be handled upon
         * Authentication Reequest reception. This also
         * means that if the ACL is blocking the client then
         * the first Authentication attempt will have been
         * already rejected. That is fine and in all
         * likeliness the ACL will get unblocked by the time
         * client re-tries.
         */
        ow_steer_policy_pre_assoc_try_auth_backoff(pre_assoc_policy);
    }
}

static void
ow_steer_policy_pre_assoc_vif_frame_rx_cb(struct osw_state_observer *self,
                                          const struct osw_state_vif_info *vif,
                                          const uint8_t *data,
                                          size_t len)
{
    struct ow_steer_policy_pre_assoc *pre_assoc_policy = container_of(self, struct ow_steer_policy_pre_assoc, state_observer);
    ow_steer_policy_pre_assoc_consider_auth_unblock(pre_assoc_policy, data, len);
}

static void
ow_steer_policy_pre_assoc_recalc_cb(struct ow_steer_policy *policy,
                                    struct ow_steer_candidate_list *candidate_list)
{
    ASSERT(policy != NULL, "");
    ASSERT(candidate_list != NULL, "");

    struct ow_steer_policy_pre_assoc *pre_assoc_policy = ow_steer_policy_get_priv(policy);
    const struct ow_steer_policy_pre_assoc_config *config = pre_assoc_policy->config;
    if (config == NULL)
        return;

    const struct osw_hwaddr *policy_bssid = ow_steer_policy_get_bssid(policy);
    struct ow_steer_candidate *candidate = ow_steer_candidate_list_lookup(candidate_list, policy_bssid);
    if (candidate == NULL) {
        LOGW("%s candidate bssid: "OSW_HWADDR_FMT" is missing", ow_steer_policy_get_prefix(policy), OSW_HWADDR_ARG(policy_bssid));
        ow_steer_policy_pre_assoc_reset_volatile_state(pre_assoc_policy);
        return;
    }

    const enum ow_steer_candidate_preference preference = ow_steer_candidate_get_preference(candidate);
    if (preference != OW_STEER_CANDIDATE_PREFERENCE_NONE) {
        LOGD("%s bssid: "OSW_HWADDR_FMT" preference: %s, already set", ow_steer_policy_get_prefix(policy), OSW_HWADDR_ARG(policy_bssid),
             ow_steer_candidate_preference_to_cstr(preference));
        ow_steer_policy_pre_assoc_reset_volatile_state(pre_assoc_policy);
        return;
    }

    struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;
    if (osw_timer_is_armed(&vstate->backoff_timer) == false)
        ow_steer_candidate_set_preference(candidate, OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    else
        ow_steer_candidate_set_preference(candidate, OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

    LOGD("%s bssid: "OSW_HWADDR_FMT" preference: %s", ow_steer_policy_get_prefix(policy), OSW_HWADDR_ARG(policy_bssid),
         ow_steer_candidate_preference_to_cstr(ow_steer_candidate_get_preference(candidate)));
}

static void
ow_steer_policy_pre_assoc_reconf_timer_cb(struct osw_timer *timer)
{
    struct ow_steer_policy_pre_assoc *pre_assoc_policy = container_of(timer, struct ow_steer_policy_pre_assoc, reconf_timer);

    bool unregister_observer = false;
    bool register_observer = false;

    if (pre_assoc_policy->config == NULL && pre_assoc_policy->next_config == NULL) {
        /* nop */
        return;
    }
    else if (pre_assoc_policy->config == NULL && pre_assoc_policy->next_config != NULL) {
        LOGI("%s config added", ow_steer_policy_get_prefix(pre_assoc_policy->base));
        register_observer = true;
    }
    else if (pre_assoc_policy->config != NULL && pre_assoc_policy->next_config == NULL) {
        LOGI("%s config removed", ow_steer_policy_get_prefix(pre_assoc_policy->base));
        unregister_observer = true;
    }
    else if (pre_assoc_policy->config != NULL && pre_assoc_policy->next_config != NULL) {
        if (memcmp(pre_assoc_policy->config, pre_assoc_policy->next_config, sizeof(*pre_assoc_policy->config)) == 0) {
            FREE(pre_assoc_policy->next_config);
            pre_assoc_policy->next_config = NULL;
            return;
        }

        LOGI("%s config changed", ow_steer_policy_get_prefix(pre_assoc_policy->base));
        unregister_observer = true;
        register_observer = true;
    }
    else {
        ASSERT(false, "");
    }

    const bool reset_policy_state = unregister_observer || register_observer;
    if (reset_policy_state == true) {
        ow_steer_policy_pre_assoc_reset_volatile_state(pre_assoc_policy);
        ow_steer_policy_schedule_stack_recalc(pre_assoc_policy->base);
    }

    FREE(pre_assoc_policy->config);
    pre_assoc_policy->config = NULL;

    if (unregister_observer == true)
        osw_state_unregister_observer(&pre_assoc_policy->state_observer);

    pre_assoc_policy->config = pre_assoc_policy->next_config;
    pre_assoc_policy->next_config = NULL;

    const struct osw_hwaddr *bssid = pre_assoc_policy->config != NULL ? &pre_assoc_policy->config->bssid : NULL;
    ow_steer_policy_set_bssid(pre_assoc_policy->base, bssid);

    if (register_observer == true)
        osw_state_register_observer(&pre_assoc_policy->state_observer);
}

struct ow_steer_policy_pre_assoc*
ow_steer_policy_pre_assoc_create(const struct osw_hwaddr *sta_addr,
                                 const struct ow_steer_policy_mediator *mediator)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(mediator != NULL, "");

    const struct ow_steer_policy_ops ops = {
        .sigusr1_dump_fn = ow_steer_policy_pre_assoc_sigusr1_dump_cb,
        .recalc_fn = ow_steer_policy_pre_assoc_recalc_cb,
    };
    const struct osw_state_observer state_observer = {
        .name = g_policy_name,
        .sta_connected_fn = ow_steer_policy_pre_assoc_sta_connected_cb,
        .sta_disconnected_fn = ow_steer_policy_pre_assoc_sta_disconnected_cb,
        .vif_probe_req_fn = ow_steer_policy_pre_assoc_vif_probe_req_cb,
        .vif_frame_rx_fn = ow_steer_policy_pre_assoc_vif_frame_rx_cb,
    };

    struct ow_steer_policy_pre_assoc *pre_assoc_policy = CALLOC(1, sizeof(*pre_assoc_policy));
    memcpy(&pre_assoc_policy->state_observer, &state_observer, sizeof(pre_assoc_policy->state_observer));
    osw_timer_init(&pre_assoc_policy->reconf_timer, ow_steer_policy_pre_assoc_reconf_timer_cb);

    struct ow_steer_policy_pre_assoc_volatile_state *vstate = &pre_assoc_policy->vstate;
    osw_timer_init(&vstate->reject_timer, ow_steer_policy_pre_assoc_reject_timer_cb);
    osw_timer_init(&vstate->backoff_timer, ow_steer_policy_pre_assoc_backoff_timer_cb);
    osw_timer_init(&vstate->auth_bypass_timer, ow_steer_policy_pre_assoc_auth_bypass_timer_cb);

    pre_assoc_policy->base = ow_steer_policy_create(g_policy_name, sta_addr, &ops, mediator, pre_assoc_policy);

    return pre_assoc_policy;
}

void
ow_steer_policy_pre_assoc_set_config(struct ow_steer_policy_pre_assoc *pre_assoc_policy,
                                             struct ow_steer_policy_pre_assoc_config *config)
{
    ASSERT(pre_assoc_policy != NULL, "");

    FREE(pre_assoc_policy->next_config);
    pre_assoc_policy->next_config = config;

    osw_timer_arm_at_nsec(&pre_assoc_policy->reconf_timer, 0);
}

void
ow_steer_policy_pre_assoc_free(struct ow_steer_policy_pre_assoc *pre_assoc_policy)
{
    if (pre_assoc_policy == NULL)
        return;

    const bool unregister_observer = pre_assoc_policy->config != NULL;

    ow_steer_policy_pre_assoc_reset_volatile_state(pre_assoc_policy);
    osw_timer_disarm(&pre_assoc_policy->reconf_timer);
    FREE(pre_assoc_policy->next_config);
    pre_assoc_policy->next_config = NULL;
    FREE(pre_assoc_policy->config);
    pre_assoc_policy->config = NULL;
    if (unregister_observer == true)
        osw_state_unregister_observer(&pre_assoc_policy->state_observer);
    ow_steer_policy_free(pre_assoc_policy->base);
    FREE(pre_assoc_policy);
}

struct ow_steer_policy*
ow_steer_policy_pre_assoc_get_base(struct ow_steer_policy_pre_assoc *pre_assoc_policy)
{
    ASSERT(pre_assoc_policy != NULL, "");
    return pre_assoc_policy->base;
}

const struct osw_hwaddr *
ow_steer_policy_pre_assoc_get_bssid(const struct ow_steer_policy_pre_assoc *counter_policy)
{
    if (counter_policy == NULL) return NULL;
    if (counter_policy->config == NULL) return NULL;
    return &counter_policy->config->bssid;
}

#include "ow_steer_policy_pre_assoc_ut.c"
