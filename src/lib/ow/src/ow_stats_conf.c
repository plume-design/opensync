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

#include <math.h>
#include <inttypes.h>
#include <memutil.h>
#include <const.h>
#include <log.h>
#include <util.h>
#include <os.h>
#include <qm_conn.h>
#include <os_time.h>
#include <stddef.h>
#include <ds_dlist.h>
#include <ds_tree.h>
#include <osw_ut.h>
#include <osw_types.h>
#include <osw_state.h>
#include <osw_stats.h>
#include <osw_stats_subscriber.h>
#include <osw_stats_defs.h>
#include <osw_module.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <ow_stats_conf.h>
#include <dpp_types.h>
#include <dpp_survey.h>
#include <dppline.h>

#if 0
_uuid              : b5e0~8b41
_version           : e9f6~f281
channel_list       : ["set",[1,6,11]]
radio_type         : 2.4G
report_type        : ["set",[]]
reporting_count    : 0
reporting_interval : 120
sampling_interval  : 10
stats_type         : survey
survey_interval_ms : 50
survey_type        : off-chan
threshold          : ["map",[["max_delay",600],["util",10]]]
#endif

#ifndef RADIO_TYPE_STR_6G
#define RADIO_TYPE_6G RADIO_TYPE_NONE
#endif

/*
 * ow_stats_conf
 *
 * This is intended to simplify integration to some
 * configuration systems like OVSDB (Wifi_Stats_Config).
 * It bakes in some of the pecularities of how the
 * polling/sampling/reporting times are set.
 *
 * Ideally this should be renamed to ow_stats_pb (protobuf)
 * and the publishing should be abstracted, so that
 * ow_stats_pb_qm could be used to publish the generated
 * protobuf over QM (unix socket). The alternate trasnport
 * would be ow_stats_pb_rbus for example.
 */

struct ow_stats_conf {
    struct ds_tree entries; /* ow_stats_conf_entry */
    struct ds_tree bands;
    struct ds_tree freqs_phy;
    struct ds_tree freqs_vif;
    struct osw_state_observer state_obs;
    struct osw_timer work;
};

struct ow_stats_conf_band {
    struct ds_tree_node node;
    char *phy_name;
    enum ow_stats_conf_radio_type type;
};

struct ow_stats_conf_freq_phy {
    struct ds_tree_node node;
    struct ds_tree vifs;
    char *phy_name;
};

struct ow_stats_conf_freq_vif {
    struct ds_tree_node phy_node; /* ow_stats_conf_freq_phy.vifs */
    struct ds_tree_node conf_node; /* ow_stats_conf.freqs_vif */
    struct ow_stats_conf_freq *phy_freq;
    char *vif_name;
    uint32_t freq_mhz;
};

struct ow_stats_conf_sta_id {
    struct osw_ifname vif_name;
    struct osw_hwaddr addr;
};

struct ow_stats_conf_sta {
    struct ds_tree_node node;
    struct ow_stats_conf_sta_id id;
    struct osw_ssid ssid;
    uint32_t num_connects;
    uint32_t num_disconnects;
    double connected_at;
    double disconnected_at;
    bool is_connected;
    /* FIXME: needs a way to ageout these entries - can't
     * remove them when client disconnects if there's a
     * bucket to be reported for it.
     */
};

struct ow_stats_conf_entry_params {
    int *channels;
    size_t n_channels;
    enum ow_stats_conf_radio_type radio_type;
    enum ow_stats_conf_scan_type scan_type;
    enum ow_stats_conf_stats_type stats_type;
    double sample_seconds;
    double report_seconds;
    bool valid;
};

struct ow_stats_conf_entry {
    struct ds_tree_node node; /* keyed by `id` */
    struct ds_dlist surveys; /* dpp_survey_record_t */
    struct ds_dlist neighbors; /* dpp_neighbor_record_list_t */
    struct ds_dlist clients; /* dpp_client_record_t */
    struct ds_tree sta_tree; /* ow_stats_conf_sta */
    struct osw_stats_subscriber *sub;
    struct osw_state_observer obs;
    struct ow_stats_conf *conf;
    struct ow_stats_conf_entry_params params;
    struct ow_stats_conf_entry_params params_next;
    double report_at;
    double last_sub_reported_at;
    char *id;
    int underrun;
};

static radio_scan_type_t
ow_stats_conf_scan_type_to_dpp(const enum ow_stats_conf_scan_type t)
{
    switch (t) {
        case OW_STATS_CONF_SCAN_TYPE_UNSPEC: return RADIO_SCAN_TYPE_NONE;
        case OW_STATS_CONF_SCAN_TYPE_ON_CHAN: return RADIO_SCAN_TYPE_ONCHAN;
        case OW_STATS_CONF_SCAN_TYPE_OFF_CHAN: return RADIO_SCAN_TYPE_OFFCHAN;
        case OW_STATS_CONF_SCAN_TYPE_FULL: return RADIO_SCAN_TYPE_FULL;
    }
    return RADIO_SCAN_TYPE_NONE;
}

static radio_type_t
ow_stats_conf_radio_type_to_dpp(const enum ow_stats_conf_radio_type t)
{
    switch (t) {
        case OW_STATS_CONF_RADIO_TYPE_UNSPEC: return RADIO_TYPE_NONE;
        case OW_STATS_CONF_RADIO_TYPE_2G: return RADIO_TYPE_2G;
        case OW_STATS_CONF_RADIO_TYPE_5G: return RADIO_TYPE_5G;
        case OW_STATS_CONF_RADIO_TYPE_5GL: return RADIO_TYPE_5GL;
        case OW_STATS_CONF_RADIO_TYPE_5GU: return RADIO_TYPE_5GU;
        case OW_STATS_CONF_RADIO_TYPE_6G: return RADIO_TYPE_6G;
    }
    return RADIO_TYPE_NONE;
}

static enum ow_stats_conf_radio_type
ow_stats_conf_dpp_to_radio_type(const radio_type_t t)
{
    switch (t) {
        case RADIO_TYPE_NONE: return OW_STATS_CONF_RADIO_TYPE_UNSPEC;
        case RADIO_TYPE_2G: return OW_STATS_CONF_RADIO_TYPE_2G;
        case RADIO_TYPE_5G: return OW_STATS_CONF_RADIO_TYPE_5G;
        case RADIO_TYPE_5GL: return OW_STATS_CONF_RADIO_TYPE_5GL;
        case RADIO_TYPE_5GU: return OW_STATS_CONF_RADIO_TYPE_5GU;
        case RADIO_TYPE_6G: return OW_STATS_CONF_RADIO_TYPE_6G;
    }
    return OW_STATS_CONF_RADIO_TYPE_UNSPEC;
}

static radio_chanwidth_t
ow_stats_conf_width_to_dpp(const uint32_t mhz)
{
    switch (mhz) {
        case 20: return RADIO_CHAN_WIDTH_20MHZ;
        case 40: return RADIO_CHAN_WIDTH_40MHZ;
        case 80: return RADIO_CHAN_WIDTH_80MHZ;
        case 160: return RADIO_CHAN_WIDTH_160MHZ;
        case 8080: return RADIO_CHAN_WIDTH_80_PLUS_80MHZ;
    }
    return RADIO_CHAN_WIDTH_UNKNOWN;
}

static const char *
ow_stats_conf_radio_type_to_str(const enum ow_stats_conf_radio_type t)
{
    switch (t) {
        case OW_STATS_CONF_RADIO_TYPE_UNSPEC: return "unspec";
        case OW_STATS_CONF_RADIO_TYPE_2G: return  "2g";
        case OW_STATS_CONF_RADIO_TYPE_5G: return "5g";
        case OW_STATS_CONF_RADIO_TYPE_5GL: return "5gl";
        case OW_STATS_CONF_RADIO_TYPE_5GU: return "5gu";
        case OW_STATS_CONF_RADIO_TYPE_6G: return "6g";
    }
    return "";
}

static const char *
ow_stats_conf_scan_type_to_str(const enum ow_stats_conf_scan_type t)
{
    switch (t) {
        case OW_STATS_CONF_SCAN_TYPE_UNSPEC: return "unspec";
        case OW_STATS_CONF_SCAN_TYPE_ON_CHAN: return "on-chan";
        case OW_STATS_CONF_SCAN_TYPE_OFF_CHAN: return "off-chan";
        case OW_STATS_CONF_SCAN_TYPE_FULL: return "full";
    }
    return "";
}

