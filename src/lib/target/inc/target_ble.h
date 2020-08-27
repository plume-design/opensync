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

#ifndef TARGET_BLE_H
#define TARGET_BLE_H

#define BLE_MAX_UUID_LEN 37
#define BLE_MAX_MAC_LEN  18
#include "stdint.h"
#include "stdbool.h"
#include <ev.h>

typedef char ble_uuid_t[BLE_MAX_UUID_LEN];
typedef char ble_mac_t[BLE_MAX_MAC_LEN];

/**
 * @ All events emitted in this enum
 */

typedef enum event_type {
    ERROR, /**< something unknown went wrong */
    BLE_UNKNOWN, /**< event doesnt match defined enum */
    BLE_ERROR, /**< error emitted from radio or device */
    BLE_ADVERTISED,
    BLE_CONNECTED,
    BLE_DISCONNECTED,
    BLE_SERV_DISCOVERED,
    BLE_CHAR_DISCOVERED,
    BLE_DESC_DISCOVERED,
    BLE_CHAR_UPDATED,
    BLE_DESC_UPDATED,
    BLE_CHAR_WRITE_SUCCESS,
    BLE_DESC_WRITE_SUCCESS,
    BLE_CHAR_NOTIFY_SUCCESS,
} event_type;

/**
 * @brief characteristic flags (BlueZ GATT API)
 */
typedef enum {
  BLE_Char_Broadcast = 0x1,
  BLE_Char_Read = 0x2,
  BLE_Char_Write_without_response = 0x4,
  BLE_Char_Write = 0x8,
  BLE_Char_Notify = 0x10,
  BLE_Char_Indicate = 0x20,
  BLE_Char_Authenticated_signed_writes = 0x40,
  BLE_Char_Extended_properties = 0x80,
  BLE_Char_Reliable_write = 0x100,
  BLE_Char_Writable_auxiliaries = 0x200,
  BLE_Char_Encrypt_read = 0x400,
  BLE_Char_Encrypt_write = 0x800,
  BLE_Char_Encrypt_authenticated_read = 0x1000,
  BLE_Char_Encrypt_authenticated_write = 0x2000,
  BLE_Char_Secure_read = 0x4000,
  BLE_Char_Secure_write = 0x8000,
  BLE_Char_Authorize = 0x10000
} ble_C_Flags;

/**
 * @brief device connection flags
 */
typedef enum {
  Ble_Success = 0,
  Ble_NotReady,
  Ble_Failed,
  Ble_InProgress,
  Ble_AlreadyConnected,
  Ble_ServiceResolveFailure,
} ble_connection_status_t;

/**
 * @brief data used to perform a write
 */
typedef struct {
  uint8_t *data;
  size_t data_length;
} barray_t;



/**
 * BLE Input Structs (parameters for command)
 */

/**
 * @brief set discovery scan parameters and enable discovery
 */
typedef struct ble_discovery_scan_params_t {
  uint32_t           scan_interval_ms; /**< how often to scan between 5 mS and 10000 mS */
  uint32_t           scan_duration_ms; /**< how long to scan and must be less then or equal to scan_interval_ms */
  bool               is_passive;       /**< if true we will listen only, if false we will respond with scan request packet */
  ble_mac_t   *mac_filter;      /**< MACs we are interested in */
  uint8_t            num_mac_filters;   /**< Number of MAC filters */
  ble_uuid_t *uuid_filter;      /**< Number of UUIDs we are interested in (only return devices that advertise these )*/
  uint8_t            num_uuid_filters; /**< Number of service UUIDs filters */
} ble_discovery_scan_params_t;

typedef struct {
  bool is_public_addr;
} ble_connect_params_t;

typedef struct ble_service_discovery_params_t {
  struct ble_service_t *filter;
  size_t num_filters;
} ble_service_discovery_params_t;

typedef struct  ble_characteristic_discovery_params_t {
  ble_uuid_t serv_uuid;
} ble_characteristic_discovery_params_t;

