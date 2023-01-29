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
#include <osw_btm.h>
#include <osw_state.h>

enum osw_btm_desc_state {
    OSW_BTM_DESC_STATE_EMPTY = 0,
    OSW_BTM_DESC_STATE_PENDING,
    OSW_BTM_DESC_STATE_IN_TRANSIT,
};

struct osw_btm_desc {
    struct osw_btm_sta *sta;
    struct osw_btm_sta_observer *observer;
    enum osw_btm_desc_state state;
    uint8_t frame_buf [OSW_DRV_FRAME_TX_DESC_BUF_SIZE];
    ssize_t frame_len;
    uint8_t dialog_token;

    struct ds_dlist_node node;
};

typedef bool
osw_btm_mux_frame_tx_schedule_fn_t(const char *phy_name,
                                   const char *vif_name,
                                   struct osw_drv_frame_tx_desc *desc);

struct osw_btm_sta {
    struct osw_hwaddr mac_addr;
    struct osw_state_observer observer;
    struct osw_throttle *throttle;
    struct osw_timer throttle_timer;
    struct osw_timer work_timer;
    struct osw_drv_frame_tx_desc *frame_tx_desc;
    struct osw_state_observer state_observer;
    const struct osw_state_sta_info *sta_info;
    struct ds_dlist desc_list;
    uint8_t dialog_token_generator;

    osw_btm_mux_frame_tx_schedule_fn_t *mux_frame_tx_schedule_fn;

    struct ds_tree_node node;
};

static struct ds_tree g_sta_tree = DS_TREE_INIT((ds_key_cmp_t*) osw_hwaddr_cmp, struct osw_btm_sta, node);

static uint8_t
osw_btm_desc_generate_dialog_token(struct osw_btm_sta *btm_sta)
{
    ASSERT(btm_sta != NULL, "");
    return btm_sta->dialog_token_generator++;
}

static void
osw_btm_desc_reset(struct osw_btm_desc *desc)
{
    ASSERT(desc != NULL, "");

    desc->state = OSW_BTM_DESC_STATE_EMPTY;
    memset(&desc->frame_buf, 0, sizeof(desc->frame_buf));
    desc->frame_len = 0;
    desc->dialog_token = 0;
}

