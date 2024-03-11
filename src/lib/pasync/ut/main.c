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

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ev.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <ev.h>
#include <mosquitto.h>

#include "log.h"
#include "os_socket.h"
#include "os_backtrace.h"
#include "ovsdb.h"
#include "jansson.h"
#include "monitor.h"
#include "json_util.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "os_util.h"

#include "target.h"
#include "pasync.h"
#include "tpsm.h"


#include "schema.h"
#include "log.h"
#include "os_backtrace.h"
#include "pasync.h"
#include "target.h"

/* speed test json output decoding */
#include "tpsm_st_pjs.h"
#include "pjs_gen_h.h"
#include "tpsm_st_pjs.h"
#include "pjs_gen_c.h"


#define ST_STATUS_OK    (0)     /* everything all right         */
#define ST_STATUS_JSON  (-1)    /* received non-json ST output  */
#define ST_STATUS_READ  (-2)    /* can't convert json to struct */


#define SISP    "isp: "
#define LSISP    strlen(SISP)

static bool st_in_progress = false;  /* prevent multiple speedtests simultaneous run */

/*
 * Initialize application logger facility
 */
bool init_logger(log_severity_t llevel)
{
    bool success = false;

    success = log_open("PASYNC", LOG_OPEN_STDOUT);

    LOG(NOTICE, "Starting PASYNC");

    /* set application global log level */
    log_severity_set(llevel);

    return success;
}


void st_cb(int id, void * buff, int buff_sz)
{
    static struct streport report;  /* used to convert ST output to struct */
    ovs_uuid_t uuid;
    struct schema_Wifi_Speedtest_Status  st_status;
    int status = ST_STATUS_READ;
    pjs_errmsg_t err;
    char * lend = NULL;
    long num;


    LOG(NOTICE, "--------------------------------------------------");
    LOG(NOTICE, "Received speed test result, %d bytes received", buff_sz);
    LOG(NOTICE, "--------------------------------------------------");
    LOG(NOTICE, "\n%s", (char*)buff);
    LOG(NOTICE, "--------------------------------------------------");

    /* signal that ST has been completed */
    st_in_progress = false;

    /* clear all data from last report */
    memset(&report, 0, sizeof(struct streport));

    /* zero whole st_status structure */
    memset(&st_status, 0, sizeof(struct schema_Wifi_Speedtest_Status));

    /* read buffer line by line and process the results */
    lend = strchr(buff, '\n');
    while ((lend != NULL) && (lend - (char*)buff < buff_sz))
    {
        *lend = '\0';

        if (buff == strstr(buff, SISP))
        {
            STRSCPY(st_status.ISP, buff + LSISP);
            st_status.ISP_exists = true;
        }

        if (buff == strstr(buff, "latency: "))
        {
            if (os_strtoul(buff + strlen("latency: "), &num, 0) == true)
            {
                st_status.RTT = num;
                st_status.RTT_exists = true;
            }
        }

        if (buff == strstr(buff, "download: "))
        {
            if (os_strtoul(buff + strlen("download: "), &num, 0) == true)
            {
                st_status.DL = num;
                st_status.DL_exists = true;
            }
        }

        if (buff == strstr(buff, "upload: "))
        {
            if (os_strtoul(buff + strlen("upload: "), &num, 0) == true)
            {
                st_status.UL = num;
                st_status.UL_exists = true;
            }
        }

        /* next line */
        buff = lend + 1;
        lend = strchr(buff, '\n');
    }



    /* set all relevant fields in st_status */
    if (report.downlink_exists)
    {
        st_status.DL = report.downlink;
        st_status.DL_exists = true;
    }

    if (report.uplink_exists)
    {
        st_status.UL = report.uplink;
        st_status.UL_exists = true;
    }

    if (report.latency_exists)
    {
        st_status.RTT = report.latency;
        st_status.RTT_exists = true;
    }

    if (report.isp_exists)
    {
        STRSCPY(st_status.ISP, report.isp);
        st_status.ISP_exists = true;
    }

    if (report.sponsor_exists)
    {
        STRSCPY(st_status.server_name, report.sponsor);
        st_status.server_name_exists = true;
    }

    if (report.timestamp_exists)
    {
        st_status.timestamp = report.timestamp;
        st_status.timestamp_exists = true;
    }

    if (report.DL_bytes_exists)
    {
        st_status.DL_bytes= report.DL_bytes;
        st_status.DL_bytes_exists = true;
    }

    if (report.UL_bytes_exists)
    {
        st_status.UL_bytes= report.UL_bytes;
        st_status.UL_bytes_exists = true;
    }

    if ((st_status.UL_exists == true) && (st_status.DL_exists = true))
    {
        status = ST_STATUS_OK;
    }

    st_status.testid = id;
    st_status.status = status;
    STRSCPY(st_status.test_type, "OOKLA");
    st_status.test_type_exists = true;

    /* fill the row with NODE data */
    if (false == ovsdb_sync_insert(SCHEMA_TABLE(Wifi_Speedtest_Status),
                                   schema_Wifi_Speedtest_Status_to_json(&st_status, err),
                                   &uuid)
       )
    {
        LOG(ERR, "Speedtest_Status insert error, ST results not written: DL: %f, UL: %f",
            st_status.DL,
            st_status.UL);
    }
    else
    {
        LOG(NOTICE, "Speedtest results written stamp: %d, status: %d, DL: %f, UL: %f, isp: %s, sponsor: %s, latency %f",
                     st_status.timestamp,
                     st_status.status,
                     st_status.DL,
                     st_status.UL,
                     st_status.ISP,
                     st_status.server_name,
                     st_status.RTT);
    }

}


