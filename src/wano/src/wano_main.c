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

#include "const.h"
#include "json_util.h"
#include "log.h"
#include "module.h"
#include "os.h"
#include "os_backtrace.h"
#include "ovsdb.h"
#include "target.h"

#include "wano.h"
#include "wano_internal.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

static log_severity_t wano_log_severity = LOG_SEVERITY_INFO;

/*
 * WANO plug-in pipelines associated with built-in interfaces
 */
static int wano_builtin_pplines_len = 0;
static wano_ppline_t *wano_builtin_pplines = NULL;

/*
 * Start built-in WAN interfaces
 */
void wano_start_builtin_ifaces(void)
{
    char iflist[] = CONFIG_MANAGER_WANO_IFACE_LIST;
    char *pif;
    char *psave;
    int ii;

    /* Split interface string and calculate its length */
    wano_builtin_pplines_len = 0;
    for (pif = strtok_r(iflist, " ", &psave);
            pif != NULL;
            pif = strtok_r(NULL, " ", &psave))
    {
        wano_builtin_pplines_len++;
    }

    wano_builtin_pplines = calloc(wano_builtin_pplines_len, sizeof(wano_builtin_pplines[0]));

    pif = iflist;
    for (ii = 0; ii < wano_builtin_pplines_len; ii++)
    {
        /* Add plug-in pipeline to interface  */
        if (!wano_ppline_init(&wano_builtin_pplines[ii], pif, "eth", 0))
        {
            LOG(ERR, "wano: %s: Error starting plug-in interface on built-in interface.", pif);
        }
        else
        {
            LOG(NOTICE, "wano: %s: Started plug-in pipeline on built-in interface.", pif);
        }

        /* Move to next interface */
        pif += strlen(pif) + 1;
    }
}

void wano_stop_builtin_ifaces(void)
{
    int ii;

    for (ii = 0; ii < wano_builtin_pplines_len; ii++)
    {
        wano_ppline_fini(&wano_builtin_pplines[ii]);
    }
}

int main(int argc, char *argv[])
{
    srand(getpid());

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &wano_log_severity))
    {
        return 1;
    }

    // enable logging
    target_log_open("WANO", 0);
    LOG(NOTICE, "Starting WAN Orchestrator - WANO");
    log_severity_set(wano_log_severity);
    log_register_dynamic_severity(EV_DEFAULT);

    backtrace_init();

    json_memdbg_init(EV_DEFAULT);

    // Connect to ovsdb
    if (!ovsdb_init_loop(EV_DEFAULT, "WANO"))
    {
        LOG(EMERG, "Initializing WANO (Failed to initialize OVSDB)");
        return 1;
    }

    // Initialize the Wifi_Inet_State/Wifi_Master_State watchers
    if (!wano_inet_state_init())
    {
        LOG(EMERG, "Error initializing Inet_State monitor.");
        return 1;
    }

    // Initialize the Connection_Manager_Uplink watcher
    if (!wano_connmgr_uplink_init())
    {
        LOG(EMERG, "Error initializing Connection_Manager_Uplink monitor.");
        return 1;
    }

    // Initialize the Port table watcher
    if (!wano_ovs_port_init())
    {
        LOG(EMERG, "Error initializing Port table monitor.");
        return 1;
    }

    // Initialize WAN plug-ins
    module_init();

    // Delete all Connection_Manager_Uplink rows
    if (!wano_connmgr_uplink_flush())
    {
        LOG(WARN, "wano: Error clearing the Connection_Manager_Uplink table.");
    }

    // Scan built-in list of WAN interfaces and start each one
    wano_start_builtin_ifaces();

    ev_run(EV_DEFAULT, 0);

    wano_stop_builtin_ifaces();

    if (!ovsdb_stop_loop(EV_DEFAULT))
    {
        LOG(ERR, "Stopping WANO (Failed to stop OVSDB");
    }

    module_fini();

    ev_default_destroy();

    LOG(NOTICE, "Exiting WANO");

    return 0;
}
