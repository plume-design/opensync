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

#include "os_nif.h"
#include "os.h"
#include "gatekeeper_cache.h"
#include "osp_ps.h"
#include "gatekeeper.pb-c.h"
#include "gatekeeper_bulk_reply_msg.h"

/* Entry type indices for counts array */
#define URL_IDX   0
#define FQDN_IDX  1
#define HOST_IDX  2
#define SNI_IDX   3
#define IPV4_IDX  4
#define IPV6_IDX  5
#define APP_IDX   6
#define NUM_TYPES 7

/******************************************************************************
 *  PRIVATE definitions
 *****************************************************************************/
/**
 * @brief Free a common reply header
 *
 * @param header pointer to the header to free
 */
static void free_common_header(Gatekeeper__Southbound__V1__GatekeeperCommonReply *header)
{
    if (header == NULL) return;

    FREE(header->device_id.data);
    FREE(header->policy);
    FREE(header->network_id);

    FREE(header);
}

static int gk_fsm_action_to_proto_action(int action)
{
    switch (action)
    {
        case FSM_ACTION_NONE:
            return GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_UNSPECIFIED;
        case FSM_ALLOW:
            return GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT;
        case FSM_BLOCK:
            return GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_BLOCK;
        case FSM_REDIRECT:
            return GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_REDIRECT;
        case FSM_REDIRECT_ALLOW:
            return GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_REDIRECT_ALLOW;
        case FSM_NOANSWER:
            return GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_NOANSWER;
        default:
            return GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_UNSPECIFIED;
    }
}

/**
 * @brief Create a common reply header from cache entry
 *
 * @param attr_entry pointer to attribute cache entry
 * @param pd_cache pointer to device cache
 * @return allocated header on success, NULL on failure
 */
static Gatekeeper__Southbound__V1__GatekeeperCommonReply *create_common_header(
        struct attr_cache *attr_entry,
        struct per_device_cache *pd_cache)
{
    Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;

    if (attr_entry == NULL || pd_cache == NULL)
    {
        LOGD("%s: Invalid arguments", __func__);
        return NULL;
    }

    header = CALLOC(1, sizeof(*header));

    gatekeeper__southbound__v1__gatekeeper_common_reply__init(header);

    /* Set header fields from cache entry */
    header->action = gk_fsm_action_to_proto_action(attr_entry->action);
    header->ttl = attr_entry->cache_ttl;
    header->policy = IS_NULL_PTR(attr_entry->gk_policy) ? NULL : STRDUP(attr_entry->gk_policy);
    header->category_id = attr_entry->category_id;
    header->confidence_level = attr_entry->confidence_level;
    header->flow_marker = attr_entry->flow_marker;
    header->network_id = IS_NULL_PTR(attr_entry->network_id) ? NULL : STRDUP(attr_entry->network_id);

    header->device_id.data = CALLOC(1, sizeof(os_macaddr_t));
    header->device_id.len = sizeof(os_macaddr_t);
    memcpy(header->device_id.data, pd_cache->device_mac, sizeof(os_macaddr_t));

    return header;
}

/**
 * @brief Count entries in cache trees and return totals
 *
 * @param device_tree pointer to the device tree
 * @param counts array to store counts for each type
 * @return total number of entries
 */
static int gk_count_cache_entries(ds_tree_t *device_tree, int counts[NUM_TYPES])
{
    struct per_device_cache *pd_cache;
    struct attr_cache *attr_entry;
    int total = 0;

    if (device_tree == NULL || counts == NULL)
    {
        LOGD("%s: Invalid arguments", __func__);
        return 0;
    }

    /* Initialize counts */
    for (int i = 0; i < NUM_TYPES; i++)
    {
        counts[i] = 0;
    }

    ds_tree_foreach (device_tree, pd_cache)
    {
        /* Count URL entries */
        ds_tree_foreach (&pd_cache->url_tree, attr_entry)
        {
            if (attr_entry->attr.url != NULL && attr_entry->attr.url->name != NULL)
            {
                counts[URL_IDX]++;
            }
        }

        /* Count hostname entries */
        ds_tree_foreach (&pd_cache->hostname_tree, attr_entry)
        {
            if (attr_entry->attr.host_name == NULL) continue;

            if (attr_entry->attr.host_name->added_by == GK_CACHE_REQ_TYPE_FQDN)
            {
                counts[FQDN_IDX]++;
            }
            else if (attr_entry->attr.host_name->added_by == GK_CACHE_REQ_TYPE_HOST)
            {
                counts[HOST_IDX]++;
            }
            else if (attr_entry->attr.host_name->added_by == GK_CACHE_REQ_TYPE_SNI)
            {
                counts[SNI_IDX]++;
            }

            // counts[HOSTNAME_IDX]++;
        }

        /* Count IPv4 entries */
        ds_tree_foreach (&pd_cache->ipv4_tree, attr_entry)
        {
            counts[IPV4_IDX]++;
        }

        /* Count IPv6 entries */
        ds_tree_foreach (&pd_cache->ipv6_tree, attr_entry)
        {
            counts[IPV6_IDX]++;
        }

        /* Count app entries */
        ds_tree_foreach (&pd_cache->app_tree, attr_entry)
        {
            counts[APP_IDX]++;
        }
    }

    /* Calculate total entries */
    for (int i = 0; i < NUM_TYPES; i++)
    {
        total += counts[i];
    }

    LOGD("%s: Cache entries - Total: %d ()URL: %d, FQDN: %d, HTTP Host: %d, HTTPS SNI: %d, IPv4: %d, IPv6: %d, App: %d",
         __func__,
         total,
         counts[URL_IDX],
         counts[FQDN_IDX],
         counts[HOST_IDX],
         counts[SNI_IDX],
         counts[IPV4_IDX],
         counts[IPV6_IDX],
         counts[APP_IDX]);

    return total;
}

