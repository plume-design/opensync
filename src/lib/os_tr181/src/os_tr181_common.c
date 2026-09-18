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
 * os_tr181 - Common utility functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "os_tr181.h"
#include "log.h"

const char *os_tr181_error_string(int error_code)
{
    switch (error_code)
    {
        case OS_TR181_SUCCESS:
            return "Success";
        case OS_TR181_ERROR:
            return "General error";
        case OS_TR181_ERROR_INIT:
            return "Initialization failed";
        case OS_TR181_ERROR_NOT_FOUND:
            return "Parameter not found";
        case OS_TR181_ERROR_INVALID:
            return "Invalid argument";
        case OS_TR181_ERROR_TIMEOUT:
            return "Operation timeout";
        case OS_TR181_ERROR_NOT_IMPLEMENTED:
            return "Not implemented";
        case OS_TR181_ERROR_OVERFLOW:
            return "Value overflow";
        default:
            return "Unknown error";
    }
}

os_tr181_param_type_t os_tr181_parse_type(const char *type_str)
{
    if (!type_str)
    {
        return OS_TR181_TYPE_STRING;
    }

    if (strcmp(type_str, "int") == 0)
    {
        return OS_TR181_TYPE_INT;
    }
    else if (strcmp(type_str, "uint") == 0)
    {
        return OS_TR181_TYPE_UINT;
    }
    else if (strcmp(type_str, "int64") == 0)
    {
        return OS_TR181_TYPE_INT64;
    }
    else if (strcmp(type_str, "uint64") == 0)
    {
        return OS_TR181_TYPE_UINT64;
    }
    else if (strcmp(type_str, "bool") == 0)
    {
        return OS_TR181_TYPE_BOOL;
    }
    else if (strcmp(type_str, "datetime") == 0)
    {
        return OS_TR181_TYPE_DATETIME;
    }
    else if (strcmp(type_str, "base64") == 0)
    {
        return OS_TR181_TYPE_BASE64;
    }

    return OS_TR181_TYPE_STRING;
}

const char *os_tr181_type_to_string(os_tr181_param_type_t type)
{
    switch (type)
    {
        case OS_TR181_TYPE_NONE:
            return "none";
        case OS_TR181_TYPE_STRING:
            return "string";
        case OS_TR181_TYPE_INT:
            return "int";
        case OS_TR181_TYPE_UINT:
            return "uint";
        case OS_TR181_TYPE_INT64:
            return "int64";
        case OS_TR181_TYPE_UINT64:
            return "uint64";
        case OS_TR181_TYPE_BOOL:
            return "bool";
        case OS_TR181_TYPE_DOUBLE:
            return "double";
        case OS_TR181_TYPE_DATETIME:
            return "datetime";
        case OS_TR181_TYPE_BASE64:
            return "base64";
        case OS_TR181_TYPE_OBJECT:
            return "object";
        case OS_TR181_TYPE_TABLE:
            return "table";
        case OS_TR181_TYPE_INSTANCE:
            return "instance";
        case OS_TR181_TYPE_METHOD:
            return "method";
        case OS_TR181_TYPE_EVENT:
            return "event";
        case OS_TR181_TYPE_PROPERTY:
            return "property";
        case OS_TR181_TYPE_DICT:
            return "dict";
        case OS_TR181_TYPE_LIST:
            return "list";
        default:
            return "unknown";
    }
}

