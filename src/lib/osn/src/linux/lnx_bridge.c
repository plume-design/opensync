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


#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "execsh.h"
#include "log.h"
#include "util.h"

#include "lnx_bridge.h"


const char lnx_br_create_cmd[] = _S(ip link add name "$1" type bridge);
const char lnx_br_del_cmd[] = _S(ip link delete dev "$1" type bridge);
const char lnx_br_port_add_cmd[] = _S(brctl addif "$1" "$2");
const char lnx_br_port_del_cmd[] = _S(brctl delif "$1" "$2");
const char lnx_port_hairpin_cmd[] = _S(ip link set "$1" type bridge_slave hairpin "$2");

bool lnx_bridge_create(char *br)
{
    int rc;

    LOGT("%s(): creating bridge %s", __func__, br);
    rc = execsh_log(LOG_SEVERITY_NOTICE, lnx_br_create_cmd, br);
    if (rc != 0)
    {
        LOGT("%s(): failed to create bridge %s ", __func__, br);
        return false;
    }

    return true;
}

bool lnx_bridge_del(char *br)
{
    int rc;

    LOGT("%s(): deleting bridge %s", __func__, br);
    rc = execsh_log(LOG_SEVERITY_NOTICE, lnx_br_del_cmd, br);
    if (rc != 0)
    {
        LOGT("%s(): failed to delete bridge %s ", __func__, br);
        return false;
    }

    return true;
}

bool lnx_bridge_set_hairpin(char *port, bool enable)
{
    int rc;

    LOGT("%s(): setting hairpin mode %s", __func__, port);
    if (enable)
    {
        rc = execsh_log(LOG_SEVERITY_NOTICE, lnx_port_hairpin_cmd, port, "on");
    }
    else
    {
        rc = execsh_log(LOG_SEVERITY_NOTICE, lnx_port_hairpin_cmd, port, "off");
    }

    if (rc != 0)
    {
        LOGT("%s(): failed to configure hairpin mode for %s ", __func__, port);
        return false;
    }
    return true;
}

bool lnx_bridge_add_port(char *br, char *port)
{
    int rc;
    char brport_path[C_MAXPATH_LEN];
    char master_path[C_MAXPATH_LEN];
    char master_realpath[PATH_MAX];
    char *current_bridge;

    snprintf(brport_path, sizeof(brport_path), "/sys/class/net/%s/brport", port);
    if (access(brport_path, F_OK) == 0)
    {
        LOGD("%s(): Port %s already added to bridge. Checking master...", __func__, port);
        snprintf(master_path, sizeof(master_path), "/sys/class/net/%s/master", port);
        if (realpath(master_path, master_realpath) == NULL)
        {
            LOGW("%s(): Reading master symbolic link for port %s failed [errno=%d]: %s",
                    __func__, port, errno, strerror(errno));
        }
        else
        {
            current_bridge = strrchr(master_realpath, '/') + 1;
            if (current_bridge == NULL)
            {
                LOGW("%s(): basename was not found for %s", __func__, master_realpath);
            }
            else if(strcmp(current_bridge, br) == 0)
            {
                LOGD("%s(): Port %s already added to %s. Skipping!", __func__, port, br);
                return true;
            }
        }
    }

    rc = execsh_log(LOG_SEVERITY_NOTICE, lnx_br_port_add_cmd, br, port);
    if (rc != 0)
    {
        LOGT("%s(): failed to add port %s to bridge %s ", __func__, port, br);
        return false;
    }

    return true;
}

bool lnx_bridge_del_port(char *br, char *port)
{
    int rc;

    rc = execsh_log(LOG_SEVERITY_NOTICE, lnx_br_port_del_cmd, br, port);
    if (rc != 0)
    {
        LOGT("%s(): failed to remove port %s to bridge %s ", __func__, port, br);
        return false;
    }

    return true;
}
