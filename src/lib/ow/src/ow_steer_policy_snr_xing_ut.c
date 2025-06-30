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

OSW_UT(ow_steer_policy_snr_xing_ut_levels)
{
    struct ow_steer_policy_mediator mediator;
    MEMZERO(mediator);
    const uint8_t hyst = 5;
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 1, 1 } };
    const struct osw_hwaddr ap2 = { .octet = { 0, 0, 0, 0, 1, 2 } };
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr sta2 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    struct ow_steer_policy_snr_xing_config lwm = {
        .mode = OW_STEER_POLICY_SNR_XING_MODE_LWM,
        .snr = 30,
        .snr_hysteresis = hyst,
    };
    struct ow_steer_policy_snr_xing_config hwm = {
        .mode = OW_STEER_POLICY_SNR_XING_MODE_HWM,
        .snr = 30,
        .snr_hysteresis = hyst,
    };
    struct ow_steer_policy_snr_xing_config bowm = {
        .mode = OW_STEER_POLICY_SNR_XING_MODE_BOTTOM_LWM,
        .snr = 10,
        .snr_hysteresis = 0,
    };
    const uint8_t snr_dead = 9;
    const uint8_t snr_alive = 11;
    const uint8_t snr_above = 31;
    const uint8_t snr_below = 29;
    const uint8_t snr_above_hyst = snr_above + hyst;
    const uint8_t snr_below_hyst = snr_below - hyst;
    struct ow_steer_policy_snr_xing *xing = ow_steer_policy_snr_xing_create("", &sta1, &mediator, "");
    struct ow_steer_policy_snr_xing_link *link1 = ow_steer_policy_snr_xing_set_link(xing, &ap1, &sta1);

    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&lwm, sizeof(lwm)));

    /* Check: initial state makes sense */
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_NONE);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    /* Check: For LWM, the BELOW can be entered only from
     * ABOVE. It cannot be entered from NONE. A symmetrical
     * inverse is true for HWM.
     */
    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_NONE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_NONE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above_hyst);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above_hyst);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);

    /* Check: Levels change as expected and re-configuration
     * transitions smoothly.
     */
    ow_steer_policy_snr_xing_set_level(xing, OW_STEER_POLICY_SNR_XING_LEVEL_NONE);
    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_NONE);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above_hyst);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_set_idle(xing, true);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    MEMZERO(lwm.bssids);
    osw_hwaddr_list_append(&lwm.bssids, &ap1);
    osw_hwaddr_list_append(&lwm.bssids, &ap2);
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&lwm, sizeof(lwm)));
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_set_idle(xing, true);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == true);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == true);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above_hyst);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);
    OSW_UT_EVAL(xing->latched == false);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);

    MEMZERO(hwm.bssids);
    osw_hwaddr_list_append(&hwm.bssids, &ap1);
    osw_hwaddr_list_append(&hwm.bssids, &ap2);
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&hwm, sizeof(hwm)));
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_set_idle(xing, true);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == true);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == true);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below_hyst);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    MEMZERO(bowm.bssids);
    osw_hwaddr_list_append(&bowm.bssids, &ap1);
    osw_hwaddr_list_append(&bowm.bssids, &ap2);
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&bowm, sizeof(bowm)));
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_dead);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_set_idle(xing, true);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == true);

    ow_steer_policy_snr_xing_set_btm(xing, true);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_set_btm(xing, false);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_alive);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_dead);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == true);

    ow_steer_policy_snr_xing_link_snr_set(link1, NULL);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == true);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_alive);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    ow_steer_policy_snr_xing_link_snr_set(link1, NULL);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);
    OSW_UT_EVAL(osw_timer_is_armed(&xing->enforce) == false);

    /* Check: MLO (>1 link) level evaluation is sound */
    struct ow_steer_policy_snr_xing_link *link2 = ow_steer_policy_snr_xing_set_link(xing, &ap2, &sta2);

    MEMZERO(lwm.bssids);
    osw_hwaddr_list_append(&lwm.bssids, &ap1);
    osw_hwaddr_list_append(&lwm.bssids, &ap2);
    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&lwm, sizeof(lwm)));
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);
    ow_steer_policy_snr_xing_set_level(xing, OW_STEER_POLICY_SNR_XING_LEVEL_NONE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_NONE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above_hyst);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_NONE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above_hyst);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_above_hyst);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above_hyst);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above_hyst);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_below);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_below);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_ABOVE);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_below);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_above_hyst);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_above);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_below);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_below);
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_BELOW);

    ow_steer_policy_snr_xing_link_snr_set(link1, &snr_above_hyst);
    ow_steer_policy_snr_xing_link_snr_set(link2, &snr_above_hyst);

    ow_steer_policy_snr_xing_set_link(xing, &ap1, osw_hwaddr_zero());
    ow_steer_policy_snr_xing_set_link(xing, &ap2, osw_hwaddr_zero());
    OSW_UT_EVAL(xing->level == OW_STEER_POLICY_SNR_XING_LEVEL_NONE);
}

