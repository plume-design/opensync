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

#include "pm_hw_acc_load.h"
#include "pm_hw_acc_load_netstats.h"
#include "pm_hw_acc_load_cpustats.h"

#include <inttypes.h>

#include <ev.h>
#include <sys/sysinfo.h>

#include <memutil.h>
#include <log.h>
#include <util.h>
#include <os.h>
#include <os_time.h>

#define PM_HW_ACC_LOAD_INTERVAL_SEC   1.0
#define PM_HW_ACC_LOAD_DEACTIVATE_SEC 5.0

/* The idea is that, once CPU util is reached and
 * Accelerator is enabled, then Accelerator is kept enabled
 * until after the traffic volume decreases _below_ 50% of
 * the volume that was observed when Accelerator was enabled
 */
#define PM_HW_ACC_LOAD_THRESHOLD_FACTOR   0.50
#define PM_HW_ACC_LOAD_100_MBPS           (100 * 1024 * 1024 / 8)
#define PM_HW_ACC_LOAD_MIN_BPS            PM_HW_ACC_LOAD_100_MBPS
#define PM_HW_ACC_LOAD_CPU_UTIL_THRESHOLD 80

#define LOG_PREFIX(l, fmt, ...)        "pm_hw_acc_load: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_SAMPLE(s, fmt, ...) LOG_PREFIX(s->l, "sample: " fmt, ##__VA_ARGS__)

struct pm_hw_acc_load_sample
{
    struct pm_hw_acc_load *l;
    struct pm_hw_acc_load_netstats *net;
    struct pm_hw_acc_load_cpustats *cpu;
    double mono_timestamp_seconds;
};

struct pm_hw_acc_load
{
    enum pm_hw_acc_load_mode mode;
    struct ev_loop *loop;
    struct pm_hw_acc_load_sample *prev_sample;
    struct pm_hw_acc_load_sample *next_sample;
    pm_hw_acc_load_updated_fn_t *updated_fn;
    uint64_t bytes_per_second;
    uint64_t packets_per_second;
    uint64_t threshold_bytes_per_second;
    uint64_t threshold_packets_per_second;
    struct pm_hw_acc_load_cpuload *cpu;
    unsigned cpu_util;
    void *priv;
    bool deactivated;
    ev_timer update;
    ev_timer deactivate;
};

const char *pm_hw_acc_load_mode_to_cstr(const enum pm_hw_acc_load_mode mode)
{
    switch (mode)
    {
        case PM_HW_ACC_LOAD_INIT:
            return "init";
        case PM_HW_ACC_LOAD_INACTIVE:
            return "inactive";
        case PM_HW_ACC_LOAD_ACTIVE:
            return "active";
        case PM_HW_ACC_LOAD_DEACTIVATING:
            return "deactivation";
    }
    return "";
}

static void pm_hw_acc_load_mode_set(struct pm_hw_acc_load *l, const enum pm_hw_acc_load_mode mode)
{
    if (l->mode == mode) return;
    LOGD(LOG_PREFIX(l, "mode: %s -> %s", pm_hw_acc_load_mode_to_cstr(l->mode), pm_hw_acc_load_mode_to_cstr(mode)));
    l->mode = mode;
    if (l->updated_fn != NULL) l->updated_fn(l->priv);
}

enum pm_hw_acc_load_mode pm_hw_acc_load_mode_get(const struct pm_hw_acc_load *l)
{
    if (l == NULL) return PM_HW_ACC_LOAD_INIT;
    return l->mode;
}

static bool pm_hw_acc_load_activation_is_needed(struct pm_hw_acc_load *l)
{
    if (WARN_ON(l->prev_sample == NULL)) return false;
    if (WARN_ON(l->next_sample == NULL)) return false;

    if (l->bytes_per_second > PM_HW_ACC_LOAD_MIN_BPS && l->cpu_util > PM_HW_ACC_LOAD_CPU_UTIL_THRESHOLD)
    {
        return true;
    }
    return false;
}

static bool pm_hw_acc_load_deactivation_is_needed(struct pm_hw_acc_load *l)
{
    if (WARN_ON(l->prev_sample == NULL)) return false;
    if (WARN_ON(l->next_sample == NULL)) return false;

    if (l->bytes_per_second < l->threshold_bytes_per_second) return true;
    if (l->packets_per_second < l->threshold_packets_per_second) return true;

    return false;
}

