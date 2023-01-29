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

#include "osw_ut.h"
#include "osw_drv_common.h"
#include "osw_drv_i.h"
#include "osw_state_i.h"

struct osw_rrm_meas_ut_dummy_drv {
    unsigned int frame_tx_cnt;
    uint8_t frame_buf [OSW_DRV_FRAME_TX_DESC_BUF_SIZE];
    ssize_t frame_len;
};

struct osw_rrm_meas_ut_sta_observer {
    const struct osw_rrm_meas_sta_observer observer;
    unsigned int tx_complete_cnt;
    unsigned int tx_error_cnt;
};

/* FIXME pass this as a cookie instead of having global variable */
static struct osw_rrm_meas_ut_dummy_drv g_dummy_drv = {
    .frame_tx_cnt = 0,
    .frame_buf = { 0 },
    .frame_len = 0,
};

OSW_UT(osw_rrm_meas_ut_build_frame)
{
    static const unsigned char ref_frame_1[] = {
        0xd0, 0x00, 0x3c, 0x00, 0xd4, 0x61, 0x9d, 0x53,
        0x75, 0x05, 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe,
        0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe, 0x00, 0x00,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x26, 0x19, 0x00,
        0x00, 0x05, 0x51, 0x00, 0x00, 0x00, 0x64, 0x00,
        0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
        0x00, 0x01, 0x02, 0x00, 0x00, 0x02, 0x01, 0x00,
    };

    static const unsigned char ref_frame_2[] = {
        0xd0, 0x00, 0x3c, 0x00, 0xd4, 0x61, 0x9d, 0x53,
        0x75, 0x05, 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe,
        0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe, 0x00, 0x00,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x26, 0x19, 0x00,
        0x00, 0x05, 0x51, 0x00, 0x00, 0x00, 0x64, 0x00,
        0x01, 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x00,
        0x00, 0x01, 0x02, 0x00, 0x00, 0x02, 0x01, 0x00,
    };
    static const unsigned char ref_frame_3[] = {
        0xd0, 0x00, 0x3c, 0x00, 0xd4, 0x61, 0x9d, 0x53,
        0x75, 0x05, 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe,
        0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe, 0x00, 0x00,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x26, 0x25, 0x00,
        0x00, 0x05, 0x51, 0x00, 0x00, 0x00, 0x64, 0x00,
        0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
        0x0c, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65,
        0x2d, 0x73, 0x73, 0x69, 0x64, 0x01, 0x02, 0x00,
        0x00, 0x02, 0x01, 0x00,
    };

    uint8_t frame_buf[OSW_DRV_FRAME_TX_DESC_BUF_SIZE] = { 0 };
    ssize_t frame_len = 0;

    const struct osw_hwaddr bssid = { .octet = { 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe } };
    const struct osw_hwaddr sta_addr = { .octet = { 0xd4, 0x61, 0x9d, 0x53, 0x75, 0x05 } };
    const uint8_t dialog_token = 0;

    const struct osw_rrm_meas_req_params req_params_1 = {
        .op_class = 81,
        .channel = 0, // all supported channels on provided op_class
        .bssid    = { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } }, //broadcast
        .ssid     = { .buf = "", .len = strlen("") }, //any ssid
    };

    const struct osw_rrm_meas_req_params req_params_2 = {
        .op_class = 81,
        .channel  = 0,
        .bssid    = { .octet = { 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe } },
        .ssid     = { .buf = "", .len = strlen("") },
    };

    const struct osw_rrm_meas_req_params req_params_3 = {
        .op_class = 81,
        .channel  = 0,
        .bssid    = { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
        .ssid     = { .buf = "example-ssid", .len = strlen("example-ssid") },
    };

    frame_len = osw_rrm_meas_build_frame(&req_params_1, &sta_addr, &bssid, dialog_token, frame_buf, sizeof(frame_buf));
    assert(frame_len == sizeof(ref_frame_1));
    assert(memcmp(&frame_buf, &ref_frame_1, frame_len) == 0);

    frame_len = osw_rrm_meas_build_frame(&req_params_2, &sta_addr, &bssid, dialog_token, frame_buf, sizeof(frame_buf));
    assert(frame_len == sizeof(ref_frame_2));
    assert(memcmp(&frame_buf, &ref_frame_2, frame_len) == 0);

    frame_len = osw_rrm_meas_build_frame(&req_params_3, &sta_addr, &bssid, dialog_token, frame_buf, sizeof(frame_buf));
    assert(frame_len == sizeof(ref_frame_3));
    assert(memcmp(&frame_buf, &ref_frame_3, frame_len) == 0);
}

static bool
osw_rrm_meas_ut_mux_frame_tx_schedule_success(const char *phy_name,
                                         const char *vif_name,
                                         struct osw_drv_frame_tx_desc *desc)
{
    g_dummy_drv.frame_tx_cnt++;
    memcpy(&g_dummy_drv.frame_buf, osw_drv_frame_tx_desc_get_frame(desc), osw_drv_frame_tx_desc_get_frame_len(desc));
    g_dummy_drv.frame_len = osw_drv_frame_tx_desc_get_frame_len(desc);

