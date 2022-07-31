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

#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>

#include "const.h"
#include "dns_cache.h"
#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dpi_sni.h"
#include "fsm_policy.h"
#include "gatekeeper_cache.h"
#include "json_mqtt.h"
#include "log.h"
#include "memutil.h"
#include "network_metadata_report.h"
#include "os_regex.h"
#include "sockaddr_storage.h"
#include "util.h"

static const struct dpi_sni_req_type
{
    char *req_str_type;
    int req_type;
} req_map[] =
{
    {
        .req_str_type = "http.host",
        .req_type = FSM_HOST_REQ,
    },
    {
        .req_str_type = "tls.sni",
        .req_type = FSM_SNI_REQ,
    },
    {
        .req_str_type = "http.url",
        .req_type = FSM_URL_REQ,
    },
    {
        .req_str_type = "tag",
        .req_type = FSM_APP_REQ,
    }
};

/* Forward declaration */
static void dpi_sni_process_verdict(struct fsm_policy_req *policy_request,
                                    struct fsm_policy_reply *policy_reply);

/**
 * @brief sets the request type based on the flow attribute
 *
 * @param the ovsdb configuration
 * @return an integer representing the type of service
 */
int
dpi_sni_get_req_type(const char *attr)
{
    const struct dpi_sni_req_type *map;
    size_t nelems;
    size_t i;
    int cmp;

    if (attr == NULL) return FSM_UNKNOWN_REQ_TYPE;

    /* Walk the known types */
    nelems = ARRAY_SIZE(req_map);
    map = req_map;
    for (i = 0; i < nelems; i++)
    {
        cmp = strcmp(map->req_str_type, attr);
        if (!cmp) return map->req_type;
        map++;
    }

    return FSM_UNKNOWN_REQ_TYPE;
}

static int
dpi_sni_init_specific_request(struct fsm_policy_req *policy_request,
                              struct fsm_request_args *request_args,
                              char *attr_value)
{
    struct fqdn_pending_req *pending_req;

    policy_request->url = STRDUP(attr_value);
    if (policy_request->url == NULL) return -1;

    policy_request->req_type = request_args->request_type;

    pending_req = policy_request->fqdn_req;
    STRSCPY(pending_req->req_info->url, attr_value);

    pending_req->numq = 1;

    return 0;
}

struct fsm_policy_req *
dpi_sni_create_request(struct fsm_request_args *request_args, char *attr_value)
{
    struct fsm_policy_req *policy_request;
    int ret;

    if (request_args == NULL || attr_value == NULL) return NULL;

    /* initialize fsm policy request structure */
    policy_request = fsm_policy_initialize_request(request_args);
    if (policy_request == NULL)
    {
        LOGD("%s: fsm policy request initialization failed for dpi sni", __func__);
        return NULL;
    }

    ret = dpi_sni_init_specific_request(policy_request, request_args, attr_value);
    if (ret == -1)
    {
        LOGT("%s: failed to initialize dpi sni related request", __func__);
        goto free_policy_req;
    }

    return policy_request;

free_policy_req:
    fsm_policy_free_request(policy_request);
    return NULL;
}

static int
dpi_sni_init_specific_reply(struct fsm_request_args *request_args,
                            struct fsm_policy_reply *policy_reply)
{
    struct fsm_policy_client *policy_client;
    struct fsm_session *session;

    session = request_args->session;
    policy_client = &session->policy_client;

    policy_reply->provider = session->service->name;
    policy_reply->policy_table = policy_client->table;
    policy_reply->send_report = session->ops.send_report;
    policy_reply->categories_check = session->provider_ops->categories_check;
    policy_reply->risk_level_check = session->provider_ops->risk_level_check;
    policy_reply->gatekeeper_req = session->provider_ops->gatekeeper_req;
    policy_reply->req_type = request_args->request_type;
    policy_reply->policy_response = dpi_sni_process_verdict;

    return 0;
}

struct fsm_policy_reply *
dpi_sni_create_reply(struct fsm_request_args *request_args)
{
    struct fsm_policy_reply *policy_reply;
    struct fsm_session *session;
    int ret;

    if (request_args == NULL) return NULL;

    session = request_args->session;
    if (session == NULL) return NULL;

    policy_reply = fsm_policy_initialize_reply(session);
    if (policy_reply == NULL)
    {
        LOGD("%s: failed to initialize policy reply for dpi sni", __func__);
        return NULL;
    }

    ret = dpi_sni_init_specific_reply(request_args, policy_reply);
    if (ret == -1)
    {
        LOGT("%s: failed to initialize dpi sni related request", __func__);
        goto free_policy_reply;
    }

    return policy_reply;

free_policy_reply:
    fsm_policy_free_reply(policy_reply);
    return NULL;
}

