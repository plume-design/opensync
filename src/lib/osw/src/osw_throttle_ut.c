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

#include <ev.h>
#include <osw_ut.h>

OSW_UT(osw_throttle_ut_rate_limit_simple)
{
    struct osw_throttle *throttle;
    uint64_t next_at_nsec;

    osw_time_set_mono_clk(OSW_TIME_SEC(0));

    /*** 1 iteration per 1s ***/
    throttle = osw_throttle_new_rate_limit(1, OSW_TIME_SEC(1));

    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_SEC(1));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == false);
    assert(next_at_nsec == OSW_TIME_SEC(1));

    osw_time_set_mono_clk(OSW_TIME_MSEC(500));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == false);
    assert(next_at_nsec == OSW_TIME_SEC(1));

    osw_time_set_mono_clk(OSW_TIME_SEC(1));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_SEC(2));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == false);
    assert(next_at_nsec == OSW_TIME_SEC(2));

    osw_throttle_free(throttle);
}

OSW_UT(osw_throttle_ut_rate_limit_complex)
{
    struct osw_throttle *throttle;
    uint64_t next_at_nsec;

    osw_time_set_mono_clk(OSW_TIME_SEC(0));

    /*** 4 iterations per 2s ***/
    throttle = osw_throttle_new_rate_limit(4, OSW_TIME_SEC(2));

    /*
     * Reach limit within 2s
     */
    osw_time_set_mono_clk(OSW_TIME_MSEC(0));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_MSEC(0));

    osw_time_set_mono_clk(OSW_TIME_MSEC(100));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_MSEC(100));

    osw_time_set_mono_clk(OSW_TIME_MSEC(200));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_MSEC(200));

    osw_time_set_mono_clk(OSW_TIME_MSEC(300));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_MSEC(2300));

    osw_time_set_mono_clk(OSW_TIME_MSEC(400));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == false);
    assert(next_at_nsec == OSW_TIME_MSEC(2300));

    /*
     * Reach limit within 2s one more time
     */
    osw_time_set_mono_clk(OSW_TIME_MSEC(3000));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_MSEC(3000));

    osw_time_set_mono_clk(OSW_TIME_MSEC(3500));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_MSEC(3500));

    osw_time_set_mono_clk(OSW_TIME_MSEC(4100));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_MSEC(4100));

    osw_time_set_mono_clk(OSW_TIME_MSEC(4800));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_MSEC(6800));

    osw_time_set_mono_clk(OSW_TIME_MSEC(5000));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == false);
    assert(next_at_nsec == OSW_TIME_MSEC(6800));

    osw_time_set_mono_clk(OSW_TIME_MSEC(6600));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == false);
    assert(next_at_nsec == OSW_TIME_MSEC(6800));

    /*
     * 6 consecutive successful taps with 1s interval
     */
    osw_time_set_mono_clk(OSW_TIME_SEC(10));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_SEC(10));

    osw_time_set_mono_clk(OSW_TIME_SEC(11));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_SEC(11));

    osw_time_set_mono_clk(OSW_TIME_SEC(12));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_SEC(12));

    osw_time_set_mono_clk(OSW_TIME_SEC(13));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_SEC(13));

    osw_time_set_mono_clk(OSW_TIME_SEC(14));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_SEC(14));

    osw_time_set_mono_clk(OSW_TIME_SEC(15));
    assert(osw_throttle_tap(throttle, &next_at_nsec) == true);
    assert(next_at_nsec == OSW_TIME_SEC(15));

    osw_throttle_free(throttle);
}
