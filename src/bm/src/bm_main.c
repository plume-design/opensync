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

/*
 * Band Steering Manager - Main Program Loop
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <stdarg.h>
#include <linux/types.h>

#include "os_backtrace.h"
#include "json_util.h"

#include "log.h"
#include "target.h"
#include "bm.h"

/*****************************************************************************/
#define MODULE_ID LOG_MODULE_ID_MAIN


/*****************************************************************************/
static struct ev_loop * _ev_loop;
static ev_signal        _ev_sigint;
static ev_signal        _ev_sigterm;
static ev_signal        _ev_sigusr1;

static log_severity_t   bm_log_severity = LOG_SEVERITY_INFO;

static ev_timer _sta_info_watcher;

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/

static void sta_info_update_callback(
        struct ev_loop *loop,
        ev_timer *watcher,
        int revents)
{
    bm_client_sta_info_update_callback();
}

void bm_main_set_timer(int sec)
{
    ev_timer_stop(_ev_loop, &_sta_info_watcher);
    ev_timer_set(&_sta_info_watcher, 0, sec);
    ev_timer_start(_ev_loop, &_sta_info_watcher);
}

static void
handle_usr1_signal(struct ev_loop *loop, ev_signal *w, int revents)
{
    LOGI("Received USR1 signal - dump stats");
    bm_client_dump_dbg_events();
    return;
}

static void
handle_signal(struct ev_loop *loop, ev_signal *w, int revents)
{
    LOGEM("Received signal %d, triggering shutdown", w->signum);
    ev_break(_ev_loop, EVBREAK_ALL);
    return;
}

/******************************************************************************
 * PUBLIC API definitions
 *****************************************************************************/

int
main(int argc, char **argv)
{
    _ev_loop = EV_DEFAULT;

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &bm_log_severity)){
        return -1;
    }

    // Initialize logging library
    target_log_open("BM", 0);
    LOGN("Starting Band Steering Manager");
    log_severity_set(bm_log_severity);

    // Enable backtrace support
    backtrace_init();

    // Setup signal handlers
    ev_signal_init(&_ev_sigint,  handle_signal, SIGINT);
    ev_signal_start(_ev_loop, &_ev_sigint);
    ev_signal_init(&_ev_sigterm, handle_signal, SIGTERM);
    ev_signal_start(_ev_loop, &_ev_sigterm);
    ev_signal_init(&_ev_sigusr1, handle_usr1_signal, SIGUSR1);
    ev_signal_start(_ev_loop, &_ev_sigusr1);

    // Initialize JSON memory debugging
    json_memdbg_init(_ev_loop);

    // Initialize EV scheduler
    if (evsched_init(_ev_loop) == false) {
        LOGEM("Failed to initialize EVSCHED");
        return(1);
    }

    if (!target_init(TARGET_INIT_MGR_BM, _ev_loop)) {
        LOGEM("Failed to initialize target");
        return(1);
    }

    // Connect to OVSDB
    if (ovsdb_init_loop(_ev_loop, "BM") == false) {
        LOGEM("Failed to initialize and connect to OVSDB");
        return(1);
    }

    // Initialize Kicking logic
    if (bm_kick_init() == false) {
        LOGEM("Failed to initialize kicking logic");
        return(1);
    }

    // Initialize BSAL Events
    if (bm_events_init(_ev_loop) == false) {
        LOGEM("Failed to initialize events");
        return(1);
    }

    // Initialize interface group
    if (bm_group_init() == false) {
        LOGEM("Failed to initialize interface group");
        return(1);
    }

    // Initialize clients
    if (bm_client_init() == false) {
        LOGEM("Failed to initialize clients");
        return(1);
    }
    // Initialize stats
    if(bm_stats_init(_ev_loop) == false) {
        LOGEM( "Failed to initialize BM stats" );
        return(1);
    }

    // Initialize neighbors
    if( bm_neighbor_init() == false ) {
        LOGEM( "Failed to initialize BM Neighbors" );
        return(1);
    }

    ev_timer_init(&_sta_info_watcher, sta_info_update_callback, 25.0, 25.0);
    ev_timer_start(_ev_loop, &_sta_info_watcher);

    /* Register to dynamic severity updates */
    log_register_dynamic_severity(_ev_loop);

    // Run main loop
    ev_run(_ev_loop, 0);

    // Cleanup & Exit
    LOGN("Band Steering Manager shutting down");

    bm_client_cleanup();
    bm_stats_cleanup();
    bm_group_cleanup();
    bm_events_cleanup();
    bm_kick_cleanup();
    bm_neighbor_cleanup();
    ovsdb_stop_loop(_ev_loop);
    target_close(TARGET_INIT_MGR_BM, _ev_loop);
    evsched_cleanup();
    ev_default_destroy();
    return(0);
}
