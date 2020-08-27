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

#ifndef TARGET_ZIGBEE_H
#define TARGET_ZIGBEE_H

#define ZIGBEE_MAX_MAC_LEN 24
#include "stdint.h"
#include "stdbool.h"
#include <ev.h>

/**
 * @brief data used to perform a write
 */
typedef struct {
  uint8_t *data;
  size_t data_length;
} zb_barray_t;

typedef char      zigbee_mac_t[ZIGBEE_MAX_MAC_LEN];  /**< 64-bit IEEE Address, eg. 00:00:00:00:00:00:00:00 */
typedef uint16_t  zigbee_node_addr_t;  /**< 16-bit PAN ID, eg. 0x2136 */
typedef uint8_t   zigbee_ep_id_t;  /**< 8-bit Endpoint ID, eg. 0x01 */
typedef uint16_t  zigbee_cluster_id_t;  /**< 16-bit Cluster ID, eg. 0x0001 */
typedef uint16_t  zigbee_attr_id_t;  /**< 16-bit Attribute ID, eg. 0x0000 */
typedef uint8_t   zigbee_command_id_t;  /**< 8-bit Command ID, eg. 0x00 */
typedef uint16_t  zigbee_profile_id_t;  /**< 16-bit Zigbee Profile ID, eg. 0x0104 (HA) */
typedef uint16_t  zigbee_device_id_t;  /**< 16-bit Zigbee Profile ID, eg. 0x0100 (On/Off Light) */

typedef struct zigbee_cluster_t
{
    zigbee_ep_id_t       ep_id;  /**< id of endpoint that exposes cluster */
    zigbee_cluster_id_t  cl_id;  /**< id of cluster */
} zigbee_cluster_t;

/**
 * Zigbee Input Structs (parameters for command)
 */
typedef struct zigbee_permit_join_params_t
{
    uint8_t  duration;  /**< number of seconds to permit joining. valid range 1-254 */
} zigbee_permit_join_params_t;

typedef struct zigbee_discover_endpoints_params_t
{
    zigbee_ep_id_t  *endpoint_filter;  /**< endpoints we are interested in */
    uint8_t                num_endpoint_filters;  /**< number of endpoints we are interested in */
} zigbee_discover_endpoints_params_t;

typedef struct zigbee_read_attributes_params_t
{
    zigbee_cluster_t  cluster;  /**< cluster that exposes attributes */
    zigbee_attr_id_t  *attribute;  /**< list of attributes to read */
    uint8_t                  num_attributes;  /**< number of attributes in list */
} zigbee_read_attributes_params_t;

typedef struct zigbee_write_attributes_params_t
{
    zigbee_cluster_t  cluster;  /**< cluster that exposes attributes */
    zb_barray_t                 data;  /**< writeAttributeRecords data to send */
} zigbee_write_attributes_params_t;

typedef struct zigbee_configure_reporting_params_t
{
    zigbee_cluster_t  cluster;  /**< cluster that exposes attributes */
    zb_barray_t                 data;  /**< configureReportingRecords data to send */
} zigbee_configure_reporting_params_t;

typedef struct zigbee_read_reporting_configuration_params_t
{
    zigbee_cluster_t  cluster;  /**< cluster that exposes attributes */
    zigbee_attr_id_t  *attribute;  /**< list of attributes to read reporting configuration */
    uint8_t                  num_attributes;  /**< number of attributes in list */
} zigbee_read_reporting_configuration_params_t;

typedef struct zigbee_discover_attributes_params_t
{
    zigbee_cluster_t  cluster;  /**< cluster that exposes attributes */
    zigbee_attr_id_t  start_attribute_id;  /**< attribute id to begin discovery, eg. 0x0000 */
    uint8_t                  max_attributes;  /**< maximum number of attributes to discover, eg. 0xFF */
} zigbee_discover_attributes_params_t;

typedef struct zigbee_discover_commands_received_params_t
{
    zigbee_cluster_t     cluster;  /**< cluster that exposes commands */
    zigbee_command_id_t  start_command_id;  /**< command id to begin discovery, eg. 0x00 */
    uint8_t                     max_commands;  /**< maximum number of commands to discover, eg. 0xFF */
} zigbee_discover_commands_received_params_t;

