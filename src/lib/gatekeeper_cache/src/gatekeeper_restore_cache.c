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

void gk_free_cache_interface_entry(struct gk_attr_cache_interface *entry)
{
    if (entry == NULL) return;

    if (entry->device_mac) FREE(entry->device_mac);
    if (entry->ip_addr) FREE(entry->ip_addr);
}

bool gk_populate_cache_entry(struct gk_device2app_repl *dev_repl, struct gk_attr_cache_interface *entry)
{
    struct sockaddr_storage ss;
    bool success = true;
    os_macaddr_t mac;
    bool ret;

    if (dev_repl == NULL || entry == NULL || dev_repl->header == NULL) return false;

    if (dev_repl->header->dev_id)
    {
        ret = os_nif_macaddr_from_str(&mac, dev_repl->header->dev_id);
        if (ret == false)
        {
            LOGD("%s: Failed to convert MAC address '%s'", __func__, dev_repl->header->dev_id);
            return false;
        }

        entry->device_mac = CALLOC(1, sizeof(os_macaddr_t));
        memcpy(entry->device_mac, &mac, sizeof(os_macaddr_t));
    }

    /* Set common fields from header */
    entry->action = dev_repl->header->action;
    entry->category_id = dev_repl->header->category_id;
    entry->confidence_level = dev_repl->header->confidence_level;
    entry->flow_marker = dev_repl->header->flow_marker;
    entry->cache_ttl = dev_repl->header->ttl;
    entry->gk_policy = dev_repl->header->policy;
    entry->network_id = dev_repl->header->network_id;

    switch (dev_repl->type)
    {
        case GK_ENTRY_TYPE_APP:
            LOGT("%s: Processing APP entry: %s", __func__, dev_repl->app_name);
            entry->attribute_type = GK_CACHE_REQ_TYPE_APP;
            entry->attr_name = dev_repl->app_name;
            break;

        case GK_ENTRY_TYPE_URL:
            LOGT("%s: Processing URL entry: %s", __func__, dev_repl->url);
            entry->attribute_type = GK_CACHE_REQ_TYPE_URL;
            entry->attr_name = dev_repl->url;
            break;

        case GK_ENTRY_TYPE_FQDN:
            LOGT("%s: Processing FQDN entry: %s", __func__, dev_repl->fqdn);
            entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
            entry->attr_name = dev_repl->fqdn;
            entry->fqdn_redirect = dev_repl->fqdn_redirect;
            break;

        case GK_ENTRY_TYPE_HOST:
            LOGT("%s: Processing HTTP Host entry: %s", __func__, dev_repl->http_host);
            entry->attribute_type = GK_CACHE_REQ_TYPE_HOST;
            entry->attr_name = dev_repl->http_host;
            break;

        case GK_ENTRY_TYPE_SNI:
            LOGT("%s: Processing HTTPS SNI entry: %s", __func__, dev_repl->https_sni);
            entry->attribute_type = GK_CACHE_REQ_TYPE_SNI;
            entry->attr_name = dev_repl->https_sni;
            break;

        case GK_ENTRY_TYPE_IPV4:
            LOGT("%s: Processing IPv4 entry: 0x%08x", __func__, dev_repl->ipv4_addr);
            struct sockaddr_in *sin;
            entry->attribute_type = GK_CACHE_REQ_TYPE_IPV4;

            /* Create sockaddr structure for IPv4 */
            memset(&ss, 0, sizeof(ss));
            sin = (struct sockaddr_in *)&ss;
            sin->sin_family = AF_INET;
            sin->sin_addr.s_addr = dev_repl->ipv4_addr;

            entry->ip_addr = CALLOC(1, sizeof(struct sockaddr_storage));
            memcpy(entry->ip_addr, &ss, sizeof(struct sockaddr_storage));
            break;

        case GK_ENTRY_TYPE_IPV6:
            LOGT("%s: Processing IPv6 entry", __func__);
            entry->attribute_type = GK_CACHE_REQ_TYPE_IPV6;

            if (dev_repl->ipv6_addr.data == NULL || dev_repl->ipv6_addr.len == 0)
            {
                LOGD("%s: IPv6 entry has NULL or empty address data", __func__);
                success = false;
                break;
            }

            struct sockaddr_in6 *sin6;
            /* Create sockaddr structure for IPv6 */
            memset(&ss, 0, sizeof(ss));
            sin6 = (struct sockaddr_in6 *)&ss;
            sin6->sin6_family = AF_INET6;

            if (dev_repl->ipv6_addr.len == sizeof(sin6->sin6_addr))
            {
                memcpy(&sin6->sin6_addr, dev_repl->ipv6_addr.data, dev_repl->ipv6_addr.len);

                entry->ip_addr = CALLOC(1, sizeof(struct sockaddr_storage));
                memcpy(entry->ip_addr, &ss, sizeof(struct sockaddr_storage));
            }
            else
            {
                LOGD("%s: Invalid IPv6 address length: %zu", __func__, dev_repl->ipv6_addr.len);
                success = false;
            }
            break;

        default:
            LOGD("%s: Unsupported entry type: %d", __func__, dev_repl->type);
            success = false;
            break;
    }

    if (!success) gk_free_cache_interface_entry(entry);

    return success;
}

