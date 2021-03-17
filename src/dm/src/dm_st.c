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

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ev.h>
#include <limits.h>
#include <dirent.h>
#include <time.h>

#include "log.h"
#include "ovsdb.h"
#include "schema.h"

#include "monitor.h"
#include "json_util.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "os_util.h"
#include "util.h"

#include "target.h"
#include "dm.h"


/* can't be on the stack */
static ovsdb_update_monitor_t st_monitor;
static bool st_in_progress = false;  /* prevent multiple speedtests simultaneous run */

void dm_stupdate_cb(ovsdb_update_monitor_t *self)
{
    struct schema_Wifi_Speedtest_Config speedtest_config;
    pjs_errmsg_t perr;
    struct dm_st_plugin *plugin;

    LOG(DEBUG, "%s", __FUNCTION__);

    switch (self->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:

            if (st_in_progress)
            {
                LOG(ERR, "Speedtest already in progress");
                return;
            }

            if (!schema_Wifi_Speedtest_Config_from_json(&speedtest_config, self->mon_json_new, false, perr))
            {
                LOG(ERR, "Parsing Wifi_Speedtest_Config NEW or MODIFY request: %s", perr);
                return;
            }

            /* run the speed test according to the cloud instructions */
            plugin = dm_st_plugin_find(speedtest_config.test_type);
            if (plugin)
            {
                plugin->st_run(&speedtest_config);
            }
            else
            {
                LOG(ERR, "speedtest '%s' not supported", speedtest_config.test_type);
            }

            break;

        case OVSDB_UPDATE_DEL:
            /* Reset configuration */
            LOG(INFO, "Cloud cleared Wifi_Speedtest_Config table");
            break;

        default:
            LOG(ERR, "Update Monitor for Wifi_Speedtest_Config reported an error.");
            break;
    }
}


/*
 * Monitor Wifi_Speedtest_Config table
 */
bool dm_st_monitor()
{
    bool ret = false;

    /* Set monitoring */
    if (false == ovsdb_update_monitor(&st_monitor,
                                      dm_stupdate_cb,
                                      SCHEMA_TABLE(Wifi_Speedtest_Config),
                                      OMT_ALL)
       )
    {
        LOG(ERR, "Error initializing Wifi_Speedtest_Config monitor");
        goto exit;
    }
    else
    {
        LOG(NOTICE, "Wifi_Speedtest_Config monitor started");
        dm_st_in_progress_set(false);
    }
    ret = true;

exit:
    return ret;
}

/*
 * Set control flag for handling multiple speedtests requests
 */
void dm_st_in_progress_set(bool value)
{
    st_in_progress = value;
    if (false == st_in_progress)
        LOG(DEBUG, "Speedtest ready");
    else
        LOG(DEBUG, "Speedtest in progress");
}

/*
 * Get control flag for handling multiple speedtests requests
 */
bool dm_st_in_progress_get()
{
    return st_in_progress;
}
