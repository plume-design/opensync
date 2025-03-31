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

#include <stdlib.h>
#include <log.h>
#include <util.h>
#include <osw_conf.h>
#include <osw_confsync.h>
#include <osw_module.h>
#include <osw_timer.h>
#include <osw_time.h>
#include <osw_ut.h>

#include "ds_dlist.h"

#define LOG_PREFIX(fmt, ...) "osw: confsync_watchdog: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_INTERVAL(interval, fmt, ...)                  \
    LOG_PREFIX(                                                  \
            "(%s, %lfs): " fmt,                                  \
            osw_confsync_watchdog_state_to_str(interval->state), \
            OSW_TIME_TO_DBL(interval->duration_nsec),            \
            ##__VA_ARGS__)

#define OSW_CONFSYNC_WATCHDOG_MAX_NOT_SETTLED_SEC   (3 * 60)  // 3 min
#define OSW_CONFSYNC_WATCHDOG_MAX_INTERVALS_SUM_SEC (5 * 60)  // 5 min
#define OSW_CONFSYNC_WATCHDOG_PERCENTAGE_THRESHOLD  0.75

/*
 *
 * Confsync watchdog
 *
 * Purpose:
 * Restart OWM when radios can't be configured or configuration flickers.
 * The state is determined based on Confsync settling observation
 *
 * Descrpition:
 *
 * This component targets to solve two problematic situations:
 * 1. Radios can't be configured
 * 2. Configuration flickers
 *
 * By observing that Confsync:
 * 1. didn't settle in 3 minutes
 * 2. spend over 75% of the last 5 minutes in Unsettled state
 *
 */

enum osw_confsync_watchdog_state
{
    OSW_CONFSYNC_WATCHDOG_SETTLED,
    OSW_CONFSYNC_WATCHDOG_UNSETTLED,
};

typedef void osw_confsync_watchdog_fatal_fn_t(void *priv);

struct osw_confsync_watchdog
{
    bool settled;
    struct osw_confsync_changed *cb_handler;
    struct osw_timer not_settled;

    uint64_t last_event_timestamp_nsec;
    ds_dlist_t settle_intervals;
    osw_confsync_watchdog_fatal_fn_t *fatal_fn;
    void *fatal_priv;
};

struct osw_confsync_watchdog_interval
{
    ds_dlist_node_t dnode;
    uint64_t duration_nsec;
    enum osw_confsync_watchdog_state state;
};

static void osw_confsync_watchdog_set_fatal_fn(
        struct osw_confsync_watchdog *m,
        osw_confsync_watchdog_fatal_fn_t *fn,
        void *priv)
{
    m->fatal_fn = fn;
    m->fatal_priv = priv;
}

static char *osw_confsync_watchdog_state_to_str(enum osw_confsync_watchdog_state state)
{
    switch (state)
    {
        case OSW_CONFSYNC_WATCHDOG_SETTLED:
            return "SETTLED";
        case OSW_CONFSYNC_WATCHDOG_UNSETTLED:
            return "UNSETTLED";
    }
    return "";
}

static void osw_confsync_watchdog_send_sigabrt(void *priv)
{
    LOGE("Aborting");
    raise(SIGABRT);
}

static void osw_confsync_watchdog_unsettled_notify(struct osw_timer *not_settled)
{
    struct osw_confsync_watchdog *m = container_of(not_settled, struct osw_confsync_watchdog, not_settled);
    LOGW(LOG_PREFIX("confsync didn't settle for %d s", OSW_CONFSYNC_WATCHDOG_MAX_NOT_SETTLED_SEC));
    if (m->fatal_fn != NULL) m->fatal_fn(m->fatal_priv);
}

static void osw_confsync_watchdog_switch_settled(struct osw_confsync_watchdog *m)
{
    if (osw_timer_is_armed(&m->not_settled))
    {
        const uint64_t now_nsec = osw_time_mono_clk();
        const uint64_t rem_nsec = osw_timer_get_remaining_nsec(&m->not_settled, now_nsec);
        const uint64_t elap_nsec = OSW_TIME_SEC(OSW_CONFSYNC_WATCHDOG_MAX_NOT_SETTLED_SEC) - rem_nsec;
        const double elap_sec = OSW_TIME_TO_DBL(elap_nsec);

        LOGD(LOG_PREFIX("Disarming timer, elapsed %lf s", elap_sec));
        osw_timer_disarm(&m->not_settled);
    }
}

static void osw_confsync_watchdog_switch_unsettled(struct osw_confsync_watchdog *m)
{
    if (!osw_timer_is_armed(&m->not_settled))
    {
        const uint64_t nsec = OSW_TIME_SEC(OSW_CONFSYNC_WATCHDOG_MAX_NOT_SETTLED_SEC);
        const uint64_t now = osw_time_mono_clk();
        osw_timer_arm_at_nsec(&m->not_settled, now + nsec);
        LOGD(LOG_PREFIX("Arming timer"));
    }
}

static bool osw_confsync_watchdog_cs_state_to_cs_w_state(struct osw_confsync_watchdog *m, enum osw_confsync_state state)
{
    bool state_is_changed = false;
    switch (state)
    {
        case OSW_CONFSYNC_IDLE:
            if (m->settled == false) state_is_changed = true;
            m->settled = true;
            break;
        case OSW_CONFSYNC_WAITING:
            if (m->settled == true) state_is_changed = true;
            m->settled = false;
            break;
        case OSW_CONFSYNC_VERIFYING:
        case OSW_CONFSYNC_REQUESTING:
            break;
    }
    return state_is_changed;
}

static bool osw_confsync_watchdog_threshold_condition(
        struct osw_confsync_watchdog *m,
        uint64_t total_time,
        uint64_t total_unsettled_time)
{
    const uint64_t target_interval_msec = OSW_TIME_SEC(OSW_CONFSYNC_WATCHDOG_MAX_INTERVALS_SUM_SEC);
    const double duty_cycle_threshold = (double)OSW_CONFSYNC_WATCHDOG_PERCENTAGE_THRESHOLD;

    const bool period_is_too_short = (total_time < target_interval_msec);
    if (period_is_too_short) return false;

    double duty_cycle = (double)total_unsettled_time;
    duty_cycle /= (double)total_time;
    return duty_cycle > duty_cycle_threshold;
}

static void osw_confsync_watchdog_threshold_check(struct osw_confsync_watchdog *m)
{
    struct osw_confsync_watchdog_interval *interval;
    interval = ds_dlist_head(&m->settle_intervals);
    if (interval == NULL || interval->state == OSW_CONFSYNC_WATCHDOG_SETTLED) return;

    uint64_t total_time = 0;
    uint64_t total_unsettled_time = 0;
    ds_dlist_foreach (&m->settle_intervals, interval)
    {
        total_time += interval->duration_nsec;
        if (interval->state == OSW_CONFSYNC_WATCHDOG_UNSETTLED) total_unsettled_time += interval->duration_nsec;
    }

    if (osw_confsync_watchdog_threshold_condition(m, total_time, total_unsettled_time))
    {
        LOGW(LOG_PREFIX(
                "confsync was unsettled at least %f%% of time in last %d s",
                OSW_CONFSYNC_WATCHDOG_PERCENTAGE_THRESHOLD * 100,
                OSW_CONFSYNC_WATCHDOG_MAX_INTERVALS_SUM_SEC * 100));
        if (m->fatal_fn != NULL) m->fatal_fn(m->fatal_priv);
    }
}

static void osw_confsync_watchdog_record_remove_old(struct osw_confsync_watchdog *m)
{
    struct osw_confsync_watchdog_interval *interval, *tmp;
    uint64_t sum_of_durations = 0;
    ds_dlist_foreach_safe (&m->settle_intervals, interval, tmp)
    {
        sum_of_durations += interval->duration_nsec;
        if (sum_of_durations > OSW_TIME_SEC(OSW_CONFSYNC_WATCHDOG_MAX_INTERVALS_SUM_SEC))
        {
            // Once this condition is true, it will be true until the end of the loop.
            LOGT(LOG_PREFIX_INTERVAL(interval, "removing"));
            ds_dlist_remove(&m->settle_intervals, interval);
            FREE(interval);
        }
    }
}

static void osw_confsync_watchdog_record_add_new(struct osw_confsync_watchdog *m)
{
    struct osw_confsync_watchdog_interval *interval;
    interval = CALLOC(1, sizeof(*interval));
    /*
     * If it's unsettled we recieved, then the event we add is SETTLE
     * then we wait in unsettled
     * when settle is recieved, the unsettled gets recorded as it just finished.
     */
    interval->state = m->settled ? OSW_CONFSYNC_WATCHDOG_UNSETTLED : OSW_CONFSYNC_WATCHDOG_SETTLED;

    const uint64_t now = osw_time_mono_clk();
    interval->duration_nsec = now - m->last_event_timestamp_nsec;
    m->last_event_timestamp_nsec = now;

    LOGT(LOG_PREFIX_INTERVAL(interval, "inserting"));

    ds_dlist_insert_head(&m->settle_intervals, interval);
}

static void osw_confsync_watchdog_record(struct osw_confsync_watchdog *m)
{
    osw_confsync_watchdog_record_add_new(m);
    osw_confsync_watchdog_threshold_check(m);
    osw_confsync_watchdog_record_remove_old(m);
}

static void osw_confsync_watchdog_cb(struct osw_confsync *cs, void *priv)
{
    struct osw_confsync_watchdog *m = (struct osw_confsync_watchdog *)priv;
    const enum osw_confsync_state cs_state = osw_confsync_get_state(cs);

    const bool state_changed = osw_confsync_watchdog_cs_state_to_cs_w_state(m, cs_state);

    if (state_changed == true)
    {
        if (m->settled)
            osw_confsync_watchdog_switch_settled(m);
        else
            osw_confsync_watchdog_switch_unsettled(m);
        osw_confsync_watchdog_record(m);
    }
}

static void osw_confsync_watchdog_init(struct osw_confsync_watchdog *m)
{
    /*
     * In the beginning when God created the Confsync,
     * The Confsync was settled and it occured on the boot.
     */
    m->settled = true;
    m->last_event_timestamp_nsec = 0;
    ds_dlist_init(&m->settle_intervals, struct osw_confsync_watchdog_interval, dnode);
    osw_timer_init(&m->not_settled, osw_confsync_watchdog_unsettled_notify);
}

static void osw_confsync_watchdog_attach(struct osw_confsync_watchdog *m)
{
    OSW_MODULE_LOAD(osw_conf);
    OSW_MODULE_LOAD(osw_confsync);

    struct osw_confsync *confsync = osw_confsync_get();
    osw_confsync_watchdog_set_fatal_fn(m, &osw_confsync_watchdog_send_sigabrt, NULL);
    struct osw_confsync_changed *cb_handler =
            osw_confsync_register_changed_fn(confsync, __FILE__, &osw_confsync_watchdog_cb, m);
    m->cb_handler = cb_handler;
}

OSW_MODULE(osw_confsync_watchdog)
{
    static struct osw_confsync_watchdog m;
    osw_confsync_watchdog_init(&m);
    osw_confsync_watchdog_attach(&m);
    return &m;
}

static void osw_confsync_watchdog_test_inc(void *priv)
{
    if (priv == NULL) return;

    uint64_t *fatals = (uint64_t *)(priv);
    (*fatals) += 1;
}

static void osw_confsync_watchdog_test_settled(struct osw_confsync_watchdog *m)
{
    enum osw_confsync_state cs = OSW_CONFSYNC_IDLE;
    bool state_changed = osw_confsync_watchdog_cs_state_to_cs_w_state(m, cs);
    assert(state_changed == true);
    assert(m->settled == true);
}

static void osw_confsync_watchdog_test_unsettled(struct osw_confsync_watchdog *m)
{
    enum osw_confsync_state cs = OSW_CONFSYNC_WAITING;
    bool state_changed = osw_confsync_watchdog_cs_state_to_cs_w_state(m, cs);
    assert(state_changed == true);
    assert(m->settled == false);
}

OSW_UT(osw_confsync_watchdog_test_doesnt_settle)
{
    struct osw_confsync_watchdog m = {0};
    osw_confsync_watchdog_init(&m);
    uint64_t *fatals = CALLOC(1, sizeof(*fatals));
    osw_confsync_watchdog_set_fatal_fn(&m, &osw_confsync_watchdog_test_inc, fatals);

    osw_confsync_watchdog_test_unsettled(&m);
    osw_confsync_watchdog_switch_unsettled(&m);

    assert(osw_timer_is_armed(&m.not_settled) == true);
    osw_ut_time_advance(OSW_TIME_SEC(OSW_CONFSYNC_WATCHDOG_MAX_NOT_SETTLED_SEC + 1));
    assert(osw_timer_is_armed(&m.not_settled) == false);
    FREE(fatals);
}

OSW_UT(osw_confsync_watchdog_test_many_events_ok)
{
    struct osw_confsync_watchdog m = {0};
    osw_confsync_watchdog_init(&m);
    uint64_t *fatals = CALLOC(1, sizeof(*fatals));
    osw_confsync_watchdog_set_fatal_fn(&m, &osw_confsync_watchdog_test_inc, fatals);
    uint64_t n = OSW_CONFSYNC_WATCHDOG_MAX_INTERVALS_SUM_SEC / 2;

    for (uint64_t i = 0; i < n; i++)
    {
        osw_confsync_watchdog_test_unsettled(&m);
        osw_confsync_watchdog_record(&m);
        osw_ut_time_advance(OSW_TIME_SEC(1));

        osw_confsync_watchdog_test_settled(&m);
        osw_confsync_watchdog_record(&m);
        osw_ut_time_advance(OSW_TIME_SEC(1));

        assert(*fatals == 0);
    }
    osw_ut_time_advance(OSW_TIME_SEC(1));
    assert(*fatals == 0);
    FREE(fatals);
}

OSW_UT(osw_confsync_watchdog_test_many_events_fail)
{
    struct osw_confsync_watchdog m = {0};
    osw_confsync_watchdog_init(&m);
    uint64_t *fatals = CALLOC(1, sizeof(*fatals));
    osw_confsync_watchdog_set_fatal_fn(&m, &osw_confsync_watchdog_test_inc, fatals);
    uint64_t n = (OSW_CONFSYNC_WATCHDOG_MAX_INTERVALS_SUM_SEC / 5) + 1;

    for (uint64_t i = 0; i < n; i++)
    {
        osw_confsync_watchdog_test_unsettled(&m);
        osw_confsync_watchdog_record(&m);
        osw_ut_time_advance(OSW_TIME_SEC(4));

        osw_confsync_watchdog_test_settled(&m);
        osw_confsync_watchdog_record(&m);
        osw_ut_time_advance(OSW_TIME_SEC(1));
    }
    assert((*fatals) > 0);
    FREE(fatals);
}

OSW_UT(osw_confsync_watchdog_test_long_events_ok)
{
    struct osw_confsync_watchdog m = {0};
    osw_confsync_watchdog_init(&m);
    uint64_t *fatals = CALLOC(1, sizeof(*fatals));
    osw_confsync_watchdog_set_fatal_fn(&m, &osw_confsync_watchdog_test_inc, fatals);
    uint64_t n = 50;

    for (uint64_t i = 0; i < n; i++)
    {
        osw_confsync_watchdog_test_unsettled(&m);
        osw_confsync_watchdog_record(&m);
        osw_ut_time_advance(OSW_TIME_SEC(220));  // slightly less then 75% of 5 mins

        assert(*fatals == 0);

        osw_confsync_watchdog_test_settled(&m);
        osw_confsync_watchdog_record(&m);
        osw_ut_time_advance(OSW_TIME_SEC(30000));

        assert(*fatals == 0);
    }
    assert(*fatals == 0);
    FREE(fatals);
}

OSW_UT(osw_confsync_watchdog_test_long_events_fail)
{
    struct osw_confsync_watchdog m = {0};
    osw_confsync_watchdog_init(&m);
    uint64_t *fatals = CALLOC(1, sizeof(*fatals));
    osw_confsync_watchdog_set_fatal_fn(&m, &osw_confsync_watchdog_test_inc, fatals);
    uint64_t n = 50;

    for (uint64_t i = 0; i < n; i++)
    {
        osw_confsync_watchdog_test_unsettled(&m);
        osw_confsync_watchdog_record(&m);
        osw_ut_time_advance(OSW_TIME_SEC(230));  // slightly more then 75% of 5 mins
        osw_confsync_watchdog_switch_unsettled(&m);
        assert(osw_timer_is_armed(&m.not_settled) == true);

        osw_confsync_watchdog_test_settled(&m);
        osw_confsync_watchdog_record(&m);
        osw_ut_time_advance(OSW_TIME_SEC(30000));
        assert(*fatals == (i + 1));
    }
    FREE(fatals);
}

OSW_UT(osw_confsync_watchdog_test_long_settle)
{
    struct osw_confsync_watchdog m = {0};
    osw_confsync_watchdog_init(&m);
    uint64_t *fatals = CALLOC(1, sizeof(*fatals));
    osw_confsync_watchdog_set_fatal_fn(&m, &osw_confsync_watchdog_test_inc, fatals);

    osw_confsync_watchdog_test_unsettled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_MSEC(1));
    assert(*fatals == 0);

    osw_confsync_watchdog_test_settled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(300000));
    assert(*fatals == 0);

    osw_confsync_watchdog_test_unsettled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_MSEC(1));
    assert(*fatals == 0);
    FREE(fatals);
}

