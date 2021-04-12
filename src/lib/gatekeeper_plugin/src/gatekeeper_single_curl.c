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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "gatekeeper_single_curl.h"
#include "gatekeeper.pb-c.h"
#include "gatekeeper.h"
#include "fsm_policy.h"
#include "log.h"
#include "util.h"

/**
 * @brief set callback for writing received data,
 *        invoked by CURLOPT_WRITEFUNCTION
 * @param gk_verdict structure containing gatekeeper server
 *        information, proto buffer and fsm_policy_req
 * @param response unpacked curl response
 * @return None
 */
static size_t
gk_curl_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    struct gk_curl_data *mem = (struct gk_curl_data *)userp;
    size_t realsize = size * nmemb;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);

    if (ptr == NULL)
    {
        /* out of memory! */
        LOGD("%s() realloc failed !!", __func__);
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

void
gk_set_redirect(struct fsm_policy_req *policy_req,
                Gatekeeper__Southbound__V1__GatekeeperFqdnReply *reply_fqdn)
{
    Gatekeeper__Southbound__V1__GatekeeperFqdnRedirectReply *fqdn_redirect;
    Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;
    Gatekeeper__Southbound__V1__GatekeeperAction gk_action;
    struct fqdn_pending_req *fqdn_req;
    static char ipv6_str[INET6_ADDRSTRLEN] = { '\0' };
    char ipv4_str[INET_ADDRSTRLEN]         = { '\0' };
    char ipv4_redirect_s[256];
    char ipv6_redirect_s[256];
    const char *res;

    fqdn_req      = policy_req->fqdn_req;
    header        = reply_fqdn->header;
    gk_action     = header->action;
    fqdn_redirect = reply_fqdn->redirect;

    if (gk_action
        != GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_REDIRECT)
        return;

    res = inet_ntop(
        AF_INET, &(fqdn_redirect->redirect_ipv4), ipv4_str, INET_ADDRSTRLEN);
    if (res != NULL)
    {
        snprintf(ipv4_redirect_s, sizeof(ipv4_redirect_s), "A-%s", ipv4_str);
        STRSCPY(fqdn_req->redirects[0], ipv4_redirect_s);
    }

    res = NULL;
    if (fqdn_redirect->redirect_ipv6.data != NULL)
    {
        res = inet_ntop(AF_INET6,
                        fqdn_redirect->redirect_ipv6.data,
                        ipv6_str,
                        INET6_ADDRSTRLEN);
    }

    if (res != NULL)
    {
        snprintf(ipv6_redirect_s, sizeof(ipv6_redirect_s), "4A-%s", ipv6_str);
        STRSCPY(fqdn_req->redirects[1], ipv6_redirect_s);
    }

    policy_req->reply.redirect = 1;
    policy_req->reply.rd_ttl = 10;

    LOGT("%s(): redirect IPv4 IP: %s, IPv6 IP: %s",
         __func__,
         ipv4_redirect_s,
         ipv6_redirect_s);
}

static int
gk_get_fsm_action(Gatekeeper__Southbound__V1__GatekeeperCommonReply *header)
{
    Gatekeeper__Southbound__V1__GatekeeperAction gk_action;
    int action;

    LOGT("%s(): Received action from gatekeeper service '%d'", __func__, header->action);

    gk_action = header->action;
    switch(gk_action)
    {
        case GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_UNSPECIFIED:
            action = FSM_ACTION_NONE;
            break;

        case GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT:
            action = FSM_ALLOW;
            break;

        case GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_BLOCK:
            action = FSM_BLOCK;
            break;

        case GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_REDIRECT:
            action = FSM_REDIRECT;
            break;

        default:
            action = FSM_ACTION_NONE;
    }

    return action;
}

static void
gk_set_report_info(struct fsm_url_reply *url_reply,
                   Gatekeeper__Southbound__V1__GatekeeperCommonReply *header)
{
    LOGT("%s() received category id: %d, confidence level %d policy '%s'",
         __func__,
         header->category_id,
         header->confidence_level,
         header->policy);

    url_reply->reply_info.gk_info.category_id = header->category_id;
    url_reply->reply_info.gk_info.confidence_level = header->confidence_level;
    if (header->policy == NULL) return;

    /* No action taken if strdup() fails */
    url_reply->reply_info.gk_info.gk_policy = strdup(header->policy);
}

/**
 * @brief unpacks the response received from the gatekeeper server
 *        and sets the policy based on the response
 *
 * @param gk_verdict structure containing gatekeeper server
 *        information, proto buffer and fsm_policy_req
 * @param response unpacked curl response
 * @return None
 */
static bool
gk_set_policy(Gatekeeper__Southbound__V1__GatekeeperReply *response,
              struct fsm_gk_verdict *gk_verdict)
{
    Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_policy_req *policy_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    int req_type;

    policy_req = gk_verdict->policy_req;
    fqdn_req = policy_req->fqdn_req;
    req_info = fqdn_req->req_info;

    url_reply = req_info->reply;
    if (url_reply == NULL) return false;

    req_type = fsm_policy_get_req_type(policy_req);

    header = NULL;
    switch (req_type)
    {
        case FSM_FQDN_REQ:
            if (response->reply_fqdn != NULL)
            {
                header = response->reply_fqdn->header;
                gk_set_redirect(policy_req, response->reply_fqdn);
            }
            else
            {
                LOGD("%s: no fqdn data available", __func__);
            }
            break;

        case FSM_SNI_REQ:
            if (response->reply_https_sni != NULL)
            {
                header = response->reply_https_sni->header;
            }
            else
            {
                LOGD("%s: no sni data available", __func__);
            }
            break;

        case FSM_HOST_REQ:
            if (response->reply_http_host != NULL)
            {
                header = response->reply_http_host->header;
            }
            else
            {
                LOGD("%s: no host data available", __func__);
            }
            break;

        case FSM_URL_REQ:
            if (response->reply_http_url != NULL)
            {
                header = response->reply_http_url->header;
            }
            else
            {
                LOGD("%s: no url data available", __func__);
            }
            break;

        case FSM_APP_REQ:
            if (response->reply_app != NULL)
            {
                header = response->reply_app->header;
            }
            else
            {
                LOGD("%s: no app data available", __func__);
            }
            break;

        case FSM_IPV4_REQ:
            if (response->reply_ipv4 != NULL)
            {
                header = response->reply_ipv4->header;
            }
            else
            {
                LOGD("%s: no ipv4 data available", __func__);
            }
            break;

        case FSM_IPV6_REQ:
            if (response->reply_ipv6 != NULL)
            {
                header = response->reply_ipv6->header;
            }
            else
            {
                LOGD("%s: no ipv6 data available", __func__);
            }
            break;

        case FSM_IPV4_FLOW_REQ:
            if (response->reply_ipv4_tuple != NULL)
            {
                header = response->reply_ipv4_tuple->header;
            }
            else
            {
                LOGD("%s: no ipv4 flow data available", __func__);
            }
            break;

        case FSM_IPV6_FLOW_REQ:
            if (response->reply_ipv6_tuple != NULL)
            {
                header = response->reply_ipv6_tuple->header;
            }
            else
            {
                LOGD("%s: no ipv6 data available", __func__);
            }
            break;

        default:
            LOGD("%s() invalid request type %d", __func__, req_type);
            break;
    }

    if (header == NULL) return false;

    policy_req->reply.action = gk_get_fsm_action(header);
    policy_req->reply.cache_ttl = header->ttl;
    gk_set_report_info(url_reply, header);

    return true;
}

/**
 * @brief unpacks the response received from the gatekeeper server
 *        and sets the policy based on the response
 *
 * @param gk_verdict structure containing gatekeeper server
 *        information, proto buffer and fsm_policy_req
 * @param data data containing curl response
 * @return true on success, false on failure
 */
static bool
gk_process_curl_response(struct gk_curl_data *data, struct fsm_gk_verdict *gk_verdict)
{
    Gatekeeper__Southbound__V1__GatekeeperReply *unpacked_data;
    const uint8_t *buf;
    int ret;

    buf = (const uint8_t *)data->memory;
    unpacked_data = gatekeeper__southbound__v1__gatekeeper_reply__unpack(NULL,
                                                                         data->size,
                                                                         buf);
    if (unpacked_data == NULL)
    {
        LOGD("%s() failed to unpack response", __func__);
        return false;
    }

    ret = gk_set_policy(unpacked_data, gk_verdict);

    gatekeeper__southbound__v1__gatekeeper_reply__free_unpacked(unpacked_data, NULL);

    return ret;
}

/**
 * @brief returns the attribute string value based on the request type.
 *
 * @param req_type attribute request type.
 */
const char *
gk_request_str(int req_type)
{
    switch (req_type)
    {
        case FSM_FQDN_REQ:
            return "fqdn";

        case FSM_SNI_REQ:
            return "https_sni";

        case FSM_HOST_REQ:
            return "http_host";

        case FSM_URL_REQ:
            return "http_url";

        case FSM_APP_REQ:
            return "app";

        case FSM_IPV4_REQ:
            return "ipv4";

        case FSM_IPV6_REQ:
            return "ipv6";

        case FSM_IPV4_FLOW_REQ:
            return "ipv4_tuple";

        case FSM_IPV6_FLOW_REQ:
            return "ipv6_tuple";

        default:
            return "";
    }
}

/**
 * @brief clean up curl handler
 *
 * @param mgr the gate keeper session
 */
void
gk_curl_easy_cleanup(struct fsm_gk_session *fsm_gk_session)
{
    struct gk_curl_easy_info *curl_info;

    curl_info = &fsm_gk_session->ecurl;

    if (curl_info->connection_active == false) return;

    if (curl_info->curl_handle == NULL) return;

    LOGT("%s(): cleaning up curl connection %p ", __func__, curl_info->curl_handle);
    curl_easy_cleanup(curl_info->curl_handle);
    curl_global_cleanup();
    curl_info->connection_active = false;
}

/**
 * @brief initializes the curl handler
 *
 * @param * @param mgr the gate keeper session
 * @return true on success, false on failure
 */
bool
gk_curl_easy_init(struct fsm_gk_session *fsm_gk_session, struct ev_loop *loop)
{
    struct gk_curl_easy_info *curl_info;
    LOGT("%s(): initializing curl_easy", __func__);

    curl_info = &fsm_gk_session->ecurl;

    if (curl_info->connection_active == true) return true;

    curl_global_init(CURL_GLOBAL_ALL);

    curl_info->curl_handle = curl_easy_init();
    if (curl_info->curl_handle == false) goto error;

    curl_easy_setopt(curl_info->curl_handle, CURLOPT_WRITEFUNCTION, gk_curl_callback);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);

    /* max. allowed time (2 secs) to establish the connection */
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_CONNECTTIMEOUT, 2L);

    /* max allowed time (2 secs) to get the data after connection is established */
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_SSL_VERIFYHOST, 1L);

    curl_info->connection_active = true;
    curl_info->connection_time = time(NULL);

    LOGT("%s() initialized curl handle %p ", __func__, curl_info->curl_handle);
    return true;

