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
 * JSON conversions for os_tr181_val_t using jansson library
 * Provides both json_t and JSON string conversions
 */

#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "os_tr181.h"
#include "os_tr181_val_json.h"
#include "log.h"

/* ========================================================================
 * Forward Declarations
 * ======================================================================== */

static os_tr181_error_t val_to_json_recursive(const os_tr181_val_t *val, json_t **json);
static os_tr181_error_t json_to_val_recursive(os_tr181_val_t *val, const json_t *json);

/* ========================================================================
 * os_tr181_val_t to json_t Conversion
 * ======================================================================== */

/*
 * Convert os_tr181_val_t to json_t
 * Caller must call json_decref() on the returned json_t when done
 */
os_tr181_error_t os_val_to_json(const os_tr181_val_t *val, json_t **json)
{
    if (!val || !json)
    {
        return OS_TR181_ERROR_INVALID;
    }

    return val_to_json_recursive(val, json);
}

static os_tr181_error_t val_to_json_recursive(const os_tr181_val_t *val, json_t **json)
{
    if (!val || !json)
    {
        return OS_TR181_ERROR_INVALID;
    }

    switch (val->type)
    {
        case OS_TR181_TYPE_NONE:
            *json = json_null();
            break;

        case OS_TR181_TYPE_INT:
            *json = json_integer(val->i);
            break;

        case OS_TR181_TYPE_UINT:
            /* JSON doesn't distinguish signed/unsigned, use integer */
            *json = json_integer((json_int_t)val->u);
            break;

        case OS_TR181_TYPE_INT64:
            *json = json_integer(val->i64);
            break;

        case OS_TR181_TYPE_UINT64:
            /* JSON integer is signed, but we can store the value */
            *json = json_integer((json_int_t)val->u64);
            break;

        case OS_TR181_TYPE_BOOL:
            *json = val->b ? json_true() : json_false();
            break;

        case OS_TR181_TYPE_DOUBLE:
            *json = json_real(val->d);
            break;

        case OS_TR181_TYPE_STRING:
        case OS_TR181_TYPE_BASE64:
            *json = json_string(val->str ? val->str : "");
            break;

        case OS_TR181_TYPE_DATETIME: {
            /* Convert datetime to string */
            char *str = NULL;
            os_tr181_error_t err = os_val_to_str(val, &str);
            if (err != OS_TR181_SUCCESS)
            {
                return err;
            }
            *json = json_string(str);
            free(str);
            break;
        }

        case OS_TR181_TYPE_DICT: {
            json_t *obj = json_object();
            if (!obj)
            {
                return OS_TR181_ERROR;
            }

            os_val_iter_t it;
            const char *key;
            os_tr181_val_t *entry_val;
            os_val_foreach_dict(it, key, entry_val, (os_tr181_val_t *)val)
            {
                json_t *entry_json = NULL;
                os_tr181_error_t err;

                /* Recursively convert value */
                err = val_to_json_recursive(entry_val, &entry_json);
                if (err != OS_TR181_SUCCESS || !entry_json)
                {
                    json_decref(obj);
                    return err != OS_TR181_SUCCESS ? err : OS_TR181_ERROR;
                }

                /* Add to object (json_object_set steals reference) */
                if (json_object_set_new(obj, key, entry_json) != 0)
                {
                    json_decref(obj);
                    return OS_TR181_ERROR;
                }
            }

            *json = obj;
            break;
        }

        case OS_TR181_TYPE_LIST: {
            json_t *arr = json_array();
            if (!arr)
            {
                return OS_TR181_ERROR;
            }

            os_val_iter_t it;
            os_tr181_val_t *entry_val;
            os_val_foreach(it, entry_val, (os_tr181_val_t *)val)
            {
                json_t *entry_json = NULL;

                /* Recursively convert value */
                os_tr181_error_t err = val_to_json_recursive(entry_val, &entry_json);
                if (err != OS_TR181_SUCCESS || !entry_json)
                {
                    json_decref(arr);
                    return err != OS_TR181_SUCCESS ? err : OS_TR181_ERROR;
                }

                /* Add to array (json_array_append_new steals reference) */
                if (json_array_append_new(arr, entry_json) != 0)
                {
                    json_decref(arr);
                    return OS_TR181_ERROR;
                }
            }

            *json = arr;
            break;
        }

        default:
            return OS_TR181_ERROR_INVALID;
    }

    if (!*json)
    {
        return OS_TR181_ERROR;
    }

    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * json_t to os_tr181_val_t Conversion
 * ======================================================================== */

/*
 * Convert json_t to os_tr181_val_t
 * Does not take ownership of json (caller must still json_decref if needed)
 */
os_tr181_error_t os_val_from_json(os_tr181_val_t *val, const json_t *json)
{
    if (!val || !json)
    {
        return OS_TR181_ERROR_INVALID;
    }

    return json_to_val_recursive(val, json);
}

static os_tr181_error_t json_to_val_recursive(os_tr181_val_t *val, const json_t *json)
{
    if (!val || !json)
    {
        return OS_TR181_ERROR_INVALID;
    }

    switch (json_typeof(json))
    {
        case JSON_NULL:
            os_val_init(val);
            break;

        case JSON_TRUE:
            *val = OS_VAL_BOOL(true);
            break;

        case JSON_FALSE:
            *val = OS_VAL_BOOL(false);
            break;

        case JSON_INTEGER:
            /* Store as int64 to preserve range */
            *val = OS_VAL_INT64(json_integer_value(json));
            break;

        case JSON_REAL:
            *val = OS_VAL_DOUBLE(json_real_value(json));
            break;

        case JSON_STRING: {
            const char *str = json_string_value(json);
            return os_val_set_str_dup(val, str);
        }

        case JSON_OBJECT: {
            os_val_set_dict(val);

            const char *key;
            json_t *value;
            json_object_foreach((json_t *)json, key, value)
            {
                os_tr181_val_t entry_val = OS_VAL_INIT();
                os_tr181_error_t err;

                /* Recursively convert value */
                err = json_to_val_recursive(&entry_val, value);
                if (err != OS_TR181_SUCCESS)
                {
                    os_val_free(val);
                    return err;
                }

                /* Add to dictionary */
                err = os_val_dict_set(val, key, &entry_val);
                os_val_free(&entry_val);

                if (err != OS_TR181_SUCCESS)
                {
                    os_val_free(val);
                    return err;
                }
            }
            break;
        }

        case JSON_ARRAY: {
            os_val_set_list(val);

            size_t index;
            json_t *value;
            json_array_foreach((json_t *)json, index, value)
            {
                os_tr181_val_t entry_val = OS_VAL_INIT();
                os_tr181_error_t err;

                /* Recursively convert value */
                err = json_to_val_recursive(&entry_val, value);
                if (err != OS_TR181_SUCCESS)
                {
                    os_val_free(val);
                    return err;
                }

                /* Append to list */
                err = os_val_list_append(val, &entry_val);
                os_val_free(&entry_val);

                if (err != OS_TR181_SUCCESS)
                {
                    os_val_free(val);
                    return err;
                }
            }
            break;
        }

        default:
            return OS_TR181_ERROR_INVALID;
    }

    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * String Conversions (using json_t internally)
 * ======================================================================== */

/*
 * Convert os_tr181_val_t to JSON string
 * Caller must free() the returned string
 */
os_tr181_error_t os_val_to_json_string(const os_tr181_val_t *val, char **json_str)
{
    json_t *json = NULL;
    os_tr181_error_t err;

    if (!val || !json_str)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Convert to json_t */
    err = os_val_to_json(val, &json);
    if (err != OS_TR181_SUCCESS || !json)
    {
        return err != OS_TR181_SUCCESS ? err : OS_TR181_ERROR;
    }

    /* Convert json_t to string (compact format, allow any type) */
    *json_str = json_dumps(json, JSON_COMPACT | JSON_ENCODE_ANY);
    json_decref(json);

    if (!*json_str)
    {
        return OS_TR181_ERROR;
    }

    return OS_TR181_SUCCESS;
}

/*
 * Parse os_tr181_val_t from JSON string
 */
os_tr181_error_t os_val_from_json_string(os_tr181_val_t *val, const char *json_str)
{
    json_t *json = NULL;
    json_error_t error;
    os_tr181_error_t err;

    if (!val || !json_str)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Parse string to json_t */
    json = json_loads(json_str, JSON_DECODE_ANY, &error);
    if (!json)
    {
        LOGE("JSON parse error: %s (line %d, column %d)", error.text, error.line, error.column);
        return OS_TR181_ERROR_INVALID;
    }

    /* Convert json_t to os_tr181_val_t */
    err = os_val_from_json(val, json);
    json_decref(json);

    return err;
}
