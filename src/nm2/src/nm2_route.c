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

#include "schema.h"
#include "log.h"
#include "nm2.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "const.h"
#include "string.h"
#include "inet.h"
#include "target.h"
#include "ds_tree.h"
#include "osn_types.h"

#define MODULE_ID LOG_MODULE_ID_MAIN

/* Cached Wifi_Route_State: */
struct nm2_route_state
{
    ovs_uuid_t                rs_uuid;                   /* Cached uuid */

    char                      rs_ifname[C_IFNAME_LEN];   /* Interface name associated with route */
    struct osn_route_status   rs_status;                 /* Route status */

    ds_tree_t                 rs_tnode;                  /* Tree node */
};

static ovsdb_table_t table_Wifi_Route_State;

static void nm2_route_update(struct schema_Wifi_Route_State *rts);
static int nm2_route_state_cmp(const struct nm2_route_state *rs_cached, const char *if_name, const struct osn_route_status *rts);
static void callback_Wifi_Route_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Route_State *old,
        struct schema_Wifi_Route_State *new);

static ds_tree_t nm2_route_state_list = DS_TREE_INIT(ds_str_cmp, struct nm2_route_state, rs_tnode);

/*
 * ===========================================================================
 *  Routing table status reporting
 * ===========================================================================
 */
bool nm2_route_init(void)
{
    /* Initialize OVSDB tables */
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Route_State);

    OVSDB_TABLE_MONITOR(Wifi_Route_State, false);

    return true;
}

/* Update OVSDB table Wifi_Route_State */
void nm2_route_update(struct schema_Wifi_Route_State *rts)
{
    json_t *where;
    json_t *cond;
    bool upsert = (rts->_update_type != OVSDB_UPDATE_DEL);

    LOG(INFO, "nm2_route: Wifi_Route_State %s: if_name=%s dest_addr=%s dest_mask=%s "
              "gateway=%s gateway_hwaddr=%s, metric=%d (table=%u)",
            upsert ? "upsert" : "delete",
            rts->if_name,
            rts->dest_addr,
            rts->dest_mask,
            rts->gateway,
            rts->gateway_hwaddr,
            rts->metric,
            rts->table);

    /* OVSDB transaction where by cached _uuid.
     *
     * An update or delete with where by cached _uuid. Alternatively we could
     * do a where by columns that determine equal routes (dest_addr, dest_mask,
     * gateway, interface, routing table), however in the schema 'table' column
     * is optional and due to a bug (or limitation) in OVSDB implementation
     * ["table", "==", ["set", []]] condition does not match.
     */
    where = json_array();
    cond = ovsdb_tran_cond_single_json("_uuid", OFUNC_EQ, ovsdb_tran_uuid_json(rts->_uuid.uuid));
    json_array_append_new(where, cond);

    if (upsert)
    {
        if (!ovsdb_table_upsert_where(&table_Wifi_Route_State, where, rts, false))
        {
            LOG(ERR, "nm2_route: Error updating Wifi_Route_State (upsert).");
        }
    }
    else
    {
        if (ovsdb_table_delete_where(&table_Wifi_Route_State, where) < 0)
        {
            LOG(ERR, "nm2_route: Error updating Wifi_Route_State (delete).");
        }

    }
}

/* Create a new Route_State cached object and add to cache. */
static struct nm2_route_state *nm2_route_state_new(const ovs_uuid_t *uuid)
{
    struct nm2_route_state *nm2_route_state;

    nm2_route_state = CALLOC(1, sizeof(*nm2_route_state));
    nm2_route_state->rs_uuid = *uuid;

    ds_tree_insert(&nm2_route_state_list, nm2_route_state, nm2_route_state->rs_uuid.uuid);

    return nm2_route_state;
}

/* Delete a Route_State cached object. */
static bool nm2_route_state_del(struct nm2_route_state *nm2_route_state)
{
    ds_tree_remove(&nm2_route_state_list, nm2_route_state);
    FREE(nm2_route_state);

    return true;
}

/* Find a Route_State cached object by uuid. */
static struct nm2_route_state *nm2_route_state_get(const ovs_uuid_t *uuid)
{
    return ds_tree_find(&nm2_route_state_list, uuid->uuid);
}

/* Cached route state comparison by route attributes that identify two routes as equal:
 *   - interface
 *   - routing table ID
 *   - destination
 *   - gateway
 *   - metric
 */
