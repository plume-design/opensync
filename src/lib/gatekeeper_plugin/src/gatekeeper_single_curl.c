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
#include <fcntl.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "gatekeeper_single_curl.h"
#include "gatekeeper.pb-c.h"
#include "gatekeeper.h"
#include "fsm_policy.h"
#include "memutil.h"
#include "kconfig.h"
#include "log.h"
#include "util.h"

#define GK_CNAME_REDIRECT_TTL (1*60*60)

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

    char *ptr = REALLOC(mem->memory, mem->size + realsize + 1);

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

/**
 * @brief update policy_reply redirect ipv6 address
 *
 * @param ipv4_address to be updated in policy_reply
 * @param policy_reply structure
 * @return None
 */
void
gk_update_policy_redirect_ipv6(char *ipv6_address,
                               struct fsm_policy_reply *policy_reply)
{
    char ipv6_redirect_s[256];

    snprintf(ipv6_redirect_s, sizeof(ipv6_redirect_s), "4A-%s", ipv6_address);
    STRSCPY(policy_reply->redirects[1], ipv6_redirect_s);
    policy_reply->redirect = true;
}

/**
 * @brief update policy_reply redirect ipv4 address
 *
 * @param ipv4_address to be updated in policy_reply
 * @param policy_reply structure
 * @return None
 */
void
gk_update_policy_redirect_ipv4(char *ipv4_address,
                               struct fsm_policy_reply *policy_reply)
{
    char ipv4_redirect_s[256];

    snprintf(ipv4_redirect_s, sizeof(ipv4_redirect_s), "A-%s", ipv4_address);
    STRSCPY(policy_reply->redirects[0], ipv4_redirect_s);
    policy_reply->redirect = true;
}

/**
 * @brief resolves the redirect_cname from gatekeeper server
 *        to ip addresses and updates the policy redirect ips
 *        also sets redirect ttl to 1 hour
 *
 * @param redirect_cname containing host name
 * @param policy_reply structure
 * @return None
 */
bool
gk_force_redirect_cname(char * redirect_cname,
                        struct fsm_policy_reply *policy_reply)
{
    struct fsm_gk_mgr *mgr;

    mgr = gatekeeper_get_mgr();
    if (!mgr->initialized) return false;

    if (mgr->getaddrinfo == NULL) return false;

    struct addrinfo *result;
    struct addrinfo hints;
    struct addrinfo *rp;
    char addr_str[256];
    const char *res;
    bool status;
    void *addr;
    int ret;

    status = true;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    result = NULL;
    ret = mgr->getaddrinfo(redirect_cname, NULL, &hints, &result);
    if (ret != 0)
    {
        LOGE("Unable to resolve : %s  [%s]\n", redirect_cname, strerror(errno));
        status = false;
        goto error;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next)
    {

        if (rp->ai_family == AF_INET)
        {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
            addr = &(ipv4->sin_addr);
        }
        else
        {
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
            addr = &(ipv6->sin6_addr);
        }
        res = inet_ntop(rp->ai_family, addr, addr_str, sizeof(addr_str));
        if (res != NULL)
        {
            if (rp->ai_family == AF_INET)
            {
                gk_update_policy_redirect_ipv4(addr_str, policy_reply);
            }
            else
            {
                gk_update_policy_redirect_ipv6(addr_str, policy_reply);
            }
        }
    }

    policy_reply->redirect = true;
    policy_reply->rd_ttl = GK_CNAME_REDIRECT_TTL;
    snprintf(policy_reply->redirect_cname, sizeof(policy_reply->redirect_cname),
             "C-%s", redirect_cname);
    LOGT("%s: cname: %s redirect IPv4 IP: %s, IPv6 IP: %s",
         __func__,
         policy_reply->redirect_cname,
         policy_reply->redirects[0],
         policy_reply->redirects[1]);

error:
   if (result) freeaddrinfo(result);

   return status;
}

