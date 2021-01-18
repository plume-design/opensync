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

#include "const.h"
#include "ds_tree.h"
#include "ovsdb_table.h"

#include "nm2.h"

/*
 * ===========================================================================
 *  Module for keeping track of uplink interfaces.
 *  An uplink interface is either a:
 *      - WAN interface
 *      - backhaul wifi interface
 *      - backhaul ethernet interface
 *
 * To determine this, we need to scan the Connection_Manager_Uplink table. This
 * table is updated by CM and WANO.
 * ===========================================================================
 */

/*
 * List of interfaces categorized as uplink
 */
struct nm2_cmu_uplink
{
    char        cu_ifname[C_IFNAME_LEN];    /* Interface name */
    char        cu_bridge[C_IFNAME_LEN];    /* The bridge of this interface */
    bool        cu_is_uplink;               /* True if interface is an uplink */
    bool        cu_is_wan;                  /* True if interface is WAN */
    ds_tree_t   cu_tnode;                   /* Tree node */
};

static void callback_Connection_Manager_Uplink(
        ovsdb_update_monitor_t *mon,
        struct schema_Connection_Manager_Uplink *old,
        struct schema_Connection_Manager_Uplink *new);

static void nm2_cmu_dispatch(const char *ifname, bool is_uplink, bool is_wan, const char *bridge);

static ovsdb_table_t table_Connection_Manager_Uplink;
static ds_tree_t nm2_cmu_uplink_list = DS_TREE_INIT(ds_str_cmp, struct nm2_cmu_uplink, cu_tnode);

void nm2_cmu_init(void)
{
    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);
    OVSDB_TABLE_MONITOR(Connection_Manager_Uplink, false);
}

void callback_Connection_Manager_Uplink(
        ovsdb_update_monitor_t *mon,
        struct schema_Connection_Manager_Uplink *old,
        struct schema_Connection_Manager_Uplink *new)
{
    struct schema_Connection_Manager_Uplink *rec;
    const char *bridge;
    struct nm2_cmu_uplink *cu;
    bool is_uplink;
    bool is_wan;

    rec = (mon->mon_type == OVSDB_UPDATE_DEL) ? old : new;
    cu = ds_tree_find(&nm2_cmu_uplink_list, rec->if_name);

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            break;

        case OVSDB_UPDATE_DEL:
            if (cu == NULL) return;

            /*
             * If either cu_is_wan or cu_is_uplink is true, it means that an
             * event was dispatched before. In that case, dispatch an event
             * that turns both of these off.
             */
            if (cu->cu_is_wan || cu->cu_is_uplink)
            {
                nm2_cmu_dispatch(cu->cu_ifname, false, false, cu->cu_bridge[0] == '\0' ? NULL : cu->cu_bridge);
            }

            ds_tree_remove(&nm2_cmu_uplink_list, cu);
            free(cu);
            return;

        default:
            LOG(ERR, "cmu: Connection_Manager_Uplink monitor update error.");
            return;
    }

    if (cu == NULL)
    {
        cu = calloc(1, sizeof(struct nm2_cmu_uplink));
        STRSCPY(cu->cu_ifname, new->if_name);
        ds_tree_insert(&nm2_cmu_uplink_list, cu, cu->cu_ifname);
    }

    if (new->is_used_exists && new->is_used && new->has_L3 && new->has_L2)
    {
        is_uplink = true;
        is_wan = !new->bridge_exists;
    }
    else
    {
        is_uplink = false;
        is_wan = false;
    }

    bridge = new->bridge_exists ? new->bridge : "";

    if (cu->cu_is_uplink != is_uplink || cu->cu_is_wan != is_wan || strcmp(cu->cu_bridge, bridge) != 0)
    {
        nm2_cmu_dispatch(cu->cu_ifname, is_uplink, is_wan, bridge[0] == '\0' ? NULL : bridge);
    }

    cu->cu_is_uplink = is_uplink;
    cu->cu_is_wan = is_wan;
    STRSCPY(cu->cu_bridge, bridge);

    return;
}

void nm2_cmu_dispatch(const char *ifname, bool is_uplink, bool is_wan, const char *bridge)
{
    nm2_mcast_uplink_notify(ifname, is_uplink, is_wan, bridge);
}

