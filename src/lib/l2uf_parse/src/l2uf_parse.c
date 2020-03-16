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


#include <stdlib.h>
#include <stddef.h>
#include <time.h>

#include "const.h"
#include "ds_tree.h"
#include "log.h"
#include "l2uf_parse.h"
#include "assert.h"
#include "json_util.h"
#include "ovsdb.h"
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "net_header_parse.h"

static struct l2uf_cache
cache_mgr =
{
    .initialized = false,
};


struct l2uf_cache *
l2uf_get_mgr(void)
{
    return &cache_mgr;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
l2uf_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the packet parsing handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
l2uf_plugin_init(struct fsm_session *session)
{
    struct l2uf_session *l2uf_session;
    struct l2uf_cache *mgr;
    struct fsm_parser_ops *parser_ops;

    if (session == NULL) return -1;

    mgr = l2uf_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(&mgr->fsm_sessions, l2uf_session_cmp,
                     struct l2uf_session, session_node);
        mgr->initialized = true;
    }

    /* Look up the l2uf session */
    l2uf_session = l2uf_lookup_session(session);
    if (l2uf_session == NULL)
    {
        LOGE("%s: could not allocate the l2uf parser", __func__);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (l2uf_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.exit = l2uf_plugin_exit;
    session->handler_ctxt = l2uf_session;

    /* Set the plugin specific ops */
    parser_ops = &session->p_ops->parser_ops;
    parser_ops->handler = l2uf_plugin_handler;

    /* Wrap up the session initialization */
    l2uf_session->session = session;

    l2uf_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    return 0;
}

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
l2uf_plugin_exit(struct fsm_session *session)
{
    struct l2uf_cache *mgr;

    mgr = l2uf_get_mgr();
    if (!mgr->initialized) return;

    l2uf_delete_session(session);
    return;
}


/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 * @param args the fsm session
 * @param net_parser the packet container
 */
void
l2uf_plugin_handler(struct fsm_session *session,
                    struct net_header_parser *net_parser)
{
    struct l2uf_session *l_session;
    struct l2uf_parser *parser;
    size_t len;

    l_session = (struct l2uf_session *)session->handler_ctxt;
    parser = &l_session->parser;
    parser->net_parser = net_parser;

    len = l2uf_parse_message(parser);
    if (len == 0) return;

    l2uf_process_message(l_session);

    return;
}


/**
 * @brief parses the received message
 *
 * @param parser the parsed data container
 * @return the size of the parsed message, or 0 on parsing error.
 */
size_t
l2uf_parse_message(struct l2uf_parser *parser)
{
    struct net_header_parser *net_parser;
    size_t len;

    if (parser == NULL) return 0;

    /* Parse network header */
    net_parser = parser->net_parser;
    parser->parsed = net_parser->parsed;
    parser->data = net_parser->data;

    /* Adjust payload length to remove potential ethernet padding */
    parser->data_len = net_parser->packet_len - net_parser->parsed;

    /* Parse the message content */
    len = l2uf_parse_content(parser);

    return len;
}


/**
 * @brief parses the received message content
 *
 * @param parser the parsed data container
 * @return the size of the parsed message content, or 0 on parsing error.
 */
size_t
l2uf_parse_content(struct l2uf_parser *parser)
{
    /*
     * Place holder to process the packet content after the network header
     */
    return parser->parsed;
}



/**
 * @brief process the parsed message
 *
 * Prepare a key to lookup the flow stats info, and update the flow stats.
 * @param l_session the l2uf session pointing to the parsed message
 */
void
l2uf_process_message(struct l2uf_session *l_session)
{
    struct net_header_parser *net_parser;
    struct eth_header *eth_header;
    struct l2uf_parser *parser;

    /* Place holder. Call the target API l2uf processing */
    parser = &l_session->parser;
    net_parser = parser->net_parser;
    eth_header = &net_parser->eth_header;

    LOGT("%s: Processing L2UF with source " PRI_os_macaddr_lower_t,
         __func__, FMT_os_macaddr_pt(eth_header->srcmac));
}


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct l2uf_session *
l2uf_lookup_session(struct fsm_session *session)
{
    struct l2uf_cache *mgr;
    struct l2uf_session *l_session;
    ds_tree_t *sessions;

    mgr = l2uf_get_mgr();
    sessions = &mgr->fsm_sessions;

    l_session = ds_tree_find(sessions, session);
    if (l_session != NULL) return l_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    l_session = calloc(1, sizeof(struct l2uf_session));
    if (l_session == NULL) return NULL;

    ds_tree_insert(sessions, l_session, session);

    return l_session;
}


/**
 * @brief Frees a l2uf session
 *
 * @param l_session the l2uf session to free
 */
void
l2uf_free_session(struct l2uf_session *l_session)
{
    free(l_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the l2uf session to delete
 */
void
l2uf_delete_session(struct fsm_session *session)
{
    struct l2uf_cache *mgr;
    struct l2uf_session *l_session;
    ds_tree_t *sessions;

    mgr = l2uf_get_mgr();
    sessions = &mgr->fsm_sessions;

    l_session = ds_tree_find(sessions, session);
    if (l_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, l_session);
    l2uf_free_session(l_session);

    return;
}