OSW_UT(ow_steer_policy_snr_xing_ut_candidates)
{
    struct ow_steer_policy_mediator mediator;
    MEMZERO(mediator);
    struct osw_channel ch;
    MEMZERO(ch);
    const struct osw_hwaddr sta1 = { .octet = { 0, 0, 0, 0, 0, 1 } };
    const struct osw_hwaddr sta2 = { .octet = { 0, 0, 0, 0, 0, 2 } };
    const struct osw_hwaddr ap1 = { .octet = { 0, 0, 0, 0, 1, 1 } };
    const struct osw_hwaddr ap2 = { .octet = { 0, 0, 0, 0, 1, 2 } };
    const struct osw_hwaddr ap3 = { .octet = { 0, 0, 0, 0, 1, 3 } };
    const struct osw_hwaddr ap4 = { .octet = { 0, 0, 0, 0, 1, 4 } };
    const struct osw_hwaddr ap5 = { .octet = { 0, 0, 0, 0, 1, 5 } };
    const struct osw_hwaddr bssids[] = { ap1, ap2 };
    struct ow_steer_policy_snr_xing_config lwm = {
        .mode = OW_STEER_POLICY_SNR_XING_MODE_LWM,
        .bssids = {
            .list = MEMNDUP(bssids, sizeof(bssids)),
            .count = ARRAY_SIZE(bssids),
        },
    };
    struct ow_steer_policy_snr_xing *xing = ow_steer_policy_snr_xing_create("", &sta1, &mediator, "");

    ow_steer_policy_snr_xing_set_config(xing, MEMNDUP(&lwm, sizeof(lwm)));

    struct ow_steer_candidate_list *tmpl = ow_steer_candidate_list_new();
    ow_steer_candidate_list_bss_mld_set(tmpl, &ap1, &ch, NULL);
    ow_steer_candidate_list_bss_mld_set(tmpl, &ap2, &ch, NULL);
    ow_steer_candidate_list_bss_mld_set(tmpl, &ap3, &ch, NULL);
    ow_steer_candidate_list_bss_mld_set(tmpl, &ap4, &ch, NULL);
    ow_steer_candidate_list_bss_mld_set(tmpl, &ap5, &ch, NULL);

    struct ow_steer_candidate_list *list;

    list = ow_steer_candidate_list_copy(tmpl);
    ow_steer_policy_snr_xing_recalc_cb(xing->base, list);
    OSW_UT_EVAL(ow_steer_candidate_list_cmp(tmpl, list) == true);
    ow_steer_candidate_list_free(list);

    ow_steer_policy_snr_xing_enforce_try_start(xing);

    list = ow_steer_candidate_list_copy(tmpl);
    ow_steer_policy_snr_xing_set_link(xing, &ap1, &sta1);
    ow_steer_policy_snr_xing_recalc_cb(xing->base, list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap1)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap2)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap3)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap4)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap5)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    ow_steer_candidate_list_free(list);

    list = ow_steer_candidate_list_copy(tmpl);
    ow_steer_policy_snr_xing_set_link(xing, &ap1, &sta1);
    ow_steer_candidate_set_preference(ow_steer_candidate_list_lookup(list, &ap2), "", OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    ow_steer_candidate_set_preference(ow_steer_candidate_list_lookup(list, &ap3), "", OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    ow_steer_candidate_set_preference(ow_steer_candidate_list_lookup(list, &ap4), "", OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    ow_steer_candidate_set_preference(ow_steer_candidate_list_lookup(list, &ap5), "", OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    ow_steer_policy_snr_xing_recalc_cb(xing->base, list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap1)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap2)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap3)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap4)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap5)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    ow_steer_candidate_list_free(list);

    list = ow_steer_candidate_list_copy(tmpl);
    ow_steer_policy_snr_xing_set_link(xing, &ap1, &sta1);
    ow_steer_candidate_set_preference(ow_steer_candidate_list_lookup(list, &ap1), "", OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    ow_steer_policy_snr_xing_recalc_cb(xing->base, list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap1)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap2)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap3)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap4)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap5)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    ow_steer_candidate_list_free(list);

    list = ow_steer_candidate_list_copy(tmpl);
    ow_steer_policy_snr_xing_set_link(xing, &ap1, &sta1);
    ow_steer_policy_snr_xing_set_link(xing, &ap2, &sta2);
    ow_steer_candidate_set_preference(ow_steer_candidate_list_lookup(list, &ap1), "", OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    ow_steer_policy_snr_xing_recalc_cb(xing->base, list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap1)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap2)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap3)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap4)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap5)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    ow_steer_candidate_list_free(list);

    list = ow_steer_candidate_list_copy(tmpl);
    ow_steer_policy_snr_xing_set_link(xing, &ap1, &sta1);
    ow_steer_policy_snr_xing_set_link(xing, &ap2, &sta2);
    ow_steer_policy_snr_xing_recalc_cb(xing->base, list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap1)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap2)) == OW_STEER_CANDIDATE_PREFERENCE_HARD_BLOCKED);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap3)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap4)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap5)) == OW_STEER_CANDIDATE_PREFERENCE_AVAILABLE);
    ow_steer_candidate_list_free(list);

    list = ow_steer_candidate_list_copy(tmpl);
    ow_steer_policy_snr_xing_set_link(xing, &ap1, osw_hwaddr_zero());
    ow_steer_policy_snr_xing_set_link(xing, &ap2, osw_hwaddr_zero());
    ow_steer_policy_snr_xing_set_link(xing, &ap3, &sta1);
    ow_steer_policy_snr_xing_recalc_cb(xing->base, list);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap1)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap2)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap3)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap4)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    OSW_UT_EVAL(ow_steer_candidate_get_preference(ow_steer_candidate_list_lookup(list, &ap5)) == OW_STEER_CANDIDATE_PREFERENCE_NONE);
    ow_steer_candidate_list_free(list);
}
