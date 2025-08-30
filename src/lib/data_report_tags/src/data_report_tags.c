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
#include "data_report_tags.h"
#include "data_report_tags_internals.h"


static char tag_marker[2] = "${";
static char gtag_marker[2] = "$[";

static struct data_report_tags_mgr drt_mgr =
{
    .initialized = false,
};


static struct op_to_str
{
    enum drt_precedence op;
    char *str_op;
} drt_op_str[] =
{
    {
        .op = DRT_UNDEFINED_PRECEDENCE,
        .str_op = "undefined",
    },
    {
        .op = DRT_INCLUDE,
        .str_op = "DRT_INCLUDE",
    },
    {
        .op = DRT_EXCLUDE,
        .str_op = "DRT_EXCLUDE",
    },
};


static char *
drt_op_to_str(enum drt_precedence op)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(drt_op_str); i++)
    {
        if (drt_op_str[i].op == op) return drt_op_str[i].str_op;
    }
    return "unknown";
}


static ovsdb_table_t table_Data_Report_Tags;

static int
drt_str_cmp(const void *a, const void *b)
{
    return strcmp(a, b);
}

static int
drt_device_cmp(const void *a, const void *b)
{
    const os_macaddr_t *dev_id_a = a;
    const os_macaddr_t *dev_id_b = b;

    return memcmp(dev_id_a->addr,
                  dev_id_b->addr,
                  sizeof(dev_id_a->addr));
}


/**
 * @brief determine the type and value of network zone mac element
 *
 * @param drt_elem the ovsdb mac element
 * @param mac the structure to be filled
 */
static void
drt_get_tag(char *drt_elem, struct drt_ovsdb_tag *tag)
{
    bool tag_has_marker;
    bool is_gtag;
    bool is_tag;
    char *tag_s;

    is_gtag = !strncmp(drt_elem, gtag_marker, sizeof(gtag_marker));
    is_tag = !strncmp(drt_elem, tag_marker, sizeof(tag_marker));

    if (is_gtag || is_tag)
    {
        tag_s = drt_elem + 2; /* pass tag marker */
        /* pass tag values marker */
        tag_has_marker = (*tag_s == TEMPLATE_DEVICE_CHAR);
        tag_has_marker |= (*tag_s == TEMPLATE_CLOUD_CHAR);
        tag_has_marker |= (*tag_s == TEMPLATE_LOCAL_CHAR);
        if (tag_has_marker) tag_s += 1;

        /* Copy tag name, remove end marker */
        STRSCPY_LEN(tag->val, tag_s, -1);
        tag->type = (is_gtag ? DRT_GTAG : DRT_TAG);
    }
    else
    {
        tag_s = drt_elem;
        STRSCPY(tag->val, tag_s);
        tag->type = DRT_MAC;
    }

    tag->ovsdb_tag_string = drt_elem;
}


struct data_report_tags_mgr *
data_report_tags_get_mgr(void)
{
    return &drt_mgr;
}

void
drt_walk_caches(void)
{
    struct drt_device_entry *device_item;
    struct data_report_tags_mgr *mgr;
    struct drt_cache_entry *drt_item;
    struct drt_tag_entry *tag_item;
    ds_tree_t *tree;

    mgr = data_report_tags_get_mgr();

    tree = &mgr->drt_cache;
    LOGT("%s: walking drt cache", __func__);
    ds_tree_foreach(tree, drt_item)
    {
        LOGT("%s:   %s", __func__, drt_item->drt_name);
    }

    tree = &mgr->tags_to_monitor;
    LOGT("%s: walking tags cache", __func__);
    ds_tree_foreach(tree, tag_item)
    {
        struct drt_tag_drt_entry *tag_drt_item;
        LOGT("%s:   %s", __func__, tag_item->tag_name);
        ds_tree_foreach(&tag_item->drt_entries, tag_drt_item)
        {
            LOGT("%s:    drt entry name: %s, operand: %s, precedence: %s", __func__,
                 tag_drt_item->drt_name, drt_op_to_str(tag_drt_item->operand),
                 drt_op_to_str(tag_drt_item->tag_op));
        }
    }

    tree = &mgr->device_cache;
    LOGT("%s: walking device cache", __func__);
    ds_tree_foreach(tree, device_item)
    {
        struct drt_features_list_entry *features_item;
        struct str_set *features_set;
        size_t features_set_nelems;
        ds_dlist_t *dlist;

        features_set = data_report_tags_get_tags(&device_item->mac);
        features_set_nelems = (features_set != NULL ? features_set->nelems : 0);
        LOGT("%s: " PRI_os_macaddr_t " nelems: %zu (feature set nelems: %zu)", __func__,
             FMT_os_macaddr_t(device_item->mac), device_item->nelems, features_set_nelems);
        dlist = &device_item->features_list;
        ds_dlist_foreach(dlist, features_item)
        {
            LOGT("%s:    feature: %s, enabled: %s", __func__,
                 features_item->name,
                 features_item->flags & DRT_FEATURE_ENABLED ? "true" : "false");
        }
    }
}


