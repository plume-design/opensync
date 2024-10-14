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

#include "fsm.h"
#include "oms.h"
#include "oms_ps.h"
#include "ovsdb_update.h"
#include "memutil.h"
#include "fsm_oms.h"

/**
 * @brief: int object state id, state string record
 */
struct fsm_oms_state
{
    int fsm_oms_state_id;
    char *fsm_oms_state_str;
};

/**
 * @brief: object state id to state string mapping
 */
static struct fsm_oms_state
fsm_oms_states[] =
{
    {
        .fsm_oms_state_id = FSM_OBJ_ACTIVE,
        .fsm_oms_state_str = "active",
    },
    {
        .fsm_oms_state_id = FSM_OBJ_ERROR,
        .fsm_oms_state_str = "error",
    },
    {
        .fsm_oms_state_id = FSM_OBJ_OBSOLETE,
        .fsm_oms_state_str = "obsolete",
    },
    {
        .fsm_oms_state_id = FSM_OBJ_LOAD_FAILED,
        .fsm_oms_state_str = "load-failed",
    }
};


/**
 * @brief returns the object state as a string when provided that state id
 *
 * @param the state id
 * @return the maching string if found, NULL otherwise
 */
static char *
fsm_state_id_to_str(int state_id)
{
    size_t len;
    size_t i;
    int id;

    len = sizeof(fsm_oms_states) / sizeof(fsm_oms_states[0]);
    for (i = 0; i < len; i++)
    {
        id = fsm_oms_states[i].fsm_oms_state_id;
        if (id == state_id) return fsm_oms_states[i].fsm_oms_state_str;
    }

    return NULL;
}


/**
 * @brief update the OMS_State table with the given object state
 *
 * @param session the fsm session triggering the call
 * @param object the object advertizing its state
 */
void
fsm_set_object_state(struct fsm_session *session, struct fsm_object *object)
{
    struct oms_state_entry state;

    MEMZERO(state);
    state.object = object->object;
    state.version = object->version;
    state.state = fsm_state_id_to_str(object->state);
    oms_update_state_entry(&state);
}


/**
 * @brief notify plugins of object updates
 *
 * @param entry the object getiing updated
 * @param ovsdb_event the object update event
 */
static void
fsm_oms_notify_plugins(struct oms_config_entry *entry, int ovsdb_event)
{
    struct fsm_object_node *node;
    struct fsm_object fsm_object;
    struct fsm_session *session;
    struct fsm_mgr *mgr;
    ds_tree_t *tree;

    mgr = fsm_get_mgr();
    tree = &mgr->objects_to_monitor;
    node = ds_tree_find(tree, entry->object);
    if (node == NULL) return;

    session = node->session;
    if (IS_NULL_PTR(session->ops.object_cb)) return;

    MEMZERO(fsm_object);
    fsm_object.object = entry->object;
    fsm_object.version = entry->version;

    session->ops.object_cb(session, &fsm_object, ovsdb_event);
}


/**
 * @brief callback to the oms library for object updates
 *
 * @param entry the object getiing updated
 * @param ovsdb_event the object update event
 */
static void
fsm_oms_config_cb(struct oms_config_entry *entry, int ovsdb_event)
{
    switch(ovsdb_event)
    {
    case OVSDB_UPDATE_NEW:
        LOGD("%s: new entry %s version %s", __func__,
             entry->object, entry->version);
        fsm_oms_notify_plugins(entry, ovsdb_event);
        break;

    case OVSDB_UPDATE_DEL:
        LOGD("%s: delete entry %s version %s", __func__,
             entry->object, entry->version);
        fsm_oms_notify_plugins(entry, ovsdb_event);
        break;

    case OVSDB_UPDATE_MODIFY:
        LOGD("%s: update entry %s version %s", __func__,
             entry->object, entry->version);
        break;

    default:
        return;
    }

    return;
}


/**
 * @brief return the highest version of an object
 *
 * @param session the querying fsm session
 * @param object the object name
 * @param max_version the version cap, excluded
 * @return the object with the highest version
 *
 * If @param max_version is provided, the return shall be lesser than it or NULL
 * The caller is responsible for freeing the returned object
 */
struct fsm_object *
fsm_oms_get_highest_version(struct fsm_session *session, char *name,
                            char *max_version)
{
    struct oms_config_entry *entry;
    struct fsm_object *object;

    entry = oms_get_highest_version(name, max_version,
                                    session->ops.version_cmp_cb);
    if (entry ==  NULL) return NULL;

    object = CALLOC(1, sizeof(*object));
    if (object == NULL) return NULL;

    object->object = name;
    object->version = entry->version;

    return object;
}

