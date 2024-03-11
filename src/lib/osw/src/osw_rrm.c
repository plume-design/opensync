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

#include <endian.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <log.h>
#include <util.h>
#include <const.h>
#include <memutil.h>
#include <osw_types.h>
#include <osw_throttle.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_state.h>
#include <osw_drv_common.h>
#include <osw_drv_mediator.h>
#include <osw_drv.h>
#include <osw_mux.h>
#include <osw_rrm.h>
#include <osw_util.h>

#define OSW_RRM_RADIO_MEAS_REQ_DEFAULT_DURATION 0x64
#define OSW_RRM_RADIO_MEAS_REQ_TIMEOUT_SEC 10

typedef const struct osw_state_sta_info *
osw_rrm_state_sta_lookup_newest_wrapper_fn_t (const struct osw_hwaddr *mac_addr);

typedef bool
osw_rrm_mux_frame_tx_schedule_wrapper_fn_t(const char *phy_name,
                                           const char *vif_name,
                                           struct osw_drv_frame_tx_desc *desc);

struct osw_rrm_sta {
    struct osw_hwaddr sta_addr;
    struct osw_rrm *rrm;
    struct osw_throttle *throttle;
    struct osw_timer tx_work_timer;
    struct osw_timer req_timeout_timer;
    struct ds_dlist desc_list;
    struct ds_dlist radio_meas_req_list;
    struct osw_drv_frame_tx_desc *frame_tx_desc;

    uint8_t dialog_token;

    struct {
        uint8_t dialog;
        uint8_t meas;
    } token_generator;

    struct ds_tree_node node;
};

struct osw_rrm_desc {
    struct osw_rrm_sta *sta;
    struct osw_rrm_desc_observer *observer;
    void *priv;

    struct ds_dlist_node node;
};

struct osw_rrm_radio_meas_req_entry_desc {
    struct osw_rrm_desc *desc;

    struct ds_dlist_node node;
};

struct osw_rrm_radio_meas_req_entry {
    struct osw_rrm_radio_meas_req req;
    struct ds_dlist desc_list;
    uint8_t meas_token;

    struct ds_dlist_node node;
};

struct osw_rrm {
    struct ds_tree sta_tree;
    struct ds_dlist rpt_observer_list;
    struct osw_state_observer state_observer;
    struct osw_rrm_rpt_observer rrm_rpt_observer;
    osw_rrm_state_sta_lookup_newest_wrapper_fn_t *sta_lookup_newest_wrapper_fn;
    osw_rrm_mux_frame_tx_schedule_wrapper_fn_t *mux_frame_tx_schedule_wrapper_fn;
};


#define OSW_RRM_PREFIX "osw: rrm: "OSW_HWADDR_FMT

static uint8_t
osw_rrm_sta_gen_dialog_token(struct osw_rrm_sta *sta)
{
    ASSERT(sta != NULL, "");

    while (sta->token_generator.dialog == 0)
        sta->token_generator.dialog++;

    return sta->token_generator.dialog++;
}

static uint8_t
osw_rrm_sta_gen_meas_token(struct osw_rrm_sta *sta)
{
    ASSERT(sta != NULL, "");

    while (sta->token_generator.meas == 0)
        sta->token_generator.meas++;

    return sta->token_generator.meas++;
}

static void
osw_rrm_radio_meas_req_entry_free(struct osw_rrm_radio_meas_req_entry *radio_meas_req,
                                  uint8_t dialog_token,
                                  enum osw_rrm_radio_status status)
{
    ASSERT(radio_meas_req != NULL, "");

    const uint8_t *dialog_token_ptr = dialog_token != 0 ? &dialog_token : NULL;

    struct osw_rrm_radio_meas_req_entry_desc *entry_desc = NULL;
    struct osw_rrm_radio_meas_req_entry_desc *tmp_entry_desc = NULL;
    ds_dlist_foreach_safe(&radio_meas_req->desc_list, entry_desc, tmp_entry_desc) {
        const uint8_t *meas_token_ptr = radio_meas_req->meas_token != 0 ? &radio_meas_req->meas_token : NULL;
        struct osw_rrm_desc_observer *desc_observer = entry_desc->desc->observer;
        if (desc_observer == NULL)
            continue;

        struct osw_rrm_desc *desc = entry_desc->desc;
        if (desc == NULL)
            continue;

        void *priv = desc->priv;
        if (desc_observer->radio_meas_req_status_fn != NULL)
            desc_observer->radio_meas_req_status_fn(priv, status, dialog_token_ptr, meas_token_ptr, &radio_meas_req->req);

        ds_dlist_remove(&radio_meas_req->desc_list, entry_desc);
        FREE(entry_desc);
    }

