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

/* opensync */
#include <ds_dlist.h>
#include <ds_tree.h>
#include <stdbool.h>
#include <memutil.h>
#include <const.h>
#include <os_time.h>
#include <log.h>
#include <util.h>
#include <os.h>

/* osw */
#include <osw_tlv.h>
#include <osw_tlv_merge.h>
#include <osw_stats.h>
#include <osw_stats_subscriber.h>
#include <osw_stats_defs.h>
#include <osw_state.h>
#include <osw_util.h>
#include <osw_module.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_mux.h>

// FIXME: Add denial of service detection/prevention against number of buckets per subscriber.

/* If a bucket is not filled within N sampling
 * periods then it should be freed up.
 */
#define OSW_STATS_BUCKET_EXPIRE_AFTER_N_PERIODS 30
#define OSW_STATS_POLL_MAX_GAP (1 / 256.0)

#define LOG_PREFIX(fmt, ...) \
    "osw: stats: " fmt, ##__VA_ARGS__

static void
set_enum_bit(unsigned long *mask, int i, bool set)
{
    const unsigned long bit = (1 << i);
    *mask &= ~bit;
    if (set == true) *mask |= bit;
}

static int
osw_stats_double_cmp(const void *a, const void *b)
{
    const double *x = a;
    const double *y = b;
    if (*x < *y) return -1;
    else if (*x > *y) return 1;
    else return 0;
}

struct osw_stats_bucket {
    struct ds_tree_node node;
    struct osw_tlv key;
    struct osw_tlv data;
    struct osw_tlv last;
    unsigned int idle_periods;
};

struct osw_stats_poller {
    struct ds_tree_node node;
    struct ds_dlist subscribers;
    double report_at;
    double seconds;
};

struct osw_stats_subscriber {
    struct ds_dlist_node node;
    struct ds_dlist_node poll_node;
    struct osw_stats *stats;
    struct osw_stats_poller *poller;
    struct ds_tree buckets[OSW_STATS_MAX__];
    double report_at;
    double report_seconds;
    double poll_seconds;
    bool postponed;
    unsigned long stats_mask;
    osw_stats_subscriber_report_fn_t *report_fn;
    void *report_priv;
};

struct osw_stats {
    struct osw_state_observer state_obs;
    struct ds_dlist subscribers;
    struct ds_tree pollers;
    struct osw_timer work;

    double postponed_at;
    double started_at;
    double reported_at;
    bool polling;
};

/* FIXME: This should be stored/allocated in OSW_MODULE().
 * The last place to untagle this global is osw_stats_put()
 * and integration against osw_drv core.
 */
static struct osw_stats g_osw_stats;

static void
osw_stats_reschedule(struct osw_stats *stats)
{
    if (stats == NULL) return;
    osw_timer_arm_at_nsec(&stats->work, 0);
}

static void
osw_stats_polling_done(struct osw_stats *stats,
                       const double now)
{
    if (stats->polling == false) {
        return;
    }

    if (stats->postponed_at != 0) {
        const double duration = (now - stats->postponed_at);
        LOGT("osw: stats: postponed some reports for %lf", duration);
        stats->postponed_at = 0;
    }

    stats->polling = false;
}

static void
osw_stats_polling_update(struct osw_stats *stats,
                         const double now,
                         const bool is_report)
{
    if (stats->polling == false) {
        return;
    }

    const double gap = (now - stats->reported_at);
    const double margin = OSW_STATS_POLL_MAX_GAP;
    const bool still_hot = (gap < margin);
    if (still_hot) {
        if (is_report) stats->reported_at = now;
        return;
    }

    osw_stats_polling_done(stats, now);
}

static void
osw_stats_polling_start(struct osw_stats *stats,
                        const double now)
{
    stats->started_at = now;
    stats->reported_at = now;
    stats->polling = true;
}

static bool
osw_stats_bucket_is_expired(struct osw_stats_bucket *b)
{
    return b->idle_periods >= OSW_STATS_BUCKET_EXPIRE_AFTER_N_PERIODS;
}

static void
osw_stats_bucket_expire_work(struct osw_stats_bucket *b)
{
    if (b->data.used > 0) {
        b->idle_periods = 0;
    }
    else {
        b->idle_periods++;
    }
}

