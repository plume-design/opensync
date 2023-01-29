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

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include "dns_cache.h"
#include "ds_tree.h"
#include "fsm.h"
#include "fsm_csum_utils.h"
#include "fsm_dns_utils.h"
#include "fsm_dpi_client_plugin.h"
#include "fsm_dpi_dns.h"
#include "fsm_dpi_utils.h"
#include "fsm_policy.h"
#include "json_mqtt.h"
#include "kconfig.h"
#include "log.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "network_metadata_report.h"
#include "sockaddr_storage.h"
#include "os.h"
#include "util.h"

#define DNS_QTYPE_A 1
#define DNS_QTYPE_AAAA 28
#define DNS_QTYPE_65 65

/* TTL Len 4 bytes + RD length 2 bytes */
#define DNS_TTL_START_OFFSET 6

/* Forward declaration */
struct dns_session *fsm_dpi_dns_get_session(struct fsm_session *session);

const char * const dns_state_str[] =
{
    [UNDEFINED] = "undefined",
    [BEGIN_DNS] = "begin",
    [DNS_QNAME] = "dns.qname",
    [DNS_TYPE] = "dns.type",
    [DNS_TTL] = "dns.ttl",
    [DNS_A] = "dns.a",
    [DNS_A_OFFSET] = "dns.a_offset",
    [DNS_AAAA] = "dns.aaaa",
    [DNS_AAAA_OFFSET] = "dns.aaaa_offset",
    [END_DNS] = "end",
    [IP_ADDRESS] = "IP address",
};

const char *dns_attr_value = "dns";

static struct dpi_dns_client main_data =
{
    .initialized = false,
};

struct dpi_dns_client *
fsm_dpi_dns_get_mgr(void)
{
    return &main_data;
}

static enum dns_state
get_dns_state(const char *attribute)
{
#define GET_DNS_STATE(attr, x)                \
    do                                        \
    {                                         \
        int cmp;                              \
        cmp = strcmp(attr, dns_state_str[x]); \
        if (cmp == 0) return x;               \
    }                                         \
    while (0)

    GET_DNS_STATE(attribute, DNS_TYPE);
    GET_DNS_STATE(attribute, DNS_TTL);
    GET_DNS_STATE(attribute, DNS_A);
    GET_DNS_STATE(attribute, DNS_A_OFFSET);
    GET_DNS_STATE(attribute, DNS_AAAA);
    GET_DNS_STATE(attribute, DNS_AAAA_OFFSET);
    GET_DNS_STATE(attribute, BEGIN_DNS);
    GET_DNS_STATE(attribute, END_DNS);
    GET_DNS_STATE(attribute, DNS_QNAME);

    return UNDEFINED;
#undef GET_DNS_STATE
}

void
fsm_dpi_dns_reset_state(void)
{
    struct dpi_dns_client *mgr;
    struct dns_record *rec;

    mgr = fsm_dpi_dns_get_mgr();

    rec = &mgr->curr_rec_processed;
    MEMZERO(*rec);
    rec->next_state = BEGIN_DNS;
    rec->idx = 0;
}

static void
fsm_dpi_dns_send_report(struct fsm_policy_req *policy_request,
                        struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *pending_req;
    struct fsm_session *session;
    const char *action_str;
    char *report;

    action_str = fsm_policy_get_action_str(policy_reply->action);
    LOGT("%s: processing dpi dns verdict with action '%s'", __func__, action_str);

    if (policy_reply->to_report != true) return;

    pending_req = policy_request->fqdn_req;
    session = pending_req->fsm_context;

    report = jencode_url_report(session, pending_req, policy_reply);

    if (session->ops.send_report == NULL)
    {
        LOGD("%s: Incomplete setup. Not sending report %s", __func__, report);
        return;
    }
    session->ops.send_report(session, report);
}


