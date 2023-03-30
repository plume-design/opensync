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

#ifndef BLEM_H_INCLUDED
#define BLEM_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <ev.h>


/**
 * Advertising data structure, exactly resembling BLE advertisement payload
 *
 * From Supplement to Bluetooth Core Specification | CSS v9, Part A, Section 1.3 FLAGS:
 *   The Flags data type shall be included when any of the Flag bits are non-zero and the
 *   advertising packet is connectable, otherwise the Flags data type may be omitted.
 *
 * So when advertising as connectable, it is impossible to remove flags. The Flags data type are prepended by the
 * BLE stack, hence the user shall not include them in the advertising data. The BLE stack adds 3 bytes at the
 * start of the advertising data, so the maximum length of the user defined advertising data shall be 28 bytes.
 *
 * This payload is used when advertising in modes:
 * - Non-connectable undirected advertising (ADV_NONCONN_IND)
 * - Scannable undirected advertising event (ADV_SCAN_IND)
 * - Connectable undirected advertising (ADV_IND)
 */
typedef struct __attribute__ ((packed)) {
    /* Flags - added by the Bluetooth Stack code
    uint8_t len_flags;      2
    uint8_t ad_type_flags;  0x01
    uint8_t flags;          (1 << 1)|(1 << 2) = LE General Discoverable Mode | BR/EDR Not Supported
    */

    /* Complete List of 16-bit Service Class UUIDs */
    uint8_t len_uuid;
    uint8_t ad_type_uuid;
    uint16_t service_uuid;

    uint8_t len_manufacturer;
    uint8_t ad_type_manufacturer;
    /* region 22 bytes of manufacturer specific data */
    uint16_t company_id;
    uint8_t version;
    char serial_num[12]; /*< Serial number is not required to be null-terminated */
    uint8_t msg_type;
    struct __attribute__ ((packed)) {
        uint8_t status;
        uint8_t _rfu[1];
        /** Random token used in pairing passkey generation */
        uint8_t pairing_token[4];
    } msg;
    /* endregion 22 bytes of manufacturer specific data */
} ble_advertising_data_t;

/**
 * Scan Response data structure, exactly resembling BLE scan response payload
 *
 * This is optionally used for convenience and for easier pod locating.
 */
typedef struct __attribute__ ((packed)) {
    /* Complete Local Name */
    uint8_t len_complete_local_name;     /**< Length of this advertising structure: 1 + strlen(`complete_local_name`) */
    uint8_t ad_type_complete_local_name; /**< Complete Local Name = 0x09 (fixed value) */
    char complete_local_name[31 - 2];    /**< Advertised name, can be shorted and non-null terminated */
} ble_scan_response_data_t;


/**
 * Initialize BLEM OVSDB tables
 */
void blem_ovsdb_init(void);

/**
 * Initialize the Bluetooth peripheral
 *
 * @param[in] p_loop  Event loop used for handling bluetooth events.
 *
 * @return true on success
 */
bool blem_ble_init(struct ev_loop *p_loop);

/**
 * Configure and start BLE advertising
 *
 * If advertising is already started it will be stopped, reconfigured and started again.
 *
 * @param     connectable  If true, advertise as connectable and enable BLE central devices to connect
 *                         and pair with this device and write configuration (if pairing is successful).
 * @param     interval_ms  Advertising interval in milliseconds (BT module configuration), in range from
 *                         20 ms to 10.24 s.
 * @param     msg_type     Message type (included in the advertising packet).
 * @param[in] msg          6-byte message payload (included in the advertising packet).
 *
 * @return true on success
 */
bool blem_ble_enable(bool connectable, int interval_ms, uint8_t msg_type, const uint8_t msg[6]);

/**
 * Stop BLE advertising and disable ability for BLE central devices to connect to this device
 */
void blem_ble_disable(void);

/**
 * Power off the Bluetooth peripheral and cleanup resources
 */
void blem_ble_close(void);


#endif /* BLEM_H_INCLUDED */