static bool gk_process_ipv6_entries(
        struct per_device_cache *pd_cache,
        Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply,
        int *idx_ipv6)
{
    struct attr_cache *attr_entry;
    bool result = true;

    if (pd_cache == NULL || bulk_reply == NULL || idx_ipv6 == NULL)
    {
        LOGD("%s: Invalid arguments", __func__);
        return false;
    }

    ds_tree_foreach (&pd_cache->ipv6_tree, attr_entry)
    {
        if (attr_entry->attr.ipv6 == NULL)
        {
            continue;
        }

        /* Bounds check to avoid buffer overflow */
        if (*idx_ipv6 >= (int)bulk_reply->n_reply_ipv6)
        {
            LOGD("%s: IPv6 index out of bounds: %d >= %d", __func__, *idx_ipv6, (int)bulk_reply->n_reply_ipv6);
            result = false;
            break;
        }

        /* Create IPv6 reply */
        Gatekeeper__Southbound__V1__GatekeeperIpv6Reply *ipv6_reply;
        ipv6_reply = CALLOC(1, sizeof(*ipv6_reply));

        gatekeeper__southbound__v1__gatekeeper_ipv6_reply__init(ipv6_reply);

        /* Create and populate common header */
        Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;
        header = create_common_header(attr_entry, pd_cache);
        if (header == NULL)
        {
            LOGD("%s: Failed to create common header for IPv6 entry", __func__);
            FREE(ipv6_reply);
            result = false;
            break;
        }

        /* Handle IPv6 address: Extract IPv6 from sockaddr_in6 structure */
        struct sockaddr_in6 *ipv6_addr = (struct sockaddr_in6 *)attr_entry->attr.ipv6;
        if (ipv6_addr->sin6_family != AF_INET6)
        {
            LOGD("%s: Invalid address family for IPv6 entry", __func__);
            free_common_header(header);
            FREE(ipv6_reply);
            result = false;
            break;
        }

        /* Allocate and copy the IPv6 address (16 bytes) */
        ipv6_reply->addr_ipv6.data = CALLOC(1, sizeof(ipv6_addr->sin6_addr.s6_addr));
        if (ipv6_reply->addr_ipv6.data == NULL)
        {
            LOGD("%s: Memory allocation failed for IPv6 address", __func__);
            free_common_header(header);
            FREE(ipv6_reply);
            result = false;
            break;
        }

        ipv6_reply->addr_ipv6.len = sizeof(ipv6_addr->sin6_addr.s6_addr);
        memcpy(ipv6_reply->addr_ipv6.data, ipv6_addr->sin6_addr.s6_addr, sizeof(ipv6_addr->sin6_addr.s6_addr));

        /* Attach header to IPv6 reply */
        ipv6_reply->header = header;

        /* Add to bulk reply array */
        bulk_reply->reply_ipv6[(*idx_ipv6)++] = ipv6_reply;
    }

    return result;
}

static bool gk_process_ipv4_entries(
        struct per_device_cache *pd_cache,
        Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply,
        int *idx_ipv4)
{
    struct attr_cache *attr_entry;
    bool result = true;

    if (pd_cache == NULL || bulk_reply == NULL || idx_ipv4 == NULL)
    {
        LOGD("%s: Invalid arguments", __func__);
        return false;
    }

    ds_tree_foreach (&pd_cache->ipv4_tree, attr_entry)
    {
        if (attr_entry->attr.ipv4 == NULL)
        {
            continue;
        }

        /* Bounds check to avoid buffer overflow */
        if (*idx_ipv4 >= (int)bulk_reply->n_reply_ipv4)
        {
            LOGD("%s: IPv4 index out of bounds: %d >= %d", __func__, *idx_ipv4, (int)bulk_reply->n_reply_ipv4);
            result = false;
            break;
        }

        /* Create IPv4 reply */
        Gatekeeper__Southbound__V1__GatekeeperIpv4Reply *ipv4_reply;
        ipv4_reply = CALLOC(1, sizeof(*ipv4_reply));

        gatekeeper__southbound__v1__gatekeeper_ipv4_reply__init(ipv4_reply);

        /* Create and populate common header */
        Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;
        header = create_common_header(attr_entry, pd_cache);
        if (header == NULL)
        {
            LOGD("%s: Failed to create common header for IPv4 entry", __func__);
            FREE(ipv4_reply);
            result = false;
            break;
        }

        /* Set IPv4 value using sockaddr_in structure */
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)attr_entry->attr.ipv4;
        if (ipv4->sin_family != AF_INET)
        {
            LOGD("%s: Invalid address family for IPv4 entry", __func__);
            free_common_header(header);
            FREE(ipv4_reply);
            result = false;
            break;
        }
        ipv4_reply->addr_ipv4 = ipv4->sin_addr.s_addr;

        /* Attach header to IPv4 reply */
        ipv4_reply->header = header;

        /* Add to bulk reply array */
        bulk_reply->reply_ipv4[(*idx_ipv4)++] = ipv4_reply;
    }

    return result;
}