/**
 * @brief: In case of gatekeeper policy, check if event
 * reporting is required. Gatekeeper policy triggers
 * reporting only for BLOCKED and REDIRECT action.
 * But if reporing is required for other action, then
 * reporting flag is set and the policy name is updated.
 */
static void
fsm_update_gk_reporting(struct fsm_policy_req *preq,
                        struct fsm_policy_reply *policy_reply)
{
    struct fsm_policy *fsm_policy;

    fsm_policy = preq->policy;
    if (fsm_policy == NULL) return;

    if (fsm_policy->action != FSM_GATEKEEPER_REQ) return;

    LOGT("%s(): checking if policy reporting is required.", __func__);
    /* gk has already taken the action to report, no need to check
     * further.
     */
    if (policy_reply->to_report == true)
    {
        if (policy_reply->rule_name == NULL)
        {
            policy_reply->rule_name = STRDUP(preq->rule_name);
        }
        return;
    }

    /* if policy does not ask for logging, just return */
    if (preq->report == false) return;

    /* policy is set to log (example logMacs), we need
     * to send the report, also overwrite policy name
     * from gatekeeper policy (gk_all) to the policy that Requires
     * logging
     */
    LOGT("%s(): setting reporting and updating policy name", __func__);
    policy_reply->to_report = true;
    FREE(policy_reply->rule_name);
    policy_reply->rule_name = STRDUP(preq->rule_name);
    policy_reply->action = preq->action;
    policy_reply->policy_idx = preq->policy_index;
}

static void
fsm_dpi_dns_process_verdict(struct fsm_policy_req *policy_request,
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

    /* Process reporting */
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

    pending_req->rd_ttl = policy_reply->rd_ttl;

    fsm_update_gk_reporting(policy_request, policy_reply);

    fsm_dpi_dns_send_report(policy_request, policy_reply);
}

struct fsm_policy_req *
fsm_dpi_dns_create_request(struct fsm_request_args *request_args,
                           char *hostname)
{
    struct fsm_policy_req *policy_request;
    struct fqdn_pending_req *pending_req;

    if (request_args == NULL || hostname == NULL) return NULL;

    /* initialize fsm policy request structure */
    policy_request = fsm_policy_initialize_request(request_args);
    if (policy_request == NULL)
    {
        LOGD("%s(): fsm policy request initialization failed for dpi sni", __func__);
        return NULL;
    }

    pending_req = policy_request->fqdn_req;
    policy_request->url = STRDUP(hostname);
    if (!policy_request->url) goto free_policy_req;
    policy_request->req_type = request_args->request_type;

    STRSCPY(pending_req->req_info->url, hostname);
    pending_req->numq = 1;

    return policy_request;

free_policy_req:
    fsm_policy_free_request(policy_request);
    return NULL;

}

void
fsm_dpi_dns_free_request(struct fsm_policy_req *request)
{
    fsm_dpi_dns_free_dns_response_ips(&request->fqdn_req->dns_response);
    FREE(request->url);
    fsm_policy_free_request(request);
}

struct fsm_policy_reply *
fsm_dpi_dns_create_reply(struct fsm_request_args *request_args)
{
    struct fsm_policy_client *policy_client;
    struct fsm_policy_reply *policy_reply;
    struct fsm_session *session;

    if (request_args == NULL) return NULL;

    session = request_args->session;
    if (session == NULL) return NULL;

    policy_client = &session->policy_client;

    policy_reply = fsm_policy_initialize_reply(session);
    if (policy_reply == NULL)
    {
        LOGD("%s(): failed to initialize policy reply for dpi sni", __func__);
        return NULL;
    }

    policy_reply->provider = session->service->name;
    policy_reply->policy_table = policy_client->table;
    policy_reply->send_report = session->ops.send_report;
    policy_reply->categories_check = session->provider_ops->categories_check;
    policy_reply->risk_level_check = session->provider_ops->risk_level_check;
    policy_reply->gatekeeper_req = session->provider_ops->gatekeeper_req;
    policy_reply->req_type = request_args->request_type;
    policy_reply->policy_response = fsm_dpi_dns_process_verdict;

    return policy_reply;
}

