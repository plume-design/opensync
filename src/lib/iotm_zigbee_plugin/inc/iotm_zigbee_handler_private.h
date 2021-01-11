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

#ifndef IOTM_ZIGBEE_HANDLER_PRIVATE_H_INCLUDED
#define IOTM_ZIGBEE_HANDLER_PRIVATE_H_INCLUDED

#include "iotm_zigbee_handler.h"

/**
 * @file iotm_zigbee_handler_private.h
 *
 * @brief components internal to the plugin
 */

#define DEFAULT_MAX_ATTR 0xFF


/**
 * @name Zigbee Event Handling
 *
 * @brief events emitted from the target layer are converted to an event for
 * iotm
 */
///@{
/**
 * @brief combines ovsdb string with tl enum and handler method for conversion
 */
struct zigbee_ev_t
{
    char *ovsdb_type; /**< OVSDB */
    zigbee_event_type event_type;
    int (*add_params)(struct plugin_event_t *, zigbee_event_t *event);
};

/**
 * @brief convert enum type into struct with event handler
 *
 * @param type enum matching one of the zigbee events
 *
 * @return struct containing a handler for event generation and the types
 */
const struct zigbee_ev_t *zigbee_event_from_type(zigbee_event_type type);

enum
{
    ZIGBEE_CONFIGURE_REPORTING,
    ZIGBEE_DISCOVER_ATTRIBUTES,
    ZIGBEE_DISCOVER_COMMANDS_GENERATED,
    ZIGBEE_DISCOVER_COMMANDS_RECEIVED,
    ZIGBEE_DISCOVER_ENDPOINTS,
    ZIGBEE_NETWORK_JOINING_PERMITTED,
    ZIGBEE_PERMIT_JOIN,
    ZIGBEE_READ_ATTRIBUTES,
    ZIGBEE_READ_REPORTING_CONFIGURATION,
    ZIGBEE_SEND_CLUSTER_SPECIFIC_COMMAND,
    ZIGBEE_SEND_NETWORK_LEAVE,
    ZIGBEE_WRITE_ATTRIBUTES,
};
///@}

/**
 * @name Zigbee Command Handling
 *
 * @brief commands recieved by iotm are translated into target layer calls
 */
///@{
/**
 * @brief combines ovsdb string with tl enum and handler method for conversion
 */
struct zigbee_cmd_t
{
    char *ovsdb_type; /**< matches OVSDB magic string, such as configure_reporting */
    int command_type; /**< matches TL enum, such as ZIGBEE_CONFIGURE_REPORTING */
    void (*handle_cmd)(struct iotm_session *, struct plugin_command_t *); /**< handler to translate command to TL call */
};

/**
 * @brief access the cmd struct via the OVSDB magic string
 *
 * @param c_type  string representation of the command
 * 
 * @return struct matching type
 * @return NULL failed to find match
 */
const struct zigbee_cmd_t *zigbee_cmd_from_string(char *c_type);
///@}

/**
 * @name Internal Structures
 *
 * @brief Used to track data for session
 */
///@{
/**
 * @brief the plugin cache, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct iotm_zigbee_handler_cache
{
    bool initialized;
    ds_tree_t iotm_sessions;
};

/**
 * @brief a session, instance of processing state and routines.
 *
 * The session provides an executing instance of the services'
 * provided by the plugin.
 * It embeds:
 * - a iotm session
 * - state information
 */
struct iotm_zigbee_session
{
    struct iotm_session *session;
    bool initialized;
    int join_until; /**< epoch time that joining will end*/
    ds_tree_t *timers;
    ds_tree_node_t session_node;
};

/**
 * @brief retrieve an existing zigbee session or allocate a new session
 * 
 * @param session  tracked by iotm, used as key for zigbee session
 *
 * @return NULL      error retrieving or allocating session
 * @return session   populated zigbee session
 */
struct iotm_zigbee_session *iotm_zigbee_lookup_session(struct iotm_session *session);

/**
 * @brief clean up zigbee session
 *
 * @param i_session session to free
 */
void iotm_zigbee_free_session(struct iotm_zigbee_session *i_session);
///@}

