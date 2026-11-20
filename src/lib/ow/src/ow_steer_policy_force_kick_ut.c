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

static const osw_sta_assoc_links_t g_assoc_links_1 = {
    .count = 1,
    .links = {
        {
            .local_sta_addr = OW_STEER_POLICY_FORCE_KICK_UT_SELF_BSSID_1,
            .remote_sta_addr = OW_STEER_POLICY_FORCE_KICK_UT_STA_ADDR,
        },
    },
};

static const osw_sta_assoc_links_t g_assoc_links_2 = {
    .count = 1,
    .links = {
        {
            .local_sta_addr = OW_STEER_POLICY_FORCE_KICK_UT_SELF_BSSID_2,
            .remote_sta_addr = OW_STEER_POLICY_FORCE_KICK_UT_STA_ADDR,
        },
    },
};

static const osw_sta_assoc_links_t g_assoc_links_mlo = {
    .count = 2,
    .links = {
        {
            .local_sta_addr = OW_STEER_POLICY_FORCE_KICK_UT_SELF_BSSID_1,
            .remote_sta_addr = OW_STEER_POLICY_FORCE_KICK_UT_STA_ADDR,
        },
        {
            .local_sta_addr = OW_STEER_POLICY_FORCE_KICK_UT_SELF_BSSID_2,
            .remote_sta_addr = OW_STEER_POLICY_FORCE_KICK_UT_STA_ADDR,
        },
    },
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
    ow_steer_policy_force_kick_set_assoc_links(force_policy, &g_assoc_links_1, OSW_STA_ASSOC_CONNECTED);
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
     * Disonnect STA
     */
    ow_steer_policy_force_kick_set_assoc_links(force_policy, NULL, OSW_STA_ASSOC_DISCONNECTED);
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
    ow_steer_policy_force_kick_set_assoc_links(force_policy, &g_assoc_links_1, OSW_STA_ASSOC_CONNECTED);
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

/*
 * MLO client with two active links: both associated candidates must be
 * hard-blocked when the kick is enforced.
 */
OSW_UT(ow_steer_policy_force_kick_mlo_links_ut)
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
     * Connect MLO STA on both links
     */
    ow_steer_policy_force_kick_set_assoc_links(force_policy, &g_assoc_links_mlo, OSW_STA_ASSOC_CONNECTED);
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
    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_2) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(neighbor_candidate) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}

/*
 * An associated candidate that is already soft-blocked must abort the kick
 * instead of being promoted to hard-blocked. This is the regression test
 * for the fixed soft->hard promotion assumption.
 */
OSW_UT(ow_steer_policy_force_kick_soft_blocked_sta_ut)
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
    ow_steer_policy_force_kick_set_assoc_links(force_policy, &g_assoc_links_1, OSW_STA_ASSOC_CONNECTED);
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
     * Another policy soft-blocks the associated candidate
     */
    ow_steer_candidate_list_clear(candidate_list);
    ow_steer_candidate_set_preference(self_candidate_1, "", OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);

    /*
     * Trigger kick
     */
    ow_steer_policy_force_kick_set_oneshot_config(force_policy, MEMNDUP(&config, sizeof(config)));
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    force_policy->base->ops.recalc_fn(force_policy->base, candidate_list);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_2) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(neighbor_candidate) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Settle the reconf timer armed by the reset
     */
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
}

/*
 * If a link's bssid is missing from the candidate list (e.g. local radio
 * stopped while the disconnection report is delayed), the kick must abort
 * instead of blocking only the candidates that happen to be present.
 */
OSW_UT(ow_steer_policy_force_kick_missing_candidate_ut)
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

    /* NOTE: g_self_bssid_1 is deliberately not in the candidate list */
    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &g_self_bssid_2, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &g_neighbor_bssid, &channel);
    self_candidate_2 = ow_steer_candidate_list_lookup(candidate_list, &g_self_bssid_2);
    neighbor_candidate = ow_steer_candidate_list_lookup(candidate_list, &g_neighbor_bssid);

    /*
     * Connect STA on link 1 (bssid not present in the candidate list)
     */
    ow_steer_policy_force_kick_set_assoc_links(force_policy, &g_assoc_links_1, OSW_STA_ASSOC_CONNECTED);
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    ow_steer_candidate_list_clear(candidate_list);
    force_policy->base->ops.recalc_fn(force_policy->base, candidate_list);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

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
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_2) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(neighbor_candidate) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Settle the reconf timer armed by the reset
     */
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
}

/*
 * If there is no unassociated candidate to steer to (eg. all candidates are
 * associated links), the kick must abort: there is nowhere to force the client
 * to go.
 */
OSW_UT(ow_steer_policy_force_kick_no_alt_candidate_ut)
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

    /*
     * Setup
     */
    osw_ut_time_init();

    force_policy = ow_steer_policy_force_kick_create(&g_sta_addr, &mediator, "");
    OSW_UT_EVAL(force_policy != NULL);
    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 0);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 0);

    /* NOTE: only the associated bssid is in the candidate list, no alt */
    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &g_self_bssid_1, &channel);
    self_candidate_1 = ow_steer_candidate_list_lookup(candidate_list, &g_self_bssid_1);

    /*
     * Connect STA
     */
    ow_steer_policy_force_kick_set_assoc_links(force_policy, &g_assoc_links_1, OSW_STA_ASSOC_CONNECTED);
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
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(self_candidate_1) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /*
     * Settle the reconf timer armed by the reset
     */
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
}

/*
 * A reconnect event during an active kick must reset the kick, like a
 * disconnect does.
 */
OSW_UT(ow_steer_policy_force_kick_reconnect_resets_kick_ut)
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
    ow_steer_policy_force_kick_set_assoc_links(force_policy, &g_assoc_links_1, OSW_STA_ASSOC_CONNECTED);
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
     * STA reconnects during the enforce period -> kick must reset
     */
    ow_steer_policy_force_kick_set_assoc_links(force_policy, &g_assoc_links_1, OSW_STA_ASSOC_RECONNECTED);
    osw_ut_time_advance(0);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);

    ow_steer_candidate_list_clear(candidate_list);
    force_policy->base->ops.recalc_fn(force_policy->base, candidate_list);

    OSW_UT_EVAL(mediator_cnt.schedule_recalc_cnt == 2);
    OSW_UT_EVAL(mediator_cnt.trigger_executor_cnt == 1);
    OSW_UT_EVAL(mediator_cnt.dismiss_executor_cnt == 1);
}
