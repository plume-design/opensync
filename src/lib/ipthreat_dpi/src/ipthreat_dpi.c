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

#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include "fsm.h"
#include "const.h"
#include "ds_tree.h"
#include "log.h"
#include "ipthreat_dpi.h"
#include "assert.h"
#include "json_util.h"
#include "ovsdb.h"
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "net_header_parse.h"
#include "network_metadata_report.h"
#include "dns_cache.h"
#include "fsm_dpi_utils.h"
#include "json_mqtt.h"
#include "memutil.h"
#include "fsm_policy.h"

#define IPTHREAT_CACHE_INTERVAL  120

#define IPTHREAT_DEFAULT_TTL 300

static struct ipthreat_dpi_cache
cache_mgr =
{
    .initialized = false,
};


struct ipthreat_dpi_cache *
ipthreat_dpi_get_mgr(void)
{
    return &cache_mgr;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
ipthreat_dpi_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * @param session the fsm session
 */
void
ipthreat_dpi_plugin_periodic(struct fsm_session *session)
{
    time_t now;
    struct ipthreat_dpi_cache *mgr = ipthreat_dpi_get_mgr();

    now = time(NULL);

    if ((now - mgr->periodic_ts) < IPTHREAT_CACHE_INTERVAL) return;

    mgr->periodic_ts = now;

    /*Clean up expired ip2action entries every 2mins.*/
    dns_cache_ttl_cleanup();
    print_dns_cache_size();
}


/**
 * @brief session configuration uodate handler
 *
 * @param session the fsm session to update
 */
static void
ipthreat_dpi_plugin_update(struct fsm_session *session)
{
    return;
}


/**
 * @brief session configuration uodate handler
 *
 * @param session the fsm session to update
 */
static void
ipthreat_dpi_plugin_update_client(struct fsm_session *session,
                                  struct policy_table *table)
{
    struct ipthreat_dpi_session *ipthreat_dpi_session;
    struct fsm_policy_client *client;
    char *outbound;
    char *inbound;
    size_t len;
    int cmp;

    len = 0;
    if (table != NULL) len = strlen(table->name);
    ipthreat_dpi_session = session->handler_ctxt;

    outbound = session->ops.get_config(session, "outbound_policy_table");
    if (outbound != NULL)
    {
        client = &ipthreat_dpi_session->outbound;
        if (table == NULL) client->table = NULL;
        else
        {
            cmp = strncmp(outbound, table->name, len);
            if (!cmp)
            {
                client->table = table;
                return;
            }
        }
    }

    inbound = session->ops.get_config(session, "inbound_policy_table");
    if (inbound != NULL)
    {
        client = &ipthreat_dpi_session->inbound;
        if (table == NULL) client->table = NULL;
        else
        {
            cmp = strncmp(inbound, table->name, len);
            if (!cmp)
            {
                client->table = table;
                return;
            }
        }
    }
}


/**
 * @brief get session name
 *
 * return then session name
 */
char *
ipthreat_get_session_name(struct fsm_policy_client *client)
{
    struct fsm_session *session;

    if (client == NULL) return NULL;

    session = client->session;
    if (session == NULL) return NULL;

    return session->name;
}



/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
ipthreat_dpi_plugin_init(struct fsm_session *session)
{
    struct ipthreat_dpi_session *ipthreat_dpi_session;
    struct fsm_dpi_plugin_ops *dpi_plugin_ops;
    struct fsm_policy_client *client;
    struct ipthreat_dpi_cache *mgr;
    char *outbound;
    char *inbound;

    if (session == NULL) return -1;

    mgr = ipthreat_dpi_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(&mgr->ipt_sessions, ipthreat_dpi_session_cmp,
                     struct ipthreat_dpi_session, session_node);
        dns_cache_init();
        mgr->periodic_ts = time(NULL);
        mgr->initialized = true;
    }

    /* Look up the ipthreat_dpi session */
    ipthreat_dpi_session = ipthreat_dpi_lookup_session(session);
    if (ipthreat_dpi_session == NULL)
    {
        LOGE("%s: could not allocate the ipthreat_dpi plugin.", __func__);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (ipthreat_dpi_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.periodic = ipthreat_dpi_plugin_periodic;
    session->ops.exit = ipthreat_dpi_plugin_exit;
    session->ops.update = ipthreat_dpi_plugin_update;
    session->handler_ctxt = ipthreat_dpi_session;

    /* Set the plugin ops */
    dpi_plugin_ops = &session->p_ops->dpi_plugin_ops;
    dpi_plugin_ops->handler = ipthreat_dpi_plugin_handler;

    /* Wrap up the session initialization */
    ipthreat_dpi_session->session = session;

    /* Prepare policy clients */
    outbound = session->ops.get_config(session, "outbound_policy_table");
    if (outbound != NULL)
    {
        client = &ipthreat_dpi_session->outbound;
        client->session = session;
        client->update_client = ipthreat_dpi_plugin_update_client;
        client->session_name = ipthreat_get_session_name;
        client->name = STRDUP(outbound);
        fsm_policy_register_client(client);
    }

    inbound = session->ops.get_config(session, "inbound_policy_table");
    if (inbound != NULL)
    {
        client = &ipthreat_dpi_session->inbound;
        client->session = session;
        client->name = STRDUP(inbound);
        client->update_client = ipthreat_dpi_plugin_update_client;
        client->session_name = ipthreat_get_session_name;
        fsm_policy_register_client(client);
    }
    ipthreat_dpi_session->initialized = true;
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
ipthreat_dpi_plugin_exit(struct fsm_session *session)
{
    struct ipthreat_dpi_session *ipthreat_dpi_session;
    struct fsm_policy_client *client;
    struct ipthreat_dpi_cache *mgr;

    mgr = ipthreat_dpi_get_mgr();
    if (!mgr->initialized) return;

    /* Deregister policy clients */
    ipthreat_dpi_session = session->handler_ctxt;

    client = &ipthreat_dpi_session->outbound;
    fsm_policy_deregister_client(client);
    FREE(client->name);

    client = &ipthreat_dpi_session->inbound;
    fsm_policy_deregister_client(client);
    FREE(client->name);

    dns_cache_cleanup_mgr();
    ipthreat_dpi_delete_session(session);

    mgr->initialized = false;
    return;
}

/**
 * @brief send an event report
 *
 * @param req the request triggering the report
 */
static void
ipthreat_dpi_send_report(struct fsm_policy_req *policy_req,
                         struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *req;
    struct fsm_session *session;
    char *report;

    if (policy_reply->to_report != true) return;

    req = policy_req->fqdn_req;
    session = req->fsm_context;

    report = jencode_url_report(session, req, policy_reply);
    session->ops.send_report(session, report);
}

/**
 * @brief Set wb details..
 *
 * receive wb dst and src.
 *
 * @return void.
 *
 */
void
populate_wb_dns_cache_entry(struct ip2action_wb_info *i2a_cache_wb,
                            struct fsm_wp_info *fqdn_reply_wb)
{
    i2a_cache_wb->risk_level = fqdn_reply_wb->risk_level;
}

/**
 * @brief Set bc details.
 *
 * receive bc dst, src and nelems.
 *
 * @return void.
 *
 */
void
populate_bc_dns_cache_entry(struct ip2action_bc_info *i2a_cache_bc,
                            struct fsm_bc_info *fqdn_reply_bc,
                            uint8_t nelems)
{
    size_t index;

    i2a_cache_bc->reputation = fqdn_reply_bc->reputation;
    for (index = 0; index < nelems; index++)
    {
        i2a_cache_bc->confidence_levels[index] =
            fqdn_reply_bc->confidence_levels[index];
    }
}

/**
 * @brief Set gk details.
 *
 * receive gk dst and src.
 *
 * @return void.
 *
 */
void
populate_gk_dns_cache_entry(struct ip2action_gk_info *i2a_cache_gk,
                            struct fsm_gk_info *fqdn_reply_gk)
{
    i2a_cache_gk->confidence_level = fqdn_reply_gk->confidence_level;
    i2a_cache_gk->category_id = fqdn_reply_gk->category_id;
    if (fqdn_reply_gk->gk_policy)
    {
        i2a_cache_gk->gk_policy = fqdn_reply_gk->gk_policy;
    }
}


/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 * @param args the fsm session
 * @param net_parser the packet container
 */
void
ipthreat_dpi_plugin_handler(struct fsm_session *session,
                            struct net_header_parser *net_parser)
{
    struct ipthreat_dpi_session *ds_session;
    struct ipthreat_dpi_parser  *parser;
    size_t len;

    ds_session = (struct ipthreat_dpi_session *)session->handler_ctxt;
    if (!ds_session) return;

    if (net_parser == NULL) return;

    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    len = ipthreat_dpi_parse_message(parser);
    if (len == 0) return;

    ipthreat_dpi_process_message(ds_session);

    return;
}

static void
util_populate_sockaddr(int af, void *ip_addr, struct sockaddr_storage *dst)
{
    if (!ip_addr) return;

    if (af == AF_INET)
    {
        struct sockaddr_in *in4 = (struct sockaddr_in *)dst;

        memset(in4, 0, sizeof(struct sockaddr_in));
        in4->sin_family = af;
        memcpy(&in4->sin_addr, ip_addr, sizeof(in4->sin_addr));
    } else if (af == AF_INET6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)dst;

        memset(in6, 0, sizeof(struct sockaddr_in6));
        in6->sin6_family = af;
        memcpy(&in6->sin6_addr, ip_addr, sizeof(in6->sin6_addr));
    }
    return;
}


/**
 * @brief return the policy table to be used
 *
 * @param session the fsm session
 * @param acc the ip flow
 */
static struct policy_table *
ipthreat_dpi_get_policy_table(struct ipthreat_dpi_session *ipthreat_dpi_session,
                              struct net_md_stats_accumulator *acc)
{
    struct fsm_policy_client *policy_client = NULL;

    if (acc->direction == NET_MD_ACC_OUTBOUND_DIR)
    {
        policy_client = &ipthreat_dpi_session->outbound;
    }
    else if (acc->direction == NET_MD_ACC_INBOUND_DIR)
    {
        policy_client = &ipthreat_dpi_session->inbound;
    }

    if (policy_client == NULL) return NULL;

    return policy_client->table;
}

static int
init_ipthreat_specific_request(struct fsm_policy_req *policy_request,
                               struct fsm_request_args *request_args,
                               struct sockaddr_storage *ip)
{
    struct fqdn_pending_req *pending_req;
    struct net_md_flow_key *key;
    struct sockaddr_storage *ip_addr;
    int result;

    key = request_args->acc->key;
    pending_req = policy_request->fqdn_req;

    policy_request->req_type = (key->ip_version == 4 ? FSM_IPV4_REQ : FSM_IPV6_REQ);
    pending_req->numq = 1;
    pending_req->rd_ttl = IPTHREAT_DEFAULT_TTL;

    result = getnameinfo((struct sockaddr *) (ip),
                         sizeof(struct sockaddr_storage),
                         pending_req->req_info->url,
                         sizeof(pending_req->req_info->url),
                         0,
                         0,
                         NI_NUMERICHOST);

    if (result)
    {
        LOGD("%s: failure: %s", __func__, strerror(errno));
        return -1;
    }

    ip_addr = policy_request->ip_addr;
    memcpy(ip_addr, ip, sizeof(*ip_addr));

    policy_request->url = pending_req->req_info->url;

    return 0;
}

/**
 * @brief creates ipthreat request structure
 *
 * Prepare a key to lookup the flow stats info, and update the flow stats.
 * @param request_args fsm request Arguments
 * @param ip_addr ip address for which policy check needs to be performed
 * @param table policy table.
 */
static struct fsm_policy_req *
ipthreat_create_request(struct fsm_request_args *request_args,
                        struct sockaddr_storage *ip_addr)
{
    struct fsm_policy_req *policy_request;
    int ret;

    /* initialize fsm policy request structure */
    policy_request = fsm_policy_initialize_request(request_args);
    if (policy_request == NULL)
    {
        LOGD("%s(): fsm policy request initialization failed for ipthreat", __func__);
        return NULL;
    }

    ret = init_ipthreat_specific_request(policy_request, request_args, ip_addr);
    if (ret == -1)
    {
        LOGT("%s(): failed to initialize ipthreat related request", __func__);
        goto free_policy_req;
    }

    return policy_request;

free_policy_req:
    fsm_policy_free_request(policy_request);
    return NULL;
}

bool
ipthreat_validate_reply(struct fsm_policy_req *policy_request,
                        struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *pending_req;

    pending_req = policy_request->fqdn_req;

    /* if the result was from the provider cache, it should already be present in dns
     * cache */
    if (policy_reply->from_cache)
    {
        LOGT("%s(): ipthreat reply is from cache", __func__);
        return false;
    }

    /* return if the reply is empty */
    if (pending_req->req_info->reply == NULL)
    {
        LOGT("%s(): reply is NULL", __func__);
        return false;
    }

    /* nothing to add in case of connection error */
    if (pending_req->req_info->reply->connection_error)
    {
        LOGT("%s(): connection error when processing ipthreat reply", __func__);
        return false;
    }

    /* return if the cloud lookup failed */
    if (policy_reply->categorized != FSM_FQDN_CAT_SUCCESS)
    {
        LOGT("%s(): ipthreat reply lookup fail", __func__);
        return false;
    }
    return true;
}

int
ipthreat_get_ttl(struct fsm_policy_req *policy_request,
                 struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *pending_req;
    int ttl;

    pending_req = policy_request->fqdn_req;

    ttl = (pending_req->rd_ttl < policy_reply->rd_ttl)
              ? pending_req->rd_ttl
              : policy_reply->rd_ttl;

    if (ttl != -1) return ttl;

    ttl = (policy_reply->cache_ttl != 0) ? policy_reply->cache_ttl
                                                 : IPTHREAT_DEFAULT_TTL;

    return ttl;
}

void
ipthreat_update_cache(struct fsm_policy_req *policy_request,
                      struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *pending_req;
    struct ip2action_req cache_req;
    size_t index;
    bool ret;
    int rc;

    memset(&cache_req, 0, sizeof(cache_req));
    pending_req = policy_request->fqdn_req;

    /* check if the reply is valid */
    ret = ipthreat_validate_reply(policy_request, policy_reply);
    if (ret == false) return;

    LOGT("%s(): updating cache for %s", __func__, policy_request->url);

    cache_req.device_mac = policy_request->device_id;
    cache_req.ip_addr = policy_request->ip_addr;
    cache_req.cache_ttl = ipthreat_get_ttl(policy_request, policy_reply);
    cache_req.action = policy_reply->action;
    cache_req.policy_idx = policy_reply->policy_idx;
    cache_req.service_id = pending_req->req_info->reply->service_id;
    cache_req.nelems = pending_req->req_info->reply->nelems;
    cache_req.cat_unknown_to_service = policy_reply->cat_unknown_to_service;

    for (index = 0; index < pending_req->req_info->reply->nelems; ++index)
    {
        cache_req.categories[index] = pending_req->req_info->reply->categories[index];
    }

    if (cache_req.service_id == IP2ACTION_BC_SVC)
    {
        populate_bc_dns_cache_entry(&cache_req.cache_bc,
                                    &pending_req->req_info->reply->bc,
                                    pending_req->req_info->reply->nelems);
    }
    else if (cache_req.service_id == IP2ACTION_WP_SVC)
    {
        populate_wb_dns_cache_entry(&cache_req.cache_wb,
                                    &pending_req->req_info->reply->wb);
    }
    else if (cache_req.service_id == IP2ACTION_GK_SVC)
    {
        populate_gk_dns_cache_entry(&cache_req.cache_gk,
                                    &pending_req->req_info->reply->gk);
    }
    else
    {
        LOGD("%s : service id %d no recognized", __func__, pending_req->req_info->reply->service_id);
    }

    rc = dns_cache_add_entry(&cache_req);
    if (!rc)
    {
        LOGW("%s: Couldn't add ip2action entry to cache.",__func__);
    }

}

void
ipthreat_process_report(struct fsm_policy_req* policy_request, struct fsm_policy_reply *policy_reply)
{
    policy_reply->to_report = true;

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

    ipthreat_dpi_send_report(policy_request, policy_reply);
}

void
ipthreat_process_action(struct fsm_session *session, int action,
                        struct net_header_parser *net_parser)
{
    struct net_md_stats_accumulator *acc;
    char ip_buf[INET6_ADDRSTRLEN] = { 0 };
    char *direction;

    acc = net_parser->acc;

    LOGT("%s(): ipthreat policy received action %d", __func__, action);

    if (action == FSM_DPI_DROP)
    {
        direction = (acc->direction == NET_MD_ACC_OUTBOUND_DIR) ?
                        "outbound" :
                        "inbound";
        if (acc->direction == NET_MD_ACC_OUTBOUND_DIR)
        {
            net_header_dstip_str(net_parser, ip_buf, sizeof(ip_buf));
        }
        else
        {
            net_header_srcip_str(net_parser, ip_buf, sizeof(ip_buf));
        }

        LOGI("%s: blocking access to: %s connection %s",
             __func__,
             direction,
             ip_buf);
        net_header_logi(net_parser);
    }

    fsm_dpi_set_acc_state(session, net_parser, action);
}


int
ipthreat_process_verdict(struct fsm_policy_req *policy_request,
                         struct fsm_policy_reply *policy_reply)
{
    LOGT("%s(): processing ipthreat verdict for policy req == %p",
         __func__,
         policy_request);

    ipthreat_update_cache(policy_request, policy_reply);

    ipthreat_process_report(policy_request, policy_reply);

    fsm_policy_free_request(policy_request);

    fsm_policy_free_reply(policy_reply);

    return 0;
}

static int
ipthreat_process_request(struct fsm_policy_req *policy_request,
                         struct fsm_policy_reply *policy_reply)
{
    LOGT("%s(): invoked", __func__);

    return fsm_apply_policies(policy_request, policy_reply);

}

static char *
ipthreat_get_provider(struct fsm_session *session)
{
    char *provider;

    if (session->service)
        provider = session->service->name;
    else
        provider = session->name;

    return provider;
}

static void
init_ipthreat_specific_reply(struct fsm_request_args *request_args,
                             struct fsm_policy_reply *policy_reply)
{
    struct fsm_session *session;

    session = request_args->session;

    policy_reply->provider = ipthreat_get_provider(session);;
    policy_reply->send_report = session->ops.send_report;
    policy_reply->categories_check = session->provider_ops->categories_check;
    policy_reply->risk_level_check = session->provider_ops->risk_level_check;
    policy_reply->gatekeeper_req = session->provider_ops->gatekeeper_req;
    policy_reply->policy_response = ipthreat_process_verdict;
}

struct fsm_policy_reply *
fsm_ipthreat_create_reply(struct fsm_request_args *request_args)
{
    struct fsm_policy_reply *policy_reply;

    policy_reply = fsm_policy_initialize_reply(request_args->session);
    if (policy_reply == NULL)
    {
        LOGD("%s(): failed to initialize policy reply for dpi sni", __func__);
        return NULL;
    }

    init_ipthreat_specific_reply(request_args, policy_reply);

    return policy_reply;
}

/**
 * @brief process the parsed message
 *
 * Prepare a key to lookup the flow stats info, and update the flow stats.
 * @param ds_session the ipthreat_dpi session pointing to the parsed message
 */
void
ipthreat_dpi_process_message(struct ipthreat_dpi_session *ds_session)
{
    struct net_md_stats_accumulator *acc;
    struct net_header_parser *net_parser;
    struct ipthreat_dpi_parser *parser;
    struct fsm_policy_req *policy_request;
    struct fsm_session *session;
    struct policy_table *table;
    struct eth_header *eth_hdr;
    struct net_md_flow_key key;
    struct iphdr *iphdr;
    struct ip6_hdr *ip6hdr;
    struct sockaddr_storage  ip_addr;
    os_macaddr_t             *dev_mac;
    struct fsm_request_args request_args;
    struct fsm_policy_reply *policy_reply;
    uint8_t *ip = NULL;
    int request_type;
    char *provider;
    int action;

    session = ds_session->session;
    parser = &ds_session->parser;
    net_parser = parser->net_parser;
    eth_hdr = &net_parser->eth_header;
    acc = net_parser->acc;

    table = ipthreat_dpi_get_policy_table(ds_session, acc);
    if (table == NULL)
    {
        LOGD("%s: Ignoring unknown/lan2lan direction packets : returning",__func__);
        return;
    }

    memset(&request_args, 0, sizeof(request_args));
    memset(&key, 0, sizeof(key));
    if (eth_hdr->srcmac)
    {
        key.smac = eth_hdr->srcmac;
    }

    if (eth_hdr->dstmac)
    {
        key.dmac = eth_hdr->dstmac;
    }

    key.vlan_id = eth_hdr->vlan_id;
    key.ethertype = eth_hdr->ethertype;

    key.ip_version = net_parser->ip_version;

    if (key.ip_version == 4) iphdr = net_header_get_ipv4_hdr(net_parser);
    if (key.ip_version == 6) ip6hdr = net_header_get_ipv6_hdr(net_parser);

    if ((acc->originator == NET_MD_ACC_ORIGINATOR_SRC &&
        acc->direction == NET_MD_ACC_OUTBOUND_DIR) ||
        (acc->originator == NET_MD_ACC_ORIGINATOR_DST &&
         acc->direction == NET_MD_ACC_INBOUND_DIR))
    {
        dev_mac = key.smac;
        if (key.ip_version == 4) ip = (uint8_t *)(&iphdr->daddr);
        if (key.ip_version == 6) ip = (uint8_t *)(&ip6hdr->ip6_dst.s6_addr);
    }
    else if ((acc->originator == NET_MD_ACC_ORIGINATOR_SRC &&
             acc->direction == NET_MD_ACC_INBOUND_DIR) ||
             (acc->originator == NET_MD_ACC_ORIGINATOR_DST &&
             acc->direction == NET_MD_ACC_OUTBOUND_DIR))
    {
        dev_mac = key.dmac;
        if (key.ip_version == 4) ip = (uint8_t *)(&iphdr->saddr);
        if (key.ip_version == 6) ip = (uint8_t *)(&ip6hdr->ip6_src.s6_addr);
    }
    else
    {
        LOGD("%s: Ignoring unknown/lan2lan direction packets.",__func__);
        return;
    }

    util_populate_sockaddr(key.ip_version == 4 ? AF_INET : AF_INET6, ip, &ip_addr);

    provider = ipthreat_get_provider(session);
    if (provider == NULL)
    {
        LOGD("%s(): ipthreat provider is NULL", __func__);
        goto error;
    }

    /* set the action as pass-through in case on invalid acc value */
    if (acc == NULL || acc->key == NULL || acc->key->ip_version == 0)
    {
        LOGD("%s(): invalid accumulator value", __func__);
        goto error;
    }

    request_args.session = session;
    request_args.device_id = dev_mac;
    request_args.acc = acc;
    request_type = (acc->key->ip_version == 4 ? FSM_IPV4_REQ : FSM_IPV6_REQ);
    request_args.request_type = request_type;

    policy_request = ipthreat_create_request(&request_args, &ip_addr);
    if (policy_request == NULL)
    {
        LOGD("%s(): failed to create ipthreat policy request", __func__);
        goto error;
    }

    policy_reply = fsm_ipthreat_create_reply(&request_args);
    if (policy_reply == NULL)
    {
        LOGD("%s(): failed to initialize ipthreat reply", __func__);
        goto clean_policy_req;
    }

    policy_reply->policy_table = table;
    policy_reply->req_type = policy_request->req_type;

    LOGT("%s(): allocated policy_request == %p, policy_reply == %p",
         __func__,
         policy_request,
         policy_reply);

    /* process the input request */
    action = ipthreat_process_request(policy_request, policy_reply);

    action = (action == FSM_BLOCK ? FSM_DPI_DROP : FSM_DPI_PASSTHRU);

    /* process the verdict */
    ipthreat_process_action(session, action, net_parser);

    return;


clean_policy_req:
    fsm_policy_free_request(policy_request);

error:
    fsm_dpi_set_acc_state(session, net_parser, FSM_DPI_PASSTHRU);
}


/**
 * @brief parses the received message
 *
 * @param parser the parsed data container
 * @return the size of the parsed message, or 0 on parsing error.
 */
size_t
ipthreat_dpi_parse_message(struct ipthreat_dpi_parser *parser)
{
    struct net_header_parser *net_parser;
    size_t len;

    if (parser == NULL) return 0;

    /* Parse network header */
    net_parser = parser->net_parser;
    parser->parsed = net_parser->parsed;
    parser->data = net_parser->data;

    /* Adjust payload length to remove potential ethernet padding */
    parser->data_len = net_parser->packet_len - net_parser->parsed;

    /* Parse the message content */
    len = ipthreat_dpi_parse_content(parser);

    return len;
}

/**
 * @brief parses the received message content
 *
 * @param parser the parsed data container
 * @return the size of the parsed message content, or 0 on parsing error.
 */
size_t
ipthreat_dpi_parse_content(struct ipthreat_dpi_parser *parser)
{
    /*
     * Place holder to process the packet content after the network header
     */
    return parser->parsed;
}


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct ipthreat_dpi_session *
ipthreat_dpi_lookup_session(struct fsm_session *session)
{
    struct ipthreat_dpi_cache *mgr;
    struct ipthreat_dpi_session *ds_session;
    ds_tree_t *sessions;

    mgr = ipthreat_dpi_get_mgr();
    sessions = &mgr->ipt_sessions;

    ds_session = ds_tree_find(sessions, session);
    if (ds_session != NULL) return ds_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    ds_session = CALLOC(1, sizeof(struct ipthreat_dpi_session));
    if (ds_session == NULL) return NULL;

    ds_tree_insert(sessions, ds_session, session);

    return ds_session;
}


/**
 * @brief Frees a ipthreat_dpi session
 *
 * @param ds_session the ipthreat_dpi session to free
 */
void
ipthreat_dpi_free_session(struct ipthreat_dpi_session *ds_session)
{
    FREE(ds_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the ipthreat_dpi session to delete
 */
void
ipthreat_dpi_delete_session(struct fsm_session *session)
{
    struct ipthreat_dpi_cache *mgr;
    struct ipthreat_dpi_session *ds_session;
    ds_tree_t *sessions;

    mgr = ipthreat_dpi_get_mgr();
    sessions = &mgr->ipt_sessions;

    ds_session = ds_tree_find(sessions, session);
    if (ds_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, ds_session);
    ipthreat_dpi_free_session(ds_session);

    return;
}

