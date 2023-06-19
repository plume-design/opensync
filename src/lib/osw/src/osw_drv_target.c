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

#include <libgen.h>
#include <ev.h>
#include <osw_drv_dummy.h>
#include <osw_state.h>
#include <osw_module.h>
#include <osw_ut.h>
#include <target.h>
#include <target_bsal.h>
#include <memutil.h>
#include <ovsdb_table.h>
#include <ovsdb_cache.h>
#include <schema_compat.h>

static ovsdb_table_t table_Wifi_Radio_Config;
static ovsdb_table_t table_Wifi_Radio_State;
static ovsdb_table_t table_Wifi_VIF_Config;
static ovsdb_table_t table_Band_Steering_Config;
static ovsdb_table_t table_Band_Steering_Clients;

struct osw_drv_target;

struct osw_drv_target_phy {
    struct ds_tree_node node;
    struct schema_Wifi_Radio_State rstate;
    struct schema_Wifi_Radio_Config config;
};

struct osw_drv_target_net {
    struct ds_dlist_node node;
    struct osw_hwaddr bssid;
    struct osw_ssid ssid;
    struct osw_psk psk;
    struct osw_wpa wpa;
};

struct osw_drv_target_vif {
    struct ds_tree_node node;
    struct ds_dlist nets;
    struct osw_drv_target *target;
    struct schema_Wifi_VIF_Config config;
    struct schema_Wifi_VIF_State state;
    char *vif_name;
    char *phy_name;
    bool exists;
    ev_timer work;
};

struct osw_drv_target_survey_entry {
    struct ds_tree node;
    int channel;
    target_survey_record_t *record;
};

struct osw_drv_target_survey {
    struct ds_tree_node node;
    struct osw_drv_target *target;
    char *phy_name;
    radio_entry_t cfg;
    radio_scan_type_t type;
    struct ds_tree entries; /* osw_drv_target_survey_entry */
    struct ds_dlist list;
    bool requested;
};

struct osw_drv_target_scan_id {
};

struct osw_drv_target_scan {
    struct ds_dlist_node node;
    struct osw_drv_target *target;
    radio_entry_t cfg;
    radio_scan_type_t type;
    uint32_t dwell;
    uint32_t *chans;
    uint32_t n_chans;
    bool started;
};

struct osw_drv_target {
    struct osw_drv_dummy dummy;
    struct osw_drv *drv;
    struct ds_tree phy_tree;
    struct ds_tree vif_tree;
    struct osw_drv_conf *conf;
    size_t conf_phy;
    size_t conf_vif;
    bool conf_phy_done;
    ev_timer conf_timer;
    ev_timer ovsdb_retry;
    struct ds_tree bsal_vap_tree;
    struct ds_tree bsal_ue_tree;
    struct ds_dlist scans;
    struct ds_tree surveys;
    struct osw_tlv *stats;
};

struct osw_drv_target_bsal_vap {
    struct ds_tree_node node;
    bsal_ifconfig_t cfg;
};

struct osw_drv_target_bsal_ue {
    struct ds_tree_node node;
    struct osw_hwaddr mac_addr;
    bsal_client_config_t cfg;
};

static void
osw_drv_target_init_cb(struct osw_drv *drv);
static void
osw_drv_target_request_config_cb(struct osw_drv *drv,
                                 struct osw_drv_conf *conf);

static bool
osw_drv_target_enabled(void)
{
    if (getenv("OSW_DRV_TARGET_DISABLED") == NULL)
        return true;
    else
        return false;
}

static bool
osw_drv_target_ovsdb_enabled(void)
{
    if (getenv("OSW_DRV_TARGET_OVSDB_DISABLED") == NULL)
        return true;
    else
        return false;
}

static void
osw_drv_target_fini_phy_cb(struct osw_drv_dummy *dummy,
                           struct osw_drv_phy_state *phy)
{
    FREE(phy->channel_states);
    phy->channel_states = NULL;
    phy->n_channel_states = 0;
}

static void
osw_drv_target_fini_vif_cb(struct osw_drv_dummy *dummy,
                           struct osw_drv_vif_state *vif)
{
    switch (vif->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            FREE(vif->u.ap.acl.list);
            FREE(vif->u.ap.psk_list.list);
            vif->u.ap.acl.list = NULL;
            vif->u.ap.acl.count = 0;
            vif->u.ap.psk_list.list = NULL;
            vif->u.ap.psk_list.count = 0;
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            while (vif->u.sta.network != NULL) {
                struct osw_drv_vif_sta_network *next = vif->u.sta.network->next;
                FREE(vif->u.sta.network);
                vif->u.sta.network = next;
            }
            break;
    }
}

static void
osw_drv_target_push_frame_tx_cb(struct osw_drv *drv,
                                const char *phy_name,
                                const char *vif_name,
                                struct osw_drv_frame_tx_desc *desc)
{
    const size_t dot11_header_size = sizeof(struct osw_drv_dot11_frame_header);
    const uint8_t *payload = osw_drv_frame_tx_desc_get_frame(desc);
    const struct osw_drv_dot11_frame *frame = (const struct osw_drv_dot11_frame*) payload;
    size_t payload_len = osw_drv_frame_tx_desc_get_frame_len(desc);
    int result;

    if (vif_name == NULL) {
        LOGD("osw: drv: target: vif_name is required, failing");
        osw_drv_report_frame_tx_state_failed(drv);
        return;
    }

    if (dot11_header_size >= payload_len) {
        LOGD("osw: drv: target: frame tx too small, failing");
        osw_drv_report_frame_tx_state_failed(drv);
        return;
    }

    payload += dot11_header_size;
    payload_len -= dot11_header_size;

    result = target_bsal_send_action(vif_name, frame->header.da, payload, payload_len);
    if (result == 0)
        osw_drv_report_frame_tx_state_submitted(drv);
    else
        osw_drv_report_frame_tx_state_failed(drv);
}

static void
osw_drv_target_fill_sta_stats_one(const char *phy_name,
                                  const char *vif_name,
                                  const struct osw_hwaddr *addr,
                                  struct osw_tlv *t)
{
    bsal_client_info_t info = {0};
    int err = target_bsal_client_info(vif_name, addr->octet, &info);
    if (err != 0) return;
    if (info.connected == false) return;

    const uint32_t tx_bytes = info.tx_bytes;
    const uint32_t rx_bytes = info.rx_bytes;
    const uint32_t snr = info.snr;

    size_t s = osw_tlv_put_nested(t, OSW_STATS_STA);
    osw_tlv_put_string(t, OSW_STATS_STA_PHY_NAME, phy_name);
    osw_tlv_put_string(t, OSW_STATS_STA_VIF_NAME, vif_name);
    osw_tlv_put_hwaddr(t, OSW_STATS_STA_MAC_ADDRESS, addr);

    osw_tlv_put_u32(t, OSW_STATS_STA_SNR_DB, snr);
    osw_tlv_put_u32(t, OSW_STATS_STA_TX_BYTES, tx_bytes);
    osw_tlv_put_u32(t, OSW_STATS_STA_RX_BYTES, rx_bytes);

    osw_tlv_end_nested(t, s);
}

static void
osw_drv_target_fill_sta_stats(struct osw_drv_dummy *dummy,
                              struct osw_tlv *t)
{
    struct osw_drv_dummy_sta *sta;
    struct osw_drv_dummy_vif *vif;
    ds_dlist_foreach(&dummy->sta_list, sta) {
        const char *phy_name = sta->phy_name.buf;
        const char *vif_name = sta->vif_name.buf;
        const struct osw_hwaddr *addr = &sta->sta_addr;
        osw_drv_target_fill_sta_stats_one(phy_name, vif_name, addr, t);
    }
    ds_tree_foreach(&dummy->vif_tree, vif) {
        if (vif->state.vif_type != OSW_VIF_STA) continue;
        if (vif->state.u.sta.link.status != OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) continue;
        const char *phy_name = vif->phy_name.buf;
        const char *vif_name = vif->vif_name.buf;
        const struct osw_hwaddr *addr = &vif->state.u.sta.link.bssid;
        osw_drv_target_fill_sta_stats_one(phy_name, vif_name, addr, t);
    }
}

static radio_type_t
phy_to_type(const struct osw_drv_phy_state *phy)
{
    const int b2ch1 = 2412;
    const int b2ch13 = 2472;
    const int b2ch14 = 2484;
    const int b5ch36 = 5180;
    const int b5ch100 = 5500;
    const int b5ch177 = 5885;
    const int b6ch1 = 5955;
    const int b6ch2 = 5935;
    const int b6ch233 = 7115;
    bool is_2g = false;
    bool is_5g = false;
    bool is_5gl = false;
    bool is_5gu = false;
    bool is_6g = false;

    size_t i ;
    const size_t n = phy->n_channel_states;
    for (i = 0; i < n; i++) {
        const struct osw_channel_state *cs = &phy->channel_states[i];
        const struct osw_channel *c = &cs->channel;
        const int freq = c->control_freq_mhz;

        if (freq >= b2ch1 && freq <= b2ch13) is_2g = true;
        if (freq == b2ch14) is_2g = true;
        if (freq >= b5ch36 && freq < b5ch100) is_5gl = true;
        if (freq >= b5ch100 && freq <= b5ch177) is_5gu = true;
        if (freq >= b6ch1 && freq <= b6ch233) is_6g = true;
        if (freq == b6ch2) is_6g = true;
    }

    is_5g = is_5gl && is_5gu;
    if (is_5g) {
        is_5gl = false;
        is_5gu = false;
    }

    const int bands = is_2g + is_5g + is_5gl + is_5gu + is_6g;
    if (WARN_ON(bands != 1)) return RADIO_TYPE_NONE;

    if (is_2g == true) return RADIO_TYPE_2G;
    if (is_5g == true) return RADIO_TYPE_5G;
    if (is_5gl == true) return RADIO_TYPE_5GL;
    if (is_5gu == true) return RADIO_TYPE_5GU;
    if (is_6g == true) return RADIO_TYPE_6G;

    return RADIO_TYPE_NONE;
}

static uint32_t
freq_to_chan(const int freq)
{
    const int b2ch1 = 2412;
    const int b2ch13 = 2472;
    const int b2ch14 = 2484;
    const int b5ch36 = 5180;
    const int b5ch177 = 5885;
    const int b6ch1 = 5955;
    const int b6ch2 = 5935;
    const int b6ch233 = 7115;
    const int b2base = 2407;
    const int b5base = 5000;
    const int b6base = 5950;

    if (freq >= b2ch1 && freq <= b2ch13) return (freq - b2base) / 5;
    if (freq == b2ch14) return 14;
    if (freq >= b5ch36 && freq <= b5ch177) return (freq - b5base) / 5;
    if (freq >= b6ch1 && freq <= b6ch233) return (freq - b6base) / 5;
    if (freq == b6ch2) return 2;
    return 0;
}

#if 0
static uint32_t *
phy_to_chans(const struct osw_drv_phy_state *phy, uint32_t *n_chans)
{
    size_t i ;
    const size_t n = phy->n_channel_states;
    uint32_t *chans = MALLOC(sizeof(chans[0]) * n);
    for (i = 0; i < n; i++) {
        const struct osw_channel_state *cs = &phy->channel_states[i];
        const struct osw_channel *c = &cs->channel;
        const int freq = c->control_freq_mhz;
        const int ch = freq_to_chan(freq);
        chans[i] = ch;
    }
    *n_chans = n;
    return chans;
}
#endif

static uint32_t *
phy_to_chan(struct osw_drv_dummy *dummy,
            const char *phy_name)
{
    struct osw_drv_dummy_phy *phy = ds_tree_find(&dummy->phy_tree, phy_name);
    if (phy == NULL) return NULL;

    struct osw_drv_dummy_vif *vif;
    ds_tree_foreach(&dummy->vif_tree, vif) {
        const bool phy_match = (strcmp(vif->phy_name.buf, phy_name) == 0);
        if (phy_match == false) continue;
        if (vif->state.enabled == false) continue;
        if (vif->state.vif_type != OSW_VIF_AP) continue;
        const struct osw_drv_vif_state_ap *vap = &vif->state.u.ap;
        const struct osw_channel *c = &vap->channel;
        const int freq = c->control_freq_mhz;
        const int ch = freq_to_chan(freq);

        return MEMNDUP(&ch, sizeof(ch));
    }

    return NULL;
}

static uint32_t
width_to_mhz(radio_chanwidth_t w)
{
    switch (w) {
        case RADIO_CHAN_WIDTH_NONE: return 0;
        case RADIO_CHAN_WIDTH_20MHZ: return 20;
        case RADIO_CHAN_WIDTH_40MHZ: return 40;
        case RADIO_CHAN_WIDTH_40MHZ_ABOVE: return 40;
        case RADIO_CHAN_WIDTH_40MHZ_BELOW: return 40;
        case RADIO_CHAN_WIDTH_80MHZ: return 80;
        case RADIO_CHAN_WIDTH_160MHZ: return 160;
        case RADIO_CHAN_WIDTH_80_PLUS_80MHZ: return 160;
        case RADIO_CHAN_WIDTH_320MHZ: return 320;
        case RADIO_CHAN_WIDTH_QTY: return 0;
    }
    return 0;
}

static uint32_t
chan_to_freq(radio_type_t t, int c)
{
    switch (t) {
        case RADIO_TYPE_NONE: return 0;
        case RADIO_TYPE_2G:
            if (c == 14) return 2484;
            return 2407 + (5*c);
        case RADIO_TYPE_5G: return 5000 + (5*c);
        case RADIO_TYPE_5GL: return 5000 + (5*c);
        case RADIO_TYPE_5GU: return 5000 + (5*c);
        case RADIO_TYPE_6G:
            if (c == 2) return 5935;
            return 5950 + (5*c);
    }
    return 0;
}

static void
osw_drv_target_bss_fill(struct osw_tlv *t,
                        const char *phy_name,
                        const radio_type_t type,
                        const dpp_neighbor_record_t *n)
{
    LOGT("%s: %s: type=%d bssid=%s chan=%d sig=%d ssid=%s width=%d",
         __func__,
         phy_name,
         n->type,
         n->bssid,
         n->chan,
         n->sig,
         n->ssid,
         n->chanwidth);

    struct osw_hwaddr bssid;
    const uint32_t freq_mhz = chan_to_freq(type, n->chan);
    const uint32_t width_mhz = width_to_mhz(n->chanwidth);
    const uint32_t snr_db = n->sig;
    const char *ssid = n->ssid;
    const size_t ssid_len = strlen(ssid);

    osw_hwaddr_from_cstr(n->bssid, &bssid);

    size_t s = osw_tlv_put_nested(t, OSW_STATS_BSS_SCAN);
    osw_tlv_put_string(t, OSW_STATS_BSS_SCAN_PHY_NAME, phy_name);
    osw_tlv_put_hwaddr(t, OSW_STATS_BSS_SCAN_MAC_ADDRESS, &bssid);
    osw_tlv_put_u32(t, OSW_STATS_BSS_SCAN_FREQ_MHZ, freq_mhz);
    osw_tlv_put_u32(t, OSW_STATS_BSS_SCAN_WIDTH_MHZ, width_mhz);
    osw_tlv_put_u32(t, OSW_STATS_BSS_SCAN_SNR_DB, snr_db);
    osw_tlv_put_buf(t, OSW_STATS_BSS_SCAN_SSID, ssid, ssid_len);
    // TODO: OSW_STATS_BSS_SCAN_IES
    osw_tlv_end_nested(t, s);
}

static void
osw_drv_target_scan_run(struct osw_drv_target *target);

static void
osw_drv_target_scan_free(struct osw_drv_target_scan *scan)
{
    ds_dlist_remove(&scan->target->scans, scan);
    FREE(scan->chans);
    FREE(scan);
}

static bool
osw_drv_target_scan_done_cb(void *priv, int status)
{
    struct osw_drv_target_scan *scan = priv;
    struct osw_drv_target *target = scan->target;
    struct osw_drv *drv = target->drv;
    const char *phy_name = scan->cfg.phy_name;
    struct osw_tlv buf = {0};
    struct osw_tlv *t = scan->target->stats ?: &buf;
    dpp_neighbor_report_data_t report = {
        .list = DS_DLIST_INIT(dpp_neighbor_record_list_t, node),
    };

    LOGT("osw: drv: target: %s: scan: done status=%d", phy_name, status);
    WARN_ON(scan->started == false);

    target_stats_scan_get(&scan->cfg, scan->chans, scan->n_chans, scan->type, &report);

    dpp_neighbor_record_list_t *i;
    while ((i = ds_dlist_remove_head(&report.list)) != NULL) {
        const dpp_neighbor_record_t *n = &i->entry;
        osw_drv_target_bss_fill(t, phy_name, scan->cfg.type, n);
        dpp_neighbor_record_free(i);
    }

    if (t == &buf && buf.used > 0) {
        LOGT("%s: %s: reporting %zu bytes", __func__, scan->cfg.phy_name, buf.used);
        osw_drv_report_stats(drv, &buf);
        osw_tlv_fini(&buf);
    }

    osw_drv_target_scan_free(scan);
    osw_drv_target_scan_run(target);

    return true;
}

