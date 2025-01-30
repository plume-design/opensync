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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "adt_upnp_curl.h"
#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dpi_client_plugin.h"
#include "fsm_dpi_adt_upnp.h"
#include "log.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "network_metadata_report.h"
#include "os.h"
#include "os_nif.h"
#include "os_types.h"
#include "qm_conn.h"
#include "util.h"
#include "fsm_fn_trace.h"

static size_t FSM_DPI_ADT_UPNP_MAX_DATAPOINTS = 10;

/**
 * @brief compare devices macs
 *
 * @param a mac address
 * @param b mac address
 * @return 0 if mac addresses match, an integer otherwise
 */
int
fsm_dpi_adt_upnp_dev_id_cmp(const void *a, const void *b)
{
    const os_macaddr_t *dev_id_a = a;
    const os_macaddr_t *dev_id_b = b;

    return memcmp(dev_id_a->addr, dev_id_b->addr, sizeof(dev_id_a->addr));
}

/**
 * @brief compare two urls
 *
 * @param a url
 * @param b url
 *
 */
int
fsm_dpi_adt_upnp_url_cmp(const void *a, const void *b)
{
    const char *url_a = a;
    const char *url_b = b;

    return strncmp(url_a, url_b, FSM_UPNP_URL_MAX_SIZE);
}

/*
 * Dump content of buffer to the log. It takes the name of the calling function
 * as parameter as it makes the logs easier to scan.
 * Changing the constant num_per_line will realign the formatting accordingly.
 */
void
hexdump(const char *function, const void *data, size_t size)
{
    const int num_per_line = 16;
    char hex[3 * num_per_line + 1];
    char ascii[num_per_line + 1];
    unsigned char c;
    char buf[16];
    size_t i;

    if (function == NULL) function = __func__;
    if ((data == NULL) || (size == 0))
    {
        LOGD("%s: Nothing to be dumped in buffer", function);
        return;
    }

    MEMZERO(ascii);
    MEMZERO(hex);

    for (i = 0; i < size; ++i)
    {
        c = ((unsigned char*)data)[i];
        SPRINTF(buf, "%02X ", c);
        STRSCAT(hex, buf);

        if ((c >= ' ') && (c <= '~'))
        {
            ascii[i % num_per_line] = c;
        }
        else
        {
            ascii[i % num_per_line] = '.';
        }

        if (((i + 1) % num_per_line == 0) || (i + 1 == size))
        {
            /* Using programmable padding to ensure proper alignment */
            LOGD("%s: %-*s |  %s", function, (num_per_line * 3), hex, ascii);
            MEMZERO(ascii);
            MEMZERO(hex);
        }
    }
}

/*
 * Dump content of cached UPnP entries for the devices.
 */
void
dump_upnp_cache(const char *function, ds_tree_t *cache)
{
    struct fsm_dpi_adt_upnp_root_desc *root_desc;
    struct upnp_device *upnp_dev;
    char mac_str[32];

    if (function == NULL) function = __func__;
    if (cache == NULL)
    {
        LOGD("%s: NULL cache pointer", function);
        return;
    }

    LOGD("%s: Start cache dump >>>", function);
    upnp_dev = ds_tree_head(cache);
    while (upnp_dev)
    {
        os_nif_macaddr_to_str(&upnp_dev->device_mac, mac_str, PRI_os_macaddr_t);
        LOGD("%s: Device %s", function, mac_str);

        root_desc = ds_tree_head(&upnp_dev->descriptions);
        while (root_desc) {
            LOGD("%s: url              = %s", function, root_desc->url);
            LOGT("%s: friendly_name    = %s", function, root_desc->friendly_name);
            LOGT("%s: manufacturer     = %s", function, root_desc->manufacturer);
            LOGT("%s: manufacturer_url = %s", function, root_desc->manufacturer_url);
            LOGT("%s: model_desc       = %s", function, root_desc->model_desc);
            LOGD("%s: model_name       = %s", function, root_desc->model_name);
            LOGT("%s: model_num        = %s", function, root_desc->model_num);
            LOGT("%s: model_url        = %s", function, root_desc->model_url);
            LOGT("%s: serial_num       = %s", function, root_desc->serial_num);
            LOGT("%s: udn              = %s", function, root_desc->udn);
            LOGT("%s: upc              = %s", function, root_desc->upc);

            root_desc = ds_tree_next(&upnp_dev->descriptions, root_desc);
        }
        upnp_dev = ds_tree_next(cache, upnp_dev);
    }
    LOGD("%s: <<< End cache dump", function);
}

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin generic fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm
 *
 * @param session pointer provided by fsm
 */
