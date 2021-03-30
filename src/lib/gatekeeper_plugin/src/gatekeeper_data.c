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

#include <stdbool.h>

#include "gatekeeper_data.h"
#include "gatekeeper_msg.h"
#include "gatekeeper.h"
#include "fsm_policy.h"
#include "log.h"


/**
 * @brief allocates and fills up a gatekeeper request header
 *
 * @param req_data gatekeepr requet context
 * @return a pointer to an allocated header
 */
static struct gk_req_header *
gatekeeper_allocate_req_header(struct gk_request_data *req_data)
{
    struct gk_req_header *header;
    struct fsm_session *session;
    struct fsm_policy_req *req;

    session = req_data->session;
    if (session == NULL) return NULL;

    req = req_data->req;
    if (req == NULL) return NULL;

    header = calloc(1, sizeof(*header));
    if (header == NULL) return NULL;

    header->dev_id = req->device_id;
    header->location_id = session->location_id;
    header->node_id = session->node_id;

    if (req->policy != NULL)
    {
        header->policy_rule = req->policy->rule_name;
    }

    return header;
}


static uint32_t
gatekeeper_update_counter(uint32_t *counter)
{
    if (*counter == UINT32_MAX) *counter = 1;
    else *counter += 1;

    return *counter;
}


/**
 * @brief returns the header pointer of a gatekeeper request
 *
 * @param gk_req the gatekeeper request
 * @return a pointer to the request header
 */
static struct gk_req_header *
gatekeeper_get_header(struct gk_request *gk_req)
{
    struct gk_req_header *header;
    union gk_data_req *req_data;

    if (gk_req == NULL) return NULL;

    req_data = &gk_req->req;

    header = NULL;
    req_data = &gk_req->req;

    switch (gk_req->type)
    {
        case FSM_FQDN_REQ:
        {
            struct gk_fqdn_request *gk_fqdn_req;

            gk_fqdn_req = &req_data->gk_fqdn_req;
            header = gk_fqdn_req->header;
            break;
        }

        case FSM_SNI_REQ:
        {
            struct gk_sni_request *gk_sni_req;

            gk_sni_req = &req_data->gk_sni_req;
            header = gk_sni_req->header;
            break;
        }

        case FSM_HOST_REQ:
        {
            struct gk_host_request *gk_host_req;

            gk_host_req = &req_data->gk_host_req;
            header = gk_host_req->header;
            break;
        }

        case FSM_URL_REQ:
        {
            struct gk_url_request *gk_url_req;

            gk_url_req = &req_data->gk_url_req;
            header = gk_url_req->header;
            break;
        }

        case FSM_APP_REQ:
        {
            struct gk_app_request *gk_app_req;

            gk_app_req = &req_data->gk_app_req;
            header = gk_app_req->header;
            break;
        }

        case FSM_IPV4_REQ:
        {
            struct gk_ip_request *gk_ip_req;

            gk_ip_req = &req_data->gk_ip_req;
            header = gk_ip_req->header;
            break;
        }

        case FSM_IPV6_REQ:
        {
            struct gk_ip_request *gk_ip_req;

            gk_ip_req = &req_data->gk_ip_req;
            header = gk_ip_req->header;
            break;
        }

        case FSM_IPV4_FLOW_REQ:
        {
            struct gk_ip_flow_request *gk_ip_flow_req;

            gk_ip_flow_req = &req_data->gk_ip_flow_req;
            header = gk_ip_flow_req->header;
            break;
        }

        case FSM_IPV6_FLOW_REQ:
        {
            struct gk_ip_flow_request *gk_ip_flow_req;

            gk_ip_flow_req = &req_data->gk_ip_flow_req;
            header = gk_ip_flow_req->header;
            break;
        }

        default:
            LOGN("%s() invalid request type %d", __func__, gk_req->type);
            break;
    }

    return header;
}


/**
 * @brief set a gatekeeper request' unique id
 * @param gk_req the gatekeeper request
 */
