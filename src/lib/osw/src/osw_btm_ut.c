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

/* Conflicts with osw_drv_i. This file shouldn't be
 * including that...
 */
#undef LOG_PREFIX

#include <osw_ut.h>
#include <osw_drv_common.h>
#include <osw_drv_dummy.h>
#include "osw_drv_i.h"

struct osw_btm_ut_dummy_drv {
    unsigned int frame_tx_cnt;
    uint8_t frame_buf [OSW_DRV_FRAME_TX_DESC_BUF_SIZE];
    ssize_t frame_len;
};

struct osw_btm_ut_sta_observer {
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
     * 0020   10 52 b4 f7 f0 1a be 13 00 00 00 7d 9d 09 03 01
     * 0030   ff 34 10 52 b4 f7 f0 1a cd 13 00 00 00 7d 95 09
     * 0040   03 01 7f
     */
    static const unsigned char ref_frame1[] = {
        0xd0, 0x00, 0x3c, 0x00, 0xd4, 0x61, 0x9d, 0x53,
        0x75, 0x05, 0x52, 0xb4, 0xf7, 0xf0, 0x1c, 0xcd,
        0x52, 0xb4, 0xf7, 0xf0, 0x1c, 0xcd, 0x00, 0x00,
        0x0a, 0x07, 0x01, 0x07, 0x01, 0x00, 0xff,

        /* Neighbor 1 */
        0x34, 0x10, 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xbe,
        0x13, 0x00, 0x00, 0x00, 0x7d, 0x9d, 0x09,
        /* - Preference SubElement */
        0x03, 0x01, 0xff,

        /* Neighbor 2 */
        0x34, 0x10, 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xcd,
        0x13, 0x00, 0x00, 0x00, 0x7d, 0x92, 0x09,
        /* - Preference SubElement */
        0x03, 0x01, 0x7f,
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
                .btmpreference = 255,
                .phy_type = 0x9,
            },
            {
                .bssid = { .octet = { 0x52, 0xb4, 0xf7, 0xf0, 0x1a, 0xcd } },
                .bssid_info = 0x13,
                .op_class = 125,
                .channel = 146,
                .disassoc_imminent = true,
                .btmpreference = 127,
                .phy_type = 0x9,
            },
        },
        .neigh_len = 2,
        .valid_int = 255,
        .abridged  = true,
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
osw_btm_req_completed_cb(void *priv, enum osw_btm_req_result result)
{
    struct osw_btm_ut_sta_observer *sta_observer = priv;
    switch (result) {
        case OSW_BTM_REQ_RESULT_SENT:
            sta_observer->tx_complete_cnt++;
            break;
        case OSW_BTM_REQ_RESULT_FAILED:
            sta_observer->tx_error_cnt++;
            break;
    }
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
                .disassoc_imminent = true,
                .phy_type = 0x9,
            },
        },
        .neigh_len = 2,
        .valid_int = 255,
        .abridged  = true,
        .bss_term = false,
    };

    struct osw_drv_vif_state drv_vif_state = {
        .exists = true,
        .mac_addr = { .octet = { 0xac, 0x4c, 0x56, 0x03, 0xd2, 0x1b }, },
    };

    struct osw_btm_ut_sta_observer sta_observer = {
        .tx_complete_cnt = 0,
        .tx_error_cnt = 0,
    };

    struct osw_drv_dummy dummy = {
        .name = "dummy",
    };

    osw_btm_t *m = OSW_MODULE_LOAD(osw_btm);
    osw_btm_sta_t *sta = osw_btm_sta_alloc(m, &sta_addr);
    osw_btm_req_t *r;

    m->mux_frame_tx_schedule_fn = osw_btm_ut_mux_frame_tx_schedule_success;
    osw_time_set_mono_clk(0);
    osw_time_set_wall_clk(0);

    osw_drv_dummy_init(&dummy);
    osw_drv_dummy_set_phy(&dummy, "phy1", (struct osw_drv_phy_state []) {{ .exists = true }});
    osw_drv_dummy_set_vif(&dummy, "phy1", "vif1", &drv_vif_state);
    osw_ut_time_advance(0);
    assert(osw_state_vif_lookup_by_mac_addr(&drv_vif_state.mac_addr) != NULL);

    /* Cannot queue BTM Request for disconnected STA */
    r = osw_btm_req_alloc(sta);
    assert(osw_btm_req_set_completed_fn(r, osw_btm_req_completed_cb, &sta_observer) == true);
    assert(osw_btm_req_set_params(r, &req_params) == true);
    assert(osw_btm_req_submit(r) == false);
    assert(sta_observer.tx_complete_cnt == 0);
    assert(sta_observer.tx_error_cnt == 0);
    osw_btm_req_drop(r);

    /* Schedule BTM Request for connected STA */
    sta->assoc.links.array[0].local_sta_addr = drv_vif_state.mac_addr;
    sta->assoc.links.array[0].remote_sta_addr = sta_addr;
    sta->assoc.links.count = 1;
    sta->assoc.links.local_mld_addr = *osw_hwaddr_zero();
    r = osw_btm_req_alloc(sta);
    assert(osw_btm_req_set_completed_fn(r, osw_btm_req_completed_cb, &sta_observer) == true);
    assert(osw_btm_req_set_params(r, &req_params) == true);
    assert(osw_btm_req_submit(r) == true);
    assert(sta_observer.tx_complete_cnt == 1);
    assert(sta_observer.tx_error_cnt == 0);
    assert(g_dummy_drv.frame_tx_cnt == 1);
    frame = (const struct osw_drv_dot11_frame*) &g_dummy_drv.frame_buf;
    assert(frame->u.action.u.bss_tm_req.dialog_token == OSW_TOKEN_MAX);
    osw_btm_req_drop(r);

    /* Schedule BTM Request again for connected STA */
    r = osw_btm_req_alloc(sta);
    assert(osw_btm_req_set_completed_fn(r, osw_btm_req_completed_cb, &sta_observer) == true);
    assert(osw_btm_req_set_params(r, &req_params) == true);
    assert(osw_btm_req_submit(r) == true);
    assert(sta_observer.tx_complete_cnt == 2);
    assert(sta_observer.tx_error_cnt == 0);
    assert(g_dummy_drv.frame_tx_cnt == 2);
    frame = (const struct osw_drv_dot11_frame*) &g_dummy_drv.frame_buf;
    assert(frame->u.action.u.bss_tm_req.dialog_token == (OSW_TOKEN_MAX - 1));
    osw_btm_req_drop(r);

    /* Now simulate tx failures */
    m->mux_frame_tx_schedule_fn = osw_btm_ut_mux_frame_tx_schedule_error;

    /* Schedule BTM Request again for connected STA */
    sta->assoc.links.array[0].local_sta_addr = drv_vif_state.mac_addr;
    sta->assoc.links.array[0].remote_sta_addr = sta_addr;
    sta->assoc.links.count = 1;
    sta->assoc.links.local_mld_addr = *osw_hwaddr_zero();
    r = osw_btm_req_alloc(sta);
    assert(osw_btm_req_set_completed_fn(r, osw_btm_req_completed_cb, &sta_observer) == true);
    assert(osw_btm_req_set_params(r, &req_params) == true);
    assert(osw_btm_req_submit(r) == true);
    assert(sta_observer.tx_complete_cnt == 2);
    assert(sta_observer.tx_error_cnt == 1);
    assert(g_dummy_drv.frame_tx_cnt == 3);
    frame = (const struct osw_drv_dot11_frame*) &g_dummy_drv.frame_buf;
    assert(frame->u.action.u.bss_tm_req.dialog_token == (OSW_TOKEN_MAX - 2));
    osw_btm_req_drop(r);
}

