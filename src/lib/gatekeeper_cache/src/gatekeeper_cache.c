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
#include <time.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "gatekeeper_cache.h"
#include "os_types.h"
#include "memutil.h"
#include "os.h"

static struct gk_cache_mgr mgr = {
    .initialized = false,
};

struct gk_cache_mgr *
gk_cache_get_mgr(void)
{
    return &mgr;
}

/**
 * @brief comparator function for comparing mac adress
 *
 * @params: _a: first MAC address to compare
 * @params: -b: second MAC address to compare
 *
 * @return true for success and false for failure.
 */
static int
gkc_mac_addr_cmp(void *_a, void *_b)
{
    os_macaddr_t *a = _a;
    os_macaddr_t *b = _b;

    return memcmp(a->addr, b->addr, sizeof(a->addr));
}

/**
 * @brief comparator function for 5-tuple
 *
 * @params: _a: first 5-tuple structure to compare
 * @params: -b: second 5-tuple structure to compare
 *
 * @return 0 if equal
 */
int
gkc_flow_entry_cmp(void *_a, void *_b)
{
    struct ip_flow_cache *key_a = _a;
    struct ip_flow_cache *key_b = _b;
    int ipl;
    int cmp;

    /* compare IP address */
    cmp = (int)(key_a->ip_version) - (int)(key_b->ip_version);
    if (cmp != 0) return cmp;

    /* get ip version compare len */
    ipl = (key_a->ip_version == 4 ? 4 : 16);
    if (cmp != 0) return cmp;

    /* Compare source IP addresses */
    cmp = memcmp(key_a->src_ip_addr, key_b->src_ip_addr, ipl);
    if (cmp != 0) return cmp;

    /* Compare destination IP addresses */
    cmp = memcmp(key_a->dst_ip_addr, key_b->dst_ip_addr, ipl);
    if (cmp != 0) return cmp;

    /* Compare ip protocols */
    cmp = (int)(key_a->protocol) - (int)(key_b->protocol);
    if (cmp != 0) return cmp;

    /* Compare source ports */
    cmp = (int)(key_a->src_port) - (int)(key_b->src_port);
    if (cmp != 0) return cmp;

    /* Compare destination ports */
    cmp = (int)(key_a->dst_port) - (int)(key_b->dst_port);
    return cmp;
}

/**
 * @brief initialize per device structure.
 *        Initializes the attributes and flow trees.
 *
 * @params: device_mac: device mac address
 *
 * @return pointer to per_device_cache on success
 *         NULL on failure
 */
static struct per_device_cache *
gkc_init_per_dev(os_macaddr_t *device_mac)
{
    struct per_device_cache *pdevice_cache;
    if (!device_mac) return NULL;

    /* initialize per device structure */
    pdevice_cache = calloc(1, sizeof(struct per_device_cache));
    if (pdevice_cache == NULL) return NULL;

    pdevice_cache->device_mac = calloc(1, sizeof(os_macaddr_t));
    if (pdevice_cache->device_mac == NULL) goto error;

    memcpy(pdevice_cache->device_mac, device_mac, sizeof(os_macaddr_t));

    ds_tree_init(&pdevice_cache->fqdn_tree,
                 ds_str_cmp,
                 struct attr_cache,
                 attr_tnode);
    ds_tree_init(&pdevice_cache->url_tree,
                 ds_str_cmp,
                 struct attr_cache,
                 attr_tnode);
    ds_tree_init(&pdevice_cache->host_tree,
                 ds_str_cmp,
                 struct attr_cache,
                 attr_tnode);
    ds_tree_init(&pdevice_cache->sni_tree,
                 ds_str_cmp,
                 struct attr_cache,
                 attr_tnode);
    ds_tree_init(&pdevice_cache->ipv4_tree,
                 ds_str_cmp,
                 struct attr_cache,
                 attr_tnode);
    ds_tree_init(&pdevice_cache->ipv6_tree,
                 ds_str_cmp,
                 struct attr_cache,
                 attr_tnode);
    ds_tree_init(&pdevice_cache->app_tree,
                 ds_str_cmp,
                 struct attr_cache,
                 attr_tnode);
    ds_tree_init(&pdevice_cache->outbound_tree,
                 gkc_flow_entry_cmp,
                 struct ip_flow_cache,
                 ipflow_tnode);
    ds_tree_init(&pdevice_cache->inbound_tree,
                 gkc_flow_entry_cmp,
                 struct ip_flow_cache,
                 ipflow_tnode);

    return pdevice_cache;

error:
    free(pdevice_cache);
    return NULL;
}

