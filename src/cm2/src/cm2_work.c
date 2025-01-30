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

#include <log.h>
#include <util.h>
#include <memutil.h>

#include "cm2_work.h"

typedef enum cm2_work_state cm2_work_state_t;

#define LOG_PREFIX(w, fmt, ...) "%s" fmt, ((w)->log_prefix ?: strfmta("%p: ", (w))), ##__VA_ARGS__

enum cm2_work_state
{
    CM2_WORK_IDLE,
    CM2_WORK_PENDING,
    CM2_WORK_COOLING_DOWN,
    CM2_WORK_COOLING_DOWN_AND_PENDING,
};

struct cm2_work
{
    char *log_prefix;
    struct ev_loop *loop;
    ev_idle idle;
    ev_timer timer;
    cm2_work_state_t state;
    cm2_work_fn_t *fn;
    void *priv;
    float deadline_sec;
    float cooldown_sec;
};

static void cm2_work_call(cm2_work_t *w)
{
    LOGT(LOG_PREFIX(w, "call"));
    ev_idle_stop(w->loop, &w->idle);
    ev_timer_stop(w->loop, &w->timer);
    ev_timer_set(&w->timer, w->cooldown_sec, 0);
    ev_timer_start(w->loop, &w->timer);
    w->state = CM2_WORK_COOLING_DOWN;
    if (w->fn != NULL) w->fn(w->priv);
}

static void cm2_work_settle(cm2_work_t *w)
{
    LOGT(LOG_PREFIX(w, "settle"));
    ev_idle_stop(w->loop, &w->idle);
    w->state = CM2_WORK_IDLE;
}

static void cm2_work_plan(cm2_work_t *w)
{
    LOGT(LOG_PREFIX(w, "plan"));
    ev_idle_start(w->loop, &w->idle);
    ev_timer_set(&w->timer, w->deadline_sec, 0);
    ev_timer_start(w->loop, &w->timer);
    w->state = CM2_WORK_PENDING;
}

static void cm2_work_plan_again(cm2_work_t *w)
{
    LOGT(LOG_PREFIX(w, "plan again"));
    w->state = CM2_WORK_COOLING_DOWN_AND_PENDING;
}

void cm2_work_schedule(cm2_work_t *w)
{
    if (w == NULL) return;
    switch (w->state)
    {
        case CM2_WORK_IDLE:
            cm2_work_plan(w);
            break;
        case CM2_WORK_PENDING:
            break;
        case CM2_WORK_COOLING_DOWN:
            cm2_work_plan_again(w);
            break;
        case CM2_WORK_COOLING_DOWN_AND_PENDING:
            break;
    }
}

void cm2_work_cancel(cm2_work_t *w)
{
    if (w == NULL) return;
    if (w->state == CM2_WORK_IDLE) return;
    LOGT(LOG_PREFIX(w, "cancel"));
    ev_timer_stop(w->loop, &w->timer);
    ev_idle_stop(w->loop, &w->idle);
    w->state = CM2_WORK_IDLE;
}

static void cm2_work_idle_cb(struct ev_loop *l, ev_idle *i, int mask)
{
    cm2_work_t *w = i->data;
    if (WARN_ON(w == NULL)) return;
    LOGT(LOG_PREFIX(w, "idle: enter"));
    switch (w->state)
    {
        case CM2_WORK_IDLE:
            ev_idle_stop(l, i);
            WARN_ON(1);
            break;
        case CM2_WORK_PENDING:
            cm2_work_call(w);
            break;
        case CM2_WORK_COOLING_DOWN:
            ev_idle_stop(l, i);
            WARN_ON(1);
            break;
        case CM2_WORK_COOLING_DOWN_AND_PENDING:
            ev_idle_stop(l, i);
            WARN_ON(1);
            break;
    }
}

static void cm2_work_timer_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_work_t *w = t->data;
    if (WARN_ON(w == NULL)) return;
    LOGT(LOG_PREFIX(w, "timer: enter"));
    switch (w->state)
    {
        case CM2_WORK_IDLE:
            WARN_ON(1);
            break;
        case CM2_WORK_PENDING:
            cm2_work_call(w);
            break;
        case CM2_WORK_COOLING_DOWN:
            WARN_ON(ev_is_active(&w->idle));
            cm2_work_settle(w);
            break;
        case CM2_WORK_COOLING_DOWN_AND_PENDING:
            cm2_work_settle(w);
            cm2_work_schedule(w);
            break;
    }
}

void cm2_work_drop(cm2_work_t *w)
{
    if (w == NULL) return;
    ev_timer_stop(w->loop, &w->timer);
    ev_idle_stop(w->loop, &w->idle);
    FREE(w->log_prefix);
    FREE(w);
}

cm2_work_t *cm2_work_alloc(void)
{
    cm2_work_t *w = CALLOC(1, sizeof(*w));
    ev_idle_init(&w->idle, cm2_work_idle_cb);
    ev_timer_init(&w->timer, cm2_work_timer_cb, 0, 0);
    w->idle.data = w;
    w->timer.data = w;
    w->loop = EV_DEFAULT;
    return w;
}

void cm2_work_set_fn(cm2_work_t *w, cm2_work_fn_t *fn, void *priv)
{
    if (w == NULL) return;
    LOGD(LOG_PREFIX(w, "fn: %p(%p) -> %p(%p)", w->fn, w->priv, fn, priv));
    w->fn = fn;
    w->priv = priv;
}

void cm2_work_set_deadline_sec(cm2_work_t *w, float t)
{
    if (w == NULL) return;
    LOGD(LOG_PREFIX(w, "deadline_sec: %f -> %f", w->deadline_sec, t));
    w->deadline_sec = t;
}

void cm2_work_set_cooldown_sec(cm2_work_t *w, float t)
{
    if (w == NULL) return;
    LOGD(LOG_PREFIX(w, "cooldown_sec: %f -> %f", w->cooldown_sec, t));
    w->cooldown_sec = t;
}

void cm2_work_set_log_prefix(cm2_work_t *w, const char *s)
{
    if (w == NULL) return;
    LOGD(LOG_PREFIX(w, "log_prefix: '%s'", s ?: ""));
    FREE(w->log_prefix);
    w->log_prefix = s ? STRDUP(s) : NULL;
}