typedef struct  ble_descriptor_discovery_params_t {
  ble_uuid_t char_uuid;
} ble_descriptor_discovery_params_t;

typedef struct 	ble_characteristic_notification_params {
  ble_uuid_t char_uuid;
} ble_characteristic_notification_params;

typedef struct ble_read_characteristic_params_t {
  ble_uuid_t char_uuid;
} ble_read_characteristic_params_t;

typedef struct ble_read_descriptor_params_t {
  ble_uuid_t char_uuid;
  ble_uuid_t desc_uuid;
} ble_read_descriptor_params_t;

typedef struct  ble_write_characteristic_params_t {
  barray_t *barray;
  ble_uuid_t char_uuid;
} ble_write_characteristic_params_t;

typedef struct  ble_write_descriptor_params_t {
  barray_t *barray;
  ble_uuid_t char_uuid;
  ble_uuid_t desc_uuid;
} ble_write_descriptor_params_t;


/**
 * BLE Output Structs (results of event)
 */
typedef struct ble_advertisement_t {
  ble_mac_t   mac; /**< string of MAC address of the device found */
  bool               is_public_address; /** true if the address is public or false if random */
  char*              name;    /**< Human Readable string for name of device, could be NULL if no name in this packet */
  bool               is_complete_name; /**< True if it's the complete name, false otherwise */
  ble_uuid_t* service_uuids; /**< service uuid list, could be NULL if no service UUIDs in this packet */
  uint8_t            num_services; /**< number of services in the advertising data packet */
  barray_t           *data;
} ble_advertisement_t;

typedef struct ble_connect_t {
  ble_connection_status_t status;
} ble_connect_t;

typedef struct ble_service_t {
  ble_uuid_t            uuid; /**< service uuid */
  bool                         is_primary; /**< If the service is a primary or TODO: ? */
} ble_service_t;

typedef struct ble_characteristic_discovery_t {
  ble_uuid_t        uuid; /**< characteristic UUID */
  ble_C_Flags       flags;     /**< how characteristic can be used */
} ble_characteristic_discovery_t;

typedef struct ble_descriptor_discovery_t {
  ble_uuid_t  uuid; /**< descriptor UUID */
} ble_descriptor_discovery_t;

typedef struct ble_characteristic_t {
  ble_uuid_t uuid; /**< uuid of characteristic */
  barray_t *data; /**< byte array of data received */
} ble_characteristic_t;

typedef struct ble_descriptor_t {
  ble_uuid_t char_uuid; /**< uuid of characteristic */
  ble_uuid_t uuid; /**< uuid of descriptor */
  barray_t *data; /**< byte array of data received */
} ble_descriptor_t;

typedef struct ble_write_success_t {
  int s_code; /**< status code of operation */
} ble_write_success_t;

typedef struct ble_notification_success_t {
    int s_code; /**< status code of operation */
} ble_notification_success_t;

/**
 * @Structures used for event emission from BLE Target Layer
 */

/**
 * @brief this is received during a BLE scan
 *
 * @note there is no parameters field as this isn't a result of a request
 */
typedef struct ble_error_t {
    event_type op; /**< type of event */
    char *error; /**< readout of error */
    void *params; /**< parameters used for event */
} ble_error_t;

typedef struct ble_advertised_t {
  ble_advertisement_t *contents;
} ble_advertised_t;

typedef struct ble_connected_t {
  ble_connect_params_t params;
  ble_connect_t connection;
} ble_connected_t;

typedef struct ble_disconnected_t {
  ble_connect_params_t params;
  ble_connect_t connection;
} ble_disconnected_t;

typedef struct ble_service_discovered_t {
  ble_service_discovery_params_t params;
  ble_service_t service;
} ble_service_discovered_t;

