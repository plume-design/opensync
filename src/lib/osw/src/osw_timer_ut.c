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

#include <const.h>
#include <ev.h>
#include <osw_ut.h>

struct osw_timer_ut_dummy {
    bool dispatched;
    struct osw_timer timer;
};

static void
osw_timer_ut_dummy_cb(struct osw_timer *timer)
{
    struct osw_timer_ut_dummy* dummy = (struct osw_timer_ut_dummy*) container_of(timer, struct osw_timer_ut_dummy, timer);
    dummy->dispatched = true;
}

static void
osw_timer_ut_nop_cb(struct osw_timer *timer)
{
    /* nop */
}

OSW_UT(osw_timer_ut_lifecycle)
{
    struct osw_timer_ut_dummy dummy_a;
    struct osw_timer_ut_dummy dummy_b;
    struct osw_timer_ut_dummy dummy_c;
    uint64_t next_at_nsec = 0;

    memset(&dummy_a, 0, sizeof(dummy_a));
    memset(&dummy_b, 0, sizeof(dummy_b));
    memset(&dummy_c, 0, sizeof(dummy_c));

    assert(osw_timer_core_get_next_at(&next_at_nsec) == false);

    /* Initialize all timers */
    osw_timer_init(&dummy_a.timer, osw_timer_ut_dummy_cb);
    osw_timer_init(&dummy_b.timer, osw_timer_ut_dummy_cb);
    osw_timer_init(&dummy_c.timer, osw_timer_ut_dummy_cb);
    assert(osw_timer_is_armed(&dummy_a.timer) == false);
    assert(osw_timer_is_armed(&dummy_b.timer) == false);
    assert(osw_timer_is_armed(&dummy_c.timer) == false);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == false);

    /*
     * Arm timer in following order (first is the earliest), expected stages:
     * - A
     * - B, A
     * - C, B, A
     */
    osw_timer_arm_at_nsec(&dummy_a.timer, 300);
    assert(osw_timer_is_armed(&dummy_a.timer) == true);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == true);
    assert(next_at_nsec == 300);

    osw_timer_arm_at_nsec(&dummy_b.timer, 200);
    assert(osw_timer_is_armed(&dummy_b.timer) == true);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == true);
    assert(next_at_nsec == 200);

    osw_timer_arm_at_nsec(&dummy_c.timer, 100);
    assert(osw_timer_is_armed(&dummy_c.timer) == true);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == true);
    assert(next_at_nsec == 100);

    /*
     * Disarm timers, expected stages
     * - C, A (disarm B)
     * - A (disarm C)
     */
    osw_timer_disarm(&dummy_b.timer);
    assert(osw_timer_is_armed(&dummy_b.timer) == false);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == true);
    assert(next_at_nsec == 100);

    osw_timer_disarm(&dummy_c.timer);
    assert(osw_timer_is_armed(&dummy_c.timer) == false);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == true);
    assert(next_at_nsec == 300);

    /* Rearm timers B and C */
    osw_timer_arm_at_nsec(&dummy_b.timer, 200);
    osw_timer_arm_at_nsec(&dummy_c.timer, 100);
    assert(osw_timer_is_armed(&dummy_b.timer) == true);
    assert(osw_timer_is_armed(&dummy_c.timer) == true);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == true);
    assert(next_at_nsec == 100);

    /* Dispatch at 50ns - no timer should fire */
    osw_timer_core_dispatch(50);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == true);
    assert(next_at_nsec == 100);
    assert(osw_timer_is_armed(&dummy_a.timer) == true);
    assert(osw_timer_is_armed(&dummy_b.timer) == true);
    assert(osw_timer_is_armed(&dummy_c.timer) == true);
    assert(dummy_a.dispatched == false);
    assert(dummy_b.dispatched == false);
    assert(dummy_c.dispatched == false);

    /* Dispatch at 75ns - no timer should fire */
    osw_timer_core_dispatch(75);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == true);
    assert(next_at_nsec == 100);
    assert(osw_timer_is_armed(&dummy_a.timer) == true);
    assert(osw_timer_is_armed(&dummy_b.timer) == true);
    assert(osw_timer_is_armed(&dummy_c.timer) == true);
    assert(dummy_a.dispatched == false);
    assert(dummy_b.dispatched == false);
    assert(dummy_c.dispatched == false);

    /* Dispatch at 100ns - C should fire */
    osw_timer_core_dispatch(100);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == true);
    assert(next_at_nsec == 200);
    assert(osw_timer_is_armed(&dummy_a.timer) == true);
    assert(osw_timer_is_armed(&dummy_b.timer) == true);
    assert(osw_timer_is_armed(&dummy_c.timer) == false);
    assert(dummy_a.dispatched == false);
    assert(dummy_b.dispatched == false);
    assert(dummy_c.dispatched == true);

    /* Dispatch at 300ns - A & B should fire */
    dummy_c.dispatched = false;
    osw_timer_core_dispatch(300);
    assert(osw_timer_core_get_next_at(&next_at_nsec) == false);
    assert(osw_timer_is_armed(&dummy_a.timer) == false);
    assert(osw_timer_is_armed(&dummy_b.timer) == false);
    assert(osw_timer_is_armed(&dummy_c.timer) == false);
    assert(dummy_a.dispatched == true);
    assert(dummy_b.dispatched == true);
    assert(dummy_c.dispatched == false);
}

OSW_UT(osw_timer_ut_ordering)
{
    struct osw_timer t0 = { .cb = osw_timer_ut_nop_cb };
    struct osw_timer t5 = { .cb = osw_timer_ut_nop_cb };
    struct osw_timer t10 = { .cb = osw_timer_ut_nop_cb };
    struct osw_timer t15 = { .cb = osw_timer_ut_nop_cb };
    struct osw_timer t20 = { .cb = osw_timer_ut_nop_cb };

    osw_timer_arm_at_nsec(&t5, 5);
    osw_timer_arm_at_nsec(&t10, 10);
    osw_timer_arm_at_nsec(&t15, 15);
    osw_timer_arm_at_nsec(&t0, 0);
    osw_timer_arm_at_nsec(&t20, 20);

    assert(ds_dlist_remove_head(&g_active_list) == &t0);
    assert(ds_dlist_remove_head(&g_active_list) == &t5);
    assert(ds_dlist_remove_head(&g_active_list) == &t10);
    assert(ds_dlist_remove_head(&g_active_list) == &t15);
    assert(ds_dlist_remove_head(&g_active_list) == &t20);
}
