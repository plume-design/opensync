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
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "const.h"
#include "json_util.h"
#include "log.h"
#include "module.h"
#include "os.h"
#include "os_backtrace.h"
#include "os_random.h"
#include "ovsdb.h"
#include "target.h"
#include "memutil.h"

#include "wano.h"
#include "wano_wan.h"
#include "wano_internal.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

/*
 * Structure for defining built-in interfaces
 */
struct wano_builtin
{
    wano_ppline_t           wb_ppline;          /* WAN plug-in pipeline */
    wano_ppline_event_t     wb_ppline_event;    /* Pipeline event object */
    wano_wan_t              wb_wan;             /* WAN configuration */
};


static log_severity_t wano_log_severity = LOG_SEVERITY_INFO;

/*
 * WANO plug-in pipelines associated with built-in interfaces
 */
static int wano_builtin_len = 0;
static struct wano_builtin *wano_builtin_list = NULL;

static wano_ppline_event_fn_t wano_builtin_ppline_event_fn;

/*
 * Start built-in WAN interfaces
 */
void wano_start_builtin_ifaces(void)
{
    char *if_type;
    char *if_name;
    char *iflist;
    char *pif;
    int ii;

    /* Scan the interface string and calculate the interface list length */
    iflist = (char[]){ CONFIG_MANAGER_WANO_IFACE_LIST };
    wano_builtin_len = 0;
    while ((pif = strsep(&iflist, " ")) != NULL)
    {
        if (*pif == '\0') continue;
        wano_builtin_len++;
    }
    wano_builtin_list = CALLOC(wano_builtin_len, sizeof(wano_builtin_list[0]));

    /* Scan the interface string again and populate the built-in interface list */
    iflist = (char[]){ CONFIG_MANAGER_WANO_IFACE_LIST };
    ii = 0;
    while ((pif = strsep(&iflist, " ")) != NULL)
    {
        if_name = strsep(&pif, ":");
        if_type = strsep(&pif, ":");

        if (if_name == NULL || if_name[0] == '\0') continue;
        /* The default interface type is eth */
        if (if_type == NULL || if_type[0] == '\0') if_type = "eth";

        /* Add plug-in pipeline to interface  */
        if (!wano_ppline_init(&wano_builtin_list[ii].wb_ppline, if_name, if_type, 0))
        {
            LOG(ERR, "wano: %s: Error starting plug-in interface on built-in interface.", if_name);
            continue;
        }

        /* Initialize the WAN object and associated it with the ppline */
        wano_wan_init(&wano_builtin_list[ii].wb_wan);
        wano_ppline_wan_set(
                &wano_builtin_list[ii].wb_ppline,
                &wano_builtin_list[ii].wb_wan);

        /* Start the ppline event watcher */
        wano_ppline_event_init(&wano_builtin_list[ii].wb_ppline_event, wano_builtin_ppline_event_fn);
        wano_ppline_event_start(
                &wano_builtin_list[ii].wb_ppline_event,
                &wano_builtin_list[ii].wb_ppline);

        LOG(NOTICE, "wano: %s: Started plug-in pipeline on built-in interface.", if_name);
        ii++;
    }

    /*
     * ii != wano_builtin_len if, for some reason, some pipelines failed to
     * initialize where ii contains the number of successfully initialized
     * pipelines
     */
    wano_builtin_len = ii;
}

void wano_builtin_ppline_event_fn(wano_ppline_event_t *event, enum wano_ppline_status status)
{
    struct wano_builtin *wb = CONTAINER_OF(event, struct wano_builtin, wb_ppline_event);

    switch (status)
    {
        case WANO_PPLINE_RESTART:
            LOG(INFO, "wano: Resetting WAN configuration.");
            wano_wan_reset(&wb->wb_wan);
            break;

        case WANO_PPLINE_ABORT:
            wano_wan_rollover(&wb->wb_wan);
            break;

        case WANO_PPLINE_IDLE:
            LOG(INFO, "wano: Moving to next WAN configuration.");
            wano_wan_next(&wb->wb_wan);
            break;

        default:
            break;
    }
}

void wano_stop_builtin_ifaces(void)
{
    int ii;

    for (ii = 0; ii < wano_builtin_len; ii++)
    {
        wano_ppline_event_stop(&wano_builtin_list[ii].wb_ppline_event);
        wano_ppline_fini(&wano_builtin_list[ii].wb_ppline);
        wano_wan_fini(&wano_builtin_list[ii].wb_wan);
    }
}

void wano_sig_handler(int signum)
{
    (void)signum;
    switch (signum)
    {
        case SIGHUP:
            wano_ppline_restart_all();
            break;

        default:
            LOG(ERR, "wano: Received unhandled signal: %d", signum);
            break;
    }
}

int main(int argc, char *argv[])
{
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

    // Initialize the AWLAN_Node table watcher
    if (!wano_awlan_node_init())
    {
        LOG(EMERG, "Error initializing AWLAN_Node table monitor.");
        return 1;
    }

    // Initialize WAN configuration
    if (!wano_wan_ovsdb_init())
    {
        LOG(EMERG, "Error initializing WAN_Config table monitor.");
        return 1;
    }

    // Initialize WAN probe
    if (!wano_dns_probe_init())
    {
        LOG(EMERG, "Error initializing WAN probe.");
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

    // Install signal handler
    struct sigaction sa;
    sa.sa_handler = wano_sig_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGHUP, &sa, NULL) != 0)
    {
        LOG(WARN, "wano: Error installing SIGHUP handler.");
    }

    ev_run(EV_DEFAULT, 0);

    wano_stop_builtin_ifaces();

    if (!ovsdb_stop_loop(EV_DEFAULT))
    {
        LOG(ERR, "Stopping WANO (Failed to stop OVSDB");
    }

    wano_dns_probe_fini();

    module_fini();

    ev_default_destroy();

    LOG(NOTICE, "Exiting WANO");

    return 0;
}