static void pm_hw_acc_load_deactivation_set_watermarks(struct pm_hw_acc_load *l)
{
    l->threshold_bytes_per_second = l->bytes_per_second * PM_HW_ACC_LOAD_THRESHOLD_FACTOR;
    l->threshold_packets_per_second = l->packets_per_second * PM_HW_ACC_LOAD_THRESHOLD_FACTOR;

    LOGD(LOG_PREFIX(
            l,
            "watermarks: %" PRIu64 " bps %" PRIu64 " pps",
            l->threshold_bytes_per_second,
            l->threshold_packets_per_second));

    WARN_ON(l->threshold_bytes_per_second == 0);
    WARN_ON(l->threshold_packets_per_second == 0);
}

static void pm_hw_acc_load_sample_drop(struct pm_hw_acc_load_sample *s)
{
    if (s == NULL) return;
    LOGT(LOG_PREFIX_SAMPLE(s, "dropping"));
    pm_hw_acc_load_netstats_drop(s->net);
    pm_hw_acc_load_cpustats_drop(s->cpu);
    FREE(s);
}

static void pm_hw_acc_load_sample_replace(struct pm_hw_acc_load_sample **l, struct pm_hw_acc_load_sample **r)
{
    pm_hw_acc_load_sample_drop(*l);
    *l = *r;
    *r = NULL;
}

static void pm_hw_acc_load_deactivation_start(struct pm_hw_acc_load *l)
{
    ev_timer_stop(l->loop, &l->deactivate);
    ev_timer_set(&l->deactivate, PM_HW_ACC_LOAD_DEACTIVATE_SEC, 0);
    ev_timer_start(l->loop, &l->deactivate);
    l->deactivated = false;
}

static void pm_hw_acc_load_deactivation_stop(struct pm_hw_acc_load *l)
{
    ev_timer_stop(l->loop, &l->deactivate);
    l->deactivated = false;
}

static bool pm_hw_acc_load_deactivation_is_elapsed(struct pm_hw_acc_load *l)
{
    return l->deactivated;
}

static void pm_hw_acc_load_compute_deltas(struct pm_hw_acc_load *l)
{
    if (l->prev_sample == NULL) return;
    if (l->next_sample == NULL) return;

    const double elapsed_seconds = l->next_sample->mono_timestamp_seconds - l->prev_sample->mono_timestamp_seconds;

    uint64_t max_tx_bytes;
    uint64_t max_rx_bytes;
    uint64_t max_tx_pkts;
    uint64_t max_rx_pkts;
    pm_hw_acc_load_netstats_compare(
            l->prev_sample->net,
            l->next_sample->net,
            &max_tx_bytes,
            &max_rx_bytes,
            &max_tx_pkts,
            &max_rx_pkts);

    l->bytes_per_second = MAX(max_tx_bytes, max_rx_bytes) / elapsed_seconds;
    l->packets_per_second = MAX(max_tx_pkts, max_rx_pkts) / elapsed_seconds;

    /* If new CPUs that were previously offline appeared in /proc/stat and
     * have a higher numeric indication then we simply discard sample
     */
    if (pm_hw_acc_load_cpustats_need_more_space(l->prev_sample->cpu, l->next_sample->cpu))
    {
        const size_t new_len = pm_hw_acc_load_cpustats_get_len(l->next_sample->cpu);
        /* Set new upper solf-limit for cpu data allocation */
        pm_hw_acc_load_cpuload_extend(&l->cpu, new_len);
        LOGI(LOG_PREFIX(l, "more processors have come online, discarding sample"));
        return;
    }

    pm_hw_acc_load_cpustats_compare(l->prev_sample->cpu, l->next_sample->cpu, l->cpu);
    l->cpu_util = pm_hw_acc_load_compute_max_cpu_util(l->cpu);

    LOGT(LOG_PREFIX(
            l,
            "deltas: bps:%" PRIu64 " pps:%" PRIu64 " util:%u%%",
            l->bytes_per_second,
            l->packets_per_second,
            l->cpu_util));
}

