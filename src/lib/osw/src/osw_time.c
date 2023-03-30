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

#include <time.h>
#include <stdint.h>
#include <osw_time.h>

static uint64_t g_mono_clk_nsec = UINT64_MAX;
static uint64_t g_wall_clk_nsec = UINT64_MAX;

uint64_t
osw_time_mono_clk(void)
{
    struct timespec tstamp;

    if (g_mono_clk_nsec != UINT64_MAX)
        return g_mono_clk_nsec;

    clock_gettime(CLOCK_MONOTONIC, &tstamp);
    return (tstamp.tv_sec * 1e9) + tstamp.tv_nsec;
}

uint64_t
osw_time_wall_clk(void)
{
    struct timespec tstamp;

    if (g_wall_clk_nsec != UINT64_MAX)
        return g_wall_clk_nsec;

    clock_gettime(CLOCK_REALTIME, &tstamp);
    return (tstamp.tv_sec * 1e9) + tstamp.tv_nsec;
}

void
osw_time_set_mono_clk(uint64_t nsec)
{
    g_mono_clk_nsec = nsec;
}

void
osw_time_set_wall_clk(uint64_t nsec)
{
    g_wall_clk_nsec = nsec;
}

#include "osw_time_ut.c"
