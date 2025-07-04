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

#ifndef EVX_H_INCLUDED
#define EVX_H_INCLUDED

#include <ev.h>
#ifdef CONFIG_LIBEVX_USE_CARES
#include <ares.h>
#endif

/*
 * ===========================================================================
 *  Implementation of a debounce timer; single shot timer that can be started
 *  as many times as needed. Each time it is started, the countdown timer is
 *  reset.
 * ===========================================================================
 */
typedef struct ev_debounce ev_debounce;
typedef void ev_debounce_fn_t(struct ev_loop *loop, ev_debounce *w, int revent);

struct ev_debounce
{
    /* Making this an union gives the user access to the data field of the watcher */
    union
    {
        struct
        {
            EV_WATCHER_TIME(ev_debounce);
        };
        ev_timer timer;
    };
    ev_debounce_fn_t   *fn;                 /* Actual callback */
    ev_tstamp           timeout;            /* Debounce timeout */
    ev_tstamp           timeout_max;        /* Debounce timeout hard limit */
    ev_tstamp           ts_start;           /* Timestamp of first debounce request */
};

/**
 * Initialize the ev_debounce object
 *
 * param[in]    ev - ev_debounce object
 * param[in]    fn - the callback
 * param[in]    timeout - timeout after which the callback will be executed
 * param[in]    timeout_max - maximum amount of time, that a callback can be
 *              postponed. If set to 0.0, the callback can be postponed
 *              indefinitely.
 */
void ev_debounce_init2(ev_debounce *ev, ev_debounce_fn_t *fn, double timeout, double timeout_max);

/**
 * Simple wrapper for ev_debounce_init() -- mostly for backwards compatibility.
 */
void ev_debounce_init(ev_debounce *ev, ev_debounce_fn_t *fn, double timeout);

/**
 * Start or re-arm the debounce timer
 */
void ev_debounce_start(struct ev_loop *loop, ev_debounce *w);

/**
 * Stop the debounce timer
 */
void ev_debounce_stop(struct ev_loop *loop, ev_debounce *w);

/**
 * Reset debounce timer values (timeout and max timeout). If the timer is
 * active, the new settings will take effect only after it is re-armed
 */
void ev_debounce_set2(ev_debounce *w, double timeout, double timeout_max);

/**
 * Reset debounce timer value. If the timer is active, the new settings will
 * take effect only after it is re-armed
 */
void ev_debounce_set(ev_debounce *w, double timeout);

#ifdef CONFIG_LIBEVX_USE_CARES
#define EVX_ARES_SERVER_LEN 256
struct evx_ares {
    struct ares_ctx {
        ev_io           io;
        struct evx_ares *eares;
    } ctx[ARES_GETSOCK_MAXNUM];
    ev_timer        tw;
    struct ev_loop  *loop;
    struct {
        ares_channel        channel;
        int                 optmask;
        struct ares_options options;
    } ares;
    int chan_initialized;
    void (*timeout_user_cb)(void);
    char server[EVX_ARES_SERVER_LEN];
};
int evx_ares_get_count_busy_fds(struct evx_ares *eares);
int evx_init_ares(struct ev_loop * loop, struct evx_ares *eares_p, void (*timeout_user_cb)(void));
int evx_ares_set_server(struct evx_ares *eares_p, char *server);
int evx_start_ares(struct evx_ares *eares_p);
void evx_stop_ares(struct evx_ares *eares_p);
void evx_close_ares(struct evx_ares *eares_p);
#endif /* CONFIG_LIBEVX_USE_CARES */

#endif /* EVX_H_INCLUDED */
