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

#include "fcm.h"
#include "fcm_mgr.h"
#include "fsm_policy.h"
#include "gatekeeper.pb-c.h"
#include "gatekeeper_bulk_reply_msg.h"
#include "gatekeeper_ecurl.h"
#include "gatekeeper_msg.h"
#include "log.h"
#include "memutil.h"
#include "os_nif.h"
#include "os_types.h"

/******************************************************************************
 *  PRIVATE definitions
 *****************************************************************************/
static bool fcm_initialize_curl_handler(void)
{
    struct gk_curl_easy_info *ecurl;
    fcm_mgr_t *mgr;
    bool success;

    mgr = fcm_get_mgr();
    ecurl = &mgr->ecurl;

    /* check if the handler is already initialized */
    if (ecurl->curl_handle != NULL) return true;

    success = gk_curl_easy_init(ecurl);
    if (!success)
    {
        LOGN("%s(): Failed to initialize curl handler", __func__);
        return false;
    }

    return true;
}

/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/
/*
 * @brief Sends the request to the gatekeeper service, then handles the response.
 * Parses the reply and sets the verdict in the request structure
 * @param req the request to lookup
 * @return true on success, false on failure
 */
bool fcm_gk_lookup(struct gk_request *req, struct gk_reply *reply)
{
    struct gk_connection_info conn_info;
    struct gk_curl_data curl_response;
    struct gk_packed_buffer *pb;
    long response_code;
    fcm_mgr_t *mgr;
    int result;
    bool ret;

    ret = true;
    mgr = fcm_get_mgr();

    /* initialize curl handler */
    result = fcm_initialize_curl_handler();
    if (result == false)
    {
        LOGN("%s(): Failed to initialize curl handler", __func__);
        return false;
    }

    /* Create the packed buffer for the gatekeeper request */
    pb = gk_serialize_request(req);
    if (pb == NULL) return false;

    /* set the connection info */
    conn_info.ecurl = &mgr->ecurl;
    conn_info.server_conf = &mgr->gk_conf;
    conn_info.pb = pb;

    /* Allocate memory for curl response */
    curl_response.memory = MALLOC(1);
    curl_response.size = 0;

    /* send the request to gatekeeper */
    result = gk_handle_curl_request(&conn_info, &curl_response, &response_code);
    if (result != GK_LOOKUP_SUCCESS)
    {
        LOGW("%s: Failed to get response from gatekeeper", __func__);
        ret = false;
        goto error;
    }

    /* process the curl response */
    LOGT("%s: Received response from gatekeeper", __func__);
    reply->type = FSM_BULK_REQ;
    ret = gk_parse_curl_response(reply, &curl_response);
    if (ret == false) goto error;

error:
    FREE(curl_response.memory);
    gk_free_packed_buffer(pb);
    return ret;
}