error:
    LOGD("%s(): failed to initialize curl handler", __func__);
    curl_global_cleanup();
    return false;
}

/**
 * @brief makes a request to gatekeeper server.
 *
 * @param gk_verdict structure containing gatekeeper server
 *        information, proto buffer and fsm_policy_req
 * @param chunk memory to store the curl response
 * @return true on success, false on failure
 */
static int
gk_send_curl_request(struct gk_curl_easy_info *curl_info, struct gk_curl_data *chunk, struct fsm_gk_verdict *gk_verdict)
{
    char errbuf[CURL_ERROR_SIZE];
    char url[1024];
    int req_type;
    CURLcode res;

    errbuf[0]='\0';

    req_type = fsm_policy_get_req_type(gk_verdict->policy_req);

#ifdef CONFIG_GATEKEEPER_ENDPOINT
    /* populate the end point url */
    snprintf(url, sizeof(url), "%s/%s", curl_info->server_url, gk_request_str(req_type));
#else
    strncpy(url, curl_info->server_url, sizeof(url));
#endif

    LOGT("%s(): sending request to %s (req type %d) using handler %p, certs path: %s, ssl cert %s, ssl key %s",
         __func__,
         url,
         req_type,
         curl_info->curl_handle,
         curl_info->ca_path,
         curl_info->ssl_cert,
         curl_info->ssl_key);

    curl_easy_setopt(curl_info->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_POSTFIELDS, gk_verdict->gk_pb->buf);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_POSTFIELDSIZE, (long)gk_verdict->gk_pb->len);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_WRITEFUNCTION, gk_curl_callback);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_CAINFO, curl_info->ca_path);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_SSLCERT, curl_info->ssl_cert);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_SSLKEY, curl_info->ssl_key);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_WRITEDATA, chunk);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_ERRORBUFFER, errbuf);

    res = curl_easy_perform(curl_info->curl_handle);
    if (res != CURLE_OK)
    {
        LOGT("%s(): curl_easy_perform failed: ret code: %d (%s)",
             __func__,
             res,
             errbuf);
    }

    return res;
}

