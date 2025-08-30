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

#include <stdint.h>
#include <stdbool.h>

#include <string.h>
#include "ds_dlist.h"
#include "ds_tree.h"
#include "os_types.h"
#include "ovsdb_utils.h"
#include "os.h"
#include "os_nif.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_utils.h"
#include "schema.h"
#include "log.h"
#include "policy_tags.h"
#include "memutil.h"
#include "network_zone.h"
#include "network_zone_internals.h"


static char tag_marker[2] = "${";
static char gtag_marker[2] = "$[";


static struct network_zone_mgr zone_mgr =
{
    .initialized = false,
};


static ovsdb_table_t table_Network_Zone;



struct network_zone_mgr *
network_zone_get_mgr(void)
{
    return &zone_mgr;
}

static void
network_zone_log(struct nz_cache_entry *nz)
{
    size_t i;

    if (!LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE)) return;

    if (nz == NULL)
    {
        LOGT("%s: network zone is null", __func__);
        return;
    }

    LOGT("%s: network zone name: %s, priority: %d", __func__, nz->name, nz->priority);
    if (nz->device_tags == NULL)
    {
        LOGT("%s: no device tags for network zone %s", __func__, nz->name);
        return;
    }
    LOGT("%s: device tags for network zone %s:", __func__, nz->name);
    for (i = 0; i < nz->device_tags->nelems; i++)
    {
        LOGT("%s: %s", nz->name, nz->device_tags->array[i]);
    }
}


bool
network_zone_tag_update_cb(om_tag_t *tag,
                           struct ds_tree *removed,
                           struct ds_tree *added,
                           struct ds_tree *updated)
{
    struct nz_cache_entry *nz_entry;
    struct nz_tag_entry *tag_entry;
    struct network_zone_mgr *mgr;
    struct network_zone_mac mac;
    om_tag_list_entry_t *item;

    mgr = network_zone_get_mgr();
    /* Check if we care about this tag */
    tag_entry = ds_tree_find(&mgr->tags_to_monitor, tag->name);
    if (tag_entry == NULL) return false;

    /* Get the zone this tag belongs to */
    nz_entry = ds_tree_find(&mgr->nz_cache, tag_entry->zone_name);
    if (nz_entry == NULL) return false;

    if (added != NULL)
    {
        ds_tree_foreach(added, item)
        {
            network_zone_get_mac(nz_entry, item->value, &mac);
            network_zone_add_mac(&mac);
        }
    }
    if (removed != NULL)
    {
        ds_tree_foreach(removed, item)
        {
            network_zone_get_mac(nz_entry, item->value, &mac);
            network_zone_delete_mac(&mac);
        }
    }
    return true;
}


/**
 * @brief determine the type and value of network zone mac element
 *
 * @param nz_elem the ovsdb mac element
 * @param mac the structure to be filled
 */
void
network_zone_get_mac(struct nz_cache_entry *nz,
                     char *nz_elem, struct network_zone_mac *mac)
{
    bool tag_has_marker;
    bool is_gtag;
    bool is_tag;
    char *tag_s;

    is_gtag = !strncmp(nz_elem, gtag_marker, sizeof(gtag_marker));
    is_tag = !strncmp(nz_elem, tag_marker, sizeof(tag_marker));

    if (is_gtag || is_tag)
    {
        tag_s = nz_elem + 2; /* pass tag marker */
        /* pass tag values marker */
        tag_has_marker = (*tag_s == TEMPLATE_DEVICE_CHAR);
        tag_has_marker |= (*tag_s == TEMPLATE_CLOUD_CHAR);
        tag_has_marker |= (*tag_s == TEMPLATE_LOCAL_CHAR);
        if (tag_has_marker) tag_s += 1;

        /* Copy tag name, remove end marker */
        STRSCPY_LEN(mac->val , tag_s, -1);
        mac->type = (is_gtag ? NZ_GTAG : NZ_TAG);
    }
    else
    {
        tag_s = nz_elem;
        STRSCPY(mac->val , tag_s);
        mac->type = NZ_MAC;
    }

    mac->priority = nz->priority;
    STRSCPY(mac->nz_name, nz->name);
    mac->ovsdb_tag_string = nz_elem;
}


static bool
network_zone_check_conversion(void *converted, int len)
{
    if (len == 0) return true;
    if (converted == NULL) return false;

    return true;
}


