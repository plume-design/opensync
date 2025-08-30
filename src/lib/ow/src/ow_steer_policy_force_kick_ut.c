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
#include "ow_steer_policy_i.h"

#define OW_STEER_POLICY_FORCE_KICK_UT_STA_ADDR { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, }, }
#define OW_STEER_POLICY_FORCE_KICK_UT_SELF_BSSID_1 { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, }, }
#define OW_STEER_POLICY_FORCE_KICK_UT_SELF_BSSID_2 { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, }, }
#define OW_STEER_POLICY_FORCE_KICK_UT_NEIGHBOR_BSSID { .octet = { 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, }, }

static const struct osw_hwaddr g_sta_addr = OW_STEER_POLICY_FORCE_KICK_UT_STA_ADDR;
static const struct osw_hwaddr g_self_bssid_1 = OW_STEER_POLICY_FORCE_KICK_UT_SELF_BSSID_1;
static const struct osw_hwaddr g_self_bssid_2 = OW_STEER_POLICY_FORCE_KICK_UT_SELF_BSSID_2;
static const struct osw_hwaddr g_neighbor_bssid = OW_STEER_POLICY_FORCE_KICK_UT_NEIGHBOR_BSSID;

static const struct osw_drv_vif_state g_drv_vif_state_1 = {
    .mac_addr = OW_STEER_POLICY_FORCE_KICK_UT_SELF_BSSID_1,
};
static const struct osw_drv_vif_state g_drv_vif_state_2 = {
    .mac_addr = OW_STEER_POLICY_FORCE_KICK_UT_SELF_BSSID_2,
};
static const struct osw_state_vif_info g_vif_1 = {
    .vif_name = "vif_1",
    .drv_state = &g_drv_vif_state_1,
};
static const struct osw_state_vif_info g_vif_2 = {
    .vif_name = "vif_1",
    .drv_state = &g_drv_vif_state_2,
};
static const struct osw_state_sta_info g_sta_info_1 = {
    .mac_addr = &g_sta_addr,
    .vif = &g_vif_1,
};
static const struct osw_state_sta_info g_sta_info_2 = {
    .mac_addr = &g_sta_addr,
    .vif = &g_vif_2,
};

struct ow_steer_policy_force_kick_ut_mediator_cnt {
    unsigned int schedule_recalc_cnt;
    unsigned int trigger_executor_cnt;
    unsigned int dismiss_executor_cnt;
};

static void
ow_steer_policy_force_kick_ut_mediator_sched_stack_recalc(struct ow_steer_policy *policy,
                                                        void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    struct ow_steer_policy_force_kick_ut_mediator_cnt *cnt = priv;
    cnt->schedule_recalc_cnt++;
}

static bool
ow_steer_policy_force_kick_ut_mediator_trigger_executor(struct ow_steer_policy *policy,
                                                       void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    struct ow_steer_policy_force_kick_ut_mediator_cnt *cnt = priv;
    cnt->trigger_executor_cnt++;
    return true;
}

static void
ow_steer_policy_force_kick_ut_mediator_dismiss_executor(struct ow_steer_policy *policy,
                                                             void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    struct ow_steer_policy_force_kick_ut_mediator_cnt *cnt = priv;
    cnt->dismiss_executor_cnt++;
}

