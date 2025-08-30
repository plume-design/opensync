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

struct osw_rrm_ut_typical_usage_req {
    bool sent;
    bool replied;
    bool timeout;
};

struct osw_rrm_ut_typical_usage_ctx {
    struct osw_rrm_desc_observer observer;
    struct osw_rrm_ut_typical_usage_req reqs[2][2];
};

struct osw_rrm_ut_process_empty_report_ctx {
    struct osw_rrm_rpt_observer observer;
    uint8_t fn_call_cnt;
};

struct osw_rrm_ut_process_multi_report_ctx {
    struct osw_rrm_rpt_observer observer;
    uint8_t fn_call_cnt;
};

static void
osw_rrm_ut_typical_usage_req_status_cb(void *priv,
                                       enum osw_rrm_radio_status status,
                                       const uint8_t *dialog_token,
                                       const uint8_t *meas_token,
                                       const struct osw_rrm_radio_meas_req *radio_meas_req)
{
    struct osw_rrm_ut_typical_usage_ctx *ctx = priv;

    OSW_UT_EVAL(dialog_token != NULL);
    OSW_UT_EVAL(*dialog_token >= 1);
    OSW_UT_EVAL(*dialog_token <= ARRAY_SIZE(ctx->reqs));

    OSW_UT_EVAL(meas_token != NULL);
    OSW_UT_EVAL(*meas_token >= 1);
    OSW_UT_EVAL(*meas_token <= ARRAY_SIZE(ctx->reqs[0]));

    size_t di = *dialog_token - 1;
    size_t mi = *meas_token - 1;

    switch (status) {
        case OSW_RRM_RADIO_STATUS_SENT:
            ctx->reqs[di][mi].sent = true;
            break;
        case OSW_RRM_RADIO_STATUS_REPLIED:
            ctx->reqs[di][mi].replied = true;
            break;
        case OSW_RRM_RADIO_STATUS_TIMEOUT:
            ctx->reqs[di][mi].timeout = true;
            break;
    }
}

static bool
osw_rrm_ut_mux_frame_tx_schedule_nop_cb(const char *phy_name,
                                        const char *vif_name,
                                        struct osw_drv_frame_tx_desc *desc)
{
    /* nop */
    return true;
}

static const struct osw_state_sta_info *
osw_rrm_ut_state_sta_lookup_newest(const struct osw_hwaddr *mac_addr)
{
    static struct osw_hwaddr sta_addr = { .octet = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };
    if (osw_hwaddr_cmp(mac_addr, &sta_addr) != 0)
        return NULL;

    static struct osw_state_phy_info phy_info = {
        .phy_name = "phy0",
    };

    static struct osw_drv_vif_state drv_vif_state = {
        .mac_addr = { .octet = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 } },
    };
    static struct osw_state_vif_info vif_info = {
        .vif_name = "vif0",
        .phy = &phy_info,
        .drv_state = &drv_vif_state,
    };
    static struct osw_drv_sta_state drv_sta_state = { 0 };
    static struct osw_state_sta_info sta_info = {
        .mac_addr = &sta_addr,
        .vif = &vif_info,
        .drv_state = &drv_sta_state,
        .connected_at = 1234,
        .assoc_req_ies = NULL,
        .assoc_req_ies_len = 0
    };

    return &sta_info;
}

