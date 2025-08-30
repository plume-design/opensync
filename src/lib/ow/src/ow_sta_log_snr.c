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

/* opensync */
#include <log.h>
#include <const.h>
#include <util.h>
#include <os.h>
#include <memutil.h>

/* osw */
#include <osw_module.h>
#include <osw_state.h>
#include <osw_stats.h>
#include <osw_stats_defs.h>
#include <osw_util.h>
#include <osw_etc.h>

/*
 * Purpose:
 *
 * Generate logs containing SNR of a client, whenever a
 * client connects, or client's SNR has changed
 * significantly enough beyond possible
 * deviation/instability of SNR reports.
 *
 * Future:
 *
 * Either this, or a derivative of this module, could be
 * used to auto-tune tx power on backhaul links when "too
 * strong" signal condition is detected. This is beyond just
 * steering. If possible, per-sta tx power could be also
 * adjusted (if supported by underlying driver) not only for
 * backhaul links, but also for regular clients if they
 * happen to find themselves too close to the AP.
 */

#define OW_STA_LOG_SNR_TOO_WEAK_SNR_DB 10 /* -95 + 10 = -85dBm that's barely enough */
#define OW_STA_LOG_SNR_TOO_STRONG_SNR_DB 75 /* -95 + 75 = -20 dBm, that's a lot */
#define OW_STA_LOG_SNR_THRESHOLD_DB 10
#define OW_STA_LOG_SNR_PERIOD_SEC 1.0

struct ow_sta_log_snr_sta_id {
    struct osw_hwaddr sta_addr;
    struct osw_ifname vif_name;
};

struct ow_sta_log_snr_sta {
    struct ds_tree_node node;
    struct ow_sta_log_snr_sta_id id;
    int last_snr_db;
    bool just_connected;
    void *assoc_ies;
    size_t assoc_ies_len;
};

struct ow_sta_log_snr {
    struct osw_state_observer state_obs;
    struct osw_stats_subscriber *stats_sub;
    struct ds_tree sta_tree;
    int threshold_db;
    int too_weak_snr_db;
    int too_strong_snr_db;
    float period_sec;
};

static int
ow_sta_log_snr_sta_id_cmp(const void *a, const void *b)
{
    const struct ow_sta_log_snr_sta_id *x = a;
    const struct ow_sta_log_snr_sta_id *y = b;
    const int d1 = strcmp(x->vif_name.buf, y->vif_name.buf);
    if (d1 != 0) return d1;
    const int d2 = osw_hwaddr_cmp(&x->sta_addr, &y->sta_addr);
    if (d2 != 0) return d2;
    return 0;
}

static void
ow_sta_log_snr_report_cb(const enum osw_stats_id stats_id,
                         const struct osw_tlv *delta,
                         const struct osw_tlv *last,
                         void *priv)
{
    struct ow_sta_log_snr *m = priv;

    if (stats_id != OSW_STATS_STA) return;

    const struct osw_stats_defs *defs = osw_stats_defs_lookup(stats_id);
    const size_t tb_size = defs->size;
    const struct osw_tlv_policy *policy = defs->tpolicy;

    const struct osw_tlv_hdr *tb[tb_size];
    memset(tb, 0, tb_size * sizeof(tb[0]));

    const size_t left = osw_tlv_parse(delta->data, delta->used, policy, tb, tb_size);
    WARN_ON(left != 0);

    const struct osw_tlv_hdr *phy = tb[OSW_STATS_STA_PHY_NAME];
    const struct osw_tlv_hdr *vif = tb[OSW_STATS_STA_VIF_NAME];
    const struct osw_tlv_hdr *mac = tb[OSW_STATS_STA_MAC_ADDRESS];
    const struct osw_tlv_hdr *snr = tb[OSW_STATS_STA_SNR_DB];

    if (phy == NULL) return;
    if (vif == NULL) return;
    if (mac == NULL) return;
    if (snr == NULL) return;

    /* It's unsigned, but putting into signed to make sure
     * subtraction won't underflow over to 2^32-1.
     */
    const int snr_db = osw_tlv_get_u32(snr);

    const char *vif_name = osw_tlv_get_string(vif);
    struct ow_sta_log_snr_sta_id id;
    MEMZERO(id);
    STRSCPY_WARN(id.vif_name.buf, vif_name);
    osw_tlv_get_hwaddr(&id.sta_addr, mac);

    struct ow_sta_log_snr_sta *sta = ds_tree_find(&m->sta_tree, &id);
    if (sta == NULL) return;

    const int snr_diff = abs(sta->last_snr_db - snr_db);
    const bool snr_diff_enough = snr_diff >= m->threshold_db;
    const bool should_report = sta->just_connected || snr_diff_enough;

    LOGT("ow: sta_log_snr: %s/"OSW_HWADDR_FMT": snr=%d last=%d diff=%d connected=%d should=%d",
         vif_name,
         OSW_HWADDR_ARG(&id.sta_addr),
         snr_db,
         sta->last_snr_db,
         snr_diff,
         sta->just_connected,
         should_report);

    if (should_report == false) return;

    if (sta->just_connected) {
        LOGI("ow: sta_log_snr: %s/"OSW_HWADDR_FMT": snr upon connect: %d",
             vif_name,
             OSW_HWADDR_ARG(&id.sta_addr),
             snr_db);
    }
    else {
        LOGI("ow: sta_log_snr: %s/"OSW_HWADDR_FMT": snr changed: %d -> %d",
             vif_name,
             OSW_HWADDR_ARG(&id.sta_addr),
             sta->last_snr_db,
             snr_db);
    }

    if (snr_db <= m->too_weak_snr_db) {
        LOGN("ow: sta_log_snr: %s/"OSW_HWADDR_FMT": snr too weak: %d <= %d, expect link issues",
             vif_name,
             OSW_HWADDR_ARG(&id.sta_addr),
             snr_db,
             m->too_weak_snr_db);
    }

    if (snr_db >= m->too_strong_snr_db) {
        LOGN("ow: sta_log_snr: %s/"OSW_HWADDR_FMT": snr too strong: %d >= %d, expect link issues",
             vif_name,
             OSW_HWADDR_ARG(&id.sta_addr),
             snr_db,
             m->too_strong_snr_db);
    }

    sta->last_snr_db = snr_db;
    sta->just_connected = false;
}

