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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "dns_cache.h"
#include "fsm_dpi_utils.h"
#include "gatekeeper_multi_curl.h"
#include "gatekeeper_single_curl.h"
#include "gatekeeper_cache.h"
#include "gatekeeper_hero_stats.h"
#include "gatekeeper_data.h"
#include "gatekeeper_msg.h"
#include "gatekeeper_ecurl.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_update.h"
#include "osp_objm.h"
#include "wc_telemetry.h"
#include "gatekeeper.h"
#include "ds_tree.h"
#include "memutil.h"
#include "kconfig.h"
#include "schema.h"
#include "const.h"
#include "log.h"
#include "fsm_fn_trace.h"
#define GK_NOT_RATED 15
#define GK_UNRATED_TTL (60*60*24)
#define GK_MULTI_CURL_REQ_TIMEOUT 120
#define GK_REDIRECT_TTL 10

static ovsdb_table_t table_SSL;

static struct fsm_gk_mgr cache_mgr =
{
    .initialized = false,
};


/**
 * @brief returns the plugin's session manager
 *
 * @return the plugin's session manager
 */
struct fsm_gk_mgr *
gatekeeper_get_mgr(void)
{
    return &cache_mgr;
}


/* Local log interval */
#define GATEKEEPER_LOG_PERIODIC 120

static void
gatekeeper_log_health_stats(struct fsm_gk_session *fsm_gk_session,
                            struct wc_health_stats *hs)
{
    struct fsm_session *session;

    session = fsm_gk_session->session;
    LOGI("%s: session %s activity report", __func__,
         session->name);

    LOGI("%s : session %s reported lookup failures: %d", __func__,
         session->name,
         fsm_gk_session->reported_lookup_failures);

    if (fsm_gk_session->remote_lookup_retries != 0)
    {
        LOGI("gatekeeper: reported lookup retries: %d",
             fsm_gk_session->remote_lookup_retries);
    }
    LOGI("%s: total lookups: %u", __func__, hs->total_lookups);
    LOGI("%s: total cache hits: %u", __func__, hs->cache_hits);
    LOGI("%s: total remote lookups: %u", __func__, hs->remote_lookups);
    LOGI("%s: connectivity failures: %u", __func__, hs->connectivity_failures);
    LOGI("%s: cloud uncategorized responses: %u", __func__,
         hs->uncategorized);
    LOGI("%s: cache entries: [%u/%u]", __func__,
         hs->cached_entries, hs->cache_size);
    LOGI("%s: min lookup latency in ms: %u", __func__,
         hs->min_latency);
    LOGI("%s: max lookup latency in ms: %u", __func__,
         hs->max_latency);
    LOGI("%s: avg lookup latency in ms: %u", __func__,
         hs->avg_latency);
    LOGI("%s: dns cache hit count: %u", __func__,
         fsm_gk_session->dns_cache_hit_count);
    LOGI("%s: recycled LRU entries: %u", __func__,
         hs->lru_recycle_count);
}

/**
 * @brief computes health stats for gatekeeper
 *
 * @param fsm_gk_session pointer to gatekeeper session
 * @param hs pointer to health stats
 */
void
gatekeeper_report_compute_health_stats(struct fsm_gk_session *fsm_gk_session,
                                       struct wc_health_stats *hs)
{
    struct fsm_url_stats *stats;
    uint32_t dns_cache_hits;
    uint32_t dns_cache_previous_hit_count;
    uint32_t delta_count;
    uint32_t count;

    stats = &fsm_gk_session->health_stats;
    dns_cache_previous_hit_count = fsm_gk_session->dns_cache_hit_count;
    dns_cache_hits = dns_cache_get_hit_count(IP2ACTION_GK_SVC);
    delta_count = dns_cache_hits - dns_cache_previous_hit_count;
    fsm_gk_session->dns_cache_hit_count = dns_cache_hits;

    count = (uint32_t)(stats->cloud_lookups + stats->cache_hits + delta_count);
    hs->total_lookups = count;

    /* Compute cache hits */
    count = (uint32_t)(stats->cache_hits) + delta_count;
    hs->cache_hits = count;

    /* Compute remote_lookups */
    count = (uint32_t)(stats->cloud_lookups);
    hs->remote_lookups = count;

    /* Compute connectivity_failures */
    hs->connectivity_failures = fsm_gk_session->gk_offline.connection_failures;
    fsm_gk_session->gk_offline.connection_failures = 0;

    /* Compute service_failures */
    count = (uint32_t)(stats->categorization_failures);
    hs->service_failures = count;

    /* Compute uncategorized requests */
    count = (uint32_t)(stats->uncategorized);
    hs->uncategorized = count;

    /* Compute min latency */
    count = (uint32_t)(stats->min_lookup_latency);
    hs->min_latency = count;
    /* if lookup has not happened, set min latency to 0 */
    if (stats->min_lookup_latency == LONG_MAX) hs->min_latency = 0;

    /* Compute max latency */
    count = (uint32_t)(stats->max_lookup_latency);
    hs->max_latency = count;

    /* Compute average latency */
    if (stats->cloud_lookups != 0)
    {
        double avg;

        /* avg_lookup_latency is the summation of all latencies */
        avg = stats->avg_lookup_latency;
        avg /= stats->cloud_lookups;
        hs->avg_latency = (uint32_t)avg;
    }

    /* Compute cached entries */
    stats->cache_entries = gk_get_cache_count();
    count = (uint32_t)(stats->cache_entries);
    hs->cached_entries = count;

    /* Compute cache size */
    stats->cache_size = gk_cache_get_size();
    count = (uint32_t)(stats->cache_size);
    hs->cache_size = count;

    /* Compute recycled cached entries delta  */
    hs->lru_recycle_count = gk_cache_get_recycled_count();
    gk_cache_reset_recycled_count();
}

static void
gatekeeper_report_health_stats(struct fsm_gk_session *fsm_gk_session,
                               time_t now)
{
    struct fsm_url_report_stats report_stats;
    struct wc_packed_buffer *serialized;
    struct wc_observation_window ow;
    struct wc_observation_point op;
    struct wc_stats_report report;
    struct fsm_session *session;
    struct wc_health_stats hs;

    memset(&report, 0, sizeof(report));
    memset(&ow, 0, sizeof(ow));
    memset(&op, 0, sizeof(op));
    memset(&hs, 0, sizeof(hs));
    memset(&report_stats, 0, sizeof(report_stats));
    session = fsm_gk_session->session;

    /* Set observation point */
    op.location_id = session->location_id;
    op.node_id = session->node_id;

    /* set observation window */
    ow.started_at = fsm_gk_session->health_stats_report_ts;
    ow.ended_at = now;
    fsm_gk_session->health_stats_report_ts = now;

    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);

    /* Log locally */
    gatekeeper_log_health_stats(fsm_gk_session, &hs);

    /* Prepare report */
    report.provider = session->name;
    report.op = &op;
    report.ow = &ow;
    report.health_stats = &hs;

    /* Serialize report */
    serialized = wc_serialize_wc_stats_report(&report);
    if (serialized == NULL) return;

    /* Emit report */
    session->ops.send_pb_report(session, fsm_gk_session->health_stats_report_topic,
                                serialized->buf, serialized->len);

    /* Free the serialized protobuf */
    wc_free_packed_buffer(serialized);

}


