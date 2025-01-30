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

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "ds_tree.h"
#include "fsm.h"
#include "fsm_policy.h"
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
struct fsm_dpi_sni_session
{
    struct fsm_session *session;
    bool initialized;
    time_t timestamp;
    char *included_devices;
    char *excluded_devices;
    struct fsm_dpi_adt_report_aggregator *adt_aggr;
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

struct fsm_dpi_sni_redirect_flow_request
{
    struct net_md_stats_accumulator *acc;
    struct fsm_session *session;
    struct net_md_flow_info *info;
    char *attribute_value;
    int req_type;
};

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the periodic routines called by fsm.
 * @param session pointer provided by fsm
 */
int
fsm_dpi_sni_init(struct fsm_session *session);

/**
 * @brief session initialization entry point
 *
 * This is provided to maintain compatibility with older
 * configurations
 */
int
dpi_sni_plugin_init(struct fsm_session *session);

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
fsm_dpi_sni_exit(struct fsm_session *session);

/**
 * @brief update routine
 *
 * @param session the fsm session keying the fsm_dpi_sni session to update
 */
void
fsm_dpi_sni_update(struct fsm_session *session);

/**
 * @brief periodic routine
 *
 * @param session the fsm session keying the fsm_dpi_sni session to process
 */
void
fsm_dpi_sni_periodic(struct fsm_session *session);

/**
 * @brief process an SNI flow attribute specifically
 *
 * @param session the fsm session
 * @param attr the attribute flow
 * @param type the value type (RTS_TYPE_BINARY, RTS_TYPE_STRING or RTS_TYPE_NUMBER)
 * @param length the length in bytes of the value
 * @param value the value itself
 * @param packet_info packet details (acc, net_parser)
 */
int
fsm_dpi_sni_process_attr(struct fsm_session *session, const char *attr,
                         uint8_t type, uint16_t length, const void *value,
                         struct fsm_dpi_plugin_client_pkt_info *pkt_info);

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

struct fsm_dpi_sni_cache *fsm_dpi_sni_get_mgr(void);

void fsm_dpi_sni_set_ttl(struct fsm_session *session, time_t ttl);

int dpi_sni_get_req_type(const char *attr);
int dpi_sni_policy_req(struct fsm_request_args *request_args, char *attr_value);
bool dpi_sni_is_redirected_flow(struct net_md_flow_info *info);
bool dpi_sni_is_redirected_attr(struct fsm_dpi_sni_redirect_flow_request *param);
struct fsm_policy_reply *dpi_sni_create_reply(struct fsm_request_args *request_args);
struct fsm_policy_req *dpi_sni_create_request(struct fsm_request_args *request_args, char *attr_value);

bool
dpi_sni_fetch_fqdn_from_url_attr(char *attribute_name, char *fqdn, int fqdn_size);

#endif /* FSM_DPI_SNI_H_INCLUDED */
