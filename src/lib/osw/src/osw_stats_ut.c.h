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

#include <osw_ut.h>
#include <os.h>

void
osw_stats_ut_put_chan_cnt(struct osw_tlv *t,
                          const enum osw_stats_chan_id id,
                          const uint32_t tx,
                          const uint32_t rx,
                          const uint32_t rx_inbss,
                          const uint32_t busy)
{
    size_t s = osw_tlv_put_nested(t, id);
    osw_tlv_put_u32(t, OSW_STATS_CHAN_CNT_TX, tx);
    osw_tlv_put_u32(t, OSW_STATS_CHAN_CNT_RX, rx);
    osw_tlv_put_u32(t, OSW_STATS_CHAN_CNT_RX_INBSS, rx_inbss);
    osw_tlv_put_u32(t, OSW_STATS_CHAN_CNT_BUSY, busy);
    osw_tlv_end_nested(t, s);
}

void
osw_stats_ut_put_chan(struct osw_tlv *t,
                      const char *phy_name,
                      const uint32_t freq,
                      const enum osw_stats_chan_id id,
                      const uint32_t active,
                      const uint32_t tx,
                      const uint32_t rx,
                      const uint32_t rx_inbss,
                      const uint32_t busy)
{
    size_t s = osw_tlv_put_nested(t, OSW_STATS_CHAN);
    osw_tlv_put_string(t, OSW_STATS_CHAN_PHY_NAME, phy_name);
    osw_tlv_put_u32(t, OSW_STATS_CHAN_FREQ_MHZ, freq);
    osw_tlv_put_u32(t, OSW_STATS_CHAN_ACTIVE_MSEC, active);
    osw_stats_ut_put_chan_cnt(t, id, tx, rx, rx_inbss, busy);
    osw_tlv_end_nested(t, s);
}

void
osw_stats_ut_report_cb(enum osw_stats_id id,
                       const struct osw_tlv *data,
                       const struct osw_tlv *last,
                       void *priv)
{
    struct osw_tlv *ret = priv;
    size_t s = osw_tlv_put_nested(ret, id);
    memcpy(osw_tlv_reserve(ret, data->used), data->data, data->used);
    osw_tlv_end_nested(ret, s);
}

static void
osw_stats_ut_run_expect(struct osw_stats *s,
                        const double now,
                        bool idle,
                        bool poll,
                        unsigned int stats_mask)
{
    bool r_poll = false;
    unsigned int r_mask = 0;
    double r_seconds = -1;
    osw_stats_run_subscribers(s, now, &r_poll, &r_mask, &r_seconds);
    if (idle == true) assert(r_seconds < 0);
    if (idle == false) assert(stats_mask == stats_mask);
    if (idle == false) assert(r_seconds >= 0);
    assert(r_poll == poll);
}

