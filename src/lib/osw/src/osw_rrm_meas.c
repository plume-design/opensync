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
#include <ev.h>
#include <log.h>
#include <const.h>
#include <util.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <memutil.h>
#include <osw_types.h>
#include <osw_timer.h>
#include <osw_time.h>
#include <osw_drv_common.h>
#include <osw_drv_mediator.h>
#include <osw_drv.h>
#include <osw_mux.h>
#include <osw_throttle.h>
#include <osw_state.h>
#include <osw_rrm_meas.h>
#include <osw_state.h>
#include <osw_util.h>
#include <osw_token.h>

typedef bool
osw_rrm_meas_mux_frame_tx_schedule_fn_t(const char *phy_name,
                                        const char *vif_name,
                                        struct osw_drv_frame_tx_desc *desc);

typedef const struct osw_state_vif_info *
osw_rrm_meas_vif_lookup_fn_t(const char *phy_name,
                             const char *vif_name);

enum osw_rrm_meas_desc_state {
    OSW_RRM_MEAS_DESC_STATE_EMPTY = 0,
    OSW_RRM_MEAS_DESC_STATE_PENDING,
    OSW_RRM_MEAS_DESC_STATE_IN_TRANSIT,
};

struct osw_rrm_meas_desc {
    struct osw_rrm_meas_sta *sta;
    const struct osw_rrm_meas_sta_observer *observer;
    enum osw_rrm_meas_desc_state state;
    uint8_t frame_buf [OSW_DRV_FRAME_TX_DESC_BUF_SIZE];
    ssize_t frame_len;
    const char *vif_name;
    const char *phy_name;
    int dialog_token;
    struct osw_token_pool_reference *pool_ref;
    osw_rrm_meas_mux_frame_tx_schedule_fn_t *mux_frame_tx_schedule_fn;
    osw_rrm_meas_vif_lookup_fn_t *vif_lookup_fn;

    struct ds_dlist_node node;
};

struct osw_rrm_meas_sta {
    struct ds_tree *owner;
    struct osw_hwaddr mac_addr;
    struct osw_throttle *throttle;
    struct osw_timer work_timer;
    struct osw_drv_frame_tx_desc *frame_tx_desc;
    struct osw_rrm_meas_desc *desc_in_flight;
    bool in_flight;
    const struct osw_state_sta_info *sta_info;
    struct ds_dlist desc_list;
    struct ds_tree neigh_tree;

    struct ds_tree_node node;
};

/* FIXME: combine rrm and btm into common action frame module */

