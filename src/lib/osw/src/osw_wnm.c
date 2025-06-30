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

#include "osw_sta_assoc.h"
#include <endian.h>
#include <os.h>
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
#include <osw_wnm.h>

#define LOG_WNM_PREFIX          "osw: wnm: "
#define LOG_PREFIX(m, fmt, ...) LOG_WNM_PREFIX "" fmt, ##__VA_ARGS__

#define LOG_PREFIX_STA(sta, fmt, ...)                                     \
    LOG_PREFIX(                                                           \
            (sta) ? (sta)->m : NULL,                                      \
            LOG_WNM_PREFIX "sta: " OSW_HWADDR_FMT ": " fmt,               \
            OSW_HWADDR_ARG((sta) ? &(sta)->sta_addr : osw_hwaddr_zero()), \
            ##__VA_ARGS__)

#define DOT11_WNM_NOT_REQ_TYPE_FW_UPDATE_NOTIFICATION 0x00
#define DOT11_WNM_NOT_REQ_TYPE_BEACON_PROT_FAILURE    0x02
#define DOT11_WNM_NOT_REQ_TYPE_VENDOR_SPECIFIC        0xdd
#define DOT11_IE_SUBELEMENT_ID_VENDOR_SPECIFIC        0xdd

#define WFA_VENDOR_SPECIFIC_OUI_0 0x50
#define WFA_VENDOR_SPECIFIC_OUI_1 0x6f
#define WFA_VENDOR_SPECIFIC_OUI_2 0x9a
#define WFA_VENDOR_SPECIFIC_OUI                                                      \
    (uint32_t)(                                                                      \
            ((uint32_t)(0x00 << 24)) | ((uint32_t)(WFA_VENDOR_SPECIFIC_OUI_0 << 16)) \
            | ((uint32_t)(WFA_VENDOR_SPECIFIC_OUI_1 << 8)) | ((uint32_t)(WFA_VENDOR_SPECIFIC_OUI_2 << 0)))

#define WFA_VENDOR_SPECIFIC_OUI_HS20_INDICATION               0x10
#define WFA_VENDOR_SPECIFIC_OUI_HS20_SUBSCRIPTION_REMEDIATION 0x00
#define WFA_VENDOR_SPECIFIC_OUI_TYPE_MBO                      0x16

#define state_obs_to_m(obs_) container_of(obs, struct osw_wnm, state_obs);

struct osw_wnm_sta_observer_params
{
    struct osw_hwaddr sta_addr;
    osw_wnm_sta_observer_notify_fn_t *notify_fn;
    void *notify_fn_priv;
};

struct osw_wnm_sta_observer
{
    ds_tree_node_t node_m;
    ds_tree_node_t node_entry;
    struct osw_wnm_sta_observer_params p;
    struct osw_wnm_sta *sta;
};

struct osw_wnm_sta_params
{
    bool mbo_capable;
    enum osw_sta_cell_cap mbo_cell_capability;
};

struct osw_wnm_sta_link
{
    ds_tree_node_t node_sta;
    ds_tree_node_t node_m;
    struct osw_hwaddr link_addr;
    struct osw_wnm_sta *sta;
};

struct osw_wnm_sta
{
    ds_tree_node_t node;
    struct osw_wnm *m;
    ds_tree_t links;
    ds_tree_t observers;
    struct osw_hwaddr sta_addr;
    struct osw_wnm_sta_params params;
};

struct osw_wnm
{
    ds_tree_t sta_tree;
    ds_tree_t links;
    struct osw_state_observer state_obs;
    osw_sta_assoc_observer_t *sta_assoc_observer;
};

static bool osw_wnm_frame_is_action(const struct osw_drv_dot11_frame *frame)
{
    ASSERT(frame != NULL, "");
    const struct osw_drv_dot11_frame_header *header = &frame->header;
    const bool is_mgmt = ((le16toh(header->frame_control) & DOT11_FRAME_CTRL_TYPE_MASK) == DOT11_FRAME_CTRL_TYPE_MGMT);
    const bool is_action =
            ((le16toh(header->frame_control) & DOT11_FRAME_CTRL_SUBTYPE_MASK) == DOT11_FRAME_CTRL_SUBTYPE_ACTION);
    return (is_mgmt && is_action);
}

static bool osw_wnm_action_frame_is_wnm_notification_request(const struct osw_drv_dot11_frame_action *action)
{
    ASSERT(action != NULL, "");
    const struct osw_drv_dot11_frame_action_wnm_req *wnm_notif_req = &action->u.wnm_notif_req;
    const bool is_wnm = (action->category == DOT11_ACTION_CATEGORY_WNM_CODE);
    const bool is_wnm_notif_req = (wnm_notif_req->action == DOT11_WNM_NOTIF_REQUEST_IE_ACTION_CODE);
    return (is_wnm && is_wnm_notif_req);
}

static struct osw_wnm_sta *osw_wnm_sta_lookup(struct osw_wnm *m, const struct osw_hwaddr *sta_addr)
{
    if (m == NULL) return NULL;
    if (sta_addr == NULL) return NULL;
    struct osw_wnm_sta *sta = ds_tree_find(&m->sta_tree, sta_addr);
    return sta;
}

static struct osw_wnm_sta *osw_wnm_sta_lookup_by_link_addr(struct osw_wnm *m, const struct osw_hwaddr *link_addr)
{
    if (m == NULL) return NULL;
    if (link_addr == NULL) return NULL;
    struct osw_wnm_sta_link *link = ds_tree_find(&m->links, link_addr);
    return link ? link->sta : NULL;
}

static struct osw_wnm_sta_link *osw_wnm_sta_link_create(struct osw_wnm_sta *sta, const struct osw_hwaddr *link_addr)
{
    struct osw_wnm_sta_link *link = CALLOC(1, sizeof(*link));
    link->sta = sta;
    link->link_addr = *link_addr;
    ds_tree_insert(&sta->links, link, &link->link_addr);
    ds_tree_insert(&sta->m->links, link, &link->link_addr);
    return link;
}

static void osw_wnm_sta_link_drop(struct osw_wnm_sta_link *link)
{
    if (link == NULL) return;
    if (WARN_ON(link->sta == NULL)) return;
    if (WARN_ON(link->sta->m == NULL)) return;
    ds_tree_remove(&link->sta->links, link);
    ds_tree_remove(&link->sta->m->links, link);
    FREE(link);
}

static struct osw_wnm_sta *osw_wnm_sta_alloc(struct osw_wnm *m, const struct osw_hwaddr *sta_addr)
{
    if (WARN_ON(m == NULL)) return NULL;
    if (WARN_ON(sta_addr == NULL)) return NULL;

    struct osw_wnm_sta *sta = CALLOC(1, sizeof(*sta));

    sta->m = m;
    sta->sta_addr = *sta_addr;
    ds_tree_init(&sta->observers, ds_void_cmp, struct osw_wnm_sta_observer, node_entry);
    ds_tree_init(&sta->links, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_wnm_sta_link, node_sta);

    LOGT(LOG_PREFIX_STA(sta, "allocating"));
    ds_tree_insert(&m->sta_tree, sta, &sta->sta_addr);
    return sta;
}

static void osw_wnm_sta_gc(struct osw_wnm_sta *sta)
{
    if (WARN_ON(sta == NULL)) return;
    if (WARN_ON(sta->m == NULL)) return;

    if (ds_tree_is_empty(&sta->observers) == false)
    {
        LOGT(LOG_PREFIX_STA(sta, "not dropping - observers exist"));
        /* TODO set timer to re-evaluate later on */
        return;
    }

    if (ds_tree_is_empty(&sta->links) == false)
    {
        LOGT(LOG_PREFIX_STA(sta, "not dropping - links exist"));
        /* TODO set timer to re-evaluate later on */
        return;
    }

    LOGT(LOG_PREFIX_STA(sta, "dropping"));
    ds_tree_remove(&sta->m->sta_tree, sta);
    FREE(sta);
}

static void osw_wnm_sta_observer_notify(struct osw_wnm_sta_observer *obs)
{
    if (obs == NULL) return;
    if (obs->sta == NULL) return;
    if (obs->p.notify_fn == NULL) return;
    obs->p.notify_fn(obs->p.notify_fn_priv, obs->sta);
}

void osw_wnm_sta_attach_observer(struct osw_wnm_sta *sta, struct osw_wnm_sta_observer *obs)
{
    if (WARN_ON(sta == NULL)) return;
    if (WARN_ON(obs == NULL)) return;
    if (WARN_ON(obs->sta != NULL)) return;

    obs->sta = sta;
    ds_tree_insert(&sta->observers, obs, obs);
    osw_wnm_sta_observer_notify(obs);
}

static void osw_wnm_sta_detach_observer(struct osw_wnm_sta_observer *obs)
{
    if (WARN_ON(obs == NULL)) return;
    if (WARN_ON(obs->sta == NULL)) return;

    ds_tree_remove(&obs->sta->observers, obs);
    osw_wnm_sta_gc(obs->sta);
    obs->sta = NULL;
}

struct osw_wnm_sta_observer_params *osw_wnm_sta_observer_params_alloc(void)
{
    struct osw_wnm_sta_observer_params *p = CALLOC(1, sizeof(*p));
    return p;
}

struct osw_wnm_sta_observer *osw_wnm_sta_observer_alloc(struct osw_wnm *m, struct osw_wnm_sta_observer_params *p)
{
    if (m == NULL) goto err;
    if (p == NULL) goto err;

    if (WARN_ON(osw_hwaddr_is_zero(&p->sta_addr) == true)) goto err;

    /* Observer can be allocated _before_ we create STA
     * in the internal structures. Create empty STA here
     * and register observer anyway.
     */
    struct osw_wnm_sta *sta = osw_wnm_sta_lookup(m, &p->sta_addr) ?: osw_wnm_sta_alloc(m, &p->sta_addr);
    struct osw_wnm_sta_observer *o = CALLOC(1, sizeof(*o));
    o->p = *p;
    FREE(p);
    osw_wnm_sta_attach_observer(sta, o);
    return o;
err:
    FREE(p);
    return NULL;
}

void osw_wnm_sta_observer_params_set_changed_fn(
        osw_wnm_sta_observer_params_t *p,
        osw_wnm_sta_observer_notify_fn_t *fn,
        void *priv)
{
    if (p == NULL) return;
    p->notify_fn = fn;
    p->notify_fn_priv = priv;
}

void osw_wnm_sta_observer_params_set_addr(osw_wnm_sta_observer_params_t *p, const struct osw_hwaddr *sta_addr)
{
    if (p == NULL) return;
    p->sta_addr = *(sta_addr ?: osw_hwaddr_zero());
}

void osw_wnm_sta_observer_drop(osw_wnm_sta_observer_t *obs)
{
    if (obs == NULL) return;
    if (WARN_ON(obs->sta == NULL)) return;

    osw_wnm_sta_detach_observer(obs);
}

bool osw_wnm_sta_is_mbo_capable(const osw_wnm_sta_t *sta)
{
    if (sta == NULL) return false;
    return sta->params.mbo_capable;
}

enum osw_sta_cell_cap osw_wnm_sta_get_mbo_cell_cap(const osw_wnm_sta_t *sta)
{
    if (sta == NULL) return OSW_STA_CELL_UNKNOWN;
    return sta->params.mbo_cell_capability;
}

static void osw_wnm_parse_vendor_wfa_subelems(const struct element *elem, struct osw_wnm_sta_params *params)
{
    if (WARN_ON(elem->datalen < 5)) return;
    /* OUI - 3 bytes [0, 1, 2] */
    /* OUI_Type - 1 byte - [3] */
    const uint8_t type = elem->data[3];
    /* Payload - variable - [4, ] */
    const uint8_t *data = &(elem->data[4]);
    size_t data_len = elem->datalen - 4;

    switch (type)
    {
        case WFA_MBO_ATTR_ID_NON_PREF_CHAN_REP:
            params->mbo_capable = true;
            /* FIXME: Implement non-preferred channel change notifications */
            break;
        case WFA_MBO_ATTR_ID_CELL_DATA_PREF:
            params->mbo_capable = true;
            if (data_len != 1)
            {
                LOGW(LOG_WNM_PREFIX "Cellular Data Capabilities incorrect data length (%zd). Parsing error?", data_len);
                return;
            }
            switch (data[0])
            {
                case 1:
                    LOGT(LOG_WNM_PREFIX "Client MBO WNM report: Mobile Data Available");
                    params->mbo_cell_capability = OSW_STA_CELL_AVAILABLE;
                    break;
                case 2:
                    LOGT(LOG_WNM_PREFIX "Client MBO WNM report: Mobile Data Not Available");
                    params->mbo_cell_capability = OSW_STA_CELL_NOT_AVAILABLE;
                    break;
                case 3:
                    LOGT(LOG_WNM_PREFIX "Client MBO WNM report: Mobile Data Incapable");
                    params->mbo_cell_capability = OSW_STA_CELL_UNKNOWN;
                    break;
                default:
                    LOGW(LOG_WNM_PREFIX "Unknown Cellular Data Capabilities attribute value (%d)", data[0]);
                    break;
            }
            break;
        default:
            LOGW(LOG_WNM_PREFIX "Unknown WFA subelement (type=%d)", type);
            break;
    }
}

static bool osw_wnm_parse_wnm_notif_req(
        const struct osw_drv_dot11_frame_action_wnm_req *wnm_notif_req,
        size_t wnm_notif_req_len,
        struct osw_wnm_sta_params *params)
{
    if (WARN_ON(params == NULL)) return false;
    if (WARN_ON(wnm_notif_req_len < 4)) return false;

    const struct element *elem;
    switch (wnm_notif_req->type)
    {
        case DOT11_WNM_NOT_REQ_TYPE_VENDOR_SPECIFIC:
            for_each_ie(elem, wnm_notif_req->variable, wnm_notif_req_len)
            {
                uint32_t oui = bin2oui24(elem->data, elem->datalen);
                switch (oui)
                {
                    case WFA_VENDOR_SPECIFIC_OUI:
                        osw_wnm_parse_vendor_wfa_subelems(elem, params);
                        break;
                    default:
                        LOGD(LOG_WNM_PREFIX "Unknown Vendor Specific WNM Notification Req Subelement (oui: %04x)", oui);
                }
            }
            break;
        case DOT11_WNM_NOT_REQ_TYPE_FW_UPDATE_NOTIFICATION: /* fall through */
        case DOT11_WNM_NOT_REQ_TYPE_BEACON_PROT_FAILURE:    /* fall through */
        default:
            LOGD(LOG_WNM_PREFIX "Unhandled WNM Notification Request (type: %d)", wnm_notif_req->type);
            break;
    }
    return true;
}

static void osw_wnm_sta_update_links(struct osw_wnm_sta *sta, const osw_sta_assoc_entry_t *entry)
{
    if (WARN_ON(sta == NULL)) return;
    if (WARN_ON(entry == NULL)) return;

    const struct osw_sta_assoc_links *sta_links = osw_sta_assoc_entry_get_active_links(entry);

    struct osw_wnm_sta_link *link;
    struct osw_wnm_sta_link *tmp;
    ds_tree_foreach_safe (&sta->links, link, tmp)
    {
        osw_wnm_sta_link_drop(link);
    }
    size_t i;
    for (i = 0; i < sta_links->count; i++)
    {
        osw_wnm_sta_link_create(sta, &sta_links->links[i].remote_sta_addr);
    }
}

static void osw_wnm_sta_notify_observers(struct osw_wnm_sta *sta)
{
    struct osw_wnm_sta_observer *o;
    LOGT(LOG_PREFIX_STA(sta, "attempting to notify observers"));
    ds_tree_foreach (&sta->observers, o)
    {
        LOGT(LOG_PREFIX_STA(sta, "notifying observer"));
        osw_wnm_sta_observer_notify(o);
    }
}

static void osw_wnm_sta_update_params(struct osw_wnm_sta *sta, const struct osw_wnm_sta_params *params)
{
    struct osw_wnm_sta_params *param = &sta->params;

    const bool mbo_changed = param->mbo_capable != params->mbo_capable;
    const bool mbo_cell_changed = param->mbo_cell_capability != params->mbo_cell_capability;
    /* FIXME handle non-preferred channel list */
    const bool anything_changed = mbo_changed || mbo_cell_changed;

    if (mbo_changed)
    {
        LOGI(LOG_PREFIX_STA(sta, "params: mbo_capable: %d -> %d", param->mbo_capable, params->mbo_capable));
        param->mbo_capable = params->mbo_capable;
    }

    if (mbo_cell_changed)
    {
        LOGI(LOG_PREFIX_STA(
                sta,
                "params: mbo_cell_capability: %s -> %s",
                osw_sta_cell_cap_to_cstr(param->mbo_cell_capability),
                osw_sta_cell_cap_to_cstr(params->mbo_cell_capability)));
        param->mbo_cell_capability = params->mbo_cell_capability;
    }

    if (anything_changed)
    {
        osw_wnm_sta_notify_observers(sta);
    }
}

static void osw_wnm_frame_rx_cb(
        struct osw_state_observer *obs,
        const struct osw_state_vif_info *vif,
        const uint8_t *data,
        size_t len)
{
    if (WARN_ON(data == NULL)) return;
    if (WARN_ON(vif == NULL)) return;
    if (WARN_ON(vif->vif_name == NULL)) return;

    const struct osw_drv_dot11_frame *frame = (const struct osw_drv_dot11_frame *)data;
    const size_t dot11_header_len = sizeof(frame->header);
    if (WARN_ON(len < dot11_header_len)) return;
    const struct osw_drv_dot11_frame_header *header = &frame->header;
    const uint8_t *sa = header->sa;
    const struct osw_hwaddr *sta_addr = osw_hwaddr_from_cptr(sa, OSW_HWADDR_LEN);

    if (osw_wnm_frame_is_action((const struct osw_drv_dot11_frame *)data) == false)
    {
        LOGT(LOG_WNM_PREFIX "received frame is not an action frame");
        return;
    }

    const struct osw_drv_dot11_frame_action *action = &frame->u.action;
    const size_t dot11_action_len = sizeof(action->category);
    if (WARN_ON(len < dot11_header_len + dot11_action_len)) return;

    if (osw_wnm_action_frame_is_wnm_notification_request(action) == false)
    {
        LOGT(LOG_WNM_PREFIX "received frame is not a wnm notification request");
        return;
    }

    LOGT(LOG_WNM_PREFIX "Received WNM Notification Request frame from: " OSW_HWADDR_FMT, OSW_HWADDR_ARG(sta_addr));
    const struct osw_drv_dot11_frame_action_wnm_req *wnm_notif_req = &action->u.wnm_notif_req;
    const size_t dot11_wnm_notif_req_min_len = sizeof(*wnm_notif_req);
    if (WARN_ON(len < dot11_header_len + dot11_action_len + dot11_wnm_notif_req_min_len)) return;

    size_t payload_len = len - (dot11_header_len + dot11_action_len + dot11_wnm_notif_req_min_len);

    struct osw_wnm *m = state_obs_to_m(obs);
    /* SA from the frame is a link addr. It may be equal to sta_addr for
     * non-MLO clients, but will be equal to one of the link addresses otherwise. */
    struct osw_wnm_sta *sta = osw_wnm_sta_lookup(m, sta_addr) ?: osw_wnm_sta_lookup_by_link_addr(m, sta_addr);
    if (sta == NULL)
    {
        /* Note that if sta_assoc module did not register STA first, all frames
         * will be dropped for the STA. This opens up a possible race between
         * sta_assoc and WNM Notification Request that we need to be aware of */
        LOGT(LOG_WNM_PREFIX "received wnm notification frame for an unknown sta. dropping...");
        return;
    }

    struct osw_wnm_sta_params params = sta->params;
    if (WARN_ON(osw_wnm_parse_wnm_notif_req(wnm_notif_req, payload_len, &params) != true)) return;

    osw_wnm_sta_update_params(sta, &params);
}

static void osw_wnm_assoc_ies_to_sta_params(struct osw_wnm_sta_params *out, const struct osw_assoc_req_info *in)
{
    out->mbo_capable = in->mbo_capable;
    out->mbo_cell_capability = in->mbo_cell_capability;
    /* FIXME implement non-preferred channel list */
}

static void osw_wnm_sta_assoc_update_cb(struct osw_wnm *m, const osw_sta_assoc_entry_t *entry)
{
    const struct osw_hwaddr *sta_addr = osw_sta_assoc_entry_get_addr(entry);
    if ((sta_addr == NULL) || (osw_hwaddr_is_zero(sta_addr) == true)) return;

    struct osw_wnm_sta *sta = osw_wnm_sta_lookup(m, sta_addr) ?: osw_wnm_sta_alloc(m, sta_addr);
    osw_wnm_sta_update_links(sta, entry);

    const void *ies = osw_sta_assoc_entry_get_assoc_ies_data(entry);
    const size_t ies_len = osw_sta_assoc_entry_get_assoc_ies_len(entry);

    struct osw_assoc_req_info info;
    MEMZERO(info);
    const bool parsed = osw_parse_assoc_req_ies(ies, ies_len, &info);

    struct osw_wnm_sta_params params;
    MEMZERO(params);
    if (parsed) osw_wnm_assoc_ies_to_sta_params(&params, &info);
    osw_wnm_sta_update_params(sta, &params);
    osw_wnm_sta_gc(sta);
}

static void osw_wnm_sta_assoc_obs_changed_cb(void *priv, const osw_sta_assoc_entry_t *entry, osw_sta_assoc_event_e ev)
{
    struct osw_wnm *m = (struct osw_wnm *)priv;

    switch (ev)
    {
        case OSW_STA_ASSOC_CONNECTED:
        case OSW_STA_ASSOC_RECONNECTED:
        case OSW_STA_ASSOC_UNDEFINED:
        case OSW_STA_ASSOC_DISCONNECTED:
            osw_wnm_sta_assoc_update_cb(m, entry);
            break;
    }
}

static void osw_wnm_init(struct osw_wnm *m)
{
    const struct osw_state_observer state_obs = {
        .name = "g_osw_wnm",
        .vif_frame_rx_fn = osw_wnm_frame_rx_cb,
    };
    m->state_obs = state_obs;
    ds_tree_init(&m->sta_tree, (ds_key_cmp_t *)osw_hwaddr_cmp, osw_wnm_sta_t, node);
    ds_tree_init(&m->links, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_wnm_sta_link, node_m);
}

static void osw_wnm_attach(struct osw_wnm *m)
{
    OSW_MODULE_LOAD(osw_state);
    osw_state_register_observer(&m->state_obs);
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p, osw_wnm_sta_assoc_obs_changed_cb, m);
    osw_sta_assoc_t *sta_assoc = OSW_MODULE_LOAD(osw_sta_assoc);
    m->sta_assoc_observer = osw_sta_assoc_observer_alloc(sta_assoc, p);
}

OSW_MODULE(osw_wnm)
{
    static struct osw_wnm m;
    osw_wnm_init(&m);
    osw_wnm_attach(&m);
    return &m;
}