static bool
osw_stats_ut_tlv_expect_chan(const struct osw_tlv *src,
                             const char *phy_name,
                             const uint32_t freq,
                             const enum osw_stats_chan_id id,
                             const uint32_t active,
                             const uint32_t tx,
                             const uint32_t rx,
                             const uint32_t rx_inbss,
                             const uint32_t busy)
{
    const struct osw_stats_defs *defs = osw_stats_defs_lookup(OSW_STATS_CHAN);
    assert(defs != NULL);

    const struct osw_tlv_hdr *i;
    const void *data = src->data;
    size_t len = src->used;

    osw_tlv_for_each(i, data, len) {
        assert(i->id == OSW_STATS_CHAN);

        const struct osw_tlv_hdr *tb[OSW_STATS_CHAN_MAX__];
        MEMZERO(tb);
        const size_t left = osw_tlv_parse(osw_tlv_get_data(i), i->len, defs->tpolicy, tb, OSW_STATS_CHAN_MAX__);
        assert(left == 0);

        assert(tb[OSW_STATS_CHAN_PHY_NAME] != NULL);
        assert(tb[OSW_STATS_CHAN_FREQ_MHZ] != NULL);

        if (strcmp(osw_tlv_get_string(tb[OSW_STATS_CHAN_PHY_NAME]), phy_name) != 0) continue;
        if (osw_tlv_get_u32(tb[OSW_STATS_CHAN_FREQ_MHZ]) != freq) continue;

        if (active > 0) {
            assert(tb[OSW_STATS_CHAN_ACTIVE_MSEC] != NULL);
            assert(osw_tlv_get_u32(tb[OSW_STATS_CHAN_ACTIVE_MSEC]) == active);
        }

        const struct osw_tlv_hdr *cnt = tb[id];
        assert(cnt != NULL);

        assert(defs->tpolicy[id].tb_size == OSW_STATS_CHAN_CNT_MAX__);
        const struct osw_tlv_hdr *tb2[OSW_STATS_CHAN_MAX__];
        MEMZERO(tb2);
        const size_t left2 = osw_tlv_parse(osw_tlv_get_data(cnt), cnt->len, defs->tpolicy[id].nested, tb2, OSW_STATS_CHAN_CNT_MAX__);
        assert(left2 == 0);

        struct {
            uint32_t i;
            int id;
        } cnts[] = {
            { .i = tx, OSW_STATS_CHAN_CNT_TX },
            { .i = rx, OSW_STATS_CHAN_CNT_RX },
            { .i = rx_inbss, OSW_STATS_CHAN_CNT_RX_INBSS },
            { .i = busy, OSW_STATS_CHAN_CNT_BUSY },
        };
        const size_t n = sizeof(cnts) / sizeof(cnts[0]);
        size_t j;

        for (j = 0; j < n; j++) {
            const struct osw_tlv_hdr *hdr = tb2[cnts[j].id];
            assert(hdr != NULL);
            const uint32_t x = osw_tlv_get_u32(hdr);
            assert(x == cnts[j].i);
        }

        return true;
    }

    return false;
}

OSW_UT(osw_stats_chan)
{
    struct osw_tlv s0;
    MEMZERO(s0);
    struct osw_tlv s1;
    MEMZERO(s1);
    struct osw_tlv ret;
    MEMZERO(ret);
    osw_stats_ut_put_chan(&s0, "phy0", 2412, OSW_STATS_CHAN_CNT_MSEC, 0, 0, 0, 0, 0);
    osw_stats_ut_put_chan(&s0, "phy0", 2422, OSW_STATS_CHAN_CNT_MSEC, 0, 0, 0, 0, 0);
    osw_stats_ut_put_chan(&s1, "phy0", 2412, OSW_STATS_CHAN_CNT_MSEC, 1000, 50, 100, 50, 500);
    osw_stats_ut_put_chan(&s1, "phy0", 2422, OSW_STATS_CHAN_CNT_MSEC, 100, 0, 0, 0, 50);
    const double t0 = 0.0;
    const double t1 = 1.0;
    const double t2 = 2.0;
    struct osw_stats stats;
    MEMZERO(stats);
    struct osw_stats_subscriber *sub = osw_stats_subscriber_alloc();
    osw_stats_init(&stats);
    osw_stats_subscriber_set_chan(sub, true);
    osw_stats_subscriber_set_poll_seconds(sub, 1.0);
    osw_stats_subscriber_set_report_seconds(sub, 1.0);
    osw_stats_subscriber_set_report_fn(sub, osw_stats_ut_report_cb, &ret);
    osw_stats_ut_run_expect(&stats, t0, true, false, OSW_STATS_CHAN);
    osw_stats_register_subscriber__(&stats, sub);
    osw_stats_ut_run_expect(&stats, t0, false, false, OSW_STATS_CHAN);
    osw_stats_ut_run_expect(&stats, t1, false, true, OSW_STATS_CHAN);
    osw_stats_put_tlv(&stats, &s0);
    osw_stats_ut_run_expect(&stats, t1, false, false, OSW_STATS_CHAN);
    assert(ret.used == 0);
    osw_stats_put_tlv(&stats, &s1);
    osw_stats_ut_run_expect(&stats, t2, false, true, OSW_STATS_CHAN);
    assert(ret.used > 0);
    assert(osw_stats_ut_tlv_expect_chan(&ret, "phy0", 2412, OSW_STATS_CHAN_CNT_MSEC, 1000, 50, 100, 50, 500) == true);
    assert(osw_stats_ut_tlv_expect_chan(&ret, "phy0", 2422, OSW_STATS_CHAN_CNT_MSEC, 100, 0, 0, 0, 50) == true);
    assert(osw_stats_ut_tlv_expect_chan(&ret, "phy0", 2412, OSW_STATS_CHAN_CNT_PERCENT, 1000, 5, 10, 5, 50) == true);
    assert(osw_stats_ut_tlv_expect_chan(&ret, "phy0", 2422, OSW_STATS_CHAN_CNT_PERCENT, 100, 0, 0, 0, 50) == true);
    osw_stats_unregister_subscriber(sub);
    osw_tlv_fini(&s0);
    osw_tlv_fini(&s1);
    osw_tlv_fini(&ret);
}

