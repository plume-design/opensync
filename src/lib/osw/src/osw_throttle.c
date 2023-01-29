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
#include <stdbool.h>
#include <log.h>
#include <memutil.h>
#include <osw_time.h>
#include <osw_throttle.h>

enum osw_throttle_type {
    OSW_THROTTLE_RATE_LIMIT,
};

struct osw_throttle_rate_limit {
    const unsigned int limit;
    const uint64_t period_nsec;

    unsigned int counter;
    uint64_t first_tap_nsec;
    uint64_t next_at_nsec;
};

struct osw_throttle {
    enum osw_throttle_type type;
    union {
        struct osw_throttle_rate_limit rate_limit;
    } data;
};

static bool
osw_throttle_rate_limit_tap(struct osw_throttle *throttle,
                            uint64_t now_nsec,
                            uint64_t *next_at_nsec)
{
    ASSERT(throttle != NULL, "");

    struct osw_throttle_rate_limit *rate_limit = &throttle->data.rate_limit;
    bool result = true;

    if (rate_limit->next_at_nsec > now_nsec) {
        result = false;
        goto finish;
    }

    if ((now_nsec - rate_limit->first_tap_nsec) >= rate_limit->period_nsec) {
        rate_limit->first_tap_nsec = now_nsec;
        rate_limit->counter = 0;
    }

    if (rate_limit->counter >= rate_limit->limit) {
        result = false;
        goto finish;
    }

    rate_limit->counter++;
    if (rate_limit->counter == rate_limit->limit)
        rate_limit->next_at_nsec = now_nsec + rate_limit->period_nsec;
    else
        rate_limit->next_at_nsec = now_nsec;

    finish:
        *next_at_nsec = rate_limit->next_at_nsec;
        return result;
}

static void
osw_throttle_rate_limit_reset(struct osw_throttle *throttle)
{
    ASSERT(throttle != NULL, "");

    struct osw_throttle_rate_limit *rate_limit = &throttle->data.rate_limit;

    rate_limit->counter = 0;
    rate_limit->first_tap_nsec = 0;
    rate_limit->next_at_nsec = 0;
}

struct osw_throttle*
osw_throttle_new_rate_limit(unsigned int limit,
                            uint64_t period_nsec)
{
    const struct osw_throttle_rate_limit rate_limit = {
        .limit = limit,
        .period_nsec = period_nsec,
        .counter = 0,
        .first_tap_nsec = 0,
        .next_at_nsec = 0,
    };
    struct osw_throttle *throttle = CALLOC(1, sizeof(*throttle));

    throttle->type = OSW_THROTTLE_RATE_LIMIT;
    memcpy(&throttle->data.rate_limit, &rate_limit, sizeof(throttle->data.rate_limit));

    return throttle;
}

void
osw_throttle_free(struct osw_throttle *throttle)
{
    FREE(throttle);
}

bool
osw_throttle_tap(struct osw_throttle *throttle,
                 uint64_t *next_at_nsec)
{
    ASSERT(throttle != NULL, "");
    ASSERT(next_at_nsec != NULL, "");

    const uint64_t now_nsec = osw_time_mono_clk();

    switch (throttle->type) {
        case OSW_THROTTLE_RATE_LIMIT:
            return osw_throttle_rate_limit_tap(throttle, now_nsec, next_at_nsec);
    };

    ASSERT(false, "osw: throttle: unreachable code");
    return false;
}

void
osw_throttle_reset(struct osw_throttle *throttle)
{
    ASSERT(throttle != NULL, "");

    switch (throttle->type) {
        case OSW_THROTTLE_RATE_LIMIT:
            return osw_throttle_rate_limit_reset(throttle);
    };
}

#include "osw_throttle_ut.c"