static void
dpi_sni_free_policy_request(struct fsm_policy_req *policy_request)
{
    FREE(policy_request->url);
    fsm_policy_free_request(policy_request);
}

static void
dpi_sni_free_memory(struct fsm_policy_req *policy_request,
                    struct fsm_policy_reply *policy_reply)
{
    dpi_sni_free_policy_request(policy_request);
    fsm_policy_free_reply(policy_reply);
}

static int
dpi_sni_process_request(struct fsm_policy_req *policy_request,
                        struct fsm_policy_reply *policy_reply)
{
    LOGT("%s: processing dpi sni request", __func__);

    return fsm_apply_policies(policy_request, policy_reply);
}

static void
dpi_sni_send_report(struct fsm_policy_req *policy_request,
                    struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *pending_req;
    struct fsm_session *session;
    char *report;

    if (policy_reply->to_report != true) return;

    pending_req = policy_request->fqdn_req;
    session = pending_req->fsm_context;

    report = jencode_url_report(session, pending_req, policy_reply);

    session->ops.send_report(session, report);
}

static void
dpi_sni_process_report(struct fsm_policy_req *policy_request,
                       struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *pending_req;

    /* Do not send the report for the redirected IP(policy page) requests */
    if (policy_request->req_type == FSM_FQDN_REQ) return;

    pending_req = policy_request->fqdn_req;

    /*
     * overwrite the redirect action to block, as established
     * flows cannot be redirected.  This is required as the
     * GK could have updated the FQDN cache, and if the request
     * if made for attribute tls.sni, gk returns from FQDN cache
     */
    if (policy_reply->action == FSM_REDIRECT)
    {
        policy_reply->action = FSM_BLOCK;
    }

    pending_req->rd_ttl = policy_reply->rd_ttl;

    policy_reply->to_report = true;
    /* Process reporting */
    if (policy_reply->log == FSM_REPORT_NONE)
    {
        policy_reply->to_report = false;
    }

    if (policy_reply->log == FSM_REPORT_BLOCKED &&
        policy_reply->action != FSM_BLOCK)
    {
        policy_reply->to_report = false;
    }

    /* Overwrite logging and policy if categorization failed */
    if (policy_reply->categorized == FSM_FQDN_CAT_FAILED)
    {
        policy_reply->action = FSM_ALLOW;
        policy_reply->to_report = true;
    }

    LOGT("%s: processing report for dpi sni", __func__);

    dpi_sni_send_report(policy_request, policy_reply);
}

static void
dpi_sni_process_verdict(struct fsm_policy_req *policy_request,
                        struct fsm_policy_reply *policy_reply)
{
    const char *action_str;

    action_str = fsm_policy_get_action_str(policy_reply->action);
    LOGT("%s: processing dpi sni verdict with action '%s'", __func__, action_str);

    dpi_sni_process_report(policy_request, policy_reply);
}

static void
dpi_sni_set_flow_marker(struct fsm_request_args *request_args,
                        struct fsm_policy_reply *policy_reply)
{
    struct net_md_stats_accumulator *acc;

    acc = request_args->acc;
    if (acc == NULL) return;

    acc->flow_marker = policy_reply->flow_marker;
    LOGT("%s(): set flow_marker to %d", __func__, acc->flow_marker);
}

/**
 * @brief request an action from the policy engine
 *
 * @param request_args request parameter values
 * @param attr_value the attribute flow value
 * @return the action to take
 */
int
dpi_sni_policy_req(struct fsm_request_args *request_args, char *attr_value)
{
    struct fsm_policy_reply *policy_reply;
    struct fsm_policy_req *policy_request;
    int action;

    LOGD("%s: attribute: %d, value %s", __func__, request_args->request_type, attr_value);

    policy_request = dpi_sni_create_request(request_args, attr_value);
    if (policy_request == NULL)
    {
        LOGD("%s: failed to create dpi sni policy request", __func__);
        goto error;
    }

    policy_reply = dpi_sni_create_reply(request_args);
    if (policy_reply == NULL)
    {
        LOGD("%s: failed to initialize dpi sni reply", __func__);
        goto clean_policy_req;
    }

    LOGT("%s: allocated policy_request == %p, policy_reply == %p",
         __func__,
         policy_request,
         policy_reply);

    /* process the input request */
    action = dpi_sni_process_request(policy_request, policy_reply);
    dpi_sni_set_flow_marker(request_args, policy_reply);

    action = (action == FSM_BLOCK ? FSM_DPI_DROP : FSM_DPI_PASSTHRU);

    /* Cleanup */
    dpi_sni_free_memory(policy_request, policy_reply);

    return action;

clean_policy_req:
    fsm_policy_free_request(policy_request);
error:
    return FSM_DPI_PASSTHRU;
}

