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

#ifndef GATEKEEPER_H_INCLUDED
#define GATEKEEPER_H_INCLUDED

#include <curl/curl.h>
#include <limits.h>
#include <stdint.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "fsm.h"
#include "ds_tree.h"
#include "wc_telemetry.h"
#include "gatekeeper_ecurl.h"

#define GK_CACHE_PERSISTENCE_INTERVAL (60*60*24) /* 24 hours */
#define GK_PERIODIC_INTERVAL 120
#define GK_UNCATEGORIZED_ID 15
#define GK_CURL_TIMEOUT      (2*60)


struct gk_req_ids
{
    uint32_t req_fqdn_id;
    uint32_t req_ipv4_id;
    uint32_t req_ipv6_id;
    uint32_t req_ipv4_tuple_id;
    uint32_t req_ipv6_tuple_id;
    uint32_t req_app_id;
    uint32_t req_https_sni_id;
    uint32_t req_http_host_id;
    uint32_t req_http_url_id;
};

struct gk_curl_multi_info
{
    struct ev_timer timer_event;
    bool mcurl_connection_active;
    time_t mcurl_connection_time;
    struct ev_loop *loop;
    CURLM *mcurl_handle;
    int still_running;
};

struct gk_mcurl_data
{
    int req_type;
    int req_id;
    time_t timestamp;
    struct timespec req_time;
    struct fsm_gk_verdict *gk_verdict;
    ds_tree_node_t mcurl_req_node;
};

/**
 * @brief the plugin manager, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct fsm_gk_mgr
{
    bool initialized;
    time_t gk_time;
    time_t last_persistence_store;
    struct gk_req_ids req_ids;
    int (*getaddrinfo)(const char *node, const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res);
    ds_tree_t fsm_sessions;
    char *cache_seed_version;
};

struct fsm_gk_verdict
{
    struct fsm_policy_req *policy_req;
    struct fsm_policy_reply *policy_reply;
    struct gk_packed_buffer *gk_pb;
    struct fsm_gk_session *gk_session_context;
};


struct gatekeeper_offline
{
    time_t offline_ts;
    time_t check_offline;
    bool provider_offline;
    uint32_t connection_failures;
};


struct gk_cname_offline
{
    time_t offline_ts;
    time_t check_offline;
    bool cname_offline;
    uint32_t cname_resolve_failures;
};

/**
 * @brief a session, instance of processing state and routines.
 *
 * The session provides an executing instance of the services'
 * provided by the plugin.
 * It embeds:
 * - a fsm session
 * - state information
 * - a packet parser
 * - latency counters
 * - a set of devices presented to the session
 */
struct fsm_gk_session
{
    struct fsm_session *session;
    struct gk_server_info gk_server_info;
    struct gk_curl_multi_info mcurl;
    struct gk_curl_easy_info ecurl;
    bool enable_multi_curl;
    bool initialized;
    int32_t reported_lookup_failures;
    int32_t remote_lookup_retries;
    ds_tree_node_t session_node;
    ds_tree_t mcurl_data_tree;          /* tree for storing gk_mcurl_data */
    struct fsm_url_stats health_stats;
    time_t health_stats_report_ts;
    long health_stats_report_interval;
    char *health_stats_report_topic;
    struct gkc_report_aggregator *hero_stats;
    time_t hero_stats_report_ts;
    long hero_stats_report_interval;
    char *hero_stats_report_topic;
    struct gatekeeper_offline gk_offline;
    struct gk_cname_offline cname_offline;
    uint32_t dns_cache_hit_count;
    const char *pattern_fqdn_lan;
    const char *pattern_fqdn;
    regex_t *re_lan;
    regex_t *re;
    struct fsm_policy_client cache_flush_client;
};


struct fsm_gk_mgr *
gatekeeper_get_mgr(void);


/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
gatekeeper_plugin_init(struct fsm_session *session);


/**
 * @brief initializes gatekeeper module plugin
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
gatekeeper_module_init(struct fsm_session *session);

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
gatekeeper_exit(struct fsm_session *session);


/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * Sends a flow stats report.
 * @param session the fsm session
 */
void
gatekeeper_periodic(struct fsm_session *session);


void
gatekeeper_update(struct fsm_session *session);

/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_gk_session *
gatekeeper_lookup_session(struct fsm_session *session);


/**
 * @brief Frees a gate keeper session
 *
 * @param gksession the gk session to delete
 */
void
gatekeeper_free_session(struct fsm_gk_session *gk_session);


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the bc session to delete
 */
void
gatekeeper_delete_session(struct fsm_session *session);


/**
 * @brief initializes gate keeper plugin
 *
 * Initializes the gate keeper plugin once, with the parameters
 * given within the session.
 * @param session the fsm session containing the BC service config
 */
bool
gatekeeper_init(struct fsm_session *session);


/**
 * @brief performs check whether to allow or block this packet.
 *        by connecting ot the guard server.
 *
 * @param session the request being processed
 * @param req the policy being processed
 * @param policy the policy being checked against
 *
 */
bool
gatekeeper_category_check(struct fsm_session *session,
                          struct fsm_policy_req *req,
                          struct fsm_policy *policy);

/**
 * @brief frees fsm_gk_verdict structure
 *
 * @return None
 */
void
free_gk_verdict(struct fsm_gk_verdict *gk_verdict);

bool
gk_check_policy_in_cache(struct fsm_policy_req *req, struct fsm_policy_reply *policy_reply);

bool
gk_add_policy_to_cache(struct fsm_policy_req *req, struct fsm_policy_reply *policy_reply);

/**
 * @brief computes health stats for gatekeeper
 *
 * @param fsm_gk_session pointer to gatekeeper session
 * @param hs pointer to health stats
 */
void
gatekeeper_report_compute_health_stats(struct fsm_gk_session *fsm_gk_session,
                                       struct wc_health_stats *hs);

bool
gk_process_using_multi_curl(struct fsm_policy_req *req, struct fsm_policy_reply *policy_reply);

void
free_mcurl_data(struct gk_mcurl_data *mcurl_request);

bool
gk_process_using_multi_curl(struct fsm_policy_req *policy_req,
                            struct fsm_policy_reply *policy_reply);
bool
gk_lookup_using_multi_curl(struct fsm_policy_req* req);

long
fsm_gk_update_latencies(struct fsm_gk_session *gk_session,
                        struct timespec *start, struct timespec *end);

void
gk_update_uncategorized_count(struct fsm_gk_session *fsm_gk_session,
                              struct fsm_gk_verdict *gk_verdict);

void
gk_update_categorization_count(struct fsm_gk_session *fsm_gk_session);

#endif /* GATEKEEPER_H_INCLUDED */
