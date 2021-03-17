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

#include "nm2_iface.h"
#include "schema.h"
#include "log.h"
#include "osn_types.h"

#include "ovsdb_table.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

#define LOG_MOD_E(FMT, ...)         LOGE("route_config: "FMT, ## __VA_ARGS__)
#define LOG_TAB_E(FIELD, FMT, ...)  LOGE("Wifi_Route_Config->"FIELD" :"FMT, ## __VA_ARGS__)

static ovsdb_table_t table_Wifi_Route_Config;

static bool parse_route_cfg(osn_route4_t *route, const struct schema_Wifi_Route_Config *psch)
{
    // initial dest validation done by schema
    if (!osn_ip_addr_from_str(&route->dest, psch->dest_addr))
    {
        LOG_TAB_E("dest_addr", "Invalid destination IP address: %s", psch->dest_addr);
        return false;
    }

    osn_ip_addr_t mask;
    if (!osn_ip_addr_from_str(&mask, psch->dest_mask))
    {
        LOG_TAB_E("dest_mask", "Invalid destination IP mask: %s", psch->dest_mask);
        return false;
    }

    route->dest.ia_prefix = osn_ip_addr_to_prefix(&mask);

    route->gw = OSN_IP_ADDR_INIT;
    route->gw_valid = false;

    if (psch->gateway_exists)
    {
        if (!osn_ip_addr_from_str(&route->gw, psch->gateway))
        {
            LOG_TAB_E("gateway", "Invalid gateway IP address: %s", psch->gateway);
            return false;
        }
        route->gw_valid = true;
    }

    route->metric = psch->metric_exists ? psch->metric : -1;
    return true;
}

static struct nm2_iface * get_interface(
    const osn_route4_t *route,
    const struct schema_Wifi_Route_Config *psch)
{
    struct nm2_iface *ifc = NULL;

    if (psch->if_name_exists)
    {
        ifc = nm2_iface_get_by_name((char*)psch->if_name);
        if (NULL == ifc)
        {
            LOG_MOD_E("Network interface %s not found", psch->if_name);
        }
    }
    else if (route->gw_valid)
    {
        char if_name[C_IFNAME_LEN];
        if (osn_route_find_dev(route->gw, if_name, sizeof(if_name)))
        {
            ifc = nm2_iface_get_by_name(if_name);
        }

        if (NULL == ifc)
        {
            LOG_MOD_E("Network interface for %s gateway not found", FMT_osn_ip_addr(route->gw));
        }
    }
    else
    {
        LOG_TAB_E("if_name", "Network interface not found, if_name or gateway expected");
    }
    
    return ifc;
}

static bool add_route(const struct schema_Wifi_Route_Config *cfg)
{
    osn_route4_t rc;
    if (!parse_route_cfg(&rc, cfg)) return false;

    struct nm2_iface * nmifc = get_interface(&rc, cfg);
    if (NULL == nmifc) return false;

    if (!inet_route4_add(nmifc->if_inet, &rc))
    {
        LOG_MOD_E("Cannot add route to %s via %s", FMT_osn_ip_addr(rc.dest), nmifc->if_name);
        return false;
    }

    return true;
}

static bool del_route(const struct schema_Wifi_Route_Config *cfg)
{
    osn_route4_t rc;
    if (!parse_route_cfg(&rc, cfg)) return false;

    struct nm2_iface * nmifc = get_interface(&rc, cfg);
    if (NULL == nmifc) return false;

    if (!inet_route4_remove(nmifc->if_inet, &rc))
    {
        LOG_MOD_E("Cannot delete route to %s via %s", FMT_osn_ip_addr(rc.dest), nmifc->if_name);
        return false;
    }
    return true;
}

/*
 * OVSDB Wifi_Route_Config table update handler.
 */
static void callback_Wifi_Route_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Route_Config *old,
        struct schema_Wifi_Route_Config *new)
{
    switch(mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            add_route(new);
            break;
        case OVSDB_UPDATE_DEL:
            del_route(old);
            break;
        case OVSDB_UPDATE_MODIFY:
            /* Reject modifications: use delete/add route instead 
             * Old struct contains only changes, cannot identify correctly
             * which route shall be deleted before adding new one */
            LOG_MOD_E("Direct route modification not supported; use del->add route instead");
            break;
        default:
            LOG_MOD_E("Invalid mon_type(%d)", mon->mon_type);
    }
}

void nm2_route_write_init()
{
    // Initialize OVSDB tables
    OVSDB_TABLE_INIT(Wifi_Route_Config, dest_addr);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Wifi_Route_Config, false);
}
