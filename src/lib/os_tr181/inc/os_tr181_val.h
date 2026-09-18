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

#include <stddef.h>
#include "os_tr181_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration - container for DICT/LIST types (definition in os_tr181_val.c) */
typedef struct os_val_container_s os_val_container_t;

/*
 * Generic value type that can hold any TR-181 value (simple or structured)
 *
 * Memory management:
 * - Scalar types (int, bool, etc.) require no cleanup
 * - String types may be allocated or static (see 'str_alloc' flag)
 * - Container types (DICT, LIST) are always allocated (see 'container' pointer)
 * - Always call os_val_free() when done with a value
 */
typedef struct
{
    os_tr181_param_type_t type; /* Type indicator */
    bool str_alloc;             /* true if 'str' is allocated and needs freeing */
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
    /* Pointer fields (not in union for safety) */
    char *str;                     /* For STRING, BASE64, or DATETIME representation */
    os_val_container_t *container; /* For DICT/LIST types (NULL for simple types) */
} os_tr181_val_t;

/* ========================================================================
 * Memory Management
 * ======================================================================== */

/*
 * Check if a type is valid for values (not path-only)
 *
 * Parameters:
 *   type - Type to check
 *
 * Returns: true if type can be used in os_tr181_val_t, false if path-only type
 */
bool os_val_is_value_type(os_tr181_param_type_t type);

/*
 * Returns true if type is a container type (DICT or LIST)
 */
bool os_val_is_container_type(os_tr181_param_type_t type);

/*
 * Returns true if type is a scalar value type (not NONE, not a container,
 * not a path-only structural type)
 */
bool os_val_is_scalar_type(os_tr181_param_type_t type);

/*
 * Initialize value to NONE type (zeroed)
 * Safe to call on uninitialized memory
 */
void os_val_init(os_tr181_val_t *v);

/*
 * Initialize uninitialized value with specific type
 * For DICT/LIST, creates empty container
 * For simple types, sets type but value is undefined
 *
 * Parameters:
 *   v    - Uninitialized value (contents will NOT be freed)
 *   type - Type to initialize to (must be valid value type)
 *
 * Note: Assumes v is uninitialized. If v already holds a value, use os_val_set_type() instead.
 */
void os_val_init_type(os_tr181_val_t *v, os_tr181_param_type_t type);

/*
 * Change type of existing value
 * For DICT/LIST, creates empty container
 * For simple types, sets type but value is undefined
 *
 * Parameters:
 *   v    - Initialized value (existing contents will be freed)
 *   type - Type to set (must be valid value type)
 *
 * Note: v must already be initialized. Existing contents will be freed.
 */
void os_val_set_type(os_tr181_val_t *v, os_tr181_param_type_t type);

/*
 * Free any allocated memory in value and reset to NONE type
 *
 * Parameters:
 *   v - Value to free, must be initialized or NULL
 *
 * Safe to call multiple times on the same value.
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

/*
 * Allocate and initialize new value on heap
 * Caller must free with os_val_delete()
 *
 * Returns: Pointer to initialized value, or NULL on allocation failure
 */
os_tr181_val_t *os_val_new(void);

/*
 * Free value and deallocate memory
 * Equivalent to: os_val_free(v); free(v);
 * Safe to call with NULL pointer
 */
void os_val_delete(os_tr181_val_t *v);

/*
 * Allocate and initialize new value on heap with specific type
 * Caller must free with os_val_delete()
 *
 * Parameters:
 *   type - Type to initialize to
 *
 * Returns: Pointer to initialized value, or NULL on allocation failure
 */
os_tr181_val_t *os_val_new_type(os_tr181_param_type_t type);

/* ========================================================================
 * Construction Macros (for inline initialization)
 * ======================================================================== */

/* C standard guarantees zero-initialization for unspecified fields,
 * so all pointers (str, container) will be NULL automatically.
 */

#define OS_VAL_INIT()      ((os_tr181_val_t){.type = OS_TR181_TYPE_NONE})
#define OS_VAL_INT(I)      ((os_tr181_val_t){.type = OS_TR181_TYPE_INT, .i = (I)})
#define OS_VAL_UINT(U)     ((os_tr181_val_t){.type = OS_TR181_TYPE_UINT, .u = (U)})
#define OS_VAL_INT64(I64)  ((os_tr181_val_t){.type = OS_TR181_TYPE_INT64, .i64 = (I64)})
#define OS_VAL_UINT64(U64) ((os_tr181_val_t){.type = OS_TR181_TYPE_UINT64, .u64 = (U64)})
#define OS_VAL_BOOL(B)     ((os_tr181_val_t){.type = OS_TR181_TYPE_BOOL, .b = (B)})
#define OS_VAL_DOUBLE(D)   ((os_tr181_val_t){.type = OS_TR181_TYPE_DOUBLE, .d = (D)})

