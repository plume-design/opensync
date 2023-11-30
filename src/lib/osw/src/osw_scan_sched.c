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

/* libc */
#include <string.h>

/* opensync */
#include <memutil.h>
#include <os.h>
#include <log.h>
#include <const.h>

/* unit */
#include <osw_scan_sched.h>
#include <osw_drv.h>
#include <osw_state.h>
#include <osw_mux.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_util.h>
#include <osw_ut.h>

#define LOG_PREFIX(ss, fmt, ...) \
    "osw: scan_sched: %p: %s/%s: " fmt, \
    ss, \
    ss->phy_name ?: "", \
    ss->vif_name ?: "", ##__VA_ARGS__

struct osw_scan_sched {
    char *phy_name;
    char *vif_name;
    struct osw_channel *channels;
    size_t n_channels;
    size_t next_channel;
    enum osw_scan_sched_mode mode;
    struct osw_timer timer;
    unsigned int dwell_time_msec;
    double interval_seconds;
    double offset_seconds;
    osw_scan_sched_filter_fn_t *filter_fn;
    void *filter_fn_priv;
};

static const char *
osw_scan_sched_mode_to_str(enum osw_scan_sched_mode mode)
{
    switch (mode) {
        case OSW_SCAN_SCHED_RR: return "rr";
        case OSW_SCAN_SCHED_ALL: return "all";
    }
    return "";
}

static bool
osw_scan_sched_is_configured(struct osw_scan_sched *ss)
{
    if (ss->phy_name == NULL) return false;
    if (ss->vif_name == NULL) return false;
    if (ss->n_channels == 0) return false;
    if (ss->interval_seconds == 0) return false;
    if (ss->dwell_time_msec == 0) return false;

    return true;
}

static void
osw_scan_sched_timer_set(struct osw_scan_sched *ss)
{
    struct osw_timer *timer = &ss->timer;

    if (osw_scan_sched_is_configured(ss)) {
        if (osw_timer_is_armed(timer)) {
            const double now = OSW_TIME_TO_DBL(osw_time_mono_clk());
            const double at = OSW_TIME_TO_DBL(timer->at_nsec);
            LOGD(LOG_PREFIX(ss, "keeping armed in %lf", (at - now)));
        }
        else {
            const double interval = ss->interval_seconds;
            const double offset = ss->offset_seconds;
            const double now = OSW_TIME_TO_DBL(osw_time_mono_clk());
            const double at = osw_periodic_get_next(interval, offset, now);
            LOGD(LOG_PREFIX(ss, "arming in %lf seconds", (at - now)));
            osw_timer_arm_at_nsec(timer, OSW_TIME_SEC(at));
        }
    }
    else {
        if (osw_timer_is_armed(timer)) {
            LOGD(LOG_PREFIX(ss, "disarming"));
            osw_timer_disarm(timer);
        }
        else {
            LOGD(LOG_PREFIX(ss, "keeping disarmed"));
        }
    }
}

static const struct osw_channel *
osw_scan_sched_get_home_chan(struct osw_scan_sched *ss)
{
    /* FIXME: This could be smarter and check for
     * other VIFs on the same PHY, although that
     * would assume a single-channel operation.
     * That's most often going to be the case in
     * the near future anyway, so it also makes
     * little sense to do anything fancy here.
     */
    const char *phy_name = ss->phy_name;
    const char *vif_name = ss->vif_name;
    if (phy_name == NULL) return NULL;
    if (vif_name == NULL) return NULL;

    const struct osw_state_vif_info *info = osw_state_vif_lookup(phy_name, vif_name);
    if (info == NULL) return NULL;

    const struct osw_drv_vif_state *state = info->drv_state;
    if (state->status != OSW_VIF_ENABLED) return NULL;

    switch (state->vif_type) {
        case OSW_VIF_UNDEFINED:
            return NULL;
        case OSW_VIF_AP:
            return &state->u.ap.channel;
        case OSW_VIF_AP_VLAN:
            return NULL;
        case OSW_VIF_STA:
            if (state->u.sta.link.status != OSW_DRV_VIF_STATE_STA_LINK_CONNECTED) {
                return NULL;
            }
            return &state->u.sta.link.channel;
    }

    assert(0);
    return NULL;
}

