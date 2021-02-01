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

#ifndef IPTHREAT_DPI_H_INCLUDED
#define IPTHREAT_DPI_H_INCLUDED

#include <jansson.h>
#include <pcap.h>
#include <stdint.h>
#include <time.h>

#include "os_types.h"
#include "fsm.h"
#include "fsm_policy.h"
#include "net_header_parse.h"


/**
 * @brief ipthreat_dpi parser
 *
 * The parser contains the parsed info for the packet currently processed
 * It embeds:
 * - the network header,
 * - the data length which excludes the network header
 * - the amount of data parsed
 */
struct ipthreat_dpi_parser
{
    struct net_header_parser *net_parser; /* network header parser */
    size_t data_len;                      /* Non-network related data length */
    uint8_t *data;                        /* Non-network data pointer */
    size_t parsed;                        /* Parsed bytes */
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
 * - a set of devices presented to the session
 */
struct ipthreat_dpi_session
{
    struct fsm_session *session;
    bool initialized;
    struct ipthreat_dpi_parser parser;
    struct fsm_policy_client inbound;
    struct fsm_policy_client outbound;
    ds_tree_node_t session_node;
};

/**
 * @brief the plugin cache, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct ipthreat_dpi_cache
{
    bool initialized;
    time_t periodic_ts;
    ds_tree_t ipt_sessions;
};


/**
 * @brief an IP request
 */
struct ipthreat_dpi_req
{
    struct fsm_session *session;
    os_macaddr_t *dev_id;
    struct sockaddr_storage *ip;
    struct policy_table *table;
    struct net_md_stats_accumulator *acc;
};


/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
ipthreat_dpi_plugin_init(struct fsm_session *session);


/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
ipthreat_dpi_plugin_exit(struct fsm_session *session);


/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 * @param session the fsm session
 * @param net_parser the container of parsed header and original packet
 */
void
ipthreat_dpi_plugin_handler(struct fsm_session *session,
                            struct net_header_parser *net_parser);

/**
 * @brief process the parsed message
 *
 * Place holder for message content processing
 * @param ds_session the ipthreat scan session pointing to the parsed message
 */
void
ipthreat_dpi_process_message(struct ipthreat_dpi_session *ds_session);


/**
 * @brief parses the received message content
 *
 * @param parser the parsed data container
 * @return the size of the parsed message content, or 0 on parsing error.
 */
size_t
ipthreat_dpi_parse_content(struct ipthreat_dpi_parser *parser);


/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * @param session the fsm session
 */
void
ipthreat_dpi_plugin_periodic(struct fsm_session *session);


/**
 * @brief parses a l2uf message
 *
 * @param parser the parsed data container
 * @return the size of the parsed message, or 0 on parsing error.
 */
size_t
ipthreat_dpi_parse_message(struct ipthreat_dpi_parser *parser);


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct ipthreat_dpi_session *
ipthreat_dpi_lookup_session(struct fsm_session *session);


/**
 * @brief Frees a ipthreat scan session
 *
 * @param l_session the ipthreat scan session to delete
 */
void
ipthreat_dpi_free_session(struct ipthreat_dpi_session *ds_session);


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the ipthreat scan session to delete
 */
void
ipthreat_dpi_delete_session(struct fsm_session *session);


struct ipthreat_dpi_cache *
ipthreat_dpi_get_mgr(void);


#endif /* IPTHREAT_DPI_H_INCLUDED */
