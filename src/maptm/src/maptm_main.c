/*
* Copyright (c) 2020, Sagemcom.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
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

// Initialize maptm module
static bool maptm_init(void)
{
    bool retval = true;
    strucWanConfig.mapt_support = true;
    retval = maptm_persistent();
    if (!retval)
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

    bool result;
    result = target_init(TARGET_INIT_MGR_MAPTM, loop);
    if (!result)
    {
        return -1;
    }

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

    /* Load MAP-T configuration */
    if (!maptm_init())
    {
        LOGE("Failed in maptm_init()");
        return -1;
    }

    // From this point on, log severity can change in runtime.
    log_register_dynamic_severity(loop);

    // Run
    ev_run(loop, 0);

    // Exit
    target_close(TARGET_INIT_MGR_MAPTM, loop);

    if (!ovsdb_stop_loop(loop)) {}

    ev_default_destroy();
    return 0;
}