static void
osw_drv_target_scan_run(struct osw_drv_target *target)
{
    while (ds_dlist_is_empty(&target->scans) == false) {
        struct osw_drv_target_scan *scan = ds_dlist_head(&target->scans);
        LOGT("osw: drv: target: %s: scan: starting type=%d n_chans=%d",
             scan->cfg.phy_name,
             scan->type,
             scan->n_chans);
        WARN_ON(scan->started);
        scan->started = true;
        const bool ok = target_stats_scan_start(&scan->cfg,
                                                scan->chans,
                                                scan->n_chans,
                                                scan->type,
                                                scan->dwell,
                                                osw_drv_target_scan_done_cb,
                                                scan);
        if (ok == true) return;
        LOGI("osw: drv: target: %s: scan: failed", scan->cfg.phy_name);

        /* In case target called the callback and
         * we cleaned it up, be careful.
         */
        if (ds_dlist_head(&target->scans) == scan) {
            osw_drv_target_scan_free(scan);
        }
    }
}


static const char *
phy_to_scan_vif(struct osw_drv_dummy *dummy, const char *phy_name)
{
    struct osw_drv_dummy_vif *vif;
    ds_tree_foreach(&dummy->vif_tree, vif) {
        if (strcmp(vif->phy_name.buf, phy_name) != 0) continue;
        if (vif->state.enabled == false) continue;
        if (vif->state.vif_type != OSW_VIF_AP) continue;
        return vif->vif_name.buf;
    }
    return NULL;
}

static bool
osw_drv_target_prep_rcfg(radio_entry_t *cfg,
                         struct osw_drv_dummy *dummy,
                         const struct osw_drv_dummy_phy *phy)
{
    const char *phy_name = phy->phy_name.buf;
    const char *vif_name = phy_to_scan_vif(dummy, phy_name);
    if (vif_name == NULL) return false;

    cfg->type = phy_to_type(&phy->state);
    STRSCPY_WARN(cfg->phy_name, phy_name);
    STRSCPY_WARN(cfg->if_name, vif_name);

    return true;
}

static bool
osw_drv_target_scan_is_dupe(struct osw_drv_target *target,
                            struct osw_drv_target_scan *scan)
{
    struct osw_drv_target_scan *i;
    struct ds_dlist *scans = &target->scans;

    WARN_ON(scan->started);

    ds_dlist_foreach(scans, i) {
        WARN_ON(i->n_chans > 0 && i->chans == NULL);
        WARN_ON(scan->n_chans > 0 && scan->chans == NULL);

        const size_t size = i->n_chans * sizeof(*i->chans);
        const bool same = (i->target == scan->target)
                       && (memcmp(&i->cfg, &scan->cfg, sizeof(i->cfg)) == 0)
                       && (i->type == scan->type)
                       && (i->dwell == scan->dwell)
                       && (i->n_chans == scan->n_chans)
                       && (i->n_chans > 0 && memcmp(i->chans, scan->chans, size) == 0)
                       && (i->started == scan->started);

        if (same) {
            return true;
        }
    }

    return false;
}

static void
osw_drv_target_scan_req_phy(struct osw_drv_target *target,
                            struct osw_drv_dummy_phy *phy,
                            const radio_scan_type_t type,
                            uint32_t dwell,
                            uint32_t *chans,
                            uint32_t n_chans)
{
    if (chans == NULL) return;
    if (phy == NULL) return;

    const bool first_req = (ds_dlist_is_empty(&target->scans) == true);
    struct osw_drv_dummy *dummy = &target->dummy;

    struct osw_drv_target_scan *scan = CALLOC(1, sizeof(*scan));
    const bool prep_ok = osw_drv_target_prep_rcfg(&scan->cfg, dummy, phy);
    if (prep_ok == false) {
        FREE(scan);
        return;
    }

    scan->target = target;
    scan->dwell = dwell;
    scan->type = type;
    scan->chans = chans;
    scan->n_chans = n_chans;

    const bool duplicate = osw_drv_target_scan_is_dupe(target, scan);
    if (duplicate) {
        WARN_ON(first_req); /* This should be impossible, but double-check */

        LOGT("osw: drv: target: %s: scan: request duplicate: type=%d dwell=%"PRIu32" n_chans=%"PRIu32,
             scan->cfg.phy_name, type, dwell, n_chans);
        FREE(scan);
        return;
    }

    LOGT("osw: drv: target: %s: scan: requesting (first=%d)", scan->cfg.phy_name, first_req);
    ds_dlist_insert_tail(&target->scans, scan);
    if (first_req == true) osw_drv_target_scan_run(target);
}

static void
osw_drv_target_survey_fill(struct osw_tlv *t,
                           const radio_entry_t *cfg,
                           const int c,
                           const dpp_survey_record_t *delta)
{
    const char *phy_name = cfg->phy_name;

    LOGT("%s: %s: chan=%d active=%u busy=%u ext=%u self=%u rx=%u tx=%u noise=%d duration=%u",
         __func__,
         phy_name,
         c,
         delta->chan_active,
         delta->chan_busy,
         delta->chan_busy_ext,
         delta->chan_self,
         delta->chan_rx,
         delta->chan_tx,
         delta->chan_noise,
         delta->duration_ms);

    /* Reporting an entry with 0 duration makes no sense.
     * It's wasteful, and since it's also used for division,
     * it would cause divide-by-zero fault, so just return.
     */
    if (delta->duration_ms == 0) return;

    size_t start_chan = osw_tlv_put_nested(t, OSW_STATS_CHAN);
    {
        const uint32_t active = delta->duration_ms;
        const float noise = delta->chan_noise;
        const uint32_t freq_mhz = chan_to_freq(cfg->type, c);

        osw_tlv_put_string(t, OSW_STATS_CHAN_PHY_NAME, phy_name);
        osw_tlv_put_u32(t, OSW_STATS_CHAN_FREQ_MHZ, freq_mhz);
        osw_tlv_put_u32_delta(t, OSW_STATS_CHAN_ACTIVE_MSEC, active);
        osw_tlv_put_float(t, OSW_STATS_CHAN_NOISE_FLOOR_DBM, noise);

        size_t start_cnt = osw_tlv_put_nested(t, OSW_STATS_CHAN_CNT_MSEC);
        {
            const uint32_t busy = delta->chan_busy * active / 100;
            const uint32_t tx = delta->chan_tx * active / 100;
            const uint32_t rx = delta->chan_rx * active / 100;
            const uint32_t inbss = delta->chan_self * active / 100;

            osw_tlv_put_u32_delta(t, OSW_STATS_CHAN_CNT_TX, tx);
            osw_tlv_put_u32_delta(t, OSW_STATS_CHAN_CNT_RX, rx);
            osw_tlv_put_u32_delta(t, OSW_STATS_CHAN_CNT_RX_INBSS, inbss);
            osw_tlv_put_u32_delta(t, OSW_STATS_CHAN_CNT_BUSY, busy);
        }
        osw_tlv_end_nested(t, start_cnt);
    }
    osw_tlv_end_nested(t, start_chan);
}

static bool
osw_drv_target_survey_cb(ds_dlist_t *list, void *priv, int status)
{
    struct osw_drv_target_survey *s = priv;
    struct osw_drv *drv = s->target->drv;
    target_survey_record_t *record;
    assert(&s->list == list);
    struct osw_tlv buf = {0};
    struct osw_tlv *t = s->target->stats ?: &buf;

    LOGT("%s: %s: status = %d", __func__, s->phy_name, status);

    while ((record = ds_dlist_remove_head(list)) != NULL) {
        const int c = record->info.chan;
        struct osw_drv_target_survey_entry *e = ds_tree_find(&s->entries, &c);

        if (e == NULL) {
            e = CALLOC(1, sizeof(*e));
            e->channel = c;
            ds_tree_insert(&s->entries, e, &e->channel);
        }

        if (e->record != NULL) {
            dpp_survey_record_t delta;
            MEMZERO(delta);
            target_survey_record_t *old_record = e->record;
            target_survey_record_t *new_record = record;
            const bool ok = target_stats_survey_convert(&s->cfg,
                                                        s->type,
                                                        new_record,
                                                        old_record,
                                                        &delta);
            if (ok == true) {
                osw_drv_target_survey_fill(t, &s->cfg, c, &delta);
            }

            target_survey_record_free(e->record);
        }

        e->record = record;
    }

    if (t == &buf && buf.used > 0) {
        LOGT("%s: %s: reporting %zu bytes", __func__, s->phy_name, buf.used);
        osw_drv_report_stats(drv, &buf);
        osw_tlv_fini(&buf);
    }

    s->requested = false;
    return true;
}

static void
osw_drv_target_survey_req_phy(struct osw_drv_target *target,
                              struct osw_drv_dummy_phy *phy)
{
    const char *phy_name = phy->phy_name.buf;
    struct osw_drv_dummy *dummy = &target->dummy;
    struct osw_drv_target_survey *s = ds_tree_find(&target->surveys, phy_name);
    if (s == NULL) {
        s = CALLOC(1, sizeof(*s));
        s->phy_name = STRDUP(phy_name);
        s->target = target;
        s->type = RADIO_SCAN_TYPE_ONCHAN;
        ds_tree_init(&s->entries, ds_int_cmp, struct osw_drv_target_survey_entry, node);
        ds_dlist_init(&s->list, target_survey_record_t, node);
        ds_tree_insert(&target->surveys, s, s->phy_name);
    }

    const bool pok = osw_drv_target_prep_rcfg(&s->cfg, dummy, phy);
    LOGT("%s: %s: prep=%d requested=%d", __func__, phy_name, pok, s->requested);
    if (pok == false) return;
    if (s->requested == true) return;

    const uint32_t n = 1;
    uint32_t *chans = phy_to_chan(dummy, phy_name);
    LOGT("%s: %s: chan=%p (%u)", __func__, phy_name, chans, chans ? chans[0] : 0);
    if (chans == NULL) return;

    s->requested = true;
    const bool gok = target_stats_survey_get(&s->cfg, chans, n, s->type, osw_drv_target_survey_cb, &s->list, s);
    LOGT("%s: %s: get=%d", __func__, phy_name, gok);
    if (gok == false) s->requested = false;
    FREE(chans);
}

static void
osw_drv_target_request_stats_cb(struct osw_drv *drv,
                                unsigned int stats_mask)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    struct osw_drv_target *target = container_of(dummy, struct osw_drv_target, dummy);
    struct osw_drv_dummy_phy *phy;
    struct osw_tlv t = {0};

    target->stats = &t;
    ds_tree_foreach(&target->dummy.phy_tree, phy) {
        const char *phy_name = phy->phy_name.buf;
        const radio_scan_type_t type = RADIO_SCAN_TYPE_ONCHAN;
        uint32_t *chans = phy_to_chan(dummy, phy_name);
        const uint32_t n_chans = chans ? 1 : 0;
        const uint32_t dwell = 0;

        osw_drv_target_survey_req_phy(target, phy);
        osw_drv_target_scan_req_phy(target, phy, type, dwell, chans, n_chans);
    }

    osw_drv_target_fill_sta_stats(dummy, &t);
    osw_drv_report_stats(drv, &t);
    osw_tlv_fini(&t);
    target->stats = NULL;
}

static void
osw_drv_target_request_sta_deauth_cb(struct osw_drv *drv,
                                     const char *phy_name,
                                     const char *vif_name,
                                     const struct osw_hwaddr *mac_addr,
                                     int dot11_reason_code)
{
    const bsal_disc_type_t type = BSAL_DISC_TYPE_DEAUTH;
    /* FIXME: Calling BSAL API isn't ideal, but is
     * quick and easy. It fits the bill for now.
     */
    const int err = target_bsal_client_disconnect(vif_name,
                                                  mac_addr->octet,
                                                  type,
                                                  dot11_reason_code);
    WARN_ON(err != 0);
}

static struct osw_drv_target g_osw_drv_target = {
    .dummy = {
        .name = "target (legacy)",
        .init_fn = osw_drv_target_init_cb,
        .fini_phy_fn = osw_drv_target_fini_phy_cb,
        .fini_vif_fn = osw_drv_target_fini_vif_cb,
        .request_config_fn = osw_drv_target_request_config_cb,
        .request_stats_fn = osw_drv_target_request_stats_cb,
        .request_sta_deauth_fn = osw_drv_target_request_sta_deauth_cb,
        .push_frame_tx_fn = osw_drv_target_push_frame_tx_cb,
    },
    .phy_tree = DS_TREE_INIT(ds_str_cmp, struct osw_drv_target_phy, node),
    .vif_tree = DS_TREE_INIT(ds_str_cmp, struct osw_drv_target_vif, node),
    .bsal_vap_tree = DS_TREE_INIT(ds_str_cmp, struct osw_drv_target_bsal_vap, node),
    .bsal_ue_tree = DS_TREE_INIT((ds_key_cmp_t*) osw_hwaddr_cmp, struct osw_drv_target_bsal_ue, node),
};

static int
osw_drv_target_ch2freq(int band_num, int chan)
{
    switch (band_num) {
        case 2: return 2407 + (chan * 5);
        case 5: return 5000 + (chan * 5);
        case 6: if (chan == 2) return 5935;
                else return 5950 + (chan * 5);
    }
    return 0;
}

static int
osw_drv_target_band2num(const char *freq_band)
{
    return strcmp(freq_band, "5G") == 0 ? 5 :
           strcmp(freq_band, "5GL") == 0 ? 5 :
           strcmp(freq_band, "5GU") == 0 ? 5 :
           strcmp(freq_band, "6G") == 0 ? 6 :
           2;
}

static enum osw_acl_policy
osw_drv_target_type2policy(const char *type)
{
    return strcmp(type, "none") == 0 ? OSW_ACL_NONE :
           strcmp(type, "whitelist") == 0 ? OSW_ACL_ALLOW_LIST :
           strcmp(type, "blacklist") == 0 ? OSW_ACL_DENY_LIST :
           OSW_ACL_NONE;
}

static enum osw_channel_width
osw_drv_target_htmode2width(const char *ht_mode)
{
    return strcmp(ht_mode, "HT20") == 0 ? OSW_CHANNEL_20MHZ :
           strcmp(ht_mode, "HT40") == 0 ? OSW_CHANNEL_40MHZ :
           strcmp(ht_mode, "HT80") == 0 ? OSW_CHANNEL_80MHZ :
           strcmp(ht_mode, "HT160") == 0 ? OSW_CHANNEL_160MHZ :
           strcmp(ht_mode, "HT320") == 0 ? OSW_CHANNEL_320MHZ :
           OSW_CHANNEL_20MHZ;
}

static int
osw_drv_target_str2keyid(const char *key_id)
{
    int i;
    if (strcmp(key_id, "key") == 0)
        return 0;
    if (sscanf(key_id, "key-%d", &i) == 1)
        return i;
    return -1;
}

static void
osw_drv_target_keyid2str(char *buf, const size_t len, const int key_id)
{
    if (key_id == 0)
        snprintf(buf, len, "key");
    else
        snprintf(buf, len, "key-%d", key_id);
}

static void
osw_drv_target_phyconf2schema(const struct osw_drv_phy_config *phy,
                              const struct osw_state_phy_info *info,
                              const struct schema_Wifi_Radio_Config *base_rconf,
                              struct schema_Wifi_Radio_Config *rconf,
                              struct schema_Wifi_Radio_Config_flags *rchanged)
{
    const struct osw_channel *c = NULL;
    const int *beacon_interval_tu = NULL;
    size_t i;
    bool channel_changed = false;
    bool mode_changed = false;
    bool beacon_interval_tu_changed = false;
    bool ht = false;
    bool vht = false;
    bool he = false;

    SCHEMA_SET_STR(rconf->if_name, phy->phy_name);
    SCHEMA_SET_BOOL(rconf->enabled, phy->enabled);
    SCHEMA_SET_INT(rconf->tx_chainmask, phy->tx_chainmask);

    switch (phy->radar) {
        case OSW_RADAR_UNSUPPORTED: break;
        case OSW_RADAR_DETECT_ENABLED: SCHEMA_SET_BOOL(rconf->dfs_demo, false); break;
        case OSW_RADAR_DETECT_DISABLED: SCHEMA_SET_BOOL(rconf->dfs_demo, true); break;
    }

