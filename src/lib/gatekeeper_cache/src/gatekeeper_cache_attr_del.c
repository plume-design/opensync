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

#include "gatekeeper_cache.h"
#include "gatekeeper_cache_cmp.h"
#include "log.h"
#include "memutil.h"
#include "sockaddr_storage.h"



/**
 * @brief frees memory used by attribute when it is deleted
 * @params: tree pointer to attribute tree
 * @params: req_type: request type
 */
void
gkc_free_attr_entry(struct attr_cache *attr_entry, enum gk_cache_request_type attr_type)
{
    union attribute_type *attr;

    if (!IS_NULL_PTR(attr_entry->gk_policy))  FREE(attr_entry->gk_policy);
    if (!IS_NULL_PTR(attr_entry->network_id)) FREE(attr_entry->network_id);

    attr = &attr_entry->attr;
    switch (attr_type)
    {
    /* the 3 entries are now folded into the 'internal' type */
    case GK_CACHE_INTERNAL_TYPE_HOSTNAME:
    case GK_CACHE_REQ_TYPE_FQDN:
        if (!IS_NULL_PTR(attr_entry->fqdn_redirect))
        {
            FREE(attr_entry->fqdn_redirect->redirect_cname);
            FREE(attr_entry->fqdn_redirect);
        }

        /* fallthru */
    case GK_CACHE_REQ_TYPE_HOST:
    case GK_CACHE_REQ_TYPE_SNI:
        FREE(attr->host_name->name);
        FREE(attr->host_name);
        break;

    case GK_CACHE_REQ_TYPE_URL:
        FREE(attr->url->name);
        FREE(attr->url);
        break;

    case GK_CACHE_REQ_TYPE_IPV4:
        FREE(attr->ipv4);
        break;

    case GK_CACHE_REQ_TYPE_IPV6:
        FREE(attr->ipv6);
        break;

    case GK_CACHE_REQ_TYPE_APP:
        FREE(attr->app_name->name);
        FREE(attr->app_name);
        break;

    default:
        break;
    }
}

/**
 * @brief deletes attribute from the attributes tree
 * @params: tree pointer to attribute tree
 * @params: req_type: request type
 */
static void
gk_clean_attribute_tree(ds_tree_t *tree, enum gk_cache_request_type attr_type)
{
    struct attr_cache *attr_entry, *remove;

    attr_entry = ds_tree_head(tree);
    while (attr_entry != NULL)
    {
        remove = attr_entry;
        attr_entry = ds_tree_next(tree, attr_entry);
        gkc_free_attr_entry(remove, attr_type);
        gkc_remove_entry(tree, remove);
    }
}

/**
 * @brief free memory used by the flow entry on deletion
 */
static void
free_flow_entry_members(struct ip_flow_cache *flow_entry)
{
    FREE(flow_entry->src_ip_addr);
    FREE(flow_entry->dst_ip_addr);
    FREE(flow_entry->gk_policy);
    FREE(flow_entry->network_id);
}

/**
 * @brief deletes IP flow from the flow tree
 * @params: tree pointer to flows tree
 * @params: req_type: request type
 */
static void
gk_clean_flow_tree(ds_tree_t *tree, enum gk_cache_request_type req_type)
{
    struct ip_flow_cache *flow_entry, *remove;
    struct gk_cache_mgr *mgr;

    mgr = gk_cache_get_mgr();

    flow_entry = ds_tree_head(tree);
    while (flow_entry != NULL)
    {
        remove = flow_entry;
        flow_entry = ds_tree_next(tree, flow_entry);
        free_flow_entry_members(remove);
        ds_tree_remove(tree, remove);
        FREE(remove);

        /* Update cache counter accordingly */
        mgr->total_entry_count--;
    }
}

/**
 * @brief clean IP flow trees for the
 *        given device
 * @params: pd_cache pointer to per device tree
 */