OSW_UT(osw_stats_chan_percent)
{
    struct osw_tlv s0;
    MEMZERO(s0);
    struct osw_tlv ret;
    MEMZERO(ret);
    osw_stats_ut_put_chan(&s0, "phy0", 2412, OSW_STATS_CHAN_CNT_PERCENT, 1000, 10, 40, 30, 60);
    osw_stats_ut_put_chan(&s0, "phy0", 2422, OSW_STATS_CHAN_CNT_PERCENT, 500, 0, 0, 0, 10);
    const double t0 = 0.0;
    const double t1 = 1.0;
    const double t1_1 = 1.1;
    struct osw_stats stats;
    MEMZERO(stats);
    struct osw_stats_subscriber *sub = osw_stats_subscriber_alloc();
    osw_stats_init(&stats);
    osw_stats_subscriber_set_chan(sub, true);
    osw_stats_subscriber_set_poll_seconds(sub, 1.0);
    osw_stats_subscriber_set_report_seconds(sub, 1.0);
    osw_stats_subscriber_set_report_fn(sub, osw_stats_ut_report_cb, &ret);
    osw_stats_ut_run_expect(&stats, t0, true, false, OSW_STATS_CHAN);
    osw_stats_register_subscriber__(&stats, sub);
    osw_stats_ut_run_expect(&stats, t0, false, false, OSW_STATS_CHAN);
    osw_stats_put_tlv(&stats, &s0);
    osw_stats_ut_run_expect(&stats, t1, false, true, OSW_STATS_CHAN);
    osw_stats_polling_done(&stats, t1_1);
    osw_stats_ut_run_expect(&stats, t1_1, false, false, 0);
    assert(ret.used > 0);
    /* active is expected 0/non-existent because there's only 1 sample of
     * percentages, but policy is expecting at least 2 samples for first usable
     * data sample for most fields */
    assert(osw_stats_ut_tlv_expect_chan(&ret, "phy0", 2412, OSW_STATS_CHAN_CNT_PERCENT, 0, 10, 40, 30, 60) == true);
    assert(osw_stats_ut_tlv_expect_chan(&ret, "phy0", 2422, OSW_STATS_CHAN_CNT_PERCENT, 0, 0, 0, 0, 10) == true);
    osw_stats_unregister_subscriber(sub);
    osw_tlv_fini(&s0);
    osw_tlv_fini(&ret);
}

