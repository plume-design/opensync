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

#ifndef OSP_UPG_H_INCLUDED
#define OSP_UPG_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>


/// @file
/// @brief Upgrade API
///
/// @addtogroup OSP
/// @{


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

#endif /* OSP_UPG_H_INCLUDED */
