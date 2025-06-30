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

#include <stddef.h>
#include <stdlib.h>

#include "json_util.h"
#include "osp_pki.h"
#include "ovsdb.h"
#include "pkim_ovsdb.h"
#include "target.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

static log_severity_t pkim_log_severity = LOG_SEVERITY_INFO;

void pkim_clock_sync(void)
{
    struct tm valid_tm = {.tm_year = 2020 - 1900, .tm_mon = 0, .tm_mday = 1};
    time_t valid_time = mktime(&valid_tm);
    LOG(NOTICE, "Waiting for clock synchronization...");
    while (time(NULL) < valid_time)
    {
        sleep(5);
    }
    LOG(NOTICE, "Clock synchronized.");
}

int main(int argc, char **argv)
{
    struct ev_loop *loop = EV_DEFAULT;

    /* Enable logging */
    target_log_open("PKIM", 0);
    log_severity_set(pkim_log_severity);
    LOGN("Starting PKI Client manager - PKIM");

    /*
     * Process the "boot" mode before the os_get_opt() below.
     *
     * No need for a help since this should never be called by the user
     * directly.
     */
    if (argc == 2 && strcmp(argv[1], "boot") == 0)
    {
        if (!osp_pki_setup())
        {
            LOGE("PKIM bootstrap failed.");
        }
        else
        {
            LOGI("PKIM bootstrap success.");
        }
        exit(0);
    }

    /* Parse command-line arguments */
    if (os_get_opt(argc, argv, &pkim_log_severity))
    {
        return -1;
    }

    /* Enable runtime severity updates */
    log_register_dynamic_severity(loop);

    backtrace_init();

    json_memdbg_init(loop);

    pkim_clock_sync();

    if (!pkim_ovsdb_init())
    {
        LOG(EMERG, "pkim: Error initializing OVSDB.");
        return 0;
    }

    ev_run(loop, 0);

    if (!ovsdb_stop_loop(loop))
    {
        LOGE("Stopping PKIM (Failed to stop OVSDB)");
    }

    ev_default_destroy();

    LOGN("Exiting PKIM");

    return 0;
}
