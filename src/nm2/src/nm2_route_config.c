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

/*
 * ===========================================================================
 *  NM2 Wifi_Route_Config implementation
 * ===========================================================================
 */

#include "ds_tree.h"
#include "log.h"
#include "osn_types.h"
#include "ovsdb_table.h"
#include "schema.h"

#include "nm2_iface.h"

struct nm2_route_cfg
{
    ovs_uuid_t      rc_uuid;        /* Cached uuid */
    char           *rc_ifname;      /* Interface name associated with route */
    osn_route4_t    rc_route;       /* Route structure */
    ds_tree_t       rc_tnode;       /* Tree node */
};

static void callback_Wifi_Route_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Route_Config *old,
        struct schema_Wifi_Route_Config *new);

static bool nm2_route_cfg_parse(
        struct nm2_route_cfg *rt,
        const struct schema_Wifi_Route_Config *schema);

static void nm2_route_cfg_free(struct nm2_route_cfg *rt);
static bool nm2_route_cfg_add(struct nm2_route_cfg *rt);
static bool nm2_route_cfg_del(struct nm2_route_cfg *rt);

/* Wifi_Route_Config table object */
static ovsdb_table_t table_Wifi_Route_Config;
/* Route cache represented as red-black tree where the row UUID is the key */
static ds_tree_t g_nm2_route_cfg_list = DS_TREE_INIT(ds_str_cmp, struct nm2_route_cfg, rc_tnode);

/*
 * Initialize the route configuration subsystem
 */
void nm2_route_cfg_init()
{
    // Initialize OVSDB tables
    OVSDB_TABLE_INIT(Wifi_Route_Config, dest_addr);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(Wifi_Route_Config, false);
}

/*
 * OVSDB Wifi_Route_Config table update handler.
 */
static void callback_Wifi_Route_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Route_Config *old,
        struct schema_Wifi_Route_Config *new)
{
    struct nm2_route_cfg *rt;

    switch(mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            old = NULL;
            break;

        case OVSDB_UPDATE_DEL:
            new = NULL;
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        default:
            LOG(ERR, "route_cfg: Unknown monitor type: %d", mon->mon_type);
            return;
    }

    if (old != NULL)
    {
        /* Delete entry */
        rt = ds_tree_find(&g_nm2_route_cfg_list,  &old->_uuid.uuid);
        if (rt == NULL)
        {
            LOG(ERR, "route_cfg: Row with uuid %s not found, unable to delete.",
                    old->_uuid.uuid);
            return;
        }

        if (!nm2_route_cfg_del(rt))
        {
            LOG(ERR, "route_cfg: Error removing route "PRI_osn_ip_addr" for %s",
                    FMT_osn_ip_addr(rt->rc_route.dest),
                    rt->rc_ifname);
        }

        ds_tree_remove(&g_nm2_route_cfg_list, rt);
        nm2_route_cfg_free(rt);
        FREE(rt);
    }

    if (new != NULL)
    {
        rt = CALLOC(1, sizeof(struct nm2_route_cfg));
        if (!nm2_route_cfg_parse(rt, new))
        {
            LOG(ERR, "route_cfg: Error parsing record with UUID %s.",
                    new->_uuid.uuid);
            FREE(rt);
            return;
        }

        if (!nm2_route_cfg_add(rt))
        {
            LOG(ERR, "route_cfg: Error adding route "PRI_osn_ip_addr" for %s",
                    FMT_osn_ip_addr(rt->rc_route.dest),
                    rt->rc_ifname);
        }

        ds_tree_insert(&g_nm2_route_cfg_list, rt, rt->rc_uuid.uuid);
    }
}

/*
 * Initialize a nm2_route_cfg structure from the Wifi_Route_Config schema
 */
