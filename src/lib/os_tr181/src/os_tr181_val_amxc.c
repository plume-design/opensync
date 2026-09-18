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
 * Platform-specific value conversions for Ambiorix (amxc)
 * Converts between os_tr181_val_t and amxc_var_t, including structured types
 */

#include <stdlib.h>
#include <string.h>
#include <amxc/amxc.h>
#include "os_tr181.h"
#include "os_tr181_val_amxc.h"
#include "log.h"

/* ========================================================================
 * Forward Declarations
 * ======================================================================== */

static os_tr181_error_t amx_htable_to_dict(const amxc_var_t *var, os_tr181_val_t *val);
static os_tr181_error_t amx_list_to_list(const amxc_var_t *var, os_tr181_val_t *val);
static os_tr181_error_t dict_to_amx_htable(const os_tr181_val_t *val, amxc_var_t *var);
static os_tr181_error_t list_to_amx_list(const os_tr181_val_t *val, amxc_var_t *var);

/* ========================================================================
 * os_tr181_val_t to amxc_var_t Conversion
 * ======================================================================== */

/*
 * Convert os_tr181_val_t to Ambiorix amxc_var_t
 * Note: var should be initialized before calling (amxc_var_init)
 * Supports all types including DICT and LIST
 */
os_tr181_error_t os_tr181_val_to_amx(const os_tr181_val_t *val, amxc_var_t *var)
{
    if (!val || !var)
    {
        return OS_TR181_ERROR_INVALID;
    }

    switch (val->type)
    {
        case OS_TR181_TYPE_NONE:
            amxc_var_set_type(var, AMXC_VAR_ID_NULL);
            break;

        case OS_TR181_TYPE_INT:
            amxc_var_set(int32_t, var, val->i);
            break;

        case OS_TR181_TYPE_UINT:
            amxc_var_set(uint32_t, var, val->u);
            break;

        case OS_TR181_TYPE_INT64:
            amxc_var_set(int64_t, var, val->i64);
            break;

        case OS_TR181_TYPE_UINT64:
            amxc_var_set(uint64_t, var, val->u64);
            break;

        case OS_TR181_TYPE_BOOL:
            amxc_var_set(bool, var, val->b);
            break;

        case OS_TR181_TYPE_DOUBLE:
            amxc_var_set(double, var, val->d);
            break;

        case OS_TR181_TYPE_STRING:
        case OS_TR181_TYPE_BASE64:
            amxc_var_set(cstring_t, var, val->str);
            break;

        case OS_TR181_TYPE_DATETIME: {
            amxc_ts_t ts = {
                .sec = val->ts.sec,
                .nsec = 0,
                .offset = val->ts.offset,
            };
            amxc_var_set_amxc_ts_t(var, &ts);
            break;
        }

        case OS_TR181_TYPE_DICT:
            return dict_to_amx_htable(val, var);

        case OS_TR181_TYPE_LIST:
            return list_to_amx_list(val, var);

        default:
            return OS_TR181_ERROR_INVALID;
    }

    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * amxc_var_t to os_tr181_val_t Conversion
 * ======================================================================== */

/*
 * Convert Ambiorix amxc_var_t to os_tr181_val_t
 * Supports all types including HTABLE -> DICT and LIST -> LIST
 *
 * Note on INT8 handling:
 * INT8 is treated as boolean for the following reasons:
 * 1. TR-181 standard does not define an int8 or byte type
 *    (valid integer types: int, long, unsignedInt, unsignedLong)
 * 2. The underlying ubus/blobmsg protocol defines BLOBMSG_TYPE_BOOL = BLOBMSG_TYPE_INT8
 *    and unconditionally treats INT8/BOOL as boolean in JSON formatting
 *    (see libubox/blobmsg_json.c line 250-251: prints "true"/"false" for INT8/BOOL case)
 * 3. Ambiorix converts boolean parameters to BLOBMSG_TYPE_INT8 internally
 *    but when converting from blobmsg it convers BLOBMSG_TYPE_INT8/BOOL to AMX INT8
 *
 * This ensures consistency with ubus behavior and proper TR-181 compliance.
 */
os_tr181_error_t os_tr181_val_from_amx(os_tr181_val_t *val, const amxc_var_t *var)
{
    if (!val || !var)
    {
        return OS_TR181_ERROR_INVALID;
    }

    uint32_t amx_type = amxc_var_type_of(var);

    switch (amx_type)
    {
        case AMXC_VAR_ID_NULL:
            val->type = OS_TR181_TYPE_NONE;
            return OS_TR181_SUCCESS;

        case AMXC_VAR_ID_BOOL:
            return os_val_set_bool(val, amxc_var_constcast(bool, var));

        case AMXC_VAR_ID_INT8:
            return os_val_set_bool(val, amxc_var_constcast(int8_t, var) != 0);

        case AMXC_VAR_ID_INT16:
            return os_val_set_int(val, amxc_var_constcast(int16_t, var));

        case AMXC_VAR_ID_INT32:
            return os_val_set_int(val, amxc_var_constcast(int32_t, var));

        case AMXC_VAR_ID_INT64:
            return os_val_set_int64(val, amxc_var_constcast(int64_t, var));

        case AMXC_VAR_ID_UINT8:
            return os_val_set_uint(val, amxc_var_constcast(uint8_t, var));

        case AMXC_VAR_ID_UINT16:
            return os_val_set_uint(val, amxc_var_constcast(uint16_t, var));

        case AMXC_VAR_ID_UINT32:
            return os_val_set_uint(val, amxc_var_constcast(uint32_t, var));

        case AMXC_VAR_ID_UINT64:
            return os_val_set_uint64(val, amxc_var_constcast(uint64_t, var));

        case AMXC_VAR_ID_FLOAT:
        case AMXC_VAR_ID_DOUBLE:
            return os_val_set_double(val, amxc_var_constcast(double, var));

        case AMXC_VAR_ID_CSTRING: {
            const char *str = amxc_var_constcast(cstring_t, var);
            return os_val_set_str_dup(val, str);
        }

        case AMXC_VAR_ID_TIMESTAMP: {
            const amxc_ts_t *ts = amxc_var_constcast(amxc_ts_t, var);
            if (!ts)
            {
                return OS_TR181_ERROR_INVALID;
            }
            os_tr181_timestamp_t ots = {
                .sec = ts->sec,
                .offset = ts->offset,
            };
            return os_val_set_datetime(val, ots);
        }

        case AMXC_VAR_ID_HTABLE:
            return amx_htable_to_dict(var, val);

        case AMXC_VAR_ID_LIST:
            return amx_list_to_list(var, val);

        default:
            /* Unknown type - this shouldn't happen with standard Ambiorix types */
            return OS_TR181_ERROR_INVALID;
    }
}

/* ========================================================================
 * Dictionary Conversion (os_tr181_val DICT <-> amxc_var htable)
 * ======================================================================== */

/*
 * Convert os_tr181_val_t DICT to amxc_var_t htable
 */
static os_tr181_error_t dict_to_amx_htable(const os_tr181_val_t *val, amxc_var_t *var)
{
    os_tr181_error_t err;

    if (!val || val->type != OS_TR181_TYPE_DICT || !var)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Set var to htable type */
    amxc_var_set_type(var, AMXC_VAR_ID_HTABLE);

    /* Convert each dictionary entry */
    os_val_iter_t it;
    const char *key;
    os_tr181_val_t *entry_val;
    os_val_foreach_dict(it, key, entry_val, (os_tr181_val_t *)val)
    {
        /* Create new amxc_var for this entry */
        amxc_var_t *amx_entry = amxc_var_add_new_key(var, key);
        if (!amx_entry)
        {
            return OS_TR181_ERROR;
        }

        /* Recursively convert value */
        err = os_tr181_val_to_amx(entry_val, amx_entry);
        if (err != OS_TR181_SUCCESS)
        {
            return err;
        }
    }

    return OS_TR181_SUCCESS;
}

/*
 * Convert amxc_var_t htable to os_tr181_val_t DICT
 */
static os_tr181_error_t amx_htable_to_dict(const amxc_var_t *var, os_tr181_val_t *val)
{
    const amxc_htable_t *htable;
    amxc_htable_it_t *it = NULL;

    if (!var || !val)
    {
        return OS_TR181_ERROR_INVALID;
    }

    htable = amxc_var_constcast(amxc_htable_t, var);
    if (!htable)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Initialize as dictionary */
    os_val_set_dict(val);

    /* Iterate through htable entries (it used by macro) */
    (void)it;
    amxc_htable_for_each(it, htable)
    {
        const char *key = amxc_htable_it_get_key(it);
        amxc_var_t *amx_value = amxc_var_from_htable_it(it);
        os_tr181_val_t entry_val = OS_VAL_INIT();
        os_tr181_error_t err;

        /* Recursively convert value */
        err = os_tr181_val_from_amx(&entry_val, amx_value);
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

    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * List Conversion (os_tr181_val LIST <-> amxc_var list)
 * ======================================================================== */

/*
 * Convert os_tr181_val_t LIST to amxc_var_t list
 */
static os_tr181_error_t list_to_amx_list(const os_tr181_val_t *val, amxc_var_t *var)
{
    os_tr181_error_t err;

    if (!val || val->type != OS_TR181_TYPE_LIST || !var)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Set var to list type */
    amxc_var_set_type(var, AMXC_VAR_ID_LIST);

    /* Convert each list element */
    os_val_iter_t it;
    os_tr181_val_t *entry_val;
    os_val_foreach(it, entry_val, (os_tr181_val_t *)val)
    {
        /* Create new amxc_var for this entry */
        amxc_var_t *amx_entry = amxc_var_add_new(var);
        if (!amx_entry)
        {
            return OS_TR181_ERROR;
        }

        /* Recursively convert value */
        err = os_tr181_val_to_amx(entry_val, amx_entry);
        if (err != OS_TR181_SUCCESS)
        {
            return err;
        }
    }

    return OS_TR181_SUCCESS;
}

/*
 * Convert amxc_var_t list to os_tr181_val_t LIST
 */
static os_tr181_error_t amx_list_to_list(const amxc_var_t *var, os_tr181_val_t *val)
{
    const amxc_llist_t *llist;
    amxc_llist_it_t *it = NULL;

    if (!var || !val)
    {
        return OS_TR181_ERROR_INVALID;
    }

    llist = amxc_var_constcast(amxc_llist_t, var);
    if (!llist)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Initialize as list */
    os_val_set_list(val);

    /* Iterate through list entries (it used by macro) */
    (void)it;
    amxc_llist_for_each(it, llist)
    {
        amxc_var_t *amx_value = amxc_var_from_llist_it(it);
        os_tr181_val_t entry_val = OS_VAL_INIT();
        os_tr181_error_t err;

        /* Recursively convert value */
        err = os_tr181_val_from_amx(&entry_val, amx_value);
        if (err != OS_TR181_SUCCESS)
        {
            os_val_free(val);
            return err;
        }

        /* Add to list */
        err = os_val_list_append(val, &entry_val);
        os_val_free(&entry_val);

        if (err != OS_TR181_SUCCESS)
        {
            os_val_free(val);
            return err;
        }
    }

    return OS_TR181_SUCCESS;
}