static void
drt_refresh_device_features_set(struct drt_device_entry *device_item)
{
    struct drt_features_list_entry *features_item;
    struct str_set *features_set;
    ds_dlist_t *dlist;
    size_t nelems;
    char *item;
    bool loop;
    size_t i;

    LOGT("%s: processing device " PRI_os_macaddr_t " with nelems %zu ", __func__,
         FMT_os_macaddr_t(device_item->mac), device_item->nelems);
    /* Safety checks */
    nelems = device_item->nelems;
    if (nelems == 0) return;

    dlist = &device_item->features_list;
    features_item = ds_dlist_head(dlist);
    if (features_item == NULL) return;

    features_set = &device_item->features_set;
    for (i = 0; i < features_set->nelems; i++)
    {
        LOGT("%s: freeing feature %s from device " PRI_os_macaddr_t, __func__,
             features_set->array[i], FMT_os_macaddr_t(device_item->mac));
        FREE(features_set->array[i]);
    }
    FREE(features_set->array);
    features_set->array = NULL;
    features_set->nelems = 0;

    features_set->array = CALLOC(device_item->nelems,
                                 sizeof(*features_set->array));
    i = 0;
    do {
        item = features_item->name;
        LOGT("%s:     feature %s, %senabled", __func__, item,
             features_item->flags & DRT_FEATURE_ENABLED ? "" : "not ");
        if (features_item->flags & DRT_FEATURE_ENABLED)
        {
            features_set->array[i] = STRDUP(item);
            features_set->nelems++;
            LOGT("%s: adding feature %s to device " PRI_os_macaddr_t, __func__,
                 features_set->array[i], FMT_os_macaddr_t(device_item->mac));
            i++;
        }
        features_item = ds_dlist_next(dlist, features_item);
        loop = (features_item != NULL);
        loop &= (i < nelems);
    } while (loop);
}


void
drt_update_device_features(struct drt_device_entry *device_item)
{
    struct drt_features_list_entry *features_item;
    ds_dlist_t *dlist;

    LOGT("%s: processing device " PRI_os_macaddr_t " with nelems %zu", __func__,
         FMT_os_macaddr_t(device_item->mac), device_item->nelems);

    dlist = &device_item->features_list;
    ds_dlist_foreach(dlist, features_item)
    {
        if (features_item->flags & DRT_STRONG_EXCLUDE)
        {
            if (features_item->flags & DRT_FEATURE_ENABLED)
            {
                device_item->nelems--;
                features_item->flags &= ~DRT_FEATURE_ENABLED;
            }
        }
        else if (features_item->flags & DRT_STRONG_INCLUDE)
        {
            if (!(features_item->flags & DRT_FEATURE_ENABLED))
            {
                device_item->nelems++;
                features_item->flags |= DRT_FEATURE_ENABLED;
            }
        }
        else if (features_item->flags & DRT_WEAK_EXCLUDE)
        {
            if (features_item->flags & DRT_FEATURE_ENABLED)
            {
                device_item->nelems--;
                features_item->flags &= ~DRT_FEATURE_ENABLED;
            }
        }
        else if (features_item->flags & DRT_WEAK_INCLUDE)
        {
            if (!(features_item->flags & DRT_FEATURE_ENABLED))
            {
                device_item->nelems++;
                features_item->flags |= DRT_FEATURE_ENABLED;
            }
        }
        LOGT("%s:    feature %s flags: 0x%x %senabled", __func__,
             features_item->name, features_item->flags,
             features_item->flags & DRT_FEATURE_ENABLED ? "" : "not ");
    }
    LOGT("%s: finished processing device " PRI_os_macaddr_t " with nelems %zu", __func__,
         FMT_os_macaddr_t(device_item->mac), device_item->nelems);
    /* (Re-)generate the feature set */
    drt_refresh_device_features_set(device_item);
}