/* FIXME: This should not be global */
static struct ds_tree g_sta_tree = DS_TREE_INIT((ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_rrm_meas_sta, node);

/* FIXME: forward decl should be avoided */
static struct osw_rrm_meas_sta *
osw_rrm_meas_get_sta(const struct osw_hwaddr *sta_addr);

static void
osw_rrm_meas_desc_reset(struct osw_rrm_meas_desc *desc)
{
    ASSERT(desc != NULL, "");
    ASSERT(desc->sta != NULL, "");

    if (desc->sta->desc_in_flight == desc) {
        desc->sta->desc_in_flight = NULL;
    }

    desc->state = OSW_RRM_MEAS_DESC_STATE_EMPTY;
    memset(&desc->frame_buf, 0, sizeof(desc->frame_buf));
    desc->frame_len = 0;

    if ((desc->pool_ref != NULL) &&
        (desc->dialog_token != OSW_TOKEN_INVALID)) {
        osw_token_pool_free_token(desc->pool_ref,
                                  desc->dialog_token);
        desc->dialog_token = OSW_TOKEN_INVALID;
    }
}

static ssize_t
osw_rrm_meas_build_frame(const struct osw_rrm_meas_req_params *req_params,
                         const struct osw_hwaddr *sta_addr,
                         const struct osw_hwaddr *bssid,
                         uint8_t dialog_token,
                         uint8_t *frame_buf,
                         size_t frame_buf_size)
{
    ASSERT(req_params != NULL, "");
    ASSERT(frame_buf != NULL, "");
    ASSERT(frame_buf_size > 0, "");

    struct osw_drv_dot11_frame *frame = (struct osw_drv_dot11_frame*) frame_buf;
    size_t frame_len = 0;
    struct osw_drv_dot11_meas_req_ie *meas_req_ie;
    struct osw_drv_dot11_meas_req_ie_subel_ssid *subel_ssid;
    struct osw_drv_dot11_meas_req_ie_subel_bri *subel_bri;
    struct osw_drv_dot11_meas_req_ie_subel_rep_det *subel_rep_det;

    frame_len += offsetof(struct osw_drv_dot11_frame, u.action.u.rrm_meas_req);
    frame_len += C_FIELD_SZ(struct osw_drv_dot11_frame, u.action.u.rrm_meas_req);
    frame_len += sizeof(struct osw_drv_dot11_meas_req_ie);
    frame_len += sizeof(struct osw_drv_dot11_meas_req_ie_subel_ssid);
    frame_len += (uint8_t)req_params->ssid.len;
    frame_len += sizeof(struct osw_drv_dot11_meas_req_ie_subel_bri);
    frame_len += sizeof(struct osw_drv_dot11_meas_req_ie_subel_rep_det);

    if (frame_len > frame_buf_size) {
        LOGW("osw: rrm_meas: [sta: "OSW_HWADDR_FMT"] failed to build frame, to small buffer (len: %zu size: %zu)",
             OSW_HWADDR_ARG(sta_addr), frame_len, frame_buf_size);
        return -1;
    }

    frame->header.frame_control = htole16(0xd0);         // action frame
    frame->header.duration      = htole16((uint16_t)60); // FIXME proper duration calculation if needed
    frame->header.seq_ctrl      = htole16(0x0000);
    memcpy(&frame->header.da, &sta_addr->octet, sizeof(frame->header.da));
    /* If bssid is not provided leave it empty for
     * lower layers to handle */
    if (bssid != NULL) {
        memcpy(&frame->header.sa, &bssid->octet, sizeof(frame->header.sa));
        memcpy(&frame->header.bssid, &bssid->octet, sizeof(frame->header.bssid));
    }

    frame->u.action.category = DOT11_ACTION_CATEGORY_RADIO_MEAS_CODE;

    frame->u.action.u.rrm_meas_req.action       = DOT11_RRM_MEAS_REQ_IE_ACTION_CODE;
    frame->u.action.u.rrm_meas_req.dialog_token = dialog_token;
    frame->u.action.u.rrm_meas_req.repetitions  = 0x0000;

    meas_req_ie                = (struct osw_drv_dot11_meas_req_ie*) frame->u.action.u.rrm_meas_req.variable;
    meas_req_ie->h.tag           = 0x26; // measurement request
    meas_req_ie->f.token         = 0x01; // FIXME
    meas_req_ie->f.req_mode      = 0x00;
    meas_req_ie->f.req_type      = 0x05; // beacon request
    meas_req_ie->u.beacon.op_class      = req_params->op_class;
    meas_req_ie->u.beacon.channel       = req_params->channel;
    meas_req_ie->u.beacon.rand_interval = htole16(0x0000);
    meas_req_ie->u.beacon.duration      = htole16(0x0064);
    meas_req_ie->u.beacon.meas_mode     = 0x01; // active
    memcpy(meas_req_ie->u.beacon.bssid, &req_params->bssid.octet, OSW_HWADDR_LEN);

    subel_ssid          = (struct osw_drv_dot11_meas_req_ie_subel_ssid*) meas_req_ie->u.beacon.variable;
    subel_ssid->tag     = 0x00;
    subel_ssid->tag_len = (uint8_t)req_params->ssid.len;
    if (subel_ssid->tag_len) {
        memcpy(subel_ssid->ssid, req_params->ssid.buf, subel_ssid->tag_len);
    }

    subel_bri                       = (struct osw_drv_dot11_meas_req_ie_subel_bri*) ((uint8_t*)subel_ssid + 2 + subel_ssid->tag_len);
    subel_bri->tag                  = 0x01;
    subel_bri->tag_len              = 0x02;
    subel_bri->reporting_condition  = 0x00; // report to be issued after each measurement
    subel_bri->thr_offs             = 0x00;

    subel_rep_det                   = (struct osw_drv_dot11_meas_req_ie_subel_rep_det*) ((uint8_t*)subel_bri + 2 + subel_bri->tag_len);
    subel_rep_det->tag              = 0x02;
    subel_rep_det->tag_len          = 0x01;
    subel_rep_det->reporting_detail = 0x00; // no fixed length fields or elements

    meas_req_ie->h.tag_len = (uint8_t)((uint8_t*)subel_rep_det - (uint8_t*)meas_req_ie) + subel_rep_det->tag_len;

    return frame_len;
}

static size_t
osw_rrm_meas_rep_parse_update_neighs(const void *action_data,
                                     const size_t action_len,
                                     const struct osw_hwaddr *sta_addr)
{
    const uint64_t now_tstamp_nsec = osw_time_mono_clk();
    const struct element *elem;
    const struct osw_drv_dot11_frame_action *action = action_data;
    const void *out_of_bounds = (const uint8_t *)action_data + action_len;
    const void *ies_start = &action->u.rrm_meas_rep.variable[0];
    const uint8_t *ies = ies_start;
    const struct osw_drv_dot11_meas_rep_ie *meas_rep_ie;
    struct osw_rrm_meas_rep_neigh *neigh;
    struct osw_hwaddr bssid;
    struct osw_rrm_meas_sta *sta = osw_rrm_meas_get_sta(sta_addr);
    size_t len = out_of_bounds - ies_start;
    size_t neigh_count = 0;

    if ((const void *)&action->category >= out_of_bounds) return -5;
    if ((const void *)&action->u.rrm_meas_rep.action >= out_of_bounds) return -6;
    if (ies_start >= out_of_bounds) return -7;

    if (action->category != DOT11_ACTION_CATEGORY_RADIO_MEAS_CODE) return -3;
    if (action->u.rrm_meas_rep.action != DOT11_RRM_MEAS_REP_IE_ACTION_CODE) return -4;

    /* FIXME: dialog token: correlate multiple in-flight or timed-out responses */

    for_each_ie(elem, ies, len) {
        if (elem->id == DOT11_RRM_MEAS_REP_IE_ELEM_ID &&
            elem->datalen >= sizeof(*meas_rep_ie)) {

            meas_rep_ie = (const void *)elem;
            /* if refused */
            if ((meas_rep_ie->f.report_mode) & DOT11_RRM_MEAS_REP_IE_REP_MODE_REFUSED_MSK) {
                LOGI("osw: rrm_meas: [sta: "OSW_HWADDR_FMT
                     "] measurement token: %d"
                     " refused RRM request",
                     OSW_HWADDR_ARG(&(sta->mac_addr)),
                     meas_rep_ie->f.token);
                break;
            }

            memcpy(bssid.octet, meas_rep_ie->u.beacon.bssid, OSW_HWADDR_LEN);
            neigh = ds_tree_find(&sta->neigh_tree, &bssid);
            if (neigh == NULL) {
                LOGT("osw: rrm_meas: allocating new neighbor");
                neigh = CALLOC(1, sizeof(*neigh));
                neigh->bssid = bssid;
                ds_tree_insert(&sta->neigh_tree, neigh, &neigh->bssid);
            }

            neigh->op_class = meas_rep_ie->u.beacon.op_class;
            neigh->channel = meas_rep_ie->u.beacon.channel;
            neigh->rcpi = meas_rep_ie->u.beacon.rcpi;
            neigh->scan_start_time = le64toh(meas_rep_ie->u.beacon.start_time);
            neigh->last_update_tstamp_nsec = now_tstamp_nsec;
            ++neigh_count;

            LOGD("osw: rrm_meas: [sta: "OSW_HWADDR_FMT
                 "] measurement token: %d"
                 " reporting neighbor: "OSW_HWADDR_FMT
                 " channel: %d rcpi: %d",
                 OSW_HWADDR_ARG(&(sta->mac_addr)),
                 meas_rep_ie->f.token,
                 OSW_HWADDR_ARG(&(neigh->bssid)),
                 neigh->channel,
                 neigh->rcpi);

            /* for further parsing:
             * struct osw_drv_dot11_meas_rep_ie_subel_rep_frag_id * frag_id;
             * if (elem->datalen >= sizeof(*meas_rep_ie) + sizeof(*frag_id)) {
             *    frag_id = ((void *)elem) + sizeof(*meas_rep_ie);
             * }
             */
        }
    }
    return neigh_count;
}

static bool
osw_rrm_meas_frame_is_action(const struct osw_drv_dot11_frame *frame)
{
    ASSERT(frame != NULL, "");

    const struct osw_drv_dot11_frame_header *header = &frame->header;
    const bool is_mgmt = ((le16toh(header->frame_control) & DOT11_FRAME_CTRL_TYPE_MASK) ==
                           DOT11_FRAME_CTRL_TYPE_MGMT);
    const bool is_action = ((le16toh(header->frame_control) & DOT11_FRAME_CTRL_SUBTYPE_MASK) ==
                             DOT11_FRAME_CTRL_SUBTYPE_ACTION);

    if (is_mgmt == false) return false;
    if (is_action == false) return false;
    return true;
}

static bool
osw_rrm_meas_action_frame_is_rrm_report(const struct osw_drv_dot11_frame_action *action)
{
    ASSERT(action != NULL, "");

    const struct osw_drv_dot11_frame_action_bss_tm_resp *bss_tm_resp = &action->u.bss_tm_resp;
    const bool is_rm = (action->category == DOT11_ACTION_CATEGORY_RADIO_MEAS_CODE);
    const bool is_rrm_report = (bss_tm_resp->action == DOT11_RRM_MEAS_REP_IE_ACTION_CODE);

    if (is_rm == false) return false;
    if (is_rrm_report == false) return false;
    return true;
}

void
osw_rrm_meas_frame_rx_cb(struct osw_state_observer *self,
                         const struct osw_state_vif_info *vif,
                         const uint8_t *data,
                         size_t len)
{
    if (WARN_ON(data == NULL)) return;
    if (WARN_ON(vif == NULL)) return;
    if (WARN_ON(vif->vif_name == NULL)) return;

    /* filter out rrm report frames and validate data length */
    const struct osw_drv_dot11_frame *frame = (const struct osw_drv_dot11_frame *)data;
    const size_t dot11_header_len = sizeof(frame->header);
    if (WARN_ON(len < dot11_header_len)) return;

    const bool is_action = osw_rrm_meas_frame_is_action((const struct osw_drv_dot11_frame *) data);
    if (is_action == false) return;

    const struct osw_drv_dot11_frame_action *action = &frame->u.action;
    const size_t dot11_action_len = sizeof(action->category);
    if (WARN_ON(len < dot11_header_len + dot11_action_len)) return;

    const bool is_rrm_report = osw_rrm_meas_action_frame_is_rrm_report(action);
    if (is_rrm_report == false) return;

    const struct osw_drv_dot11_frame_action_rrm_meas_rep *rrm_meas_rep = &action->u.rrm_meas_rep;
    const size_t dot11_rrm_meas_rep_min_len = sizeof(*rrm_meas_rep);
    if (WARN_ON(len < dot11_header_len + dot11_action_len + dot11_rrm_meas_rep_min_len)) return;

    const struct osw_drv_dot11_frame_header *header = &frame->header;
    const uint8_t *sa = header->sa;
    struct osw_hwaddr sta_addr;
    memcpy(sta_addr.octet, sa, OSW_HWADDR_LEN);
    LOGI("osw: rrm_meas_frame_rx_cb: received rrm report,"
         " vif_name: %s"
         " sta_addr: "OSW_HWADDR_FMT,
         vif->vif_name,
         OSW_HWADDR_ARG(&sta_addr));

    const uint8_t *ies = rrm_meas_rep->variable;
    const size_t ies_len = data + len - ies;
    osw_rrm_meas_rep_parse_update_neighs(ies,
                                         ies_len,
                                         &sta_addr);
}

static void
osw_rrm_meas_sta_schedule_work_at(struct osw_rrm_meas_sta *rrm_meas_sta, uint64_t timestamp)
{
    ASSERT(rrm_meas_sta != NULL, "");
    osw_timer_arm_at_nsec(&rrm_meas_sta->work_timer, timestamp);
}

static void
osw_rrm_meas_sta_try_req_tx(struct osw_rrm_meas_sta *rrm_meas_sta)
{
    ASSERT(rrm_meas_sta != NULL, "");

    const struct osw_drv_dot11_frame *frame = NULL;
    struct osw_rrm_meas_desc *desc = NULL;

    if (ds_dlist_is_empty(&rrm_meas_sta->desc_list) == true) {
        goto cease_tx_attempt;
    }

    ds_dlist_foreach(&rrm_meas_sta->desc_list, desc)
    {
        frame = (const struct osw_drv_dot11_frame*) &desc->frame_buf;
        switch (desc->state) {
            case OSW_RRM_MEAS_DESC_STATE_PENDING:
                /* just continue */
                break;
            case OSW_RRM_MEAS_DESC_STATE_IN_TRANSIT:
                LOGT("osw: rrm_meas: [sta: "OSW_HWADDR_FMT" dialog_token: %u] req already in transit",
                     OSW_HWADDR_ARG(&rrm_meas_sta->mac_addr), frame->u.action.u.rrm_meas_req.dialog_token);
                goto cease_tx_attempt;
            case OSW_RRM_MEAS_DESC_STATE_EMPTY:
                LOGT("osw: rrm_meas: [sta: "OSW_HWADDR_FMT" dialog_token: %u] cannot tx empty req",
                     OSW_HWADDR_ARG(&rrm_meas_sta->mac_addr), frame->u.action.u.rrm_meas_req.dialog_token);
                goto cease_tx_attempt;
        }

        /* This is checked _after_ head of desc_list to allow
         * differentiating it in logs. This is a corner case
         * that can happen if desc is freed while it was in
         * progress.
         */
        if (rrm_meas_sta->desc_in_flight != NULL) {
            frame = (const struct osw_drv_dot11_frame*) &rrm_meas_sta->desc_in_flight->frame_buf;
            LOGT("osw: rrm_meas: [sta: "OSW_HWADDR_FMT" dialog_token: %u] req already in transit (old)",
                 OSW_HWADDR_ARG(&rrm_meas_sta->mac_addr), frame->u.action.u.rrm_meas_req.dialog_token);
            goto cease_tx_attempt;
        }

        if (rrm_meas_sta->throttle != NULL) {
            uint64_t next_at_nsec;
            bool result;

            result = osw_throttle_tap(rrm_meas_sta->throttle, &next_at_nsec);
            if (result == false) {
                osw_rrm_meas_sta_schedule_work_at(rrm_meas_sta, next_at_nsec);
                LOGT("osw: rrm_meas: [sta: "OSW_HWADDR_FMT"] cease req tx attempt due to throttle condition",
                     OSW_HWADDR_ARG(&rrm_meas_sta->mac_addr));
                goto cease_tx_attempt;
            }
        }

        desc->state = OSW_RRM_MEAS_DESC_STATE_IN_TRANSIT;

        osw_drv_frame_tx_desc_set_frame(rrm_meas_sta->frame_tx_desc, desc->frame_buf, desc->frame_len);
        if (desc->mux_frame_tx_schedule_fn == NULL) {
            LOGE("osw: rrm_meas: no mux to send frame to");
            goto cease_tx_attempt;
        }

        if (desc->phy_name == NULL || desc->vif_name == NULL) {
            LOGE("osw: rrm_meas: phy_name or vif_name not provided - ceasing tx");
        } else {
            rrm_meas_sta->desc_in_flight = desc;
            rrm_meas_sta->in_flight = true;
            desc->mux_frame_tx_schedule_fn(desc->phy_name, desc->vif_name, rrm_meas_sta->frame_tx_desc);
            LOGD("osw: rrm_meas: [sta: "OSW_HWADDR_FMT" dialog_token: %u] req was passed to drv",
                 OSW_HWADDR_ARG(&rrm_meas_sta->mac_addr), frame->u.action.u.rrm_meas_req.dialog_token);
        }
        return;

cease_tx_attempt:
        LOGD("osw: rrm_meas: [sta: "OSW_HWADDR_FMT"] req tx attempt was ceased", OSW_HWADDR_ARG(&rrm_meas_sta->mac_addr));
    }
}

static void
osw_rrm_meas_sta_work_timer_cb(struct osw_timer *timer)
{
    struct osw_rrm_meas_sta *sta = (struct osw_rrm_meas_sta*) container_of(timer, struct osw_rrm_meas_sta, work_timer);
    osw_rrm_meas_sta_try_req_tx(sta);
}

static void
osw_rrm_meas_drv_frame_tx_result_cb(struct osw_drv_frame_tx_desc *tx_desc,
                                    enum osw_frame_tx_result result,
                                    void *caller_priv)
{
    const struct osw_drv_dot11_frame *rrm_meas_frame = NULL;
    struct osw_rrm_meas_sta *sta = (struct osw_rrm_meas_sta *) caller_priv;
    struct osw_rrm_meas_desc *rrm_meas_desc = sta->desc_in_flight;

    const bool unexpected = (sta->in_flight == false);
    if (WARN_ON(unexpected))
        goto ignore_tx_report;

    sta->in_flight = false;

    const bool was_freed_while_in_transit = (rrm_meas_desc == NULL);
    if (was_freed_while_in_transit)
        goto ignore_tx_report;

    const bool invalid_desc_state = (rrm_meas_desc->state != OSW_RRM_MEAS_DESC_STATE_IN_TRANSIT);
    if (WARN_ON(invalid_desc_state))
        goto ignore_tx_report;

    rrm_meas_frame = (const struct osw_drv_dot11_frame*) &rrm_meas_desc->frame_buf;

    LOGD("osw: rrm_meas: [sta: "OSW_HWADDR_FMT" dialog token: %u] drv reported req tx result: %s",
         OSW_HWADDR_ARG(&sta->mac_addr), rrm_meas_frame->u.action.u.rrm_meas_req.dialog_token,
         osw_frame_tx_result_to_cstr(result));

    switch (result) {
        case OSW_FRAME_TX_RESULT_SUBMITTED:
            if (rrm_meas_desc->observer != NULL &&
                rrm_meas_desc->observer->req_tx_complete_fn != NULL)
                rrm_meas_desc->observer->req_tx_complete_fn(rrm_meas_desc->observer);
            break;
        case OSW_FRAME_TX_RESULT_FAILED:
        case OSW_FRAME_TX_RESULT_DROPPED:
            if (rrm_meas_desc->observer != NULL &&
                rrm_meas_desc->observer->req_tx_error_fn != NULL)
                rrm_meas_desc->observer->req_tx_error_fn(rrm_meas_desc->observer);
            break;
    }

    osw_rrm_meas_desc_reset(rrm_meas_desc);
    return;

ignore_tx_report:
        LOGD("osw: rrm_meas: [sta: "OSW_HWADDR_FMT"] ignored drv req tx report result: %s",
             OSW_HWADDR_ARG(&sta->mac_addr), osw_frame_tx_result_to_cstr(result));
}

static struct osw_rrm_meas_sta *
osw_rrm_meas_get_sta(const struct osw_hwaddr *sta_addr)
{
    ASSERT(sta_addr != NULL, "");

    struct osw_rrm_meas_sta *sta = ds_tree_find(&g_sta_tree, sta_addr);

    if (sta != NULL)
        return sta;

    sta = CALLOC(1, sizeof(*sta));
    sta->owner = &g_sta_tree;
    memcpy(&sta->mac_addr, sta_addr, sizeof(sta->mac_addr));
    osw_timer_init(&sta->work_timer, osw_rrm_meas_sta_work_timer_cb);
    sta->frame_tx_desc = osw_drv_frame_tx_desc_new(osw_rrm_meas_drv_frame_tx_result_cb, sta);
    sta->sta_info = osw_state_sta_lookup_newest(&sta->mac_addr);
    ds_dlist_init(&sta->desc_list, struct osw_rrm_meas_desc, node);
    ds_tree_init(&sta->neigh_tree, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_rrm_meas_rep_neigh, node);

    ds_tree_insert(sta->owner, sta, &sta->mac_addr);

    return sta;
}

static void
osw_rrm_meas_rep_neigh_free(struct osw_rrm_meas_rep_neigh *neigh)
{
    ASSERT(neigh != NULL, "");
    FREE(neigh);
}

static void
osw_rrm_meas_sta_free(struct osw_rrm_meas_sta *sta)
{
    ASSERT(sta != NULL, "");

    struct osw_rrm_meas_rep_neigh *neigh;

    osw_throttle_free(sta->throttle);
    osw_timer_disarm(&sta->work_timer);
    osw_drv_frame_tx_desc_free(sta->frame_tx_desc);

    while ((neigh = ds_tree_remove_head(&sta->neigh_tree)) != NULL) {
        osw_rrm_meas_rep_neigh_free(neigh);
    }

    ds_tree_remove(sta->owner, sta);
    FREE(sta);
}

struct osw_rrm_meas_desc *
osw_rrm_meas_get_desc(const struct osw_hwaddr *sta_addr,
                      const struct osw_rrm_meas_sta_observer *observer,
                      const char *phy_name,
                      const char *vif_name)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(observer != NULL, "");
    ASSERT(phy_name != NULL, "");
    ASSERT(vif_name != NULL, "");

    struct osw_rrm_meas_sta *sta = osw_rrm_meas_get_sta(sta_addr);
    struct osw_rrm_meas_desc *desc = CALLOC(1, sizeof(*desc));

    desc->sta = sta;
    desc->observer = observer;
    desc->vif_name = STRDUP(vif_name);
    desc->phy_name = STRDUP(phy_name);
    desc->mux_frame_tx_schedule_fn = osw_mux_frame_tx_schedule;
    desc->vif_lookup_fn = osw_state_vif_lookup;

    struct osw_ifname osw_vif_name;
    STRSCPY_WARN(osw_vif_name.buf, vif_name);
    desc->pool_ref = osw_token_pool_ref_get(&osw_vif_name,
                                            sta_addr);

    ds_dlist_insert_tail(&sta->desc_list, desc);

    return desc;
}

