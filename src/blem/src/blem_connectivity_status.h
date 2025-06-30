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

#ifndef BLEM_CONNECTIVITY_STATUS_H_INCLUDED
#define BLEM_CONNECTIVITY_STATUS_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>

/**
 * BLE Onboarding Connectivity Status bitfield bit positions
 */
typedef enum
{
    BLE_ONBOARDING_STATUS_BIT_ETH_PHY_LINK = 0,          /**< Ethernet physical link (bit 0, bitmask 0x01) */
    BLE_ONBOARDING_STATUS_BIT_WIFI_PHY_LINK = 1,         /**< Wi-Fi physical link (bit 1, bitmask 0x02) */
    BLE_ONBOARDING_STATUS_BIT_BACKHAUL_OVER_ETH = 2,     /**< Backhaul over Ethernet (bit 2, bitmask 0x04) */
    BLE_ONBOARDING_STATUS_BIT_BACKHAUL_OVER_WIFI = 3,    /**< Backhaul over Wi-Fi (bit 3, bitmask 0x08) */
    BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_ROUTER = 4,   /**< Connected to Router (bit 4, bitmask 0x10) */
    BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_INTERNET = 5, /**< Connected to Internet (bit 5, bitmask 0x20) */
    BLE_ONBOARDING_STATUS_BIT_CONNECTED_TO_CLOUD = 6,    /**< Connected to Cloud (bit 6, bitmask 0x40) */
    /** Invalid status bit, used to distinguish initial unknown state and absence of all other bits */
    BLE_ONBOARDING_STATUS_BIT__UNKNOWN = 7
} ble_connectivity_status_bit_t;

/**
 * Callback function called when Connectivity Status bitfield value changes
 *
 * @param status_old  Previous Connectivity Status bitfield value.
 * @param status      New (current) Connectivity Status bitfield value.
 *
 * @see blem_connectivity_status_init
 * @see ble_connectivity_status_bit_t
 */
typedef void (*blem_connectivity_status_updated_cb_t)(uint8_t status_old, uint8_t status);

/**
 * Initialize the node connectivity status monitoring
 *
 * This function initializes and starts monitoring OVSDB
 * tables related to the node connectivity status.
 *
 * @param[in] callback  Function to be called when the connectivity status changes.
 * @param[in] debounce  If greater than 0, then the `callback` will be called only
 *                      after the status bitmask has not changed for at least this
 *                      many seconds.
 *                      For example, if this is set to 1.0, and the status bitmask
 *                      changes at t=0.1, t=0.5, t=1.0, t=1.5, then only the last
 *                      status value (calculated at t=1.5) will be reported after
 *                      the debouncing period, at t=2.5.
 *                      If 0, then the `callback` will be called immediately after
 *                      each status bitmask change.
 *
 * @see blem_connectivity_status_fini
 */
void blem_connectivity_status_init(blem_connectivity_status_updated_cb_t callback, float debounce);

/**
 * Check if the given status represents working internet connectivity
 *
 * @param status  Connectivity Status bitfield value to check.
 *
 * @return true if connected to internet, false otherwise.
 */
bool blem_connectivity_status_is_connected_to_internet(uint8_t status);

/**
 * Check if the given status represents working cloud connectivity
 *
 * @param status  Connectivity Status bitfield value to check.
 *
 * @return true if connected to cloud, false otherwise.
 */
bool blem_connectivity_status_is_connected_to_cloud(uint8_t status);

/**
 * Stop the BLE connectivity status monitoring
 */
void blem_connectivity_status_fini(void);

#endif /* BLEM_CONNECTIVITY_STATUS_H_INCLUDED */