static bool gk_process_fqdn_redirect(
        struct attr_cache *attr_entry,
        Gatekeeper__Southbound__V1__GatekeeperFqdnReply *fqdn_reply)
{
    if (attr_entry->fqdn_redirect == NULL) return true;

    Gatekeeper__Southbound__V1__GatekeeperFqdnRedirectReply *redirect;
    redirect = CALLOC(1, sizeof(*redirect));

    gatekeeper__southbound__v1__gatekeeper_fqdn_redirect_reply__init(redirect);

    /* Set redirect values if available */
    redirect->redirect_cname = (attr_entry->fqdn_redirect->redirect_cname != NULL)
                                       ? STRDUP(attr_entry->fqdn_redirect->redirect_cname)
                                       : NULL;

    /* Process any IPv4 redirect */
    if (strlen(attr_entry->fqdn_redirect->redirect_ips[0]) > 0)
    {
        struct in_addr addr;
        char *redirect_without_prefix = attr_entry->fqdn_redirect->redirect_ips[0];

        /* Remove any prefix */
        if (strncmp(redirect_without_prefix, "A-", 2) == 0)
        {
            redirect_without_prefix += 2;
        }

        if (inet_pton(AF_INET, redirect_without_prefix, &addr) == 1)
        {
            redirect->redirect_ipv4 = addr.s_addr;
            LOGD("%s: Successfully converted IPv4 redirect address: %s -> %s",
                 __func__,
                 attr_entry->fqdn_redirect->redirect_ips[0],
                 redirect_without_prefix);
        }
        else
        {
            LOGD("%s: Failed to convert IPv4 redirect address: %s",
                 __func__,
                 attr_entry->fqdn_redirect->redirect_ips[0]);
        }
    }

    /* Process any IPv6 redirect */
    if (strlen(attr_entry->fqdn_redirect->redirect_ips[1]) > 0)
    {
        struct in6_addr addr6;
        if (inet_pton(AF_INET6, attr_entry->fqdn_redirect->redirect_ips[1], &addr6) == 1)
        {
            redirect->redirect_ipv6.data = CALLOC(1, sizeof(addr6));
            redirect->redirect_ipv6.len = sizeof(addr6);
            memcpy(redirect->redirect_ipv6.data, &addr6, sizeof(addr6));
        }
    }

    /* Attach redirect to FQDN reply */
    fqdn_reply->redirect = redirect;
    return true;
}