OSW_UT(osw_confsync_watchdog_test_long_unsettle)
{
    struct osw_confsync_watchdog m = {0};
    osw_confsync_watchdog_init(&m);
    uint64_t *fatals = CALLOC(1, sizeof(*fatals));
    osw_confsync_watchdog_set_fatal_fn(&m, &osw_confsync_watchdog_test_inc, fatals);

    osw_confsync_watchdog_test_unsettled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(OSW_CONFSYNC_WATCHDOG_MAX_INTERVALS_SUM_SEC + 1));

    osw_confsync_watchdog_test_settled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    assert(*fatals == 1);
    FREE(fatals);
}

OSW_UT(osw_confsync_watchdog_test_tricky_ok)
{
    struct osw_confsync_watchdog m = {0};
    osw_confsync_watchdog_init(&m);
    uint64_t *fatals = CALLOC(1, sizeof(*fatals));
    osw_confsync_watchdog_set_fatal_fn(&m, &osw_confsync_watchdog_test_inc, fatals);

    osw_confsync_watchdog_test_unsettled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(3 * 60));

    osw_confsync_watchdog_test_settled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(119));  // 4min 59s - 3min 45s

    osw_confsync_watchdog_test_unsettled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(45));

    assert(*fatals == 0);
    FREE(fatals);
}

