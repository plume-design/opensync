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

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <ev.h>

#include "ovsdb.h"
#include "monitor.h"
#include "log.h"
#include "os_backtrace.h"
#include "um.h"
#include "json_util.h"

#define MODULE_ID LOG_MODULE_ID_UPG

extern struct ev_io wovsdb;

bool um_init(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    int ch;
    bool dbgmode = false;

    /*
     * Get all input arguments
     * At the moment only legal is -d (run in debug mode)
     */
    while ((ch = getopt(argc, argv, "d")) != -1) {
        switch(ch) {
            case 'd':
                dbgmode = true;
                break;
            default:
                /* do nothing   */
                break;
        }
    }

    /*
     * LOG open
     */
    if (!log_open("UM", 0))
    {
        return false;
    }

    /* set application global log level */
    if (false == dbgmode)
    {
        log_severity_set(LOG_SEVERITY_INFO);
    }
    else
    {
        log_severity_set(LOG_SEVERITY_DEBUG);
    }

    /* Install crash handlers */
    backtrace_init();

    json_memdbg_init(NULL);

    return true;
}

/*
 * Main
 */
int main(int argc, char ** argv)
{
    LOG(NOTICE, "Starting Upgrade Manager - UM");

    if (true != um_init(argc, argv))
    {
        /* can't log this a log failed  */
        exit(-1);
    }

    if (false == um_ovsdb()) {
        LOG(ERR, "Error run upgrade ovsdb");
        goto exit;
    }

    /* Enable runtime severity updates */
    log_register_dynamic_severity(EV_DEFAULT);

    /* Loop */
    ev_run(EV_DEFAULT, 0);

exit:
    LOG(ERR, "Stopping Upgrade Manager - UM");

    ev_default_destroy();

    return 0;
}
