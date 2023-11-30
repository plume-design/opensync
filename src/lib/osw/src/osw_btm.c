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
#include <osw_state.h>
#include <osw_token.h>
#include <osw_util.h>
#include <osw_btm.h>

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
    int dialog_token;

    struct ds_dlist_node node;
};

typedef bool
osw_btm_mux_frame_tx_schedule_fn_t(const char *phy_name,
                                   const char *vif_name,
                                   struct osw_drv_frame_tx_desc *desc);

struct osw_btm_sta_info {
    struct ds_tree_node node;
    const struct osw_state_sta_info *info;
};

struct osw_btm_sta {
    struct ds_tree infos;
    struct osw_hwaddr mac_addr;
    struct osw_state_observer observer;
    struct osw_throttle *throttle;
    struct osw_timer throttle_timer;
    struct osw_timer work_timer;
    struct osw_drv_frame_tx_desc *frame_tx_desc;
    struct osw_state_observer state_observer;
    struct osw_btm_desc *desc_in_flight;
    bool in_flight;
    const struct osw_state_sta_info *sta_info;
    struct ds_dlist desc_list;
    struct osw_token_pool_reference *pool_ref;

    osw_btm_mux_frame_tx_schedule_fn_t *mux_frame_tx_schedule_fn;

    struct ds_tree_node node;
};

static struct ds_dlist g_btm_response_observer_list = DS_DLIST_INIT(struct osw_btm_response_observer, node);
static struct ds_tree g_sta_tree = DS_TREE_INIT((ds_key_cmp_t*) osw_hwaddr_cmp, struct osw_btm_sta, node);

static const struct osw_state_sta_info *
osw_btm_sta_infos_find_newest(struct osw_btm_sta *sta)
{
    struct osw_btm_sta_info *newest = ds_tree_head(&sta->infos);
    struct osw_btm_sta_info *info;
    ds_tree_foreach(&sta->infos, info) {
        if (info->info->connected_at > newest->info->connected_at) {
            newest = info;
        }
    }
    return (newest != NULL) ? newest->info : NULL;
}

static void
osw_btm_sta_infos_add(struct osw_btm_sta *sta,
                      const struct osw_state_sta_info *info)
{
    const bool different_sta = (osw_hwaddr_is_equal(&sta->mac_addr, info->mac_addr) == false);
    if (different_sta) return;

    const bool already_added = (ds_tree_find(&sta->infos, info) == info);
    if (WARN_ON(already_added)) return;

    struct osw_btm_sta_info *i = CALLOC(1, sizeof(*i));
    i->info = info;
    ds_tree_insert(&sta->infos, i, info);
}

static void
osw_btm_sta_infos_del(struct osw_btm_sta *sta,
                      const struct osw_state_sta_info *info)
{
    const bool different_sta = (osw_hwaddr_is_equal(&sta->mac_addr, info->mac_addr) == false);
    if (different_sta) return;

    struct osw_btm_sta_info *i = ds_tree_find(&sta->infos, info);
    const bool does_not_exist = (i == NULL);
    if (WARN_ON(does_not_exist)) return;

    ds_tree_remove(&sta->infos, i);
    FREE(i);
}

void
osw_btm_register_btm_response_observer(struct osw_btm_response_observer *observer)
{
    ASSERT(observer != NULL, "");

    LOGD("osw: btm: registering observer,"
         " sta_addr: "OSW_HWADDR_FMT,
         OSW_HWADDR_ARG(&observer->sta_addr));
    ds_dlist_insert_tail(&g_btm_response_observer_list, observer);
}

void
osw_btm_unregister_btm_response_observer(struct osw_btm_response_observer *observer)
{
    ASSERT(observer != NULL, "");

    LOGD("osw: btm: unregistering observer,"
         " sta_addr: "OSW_HWADDR_FMT,
         OSW_HWADDR_ARG(&observer->sta_addr));
    ds_dlist_remove(&g_btm_response_observer_list, observer);
}

static void
osw_btm_notify_btm_response(const struct osw_hwaddr *sta_addr,
                            const int response_code,
                            const struct osw_btm_retry_neigh_list *retry_neigh_list)
{
    struct osw_btm_response_observer *observer;
    ds_dlist_foreach(&g_btm_response_observer_list, observer) {
        if (osw_hwaddr_cmp(&observer->sta_addr, sta_addr) != 0) continue;
        if (observer->btm_response_fn == NULL) continue;
        LOGT("osw: btm: notify observer about btm response,"
             " sta_addr: "OSW_HWADDR_FMT,
             OSW_HWADDR_ARG(&observer->sta_addr));
        observer->btm_response_fn(observer,
                                  response_code,
                                  retry_neigh_list);
    }
}

