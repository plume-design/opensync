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
#include <stdbool.h>


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
#define OSP_LED_POSITION_DEFAULT    0               /**< LED position for default system actions */

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
    OSP_LED_ST_IOT_ALARM,       /**< Alarm triggered by IoT devices */
    OSP_LED_ST_CLOUD,           /**< Custom pattern pushed from the cloud */
    OSP_LED_ST_LAST             /**< (table sentinel) */
};

/**
 * LED color value in RGB format
 */
struct osp_led_color {
    union {
        struct __attribute__((packed))
        {
            /* Order of fields is little-endian to allow for a packed RGB color value */
            uint8_t b;  /**< Blue in range [0, 255] */
            uint8_t g;  /**< Green in range [0, 255] */
            uint8_t r;  /**< Red in range [0, 255] */
        };
        uint32_t rgb;  /**< RGB color value */
    };
};

/**
 * LED pattern element which describes the LED characteristics
 * for a portion of the LED cycle period.
 */
struct osp_led_pattern_el {
    uint16_t duration;           /**< Total duration of this element in milliseconds, including fade time */
    struct osp_led_color color;  /**< RGB color */
    uint16_t fade;               /**< Transition time from current to set color in milliseconds */
};

struct led_ctx
{
    enum osp_led_state cur_state;
    bool     state_enab[OSP_LED_ST_LAST];
    uint32_t state_prio[OSP_LED_ST_LAST];
    uint32_t state_def_prio[OSP_LED_ST_LAST];
    struct osp_led_pattern_el *pattern_els;
    int pattern_els_count;
};

/**
 * Initialize the LED subsystem
 *
 * @return 0 on success, -1 on error
 */
int osp_led_init(void);


/**
 * Set LED to specified business level state (high-level LED API)
 *
 * @param[in] state        LED state
 * @param[in] priority     LED state priority -- 0 is highest. A higher priority
 *                         state overrides current LED behavior.
 * @param[in] position     Position of LED whose state we are trying to set. Allows
 *                         setting different patterns on systems with multiple LEDs.
 * @param[in] pattern_els  List of LED custom pattern elements where each element
 *                         controls LED behavior for a certain duration based on
 *                         specified values. The pattern is executed in a cycle.
 *                         Only applicable to the "cloud" state.
 * @param[in] pattern_els_count  Number of elements in LED pattern
 *
 * @return 0 on success, -1 on error
 */
int osp_led_set_state(
        enum osp_led_state state,
        uint32_t priority,
        uint8_t position,
        struct osp_led_pattern_el *pattern_els,
        int pattern_els_count);


/**
 * Clear a LED state
 *
 * If the specified state has the highest priority when being cleared, the
 * next highest priority state is applied.
 * If there are no states on the LED state stack, @ref OSP_LED_ST_IDLE is applied.
 *
 * @param[in] state     A previously set LED state
 * @param[in] position  Position of LED whose state we are trying to clear.
 *
 * @return 0 on success, -1 on error
 */
int osp_led_clear_state(enum osp_led_state state, uint8_t position);


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
 * @param[in]  position  Position of LED whose state we are trying to get.
 *
 * @return 0 on success, -1 on error
 */
int osp_led_get_state(enum osp_led_state *state, uint32_t *priority, uint8_t position);


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

/**
 * Get the default priority value LED state
 *
 * @param[in]  state     LED state
 *
 * @return 0 on success, -1 on error
 */
uint32_t osp_led_get_state_default_prio(enum osp_led_state state);

/**
 * Add a new active LED state into OVSDB table LED_Config
 *
 * @param[in] state     LED state to add into table
 * @param[in] position  Position of LED whose state we are trying to add.
 *
 * @return 0 on success, -1 on error when failing to insert new row
 */
int osp_led_ovsdb_add_led_config(enum osp_led_state state, uint32_t priority, uint8_t position);

/**
 * Delete a currently active LED state from OVSDB table LED_Config
 *
 * @param[in] state     LED state to delete from table
 * @param[in] position  Position of LED whose state we are trying to delete.
 *
 * @return 0 on success, -1 on error when no rows were deleted
 */
int osp_led_ovsdb_delete_led_config(enum osp_led_state state, uint8_t position);

/**
 * Gets currently active LED state for desired position from OVSDB table LED_Config
 *
 * @param[in]  position  Position of LED whose state we are trying to get
 *
 * @return 0 on success, -1 on error when no rows were deleted
 */
enum osp_led_state osp_led_ovsdb_get_active_led_state(uint8_t position);

/**
 * Gets the default priorities for every LED state.
 *
 * @param[out]  priorities      Array of default priorities for each state
 * @param[in]   priorities_num  Number of priorities to get values for,
 *                              should be equal to the number of states
 *
 * @return true on success, false on error
 */
bool osp_led_tgt_get_def_state_priorities(uint32_t priorities[], int priorities_num);

/**
 * Set LED to specified target level state (low-level LED API)
 *
 * @param[in] state        LED state
 * @param[in] position     Position of LED whose state we are trying to set. Allows
 *                         setting different patterns on systems with multiple LEDs.
 * @param[in] pattern_els  List of LED custom pattern elements where each element
 *                         controls LED behavior for a certain duration based on
 *                         specified values. The pattern is executed in a cycle.
 *                         Only applicable to the "cloud" state.
 * @param[in] pattern_els_count  Number of elements in LED pattern
 *
 * @return true on success, false on error
 */
bool osp_led_tgt_set_state(
        enum osp_led_state state,
        uint8_t position,
        struct osp_led_pattern_el *pattern_els,
        int pattern_els_count);

/**
 * Initialize the LED target layer subsystem
 *
 * @return 0 on success, -1 on error
 */
void osp_led_tgt_init(void);

/// @} OSP_LED
/// @} OSP

#endif /* OSP_LED_H_INCLUDED */
