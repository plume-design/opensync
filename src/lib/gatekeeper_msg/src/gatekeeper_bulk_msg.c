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
#include <stdlib.h>

#include "fsm_policy.h"
#include "gatekeeper.pb-c.h"
#include "gatekeeper_single_curl.h"
#include "gatekeeper_bulk_msg.h"
#include "gatekeeper_ecurl.h"
#include "gatekeeper_msg.h"
#include "gatekeeper_cache.h"
#include "log.h"
#include "os_nif.h"
#include "memutil.h"

/******************************************************************************
 *  PRIVATE definitions
 *****************************************************************************/

static Gatekeeper__Southbound__V1__GatekeeperAppReq **gk_set_pb_bulk_app(struct gk_bulk_request *request);

static struct gk_device2app_repl *gk_create_common_reply_entry(
        Gatekeeper__Southbound__V1__GatekeeperCommonReply *header,
        int entry_type)
{
    struct gk_device2app_repl *dev_entry;
    char mac_str[OS_MACSTR_SZ];

    if (header == NULL)
    {
        LOGE("%s: Invalid header or device_id for %d", __func__, entry_type);
        return NULL;
    }

    /* Allocate and initialize entry structure */
    dev_entry = CALLOC(1, sizeof(struct gk_device2app_repl));
    dev_entry->header = CALLOC(1, sizeof(struct gk_reply_header));
    dev_entry->type = entry_type;
    dev_entry->header->action = gk_get_fsm_action(header);
    dev_entry->header->category_id = header->category_id;
    dev_entry->header->flow_marker = header->flow_marker;
    dev_entry->header->confidence_level = header->confidence_level;
    dev_entry->header->ttl = header->ttl;
    dev_entry->header->policy = header->policy ? STRDUP(header->policy) : NULL;
    dev_entry->header->network_id = header->network_id ? STRDUP(header->network_id) : NULL;

    /* Set device id if available */
    if (header->device_id.data && entry_type != GK_ENTRY_TYPE_TRAFFIC_CLASS)
    {
        os_nif_macaddr_to_str((os_macaddr_t *)header->device_id.data, mac_str, PRI_os_macaddr_lower_t);
        dev_entry->header->dev_id = STRDUP(mac_str);
        LOGD("%s: device MAC: %s", __func__, mac_str);
    }

    return dev_entry;
}