static void
osw_stats_bucket_free(struct ds_tree *buckets, struct osw_stats_bucket *b)
{
    if (b == NULL) return;
    if (buckets != NULL) ds_tree_remove(buckets, b);
    osw_tlv_fini(&b->key);
    osw_tlv_fini(&b->data);
    osw_tlv_fini(&b->last);
    FREE(b);
}

static bool
osw_stats_first_to_bool(const bool inherited,
                        const enum osw_tlv_merge_first_policy f)
{
    switch (f) {
        case OSW_TLV_INHERIT_FIRST: return inherited;
        case OSW_TLV_DELTA_AGAINST_ZERO: return true;
        case OSW_TLV_TWO_SAMPLES_MINIMUM: return false;
    }
    assert(0);
    return true;
}

static enum osw_tlv_merge_result
osw_stats_subscriber_put(struct osw_stats_subscriber *sub,
                         const enum osw_stats_id id,
                         const struct osw_stats_defs *defs,
                         const struct osw_tlv *key,
                         const void *data,
                         const size_t len)
{
    const int bit = 1 << id;
    if ((sub->stats_mask & bit) == 0) return OSW_TLV_MERGE_OK;

    struct ds_tree *tree = &sub->buckets[id];
    struct osw_stats_bucket *bucket = ds_tree_find(tree, key);
    const bool first = osw_stats_first_to_bool(true, defs->first);

    if (bucket == NULL) {
        bucket = CALLOC(1, sizeof(*bucket));
        osw_tlv_copy(&bucket->key, key);
        ds_tree_insert(tree, bucket, &bucket->key);
    }

    struct osw_tlv backup;
    MEMZERO(backup);
    osw_tlv_copy(&backup, &bucket->data);

    const enum osw_tlv_merge_result r = osw_tlv_merge(&bucket->data,
                                                      &bucket->last,
                                                      data, len,
                                                      first,
                                                      defs->tpolicy,
                                                      defs->mpolicy,
                                                      defs->size);
    switch (r) {
        case OSW_TLV_MERGE_OK:
            break;
        case OSW_TLV_MERGE_ERR:
            break;
        case OSW_TLV_MERGE_ERR_UNDERFLOW:
            /* Some absolute TLVs need 2 samples to compute
             * the delta. However some require only 1
             * because they can be compared against "0". For
             * example re-association and station counters
             * can be compared against 0. To handle the
             * latter clear out last samples and try again.
             */
            osw_tlv_fini(&bucket->data);
            osw_tlv_fini(&bucket->last);
            bucket->data = backup;
            MEMZERO(backup);
            osw_tlv_merge(&bucket->data,
                          &bucket->last,
                          data, len,
                          first,
                          defs->tpolicy,
                          defs->mpolicy,
                          defs->size);

            break;
    }
    osw_tlv_fini(&backup);
    return r;
}

void
osw_stats_subscriber_flush(struct osw_stats_subscriber *sub)
{
    const size_t n = ARRAY_SIZE(sub->buckets);
    size_t i;

    /* FIXME: Before running this loop (and calling
     * report_fn) it would be useful to run a set of sanity,
     * and fixup operations. For example if OSW_STATS_CHAN
     * buckets are present, and they have NOISE_FLOOR_DBM
     * defined, then it could be used to synthesize RSSI_DBM
     * in OSW_STATS_BSS_SCAN buckets that provided only
     * SNR_DB, such that the consumer (report_fn) can get
     * more data, even if the underlying (raw) reports were
     * incomplete and would require aggregating. IOW This
     * could offload the fixup phase to here, instead of
     * per-subscriber.
     */

    for (i = 0; i < n; i++) {
        const enum osw_stats_id id = i;
        const struct osw_stats_defs *defs = osw_stats_defs_lookup(id);
        struct ds_tree *buckets = &sub->buckets[i];
        struct osw_stats_bucket *b;
        struct osw_stats_bucket *tmp;

        ds_tree_foreach_safe(buckets, b, tmp) {
            osw_stats_bucket_expire_work(b);
            if (osw_stats_bucket_is_expired(b) == true) {
                osw_stats_bucket_free(buckets, b);
                continue;
            }

            if (b->data.used == 0) {
                continue;
            }

            if (sub->report_fn != NULL) {
                if (defs != NULL &&
                    defs->postprocess_fn != NULL) {
                    defs->postprocess_fn(&b->data);
                }
                sub->report_fn(id, &b->data, &b->last, sub->report_priv);
            }

            osw_tlv_reset(&b->data);
        }
    }
}