    for (i = 0; i < phy->vif_list.count; i++) {
        struct osw_drv_vif_config *vif = &phy->vif_list.list[i];
        if (vif->vif_type != OSW_VIF_AP) continue;
        if (vif->enabled == false) continue;
        channel_changed |= vif->u.ap.channel_changed;
        if (c == NULL) c = &vif->u.ap.channel;
        if (memcmp(c, &vif->u.ap.channel, sizeof(*c)) == 0) continue;
        c = NULL;
        break;
    }

    if (c != NULL) {
        const int b2ch1 = 2412;
        const int b2ch13 = 2472;
        const int b2ch14 = 2484;
        const int b5ch36 = 5180;
        const int b5ch177 = 5885;
        const int b6ch1 = 5955;
        const int b6ch2 = 5935;
        const int b6ch233 = 7115;

        switch (c->width) {
            case OSW_CHANNEL_20MHZ: SCHEMA_SET_STR(rconf->ht_mode, "HT20"); break;
            case OSW_CHANNEL_40MHZ: SCHEMA_SET_STR(rconf->ht_mode, "HT40"); break;
            case OSW_CHANNEL_80MHZ: SCHEMA_SET_STR(rconf->ht_mode, "HT80"); break;
            case OSW_CHANNEL_160MHZ: SCHEMA_SET_STR(rconf->ht_mode, "HT160"); break;
            case OSW_CHANNEL_320MHZ: SCHEMA_SET_STR(rconf->ht_mode, "HT320"); break;
            case OSW_CHANNEL_80P80MHZ: break;
        }
        if (c->control_freq_mhz >= b2ch1 && c->control_freq_mhz <= b2ch13) {
            SCHEMA_SET_INT(rconf->channel, (c->control_freq_mhz - 2407) / 5);
            SCHEMA_SET_STR(rconf->freq_band, "2.4G");
        }
        else if (c->control_freq_mhz == b2ch14) {
            SCHEMA_SET_INT(rconf->channel, 14);
            SCHEMA_SET_STR(rconf->freq_band, "2.4G");
        }
        else if (c->control_freq_mhz >= b5ch36 && c->control_freq_mhz <= b5ch177) {
            SCHEMA_SET_INT(rconf->channel, (c->control_freq_mhz - 5000) / 5);
            SCHEMA_SET_STR(rconf->freq_band, "5G");

            /* FIXME: This is a hack. Driver should not be reading it's own
             * states like this. This is a compatibility layer so this is
             * acceptable.
             */
            if (info != NULL) {
                const struct osw_channel_state *cs = info->drv_state->channel_states;
                const size_t n = info->drv_state->n_channel_states;
                const int high_5gl = 5000 + (5 * 64);
                const int low_5gu = 5000 + (5 * 100);
                bool has_5gl = false;
                bool has_5gu = false;
                for (i = 0; i < n; i++) {
                    if (cs[i].channel.control_freq_mhz <= high_5gl)
                        has_5gl = true;
                    if (cs[i].channel.control_freq_mhz >= low_5gu)
                        has_5gu = true;
                }
                if (has_5gl != has_5gu)
                    SCHEMA_SET_STR(rconf->freq_band, has_5gl ? "5GL" : "5GU");
            }
        }
        else if (c->control_freq_mhz == b6ch2) {
            SCHEMA_SET_INT(rconf->channel, 2);
            SCHEMA_SET_STR(rconf->freq_band, "6G");
        }
        else if (c->control_freq_mhz >= b6ch1 && c->control_freq_mhz <= b6ch233) {
            SCHEMA_SET_INT(rconf->channel, (c->control_freq_mhz - 5950) / 5);
            SCHEMA_SET_STR(rconf->freq_band, "6G");
        }
    }

    for (i = 0; i < phy->vif_list.count; i++) {
        struct osw_drv_vif_config *vif = &phy->vif_list.list[i];
        if (vif->vif_type != OSW_VIF_AP) continue;
        mode_changed |= vif->u.ap.mode_changed;
        ht |= vif->u.ap.mode.ht_enabled;
        vht |= vif->u.ap.mode.vht_enabled;
        he |= vif->u.ap.mode.he_enabled;
    }

    if (he == true)
        SCHEMA_SET_STR(rconf->hw_mode, "11ax");
    else if (vht == true)
        SCHEMA_SET_STR(rconf->hw_mode, "11ac");
    else if (ht == true)
        SCHEMA_SET_STR(rconf->hw_mode, "11n");
    else if (strcmp(rconf->freq_band, "2.4G") == 0)
        SCHEMA_SET_STR(rconf->hw_mode, "11g");
    else if (strcmp(rconf->freq_band, "5G") == 0)
        SCHEMA_SET_STR(rconf->hw_mode, "11a");
    else if (strcmp(rconf->freq_band, "5GU") == 0)
        SCHEMA_SET_STR(rconf->hw_mode, "11a");
    else if (strcmp(rconf->freq_band, "5GL") == 0)
        SCHEMA_SET_STR(rconf->hw_mode, "11a");
    else if (strcmp(rconf->freq_band, "6G") == 0)
        SCHEMA_SET_STR(rconf->hw_mode, "11a");

    for (i = 0; i < phy->vif_list.count; i++) {
        struct osw_drv_vif_config *vif = &phy->vif_list.list[i];
        if (vif->vif_type != OSW_VIF_AP) continue;
        if (vif->enabled == false) continue;
        beacon_interval_tu_changed |= vif->u.ap.beacon_interval_tu_changed;
        if (beacon_interval_tu == NULL) beacon_interval_tu = &vif->u.ap.beacon_interval_tu;
        if (vif->u.ap.beacon_interval_tu == *beacon_interval_tu) continue;
        beacon_interval_tu = NULL;
        break;
    }

    if (beacon_interval_tu != NULL && *beacon_interval_tu > 0) {
        SCHEMA_SET_INT(rconf->bcn_int, *beacon_interval_tu);
    }

    rchanged->enabled = phy->enabled_changed;
    rchanged->tx_chainmask = phy->tx_chainmask_changed;
    rchanged->dfs_demo = phy->radar_changed;
    rchanged->channel = channel_changed;
    rchanged->freq_band = channel_changed;
    rchanged->ht_mode = channel_changed;
    rchanged->hw_mode = mode_changed;
    rchanged->bcn_int = beacon_interval_tu_changed;

    if (base_rconf != NULL) {
        SCHEMA_CPY_STR(rconf->hw_type, base_rconf->hw_type);
        SCHEMA_CPY_MAP(rconf->hw_config, base_rconf->hw_config);
        /* FIXME: No way to know whether it has changed or not */
    }


    /* FIXME
       tx_power
       country
       zero_wait_dfs

       channel_sync
       vif_configs
       thermal_shutdown
       thermal_downgrade_temp
       thermal_upgrade_temp
       thermal_integration
       temperature_control
       thermal_tx_chainmask
       fallback_parents
     */
}

static const char *
pmf_to_str(enum osw_pmf pmf)
{
    switch (pmf) {
        case OSW_PMF_DISABLED: return SCHEMA_CONSTS_SECURITY_PMF_DISABLED;
        case OSW_PMF_OPTIONAL: return SCHEMA_CONSTS_SECURITY_PMF_OPTIONAL;
        case OSW_PMF_REQUIRED: return SCHEMA_CONSTS_SECURITY_PMF_REQUIRED;
    }
    return SCHEMA_CONSTS_SECURITY_PMF_DISABLED;
}

static enum osw_pmf
pmf_from_str(const char *str)
{
    if (str == NULL) return OSW_PMF_DISABLED;
    if (strcmp(str, SCHEMA_CONSTS_SECURITY_PMF_DISABLED) == 0) return OSW_PMF_DISABLED;
    if (strcmp(str, SCHEMA_CONSTS_SECURITY_PMF_OPTIONAL) == 0) return OSW_PMF_OPTIONAL;
    if (strcmp(str, SCHEMA_CONSTS_SECURITY_PMF_REQUIRED) == 0) return OSW_PMF_REQUIRED;
    return OSW_PMF_DISABLED;
}

static void
osw_drv_target_wpa2schema(struct schema_Wifi_VIF_Config *vconf,
                          struct schema_Wifi_VIF_Config_flags *vchanged,
                          const struct osw_wpa *wpa,
                          bool wpa_changed)
{
    if (wpa->group_rekey_seconds >= 0)
        SCHEMA_SET_INT(vconf->group_rekey, wpa->group_rekey_seconds);

    SCHEMA_SET_INT(vconf->ft_mobility_domain, wpa->ft_mobility_domain);
    SCHEMA_SET_BOOL(vconf->wpa, wpa->rsn || wpa->wpa);
    SCHEMA_SET_BOOL(vconf->wpa_pairwise_tkip, wpa->wpa && wpa->pairwise_tkip);
    SCHEMA_SET_BOOL(vconf->wpa_pairwise_ccmp, wpa->wpa && wpa->pairwise_ccmp);
    SCHEMA_SET_BOOL(vconf->rsn_pairwise_tkip, wpa->rsn && wpa->pairwise_tkip);
    SCHEMA_SET_BOOL(vconf->rsn_pairwise_ccmp, wpa->rsn && wpa->pairwise_ccmp);
    SCHEMA_SET_STR(vconf->pmf, pmf_to_str(wpa->pmf));

    if (wpa->akm_psk)    SCHEMA_VAL_APPEND(vconf->wpa_key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK);
    if (wpa->akm_sae)    SCHEMA_VAL_APPEND(vconf->wpa_key_mgmt, SCHEMA_CONSTS_KEY_SAE);
    if (wpa->akm_ft_psk) SCHEMA_VAL_APPEND(vconf->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_PSK);
    if (wpa->akm_ft_sae) SCHEMA_VAL_APPEND(vconf->wpa_key_mgmt, SCHEMA_CONSTS_KEY_FT_SAE);

    vchanged->wpa = wpa_changed;
    vchanged->wpa_key_mgmt = wpa_changed;
}

static void
osw_drv_target_vifconf2schema(struct osw_drv_vif_config *vif,
                              const struct schema_Wifi_VIF_Config *base_vconf,
                              struct schema_Wifi_VIF_Config *vconf,
                              struct schema_Wifi_VIF_Config_flags *vchanged,
                              struct schema_Wifi_Credential_Config *cconfs,
                              int *n_cconfs)
{
    struct osw_drv_vif_config_ap *ap = &vif->u.ap;
    struct osw_drv_vif_config_sta *sta = &vif->u.sta;
    struct osw_hwaddr_str mac_str;
    size_t i;

    SCHEMA_SET_STR(vconf->if_name, vif->vif_name);
    SCHEMA_SET_BOOL(vconf->enabled, vif->enabled);

    vchanged->enabled = vif->enabled_changed;

    /* FIXME: vif_radio_idx needs to be taken from real
     * ovsdb, it is nowhere to be found, and will nowhere to
     * be found in OSW.
     */

    switch (vif->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            ap->ssid.buf[ap->ssid.len] = 0; /* sanity */
            SCHEMA_SET_STR(vconf->mode, "ap");
            SCHEMA_SET_STR(vconf->ssid, ap->ssid.buf);
            SCHEMA_SET_STR(vconf->bridge, ap->bridge_if_name.buf);
            SCHEMA_SET_STR(vconf->ssid_broadcast, ap->ssid_hidden ? "disabled" : "enabled");
            switch (ap->acl_policy) {
                case OSW_ACL_NONE: SCHEMA_SET_STR(vconf->mac_list_type, "none"); break;
                case OSW_ACL_ALLOW_LIST: SCHEMA_SET_STR(vconf->mac_list_type, "whitelist"); break;
                case OSW_ACL_DENY_LIST: SCHEMA_SET_STR(vconf->mac_list_type, "blacklist"); break;
            }
            SCHEMA_SET_BOOL(vconf->ap_bridge, ap->isolated ? false : true);
            SCHEMA_SET_BOOL(vconf->mcast2ucast, ap->mcast2ucast);
            for (i = 0; i < ap->acl.count; i++) {
                SCHEMA_VAL_APPEND(vconf->mac_list,
                                  osw_hwaddr2str(&ap->acl.list[i], &mac_str));
            }
            for (i = 0; i < ap->psk_list.count; i++) {
                const struct osw_ap_psk *psk = &ap->psk_list.list[i];
                char key_id[64];
                osw_drv_target_keyid2str(key_id, sizeof(key_id), psk->key_id);
                SCHEMA_KEY_VAL_SET(vconf->wpa_psks, key_id, psk->psk.str);
            }
            SCHEMA_SET_BOOL(vconf->btm, ap->mode.wnm_bss_trans);
            SCHEMA_SET_BOOL(vconf->rrm, ap->mode.rrm_neighbor_report);
            SCHEMA_SET_BOOL(vconf->uapsd_enable, ap->mode.wmm_uapsd_enabled);
            SCHEMA_SET_BOOL(vconf->wps, ap->mode.wps);
            osw_drv_target_wpa2schema(vconf, vchanged, &ap->wpa, ap->wpa_changed);

            // FIXME: ft, 802.1x, dpp

            vchanged->mode = vif->vif_type_changed;
            vchanged->ssid = ap->ssid_changed;
            vchanged->bridge = ap->bridge_if_name_changed;
            vchanged->ssid_broadcast = ap->ssid_hidden_changed;
            vchanged->ap_bridge = ap->isolated_changed;
            vchanged->mac_list_type = ap->acl_policy_changed;
            vchanged->mcast2ucast = ap->mcast2ucast_changed;
            vchanged->mac_list = ap->acl_changed;
            vchanged->wpa_psks = ap->psk_list_changed;
            vchanged->btm = ap->mode_changed;
            vchanged->rrm = ap->mode_changed;
            vchanged->uapsd_enable = ap->mode_changed;
            vchanged->wps = ap->mode_changed;
            vchanged->wpa = ap->wpa_changed;
            vchanged->wpa_key_mgmt = ap->wpa_changed;
            vchanged->ft_mobility_domain = ap->wpa_changed;
            vchanged->group_rekey = ap->wpa_changed;

            if (base_vconf != NULL) {
                if (base_vconf->vif_radio_idx_exists == true) {
                    SCHEMA_SET_INT(vconf->vif_radio_idx, base_vconf->vif_radio_idx);
                    /* FIXME: Unfortunately it's impossible to tell whether
                     * vif_radio_idx has been applied or not, so it's
                     * impossible to tell if it has changed or not.
                     */
                }
            }

            /* FIXME
               parent
               vif_dbg_lvl
               wds
               security
               credential_configs
               vlan_id
               min_hw_mode
               ft_psk
               dynamic_beacon
               multi_ap
               wps_pbc
               wps_pbc_key_id
               wpa_oftags
               radius_srv_addr
               radius_srv_port
               radius_srv_secret
               default_oftag
               dpp_connector
               dpp_csign_hex
               dpp_netaccesskey_hex
               dpp_cc
               min_rssi
               max_sta
               passpoint_hessid
               airtime_precedence
             */
            break;
        case OSW_VIF_AP_VLAN:
            SCHEMA_SET_STR(vconf->mode, "ap_vlan");
            /* XXX: This isn't expected to be supported any
             * time soon. These interfaces are expected to
             * be only spawned by the underlying driver via
             * state reports for Multi AP purposes.
             */
            break;
        case OSW_VIF_STA:
            SCHEMA_SET_STR(vconf->mode, "sta");

            /* FIXME: STA is always expected to be the
             * first/primary interface on a radio implying
             * that its mac address should be identical to
             * the radio's base mac address for ACL
             * filtering purposes on root APs. This isn't
             * ideal in this case, but that's what the
             * underlying API expects so play along.
             */
            SCHEMA_SET_INT(vconf->vif_radio_idx, 0);

            struct osw_drv_vif_sta_network *net = sta->network;
            if (net != NULL) {
                if (net->next == NULL) {
                    osw_drv_target_wpa2schema(vconf, vchanged, &net->wpa,
                                              sta->network_changed);
                    SCHEMA_KEY_VAL_APPEND(vconf->wpa_psks, "key", net->psk.str);
                    SCHEMA_SET_STR(vconf->ssid, net->ssid.buf);
                    if (osw_hwaddr_is_zero(&net->bssid) == false) {
                        const char *str = osw_hwaddr2str(&net->bssid, &mac_str);
                        SCHEMA_SET_STR(vconf->parent, str);
                    }
                    *n_cconfs = 0;
                }
                else {
                    /* More than 1 network. Typically onboarding case. */
                    int j = 0;
                    while (net && j < *n_cconfs) {
                        struct schema_Wifi_Credential_Config *c = &cconfs[j];
                        SCHEMA_SET_STR(c->ssid, net->ssid.buf);
                        SCHEMA_SET_STR(c->onboard_type, "gre");
                        /* FIXME: This assumes only WPA-PSK. This is fine for
                         * now because nothing else is really used and this
                         * adapter code is a throw-away.
                         */
                        SCHEMA_KEY_VAL_APPEND(c->security, "encryption", "WPA-PSK");
                        SCHEMA_KEY_VAL_APPEND(c->security, "key", net->psk.str);
                        net = net->next;
                        j++;
                    }
                    *n_cconfs = j;
                }
            }
            break;
    }
}

