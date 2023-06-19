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
ow_steer_policy_band_filter_ut_mediator_sched_stack_recalc(struct ow_steer_policy *policy,
                                                          void *priv)
{
    OSW_UT_EVAL(priv != NULL);
    unsigned int *cnt = priv;
    *cnt = *cnt + 1;
}

OSW_UT(ow_steer_policy_band_filter_ut_typical_case)
{
    const struct osw_hwaddr bssid_2g = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA }, };
    const struct osw_hwaddr bssid_5g = { .octet = { 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB }, };
    const struct osw_hwaddr bssid_6g = { .octet = { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC }, };
    const struct osw_hwaddr addr = { .octet = { 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD }, };
    const struct osw_channel channel_2g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct osw_channel channel_5g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 5745, };
    const struct osw_channel channel_6g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 5995, };
    struct ow_steer_policy_band_filter_config *config = NULL;
    struct ow_steer_policy_band_filter *filter_policy = NULL;
    struct ow_steer_candidate_list *candidate_list = NULL;
    unsigned int schedule_recalc_cnt = 0;
    const struct ow_steer_policy_mediator mediator = {
        .sched_recalc_stack_fn = ow_steer_policy_band_filter_ut_mediator_sched_stack_recalc,
        .priv = &schedule_recalc_cnt,
    };

    /* Setup internal bits */
    osw_ut_time_init();
    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_2g, &channel_2g);
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_5g, &channel_5g);
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_6g, &channel_6g);

    filter_policy = ow_steer_policy_band_filter_create("band_filter", &addr, &mediator);
    OSW_UT_EVAL(filter_policy != NULL);
    OSW_UT_EVAL(schedule_recalc_cnt == 0);

    /* Mark all bands by as out-of-scope */
    ow_steer_candidate_list_clear(candidate_list);

    config = CALLOC(1, sizeof(*config));
    config->included_preference.override = false;
    config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_NONE;
    config->excluded_preference.override = true;
    config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE;
    ow_steer_policy_band_filter_set_config(filter_policy, config);
    OSW_UT_EVAL(schedule_recalc_cnt == 1);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_2g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_5g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_6g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);

    /* Only 5 GHz passes through policy */
    ow_steer_candidate_list_clear(candidate_list);

    config = CALLOC(1, sizeof(*config));
    config->included_preference.override = false;
    config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_NONE;
    config->excluded_preference.override = true;
    config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE;
    config->band_list[0] = OSW_BAND_5GHZ;
    config->band_list_len = 1;
    ow_steer_policy_band_filter_set_config(filter_policy, config);
    OSW_UT_EVAL(schedule_recalc_cnt == 2);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_2g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_5g)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_6g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);

    /* Now 5 GHz are marked as available */
    ow_steer_candidate_list_clear(candidate_list);

    config = CALLOC(1, sizeof(*config));
    config->included_preference.override = true;
    config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE;
    config->excluded_preference.override = true;
    config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE;
    config->band_list[0] = OSW_BAND_5GHZ;
    config->band_list_len = 1;
    ow_steer_policy_band_filter_set_config(filter_policy, config);
    OSW_UT_EVAL(schedule_recalc_cnt == 3);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_2g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_5g)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_6g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);

    /* Add 6 GHz to the list */
    ow_steer_candidate_list_clear(candidate_list);

    config = CALLOC(1, sizeof(*config));
    config->included_preference.override = true;
    config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE;
    config->excluded_preference.override = true;
    config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE;
    config->band_list[0] = OSW_BAND_5GHZ;
    config->band_list[1] = OSW_BAND_6GHZ;
    config->band_list_len = 2;
    ow_steer_policy_band_filter_set_config(filter_policy, config);
    OSW_UT_EVAL(schedule_recalc_cnt == 4);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_2g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_5g)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_6g)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

    /* Set the same config (expect no sched recalc) */
    ow_steer_candidate_list_clear(candidate_list);

    config = CALLOC(1, sizeof(*config));
    config->included_preference.override = true;
    config->included_preference.value = OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE;
    config->excluded_preference.override = true;
    config->excluded_preference.value = OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE;
    config->band_list[0] = OSW_BAND_5GHZ;
    config->band_list[1] = OSW_BAND_6GHZ;
    config->band_list_len = 2;
    ow_steer_policy_band_filter_set_config(filter_policy, config);
    OSW_UT_EVAL(schedule_recalc_cnt == 4);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_2g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_5g)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_6g)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);

    /* All bands are filtered out by policy */
    ow_steer_candidate_list_clear(candidate_list);

    ow_steer_policy_band_filter_set_config(filter_policy, NULL);
    OSW_UT_EVAL(schedule_recalc_cnt == 5);

    filter_policy->base->ops.recalc_fn(filter_policy->base, candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_2g)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_5g)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_6g)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}
