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

#ifndef OSP_BLE_H_INCLUDED
#define OSP_BLE_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <ev.h>


/// @file
/// @brief Bluetooth Low Energy API
///
/// @addtogroup OSP
/// @{


// ===========================================================================
//  Bluetooth Low Energy API
// ===========================================================================

/// @defgroup OSP_BLE  Bluetooth Low Energy API
/// OpenSync BLE API
/// @{


/**
 * Callback invoked from the driver when BLE central device connects
 *
 * @param bd_address  BT MAC address of connected device.
 *
 * @return true if this command was handled
 */
typedef bool (*osp_ble_on_device_connected_cb_t)(uint8_t bd_address[6]);

/**
 * Callback invoked from the driver when previously connected BLE central device disconnects
 *
 * @return true if this command was handled
 */
typedef bool (*osp_ble_on_device_disconnected_cb_t)(void);

/**
 * Callback invoked from the driver when connected device completes pairing challenge
 *
 * @param paired  Whether the pairing was successful, or failed and the peer will be disconnected.
 *
 * @return true if this command was handled
 */
typedef bool (*osp_ble_on_pairing_status_cb_t)(bool paired);

/**
 * Callback invoked from the driver when connected BLE central writes new data to the JSON GATT characteristic
 *
 * @param[in] value  Pointer to event value - local configuration JSON string (not guaranteed to be null terminated).
 * @param     len    Length of the value (including null-termination, if present).
 *
 * @return true if this command was handled
 */
typedef bool (*osp_ble_on_gatt_json_cb_t)(char *value, uint16_t len);


/**
 * Initialize the Bluetooth Low Energy subsystem
 *
 *  Open and configure communication interface with the BT chip, download required
 *  firmware, configure static parameters (e.g. Bluetooth Device Address), prepare
 *  the Bluetooth module for advertising.
 *
 * @param[in] loop                       Optional event loop used to handle Bluetooth stack asynchronous events
 *                                       and invoke the callbacks. If not provided, it is impossible to accept
 *                                       BLE connections (therefore callbacks below will never be invoked),
 *                                       but non-connectable advertising is still possible.
 * @param     on_device_connected_cb     Optional callback called when new BLE Central device initiates connection
 *                                       to this device (before pairing process is initiated).
 * @param     on_device_disconnected_cb  Optional callback called when previously connected BLE Central device
 *                                       disconnects for any reason.
 * @param     on_pairing_status_cb       Optional callback called when pairing with newly connected device succeeds
 *                                       or fails (in that case the device will be disconnected soon after).
 * @param     on_gatt_json_cb            Optional callback called when connected device writes new data to the
 *                                       local-config JSON GATT characteristic.
 *
 * @return true on success
 */
bool osp_ble_init(struct ev_loop *loop,
                  osp_ble_on_device_connected_cb_t on_device_connected_cb,
                  osp_ble_on_device_disconnected_cb_t on_device_disconnected_cb,
                  osp_ble_on_pairing_status_cb_t on_pairing_status_cb,
                  osp_ble_on_gatt_json_cb_t on_gatt_json_cb);

/**
 * Close the Bluetooth Low Energy subsystem and cleanup resources
 */
void osp_ble_close(void);

/**
 * Get the value advertised as Complete List of 16-bit Service UUIDs
 *
 * @param[in,out] uuid  Pointer to the value advertised as Complete List of 16-bit Service UUIDs.
 *                      The default value defined by BLEM_GATT_SERVICE_UUID (passed to this function)
 *                      can be changed by writing to this address.
 *
 * @return true if the default value passed to this function was modified
 */
bool osp_ble_get_service_uuid(uint16_t *uuid);

/**
 * Set Device Name characteristic of Generic Access GATT Service, if supported
 *
 * @param[in] device_name  Device Name to set as null-delimited UTF-8 string (null-delimiter will not be copied).
 *
 * @return true on success
 */
bool osp_ble_set_device_name(const char *device_name);

/**
 * Set advertising packet payload
 *
 * 3 bytes of Flags data type is expected to be prepended by the Bluetooth Stack code, usually:
 *      uint8_t len_flags     = 2
 *      uint8_t ad_type_flags = 0x01
 *      uint8_t flags         = (1 << 1)|(1 << 2) = LE General Discoverable Mode | BR/EDR Not Supported
 * That is why this payload is always maximum 28 instead of 31 bytes long.
 *
 * @param[in] payload  Advertising packet payload, max. 28 bytes.
 * @param     len      Length of the payload in range [0, 28].
 *
 * @return true on success
 */
bool osp_ble_set_advertising_data(const uint8_t payload[28], uint8_t len);

/**
 * Set scan response packet payload
 *
 * @param[in] payload  Scan Response packet payload, max. 31 bytes.
 * @param     len      Length of the payload in range [0, 31].
 *
 * @return true on success
 */
bool osp_ble_set_scan_response_data(const uint8_t payload[31], uint8_t len);

/**
 * Set advertising parameters
 *
 * @param enabled      Whether to enable advertising.
 * @param sr_enabled   Whether to enable scan responses.
 * @param interval_ms  Advertising interval in ms (20 ms to 10.485 s, resolution 0.625 ms).
 *
 * @return true on success
 */
bool osp_ble_set_advertising_params(bool enabled, bool sr_enabled, uint16_t interval_ms);

/**
 * Set connectable mode
 *
 * @param enabled  Enable advertising as connectable and reception of connection requests.
 *
 * @return true on success
 */
bool osp_ble_set_connectable(bool enabled);

/**
 * Calculate 6-digit passkey for Passkey Entry BLE pairing method
 *
 * This passkey is later set applied to the BLE stack using @ref osp_ble_set_pairing_passkey.
 *
 * @param[in]  random_token  Random token from advertisement data (4 bytes).
 *                           This token always has stable bits cleared (neutral).
 * @param[out] passkey       Pointer to a variable to which the generated passkey shall be stored.
 *                           Simple passkeys with 3 or more same or consecutive digits will be
 *                           rejected and this function will be called again with the new
 *                           `random_token`, until it produces valid passkey or returns false.
 *                           Some examples of rejected passkeys:
 *                           - 0 (000000), 111111, ..., 999999
 *                           - 012345, 123456, ..., 456789
 *                           - 987654, 876543, ..., 543210
 *
 * @return true on success (passkey was calculated and written to `passkey`)
 */
bool osp_ble_calculate_pairing_passkey(const uint8_t token[4], uint32_t *passkey);

/**
 * Set passkey for Passkey Entry Pairing
 *
 * @param passkey  6-digit (1-999999) passkey - value 0 rejects pairing.
 *
 * @return true on success
 */
bool osp_ble_set_pairing_passkey(uint32_t passkey);

/**
 * Set GATT service - Local-config JSON GATT characteristic value
 *
 * @param value  Value to set (shall be valid JSON), not required to be NULL terminated.
 * @param len    Length of the value (including null termination, if present).
 *
 * @return true on success
 */
bool osp_ble_set_gatt_json(const char *value, uint16_t len);


/// @} OSP_BLE
/// @} OSP

#endif /* OSP_BLE_H_INCLUDED */