OSW_UT(osw_stats_chan_percent_disabled)
{
    struct osw_tlv s0;
    MEMZERO(s0);
    struct osw_tlv ret;
    MEMZERO(ret);
    osw_stats_ut_put_chan(&s0, "phy0", 2412, OSW_STATS_CHAN_CNT_PERCENT, 1000, 10, 40, 30, 60);
    osw_stats_ut_put_chan(&s0, "phy0", 2422, OSW_STATS_CHAN_CNT_PERCENT, 500, 0, 0, 0, 10);
    const double t0 = 0.0;
    const double t1 = 1.0;
    struct osw_stats stats;
    MEMZERO(stats);
    struct osw_stats_subscriber *sub = osw_stats_subscriber_alloc();
    osw_stats_init(&stats);
    osw_stats_subscriber_set_chan(sub, false);
    osw_stats_subscriber_set_poll_seconds(sub, 1.0);
    osw_stats_subscriber_set_report_seconds(sub, 1.0);
    osw_stats_subscriber_set_report_fn(sub, osw_stats_ut_report_cb, &ret);
    osw_stats_ut_run_expect(&stats, t0, true, false, OSW_STATS_CHAN);
    osw_stats_register_subscriber__(&stats, sub);
    osw_stats_ut_run_expect(&stats, t0, false, false, OSW_STATS_CHAN);
    osw_stats_put_tlv(&stats, &s0);
    osw_stats_ut_run_expect(&stats, t1, false, true, OSW_STATS_CHAN);
    assert(ret.used == 0);
    osw_stats_unregister_subscriber(sub);
    osw_tlv_fini(&s0);
    osw_tlv_fini(&ret);
}

OSW_UT(osw_stats_bucket_expiry)
{
    struct osw_tlv s0;
    MEMZERO(s0);
    osw_stats_ut_put_chan(&s0, "phy0", 2412, OSW_STATS_CHAN_CNT_MSEC, 0, 0, 0, 0, 0);
    struct osw_stats stats;
    MEMZERO(stats);
    struct osw_stats_subscriber *sub = osw_stats_subscriber_alloc();
    osw_stats_init(&stats);
    osw_stats_subscriber_set_chan(sub, true);
    osw_stats_subscriber_set_poll_seconds(sub, 1.0);
    osw_stats_subscriber_set_report_seconds(sub, 1.0);
    osw_stats_register_subscriber__(&stats, sub);
    osw_stats_put_tlv(&stats, &s0);
    assert(ds_tree_is_empty(&sub->buckets[OSW_STATS_CHAN]) == false);
    osw_stats_subscriber_flush(sub);
    assert(ds_tree_is_empty(&sub->buckets[OSW_STATS_CHAN]) == false);
    {
        /* No more stats will be put into the bucket now. It
         * should expire after N number of flushes.
         */
        size_t n = 0;
        while (ds_tree_is_empty(&sub->buckets[OSW_STATS_CHAN]) == false) {
            osw_stats_subscriber_flush(sub);
            n++;
        }
        assert(n == OSW_STATS_BUCKET_EXPIRE_AFTER_N_PERIODS);
    }
    osw_stats_unregister_subscriber(sub);
    osw_tlv_fini(&s0);
}

static void
osw_stats_ut_put_bss(struct osw_tlv *t,
                     const char *phy_name,
                     const struct osw_hwaddr *bssid,
                     uint32_t freq,
                     uint32_t width,
                     const char *ssid,
                     const void *ies,
                     size_t ies_len)
{
    size_t s = osw_tlv_put_nested(t, OSW_STATS_BSS_SCAN);
    osw_tlv_put_string(t, OSW_STATS_BSS_SCAN_PHY_NAME, phy_name);
    osw_tlv_put_hwaddr(t, OSW_STATS_BSS_SCAN_MAC_ADDRESS, bssid);
    osw_tlv_put_u32(t, OSW_STATS_BSS_SCAN_FREQ_MHZ, freq);
    osw_tlv_put_u32(t, OSW_STATS_BSS_SCAN_WIDTH_MHZ, width);
    osw_tlv_put_buf(t, OSW_STATS_BSS_SCAN_SSID, ssid, strlen(ssid));
    osw_tlv_put_buf(t, OSW_STATS_BSS_SCAN_IES, ies, ies_len);
    osw_tlv_end_nested(t, s);
}