static void
osw_stats_subscriber_free_buckets(struct osw_stats_subscriber *sub)
{
    const size_t n = ARRAY_SIZE(sub->buckets);
    size_t i;

    for (i = 0; i < n; i++) {
        struct ds_tree *buckets = &sub->buckets[i];
        struct osw_stats_bucket *tmp;
        struct osw_stats_bucket *b;

        ds_tree_foreach_safe(buckets, b, tmp) {
            osw_stats_bucket_free(buckets, b);
        }
    }
}

static int
memcmp_safe(const void *a, const void *b, size_t alen, size_t blen)
{
    int r = memcmp(a, b, alen > blen ? blen : alen);
    if (alen == blen) return r;
    if (r != 0) return r;
    if (alen > blen) return 1;
    if (blen > alen) return -1;
    return 0;
}

static int
osw_tlv_cmp_cb(const void *a, const void *b)
{
    const struct osw_tlv *x = a;
    const struct osw_tlv *y = b;
    return memcmp_safe(x->data, y->data, x->used, y->used);
}

struct osw_stats_subscriber *
osw_stats_subscriber_alloc(void)
{
    struct osw_stats_subscriber *sub = CALLOC(1, sizeof(*sub));
    size_t i;
    for (i = 0; i < ARRAY_SIZE(sub->buckets); i++) {
        ds_tree_init(&sub->buckets[i],
                     osw_tlv_cmp_cb,
                     struct osw_stats_bucket,
                     node);
    }
    return sub;
}

void
osw_stats_subscriber_free(struct osw_stats_subscriber *sub)
{
    if (sub == NULL) return;
    assert(sub->stats == NULL);
    assert(sub->poller == NULL);
    osw_stats_subscriber_free_buckets(sub);
    FREE(sub);
}

void
osw_stats_subscriber_set_report_fn(struct osw_stats_subscriber *sub,
                                   osw_stats_subscriber_report_fn_t *fn,
                                   void *priv)
{
    sub->report_fn = fn;
    sub->report_priv = priv;
    osw_stats_reschedule(sub->stats);
}

void
osw_stats_subscriber_set_report_seconds(struct osw_stats_subscriber *sub,
                                        double seconds)
{
    sub->report_seconds = seconds;
    osw_stats_reschedule(sub->stats);
}

void
osw_stats_subscriber_set_poll_seconds(struct osw_stats_subscriber *sub,
                                      double seconds)
{
    sub->poll_seconds = seconds;
    osw_stats_reschedule(sub->stats);
}

void
osw_stats_subscriber_set_phy(struct osw_stats_subscriber *sub,
                             bool enabled)
{
    set_enum_bit(&sub->stats_mask, OSW_STATS_PHY, enabled);
    osw_stats_reschedule(sub->stats);
}

void
osw_stats_subscriber_set_vif(struct osw_stats_subscriber *sub,
                             bool enabled)
{
    set_enum_bit(&sub->stats_mask, OSW_STATS_VIF, enabled);
    osw_stats_reschedule(sub->stats);
}

void
osw_stats_subscriber_set_sta(struct osw_stats_subscriber *sub,
                             bool enabled)
{
    set_enum_bit(&sub->stats_mask, OSW_STATS_STA, enabled);
    osw_stats_reschedule(sub->stats);
}

void
osw_stats_subscriber_set_chan(struct osw_stats_subscriber *sub,
                              bool enabled)
{
    set_enum_bit(&sub->stats_mask, OSW_STATS_CHAN, enabled);
    osw_stats_reschedule(sub->stats);
}

void
osw_stats_subscriber_set_bss(struct osw_stats_subscriber *sub,
                              bool enabled)
{
    set_enum_bit(&sub->stats_mask, OSW_STATS_BSS_SCAN, enabled);
    osw_stats_reschedule(sub->stats);
}