static bool gk_parse_bulk_reply(struct gk_bulk_reply *bulk_reply, Gatekeeper__Southbound__V1__GatekeeperBulkReply *src)
{
    Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;
    struct gk_device2app_repl *dev_entry;
    char ipv4_str[INET_ADDRSTRLEN];
    char ipv6_str[INET6_ADDRSTRLEN];
    struct in_addr ipv4_addr;
    size_t total_entries = 0;
    size_t entry_idx = 0;
    int entry_type = 0;

    LOGD("%s: Starting to parse bulk reply", __func__);

    if (src == NULL || bulk_reply == NULL)
    {
        LOGE("%s: Invalid arguments - src:%p, bulk_reply:%p", __func__, src, bulk_reply);
        return false;
    }

    /* Calculate total number of entries */
    total_entries = src->n_reply_app + src->n_reply_ipv4 + src->n_reply_ipv6 + src->n_reply_http_url + src->n_reply_fqdn
                    + src->n_reply_http_host + src->n_reply_https_sni;

    LOGD("%s: Processing bulk reply with entries - TOTAL:%zu, APP:%zu, IPv4:%zu, IPv6:%zu, URL:%zu, FQDN:%zu, "
         "HTTP_HOST:%zu, HTTPS_SNI:%zu",
         __func__,
         total_entries,
         src->n_reply_app,
         src->n_reply_ipv4,
         src->n_reply_ipv6,
         src->n_reply_http_url,
         src->n_reply_fqdn,
         src->n_reply_http_host,
         src->n_reply_https_sni);

    /* Allocate memory for all entries */
    bulk_reply->n_devices = total_entries;
    bulk_reply->devices = CALLOC(total_entries, sizeof(struct gk_device2app_repl *));

    /* Process APP entries */
    LOGD("%s: Starting to process %zu APP entries", __func__, src->n_reply_app);
    for (size_t i = 0; i < src->n_reply_app; i++)
    {
        Gatekeeper__Southbound__V1__GatekeeperAppReply *app_reply;

        app_reply = src->reply_app[i];
        if (app_reply == NULL) continue;

        if (app_reply->traffic_class)
        {
            entry_type = GK_ENTRY_TYPE_TRAFFIC_CLASS;
        }
        else
        {
            entry_type = GK_ENTRY_TYPE_APP;
        }
        dev_entry = gk_create_common_reply_entry(app_reply->header, entry_type);
        if (dev_entry == NULL)
        {
            LOGD("%s: Failed to create common entry for APP/TRAFFIC_CLASS entry %zu", __func__, i);
            continue;
        }
        /* Copy APP-specific data */
        LOGT("%s: APP entry %zu - traffic class:%s", __func__, i, app_reply->traffic_class);
        if (entry_type == GK_ENTRY_TYPE_TRAFFIC_CLASS)
        {
            dev_entry->traffic_class = app_reply->traffic_class ? STRDUP(app_reply->traffic_class) : NULL;
            LOGD("%s: APP entry %zu - traffic class:%s", __func__, i, app_reply->traffic_class);
        }
        else
        {
            dev_entry->app_name = app_reply->app_name ? STRDUP(app_reply->app_name) : NULL;
            LOGD("%s: APP entry %zu - name:%s, action:%d", __func__, i, app_reply->app_name, dev_entry->header->action);
        }

        LOGT("%s: Adding APP entry %zu to bulk_reply at index %zu", __func__, i, entry_idx);
        bulk_reply->devices[entry_idx++] = dev_entry;
    }

    /* Process IPv4 entries */
    LOGD("%s: Starting to process %zu IPv4 entries", __func__, src->n_reply_ipv4);
    for (size_t i = 0; i < src->n_reply_ipv4; i++)
    {
        Gatekeeper__Southbound__V1__GatekeeperIpv4Reply *ipv4_reply;

        ipv4_reply = src->reply_ipv4[i];
        if (ipv4_reply == NULL) continue;

        header = ipv4_reply->header;

        dev_entry = gk_create_common_reply_entry(header, GK_ENTRY_TYPE_IPV4);
        if (dev_entry == NULL)
        {
            LOGD("%s: Failed to create common entry for IPv4 entry %zu", __func__, i);
            continue;
        }

        /* Convert IPv4 to string representation */
        dev_entry->ipv4_addr = ipv4_reply->addr_ipv4;
        ipv4_addr.s_addr = dev_entry->ipv4_addr;
        if (inet_ntop(AF_INET, &ipv4_addr, ipv4_str, sizeof(ipv4_str)))
        {
            LOGT("%s: IPv4 entry %zu - addr: %s", __func__, i, ipv4_str);
        }
        else
        {
            LOGN("%s: Failed to convert IPv4 address", __func__);
        }

        bulk_reply->devices[entry_idx++] = dev_entry;
    }

    /* Process IPv6 entries */
    LOGD("%s: Starting to process %zu IPv6 entries", __func__, src->n_reply_ipv6);
    for (size_t i = 0; i < src->n_reply_ipv6; i++)
    {
        Gatekeeper__Southbound__V1__GatekeeperIpv6Reply *ipv6_reply;

        ipv6_reply = src->reply_ipv6[i];
        if (ipv6_reply == NULL) continue;

        header = ipv6_reply->header;
        dev_entry = gk_create_common_reply_entry(header, GK_ENTRY_TYPE_IPV6);
        if (dev_entry == NULL)
        {
            LOGD("%s: Failed to create common entry for IPv6 entry %zu", __func__, i);
            continue;
        }

        /* Copy IPv6 address data */
        dev_entry->ipv6_addr.len = ipv6_reply->addr_ipv6.len;
        dev_entry->ipv6_addr.data = CALLOC(1, ipv6_reply->addr_ipv6.len);

        memcpy(dev_entry->ipv6_addr.data, ipv6_reply->addr_ipv6.data, ipv6_reply->addr_ipv6.len);

        /* Log IPv6 address for debugging */
        if (inet_ntop(AF_INET6, dev_entry->ipv6_addr.data, ipv6_str, sizeof(ipv6_str)) != NULL)
        {
            LOGD("%s: IPv6 entry %zu - addr: %s", __func__, i, ipv6_str);
        }
        else
        {
            LOGE("%s: Failed to convert IPv6 address to string format", __func__);
        }

        bulk_reply->devices[entry_idx++] = dev_entry;
    }

    /* Process URL entries */
    LOGD("%s: Starting to process %zu URL entries", __func__, src->n_reply_http_url);
    for (size_t i = 0; i < src->n_reply_http_url; i++)
    {
        Gatekeeper__Southbound__V1__GatekeeperHttpUrlReply *url_reply;

        url_reply = src->reply_http_url[i];
        if (url_reply == NULL) continue;

        header = url_reply->header;

        dev_entry = gk_create_common_reply_entry(header, GK_ENTRY_TYPE_URL);
        if (dev_entry == NULL)
        {
            LOGD("%s: Failed to create common entry for URL entry %zu", __func__, i);
            continue;
        }
        dev_entry->url = url_reply->http_url ? STRDUP(url_reply->http_url) : NULL;
        bulk_reply->devices[entry_idx++] = dev_entry;
    }

    /* Process FQDN entries */
    LOGD("%s: Starting to process %zu FQDN entries", __func__, src->n_reply_fqdn);
    for (size_t i = 0; i < src->n_reply_fqdn; i++)
    {
        Gatekeeper__Southbound__V1__GatekeeperFqdnReply *fqdn_reply;
        Gatekeeper__Southbound__V1__GatekeeperFqdnRedirectReply *redirect_reply;

        fqdn_reply = src->reply_fqdn[i];
        if (fqdn_reply == NULL) continue;

        header = fqdn_reply->header;
        dev_entry = gk_create_common_reply_entry(header, GK_ENTRY_TYPE_FQDN);
        if (dev_entry == NULL)
        {
            LOGD("%s: Failed to create common entry for FQDN entry %zu", __func__, i);
            continue;
        }

        dev_entry->fqdn = fqdn_reply->query_name ? STRDUP(fqdn_reply->query_name) : NULL;

        /* Process FQDN redirect reply if present and needed */
        redirect_reply = fqdn_reply->redirect;
        if (redirect_reply && (redirect_reply->redirect_ipv4 || redirect_reply->redirect_ipv6.data))
        {
            dev_entry->fqdn_redirect = CALLOC(1, sizeof(struct fqdn_redirect_s));
            dev_entry->fqdn_redirect->redirect = true;

            int redirect_ip_index = 0;

            /* Handle IPv4 redirect if present */
            if (redirect_reply->redirect_ipv4)
            {
                char ipv4_str[INET_ADDRSTRLEN] = {'\0'};
                const char *res;

                res = inet_ntop(AF_INET, &(redirect_reply->redirect_ipv4), ipv4_str, INET_ADDRSTRLEN);
                if (res)
                {
                    LOGT("%s: FQDN entry %zu - redirect IPv4 IP: %s", __func__, i, ipv4_str);
                    char ipv4_redirect_s[INET_ADDRSTRLEN + 3];
                    snprintf(ipv4_redirect_s, sizeof(ipv4_redirect_s), "A-%s", ipv4_str);
                    STRSCPY(dev_entry->fqdn_redirect->redirect_ips[redirect_ip_index++], ipv4_redirect_s);
                }
                else
                {
                    LOGD("%s: Failed to convert IPv4 address", __func__);
                }
            }

            /* Handle IPv6 redirect if present */
            if (redirect_reply->redirect_ipv6.data)
            {
                char ipv6_str[INET6_ADDRSTRLEN] = {'\0'};
                const char *res;

                res = inet_ntop(AF_INET6, redirect_reply->redirect_ipv6.data, ipv6_str, INET6_ADDRSTRLEN);
                if (res)
                {
                    LOGT("%s: FQDN entry %zu - redirect IPv6 IP: %s", __func__, i, ipv6_str);
                    char ipv6_redirect_s[INET6_ADDRSTRLEN + 5];
                    snprintf(ipv6_redirect_s, sizeof(ipv6_redirect_s), "AAAA-%s", ipv6_str);
                    STRSCPY(dev_entry->fqdn_redirect->redirect_ips[redirect_ip_index++], ipv6_redirect_s);
                }
                else
                {
                    LOGD("%s: Failed to convert IPv6 address", __func__);
                }
            }

            /* If no valid redirect IPs were processed, free the allocated memory */
            if (redirect_ip_index == 0)
            {
                FREE(dev_entry->fqdn_redirect);
                dev_entry->fqdn_redirect = NULL;
                dev_entry->fqdn_redirect->redirect = false;
                LOGD("%s: No valid redirect IPs found, freeing fqdn_redirect", __func__);
            }
        }

        bulk_reply->devices[entry_idx++] = dev_entry;
    }
    /* Process HTTP Host entries */
    LOGD("%s: Starting to process %zu HTTP Host entries", __func__, src->n_reply_http_host);
    for (size_t i = 0; i < src->n_reply_http_host; i++)
    {
        Gatekeeper__Southbound__V1__GatekeeperHttpHostReply *http_host_reply;
        http_host_reply = src->reply_http_host[i];

        if (http_host_reply == NULL) continue;

        header = http_host_reply->header;
        dev_entry = gk_create_common_reply_entry(header, GK_ENTRY_TYPE_HOST);
        if (dev_entry == NULL)
        {
            LOGD("%s: Failed to create common entry for Host entry %zu", __func__, i);
            continue;
        }

        dev_entry->http_host = http_host_reply->http_host ? STRDUP(http_host_reply->http_host) : NULL;
        bulk_reply->devices[entry_idx++] = dev_entry;
    }

    /* Process HTTPS SNI entries */
    LOGD("%s: Starting to process %zu HTTPS SNI entries", __func__, src->n_reply_https_sni);
    for (size_t i = 0; i < src->n_reply_https_sni; i++)
    {
        Gatekeeper__Southbound__V1__GatekeeperHttpsSniReply *https_sni_reply;
        https_sni_reply = src->reply_https_sni[i];

        if (https_sni_reply == NULL) continue;

        header = https_sni_reply->header;
        dev_entry = gk_create_common_reply_entry(header, GK_ENTRY_TYPE_SNI);
        if (dev_entry == NULL)
        {
            LOGD("%s: Failed to create common entry for SNI entry %zu", __func__, i);
            continue;
        }

        dev_entry->https_sni = https_sni_reply->https_sni ? STRDUP(https_sni_reply->https_sni) : NULL;
        bulk_reply->devices[entry_idx++] = dev_entry;
    }

    /* Update actual number of entries if some were skipped due to errors */
    bulk_reply->n_devices = entry_idx;

    LOGN("%s: Completed parsing bulk reply - processed %zu/%zu entries", __func__, entry_idx, total_entries);

    return true;
}