int
fsm_dpi_adt_upnp_init(struct fsm_session *session)
{
    struct fsm_dpi_adt_upnp_session *adt_upnp_session;
    struct fsm_dpi_adt_upnp_report_aggregator *aggr;
    struct fsm_dpi_client_session *client_session;
    struct fsm_dpi_plugin_client_ops *client_ops;
    int ret;

    /* Initialize generic client */
    ret = fsm_dpi_client_init(session);
    if (ret != 0) return ret;

    /* And now all the ADT specific calls */
    session->ops.update = fsm_dpi_adt_upnp_update;
    session->ops.periodic = fsm_dpi_adt_upnp_periodic;
    session->ops.exit = fsm_dpi_adt_upnp_exit;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_adt_upnp_process_attr;
    FSM_FN_MAP(fsm_dpi_adt_upnp_process_attr);

    /* Fetch the specific updates for this client plugin */
    session->ops.update(session);

    client_session = (struct fsm_dpi_client_session *)session->handler_ctxt;
    adt_upnp_session = (struct fsm_dpi_adt_upnp_session *)client_session->private_session;
    if (adt_upnp_session == NULL)
    {
        adt_upnp_session = CALLOC(1, sizeof(*adt_upnp_session));
        if (adt_upnp_session == NULL) return -1;

        adt_upnp_session->adt_upnp_aggr = CALLOC(1, sizeof(*adt_upnp_session->adt_upnp_aggr));
        if (adt_upnp_session->adt_upnp_aggr == NULL)
        {
            FREE(adt_upnp_session);
            return -1;
        }

        adt_upnp_session->last_scan = time(NULL);
        adt_upnp_session->initialized = true;

        /* Initialize libcurl */
        adt_upnp_curl_init(session->loop);

        client_session->private_session = adt_upnp_session;
    }

    /* Initialize the aggregator */
    aggr = adt_upnp_session->adt_upnp_aggr;
    if (aggr->initialized) return 0;

    aggr->node_id = STRDUP(session->node_id);
    if (aggr->node_id == NULL) goto cleanup;
    aggr->location_id = STRDUP(session->location_id);
    if (aggr->location_id == NULL) goto cleanup;

    aggr->data_max = FSM_DPI_ADT_UPNP_MAX_DATAPOINTS;
    aggr->data_idx = 0;
    aggr->data_prov = aggr->data_max;
    aggr->data = CALLOC(aggr->data_prov, sizeof(*aggr->data));
    if (aggr->data == NULL) goto cleanup;

    /* Make eventual testing easier */
    aggr->send_report = qm_conn_send_direct;

    aggr->initialized = true;

    /* Initialize the device cache */
    ds_tree_init(&adt_upnp_session->session_upnp_devices,
                 fsm_dpi_adt_upnp_dev_id_cmp,
                 struct upnp_device, next_node);

    /* All done */
    adt_upnp_session->initialized = true;
    LOGD("%s: Added session %s", __func__, session->name);

    return 0;

cleanup:
    fsm_dpi_client_exit(session);

    return -1;
}


/*
 * Provided for compatibility
 */
int
dpi_adt_upnp_plugin_init(struct fsm_session *session)
{
    return fsm_dpi_adt_upnp_init(session);
}