static bool gk_create_host_entry(
        struct attr_cache *attr_entry,
        struct per_device_cache *pd_cache,
        int type,
        int *index_ptr,
        size_t max_entries,
        Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply)
{
    if (attr_entry->attr.host_name == NULL || attr_entry->attr.host_name->name == NULL)
    {
        LOGW("%s: Invalid host name entry, skipping", __func__);
        return false;
    }

    /* Bounds check */
    if (*index_ptr >= (int)max_entries)
    {
        LOGW("%s: Index out of bounds: %d >= %zu", __func__, *index_ptr, max_entries);
        return false;
    }

    /* create common header for all types */
    Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;

    header = create_common_header(attr_entry, pd_cache);
    if (header == NULL)
    {
        LOGD("%s: Failed to create common header for entry type %d", __func__, type);
        return false;
    }

    switch (type)
    {
        case GK_CACHE_REQ_TYPE_FQDN:
            LOGD("%s: Creating FQDN entry for '%s'", __func__, attr_entry->attr.host_name->name);
            Gatekeeper__Southbound__V1__GatekeeperFqdnReply *fqdn_reply;

            fqdn_reply = CALLOC(1, sizeof(*fqdn_reply));

            gatekeeper__southbound__v1__gatekeeper_fqdn_reply__init(fqdn_reply);
            fqdn_reply->header = header;
            fqdn_reply->query_name = STRDUP(attr_entry->attr.host_name->name);

            /* Process any FQDN redirect info */
            if (attr_entry->fqdn_redirect != NULL)
            {
                if (!gk_process_fqdn_redirect(attr_entry, fqdn_reply))
                {
                    free_common_header(header);
                    FREE(fqdn_reply->query_name);
                    FREE(fqdn_reply);
                    return false;
                }
            }

            bulk_reply->reply_fqdn[(*index_ptr)++] = fqdn_reply;
            LOGD("%s: Added FQDN '%s' at index %d", __func__, attr_entry->attr.host_name->name, *index_ptr - 1);
            break;

        case GK_CACHE_REQ_TYPE_HOST:
            LOGD("%s: Creating HTTP Host entry for '%s'", __func__, attr_entry->attr.host_name->name);
            Gatekeeper__Southbound__V1__GatekeeperHttpHostReply *http_host_reply;

            http_host_reply = CALLOC(1, sizeof(*http_host_reply));

            gatekeeper__southbound__v1__gatekeeper_http_host_reply__init(http_host_reply);
            http_host_reply->header = header;
            http_host_reply->http_host = STRDUP(attr_entry->attr.host_name->name);

            bulk_reply->reply_http_host[(*index_ptr)++] = http_host_reply;
            LOGD("%s: Added HTTP Host '%s' at index %d", __func__, attr_entry->attr.host_name->name, *index_ptr - 1);
            break;

        case GK_CACHE_REQ_TYPE_SNI:
            LOGD("%s: Creating HTTPS SNI entry for '%s'", __func__, attr_entry->attr.host_name->name);
            Gatekeeper__Southbound__V1__GatekeeperHttpsSniReply *https_sni_reply;

            https_sni_reply = CALLOC(1, sizeof(*https_sni_reply));

            gatekeeper__southbound__v1__gatekeeper_https_sni_reply__init(https_sni_reply);
            https_sni_reply->header = header;
            https_sni_reply->https_sni = STRDUP(attr_entry->attr.host_name->name);

            bulk_reply->reply_https_sni[(*index_ptr)++] = https_sni_reply;
            LOGD("%s: Added HTTPS SNI '%s' at index %d", __func__, attr_entry->attr.host_name->name, *index_ptr - 1);
            break;

        default:
            LOGD("%s: Unknown hostname type: %d for entry '%s'", __func__, type, attr_entry->attr.host_name->name);
            free_common_header(header);
            return false;
    }
    return true;
}

static bool gk_process_hostname_entries(
        struct per_device_cache *pd_cache,
        Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply,
        int *idx_fqdn,
        int *idx_http_host,
        int *idx_https_sni)
{
    struct attr_cache *attr_entry;
    int processed_count = 0;
    bool success = true;
    int type;

    if (pd_cache == NULL || pd_cache->device_mac == NULL)
    {
        LOGD("%s: Invalid arguments ", __func__);
        return false;
    }

    LOGD("%s: Processing indices FQDN:%d, HTTP:%d, SNI:%d for hostname: " PRI_os_macaddr_lower_t,
         __func__,
         *idx_fqdn,
         *idx_http_host,
         *idx_https_sni,
         FMT_os_macaddr_pt(pd_cache->device_mac));

    /* Iterate through all hostname entries */
    ds_tree_foreach (&pd_cache->hostname_tree, attr_entry)
    {
        processed_count++;

        if (attr_entry->attr.host_name == NULL || attr_entry->attr.host_name->name == NULL) continue;

        type = attr_entry->attr.host_name->added_by;

        switch (attr_entry->attr.host_name->added_by)
        {
            case GK_CACHE_REQ_TYPE_FQDN:
                success = gk_create_host_entry(
                        attr_entry,
                        pd_cache,
                        type,
                        idx_fqdn,
                        bulk_reply->n_reply_fqdn,
                        bulk_reply);
                break;

            case GK_CACHE_REQ_TYPE_HOST:
                success = gk_create_host_entry(
                        attr_entry,
                        pd_cache,
                        type,
                        idx_http_host,
                        bulk_reply->n_reply_http_host,
                        bulk_reply);
                break;

            case GK_CACHE_REQ_TYPE_SNI:
                success = gk_create_host_entry(
                        attr_entry,
                        pd_cache,
                        type,
                        idx_https_sni,
                        bulk_reply->n_reply_https_sni,
                        bulk_reply);
                break;

            default:
                success = false;
                LOGD("%s: Unknown hostname type: %d for entry '%s'",
                     __func__,
                     attr_entry->attr.host_name->added_by,
                     attr_entry->attr.host_name->name);
                break;
        }
    }

    LOGD("%s: Finished processing %d hostname entries - FQDN:%d/%d, HTTP Host:%d/%d, HTTPS SNI:%d/%d, result:%s",
         __func__,
         processed_count,
         *idx_fqdn,
         (int)bulk_reply->n_reply_fqdn,
         *idx_http_host,
         (int)bulk_reply->n_reply_http_host,
         *idx_https_sni,
         (int)bulk_reply->n_reply_https_sni,
         success ? "success" : "failure");

    return success;
}

