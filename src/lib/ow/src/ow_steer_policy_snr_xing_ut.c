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

#include <osw_ut.h>
#include <os.h>
#include "ow_steer_policy_i.h"

/* From osw_util */
extern const uint8_t osw_util_ut_non_11kv_assoc_ies[];
extern const size_t osw_util_ut_non_11kv_assoc_ies_len;
extern const uint8_t osw_util_ut_11kv_assoc_ies[];
extern const size_t osw_util_ut_11kv_assoc_ies_len;

struct ow_steer_policy_snr_xing_ut_mediator_cnt {
    unsigned int schedule_recalc_cnt;
    unsigned int trigger_executor_cnt;
    unsigned int dismiss_executor_cnt;
};

static void
ow_steer_policy_snr_xing_ut_mediator_sched_stack_recalc(struct ow_steer_policy *policy,
                                                        void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    struct ow_steer_policy_snr_xing_ut_mediator_cnt *cnt = priv;
    cnt->schedule_recalc_cnt++;
}

static bool
ow_steer_policy_snr_xing_ut_mediator_trigger_executor(struct ow_steer_policy *policy,
                                                       void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    struct ow_steer_policy_snr_xing_ut_mediator_cnt *cnt = priv;
    cnt->trigger_executor_cnt++;
    return true;
}

static void
ow_steer_policy_snr_xing_ut_mediator_dismis_executor(struct ow_steer_policy *policy,
                                                             void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    struct ow_steer_policy_snr_xing_ut_mediator_cnt *cnt = priv;
    cnt->dismiss_executor_cnt++;
}

OSW_UT(ow_steer_policy_snr_xing_ut_eval_xing_change)
{
    struct ow_steer_policy_snr_xing_config config_buf;
    memset(&config_buf, 0, sizeof(config_buf));

    struct ow_steer_policy_snr_xing xing_policy;
    memset(&xing_policy, 0, sizeof(xing_policy));
    xing_policy.config = &config_buf;

    struct ow_steer_policy_snr_xing_config *config = xing_policy.config;
    struct ow_steer_policy_snr_xing_state *state = &xing_policy.state;
    size_t i;

    ow_steer_policy_snr_xing_state_reset(&xing_policy);
    config->snr = 100;

    /* none -> below */
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 90;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);

    /* below -> below */
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 80;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 70;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);

    /* below -> threshold */
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 100;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_UP);

    /* threshold -> threshold */
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 100;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);

    /* threshold -> up */
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 110;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 120;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);

    /* up -> threshold */
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 100;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);

    /* threshold -> threshold */
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 100;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);

    /* threshold -> up */
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 110;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 120;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);

    /* up -> down */
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 60;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_DOWN);
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 70;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);

    /* down -> up */
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 110;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_UP);
    i = osw_circ_buf_push_rotate(&state->snr_buf);
    state->snr[i] = 120;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_xing_change(&xing_policy) == OW_STEER_POLICY_SNR_XING_CHANGE_NONE);
}

