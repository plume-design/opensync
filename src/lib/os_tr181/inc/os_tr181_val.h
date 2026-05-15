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
 * os_tr181_val - Value type system for TR-181 parameters
 *
 * Provides a unified value representation that avoids string conversion overhead
 * and preserves type information.
 */

#ifndef OS_TR181_VAL_H
#define OS_TR181_VAL_H

#include "os_tr181_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Generic value type that can hold any TR-181 primitive value
 *
 * Memory management:
 * - Scalar types (int, bool, etc.) require no cleanup
 * - String types may be allocated or static (see 'alloc' flag)
 * - Always call os_val_free() when done with a value
 */
typedef struct
{
    os_tr181_param_type_t type; /* Type indicator */
    bool alloc;                 /* true if 'str' is allocated and needs freeing */
    union
    {
        int i;
        unsigned int u;
        int64_t i64;
        uint64_t u64;
        bool b;
        double d;
        os_tr181_timestamp_t ts;
    };
    char *str; /* for STRING, BASE64, or datetime representation */
} os_tr181_val_t;

/* ========================================================================
 * Memory Management
 * ======================================================================== */

/*
 * Initialize value to NONE type (zeroed)
 */
void os_val_init(os_tr181_val_t *v);

/*
 * Free any allocated memory in value and reset to NONE type
 * Safe to call multiple times or on uninitialized values
 */
void os_val_free(os_tr181_val_t *v);

/*
 * Deep copy value from src to dst
 * Allocates new memory for strings if needed
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_copy(os_tr181_val_t *dst, const os_tr181_val_t *src);

/*
 * Move value from src to dst (transfer ownership)
 * After move, src becomes NONE type
 */
void os_val_move(os_tr181_val_t *dst, os_tr181_val_t *src);

/* ========================================================================
 * Construction Macros (for inline initialization)
 * ======================================================================== */

#define OS_VAL_INIT()      ((os_tr181_val_t){.type = OS_TR181_TYPE_NONE, .alloc = false})
#define OS_VAL_INT(I)      ((os_tr181_val_t){.type = OS_TR181_TYPE_INT, .i = (I), .alloc = false})
#define OS_VAL_UINT(U)     ((os_tr181_val_t){.type = OS_TR181_TYPE_UINT, .u = (U), .alloc = false})
#define OS_VAL_INT64(I64)  ((os_tr181_val_t){.type = OS_TR181_TYPE_INT64, .i64 = (I64), .alloc = false})
#define OS_VAL_UINT64(U64) ((os_tr181_val_t){.type = OS_TR181_TYPE_UINT64, .u64 = (U64), .alloc = false})
#define OS_VAL_BOOL(B)     ((os_tr181_val_t){.type = OS_TR181_TYPE_BOOL, .b = (B), .alloc = false})
#define OS_VAL_DOUBLE(D)   ((os_tr181_val_t){.type = OS_TR181_TYPE_DOUBLE, .d = (D), .alloc = false})

/*
 * String macros
 * OS_VAL_STR_REF - Create value referencing external string
 * The string is NOT copied or owned by the value.
 * Caller must ensure string remains valid for the value's lifetime.
 */
#define OS_VAL_STR_REF(S) ((os_tr181_val_t){.type = OS_TR181_TYPE_STRING, .str = (char *)(S), .alloc = false})

/* ========================================================================
 * Construction Functions (type-safe wrappers)
 * ======================================================================== */

static inline os_tr181_val_t os_val_int(int i)
{
    return OS_VAL_INT(i);
}
static inline os_tr181_val_t os_val_uint(unsigned int u)
{
    return OS_VAL_UINT(u);
}
static inline os_tr181_val_t os_val_int64(int64_t i64)
{
    return OS_VAL_INT64(i64);
}
static inline os_tr181_val_t os_val_uint64(uint64_t u64)
{
    return OS_VAL_UINT64(u64);
}
static inline os_tr181_val_t os_val_bool(bool b)
{
    return OS_VAL_BOOL(b);
}
static inline os_tr181_val_t os_val_double(double d)
{
    return OS_VAL_DOUBLE(d);
}

/* String construction functions (not inline due to strdup allocation) */
os_tr181_val_t os_val_str_ref(const char *s);
os_tr181_val_t os_val_str_dup(const char *s);

/* ========================================================================
 * Setters (replace value in-place)
 * ======================================================================== */

os_tr181_error_t os_val_set_int(os_tr181_val_t *v, int i);
os_tr181_error_t os_val_set_uint(os_tr181_val_t *v, unsigned int u);
os_tr181_error_t os_val_set_int64(os_tr181_val_t *v, int64_t i64);
os_tr181_error_t os_val_set_uint64(os_tr181_val_t *v, uint64_t u64);
os_tr181_error_t os_val_set_bool(os_tr181_val_t *v, bool b);
os_tr181_error_t os_val_set_double(os_tr181_val_t *v, double d);

