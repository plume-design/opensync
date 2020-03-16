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

#ifndef L2UF_PARSE_H_INCLUDED
#define L2UF_PARSE_H_INCLUDED

#include <jansson.h>
#include <pcap.h>
#include <stdint.h>
#include <time.h>

#include "os_types.h"
#include "fsm.h"
#include "net_header_parse.h"

/**
 * @brief l2uf parser
 *
 * The parser contains the parsed info for the packet currently processed
 * It embeds:
 * - the network header,
 * - the data length which excludes the network header
 * - the amount of data parsed
 */
struct l2uf_parser
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
 * - a flow stats aggregator
 * - a set of devices presented to the session
 */
struct l2uf_session
{
    struct fsm_session *session;
    bool initialized;
    struct l2uf_parser parser;
    ds_tree_node_t session_node;
};

/**
 * @brief the plugin cache, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct l2uf_cache
{
    bool initialized;
    ds_tree_t fsm_sessions;
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
l2uf_plugin_init(struct fsm_session *session);


/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
l2uf_plugin_exit(struct fsm_session *session);


/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 * @param session the fsm session
 * @param net_parser the container of parsed header and original packet
 */
void
l2uf_plugin_handler(struct fsm_session *session,
                    struct net_header_parser *net_parser);


/**
 * @brief parses a l2uf message
 *
 * @param parser the parsed data container
 * @return the size of the parsed message, or 0 on parsing error.
 */
size_t
l2uf_parse_message(struct l2uf_parser *parser);


/**
 * @brief parses the received message content
 *
 * @param parser the parsed data container
 * @return the size of the parsed message content, or 0 on parsing error.
 */
size_t
l2uf_parse_content(struct l2uf_parser *parser);


/**
 * @brief process the parsed message
 *
 * Place holder for message content processing
 * @param l_session the l2uf session pointing to the parsed message
 */
void
l2uf_process_message(struct l2uf_session *f_session);


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct l2uf_session *
l2uf_lookup_session(struct fsm_session *session);


/**
 * @brief Frees a l2uf session
 *
 * @param l_session the l2uf session to delete
 */
void
l2uf_free_session(struct l2uf_session *l_session);


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the l2uf session to delete
 */
void
l2uf_delete_session(struct fsm_session *session);


struct l2uf_cache *
l2uf_get_mgr(void);


#endif /* L2UF_PARSE_H_INCLUDED */