typedef struct zigbee_discover_commands_generated_params_t
{
    zigbee_cluster_t     cluster;  /**< cluster that exposes commands */
    zigbee_command_id_t  start_command_id;  /**< command id to begin discovery, eg. 0x00 */
    uint8_t                     max_commands;  /**< maximum number of commands to discover, eg. 0xFF */
} zigbee_discover_commands_generated_params_t;

typedef struct zigbee_send_cluster_specific_command_params_t
{
    zigbee_cluster_t     cluster;  /**< cluster that exposes command */
    zigbee_command_id_t  command_id;  /**< command id */
    zb_barray_t                    *data;  /**< data to send with command. NULL if command does not require data. */
} zigbee_send_cluster_specific_command_params_t;

/**
 * Zigbee Output Structs (results of event)
 */
typedef enum zigbee_event_type
{
    ZIGBEE_UNKNOWN, /**< event doesnt match defined enum */
    ZIGBEE_ERROR, /**< error emitted from radio or device */
    ZIGBEE_DEVICE_ANNCED,
    ZIGBEE_EP_DISCOVERED,
    ZIGBEE_ATTR_DISCOVERED,
    ZIGBEE_COMM_RECV_DISCOVERED,
    ZIGBEE_COMM_GEN_DISCOVERED,
    ZIGBEE_ATTR_VALUE_RECEIVED,
    ZIGBEE_ATTR_WRITE_SUCCESS,
    ZIGBEE_REPORT_CONFIGED_SUCCESS,
    ZIGBEE_REPORT_CONFIG_RECEIVED,
    ZIGBEE_DEFAULT_RESPONSE,
} zigbee_event_type;

/**
 * @Structures used for event emission from Zigbee Target Layer
 */
typedef struct zigbee_error_t {
    zigbee_event_type  op; /**< type of event */
    char               *error; /**< readout of error */
    void               *params; /**< parameters used for event */
} zigbee_error_t;

typedef struct zigbee_annce_t
{
    zigbee_node_addr_t  node_addr;  /**< NWK address of the remote device */
} zigbee_annce_t;

typedef struct zigbee_device_annced_t
{
    zigbee_annce_t  contents;  /**< contents of Zigbee device_annce message */
} zigbee_device_annced_t;

typedef struct zigbee_endpoint_t
{
    zigbee_ep_id_t       ep;  /**< id of endpoint */
    zigbee_profile_id_t  profile_id;  /**< id of profile that is supported on this endpoint */
    zigbee_device_id_t   device_id;  /**< id of device description that is supported on this endpoint */
    uint8_t                     input_cluster_count;  /**< number of input clusters exposed on this endpoint */
    zigbee_cluster_id_t  *input_clusters;  /**< list of input clusters exposed on this endpoint */
    uint8_t                     output_cluster_count;  /**< number of output clusters exposed on this endpoint */
    zigbee_cluster_id_t  *output_clusters;  /**< list of output clusters exposed on this endpoint */
} zigbee_endpoint_t;

typedef struct zigbee_ep_discovered_t
{
    zigbee_discover_endpoints_params_t  params;  /**< discover endpoints command paramaters that triggered event */
    zigbee_endpoint_t                   contents;  /**< contents of Zigbee endpoint */
} zigbee_ep_discovered_t;

typedef struct zigbee_attribute_t
{
    zigbee_cluster_t  cluster;  /**< cluster that exposes attribute */
    zigbee_attr_id_t  attr_id;  /**< id of attribute */
    uint8_t                  attr_data_type;  /**< ZCL data type of attribute */
} zigbee_attribute_t;

typedef struct zigbee_attr_discovered_t
{
    zigbee_discover_attributes_params_t  params;  /**< discover attributes command paramaters that triggered event */
    zigbee_attribute_t                   contents;  /**< contents of Zigbee attribute */
} zigbee_attr_discovered_t;

typedef struct zigbee_comm_recv_t
{
    zigbee_cluster_t     cluster;  /**< cluster that exposes command */
    zigbee_command_id_t  comm_id;  /**< id of command */
} zigbee_comm_recv_t;

