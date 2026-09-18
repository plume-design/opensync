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

#include <ox_dm_deviceinfo.h>
#include <ox_router.h>
#include <ox_table_singleton.h>
#include <os_tr181.h>
#include <dm_schema.h>
#include <memutil.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Network interface used to derive ManufacturerOUI from its MAC address.
 * Used as a fallback when the MANUFACTUREROUI environment variable is not set.
 * Override per-platform if the base MAC is on a different interface. */
#define DEVICEINFO_OUI_IFNAME "eth0"

bool ox_dm_deviceinfo_add_routes(ox_router_t *router)
{
    const ox_route_t routes[] = {
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.DeviceInfo.Manufacturer),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_table = "AWLAN_Node",
                        .ovsdb_column = "vendor_manufacturer",
                        .get_cb = ox_table_singleton_auto_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.DeviceInfo.FriendlyName),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_table = "AWLAN_Node",
                        .ovsdb_column = "vendor_name",
                        .get_cb = ox_table_singleton_auto_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.DeviceInfo.ModelName),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_table = "AWLAN_Node",
                        .ovsdb_column = "model",
                        .get_cb = ox_table_singleton_auto_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.DeviceInfo.SerialNumber),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_table = "AWLAN_Node",
                        .ovsdb_column = "serial_number",
                        .get_cb = ox_table_singleton_auto_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.DeviceInfo.SoftwareVersion),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_table = "AWLAN_Node",
                        .ovsdb_column = "firmware_version",
                        .get_cb = ox_table_singleton_auto_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.DeviceInfo.HardwareVersion),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_table = "AWLAN_Node",
                        .ovsdb_column = "revision",
                        .get_cb = ox_table_singleton_auto_get,
                    },
        },
        {
            .param =
                    {
                        .tr181_param = DM_SCHEMA_DUP(Device.DeviceInfo.ModelNumber),
                        .tr181_type = OS_TR181_TYPE_STRING,
                        .ovsdb_table = "AWLAN_Node",
                        .ovsdb_column = "sku_number",
                        .get_cb = ox_table_singleton_auto_get,
                    },
        },
        {},
    };
    /* Heap-copy the stack array so the router's param_ctx->route pointers remain valid
     * for the process lifetime. Intentionally not freed. */
    ox_route_t *routes_heap = MALLOC(sizeof(routes));
    memcpy(routes_heap, routes, sizeof(routes));
    return ox_router_add_routes(router, routes_heap);
}

static os_tr181_error_t deviceinfo_get_hostname(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    char buf[256];
    buf[sizeof(buf) - 1] = '\0';
    if (gethostname(buf, sizeof(buf) - 1) != 0) *buf = '\0';
    return os_val_set_str_dup(value, buf);
}

static os_tr181_error_t deviceinfo_get_manufacturer_oui(const char *param_path, os_tr181_val_t *value, void *user_data)
{
    /* Prefer MANUFACTUREROUI env var (set by platform provisioning scripts) */
    const char *env_oui = getenv("MANUFACTUREROUI");
    if (env_oui != NULL && strlen(env_oui) == 6) return os_val_set_str_dup(value, env_oui);

    /* Fall back to deriving OUI from the first 3 bytes of DEVICEINFO_OUI_IFNAME MAC */
    struct ifreq ifr = {0};
    strncpy(ifr.ifr_name, DEVICEINFO_OUI_IFNAME, sizeof(ifr.ifr_name) - 1);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return OS_TR181_ERROR;
    const int rc = ioctl(fd, SIOCGIFHWADDR, &ifr);
    close(fd);
    if (rc < 0) return OS_TR181_ERROR;
    const unsigned char *mac = (unsigned char *)ifr.ifr_hwaddr.sa_data;
    char oui[7];
    snprintf(oui, sizeof(oui), "%02X%02X%02X", mac[0], mac[1], mac[2]);
    return os_val_set_str_dup(value, oui);
}

bool ox_dm_deviceinfo_register(ox_router_t *router)
{
    os_tr181_handle_t *tr181 = router->tr181_handle;
    os_tr181_error_t err;

    err = os_tr181_register_parameter(
            tr181,
            DM_SCHEMA(Device.DeviceInfo.HostName),
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_READONLY,
            deviceinfo_get_hostname,
            NULL,
            NULL);
    if (err != OS_TR181_SUCCESS) return false;

    err = os_tr181_register_parameter(
            tr181,
            DM_SCHEMA(Device.DeviceInfo.ManufacturerOUI),
            OS_TR181_TYPE_STRING,
            OS_TR181_ACCESS_READONLY,
            deviceinfo_get_manufacturer_oui,
            NULL,
            NULL);
    return err == OS_TR181_SUCCESS;
}
