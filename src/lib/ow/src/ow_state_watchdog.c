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
#include <inttypes.h>
#include <log.h>
#include <const.h>
#include <osw_state.h>
#include <osw_mux.h>
#include <osw_module.h>
#include <osw_time.h>
#include <osw_timer.h>

/* Purpose
 *
 * Provide best-effort hints in case osw driver
 * implementations are buggy and don't report state change
 * reports when underlying system state actually changes.
 *
 * It is possible to override operational timings via
 * environment variables matching the #define names.
 */

#define OW_STATE_WATCHDOG_GRACE_NSEC OSW_TIME_SEC(5)
#define OW_STATE_WATCHDOG_POLL_NSEC OSW_TIME_SEC(5)
#define OW_STATE_WATCHDOG_PERIOD_NSEC OSW_TIME_SEC(5 * 60) /* 5 minutes */
#define OW_STATE_WATCHDOG_POSTPONE_NSEC OSW_TIME_SEC(5 * 60) /* 5 minutes */

enum state {
    ARM_PERIOD, /* -> WAITING_FOR_PERIOD */
    WAITING_FOR_PERIOD, /* -> WAITING_FOR_IDLE */
    WAITING_FOR_IDLE, /* -> [POLL | POLL_FORCED] */
    POLL, /* -> WAITING_FOR_POLL */
    POLL_FORCED, /* -> ARM_PERIOD */
    WAITING_FOR_POLL, /* -> WAITING_FOR_POLL_FORCED */
    WAITING_FOR_POLL_FORCED, /* -> ARM_PERIOD */
};

struct ow_state_watchdog {
    struct osw_state_observer state_obs;

    enum state state;

    struct osw_timer period;
    struct osw_timer grace;
    struct osw_timer poll;
    struct osw_timer postpone;

    uint64_t grace_nsec;
    uint64_t poll_nsec;
    uint64_t period_nsec;
    uint64_t postpone_nsec;

    unsigned int changes_while_polling;
};

static void
arm_poll(struct ow_state_watchdog *wdog, const uint64_t now)
{
    const uint64_t poll_at = now + wdog->poll_nsec;
    osw_timer_arm_at_nsec(&wdog->poll, poll_at);
    wdog->changes_while_polling = 0;
}

static void
arm_period(struct ow_state_watchdog *wdog, const uint64_t now)
{
    const uint64_t period_at = now + wdog->period_nsec;
    osw_timer_arm_at_nsec(&wdog->period, period_at);
}

static void
arm_grace(struct ow_state_watchdog *wdog, const uint64_t now)
{
    const uint64_t at = now + wdog->grace_nsec;
    osw_timer_arm_at_nsec(&wdog->grace, at);
}

static void
arm_postpone(struct ow_state_watchdog *wdog, const uint64_t now)
{
    const uint64_t at = now + wdog->postpone_nsec;
    osw_timer_arm_at_nsec(&wdog->postpone, at);
}

static void
update_count(struct ow_state_watchdog *wdog)
{
    const bool should_update = osw_timer_is_armed(&wdog->poll) == true;
    if (should_update == true) wdog->changes_while_polling++;
    LOGT("ow: state watchdog: changes = %u (should %d)", wdog->changes_while_polling, should_update);
}

static void
note_change(struct ow_state_watchdog *wdog, const uint64_t now)
{
    arm_grace(wdog, now);
    update_count(wdog);
}

static const char *
state_to_str(const enum state s)
{
    switch (s) {
        case WAITING_FOR_PERIOD: return "waiting_for_period";
        case WAITING_FOR_IDLE: return "waiting_for_idle";
        case POLL: return "poll";
        case POLL_FORCED: return "poll_forced";
        case WAITING_FOR_POLL: return "waiting_for_poll";
        case WAITING_FOR_POLL_FORCED: return "waiting_for_poll_forced";
        case ARM_PERIOD: return "arm_period";
    }
    return "?";
}

static void
set_state(struct ow_state_watchdog *wdog, const enum state s)
{
    if (wdog->state == s) return;
    const char *from = state_to_str(wdog->state);
    const char *to = state_to_str(s);
    LOGT("ow: state watchdog: state %s -> %s", from, to);
    wdog->state = s;
}