    FREE(radio_meas_req);
}

static size_t
osw_rrm_build_radio_meas_req_compute_frame_len(struct osw_rrm_sta *sta)
{
    ASSERT(sta != NULL, "");

    size_t frame_len = 0;
    frame_len += C_FIELD_SZ(struct osw_drv_dot11_frame, header);
    frame_len += C_FIELD_SZ(struct osw_drv_dot11_frame, u.action.category);
    frame_len += C_FIELD_SZ(struct osw_drv_dot11_frame, u.action.u.rrm_meas_req);

    struct osw_rrm_radio_meas_req_entry *radio_meas_req_entry = NULL;
    ds_dlist_foreach(&sta->radio_meas_req_list, radio_meas_req_entry) {
        if (WARN_ON(radio_meas_req_entry->meas_token != 0))
            continue;

        if (radio_meas_req_entry->req.type != OSW_RRM_RADIO_MEAS_REQ_TYPE_BEACON)
            continue;

        frame_len += C_FIELD_SZ(struct osw_drv_dot11_meas_req_ie, h);
        frame_len += C_FIELD_SZ(struct osw_drv_dot11_meas_req_ie, f);
        frame_len += C_FIELD_SZ(struct osw_drv_dot11_meas_req_ie, u.beacon);

        const struct osw_rrm_radio_meas_beacon_req *params = &radio_meas_req_entry->req.u.beacon;
        if (params->ssid.len > 0) {
            frame_len += sizeof(struct osw_drv_dot11_meas_req_ie_subel_ssid);
            frame_len += params->ssid.len;
        }
        frame_len += sizeof(struct osw_drv_dot11_meas_req_ie_subel_bri);
        frame_len += sizeof(struct osw_drv_dot11_meas_req_ie_subel_rep_det);
    }

    return frame_len;
}

