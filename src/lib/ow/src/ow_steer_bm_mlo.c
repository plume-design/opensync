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

#include <string.h>

#include <os.h>
#include <const.h>
#include <ds_tree.h>
#include <memutil.h>
#include <log.h>
#include <opensync_stats_mlo.pb-c.h>
#include <qm_conn.h>
#include <os_types.h>
#include <osw_types.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_module.h>
#include <osw_state.h>
#include <osw_util.h>
#include <osw_sta_assoc.h>
#include <osw_sta_idle.h>
#include <osw_sta_snr.h>
#include <osp_unit.h>
#include <target.h>
#include "ow_steer_bm_mlo.h"

/*
 * MLO-aware band-steering report producer (BSReportV2).
 *
 * This module is self-contained: it registers against osw_sta_assoc and
 * osw_state to source the association lifecycle (connect / disconnect /
 * reconnect / capabilities) on its own, and it owns the protobuf report and the
 * MQTT publish cadence.
 * ow_steer_bm only needs to:
 *   - scope the producer to configured clients via _client_track()/_untrack(),
 *   - feed the few events that are internal steering decisions and therefore not
 *     observable through osw_sta_assoc (steering actions, misc events and the
 *     disconnect reason codes).
 */

#define LOG_PREFIX(fmt, ...)      "ow: steer: bm: mlo: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_M(m, fmt, ...) LOG_PREFIX("%p: " fmt, (m), ##__VA_ARGS__)

#define OW_STEER_BM_MLO_DEFAULT_INTERVAL_NSEC OSW_TIME_SEC(60)
#define OW_STEER_BM_MLO_SNR_BUF_SIZE          256
#define OW_STEER_BM_MLO_MAX_EVENTS            256
#define OW_STEER_BM_MLO_IDLE_BYTES_PER_SEC    250

#define SET_PERCENTILE(_si, _field, _sorted, _n, _pct)                           \
    do                                                                           \
    {                                                                            \
        const uint8_t *_p = ow_steer_bm_mlo_percentile((_sorted), (_n), (_pct)); \
        if (_p != NULL)                                                          \
        {                                                                        \
            (_si)->has_##_field = true;                                          \
            (_si)->_field = (float)*_p;                                          \
        }                                                                        \
    } while (0)

struct ow_steer_bm_mlo_saved_log
{
    struct osw_channel channel;
    uint8_t *samples;
    size_t count;
    Sts__BsMlo__SignalInfoSource source;
};

struct ow_steer_bm_mlo_link_signal
{
    struct ds_tree_node node;
    ow_steer_bm_mlo_t *m;
    struct osw_hwaddr bssid;
    struct osw_hwaddr sta_mac;
    osw_sta_snr_observer_t *data_obs;
    osw_sta_snr_observer_t *probe_obs;
    struct ow_steer_bm_mlo_saved_log *saved_logs;
    size_t n_saved_logs;
};

struct ow_steer_bm_mlo_client
{
    struct ds_tree_node node;
    ow_steer_bm_mlo_t *m;
    struct osw_hwaddr addr;
    bool bss_tracked;
    bool connected;
    bool is_mlo;
    uint64_t observation_start_nsec;
    osw_sta_assoc_links_t last_links;
    struct ds_tree link_signals;
    osw_sta_idle_observer_t *idle_obs;
};

struct ow_steer_bm_mlo
{
    char *topic;
    char *node_id;
    uint64_t report_interval_nsec;
    Sts__BsMlo__BSReportV2 *report;
    struct ds_tree clients;
    osw_sta_assoc_t *sta_assoc;
    osw_sta_assoc_observer_t *global_assoc_obs;
    struct osw_timer report_timer;
    osw_sta_snr_t *sta_snr;
    osw_sta_idle_t *sta_idle;
};

static bool ow_steer_bm_mlo_phy_is_5g_unified(const struct osw_state_vif_info *vif)
{
    if (vif->phy == NULL) return false;
    if (vif->phy->drv_state == NULL) return false;
    const struct osw_drv_phy_state *phy = vif->phy->drv_state;
    bool has_5gl = false;
    bool has_5gu = false;
    for (size_t i = 0; i < phy->n_channel_states; i++)
    {
        const int freq = phy->channel_states[i].channel.control_freq_mhz;
        if (freq >= 5180 && freq <= 5480)
            has_5gl = true;
        else if (freq >= 5500 && freq <= 5885)
            has_5gu = true;
    }
    return has_5gl && has_5gu;
}

static bool ow_steer_bm_mlo_band_from_bssid(
        const struct osw_hwaddr *bssid,
        const struct osw_channel *ch_override,
        Sts__BsMlo__RadioBandType *band_out,
        uint32_t *chan_out,
        bool *chan_valid_out)
{
    *chan_valid_out = false;
    const struct osw_state_vif_info *vif = osw_state_vif_lookup_by_mac_addr(bssid);
    if (vif == NULL || vif->drv_state == NULL) return false;
    if (vif->drv_state->vif_type != OSW_VIF_AP) return false;

    const struct osw_channel *ch = (ch_override != NULL) ? ch_override : &vif->drv_state->u.ap.channel;
    const int chan = osw_freq_to_chan(ch->control_freq_mhz);
    if (chan > 0)
    {
        *chan_out = (uint32_t)chan;
        *chan_valid_out = true;
    }

    switch (osw_channel_to_band(ch))
    {
        case OSW_BAND_2GHZ:
            *band_out = STS__BS_MLO__RADIO_BAND_TYPE__BAND2G;
            return true;
        case OSW_BAND_5GHZ:
            if (ow_steer_bm_mlo_phy_is_5g_unified(vif))
                *band_out = STS__BS_MLO__RADIO_BAND_TYPE__BAND5G;
            else if (ch->control_freq_mhz < 5500)
                *band_out = STS__BS_MLO__RADIO_BAND_TYPE__BAND5GL;
            else
                *band_out = STS__BS_MLO__RADIO_BAND_TYPE__BAND5GU;
            return true;
        case OSW_BAND_6GHZ:
            *band_out = STS__BS_MLO__RADIO_BAND_TYPE__BAND6G;
            return true;
        case OSW_BAND_UNDEFINED:
            return false;
    }
    return false;
}

static int ow_steer_bm_mlo_snr_cmp(const void *a, const void *b)
{
    const uint8_t *x = a;
    const uint8_t *y = b;
    if (*x < *y) return -1;
    if (*x > *y) return 1;
    return 0;
}