bool nm2_route_cfg_parse(struct nm2_route_cfg *rt, const struct schema_Wifi_Route_Config *schema)
{
    osn_ip_addr_t mask;

    if (!osn_ip_addr_from_str(&rt->rc_route.dest, schema->dest_addr))
    {
        LOG(ERR, "route_cfg: Invalid destination IP address: %s", schema->dest_addr);
        return false;
    }

    if (!osn_ip_addr_from_str(&mask, schema->dest_mask))
    {
        LOG(ERR, "route_cfg: Invalid route mask: %s", schema->dest_mask);
        return false;
    }

    rt->rc_route.dest.ia_prefix = osn_ip_addr_to_prefix(&mask);

    rt->rc_route.gw = OSN_IP_ADDR_INIT;
    rt->rc_route.gw_valid = schema->gateway_exists;

    if (schema->gateway_exists && !osn_ip_addr_from_str(&rt->rc_route.gw, schema->gateway))
    {
        LOG(ERR, "route_cfg: Invalid gateway address: %s", schema->gateway);
        return false;
    }

    rt->rc_ifname = schema->if_name_exists ? STRDUP(schema->if_name) : NULL;
    rt->rc_route.metric = schema->metric_exists ? schema->metric : -1;
    rt->rc_ifname = STRDUP(schema->if_name);
    memcpy(&rt->rc_uuid, &schema->_uuid, sizeof(rt->rc_uuid));

    return true;
}

bool nm2_route_cfg_add(struct nm2_route_cfg *rt)
{
    struct nm2_iface *pif = NULL;

    /* If the interface name is not set, this is a no-op */
    if (rt->rc_ifname == NULL) return true;

    pif = nm2_iface_get_by_name(rt->rc_ifname);
    if (pif == NULL)
    {
        /*
         * The case where the interface does not exist yet will be handled by
         * nm2_route_cfg_reapply(), so return success here.
         */
        return true;
    }

    if (rt->rc_route.metric == -1)
    {
        int rtmod = 0;
        /*
         * Assign the route metric dynamically according to the interface type.
         *
         * This is still somewhat suboptimal as interfaces with the same type
         * can still have the same route. However this case (for now) is more
         * theoretical than practical and the current approach does solve,
         * among other things, all current LTE use cases.
         */
        switch (pif->if_type)
        {
            case NM2_IFTYPE_BRIDGE:
            case NM2_IFTYPE_ETH:
            case NM2_IFTYPE_VLAN:
                rtmod = 0;
                break;

            case NM2_IFTYPE_GRE:
            case NM2_IFTYPE_GRE6:
                rtmod = 10;
                break;

            case NM2_IFTYPE_TAP:
                rtmod = 20;
                break;

            case NM2_IFTYPE_VIF:
                rtmod = 30;
                break;

            case NM2_IFTYPE_PPPOE:
                rtmod = 40;
                break;

            case NM2_IFTYPE_LTE:
                rtmod = 50;
                break;

            default:
                rtmod = 99;
                break;
        }

        rt->rc_route.metric = CONFIG_MANAGER_NM_ROUTE_BASE_METRIC + rtmod;
        LOG(INFO, "route_cfg: %s: Setting metric of route "PRI_osn_ip_addr" to %d.",
                pif->if_name,
                FMT_osn_ip_addr(rt->rc_route.dest),
                rt->rc_route.metric);
    }

    if (!inet_route4_add(pif->if_inet, &rt->rc_route))
    {
        return false;
    }

    nm2_iface_apply(pif);
    return true;
}

bool nm2_route_cfg_del(struct nm2_route_cfg *rt)
{
    struct nm2_iface *pif = NULL;

    /* If the interface name is not set, this is a no-op */
    if (rt->rc_ifname == NULL) return true;

    pif = nm2_iface_get_by_name(rt->rc_ifname);
    if (pif == NULL)
    {
        /* Interface does not exist -- nothing to do */
        return true;
    }

    if (!inet_route4_remove(pif->if_inet, &rt->rc_route))
    {
        return false;
    }

    nm2_iface_apply(pif);
    return true;
}

/*
 * Free a nm2_route_cfg structure
 */
void nm2_route_cfg_free(struct nm2_route_cfg *rt)
{
    FREE(rt->rc_ifname);
}

/*
 * Re-apply all cached routes to the interface. This is typically called
 * at interface creation.
 */
void nm2_route_cfg_reapply(struct nm2_iface *pif)
{
    struct nm2_route_cfg *rt;

    ds_tree_foreach(&g_nm2_route_cfg_list, rt)
    {
        if (strcmp(pif->if_name, rt->rc_ifname) != 0) continue;

        if (!inet_route4_add(pif->if_inet, &rt->rc_route))
        {
            LOG(ERR, "route_cfg: Error reapplying route "PRI_osn_ip_addr" for %s",
                    FMT_osn_ip_addr(rt->rc_route.dest),
                    rt->rc_ifname);
        }
    }
}