/**
 * OVSDB monitor update callback for SSL table
 */
static void
callback_SSL(ovsdb_update_monitor_t *mon,
             struct schema_SSL *old_rec,
             struct schema_SSL *ssl)
{
    struct fsm_gk_mgr *mgr;
    ds_tree_t *sessions;
    struct fsm_gk_session *fsm_gk_session;
    struct gk_server_info *server_info;

    if (!ssl->certificate_exists || !ssl->private_key_exists) return;

    LOGD("%s(): reading SSL certs on update", __func__);

    mgr = gatekeeper_get_mgr();
    if (!mgr->initialized) return;

    sessions = &mgr->fsm_sessions;
    fsm_gk_session = ds_tree_head(sessions);
    while (fsm_gk_session != NULL)
    {
        server_info = &fsm_gk_session->gk_server_info;
        strncpy(server_info->ssl_cert, ssl->certificate, sizeof(server_info->ssl_cert));
        strncpy(server_info->ssl_key, ssl->private_key, sizeof(server_info->ssl_key));
        strncpy(server_info->ca_path, ssl->ca_cert, sizeof(server_info->ca_path));

        LOGD("%s(): ssl cert %s, priv key %s, ca_path: %s",
             __func__,
             server_info->ssl_cert,
             server_info->ssl_key,
             server_info->ca_path);

        fsm_gk_session = ds_tree_next(sessions, fsm_gk_session);
    }
}

/**
 * @brief frees fsm_gk_verdict structure
 *
 * @return None
 */
void
free_gk_verdict(struct fsm_gk_verdict *gk_verdict)
{
    gk_free_packed_buffer(gk_verdict->gk_pb);
    FREE(gk_verdict);
}

void
gk_update_redirect_from_cache(struct fsm_policy_req *req,
                              struct gk_attr_cache_interface *entry,
                              struct fsm_policy_reply *policy_reply)
{
    bool update;

    /* update redirect entries only for attr type FQDN, HOST and SNI */
    update = (entry->attribute_type == GK_CACHE_REQ_TYPE_FQDN);
    update |= (entry->attribute_type == GK_CACHE_REQ_TYPE_HOST);
    update |= (entry->attribute_type == GK_CACHE_REQ_TYPE_SNI);
    if (!update) return;

    /* return if fqdn_redirect is not set */
    if (entry->fqdn_redirect == NULL) return;

    /* if redirect flag is not set, return
     * without updating redirect entries
     */
    if (!entry->fqdn_redirect->redirect) return;

    policy_reply->redirect = entry->fqdn_redirect->redirect;
    policy_reply->rd_ttl   = entry->fqdn_redirect->redirect_ttl;
    if (entry->fqdn_redirect->redirect_cname)
    {
        STRSCPY(policy_reply->redirect_cname, entry->fqdn_redirect->redirect_cname);
    }

    STRSCPY(policy_reply->redirects[0], entry->fqdn_redirect->redirect_ips[0]);
    STRSCPY(policy_reply->redirects[1], entry->fqdn_redirect->redirect_ips[1]);
}

/**
 * @brief checks if the policy is present in attribute cache.
 *
 * @param req: the request being processed
 * @return true if present false otherwise
 */
static bool
gatekeeper_check_attr_cache(struct fsm_policy_req *req,
                            struct fsm_policy_reply *policy_reply)
{
    struct gk_attr_cache_interface *entry;
    struct net_md_stats_accumulator *acc;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    struct fsm_session *session;
    char *req_type_str;
    int req_type;
    bool ret;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;
    url_reply = req_info->reply;
    req_type = req->req_type;

    session = req->session;
    if (session == NULL) return false;

    entry = CALLOC(1, sizeof(*entry));
    if (entry == NULL) return false;

    entry->fqdn_redirect = CALLOC(1, sizeof(*entry->fqdn_redirect));
    if (entry->fqdn_redirect == NULL)
    {
        FREE(entry);
        return false;
    }

    acc = req->acc;
    entry->device_mac = req->device_id;
    entry->ip_addr = req->ip_addr;
    entry->attribute_type = req_type;
    entry->attr_name = req->url;
    entry->direction = (acc != NULL ? acc->direction : NET_MD_ACC_UNSET_DIR);
    entry->network_id = fsm_ops_get_network_id(req->session, req->device_id);

    ret = gkc_lookup_attribute_entry(entry, true);
    req_type_str = gatekeeper_req_type_to_str(req_type);
    LOGT("%s(): %s of type %s, is %s in cache",
         __func__,
         req->url ? req->url : "None",
         req_type_str,
         ret ? "found" : "not found");
    if (ret)
    {
        policy_reply->action = entry->action;

        policy_reply->flow_marker = entry->flow_marker;

        gk_update_redirect_from_cache(req, entry, policy_reply);

        policy_reply->categorized = entry->categorized;
        url_reply->reply_info.gk_info.category_id = entry->category_id;
        url_reply->reply_info.gk_info.confidence_level = entry->confidence_level;
        url_reply->service_id = URL_GK_SVC;
        if (entry->gk_policy != NULL)
        {
            url_reply->reply_info.gk_info.gk_policy = STRDUP(entry->gk_policy);
        }
        policy_reply->from_cache = true;
        policy_reply->cat_unknown_to_service = entry->is_private_ip;
    }

    FREE(entry->gk_policy);
    if (entry->fqdn_redirect) FREE(entry->fqdn_redirect->redirect_cname);
    FREE(entry->fqdn_redirect);
    FREE(entry);
    return ret;
}

/**
 * @brief checks if the policy is present in the IP flow cache.
 *
 * @param req: the request being processed
 * @return true if present false otherwise
 */
static bool
gatekeeper_check_ipflow_cache(struct fsm_policy_req *req,
                              struct fsm_policy_reply *policy_reply)
{
    struct gkc_ip_flow_interface *flow_entry;
    struct net_md_stats_accumulator *acc;
    struct fsm_session *session;
    struct net_md_flow_key *key;
    bool ret;

    acc = req->acc;
    if (acc == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;

    session = req->session;
    if (session == NULL) return false;

    flow_entry = CALLOC(1, sizeof(*flow_entry));
    if (flow_entry == NULL) return false;

    flow_entry->device_mac = req->device_id;
    flow_entry->direction = acc->direction;
    flow_entry->ip_version = key->ip_version;
    flow_entry->protocol = key->ipprotocol;
    flow_entry->src_ip_addr = key->src_ip;
    flow_entry->dst_ip_addr = key->dst_ip;
    flow_entry->src_port = key->sport;
    flow_entry->dst_port = key->dport;
    flow_entry->network_id = fsm_ops_get_network_id(req->session, req->device_id);

    ret = gkc_lookup_flow(flow_entry, true);
    if (ret == true)
    {
        /* value found in cache update reply */
        policy_reply->action = flow_entry->action;
        policy_reply->from_cache = true;
        policy_reply->cat_unknown_to_service = flow_entry->is_private_ip;
    }

    FREE(flow_entry);

    return ret;
}

/**
 * @brief checks if the policy is present in the attribute cache.
 *
 * @param req: the request being processed
 * @return true if present false otherwise
 */
bool
gk_check_policy_in_cache(struct fsm_policy_req *req,
                         struct fsm_policy_reply *policy_reply)
{
    struct gk_cache_mgr *cache_mgr;
    int req_type;
    bool ret = false;