static void pm_hw_acc_load_update(struct pm_hw_acc_load *l)
{
    switch (l->mode)
    {
        case PM_HW_ACC_LOAD_INIT:
            pm_hw_acc_load_mode_set(l, PM_HW_ACC_LOAD_INACTIVE);
            break;
        case PM_HW_ACC_LOAD_INACTIVE:
            if (pm_hw_acc_load_activation_is_needed(l))
            {
                pm_hw_acc_load_deactivation_set_watermarks(l);
                pm_hw_acc_load_mode_set(l, PM_HW_ACC_LOAD_ACTIVE);
            }
            break;
        case PM_HW_ACC_LOAD_ACTIVE:
            if (pm_hw_acc_load_deactivation_is_needed(l))
            {
                pm_hw_acc_load_deactivation_start(l);
                pm_hw_acc_load_mode_set(l, PM_HW_ACC_LOAD_DEACTIVATING);
            }
            break;
        case PM_HW_ACC_LOAD_DEACTIVATING:
            if (pm_hw_acc_load_deactivation_is_needed(l) == false)
            {
                pm_hw_acc_load_deactivation_stop(l);
                pm_hw_acc_load_mode_set(l, PM_HW_ACC_LOAD_ACTIVE);
            }
            else if (pm_hw_acc_load_deactivation_is_elapsed(l))
            {
                pm_hw_acc_load_mode_set(l, PM_HW_ACC_LOAD_INACTIVE);
            }
            break;
    }
}

static struct pm_hw_acc_load_sample *pm_hw_acc_load_sample_get(struct pm_hw_acc_load *l)
{
    struct pm_hw_acc_load_sample *s = CALLOC(1, sizeof(*s));
    s->l = l;
    s->net = pm_hw_acc_load_netstats_get();
    const size_t soft_cpu_limit = pm_hw_acc_load_cpuload_get_len(l->cpu);
    s->cpu = pm_hw_acc_load_cpustats_get(soft_cpu_limit);
    s->mono_timestamp_seconds = clock_mono_double();
    LOGT(LOG_PREFIX_SAMPLE(s, "allocated"));
    return s;
}

static void pm_hw_acc_load_update_cb(struct ev_loop *loop, ev_timer *t, int flags)
{
    struct pm_hw_acc_load *l = t->data;
    LOGT(LOG_PREFIX(l, "updating"));
    l->next_sample = pm_hw_acc_load_sample_get(l);
    pm_hw_acc_load_compute_deltas(l);
    pm_hw_acc_load_update(l);
    pm_hw_acc_load_sample_replace(&l->prev_sample, &l->next_sample);
}

static void pm_hw_acc_load_deactivate_cb(struct ev_loop *loop, ev_timer *t, int flags)
{
    struct pm_hw_acc_load *l = t->data;
    LOGT(LOG_PREFIX(l, "deactivated"));
    l->deactivated = true;
    /* next update_cb() should pick this up and
     * transition to PM_HW_ACC_LOAD_INACTIVE if
     * it's still applicable.
     */
}

struct pm_hw_acc_load *pm_hw_acc_load_alloc(pm_hw_acc_load_updated_fn_t *updated_fn, void *priv)
{
    struct pm_hw_acc_load *l = CALLOC(1, sizeof(*l));
    l->loop = EV_DEFAULT;
    l->mode = PM_HW_ACC_LOAD_INIT;
    l->updated_fn = updated_fn;
    l->priv = priv;
    l->cpu = pm_hw_acc_load_cpuload_alloc();
    const float interval_sec = PM_HW_ACC_LOAD_INTERVAL_SEC;
    ev_timer_init(&l->update, pm_hw_acc_load_update_cb, interval_sec, interval_sec);
    ev_timer_init(&l->deactivate, pm_hw_acc_load_deactivate_cb, 0, 0);
    l->update.data = l;
    l->deactivate.data = l;
    ev_timer_start(l->loop, &l->update);
    LOGD(LOG_PREFIX(l, "allocated"));
    return l;
}

void pm_hw_acc_load_drop(struct pm_hw_acc_load *l)
{
    if (l == NULL) return;
    LOGD(LOG_PREFIX(l, "dropping"));
    pm_hw_acc_load_sample_drop(l->prev_sample);
    pm_hw_acc_load_sample_drop(l->next_sample);
    ev_timer_stop(l->loop, &l->update);
    pm_hw_acc_load_cpuload_drop(l->cpu);
    FREE(l);
}