static void
network_zone_delete_device_entry(struct nz_device_entry *entry)
{
    struct nz_device_zone *zone;
    ds_dlist_t *zones;

    zones = &entry->zones;
    zone = ds_dlist_head(zones);
    while (zone != NULL)
    {
        struct nz_device_zone *to_remove;
        struct nz_device_zone *next;

        next = ds_dlist_next(zones, zone);
        to_remove = zone;
        ds_dlist_remove(zones, to_remove);
        FREE(to_remove);
        zone = next;
    }

    FREE(entry);
}


void
network_zone_delete_mac_entry(struct nz_device_entry *entry,
                              struct network_zone_mac *mac)
{
    struct nz_device_zone *zone;
    ds_dlist_t *zones;

    zones = &entry->zones;
    zone = ds_dlist_head(zones);
    while (zone != NULL)
    {
        int rc;

        rc = strcmp(zone->name, mac->nz_name);
        if (rc == 0)
        {
            ds_dlist_remove(zones, zone);
            FREE(zone);
            return;
        }
        zone = ds_dlist_next(zones, zone);
    }
}


void
network_zone_delete_tag(struct network_zone_mac *tag)
{
    om_tag_list_entry_t *tag_item;
    struct network_zone_mgr *mgr;
    struct nz_tag_entry *entry;
    om_tag_t *ovsdb_tag;
    ds_tree_t *tree;
    int tag_type;

    /* Safety checks */
    if (tag == NULL) return;
    if (tag->type == NZ_MAC) return;

    /* Look up the tag */
    mgr = network_zone_get_mgr();
    entry = ds_tree_find(&mgr->tags_to_monitor, tag->val);
    if (entry == NULL)
    {
        LOGD("%s: tag %s from zone %s not found in the monitor list", __func__,
             tag->val, tag->nz_name);
        return;
    }

    /* Get the tag type */
    tag_type = om_get_type_of_tag(tag->ovsdb_tag_string);
    if (tag_type == -1) return;

    if (tag_type == OM_TLE_FLAG_NONE) tag_type = 0;

    ds_tree_remove(&mgr->tags_to_monitor, entry);
    FREE(entry);

    /* Process the tag content */
    ovsdb_tag = om_tag_find(tag->ovsdb_tag_string);

    /* The tag might not be yet configured */
    if (ovsdb_tag == NULL) return;

    /* Register all values */
    tree = &ovsdb_tag->values;
    ds_tree_foreach(tree, tag_item)
    {
        struct network_zone_mac mac_item;

        /* Check for matching type */
        if (tag_type && !(tag_item->flags & tag_type)) continue;

        STRSCPY(mac_item.val, tag_item->value);
        STRSCPY(mac_item.nz_name, tag->nz_name);
        mac_item.ovsdb_tag_string = tag_item->value;
        mac_item.priority = tag->priority;
        mac_item.type = NZ_MAC;
        network_zone_delete_mac(&mac_item);
    }

    return;
}


void
network_zone_delete_mac(struct network_zone_mac *mac)
{
    struct nz_device_entry *entry;
    struct network_zone_mgr *mgr;
    struct nz_device_zone *zone;
    os_macaddr_t lookup_mac;
    ds_dlist_t *zones;
    bool ret;

    /* Safety checks */
    if (mac == NULL) return;
    if (mac->type != NZ_MAC) return;

    /* Convert the mac string to a byte array for lookup purposes */
    ret = os_nif_macaddr_from_str(&lookup_mac, mac->val);
    if (ret == false) return;

    /* Lookup the device */
    mgr = network_zone_get_mgr();
    entry = ds_tree_find(&mgr->device_cache, &lookup_mac);
    if (entry == NULL) return;

    /* the entry was found, delete it */
    network_zone_delete_mac_entry(entry, mac);

    /* Delete the device entry if it has no more zones */

    zones = &entry->zones;
    zone = ds_dlist_head(zones);
    if (zone == NULL)
    {
        ds_tree_remove(&mgr->device_cache, entry);
        FREE(entry);
    }
}


static void
network_zone_delete_zone_entry(struct nz_cache_entry *nz)
{
    struct network_zone_mac mac;
    struct str_set *macs;
    size_t i;

    /* Sanity checks */
    if (nz == NULL) return;
    if (nz->device_tags ==  NULL) return;

    macs = nz->device_tags;
    for (i = 0; i < macs->nelems; i++)
    {
        network_zone_get_mac(nz, macs->array[i], &mac);
        switch (mac.type)
        {
            case NZ_GTAG:
            case NZ_TAG:
                network_zone_delete_tag(&mac);
                break;

            case NZ_MAC:
                network_zone_delete_mac(&mac);
                break;

            default:
                LOGD("%s: unexpected type %d for elem %s", __func__,
                     mac.type, mac.val);
                break;
        }
    }

    free_str_set(nz->device_tags);
    FREE(nz->name);
    FREE(nz);
}


