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

#include "nm2.h"
#include "nm2_nb_bridge.h"
#include "nm2_nb_port.h"
#include "os_util.h"
#include "ovsdb_table.h"
#include "reflink.h"
#include "inet_bridge.h"

static ovsdb_table_t table_Bridge;
static ds_tree_t nm2_bridge_list = DS_TREE_INIT(ds_str_cmp, struct nm2_bridge, br_tnode);

ds_tree_t *nm2_bridge_get_list(void)
{
    return &nm2_bridge_list;
}

void nm2_bridge_init(void)
{
    LOG(INFO, "Initializing NM Bridge monitoring.");

    OVSDB_TABLE_INIT_NO_KEY(Bridge);
    OVSDB_TABLE_MONITOR(Bridge, false);
}

void nm2_bridge_del(struct nm2_bridge *br)
{
    LOGT("%s(): Releasing bridge %s", __func__, br->br_name);

    uuidset_fini(&br->br_ports);

    /* Destroy inet object */
    inet_del(br->br_inet);

    ds_tree_remove(&nm2_bridge_list, br);

    FREE(br);
}

void nm2_bridge_ref_fn(reflink_t *obj, reflink_t *sender)
{
    struct nm2_bridge *br;

    TRACE();
    br = CONTAINER_OF(obj, struct nm2_bridge, br_reflink);

    /* entry is deleted */
    if (sender == NULL)
    {
        LOGN("%s(): Reference count of object "PRI(reflink_t)" reached 0", __func__,
             FMT(reflink_t, br->br_reflink));
        nm2_bridge_del(br);
    }
}

void nm2_add_port_to_br(struct nm2_bridge *bridge, struct nm2_port *port, bool add)
{
    struct nm2_iface *piface;
    bool success;

    if (bridge == NULL || bridge->br_inet == NULL)
    {
        LOGE("%s(): bridge is NULL, cannot add port %s to bridge", __func__, port->port_name);
        return;
    }

    /* check if the interface is available on wifi_inet_config table.
     * if still not avialable, do not configure the port, wifi_inet cb
     * will configure it
     */
    piface = nm2_iface_get_by_name(port->port_name);
    /* check for interface existence only when adding.
     * for delete, the interface might already been removed from interface list.
     * so check only in case of add */
    if (add && piface == NULL)
    {
        LOGD("%s(): interface details still not available for %s, not %s port to bridge",
             __func__, port->port_name, (add == true ? "adding" : "removing"));
        return;
    }

    inet_br_port_set(bridge->br_inet, port->port_inet, add);
    success = inet_commit(bridge->br_inet);
    if (!success)
    {
        LOGD("%s(): error adding port %s to bridge %s", __func__, port->port_name, bridge->br_name);
    }
}

/*
 * Bridge.ports update; this function is called whenever a port is added, removed or
 * modified
 */
void nm2_bridge_ports_update(uuidset_t *us, enum uuidset_event type, reflink_t *remote)
{
    bool add;

    /* Just re-apply the configuration */
    struct nm2_bridge *br = CONTAINER_OF(us, struct nm2_bridge, br_ports);
    struct nm2_port *port = CONTAINER_OF(remote, struct nm2_port, port_reflink);

    LOGT("%s(): port update type %d received for bridge %s ", __func__, type,
         br->br_name);

    switch (type)
    {
    case UUIDSET_NEW:
        if (port->port_valid == false)
        {
            LOGT("%s(): port data is not ready, not creating port", __func__);
            return;
        }
        add = true;
        break;

    case UUIDSET_MOD:
        add = port->port_valid;
        break;

    case UUIDSET_DEL:
        if (port->port_valid == false)
        {
            LOGT("%s(): port data not valid not deleting port", __func__);
            return;
        }
        add = false;
        break;

    default:
        return;
    }

    if (add)
    {
        /* store the pointer to br structure, req if port has to be created from
         * wifi_inet cb */
        port->port_bridge = br;
        LOGD("%s(): adding port %s to bridge %s", __func__, port->port_name, br->br_name);
        nm2_add_port_to_br(br, port, add);
    }
    else
    {
        port->port_bridge = br;
        LOGT("%s(): deleting port %s from %s", __func__, port->port_name, br->br_name);
        nm2_add_port_to_br(br, port, add);
    }
}

/*
 * Update a struct nm2_bridge from the schema
 */
void nm2_bridge_update(struct nm2_bridge *br, struct schema_Bridge *schema)
{
    TRACE();
    STRSCPY(br->br_name, schema->name);
    uuidset_set(&br->br_ports, schema->ports, schema->ports_len);
}

static struct nm2_bridge *nm2_bridge_get(ovs_uuid_t *uuid)
{
    struct nm2_bridge *br;

    br = ds_tree_find(&nm2_bridge_list, uuid->uuid);
    if (br) return br;

    br = CALLOC(1, sizeof(*br));
    if (br == NULL) return NULL;

    br->br_uuid = *uuid;

    reflink_init(&br->br_reflink, "Bridge");
    reflink_set_fn(&br->br_reflink, nm2_bridge_ref_fn);

    /* Initialize bridge ports */
    uuidset_init(&br->br_ports, "Bridge.ports", nm2_ports_getref,
                 nm2_bridge_ports_update);

    ds_tree_insert(&nm2_bridge_list, br, br->br_uuid.uuid);

    return br;
}

void callback_Bridge(ovsdb_update_monitor_t *mon, struct schema_Bridge *old_rec,
                     struct schema_Bridge *new)
{
    struct nm2_bridge *br;

    LOGT("%s(): bridge table update callback invoked, update type %d", __func__,
         mon->mon_type);
    switch (mon->mon_type)
    {
    case OVSDB_UPDATE_NEW:
        br = nm2_bridge_get(&new->_uuid);
        if (br == NULL) return;
        reflink_ref(&br->br_reflink, 1);
        break;

    case OVSDB_UPDATE_MODIFY:
        br = nm2_bridge_get(&new->_uuid);
        if (br == NULL)
        {
            LOGE("%s(): bridge with uuid %s not found, cannot update", __func__,
                 new->_uuid.uuid);
            return;
        }
        break;

    case OVSDB_UPDATE_DEL:
        br = nm2_bridge_get(&old_rec->_uuid);
        if (br == NULL)
        {
            LOGE("%s(): bridge with uuid %s not found, cannot delete", __func__,
                 old_rec->_uuid.uuid);
            return;
        }

        /* Decrease the reference count */
        reflink_ref(&br->br_reflink, -1);
        return;

    default:
        LOG(ERR, "%s(): invalid action type", __func__);
        return;
    }

    /* create the bridge for insert operation.  Bridge is deleted when del
     * operation in reflink callback. For modify no action is required */

    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        STRSCPY(br->br_name, new->name);
        LOGD("%s(): creating bridge %s", __func__, br->br_name);
        br->br_inet = inet_bridge_new(br->br_name);
        if (br->br_inet == NULL)
        {
            LOGE("%s(): failed to initialize inet bridge object", __func__);
            FREE(br);
            return;
        }
        inet_br_set(br->br_inet, br->br_name, true);
        inet_commit(br->br_inet);
    }

    nm2_bridge_update(br, new);
}