static bool
osw_stats_ut_tlv_expect_bss(const struct osw_tlv *src,
                            const char *phy_name,
                            const struct osw_hwaddr *bssid,
                            const uint32_t freq,
                            const uint32_t width,
                            const char *ssid,
                            const void *ies,
                            size_t ies_len)
{
    const struct osw_stats_defs *defs = osw_stats_defs_lookup(OSW_STATS_BSS_SCAN);
    assert(defs != NULL);

    const struct osw_tlv_hdr *i;
    const void *data = src->data;
    size_t len = src->used;

    osw_tlv_for_each(i, data, len) {
        assert(i->id == OSW_STATS_BSS_SCAN);

        const struct osw_tlv_hdr *tb[OSW_STATS_CHAN_MAX__];
        MEMZERO(tb);
        const size_t left = osw_tlv_parse(osw_tlv_get_data(i), i->len, defs->tpolicy, tb, OSW_STATS_BSS_SCAN_MAX__);
        assert(left == 0);

        assert(tb[OSW_STATS_BSS_SCAN_PHY_NAME] != NULL);
        assert(tb[OSW_STATS_BSS_SCAN_MAC_ADDRESS] != NULL);
        assert(tb[OSW_STATS_BSS_SCAN_FREQ_MHZ] != NULL);
        assert(tb[OSW_STATS_BSS_SCAN_WIDTH_MHZ] != NULL);
        assert(tb[OSW_STATS_BSS_SCAN_SSID] != NULL);
        assert(tb[OSW_STATS_BSS_SCAN_IES] != NULL);

        if (strcmp(osw_tlv_get_string(tb[OSW_STATS_CHAN_PHY_NAME]), phy_name) != 0) continue;
        if (osw_tlv_get_u32(tb[OSW_STATS_BSS_SCAN_FREQ_MHZ]) != freq) continue;
        if (osw_tlv_get_u32(tb[OSW_STATS_BSS_SCAN_WIDTH_MHZ]) != width) continue;
        if (memcmp(osw_tlv_get_data(tb[OSW_STATS_BSS_SCAN_MAC_ADDRESS]), bssid, sizeof(bssid->octet)) != 0) continue;
        if (memcmp(osw_tlv_get_data(tb[OSW_STATS_BSS_SCAN_SSID]), ssid, strlen(ssid)) != 0) continue;
        if (memcmp(osw_tlv_get_data(tb[OSW_STATS_BSS_SCAN_IES]), ies, ies_len) != 0) continue;

        return true;
    }

    return false;
}

OSW_UT(osw_stats_bss_scan)
{
    struct osw_tlv s0;
    MEMZERO(s0);
    struct osw_tlv ret;
    MEMZERO(ret);
    const struct osw_hwaddr addr1 = { .octet = {1} };
    const struct osw_hwaddr addr2 = { .octet = {2} };
    osw_stats_ut_put_bss(&s0, "phy0", &addr1, 2412, 20, "fgsfds", "hell", 4);
    osw_stats_ut_put_bss(&s0, "phy1", &addr2, 5180, 80, "wutwut", "12", 2);
    const double t0 = 0.0;
    const double t1 = 1.0;
    const double t1_1 = 1.1;
    struct osw_stats stats;
    MEMZERO(stats);
    struct osw_stats_subscriber *sub = osw_stats_subscriber_alloc();
    osw_stats_init(&stats);
    osw_stats_subscriber_set_bss(sub, true);
    osw_stats_subscriber_set_poll_seconds(sub, 1.0);
    osw_stats_subscriber_set_report_seconds(sub, 1.0);
    osw_stats_subscriber_set_report_fn(sub, osw_stats_ut_report_cb, &ret);
    osw_stats_ut_run_expect(&stats, t0, true, false, OSW_STATS_BSS_SCAN);
    osw_stats_register_subscriber__(&stats, sub);
    osw_stats_ut_run_expect(&stats, t0, false, false, OSW_STATS_BSS_SCAN);
    osw_stats_put_tlv(&stats, &s0);
    osw_stats_ut_run_expect(&stats, t1, false, true, OSW_STATS_BSS_SCAN);
    osw_stats_polling_done(&stats, t1_1);
    osw_stats_ut_run_expect(&stats, t1_1, false, false, 0);
    assert(ret.used > 0);
    assert(osw_stats_ut_tlv_expect_bss(&ret, "phy0", &addr1, 2412, 20, "fgsfds", "hell", 4) == true);
    assert(osw_stats_ut_tlv_expect_bss(&ret, "phy1", &addr2, 5180, 80, "wutwut", "12", 2) == true);
    osw_stats_unregister_subscriber(sub);
    osw_tlv_fini(&s0);
    osw_tlv_fini(&ret);
}

