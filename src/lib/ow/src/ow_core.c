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
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/* 3rd party */
#include <ev.h>

/* opensync */
#include <target.h>
#include <module.h>
#include <osw_module.h>
#include <log.h>
#include <ds_tree.h>
#include <ds_list.h>
#include <os_backtrace.h>

/* osw */
#include <osw_ut.h>
#include <osw_thread.h>

/* onewifi */
#include "ow_conf.h"

static log_severity_t
ow_core_get_log_severity(void)
{
    const char *p = getenv("OW_CORE_LOG_SEVERITY");
    if (p != NULL) {
        if (strcasecmp(p, "notice") == 0) return LOG_SEVERITY_NOTICE;
        else if (strcasecmp(p, "info") == 0) return LOG_SEVERITY_INFO;
        else if (strcasecmp(p, "debug") == 0) return LOG_SEVERITY_DEBUG;
        else if (strcasecmp(p, "trace") == 0) return LOG_SEVERITY_TRACE;
        else return atoi(p);
    }
    return LOG_SEVERITY_INFO;
}

void
ow_core_init(EV_P)
{
    backtrace_init();
    ev_default_loop(EVFLAG_FORKCHECK);
    target_log_open("OW", 0);
    log_severity_set(ow_core_get_log_severity());
    osw_thread_init();
    module_init();
    assert(ev_run(EV_A_ EVRUN_ONCE | EVRUN_NOWAIT) == 0);
}

void
ow_core_run(EV_P)
{
    osw_module_load();

    /* FIXME: This is a little clunky. osw_ev is supposed to
     * be _the_ ev loop provider. However with the current
     * state of affairs it needs a bit more work to clean
     * this up.
    */
    assert(OSW_MODULE_LOAD(osw_ev) == EV_A);

    /* FIXME: This is necessary to avoid mainloop exiting if
     * there are no apparent action watchers. In some corner
     * cases (depending on the type of buses) this can yield
     * 0. To avoid exiting in such cases grab a reference so
     * the mainloop keeps running.
     */
    ev_ref(EV_A);
    ev_run(EV_A_ 0);
}
