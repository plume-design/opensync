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

#if !defined(OSP_H_INCLUDED)
#define OSP_H_INCLUDED

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "osp_tm.h"
#include "osp_led.h"


/// @file
/// @brief Platform APIs
///
/// @defgroup OSP  OpenSync Platform API
/// OpenSync Platform API and types
/// @{


// ===========================================================================
//  Button API
// ===========================================================================

/// @defgroup OSP_BTN Button API
/// OpenSync Button API
/// @{

/**
 * Enumeration of buttons supported by OpenSync
 */
enum osp_btn_name
{
    OSP_BTN_NAME_RESET = (1 << 0),      /**< Factory reset button */
    OSP_BTN_NAME_WPS = (1 << 1),        /**< WiFi WPS button */

    /* More buttons can be added */
};

/**
 * Get the capabilities related to the buttons
 *
 * @param[out] caps Bitmask of buttons supported by the target
 *                  You can test if a button is supported by testing the bitmask
 *                  For example, to test if the reset button is supported by the
 *                  target, you can test (caps & @ref OSP_BTN_NAME_RESET)
 *
 * @return true on success
 */
bool osp_btn_get_caps(uint32_t *caps);

/**
 * Definition of an event associated with a button
 *
 * Example 1: Button is pushed
 *            - pushed = true
 *            - duration = 0
 *            - double_click = false
 *
 * Example 2: Button is double click
 *            - pushed = false
 *            - duration = 0
 *            - double_click = true
 *
 * Example 3: Button is released after 1 second
 *            - pushed = false
 *            - duration = 1000
 *            - double_click = false
 *
 * Example 4: Button is released after 5 seconds
 *            - pushed = false
 *            - duration = 5000
 *            - double_click = false
 */
struct osp_btn_event
{
    /**
     * True if the button is pushed, false if the button is released
     */
    bool pushed;

    /**
     * Duration in milliseconds of pressing the button
     *
     * Valid only when the button is released and it was not a double click.
     */
    unsigned int duration;

    /**
     * True if the button is pushed and released two times in less than
     * 1000 milliseconds
     *
     * Valid only when the button is released.
     */
    bool double_click;
};

/**
 * Callback called by the target layer when an event is received on a button
 *
 * @param[in] obj    Pointer to the object that was supplied when the callback
 *                   was registered (@ref osp_btn_register call)
 * @param[in] name   Button associated with the event
 * @param[in] event  Details of the button event
 */
typedef void (*osp_btn_cb)(void *obj, enum osp_btn_name name, const struct osp_btn_event *event);

/**
 * Register the callback to receive button events
 *
 * @param[in] cb  Callback called by the target layer when an event is received
 *                on a button.
 *                If callback is NULL, the target must unregister the previous
 *                one for this specific obj.
 * @param[in] obj User pointer which will be given back when the callback will
 *                be called
 *
 * @return true on success
 */
bool osp_btn_register(osp_btn_cb cb, void *obj);

/// @} OSP_BTN


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


// ===========================================================================
//  Upgrade API
// ===========================================================================

/// @defgroup OSP_UPG  Upgrade API
/// OpenSync Upgrade API
/// @{

/**
 * Type of upgrade operations
 */
typedef enum
{
    OSP_UPG_DL,     /**< Download of the upgrade file */
    OSP_UPG_UPG     /**< Upgrade process              */
} osp_upg_op_t;


/**
 * Upgrade operations status
 */
typedef enum
{
    OSP_UPG_OK           = 0,   /**< Success                       */
    OSP_UPG_ARGS         = 1,   /**< Wrong arguments (app error)   */
    OSP_UPG_URL          = 3,   /**< Error setting url             */
    OSP_UPG_DL_FW        = 4,   /**< DL of FW image failed         */
    OSP_UPG_DL_MD5       = 5,   /**< DL of *.md5 sum failed        */
    OSP_UPG_MD5_FAIL     = 6,   /**< md5 CS failed or platform     */
    OSP_UPG_IMG_FAIL     = 7,   /**< Image check failed            */
    OSP_UPG_FL_ERASE     = 8,   /**< Flash erase failed            */
    OSP_UPG_FL_WRITE     = 9,   /**< Flash write failed            */
    OSP_UPG_FL_CHECK     = 10,  /**< Flash verification failed     */
    OSP_UPG_BC_SET       = 11,  /**< New FW commit failed          */
    OSP_UPG_APPLY        = 12,  /**< Applying new FW failed        */
    OSP_UPG_BC_ERASE     = 14,  /**< Clean FW commit info failed   */
    OSP_UPG_SU_RUN       = 15,  /**< Upgrade in progress running   */
    OSP_UPG_DL_NOFREE    = 16,  /**< Not enough free space on unit */
    OSP_UPG_WRONG_PARAM  = 17,  /**< Wrong flashing parameters     */
    OSP_UPG_INTERNAL     = 18   /**< Internal error                */
} osp_upg_status_t;


/**
 * Callback invoked by target layer during download & upgrade process
 *
 * @param[in] op - operation: download, download CS file or upgrade
 * @param[in] status status
 * @param[in] completed percentage of completed work 0 - 100%
 */
typedef void (*osp_upg_cb)(const osp_upg_op_t op,
                           const osp_upg_status_t status,
                           uint8_t completed);


/**
 * Check system requirements for upgrade, like no upgrade in progress,
 * available flash space etc.
 */
bool osp_upg_check_system(void);

/**
 * Download an image suitable for upgrade from @p uri and store it locally.
 * Upon download and verification completion, invoke the @p dl_cb callback.
 */
bool osp_upg_dl(char *url, uint32_t timeout, osp_upg_cb dl_cb);

/**
 * Write the previously downloaded image to the system. If the image
 * is encrypted, a password must be specified in @p password.
 *
 * After the image was successfully applied, the @p upg_cb callback is invoked.
 */
bool osp_upg_upgrade(char *password, osp_upg_cb upg_cb);

/**
 * On dual-boot system, flag the newly flashed image as the active one.
 * This can be a no-op on single image systems.
 */
bool osp_upg_commit(void);

/**
 * Return a more detailed error code related to a failed osp_upg_*() function
 * call. See osp_upg_status_t for a detailed list of error codes.
 */
int osp_upg_errno(void);


/// @} OSP_UPG
/// @} OSP

#endif /* OSP_H_INCLUDED */