/**
 * @brief initialize gk_cache manager.
 */
void
gk_cache_init_mgr(struct gk_cache_mgr *mgr)
{
    if (!mgr) return;
    if (mgr->initialized) return;

    /* initialize per device tree */
    ds_tree_init(&mgr->per_device_tree,
                 gkc_mac_addr_cmp,
                 struct per_device_cache,
                 perdevice_tnode);

    mgr->initialized = true;

    return;
}

/**
 * @brief adds redirect entry details to cache
 *
 * @params: input: contains redirect entries to be added
 *
 * @params: new_entry pointer to cache entry
 */
void
gk_add_new_redirect_entry(struct gk_attr_cache_interface *input,
                          struct attr_cache *new_entry)
{
    struct fqdn_redirect_s *in_redirect;
    struct fqdn_redirect_s *new_redirect;

    if (input->fqdn_redirect == NULL) return;
    in_redirect  = input->fqdn_redirect;

    new_entry->fqdn_redirect = CALLOC(1, sizeof(*new_redirect));
    if (new_entry->fqdn_redirect == NULL) return;

    new_redirect = new_entry->fqdn_redirect;

    /* return if redirection is not required */
    if (in_redirect->redirect == false) return;

    new_redirect->redirect     = in_redirect->redirect;
    new_redirect->redirect_ttl = in_redirect->redirect_ttl;
    STRSCPY(new_redirect->redirect_ips[0], in_redirect->redirect_ips[0]);
    STRSCPY(new_redirect->redirect_ips[1], in_redirect->redirect_ips[1]);
}

/**
 * @brief create a new attribute entry fo the given
 *        attribute type.
 *
 * @params: entry: specifing the attribute type
 *
 * @return return pointer to created attribute struct
 *         NULL on failure
 */
static struct attr_cache*
gkc_new_attr_entry(struct gk_attr_cache_interface *entry)
{
    struct attr_cache *new_attr;

    new_attr = calloc(1, sizeof(struct attr_cache));
    if (new_attr == NULL) return NULL;

    switch (entry->attribute_type)
    {
    case GK_CACHE_REQ_TYPE_FQDN:
        new_attr->attr.fqdn = strdup(entry->attr_name);
        gk_add_new_redirect_entry(entry, new_attr);
        break;

    case GK_CACHE_REQ_TYPE_URL:
        new_attr->attr.url = strdup(entry->attr_name);
        break;

    case GK_CACHE_REQ_TYPE_HOST:
        new_attr->attr.host = strdup(entry->attr_name);
        break;

    case GK_CACHE_REQ_TYPE_SNI:
        new_attr->attr.sni = strdup(entry->attr_name);
        break;

    case GK_CACHE_REQ_TYPE_IPV4:
        new_attr->attr.ipv4 = strdup(entry->attr_name);
        break;

    case GK_CACHE_REQ_TYPE_IPV6:
        new_attr->attr.ipv6 = strdup(entry->attr_name);
        break;

    case GK_CACHE_REQ_TYPE_APP:
        new_attr->attr.app_name = strdup(entry->attr_name);
        break;

    default:
        return NULL;
        break;
    }

    /* add entry creation time and provided TTL value */
    new_attr->cache_ts  = time(NULL);
    new_attr->cache_ttl = entry->cache_ttl;

    /* set action value: allow or block */
    new_attr->action = entry->action;
    new_attr->categorized = entry->categorized;
    new_attr->category_id = entry->category_id;
    new_attr->confidence_level = entry->confidence_level;
    if (entry->gk_policy)
    {
        new_attr->gk_policy = strdup(entry->gk_policy);
    }

    return new_attr;
}

/**
 * @brief add the attribute to per device tree.
 *
 * @params: pdevice_cache: device tree structure
 * @params: entry: interface structure with input values
 *
 */
static bool
gkc_add_attr_tree(struct per_device_cache *pdevice_cache,
                  struct gk_attr_cache_interface *entry)
{
    struct attr_cache *new_attr_cache;
    struct gk_cache_mgr *mgr;
    bool ret = true;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return false;

