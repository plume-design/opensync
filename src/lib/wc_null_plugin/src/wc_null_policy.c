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
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <regex.h>

#include "fsm.h"
#include "fsm_policy.h"
#include "log.h"
#include "wc_null_service.h"

static char *wc_null_catstr = "wc null category description";

/**
 * @brief returns a string matching the category
 *
 * @param category the category
 */
char *
fsm_wc_null_report_cat(struct fsm_session *session, int category)
{
    return wc_null_catstr;
}


/**
 * @brief initializes the web cat null service
 *
 * Initializes the wc null service once, with the parameters
 * given within the session.
 * @param session the fsm session containing the BC service config
 */
bool
fsm_wc_null_init(struct fsm_session *session)
{
    struct fsm_wc_null_mgr *mgr;

    mgr = fsm_wc_null_get_mgr();
    if (mgr->initialized) return true;

    LOGI("Initializing web cat null plugin session");

    mgr->initialized = true;
    return true;
}


/**
 * @brief check if a fqdn category matches the policy's category rule
 *
 * @param session the request being processed
 * @param req the policy beig processed
 * @param policy the policy being checked against
 *
 */
bool
fsm_wc_null_cat_check(struct fsm_session *session,
                      struct fsm_policy_req *req,
                      struct fsm_policy *policy)
{
    return false;
}


/**
 * @brief check if a fqdn risk level matches the policy's category rule
 *
 * @param session the request being processed
 * @param req the policy beig processed
 * @param policy the policy being checked against
 *
 */
bool
fsm_wc_null_risk_level_check(struct fsm_session *session,
                             struct fsm_policy_req *req,
                             struct fsm_policy *policy)
{
    return false;
}


void
fsm_wc_null_get_stats(struct fsm_session *session,
                      struct fsm_url_stats *stats)
{
    stats->cloud_lookups = 0;
    stats->cloud_hits = 0;
    stats->cache_hits = 0;
    stats->categorization_failures = 0;
    stats->uncategorized = 0;
    stats->cache_entries = 0;
    stats->cache_size = 0;
    stats->min_lookup_latency = 0;
    stats->min_lookup_latency = 0;
    stats->max_lookup_latency = 0;

    return;
}


/**
 * @brief post process a resolved fqdn request
 *
 * @param session the fsm session
 * @param req the request context
 */
void
fsm_wc_null_process_reply(struct fsm_session *session,
                          struct fqdn_pending_req *req)
{
    return;
}