static bool gk_process_app_entries(
        struct per_device_cache *pd_cache,
        Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply,
        int *idx_app)
{
    struct attr_cache *attr_entry;
    bool result = true;

    if (pd_cache == NULL || bulk_reply == NULL || idx_app == NULL)
    {
        LOGD("%s: Invalid arguments", __func__);
        return false;
    }

    ds_tree_foreach (&pd_cache->app_tree, attr_entry)
    {
        if (attr_entry->attr.app_name == NULL || attr_entry->attr.app_name->name == NULL)
        {
            continue;
        }

        /* Bounds check to avoid buffer overflow */
        if (*idx_app >= (int)bulk_reply->n_reply_app)
        {
            LOGD("%s: App index out of bounds: %d >= %d", __func__, *idx_app, (int)bulk_reply->n_reply_app);
            result = false;
            break;
        }

        /* Create app reply */
        Gatekeeper__Southbound__V1__GatekeeperAppReply *app_reply;
        app_reply = CALLOC(1, sizeof(*app_reply));

        gatekeeper__southbound__v1__gatekeeper_app_reply__init(app_reply);

        /* Create and populate common header */
        Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;
        header = create_common_header(attr_entry, pd_cache);
        if (header == NULL)
        {
            LOGD("%s: Failed to create common header for App entry", __func__);
            free(app_reply);
            result = false;
            break;
        }

        /* Set app value */
        app_reply->app_name = STRDUP(attr_entry->attr.app_name->name);

        /* Attach header to App reply */
        app_reply->header = header;

        /* Add to bulk reply array */
        bulk_reply->reply_app[(*idx_app)++] = app_reply;
    }

    return result;
}

/**
 * @brief Process URL entries for a device
 *
 * @param pd_cache pointer to the device cache
 * @param bulk_reply pointer to the bulk reply
 * @param idx_url pointer to the URL index counter
 * @return true on success, false on failure
 */

static bool gk_process_url_entries(
        struct per_device_cache *pd_cache,
        Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply,
        int *idx_url)
{
    struct attr_cache *attr_entry;
    bool result = true;

    if (pd_cache == NULL || bulk_reply == NULL || idx_url == NULL)
    {
        LOGD("%s: Invalid arguments", __func__);
        return false;
    }

    ds_tree_foreach (&pd_cache->url_tree, attr_entry)
    {
        if (attr_entry->attr.url == NULL || attr_entry->attr.url->name == NULL) continue;

        /* Bounds check to avoid buffer overflow */
        if (*idx_url >= (int)bulk_reply->n_reply_http_url)
        {
            LOGD("%s: URL index out of bounds: %d >= %d", __func__, *idx_url, (int)bulk_reply->n_reply_http_url);
            result = false;
            break;
        }

        /* Create URL reply */
        Gatekeeper__Southbound__V1__GatekeeperHttpUrlReply *url_reply;
        url_reply = CALLOC(1, sizeof(*url_reply));

        gatekeeper__southbound__v1__gatekeeper_http_url_reply__init(url_reply);

        /* Create and populate common header */
        Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;
        header = create_common_header(attr_entry, pd_cache);
        if (header == NULL)
        {
            LOGD("%s: Failed to create common header for URL entry", __func__);
            FREE(url_reply);
            result = false;
            break;
        }

        /* Set URL value */
        url_reply->http_url = STRDUP(attr_entry->attr.url->name);
        if (url_reply->http_url == NULL)
        {
            LOGD("%s: Failed to allocate memory for URL string", __func__);
            free_common_header(header);
            FREE(url_reply);
            result = false;
            break;
        }

        /* Attach header to URL reply */
        url_reply->header = header;

        /* Add to bulk reply array */
        bulk_reply->reply_http_url[(*idx_url)++] = url_reply;
    }

    return result;
}

/**
 * @brief Populate reply arrays from tree entries
 *
 * @param device_tree pointer to the device tree
 * @param bulk_reply pointer to the bulk reply
 * @return true on success, false on failure
 */
