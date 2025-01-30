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

#include <limits.h>
#include <stdlib.h>

#include "os_random.h"
#include "osa_assert.h"

/* Maximum numbers of shifts allowed in an `int` type */
#define BACKOFF_MAX_STEP ((int)(sizeof(int) * 8 - 2))

/*
 * Function for calculating backoff timer for (connection) retries
 *
 * @param[in]   step    Current step (retry attempt), must be >= 0
 * @param[in]   min     Minimum interval, must be >= 1
 * @param[in]   max     Maximum interval, must be > min
 *
 * The function uses exponential formula (min << step), but ensures there's
 * always a random component to the timer. The timer never exceeds @p max.
 */
int netutil_backoff_time(int step, int min, int max)
{
    int backoff;

    ASSERT(min > 0, "min must be greater than 0");
    ASSERT(max > min, "max must be greater than min");

    if (step < 0 || step >= BACKOFF_MAX_STEP)
    {
        step = BACKOFF_MAX_STEP;
    }
    else
    {
        step++;
    }

    if (min > (INT_MAX / (1 << step)))
    {
        backoff = max;
    }
    else
    {
        backoff = min << step;
        if (backoff > max) backoff = max;
    }

    /* Add a random component */
    backoff += os_random() % backoff;
    backoff >>= 1;

    return backoff;
}
