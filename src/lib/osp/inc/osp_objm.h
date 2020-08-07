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

#ifndef OSP_OBJM_H_INCLUDED
#define OSP_OBJM_H_INCLUDED

#include <stdio.h>
#include <stdbool.h>


/// @file
/// @brief OSP Object Management API
///
/// @addtogroup OSP
/// @{


// ===========================================================================
//  Object Management API
// ===========================================================================

/// @defgroup OSP_OBJM Object Management API
/// OpenSync Object Management API
/// @{


/**
 * Install object to object storage
 *
 * @param[in] path      Path where the file for installation is located
 * @param[in] name      Name of the object
 * @param[in] version   Version of object
 *
 * @return true if install is successful
 *
 */
bool osp_objm_install(char *path, char *name, char *version);

/**
 * Remove object from object storage
 *
 * @param[in] name      Name of the object
 * @param[in] version   Version of object
 *
 * @return true if removal is successful
 *
 */
bool osp_objm_remove(char *name, char *version);

/**
 * Get path on filesystem where installed object is available
 *
 * @param[out] buf       Buffer in which path of object is returned
 * @param[in]  buffsz    Size of the provided buffer
 * @param[in]  name      Name of the object
 * @param[in]  version   Version of object
 *
 * @return true if buf is populated with path
 *
 */
bool osp_objm_path(char *buf, size_t buffsz, char *name, char *version);


/// @} OSP_OBJM
/// @} OSP

#endif /* OSP_OBJM_H_INCLUDED */
