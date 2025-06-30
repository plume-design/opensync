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
#include <const_ieee80211.h>
#include <util.h>
#include <os.h>
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
#include <osw_state.h>
#include <osw_token.h>
#include <osw_util.h>
#include <osw_btm.h>
#include <osw_sta_assoc.h>

#define LOG_PREFIX(fmt, ...) \
    "osw: btm: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_STA(sta, fmt, ...) \
    LOG_PREFIX(OSW_HWADDR_FMT ": " fmt, OSW_HWADDR_ARG(&(sta)->mac_addr), ##__VA_ARGS__)

#define LOG_PREFIX_REQ(req, fmt, ...) \
    LOG_PREFIX_STA((req)->sta, "%p: " fmt, (req), ##__VA_ARGS__)

enum osw_btm_req_state {
    OSW_BTM_REQ_STATE_INIT,
    OSW_BTM_REQ_STATE_READY,
    OSW_BTM_REQ_STATE_SUBMITTED,
    OSW_BTM_REQ_STATE_COMPLETED,
};

typedef struct osw_btm_req_link osw_btm_req_link_t;

struct osw_btm_req_link {
    ds_tree_node_t node;
    osw_btm_req_t *req;
    struct osw_drv_frame_tx_desc *frame_tx_desc;
    uint8_t frame_buf[OSW_DRV_FRAME_TX_DESC_BUF_SIZE];
    ssize_t frame_len;
    char *phy_name;
    char *vif_name;
    enum osw_btm_req_result result;
};

struct osw_btm_req {
    osw_btm_sta_t *sta;
    ds_tree_node_t node;
    ds_tree_t links;
    enum osw_btm_req_state state;
    osw_btm_req_completed_fn_t *completed_fn;
    void *completed_fn_priv;
    osw_btm_req_response_fn_t *response_fn;
    void *response_fn_priv;
    struct osw_btm_req_params *params;
    int dialog_token;
};

struct osw_btm_resp {
    struct osw_hwaddr sta_addr;
    struct osw_hwaddr target_bssid;
    uint8_t status_code;
    uint8_t dialog_token;
    struct osw_btm_retry_neigh *neighs;
    size_t n_neighs;
};

typedef bool
osw_btm_mux_frame_tx_schedule_fn_t(const char *phy_name,
                                   const char *vif_name,
                                   struct osw_drv_frame_tx_desc *desc);

struct osw_btm_sta_assoc_link {
    struct osw_hwaddr remote_sta_addr;
    struct osw_hwaddr local_sta_addr;
};

struct osw_btm_sta_assoc_links {
    struct osw_btm_sta_assoc_link array[OSW_STA_MAX_LINKS];
    struct osw_hwaddr local_mld_addr;
    size_t count;
};

struct osw_btm_sta_assoc {
    struct osw_btm_sta_assoc_links links;
    osw_sta_assoc_observer_t *obs;
};

struct osw_btm_sta {
    osw_btm_t *m;
    ds_tree_node_t node;
    ds_tree_t reqs;

    struct osw_hwaddr mac_addr;
    struct osw_btm_sta_assoc assoc;
    struct osw_token_pool_reference *pool_ref;
    int refcount;
};

struct osw_btm_obs_assoc {
    struct osw_hwaddr_list addrs;
    osw_sta_assoc_observer_t *obs;
};

struct osw_btm_obs {
    osw_btm_t *m;
    ds_tree_node_t node;
    struct osw_hwaddr sta_addr;
    struct osw_btm_obs_assoc assoc;
    osw_btm_obs_received_fn_t *received_fn;
    void *received_fn_priv;
};

struct osw_btm {
    ds_tree_t stas;
    ds_tree_t obs;
    struct osw_state_observer state_obs;
    osw_btm_mux_frame_tx_schedule_fn_t *mux_frame_tx_schedule_fn;
};

osw_btm_obs_t *
osw_btm_obs_alloc(osw_btm_t *m)
{
    if (m == NULL) return NULL;

    osw_btm_obs_t *o = CALLOC(1, sizeof(*o));
    o->m = m;
    ds_tree_insert(&m->obs, o, o);
    return o;
}

static void
osw_btm_obs_sta_changed_cb(void *priv, const osw_sta_assoc_entry_t *e, const osw_sta_assoc_event_e ev)
{
    struct osw_btm_obs *o = priv;
    const osw_sta_assoc_links_t *l = osw_sta_assoc_entry_get_active_links(e);

    osw_hwaddr_list_flush(&o->assoc.addrs);

    if (l->count > 0) {
        const struct osw_hwaddr *addr = osw_sta_assoc_entry_get_addr(e);
        osw_hwaddr_list_append(&o->assoc.addrs, addr);
    }

    osw_sta_assoc_links_append_remote_to(l, &o->assoc.addrs);
}

static osw_sta_assoc_observer_params_t *
osw_btm_obs_alloc_observer_params(osw_btm_obs_t *obs)
{
    if (osw_hwaddr_is_zero(&obs->sta_addr)) return NULL;
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_addr(p, &obs->sta_addr);
    osw_sta_assoc_observer_params_set_changed_fn(p, osw_btm_obs_sta_changed_cb, obs);
    return p;
}

static osw_sta_assoc_observer_t *
osw_btm_obs_alloc_sta_assoc_obs(osw_btm_obs_t *obs)
{
    osw_sta_assoc_t *am = OSW_MODULE_LOAD(osw_sta_assoc);
    osw_sta_assoc_observer_params_t *p = osw_btm_obs_alloc_observer_params(obs);
    return osw_sta_assoc_observer_alloc(am, p);
}

void
osw_btm_obs_set_sta_addr(osw_btm_obs_t *obs,
                         const struct osw_hwaddr *addr)
{
    if (obs == NULL) return;
    osw_sta_assoc_observer_drop(obs->assoc.obs);
    obs->sta_addr = *(addr ?: osw_hwaddr_zero());
    obs->assoc.obs = osw_hwaddr_is_zero(&obs->sta_addr)
                   ? NULL
                   : osw_btm_obs_alloc_sta_assoc_obs(obs);
}

void
osw_btm_obs_set_received_fn(osw_btm_obs_t *obs,
                            osw_btm_obs_received_fn_t *fn,
                            void *priv)
{
    if (obs == NULL) return;
    obs->received_fn = fn;
    obs->received_fn_priv = priv;
}

void
osw_btm_obs_drop(osw_btm_obs_t *obs)
{
    if (obs == NULL) return;
    osw_sta_assoc_observer_drop(obs->assoc.obs);
    ds_tree_remove(&obs->m->obs, obs);
    FREE(obs);
}

static void
osw_btm_obs_notify_received(osw_btm_t *m,
                            const osw_btm_resp_t *resp)
{
    osw_btm_obs_t *o;
    ds_tree_foreach(&m->obs, o) {
        const struct osw_hwaddr *addrs = o->assoc.addrs.list;
        const size_t count = o->assoc.addrs.count;
        if (osw_hwaddr_list_contains(addrs, count, &resp->sta_addr) == false) continue;
        if (o->received_fn == NULL) continue;
        o->received_fn(o->received_fn_priv, resp->status_code, resp->neighs, resp->n_neighs);
    }
}

static void
osw_btm_req_get_disassoc_imminent(const struct osw_btm_req_params *params,
                                  bool *disassoc_imminent,
                                  uint16_t *disassoc_timer)
{
    size_t i;
    *disassoc_imminent = params->disassoc_imminent;
    *disassoc_timer = params->disassoc_timer;
    for (i = 0; i < params->neigh_len; i ++) {
        const struct osw_btm_req_neigh *n = &params->neigh[i];
        if (n->disassoc_imminent) {
            *disassoc_imminent = true;
        }
        if (*disassoc_timer == 0 || *disassoc_timer > n->disassoc_timer) {
            *disassoc_timer = n->disassoc_timer ?: 1;
        }
    }
}

static bool
osw_btm_mbo_put_cell_pref(void **tail, ssize_t *rem, enum osw_btm_mbo_cell_preference pref)
{
    const uint8_t attr = C_IEEE80211_MBO_ATTR_CELL_PREF;
    switch (pref)
    {
        case OSW_BTM_MBO_CELL_PREF_NONE:
            break;
        case OSW_BTM_MBO_CELL_PREF_EXCLUDE_CELL:
            return buf_put_attr_u8(tail, rem, attr, C_IEEE80211_MBO_CELL_PREF_EXCLUDE);
        case OSW_BTM_MBO_CELL_PREF_AVOID_CELL:
            return buf_put_attr_u8(tail, rem, attr, C_IEEE80211_MBO_CELL_PREF_SHOULD_NOT_USE);
        case OSW_BTM_MBO_CELL_PREF_RECOMMEND_CELL:
            return buf_put_attr_u8(tail, rem, attr, C_IEEE80211_MBO_CELL_PREF_SHOULD_USE);
    }
    return true;
}

static bool
osw_btm_mbo_put_reason(void **tail, ssize_t *rem, enum osw_btm_mbo_reason reason)
{
    const uint8_t attr = C_IEEE80211_MBO_ATTR_BTM_REASON;
    switch (reason)
    {
        case OSW_BTM_MBO_REASON_NONE:
            break;
        case OSW_BTM_MBO_REASON_LOW_RSSI:
            return buf_put_attr_u8(tail, rem, attr, C_IEEE80211_MBO_BTM_REASON_LOW_RSSI);
    }
    return true;
}

static bool
osw_btm_mbo_put(void **tail, ssize_t *rem, const struct osw_btm_req_params *req_params)
{
    void *old = *tail;
    bool ok = true;

    ok &= buf_put_u8(tail, rem, C_IEEE80211_EID_VENDOR);
    uint8_t *vendor_len = buf_pull(tail, rem, sizeof(uint8_t));
    ok &= buf_put_u8(tail, rem, C_IEEE80211_WFA_OUI_BYTE0);
    ok &= buf_put_u8(tail, rem, C_IEEE80211_WFA_OUI_BYTE1);
    ok &= buf_put_u8(tail, rem, C_IEEE80211_WFA_OUI_BYTE2);
    ok &= buf_put_u8(tail, rem, C_IEEE80211_MBO_OUI_TYPE);
    const void *start = *tail;
    ok &= osw_btm_mbo_put_cell_pref(tail, rem, req_params->mbo.cell_preference);
    ok &= osw_btm_mbo_put_reason(tail, rem, req_params->mbo.reason);
    const void *end = *tail;
    if (!ok) return false;

    *vendor_len = buf_len(vendor_len, *tail) - 1;
    if (start == end) {
        ok &= buf_restore(tail, rem, old);
    }

    return ok;
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

    LOGT("osw: btm: build: "OSW_HWADDR_FMT": frame_size=%zu",
         OSW_HWADDR_ARG(sta_addr),
         frame_buf_size);

    void *tail = frame_buf;
    ssize_t rem = frame_buf_size;

    bool disassoc_imminent = false;
    uint16_t disassoc_timer = 0;
    osw_btm_req_get_disassoc_imminent(req_params, &disassoc_imminent, &disassoc_timer);

    uint8_t options = 0;
    options |= (req_params->neigh_len > 0 ? C_IEEE80211_BTM_REQ_PREF_CAND : 0);
    options |= (req_params->abridged == true ? C_IEEE80211_BTM_REQ_ABRIDGED : 0);
    options |= (disassoc_imminent == true ? C_IEEE80211_BTM_REQ_DISASSOC_IMMINENT : 0);
    options |= (req_params->bss_term == true ? C_IEEE80211_BTM_REQ_BSS_TERM : 0);

    uint16_t fc = 0;
    fc |= C_MASK_PREP(C_IEEE80211_FC_TYPE, C_IEEE80211_FC_TYPE_MGMT);
    fc |= C_MASK_PREP(C_IEEE80211_FC_SUBTYPE, C_IEEE80211_FC_MGMT_SUBTYPE_ACTION);

    bool ok = true;
    ok &= buf_put_u16(&tail, &rem, htole16(fc));
    ok &= buf_put_u16(&tail, &rem, htole16(60)); /* duration */
    ok &= buf_put_ptr(&tail, &rem, sta_addr);
    ok &= buf_put_ptr(&tail, &rem, bssid);
    ok &= buf_put_ptr(&tail, &rem, bssid);
    ok &= buf_put_u16(&tail, &rem, htole16(0)); /* seq */

    ok &= buf_put_u8(&tail, &rem, C_IEEE80211_ACTION_CAT_WNM);
    ok &= buf_put_u8(&tail, &rem, C_IEEE80211_WNM_BTM_REQ);
    ok &= buf_put_u8(&tail, &rem, dialog_token);
    ok &= buf_put_u8(&tail, &rem, options);
    ok &= buf_put_u16(&tail, &rem, htole16(disassoc_timer));
    ok &= buf_put_u8(&tail, &rem, req_params->valid_int);

    size_t i;
    for (i = 0; i < req_params->neigh_len; i ++) {
        const struct osw_btm_req_neigh *n = &req_params->neigh[i];

        ok &= buf_put_u8(&tail, &rem, C_IEEE80211_EID_NEIGH_REPORT);
        uint8_t *n_len = buf_pull(&tail, &rem, sizeof(uint8_t));
        ok &= buf_put(&tail, &rem, &n->bssid, 6);
        ok &= buf_put_u32(&tail, &rem, htole32(n->bssid_info));
        ok &= buf_put_u8(&tail, &rem, n->op_class);
        ok &= buf_put_u8(&tail, &rem, n->channel);
        ok &= buf_put_u8(&tail, &rem, n->phy_type);
        ok &= buf_put_attr_u8(&tail, &rem, C_IEEE80211_NR_EID_BSS_TRANS_CAND_PREF, n->btmpreference);

        if (n_len) *n_len = buf_len(n_len, tail) - 1;
    }

    ok &= osw_btm_mbo_put(&tail, &rem, req_params);

    LOGT("osw: btm: build: "OSW_HWADDR_FMT": remaining=%zd",
         OSW_HWADDR_ARG(sta_addr),
         rem);

    if (WARN_ON(!ok)) {
        return -1;
    }

    return buf_len(frame_buf, tail);
}

void
osw_btm_req_params_log(const struct osw_btm_req_params *params)
{
    size_t i;
    bool disassoc_imminent = false;
    uint16_t disassoc_timer = 0;

    osw_btm_req_get_disassoc_imminent(params, &disassoc_imminent, &disassoc_timer);

    LOGI("osw: btm: parameters:"
         " valid_int %"PRIu8
         " abridged %d"
         " disassoc_imminent %d"
         " disassoc_timer %hu"
         " bss_term %d"
         " neighs %zu",
         params->valid_int,
         params->abridged,
         disassoc_imminent,
         disassoc_timer,
         params->bss_term,
         params->neigh_len);

    for (i = 0; i < params->neigh_len; i ++) {
        const struct osw_btm_req_neigh *n = &params->neigh[i];
        LOGI("osw: btm: parameters: neighbor[%zu]:"
             " bssid "OSW_HWADDR_FMT
             " info 0x%08"PRIx32
             " chan %"PRIu8
             " disassoc_imminent %d"
             " disassoc_timer %"PRIu16
             " opclass %"PRIu8
             " phytype %"PRIu8,
             i,
             OSW_HWADDR_ARG(&n->bssid),
             n->bssid_info,
             n->channel,
             n->disassoc_imminent,
             n->disassoc_timer,
             n->op_class,
             n->phy_type);
    }
}

static void
osw_btm_sta_changed_cb(void *priv, const osw_sta_assoc_entry_t *e, const osw_sta_assoc_event_e ev)
{
    struct osw_btm_sta *sta = priv;
    const osw_sta_assoc_links_t *l = osw_sta_assoc_entry_get_active_links(e);
    sta->assoc.links.count = l->count;
    sta->assoc.links.local_mld_addr = *(osw_sta_assoc_entry_get_local_mld_addr(e) ?: osw_hwaddr_zero());
    size_t i;
    for (i = 0; i < sta->assoc.links.count; i++) {
        if (WARN_ON(i >= ARRAY_SIZE(sta->assoc.links.array))) break;
        sta->assoc.links.array[i].local_sta_addr = l->links[i].local_sta_addr;
        sta->assoc.links.array[i].remote_sta_addr = l->links[i].remote_sta_addr;
    }
}

static enum osw_btm_req_result
osw_btm_tx_result_to_result(enum osw_frame_tx_result result)
{
    switch (result) {
        case OSW_FRAME_TX_RESULT_SUBMITTED:
            return OSW_BTM_REQ_RESULT_SENT;
        case OSW_FRAME_TX_RESULT_FAILED:
            return OSW_BTM_REQ_RESULT_FAILED;
        case OSW_FRAME_TX_RESULT_DROPPED:
            return OSW_BTM_REQ_RESULT_FAILED;
    }
    return OSW_BTM_REQ_RESULT_FAILED;
}

static bool
osw_btm_req_link_expects_tx_result(const osw_btm_req_link_t *l)
{
    switch (l->req->state) {
        case OSW_BTM_REQ_STATE_INIT:
            break;
        case OSW_BTM_REQ_STATE_READY:
            break;
        case OSW_BTM_REQ_STATE_SUBMITTED:
            return (l->frame_tx_desc != NULL);
        case OSW_BTM_REQ_STATE_COMPLETED:
            break;
    }
    return false;
}

static const char *
osw_btm_req_state_to_cstr(enum osw_btm_req_state s)
{
    switch (s) {
        case OSW_BTM_REQ_STATE_INIT:
            return "init";
        case OSW_BTM_REQ_STATE_READY:
            return "ready";
        case OSW_BTM_REQ_STATE_SUBMITTED:
            return "submitted";
        case OSW_BTM_REQ_STATE_COMPLETED:
            return "completed";
    }
    return "";
}

static const char *
osw_btm_req_result_to_cstr(enum osw_btm_req_result r)
{
    switch (r) {
        case OSW_BTM_REQ_RESULT_SENT:
            return "sent";
        case OSW_BTM_REQ_RESULT_FAILED:
            return "failed";
    }
    return "";
}

static bool
osw_btm_req_is_completed(osw_btm_req_t *r, enum osw_btm_req_result *rr)
{
    osw_btm_req_link_t *l;
    size_t sent = 0;
    ds_tree_foreach(&r->links, l) {
        if (l->frame_tx_desc != NULL) {
            return false;
        }
        switch (l->result) {
            case OSW_BTM_REQ_RESULT_SENT: sent++; break;
            case OSW_BTM_REQ_RESULT_FAILED: break;
        }
    }
    *rr = sent ? OSW_BTM_REQ_RESULT_SENT : OSW_BTM_REQ_RESULT_FAILED;
    return true;
}

static void
osw_btm_drv_frame_tx_result_cb(struct osw_drv_frame_tx_desc *tx_desc,
                               enum osw_frame_tx_result tr,
                               void *caller_priv)
{
    osw_btm_req_link_t *l = caller_priv;
    osw_btm_req_t *r = l->req;

    if (osw_btm_req_link_expects_tx_result(l) == false) {
        LOGD(LOG_PREFIX_REQ(r, "unexpected tx result while in %s state",
             osw_btm_req_state_to_cstr(r->state)));
        return;
    }

    l->result = osw_btm_tx_result_to_result(tr);
    osw_drv_frame_tx_desc_free(l->frame_tx_desc);
    l->frame_tx_desc = NULL;

    enum osw_btm_req_result rr;
    if (osw_btm_req_is_completed(r, &rr) == false)
        return;

    r->state = OSW_BTM_REQ_STATE_COMPLETED;
    if (r->completed_fn != NULL) {
        r->completed_fn(r->completed_fn_priv, rr);
    }

    LOGD(LOG_PREFIX_REQ(r, "completed: result=%s (from:%s)",
         osw_btm_req_result_to_cstr(rr),
         osw_frame_tx_result_to_cstr(tr)));
}

static bool
osw_btm_req_tx_build(osw_btm_req_t *r)
{
    /* Arguably, this is not correct. The driver really
     * should be able to provide means to sending out MLD
     * addressed frames by automatically submitting them
     * onto the best/available link. Userspace has either no
     * means, or its way too racy, to know what link is
     * active and to select which one to use.
     *
     * For now, just spray-and-pray. This doesn't need to be
     * perfect, it needs to mostly work. 11be link removal
     * handling can't be done in userspace properly anyway.
     *
     * FIXME: Revisit later to allow submitting MLD
     * addressed frames unto mld_if_name perhaps?
     */
    size_t i;
    for (i = 0; i < r->sta->assoc.links.count; i++) {
        const struct osw_btm_sta_assoc_link *link = &r->sta->assoc.links.array[i];
        const struct osw_hwaddr *bssid = &link->local_sta_addr;
        const struct osw_hwaddr *sta_addr = &link->remote_sta_addr;
        const struct osw_state_vif_info *vif_info = osw_state_vif_lookup_by_mac_addr(bssid);
        if (WARN_ON(vif_info == NULL)) return false;

        osw_btm_req_link_t *l = CALLOC(1, sizeof(*l));
        ds_tree_insert(&r->links, l, l);
        l->req = r;
        l->vif_name = STRDUP(vif_info->vif_name);
        l->phy_name = STRDUP(vif_info->phy->phy_name);
        l->frame_len = osw_btm_build_frame(r->params,
                                           sta_addr,
                                           bssid,
                                           r->dialog_token,
                                           l->frame_buf,
                                           sizeof(l->frame_buf));
        if (l->frame_len < 0) return false;

        l->frame_tx_desc = osw_drv_frame_tx_desc_new(osw_btm_drv_frame_tx_result_cb, l);
        osw_drv_frame_tx_desc_set_frame(l->frame_tx_desc, l->frame_buf, l->frame_len);
    }
    return true;
}

static void
osw_btm_req_tx_submit(osw_btm_req_t *r)
{
    /* State is set before calling schedule_fn because the
     * result callback may be fired before returning.
     */
    r->state = OSW_BTM_REQ_STATE_SUBMITTED;

    osw_btm_req_link_t *l;
    ds_tree_foreach(&r->links, l) {
        l->req->sta->m->mux_frame_tx_schedule_fn(l->phy_name, l->vif_name, l->frame_tx_desc);
        LOGI(LOG_PREFIX_REQ(r, "%s/%s: submitted: token=%d",
                            l->phy_name,
                            l->vif_name,
                            r->dialog_token));
    }
}

static void
osw_btm_req_drop_links(osw_btm_req_t *r)
{
    osw_btm_req_link_t *l;
    while ((l = ds_tree_remove_head(&r->links)) != NULL) {
        osw_drv_frame_tx_desc_free(l->frame_tx_desc);
        FREE(l->phy_name);
        FREE(l->vif_name);
        FREE(l);
    }
}

static bool
osw_btm_req_tx(osw_btm_req_t *r)
{
    r->dialog_token = osw_token_pool_fetch_token(r->sta->pool_ref);
    if (r->dialog_token == OSW_TOKEN_INVALID) {
        LOGW(LOG_PREFIX_REQ(r, "cannot tx: out of tokens"));
        return false;
    }

    if (osw_btm_req_tx_build(r) == false) {
        LOGW(LOG_PREFIX_REQ(r, "cannot tx: failed to build"));
        osw_btm_req_drop_links(r);
        osw_token_pool_free_token(r->sta->pool_ref, r->dialog_token);
        r->dialog_token = OSW_TOKEN_INVALID;
        return false;
    }

    osw_btm_req_tx_submit(r);
    return true;
}

osw_btm_sta_t *
osw_btm_sta_alloc(osw_btm_t *m, const struct osw_hwaddr *addr)
{
    if (m == NULL) return NULL;
    if (WARN_ON(addr == NULL)) return NULL;

    osw_btm_sta_t *sta = ds_tree_find(&m->stas, addr);
    if (sta) {
        sta->refcount++;
        return sta;
    }

    sta = CALLOC(1, sizeof(*sta));
    ds_tree_init(&sta->reqs, ds_void_cmp, osw_btm_req_t, node);
    sta->refcount = 1;
    sta->m = m;
    sta->mac_addr = *addr;
    sta->pool_ref = osw_token_pool_ref_get(NULL, addr);

    osw_sta_assoc_t *am = OSW_MODULE_LOAD(osw_sta_assoc);
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_addr(p, addr);
    osw_sta_assoc_observer_params_set_changed_fn(p, osw_btm_sta_changed_cb, sta);
    sta->assoc.obs = osw_sta_assoc_observer_alloc(am, p);

    ds_tree_insert(&m->stas, sta, &sta->mac_addr);

    return sta;
}

void
osw_btm_sta_drop(osw_btm_sta_t *sta)
{
    if (sta == NULL) return;
    if (WARN_ON(sta->refcount == 0)) return;
    sta->refcount--;
    if (sta->refcount > 0) return;
    if (WARN_ON(ds_tree_is_empty(&sta->reqs) == false)) return;

    osw_sta_assoc_observer_drop(sta->assoc.obs);
    osw_token_pool_ref_free(sta->pool_ref);

    ds_tree_remove(&sta->m->stas, sta);
    FREE(sta);
}

osw_btm_req_t *
osw_btm_req_alloc(osw_btm_sta_t *sta)
{
    if (sta == NULL) return NULL;

    osw_btm_req_t *r = CALLOC(1, sizeof(*r));
    ds_tree_init(&r->links, ds_void_cmp, osw_btm_req_link_t, node);
    r->sta = sta;
    r->dialog_token = OSW_TOKEN_INVALID;
    ds_tree_insert(&sta->reqs, r, r);
    return r;
}

void
osw_btm_req_drop(osw_btm_req_t *r)
{
    if (r == NULL) return;

    osw_btm_req_drop_links(r);
    osw_token_pool_free_token(r->sta->pool_ref, r->dialog_token);
    ds_tree_remove(&r->sta->reqs, r);
    FREE(r->params);
    FREE(r);
}

static bool
osw_btm_req_is_configurable(const osw_btm_req_t *r)
{
    switch (r->state) {
        case OSW_BTM_REQ_STATE_INIT:
        case OSW_BTM_REQ_STATE_READY:
            break;
        case OSW_BTM_REQ_STATE_SUBMITTED:
        case OSW_BTM_REQ_STATE_COMPLETED:
            LOGD(LOG_PREFIX_REQ(r, "already in %s state",
                 osw_btm_req_state_to_cstr(r->state)));
            return false;
    }
    return true;
}

static bool
osw_btm_req_is_ready(const osw_btm_req_t *r)
{
    switch (r->state) {
        case OSW_BTM_REQ_STATE_READY:
            return true;
        case OSW_BTM_REQ_STATE_INIT:
        case OSW_BTM_REQ_STATE_SUBMITTED:
        case OSW_BTM_REQ_STATE_COMPLETED:
            break;
    }
    return false;
}

bool
osw_btm_req_set_completed_fn(osw_btm_req_t *r, osw_btm_req_completed_fn_t *fn, void *priv)
{
    if (r == NULL) return false;
    LOGD(LOG_PREFIX_REQ(r, "setting: completed_fn: '%p' -> '%p'",
         r->completed_fn,
         fn));
    if (osw_btm_req_is_configurable(r) == false) return false;
    r->completed_fn = fn;
    r->completed_fn_priv = priv;
    return true;
}

bool
osw_btm_req_set_response_fn(osw_btm_req_t *r, osw_btm_req_response_fn_t *fn, void *priv)
{
    if (r == NULL) return false;
    LOGD(LOG_PREFIX_REQ(r, "setting: response_fn: '%p' -> '%p'",
         r->response_fn,
         fn));
    if (osw_btm_req_is_configurable(r) == false) return false;
    r->response_fn = fn;
    r->response_fn_priv = priv;
    return true;
}

bool
osw_btm_req_set_params(osw_btm_req_t *r, const struct osw_btm_req_params *params)
{
    if (r == NULL) return false;

    LOGD(LOG_PREFIX_REQ(r, "setting: params: %p", params));
    if (osw_btm_req_is_configurable(r) == false) return false;

    FREE(r->params);
    r->params = NULL;
    r->state = OSW_BTM_REQ_STATE_INIT;
    if (params == NULL) return true;

    r->params = MEMNDUP(params, sizeof(*params));
    r->state = OSW_BTM_REQ_STATE_READY;
    return true;
}

bool
osw_btm_req_submit(osw_btm_req_t *r)
{
    if (r == NULL) return false;
    if (osw_btm_req_is_ready(r) == false) return false;
    if (r->sta->assoc.links.count == 0) return false;

    /* This is intended to, possibly in the future, offload
     * spawning multiple BTM Req frames per MLO link when
     * Affiliated APs are being removed. Current 802.11be
     * drafts are a bit unclear on some of the details on
     * that, and whether it'll be even feasible to generate
     * BTM Req outside of SME of the WLAN driver itself is
     * still an open question. In such case the idea is to
     * move the r->params to sta->params and to program it
     * in the WLAN driver so it can gracefully provide AP
     * Candidates properly autonomously when needed.
     */

    return osw_btm_req_tx(r);
}

uint8_t
osw_btm_resp_get_status(const osw_btm_resp_t *resp)
{
    if (resp == NULL) return 0;
    return resp->status_code;
}

static void
osw_btm_req_report_response(struct osw_btm *m, const osw_btm_resp_t *resp)
{
    /* FIXME: This isn't really efficient, but doing this
     * efficiently requires more extensive rework of a lot
     * of code in here. This isn't performance critical, so
     * it should be fine for now.
     */
    const struct osw_hwaddr *bssid = NULL;
    struct osw_btm_sta *sta;
    ds_tree_foreach(&m->stas, sta) {
        const osw_sta_assoc_entry_t *e = osw_sta_assoc_observer_get_entry(sta->assoc.obs);
        const osw_sta_assoc_links_t *links = osw_sta_assoc_entry_get_active_links(e);
        const osw_sta_assoc_link_t *link = osw_sta_assoc_links_lookup(links, bssid, &resp->sta_addr);
        if (link == NULL) continue;

        osw_btm_req_t *req;
        ds_tree_foreach(&sta->reqs, req) {
            if (req->dialog_token != resp->dialog_token) continue;
            if (req->response_fn == NULL) continue;
            req->response_fn(req->response_fn_priv, resp);
        }
    }
}

static bool
osw_btm_resp_parse_nr(const void **buf,
                      ssize_t *rem,
                      struct osw_btm_req_neigh *n,
                      uint8_t *preference)
{
    uint32_t info_le32;

    if (!buf_get_into(buf, rem, &n->bssid)) return false;
    if (!buf_get_u32(buf, rem, &info_le32)) return false;
    if (!buf_get_u8(buf, rem, &n->op_class)) return false;
    if (!buf_get_u8(buf, rem, &n->channel)) return false;
    if (!buf_get_u8(buf, rem, &n->phy_type)) return false;
    n->bssid_info = le32toh(info_le32);

    uint8_t aid;
    uint8_t alen;
    const void *attr;

    if (!buf_get_u8(buf, rem, &aid)) return true;
    if (!buf_get_u8(buf, rem, &alen)) return false;
    if (!buf_get_as_ptr(buf, rem, &attr, alen)) return false;

    ssize_t attr_len = alen;
    if (!buf_get_u8(&attr, &attr_len, preference)) return false;

    return true;
}

static bool
osw_btm_resp_get_neighs(const void **buf,
                        ssize_t *rem,
                        struct osw_btm_retry_neigh **list,
                        size_t *count)
{
    while (*rem > 0)
    {
        uint8_t eid;
        uint8_t elen;
        const void *elem;

        if (!buf_get_u8(buf, rem, &eid)) break;
        if (!buf_get_u8(buf, rem, &elen)) break;
        if (!buf_get_as_ptr(buf, rem, &elem, elen)) break;
        if (eid != C_IEEE80211_EID_NEIGH_REPORT) continue;

        struct osw_btm_retry_neigh n;
        MEMZERO(n);
        ssize_t len = elen;
        if (!osw_btm_resp_parse_nr(&elem, &len, &n.neigh, &n.preference)) continue;

        const size_t last = (*count)++;
        const size_t size = sizeof(**list) * (*count);
        (*list) = REALLOC(*list, size);
        (*list)[last] = n;
    }
    WARN_ON(*rem != 0);
    return true;
}

static bool
osw_btm_resp_parse(const void *buf,
                   ssize_t rem,
                   osw_btm_resp_t *resp)
{
    uint16_t fc_le16;
    uint16_t dur_le16;
    uint16_t seq_le16;
    struct osw_hwaddr ta;
    struct osw_hwaddr bssid;

    if (!buf_get_u16(&buf, &rem, &fc_le16)) return false;
    if (!buf_get_u16(&buf, &rem, &dur_le16)) return false;
    if (!buf_get_into(&buf, &rem, &resp->sta_addr)) return false;
    if (!buf_get_into(&buf, &rem, &ta)) return false;
    if (!buf_get_into(&buf, &rem, &bssid)) return false;
    if (!buf_get_u16(&buf, &rem, &seq_le16)) return false;

    const uint16_t fc = le16toh(fc_le16);
    const uint16_t type = C_MASK_GET(C_IEEE80211_FC_TYPE, fc);
    const uint16_t subtype = C_MASK_GET(C_IEEE80211_FC_SUBTYPE, fc);

    if (type != C_IEEE80211_FC_TYPE_MGMT) return false;
    if (subtype != C_IEEE80211_FC_MGMT_SUBTYPE_ACTION) return false;

    uint8_t category;
    uint8_t action;
    uint8_t bss_term_delay;

    if (!buf_get_u8(&buf, &rem, &category)) return false;
    if (!buf_get_u8(&buf, &rem, &action)) return false;
    if (category != C_IEEE80211_ACTION_CAT_WNM) return false;
    if (action != C_IEEE80211_WNM_BTM_RESP) return false;

    if (!buf_get_u8(&buf, &rem, &resp->dialog_token)) return false;
    if (!buf_get_u8(&buf, &rem, &resp->status_code)) return false;
    if (!buf_get_u8(&buf, &rem, &bss_term_delay)) return false;

    if (resp->status_code == C_IEEE80211_BTM_RESP_STATUS_ACCEPT) {
        if (!buf_get_into(&buf, &rem, &resp->target_bssid)) return false;
    }

    if (!osw_btm_resp_get_neighs(&buf, &rem, &resp->neighs, &resp->n_neighs)) return false;

    WARN_ON(rem != 0);
    return true;
}

static void
osw_btm_resp_frame_rx_cb(struct osw_state_observer *obs,
                         const struct osw_state_vif_info *vif,
                         const uint8_t *data,
                         size_t len)
{
    if (WARN_ON(data == NULL)) return;
    if (WARN_ON(vif == NULL)) return;
    if (WARN_ON(vif->vif_name == NULL)) return;

    osw_btm_t *m = container_of(obs, typeof(*m), state_obs);
    osw_btm_resp_t resp;
    MEMZERO(resp);

    const bool ok = osw_btm_resp_parse(data, len, &resp);
    if (ok)
    {
        LOGI(LOG_PREFIX("resp: "OSW_HWADDR_FMT": token=%hhu target="OSW_HWADDR_FMT" status=%hhu neighs=%zu",
                        OSW_HWADDR_ARG(&resp.sta_addr),
                        resp.dialog_token,
                        OSW_HWADDR_ARG(&resp.target_bssid),
                        resp.status_code,
                        resp.n_neighs));
        osw_btm_req_report_response(m, &resp);
        osw_btm_obs_notify_received(m, &resp);
    }
    FREE(resp.neighs);
}

static osw_btm_t *
osw_btm_alloc(void)
{
    osw_btm_t *m = CALLOC(1, sizeof(*m));
    ds_tree_init(&m->stas, (ds_key_cmp_t *)osw_hwaddr_cmp, osw_btm_sta_t, node);
    ds_tree_init(&m->obs, ds_void_cmp, osw_btm_obs_t, node);
    m->state_obs.name = __FILE__;
    m->state_obs.vif_frame_rx_fn = osw_btm_resp_frame_rx_cb;
    m->mux_frame_tx_schedule_fn = osw_mux_frame_tx_schedule;
    osw_state_register_observer(&m->state_obs);
    return m;
}

OSW_MODULE(osw_btm)
{
    OSW_MODULE_LOAD(osw_token);
    return osw_btm_alloc();
}

#include "osw_btm_ut.c"