static const char *
ow_stats_conf_stats_type_to_str(const enum ow_stats_conf_stats_type t)
{
    switch (t) {
        case OW_STATS_CONF_STATS_TYPE_SURVEY: return "survey";
        case OW_STATS_CONF_STATS_TYPE_CLIENT: return "client";
        case OW_STATS_CONF_STATS_TYPE_NEIGHBOR: return "neighbor";
        case OW_STATS_CONF_STATS_TYPE_UNSPEC: return "unspec";
    }
    return "";
}

static void
ow_stats_conf_entry_params_free(struct ow_stats_conf_entry_params *p)
{
    FREE(p->channels);
    memset(p, 0, sizeof(*p));
}

static void
ow_stats_conf_entry_params_copy(struct ow_stats_conf_entry_params *dst,
                                const struct ow_stats_conf_entry_params *src)
{
    *dst = *src;
    if (dst->channels != NULL) {
        const size_t size = dst->n_channels * sizeof(dst->channels[0]);
        dst->channels = MEMNDUP(dst->channels, size);
    }
}

static bool
ow_stats_conf_entry_params_changed(const struct ow_stats_conf_entry_params *a,
                                   const struct ow_stats_conf_entry_params *b)
{
    const size_t size = a->n_channels * sizeof(a->channels[0]);
    return (a->n_channels != b->n_channels)
        || (a->radio_type != b->radio_type)
        || (a->scan_type != b->scan_type)
        || (a->stats_type != b->stats_type)
        || (a->sample_seconds != b->sample_seconds)
        || (a->report_seconds != b->report_seconds)
        || (size > 0 &&
            ((a->channels == NULL || b->channels == NULL) ||
             memcmp(a->channels, b->channels, size) != 0));
}

static void
ow_stats_conf_entry_get_sub_timings(const struct ow_stats_conf_entry *e,
                                    double *poll,
                                    double *report)
{
    *poll = 0;
    *report = 0;
    switch (e->params.stats_type) {
        case OW_STATS_CONF_STATS_TYPE_SURVEY:
            /* Surveys are special compared to others.
             * Survey samples are not aggregated per keyed
             * bucket. Instead, they are appended on a list.
             */
            *poll = e->params.sample_seconds;
            *report = e->params.sample_seconds;
            break;
        case OW_STATS_CONF_STATS_TYPE_CLIENT: /* fall-through */
        case OW_STATS_CONF_STATS_TYPE_NEIGHBOR:
            *report = e->params.report_seconds;
            *poll = e->params.sample_seconds;
            break;
        case OW_STATS_CONF_STATS_TYPE_UNSPEC:
            break;
    }

    if (*poll == 0) {
        *poll = *report;
    }
}

static void
ow_stats_conf_entry_free_surveys(struct ow_stats_conf_entry *e)
{
    dpp_survey_record_t *i;
    while ((i = ds_dlist_remove_head(&e->surveys)) != NULL) {
        dpp_survey_record_free(i);
    }
}

static void
ow_stats_conf_entry_free_neighbors(struct ow_stats_conf_entry *e)
{
    dpp_neighbor_record_list_t *i;
    while ((i = ds_dlist_remove_head(&e->neighbors)) != NULL) {
        dpp_neighbor_record_free(i);
    }
}

static void
ow_stats_conf_entry_free_clients(struct ow_stats_conf_entry *e)
{
    dpp_client_record_t *i;
    while ((i = ds_dlist_remove_head(&e->clients)) != NULL) {
        dpp_client_record_free(i);
    }
}

static void
ow_stats_conf_entry_free_sta_list(struct ow_stats_conf_entry *e)
{
    struct ow_stats_conf_sta *sta;
    while ((sta = ds_tree_head(&e->sta_tree)) != NULL) {
        ds_tree_remove(&e->sta_tree, sta);
        FREE(sta);
    }
}

static void
ow_stats_conf_entry_stop(struct ow_stats_conf_entry *e)
{
    if (e == NULL) return;
    if (e->sub != NULL) {
        osw_state_unregister_observer(&e->obs);
        osw_stats_unregister_subscriber(e->sub);
        osw_stats_subscriber_free(e->sub);
        e->sub = NULL;
        LOGI("ow: stats: entry: %s: stopping", e->id ?: "");
    }
    ow_stats_conf_entry_free_surveys(e);
    ow_stats_conf_entry_free_neighbors(e);
    ow_stats_conf_entry_free_clients(e);
    ow_stats_conf_entry_free_sta_list(e);
}

static void
ow_stats_conf_entry_free(struct ow_stats_conf_entry *e)
{
    if (e == NULL) return;
    e->report_at = -1;
    ow_stats_conf_entry_stop(e);
    ow_stats_conf_entry_params_free(&e->params);
    ow_stats_conf_entry_params_free(&e->params_next);
    ds_tree_remove(&e->conf->entries, e);
    FREE(e->id);
    FREE(e);
}

static uint32_t
ow_stats_conf_freq_to_chan(const enum ow_stats_conf_radio_type t,
                           const uint32_t freq_mhz)
{
    if (freq_mhz == 0) return 0;
    switch (t) {
        case OW_STATS_CONF_RADIO_TYPE_UNSPEC:
            break;
        case OW_STATS_CONF_RADIO_TYPE_2G:
            return (freq_mhz - 2407) / 5;
        case OW_STATS_CONF_RADIO_TYPE_5G: /* fall through */
        case OW_STATS_CONF_RADIO_TYPE_5GL: /* fall through */
        case OW_STATS_CONF_RADIO_TYPE_5GU:
            return (freq_mhz - 5000) / 5;
        case OW_STATS_CONF_RADIO_TYPE_6G:
            if (freq_mhz == 5935) return 2;
            return (freq_mhz - 5950) / 5;
    }
    return 0;
}

static uint32_t
ow_stats_conf_get_oper_mhz(struct ow_stats_conf *c,
                           const char *phy_name)
{
    struct ow_stats_conf_freq_phy *fp = ds_tree_find(&c->freqs_phy, phy_name);
    if (fp == NULL) return 0;
    struct ow_stats_conf_freq_vif *fv = ds_tree_head(&fp->vifs);
    if (fv == NULL) return 0;
    return fv->freq_mhz;
}

static struct ow_stats_conf_band *
ow_stats_conf_get_radio_type(struct ow_stats_conf *c,
                             const enum ow_stats_conf_radio_type type)
{
    struct ow_stats_conf_band *b;
    ds_tree_foreach(&c->bands, b)
        if (b->type == type)
            return b;
    return NULL;
}

static uint32_t
ow_stats_conf_get_oper_chan(struct ow_stats_conf *c,
                            const enum ow_stats_conf_radio_type type)
{
    struct ow_stats_conf_band *b = ow_stats_conf_get_radio_type(c, type);
    const uint32_t freq_mhz = ow_stats_conf_get_oper_mhz(c, b->phy_name);
    const uint32_t chan = ow_stats_conf_freq_to_chan(type, freq_mhz);
    return chan;
}

static bool
ow_stats_conf_is_chan_ok(struct ow_stats_conf_entry *e,
                         const char *phy_name,
                         const uint32_t chan)
{
    switch (e->params.scan_type) {
        case OW_STATS_CONF_SCAN_TYPE_UNSPEC: return false;
        case OW_STATS_CONF_SCAN_TYPE_FULL: return false; /* FIXME */
        case OW_STATS_CONF_SCAN_TYPE_ON_CHAN: return true;
        case OW_STATS_CONF_SCAN_TYPE_OFF_CHAN:
            {
                const uint32_t oper_mhz = ow_stats_conf_get_oper_mhz(e->conf, phy_name);
                const uint32_t oper_chan = ow_stats_conf_freq_to_chan(e->params.radio_type, oper_mhz);
                if (oper_chan == chan) return false;
                if (e->params.n_channels == 0) return true;
                size_t i;
                bool match = false;
                for (i = 0; i < e->params.n_channels; i++)
                    if (e->params.channels[i] == (int)chan)
                        match = true;
                return match;
            }
    }
    return false;
}

