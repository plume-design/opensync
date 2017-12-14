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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "log.h"
#include "dpp_device.h"

#define MODULE_ID LOG_MODULE_ID_TARGET

/*
 * DEVICE STATS
 */
#define LINUX_PROC_LOADAVG_FILE   "/proc/loadavg"
#define LINUX_PROC_UPTIME_FILE   "/proc/uptime"

/*
 * PUBLIC  definitions
 */

static bool linux_device_load_get(
        dpp_device_record_t        *record)
{
    int32_t                         rc;
    FILE                           *proc_file = NULL;

    proc_file = fopen(LINUX_PROC_LOADAVG_FILE, "r");
    if (NULL == proc_file)
    {
        LOG(ERR,
            "Parsing device stats (Failed to open %s)",
            LINUX_PROC_LOADAVG_FILE);
        return false;
    }

    rc =
        fscanf(proc_file,
        "%lf %lf %lf",
        &record->load[DPP_DEVICE_LOAD_AVG_ONE],
        &record->load[DPP_DEVICE_LOAD_AVG_FIVE],
        &record->load[DPP_DEVICE_LOAD_AVG_FIFTEEN]);

    fclose(proc_file);

    if (DPP_DEVICE_LOAD_AVG_QTY != rc)
    {
        LOG(ERR,
            "Parsing device stats (Failed to read %s)",
            LINUX_PROC_LOADAVG_FILE);
        return false;
    }

    LOG(TRACE,
        "Parsed device load %0.2f %0.2f %0.2f",
        record->load[DPP_DEVICE_LOAD_AVG_ONE],
        record->load[DPP_DEVICE_LOAD_AVG_FIVE],
        record->load[DPP_DEVICE_LOAD_AVG_FIFTEEN]);

    return true;
}

static bool linux_device_uptime_get(
        dpp_device_record_t        *record)
{
    int32_t     rc;
    char        *name = LINUX_PROC_UPTIME_FILE;
    FILE        *proc_file = NULL;

    proc_file = fopen(name, "r");
    if (NULL == proc_file)
    {
        LOG(ERR, "Parsing device stats (Failed to open %s)", name);
        return false;
    }
    rc = fscanf(proc_file, "%u", &record->uptime);
    fclose(proc_file);
    if (1 != rc)
    {
        LOG(ERR, "Parsing device stats (Failed to read %s)", name);
        return false;
    }
    LOG(TRACE, "Parsed device uptime %u", record->uptime);

    return true;
}

bool target_stats_device_get(
        dpp_device_record_t        *device_entry)
{
    bool rc;

    rc = linux_device_load_get(device_entry);
    if (!rc)
    {
        LOG(ERR,
            "Sending device report "
            "(failed to retreive device load");
        return false;
    }

    rc = linux_device_uptime_get(device_entry);
    if (!rc)
    {
        LOG(ERR,
            "Sending device report "
            "(failed to retreive device uptime");
        return false;
    }

    return true;
}