static int nm2_route_state_cmp(
        const struct nm2_route_state *rs_cached,
        const char *if_name,
        const struct osn_route_status *rts)
{
    int rc;

    rc = strcmp(rs_cached->rs_ifname, if_name);
    if (rc != 0) return rc;

    if (rs_cached->rs_status.rts_route.table < rts->rts_route.table)
    {
        return -1;
    }
    else if (rs_cached->rs_status.rts_route.table > rts->rts_route.table)
    {
        return 1;
    }

    rc = osn_ip_addr_cmp(&rs_cached->rs_status.rts_route.dest, &rts->rts_route.dest);
    if (rc != 0) return rc;

    rc = osn_ip_addr_cmp(&rs_cached->rs_status.rts_route.gw, &rts->rts_route.gw);
    if (rc != 0) return rc;

    rc = rs_cached->rs_status.rts_route.metric - rts->rts_route.metric;
    if (rc != 0) return rc;

    return 0;
}

/* Find the route in route state cache. */
static struct nm2_route_state *nm2_route_state_find_in_cache(const char *if_name, const struct osn_route_status *rts)
{
    struct nm2_route_state *rs_cached = NULL;

    ds_tree_foreach(&nm2_route_state_list, rs_cached)
    {
        if (nm2_route_state_cmp(rs_cached, if_name, rts) == 0)
        {
            return rs_cached;
        }
    }
    return NULL;
}

/* Update Route_State cached object with values from the schema structure. */
static bool nm2_route_state_update(
        struct nm2_route_state *nm2_route_state,
        const struct schema_Wifi_Route_State *schema_rts)
{
    STRSCPY(nm2_route_state->rs_ifname, schema_rts->if_name);

    osn_ip_addr_t dest_addr;
    if (!osn_ip_addr_from_str(&dest_addr, schema_rts->dest_addr))
    {
        LOG(ERR, "nm2_route: Error parsing dest_addr: %s", schema_rts->dest_addr);
        return false;
    }
    osn_ip_addr_t dest_mask;
    if (!osn_ip_addr_from_str(&dest_mask, schema_rts->dest_mask))
    {
        LOG(ERR, "nm2_route: Error parsing dest_mask: %s", schema_rts->dest_mask);
        return false;
    }
    dest_addr.ia_prefix = osn_ip_addr_to_prefix(&dest_mask);
    nm2_route_state->rs_status.rts_route.dest = dest_addr;

    if (!osn_ip_addr_from_str(&nm2_route_state->rs_status.rts_route.gw, schema_rts->gateway))
    {
        LOG(ERR, "nm2_route: Error parsing gateway address: %s", schema_rts->gateway);
        return false;
    }
    if (nm2_route_state->rs_status.rts_route.gw.ia_addr.s_addr != 0)
    {
        nm2_route_state->rs_status.rts_route.gw_valid = true;
    }
    else
    {
        nm2_route_state->rs_status.rts_route.gw_valid = false;
    }

    if (schema_rts->gateway_hwaddr_exists)
    {
        if (!osn_mac_addr_from_str(&nm2_route_state->rs_status.rts_gw_hwaddr, schema_rts->gateway_hwaddr))
        {
            LOG(ERR, "nm2_route: Error parsing gateway HW address: %s", schema_rts->gateway_hwaddr);
            return false;
        }
    }
    else
    {
        memset(&nm2_route_state->rs_status.rts_gw_hwaddr, 0, sizeof(nm2_route_state->rs_status.rts_gw_hwaddr));
    }

    if (schema_rts->pref_src_exists)
    {
        if (!osn_ip_addr_from_str(&nm2_route_state->rs_status.rts_route.pref_src, schema_rts->pref_src))
        {
            LOG(ERR, "nm2_route: Error parsing preferred source address: %s", schema_rts->pref_src);
            return false;
        }
        nm2_route_state->rs_status.rts_route.pref_src_set = true;
    }
    else
    {
        memset(&nm2_route_state->rs_status.rts_route.pref_src, 0, sizeof(nm2_route_state->rs_status.rts_route.pref_src));
        nm2_route_state->rs_status.rts_route.pref_src_set = false;
    }

    if (schema_rts->table_exists)
    {
        nm2_route_state->rs_status.rts_route.table = schema_rts->table;
    }
    else
    {
        nm2_route_state->rs_status.rts_route.table = 0;
    }

    if (schema_rts->metric_exists)
    {
        nm2_route_state->rs_status.rts_route.metric = schema_rts->metric;
    }
    else
    {
        nm2_route_state->rs_status.rts_route.metric = 0;
    }

    return true;
}

/* Route change notification callback called by inet/osn sublayers when a
 * route is added or removed. */
