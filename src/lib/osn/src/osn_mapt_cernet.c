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

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "log.h"
#include "os.h"

#include "osn_mapt.h"

#define MAPTM_CMD_LEN               256

bool osn_mapt_configure(
        const char *brprefix,
        int ratio,
        const char *intfname,
        const char *wanintf,
        const char *IPv6prefix,
        const char *subnetcidr4,
        const char *ipv4PublicAddress,
        int PSIDoffset,
        int PSID)
{
    char cmd[MAPTM_CMD_LEN] = {0x0};

    if ((brprefix==NULL) || (intfname==NULL) || (wanintf==NULL) || (IPv6prefix==NULL) || (subnetcidr4==NULL) || (ipv4PublicAddress==NULL))
    {
        LOG(ERR, "map-t: %s: Invalid parameter(s)", intfname);
        return false;
    }

    snprintf(cmd, MAPTM_CMD_LEN, "ivictl -r -d -P %s -R %d -T ", brprefix, ratio);
    /* We have to verify why cmd_log return false otherwise cmd is exc */
    LOGT("cmd: %s", cmd);
    cmd_log(cmd);

    snprintf(
        cmd,
        MAPTM_CMD_LEN,
        "ivictl -s -i %s -I %s -P %s -H -N -a %s -A %s -z %d -R %d -o %d -T",
        intfname,
        wanintf,
        IPv6prefix,
        subnetcidr4,
        ipv4PublicAddress,
        PSIDoffset,
        ratio,
        PSID);
    LOGT("cmd: %s", cmd);
    /* We have to verify why cmd_log return false otherwise cmd is exc */
    cmd_log(cmd);

    return true;
}

bool osn_mapt_stop()
{
    char cmd[MAPTM_CMD_LEN] = {0x0};

    snprintf(cmd, sizeof(cmd), "ivictl -q");
    if (cmd_log(cmd)) return true;

    return false;
}