void
fsm_dpi_dns_free_reply(struct fsm_policy_reply *reply)
{
    fsm_policy_free_reply(reply);
}

void
fsm_dpi_dns_update_csum(struct net_header_parser *net_parser)
{
    struct udphdr *udp_hdr;
    uint8_t *packet;
    uint16_t csum;

    /* update check sum */
    packet = (uint8_t *)net_parser->start;
    udp_hdr = net_parser->ip_pld.udphdr;
    if (net_parser->ip_version == 4) udp_hdr->check = 0;
    else if (net_parser->ip_version == 6)
    {
        /* IPv6 */
        csum = fsm_compute_udp_checksum(packet, net_parser);
        udp_hdr->check = csum;
    }
    net_parser->payload_updated = true;
}

/**
 * @brief update dns response ips
 *
 * @param net_parser the packet container
 * @param acc the flow
 * @param policy_reply verdict info for the flow
 */
bool
fsm_dpi_dns_update_response_ips(struct net_header_parser *net_header,
                                struct net_md_stats_accumulator *acc,
                                struct fsm_policy_reply *policy_reply)
{
    struct net_md_flow_key *key;
    struct dpi_dns_client *mgr;
    struct dns_record *rec;
    uint8_t *packet;
    bool is_updated;
    size_t parsed;
    size_t i;

    is_updated = false;

    if (net_header == NULL) return is_updated;
    if (acc == NULL) return is_updated;

    key = acc->key;
    if (key == NULL) return is_updated;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized) return is_updated;

    rec = &mgr->curr_rec_processed;
    packet = (uint8_t *)net_header->start;
    parsed = net_header->parsed;

    if (key->ipprotocol != IPPROTO_UDP) return is_updated;

    for (i = 0; i < rec->idx; i++)
    {
        uint8_t *p_ttl = packet + parsed + (rec->resp[i].offset - DNS_TTL_START_OFFSET);
        if (rec->resp[i].ip_v == 4)
        {
            char *ipv4_addr = fsm_dns_check_redirect(policy_reply->redirects[0],
                                                     IPv4_REDIRECT);
            if (ipv4_addr == NULL)
            {
                ipv4_addr = fsm_dns_check_redirect(policy_reply->redirects[1],
                                                   IPv4_REDIRECT);
            }
            if (ipv4_addr != NULL)
            {
                inet_pton(AF_INET, ipv4_addr,
                          (packet + parsed + rec->resp[i].offset));
                *(uint32_t *)(p_ttl) = htonl(policy_reply->rd_ttl);
                is_updated = true;
            }
        }
        if (rec->resp[i].ip_v == 6)
        {
            char *ipv6_addr = fsm_dns_check_redirect(policy_reply->redirects[0],
                                                     IPv6_REDIRECT);
            if (ipv6_addr == NULL)
            {
                ipv6_addr = fsm_dns_check_redirect(policy_reply->redirects[1],
                                                   IPv6_REDIRECT);
            }
            if (ipv6_addr != NULL)
            {
                inet_pton(AF_INET6, ipv6_addr,
                              packet + parsed + rec->resp[i].offset);
                *(uint32_t *)(p_ttl) = htonl(policy_reply->rd_ttl);
                is_updated = true;
            }
        }
    }

    return is_updated;
}

void
fsm_dpi_dns_free_dns_response_ips(struct dns_response_s *dns_response)
{
    int i;

    if (dns_response == NULL) return;

    if (!dns_response->ipv4_cnt && !dns_response->ipv6_cnt) return;

    if (dns_response->ipv4_cnt)
    {
        for (i = 0; i < dns_response->ipv4_cnt; i++)
        {
            FREE(dns_response->ipv4_addrs[i]);
        }
    }
    if (dns_response->ipv6_cnt)
    {
        for (i = 0; i < dns_response->ipv6_cnt; i++)
        {
            FREE(dns_response->ipv6_addrs[i]);
        }
    }
}