static bool
osw_rrm_meas_desc_can_set_req_params(struct osw_rrm_meas_desc *desc)
{
    if (WARN_ON(desc->sta == NULL)) return false;
    if (desc->sta->desc_in_flight != NULL) return false;
    if (desc->sta->in_flight) return false;

    switch (desc->state) {
        case OSW_RRM_MEAS_DESC_STATE_PENDING:
            return true;
        case OSW_RRM_MEAS_DESC_STATE_IN_TRANSIT:
            return false;
        case OSW_RRM_MEAS_DESC_STATE_EMPTY:
            return true;
    }
    WARN_ON(1);
    return false;
}

bool
osw_rrm_meas_desc_set_req_params(struct osw_rrm_meas_desc *desc,
                                 const struct osw_rrm_meas_req_params *params)
{
    ASSERT(desc != NULL, "");
    ASSERT(desc->vif_lookup_fn != NULL, "");

    const bool can_set = osw_rrm_meas_desc_can_set_req_params(desc);
    const bool cannot_set = !can_set;
    if (cannot_set) {
        return false;
    }

    osw_rrm_meas_desc_reset(desc);

    struct osw_rrm_meas_sta *rrm_meas_sta = desc->sta;
    const struct osw_state_vif_info *vif_info = desc->vif_lookup_fn(desc->phy_name, desc->vif_name);
    if (WARN_ON(vif_info == NULL)) {
        return false;
    }

    const struct osw_hwaddr *bssid = &vif_info->drv_state->mac_addr;
    ds_dlist_remove(&rrm_meas_sta->desc_list, desc);
    ds_dlist_insert_tail(&rrm_meas_sta->desc_list, desc);

    if (params == NULL) {
        return true;
    }

    int dtoken = osw_token_pool_fetch_token(desc->pool_ref);
    if (WARN_ON(dtoken == OSW_TOKEN_INVALID)) dtoken = 0;
    desc->dialog_token = dtoken;

    desc->frame_len = osw_rrm_meas_build_frame(params,
                                               &rrm_meas_sta->mac_addr,
                                               bssid,
                                               desc->dialog_token,
                                               desc->frame_buf,
                                               sizeof(desc->frame_buf));
    if (desc->frame_len < 0) {
        osw_rrm_meas_desc_reset(desc);
        return false;
    }

    desc->state = OSW_RRM_MEAS_DESC_STATE_PENDING;
    osw_rrm_meas_sta_schedule_work_at(rrm_meas_sta, 0);

    return true;
}