    osw_rrm_meas_drv_frame_tx_result_cb(desc, OSW_FRAME_TX_RESULT_SUBMITTED, desc->caller_priv);

    return true;
}

static bool
osw_rrm_meas_ut_mux_frame_tx_schedule_error(const char *phy_name,
                                            const char *vif_name,
                                            struct osw_drv_frame_tx_desc *desc)
{
    g_dummy_drv.frame_tx_cnt++;
    memcpy(&g_dummy_drv.frame_buf, osw_drv_frame_tx_desc_get_frame(desc), osw_drv_frame_tx_desc_get_frame_len(desc));
    g_dummy_drv.frame_len = osw_drv_frame_tx_desc_get_frame_len(desc);

    osw_rrm_meas_drv_frame_tx_result_cb(desc, OSW_FRAME_TX_RESULT_FAILED, desc->caller_priv);

    return true;
}

static void
osw_rrm_meas_req_tx_complete_cb(const struct osw_rrm_meas_sta_observer *observer)
{
    struct osw_rrm_meas_ut_sta_observer *sta_observer = (struct osw_rrm_meas_ut_sta_observer*) container_of(observer, struct osw_rrm_meas_ut_sta_observer, observer);
    sta_observer->tx_complete_cnt++;
}

static void
osw_rrm_meas_req_tx_error_cb(const struct osw_rrm_meas_sta_observer *observer)
{
    struct osw_rrm_meas_ut_sta_observer *sta_observer = (struct osw_rrm_meas_ut_sta_observer*) container_of(observer, struct osw_rrm_meas_ut_sta_observer, observer);
    sta_observer->tx_error_cnt++;
}

static const struct osw_state_vif_info *
osw_rrm_meas_ut_vif_lookup(const char *phy_name,
                           const char *vif_name)
{
    static const struct osw_drv_vif_state vif_drv_state = {
        .mac_addr = { .octet = { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, } }
    };
    static const struct osw_state_vif_info  vif_info = {
        .drv_state = &vif_drv_state,
    };

    return &vif_info;
}