OSW_UT(osw_btm_ut_resp_parse)
{
    static const unsigned char ref_frame1[] = {
        0xd0, 0x00, // frame control
        0x00, 0x00, // duration
        0x11, 0xaa, 0xaa, 0xaa, 0xaa, 0x44, // ra
        0x22, 0xaa, 0xaa, 0xaa, 0xaa, 0x55, // ta
        0x33, 0xaa, 0xaa, 0xaa, 0xaa, 0x66, // bssid
        0x00, 0x00, // sequence

        0x0A, // category
        0x08, // action
        0x99, // dialog token
        0x88, // status code
        0x00, // bss termination delay

        // neighbor 1
        0x34, // nr element id
        0x10, // length
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // bssid
        0x01, 0x02, 0x03, 0x04, // bssid info
        0x01, // op class
        0x02, // channel
        0x03, // phy_type

        0x00, // preference sub elem id
        0x01, // length
        0x42, // preference value

        // neighbor 2
        0x34, // nr element id
        0x10, // length
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, // bssid
        0x05, 0x06, 0x07, 0x08, // bssid info
        0x04, // op class
        0x05, // channel
        0x06, // phy_type

        0x00, // preference sub elem id
        0x01, // length
        0x24, // preference value
    };
    const struct osw_hwaddr sta1 = { .octet = { 0x11, 0xaa, 0xaa, 0xaa, 0xaa, 0x44 } };
    const struct osw_hwaddr ap1 = { .octet = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 } };
    const struct osw_hwaddr ap2 = { .octet = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 } };

    osw_btm_resp_t resp;
    MEMZERO(resp);
    const bool ok = osw_btm_resp_parse(ref_frame1, sizeof(ref_frame1), &resp);

    assert(ok);
    assert(resp.status_code == 0x88);
    assert(resp.dialog_token == 0x99);
    assert(osw_hwaddr_is_equal(&resp.sta_addr, &sta1));

    assert(resp.n_neighs == 2);

    assert(osw_hwaddr_is_equal(&resp.neighs[0].neigh.bssid, &ap1));
    assert(resp.neighs[0].neigh.bssid_info == 0x04030201);
    assert(resp.neighs[0].neigh.op_class == 0x01);
    assert(resp.neighs[0].neigh.channel == 0x02);
    assert(resp.neighs[0].neigh.phy_type == 0x03);
    assert(resp.neighs[0].preference == 0x42);

    assert(osw_hwaddr_is_equal(&resp.neighs[1].neigh.bssid, &ap2));
    assert(resp.neighs[1].neigh.bssid_info == 0x08070605);
    assert(resp.neighs[1].neigh.op_class == 0x04);
    assert(resp.neighs[1].neigh.channel == 0x05);
    assert(resp.neighs[1].neigh.phy_type == 0x06);
    assert(resp.neighs[1].preference == 0x24);
}