static ssize_t
osw_btm_build_frame(const struct osw_btm_req_params *req_params,
                    const struct osw_hwaddr *sta_addr,
                    const struct osw_hwaddr *bssid,
                    uint8_t dialog_token,
                    uint8_t *frame_buf,
                    size_t frame_buf_size)
{
    ASSERT(req_params != NULL, "");
    ASSERT(bssid != NULL, "");
    ASSERT(frame_buf != NULL, "");
    ASSERT(frame_buf_size > 0, "");

    static const uint8_t dot11_pref_list_incl = 0x01;
    static const uint8_t dot11_abridged = 0x02;
    static const uint8_t dot11_disassoc_imminent = 0x04;
    static const uint8_t dot11_bss_term_incl = 0x08;
    static const uint16_t dot11_frame_control = 0xd0;
    static const uint8_t dot11_category = 0x0A;
    static const uint8_t dot11_action_code = 0x07;
    static const uint8_t dot11_neigh_report_tag = 0x34;

    static const uint16_t duration = 60;
    static const uint16_t diassoc_timer = 0x0000;
    static const uint8_t neigh_report_tag_len = 13;

    struct osw_drv_dot11_frame *frame = (struct osw_drv_dot11_frame*) frame_buf;
    size_t frame_len = 0;
    uint8_t options = 0;
    uint8_t *neigh_list = NULL;
    size_t i = 0;

    /* compute frame length first */
    frame_len += offsetof(struct osw_drv_dot11_frame, u.action.u.bss_tm_req);
    frame_len += C_FIELD_SZ(struct osw_drv_dot11_frame, u.action.u.bss_tm_req);
    frame_len += req_params->neigh_len * sizeof(struct osw_drv_dot11_neighbor_report);

    if (frame_len > frame_buf_size) {
        LOGW("osw: btm: [sta: "OSW_HWADDR_FMT"] failed to build frame, to small buffer (len: %zu size: %zu)",
             OSW_HWADDR_ARG(sta_addr), frame_len, frame_buf_size);
        return -1;
    }

    /* Build frame */
    frame->header.frame_control = htole16(dot11_frame_control);
    frame->header.duration = htole16(duration);
    memcpy(&frame->header.da, &sta_addr->octet, sizeof(frame->header.da));
    memcpy(&frame->header.sa, &bssid->octet, sizeof(frame->header.sa));
    memcpy(&frame->header.bssid, &bssid->octet, sizeof(frame->header.bssid));
    frame->header.seq_ctrl = 0;

    options |= req_params->neigh_len > 0 ? dot11_pref_list_incl : 0;
    options |= req_params->abridged == true ? dot11_abridged : 0;
    options |= req_params->disassoc_imminent == true ? dot11_disassoc_imminent : 0;
    options |= req_params->bss_term == true ? dot11_bss_term_incl : 0;

    frame->u.action.category = dot11_category;
    frame->u.action.u.bss_tm_req.action = dot11_action_code;
    frame->u.action.u.bss_tm_req.dialog_token = dialog_token;
    frame->u.action.u.bss_tm_req.options = options;
    frame->u.action.u.bss_tm_req.disassoc_timer = htole16(diassoc_timer);
    frame->u.action.u.bss_tm_req.validity_interval = req_params->valid_int;

    neigh_list = frame->u.action.u.bss_tm_req.variable;
    for (i = 0; i < req_params->neigh_len; i ++) {
        const struct osw_btm_req_neigh *neigh = &req_params->neigh[i];
        struct osw_drv_dot11_neighbor_report *entry = (struct osw_drv_dot11_neighbor_report*) neigh_list;

        entry->tag = dot11_neigh_report_tag;
        entry->tag_len = neigh_report_tag_len;
        memcpy(&entry->bssid, &neigh->bssid, sizeof(entry->bssid));
        entry->bssid_info = htole32(neigh->bssid_info);
        entry->op_class = neigh->op_class;
        entry->channel = neigh->channel;
        entry->phy_type = neigh->phy_type;

        neigh_list += sizeof(*entry);
    }

    return frame_len;
}

static void
osw_btm_sta_schedule_work(struct osw_btm_sta *btm_sta)
{
    ASSERT(btm_sta != NULL, "");
    osw_timer_arm_at_nsec(&btm_sta->work_timer, osw_time_mono_clk());
}

