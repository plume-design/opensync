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

static void
ow_steer_policy_bss_filter_ut_mediator_sched_stack_recalc(struct ow_steer_policy *policy,
                                                          void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    unsigned int *cnt = priv;
    *cnt = *cnt + 1;
}

OSW_UT(ow_steer_policy_bss_filter_ut_typical_case)
{
    const struct osw_hwaddr bssid_a = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA }, };
    const struct osw_hwaddr bssid_b = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB }, };
    const struct osw_hwaddr bssid_c = { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC }, };
    const struct osw_hwaddr addr = { .octet = { 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD }, };
    const struct osw_channel channel = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    struct ow_steer_policy_bss_filter_config *config = NULL;
    struct ow_steer_policy_bss_filter *filter_policy = NULL;
    struct ow_steer_candidate_list *candidate_list = NULL;
    unsigned int schedule_recalc_cnt = 0;
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_bss_filter_ut_mediator_sched_stack_recalc,
        .priv = &schedule_recalc_cnt,
    };

    /* Setup internal bits */
    osw_ut_time_init();
    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_a, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_b, &channel);
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_c, &channel);

    filter_policy = ow_steer_policy_bss_filter_create(0, "bss_filter", &addr, &mediator);
    OSW_UT_EVAL(filter_policy != NULL);
    OSW_UT_EVAL(schedule_recalc_cnt == 0);

    /* No config -> no action */
    ow_steer_candidate_list_clear(candidate_list);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_a)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_c)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Only BSS B passes through policy */
    ow_steer_candidate_list_clear(candidate_list);

    config = CALLOC(1, sizeof(*config));
    config->included_preference.override = false;
    config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_NONE;
    config->excluded_preference.override = true;
    config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED;
    memcpy(&config->bssid_list[0], &bssid_b, sizeof(config->bssid_list[0]));
    config->bssid_list_len = 1;
    ow_steer_policy_bss_filter_set_config(filter_policy, config);
    OSW_UT_EVAL(schedule_recalc_cnt == 1);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_a)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_c)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);

    /* Allowing BSS B again shiuldn't have any effect */
    ow_steer_candidate_list_clear(candidate_list);

    config = CALLOC(1, sizeof(*config));
    config->included_preference.override = false;
    config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_NONE;
    config->excluded_preference.override = true;
    config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED;
    memcpy(&config->bssid_list[0], &bssid_b, sizeof(config->bssid_list[0]));
    config->bssid_list_len = 1;
    ow_steer_policy_bss_filter_set_config(filter_policy, config);
    OSW_UT_EVAL(schedule_recalc_cnt == 1);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_a)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_c)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);

    /* Allow BSS A and B */
    ow_steer_candidate_list_clear(candidate_list);

    config = CALLOC(1, sizeof(*config));
    config->included_preference.override = false;
    config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_NONE;
    config->excluded_preference.override = true;
    config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED;
    memcpy(&config->bssid_list[0], &bssid_a, sizeof(config->bssid_list[0]));
    memcpy(&config->bssid_list[1], &bssid_b, sizeof(config->bssid_list[1]));
    config->bssid_list_len = 2;
    ow_steer_policy_bss_filter_set_config(filter_policy, config);
    OSW_UT_EVAL(schedule_recalc_cnt == 2);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_a)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_c)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);

    /* Unset config -> no action */
    ow_steer_candidate_list_clear(candidate_list);

    ow_steer_policy_bss_filter_set_config(filter_policy, NULL);
    OSW_UT_EVAL(schedule_recalc_cnt == 3);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_a)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_c)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);

    /* Hard-block C and soft-block others */
    ow_steer_candidate_list_clear(candidate_list);

    config = CALLOC(1, sizeof(*config));
    config->included_preference.override = true;
    config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED;
    config->excluded_preference.override = true;
    config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED;
    memcpy(&config->bssid_list[0], &bssid_c, sizeof(config->bssid_list[0]));
    config->bssid_list_len = 2;
    ow_steer_policy_bss_filter_set_config(filter_policy, config);
    OSW_UT_EVAL(schedule_recalc_cnt == 4);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_a)) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_b)) == OW_STEER_CANDIDATE_PREFERENCE_SOFT_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_c)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
}
