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

#include "nm2_nb_port.h"
#include "nm2.h"
#include "nm2_nb_bridge.h"
#include "nm2_nb_interface.h"
#include "ovsdb_utils.h"
#include "inet_port.h"
#include "osn_bridge.h"

static ds_tree_t nm2_port_list = DS_TREE_INIT(ds_str_cmp, struct nm2_port, port_tnode);
static void nm2_inet_bridge_port_set(struct nm2_port *port, bool add);

static void nm2_port_free(struct nm2_port *port)
{
    FREE(port);
}

static void nm2_port_release(struct nm2_port *port)
{
    LOGT("%s(): releasing port %s", __func__, port->port_name);
    /* inform listeners that this port is being removed */
    port->port_valid = false;
    reflink_signal(&port->port_reflink);

    /* destroy the reference link */
    reflink_fini(&port->port_reflink);

    /* clean up */
    ds_tree_remove(&nm2_port_list, port);
    uuidset_fini(&port->port_interfaces);

    /* Destroy the inet object */
    LOGD("%s(): freeing port_inet %p for %s", __func__, port->port_inet, port->port_name);
    inet_del(port->port_inet);

    nm2_port_free(port);
}

ds_tree_t *nm2_port_get_list(void)
{
    return &nm2_port_list;
}

/*
 * Port reflink callback
 */
void nm2_port_ref_fn(reflink_t *obj, reflink_t *sender)
{
    struct nm2_port *port;

    TRACE();
    port = CONTAINER_OF(obj, struct nm2_port, port_reflink);

    /* Object deletion */
    if (sender == NULL)
    {
        LOGI("%s(): Port reference count of object " PRI(reflink_t) " reached 0",
             __func__, FMT(reflink_t, port->port_reflink));
        nm2_port_release(port);
        return;
    }
}

/*
 * Port.interface update; this function is called whenever a interface is added, removed
 * or modified
 */
static void nm2_port_if_update(uuidset_t *us, enum uuidset_event type, reflink_t *remote)
{
    bool add = false;
    /* Just re-apply the configuration */
    struct nm2_port *port = CONTAINER_OF(us, struct nm2_port, port_interfaces);
    struct nm2_interface *intf = CONTAINER_OF(remote, struct nm2_interface, if_reflink);

    TRACE();
    switch (type)
    {
    case UUIDSET_NEW:
        add = true;
        break;

    case UUIDSET_MOD:
        add = true;
        break;

    case UUIDSET_DEL:
        add = false;
        break;

    default:
        return;
    }

    if (add)
    {
        LOGT("%s(): interface %s is added to port %s", __func__, intf->if_name,
             port->port_name);

        /* readded port */
        nm2_inet_bridge_port_set(port, add);
    }
    else
    {
        LOGT("%s(): interface %s is removed from port %s", __func__, intf->if_name,
             port->port_name);
        nm2_inet_bridge_port_set(port, add);
    }
}

/*
 * Get a reference to the nm2_port structure associated
 * with uuid.
 */
struct nm2_port *nm2_port_get(const ovs_uuid_t *uuid)
{
    struct nm2_port *port;

    TRACE();
    port = ds_tree_find(&nm2_port_list, (void *)uuid->uuid);
    if (port) return port;

    /* port not found in list, create and add to port list*/
    port = CALLOC(1, sizeof(*port));
    if (port == NULL) return NULL;

    port->port_uuid = *uuid;

    reflink_init(&port->port_reflink, "Port");
    reflink_set_fn(&port->port_reflink, nm2_port_ref_fn);

    /* Initialize port interfaces */
    uuidset_init(&port->port_interfaces, "Port.interface", nm2_if_getref,
                 nm2_port_if_update);

    ds_tree_insert(&nm2_port_list, port, port->port_uuid.uuid);

    return port;
}

/*
 * Acquire a reflink to an Port object
 */
reflink_t *nm2_ports_getref(const ovs_uuid_t *uuid)
{
    struct nm2_port *port;

    port = nm2_port_get(uuid);
    if (port == NULL)
    {
        LOGN("%s(): Unable to acquire an Port object for UUID %s", __func__, uuid->uuid);
        return NULL;
    }

    return &port->port_reflink;
}

static struct nm2_port *nm2_port_get_by_name(const char *name)
{
    struct nm2_port *port;
    int res;

    port = ds_tree_head(&nm2_port_list);

    /* check for the port we are interested */
    while (port != NULL)
    {
        res = strcmp(port->port_name, name);
        if (res == 0)
        {
            LOGT("%s(): '%s' is present in the ports tree", __func__, name);
            return port;
        }
        port = ds_tree_next(&nm2_port_list, port);
    }

    /* port not found */
    return NULL;
}

/**
 * @brief checks for the provided key in the tree
 * and returns the value associated with the key.
 * @param tree - tree in which the key has to be searched
 * @param key - key value to search
 * @return returns the value if the key is found, else NULL
 */
static char *nm2_get_other_config_val(ds_tree_t  *tree, char *key)
{
   struct str_pair *pair;

   if (!tree) return NULL;

   pair = ds_tree_find(tree, key);
   if (!pair) return NULL;

   LOGT("%s: other_config %s:%s",__func__, key, pair->value);
   return pair->value;
}

