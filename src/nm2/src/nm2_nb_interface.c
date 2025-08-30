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

#include "nm2_nb_interface.h"
#include "nm2_nb_port.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "os_util.h"
#include "reflink.h"
#include "nm2.h"

static ovsdb_table_t table_Interface;

static ds_tree_t nm2_if_list = DS_TREE_INIT(ds_str_cmp, struct nm2_interface, if_tnode);

static void nm2_if_free(struct nm2_interface *intf)
{
    FREE(intf);
}

static void nm2_if_release(struct nm2_interface *intf)
{
    LOGT("%s(): releasing interface %s", __func__, intf->if_name);

    /* inform listeners that the interface is being removed */
    reflink_signal(&intf->if_reflink);

    osn_netif_del(intf->if_netif);

    /* clean up*/
    ds_tree_remove(&nm2_if_list, intf);
    nm2_if_free(intf);
}

/*
 * Interface reflink callback
 */
void nm2_if_ref_fn(reflink_t *obj, reflink_t *sender)
{
    struct nm2_interface *intf;

    TRACE();
    intf = CONTAINER_OF(obj, struct nm2_interface, if_reflink);
    /* Object deletion */
    if (sender == NULL)
    {
        LOGI("%s(): Interface reference count of object " PRI(reflink_t) " reached 0",
             __func__, FMT(reflink_t, intf->if_reflink));
        nm2_if_release(intf);
        return;
    }
}

struct nm2_interface *nm2_if_lookup(const char *ifname)
{
    struct nm2_interface *intf;
    int cmp;

    intf = ds_tree_head(&nm2_if_list);
    while (intf != NULL)
    {
        cmp = strcmp(intf->if_name, ifname);
        if (cmp == 0) return intf;

        intf = ds_tree_next(&nm2_if_list, intf);
    }

    return NULL;
}

static void nm2_if_config_apply(struct nm2_interface *intf)
{
    osn_mac_addr_t hwaddr;
    int rc = 0;

    /* Check if MAC address needs to be configured.
     * OSN layer does not configure the mac address if set to all zeros */
    if (intf->if_mac_exists) rc = osn_mac_addr_cmp(&intf->if_mac_w, &intf->if_mac_r);
    hwaddr = (rc == 0) ? OSN_MAC_ADDR_INIT : intf->if_mac_w;

    LOGT("%s(): configuring interface %s with mac " PRI_osn_mac_addr "", __func__, intf->if_name, FMT_osn_mac_addr(hwaddr));
    osn_netif_hwaddr_set(intf->if_netif, hwaddr);
    rc = osn_netif_apply(intf->if_netif);
    if (rc == false)
    {
        LOGN("%s(): error updating config for %s", __func__, intf->if_name);
        return;
    }
}

static bool nm2_if_set_runtime_state(struct osn_netif_status *status)
{
    struct schema_Interface intf;
    char hwaddr_s[64];
    int ret;
    char *filter[] = {"+",
                      SCHEMA_COLUMN(Interface, admin_state),
                      SCHEMA_COLUMN(Interface, mtu),
                      SCHEMA_COLUMN(Interface, ifindex),
                      SCHEMA_COLUMN(Interface, mac_in_use),
                      NULL};


    memset(&intf, 0, sizeof(intf));

    SCHEMA_SET_STR(intf.admin_state, status->ns_up ? "up" : "down");
    SCHEMA_SET_INT(intf.mtu, status->ns_mtu);
    SCHEMA_SET_INT(intf.ifindex, status->ns_index);
    snprintf(hwaddr_s, sizeof(hwaddr_s), PRI(os_macaddr_t), FMT_osn_mac_addr(status->ns_hwaddr));
    SCHEMA_SET_STR(intf.mac_in_use, hwaddr_s);

    LOGT("%s(): updating runtime status for %s (%s)", __func__, status->ns_ifname, hwaddr_s);
    ret = ovsdb_table_update_where_f(
        &table_Interface, ovsdb_where_simple(SCHEMA_COLUMN(Interface, name), status->ns_ifname),
        &intf, filter);

    return ret == 1;
}