static const uint8_t *ow_steer_bm_mlo_percentile(const uint8_t *sorted, size_t n, size_t percentile)
{
    if (WARN_ON(percentile == 0)) return NULL;
    if (WARN_ON(percentile > 100)) return NULL;
    const size_t min_samples = 100 / percentile;
    if (n < min_samples) return NULL;
    const size_t idx = percentile * (n - 1) / 100;
    if (WARN_ON(idx >= n)) return NULL;
    return &sorted[idx];
}

static struct ow_steer_bm_mlo_link_signal *ow_steer_bm_mlo_link_signal_get(
        struct ow_steer_bm_mlo_client *client,
        const struct osw_hwaddr *bssid,
        const struct osw_hwaddr *sta_mac)
{
    if (WARN_ON(client == NULL)) return NULL;
    if (WARN_ON(bssid == NULL)) return NULL;
    if (WARN_ON(sta_mac == NULL)) return NULL;

    struct ow_steer_bm_mlo_link_signal *ls = ds_tree_find(&client->link_signals, bssid);
    if (ls != NULL) return ls;

    ls = CALLOC(1, sizeof(*ls));
    ls->m = client->m;
    ls->bssid = *bssid;
    ls->sta_mac = *sta_mac;
    ds_tree_insert(&client->link_signals, ls, &ls->bssid);
    return ls;
}

static void ow_steer_bm_mlo_link_signal_free(struct ow_steer_bm_mlo_link_signal *ls)
{
    if (ls == NULL) return;
    osw_sta_snr_observer_drop(ls->data_obs);
    osw_sta_snr_observer_drop(ls->probe_obs);
    for (size_t i = 0; i < ls->n_saved_logs; i++)
        FREE(ls->saved_logs[i].samples);
    FREE(ls->saved_logs);
    FREE(ls);
}

static void ow_steer_bm_mlo_save_flushed_log(
        struct ow_steer_bm_mlo_link_signal *ls,
        osw_sta_snr_observer_t *obs,
        Sts__BsMlo__SignalInfoSource source)
{
    if (ls->m->topic == NULL) return;

    const uint8_t *samples = osw_sta_snr_observer_get_log(obs);
    const size_t n = osw_sta_snr_observer_get_log_size(obs);
    if (samples == NULL || n == 0) return;

    const struct osw_channel *ch = osw_sta_snr_observer_get_channel(obs);
    if (ch == NULL) return;

    const size_t idx = ls->n_saved_logs;
    ls->n_saved_logs++;
    ls->saved_logs = REALLOC(ls->saved_logs, ls->n_saved_logs * sizeof(*ls->saved_logs));

    struct ow_steer_bm_mlo_saved_log *sl = &ls->saved_logs[idx];
    sl->channel = *ch;
    sl->samples = MALLOC(n * sizeof(uint8_t));
    memcpy(sl->samples, samples, n * sizeof(uint8_t));
    sl->count = n;
    sl->source = source;
}

static void ow_steer_bm_mlo_data_log_flushed_cb(void *priv)
{
    struct ow_steer_bm_mlo_link_signal *ls = priv;
    ow_steer_bm_mlo_save_flushed_log(ls, ls->data_obs, STS__BS_MLO__SIGNAL_INFO_SOURCE__DATA_PACKET);
}

static void ow_steer_bm_mlo_probe_log_flushed_cb(void *priv)
{
    struct ow_steer_bm_mlo_link_signal *ls = priv;
    ow_steer_bm_mlo_save_flushed_log(ls, ls->probe_obs, STS__BS_MLO__SIGNAL_INFO_SOURCE__PROBE_REQUEST);
}

static void ow_steer_bm_mlo_link_signal_ensure_obs(
        struct ow_steer_bm_mlo_link_signal *ls,
        ow_steer_bm_mlo_t *m,
        const struct osw_hwaddr *mld_addr)
{
    if (ls->data_obs == NULL)
    {
        osw_sta_snr_params_t *p = osw_sta_snr_params_alloc();
        osw_sta_snr_params_set_sta_addr(p, &ls->sta_mac);
        osw_sta_snr_params_set_mld_addr(p, mld_addr);
        osw_sta_snr_params_set_vif_addr(p, &ls->bssid);
        osw_sta_snr_params_set_log_capacity(p, OW_STEER_BM_MLO_SNR_BUF_SIZE);
        osw_sta_snr_params_set_log_flushed_fn(p, ow_steer_bm_mlo_data_log_flushed_cb, ls);
        ls->data_obs = osw_sta_snr_observer_alloc(m->sta_snr, p);
    }
    if (ls->probe_obs == NULL)
    {
        osw_sta_snr_params_t *p = osw_sta_snr_params_alloc();
        osw_sta_snr_params_set_sta_addr(p, &ls->sta_mac);
        osw_sta_snr_params_set_mld_addr(p, mld_addr);
        osw_sta_snr_params_set_vif_addr(p, &ls->bssid);
        osw_sta_snr_params_set_source_data_rx(p, false);
        osw_sta_snr_params_set_source_probe_rx(p, true);
        osw_sta_snr_params_set_log_capacity(p, OW_STEER_BM_MLO_SNR_BUF_SIZE);
        osw_sta_snr_params_set_log_flushed_fn(p, ow_steer_bm_mlo_probe_log_flushed_cb, ls);
        ls->probe_obs = osw_sta_snr_observer_alloc(m->sta_snr, p);
    }
}

static void ow_steer_bm_mlo_link_signal_free_all(struct ds_tree *tree)
{
    struct ow_steer_bm_mlo_link_signal *ls;
    while ((ls = ds_tree_remove_head(tree)) != NULL)
    {
        ow_steer_bm_mlo_link_signal_free(ls);
    }
}

static Sts__BsMlo__SignalInfo *ow_steer_bm_mlo_build_signal_info(const uint8_t *samples, size_t n)
{
    if (samples == NULL || n == 0) return NULL;

    uint8_t sorted[OW_STEER_BM_MLO_SNR_BUF_SIZE];
    if (WARN_ON(n > OW_STEER_BM_MLO_SNR_BUF_SIZE)) n = OW_STEER_BM_MLO_SNR_BUF_SIZE;
    memcpy(sorted, samples, n * sizeof(uint8_t));
    qsort(sorted, n, sizeof(uint8_t), ow_steer_bm_mlo_snr_cmp);

    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += sorted[i];

    Sts__BsMlo__SignalInfo *si = MALLOC(sizeof(*si));
    sts__bs_mlo__signal_info__init(si);
    si->rssi_avg_dbm = (float)sum / (float)n;
    si->rssi_min_dbm = (float)sorted[0];
    si->rssi_max_dbm = (float)sorted[n - 1];
    si->has_sample_count = true;
    si->sample_count = (uint32_t)n;

    SET_PERCENTILE(si, rssi_p5, sorted, n, 5);
    SET_PERCENTILE(si, rssi_p25, sorted, n, 25);
    SET_PERCENTILE(si, rssi_p50, sorted, n, 50);
    SET_PERCENTILE(si, rssi_p75, sorted, n, 75);
    SET_PERCENTILE(si, rssi_p95, sorted, n, 95);

    return si;
}