OSW_UT(osw_btm_ut_mbo)
{
    char buf[64];
    ssize_t rem = sizeof(buf);
    void *tail = buf;
    struct osw_btm_req_params params = {
        .mbo = {
            .cell_preference = OSW_BTM_MBO_CELL_PREF_NONE,
            .reason = OSW_BTM_MBO_REASON_NONE,
        },
    };

    /* Test: no MBO params should yield no MBO elements */
    osw_btm_mbo_put(&tail, &rem, &params);
    assert(buf_len(tail, buf) == 0);

    /* Test: Check if at least one of the attributes sets up MBO elements as expected */
    params.mbo.cell_preference = OSW_BTM_MBO_CELL_PREF_EXCLUDE_CELL;
    osw_btm_mbo_put(&tail, &rem, &params);
    {
        ssize_t len = buf_len(buf, tail);
        const void *head = buf;

        uint8_t eid;
        uint8_t elen;
        assert(buf_get_u8(&head, &len, &eid));
        assert(buf_get_u8(&head, &len, &elen));
        assert(eid == C_IEEE80211_EID_VENDOR);

        const void *data;
        ssize_t datalen = elen;
        assert(buf_get_as_ptr(&head, &len, &data, elen));

        uint8_t oui[3];
        uint8_t oui_type;
        assert(buf_get_u8(&data, &datalen, &oui[0]));
        assert(buf_get_u8(&data, &datalen, &oui[1]));
        assert(buf_get_u8(&data, &datalen, &oui[2]));
        assert(buf_get_u8(&data, &datalen, &oui_type));
        assert(oui[0] == C_IEEE80211_WFA_OUI_BYTE0);
        assert(oui[1] == C_IEEE80211_WFA_OUI_BYTE1);
        assert(oui[2] == C_IEEE80211_WFA_OUI_BYTE2);
        assert(oui_type == C_IEEE80211_MBO_OUI_TYPE);

        uint8_t aid;
        uint8_t alen;
        uint8_t attr;
        assert(buf_get_u8(&data, &datalen, &aid));
        assert(buf_get_u8(&data, &datalen, &alen));
        assert(buf_get_u8(&data, &datalen, &attr));

        assert(aid == C_IEEE80211_MBO_ATTR_CELL_PREF);
        assert(alen == 1);
        assert(attr == C_IEEE80211_MBO_CELL_PREF_EXCLUDE);
    }
}