static void
ow_sta_log_snr_dump_assoc_ies(const struct ow_sta_log_snr_sta *sta)
{
    const void *ies = sta->assoc_ies;
    size_t ies_len = sta->assoc_ies_len;
    struct osw_assoc_req_info info;

    const bool valid = osw_parse_assoc_req_ies(ies, ies_len, &info);
    const bool invalid = !valid;
    if (invalid) return;

    const char *vif_name = sta->id.vif_name.buf;
    const struct osw_hwaddr *sta_addr = &sta->id.sta_addr;
    const enum osw_channel_width width = osw_assoc_req_to_max_chwidth(&info);
    const unsigned int max_nss = osw_assoc_req_to_max_nss(&info);
    const unsigned int max_mcs = osw_assoc_req_to_max_mcs(&info);

    bool band_2ghz = false;
    bool band_5ghz = false;
    bool band_6ghz = false;
    size_t i;
    for (i = 0; i < info.op_class_cnt; i++) {
        const uint8_t op_class = info.op_class_list[i];
        const enum osw_band band = osw_op_class_to_band(op_class);
        switch (band) {
            case OSW_BAND_UNDEFINED: break;
            case OSW_BAND_2GHZ: band_2ghz = true; break;
            case OSW_BAND_5GHZ: band_5ghz = true; break;
            case OSW_BAND_6GHZ: band_6ghz = true; break;
        }
    }

    /* FIXME: This could estimate max phy_rate based on the
     * capabs and dump it into logs for easier debugging. It
     * would then also be able to estimate it based on SNR
     * changes.
     */
    LOGI("ow: sta_log_snr: %s/"OSW_HWADDR_FMT": link capab: bw=%sMHz nss=%u mcs=%u%s%s%s%s%s%s%s%s%s%s",
         vif_name,
         OSW_HWADDR_ARG(sta_addr),
         osw_channel_width_to_str(width),
         max_nss,
         max_mcs,
         info.ht_caps_present ? " 11n" : "",
         info.vht_caps_present ? " 11ac" : "",
         info.he_caps_present ? " 11ax" : "",
         band_2ghz ? " 2ghz" : "",
         band_5ghz ? " 5ghz" : "",
         band_6ghz ? " 6ghz" : "",
         info.wnm_bss_trans ? " btm" : "",
         info.rrm_neighbor_bcn_pas_meas ? " rrmp" : "",
         info.rrm_neighbor_bcn_act_meas ? " rrma" : "",
         info.rrm_neighbor_bcn_tab_meas ? " rrmt" : "");
}

static void
ow_sta_log_snr_set_assoc_ies(struct ow_sta_log_snr_sta *sta,
                             const struct osw_state_sta_info *info)
{
    const void *new_ies = info != NULL ? info->assoc_req_ies : NULL;
    const size_t new_ies_len = info != NULL ? info->assoc_req_ies_len : 0;

    const bool ies_added = (sta->assoc_ies == NULL)
                        && (new_ies != NULL);
    const bool ies_removed = (sta->assoc_ies != NULL)
                          && (new_ies == NULL);
    const bool ies_same = (sta->assoc_ies != NULL)
                       && (new_ies != NULL)
                       && (sta->assoc_ies_len == new_ies_len)
                       && (memcmp(sta->assoc_ies, new_ies, sta->assoc_ies_len) == 0);
    const bool ies_changed = ies_added || ies_removed || (ies_same == false);

    if (ies_changed) {
        FREE(sta->assoc_ies);
        sta->assoc_ies = NULL;
        sta->assoc_ies_len = 0;

        if (new_ies != NULL) {
            sta->assoc_ies = MEMNDUP(new_ies, new_ies_len);
            sta->assoc_ies_len = new_ies_len;
        }

        ow_sta_log_snr_dump_assoc_ies(sta);
    }
}

