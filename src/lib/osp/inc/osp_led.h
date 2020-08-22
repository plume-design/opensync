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

#ifndef OSP_LED_H_INCLUDED
#define OSP_LED_H_INCLUDED

#include <stdint.h>


/// @file
/// @brief LED API
///
/// @addtogroup OSP
/// @{


// ===========================================================================
//  LED API
// ===========================================================================

/// @defgroup OSP_LED  LED API
/// OpenSync LED API
/// @{

#define OSP_LED_PRIORITY_DISABLE    ((uint32_t)-1)  /**< LED state disabled */
#define OSP_LED_PRIORITY_DEFAULT    ((uint32_t)-2)  /**< Lowest priority */

/**
 * Available LED states
 *
 * These are business logic level LED states, implemented by target layer.
 */
enum osp_led_state
{
    OSP_LED_ST_IDLE = 0,        /**< Idle (normal operation) */
    OSP_LED_ST_ERROR,           /**< Error state (generic)*/
    OSP_LED_ST_CONNECTED,       /**< Connected */
    OSP_LED_ST_CONNECTING,      /**< Connecting */
    OSP_LED_ST_CONNECTFAIL,     /**< Failed to connect */
    OSP_LED_ST_WPS,             /**< WPS active */
    OSP_LED_ST_OPTIMIZE,        /**< Optimization in progress */
    OSP_LED_ST_LOCATE,          /**< Locating */
    OSP_LED_ST_HWERROR,         /**< Hardware fault */
    OSP_LED_ST_THERMAL,         /**< Thermal panic*/
    OSP_LED_ST_BTCONNECTABLE,   /**< Bluetooth accepting connections */
    OSP_LED_ST_BTCONNECTING,    /**< Bluetooth connecting */
    OSP_LED_ST_BTCONNECTED,     /**< Bluetooth connected */
    OSP_LED_ST_BTCONNECTFAIL,   /**< Bluetooth connection failed */
    OSP_LED_ST_UPGRADING,       /**< Upgrade in progress */
    OSP_LED_ST_UPGRADED,        /**< Upgrade finished */
    OSP_LED_ST_UPGRADEFAIL,     /**< Upgrade failed */
    OSP_LED_ST_HWTEST,          /**< Hardware test - FQC */
    OSP_LED_ST_LAST             /**< (table sentinel) */
};


/**
 * Initialize the LED subsystem
 *
 * @param[out] led_cnt  Number of LED's supported by the system
 *
 * @return 0 on success, -1 on error
 */
int osp_led_init(int *led_cnt);


/**
 * Set LED to specified business level state (high-level LED API)
 *
 * @param[in] state     LED state
 * @param[in] priority  LED state priority -- 0 is highest. A higher priority
 *                      state overrides current LED behavior.
 *
 * @return 0 on success, -1 on error
 */
int osp_led_set_state(enum osp_led_state state, uint32_t priority);


/**
 * Clear a LED state
 *
 * If the specified state has the highest priority when being cleared, the
 * next highest priority state is applied.
 * If there are no states on the LED state stack, @ref OSP_LED_ST_IDLE is applied.
 *
 * @param[in] state  A previously set LED state
 *
 * @return 0 on success, -1 on error
 */
int osp_led_clear_state(enum osp_led_state state);


/**
 * Clear all LED states and set LED to 'Idle' state (@ref OSP_LED_ST_IDLE)
 *
 * @return 0 on success, -1 on error
 */
int osp_led_reset(void);


/**
 * Get currently active LED state
 *
 * @param[out] state     Current LED state
 * @param[out] priority  Priority of the current state
 *
 * @return 0 on success, -1 on error
 */
int osp_led_get_state(enum osp_led_state *state, uint32_t *priority);


/**
 * Get the textual representation of the given state
 *
 * @param[in] state  LED state to convert to string
 *
 * @return null-terminated string
 */
const char* osp_led_state_to_str(enum osp_led_state state);

/**
 * Convert string to LED state
 *
 * @param[in] str  null-terminated string to convert
 *
 * @return state or OSP_LED_ST_LAST on failure
 */
enum osp_led_state osp_led_str_to_state(const char *str);


/// @} OSP_LED
/// @} OSP

#endif /* OSP_LED_H_INCLUDED */
