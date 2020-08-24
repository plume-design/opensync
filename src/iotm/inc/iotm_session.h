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

#ifndef IOTM_SESSION_H_INCLUDED
#define IOTM_SESSION_H_INCLUDED
/**
 * @file iotm_session.h
 *
 * @brief container for all data shared with plugin
 *
 * @note a session is an instance of a Plugin. Each row of IOT_Manager_Config
 * corresponds to a session node.
 */

#include "iotm_plug_command.h"
#include "iotm_plug_event.h"
#include "iotm_rule.h"
#include "ovsdb.h"       /* ovsdb helpers */
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "ovsdb_utils.h"

struct iotm_session;

/**
 * @brief function pointers for methods shared between manager and plugin
 *
 * @note some of these methods are provided to the plugin. This allows the
 * plugin to interact with data that is stored in an opaque way by the manager,
 * as well as route data back into the manager.
 */
struct iotm_session_ops
{
    /**< Function Pointers provided TO the plugin */
    void (*emit)(struct iotm_session *, struct plugin_event_t *event); /**< Emit that an IoT event has been observed. Provided to the plugin. */
    void (*send_report)(struct iotm_session *, char *); /**< MQTT json report sending routine. Provided to the plugin  */
    void (*send_pb_report)(struct iotm_session *, char *, void *, size_t); /**< MQTT protobuf report sending routine. Provided to the plugin  */
    struct iotm_event *(*get_event)(struct iotm_session *session, char *ev); /**< retrieve an event and all pertaining rules */
    char * (*get_config)(struct iotm_session *, char *key); /**< other_config parser. Provided to the plugin  */
	struct plugin_event_t *(*plugin_event_new)(); /**< allocate a new plugin event, plugin builds event and then emits it */
    void (*foreach_tag)(char *tag, void (*cb)(char *key, char *value, void *ctx), void *ctx); /**< iterate over each tag matching passed tag, if tag is null iterate over all */
    int (*update_tag)(char *name, struct schema_Openflow_Tag *row); /**< upserts a tag row with new tags. Provided to the plugin. */
    int (*update_rules)(struct schema_IOT_Rule_Config rows[], size_t num_rules); /**< upserts the rule to the rule table. Provided to the plugin. */
    int (*remove_rules)(struct schema_IOT_Rule_Config rows[], size_t num_rules); /**< remove the rule from the rule table. Provided to the plugin. */

    /**< Function Pointers provided BY the plugin */
    void (*handle)(struct iotm_session *, struct plugin_command_t *command); /**< React to an IoT event. Provided by plugin. */
    void (*update)(struct iotm_session *); /**< ovsdb update callback. Provided by the plugin  */
    void (*tag_update)(struct iotm_session *); /**< tags have updated, provided by plugin */
    void (*rule_update)(struct iotm_session *, ovsdb_update_monitor_t *mon, struct iotm_rule *); /**< ovsdb rule table has updated, inform plugin and send new rule. Provided to the plugin. */
    void (*periodic)(struct iotm_session *); /**< periodic callback. Provided by the plugin  */
    void (*exit)(struct iotm_session *); /**< plugin exit. Provided by the plugin  */
};

/**
 * @brief stores all data related to the plugin, passed to each plugin method
 *
 * The session is the main structure exchanged with the service plugins.
 * Function pointers, OVSDB information, and things that are tracked by IoTM
 * are accessible through this struct.
 */
typedef struct iotm_session
{
    struct iotm_session_conf *conf;   /**< ovsdb configuration */
    struct iotm_session_ops ops;      /**< session function pointers */
    ds_tree_t *events;                /**< events relevant to plugin with rules */
    ds_tree_t *mqtt_headers;         /**< mqtt headers from AWLAN_Node */
    char *name;                      /**< convenient session name pointer */
    char *topic;                     /**< convenient mqtt topic pointer */
    char *location_id;               /**< convenient mqtt location id pointer */
    char *node_id;                   /**< convenient mqtt node id pointer */
    void *handler_ctxt;              /**< session private context */
    void *handle;                    /**< plugin dso handle */
    char *dso;                       /**< plugin dso path */
    struct tl_context_tree_t *tl_ctx_tree;   /**< allows a plugin to look up a context by a key */
    int64_t report_count;            /**< mqtt reports counter */
    struct ev_loop *loop;            /**< event loop */
    struct iotm_session *service;     /**< service provider */
    ds_tree_node_t iotm_sess_node;         /**< session manager node handle */
} iotm_session;

/**
 * @brief session representation of the OVSDB related table
 *
 * @note Mirrors the IOT_Manager_Config table contents
 */
struct iotm_session_conf
{
    char *handler;             /**< Session unique name      */
    char *plugin;              /**< Session's service plugin */
    ds_tree_t *other_config;   /**< Session's private config */
};

/**
 * @brief add a session based on OVSDB configuration
 *
 * @param conf   OVSDB row to add to tracked session
 */
void iotm_add_session(struct schema_IOT_Manager_Config *conf);

/**
 * @brief free members of a session
 *
 * @param session  node to free
 */
void iotm_free_session(struct iotm_session *session);

/**
 * @brief get a session by name from it's respective tree
 *
 * @param sessions   tree to perform lookup in
 * @param name       name of session
 *
 * @return session   pointer to session matching name
 * @return NULL      no session found
 */
struct iotm_session *get_session(ds_tree_t *sessions, char *name);

/**
 * @brief remove the session matching the OVSDB row
 *
 * @param conf   configuration row for removal
 */
void iotm_delete_session(struct schema_IOT_Manager_Config *conf);

/**
 * @brief retrieves the value from the provided key in the
 * other_config value
 *
 * @param session the iotm session owning the other_config
 * @param conf_key other_config key to look up
 */
char *iotm_get_other_config_val(struct iotm_session *session, char *key);

/**
 * @brief iterate over each tag in the session
 *
 * @param tag  name of a tag, i.e. "iot_connected_devices", if NULL will pass
 * every tag
 * @param cb   callback for each tag match
 * @param ctx  passthrough provided to caller
 *
 */
void session_foreach_tag(
        char *tag,
        void (*cb)(char *key, char *value, void *ctx),
        void *ctx);

#endif // IOTM_SESSION_H_INCLUDED */