typedef struct zigbee_comm_recv_discovered_t
{
    zigbee_discover_commands_received_params_t  params;  /**< discover commands received command paramaters that triggered event */
    zigbee_comm_recv_t                          contents;  /**< contents of Zigbee command received */
} zigbee_comm_recv_discovered_t;

typedef struct zigbee_comm_gen_t
{
    zigbee_cluster_t     cluster;  /**< cluster that exposes command */
    zigbee_command_id_t  comm_id;  /**< id of command */
} zigbee_comm_gen_t;

typedef struct zigbee_comm_gen_discovered_t
{
    zigbee_discover_commands_generated_params_t  params;  /**< discover commands generated command paramaters that triggered event */
    zigbee_comm_gen_t                            contents;  /**< contents of Zigbee command generated */
} zigbee_comm_gen_discovered_t;

typedef struct  zigbee_attr_value_t
{
    zigbee_cluster_t  cluster;  /**< cluster that exposes attribute */
    zigbee_attr_id_t  attr_id;  /**< id of attribute */
    uint8_t           attr_data_type;  /**< ZCL data type of attribute */
    zb_barray_t       attr_value;  /**< attribute value */
} zigbee_attr_value_t;

typedef struct  zigbee_attr_value_received_t
{
    bool  is_report; /**< True if result of report, false if read */
    union
    {
        zigbee_read_attributes_params_t      a_params; /**< If this is the result of a read, access data here */
        zigbee_configure_reporting_params_t  r_params; /**< If this is the result of a report, access data here */
    };
    zigbee_attr_value_t  contents; /**< Data of attribute value */
} zigbee_attr_value_received_t;

typedef struct zigbee_write_success_t
{
    uint8_t  s_code;  /**< status code of zigbee write command */
} zigbee_write_success_t;

typedef struct zigbee_attr_write_t
{
    zigbee_cluster_t  cluster;  /**< cluster that exposes attribute */
    zigbee_attr_id_t  attr_id;  /**< id of attribute */
    zigbee_write_success_t   write;  /**< status of zigbee write command */
} zigbee_attr_write_t;

typedef struct zigbee_attr_write_success_t
{
    zigbee_write_attributes_params_t  params;  /**< write attributes command paramaters that triggered event */
    zigbee_attr_write_t               contents;  /**< contents of Zigbee write attribute */
} zigbee_attr_write_success_t;

typedef struct zigbee_report_success_t
{
    uint8_t  s_code;  /**< status code of zigbee configure reporting command */
} zigbee_report_success_t;

typedef struct zigbee_report_configured_t
{
    zigbee_cluster_t  cluster;  /**< cluster that exposes attribute */
    zigbee_attr_id_t  attr_id;  /**< id of attribute */
    zigbee_report_success_t  report;  /**< status of zigbee configure reporting command */
} zigbee_report_configured_t;

typedef struct zigbee_report_configured_success_t
{
    zigbee_configure_reporting_params_t  params;  /**< configure reporting command paramaters that triggered event */
    zigbee_report_configured_t           contents;  /**< contents of Zigbee configure reporting */
} zigbee_report_configured_success_t;

typedef struct zigbee_report_config_record_t
{
    uint8_t   attr_data_type;  /**< ZCL data type of attribute */
    uint16_t  min_report_interval;  /**< Minimum reporting interval in seconds */
    uint16_t  max_report_interval;  /**< Maximum reporting interval in seconds */
    uint16_t  timeout_period;  /**< Maximum expected time, in seconds, between received reports */
} zigbee_report_config_record_t;

typedef struct zigbee_report_config_t
{
    zigbee_cluster_t        cluster;  /**< cluster that exposes attribute */
    zigbee_attr_id_t        attr_id;  /**< id of attribute */
    bool                           r_configured;  /**< indicates that reporting is configured for this attribute */
    zigbee_report_config_record_t  *record;  /**< values of zigbee reporting config. NULL if r_configured == false */
} zigbee_report_config_t;

