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
struct fsm_gk_mgr
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
struct fsm_gk_session
{
    struct fsm_session *session;
    bool initialized;
    ds_tree_node_t session_node;
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
gatekeeper_free_session(struct fsm_gk_session *gksession);


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
 * @brief perfroms check whether to allow or block this packet.
 *        by connecting ot the guard server.
 *
 * @param session the request being processed
 * @param req the policy beig processed
 * @param policy the policy being checked against
 *
 */
bool
gatekeeper_category_check(struct fsm_session *session,
                          struct fsm_policy_req *req,
                          struct fsm_policy *policy);


#endif /* GATEKEEPER_H_INCLUDED */
