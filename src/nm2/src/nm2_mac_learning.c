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

#include <stdlib.h>
#include <sys/wait.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <inttypes.h>
#include <jansson.h>

#include "const.h"
#include "json_util.h"
#include "schema.h"
#include "log.h"
#include "nm2.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "ds_tree.h"
#include "synclist.h"
#include "target.h"

#include "nm2_iface.h"

// Defines
#define MODULE_ID LOG_MODULE_ID_MAIN

// OVSDB constants
#define OVSDB_MAC_TABLE            "OVS_MAC_Learning"

static bool                         g_mac_learning_init = false;

/*
 * Local cache of OVS mac entries
 */
struct nm2_mac_entry
{
    osn_mac_addr_t      me_mac;
    char                me_brname[16];
    char                me_ifname[16];
    bool                me_active;      /* True if entry is present in ovsdb */
    ds_tree_node_t      me_tnode;
    synclist_node_t     me_slnode;
};

/*
 * List of interfaces with MAC reporting enabled
 */
struct nm2_mac_iface
{
    char                mi_iface[C_IFNAME_LEN];     /* Interface name */
    ds_tree_node_t      mi_tnode;
};

static void nm2_mac_refresh(void);
static int nm2_mac_entry_cmp(void *a, void *b);
static bool nm2_mac_update(struct schema_OVS_MAC_Learning *omac, bool oper_status);
static void nm2_mac_entry_to_schema(struct schema_OVS_MAC_Learning *schema, struct nm2_mac_entry *me);
static synclist_fn_t nm2_mac_entry_sync_fn;

static ds_tree_t nm2_mac_list = DS_TREE_INIT(
        nm2_mac_entry_cmp,
        struct nm2_mac_entry,
        me_tnode);

static synclist_t nm2_mac_synclist = SYNCLIST_INIT(
        nm2_mac_entry_cmp,
        struct nm2_mac_entry,
        me_slnode,
        nm2_mac_entry_sync_fn);

static ds_tree_t nm2_mac_iface_list = DS_TREE_INIT(
        ds_str_cmp,
        struct nm2_mac_iface,
        mi_tnode);

/*
 * ===========================================================================
 *  Public API -- enable or disable MAC reporting on the interface
 * ===========================================================================
 */
void nm2_mac_reporting_set(const char *ifname, bool enable)
{
    struct nm2_mac_iface *mi;

    mi = ds_tree_find(&nm2_mac_iface_list, (void *)ifname);
    if (enable && mi == NULL)
    {
        /* Insert interface to the list of interface eligible for MAC reporting */
        mi = calloc(1, sizeof(struct nm2_mac_iface));
        STRSCPY(mi->mi_iface, ifname);
        ds_tree_insert(&nm2_mac_iface_list, mi, mi->mi_iface);
        nm2_mac_refresh();
    }
    else if (!enable && mi != NULL)
    {
        /* Remove interface from MAC reporting list */
        ds_tree_remove(&nm2_mac_iface_list, mi);
        free(mi);
        nm2_mac_refresh();
    }
}

/******************************************************************************
 *  PROTECTED definitions
 *****************************************************************************/
static bool
nm2_mac_learning_update(struct schema_OVS_MAC_Learning *omac, bool oper_status)
{
    pjs_errmsg_t        perr;
    json_t             *where, *row;
    bool                ret;

    /* Skip deleting the empty entries at startup */
    if(!g_mac_learning_init && (strlen(omac->hwaddr) == 0))
    {
        return true;
    }

    // force lower case mac
    str_tolower(omac->hwaddr);
    LOGT("Updating MAC learning '%s' %d", omac->hwaddr, oper_status);

    if (oper_status == false)
    {
        where = ovsdb_tran_cond(OCLM_STR, "hwaddr", OFUNC_EQ, omac->hwaddr);
        ret = ovsdb_sync_delete_where(OVSDB_MAC_TABLE, where);
        if (!ret)
        {
            LOGE("Updating MAC learning %s (Failed to remove entry)",
                        omac->hwaddr);
            return false;
        }
        LOGD("Removed MAC learning '%s' with '%s' '%s'",
                        omac->hwaddr, omac->brname, omac->ifname);

        // Update the eth_devices tag in OpenFlow_Tag
        if (lan_clients_oftag_remove_mac(omac->hwaddr) == -1)
            LOGE("Updating OpenFlow_Tag %s (Failed to remove entry)",
                   omac->hwaddr);
    }
    else
    {
        where = ovsdb_tran_cond(OCLM_STR, "hwaddr", OFUNC_EQ, omac->hwaddr);
        row   = schema_OVS_MAC_Learning_to_json(omac, perr);
        ret = ovsdb_sync_upsert_where(OVSDB_MAC_TABLE, where, row, NULL);
        if (!ret)
        {
            LOGE("Updating MAC learning %s (Failed to insert entry)",
                        omac->hwaddr);
            return false;
        }
        LOGD("Updated MAC learning '%s' with '%s' '%s'",
                        omac->hwaddr, omac->brname, omac->ifname);

        // Update the eth_devices tag in OpenFlow_Tag
        if (lan_clients_oftag_add_mac(omac->hwaddr) == -1)
            LOGE("Updating OpenFlow_Tag %s (Failed to insert entry)",
                   omac->hwaddr);
    }

    return true;
}