void
fsm_dpi_adt_upnp_exit(struct fsm_session *session)
{
    struct fsm_dpi_adt_upnp_session *adt_upnp_session;
    struct fsm_dpi_adt_upnp_report_aggregator *aggr;

    adt_upnp_session = fsm_dpi_adt_upnp_get_session(session);
    if (adt_upnp_session == NULL) goto clean_all;

    aggr = adt_upnp_session->adt_upnp_aggr;
    if (aggr == NULL) goto clean_all;
    if (!aggr->initialized) goto clean_all;

    // dpi_adt_upnp_send_report(session);

    /* This should be empty, but make extra sure */
    // dpi_adt_upnp_free_aggr_store(aggr);

    FREE(aggr->data);
    FREE(aggr->location_id);
    FREE(aggr->node_id);
    FREE(aggr);

    /* Clear the local cache */
    fsm_dpi_adt_free_cache(&adt_upnp_session->session_upnp_devices);

    /* Release cURL */
    adt_upnp_curl_exit();

    FREE(adt_upnp_session);


clean_all:
    /* Free the generic client */
    fsm_dpi_client_exit(session);
}

void
fsm_dpi_adt_free_cache(ds_tree_t *cache)
{
    struct fsm_dpi_adt_upnp_root_desc *root_desc, *rm_root_desc;
    struct upnp_device *upnp_dev, *rm_upnp_dev;

    upnp_dev = ds_tree_head(cache);
    while (upnp_dev)
    {
        root_desc = ds_tree_head(&upnp_dev->descriptions);
        while (root_desc)
        {
            rm_root_desc = root_desc;
            root_desc = ds_tree_next(&upnp_dev->descriptions, root_desc);

            ds_tree_remove(&upnp_dev->descriptions, rm_root_desc);
            FREE(rm_root_desc);
        }

        rm_upnp_dev = upnp_dev;
        upnp_dev = ds_tree_next(cache, upnp_dev);

        ds_tree_remove(cache, rm_upnp_dev);
        FREE(rm_upnp_dev);
    }
}

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_adt_upnp_update(struct fsm_session *session)
{
    /* Generic config first */
    fsm_dpi_client_update(session);

    /* ADT specific entries */
    LOGD("%s: Updating ADT UPnP config", __func__);
}

void
fsm_dpi_adt_upnp_periodic(struct fsm_session *session)
{
    struct fsm_dpi_adt_upnp_session *adt_upnp_session;
    bool need_periodic;
    double cmp;
    time_t now;

    need_periodic = fsm_dpi_client_periodic_check(session);
    if (!need_periodic) return;

    fsm_dpi_client_periodic(session);

    adt_upnp_session = fsm_dpi_adt_upnp_get_session(session);
    now = time(NULL);
    cmp = difftime(now, adt_upnp_session->last_scan);
    if (cmp < PROBE_UPNP) return;

    /*
    * Trigger a scan by sending a M-SEARCH. The responses will be picked up
    * the same way as advertisements
    */
    LOGT("%s: Trigger a UPnP scan", __func__);
    fsm_dpi_adt_upnp_send_probe();
    adt_upnp_session->last_scan = now;

    /* Dump the content of the cache to log */
    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_TRACE))
    {
        dump_upnp_cache(__func__, &adt_upnp_session->session_upnp_devices);
    }

    /* Since we are sending reports without buffering, this should be a no-op */
    // TODO: reporting is disabled at this point
    // fsm_dpi_adt_upnp_send_report(session);
}

/**
 * @brief process a flow attribute
 *
 * @param session the fsm session
 * @param attr the attribute name
 * @param value the attribute value
 * @param acc the flow
 */