/* FIXME: This could be generalized to handle list of home[] */
static size_t
osw_scan_sched_build_scan_chan(struct osw_scan_sched *ss,
                               const struct osw_channel *home,
                               struct osw_channel *channels,
                               size_t n_channels)
{
    size_t used = 0;
    const int home_freq = home != NULL ? home->control_freq_mhz : 0;
    const bool skip_home = true; /* for now, always skip home-chan */
    const struct osw_channel *first = NULL;

    for (;;) {
        if (ss->n_channels == 0) {
            break;
        }

        if (ss->next_channel >= ss->n_channels) {
            ss->next_channel = 0;
        }

        if (used == n_channels) {
            break;
        }

        if (used == ss->n_channels) {
            break;
        }

        const struct osw_channel *c = &ss->channels[ss->next_channel];
        if (c == first) {
            break;
        }
        else if (first == NULL) {
            first = c;
        }

        ss->next_channel++;

        const bool is_home = (c->control_freq_mhz == home_freq);
        if (is_home && skip_home) {
            continue;
        }

        LOGT("%s: [%zu] = " OSW_CHANNEL_FMT " (next=%zu)",
             __func__,
             used,
             OSW_CHANNEL_ARG(c),
             ss->next_channel);

        channels[used] = *c;
        used++;
    }

    return used;
}

static bool
osw_scan_sched_is_allowed(struct osw_scan_sched *ss)
{
    osw_scan_sched_filter_fn_t *fn = ss->filter_fn;
    void *priv = ss->filter_fn_priv;

    if (fn == NULL) {
        return true;
    }

    const enum osw_scan_sched_filter result = fn(ss, priv);
    switch (result){
        case OSW_SCAN_SCHED_ALLOW: return true;
        case OSW_SCAN_SCHED_DENY: return false;
    }

    assert(0);
    return false;
}

static void
osw_scan_sched_request(struct osw_scan_sched *ss,
                       struct osw_channel *list,
                       size_t count)
{
    struct osw_drv_scan_params params = {
        .channels = list,
        .n_channels = count,
        .dwell_time_msec = ss->dwell_time_msec,
        .passive = true,
    };
    const char *phy_name = ss->phy_name;
    const char *vif_name = ss->vif_name;

    if (phy_name == NULL) return;
    if (vif_name == NULL) return;

    LOGD(LOG_PREFIX(ss, "requesting: dwell=%u channels(%zu)=%d, ...",
                    ss->dwell_time_msec,
                    count,
                    count > 0 ? list[0].control_freq_mhz : -1));

    const bool ok = osw_mux_request_scan(phy_name, vif_name, &params);
    const bool failed = !ok;
    WARN_ON(failed);
}

static void
osw_scan_sched_timer_work_rr(struct osw_scan_sched *ss)
{
    const struct osw_channel *home = osw_scan_sched_get_home_chan(ss);
    struct osw_channel c;
    const size_t used = osw_scan_sched_build_scan_chan(ss, home, &c, 1);
    if (used == 0) return;
    osw_scan_sched_request(ss, &c, used);
}

static void
osw_scan_sched_timer_work_full(struct osw_scan_sched *ss)
{
    const struct osw_channel *home = osw_scan_sched_get_home_chan(ss);
    const size_t count = ss->n_channels;
    assert(count < 1000); /* keep it sane */
    struct osw_channel list[count];
    const size_t used = osw_scan_sched_build_scan_chan(ss, home, list, count);
    if (used == 0) return;
    osw_scan_sched_request(ss, list, used);
}

static void
osw_scan_sched_timer_work(struct osw_scan_sched *ss)
{
    const bool allowed = osw_scan_sched_is_allowed(ss);
    if (allowed) {
        switch (ss->mode) {
            case OSW_SCAN_SCHED_RR:
                osw_scan_sched_timer_work_rr(ss);
                break;
            case OSW_SCAN_SCHED_ALL:
                osw_scan_sched_timer_work_full(ss);
                break;
        }
    }
    osw_scan_sched_timer_set(ss);
}

static void
osw_scan_sched_timer_cb(struct osw_timer *timer)
{
    struct osw_scan_sched *ss = container_of(timer, struct osw_scan_sched, timer);
    osw_scan_sched_timer_work(ss);
}

void
osw_scan_sched_set_interval(struct osw_scan_sched *ss,
                            double seconds)
{
    if (ss->interval_seconds == seconds) return;

    LOGD(LOG_PREFIX(ss, "interval: %lf", seconds));
    ss->interval_seconds = seconds;
    osw_timer_disarm(&ss->timer);
    osw_scan_sched_timer_set(ss);
}

