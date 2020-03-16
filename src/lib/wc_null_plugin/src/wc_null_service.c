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

#include "wc_null_service.h"
#include "const.h"
#include "ds_tree.h"
#include "log.h"


static struct fsm_wc_null_mgr cache_mgr =
{
    .initialized = false,
};


/**
 * @brief returns the plugin's session manager
 *
 * @return the plugin's session manager
 */
struct fsm_wc_null_mgr *
fsm_wc_null_get_mgr(void)
{
    return &cache_mgr;
}


/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions match
 */
static int
fsm_wc_null_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a == p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
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
fsm_wc_null_plugin_init(struct fsm_session *session)
{
    struct fsm_wc_null_session *fsm_wc_null_session;
    struct fsm_web_cat_ops *cat_ops;
    struct fsm_wc_null_mgr *mgr;

    if (session == NULL) return -1;

    mgr = fsm_wc_null_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        bool ret;

        ret = fsm_wc_null_init(session);
        if (!ret) return 0;

        ds_tree_init(&mgr->fsm_sessions, fsm_wc_null_session_cmp,
                     struct fsm_wc_null_session, session_node);

        mgr->initialized = true;
    }

    /* Look up the fsm bc session */
    fsm_wc_null_session = fsm_wc_null_lookup_session(session);
    if (fsm_wc_null_session == NULL)
    {
        LOGE("%s: could not allocate fsm wc null plugin", __func__);
        return -1;
    }

    /* Bail if the session is already initialized */
    if (fsm_wc_null_session->initialized) return 0;

    /* Set the fsm session */
    session->ops.periodic = fsm_wc_null_periodic;
    session->ops.exit = fsm_wc_null_exit;
    session->handler_ctxt = fsm_wc_null_session;

    /* Set the plugin specific ops */
    cat_ops = &session->p_ops->web_cat_ops;
    cat_ops->categories_check = fsm_wc_null_cat_check;
    cat_ops->cat2str = fsm_wc_null_report_cat;
    cat_ops->get_stats = fsm_wc_null_get_stats;
    cat_ops->dns_response = fsm_wc_null_process_reply;

    fsm_wc_null_session->initialized = true;
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
fsm_wc_null_exit(struct fsm_session *session)
{
    struct fsm_wc_null_mgr *mgr;

    mgr = fsm_wc_null_get_mgr();
    if (!mgr->initialized) return;

    fsm_wc_null_delete_session(session);

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
fsm_wc_null_periodic(struct fsm_session *session)
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
struct fsm_wc_null_session *
fsm_wc_null_lookup_session(struct fsm_session *session)
{
    struct fsm_wc_null_mgr *mgr;
    struct fsm_wc_null_session *wc_session;
    ds_tree_t *sessions;

    mgr = fsm_wc_null_get_mgr();
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
 * @brief Frees a fsm bc session
 *
 * @param wc_session the fsm wc session to delete
 */
void
fsm_wc_null_free_session(struct fsm_wc_null_session *wc_session)
{
    free(wc_session);
}


/**
 * @brief deletes a session
 *
 * @param session the fsm session keying the wc session to delete
 */
void
fsm_wc_null_delete_session(struct fsm_session *session)
{
    struct fsm_wc_null_mgr *mgr;
    struct fsm_wc_null_session *wc_session;
    ds_tree_t *sessions;

    mgr = fsm_wc_null_get_mgr();
    sessions = &mgr->fsm_sessions;

    wc_session = ds_tree_find(sessions, session);
    if (wc_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, wc_session);
    fsm_wc_null_free_session(wc_session);

    return;
}
