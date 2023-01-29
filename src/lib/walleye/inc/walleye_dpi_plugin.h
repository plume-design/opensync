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

#ifndef __WALLEYE_PLUGIN_H__
#define __WALLEYE_PLUGIN_H__
#include <jansson.h>
#include <pcap.h>
#include <stdint.h>
#include <time.h>

#include "os_types.h"
#include "fsm.h"
#include "network_metadata.h"
#include "net_header_parse.h"

#include "rts.h"
#include "nfe.h"

/**
 * @brief demo parser
 *
 * The parser contains the parsed info for the packet currently processed
 * It embeds:
 * - the network header,
 * - the data length which excludes the network header
 * - the amount of data parsed
 */
struct dpi_parser
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
 */
struct dpi_session
{
    struct fsm_session *session;
    bool initialized;
    bool signature_loaded;
    bool conn_releasing;
    bool scan_dbg_enable;
    struct dpi_parser parser;
    rts_handle_t handle;
    nfe_conntrack_t ct;
    uint32_t rts_dict_expiry;
    uint32_t connections;
    uint32_t streams;
    uint32_t err_incomplete;
    uint32_t err_length;
    uint32_t err_create;
    uint32_t err_scan;
    char *wc_topic;
    int wc_interval;
    ds_tree_node_t session_node;
};

/**
 * @brief the plugin cache, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct dpi_plugin_cache
{
    bool initialized;
    bool signature_loaded;
    ds_tree_t fsm_sessions;
    time_t periodic_ts;
    char *signature_version;
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
walleye_dpi_plugin_init(struct fsm_session *session);


/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
dpi_plugin_exit(struct fsm_session *session);


/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 * @param args the fsm session
 * @param h the pcap capture header
 * @param bytes a pointer to the captured packet
 */
void
dpi_plugin_handler(struct fsm_session *session,
                        struct net_header_parser *net_parser);


/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * @param session the fsm session
 */
void
dpi_plugin_periodic(struct fsm_session *session);


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct dpi_session *
dpi_lookup_session(struct fsm_session *session);


/**
 * @brief Frees a fsm demo session
 *
 * @param f_session the fsm demo session to delete
 */
void
dpi_free_session(struct dpi_session *f_session);


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the http session to delete
 */
void
dpi_delete_session(struct fsm_session *session);

/**
 * @brief returns the plugin's session manager
 *
 * @return the plugin's session manager
 */
struct dpi_plugin_cache *
dpi_get_mgr(void);


#endif /* __WALLEYE_PLUGIN_H__ */
