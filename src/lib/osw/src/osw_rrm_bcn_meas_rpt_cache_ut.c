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

OSW_UT(osw_rrm_bcn_meas_rpt_cache_lifetime)
{
    const struct osw_hwaddr ref_sta_addr = { .octet = { 0xd4, 0x61, 0x9d, 0x53, 0x75, 0x05 } };
    const struct osw_hwaddr ref_bssid0 = { .octet = { 0x14, 0x21, 0x4d, 0x57, 0xc4, 0x12 } };
    const struct osw_hwaddr ref_bssid1 = { .octet = { 0x14, 0x78, 0x4d, 0x57, 0xc4, 0x90 } };

    osw_ut_time_init();

    struct osw_rrm_bcn_meas_rpt_cache cache;
    osw_rrm_bcn_meas_rpt_cache_init(&cache);

    /* There should be no reports for STA 0 */
    const struct osw_rrm_bcn_meas_rpt *rpt = NULL;
    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid0);
    OSW_UT_EVAL(rpt == NULL);
    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid1);
    OSW_UT_EVAL(rpt == NULL);

    /* Add first report for STA 0 + BSSID 0 */
    struct osw_drv_dot11_frame_header frame_header_bssid0_rpt;
    memset(&frame_header_bssid0_rpt, 0, sizeof(frame_header_bssid0_rpt));
    memcpy(&frame_header_bssid0_rpt.sa, &ref_sta_addr.octet, sizeof(frame_header_bssid0_rpt.sa));
    struct osw_drv_dot11_frame_action_rrm_meas_rep rrm_meas_rep_bssid0_rpt;
    memset(&rrm_meas_rep_bssid0_rpt, 0, sizeof(rrm_meas_rep_bssid0_rpt));
    struct osw_drv_dot11_meas_rep_ie_fixed meas_rpt_ie_fixed_bssid0_rpt;
    memset(&meas_rpt_ie_fixed_bssid0_rpt, 0, sizeof(meas_rpt_ie_fixed_bssid0_rpt));
    struct osw_drv_dot11_meas_rpt_ie_beacon meas_rpt_ie_beacon_bssid0_rpt;
    memset(&meas_rpt_ie_beacon_bssid0_rpt, 0, sizeof(meas_rpt_ie_beacon_bssid0_rpt));
    meas_rpt_ie_beacon_bssid0_rpt.op_class = 128;
    meas_rpt_ie_beacon_bssid0_rpt.channel = 44;
    meas_rpt_ie_beacon_bssid0_rpt.rcpi = 162;
    memcpy(&meas_rpt_ie_beacon_bssid0_rpt.bssid, &ref_bssid0.octet, sizeof(meas_rpt_ie_beacon_bssid0_rpt.bssid));

    cache.rrm_rpt_observer.bcn_meas_fn(&cache.rrm_rpt_observer,
                                       &frame_header_bssid0_rpt,
                                       &rrm_meas_rep_bssid0_rpt,
                                       &meas_rpt_ie_fixed_bssid0_rpt,
                                       &meas_rpt_ie_beacon_bssid0_rpt);

    /* Only STA 0 + BSSID 0 report should be available */
    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid0);
    OSW_UT_EVAL(rpt != NULL);
    OSW_UT_EVAL(rpt->op_class == meas_rpt_ie_beacon_bssid0_rpt.op_class);
    OSW_UT_EVAL(rpt->channel == meas_rpt_ie_beacon_bssid0_rpt.channel);
    OSW_UT_EVAL(rpt->rcpi == meas_rpt_ie_beacon_bssid0_rpt.rcpi);

    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid1);
    OSW_UT_EVAL(rpt == NULL);

    /* Add first report for STA 0 + BSSID 0 */
    const uint64_t step_duration_sec = 5;
    OSW_UT_EVAL(step_duration_sec < OSW_RRM_BCN_MEAS_RPT_EXPIRE_PERIOD_SEC);
    osw_ut_time_advance(OSW_TIME_SEC(step_duration_sec));

    struct osw_drv_dot11_frame_header frame_header_bssid1_rpt;
    memset(&frame_header_bssid1_rpt, 0, sizeof(frame_header_bssid1_rpt));
    memcpy(&frame_header_bssid1_rpt.sa, &ref_sta_addr.octet, sizeof(frame_header_bssid1_rpt.sa));
    struct osw_drv_dot11_frame_action_rrm_meas_rep rrm_meas_rep_bssid1_rpt;
    memset(&rrm_meas_rep_bssid1_rpt, 0, sizeof(rrm_meas_rep_bssid1_rpt));
    struct osw_drv_dot11_meas_rep_ie_fixed meas_rpt_ie_fixed_bssid1_rpt;
    memset(&meas_rpt_ie_fixed_bssid1_rpt, 0, sizeof(meas_rpt_ie_fixed_bssid1_rpt));
    struct osw_drv_dot11_meas_rpt_ie_beacon meas_rpt_ie_beacon_bssid1_rpt;
    memset(&meas_rpt_ie_beacon_bssid1_rpt, 0, sizeof(meas_rpt_ie_beacon_bssid1_rpt));
    meas_rpt_ie_beacon_bssid1_rpt.op_class = 81;
    meas_rpt_ie_beacon_bssid1_rpt.channel = 1;
    meas_rpt_ie_beacon_bssid1_rpt.rcpi = 150;
    memcpy(&meas_rpt_ie_beacon_bssid1_rpt.bssid, &ref_bssid1.octet, sizeof(meas_rpt_ie_beacon_bssid1_rpt.bssid));

    cache.rrm_rpt_observer.bcn_meas_fn(&cache.rrm_rpt_observer,
                                       &frame_header_bssid1_rpt,
                                       &rrm_meas_rep_bssid1_rpt,
                                       &meas_rpt_ie_fixed_bssid1_rpt,
                                       &meas_rpt_ie_beacon_bssid1_rpt);

    /* Both BSSID 0 and 1 should be available for STA 0 */
    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid0);
    OSW_UT_EVAL(rpt != NULL);
    OSW_UT_EVAL(rpt->op_class == meas_rpt_ie_beacon_bssid0_rpt.op_class);
    OSW_UT_EVAL(rpt->channel == meas_rpt_ie_beacon_bssid0_rpt.channel);
    OSW_UT_EVAL(rpt->rcpi == meas_rpt_ie_beacon_bssid0_rpt.rcpi);

    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid1);
    OSW_UT_EVAL(rpt != NULL);
    OSW_UT_EVAL(rpt->op_class == meas_rpt_ie_beacon_bssid1_rpt.op_class);
    OSW_UT_EVAL(rpt->channel == meas_rpt_ie_beacon_bssid1_rpt.channel);
    OSW_UT_EVAL(rpt->rcpi == meas_rpt_ie_beacon_bssid1_rpt.rcpi);

    /* Report for BSSID 0 should expire */
    osw_ut_time_advance(OSW_TIME_SEC(OSW_RRM_BCN_MEAS_RPT_EXPIRE_PERIOD_SEC - step_duration_sec));

    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid0);
    OSW_UT_EVAL(rpt == NULL);

    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid1);
    OSW_UT_EVAL(rpt != NULL);
    OSW_UT_EVAL(rpt->op_class == meas_rpt_ie_beacon_bssid1_rpt.op_class);
    OSW_UT_EVAL(rpt->channel == meas_rpt_ie_beacon_bssid1_rpt.channel);
    OSW_UT_EVAL(rpt->rcpi == meas_rpt_ie_beacon_bssid1_rpt.rcpi);

    /* Update BSSID 1 report */
    struct osw_drv_dot11_frame_header frame_header_bssid1_new_rpt;
    memset(&frame_header_bssid1_new_rpt, 0, sizeof(frame_header_bssid1_new_rpt));
    memcpy(&frame_header_bssid1_new_rpt.sa, &ref_sta_addr.octet, sizeof(frame_header_bssid1_new_rpt.sa));
    struct osw_drv_dot11_frame_action_rrm_meas_rep rrm_meas_rep_bssid1_new_rpt;
    memset(&rrm_meas_rep_bssid1_new_rpt, 0, sizeof(rrm_meas_rep_bssid1_new_rpt));
    struct osw_drv_dot11_meas_rep_ie_fixed meas_rpt_ie_fixed_bssid1_new_rpt;
    memset(&meas_rpt_ie_fixed_bssid1_new_rpt, 0, sizeof(meas_rpt_ie_fixed_bssid1_new_rpt));
    struct osw_drv_dot11_meas_rpt_ie_beacon meas_rpt_ie_beacon_bssid1_new_rpt;
    memset(&meas_rpt_ie_beacon_bssid1_new_rpt, 0, sizeof(meas_rpt_ie_beacon_bssid1_new_rpt));
    meas_rpt_ie_beacon_bssid1_new_rpt.op_class = 81;
    meas_rpt_ie_beacon_bssid1_new_rpt.channel = 11;
    meas_rpt_ie_beacon_bssid1_new_rpt.rcpi = 168;
    memcpy(&meas_rpt_ie_beacon_bssid1_new_rpt.bssid, &ref_bssid1.octet, sizeof(meas_rpt_ie_beacon_bssid1_new_rpt.bssid));

    cache.rrm_rpt_observer.bcn_meas_fn(&cache.rrm_rpt_observer,
                                       &frame_header_bssid1_new_rpt,
                                       &rrm_meas_rep_bssid1_new_rpt,
                                       &meas_rpt_ie_fixed_bssid1_new_rpt,
                                       &meas_rpt_ie_beacon_bssid1_new_rpt);

    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid0);
    OSW_UT_EVAL(rpt == NULL);

    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid1);
    OSW_UT_EVAL(rpt != NULL);
    OSW_UT_EVAL(rpt->op_class == meas_rpt_ie_beacon_bssid1_new_rpt.op_class);
    OSW_UT_EVAL(rpt->channel == meas_rpt_ie_beacon_bssid1_new_rpt.channel);
    OSW_UT_EVAL(rpt->rcpi == meas_rpt_ie_beacon_bssid1_new_rpt.rcpi);

    /* Check cache internals */
    OSW_UT_EVAL(ds_tree_len(&cache.sta_tree) == 1);
    struct osw_rrm_bcn_meas_rpt_sta *sta = ds_tree_find(&cache.sta_tree, &ref_sta_addr);
    OSW_UT_EVAL(sta != NULL);

    OSW_UT_EVAL(ds_tree_len(&sta->neigh_tree) == 2);

    /* Run GC period manually */
    sta = ds_tree_find(&cache.sta_tree, &ref_sta_addr);
    OSW_UT_EVAL(sta != NULL);

    osw_rrm_bcn_meas_rpt_sta_gc_timer_cb(&sta->gc_timer);

    OSW_UT_EVAL(ds_tree_len(&cache.sta_tree) == 1);
    sta = ds_tree_find(&cache.sta_tree, &ref_sta_addr);
    OSW_UT_EVAL(sta != NULL);

    OSW_UT_EVAL(ds_tree_len(&sta->neigh_tree) == 1); /* BSSID Report is gone */
    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid0);
    OSW_UT_EVAL(rpt == NULL);

    rpt = osw_rrm_bcn_meas_rpt_cache_lookup(&cache, &ref_sta_addr, &ref_bssid1);
    OSW_UT_EVAL(rpt != NULL);

    /* Run timer until GC timer fires (BSSID 1 Report shiuld be removed) */
    osw_ut_time_advance(OSW_TIME_SEC(OSW_RRM_BCN_MEAS_RPT_GC_PERIOD_SEC));

    sta = ds_tree_find(&cache.sta_tree, &ref_sta_addr);
    OSW_UT_EVAL(sta == NULL);
}
