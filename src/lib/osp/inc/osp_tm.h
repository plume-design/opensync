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

#ifndef OSP_TM_H_INCLUDED
#define OSP_TM_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include "osp_temp.h"

/// @file
/// @brief Thermal Management API
///
/// @addtogroup OSP
/// @{


// ===========================================================================
//  Thermal Management API
// ===========================================================================

/// @defgroup OSP_TM  Thermal Management API
/// OpenSync Thermal Management API
/// @{


/**
 * Thermal state table element
 */
struct osp_tm_therm_state
{
    int temp_thrld[CONFIG_OSP_TM_TEMP_SRC_MAX];
    unsigned int radio_txchainmask[CONFIG_OSP_TM_TEMP_SRC_MAX];
    unsigned int fan_rpm;
};

/**
 * Gets thermal management states table
 */
const struct osp_tm_therm_state* osp_tm_get_therm_tbl(void);

/**
 * Gets number of thermal states in the thermal table
 */
int osp_tm_get_therm_states_cnt(void);

/**
 * Gets currently active thermal state which is set by Platform Manager in
 * Node_State
 */
bool osp_tm_ovsdb_get_thermal_state(int *thermal_state);

/**
 * Gets desired fan RPM based on the current thermal state
 */
bool osp_tm_get_fan_rpm_from_thermal_state(const int state, int *fan_rpm);

/**
 * Gets led state from AWLAN_Node as an indication of a hardware error on
 * the device
 */
bool osp_tm_get_led_state(int *led_state);

/**
 * Initialize thermal management subsystem
 *
 * Should return a thermal states table, together with a count of thermal
 * states and count of temperature sources. Thermal states table should go
 * from the lowest thermal state to the highest. First element of the array
 * should have lowest temperature thresholds. Last element of the array
 * should have the highest temperature thresholds. If temperature rises
 * above the highest temperature threshold, the device will be rebooted.
 */
int osp_tm_init(
        const struct osp_tm_therm_state **tbl,
        unsigned int *therm_state_cnt,
        unsigned int *temp_src_cnt,
        void **priv);

/**
 * Thermal management subsystem cleanup
 */
void osp_tm_deinit(void *priv);

/**
 * Return true if temperature source with index idx is currently enabled
 */
bool osp_tm_is_temp_src_enabled(void *priv, int idx);

/**
 * Return the current fan RPM
 */
int osp_tm_get_fan_rpm(void *priv, unsigned int *rpm);

/**
 * Set the desired fan RPM
 */
int osp_tm_set_fan_rpm(void *priv, unsigned int rpm);


/// @} OSP_TM
/// @} OSP

#endif /* OSP_TM_H_INCLUDED */