void
osw_scan_sched_set_offset(struct osw_scan_sched *ss,
                          double seconds)
{
    if (ss->offset_seconds == seconds) return;

    LOGD(LOG_PREFIX(ss, "offset: %lf", seconds));
    ss->offset_seconds = seconds;
    osw_timer_disarm(&ss->timer);
    osw_scan_sched_timer_set(ss);
}

void
osw_scan_sched_set_mode(struct osw_scan_sched *ss,
                        enum osw_scan_sched_mode mode)
{
    if (ss->mode == mode) return;

    const char *mode_str = osw_scan_sched_mode_to_str(mode);
    LOGD(LOG_PREFIX(ss, "mode: %s", mode_str));
    ss->mode = mode;
    ss->next_channel = 0;
    osw_scan_sched_timer_set(ss);
}

void
osw_scan_sched_set_dwell_time_msec(struct osw_scan_sched *ss,
                                   unsigned int dwell_time_msec)
{
    if (ss->dwell_time_msec == dwell_time_msec) return;

    LOGD(LOG_PREFIX(ss, "dwell: %u", dwell_time_msec));
    ss->dwell_time_msec = dwell_time_msec;
    osw_scan_sched_timer_set(ss);
}

void
osw_scan_sched_set_phy_name(struct osw_scan_sched *ss,
                            const char *phy_name)
{
    const bool same = (phy_name == NULL && ss->phy_name == NULL)
                   || (phy_name != NULL && ss->phy_name != NULL &&
                       strcmp(phy_name, ss->phy_name) == 0);
    if (same) return;

    LOGD(LOG_PREFIX(ss, "phy_name: %s", phy_name ?: "(null)"));
    FREE(ss->phy_name);
    ss->phy_name = NULL;
    if (phy_name != NULL) {
        ss->phy_name = STRDUP(phy_name);
    }
    osw_scan_sched_timer_set(ss);
}

void
osw_scan_sched_set_vif_name(struct osw_scan_sched *ss,
                            const char *vif_name)
{
    const bool same = (vif_name == NULL && ss->vif_name == NULL)
                   || (vif_name != NULL && ss->vif_name != NULL &&
                       strcmp(vif_name, ss->vif_name) == 0);
    if (same) return;

    LOGD(LOG_PREFIX(ss, "vif_name: %s", vif_name ?: "(null)"));
    FREE(ss->vif_name);
    ss->vif_name = NULL;
    if (vif_name != NULL) {
        ss->vif_name = STRDUP(vif_name);
    }
    osw_scan_sched_timer_set(ss);
}

void
osw_scan_sched_set_filter_fn(struct osw_scan_sched *ss,
                             osw_scan_sched_filter_fn_t *fn,
                             void *priv)
{
    LOGD(LOG_PREFIX(ss, "filter: %p + %p", fn, priv));
    ss->filter_fn = fn;
    ss->filter_fn_priv = priv;
    osw_scan_sched_timer_set(ss);
}

void
osw_scan_sched_set_channels(struct osw_scan_sched *ss,
                            const struct osw_channel *channels,
                            const size_t n_channels)
{
    const size_t size = n_channels * sizeof(*channels);
    const bool same = (ss->n_channels == n_channels)
                   && ( (ss->channels == NULL && channels == NULL) ||
                        (memcmp(ss->channels, channels, size) == 0) );
    if (same) return;

    char chans[4096] = {0};
    char *p = chans;
    size_t n = sizeof(chans);
    size_t i;
    for (i = 0; i < n_channels; i++) {
        const int freq = channels[i].control_freq_mhz;
        csnprintf(&p, &n, "%u, ", freq);
    }
    LOGD(LOG_PREFIX(ss, "channels: %s", chans));

    FREE(ss->channels);
    ss->channels = NULL;
    ss->n_channels = 0;
    ss->next_channel = 0;

    if (n_channels > 0 && channels != NULL) {
        ss->channels = MEMNDUP(channels, size);
        ss->n_channels = n_channels;
    }

    osw_scan_sched_timer_set(ss);
}

static void
osw_scan_sched_init(struct osw_scan_sched *ss)
{
    osw_timer_init(&ss->timer, osw_scan_sched_timer_cb);
}

