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

#include "ovsdb_table.h"
#include "schema.h"
#include "wano.h"
#include "wano_internal.h"

bool wano_inet_config_update(
        const char *ifname,
        struct wano_inet_config_args *args)
{
    struct schema_Wifi_Inet_Config inet_config;
    ovsdb_table_t table_Wifi_Inet_Config;

    memset(&inet_config, 0, sizeof(inet_config));
    inet_config._partial_update = true;

    OVSDB_TABLE_INIT(Wifi_Inet_Config, if_name);

    /*
     * Fill in the parameters
     */
    SCHEMA_SET_STR(inet_config.if_name, ifname);

    if (args->if_type != NULL)
    {
        SCHEMA_SET_STR(inet_config.if_type, args->if_type);
    }

    if (args->enabled == WANO_TRI_TRUE)
    {
        SCHEMA_SET_INT(inet_config.enabled, true);
    }

    if (args->enabled == WANO_TRI_FALSE)
    {
        SCHEMA_SET_INT(inet_config.enabled, false);
    }

    if (args->network == WANO_TRI_TRUE)
    {
        SCHEMA_SET_INT(inet_config.network, true);
    }

    if (args->network == WANO_TRI_FALSE)
    {
        SCHEMA_SET_INT(inet_config.network, false);
    }

    if (args->nat == WANO_TRI_TRUE)
    {
        SCHEMA_SET_INT(inet_config.NAT, true);
    }

    if (args->nat == WANO_TRI_FALSE)
    {
        SCHEMA_SET_INT(inet_config.NAT, false);
    }

    if (args->ip_assign_scheme != NULL)
    {
        SCHEMA_SET_STR(inet_config.ip_assign_scheme, args->ip_assign_scheme);
    }

    if (args->inet_addr != NULL)
    {
        SCHEMA_SET_STR(inet_config.inet_addr, args->inet_addr);
    }

    if (args->netmask != NULL)
    {
        SCHEMA_SET_STR(inet_config.netmask, args->netmask);
    }

    if (args->gateway != NULL)
    {
        SCHEMA_SET_STR(inet_config.gateway, args->gateway);
    }

    if (args->dns1 != NULL)
    {
        inet_config.dns_present = true;
        STRSCPY(inet_config.dns_keys[inet_config.dns_len], "primary");
        STRSCPY(inet_config.dns[inet_config.dns_len], args->dns1);
        inet_config.dns_len++;
    }

    if (args->dns2 != NULL)
    {
        inet_config.dns_present = true;
        STRSCPY(inet_config.dns_keys[inet_config.dns_len], "secondary");
        STRSCPY(inet_config.dns[inet_config.dns_len], args->dns2);
        inet_config.dns_len++;
    }

    if (args->parent_ifname != NULL)
    {
        SCHEMA_SET_STR(inet_config.parent_ifname, args->parent_ifname);
    }

    if (args->vlan_id != 0)
    {
        SCHEMA_SET_INT(inet_config.vlan_id, args->vlan_id);
    }

    return ovsdb_table_upsert_simple(
            &table_Wifi_Inet_Config,
            "if_name",
            (char *)ifname,
            &inet_config,
            false);
}
