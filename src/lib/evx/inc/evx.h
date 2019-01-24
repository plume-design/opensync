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
#ifdef BUILD_HAVE_LIBCARES
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
    ev_tstamp timeout;
};

static inline void ev_debounce_init(
        ev_debounce *ev,
        void (ev_debounce_fn)(struct ev_loop *loop, ev_debounce *w, int revent),
        double timeout)
{
    ev->timeout = timeout;
    ev_timer_init(&ev->timer, (void *)ev_debounce_fn, timeout, 0.0);
}

static inline void ev_debounce_start(struct ev_loop *loop, ev_debounce *w)
{
    if (ev_is_active(&w->timer))
    {
        ev_timer_stop(loop, &w->timer);
    }

    /* Re-arm the timer */
    ev_timer_set(&w->timer, w->timeout, 0.0);
    ev_timer_start(loop, &w->timer);
}

static inline void ev_debounce_stop(struct ev_loop *loop, ev_debounce *w)
{
    ev_timer_stop(loop, &w->timer);
}

#ifdef BUILD_HAVE_LIBCARES
typedef struct {
    ev_io    io;
    ev_timer tw;
    struct ev_loop * loop;
    struct {
        ares_channel channel;
        struct ares_options options;
    } ares;
    int chan_initialized;
} evx_ares;

int evx_init_ares(struct ev_loop * loop, evx_ares *eares_p);
int evx_init_default_chan_options(evx_ares *eares_p);
void evx_stop_ares(evx_ares *eares_p);
#endif /* BUILD_HAVE_LIBCARES */

#endif /* EVX_H_INCLUDED */
