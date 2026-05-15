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
#include "gatekeeper_bulk_msg.h"
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
    fcm_mgr_t *mgr;
    bool ret;

    mgr = fcm_get_mgr();

    /* initialize curl handler */
    ret = fcm_initialize_curl_handler();
    if (ret == false)
    {
        LOGN("%s(): Failed to initialize curl handler", __func__);
        return false;
    }

    /* set the connection info */
    conn_info.ecurl = &mgr->ecurl;
    conn_info.server_conf = &mgr->gk_conf;
    conn_info.pb = NULL; /* Will be set by gk_perform_bulk_lookup */

    /* Perform the bulk lookup using the gatekeeper_msg API */
    ret = gk_perform_bulk_lookup(&conn_info, req, reply);
    if (ret == false)
    {
        LOGW("%s: Bulk lookup failed", __func__);
        return false;
    }

    return ret;
}