int
fsm_dpi_adt_upnp_process_attr(struct fsm_session *session, const char *attr,
                              uint8_t type, uint16_t length, const void *value,
                              struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    char rootdevice_url[FSM_UPNP_URL_MAX_SIZE];
    int action = FSM_DPI_PASSTHRU;
    int cmp;

    if (session == NULL)
    {
        LOGT("%s: Session is NULL", __func__);
        goto report_action;
    }
    if (attr == NULL || attr[0] == '\0')
    {
        LOGT("%s: Invalid attribute key", __func__);
        goto report_action;
    }
    if (value == NULL || length == 0)
    {
        LOGT("%s: Invalid value for attr %s", __func__, attr);
        goto report_action;
    }

    /* Process the generic part (e.g., logging, include, exclude lists) */
    action = fsm_dpi_client_process_attr(session, attr, type, length, value, pkt_info);
    if (action == FSM_DPI_IGNORED)
    {
        LOGD("%s: Nothing to be reported for %s", __func__, attr);
        action = FSM_DPI_PASSTHRU;
        goto report_action;
    }

    /* Do we have the URL from where to fetch the actual content of the rootdevice ? */
    cmp = strcmp(attr, "ssdp.location");
    if (cmp == 0)
    {
        if (type != RTS_TYPE_STRING)
        {
            LOGD("%s: value for %s should be a string", __func__, attr);
            goto report_action;
        }

        STRSCPY_LEN(rootdevice_url, value, length);
        fsm_dpi_adt_upnp_process_notify(session, rootdevice_url, pkt_info);

        goto report_action;
    }

    LOGD("%s: Ignoring %s", __func__, attr);

report_action:
    return action;
};

struct fsm_dpi_adt_upnp_session *
fsm_dpi_adt_upnp_get_session(struct fsm_session *session)
{
    struct fsm_dpi_adt_upnp_session *adt_upnp_session;
    struct fsm_dpi_client_session *client_session;

    client_session = (struct fsm_dpi_client_session *)session->handler_ctxt;
    if (!client_session) return NULL;
    adt_upnp_session = (struct fsm_dpi_adt_upnp_session *)client_session->private_session;
    return adt_upnp_session;
}

struct fsm_dpi_adt_upnp_root_desc *
fsm_dpi_adt_upnp_get_device(struct fsm_dpi_adt_upnp_session *session, os_macaddr_t *device_mac,
                            char *rootdevice_url)
{
    struct fsm_dpi_adt_upnp_root_desc *root_desc;
    struct upnp_device *upnp_dev;

    if (session == NULL) return NULL;
    if (device_mac == NULL) return NULL;
    if (rootdevice_url == NULL || rootdevice_url[0] == '\0') return NULL;

    upnp_dev = ds_tree_find(&session->session_upnp_devices, device_mac);
    if (upnp_dev == NULL)
    {
        LOGT("%s: Creating new device entry for MAC", __func__);
        upnp_dev = CALLOC(1, sizeof(*upnp_dev));

        ds_tree_init(&upnp_dev->descriptions, fsm_dpi_adt_upnp_url_cmp,
                     struct fsm_dpi_adt_upnp_root_desc, next_node);
        MEM_CPY(&upnp_dev->device_mac, device_mac, sizeof(*device_mac));

        ds_tree_insert(&session->session_upnp_devices, upnp_dev, &upnp_dev->device_mac);
    }

    root_desc = ds_tree_find(&upnp_dev->descriptions, rootdevice_url);
    if (root_desc == NULL)
    {
        LOGT("%s: Create new root-desc entry for MAC with %s", __func__, rootdevice_url);
        root_desc = CALLOC(1, sizeof(*root_desc));
        root_desc->state = FSM_DPI_ADT_UPNP_INIT;
        STRLCPY(root_desc->url, rootdevice_url);

        ds_tree_insert(&upnp_dev->descriptions, root_desc, rootdevice_url);

        root_desc->udev = upnp_dev;
    }

    return root_desc;
}