OSW_UT(osw_stats_bss_scan_report)
{
    struct osw_tlv s0;
    MEMZERO(s0);
    struct osw_tlv ret;
    MEMZERO(ret);
    const struct osw_hwaddr addr1 = { .octet = {1} };
    const struct osw_hwaddr addr2 = { .octet = {2} };
    osw_stats_ut_put_bss(&s0, "phy0", &addr1, 2412, 20, "fgsfds", "hell", 4);
    osw_stats_ut_put_bss(&s0, "phy1", &addr2, 5180, 80, "wutwut", "12", 2);
    const double t0 = 0.0;
    const double t1 = 1.0;
    const double t2 = 3.0;
    const double t3 = 5.0;
    const double t3_1 = 5.1;
    const double t4 = 6.0;
    const double t5 = 8.0;
    const double t6 = 10.0;
    const double t6_1 = 10.1;
    struct osw_stats stats;
    MEMZERO(stats);
    struct osw_stats_subscriber *sub = osw_stats_subscriber_alloc();
    osw_stats_init(&stats);
    osw_stats_subscriber_set_bss(sub, true);
    osw_stats_subscriber_set_poll_seconds(sub, 1.0);
    osw_stats_subscriber_set_report_seconds(sub, 5);
    osw_stats_subscriber_set_report_fn(sub, osw_stats_ut_report_cb, &ret);
    osw_stats_ut_run_expect(&stats, t0, true, false, OSW_STATS_BSS_SCAN);
    osw_stats_register_subscriber__(&stats, sub);
    osw_stats_ut_run_expect(&stats, t0, false, false, OSW_STATS_BSS_SCAN);
    osw_stats_put_tlv(&stats, &s0);
    osw_stats_ut_run_expect(&stats, t1, false, true, OSW_STATS_BSS_SCAN);
    assert(ret.used == 0);
    osw_stats_ut_run_expect(&stats, t2, false, true, OSW_STATS_BSS_SCAN);
    assert(ret.used == 0);
    osw_stats_ut_run_expect(&stats, t3, false, true, OSW_STATS_BSS_SCAN);
    osw_stats_polling_done(&stats, t3_1);
    osw_stats_ut_run_expect(&stats, t3_1, false, false, 0);
    assert(ret.used > 0);
    assert(osw_stats_ut_tlv_expect_bss(&ret, "phy0", &addr1, 2412, 20, "fgsfds", "hell", 4) == true);
    assert(osw_stats_ut_tlv_expect_bss(&ret, "phy1", &addr2, 5180, 80, "wutwut", "12", 2) == true);
    osw_tlv_fini(&ret);
    assert(ret.used == 0);
    osw_stats_put_tlv(&stats, &s0);
    assert(ret.used == 0);
    osw_stats_ut_run_expect(&stats, t4, false, true, OSW_STATS_BSS_SCAN);
    assert(ret.used == 0);
    osw_stats_ut_run_expect(&stats, t5, false, true, OSW_STATS_BSS_SCAN);
    assert(ret.used == 0);
    osw_stats_ut_run_expect(&stats, t6, false, true, OSW_STATS_BSS_SCAN);
    osw_stats_polling_done(&stats, t6_1);
    osw_stats_ut_run_expect(&stats, t6_1, false, false, 0);
    assert(ret.used > 0);
    assert(osw_stats_ut_tlv_expect_bss(&ret, "phy0", &addr1, 2412, 20, "fgsfds", "hell", 4) == true);
    assert(osw_stats_ut_tlv_expect_bss(&ret, "phy1", &addr2, 5180, 80, "wutwut", "12", 2) == true);
    osw_stats_unregister_subscriber(sub);
    osw_tlv_fini(&s0);
    osw_tlv_fini(&ret);
}