static void
osw_drv_target_cs_append(struct osw_channel_state **cs,
                         size_t *n_cs,
                         const int freq,
                         const enum osw_channel_state_dfs dfs_state)
{
    int last = *n_cs;
    (*n_cs)++;
    *cs = REALLOC(*cs, *n_cs * sizeof(**cs));
    memset(&(*cs)[last], 0, sizeof(**cs));
    (*cs)[last].channel.control_freq_mhz = freq;
    (*cs)[last].dfs_state = dfs_state;
}

static struct osw_channel_state *
osw_drv_target_cs_lookup(struct osw_channel_state *base,
                         size_t n,
                         int freq)
{
    while (n--) {
        if (base->channel.control_freq_mhz == freq)
            return base;
        base++;
    }
    return NULL;
}

static enum osw_channel_state_dfs
osw_drv_target_str_to_dfs_state(const char *str)
{
    if (strstr(str, "allowed") != NULL) return OSW_CHANNEL_NON_DFS;
    if (strstr(str, "nop_finished") != NULL) return OSW_CHANNEL_DFS_CAC_POSSIBLE;
    if (strstr(str, "nop_started") != NULL) return OSW_CHANNEL_DFS_NOL;
    if (strstr(str, "cac_started") != NULL) return OSW_CHANNEL_DFS_CAC_IN_PROGRESS;
    if (strstr(str, "cac_completed") != NULL) return OSW_CHANNEL_DFS_CAC_COMPLETED;
    return OSW_CHANNEL_NON_DFS;
}

static void
osw_drv_target_schema2phystate(const struct schema_Wifi_Radio_State *rstate,
                               struct osw_drv_phy_state *info)
{
    int band_num = osw_drv_target_band2num(rstate->freq_band);
    int i;

    info->exists = true;
    info->enabled = rstate->enabled;
    info->tx_chainmask = rstate->tx_chainmask;
    if (rstate->dfs_demo_exists) {
        info->radar = rstate->dfs_demo == true
                    ? OSW_RADAR_DETECT_DISABLED
                    : OSW_RADAR_DETECT_ENABLED;
    }
    sscanf(rstate->mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
           &info->mac_addr.octet[0],
           &info->mac_addr.octet[1],
           &info->mac_addr.octet[2],
           &info->mac_addr.octet[3],
           &info->mac_addr.octet[4],
           &info->mac_addr.octet[5]);

    if (strlen(rstate->country) > 0) {
        sscanf(rstate->country, "%c%c",
               &info->reg_domain.ccode[0],
               &info->reg_domain.ccode[1]);
        const char *slash = strstr(rstate->country, "/");
        if (slash != NULL) {
            info->reg_domain.revision = atoi(slash+1);
        }
    }

    if (rstate->hw_params_len > 0) {
        const char *country_id = SCHEMA_KEY_VAL_NULL(rstate->hw_params, "country_id");
        const char *reg_domain = SCHEMA_KEY_VAL_NULL(rstate->hw_params, "reg_domain");
        if (country_id != NULL && reg_domain != NULL) {
            info->reg_domain.ccode[0] = '0';
            info->reg_domain.ccode[1] = '0';
            info->reg_domain.revision = atoi(reg_domain);
            /* FIXME: Infer OSW_DRV_REG_DFS_ to info->reg_domain.dfs */
#if 0
            switch (atoi(reg_domain)) {
                case 0x3a: info->reg_domain.dfs = OSW_DRV_REG_DFS_FCC; break;
                case 0x37: info->reg_domain.dfs = OSW_DRV_REG_DFS_ETSI; break;
                case 0x833a:
                case 0x8faf: info->reg_domain.dfs = OSW_DRV_REG_DFS_FCC; break;
                case 0x0014:
                case 0x0014:
                case 0x822a:
                case 0x82be:
                case 0x8178:
                case 0x8158:
            }
#endif
        }
    }

    for (i = 0; i < rstate->channels_len; i++) {
        const int ch = atoi(rstate->channels_keys[i]);
        const int freq = osw_drv_target_ch2freq(band_num, ch);
        const char *str = rstate->channels[i];
        const enum osw_channel_state_dfs dfs_state = osw_drv_target_str_to_dfs_state(str);

        osw_drv_target_cs_append(&info->channel_states,
                                 &info->n_channel_states,
                                 freq,
                                 dfs_state);
    }

    for (i = 0; i < rstate->allowed_channels_len; i++) {
        const int ch = rstate->allowed_channels[i];
        const int freq = osw_drv_target_ch2freq(band_num, ch);
        const enum osw_channel_state_dfs dfs_state = OSW_CHANNEL_NON_DFS;

        if (osw_drv_target_cs_lookup(info->channel_states,
                                     info->n_channel_states,
                                     freq) != NULL) continue;

        osw_drv_target_cs_append(&info->channel_states,
                                 &info->n_channel_states,
                                 freq,
                                 dfs_state);
    }
}

static void
osw_drv_target_vconf2vstate(const struct schema_Wifi_VIF_Config *vconf,
                            struct schema_Wifi_VIF_State *vstate)
{
    SCHEMA_CPY_STR(vstate->pmf, vconf->pmf);
    SCHEMA_CPY_INT(vstate->ft_mobility_domain, vconf->ft_mobility_domain);
    SCHEMA_CPY_INT(vstate->wpa, vconf->wpa);
    SCHEMA_CPY_INT(vstate->wpa_pairwise_tkip, vconf->wpa_pairwise_tkip);
    SCHEMA_CPY_INT(vstate->rsn_pairwise_tkip, vconf->rsn_pairwise_tkip);
    SCHEMA_CPY_INT(vstate->wpa_pairwise_ccmp, vconf->wpa_pairwise_ccmp);
    SCHEMA_CPY_INT(vstate->rsn_pairwise_ccmp, vconf->rsn_pairwise_ccmp);

    int i;
    for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
        SCHEMA_VAL_APPEND(vstate->wpa_key_mgmt, vconf->wpa_key_mgmt[i]);
    }
}

static void
osw_drv_target_schema2wpa(const struct schema_Wifi_VIF_State *vstate,
                          struct osw_wpa *wpa)
{
    wpa->group_rekey_seconds = OSW_WPA_GROUP_REKEY_UNDEFINED;

    if (vstate->wpa == false) return;

    wpa->group_rekey_seconds = vstate->group_rekey_exists == true
                             ? vstate->group_rekey
                             : OSW_WPA_GROUP_REKEY_UNDEFINED;
    wpa->ft_mobility_domain = vstate->ft_mobility_domain;
    wpa->wpa = vstate->wpa_pairwise_tkip
            || vstate->wpa_pairwise_ccmp;
    wpa->rsn = vstate->rsn_pairwise_tkip
            || vstate->rsn_pairwise_ccmp;
    wpa->pairwise_tkip = vstate->wpa_pairwise_tkip
                      || vstate->rsn_pairwise_tkip;
    wpa->pairwise_ccmp = vstate->wpa_pairwise_ccmp
                      || vstate->rsn_pairwise_ccmp;
    wpa->pmf = pmf_from_str(vstate->pmf);

    int i;
    for (i = 0; i < vstate->wpa_key_mgmt_len; i++) {
        const char *v = vstate->wpa_key_mgmt[i];
             if (strcmp(v, SCHEMA_CONSTS_KEY_WPA_PSK) == 0) wpa->akm_psk = true;
        else if (strcmp(v, SCHEMA_CONSTS_KEY_SAE) == 0) wpa->akm_sae = true;
        else if (strcmp(v, SCHEMA_CONSTS_KEY_FT_PSK) == 0) wpa->akm_ft_psk = true;
        else if (strcmp(v, SCHEMA_CONSTS_KEY_FT_SAE) == 0) wpa->akm_ft_sae = true;
        else LOGW("%s: unsupported akm: %s", vstate->if_name, v);
    }
}

static void
osw_drv_target_schema2vifstate(const struct schema_Wifi_VIF_State *vstate,
                               const struct schema_Wifi_Radio_State *rstate,
                               struct osw_drv_vif_state *info)
{
    struct osw_drv_vif_state_sta *vsta = &info->u.sta;

    info->enabled = vstate->enabled;
    info->vif_type = strcmp(vstate->mode, "ap") == 0 ? OSW_VIF_AP :
                     strcmp(vstate->mode, "ap_vlan") == 0 ? OSW_VIF_AP_VLAN :
                     strcmp(vstate->mode, "sta") == 0 ? OSW_VIF_STA :
                     OSW_VIF_UNDEFINED;
    sscanf(vstate->mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
           &info->mac_addr.octet[0],
           &info->mac_addr.octet[1],
           &info->mac_addr.octet[2],
           &info->mac_addr.octet[3],
           &info->mac_addr.octet[4],
           &info->mac_addr.octet[5]);

    switch (info->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            STRSCPY_WARN(info->u.ap.bridge_if_name.buf, vstate->bridge);
            STRSCPY_WARN(info->u.ap.ssid.buf, vstate->ssid);
            info->u.ap.ssid.len = strlen(vstate->ssid);
            info->u.ap.beacon_interval_tu = rstate->bcn_int;
            info->u.ap.isolated = vstate->ap_bridge_exists == true
                                ? (vstate->ap_bridge == true ? false : true)
                                : false;
            info->u.ap.mcast2ucast = vstate->mcast2ucast;
            info->u.ap.acl_policy = osw_drv_target_type2policy(vstate->mac_list_type);
            info->u.ap.ssid_hidden = strcmp(vstate->ssid_broadcast, "enabled") == 0 ? false :
                                     strcmp(vstate->ssid_broadcast, "disabled") == 0 ? true :
                                     false;
            info->u.ap.mode.wnm_bss_trans = vstate->btm;
            info->u.ap.mode.rrm_neighbor_report = vstate->rrm;
            info->u.ap.mode.wmm_enabled = true;
            info->u.ap.mode.wmm_uapsd_enabled = vstate->uapsd_enable;
            info->u.ap.mode.wps = vstate->wps;
            if (strcmp(rstate->hw_mode, "11ax") == 0) {
                info->u.ap.mode.ht_enabled = true;
                info->u.ap.mode.vht_enabled = true;
                info->u.ap.mode.he_enabled = true;
            }
            else if (strcmp(rstate->hw_mode, "11ac") == 0) {
                info->u.ap.mode.ht_enabled = true;
                info->u.ap.mode.vht_enabled = true;
            }
            else if (strcmp(rstate->hw_mode, "11n") == 0) {
                info->u.ap.mode.ht_enabled = true;
            }
            if (strcmp(rstate->freq_band, "6G") == 0) {
                info->u.ap.mode.ht_enabled = false;
                info->u.ap.mode.vht_enabled = false;
            }
            if (vstate->channel_exists) {
                int band_num = osw_drv_target_band2num(rstate->freq_band);
                info->u.ap.channel.control_freq_mhz = osw_drv_target_ch2freq(band_num, vstate->channel);
                info->u.ap.channel.width = osw_drv_target_htmode2width(rstate->ht_mode);
                /* FIXME: derive center_freq0_mhz */
            }
            osw_drv_target_schema2wpa(vstate, &info->u.ap.wpa);
            if (vstate->mac_list_len > 0) {
                int i;
                info->u.ap.acl.count = vstate->mac_list_len;
                info->u.ap.acl.list = CALLOC(vstate->mac_list_len, sizeof(*info->u.ap.acl.list));
                for (i = 0; i < vstate->mac_list_len; i++) {
                    sscanf(vstate->mac_list[i], "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                           &info->u.ap.acl.list[i].octet[0],
                           &info->u.ap.acl.list[i].octet[1],
                           &info->u.ap.acl.list[i].octet[2],
                           &info->u.ap.acl.list[i].octet[3],
                           &info->u.ap.acl.list[i].octet[4],
                           &info->u.ap.acl.list[i].octet[5]);
                }
            }
            if (vstate->wpa_psks_len > 0) {
                struct osw_ap_psk_list *psks = &info->u.ap.psk_list;
                int i;
                psks->count = vstate->wpa_psks_len;
                psks->list = CALLOC(psks->count, sizeof(*psks->list));
                for (i = 0; i < vstate->wpa_psks_len; i++) {
                    const char *key_id = vstate->wpa_psks_keys[i];
                    psks->list[i].key_id = osw_drv_target_str2keyid(key_id);
                    STRSCPY_WARN(psks->list[i].psk.str, vstate->wpa_psks[i]);
                }
            }
            /* FIXME: use schema_consts */

            // radius_list
            #if 0
            if (strcmp(rstate->min_hw_mode, "11n") == 0) {
                info->u.ap.mode.ht_required = true;
            else if (strcmp(rstate->min_hw_mode, "11ac") == 0) {
                info->u.ap.mode.ht_required = true;
                info->u.ap.mode.vht_required = true;
            }
            #endif
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            {
                int band_num = osw_drv_target_band2num(rstate->freq_band);
                vsta->link.channel.control_freq_mhz = osw_drv_target_ch2freq(band_num, vstate->channel);
                vsta->link.channel.width = osw_drv_target_htmode2width(rstate->ht_mode);
            }
            STRSCPY_WARN(vsta->link.ssid.buf, vstate->ssid);
            STRSCPY_WARN(vsta->link.psk.str, vstate->wpa_psks[0]);
            vsta->link.ssid.len = strlen(vstate->ssid);
            vsta->link.status = strlen(vstate->parent) > 0
                              ? OSW_DRV_VIF_STATE_STA_LINK_CONNECTED
                              : OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED;
            memset(&vsta->link.bssid, 0, sizeof(vsta->link.bssid));
            osw_hwaddr_from_cstr(vstate->parent, &vsta->link.bssid);
            osw_drv_target_schema2wpa(vstate, &vsta->link.wpa);
            break;
    }
}

static void
osw_drv_target_nets2vifstate(const char *vif_name,
                             struct ds_dlist *nets,
                             struct osw_drv_vif_state *state)
{
    struct osw_drv_vif_state_sta *vsta = &state->u.sta;
    struct osw_drv_vif_sta_network **tail = &vsta->network;
    struct osw_drv_target_net *i;
    size_t cnt = 0;

    switch (state->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            ds_dlist_foreach(nets, i) {
                struct osw_drv_vif_sta_network *n = CALLOC(1, sizeof(*n));
                n->bssid = i->bssid;
                n->ssid = i->ssid;
                n->psk = i->psk;
                n->wpa = i->wpa;
                *tail = n;
                tail = &n->next;
                cnt++;
            }
            LOGI("osw: drv: target: %s: reporting %zu nets", vif_name, cnt);
            break;
    }
}

static void
osw_drv_target_vif_get_neighs(const char *vif_name,
                              enum osw_vif_type vif_type,
                              struct osw_neigh_list *neigh_list)
{
    assert(vif_name != NULL);

    unsigned int bsal_neigh_cnt = 0;
    const unsigned int max_bsal_neigh_cnt = 64;
    bsal_neigh_info_t *bsal_neigh_list;

    if (vif_type != OSW_VIF_AP) {
        LOGD("osw: drv: target: %s: vif is not an ap, skip getting neighbors", vif_name);
        return;
    }

    LOGD("osw: drv: target: %s: getting vif neighbor list", vif_name);

    bsal_neigh_list = CALLOC(max_bsal_neigh_cnt, sizeof(*bsal_neigh_list));
    const bool res = target_bsal_rrm_get_neighbors(vif_name,
                                                   bsal_neigh_list,
                                                   &bsal_neigh_cnt,
                                                   max_bsal_neigh_cnt);

    if (WARN_ON(res != 0) ||
        WARN_ON(bsal_neigh_list == NULL)) {
        goto free_bsal_neighs;
    }

    if (neigh_list->list == NULL) {
        neigh_list->list = CALLOC(bsal_neigh_cnt, sizeof(*neigh_list->list));
    }
    else {
        REALLOC(neigh_list->list, bsal_neigh_cnt * sizeof(*neigh_list->list));
    }
    neigh_list->count = 0;