static ssize_t
osw_rrm_build_radio_meas_req_frame(struct osw_rrm_sta *sta,
                                   const struct osw_state_sta_info *sta_info,
                                   uint8_t dialog_token,
                                   uint8_t *frame_buf,
                                   size_t frame_buf_size)
{
    ASSERT(sta != NULL, "");
    ASSERT(sta_info != NULL, "");
    ASSERT(frame_buf != NULL, "");
    ASSERT(frame_buf_size > 0, "");

    struct osw_drv_dot11_frame *frame = (struct osw_drv_dot11_frame*) frame_buf;
    const size_t frame_len = osw_rrm_build_radio_meas_req_compute_frame_len(sta);

    if (frame_len > frame_buf_size)
        return -1;

    memset(frame_buf, 0, frame_buf_size);

    struct osw_drv_dot11_frame_header *header = &frame->header;
    header->frame_control = htole16(DOT11_FRAME_CTRL_SUBTYPE_ACTION);
    header->duration = htole16(60);
    header->seq_ctrl = 0;
    memcpy(&header->da, &sta->sta_addr.octet, sizeof(header->da));
    memcpy(&header->sa, &sta_info->vif->drv_state->mac_addr, sizeof(header->sa));
    memcpy(&header->bssid, &sta_info->vif->drv_state->mac_addr, sizeof(header->bssid));

    struct osw_drv_dot11_frame_action *action = &frame->u.action;
    action->category = DOT11_ACTION_CATEGORY_RADIO_MEAS_CODE;

    struct osw_drv_dot11_frame_action_rrm_meas_req *rrm_meas_req = &frame->u.action.u.rrm_meas_req;
    rrm_meas_req->action = DOT11_RRM_MEAS_REQ_IE_ACTION_CODE;
    rrm_meas_req->dialog_token = dialog_token;
    rrm_meas_req->repetitions = 0;

    size_t next_meas_req_offset = 0;
    struct osw_rrm_radio_meas_req_entry *radio_meas_req_entry = NULL;
    ds_dlist_foreach(&sta->radio_meas_req_list, radio_meas_req_entry) {
        if (WARN_ON(radio_meas_req_entry->meas_token != 0))
            continue;
        if (radio_meas_req_entry->req.type != OSW_RRM_RADIO_MEAS_REQ_TYPE_BEACON)
            continue;

        struct osw_drv_dot11_meas_req_ie *meas_req = (struct osw_drv_dot11_meas_req_ie *) (rrm_meas_req->variable + next_meas_req_offset);
        radio_meas_req_entry->meas_token = osw_rrm_sta_gen_meas_token(sta);

        struct osw_drv_dot11_meas_req_ie_header *meas_req_hdr = &meas_req->h;
        meas_req_hdr->tag = DOT11_RRM_MEAS_REQ_IE_ELEM_ID;
        meas_req_hdr->tag_len = 0; /* It'll be set later */

        struct osw_drv_dot11_meas_req_ie_fixed *meas_req_fixed = &meas_req->f;
        meas_req_fixed->token = radio_meas_req_entry->meas_token;
        meas_req_fixed->req_mode = 0;
        meas_req_fixed->req_type = DOT11_RRM_MEAS_REQ_IE_TYPE_BEACON;

        struct osw_drv_dot11_meas_req_beacon *beacon_meas_req = &meas_req->u.beacon;
        beacon_meas_req->op_class = radio_meas_req_entry->req.u.beacon.op_class;
        beacon_meas_req->channel = radio_meas_req_entry->req.u.beacon.channel;
        beacon_meas_req->rand_interval = 0;
        beacon_meas_req->duration = htole16(OSW_RRM_RADIO_MEAS_REQ_DEFAULT_DURATION);
        beacon_meas_req->meas_mode = DOT11_RRM_MEAS_REQ_IE_MEAS_REQ_BEACON_MODE_ACTIVE;

        size_t subel_offset = 0;

        const struct osw_rrm_radio_meas_beacon_req *params = &radio_meas_req_entry->req.u.beacon;
        if (params->ssid.len > 0) {
            struct osw_drv_dot11_meas_req_ie_subel_ssid *ssid_subel = (struct osw_drv_dot11_meas_req_ie_subel_ssid*) (beacon_meas_req->variable + subel_offset);
            ssid_subel->tag = DOT11_RRM_MEAS_REQ_IE_MEAS_REQ_BEACON_SUBEL_ID_SSID;
            ssid_subel->tag_len = params->ssid.len;
            memcpy(ssid_subel->ssid, params->ssid.buf, params->ssid.len);
            subel_offset += sizeof(struct osw_drv_dot11_meas_req_ie_subel_ssid) + params->ssid.len;
        }

        struct osw_drv_dot11_meas_req_ie_subel_bri *bri_subel = (struct osw_drv_dot11_meas_req_ie_subel_bri*) (beacon_meas_req->variable + subel_offset);
        bri_subel->tag = DOT11_RRM_MEAS_REQ_IE_MEAS_REQ_BEACON_SUBEL_ID_BEACON_REPORTING;
        bri_subel->tag_len = 0x02;
        bri_subel->reporting_condition = 0; /* report to be issued after each measurement */
        bri_subel->thr_offs = 0;
        subel_offset += sizeof(struct osw_drv_dot11_meas_req_ie_subel_bri);

        struct osw_drv_dot11_meas_req_ie_subel_rep_det *rep_det_subel = (struct osw_drv_dot11_meas_req_ie_subel_rep_det*) (beacon_meas_req->variable + subel_offset);
        rep_det_subel->tag = DOT11_RRM_MEAS_REQ_IE_MEAS_REQ_BEACON_SUBEL_ID_REPORTING_DETAIL;
        rep_det_subel->tag_len = 0x01;
        rep_det_subel->reporting_detail = 0; /* no fixed length fields or elements */
        subel_offset += sizeof(struct osw_drv_dot11_meas_req_ie_subel_rep_det);

        next_meas_req_offset += sizeof(struct osw_drv_dot11_meas_req_ie_header);
        next_meas_req_offset += sizeof(struct osw_drv_dot11_meas_req_ie_fixed);
        next_meas_req_offset += sizeof(struct osw_drv_dot11_meas_req_beacon);
        next_meas_req_offset += subel_offset;

        meas_req_hdr->tag_len = sizeof(struct osw_drv_dot11_meas_req_ie_fixed) + sizeof(struct osw_drv_dot11_meas_req_beacon) + subel_offset;
    }

    return frame_len;
}

