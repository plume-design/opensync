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
#include <inttypes.h>
#include <stdlib.h>
#include <stddef.h>
#include <time.h>

#include "assert.h"
#include "const.h"
#include "ds_tree.h"
#include "json_mqtt.h"
#include "memutil.h"
#include "log.h"
#include "fsm_dpi_sni.h"
#include "network_metadata_report.h"
#include "policy_tags.h"
#include "dns_cache.h"
#include "gatekeeper_cache.h"
#include "fsm_dpi_utils.h"

static struct fsm_dpi_sni_cache
cache_mgr =
{
    .initialized = false,
};


struct fsm_dpi_sni_cache *
fsm_dpi_sni_get_mgr(void)
{
    return &cache_mgr;
}

static const struct fsm_req_type
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

/* Forward definition */
static void
fsm_dpi_sni_process_verdict(struct fsm_policy_req *policy_request,
                            struct fsm_policy_reply *policy_reply);

/**
 * @brief sets the request type based on the flow attribute
 *
 * @param the ovsdb configuration
 * @return an integer representing the type of service
 */
int
fsm_req_type(char *attr)
{
    const struct fsm_req_type *map;
    size_t nelems;
    size_t i;
    int cmp;

    if (attr == NULL) return FSM_UNKNOWN_REQ_TYPE;

    /* Walk the known types */
    nelems = (sizeof(req_map) / sizeof(req_map[0]));
    map = req_map;
    for (i = 0; i < nelems; i++)
    {
        cmp = strcmp(map->req_str_type, attr);
        if (!cmp) return map->req_type;

        map++;
    }

    return FSM_UNKNOWN_REQ_TYPE;
}

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
int
fsm_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm
 *
 * @param session pointer provided by fsm
 */