static void gk_free_bulk_app_req(Gatekeeper__Southbound__V1__GatekeeperBulkRequest *bulk_req)
{
    size_t i;

    for (i = 0; i < bulk_req->n_req_app; i++)
    {
        gk_free_app_req(bulk_req->req_app[i]);
    }

    FREE(bulk_req->req_app);
}

static void gk_free_bulk_response(struct gk_reply *reply)
{
    struct gk_bulk_reply *bulk_reply;
    struct gk_device2app_repl *dev_app;
    size_t i;

    bulk_reply = (struct gk_bulk_reply *)&reply->data_reply;
    for (i = 0; i < bulk_reply->n_devices; i++)
    {
        dev_app = bulk_reply->devices[i];
        if (!IS_NULL_PTR(dev_app->header->dev_id)) FREE(dev_app->header->dev_id);

        if (!IS_NULL_PTR(dev_app->header->policy)) FREE(dev_app->header->policy);

        if (!IS_NULL_PTR(dev_app->header)) FREE(dev_app->header);

        /* Free based on entry type */
        if (!IS_NULL_PTR(dev_app->app_name))
        {
            FREE(dev_app->app_name);
        }

        /* Free URL if present */
        if (!IS_NULL_PTR(dev_app->url))
        {
            FREE(dev_app->url);
        }

        /* Free IPv6 data if present */
        if (!IS_NULL_PTR(dev_app->ipv6_addr.data))
        {
            FREE(dev_app->ipv6_addr.data);
        }

        /* Free sni data if present */
        if (!IS_NULL_PTR(dev_app->https_sni))
        {
            FREE(dev_app->https_sni);
        }

        /* Free http host if present */
        if (!IS_NULL_PTR(dev_app->http_host))
        {
            FREE(dev_app->http_host);
        }

        /* Free FQDN if present */
        if (!IS_NULL_PTR(dev_app->fqdn))
        {
            FREE(dev_app->fqdn);
        }

        /* Free FQDN redirect if present */
        if (!IS_NULL_PTR(dev_app->fqdn_redirect))
        {
            FREE(dev_app->fqdn_redirect);
            dev_app->fqdn_redirect = NULL;
        }

        FREE(dev_app);
    }
    FREE(bulk_reply->devices);
}