static void
osw_rrm_sta_tx_work_timer_cb(struct osw_timer *timer)
{
    struct osw_rrm_sta *sta = (struct osw_rrm_sta*) container_of(timer, struct osw_rrm_sta, tx_work_timer);
    struct osw_rrm *rrm = sta->rrm;
    const struct osw_state_sta_info *sta_info = rrm->sta_lookup_newest_wrapper_fn(&sta->sta_addr);
    if (sta_info == NULL)
        return;

    if (sta->dialog_token != 0) {
        LOGD("osw: rrm: sta_addr: "OSW_HWADDR_FMT" cannot schedule request frame tx, prev frame dialog_token: %u is still in transit",
             OSW_HWADDR_ARG(&sta->sta_addr), sta->dialog_token);
        return;
    }

    struct osw_rrm_radio_meas_req_entry *radio_meas_req_entry = NULL;
    ds_dlist_foreach(&sta->radio_meas_req_list, radio_meas_req_entry) {
        if (radio_meas_req_entry->meas_token != 0) {
            LOGW("osw: rrm: sta_addr: "OSW_HWADDR_FMT" cannot schedule request frame tx, orphaned meas req meas_token: %u found",
                 OSW_HWADDR_ARG(&sta->sta_addr), radio_meas_req_entry->meas_token);
            return;
        }

        break;
    }

    if (radio_meas_req_entry == NULL)
        return;

    if (sta->throttle != NULL) {
        uint64_t next_at_nsec = 0;
        const bool result = osw_throttle_tap(sta->throttle, &next_at_nsec);
        if (result == false) {
            osw_timer_arm_at_nsec(&sta->tx_work_timer, next_at_nsec);
            LOGD("osw: rrm: sta_addr: "OSW_HWADDR_FMT" cannot schedule request frame tx, postponed bythrottle", OSW_HWADDR_ARG(&sta->sta_addr));
            return;
        }
    }

    const uint8_t dialog_token = osw_rrm_sta_gen_dialog_token(sta);
    uint8_t frame_buf [OSW_DRV_FRAME_TX_DESC_BUF_SIZE];
    const ssize_t frame_len = osw_rrm_build_radio_meas_req_frame(sta, sta_info, dialog_token, frame_buf, sizeof(frame_buf));
    if (frame_len <= 0) {
        LOGW(OSW_RRM_PREFIX"cannot schedule request frame tx, failed to compose frame", OSW_HWADDR_ARG(&sta->sta_addr));
        return;
    }

    sta->dialog_token = dialog_token;
    osw_drv_frame_tx_desc_set_frame(sta->frame_tx_desc, frame_buf, frame_len);
    rrm->mux_frame_tx_schedule_wrapper_fn(sta_info->vif->phy->phy_name, sta_info->vif->vif_name, sta->frame_tx_desc);

    const uint64_t timeout_tstamp_nsec = osw_time_mono_clk() + OSW_TIME_SEC(OSW_RRM_RADIO_MEAS_REQ_TIMEOUT_SEC);
    osw_timer_arm_at_nsec(&sta->req_timeout_timer, timeout_tstamp_nsec);

    LOGD("osw: rrm: sta_addr: "OSW_HWADDR_FMT" scheduling request frame dialog_token: %u tx", OSW_HWADDR_ARG(&sta->sta_addr), dialog_token);
}

static void
osw_rrm_sta_req_timeout_timer_cb(struct osw_timer *timer)
{
    struct osw_rrm_sta *sta = (struct osw_rrm_sta*) container_of(timer, struct osw_rrm_sta, req_timeout_timer);

    if (WARN_ON(sta->dialog_token == 0))
        return;

    struct osw_rrm_radio_meas_req_entry *radio_meas_req = NULL;
    ds_dlist_foreach(&sta->radio_meas_req_list, radio_meas_req) {
        if (radio_meas_req->meas_token == 0)
            continue;

        ds_dlist_remove(&sta->radio_meas_req_list, radio_meas_req);
        osw_rrm_radio_meas_req_entry_free(radio_meas_req, sta->dialog_token, OSW_RRM_RADIO_STATUS_TIMEOUT);
    }

    sta->dialog_token = 0;
    osw_timer_arm_at_nsec(&sta->tx_work_timer, 0);
}

static void
osw_rrm_drv_frame_tx_result_cb(struct osw_drv_frame_tx_desc *tx_desc,
                               enum osw_frame_tx_result result,
                               void *caller_priv)
{
    ASSERT(caller_priv != NULL, "");

    struct osw_rrm_sta *sta = caller_priv;
    if (sta->dialog_token == 0)
        return;

    LOGD("osw: rrm: sta_addr: "OSW_HWADDR_FMT" request frame dialog_token: %u sent", OSW_HWADDR_ARG(&sta->sta_addr), sta->dialog_token);

    struct osw_rrm_radio_meas_req_entry *meas_req = NULL;
    ds_dlist_foreach(&sta->radio_meas_req_list, meas_req) {
        if (meas_req->meas_token == 0)
            continue;

        struct osw_rrm_radio_meas_req_entry_desc *entry_desc = NULL;
        ds_dlist_foreach(&meas_req->desc_list, entry_desc) {
            struct osw_rrm_desc_observer *observer = entry_desc->desc->observer;
            if (observer == NULL)
                continue;

            if (observer->radio_meas_req_status_fn == NULL)
                continue;

            struct osw_rrm_desc *desc = entry_desc->desc;
            if (desc == NULL)
                continue;;

            void *priv = desc->priv;
            const uint8_t *dialog_token_ptr = &sta->dialog_token;
            const uint8_t *meas_token_ptr = &meas_req->meas_token;
            observer->radio_meas_req_status_fn(priv, OSW_RRM_RADIO_STATUS_SENT,
                                               dialog_token_ptr, meas_token_ptr,
                                               &meas_req->req);
        }
    }
}

