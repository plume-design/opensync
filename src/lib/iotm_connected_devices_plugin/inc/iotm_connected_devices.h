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
#ifndef IOTM_CONNECTED_DEVICES_H
#define IOTM_CONNECTED_DEVICES_H

/**
 * @file iotm_connected_devices.h
 *
 * @brief Plugin that keeps track of all devices currently connected to the
 * system
 *
 * @note All currently connected devices are tracked in the Openflow_Tag
 * matcheing the CONNECT_TAG.
 *
 * The rules matching the connect_rules for the plugin allow for mapping of
 * protocol specific events to a simple connect and disconnect event. Default
 * rules are loaded by the plugin, extra protocols may be loaded by the cloud.
 */

#include "iotm.h"
#include "ds_tree.h"

#define MAC_KEY "mac"
#define CONNECT "connect"
#define DISCONNECT "disconnect"

#define CD_VERSION "0.0.2"

/**
 * @brief the plugin cache, a singleton tracking instances
 *
 * The cache tracks the global initialization of the plugin
 * and the running sessions.
 */
struct iotm_connected_devices_cache
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
struct iotm_connected_session
{
    struct iotm_session *session;
    bool initialized;
    ds_tree_t session_devices;
    ds_tree_node_t session_node;
};


int iotm_connected_devices_init(struct iotm_session *session);
void iotm_connected_devices_exit(struct iotm_session *session);

/**
 * @brief based off the command update the connected device tags
 *
 * @param       session   session tracked by IOTM, access to stored tags
 * @param       command   IoT command that may lead to update
 * @param[out]   row       if not NULL, row that needs to update the tag table
 *
 * @return 0    built update, loaded into row
 * @return -1   not of type that required update, or error occured
 */
int build_tag_row_update(
        struct iotm_session *session,
        struct plugin_command_t *command,
        struct schema_Openflow_Tag *row);

void iotm_connected_devices_handle(
        struct iotm_session *session,
        struct plugin_command_t *command);


/**
 * @brief check whether type is a disconnect event or not
 *
 * @param type  type of event, such as ble_disconnected
 *
 * @return true  type matches one of our disconnect types
 */
bool is_disconnect(char *type);

/**
 * @brief check whether type is a connect event or not
 *
 * @param type  type of event, such as ble_connected
 *
 * @return true  type matches one of our connect types
 */
bool is_connect(char *type);


#endif /* IOTM_CONNECTED_DEVICES_H */