    cache_mgr = gk_cache_get_mgr();
    if (!cache_mgr->initialized) return false;

    req_type = fsm_policy_get_req_type(req);
    if (req_type >= FSM_FQDN_REQ && req_type <= FSM_APP_REQ)
    {
        LOGT("%s(): checking attribute cache", __func__);
        ret = gatekeeper_check_attr_cache(req, policy_reply);
    }
    else if (req_type >= FSM_IPV4_FLOW_REQ && req_type <= FSM_IPV6_FLOW_REQ)
    {
        LOGT("%s(): checking IP Flow cache", __func__);
        ret = gatekeeper_check_ipflow_cache(req, policy_reply);
    }

    return ret;
}

/**
 * @brief Populates redirect entries for fqdn attribute cache.
 *
 * @param req: the request being processed
 * @param entry: cache entry to be updated
 * @return true if the success false otherwise
 */
void
gk_populate_redirect_entry(struct gk_attr_cache_interface *entry,
                           struct fsm_policy_req *req,
                           struct fsm_policy_reply *policy_reply)
{
    struct fqdn_redirect_s *redirect_entry;
    size_t len;

    if (policy_reply->redirect == false) return;

    entry->fqdn_redirect = CALLOC(1, sizeof(*entry->fqdn_redirect));
    if (entry->fqdn_redirect == NULL) return;
    redirect_entry = entry->fqdn_redirect;

    redirect_entry->redirect     = policy_reply->redirect;
    redirect_entry->redirect_ttl = policy_reply->rd_ttl;

    len = strlen(policy_reply->redirect_cname);
    if (len != 0)
    {
        redirect_entry->redirect_cname = STRDUP(policy_reply->redirect_cname);
        LOGT("%s(): populated redirect entries for gk cache, redirect flag %d "
             "CNAME %s",
             __func__,
             redirect_entry->redirect,
             redirect_entry->redirect_cname);
    }

    if (strlen(policy_reply->redirects[0]) != 0)
        STRSCPY(redirect_entry->redirect_ips[0], policy_reply->redirects[0]);
    if (strlen(policy_reply->redirects[1]) != 0)
        STRSCPY(redirect_entry->redirect_ips[1], policy_reply->redirects[1]);

    LOGT("%s(): populated redirect entries for gk cache, redirect flag %d "
         "IPv4 %s, IPv6 %s",
         __func__,
         redirect_entry->redirect,
         redirect_entry->redirect_ips[0],
         redirect_entry->redirect_ips[1]);
}


/**
 * @brief Populates the entries and adds to attribute cache.
 *
 * @param req: the request being processed
 * @return true if the success false otherwise
 */
static bool
gatekeeper_add_attr_cache(struct fsm_policy_req *req,
                          struct fsm_policy_reply *policy_reply)
{
    struct gk_attr_cache_interface *entry;
    struct net_md_stats_accumulator *acc;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    uint8_t direction;
    int req_type;
    bool ret;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;
    url_reply = req_info->reply;
    req_type = req->req_type;

    if (url_reply == NULL)
    {
        LOGD("%s: url_reply is NULL, not adding to attr cache", __func__);
        return false;
    }

    acc = req->acc;
    direction = (acc != NULL) ? acc->direction : NET_MD_ACC_UNSET_DIR;

    entry = CALLOC(1, sizeof(*entry));
    if (entry == NULL) return false;

    entry->action = policy_reply->action;
    entry->device_mac = req->device_id;
    entry->ip_addr = req->ip_addr;
    entry->direction = direction;
    entry->attribute_type = req_type;
    entry->cache_ttl = policy_reply->cache_ttl;
    entry->categorized = policy_reply->categorized;
    entry->attr_name = req->url;
    entry->category_id = url_reply->reply_info.gk_info.category_id;
    entry->confidence_level = url_reply->reply_info.gk_info.confidence_level;
    entry->is_private_ip = policy_reply->cat_unknown_to_service;
    entry->flow_marker = policy_reply->flow_marker;
    if (url_reply->reply_info.gk_info.gk_policy)
    {
        entry->gk_policy = url_reply->reply_info.gk_info.gk_policy;
    }

    if (req->network_id)
    {
        entry->network_id = req->network_id;
    }

    /* check if fqdn redirect entries needs to be added. */
    if (req_type == GK_CACHE_REQ_TYPE_FQDN ||
        req_type == GK_CACHE_REQ_TYPE_HOST ||
        req_type == GK_CACHE_REQ_TYPE_SNI)
    {
        gk_populate_redirect_entry(entry, req, policy_reply);
    }

    ret = gkc_add_attribute_entry(entry);
    LOGT("%s(): adding %s (attr type %d) ttl (%" PRIu64 ") to cache %s ",
         __func__,
         req->url,
         req_type,
         entry->cache_ttl,
         (ret == true) ? "success" : "failed");

    if (entry->fqdn_redirect != NULL)
        FREE(entry->fqdn_redirect->redirect_cname);
    FREE(entry->fqdn_redirect);
    FREE(entry);
    return ret;
}

/**
 * @brief Populates the entries and adds to IP Flow cache.
 *
 * @param req: the request being processed
 * @return true if the success false otherwise
 */
static bool
gatekeeper_add_ipflow_cache(struct fsm_policy_req *req,
                            struct fsm_policy_reply *policy_reply)
{
    struct gkc_ip_flow_interface *flow_entry;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    struct net_md_stats_accumulator *acc;
    struct flow_key *fkey;
    struct net_md_flow_key *key;
    bool ret;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;
    url_reply = req_info->reply;

    if (url_reply == NULL)
    {
        LOGD("%s: url_reply is NULL, not adding to IP flow cache", __func__);
        return false;
    }

    acc = req->acc;
    if (acc == NULL) return false;

    key = acc->key;
    if (key == NULL) return false;

    fkey = acc->fkey;

    LOGT("%s: adding flow src: %s, dst: %s, proto: %d, sport: %d, dport: %d to cache",
         __func__,
         fkey->src_ip, fkey->dst_ip, fkey->protocol, fkey->sport, fkey->dport);

    flow_entry = CALLOC(1, sizeof(*flow_entry));
    if (flow_entry == NULL) return false;

    flow_entry->device_mac = req->device_id;
    flow_entry->direction = acc->direction;
    flow_entry->ip_version = key->ip_version;
    flow_entry->protocol = key->ipprotocol;
    flow_entry->cache_ttl = policy_reply->cache_ttl;
    flow_entry->action = policy_reply->action;
    flow_entry->src_ip_addr = key->src_ip;
    flow_entry->dst_ip_addr = key->dst_ip;
    flow_entry->src_port = key->sport;
    flow_entry->dst_port = key->dport;
    flow_entry->category_id = url_reply->reply_info.gk_info.category_id;
    flow_entry->confidence_level = url_reply->reply_info.gk_info.confidence_level;
    flow_entry->is_private_ip = policy_reply->cat_unknown_to_service;
    if (url_reply->reply_info.gk_info.gk_policy)
    {
        flow_entry->gk_policy = url_reply->reply_info.gk_info.gk_policy;
    }

    if (req->network_id)
    {
        flow_entry->network_id = req->network_id;
    }

    ret = gkc_add_flow_entry(flow_entry);
    LOGT("%s(): adding cache %s ",
         __func__,
         (ret == true) ? "success" : "failed");