static char *ow_steer_bm_mlo_mac_dup(const struct osw_hwaddr *addr)
{
    struct osw_hwaddr_str str;
    osw_hwaddr2str(addr, &str);
    return STRDUP(str.buf);
}

static Sts__BsMlo__LinkInfo *ow_steer_bm_mlo_build_link_info(
        const struct osw_hwaddr *bssid,
        const struct osw_hwaddr *sta_mac,
        const struct osw_channel *ch_override)
{
    if (WARN_ON(bssid == NULL)) return NULL;
    if (WARN_ON(sta_mac == NULL)) return NULL;

    Sts__BsMlo__RadioBandType band;
    uint32_t chan = 0;
    bool chan_valid = false;
    if (ow_steer_bm_mlo_band_from_bssid(bssid, ch_override, &band, &chan, &chan_valid) == false) return NULL;

    Sts__BsMlo__LinkInfo *li = MALLOC(sizeof(*li));
    sts__bs_mlo__link_info__init(li);
    /* link_id is the actual 802.11be Link ID, which osw_sta_assoc does not expose
     * today, so it is left unset (optional in the proto). TODO: extend
     * osw_sta_assoc to carry the per-link 802.11be Link ID, then populate here:
     *   li->has_link_id = true;
     *   li->link_id = <802.11be link id from the osw_sta_assoc link>;
     */
    li->band = band;
    li->bssid = ow_steer_bm_mlo_mac_dup(bssid);
    li->sta_mac = ow_steer_bm_mlo_mac_dup(sta_mac);
    if (chan_valid)
    {
        li->has_channel_number = true;
        li->channel_number = chan;
    }
    return li;
}

static Sts__BsMlo__LinkInfo **ow_steer_bm_mlo_build_links(const osw_sta_assoc_links_t *links, size_t *n_out)
{
    *n_out = 0;
    if (links == NULL || links->count == 0) return NULL;
    Sts__BsMlo__LinkInfo **arr = MALLOC(links->count * sizeof(*arr));
    for (size_t i = 0; i < links->count; i++)
    {
        Sts__BsMlo__LinkInfo *li = ow_steer_bm_mlo_build_link_info(
                &links->links[i].local_sta_addr,
                &links->links[i].remote_sta_addr,
                NULL);
        if (li == NULL) continue;
        arr[(*n_out)++] = li;
    }
    if (*n_out == 0)
    {
        FREE(arr);
        return NULL;
    }
    return arr;
}

static const osw_sta_assoc_links_t *ow_steer_bm_mlo_client_live_links(
        const struct ow_steer_bm_mlo_client *c,
        bool *is_mlo_out)
{
    if (WARN_ON(c == NULL)) return NULL;
    if (c->connected == false) return NULL;
    if (is_mlo_out != NULL) *is_mlo_out = c->is_mlo;
    return &c->last_links;
}

/* Find a tracked client by its association address (the m->clients key, which
 * equals ow_steer_bm's client/sta addr — the MLD MAC for MLO). */
static struct ow_steer_bm_mlo_client *ow_steer_bm_mlo_client_by_assoc(
        ow_steer_bm_mlo_t *m,
        const struct osw_hwaddr *assoc)
{
    return ds_tree_find(&m->clients, assoc);
}

/* Resolve a per-link STA MAC to its owning tracked client. Some legacy event
 * paths (e.g. per-link activity on link-add) only carry a link address, not the
 * association address, so they can't use the ds_tree key directly. */
static struct ow_steer_bm_mlo_client *ow_steer_bm_mlo_client_by_link(
        ow_steer_bm_mlo_t *m,
        const struct osw_hwaddr *link_sta_addr)
{
    struct ow_steer_bm_mlo_client *c;
    ds_tree_foreach (&m->clients, c)
    {
        for (size_t i = 0; i < c->last_links.count; i++)
        {
            if (osw_hwaddr_is_equal(&c->last_links.links[i].remote_sta_addr, link_sta_addr)) return c;
        }
    }
    return NULL;
}

static void ow_steer_bm_mlo_send(ow_steer_bm_mlo_t *m)
{
    if (m->report == NULL) return;
    if (m->report->n_events == 0) return;
    if (m->topic == NULL)
    {
        LOGW(LOG_PREFIX_M(m, "dropping %zu events: no mqtt topic", (size_t)m->report->n_events));
        return;
    }

    const size_t size = sts__bs_mlo__bsreport_v2__get_packed_size(m->report);
    void *buf = MALLOC(size);
    const size_t len = sts__bs_mlo__bsreport_v2__pack(m->report, buf);
    LOGI(LOG_PREFIX_M(m, "sending report: events=%zu len=%zu topic=%s", (size_t)m->report->n_events, len, m->topic));
    qm_response_t res;
    const bool sent = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, m->topic, buf, (int)len, &res);
    WARN_ON(sent == false);
    FREE(buf);
}

static void ow_steer_bm_mlo_reset(ow_steer_bm_mlo_t *m)
{
    if (m->report == NULL) return;
    sts__bs_mlo__bsreport_v2__free_unpacked(m->report, NULL);
    m->report = NULL;
}

static Sts__BsMlo__BSReportV2 *ow_steer_bm_mlo_get_report(ow_steer_bm_mlo_t *m)
{
    if (m->report == NULL)
    {
        m->report = MALLOC(sizeof(*m->report));
        sts__bs_mlo__bsreport_v2__init(m->report);
        m->report->nodeid = STRDUP(m->node_id);
    }
    return m->report;
}

static void ow_steer_bm_mlo_append(ow_steer_bm_mlo_t *m, Sts__BsMlo__BSEvent *ev)
{
    if (m->topic == NULL)
    {
        sts__bs_mlo__bsevent__free_unpacked(ev, NULL);
        return;
    }
    Sts__BsMlo__BSReportV2 *r = ow_steer_bm_mlo_get_report(m);
    const size_t idx = r->n_events;
    r->n_events++;
    r->events = REALLOC(r->events, r->n_events * sizeof(*r->events));
    r->events[idx] = ev;

    if (r->n_events >= OW_STEER_BM_MLO_MAX_EVENTS)
    {
        LOGI(LOG_PREFIX_M(m, "event count %zu hit max, flushing early", (size_t)r->n_events));
        ow_steer_bm_mlo_send(m);
        ow_steer_bm_mlo_reset(m);
    }
}

