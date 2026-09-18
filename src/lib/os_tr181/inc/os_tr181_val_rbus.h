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

/*
 * os_tr181_val_rbus.h - RBUS rbusValue_t conversion functions
 *
 * This header declares functions for converting between os_tr181_val_t
 * and RBUS rbusValue_t types.
 */

#ifndef OS_TR181_VAL_RBUS_H
#define OS_TR181_VAL_RBUS_H

#include "os_tr181_types.h"
#include "os_tr181_val.h"
#include <rbus/rbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert os_tr181_val_t to rbusValue_t
 *
 * Parameters:
 *   val - Source value to convert
 *   rbus_val - Output RBUS value (caller must release with rbusValue_Release)
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_val_to_rbus(const os_tr181_val_t *val, rbusValue_t *rbus_val);

/*
 * Convert rbusValue_t to os_tr181_val_t
 *
 * Parameters:
 *   val - Output value (caller must initialize and free with os_val_free)
 *   rbus_val - Source RBUS value
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_val_from_rbus(os_tr181_val_t *val, rbusValue_t rbus_val);

/*
 * Convert rbusObject_t to os_tr181_val_t (as dict)
 *
 * Parameters:
 *   val - Output value (caller must initialize and free with os_val_free)
 *   obj - Source RBUS object
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_tr181_val_from_rbus_object(os_tr181_val_t *val, rbusObject_t obj);

#ifdef __cplusplus
}
#endif

#endif /* OS_TR181_VAL_RBUS_H */
