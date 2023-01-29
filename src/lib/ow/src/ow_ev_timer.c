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

#include <stdint.h>
#include <inttypes.h>
#include <ev.h>
#include <const.h>
#include <log.h>
#include <ds_dlist.h>
#include <osa_assert.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_module.h>

#define OW_EV_MAX_OVERRUN_NSEC OSW_TIME_SEC(5)

struct ow_ev_timer {
    ev_prepare prepare;
    ev_timer timer;
};

struct ow_ev_timer g_ev_timer;

static void
ow_ev_timer_cb(EV_P_ ev_timer *arg,
               int events)
{
    const uint64_t now_nsec = osw_time_mono_clk();
    osw_timer_core_dispatch(now_nsec);
}

static void
ow_ev_prepare_cb(EV_P_ ev_prepare *arg,
                 int events)
{
    struct ow_ev_timer *timer = container_of(arg, struct ow_ev_timer, prepare);
    const uint64_t now_nsec = osw_time_mono_clk();
    uint64_t next_at_nsec;
    double next_at_dbl;
    bool result;

    result = osw_timer_core_get_next_at(&next_at_nsec);
    if (result == false)
        return;

    if (next_at_nsec == 0) {
        next_at_dbl = 0.;
    }
    else if (next_at_nsec >= now_nsec) {
        next_at_dbl = OSW_TIME_TO_DBL(next_at_nsec - now_nsec);
    }
    else /* now_nsec > next_at_nsec, overrun */ {
        next_at_dbl = 0.;
        LOGD("ow: ev_timer: timer overrun next_at_nsec: %"PRIu64" is earlier than now_nsec: %"PRIu64", setting timer at sec: %.2f",
             next_at_nsec, now_nsec, next_at_dbl);
        const uint64_t overrun_nsec = now_nsec - next_at_nsec;
        WARN_ON(overrun_nsec >= OW_EV_MAX_OVERRUN_NSEC);
    }

    ev_timer_stop(EV_A_ &timer->timer);
    ev_timer_set(&timer->timer, next_at_dbl, 0.);
    ev_timer_start(EV_A_ &timer->timer);
}

OSW_MODULE(ow_ev_timer)
{
    struct ev_loop *loop = OSW_MODULE_LOAD(osw_ev);

    ev_prepare_init(&g_ev_timer.prepare, ow_ev_prepare_cb);
    ev_timer_init(&g_ev_timer.timer, ow_ev_timer_cb, 0., 0.);
    ev_prepare_start(loop, &g_ev_timer.prepare);
    ev_timer_start(loop, &g_ev_timer.timer);

    LOGI("ow: ev_timer: initialized");
    return NULL;
}
