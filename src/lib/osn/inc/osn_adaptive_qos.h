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

#include "osn_types.h"
#include "ds_map_str.h"

#define OSN_ADAPTIVE_QOS_DEFAULT_RAND_REFLECTORS true
#define OSN_ADAPTIVE_QOS_DEFAULT_PING_INTERVAL   300 /* milliseconds */
#define OSN_ADAPTIVE_QOS_DEFAULT_NUM_PINGERS     6
#define OSN_ADAPTIVE_QOS_DEFAULT_ACTIVE_THRESH   2000; /* kbit/s */

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
 * Add a reflector to the (end of the) list of custom reflectors.
 *
 * Custom reflector list is used if at least 1 custom reflector is defined. Otherwise default list
 * of reflectors is used.
 *
 * @param self[in]          A valid @ref osn_adaptive_qos_t object
 *
 * @param ip_addr[in]       IPv4 or IPv6 reflector address
 *
 * @return true on success
 */
bool osn_adaptive_qos_reflector_add(osn_adaptive_qos_t *self, const osn_ipany_addr_t *ip_addr);

/**
 * Convenience function to add/define a list of custom reflectors in one shot.
 *
 * @param self[in]          A valid @ref osn_adaptive_qos_t object
 *
 * @param ip_addr_list[in]  Array of IPv4 or IPv6 reflector addresses to be used as custom reflector list
 *
 * @param num[in]           Number of reflectors on the list
 *
 * @return true on success
 */
bool osn_adaptive_qos_reflector_list_add(osn_adaptive_qos_t *self, const osn_ipany_addr_t ip_addr_list[], int num);

/**
 * Clear the list of custom reflectors. Default reflectors will be used.
 *
 * @param self[in]          A valid @ref osn_adaptive_qos_t object
 *
 * @return true on success
 */
void osn_adaptive_qos_reflectors_list_clear(osn_adaptive_qos_t *self);

/**
 * Enable or disable randomization of reflectors at startup. Default is true.
 *
 * @param self[in]          A valid @ref osn_adaptive_qos_t object
 *
 * @param randomize[in]     Randomize or not the reflectors list
 *
 * @return true on success
 */
bool osn_adaptive_qos_reflectors_randomize_set(osn_adaptive_qos_t *self, bool randomize);

/**
 * Set interval time for ping.
 *
 * If not set, default is @ref OSN_ADAPTIVE_QOS_DEFAULT_PING_INTERVAL
 *
 * @param self[in]          A valid @ref osn_adaptive_qos_t object
 *
 * @param ping_interval[in] Interval time for ping in milliseconds.
 *
 * @return true on success
 */
bool osn_adaptive_qos_reflectors_ping_interval_set(osn_adaptive_qos_t *self, int ping_interval);

/**
 * Set number of pingers to maintain.
 *
 * If not set, default is @ref OSN_ADAPTIVE_QOS_DEFAULT_NUM_PINGERS
 *
 * @param self[in]          A valid @ref osn_adaptive_qos_t object
 *
 * @param num_pingers[in]   Number of pingers to maintain
 *
 * @return true on success
 */
bool osn_adaptive_qos_num_pingers_set(osn_adaptive_qos_t *self, int num_pingers);

/**
 * Set threshold bellow which DL/UL is considered idle.
 *
 * If not set, default is @ref OSN_ADAPTIVE_QOS_DEFAULT_ACTIVE_THRESH
 *
 * @param self[in]          A valid @ref osn_adaptive_qos_t object
 *
 * @param threshold[in]     Threshold in Kbit/s below which DL/UL is considered idle
 *
 * @return true on success
 */
bool osn_adaptive_qos_active_threshold_set(osn_adaptive_qos_t *self, int threshold);

/**
 * Set other config key/value params.
 *
 * @param self[in]          A valid @ref osn_adaptive_qos_t object
 *
 * @param other_config[in]  Other config key/value parameters
 *
 * @return true on success
 */
bool osn_adaptive_qos_other_config_set(osn_adaptive_qos_t *self, const ds_map_str_t *other_config);

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