static struct osw_rrm_sta*
osw_rrm_get_sta(struct osw_rrm *rrm,
                const struct osw_hwaddr *sta_addr)
{
    ASSERT(rrm != NULL, "");
    ASSERT(sta_addr != NULL, "");

    struct osw_rrm_sta *sta = ds_tree_find(&rrm->sta_tree, sta_addr);

    if (sta != NULL)
        return sta;

    sta = CALLOC(1, sizeof(*sta));
    memcpy(&sta->sta_addr, sta_addr, sizeof(sta->sta_addr));
    sta->rrm = rrm;
    osw_timer_init(&sta->tx_work_timer, osw_rrm_sta_tx_work_timer_cb);
    osw_timer_init(&sta->req_timeout_timer, osw_rrm_sta_req_timeout_timer_cb);
    ds_dlist_init(&sta->desc_list, struct osw_rrm_desc, node);
    ds_dlist_init(&sta->radio_meas_req_list, struct osw_rrm_radio_meas_req_entry, node);
    sta->frame_tx_desc = osw_drv_frame_tx_desc_new(osw_rrm_drv_frame_tx_result_cb, sta);

    ds_tree_insert(&rrm->sta_tree, sta, &sta->sta_addr);

    return sta;
}

static void
osw_rrm_sta_free(struct osw_rrm_sta *sta)
{
    ASSERT(sta != NULL, "");

    ASSERT(ds_dlist_is_empty(&sta->radio_meas_req_list) == true, NULL);

    osw_throttle_free(sta->throttle);
    osw_timer_disarm(&sta->tx_work_timer);
    osw_timer_disarm(&sta->req_timeout_timer);
    osw_drv_frame_tx_desc_free(sta->frame_tx_desc);

    FREE(sta);
}

static bool
osw_rrm_bcn_meas_rpt_subtract_len(size_t *remaining_len,
                                  size_t field_len)
{
    ASSERT(remaining_len != NULL, "");

    if (*remaining_len < field_len)
        return false;

    *remaining_len = *remaining_len - field_len;
    return true;
}