bool
network_zone_add_device_zone(struct nz_device_entry *entry,
                             struct network_zone_mac *mac)
{
    struct nz_device_zone *zone_to_add;
    struct nz_device_zone *zone_iter;
    ds_dlist_t *device_zones;

    zone_to_add = CALLOC(1, sizeof(*zone_to_add));
    if (zone_to_add == NULL) return false;

    /* Fill the new zone entry */
    STRSCPY(zone_to_add->name, mac->nz_name);
    zone_to_add->priority = mac->priority;

    /* Insert the zone entry in a list sorted by the zone priority */
    device_zones = &entry->zones;
    zone_iter = ds_dlist_head(device_zones);

    while (zone_iter != NULL)
    {
        if (zone_to_add->priority >= zone_iter->priority)
        {
            ds_dlist_insert_before(device_zones, zone_iter, zone_to_add);
            return true;
        }
        zone_iter = ds_dlist_next(device_zones, zone_iter);
    }

    ds_dlist_insert_tail(device_zones, zone_to_add);

    return true;
}


bool
network_zone_add_tag(struct network_zone_mac *tag)
{
    om_tag_list_entry_t *tag_item;
    struct network_zone_mgr *mgr;
    struct nz_tag_entry *entry;
    om_tag_t *ovsdb_tag;
    ds_tree_t *tree;
    int tag_type;

    LOGD("%s:%d: mac value: %s (ovsdb: %s), type %d", __func__, __LINE__,
         tag->val, tag->ovsdb_tag_string, tag->type);
    /* Safety checks */
    if (tag == NULL) return false;
    if (tag->type == NZ_MAC) return false;

    /* Look up the tag */
    mgr = network_zone_get_mgr();
    entry = ds_tree_find(&mgr->tags_to_monitor, tag->val);
    if (entry != NULL)
    {
        LOGD("%s: tag %s already registered for zone %s", __func__,
             tag->val, tag->nz_name);
        return false;
    }

    /* Get the tag type */
    tag_type = om_get_type_of_tag(tag->ovsdb_tag_string);
    if (tag_type == -1) return false;

    if (tag_type == OM_TLE_FLAG_NONE) tag_type = 0;

    /* Add the tag to the list of tags to monitor */
    entry = CALLOC(1, sizeof(*entry));
    if (entry == NULL) return false;

    STRSCPY(entry->name, tag->val);
    STRSCPY(entry->zone_name, tag->nz_name);
    entry->type = tag->type;

    ds_tree_insert(&mgr->tags_to_monitor, entry, entry->name);

    /* Process the tag content */
    ovsdb_tag = om_tag_find(tag->ovsdb_tag_string);
    /* The tag might not be yet configured */
    if (ovsdb_tag == NULL) return true;

    /* Register all values */
    tree = &ovsdb_tag->values;
    ds_tree_foreach(tree, tag_item)
    {
        struct network_zone_mac mac_item;

        /* Check for matching type */
        if (tag_type && !(tag_item->flags & tag_type)) continue;

        STRSCPY(mac_item.val, tag_item->value);
        STRSCPY(mac_item.nz_name, tag->nz_name);
        mac_item.ovsdb_tag_string = tag_item->value;
        mac_item.priority = tag->priority;
        mac_item.type = NZ_MAC;
        network_zone_add_mac(&mac_item);
    }

    return true;
}