static void
osw_btm_desc_reset(struct osw_btm_desc *desc)
{
    ASSERT(desc != NULL, "");
    ASSERT(desc->sta != NULL, "");

    if (desc->sta->desc_in_flight == desc) {
        desc->sta->desc_in_flight = NULL;
    }

    desc->state = OSW_BTM_DESC_STATE_EMPTY;
    memset(&desc->frame_buf, 0, sizeof(desc->frame_buf));
    desc->frame_len = 0;

    if ((desc->sta->pool_ref != NULL) &&
        (desc->dialog_token != OSW_TOKEN_INVALID)) {
        osw_token_pool_free_token(desc->sta->pool_ref,
                                  desc->dialog_token);
        desc->dialog_token = OSW_TOKEN_INVALID;
    }
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
    frame->u.action.u.bss_tm_req.disassoc_timer = htole16(req_params->disassoc_timer);
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

void
osw_btm_sta_log_req_params(const struct osw_btm_req_params *params)
{
    size_t i;

    LOGI("osw: btm: parameters:"
         " valid_int %"PRIu8
         " abridged %d"
         " disassoc_imminent %d"
         " disassoc_timer %hu"
         " bss_term %d"
         " neighs %zu",
         params->valid_int,
         params->abridged,
         params->disassoc_imminent,
         params->disassoc_timer,
         params->bss_term,
         params->neigh_len);

    for (i = 0; i < params->neigh_len; i ++) {
        const struct osw_btm_req_neigh *n = &params->neigh[i];
        LOGI("osw: btm: parameters: neighbor[%zu]:"
             " bssid "OSW_HWADDR_FMT
             " info 0x%08"PRIx32
             " chan %"PRIu8
             " opclass %"PRIu8
             " phytype %"PRIu8,
             i,
             OSW_HWADDR_ARG(&n->bssid),
             n->bssid_info,
             n->channel,
             n->op_class,
             n->phy_type);
    }
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

    if (btm_sta->sta_info == NULL) {
        LOGT("osw: btm: [sta: "OSW_HWADDR_FMT"] cannot tx req to disconnected sta",
             OSW_HWADDR_ARG(&btm_sta->mac_addr));
        goto cease_tx_attempt;
    }

    const struct osw_state_vif_info *vif_info = btm_sta->sta_info->vif;
    const struct osw_drv_dot11_frame *frame = NULL;
    struct osw_btm_desc *desc;

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

    if (btm_sta->in_flight) {
        LOGT("osw: btm: [sta: "OSW_HWADDR_FMT" dialog_token: ?] req already in transit (freed)",
             OSW_HWADDR_ARG(&btm_sta->mac_addr));
        goto cease_tx_attempt;
    }

    if (btm_sta->desc_in_flight != NULL) {
        frame = (const struct osw_drv_dot11_frame*) &btm_sta->desc_in_flight->frame_buf;
        LOGT("osw: btm: [sta: "OSW_HWADDR_FMT" dialog_token: %u] req already in transit (non-head)",
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
    btm_sta->desc_in_flight = desc;
    btm_sta->in_flight = true;
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
osw_btm_sta_set_info(struct osw_btm_sta *btm_sta,
                     const struct osw_state_sta_info *sta_info)
{
    if (btm_sta->sta_info == sta_info) return;

    if (btm_sta->pool_ref != NULL) {
        /* free all dialog tokens */
        struct osw_btm_desc *desc;
        ds_dlist_foreach(&btm_sta->desc_list, desc) {
            if (desc->dialog_token != OSW_TOKEN_INVALID) {
                osw_token_pool_free_token(btm_sta->pool_ref,
                                          desc->dialog_token);
                desc->dialog_token = OSW_TOKEN_INVALID;
            }
        }

        /* free pool reference */
        osw_token_pool_ref_free(btm_sta->pool_ref);
        btm_sta->pool_ref = NULL;
    }

    if (sta_info == NULL) {
        /* FIXME: This probably shouldn't really reset throttle just yet ? */
        if (btm_sta->throttle != NULL) {
            osw_throttle_reset(btm_sta->throttle);
            osw_timer_disarm(&btm_sta->throttle_timer);
        }
    }
    else {
        const struct osw_hwaddr *sta_addr = &btm_sta->mac_addr;
        const char *vif_name_cstr = sta_info->vif->vif_name;
        struct osw_ifname vif_name;
        STRSCPY_WARN(vif_name.buf, vif_name_cstr);
        btm_sta->pool_ref = osw_token_pool_ref_get(&vif_name, sta_addr);
    }

    if (btm_sta->frame_tx_desc != NULL) {
        osw_drv_frame_tx_desc_cancel(btm_sta->frame_tx_desc);
    }

    btm_sta->sta_info = sta_info;
    osw_btm_sta_schedule_work(btm_sta);
}

static void
osw_btm_sta_connected_cb(struct osw_state_observer *observer,
                         const struct osw_state_sta_info *sta_info)
{
    struct osw_btm_sta *btm_sta = container_of(observer, struct osw_btm_sta, observer);
    osw_btm_sta_infos_add(btm_sta, sta_info);
    const struct osw_state_sta_info *newest_sta_info = osw_btm_sta_infos_find_newest(btm_sta);
    osw_btm_sta_set_info(btm_sta, newest_sta_info);
}

static void
osw_btm_sta_disconnected_cb(struct osw_state_observer *observer,
                            const struct osw_state_sta_info *sta_info)
{
    struct osw_btm_sta *btm_sta = container_of(observer, struct osw_btm_sta, observer);
    osw_btm_sta_infos_del(btm_sta, sta_info);
    const struct osw_state_sta_info *newest_sta_info = osw_btm_sta_infos_find_newest(btm_sta);
    osw_btm_sta_set_info(btm_sta, newest_sta_info);
}

static void
osw_btm_drv_frame_tx_result_cb(struct osw_drv_frame_tx_desc *tx_desc,
                               enum osw_frame_tx_result result,
                               void *caller_priv)
{
    const struct osw_drv_dot11_frame *btm_frame = NULL;
    struct osw_btm_sta *sta = (struct osw_btm_sta *) caller_priv;
    struct osw_btm_desc *btm_desc = sta->desc_in_flight;

    const bool unexpected = (sta->in_flight == false);
    if (WARN_ON(unexpected))
        goto ignore_tx_report;

    sta->in_flight = false;

    const bool was_freed_while_in_transit = (btm_desc == NULL);
    if (was_freed_while_in_transit)
        goto ignore_tx_report;

    const bool invalid_desc_state = (btm_desc->state != OSW_BTM_DESC_STATE_IN_TRANSIT);
    if (WARN_ON(invalid_desc_state))
        goto ignore_tx_report;

    btm_frame = (const struct osw_drv_dot11_frame*) &btm_desc->frame_buf;

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
    ds_tree_init(&sta->infos, ds_void_cmp, struct osw_btm_sta_info, node);
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

    ASSERT(ds_tree_is_empty(&sta->infos), "");
    ASSERT(sta->sta_info == NULL, "");
    ASSERT(sta->pool_ref == NULL, "");

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

    osw_btm_desc_reset(desc);
    ds_dlist_remove(&btm_sta->desc_list, desc);
    FREE(desc);

    if (ds_dlist_is_empty(&btm_sta->desc_list) == true)
        osw_btm_sta_free(btm_sta);
}

static bool
osw_btm_desc_can_set_req_params(struct osw_btm_desc *desc)
{
    if (WARN_ON(desc->sta == NULL)) return false;
    if (desc->sta->desc_in_flight != NULL) return false;
    if (desc->sta->in_flight) return false;

    switch (desc->state) {
        case OSW_BTM_DESC_STATE_IN_TRANSIT:
            return false;
        case OSW_BTM_DESC_STATE_PENDING:
            return true;
        case OSW_BTM_DESC_STATE_EMPTY:
            return true;
    }

    WARN_ON(1);
    return false;
}

bool
osw_btm_desc_set_req_params(struct osw_btm_desc *desc,
                            const struct osw_btm_req_params *params)
{
    ASSERT(desc != NULL, "");
    ASSERT(desc->sta != NULL, "");

    const bool can_set = osw_btm_desc_can_set_req_params(desc);
    const bool cannot_set = !can_set;
    if (cannot_set) {
        return false;
    }

    osw_btm_desc_reset(desc);

    struct osw_btm_sta *btm_sta = desc->sta;
    if (btm_sta->sta_info == NULL) {
        return false;
    }

    const struct osw_state_vif_info *vif_info = btm_sta->sta_info->vif;

    ds_dlist_remove(&btm_sta->desc_list, desc);
    ds_dlist_insert_head(&btm_sta->desc_list, desc);

    int dtoken = 0;
    struct osw_token_pool_reference *pool_ref = desc->sta->pool_ref;
    if (pool_ref != NULL) dtoken = osw_token_pool_fetch_token(desc->sta->pool_ref);
    const bool fetch_token_failed = (dtoken == OSW_TOKEN_INVALID);
    if (WARN_ON(fetch_token_failed == true)) dtoken = 0;

    desc->dialog_token = dtoken;
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

static bool
osw_btm_parse_neighbor_report(const struct osw_hwaddr *sta_addr,
                              struct osw_btm_retry_neigh *retry_neigh,
                              const struct osw_drv_dot11_neighbor_report *neighbor_element)
{
    /* validate tag length */
    const unsigned int neigh_elem_len_w_hdr = neighbor_element->tag_len + 2;
    const unsigned int neigh_elem_struct_size = sizeof(*neighbor_element);
    if (neigh_elem_len_w_hdr < neigh_elem_struct_size) {
        LOGI("osw: btm_parse_neighbor_report: neighbor report element too small,"
             " neigh_elem_len_w_hdr: %d"
             " neigh_elem_struct_size: %d",
             neigh_elem_len_w_hdr,
             neigh_elem_struct_size);
        return false;
    }

    /* determine neighbor preference */
    int neigh_pref = -1;
    const struct element *subelem;
    const uint8_t *subelems = neighbor_element->variable;
    const size_t subelems_len = neigh_elem_len_w_hdr - neigh_elem_struct_size;
    for_each_ie(subelem, subelems, subelems_len) {

        if (subelem->id != DOT11_NEIGHBOR_REPORT_CANDIDATE_PREFERENCE) continue;

        if (subelem->datalen == 1) {
            LOGT("osw: btm_parse_neighbor_report: candidate preference subelement present");
            struct osw_drv_dot11_neighbor_preference *neigh_pref_sub = (struct osw_drv_dot11_neighbor_preference *)subelem;
            neigh_pref = neigh_pref_sub->preference;
            break;
        }
    }

    struct osw_btm_req_neigh *retry_neigh_n = &retry_neigh->neigh;
    memcpy(&retry_neigh_n->bssid, neighbor_element->bssid, sizeof(retry_neigh_n->bssid));
    retry_neigh_n->bssid_info = neighbor_element->bssid_info;
    retry_neigh_n->op_class = neighbor_element->op_class;
    retry_neigh_n->channel = neighbor_element->channel;
    retry_neigh_n->phy_type = neighbor_element->phy_type;
    retry_neigh->preference = neigh_pref;

    LOGI("osw: btm: neigh_reject_candidates: parsed neighbor from btm reject,"
         " sta_addr: " OSW_HWADDR_FMT
         " bssid: " OSW_HWADDR_FMT
         " bssid_info: %02x%02x%02x%02x"
         " op_class: %hhu"
         " channel: %hhu"
         " phy_type: %hhu"
         " preference: %d",
         OSW_HWADDR_ARG(sta_addr),
         OSW_HWADDR_ARG(&retry_neigh_n->bssid),
         (retry_neigh_n->bssid_info >> 24) & 0xff,
         (retry_neigh_n->bssid_info >> 16) & 0xff,
         (retry_neigh_n->bssid_info >> 8) & 0xff,
         (retry_neigh_n->bssid_info) & 0xff,
         retry_neigh_n->op_class,
         retry_neigh_n->channel,
         retry_neigh_n->phy_type,
         retry_neigh->preference);

    return true;
}

static void
osw_btm_resp_parse_neigh_reject_candidates(const struct osw_hwaddr *sta_addr,
                                           const uint8_t *ies,
                                           const size_t ies_len)
{
    struct osw_btm_retry_neigh_list retry_neigh_list;
    retry_neigh_list.neigh_len = 0;

    const struct element *elem;
    for_each_ie(elem, ies, ies_len) {
        if (elem->id != DOT11_NEIGHBOR_REPORT_IE_TAG) continue;

        LOGT("osw: btm: neigh_reject_candidates: parsing neigbor report from btm response reject");
        const bool ok = osw_btm_parse_neighbor_report(sta_addr,
                                                      &retry_neigh_list.neigh[retry_neigh_list.neigh_len],
                                                      (struct osw_drv_dot11_neighbor_report *)elem);

        if (ok == true) retry_neigh_list.neigh_len++;
        if (retry_neigh_list.neigh_len >= OSW_BTM_REQ_NEIGH_SIZE) {
            LOGN("osw: btm: neigh_reject_candidates: response neighbors list full");
            break;
        }
    }

    osw_btm_notify_btm_response(sta_addr,
                                DOT11_BTM_RESPONSE_CODE_REJECT_CAND_LIST_PROVIDED,
                                &retry_neigh_list);
}

static bool
osw_btm_frame_is_action(const struct osw_drv_dot11_frame *frame)
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
osw_btm_action_frame_is_btm_response(const struct osw_drv_dot11_frame_action *action)
{
    ASSERT(action != NULL, "");

    const struct osw_drv_dot11_frame_action_bss_tm_resp *bss_tm_resp = &action->u.bss_tm_resp;
    const bool is_wnm = (action->category == DOT11_ACTION_CATEGORY_WNM_CODE);
    const bool is_btm_response = (bss_tm_resp->action == DOT11_BTM_RESPONSE_IE_ACTION_CODE);

    if (is_wnm == false) return false;
    if (is_btm_response == false) return false;
    return true;
}

static void
osw_btm_frame_rx_cb(struct osw_state_observer *self,
                    const struct osw_state_vif_info *vif,
                    const uint8_t *data,
                    size_t len)
{
    if (WARN_ON(data == NULL)) return;
    if (WARN_ON(vif == NULL)) return;
    if (WARN_ON(vif->vif_name == NULL)) return;

    /* filter out btm response frames and validate data length */
    const struct osw_drv_dot11_frame *frame = (const struct osw_drv_dot11_frame *)data;
    const size_t dot11_header_len = sizeof(frame->header);
    if (WARN_ON(len < dot11_header_len)) return;

    const bool is_action = osw_btm_frame_is_action((const struct osw_drv_dot11_frame *) data);
    if (is_action == false) return;

    const struct osw_drv_dot11_frame_action *action = &frame->u.action;
    const size_t dot11_action_len = sizeof(action->category);
    if (WARN_ON(len < dot11_header_len + dot11_action_len)) return;

    const bool is_btm_response = osw_btm_action_frame_is_btm_response(action);
    if (is_btm_response == false) return;

    const struct osw_drv_dot11_frame_action_bss_tm_resp *bss_tm_resp = &action->u.bss_tm_resp;
    const size_t dot11_btm_resp_min_len = sizeof(*bss_tm_resp);
    if (WARN_ON(len < dot11_header_len + dot11_action_len + dot11_btm_resp_min_len)) return;

    const struct osw_drv_dot11_frame_header *header = &frame->header;
    const uint8_t *sa = header->sa;
    struct osw_hwaddr sta_addr;
    memcpy(sta_addr.octet, sa, OSW_HWADDR_LEN);
    LOGI("osw: btm_frame_rep_cb: received btm response,"
         " vif_name: %s"
         " sta_addr: "OSW_HWADDR_FMT
         " status_code: %d",
         vif->vif_name,
         OSW_HWADDR_ARG(&sta_addr),
         bss_tm_resp->status_code);

    /* proceed only with response containing neighbor list */
    const bool cand_list_provided = (bss_tm_resp->status_code == DOT11_BTM_RESPONSE_CODE_REJECT_CAND_LIST_PROVIDED);
    if (cand_list_provided == false) return;

    const uint8_t *ies = bss_tm_resp->variable;
    const size_t ies_len = data + len - ies;
    osw_btm_resp_parse_neigh_reject_candidates(&sta_addr,
                                               ies,
                                               ies_len);
}

OSW_MODULE(osw_btm)
{
    OSW_MODULE_LOAD(osw_token);
    struct osw_state_observer *state_observer = CALLOC(1, sizeof(*state_observer));
    state_observer->name = "g_osw_btm";
    state_observer->vif_frame_rx_fn = osw_btm_frame_rx_cb;
    osw_state_register_observer(state_observer);
    return NULL;
}

#include "osw_btm_ut.c"