    FREE(flow_entry);

    return ret;
}

static void
gk_fsm_adjust_cache_ttl(struct fsm_policy_req *req, struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    uint32_t category_id;
    bool ret;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;
    url_reply = req_info->reply;

    if (url_reply == NULL) return;

    category_id = url_reply->reply_info.gk_info.category_id;
    if (category_id != GK_NOT_RATED) return;

    /* update TTL value only for private IPs */
    ret = is_private_ip(req->url);
    if (ret == false) return;

    LOGD("%s: setting cache ttl for %s to %d seconds", __func__,
         req->url, GK_UNRATED_TTL);

    policy_reply->cache_ttl = GK_UNRATED_TTL;
    policy_reply->cat_unknown_to_service = true;
}

/**
 * @brief Adjust redirect TTL value based on redirect
 * action.
 *
 * @param policy_reply FSM policy_reply
 * @return None
 */
static void
gk_fsm_adjust_rd_ttl(struct fsm_policy_reply *policy_reply)
{
    if (policy_reply->action == FSM_REDIRECT) policy_reply->rd_ttl = GK_REDIRECT_TTL;
    if (policy_reply->action == FSM_REDIRECT_ALLOW) policy_reply->rd_ttl = policy_reply->cache_ttl;

    LOGT("%s(): set redirect TTL value to %d", __func__, policy_reply->rd_ttl);
}

/**
 * @brief Add entry to the cache based on request type.
 *
 * @param req: the request being processed
 * @return true if the success false otherwise
 */
bool
gk_add_policy_to_cache(struct fsm_policy_req *req, struct fsm_policy_reply *policy_reply)
{
    struct gk_cache_mgr *cache_mgr;
    bool redirect_reply;
    bool ret = false;
    int req_type;

    cache_mgr = gk_cache_get_mgr();
    if (!cache_mgr->initialized) return false;

    redirect_reply = gk_is_redirect_reply(req->req_type, policy_reply->action);
    if (redirect_reply) gk_fsm_adjust_rd_ttl(policy_reply);

    /* Overwrite TTL to large value for local ip and set fqdn flag */
    gk_fsm_adjust_cache_ttl(req, policy_reply);

    req_type = fsm_policy_get_req_type(req);
    if (req_type >= FSM_FQDN_REQ && req_type <= FSM_APP_REQ)
    {
        ret = gatekeeper_add_attr_cache(req, policy_reply);
    }
    else if (req_type >= FSM_IPV4_FLOW_REQ && req_type <= FSM_IPV6_FLOW_REQ)
    {
        ret = gatekeeper_add_ipflow_cache(req, policy_reply);
    }

    return ret;
}

/**
 * @brief compute the latency indicators for a given request
 *
 * @param gk_session the session holding the latency indicators
 * @param start the clock_t value before the categorization API call
 * @param end the clock_t value before the categorization API call
 */
long
fsm_gk_update_latencies(struct fsm_gk_session *gk_session,
                        struct timespec *start, struct timespec *end)
{
    struct fsm_url_stats *stats;
    long nanoseconds;
    long latency;
    long seconds;

    stats = &gk_session->health_stats;

    seconds = end->tv_sec - start->tv_sec;
    nanoseconds = end->tv_nsec - start->tv_nsec;

    /* Compute latency in nanoseconds */
    latency = (seconds * 1000000000L) + nanoseconds;

    /* Translate in milliseconds */
    latency /= 1000000L;

    stats->avg_lookup_latency += latency;

    if (latency < stats->min_lookup_latency)
    {
        stats->min_lookup_latency = latency;
    }

    if (latency > stats->max_lookup_latency)
    {
        stats->max_lookup_latency = latency;
    }

    return latency;
}

void
gk_purge_mcurl_data(struct fsm_gk_session* gk_session)
{
    struct gk_mcurl_data* mcurl_data;
    struct gk_mcurl_data* to_remove;
    time_t now;
    double cmp;

    LOGT("%s(): removing expired mcurl data", __func__);

    now = time(NULL);
    mcurl_data = ds_tree_head(&gk_session->mcurl_data_tree);
    while (mcurl_data)
    {
        /* get the time difference */
        cmp = difftime(now, mcurl_data->timestamp);

        /* continue if current request is not yet timed out */
        if (cmp < GK_MULTI_CURL_REQ_TIMEOUT)
        {
            mcurl_data = ds_tree_next(&gk_session->mcurl_data_tree, mcurl_data);
            continue;
        }

        /* request is timed out, need to be removed */
        LOGT("%s() removing request from tree: type %d, id %d policy_req == %p",
             __func__,
             mcurl_data->req_type,
             mcurl_data->req_id,
             mcurl_data->gk_verdict->policy_req);
        to_remove = mcurl_data;
        mcurl_data = ds_tree_next(&gk_session->mcurl_data_tree, mcurl_data);

        /* remove current node from the tree */
        ds_tree_remove(&gk_session->mcurl_data_tree, to_remove);
        /* free up the memory */
        free_mcurl_data(to_remove);
    }
}

/**
 * @brief adds the request details to mcurl_data_tree
 *
 * @param session fsm_session pointer
 * @param req pointer to fsm policy request
 *        information, proto buffer and fsm_policy_req
 * returns the added object if successful else returns NULL
 */
static struct gk_mcurl_data *
gk_add_mcurl_data(struct fsm_session *session,
                  struct fsm_policy_req *policy_req,
                  struct fsm_policy_reply *policy_reply)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_gk_verdict *gk_verdict;
    struct gk_mcurl_data *mcurl_data;

    fsm_gk_session = gatekeeper_lookup_session(session->service);
    if (fsm_gk_session == NULL) return NULL;

    mcurl_data = CALLOC(1, sizeof(*mcurl_data));
    if (mcurl_data == NULL) return NULL;

    gk_verdict = CALLOC(1, sizeof(*gk_verdict));
    if (gk_verdict == NULL) goto error;

    gk_verdict->policy_req = policy_req;
    gk_verdict->policy_reply = policy_reply;
    gk_verdict->gk_session_context = fsm_gk_session;
    gk_verdict->gk_pb = gatekeeper_get_req(session, policy_req, mcurl_data);
    if (gk_verdict->gk_pb == NULL)
    {
        LOGE("%s(): failed to serialize curl data", __func__);
        goto error;
    }

    mcurl_data->timestamp = time(NULL);
    clock_gettime(CLOCK_REALTIME, &mcurl_data->req_time);
    mcurl_data->req_type   = policy_req->req_type;
    mcurl_data->gk_verdict = gk_verdict;
    LOGT("%s(): added curl data for request type: %d, with id %d, gk_verdict: %p, policy_req: %p, policy reply:%p",
         __func__,
         mcurl_data->req_type,
         mcurl_data->req_id,
         mcurl_data->gk_verdict,
         gk_verdict->policy_req,
         gk_verdict->policy_reply);

    ds_tree_insert(&fsm_gk_session->mcurl_data_tree, mcurl_data, mcurl_data);
    return mcurl_data;

error:
    FREE(mcurl_data);
    FREE(gk_verdict);
    return NULL;
}

