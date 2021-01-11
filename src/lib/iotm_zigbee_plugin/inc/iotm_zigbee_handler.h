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

#ifndef IOTM_ZIGBEE_HANDLER_H_INCLUDED
#define IOTM_ZIGBEE_HANDLER_H_INCLUDED

#include "iotm.h"
#include "ds_tree.h"
#include "target_zigbee.h"
#include "iotm_zigbee_handler_private.h"

/**
 * @file iotm_zigbee_handler.h
 *
 * @brief contains definitions that link into IoTM
 */


#define TL_KEY "zigbee"
#define ZB_VERSION "0.0.1"
/**
 * @brief valid parameter  keys for zigbee
 */
#define ZB_MAC                 "mac"
#define ZB_INPUT_CLUSTER       "input_cluster"
#define ZB_OUTPUT_CLUSTER      "output_cluster"
#define ZB_CLUSTER_ID          "cluster_id"
#define ZB_DEVICE_ID           "device_id"
#define ZB_PROFILE_ID          "profile_id"
#define ZB_NODE_ADDRESS        "node_address"
#define ZB_ATTR_ID             "attribute_id"
#define ZB_ATTR_START_ID       "attribute_start_id"
#define ZB_ATTR_DATA_TYPE      "attribute_data_type"
#define ZB_MAX_ATTR            "max_attributes"
#define ZB_CMD_ID              "command_id"
#define ZB_START_CMD_ID        "start_command_id"
#define ZB_MAX_CMDS            "max_commands"
#define ZB_EP                  "endpoint"
#define ZB_EP_FILT             "endpoint_filter"
#define ZB_PARAM_DATA          "param_data"
#define ZB_STATUS              "status_code"
#define ZB_DATA                "data"
#define ZB_DATA_LEN            "data_len"
#define ZB_IS_REPORT           "is_report"
#define ZB_IS_REPORT_CONF      "is_reporting_configured"
#define ZB_MIN_REPORT_INT      "min_report_interval"
#define ZB_MAX_REPORT_INT      "max_report_interval"
#define ZB_TIMEOUT             "timeout_period"
#define ZB_ERR                 "error"
#define DECODE_TYPE            "decode_type"

// device pairing defines
#define ZB_PAIRING_ENABLE "zigbee_permit_joining"
#define ZB_PAIRING_START "permit_joining_start_epoch"
#define ZB_PAIRING_DURATION "permit_joining_timeout"


/**
 * @brief called when IoTM initialized / loads the plugin
 *
 * @param session  session tracked by iotm
 */
int iotm_zigbee_handler_init(struct iotm_session *session);

/**
 * @brief called when plugin is being unloaded
 */
void iotm_zigbee_handler_exit(struct iotm_session *session);


/**
 * @brief set up periodic callback to run cleanup and such
 *
 * @param session  session tracked by manager
 *
 * @note no-op for zigbee
 */
void iotm_zigbee_handler_periodic(struct iotm_session *session);

/**
 * @brief this method is called any time there are ovsdb updates
 *
 * @param session  contains current state reflected in OVSDB
 *
 * @note no-op for zigbee implementation
 */
void iotm_zigbee_handler_update(struct iotm_session *session);

/**
 * @brief ran any time IOT_Rule_Config updates
 *
 * @param session  tracked by manager
 * @param mon      type of rule change
 * @param rule     rule that changed
 */
void zigbee_rule_update(
        struct iotm_session *session,
        ovsdb_update_monitor_t *mon,
        struct iotm_rule *rule);

/**
 * @brief ran when the tags update
 */
void no_op_tag_update(struct iotm_session *session);
/**
 * @brief handler for when a command is routed to the plugin
 *
 * @param session  session tracked by iotm - instance of plugin
 * @param command  command that needs to be processed
 */
void iotm_zigbee_handler(
        struct iotm_session *session,
        struct plugin_command_t *command);

#endif /* IOTM_ZIGBEE_HANDLER_H_INCLUDED */
