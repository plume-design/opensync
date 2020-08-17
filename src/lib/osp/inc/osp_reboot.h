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

#ifndef OSP_REBOOT_H_INCLUDED
#define OSP_REBOOT_H_INCLUDED

#include <stdbool.h>
#include <stdio.h>


/// @file
/// @brief Reboot API
///
/// @addtogroup OSP
/// @{


// ===========================================================================
//  Reboot API
// ===========================================================================

/// @defgroup OSP_REBOOT Reboot API
/// OpenSync Reboot API
/// @{

/**
 * Reboot type
 */
enum osp_reboot_type
{
    OSP_REBOOT_UNKNOWN,         /**< Unknown reboot reason */
    OSP_REBOOT_COLD_BOOT,       /**< Power-on / cold boot */
    OSP_REBOOT_POWER_CYCLE,     /**< Power cycle or spontaneous reset */
    OSP_REBOOT_WATCHDOG,        /**< Watchdog triggered reboot */
    OSP_REBOOT_CRASH,           /**< Reboot due to kernel/system/driver crash */
    OSP_REBOOT_USER,            /**< Human triggered reboot (via shell or otherwise) */
    OSP_REBOOT_DEVICE,          /**< Device initiated reboot (upgrade, health check or otherwise) */
    OSP_REBOOT_HEALTH_CHECK,    /**< Health check failed */
    OSP_REBOOT_UPGRADE,         /**< Reboot due to an upgrade */
    OSP_REBOOT_THERMAL,         /**< Reboot due to a thermal event */
    OSP_REBOOT_CLOUD,           /**< Cloud initiated reboot */
    OSP_REBOOT_CANCEL           /**< Cancel last reboot record */
};

/**
 * Unit reboot
 *
 * @param[in] type          Reboot type (request source)
 * @param[in] reason        Reboot reason (description)
 * @param[in] ms_delay      Delay actual reboot in ms
 *
 * @return true on success
 *
 * @note
 * If the reboot type is OSP_REBOOT_CANCEL, the last record reboot record is
 * invalidated.
 */
bool osp_unit_reboot_ex(enum osp_reboot_type type, const char *reason, int ms_delay);

/** osp_unit_reboot() is a simplified alias to osp_unit_reboot_ex() */
static inline bool osp_unit_reboot(const char *reason, int ms_delay)
{
    return osp_unit_reboot_ex(OSP_REBOOT_DEVICE, reason, ms_delay);
}

/**
 * Unit reboot & factory reset
 *
 * @param[in] reason        Reboot reason (description)
 * @param[in] ms_delay      Delay actual factory reboot in ms
 *
 * @return true on success
 */
bool osp_unit_factory_reboot(const char *reason, int ms_delay);

/**
 * This function returns the last reboot type and reason
 *
 * @param[out]  type        Returns an enum of type osp_reboot_type
 * @param[out]  reason      Returns the reboot reason (as string)
 * @param[out]  reason_sz   Maximum size of @p reason
 *
 * @return
 * This function returns true if it was able to successfully detect
 * the reboot type, or false in case of an error.
 */
bool osp_unit_reboot_get(enum osp_reboot_type *type, char *reason, ssize_t reason_sz);


/// @} OSP_REBOOT
/// @} OSP

#endif /* OSP_REBOOT_H_INCLUDED */