static void
osw_stats_run_subscribers(struct osw_stats *stats,
                          const double now,
                          bool *poll,
                          unsigned int *stats_mask,
                          double *remaining_seconds)
{
    struct osw_stats_subscriber *sub;
    struct osw_stats_subscriber *ts;
    struct osw_stats_poller *p;
    struct osw_stats_poller *tp;

    ds_dlist_foreach(&stats->subscribers, sub) {
        p = sub->poller;
        if (p != NULL && p->seconds != sub->poll_seconds) {
            ds_dlist_remove(&p->subscribers, sub);
            sub->poller = NULL;
            p = NULL;
        }
        if (p == NULL && sub->poll_seconds > 0) {
            p = ds_tree_find(&stats->pollers, &sub->poll_seconds);
            if (p == NULL) {
                p = CALLOC(1, sizeof(*p));
                ds_dlist_init(&p->subscribers, struct osw_stats_subscriber, poll_node);
                p->seconds = sub->poll_seconds;
                osw_periodic_eval(&p->report_at,
                                  p->seconds,
                                  0,
                                  now);
                ds_tree_insert(&stats->pollers, p, &p->seconds);
            }
        }
        if (sub->poller == NULL && p != NULL) {
            ds_dlist_insert_tail(&p->subscribers, sub);
        }
        sub->poller = p;
    }

    *poll = false;
    *stats_mask = 0;

    ds_tree_foreach_safe(&stats->pollers, p, tp) {
        if (ds_dlist_is_empty(&p->subscribers) == true) {
            ds_tree_remove(&stats->pollers, p);
            FREE(p);
            continue;
        }

        const bool expired = osw_periodic_eval(&p->report_at,
                                               p->seconds,
                                               0,
                                               now);
        if (expired) {
            osw_stats_polling_start(stats, now);
            ds_dlist_foreach(&p->subscribers, sub) {
                *stats_mask |= sub->stats_mask;
            }
            *poll = true;
        }

        const double r = (p->report_at - now);
        osw_min_duration(remaining_seconds, r);
    }

    osw_stats_polling_update(stats, now, false);

    ds_dlist_foreach_safe(&stats->subscribers, sub, ts) {
        const bool expired = osw_periodic_eval(&sub->report_at, sub->report_seconds, 0, now);
        if (expired || sub->postponed) {
            const bool overrun = (expired && sub->postponed);
            if (stats->polling && !overrun) {
                const double postpone_for = OSW_STATS_POLL_MAX_GAP;
                if (stats->postponed_at == 0) {
                    stats->postponed_at = now;
                }
                sub->postponed = true;
                osw_min_duration(remaining_seconds, postpone_for);
                continue;
            }
            if (overrun) {
                LOGN("osw: stats: postponed report (%lf) were overrun, reporting now",
                     sub->report_seconds);
            }
            sub->postponed = false;
            osw_stats_subscriber_flush(sub);
        }

        if (sub->report_at == 0) {
            continue;
        }

        const double r = sub->report_at - now;
        osw_min_duration(remaining_seconds, r);
    }
}

void
osw_stats_run(bool *poll,
              unsigned int *stats_mask,
              double *remaining_seconds,
              const double now)
{
    osw_stats_run_subscribers(&g_osw_stats,
                              now,
                              poll,
                              stats_mask,
                              remaining_seconds);
}

static void
osw_stats_register_subscriber__(struct osw_stats *stats,
                                struct osw_stats_subscriber *sub)
{
    assert(sub->stats == NULL);
    ds_dlist_insert_tail(&stats->subscribers, sub);
    sub->stats = stats;
    osw_stats_reschedule(stats);
}

void
osw_stats_register_subscriber(struct osw_stats_subscriber *sub)
{
    return osw_stats_register_subscriber__(&g_osw_stats, sub);
}

void
osw_stats_unregister_subscriber(struct osw_stats_subscriber *sub)
{
    struct osw_stats *stats = sub->stats;
    struct osw_stats_poller *p = sub->poller;
    assert(stats != NULL);
    if (p != NULL) {
        ds_dlist_remove(&p->subscribers, sub);
        sub->poller = NULL;
    }
    ds_dlist_remove(&stats->subscribers, sub);
    osw_stats_reschedule(stats);
    sub->stats = NULL;
}

static bool
osw_stats_put_key_chan(struct osw_tlv *key,
                       const struct osw_stats_defs *defs,
                       const struct osw_tlv_hdr **tb)
{
    const struct osw_tlv_hdr *phy_name = tb[OSW_STATS_CHAN_PHY_NAME];
    const struct osw_tlv_hdr *freq_mhz = tb[OSW_STATS_CHAN_FREQ_MHZ];

    if (phy_name == NULL) return false;
    if (freq_mhz == NULL) return false;

    osw_tlv_put_copy(key, phy_name);
    osw_tlv_put_copy(key, freq_mhz);
    return true;
}