void
fsm_dpi_dns_populate_response_ips(struct dns_response_s *dns_resp_ips)
{
    struct dpi_dns_client *mgr;
    struct dns_record *rec;
    size_t i;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized) return;

    rec = &mgr->curr_rec_processed;

    for (i = 0; i < rec->idx; i++)
    {
        char ipv4_addr[INET_ADDRSTRLEN];
        char ipv6_addr[INET6_ADDRSTRLEN];

        if (rec->resp[i].ip_v == 4)
        {
            inet_ntop(AF_INET, rec->resp[i].address, ipv4_addr, INET_ADDRSTRLEN);
            dns_resp_ips->ipv4_addrs[dns_resp_ips->ipv4_cnt] = strdup(ipv4_addr);
            dns_resp_ips->ipv4_cnt++;
        }
        if (rec->resp[i].ip_v == 6)
        {
            inet_ntop(AF_INET6, rec->resp[i].address, ipv6_addr, INET6_ADDRSTRLEN);
            dns_resp_ips->ipv6_addrs[dns_resp_ips->ipv6_cnt] = strdup(ipv6_addr);
            dns_resp_ips->ipv6_cnt++;
        }
    }
}

bool
is_record_to_process()
{
    struct dpi_dns_client *mgr;
    struct dns_record *rec;
    bool rc;

    mgr = fsm_dpi_dns_get_mgr();
    rc = (mgr->identical_plugin_enabled);
    if (!rc) return true;

    rec = &mgr->curr_rec_processed;
    rc = (rec->resp[0].type == DNS_QTYPE_65);
    return rc;
}

bool
is_valid_qtype(uint8_t qtype)
{
    bool rc;

    rc = false;
    switch (qtype)
    {
        case DNS_QTYPE_A:
        case DNS_QTYPE_AAAA:
        case DNS_QTYPE_65:
            rc = true;
            break;

        default:
            break;
    }
    return rc;
}

int
fsm_dpi_dns_process_dns_record(struct fsm_session *session,
                               struct net_md_stats_accumulator *acc,
                               struct net_header_parser *net_parser)
{
    struct fsm_dns_update_tag_param dns_tag_param;
    struct fsm_request_args fsm_request_param;
    struct dns_cache_param dns_cache_param;
    struct fsm_policy_req *policy_request;
    struct fsm_policy_reply *policy_reply;
    char str_addr[INET6_ADDRSTRLEN + 1];
    struct fqdn_pending_req *fqdn_req;
    struct sockaddr_storage ipaddr;
    struct net_md_flow_info info;
    struct dpi_dns_client *mgr;
    struct dns_record *rec;
    uint8_t qtype;
    int action;
    size_t i;
    bool rc;
    int af;

