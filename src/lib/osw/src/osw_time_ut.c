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

#include <unistd.h>
#include <float.h>
#include <math.h>
#include <ev.h>
#include <osw_ut.h>

OSW_UT(osw_time_ut_macros)
{
    assert(OSW_TIME_SEC(4) == 4e9);
    assert(OSW_TIME_MSEC(300) == 0.3e9);
    assert((OSW_TIME_TO_DBL(13.l - OSW_TIME_TO_DBL(OSW_TIME_SEC(13))) <=  DBL_EPSILON) &&
           (OSW_TIME_TO_DBL(13.l - OSW_TIME_TO_DBL(OSW_TIME_SEC(13))) >= -DBL_EPSILON));
}

OSW_UT(osw_time_ut_mono_clk_override)
{
    uint64_t tstamp;

    tstamp = osw_time_mono_clk();
    assert(tstamp > 0);

    osw_time_set_mono_clk(357);
    tstamp = osw_time_mono_clk();
    assert(tstamp == 357);
    tstamp = osw_time_mono_clk();
    assert(tstamp == 357);

    osw_time_set_mono_clk(654321);
    tstamp = osw_time_mono_clk();
    assert(tstamp == 654321);
}