static bool
osw_stats_put_key_bss_scan(struct osw_tlv *key,
                           const struct osw_stats_defs *defs,
                           const struct osw_tlv_hdr **tb)
{
    const struct osw_tlv_hdr *phy_name = tb[OSW_STATS_BSS_SCAN_PHY_NAME];
    const struct osw_tlv_hdr *bssid = tb[OSW_STATS_BSS_SCAN_MAC_ADDRESS];

    if (phy_name == NULL) return false;
    if (bssid == NULL) return false;

    osw_tlv_put_copy(key, phy_name);
    osw_tlv_put_copy(key, bssid);
    return true;
}

static bool
osw_stats_put_key_sta(struct osw_tlv *key,
                      const struct osw_stats_defs *defs,
                      const struct osw_tlv_hdr **tb)
{
    const struct osw_tlv_hdr *phy_name = tb[OSW_STATS_STA_PHY_NAME];
    const struct osw_tlv_hdr *vif_name = tb[OSW_STATS_STA_VIF_NAME];
    const struct osw_tlv_hdr *sta_addr = tb[OSW_STATS_STA_MAC_ADDRESS];

    if (phy_name == NULL) return false;
    if (vif_name == NULL) return false;
    if (sta_addr == NULL) return false;

    osw_tlv_put_copy(key, phy_name);
    osw_tlv_put_copy(key, vif_name);
    osw_tlv_put_copy(key, sta_addr);
    return true;
}

static bool
osw_stats_put_key(struct osw_tlv *key,
                  const enum osw_stats_id id,
                  const struct osw_stats_defs *defs,
                  const struct osw_tlv_hdr **tb)
{
    switch (id) {
        case OSW_STATS_PHY: return false;
        case OSW_STATS_VIF: return false;
        case OSW_STATS_STA: return osw_stats_put_key_sta(key, defs, tb);
        case OSW_STATS_CHAN: return osw_stats_put_key_chan(key, defs, tb);
        case OSW_STATS_BSS_SCAN: return osw_stats_put_key_bss_scan(key, defs, tb);
        case OSW_STATS_MAX__: return false;
    }
    return false;
}

static void
osw_stats_log_prefix_sta(char **buf,
                         size_t *len,
                         const struct osw_tlv_hdr **tb)
{
    const struct osw_tlv_hdr *phy_name = tb[OSW_STATS_STA_PHY_NAME];
    const struct osw_tlv_hdr *vif_name = tb[OSW_STATS_STA_VIF_NAME];
    const struct osw_tlv_hdr *sta_addr = tb[OSW_STATS_STA_MAC_ADDRESS];
    const struct osw_hwaddr *addr = osw_tlv_get_data(sta_addr) ?: osw_hwaddr_zero();
    csnprintf(buf, len, LOG_PREFIX("sta: %s/%s/"OSW_HWADDR_FMT": ",
                osw_tlv_get_string(phy_name) ?: "",
                osw_tlv_get_string(vif_name) ?: "",
                OSW_HWADDR_ARG(addr)));
}

static void
osw_stats_log_prefix_chan(char **buf,
                          size_t *len,
                          const struct osw_tlv_hdr **tb)
{
    const struct osw_tlv_hdr *phy_name = tb[OSW_STATS_CHAN_PHY_NAME];
    const struct osw_tlv_hdr *freq_mhz = tb[OSW_STATS_CHAN_FREQ_MHZ];
    const uint32_t zero = 0;
    csnprintf(buf, len, LOG_PREFIX("chan: %s/%"PRIu32": ",
                osw_tlv_get_string(phy_name) ?: "",
                *(uint32_t *)(osw_tlv_get_data(freq_mhz) ?: &zero)));
}

static void
osw_stats_log_prefix_bss_scan(char **buf,
                              size_t *len,
                              const struct osw_tlv_hdr **tb)
{
    const struct osw_tlv_hdr *phy_name = tb[OSW_STATS_BSS_SCAN_PHY_NAME];
    const struct osw_tlv_hdr *bssid = tb[OSW_STATS_BSS_SCAN_MAC_ADDRESS];
    const struct osw_hwaddr *addr = osw_tlv_get_data(bssid) ?: osw_hwaddr_zero();
    csnprintf(buf, len, LOG_PREFIX("bss_scan: %s/"OSW_HWADDR_FMT": ",
                osw_tlv_get_string(phy_name) ?: "",
                OSW_HWADDR_ARG(addr)));
}