    action = FSM_DPI_PASSTHRU;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized) return action;

    /* Fetch information (category, etc) for this FQDN */
    rec = &mgr->curr_rec_processed;

    rc = is_record_to_process();
    if (!rc)
    {
        LOGT("%s: query type: %d dns_parse is enabled returning...", __func__, rec->resp[0].type);
        return action;
    }

    qtype = rec->resp[0].type;
    rc = is_valid_qtype(qtype);
    if (!rc)
    {
        LOGT("%s: not processing query type: %d ", __func__, rec->resp[0].type);
        return action;
    }


    LOGT("%s: Processing dpi DNS request for %s with %zd answers", __func__, rec->qname, rec->idx);

    /* Interact with GK to get verdict for the name only */
    MEMZERO(info);
    rc = net_md_get_flow_info(acc, &info);
    if (!rc) return action;

    MEMZERO(fsm_request_param);
    fsm_request_param.session = session;
    fsm_request_param.device_id = info.local_mac;
    fsm_request_param.acc = acc;
    fsm_request_param.request_type = FSM_FQDN_REQ;

    policy_request = fsm_dpi_dns_create_request(&fsm_request_param, rec->qname);
    if (policy_request == NULL)
    {
        LOGD("%s: Failed to create dpi DNS policy request", __func__);
        return action;
    }

    policy_reply = fsm_dpi_dns_create_reply(&fsm_request_param);
    if (policy_reply == NULL)
    {
        LOGD("%s: Failed to create dpi DNS policy reply", __func__);
        return action;
    }

    /* Populate the IPs in the FQDN request */
    fqdn_req = policy_request->fqdn_req;
    fsm_dpi_dns_populate_response_ips(&fqdn_req->dns_response);

    action = fsm_apply_policies(policy_request, policy_reply);

    LOGT("%s: dpi_dns policy received action: %s", __func__,
         fsm_policy_get_action_str(action));

    /*
     * We can have responses without `answers`.
     * Skip updating the information for the IPs.
     * The actual action returned in this case must be fully over-written,
     * since we want to _always_ finsih processing of DNS transaction.
     */
    if (rec->idx == 0)
    {
        LOGT("%s: No answer to process.", __func__);
        action = FSM_DPI_PASSTHRU;
        goto no_answer;
    }

    /* Now assign the gathered information to the IPs in the cache */
    for (i = 0; i < rec->idx; i++)
    {
        if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG))
        {
            if (rec->resp[i].ip_v == 4)
            {
                inet_ntop(AF_INET, rec->resp[i].address, str_addr, INET_ADDRSTRLEN);
            }
            else if (rec->resp[i].ip_v == 6)
            {
                inet_ntop(AF_INET6, rec->resp[i].address, str_addr, INET6_ADDRSTRLEN);
            }
            else
            {
                LOGD("%s: unknown AF family %d for response %zu", __func__,
                     rec->resp[i].ip_v, i);
                continue;
            }
            LOGD("%s: record: qname=%s type=%d ttl=%d addr=%s offset=%d",
                 __func__, rec->qname, rec->resp[i].type, rec->resp[i].ttl,
                 str_addr, rec->resp[i].offset);
        }

        MEMZERO(dns_cache_param);
        dns_cache_param.req = policy_request->fqdn_req;
        dns_cache_param.policy_reply = policy_reply;
        dns_cache_param.ttl = rec->resp[i].ttl;
        dns_cache_param.action_by_name = action;
        dns_cache_param.direction = NET_MD_ACC_OUTBOUND_DIR;
        dns_cache_param.network_id = fsm_ops_get_network_id(session, info.local_mac);

        if (rec->resp[i].ip_v == 4)
        {
            af = AF_INET;
        }
        else if (rec->resp[i].ip_v == 6)
        {
            af = AF_INET6;
        }
        else
        {
            LOGD("%s: unknown AF family %d for response %zu", __func__,
                 rec->resp[i].ip_v, i);
            continue;
        }
        sockaddr_storage_populate(af, rec->resp[i].address, &ipaddr);
        dns_cache_param.ipaddr = &ipaddr;

        rc = fsm_dns_cache_add_entry(&dns_cache_param);
        if (!rc)
        {
            LOGD("%s: Failed to insert DNS record", __func__);
        }
    }

    if (policy_reply->action == FSM_UPDATE_TAG)
    {
        /* Update Tag if we are interested in the IPs returned. */
        dns_tag_param.dev_id = fsm_request_param.device_id;
        dns_tag_param.policy_reply = policy_reply;
        dns_tag_param.dns_response = &fqdn_req->dns_response;
        if (dns_tag_param.dns_response->ipv4_cnt || dns_tag_param.dns_response->ipv6_cnt)
        {
            mgr->update_tag(&dns_tag_param);
        }
    }

    if ((action == FSM_REDIRECT) || (action == FSM_REDIRECT_ALLOW) || (action == FSM_BLOCK))
    {
        rc = fsm_dpi_dns_update_response_ips(net_parser, acc, policy_reply);
        if (!rc)
        {
            LOGD("%s: Failed to update response ips", __func__);
        }
    }

    /* Allow dns responses */
    action = FSM_DPI_PASSTHRU;