/**
 * @brief return the last active version of an object
 *
 * @param session the querying fsm session
 * @param object the object name
 * @return the object with the active version
 *
 * If no last active version is saved in persistent storage return NULL
 * The caller is responsible for freeing the returned object
 */
struct fsm_object *
fsm_oms_get_last_active_version(struct fsm_session *session, char *name)
{
    struct oms_config_entry *entry;
    struct fsm_object *object;

    entry = oms_ps_get_last_active_version(name);
    if (entry ==  NULL) return NULL;

    object = CALLOC(1, sizeof(*object));
    if (object == NULL) return NULL;

    object->object = name;
    object->version = entry->version;

    return object;
}


/**
 * @brief return the "best" version of an object
 *
 * @param session the querying fsm session
 * @param object the object name
 * @return the object with the active version
 *
 * First try to get the last downloaded version,
 * then the last active version, finally the FW integrated version.
 * The caller is responsible for freeing the returned object.
 */
struct fsm_object *
fsm_oms_get_best_version(struct fsm_session *session, char *name)
{
    struct oms_config_entry *oms_entry;
    struct fsm_object *object;

    oms_entry = oms_get_best_object(name);
    if (oms_entry == NULL) return NULL;

    object = CALLOC(1, sizeof(*object));

    object->object = name;
    object->version = oms_entry->version;

    return object;
}


static bool
fsm_oms_accept_id(const char *object_id)
{
    return true;
}


void
fsm_register_object_to_monitor(struct fsm_session *session, char *name)
{
    struct fsm_object_node *node;
    struct fsm_mgr *mgr;
    ds_tree_t *tree;

    mgr = fsm_get_mgr();
    tree = &mgr->objects_to_monitor;
    node = ds_tree_find(tree, name);
    if (node != NULL)
    {
        LOGE("%s: object %s already monitored by %s", __func__,
             name, node->session->name);
        return;
    }

    node = CALLOC(1, sizeof(*node));
    node->object = STRDUP(name);
    node->session = session;

    ds_tree_insert(tree, node, node->object);
}


void
fsm_unregister_object_to_monitor(struct fsm_session *session, char *name)
{
    struct fsm_object_node *node;
    struct fsm_mgr *mgr;
    ds_tree_t *tree;

    mgr = fsm_get_mgr();
    tree = &mgr->objects_to_monitor;
    node = ds_tree_find(tree, name);
    if (node == NULL)
    {
        LOGE("%s: object %s not monitored", __func__,
             name);
        return;
    }

    ds_tree_remove(tree, node);
    FREE(node->object);
    FREE(node);
}


static int
fsm_object_cmp(const void *a, const void *b)
{
    return strcmp(a, b);
}

void
fsm_oms_init(void)
{
    struct oms_ovsdb_set oms_set;
    struct fsm_mgr *mgr;

    mgr = fsm_get_mgr();
    ds_tree_init(&mgr->objects_to_monitor, fsm_object_cmp,
                 struct fsm_object_node, obj_node);

    oms_init_manager();

    memset(&oms_set, 0, sizeof(oms_set));
    oms_set.monitor_config = true;
    oms_set.monitor_state = true;
    oms_set.monitor_awlan = false;
    oms_set.accept_id = fsm_oms_accept_id;
    oms_set.config_cb = fsm_oms_config_cb;
    oms_set.state_cb = NULL;
    oms_set.report_cb = NULL;

    oms_ovsdb_init(&oms_set);
    return;
}

void
fsm_oms_exit(void)
{
    struct fsm_object_node *remove;
    struct fsm_object_node *entry;
    struct oms_ovsdb_set oms_set;
    struct fsm_mgr *mgr;
    ds_tree_t *tree;

    mgr = fsm_get_mgr();
    tree = &mgr->objects_to_monitor;
    entry = ds_tree_head(tree);
    while (entry != NULL)
    {
        remove = entry;
        entry = ds_tree_next(tree, entry);
        ds_tree_remove(tree, remove);
        FREE(remove->object);
        FREE(remove);
    }

    MEMZERO(oms_set);
    oms_set.monitor_config = true;
    oms_set.monitor_state = false;
    oms_set.monitor_awlan = false;

    oms_ovsdb_exit(&oms_set);
    oms_exit_manager();
}
