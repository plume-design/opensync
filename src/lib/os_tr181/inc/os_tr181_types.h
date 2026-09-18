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
    OS_TR181_ERROR_OVERFLOW = -7,

    /* Async operation status codes */
    OS_TR181_ERROR_DEFERRED = -100,     /* Method will complete asynchronously (not an error) */
    OS_TR181_ERROR_ASYNC_TIMEOUT = -101 /* Async operation timed out */
} os_tr181_error_t;

/* ========================================================================
 * Parameter types
 * ======================================================================== */

/*
 * NOTE: This enum serves dual purpose:
 * 1. Value Types: Used in os_tr181_val_t to describe runtime data
 *    - DICT and LIST are value-only types (structured runtime data)
 * 2. Path Types: Returned by os_tr181_list() to describe TR-181 tree nodes
 *    - OBJECT and INSTANCE are path-only types (describe tree structure)
 * Simple types (STRING, INT, etc.) work in both contexts.
 */
typedef enum
{
    OS_TR181_TYPE_NONE = 0, /* Null/no value (valid type, like JSON null) */

    /* Simple types (used for both paths and values) */
    OS_TR181_TYPE_STRING,
    OS_TR181_TYPE_INT,
    OS_TR181_TYPE_UINT,
    OS_TR181_TYPE_INT64,
    OS_TR181_TYPE_UINT64,
    OS_TR181_TYPE_BOOL,
    OS_TR181_TYPE_DOUBLE,
    OS_TR181_TYPE_DATETIME,
    OS_TR181_TYPE_BASE64,

    /* Value-only types (structured runtime data) */
    OS_TR181_TYPE_DICT, /* Dictionary/map - key-value pairs (value type only) */
    OS_TR181_TYPE_LIST, /* List/array - ordered sequence (value type only) */

    /* Path-only types (describe TR-181 tree structure) */
    OS_TR181_TYPE_OBJECT,   /* TR-181 object path (container with children) */
    OS_TR181_TYPE_TABLE,    /* TR-181 multi-instance table */
    OS_TR181_TYPE_INSTANCE, /* TR-181 table instance */
    OS_TR181_TYPE_METHOD,   /* TR-181 RPC method */
    OS_TR181_TYPE_EVENT,    /* TR-181 USP event */
    OS_TR181_TYPE_PROPERTY, /* TR-181 property with unknown/unspecified data type */
} os_tr181_param_type_t;

/* ========================================================================
 * Parameter Access Flags
 * ======================================================================== */

/*
 * Access control flags for parameter registration.
 * Supports TR-181 standard access types: readOnly, readWrite, writeOnceReadOnly.
 *
 * Individual bit flags:
 * - OS_TR181_ACCESS_READ_FLAG: Parameter can be read
 * - OS_TR181_ACCESS_WRITE_FLAG: Parameter can be written
 * - OS_TR181_ACCESS_WRITE_ONCE_FLAG: Parameter can only be written once (constraint)
 * - OS_TR181_ACCESS_KEY_FLAG: Parameter is a table instance key (unique identifier)
 *
 * Standard access patterns:
 * - OS_TR181_ACCESS_READONLY: Read-only parameter (TR-181 readOnly)
 * - OS_TR181_ACCESS_READWRITE: Read-write parameter (TR-181 readWrite)
 * - OS_TR181_ACCESS_WRITEONCE: Write-once then read-only (TR-181 writeOnceReadOnly)
 *
 * Table Instance Keys:
 * - Use OS_TR181_ACCESS_KEY_FLAG combined with access patterns
 * - Keys are always unique per TR-181 standard
 * - Typically used for Alias parameters in multi-instance tables (Device.Foo.{i}.Alias)
 * - Only valid for template parameters (path contains {i})
 *
 * Write-Once Enforcement:
 * - For table keys: Native platform support (Ambiorix attributes, RBUS/CCSP behavior)
 * - For singleton params: RBUS/CCSP uses library tracking (Ambiorix per standard: keys only)
 *
 * Example usage:
 *   // Regular read-only parameter
 *   os_tr181_register_parameter(handle, "Device.DeviceInfo.ModelName",
 *                               OS_TR181_TYPE_STRING, OS_TR181_ACCESS_READONLY,
 *                               get_cb, NULL, data);
 *
 *   // Table instance key (Alias) - write-once
 *   os_tr181_register_parameter(handle, "Device.WiFi.SSID.{i}.Alias",
 *                               OS_TR181_TYPE_STRING,
 *                               OS_TR181_ACCESS_KEY_FLAG | OS_TR181_ACCESS_WRITEONCE,
 *                               get_cb, set_cb, data);
 */

/* Individual bit flags */
#define OS_TR181_ACCESS_READ_FLAG       (1 << 0) /* 0x01 - Read capability */
#define OS_TR181_ACCESS_WRITE_FLAG      (1 << 1) /* 0x02 - Write capability */
#define OS_TR181_ACCESS_WRITE_ONCE_FLAG (1 << 2) /* 0x04 - Write-once constraint */
#define OS_TR181_ACCESS_KEY_FLAG        (1 << 3) /* 0x08 - Table instance key parameter */
#define OS_TR181_ACCESS_PERSISTENT_FLAG (1 << 4) /* 0x10 - Persist value across restarts */

/*
 * Ambiorix backend persistence configuration (environment variables)
 *
 * The following environment variables control ODL persistence behaviour when
 * using the Ambiorix backend.  They are read once at os_tr181_init_ex() time.
 * The CCSP backend uses PSM and ignores these variables entirely.
 *
 *   OS_TR181_AMX_PERSIST_STORAGE_DIR
 *     Directory in which per-object ODL save files are written.
 *     Default: /etc/config/os_tr181
 *
 *   OS_TR181_AMX_PERSIST_SAVE_DELAY_MS
 *     Debounce window in milliseconds: the file is written this long after
 *     the last change.  Values <= 0 are ignored and the default is used.
 *     Default: 1000 ms
 *
 *   OS_TR181_AMX_PERSIST_INIT_DELAY_MS
 *     Delay in milliseconds before the very first save after startup.
 *     This prevents a write burst during object registration and initialisation.
 *     Values <= 0 are ignored and the default is used.
 *     Default: 5000 ms
 */

/* Standard access patterns (public API) */
#define OS_TR181_ACCESS_READONLY  OS_TR181_ACCESS_READ_FLAG
#define OS_TR181_ACCESS_READWRITE (OS_TR181_ACCESS_READ_FLAG | OS_TR181_ACCESS_WRITE_FLAG)
#define OS_TR181_ACCESS_WRITEONCE \
    (OS_TR181_ACCESS_READ_FLAG | OS_TR181_ACCESS_WRITE_FLAG | OS_TR181_ACCESS_WRITE_ONCE_FLAG)

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