bool
gk_process_using_multi_curl(struct fsm_policy_req *policy_req,
                            struct fsm_policy_reply *policy_reply)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_mcurl_data *mcurl_data;
    struct fsm_session *session;

    session = policy_req->session;


    fsm_gk_session = gatekeeper_lookup_session(session->service);
    if (fsm_gk_session == NULL) return false;

    mcurl_data = gk_add_mcurl_data(session, policy_req, policy_reply);
    if (mcurl_data == NULL) return false;
    LOGT("%s(): returning mcurl_data == %p, mcurl_data->gk_verdict == %p", __func__, mcurl_data, mcurl_data->gk_verdict);

    gk_send_mcurl_request(fsm_gk_session, mcurl_data);
    return true;
}

bool
gk_lookup_using_multi_curl(struct fsm_policy_req *req)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_session *session;

    int req_type;
    session = req->session;

    fsm_gk_session = gatekeeper_lookup_session(session->service);
    if (fsm_gk_session ==  NULL)
    {
        LOGD("%s(): gatekeeper session lookup failed", __func__);
        return false;
    }

    if (fsm_gk_session->enable_multi_curl == false) return false;

    req_type = fsm_policy_get_req_type(req);
    /* do not perform mcurl request for fqdns */
    if (req_type == FSM_FQDN_REQ) return false;

    return true;
}

/**
 * @brief performs check whether to allow or block this packet.
 *        by connecting ot the guard server.
 *
 * @param session the fsm session
 * @req: the request being processed
 * @policy: the policy being checked against
 * @return true if the success false otherwise
 */
bool
gatekeeper_get_verdict(struct fsm_policy_req *req,
                       struct fsm_policy_reply *policy_reply)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_server_info *server_info;
    struct gatekeeper_offline *offline;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_gk_verdict *gk_verdict;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    struct fsm_url_stats *stats;
    struct fsm_session *session;
    struct timespec start;
    struct timespec end;
    long lookup_latency;
    bool use_mcurl;
    bool ret = true;
    bool incache;
    int gk_response;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;
    session = req->session;

    LOGT("%s(): performing gatekeeper policy check", __func__);

    url_reply = CALLOC(1, sizeof(*url_reply));
    if (url_reply == NULL) return false;

    req_info->reply = url_reply;

    url_reply->service_id = URL_GK_SVC;

    fsm_gk_session = gatekeeper_lookup_session(session->service);
    if (fsm_gk_session ==  NULL)
    {
        LOGD("%s(): gatekeeper session lookup failed", __func__);
        return false;
    }

    stats = &fsm_gk_session->health_stats;
    offline = &fsm_gk_session->gk_offline;

    incache = gk_check_policy_in_cache(req, policy_reply);
    if (incache == true)
    {
        if (!policy_reply->cat_unknown_to_service)
            stats->cache_hits++;
        LOGT("%s(): %s found in cache, returning action %s (mark:%d), redirect flag %d from cache",
             __func__,
             req->url,
             fsm_policy_get_action_str(policy_reply->action),
             policy_reply->flow_marker,
             policy_reply->redirect);
        return true;
    }

    if (offline->provider_offline)
    {
        time_t now = time(NULL);
        bool backoff;

        backoff = ((now - offline->offline_ts) < offline->check_offline);

        if (backoff) return false;
        offline->provider_offline = false;
    }

    server_info = &fsm_gk_session->gk_server_info;
    if (!server_info->server_url) return false;

    gk_verdict = CALLOC(1, sizeof(*gk_verdict));
    if (gk_verdict == NULL) return false;

    gk_verdict->policy_req = req;
    gk_verdict->policy_reply = policy_reply;
    gk_verdict->gk_session_context = fsm_gk_session;

    LOGT("%s: url:%s path:%s", __func__, server_info->server_url, server_info->ca_path);

    gk_verdict->gk_pb = gatekeeper_get_req(session, req, NULL);
    if (gk_verdict->gk_pb == NULL)
    {
        LOGD("%s() curl request serialization failed", __func__);
        ret = false;
        goto error;
    }

    policy_reply->categorized = FSM_FQDN_CAT_SUCCESS;
    use_mcurl = gk_lookup_using_multi_curl(req);

    if (use_mcurl == true)
    {
        LOGT("%s(): processing request using multi curl", __func__);
        gk_process_using_multi_curl(req, policy_reply);
        /* set reply type to async, as request is processed using multi curl */
        policy_reply->reply_type = FSM_ASYNC_REPLY;
        ret = true;
        goto error;
    }
    else
    {
        LOGT("%s(): processing request using easy curl", __func__);
        memset(&start, 0, sizeof(start));
        memset(&end, 0, sizeof(end));

        clock_gettime(CLOCK_REALTIME, &start);
        policy_reply->reply_type = FSM_INLINE_REPLY;
        fsm_fn_trace(gk_gatekeeper_lookup, FSM_FN_ENTER);
        gk_response = gk_gatekeeper_lookup(session, fsm_gk_session, gk_verdict, policy_reply);
        fsm_fn_trace(gk_gatekeeper_lookup, FSM_FN_EXIT);
        if (gk_response != GK_LOOKUP_SUCCESS)
        {
            policy_reply->categorized = FSM_FQDN_CAT_FAILED;

            /* start backoff timer for connection or service errors */
            if (gk_response == GK_CONNECTION_ERROR || gk_response == GK_SERVICE_ERROR)
            {
                offline->provider_offline = true;
                offline->offline_ts = time(NULL);
            }

            /* increment connection failure count for connection errors */
            if (gk_response == GK_CONNECTION_ERROR) offline->connection_failures++;

            LOGD("%s() curl error not updating cache", __func__);
            ret = false;
            goto error;
        }
        clock_gettime(CLOCK_REALTIME, &end);

        /* update stats for processing the request */
        lookup_latency = fsm_gk_update_latencies(fsm_gk_session, &start, &end);
        LOGT("%s(): cloud lookup latency for '%s' is %ld ms", __func__, req->url, lookup_latency);
    }

    gk_add_policy_to_cache(req, policy_reply);

error:
    LOGT("%s(): verdict for '%s' is %s", __func__, req->url,
         fsm_policy_get_action_str(policy_reply->action));
    free_gk_verdict(gk_verdict);


    return ret;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions match
 */
