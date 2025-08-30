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
#include <syslog.h>
#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>

#include "ds_tree.h"
#include "log.h"
#include "os.h"
#include "os_socket.h"
#include "ovsdb.h"
#include "evext.h"
#include "os_backtrace.h"
#include "json_util.h"
#include "target.h"
#include "fsm.h"
#include "fsm_oms.h"
#include "nf_utils.h"
#include "neigh_table.h"
#include "kconfig.h"
#include "qm_conn.h"
#include "fsm_fn_trace.h"
#include "mem_monitor.h"
#include "fsm_internal.h"

/******************************************************************************/

#define MODULE_ID LOG_MODULE_ID_MAIN

/******************************************************************************/


/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/

static log_severity_t  fsm_log_severity = LOG_SEVERITY_INFO;


/******************************************************************************
 *  PUBLIC API definitions
 *****************************************************************************/


int main(int argc, char ** argv)
{
    struct ev_loop *loop = EV_DEFAULT;

    /* Populate IPv4 and IPv6 Neighbour only */
    uint32_t neigh_table_events = IPV4_NEIGHBORS | IPV6_NEIGHBORS;

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &fsm_log_severity)) {
        return -1;
    }

    fsm_fn_tracer_init();

    // enable logging
    target_log_open("FSM", 0);
    LOGN("Starting FSM (Flow Service Manager)");
    log_severity_set(fsm_log_severity);

    /* Register to dynamic severity updates */
    log_register_dynamic_severity(loop);

    fsm_init_mgr(loop);
    fsm_init_mem_monitor();

    backtrace_init();

    json_memdbg_init(loop);

    if (!target_init(TARGET_INIT_MGR_FSM, loop)) {
        return -1;
    }

    if (!ovsdb_init_loop_with_priority(loop, "FSM", -2)) {
        LOGE("Initializing FSM "
             "(Failed to initialize OVSDB)");
        return -1;
    }

    fsm_event_init();

    // WAR: Init oms before fsm ovsdb tables
    // OMS needs to be initialized before any plugins are loaded.
    // This way first callbacks are missed by plugins. This avoids
    // problem when fsm crashes and after recovery plugins will
    // get back multiple versions without guaranteed order. This
    // might confuse plugin since they expect that last update is
    // the one that needs to be used.
    fsm_oms_init();

    if (fsm_ovsdb_init()) {
        LOGE("Initializing FSM "
             "(Failed to initialize FSM tables)");
        return -1;
    }

    if (dpp_init() == false) {
        LOGE("Error initializing dpp lib\n");
        return -1;
    }

    if (qm_conn_init() == false) {
        LOGE("Initializing qm_conn");
        return -1;
    }

    if (neigh_table_init())
    {
        LOGE("Initializing Neighbour Table failed " );
        return -1;
    }

    if ((!kconfig_enabled(CONFIG_FSM_TAP_INTF)) &&
        (!kconfig_enabled(CONFIG_FSM_CONNTRACK)))
    {
        neigh_table_events |= DHCP_LEASED_IP;
    }

    neigh_table_init_monitor(loop, false, neigh_table_events);

    if (nf_ct_init(loop, NULL) < 0)
    {
        LOGE("Error initializing conntrack\n");
        return -1;
    }


    ev_run(loop, 0);

    target_close(TARGET_INIT_MGR_FSM, loop);

    neigh_table_cleanup();

    fsm_oms_exit();

    if (!ovsdb_stop_loop(loop)) {
        LOGE("Stopping FSM "
             "(Failed to stop OVSDB");
    }

    ev_loop_destroy(loop);

    LOGN("Exiting FSM");

    return 0;
}
