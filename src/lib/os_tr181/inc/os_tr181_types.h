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
 * os_tr181_types - Common type definitions for TR-181 library
 *
 * This header contains fundamental types and constants used throughout
 * the os_tr181 library. It has no dependencies and can be included
 * by any other header or implementation file.
 */

#ifndef OS_TR181_TYPES_H
#define OS_TR181_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Return codes / Error type
 * ======================================================================== */

typedef enum
{
    OS_TR181_SUCCESS = 0,
    OS_TR181_ERROR = -1,
    OS_TR181_ERROR_INIT = -2,
    OS_TR181_ERROR_NOT_FOUND = -3,
    OS_TR181_ERROR_INVALID = -4,
    OS_TR181_ERROR_TIMEOUT = -5,
    OS_TR181_ERROR_NOT_IMPLEMENTED = -6,
    OS_TR181_ERROR_OVERFLOW = -7
} os_tr181_error_t;

/* ========================================================================
 * Parameter types
 * ======================================================================== */

typedef enum
{
    OS_TR181_TYPE_NONE = 0, /* Uninitialized/invalid */
    OS_TR181_TYPE_STRING,
    OS_TR181_TYPE_INT,
    OS_TR181_TYPE_UINT,
    OS_TR181_TYPE_INT64,
    OS_TR181_TYPE_UINT64,
    OS_TR181_TYPE_BOOL,
    OS_TR181_TYPE_DOUBLE,
    OS_TR181_TYPE_DATETIME,
    OS_TR181_TYPE_BASE64,
    OS_TR181_TYPE_OBJECT,
    OS_TR181_TYPE_INSTANCE
} os_tr181_param_type_t;

/* ========================================================================
 * Timestamp structure
 * ======================================================================== */

/*
 * Timestamp structure for datetime values
 * Represents time in seconds since Unix epoch with timezone offset
 * Sufficient for TR-181 SOAP dateTime (ISO 8601 subset)
 */
typedef struct
{
    int64_t sec;    /* Seconds since epoch (1970-01-01T00:00:00Z) */
    int16_t offset; /* Offset from UTC in minutes [-1439, 1439] */
} os_tr181_timestamp_t;

#ifdef __cplusplus
}
#endif

#endif /* OS_TR181_TYPES_H */