    new_attr_cache = gkc_new_attr_entry(entry);
    if (new_attr_cache == NULL) return false;

    switch (entry->attribute_type)
    {
    case GK_CACHE_REQ_TYPE_FQDN:
            ds_tree_insert(
                &pdevice_cache->fqdn_tree, new_attr_cache, new_attr_cache->attr.fqdn);
        break;

    case GK_CACHE_REQ_TYPE_URL:
            ds_tree_insert(
                &pdevice_cache->url_tree, new_attr_cache, new_attr_cache->attr.url);
        break;

    case GK_CACHE_REQ_TYPE_HOST:
            ds_tree_insert(
                &pdevice_cache->host_tree, new_attr_cache, new_attr_cache->attr.host);
        break;

    case GK_CACHE_REQ_TYPE_SNI:
            ds_tree_insert(
                &pdevice_cache->sni_tree, new_attr_cache, new_attr_cache->attr.sni);
        break;

    case GK_CACHE_REQ_TYPE_IPV4:
            ds_tree_insert(
                &pdevice_cache->ipv4_tree, new_attr_cache, new_attr_cache->attr.ipv4);
        break;

    case GK_CACHE_REQ_TYPE_IPV6:
            ds_tree_insert(
                &pdevice_cache->ipv6_tree, new_attr_cache, new_attr_cache->attr.ipv6);
        break;

    case GK_CACHE_REQ_TYPE_APP:
            ds_tree_insert(
                &pdevice_cache->app_tree, new_attr_cache, new_attr_cache->attr.app_name);
        break;

    default:
        ret = false;
        break;
    }

    return ret;
}

/**
 * @brief initializes the per device tree if not initialized and then
 *        adds the attribute to it.
 *
 * @params: entry: interface structure with input values
 * @return: true or success false on failure
 */
bool
gkc_add_attribute_entry(struct gk_attr_cache_interface *entry)
{
    struct gk_cache_mgr *mgr;
    struct per_device_cache *pdevice_cache;
    int ret;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return false;

    ret = gkc_lookup_attribute_entry(entry, false);
    if (ret)
    {
        LOGT("%s(): attribute entry " PRI_os_macaddr_lower_t " already present",
             __func__, FMT_os_macaddr_pt(entry->device_mac));
        return false;
    }

    if (mgr->count >= GK_MAX_CACHE_ENTRIES)
    {
        LOGN("%s() max cache entries %" PRIu64 " reached, cannot add entry to GK cache",
             __func__, mgr->count);
        return false;
    }

    /* return if attribute type is not valid */
    if (entry->attribute_type < GK_CACHE_REQ_TYPE_FQDN
        || entry->attribute_type > GK_CACHE_REQ_TYPE_APP)
        return false;

    pdevice_cache = ds_tree_find(&mgr->per_device_tree, entry->device_mac);
    if (pdevice_cache == NULL)
    {
        /* create a new per device tree */
        pdevice_cache = gkc_init_per_dev(entry->device_mac);
        if (pdevice_cache == NULL) return false;

        ds_tree_insert(
            &mgr->per_device_tree, pdevice_cache, pdevice_cache->device_mac);
    }

    ret = gkc_add_attr_tree(pdevice_cache, entry);
    if (ret == false) return false;

     /* increment cache entries counter */
    mgr->count++;

    if (entry->action == FSM_ALLOW)
    {
        pdevice_cache->allowed[entry->attribute_type]++;
    }
    else
    {
        pdevice_cache->blocked[entry->attribute_type]++;
    }

    return true;
}

/**
 * @brief initializes the per device tree if not initialized and then
 *        adds the IP flow entry to it.
 *
 * @params: entry: IP flow interface structure with input values
 * @return: true or success false on failure
 */