static void
ow_sta_log_snr_sta_connected_cb(struct osw_state_observer *o,
                                const struct osw_state_sta_info *info)
{
    struct ow_sta_log_snr *m = container_of(o, struct ow_sta_log_snr, state_obs);
    struct ow_sta_log_snr_sta_id id;
    MEMZERO(id);
    STRSCPY_WARN(id.vif_name.buf, info->vif->vif_name);
    id.sta_addr = *info->mac_addr;
    struct ow_sta_log_snr_sta *sta = ds_tree_find(&m->sta_tree, &id);
    if (WARN_ON(sta != NULL)) return;
    sta = CALLOC(1, sizeof(*sta));
    sta->id = id;
    sta->just_connected = true;
    ow_sta_log_snr_set_assoc_ies(sta, info);
    ds_tree_insert(&m->sta_tree, sta, &sta->id);
}

static void
ow_sta_log_snr_sta_changed_cb(struct osw_state_observer *o,
                              const struct osw_state_sta_info *info)
{
    struct ow_sta_log_snr *m = container_of(o, struct ow_sta_log_snr, state_obs);
    struct ow_sta_log_snr_sta_id id;
    MEMZERO(id);
    STRSCPY_WARN(id.vif_name.buf, info->vif->vif_name);
    id.sta_addr = *info->mac_addr;
    struct ow_sta_log_snr_sta *sta = ds_tree_find(&m->sta_tree, &id);
    if (WARN_ON(sta == NULL)) return;
    ow_sta_log_snr_set_assoc_ies(sta, info);
}

static void
ow_sta_log_snr_sta_disconnected_cb(struct osw_state_observer *o,
                                   const struct osw_state_sta_info *info)
{
    struct ow_sta_log_snr *m = container_of(o, struct ow_sta_log_snr, state_obs);
    struct ow_sta_log_snr_sta_id id;
    MEMZERO(id);
    STRSCPY_WARN(id.vif_name.buf, info->vif->vif_name);
    id.sta_addr = *info->mac_addr;
    struct ow_sta_log_snr_sta *sta = ds_tree_find(&m->sta_tree, &id);
    if (WARN_ON(sta == NULL)) return;
    ow_sta_log_snr_set_assoc_ies(sta, NULL);
    ds_tree_remove(&m->sta_tree, sta);
    FREE(sta);
}

#define SET_PARAM_F(w, n, e) do { \
        const char *v = osw_etc_get(#e); \
        if (v != NULL) (w)->n = strtof(v, NULL); \
    } while (0)

#define SET_PARAM_ULL(w, n, e) do { \
        const char *v = osw_etc_get(#e); \
        if (v != NULL) (w)->n = strtoull(v, NULL, 10); \
    } while (0)

OSW_MODULE(ow_sta_log_snr)
{
    OSW_MODULE_LOAD(osw_state);
    OSW_MODULE_LOAD(osw_stats);
    static struct ow_sta_log_snr m = {
        .state_obs = {
        .name = __FILE__,
            .sta_connected_fn = ow_sta_log_snr_sta_connected_cb,
            .sta_changed_fn = ow_sta_log_snr_sta_changed_cb,
            .sta_disconnected_fn = ow_sta_log_snr_sta_disconnected_cb,
        },
        .sta_tree = DS_TREE_INIT(ow_sta_log_snr_sta_id_cmp, struct ow_sta_log_snr_sta, node),
        .threshold_db = OW_STA_LOG_SNR_THRESHOLD_DB,
        .too_weak_snr_db = OW_STA_LOG_SNR_TOO_WEAK_SNR_DB,
        .too_strong_snr_db = OW_STA_LOG_SNR_TOO_STRONG_SNR_DB,
        .period_sec = OW_STA_LOG_SNR_PERIOD_SEC,
    };
    SET_PARAM_ULL(&m, threshold_db, OW_STA_LOG_SNR_THRESHOLD_DB);
    SET_PARAM_ULL(&m, too_weak_snr_db, OW_STA_LOG_SNR_TOO_WEAK_SNR_DB);
    SET_PARAM_ULL(&m, too_strong_snr_db, OW_STA_LOG_SNR_TOO_STRONG_SNR_DB);
    SET_PARAM_F(&m, period_sec, OW_STA_LOG_SNR_PERIOD_SEC);
    m.stats_sub = osw_stats_subscriber_alloc();

    osw_stats_subscriber_set_sta(m.stats_sub, true);
    osw_stats_subscriber_set_report_fn(m.stats_sub, ow_sta_log_snr_report_cb, &m);
    osw_stats_subscriber_set_report_seconds(m.stats_sub, m.period_sec);
    osw_stats_subscriber_set_poll_seconds(m.stats_sub, m.period_sec);

    osw_stats_register_subscriber(m.stats_sub);
    osw_state_register_observer(&m.state_obs);
    return &m;
}