static bool
gatekeeper_set_req_id(struct gk_request *gk_req)
{
    struct gk_req_header *header;
    struct fsm_gk_mgr *mgr;
    uint32_t *id;

    mgr = gatekeeper_get_mgr();

    if (gk_req == NULL) return false;

    header = gatekeeper_get_header(gk_req);
    if (header == NULL) return false;

    id  = NULL;

    switch (gk_req->type)
    {
        case FSM_FQDN_REQ:
            id = &mgr->req_ids.req_fqdn_id;
            break;

        case FSM_SNI_REQ:
            id = &mgr->req_ids.req_https_sni_id;
            break;

        case FSM_HOST_REQ:
            id = &mgr->req_ids.req_http_host_id;
            break;

        case FSM_URL_REQ:
            id = &mgr->req_ids.req_http_url_id;
            break;

        case FSM_APP_REQ:
            id = &mgr->req_ids.req_app_id;
            break;

        case FSM_IPV4_REQ:
            id = &mgr->req_ids.req_ipv4_id;
            break;

        case FSM_IPV6_REQ:
            id = &mgr->req_ids.req_ipv6_id;
            break;

        case FSM_IPV4_FLOW_REQ:
            id = &mgr->req_ids.req_ipv4_tuple_id;
            break;

        case FSM_IPV6_FLOW_REQ:
            id = &mgr->req_ids.req_ipv6_tuple_id;
            break;

        default:
            LOGN("%s() invalid request type %d", __func__, gk_req->type);
            return false;
    }

    header->req_id = gatekeeper_update_counter(id);

    return true;
}


static bool
gatekeeper_get_fqdn_req(struct gk_request_data *request_data)
{
    struct gk_fqdn_request *gk_fqdn_req;
    struct fsm_policy_req *policy_req;
    struct gk_req_header *header;
    union gk_data_req *req_data;
    struct gk_request *req;

    if (request_data == NULL) return false;

    policy_req = request_data->req;
    if (policy_req == NULL) return false;

    req = request_data->gk_req;
    req_data = &req->req;

    header = gatekeeper_allocate_req_header(request_data);
    if (header == NULL) return false;

    gk_fqdn_req = &req_data->gk_fqdn_req;
    gk_fqdn_req->header = header;
    gk_fqdn_req->fqdn = policy_req->url;

    return true;
}


static bool
gatekeeper_get_ip_req(struct gk_request_data *request_data)
{
    struct fsm_policy_req *policy_req;
    struct gk_ip_request *gk_ip_req;
    struct gk_req_header *header;
    union gk_data_req *req_data;
    struct gk_request *req;

    if (request_data == NULL) return false;

    policy_req = request_data->req;
    if (policy_req == NULL) return NULL;

    req = request_data->gk_req;
    req_data = &req->req;

    header = gatekeeper_allocate_req_header(request_data);
    if (header == NULL) return false;

    gk_ip_req = &req_data->gk_ip_req;
    gk_ip_req->header = header;
    gk_ip_req->acc = policy_req->acc;

    return true;
}


static bool
gatekeeper_get_url_req(struct gk_request_data *request_data)
{
    struct fsm_policy_req *policy_req;
    struct gk_url_request *gk_url_req;
    struct gk_req_header *header;
    union gk_data_req *req_data;
    struct gk_request *req;

    if (request_data == NULL) return false;

    policy_req = request_data->req;
    if (policy_req == NULL) return false;

    req = request_data->gk_req;
    req_data = &req->req;

    header = gatekeeper_allocate_req_header(request_data);
    if (header == NULL) return false;

    gk_url_req = &req_data->gk_url_req;
    gk_url_req->header = header;
    gk_url_req->url = policy_req->url;

    return true;
}


static bool
gatekeeper_get_http_host_req(struct gk_request_data *request_data)
{
    struct gk_host_request *gk_host_req;
    struct fsm_policy_req *policy_req;
    struct gk_req_header *header;
    union gk_data_req *req_data;
    struct gk_request *req;

    if (request_data == NULL) return false;

    policy_req = request_data->req;
    if (policy_req == NULL) return false;

    req = request_data->gk_req;
    req->type = FSM_HOST_REQ;
    req_data = &req->req;

    header = gatekeeper_allocate_req_header(request_data);
    if (header == NULL) return false;

    gk_host_req = &req_data->gk_host_req;
    gk_host_req->header = header;
    gk_host_req->host = policy_req->url;

    return true;
}