static bool
ow_stats_conf_is_scan_type_ok(struct ow_stats_conf_entry *e,
                              const char *phy_name,
                              const uint32_t freq_mhz)
{
    const uint32_t oper_mhz = ow_stats_conf_get_oper_mhz(e->conf, phy_name);
    const enum ow_stats_conf_scan_type scan_type = oper_mhz == freq_mhz
                                                 ? OW_STATS_CONF_SCAN_TYPE_ON_CHAN
                                                 : OW_STATS_CONF_SCAN_TYPE_OFF_CHAN;
    return e->params.scan_type == scan_type;
}

static bool
ow_stats_conf_is_radio_type_ok(struct ow_stats_conf_entry *e,
                               const char *phy_name)
{
    struct ow_stats_conf *c = e->conf;
    const struct ow_stats_conf_band *band = ds_tree_find(&c->bands, phy_name);
    if (band == NULL) return false;
    return e->params.radio_type == band->type;
}

static void
ow_stats_conf_sub_report_survey(struct ow_stats_conf_entry *e,
                                const struct osw_stats_defs *defs,
                                const struct osw_tlv_hdr **tb,
                                const double now)
{
    const struct osw_tlv_hdr *percent = tb[OSW_STATS_CHAN_CNT_PERCENT];
    const struct osw_tlv_hdr *phy = tb[OSW_STATS_CHAN_PHY_NAME];
    const struct osw_tlv_hdr *freq = tb[OSW_STATS_CHAN_FREQ_MHZ];

    if (percent == NULL) return;
    if (phy == NULL) return;
    if (freq == NULL) return;

    const char *phy_name = osw_tlv_get_string(phy);
    const uint32_t freq_mhz = osw_tlv_get_u32(freq);
    const uint32_t chan = ow_stats_conf_freq_to_chan(e->params.radio_type, freq_mhz);

    if (ow_stats_conf_is_radio_type_ok(e, phy_name) == false) return;
    if (ow_stats_conf_is_scan_type_ok(e, phy_name, freq_mhz) == false) return;
    if (ow_stats_conf_is_chan_ok(e, phy_name, chan) == false) return;

    const struct osw_tlv_hdr *active = tb[OSW_STATS_CHAN_ACTIVE_MSEC];
    const struct osw_tlv_hdr *nf = tb[OSW_STATS_CHAN_NOISE_FLOOR_DBM];
    const size_t tb_size2 = defs->tpolicy[OSW_STATS_CHAN_CNT_PERCENT].tb_size;
    const struct osw_tlv_policy *policy2 = defs->tpolicy[OSW_STATS_CHAN_CNT_PERCENT].nested;
    const struct osw_tlv_hdr *tb2[tb_size2];
    memset(tb2, 0, tb_size2 * sizeof(tb2[0]));
    const void *data2 = osw_tlv_get_data(percent);
    const size_t len2 = percent->len;
    const size_t left2 = osw_tlv_parse(data2, len2, policy2, tb2, tb_size2);
    const struct osw_tlv_hdr *tx = tb2[OSW_STATS_CHAN_CNT_TX];
    const struct osw_tlv_hdr *rx = tb2[OSW_STATS_CHAN_CNT_RX];
    const struct osw_tlv_hdr *inbss = tb2[OSW_STATS_CHAN_CNT_RX_INBSS];
    const struct osw_tlv_hdr *busy = tb2[OSW_STATS_CHAN_CNT_BUSY];
    WARN_ON(left2 != 0);

    const uint32_t active_msec = active ? osw_tlv_get_u32(active) : 0;
    if (active_msec == 0) return;

    dpp_survey_record_t *r = dpp_survey_record_alloc();
    ds_dlist_insert_tail(&e->surveys, r);

    r->info.chan = ow_stats_conf_freq_to_chan(e->params.radio_type, freq_mhz);
    r->info.timestamp_ms = now * 1e3;
    r->duration_ms = active_msec;

    if (tx) r->chan_tx = osw_tlv_get_u32(tx);
    if (rx) r->chan_rx = osw_tlv_get_u32(rx);
    if (inbss) r->chan_self = osw_tlv_get_u32(inbss);
    if (busy) r->chan_busy = osw_tlv_get_u32(busy);
    if (nf) r->chan_noise = (int)osw_tlv_get_float(nf);

    LOGT("ow: stats: conf: report: survey:"
         " type=%s"
         " chan=%"PRIu32
         " ts=%"PRIu64
         " msec=%"PRIu32
         " nf=%"PRId32
         " tx=%"PRIu32
         " rx=%"PRIu32
         " self=%"PRIu32
         " busy=%"PRIu32,
         ow_stats_conf_scan_type_to_str(e->params.scan_type),
         r->info.chan,
         r->info.timestamp_ms,
         r->duration_ms,
         r->chan_noise,
         r->chan_tx,
         r->chan_rx,
         r->chan_self,
         r->chan_busy);

    /* FIXME: r->chan_noise */
}

static void
ow_stats_conf_sub_report_bss_scan(struct ow_stats_conf_entry *e,
                                  const struct osw_stats_defs *defs,
                                  const struct osw_tlv_hdr **tb,
                                  const double now)
{
    const struct osw_tlv_hdr *phy = tb[OSW_STATS_BSS_SCAN_PHY_NAME];
    const struct osw_tlv_hdr *mac = tb[OSW_STATS_BSS_SCAN_MAC_ADDRESS];
    const struct osw_tlv_hdr *freq = tb[OSW_STATS_BSS_SCAN_FREQ_MHZ];
    const struct osw_tlv_hdr *ssid = tb[OSW_STATS_BSS_SCAN_SSID];
    const struct osw_tlv_hdr *width = tb[OSW_STATS_BSS_SCAN_WIDTH_MHZ];
    const struct osw_tlv_hdr *ies = tb[OSW_STATS_BSS_SCAN_IES];
    const struct osw_tlv_hdr *snr = tb[OSW_STATS_BSS_SCAN_SNR_DB];

    if (phy == NULL) return;
    if (mac == NULL) return;

    const char *phy_name = osw_tlv_get_string(phy);
    const uint32_t freq_mhz = osw_tlv_get_u32(freq);
    const uint32_t chan = ow_stats_conf_freq_to_chan(e->params.radio_type, freq_mhz);

    if (ow_stats_conf_is_radio_type_ok(e, phy_name) == false) return;
    if (ow_stats_conf_is_scan_type_ok(e, phy_name, freq_mhz) == false) return;
    if (ow_stats_conf_is_chan_ok(e, phy_name, chan) == false) return;

    struct osw_hwaddr bssid;
    struct osw_hwaddr_str bssid_strbuf;
    osw_tlv_get_hwaddr(&bssid, mac);
    const char *bssid_str = osw_hwaddr2str(&bssid, &bssid_strbuf);

    dpp_neighbor_record_list_t *r = dpp_neighbor_record_alloc();
    ds_dlist_insert_tail(&e->neighbors, r);

    r->entry.chan = chan;
    r->entry.type = ow_stats_conf_radio_type_to_dpp(e->params.radio_type);
    snprintf(r->entry.bssid,sizeof(r->entry.ssid), "%s", bssid_str);

    if (ssid != NULL) {
        memcpy(r->entry.ssid, osw_tlv_get_data(ssid), ssid->len);
        r->entry.ssid[ssid->len] = '\0';
    }

    if (width != NULL) {
        const uint32_t mhz = osw_tlv_get_u32(width);
        r->entry.chanwidth = ow_stats_conf_width_to_dpp(mhz);
    }

    if (ies != NULL) {
        /* FIXME:*/
    }

    if (snr != NULL) {
        const uint32_t snr_db = osw_tlv_get_u32(snr);
        r->entry.sig = snr_db;
    }

    LOGT("ow: stats: conf: report: bss:"
         " type=%s"
         " bssid=%s"
         " radio=%d"
         " chan=%"PRIu32
         " width=%d"
         " snr=%"PRId32
         " ssid=%s",
         ow_stats_conf_scan_type_to_str(e->params.scan_type),
         r->entry.bssid,
         r->entry.type,
         r->entry.chan,
         r->entry.chanwidth,
         r->entry.sig,
         r->entry.ssid);

    /* FIXME: r->entry.tsf (unnecessary?) */
    /* FIXME: r->entry.sig (rssi) */
    /* FIXME: r->entry.lastseen for diff reports */
}