int
gk_clean_per_device_entry(struct per_device_cache *pd_cache)
{
    int pdevice_count;

    pdevice_count = gk_get_cache_count();

    gk_clean_attribute_tree(&pd_cache->hostname_tree, GK_CACHE_INTERNAL_TYPE_HOSTNAME);
    gk_clean_attribute_tree(&pd_cache->url_tree, GK_CACHE_REQ_TYPE_URL);
    gk_clean_attribute_tree(&pd_cache->ipv4_tree, GK_CACHE_REQ_TYPE_IPV4);
    gk_clean_attribute_tree(&pd_cache->ipv6_tree, GK_CACHE_REQ_TYPE_IPV6);
    gk_clean_attribute_tree(&pd_cache->app_tree, GK_CACHE_REQ_TYPE_APP);
    gk_clean_flow_tree(&pd_cache->inbound_tree, GK_CACHE_REQ_TYPE_INBOUND);
    gk_clean_flow_tree(&pd_cache->outbound_tree, GK_CACHE_REQ_TYPE_OUTBOUND);
    FREE(pd_cache->device_mac);
    FREE(pd_cache->category_cache);

    pdevice_count -= gk_get_cache_count();

    return pdevice_count;
}

/**
 * @brief free the memory used by cache entries
 *        when they are delete
 * @params: tree pointer to per device tree
 */
static void
gk_free_cache_tree(ds_tree_t *tree)
{
    struct per_device_cache *pdevice, *remove;

    pdevice = ds_tree_head(tree);
    while (pdevice != NULL)
    {
        remove = pdevice;
        pdevice = ds_tree_next(tree, pdevice);
        gk_clean_per_device_entry(remove);
        ds_tree_remove(tree, remove);
        FREE(remove);
    }
}

/**
 * @brief clean up all the gatekeeper cache entries
 */
void
gk_cache_cleanup(void)
{
    struct attr_cache *attr_entry;
    struct gk_cache_mgr *mgr;
    ds_tree_t *tree;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return;

    tree = &mgr->per_device_tree;
    gk_free_cache_tree(tree);
    tree = &mgr->location_wide_cache;
    ds_tree_foreach(tree, attr_entry)
    {
        gkc_free_attr_entry(attr_entry, attr_entry->type);
        ds_tree_remove(tree, attr_entry);
    }

    mgr->total_entry_count = 0;
    mgr->lru_available = mgr->lru_size;
    mgr->lru_free = ds_dlist_head(&mgr->lru_list);
    mgr->lru_recycled = 0;
}

/**
 * @brief check if the TTL is expired for the given
 *        attribute
 * @params: attr_entry pointer to attribute tree
 * @return true on success, false on failure
 */
static bool
gkc_attr_ttl_expired(struct attr_cache *attr_entry)
{
    time_t now;
    now = time(NULL);

    /* check if TTL is expired */
    if ((now - attr_entry->original_ts) < attr_entry->cache_ttl)
    {
        return false;
    }
    return true;
}

static const char *
gk_get_attribute_value(struct attr_cache *attr_entry, enum gk_cache_request_type attr_type)
{
    static char ip_str[INET6_ADDRSTRLEN];
    union attribute_type *attr;
    struct sockaddr_in6 *ipv6;
    struct sockaddr_in *ipv4;

    attr = &attr_entry->attr;

    switch (attr_type)
    {
        case GK_CACHE_INTERNAL_TYPE_HOSTNAME:
            LOGD("%s(): Fetching attr value using GK_CACHE_INTERNAL_TYPE_HOSTNAME", __func__);
            /* fallthru */
        case GK_CACHE_REQ_TYPE_FQDN:
        case GK_CACHE_REQ_TYPE_HOST:
        case GK_CACHE_REQ_TYPE_SNI:
            return attr->host_name->name;

        case GK_CACHE_REQ_TYPE_URL:
            return attr->url->name;

        case GK_CACHE_REQ_TYPE_IPV4:
            ipv4 = (struct sockaddr_in *)&attr->ipv4->ip_addr;
            inet_ntop(AF_INET, &ipv4->sin_addr, ip_str, INET_ADDRSTRLEN);
            return ip_str;

        case GK_CACHE_REQ_TYPE_IPV6:
            ipv6 = (struct sockaddr_in6 *)&attr->ipv6->ip_addr;
            inet_ntop(AF_INET6, &ipv6->sin6_addr, ip_str, INET6_ADDRSTRLEN);
            return ip_str;

        case GK_CACHE_REQ_TYPE_APP:
            return attr->app_name->name;

        default:
            return "unknown";
    }
}