bool
network_zone_add_mac(struct network_zone_mac *mac)
{
    struct nz_device_entry *entry;
    struct network_zone_mgr *mgr;
    os_macaddr_t lookup_mac;
    bool ret;

    /* Safety checks */
    if (mac == NULL) return false;
    if (mac->type != NZ_MAC) return false;

    /* Convert the mac string to a byte array for lookup purposes */
    ret = os_nif_macaddr_from_str(&lookup_mac, mac->val);
    if (ret == false) return false;

    /* Lookup the device */
    mgr = network_zone_get_mgr();
    entry = ds_tree_find(&mgr->device_cache, &lookup_mac);

    /* If found, add the new zone in the list of the device's zones */
    if (entry != NULL)
    {
        ret = network_zone_add_device_zone(entry, mac);
        return ret;
    }

    /* The device entry was not found, allocate a new one */
    entry = CALLOC(1, sizeof(*entry));
    if (entry == NULL) return false;

    /* Convert the mac address from a string to a os_macaddr_t structure */
    ret = os_nif_macaddr_from_str(&entry->mac, mac->val);
    if (ret == false) goto out_on_err;

    /* Initialize the list of zones */
    ds_dlist_init(&entry->zones, struct nz_device_zone, node);

    /* Add the zone to the device list */
    network_zone_add_device_zone(entry, mac);

    /* Add the device entry to the  device cache */
    ds_tree_insert(&mgr->device_cache, entry, &entry->mac);

    return true;

out_on_err:
    FREE(entry);
    return false;
}


static void
network_zone_add_devices(struct nz_cache_entry *nz)
{
    struct network_zone_mac mac;
    struct str_set *macs;
    size_t i;

    LOGD("%s:%d: zone: %s", __func__, __LINE__, nz->name);
    /* Sanity checks */
    if (nz == NULL) return;
    if (nz->device_tags ==  NULL) return;

    macs = nz->device_tags;
    LOGD("%s:%d: zone: %s", __func__, __LINE__, nz->name);
    for (i = 0; i < macs->nelems; i++)
    {
        network_zone_get_mac(nz, macs->array[i], &mac);
        LOGD("%s: mac value: %s (ovsdb: %s), type %d", __func__,
             mac.val, mac.ovsdb_tag_string, mac.type);
        switch (mac.type)
        {
            case NZ_GTAG:
            case NZ_TAG:
                network_zone_add_tag(&mac);
                break;

            case NZ_MAC:
                network_zone_add_mac(&mac);
                break;

            default:
                LOGD("%s: unexpected type %d for elem %s", __func__,
                     mac.type, mac.val);
                break;
        }
    }
}

static struct nz_cache_entry *
network_zone_create(struct schema_Network_Zone *new_zone)
{
    struct nz_cache_entry *new_nz;
    bool check;

    /* Allocate a zone cache entry */
    new_nz = CALLOC(1, sizeof(*new_nz));
    if (new_nz == NULL) return NULL;

    /* Translate the ovs entry to the cache entry */
    new_nz->name = STRDUP(new_zone->name);
    if (new_nz->name == NULL) return NULL;

    new_nz->device_tags = schema2str_set(sizeof(new_zone->macs[0]),
                                         new_zone->macs_len,
                                         new_zone->macs);
    check = network_zone_check_conversion(new_nz, new_zone->macs_len);
    if (check == false) return NULL;

    new_nz->priority = new_zone->priority;

    /* Process the mac elements */
    network_zone_add_devices(new_nz);

    network_zone_log(new_nz);

    return new_nz;
}

static void
network_zone_add(struct schema_Network_Zone *new_zone)
{
    struct nz_cache_entry *new_nz;
    struct network_zone_mgr *mgr;

    new_nz = network_zone_create(new_zone);
    if (new_nz == NULL) return;

    /* Add the entry in the zone cache */
    mgr = network_zone_get_mgr();
    ds_tree_insert(&mgr->nz_cache, new_nz, new_nz->name);

}


static struct nz_cache_entry *
network_zone_schema_lookup(struct schema_Network_Zone *ovsdb_nz)
{
    struct network_zone_mgr *mgr;
    struct nz_cache_entry *nz;
    ds_tree_t *tree;
    char *name;

    mgr = network_zone_get_mgr();

    name = ovsdb_nz->name;
    tree = &mgr->nz_cache;
    nz = ds_tree_find(tree, name);

    return nz;
}


static void
network_zone_delete(struct schema_Network_Zone *ovsdb_nz)
{
    struct network_zone_mgr *mgr;
    struct nz_cache_entry *nz;

    mgr = network_zone_get_mgr();
    nz = network_zone_schema_lookup(ovsdb_nz);
    if (nz == NULL) return;

    ds_tree_remove(&mgr->nz_cache, nz);
    network_zone_delete_zone_entry(nz);
}


static void
network_zone_update(struct schema_Network_Zone *old_rec,
                    struct schema_Network_Zone *node_cfg)
{
    /* Delete the old record */
    network_zone_delete(node_cfg);

    /* Add the new record */
    network_zone_add(node_cfg);
}


/**
 * @brief registered callback for Node_Config events
 */
