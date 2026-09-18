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
 * os_tr181_val_json.h - JSON conversion functions for os_tr181_val_t
 *
 * This header declares functions for converting between os_tr181_val_t
 * and JSON format using the jansson library.
 */

#ifndef OS_TR181_VAL_JSON_H
#define OS_TR181_VAL_JSON_H

#include "os_tr181_types.h"
#include "os_tr181_val.h"
#include <jansson.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Convert os_tr181_val_t to json_t
 *
 * Parameters:
 *   val - Source value to convert
 *   json - Output JSON object (caller must free with json_decref)
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_to_json(const os_tr181_val_t *val, json_t **json);

/*
 * Convert json_t to os_tr181_val_t
 *
 * Parameters:
 *   val - Output value (caller must initialize and free with os_val_free)
 *   json - Source JSON object
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_from_json(os_tr181_val_t *val, const json_t *json);

/*
 * Convert os_tr181_val_t to JSON string
 *
 * Parameters:
 *   val - Source value to convert
 *   json_str - Output JSON string (caller must free with free())
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_to_json_string(const os_tr181_val_t *val, char **json_str);

/*
 * Convert JSON string to os_tr181_val_t
 *
 * Parameters:
 *   val - Output value (caller must initialize and free with os_val_free)
 *   json_str - Source JSON string
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_from_json_string(os_tr181_val_t *val, const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* OS_TR181_VAL_JSON_H */