    unsigned int i;
    for (i = 0; i < bsal_neigh_cnt; i++) {
        struct osw_neigh *neigh = &neigh_list->list[i];
        bsal_neigh_info_t *bsal_neigh = &bsal_neigh_list[i];

        memcpy(neigh->bssid.octet, bsal_neigh->bssid, OSW_HWADDR_LEN);
        neigh->bssid_info = bsal_neigh->bssid_info;
        neigh->op_class = bsal_neigh->op_class;
        neigh->channel = bsal_neigh->channel;
        neigh->phy_type = bsal_neigh->phy_type;
        neigh_list->count++;

        LOGT("osw: drv: target: %s: retrieved vif neighbor,"
             " bssid: "OSW_HWADDR_FMT
             " bssid_info: %08x"
             " op_class: %u"
             " channel: %u"
             " phy_type: %u",
             vif_name,
             OSW_HWADDR_ARG(&neigh->bssid),
             neigh->bssid_info,
             neigh->op_class,
             neigh->channel,
             neigh->phy_type);
    }

free_bsal_neighs:
    FREE(bsal_neigh_list);
}

struct osw_drv_target_phy *
osw_drv_target_phy_get(struct osw_drv_target *target,
                       const char *phy_name)
{
    struct osw_drv_target_phy *phy = ds_tree_find(&target->phy_tree, phy_name);
    if (phy == NULL) {
        phy = CALLOC(1, sizeof(*phy));
        SCHEMA_SET_STR(phy->rstate.if_name, phy_name);
        SCHEMA_SET_STR(phy->config.if_name, phy_name);
        ds_tree_insert(&target->phy_tree, phy, phy->rstate.if_name);
    }
    return phy;
}

void
osw_drv_target_phy_set_hw_type(struct osw_drv_target *target,
                               const char *phy_name,
                               const char *hw_type)
{
    struct osw_drv_target_phy *phy = osw_drv_target_phy_get(target, phy_name);
    if (phy == NULL) return;
    SCHEMA_SET_STR(phy->config.hw_type, hw_type);
}

void
osw_drv_target_phy_set_hw_config(struct osw_drv_target *target,
                                 const char *phy_name,
                                 const char **hw_config_keys,
                                 const char **hw_config_values,
                                 size_t n_hw_config)
{
    struct osw_drv_target_phy *phy = osw_drv_target_phy_get(target, phy_name);
    if (phy == NULL) return;
    phy->config.hw_config_len = 0;

    size_t i;
    for (i = 0; i < n_hw_config; i++) {
        const char *k = hw_config_keys[i];
        const char *v = hw_config_values[i];
        SCHEMA_KEY_VAL_APPEND(phy->config.hw_config, k, v);
    }
}

static void
osw_drv_target_phy_fill_base(struct osw_drv_target *target,
                             const char *phy_name,
                             struct schema_Wifi_Radio_Config *base)
{
    if (osw_drv_target_ovsdb_enabled()) {
        ovsdb_table_select_one(&table_Wifi_Radio_Config,
                               SCHEMA_COLUMN(Wifi_Radio_Config, if_name),
                               phy_name,
                               base);
    }
    else {
        struct osw_drv_target_phy *phy = ds_tree_find(&target->phy_tree, phy_name);
        if (phy != NULL) {
            *base = phy->config;
        }
    }
}

static void
osw_drv_target_phy_set(struct osw_drv_target *priv,
                       struct osw_drv_phy_config *phy)
{
    struct schema_Wifi_Radio_Config base_rconf = {0};
    struct schema_Wifi_Radio_Config rconf = {0};
    struct schema_Wifi_Radio_Config_flags rchanged = {0};
    const struct osw_state_phy_info *info = osw_state_phy_lookup(phy->phy_name);

    osw_drv_target_phy_fill_base(priv, info->phy_name, &base_rconf);
    osw_drv_target_phyconf2schema(phy, info, &base_rconf, &rconf, &rchanged);
    target_radio_config_set2(&rconf, &rchanged);
}

static void// forward decl
osw_drv_target_vif_set_nets(struct osw_drv_target *target,
                            const char *vif_name,
                            const struct schema_Wifi_VIF_Config *vconf,
                            const struct schema_Wifi_Credential_Config *c,
                            int n);

static void
osw_drv_target_vif_fill_base(struct osw_drv_target *target,
                             const char *vif_name,
                             struct schema_Wifi_VIF_Config *base)
{
    if (osw_drv_target_ovsdb_enabled()) {
        ovsdb_table_select_one(&table_Wifi_VIF_Config,
                               SCHEMA_COLUMN(Wifi_VIF_Config, if_name),
                               vif_name,
                               base);
    }
    else {
        struct osw_drv_target_vif *vif = ds_tree_find(&target->vif_tree, vif_name);
        if (vif != NULL) {
            *base = vif->config;
        }
    }
}

static void
osw_drv_target_vif_set_neighs(const char *vif_name,
                              enum osw_vif_type vif_type,
                              const struct osw_neigh_list *neigh_list,
                              const bool neigh_list_changed)
{
    assert(neigh_list != NULL);

    if (vif_type != OSW_VIF_AP) {
        LOGT("osw: drv: target: %s: vif is not ap, skip setting neighbors", vif_name);
        return;
    }
    if (neigh_list_changed == false) {
        LOGT("osw: drv: target: %s: neighbor list did not change, skip setting neighbors", vif_name);
        return;
    }

    LOGD("osw: drv: target: %s: setting neighbors", vif_name);

    /* remove neighbors */
    struct osw_neigh_list state_neigh_list;
    MEMZERO(state_neigh_list);
    osw_drv_target_vif_get_neighs(vif_name,
                                  vif_type,
                                  &state_neigh_list);

    size_t i;
    for (i = 0; i < state_neigh_list.count; i++) {
        const struct osw_neigh *state_neigh = &state_neigh_list.list[i];
        bsal_neigh_info_t bsal_neigh;
        MEMZERO(bsal_neigh);
        memcpy(bsal_neigh.bssid, state_neigh->bssid.octet, BSAL_MAC_ADDR_LEN);

        LOGD("osw: drv: target: %s: removing neighbor "OSW_HWADDR_FMT,
             vif_name,
             OSW_HWADDR_ARG(&state_neigh->bssid));

        target_bsal_rrm_remove_neighbor(vif_name, &bsal_neigh);
    }
    FREE(state_neigh_list.list);

    /* add neighbors */
    for (i = 0; i < neigh_list->count; i++) {
        const struct osw_neigh *neigh = &neigh_list->list[i];

        bsal_neigh_info_t bsal_neigh;
        MEMZERO(bsal_neigh);
        memcpy(bsal_neigh.bssid, neigh->bssid.octet, BSAL_MAC_ADDR_LEN);
        bsal_neigh.bssid_info = neigh->bssid_info;
        bsal_neigh.op_class = neigh->op_class;
        bsal_neigh.channel = neigh->channel;
        bsal_neigh.phy_type = neigh->phy_type;
        bsal_neigh.opt_subelems_len = 0;

        LOGD("osw: drv: target: %s: setting neighbor"
             " bssid: "OSW_HWADDR_FMT
             " bssid_info: %08x"
             " op_class: %u"
             " channel: %u"
             " phy_type: %u",
             vif_name,
             OSW_HWADDR_ARG(&neigh->bssid),
             bsal_neigh.bssid_info,
             bsal_neigh.op_class,
             bsal_neigh.channel,
             bsal_neigh.phy_type);

        target_bsal_rrm_set_neighbor(vif_name, &bsal_neigh);
    }
}

static void
osw_drv_target_vif_set(struct osw_drv_target *priv,
                       struct osw_drv_phy_config *phy,
                       struct osw_drv_vif_config *vif)
{
    struct schema_Wifi_VIF_Config base_vconf = {0};
    struct schema_Wifi_VIF_Config vconf = {0};
    struct schema_Wifi_VIF_Config_flags vchanged = {0};
    struct schema_Wifi_Radio_Config base_rconf = {0};
    struct schema_Wifi_Radio_Config rconf = {0};
    struct schema_Wifi_Radio_Config_flags rchanged = {0};
    struct schema_Wifi_Credential_Config cconfs[8] = {0};
    int n_cconfs = ARRAY_SIZE(cconfs);
    const struct osw_state_phy_info *info = osw_state_phy_lookup(phy->phy_name);
    const char *vif_name = vif->vif_name;

    osw_drv_target_phy_fill_base(priv, phy->phy_name, &base_rconf);
    osw_drv_target_vif_fill_base(priv, vif->vif_name, &base_vconf);
    osw_drv_target_phyconf2schema(phy, info, &base_rconf, &rconf, &rchanged);
    osw_drv_target_vifconf2schema(vif, &base_vconf, &vconf, &vchanged, cconfs, &n_cconfs);