void
gk_set_redirect(struct fsm_gk_verdict *gk_verdict,
                Gatekeeper__Southbound__V1__GatekeeperFqdnReply *reply_fqdn)
{
    Gatekeeper__Southbound__V1__GatekeeperFqdnRedirectReply *fqdn_redirect;
    Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;
    Gatekeeper__Southbound__V1__GatekeeperAction gk_action;
    char ipv6_str[INET6_ADDRSTRLEN] = { '\0' };
    char ipv4_str[INET_ADDRSTRLEN]  = { '\0' };
    struct fsm_policy_reply *policy_reply;
    struct fsm_gk_session *gk_session;
    struct gk_cname_offline *offline;
    bool cname_redirect;
    const char *res;
    bool backoff;
    bool status;
    time_t now;

    header        = reply_fqdn->header;
    gk_action     = header->action;
    fqdn_redirect = reply_fqdn->redirect;
    policy_reply  = gk_verdict->policy_reply;
    gk_session    = gk_verdict->gk_session_context;

    policy_reply->redirect = false;

    if (gk_action
        != GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_REDIRECT)
        return;

    policy_reply->rd_ttl = 10;

    cname_redirect = (fqdn_redirect->redirect_cname != NULL);
    if (cname_redirect) cname_redirect &= (strlen(fqdn_redirect->redirect_cname) != 0);
    if (cname_redirect)
    {
        /* cname backoff timer */
        offline = &gk_session->cname_offline;
        if (offline->cname_offline)
        {
            now = time(NULL);
            backoff = ((now - offline->offline_ts) < offline->check_offline);

            if (backoff) return;
            offline->cname_offline = false;
        }

        status = gk_force_redirect_cname(fqdn_redirect->redirect_cname, policy_reply);
        if (!status)
        {
            LOGT("%s : triggering backoff timer for cname = %s", __func__,
                 fqdn_redirect->redirect_cname);
            offline->cname_offline = true;
            offline->offline_ts = time(NULL);
            offline->cname_resolve_failures++;
        }
        return;
    }

    res = inet_ntop(AF_INET, &(fqdn_redirect->redirect_ipv4), ipv4_str, INET_ADDRSTRLEN);
    if (res != NULL)
    {
        gk_update_policy_redirect_ipv4(ipv4_str, policy_reply);
    }

    res = NULL;
    if ((fqdn_redirect->redirect_ipv6.data != NULL) &&
        (fqdn_redirect->redirect_ipv6.len != 0))

    {
        res = inet_ntop(AF_INET6,
                        fqdn_redirect->redirect_ipv6.data,
                        ipv6_str,
                        INET6_ADDRSTRLEN);

        if (res != NULL)
        {
            gk_update_policy_redirect_ipv6(ipv6_str, policy_reply);
        }
    }

    LOGT("%s(): redirect IPv4 IP: %s, IPv6 IP: %s",
         __func__,
         policy_reply->redirects[0],
         policy_reply->redirects[1]);
}

static int
gk_get_fsm_action(Gatekeeper__Southbound__V1__GatekeeperCommonReply *header)
{
    Gatekeeper__Southbound__V1__GatekeeperAction gk_action;
    int action;

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

    LOGT("%s(): Received action from gatekeeper service '%s'", __func__,
         fsm_policy_get_action_str(action));

    return action;
}

static void
gk_set_report_info(struct fsm_url_reply *url_reply,
                   Gatekeeper__Southbound__V1__GatekeeperCommonReply *header)
{
    char *policy_name;

    if (header->policy == NULL)
    {
        policy_name = STRDUP("NULL_PTR");
    }
    else if (strcmp("NULL", header->policy) == 0 && url_reply->reply_info.gk_info.gk_policy != NULL)
    {
        policy_name = STRDUP(url_reply->reply_info.gk_info.gk_policy);
    }
    else
    {
        policy_name = STRDUP(header->policy);
    }

    LOGT("%s() received category id: %d, confidence level %d policy '%s'",
         __func__,
         header->category_id,
         header->confidence_level,
         policy_name);


    url_reply->reply_info.gk_info.category_id = header->category_id;
    url_reply->reply_info.gk_info.confidence_level = header->confidence_level;

    FREE(url_reply->reply_info.gk_info.gk_policy);
    url_reply->reply_info.gk_info.gk_policy = policy_name;
}