static enum drt_precedence
drt_get_ovsdb_precedence(char *precedence)
{
    int rc;

    rc = strcmp(precedence, "include");
    if (!rc) return DRT_INCLUDE;

    rc = strcmp(precedence, "exclude");
    if (!rc) return DRT_EXCLUDE;

    return DRT_UNDEFINED_PRECEDENCE;
}


static bool
drt_check_conversion(void *converted, int len)
{
    if (len == 0) return true;
    if (converted == NULL) return false;

    return true;
}


static struct drt_tag_drt_entry *
drt_add_drt_to_tag(struct drt_tag_entry *tag_entry,
                   struct drt_cache_entry *drt,
                   enum drt_precedence tag_operand)
{
    struct drt_tag_drt_entry *tag_drt_entry;

    tag_drt_entry = ds_tree_find(&tag_entry->drt_entries, drt->drt_name);
    if (tag_drt_entry != NULL)
    {
        LOGT("%s: data report tag %s already added for tag %s", __func__,
             tag_drt_entry->drt_name, tag_entry->tag_name);
        return NULL;
    }

    tag_drt_entry = CALLOC(1, sizeof(*tag_drt_entry));
    STRSCPY(tag_drt_entry->drt_name, drt->drt_name);
    tag_drt_entry->operand = tag_operand;
    tag_drt_entry->tag_op = drt->precedence;
    tag_drt_entry->type = tag_entry->type;
    ds_tree_insert(&tag_entry->drt_entries, tag_drt_entry, tag_drt_entry->drt_name);
    return tag_drt_entry;
}


static void
drt_add_drt_to_device(struct drt_device_entry *device_item,
                      struct drt_tag_drt_entry *tag_drt_entry)
{
    struct drt_features_list_entry *features_entry;
    char mac_str[32];

    /* Safety checks */
    if (device_item == NULL) return;
    if (tag_drt_entry == NULL) return;

    os_nif_macaddr_to_str(&device_item->mac, mac_str, PRI_os_macaddr_lower_t);
    LOGT("%s: adding feature %s to device %s", __func__, tag_drt_entry->drt_name, mac_str);
    LOGT("%s: tag_op %s, operand %s", __func__,
         drt_op_to_str(tag_drt_entry->tag_op), drt_op_to_str(tag_drt_entry->operand));
    /* Allocate a feature entry and store it */
    features_entry = CALLOC(1, sizeof(*features_entry));
    STRSCPY(features_entry->name, tag_drt_entry->drt_name);

    ds_dlist_insert_tail(&device_item->features_list, features_entry);

    /* Compute the de feature flag */
    /* The tag is marked for include, the precedence operation is include */
    if ((tag_drt_entry->tag_op == DRT_INCLUDE) && (tag_drt_entry->operand == DRT_INCLUDE))
    {
        features_entry->flags |= DRT_STRONG_INCLUDE;
    }

    /* The tag is marked for include, the precedence operation is exclude */
    else if ((tag_drt_entry->tag_op == DRT_EXCLUDE) && (tag_drt_entry->operand == DRT_EXCLUDE))
    {
        features_entry->flags |= DRT_STRONG_EXCLUDE;
    }

    /* The tag is marked for include, the precedence operation is exclude */
    else if ((tag_drt_entry->tag_op == DRT_INCLUDE) && (tag_drt_entry->operand == DRT_EXCLUDE))
    {
        features_entry->flags |= DRT_WEAK_EXCLUDE;
    }

    /* The tag is marked for exclude, the precedence operation is include */
    else if ((tag_drt_entry->tag_op == DRT_EXCLUDE) && (tag_drt_entry->operand == DRT_INCLUDE))
    {
        features_entry->flags |= DRT_WEAK_INCLUDE;
    }

    drt_update_device_features(device_item);
}


static void
drt_process_tagged_devices(struct drt_tag_entry *drt_tag,
                           struct drt_tag_drt_entry *tag_drt_entry)
{
    struct data_report_tags_mgr *mgr;
    om_tag_list_entry_t *tag_item;
    om_tag_t *ovsdb_tag;
    ds_tree_t *tree;
    int tag_type;

    /* Safety checks */
    if (drt_tag == NULL) return;
    if (drt_tag->type == DRT_MAC) return;
    if (tag_drt_entry == NULL) return;

    mgr = data_report_tags_get_mgr();

    /* Get the tag type */
    tag_type = om_get_type_of_tag(drt_tag->ovsdb_tag_string);
    if (tag_type == -1) return;

