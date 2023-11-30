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

#include <net/if.h>
#include <dirent.h>

#include "nm2.h"
#include "os.h"
#include "os_nif.h"
#include "target.h"

#ifndef MAC_STR_LEN
#define MAC_STR_LEN 18
#endif /* MAC_STR_LEN */

static bool nm2_ovs_insert_port_into_native_bridge(char *br_name, char *port_name)
{
    char command[512];
    char *op = "add-port";
    bool ret = false;

    /* add/delete it to/from OVS bridge */
    LOGI("Linux bridge: %s port = %s bridge = %s", op, port_name, br_name);
    snprintf(command, sizeof(command), "ovs-vsctl --no-wait %s %s %s",
             op, br_name, port_name);
    LOGD("%s: Command: %s", __func__, command);
    ret = target_device_execute(command);

    return ret;
}


#define MAX_PATH_LEN 1024

void nm2_default_ports_create_tables(char *br_name)
{
    char bridge_path[MAX_PATH_LEN] = {0};
    struct dirent *de;
    DIR *dir;
    int ret = 0;

    snprintf(bridge_path, sizeof(bridge_path), "/sys/class/net/%s/brif", br_name);

    dir = opendir(bridge_path);
    if (dir == NULL)
    {
        LOG(DEBUG, "Failed to open directory %s", bridge_path);
        return;
    }

    while ((de = readdir(dir)) != NULL)
    {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        ret = nm2_ovs_insert_port_into_native_bridge(br_name, de->d_name);
        if (ret == false) LOGE("%s: Couldn't add port %s to %s", __func__, de->d_name, br_name);
    }
}

/**
 * @brief creates default Interface, Port and Bridge tables.
 *
 * @return true on success, false on failure
 */
bool nm2_default_br_create_tables(char *br_name)
{
    char command[512];
    char *op = "add-br";
    bool bridge_exists;
    bool ret = false;

    snprintf(command, sizeof(command), "ovs-vsctl --no-wait list-br | grep %s", br_name);
    LOGD("%s: Command: %s", __func__, command);

    bridge_exists = target_device_execute(command);

    if (!bridge_exists) {
        LOGI("Linux bridge: %s ", br_name);

        snprintf(command, sizeof(command), "ovs-vsctl --no-wait %s %s",
                 op, br_name);
        LOGD("%s: Command: %s", __func__, command);
        ret = target_device_execute(command);
    }

    return ret;
}


void nm2_default_br_init(char *br_name)
{
    nm2_default_br_create_tables(br_name);
    nm2_default_ports_create_tables(br_name);
}