OSW_UT(osw_stats_bss_scan_report_nopoll)
{
    struct osw_tlv s0;
    MEMZERO(s0);
    struct osw_tlv ret;
    MEMZERO(ret);
    const struct osw_hwaddr addr1 = { .octet = {1} };
    const struct osw_hwaddr addr2 = { .octet = {2} };
    osw_stats_ut_put_bss(&s0, "phy0", &addr1, 2412, 20, "fgsfds", "hell", 4);
    osw_stats_ut_put_bss(&s0, "phy1", &addr2, 5180, 80, "wutwut", "12", 2);
    const double t0 = 0.0;
    const double t1 = 1.0;
    const double t2 = 3.0;
    const double t3 = 5.0;
    const double t4 = 6.0;
    const double t5 = 8.0;
    const double t6 = 10.0;
    struct osw_stats stats;
    MEMZERO(stats);
    struct osw_stats_subscriber *sub = osw_stats_subscriber_alloc();
    osw_stats_init(&stats);
    osw_stats_subscriber_set_bss(sub, true);
    osw_stats_subscriber_set_report_seconds(sub, 5);
    osw_stats_subscriber_set_report_fn(sub, osw_stats_ut_report_cb, &ret);
    osw_stats_ut_run_expect(&stats, t0, true, false, OSW_STATS_BSS_SCAN);
    osw_stats_register_subscriber__(&stats, sub);
    osw_stats_ut_run_expect(&stats, t0, false, false, OSW_STATS_BSS_SCAN);
    osw_stats_put_tlv(&stats, &s0);
    osw_stats_ut_run_expect(&stats, t1, false, false, OSW_STATS_BSS_SCAN);
    assert(ret.used == 0);
    osw_stats_ut_run_expect(&stats, t2, false, false, OSW_STATS_BSS_SCAN);
    assert(ret.used == 0);
    osw_stats_ut_run_expect(&stats, t3, false, false, OSW_STATS_BSS_SCAN);
    assert(ret.used > 0);
    assert(osw_stats_ut_tlv_expect_bss(&ret, "phy0", &addr1, 2412, 20, "fgsfds", "hell", 4) == true);
    assert(osw_stats_ut_tlv_expect_bss(&ret, "phy1", &addr2, 5180, 80, "wutwut", "12", 2) == true);
    osw_tlv_fini(&ret);
    assert(ret.used == 0);
    osw_stats_put_tlv(&stats, &s0);
    assert(ret.used == 0);
    osw_stats_ut_run_expect(&stats, t4, false, false, OSW_STATS_BSS_SCAN);
    assert(ret.used == 0);
    osw_stats_ut_run_expect(&stats, t5, false, false, OSW_STATS_BSS_SCAN);
    assert(ret.used == 0);
    osw_stats_ut_run_expect(&stats, t6, false, false, OSW_STATS_BSS_SCAN);
    assert(ret.used > 0);
    assert(osw_stats_ut_tlv_expect_bss(&ret, "phy0", &addr1, 2412, 20, "fgsfds", "hell", 4) == true);
    assert(osw_stats_ut_tlv_expect_bss(&ret, "phy1", &addr2, 5180, 80, "wutwut", "12", 2) == true);
    osw_stats_unregister_subscriber(sub);
    osw_tlv_fini(&s0);
    osw_tlv_fini(&ret);
}

