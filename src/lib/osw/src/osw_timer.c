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
#include <log.h>
#include <osa_assert.h>
#include <ds_dlist.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_module.h>

static struct ds_dlist g_active_list = DS_DLIST_INIT(struct osw_timer, node);

void
osw_timer_core_dispatch(uint64_t now_nsec)
{
    struct ds_dlist pending_list = DS_DLIST_INIT(struct osw_timer, node);

    /* Prepare */
    while (ds_dlist_is_empty(&g_active_list) == false) {
        struct osw_timer *timer = (struct osw_timer*) ds_dlist_head(&g_active_list);

        if (timer->at_nsec > now_nsec)
            break;

        WARN_ON(timer->list != &g_active_list);
        ds_dlist_remove(&g_active_list, timer);
        timer->list = NULL;

        ds_dlist_insert_tail(&pending_list, timer);
        timer->list = &pending_list;
    }

    /* Dispatch */
    while (ds_dlist_is_empty(&pending_list) == false) {
        struct osw_timer *timer = (struct osw_timer*) ds_dlist_head(&pending_list);

        WARN_ON(timer->list != &pending_list);
        ds_dlist_remove(&pending_list, timer);
        timer->list = NULL;

        ASSERT(timer->cb != NULL, "");
        timer->cb(timer);
    }
}

bool
osw_timer_core_get_next_at(uint64_t *next_at_nsec)
{
    ASSERT(next_at_nsec != NULL, "");

    struct osw_timer *timer;

    if (ds_dlist_is_empty(&g_active_list) == true)
        return false;

    timer = (struct osw_timer*) ds_dlist_head(&g_active_list);
    *next_at_nsec = timer->at_nsec;
    return true;
}

void
osw_timer_init(struct osw_timer *timer,
               osw_timer_fn *cb)
{
    ASSERT(timer != NULL, "");
    ASSERT(cb != NULL, "");

    memset(timer, 0, sizeof(*timer));
    timer->cb = cb;
}

void
osw_timer_arm_at_nsec(struct osw_timer *timer,
                      uint64_t nsec)
{
    ASSERT(timer != NULL, "");
    ASSERT(timer->cb != NULL, "");

    struct osw_timer *next_timer;

    timer->at_nsec = nsec;
    if (timer->list) {
        ds_dlist_remove(timer->list, timer);
        timer->list = NULL;
    }

    ds_dlist_foreach(&g_active_list, next_timer)
        if (next_timer->at_nsec >= timer->at_nsec)
            break;

    if (next_timer != NULL)
        ds_dlist_insert_before(&g_active_list, next_timer, timer);
    else
        ds_dlist_insert_tail(&g_active_list, timer);

    timer->list = &g_active_list;
}

void
osw_timer_disarm(struct osw_timer *timer)
{
    ASSERT(timer != NULL, "");

    if (timer->list == NULL)
        return;

    ds_dlist_remove(timer->list, timer);
    timer->list = NULL;
}

bool
osw_timer_is_armed(const struct osw_timer *timer)
{
    ASSERT(timer != NULL, "");
    return timer->list != NULL;
}

uint64_t
osw_timer_get_remaining_nsec(const struct osw_timer *timer,
                             uint64_t now_nsec)
{
    ASSERT(timer != NULL, "");
    if (osw_timer_is_armed(timer) == true) {
        if (timer->at_nsec >= now_nsec) {
            return timer->at_nsec - now_nsec;
        }
        else {
            return 0;
        }
    }
    else {
        return UINT64_MAX;
    }
}

OSW_MODULE(osw_timer)
{
    return NULL;
}

#include "osw_timer_ut.c"
