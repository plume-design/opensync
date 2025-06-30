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

#ifndef OSP_TM_SENSORS_H_INCLUDED
#define OSP_TM_SENSORS_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <ev.h>

#include "osn_types.h"

/// @file
/// @brief Thermal Management Sensors API
///
/// @addtogroup OSP
/// @{

// ===========================================================================
//  Thermal Management Sensors API
// ===========================================================================

/// @defgroup OSP_TMS  Thermal Management Sensors API
/// OpenSync Thermal Management Sensors API
/// @{

/**
 * Check if the external temperature sensor is present on index `idx` which
 * corresponds to the `osp_temp_srcs` array index in osp_temp_srcs.c.
 *
 * @param idx Index of the temperature source
 *
 * @return true if the sensor is present, false otherwise
 */
bool osp_tm_sensors_is_temp_snsr_present(int idx);

/**
 * Get the temperature from the external sensor on index `idx` which
 * corresponds to the `osp_temp_srcs` array index in osp_temp_srcs.c
 * in degrees Celsius.
 *
 * @param idx Index of the temperature source
 * @param temp Pointer to store the temperature value
 *
 * @return true on success, false on failure
 */
bool osp_tm_sensors_get_temp_snsr_val(int idx, int *temp);

/// @} OSP_TMS
/// @} OSP

#endif /* OSP_TM_SENSORS_H_INCLUDED */