static Sts__BsMlo__BSClient *ow_steer_bm_mlo_client_proto_new(const struct osw_hwaddr *assoc)
{
    Sts__BsMlo__BSClient *c = MALLOC(sizeof(*c));
    sts__bs_mlo__bsclient__init(c);
    c->assoc_address = ow_steer_bm_mlo_mac_dup(assoc);
    return c;
}

static Sts__BsMlo__BSEvent *ow_steer_bm_mlo_bsevent(Sts__BsMlo__BSClient *client)
{
    Sts__BsMlo__BSEvent *ev = MALLOC(sizeof(*ev));
    sts__bs_mlo__bsevent__init(ev);
    ev->timestamp_utc_ms = OSW_TIME_TO_MS(osw_time_wall_clk());
    ev->client = client;
    return ev;
}

static bool ow_steer_bm_mlo_steering_type(ow_steer_bm_mlo_steering_type_e t, Sts__BsMlo__SteeringActionType *out)
{
    switch (t)
    {
        case OW_STEER_BM_MLO_STEERING_CLIENT_BS_BTM:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_BS_BTM;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_STICKY_BTM:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_STICKY_BTM;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_BTM:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_BTM;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_BTM_STATUS:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_BTM_STATUS;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_BS_BTM_RETRY:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_BS_BTM_RETRY;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_STICKY_BTM_RETRY:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_STICKY_BTM_RETRY;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_BTM_RETRY:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_BTM_RETRY;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_KICKED:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_KICKED;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_BS_KICK:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_BS_KICK;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_STICKY_KICK:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_STICKY_KICK;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_SPECULATIVE_KICK:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_SPECULATIVE_KICK;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_DIRECTED_KICK:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_DIRECTED_KICK;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_GHOST_DEVICE_KICK:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_GHOST_DEVICE_KICK;
            return true;
        case OW_STEER_BM_MLO_STEERING_BAND_STEERING_ATTEMPT:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__BAND_STEERING_ATTEMPT;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_STEERING_ATTEMPT:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_STEERING_ATTEMPT;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_STEERING_STARTED:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_STEERING_STARTED;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_STEERING_DISABLED:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_STEERING_DISABLED;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_STEERING_EXPIRED:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_STEERING_EXPIRED;
            return true;
        case OW_STEER_BM_MLO_STEERING_CLIENT_STEERING_FAILED:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__CLIENT_STEERING_FAILED;
            return true;
        case OW_STEER_BM_MLO_STEERING_AUTH_BLOCK:
            *out = STS__BS_MLO__STEERING_ACTION_TYPE__AUTH_BLOCK;
            return true;
    }
    return false;
}

static bool ow_steer_bm_mlo_misc_type(ow_steer_bm_mlo_misc_type_e t, Sts__BsMlo__MiscSubType *out)
{
    switch (t)
    {
        case OW_STEER_BM_MLO_MISC_BACKOFF:
            *out = STS__BS_MLO__MISC_SUB_TYPE__BACKOFF;
            return true;
        case OW_STEER_BM_MLO_MISC_ACTIVITY:
            *out = STS__BS_MLO__MISC_SUB_TYPE__ACTIVITY;
            return true;
    }
    return false;
}

static bool ow_steer_bm_mlo_assoc_req_6g_capable(const struct osw_assoc_req_info *info)
{
    for (unsigned int i = 0; i < info->op_class_cnt; i++)
    {
        if (osw_op_class_to_band(info->op_class_list[i]) == OSW_BAND_6GHZ) return true;
    }
    return false;
}

static bool ow_steer_bm_mlo_wifi_standard(const struct osw_assoc_req_info *info, uint32_t *out)
{
    if (info->eht_op_chwidth_present || info->eht_cap_mcs > 0 || info->eht_cap_nss > 0)
    {
        *out = 7;
        return true;
    }
    if (info->he_caps_present)
    {
        *out = 6;
        return true;
    }
    if (info->vht_caps_present)
    {
        *out = 5;
        return true;
    }
    if (info->ht_caps_present)
    {
        *out = 4;
        return true;
    }
    return false;
}

static Sts__BsMlo__ClientCapabilities *ow_steer_bm_mlo_build_caps(const osw_sta_assoc_entry_t *entry)
{
    const void *ies = osw_sta_assoc_entry_get_assoc_ies_data(entry);
    const size_t ies_len = osw_sta_assoc_entry_get_assoc_ies_len(entry);
    if (ies == NULL || ies_len == 0) return NULL;

    struct osw_assoc_req_info info;
    MEMZERO(info);
    if (osw_parse_assoc_req_ies(ies, ies_len, &info) == false) return NULL;

    const osw_sta_assoc_links_t *active = osw_sta_assoc_entry_get_active_links(entry);
    if (active == NULL || active->count == 0) return NULL;

    const uint32_t max_chwidth = (uint32_t)osw_assoc_req_to_max_chwidth(&info);
    const uint32_t max_streams = osw_assoc_req_to_max_nss(&info);
    const uint32_t max_mcs = osw_assoc_req_to_max_mcs(&info);
    const bool max_txpower_valid = info.max_tx_power > 0;
    const uint32_t max_txpower = max_txpower_valid ? (uint32_t)info.max_tx_power : 0;
    uint32_t wifi_std = 0;
    const bool wifi_std_valid = ow_steer_bm_mlo_wifi_standard(&info, &wifi_std);

    Sts__BsMlo__ClientCapabilities *cc = MALLOC(sizeof(*cc));
    sts__bs_mlo__client_capabilities__init(cc);
    cc->has_is_btm_supported = true;
    cc->is_btm_supported = info.wnm_bss_trans;
    cc->has_band_cap_6g = true;
    cc->band_cap_6g = ow_steer_bm_mlo_assoc_req_6g_capable(&info);
    cc->has_assoc_ies = true;
    cc->assoc_ies.len = ies_len;
    cc->assoc_ies.data = MALLOC(ies_len);
    memcpy(cc->assoc_ies.data, ies, ies_len);

    Sts__BsMlo__LinkCapability **lcs = MALLOC(active->count * sizeof(*lcs));
    size_t n_out = 0;
    for (size_t i = 0; i < active->count; i++)
    {
        Sts__BsMlo__LinkInfo *li = ow_steer_bm_mlo_build_link_info(
                &active->links[i].local_sta_addr,
                &active->links[i].remote_sta_addr,
                NULL);
        if (li == NULL) continue;

        Sts__BsMlo__LinkCapability *lc = MALLOC(sizeof(*lc));
        sts__bs_mlo__link_capability__init(lc);
        lc->link = li;
        lc->has_max_chwidth = true;
        lc->max_chwidth = max_chwidth;
        lc->has_max_streams = true;
        lc->max_streams = max_streams;
        lc->has_max_mcs = true;
        lc->max_mcs = max_mcs;
        if (max_txpower_valid)
        {
            lc->has_max_txpower = true;
            lc->max_txpower = max_txpower;
        }
        if (wifi_std_valid)
        {
            lc->has_wifi_standard = true;
            lc->wifi_standard = wifi_std;
        }
        lcs[n_out++] = lc;
    }
    if (n_out > 0)
    {
        cc->links = lcs;
        cc->n_links = n_out;
    }
    else
    {
        FREE(lcs);
    }

    return cc;
}