static void
osw_rrm_process_frame(struct osw_rrm *rrm,
                      const uint8_t *data,
                      size_t len)
{
    ASSERT(rrm != NULL, "");
    ASSERT(data != NULL, "");
    ASSERT(len > 0, "");

    const struct osw_drv_dot11_frame *frame = (const struct osw_drv_dot11_frame*) data;
    size_t remaining_len = len;

    /* Header */
    if (osw_rrm_bcn_meas_rpt_subtract_len(&remaining_len, C_FIELD_SZ(struct osw_drv_dot11_frame, header)) == false) {
        LOGT("osw: rrm: dropping frame, too short to be dot11 frame");
        return;
    }

    const struct osw_drv_dot11_frame_header *header = &frame->header;
    struct osw_hwaddr sta_addr;
    memcpy(sta_addr.octet, &header->sa, sizeof(sta_addr.octet));
    const uint16_t frame_type = le16toh(header->frame_control) & DOT11_FRAME_CTRL_TYPE_MASK;
    if (frame_type != DOT11_FRAME_CTRL_TYPE_MGMT) {
        LOGT(OSW_RRM_PREFIX"dropping frame, non-mgmt frame", OSW_HWADDR_ARG(&sta_addr));
        return;
    }

    const uint16_t frame_subtype = le16toh(header->frame_control) & DOT11_FRAME_CTRL_SUBTYPE_MASK;
    if (frame_subtype != DOT11_FRAME_CTRL_SUBTYPE_ACTION) {
        LOGT(OSW_RRM_PREFIX"dropping frame, non-mgmt frame", OSW_HWADDR_ARG(&sta_addr));
        return;
    }

    /* Action */
    if (osw_rrm_bcn_meas_rpt_subtract_len(&remaining_len, C_FIELD_SZ(struct osw_drv_dot11_frame_action, category)) == false) {
        LOGT(OSW_RRM_PREFIX"dropping frame, too short to be action frame", OSW_HWADDR_ARG(&sta_addr));
        return;
    }

    const struct osw_drv_dot11_frame_action *action = &frame->u.action;
    if (action->category != DOT11_ACTION_CATEGORY_RADIO_MEAS_CODE) {
        LOGT(OSW_RRM_PREFIX"dropping frame, not a radio meas category", OSW_HWADDR_ARG(&sta_addr));
        return;
    }

    /* Measurment Report */
    if (osw_rrm_bcn_meas_rpt_subtract_len(&remaining_len, C_FIELD_SZ(struct osw_drv_dot11_frame_action, u.rrm_meas_rep)) == false) {
        LOGT(OSW_RRM_PREFIX"dropping frame, too short to be radio meas rpt frame", OSW_HWADDR_ARG(&sta_addr));
        return;
    }

    const struct osw_drv_dot11_frame_action_rrm_meas_rep *rrm_meas_rep = &frame->u.action.u.rrm_meas_rep;
    if (rrm_meas_rep->action != DOT11_RRM_MEAS_REP_IE_ACTION_CODE) {
        LOGT(OSW_RRM_PREFIX"dropping frame, not a radio meas rpt action", OSW_HWADDR_ARG(&sta_addr));
        return;
    }

    /* Measurment Report IEs (verify) */
    const struct element *meas_rpt_ie_elem = NULL;
    for_each_ie(meas_rpt_ie_elem, &rrm_meas_rep->variable, remaining_len) {
        const size_t min_ie_len = C_FIELD_SZ(struct osw_drv_dot11_meas_rep_ie, f);
        if (meas_rpt_ie_elem->datalen < min_ie_len) {
            LOGT(OSW_RRM_PREFIX"dropping report, radio meas rpt element too short to hold meas rep ie header", OSW_HWADDR_ARG(&sta_addr));
            return;
        }

        const struct osw_drv_dot11_meas_rep_ie *meas_rpt_ie = (const struct osw_drv_dot11_meas_rep_ie*) meas_rpt_ie_elem;
        const struct osw_drv_dot11_meas_rep_ie_header *meas_rpt_ie_header = &meas_rpt_ie->h;
        if (meas_rpt_ie_header->tag != DOT11_RRM_MEAS_REP_IE_ELEM_ID) {
            LOGT(OSW_RRM_PREFIX"dropping report, not a radio meas rpt elem", OSW_HWADDR_ARG(&sta_addr));
            continue;
        }

        const struct osw_drv_dot11_meas_rep_ie_fixed *meas_rpt_ie_fixed = &meas_rpt_ie->f;
        if (meas_rpt_ie_fixed->report_type != DOT11_RRM_MEAS_REP_IE_TYPE_BEACON) {
            LOGT(OSW_RRM_PREFIX"dropping report, not a radio meas bcn rpt elem", OSW_HWADDR_ARG(&sta_addr));
            continue;
        }

        if (meas_rpt_ie_fixed->report_mode == 0) {
            const size_t min_ie_len = C_FIELD_SZ(struct osw_drv_dot11_meas_rep_ie, f)
                                    + C_FIELD_SZ(struct osw_drv_dot11_meas_rep_ie, u.beacon);
            if (meas_rpt_ie_elem->datalen <= min_ie_len) {
                LOGT(OSW_RRM_PREFIX"dropping report, radio meas rpt element too short to hold meas rep ie header and bcn meas rpt", OSW_HWADDR_ARG(&sta_addr));
                return;
            }
        }
    }

    /* Measurment Report IEs (process) */
    for_each_ie(meas_rpt_ie_elem, &rrm_meas_rep->variable, remaining_len) {
        const struct osw_drv_dot11_meas_rep_ie *meas_rpt_ie = (const struct osw_drv_dot11_meas_rep_ie*) meas_rpt_ie_elem;
        const struct osw_drv_dot11_meas_rep_ie_fixed *meas_rpt_ie_fixed = &meas_rpt_ie->f;
        const struct osw_drv_dot11_meas_rpt_ie_beacon *meas_rpt_ie_beacon = NULL;
        if (meas_rpt_ie_fixed->report_mode == 0)
            meas_rpt_ie_beacon = &meas_rpt_ie->u.beacon;

        struct osw_rrm_rpt_observer *observer = NULL;
        ds_dlist_foreach(&rrm->rpt_observer_list, observer)
            if (observer->bcn_meas_fn != NULL)
                observer->bcn_meas_fn(observer, header, rrm_meas_rep, meas_rpt_ie_fixed, meas_rpt_ie_beacon);
    }
}

static void
osw_rrm_state_frame_rx_cb(struct osw_state_observer *self,
                          const struct osw_state_vif_info *vif,
                          const uint8_t *data,
                          size_t len)
{
    ASSERT(self != NULL, "");
    ASSERT(data != NULL, "");
    ASSERT(len > 0, "");

    struct osw_rrm *rrm = container_of(self, struct osw_rrm, state_observer);
    osw_rrm_process_frame(rrm, data, len);
}

