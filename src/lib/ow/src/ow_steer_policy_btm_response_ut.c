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

OSW_UT(ow_steer_policy_btm_response_ut_with_preferences)
{
    const struct osw_hwaddr bssid_2g = { .octet = { 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa }, };
    const struct osw_hwaddr bssid_5g = { .octet = { 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb }, };
    const struct osw_hwaddr bssid_6g = { .octet = { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, };
    const struct osw_hwaddr addr = { .octet = { 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd }, };
    const struct osw_channel channel_2g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct osw_channel channel_5g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 5745, };
    const struct osw_channel channel_6g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 5995, };
    struct ow_steer_policy_btm_response *btm_response_policy = NULL;
    struct ow_steer_candidate_list *candidate_list = NULL;
    const struct ow_steer_policy_mediator mediator = {0};

    /* initialize */
    osw_ut_time_init();
    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_2g, &channel_2g);
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_5g, &channel_5g);
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_6g, &channel_6g);
    btm_response_policy = ow_steer_policy_btm_response_create("btm_response",
                                                              &addr,
                                                              &mediator);
    OSW_UT_EVAL(btm_response_policy != NULL);

    /* btm response with preferences */
    struct osw_btm_retry_neigh_list retry_neigh_list;
    retry_neigh_list.neigh[0].neigh.bssid = bssid_2g;
    retry_neigh_list.neigh[1].neigh.bssid = bssid_5g;
    retry_neigh_list.neigh[0].preference = 1;
    retry_neigh_list.neigh[1].preference = 2;
    retry_neigh_list.neigh_len = 2;

    ow_steer_candidate_list_clear(candidate_list);
    struct osw_btm_response_observer *btm_response_observer = &btm_response_policy->btm_resp_obs;
    ow_steer_policy_btm_response_cb(btm_response_observer,
                                    DOT11_BTM_RESPONSE_CODE_REJECT_CAND_LIST_PROVIDED,
                                    &retry_neigh_list);

    btm_response_policy->base->ops.recalc_fn(btm_response_policy->base,
                                             candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_2g)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_5g)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_6g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);
}

OSW_UT(ow_steer_policy_btm_response_ut_without_preferences)
{
    const struct osw_hwaddr bssid_2g = { .octet = { 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa }, };
    const struct osw_hwaddr bssid_5g = { .octet = { 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb }, };
    const struct osw_hwaddr bssid_6g = { .octet = { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, };
    const struct osw_hwaddr addr = { .octet = { 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd }, };
    const struct osw_channel channel_2g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 2412, };
    const struct osw_channel channel_5g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 5745, };
    const struct osw_channel channel_6g = { .width = OSW_CHANNEL_20MHZ, .control_freq_mhz = 5995, };
    struct ow_steer_policy_btm_response *btm_response_policy = NULL;
    struct ow_steer_candidate_list *candidate_list = NULL;
    const struct ow_steer_policy_mediator mediator = {0};

    /* initialize */
    osw_ut_time_init();
    candidate_list = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_2g, &channel_2g);
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_5g, &channel_5g);
    ow_steer_candidate_list_bss_set(candidate_list, &bssid_6g, &channel_6g);
    btm_response_policy = ow_steer_policy_btm_response_create("btm_response",
                                                              &addr,
                                                              &mediator);
    OSW_UT_EVAL(btm_response_policy != NULL);

    /* btm response with preferences */
    struct osw_btm_retry_neigh_list retry_neigh_list;
    retry_neigh_list.neigh[0].neigh.bssid = bssid_2g;
    retry_neigh_list.neigh[1].neigh.bssid = bssid_6g;
    retry_neigh_list.neigh_len = 2;

    ow_steer_candidate_list_clear(candidate_list);
    struct osw_btm_response_observer *btm_response_observer = &btm_response_policy->btm_resp_obs;
    ow_steer_policy_btm_response_cb(btm_response_observer,
                                    DOT11_BTM_RESPONSE_CODE_REJECT_CAND_LIST_PROVIDED,
                                    &retry_neigh_list);

    btm_response_policy->base->ops.recalc_fn(btm_response_policy->base,
                                             candidate_list);

    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_2g)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_5g)) == OW_STEER_CANDIDATE_PREFERENCE_OUT_OF_SCOPE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(candidate_list, &bssid_6g)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
}