bool
gkc_add_flow_entry(struct gkc_ip_flow_interface *entry)
{
    struct per_device_cache *pdevice;
    enum gk_cache_request_type attr_type;
    struct gk_cache_mgr *mgr;
    int ret;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return false;

    if (!entry->device_mac) return false;

    ret = gkc_lookup_flow(entry, false);
    if (ret)
    {
        LOGT("%s(): entry " PRI_os_macaddr_lower_t " already present",
             __func__, FMT_os_macaddr_pt(entry->device_mac));
        return false;
    }

    if (mgr->count >= GK_MAX_CACHE_ENTRIES)
    {
        LOGN("%s() max cache entries of %" PRIu64 " reached, cannot add entry to GK cache",
             __func__, mgr->count);
        return false;
    }

    pdevice = ds_tree_find(&mgr->per_device_tree, entry->device_mac);
    if (pdevice == NULL)
    {
        /* create a new per device tree */
        pdevice = gkc_init_per_dev(entry->device_mac);
        if (pdevice == NULL) return false;

        ds_tree_insert(&mgr->per_device_tree, pdevice, pdevice->device_mac);
    }

    ret = gkc_add_flow_tree(pdevice, entry);
    if (ret == false) return false;

    /* increment cache entries counter */
    mgr->count++;

    /* set the attribute type */
    attr_type = (entry->direction == GKC_FLOW_DIRECTION_INBOUND
                 ? GK_CACHE_REQ_TYPE_INBOUND
                 : GK_CACHE_REQ_TYPE_OUTBOUDND);

    if (entry->action == FSM_ALLOW)
    {
        pdevice->allowed[attr_type]++;
    }
    else
    {
        pdevice->blocked[attr_type]--;
    }

    return true;
}

/**
 * @brief look up the device tree to the find the given
 *        device.
 *
 * @params: device_mac: mac address of the device
 * @return: pointer to per_device_cache if found else NULL
 */
static struct per_device_cache *
gkc_lookup_device_tree(os_macaddr_t *device_mac)
{
    struct per_device_cache *pdevice_cache;
    struct gk_cache_mgr *mgr;

    if (!device_mac) return NULL;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return NULL;

    pdevice_cache = ds_tree_find(&mgr->per_device_tree, device_mac);
    return pdevice_cache;
}

/**
 * @brief check if the given flow is present in the cache
 *
 * @params: req: IP flow interface structure with the flow
 *          tuple to find
 * @return: true if found false if not present
 */
bool
gkc_lookup_flow(struct gkc_ip_flow_interface *req, int update_count)
{
    struct per_device_cache *pdevice;
    int ret;

    if (!req) return false;
    ret = gkc_is_input_valid(req);
    if (ret == false) return false;

    /* look up the per device tree first */
    pdevice = gkc_lookup_device_tree(req->device_mac);
    if (pdevice == NULL) return false;

    /* lookup flows tree */
    ret = gkc_lookup_flows_for_device(pdevice, req, update_count);

    return ret;
}

/**
 * @brief remove the give flow from the cache
 *
 * @params: req: IP flow interface structure with the flow
 *          tuple to delete
 * @return: true if success false is failed
 */
bool
gkc_del_flow(struct gkc_ip_flow_interface *req)
{
    struct per_device_cache *pdevice;
    struct gk_cache_mgr *mgr;
    int ret;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return false;

    ret = gkc_is_input_valid(req);
    if (ret == false) return false;

    /* first check if the device is present */
    pdevice = gkc_lookup_device_tree(req->device_mac);
    if (pdevice == NULL) return false;

    ret = gkc_del_flow_from_dev(pdevice, req);
    if (ret == false) return false;

    mgr->count--;
    return ret;
}

/**
 * @brief initialize gk_cache.
 *
 * receive none
 *
 * @return true for success and false for failure.
 */
bool
gk_cache_init(void)
{
    struct gk_cache_mgr *mgr;

    mgr = gk_cache_get_mgr();
    gk_cache_init_mgr(mgr);

    return true;
}

/**
 * @brief cleanup allocated memory.
 *
 * receive none
 *
 * @return void.
 */
void
gkc_cleanup_mgr(void)
{
    struct gk_cache_mgr *mgr = gk_cache_get_mgr();

    if (!mgr->initialized) return;
    gk_cache_cleanup();
    mgr->initialized = false;
    mgr->count = 0;
}

void
gkc_lookup_redirect_entry(struct gk_attr_cache_interface *req,
                          struct attr_cache *attr_entry)
{
    if (req->attribute_type != GK_CACHE_REQ_TYPE_FQDN) return;

    if (req->fqdn_redirect == NULL || attr_entry->fqdn_redirect == NULL) return;