OSW_UT(ow_steer_policy_snr_xing_ut_eval_txrx_state)
{
    struct ow_steer_policy_snr_xing_config enabled;
    struct ow_steer_policy_snr_xing_config disabled;
    struct ow_steer_policy_mediator mediator;
    struct osw_hwaddr addr;
    MEMZERO(enabled);
    MEMZERO(disabled);
    MEMZERO(mediator);
    MEMZERO(addr);

    struct ow_steer_policy_snr_xing *xing = ow_steer_policy_snr_xing_create("test", &addr, &mediator);
    struct ow_steer_policy_snr_xing_state *state = &xing->state;

    /*
     * HWM
     */
    enabled.mode = OW_STEER_POLICY_SNR_XING_MODE_HWM;
    enabled.mode_config.hwm.txrx_bytes_limit.active = true;
    enabled.mode_config.hwm.txrx_bytes_limit.delta = 1000;

    disabled.mode = OW_STEER_POLICY_SNR_XING_MODE_HWM;
    disabled.mode_config.hwm.txrx_bytes_limit.active = false;
    disabled.mode_config.hwm.txrx_bytes_limit.delta = 1000;

    /* Empty buffer */
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&enabled, sizeof(enabled)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE);
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&disabled, sizeof(disabled)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);

    /* Below threshold */
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&enabled, sizeof(enabled)));
    osw_ut_time_advance(0);
    ow_steer_policy_snr_xing_activity_feed(&state->activity, 500);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);

    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&disabled, sizeof(disabled)));
    osw_ut_time_advance(0);
    ow_steer_policy_snr_xing_activity_feed(&state->activity, 500);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);

    /* Above threshold */
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&enabled, sizeof(enabled)));
    osw_ut_time_advance(0);
    ow_steer_policy_snr_xing_activity_feed(&state->activity, 7000);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE);

    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&disabled, sizeof(disabled)));
    osw_ut_time_advance(0);
    ow_steer_policy_snr_xing_activity_feed(&state->activity, 7000);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);

    /*
     * LWM
     */
    enabled.mode = OW_STEER_POLICY_SNR_XING_MODE_LWM;
    enabled.mode_config.hwm.txrx_bytes_limit.active = true;
    enabled.mode_config.hwm.txrx_bytes_limit.delta = 1000;

    disabled.mode = OW_STEER_POLICY_SNR_XING_MODE_LWM;
    disabled.mode_config.hwm.txrx_bytes_limit.active = false;
    disabled.mode_config.hwm.txrx_bytes_limit.delta = 1000;

    /* Empty buffer */
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&enabled, sizeof(enabled)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE);
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&disabled, sizeof(disabled)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);

    /* Below threshold */
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&enabled, sizeof(enabled)));
    osw_ut_time_advance(0);
    ow_steer_policy_snr_xing_activity_feed(&state->activity, 500);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);

    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&disabled, sizeof(disabled)));
    osw_ut_time_advance(0);
    ow_steer_policy_snr_xing_activity_feed(&state->activity, 500);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);

    /* Above threshold */
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&enabled, sizeof(enabled)));
    osw_ut_time_advance(0);
    ow_steer_policy_snr_xing_activity_feed(&state->activity, 7000);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_ACTIVE);

    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&disabled, sizeof(disabled)));
    osw_ut_time_advance(0);
    ow_steer_policy_snr_xing_activity_feed(&state->activity, 7000);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);

    /*
     * Bottom LWM
     */
    enabled.mode = OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM;

    /* Empty buffer */
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&disabled, sizeof(disabled)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);

    /* Low delta */
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&enabled, sizeof(enabled)));
    osw_ut_time_advance(0);
    ow_steer_policy_snr_xing_activity_feed(&state->activity, 50);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);

    /* High delta */
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&enabled, sizeof(enabled)));
    osw_ut_time_advance(0);
    ow_steer_policy_snr_xing_activity_feed(&state->activity, 70000);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(ow_steer_policy_snr_xing_eval_txrx_state(xing) == OW_STEER_POLICY_SNR_XING_TXRX_STATE_IDLE);
}

OSW_UT(ow_steer_policy_snr_xing_ut_check_sta_caps)
{
    struct ow_steer_policy_snr_xing_config config_buf;
    memset(&config_buf, 0, sizeof(config_buf));

    struct ow_steer_policy_snr_xing xing_policy;
    memset(&xing_policy, 0, sizeof(xing_policy));
    xing_policy.config = &config_buf;

    struct ow_steer_policy_snr_xing_config *config = xing_policy.config;
    struct ow_steer_policy_snr_xing_state *state = &xing_policy.state;

    /*
     * HWM
     */
    config->mode = OW_STEER_POLICY_SNR_XING_MODE_HWM;

    state->rrm_neighbor_bcn_act_meas = false;
    state->wnm_bss_trans = false;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == true);

    state->rrm_neighbor_bcn_act_meas = true;
    state->wnm_bss_trans = false;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == true);

    state->rrm_neighbor_bcn_act_meas = false;
    state->wnm_bss_trans = true;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == true);

    state->rrm_neighbor_bcn_act_meas = true;
    state->wnm_bss_trans = true;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == true);

    /*
     * LWM
     */
    config->mode = OW_STEER_POLICY_SNR_XING_MODE_LWM;

    state->rrm_neighbor_bcn_act_meas = false;
    state->wnm_bss_trans = false;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == true);

    state->rrm_neighbor_bcn_act_meas = true;
    state->wnm_bss_trans = false;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == true);

    state->rrm_neighbor_bcn_act_meas = false;
    state->wnm_bss_trans = true;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == true);

    state->rrm_neighbor_bcn_act_meas = true;
    state->wnm_bss_trans = true;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == true);

    /*
     * Bottom LWM
     */
    config->mode = OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM;

    state->rrm_neighbor_bcn_act_meas = false;
    state->wnm_bss_trans = false;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == true);

    state->rrm_neighbor_bcn_act_meas = true;
    state->wnm_bss_trans = false;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == false);

    state->rrm_neighbor_bcn_act_meas = false;
    state->wnm_bss_trans = true;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == false);

    state->rrm_neighbor_bcn_act_meas = true;
    state->wnm_bss_trans = true;
    OSW_UT_EVAL(ow_steer_policy_snr_xing_check_sta_caps(&xing_policy) == false);
}