void cb(int id, void * buff, int buff_sz)
{
    LOG(NOTICE, "--------------------------------------------------");
    LOG(NOTICE, "task %d completed, %d bytes received", id, buff_sz);
    LOG(NOTICE, "--------------------------------------------------");
    LOG(NOTICE, "\n%s", (char*)buff);
    LOG(NOTICE, "--------------------------------------------------");
}

/*
 * Main
 */
int main (int argc, char ** argv)
{
    struct ev_loop *loop;
    char st_cmd[TARGET_BUFF_SZ];

    /* log all errors, warnings, etc. */
    init_logger(LOG_SEVERITY_DEBUG);

    backtrace_init();

    loop = ev_default_loop(0);

    if (!loop)
    {
        LOGE("Initializing pasync test failed");
    }

    //json_memdbg_init(loop);
    /* start a new process and monitor its output */
    const char* tools_dir = target_tools_dir();
    if (tools_dir == NULL)
    {
        LOG(ERR, "Error, tools dir not defined");
    }
    else
    {
        struct stat sb;
        if ( !(0 == stat(tools_dir, &sb) && S_ISDIR(sb.st_mode)) )
        {
            LOG(ERR, "Error, tools dir does not exist");
        }
    }

    sprintf(st_cmd, CONFIG_INSTALL_PREFIX"/bin/ookla -t 2 -c http://www.speedtest.net/api/embed/plume/config");
    if (false == pasync_ropen(loop, 1, st_cmd, st_cb))
    {
        LOG(ERR, "Error running pasync_ropen 1");
    }

#if 0
    if (false == pasync_ropen(loop, 2, "ls -alh /tmp", cb))
    {
        LOG(ERR, "Error running pasync_ropen 2");
    }

    sprintf(st_cmd, "%s/linkspeed", tools_dir);
    if (false == pasync_ropen(loop, 3, st_cmd, cb))
    {
        LOG(ERR, "Error running pasync_ropen 3");
    }

    if (false == pasync_ropen(loop, 4, "/bin/true", cb))
    {
        LOG(ERR, "Error running pasync_ropen 4");
    }
#endif

    /* start the main loop and wait for the event to come */
    ev_run(loop, 0);

    LOGN("Exiting pasync_test");

    /* clean up */
    ev_default_destroy();

    return 0;
}