static void
ow_stats_conf_sub_report_sta(struct ow_stats_conf_entry *e,
                             const struct osw_stats_defs *defs,
                             const struct osw_tlv_hdr **tb,
                             const double now)
{
    const struct osw_tlv_hdr *phy = tb[OSW_STATS_STA_PHY_NAME];
    const struct osw_tlv_hdr *vif = tb[OSW_STATS_STA_VIF_NAME];
    const struct osw_tlv_hdr *mac = tb[OSW_STATS_STA_MAC_ADDRESS];

    if (phy == NULL) return;
    if (vif == NULL) return;
    if (mac == NULL) return;

    const char *phy_name = osw_tlv_get_string(phy);
    const char *vif_name = osw_tlv_get_string(vif);
    (void)vif_name;

    if (ow_stats_conf_is_radio_type_ok(e, phy_name) == false) return;

    struct ow_stats_conf_sta_id id;
    MEMZERO(id);
    STRSCPY_WARN(id.vif_name.buf, vif_name);
    osw_tlv_get_hwaddr(&id.addr, mac);

    /* FIXME: This is subject to a possible race. If client
     * connects as report is about to be scheduled it's
     * possible that driver will deliver stats, but won't go
     * through all the motions of reporting sta_state. In
     * that case the report will be posponed until another
     * report target time. This could be re-tried sooner
     * upon sta_connected_cb, although that would violate
     * the reporting interval a little bit.
     *
     * Right now what it means is that the reported stats
     * delta will be lost, although arguably there would be
     * little to report anyway.
     */
    struct ow_stats_conf_sta *sta = ds_tree_find(&e->sta_tree, &id);
    if (sta == NULL) return;

    dpp_client_record_t *r = dpp_client_record_alloc();
    ds_dlist_insert_tail(&e->clients, r);

    r->is_connected = sta->is_connected;
    r->connected = sta->num_connects;
    r->disconnected = sta->num_disconnects;
    r->connect_ts = sta->connected_at * 1e3;
    r->disconnect_ts = sta->disconnected_at * 1e3;
    assert(sizeof(r->info.essid) > sta->ssid.len);
    memcpy(r->info.essid, sta->ssid.buf, sta->ssid.len);
    /* FIXME: network id */

    sta->num_connects = 0;
    sta->num_disconnects = 0;
    sta->connected_at = 0;
    sta->disconnected_at = 0;

    r->info.type = ow_stats_conf_radio_type_to_dpp(e->params.radio_type);
    memcpy(r->info.mac, &id.addr.octet, 6);

    const struct osw_tlv_hdr *tx_bytes = tb[OSW_STATS_STA_TX_BYTES];
    const struct osw_tlv_hdr *rx_bytes = tb[OSW_STATS_STA_RX_BYTES];
    const struct osw_tlv_hdr *tx_frames = tb[OSW_STATS_STA_TX_FRAMES];
    const struct osw_tlv_hdr *rx_frames = tb[OSW_STATS_STA_RX_FRAMES];
    const struct osw_tlv_hdr *tx_retries = tb[OSW_STATS_STA_TX_RETRIES];
    const struct osw_tlv_hdr *rx_retries = tb[OSW_STATS_STA_RX_RETRIES];
    const struct osw_tlv_hdr *tx_errors = tb[OSW_STATS_STA_TX_ERRORS];
    const struct osw_tlv_hdr *rx_errors = tb[OSW_STATS_STA_RX_ERRORS];
    const struct osw_tlv_hdr *tx_rate = tb[OSW_STATS_STA_TX_RATE_MBPS];
    const struct osw_tlv_hdr *rx_rate = tb[OSW_STATS_STA_RX_RATE_MBPS];
    const struct osw_tlv_hdr *snr = tb[OSW_STATS_STA_SNR_DB];

    if (tx_bytes) r->stats.bytes_tx = osw_tlv_get_u32(tx_bytes);
    if (rx_bytes) r->stats.bytes_rx = osw_tlv_get_u32(rx_bytes);
    if (tx_frames) r->stats.frames_tx = osw_tlv_get_u32(tx_frames);
    if (rx_frames) r->stats.frames_rx = osw_tlv_get_u32(rx_frames);
    if (tx_retries) r->stats.retries_tx = osw_tlv_get_u32(tx_retries);
    if (rx_retries) r->stats.retries_rx = osw_tlv_get_u32(rx_retries);
    if (tx_errors) r->stats.errors_tx = osw_tlv_get_u32(tx_errors);
    if (rx_errors) r->stats.errors_rx = osw_tlv_get_u32(rx_errors);
    if (tx_rate) r->stats.rate_tx = osw_tlv_get_u32(tx_rate);
    if (rx_rate) r->stats.rate_rx = osw_tlv_get_u32(rx_rate);
    if (snr) r->stats.rssi = osw_tlv_get_u32(snr);

    LOGT("ow: stats: conf: report: sta: "OSW_HWADDR_FMT":"
         " sta=%p"
         " radio=%u"
         " ssid=%s"
         " is_connected=%"PRIu32
         " connected=%"PRIu32
         " disconnected=%"PRIu32
         " connect_ts=%"PRIu64
         " disconnect_ts=%"PRIu64
         " bytes_tx=%"PRIu64
         " bytes_rx=%"PRIu64
         " frames_tx=%"PRIu64
         " frames_rx=%"PRIu64
         " retries_tx=%"PRIu64
         " retries_rx=%"PRIu64
         " errors_tx=%"PRIu64
         " errors_rx=%"PRIu64
         " rate_tx=%lf"
         " rate_rx=%lf"
         " rssi=%"PRId32,
         OSW_HWADDR_ARG(&id.addr),
         sta,
         r->info.type,
         r->info.essid,
         r->is_connected,
         r->connected,
         r->disconnected,
         r->connect_ts,
         r->disconnect_ts,
         r->stats.bytes_tx,
         r->stats.bytes_rx,
         r->stats.frames_tx,
         r->stats.frames_rx,
         r->stats.retries_tx,
         r->stats.retries_rx,
         r->stats.errors_tx,
         r->stats.errors_rx,
         r->stats.rate_tx,
         r->stats.rate_rx,
         r->stats.rssi);

    /* FIXME: rate_rx_perceived */
    /* FIXME: rate_tx_perceived */
}

static void
ow_stats_conf_sub_report(const enum osw_stats_id id,
                         const struct osw_tlv *delta,
                         const double now,
                         struct ow_stats_conf_entry *e)
{
    const struct osw_stats_defs *defs = osw_stats_defs_lookup(id);
    const size_t tb_size = defs->size;
    const struct osw_tlv_policy *policy = defs->tpolicy;

    const struct osw_tlv_hdr *tb[tb_size];
    memset(tb, 0, tb_size * sizeof(tb[0]));

    const size_t left = osw_tlv_parse(delta->data, delta->used, policy, tb, tb_size);
    WARN_ON(left != 0);

    e->last_sub_reported_at = now;
    switch (id) {
        case OSW_STATS_PHY: break;
        case OSW_STATS_VIF: break;
        case OSW_STATS_STA: ow_stats_conf_sub_report_sta(e, defs, tb, now); break;
        case OSW_STATS_CHAN: ow_stats_conf_sub_report_survey(e, defs, tb, now); break;
        case OSW_STATS_BSS_SCAN: ow_stats_conf_sub_report_bss_scan(e, defs, tb, now); break;
        case OSW_STATS_MAX__: break;
    }
}

static void
ow_stats_conf_sub_report_cb(const enum osw_stats_id id,
                            const struct osw_tlv *delta,
                            const struct osw_tlv *last,
                            void *priv)
{
    const double now_mono = OSW_TIME_TO_DBL(osw_time_mono_clk());
    ow_stats_conf_sub_report(id, delta, now_mono, priv);
}