/**
 * @brief checks if category id is set to uncategorized and
 * increment the counter.
 * @param header response header received from gatekeeper service
 * @param fsm_gk_session gatekeeper session
 * @return None
 */
static void
gk_update_uncategorized_count(struct fsm_gk_session *fsm_gk_session,
                              struct fsm_gk_verdict *gk_verdict)
{
    struct fqdn_pending_req *fqdn_req;
    struct fsm_policy_req *policy_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    struct fsm_url_stats *stats;

    policy_req = gk_verdict->policy_req;
    fqdn_req = policy_req->fqdn_req;
    req_info = fqdn_req->req_info;

    url_reply = req_info->reply;
    if (url_reply == NULL) return;

    /* return if the category id is not uncategorized (category_id 15) */
    if (url_reply->reply_info.gk_info.category_id != GK_UNCATEGORIZED_ID)
        return;

    stats = &fsm_gk_session->health_stats;
    stats->uncategorized++;
}

/**
 * @brief increments categorization (service) failure count.
 *
 * @param fsm_gk_session gatekeeper session
 *
 * @return None
 */
static void
gk_update_categorization_count(struct fsm_gk_session *fsm_gk_session)
{
    struct fsm_url_stats *stats;

    stats = &fsm_gk_session->health_stats;
    stats->categorization_failures++;
}

