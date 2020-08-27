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

#ifndef IOTM_BLE_HANDLER_H
#define IOTM_BLE_HANDLER_H

/**
 * @file iotm_ble_handler.h
 *
 * @brief top level header for plugin
 */

#include "iotm.h"
#include "target_ble.h"
#include "ds_tree.h"

#define TL_KEY "ble"

#define SCAN_RETRY_COUNT 5
#define SCAN_BACKOFF_TIME 0.3

// All parameter definitions
#define MAC_KEY "mac"
#define WLD_KEY "*"
#define NAME_KEY "name"
#define CONNECT_KEY "connection_status"
#define C_FLAG "c_flag"
#define IS_NOTIFICATION "is_notification"
#define HEX_KEY "hex"
#define DATA "data"
#define DATA_LEN "data_len"



#define DECODE_TYPE "decode_type"
#define DATA "data"
#define P_MAC "mac"
// OVSDB Flag Keys
#define PUBLIC_ADDR "is_public_addr" // connect_param
#define IS_PRIMARY "is_primary" // get if services in list are primary
#define SERV_UUID "serv_uuid" // uuid of service
#define CHAR_UUID "char_uuid" // uuid of characteristic to interact with
#define DESC_UUID "desc_uuid" // uuid of a ble descriptor

#define S_CODE "status_code"


struct adv_sizes_t
{
    bool mac_wld; /**< set if a wildcard is found */
    bool uuid_wld; /**< set if a uuid wildcard is found */
    size_t mac_len; /**< number of mac entries */
    size_t uuid_len; /**< number of uuid entries */
} adv_sizes_t;

struct adv_contents_t
{
    size_t mac_index;
    size_t uuid_index;
    struct ble_discovery_scan_params_t *params;
} adv_contents_t;

enum
{
    BLE_UNKNOWN_CMD,
    BLE_CONNECT_DEVICE,
    BLE_DISABLE_CHARACTERISTIC_NOTIFICATIONS,
    BLE_DISCONNECT_DEVICE,
    BLE_DISCOVER_CHARACTERISTICS,
    BLE_DISCOVER_DESCRIPTORS,
    BLE_DISCOVER_SERVICES,
    BLE_ENABLE_CHARACTERISTIC_NOTIFICATIONS,
    BLE_READ_CHARACTERISTIC,
    BLE_READ_DESCRIPTOR,
    BLE_WRITE_CHARACTERISTIC,
    BLE_WRITE_DESCRIPTOR,
};

struct ble_cmd_t
{
    char *ovsdb_type;
    int command_type;
    void (*handle_cmd)(struct iotm_session *, struct plugin_command_t *);
};

struct ble_type
{
    char *ovsdb_type;
    enum event_type event_type;
	int (*add_params)(struct iotm_session *, struct plugin_event_t *, ble_event_t *event);
};

enum
{
    UNKNOWN,
    HEX,
    UTF8
};

struct decode_type
{
    char *ovsdb_type;
    int type;
    int (*decoder)(char *in, unsigned char *out); /**< decodes string to byte based off encoding */
};

/**
 * @brief Decoder Helpers
 *
 * These allow for conversion from OVDSB strings to byte arrays that can be
 * interpreted by the target layer. There are different strategies for
 * converting encoded strings to bytes, translations may be defined here and
 * added to the struct, allowing for more OVSDB Decode Types
 */
///{@

/**
 * @brief placeholder for unimplemented parsers
 */
int not_impl_to_bytes(char *pos, unsigned char *val);

/**
 * @brief convert a hex string into a byte array
 *
 * @param source_str  hex string, e.g. "DEADBEEF"
 * @param dest_buffer output byte array
 *
 * @return length of output byte array
 */
int hex2bin(char *source_str, unsigned char *dest_buffer);

/**
 * @brief convert a binary array into a hex string
 *
 * @note each byte decodes into 2 letters, i.e. 14 -> 0D
 *
 * @param in     input byte array : i.e. { 14, 12, 0, 3 }
 * @param insz   size of input byte array
 * @param out    output buffer
 * @param outsz  size of outbut buffer
 *
 * @return 0 loaded output buffer
 * @return -1 failed to load output buffer
 */
int bin2hex(unsigned char * in, size_t insz, char * out, size_t outsz);

/**
 * @brief takes a command with data elements loaded and translates into barray
 *
 * @param cmd           command from manager, should have flag DATA to convert
 * @param barray[out]   byte array struct
 *
 * @return 0  loaded byte array
 * @return -1 error loading array
 */
int decode_data_helper(struct plugin_command_t *cmd, barray_t *barray);

///@}

/**
 * @brief convert an ovsdb action type to a target layer command type
 */
const struct ble_cmd_t *ble_cmd_from_string(char *c_type);

/**
 * @brief convert an ovsdb event type to a target layer event type
 */
event_type ble_event_type(char *c_type);

/**
 * @brief the plugin cache, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct iotm_ble_handler_cache
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
 * - a set of devices presented to the session
 */
