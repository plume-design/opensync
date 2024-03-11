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

/**
 * FM - flash manager
 *
 * Responsible for archiving /var/log/messages file to flash memory archive
 * on CONFIG_INSTALL_PREFIX/bin/log_archive.
 *
 * Content of /var/log/messages is archived on every modify event (live copy).
 * At rotation by syslogd all files on log archive rotated.
 *
 * Two special cases for live copy:
 *  - At FM restart we need to sync files.
 *  - At system restart we need to archive old file and create fresh new copy.
 */
#include <errno.h>
#include <ev.h>
#include <getopt.h>
#include <jansson.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "ds_tree.h"
#include "evext.h"
#include "json_util.h"
#include "log.h"
#include "os.h"
#include "os_backtrace.h"
#include "os_socket.h"
#include "ovsdb.h"
#include "target.h"

#include "fm.h"

/*****************************************************************************/

/* Log entries from this file will contain "MAIN" */
#define MODULE_ID LOG_MODULE_ID_MAIN

/* Default log severity */
static log_severity_t log_severity = LOG_SEVERITY_INFO;

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/

static void fm_trigger_callback(FILE *fp)
{
    // TODO: dump some relevant data before log-pull happens
    fprintf(fp, "Flash Manager dummy data\n");
}

/******************************************************************************
 * Main
 *
 * Note: Command line arguments allow overriding the log severity
 *****************************************************************************/

int main(int argc, char **argv)
{
    struct ev_loop *loop = EV_DEFAULT;

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &log_severity))
    {
        return -1;
    }

    // Initialize logging library
    target_log_open("FM", 0);
    LOGN("Starting flash manager - FM");
    log_severity_set(log_severity);

    // Enable runtime severity updates
    log_register_dynamic_severity(loop);

    // Install crash handlers that dump the stack to the log file
    backtrace_init();

    // Allow recurrent json memory usage reports in the log file
    json_memdbg_init(loop);

    // Initialize target library
    if (!target_init(TARGET_INIT_MGR_FM, loop))
    {
        LOGE("Initializing FM "
             "(Failed to initialize target library)");
        return -1;
    }

    fm_event_init(loop);

    // Connect to OVSDB
    if (!ovsdb_init_loop(loop, "FM"))
    {
        LOGE("Initializing FM "
             "(Failed to initialize OVSDB)");
        return -1;
    }

    // Register for relevant OVSDB events
    if (fm_ovsdb_init())
    {
        LOGE("Initializing FM "
             "(Failed to initialize FM tables)");
        return -1;
    }

    if (fm_ovsdb_set_default_log_state())
    {
        LOGE("Initializing FM "
             "(OVSDB: Set default state failed)");
        return -1;
    }

    // Register the callback that will be executed before a log-pull
    log_register_dynamic_trigger(loop, fm_trigger_callback);

    ev_run(loop, 0);

    target_close(TARGET_INIT_MGR_FM, loop);

    ev_default_destroy();

    LOGN("Exiting FM");

    return 0;
}