static void
osw_btm_sta_try_req_tx(struct osw_btm_sta *btm_sta)
{
    ASSERT(btm_sta != NULL, "");

    const struct osw_state_vif_info *vif_info = btm_sta->sta_info->vif;
    const struct osw_drv_dot11_frame *frame = NULL;
    struct osw_btm_desc *desc;

    if (btm_sta->sta_info == NULL) {
        LOGT("osw: btm: [sta: "OSW_HWADDR_FMT"] cannot tx req to disconnected sta",
             OSW_HWADDR_ARG(&btm_sta->mac_addr));
        goto cease_tx_attempt;
    }

    if (ds_dlist_is_empty(&btm_sta->desc_list) == true) {
        goto cease_tx_attempt;
    }

    desc = ds_dlist_head(&btm_sta->desc_list);
    frame = (const struct osw_drv_dot11_frame*) &desc->frame_buf;
    switch (desc->state) {
        case OSW_BTM_DESC_STATE_PENDING:
            /* just continue */
            break;
        case OSW_BTM_DESC_STATE_IN_TRANSIT:
            LOGT("osw: btm: [sta: "OSW_HWADDR_FMT" dialog_token: %u] req already in transit",
                 OSW_HWADDR_ARG(&btm_sta->mac_addr), frame->u.action.u.bss_tm_req.dialog_token);
            goto cease_tx_attempt;
        case OSW_BTM_DESC_STATE_EMPTY:
            LOGT("osw: btm: [sta: "OSW_HWADDR_FMT" dialog_token: %u] cannot tx empty req",
                 OSW_HWADDR_ARG(&btm_sta->mac_addr), frame->u.action.u.bss_tm_req.dialog_token);
            goto cease_tx_attempt;
    }

    if (btm_sta->throttle != NULL) {
        uint64_t next_at_nsec;
        bool result;

        result = osw_throttle_tap(btm_sta->throttle, &next_at_nsec);
        if (result == false) {
            osw_timer_arm_at_nsec(&btm_sta->throttle_timer, next_at_nsec);
            LOGT("osw: btm: [sta: "OSW_HWADDR_FMT"] cease req tx attempt due to throttle condition",
                 OSW_HWADDR_ARG(&btm_sta->mac_addr));
            goto cease_tx_attempt;
        }
    }

    desc->state = OSW_BTM_DESC_STATE_IN_TRANSIT;

    osw_drv_frame_tx_desc_set_frame(btm_sta->frame_tx_desc, desc->frame_buf, desc->frame_len);
    ASSERT(btm_sta->mux_frame_tx_schedule_fn != NULL, "");
    btm_sta->mux_frame_tx_schedule_fn(vif_info->phy->phy_name, vif_info->vif_name, btm_sta->frame_tx_desc);

    LOGD("osw: btm: [sta: "OSW_HWADDR_FMT" dialog_token: %u] req was passed to drv",
         OSW_HWADDR_ARG(&btm_sta->mac_addr), frame->u.action.u.bss_tm_req.dialog_token);

    return;

    cease_tx_attempt:
        LOGD("osw: btm: [sta: "OSW_HWADDR_FMT"] req tx attempt was cease", OSW_HWADDR_ARG(&btm_sta->mac_addr));
}

static void
osw_btm_sta_throttle_timer_cb(struct osw_timer *timer)
{
    struct osw_btm_sta *sta = (struct osw_btm_sta*) container_of(timer, struct osw_btm_sta, throttle_timer);
    osw_btm_sta_schedule_work(sta);
}

static void
osw_btm_sta_work_timer_cb(struct osw_timer *timer)
{
    struct osw_btm_sta *sta = (struct osw_btm_sta*) container_of(timer, struct osw_btm_sta, work_timer);
    osw_btm_sta_try_req_tx(sta);
}

static void
osw_btm_sta_connected_cb(struct osw_state_observer *observer,
                         const struct osw_state_sta_info *sta_info)
{
    struct osw_btm_sta *btm_sta = container_of(observer, struct osw_btm_sta, observer);

    btm_sta->sta_info = sta_info;
    osw_btm_sta_schedule_work(btm_sta);
}

static void
osw_btm_sta_disconnected_cb(struct osw_state_observer *observer,
                            const struct osw_state_sta_info *sta_info)
{
    struct osw_btm_sta *btm_sta = container_of(observer, struct osw_btm_sta, observer);

    if (btm_sta->sta_info != sta_info)
        return;

    btm_sta->sta_info = NULL;
    if (btm_sta->throttle != NULL) {
        osw_throttle_reset(btm_sta->throttle);
        osw_timer_disarm(&btm_sta->throttle_timer);
    }

    osw_drv_frame_tx_desc_cancel(btm_sta->frame_tx_desc);
}