    if (tag_type == OM_TLE_FLAG_NONE) tag_type = 0;

    /* Process the tag content */
    ovsdb_tag = om_tag_find(drt_tag->ovsdb_tag_string);
    /* The tag might not be yet configured */
    if (ovsdb_tag == NULL) return;

    /* Register all values */
    if (drt_tag->added == NULL)
    {
        tree = &ovsdb_tag->values;
    }
    else
    {
        tree = drt_tag->added;
    }
    ds_tree_foreach(tree, tag_item)
    {
        struct drt_device_entry *device_item;
        os_macaddr_t lookup;
        bool ret;

        /* Check for matching type */
        if (tag_type && !(tag_item->flags & tag_type)) continue;

        /* look up the device */
        ret = os_nif_macaddr_from_str(&lookup, tag_item->value);
        if (ret == false) continue;

        device_item = ds_tree_find(&mgr->device_cache, &lookup);
        /* The device entry was found. Update its features */
        if (device_item != NULL)
        {
            drt_add_drt_to_device(device_item, tag_drt_entry);
            continue;
        }

        /* The device not not found. Create an entry */
        device_item = CALLOC(1, sizeof(*device_item));
        MEM_CPY(&device_item->mac, &lookup, sizeof(device_item->mac));
        ds_tree_insert(&mgr->device_cache, device_item, &device_item->mac);
        ds_dlist_init(&device_item->features_list, struct drt_features_list_entry, node);
        drt_add_drt_to_device(device_item, tag_drt_entry);
    }
}


static void
drt_add_mac(struct drt_ovsdb_tag *tag,
            struct drt_cache_entry *drt,
            enum drt_precedence tag_operand)
{
    struct drt_tag_drt_entry tag_drt_entry;
    struct drt_device_entry *device_item;
    struct data_report_tags_mgr *mgr;
    os_macaddr_t lookup;
    bool ret;

    if (tag == NULL) return;
    if (drt == NULL) return;

    memset(&tag_drt_entry, 0, sizeof(tag_drt_entry));

    STRSCPY(tag_drt_entry.drt_name, drt->drt_name);
    tag_drt_entry.operand = tag_operand;
    tag_drt_entry.tag_op = drt->precedence;
    tag_drt_entry.type = tag->type;

    /* look up the device */
    ret = os_nif_macaddr_from_str(&lookup, tag->val);
    if (ret == false) return;

    mgr = data_report_tags_get_mgr();
    device_item = ds_tree_find(&mgr->device_cache, &lookup);
    /* The device entry was found. Update its features */
    if (device_item != NULL)
    {
        drt_add_drt_to_device(device_item, &tag_drt_entry);
        return;
    }

    /* The device not not found. Create an entry */
    device_item = CALLOC(1, sizeof(*device_item));
    MEM_CPY(&device_item->mac, &lookup, sizeof(device_item->mac));
    ds_tree_insert(&mgr->device_cache, device_item, &device_item->mac);
    ds_dlist_init(&device_item->features_list, struct drt_features_list_entry, node);
    drt_add_drt_to_device(device_item, &tag_drt_entry);
}


static void
drt_add_tag(struct drt_ovsdb_tag *tag,
            struct drt_cache_entry *drt,
            enum drt_precedence tag_operand)
{
    struct drt_tag_drt_entry *tag_drt_entry;
    struct data_report_tags_mgr *mgr;
    struct drt_tag_entry *tag_entry;
    int tag_type;

    /* Safety checks */
    if (tag == NULL) return;
    if ((tag->type != DRT_TAG) && (tag->type != DRT_GTAG)) return;

    mgr = data_report_tags_get_mgr();

    /* Look up the tag */
    tag_entry = ds_tree_find(&mgr->tags_to_monitor, tag->val);
    if (tag_entry != NULL)
    {
        /* Process the tagged devices */
        tag_drt_entry = drt_add_drt_to_tag(tag_entry, drt, tag_operand);
        drt_process_tagged_devices(tag_entry, tag_drt_entry);
        return;
    }

    /* Get the tag type */
    tag_type = om_get_type_of_tag(tag->ovsdb_tag_string);
    if (tag_type == -1) return;

    if (tag_type == OM_TLE_FLAG_NONE) tag_type = 0;

    /* Add the tag to the list of tags to monitor */
    tag_entry = CALLOC(1, sizeof(*tag_entry));
    if (tag_entry == NULL) return;