OSW_UT(osw_stats_poll_coalescing)
{
    const double t0 = 0.0;
    const double t1 = 6.0;
    struct osw_stats stats;
    MEMZERO(stats);
    struct osw_stats_subscriber *sub1 = osw_stats_subscriber_alloc();
    struct osw_stats_subscriber *sub2 = osw_stats_subscriber_alloc();
    osw_stats_init(&stats);
    osw_stats_subscriber_set_poll_seconds(sub1, 5);
    osw_stats_subscriber_set_poll_seconds(sub2, 5);
    osw_stats_subscriber_set_chan(sub1, true);
    osw_stats_subscriber_set_bss(sub2, true);
    osw_stats_register_subscriber__(&stats, sub1);
    osw_stats_register_subscriber__(&stats, sub2);
    {
        double r = -1;
        unsigned int mask = 0;
        bool poll = false;
        osw_stats_run_subscribers(&stats, t0, &poll, &mask, &r);
        assert(poll == false);
        assert(mask == 0);
        assert(r == 5.0);
    }
    assert(sub1->poller != NULL);
    assert(sub1->poller == sub2->poller);
    {
        size_t n = 0;
        struct osw_stats_poller *p;
        ds_tree_foreach(&stats.pollers, p) n++;
        assert(n == 1);
    }
    {
        double r = -1;
        unsigned int mask = 0;
        bool poll = false;
        osw_stats_run_subscribers(&stats, t1, &poll, &mask, &r);
        assert(poll == true);
        assert(mask == ((1 << OSW_STATS_CHAN) |
                        (1 << OSW_STATS_BSS_SCAN)));
        assert(r == 4.0);
    }
    osw_stats_unregister_subscriber(sub1);
    osw_stats_unregister_subscriber(sub2);
}

OSW_UT(osw_stats_poll_no_coalescing)
{
    const double t0 = 0.0;
    const double t1 = 5.0;
    const double t2 = 7.0;
    struct osw_stats stats;
    MEMZERO(stats);
    struct osw_stats_subscriber *sub1 = osw_stats_subscriber_alloc();
    struct osw_stats_subscriber *sub2 = osw_stats_subscriber_alloc();
    osw_stats_init(&stats);
    osw_stats_subscriber_set_poll_seconds(sub1, 6);
    osw_stats_subscriber_set_poll_seconds(sub2, 4);
    osw_stats_subscriber_set_chan(sub1, true);
    osw_stats_subscriber_set_bss(sub2, true);
    osw_stats_register_subscriber__(&stats, sub1);
    osw_stats_register_subscriber__(&stats, sub2);
    {
        double r = -1;
        unsigned int mask = 0;
        bool poll = false;
        osw_stats_run_subscribers(&stats, t0, &poll, &mask, &r);
        assert(poll == false);
        assert(mask == 0);
        assert(r == 4.0);
    }
    assert(sub1->poller != NULL);
    assert(sub2->poller != NULL);
    assert(sub1->poller != sub2->poller);
    {
        size_t n = 0;
        struct osw_stats_poller *p;
        ds_tree_foreach(&stats.pollers, p) n++;
        assert(n == 2);
    }
    {
        double r = -1;
        unsigned int mask = 0;
        bool poll = false;
        osw_stats_run_subscribers(&stats, t1, &poll, &mask, &r);
        assert(poll == true);
        assert(mask == (1 << OSW_STATS_BSS_SCAN));
        assert(r == 1.0);
    }
    {
        double r = -1;
        unsigned int mask = 0;
        bool poll = false;
        osw_stats_run_subscribers(&stats, t2, &poll, &mask, &r);
        assert(poll == true);
        assert(mask == (1 << OSW_STATS_CHAN));
        assert(r == 1.0);
    }
    osw_stats_unregister_subscriber(sub1);
    osw_stats_unregister_subscriber(sub2);
}