struct osw_scan_sched *
osw_scan_sched_alloc(void)
{
    struct osw_scan_sched *ss = CALLOC(1, sizeof(*ss));
    osw_scan_sched_init(ss);
    return ss;
}

void
osw_scan_sched_free(struct osw_scan_sched *ss)
{
    if (ss == NULL) return;

    osw_scan_sched_set_phy_name(ss, NULL);
    osw_scan_sched_set_vif_name(ss, NULL);
    osw_scan_sched_set_channels(ss, NULL, 0);
    osw_timer_disarm(&ss->timer);
    FREE(ss);
}

OSW_UT(osw_scan_sched_build)
{
    struct osw_scan_sched ss;
    MEMZERO(ss);
    osw_scan_sched_init(&ss);

    const struct osw_channel c1 = { .control_freq_mhz = 2412 };
    const struct osw_channel c6 = { .control_freq_mhz = 2437 };
    const struct osw_channel c11 = { .control_freq_mhz = 2462 };
    const struct osw_channel cl1[] = { c1, c6, c11 };

    struct osw_channel ol[6];
    struct osw_channel oc;

    osw_scan_sched_set_channels(&ss, cl1, ARRAY_SIZE(cl1));

    osw_scan_sched_build_scan_chan(&ss, NULL, &oc, 1); assert(memcmp(&oc, &c1, sizeof(oc)) == 0);
    osw_scan_sched_build_scan_chan(&ss, NULL, &oc, 1); assert(memcmp(&oc, &c6, sizeof(oc)) == 0);
    osw_scan_sched_build_scan_chan(&ss, NULL, &oc, 1); assert(memcmp(&oc, &c11, sizeof(oc)) == 0);

    osw_scan_sched_build_scan_chan(&ss, &c6, &oc, 1); assert(memcmp(&oc, &c1, sizeof(oc)) == 0);
    osw_scan_sched_build_scan_chan(&ss, &c6, &oc, 1); assert(memcmp(&oc, &c11, sizeof(oc)) == 0);
    osw_scan_sched_build_scan_chan(&ss, &c6, &oc, 1); assert(memcmp(&oc, &c1, sizeof(oc)) == 0);
    osw_scan_sched_build_scan_chan(&ss, &c6, &oc, 1); assert(memcmp(&oc, &c11, sizeof(oc)) == 0);

    osw_scan_sched_build_scan_chan(&ss, &c1, &oc, 1); assert(memcmp(&oc, &c6, sizeof(oc)) == 0);

    assert(osw_scan_sched_build_scan_chan(&ss, &c1, ol, ARRAY_SIZE(ol)) == 2);
    assert(memcmp(&ol[0], &c11, sizeof(c11)) == 0);
    assert(memcmp(&ol[1], &c6, sizeof(c6)) == 0);

    assert(osw_scan_sched_build_scan_chan(&ss, &c1, ol, ARRAY_SIZE(ol)) == 2);
    assert(memcmp(&ol[0], &c11, sizeof(c11)) == 0);
    assert(memcmp(&ol[1], &c6, sizeof(c6)) == 0);

    /* This should reset the iterator, thus different list order is expected */
    osw_scan_sched_set_mode(&ss, OSW_SCAN_SCHED_ALL);
    assert(osw_scan_sched_build_scan_chan(&ss, &c1, ol, ARRAY_SIZE(ol)) == 2);
    assert(memcmp(&ol[0], &c6, sizeof(c6)) == 0);
    assert(memcmp(&ol[1], &c11, sizeof(c11)) == 0);

    /* Empty outputs expected */
    osw_scan_sched_set_channels(&ss, &c1, 1);
    assert(osw_scan_sched_build_scan_chan(&ss, &c1, &oc, 1) == 0);
    assert(osw_scan_sched_build_scan_chan(&ss, &c1, &oc, 1) == 0);
    assert(osw_scan_sched_build_scan_chan(&ss, &c1, &oc, 1) == 0);

    /* Not empty anymore */
    assert(osw_scan_sched_build_scan_chan(&ss, NULL, &oc, 1) == 1);
    assert(memcmp(&oc, &c1, sizeof(oc)) == 0);

    /* Empty again */
    osw_scan_sched_set_channels(&ss, NULL, 0);
    assert(osw_scan_sched_build_scan_chan(&ss, NULL, &oc, 1) == 0);
}
