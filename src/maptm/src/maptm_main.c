/* Copyright (c) 2019 Charter, Inc.
 *
 * This module contains unpublished, confidential, proprietary
 * material. The use and dissemination of this material are
 * governed by a license. The above copyright notice does not
 * evidence any actual or intended publication of this material.
 *
 * Created: 29 July 2019
 *
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

#include "evsched.h"
#include "log.h"
#include "os.h"
#include "ovsdb.h"
#include "evext.h"
#include "os_backtrace.h"
#include "json_util.h"
#include "target.h"

#include "maptm.h"
//#include "target_maptm.h"

#ifdef MAPTM_DEBUG
#undef LOGI
#define LOGI    printf
#endif 

/*****************************************************************************/

#define MODULE_ID LOG_MODULE_ID_MAIN

/*****************************************************************************/

static log_severity_t    maptm_log_severity = LOG_SEVERITY_INFO;

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/
static bool maptm_init(void)
{
    bool retval = true;
    retval = maptm_persistent();
    if (retval != true)
    {
        LOGE("Failed setting up MAPT Table");
    }
    return retval;
}

/******************************************************************************
 *  PUBLIC API definitions
 *****************************************************************************/
int main(int argc, char ** argv)
{
    struct ev_loop *loop = EV_DEFAULT;

    // Parse command-line arguments
    if (os_get_opt(argc, argv, &maptm_log_severity))
    {
        return -1;
    }

    // Logging setup
    target_log_open("MAPTM", 0);
    LOGT("Starting MAPT Manager");

    log_severity_set(maptm_log_severity);
    if (!target_init(TARGET_INIT_MGR_MAPTM, loop))
    { 
        return -1;
    }

    LOGT("Starting target_init  Manager");
    if (!ovsdb_init_loop(loop, "maptm"))
    {
        LOGE("Initializing maptm " "(Failed to initialize OVSDB)");
        return -1;
    }

    if (maptm_ovsdb_init())
    {
        return -1;
    }
    if (maptm_dhcp_option_init())
    {
        return -1;
    }
    
    /* Load Configuration mapt*/
    if (!maptm_init())
    {
        LOGE("Failed in maptm_init()");
        //return -1;
    }
    
    // From this point on log severity can change in runtime.
    log_register_dynamic_severity(loop);

    // Run
    ev_run(loop, 0);

    // Exit
    target_close(TARGET_INIT_MGR_MAPTM, loop);

    if (!ovsdb_stop_loop(loop)) {}

    ev_default_destroy();
    return 0;
}
