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

#ifndef OS_EV_TRACE_H_INCLUDED
#define OS_EV_TRACE_H_INCLUDED

typedef void (*os_ev_trace_func_t)(void *p, int tracer);
typedef void (*os_ev_trace_register_func_t)(void *p, const char *fname);

struct os_ev_trace_setting
{
    os_ev_trace_func_t tracer;
    os_ev_trace_register_func_t mapper;
};

#ifdef CONFIG_OS_EV_TRACE

#include <ev_trace.h>

enum
{
    OS_EV_ENTER = EV_ENTER,
    OS_EV_EXIT = EV_EXIT,
};

int os_ev_trace_init(struct os_ev_trace_setting *set);
void os_ev_trace_map(void *p, const char *fname);

#else

enum
{
    OS_EV_ENTER = 0,
    OS_EV_EXIT = 1,
};

static inline int os_ev_trace_init(struct os_ev_trace_setting *set)
{
    return 0;
}

static inline void os_ev_trace_map(void *p, const char *fname)
{
    return;
}
#endif /* CONFIG_OS_EV_TRACE */

#define OS_EV_TRACE_MAP(f) \
    do \
    { \
        os_ev_trace_map(f, #f); \
    } while (0)

#endif /* OS_EV_TRACE_H_INCLUDED */
