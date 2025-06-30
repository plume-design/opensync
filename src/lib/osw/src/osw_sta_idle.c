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
#include <const.h>
#include <ds_tree.h>
#include <memutil.h>

#include <osw_sta_idle.h>
#include <osw_sta_assoc.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_tlv.h>
#include <osw_stats.h>
#include <osw_stats_defs.h>
#include <osw_stats_subscriber.h>
#include <osw_ut.h>

#define LOG_PREFIX(fmt, ...) "osw: sta: idle: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_OBS(obs, fmt, ...) \
    LOG_PREFIX(OSW_HWADDR_FMT ": " fmt, OSW_HWADDR_ARG(&(obs)->params->sta_addr), ##__VA_ARGS__)

#define OSW_STA_IDLE_POLL_SEC           0.5f
#define OSW_STA_IDLE_COLLECT_SEC        1.0f
#define OSW_STA_IDLE_AGEOUT_SEC_DEFAULT 10

struct osw_sta_idle
{
    ds_tree_t obs;
    osw_sta_assoc_t *sta_assoc;
    struct osw_stats_subscriber *sub;
    bool stats;
};

struct osw_sta_idle_params
{
    struct osw_hwaddr sta_addr;
    uint32_t bytes_per_sec;
    uint32_t ageout_sec;
    osw_sta_idle_notify_fn_t *notify_fn;
    void *notify_fn_priv;
};

struct osw_sta_idle_observer
{
    ds_tree_node_t node;
    osw_sta_idle_t *m;
    osw_sta_idle_params_t *params;
    osw_sta_assoc_observer_t *sta_assoc_obs;
    struct osw_timer ageout;
    struct osw_timer collect;
    uint32_t bytes;
};

static void osw_sta_idle_init(osw_sta_idle_t *m)
{
    ds_tree_init(&m->obs, ds_void_cmp, osw_sta_idle_observer_t, node);
}

static void osw_sta_idle_attach(osw_sta_idle_t *m)
{
    OSW_MODULE_LOAD(osw_stats);
    OSW_MODULE_LOAD(osw_state);
    m->stats = true;
    m->sta_assoc = OSW_MODULE_LOAD(osw_sta_assoc);
}

static void osw_sta_idle_observer_notify_active(osw_sta_idle_observer_t *obs)
{
    LOGD(LOG_PREFIX_OBS(obs, "active"));
    obs->params->notify_fn(obs->params->notify_fn_priv, false);
}

static void osw_sta_idle_observer_notify_idle(osw_sta_idle_observer_t *obs)
{
    LOGD(LOG_PREFIX_OBS(obs, "idle"));
    obs->params->notify_fn(obs->params->notify_fn_priv, true);
}

static void osw_sta_idle_observer_arm_ageout(osw_sta_idle_observer_t *obs)
{
    const uint64_t at_nsec = osw_time_mono_clk() + OSW_TIME_SEC(obs->params->ageout_sec);
    osw_timer_arm_at_nsec(&obs->ageout, at_nsec);
}

static void osw_sta_idle_observer_arm_collect(osw_sta_idle_observer_t *obs)
{
    const uint64_t at_nsec = osw_time_mono_clk() + OSW_TIME_SEC(OSW_STA_IDLE_COLLECT_SEC);
    osw_timer_arm_at_nsec(&obs->collect, at_nsec);
}

static void osw_sta_idle_observer_ageout_cb(struct osw_timer *t)
{
    osw_sta_idle_observer_t *obs = container_of(t, osw_sta_idle_observer_t, ageout);
    osw_sta_idle_observer_notify_idle(obs);
}

static void osw_sta_idle_observer_set_active(osw_sta_idle_observer_t *obs)
{
    const bool not_reported_yet = (osw_timer_is_armed(&obs->ageout) == false);
    if (not_reported_yet)
    {
        osw_sta_idle_observer_notify_active(obs);
    }
    else
    {
        LOGT(LOG_PREFIX_OBS(obs, "still active"));
    }

    osw_sta_idle_observer_arm_ageout(obs);
}

static void osw_sta_idle_observer_check(osw_sta_idle_observer_t *obs)
{
    const uint32_t bytes_per_sec = (obs->bytes / OSW_STA_IDLE_COLLECT_SEC);
    const bool idle = (bytes_per_sec < obs->params->bytes_per_sec);
    if (idle) return;

    osw_sta_idle_observer_set_active(obs);
}

static void osw_sta_idle_observer_collect_cb(struct osw_timer *t)
{
    osw_sta_idle_observer_t *obs = container_of(t, osw_sta_idle_observer_t, collect);
    obs->bytes = 0;
}

static void osw_sta_idle_observer_feed(osw_sta_idle_observer_t *obs, uint32_t bytes)
{
    obs->bytes += bytes;
    osw_sta_idle_observer_check(obs);
    if (osw_timer_is_armed(&obs->collect)) return;
    osw_sta_idle_observer_arm_collect(obs);
}

static void osw_sta_idle_stats_report_bytes(osw_sta_idle_t *m, const struct osw_hwaddr *sta_addr, const uint32_t bytes)
{
    osw_sta_idle_observer_t *obs;
    ds_tree_foreach (&m->obs, obs)
    {
        const osw_sta_assoc_entry_t *e = osw_sta_assoc_observer_get_entry(obs->sta_assoc_obs);
        const osw_sta_assoc_links_t *l = osw_sta_assoc_entry_get_active_links(e);
        const osw_sta_assoc_link_t *found = osw_sta_assoc_links_lookup(l, NULL, sta_addr);
        if (found)
        {
            osw_sta_idle_observer_feed(obs, bytes);
        }
    }
}

static void osw_sta_idle_stats_report_cb(
        enum osw_stats_id id,
        const struct osw_tlv *data,
        const struct osw_tlv *last,
        void *priv)
{
    if (id != OSW_STATS_STA) return;

    osw_sta_idle_t *m = priv;
    const struct osw_stats_defs *stats_defs = osw_stats_defs_lookup(OSW_STATS_STA);
    const struct osw_tlv_hdr *tb[OSW_STATS_STA_MAX__] = {0};

    osw_tlv_parse(data->data, data->used, stats_defs->tpolicy, tb, OSW_STATS_STA_MAX__);

    const struct osw_tlv_hdr *sta_addr_t = tb[OSW_STATS_STA_MAC_ADDRESS];
    const struct osw_tlv_hdr *tx_bytes_t = tb[OSW_STATS_STA_TX_BYTES];
    const struct osw_tlv_hdr *rx_bytes_t = tb[OSW_STATS_STA_RX_BYTES];

    if (sta_addr_t == NULL) return;

    const struct osw_hwaddr *sta_addr = osw_tlv_get_data(sta_addr_t);
    const uint32_t tx_bytes = tx_bytes_t ? osw_tlv_get_u32(tx_bytes_t) : 0;
    const uint32_t rx_bytes = rx_bytes_t ? osw_tlv_get_u32(rx_bytes_t) : 0;
    const uint32_t bytes = tx_bytes + rx_bytes;

    osw_sta_idle_stats_report_bytes(m, sta_addr, bytes);
}

static void osw_sta_idle_start(osw_sta_idle_t *m)
{
    if (m->stats == false) return;
    if (m->sub != NULL) return;
    LOGI(LOG_PREFIX("starting"));
    m->sub = osw_stats_subscriber_alloc();
    osw_stats_subscriber_set_report_seconds(m->sub, OSW_STA_IDLE_POLL_SEC);
    osw_stats_subscriber_set_poll_seconds(m->sub, OSW_STA_IDLE_POLL_SEC);
    osw_stats_subscriber_set_report_fn(m->sub, osw_sta_idle_stats_report_cb, m);
    osw_stats_subscriber_set_sta(m->sub, true);
    osw_stats_register_subscriber(m->sub);
}

static void osw_sta_idle_stop(osw_sta_idle_t *m)
{
    if (m->sub == NULL) return;
    if (ds_tree_is_empty(&m->obs) == false) return;
    LOGI(LOG_PREFIX("stopping"));
    osw_stats_unregister_subscriber(m->sub);
    osw_stats_subscriber_free(m->sub);
    m->sub = NULL;
}

osw_sta_idle_params_t *osw_sta_idle_params_alloc(void)
{
    osw_sta_idle_params_t *p = CALLOC(1, sizeof(*p));
    p->ageout_sec = OSW_STA_IDLE_AGEOUT_SEC_DEFAULT;
    return p;
}

void osw_sta_idle_params_drop(osw_sta_idle_params_t *p)
{
    if (p == NULL) return;
    FREE(p);
}

void osw_sta_idle_params_set_sta_addr(osw_sta_idle_params_t *p, const struct osw_hwaddr *sta_addr)
{
    if (p == NULL) return;
    p->sta_addr = *(sta_addr ?: osw_hwaddr_zero());
}

void osw_sta_idle_params_set_bytes_per_sec(osw_sta_idle_params_t *p, uint32_t bytes_per_sec)
{
    if (p == NULL) return;
    p->bytes_per_sec = bytes_per_sec;
}

void osw_sta_idle_params_set_ageout_sec(osw_sta_idle_params_t *p, uint32_t ageout_sec)
{
    if (p == NULL) return;
    p->ageout_sec = ageout_sec;
}

void osw_sta_idle_params_set_notify_fn(osw_sta_idle_params_t *p, osw_sta_idle_notify_fn_t *fn, void *priv)
{
    if (p == NULL) return;
    p->notify_fn = fn;
    p->notify_fn_priv = priv;
}

static void osw_sta_idle_observer_changed_cb(void *priv, const osw_sta_assoc_entry_t *entry, osw_sta_assoc_event_e ev)
{
    osw_sta_idle_observer_t *obs = priv;
    switch (ev)
    {
        case OSW_STA_ASSOC_CONNECTED:
        case OSW_STA_ASSOC_RECONNECTED:
            osw_sta_idle_observer_set_active(obs);
            break;
        case OSW_STA_ASSOC_UNDEFINED:
        case OSW_STA_ASSOC_DISCONNECTED:
            break;
    }
}

osw_sta_assoc_observer_t *osw_sta_idle_observer_alloc_sta_assoc_obs(
        osw_sta_idle_observer_t *obs,
        osw_sta_idle_params_t *p)
{
    osw_sta_assoc_observer_params_t *sp = osw_sta_assoc_observer_params_alloc();
    osw_sta_assoc_observer_params_set_changed_fn(sp, osw_sta_idle_observer_changed_cb, obs);
    osw_sta_assoc_observer_params_set_addr(sp, &p->sta_addr);
    return osw_sta_assoc_observer_alloc(obs->m->sta_assoc, sp);
}

osw_sta_idle_observer_t *osw_sta_idle_observer_alloc(osw_sta_idle_t *m, osw_sta_idle_params_t *p)
{
    if (WARN_ON(p == NULL)) goto err;
    if (WARN_ON(p->notify_fn == NULL)) goto err;
    if (WARN_ON(osw_hwaddr_is_zero(&p->sta_addr))) goto err;
    if (WARN_ON(p->bytes_per_sec == 0)) goto err;
    if (m == NULL) goto err;

    osw_sta_idle_observer_t *obs = CALLOC(1, sizeof(*obs));
    obs->m = m;
    obs->params = p;
    osw_timer_init(&obs->ageout, osw_sta_idle_observer_ageout_cb);
    osw_timer_init(&obs->collect, osw_sta_idle_observer_collect_cb);
    obs->sta_assoc_obs = osw_sta_idle_observer_alloc_sta_assoc_obs(obs, p);
    ds_tree_insert(&m->obs, obs, obs);
    osw_sta_idle_start(m);
    LOGD(LOG_PREFIX_OBS(obs, "allocated"));
    return obs;

err:
    osw_sta_idle_params_drop(p);
    return NULL;
}

void osw_sta_idle_observer_drop(osw_sta_idle_observer_t *obs)
{
    if (obs == NULL) return;
    if (WARN_ON(obs->m == NULL)) return;

    LOGD(LOG_PREFIX_OBS(obs, "dropping"));
    osw_sta_idle_t *m = obs->m;
    osw_timer_disarm(&obs->collect);
    osw_timer_disarm(&obs->ageout);
    osw_sta_idle_params_drop(obs->params);
    osw_sta_assoc_observer_drop(obs->sta_assoc_obs);
    ds_tree_remove(&m->obs, obs);
    FREE(obs);

    osw_sta_idle_stop(m);
}

OSW_MODULE(osw_sta_idle)
{
    static osw_sta_idle_t m;
    osw_sta_idle_init(&m);
    osw_sta_idle_attach(&m);
    return &m;
}

static void osw_sta_idle_test_cb(void *priv, bool idle)
{
    int *out = priv;
    *out = idle ? 1 : 0;
}

OSW_UT(osw_sta_idle_test)
{
    osw_sta_idle_t m = {0};
    osw_sta_idle_init(&m);

    const struct osw_hwaddr addr = {.octet = {0, 0, 0, 0, 0, 1}};
    int idle = 2; /* 0=active, 1=idle, 2=invalid */
    osw_sta_idle_params_t *p = osw_sta_idle_params_alloc();
    osw_sta_idle_params_set_sta_addr(p, &addr);
    osw_sta_idle_params_set_ageout_sec(p, 5);
    osw_sta_idle_params_set_notify_fn(p, osw_sta_idle_test_cb, &idle);
    osw_sta_idle_params_set_bytes_per_sec(p, 100);
    osw_sta_idle_observer_t *o = osw_sta_idle_observer_alloc(&m, p);
    p = NULL;

    assert(idle == 2);
    osw_ut_time_advance(OSW_TIME_SEC(o->params->ageout_sec));
    assert(idle == 2);
    osw_sta_idle_observer_feed(o, o->params->bytes_per_sec / 2);
    assert(idle == 2);
    osw_sta_idle_observer_feed(o, o->params->bytes_per_sec / 2);
    assert(idle == 0);
    osw_ut_time_advance(OSW_TIME_SEC(OSW_STA_IDLE_COLLECT_SEC));
    assert(idle == 0);
    osw_ut_time_advance(OSW_TIME_SEC(o->params->ageout_sec));
    assert(idle == 1);
    osw_sta_idle_observer_feed(o, o->params->bytes_per_sec);
    osw_ut_time_advance(OSW_TIME_SEC(OSW_STA_IDLE_COLLECT_SEC));
    assert(idle == 0);

    /* low volume active station */
    int cycles = 10;
    for (; cycles > 0; cycles--)
    {
        osw_ut_time_advance(OSW_TIME_SEC((o->params->ageout_sec - OSW_STA_IDLE_COLLECT_SEC) / 2));
        assert(idle == 0);
        osw_sta_idle_observer_feed(o, o->params->bytes_per_sec);
        osw_ut_time_advance(OSW_TIME_SEC(OSW_STA_IDLE_COLLECT_SEC));
        assert(idle == 0);
    }

    osw_ut_time_advance(OSW_TIME_SEC(o->params->ageout_sec));
    assert(idle == 1);
}