static void
osw_btm_drv_frame_tx_result_cb(const struct osw_drv_frame_tx_desc *tx_desc,
                               enum osw_frame_tx_result result,
                               void *caller_priv)
{
    const struct osw_drv_dot11_frame *drv_frame = NULL;
    const struct osw_drv_dot11_frame *btm_frame = NULL;
    struct osw_btm_sta *sta = (struct osw_btm_sta *) caller_priv;
    struct osw_btm_desc *btm_desc = ds_dlist_head(&sta->desc_list);

    if (WARN_ON(btm_desc == NULL))
        goto ignore_tx_report;

    if (btm_desc->state != OSW_BTM_DESC_STATE_IN_TRANSIT)
        goto ignore_tx_report;

    drv_frame = (const struct osw_drv_dot11_frame*) osw_drv_frame_tx_desc_get_frame(tx_desc);
    btm_frame = (const struct osw_drv_dot11_frame*) &btm_desc->frame_buf;
    if (WARN_ON(drv_frame == NULL || btm_frame == NULL))
        goto ignore_tx_report;

    if (WARN_ON(drv_frame->u.action.u.bss_tm_req.dialog_token != btm_frame->u.action.u.bss_tm_req.dialog_token))
        goto ignore_tx_report;

    LOGD("osw: btm: [sta: "OSW_HWADDR_FMT" dialog token: %u] drv reported req tx result: %s",
         OSW_HWADDR_ARG(&sta->mac_addr), btm_frame->u.action.u.bss_tm_req.dialog_token,
         osw_frame_tx_result_to_cstr(result));

    switch (result) {
        case OSW_FRAME_TX_RESULT_SUBMITTED:
            if (btm_desc->observer->req_tx_complete_fn != NULL)
                btm_desc->observer->req_tx_complete_fn(btm_desc->observer);
            break;
        case OSW_FRAME_TX_RESULT_FAILED:
        case OSW_FRAME_TX_RESULT_DROPPED:
            if (btm_desc->observer->req_tx_error_fn != NULL)
                btm_desc->observer->req_tx_error_fn(btm_desc->observer);
            break;
    }

    osw_btm_desc_reset(btm_desc);
    return;

ignore_tx_report:
    LOGD("osw: btm: [sta: "OSW_HWADDR_FMT"] ignored drv req tx report result: %s",
         OSW_HWADDR_ARG(&sta->mac_addr), osw_frame_tx_result_to_cstr(result));
}

static struct osw_btm_sta*
osw_btm_get_sta(const struct osw_hwaddr *sta_addr,
                osw_btm_mux_frame_tx_schedule_fn_t *mux_frame_tx_schedule_fn)
{
    ASSERT(sta_addr != NULL, "");

    const struct osw_state_observer observer = {
        .name = "osw_btm",
        .sta_connected_fn = osw_btm_sta_connected_cb,
        .sta_disconnected_fn = osw_btm_sta_disconnected_cb,
    };

    struct osw_btm_sta *sta = ds_tree_find(&g_sta_tree, sta_addr);

    if (sta != NULL)
        return sta;

    sta = CALLOC(1, sizeof(*sta));
    memcpy(&sta->mac_addr, sta_addr, sizeof(sta->mac_addr));
    memcpy(&sta->observer, &observer, sizeof(sta->observer));
    osw_timer_init(&sta->throttle_timer, osw_btm_sta_throttle_timer_cb);
    osw_timer_init(&sta->work_timer, osw_btm_sta_work_timer_cb);
    sta->frame_tx_desc = osw_drv_frame_tx_desc_new(osw_btm_drv_frame_tx_result_cb, sta);
    sta->sta_info = osw_state_sta_lookup_newest(&sta->mac_addr);
    ds_dlist_init(&sta->desc_list, struct osw_btm_desc, node);
    sta->mux_frame_tx_schedule_fn = mux_frame_tx_schedule_fn;

    osw_state_register_observer(&sta->observer);

    ds_tree_insert(&g_sta_tree, sta, &sta->mac_addr);

    return sta;
}

static void
osw_btm_sta_free(struct osw_btm_sta *sta)
{
    ASSERT(sta != NULL, "");

    osw_state_unregister_observer(&sta->observer);
    osw_throttle_free(sta->throttle);
    osw_timer_disarm(&sta->throttle_timer);
    osw_timer_disarm(&sta->work_timer);
    osw_drv_frame_tx_desc_free(sta->frame_tx_desc);

    ds_tree_remove(&g_sta_tree, sta);
    FREE(sta);
}

