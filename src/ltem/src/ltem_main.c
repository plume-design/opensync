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
#include <getopt.h>
#include <time.h>
#include <string.h>

#include "memutil.h"
#include "log.h"
#include "json_util.h"
#include "os.h"
#include "ovsdb.h"
#include "target.h"
#include "network_metadata.h"

#include "ltem_mgr.h"

/* Default log severity */
static log_severity_t  log_severity = LOG_SEVERITY_INFO;

/* Log entries from this file will contain "MAIN" */
#define MODULE_ID LOG_MODULE_ID_MAIN

/**
 * @brief ltem manager
 *
 */
static ltem_mgr_t ltem_mgr;

/**
 * @brief ltem manager accessor
 */
ltem_mgr_t *
ltem_get_mgr(void)
{
    return &ltem_mgr;
}

bool
ltem_init_mgr(struct ev_loop *loop)
{
    lte_config_info_t *lte_config;
    lte_state_info_t *lte_state;
    lte_route_info_t *lte_route;

    ltem_mgr_t *mgr = ltem_get_mgr();

    memset(mgr, 0, sizeof(ltem_mgr_t));

    mgr->loop = loop;

    lte_config = CALLOC(1, sizeof(lte_config_info_t));
    lte_state = CALLOC(1, sizeof(lte_state_info_t));
    lte_route = CALLOC(1, sizeof(lte_route_info_t));

    mgr->lte_config_info = lte_config;
    mgr->lte_state_info = lte_state;
    mgr->lte_route = lte_route;

    return true;
}

/**
 * Main
 *
 * Note: Command line arguments allow overriding the log severity
 */
int main(int argc, char ** argv)
{
    struct ev_loop *loop = EV_DEFAULT;
    ltem_mgr_t *mgr;

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &log_severity))
    {
        return -1;
    }

    // Initialize logging library
    target_log_open("LTEM", 0);  // 0 = syslog and TTY (if present)
    LOGN("Starting LTEM (LTE Manager)");
    log_severity_set(log_severity);

    // Enable runtime severity updates
    log_register_dynamic_severity(loop);

    // Install crash handlers that dump the stack to the log file
    backtrace_init();

    // Initialize target structure
    if (!target_init(TARGET_INIT_MGR_LTEM, loop))
    {
        LOGE("Initializing LTEM "
             "(Failed to initialize target library)");
        return -1;
    }

    ltem_set_lte_state(LTEM_LTE_STATE_INIT);

    // Initialize the manager
    if (!ltem_init_mgr(loop))
    {
        LOGE("Initializing LTEM "
              "(Failed to initialize manager)");
        return -1;
    }

    ltem_event_init();

    // Connect to OVSDB
    if (!ovsdb_init_loop(loop, "LTEM"))
    {
        LOGE("Initializing LTEM "
             "(Failed to initialize OVSDB)");
        return -1;
    }

    // Register to relevant OVSDB tables events
    if (ltem_ovsdb_init())
    {
        LOGE("Initializing LTEM "
             "(Failed to initialize LTEM tables)");
        return -1;
    }

    // Create client table
    mgr = ltem_get_mgr();
    ltem_create_client_table(mgr);

    LOGI("%s: state=%s", __func__, ltem_get_lte_state_name(mgr->lte_state));

    // Start the event loop
    ev_run(loop, 0);

    target_close(TARGET_INIT_MGR_LTEM, loop);

    if (!ovsdb_stop_loop(loop))
    {
        LOGE("Stopping LTEM "
             "(Failed to stop OVSDB)");
    }

    ev_loop_destroy(loop);

    LOGN("Exiting LTEM");

    return 0;
}