/**
 * @name Pairing Utilities
 *
 * @brief timers and methods for tracking when iotm wishes to enable pairing
 */
///@{
struct pairing_node_t;
/**
 * @brief contain data requred to enable zigbee pairing
 */
struct timer_data_t
{
    struct iotm_zigbee_session *zb_session; /**< for viewing current pairing configuration */
    time_t start_time; /**< when the request should start pairing (epoch time) */
    int duration; /**< how long pairing should be enabled */
    struct pairing_node_t *parent; /**< tree node for tracking pair request */
} timer_data_t;

/**
 * @brief tree node for tracking pair request
 *
 * @note useful if this takes place in the future
 */
struct pairing_node_t
{
    long key; /**< unique value, method for looking up in tree */
    ev_timer watcher; /**< ev loop timer structure */
    ds_tree_node_t timer_node; /**< ds structure */
} timer_node_t;

/**
 * @brief allocate and initialize elements of a timer node
 *
 * @param       zb_session   tracks zigbee context for data required by manager
 * @param       start_time   epoch timestamp for when pairing should begin, param
 * @param       duration     how long pairing should be enabled for      
 * @param[out]  pair_node    allocated structure for pairing
 */
int init_pairing_node(
        struct iotm_zigbee_session *zb_session,
        time_t start_time,
        int duration,
        struct pairing_node_t **pair_node);

/**
 * @brief free all elements of a pairing node
 *
 * @param pair_node  node to free
 */
void free_pairing_node(struct pairing_node_t *pair_node);

/**
 * @brief pull parameters out of a iotm_rule
 *
 * @param       rule     param from iotm - should contain parameters for pairing
 * @param[out]  start    if parameter is found this is the epoch start time
 * @param[out]  duration amount of time to pair for
 */
int get_pairing_params(
        struct iotm_rule *rule,
        time_t *start,
        int *duration);

/**
 * @brief check whether the epoch time has already occurred
 *
 * @param start   time to chec
 * 
 * @return true   time is in past
 * @return false  time is not in the past
 */
bool is_in_past(time_t start);

/**
 * @brief  register a new timer with the manager that will begin pairing
 * 
 * @param zb_session  context for plugin, contains timers and info
 * @param node        container for info needed to enable pairing
 *
 * @return 0 successfully added to pairing queue
 * @return -1 error adding to pairing queue
 */
int queue_timer(struct iotm_zigbee_session *zb_session, struct pairing_node_t *node);

/**
 * @brief get the unique key to reference the pair event in the tree
 *
 * @param data   data that needs to be stored
 * 
 * @return key   key to store node with
 */
long get_key(struct timer_data_t *data);

/**
 * @brief enable pairing right now using data in struct
 *
 * @param pair  pairing information
 *
 * @return 0   pairing enabled
 * @return -1  failed to enable pairing
 */
int permit_joining(struct timer_data_t *pair);
///@}

/**
 * @name Data Conversion Helpers
 *
 * @brief Methods to help translate from tl data type to iotm
 */
///@{
/**
 * @brief get binary array from a param
 *
 * @note this allocates the array, caller must free with free_data_param
 *
 * @param      cmd    contains parameters for command
 * @param[out] zb_barray output zb_barray
 *
 * @return 0    zb_barray loaded
 * @return -1   couldn't load zb_barray
 */
int get_data_param(
        struct plugin_command_t *cmd,
        zb_barray_t *zb_barray);

/**
 * @brief free a zb_barray
 *
 * @param zb_barray  object to free
 */
void free_data_param(zb_barray_t *zb_barray);

/**
 * @brief get a uint8 parameter from a command
 *
 * @param       cmd   command to get param from
 * @param       key   i.e. 'attribute_id' that maps to uint8
 * @param[out]  out   uint8 that is loaded on success
 *
 * @return 0   out is loaded
 * @return -1  error getting param
 */
int get_uint8_param(
        struct plugin_command_t *cmd,
        char *key,
        uint8_t *output);

/**
 * @brief get a uint16 parameter from a command
 *
 * @param       cmd   command to get param from
 * @param       key   i.e. 'attribute_id' that maps to uint16
 * @param[out]  out   uint16 that is loaded on success
 *
 * @return 0   out is loaded
 * @return -1  error getting param
 */
