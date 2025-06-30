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

#include <memutil.h>
#include <ds_tree.h>
#include <ds_dlist.h>
#include <const.h>
#include <os.h>

#include <osw_sta_snr.h>
#include <osw_sta_assoc.h>
#include <osw_state.h>
#include <osw_types.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_btm.h>
#include <osw_mux.h>
#include <osw_util.h>
#include <osw_wnm.h>

#include <ow_steer_hs.h>

#define OW_STEER_HS_DISASSOC_IMMINENT_DELAY_SEC 5

enum ow_steer_hs_level
{
    OW_STEER_HS_GOOD,
    OW_STEER_HS_SOFT,
    OW_STEER_HS_HARD,
};

typedef struct ow_steer_hs_sta ow_steer_hs_sta_t;
typedef struct ow_steer_hs_sta_link ow_steer_hs_sta_link_t;
typedef enum ow_steer_hs_level ow_steer_hs_level_e;

#define LOG_PREFIX(fmt, ...) "ow: steer: hs: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_VIF(vif, fmt, ...)             \
    LOG_PREFIX(                                   \
            "vif: %s (" OSW_HWADDR_FMT "): " fmt, \
            (vif)->vif_name ?: "",                \
            OSW_HWADDR_ARG(&(vif)->bssid),        \
            ##__VA_ARGS__)

#define LOG_PREFIX_STA(sta, fmt, ...) \
    LOG_PREFIX("sta: " OSW_HWADDR_FMT ": " fmt, OSW_HWADDR_ARG(&(sta)->addr), ##__VA_ARGS__)

#define LOG_PREFIX_LINK(link, fmt, ...)                                                                     \
    LOG_PREFIX_STA(                                                                                         \
            (link)->sta,                                                                                    \
            "link: %s: " fmt,                                                                               \
            (link)->vif && (link)->vif->vif_name ? (link)->vif->vif_name                                    \
                                                 : strfmta(OSW_HWADDR_FMT, OSW_HWADDR_ARG(&(link)->bssid)), \
            ##__VA_ARGS__)

struct ow_steer_hs_sta_link
{
    ds_tree_node_t node_by_bssid; /* ow_steer_hs_sta_t::links */
    ds_tree_node_t node_by_vif;   /* ow_steer_hs_vif_t::links */
    struct osw_hwaddr bssid;
    struct osw_hwaddr addr;
    osw_sta_snr_observer_t *snr;
    bool snr_valid;
    uint8_t snr_db;
    ow_steer_hs_sta_t *sta;
    ow_steer_hs_vif_t *vif;
};

struct ow_steer_hs_sta
{
    ds_tree_node_t node; /* ow_steer_hs_t::stas */
    ds_tree_t links;
    ow_steer_hs_t *hs;
    struct osw_hwaddr addr;
    struct osw_timer disassoc_timer;
    enum osw_sta_cell_cap cell_status;
    ow_steer_hs_level_e level;
    osw_btm_req_t *btm_req;
    osw_btm_sta_t *btm_sta;
    osw_wnm_sta_observer_t *wnm_sta;
};

struct ow_steer_hs_vif
{
    ds_tree_node_t node_by_name;  /* ow_steer_hs_t::vifs_by_name */
    ds_tree_node_t node_by_bssid; /* ow_steer_hs_t::vifs_by_bssid */
    ds_tree_t links;
    ow_steer_hs_t *hs;
    char *vif_name;
    uint8_t soft_snr_db;
    uint8_t hard_snr_db;
    struct osw_hwaddr bssid;
    struct osw_state_observer state_obs;
};

struct ow_steer_hs
{
    ds_tree_t stas;
    ds_tree_t vifs_by_name;
    ds_tree_t vifs_by_bssid;
    osw_sta_assoc_observer_t *sta_obs;

    osw_sta_snr_t *m_sta_snr;
    osw_sta_assoc_t *m_sta_assoc;
    osw_wnm_t *m_wnm;
    osw_btm_t *m_btm;
};

static ow_steer_hs_level_e ow_steer_hs_sta_derive_level(ow_steer_hs_sta_t *sta)
{
    ow_steer_hs_sta_link_t *link;
    size_t soft = 0;
    size_t hard = 0;
    size_t n = 0;
    ds_tree_foreach (&sta->links, link)
    {
        if (link->snr_valid == false) continue;
        if (link->vif == NULL) continue;
        n++;
        if (link->vif->hard_snr_db > 0 && link->snr_db <= link->vif->hard_snr_db) hard++;
        if (link->vif->soft_snr_db > 0 && link->snr_db <= link->vif->soft_snr_db) soft++;
    }
    if (n == hard && n > 0) return OW_STEER_HS_HARD;
    if (n == soft && n > 0) return OW_STEER_HS_SOFT;
    return OW_STEER_HS_GOOD;
}

static const char *ow_steer_hs_level_to_cstr(ow_steer_hs_level_e level)
{
    switch (level)
    {
        case OW_STEER_HS_GOOD:
            return "good";
        case OW_STEER_HS_SOFT:
            return "soft";
        case OW_STEER_HS_HARD:
            return "hard";
    }
    return "";
}

static void ow_steer_hs_sta_steer_disassoc_arm(ow_steer_hs_sta_t *sta)
{
    if (osw_timer_is_armed(&sta->disassoc_timer) == true) return;
    const uint64_t delay = OW_STEER_HS_DISASSOC_IMMINENT_DELAY_SEC;
    const uint64_t when = osw_time_mono_clk() + OSW_TIME_SEC(delay);
    LOGI(LOG_PREFIX_STA(sta, "disassoc: arming in %" PRIu64 " seconds", delay));
    osw_timer_arm_at_nsec(&sta->disassoc_timer, when);
}

static void ow_steer_hs_sta_steer_disassoc_disarm(ow_steer_hs_sta_t *sta)
{
    if (osw_timer_is_armed(&sta->disassoc_timer) == false) return;
    LOGI(LOG_PREFIX_STA(sta, "disassoc: disarming"));
    osw_timer_disarm(&sta->disassoc_timer);
}

static void ow_steer_hs_sta_steer_cancel(ow_steer_hs_sta_t *sta)
{
    if (sta->btm_req != NULL)
    {
        LOGI(LOG_PREFIX_STA(sta, "steer: dropping btm req"));
        osw_btm_req_drop(sta->btm_req);
        sta->btm_req = NULL;
    }
    ow_steer_hs_sta_steer_disassoc_disarm(sta);
}

static bool ow_steer_hs_sta_steer_fill_btm_params(ow_steer_hs_sta_t *sta, struct osw_btm_req_params *params)
{
    if (sta == NULL) return false;
    if (params == NULL) return false;

    /* FIXME: This is tricky. With MLO this will depend on
     * the Beacon of a given link.. Let's just assume this
     * for now. It shouldn't hurt too much.
     */
    const uint16_t bcn_int_ms = 200;

    /**
     * Here's the "why" on sta->cell_status and sta->level
     * logic:
     *
     * The idea is that a STA will experience signal degrade
     * over time slowly enough that we'll first consider it
     * for soft steering.
     *
     * In the soft steering case there's time, so we let it
     * still consider Wi-Fi networks on best-effort basis.
     * If it finds one - great. If it doesn't - not terrible
     * yet.
     *
     * Once we get into hard steering - there's no time. We
     * need to get rid of the STA now because it will suffer
     * low performance, waste airtime and possibly impact
     * non-public service quality. In this case let the STA
     * know it should just go to Cellular if possible. If
     * it's smart enough it'll re-scan Wi-Fi after roaming
     * to Cellular and re-try Wi-Fi at its own pace.
     * Hopefully this maintains STA connectivity to
     * Internet.
     *
     * This doesn't attempt building fancy Candidate List
     * intentionally. There's a sufficient number of
     * uncertainties and guesswork that'd need to be
     * involved that - when done wrong - can lead to worse
     * outcomes compared to just leaving STA to figure out
     * where to go to.
     *
     * This just keeps it simple.
     */

    MEMZERO(*params);
    params->abridged = false;
    params->disassoc_imminent = (sta->level == OW_STEER_HS_HARD);
    params->disassoc_timer = (OW_STEER_HS_DISASSOC_IMMINENT_DELAY_SEC * 1000) / bcn_int_ms;
    params->mbo.reason = OSW_BTM_MBO_REASON_LOW_RSSI;

    switch (sta->cell_status)
    {
        case OSW_STA_CELL_UNKNOWN:
        case OSW_STA_CELL_NOT_AVAILABLE:
            /* Technically it is legal to include cell
             * preference here too, but the STA is expected
             * ("shall") to ignore it anyway.
             */
            break;
        case OSW_STA_CELL_AVAILABLE:
            params->mbo.cell_preference = (sta->level == OW_STEER_HS_HARD) ? OSW_BTM_MBO_CELL_PREF_RECOMMEND_CELL
                                                                           : OSW_BTM_MBO_CELL_PREF_AVOID_CELL;
            break;
    }

    return true;
}

static void ow_steer_hs_sta_steer_req_complete_cb(void *priv, enum osw_btm_req_result result)
{
    ow_steer_hs_sta_t *sta = priv;
    switch (result)
    {
        case OSW_BTM_REQ_RESULT_SENT:
            LOGI(LOG_PREFIX_STA(sta, "steer: sent"));
            break;
        case OSW_BTM_REQ_RESULT_FAILED:
            LOGI(LOG_PREFIX_STA(sta, "steer: failed to send"));
            break;
    }
}

static void ow_steer_hs_sta_steer_req_response_cb(void *priv, const osw_btm_resp_t *resp)
{
    ow_steer_hs_sta_t *sta = priv;
    LOGI(LOG_PREFIX_STA(sta, "steer: response: %u", osw_btm_resp_get_status(resp)));
}

static void ow_steer_hs_sta_steer_submit_btm(ow_steer_hs_sta_t *sta)
{
    sta->btm_req = osw_btm_req_alloc(sta->btm_sta);
    if (sta->btm_req == NULL) return;
    struct osw_btm_req_params params;
    const bool ok = true && ow_steer_hs_sta_steer_fill_btm_params(sta, &params)
                    && osw_btm_req_set_completed_fn(sta->btm_req, ow_steer_hs_sta_steer_req_complete_cb, sta)
                    && osw_btm_req_set_response_fn(sta->btm_req, ow_steer_hs_sta_steer_req_response_cb, sta)
                    && osw_btm_req_set_params(sta->btm_req, &params) && osw_btm_req_submit(sta->btm_req);
    if (ok) return;
    LOGI(LOG_PREFIX_STA(sta, "steer: failed to submit"));
}

static void ow_steer_hs_sta_steer_soft(ow_steer_hs_sta_t *sta)
{
    ow_steer_hs_sta_steer_cancel(sta);
    ow_steer_hs_sta_steer_submit_btm(sta);
}

static void ow_steer_hs_sta_steer_disassoc_link(ow_steer_hs_sta_link_t *link, int reason)
{
    if (link->vif == NULL) return;
    if (link->vif->vif_name == NULL) return;
    const struct osw_state_vif_info *info = osw_state_vif_lookup_by_vif_name(link->vif->vif_name);
    const char *phy_name = info->phy->phy_name;
    const char *vif_name = info->vif_name;
    LOGI(LOG_PREFIX_LINK(link, "disassoc: sending"));
    osw_mux_request_sta_deauth(phy_name, vif_name, &link->addr, reason);
}

static void ow_steer_hs_sta_steer_disassoc(ow_steer_hs_sta_t *sta)
{
    int reason = 0; /* FIXME */
    ow_steer_hs_sta_link_t *link;
    ds_tree_foreach (&sta->links, link)
    {
        ow_steer_hs_sta_steer_disassoc_link(link, reason);
    }
}

static void ow_steer_hs_sta_steer_disassoc_timer_cb(struct osw_timer *t)
{
    ow_steer_hs_sta_t *sta = container_of(t, typeof(*sta), disassoc_timer);
    ow_steer_hs_sta_steer_disassoc(sta);
}

static void ow_steer_hs_sta_steer_hard(ow_steer_hs_sta_t *sta)
{
    ow_steer_hs_sta_steer_cancel(sta);
    ow_steer_hs_sta_steer_submit_btm(sta);
    ow_steer_hs_sta_steer_disassoc_arm(sta);
}

static void ow_steer_hs_sta_steer(ow_steer_hs_sta_t *sta)
{
    switch (sta->level)
    {
        case OW_STEER_HS_GOOD:
            ow_steer_hs_sta_steer_cancel(sta);
            break;
        case OW_STEER_HS_SOFT:
            ow_steer_hs_sta_steer_soft(sta);
            break;
        case OW_STEER_HS_HARD:
            ow_steer_hs_sta_steer_hard(sta);
            break;
    }
}

static void ow_steer_hs_sta_set_level(ow_steer_hs_sta_t *sta, ow_steer_hs_level_e level)
{
    if (sta->level == level) return;
    LOGI(LOG_PREFIX_STA(
            sta,
            "level: %s -> %s",
            ow_steer_hs_level_to_cstr(sta->level),
            ow_steer_hs_level_to_cstr(level)));
    sta->level = level;
    ow_steer_hs_sta_steer(sta);
}

static void ow_steer_hs_sta_set_cell_status(ow_steer_hs_sta_t *sta, enum osw_sta_cell_cap cell_status)
{
    if (sta->cell_status == cell_status) return;
    LOGI(LOG_PREFIX_STA(
            sta,
            "cell_status: %s -> %s",
            osw_sta_cell_cap_to_cstr(sta->cell_status),
            osw_sta_cell_cap_to_cstr(cell_status)));
    sta->cell_status = cell_status;
    ow_steer_hs_sta_steer(sta);
}

static void ow_steer_hs_sta_recalc(ow_steer_hs_sta_t *sta)
{
    const ow_steer_hs_level_e level = ow_steer_hs_sta_derive_level(sta);
    ow_steer_hs_sta_set_level(sta, level);
}

static void ow_steer_hs_sta_wnm_notify_cb(void *priv, const osw_wnm_sta_t *wnm_sta)
{
    ow_steer_hs_sta_t *sta = priv;
    const enum osw_sta_cell_cap cell_status = osw_wnm_sta_get_mbo_cell_cap(wnm_sta);
    ow_steer_hs_sta_set_cell_status(sta, cell_status);
}

static osw_wnm_sta_observer_params_t *ow_steer_hs_sta_alloc_wnm_params(ow_steer_hs_sta_t *sta)
{
    osw_wnm_sta_observer_params_t *p = osw_wnm_sta_observer_params_alloc();
    osw_wnm_sta_observer_params_set_addr(p, &sta->addr);
    osw_wnm_sta_observer_params_set_changed_fn(p, ow_steer_hs_sta_wnm_notify_cb, sta);
    return p;
}

static osw_wnm_sta_observer_t *ow_steer_hs_sta_alloc_wnm_obs(ow_steer_hs_sta_t *sta)
{
    osw_wnm_sta_observer_params_t *p = ow_steer_hs_sta_alloc_wnm_params(sta);
    return osw_wnm_sta_observer_alloc(sta->hs->m_wnm, p);
}

static ow_steer_hs_sta_t *ow_steer_hs_sta_alloc(ow_steer_hs_t *hs, const struct osw_hwaddr *addr)
{
    if (hs == NULL) return NULL;
    if (addr == NULL) return NULL;

    ow_steer_hs_sta_t *sta = CALLOC(1, sizeof(*sta));
    sta->hs = hs;
    sta->addr = *addr;
    sta->wnm_sta = ow_steer_hs_sta_alloc_wnm_obs(sta);
    sta->btm_sta = osw_btm_sta_alloc(hs->m_btm, addr);
    osw_timer_init(&sta->disassoc_timer, ow_steer_hs_sta_steer_disassoc_timer_cb);
    ds_tree_init(&sta->links, (ds_key_cmp_t *)osw_hwaddr_cmp, ow_steer_hs_sta_link_t, node_by_bssid);
    ds_tree_insert(&hs->stas, sta, &sta->addr);
    LOGT(LOG_PREFIX_STA(sta, "allocated"));
    return sta;
}

static void ow_steer_hs_sta_link_set_vif(ow_steer_hs_sta_link_t *link, ow_steer_hs_vif_t *vif)
{
    if (link->vif == vif) return;
    if (link->vif != NULL)
    {
        LOGT(LOG_PREFIX_LINK(link, "detaching to: " OSW_HWADDR_FMT, OSW_HWADDR_ARG(&link->bssid)));
        ds_tree_remove(&link->vif->links, link);
        link->vif = NULL;
    }
    if (vif != NULL)
    {
        LOGT(LOG_PREFIX_LINK(link, "attaching to: %s", vif->vif_name));
        ds_tree_insert(&vif->links, link, &link->bssid);
        link->vif = vif;
    }
    ow_steer_hs_sta_recalc(link->sta);
}

static void ow_steer_hs_sta_link_drop(ow_steer_hs_sta_link_t *link)
{
    if (link == NULL) return;
    LOGT(LOG_PREFIX_LINK(link, "dropping"));
    osw_sta_snr_observer_drop(link->snr);
    ow_steer_hs_sta_link_set_vif(link, NULL);
    ds_tree_remove(&link->sta->links, link);
    FREE(link);
}

static void ow_steer_hs_sta_drop_links(ow_steer_hs_sta_t *sta)
{
    ow_steer_hs_sta_link_t *link;
    while ((link = ds_tree_head(&sta->links)) != NULL)
    {
        ow_steer_hs_sta_link_drop(link);
    }
}

static void ow_steer_hs_sta_drop(ow_steer_hs_sta_t *sta)
{
    if (sta == NULL) return;
    if (sta->hs == NULL) return;
    LOGT(LOG_PREFIX_STA(sta, "dropping"));
    ow_steer_hs_sta_drop_links(sta);
    ow_steer_hs_sta_steer_cancel(sta);
    ds_tree_remove(&sta->hs->stas, sta);
    osw_wnm_sta_observer_drop(sta->wnm_sta);
    osw_btm_sta_drop(sta->btm_sta);
    FREE(sta);
}

static void ow_steer_hs_sta_link_snr_notify_cb(void *priv, const uint8_t *snr_db)
{
    ow_steer_hs_sta_link_t *link = priv;
    link->snr_valid = snr_db != NULL;
    link->snr_db = snr_db ? *snr_db : 0;
    if (link->vif != NULL)
    {
        LOGT(LOG_PREFIX_LINK(link, "snr: %" PRIu8 "dB %s", link->snr_db, link->snr_valid ? "valid" : "invalid"));
    }
    ow_steer_hs_sta_recalc(link->sta);
}

static void ow_steer_hs_vif_recalc_stas(ow_steer_hs_vif_t *vif)
{
    ow_steer_hs_sta_t *sta;
    ds_tree_foreach (&vif->hs->stas, sta)
    {
        ow_steer_hs_sta_link_t *link;
        ds_tree_foreach (&sta->links, link)
        {
            if (link->vif == vif)
            {
                ow_steer_hs_sta_recalc(link->sta);
            }
        }
    }
}

static osw_sta_snr_observer_t *ow_steer_hs_sta_link_alloc_snr(ow_steer_hs_sta_link_t *link)
{
    if (osw_hwaddr_is_zero(&link->addr)) return NULL;
    osw_sta_snr_params_t *p = osw_sta_snr_params_alloc();
    osw_sta_snr_params_set_sta_addr(p, &link->addr);
    osw_sta_snr_params_set_vif_addr(p, &link->bssid);
    osw_sta_snr_params_set_notify_fn(p, ow_steer_hs_sta_link_snr_notify_cb, link);
    return osw_sta_snr_observer_alloc(link->sta->hs->m_sta_snr, p);
}

static void ow_steer_hs_sta_link_update_vif(ow_steer_hs_sta_link_t *link)
{
    ow_steer_hs_vif_t *vif = ds_tree_find(&link->sta->hs->vifs_by_bssid, &link->bssid);
    ow_steer_hs_sta_link_set_vif(link, vif);
}

static void ow_steer_hs_relink_sta_links_to_vifs(ow_steer_hs_t *hs)
{
    ow_steer_hs_sta_t *sta;
    ds_tree_foreach (&hs->stas, sta)
    {
        ow_steer_hs_sta_link_t *link;
        ds_tree_foreach (&sta->links, link)
        {
            ow_steer_hs_sta_link_update_vif(link);
        }
    }
}

static ow_steer_hs_sta_link_t *ow_steer_hs_sta_link_alloc(ow_steer_hs_sta_t *sta, const struct osw_hwaddr *bssid)
{
    if (sta == NULL) return NULL;
    if (bssid == NULL) return NULL;
    ow_steer_hs_sta_link_t *link = CALLOC(1, sizeof(*link));
    link->sta = sta;
    link->bssid = *bssid;
    ds_tree_insert(&sta->links, link, &link->bssid);
    LOGT(LOG_PREFIX_LINK(link, "allocated"));
    ow_steer_hs_sta_link_update_vif(link);
    return link;
}

static void ow_steer_hs_sta_link_set(ow_steer_hs_sta_t *sta, const osw_sta_assoc_link_t *link)
{
    const struct osw_hwaddr *bssid = &link->local_sta_addr;
    const struct osw_hwaddr *addr = &link->remote_sta_addr;
    ow_steer_hs_sta_link_t *sta_link = ds_tree_find(&sta->links, bssid) ?: ow_steer_hs_sta_link_alloc(sta, bssid);
    if (osw_hwaddr_is_equal(&sta_link->addr, addr)) return;
    sta_link->addr = *addr;
    osw_sta_snr_observer_drop(sta_link->snr);
    sta_link->snr = ow_steer_hs_sta_link_alloc_snr(sta_link);
}

static void ow_steer_hs_sta_set_links(ow_steer_hs_sta_t *sta, const osw_sta_assoc_links_t *links)
{
    size_t i;
    for (i = 0; i < links->count; i++)
    {
        const osw_sta_assoc_link_t *link = &links->links[i];
        ow_steer_hs_sta_link_set(sta, link);
    }

    ow_steer_hs_sta_link_t *link;
    ow_steer_hs_sta_link_t *tmp;
    ds_tree_foreach_safe (&sta->links, link, tmp)
    {
        const struct osw_hwaddr *bssid = &link->bssid;
        const osw_sta_assoc_link_t *found = osw_sta_assoc_links_lookup(links, bssid, NULL);
        if (found == NULL)
        {
            ow_steer_hs_sta_link_drop(link);
        }
    }
}

static void ow_steer_hs_sta_gc(ow_steer_hs_sta_t *sta)
{
    if (ds_tree_is_empty(&sta->links) == false) return;
    ow_steer_hs_sta_drop(sta);
}

static void ow_steer_hs_update_sta(ow_steer_hs_t *hs, const osw_sta_assoc_entry_t *entry, osw_sta_assoc_event_e ev)
{
    const struct osw_hwaddr *sta_addr = osw_sta_assoc_entry_get_addr(entry);
    if (sta_addr == NULL) return;

    ow_steer_hs_sta_t *sta = ds_tree_find(&hs->stas, sta_addr) ?: ow_steer_hs_sta_alloc(hs, sta_addr);
    if (sta == NULL) return;

    const osw_sta_assoc_links_t *links = osw_sta_assoc_entry_get_active_links(entry);
    if (links == NULL) return;

    ow_steer_hs_sta_set_links(sta, links);
    ow_steer_hs_sta_gc(sta);
}

static void ow_steer_hs_sta_notify_cb(void *priv, const osw_sta_assoc_entry_t *entry, osw_sta_assoc_event_e ev)
{
    ow_steer_hs_t *hs = priv;
    ow_steer_hs_update_sta(hs, entry, ev);
}

static void ow_steer_hs_vif_set_bssid(ow_steer_hs_vif_t *vif, const struct osw_hwaddr *bssid)
{
    if (vif == NULL) return;
    if (osw_hwaddr_is_equal(&vif->bssid, bssid)) return;
    LOGI(LOG_PREFIX_VIF(
            vif,
            "bssid: " OSW_HWADDR_FMT " -> " OSW_HWADDR_FMT,
            OSW_HWADDR_ARG(&vif->bssid),
            OSW_HWADDR_ARG(bssid)));
    if (osw_hwaddr_is_zero(&vif->bssid) == false)
    {
        ds_tree_remove(&vif->hs->vifs_by_bssid, vif);
    }
    vif->bssid = *bssid;
    if (osw_hwaddr_is_zero(&vif->bssid) == false)
    {
        ds_tree_insert(&vif->hs->vifs_by_bssid, vif, &vif->bssid);
    }
    ow_steer_hs_relink_sta_links_to_vifs(vif->hs);
}

static void ow_steer_hs_vif_notify_cb(struct osw_state_observer *obs, const struct osw_state_vif_info *info)
{
    ow_steer_hs_vif_t *vif = container_of(obs, typeof(*vif), state_obs);
    const bool other_vif = (strcmp(vif->vif_name, info->vif_name) != 0);
    if (other_vif) return;
    const struct osw_hwaddr *bssid = info->drv_state->exists ? &info->drv_state->mac_addr : osw_hwaddr_zero();
    ow_steer_hs_vif_set_bssid(vif, bssid);
}

static void ow_steer_hs_drop_stas(ow_steer_hs_t *hs)
{
    ow_steer_hs_sta_t *sta;
    while ((sta = ds_tree_head(&hs->stas)) != NULL)
    {
        LOGI(LOG_PREFIX("sta_obs: flushing: " OSW_HWADDR_FMT, OSW_HWADDR_ARG(&sta->addr)));
        ow_steer_hs_sta_drop(sta);
    }
}

static osw_sta_assoc_observer_t *ow_steer_hs_alloc_sta_obs(ow_steer_hs_t *hs)
{
    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p, ow_steer_hs_sta_notify_cb, hs);
    return osw_sta_assoc_observer_alloc(hs->m_sta_assoc, p);
}

static void ow_steer_hs_sta_obs_attach(ow_steer_hs_t *hs)
{
    if (hs->sta_obs != NULL) return;
    LOGI(LOG_PREFIX("sta_obs: attaching"));
    hs->sta_obs = ow_steer_hs_alloc_sta_obs(hs);
}

static void ow_steer_hs_sta_obs_detach(ow_steer_hs_t *hs)
{
    if (hs->sta_obs == NULL) return;
    LOGI(LOG_PREFIX("sta_obs: detaching"));
    osw_sta_assoc_observer_drop(hs->sta_obs);
    hs->sta_obs = NULL;
    ow_steer_hs_drop_stas(hs);
}

static bool ow_steer_hs_sta_obs_is_needed(ow_steer_hs_t *hs)
{
    return (ds_tree_len(&hs->vifs_by_name) > 0);
}

static void ow_steer_hs_sta_obs_update(ow_steer_hs_t *hs)
{
    if (ow_steer_hs_sta_obs_is_needed(hs))
    {
        ow_steer_hs_sta_obs_attach(hs);
    }
    else
    {
        ow_steer_hs_sta_obs_detach(hs);
    }
}

ow_steer_hs_vif_t *ow_steer_hs_vif_alloc(ow_steer_hs_t *hs, const char *vif_name)
{
    if (hs == NULL) return NULL;
    if (vif_name == NULL) return NULL;
    if (WARN_ON(ds_tree_find(&hs->vifs_by_name, vif_name) != NULL)) return NULL;

    ow_steer_hs_vif_t *vif = CALLOC(1, sizeof(*vif));
    vif->hs = hs;
    vif->vif_name = STRDUP(vif_name);
    vif->state_obs.vif_added_fn = ow_steer_hs_vif_notify_cb;
    vif->state_obs.vif_removed_fn = ow_steer_hs_vif_notify_cb;
    vif->state_obs.vif_changed_fn = ow_steer_hs_vif_notify_cb;
    ds_tree_init(&vif->links, ds_void_cmp, ow_steer_hs_sta_link_t, node_by_vif);
    ds_tree_insert(&hs->vifs_by_name, vif, vif->vif_name);
    LOGT(LOG_PREFIX_VIF(vif, "allocated"));
    osw_state_register_observer(&vif->state_obs);
    ow_steer_hs_sta_obs_update(hs);
    return vif;
}

void ow_steer_hs_vif_drop(ow_steer_hs_vif_t *vif)
{
    if (vif == NULL) return;
    LOGT(LOG_PREFIX_VIF(vif, "dropping"));
    osw_state_unregister_observer(&vif->state_obs);
    ow_steer_hs_vif_set_bssid(vif, osw_hwaddr_zero());
    assert(ds_tree_is_empty(&vif->links));
    ds_tree_remove(&vif->hs->vifs_by_name, vif);
    ow_steer_hs_sta_obs_update(vif->hs);
    FREE(vif->vif_name);
    FREE(vif);
}

void ow_steer_hs_vif_set_soft_snr_db(ow_steer_hs_vif_t *vif, uint8_t soft_snr_db)
{
    if (vif == NULL) return;
    if (vif->soft_snr_db == soft_snr_db) return;
    LOGI(LOG_PREFIX_VIF(vif, "soft_snr_db: %hhu -> %hhu", vif->soft_snr_db, soft_snr_db));
    vif->soft_snr_db = soft_snr_db;
    ow_steer_hs_vif_recalc_stas(vif);
}

void ow_steer_hs_vif_set_hard_snr_db(ow_steer_hs_vif_t *vif, uint8_t hard_snr_db)
{
    if (vif == NULL) return;
    if (vif->hard_snr_db == hard_snr_db) return;
    LOGI(LOG_PREFIX_VIF(vif, "hard_snr_db: %hhu -> %hhu", vif->hard_snr_db, hard_snr_db));
    vif->hard_snr_db = hard_snr_db;
    ow_steer_hs_vif_recalc_stas(vif);
}

void ow_steer_hs_reset(ow_steer_hs_t *hs)
{
    if (hs == NULL) return;
    ow_steer_hs_vif_t *vif;
    while ((vif = ds_tree_head(&hs->vifs_by_name)) != NULL)
    {
        ow_steer_hs_vif_drop(vif);
    }
}

static void ow_steer_hs_init(ow_steer_hs_t *hs)
{
    ds_tree_init(&hs->stas, (ds_key_cmp_t *)osw_hwaddr_cmp, ow_steer_hs_sta_t, node);
    ds_tree_init(&hs->vifs_by_name, ds_str_cmp, ow_steer_hs_vif_t, node_by_name);
    ds_tree_init(&hs->vifs_by_bssid, (ds_key_cmp_t *)osw_hwaddr_cmp, ow_steer_hs_vif_t, node_by_bssid);
}

static void ow_steer_hs_attach(ow_steer_hs_t *hs)
{
    hs->m_sta_assoc = OSW_MODULE_LOAD(osw_sta_assoc);
    hs->m_sta_snr = OSW_MODULE_LOAD(osw_sta_snr);
    hs->m_btm = OSW_MODULE_LOAD(osw_btm);
    hs->m_wnm = OSW_MODULE_LOAD(osw_wnm);
}

OSW_MODULE(ow_steer_hs)
{
    static ow_steer_hs_t hs;
    ow_steer_hs_init(&hs);
    ow_steer_hs_attach(&hs);
    return &hs;
}