static void gk_free_bulk_apps(struct gk_device2app_req *devices)
{
    size_t i;

    for (i = 0; i < devices->n_apps; i++)
    {
        FREE(devices->apps[i]);
    }
    FREE(devices->apps);
    return;
}

static void gk_free_bulk_request(struct gk_request *req)
{
    struct gk_bulk_request *bulk_request;
    struct gk_device2app_req *dev;
    struct gk_req_header *hdr;
    size_t i;

    bulk_request = (struct gk_bulk_request *)&req->req;
    for (i = 0; i < bulk_request->n_devices; i++)
    {
        dev = bulk_request->devices[i];
        gk_free_bulk_apps(dev);

        hdr = dev->header;
        FREE(hdr->dev_id);
        FREE(hdr->network_id);
        FREE(hdr);
        FREE(dev);
    }

    FREE(bulk_request->devices);
    return;
}

static size_t gk_get_bulk_app_count(struct gk_bulk_request *remark_req)
{
    size_t count = 0;
    size_t i;

    /* loop through each device to get the total number of apps request
     * of all device */
    for (i = 0; i < remark_req->n_devices; i++)
    {
        count += remark_req->devices[i]->n_apps;
    }
    return count;
}
/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/

Gatekeeper__Southbound__V1__GatekeeperBulkRequest *gk_set_pb_bulk_app_request(struct gk_bulk_request *request)
{
    Gatekeeper__Southbound__V1__GatekeeperBulkRequest *pb;
    if (request->n_devices == 0) return NULL;

    pb = CALLOC(1, sizeof(*pb));
    gatekeeper__southbound__v1__gatekeeper_bulk_request__init(pb);
    pb->n_req_app = gk_get_bulk_app_count(request);
    pb->req_app = gk_set_pb_bulk_app(request);

    return pb;
}