typedef struct ble_characteristic_discovered_t {
  ble_characteristic_discovery_params_t params;
  ble_characteristic_discovery_t characteristic;
} ble_characteristic_discovered_t;

typedef struct  ble_descriptor_discovered_t {
  ble_descriptor_discovery_params_t  params;
  ble_descriptor_discovery_t descriptor;
} ble_descriptor_discovered_t;

typedef struct  ble_characteristic_updated_t {
  bool is_notification; /**< True if result of notification, false if read */
  union {
    ble_read_characteristic_params_t c_params; /**< If this is the result of a read access data here */
    ble_characteristic_notification_params  n_params; /**< If this is the result of a notification access data here */
  };
  ble_characteristic_t characteristic; /**< Data of new characteristic value */
} ble_characteristic_updated_t;

typedef struct  ble_descriptor_updated_t {
  ble_read_descriptor_params_t d_params; /**< Read data paramaters */
  ble_descriptor_t descriptor; /**< Data of new descriptor value */
} ble_descriptor_updated_t;

typedef struct ble_char_write_success_t {
  ble_write_characteristic_params_t *params;
  ble_write_success_t write;
} ble_char_write_success_t;

typedef struct ble_desc_write_success_t {
  ble_write_descriptor_params_t *params;
  ble_write_success_t write;
} ble_desc_write_success_t;

typedef struct ble_char_notification_success_t {
     ble_characteristic_notification_params  *params;
     ble_notification_success_t              notification;
} ble_char_notification_success_t;

/**
 * @These are the data structures that can be populated and emitted, based on
 * the corresponding event type
 */
typedef union ble_operation {
  ble_error_t                      error; /**< some error occurred */
  ble_advertised_t                 advertise; /**< advertisement data received */
  ble_connected_t                  connection; /**< connection event received */
  ble_disconnected_t               disconnection; /**< disconnection event received */
  ble_service_discovered_t         s_discovered; /**< service discovered */
  ble_characteristic_discovered_t  c_discovered; /**< characteristic discovered */
  ble_descriptor_discovered_t      d_discovered; /**< descriptor discovered */
  ble_characteristic_updated_t     c_updated; /**< characteristic updated (read|notify) */
  ble_descriptor_updated_t         d_updated; /**< descriptor updated (read|notify) */
  ble_char_write_success_t         c_written; /**< characteristic write succeeded */
  ble_desc_write_success_t         d_written; /**< descriptor write succeeded */
  ble_char_notification_success_t  c_notify; /**< characteristic enable notifications succeeded */
} ble_operation;

/**
 * @brief Every BLE Event is encapsulated in this struct
 */
typedef struct ble_event_t {
  enum event_type type; /**< Operation that emitted the event */
  ble_mac_t mac; /**< mac of device that emitted event */
  union ble_operation op; /**< Data structure containing information about the event */
} ble_event_t;


/**
 * @note callbacks start here
 */
/**
 * @brief callback for any errors that occur
 *
 * @param context context for caller, loaded with init
 */
typedef void (*ble_event_cb_t)(
        void *caller_ctx,
        ble_event_t *event);

/**
 * @brief initialization provided to target layer if required
 *
 * @param context    pointer to context pointer, for target layer to keep relevant data
 * @param caller_ctx context for the caller, should be present in ev_cb
 * @param loop       event loop for use by TL, TL functions MUST not block
 * @param ev_cb      callback function for ble events
 *
 * @return true      initialization finished with no errors
 * @return false     errors occurred during initialization
 */
bool ble_init(
        void **context,
        void *caller_ctx,
        struct ev_loop* loop,
        void (*ev_cb)(void *c, ble_event_t *e));

/**
 * @brief exit and cleanup the ble target layer
 */
bool ble_exit(void *context);

/**
 * BLE Commands
 */

/**
 * @brief scan for a set of whitelisted service uuids, no uuids means scan all
 *
 * @param context   context defined by TL, passed to all function calls
 * @param params    configuration for scan. If num_uuid_filters = 0, expected to scan for all.
 *
 * @return true     began discovery scan
 * @return false    failed to enable discovery scan
 */