/**
 * @brief sends request to the gatekeeper services
 *        parses and updates the action.
 *
 * @param gk_verdict structure containing gatekeeper server
 *        information, proto buffer and fsm_policy_req
 * @return gatekeeper response code.
 */
int
gk_send_request(struct fsm_session *session,
                struct fsm_gk_session *fsm_gk_session,
                struct fsm_gk_verdict *gk_verdict)
{
    struct gk_curl_easy_info *curl_info;
    struct fsm_policy_req *policy_req;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    struct fsm_url_stats *stats;
    struct gk_curl_data chunk;
    long response_code;
    int gk_response = GK_LOOKUP_SUCCESS;
    CURLcode res;
    bool ret;

    policy_req = gk_verdict->policy_req;
    fqdn_req = policy_req->fqdn_req;
    fqdn_req->provider = session->provider;
    req_info = fqdn_req->req_info;

    url_reply = req_info->reply;
    curl_info = &fsm_gk_session->ecurl;

    if (curl_info->connection_active == false)
    {
        LOGT("%s(): creating new curl handler", __func__);
        gk_curl_easy_init(fsm_gk_session, session->loop);
    }

    /* will be increased as needed by the realloc */
    chunk.memory = malloc(1);
    if (chunk.memory == NULL) return GK_LOOKUP_FAILURE;

    /* no data at this point */
    chunk.size = 0;

    /* update connection time */
    curl_info->connection_time = time(NULL);
    res = gk_send_curl_request(curl_info, &chunk, gk_verdict);

    curl_easy_getinfo(curl_info->curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    url_reply->lookup_status = response_code;
    if (res != CURLE_OK)
    {
        LOGT("%s(): curl request failed!!", __func__);
        url_reply->connection_error = true;
        fqdn_req->to_report = false;
        url_reply->error = res;
        gk_response = GK_CONNECTION_ERROR;
        goto error;
    }

    stats = &fsm_gk_session->health_stats;
    stats->cloud_lookups++;

    LOGT("%s(): %zu bytes retrieved", __func__, chunk.size);
    if (chunk.size == 0)
    {
        gk_response = GK_CONNECTION_ERROR;
        goto error;
    }

    ret = gk_process_curl_response(&chunk, gk_verdict);
    if (ret == false) gk_response = GK_SERVICE_ERROR;

    /* if the curl reponse was successful and reply processing failed
     * treate it as service failures.
     */
    if (gk_response == GK_SERVICE_ERROR) gk_update_categorization_count(fsm_gk_session);

    /* update uncategorized counter (reply with category-id 15) */
    gk_update_uncategorized_count(fsm_gk_session, gk_verdict);

error:
    free(chunk.memory);
    return gk_response;
}