void gk_free_bulk_req(Gatekeeper__Southbound__V1__GatekeeperReq *pb)
{
    Gatekeeper__Southbound__V1__GatekeeperBulkRequest *bulk_req;

    bulk_req = pb->req_bulk;
    if (bulk_req == NULL) return;

    if (bulk_req->n_req_app) gk_free_bulk_app_req(bulk_req);

    FREE(bulk_req);
}

/**
 * @brief Parses the protobuf reply and populates the corresponding gk_reply structure.
 *
 * @param reply Pointer to the gk_reply structure to be populated.
 * @param pb_reply Pointer to the protobuf reply (Gatekeeper__Southbound__V1__GatekeeperReply) to be parsed.
 *
 * @return true if the reply was successfully parsed else false.
 */
bool gk_parse_reply(struct gk_reply *reply, Gatekeeper__Southbound__V1__GatekeeperReply *pb_reply)
{
    struct gk_bulk_reply *bulk_reply;
    union gk_data_reply *data_reply;

    if (pb_reply == NULL || reply == NULL) return false;

    data_reply = &reply->data_reply;
    switch (reply->type)
    {
        case FSM_BULK_REQ:
            bulk_reply = &data_reply->bulk_reply;
            gk_parse_bulk_reply(bulk_reply, pb_reply->bulk_reply);
            LOGN("Parsed bulk reply with %zu entries", bulk_reply->n_devices);
            break;

        default:
            LOGN("Invalid request type %d", reply->type);
            return false;
    }
    return true;
}