struct iotm_ble_handler_session
{
    struct iotm_session *session;
    bool initialized;
    struct ble_discovery_scan_params_t current_scan_params;
    ds_tree_t session_devices;
    ds_tree_node_t session_node;
};

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
int iotm_ble_handler_session_cmp(void *a, void *b);

/**
 * @brief set up data and bind plugin handlers
 *
 * @param session  contains data for plugin constructed in manager
 *
 * @note called by manger after dso is loaded, name can be configured in OVSDB
 */
int iotm_ble_handler_init(struct iotm_session *session);

/**
 * @brief cleanup and unload plugin
 */
void iotm_ble_handler_exit(struct iotm_session *session);

/**
 * @brief Frees a iotm ble_handler session
 *
 * @param i_session the iotm ble_handler session to delete
 */
void iotm_ble_handler_free_session(struct iotm_ble_handler_session *i_session);

/**
 * @brief deletes a session
 *
 * @param session the iotm session keying the iot session to delete
 */
void iotm_ble_handler_delete_session(struct iotm_session *session);

/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct iotm_ble_handler_session* iotm_ble_handler_lookup_session(struct iotm_session *session);

/**
 * @brief set up periodic callback to run cleanup and such
 *
 * @param session  session tracked by manager
 */
void iotm_ble_handler_periodic(struct iotm_session *session);

/**
 * @brief handler for when events are passed from IOTM
 *
 * @param session current session data maintained for plugin
 * @param command data containing info about the event and command
 */
void iotm_ble_handle(
        struct iotm_session *session,
        struct plugin_command_t *command);

/**
 * @brief  handle the target layer emitting an event
 *
 * @param context  iotm_session of plugin, passed to tl on init
 * @param event    event signaled from tl
 */
void event_cb(void *context, ble_event_t *event);

/**
 * @brief this method is called any time there are ovsdb updates
 *
 * @param session  contains current state reflected in OVSDB
 */
void
iotm_ble_handler_update(struct iotm_session *session);

/**
 * @brief used to update scanning params on change in advertise rule
 *
 * @param session session data for plugin 
 * @param mon     type of ovsdb transaction
 * @param rule    rule that was modified
 */
void
iotm_ble_handler_rule_update(
        struct iotm_session *session,
        ovsdb_update_monitor_t *mon,
        struct iotm_rule *rule);

/**
 * @brief allocate members of a scan struct
 */
int alloc_scan_filter_params(
        size_t num_macs,
        size_t num_uuids,
        ble_discovery_scan_params_t *params);

/**
 * @brief free members of scan filter
 */
int free_scan_filter_params(ble_discovery_scan_params_t *params);

/**
 * @brief load the filters parameters for the ble advertise scan
 *
 * @param event  'ble_advertised' event, has function to iterate over rules
 * @param[out] params  struct to begin ble scan
 *
 * @note check should be done outside to look for lack of advertise rules
 * this will return NULL for uuid or mac either on wildcard or no match
 *
 * @return 0 success
 * @return -1 failed to allocate
 */
int get_scan_params(
        struct iotm_event *event,
        ble_discovery_scan_params_t *params);

/**
 * @brief handler for when events are passed from IOTM
 */
void iotm_ble_handle(
        struct iotm_session *session,
        struct plugin_command_t *command);

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
iotm_ble_handler_init(struct iotm_session *session);


/** @name Command Handler Functions
 * @brief Initialization and cleanup for manager resources
 */
///@{
int get_connect_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_connect_params_t *params);

void handle_connect(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);

int get_char_notification_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_characteristic_notification_params *params);

void handle_disable_char_notifications(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);

void handle_enable_char_notifications(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);

void handle_disconnect_device(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);

int get_char_discovery_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_characteristic_discovery_params_t *params);

void handle_discover_chars(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);

int get_serv_discovery_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_service_discovery_params_t *params);

void handle_discover_servs(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);

int get_char_read_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_read_characteristic_params_t *params);
void handle_char_read(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);

int get_desc_read_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_read_descriptor_params_t *params);
void handle_desc_read(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);

int get_char_write_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_write_characteristic_params_t *params);
void handle_char_write(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);

int get_desc_write_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_write_descriptor_params_t *params);

void handle_desc_write(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);

void handle_cmd_default(
        struct iotm_session *sess,
        struct plugin_command_t *cmd);
///@}


/** @name Event Filter Handler Functions
 * @brief add relevant filter tags to an IoT event for the manager to use
 */
///@{
int advertised_add(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);

int add_connected_filters(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);

int add_disconnected_params(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);

int add_service_discovery(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);

int add_characteristic_discovery(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);

int add_descriptor_discovery(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);

int add_characteristic_updated(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);

int add_characteristic_write_success(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);

int add_descriptor_write_success(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);

int add_char_notify_success(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);

int default_add(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event);
///@}
#endif /* IOTM_BLE_HANDLER_H */