static int
fsm_gk_session_cmp(const void *a, const void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

static int
gk_mcurl_cmp(const void *a, const void *b)
{
    const struct gk_mcurl_data *ta = a;
    const struct gk_mcurl_data *tb = b;

    int cmp = ta->req_id - tb->req_id;
    if (cmp) return cmp;

    cmp = ta->req_type - tb->req_type;
    return cmp;
}

/**
 * @brief initializes gate keeper plugin
 *
 * Initializes the gate keeper plugin once, with the parameters
 * given within the session.
 * @param session the fsm session containing the BC service config
 */
bool
gatekeeper_init(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;

    mgr = gatekeeper_get_mgr();
    if (mgr->initialized) return true;

    memset(&mgr->req_ids, 0, sizeof(mgr->req_ids));
    mgr->getaddrinfo = getaddrinfo;
    mgr->initialized = true;
    return true;
}

/**
 * @brief initializes curl handler
 *
 * @param session the fsm session containing the gatekeeper service
 */
bool
gatekeeper_init_curl(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_server_info *server_info;
    struct fsm_gk_mgr *gk_mgr;
    const char *cert_path;

    fsm_gk_session = (struct fsm_gk_session *)session->handler_ctxt;
    if (!fsm_gk_session) return false;

    gk_mgr = gatekeeper_get_mgr();
    if (!gk_mgr->initialized) return false;

    LOGI("%s(): initializing curl", __func__);

    server_info = &fsm_gk_session->gk_server_info;

    memset(server_info, 0, sizeof(*server_info));
    server_info->server_url = session->ops.get_config(session, "gk_url");

    cert_path = session->ops.get_config(session, "cacert");
    if (cert_path)
    {
        server_info->cert_path = session->ops.get_config(session, "cacert");
    }

    ds_tree_init(&fsm_gk_session->mcurl_data_tree,
                    gk_mcurl_cmp,
                    struct gk_mcurl_data,
                    mcurl_req_node);

    if (fsm_gk_session->enable_multi_curl)
    {
        LOGI("%s(): initializing multi curl", __func__);
        gk_multi_curl_init(&fsm_gk_session->mcurl, session->loop);
    }
    else
    {
        LOGT("%s(): initializing single curl..", __func__);
        gk_curl_easy_init(&fsm_gk_session->ecurl);
    }

    return true;
}

void
gatekeeper_monitor_ssl_table(void)
{
    LOGI("%s(): monitoring SSL table", __func__);
    OVSDB_TABLE_INIT_NO_KEY(SSL);
    OVSDB_TABLE_MONITOR_F(SSL, ((char*[]){"ca_cert", "certificate", "private_key", NULL}));
}

void
gatekeeper_unmonitor_ssl_table(void)
{
    LOGI("%s(): unmonitoring SSL table", __func__);

    /* Deregister monitor events */
    ovsdb_table_fini(&table_SSL);
}

static int
gatekeeper_load_cache_seed(struct fsm_session *session,
                           struct fsm_object *object)
{
    const char * const path = "gatekeeper_cache_seed.bin";
    char cache_seed_file[PATH_MAX+128];
    char store[PATH_MAX] = { 0 };
    struct gk_packed_buffer pb;
    struct fsm_object stale;
    struct fsm_gk_mgr *mgr;
    void *buf = NULL;
    ssize_t size = 0;
    struct stat sb;
    bool ret;
    int fd;
    int rc;

    mgr = gatekeeper_get_mgr();

    ret = IS_NULL_PTR(mgr->cache_seed_version);
    /* Bail if the cache seed to add is already the active cache seed */
    if (!ret)
    {
        rc = strcmp(mgr->cache_seed_version, object->version);
        if (!rc)
        {
            LOGI("%s: cache seed version already loaded: %s", __func__,
                 object->version);
            return 0;
        }
    }

    rc = -1;
    object->state = FSM_OBJ_LOAD_FAILED;
    ret = osp_objm_path(store, sizeof(store),
                        object->object, object->version);
    if (!ret) goto err;

    LOGI("%s: retrieved store path: %s", __func__, store);
    snprintf(cache_seed_file, sizeof(cache_seed_file), "%s/%s",
             store, path);
    fd = open(cache_seed_file, O_RDONLY);
    if (fd == -1)
    {
        LOGE("%s: failed to open %s", __func__, cache_seed_file);
    }
    fstat(fd, &sb);
    if (sb.st_size == 0)
    {
        LOGE("%s: empty file %s", __func__, cache_seed_file);
        close(fd);
        goto err;
    }

    buf = MALLOC(sb.st_size);
    size = read(fd, buf, sb.st_size);
    if (size != sb.st_size)
    {
        LOGE("%s: failed to read %s", __func__, cache_seed_file);
        close(fd);
        goto err;
    }
    close(fd);

    LOGT("%s:Restoring Cache entries", __func__);
    pb.buf = buf;
    pb.len = sb.st_size;

    gk_restore_cache_from_buffer(&pb);
    gk_cache_sync_location_entries();

    LOGT("dumping cache entries after seeding");
    gkc_print_cache_entries();

    LOGI("%s: loaded cache seed version: %s", __func__, object->version);
    object->state = FSM_OBJ_ACTIVE;

    ret = IS_NULL_PTR(mgr->cache_seed_version);
    if (!ret)
    {
        stale.object = object->object;
        stale.version = mgr->cache_seed_version;
        stale.state = FSM_OBJ_OBSOLETE;
        session->ops.state_cb(session, &stale);
        FREE(mgr->cache_seed_version);
    }
    mgr->cache_seed_version = STRDUP(object->version);
    LOGI("%s: %s: cache seed version is now %s", __func__, object->object,
         mgr->cache_seed_version == NULL ? "None" : mgr->cache_seed_version);
    rc = 0;

err:
    session->ops.state_cb(session, object);
    FREE(buf);

    return rc;
}


static void
gatekeeper_seed_load_best(struct fsm_session *session)
{
    struct fsm_object *object;
    int cnt;
    int rc;

    /* Put a hard limit to the loop */
    cnt = 4;
    do
    {
        /* Retrieve the best version of the cache seed */
        object = session->ops.best_obj_cb(session, "gatekeeper_cache_seed");
        if (object == NULL) return;

        /* Apply the best cache seed */
        rc = gatekeeper_load_cache_seed(session, object);
        if (!rc)
        {
            LOGI("%s: installing %s version %s succeeded", __func__,
                 object->object, object->version);
            FREE(object);
        }
        cnt--;
    } while ((rc != 0) && (cnt != 0));
}


static void
gatekeeper_seed_store_update(struct fsm_session *session,
                             struct fsm_object *object,
                             int ovsdb_event)
{
    int rc;

    switch(ovsdb_event)
    {
        case OVSDB_UPDATE_NEW:
            LOGI("%s: gatekeeer cache seed version %s",
                 __func__, object->version);
            rc = gatekeeper_load_cache_seed(session, object);
            if (rc != 0) gatekeeper_seed_load_best(session);
            break;

        case OVSDB_UPDATE_DEL:
            break;

        case OVSDB_UPDATE_MODIFY:
            break;

        default:
            return;
    }
}


static const char pattern_fqdn[] =
    "^(([a-zA-Z0-9_]|[a-zA-Z0-9_][a-zA-Z0-9_-]*[a-zA-Z0-9])\\.){1,}"
    "([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9_-]*[a-zA-Z0-9])$";

static const char pattern_fqdn_lan[] =
    "^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9_-]*[a-zA-Z0-9])\\.){1,}"
    "(lan)$";

/**
 * @brief initialized gatekeeper plugin module
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
gatekeeper_module_init(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_policy_client *fsm_client;
    struct fsm_web_cat_ops *cat_ops;
    struct fsm_gk_mgr *mgr;
    time_t now;
    int rc;

    if (session == NULL) return -1;

    mgr = gatekeeper_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        char *lru_size_str;
        size_t lru_size;
        bool ret;

        ret = gatekeeper_init(session);
        if (!ret) return 0;

        lru_size_str = session->ops.get_config(session, "lru_size");
        if (lru_size_str != NULL)
        {
            lru_size = strtoul(lru_size_str, NULL, 0);
        }
        else
        {
            lru_size = CONFIG_GATEKEEPER_CACHE_LRU_SIZE;
        }

        gk_cache_init(lru_size);

        ds_tree_init(&mgr->fsm_sessions, fsm_gk_session_cmp,
                     struct fsm_gk_session, session_node);
        mgr->gk_time = time(NULL);
        mgr->last_persistence_store = time(NULL);
        mgr->initialized = true;
    }

    /* Look up the fsm gatekeeper session */
    fsm_gk_session = gatekeeper_lookup_session(session);
    if (fsm_gk_session == NULL)
    {
        LOGE("%s: could not allocate gate keeper plugin", __func__);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (fsm_gk_session->initialized) return 0;

    /* enable or disable multi curl support */
    fsm_gk_session->enable_multi_curl = 0;
    fsm_gk_session->enable_multi_curl = kconfig_enabled(CONFIG_GATEKEEPER_MULTI_CURL);

    /* Set the fsm session */
    session->ops.periodic = gatekeeper_periodic;
    session->ops.update = gatekeeper_update;
    session->ops.object_cb = gatekeeper_seed_store_update;
    session->ops.exit = gatekeeper_exit;
    session->handler_ctxt = fsm_gk_session;

    fsm_gk_session->session = session;
    gatekeeper_update(session);

    gatekeeper_init_curl(session);

    /* Set the plugin specific ops */
    cat_ops = &session->p_ops->web_cat_ops;
    cat_ops->gatekeeper_req = gatekeeper_get_verdict;

    now = time(NULL);
    fsm_gk_session->health_stats_report_ts = now;
    fsm_gk_session->hero_stats_report_ts = now;

    fsm_gk_session->gk_offline.check_offline = 30;
    fsm_gk_session->gk_offline.provider_offline = false;
    fsm_gk_session->cname_offline.check_offline = 30;
    fsm_gk_session->cname_offline.cname_offline = false;

    /* initialize latency counter */
    fsm_gk_session->health_stats.min_lookup_latency = LONG_MAX;

    /* Prepare the regular expressions used to validate domain names */
    fsm_gk_session->re = CALLOC(1, sizeof(*fsm_gk_session->re));
    if (fsm_gk_session->re == NULL)
    {
        LOGE("%s: could not allocate regular expression", __func__);
        goto err;
    }

    rc = regcomp(fsm_gk_session->re, pattern_fqdn, REG_EXTENDED);
    if (rc != 0)
    {
        LOGE("%s: regcomp(%s) failed, reason (%d)\n", __func__,
             pattern_fqdn, rc);
        goto err;
    }

    fsm_gk_session->pattern_fqdn = pattern_fqdn;

    fsm_gk_session->re_lan = CALLOC(1, sizeof(*fsm_gk_session->re_lan));
    if (fsm_gk_session->re_lan == NULL)
    {
        LOGE("%s: could not allocate regular expression", __func__);
        goto err;
    }

    rc = regcomp(fsm_gk_session->re_lan, pattern_fqdn_lan, REG_EXTENDED);
    if (rc != 0)
    {
        LOGE("%s: regcomp(%s) failed, reason (%d)\n", __func__,
             pattern_fqdn_lan, rc);
        goto err;
    }
    fsm_gk_session->pattern_fqdn_lan = pattern_fqdn_lan;

    /* Initialize dns cache hit count */
    fsm_gk_session->dns_cache_hit_count = 0;

    /* register our cache flush client */
    fsm_client = &fsm_gk_session->cache_flush_client;
    fsm_client->session = session;
    fsm_client->name = STRDUP("clear_gatekeeper_cache");
    fsm_client->flush_cache = gkc_flush_client;
    fsm_policy_register_client(fsm_client);

    /* Setup hero cache */
    fsm_gk_session->hero_stats = gkhc_get_aggregator();
    if (!fsm_gk_session->hero_stats->initialized)
        gkhc_init_aggregator(fsm_gk_session->hero_stats, session);
    gkhc_activate_window(fsm_gk_session->hero_stats);

    /* Register cache seed object monitoring */
    session->ops.monitor_object(session, "gatekeeper_cache_seed");

    fsm_gk_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);
    LOGT("%s: session %s is multi-curl %s", __func__, session->name,
         fsm_gk_session->enable_multi_curl ? "enabled" : "disabled");

    FSM_FN_MAP(gk_gatekeeper_lookup);

    /* Load best cache seed version */
    gatekeeper_seed_load_best(session);

    return 0;

err:
    gatekeeper_exit(session);
    return -1;
}

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
gatekeeper_plugin_init(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;
    bool enable_monitor = false;
    int ret;

    if (session == NULL) return -1;

    mgr = gatekeeper_get_mgr();

    if (!mgr->initialized)
    {
        enable_monitor = true;
    }

    ret = gatekeeper_module_init(session);
    if (ret == -1)
    {
        LOGI("%s(): failed to initialize gatekeeper module", __func__);
        return ret;
    }

    if (enable_monitor)
    {
        gatekeeper_monitor_ssl_table();
    }

    return ret;
}


