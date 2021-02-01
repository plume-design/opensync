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

#include "gatekeeper_curl.h"
#include "gatekeeper.h"
#include "ds_tree.h"
#include "const.h"
#include "log.h"


static struct fsm_gk_mgr cache_mgr =
{
    .initialized = false,
};


/**
 * @brief returns the plugin's session manager
 *
 * @return the plugin's session manager
 */
struct fsm_gk_mgr *
gatekeeper_get_mgr(void)
{
    return &cache_mgr;
}


/**
 * @brief perfroms check whether to allow or block this packet.
 *        by connecting ot the guard server.
 *
 * @param session the fsm session
 * @req: the request being processed
 * @policy: the policy being checked against
 * @return true if the success false otherwise
 */
bool
gatekeeper_get_verdict(struct fsm_session *session,
                       struct fsm_policy_req *req)
{
    LOGI("%s: session %s", __func__, session->name);
    gk_new_conn(req->url);

    return true;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions match
 */
static int
fsm_gk_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}


/**
 * @brief initializes gate keeper plugin
 *
 * Initializes the gate keeper plugin once, with the parameters
 * given within the session.
 * @param session the fsm session containing the BC service config
 */
bool
gatekeeper_init(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;
    mgr = gatekeeper_get_mgr();
    if (mgr->initialized) return true;

    gk_curl_init(session->loop);

    mgr->initialized = true;
    return true;
}


/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the pcap handler and the periodic routines called
 * by fsm.
 * @param session pointer provided by fsm
 */
int
gatekeeper_plugin_init(struct fsm_session *session)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_web_cat_ops *cat_ops;
    struct fsm_gk_mgr *mgr;

    if (session == NULL) return -1;

    mgr = gatekeeper_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        bool ret;

        ret = gatekeeper_init(session);
        if (!ret) return 0;

        ds_tree_init(&mgr->fsm_sessions, fsm_gk_session_cmp,
                     struct fsm_gk_session, session_node);

        mgr->initialized = true;
    }

    /* Look up the fsm bc session */
    fsm_gk_session = gatekeeper_lookup_session(session);
    if (fsm_gk_session == NULL)
    {
        LOGE("%s: could not allocate gate keeper plugin", __func__);
        gk_curl_exit();
        return -1;
    }

    /* Bail if the session is already initialized */
    if (fsm_gk_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.periodic = gatekeeper_periodic;
    session->ops.exit = gatekeeper_exit;
    session->handler_ctxt = fsm_gk_session;

    /* Set the plugin specific ops */
    cat_ops = &session->p_ops->web_cat_ops;
    cat_ops->gatekeeper_req = gatekeeper_get_verdict;

    fsm_gk_session->initialized = true;
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
gatekeeper_exit(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;

    mgr = gatekeeper_get_mgr();
    if (!mgr->initialized) return;

    gatekeeper_delete_session(session);

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
gatekeeper_periodic(struct fsm_session *session)
{
    /* Place holder */
}


/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct fsm_gk_session *
gatekeeper_lookup_session(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;
    struct fsm_gk_session *wc_session;
    ds_tree_t *sessions;

    mgr = gatekeeper_get_mgr();
    sessions = &mgr->fsm_sessions;

    wc_session = ds_tree_find(sessions, session);
    if (wc_session != NULL) return wc_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    wc_session = calloc(1, sizeof(*wc_session));
    if (wc_session == NULL) return NULL;

    ds_tree_insert(sessions, wc_session, session);

    return wc_session;
}


/**
 * @brief Frees a gate keeper session
 *
 * @param wc_session the gate keeper session to delete
 */
void
gatekeeper_free_session(struct fsm_gk_session *wc_session)
{
    free(wc_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the wc session to delete
 */
void
gatekeeper_delete_session(struct fsm_session *session)
{
    struct fsm_gk_mgr *mgr;
    struct fsm_gk_session *wc_session;
    ds_tree_t *sessions;

    mgr = gatekeeper_get_mgr();
    sessions = &mgr->fsm_sessions;

    wc_session = ds_tree_find(sessions, session);
    if (wc_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, wc_session);
    gatekeeper_free_session(wc_session);

    return;
}