/**
 * @brief parses Port schema for hairpin configuration.  By default,
 * it hairpin mode should be set.  It will be disabled only if
 * hairpin_mode is set to "no" in the other_config
 * @param config configuration on the Port
 * @return True is hairpin mode is enabled else false.
 */
static bool nm2_get_hairpin_mode(struct schema_Port *config)
{
    ds_tree_t *other_config;
    bool mode = false;
    char *mode_val;
    int ret;

    /* hairpin mode should be configured by default. */
    if (config->other_config_present == false) return mode;
    if (config->other_config_len == 0) return mode;

    other_config = schema2tree(sizeof(config->other_config_keys[0]),
                               sizeof(config->other_config[0]),
                               config->other_config_len,
                               config->other_config_keys,
                               config->other_config);

    /* hairpin mode should be enabled by default. If not configured
     * in other_config, it should be enabled. */
    mode_val = nm2_get_other_config_val(other_config, "hairpin_mode");
    if (mode_val == NULL) goto out;

    ret = strcmp(mode_val, "on");
    if (ret == 0) mode = true;

    LOGT("%s(): mode : %d", __func__, mode);

out:
    free_str_tree(other_config);

    return mode;
}

/*
 * Update a struct nm2_port from the schema definition, this will
 *  be called either for add or modify operations
 */
static void nm2_port_update(struct nm2_port *port, struct schema_Port *schema)
{
    inet_port_t *in_port;

    TRACE();
    /* port_valid will be true in case of update operation */
    if (port->port_valid)
    {
        /* invalidate the port config and inform the listeners */
        port->port_valid = false;
        reflink_signal(&port->port_reflink);
    }

    /* copy data from schema */
    STRSCPY(port->port_name, schema->name);
    port->port_hairpin = nm2_get_hairpin_mode(schema);

    in_port = CONTAINER_OF(port->port_inet, inet_port_t, inet);
    in_port->in_port_hairpin = port->port_hairpin;

    uuidset_set(&port->port_interfaces, schema->interfaces, schema->interfaces_len);

    /* port struct is updated, notify listeners */
    port->port_valid = true;
    reflink_signal(&port->port_reflink);
}

/******************************************************************************
 *  public api definitions
 *****************************************************************************/

void nm2_nb_port_cfg_reapply(const char *port_name)
{
    struct nm2_port *port;
    struct nm2_bridge *br;

    LOGT("%s(): reapplying port configuration for %s", __func__, port_name);
    /* check if the port is present in the port list */
    port = nm2_port_get_by_name(port_name);
    if (port == NULL) return;

    br = port->port_bridge;
    if (br == NULL)
    {
        nm2_inet_bridge_port_set(port, false);
        return;
    }

    /* port information is present in the Port config, so create the port */
    nm2_inet_bridge_port_set(port, true);
}


static void nm2_inet_bridge_port_set(struct nm2_port *port, bool add)
{
    struct nm2_bridge *br;

    br = port->port_bridge;
    if (br == NULL || br->br_inet == NULL)
    {
        LOGD("%s(): parent bridge pointer is NULL, cannot add ports to bridge", __func__);
        return;
    }
    inet_br_port_set(br->br_inet, port->port_inet, add);
    inet_commit(br->br_inet);
}

void nm2_inet_bridge_config_reapply(struct nm2_iface *pif)
{
    struct nm2_port *port;

    TRACE();
    /* If the interface is found in Ports table, that means Bridge update is received,
     * before being added Wifi_inet table.  So it needs to be configured */
    port = nm2_port_get_by_name(pif->if_name);
    if (port == NULL) return;

    nm2_inet_bridge_port_set(port, true);
}

void nm2_nb_port_process_update(ovsdb_update_monitor_t *mon, struct schema_Port *old,
                                struct schema_Port *new)
{
    struct nm2_port *port;

    LOGT("%s(): received Port table update for %s update type %d", __func__, new->name,
         mon->mon_type);

    switch (mon->mon_type)
    {
    case OVSDB_UPDATE_NEW:
        port = nm2_port_get(&new->_uuid);
        if (port == NULL) return;
        reflink_ref(&port->port_reflink, 1);
        break;

    case OVSDB_UPDATE_MODIFY:
        port = nm2_port_get(&new->_uuid);
        if (port == NULL)
        {
            LOGE("%s(): Port with uuid %s not found, cannot delete", __func__,
                 old->_uuid.uuid);
            return;
        }
        break;

    case OVSDB_UPDATE_DEL:
        port = nm2_port_get(&old->_uuid);
        if (port == NULL)
        {
            LOGE("%s(): Port with uuid %s not found, cannot delete", __func__,
                 old->_uuid.uuid);
            return;
        }
        reflink_ref(&port->port_reflink, -1);
        return;

    default:
        LOGW("%s:mon upd error: %d", __func__, mon->mon_type);
        return;
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        port->port_inet = inet_port_new(new->name);
    }

    /* will come here for only add and modify case. */
    /* update the port structure with the schema configuration */
    nm2_port_update(port, new);

}