bool
dpi_sni_fetch_fqdn_from_url_attr(char *attribute_name, char *fqdn)
{
    /*
     * http.url of the following format:
     *
     * http://host/path
     *
     * "host" will be extracted from the above format as a FQDN.
     * Note : No FQDN sanitation is performed
     */
    char *pattern = "http://";
    char *pattern_start;
    char *pattern_end;
    int pattern_len = 0;
    char *found;

    if (attribute_name == NULL) return false;

    found = strstr(attribute_name, pattern);

    if (found == NULL) return false;

    pattern_start = found + strlen(pattern);

    pattern_end = strchr(pattern_start, '/');

    if (pattern_end == NULL)
    {
        strcpy(fqdn, pattern_start);
        return true;
    }

    pattern_len = pattern_end - pattern_start;

    strncpy(fqdn, pattern_start, pattern_len);
    fqdn[pattern_len + 1] = '\0';

    return true;
}

bool
dpi_sni_is_flow_redirected(struct net_md_flow_info *info,
                           struct fsm_policy_reply *policy_reply)
{
    char remote_ip_str[128] = { 0 };
    char *ipv4_addr;
    char *ipv6_addr;
    int ret;
    int af;

    af = info->ip_version == 4 ? AF_INET : AF_INET6;
    inet_ntop(af, info->remote_ip, remote_ip_str, sizeof(remote_ip_str));

    if (af == AF_INET)
    {
        ipv4_addr = fsm_dns_check_redirect(policy_reply->redirects[0],
                                           IPv4_REDIRECT);
        if (ipv4_addr == NULL)
        {
            ipv4_addr = fsm_dns_check_redirect(policy_reply->redirects[1],
                                               IPv4_REDIRECT);
        }
        if (ipv4_addr != NULL)
        {
            ret = strcmp(ipv4_addr, remote_ip_str);
            if (!ret) return true;
        }
    }
    else
    {
        ipv6_addr = fsm_dns_check_redirect(policy_reply->redirects[0],
                                           IPv6_REDIRECT);
        if (ipv6_addr == NULL)
        {
            ipv6_addr = fsm_dns_check_redirect(policy_reply->redirects[1],
                                               IPv6_REDIRECT);
        }
        if (ipv6_addr != NULL)
        {
            ret = strcmp(ipv6_addr, remote_ip_str);
            if (!ret) return true;
        }
    }

    return false;
}


bool dpi_sni_is_redirected_attr(struct fsm_dpi_sni_redirect_flow_request *param)
{
    struct fsm_policy_reply *policy_reply;
    struct fsm_policy_req *policy_request;
    struct net_md_stats_accumulator *acc;
    struct fsm_request_args req_args;
    struct net_md_flow_info *info;
    struct fsm_session *session;
    char fqdn_value[C_FQDN_LEN];
    char *attr_value;
    int request_type;
    bool  redirect;
    bool rc;

    redirect = false;
    info = param->info;
    attr_value = param->attribute_value;
    request_type = param->req_type;
    session = param->session;
    acc = param->acc;

    if (info == NULL || info->local_mac == NULL || info->remote_ip == NULL)
    {
        return redirect;
    }

    MEMZERO(fqdn_value);
    switch (request_type)
    {
        case FSM_HOST_REQ:
        {
            STRSCPY(fqdn_value, attr_value);
            break;
        }

        case FSM_URL_REQ:
        {
            rc = dpi_sni_fetch_fqdn_from_url_attr(attr_value, fqdn_value);
            if (rc == false) return redirect;
            break;
        }

        case FSM_APP_REQ:
        case FSM_SNI_REQ:
        default:
            return redirect;
    }

    MEMZERO(req_args);
    req_args.session = session;
    req_args.device_id = info->local_mac;
    req_args.acc = acc;
    req_args.request_type = FSM_FQDN_REQ;

    policy_request = dpi_sni_create_request(&req_args, fqdn_value);
    if (policy_request == NULL)
    {
        LOGD("%s: failed to create dpi sni policy request", __func__);
        return redirect;
    }

    policy_reply = dpi_sni_create_reply(&req_args);
    if (policy_reply == NULL)
    {
        LOGD("%s: failed to initialize dpi sni reply", __func__);
        goto clean_policy_req;
    }

    LOGT("%s: allocated policy_request == %p, policy_reply == %p",
         __func__,
         policy_request,
         policy_reply);

    fsm_apply_policies(policy_request, policy_reply);

    if (policy_reply->redirect)
    {
        redirect = dpi_sni_is_flow_redirected(info, policy_reply);
    }

    /* Cleanup */
    dpi_sni_free_memory(policy_request, policy_reply);

    return redirect;

clean_policy_req:
    dpi_sni_free_policy_request(policy_request);

    return redirect;
}

