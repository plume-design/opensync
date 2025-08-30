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

#include <regex.h>
#include <stdbool.h>

#include "fsm_dpi_utils.h"
#include "gatekeeper_data.h"
#include "gatekeeper_msg.h"
#include "gatekeeper.h"
#include "fsm_policy.h"
#include "memutil.h"
#include "fsm.h"
#include "log.h"

/**
 * @brief validates a FQDN/SNI/Hostname string
 *
 * @param site to attribute to validate
 * @return true if it is a valid attribute, false otherwise
 */
bool
gatekeeper_validate_fqdn(struct fsm_session *session, char *site)
{

    struct fsm_gk_session *gk_session;
    size_t len;
    int rc;

    if (session == NULL) return false;

    gk_session = session->handler_ctxt;
    if (gk_session == NULL) return false;

    /* returns false upon empty string */
    if (site == NULL)
    {
        LOGD("%s: string is NULL", __func__);
        return false;
    }

    len = strlen(site);
    if (len == 0)
    {
        LOGE("%s: string length is NULL", __func__);
        return false;
    }

    /* check fqdn < 255, to abide by RFC 1032 defined size */
    if (len > 255)
    {
        LOGD("%s: Invalid FQDN length: %zu, string exceeds 255 characters\n",
             __func__, len);
        return false;
    }

    /* generate regex match */
    rc = regexec(gk_session->re, site, (size_t)0, NULL, 0);
    if (rc != 0)
    {
        LOGD("%s: Invalid FQDN: (%d) Failed to match \"%s\"\n", __func__,
             rc, gk_session->pattern_fqdn);
        goto err;
    }

    /* generate regex match */
    rc = regexec(gk_session->re_lan, site, (size_t)0, NULL, 0);
    if (rc == 0)
    {
        LOGD("%s: Dropping lan FQDN: %s: matched \"%s\"\n", __func__,
             site, gk_session->pattern_fqdn_lan);
        goto err;
    }

    return true;

err:
    return false;
}


/**
 * @brief maps a request type to a string
 *
 * @param req_type the request type
 * @return a string representing the request type
 */
char *
gatekeeper_req_type_to_str(int req_type)
{
    struct req_type_mapping
    {
        int req_type;
        char *req_type_str;
    } mapping[] =
    {
        {
            .req_type = FSM_UNKNOWN_REQ_TYPE,
            .req_type_str = "unknown type",
        },
        {
            .req_type = FSM_FQDN_REQ,
            .req_type_str = "fqdn",
        },
        {
            .req_type = FSM_URL_REQ,
            .req_type_str = "http_url",
        },
        {
            .req_type = FSM_HOST_REQ,
            .req_type_str = "http_host",
        },
        {
            .req_type = FSM_SNI_REQ,
            .req_type_str = "https_sni",
        },
        {
            .req_type = FSM_IPV4_REQ,
            .req_type_str = "ipv4",
        },
        {
            .req_type = FSM_IPV6_REQ,
            .req_type_str = "ipv6",
        },
        {
            .req_type = FSM_APP_REQ,
            .req_type_str = "app",
        },
        {
            .req_type = FSM_IPV4_FLOW_REQ,
            .req_type_str = "ipv4_tuple",
        },
        {
            .req_type = FSM_IPV6_FLOW_REQ,
            .req_type_str = "ipv6_tuple",
        }
    };

    size_t len;
    size_t i;

    len = sizeof(mapping) / sizeof(mapping[0]);
    for (i = 0; i < len; i++)
    {
        if (req_type == mapping[i].req_type) return mapping[i].req_type_str;
    }

    return NULL;
}


static void
gatekeeper_add_netid(struct gk_req_header *header, struct gk_request_data *req_data)
{
    struct fsm_policy_req *req;

    req = req_data->req;
    if (req == NULL) return;

    header->network_id = fsm_ops_get_network_id(req_data->session, req->device_id);
    req->network_id = header->network_id;

    LOGT("%s(): set network id to '%s' in gatekeeper request", __func__, header->network_id);
}


/**
 * @brief allocates and fills up a gatekeeper request header
 *
 * @param req_data gatekeepr request context
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

    header = CALLOC(1, sizeof(*header));
    if (header == NULL) return NULL;

    header->dev_id = req->device_id;
    header->location_id = session->location_id;
    header->node_id = session->node_id;
    header->supported_features = req->supported_features;
    gatekeeper_add_netid(header, req_data);

    if (req->policy != NULL)
    {
        header->policy_rule = req->policy->rule_name;
    }
    LOGT("%s(): network id in gw header %s", __func__, header->network_id);

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
            LOGD("%s() invalid request type %d", __func__, gk_req->type);
            break;
    }

    return header;
}


/**
 * @brief set a gatekeeper request' unique id
 * @param gk_req the gatekeeper request
 * @mcurl_data mcurl data, will be NULL if using easy curl
 */