void
fsm_dpi_adt_upnp_send_probe(void)
{
    char *request =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST:239.255.255.250:1900\r\n"
        "MAN:\"ssdp:discover\"\r\n"
        "ST:ssdp:all\r\n"
        "MX:1\r\n\r\n";
    char *bcast_addr = "239.255.255.250";
    uint16_t bcast_port = 1900;
    struct sockaddr_in mcast;
    int so_broadcast = 1;
    char *err_str;
    int req_size;
    int ret;
    int sd;

    sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sd < 0)
    {
        err_str = strerror(errno);
        LOGE("%s: Could not open socket - %s", __func__, err_str);
        goto fail;
    }

    ret = setsockopt(sd, SOL_SOCKET, SO_BROADCAST,
                     &so_broadcast, sizeof(so_broadcast));
    if (ret)
    {
        err_str = strerror(errno);
        LOGE("%s: Could not set socket to broadcast mode - %s", __func__, err_str);
        goto fail;
    }

    MEMZERO(mcast);
    mcast.sin_family = AF_INET;
    inet_pton(AF_INET, bcast_addr, &mcast.sin_addr);
    mcast.sin_port = htons(bcast_port);

    /* Send the broadcast request */
    req_size = strlen(request);
    ret = sendto(sd, request, req_size, 0,
                 (struct sockaddr *)&mcast, sizeof(mcast));
    if (ret < 0)
    {
        err_str = strerror(errno);
        LOGE("%s: Could not open send broadcast message - %s", __func__, err_str);
        goto fail;
    }
    else if (ret != req_size)
    {
        LOGW("%s: Did not send entire request (%d out of %d)", __func__, ret, req_size);
        goto fail;
    }

fail:
    if (sd >= 0) close(sd);
    return;
}

/*
 * By construction, rootdevice_url cannot be NULL or zero length (no further checks)
 */
int
fsm_dpi_adt_upnp_process_notify(struct fsm_session *session, char *rootdevice_url,
                                struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct fsm_dpi_adt_upnp_root_desc *upnp_device;
    struct fsm_dpi_adt_upnp_session *upnp_session;
    struct net_md_stats_accumulator *acc;
    struct net_md_flow_info info;
    char ip_str[INET6_ADDRSTRLEN];
    int domain;
    char *cmp;
    bool rc;

    LOGD("%s: Processing for '%s'.", __func__, rootdevice_url);

    upnp_session = fsm_dpi_adt_upnp_get_session(session);
    LOGD("%s: fetched private session as %d", __func__, upnp_session->initialized);

    /* Need to verify promptly if we have a broken location or URL of some sorts */
    /* e.g., the url should contain the IP address of the source device ! */
    acc = pkt_info->acc;
    if (acc == NULL)
    {
        LOGD("%s: No accumulator attached", __func__);
        return NO_ACC;
    }

    MEMZERO(info);
    rc = net_md_get_flow_info(acc, &info);
    if (!rc)
    {
        LOGD("%s: No flow information", __func__);
        return NO_INFO;
    }

    // Check direction here to pick the local or remote
    // or extract from acc->key->src_ip !
    domain = ((info.ip_version == 4) ? AF_INET : AF_INET6);
    inet_ntop(domain, info.local_ip, ip_str, sizeof(ip_str));

// Temporarily enabling debugging
#define DBG_PAYLOAD 1
#if DBG_PAYLOAD == 1
    hexdump(__func__, pkt_info->parser->start, pkt_info->parser->packet_len);
#endif

    cmp = strstr(rootdevice_url, ip_str);
    if (cmp == NULL)
    {
        LOGD("%s: Not fetching broken location : %s (ip==%s) (dir==%d)", __func__,
             rootdevice_url, ip_str, info.direction);
        return WRONG_IP;
    }

    /*
     * Check if we already have this entry in the cache, creating a
     * new entry if not present.
     */
    upnp_device = fsm_dpi_adt_upnp_get_device(upnp_session, info.local_mac, rootdevice_url);
    if (upnp_device == NULL)
    {
        LOGW("%s: Passing NULL parameter(s)", __func__);
        return NULL_PARAM;
    }
    if (upnp_device->session == NULL) upnp_device->session = session;

    /* If the cache entry is not populated, then fetch XML document */
    if (upnp_device->state == FSM_DPI_ADT_UPNP_INIT)
    {
        LOGT("%s: Calling adt_upnp_call_mcurl() for %s", __func__, rootdevice_url);
        adt_upnp_call_mcurl(upnp_device);
    }
    else
    {
        LOGD("%s: We already have info for %s or %s", __func__, rootdevice_url, upnp_device->url);
    }

    return 0;
}