static bool
ow_stats_conf_entry_report_survey(struct ow_stats_conf_entry *e,
                                  const double now_mono,
                                  const double now_real)
{
    if (ds_dlist_is_empty(&e->surveys)) return false;

    const uint64_t mono_msec = now_mono * 1e3;
    const uint64_t real_msec = now_real * 1e3;

    dpp_survey_record_t *i;
    ds_dlist_foreach(&e->surveys, i) {
        i->info.timestamp_ms -= mono_msec;
        i->info.timestamp_ms += real_msec;
    }

    dpp_survey_report_data_t r = {
        .list = e->surveys,
        .report_type = REPORT_TYPE_RAW, /* FIXME: support other types */
        .radio_type = ow_stats_conf_radio_type_to_dpp(e->params.radio_type),
        .scan_type = ow_stats_conf_scan_type_to_dpp(e->params.scan_type),
        .timestamp_ms = real_msec,
    };
    dpp_put_survey(&r);
    ow_stats_conf_entry_free_surveys(e);
    return true;
}

static bool
ow_stats_conf_entry_report_neighbor(struct ow_stats_conf_entry *e,
                                    const double now_mono,
                                    const double now_real)
{
    if (ds_dlist_is_empty(&e->neighbors)) return false;

    const uint64_t mono_msec = now_mono * 1e3;
    const uint64_t real_msec = now_real * 1e3;

    (void)mono_msec;

    dpp_neighbor_report_data_t r = {
        .list = e->neighbors,
        .report_type = REPORT_TYPE_RAW, /* FIXME: support other types */
        .radio_type = ow_stats_conf_radio_type_to_dpp(e->params.radio_type),
        .scan_type = ow_stats_conf_scan_type_to_dpp(e->params.scan_type),
        .timestamp_ms = real_msec,
    };
    dpp_put_neighbor(&r);
    ow_stats_conf_entry_free_neighbors(e);
    return true;
}

static bool
ow_stats_conf_entry_report_client(struct ow_stats_conf_entry *e,
                                  const double now_mono,
                                  const double now_real)
{
    if (ds_dlist_is_empty(&e->clients)) return false;

    const uint32_t channel = ow_stats_conf_get_oper_chan(e->conf, e->params.radio_type);
    const uint64_t mono_msec = now_mono * 1e3;
    const uint64_t real_msec = now_real * 1e3;

    dpp_client_record_t *i;
    ds_dlist_foreach(&e->clients, i) {
        if (i->connect_ts) {
            i->connect_ts -= mono_msec;
            i->connect_ts += real_msec;
        }

        if (i->disconnect_ts) {
            i->disconnect_ts -= mono_msec;
            i->disconnect_ts += real_msec;
        }
    }

    struct ow_stats_conf_sta *sta;
    struct ow_stats_conf_sta *tmp;
    ds_tree_foreach_safe(&e->sta_tree, sta, tmp) {
        if (sta->is_connected == true ||
            sta->num_connects != 0 ||
            sta->num_disconnects != 0 ||
            sta->connected_at != 0 ||
            sta->disconnected_at != 0) {
            continue;
        }

        ds_tree_remove(&e->sta_tree, sta);
        FREE(sta);
    }

    dpp_client_report_data_t r = {
        .list = e->clients,
        .radio_type = ow_stats_conf_radio_type_to_dpp(e->params.radio_type),
        .timestamp_ms = real_msec,
        .channel = channel,
        /* FIXME: uplink_type? */
        /* FIXME: uplink_changed? */
    };
    dpp_put_client(&r);
    ow_stats_conf_entry_free_clients(e);
    return true;
}


#define OW_STATS_CONF_MAX_UNDERRUN 10

static bool
ow_stats_conf_is_entry_ready(struct ow_stats_conf_entry *e,
                             const double now)
{
    /* FIXME: The osw_stats_subscriber report timer will
     * overlap with ow_stats_conf report timer. Their order
     * of undefined currently so it's possible that this
     * code is called _before_ osw_stats_subscriber delivers
     * buckets of data. In such case don't advance the
     * timer. Keep it unchanged (but armed) so that this
     * function is re-run. Keep a limit to avoid infinite
     * loops though.
     *
     * This could be fixed if ow_stats_conf_run() and
     * osw_stats_run() get orchestrated in order to prevent
     * this in the first place. But these are intended to
     * not know about each other, at least explicitly, so
     * there's no easy way to fix this otherwise for now.
     */
    const double age_seconds = now - e->last_sub_reported_at;
    const double max_age_seconds = (e->params.sample_seconds ?: e->params.report_seconds) / 2;
    if (age_seconds > max_age_seconds) {
        e->underrun++;
        if (e->underrun < OW_STATS_CONF_MAX_UNDERRUN) {
            return false;
        }

        LOGI("ow: stats: conf: underrun on entry '%s', type=%s radio=%s chan=%s",
             e->id,
             ow_stats_conf_stats_type_to_str(e->params.stats_type),
             ow_stats_conf_radio_type_to_str(e->params.radio_type),
             ow_stats_conf_scan_type_to_str(e->params.scan_type));
    }
    return true;
}

static void
ow_stats_conf_entry_report(struct ow_stats_conf_entry *e,
                           const double now_mono,
                           const double now_real)
{
    if (e->report_at <= 0) return;
    if (e->report_at > now_mono) return;
    if (ow_stats_conf_is_entry_ready(e, now_mono) == false) return;

    switch (e->params.stats_type) {
    case OW_STATS_CONF_STATS_TYPE_SURVEY:
        ow_stats_conf_entry_report_survey(e, now_mono, now_real);
        break;
    case OW_STATS_CONF_STATS_TYPE_NEIGHBOR:
        ow_stats_conf_entry_report_neighbor(e, now_mono, now_real);
        break;
    case OW_STATS_CONF_STATS_TYPE_CLIENT:
        ow_stats_conf_entry_report_client(e, now_mono, now_real);
        break;
    case OW_STATS_CONF_STATS_TYPE_UNSPEC:
        break;
    }

    assert(e->params.report_seconds > 0);
    e->report_at = (floor(now_mono / e->params.report_seconds) + 1) * e->params.report_seconds;
    e->underrun = 0;
}

static int
ow_stats_conf_sta_cmp(const void *a, const void *b)
{
    const struct ow_stats_conf_sta_id *x = a;
    const struct ow_stats_conf_sta_id *y = b;
    const int c1 = osw_hwaddr_cmp(&x->addr, &y->addr);
    const int c2 = strcmp(x->vif_name.buf, y->vif_name.buf);
    if (c1 != 0) return c1;
    if (c2 != 0) return c2;
    return 0;
}

const struct osw_ssid *
ow_stats_conf_sta_get_ssid(const struct osw_state_sta_info *sta)
{
    const struct osw_drv_vif_state_ap *vap = &sta->vif->drv_state->u.ap;
    const struct osw_drv_vif_state_sta *vsta = &sta->vif->drv_state->u.sta;

    switch (sta->vif->drv_state->vif_type) {
        case OSW_VIF_UNDEFINED: return NULL;
        case OSW_VIF_AP_VLAN: return NULL;
        case OSW_VIF_AP: return &vap->ssid;
        case OSW_VIF_STA: return &vsta->link.ssid;
    }

    return NULL;
}

struct ow_stats_conf_sta *
ow_stats_conf_sta_get(struct ow_stats_conf_entry *e,
                      const struct osw_state_sta_info *sta)
{
    struct ow_stats_conf_sta_id id;
    MEMZERO(id);
    STRSCPY_WARN(id.vif_name.buf, sta->vif->vif_name);
    id.addr = *sta->mac_addr;

    struct ow_stats_conf_sta *i = ds_tree_find(&e->sta_tree, &id);
    if (i != NULL) return i;

    const struct osw_ssid *ssid = ow_stats_conf_sta_get_ssid(sta);
    if (WARN_ON(ssid == NULL)) return NULL;

    i = CALLOC(1, sizeof(*i));
    i->id = id;
    memcpy(&i->ssid, ssid, sizeof(*ssid));
    ds_tree_insert(&e->sta_tree, i, &i->id);

    return i;
}

