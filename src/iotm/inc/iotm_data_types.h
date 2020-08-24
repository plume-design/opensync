/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef IOTM_DATA_TYPES_H_INCLUDED
#define IOTM_DATA_TYPES_H_INCLUDED

/**
 * @file iotm_data_types.h
 *
 * @brief conversion utilities from string -> types
 *
 * Everything is stored as a string in OVSDB. This allows for plugins that know
 * a param is a specific type (i.e. param 'uuid': '0x0a' is a uint8), then the
 * plugin can access the parameter as that specific type.
 */

/**
 * @brief supported data types, type should be this enum
 */
enum
{
    UINT8, /**< 0xde | "0xDE" */
    UINT16, /**< 0xdead | "0xDEAD" */
    INT, /**< 5 | "5" */
    LONG, /**< 2147483647 | "2147483647" */
    STRING, /**< 'hello' | 'hello' */
    HEX_ARRAY, /**< {.data = {0xab, 0xfe}, .data_length = 2} | "abfe" */
};

#include <stdint.h>
#include <stddef.h>
/**
 * @brief data used to perform a write
 */
typedef struct 
{
  uint8_t *data;
  size_t data_length;
} iot_barray_t;

/**
 * @brief convert a string into the specified type
 *
 * @param      input  input, such as '0x0a'
 * @param      type   such as UINT8
 * @param[out] out    output voidptr, such as uint8_t *var
 *
 * @return 0 string converted, output buffer loaded
 * @return -1  failed conversion
 */
int convert_to_type(char *input, int type, void *out);

/**
 * @brief convert a type into a string
 *
 * @param      input  void * that can be cast to type
 * @param      int    type of input to be converted
 * @param[out] out    output string after conversion
 *
 * @return 0     type loaded into string
 * @return -1    failed to convert type into string
 */
int convert_from_type(void *input, int type, char *out);

/**
 * @brief allocate the string container of appropriate size for type
 *
 * @param type  what type string must be able to fill
 */
char *alloc_type_string(int type);

/**
 * @brief allocate the container for a type, to hold data after conversion
 *
 * @param      type   type to allocate
 * @param[out] out    void * that holds allocated memory
 */
int alloc_data(int type, void **out);

#endif // IOTM_DATA_TYPES_H_INCLUDED */