struct osw_btm_desc*
osw_btm_get_desc_internal(const struct osw_hwaddr *sta_addr,
                          struct osw_btm_sta_observer *observer,
                          osw_btm_mux_frame_tx_schedule_fn_t *mux_frame_tx_schedule_fn)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(observer != NULL, "");
    ASSERT(mux_frame_tx_schedule_fn != NULL, "");

    struct osw_btm_sta *sta = osw_btm_get_sta(sta_addr, mux_frame_tx_schedule_fn);
    struct osw_btm_desc *desc = CALLOC(1, sizeof(*desc));

    desc->sta = sta;
    desc->observer = observer;

    ds_dlist_insert_tail(&sta->desc_list, desc);

    return desc;
}

struct osw_btm_desc*
osw_btm_get_desc(const struct osw_hwaddr *sta_addr,
                 struct osw_btm_sta_observer *observer)
{
    ASSERT(sta_addr != NULL, "");
    ASSERT(observer != NULL, "");
    return osw_btm_get_desc_internal(sta_addr, observer, osw_mux_frame_tx_schedule);
}

void
osw_btm_desc_free(struct osw_btm_desc *desc)
{
    if (desc == NULL)
        return;

    struct osw_btm_sta *btm_sta = desc->sta;

    ds_dlist_remove(&btm_sta->desc_list, desc);
    FREE(desc);

    if (ds_dlist_is_empty(&btm_sta->desc_list) == true)
        osw_btm_sta_free(btm_sta);
}

bool
osw_btm_desc_set_req_params(struct osw_btm_desc *desc,
                            const struct osw_btm_req_params *params)
{
    ASSERT(desc != NULL, "");

    switch (desc->state) {
        case OSW_BTM_DESC_STATE_IN_TRANSIT:
            return false;
        case OSW_BTM_DESC_STATE_PENDING:
            if (params != NULL) {
                return false;
            }
            else {
                osw_btm_desc_reset(desc);
                return true;
            }
            break;
        case OSW_BTM_DESC_STATE_EMPTY:
            /* continue */
            break;
    }

    struct osw_btm_sta *btm_sta = desc->sta;

    if (btm_sta->sta_info == NULL) {
        osw_btm_desc_reset(desc);
        return false;
    }

    const struct osw_state_vif_info *vif_info = btm_sta->sta_info->vif;

    ds_dlist_remove(&btm_sta->desc_list, desc);
    ds_dlist_insert_tail(&btm_sta->desc_list, desc);

    desc->dialog_token = osw_btm_desc_generate_dialog_token(btm_sta);
    desc->frame_len = osw_btm_build_frame(params,
                                          &btm_sta->mac_addr,
                                          &vif_info->drv_state->mac_addr,
                                          desc->dialog_token,
                                          desc->frame_buf,
                                          sizeof(desc->frame_buf));
    if (desc->frame_len < 0) {
        osw_btm_desc_reset(desc);
        return false;
    }

    desc->state = OSW_BTM_DESC_STATE_PENDING;
    osw_btm_sta_schedule_work(btm_sta);

    return true;
}

struct osw_btm_sta*
osw_btm_desc_get_sta(struct osw_btm_desc *desc)
{
    ASSERT(desc != NULL, "");
    ASSERT(desc->sta != NULL, "");
    return desc->sta;
}

void
osw_btm_sta_set_throttle(struct osw_btm_sta *btm_sta,
                         struct osw_throttle *throttle)
{
    ASSERT(btm_sta != NULL, "");

    osw_throttle_free(btm_sta->throttle);
    if (throttle !=  NULL)
        btm_sta->throttle = throttle;

    osw_btm_sta_schedule_work(btm_sta);
}

#include "osw_btm_ut.c"
