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

#include <unistd.h>
#include <ev.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#define MODULE_ID LOG_MODULE_ID_MAIN

#include "json_util.h"
#include "log.h"
#include "ovsdb.h"
#include "target.h"

#include "psm.h"

static log_severity_t psm_log_severity = LOG_SEVERITY_INFO;

static bool g_psm_opt_restore = false;   /* --restore option */

void psm_usage(void)
{
    fprintf(stderr, "Available arguments:\n\n");

    fprintf(stderr, "   [-v]            set verbose level\n");
    fprintf(stderr, "   -h --help       Help\n");
    fprintf(stderr, "   -r --restore    Restore data from persistent storage, apply it to OVSDB and exit\n");
}

bool psm_parse_opts(int argc, char *argv[])
{
    int optidx;
    char opt;

    struct option lopts[] =
    {
        {
            "help", no_argument, 0, 'h',
        },
        {
            "restore", no_argument, 0, 'r',
        },
        {
            NULL, 0, 0, 0,
        }
    };

    opt = getopt_long(argc, argv, "hl", lopts, &optidx);
    switch (opt)
    {
        case 'h':
            psm_usage();
            return false;

        case 'r':
            g_psm_opt_restore = true;
            break;

        case 'v':
            /* Ignore, this is a passthrough to os_opt_get() */
            break;
    }

    return true;
}

int main(int argc, char *argv[])
{
    int retval = 0;

    if (!psm_parse_opts(argc, argv))
    {
        return 1;
    }

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &psm_log_severity))
    {
        return 1;
    }

    // enable logging
    target_log_open("PSM", 0);
    LOG(NOTICE, "Starting Persistent Storage Manager - PSM");
    log_severity_set(psm_log_severity);
    log_register_dynamic_severity(EV_DEFAULT);

    backtrace_init();

    json_memdbg_init(EV_DEFAULT);

    // Connect to ovsdb
    if (!ovsdb_init_loop(EV_DEFAULT, "PSM"))
    {
        LOG(EMERG, "Initializing PSM (Failed to initialize OVSDB)");
        return 1;
    }

    if (!psm_ovsdb_schema_init(!g_psm_opt_restore))
    {
        LOG(EMERG, "Error initializing PSM (Schema error).");
        return 1;
    }

    psm_erase_config_init();

    psm_ovsdb_row_init();

    if (g_psm_opt_restore)
    {
        /* --restore: Restore persistent storage data to OVSDB */
        if (!psm_ovsdb_row_restore())
        {
            LOG(ERR, "Error replicating persistent data to OVSDB.");
            retval = 1;
        }
    }
    else
    {
        /* Default -- run the main loop */
        ev_run(EV_DEFAULT, 0);
    }

    if (!ovsdb_stop_loop(EV_DEFAULT))
    {
        LOG(ERR, "Stopping PSM (Failed to stop OVSDB");
    }

    ev_default_destroy();

    LOG(NOTICE, "Exiting PSM");

    return retval;
}