    req->fqdn_redirect->redirect     = attr_entry->fqdn_redirect->redirect;
    req->fqdn_redirect->redirect_ttl = attr_entry->fqdn_redirect->redirect_ttl;
    STRSCPY(req->fqdn_redirect->redirect_ips[0],
            attr_entry->fqdn_redirect->redirect_ips[0]);
    STRSCPY(req->fqdn_redirect->redirect_ips[1],
            attr_entry->fqdn_redirect->redirect_ips[1]);
}

/**
 * @brief check if the attribute is present in the attribute
 *        tree
 *
 * @params: req: attribute interface structure with the
 *          attribute value to find
 * @return: true if found false if not present
 */
static bool
gkc_lookup_attr_tree(ds_tree_t *tree, struct gk_attr_cache_interface *req, int update_count)
{
    struct attr_cache *attr_entry;

    if (!req->attr_name) return false;

    attr_entry = ds_tree_find(tree, req->attr_name);
    if (attr_entry == NULL) return false;

    /* increment the hit counter for this attribute */
    if (update_count)
    {
        attr_entry->hit_count++;
    }

    /* update the request with hit counter */
    req->hit_counter = attr_entry->hit_count;

    req->action = attr_entry->action;
    req->categorized = attr_entry->categorized;
    req->category_id = attr_entry->category_id;
    req->confidence_level = attr_entry->confidence_level;

    gkc_lookup_redirect_entry(req, attr_entry);

    if (attr_entry->gk_policy != NULL)
    {
        req->gk_policy = strdup(attr_entry->gk_policy);
    }

    return true;
}


/**
 * @brief check if the attribute is present in either of
 *        FQDN, HOST or SNI cache.
 *
 * @params: req: attribute interface structure with the
 *          attribute value to find
 * @return: true if found false if not present
 */
static bool
gkc_lookup_common_tree(struct per_device_cache *pdevice,
                       struct gk_attr_cache_interface *req,
                       int update_count)
{
    int ret = false;

    /* Lookup in FQDN tree and return if found */
    ret = gkc_lookup_attr_tree(&pdevice->fqdn_tree, req, update_count);
    if (ret) return ret;

    /* Lookup in Host tree and return if found */
    ret = gkc_lookup_attr_tree(&pdevice->host_tree, req, update_count);
    if (ret) return ret;

    /* Lookup in SNI tree and return if found */
    ret = gkc_lookup_attr_tree(&pdevice->sni_tree, req, update_count);
    if (ret) return ret;

    /* Not found in either FQDN, host or sni tree */
    return false;
}

/**
 * @brief check if the attribute is present in the device
 *        tree
 *
 * @params: req: attribute interface structure with the
 *          attribute value to find
 * @return: true if found false if not present
 */
static bool
gkc_lookup_attributes_tree(struct per_device_cache *pdevice,
                           struct gk_attr_cache_interface *req,
                           int update_count)
{
    int ret = false;

    switch (req->attribute_type)
    {
    case GK_CACHE_REQ_TYPE_FQDN:
        ret = gkc_lookup_common_tree(pdevice, req, update_count);
        break;

    case GK_CACHE_REQ_TYPE_URL:
        ret = gkc_lookup_attr_tree(&pdevice->url_tree, req, update_count);
        break;

    case GK_CACHE_REQ_TYPE_HOST:
        ret = gkc_lookup_common_tree(pdevice, req, update_count);
        break;

    case GK_CACHE_REQ_TYPE_SNI:
        ret = gkc_lookup_common_tree(pdevice, req, update_count);
        break;

    case GK_CACHE_REQ_TYPE_IPV4:
        ret = gkc_lookup_attr_tree(&pdevice->ipv4_tree, req, update_count);
        break;

    case GK_CACHE_REQ_TYPE_IPV6:
        ret = gkc_lookup_attr_tree(&pdevice->ipv6_tree, req, update_count);
        break;

    case GK_CACHE_REQ_TYPE_APP:
        ret = gkc_lookup_attr_tree(&pdevice->app_tree, req, update_count);
        break;

    default:
        break;
    }

    return ret;
}

/**
 * @brief Lookup cached action for given attribute
 *
 * receive ipaddress and mac of device.
 *
 * output action.
 *
 * @return true for success and false for failure.
 */
