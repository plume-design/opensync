/**
* Copyright (c) 2020, Charter Communications Inc. All rights reserved.
* 
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*    1. Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*    2. Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*    3. Neither the name of the Charter Communications Inc. nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
* 
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "iotm_connected_devices.h"

/**
 * @file iotm_connected_devices.c
 *
 * @brief implementation of plugin, follows standard IoTM plugin patterns
 */

/**
 * @brief rules that allow routing of connect and disconnect events to plugin
 *
 * @note installed on init, removed on exit
 * @note actions_key needs to be plugin handler, this is dynamically loaded
 * into OVSDB IOT_Manager_Config table, so it is accessed through the session
 * pointer as the name and loaded into the rule before insertion
 */
#define NUM_CONNECT_RULES 4
static struct schema_IOT_Rule_Config connect_rules[] =
{
    {
        .name = "connected_device_ble_connect_rule",
        .event = "ble_connected",
        .filter_len = 1,
        .filter_keys =
        {
            "mac"
        },
        .filter =
        {
            "*",
        },
        .actions_len = 1,
        .actions =
        {
            CONNECT,
        },
    },
    {
        .name = "connected_device_ble_disconnect_rule",
        .event = "ble_disconnected",
        .filter_len = 1,
        .filter_keys =
        {
            "mac"
        },
        .filter =
        {
            "*",
        },
        .actions_len = 1,
        .actions =
        {
            DISCONNECT,
        },
    },
    {
        .name = "connected_device_zb_connect_rule",
        .event = "zigbee_device_annced",
        .filter_len = 1,
        .filter_keys =
        {
            "mac"
        },
        .filter =
        {
            "*",
        },
        .actions_len = 1,
        .actions =
        {
            CONNECT,
        },
    },
    {
        .name = "connected_device_zb_disconnect_rule",
        // TODO: heartbeat to emit this not implemented in handler
        .event = "zigbee_device_disconnected", 
        .filter_len = 1,
        .filter_keys =
        {
            "mac"
        },
        .filter =
        {
            "*",
        },
        .actions_len = 1,
        .actions =
        {
            DISCONNECT,
        },
    }
};

static struct iotm_connected_devices_cache
cache_mgr =
{
    .initialized = false,
};

struct iotm_connected_devices_cache *
iotm_connected_get_mgr(void)
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
static int iotm_connected_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

/**
 * @brief Frees a iotm connected session
 *
 * @param i_session the iotm connected session to delete
 */
void iotm_connected_free_session(struct iotm_connected_session *i_session)
{
    // cleanup rules if necessary
    free(i_session);
}

/**
 * @brief deletes a session
 *
 * @param session the iotm session keying the iot session to delete
 */
void iotm_connected_delete_session(struct iotm_session *session)
{
    struct iotm_connected_devices_cache *mgr;
    struct iotm_connected_session *i_session;
    ds_tree_t *sessions;

    mgr = iotm_connected_get_mgr();
    sessions = &mgr->iotm_sessions;

    i_session = ds_tree_find(sessions, session);
    if (i_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, i_session);
    iotm_connected_free_session(i_session);

    return;
}

/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct iotm_connected_session *iotm_connected_lookup_session(
        struct iotm_session *session)
{
    struct iotm_connected_devices_cache *mgr;
    struct iotm_connected_session *i_session;
    ds_tree_t *sessions;

    mgr = iotm_connected_get_mgr();
    sessions = &mgr->iotm_sessions;

    i_session = ds_tree_find(sessions, session);
    if (i_session != NULL) return i_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    i_session = calloc(1, sizeof(struct iotm_connected_session));
    if (i_session == NULL) return NULL;

    ds_tree_insert(sessions, i_session, session);

    return i_session;
}

/**
 * @brief set up periodic callback to run cleanup and such
 *
 * @param session  session tracked by manager
 */
void iotm_connected_devices_periodic(
        struct iotm_session *session)
{
    if (session->topic == NULL) return;
    // any periodic work here
    return;
}

void iotm_connected_devices_exit(
        struct iotm_session *session)
{
    LOGI("%s: Cleaning and exit from the connected plugin session.\n",
            __func__);
    struct iotm_connected_devices_cache *mgr;
    mgr = iotm_connected_get_mgr();
    if (!mgr->initialized) return;

    session->ops.remove_rules(connect_rules, NUM_CONNECT_RULES);

    // cleanup logic if necessary here
    iotm_connected_delete_session(session);
}

/**
 * @brief this method is called any time there are ovsdb updates
 *
 * @param session  contains current state reflected in OVSDB
 */
void iotm_connected_devices_update(
        struct iotm_session *session)
{
    return;
}

bool is_disconnect(char *type)
{
    if (strcmp(DISCONNECT, type) == 0) return true;
    return false;
}

bool is_connect(char *type)
{
    if (strcmp(CONNECT, type) == 0) return true;
    return false;
}