static void
osw_rrm_radio_rpt_bcn_meas_cb(struct osw_rrm_rpt_observer *observer,
                              const struct osw_drv_dot11_frame_header *frame_header,
                              const struct osw_drv_dot11_frame_action_rrm_meas_rep *rrm_meas_rep,
                              const struct osw_drv_dot11_meas_rep_ie_fixed *meas_rpt_ie_fixed,
                              const struct osw_drv_dot11_meas_rpt_ie_beacon *meas_rpt_ie_beacon)
{
    ASSERT(observer != NULL, "");
    ASSERT(rrm_meas_rep != NULL, "");
    ASSERT(meas_rpt_ie_fixed != NULL, "");

    struct osw_rrm *rrm = container_of(observer, struct osw_rrm, rrm_rpt_observer);

    struct osw_hwaddr sta_addr;
    memcpy(&sta_addr.octet, &frame_header->sa, sizeof(sta_addr.octet));
    struct osw_rrm_sta *sta = ds_tree_find(&rrm->sta_tree, &sta_addr);
    if (sta == NULL) {
        LOGT("osw: rrm: drop bcn meas rpt, unknowsn sta_addr: "OSW_HWADDR_FMT, OSW_HWADDR_ARG(&sta_addr));
        return;
    }

    const uint8_t dialog_token = rrm_meas_rep->dialog_token;
    if (sta->dialog_token != dialog_token) {
        LOGT(OSW_RRM_PREFIX"drop bcn meas rpt, unexcpected dialog_token: %u", OSW_HWADDR_ARG(&sta_addr), dialog_token);
        return;
    }

    const uint8_t meas_token = meas_rpt_ie_fixed->token;
    struct osw_rrm_radio_meas_req_entry *radio_meas_req = NULL;
    ds_dlist_foreach(&sta->radio_meas_req_list, radio_meas_req)
        if (radio_meas_req->meas_token == meas_token)
            break;

    if (radio_meas_req == NULL) {
        LOGT(OSW_RRM_PREFIX"drop bcn meas rpt, unexcpected meas_token: %u", OSW_HWADDR_ARG(&sta_addr), meas_token);
        return;
    }

    ds_dlist_remove(&sta->radio_meas_req_list, radio_meas_req);
    osw_rrm_radio_meas_req_entry_free(radio_meas_req, dialog_token, OSW_RRM_RADIO_STATUS_REPLIED);

    LOGT(OSW_RRM_PREFIX"dialog_token: %u meas_token: %u bcmn meas rpt received", OSW_HWADDR_ARG(&sta_addr), dialog_token, meas_token);

    /*
     * Send another frame if all Measurments Requests (meas tokens) within frame
     * (dialog token) were replied.
     */
    ds_dlist_foreach(&sta->radio_meas_req_list, radio_meas_req)
        if (radio_meas_req->meas_token != 0)
            return;

    LOGT(OSW_RRM_PREFIX"dialog_token: %u all meas reqs completed", dialog_token, OSW_HWADDR_ARG(&sta_addr));

    sta->dialog_token = 0;
    osw_timer_arm_at_nsec(&sta->tx_work_timer, 0);
    osw_timer_disarm(&sta->req_timeout_timer);
}

static void
osw_rrm_init(struct osw_rrm *rrm)
{
    ASSERT(rrm != NULL, "");

    memset(rrm, 0, sizeof(*rrm));
    ds_tree_init(&rrm->sta_tree, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_rrm_sta, node);
    rrm->state_observer.vif_frame_rx_fn = osw_rrm_state_frame_rx_cb;
    osw_state_register_observer(&rrm->state_observer);
    rrm->rrm_rpt_observer.bcn_meas_fn = osw_rrm_radio_rpt_bcn_meas_cb;
    ds_dlist_init(&rrm->rpt_observer_list, struct osw_rrm_rpt_observer, node);
    rrm->sta_lookup_newest_wrapper_fn = osw_state_sta_lookup_newest;
    rrm->mux_frame_tx_schedule_wrapper_fn = osw_mux_frame_tx_schedule;
}

struct osw_rrm_desc*
osw_rrm_get_desc(struct osw_rrm *rrm,
                 const struct osw_hwaddr *sta_addr,
                 struct osw_rrm_desc_observer *observer,
                 void *priv)
{
    ASSERT(rrm != NULL, "");
    ASSERT(sta_addr != NULL, "");
    ASSERT(observer != NULL, "");

    struct osw_rrm_sta *sta = osw_rrm_get_sta(rrm, sta_addr);
    struct osw_rrm_desc *desc = CALLOC(1, sizeof(*desc));

    desc->sta = sta;
    desc->observer = observer;
    desc->priv = priv;

    ds_dlist_insert_tail(&sta->desc_list, desc);

    return desc;
}