void
ow_stats_conf_sta_set_connected(struct ow_stats_conf_entry *e,
                                const struct osw_state_sta_info *sta,
                                const bool connected,
                                const double now)
{
    struct ow_stats_conf_sta *scsta = ow_stats_conf_sta_get(e, sta);
    if (scsta == NULL) return;

    if (connected == true) {
        scsta->connected_at = now;
        scsta->num_connects++;
    }
    else {
        scsta->disconnected_at = now;
        scsta->num_disconnects++;
    }

    scsta->is_connected = connected;
}

void
ow_stats_conf_sta_connected_cb(struct osw_state_observer *self,
                               const struct osw_state_sta_info *sta)
{
    struct ow_stats_conf_entry *e = container_of(self, struct ow_stats_conf_entry, obs);
    const double now_mono = OSW_TIME_TO_DBL(osw_time_mono_clk());
    ow_stats_conf_sta_set_connected(e, sta, true, now_mono);
}

void
ow_stats_conf_sta_disconnected_cb(struct osw_state_observer *self,
                                  const struct osw_state_sta_info *sta)
{
    struct ow_stats_conf_entry *e = container_of(self, struct ow_stats_conf_entry, obs);
    const double now_mono = OSW_TIME_TO_DBL(osw_time_mono_clk());
    ow_stats_conf_sta_set_connected(e, sta, false, now_mono);
}

static const struct osw_state_observer g_ow_stats_conf_sta_obs = {
    .name = __FILE__,
    .sta_connected_fn = ow_stats_conf_sta_connected_cb,
    .sta_disconnected_fn = ow_stats_conf_sta_disconnected_cb,
};

static void
ow_stats_conf_entry_start(struct ow_stats_conf_entry *e,
                          const double now)
{
    double poll;
    double report;
    ow_stats_conf_entry_get_sub_timings(e, &poll, &report);
    assert(report > 0);

    LOGI("ow: stats: entry: %s: starting type=%s radio=%s chan=%s",
          e->id ?: "",
          ow_stats_conf_stats_type_to_str(e->params.stats_type),
          ow_stats_conf_radio_type_to_str(e->params.radio_type),
          ow_stats_conf_scan_type_to_str(e->params.scan_type));

    e->obs = g_ow_stats_conf_sta_obs;

    assert(e->sub == NULL);
    e->sub = osw_stats_subscriber_alloc();
    switch (e->params.stats_type) {
        case OW_STATS_CONF_STATS_TYPE_SURVEY: osw_stats_subscriber_set_chan(e->sub, true); break;
        case OW_STATS_CONF_STATS_TYPE_CLIENT: osw_stats_subscriber_set_sta(e->sub, true); break;
        case OW_STATS_CONF_STATS_TYPE_NEIGHBOR: osw_stats_subscriber_set_bss(e->sub, true); break;
        case OW_STATS_CONF_STATS_TYPE_UNSPEC: break;
    }
    osw_stats_subscriber_set_report_fn(e->sub, ow_stats_conf_sub_report_cb, e);
    osw_stats_subscriber_set_report_seconds(e->sub, report);
    osw_stats_subscriber_set_poll_seconds(e->sub, poll);
    osw_stats_register_subscriber(e->sub);
    osw_state_register_observer(&e->obs);

    if (e->params.report_seconds > 0) {
        e->report_at = (floor(now / e->params.report_seconds) + 1) * e->params.report_seconds;
    }
}

enum ow_stats_conf_entry_process_op {
    OW_STATS_CONF_ENTRY_PROCESS_NOP,
    OW_STATS_CONF_ENTRY_PROCESS_STOP,
    OW_STATS_CONF_ENTRY_PROCESS_FREE,
    OW_STATS_CONF_ENTRY_PROCESS_START,
};

static enum ow_stats_conf_entry_process_op
ow_stats_conf_entry_process__(struct ow_stats_conf_entry *e)
{
    bool processed = true;
    const bool changed = ow_stats_conf_entry_params_changed(&e->params,
                                                            &e->params_next);

    ow_stats_conf_entry_params_free(&e->params);
    ow_stats_conf_entry_params_copy(&e->params, &e->params_next);

    if (changed == true) {
        processed = false;
    }

    if (e->params.valid == false) {
        processed = false;
    }

    if (processed == true) {
        return OW_STATS_CONF_ENTRY_PROCESS_NOP;
    }

    if (e->params_next.valid == false) {
        return OW_STATS_CONF_ENTRY_PROCESS_FREE;
    }

    if (e->params.radio_type == OW_STATS_CONF_RADIO_TYPE_UNSPEC) {
        LOGD("%s: %s: radio type unspec", __func__, e->id);
        return OW_STATS_CONF_ENTRY_PROCESS_STOP;
    }

    switch (e->params.stats_type) {
        case OW_STATS_CONF_STATS_TYPE_UNSPEC:
            return OW_STATS_CONF_ENTRY_PROCESS_STOP;
        case OW_STATS_CONF_STATS_TYPE_NEIGHBOR:
        case OW_STATS_CONF_STATS_TYPE_SURVEY:
            if (e->params.scan_type == OW_STATS_CONF_SCAN_TYPE_UNSPEC) {
                LOGD("%s: %s: scan type unspec", __func__, e->id);
                return OW_STATS_CONF_ENTRY_PROCESS_STOP;
            }
            break;
        case OW_STATS_CONF_STATS_TYPE_CLIENT:
            break;
    }

    return OW_STATS_CONF_ENTRY_PROCESS_START;
}

static void
ow_stats_conf_entry_process(struct ow_stats_conf_entry *e,
                            const double now)
{
    switch (ow_stats_conf_entry_process__(e)) {
        case OW_STATS_CONF_ENTRY_PROCESS_NOP:
            break;
        case OW_STATS_CONF_ENTRY_PROCESS_STOP:
            ow_stats_conf_entry_stop(e);
            break;
        case OW_STATS_CONF_ENTRY_PROCESS_FREE:
            ow_stats_conf_entry_free(e);
            break;
        case OW_STATS_CONF_ENTRY_PROCESS_START:
            ow_stats_conf_entry_stop(e);
            ow_stats_conf_entry_start(e, now);
            break;
    }
}

struct ow_stats_conf_entry *
ow_stats_conf_get_entry(struct ow_stats_conf *conf,
                        const char *id)
{
    struct ow_stats_conf_entry *e = ds_tree_find(&conf->entries, id);
    if (e == NULL) {
        e = CALLOC(1, sizeof(*e));
        e->conf = conf;
        e->id = STRDUP(id);
        ds_dlist_init(&e->surveys, dpp_survey_record_t, node);
        ds_dlist_init(&e->neighbors, dpp_neighbor_record_list_t, node);
        ds_dlist_init(&e->clients, dpp_client_record_t, node);
        ds_tree_init(&e->sta_tree, ow_stats_conf_sta_cmp, struct ow_stats_conf_sta, node);
        ds_tree_insert(&conf->entries, e, e->id);
    }
    return e;
}

void
ow_stats_conf_entry_reset_all(struct ow_stats_conf *conf)
{
    struct ow_stats_conf_entry *e;
    ds_tree_foreach(&conf->entries, e)
        ow_stats_conf_entry_reset(e);
}

void
ow_stats_conf_entry_reset(struct ow_stats_conf_entry *e)
{
    osw_timer_arm_at_nsec(&e->conf->work, 0);
    memset(&e->params_next, 0, sizeof(e->params_next));
}

void
ow_stats_conf_entry_set_sampling(struct ow_stats_conf_entry *e,
                                 int seconds)
{
    osw_timer_arm_at_nsec(&e->conf->work, 0);
    e->params_next.valid = true;
    e->params_next.sample_seconds = seconds;
}

void
ow_stats_conf_entry_set_reporting(struct ow_stats_conf_entry *e,
                                  int seconds)
{
    osw_timer_arm_at_nsec(&e->conf->work, 0);
    e->params_next.valid = true;
    e->params_next.report_seconds = seconds;
}

void
ow_stats_conf_entry_set_radio_type(struct ow_stats_conf_entry *e,
                                   enum ow_stats_conf_radio_type radio_type)
{
    osw_timer_arm_at_nsec(&e->conf->work, 0);
    e->params_next.valid = true;
    e->params_next.radio_type = radio_type;
}

void
ow_stats_conf_entry_set_scan_type(struct ow_stats_conf_entry *e,
                                   enum ow_stats_conf_scan_type scan_type)
{
    osw_timer_arm_at_nsec(&e->conf->work, 0);
    e->params_next.valid = true;
    e->params_next.scan_type = scan_type;
}