/*
 * String macros
 * OS_VAL_STR_REF - Create value referencing external string
 * The string is NOT copied or owned by the value.
 * Caller must ensure string remains valid for the value's lifetime.
 */
#define OS_VAL_STR_REF(S) ((os_tr181_val_t){.type = OS_TR181_TYPE_STRING, .str = (char *)(S), .str_alloc = false})

/*
 * Dictionary/List construction macros
 */
#define OS_VAL_DICT() ((os_tr181_val_t){.type = OS_TR181_TYPE_DICT})
#define OS_VAL_LIST() ((os_tr181_val_t){.type = OS_TR181_TYPE_LIST})

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
 * Convert TR-181 parameter value to string representation (allocates memory)
 *
 * Supports simple parameter types only (string, int, uint, bool, double, etc.)
 * For complex types (LIST/DICT), use os_val_to_json_string()
 *
 * Caller must free returned string with free()
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_to_str(const os_tr181_val_t *v, char **str);

/*
 * Parse string into TR-181 parameter value based on type hint
 *
 * Supports simple parameter types only (string, int, uint, bool, double, etc.)
 * For complex types (LIST/DICT), use os_val_from_json_string()
 *
 * Parameters:
 *   v    - Value to set
 *   str  - String to parse
 *   type - Expected parameter type (simple types only)
 *
 * Returns:
 *   OS_TR181_SUCCESS on success
 *   OS_TR181_ERROR_INVALID if type is not a simple parameter type (NONE/LIST/DICT)
 *   Other error codes on parse failure
 */
os_tr181_error_t os_val_from_str(os_tr181_val_t *v, const char *str, os_tr181_param_type_t type);

/* ========================================================================
 * Dictionary Operations (OS_TR181_TYPE_DICT)
 *
 * Dictionary is a collection of key-value pairs (hash table / map).
 * Keys are strings, values are os_tr181_val_t (can be any type including nested dict/list).
 *
 * Memory Management:
 * - Dictionary owns its keys and values
 * - Setting/appending makes deep copies
 * - Getting returns const pointers (read-only, do not modify or free)
 * - os_val_free() recursively frees all nested structures
 * ======================================================================== */

/*
 * Initialize value as empty dictionary
 * Frees any existing value first
 */
void os_val_set_dict(os_tr181_val_t *val);

/*
 * Set dictionary entry (key-value pair)
 * Makes a deep copy of the value
 *
 * Parameters:
 *   dict  - Dictionary value (must be OS_TR181_TYPE_DICT)
 *   key   - Key string (must not be NULL or empty)
 *   value - Value to store (makes deep copy)
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_dict_set(os_tr181_val_t *dict, const char *key, const os_tr181_val_t *value);

/*
 * Get dictionary entry
 * Returns pointer to internal value (read-only, do not modify or free)
 *
 * Parameters:
 *   dict - Dictionary value (must be OS_TR181_TYPE_DICT)
 *   key  - Key string
 *
 * Returns: Pointer to value if found, NULL otherwise
 */
const os_tr181_val_t *os_val_dict_get(const os_tr181_val_t *dict, const char *key);

/*
 * Check if key exists in dictionary
 *
 * Parameters:
 *   dict - Dictionary value (must be OS_TR181_TYPE_DICT)
 *   key  - Key string
 *
 * Returns: true if key exists, false otherwise
 */
bool os_val_dict_has_key(const os_tr181_val_t *dict, const char *key);

/*
 * Remove key from dictionary
 *
 * Parameters:
 *   dict - Dictionary value (must be OS_TR181_TYPE_DICT)
 *   key  - Key string
 *
 * Returns: OS_TR181_SUCCESS if key was removed,
 *          OS_TR181_ERROR_NOT_FOUND if key doesn't exist,
 *          error code for other errors
 */
os_tr181_error_t os_val_dict_remove(os_tr181_val_t *dict, const char *key);

/*
 * Get number of entries in dictionary
 *
 * Parameters:
 *   dict - Dictionary value (must be OS_TR181_TYPE_DICT)
 *
 * Returns: Number of key-value pairs, 0 if empty or invalid
 */
size_t os_val_dict_size(const os_tr181_val_t *dict);