static void
do_work(struct ow_state_watchdog *wdog,
        const uint64_t now)
{
    int budget = 64;

    for (;;) {
        LOGT("ow: state watchdog: work: budget=%d", budget);
        enum state last = wdog->state;
        switch (wdog->state) {
            case WAITING_FOR_PERIOD:
                if (osw_timer_is_armed(&wdog->period) == false) {
                    arm_postpone(wdog, now);
                    set_state(wdog, WAITING_FOR_IDLE);
                }
                break;
            case WAITING_FOR_IDLE:
                if (osw_timer_is_armed(&wdog->postpone) == false) {
                    set_state(wdog, POLL_FORCED);
                    break;
                }
                if (osw_timer_is_armed(&wdog->grace) == false) {
                    set_state(wdog, POLL);
                    break;
                }
                break;
            case POLL:
                osw_mux_poll();
                arm_poll(wdog, now);
                set_state(wdog, WAITING_FOR_POLL);
                break;
            case POLL_FORCED:
                osw_mux_poll();
                arm_poll(wdog, now);
                set_state(wdog, WAITING_FOR_POLL_FORCED);
                break;
            case WAITING_FOR_POLL:
                if (osw_timer_is_armed(&wdog->poll) == false) {
                    if (wdog->changes_while_polling > 0) {
                        const unsigned int n = wdog->changes_while_polling;
                        LOGN("ow: state watchdog: %u changes observed during poll, possible driver bug", n);
                    }
                    set_state(wdog, ARM_PERIOD);
                    break;
                }
                break;
            case WAITING_FOR_POLL_FORCED:
                if (osw_timer_is_armed(&wdog->poll) == false) {
                    if (wdog->changes_while_polling > 0) {
                        const unsigned int n = wdog->changes_while_polling;
                        LOGI("ow: state watchdog: %u changes observed during poll", n);
                    }
                    set_state(wdog, ARM_PERIOD);
                    break;
                }
                break;
            case ARM_PERIOD:
                arm_period(wdog, now);
                set_state(wdog, WAITING_FOR_PERIOD);
                break;
        }

        assert(--budget);

        if (last == wdog->state)
            break;
    }
}

static void
changed_cb(struct osw_state_observer *o)
{
    struct ow_state_watchdog *wdog = container_of(o, struct ow_state_watchdog, state_obs);
    const uint64_t now = osw_time_mono_clk();

    note_change(wdog, now);
    do_work(wdog, now);
}

static void
changed_phy_cb(struct osw_state_observer *o, const struct osw_state_phy_info *info)
{
    changed_cb(o);
}

static void
changed_vif_cb(struct osw_state_observer *o, const struct osw_state_vif_info *info)
{
    changed_cb(o);
}

static void
changed_sta_cb(struct osw_state_observer *o, const struct osw_state_sta_info *info)
{
    changed_cb(o);
}

#define DO_WORK(fn, m) \
static void \
fn(struct osw_timer *t) \
{ \
    struct ow_state_watchdog *wdog = container_of(t, struct ow_state_watchdog, m); \
    const uint64_t now = osw_time_mono_clk(); \
 \
    LOGT("ow: state watchdog: "#m" done"); \
    do_work(wdog, now); \
}

DO_WORK(poll_cb, poll);
DO_WORK(grace_cb, grace);
DO_WORK(period_cb, period);
DO_WORK(postpone_cb, postpone);

#define SET_PARAM(w, n, e) do { \
        const char *v = getenv(#e); \
        if (v != NULL) (w)->n = strtoull(v, NULL, 10); \
    } while (0)

OSW_MODULE(ow_state_watchdog)
{
    OSW_MODULE_LOAD(osw_state);
    OSW_MODULE_LOAD(osw_timer);
    OSW_MODULE_LOAD(osw_mux);
    static struct ow_state_watchdog wdog = {
        .state = ARM_PERIOD,
        .grace_nsec = OW_STATE_WATCHDOG_GRACE_NSEC,
        .poll_nsec = OW_STATE_WATCHDOG_POLL_NSEC,
        .period_nsec = OW_STATE_WATCHDOG_PERIOD_NSEC,
        .postpone_nsec = OW_STATE_WATCHDOG_POSTPONE_NSEC,
        .grace = { .cb = grace_cb },
        .poll = { .cb = poll_cb },
        .period = { .cb = period_cb },
        .postpone = { .cb = postpone_cb },
        .state_obs = {
            .name = __FILE__,
            .phy_added_fn = changed_phy_cb,
            .phy_removed_fn = changed_phy_cb,
            .phy_changed_fn = changed_phy_cb,
            .vif_added_fn = changed_vif_cb,
            .vif_removed_fn = changed_vif_cb,
            .vif_changed_fn = changed_vif_cb,
            .sta_connected_fn = changed_sta_cb,
            .sta_disconnected_fn = changed_sta_cb,
            .sta_changed_fn = changed_sta_cb,
        },
    };
    SET_PARAM(&wdog, grace_nsec, OW_STATE_WATCHDOG_GRACE_NSEC);
    SET_PARAM(&wdog, poll_nsec, OW_STATE_WATCHDOG_POLL_NSEC);
    SET_PARAM(&wdog, period_nsec, OW_STATE_WATCHDOG_PERIOD_NSEC);
    SET_PARAM(&wdog, postpone_nsec, OW_STATE_WATCHDOG_POSTPONE_NSEC);
    LOGI("ow: state watchdog: params:"
         " grace=%"PRIu64
         " poll=%"PRIu64
         " period=%"PRIu64
         " postpone=%"PRIu64,
         wdog.grace_nsec,
         wdog.poll_nsec,
         wdog.period_nsec,
         wdog.postpone_nsec);
    osw_state_register_observer(&wdog.state_obs);
    const uint64_t now = osw_time_mono_clk();
    do_work(&wdog, now);
    return NULL;
}