void
ow_stats_conf_entry_set_stats_type(struct ow_stats_conf_entry *e,
                                   enum ow_stats_conf_stats_type stats_type)
{
    osw_timer_arm_at_nsec(&e->conf->work, 0);
    e->params_next.valid = true;
    e->params_next.stats_type = stats_type;
}

void
ow_stats_conf_entry_set_channels(struct ow_stats_conf_entry *e,
                                 const int *channels,
                                 size_t n_channels)
{
    osw_timer_arm_at_nsec(&e->conf->work, 0);
    e->params_next.valid = true;

    const size_t size = n_channels * sizeof(*channels);
    const bool changed = ((n_channels == e->params_next.n_channels) &&
                          (n_channels == 0 ||
                           memcmp(channels, e->params_next.channels, size) == 0));
    if (changed == false) return;

    FREE(e->params_next.channels);
    e->params_next.channels = NULL;
    e->params_next.n_channels = 0;
    if (channels == NULL) return;
    e->params_next.channels = MEMNDUP(channels, size);
    e->params_next.n_channels = n_channels;
}

static bool
ow_stats_conf_report_gen(uint8_t **buf, uint32_t *len)
{
    const size_t max = 128 * 1024;
#ifndef DPP_FAST_PACK
    if (dpp_get_queue_elements() == 0)
        return false;

    *buf = MALLOC(max);
    return dpp_get_report(*buf, max, len);
#else
    return dpp_get_report2(buf, max, len);
#endif
}

static bool
ow_stats_conf_report_send_try(void)
{
    uint8_t *buf = NULL;
    uint32_t len;
    const bool generated = ow_stats_conf_report_gen(&buf, &len);
    const bool empty = !generated || len <= 0;
    if (empty) goto free;
    qm_response_t res;
    LOGI("ow: stats: conf: sending report: len=%u", len);
    const bool sent = qm_conn_send_stats(buf, len, &res);
    WARN_ON(sent == false);
    /* FIXME: Should probably store the buffer if sending
     * failed and re-try later without re-generating reports
     */
free:
    FREE(buf);
    return empty;
}

static void
ow_stats_conf_report_send(void)
{
    for (;;) {
        const bool empty = ow_stats_conf_report_send_try();
        if (empty) break;
        LOGD("ow: stats: conf: oversized report, trying more");
    }
}

static double
ow_stats_conf_get_next_at(struct ow_stats_conf *c)
{
    struct ow_stats_conf_entry *e;
    double at = -1;

    ds_tree_foreach(&c->entries, e) {
        const bool changed = ow_stats_conf_entry_params_changed(&e->params,
                                                                &e->params_next);
        if (changed == true) at = 0;
        if (e->report_at <= 0) continue;
        if (at < 0) at = e->report_at;
        if (at > e->report_at) at = e->report_at;
    }

    return OSW_TIME_SEC(at);
}

static void
ow_stats_conf_run(struct ow_stats_conf *c,
                  const double now_mono,
                  const double now_real)
{
    struct ow_stats_conf_entry *e;
    struct ow_stats_conf_entry *te;

    ds_tree_foreach_safe(&c->entries, e, te)
        ow_stats_conf_entry_process(e, now_mono);

    ds_tree_foreach(&c->entries, e)
        ow_stats_conf_entry_report(e, now_mono, now_real);

    ow_stats_conf_report_send();

    const double next_at = ow_stats_conf_get_next_at(c);
    if (next_at >= 0) osw_timer_arm_at_nsec(&c->work, next_at);
}

static void
ow_stats_conf_work_cb(struct osw_timer *t)
{
    struct ow_stats_conf *c = container_of(t, struct ow_stats_conf, work);
    const double now_mono = OSW_TIME_TO_DBL(osw_time_mono_clk());
    const double now_real = OSW_TIME_TO_DBL(osw_time_wall_clk());
    ow_stats_conf_run(c, now_mono, now_real);
}

radio_type_t
ow_stats_conf_phy_to_type(const struct osw_state_phy_info *phy)
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
    bool band_2g = false;
    bool band_5gl = false;
    bool band_5gu = false;
    bool band_6g = false;
    size_t i;
    for (i = 0; i < phy->drv_state->n_channel_states; i++) {
        const struct osw_channel_state *s = &phy->drv_state->channel_states[i];
        const int freq = s->channel.control_freq_mhz;
        if (freq >= b2ch1 && freq <= b2ch13) band_2g = true;
        if (freq == b2ch14) band_2g = true;
        if (freq >= b5ch36 && freq < b5ch100) band_5gl = true;
        if (freq >= b5ch100 && freq <= b5ch177) band_5gu = true;
        if (freq == b6ch2) band_6g = true;
        if (freq >= b6ch1 && freq <= b6ch233) band_6g = true;
    }
    const bool band_5g = band_5gl && band_5gu;
    const bool bands = band_2g + (band_5gl || band_5gu) + band_6g;
    if (bands != 1) return RADIO_TYPE_NONE;
    else if (band_2g) return RADIO_TYPE_2G;
    else if (band_5g) return RADIO_TYPE_5G;
    else if (band_5gl) return RADIO_TYPE_5GL;
    else if (band_5gu) return RADIO_TYPE_5GU;
    else if (band_6g) return RADIO_TYPE_6G;
    else return RADIO_TYPE_NONE;
}

static void
ow_stats_conf_band_set__(struct ow_stats_conf *c,
                         const char *phy_name,
                         enum ow_stats_conf_radio_type t)
{
    struct ow_stats_conf_band *b = ds_tree_find(&c->bands, phy_name);

    if (b == NULL) {
        b = CALLOC(1, sizeof(*b));
        b->phy_name = STRDUP(phy_name);
        ds_tree_insert(&c->bands, b, b->phy_name);
    }

    b->type = t;

    if (t == OW_STATS_CONF_RADIO_TYPE_UNSPEC) {
        ds_tree_remove(&c->bands, b);
        FREE(b->phy_name);
        FREE(b);
    }
}

static void
ow_stats_conf_band_set(struct osw_state_observer *self,
                       const struct osw_state_phy_info *phy,
                       bool removing)
{
    struct ow_stats_conf *c = container_of(self, struct ow_stats_conf, state_obs);
    const radio_type_t dpp_t = removing ? 0 : ow_stats_conf_phy_to_type(phy);
    const enum ow_stats_conf_radio_type t = ow_stats_conf_dpp_to_radio_type(dpp_t);
    ow_stats_conf_band_set__(c, phy->phy_name, t);
}

static void
ow_stats_conf_phy_added_cb(struct osw_state_observer *self,
                           const struct osw_state_phy_info *phy)
{
    ow_stats_conf_band_set(self, phy, false);
}

static void
ow_stats_conf_phy_changed_cb(struct osw_state_observer *self,
                             const struct osw_state_phy_info *phy)
{
    ow_stats_conf_band_set(self, phy, false);
}

static void
ow_stats_conf_phy_removed_cb(struct osw_state_observer *self,
                             const struct osw_state_phy_info *phy)
{
    ow_stats_conf_band_set(self, phy, true);
}

static int
ow_stats_conf_vif_get_freq_mhz(const struct osw_state_vif_info *vif)
{
    if (vif->drv_state->enabled == false) return 0;
    switch (vif->drv_state->vif_type) {
        case OSW_VIF_UNDEFINED: return 0;
        case OSW_VIF_AP: return vif->drv_state->u.ap.channel.control_freq_mhz;
        case OSW_VIF_AP_VLAN: return 0;
        case OSW_VIF_STA: return 0; /* FIXME */
    }
    return 0;
}

