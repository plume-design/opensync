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
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include "dns_cache.h"
#include "dns_parse.h"
#include "fsm_dns_utils.h"
#include "fsm_policy.h"
#include "gatekeeper_cache.h"
#include "log.h"
#include "network_metadata_report.h"
#include "os.h"

#define IP2ACTION_MIN_TTL (6*60*60)

/**
 * @brief Set wb details..
 *
 * receive wb dst and src.
 *
 * @return void.
 */
void
populate_dns_wb_cache_entry(struct ip2action_wb_info *i2a_cache_wb,
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
 */
void
populate_dns_bc_cache_entry(struct ip2action_bc_info *i2a_cache_bc,
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
 */
void
populate_dns_gk_cache_entry(struct ip2action_gk_info *i2a_cache_gk,
                            struct fsm_gk_info *fqdn_reply_gk)
{
    i2a_cache_gk->confidence_level = fqdn_reply_gk->confidence_level;
    i2a_cache_gk->category_id = fqdn_reply_gk->category_id;
    if (fqdn_reply_gk->gk_policy)
    {
        i2a_cache_gk->gk_policy = fqdn_reply_gk->gk_policy;
    }
}

static bool
add_redirect_entry_ip2action(struct dns_cache_param *param)
{
    struct ip2action_req ip_cache_req;
    struct sockaddr_storage *ipaddr;
    struct fqdn_pending_req *req;
    int rc;

    req = param->req;
    ipaddr = param->ipaddr;

    MEMZERO(ip_cache_req);
    ip_cache_req.device_mac = &req->dev_id;
    ip_cache_req.ip_addr = ipaddr;
    ip_cache_req.service_id = req->req_info->reply->service_id;
    ip_cache_req.action = FSM_ALLOW;
    ip_cache_req.redirect_flag = true;
    ip_cache_req.direction = param->direction;

    /* set required values for adding to cache */
    if (ip_cache_req.service_id == IP2ACTION_BC_SVC)
    {
        ip_cache_req.nelems = 1;
        ip_cache_req.cache_info.bc_info.reputation = 3;
        ip_cache_req.cache_info.bc_info.confidence_levels[0] = 1;
    }
    else if (ip_cache_req.service_id == IP2ACTION_WP_SVC)
    {
        ip_cache_req.cache_info.wb_info.risk_level = 5;
    }

    ip_cache_req.cache_ttl = DNS_REDIRECT_TTL;

    rc = dns_cache_add_entry(&ip_cache_req);
    if (!rc)
    {
        LOGD("%s: Couldn't add ip entry to DNS cache.", __func__);
        return false;
    }

    return true;
}

static bool
add_redirect_entry_gkc(struct dns_cache_param *param)
{
    struct gk_attr_cache_interface gkc_entry;
    struct fsm_gk_info *fqdn_reply_gk;
    struct sockaddr_storage *ipaddr;
    struct fqdn_pending_req *req;
    bool rc;

    req = param->req;
    ipaddr = param->ipaddr;

    MEMZERO(gkc_entry);
    fqdn_reply_gk = &req->req_info->reply->gk;
    gkc_entry.device_mac = &req->dev_id;
    gkc_entry.attribute_type = (ipaddr->ss_family == AF_INET ?
                                GK_CACHE_REQ_TYPE_IPV4 :
                                GK_CACHE_REQ_TYPE_IPV6);

    gkc_entry.ip_addr = ipaddr;
    gkc_entry.cache_ttl = DNS_REDIRECT_TTL;
    gkc_entry.action_by_name = FSM_ALLOW;
    gkc_entry.direction = param->direction;
    gkc_entry.gk_policy = fqdn_reply_gk->gk_policy;
    gkc_entry.redirect_flag = true;

    /* Force the category ID to an acceptable value */
    gkc_entry.category_id = 15; /* GK_NOT_RATED */
    gkc_entry.confidence_level = 0;
    gkc_entry.categorized = FSM_FQDN_CAT_SUCCESS;
    gkc_entry.is_private_ip = false;
    gkc_entry.network_id = param->network_id;

    rc = gkc_upsert_attribute_entry(&gkc_entry);
    if (!rc)
    {
        LOGD("%s: Couldn't add ip entry to gatekeeper cache for DNS.", __func__);
        return false;
    }

    return true;
}

bool
fsm_dns_cache_add_redirect_entry(struct dns_cache_param *param)
{
    struct fsm_url_request *req_info;
    bool gk_cache;
    bool ret;

    if (param->req == NULL) return false;

    req_info = param->req->req_info;
    if (req_info == NULL || req_info->reply == NULL) return false;

    LOGD("%s(): adding redirect cache entry", __func__);

    gk_cache = is_dns_cache_disabled();
    gk_cache &= (req_info->reply->service_id == IP2ACTION_GK_SVC);

    if (gk_cache)
    {
        ret = add_redirect_entry_gkc(param);
    }
    else
    {
        ret = add_redirect_entry_ip2action(param);
    }

    return ret;
}

static int
get_fsm_action(int action)
{
    int fsm_action;

    switch (action)
    {
        case FSM_REDIRECT:
            fsm_action = FSM_BLOCK;
            break;

        case FSM_OBSERVED:
            fsm_action = FSM_ALLOW;
            break;

        default:
            fsm_action = action;
            break;
    }
    return fsm_action;
}

static bool
add_entry_ip2action(struct dns_cache_param *param)
{
    struct fsm_policy_reply *policy_reply;
    struct ip2action_req ip_cache_req;
    struct fsm_url_reply *req_reply;
    struct sockaddr_storage *ipaddr;
    struct fqdn_pending_req *req;
    uint32_t ttl;
    size_t index;
    bool rc;

    req = param->req;
    req_reply = req->req_info->reply;
    policy_reply = param->policy_reply;
    ipaddr = param->ipaddr;
    ttl = param->ttl;

    MEMZERO(ip_cache_req);
    ip_cache_req.device_mac = &req->dev_id;
    ip_cache_req.ip_addr = ipaddr;
    ip_cache_req.cache_ttl = ttl;
    ip_cache_req.policy_idx = policy_reply->policy_idx;
    ip_cache_req.service_id = req_reply->service_id;
    ip_cache_req.nelems = req_reply->nelems;
    ip_cache_req.cat_unknown_to_service = policy_reply->cat_unknown_to_service;
    ip_cache_req.direction = param->direction;
    ip_cache_req.action_by_name = get_fsm_action(param->action_by_name);
    ip_cache_req.action = get_fsm_action(param->action);

    for (index = 0; index < req_reply->nelems; ++index)
    {
        ip_cache_req.categories[index] = req_reply->categories[index];
    }

    if (ip_cache_req.service_id == IP2ACTION_BC_SVC)
    {
        populate_dns_bc_cache_entry(&ip_cache_req.cache_bc,
                                    &req_reply->bc,
                                    req_reply->nelems);
    }
    else if (ip_cache_req.service_id == IP2ACTION_WP_SVC)
    {
        populate_dns_wb_cache_entry(&ip_cache_req.cache_wb,
                                    &req_reply->wb);
    }
    else if (ip_cache_req.service_id == IP2ACTION_GK_SVC)
    {
        populate_dns_gk_cache_entry(&ip_cache_req.cache_gk,
                                    &req_reply->gk);
    }
    else
    {
        LOGD("%s : service id %d no recognized", __func__,
             req_reply->service_id);
    }

    rc = dns_cache_add_entry(&ip_cache_req);
    if (rc == false)
    {
        LOGW("%s: Couldn't add ip2action entry to cache.", __func__);
    }

    return true;
}

static bool
add_entry_gkc(struct dns_cache_param *param)
{
    struct gk_attr_cache_interface gkc_entry;
    struct fsm_policy_reply *policy_reply;
    struct fsm_gk_info *fqdn_reply_gk;
    struct sockaddr_storage *ipaddr;
    char ip_str[INET6_ADDRSTRLEN];
    struct fqdn_pending_req *req;
    uint32_t ttl;
    bool rc;

    req = param->req;
    policy_reply = param->policy_reply;
    ipaddr = param->ipaddr;
    ttl = param->ttl;

    MEMZERO(gkc_entry);
    fqdn_reply_gk = &req->req_info->reply->gk;
    gkc_entry.device_mac = &req->dev_id;
    gkc_entry.attribute_type = (ipaddr->ss_family == AF_INET ?
                                GK_CACHE_REQ_TYPE_IPV4 :
                                GK_CACHE_REQ_TYPE_IPV6);
    gkc_entry.ip_addr = ipaddr;
    gkc_entry.cache_ttl = ttl;

    gkc_entry.gk_policy = fqdn_reply_gk->gk_policy;
    gkc_entry.category_id = fqdn_reply_gk->category_id;
    gkc_entry.confidence_level = fqdn_reply_gk->confidence_level;
    gkc_entry.categorized = policy_reply->categorized;
    gkc_entry.is_private_ip = policy_reply->cat_unknown_to_service;
    gkc_entry.direction = param->direction;
    gkc_entry.action_by_name = get_fsm_action(param->action_by_name);
    gkc_entry.action = get_fsm_action(param->action);
    gkc_entry.network_id = param->network_id;

    if (LOG_SEVERITY_ENABLED(LOG_SEVERITY_DEBUG))
    {
        if (ipaddr->ss_family == AF_INET)
        {
            inet_ntop(AF_INET, &((struct sockaddr_in *)ipaddr)->sin_addr, ip_str, INET_ADDRSTRLEN);
        }
        else
        {
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ipaddr)->sin6_addr, ip_str, INET6_ADDRSTRLEN);
        }
        LOGD("%s: adding %s to the gatekeeper cache", __func__, ip_str);
    }

    rc = gkc_upsert_attribute_entry(&gkc_entry);
    if (!rc)
    {
        LOGD("%s: Couldn't add ip entry to gatekeeper cache.", __func__);
    }

    return true;
}

