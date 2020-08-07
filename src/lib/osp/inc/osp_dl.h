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

#ifndef OSP_DL_H_INCLUDED
#define OSP_DL_H_INCLUDED

#include <stdio.h>


/// @file
/// @brief OSP Download API
///
/// @addtogroup OSP
/// @{


// ===========================================================================
//  Download API
// ===========================================================================

/// @defgroup OSP_DL  Download API
/// OpenSync Download API
/// @{


/** Enum osp_dl_status for status reporting */
enum osp_dl_status
{
    OSP_DL_OK = 0,              ///< Download OK
    OSP_DL_DOWNLOAD_FAILED,     ///< Download failed
    OSP_DL_ERROR                ///< General download error
};

/**
 * Complete download callback function
 *
 * @param[in] status  Status of finished download
 * @param[in] cb_ctx  Context struct of osp_dl_download caller.
 *
 */
typedef void (*osp_dl_cb)(const enum osp_dl_status status, void *cb_ctx);

/**
 * Function to download a file from @p url to @p dst_path.
 * Non-blocking implementation is expected. After a successful download,
 * a failure, or an expired timeout, it is expected that dl_cb callback
 * is called with the status.
 *
 * @param[in] url       URL of file to download
 * @param[in] dst_path  Path where to download the file to
 * @param[in] timeout   Timeout for the download operation
 * @param[in] dl_cb     Callback for when downloading is finished, or failure or a timeout occurred
 * @param[in] cb_ctx    Caller context struct that is passed in dl_cb callback
 *
 * @return true if download is started successfully
 *
 */
bool osp_dl_download(char *url, char *dst_path, int timeout, osp_dl_cb dl_cb, void *cb_ctx);


/// @} OSP_DL
/// @} OSP

#endif /* OSP_DL_H_INCLUDED */