static void
osw_stats_log_prefix(char **buf,
                     size_t *len,
                     const enum osw_stats_id id,
                     const struct osw_tlv_hdr **tb)
{
    switch (id) {
        case OSW_STATS_PHY: return;
        case OSW_STATS_VIF: return;
        case OSW_STATS_STA: return osw_stats_log_prefix_sta(buf, len, tb);
        case OSW_STATS_CHAN: return osw_stats_log_prefix_chan(buf, len, tb);
        case OSW_STATS_BSS_SCAN: return osw_stats_log_prefix_bss_scan(buf, len, tb);
        case OSW_STATS_MAX__: return;
    }
}

static void
osw_stats_log(const enum osw_stats_id id,
              const struct osw_tlv_hdr **tb,
              log_severity_t sev,
              const char *fmt,
              ...)
{
    char buf[1024];
    char *p = buf;
    size_t len = sizeof(buf);
    osw_stats_log_prefix(&p, &len, id, tb);

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p, len, fmt, ap);
    va_end(ap);

    mlog(sev, MODULE_ID, "%s", buf);
}

static void
osw_stats_put_one(struct osw_stats *stats,
                  const struct osw_tlv_hdr *src)
{
    const enum osw_stats_id id = src->id;
    const struct osw_stats_defs *defs = osw_stats_defs_lookup(id);
    struct osw_stats_subscriber *sub;
    struct osw_tlv key = {0};
    const void *data = osw_tlv_get_data(src);
    const size_t len = src->len;

    if (defs == NULL) {
        // FIXME warning
        return;
    }

    const size_t size = defs->size;
    const struct osw_tlv_policy *p = defs->tpolicy;
    const struct osw_tlv_hdr *tb[size];

    memset(tb, 0, size * sizeof(*tb));
    const size_t remaining = osw_tlv_parse(data, len, p, tb, size);

    if (remaining != 0) {
        // FIXME warning
    }


    if (osw_stats_put_key(&key, id, defs, tb) == false) {
        // FIXME warning
        return;
    }

    bool underflow = false;
    ds_dlist_foreach(&stats->subscribers, sub) {
        const enum osw_tlv_merge_result r = osw_stats_subscriber_put(sub, id, defs, &key, data, len);
        switch (r) {
            case OSW_TLV_MERGE_OK:
                break;
            case OSW_TLV_MERGE_ERR:
                break;
            case OSW_TLV_MERGE_ERR_UNDERFLOW:
                underflow = true;
                break;
        }
    }

    if (underflow) {
        osw_stats_log(id, tb, LOG_SEVERITY_INFO, "underflow, some stats were lost");
    }

    osw_tlv_fini(&key);
}

static void
osw_stats_put_tlv(struct osw_stats *stats,
                  const struct osw_tlv *src)
{
    const void *ptr = src->data;
    size_t len = src->used;
    const struct osw_tlv_hdr *i;

    osw_tlv_for_each(i, ptr, len) {
        osw_stats_put_one(stats, i);
    }
}

void
osw_stats_put(const struct osw_tlv *src)
{
    struct osw_stats *stats = &g_osw_stats;
    const double now = OSW_TIME_TO_DBL(osw_time_mono_clk());

    osw_stats_put_tlv(stats, src);
    osw_stats_polling_update(stats, now, true);
}

static void
osw_stats_reset_last__(struct osw_stats *stats,
                       enum osw_stats_id id)
{
    struct osw_stats_subscriber *sub;

    ds_dlist_foreach(&stats->subscribers, sub) {
        struct ds_tree *buckets = &sub->buckets[id];
        struct osw_stats_bucket *bucket;

        ds_tree_foreach(buckets, bucket) {
            osw_tlv_fini(&bucket->last);
        }
    }
}

void
osw_stats_reset_last(enum osw_stats_id id)
{
    osw_stats_reset_last__(&g_osw_stats, id);
}