bool gk_populate_bulk_reply_arrays(ds_tree_t *device_tree, Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply)
{
    struct per_device_cache *pd_cache;
    bool result = true;

    if (device_tree == NULL || bulk_reply == NULL) return false;

    /* Tracking indices for each reply type */
    int idx_url = 0, idx_fqdn = 0, idx_http_host = 0, idx_https_sni = 0;
    int idx_ipv4 = 0, idx_ipv6 = 0, idx_app = 0;
    int idx_ipv4_tuple = 0, idx_ipv6_tuple = 0;

    /* Populate reply arrays from tree entries */
    ds_tree_foreach (device_tree, pd_cache)
    {
        /* Process URL entries */
        if (!gk_process_url_entries(pd_cache, bulk_reply, &idx_url))
        {
            LOGD("%s: Failed to process URL entries", __func__);
            result = false;
            break;
        }

        /* Process App entries */
        if (!gk_process_app_entries(pd_cache, bulk_reply, &idx_app))
        {
            LOGD("%s: Failed to process APP entries", __func__);
            result = false;
            break;
        }

        /* Process hostname entries */
        if (!gk_process_hostname_entries(pd_cache, bulk_reply, &idx_fqdn, &idx_http_host, &idx_https_sni))
        {
            LOGD("%s: Failed to process hostname entries", __func__);
            result = false;
            break;
        }

        /* Process IPv4 entries */
        if (gk_process_ipv4_entries(pd_cache, bulk_reply, &idx_ipv4) == false)
        {
            LOGD("%s: Failed to process IPv4 entries", __func__);
            result = false;
            break;
        }
        /* IPv6 entries would be processed here */
        if (gk_process_ipv6_entries(pd_cache, bulk_reply, &idx_ipv6) == false)
        {
            LOGD("%s: Failed to process IPv6 entries", __func__);
            result = false;
            break;
        }
    }

    /* Update final counts for each reply type to match the actual populated entries */
    bulk_reply->n_reply_http_url = idx_url;
    bulk_reply->n_reply_fqdn = idx_fqdn;
    bulk_reply->n_reply_http_host = idx_http_host;
    bulk_reply->n_reply_https_sni = idx_https_sni;
    bulk_reply->n_reply_ipv4 = idx_ipv4;
    bulk_reply->n_reply_ipv6 = idx_ipv6;
    bulk_reply->n_reply_app = idx_app;
    bulk_reply->n_reply_ipv4_tuple = idx_ipv4_tuple;
    bulk_reply->n_reply_ipv6_tuple = idx_ipv6_tuple;

    return result;
}

static void gk_clean_http_url(Gatekeeper__Southbound__V1__GatekeeperHttpUrlReply *reply)
{
    if (reply == NULL || reply->header == NULL) return;

    free_common_header(reply->header);
    FREE(reply->http_url);
}

static void gk_clean_app(Gatekeeper__Southbound__V1__GatekeeperAppReply *reply)
{
    if (reply == NULL || reply->header == NULL) return;

    free_common_header(reply->header);
    FREE(reply->app_name);
}

static void gk_clean_ipv4(Gatekeeper__Southbound__V1__GatekeeperIpv4Reply *reply)
{
    if (reply == NULL || reply->header == NULL) return;

    free_common_header(reply->header);
}

static void gk_clean_ipv6(Gatekeeper__Southbound__V1__GatekeeperIpv6Reply *reply)
{
    if (reply == NULL || reply->header == NULL) return;

    free_common_header(reply->header);

    if (reply->addr_ipv6.data) FREE(reply->addr_ipv6.data);
}

void gk_cleanup_fqdn(Gatekeeper__Southbound__V1__GatekeeperFqdnReply *reply)
{
    Gatekeeper__Southbound__V1__GatekeeperFqdnRedirectReply *redirect;
    Gatekeeper__Southbound__V1__GatekeeperFqdnNxdomainReply *nxdomain;

    if (reply == NULL || reply->header == NULL) return;
    free_common_header(reply->header);

    if (!IS_NULL_PTR(reply->query_name)) FREE(reply->query_name);
    redirect = reply->redirect;
    if (redirect != NULL)
    {
        FREE(redirect->redirect_cname);
        FREE(redirect->redirect_ipv6.data);
        FREE(redirect);
    }

    nxdomain = reply->nxdomain;
    if (nxdomain && nxdomain->autority)
    {
        FREE(nxdomain->autority);
        FREE(nxdomain);
    }
}

void gk_cleanup_http_host(Gatekeeper__Southbound__V1__GatekeeperHttpHostReply *reply)
{
    Gatekeeper__Southbound__V1__GatekeeperRedirectReply *redirect;

    if (reply == NULL || reply->header == NULL) return;
    free_common_header(reply->header);

    if (!IS_NULL_PTR(reply->http_host)) FREE(reply->http_host);

    redirect = reply->redirect;
    if (redirect == NULL) return;
    if (redirect->redirect_cname) FREE(redirect->redirect_cname);
    if (redirect->redirect_ipv6.data) FREE(redirect->redirect_ipv6.data);

    FREE(redirect);
}

void gk_cleanup_https_sni(Gatekeeper__Southbound__V1__GatekeeperHttpsSniReply *reply)
{
    Gatekeeper__Southbound__V1__GatekeeperRedirectReply *redirect;

    if (reply == NULL || reply->header == NULL) return;
    free_common_header(reply->header);

    if (!IS_NULL_PTR(reply->https_sni)) FREE(reply->https_sni);

    redirect = reply->redirect;
    if (redirect == NULL) return;
    if (redirect->redirect_cname) FREE(redirect->redirect_cname);
    if (redirect->redirect_ipv6.data) FREE(redirect->redirect_ipv6.data);

    FREE(redirect);
}

#define CLEANUP_ENTRIES(attr_entries, count, cleanup_fn) \
    for (size_t i = 0; i < count; i++)                   \
    {                                                    \
        if (attr_entries[i])                             \
        {                                                \
            cleanup_fn(attr_entries[i]);                 \
            FREE(attr_entries[i]);                       \
        }                                                \
    }                                                    \
    FREE(attr_entries);