bool
osw_rrm_desc_schedule_radio_meas_req(struct osw_rrm_desc *desc,
                                     const struct osw_rrm_radio_meas_req *radio_meas_req)
{
    ASSERT(desc != NULL, "");
    ASSERT(radio_meas_req != NULL, "");

    struct osw_rrm_sta *sta = desc->sta;
    struct osw_rrm *rrm = sta->rrm;
    const struct osw_state_sta_info *sta_info = rrm->sta_lookup_newest_wrapper_fn(&desc->sta->sta_addr);
    if (sta_info == NULL)
        return false;

    struct osw_rrm_radio_meas_req_entry *radio_meas_req_entry = NULL;
    ds_dlist_foreach(&sta->radio_meas_req_list, radio_meas_req_entry)
        if (memcmp(&radio_meas_req_entry->req, radio_meas_req, sizeof(radio_meas_req_entry->req)) == 0)
            break;

    if (radio_meas_req_entry == NULL) {
        radio_meas_req_entry = CALLOC(1, sizeof(*radio_meas_req_entry));
        memcpy(&radio_meas_req_entry->req, radio_meas_req, sizeof(radio_meas_req_entry->req));
        ds_dlist_init(&radio_meas_req_entry->desc_list, struct osw_rrm_radio_meas_req_entry_desc, node);

        ds_dlist_insert_tail(&sta->radio_meas_req_list, radio_meas_req_entry);
    }

    struct osw_rrm_radio_meas_req_entry_desc *entry_desc = NULL;
    ds_dlist_foreach(&radio_meas_req_entry->desc_list, entry_desc)
        if (entry_desc->desc == desc)
            break;

    if (entry_desc == NULL) {
        entry_desc = CALLOC(1, sizeof(*entry_desc));
        entry_desc->desc = desc;
        ds_dlist_insert_tail(&radio_meas_req_entry->desc_list, entry_desc);
    }

    osw_timer_arm_at_nsec(&sta->tx_work_timer, 0);

    return true;
}

void
osw_rrm_desc_free(struct osw_rrm_desc *desc)
{
    if (desc == NULL)
        return;

    ASSERT(desc->sta != NULL, NULL);

    struct osw_rrm_sta *sta = desc->sta;

    struct osw_rrm_radio_meas_req_entry *radio_meas_req_entry = NULL;
    struct osw_rrm_radio_meas_req_entry *tmp_radio_meas_req_entry = NULL;
    ds_dlist_foreach_safe(&sta->radio_meas_req_list, radio_meas_req_entry, tmp_radio_meas_req_entry) {
        struct osw_rrm_radio_meas_req_entry_desc *entry_desc = NULL;
        struct osw_rrm_radio_meas_req_entry_desc *tmp_entry_desc;
        ds_dlist_foreach_safe(&radio_meas_req_entry->desc_list, entry_desc, tmp_entry_desc)
            if (entry_desc->desc == desc)
                break;

        if (entry_desc != NULL) {
            ds_dlist_remove(&radio_meas_req_entry->desc_list, entry_desc);
            FREE(entry_desc);
        }

        if (ds_dlist_is_empty(&radio_meas_req_entry->desc_list) == true) {
            ds_dlist_remove(&sta->radio_meas_req_list, radio_meas_req_entry);
            FREE(radio_meas_req_entry);
        }
    }

    ds_dlist_remove(&sta->desc_list, desc);
    FREE(desc);

    if (ds_dlist_is_empty(&sta->desc_list) == true) {
        struct osw_rrm *rrm = sta->rrm;
        ds_tree_remove(&rrm->sta_tree, &sta->sta_addr);
        osw_rrm_sta_free(sta);
    }
}

void
osw_rrm_register_rpt_observer(struct osw_rrm *rrm,
                              struct osw_rrm_rpt_observer *observer)
{
    ASSERT(rrm != NULL, "");
    ASSERT(observer != NULL, "");
    ds_dlist_insert_tail(&rrm->rpt_observer_list, observer);
}

void
osw_rrm_unregister_rpt_observer(struct osw_rrm *rrm,
                                struct osw_rrm_rpt_observer *observer)
{
    ASSERT(rrm != NULL, "");
    ASSERT(observer != NULL, "");
    ds_dlist_remove(&rrm->rpt_observer_list, observer);
}

OSW_MODULE(osw_rrm)
{
    OSW_MODULE_LOAD(osw_state);
    static struct osw_rrm rrm;
    osw_rrm_init(&rrm);
    osw_rrm_register_rpt_observer(&rrm, &rrm.rrm_rpt_observer);
    return &rrm;
}

#include "osw_rrm_ut.c"