/**
 * @brief passthrough utility to allow foreach iterator to build up new OVSDB
 * row 
 */
struct tag_builder_t
{
    struct schema_Openflow_Tag *row; /**< row to be inserted into openflow */
    char *tag; /**< tag to add or remove */
    bool is_adding; /**< whether the tag is being added or removed */
    bool found; /**< whether value was found currently in row */
};

/**
 * @brief add a string tag to the Openflow_Tag row
 *
 * @param row   struct that is being built for insertion, will append new tag
 * @param tag   value to append to device_value set
 */
void push_tag_to_row(struct schema_Openflow_Tag *row, char *tag)
{
    int index = row->device_value_len;
    strcpy(row->device_value[index], tag);
    row->device_value_len += 1;
}

void add_name_to_row(struct schema_Openflow_Tag *row, char *name)
{
    strcpy(row->name, name);
}

/**
 * @brief called for every current tag value
 *
 * @note utility to build a OVSDB update matching the current data stored in
 * memory
 *
 * @note either checks to make sure the value isn't already in OVSDB if we are
 * trying to add a value, or 'removes' the value by not adding the tag to the
 * row being built
 *
 * @param name   name of tag, i.e. iot_connected_devices
 * @param value  value of tag, a mac, uuid, etc.
 * @param ctx    reference to tag_builder_t which contains row and metadata
 */
void tag_row_builder(char *name, char *value, void *ctx)
{
    struct tag_builder_t *tag_row = (struct tag_builder_t *) ctx;
    if (tag_row == NULL) return;

    if (tag_row->is_adding) // make sure add isn't redundant
    {
        if (strcmp(value, tag_row->tag) == 0) tag_row->found = true;
    }
    else // remove tag from row (just don't add)
    {
        if (strcmp(value, tag_row->tag) == 0) return;
    }
    
    push_tag_to_row(tag_row->row, value);
}

int build_tag_row_update(
        struct iotm_session *session,
        struct plugin_command_t *command,
        struct schema_Openflow_Tag *row)
{
    if (session == NULL
            || command == NULL
            || row == NULL) return -1;

    add_name_to_row(row, CONNECT_TAG);

    struct tag_builder_t builder =
    {
        .row = row,
        .tag = command->ops.get_param(command, MAC_KEY),
    };

    if (is_connect(command->action))
    {
        builder.is_adding = true;
        session->ops.foreach_tag(CONNECT_TAG, tag_row_builder, &builder);
        if (!builder.found) push_tag_to_row(row, builder.tag);
        return 0;
    }
    else if (is_disconnect(command->action))
    {
        builder.is_adding = false;
        session->ops.foreach_tag(CONNECT_TAG, tag_row_builder, &builder);
        return 0;
    }
    return -1;
}

/**
 * @brief handler for when events are passed from IOTM
 */
void iotm_connected_devices_handle(
        struct iotm_session *session,
        struct plugin_command_t *command)
{
    if (command == NULL || command->action == NULL) return;

    struct schema_Openflow_Tag update;
    memset(&update, 0, sizeof(update));

    int err = build_tag_row_update(session, command, &update);

    if (!err)
    {
        err = session->ops.update_tag(update.name, &update);
        if (err) LOGE("%s: Update for tag failed.", __func__);
    }
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
iotm_connected_devices_init(struct iotm_session *session)
{
    LOGI("Initializing the Connected Device plugin, version [%s]", CD_VERSION);
    struct iotm_connected_devices_cache *mgr;
    struct iotm_connected_session *iotm_connected_session;

    if (session == NULL) return -1;

    mgr = iotm_connected_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(
                &mgr->iotm_sessions,
                iotm_connected_session_cmp,
                struct iotm_connected_session,
                session_node);

        mgr->initialized = true;
    }

    /* Look up the iotm connected session */
    iotm_connected_session= iotm_connected_lookup_session(session);

    if (iotm_connected_session == NULL)
    {
        LOGE("%s: could not allocate iotm_connected parser", __func__);
        return -1;
    }

    // Load plugin defined function pointers
    session->ops.periodic = iotm_connected_devices_periodic;
    session->ops.update = iotm_connected_devices_update;
    session->ops.exit = iotm_connected_devices_exit;
    session->ops.handle = iotm_connected_devices_handle;
    session->ops.rule_update = no_op_rule_update;
    session->ops.tag_update = no_op_tag_update;


    // load in related rules
    for ( size_t i = 0; i < NUM_CONNECT_RULES; i++ )
    {
        // make sure all rules have the manager name (defined in
        // IOT_Manager_Config) as the routeable action
       strcpy(connect_rules[i].actions_keys[0], session->name);
       LOGI("%s: Processing rule [%s] for installation.", __func__, connect_rules[i].name);
    }
    session->ops.update_rules(connect_rules, NUM_CONNECT_RULES);

    return 0;
}