static void ow_steer_bm_mlo_emit_connect(
        ow_steer_bm_mlo_t *m,
        struct ow_steer_bm_mlo_client *client,
        bool is_mlo,
        const osw_sta_assoc_links_t *links)
{
    if (WARN_ON(client == NULL)) return;
    size_t n_out = 0;
    Sts__BsMlo__LinkInfo **arr = ow_steer_bm_mlo_build_links(links, &n_out);
    if (arr == NULL) return;

    Sts__BsMlo__ConnectEvent *ce = MALLOC(sizeof(*ce));
    sts__bs_mlo__connect_event__init(ce);
    ce->assoc_links = arr;
    ce->n_assoc_links = n_out;
    ce->has_is_mlo = true;
    ce->is_mlo = is_mlo;

    Sts__BsMlo__BSClient *c = ow_steer_bm_mlo_client_proto_new(&client->addr);
    c->connect = ce;
    ow_steer_bm_mlo_append(m, ow_steer_bm_mlo_bsevent(c));
}

static void ow_steer_bm_mlo_emit_disconnect(
        ow_steer_bm_mlo_t *m,
        struct ow_steer_bm_mlo_client *client,
        const osw_sta_assoc_links_t *links,
        bool is_mlo)
{
    if (WARN_ON(client == NULL)) return;
    size_t n_out = 0;
    Sts__BsMlo__LinkInfo **arr = ow_steer_bm_mlo_build_links(links, &n_out);
    if (arr == NULL) return;

    Sts__BsMlo__DisconnectEvent *de = MALLOC(sizeof(*de));
    sts__bs_mlo__disconnect_event__init(de);
    de->last_links = arr;
    de->n_last_links = n_out;
    de->has_is_mlo = true;
    de->is_mlo = is_mlo;

    Sts__BsMlo__BSClient *c = ow_steer_bm_mlo_client_proto_new(&client->addr);
    c->disconnect = de;
    ow_steer_bm_mlo_append(m, ow_steer_bm_mlo_bsevent(c));
}

static void ow_steer_bm_mlo_emit_caps(
        ow_steer_bm_mlo_t *m,
        struct ow_steer_bm_mlo_client *client,
        const osw_sta_assoc_entry_t *entry)
{
    if (WARN_ON(client == NULL)) return;
    Sts__BsMlo__ClientCapabilities *cc = ow_steer_bm_mlo_build_caps(entry);
    if (cc == NULL) return;

    Sts__BsMlo__BSClient *c = ow_steer_bm_mlo_client_proto_new(&client->addr);
    c->capabilities = cc;
    ow_steer_bm_mlo_append(m, ow_steer_bm_mlo_bsevent(c));
}