    const bool ok = target_vif_config_set2(&vconf, &rconf, cconfs, &vchanged, n_cconfs);
    if (ok == true) {
        osw_drv_target_vif_set_nets(priv, vif_name, &vconf, cconfs, n_cconfs);
        osw_drv_target_vif_set_neighs(vif_name,
                                      vif->vif_type,
                                      &vif->u.ap.neigh_list,
                                      vif->u.ap.neigh_list_changed);
    }
}

static const char *
osw_drv_target_vconf_lookup_phy(const ovs_uuid_t *uuid)
{
    ovsdb_cache_row_t *row;
    int i;

    ds_tree_foreach(&table_Wifi_Radio_Config.rows, row) {
        const struct schema_Wifi_Radio_Config *rconf = (const void *)row->record;
        for (i = 0; i < rconf->vif_configs_len; i++) {
            if (strcmp(rconf->vif_configs[i].uuid, uuid->uuid) == 0)
                return rconf->if_name;
        }
    }

    return NULL;
}

static bool
osw_drv_target_vif_get_exists(const struct osw_drv_target_vif *vif)
{
    return vif->exists == true || vif->state.enabled == true;
}

static const char *
osw_drv_target_vif_get_phy_name(const struct osw_drv_target_vif *vif)
{
    if (vif->phy_name != NULL) return vif->phy_name;
    return osw_drv_target_vconf_lookup_phy(&vif->config._uuid);
}

static void
osw_drv_target_vif_free_nets(struct osw_drv_target_vif *vif)
{
    struct osw_drv_target_net *n;
    while ((n = ds_dlist_remove_head(&vif->nets)) != NULL)
        FREE(n);
}

static void
osw_drv_target_vif_gc(struct osw_drv_target_vif *vif)
{
    struct ds_tree *tree = &vif->target->vif_tree;

    if (osw_drv_target_vif_get_exists(vif) == true) return;

    osw_drv_target_vif_free_nets(vif);
    ds_tree_remove(tree, vif);
    FREE(vif->vif_name);
    FREE(vif->phy_name);
    FREE(vif);
}

static bool
osw_drv_target_vif_sync(struct osw_drv_target_vif *vif)
{
    struct osw_drv_dummy *dummy = &vif->target->dummy;
    struct ds_tree *phy_tree = &vif->target->phy_tree;
    const char *phy_name = osw_drv_target_vif_get_phy_name(vif);
    const char *vif_name = vif->vif_name;
    struct schema_Wifi_Radio_State rzero = {0};
    const struct schema_Wifi_Radio_State *rstate = &rzero;
    struct osw_drv_vif_state state = {0};
    const bool exists = osw_drv_target_vif_get_exists(vif);

    if (phy_name == NULL && exists == false)
        return true;

    if (phy_name == NULL)
        return false;

    struct osw_drv_target_phy *phy = ds_tree_find(phy_tree, phy_name);
    if (phy != NULL)
        rstate = &phy->rstate;

    if (strlen(vif->state.mode) == 0)
        SCHEMA_CPY_STR(vif->state.mode, vif->config.mode);

    osw_drv_target_schema2vifstate(&vif->state, rstate, &state);
    osw_drv_target_nets2vifstate(vif_name, &vif->nets, &state);
    osw_drv_target_vif_get_neighs(vif_name,
                                  state.vif_type,
                                  &state.u.ap.neigh_list);
    state.exists = exists;

    if (state.exists == true)
        osw_drv_dummy_set_vif(dummy, phy_name, vif_name, &state);
    else
        osw_drv_dummy_set_vif(dummy, phy_name, vif_name, NULL);

    return true;
}

static void
osw_drv_target_vif_work_cb(EV_P_ ev_timer *arg, int events)
{
    struct osw_drv_target_vif *vif = container_of(arg, struct osw_drv_target_vif, work);
    if (osw_drv_target_vif_sync(vif) == false) return;
    ev_timer_stop(EV_A_ arg);
    osw_drv_target_vif_gc(vif);
}

static struct osw_drv_target_vif *
osw_drv_target_vif_get(struct osw_drv_target *target,
                       const char *vif_name)
{
    struct ds_tree *tree = &target->vif_tree;
    struct osw_drv_target_vif *vif = ds_tree_find(tree, vif_name);
    if (vif == NULL) {
        vif = CALLOC(1, sizeof(*vif));
        vif->target = target;
        vif->vif_name = STRDUP(vif_name);
        ev_timer_init(&vif->work, osw_drv_target_vif_work_cb, 0, 0);
        ds_dlist_init(&vif->nets, struct osw_drv_target_net, node);
        ds_tree_insert(tree, vif, vif->vif_name);
    }
    return vif;
}

static void
osw_drv_target_vif_sched(struct osw_drv_target_vif *vif)
{
    ev_timer_stop(EV_DEFAULT_ &vif->work);
    ev_timer_set(&vif->work, 0, 5);
    ev_timer_start(EV_DEFAULT_ &vif->work);
}

static void
osw_drv_target_vif_set_uuid(struct osw_drv_target *target,
                            const char *vif_name,
                            const char* uuid)
{
    struct osw_drv_target_vif *vif = osw_drv_target_vif_get(target, vif_name);
    STRSCPY_WARN(vif->config._uuid.uuid, uuid);
    osw_drv_target_vif_sched(vif);
}

void
osw_drv_target_vif_set_mode(struct osw_drv_target *target,
                            const char *vif_name,
                            const char *mode)
{
    struct osw_drv_target_vif *vif = osw_drv_target_vif_get(target, vif_name);
    SCHEMA_SET_STR(vif->config.mode, mode);
    osw_drv_target_vif_sched(vif);
}

void
osw_drv_target_vif_set_idx(struct osw_drv_target *target,
                           const char *vif_name,
                           int vif_radio_idx)
{
    struct osw_drv_target_vif *vif = osw_drv_target_vif_get(target, vif_name);
    SCHEMA_SET_INT(vif->config.vif_radio_idx, vif_radio_idx);
    osw_drv_target_vif_sched(vif);
}

static void
osw_drv_target_vif_set_state(struct osw_drv_target *target,
                             const char *vif_name,
                             const struct schema_Wifi_VIF_State *state)
{
    struct osw_drv_target_vif *vif = osw_drv_target_vif_get(target, vif_name);
    vif->state = *state;
    schema_vstate_unify(&vif->state);
    osw_drv_target_vif_sched(vif);
}

void
osw_drv_target_vif_set_phy(struct osw_drv_target *target,
                           const char *vif_name,
                           const char *phy_name)
{
    struct osw_drv_target_vif *vif = osw_drv_target_vif_get(target, vif_name);
    FREE(vif->phy_name);
    vif->phy_name = STRDUP(phy_name);
    osw_drv_target_vif_sched(vif);
}

void
osw_drv_target_vif_set_exists(struct osw_drv_target *target,
                              const char *vif_name,
                              bool exists)
{
    struct osw_drv_target_vif *vif = osw_drv_target_vif_get(target, vif_name);
    vif->exists = exists;
    osw_drv_target_vif_sched(vif);
}

static void
osw_drv_target_vif_set_nets(struct osw_drv_target *target,
                            const char *vif_name,
                            const struct schema_Wifi_VIF_Config *vconf,
                            const struct schema_Wifi_Credential_Config *c,
                            int n)
{
    struct osw_drv_target_vif *vif = osw_drv_target_vif_get(target, vif_name);

    LOGI("osw: drv: target: %s: setting nets (%d)", vif_name, n);

    osw_drv_target_vif_free_nets(vif);

    if (n == 0) {
        struct schema_Wifi_VIF_State vstate = {0};
        struct osw_drv_target_net *net = CALLOC(1, sizeof(*net));
        const char *parent = strlen(vconf->parent) > 0
                           ? vconf->parent
                           : "00:00:00:00:00:00";
        const char *psk = vconf->wpa_psks[0];
        const char *ssid = vconf->ssid;
        const size_t ssid_len = strlen(ssid);

        /* FIXME */
        osw_drv_target_vconf2vstate(vconf, &vstate);
        osw_drv_target_schema2wpa(&vstate, &net->wpa);

        osw_hwaddr_from_cstr(parent, &net->bssid);
        STRSCPY_WARN(net->psk.str, psk);
        STRSCPY_WARN(net->ssid.buf, ssid);
        net->ssid.len = ssid_len;

        ds_dlist_insert_tail(&vif->nets, net);
    }

    for (; n--; c++) {
        struct osw_drv_target_net *net = CALLOC(1, sizeof(*net));

        STRSCPY_WARN(net->ssid.buf, c->ssid);
        net->ssid.len = strlen(c->ssid);

        const char *psk = SCHEMA_KEY_VAL(c->security, "key");
        STRSCPY_WARN(net->psk.str, psk);

        net->wpa.rsn = true;
        net->wpa.akm_psk = true;
        net->wpa.pairwise_ccmp = true;

        ds_dlist_insert_tail(&vif->nets, net);
    }

    osw_drv_target_vif_sched(vif);
}

static void
osw_drv_target_stats_set_enabled(struct osw_drv_target *target,
                                 const struct schema_Wifi_VIF_State *vstate,
                                 const char *phy_name)
{
    radio_entry_t cfg;
    MEMZERO(cfg);

    const char *vif_name = vstate->if_name;
    struct osw_drv_dummy *dummy = &target->dummy;
    struct osw_drv_dummy_phy *dummy_phy = ds_tree_find(&dummy->phy_tree, phy_name);
    if (dummy_phy == NULL) return;

    cfg.type = phy_to_type(&dummy_phy->state);
    STRSCPY_WARN(cfg.phy_name, phy_name);
    STRSCPY_WARN(cfg.if_name, vstate->if_name);

    struct ds_tree *vif_tree = &target->vif_tree;
    struct osw_drv_target_vif *target_vif = ds_tree_find(vif_tree, vif_name);

    const bool new_vif = (target_vif == NULL)
                      || (target_vif->state.enabled == false &&
                          vstate->enabled == true);
    const bool not_a_new_vif = !new_vif;

    /* There's no way to know if stats are already enabled.
     * And even then, the driver doesn't properly notify
     * about that in some builds so relying on that is not a
     * good idea.
     *
     * The driver can reset stats settings when last VIF is
     * destroyed. As such try to enable stats whenever any
     * VIF is enabled -- including first VIF per PHY -- to
     * make sure stats are always enabled.
     *
     * This avoids hammerring the driver with enabling
     * requests whenever any VIF attribute changes. This is
     * adapted to mimic SM's behaviour.
     */
    if (not_a_new_vif) return;

    LOGD("osw: drv: target: %s: enabling stats due to %s bringup",
         phy_name,
         vif_name);

    const bool enabled_ok = target_radio_tx_stats_enable(&cfg, true);
    const bool fastscan_ok = target_radio_fast_scan_enable(&cfg, (char *)vif_name);

    WARN_ON(enabled_ok == false);
    WARN_ON(fastscan_ok == false);
}

static void
osw_drv_target_op_vstate_cb(const struct schema_Wifi_VIF_State *vstate,
                            const char *phy)
{
    struct osw_drv_target *target = &g_osw_drv_target;
    LOGI("osw: drv: target: vstate report: %s: %s", phy ?: "?", vstate->if_name);
    if (phy != NULL && strlen(phy) > 0) {
        osw_drv_target_stats_set_enabled(target, vstate, phy);
        osw_drv_target_vif_set_phy(target, vstate->if_name, phy);
    }
    osw_drv_target_vif_set_state(target, vstate->if_name, vstate);
}

static void
osw_drv_target_op_rstate_cb(const struct schema_Wifi_Radio_State *rstate)
{
    struct osw_drv_target *target = &g_osw_drv_target;
    struct osw_drv_dummy *dummy = &target->dummy;
    struct osw_drv_phy_state info = {0};
    struct osw_drv_target_phy *tphy = ds_tree_find(&target->phy_tree, rstate->if_name);
    struct osw_drv_target_vif *tvif;

    LOGI("osw: drv: target: rstate report: %s", rstate->if_name);
    osw_drv_target_schema2phystate(rstate, &info);

    if (info.exists == true) {
        if (tphy == NULL) {
            tphy = CALLOC(1, sizeof(*tphy));
            tphy->rstate = *rstate;
            ds_tree_insert(&target->phy_tree, tphy, tphy->rstate.if_name);
        }
        else {
            tphy->rstate = *rstate;
        }
        osw_drv_dummy_set_phy(dummy, rstate->if_name, &info);
    }
    else {
        if (tphy != NULL) {
            ds_tree_remove(&target->phy_tree, tphy);
            FREE(tphy);
            tphy = NULL;
        }
        osw_drv_dummy_set_phy(dummy, rstate->if_name, NULL);
    }

    /* Some rstate values are used to derive osw_drv_vif_state. Therefore any
     * change to rstate needs to generate a vif state re-report. The system
     * will debounce it and won't generate any actions if the state doesn't
     * deviate from the desired configuration though.
     */
    ds_tree_foreach(&target->vif_tree, tvif) {
        if (strlen(tvif->state.if_name) == 0) continue;

        const char *phy_name = osw_drv_target_vif_get_phy_name(tvif);
        if (phy_name == NULL) continue;
        if (strcmp(phy_name, rstate->if_name) != 0) continue;

        osw_drv_target_op_vstate_cb(&tvif->state, rstate->if_name);
    }
}

static void
osw_drv_target_op_client_cb(const struct schema_Wifi_Associated_Clients *client,
                            const char *vif,
                            bool associated)
{
    struct osw_drv_target *target = &g_osw_drv_target;
    struct osw_drv_dummy *dummy = &target->dummy;
    const char *phy_name = NULL;
    struct osw_drv_sta_state info = {0};
    struct osw_hwaddr addr;

    info.key_id = osw_drv_target_str2keyid(client->key_id);
    info.connected = associated;

    sscanf(client->mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
            &addr.octet[0],
            &addr.octet[1],
            &addr.octet[2],
            &addr.octet[3],
            &addr.octet[4],
            &addr.octet[5]);

    osw_drv_dummy_set_sta(dummy, NULL, vif, &addr, &info);

    {
        bsal_client_info_t info = {0};
        int err = target_bsal_client_info(vif, addr.octet, &info);
        if (err == 0) {
            osw_drv_dummy_set_sta_ies(dummy, phy_name, vif, &addr,
                                      info.assoc_ies,
                                      info.assoc_ies_len);
        }
    }

    LOGI("osw: drv: target: %s: %s: %sconnected: key_id=%s/%d",
         vif, client->mac, associated ? "" : "dis",
         client->key_id, info.key_id);
}

static void
osw_drv_target_flush_sta_iter_cb(struct osw_drv_dummy *dummy,
                                 const char *phy_name,
                                 const char *vif_name,
                                 const struct osw_hwaddr *sta_addr,
                                 void *fn_data)
{
    const char *vif = fn_data;
    if (strcmp(vif_name, vif) != 0) return;
    osw_drv_dummy_set_sta(dummy, phy_name, vif_name, sta_addr, NULL);
}

static void
osw_drv_target_flush_sta(const char *vif)
{
    struct osw_drv_target *target = &g_osw_drv_target;
    struct osw_drv_dummy *dummy = &target->dummy;
    char *vifp = STRDUP(vif);

    osw_drv_dummy_iter_sta(dummy, osw_drv_target_flush_sta_iter_cb, vifp);
    FREE(vifp);
}

static void
osw_drv_target_op_clients_cb(const struct schema_Wifi_Associated_Clients *clients,
                             int num,
                             const char *vif)
{
    osw_drv_target_flush_sta(vif);
    while (num--) {
        osw_drv_target_op_client_cb(clients, vif, true);
        clients++;
    }
}

static void
osw_drv_target_op_flush_clients_cb(const char *vif)
{
    osw_drv_target_flush_sta(vif);
}

static void
osw_drv_target_op_csa_rx_cb(const char *phy_name,
                            const char *vif_name,
                            int chan_pri_freq_mhz,
                            int chan_width_mhz)
{
    struct osw_drv_target *target = &g_osw_drv_target;
    struct osw_drv_dummy *dummy = &target->dummy;
    const struct osw_channel channel = {
        .control_freq_mhz = chan_pri_freq_mhz,
        .width = chan_width_mhz == 20 ? OSW_CHANNEL_20MHZ
            : chan_width_mhz == 40 ? OSW_CHANNEL_40MHZ
            : chan_width_mhz == 80 ? OSW_CHANNEL_80MHZ
            : chan_width_mhz == 160 ? OSW_CHANNEL_160MHZ
            : chan_width_mhz == 320 ? OSW_CHANNEL_320MHZ
            : OSW_CHANNEL_20MHZ,
    };

    osw_drv_report_vif_channel_change_advertised(dummy->drv,
                                                 phy_name,
                                                 vif_name,
                                                 &channel);
}

static const struct target_radio_ops g_rops = {
    .op_vstate = osw_drv_target_op_vstate_cb,
    .op_rstate = osw_drv_target_op_rstate_cb,
    .op_client = osw_drv_target_op_client_cb,
    .op_clients = osw_drv_target_op_clients_cb,
    .op_flush_clients = osw_drv_target_op_flush_clients_cb,
    .op_csa_rx = osw_drv_target_op_csa_rx_cb,
};

void
osw_drv_target_bsal_vap_register(struct osw_drv_target *target,
                                 const char *vif_name)
{
    assert(target != NULL);
    assert(vif_name != NULL);

    struct osw_drv_target_bsal_vap *vap = CALLOC(1, sizeof(*vap));
    //struct osw_drv_target_bsal_ue *ue;

    STRSCPY(vap->cfg.ifname, vif_name);
    vap->cfg.inact_check_sec = 10;
    vap->cfg.inact_tmout_sec_normal = 60;
    vap->cfg.inact_tmout_sec_overload = 30;
    if (target_bsal_iface_add(&vap->cfg) != 0) {
        LOGW("osw: drv: target: failed to register vif: %s in basl", vif_name);
        goto error;
    }

    ds_tree_insert(&target->bsal_vap_tree, vap, vap->cfg.ifname);
    LOGI("osw: drv: target: registered vif: %s in bsal", vif_name);

    /*ds_tree_foreach(&target->bsal_ue_tree, ue) {
        if (target_bsal_client_add(vap->cfg.ifname, (const uint8_t*)&ue->mac_addr.octet, &ue->cfg) == 0) {
            LOGI("osw: drv: target: registered client: "OSW_HWADDR_FMT" at vif: %s in bsal",
                 OSW_HWADDR_ARG(&ue->mac_addr), vap->cfg.ifname);
        }
        else {
            LOGW("osw: drv: target: failed to register client: "OSW_HWADDR_FMT" at vif: %s in bsal",
                 OSW_HWADDR_ARG(&ue->mac_addr), vap->cfg.ifname);
        }
    }*/

    return;

error:
    FREE(vap);
}

void
osw_drv_target_bsal_vap_unregister(struct osw_drv_target *target,
                                   const char *vif_name)
{
    assert(target != NULL);
    assert(vif_name != NULL);

    struct osw_drv_target_bsal_vap *vap;
    //struct osw_drv_target_bsal_ue *ue;

    vap = ds_tree_find(&target->bsal_vap_tree, vif_name);
    if (vap == NULL) {
        LOGW("osw: drv: target: failed to find vif: %s in regitered bsal's vap tree", vif_name);
        return;
    }

    ds_tree_remove(&target->bsal_vap_tree, vap);
    if (target_bsal_iface_remove(&vap->cfg) == 0)
        LOGI("osw: drv: target: unregistered vif: %s from bsal", vif_name);
    else
        LOGW("osw: drv: target: failed to unregister vif: %s from basl", vif_name);

    /*ds_tree_foreach(&target->bsal_ue_tree, ue) {
        if (target_bsal_client_remove(vap->cfg.ifname, (const uint8_t*)&ue->mac_addr.octet) == 0) {
            LOGI("osw: drv: target: unregistered client: "OSW_HWADDR_FMT" from vif: %s from bsal",
                 OSW_HWADDR_ARG(&ue->mac_addr), vap->cfg.ifname);
        }
        else {
            LOGW("osw: drv: target: failed to unregister client: "OSW_HWADDR_FMT" from vif: %s from bsal",
                 OSW_HWADDR_ARG(&ue->mac_addr), vap->cfg.ifname);
        }
    }*/

    FREE(vap);
}

void
osw_drv_target_bsal_ue_register(struct osw_drv_target *target,
                                const struct osw_hwaddr *mac_addr)
{
    assert(target != NULL);
    assert(mac_addr != NULL);

    struct osw_drv_target_bsal_ue *ue = CALLOC(1, sizeof(*ue));
    //struct osw_drv_target_bsal_vap *vap;

    memcpy(&ue->mac_addr, mac_addr, sizeof(ue->mac_addr));

    /*ds_tree_foreach(&target->bsal_vap_tree, vap) {
        if (target_bsal_client_add(vap->cfg.ifname, (const uint8_t*)&ue->mac_addr.octet, &ue->cfg) == 0) {
            LOGI("osw: drv: target: registered client: "OSW_HWADDR_FMT" at vif: %s in bsal",
                 OSW_HWADDR_ARG(&ue->mac_addr), vap->cfg.ifname);
        }
        else {
            LOGW("osw: drv: target: failed to register client: "OSW_HWADDR_FMT" at vif: %s in bsal",
                 OSW_HWADDR_ARG(&ue->mac_addr), vap->cfg.ifname);
            goto error;
        }
    }*/

    ds_tree_insert(&target->bsal_ue_tree, ue, &ue->mac_addr);

    return;

error:
    FREE(ue);
}

void
osw_drv_target_bsal_ue_unregister(struct osw_drv_target *target,
                                  const struct osw_hwaddr *mac_addr)
{
    assert(target != NULL);
    assert(mac_addr != NULL);

    struct osw_drv_target_bsal_ue *ue;
    //struct osw_drv_target_bsal_vap *vap;

    ue = ds_tree_find(&target->bsal_ue_tree, mac_addr);
    if (ue == NULL) {
        LOGW("osw: drv: target: failed to find client: "OSW_HWADDR_FMT" in regitered bsal's ue tree",
             OSW_HWADDR_ARG(mac_addr));
        return;
    }

    /*ds_tree_foreach(&target->bsal_vap_tree, vap) {
        if (target_bsal_client_remove(vap->cfg.ifname, (const uint8_t*)&ue->mac_addr.octet) == 0) {
            LOGI("osw: drv: target: unregistered client: "OSW_HWADDR_FMT" from vif: %s from bsal",
                 OSW_HWADDR_ARG(&ue->mac_addr), vap->cfg.ifname);
        }
        else {
            LOGW("osw: drv: target: failed to unregister client: "OSW_HWADDR_FMT" from vif: %s from bsal",
                 OSW_HWADDR_ARG(&ue->mac_addr), vap->cfg.ifname);
        }
    }*/

    ds_tree_remove(&target->bsal_ue_tree, ue);

    return;

error:
    FREE(ue);
}

static void
callback_Wifi_Radio_Config(ovsdb_update_monitor_t *mon,
                           struct schema_Wifi_Radio_Config *old,
                           struct schema_Wifi_Radio_Config *rconf,
                           ovsdb_cache_row_t *row)
{
}

static void
callback_Wifi_VIF_Config(ovsdb_update_monitor_t *mon,
                         struct schema_Wifi_VIF_Config *old,
                         struct schema_Wifi_VIF_Config *vconf,
                         ovsdb_cache_row_t *row)
{
    struct osw_drv_target *target = &g_osw_drv_target;

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            osw_drv_target_vif_set_uuid(target, vconf->if_name, vconf->_uuid.uuid);
            osw_drv_target_vif_set_mode(target, vconf->if_name, vconf->mode);
            osw_drv_target_vif_set_exists(target, vconf->if_name, true);
            break;
        case OVSDB_UPDATE_DEL:
            osw_drv_target_vif_set_exists(target, vconf->if_name, false);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void
callback_Band_Steering_Config(ovsdb_update_monitor_t *mon,
                              struct schema_Band_Steering_Config *old,
                              struct schema_Band_Steering_Config *steer_cfg,
                              ovsdb_cache_row_t *row)
{
    struct osw_drv_target *target = &g_osw_drv_target;

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            if (steer_cfg->ifnames_len > 0) {
                int i;
                for (i = 0; i < steer_cfg->ifnames_len; i++)
                    osw_drv_target_bsal_vap_register(target, steer_cfg->ifnames_keys[i]);
            }

            if (steer_cfg->if_name_2g_exists == true)
                osw_drv_target_bsal_vap_register(target, steer_cfg->if_name_5g);

            if (steer_cfg->if_name_5g_exists == true)
                osw_drv_target_bsal_vap_register(target, steer_cfg->if_name_2g);
            break;
        case OVSDB_UPDATE_MODIFY:
            break;
        case OVSDB_UPDATE_DEL:
            if (old->ifnames_len > 0) {
                int i;
                for (i = 0; i < old->ifnames_len; i++)
                    osw_drv_target_bsal_vap_unregister(target, old->ifnames_keys[i]);
            }

            if (old->if_name_2g_exists == true)
                osw_drv_target_bsal_vap_unregister(target, old->if_name_5g);

            if (old->if_name_5g_exists == true)
                osw_drv_target_bsal_vap_unregister(target, old->if_name_2g);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void
callback_Band_Steering_Clients(ovsdb_update_monitor_t *mon,
                               struct schema_Band_Steering_Clients *old,
                               struct schema_Band_Steering_Clients *steer_client,
                               ovsdb_cache_row_t *row)
{
    struct osw_drv_target *target = &g_osw_drv_target;
    struct osw_hwaddr mac_addr;

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
            osw_hwaddr_from_cstr(steer_client->mac, &mac_addr);
            osw_drv_target_bsal_ue_register(target, &mac_addr);
            break;
        case OVSDB_UPDATE_MODIFY:
            break;
        case OVSDB_UPDATE_DEL:
            osw_hwaddr_from_cstr(old->mac, &mac_addr);
            osw_drv_target_bsal_ue_unregister(target, &mac_addr);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void
osw_drv_target_conf_sched(struct osw_drv_target *target)
{
    ev_timer_stop(EV_DEFAULT_ &target->conf_timer);
    ev_timer_set(&target->conf_timer, 0, 0);
    ev_timer_start(EV_DEFAULT_ &target->conf_timer);
}

static void
osw_drv_target_conf_timer_cb(EV_P_ ev_timer *arg, int events)
{
    struct osw_drv_target *target = container_of(arg, struct osw_drv_target, conf_timer);
    struct osw_drv_conf *conf = target->conf;

    while (target->conf_phy < target->conf->n_phy_list) {
        struct osw_drv_phy_config *phy = &conf->phy_list[target->conf_phy];

        if (target->conf_phy_done == false) {
            size_t i;
            bool phy_changed = phy->changed;

            /* Target API configures some attributes per PHY, not per VIF. OSW
             * itself isn't aware.
             */
            for (i = 0; i < phy->vif_list.count; i++) {
                struct osw_drv_vif_config *vif = &phy->vif_list.list[i];
                if (vif->vif_type != OSW_VIF_AP) continue;
                phy_changed |= vif->u.ap.channel_changed;
                phy_changed |= vif->u.ap.beacon_interval_tu_changed;
            }

            if (phy_changed == true) {
                osw_drv_target_phy_set(target, phy);
                target->conf_phy_done = true;
                osw_drv_target_conf_sched(target);
                return;
            }
        }

        while (target->conf_vif < phy->vif_list.count) {
            struct osw_drv_vif_config *vif = &phy->vif_list.list[target->conf_vif];

            target->conf_vif++;
            if (vif->changed) {
                osw_drv_target_vif_set(target, phy, vif);
                osw_drv_target_conf_sched(target);
                return;
            }
        }

        target->conf_phy++;
        target->conf_vif = 0;
        target->conf_phy_done = false;
    }

    osw_drv_conf_free(conf);
    target->conf = NULL;
}

static void
osw_drv_target_ovsdb_retry_cb(EV_P_ ev_timer *t, int events)
{
    if (ovsdb_init_loop(EV_A_ NULL) == false) {
        LOGI("osw: drv: target: failed to connect to ovsdb, will retry later");
        return;
    }

    OVSDB_CACHE_MONITOR(Wifi_Radio_Config, true);
    OVSDB_CACHE_MONITOR(Wifi_VIF_Config, true);
    OVSDB_CACHE_MONITOR(Band_Steering_Config, true);
    OVSDB_CACHE_MONITOR(Band_Steering_Clients, true);

    ev_timer_stop(EV_A_ t);
    LOGI("osw: drv: target: ovsdb ready");
}

static void
osw_drv_target_bsal_handle_probe_req_event(const struct osw_drv_target_vif *vif,
                                           const bsal_ev_probe_req_t *event)
{
    assert(vif != NULL);
    assert(event != NULL);

    struct osw_drv_report_vif_probe_req probe_req;
    struct osw_drv_target *target = &g_osw_drv_target;
    struct osw_drv_dummy *dummy = &target->dummy;

    memset(&probe_req, 0, sizeof(probe_req));
    memcpy(&probe_req.sta_addr.octet, event->client_addr, sizeof(probe_req.sta_addr.octet));
    probe_req.snr = event->rssi;
    if (vif->state.ssid_exists == true && strlen(vif->state.ssid) > 0) {
        STRSCPY(probe_req.ssid.buf, vif->state.ssid);
        probe_req.ssid.len = strlen(vif->state.ssid) - 1;
    }

    /* This log is very noisy! */
    /*LOGD("osw: drv: target: client: "OSW_HWADDR_FMT" sent bsal probe req event snr: %u ssid: %s vif: %s",
         OSW_HWADDR_ARG(&probe_req.sta_addr), probe_req.snr, probe_req.ssid.len > 0 ? "present" : "(null)",
         vif->vif_name);*/

    if (probe_req.ssid.len == 0) {
        struct osw_drv_target_vif *entry;
        ds_tree_foreach(&target->vif_tree, entry)
            osw_drv_report_vif_probe_req(dummy->drv, vif->phy_name, vif->vif_name, &probe_req);
    }
    else {
        osw_drv_report_vif_probe_req(dummy->drv, vif->phy_name, vif->vif_name, &probe_req);
    }
}

static void
osw_drv_target_bsal_handle_action_frame_event(const struct osw_drv_target_vif *vif,
                                              const bsal_ev_action_frame_t *event)
{
    struct osw_drv_target *target = &g_osw_drv_target;
    struct osw_drv_dummy *dummy = &target->dummy;
    const struct osw_drv_dot11_frame *frame = (const void *)event->data;
    const void *out_of_bounds = event->data + event->data_len;
    const struct osw_drv_vif_frame_rx rx = {
        .data = event->data,
        .len = (size_t)event->data_len,
    };

    assert(event != NULL);
    assert(event->data != NULL);

    if (vif == NULL) {
        LOGD("osw: drv: target: recevied bsal event for unknown vif");
        return;
    }

    if ((const void *)&frame->u.action.category >= out_of_bounds) {
        LOGI("osw: drv: target: %s: action frame: too short", vif->vif_name);
        return;
    }

    switch (frame->u.action.category) {
        case DOT11_ACTION_CATEGORY_RADIO_MEAS_CODE:
            if ((const void *)&frame->u.action.u.rrm_meas_rep.action >= out_of_bounds) {
                LOGI("osw: drv: target: %s: action frame: rrm meas rep: too short", vif->vif_name);
                return;
            }

            switch (frame->u.action.u.rrm_meas_rep.action) {
                case DOT11_RRM_MEAS_REP_IE_ACTION_CODE:
                    LOGD("osw: drv: target: %s: recevied bsal event: RRM Report", vif->vif_name);
                    osw_drv_report_vif_frame_rx(dummy->drv,
                                                vif->phy_name,
                                                vif->vif_name,
                                                &rx);
                    break;
            }
            break;
        default:
            LOGD("osw: drv: target: recevied bsal event: unhandled event: 0x%02X", event->data[0]);
            break;
    }
}

static void
osw_drv_target_bsal_event_cb(bsal_event_t *event)
{
    assert(event != NULL);

    struct osw_drv_target *target = &g_osw_drv_target;
    struct osw_drv_target_vif *vif = ds_tree_find(&target->vif_tree, event->ifname);

    if (vif == NULL) {
        LOGD("osw: drv: target: recevied bsal event for unknown vif: %s", event->ifname);
        return;
    }

    if (vif->phy_name == NULL) {
        LOGD("osw: drv: target: recevied bsal event for vif: %s at unknown phy", event->ifname);
        return;
    }

    switch(event->type) {
        case BSAL_EVENT_PROBE_REQ:
            osw_drv_target_bsal_handle_probe_req_event(vif, &event->data.probe_req);
            break;
        case BSAL_EVENT_AUTH_FAIL:
            /* TODO */
            break;
        case BSAL_EVENT_ACTION_FRAME:
            osw_drv_target_bsal_handle_action_frame_event(vif, &event->data.action_frame);
            break;
        default:
            ; /* nop */
    }
}

void
osw_drv_target_init_cb(struct osw_drv *drv)
{
    struct osw_drv_target *target = &g_osw_drv_target;
    struct osw_drv_dummy *dummy = &target->dummy;

    if (target_init(TARGET_INIT_MGR_WM, EV_DEFAULT) == false) {
        LOGI("osw: drv: target: failed to initialize");
        return;
    }

    if (target_bsal_init(osw_drv_target_bsal_event_cb, EV_DEFAULT) != 0) {
        LOGI("osw: drv: target: failed to initialize bsal");
        return;
    }

    ev_timer_init(&target->conf_timer, osw_drv_target_conf_timer_cb, 0, 0);
    ev_timer_init(&target->ovsdb_retry, osw_drv_target_ovsdb_retry_cb, 0, 1);

    if (osw_drv_target_ovsdb_enabled()) {
        ev_timer_start(EV_DEFAULT_ &target->ovsdb_retry);
    }
    else {
        LOGI("osw: drv: target: ovsdb hooks disabled, expecting external help");
    }

    setenv("TARGET_DISABLE_FALLBACK_PARENTS", "1", 1);
    setenv("TARGET_DISABLE_ACL_ENFORCE", "1", 1);
    setenv("TARGET_DISABLE_ACL_MASKING", "1", 1);
    setenv("TARGET_DISABLE_OVSDB_POKING", "1", 1);

    target_radio_init(&g_rops);
    target->drv = drv;
    osw_drv_set_priv(drv, dummy);
}

void
osw_drv_target_request_config_cb(struct osw_drv *drv,
                                 struct osw_drv_conf *conf)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    struct osw_drv_target *target = container_of(dummy, struct osw_drv_target, dummy);

    osw_drv_conf_free(target->conf);
    target->conf = conf;
    target->conf_phy_done = false;
    target->conf_phy = 0;
    target->conf_vif = 0;

    osw_drv_target_conf_sched(target);

}

/*
FIXME: dummy doesnt support it yet
static void
osw_drv_target_request_sta_deauth_cb(struct osw_drv *drv,
                                     const char *phy_name,
                                     const char *vif_name,
                                     const struct osw_hwaddr *mac_addr,
                                     int dot11_reason_code)
{
}
*/

struct osw_drv_target *
osw_drv_target_get_ref(void)
{
    return &g_osw_drv_target;
}

OSW_MODULE(osw_drv_target)
{
    struct ev_loop *loop = OSW_MODULE_LOAD(osw_ev);
    struct osw_drv_target *target = &g_osw_drv_target;
    struct osw_drv_dummy *dummy = &target->dummy;

    if (osw_drv_target_enabled() == false) {
        LOGI("osw: drv: target: disabled");
        return NULL;
    }

    osw_drv_dummy_init(dummy);
    ds_dlist_init(&target->scans, struct osw_drv_target_scan, node);
    ds_tree_init(&target->surveys, ds_str_cmp, struct osw_drv_target_survey, node);
    target_init(TARGET_INIT_MGR_SM, loop);

    /* FIXME: This could use ovsdb_cache to avoid explicit
     * selects. It might be more efficient.
     */
    OVSDB_TABLE_INIT(Wifi_Radio_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Radio_State, if_name);
    OVSDB_TABLE_INIT(Wifi_VIF_Config, if_name);
    OVSDB_TABLE_INIT_NO_KEY(Band_Steering_Config);
    OVSDB_TABLE_INIT(Band_Steering_Clients, mac);

    return NULL;
}

OSW_UT(osw_drv_target_ut_phyconf2schema)
{
    struct schema_Wifi_Radio_Config base_rconf = {0};
    struct schema_Wifi_Radio_Config rconf = {0};
    struct schema_Wifi_Radio_Config_flags rchanged = {0};
    struct osw_channel_state cstates[] = {
        { .channel = { .control_freq_mhz = 5180 } },
        { .channel = { .control_freq_mhz = 5745 } },
    };
    const struct osw_drv_phy_state dstate_5g = {
        .n_channel_states = 2,
        .channel_states = cstates,
    };
    const struct osw_drv_phy_state dstate_5gl = {
        .n_channel_states = 1,
        .channel_states = &cstates[0],
    };
    const struct osw_drv_phy_state dstate_5gu = {
        .n_channel_states = 1,
        .channel_states = &cstates[1],
    };
    struct osw_state_phy_info info = {
        .phy_name = "phy1",
        .drv_state = &dstate_5g,
    };
    struct osw_drv_vif_config vifs[] = {
        {
            .vif_name = "vif1",
            .vif_type = OSW_VIF_AP,
            .enabled = true,
            .u.ap = {
                .beacon_interval_tu = 100,
                .beacon_interval_tu_changed = true,
                .channel_changed = true,
                .channel = {
                    .control_freq_mhz = 2412,
                    .width = OSW_CHANNEL_20MHZ,
                },
                .mode_changed = true,
                .mode = {
                    .vht_enabled = true,
                },
            },
        },
        {
            .vif_name = "vif2",
            .vif_type = OSW_VIF_AP,
            .enabled = true,
            .u.ap = {
                .beacon_interval_tu = 100,
                .channel = {
                    .control_freq_mhz = 2412,
                    .width = OSW_CHANNEL_20MHZ,
                },
            },
        },
        {
            .vif_name = "vif3",
            .vif_type = OSW_VIF_STA,
            .enabled = true,
            .u.ap = {
                .channel = {
                    .control_freq_mhz = 2417,
                    .width = OSW_CHANNEL_20MHZ,
                },
                .mode = {
                    .he_enabled = true,
                },
            },
        },
    };
    struct osw_drv_phy_config phy = {
        .phy_name = "phy1",
        .enabled = true,
        .enabled_changed = true,
        .tx_chainmask = 0x0f,
        .tx_chainmask_changed = true,
        .radar = OSW_RADAR_DETECT_DISABLED,
        .radar_changed = true,
        .vif_list = {
            .list = vifs,
            .count = ARRAY_SIZE(vifs),
        },
    };

    SCHEMA_SET_STR(base_rconf.hw_type, "hello");
    SCHEMA_KEY_VAL_SET(base_rconf.hw_config, "a", "1");
    SCHEMA_KEY_VAL_SET(base_rconf.hw_config, "b", "2");
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);

    assert(strcmp(rconf.if_name, phy.phy_name) == 0);
    assert(rchanged.enabled);
    assert(rchanged.tx_chainmask);
    assert(rchanged.dfs_demo);
    assert(rchanged.channel);
    assert(rchanged.ht_mode);
    assert(rchanged.freq_band);
    assert(rchanged.hw_mode);
    assert(rchanged.bcn_int);
    assert(rconf.enabled == phy.enabled);
    assert(rconf.enabled_exists == true);
    assert(rconf.tx_chainmask == phy.tx_chainmask);
    assert(rconf.tx_chainmask_exists == true);
    assert(rconf.dfs_demo == true);
    assert(rconf.dfs_demo_exists == true);
    assert(rconf.channel == 1);
    assert(rconf.channel_exists == true);
    assert(strcmp(rconf.ht_mode, "HT20") == 0);
    assert(rconf.ht_mode_exists == true);
    assert(strcmp(rconf.freq_band, "2.4G") == 0);
    assert(rconf.freq_band_exists == true);
    assert(strcmp(rconf.hw_mode, "11ac") == 0);
    assert(rconf.hw_mode_exists == true);
    assert(rconf.bcn_int == 100);
    assert(rconf.bcn_int_exists == true);
    assert(rconf.hw_config_len == 2);
    assert(strcmp(rconf.hw_type, "hello") == 0);
    assert(SCHEMA_KEY_VAL_NULL(rconf.hw_config, "a") != NULL);
    assert(SCHEMA_KEY_VAL_NULL(rconf.hw_config, "b") != NULL);
    assert(strcmp(SCHEMA_KEY_VAL(rconf.hw_config, "a"), "1") == 0);
    assert(strcmp(SCHEMA_KEY_VAL(rconf.hw_config, "b"), "2") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.beacon_interval_tu = 200;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(rconf.bcn_int_exists == false);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    phy.radar = OSW_RADAR_UNSUPPORTED;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(rconf.dfs_demo_exists == false);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.channel_changed = false;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(rchanged.channel == false);
    assert(rchanged.ht_mode == false);
    assert(rchanged.freq_band == false);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.channel.control_freq_mhz = 2417;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(rconf.channel_exists == false);
    assert(rconf.ht_mode_exists == false);
    assert(rconf.freq_band_exists == false);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.channel.control_freq_mhz = 5180;
    vifs[1].u.ap.channel.control_freq_mhz = 5180;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(rconf.channel == 36);
    assert(strcmp(rconf.freq_band, "5G") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    info.drv_state = &dstate_5gl;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(strcmp(rconf.freq_band, "5GL") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    info.drv_state = &dstate_5g;
    vifs[0].u.ap.channel.control_freq_mhz = 5745;
    vifs[1].u.ap.channel.control_freq_mhz = 5745;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(rconf.channel == 149);
    assert(strcmp(rconf.freq_band, "5G") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    info.drv_state = &dstate_5gu;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(strcmp(rconf.freq_band, "5GU") == 0);
    info.drv_state = &dstate_5g;

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.channel.control_freq_mhz = 5955;
    vifs[1].u.ap.channel.control_freq_mhz = 5955;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(rconf.channel == 1);
    assert(strcmp(rconf.freq_band, "6G") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.channel.control_freq_mhz = 5935;
    vifs[1].u.ap.channel.control_freq_mhz = 5935;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(rconf.channel == 2);
    assert(strcmp(rconf.freq_band, "6G") == 0); /* FIXME 5GL/5GU */

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.channel.width = OSW_CHANNEL_40MHZ;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(rconf.channel_exists == false);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.channel.width = OSW_CHANNEL_40MHZ;
    vifs[1].u.ap.channel.width = OSW_CHANNEL_40MHZ;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(strcmp(rconf.ht_mode, "HT40") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.channel.width = OSW_CHANNEL_80MHZ;
    vifs[1].u.ap.channel.width = OSW_CHANNEL_80MHZ;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(strcmp(rconf.ht_mode, "HT80") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.channel.width = OSW_CHANNEL_160MHZ;
    vifs[1].u.ap.channel.width = OSW_CHANNEL_160MHZ;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(strcmp(rconf.ht_mode, "HT160") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[1].u.ap.mode.he_enabled = true;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(strcmp(rconf.hw_mode, "11ax") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.mode.vht_enabled = false;
    vifs[1].u.ap.mode.he_enabled = false;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(strcmp(rconf.hw_mode, "11a") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.channel.control_freq_mhz = 2412;
    vifs[1].u.ap.channel.control_freq_mhz = 2412;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(strcmp(rconf.hw_mode, "11g") == 0);

    memset(&rconf, 0, sizeof(rconf));
    memset(&rchanged, 0, sizeof(rchanged));
    vifs[0].u.ap.mode.ht_enabled = true;
    osw_drv_target_phyconf2schema(&phy, &info, &base_rconf, &rconf, &rchanged);
    assert(strcmp(rconf.hw_mode, "11n") == 0);
}

OSW_UT(osw_drv_target_ut_vifconf2schema_ap)
{
    struct schema_Wifi_VIF_Config base_vconf = {0};
    struct schema_Wifi_VIF_Config vconf = {0};
    struct schema_Wifi_VIF_Config_flags vchanged = {0};
    struct osw_hwaddr acl[] = {
        { .octet = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06} },
        { .octet = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16} },
    };
    struct osw_ap_psk psks[] = {
        { .key_id = 1, .psk.str = "12345678" },
        { .key_id = 2, .psk.str = "87654321" },
        { .key_id = 3, .psk.str = "00000000" },
    };
    struct osw_drv_vif_config vif = {
        .vif_name = "vif1",
        .vif_type = OSW_VIF_AP,
        .enabled = true,
        .enabled_changed = true,
        .u.ap = {
            .bridge_if_name = { .buf = "br0" },
            .bridge_if_name_changed = true,
            .ssid = { .buf = "ssid1", .len = 5 },
            .ssid_changed = true,
            .ssid_hidden = true,
            .ssid_hidden_changed = true,
            .acl_policy = OSW_ACL_ALLOW_LIST,
            .acl_policy_changed = true,
            .isolated = true,
            .isolated_changed = true,
            .mcast2ucast = true,
            .mcast2ucast_changed = true,
            .mode_changed = true,
            .mode = {
                .wnm_bss_trans = true,
                .rrm_neighbor_report = true,
                .wmm_uapsd_enabled = true,
                .wps = true,
            },
            .wpa_changed = true,
            .wpa = {
                .wpa = true,
                .rsn = true,
                .akm_psk = true,
                .akm_sae = true,
                .pairwise_tkip = true,
                .pairwise_ccmp = true,
                .group_rekey_seconds = 60,
                .ft_mobility_domain = 0xff00,
            },
            .acl_changed = true,
            .acl = {
                .list = acl,
                .count = ARRAY_SIZE(acl),
            },
            .psk_list_changed = true,
            .psk_list = {
                .list = psks,
                .count = ARRAY_SIZE(psks),
            },
        },
    };

    SCHEMA_SET_INT(base_vconf.vif_radio_idx, 4);
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    assert(strcmp(vconf.if_name, vif.vif_name) == 0);
    assert(strcmp(vconf.mode, "ap") == 0);
    assert(strcmp(vconf.bridge, vif.u.ap.bridge_if_name.buf) == 0);
    assert(strcmp(vconf.ssid, vif.u.ap.ssid.buf) == 0);
    assert(strcmp(vconf.ssid_broadcast, "disabled") == 0);
    assert(strcmp(vconf.mac_list_type, "whitelist") == 0);
    assert(vconf.enabled == true);
    assert(vconf.ap_bridge == false);
    assert(vconf.mcast2ucast == true);
    assert(vconf.rrm == true);
    assert(vconf.btm == true);
    assert(vconf.uapsd_enable == true);
    assert(vconf.wps == true);
    assert(vconf.wpa == true);
    assert(vconf.ft_mobility_domain == 0xff00);
    assert(vconf.group_rekey == vif.u.ap.wpa.group_rekey_seconds);
    assert(vconf.vif_radio_idx == 4);
    {
        bool psk = false;
        bool sae = false;
        int i;
        for (i = 0; i < vconf.wpa_key_mgmt_len; i++) {
            if (strcmp(vconf.wpa_key_mgmt[i], SCHEMA_CONSTS_KEY_WPA_PSK) == 0) psk = true;
            if (strcmp(vconf.wpa_key_mgmt[i], SCHEMA_CONSTS_KEY_SAE) == 0) sae = true;
        }
        const bool wpa = vconf.wpa_pairwise_tkip
                      || vconf.wpa_pairwise_ccmp;
        const bool rsn = vconf.rsn_pairwise_tkip
                      || vconf.rsn_pairwise_ccmp;
        assert(wpa == true);
        assert(rsn == true);
        assert(psk == true);
        assert(sae == true);
    }
    {
        bool found[ARRAY_SIZE(acl)] = {0};
        int i;
        size_t j;
        for (i = 0; i < vconf.mac_list_len; i++) {
            struct osw_hwaddr addr;
            sscanf(vconf.mac_list[i], "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                   &addr.octet[0],
                   &addr.octet[1],
                   &addr.octet[2],
                   &addr.octet[3],
                   &addr.octet[4],
                   &addr.octet[5]);
            for (j = 0; j < ARRAY_SIZE(acl); j++) {
                if (memcmp(&addr, &acl[j], sizeof(addr)) == 0)
                    found[j] = true;
            }
        }
        int n = 0;
        for (j = 0; j < ARRAY_SIZE(acl); j++)
            if (found[j]) n++;
        assert(n == ARRAY_SIZE(acl));
    }
    {
        bool found[ARRAY_SIZE(psks)] = {0};
        int i;
        size_t j;
        for (i = 0; i < vconf.wpa_psks_len; i++) {
            int key_id = osw_drv_target_str2keyid(vconf.wpa_psks_keys[i]);
            for (j = 0; j < ARRAY_SIZE(psks); j++)
                if (psks[j].key_id == key_id)
                    found[j] = true;
        }
        int n = 0;
        for (j = 0; j < ARRAY_SIZE(psks); j++)
            if (found[j]) n++;
        assert(n == ARRAY_SIZE(psks));
    }

    assert(vconf.enabled_exists == true);
    assert(vconf.mode_exists == true);
    assert(vconf.bridge_exists == true);
    assert(vconf.ssid_exists == true);
    assert(vconf.ssid_broadcast_exists == true);
    assert(vconf.mac_list_type_exists == true);
    assert(vconf.ap_bridge_exists == true);
    assert(vconf.mcast2ucast_exists == true);
    assert(vconf.rrm_exists == true);
    assert(vconf.btm_exists == true);
    assert(vconf.uapsd_enable_exists == true);
    assert(vconf.wps_exists == true);
    assert(vconf.wpa_exists == true);
    assert(vconf.ft_mobility_domain_exists == true);
    assert(vconf.group_rekey_exists == true);
    assert(vconf.wpa_key_mgmt_len == 2);
    assert(vconf.mac_list_len == 2);
    assert(vconf.wpa_psks_len == 3);
    assert(vconf.vif_radio_idx_exists == true);

    assert(vchanged.enabled == true);
    assert(vchanged.bridge == true);
    assert(vchanged.ssid == true);
    assert(vchanged.ssid_broadcast == true);
    assert(vchanged.mac_list_type == true);
    assert(vchanged.ap_bridge == true);
    assert(vchanged.mcast2ucast == true);
    assert(vchanged.rrm == true);
    assert(vchanged.btm == true);
    assert(vchanged.uapsd_enable == true);
    assert(vchanged.wps == true);
    assert(vchanged.wpa == true);
    assert(vchanged.ft_mobility_domain == true);
    assert(vchanged.group_rekey == true);
    assert(vchanged.wpa_key_mgmt == true);
    assert(vchanged.mac_list == true);
    assert(vchanged.wpa_psks == true);

    memset(&vconf, 0, sizeof(vconf));
    memset(&vchanged, 0, sizeof(vchanged));
    vif.u.ap.acl_policy = OSW_ACL_DENY_LIST;
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    assert(strcmp(vconf.mac_list_type, "blacklist") == 0);

    memset(&vconf, 0, sizeof(vconf));
    memset(&vchanged, 0, sizeof(vchanged));
    vif.u.ap.acl_policy = OSW_ACL_NONE;
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    assert(strcmp(vconf.mac_list_type, "none") == 0);

    memset(&vconf, 0, sizeof(vconf));
    memset(&vchanged, 0, sizeof(vchanged));
    vif.u.ap.acl_policy = OSW_ACL_DENY_LIST + 1;
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    assert(vconf.mac_list_type_exists == false);

    memset(&vconf, 0, sizeof(vconf));
    memset(&vchanged, 0, sizeof(vchanged));
    vif.u.ap.wpa.rsn = false;
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    {
        bool psk = false;
        bool sae = false;
        int i;
        for (i = 0; i < vconf.wpa_key_mgmt_len; i++) {
            if (strcmp(vconf.wpa_key_mgmt[i], SCHEMA_CONSTS_KEY_WPA_PSK) == 0) psk = true;
            if (strcmp(vconf.wpa_key_mgmt[i], SCHEMA_CONSTS_KEY_SAE) == 0) sae = true;
        }
        const bool wpa = vconf.wpa_pairwise_tkip
                      || vconf.wpa_pairwise_ccmp;
        const bool rsn = vconf.rsn_pairwise_tkip
                      || vconf.rsn_pairwise_ccmp;
        assert(wpa == true);
        assert(rsn == false);
        assert(psk == true);
        assert(sae == true); /* this is arguably wrong */
    }

    memset(&vconf, 0, sizeof(vconf));
    memset(&vchanged, 0, sizeof(vchanged));
    vif.u.ap.wpa.wpa = false;
    vif.u.ap.wpa.rsn = true;
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    {
        bool psk = false;
        bool sae = false;
        int i;
        for (i = 0; i < vconf.wpa_key_mgmt_len; i++) {
            if (strcmp(vconf.wpa_key_mgmt[i], SCHEMA_CONSTS_KEY_WPA_PSK) == 0) psk = true;
            if (strcmp(vconf.wpa_key_mgmt[i], SCHEMA_CONSTS_KEY_SAE) == 0) sae = true;
        }
        const bool wpa = vconf.wpa_pairwise_tkip
                      || vconf.wpa_pairwise_ccmp;
        const bool rsn = vconf.rsn_pairwise_tkip
                      || vconf.rsn_pairwise_ccmp;
        assert(wpa == false);
        assert(rsn == true);
        assert(psk == true);
        assert(sae == true);
    }

    memset(&vconf, 0, sizeof(vconf));
    memset(&vchanged, 0, sizeof(vchanged));
    vif.u.ap.wpa.akm_psk = false;
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    {
        bool sae = false;
        bool psk = false;
        int i;
        for (i = 0; i < vconf.wpa_key_mgmt_len; i++) {
            if (strcmp(vconf.wpa_key_mgmt[i], SCHEMA_CONSTS_KEY_WPA_PSK) == 0) psk = true;
            if (strcmp(vconf.wpa_key_mgmt[i], SCHEMA_CONSTS_KEY_SAE) == 0) sae = true;
        }
        const bool wpa = vconf.wpa_pairwise_tkip
                      || vconf.wpa_pairwise_ccmp;
        const bool rsn = vconf.rsn_pairwise_tkip
                      || vconf.rsn_pairwise_ccmp;
        assert(wpa == false);
        assert(rsn == true);
        assert(psk == false);
        assert(sae == true);
    }

    memset(&vconf, 0, sizeof(vconf));
    memset(&vchanged, 0, sizeof(vchanged));
    vif.u.ap.wpa.group_rekey_seconds = 0;
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    assert(vconf.group_rekey_exists == true);
    assert(vconf.group_rekey == vif.u.ap.wpa.group_rekey_seconds);

    memset(&vconf, 0, sizeof(vconf));
    memset(&vchanged, 0, sizeof(vchanged));
    vif.u.ap.wpa.group_rekey_seconds = OSW_WPA_GROUP_REKEY_UNDEFINED;
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    assert(vconf.group_rekey_exists == false);

    memset(&vconf, 0, sizeof(vconf));
    memset(&vchanged, 0, sizeof(vchanged));
    psks[ARRAY_SIZE(psks) - 1].key_id = psks[0].key_id;
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    assert(vconf.wpa_psks_len == 2);

    memset(&base_vconf, 0, sizeof(base_vconf));
    memset(&vconf, 0, sizeof(vconf));
    memset(&vchanged, 0, sizeof(vchanged));
    osw_drv_target_vifconf2schema(&vif, &base_vconf, &vconf, &vchanged, NULL, 0);
    assert(vconf.vif_radio_idx_exists == false);
}

OSW_UT(osw_drv_target_ut_cs)
{
    struct osw_drv_phy_state info = {0};
    const enum osw_channel_state_dfs s = OSW_CHANNEL_NON_DFS;

    osw_drv_target_cs_append(&info.channel_states, &info.n_channel_states, 2000, s);
    osw_drv_target_cs_append(&info.channel_states, &info.n_channel_states, 3000, s);
    osw_drv_target_cs_append(&info.channel_states, &info.n_channel_states, 4000, s);
    assert(info.n_channel_states == 3);
    assert(info.channel_states[0].channel.control_freq_mhz == 2000);
    assert(info.channel_states[1].channel.control_freq_mhz == 3000);
    assert(info.channel_states[2].channel.control_freq_mhz == 4000);
    assert(osw_drv_target_cs_lookup(info.channel_states, info.n_channel_states, 2000) == &info.channel_states[0]);
    assert(osw_drv_target_cs_lookup(info.channel_states, info.n_channel_states, 3000) == &info.channel_states[1]);
    assert(osw_drv_target_cs_lookup(info.channel_states, info.n_channel_states, 4000) == &info.channel_states[2]);
}

OSW_UT(osw_drv_target_ut_ch2freq)
{
    assert(osw_drv_target_ch2freq(1, 1) == 0);
    assert(osw_drv_target_ch2freq(2, 1) == 2412);
    assert(osw_drv_target_ch2freq(5, 36) == 5180);
    assert(osw_drv_target_ch2freq(6, 1) == 5955);
    assert(osw_drv_target_ch2freq(6, 2) == 5935);
}

OSW_UT(osw_drv_target_ut_keyid)
{
    char buf[64];

    assert(osw_drv_target_str2keyid("key") == 0);
    assert(osw_drv_target_str2keyid("key-4") == 4);
    assert(osw_drv_target_str2keyid("key--3") == -3);
    osw_drv_target_keyid2str(buf, sizeof(buf), 0); assert(strcmp(buf, "key") == 0);
    osw_drv_target_keyid2str(buf, sizeof(buf), 10); assert(strcmp(buf, "key-10") == 0);
    osw_drv_target_keyid2str(buf, sizeof(buf), -2); assert(strcmp(buf, "key--2") == 0);
}