/**
 * @brief Clean up allocated resources in bulk reply on error
 *
 * @param bulk_reply pointer to the bulk reply to clean up
 */
void gk_cleanup_bulk_reply(Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply)
{
    if (bulk_reply == NULL) return;

    CLEANUP_ENTRIES(bulk_reply->reply_http_url, bulk_reply->n_reply_http_url, gk_clean_http_url);
    CLEANUP_ENTRIES(bulk_reply->reply_app, bulk_reply->n_reply_app, gk_clean_app);
    CLEANUP_ENTRIES(bulk_reply->reply_ipv4, bulk_reply->n_reply_ipv4, gk_clean_ipv4);
    CLEANUP_ENTRIES(bulk_reply->reply_ipv6, bulk_reply->n_reply_ipv6, gk_clean_ipv6);
    CLEANUP_ENTRIES(bulk_reply->reply_fqdn, bulk_reply->n_reply_fqdn, gk_cleanup_fqdn);
    CLEANUP_ENTRIES(bulk_reply->reply_http_host, bulk_reply->n_reply_http_host, gk_cleanup_http_host);
    CLEANUP_ENTRIES(bulk_reply->reply_https_sni, bulk_reply->n_reply_https_sni, gk_cleanup_https_sni);

    FREE(bulk_reply);
}

/**
 * @brief Allocate arrays for a bulk reply based on counts
 *
 * @param bulk_reply pointer to the bulk reply
 * @param counts array of counts for each type
 * @return true on success, false on failure
 */
static bool gk_allocate_reply_arrays(Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply, int counts[NUM_TYPES])
{
    if (bulk_reply == NULL || counts == NULL)
    {
        LOGD("%s: Invalid arguments", __func__);
        return false;
    }

    /* URL replies */
    if (counts[URL_IDX] > 0)
    {
        bulk_reply->n_reply_http_url = counts[URL_IDX];
        bulk_reply->reply_http_url =
                CALLOC(counts[URL_IDX], sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpUrlReply *));
    }

    /* Hostname-related replies */
    if (counts[FQDN_IDX] > 0)
    {
        bulk_reply->n_reply_fqdn = counts[FQDN_IDX];
        bulk_reply->reply_fqdn = CALLOC(counts[FQDN_IDX], sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnReply *));
    }

    if (counts[HOST_IDX] > 0)
    {
        bulk_reply->n_reply_http_host = counts[HOST_IDX];
        bulk_reply->reply_http_host =
                CALLOC(counts[HOST_IDX], sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpHostReply *));
    }

    if (counts[SNI_IDX] > 0)
    {
        bulk_reply->n_reply_https_sni = counts[SNI_IDX];
        bulk_reply->reply_https_sni =
                CALLOC(counts[SNI_IDX], sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpsSniReply *));
    }

    /* IPv4 replies */
    if (counts[IPV4_IDX] > 0)
    {
        bulk_reply->n_reply_ipv4 = counts[IPV4_IDX];
        bulk_reply->reply_ipv4 = CALLOC(counts[IPV4_IDX], sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv4Reply *));
    }

    /* IPv6 replies */
    if (counts[IPV6_IDX] > 0)
    {
        bulk_reply->n_reply_ipv6 = counts[IPV6_IDX];
        bulk_reply->reply_ipv6 = CALLOC(counts[IPV6_IDX], sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv6Reply *));
    }

    /* App replies */
    if (counts[APP_IDX] > 0)
    {
        bulk_reply->n_reply_app = counts[APP_IDX];
        bulk_reply->reply_app = CALLOC(counts[APP_IDX], sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReply *));
    }

    return true;
}

/**
 * @brief Serialize the gatekeeper cache into a protobuf structure
 *
 * This function populates a Gatekeeper__Southbound__V1__GatekeeperBulkReply
 * structure with data from the device tree in the cache manager.
 *
 * @return allocated GatekeeperBulkReply or NULL on error
 */