OSW_UT(osw_rrm_ut_build_multi_req_frame) {
    osw_ut_time_init();

    const uint8_t ref_frame[] = {
        0xd0, 0x00, 0x3c, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0x26, 0x25, 0x01,
        0x00, 0x05, 0x51, 0x00, 0x00, 0x00, 0x64, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0c, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2d, 0x73, 0x73, 0x69, 0x64, 0x01, 0x02, 0x00,
        0x00, 0x02, 0x01, 0x00, 0x26, 0x17, 0x02, 0x00, 0x05, 0x83, 0x09, 0x00, 0x00, 0x64, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x02, 0x01, 0x00
    };

    const struct osw_hwaddr sta_addr = { .octet = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

    struct osw_rrm rrm;
    osw_rrm_init(&rrm);
    rrm.sta_lookup_newest_wrapper_fn = osw_rrm_ut_state_sta_lookup_newest;

    struct osw_rrm_sta *sta = osw_rrm_get_sta(&rrm, &sta_addr);
    OSW_UT_EVAL(sta != NULL);

    struct osw_rrm_desc_observer observer = {
        .name = "osw_rrm_ut",
        .radio_meas_req_status_fn = NULL,
    };
    struct osw_rrm_desc *desc = osw_rrm_get_desc(&rrm, &sta_addr, &observer, NULL);
    OSW_UT_EVAL(desc != NULL);

    bool result = false;

    const struct osw_rrm_radio_meas_req req_a = {
        .type = OSW_RRM_RADIO_MEAS_REQ_TYPE_BEACON,
        .u.beacon = {
            .op_class = 81,
            .channel = 0,
            .bssid = { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
            .ssid = { .buf = "example-ssid", .len = strlen("example-ssid") },
        },
    };

    result = osw_rrm_desc_schedule_radio_meas_req(desc, &req_a);
    OSW_UT_EVAL(result == true);

    const struct osw_rrm_radio_meas_req req_b = {
        .type = OSW_RRM_RADIO_MEAS_REQ_TYPE_BEACON,
        .u.beacon = {
            .op_class = 131,
            .channel = 9,
            .bssid = { .octet = { 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe } },
            .ssid = { .buf = "", .len = 0 },
        },
    };

    result = osw_rrm_desc_schedule_radio_meas_req(desc, &req_b);
    OSW_UT_EVAL(result == true);

    const uint8_t dialog_token = osw_rrm_sta_gen_dialog_token(sta);
    uint8_t frame_buf [OSW_DRV_FRAME_TX_DESC_BUF_SIZE];
    const struct osw_state_sta_info *sta_info = osw_rrm_ut_state_sta_lookup_newest(&sta_addr);
    OSW_UT_EVAL(sta_info != NULL);
    const ssize_t frame_len = osw_rrm_build_radio_meas_req_frame(sta, sta_info, dialog_token, frame_buf, sizeof(frame_buf));

    OSW_UT_EVAL(frame_len == sizeof(ref_frame));
    OSW_UT_EVAL(memcmp(frame_buf, ref_frame, frame_len) == 0);
}

OSW_UT(osw_rrm_ut_build_single_req_frame) {
    osw_ut_time_init();

    const uint8_t ref_frame[] = {
        0xd0, 0x00, 0x3c, 0x00, 0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x00,
        0x05, 0x00, 0x01, 0x00, 0x00, 0x26, 0x25, 0x01,
        0x00, 0x05, 0x51, 0x00, 0x00, 0x00, 0x64, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0c, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65,
        0x2d, 0x73, 0x73, 0x69, 0x64, 0x01, 0x02, 0x00,
        0x00, 0x02, 0x01, 0x00
    };

    const struct osw_hwaddr sta_addr = { .octet = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };
    const struct osw_state_sta_info *sta_info = osw_rrm_ut_state_sta_lookup_newest(&sta_addr);
    OSW_UT_EVAL(sta_info != NULL);

    struct osw_rrm rrm;
    osw_rrm_init(&rrm);
    rrm.sta_lookup_newest_wrapper_fn = osw_rrm_ut_state_sta_lookup_newest;

    struct osw_rrm_sta *sta = osw_rrm_get_sta(&rrm, &sta_addr);
    OSW_UT_EVAL(sta != NULL);

    struct osw_rrm_desc_observer observer = {
        .name = "osw_rrm_ut",
        .radio_meas_req_status_fn = NULL,
    };
    struct osw_rrm_desc *desc = osw_rrm_get_desc(&rrm, &sta_addr, &observer, NULL);
    OSW_UT_EVAL(desc != NULL);

    bool result = false;

    const struct osw_rrm_radio_meas_req req_a = {
        .type = OSW_RRM_RADIO_MEAS_REQ_TYPE_BEACON,
        .u.beacon = {
            .op_class = 81,
            .channel = 0,
            .bssid = { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
            .ssid = { .buf = "example-ssid", .len = strlen("example-ssid") },
        },
    };

    result = osw_rrm_desc_schedule_radio_meas_req(desc, &req_a);
    OSW_UT_EVAL(result == true);

    const uint8_t dialog_token = osw_rrm_sta_gen_dialog_token(sta);
    uint8_t frame_buf [OSW_DRV_FRAME_TX_DESC_BUF_SIZE];
    const ssize_t frame_len = osw_rrm_build_radio_meas_req_frame(sta, sta_info, dialog_token, frame_buf, sizeof(frame_buf));

    OSW_UT_EVAL(frame_len == sizeof(ref_frame));
    OSW_UT_EVAL(memcmp(frame_buf, ref_frame, frame_len) == 0);
}

static void
osw_rrm_ut_process_empty_report(struct osw_rrm_rpt_observer *observer,
                                const struct osw_drv_dot11_frame_header *frame_header,
                                const struct osw_drv_dot11_frame_action_rrm_meas_rep *rrm_meas_rep,
                                const struct osw_drv_dot11_meas_rep_ie_fixed *meas_rpt_ie_fixed,
                                const struct osw_drv_dot11_meas_rpt_ie_beacon *meas_rpt_ie_beacon)
{
    OSW_UT_EVAL(observer != NULL);
    OSW_UT_EVAL(rrm_meas_rep != NULL);
    OSW_UT_EVAL(meas_rpt_ie_fixed != NULL);
    OSW_UT_EVAL(meas_rpt_ie_beacon == NULL);

    struct osw_rrm_ut_process_empty_report_ctx *ctx = container_of(observer, struct osw_rrm_ut_process_empty_report_ctx, observer);
    ctx->fn_call_cnt++;
}

OSW_UT(osw_rrm_process_empty_report)
{
    /* Measurement Report Mode: Refused */
    const uint8_t ref_frame[] = {
        0xd0, 0x00, 0x3c, 0x00, 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe, 0xd4, 0x61, 0x9d, 0x53, 0x75, 0x05,
        0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe, 0xc0, 0x9a, 0x05, 0x01, 0x01, 0x27, 0x03, 0x02, 0x04, 0x05,
    };

    struct osw_rrm rrm;
    osw_rrm_init(&rrm);

    struct osw_rrm_ut_process_empty_report_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.observer.bcn_meas_fn = osw_rrm_ut_process_empty_report;

    osw_rrm_register_rpt_observer(&rrm, &ctx.observer);

    osw_rrm_process_frame(&rrm, ref_frame, sizeof(ref_frame));

    OSW_UT_EVAL(ctx.fn_call_cnt == 1);
}

static void
osw_rrm_ut_process_multi_report(struct osw_rrm_rpt_observer *observer,
                                const struct osw_drv_dot11_frame_header *frame_header,
                                const struct osw_drv_dot11_frame_action_rrm_meas_rep *rrm_meas_rep,
                                const struct osw_drv_dot11_meas_rep_ie_fixed *meas_rpt_ie_fixed,
                                const struct osw_drv_dot11_meas_rpt_ie_beacon *meas_rpt_ie_beacon)
{
    OSW_UT_EVAL(observer != NULL);
    OSW_UT_EVAL(rrm_meas_rep != NULL);
    OSW_UT_EVAL(meas_rpt_ie_fixed != NULL);
    OSW_UT_EVAL(meas_rpt_ie_beacon != NULL);

    const struct osw_hwaddr ref_sta_addr = { .octet = { 0xd4, 0x61, 0x9d, 0x53, 0x75, 0x05 } };
    OSW_UT_EVAL(memcmp(&ref_sta_addr.octet, &frame_header->sa, sizeof(ref_sta_addr.octet)) == 0);

    const uint8_t dialog_token = rrm_meas_rep->dialog_token;
    const uint8_t meas_token = meas_rpt_ie_fixed->token;

    if (dialog_token == 1 && meas_token == 1) {
        const struct osw_hwaddr ref_bssid = { .octet = { 0xfe, 0x9f, 0x07, 0x00, 0xb1, 0x8c } };
        OSW_UT_EVAL(memcmp(&ref_bssid.octet, &meas_rpt_ie_beacon->bssid, sizeof(ref_bssid.octet)) == 0);

        OSW_UT_EVAL(meas_rpt_ie_beacon->op_class == 128);
        OSW_UT_EVAL(meas_rpt_ie_beacon->channel == 44);
        OSW_UT_EVAL(meas_rpt_ie_beacon->rcpi == 164);
    }
    else if (dialog_token == 1 && meas_token == 2) {
        const struct osw_hwaddr ref_bssid = { .octet = { 0xfe, 0x9f, 0x07, 0x00, 0xb1, 0x69 } };
        OSW_UT_EVAL(memcmp(&ref_bssid.octet, &meas_rpt_ie_beacon->bssid, sizeof(ref_bssid.octet)) == 0);

        OSW_UT_EVAL(meas_rpt_ie_beacon->op_class == 128);
        OSW_UT_EVAL(meas_rpt_ie_beacon->channel == 44);
        OSW_UT_EVAL(meas_rpt_ie_beacon->rcpi == 162);
    }
    else {
        OSW_UT_EVAL(false);
    }

    struct osw_rrm_ut_process_multi_report_ctx *ctx = container_of(observer, struct osw_rrm_ut_process_multi_report_ctx, observer);
    ctx->fn_call_cnt++;
}

OSW_UT(osw_rrm_process_multi_report)
{
    /* This frame contains two reports */
    const uint8_t ref_frame[] = {
        0xd0, 0x00, 0x3c, 0x00, 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe, 0xd4, 0x61, 0x9d, 0x53, 0x75, 0x05,
        0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe, 0xc0, 0x9a, 0x05, 0x01, 0x01, 0x27, 0x21, 0x01, 0x00, 0x05,
        0x80, 0x2c, 0x21, 0x37, 0xfd, 0xa8, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x09, 0xa4, 0xff, 0xfe,
        0x9f, 0x07, 0x00, 0xb1, 0x8c, 0x00, 0xe1, 0xe9, 0x28, 0x2e, 0x02, 0x02, 0x01, 0x00, 0x27, 0x21,
        0x02, 0x00, 0x05, 0x80, 0x2c, 0x21, 0x37, 0xfd, 0xa8, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x09,
        0xa2, 0xff, 0xfe, 0x9f, 0x07, 0x00, 0xb1, 0x69, 0x00, 0xa0, 0xd7, 0x28, 0x2e, 0x02, 0x02, 0x01,
        0x00
    };

    struct osw_rrm rrm;
    osw_rrm_init(&rrm);

    struct osw_rrm_ut_process_multi_report_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.observer.bcn_meas_fn = osw_rrm_ut_process_multi_report;

    osw_rrm_register_rpt_observer(&rrm, &ctx.observer);

    osw_rrm_process_frame(&rrm, ref_frame, sizeof(ref_frame));

    OSW_UT_EVAL(ctx.fn_call_cnt == 2);
}

#define OSW_RRM_UT_CHECK_REQ(ctx, dialog_token, meas_token, exp_sent, exp_replied, exp_timeout) \
    OSW_UT_EVAL(ctx.reqs[dialog_token - 1][meas_token - 1].sent == exp_sent);                   \
    OSW_UT_EVAL(ctx.reqs[dialog_token - 1][meas_token - 1].replied == exp_replied);             \
    OSW_UT_EVAL(ctx.reqs[dialog_token - 1][meas_token - 1].timeout == exp_timeout)

OSW_UT(osw_rrm_ut_typical_usage) {
    osw_ut_time_init();

    const struct osw_hwaddr sta_addr = { .octet = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

    struct osw_rrm rrm;
    osw_rrm_init(&rrm);
    rrm.sta_lookup_newest_wrapper_fn = osw_rrm_ut_state_sta_lookup_newest;
    rrm.mux_frame_tx_schedule_wrapper_fn = osw_rrm_ut_mux_frame_tx_schedule_nop_cb;

    /* Get descriptors */
    struct osw_rrm_ut_typical_usage_ctx ctx_a;
    memset(&ctx_a, 0, sizeof(ctx_a));
    ctx_a.observer.name = "osw_rrm_ut";
    ctx_a.observer.radio_meas_req_status_fn = osw_rrm_ut_typical_usage_req_status_cb;

    struct osw_rrm_desc *desc_a = osw_rrm_get_desc(&rrm, &sta_addr, &ctx_a.observer, &ctx_a);
    OSW_UT_EVAL(desc_a != NULL);

    struct osw_rrm_ut_typical_usage_ctx ctx_b;
    memset(&ctx_b, 0, sizeof(ctx_b));
    ctx_b.observer.name = "osw_rrm_ut";
    ctx_b.observer.radio_meas_req_status_fn = osw_rrm_ut_typical_usage_req_status_cb;

    struct osw_rrm_desc *desc_b = osw_rrm_get_desc(&rrm, &sta_addr, &ctx_b.observer, &ctx_b);
    OSW_UT_EVAL(desc_b != NULL);

    struct osw_rrm_sta *sta = osw_rrm_get_sta(&rrm, &sta_addr);
    OSW_UT_EVAL(sta != NULL);

    /* Submit requests on descriptor A */
    OSW_UT_EVAL(osw_timer_is_armed(&sta->tx_work_timer) == false);
    OSW_UT_EVAL(osw_timer_is_armed(&sta->req_timeout_timer) == false);

    const struct osw_rrm_radio_meas_req req_a1 = {
        .type = OSW_RRM_RADIO_MEAS_REQ_TYPE_BEACON,
        .u.beacon = {
            .op_class = 81,
            .channel = 0,
            .bssid = { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
            .ssid = { .buf = "example-ssid", .len = strlen("example-ssid") },
        },
    };

    bool result = osw_rrm_desc_schedule_radio_meas_req(desc_a, &req_a1);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(osw_timer_is_armed(&sta->tx_work_timer) == true);
    OSW_UT_EVAL(osw_timer_is_armed(&sta->req_timeout_timer) == false);

    const struct osw_rrm_radio_meas_req req_a2 = {
        .type = OSW_RRM_RADIO_MEAS_REQ_TYPE_BEACON,
        .u.beacon = {
            .op_class = 131,
            .channel = 9,
            .bssid = { .octet = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
            .ssid = { .buf = "", .len = 0 },
        },
    };

    result = osw_rrm_desc_schedule_radio_meas_req(desc_a, &req_a2);
    OSW_UT_EVAL(result == true);

    /* Submit requests on descriptor B */
    const struct osw_rrm_radio_meas_req req_b1 = req_a1;

    result = osw_rrm_desc_schedule_radio_meas_req(desc_b, &req_b1);
    OSW_UT_EVAL(result == true);
    OSW_UT_EVAL(osw_timer_is_armed(&sta->tx_work_timer) == true);
    OSW_UT_EVAL(osw_timer_is_armed(&sta->req_timeout_timer) == false);

    /* Send frame */
    osw_ut_time_advance(0);
    OSW_UT_EVAL(osw_timer_is_armed(&sta->tx_work_timer) == false);
    OSW_UT_EVAL(osw_timer_is_armed(&sta->req_timeout_timer) == true);

    OSW_RRM_UT_CHECK_REQ(ctx_a, 1, 1, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_a, 1, 2, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_a, 2, 1, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_a, 2, 2, false, false, false);

    OSW_RRM_UT_CHECK_REQ(ctx_b, 1, 1, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 1, 2, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 2, 1, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 2, 2, false, false, false);

    osw_rrm_drv_frame_tx_result_cb(NULL, OSW_FRAME_TX_RESULT_SUBMITTED, sta);

    OSW_RRM_UT_CHECK_REQ(ctx_a, 1, 1, true, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_a, 1, 2, true, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 2, 1, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 2, 2, false, false, false);

    OSW_RRM_UT_CHECK_REQ(ctx_b, 1, 1, true, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 1, 2, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 2, 1, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 2, 2, false, false, false);

    /* Simulate A1(B1) Report RX */
    struct osw_drv_dot11_frame_header frame_header;
    memset(&frame_header, 0, sizeof(frame_header));
    memcpy(&frame_header.sa, &sta_addr.octet, sizeof(frame_header.sa));

    struct osw_drv_dot11_frame_action_rrm_meas_rep rrm_meas_rep_a1;
    memset(&rrm_meas_rep_a1, 0, sizeof(rrm_meas_rep_a1));
    rrm_meas_rep_a1.dialog_token = 1;

    struct osw_drv_dot11_meas_rep_ie_fixed meas_rpt_ie_fixed_a1;
    memset(&meas_rpt_ie_fixed_a1, 0, sizeof(meas_rpt_ie_fixed_a1));
    meas_rpt_ie_fixed_a1.token = 1;

    rrm.rrm_rpt_observer.bcn_meas_fn(&rrm.rrm_rpt_observer, &frame_header, &rrm_meas_rep_a1, &meas_rpt_ie_fixed_a1, NULL);

    OSW_RRM_UT_CHECK_REQ(ctx_a, 1, 1, true, true, false);
    OSW_RRM_UT_CHECK_REQ(ctx_a, 1, 2, true, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_a, 2, 1, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_a, 2, 2, false, false, false);

    OSW_RRM_UT_CHECK_REQ(ctx_b, 1, 1, true, true, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 1, 2, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 2, 1, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 2, 2, false, false, false);

    /* Timeout A2 Report */
    osw_ut_time_advance(OSW_TIME_SEC(OSW_RRM_RADIO_MEAS_REQ_TIMEOUT_SEC));

    OSW_RRM_UT_CHECK_REQ(ctx_a, 1, 1, true, true, false);
    OSW_RRM_UT_CHECK_REQ(ctx_a, 1, 2, true, false, true);
    OSW_RRM_UT_CHECK_REQ(ctx_a, 2, 1, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_a, 2, 2, false, false, false);

    OSW_RRM_UT_CHECK_REQ(ctx_b, 1, 1, true, true, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 1, 2, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 2, 1, false, false, false);
    OSW_RRM_UT_CHECK_REQ(ctx_b, 2, 2, false, false, false);
}