typedef struct zigbee_report_config_received_t
{
    zigbee_read_reporting_configuration_params_t  params;  /**< read reporting configuration command paramaters that triggered event */
    zigbee_report_config_t                        contents;  /**< contents of Zigbee reporting configuration */
} zigbee_report_config_received_t;

typedef struct zigbee_default_response_status_t
{
    uint8_t  s_code;  /**< status code of zigbee default response */
} zigbee_default_response_status_t;

typedef struct zigbee_default_response_t
{
    zigbee_cluster_t           cluster;  /**< cluster that exposes command */
    zigbee_command_id_t        comm_id;  /**< id of command that response is for */
    zigbee_default_response_status_t  status;  /**< status of command */
} zigbee_default_response_t;

typedef struct zigbee_default_response_received_t
{
    zigbee_send_cluster_specific_command_params_t  params;  /**< cluster specific command paramaters that triggered event */
    zigbee_default_response_t                      contents;  /**< contents of Zigbee default response */
} zigbee_default_response_received_t;

/**
 * @These are the Zigbee data structures that can be populated and emitted,
 * based on the corresponding event type
 */
typedef union zigbee_operation
{
    zigbee_error_t                      error; /**< some error occurred */
    zigbee_device_annced_t              device_annced; /**< a new device has joined the network */
    zigbee_ep_discovered_t              ep_discovered; /**< a new endpoint has been discovered */
    zigbee_attr_discovered_t            attr_discovered; /**< a new attribute has been discovered */
    zigbee_comm_recv_discovered_t       comm_recv_discovered; /**< a new command received has been discovered */
    zigbee_comm_gen_discovered_t        comm_gen_discovered; /**< a new command generated has been discovered */
    zigbee_attr_value_received_t        attr_value; /**< a new attribute value was received */
    zigbee_attr_write_success_t         attr_write; /**< status of write command issued to remote device */
    zigbee_report_configured_success_t  report_success; /**< status of configure reporting request */
    zigbee_report_config_received_t     report_config; /**< a new reporting configuration was received */
    zigbee_default_response_received_t  default_response; /**< a new default response has been received. provides status of commands issued to a remote device */
} zigbee_operation;

/**
 * @brief Every Zigbee Event is encapsulated in this struct
 */
typedef struct zigbee_event_t
{
    enum zigbee_event_type  type; /**< Operation that emitted the event */
    zigbee_mac_t     mac; /**< mac of device that emitted event */
    union zigbee_operation  op; /**< Data structure containing information about the event */
} zigbee_event_t;

/**
 * @brief callback for Zigbee events
 *
 * @param caller_ctx  pointer to context for caller, loaded with init
 * @param event       pointer to Zigbee event struct
 */
typedef void (*zigbee_event_cb_t)(
        void *caller_ctx,
        zigbee_event_t *event
        );

/**
 * @brief initialization provided to zigbee target layer if required
 *
 * @param context    pointer to context pointer, for target layer to keep relevant data
 * @param caller_ctx context for the caller, should be present in ev_cb
 * @param loop       event loop for use by TL, TL functions MUST not block
 * @param ev_cb      callback function for Zigbee events
 *
 * @return true      initialization finished with no errors
 * @return false     errors occurred during initialization
 */
bool zigbee_init(
        void **context,
        void *caller_ctx,
        struct ev_loop* loop,
        void (*ev_cb)(void *c, zigbee_event_t *e)
        );

/**
 * @brief exit and cleanup the zigbee target layer
 */
bool zigbee_exit(void *context);

/**
 * Zigbee Commands
 */

/**
 * @brief Enable local permit join and broadcast Mgmt_Permit_Join_req
 *
 * @param  context  opaque struct for TL
 * @param  params   param struct to permit join
 *
 * @return true     permit join successfully enabled and message sent
 * @return false    error occurred
 */
bool zigbee_permit_join(
        void *context,
        zigbee_permit_join_params_t params
        );

/**
 * @brief Check if network is currently permitting joining
 *
 * @param  context  opaque struct for TL
 *
 * @return true     network is currently permitting devices to join
 * @return false    network is not open for joining
 */
bool zigbee_network_joining_permitted(
        void *context
        );