Gatekeeper__Southbound__V1__GatekeeperBulkReply *gk_cache_to_bulk_reply(void)
{
    Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply;
    struct gk_cache_mgr *mgr;
    ds_tree_t *device_tree;
    int counts[NUM_TYPES];
    int total_entries;

    /* Get the cache manager and device tree */
    mgr = gk_cache_get_mgr();
    if (mgr == NULL)
    {
        LOGD("%s: Failed to get cache manager - gk_cache_get_mgr() returned NULL", __func__);
        return NULL;
    }

    device_tree = &mgr->per_device_tree;

    /* Allocate the bulk reply structure */
    bulk_reply = CALLOC(1, sizeof(*bulk_reply));

    /* Initialize the protobuf structure */
    gatekeeper__southbound__v1__gatekeeper_bulk_reply__init(bulk_reply);

    /* Count all entries in the cache */
    total_entries = gk_count_cache_entries(device_tree, counts);

    LOGD("%s: Total entries in cache: %d", __func__, total_entries);

    /* If no entries in the cache, return empty reply */
    if (total_entries == 0)
    {
        LOGI("%s: No entries in cache, returning empty reply", __func__);
        FREE(bulk_reply);
        return NULL;
    }

    /* Allocate arrays based on counts */
    if (!gk_allocate_reply_arrays(bulk_reply, counts))
    {
        LOGD("%s: Failed to allocate reply arrays", __func__);
        gk_cleanup_bulk_reply(bulk_reply);
        return NULL;
    }

    /* Populate reply arrays */
    if (!gk_populate_bulk_reply_arrays(device_tree, bulk_reply))
    {
        LOGD("%s: Failed to populate reply arrays", __func__);
        gk_cleanup_bulk_reply(bulk_reply);
        return NULL;
    }

    return bulk_reply;
}

void store_serialized_cache_entries(struct gk_packed_buffer *pb)
{
    osp_ps_t *ps_handle = NULL;

    if (pb == NULL || pb->buf == NULL || pb->len == 0)
    {
        LOGD("%s: Invalid packed buffer", __func__);
        return;
    }

    ps_handle = osp_ps_open(GATEKEEPER_CACHE_STORE_NAME, OSP_PS_RDWR | OSP_PS_PRESERVE);
    if (ps_handle == NULL)
    {
        LOGD("%s: Failed to open persistent store: %s", __func__, GATEKEEPER_CACHE_STORE_NAME);
        return;
    }

    if (!osp_ps_set(ps_handle, GATEKEEPER_CACHE_DATA_KEY, pb->buf, pb->len))
    {
        LOGD("%s: Failed to write serialized cache to persistent store", __func__);
        osp_ps_close(ps_handle);
        return;
    }

    LOGD("%s: Stored serialized cache (size: %zu) to persistent store", __func__, pb->len);
    osp_ps_close(ps_handle);
}

struct gk_packed_buffer *gk_pack_reply_to_buffer(Gatekeeper__Southbound__V1__GatekeeperReply *pb)
{
    struct gk_packed_buffer *serialized = NULL;
    void *buf = NULL;
    size_t len;

    if (pb == NULL) return NULL;

    serialized = CALLOC(1, sizeof(*serialized));

    /* Get serialization length */
    len = gatekeeper__southbound__v1__gatekeeper_reply__get_packed_size(pb);
    if (len == 0)
    {
        LOGD("%s: Packed size is 0", __func__);
        goto out_err;
    }

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    serialized->len = gatekeeper__southbound__v1__gatekeeper_reply__pack(pb, buf);
    if (serialized->len != len) goto out_err;
    serialized->buf = buf;

    return serialized;

out_err:
    if (buf != NULL) FREE(buf);
    gk_free_packed_buffer(serialized);
    return NULL;
}

void gk_store_serialized_cache(ds_tree_t *tree)
{
    Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply;
    Gatekeeper__Southbound__V1__GatekeeperReply gk_reply;
    struct gk_packed_buffer *pb;

    /* Convert the device cache into a bulk reply format*/
    bulk_reply = gk_cache_to_bulk_reply();
    if (bulk_reply == NULL)
    {
        LOGD("%s: Failed to serialize cache", __func__);
        return;
    }

    /* assign bulk reply to gk_reply */
    gatekeeper__southbound__v1__gatekeeper_reply__init(&gk_reply);
    gk_reply.bulk_reply = bulk_reply;

    pb = gk_pack_reply_to_buffer(&gk_reply);
    if (pb == NULL)
    {
        LOGD("%s: Failed to pack bulk reply to buffer", __func__);
        gk_cleanup_bulk_reply(bulk_reply);
        return;
    }

    LOGD("%s: Storing the cache entries into persistent storage", __func__);
    store_serialized_cache_entries(pb);

    /* Free bulk reply */
    gk_cleanup_bulk_reply(bulk_reply);

    gk_free_packed_buffer(pb);
}

static bool gk_del_persist_cache(void)
{
    LOGD("%s: Erasing persistent store", __func__);
    return osp_ps_erase_store_name(GATEKEEPER_CACHE_STORE_NAME, 0);
}

/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/
/*
 * gk_store_cache_in_persistence - Store the current cache in persistent storage
 *
 * This function serializes the current cache entries and stores them in
 * persistent storage for later retrieval.
 */
void gk_store_cache_in_persistence(void)
{
    struct gk_cache_mgr *mgr;
    ds_tree_t *tree;

    /* Delete the existing cache. If for some reason cache store fails,
     * then the persistent cache won't be in sync */
    gk_del_persist_cache();

    mgr = gk_cache_get_mgr();
    if (!mgr->initialized) return;

    LOGD("%s: Storing cache to persistence storage.", __func__);

    tree = &mgr->per_device_tree;

    gkc_print_cache_entries();

    gk_store_serialized_cache(tree);
}
