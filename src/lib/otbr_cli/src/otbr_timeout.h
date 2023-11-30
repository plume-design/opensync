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

#ifndef OTBR_TIMEOUT_H_INCLUDED
#define OTBR_TIMEOUT_H_INCLUDED

#include <ev.h>
#include <stdbool.h>

#include "otbr_cli_util.h"

/**
 * Initialize the timer for the use with other `otbr_timeout_*` functions
 *
 * @param[in] loop   The event loop to use for running the `timer`.
 * @param[in] timer  The timer to use for timeouts.
 */
void otbr_timeout_init(struct ev_loop *loop, ev_timer *timer) NONNULL(1, 2);

/**
 * Start the initialized timer
 *
 * @param[in] timer    The timer to use.
 * @param[in] seconds  The amount of time to wait before the timer times-out, in seconds.
 *
 * @note Call @ref otbr_timeout_stop after timer usage.
 */
void otbr_timeout_start(ev_timer *timer, float seconds) NONNULL(1);

/**
 * Get the remaining time until the timer times-out
 *
 * @param[in] timer  The timer started with @ref otbr_timeout_start.
 *
 * @return the remaining time until the timer times-out (seconds).
 */
float otbr_timeout_remaining(ev_timer *timer) NONNULL(1);

/**
 * Run the event loop once and check if the timer has timed-out
 *
 * @param[in]  timer      The timer started with @ref otbr_timeout_start.
 * @param[in]  block      If true, block until at least one event has been processed.
 *                        This includes the timer timeout event, so it will trigger eventually,
 *                        even if this means blocking until the timer times-out, if there are
 *                        no other watchers and events to process.
 *                        If false, return immediately if there are no events to process.
 *                        All already pending active events will be processed in both cases.
 * @param[out] remaining  If not `NULL`, the remaining time until the timer times-out (seconds)
 *                        is written to this variable (0 if the timer has already timed-out).
 *
 * @returns true if the timer is still active (has not timed-out yet), false if the timer has timed-out.
 */
bool otbr_timeout_tick(ev_timer *timer, bool block, float *remaining) NONNULL(1);

/**
 * Block (sleep) for the specified amount of time or until the timer times-out
 *
 * @param[in] timer    The timer started with @ref otbr_timeout_start.
 * @param[in] seconds  The amount of time to sleep, in seconds.
 *
 * @return true if this function blocked for the specified amount of time,
 *         false if the specified amount of time was longer than the remaining time until the timer times-out.
 *
 * @note This function does not run the event loop.
 */
bool otbr_timeout_sleep(ev_timer *timer, float seconds) NONNULL(1);

/**
 * Stop and cleanup the timer started with @ref otbr_timeout_start
 *
 * Nothing happens if the timer has already timed-out or was not started at all.
 *
 * @param[in] timer  The timer started with @ref otbr_timeout_start.
 *
 * @return true if the timer was still active (not timed-out yet), false if the timer has already timed-out.
 */
bool otbr_timeout_stop(ev_timer *timer) NONNULL(1);

#endif /* OTBR_TIMEOUT_H_INCLUDED */