static void nm2_if_netif_status_fn(osn_netif_t *netif, struct osn_netif_status *status)
{
    struct nm2_interface *intf;

    LOGT("%s(): received interface status for '%s' mac " PRI_osn_mac_addr " ", __func__,
         status->ns_ifname, FMT_osn_mac_addr(status->ns_hwaddr));

    intf = nm2_if_lookup(status->ns_ifname);
    if (intf == NULL) return;

    /* update the hardware address */
    intf->if_mac_r = status->ns_hwaddr;
    nm2_if_set_runtime_state(status);

    nm2_if_config_apply(intf);

    nm2_nb_port_cfg_reapply(status->ns_ifname);
}

struct nm2_interface *nm2_if_get_from_uuid(const ovs_uuid_t *uuid)
{
    return ds_tree_find(&nm2_if_list, (void *)uuid->uuid);
}

struct nm2_interface *nm2_if_get(const ovs_uuid_t *uuid)
{
    struct nm2_interface *intf;

    intf = nm2_if_get_from_uuid(uuid);
    if (intf) return intf;

    intf = CALLOC(1, sizeof(*intf));
    if (intf == NULL) return NULL;

    intf->if_uuid = *uuid;
    reflink_init(&intf->if_reflink, "Interface");
    reflink_set_fn(&intf->if_reflink, nm2_if_ref_fn);

    ds_tree_insert(&nm2_if_list, intf, intf->if_uuid.uuid);
    return intf;
}

/******************************************************************************
 *  public api definitions
 *****************************************************************************/
/*
 * Acquire a reflink to an Interface object
 */
reflink_t *nm2_if_getref(const ovs_uuid_t *uuid)
{
    struct nm2_interface *intf;

    intf = nm2_if_get(uuid);
    if (intf == NULL)
    {
        LOGN("%s(): Unable to acquire an Interface object for UUID %s", __func__,
             uuid->uuid);
        return NULL;
    }
    return &intf->if_reflink;
}

ds_tree_t *nm2_if_get_list(void)
{
    return &nm2_if_list;
}

void nm2_if_update_mac(struct nm2_interface *intf, const char *config_mac)
{
    intf->if_mac_exists = true;
    osn_mac_addr_from_str(&intf->if_mac_w, config_mac);
}

void nm2_if_update_netif_obj(struct nm2_interface *intf, char *if_name)
{
    intf->if_netif = osn_netif_new(if_name);
    if (intf->if_netif == NULL)
    {
        LOGW("%s(): error creating netif object for %s", __func__, if_name);
        return;
    }

    /* register for notifying us when interface config changes */
    osn_netif_status_notify(intf->if_netif, nm2_if_netif_status_fn);
}

void nm2_if_update(struct nm2_interface *intf, struct schema_Interface *schema)
{
    /* copy data from schema */
    STRSCPY(intf->if_name, schema->name);
    if (schema->mac_exists) nm2_if_update_mac(intf, schema->mac);
    if (intf->if_netif == NULL) nm2_if_update_netif_obj(intf, schema->name);

    /* interface struct is updated, notify listeners */
    reflink_signal(&intf->if_reflink);
}

void callback_Interface(ovsdb_update_monitor_t *mon, struct schema_Interface *old_rec,
                        struct schema_Interface *new)
{
    struct nm2_interface *intf;
    LOGT("%s(): received Interface table update, type %d", __func__, mon->mon_type);

    switch (mon->mon_type)
    {
    case OVSDB_UPDATE_NEW:
        intf = nm2_if_get(&new->_uuid);
        if (intf == NULL) return;
        reflink_ref(&intf->if_reflink, 1);
        break;

    case OVSDB_UPDATE_MODIFY:
        intf = nm2_if_get(&new->_uuid);
        if (intf == NULL) return;
        break;

    case OVSDB_UPDATE_DEL:
        intf = nm2_if_get(&old_rec->_uuid);
        if (intf == NULL) return;
        reflink_ref(&intf->if_reflink, -1);
        return;

    default:
        LOGE("%s:invalid action %d", __func__, mon->mon_type);
        return;
    }

    nm2_if_update(intf, new);

    /* apply interface config on the device using osn layer */
    nm2_if_config_apply(intf);
}

/*
 * Initialize table monitors
 */
bool nm2_if_init(void)
{
    LOG(INFO, "Initializing NM Interface monitoring.");
    OVSDB_TABLE_INIT_NO_KEY(Interface);
    OVSDB_TABLE_MONITOR(Interface, false);

    return true;
}
