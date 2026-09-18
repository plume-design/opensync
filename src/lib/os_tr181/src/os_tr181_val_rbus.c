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
 * Platform-specific value conversions for RBUS
 * Converts between os_tr181_val_t and rbusValue_t/rbusObject_t, including structured types
 */

#include <stdlib.h>
#include <string.h>
#include <rbus/rbus.h>
#include "os_tr181.h"
#include "os_tr181_val_rbus.h"
#include "log.h"

/* ========================================================================
 * Forward Declarations
 * ======================================================================== */

static os_tr181_error_t rbus_object_to_dict(rbusValue_t value, os_tr181_val_t *val);
static os_tr181_error_t dict_to_rbus_object(const os_tr181_val_t *val, rbusValue_t value);

/* Note: RBUS doesn't have a native array type in rbusValue_t, arrays are
 * typically represented as rbusObject with numeric keys "0", "1", etc.
 * We'll use that convention for LIST conversion. */

/* ========================================================================
 * os_tr181_val_t to rbusValue_t Conversion
 * ======================================================================== */

/*
 * Convert os_tr181_val_t to rbusValue_t
 * Caller must release the rbusValue with rbusValue_Release when done
 */
os_tr181_error_t os_tr181_val_to_rbus(const os_tr181_val_t *val, rbusValue_t *out_value)
{
    rbusValue_t value;

    if (!val || !out_value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    rbusValue_Init(&value);

    switch (val->type)
    {
        case OS_TR181_TYPE_NONE:
            /* rbusValue_Init already creates RBUS_NONE type */
            break;

        case OS_TR181_TYPE_BOOL:
            rbusValue_SetBoolean(value, val->b);
            break;

        case OS_TR181_TYPE_INT:
            rbusValue_SetInt32(value, val->i);
            break;

        case OS_TR181_TYPE_UINT:
            rbusValue_SetUInt32(value, val->u);
            break;

        case OS_TR181_TYPE_INT64:
            rbusValue_SetInt64(value, val->i64);
            break;

        case OS_TR181_TYPE_UINT64:
            rbusValue_SetUInt64(value, val->u64);
            break;

        case OS_TR181_TYPE_DOUBLE:
            rbusValue_SetDouble(value, val->d);
            break;

        case OS_TR181_TYPE_STRING:
        case OS_TR181_TYPE_BASE64:
            rbusValue_SetString(value, val->str);
            break;

        case OS_TR181_TYPE_DATETIME: {
            char *str = NULL;
            os_tr181_error_t err = os_val_to_str(val, &str);
            if (err != OS_TR181_SUCCESS)
            {
                rbusValue_Release(value);
                return err;
            }
            rbusValue_SetString(value, str);
            free(str);
            break;
        }

        case OS_TR181_TYPE_DICT: {
            os_tr181_error_t err = dict_to_rbus_object(val, value);
            if (err != OS_TR181_SUCCESS)
            {
                rbusValue_Release(value);
                return err;
            }
            break;
        }

        case OS_TR181_TYPE_LIST: {
            /* Convert list to rbusObject with numeric keys */
            rbusObject_t obj;
            os_tr181_error_t err = OS_TR181_SUCCESS;
            size_t i = 0;

            rbusObject_Init(&obj, NULL);

            os_val_iter_t it;
            os_tr181_val_t *item;
            os_val_foreach(it, item, (os_tr181_val_t *)val)
            {
                rbusValue_t item_value;
                char key[32];

                /* Create key as string index */
                snprintf(key, sizeof(key), "%zu", i);
                i++;

                /* Recursively convert item */
                err = os_tr181_val_to_rbus(item, &item_value);
                if (err != OS_TR181_SUCCESS)
                {
                    rbusObject_Release(obj);
                    rbusValue_Release(value);
                    return err;
                }

                rbusObject_SetValue(obj, key, item_value);
                rbusValue_Release(item_value);
            }

            rbusValue_SetObject(value, obj);
            rbusObject_Release(obj);
            break;
        }

        default:
            rbusValue_Release(value);
            return OS_TR181_ERROR_INVALID;
    }

    *out_value = value;
    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * rbusValue_t to os_tr181_val_t Conversion
 * ======================================================================== */

/*
 * Convert rbusValue_t to os_tr181_val_t
 */
os_tr181_error_t os_tr181_val_from_rbus(os_tr181_val_t *val, rbusValue_t value)
{
    rbusValueType_t value_type;

    if (!value || !val)
    {
        return OS_TR181_ERROR_INVALID;
    }

    value_type = rbusValue_GetType(value);

    switch (value_type)
    {
        case RBUS_NONE:
            val->type = OS_TR181_TYPE_NONE;
            break;

        case RBUS_BOOLEAN:
            *val = OS_VAL_BOOL(rbusValue_GetBoolean(value));
            break;

        case RBUS_INT32:
            *val = OS_VAL_INT(rbusValue_GetInt32(value));
            break;

        case RBUS_UINT32:
            *val = OS_VAL_UINT(rbusValue_GetUInt32(value));
            break;

        case RBUS_INT64:
            *val = OS_VAL_INT64(rbusValue_GetInt64(value));
            break;

        case RBUS_UINT64:
            *val = OS_VAL_UINT64(rbusValue_GetUInt64(value));
            break;

        case RBUS_SINGLE:
        case RBUS_DOUBLE:
            *val = OS_VAL_DOUBLE(rbusValue_GetDouble(value));
            break;

        case RBUS_STRING: {
            const char *str = rbusValue_GetString(value, NULL);
            return os_val_set_str_dup(val, str);
        }

        case RBUS_DATETIME: {
            const char *str = rbusValue_GetString(value, NULL);
            os_tr181_val_t temp = OS_VAL_INIT();
            os_tr181_error_t err = os_val_from_str(&temp, str, OS_TR181_TYPE_DATETIME);
            if (err == OS_TR181_SUCCESS)
            {
                *val = temp;
            }
            return err;
        }

        case RBUS_BYTES: {
            const char *str = rbusValue_GetString(value, NULL);
            return os_val_set_str_dup(val, str);
        }

        case RBUS_OBJECT:
            return rbus_object_to_dict(value, val);

        default: {
            /* Fallback: convert to string */
            char *str = rbusValue_ToString(value, NULL, 0);
            os_tr181_error_t err = os_val_set_str_dup(val, str ? str : "");
            free(str);
            return err;
        }
    }

    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * Dictionary Conversion (os_tr181_val DICT <-> rbusObject_t)
 * ======================================================================== */

/*
 * Convert os_tr181_val_t DICT to rbusObject_t
 */
static os_tr181_error_t dict_to_rbus_object(const os_tr181_val_t *val, rbusValue_t value)
{
    rbusObject_t obj;
    os_tr181_error_t err;

    if (!val || val->type != OS_TR181_TYPE_DICT || !value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    rbusObject_Init(&obj, NULL);

    /* Convert each dictionary entry */
    os_val_iter_t it;
    const char *key;
    os_tr181_val_t *entry_val;
    os_val_foreach_dict(it, key, entry_val, (os_tr181_val_t *)val)
    {
        rbusValue_t entry_rbus;

        /* Recursively convert value */
        err = os_tr181_val_to_rbus(entry_val, &entry_rbus);
        if (err != OS_TR181_SUCCESS)
        {
            rbusObject_Release(obj);
            return err;
        }

        rbusObject_SetValue(obj, key, entry_rbus);
        rbusValue_Release(entry_rbus);
    }

    rbusValue_SetObject(value, obj);
    rbusObject_Release(obj);

    return OS_TR181_SUCCESS;
}

/*
 * Convert rbusObject_t to os_tr181_val_t
 * Detects if object is a list (all keys are numeric "0", "1", etc.) or a dict
 */
static os_tr181_error_t rbus_object_to_dict(rbusValue_t value, os_tr181_val_t *val)
{
    rbusObject_t obj;
    rbusProperty_t prop;
    bool is_list = true;
    size_t expected_index = 0;

    if (!value || !val)
    {
        return OS_TR181_ERROR_INVALID;
    }

    obj = rbusValue_GetObject(value);
    if (!obj)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* First pass: determine if this is a list (all numeric sequential keys) */
    prop = rbusObject_GetProperties(obj);

    /* Empty object defaults to DICT (can't determine intent) */
    if (!prop)
    {
        os_val_set_dict(val);
        return OS_TR181_SUCCESS;
    }

    while (prop && is_list)
    {
        const char *name = rbusProperty_GetName(prop);
        char expected[32];
        snprintf(expected, sizeof(expected), "%zu", expected_index);

        if (strcmp(name, expected) != 0)
        {
            is_list = false;
        }

        expected_index++;
        prop = rbusProperty_GetNext(prop);
    }

    /* Convert based on detected type */
    if (is_list && expected_index > 0)
    {
        /* Convert as LIST */
        os_val_set_list(val);

        prop = rbusObject_GetProperties(obj);
        while (prop)
        {
            rbusValue_t prop_value = rbusProperty_GetValue(prop);
            os_tr181_val_t entry_val = OS_VAL_INIT();
            os_tr181_error_t err;

            /* Recursively convert value */
            err = os_tr181_val_from_rbus(&entry_val, prop_value);
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

            prop = rbusProperty_GetNext(prop);
        }
    }
    else
    {
        /* Convert as DICT */
        os_val_set_dict(val);

        prop = rbusObject_GetProperties(obj);
        while (prop)
        {
            const char *name = rbusProperty_GetName(prop);
            rbusValue_t prop_value = rbusProperty_GetValue(prop);
            os_tr181_val_t entry_val = OS_VAL_INIT();
            os_tr181_error_t err;

            /* Recursively convert value */
            err = os_tr181_val_from_rbus(&entry_val, prop_value);
            if (err != OS_TR181_SUCCESS)
            {
                os_val_free(val);
                return err;
            }

            /* Add to dictionary */
            err = os_val_dict_set(val, name, &entry_val);
            os_val_free(&entry_val);

            if (err != OS_TR181_SUCCESS)
            {
                os_val_free(val);
                return err;
            }

            prop = rbusProperty_GetNext(prop);
        }
    }

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_val_from_rbus_object(os_tr181_val_t *val, rbusObject_t obj)
{
    rbusProperty_t prop;
    os_tr181_error_t err;

    if (!val || !obj)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Initialize as dict */
    *val = OS_VAL_DICT();

    /* Iterate through object properties */
    prop = rbusObject_GetProperties(obj);
    while (prop)
    {
        const char *prop_name = rbusProperty_GetName(prop);
        rbusValue_t prop_value = rbusProperty_GetValue(prop);

        if (prop_name && prop_value)
        {
            os_tr181_val_t temp_val = OS_VAL_INIT();
            err = os_tr181_val_from_rbus(&temp_val, prop_value);
            if (err == OS_TR181_SUCCESS)
            {
                os_val_dict_set(val, prop_name, &temp_val);
            }
            os_val_free(&temp_val);

            if (err != OS_TR181_SUCCESS)
            {
                os_val_free(val);
                return err;
            }
        }

        prop = rbusProperty_GetNext(prop);
    }

    return OS_TR181_SUCCESS;
}
