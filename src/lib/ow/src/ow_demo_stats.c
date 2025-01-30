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

#include <util.h>
#include <log.h>
#include <osw_stats.h>
#include <osw_stats_defs.h>
#include <osw_stats_subscriber.h>
#include <osw_module.h>
#include <osw_etc.h>

#define NOTE(fmt, ...) LOGI("%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

static bool
ow_demo_stats_is_enabled(void)
{
    return atoi(osw_etc_get("OW_DEMO_STATS_ENABLED") ?: "0") == 1;
}

static void
ow_demo_stats_report_chan(const struct osw_tlv *data,
                          const struct osw_tlv *last,
                          void *priv)
{
    const struct osw_stats_defs *defs = osw_stats_defs_lookup(OSW_STATS_CHAN);

    assert(defs != NULL);

    const struct osw_tlv_policy *p = defs->tpolicy;
    const size_t size = defs->size;
    const struct osw_tlv_hdr *tb[size];

    memset(tb, 0, size * sizeof(*tb));
    osw_tlv_parse(data->data, data->used, p, tb, size);

    if (tb[OSW_STATS_CHAN_ACTIVE_MSEC] &&
        osw_tlv_get_u32(tb[OSW_STATS_CHAN_ACTIVE_MSEC]) == 0) {
        return;
    }

    char *line = NULL;
    strgrow(&line, "chan: report:");

    if (tb[OSW_STATS_CHAN_PHY_NAME])
        strgrow(&line, " phy=%s", osw_tlv_get_string(tb[OSW_STATS_CHAN_PHY_NAME]));
    if (tb[OSW_STATS_CHAN_FREQ_MHZ])
        strgrow(&line, " freq=%uMHz", osw_tlv_get_u32(tb[OSW_STATS_CHAN_FREQ_MHZ]));
    if (tb[OSW_STATS_CHAN_ACTIVE_MSEC])
        strgrow(&line, " active=%umsec", osw_tlv_get_u32(tb[OSW_STATS_CHAN_ACTIVE_MSEC]));
    if (tb[OSW_STATS_CHAN_NOISE_FLOOR_DBM])
        strgrow(&line, " nf=%ddBm", (int)osw_tlv_get_float(tb[OSW_STATS_CHAN_NOISE_FLOOR_DBM]));

    if (tb[OSW_STATS_CHAN_CNT_MSEC]) {
        const struct osw_tlv_hdr *msec = tb[OSW_STATS_CHAN_CNT_MSEC];
        const struct osw_tlv_policy *p2 = p[OSW_STATS_CHAN_CNT_MSEC].nested;
        const size_t size2 = defs->size;
        const struct osw_tlv_hdr *tb2[size2];
        const void *data = osw_tlv_get_data(msec);
        const size_t len = msec->len;

        strgrow(&line, " msec:");
        memset(tb2, 0, size * sizeof(*tb2));
        osw_tlv_parse(data, len, p2, tb2, size2);

        if (tb2[OSW_STATS_CHAN_CNT_TX])
            strgrow(&line, " tx=%u", osw_tlv_get_u32(tb2[OSW_STATS_CHAN_CNT_TX]));
        if (tb2[OSW_STATS_CHAN_CNT_RX])
            strgrow(&line, " rx=%u", osw_tlv_get_u32(tb2[OSW_STATS_CHAN_CNT_RX]));
        if (tb2[OSW_STATS_CHAN_CNT_RX_INBSS])
            strgrow(&line, " inbss=%u", osw_tlv_get_u32(tb2[OSW_STATS_CHAN_CNT_RX_INBSS]));
        if (tb2[OSW_STATS_CHAN_CNT_BUSY])
            strgrow(&line, " busy=%u", osw_tlv_get_u32(tb2[OSW_STATS_CHAN_CNT_BUSY]));
    }

    if (tb[OSW_STATS_CHAN_CNT_PERCENT]) {
        const struct osw_tlv_hdr *percent = tb[OSW_STATS_CHAN_CNT_PERCENT];
        const struct osw_tlv_policy *p2 = p[OSW_STATS_CHAN_CNT_PERCENT].nested;
        const size_t size2 = defs->size;
        const struct osw_tlv_hdr *tb2[size2];
        const void *data = osw_tlv_get_data(percent);
        const size_t len = percent->len;

        strgrow(&line, " percent:");
        memset(tb2, 0, size * sizeof(*tb2));
        osw_tlv_parse(data, len, p2, tb2, size2);

        if (tb2[OSW_STATS_CHAN_CNT_TX])
            strgrow(&line, " tx=%u", osw_tlv_get_u32(tb2[OSW_STATS_CHAN_CNT_TX]));
        if (tb2[OSW_STATS_CHAN_CNT_RX])
            strgrow(&line, " rx=%u", osw_tlv_get_u32(tb2[OSW_STATS_CHAN_CNT_RX]));
        if (tb2[OSW_STATS_CHAN_CNT_RX_INBSS])
            strgrow(&line, " inbss=%u", osw_tlv_get_u32(tb2[OSW_STATS_CHAN_CNT_RX_INBSS]));
        if (tb2[OSW_STATS_CHAN_CNT_BUSY])
            strgrow(&line, " busy=%u", osw_tlv_get_u32(tb2[OSW_STATS_CHAN_CNT_BUSY]));
    }

    NOTE("%s", line);
    free(line);
}