/*
 * Get all keys in dictionary
 *
 * Parameters:
 *   dict     - Dictionary value (must be OS_TR181_TYPE_DICT)
 *   keys     - Caller-allocated array to fill with key pointers (size >= capacity)
 *   capacity - Size of keys array (should be >= os_val_dict_size(dict))
 *   count    - Output: actual number of keys written to array (os_val_dict_size(dict))
 *
 * Returns: OS_TR181_SUCCESS on success
 *          OS_TR181_ERROR_INVALID if dict is NULL, wrong type, or capacity too small
 *
 * Notes:
 *   - Caller allocates the keys array (stack or heap)
 *   - Key strings are owned by dict and must NOT be freed
 *   - Keys array can be freed if heap-allocated
 *   - If capacity < dict size, returns OS_TR181_ERROR_INVALID
 */
os_tr181_error_t os_val_dict_get_keys(const os_tr181_val_t *dict, const char **keys, size_t capacity, size_t *count);

/* ========================================================================
 * Dictionary Convenience Functions
 *
 * These functions provide shortcuts for common operations with simple types.
 * They handle type conversion and provide default values on error.
 * ======================================================================== */

/* Get dictionary entry as string (returns default_val if not found or wrong type) */
const char *os_val_dict_get_string_or(const os_tr181_val_t *dict, const char *key, const char *default_val);

/* Get dictionary entry as int (returns default_val if not found or wrong type) */
int32_t os_val_dict_get_int_or(const os_tr181_val_t *dict, const char *key, int32_t default_val);

/* Get dictionary entry as uint (returns default_val if not found or wrong type) */
uint32_t os_val_dict_get_uint_or(const os_tr181_val_t *dict, const char *key, uint32_t default_val);

/* Get dictionary entry as bool (returns default_val if not found or wrong type) */
bool os_val_dict_get_bool_or(const os_tr181_val_t *dict, const char *key, bool default_val);

/* Get dictionary entry as double (returns default_val if not found or wrong type) */
double os_val_dict_get_double_or(const os_tr181_val_t *dict, const char *key, double default_val);

/* Set dictionary entry with string value (makes copy) */
os_tr181_error_t os_val_dict_set_string(os_tr181_val_t *dict, const char *key, const char *value);

/* Set dictionary entry with int value */
os_tr181_error_t os_val_dict_set_int(os_tr181_val_t *dict, const char *key, int32_t value);

/* Set dictionary entry with uint value */
os_tr181_error_t os_val_dict_set_uint(os_tr181_val_t *dict, const char *key, uint32_t value);

/* Set dictionary entry with bool value */
os_tr181_error_t os_val_dict_set_bool(os_tr181_val_t *dict, const char *key, bool value);

/* Set dictionary entry with double value */
os_tr181_error_t os_val_dict_set_double(os_tr181_val_t *dict, const char *key, double value);

/* ========================================================================
 * List Operations (OS_TR181_TYPE_LIST)
 *
 * List is an ordered sequence of values (array / vector).
 * Values are os_tr181_val_t (can be any type including nested dict/list).
 *
 * Memory Management:
 * - List owns its values
 * - Appending/setting makes deep copies
 * - Getting returns const pointers (read-only, do not modify or free)
 * - os_val_free() recursively frees all nested structures
 * ======================================================================== */

/*
 * Initialize value as empty list
 * Frees any existing value first
 */
void os_val_set_list(os_tr181_val_t *val);

/*
 * Append value to end of list
 * Makes a deep copy of the value
 *
 * Parameters:
 *   list  - List value (must be OS_TR181_TYPE_LIST)
 *   value - Value to append (makes deep copy)
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_list_append(os_tr181_val_t *list, const os_tr181_val_t *value);

/*
 * Get list entry at index
 * Returns pointer to internal value (read-only, do not modify or free)
 *
 * Parameters:
 *   list  - List value (must be OS_TR181_TYPE_LIST)
 *   index - Zero-based index
 *
 * Returns: Pointer to value if index valid, NULL otherwise
 */
const os_tr181_val_t *os_val_list_get(const os_tr181_val_t *list, size_t index);

/*
 * Set list entry at index (replaces existing value)
 * Makes a deep copy of the value
 *
 * Parameters:
 *   list  - List value (must be OS_TR181_TYPE_LIST)
 *   index - Zero-based index (must be < list size)
 *   value - Value to store (makes deep copy)
 *
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 */
os_tr181_error_t os_val_list_set(os_tr181_val_t *list, size_t index, const os_tr181_val_t *value);

/*
 * Get number of elements in list
 *
 * Parameters:
 *   list - List value (must be OS_TR181_TYPE_LIST)
 *
 * Returns: Number of elements, 0 if empty or invalid
 */
size_t os_val_list_size(const os_tr181_val_t *list);