/*
 * Set datetime value from timestamp structure
 * Timestamp is passed by value (small struct)
 */
os_tr181_error_t os_val_set_datetime(os_tr181_val_t *v, os_tr181_timestamp_t ts);

/*
 * String setters with allocation control
 *
 * os_val_set_str_dup: Makes a copy of the string (safe, allocates memory)
 * os_val_set_str_ref: Borrows the pointer (no allocation, caller manages lifetime)
 *
 * Note: Static strings must remain valid for the lifetime of the value!
 */
os_tr181_error_t os_val_set_str_dup(os_tr181_val_t *v, const char *s);
os_tr181_error_t os_val_set_str_ref(os_tr181_val_t *v, const char *s);

/* ========================================================================
 * Getters (strict type checking)
 *
 * These functions enforce strict type matching with some safe conversions:
 * - Signed/unsigned conversion allowed if value fits
 * - 64-bit to 32-bit conversion allowed if value fits
 * - Returns OS_TR181_ERROR_OVERFLOW if value doesn't fit (stores clamped value)
 * - Returns OS_TR181_ERROR_INVALID for incompatible type conversions
 * ======================================================================== */

os_tr181_error_t os_val_get_int(const os_tr181_val_t *v, int *out);
os_tr181_error_t os_val_get_uint(const os_tr181_val_t *v, unsigned int *out);
os_tr181_error_t os_val_get_int64(const os_tr181_val_t *v, int64_t *out);
os_tr181_error_t os_val_get_uint64(const os_tr181_val_t *v, uint64_t *out);
os_tr181_error_t os_val_get_bool(const os_tr181_val_t *v, bool *out);
os_tr181_error_t os_val_get_double(const os_tr181_val_t *v, double *out);
os_tr181_error_t os_val_get_datetime(const os_tr181_val_t *v, os_tr181_timestamp_t *out);

/*
 * Get string value (returns direct pointer, no allocation)
 *
 * Returns pointer to internal string if value is STRING or BASE64 type
 * Returns NULL and sets error code for other types
 *
 * Note: Returned pointer is valid until value is freed or modified
 * Do NOT free the returned pointer!
 *
 * Parameters:
 *   v     - Value to get string from
 *   error - Optional pointer to receive error code (can be NULL)
 *
 * Returns: String pointer on success, NULL on error
 */
const char *os_val_get_str(const os_tr181_val_t *v, os_tr181_error_t *error);

/* ========================================================================
 * Convenience Getters (return value with default on error)
 * ======================================================================== */

int os_val_get_int_or(const os_tr181_val_t *v, int default_val);
unsigned int os_val_get_uint_or(const os_tr181_val_t *v, unsigned int default_val);
int64_t os_val_get_int64_or(const os_tr181_val_t *v, int64_t default_val);
uint64_t os_val_get_uint64_or(const os_tr181_val_t *v, uint64_t default_val);
bool os_val_get_bool_or(const os_tr181_val_t *v, bool default_val);
double os_val_get_double_or(const os_tr181_val_t *v, double default_val);
const char *os_val_get_str_or(const os_tr181_val_t *v, const char *default_val);

/* ========================================================================
 * Type Conversion (with coercion and parsing)
 *
 * These functions attempt to convert between types intelligently:
 * - Parse strings to numbers
 * - Convert between numeric types with range checking
 * - Convert booleans to/from numbers and strings
 * ======================================================================== */

os_tr181_error_t os_val_to_int(const os_tr181_val_t *v, int *out);
os_tr181_error_t os_val_to_uint(const os_tr181_val_t *v, unsigned int *out);
os_tr181_error_t os_val_to_int64(const os_tr181_val_t *v, int64_t *out);
os_tr181_error_t os_val_to_uint64(const os_tr181_val_t *v, uint64_t *out);
os_tr181_error_t os_val_to_bool(const os_tr181_val_t *v, bool *out);
os_tr181_error_t os_val_to_double(const os_tr181_val_t *v, double *out);

/* ========================================================================
 * String Conversion
 * ======================================================================== */

/*
 * Convert value to string representation (allocates memory)
 *
 * Caller must free returned string with free()
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_to_str(const os_tr181_val_t *v, char **str);

/*
 * Parse string and set value based on type hint
 *
 * Parameters:
 *   v    - Value to set
 *   str  - String to parse
 *   type - Expected type (from os_tr181_param_type_t)
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_from_str(os_tr181_val_t *v, const char *str, os_tr181_param_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* OS_TR181_VAL_H */