bool
gkc_lookup_attribute_entry(struct gk_attr_cache_interface *req, bool update_count)
{
    struct per_device_cache *pdevice;
    bool ret;

    if (!req || !req->device_mac) return false;

    if (req->attribute_type < GK_CACHE_REQ_TYPE_FQDN
        || req->attribute_type >= GK_CACHE_MAX_REQ_TYPES)
        return false;

    /* look up the per device tree first */
    pdevice = gkc_lookup_device_tree(req->device_mac);
    if (pdevice == NULL) return false;

    /* lookup the attributes tree */
    ret = gkc_lookup_attributes_tree(pdevice, req, update_count);
    if (ret == false) return ret;

    /* update the time statmp */
    pdevice->req_counter[req->attribute_type] += 1;

    return ret;
}

/**
 * @brief Delete fqdn entry.
 *
 * receive gk_cache_entry.
 *
 * @return true for success and false for failure.
 */
bool
gkc_del_attribute(struct gk_attr_cache_interface *req)
{
    struct per_device_cache *pdevice;
    struct gk_cache_mgr *mgr;
    int ret;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return false;

    if (!req || !req->device_mac) return false;

    /* first check if the device is present */
    pdevice = gkc_lookup_device_tree(req->device_mac);
    if (pdevice == NULL) return false;

    ret = gkc_del_attr_from_dev(pdevice, req);
    if (ret == false) return false;

    mgr->count--;

    return ret;
}

/**
 * @brief remove old cache entres.
 *
 * @param ttl the cache entry time to live
 */
void
gkc_ttl_cleanup(void)
{
    struct gk_cache_mgr *mgr;
    ds_tree_t *tree;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return;

    tree = &mgr->per_device_tree;
    gk_cache_check_ttl_device_tree(tree);
}

/**
 * @brief returns the number of entries present in the cache
 *
 * @return number of entries present in the cache
 */
unsigned long
gk_get_cache_count(void)
{
    struct gk_cache_mgr *mgr;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return 0;

    return mgr->count;
}

/**
 * @brief get the count of the devices stored in cache
 *
 * @return returns the device count value
 */
int
gk_get_device_count(void)
{
    struct per_device_cache *pdevice;
    struct gk_cache_mgr *mgr;
    ds_tree_t *tree;

    int count = 0;
    mgr       = gk_cache_get_mgr();
    tree      = &mgr->per_device_tree;

    if (!mgr->initialized) return count;

    ds_tree_foreach(tree, pdevice)
    {
        count++;
    }

    return count;
}

/**
 * @brief get the count of devices having allowed action
 *
 * @params: device_mac mac address of the device
 * @params: attr_type attribute type
 * @return counter value of the device.
 */
uint64_t
gkc_get_allowed_counter(os_macaddr_t *device_mac,
                        enum gk_cache_request_type attr_type)
{
    struct per_device_cache *pdevice;

    if (!device_mac) return 0;

    if (attr_type < GK_CACHE_REQ_TYPE_FQDN
        || attr_type >= GK_CACHE_MAX_REQ_TYPES)
        return 0;

    /* look up the per device tree first */
    pdevice = gkc_lookup_device_tree(device_mac);
    if (pdevice == NULL) return 0;

    return pdevice->allowed[attr_type];
}

/**
 * @brief check ttl value for the given attribue
 *        or flow tree
 *
 * @params: attribute or flow tree
 *
 * @return void.
 */
uint64_t
gkc_get_blocked_counter(os_macaddr_t *device_mac,
                        enum gk_cache_request_type attr_type)
{
    struct per_device_cache *pdevice;

    if (!device_mac) return 0;

    if (attr_type < GK_CACHE_REQ_TYPE_FQDN
        || attr_type >= GK_CACHE_MAX_REQ_TYPES)
        return 0;

    /* look up the per device tree first */
    pdevice = gkc_lookup_device_tree(device_mac);
    if (pdevice == NULL) return 0;

    return pdevice->blocked[attr_type];
}

