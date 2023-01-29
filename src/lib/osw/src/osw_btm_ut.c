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
#include <osw_drv_common.h>
#include "osw_drv_i.h"

struct osw_btm_ut_dummy_drv {
    unsigned int frame_tx_cnt;
    uint8_t frame_buf [OSW_DRV_FRAME_TX_DESC_BUF_SIZE];
    ssize_t frame_len;
};

struct osw_btm_ut_sta_observer {
    struct osw_btm_sta_observer observer;
    unsigned int tx_complete_cnt;
    unsigned int tx_error_cnt;
};

static struct osw_btm_ut_dummy_drv g_dummy_drv = {
    .frame_tx_cnt = 0,
    .frame_buf = { 0 },
    .frame_len = 0,
};

OSW_UT(osw_btm_ut_build_frame)
{
    /*
     * Frame 1 (this hexdump can be imported in WireShark)
     * 0000   d0 00 3c 00 d4 61 9d 53 75 05 52 b4 f7 f0 1c cd
     * 0010   52 b4 f7 f0 1c cd 00 00 0a 07 01 07 00 00 ff 34
     * 0020   0d 52 b4 f7 f0 1a be 13 00 00 00 7d 9d 09 34 0d
     * 0030   52 b4 f7 f0 1a cd 13 00 00 00 7d 95 09
     */
    static const unsigned char ref_frame1[] = {
        0xd0, 0x00, 0x3c, 0x00, 0xd4, 0x61, 0x9d, 0x53,
        0x75, 0x05, 0x52, 0xb4, 0xf7, 0xf0, 0x1c, 0xcd,
        0x52, 0xb4, 0xf7, 0xf0, 0x1c, 0xcd, 0x00, 0x00,
        0x0a, 0x07, 0x01, 0x07, 0x00, 0x00, 0xff, 0x34,
        0x0d, 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe, 0x13,
        0x00, 0x00, 0x00, 0x7d, 0x9d, 0x09, 0x34, 0x0d,
        0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xcd, 0x13, 0x00,
        0x00, 0x00, 0x7d, 0x92, 0x09,
    };

    const struct osw_hwaddr bssid = { .octet = { 0x52, 0xb4, 0xf7, 0xf0, 0x1c, 0xcd, } };
    const struct osw_hwaddr sta_addr = { .octet = { 0xd4, 0x61, 0x9d, 0x53, 0x75, 0x05, } };
    const uint8_t dialog_token = 1;
    const struct osw_btm_req_params req_params = {
        .neigh = {
            {
                .bssid = { .octet = { 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe } },
                .bssid_info = 0x13,
                .op_class =125,
                .channel = 157,
                .phy_type = 0x9,
            },
            {
                .bssid = { .octet = { 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xcd } },
                .bssid_info = 0x13,
                .op_class = 125,
                .channel = 146,
                .phy_type = 0x9,
            },
        },
        .neigh_len = 2,
        .valid_int = 255,
        .abridged  = true,
        .disassoc_imminent = true,
        .bss_term = false,
    };

    uint8_t frame_buf[OSW_DRV_FRAME_TX_DESC_BUF_SIZE] = { 0 };
    ssize_t frame_len = 0;

    frame_len = osw_btm_build_frame(&req_params, &sta_addr, &bssid, dialog_token, frame_buf, sizeof(frame_buf));
    assert(frame_len == sizeof(ref_frame1));
    assert(memcmp(&frame_buf, &ref_frame1, frame_len) == 0);
}

static bool
osw_btm_ut_mux_frame_tx_schedule_success(const char *phy_name,
                                         const char *vif_name,
                                         struct osw_drv_frame_tx_desc *desc)
{
    g_dummy_drv.frame_tx_cnt++;
    memcpy(&g_dummy_drv.frame_buf, osw_drv_frame_tx_desc_get_frame(desc), osw_drv_frame_tx_desc_get_frame_len(desc));
    g_dummy_drv.frame_len = osw_drv_frame_tx_desc_get_frame_len(desc);

    osw_btm_drv_frame_tx_result_cb(desc, OSW_FRAME_TX_RESULT_SUBMITTED, desc->caller_priv);

    return true;
}

static bool
osw_btm_ut_mux_frame_tx_schedule_error(const char *phy_name,
                                       const char *vif_name,
                                       struct osw_drv_frame_tx_desc *desc)
{
    g_dummy_drv.frame_tx_cnt++;
    memcpy(&g_dummy_drv.frame_buf, osw_drv_frame_tx_desc_get_frame(desc), osw_drv_frame_tx_desc_get_frame_len(desc));
    g_dummy_drv.frame_len = osw_drv_frame_tx_desc_get_frame_len(desc);

    osw_btm_drv_frame_tx_result_cb(desc, OSW_FRAME_TX_RESULT_FAILED, desc->caller_priv);

    return true;
}