int get_uint16_param(
        struct plugin_command_t *cmd,
        char *key,
        uint16_t *out);

/**
 * @brief get a list of uint16 parameters and load them as a list
 *
 * @note output will be allocated, must be freed by caller
 *
 * @param      cmd      iotm command containing parameters
 * @param      key      key matching expected parameter value
 * @param[out] output   will be pointer to uint16 data
 * @param[out] num_out  size of array that was allocated
 *
 * @return 0   allocated the data
 * @return -1  failed to allocate data for find parameters
 */
int alloc_and_load_uint16_params(
        struct plugin_command_t *cmd,
        char *key,
        uint16_t **output,
        uint8_t *num_out);

/**
 * @brief get a list of uint8 parameters and load them as a list
 *
 * @note output will be allocated, must be freed by caller
 *
 * @param      cmd      iotm command containing parameters
 * @param      key      key matching expected parameter value
 * @param[out] output   will be pointer to uint8 data
 * @param[out] num_out  size of array that was allocated
 *
 * @return 0   allocated the data
 * @return -1  failed to allocate data for find parameters
 */
int alloc_and_load_uint8_params(
        struct plugin_command_t *cmd,
        char *key,
        uint8_t **output,
        uint8_t *num_out);

/**
 * @brief  get cluster parameters from a command
 *
 * @param      cmd      command generated in iotm
 * @param[out] cluster  cluster with data loaded in
 *
 * @return 0   cluster data loaded
 * @return -1  error getting param, cluster not loaded
 */
int get_cluster_param(
        struct plugin_command_t *cmd,
        zigbee_cluster_t *cluster);

/**
 * @brief get an attirbute ID from a plugin command
 *
 * @param       cmd  command with string id
 * @param[out]  id   output identifier 
 */
int get_attribute_id(
        struct plugin_command_t *cmd,
        zigbee_attr_id_t *id);
///@}


/**
 * @brief called by target layer any time a zigbee event is recieved
 */
void zigbee_event_cb(void *context, zigbee_event_t *event);

/**
 * @name TL Event Handlers
 * @brief translate target layer event into iotm event
 * @note methods to load a plugin_event_t based off a zigbee event
 */
///@{
int add_default(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);

int add_zigbee_unknown(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event);

int add_zigbee_error(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event);

int add_zigbee_device_annced(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);

int add_zigbee_ep_discovered(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);

int add_zigbee_attr_discovered(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);

int add_zigbee_comm_recv_discovered(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);

int add_zigbee_comm_gen_discovered(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);

int add_zigbee_attr_value_received(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);

int add_zigbee_attr_write_success(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);

int add_zigbee_report_configed_success(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);

int add_zigbee_report_config_received(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);

int add_zigbee_default_response(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event);
///@}

/**
 * @name IoTM Command Handlers
 * @brief translate command from iotm into a target layer function
 */
///@{
void handle_configure_reporting(
        struct iotm_session *session,
        struct plugin_command_t *cmd);

void handle_discover_attributes(
        struct iotm_session *session,
        struct plugin_command_t *cmd);

void handle_discover_commands_generated(
        struct iotm_session *session,
        struct plugin_command_t *cmd);

void handle_discover_commands_received(
        struct iotm_session *session,
        struct plugin_command_t *cmd);

void handle_discover_endpoints(
        struct iotm_session *session,
        struct plugin_command_t *cmd);

void handle_read_attributes(
        struct iotm_session *session,
        struct plugin_command_t *cmd);

void handle_read_reporting_configuration(
        struct iotm_session *session,
        struct plugin_command_t *cmd);

void handle_send_cluster_specific_command(
        struct iotm_session *session,
        struct plugin_command_t *cmd);

void handle_send_network_leave(
        struct iotm_session *session,
        struct plugin_command_t *cmd);

void handle_write_attributes(
        struct iotm_session *session,
        struct plugin_command_t *cmd);
///@}
#endif /* IOTM_ZIGBEE_HANDLER_PRIVATE_H_INCLUDED */