/**
 * @brief send network leave request to device
 *
 * @param  context  opaque struct for TL
 * @param  mac      MAC of device for request
 *
 * @return true     command successfully sent to target device
 * @return false    error occurred sending command
 */
bool zigbee_send_network_leave(
        void *context,
        zigbee_mac_t mac
        );

/**
 * @brief discover device's active endpoints, and their clusters
 *
 * @param  context  opaque struct for TL
 * @param  mac      MAC of device for request
 * @param  params   param struct to discover endpoints
 *
 * @return true     command successfully sent to target device
 * @return false    error occurred sending command
 */
bool zigbee_discover_endpoints(
        void *context,
        zigbee_mac_t mac,
        zigbee_discover_endpoints_params_t params
        );

/**
 * @brief read values of attributes
 *
 * @param  context  opaque struct for TL
 * @param  mac      MAC of device for request
 * @param  params   param struct to read values of attributes
 *
 * @return true     command successfully sent to target device
 * @return false    error occurred sending command
 */
bool zigbee_read_attributes(
        void *context,
        zigbee_mac_t mac,
        zigbee_read_attributes_params_t params
        );

/**
 * @brief write values to attributes
 *
 * @param  context  opaque struct for TL
 * @param  mac      MAC of device for request
 * @param  params   param struct to write values of attributes
 *
 * @return true     command successfully sent to target device
 * @return false    error occurred sending command
 */
bool zigbee_write_attributes(
        void *context,
        zigbee_mac_t mac,
        zigbee_write_attributes_params_t params
        );

/**
 * @brief Configure reporting for attribute on device
 *
 * @param  context  opaque struct for TL
 * @param  mac      MAC of device for request
 * @param  params   param struct to configure reporting
 *
 * @return true     command successfully sent to target device
 * @return false    error occurred sending command
 */
bool zigbee_configure_reporting(
        void *context,
        zigbee_mac_t mac,
        zigbee_configure_reporting_params_t params
        );

/**
 * @brief Read reporting configuration for attributes
 *
 * @param  context  opaque struct for TL
 * @param  mac      MAC of device for request
 * @param  params   param struct to read reporting configuration
 *
 * @return true     command successfully sent to target device
 * @return false    error occurred sending command
 */
bool zigbee_read_reporting_configuration(
        void *context,
        zigbee_mac_t mac,
        zigbee_read_reporting_configuration_params_t params
        );

/**
 * @brief Discover attributes implemented on a cluster
 *
 * @param  context  opaque struct for TL
 * @param  mac      MAC of device for request
 * @param  params   param struct to discover attributes
 *
 * @return true     command successfully sent to target device
 * @return false    error occurred sending command
 */
bool zigbee_discover_attributes(
        void *context,
        zigbee_mac_t mac,
        zigbee_discover_attributes_params_t params
        );

/**
 * @brief Discover commands received on a cluster
 *
 * @param  context  opaque struct for TL
 * @param  mac      MAC of device for request
 * @param  params   param struct to discover commands received
 *
 * @return true     command successfully sent to target device
 * @return false    error occurred sending command
 */
bool zigbee_discover_commands_received(
        void *context,
        zigbee_mac_t mac,
        zigbee_discover_commands_received_params_t params
        );

/**
 * @brief Discover commands generated on a cluster
 *
 * @param  context  opaque struct for TL
 * @param  mac      MAC of device for request
 * @param  params   param struct to discover commands generated
 *
 * @return true     command successfully sent to target device
 * @return false    error occurred sending command
 */
bool zigbee_discover_commands_generated(
        void *context,
        zigbee_mac_t mac,
        zigbee_discover_commands_generated_params_t params
        );

/**
 * @brief Send cluster specific command to device
 *
 * @param  context  opaque struct for TL
 * @param  mac      MAC of device for request
 * @param  params   param struct to send cluster specific command
 *
 * @return true     command successfully sent to target device
 * @return false    error occurred sending command
 */
bool zigbee_send_cluster_specific_command(
        void *context,
        zigbee_mac_t mac,
        zigbee_send_cluster_specific_command_params_t params
        );

#endif // TARGET_ZIGBEE_H