static void
check_caching(struct dns_cache_param *param, bool *cache, bool *gk_cache)
{
    struct fsm_policy_reply *policy_reply;
    struct fqdn_pending_req *req;
    bool disabled_dns_cache;
    bool _gk_cache;
    bool _cache;

    req = param->req;
    policy_reply = param->policy_reply;

    /* Check if caching is required */
    _cache = (req != NULL);
    if (_cache) _cache &= (req->req_info != NULL);
    if (_cache) _cache &= (req->req_info->reply != NULL);
    if (_cache) _cache &= !(req->req_info->reply->connection_error);
    if (_cache) _cache &= (policy_reply->categorized == FSM_FQDN_CAT_SUCCESS);

    /* Check the cache type we'll be using */
    _gk_cache = _cache;
    if (_gk_cache) _gk_cache &= (req->req_info->reply->service_id == IP2ACTION_GK_SVC);

    /* Check if we'll be using the DNS cache */
    disabled_dns_cache = is_dns_cache_disabled();
    if (_cache) _cache &= !disabled_dns_cache;
    if (_gk_cache) _gk_cache &= disabled_dns_cache;

    *cache = _cache;
    *gk_cache = _gk_cache;
}

bool
fsm_dns_cache_add_entry(struct dns_cache_param *param)
{
    bool gk_cache;
    bool cache;

    check_caching(param, &cache, &gk_cache);

    LOGT("%s: cache: %s, gk_cache: %s", __func__,
            cache ? "true" : "false",
            gk_cache ? "true" : "false");

    /* Trigger the gatekeeper cache addition API */
    if (gk_cache)
    {
        add_entry_gkc(param);
    }
    else if (cache)
    {
        add_entry_ip2action(param);
    }

    return true;
}


bool
fsm_dns_cache_flush_ttl()
{
    bool rc;

    rc = dns_cache_ttl_cleanup();

    return rc;
}

void
fsm_dns_cache_print(struct dns_cache_param *param)
{
    bool gk_cache;
    bool cache;

    check_caching(param, &cache, &gk_cache);

    if (gk_cache)
    {
        /* Print IPv4 and IPv6 caches only */
        gkc_print_cache_parts(GK_CACHE_REQ_TYPE_IPV4);
        gkc_print_cache_parts(GK_CACHE_REQ_TYPE_IPV6);
    }
    else if (cache)
    {
        dns_cache_print_details();
        dns_cache_print();
    }
}
