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

#include <ev.h>          /* libev routines */
#include <getopt.h>      /* command line arguments */

#include "evsched.h"     /* ev helpers */
#include "log.h"         /* Logging routines */
#include "json_util.h"   /* json routines */
#include "os.h"          /* OS helpers */
#include "ovsdb.h"       /* ovsdb helpers */
#include "target.h"      /* target API */
#include "hello_world.h" /* our api */

/* Default log severlty */
static log_severity_t  hello_world_log_severity = LOG_SEVERITY_INFO;

/* Log entries from this file to contain MAIN */
#define MODULE_ID LOG_MODULE_ID_MAIN

/**
 * Main program.
 * The command line arguments allow bumping up the log severity
 */
int main(int argc, char ** argv)
{
    struct ev_loop *loop = EV_DEFAULT;

    /* Parse command-line arguments */
    if (os_get_opt(argc, argv, &hello_world_log_severity)) {
        return -1;
    }

    /* enable logging */
    target_log_open("HELLO_WORLD", 0); /* 0: log in syslog */
    LOGN("HELLO_WORLD");
    log_severity_set(hello_world_log_severity);

    /* Register to dynamic log severity updates */
    log_register_dynamic_severity(loop);

    /* Install crash handlers that dump the current stack in the log file */
    backtrace_init();

    /* Allow recurrent json memory usage reports in the log file */
    json_memdbg_init(loop);
    LOGI("%s: a new log entry", __func__);
    /* Initialize EV context */
    if (evsched_init(loop) == false) {
        LOGE("Initializing HELLO_WORLD "
             "(Failed to initialize EVSCHED)");
        return -1;
    }

    /* Initialize target structure */
    if (!target_init(TARGET_INIT_MGR_HELLO_WORLD, loop)) {
        return -1;
    }

    /* Initialize connection to ovsdb */
    if (!ovsdb_init_loop(loop, "HELLO_WORLD")) {
        LOGE("Initializing HELLO_WORLD "
             "(Failed to initialize OVSDB)");
        return -1;
    }

    /* Register to relevant ovsdb tables events */
    if (hello_world_ovsdb_init()) {
        LOGE("Initializing HELLO_WORLD "
             "(Failed to initialize HELLO_WORLD tables)");
        return -1;
    }

    /* Initialize data pipeline */
    if (dpp_init() == false) {
        LOGE("Error initializing dpp lib\n");
        return -1;
    }

    /* Start the event loop */
    ev_run(loop, 0);

    target_close(TARGET_INIT_MGR_HELLO_WORLD, loop);

    if (!ovsdb_stop_loop(loop)) {
        LOGE("Stopping HELLO_WORLD "
             "(Failed to stop OVSDB");
    }

    ev_loop_destroy(loop);

    LOGN("Exiting HELLO_WORLD");

    return 0;
}