    STRSCPY(tag_entry->tag_name, tag->val);
    tag_entry->type = tag->type;
    tag_entry->ovsdb_tag_string = STRDUP(tag->ovsdb_tag_string);
    ds_tree_init(&tag_entry->drt_entries, drt_str_cmp, struct drt_tag_drt_entry, node);
    tag_drt_entry = drt_add_drt_to_tag(tag_entry, drt, tag_operand);

    ds_tree_insert(&mgr->tags_to_monitor, tag_entry, tag_entry->tag_name);

    /* Process the tagged devices */
    drt_process_tagged_devices(tag_entry, tag_drt_entry);
}


static void
drt_add_tag_entries(struct str_set *tags,
                    struct drt_cache_entry *drt,
                    enum drt_precedence tag_operand)
{
    struct drt_ovsdb_tag tag;
    size_t i;

    /* Sanity checks */
    if (tags == NULL) return;
    if (drt == NULL) return;

    for (i = 0; i < tags->nelems; i++)
    {
        drt_get_tag(tags->array[i], &tag);
        LOGD("%s: tag value: %s (ovsdb: %s), type %d", __func__,
             tag.val, tag.ovsdb_tag_string, tag.type);
        switch(tag.type)
        {
            case DRT_GTAG:
            case DRT_TAG:
                drt_add_tag(&tag, drt, tag_operand);
                break;

            case DRT_MAC:
                drt_add_mac(&tag, drt, tag_operand);
                break;

            default:
                LOGD("%s: unexpected type %d for elem %s", __func__,
                     tag.type, tag.val);
                break;
        }
    }
}


static void
drt_add_devices(struct drt_cache_entry *drt)
{
    struct str_set *tags;

    /* Sanity checks */
    if (drt == NULL) return;

    /* Process the included macs */
    tags = drt->included_macs;
    drt_add_tag_entries(tags, drt, DRT_INCLUDE);

    /* Process the excluded macs */
    tags = drt->excluded_macs;
    drt_add_tag_entries(tags, drt, DRT_EXCLUDE);
}


static struct drt_cache_entry *
drt_create(struct schema_Data_Report_Tags *new_report_tags)
{
    struct drt_cache_entry *new_drt;
    enum drt_precedence precedence;
    bool check;

    /* Sanity check */
    if (new_report_tags == NULL) return NULL;

    precedence = drt_get_ovsdb_precedence(new_report_tags->precedence);
    if (precedence == DRT_UNDEFINED_PRECEDENCE) return NULL;

    /* Allocate a data report tags cache entry */
    new_drt = CALLOC(1, sizeof(*new_drt));

    /* Translate the ovs entry to the cache entry */
    STRSCPY(new_drt->drt_name, new_report_tags->name);

    new_drt->included_macs = schema2str_set(sizeof(new_report_tags->included_macs[0]),
                                            new_report_tags->included_macs_len,
                                            new_report_tags->included_macs);
    check = drt_check_conversion(new_drt, new_report_tags->included_macs_len);
    if (check == false) return NULL;

    new_drt->excluded_macs = schema2str_set(sizeof(new_report_tags->excluded_macs[0]),
                                            new_report_tags->excluded_macs_len,
                                            new_report_tags->excluded_macs);
    check = drt_check_conversion(new_drt, new_report_tags->excluded_macs_len);
    if (check == false) return NULL;

    new_drt->precedence = precedence;

    /* Process the [included | excluded]_macs elements */
    drt_add_devices(new_drt);

    return new_drt;
}


static void
drt_add(struct schema_Data_Report_Tags *new_data_report_tags)
{
    struct data_report_tags_mgr *mgr;
    struct drt_cache_entry *new_drt;

    new_drt = drt_create(new_data_report_tags);
    if (new_drt == NULL) return;

    /* Add the entry in the data_report_tags cache */
    mgr = data_report_tags_get_mgr();
    ds_tree_insert(&mgr->drt_cache, new_drt, new_drt->drt_name);
}


static void
drt_free_device_entry(struct drt_device_entry *device_item)
{
    struct drt_features_list_entry *features_item;
    struct drt_features_list_entry *remove;
    struct drt_features_list_entry *next;
    struct str_set *set;
    ds_dlist_t *dlist;
    char **array_iter;
    size_t nelems;
    char **array;
    size_t i;

    dlist = &device_item->features_list;
    features_item = ds_dlist_head(dlist);

    while (features_item != NULL)
    {
        next = ds_dlist_next(dlist, features_item);
        remove = features_item;
        features_item = next;
        ds_dlist_remove(dlist, remove);
        FREE(remove);
    }

    set = &device_item->features_set;
    if (set == NULL) return;

    nelems = set->nelems;
    array = set->array;
    array_iter = array;
    for (i = 0; i < nelems; i++) FREE(*array_iter++);
    set->nelems = 0;
    FREE(array);

    FREE(device_item);
}


