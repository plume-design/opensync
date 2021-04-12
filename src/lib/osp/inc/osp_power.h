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

#ifndef OSP_POWER_H_INCLUDED
#define OSP_POWER_H_INCLUDED

#include <stdio.h>
#include <stdbool.h>


/// @file
/// @brief Power API
///
/// @addtogroup OSP
/// @{


// ===========================================================================
//  Power API
// ===========================================================================

/// @defgroup OSP_POWER Power API
/// OpenSync Power API
/// @{


/**
 * Power supply type
 */
enum osp_power_ps_type {
    OSP_POWER_PS_TYPE_UNKNOWN,
    OSP_POWER_PS_TYPE_AC,
    OSP_POWER_PS_TYPE_BATTERY,
    OSP_POWER_PS_TYPE_POE,
    OSP_POWER_PS_TYPE_POE_PLUS
};

/**
 * @brief Get type of the power supply
 *
 * This function provides information about the type of
 * the power supply source. There are three supported types:
 * - AC/DC
 * - PoE
 * - Battery
 *
 * @param type  Power supply type
 *
 * @return true on success
 *
 */
bool osp_power_get_power_supply_type(enum osp_power_ps_type *type);

/**
 * @brief Get power consumption
 *
 * This function provides information about power
 * conspution of the unit.
 *
 * @param milliwatts  Power consumption in milliwatts
 *
 * @return true on success
 *
 */
bool osp_power_get_power_consumption(uint32_t *milliwatts);

/**
 * @brief Get battery level
 *
 * In case device supports battery as power supply
 * source, this function provides information
 * about the current level of the battery.
 *
 *
 * @param batt_level  Battery level from 0% to 100%
 *
 * @return true on success
 *
 */
bool osp_power_get_battery_level(uint8_t *batt_level);


/// @} OSP_POWER
/// @} OSP

#endif /* OSP_POWER_H_INCLUDED */
