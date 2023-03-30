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

#ifndef OSW_TIMER_H
#define OSW_TIMER_H

struct osw_timer_core;
struct osw_timer;

typedef void
osw_timer_fn(struct osw_timer *timer);

struct osw_timer {
    osw_timer_fn *cb;
    uint64_t at_nsec;

    struct ds_dlist *list;
    struct ds_dlist_node node;
};

void
osw_timer_core_dispatch(uint64_t now_nsec);

bool
osw_timer_core_get_next_at(uint64_t *next_at_nsec);

void
osw_timer_init(struct osw_timer *timer,
               osw_timer_fn *cb);

void
osw_timer_arm_at_nsec(struct osw_timer *timer,
                      uint64_t nsec);

void
osw_timer_disarm(struct osw_timer *timer);

bool
osw_timer_is_armed(const struct osw_timer *timer);

uint64_t
osw_timer_get_remaining_nsec(const struct osw_timer *timer,
                             uint64_t now_nsec);

#endif /* OSW_TIMER_H */