bool
dpi_sni_is_redirected_flow(struct net_md_flow_info *info)
{
    struct gk_attr_cache_interface entry;
    struct ip2action_req lkp_req;
    struct sockaddr_storage ip;
    bool dns_cache_disabled;
    char buf[128] = { 0 };
    const char *res;
    bool ret;
    int af;
    int rc;

    if (info == NULL || info->local_mac == NULL || info->remote_ip == NULL)
    {
        return false;
    }

    dns_cache_disabled = is_dns_cache_disabled();
    if (dns_cache_disabled)
    {
        /* look up the gatekeeper cache */
        MEMZERO(entry);

        entry.device_mac = info->local_mac;
        af = info->ip_version == 4 ? AF_INET : AF_INET6;
        sockaddr_storage_populate(af, info->remote_ip, &ip);
        entry.ip_addr = &ip;
        entry.attribute_type = (info->ip_version == 4 ?
                                GK_CACHE_REQ_TYPE_IPV4 :
                                GK_CACHE_REQ_TYPE_IPV6);
        res = inet_ntop(af, info->remote_ip, buf, sizeof(buf));
        if (res == NULL)
        {
            LOGE("%s: inet_ntop failed: %s", __func__, strerror(errno));
            return false;
        }
        entry.attr_name = buf;
        entry.direction = info->direction;
        ret  = gkc_lookup_attribute_entry(&entry, true);
        if (ret == true && entry.redirect_flag == false)
        {
            ret = false;
            LOGD("%s: redirect entry is not found in gatekeeper cache", __func__);
        }

        return ret;
    }

    /* look up the dns cache */
    MEMZERO(lkp_req);
    lkp_req.device_mac = info->local_mac;
    af = info->ip_version == 4 ? AF_INET : AF_INET6;

    sockaddr_storage_populate(af, info->remote_ip, &ip);
    lkp_req.ip_addr = &ip;
    lkp_req.direction = info->direction;

    rc = dns_cache_ip2action_lookup(&lkp_req);
    LOGD("%s: cache lookup returned %d, redirect flag: %d",
         __func__,
         rc,
         lkp_req.redirect_flag);
    if (rc == false) return false;

    if (lkp_req.service_id == IP2ACTION_GK_SVC)
    {
        FREE(lkp_req.cache_gk.gk_policy);
    }

    if (lkp_req.redirect_flag == false) return false;

    return true;
}


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_dpi_sni_session *
fsm_dpi_sni_lookup_session(struct fsm_session *session)
{
    struct fsm_dpi_sni_session *u_session;
    struct fsm_dpi_sni_cache *mgr;
    ds_tree_t *sessions;

    mgr = fsm_dpi_sni_get_mgr();
    sessions = &mgr->fsm_sessions;

    u_session = ds_tree_find(sessions, session);
    if (u_session != NULL) return u_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    u_session = CALLOC(1, sizeof(*u_session));
    if (u_session == NULL) return NULL;

    ds_tree_insert(sessions, u_session, session);

    return u_session;
}

/**
 * @brief Frees a fsm url session
 *
 * @param u_session the fsm url session to free
 */
void
fsm_dpi_sni_free_session(struct fsm_dpi_sni_session *u_session)
{
    FREE(u_session);
}

/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the fsm session session to delete
 */
void
fsm_dpi_sni_delete_session(struct fsm_session *session)
{
    struct fsm_dpi_sni_session *u_session;
    struct fsm_dpi_sni_cache *mgr;
    ds_tree_t *sessions;

    mgr = fsm_dpi_sni_get_mgr();
    sessions = &mgr->fsm_sessions;

    u_session = ds_tree_find(sessions, session);
    if (u_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, u_session);
    fsm_dpi_sni_free_session(u_session);
}
