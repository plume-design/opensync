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

#include "otbr_timeout.h"
#include "osa_assert.h"

static inline void NONNULL(1, 2) set_loop(ev_timer *const timer, struct ev_loop *const loop)
{
    timer->data = loop;
}

static inline struct ev_loop *NONNULL(1) get_loop(const ev_timer *const timer)
{
    ASSERT(timer->data != NULL, "Timeout is not initialized");
    return timer->data;
}

static void NONNULL(1, 2) ev_timer_dummy_callback(struct ev_loop *const loop, ev_timer *const timer, const int r_events)
{
    /* No need to do anything here */
    (void)loop;
    (void)timer;
    (void)r_events;
}

void otbr_timeout_init(struct ev_loop *const loop, ev_timer *const timer)
{
    ev_timer_init(timer, ev_timer_dummy_callback, 0, 0);
    set_loop(timer, loop);
}

void otbr_timeout_start(ev_timer *const timer, const float seconds)
{
    if (ev_is_active(timer))
    {
        ev_timer_stop(get_loop(timer), timer);
    }
    /* The relative timeouts are calculated relative to the ev_now() time, which can differ significantly
     * from the current ev_time() time, if the event loop is blocked for a long time. */
    ev_now_update(get_loop(timer));
    ev_timer_set(timer, seconds, 0);
    ev_timer_start(get_loop(timer), timer);
}

float otbr_timeout_remaining(ev_timer *const timer)
{
    return ev_is_active(timer) ? (float)ev_timer_remaining(get_loop(timer), timer) : 0;
}

bool otbr_timeout_tick(ev_timer *const timer, const bool block, float *const remaining)
{
    const bool rc = (ev_run(get_loop(timer), block ? EVRUN_ONCE : EVRUN_NOWAIT) && ev_is_active(timer));

    if (remaining != NULL)
    {
        *remaining = rc ? (float)ev_timer_remaining(get_loop(timer), timer) : 0;
    }

    return rc;
}

bool otbr_timeout_sleep(ev_timer *const timer, float seconds)
{
    float remaining;
    bool rc;

    ASSERT(seconds <= 86400, "Sleep interval too long");

    if (seconds <= 0)
    {
        return true;
    }

    remaining = otbr_timeout_remaining(timer);
    if (seconds > remaining)
    {
        seconds = remaining;
        rc = false;
    }
    else
    {
        rc = true;
    }

    ev_sleep(seconds);
    return rc;
}

bool otbr_timeout_stop(ev_timer *const timer)
{
    if (ev_is_active(timer))
    {
        ev_timer_stop(get_loop(timer), timer);
        return true;
    }
    return false;
}