static void
dump_attr_tree(ds_tree_t *tree, enum gk_cache_request_type req_type)
{
    struct attr_cache *entry;

    if (tree == NULL) return;

    ds_tree_foreach(tree, entry)
    {
        switch (req_type)
        {
        case GK_CACHE_REQ_TYPE_FQDN:
            LOGT("\t\t\t %s", entry->attr.fqdn);
            break;

        case GK_CACHE_REQ_TYPE_URL:
            LOGT("\t\t\t %s", entry->attr.url);
            break;

        case GK_CACHE_REQ_TYPE_HOST:
            LOGT("\t\t\t %s", entry->attr.host);
            break;

        case GK_CACHE_REQ_TYPE_SNI:
            LOGT("\t\t\t %s", entry->attr.sni);
            break;

        case GK_CACHE_REQ_TYPE_IPV4:
            LOGT("\t\t\t %s", entry->attr.ipv4);
            break;

        case GK_CACHE_REQ_TYPE_IPV6:
            LOGT("\t\t\t %s", entry->attr.ipv6);
            break;

        case GK_CACHE_REQ_TYPE_APP:
            LOGT("\t\t\t %s", entry->attr.app_name);
            break;

        default:
            break;
        }
    }
}

static void
dump_flow_tree(ds_tree_t *tree)
{
    char src_ip_str[INET6_ADDRSTRLEN];
    char dst_ip_str[INET6_ADDRSTRLEN];
    int domain;

    struct ip_flow_cache *entry;

    if (tree == NULL) return;

    ds_tree_foreach(tree, entry)
    {
        domain = (entry->ip_version == 4 ? AF_INET : AF_INET6);
        inet_ntop(domain, entry->src_ip_addr, src_ip_str, sizeof(src_ip_str));
        inet_ntop(domain, entry->dst_ip_addr, dst_ip_str, sizeof(dst_ip_str));
        LOGT("src ip %s, dst ip: %s sport: %d, dport: %d proto: %d action: %d",
             src_ip_str,
             dst_ip_str,
             entry->src_port,
             entry->dst_port,
             entry->protocol,
             entry->action);
    }
}

/**
 * @brief print cache'd entres.
 *
 */
void
gkc_cache_entries(void)
{
    struct gk_cache_mgr *mgr;
    struct per_device_cache *entry;
    ds_tree_t *tree;
    ds_tree_t *subtree = NULL;

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return;

    tree = &mgr->per_device_tree;

    LOGT("%s: gatekeeper_cache dump", __func__);
    LOGT("=====START=====");

    ds_tree_foreach(tree, entry)
    {
        LOGT(
            "--------------------------------------------------------------------------------------------------");
        LOGT("\t \t Printing cache entries for device " PRI_os_macaddr_lower_t
             " ",
             FMT_os_macaddr_pt(entry->device_mac));
        LOGT(
            "--------------------------------------------------------------------------------------------------");
        subtree = &entry->fqdn_tree;
        LOGT("\t FQDN Entries : \n");
        dump_attr_tree(subtree, GK_CACHE_REQ_TYPE_FQDN);

        subtree = &entry->url_tree;
        LOGT("\t URL Entries : \n");
        dump_attr_tree(subtree, GK_CACHE_REQ_TYPE_URL);

        subtree = &entry->host_tree;
        LOGT("\t HOST Entries : \n");
        dump_attr_tree(subtree, GK_CACHE_REQ_TYPE_HOST);

        subtree = &entry->sni_tree;
        LOGT("\t SNI Entries : \n");
        dump_attr_tree(subtree, GK_CACHE_REQ_TYPE_SNI);

        subtree = &entry->ipv4_tree;
        LOGT("\t IPv4 Entries : \n");
        dump_attr_tree(subtree, GK_CACHE_REQ_TYPE_IPV4);

        subtree = &entry->ipv6_tree;
        LOGT("\t IPv6 Entries : \n");
        dump_attr_tree(subtree, GK_CACHE_REQ_TYPE_IPV6);

        subtree = &entry->app_tree;
        LOGT("\t APP Name Entries : \n");
        dump_attr_tree(subtree, GK_CACHE_REQ_TYPE_APP);

        subtree = &entry->inbound_tree;
        LOGT("\t Inbound Entries : \n");
        dump_flow_tree(subtree);

        subtree = &entry->outbound_tree;
        LOGT("\t Outbound Entries : \n");
        dump_flow_tree(subtree);
    }
    LOGT("=====END=====");
    return;
}

void
clear_gatekeeper_cache(void)
{
    LOGN("clearing gate keeper cache");
    gk_cache_cleanup();
}