static struct ow_steer_bm_mlo_client *ow_steer_bm_mlo_client_get_or_create(
        ow_steer_bm_mlo_t *m,
        const struct osw_hwaddr *addr)
{
    struct ow_steer_bm_mlo_client *client = ds_tree_find(&m->clients, addr);
    if (client != NULL) return client;

    client = CALLOC(1, sizeof(*client));
    client->m = m;
    client->addr = *addr;
    ds_tree_init(&client->link_signals, (ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_steer_bm_mlo_link_signal, node);
    ds_tree_insert(&m->clients, client, &client->addr);
    LOGD(LOG_PREFIX_M(m, "client: " OSW_HWADDR_FMT ": discovered", OSW_HWADDR_ARG(addr)));
    return client;
}

static void ow_steer_bm_mlo_idle_notify(void *priv, bool idle)
{
    struct ow_steer_bm_mlo_client *client = priv;
    ow_steer_bm_mlo_report_misc(client->m, &client->addr, OW_STEER_BM_MLO_MISC_ACTIVITY);
}

static void ow_steer_bm_mlo_idle_obs_alloc(struct ow_steer_bm_mlo_client *client)
{
    if (client->idle_obs != NULL) return;
    osw_sta_idle_params_t *p = osw_sta_idle_params_alloc();
    osw_sta_idle_params_set_sta_addr(p, &client->addr);
    osw_sta_idle_params_set_bytes_per_sec(p, OW_STEER_BM_MLO_IDLE_BYTES_PER_SEC);
    osw_sta_idle_params_set_notify_fn(p, ow_steer_bm_mlo_idle_notify, client);
    client->idle_obs = osw_sta_idle_observer_alloc(client->m->sta_idle, p);
}

static void ow_steer_bm_mlo_idle_obs_drop(struct ow_steer_bm_mlo_client *client)
{
    if (client->idle_obs == NULL) return;
    osw_sta_idle_observer_drop(client->idle_obs);
    client->idle_obs = NULL;
}

static struct ow_steer_bm_mlo_client *ow_steer_bm_mlo_client_free(struct ow_steer_bm_mlo_client *client)
{
    if (client == NULL) return NULL;
    if (client->connected) return client;
    if (client->bss_tracked) return client;
    ow_steer_bm_mlo_idle_obs_drop(client);
    ow_steer_bm_mlo_link_signal_free_all(&client->link_signals);
    ow_steer_bm_mlo_t *m = client->m;
    ds_tree_remove(&m->clients, client);
    LOGD(LOG_PREFIX_M(m, "client: " OSW_HWADDR_FMT ": freed", OSW_HWADDR_ARG(&client->addr)));
    FREE(client);
    return NULL;
}

static struct ow_steer_bm_mlo_client *ow_steer_bm_mlo_client_set_connected(
        struct ow_steer_bm_mlo_client *client,
        bool connected)
{
    if (client == NULL) return NULL;
    if (connected && !client->connected)
    {
        struct ow_steer_bm_mlo_link_signal *ls;
        ds_tree_foreach (&client->link_signals, ls)
        {
            osw_sta_snr_observer_reset_log(ls->data_obs);
            osw_sta_snr_observer_reset_log(ls->probe_obs);
            for (size_t i = 0; i < ls->n_saved_logs; i++)
                FREE(ls->saved_logs[i].samples);
            FREE(ls->saved_logs);
            ls->saved_logs = NULL;
            ls->n_saved_logs = 0;
        }
        client->observation_start_nsec = osw_time_mono_clk();
        ow_steer_bm_mlo_idle_obs_alloc(client);
    }
    if (!connected && client->connected) ow_steer_bm_mlo_idle_obs_drop(client);
    client->connected = connected;
    return ow_steer_bm_mlo_client_free(client);
}

static struct ow_steer_bm_mlo_client *ow_steer_bm_mlo_client_set_bss_tracked(
        struct ow_steer_bm_mlo_client *client,
        bool tracked)
{
    if (client == NULL) return NULL;
    if (client->bss_tracked == tracked) return client;
    LOGD(LOG_PREFIX_M(
            client->m,
            "client: " OSW_HWADDR_FMT ": bss %s",
            OSW_HWADDR_ARG(&client->addr),
            tracked ? "tracked" : "untracked"));
    client->bss_tracked = tracked;
    return ow_steer_bm_mlo_client_free(client);
}

static void ow_steer_bm_mlo_global_assoc_cb(void *priv, const osw_sta_assoc_entry_t *entry, osw_sta_assoc_event_e ev)
{
    ow_steer_bm_mlo_t *m = priv;
    if (WARN_ON(m == NULL)) return;
    const struct osw_hwaddr *addr = osw_sta_assoc_entry_get_addr(entry);
    if (addr == NULL) return;

    switch (ev)
    {
        case OSW_STA_ASSOC_CONNECTED: {
            struct ow_steer_bm_mlo_client *client = ow_steer_bm_mlo_client_get_or_create(m, addr);
            const osw_sta_assoc_links_t *links = osw_sta_assoc_entry_get_active_links(entry);
            client = ow_steer_bm_mlo_client_set_connected(client, true);
            client->is_mlo = osw_sta_assoc_entry_is_mlo(entry);
            client->last_links = *links;
            for (size_t i = 0; i < links->count; i++)
            {
                struct ow_steer_bm_mlo_link_signal *ls = ow_steer_bm_mlo_link_signal_get(
                        client,
                        &links->links[i].local_sta_addr,
                        &links->links[i].remote_sta_addr);
                ow_steer_bm_mlo_link_signal_ensure_obs(ls, m, &client->addr);
            }
            ow_steer_bm_mlo_emit_connect(m, client, client->is_mlo, links);
            ow_steer_bm_mlo_emit_caps(m, client, entry);
            break;
        }
        case OSW_STA_ASSOC_RECONNECTED: {
            struct ow_steer_bm_mlo_client *client = ow_steer_bm_mlo_client_get_or_create(m, addr);
            const osw_sta_assoc_links_t *active = osw_sta_assoc_entry_get_active_links(entry);
            const osw_sta_assoc_links_t *stale = osw_sta_assoc_entry_get_stale_links(entry);
            const bool is_mlo = osw_sta_assoc_entry_is_mlo(entry);
            if (stale->count > 0) ow_steer_bm_mlo_emit_disconnect(m, client, stale, client->is_mlo);
            if (active->count > 0)
            {
                client = ow_steer_bm_mlo_client_set_connected(client, true);
                client->is_mlo = is_mlo;
                client->last_links = *active;
                for (size_t i = 0; i < active->count; i++)
                {
                    struct ow_steer_bm_mlo_link_signal *ls = ow_steer_bm_mlo_link_signal_get(
                            client,
                            &active->links[i].local_sta_addr,
                            &active->links[i].remote_sta_addr);
                    ow_steer_bm_mlo_link_signal_ensure_obs(ls, m, &client->addr);
                }
                ow_steer_bm_mlo_emit_connect(m, client, is_mlo, active);
                ow_steer_bm_mlo_emit_caps(m, client, entry);
            }
            break;
        }
        case OSW_STA_ASSOC_DISCONNECTED: {
            struct ow_steer_bm_mlo_client *client = ds_tree_find(&m->clients, addr);
            if (client == NULL) break;
            if (client->connected == false) break;
            ow_steer_bm_mlo_emit_disconnect(m, client, &client->last_links, client->is_mlo);
            client = ow_steer_bm_mlo_client_set_connected(client, false);
            break;
        }
        case OSW_STA_ASSOC_UNDEFINED:
            break;
    }
}

void ow_steer_bm_mlo_client_track(ow_steer_bm_mlo_t *m, const struct osw_hwaddr *assoc)
{
    if (m == NULL || assoc == NULL) return;
    struct ow_steer_bm_mlo_client *client = ow_steer_bm_mlo_client_get_or_create(m, assoc);
    client = ow_steer_bm_mlo_client_set_bss_tracked(client, true);
}

void ow_steer_bm_mlo_client_untrack(ow_steer_bm_mlo_t *m, const struct osw_hwaddr *assoc)
{
    if (m == NULL || assoc == NULL) return;
    struct ow_steer_bm_mlo_client *client = ds_tree_find(&m->clients, assoc);
    client = ow_steer_bm_mlo_client_set_bss_tracked(client, false);
}

void ow_steer_bm_mlo_report_steering(
        ow_steer_bm_mlo_t *m,
        const struct osw_hwaddr *assoc,
        const ow_steer_bm_mlo_steering_params_t *params)
{
    if (m == NULL || assoc == NULL || params == NULL) return;
    struct ow_steer_bm_mlo_client *client = ow_steer_bm_mlo_client_by_assoc(m, assoc);
    if (client == NULL) return;
    if (client->bss_tracked == false) return;

    bool is_mlo = false;
    const osw_sta_assoc_links_t *links = ow_steer_bm_mlo_client_live_links(client, &is_mlo);
    if (links == NULL) return;

    size_t n_out = 0;
    Sts__BsMlo__LinkInfo **arr = ow_steer_bm_mlo_build_links(links, &n_out);
    if (arr == NULL) return;

    Sts__BsMlo__SteeringActionEvent *se = MALLOC(sizeof(*se));
    sts__bs_mlo__steering_action_event__init(se);
    se->assoc_links = arr;
    se->n_assoc_links = n_out;
    se->has_is_mlo = true;
    se->is_mlo = is_mlo;

    Sts__BsMlo__SteeringActionType sa;
    if (ow_steer_bm_mlo_steering_type(params->type, &sa))
    {
        se->has_type = true;
        se->type = sa;
    }
    if (params->allow_acl_valid)
    {
        se->has_allow_acl = true;
        se->allow_acl = params->allow_acl;
    }
    if (params->btm_response_code_valid)
    {
        se->has_btm_response_code = true;
        se->btm_response_code = params->btm_response_code;
    }

    if (params->n_target_bssids > 0 && params->target_bssids != NULL)
    {
        se->n_target_bssids = params->n_target_bssids;
        se->target_bssids = MALLOC(params->n_target_bssids * sizeof(*se->target_bssids));
        for (size_t i = 0; i < params->n_target_bssids; i++)
            se->target_bssids[i] = ow_steer_bm_mlo_mac_dup(&params->target_bssids[i]);
    }

    Sts__BsMlo__BSClient *c = ow_steer_bm_mlo_client_proto_new(&client->addr);
    c->steering_action = se;
    ow_steer_bm_mlo_append(m, ow_steer_bm_mlo_bsevent(c));
}

void ow_steer_bm_mlo_report_misc(
        ow_steer_bm_mlo_t *m,
        const struct osw_hwaddr *sta_addr,
        ow_steer_bm_mlo_misc_type_e type)
{
    if (m == NULL || sta_addr == NULL) return;
    /* Misc events arrive keyed either by the association address (backoff,
     * traffic activity) or by a per-link STA MAC (activity on link-add), so try
     * the association key first, then resolve a per-link address. */
    struct ow_steer_bm_mlo_client *client = ow_steer_bm_mlo_client_by_assoc(m, sta_addr);
    if (client == NULL) client = ow_steer_bm_mlo_client_by_link(m, sta_addr);
    if (client == NULL) return;
    if (client->bss_tracked == false) return;

    bool is_mlo = false;
    const osw_sta_assoc_links_t *links = ow_steer_bm_mlo_client_live_links(client, &is_mlo);
    if (links == NULL) return;

    size_t n_out = 0;
    Sts__BsMlo__LinkInfo **arr = ow_steer_bm_mlo_build_links(links, &n_out);
    if (arr == NULL) return;

    Sts__BsMlo__MiscEvent *me = MALLOC(sizeof(*me));
    sts__bs_mlo__misc_event__init(me);
    me->assoc_links = arr;
    me->n_assoc_links = n_out;
    me->has_is_mlo = true;
    me->is_mlo = is_mlo;

    Sts__BsMlo__MiscSubType ms;
    if (ow_steer_bm_mlo_misc_type(type, &ms))
    {
        me->has_type = true;
        me->type = ms;
    }

    Sts__BsMlo__BSClient *c = ow_steer_bm_mlo_client_proto_new(&client->addr);
    c->misc = me;
    ow_steer_bm_mlo_append(m, ow_steer_bm_mlo_bsevent(c));
}

static size_t ow_steer_bm_mlo_link_signal_sample_count(
        const struct ow_steer_bm_mlo_link_signal *ls,
        Sts__BsMlo__SignalInfoSource source)
{
    if (source == STS__BS_MLO__SIGNAL_INFO_SOURCE__DATA_PACKET) return osw_sta_snr_observer_get_log_size(ls->data_obs);
    return osw_sta_snr_observer_get_log_size(ls->probe_obs);
}

static const uint8_t *ow_steer_bm_mlo_link_signal_samples(
        const struct ow_steer_bm_mlo_link_signal *ls,
        Sts__BsMlo__SignalInfoSource source)
{
    if (source == STS__BS_MLO__SIGNAL_INFO_SOURCE__DATA_PACKET) return osw_sta_snr_observer_get_log(ls->data_obs);
    return osw_sta_snr_observer_get_log(ls->probe_obs);
}

static void ow_steer_bm_mlo_emit_signal_report_source(
        ow_steer_bm_mlo_t *m,
        struct ow_steer_bm_mlo_client *client,
        Sts__BsMlo__SignalInfoSource source)
{
    struct ow_steer_bm_mlo_link_signal *ls;
    size_t n_links = 0;

    ds_tree_foreach (&client->link_signals, ls)
    {
        if (ow_steer_bm_mlo_link_signal_sample_count(ls, source) > 0) n_links++;
    }
    if (n_links == 0) return;

    Sts__BsMlo__LinkSignalReport **lsrs = MALLOC(n_links * sizeof(*lsrs));
    size_t idx = 0;
    ds_tree_foreach (&client->link_signals, ls)
    {
        const uint8_t *samples = ow_steer_bm_mlo_link_signal_samples(ls, source);
        const size_t n = ow_steer_bm_mlo_link_signal_sample_count(ls, source);
        if (n == 0) continue;

        Sts__BsMlo__SignalInfo *si = ow_steer_bm_mlo_build_signal_info(samples, n);
        if (si == NULL) continue;

        Sts__BsMlo__LinkInfo *li = ow_steer_bm_mlo_build_link_info(&ls->bssid, &ls->sta_mac, NULL);
        if (li == NULL)
        {
            FREE(si);
            continue;
        }

        Sts__BsMlo__LinkSignalReport *lsr = MALLOC(sizeof(*lsr));
        sts__bs_mlo__link_signal_report__init(lsr);
        lsr->link = li;
        lsr->signal = si;
        lsrs[idx++] = lsr;
    }

    ds_tree_foreach (&client->link_signals, ls)
    {
        for (size_t i = 0; i < ls->n_saved_logs; i++)
        {
            struct ow_steer_bm_mlo_saved_log *sl = &ls->saved_logs[i];
            if (sl->source != source) continue;
            if (sl->count == 0) continue;

            Sts__BsMlo__SignalInfo *si = ow_steer_bm_mlo_build_signal_info(sl->samples, sl->count);
            if (si == NULL) continue;

            Sts__BsMlo__LinkInfo *li = ow_steer_bm_mlo_build_link_info(&ls->bssid, &ls->sta_mac, &sl->channel);
            if (li == NULL)
            {
                FREE(si);
                continue;
            }

            Sts__BsMlo__LinkSignalReport *lsr = MALLOC(sizeof(*lsr));
            sts__bs_mlo__link_signal_report__init(lsr);
            lsr->link = li;
            lsr->signal = si;

            n_links++;
            lsrs = REALLOC(lsrs, n_links * sizeof(*lsrs));
            lsrs[idx++] = lsr;
        }
    }

    if (idx == 0)
    {
        FREE(lsrs);
        return;
    }

    Sts__BsMlo__SignalReportEvent *sre = MALLOC(sizeof(*sre));
    sts__bs_mlo__signal_report_event__init(sre);
    sre->source = source;
    sre->links = lsrs;
    sre->n_links = idx;
    const uint64_t now_nsec = osw_time_mono_clk();
    const uint64_t elapsed_nsec = now_nsec - client->observation_start_nsec;
    sre->has_period_ms = true;
    sre->period_ms = (uint32_t)OSW_TIME_TO_MS(elapsed_nsec);

    Sts__BsMlo__BSClient *c = ow_steer_bm_mlo_client_proto_new(&client->addr);
    c->signal_report = sre;
    ow_steer_bm_mlo_append(m, ow_steer_bm_mlo_bsevent(c));
}

static void ow_steer_bm_mlo_emit_signal_reports(ow_steer_bm_mlo_t *m)
{
    struct ow_steer_bm_mlo_client *client;
    ds_tree_foreach (&m->clients, client)
    {
        ow_steer_bm_mlo_emit_signal_report_source(m, client, STS__BS_MLO__SIGNAL_INFO_SOURCE__DATA_PACKET);
        ow_steer_bm_mlo_emit_signal_report_source(m, client, STS__BS_MLO__SIGNAL_INFO_SOURCE__PROBE_REQUEST);
    }
}

static void ow_steer_bm_mlo_reset_signal_bufs(ow_steer_bm_mlo_t *m)
{
    struct ow_steer_bm_mlo_client *client;
    ds_tree_foreach (&m->clients, client)
    {
        struct ow_steer_bm_mlo_link_signal *ls;
        ds_tree_foreach (&client->link_signals, ls)
        {
            osw_sta_snr_observer_reset_log(ls->data_obs);
            osw_sta_snr_observer_reset_log(ls->probe_obs);
            for (size_t i = 0; i < ls->n_saved_logs; i++)
                FREE(ls->saved_logs[i].samples);
            FREE(ls->saved_logs);
            ls->saved_logs = NULL;
            ls->n_saved_logs = 0;
        }
    }
}

static void ow_steer_bm_mlo_reset_observation_starts(ow_steer_bm_mlo_t *m)
{
    const uint64_t now = osw_time_mono_clk();
    struct ow_steer_bm_mlo_client *client;
    ds_tree_foreach (&m->clients, client)
    {
        if (client->connected) client->observation_start_nsec = now;
    }
}

static void ow_steer_bm_mlo_flush(ow_steer_bm_mlo_t *m)
{
    ow_steer_bm_mlo_emit_signal_reports(m);
    ow_steer_bm_mlo_send(m);
    ow_steer_bm_mlo_reset(m);
    ow_steer_bm_mlo_reset_signal_bufs(m);
    ow_steer_bm_mlo_reset_observation_starts(m);
}

static void ow_steer_bm_mlo_recalc(ow_steer_bm_mlo_t *m)
{
    const bool enabled = (m->topic != NULL) && (m->report_interval_nsec > 0);
    if (enabled == false)
    {
        osw_timer_disarm(&m->report_timer);
        ow_steer_bm_mlo_reset(m);
        ow_steer_bm_mlo_reset_signal_bufs(m);
        return;
    }
    if (osw_timer_is_armed(&m->report_timer) == false)
        osw_timer_arm_at_nsec(&m->report_timer, osw_time_mono_clk() + m->report_interval_nsec);
}

static void ow_steer_bm_mlo_report_timer_cb(struct osw_timer *timer)
{
    ow_steer_bm_mlo_t *m = container_of(timer, struct ow_steer_bm_mlo, report_timer);
    ow_steer_bm_mlo_flush(m);
    if (WARN_ON(m->report_interval_nsec == 0)) return;
    osw_timer_arm_at_nsec(&m->report_timer, timer->at_nsec + m->report_interval_nsec);
}

void ow_steer_bm_mlo_set_topic(ow_steer_bm_mlo_t *m, const char *topic)
{
    if (m == NULL) return;
    const char *norm = (topic != NULL && *topic != '\0') ? topic : NULL;
    if (m->topic == NULL && norm == NULL) return;
    if (m->topic != NULL && norm != NULL && strcmp(m->topic, norm) == 0) return;
    LOGI(LOG_PREFIX_M(m, "mqtt topic: '%s' -> '%s'", m->topic ?: "(none)", norm ?: "(none)"));
    FREE(m->topic);
    m->topic = norm ? STRDUP(norm) : NULL;
    ow_steer_bm_mlo_recalc(m);
}

static char *ow_steer_bm_mlo_get_node_id(void)
{
    char *buf = MALLOC(TARGET_ID_SZ);
    if (osp_unit_id_get(buf, TARGET_ID_SZ) == false)
    {
        FREE(buf);
        return NULL;
    }
    return buf;
}

static ow_steer_bm_mlo_t *ow_steer_bm_mlo_new(void)
{
    char *node_id = ow_steer_bm_mlo_get_node_id();
    if (WARN_ON(node_id == NULL)) return NULL;

    ow_steer_bm_mlo_t *m = CALLOC(1, sizeof(*m));
    m->node_id = node_id;
    m->report_interval_nsec = OW_STEER_BM_MLO_DEFAULT_INTERVAL_NSEC;
    ds_tree_init(&m->clients, (ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_steer_bm_mlo_client, node);
    osw_timer_init(&m->report_timer, ow_steer_bm_mlo_report_timer_cb);
    m->sta_assoc = OSW_MODULE_LOAD(osw_sta_assoc);
    m->sta_idle = OSW_MODULE_LOAD(osw_sta_idle);
    m->sta_snr = osw_sta_snr_load();

    osw_sta_assoc_observer_params_t *p = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(p, ow_steer_bm_mlo_global_assoc_cb, m);
    m->global_assoc_obs = osw_sta_assoc_observer_alloc(m->sta_assoc, p);

    LOGI(LOG_PREFIX_M(m, "allocated"));
    return m;
}

OSW_MODULE(ow_steer_bm_mlo)
{
    OSW_MODULE_LOAD(osw_state);
    OSW_MODULE_LOAD(osw_sta_assoc);
    OSW_MODULE_LOAD(osw_sta_idle);
    OSW_MODULE_LOAD(osw_sta_snr);
    return ow_steer_bm_mlo_new();
}
