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

/* libc */
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <stdbool.h>

/* opensync */
#include <log.h>
#include <manager_watchdog.h>

struct manager_watchdog_ctx
{
    ev_timer wdog_kick_timer;  /**< Timer to periodically kick the watchdog */
    bool is_initialized;       /**< Flag to prevent re-initialization */
    uint32_t wdog_timeout_sec; /**< Total timeout in seconds before the watchdog bites */
};
static struct manager_watchdog_ctx mgr_wdg_ctx = {0};

/**
 * @brief Resets the SIGALRM countdown using the provided timeout.
 */
static void manager_watchdog_defer(uint32_t timeout_sec)
{
    alarm(timeout_sec);
}

/**
 * @brief Signal handler for SIGALRM. Executes if the watchdog times out.
 */
static void manager_watchdog_sig_cb(int signum)
{
    if (signum != SIGALRM) return;

    LOGEM("main loop was not entered for too long, "
          "possible infinite loop or blocking call");
    assert(0);
}

/**
 * @brief libev timer callback that periodically kicks the watchdog.
 */
static void manager_watchdog_kick_cb(EV_P_ ev_timer *w, int revents)
{
    struct manager_watchdog_ctx *ctx = w->data;
    uint32_t timeout_sec = ctx->wdog_timeout_sec;
    manager_watchdog_defer(timeout_sec);
}

/**
 * @brief Public function to initialize and start the watchdog.
 */
void manager_watchdog_init(struct ev_loop *loop, uint32_t wdog_timeout_sec)
{
    if (wdog_timeout_sec == 0) return;

    /*
     * The function should not be called multiple times with different timeouts
     * in the same loop, however calling it with the same timeout is allowed and
     * has no effect on the initial watchdog loop.
     */
    if (mgr_wdg_ctx.is_initialized)
    {
        if (mgr_wdg_ctx.wdog_timeout_sec != wdog_timeout_sec) assert(0);

        LOGW("Manager watchdog is already initialized with timeout %u seconds", mgr_wdg_ctx.wdog_timeout_sec);
        return;
    }

    LOGD("Initializing manager watchdog with timeout %u seconds", wdog_timeout_sec);
    mgr_wdg_ctx.wdog_timeout_sec = wdog_timeout_sec;
    mgr_wdg_ctx.wdog_kick_timer.data = &mgr_wdg_ctx;
    const ev_tstamp wdog_kick_sec = wdog_timeout_sec / 2.0;
    ev_timer_init(&mgr_wdg_ctx.wdog_kick_timer, manager_watchdog_kick_cb, wdog_kick_sec, wdog_kick_sec);
    ev_timer_start(loop, &mgr_wdg_ctx.wdog_kick_timer);
    ev_unref(loop);
    signal(SIGALRM, manager_watchdog_sig_cb);
    manager_watchdog_defer(wdog_timeout_sec);

    mgr_wdg_ctx.is_initialized = true;
}
