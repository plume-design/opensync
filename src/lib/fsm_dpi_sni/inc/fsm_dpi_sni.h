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

#ifndef FSM_DPI_SNI_H_INCLUDED
#define FSM_DPI_SNI_H_INCLUDED

#include <jansson.h>
#include <stdint.h>
#include <time.h>

#include "fsm.h"
#include "network_metadata_report.h"
#include "os_types.h"

/**
 * @brief a session, instance of processing state and routines.
 *
 * The session provides an executing instance of the services'
 * provided by the plugin.
 * It embeds:
 * - a fsm session
 * - state information
 * - a packet parser
 */
struct fsm_dpi_sni_session
{
    struct fsm_session *session;
    bool initialized;
    time_t timestamp;
    char *included_devices;
    char *excluded_devices;
    ds_tree_node_t session_node;
};

/**
 * @brief the plugin cache, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct fsm_dpi_sni_cache
{
    bool initialized;
    ds_tree_t fsm_sessions;
};


/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm_dpi_sni session to process
 */
void
fsm_dpi_sni_plugin_periodic(struct fsm_session *session);


/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the periodic routines called by fsm.
 * @param session pointer provided by fsm
 */
int
dpi_sni_plugin_init(struct fsm_session *session);


/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm_dpi_sni session to update
 */
void
fsm_dpi_sni_plugin_update(struct fsm_session *session);


/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
fsm_dpi_sni_plugin_exit(struct fsm_session *session);


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_dpi_sni_session *
fsm_dpi_sni_lookup_session(struct fsm_session *session);


/**
 * @brief Frees a fsm_dpi_sni session
 *
 * @param n_session the fsm_dpi_sni session to delete
 */
void
fsm_dpi_sni_free_session(struct fsm_dpi_sni_session *u_session);


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the ndp session to delete
 */
void
fsm_dpi_sni_delete_session(struct fsm_session *session);


struct fsm_dpi_sni_cache *
fsm_dpi_sni_get_mgr(void);

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
                         struct net_md_stats_accumulator *acc);

bool
is_redirected_flow(struct net_md_flow_info *info, const char *attr);

#endif /* FSM_DPI_SNI_H_INCLUDED */
