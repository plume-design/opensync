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

#include <log.h>
#include <ds_tree.h>
#include <os.h>
#include <os_time.h>
#include <memutil.h>

#include <osw_sta_snr.h>
#include <osw_tlv.h>
#include <osw_stats.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_stats_defs.h>
#include <osw_stats_subscriber.h>
#include <osw_ut.h>

#define LOG_PREFIX(fmt, ...) "osw: sta: snr: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_OBS(obs, fmt, ...)                 \
    LOG_PREFIX(                                       \
            OSW_HWADDR_FMT " (%s): " fmt,             \
            OSW_HWADDR_ARG(&(obs)->params->sta_addr), \
            (obs)->params->vif_name ?: "*",           \
            ##__VA_ARGS__)

#define OSW_STA_SNR_POLL_SEC 0.5f

/* The larger the factor the more resilient to sudden
 * changes SNR is, but at the expense of making it react
 * slower to actual drops.
 */
#define OSW_STA_SNR_EMA_DECAY_FACTOR 1
#define OSW_STA_SNR_EMA_DECAY        (OSW_STA_SNR_EMA_DECAY_FACTOR * (OSW_STA_SNR_POLL_SEC * 1000))

typedef struct osw_sta_snr_ema osw_sta_snr_ema_t;

struct osw_sta_snr
{
    ds_tree_t obs;
    struct osw_stats_subscriber *sub;
    bool stats;
};

struct osw_sta_snr_params
{
    struct osw_hwaddr sta_addr;
    char *vif_name;
    struct osw_hwaddr vif_addr;
    int ageout_sec;
    osw_sta_snr_notify_fn_t *notify_fn;
    void *notify_fn_priv;
};

struct osw_sta_snr_ema
{
    float smoothed_snr_db;
    float decay;
    uint64_t last_sample_ms;
};

struct osw_sta_snr_observer
{
    ds_tree_node_t node;
    osw_sta_snr_t *m;
    osw_sta_snr_params_t *params;
    osw_sta_snr_ema_t e;
    struct osw_timer ageout;
    uint8_t reported_snr_db;
    /* FIXME: This could maintain a cache of last-seen SNR
     * with an ageout timer to allow observers to get
     * immediate SNR notification without needing to wait
     * for next statistics report (that may not even come).
     */
};

static void osw_sta_snr_ema_init(struct osw_sta_snr_ema *e)
{
    MEMZERO(*e);
    e->decay = OSW_STA_SNR_EMA_DECAY;
}

static bool osw_sta_snr_ema_is_empty(struct osw_sta_snr_ema *e)
{
    return e->last_sample_ms == 0;
}

static float osw_sta_snr_ema_feed(struct osw_sta_snr_ema *e, uint8_t snr_db, uint64_t now_ms)
{
    if (osw_sta_snr_ema_is_empty(e))
    {
        e->smoothed_snr_db = snr_db;
    }
    else
    {
        const float elapsed = (now_ms - e->last_sample_ms);
        const float factor = elapsed / (e->decay + elapsed);
        const float old_part = (1 - factor) * e->smoothed_snr_db;
        const float new_part = factor * (float)snr_db;
        e->smoothed_snr_db = old_part + new_part;
    }

    e->last_sample_ms = now_ms;
    return e->smoothed_snr_db;
}

static void osw_sta_snr_init(osw_sta_snr_t *m)
{
    ds_tree_init(&m->obs, ds_void_cmp, osw_sta_snr_observer_t, node);
}

static void osw_sta_snr_attach(osw_sta_snr_t *m)
{
    OSW_MODULE_LOAD(osw_stats);
    m->stats = true;
}

static void osw_sta_snr_observer_notify(osw_sta_snr_observer_t *obs, const uint8_t *snr_db)
{
    const osw_sta_snr_params_t *p = obs->params;
    const char *snr_str = snr_db ? strfmta("%hhu dB", *snr_db) : "aged out";
    LOGT(LOG_PREFIX_OBS(obs, "snr: %s", snr_str));
    p->notify_fn(p->notify_fn_priv, snr_db);
}

static void osw_sta_snr_observer_ageout_cb(struct osw_timer *t)
{
    osw_sta_snr_observer_t *obs = container_of(t, typeof(*obs), ageout);
    osw_sta_snr_ema_init(&obs->e);
    osw_sta_snr_observer_notify(obs, NULL);
}

static void osw_sta_snr_observer_ageout_arm(osw_sta_snr_observer_t *obs)
{
    if (obs->params->ageout_sec == 0) return;
    const uint64_t at = osw_time_mono_clk() + OSW_TIME_SEC(obs->params->ageout_sec);
    osw_timer_arm_at_nsec(&obs->ageout, at);
}

static void osw_sta_snr_observer_feed(osw_sta_snr_observer_t *obs, uint8_t snr_db)
{
    const bool first_report = osw_sta_snr_ema_is_empty(&obs->e);
    const float smoothed_snr_db = osw_sta_snr_ema_feed(&obs->e, snr_db, clock_mono_ms());
    const uint8_t smoothed_snr_db_u8 = smoothed_snr_db;
    if (first_report || smoothed_snr_db_u8 != obs->reported_snr_db)
    {
        obs->reported_snr_db = smoothed_snr_db_u8;
        osw_sta_snr_observer_notify(obs, &obs->reported_snr_db);
    }
    osw_sta_snr_observer_ageout_arm(obs);
}

static void osw_sta_snr_stats_report_cb(
        enum osw_stats_id id,
        const struct osw_tlv *data,
        const struct osw_tlv *last,
        void *priv)
{
    if (id != OSW_STATS_STA) return;

    osw_sta_snr_t *m = priv;
    const struct osw_stats_defs *stats_defs = osw_stats_defs_lookup(OSW_STATS_STA);
    if (stats_defs == NULL) return;
    const struct osw_tlv_hdr *tb[OSW_STATS_STA_MAX__] = {0};

    osw_tlv_parse(data->data, data->used, stats_defs->tpolicy, tb, OSW_STATS_STA_MAX__);

    const struct osw_tlv_hdr *vif_name_t = tb[OSW_STATS_STA_VIF_NAME];
    const struct osw_tlv_hdr *vif_addr_t = tb[OSW_STATS_STA_VIF_ADDRESS];
    const struct osw_tlv_hdr *sta_addr_t = tb[OSW_STATS_STA_MAC_ADDRESS];
    const struct osw_tlv_hdr *snr_db_t = tb[OSW_STATS_STA_SNR_DB];

    if (vif_name_t == NULL && vif_addr_t == NULL) return;
    if (sta_addr_t == NULL) return;
    if (snr_db_t == NULL) return;

    const char *vif_name = vif_name_t ? osw_tlv_get_string(vif_name_t) : NULL;
    const struct osw_hwaddr *vif_addr = vif_addr_t ? osw_tlv_get_data(vif_addr_t) : osw_hwaddr_zero();
    const struct osw_hwaddr *sta_addr = osw_tlv_get_data(sta_addr_t);
    uint32_t snr_db = osw_tlv_get_u32(tb[OSW_STATS_STA_SNR_DB]);

    if (WARN_ON(snr_db > UINT8_MAX)) snr_db = UINT8_MAX;

    osw_sta_snr_observer_t *obs;
    ds_tree_foreach (&m->obs, obs)
    {
        const osw_sta_snr_params_t *p = obs->params;
        const bool sta_addr_match = osw_hwaddr_is_equal(&p->sta_addr, sta_addr);
        const bool vif_wildcard = (p->vif_name == NULL);
        const bool vif_name_match = vif_name ? (p->vif_name != NULL && strcmp(p->vif_name, vif_name) == 0) : false;
        const bool vif_addr_match = osw_hwaddr_is_zero(vif_addr) ? false : osw_hwaddr_is_equal(&p->vif_addr, vif_addr);
        const bool vif_match = vif_name_match || vif_addr_match;

        if (sta_addr_match && (vif_wildcard || vif_match))
        {
            osw_sta_snr_observer_feed(obs, snr_db);
        }
    }
}

static void osw_sta_snr_start(osw_sta_snr_t *m)
{
    if (m->stats == false) return;
    if (m->sub != NULL) return;
    LOGI(LOG_PREFIX("starting"));
    m->sub = osw_stats_subscriber_alloc();
    osw_stats_subscriber_set_report_seconds(m->sub, OSW_STA_SNR_POLL_SEC);
    osw_stats_subscriber_set_poll_seconds(m->sub, OSW_STA_SNR_POLL_SEC);
    osw_stats_subscriber_set_report_fn(m->sub, osw_sta_snr_stats_report_cb, m);
    osw_stats_subscriber_set_sta(m->sub, true);
    osw_stats_register_subscriber(m->sub);
}

static void osw_sta_snr_stop(osw_sta_snr_t *m)
{
    if (m->sub == NULL) return;
    if (ds_tree_is_empty(&m->obs) == false) return;
    LOGI(LOG_PREFIX("stopping"));
    osw_stats_unregister_subscriber(m->sub);
    osw_stats_subscriber_free(m->sub);
    m->sub = NULL;
}

osw_sta_snr_params_t *osw_sta_snr_params_alloc(void)
{
    osw_sta_snr_params_t *p = CALLOC(1, sizeof(*p));
    return p;
}

void osw_sta_snr_params_drop(osw_sta_snr_params_t *p)
{
    if (p == NULL) return;
    FREE(p);
}

void osw_sta_snr_params_set_sta_addr(osw_sta_snr_params_t *p, const struct osw_hwaddr *sta_addr)
{
    if (p == NULL) return;
    p->sta_addr = *(sta_addr ?: osw_hwaddr_zero());
}

void osw_sta_snr_params_set_vif_name(osw_sta_snr_params_t *p, const char *vif_name)
{
    if (p == NULL) return;
    FREE(p->vif_name);
    p->vif_name = vif_name ? STRDUP(vif_name) : NULL;
}

void osw_sta_snr_params_set_vif_addr(osw_sta_snr_params_t *p, const struct osw_hwaddr *vif_addr)
{
    if (p == NULL) return;
    p->vif_addr = *(vif_addr ?: osw_hwaddr_zero());
}

void osw_sta_snr_params_set_ageout_sec(osw_sta_snr_params_t *p, unsigned int seconds)
{
    if (p == NULL) return;
    p->ageout_sec = seconds;
}

void osw_sta_snr_params_set_notify_fn(osw_sta_snr_params_t *p, osw_sta_snr_notify_fn_t *fn, void *priv)
{
    if (p == NULL) return;
    p->notify_fn = fn;
    p->notify_fn_priv = priv;
}

osw_sta_snr_observer_t *osw_sta_snr_observer_alloc(osw_sta_snr_t *m, osw_sta_snr_params_t *p)
{
    if (WARN_ON(p == NULL)) goto err;
    if (WARN_ON(p->notify_fn == NULL)) goto err;
    if (m == NULL) goto err;

    osw_sta_snr_observer_t *obs = CALLOC(1, sizeof(*obs));
    obs->params = p;
    obs->m = m;
    osw_timer_init(&obs->ageout, osw_sta_snr_observer_ageout_cb);
    osw_sta_snr_ema_init(&obs->e);
    ds_tree_insert(&m->obs, obs, obs);
    osw_sta_snr_start(m);
    LOGD(LOG_PREFIX_OBS(obs, "allocated"));
    return obs;

err:
    osw_sta_snr_params_drop(p);
    return NULL;
}

const uint8_t *osw_sta_snr_observer_get_last(osw_sta_snr_observer_t *obs)
{
    if (obs == NULL) return NULL;
    if (osw_sta_snr_ema_is_empty(&obs->e)) return NULL;
    return &obs->reported_snr_db;
}

void osw_sta_snr_observer_drop(osw_sta_snr_observer_t *obs)
{
    if (obs == NULL) return;
    if (WARN_ON(obs->m == NULL)) return;

    LOGD(LOG_PREFIX_OBS(obs, "dropping"));
    osw_sta_snr_t *m = obs->m;
    osw_sta_snr_params_drop(obs->params);
    osw_timer_disarm(&obs->ageout);
    ds_tree_remove(&m->obs, obs);
    FREE(obs);

    osw_sta_snr_stop(m);
}

OSW_MODULE(osw_sta_snr)
{
    static osw_sta_snr_t m;
    osw_sta_snr_init(&m);
    osw_sta_snr_attach(&m);
    return &m;
}
