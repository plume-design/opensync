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
#include "gatekeeper_bulk_reply_msg.h"
#include "gatekeeper_msg.h"
#include "log.h"
#include "os_nif.h"
#include "memutil.h"

/******************************************************************************
 *  PRIVATE definitions
 *****************************************************************************/
static bool gk_parse_bulk_reply(struct gk_bulk_reply *bulk_reply, Gatekeeper__Southbound__V1__GatekeeperBulkReply *src)
{
    Gatekeeper__Southbound__V1__GatekeeperAppReply *app_reply;
    Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;
    struct gk_device2app_repl *dev_app;
    char mac_str[OS_MACSTR_SZ];

    if (src == NULL || bulk_reply == NULL) return false;

    bulk_reply->n_devices = src->n_reply_app;
    bulk_reply->devices = CALLOC(src->n_reply_app, sizeof(struct gk_device2app_repl *));

    for (size_t i = 0; i < src->n_reply_app; i++)
    {
        app_reply = src->reply_app[i];
        header = app_reply->header;
        dev_app = CALLOC(1, sizeof(struct gk_device2app_repl));

        dev_app->header = CALLOC(1, sizeof(struct gk_reply_header));
        dev_app->app_name = STRDUP(app_reply->app_name);
        dev_app->header->action = gk_get_fsm_action(header);
        dev_app->header->category_id = header->category_id;
        dev_app->header->flow_marker = header->flow_marker;

        /* set device id */
        os_nif_macaddr_to_str((os_macaddr_t *)header->device_id.data, mac_str, PRI_os_macaddr_lower_t);
        dev_app->header->dev_id = STRDUP(mac_str);

        bulk_reply->devices[i] = dev_app;
    }

    return true;
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
        FREE(dev_app->header->dev_id);
        FREE(dev_app->header);
        FREE(dev_app->app_name);
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

/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/

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
