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

#include "ble_adv_data.h"

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
 * Configure advertising of the BLE proximity beacons
 *
 * @param     enable        Enable or disable advertising of the proximity beacons.
 * @param     adv_tx_power  Advertising transmit power in 0.1 dBm steps.
 * @param     adv_interval  Advertising interval in milliseconds.
 * @param[in] uuid          Proximity Beacon UUID.
 * @param     major         Proximity Beacon Major ID.
 * @param     minor         Proximity Beacon Minor ID.
 * @param     meas_power    Calibrated (measured) RSSI at 1 meter distance.
 */
void blem_ble_proximity_configure(
        bool enable,
        int16_t adv_tx_power,
        uint16_t adv_interval,
        const uint8_t uuid[16],
        uint16_t major,
        uint16_t minor,
        int8_t meas_power,
        blem_ble_adv_on_state_t on_state_change);

/**
 * Power off the Bluetooth peripheral and cleanup resources
 */
void blem_ble_close(void);

#endif /* BLEM_H_INCLUDED */