/*
 * Remove element at index
 * Remaining elements after index are shifted down
 *
 * Parameters:
 *   list  - List value (must be OS_TR181_TYPE_LIST)
 *   index - Zero-based index (must be < list size)
 *
 * Returns: OS_TR181_SUCCESS if element was removed,
 *          OS_TR181_ERROR_INVALID if index out of bounds or invalid list,
 *          error code for other errors
 */
os_tr181_error_t os_val_list_remove(os_tr181_val_t *list, size_t index);

/* ========================================================================
 * List Convenience Functions
 *
 * These functions provide shortcuts for common operations with simple types.
 * ======================================================================== */

/* Append string value to list (makes copy) */
os_tr181_error_t os_val_list_append_string(os_tr181_val_t *list, const char *value);

/* Append int value to list */
os_tr181_error_t os_val_list_append_int(os_tr181_val_t *list, int32_t value);

/* Append uint value to list */
os_tr181_error_t os_val_list_append_uint(os_tr181_val_t *list, uint32_t value);

/* Append bool value to list */
os_tr181_error_t os_val_list_append_bool(os_tr181_val_t *list, bool value);

/* Append double value to list */
os_tr181_error_t os_val_list_append_double(os_tr181_val_t *list, double value);

/* ========================================================================
 * Iterator for DICT and LIST containers
 *
 * Provides sequential access to all elements of a DICT or LIST value
 * without needing to know keys or indices in advance.
 *
 * - Modifying a value's content (os_val_set_... etc.) during iteration
 *   is safe.
 * - Structural modification of the container (inserting or removing
 *   entries other than via os_val_iter_delete()) is undefined behaviour.
 * ======================================================================== */

/* Iterator state — allocated by caller, do not copy while iterating */
typedef struct os_val_iter_s
{
    os_tr181_val_t *container; /* required for os_val_iter_delete() */
    void *current;             /* current dict_entry* or list_entry* (NULL after delete) */
    void *next_node;           /* pre-fetched next — safe if current is deleted */
    void *prev_node;           /* predecessor node — NULL if current is/was the head */
} os_val_iter_t;

/*
 * Initialize iterator and return pointer to the first value.
 * Returns NULL if the container is empty or not a DICT/LIST.
 */
os_tr181_val_t *os_val_iter_init(os_val_iter_t *iter, os_tr181_val_t *container);

/*
 * Advance to the next element and return its value.
 * Returns NULL when iteration is complete.
 */
os_tr181_val_t *os_val_iter_next(os_val_iter_t *iter);

/*
 * Return the key of the current element (DICT only).
 * Returns NULL for LIST or when iteration is complete.
 */
const char *os_val_iter_key(const os_val_iter_t *iter);

/*
 * Remove the current element from the container.
 * os_val_iter_next() will move to the element after the deleted one.
 * Safe to call at most once per iteration step.
 */
void os_val_iter_delete(os_val_iter_t *iter);

/*
 * Iterate over all elements of a DICT or LIST.
 *   iter_var  - os_val_iter_t variable (declared by caller)
 *   val       - os_tr181_val_t * variable (declared by caller)
 *   container - pointer to the DICT or LIST os_tr181_val_t
 *
 * Example:
 *   os_val_iter_t it;
 *   os_tr181_val_t *val;
 *   os_val_foreach(it, val, &list) {
 *       os_val_set_int(val, 0);
 *   }
 */
#define os_val_foreach(iter_var, val, container) \
    for ((val) = os_val_iter_init(&(iter_var), (container)); (val) != NULL; (val) = os_val_iter_next(&(iter_var)))

/*
 * Iterate over all elements of a DICT, with key assigned each step.
 *   iter_var  - os_val_iter_t variable (declared by caller)
 *   key       - const char * variable (declared by caller)
 *   val       - os_tr181_val_t * variable (declared by caller)
 *   container - pointer to the DICT os_tr181_val_t
 *
 * Example:
 *   os_val_iter_t it;
 *   const char *key;
 *   os_tr181_val_t *val;
 *   os_val_foreach_dict(it, key, val, &dict) {
 *       printf("%s\n", key);
 *   }
 */
#define os_val_foreach_dict(iter_var, key, val, container)                                                        \
    for ((val) = os_val_iter_init(&(iter_var), (container)), (key) = os_val_iter_key(&(iter_var)); (val) != NULL; \
         (val) = os_val_iter_next(&(iter_var)), (key) = os_val_iter_key(&(iter_var)))

/* ========================================================================
 * JSON Conversion Support
 * ======================================================================== */

#include "os_tr181_val_json.h"

#ifdef __cplusplus
}
#endif

#endif /* OS_TR181_VAL_H */
