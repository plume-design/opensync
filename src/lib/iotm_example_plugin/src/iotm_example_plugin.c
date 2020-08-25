/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "iotm_example_plugin.h"

static struct iotm_example_plugin_cache
cache_mgr =
{
    .initialized = false,
};

struct iotm_example_plugin_cache *
iotm_example_get_mgr(void)
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
iotm_example_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

/**
 * @brief Frees a iotm example session
 *
 * @param i_session the iotm example session to delete
 */
void
iotm_example_free_session(struct iotm_example_session *i_session)
{
    // cleanup rules if necessary
    free(i_session);
}

/**
 * @brief deletes a session
 *
 * @param session the iotm session keying the iot session to delete
 */
void
iotm_example_delete_session(struct iotm_session *session)
{
    struct iotm_example_plugin_cache *mgr;
    struct iotm_example_session *i_session;
    ds_tree_t *sessions;

    mgr = iotm_example_get_mgr();
    sessions = &mgr->iotm_sessions;

    i_session = ds_tree_find(sessions, session);
    if (i_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, i_session);
    iotm_example_free_session(i_session);

    return;
}

/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct iotm_example_session *
iotm_example_lookup_session(struct iotm_session *session)
{
    struct iotm_example_plugin_cache *mgr;
    struct iotm_example_session *i_session;
    ds_tree_t *sessions;

    mgr = iotm_example_get_mgr();
    sessions = &mgr->iotm_sessions;

    i_session = ds_tree_find(sessions, session);
    if (i_session != NULL) return i_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    i_session = calloc(1, sizeof(struct iotm_example_session));
    if (i_session == NULL) return NULL;

    ds_tree_insert(sessions, i_session, session);

    return i_session;
}

/**
 * @brief set up periodic callback to run cleanup and such
 *
 * @param session  session tracked by manager
 */
void
iotm_example_plugin_periodic(struct iotm_session *session)
{
    if (session->topic == NULL) return;
    // any periodic work here
    return;
}

void
iotm_example_plugin_exit(struct iotm_session *session)
{
    LOGI("%s: Cleaning and exit from the example plugin session.\n",
            __func__);
    struct iotm_example_plugin_cache *mgr;
    mgr = iotm_example_get_mgr();
    if (!mgr->initialized) return;

    // cleanup logic if necessary here
    iotm_example_delete_session(session);
}

/**
 * @brief this method is called any time there are ovsdb updates
 *
 * @param session  contains current state reflected in OVSDB
 */
void
iotm_example_plugin_update(struct iotm_session *session)
{
    return;
}

void log_cb(ds_list_t *dl, struct iotm_value_t *val, void *ctx)
{
    LOGI("%s: Param Key : [%s] | Param Value : [%s]\n",
            __func__, val->key, val->value);
}

/**
 * @brief handler for when events are passed from IOTM
 */
void iotm_example_plugin_handle(
        struct iotm_session *session,
        struct plugin_command_t *command)
{
    LOGI("in %s\n",  __func__);
    if ( command == NULL ) return;

    struct iotm_tree_t *params = command->params;

    LOGI("%s: Information from: [%s]\n", 
            __func__, command->action);

    params->foreach_val(params, log_cb, NULL);
}

void no_op_tag_update(struct iotm_session *session)
{
    // NO-OP
}

void no_op_rule_update(
        struct iotm_session *session,
        ovsdb_update_monitor_t *mon,
        struct iotm_rule *rule)
{
    // NO-OP
}

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the event handler and the periodic routines called
 * by iotm.
 * @param session pointer provided by iotm
 *
 * @note init name loaded in IOT_Manager_Config for other_config_value
 * ['dso_init']
 *
 * @note if ['dso_init'] is not set the default will be the <name>_plugin_init
 */
int
iotm_example_plugin_init(struct iotm_session *session)
{
    printf("in %s\n",  __func__);
    struct iotm_example_plugin_cache *mgr;
    struct iotm_example_session *iotm_example_session;

    if (session == NULL) return -1;

    mgr = iotm_example_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(&mgr->iotm_sessions, iotm_example_session_cmp,
                     struct iotm_example_session, session_node);

        mgr->initialized = true;
    }

    /* Look up the iotm example session */
    iotm_example_session= iotm_example_lookup_session(session);
    printf("after getting session\n");

    if (iotm_example_session == NULL)
    {
        LOGE("%s: could not allocate iotm_example parser", __func__);
        return -1;
    }

    // Load plugin defined function pointers
    session->ops.periodic = iotm_example_plugin_periodic;
    session->ops.update = iotm_example_plugin_update;
    session->ops.exit = iotm_example_plugin_exit;
    session->ops.handle = iotm_example_plugin_handle;
    session->ops.rule_update = no_op_rule_update;
    session->ops.tag_update = no_op_tag_update;

    return 0;
}
