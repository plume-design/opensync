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
#include <stdio.h>
#include <string.h>

#include "const.h"
#include "json_util.h"
#include "log.h"
#include "module.h"
#include "os.h"
#include "os_backtrace.h"
#include "target.h"

#include "qosm_internal.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

static log_severity_t qosm_log_severity = LOG_SEVERITY_INFO;

int main(int argc, char *argv[])
{
    // Parse command-line arguments
    if (os_get_opt(argc, argv, &qosm_log_severity))
    {
        return 1;
    }

    // Initialize logging
    target_log_open("QOSM", 0);
    LOG(NOTICE, "Starting QoS Manager - QOSM");
    log_severity_set(qosm_log_severity);
    log_register_dynamic_severity(EV_DEFAULT);

    backtrace_init();

    json_memdbg_init(EV_DEFAULT);

    // Connect to OVSDB
    if (!ovsdb_init_loop(EV_DEFAULT, "QOSM"))
    {
        LOG(EMERG, "Initializing of QoSM (Failed to initialize OVSDB)");
        return 1;
    }

    // Initialize modules
    module_init();

    // Initialize OVSDB tables
    qosm_ip_interface_init();
    qosm_interface_qos_init();
    qosm_interface_queue_init();

    ev_run(EV_DEFAULT, 0);

    if (!ovsdb_stop_loop(EV_DEFAULT))
    {
        LOG(ERR, "Stopping QoSM (Failed to stop OVSDB");
    }

    module_fini();

    ev_default_destroy();

    LOG(NOTICE, "Exiting QoSM");

    return 0;
}
