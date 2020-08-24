/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <ev.h>          /* libev routines */
#include <getopt.h>      /* command line arguments */

#include "ds_tree.h"
#include "evsched.h"     /* ev helpers */
#include "log.h"         /* Logging routines */
#include "json_util.h"   /* json routines */
#include "os.h"          /* OS helpers */
#include "ovsdb.h"       /* ovsdb helpers */
#include "target.h"      /* target API */
#include "iotm.h" /* our api */
#include "iotm_ev.h"
#include "iotm_ovsdb.h"


/* Log entries from this file to contain MAIN */
static log_severity_t  log_severity = LOG_SEVERITY_INFO;
static struct ev_loop *Loop = NULL; // global loop
#define MODULE_ID LOG_MODULE_ID_MAIN

static void iotm_exit(struct ev_loop *loop)
{

    // Close things down
    target_close(TARGET_INIT_MGR_IOTM, loop);

    if (!ovsdb_stop_loop(loop))
    {
        LOGE("Stopping IOTM (Failed to stop OVSDB)");
    }

    iotm_teardown_mgr();
    ev_loop_destroy(loop);
}

// watcher to catch SIGINT. Call cleanup routines
ev_signal signal_watcher;
static void sigint_cb (EV_P_ ev_signal *w, int revents)
{
    if (Loop == NULL)
    {
        LOGE("%s: Loop not initialized. Exiting.",
                __func__);
        exit(-1);
    }
    iotm_exit(Loop);
    LOGI("%s: Exiting IOTM.\n", __func__);
    exit(0);
}

/**
 * Main program.
 */
int main(int argc, char ** argv)
{
    printf("IN UPDATED FILE\n");
    struct ev_loop *loop = EV_DEFAULT;

    // Initialize logging library
    target_log_open("IOTM", 0);  // 0 = syslog and TTY (if present)
    LOGN("IOTM");
    log_severity_set(log_severity);

    // Enable runtime severity updates
    log_register_dynamic_severity(loop);

    // Install crash handlers that dump the stack to the log file
    backtrace_init();

    // Initialize EV context
    if (evsched_init(loop) == false)
    {
        LOGE("Initializing IOTM "
                "(Failed to initialize EVSCHED)");
        return -1;
    }

    // Initialize target structure
    if (!target_init(TARGET_INIT_MGR_IOTM, loop))
    {
        LOGE("Initializing IOTM "
                "(Failed to initialize target library)");
        return -1;
    }

    iotm_init_mgr(loop);

    // Connect to OVSDB
    if (!ovsdb_init_loop(loop, "IOTM"))
    {
        LOGE("Initializing IOTM (Failed to initialize OVSDB)");
        return -1;
    }

    // Register to relevant OVSDB tables events
    if (iotm_ovsdb_init())
    {
        LOGE("Initializing IOTM (Failed to initialize IOTM tables)");
        return -1;
    }

    Loop = loop; // bind for sigint
    ev_signal_init(&signal_watcher, sigint_cb, SIGINT);
    ev_signal_start(loop, &signal_watcher);


    iotm_ev_init();

    // Start the event loop
    LOGI("%s: All layers initialized, blocking loop.",
            __func__);
    ev_run(loop, 0);

    iotm_exit(loop);
    LOGN("Exiting IOTM");

    return 0;
}