/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/
bool
nm2_mac_learning_init(void)
{
    bool         ret;

    /* Register to MAC learning changed ... */
    ret = target_mac_learning_register(nm2_mac_update);
    if (false == ret)
    {
        return false;
    }

    g_mac_learning_init = true;

    return true;
}

/*
 * Synchronize the internal MAC list with the synclist -- this is the place
 * where one should add filters
 */
void nm2_mac_refresh(void)
{
    struct nm2_mac_entry *pme;

    synclist_begin(&nm2_mac_synclist);

    ds_tree_foreach(&nm2_mac_list, pme)
    {
        /* Skip non-registered interfaces */
        if (ds_tree_find(&nm2_mac_iface_list, pme->me_ifname) == NULL)
        {
            continue;
        }

        synclist_add(&nm2_mac_synclist, pme);
    }

    synclist_end(&nm2_mac_synclist);
}

bool nm2_mac_update(struct schema_OVS_MAC_Learning *mac, bool add)
{
    struct nm2_mac_entry kme;
    struct nm2_mac_entry *pme;

    if (strlen(mac->hwaddr) == 0) return true;

    memset(&kme, 0, sizeof(kme));
    STRSCPY(kme.me_brname, mac->brname);
    STRSCPY(kme.me_ifname, mac->ifname);
    if (!osn_mac_addr_from_str(&kme.me_mac, mac->hwaddr))
    {
        LOG(WARN, "Error updating MAC entry, invalid MAC address: %s (brname:%s ifname%s)",
                mac->hwaddr,
                mac->brname,
                mac->ifname);
        return true;
    }

    /* Find an existing entry */
    pme = ds_tree_find(&nm2_mac_list, &kme);

    /* Handle the add case first since its the simplest */
    if (add)
    {
        if (pme != NULL)
        {
            LOG(WARN, "nm2_mac: MAC entry already exists, unable to add: %s",
                    mac->hwaddr);
            return true;
        }

        pme = calloc(1, sizeof(struct nm2_mac_entry));
        memcpy(pme, &kme, sizeof(*pme));
        ds_tree_insert(&nm2_mac_list, pme, pme);
        nm2_mac_refresh();

        return true;
    }

    if (pme == NULL)
    {
        LOG(WARN, "nm2_mac: MAC entry doesn't exist, unable to delete: %s",
                mac->hwaddr);
        return true;
    }

    if (pme->me_active)
    {
        synclist_del(&nm2_mac_synclist, pme);
    }

    ds_tree_remove(&nm2_mac_list, pme);
    free(pme);

    return true;
}

/*
 * Synclist synchronisation function
 */
void *nm2_mac_entry_sync_fn(synclist_t *list, void *_old, void *_new)
{
    (void)list;

    struct nm2_mac_entry *old = _old;
    struct nm2_mac_entry *new = _new;

    struct schema_OVS_MAC_Learning schema;

    /* Add */
    if (old == NULL)
    {
        nm2_mac_entry_to_schema(&schema, new);
        nm2_mac_learning_update(&schema, true);
        new->me_active = true;
        return new;
    }
    /* Remove */
    else if (new == NULL)
    {
        nm2_mac_entry_to_schema(&schema, old);
        nm2_mac_learning_update(&schema, false);
        old->me_active = false;
        return NULL;
    }

    return old;
}

/*
 * Convert a nm2_mac_entry structure to a schema_OVS_MAC_Learning structure
 */
void nm2_mac_entry_to_schema(
        struct schema_OVS_MAC_Learning *schema,
        struct nm2_mac_entry *me)
{
    char mac[sizeof(schema->hwaddr)];

    memset(schema, 0, sizeof(*schema));

    SCHEMA_SET_STR(schema->brname, me->me_brname);
    SCHEMA_SET_STR(schema->ifname, me->me_ifname);

    snprintf(
            mac,
            sizeof(mac),
            PRI_osn_mac_addr,
            FMT_osn_mac_addr(me->me_mac));
    SCHEMA_SET_STR(schema->hwaddr, mac);
}

/*
 * nm2_mac_entry comparator: first sort by bridge name, then by interface name
 * and finally by the MAC address
 */
int nm2_mac_entry_cmp(void *_a, void *_b)
{
    int rc;

    struct nm2_mac_entry *a = _a;
    struct nm2_mac_entry *b = _b;

    rc = strcmp(a->me_ifname, b->me_ifname);
    if (rc != 0) return rc;

    rc = strcmp(a->me_brname, b->me_brname);
    if (rc != 0) return rc;

    return osn_mac_addr_cmp(&a->me_mac, &b->me_mac);
}