static void
osw_stats_work_cb(struct osw_timer *t)
{
    struct osw_stats *stats = container_of(t, struct osw_stats, work);
    const double now = OSW_TIME_TO_DBL(osw_time_mono_clk());
    unsigned int stats_mask;
    double seconds = -1;
    bool poll;

    LOGT("osw: stats: running");
    osw_stats_run(&poll, &stats_mask, &seconds, now);

    if (seconds >= 0) {
        osw_timer_arm_at_nsec(&stats->work, OSW_TIME_SEC(now + seconds));
    }

    if (poll == true) {
        LOGT("osw: stats: requesting 0x%08x", stats_mask);
        WARN_ON(stats_mask == 0);
        osw_mux_request_stats(stats_mask);
    }
}

static void
osw_stats_sta_invalidate(struct osw_stats *m,
                         const char *phy_name,
                         const char *vif_name,
                         const struct osw_hwaddr *sta_addr)
{
    struct osw_tlv key;
    MEMZERO(key);

    osw_tlv_put_string(&key, OSW_STATS_STA_PHY_NAME, phy_name);
    osw_tlv_put_string(&key, OSW_STATS_STA_VIF_NAME, vif_name);
    osw_tlv_put_hwaddr(&key, OSW_STATS_STA_MAC_ADDRESS, sta_addr);

    struct osw_stats_subscriber *sub;
    const enum osw_stats_id id = OSW_STATS_STA;
    ds_dlist_foreach(&m->subscribers, sub) {
        struct ds_tree *tree = &sub->buckets[id];
        struct osw_stats_bucket *bucket = ds_tree_find(tree, &key);
        if (bucket == NULL) continue;

        osw_tlv_fini(&bucket->last);
    }

    osw_tlv_fini(&key);
}

static void
osw_stats_sta_disconnected_cb(struct osw_state_observer *obs,
                              const struct osw_state_sta_info *info)
{
    struct osw_stats *m = container_of(obs, typeof(*m), state_obs);
    const char *phy_name = info->vif->phy->phy_name;
    const char *vif_name = info->vif->vif_name;
    const struct osw_hwaddr *sta_addr = info->mac_addr;

    osw_stats_sta_invalidate(m, phy_name, vif_name, sta_addr);
}

static void
osw_stats_vif_added_cb(struct osw_state_observer *obs,
                       const struct osw_state_vif_info *vif)
{
    LOGI("osw: stats: survey: resetting due to vif %s added", vif->vif_name);
    osw_stats_reset_last(OSW_STATS_CHAN);
}

static void
osw_stats_vif_removed_cb(struct osw_state_observer *obs,
                       const struct osw_state_vif_info *vif)
{
    LOGI("osw: stats: survey: resetting due to vif %s removed", vif->vif_name);
    osw_stats_reset_last(OSW_STATS_CHAN);
}

static void
osw_stats_vif_channel_changed_cb(struct osw_state_observer *obs,
                                 const struct osw_state_vif_info *vif,
                                 const struct osw_channel *new_channel,
                                 const struct osw_channel *old_channel)
{
    LOGI("osw: stats: survey: resetting due to vif channel changed: %s: "OSW_CHANNEL_FMT" -> "OSW_CHANNEL_FMT,
         vif->vif_name,
         OSW_CHANNEL_ARG(old_channel),
         OSW_CHANNEL_ARG(new_channel));

    osw_stats_reset_last(OSW_STATS_CHAN);
}

static void
osw_stats_init(struct osw_stats *stats)
{
    ds_dlist_init(&stats->subscribers, struct osw_stats_subscriber, node);
    ds_tree_init(&stats->pollers, osw_stats_double_cmp, struct osw_stats_poller, node);
    osw_timer_init(&stats->work, osw_stats_work_cb);

    stats->state_obs.name = __FILE__;
    stats->state_obs.sta_disconnected_fn = osw_stats_sta_disconnected_cb;
    stats->state_obs.vif_added_fn = osw_stats_vif_added_cb;
    stats->state_obs.vif_removed_fn = osw_stats_vif_removed_cb;
    stats->state_obs.vif_channel_changed_fn = osw_stats_vif_channel_changed_cb;
}

static void
osw_stats_attach(struct osw_stats *m)
{
    osw_state_register_observer(&m->state_obs);
}

OSW_MODULE(osw_stats)
{
    OSW_MODULE_LOAD(osw_timer);
    OSW_MODULE_LOAD(osw_mux);
    struct osw_stats *s = &g_osw_stats;
    osw_stats_init(s);
    osw_stats_attach(s);
    return NULL;
}

#include "osw_stats_ut.c.h"