os_tr181_error_t os_tr181_get_int(os_tr181_handle_t *handle, const char *param_name, int *value)
{
    os_tr181_val_t val = OS_VAL_INIT();
    os_tr181_error_t ret;

    if (!value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    ret = os_tr181_get_val(handle, param_name, &val);
    if (ret != OS_TR181_SUCCESS)
    {
        return ret;
    }

    /* Use type conversion to get int - handles INT, UINT, STRING conversion */
    ret = os_val_to_int(&val, value);
    os_val_free(&val);

    return ret;
}

os_tr181_error_t os_tr181_get_str(os_tr181_handle_t *handle, const char *param_name, char **value)
{
    os_tr181_val_t val = OS_VAL_INIT();
    os_tr181_error_t ret;

    ret = os_tr181_get_val(handle, param_name, &val);
    if (ret != OS_TR181_SUCCESS)
    {
        return ret;
    }

    /* Convert to string - handles all types */
    ret = os_val_to_str(&val, value);
    os_val_free(&val);

    return ret;
}

os_tr181_error_t os_tr181_set_int(os_tr181_handle_t *handle, const char *param_name, int value)
{
    os_tr181_val_t val = OS_VAL_INT(value);
    return os_tr181_set_val(handle, param_name, &val);
}

os_tr181_error_t os_tr181_set_str(os_tr181_handle_t *handle, const char *param_name, const char *value)
{
    os_tr181_val_t val = OS_VAL_STR_REF(value);
    return os_tr181_set_val(handle, param_name, &val);
}

os_tr181_error_t os_tr181_get_int64(os_tr181_handle_t *handle, const char *param_name, int64_t *value)
{
    os_tr181_val_t val = OS_VAL_INIT();
    os_tr181_error_t ret;

    if (!value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    ret = os_tr181_get_val(handle, param_name, &val);
    if (ret != OS_TR181_SUCCESS)
    {
        return ret;
    }

    /* Use type conversion to get int64 - handles INT64, INT, UINT, STRING conversion */
    ret = os_val_to_int64(&val, value);
    os_val_free(&val);

    return ret;
}

os_tr181_error_t os_tr181_set_int64(os_tr181_handle_t *handle, const char *param_name, int64_t value)
{
    os_tr181_val_t val = OS_VAL_INT64(value);
    return os_tr181_set_val(handle, param_name, &val);
}

/* qsort comparison function for integers */
static int compare_ints(const void *a, const void *b)
{
    int aa = *(const int *)a;
    int bb = *(const int *)b;
    if (aa < bb) return -1;
    if (aa > bb) return 1;
    return 0;
}

void os_tr181_sort_instances(int *instance_numbers, int count)
{
    if (!instance_numbers || count <= 0)
    {
        return;
    }
    qsort(instance_numbers, count, sizeof(int), compare_ints);
}

int os_tr181_parse_instance(const char *path)
{
    const char *p;
    int last_instance = -1;

    if (!path)
    {
        return -1;
    }

    /* Walk through the path, looking for ".<number>." patterns */
    p = path;
    while ((p = strchr(p, '.')) != NULL)
    {
        p++; /* Move past the dot */

        /* Check if next character is a digit */
        if (isdigit(*p))
        {
            char *endptr;
            long num = strtol(p, &endptr, 10);

            /* Valid instance number if followed by a dot and num > 0 */
            if (endptr > p && *endptr == '.' && num > 0)
            {
                last_instance = (int)num;
                p = endptr; /* Continue from after this number */
            }
        }
    }

    return last_instance;
}

int os_tr181_parse_instances(const char *path, int *indices, int max_count)
{
    const char *p;
    int count = 0;

    if (!path || !indices || max_count <= 0)
    {
        return -1;
    }

    /* Walk through the path, collecting all ".<number>." patterns */
    p = path;
    while ((p = strchr(p, '.')) != NULL && count < max_count)
    {
        p++; /* Move past the dot */

        /* Check if next character is a digit */
        if (isdigit(*p))
        {
            char *endptr;
            long num = strtol(p, &endptr, 10);

            /* Valid instance number if followed by a dot and num > 0 */
            if (endptr > p && *endptr == '.' && num > 0)
            {
                indices[count++] = (int)num;
                p = endptr; /* Continue from after this number */
            }
        }
    }

    return count;
}

os_tr181_error_t os_tr181_init(os_tr181_handle_t **handle)
{
    return os_tr181_init_ex(handle, NULL);
}

os_tr181_error_t os_tr181_add_instance(os_tr181_handle_t *handle, const char *object_path, int *instance_number)
{
    return os_tr181_add_instance_ex(handle, object_path, 0, NULL, NULL, instance_number);
}
