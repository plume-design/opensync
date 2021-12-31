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

#ifndef FSM_DPI_CLIENT_PLUGIN_H_INCLUDED
#define FSM_DPI_CLIENT_PLUGIN_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "ds_tree.h"
#include "fsm.h"
#include "network_metadata_report.h"

/**
 * @brief a session, instance of processing state and routines.
 *
 * The session provides an executing instance of the services
 * provided by the plugin.
 * It embeds:
 * - a fsm session
 * - state information
 * - a packet parser
 */
struct fsm_dpi_client_session
{
    struct fsm_session *session;
    bool initialized;
    time_t timestamp;
    time_t ttl;
    char *included_devices;
    char *excluded_devices;
    void *private_session;       /* to be cast into the client's correct context struct */
    ds_tree_node_t session_node;
};

/**
 * @brief the plugin cache, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct fsm_dpi_client_cache
{
    bool initialized;
    ds_tree_t fsm_sessions;
};

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the periodic routines called by fsm.
 * @param session pointer provided by fsm
 */
int fsm_dpi_client_init(struct fsm_session *session);

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void fsm_dpi_client_exit(struct fsm_session *session);

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm_dpi_client session to update
 */
void fsm_dpi_client_update(struct fsm_session *session);

/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm_dpi_client session to process
 */
void fsm_dpi_client_periodic(struct fsm_session *session);

/**
 * @brief set the ttl for the periodic function call
 *
 * @param session the fsm session keying the fsm_dpi_client session to process
 */
void fsm_dpi_client_set_ttl(struct fsm_session *session, time_t t);

/**
 * @brief check whether the ttl has expired for the periodic routine call
 *
 * @param session the fsm session keying the fsm_dpi_client session to process
 * @return true if the periodic function should be called
 */
bool fsm_dpi_client_periodic_check(struct fsm_session *session);

/**
 * @brief process a flow attribute (shared processing of `include`, `exclude`,
 *        as well as initial parameter checks)
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param type the value type (RTS_TYPE_BINARY, RTS_TYPE_STRING or RTS_TYPE_NUMBER)
 * @param length the length in bytes of the value
 * @param value the value itself
 * @param acc the flow
 */
int
fsm_dpi_client_process_attr(struct fsm_session *session, const char *attr,
                            uint8_t type, uint16_t length, const void *value,
                            struct fsm_dpi_plugin_client_pkt_info *pkt_info);

struct fsm_dpi_client_cache *fsm_dpi_client_get_mgr(void);

/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_dpi_client_session *fsm_dpi_client_lookup_session(struct fsm_session *session);

/**
 * @brief Frees a fsm_dpi_client session
 *
 * @param n_session the fsm_dpi_client session to delete
 */
void fsm_dpi_client_free_session(struct fsm_dpi_client_session *u_session);

/**
 * @brief deletes a session
 *
 * @param session the fsm session to delete
 */
void fsm_dpi_client_delete_session(struct fsm_session *session);

int fsm_session_cmp(const void *a, const void *b);

#endif /* FSM_DPI_CLIENT_PLUGIN_H_INCLUDED */