void nm2_route_notify(inet_t *inet, struct osn_route_status *rts, bool remove)
{
    struct schema_Wifi_Route_State schema_rts;
    struct nm2_route_state *rs_cached;

    LOG(TRACE, "nm2_route: %s: Route state notify, remove = %d", inet->in_ifname, remove);

    memset(&schema_rts, 0, sizeof(schema_rts));

    /* Check if such a route is in our cache (that is if an equal route is
     * already in Wifi_Route_State). */
    rs_cached = nm2_route_state_find_in_cache(inet->in_ifname, rts);
    if (rs_cached != NULL)
    {
        LOG(DEBUG, "nm2_route: Found Route_State in cache. uuid=%s", rs_cached->rs_uuid.uuid);

        /* Set _uuid in the schema struct to the cached uuid. This will be used
         * with update/delete where by _uuid. */
        schema_rts._uuid = rs_cached->rs_uuid;
    }

    /* Set other columns in the schema struct: */

    if (strscpy(schema_rts.if_name, inet->in_ifname, sizeof(schema_rts.if_name)) < 0)
    {
        LOG(WARN, "nm2_route: %s: Route state interface name too long.", inet->in_ifname);
        return;
    }

    osn_ip_addr_t dest_addr = { .ia_addr = rts->rts_route.dest.ia_addr, .ia_prefix = -1 };
    snprintf(schema_rts.dest_addr, sizeof(schema_rts.dest_addr), PRI_osn_ip_addr,
            FMT_osn_ip_addr(dest_addr));

    osn_ip_addr_t dest_mask = osn_ip_addr_from_prefix(rts->rts_route.dest.ia_prefix);
    snprintf(schema_rts.dest_mask, sizeof(schema_rts.dest_mask), PRI_osn_ip_addr,
            FMT_osn_ip_addr(dest_mask));

    snprintf(schema_rts.gateway, sizeof(schema_rts.gateway), PRI_osn_ip_addr,
            FMT_osn_ip_addr(rts->rts_route.gw));

    snprintf(schema_rts.gateway_hwaddr, sizeof(schema_rts.gateway_hwaddr), PRI_osn_mac_addr,
            FMT_osn_mac_addr(rts->rts_gw_hwaddr));

    schema_rts.gateway_hwaddr_exists = osn_mac_addr_cmp(&rts->rts_gw_hwaddr, &OSN_MAC_ADDR_INIT) != 0;

    if (rts->rts_route.pref_src_set)
    {
        snprintf(schema_rts.pref_src, sizeof(schema_rts.pref_src), PRI_osn_ip_addr,
                FMT_osn_ip_addr(rts->rts_route.pref_src));
        schema_rts.pref_src_exists = true;
    }

    if (rts->rts_route.table != 0)
    {
        schema_rts.table = rts->rts_route.table;
        schema_rts.table_exists = true;
    }
    else
    {
        schema_rts.table_exists = false;
    }

    if (rts->rts_route.metric > 0)
    {
        schema_rts.metric = rts->rts_route.metric;
        schema_rts.metric_exists = true;
    }
    else
    {
        schema_rts.metric_exists = false;
    }

    schema_rts._update_type = remove ? OVSDB_UPDATE_DEL : OVSDB_UPDATE_MODIFY;

    nm2_route_update(&schema_rts);
}

/*
 * OVSDB monitor update callback for Wifi_Route_State.
 *
 * Used to update the in-memory cache of Wifi_Route_State.
 */
static void callback_Wifi_Route_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Route_State *old,
        struct schema_Wifi_Route_State *new)
{
    struct nm2_route_state *route_state;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            route_state = nm2_route_state_new(&new->_uuid);
            if (route_state == NULL)
            {
                LOG(ERR, "nm2_route: Error allocating a route state cache object (uuid %s).",
                        new->_uuid.uuid);
                return;
            }
            break;

        case OVSDB_UPDATE_MODIFY:
            route_state = nm2_route_state_get(&new->_uuid);
            if (route_state == NULL)
            {
                LOG(ERR, "nm2_route: Route_State with uuid %s not found in cache. Cannot update.", new->_uuid.uuid);
                return;
            }
            break;

        case OVSDB_UPDATE_DEL:
            route_state = nm2_route_state_get(&new->_uuid);
            if (route_state == NULL)
            {
                LOG(ERR, "nm2_route: Route_State with uuid %s not found in cache. Cannot delete.", old->_uuid.uuid);
                return;
            }

            nm2_route_state_del(route_state);
            return;

        default:
            LOG(ERR, "nm2_route: Monitor update error.");
            return;
    }

    if (!nm2_route_state_update(route_state, new))
    {
        LOG(ERR, "nm2_route: Unable to parse Wifi_Route_State schema.");
    }
}
