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

#ifndef WE_DPI_PLUGIN_H_INCLUDED
#define WE_DPI_PLUGIN_H_INCLUDED

#include "pthread.h"

#include "fsm.h"
#include "net_header_parse.h"
#include "ovsdb_table.h"
#include "we.h"

/**
 * @brief A DPI session
 */
struct we_dpi_session
{
    struct fsm_session *fsm;
    we_state_t we_state;
    int mempool_size;
    /* for our conntrack thread */
    pthread_t thread;
    /* for the conntrack thread, packet path, config callback, and FSM periodic */
    pthread_mutex_t lock;
    bool initialized;
};

/**
 * @brief WE user data for the agent
 *
 * User data that is passed through to external functions from WE applications.
 */
struct we_dpi_agent_userdata
{
    struct fsm_session *fsm;
    struct net_md_stats_accumulator *acc;
};

/**
 * @brief Plugin cache for the WE DPI Plugin
 *
 * Tracks global initialization of the plugin and its single DPI session.
 */
struct we_dpi_plugin_cache
{
    struct we_dpi_session dpi_session;
    bool initialized;
};

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int we_dpi_plugin_init(struct fsm_session *session);

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void we_dpi_plugin_exit(struct fsm_session *session);

/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 * @param args the fsm session
 * @param h the pcap capture header
 * @param bytes a pointer to the captured packet
 */
void we_dpi_plugin_handler(struct fsm_session *session, struct net_header_parser *net_parser);

/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * @param session the fsm session
 */
void we_dpi_plugin_periodic(struct fsm_session *session);

/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the http session to delete
 */
void we_dpi_delete_session(struct fsm_session *session);

/* FIXME: DEV ONLY */

int dev_we_dpi_plugin_init(struct fsm_session *session);

void dev_we_dpi_plugin_exit(struct fsm_session *session);

#endif /* WE_DPI_PLUGIN_H_INCLUDED */
