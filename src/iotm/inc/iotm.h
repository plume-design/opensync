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

#ifndef IOTM_H_INCLUDED
#define IOTM_H_INCLUDED

/**
 * @file iotm.h
 *
 * @brief top level header for manager
 *
 * Includes mgr struct and data shared with plugins
 */

#include "qm_conn.h"
#include "ds_tree.h"
#include "ds_list.h"
#include "evsched.h"     /* ev helpers */
#include "log.h"         /* Logging routines */
#include "json_util.h"   /* json routines */
#include "os.h"          /* OS helpers */
#include "ovsdb.h"       /* ovsdb helpers */
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "ovsdb_utils.h"
#include "target.h"           /* target API */

#include "iotm_session.h"
#include "iotm_rule.h"
#include "iotm_event.h"
#include "iotm_list.h"
#include "iotm_plug_command.h"
#include "iotm_plug_event.h"
#include "iotm_tl.h"
#include "iotm_data_types.h"

#define CONNECT_TAG "iot_connected_devices"
#define MAC_KEY "mac"



/**
 * @brief contains data used globally within the manager
 */
struct iotm_mgr
{
    struct tl_context_tree_t *tl_ctx_tree;   /**< allows a plugin to look up a context by a key */
    ds_tree_t *mqtt_headers;  /**< MQTT headers presented by ovsdb */
    iotm_tree_t *tags;        /**< oftags currently tracked by OVSDB */
    char *location_id;        /**< convenient mqtt location id pointer */
    char *node_id;            /**< convenient mqtt node id pointer */
    ds_tree_t events;         /**< rb tree containing events with rules associated */
    ds_tree_t iotm_sessions;  /**< rb tree containing plugin sessions */
    ev_timer timer;           /**< manager's event timer */
    struct ev_loop *loop;     /**< event loop */
    time_t periodic_ts;       /**< manager's periodic timestamp */
    char pid[16];             /**< manager's pid */
    void *tl_context;         /**< global context, to be used by target layer */
    bool (*init_plugin)(struct iotm_session *); /**< DSO plugin init */
};

/** @name EntryExitMethods
 * @brief Initialization and cleanup for manager resources
 */
///@{
/**
 * @brief all setup required for manager
 *
 * @param loop event loop for IoTM
 */
void iotm_init_mgr(struct ev_loop *loop);

/**
 * @brief clean up and free all data
 */
int iotm_teardown_mgr(void);

///@}


/**
 * @name IoT Helpers
 *
 * @brief methods that enable interacting with IoT Data Structures
 */
///@{
/**
 * @brief Get the manager struct that is static within the program
 */
struct iotm_mgr *iotm_get_mgr(void);

/**
 * @note get current tag tree
 */
struct iotm_tree_t *iotm_get_tags(void);
/**
 * @brief get all iot events
 */
ds_tree_t *iotm_get_events();

/**
 * @brief lookup an event such as 'connect' in the session
 *
 * @param session   a session tracked for a plugin
 * @param ev        name of event, maps to name in OVSDB
 */
struct iotm_event *get_event(struct iotm_session *session, char *ev);

/**
 * @brief get an event node and all associated rules
 *
 * @param name    key for event mapping to OVSDB string
 *
 * @return event  node
 */
struct iotm_event *iotm_event_get(char *ev_key);

/**
 * @brief retrieve a rule
 *
 * @param name name of the rule as matched in OVSDB
 * @param ev_key  event that the rule is bound to
 *
 * @return rule node
 */
struct iotm_rule *iotm_get_rule(char *name, char *ev_key);

/**
 * @brief get all current plugin sessions
 */
ds_tree_t *
iotm_get_sessions(void);
///@}

#endif // IOTM_H_INCLUDED */