OSW_UT(ow_steer_policy_force_kick_successful_ut)
{
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };

    struct ow_steer_policy_force_kick_config config = {};
    struct ow_steer_policy_force_kick_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_force_kick_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_force_kick_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_force_kick_ut_mediator_dismiss_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_force_kick *force_policy = NULL;
    struct ow_steer_candidate_list *candidate_list = NULL;
    struct ow_steer_candidate *self_candidate_1 = NULL;
    struct ow_steer_candidate *self_candidate_2 = NULL;
    struct ow_steer_candidate *neighbor_candidate = NULL;

    /*
     * Setup
     */
    osw_ut_time_init();

    force_policy = ow_steer_policy_force_kick_create(&g_sta_addr, &mediator, "");
    OSW_UT_EVAL(force_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &g_self_bssid_1, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &g_self_bssid_2, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &g_neighbor_bssid, &channel);
    self_candidate_1 = ow_steer_candidate_list_lookup(candidate_list, &g_self_bssid_1);
    self_candidate_2 = ow_steer_candidate_list_lookup(candidate_list, &g_self_bssid_2);
    neighbor_candidate = ow_steer_candidate_list_lookup(candidate_list, &g_neighbor_bssid);

    /*
     * Connect STA
     */
    force_policy->state_observer.sta_connected_fn(&force_policy->state_observer, &g_sta_info_1);
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    force_policy->base->ops.recalc_fn(force_policy->base, candidate_list);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_2) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(neighbor_candidate) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Trigger kick
     */
    ow_steer_policy_force_kick_set_oneshot_config(force_policy, MEMNDUP(&config, sizeof(config)));
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    force_policy->base->ops.recalc_fn(force_policy->base, candidate_list);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_2) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(neighbor_candidate) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Disconnect STA
     */
    force_policy->state_observer.sta_disconnected_fn(&force_policy->state_observer, &g_sta_info_1);
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    ow_steer_candidate_list_clear(candidate_list);
    force_policy->base->ops.recalc_fn(force_policy->base, candidate_list);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_2) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(neighbor_candidate) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_force_kick_timeout_ut)
{
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };

    struct ow_steer_policy_force_kick_config config = {};
    struct ow_steer_policy_force_kick_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_force_kick_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_force_kick_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_force_kick_ut_mediator_dismiss_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_force_kick *force_policy = NULL;
    struct ow_steer_candidate_list *candidate_list = NULL;
    struct ow_steer_candidate *self_candidate_1 = NULL;
    struct ow_steer_candidate *self_candidate_2 = NULL;
    struct ow_steer_candidate *neighbor_candidate = NULL;

    /*
     * Setup
     */
    osw_ut_time_init();

    force_policy = ow_steer_policy_force_kick_create(&g_sta_addr, &mediator, "");
    OSW_UT_EVAL(force_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &g_self_bssid_1, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &g_self_bssid_2, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &g_neighbor_bssid, &channel);
    self_candidate_1 = ow_steer_candidate_list_lookup(candidate_list, &g_self_bssid_1);
    self_candidate_2 = ow_steer_candidate_list_lookup(candidate_list, &g_self_bssid_2);
    neighbor_candidate = ow_steer_candidate_list_lookup(candidate_list, &g_neighbor_bssid);

    /*
     * Connect STA
     */
    force_policy->state_observer.sta_connected_fn(&force_policy->state_observer, &g_sta_info_1);
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    force_policy->base->ops.recalc_fn(force_policy->base, candidate_list);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_2) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(neighbor_candidate) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Trigger kick
     */
    ow_steer_policy_force_kick_set_oneshot_config(force_policy, MEMNDUP(&config, sizeof(config)));
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    force_policy->base->ops.recalc_fn(force_policy->base, candidate_list);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_2) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(neighbor_candidate) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * STA doesn't do anything
     */
    osw_ut_time_advance(OSW_TIME_SEC(25));

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    ow_steer_candidate_list_clear(candidate_list);
    force_policy->base->ops.recalc_fn(force_policy->base, candidate_list);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_2) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(neighbor_candidate) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

OSW_UT(ow_steer_policy_force_kick_disconnected_sta_ut)
{
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };

    struct ow_steer_policy_force_kick_config config = {};
    struct ow_steer_policy_force_kick_ut_mediator_cnt mediator_cnt = {
        .schedule_recalc_cnt = 0,
        .trigger_executor_cnt = 0,
        .dismiss_executor_cnt = 0,
    };
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_force_kick_ut_mediator_sched_stack_recalc,
        .trigger_executor_fn = ow_steer_policy_force_kick_ut_mediator_trigger_executor,
        .dismiss_executor_fn = ow_steer_policy_force_kick_ut_mediator_dismiss_executor,
        .priv = &mediator_cnt,
    };
    struct ow_steer_policy_force_kick *force_policy = NULL;
    struct ow_steer_candidate_list *candidate_list = NULL;
    struct ow_steer_candidate *self_candidate_1 = NULL;
    struct ow_steer_candidate *self_candidate_2 = NULL;
    struct ow_steer_candidate *neighbor_candidate = NULL;

    /*
     * Setup
     */
    osw_ut_time_init();

    force_policy = ow_steer_policy_force_kick_create(&g_sta_addr, &mediator, "");
    OSW_UT_EVAL(force_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &g_self_bssid_1, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &g_self_bssid_2, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &g_neighbor_bssid, &channel);
    self_candidate_1 = ow_steer_candidate_list_lookup(candidate_list, &g_self_bssid_1);
    self_candidate_2 = ow_steer_candidate_list_lookup(candidate_list, &g_self_bssid_2);
    neighbor_candidate = ow_steer_candidate_list_lookup(candidate_list, &g_neighbor_bssid);

    /*
     * Trigger kick
     */
    ow_steer_policy_force_kick_set_oneshot_config(force_policy, MEMNDUP(&config, sizeof(config)));
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    ow_steer_candidate_list_clear(candidate_list);
    force_policy->base->ops.recalc_fn(force_policy->base, candidate_list);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_2) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(neighbor_candidate) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}