static bool
gatekeeper_get_sni_req(struct gk_request_data *request_data)
{
    struct fsm_policy_req *policy_req;
    struct gk_sni_request *gk_sni_req;
    struct gk_req_header *header;
    union gk_data_req *req_data;
    struct gk_request *req;

    if (request_data == NULL) return false;

    policy_req = request_data->req;
    if (policy_req == NULL) return false;

    req = request_data->gk_req;
    req->type = FSM_SNI_REQ;
    req_data = &req->req;

    header = gatekeeper_allocate_req_header(request_data);
    if (header == NULL) return false;

    gk_sni_req = &req_data->gk_sni_req;
    gk_sni_req->header = header;
    gk_sni_req->sni = policy_req->url;

    return true;
}


static bool
gatekeeper_get_app_req(struct gk_request_data *request_data)
{
    struct fsm_policy_req *policy_req;
    struct gk_app_request *gk_app_req;
    struct gk_req_header *header;
    union gk_data_req *req_data;
    struct gk_request *req;

    if (request_data == NULL) return false;

    policy_req = request_data->req;
    if (policy_req == NULL) return false;

    req = request_data->gk_req;
    req->type = FSM_APP_REQ;
    req_data = &req->req;

    header = gatekeeper_allocate_req_header(request_data);
    if (header == NULL) return false;

    gk_app_req = &req_data->gk_app_req;
    gk_app_req->header = header;
    gk_app_req->appname = policy_req->url;

    return true;
}


static void
gatekeeper_free_req(struct gk_request *gk_req)
{
    struct gk_req_header *header;

    header = gatekeeper_get_header(gk_req);
    free(header);
    free(gk_req);
}


void
gatekeeper_free_pb(struct gk_packed_buffer *pb)
{
    gk_free_packed_buffer(pb);
}


struct gk_packed_buffer *
gatekeeper_get_req(struct fsm_session *session,
                   struct fsm_policy_req *req)
{
    struct gk_request_data request_data;
    struct gk_packed_buffer *pb = NULL;
    struct gk_request *gk_req;
    int req_type;
    bool rc;

    gk_req = calloc(1, sizeof(*gk_req));
    if (gk_req == NULL) return NULL;

    request_data.session = session;
    request_data.req = req;
    request_data.gk_req = gk_req;

    rc = true;
    req_type = fsm_policy_get_req_type(req);
    gk_req->type = req_type;

    LOGT("%s(): request type %d, url %s", __func__, req_type, req->url);

    switch (req_type)
    {
        case FSM_FQDN_REQ:
            rc = gatekeeper_get_fqdn_req(&request_data);
            break;

        case FSM_SNI_REQ:
            rc = gatekeeper_get_sni_req(&request_data);
            break;

        case FSM_HOST_REQ:
            rc = gatekeeper_get_http_host_req(&request_data);
            break;

        case FSM_URL_REQ:
            rc = gatekeeper_get_url_req(&request_data);
            break;

        case FSM_APP_REQ:
            rc = gatekeeper_get_app_req(&request_data);
            break;

        case FSM_IPV4_REQ:
        case FSM_IPV6_REQ:
            rc = gatekeeper_get_ip_req(&request_data);
            break;


        case FSM_IPV4_FLOW_REQ:
            rc = false;
            break;

        case FSM_IPV6_FLOW_REQ:
            rc = false;
            break;

        default:
            LOGN("%s() invalid request type %d", __func__, req_type);
            rc = false;
            break;
    }

    if (!rc) goto err;

    rc = gatekeeper_set_req_id(gk_req);
    if (!rc) goto err;

    pb = gk_serialize_request(gk_req);

err:
    gatekeeper_free_req(gk_req);
    return pb;
}
