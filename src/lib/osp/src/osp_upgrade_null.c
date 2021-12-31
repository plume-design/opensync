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

#include <sys/types.h>

#include "log.h"
#include "util.h"
#include "memutil.h"
#include "osp_upg.h"

/**
 * @brief Check system requirements for upgrade, like
 *        no upgrade in progress, available flash space etc
 */
bool osp_upg_check_system(void)
{
    return false;
}

/**
 * Download an image suitable for upgrade from @p uri store it locally.
 * Upon download and verification completion, invoke the @p dl_cb callback.
 */
bool osp_upg_dl(char * url, uint32_t timeout, osp_upg_cb dl_cb)
{
    (void)url;
    (void)timeout;
    (void)dl_cb;

    return false;
}

/**
 * Write the previously downloaded image to the system. If the image
 * is encrypted, a password must be specified in @password.
 *
 * After the image was successfully applied, the @p upg_cb callback is invoked.
 */
bool osp_upg_upgrade(char *password, osp_upg_cb upg_cb)
{
    (void)password;
    (void)upg_cb;

    return false;
}

/**
 * On dual-boot system, flag the newly flashed image as the active one.
 * This can be a no-op on single image systems.
 */
bool osp_upg_commit(void)
{
    return false;
}

/**
 * Activate to the new image (must be called after a osp_upg_commit()).
 * This implies a reboot of the system.
 */
bool osp_upg_apply(uint32_t timeout_ms)
{
    (void)timeout_ms;

    return false;
}

/**
 * Return more detailed error code in relation to a failed osp_upg_() function.
 * See osp_upg_status_t for a detailed list of error codes.
 */
int osp_upg_errno(void)
{
    return 1;
}

