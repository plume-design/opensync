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

/* 3rd party */
#include <ev.h>

/* opensync */
#include <osw_module.h>

#define OW_SIGALRM_WDOG_SECONDS 30.0
#define OW_SIGALRM_WDOG_COUNT 3

static ev_async g_ow_sigalrm_async;
struct ev_loop *g_ow_sigalrm_loop;
static int g_ow_sigalrm_cnt;

static void
ow_sigalrm_sig_cb(int signum)
{
    if (signum != SIGALRM) return;

    assert(++g_ow_sigalrm_cnt < OW_SIGALRM_WDOG_COUNT);
    ev_async_send(g_ow_sigalrm_loop, &g_ow_sigalrm_async);
    alarm(OW_SIGALRM_WDOG_SECONDS);
}

static void
ow_sigalrm_async_cb(EV_P_ ev_async *arg, int events)
{
    alarm(0);
    g_ow_sigalrm_cnt = 0;
    alarm(OW_SIGALRM_WDOG_SECONDS);
}

static void
ow_sigalrm_init(EV_P)
{
    ev_async_init(&g_ow_sigalrm_async, ow_sigalrm_async_cb);
    ev_async_start(EV_A_ &g_ow_sigalrm_async);
    ev_unref(EV_A);
    g_ow_sigalrm_loop = EV_A;
    signal(SIGALRM, ow_sigalrm_sig_cb);
    alarm(OW_SIGALRM_WDOG_SECONDS);
}

OSW_MODULE(ow_sigalrm)
{
    struct ev_loop *loop = OSW_MODULE_LOAD(osw_ev);
    ow_sigalrm_init(loop);
    return NULL;
}