OSW_UT(osw_confsync_watchdog_test_bootup)
{
    struct osw_confsync_watchdog m = {0};
    osw_confsync_watchdog_init(&m);
    uint64_t *fatals = CALLOC(1, sizeof(*fatals));
    osw_confsync_watchdog_set_fatal_fn(&m, &osw_confsync_watchdog_test_inc, fatals);

    osw_confsync_watchdog_test_unsettled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(99));

    osw_confsync_watchdog_test_settled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(1));

    assert(*fatals == 0);
    FREE(fatals);
}

OSW_UT(osw_confsync_watchdog_test_bad_divide)
{
    struct osw_confsync_watchdog m = {0};
    osw_confsync_watchdog_init(&m);
    uint64_t *fatals = CALLOC(1, sizeof(*fatals));
    osw_confsync_watchdog_set_fatal_fn(&m, &osw_confsync_watchdog_test_inc, fatals);

    osw_ut_time_init();
    osw_confsync_watchdog_test_unsettled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    osw_confsync_watchdog_test_settled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(OSW_CONFSYNC_WATCHDOG_MAX_INTERVALS_SUM_SEC - 2));
    osw_confsync_watchdog_test_unsettled(&m);
    osw_confsync_watchdog_record(&m);
    osw_ut_time_advance(OSW_TIME_SEC(1));
    osw_confsync_watchdog_test_settled(&m);
    osw_confsync_watchdog_record(&m);

    assert(*fatals == 0);
    FREE(fatals);
}