static void
drt_delete_drt_from_device(struct drt_device_entry *device_item,
                           struct drt_tag_drt_entry *tag_drt_entry)
{
    struct drt_features_list_entry *features_item;
    struct drt_features_list_entry *remove;
    struct drt_features_list_entry *next;
    ds_dlist_t *dlist;

    dlist = &device_item->features_list;
    features_item = ds_dlist_head(dlist);

    while (features_item != NULL)
    {
        int ret;

        next = ds_dlist_next(dlist, features_item);
        ret = strcmp(features_item->name, tag_drt_entry->drt_name);
        if (ret != 0)
        {
            features_item = next;
            continue;
        }

        if (features_item->flags & DRT_FEATURE_ENABLED)
        {
            device_item->nelems--;
        }
        remove = features_item;
        features_item = next;
        ds_dlist_remove(dlist, remove);
        FREE(remove);
    }

    /* Refresh the device's feature set */
    drt_refresh_device_features_set(device_item);
}


static void
drt_delete_tagged_devices(struct drt_tag_entry *tag_entry,
                          struct drt_tag_drt_entry *tag_drt_entry)
{
    struct data_report_tags_mgr *mgr;
    om_tag_list_entry_t *tag_item;
    om_tag_t *ovsdb_tag;
    ds_tree_t *tree;
    int tag_type;

    /* Safety checks */
    if (tag_entry == NULL) return;
    if (tag_entry->type == DRT_MAC) return;
    if (tag_drt_entry == NULL) return;

    mgr = data_report_tags_get_mgr();

    /* Get the tag type */
    tag_type = om_get_type_of_tag(tag_entry->ovsdb_tag_string);
    if (tag_type == -1) return;

    if (tag_type == OM_TLE_FLAG_NONE) tag_type = 0;

    /* Process the tag content */
    ovsdb_tag = om_tag_find(tag_entry->ovsdb_tag_string);
    /* The tag might not be yet configured */
    if (ovsdb_tag == NULL) return;

    if (tag_entry->removed == NULL)
    {
        tree = &ovsdb_tag->values;
    }
    else
    {
        tree = tag_entry->removed;
    }

    ds_tree_foreach(tree, tag_item)
    {
        struct drt_device_entry *device_item;
        os_macaddr_t lookup;
        bool ret;

        /* Check for matching type */
        if (tag_type && !(tag_item->flags & tag_type)) continue;

        /* look up the device */
        ret = os_nif_macaddr_from_str(&lookup, tag_item->value);
        if (ret == false) continue;

        device_item = ds_tree_find(&mgr->device_cache, &lookup);
        /* The device entry was found. Update its features */
        if (device_item == NULL) continue;

        drt_delete_drt_from_device(device_item, tag_drt_entry);

        /* Delete the device if its feature list is empty */
        ret = ds_dlist_is_empty(&device_item->features_list);
        if (ret == true)
        {
            ds_tree_remove(&mgr->device_cache, device_item);
            LOGT("%s:%d: tag %s, deleting device: %s", __func__, __LINE__,
                 tag_entry->tag_name, tag_item->value);
            drt_free_device_entry(device_item);
        }
        else
        {
            LOGT("%s: device %s still has features", __func__, tag_item->value);
        }
    }
}


static void
drt_delete_mac_entry(struct drt_ovsdb_tag *tag,
                     struct drt_cache_entry *drt,
                     enum drt_precedence tag_operand)
{
    struct drt_tag_drt_entry tag_drt_entry;
    struct drt_device_entry *device_item;
    struct data_report_tags_mgr *mgr;
    os_macaddr_t lookup;
    bool ret;

    if (tag == NULL) return;
    if (drt == NULL) return;

    memset(&tag_drt_entry, 0, sizeof(tag_drt_entry));

    STRSCPY(tag_drt_entry.drt_name, drt->drt_name);
    tag_drt_entry.operand = tag_operand;
    tag_drt_entry.tag_op = drt->precedence;
    tag_drt_entry.type = tag->type;

    /* look up the device */
    ret = os_nif_macaddr_from_str(&lookup, tag->val);
    if (ret == false) return;