struct osw_rrm_meas_sta *
osw_rrm_meas_desc_get_sta(struct osw_rrm_meas_desc *desc)
{
    ASSERT(desc != NULL, "");
    ASSERT(desc->sta != NULL, "");
    return desc->sta;
}

void
osw_rrm_meas_sta_set_throttle(struct osw_rrm_meas_sta *rrm_meas_sta,
                              struct osw_throttle *throttle)
{
    ASSERT(rrm_meas_sta != NULL, "");
    ASSERT(throttle != NULL, "");

    osw_throttle_free(rrm_meas_sta->throttle);
    rrm_meas_sta->throttle = throttle;

    osw_rrm_meas_sta_schedule_work_at(rrm_meas_sta, 0);
}

const struct osw_rrm_meas_rep_neigh *
osw_rrm_meas_get_neigh(const struct osw_hwaddr *sta_addr,
                       const struct osw_hwaddr *bssid)
{
    struct osw_rrm_meas_sta *sta = ds_tree_find(&g_sta_tree, sta_addr);
    if (sta == NULL)
        return NULL;

    return ds_tree_find(&sta->neigh_tree, bssid);
}

void
osw_rrm_meas_desc_free(struct osw_rrm_meas_desc *desc)
{
    if (desc == NULL)
        return;

    struct osw_rrm_meas_sta *rrm_meas_sta = desc->sta;

    osw_rrm_meas_desc_reset(desc);
    ds_dlist_remove(&rrm_meas_sta->desc_list, desc);
    FREE(desc->vif_name);
    FREE(desc->phy_name);
    osw_token_pool_ref_free(desc->pool_ref);
    FREE(desc);

    if (ds_dlist_is_empty(&rrm_meas_sta->desc_list) == true)
        osw_rrm_meas_sta_free(rrm_meas_sta);
}

OSW_MODULE(osw_rrm_meas)
{
    OSW_MODULE_LOAD(osw_token);
    struct osw_state_observer *state_observer = CALLOC(1, sizeof(*state_observer));
    state_observer->name = "osw_rrm_meas";
    state_observer->vif_frame_rx_fn = osw_rrm_meas_frame_rx_cb;
    osw_state_register_observer(state_observer);
    return NULL;
}

#include "osw_rrm_meas_ut.c"