static ds_tree_t *
gk_get_attribute_tree(struct per_device_cache *pdevice, int attr_type)
{
    ds_tree_t *attr_tree;

    switch (attr_type)
    {
        case GK_CACHE_INTERNAL_TYPE_HOSTNAME:
            LOGD("%s(): Fetching tree for attr using GK_CACHE_INTERNAL_TYPE_HOSTNAME", __func__);
            /* fallthru */
        case GK_CACHE_REQ_TYPE_FQDN:
        case GK_CACHE_REQ_TYPE_HOST:
        case GK_CACHE_REQ_TYPE_SNI:
            attr_tree = &pdevice->hostname_tree;
            break;

        case GK_CACHE_REQ_TYPE_URL:
            attr_tree = &pdevice->url_tree;
            break;

        case GK_CACHE_REQ_TYPE_IPV4:
            attr_tree = &pdevice->ipv4_tree;
            break;

        case GK_CACHE_REQ_TYPE_IPV6:
            attr_tree = &pdevice->ipv6_tree;
            break;

        case GK_CACHE_REQ_TYPE_APP:
            attr_tree = &pdevice->app_tree;
            break;

        default:
            attr_tree = NULL;
    }

    return attr_tree;
}

/**
 * @brief delete the given attribute from the attr
 *        tree if TTL is expired
 *
 * @params: gk_del_info cache delete info structure
 */

static void
gkc_cleanup_ttl_attribute_tree(struct gkc_del_info_s *gk_del_info)
{
    struct attr_cache *attr_entry, *remove;
    struct gk_cache_mgr *mgr;
    bool ttl_expired;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return;

    attr_entry = ds_tree_head(gk_del_info->tree);
    while (attr_entry != NULL)
    {
        remove = attr_entry;
        attr_entry = ds_tree_next(gk_del_info->tree, attr_entry);
        ttl_expired = gkc_attr_ttl_expired(remove);
        if (ttl_expired == false) continue;

        LOGT("%s(): Removing attribute '%s', for device " PRI_os_macaddr_lower_t " due to expired TTL",
             __func__,
             gk_get_attribute_value(remove, gk_del_info->attr_type),
             FMT_os_macaddr_pt(gk_del_info->pdevice->device_mac));

        gkc_free_attr_entry(remove, gk_del_info->attr_type);

        gk_del_info->attr_del_count++;

        /* decrement the cache entries counter */
        gkc_remove_entry(gk_del_info->tree, remove);
        mgr->total_entry_count--;
    }
}

/**
 * @brief deletes attributes with expired TTL value
 *
 * @params: pdevice pointer to device structure
 */
static void
gk_cache_check_ttl_per_device(struct per_device_cache *pdevice)
{
    struct gkc_del_info_s *gk_del_info;
    int attr_types[] = { GK_CACHE_INTERNAL_TYPE_HOSTNAME, GK_CACHE_REQ_TYPE_URL,
                         GK_CACHE_REQ_TYPE_IPV4, GK_CACHE_REQ_TYPE_IPV6,
                         GK_CACHE_REQ_TYPE_APP };
    size_t i;

    gk_del_info = CALLOC(1, sizeof(*gk_del_info));
    if (gk_del_info == NULL) return;

    gk_del_info->pdevice = pdevice;

    /* loop through all the attributes and check for TTL expiry */
    /* Note: HOST, FQDN and SNI are wrapped in one single tree */
    for (i = 0; i < sizeof(attr_types) / sizeof(attr_types[0]); i++)
    {
        gk_del_info->attr_type = attr_types[i];
        /* get the tree for this attribute type */
        gk_del_info->tree = gk_get_attribute_tree(pdevice, attr_types[i]);
        gkc_cleanup_ttl_attribute_tree(gk_del_info);
    }

    /* check the flow inbound tree */
    gk_del_info->attr_type = GK_CACHE_REQ_TYPE_INBOUND;
    gk_del_info->tree = &pdevice->inbound_tree;
    gkc_cleanup_ttl_flow_tree(gk_del_info);

    /* check the flow outbound tree */
    gk_del_info->attr_type = GK_CACHE_REQ_TYPE_OUTBOUND;
    gk_del_info->tree = &pdevice->outbound_tree;
    gkc_cleanup_ttl_flow_tree(gk_del_info);

    LOGT("%s(): number of expired TTL entries for device: " PRI_os_macaddr_lower_t " attribute type: %" PRIu64
         ", flow type %" PRIu64,
         __func__,
         FMT_os_macaddr_pt(pdevice->device_mac),
         gk_del_info->attr_del_count,
         gk_del_info->flow_del_count);

    FREE(gk_del_info);
}