    mgr = data_report_tags_get_mgr();
    device_item = ds_tree_find(&mgr->device_cache, &lookup);
    /* The device entry was not found. Exit */
    if (device_item == NULL) return;

    drt_delete_drt_from_device(device_item, &tag_drt_entry);

    /* Delete the device if its feature list is empty */
    ret = ds_dlist_is_empty(&device_item->features_list);
    if (ret == true)
    {
        ds_tree_remove(&mgr->device_cache, device_item);
        LOGT("%s:%d: deleting device: %s", __func__, __LINE__,
             tag->val);
        drt_free_device_entry(device_item);
    }
    else
    {
        LOGT("%s: device %s still has features", __func__, tag->val);
    }
}


static void
drt_delete_tag_entry(struct drt_ovsdb_tag *tag,
                     struct drt_cache_entry *drt,
                     enum drt_precedence tag_operand)
{
    struct drt_tag_drt_entry *tag_drt_entry;
    struct data_report_tags_mgr *mgr;
    struct drt_tag_entry *tag_entry;

    /* Safety checks */
    if (tag == NULL) return;
    if ((tag->type != DRT_TAG) && (tag->type != DRT_GTAG)) return;

    mgr = data_report_tags_get_mgr();

    /* Look up the tag */
    tag_entry = ds_tree_find(&mgr->tags_to_monitor, tag->val);
    if (tag_entry == NULL) return;

    tag_drt_entry = ds_tree_find(&tag_entry->drt_entries, drt->drt_name);
    if (tag_drt_entry == NULL) return;

    /* Process the tagged devices */
    drt_delete_tagged_devices(tag_entry, tag_drt_entry);

    /* Remove the drt entry from the tag */
    ds_tree_remove(&tag_entry->drt_entries, tag_drt_entry);
    FREE(tag_drt_entry);

    /* Remove the tag entry if it has no more drt entries */
    if (ds_tree_is_empty(&tag_entry->drt_entries))
    {
        ds_tree_remove(&mgr->tags_to_monitor, tag_entry);
        FREE(tag_entry->ovsdb_tag_string);
        FREE(tag_entry);
    }
}


static void
drt_delete_tags_entries(struct str_set *tags,
                        struct drt_cache_entry *drt,
                        enum drt_precedence tag_operand)
{
    struct drt_ovsdb_tag tag;
    size_t i;

    /* Sanity checks */
    if (tags == NULL) return;
    if (drt == NULL) return;

    for (i = 0; i < tags->nelems; i++)
    {
        drt_get_tag(tags->array[i], &tag);
        LOGD("%s: tag value: %s (ovsdb: %s), type %d", __func__,
             tag.val, tag.ovsdb_tag_string, tag.type);
        switch(tag.type)
        {
            case DRT_GTAG:
            case DRT_TAG:
                drt_delete_tag_entry(&tag, drt, tag_operand);
                break;

            case DRT_MAC:
                drt_delete_mac_entry(&tag, drt, tag_operand);
                break;

            default:
                LOGD("%s: unexpected type %d for elem %s", __func__,
                     tag.type, tag.val);
                break;
        }
    }
}


static void
drt_delete_devices(struct drt_cache_entry *drt)
{
    struct str_set *tags;

    /* Sanity checks */
    if (drt == NULL) return;

    /* Process the included macs */
    tags = drt->included_macs;
    drt_delete_tags_entries(tags, drt, DRT_INCLUDE);

    /* Process the excluded macs */
    tags = drt->excluded_macs;
    drt_delete_tags_entries(tags, drt, DRT_EXCLUDE);
}


static void
drt_delete_drt_entry(struct drt_cache_entry *drt)
{
    /* Sanity checks */
    if (drt == NULL) return;

    drt_delete_devices(drt);

    free_str_set(drt->included_macs);
    free_str_set(drt->excluded_macs);
    FREE(drt);
}


static struct drt_cache_entry *
drt_schema_lookup(struct schema_Data_Report_Tags *ovsdb_drt)
{
    struct data_report_tags_mgr *mgr;
    struct drt_cache_entry *drt;
    ds_tree_t *tree;
    char *name;

    mgr = data_report_tags_get_mgr();

    name = ovsdb_drt->name;
    tree = &mgr->drt_cache;
    drt = ds_tree_find(tree, name);

    return drt;
}