static void
ow_stats_conf_freq_set__(struct ow_stats_conf *c,
                         const char *phy_name,
                         const char *vif_name,
                         uint32_t freq_mhz)
{
    struct ow_stats_conf_freq_vif *fv = ds_tree_find(&c->freqs_vif, vif_name);
    struct ow_stats_conf_freq_phy *fp = ds_tree_find(&c->freqs_phy, phy_name);

    if (fp == NULL) {
        fp = CALLOC(1, sizeof(*fp));
        fp->phy_name = STRDUP(phy_name);
        ds_tree_init(&fp->vifs, ds_str_cmp, struct ow_stats_conf_freq_vif, phy_node);
        ds_tree_insert(&c->freqs_phy, fp, fp->phy_name);
    }

    if (fv == NULL) {
        fv = CALLOC(1, sizeof(*fv));
        fv->vif_name = STRDUP(vif_name);
        ds_tree_insert(&c->freqs_vif, fv, fv->vif_name);
        ds_tree_insert(&fp->vifs, fv, fv->vif_name);
    }

    fv->freq_mhz = freq_mhz;

    if (freq_mhz == 0) {
        ds_tree_remove(&fp->vifs, fv);
        ds_tree_remove(&c->freqs_vif, fv);
        FREE(fv->vif_name);
        FREE(fv);

        if (ds_tree_is_empty(&fp->vifs) == true) {
            ds_tree_remove(&c->freqs_phy, fp);
            FREE(fp->phy_name);
            FREE(fp);
        }
    }
}

static bool
ow_stats_conf_freq_is_valid(const struct osw_state_vif_info *vif)
{
    switch (vif->drv_state->vif_type) {
        case OSW_VIF_UNDEFINED:
        case OSW_VIF_AP_VLAN:
            return false;
        case OSW_VIF_AP:
        case OSW_VIF_STA:
            return vif->drv_state->enabled;
    }
    return false;
}

static void
ow_stats_conf_freq_set(struct osw_state_observer *self,
                       const struct osw_state_vif_info *vif,
                       const bool removing)
{
    struct ow_stats_conf *c = container_of(self, struct ow_stats_conf, state_obs);
    const bool valid = (removing == false && ow_stats_conf_freq_is_valid(vif));
    const int freq_mhz = valid ? ow_stats_conf_vif_get_freq_mhz(vif) : 0;
    ow_stats_conf_freq_set__(c, vif->phy->phy_name, vif->vif_name, freq_mhz);
}

static void
ow_stats_conf_vif_added_cb(struct osw_state_observer *self,
                           const struct osw_state_vif_info *vif)
{
    ow_stats_conf_freq_set(self, vif, false);
}

static void
ow_stats_conf_vif_changed_cb(struct osw_state_observer *self,
                             const struct osw_state_vif_info *vif)
{
    ow_stats_conf_freq_set(self, vif, false);
}

static void
ow_stats_conf_vif_removed_cb(struct osw_state_observer *self,
                             const struct osw_state_vif_info *vif)
{
    ow_stats_conf_freq_set(self, vif, true);
}

static void
ow_stats_conf_init_dpp(void)
{
    static bool initialized = false;
    if (initialized == true) return;
    dpp_init();
    initialized = true;
}

static void
ow_stats_conf_init(struct ow_stats_conf *c)
{
    ds_tree_init(&c->entries, ds_str_cmp, struct ow_stats_conf_entry, node);
    ds_tree_init(&c->bands, ds_str_cmp, struct ow_stats_conf_band, node);
    ds_tree_init(&c->freqs_phy, ds_str_cmp, struct ow_stats_conf_freq_phy, node);
    ds_tree_init(&c->freqs_vif, ds_str_cmp, struct ow_stats_conf_freq_vif, conf_node);
    ow_stats_conf_init_dpp();
    osw_timer_init(&c->work, ow_stats_conf_work_cb);
}

static void
ow_stats_conf_attach(struct ow_stats_conf *c)
{
    struct osw_state_observer obs = {
        .name = __FILE__,
        .phy_added_fn = ow_stats_conf_phy_added_cb,
        .phy_changed_fn = ow_stats_conf_phy_changed_cb,
        .phy_removed_fn = ow_stats_conf_phy_removed_cb,
        .vif_added_fn = ow_stats_conf_vif_added_cb,
        .vif_changed_fn = ow_stats_conf_vif_changed_cb,
        .vif_removed_fn = ow_stats_conf_vif_removed_cb,
    };
    memcpy(&c->state_obs, &obs, sizeof(obs));
    osw_state_register_observer(&c->state_obs);
}

struct ow_stats_conf *
ow_stats_conf_get(void)
{
    static struct ow_stats_conf c;
    if (c.state_obs.name == NULL) {
        ow_stats_conf_init(&c);
        ow_stats_conf_attach(&c);
    }
    return &c;
}

OSW_MODULE(ow_stats_conf)
{
    OSW_MODULE_LOAD(osw_stats);
    return NULL;
}

OSW_UT(ow_stats_conf_changed)
{
    struct ow_stats_conf_entry_params p1;
    MEMZERO(p1);
    struct ow_stats_conf_entry_params p2;
    MEMZERO(p2);
    int c1[] = { 1, 2, 3 };

    assert(ow_stats_conf_entry_params_changed(&p1, &p2) == false);
    p1.n_channels = 1;
    assert(ow_stats_conf_entry_params_changed(&p1, &p2) == true);
    p2.n_channels = 1;
    assert(ow_stats_conf_entry_params_changed(&p1, &p2) == true);
    p1.n_channels = 0;
    p2.n_channels = 0;

    p1.channels = c1;
    p1.n_channels = ARRAY_SIZE(c1);

    ow_stats_conf_entry_params_free(&p2);
    assert(ow_stats_conf_entry_params_changed(&p1, &p2) == true);
    ow_stats_conf_entry_params_copy(&p2, &p1);
    assert(p2.n_channels == 3);
    assert(p2.channels != NULL);
    assert(ow_stats_conf_entry_params_changed(&p1, &p2) == false);
    ow_stats_conf_entry_params_free(&p2);
}

OSW_UT(ow_stats_conf_process)
{
    struct ow_stats_conf c;
    MEMZERO(c);
    struct ow_stats_conf_entry e1 = {
        .conf = &c,
        .id = "e1",
    };
    enum ow_stats_conf_entry_process_op op;

    ow_stats_conf_init(&c);

    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_FREE);

    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_FREE);

    ow_stats_conf_entry_set_sampling(&e1, 1);
    assert(e1.params_next.valid == true);
    assert(e1.params_next.sample_seconds == 1);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_STOP);

    ow_stats_conf_entry_set_radio_type(&e1, OW_STATS_CONF_RADIO_TYPE_2G);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_STOP);

    ow_stats_conf_entry_set_stats_type(&e1, OW_STATS_CONF_STATS_TYPE_SURVEY);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_STOP);

    ow_stats_conf_entry_set_stats_type(&e1, OW_STATS_CONF_STATS_TYPE_NEIGHBOR);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_STOP);

    ow_stats_conf_entry_set_stats_type(&e1, OW_STATS_CONF_STATS_TYPE_NEIGHBOR);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_NOP);

    ow_stats_conf_entry_set_scan_type(&e1, OW_STATS_CONF_SCAN_TYPE_ON_CHAN);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_START);

    ow_stats_conf_entry_set_scan_type(&e1, OW_STATS_CONF_SCAN_TYPE_UNSPEC);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_STOP);

    ow_stats_conf_entry_set_stats_type(&e1, OW_STATS_CONF_STATS_TYPE_CLIENT);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_START);

    ow_stats_conf_entry_set_stats_type(&e1, OW_STATS_CONF_STATS_TYPE_CLIENT);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_NOP);

    ow_stats_conf_entry_reset(&e1);
    ow_stats_conf_entry_set_sampling(&e1, 1);
    ow_stats_conf_entry_set_radio_type(&e1, OW_STATS_CONF_RADIO_TYPE_2G);
    ow_stats_conf_entry_set_stats_type(&e1, OW_STATS_CONF_STATS_TYPE_CLIENT);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_NOP);

    ow_stats_conf_entry_reset(&e1);
    ow_stats_conf_entry_set_sampling(&e1, 2);
    ow_stats_conf_entry_set_radio_type(&e1, OW_STATS_CONF_RADIO_TYPE_2G);
    ow_stats_conf_entry_set_stats_type(&e1, OW_STATS_CONF_STATS_TYPE_CLIENT);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_START);

    ow_stats_conf_entry_reset(&e1);
    op = ow_stats_conf_entry_process__(&e1);
    assert(op == OW_STATS_CONF_ENTRY_PROCESS_FREE);
}
