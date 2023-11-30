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

#ifndef OTBR_CLI_UTIL_H_INCLUDED
#define OTBR_CLI_UTIL_H_INCLUDED

#include <stddef.h>

/** Macro used to inform the compiler that the function parameters (count starting with 1) shall not be NULL */
#define NONNULL(...) __attribute__((__nonnull__(__VA_ARGS__)))

/** Get the length of a string literal */
#define STRLEN(s) (sizeof(s) - 1)

/**
 * Macro used to check if an array is empty
 *
 * This also checks for data integrity, that is, if array is `NULL` then count is 0
 * and vice versa, which is always true for initialized (`{NULL, 0}`), freed, and
 * properly used arrays (count is managed for every addition or removal of an item).
 *
 * @param array_var  Array to check (may be `NULL`).
 * @param count_var  Number of items in the array.
 *
 * @return true if the array is empty.
 */
#define ARRAY_IS_EMPTY(array_var, count_var) \
    ({ \
        ASSERT(((array_var) == NULL) == ((count_var) == 0), \
               "Uninitialized array (%p but count=%d)", \
               (array_var), \
               (count_var)); \
        (((array_var) == NULL) && ((count_var) == 0)); \
    })

/**
 * Macro used to resize an array and assign an item to its last slot
 *
 * @param array_var  Array to which the item will be appended. Will be reallocated.
 * @param count_var  Variable with number of items in the array. Will be incremented.
 * @param item       Item to assign to the last slot of the array.
 *
 * @return Pointer to the newly appended item in the array (last array slot),
 *         containing the `item` value.
 *
 * @see @ref ARRAY_APPEND_COPY()
 * @see @ref ARRAY_FREE_ITEMS()
 */
#define ARRAY_APPEND(array_var, count_var, item) \
    ({ /* Cannot take the address of rvalues - use test pointers for type check instead */ \
        const __typeof__(item) *const _p_new_item = NULL; \
        const __typeof__((array_var)[0]) *const _p_arr_item = _p_new_item; \
        (void)_p_arr_item; \
        (array_var) = REALLOC((array_var), ((count_var) + 1) * sizeof((array_var)[0])); \
        (array_var)[(count_var)] = (item); \
        &(array_var)[(count_var)++]; \
    })

/**
 * Macro used to resize an array and copy an item to its last slot
 *
 * @param array_var  Array to which the item will be appended. Will be reallocated.
 * @param count_var  Variable with number of items in the array. Will be incremented.
 * @param item       Item to copy to the last slot of the array.
 *
 * @return Pointer to the newly appended item in the array (last array slot),
 *         containing a copy of the `item`.
 *
 * @see @ref ARRAY_APPEND()
 * @see @ref ARRAY_FREE()
 */
#define ARRAY_APPEND_COPY(array_var, count_var, item) \
    ({ /* Ensure item is lvalue of the same type as the array */ \
        const __typeof__(array_var[0]) *const _p_item = &(item); \
        (array_var) = REALLOC((array_var), ((count_var) + 1) * sizeof((array_var)[0])); \
        memcpy(&(array_var)[count_var], _p_item, sizeof((array_var)[0])); \
        &(array_var)[(count_var)++]; \
    })

/**
 * Macro used to free the array, without freeing the items in the array
 *
 * @param array_var  Array to free (may be `NULL`). Set to `NULL` when done.
 * @param count_var  Number of items in the array. Set to 0 when done.
 */
#define ARRAY_FREE(array_var, count_var) \
    do \
    { \
        if (!ARRAY_IS_EMPTY(array_var, count_var)) \
        { \
            FREE(array_var); \
            (array_var) = NULL; \
            (count_var) = 0; \
        } \
    } while (0)

/**
 * Macro used to free all items in an array and the array itself
 *
 * @param array_var  Array with items to free (may be `NULL`). All items in this array
 *                   will be freed and the variable itself set to `NULL` when done.
 * @param count_var  Number of items in the array. Set to `0` when done.
 */
#define ARRAY_FREE_ITEMS(array_var, count_var) \
    do \
    { \
        const void *const _ensure_items_are_pointers = (__typeof__(array_var[0]))NULL; \
        (void)_ensure_items_are_pointers; \
        if (!ARRAY_IS_EMPTY(array_var, count_var)) \
        { \
            while ((count_var) > 0) \
            { \
                FREE((array_var)[--(count_var)]); \
            } \
            FREE(array_var); \
            (array_var) = NULL; \
        } \
    } while (0)

/**
 * Get the number of whitespace characters at the beginning of a string
 *
 * @param[in] str  String to check for leading whitespace. If `NULL`, the function returns 0.
 *
 * @return number of leading whitespaces in the string.
 */
unsigned int str_leading_whitespace(const char *str);

/**
 * Locate end of a substring in a string
 *
 * @param[in] haystack  String to search in. If `NULL`, the function returns `NULL`.
 * @param[in] needle    Substring to search for.
 *
 * @return pointer to the first character after the located substring, or `NULL` if the substring is not found.
 */
const char *strstra(const char *haystack, const char *needle) NONNULL(2);

/**
 * Try to convert a string to an integer
 *
 * @param[in]  str           String representing the value to convert.
 *                           If `NULL`, the function returns `NULL`.
 * @param[out] value         Pointer to the variable to store the value to (fixed‐width basic integer types).
 *                           If `NULL`, conversion is still performed but the result is not stored.
 * @param[in]  sizeof_value  `sizeof(*value)`, 1, 2, 4, and 8 are supported. Required even if `value` is `NULL`.
 * @param[in]  base          Base of the value representation in the string. Specify positive value if `value`
 *                           is pointer to unsigned integer (e.g. 10 or 16 for uint32_t), and negative value
 *                           if `value` is pointer to signed integer (e.g. -10 or -16 for int32_t).
 *
 * @return pointer to the first character after the parsed value in the string ('\0' if the whole string was parsed),
 *         or `NULL` if the conversion failed.
 *
 * @see @ref strtoll
 * @see @ref strtoull
 */
const char *strtonum(const char *str, void *value, size_t sizeof_value, int base);

#endif /* OTBR_CLI_UTIL_H_INCLUDED */
