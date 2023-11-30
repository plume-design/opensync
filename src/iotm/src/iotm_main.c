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

#include "iotm.h"
#include "log.h"
#include "os.h"
#include "ovsdb.h"
#include "os_backtrace.h"
#include "target.h"

/******************************************************************************/

#define MODULE_ID LOG_MODULE_ID_MAIN

/******************************************************************************/

static log_severity_t iotm_log_severity = LOG_SEVERITY_INFO;

/******************************************************************************
 *  PUBLIC API definitions
 *****************************************************************************/

int main(int argc, char ** argv)
{
    struct ev_loop *loop = EV_DEFAULT;

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &iotm_log_severity) != 0)
    {
        return -1;
    }
    target_log_open("IoTM", 0);
    LOGN("Starting IoT Manager");
    log_severity_set(iotm_log_severity);

    // From this point on log severity can change in runtime.
    log_register_dynamic_severity(loop);

    backtrace_init();

    if (!target_init(TARGET_INIT_MGR_IOTM, loop))
    {
        LOGE("Target init failed");
        return -1;
    }

    if (!ovsdb_init_loop(loop, "IoTM"))
    {
        LOGE("OVSDB init failed");
        return -1;
    }

#ifdef CONFIG_IOTM_ENABLE_THREAD
    if (!iotm_thread_otbr_init(loop))
    {
        LOGE("IoTM OSP OTBR init failed");
        return -1;
    }
#endif /* CONFIG_IOTM_ENABLE_THREAD */

    ev_run(loop, 0);

    // Exit
#ifdef CONFIG_IOTM_ENABLE_THREAD
    iotm_thread_otbr_close();
#endif /* CONFIG_IOTM_ENABLE_THREAD */

    if (!ovsdb_stop_loop(loop))
    {
        LOGE("Stop failed");
    }

    target_close(TARGET_INIT_MGR_IOTM, loop);

    ev_default_destroy();

    LOGN("Exiting IoTM");

    return 0;
}