/*
 * @brief Parses the curl response and populates the gk_reply structure.
 *
 * @param reply Pointer to the gk_reply structure to be populated.
 * @param data Pointer to the gk_curl_data structure containing the curl response.
 *
 * @return true if the curl response was successfully parsed else false
 */
bool gk_parse_curl_response(struct gk_reply *reply, struct gk_curl_data *data)
{
    Gatekeeper__Southbound__V1__GatekeeperReply *pb_reply;
    const uint8_t *buf;
    int ret;

    buf = (const uint8_t *)data->memory;
    /* Unpack the protobuf Gatekeeper reply */
    pb_reply = gatekeeper__southbound__v1__gatekeeper_reply__unpack(NULL, data->size, buf);
    if (pb_reply == NULL)
    {
        LOGN("%s(): Failed to unpack the gatekeeper reply", __func__);
        return false;
    }

    /* Process unpacked data */
    ret = gk_parse_reply(reply, pb_reply);
    gatekeeper__southbound__v1__gatekeeper_reply__free_unpacked(pb_reply, NULL);

    return ret;
}

/*
 * @brief Frees the bulk response in the gk_reply structure.
 *
 * @param reply Pointer to the gk_reply structure containing the bulk response data.
 */
void gk_clear_bulk_responses(struct gk_reply *reply)
{
    if (reply->type == FSM_BULK_REQ)
    {
        gk_free_bulk_response(reply);
    }
}

/*
 * @brief Frees the bulk request in the gk_request structure.
 *
 * @param request Pointer to the gk_request structure containing the bulk request data.
 */
void gk_clear_bulk_requests(struct gk_request *req)
{
    if (req->type == FSM_BULK_REQ)
    {
        gk_free_bulk_request(req);
    }
}

bool gk_set_pb_bulk_request(Gatekeeper__Southbound__V1__GatekeeperReq *gk_req_pb, struct gk_bulk_request *request)
{
    if (request == NULL || gk_req_pb == NULL) return false;
    switch (request->req_type)
    {
        case FSM_APP_REQ:
        case FSM_TRAFFIC_CLASS_REQ:
            gk_req_pb->req_bulk = gk_set_pb_bulk_app_request(request);
            return (gk_req_pb->req_bulk != NULL) ? true : false;
        default:
            LOGN("%s():%d Invalid bulk request type: %d", __func__, __LINE__, request->req_type);
            return false;
    }
}

