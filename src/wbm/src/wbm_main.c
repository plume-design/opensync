#include <ev.h>          // libev routines
#include <getopt.h>      // command line arguments

#include "evsched.h"     // ev helpers
#include "log.h"         // logging routines
#include "json_util.h"   // json routines
#include "os.h"          // OS helpers
#include "ovsdb.h"       // OVSDB helpers
#include "target.h"      // target API

#include "wbm.h"         // own header

/* Log entries from this file will contain "MAIN" */
#define MODULE_ID LOG_MODULE_ID_MAIN

#define MNGR_NAME "WBM"
#define TARGET_INIT_MGR TARGET_INIT_MGR_WBM

static ev_signal       g_ev_sigterm;
static log_severity_t  g_log_severity = LOG_SEVERITY_INFO;

static void wbm_main_handler_sigterm(struct ev_loop *loop, ev_signal *w, int revents)
{
    LOGI("Received signal %d, triggering shutdown", w->signum);
    ev_break(loop, EVBREAK_ALL);
}

int main(int argc, char **argv)
{
    struct ev_loop *loop = EV_DEFAULT;

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &g_log_severity) != 0) {
        return -1;
    }

    // Initialize logging library
    target_log_open(MNGR_NAME, 0);  // 0 = syslog and TTY (if present)
    LOGN("Starting manager - " MNGR_NAME);
    log_severity_set(g_log_severity);

    // Enable runtime severity updates
    log_register_dynamic_severity(loop);

    // Install crash handlers that dump the stack to the log file
    backtrace_init();

    // Allow recurrent json memory usage reports in the log file
    json_memdbg_init(loop);

    // Setup signal handlers
    ev_signal_init(&g_ev_sigterm, wbm_main_handler_sigterm, SIGTERM);
    ev_signal_start(loop, &g_ev_sigterm);

    // Initialize EV context
    if (!evsched_init(loop)) {
        LOGE("Initializing " MNGR_NAME " (Failed to initialize EVSCHED)");
        return -1;
    }

    // Initialize target structure
    if (!target_init(TARGET_INIT_MGR, loop)) {
        LOGE("Initializing " MNGR_NAME " (Failed to initialize target library)");
        return -1;
    }

    // Connect to OVSDB
    if (!ovsdb_init_loop(loop, MNGR_NAME)) {
        LOGE("Initializing " MNGR_NAME " (Failed to initialize OVSDB init loop)");
        return -1;
    }

    // Register to relevant OVSDB tables events
    if (wbm_ovsdb_init() != 0) {
        LOGE("Initializing " MNGR_NAME " (Failed to initialize ovsdb)");
        return -1;
    }

    // Initialize core
    if (wbm_engine_init() != 0) {
        LOGE("Initializing " MNGR_NAME " (Failed to initialize core)");
        return -1;
    }

    // Start the event loop
    ev_run(loop, 0);

    wbm_engine_uninit();
    wbm_ovsdb_uninit();
    ovsdb_stop_loop(loop);
    target_close(TARGET_INIT_MGR, loop);
    ev_signal_stop(loop, &g_ev_sigterm);
    ev_loop_destroy(loop);

    LOGN("Exiting " MNGR_NAME);

    return 0;
}