bool ble_enable_discovery_scan(
        void *context,
        ble_discovery_scan_params_t* params);

bool ble_disable_discovery_scan(
        void *context);

/**
 * @brief issue connect request to a device, call callbacks
 *
 * @param  mac                  MAC of device to be connected to
 * @param  is_public_address    whether public or random address
 *
 * @return  true                device struct is fully populated (services have
 * @return  false               services failed to resolve or some other
 *                                 connect error
 */
bool ble_connect_device(
        void *context,
        ble_mac_t mac,
        ble_connect_params_t *params);


bool ble_disconnect_device(
        void *context,
        ble_mac_t mac);


/**
 * @brief discovered services for a given device
 *
 * @param context  opaque struct for TL
 * @param mac      device for service discovery
 * @param filter   uuid list, if NULL discover all
 */
bool ble_discover_services(
        void *context,
        ble_mac_t mac,
        ble_service_discovery_params_t *params);

/**
 * @brief discover characteristics for a given service
 *
 * @param context  opaque struct for TL
 * @param mac      device for service discovery
 * @param serv_uuid   service uuid to discover characteristics for
 */
bool ble_discover_characteristics(
        void *context,
        ble_mac_t mac,
        ble_characteristic_discovery_params_t *params);

/**
 * @brief discover descriptors for a given service
 *
 * @param context  opaque struct for TL
 * @param mac      device for service discovery
 * @param serv_uuid   characteristic uuid to discover descriptors for
 */
bool ble_discover_descriptors(
        void *context,
        ble_mac_t mac,
        ble_descriptor_discovery_params_t *params);

/**
 * @brief subscribe to notifications for a characteristic on a device
 *
 * when new notifications are received they are sent to any callbacks bound
 * with the function ble_notify_characteristic
 *
 * @param    mac              MAC of device for request
 * @param    params           required to load notifications
 */
bool ble_enable_characteristic_notifications(
        void *context,
        ble_mac_t mac,
        ble_characteristic_notification_params *params);

/**
 * @brief stop subscription to notifications for a characteristic on a device
 *
 * If it fails, on_error_cb() will be invoked
 *
 * @param    mac              MAC of device for request
 * @param    char_uuid        UUID of characteristic we are interested in
 */
bool ble_disable_characteristic_notifications(
        void *context,
        ble_mac_t  mac,
        ble_characteristic_notification_params *params);

/**
 * @brief read the data out of a characteristic
 *
 * If it fails, characteristic_read_cb() will be invoked with success false,
 * otherwise success will be true, and the data will be filled.
 *
 * @param    mac              MAC of device for request
 * @param    params           param struct to read a characteristic
 */
bool ble_read_characteristic(
        void *context,
        ble_mac_t  mac,
        ble_read_characteristic_params_t *params);

/**
 * @brief read the data out of a descriptor
 *
 * If it fails, descriptor_read_cb() will be invoked with success false,
 * otherwise success will be true, and the data will be filled.
 *
 * @param    mac              MAC of device for request
 * @param    params           param struct to read a descriptor
 */
bool ble_read_descriptor(
        void *context,
        ble_mac_t  mac,
        ble_read_descriptor_params_t *params);

/**
 * @brief write data to a characteristic
 *
 * @param    mac              MAC of device for request
 * @param    params           parameters required to write a characteristic
 */
bool ble_write_characteristic(
        void *context,
        ble_mac_t  mac,
        ble_write_characteristic_params_t *params);

/**
 * @brief write data to a descriptor
 *
 * @param    mac              MAC of device for request
 * @param    params           parameters required to write a characteristic
 */
bool ble_write_descriptor(
        void *context,
        ble_mac_t  mac,
        ble_write_descriptor_params_t *params);


#endif /* TARGET_BLE_H */