int
dpi_sni_plugin_init(struct fsm_session *session)
{
    struct fsm_dpi_plugin_client_ops *client_ops;
    struct fsm_dpi_sni_session *fsm_dpi_sni_session;
    struct fsm_dpi_sni_cache *mgr;

    if (session == NULL || session->p_ops == NULL) return -1;

    mgr = fsm_dpi_sni_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(&mgr->fsm_sessions, fsm_session_cmp,
                     struct fsm_dpi_sni_session, session_node);
        mgr->initialized = true;
    }

    /* Look up the fsm_dpi_sni session */
    fsm_dpi_sni_session = fsm_dpi_sni_lookup_session(session);
    if (fsm_dpi_sni_session == NULL)
    {
        LOGE("%s: could not allocate the fsm url session %s", __func__,
             session->name);

        return -1;
    }

    /* Bail if the session is already initialized */
    if (fsm_dpi_sni_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.update = fsm_dpi_sni_plugin_update;
    session->ops.periodic = fsm_dpi_sni_plugin_periodic;
    session->ops.exit = fsm_dpi_sni_plugin_exit;
    session->handler_ctxt = fsm_dpi_sni_session;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_sni_process_attr;

    /* Wrap up the session initialization */
    fsm_dpi_sni_session->session = session;
    fsm_dpi_sni_plugin_update(session);

    fsm_dpi_sni_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    return 0;
}

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
fsm_dpi_sni_plugin_exit(struct fsm_session *session)
{
    struct fsm_dpi_sni_cache *mgr;

    mgr = fsm_dpi_sni_get_mgr();
    if (!mgr->initialized) return;

    fsm_dpi_sni_delete_session(session);
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

/**
 * @brief make the TTL configurable
 */
time_t FSM_DPI_SNI_CHECK_TTL = (2 * 60);

void
fsm_dpi_sni_set_ttl(time_t t)
{
    FSM_DPI_SNI_CHECK_TTL = t;
}

/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm url session to uprocess
 */
void
fsm_dpi_sni_plugin_periodic(struct fsm_session *session)
{
    struct fsm_dpi_sni_session *u_session;
    time_t cmp_clean;
    time_t now;

    u_session = session->handler_ctxt;
    if (u_session == NULL) return;

    now = time(NULL);
    cmp_clean = now - u_session->timestamp;
    if (cmp_clean < FSM_DPI_SNI_CHECK_TTL) return;

    u_session->timestamp = now;
}

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_sni_plugin_update(struct fsm_session *session)
{
    struct fsm_dpi_sni_session *u_session;

    u_session = session->handler_ctxt;
    if (u_session == NULL) return;

    u_session->included_devices = session->ops.get_config(session, "included_devices");
    u_session->excluded_devices = session->ops.get_config(session, "excluded_devices");
}

static void
fsm_dpi_sni_send_report(struct fsm_policy_req *policy_request,
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

static int
init_dpi_sni_specific_request(struct fsm_policy_req *policy_request,
                              struct fsm_request_args *request_args,
                              char *attr_value)
{
    struct fqdn_pending_req *pending_req;

    pending_req = policy_request->fqdn_req;
    policy_request->url = STRDUP(attr_value);
    policy_request->req_type = request_args->request_type;

    STRSCPY(pending_req->req_info->url, attr_value);
    pending_req->numq = 1;

    return 0;
}

struct fsm_policy_req *
fsm_dpi_sni_create_request(struct fsm_request_args *request_args, char *attr_value)
{
    struct fsm_policy_req *policy_request;
    int ret;

    if (request_args == NULL || attr_value == NULL) return NULL;

    /* initialize fsm policy request structure */
    policy_request = fsm_policy_initialize_request(request_args);
    if (policy_request == NULL)
    {
        LOGD("%s(): fsm policy request initialization failed for dpi sni", __func__);
        return NULL;
    }

    ret = init_dpi_sni_specific_request(policy_request, request_args, attr_value);
    if (ret == -1)
    {
        LOGT("%s(): failed to initialize dpi sni related request", __func__);
        goto free_policy_req;
    }
    return policy_request;

free_policy_req:
    fsm_policy_free_request(policy_request);
    return NULL;
}

static void
init_dpi_sni_specific_reply(struct fsm_request_args *request_args,
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
    policy_reply->policy_response = fsm_dpi_sni_process_verdict;
}

struct fsm_policy_reply *
fsm_dpi_sni_create_reply(struct fsm_request_args *request_args)
{
    struct fsm_policy_reply *policy_reply;
    struct fsm_session *session;

    if (request_args == NULL) return NULL;

    session = request_args->session;
    if (session == NULL) return NULL;

    policy_reply = fsm_policy_initialize_reply(session);
    if (policy_reply == NULL)
    {
        LOGD("%s(): failed to initialize policy reply for dpi sni", __func__);
        return NULL;
    }

    init_dpi_sni_specific_reply(request_args, policy_reply);

    return policy_reply;
}

static int
fsm_dpi_sni_process_request(struct fsm_policy_req *policy_request,
                            struct fsm_policy_reply *policy_reply)
{
    LOGT("%s(): processing dpi sni request", __func__);

    return fsm_apply_policies(policy_request, policy_reply);
}


static void
fsm_dpi_sni_process_report(struct fsm_policy_req *policy_request,
                           struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *pending_req;

    pending_req = policy_request->fqdn_req;

    /* overwrite the redirect action to block, as established
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

    LOGT("%s(): processing report for dpi sni", __func__);

    fsm_dpi_sni_send_report(policy_request, policy_reply);
}

static void
dpi_sni_free_memory(struct fsm_policy_req *policy_request,
                    struct fsm_policy_reply *policy_reply)
{
    FREE(policy_request->url);
    fsm_policy_free_request(policy_request);
    fsm_policy_free_reply(policy_reply);
}

static void
fsm_dpi_sni_process_verdict(struct fsm_policy_req *policy_request,
                            struct fsm_policy_reply *policy_reply)
{
    LOGT("%s(): processing dpi sni verdict with action %d", __func__, policy_reply->action);

    fsm_dpi_sni_process_report(policy_request, policy_reply);

    dpi_sni_free_memory(policy_request, policy_reply);
}

/**
 * @brief request an action from the policy engine
 *
 * @param request_args request parameter values
 * @param attr_value the attribute flow value
 * @return the action to take
 */
static int
fsm_dpi_sni_policy_req(struct fsm_request_args *request_args,
                       char *attr_value)
{
    struct fsm_policy_reply *policy_reply;
    struct fsm_policy_req *policy_request;
    int action;

    LOGD("%s: attribute: %d, value %s", __func__, request_args->request_type, attr_value);

    policy_request = fsm_dpi_sni_create_request(request_args, attr_value);
    if (policy_request == NULL)
    {
        LOGD("%s(): failed to create dpi sni policy request", __func__);
        goto error;
    }

    policy_reply = fsm_dpi_sni_create_reply(request_args);
    if (policy_reply == NULL)
    {
        LOGD("%s(): failed to initialize dpi sni reply", __func__);
        goto clean_policy_req;
    }

    LOGT("%s(): allocated policy_request == %p, policy_reply == %p",
         __func__,
         policy_request,
         policy_reply);

    /* process the input request */
    action = fsm_dpi_sni_process_request(policy_request, policy_reply);

    action = (action == FSM_BLOCK ? FSM_DPI_DROP : FSM_DPI_PASSTHRU);

    return action;

clean_policy_req:
    fsm_policy_free_request(policy_request);
error:
    return FSM_DPI_PASSTHRU;
}

/**
 * @brief check if a mac address belongs to a given tag or matches a value
 *
 * @param the mac address to check
 * @param val an opensync tag name or the string representation of a mac address
 * @return true if the mac matches the value, false otherwise
 */
static bool
fsm_dpi_sni_find_mac_in_val(os_macaddr_t *mac, char *val)
{
    char mac_s[32] = { 0 };
    bool rc;
    int ret;

    if (val == NULL) return false;
    if (mac == NULL) return false;

    snprintf(mac_s, sizeof(mac_s), PRI_os_macaddr_lower_t, FMT_os_macaddr_pt(mac));

    rc = om_tag_in(mac_s, val);
    if (rc) return true;

    ret = strncmp(mac_s, val, strlen(mac_s));
    return (ret == 0);
}

static void
dpi_parse_populate_sockaddr(int af, void *ip_addr, struct sockaddr_storage *dst)
{
    if (!ip_addr) return;

    if (af == AF_INET)
    {
        struct sockaddr_in *in4 = (struct sockaddr_in *)dst;

        memset(in4, 0, sizeof(struct sockaddr_in));
        in4->sin_family = af;
        memcpy(&in4->sin_addr, ip_addr, sizeof(in4->sin_addr));
    }
    else if (af == AF_INET6)
    {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)dst;

        memset(in6, 0, sizeof(struct sockaddr_in6));
        in6->sin6_family = af;
        memcpy(&in6->sin6_addr, ip_addr, sizeof(in6->sin6_addr));
    }
}

bool
is_redirected_flow(struct net_md_flow_info *info, const char *attr)
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

    LOGT("%s(): checking attribute %s", __func__, attr);

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
        dpi_parse_populate_sockaddr(af, info->remote_ip, &ip);
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
    memset(&lkp_req, 0, sizeof(lkp_req));
    lkp_req.device_mac = info->local_mac;
    af = info->ip_version == 4 ? AF_INET : AF_INET6;

    dpi_parse_populate_sockaddr(af, info->remote_ip, &ip);
    lkp_req.ip_addr = &ip;
    lkp_req.direction = info->direction;

    rc = dns_cache_ip2action_lookup(&lkp_req);
    LOGD("%s(): cache lookup returned %d, redirect flag: %d",
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
 * @brief process a flow attribute
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param value the attribute flow value
 * @param acc the flow
 */
int
fsm_dpi_sni_process_attr(struct fsm_session *session, char *attr, char *value,
                         struct net_md_stats_accumulator *acc)
{
    struct fsm_dpi_sni_session *u_session;
    struct fsm_request_args request_args;
    struct net_md_flow_info info;
    struct net_md_flow_key *key;
    int request_type;
    bool excluded;
    bool included;
    int action;
    bool act;
    bool ret;
    bool rc;

    action = FSM_DPI_IGNORED;

    if (acc == NULL) goto out;
    if (acc->originator == NET_MD_ACC_UNKNOWN_ORIGINATOR) goto out;

    key = acc->key;
    if (key == NULL) goto out;

    if (session == NULL) goto out;
    u_session = session->handler_ctxt;
    if (u_session == NULL) goto out;

    LOGT("%s: %s: attribute: %s, value %s", __func__,
         session->name, attr, value);
    LOGT("%s: service provider: %s", __func__,
         session->service ? session->service->name : "None");

    excluded = (u_session->excluded_devices != NULL);
    included = (u_session->included_devices != NULL);

    memset(&info, 0, sizeof(info));
    ret = net_md_get_flow_info(acc, &info);
    if (!ret) goto out;

    if (excluded || included)
    {
        if (info.local_mac == NULL) goto out;
        if (excluded)
        {
            act = !fsm_dpi_sni_find_mac_in_val(info.local_mac,
                                               u_session->excluded_devices);
            if (!act) goto out;
        }
        if (included)
        {
            act = fsm_dpi_sni_find_mac_in_val(info.local_mac,
                                              u_session->included_devices);
            if (!act) goto out;
        }
    }

    if (session->service == NULL) return FSM_DPI_PASSTHRU;

    rc = is_redirected_flow(&info, attr);
    if (rc == true)
    {
        LOGD("%s(): redirected flow detected", __func__);
        goto out;
    }

    request_type = fsm_req_type(attr);
    if (request_type == FSM_UNKNOWN_REQ_TYPE)
    {
        LOGE("%s: unknown attribute %s", __func__, attr);
        return FSM_DPI_PASSTHRU;
    }

    memset(&request_args, 0, sizeof(request_args));
    request_args.session = session;
    request_args.device_id = info.local_mac;
    request_args.acc = acc;
    request_args.request_type = request_type;

    action = fsm_dpi_sni_policy_req(&request_args, value);

out:
    return action;
}