OSW_UT(osw_rrm_meas_ut_send_single_meas)
{
    const struct osw_drv_dot11_frame *frame = NULL;
    const struct osw_hwaddr sta_addr = { .octet = { 0xd4, 0x61, 0x9d, 0x53, 0x75, 0x05, } };
    const struct osw_rrm_meas_req_params req_params = {
        .op_class = 81,
        .channel  = 0,
        .bssid    = { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
        .ssid     = { .buf = "", .len = strlen("") },
    };
    char *phy_name = "phy0";
    char *vif_name = "vif0";
    struct osw_rrm_meas_desc *desc = NULL;
    struct osw_rrm_meas_ut_sta_observer sta_observer = {
        .observer = {
            .req_tx_complete_fn = osw_rrm_meas_req_tx_complete_cb,
            .req_tx_error_fn = osw_rrm_meas_req_tx_error_cb,
        },
        .tx_complete_cnt = 0,
        .tx_error_cnt = 0,
    };
    bool result;

    osw_time_set_mono_clk(0);
    osw_time_set_wall_clk(0);

    desc = osw_rrm_meas_get_desc(&sta_addr, &sta_observer.observer, phy_name, vif_name);
    assert(desc != NULL);
    desc->mux_frame_tx_schedule_fn = osw_rrm_meas_ut_mux_frame_tx_schedule_success;
    desc->vif_lookup_fn = osw_rrm_meas_ut_vif_lookup;

    /* Schedule RRM Measurement Request */
    result = osw_rrm_meas_desc_set_req_params(desc, &req_params);
    osw_ut_time_advance(0);

    assert(result == true);
    assert(sta_observer.tx_complete_cnt == 1);
    assert(sta_observer.tx_error_cnt == 0);
    assert(g_dummy_drv.frame_tx_cnt == 1);
    frame = (const struct osw_drv_dot11_frame*) &g_dummy_drv.frame_buf;
    assert(frame->u.action.u.rrm_meas_req.dialog_token == 0);

    /* Schedule RRM Measurement Request again for connected STA */
    result = osw_rrm_meas_desc_set_req_params(desc, &req_params);
    osw_ut_time_advance(0);

    assert(result == true);
    assert(sta_observer.tx_complete_cnt == 2);
    assert(sta_observer.tx_error_cnt == 0);
    assert(g_dummy_drv.frame_tx_cnt == 2);
    frame = (const struct osw_drv_dot11_frame*) &g_dummy_drv.frame_buf;
    assert(frame->u.action.u.rrm_meas_req.dialog_token == 1);

    /* Recreate desc, but with failing drv tx */
    osw_rrm_meas_sta_free(desc->sta);

    desc = osw_rrm_meas_get_desc(&sta_addr, &sta_observer.observer, phy_name, vif_name);
    assert(desc != NULL);
    desc->mux_frame_tx_schedule_fn = osw_rrm_meas_ut_mux_frame_tx_schedule_error;
    desc->vif_lookup_fn = osw_rrm_meas_ut_vif_lookup;

    /* Schedule RRM Measurement Request again for connected STA */
    result = osw_rrm_meas_desc_set_req_params(desc, &req_params);
    osw_ut_time_advance(0);

    assert(result == true);
    assert(sta_observer.tx_complete_cnt == 2);
    assert(sta_observer.tx_error_cnt == 1);
    assert(g_dummy_drv.frame_tx_cnt == 3);
    frame = (const struct osw_drv_dot11_frame*) &g_dummy_drv.frame_buf;
    assert(frame->u.action.u.rrm_meas_req.dialog_token == 0);
    osw_rrm_meas_sta_free(desc->sta);
}

OSW_UT(osw_rrm_meas_ut_process_rrm_report)
{
    const struct osw_hwaddr sta_mac_1 = { .octet = { 0xde, 0xad, 0xca, 0xfe, 0xfe, 0xed } };
    const struct osw_hwaddr sta_mac_2 = { .octet = { 0x01, 0x23, 0x34, 0x56, 0x78, 0x9a } };
    const struct osw_hwaddr neigh1_mac_1 = { .octet = { 0xfe, 0x9f, 0x07, 0x00, 0xb1, 0x8c } };
    const struct osw_hwaddr neigh2_mac_1 = { .octet = { 0xfe, 0x9f, 0x07, 0x00, 0xb1, 0x69 } };
    static const unsigned char report_frame_1[] = {
        0x05, 0x01, 0x01, 0x27, 0x21, 0x02, 0x00, 0x05,
        0x80, 0x2c, 0x21, 0x37, 0xfd, 0xa8, 0x00, 0x00,
        0x00, 0x00, 0x1e, 0x00, 0x09, 0xa4, 0xff, 0xfe,
        0x9f, 0x07, 0x00, 0xb1, 0x8c, 0x00, 0xe1, 0xe9,
        0x28, 0x2e, 0x02, 0x02, 0x01, 0x00, 0x27, 0x21,
        0x02, 0x00, 0x05, 0x80, 0x2c, 0x21, 0x37, 0xfd,
        0xa8, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x09,
        0xa2, 0xff, 0xfe, 0x9f, 0x07, 0x00, 0xb1, 0x69,
        0x00, 0xa0, 0xd7, 0x28, 0x2e, 0x02, 0x02, 0x01,
        0x00,
    };

    static const unsigned char report_frame_2[] = {
        0x05, 0x01, 0x01, 0x27, 0x03, 0x02, 0x04, 0x05,
        0xd8, 0x0b, 0xb4, 0xf4,
    };
    size_t neigh_count;
    struct osw_rrm_meas_rep_neigh *neigh;
    struct osw_rrm_meas_desc *sta_desc_1, *sta_desc_2;
    struct osw_rrm_meas_sta_observer sta_observer = {
        .req_tx_complete_fn = osw_rrm_meas_req_tx_complete_cb,
        .req_tx_error_fn = osw_rrm_meas_req_tx_error_cb,
    };

    /* create 2 stations */
    sta_desc_1 = osw_rrm_meas_get_desc(&sta_mac_1, &sta_observer, "phy0", "vif0");
    assert(sta_desc_1 != NULL);
    sta_desc_2 = osw_rrm_meas_get_desc(&sta_mac_2, &sta_observer, "phy0", "vif0");
    assert(sta_desc_2 != NULL);

    /* validate report of 2 neighbors */
    neigh_count = osw_rrm_meas_rep_parse_update_neighs(report_frame_1,
                                                       sizeof(report_frame_1),
                                                       &sta_mac_1);
    assert(neigh_count == 2);

    neigh = (struct osw_rrm_meas_rep_neigh *)osw_rrm_meas_get_neigh(&sta_mac_1, &neigh1_mac_1);
    assert(neigh != NULL);
    assert(memcmp(neigh->bssid.octet, neigh1_mac_1.octet, OSW_HWADDR_LEN) == 0);
    assert(neigh->op_class == 128);
    assert(neigh->channel == 44);
    assert(neigh->rcpi == 164);
    assert(neigh->scan_start_time == 0x00000000a8fd3721);

    neigh = (struct osw_rrm_meas_rep_neigh *)osw_rrm_meas_get_neigh(&sta_mac_1, &neigh2_mac_1);
    assert(neigh != NULL);
    assert(memcmp(neigh->bssid.octet, neigh2_mac_1.octet, OSW_HWADDR_LEN) == 0);
    assert(neigh->op_class == 128);
    assert(neigh->channel == 44);
    assert(neigh->rcpi == 162);
    assert(neigh->scan_start_time == 0x00000000a8fd3721);

    /* validate rejected report */
    neigh_count = osw_rrm_meas_rep_parse_update_neighs(report_frame_2,
                                                       sizeof(report_frame_2),
                                                       &sta_mac_2);
    assert(neigh_count == 0);
}