static void
gk_set_flow_mark(struct fsm_policy_req *policy_req, int action, uint32_t mark)
{
    struct net_md_stats_accumulator *acc;

    acc = policy_req->acc;
    if (acc == NULL) return;

    acc->flow_marker = (action == FSM_BLOCK) ? FSM_DPI_DROP : FSM_DPI_PASSTHRU;
    LOGT("%s(): flow_marker set to %d (%s)", __func__, acc->flow_marker,
         fsm_policy_get_action_str(action));

    /* no need to check for mark value if verdict is drop */
    if (action == FSM_BLOCK) return;

    if (mark != 0) acc->flow_marker = mark;

    LOGT("%s(): updated flow_marker to %d", __func__, acc->flow_marker);
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
bool
gk_set_policy(Gatekeeper__Southbound__V1__GatekeeperReply *response,
              struct fsm_gk_verdict *gk_verdict)
{
    Gatekeeper__Southbound__V1__GatekeeperCommonReply *header;
    struct fsm_policy_reply *policy_reply;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_policy_req *policy_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    int req_type;

    policy_reply = gk_verdict->policy_reply;
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
                gk_set_redirect(gk_verdict, response->reply_fqdn);
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

    policy_reply->action = gk_get_fsm_action(header);
    gk_set_flow_mark(policy_req, policy_reply->action, header->flow_marker);
    policy_reply->cache_ttl = header->ttl;

    if (policy_reply->rule_name == NULL)
    {
        if (policy_req->rule_name)
        {
            LOGD("%s: Setting policy_reply to %s", __func__, policy_req->rule_name);
            policy_reply->rule_name = STRDUP(policy_req->rule_name);
        }
        else
        {
            LOGD("%s: policy request rule name is NULL", __func__);
        }
    }

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
gk_process_curl_response(struct gk_curl_data *data,
                         struct fsm_gk_verdict *gk_verdict,
                         struct fsm_policy_reply *policy_reply)
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
 * @brief clean up curl handler
 *
 * @param mgr the gate keeper session
 */
void
gk_curl_easy_cleanup(struct fsm_gk_session *fsm_gk_session)
{
    struct gk_curl_easy_info *curl_info;

    curl_info = &fsm_gk_session->ecurl;

    if (curl_info->ecurl_connection_active == false) return;

    if (curl_info->curl_handle == NULL) return;

    LOGT("%s(): cleaning up curl connection %p ", __func__, curl_info->curl_handle);
    curl_easy_cleanup(curl_info->curl_handle);
    curl_global_cleanup();
    curl_info->ecurl_connection_active = false;
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

    if (curl_info->ecurl_connection_active == true) return true;

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

    curl_info->ecurl_connection_active = true;
    curl_info->ecurl_connection_time = time(NULL);

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
gk_send_curl_request(struct fsm_gk_session *fsm_gk_session,
                     struct gk_curl_data *chunk,
                     struct fsm_gk_verdict *gk_verdict)
{
    struct gk_server_info *server_info;
    struct gk_curl_easy_info *curl_info;
    char errbuf[CURL_ERROR_SIZE];
    char url[1024];
    int req_type;
    CURLcode res;

    errbuf[0]='\0';
    server_info = &fsm_gk_session->gk_server_info;
    curl_info = &fsm_gk_session->ecurl;

    req_type = fsm_policy_get_req_type(gk_verdict->policy_req);

    if (kconfig_enabled(CONFIG_GATEKEEPER_ENDPOINT))
    {
        /* populate the end point url */
        snprintf(url, sizeof(url), "%s/%d", server_info->server_url, req_type);
    }
    else
    {
        strncpy(url, server_info->server_url, sizeof(url));
    }

    LOGT("%s(): sending request to %s (req type %d) using handler %p, certs path: %s, ssl cert %s, ssl key %s",
         __func__,
         url,
         req_type,
         curl_info->curl_handle,
         server_info->cert_path != NULL ? server_info->cert_path : server_info->ca_path,
         server_info->cert_path != NULL ? "None" : server_info->ssl_cert,
         server_info->cert_path != NULL ? "None" : server_info->ssl_key);

    curl_easy_setopt(curl_info->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_POSTFIELDS, gk_verdict->gk_pb->buf);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_POSTFIELDSIZE, (long)gk_verdict->gk_pb->len);
    curl_easy_setopt(curl_info->curl_handle, CURLOPT_WRITEFUNCTION, gk_curl_callback);
    if (server_info->cert_path != NULL)
    {
        curl_easy_setopt(curl_info->curl_handle, CURLOPT_CAINFO, server_info->cert_path);
    }
    else
    {
        curl_easy_setopt(curl_info->curl_handle, CURLOPT_CAINFO, server_info->ca_path);
        curl_easy_setopt(curl_info->curl_handle, CURLOPT_SSLCERT, server_info->ssl_cert);
        curl_easy_setopt(curl_info->curl_handle, CURLOPT_SSLKEY, server_info->ssl_key);
    }
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
void
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
void
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
gk_send_ecurl_request(struct fsm_session *session,
                      struct fsm_gk_session *fsm_gk_session,
                      struct fsm_gk_verdict *gk_verdict,
                      struct fsm_policy_reply *policy_reply)
{
    struct gk_curl_easy_info *curl_info;
    struct fsm_policy_req *policy_req;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    struct fsm_url_stats *stats;
    struct gk_curl_data chunk;
    uint32_t category_id;
    long response_code;
    int gk_response;
    CURLcode res;
    bool ret;

    gk_response = GK_LOOKUP_SUCCESS;
    policy_req = gk_verdict->policy_req;
    fqdn_req = policy_req->fqdn_req;
    policy_reply->provider = session->provider;
    req_info = fqdn_req->req_info;

    url_reply = req_info->reply;
    curl_info = &fsm_gk_session->ecurl;

    if (curl_info->ecurl_connection_active == false)
    {
        LOGT("%s(): creating new curl handler", __func__);
        gk_curl_easy_init(fsm_gk_session, session->loop);
    }

    /* will be increased as needed by the realloc */
    chunk.memory = (char *)MALLOC(1);
    if (chunk.memory == NULL) return GK_LOOKUP_FAILURE;

    /* no data at this point */
    chunk.size = 0;

    /* update connection time */
    curl_info->ecurl_connection_time = time(NULL);
    res = gk_send_curl_request(fsm_gk_session, &chunk, gk_verdict);
    stats = &fsm_gk_session->health_stats;
    stats->cloud_lookups++;

    curl_easy_getinfo(curl_info->curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    url_reply->lookup_status = response_code;
    if (res != CURLE_OK)
    {
        LOGT("%s(): curl request failed!!", __func__);
        url_reply->connection_error = true;
        policy_reply->to_report = false;
        url_reply->error = res;
        gk_response = GK_CONNECTION_ERROR;
        goto error;
    }

    LOGT("%s(): %zu bytes retrieved", __func__, chunk.size);
    if (chunk.size == 0)
    {
        gk_response = GK_CONNECTION_ERROR;
        goto error;
    }

    ret = gk_process_curl_response(&chunk, gk_verdict, policy_reply);
    category_id = url_reply->reply_info.gk_info.category_id;

    /* Even though curl response was successful,
     * if reply processing failed or
     * if category id received from gatekeeper is zero (0)
     * treat each one as gatekeeper service failure.
     */
    if (category_id == 0 || ret == false) gk_response = GK_SERVICE_ERROR;
    if (gk_response == GK_SERVICE_ERROR) gk_update_categorization_count(fsm_gk_session);

    /* update uncategorized counter (reply with category-id 15) */
    gk_update_uncategorized_count(fsm_gk_session, gk_verdict);

error:
    FREE(chunk.memory);
    return gk_response;
}