static void
ow_demo_stats_report_bss_scan(const struct osw_tlv *data,
                              const struct osw_tlv *last,
                              void *priv)
{
    const struct osw_stats_defs *defs = osw_stats_defs_lookup(OSW_STATS_BSS_SCAN);

    assert(defs != NULL);

    const struct osw_tlv_policy *p = defs->tpolicy;
    const size_t size = defs->size;
    const struct osw_tlv_hdr *tb[size];

    memset(tb, 0, size * sizeof(*tb));
    osw_tlv_parse(data->data, data->used, p, tb, size);

    char *line = NULL;
    strgrow(&line, "bss: report:");

    if (tb[OSW_STATS_BSS_SCAN_PHY_NAME]) {
        strgrow(&line, " phy=%s", osw_tlv_get_string(tb[OSW_STATS_BSS_SCAN_PHY_NAME]));
    }

    if (tb[OSW_STATS_BSS_SCAN_MAC_ADDRESS]) {
        const struct osw_hwaddr *bssid = osw_tlv_get_data(tb[OSW_STATS_BSS_SCAN_MAC_ADDRESS]);
        strgrow(&line, " bssid=" OSW_HWADDR_FMT, OSW_HWADDR_ARG(bssid));
    }

    if (tb[OSW_STATS_BSS_SCAN_SSID]) {
        const struct osw_tlv_hdr *h = tb[OSW_STATS_BSS_SCAN_SSID];
        struct osw_ssid ssid;
        memcpy(ssid.buf, osw_tlv_get_data(h), h->len);
        ssid.len = h->len;
        strgrow(&line, " ssid=" OSW_SSID_FMT, OSW_SSID_ARG(&ssid));
    }

    if (tb[OSW_STATS_BSS_SCAN_FREQ_MHZ]) {
        strgrow(&line, " freq=%uMHz", osw_tlv_get_u32(tb[OSW_STATS_BSS_SCAN_FREQ_MHZ]));
    }

    if (tb[OSW_STATS_BSS_SCAN_WIDTH_MHZ]) {
        strgrow(&line, " width=%uMHz", osw_tlv_get_u32(tb[OSW_STATS_BSS_SCAN_WIDTH_MHZ]));
    }

    if (tb[OSW_STATS_BSS_SCAN_SNR_DB]) {
        strgrow(&line, " snr=%udB", osw_tlv_get_u32(tb[OSW_STATS_BSS_SCAN_SNR_DB]));
    }

    NOTE("%s", line);
    free(line);
}

static void
ow_demo_stats_report_cb(enum osw_stats_id id,
                        const struct osw_tlv *data,
                        const struct osw_tlv *last,
                        void *priv)
{
    switch (id) {
        case OSW_STATS_PHY: break;
        case OSW_STATS_VIF: break;
        case OSW_STATS_STA: break;
        case OSW_STATS_CHAN: return ow_demo_stats_report_chan(data, last, priv);
        case OSW_STATS_BSS_SCAN: return ow_demo_stats_report_bss_scan(data, last, priv);
        case OSW_STATS_MAX__: break;
    }
}

static void
ow_demo_stats_start(void)
{
    NOTE("starting");
    struct osw_stats_subscriber *sub = osw_stats_subscriber_alloc();
    osw_stats_subscriber_set_report_seconds(sub, 10);
    osw_stats_subscriber_set_poll_seconds(sub, 10);
    osw_stats_subscriber_set_chan(sub, true);
    osw_stats_subscriber_set_bss(sub, true);
    osw_stats_subscriber_set_report_fn(sub, ow_demo_stats_report_cb, sub);
    NOTE("registering");
    osw_stats_register_subscriber(sub);
    NOTE("started");
}

OSW_MODULE(ow_demo_stats)
{
    if (ow_demo_stats_is_enabled() == false) return NULL;
    ow_demo_stats_start();
    return NULL;
}