/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
gatekeeper_exit(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_policy_client *fsm_client;
    struct fsm_gk_mgr *mgr;

    mgr = gatekeeper_get_mgr();
    if (!mgr->initialized) return;

    fsm_gk_session = (struct fsm_gk_session *)session->handler_ctxt;
    if (!fsm_gk_session) return;

    fsm_client = &fsm_gk_session->cache_flush_client;
    fsm_policy_deregister_client(fsm_client);

    if (fsm_gk_session->re)
    {
        regfree(fsm_gk_session->re);
        FREE(fsm_gk_session->re);
    }

    if (fsm_gk_session->re_lan)
    {
        regfree(fsm_gk_session->re_lan);
        FREE(fsm_gk_session->re_lan);
    }

    gk_curl_easy_cleanup(&fsm_gk_session->ecurl);
    gk_curl_multi_cleanup(&fsm_gk_session->mcurl);
    mgr->initialized = false;
    gatekeeper_unmonitor_ssl_table();

    gatekeeper_delete_session(session);
}

/**
 * @brief close curl connection if it is ideal for more then
 *        set GK_CURL_TIMEOUT value. When curl request
 *        is made, the ecurl_connection_time value is updated.
 *
 * @param ctime current time value
 */
void
gk_curl_easy_timeout(struct fsm_gk_session *fsm_gk_session, time_t ctime)
{
    struct gk_curl_easy_info *curl_info;
    double time_diff;

    curl_info = &fsm_gk_session->ecurl;

    if (curl_info->ecurl_connection_active == false) return;

    time_diff = ctime - curl_info->ecurl_connection_time;

    if (time_diff < GK_CURL_TIMEOUT) return;

    LOGD("%s(): closing curl connection.", __func__);
    gk_curl_easy_cleanup(&fsm_gk_session->ecurl);
}

void
gk_multi_curl_timeout(struct fsm_gk_session *fsm_gk_session, time_t ctime)
{
    struct gk_curl_multi_info *mcurl_info;
    double time_diff;

    mcurl_info = &fsm_gk_session->mcurl;

    if (mcurl_info->mcurl_connection_active == false) return;

    time_diff  = ctime - mcurl_info->mcurl_connection_time;

    if (time_diff < GK_CURL_TIMEOUT) return;

    LOGD("%s(): closing multi curl connection due to inactive timeout", __func__);

    gk_curl_multi_cleanup(&fsm_gk_session->mcurl);
}

/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * Sends a flow stats report.
 * @param session the fsm session
 */
