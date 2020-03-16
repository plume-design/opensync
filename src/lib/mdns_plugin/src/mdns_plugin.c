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
#include "assert.h"
#include "ovsdb.h"
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "schema.h"

#include "mdns_plugin.h"

static struct mdns_plugin_mgr
mgr =
{
    .initialized = false,
};

struct mdns_plugin_mgr *
mdns_get_mgr(void)
{
    return &mgr;
}

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int
mdns_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

/**
 * @brief get a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct mdns_session *
mdns_get_session(struct fsm_session *session)
{
    struct mdns_plugin_mgr *mgr;
    struct mdns_session *md_session;
    ds_tree_t *sessions;

    mgr = mdns_get_mgr();
    sessions = &mgr->fsm_sessions;

    md_session = ds_tree_find(sessions, session);
    if (md_session != NULL) return md_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    md_session = calloc(1, sizeof(struct mdns_session));
    if (md_session == NULL) return NULL;

    md_session->initialized = false;
    ds_tree_insert(sessions, md_session, session);

    return md_session;
}


void
mdns_mgr_init(void)
{
    struct mdns_plugin_mgr *mgr;

    mgr = mdns_get_mgr();
    if (mgr->initialized) return;

    ds_tree_init(&mgr->fsm_sessions, mdns_session_cmp,
                 struct mdns_session, session_node);
    mgr->ovsdb_init = mdns_ovsdb_init;
    mgr->ctxt = NULL;

    mgr->initialized = true;
}

/**
 * @brief Frees a mdns session
 *
 * @param d_session the dns session to delete
 */
void
mdns_free_session(struct mdns_session *md_session)
{
    free(md_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the mdns session to delete
 */
void
mdns_delete_session(struct fsm_session *session)
{
    struct mdns_plugin_mgr *mgr;
    struct mdns_session *md_session;
    ds_tree_t *sessions;

    mgr = mdns_get_mgr();
    sessions = &mgr->fsm_sessions;

    md_session = ds_tree_find(sessions, session);
    if (md_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);

    ds_tree_remove(sessions, md_session);
    mdns_free_session(md_session);

    return;
}

/**
 * @brief session exit point
 *
 * Frees up resources used by the session.
 * @param session pointer provided by fsm
 */
void
mdns_plugin_exit(struct fsm_session *session)
{
    struct mdns_plugin_mgr *mgr;

    mgr = mdns_get_mgr();
    if (!mgr->initialized) return;

    mdnsd_ctxt_exit();
    mdns_delete_session(session);
    mgr->initialized = false;
    return;
}

/**
 * @brief session packet processing entry point
 *
 * packet processing handler.
 * @param args the fsm session
 * @param h the pcap capture header
 * @param bytes a pointer to the captured packet
 */
void
mdns_plugin_handler(struct fsm_session *session,
                    struct net_header_parser *net_parser)
{
    struct mdns_session *f_session;
    struct mdns_parser *parser;

    f_session = (struct mdns_session *)session->handler_ctxt;
    parser = &f_session->parser;
    parser->net_parser = net_parser;

    // mdns parsing to be added
    return;
}

/**
 * @brief session packet periodic processing entry point
 *
 * Periodically called by the fsm manager
 * Sends a flow stats report.
 * @param session the fsm session
 */
void
mdns_plugin_periodic(struct fsm_session *session)
{
    struct mdns_session *f_session;
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context *pctxt = mgr->ctxt;

    if (session->topic == NULL || !pctxt) return;

    f_session = session->handler_ctxt;

    // Update mdnsd ctxt.
    mdnsd_ctxt_update(f_session);

    return;
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
mdns_plugin_init(struct fsm_session *session)
{
    struct mdns_plugin_mgr *mgr;
    struct mdns_session    *md_session;

    if (session == NULL) return -1;

    /* Initialize the manager on first call */
    mdns_mgr_init();
    mgr = mdns_get_mgr();
    mgr->loop = session->loop;
    mgr->ovsdb_init();

    /* Look up the dns session */
    md_session = mdns_get_session(session);
    if (md_session == NULL)
    {
        LOGE("%s: could not allocate mdns plugin", __func__);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (md_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.periodic = mdns_plugin_periodic;
    session->ops.exit = mdns_plugin_exit;
    session->handler_ctxt = md_session;

    /* Wrap up the session initialization */
    md_session->session = session;

    // Initialize mdnsd.
    if (!mdnsd_ctxt_init(md_session)) goto err_plugin;

    md_session->initialized = true;
    LOGD("%s: added session %s", __func__, session->name);

    return 0;

err_plugin:
    free(md_session);
    return -1;
}
