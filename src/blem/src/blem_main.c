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

#include <ev.h>

#include "blem.h"
#include "log.h"
#include "os.h"
#include "ovsdb.h"
#include "os_backtrace.h"
#include "json_util.h"
#include "target.h"

/******************************************************************************/

#define MODULE_ID LOG_MODULE_ID_MAIN

/******************************************************************************/

static log_severity_t blem_log_severity = LOG_SEVERITY_INFO;

/******************************************************************************
 *  PUBLIC API definitions
 *****************************************************************************/

int main(int argc, char ** argv)
{
    struct ev_loop *loop = EV_DEFAULT;

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &blem_log_severity) != 0)
    {
        return -1;
    }
    target_log_open("BLEM", 0);
    LOGN("Starting BLEM (BLE manager)");
    log_severity_set(blem_log_severity);

    // From this point on log severity can change in runtime.
    log_register_dynamic_severity(loop);

    backtrace_init();

    json_memdbg_init(loop);

    if (!target_init(TARGET_INIT_MGR_BLEM, loop))
    {
        LOGE("Initializing BLEM (target_init(TARGET_INIT_MGR_BLEM) failed)");
        return -1;
    }

    if (!blem_ble_init(loop))
    {
        return -1;
    }

    if (!ovsdb_init_loop(loop, "BLEM"))
    {
        LOGE("Initializing BLEM (Failed to initialize OVSDB)");
        return -1;
    }

    blem_ovsdb_onboarding_init();
    blem_ovsdb_proximity_init();

    ev_run(loop, 0);

    // Exit
    blem_ovsdb_proximity_fini();
    blem_ovsdb_onboarding_fini();

    blem_ble_close();
    target_close(TARGET_INIT_MGR_BLEM, loop);

    if (!ovsdb_stop_loop(loop))
    {
        LOGE("Stopping BLEM (Failed to stop OVSDB");
    }

    ev_default_destroy();

    LOGN("Exiting BLEM");

    return 0;
}