void gk_add_reply_entries_to_cache(struct gk_reply *reply)
{
    struct gk_attr_cache_interface entry;
    struct gk_device2app_repl *dev_repl;
    struct gk_bulk_reply *bulk_reply;
    int failed_count = 0;
    int added_count = 0;
    bool result;

    LOGD("%s: Starting to populate cache from reply data", __func__);
    if (reply == NULL) return;

    bulk_reply = (struct gk_bulk_reply *)&reply->data_reply;
    if (bulk_reply == NULL || bulk_reply->n_devices == 0)
    {
        LOGD("%s: No cache entries to populate", __func__);
        return;
    }

    LOGD("%s: Processing %zu devices from bulk reply", __func__, bulk_reply->n_devices);
    /* Process each device entry */
    for (size_t i = 0; i < bulk_reply->n_devices; i++)
    {
        dev_repl = bulk_reply->devices[i];
        if (dev_repl == NULL || dev_repl->header == NULL)
        {
            LOGD("%s: Skipping NULL device entry at index %zu", __func__, i);
            failed_count++;
            continue;
        }

        MEMZERO(entry);

        /* Populate cache entry from device reply */
        result = gk_populate_cache_entry(dev_repl, &entry);
        if (!result)
        {
            LOGD("%s: Failed to populate cache entry for device %zu", __func__, i);
            failed_count++;
            continue;
        }

        /* Add the entry to the cache */
        if (gkc_add_attribute_entry(&entry))
        {
            LOGD("%s: Successfully added entry %zu to cache", __func__, i);
            added_count++;
        }
        else
        {
            LOGD("%s: Failed to add entry %zu to cache", __func__, i);
            failed_count++;
        }

        /* Free the entry after adding it to cache */
        gk_free_cache_interface_entry(&entry);
    }

    LOGD("%s: Cache population complete. Success: %u, Failure: %u", __func__, added_count, failed_count);
}

bool gk_deserialize_cache(struct gk_packed_buffer *pb, struct gk_reply *reply)
{
    Gatekeeper__Southbound__V1__GatekeeperReply *pb_reply;

    LOGD("%s: Starting unserialization process", __func__);
    if (pb == NULL || pb->buf == NULL) return false;

    reply->type = FSM_BULK_REQ;

    /* Unpack the serialized buffer */
    pb_reply = gatekeeper__southbound__v1__gatekeeper_reply__unpack(NULL, pb->len, pb->buf);
    if (pb_reply == NULL)
    {
        LOGD("%s(): Failed to unpack the gatekeeper cache", __func__);
        return false;
    }

    LOGT("%s: Calling gk_parse_reply to process data", __func__);
    gk_parse_reply(reply, pb_reply);

    gatekeeper__southbound__v1__gatekeeper_reply__free_unpacked(pb_reply, NULL);

    return true;
}

/**
 * @brief Restore cache from a packed buffer
 *
 * This function restores the cache from a packed buffer containing serialized cache data.
 * It populates the gk_reply structure and adds the entries to the cache.
 *
 * @param pb Pointer to the packed buffer containing serialized cache data.
 * @return true if successful, false otherwise.
 */
bool gk_restore_cache_from_buffer(struct gk_packed_buffer *pb)
{
    struct gk_reply reply;
    bool success;

    /* unserialize the cache and add the result to reply structure */
    success = gk_deserialize_cache(pb, &reply);
    if (!success)
    {
        LOGD("%s: Failed to unserialize cache data", __func__);
        return false;
    }

    LOGT("%s: Retrieved %zu cache entries from persistent storage", __func__, reply.data_reply.bulk_reply.n_devices);

    /* add replies to gk cache */
    gk_add_reply_entries_to_cache(&reply);
    gk_clear_bulk_responses(&reply);

    return true;
}

/**
 * @brief Restore cache from persistent storage
 *
 * This function attempts to restore the cache from the persistent storage.
 * It reads the serialized cache data and populates the cache entries.
 */
void gk_restore_cache_from_persistance(void)
{
    osp_ps_t *ps_handle = NULL;
    void *buf = NULL;
    ssize_t size = 0;

    LOGD("%s:  restore cache from persistent storage storage", __func__);
    gkc_print_cache_entries();

    /* Open the persistent store */
    ps_handle = osp_ps_open(GATEKEEPER_CACHE_STORE_NAME, OSP_PS_READ | OSP_PS_PRESERVE);
    if (ps_handle == NULL)
    {
        LOGD("%s: Failed to open persistent store: %s", __func__, GATEKEEPER_CACHE_STORE_NAME);
        return;
    }

    /* Determine stored data size */
    size = osp_ps_get(ps_handle, GATEKEEPER_CACHE_DATA_KEY, NULL, 0);
    if (size <= 0)
    {
        LOGD("%s: No valid cache data found (size: %zd)", __func__, size);
        osp_ps_close(ps_handle);
        return;
    }

    buf = MALLOC((size_t)size);
    if (buf == NULL)
    {
        LOGD("%s: Memory allocation failed (size: %zd)", __func__, size);
        osp_ps_close(ps_handle);
        return;
    }

    /* Read serialized cache data */
    if (osp_ps_get(ps_handle, GATEKEEPER_CACHE_DATA_KEY, buf, (size_t)size) != size)
    {
        LOGD("%s: Failed to read serialized cache data (expected size: %zd)", __func__, size);
        FREE(buf);
        osp_ps_close(ps_handle);
        return;
    }

    struct gk_packed_buffer pb = {.buf = buf, .len = size};

    LOGT("%s:Restoring Cache entries", __func__);
    gk_restore_cache_from_buffer(&pb);

    LOGT("dumping cache entries after restoring from persistent storage");
    gkc_print_cache_entries();

    FREE(buf);
    osp_ps_close(ps_handle);
}