static void
callback_Network_Zone(ovsdb_update_monitor_t *mon,
                      struct schema_Network_Zone *old_rec,
                      struct schema_Network_Zone *node_cfg)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        LOGI("%s: new network zone: %s", __func__, node_cfg->name);
        network_zone_add(node_cfg);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        network_zone_delete(old_rec);
        LOGI("%s: deleted network zone %s", __func__, old_rec->name);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        LOGI("%s: update network zone %s", __func__, node_cfg->name);
        network_zone_update(old_rec, node_cfg);
    }
}


static void
network_zone_clean_device_cache(void)
{
    struct nz_device_entry *entry;
    struct network_zone_mgr *mgr;
    ds_tree_t *tree;

    mgr = network_zone_get_mgr();

    if (mgr->initialized == false) return;

    tree = &mgr->device_cache;
    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        struct nz_device_entry *to_remove;
        struct nz_device_entry *next;

        next = ds_tree_next(tree, entry);
        to_remove = entry;

        ds_tree_remove(tree, to_remove);
        network_zone_delete_device_entry(to_remove);

        entry = next;
    }
}


static void
network_zone_clean_zone_cache(void)
{
    struct network_zone_mgr *mgr;
    struct nz_cache_entry *entry;
    ds_tree_t *tree;

    mgr = network_zone_get_mgr();

    if (mgr->initialized == false) return;

    tree = &mgr->device_cache;
    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        struct nz_cache_entry *to_remove;
        struct nz_cache_entry *next;

        next = ds_tree_next(tree, entry);
        to_remove = entry;

        ds_tree_remove(tree, to_remove);
        network_zone_delete_zone_entry(entry);

        entry = next;
    }
}


static void
network_zone_clean_tag_cache(void)
{
    struct network_zone_mgr *mgr;
    struct nz_tag_entry *entry;
    ds_tree_t *tree;

    mgr = network_zone_get_mgr();

    if (mgr->initialized == false) return;

    tree = &mgr->tags_to_monitor;
    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        struct nz_tag_entry *to_remove;
        struct nz_tag_entry *next;

        next = ds_tree_next(tree, entry);
        to_remove = entry;

        ds_tree_remove(tree, to_remove);
        FREE(entry);

        entry = next;
    }
}


char *
network_zone_get_zone(os_macaddr_t *mac)
{
    struct nz_device_entry *dev_entry;
    struct network_zone_mgr *mgr;
    struct nz_device_zone *zone;

    mgr = network_zone_get_mgr();

    if (mgr->initialized == false) return NULL;

    dev_entry = ds_tree_find(&mgr->device_cache, mac);
    if (dev_entry == NULL) return NULL;

    zone = ds_dlist_head(&dev_entry->zones);
    if (zone == NULL) return NULL;

    return zone->name;
}


static int
network_zone_device_cmp(const void *a, const void *b)
{
    const os_macaddr_t *dev_id_a = a;
    const os_macaddr_t *dev_id_b = b;

    return memcmp(dev_id_a->addr,
                  dev_id_b->addr,
                  sizeof(dev_id_a->addr));
}


static int
network_str_cmp(const void *a, const void *b)
{
    return strcmp(a, b);
}


void
network_zone_init_manager(void)
{
    struct network_zone_mgr *mgr;

    mgr = network_zone_get_mgr();
    if (mgr->initialized) return;

    LOGD("%s: initializing", __func__);

    ds_tree_init(&mgr->device_cache, network_zone_device_cmp,
                 struct nz_device_entry, node);

    ds_tree_init(&mgr->nz_cache, network_str_cmp,
                 struct nz_cache_entry, node);

    ds_tree_init(&mgr->tags_to_monitor, network_str_cmp,
                 struct nz_tag_entry, node);

    mgr->initialized = true;
}


void
network_zone_init(void)
{
    struct network_zone_mgr *mgr;

    mgr = network_zone_get_mgr();

    if (mgr->initialized)
    {
        LOGT("%s: already initialized", __func__);
        return;
    }

    network_zone_init_manager();
    OVSDB_TABLE_INIT_NO_KEY(Network_Zone);
    OVSDB_TABLE_MONITOR(Network_Zone, false);
}


void
network_zone_exit(void)
{
    struct network_zone_mgr *mgr;

    mgr = network_zone_get_mgr();

    if (mgr->initialized == false) return;

    network_zone_clean_device_cache();
    network_zone_clean_zone_cache();
    network_zone_clean_tag_cache();

    mgr->initialized = false;
}