no_answer:
    if (net_parser != NULL)
    {
        /* update check sum */
        fsm_dpi_dns_update_csum(net_parser);
    }

    /* Cleanup */
    fsm_dpi_dns_free_reply(policy_reply);
    fsm_dpi_dns_free_request(policy_request);

    return action;
}

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
dns_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

void
fsm_dpi_dns_identical_plugin_status(struct fsm_session *session, bool status)
{
    struct dpi_dns_client *mgr;

    mgr = fsm_dpi_dns_get_mgr();
    mgr->identical_plugin_enabled = status;
    LOGT("%s: identical plugin enabled : %s", __func__, status ? "true" : "false");
}


/**
 * @brief session initialization entry point
 *
 * Initializes the plugin generic fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm
 *
 * @param session pointer provided by fsm
 */
int
fsm_dpi_dns_init(struct fsm_session *session)
{
    struct fsm_dpi_plugin_client_ops *client_ops;
    struct dns_session *dns_session;
    struct dpi_dns_client *mgr;
    char *provider;
    int ret;

    /* Initialize generic client */
    ret = fsm_dpi_client_init(session);
    if (ret != 0) return -1;

    /* And now all the DNS specific calls */
    session->ops.update = fsm_dpi_dns_update;
    session->ops.periodic = fsm_dpi_dns_periodic;
    session->ops.exit = fsm_dpi_dns_exit;
    session->ops.notify_identical_sessions = fsm_dpi_dns_identical_plugin_status;
    session->plugin_id = FSM_DPI_DNS_PLUGIN;

    /* Set the plugin specific ops */
    client_ops = &session->p_ops->dpi_plugin_client_ops;
    client_ops->process_attr = fsm_dpi_dns_process_attr;

    /* Fetch the specific updates for this client plugin */
    session->ops.update(session);

    mgr = fsm_dpi_dns_get_mgr();
    if (mgr->initialized) return 1;

    ds_tree_init(&mgr->fsm_sessions, dns_session_cmp,
                 struct dns_session, next);
    mgr->initialized = true;

    /* Look up the dns session */
    dns_session = fsm_dpi_dns_get_session(session);
    if (dns_session == NULL)
    {
        LOGE("%s: could not allocate dns parser", __func__);
        return -1;
    }

    provider = session->ops.get_config(session, "provider_plugin");
    if (provider != NULL)
    {
        dns_session->service_provider = dns_cache_get_service_provider(provider);
    }
    mgr->update_tag = fsm_dns_update_tag;

    return 0;
}


/*
 * Provided for compatibility
 */
int
dpi_dns_plugin_init(struct fsm_session *session)
{
    return fsm_dpi_dns_init(session);
}


void
fsm_dpi_dns_exit(struct fsm_session *session)
{
    LOGD("%s: Exit DNS client plugin", __func__);

    /* Free the generic client */
    fsm_dpi_client_exit(session);
}

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm url session to update
 */
void
fsm_dpi_dns_update(struct fsm_session *session)
{
    /* Generic config first */
    fsm_dpi_client_update(session);

    /* ADT specific entries */
    LOGD("%s: Updating DNS config", __func__);
}