static void
osw_btm_req_tx_complete_cb(struct osw_btm_sta_observer *observer)
{
    struct osw_btm_ut_sta_observer *sta_observer = (struct osw_btm_ut_sta_observer*) container_of(observer, struct osw_btm_ut_sta_observer, observer);
    sta_observer->tx_complete_cnt++;
}

static void
osw_btm_req_tx_error_cb(struct osw_btm_sta_observer *observer)
{
    struct osw_btm_ut_sta_observer *sta_observer = (struct osw_btm_ut_sta_observer*) container_of(observer, struct osw_btm_ut_sta_observer, observer);
    sta_observer->tx_error_cnt++;
}

OSW_UT(osw_btm_ut_send_single_btm)
{
    const struct osw_drv_dot11_frame *frame = NULL;
    const struct osw_hwaddr sta_addr = { .octet = { 0xd4, 0x61, 0x9d, 0x53, 0x75, 0x05, } };
    const struct osw_btm_req_params req_params = {
        .neigh = {
            {
                .bssid = { .octet = { 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe } },
                .bssid_info = 0x13,
                .op_class =125,
                .channel = 157,
                .phy_type = 0x9,
            },
            {
                .bssid = { .octet = { 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xcd } },
                .bssid_info = 0x13,
                .op_class = 125,
                .channel = 146,
                .phy_type = 0x9,
            },
        },
        .neigh_len = 2,
        .valid_int = 255,
        .abridged  = true,
        .disassoc_imminent = true,
        .bss_term = false,
    };

    struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = { .octet = { 0xac, 0x4c, 0x56, 0x03, 0xd2, 0x1b }, },
    };
    struct osw_state_phy_info phy = {
        .phy_name = "phy0",
    };
    struct osw_state_vif_info vif = {
        .vif_name = "vif0",
        .phy = &phy,
        .drv_state = &drv_vif_state,
    };
    struct osw_state_sta_info sta_info = {
        .mac_addr = &sta_addr,
        .vif = &vif,
    };

    struct osw_btm_desc *desc = NULL;
    struct osw_btm_ut_sta_observer sta_observer = {
        .observer = {
            .req_tx_complete_fn = osw_btm_req_tx_complete_cb,
            .req_tx_error_fn = osw_btm_req_tx_error_cb,
        },
        .tx_complete_cnt = 0,
        .tx_error_cnt = 0,
    };
    bool result;

    osw_time_set_mono_clk(0);
    osw_time_set_wall_clk(0);

    desc = osw_btm_get_desc_internal(&sta_addr, &sta_observer.observer, osw_btm_ut_mux_frame_tx_schedule_success);
    assert(desc != NULL);

    /* Cannot queue BTM Request for disconnected STA */
    result = osw_btm_desc_set_req_params(desc, &req_params);
    osw_ut_time_advance(0);

    assert(result == false);
    assert(sta_observer.tx_complete_cnt == 0);
    assert(sta_observer.tx_error_cnt == 0);

    /* Schedule BTM Request for connected STA */
    osw_btm_sta_connected_cb(&desc->sta->observer, &sta_info);
    result = osw_btm_desc_set_req_params(desc, &req_params);
    osw_ut_time_advance(0);

    assert(result == true);
    assert(sta_observer.tx_complete_cnt == 1);
    assert(sta_observer.tx_error_cnt == 0);
    assert(g_dummy_drv.frame_tx_cnt == 1);
    frame = (const struct osw_drv_dot11_frame*) &g_dummy_drv.frame_buf;
    assert(frame->u.action.u.bss_tm_req.dialog_token == 0);

    /* Schedule BTM Request again for connected STA */
    result = osw_btm_desc_set_req_params(desc, &req_params);
    osw_ut_time_advance(0);

    assert(result == true);
    assert(sta_observer.tx_complete_cnt == 2);
    assert(sta_observer.tx_error_cnt == 0);
    assert(g_dummy_drv.frame_tx_cnt == 2);
    frame = (const struct osw_drv_dot11_frame*) &g_dummy_drv.frame_buf;
    assert(frame->u.action.u.bss_tm_req.dialog_token == 1);

    /* Recreate desc, but with failing drv tx */
    osw_btm_desc_free(desc);

    desc = osw_btm_get_desc_internal(&sta_addr, &sta_observer.observer, osw_btm_ut_mux_frame_tx_schedule_error);
    osw_btm_sta_connected_cb(&desc->sta->observer, &sta_info);

    /* Schedule BTM Request again for connected STA */
    result = osw_btm_desc_set_req_params(desc, &req_params);
    osw_ut_time_advance(0);

    assert(result == true);
    assert(sta_observer.tx_complete_cnt == 2);
    assert(sta_observer.tx_error_cnt == 1);
    assert(g_dummy_drv.frame_tx_cnt == 3);
    frame = (const struct osw_drv_dot11_frame*) &g_dummy_drv.frame_buf;
    assert(frame->u.action.u.bss_tm_req.dialog_token == 0);
}
