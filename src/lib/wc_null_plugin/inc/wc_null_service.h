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

#ifndef WC_NULL_SERVICE_H_INCLUDED
#define WC_NULL_SERVICE_H_INCLUDED

#include <limits.h>
#include <stdint.h>

#include "fsm.h"
#include "ds_tree.h"


/**
 * @brief the plugin manager, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct fsm_wc_null_mgr
{
    bool initialized;
    ds_tree_t fsm_sessions;
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
struct fsm_wc_null_session
{
    struct fsm_session *session;
    bool initialized;
    ds_tree_node_t session_node;
};


struct fsm_wc_null_mgr *
fsm_wc_null_get_mgr(void);


/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
fsm_wc_null_plugin_init(struct fsm_session *session);


/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
fsm_wc_null_exit(struct fsm_session *session);


/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * Sends a flow stats report.
 * @param session the fsm session
 */
void
fsm_wc_null_periodic(struct fsm_session *session);


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_wc_null_session *
fsm_wc_null_lookup_session(struct fsm_session *session);


/**
 * @brief Frees a fsm bc session
 *
 * @param wc_session the wc session to delete
 */
void
fsm_wc_null_free_session(struct fsm_wc_null_session *wc_session);


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the bc session to delete
 */
void
fsm_wc_null_delete_session(struct fsm_session *session);


/**
 * @brief initializes the web cat null service
 *
 * Initializes the wc null service once, with the parameters
 * given within the session.
 * @param session the fsm session containing the BC service config
 */
bool
fsm_wc_null_init(struct fsm_session *session);


/**
 * @brief returns a string matching the category
 *
 * @param session the fsm session
 * @param category the category
 */
char *
fsm_wc_null_report_cat(struct fsm_session *session,
                       int category);

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
                      struct fsm_policy *policy);


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
                             struct fsm_policy *policy);


/**
 * @brief post process a resolved fqdn request
 *
 * @param session the fsm session
 * @param req the request context
 */
void
fsm_wc_null_process_reply(struct fsm_session *session,
                          struct fqdn_pending_req *req);


/**
 * @brief get activity stats
 *
 * @param session the fsm session
 * @param stats the stats to fill
 */
void
fsm_wc_null_get_stats(struct fsm_session *session,
                      struct fsm_url_stats *stats);


#endif /* WC_NULL_SERVICE_H_INCLUDED */
