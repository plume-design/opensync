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
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "jansson.h"
#include "json_util.h"
#include "log.h"
#include "monitor.h"
#include "os_backtrace.h"
#include "os_socket.h"
#include "ovsdb.h"
#include "schema.h"
#include "target.h"
#include "tpsm_mod.h"

#include "module.h"
#include "tpsm.h"

/*****************************************************************************/

#define TPSM_CFG_TBL_MAX 1
#define OVSDB_DEF_TABLE "Open_vSwitch"
#define TARGET_READY_POLL 10

#define GW_CFG_DIR "/mnt/data/config"
#define GW_CFG_FILE GW_CFG_DIR "/gw.cfg"
#define GW_CFG_FILE_MD5 GW_CFG_DIR "/gw.cfg.md5"

/*****************************************************************************/

extern struct ev_io wovsdb;

static log_severity_t tpsm_log_severity = LOG_SEVERITY_INFO;

int main(int argc, char **argv)
{
    struct ev_loop *loop = EV_DEFAULT;

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &tpsm_log_severity) != 0)
    {
        return -1;
    }
    target_log_open("TPSM", 0);
    LOGN("Starting thirdparty manager - TPSM");
    log_severity_set(tpsm_log_severity);

    // From this point on log severity can change in runtime.
    log_register_dynamic_severity(loop);

    backtrace_init();

    json_memdbg_init(loop);

    if (!target_init(TARGET_INIT_MGR_TPSM, loop))
    {
        LOGE("Target init failed");
        return -1;
    }

    if (!ovsdb_init_loop(loop, "TPSM"))
    {
        LOGE("OVSDB init failed");
        return -1;
    }

    tpsm_mod_init();
    tpsm_st_monitor();

    /* Start all modules */
    LOG(NOTICE, "Initializing modules...");
    module_init();

    ev_run(loop, 0);

    // Exit

    if (!ovsdb_stop_loop(loop))
    {
        LOGE("Stop failed");
    }
    module_fini();

    target_close(TARGET_INIT_MGR_TPSM, loop);

    ev_default_destroy();

    LOGN("Exiting TPSM");

    return 0;
}