static bool
gatekeeper_set_req_id(struct gk_request *gk_req, struct gk_mcurl_data *mcurl_data)
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
            LOGD("%s() invalid request type %d", __func__, gk_req->type);
            return false;
    }

    header->req_id = gatekeeper_update_counter(id);

    LOGT("%s(): request id set to %d for request type %d",
         __func__,
         header->req_id,
         gk_req->type);

    if (mcurl_data) {
        mcurl_data->req_id = header->req_id;
    }

    return true;
}


static bool
gatekeeper_get_fqdn_req(struct gk_request_data *request_data)
{
    struct gk_fqdn_request *gk_fqdn_req;
    struct fsm_policy_req *policy_req;
    struct gk_req_header *header;
    struct fsm_session *service;
    struct fsm_session *session;
    union gk_data_req *req_data;
    struct gk_request *req;

    bool rc;

    if (request_data == NULL) return false;

    policy_req = request_data->req;
    if (policy_req == NULL) return false;

    if (policy_req->url == NULL)
    {
        LOGE("%s(): no fqdn provided", __func__);
        return false;
    }

    session = request_data->session;
    if (session == NULL) return false;

    service = session->service;
    if (service == NULL) return NULL;

    rc = gatekeeper_validate_fqdn(service, policy_req->url);
    if (!rc)
    {
        LOGD("%s: invalid fqdn %s", __func__, policy_req->url);
        return false;
    }

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
    struct fsm_session *service;
    struct fsm_session *session;
    union gk_data_req *req_data;
    struct gk_request *req;
    bool rc;

    if (request_data == NULL) return false;

    policy_req = request_data->req;
    if (policy_req == NULL) return false;

    if (policy_req->url == NULL)
    {
        LOGE("%s(): no http host provided", __func__);
        return false;
    }

    session = request_data->session;
    if (session == NULL) return false;

    service = session->service;
    if (service == NULL) return NULL;

    rc = gatekeeper_validate_fqdn(service, policy_req->url);
    if (!rc)
    {
        LOGD("%s: invalid http host %s", __func__, policy_req->url);
        return false;
    }

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
    struct fsm_session *service;
    struct fsm_session *session;
    union gk_data_req *req_data;
    struct gk_request *req;
    bool rc;

    if (request_data == NULL) return false;

    policy_req = request_data->req;
    if (policy_req == NULL) return false;

    if (policy_req->url == NULL)
    {
        LOGE("%s(): no sni provided", __func__);
        return false;
    }

    session = request_data->session;
    if (session == NULL) return false;

    service = session->service;
    if (service == NULL) return NULL;

    rc = gatekeeper_validate_fqdn(service, policy_req->url);
    if (!rc)
    {
        LOGD("%s: invalid sni %s", __func__, policy_req->url);
        return false;
    }

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

    if (policy_req->url == NULL)
    {
        LOGE("%s(): no app name provided", __func__);
        return false;
    }

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
    FREE(header);
    FREE(gk_req);
}


void
gatekeeper_free_pb(struct gk_packed_buffer *pb)
{
    gk_free_packed_buffer(pb);
}


struct gk_packed_buffer *
gatekeeper_get_req(struct fsm_session *session,
                   struct fsm_policy_req *req,
                   struct gk_mcurl_data *mcurl_data)
{
    struct gk_request_data request_data;
    struct gk_packed_buffer *pb = NULL;
    struct gk_request *gk_req;
    char *req_type_str;
    int req_type;
    bool rc;

    gk_req = CALLOC(1, sizeof(*gk_req));
    if (gk_req == NULL) return NULL;

    request_data.session = session;
    request_data.req = req;
    request_data.gk_req = gk_req;

    req_type = fsm_policy_get_req_type(req);
    req_type_str = gatekeeper_req_type_to_str(req_type);

    LOGT("%s(): request type %s, request attribute: %s", __func__,
         req_type_str != NULL ? req_type_str : "unknown",
         req->url ? req->url : "url not set");

    gk_req->type = req_type;

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
            LOGD("%s() invalid request type %d", __func__, req_type);
            rc = false;
            break;
    }

    if (!rc) goto err;

    rc = gatekeeper_set_req_id(gk_req, mcurl_data);
    if (!rc) goto err;

    pb = gk_serialize_request(gk_req);

err:
    gatekeeper_free_req(gk_req);
    return pb;
}
