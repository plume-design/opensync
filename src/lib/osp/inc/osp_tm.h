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
 * Maximum number of temperature sources
 */
#define OSP_TM_TEMP_SRC_MAX     (3)

/**
 * Averaging window size
 *
 * Measure running average of temperature over this number of temperature
 * samples. This is a compromise between a low number of samples to react to
 * fast rising temperature, and a high number of samples to react to bad
 * temperature readings.
 */
#define OSP_TM_TEMP_AVG_CNT     (3)

/**
 * Thermal state table element
 */
struct osp_tm_therm_state
{
    int temp_thrld[OSP_TM_TEMP_SRC_MAX];
    unsigned int radio_txchainmask[OSP_TM_TEMP_SRC_MAX];
    unsigned int fan_rpm;
};

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
 * Return the name of the temperature source with index idx
 */
const char* osp_tm_get_temp_src_name(void *priv, int idx);

/**
 * Return the temperature of the requested temperature source
 */
int osp_tm_get_temperature(void *priv, int idx, int *temp);

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