Gatekeeper__Southbound__V1__GatekeeperAppReq **gk_set_pb_bulk_app(struct gk_bulk_request *request)
{
    Gatekeeper__Southbound__V1__GatekeeperAppReq **pb_tbl;
    struct gk_device2app_req *dev_apps;
    struct gk_app_request *app_req;
    union gk_data_req *data_req;
    struct gk_request gk_req;
    size_t allocated;
    char *app_name;
    int count;
    size_t i;
    size_t j;

    MEMZERO(gk_req);
    gk_req.type = request->req_type;
    data_req = &gk_req.req;
    app_req = &data_req->gk_app_req;

    count = gk_get_bulk_app_count(request);
    pb_tbl = CALLOC(count, sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReq *));

    allocated = 0;
    for (i = 0; i < request->n_devices; i++)
    {
        dev_apps = request->devices[i];
        if (dev_apps == NULL || dev_apps->header == NULL)
        {
            LOGD("%s(): request header or device id is not set", __func__);
            goto free_pb;
        }

        /* set the header */
        app_req->header = dev_apps->header;

        for (j = 0; j < dev_apps->n_apps; j++)
        {
            app_name = dev_apps->apps[j];
            if (app_name == NULL)
            {
                LOGD("%s(): Invalid app or app name", __func__);
                goto free_pb;
            }

            if (request->req_type == FSM_APP_REQ)
                app_req->appname = app_name;
            else
                app_req->traffic_class = app_name;
            pb_tbl[allocated] = gk_set_pb_app_req(app_req);
            allocated++;
        }
    }
    return pb_tbl;

free_pb:
    for (i = 0; i < allocated; i++)
    {
        gk_free_app_req(pb_tbl[i]);
    }

    FREE(pb_tbl);
    return NULL;
}

/**
 * @brief Perform a bulk lookup request to gatekeeper
 *
 * This is a convenience wrapper that handles the complete request/response cycle:
 * - Serializes the request
 * - Sends it to gatekeeper via curl
 * - Parses the response
 *
 * @param conn_info Connection information (curl handle, server config)
 * @param req The request to send
 * @param reply The reply structure to populate
 * @return true on success, false on failure
 */
bool gk_perform_bulk_lookup(struct gk_connection_info *conn_info, struct gk_request *req, struct gk_reply *reply)
{
    struct gk_curl_data curl_response;
    struct gk_packed_buffer *pb;
    long response_code;
    int result;
    bool ret;

    if (conn_info == NULL || req == NULL || reply == NULL)
    {
        LOGE("%s: Invalid parameters", __func__);
        return false;
    }

    ret = true;

    /* Create the packed buffer for the gatekeeper request */
    pb = gk_serialize_request(req);
    if (pb == NULL)
    {
        LOGE("%s: Failed to serialize request", __func__);
        return false;
    }

    /* Set the packed buffer in the connection info */
    conn_info->pb = pb;

    /* Allocate memory for curl response */
    curl_response.memory = MALLOC(1);
    if (curl_response.memory == NULL)
    {
        LOGE("%s: Failed to allocate memory for curl response", __func__);
        gk_free_packed_buffer(pb);
        return false;
    }
    curl_response.size = 0;

    /* Send the request to gatekeeper */
    result = gk_handle_curl_request(conn_info, &curl_response, &response_code);
    if (result != GK_LOOKUP_SUCCESS)
    {
        LOGW("%s: Failed to get response from gatekeeper (result=%d)", __func__, result);
        ret = false;
        goto cleanup;
    }

    /* Process the curl response */
    LOGT("%s: Received response from gatekeeper", __func__);
    reply->type = FSM_BULK_REQ;
    ret = gk_parse_curl_response(reply, &curl_response);
    if (ret == false)
    {
        LOGE("%s: Failed to parse curl response", __func__);
        goto cleanup;
    }

cleanup:
    FREE(curl_response.memory);
    gk_free_packed_buffer(pb);
    return ret;
}