/**
 * @brief deletes all the flows and attributes with
 *        expired TTL
 *
 * @params: tree device tree structure check and
 *          and delete entries.
 */
void
gk_cache_check_ttl_device_tree(ds_tree_t *tree)
{
    struct per_device_cache *pdevice, *current;

    LOGT("%s(): cache entries before flushing expired TTL entries: %lu", __func__, gk_get_cache_count());

    pdevice = ds_tree_head(tree);
    while (pdevice != NULL)
    {
        current = pdevice;
        pdevice = ds_tree_next(tree, pdevice);
        gk_cache_check_ttl_per_device(current);
    }
    LOGT("%s(): cache entries after flushing expired TTL entries: %lu", __func__, gk_get_cache_count());
}


/**
 * @brief deletes the attribute from the attr
 *        tree
 *
 * @params: req attribute interface structure with the
 *          attribute value to delete
 * @params: attr_tree attribute tree pointer
 * @return: true if success false if failed
 */
static bool
gkc_del_attr(ds_tree_t *attr_tree, struct gk_attr_cache_interface *req)
{
    struct attr_cache *attr_entry;

    attr_entry = ds_tree_find(attr_tree, &req->cache_key);
    if (attr_entry == NULL)
    {
        LOGD("%s(): attribute not found in the tree", __func__);
        return false;
    }

    LOGT("%s: deleting attribute %s for device " PRI_os_macaddr_lower_t " ",
         __func__,
         gk_get_attribute_value(attr_entry, req->attribute_type),
         FMT_os_macaddr_pt(req->device_mac));

    gkc_free_attr_entry(attr_entry, req->attribute_type);
    gkc_remove_entry(attr_tree, attr_entry);

    return true;
}

/**
 * @brief delete if the attribute from the device
 *        tree
 *
 * @params: req attribute interface structure with the
 *          attribute value to delete
 * @params: pdevice pointer to device structure
 * @return: true if success false if failed
 */
bool
gkc_del_attr_from_dev(struct per_device_cache *pdevice, struct gk_attr_cache_interface *req)
{
    bool ret = false;

    switch (req->attribute_type)
    {
        case GK_CACHE_INTERNAL_TYPE_HOSTNAME:
            LOGI("%s(): Delete attr using GK_CACHE_INTERNAL_TYPE_HOSTNAME", __func__);
            /* fallthru */
        case GK_CACHE_REQ_TYPE_FQDN:
        case GK_CACHE_REQ_TYPE_HOST:
        case GK_CACHE_REQ_TYPE_SNI:
            ret = gkc_del_attr(&pdevice->hostname_tree, req);
            break;

        case GK_CACHE_REQ_TYPE_URL:
            ret = gkc_del_attr(&pdevice->url_tree, req);
            break;

        case GK_CACHE_REQ_TYPE_IPV4:
            ret = gkc_del_attr(&pdevice->ipv4_tree, req);
            break;

        case GK_CACHE_REQ_TYPE_IPV6:
            ret = gkc_del_attr(&pdevice->ipv6_tree, req);
            break;

        case GK_CACHE_REQ_TYPE_APP:
            ret = gkc_del_attr(&pdevice->app_tree, req);
            break;

        default:
            LOGD("%s(): invalid attribute type %d", __func__, req->attribute_type);
            break;
    }

    return ret;
}
