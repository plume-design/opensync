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

#ifndef FSM_FN_TRACE_H_INCLUDED
#define FSM_FN_TRACE_H_INCLUDED

#include "os_ev_trace.h"

#ifdef CONFIG_OS_EV_TRACE
enum
{
    FSM_FN_ENTER = OS_EV_ENTER,
    FSM_FN_EXIT = OS_EV_EXIT,
};

void fsm_fn_tracer_init(void);

void fsm_fn_periodic(struct fsm_session *session);

void fsm_fn_trace(void *p, int trace);

void fsm_fn_map(void *p, const char *fname);

#else

enum
{
    FSM_FN_ENTER = 0,
    FSM_FN_EXIT = 1,
};

static inline void fsm_fn_tracer_init(void)
{
}

static inline void fsm_fn_periodic(struct fsm_session *session)
{
}

static inline void fsm_fn_trace(void *p, int trace)
{
}

static inline void fsm_fn_map(void *p, const char *fname)
{
}

#endif /* CONFIG_OS_EV_TRACE */

#define FSM_FN_MAP(f) \
    do \
    { \
        fsm_fn_map(f, #f); \
    } while (0)

#endif /* FSM_FN_TRACE_H_INCLUDED */