OSW_UT(ow_steer_policy_snr_xing_ut_hwm)
{
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct osw_hwaddr bssid = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, }, };
    const struct osw_hwaddr other_bssid = { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, }, };
    const struct osw_hwaddr sta_addr = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, }, };
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif = {
        .vif_name = "vif_0",
        .drv_state = &drv_vif_state,
    };
    struct osw_state_sta_info sta_info = {
        .mac_addr = &sta_addr,
        .vif = &vif,
        .assoc_req_ies = &osw_util_ut_11kv_assoc_ies,
        .assoc_req_ies_len = osw_util_ut_11kv_assoc_ies_len,
    };
    struct ow_steer_policy_snr_xing_config config = {
        .bssid = bssid,
        .snr = 50,
        .mode = OW_STEER_POLICY_SNR_XING_MODE_HWM,
        .mode_config.hwm.txrx_bytes_limit = {
            .active = true,
            .delta = 2000,
        },
    };
    struct ow_steer_policy_snr_xing_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_snr_xing_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_snr_xing_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_snr_xing_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_snr_xing *xing_policy;
    struct ow_steer_candidate_list *candidate_list;
    struct ow_steer_candidate *candidate_cur;
    struct ow_steer_candidate *candidate_other;

    /*
     * Setup
     */
    osw_ut_time_init();

    xing_policy = ow_steer_policy_snr_xing_create("hwm", &sta_addr, &mediator);
    OSW_UT_EVAL(xing_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_snr_xing_set_config(xing_policy, MEMNDUP(&config, sizeof(config)));
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &bssid, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &other_bssid, &channel);

    candidate_cur = ow_steer_candidate_list_lookup(candidate_list, &bssid);
    candidate_other = ow_steer_candidate_list_lookup(candidate_list, &other_bssid);

    /*
     * Connect 11kv STA
     */
    xing_policy->state_observer.sta_connected_fn(&xing_policy->state_observer, &sta_info);

    /*
     * Report stats (low SNR, high data vol diff)
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 40);
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 3000);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report the same stats
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 40);
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 3000);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 35);
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 3200);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 38);
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 3800);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report high SNR (>HWM)
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 60);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report low SNR (<HWM)
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 20);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);


    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 30);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report high SNR (>HWM)
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 60);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report low data vol diff
     */
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 1000);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    osw_ut_time_advance(OSW_TIME_SEC(xing_policy->state.activity.grace_seconds + 1));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 700);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 800);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

    /*
     * Check whether policy dismisses
     */
    osw_ut_time_advance(OSW_TIME_SEC(10));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_snr_xing_ut_lwm)
{
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct osw_hwaddr bssid = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, }, };
    const struct osw_hwaddr other_bssid = { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, }, };
    const struct osw_hwaddr sta_addr = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, }, };
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif = {
        .vif_name = "vif_0",
        .drv_state = &drv_vif_state,
    };
    struct osw_state_sta_info sta_info = {
        .mac_addr = &sta_addr,
        .vif = &vif,
        .assoc_req_ies = &osw_util_ut_11kv_assoc_ies,
        .assoc_req_ies_len = osw_util_ut_11kv_assoc_ies_len,
    };
    struct ow_steer_policy_snr_xing_config config = {
        .bssid = bssid,
        .snr = 50,
        .mode = OW_STEER_POLICY_SNR_XING_MODE_LWM,
        .mode_config.lwm.txrx_bytes_limit = {
            .active = true,
            .delta = 2000,
        },
    };
    struct ow_steer_policy_snr_xing_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_snr_xing_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_snr_xing_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_snr_xing_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_snr_xing *xing_policy;
    struct ow_steer_candidate_list *candidate_list;
    struct ow_steer_candidate *candidate_cur;
    struct ow_steer_candidate *candidate_other;

    /*
     * Setup
     */
    osw_ut_time_init();

    xing_policy = ow_steer_policy_snr_xing_create("lwm", &sta_addr, &mediator);
    OSW_UT_EVAL(xing_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_snr_xing_set_config(xing_policy, MEMNDUP(&config, sizeof(config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &bssid, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &other_bssid, &channel);

    candidate_cur = ow_steer_candidate_list_lookup(candidate_list, &bssid);
    candidate_other = ow_steer_candidate_list_lookup(candidate_list, &other_bssid);

    /*
     * Connect 11kv STA
     */
    xing_policy->state_observer.sta_connected_fn(&xing_policy->state_observer, &sta_info);

    /*
     * Report stats (high SNR, high data vol diff)
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 65);
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 3000);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report the same stats
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 65);
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 3000);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report low SNR (<LWM)
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 40);
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

    /*
     * Check whether policy dismisses
     */
    osw_ut_time_advance(OSW_TIME_SEC(10));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_snr_xing_ut_bottom_lwm_11kv_sta)
{
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct osw_hwaddr bssid = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, }, };
    const struct osw_hwaddr other_bssid = { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, }, };
    const struct osw_hwaddr sta_addr = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, }, };
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif = {
        .vif_name = "vif_0",
        .drv_state = &drv_vif_state,
    };
    struct osw_state_sta_info sta_info = {
        .mac_addr = &sta_addr,
        .vif = &vif,
        .assoc_req_ies = &osw_util_ut_11kv_assoc_ies,
        .assoc_req_ies_len = osw_util_ut_11kv_assoc_ies_len,
    };
    struct ow_steer_policy_snr_xing_config config = {
        .bssid = bssid,
        .snr = 50,
        .mode = OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM,
    };
    struct ow_steer_policy_snr_xing_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_snr_xing_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_snr_xing_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_snr_xing_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_snr_xing *xing_policy;
    struct ow_steer_candidate_list *candidate_list;
    struct ow_steer_candidate *candidate_cur;
    struct ow_steer_candidate *candidate_other;

    /*
     * Setup
     */
    osw_ut_time_init();

    xing_policy = ow_steer_policy_snr_xing_create("lwm", &sta_addr, &mediator);
    OSW_UT_EVAL(xing_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_snr_xing_set_config(xing_policy, MEMNDUP(&config, sizeof(config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &bssid, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &other_bssid, &channel);

    candidate_cur = ow_steer_candidate_list_lookup(candidate_list, &bssid);
    candidate_other = ow_steer_candidate_list_lookup(candidate_list, &other_bssid);

    /*
     * Connect 11kv STA
     */
    xing_policy->state_observer.sta_connected_fn(&xing_policy->state_observer, &sta_info);

    /*
     * Report stats (high SNR, high data vol diff)
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 65);
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 3000);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report the same stats
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 65);
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 3000);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report low SNR (<Bottom LWM)
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 40);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Check whether policy dismisses
     */
    osw_ut_time_advance(OSW_TIME_SEC(10));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_snr_xing_ut_bottom_lwm_non_11kv_sta)
{
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct osw_hwaddr bssid = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, }, };
    const struct osw_hwaddr other_bssid = { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, }, };
    const struct osw_hwaddr sta_addr = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, }, };
    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = bssid,
    };
    struct osw_state_vif_info vif = {
        .vif_name = "vif_0",
        .drv_state = &drv_vif_state,
    };
    struct osw_state_sta_info sta_info = {
        .mac_addr = &sta_addr,
        .vif = &vif,
        .assoc_req_ies = &osw_util_ut_non_11kv_assoc_ies,
        .assoc_req_ies_len = osw_util_ut_non_11kv_assoc_ies_len,
    };
    struct ow_steer_policy_snr_xing_config config = {
        .bssid = bssid,
        .snr = 50,
        .mode = OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM,
    };
    struct ow_steer_policy_snr_xing_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_snr_xing_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_snr_xing_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_snr_xing_ut_mediator_dismis_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_snr_xing *xing_policy;
    struct ow_steer_candidate_list *candidate_list;
    struct ow_steer_candidate *candidate_cur;
    struct ow_steer_candidate *candidate_other;

    /*
     * Setup
     */
    osw_ut_time_init();

    xing_policy = ow_steer_policy_snr_xing_create("lwm", &sta_addr, &mediator);
    OSW_UT_EVAL(xing_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_policy_snr_xing_set_config(xing_policy, MEMNDUP(&config, sizeof(config)));
    osw_ut_time_advance(0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &bssid, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &other_bssid, &channel);

    candidate_cur = ow_steer_candidate_list_lookup(candidate_list, &bssid);
    candidate_other = ow_steer_candidate_list_lookup(candidate_list, &other_bssid);

    /*
     * Connect non-11kv STA
     */
    xing_policy->state_observer.sta_connected_fn(&xing_policy->state_observer, &sta_info);

    /*
     * Report stats (high SNR, high data vol diff)
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 65);
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 3000);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report the same stats
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 65);
    xing_policy->base->ops.sta_data_vol_change_fn(xing_policy->base, &bssid, 3000);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Report low SNR (<Bottom LWM)
     */
    xing_policy->base->ops.sta_snr_change_fn(xing_policy->base, &bssid, 40);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

    /*
     * Check whether policy dismisses
     */
    osw_ut_time_advance(OSW_TIME_SEC(10));
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 3);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    ow_steer_candidate_list_clear(candidate_list);
    xing_policy->base->ops.recalc_fn(xing_policy->base, candidate_list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_cur) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(candidate_other) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}