void
fsm_dpi_dns_periodic(struct fsm_session *session)
{
    bool need_periodic;

    need_periodic = fsm_dpi_client_periodic_check(session);
    if (need_periodic)
    {
        /* Nothing specific to be done */
    }
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
fsm_dpi_dns_process_attr(struct fsm_session *session, const char *attr,
                         uint8_t type, uint16_t length, const void *value,
                         struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    struct net_header_parser *net_parser;
    struct net_md_stats_accumulator *acc;
    struct dpi_dns_client *mgr;
    enum dns_state curr_state;
    struct dns_record *rec;
    int action;
    int cmp;

    if (pkt_info == NULL) return FSM_DPI_IGNORED;

    acc = pkt_info->acc;
    if (acc == NULL) return FSM_DPI_IGNORED;

    net_parser = pkt_info->parser;
    if (net_parser == NULL) return FSM_DPI_IGNORED;

    /* Process the generic part (e.g., logging, include, exclude lists) */
    action = fsm_dpi_client_process_attr(session, attr, type, length, value, pkt_info);
    if (action == FSM_DPI_IGNORED) return action;

    /*
     * The combo (device, attribute) is to be processed, but no service provided
     * Pass through.
     */
    if (session->service == NULL) return FSM_DPI_PASSTHRU;

    action = FSM_DPI_IGNORED;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized)
    {
        LOGD("%s: not initialized", __func__);
        return -1;
    }

    rec = &mgr->curr_rec_processed;

    curr_state = get_dns_state(attr);
    switch (curr_state)
    {
        case BEGIN_DNS:
        {
            if (type != RTS_TYPE_STRING)
            {
                LOGD("%s: value for %s should be a string", __func__, attr);
                goto reset_state_machine;
            }
            cmp = strncmp(value, dns_attr_value, length);
            if (cmp)
            {
                LOGD("%s: value for %s should be %s", __func__, attr, dns_attr_value);
                goto reset_state_machine;
            }
            if (rec->next_state != UNDEFINED && rec->next_state != curr_state) goto wrong_state;

            fsm_dpi_dns_reset_state();
            rec->next_state = DNS_QNAME;
            LOGT("%s: start new DNS record - next is %s",
                 __func__, dns_state_str[rec->next_state]);
            break;
        }

        case DNS_QNAME:
        {
            if (type != RTS_TYPE_STRING)
            {
                LOGD("%s: value for %s should be a string", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            STRSCPY_LEN(rec->qname, value, length);
            rec->next_state = DNS_TYPE;
            LOGT("%s: copied %s = %s - next is %s",
                 __func__, dns_state_str[curr_state], rec->qname,
                 dns_state_str[rec->next_state]);
            break;
        }

        case DNS_TYPE:
        {
            if (type != RTS_TYPE_NUMBER)
            {
                LOGD("%s: value for %s should be a number", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            rec->next_state = DNS_TTL;
            rec->resp[rec->idx].type = *(int64_t *)value;
            LOGT("%s: copied %s = %d - next is %s",
                 __func__, dns_state_str[curr_state], rec->resp[rec->idx].type,
                 dns_state_str[rec->next_state]);
            break;
        }

        case DNS_TTL:
        {
            if (type != RTS_TYPE_NUMBER)
            {
                LOGD("%s: value for %s should be a number", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            rec->next_state = IP_ADDRESS;
            rec->resp[rec->idx].ttl = *(int64_t *)value;
            LOGT("%s: copied %s = %d - next is %s",
                 __func__, dns_state_str[curr_state], rec->resp[rec->idx].ttl,
                 dns_state_str[rec->next_state]);
            break;
        }

        case DNS_A:
        {
            char ipv4_addr[INET_ADDRSTRLEN];
            const char *res;

            if (type != RTS_TYPE_BINARY)
            {
                LOGD("%s: value for %s should be a binary array", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != IP_ADDRESS) goto wrong_state;
            rec->next_state = DNS_A_OFFSET;
            rec->resp[rec->idx].ip_v = 4;
            memcpy(rec->resp[rec->idx].address, value, length);
            res = inet_ntop(AF_INET, value, ipv4_addr, INET_ADDRSTRLEN);
            LOGT("%s: copied %s - next is %s",
                 __func__, (res != NULL ? ipv4_addr : dns_state_str[curr_state]),
                            dns_state_str[rec->next_state]);
            break;
        }

        case DNS_A_OFFSET:
        {
            if (type != RTS_TYPE_NUMBER)
            {
                LOGD("%s: value for %s should be a number", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            rec->next_state = DNS_TYPE;
            rec->resp[rec->idx].offset = *(int64_t *)value;
            LOGT("%s: copied %s - next is %s or end",
                 __func__, dns_state_str[curr_state], dns_state_str[rec->next_state]);
            rec->idx++;
            break;
        }

        case DNS_AAAA:
        {
            char ipv6_addr[INET6_ADDRSTRLEN];
            const char *res;

            if (type != RTS_TYPE_BINARY)
            {
                LOGD("%s: value for %s should be a binary array", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != IP_ADDRESS) goto wrong_state;

            rec->next_state = DNS_AAAA_OFFSET;
            rec->resp[rec->idx].ip_v = 6;
            memcpy(rec->resp[rec->idx].address, value, length);
            res = inet_ntop(AF_INET6, value, ipv6_addr, INET6_ADDRSTRLEN);
            LOGT("%s: copied %s - next is %s",
                 __func__, (res != NULL ? ipv6_addr : dns_state_str[curr_state]),
                 dns_state_str[rec->next_state]);
            break;
        }

        case DNS_AAAA_OFFSET:
        {
            if (type != RTS_TYPE_NUMBER)
            {
                LOGD("%s: value for %s should be a number", __func__, attr);
                goto reset_state_machine;
            }
            if (rec->next_state != curr_state) goto wrong_state;

            rec->next_state = DNS_TYPE;
            rec->resp[rec->idx].offset = *(int64_t *)value;
            LOGT("%s: copied %s - next is %s or end",
                 __func__, dns_state_str[curr_state], dns_state_str[rec->next_state]);
            rec->idx++;
            break;
        }

        case END_DNS:
        {
            if (type != RTS_TYPE_STRING)
            {
                LOGD("%s: value for %s should be a string", __func__, attr);
                goto reset_state_machine;
            }
            cmp = strncmp(value, dns_attr_value, length);
            if (cmp)
            {
                LOGD("%s: value for %s should be %s", __func__, attr, dns_attr_value);
                goto reset_state_machine;
            }
            if (rec->next_state == DNS_TYPE && rec->idx == 0)
            {
                LOGD("%s: No A, AAAA or HTTPS for %s", __func__, rec->qname);
            }
            if (rec->next_state != DNS_TYPE) goto wrong_state;

            rec->next_state = BEGIN_DNS;

            /* Now we can process the record */
            action = fsm_dpi_dns_process_dns_record(session, acc, net_parser);
            break;
        }

        default:
        {
            LOGD("%s: Unexpected attr '%s' (expected state '%s')",
                 __func__, attr, dns_state_str[rec->next_state]);
            goto reset_state_machine;
        }
    }

    return action;

wrong_state:
    LOGD("%s: Failed when processing attr '%s' (expected state '%s')",
         __func__, attr, dns_state_str[rec->next_state]);
reset_state_machine:
    fsm_dpi_dns_reset_state();
    return -1;
}

/**
 * @brief get a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct dns_session *
fsm_dpi_dns_get_session(struct fsm_session *session)
{
    struct dpi_dns_client *mgr;
    struct dns_session *d_session;
    ds_tree_t *sessions;

    mgr = fsm_dpi_dns_get_mgr();
    sessions = &mgr->fsm_sessions;

    d_session = ds_tree_find(sessions, session);
    if (d_session != NULL) return d_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    d_session = CALLOC(1, sizeof(struct dns_session));
    if (d_session == NULL) return NULL;

    d_session->initialized = false;
    ds_tree_insert(sessions, d_session, session);

    return d_session;
}
