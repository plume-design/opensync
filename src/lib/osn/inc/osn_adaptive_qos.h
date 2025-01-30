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

#ifndef OSN_ADAPTIVE_QOS_H_INCLUDED
#define OSN_ADAPTIVE_QOS_H_INCLUDED

#include <stdbool.h>

/**
 * @file osn_adaptive_qos.h
 *
 * @brief OpenSync Adaptive QoS API
 *
 * @defgroup OSN_ADAPTIVE_QOS OpenSync Adaptive QoS APIs
 *
 * OpenSync Adaptive QoS API
 *
 * @{
 */

/**
 * Adaptive QoS object type.
 *
 * This is an opaque type. The actual structure implementation is hidden as
 * it depends on the underlying backend implementation.
 *
 * A new instance of the object can be obtained by calling @ref osn_adaptive_qos_new()
 * and must be destroyed using @ref osn_adaptive_qos_del().
 */
typedef struct osn_adaptive_qos osn_adaptive_qos_t;

/**
 * Create a new instance of osn_adaptive_qos_t object.
 *
 * @param[in] DL_ifname     Download interface name
 *
 * @param[in] DL_ifname     Upload interface name
 *
 * @return  A valid osn_adaptive_qos_t object or NULL on error.
 */
osn_adaptive_qos_t *osn_adaptive_qos_new(const char *DL_ifname, const char *UL_ifname);

/**
 * Enable or disable actually changing the download shaper rate.
 *
 * Default is enabled.
 *
 * If disabled, the underlying backend may only monitor the connection.
 *
 * @param[in] shaper_adjust     Adjust shaper in the download direction? Default is true.
 *
 * @return  true on success
 */
bool osn_adaptive_qos_DL_shaper_adjust_set(osn_adaptive_qos_t *self, bool shaper_adjust);

/**
 * Set adaptive shaper parameters for the download direction.
 *
 * @param self[in]          A valid @ref osn_adaptive_qos_t object.
 *
 * @param min_rate[in]      Minimum bandwidth for download (Kbit/s)
 *
 * @param base_rate[in]     Steady state bandwidth for download (Kbit/s)
 *
 * @param max_rate[in]      Maximum bandwidth for download (Kbit/s)
 *
 * @return true on success.
 */
bool osn_adaptive_qos_DL_shaper_params_set(osn_adaptive_qos_t *self, int min_rate, int base_rate, int max_rate);

/**
 * Enable or disable actually changing the upload shaper rate.
 *
 * Default is enabled.
 *
 * If disabled, the underlying backend may only monitor the connection.
 *
 * @param[in] shaper_adjust     Adjust shaper in the upload direction? Default is true.
 *
 * @return  true on success
 */
bool osn_adaptive_qos_UL_shaper_adjust_set(osn_adaptive_qos_t *self, bool shaper_adjust);

/**
 * Set adaptive shaper parameters for the upload direction.
 *
 * @param self[in]          A valid @ref osn_adaptive_qos_t object.
 *
 * @param min_rate[in]      Minimum bandwidth for upload (Kbit/s)
 *
 * @param base_rate[in]     Steady state bandwidth for upload (Kbit/s)
 *
 * @param max_rate[in]      Maximum bandwidth for upload (Kbit/s)
 *
 * @return true on success.
 */
bool osn_adaptive_qos_UL_shaper_params_set(osn_adaptive_qos_t *self, int min_rate, int base_rate, int max_rate);

/**
 * Apply the Adaptive QoS configuration parameters to the running system.
 *
 * After you set Adaptive QoS configuration parameters you must call this
 * function to ensure the configuration takes effect.
 *
 * @param[in] self           A valid @ref osn_adaptive_qos_t object
 *
 * @return true on success
 */
bool osn_adaptive_qos_apply(osn_adaptive_qos_t *self);

/**
 * Destroy a valid osn_adaptive_qos_t object.
 *
 * If Adaptive QoS configuration was applied and Adaptive QoS service started, it
 * will be deconfigured and stopped.
 *
 * @param[in] self          A valid @ref osn_adaptive_qos_t object
 *
 * @return true on success. Regardless of the return value after this function
 *         returns the input parameter should be considered invalid and must
 *         no longer be used.
 */
bool osn_adaptive_qos_del(osn_adaptive_qos_t *self);

#endif /* OSN_ADAPTIVE_QOS_H_INCLUDED */