void
gatekeeper_periodic(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_url_stats *stats;
    int num_hero_stats_records;
    struct fsm_gk_mgr *mgr;
    double cmp_report;
    bool get_stats;
    time_t now;

    fsm_gk_session = (struct fsm_gk_session *)session->handler_ctxt;
    if (!fsm_gk_session) return;

    mgr = gatekeeper_get_mgr();
    if (!mgr->initialized) return;

    now = time(NULL);

    /* Check if the time has come to report the stats through mqtt */
    /* First health stats */
    cmp_report = now - fsm_gk_session->health_stats_report_ts;
    get_stats = (cmp_report >= fsm_gk_session->health_stats_report_interval);
    if (get_stats)
    {
        /* Report to mqtt */
        gatekeeper_report_health_stats(fsm_gk_session, now);

        /* Reset stat counters */
        stats = &fsm_gk_session->health_stats;
        memset(stats, 0, sizeof(*stats));
        stats->min_lookup_latency = LONG_MAX;
    }

    /* Now check for hero stats */
    num_hero_stats_records = gkhc_send_report(session,
                                              fsm_gk_session->hero_stats_report_interval);
    if (num_hero_stats_records > 0)
        LOGT("%s: Reported into %d hero_stats records",
             __func__, num_hero_stats_records);
    else if (num_hero_stats_records < 0)
        LOGD("%s: Failed to report hero_stats", __func__);

    /* Store cache in persistence every 24 hours */

    if ((now - mgr->last_persistence_store) >= GK_CACHE_PERSISTENCE_INTERVAL)
    {
        LOGD("storing cache in persistence !!");
        gk_store_cache_in_persistence();
        mgr->last_persistence_store = now;
    }

    /* Proceed to other periodic tasks */
    if ((now - mgr->gk_time) < GK_PERIODIC_INTERVAL) return;
    mgr->gk_time = now;

    gk_curl_easy_timeout(fsm_gk_session, now);

    gk_multi_curl_timeout(fsm_gk_session, now);

    /* remove expired entries from cache */
    gkc_ttl_cleanup();

    /* remove timed-out curl requests */
    gk_purge_mcurl_data(fsm_gk_session);
}

#define GATEKEEPER_REPORT_STATS_INTERVAL (60*10)

void
gatekeeper_update(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_server_info *server_info;
    char *hs_report_interval;
    char *hs_report_topic;
    char *mcurl_config;
    long interval;
    int val;

    LOGT("%s(): gatekeeper configuration udated, reading new config", __func__);

    fsm_gk_session = (struct fsm_gk_session *)session->handler_ctxt;
    if (!fsm_gk_session) return;

    server_info = &fsm_gk_session->gk_server_info;

    mcurl_config = session->ops.get_config(session, "multi_curl");

    if (mcurl_config != NULL)
    {
        LOGT("%s(): session %p: multi_curl key value: %s",
             __func__, session, mcurl_config);
        val = strcmp(mcurl_config, "enable");
        if (val == 0)
        {
            fsm_gk_session->enable_multi_curl = true;
        }
        else
        {
            val = strcmp(mcurl_config, "disable");
            if (val == 0)
            {
                fsm_gk_session->enable_multi_curl = false;
            }
        }
    }
    else
    {
        LOGT("%s(): session %p could not find multi_curl key in other_config",
             __func__, session);
        fsm_gk_session->enable_multi_curl = false;
    }

    /* currently overwriting ovsdb config with kconfig value */
    fsm_gk_session->enable_multi_curl = kconfig_enabled(CONFIG_GATEKEEPER_MULTI_CURL);

    server_info->server_url = session->ops.get_config(session, "gk_url");

    /* Health stats configuration */
    fsm_gk_session->health_stats_report_interval = (long)GATEKEEPER_REPORT_STATS_INTERVAL;
    hs_report_interval = session->ops.get_config(session,
                                                 "wc_health_stats_interval_secs");
    if (hs_report_interval != NULL)
    {
        interval = strtoul(hs_report_interval, NULL, 10);
        fsm_gk_session->health_stats_report_interval = (long)interval;
    }
    hs_report_topic = session->ops.get_config(session,
                                              "wc_health_stats_topic");
    fsm_gk_session->health_stats_report_topic = hs_report_topic;

    /* Hero stats configuration */
    fsm_gk_session->hero_stats_report_interval = (long)GATEKEEPER_REPORT_STATS_INTERVAL;
    hs_report_interval = session->ops.get_config(session,
                                                 "wc_hero_stats_interval_secs");
    if (hs_report_interval != NULL)
    {
        interval = strtoul(hs_report_interval, NULL, 10);
        fsm_gk_session->hero_stats_report_interval = (long)interval;
    }
    hs_report_topic = session->ops.get_config(session,
                                              "wc_hero_stats_topic");
    fsm_gk_session->hero_stats_report_topic = hs_report_topic;

    /* As long as the GK cache is not persisted, there are no chance a flush
     * rule will have any impact at startup.
     */

    LOGT("%s: session %s is multi-curl %s", __func__, session->name,
         fsm_gk_session->enable_multi_curl ? "enabled" : "disabled");
}

/**
 * @brief frees memory used by mcurl data
 *
 * @param mcurl_request pointer to mcurl_request
 */
void
free_mcurl_data(struct gk_mcurl_data *mcurl_request)
{
    struct fsm_gk_verdict *gk_verdict;

    if (mcurl_request == NULL) return;

    if (mcurl_request->gk_verdict)
    {
        gk_verdict = mcurl_request->gk_verdict;

        if (gk_verdict->gk_pb) gk_free_packed_buffer(gk_verdict->gk_pb);

        FREE(gk_verdict);
    }

    FREE(mcurl_request);
}

/**
 * @brief frees memory used by mcurl_data_tree
 *
 * @param tree pointer to mcurl_data_tree
 */
void
gk_clean_mcurl_tree(ds_tree_t *tree)
{
    struct gk_mcurl_data *to_remove;
    struct gk_mcurl_data *current;

    if (tree == NULL) return;

    current = ds_tree_head(tree);
    while (current != NULL)
    {
        to_remove = current;
        current = ds_tree_next(tree, current);
        ds_tree_remove(tree, to_remove);
        free_mcurl_data(to_remove);
    }
}

/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_gk_session *
gatekeeper_lookup_session(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;
    struct fsm_gk_session *gk_session;
    ds_tree_t *sessions;

    mgr = gatekeeper_get_mgr();
    sessions = &mgr->fsm_sessions;

    gk_session = ds_tree_find(sessions, session);
    if (gk_session != NULL) return gk_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    gk_session = CALLOC(1, sizeof(*gk_session));
    if (gk_session == NULL) return NULL;

    ds_tree_insert(sessions, gk_session, session);

    return gk_session;
}


/**
 * @brief Frees a gate keeper session
 *
 * @param wc_session the gate keeper session to delete
 */
void
gatekeeper_free_session(struct fsm_gk_session *gk_session)
{
    FREE(gk_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the wc session to delete
 */
void
gatekeeper_delete_session(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;
    struct fsm_gk_session *gk_session;
    ds_tree_t *sessions;

    mgr = gatekeeper_get_mgr();
    sessions = &mgr->fsm_sessions;

    gk_session = ds_tree_find(sessions, session);
    if (gk_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, gk_session);
    gk_clean_mcurl_tree(&gk_session->mcurl_data_tree);
    gatekeeper_free_session(gk_session);
}