static void
drt_delete(struct schema_Data_Report_Tags *ovsdb_drt)
{
    struct data_report_tags_mgr *mgr;
    struct drt_cache_entry *drt;

    mgr = data_report_tags_get_mgr();
    drt = drt_schema_lookup(ovsdb_drt);
    if (drt == NULL) return;

    ds_tree_remove(&mgr->drt_cache, drt);
    drt_delete_drt_entry(drt);
}


static void
drt_update(struct schema_Data_Report_Tags *old_drt,
           struct schema_Data_Report_Tags *new_drt)
{
    /* Delete the old record */
    drt_delete(new_drt);

    /* Add the new record */
    drt_add(new_drt);
}


bool
data_report_tags_update_cb(om_tag_t *tag,
                           struct ds_tree *removed,
                           struct ds_tree *added,
                           struct ds_tree *updated)
{
    struct drt_tag_drt_entry *tag_drt_item;
    struct data_report_tags_mgr *mgr;
    struct drt_tag_entry *tag_entry;

    mgr = data_report_tags_get_mgr();

    /* Check if we care about this tag */
    tag_entry = ds_tree_find(&mgr->tags_to_monitor, tag->name);
    if (tag_entry == NULL) return false;

    tag_entry->added = added;
    tag_entry->removed = removed;

    ds_tree_foreach(&tag_entry->drt_entries, tag_drt_item)
    {
        if (added != NULL) drt_process_tagged_devices(tag_entry, tag_drt_item);
        if (removed != NULL) drt_delete_tagged_devices(tag_entry, tag_drt_item);
    }

    tag_entry->added = NULL;
    tag_entry->removed = NULL;

    return true;
}


/**
 * @brief registered callback for Data_Report_Tags events
 */
static void
callback_Data_Report_Tags(ovsdb_update_monitor_t *mon,
                          struct schema_Data_Report_Tags *old_rec,
                          struct schema_Data_Report_Tags *node_cfg)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        LOGI("%s: new data report tags entry: %s", __func__, node_cfg->name);
        drt_add(node_cfg);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        drt_delete(old_rec);
        LOGI("%s: deleted data report tags entry: %s", __func__, old_rec->name);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        drt_update(old_rec, node_cfg);
        LOGI("%s: updated data report tags entry %s", __func__, node_cfg->name);
    }
}


void
data_report_tags_init_manager(void)
{
    struct data_report_tags_mgr *mgr;

    mgr = data_report_tags_get_mgr();

    if (mgr->initialized) return;

    LOGD("%s: initializing", __func__);

    ds_tree_init(&mgr->drt_cache, drt_str_cmp,
                 struct drt_cache_entry, node);

    ds_tree_init(&mgr->tags_to_monitor, drt_str_cmp,
                 struct drt_tag_entry, node);

    ds_tree_init(&mgr->device_cache, drt_device_cmp,
                 struct drt_device_entry, node);

    mgr->initialized = true;
}


void
data_report_tags_init(void)
{
    struct data_report_tags_mgr *mgr;

    mgr = data_report_tags_get_mgr();

    if (mgr->initialized)
    {
        LOGT("%s: already initialized", __func__);
        return;
    }

    data_report_tags_init_manager();

    OVSDB_TABLE_INIT_NO_KEY(Data_Report_Tags);
    OVSDB_TABLE_MONITOR(Data_Report_Tags, false);
}


void
data_report_tags_exit(void)
{
    struct drt_cache_entry *drt_entry;
    struct data_report_tags_mgr *mgr;
    ds_tree_t *tree;

    mgr = data_report_tags_get_mgr();

    if (mgr->initialized == false) return;

    /* Delete all DRT entries */
    tree = &mgr->drt_cache;
    drt_entry = ds_tree_head(tree);

    while (drt_entry != NULL)
    {
        struct drt_cache_entry *remove;
        struct drt_cache_entry *next;

        next = ds_tree_next(tree, drt_entry);
        remove = drt_entry;
        drt_entry = next;
        ds_tree_remove(tree, remove);
        drt_delete_drt_entry(remove);
    }

    mgr->initialized = false;
}


struct str_set *
data_report_tags_get_tags(os_macaddr_t *mac)
{
    struct data_report_tags_mgr *mgr;
    struct drt_device_entry *device;
    ds_tree_t *tree;

    /* Safety check */
    if (mac == NULL) return NULL;

    mgr = data_report_tags_get_mgr();
    if (mgr->initialized == false) return NULL;

    tree = &mgr->device_cache;
    device = ds_tree_find(tree, mac);
    if (device == NULL) return NULL;

    return &device->features_set;
}
