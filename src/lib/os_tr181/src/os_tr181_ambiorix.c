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
 * os_tr181 - Ambiorix implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <amxc/amxc.h>
#include <amxp/amxp.h>
#include <amxd/amxd_types.h>
#include <amxd/amxd_dm.h>
#include <amxd/amxd_object.h>
#include <amxd/amxd_object_event.h>
#include <amxd/amxd_parameter.h>
#include <amxb/amxb.h>
#include <amxb/amxb_register.h>

#include "os_tr181.h"
#include "log.h"

/* Subscription entry */
struct subscription_entry
{
    os_tr181_handle_t *handle; /* Back-reference to parent handle */
    char *path;                /* Original path from user (parameter or object) */
    char *subscribe_path;      /* Actual subscribed object path (always ends with .) */
    char *filter_param;        /* Parameter name to filter (NULL for wildcard) */
    os_tr181_event_cb_t callback;
    void *user_data;
    bool marked_for_removal; /* True if unsubscribe was called during notification callback */
    struct subscription_entry *next;
};

/* Registered parameter entry */
struct param_entry
{
    char *path;
    os_tr181_param_type_t type;
    int writable;
    os_tr181_get_cb_t get_cb;
    os_tr181_set_cb_t set_cb;
    void *user_data;
    struct param_entry *next;
};

/* Registered object entry */
struct object_entry
{
    char *path;
    amxd_object_t *amx_object;
    struct object_entry *next;
};

/* Registered table entry */
struct table_entry
{
    char *path;
    amxd_object_t *amx_object;
    os_tr181_add_cb_t add_cb;
    os_tr181_del_cb_t del_cb;
    void *user_data;
    struct table_entry *next;
};

struct os_tr181_handle_s
{
    amxb_bus_ctx_t *bus_ctx;
    amxd_dm_t dm;
    struct subscription_entry *subscriptions;
    struct object_entry *objects;
    struct param_entry *parameters;
    struct table_entry *tables;
    bool in_callback; /* True when executing user callback functions */
};

/* Type translation helpers */
os_tr181_param_type_t amxc_type_to_os_tr181(uint32_t amxc_type)
{
    switch (amxc_type)
    {
        case AMXC_VAR_ID_BOOL:
            return OS_TR181_TYPE_BOOL;
        case AMXC_VAR_ID_INT8:
        case AMXC_VAR_ID_INT16:
        case AMXC_VAR_ID_INT32:
            return OS_TR181_TYPE_INT;
        case AMXC_VAR_ID_INT64:
            return OS_TR181_TYPE_INT64;
        case AMXC_VAR_ID_UINT8:
        case AMXC_VAR_ID_UINT16:
        case AMXC_VAR_ID_UINT32:
            return OS_TR181_TYPE_UINT;
        case AMXC_VAR_ID_UINT64:
            return OS_TR181_TYPE_UINT64;
        case AMXC_VAR_ID_CSTRING:
        default:
            return OS_TR181_TYPE_STRING;
    }
}

uint32_t os_tr181_type_to_amxc(os_tr181_param_type_t type)
{
    switch (type)
    {
        case OS_TR181_TYPE_BOOL:
            return AMXC_VAR_ID_BOOL;
        case OS_TR181_TYPE_INT:
            return AMXC_VAR_ID_INT32;
        case OS_TR181_TYPE_UINT:
            return AMXC_VAR_ID_UINT32;
        case OS_TR181_TYPE_INT64:
            return AMXC_VAR_ID_INT64;
        case OS_TR181_TYPE_UINT64:
            return AMXC_VAR_ID_UINT64;
        case OS_TR181_TYPE_STRING:
        case OS_TR181_TYPE_DATETIME:
        case OS_TR181_TYPE_BASE64:
        default:
            return AMXC_VAR_ID_CSTRING;
    }
}

/* ========================================================================
 * Value Conversion Functions (os_tr181_val_t <-> amxc_var_t)
 * ======================================================================== */

/*
 * Convert os_tr181_val_t to Ambiorix amxc_var_t
 * Note: var should be initialized before calling (amxc_var_init)
 */
os_tr181_error_t os_tr181_val_to_amx(const os_tr181_val_t *val, amxc_var_t *var)
{
    if (!val || !var)
    {
        return OS_TR181_ERROR_INVALID;
    }

    switch (val->type)
    {
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
            /* Convert timestamp to string */
            char *str = NULL;
            os_tr181_error_t err = os_val_to_str(val, &str);
            if (err != OS_TR181_SUCCESS)
            {
                return err;
            }
            amxc_var_set(cstring_t, var, str);
            free(str);
            break;
        }

        default:
            return OS_TR181_ERROR_INVALID;
    }

    return OS_TR181_SUCCESS;
}

/*
 * Convert Ambiorix amxc_var_t to os_tr181_val_t
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
        case AMXC_VAR_ID_BOOL:
            return os_val_set_bool(val, amxc_var_constcast(bool, var));

        case AMXC_VAR_ID_INT8:
        case AMXC_VAR_ID_INT16:
        case AMXC_VAR_ID_INT32:
            return os_val_set_int(val, amxc_var_constcast(int32_t, var));

        case AMXC_VAR_ID_INT64:
            return os_val_set_int64(val, amxc_var_constcast(int64_t, var));

        case AMXC_VAR_ID_UINT8:
        case AMXC_VAR_ID_UINT16:
        case AMXC_VAR_ID_UINT32:
            return os_val_set_uint(val, amxc_var_constcast(uint32_t, var));

        case AMXC_VAR_ID_UINT64:
            return os_val_set_uint64(val, amxc_var_constcast(uint64_t, var));

        case AMXC_VAR_ID_DOUBLE:
        case AMXC_VAR_ID_FLOAT:
            return os_val_set_double(val, amxc_var_constcast(double, var));

        case AMXC_VAR_ID_CSTRING: {
            const char *str = amxc_var_constcast(cstring_t, var);
            return os_val_set_str_dup(val, str);
        }

        default:
            return OS_TR181_ERROR_INVALID;
    }
}

/* Helper function to convert string value to amxc_var based on type */
os_tr181_error_t os_tr181_init(os_tr181_handle_t **handle)
{
    os_tr181_handle_t *h = malloc(sizeof(os_tr181_handle_t));
    if (!h)
    {
        return OS_TR181_ERROR_INIT;
    }

    h->bus_ctx = NULL;
    h->subscriptions = NULL;
    h->objects = NULL;
    h->parameters = NULL;
    h->tables = NULL;
    h->in_callback = false;

    /* Initialize data model */
    amxd_dm_init(&h->dm);

    /* Load ambiorix ubus backend */
    if (amxb_be_load("libamxb_ubus.so") != 0)
    {
        LOGE("Failed to load ubus backend");
        amxd_dm_clean(&h->dm);
        free(h);
        return OS_TR181_ERROR_INIT;
    }

    /* Connect to ubus */
    if (amxb_connect(&h->bus_ctx, "ubus:/var/run/ubus/ubus.sock") != 0)
    {
        LOGE("Failed to connect to ubus");
        amxd_dm_clean(&h->dm);
        free(h);
        return OS_TR181_ERROR_INIT;
    }

    *handle = h;
    return OS_TR181_SUCCESS;
}

void os_tr181_close(os_tr181_handle_t *handle)
{
    if (handle)
    {
        /* Clean up all subscriptions */
        struct subscription_entry *sub = handle->subscriptions;
        while (sub)
        {
            struct subscription_entry *next = sub->next;
            free(sub->path);
            free(sub);
            sub = next;
        }

        /* Clean up registered objects */
        struct object_entry *obj = handle->objects;
        while (obj)
        {
            struct object_entry *next = obj->next;
            free(obj->path);
            free(obj);
            obj = next;
        }

        /* Clean up registered parameters */
        struct param_entry *param = handle->parameters;
        while (param)
        {
            struct param_entry *next = param->next;
            free(param->path);
            free(param);
            param = next;
        }

        /* Clean up registered tables */
        struct table_entry *table = handle->tables;
        while (table)
        {
            struct table_entry *next = table->next;
            free(table->path);
            free(table);
            table = next;
        }

        if (handle->bus_ctx)
        {
            amxb_disconnect(handle->bus_ctx);
            amxb_free(&handle->bus_ctx);
        }

        /* Clean up data model */
        amxd_dm_clean(&handle->dm);

        free(handle);
    }
}

os_tr181_error_t os_tr181_get_val(os_tr181_handle_t *handle, const char *param_name, os_tr181_val_t *value)
{
    int ret;
    amxc_var_t result;
    amxc_var_t *param_value = NULL;

    if (!handle || !handle->bus_ctx || !param_name || !value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    amxc_var_init(&result);

    ret = amxb_get(handle->bus_ctx, param_name, 0, &result, OS_TR181_DEFAULT_TIMEOUT_MS / 1000);

    if (ret != 0)
    {
        amxc_var_clean(&result);
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /*amxc_var_dump_stream(&result, stderr);*/
    /* Parse the result structure:
     * Result is an array with one element containing object path -> parameters
     * Structure: [ { "Device.Path." = { "ParamName" = value } } ]
     */
    const amxc_var_t *array_elem = amxc_var_get_index(&result, 0, AMXC_VAR_FLAG_DEFAULT);
    if (array_elem)
    {
        /* Get the first (and only) key which is the object path */
        const amxc_htable_t *htable = amxc_var_constcast(amxc_htable_t, array_elem);
        if (htable)
        {
            amxc_htable_it_t *it = amxc_htable_get_first(htable);
            if (it)
            {
                const amxc_var_t *obj_var = amxc_var_from_htable_it(it);
                /* Now get the parameter name from the full path */
                const char *last_dot = strrchr(param_name, '.');
                if (last_dot && last_dot > param_name)
                {
                    const char *param_only = last_dot + 1;
                    param_value = amxc_var_get_key(obj_var, param_only, AMXC_VAR_FLAG_DEFAULT);
                    if (param_value)
                    {
                        /* Convert Ambiorix value to os_tr181_val_t */
                        os_tr181_error_t err = os_tr181_val_from_amx(value, param_value);
                        amxc_var_clean(&result);
                        return err;
                    }
                }
            }
        }
    }

    amxc_var_clean(&result);
    return OS_TR181_ERROR;
}

os_tr181_error_t os_tr181_set_val(os_tr181_handle_t *handle, const char *param_name, const os_tr181_val_t *value)
{
    int ret;
    amxc_var_t values;
    amxc_var_t *param_value = NULL;
    char *object_path = NULL;
    char *param_only = NULL;
    char *last_dot = NULL;

    if (!handle || !handle->bus_ctx || !param_name || !value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Extract object path and parameter name */
    object_path = strdup(param_name);
    last_dot = strrchr(object_path, '.');
    if (!last_dot || last_dot == object_path)
    {
        free(object_path);
        return OS_TR181_ERROR_INVALID;
    }

    /* Split into object path and parameter name */
    last_dot++;
    param_only = strdup(last_dot);
    *last_dot = '\0'; /* Terminate object_path at the last dot */

    amxc_var_init(&values);
    amxc_var_set_type(&values, AMXC_VAR_ID_HTABLE);

    /* Add a new variant to the hash table and get pointer to it */
    param_value = amxc_var_add_new_key(&values, param_only);
    if (!param_value)
    {
        amxc_var_clean(&values);
        free(object_path);
        free(param_only);
        return OS_TR181_ERROR;
    }

    /* Convert os_tr181_val_t to amxc_var_t */
    os_tr181_error_t conv_err = os_tr181_val_to_amx(value, param_value);
    if (conv_err != OS_TR181_SUCCESS)
    {
        amxc_var_clean(&values);
        free(object_path);
        free(param_only);
        return conv_err;
    }

    amxc_var_t ret_var;
    amxc_var_init(&ret_var);
    ret = amxb_set(handle->bus_ctx, object_path, &values, &ret_var, OS_TR181_DEFAULT_TIMEOUT_MS / 1000);

    amxc_var_clean(&ret_var);
    amxc_var_clean(&values);
    free(object_path);
    free(param_only);
    return (ret == 0) ? OS_TR181_SUCCESS : OS_TR181_ERROR;
}

/* Helper function to map amxd_object_type_t to os_tr181_param_type_t */
static os_tr181_param_type_t amxd_object_type_to_os_tr181_type(uint32_t obj_type)
{
    switch (obj_type)
    {
        case amxd_object_template:
            return OS_TR181_TYPE_OBJECT;
        case amxd_object_instance:
            return OS_TR181_TYPE_INSTANCE;
        default: /* amxd_object_singleton, amxd_object_root, amxd_object_mib */
            return OS_TR181_TYPE_OBJECT;
    }
}

/* Callback data structure for amxb_list */
struct list_callback_data
{
    os_tr181_param_info_t *params;
    int count;
    int capacity;
    int status; /* OS_TR181_SUCCESS or error code */
    amxb_bus_ctx_t *bus_ctx;
    const char *base_path; /* The original path being listed */
    bool recursive;        /* Whether listing is recursive */
};

/* Helper function to add a parameter to the list */
static int add_param_to_list(
        struct list_callback_data *cb_data,
        const char *name,
        os_tr181_param_type_t type,
        int writable)
{
    /* Expand capacity if needed */
    if (cb_data->count >= cb_data->capacity)
    {
        int new_capacity = cb_data->capacity == 0 ? 128 : cb_data->capacity * 2;
        os_tr181_param_info_t *new_params = realloc(cb_data->params, new_capacity * sizeof(os_tr181_param_info_t));
        if (!new_params)
        {
            LOGE("Failed to allocate memory for parameter list");
            return OS_TR181_ERROR;
        }
        cb_data->params = new_params;
        cb_data->capacity = new_capacity;
    }

    cb_data->params[cb_data->count].name = strdup(name);
    if (!cb_data->params[cb_data->count].name)
    {
        LOGE("Failed to allocate memory for parameter name");
        return OS_TR181_ERROR;
    }
    cb_data->params[cb_data->count].type = type;
    cb_data->params[cb_data->count].writable = writable;
    cb_data->count++;

    return OS_TR181_SUCCESS;
}

/* Callback function for amxb_list to process object paths */
static void list_callback(const amxb_bus_ctx_t *bus_ctx, const amxc_var_t *const data, void *priv)
{
    struct list_callback_data *cb_data = (struct list_callback_data *)priv;
    amxc_var_t describe_result;
    int ret;
    bool is_base_path;

    (void)bus_ctx; /* Unused */

    if (!data || !cb_data)
    {
        return;
    }

    /* If already in error state, don't process more */
    if (cb_data->status != OS_TR181_SUCCESS)
    {
        return;
    }

    /* data contains an array of object paths */
    amxc_var_for_each(var_path, data)
    {
        const char *obj_path = amxc_var_constcast(cstring_t, var_path);
        if (!obj_path)
        {
            continue;
        }

        /* Check if this is the base path or a child object */
        is_base_path = (strcmp(obj_path, cb_data->base_path) == 0);

        LOGD("list_callback: object path='%s' (is_base=%d)", obj_path, is_base_path);

        /* Describe this object to get its parameters */
        amxc_var_init(&describe_result);
        ret = amxb_describe(cb_data->bus_ctx, obj_path, AMXB_FLAG_PARAMETERS, &describe_result, 5);

        if (ret != 0)
        {
            LOGD("  amxb_describe failed for %s: %d", obj_path, ret);
            amxc_var_clean(&describe_result);
            continue;
        }

        /* Parse describe result - format: array[0] contains object info */
        amxc_var_t *obj_info = amxc_var_get_index(&describe_result, 0, AMXC_VAR_FLAG_DEFAULT);
        if (!obj_info)
        {
            amxc_var_clean(&describe_result);
            continue;
        }

        /* Check if object is protected - skip if so */
        amxc_var_t *obj_attrs = GET_ARG(obj_info, "attributes");
        if (obj_attrs)
        {
            uint32_t obj_protected = GET_UINT32(obj_attrs, "protected");
            if (obj_protected)
            {
                LOGD("  object is protected, skipping");
                amxc_var_clean(&describe_result);
                continue;
            }
        }

        /* Get object type from describe result
         * Note: For instances, the 'path' field returns template path without instance number
         * So we use the obj_path parameter (from amxb_list) which has the correct full path */
        uint32_t obj_type_id = GET_UINT32(obj_info, "type_id");

        LOGD("  object: obj_path=%s, type_id=%u", obj_path, obj_type_id);

        /* Add the object itself to the list using the correct path from amxb_list */
        os_tr181_param_type_t obj_type = amxd_object_type_to_os_tr181_type(obj_type_id);
        ret = add_param_to_list(cb_data, obj_path, obj_type, 1);
        if (ret != OS_TR181_SUCCESS)
        {
            cb_data->status = ret;
            amxc_var_clean(&describe_result);
            return;
        }

        /* Skip parameters for template objects (type_id=2)
         * Templates describe what instances look like but don't have actual parameters */
        if (obj_type_id == amxd_object_template)
        {
            LOGD("  template object, skipping parameters");
            amxc_var_clean(&describe_result);
            continue;
        }

        /* In non-recursive mode, only get parameters for the base path object
         * Child objects are listed but their parameters are not enumerated */
        if (!cb_data->recursive && !is_base_path)
        {
            LOGD("  non-recursive mode, skipping parameters for child object");
            amxc_var_clean(&describe_result);
            continue;
        }

        /* Now extract all parameters from this object */
        amxc_var_t *params_table = GET_ARG(obj_info, "parameters");
        if (params_table && amxc_var_type_of(params_table) == AMXC_VAR_ID_HTABLE)
        {
            const amxc_htable_t *params_ht = amxc_var_constcast(amxc_htable_t, params_table);

            amxc_htable_iterate(it, params_ht)
            {
                const char *param_name = amxc_htable_it_get_key(it);
                amxc_var_t *param_var = amxc_var_from_htable_it(it);

                if (!param_name || !param_var)
                {
                    continue;
                }

                /* Check if parameter is protected - skip if so */
                amxc_var_t *param_attrs = GET_ARG(param_var, "attributes");
                if (param_attrs)
                {
                    uint32_t param_protected = GET_UINT32(param_attrs, "protected");
                    if (param_protected)
                    {
                        LOGD("  parameter %s is protected, skipping", param_name);
                        continue;
                    }
                }

                /* Build full parameter path */
                char full_path[512];
                snprintf(full_path, sizeof(full_path), "%s%s", obj_path, param_name);

                /* Get parameter type using existing conversion function */
                uint32_t param_type_id = GET_UINT32(param_var, "type_id");
                os_tr181_param_type_t param_type = amxc_type_to_os_tr181(param_type_id);

                /* Get writable flag from attributes */
                int writable = 1; /* Default to writable */
                if (param_attrs)
                {
                    uint32_t read_only = GET_UINT32(param_attrs, "read-only");
                    writable = (read_only == 0);
                }

                LOGD("  param: %s, type_id=%u, writable=%d", param_name, param_type_id, writable);

                /* Add parameter to list */
                ret = add_param_to_list(cb_data, full_path, param_type, writable);
                if (ret != OS_TR181_SUCCESS)
                {
                    cb_data->status = ret;
                    amxc_var_clean(&describe_result);
                    return;
                }
            }
        }

        amxc_var_clean(&describe_result);
    }
}

os_tr181_error_t os_tr181_list(
        os_tr181_handle_t *handle,
        const char *path,
        bool recursive,
        os_tr181_param_info_t **params,
        int *count)
{
    int ret;
    uint32_t flags;
    struct list_callback_data cb_data = {
        .params = NULL,
        .count = 0,
        .capacity = 0,
        .status = OS_TR181_SUCCESS,
        .bus_ctx = handle->bus_ctx,
        .base_path = path,
        .recursive = recursive};

    if (!handle || !handle->bus_ctx || !path || !params || !count)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Set flags: list objects/instances (not individual parameters) */
    flags = AMXB_FLAG_OBJECTS | AMXB_FLAG_INSTANCES;
    if (!recursive)
    {
        flags |= AMXB_FLAG_FIRST_LVL;
    }

    LOGD("amxb_list path='%s', flags=0x%x (recursive=%d)", path, flags, recursive);

    /* List objects - callback will describe each object to get parameters */
    ret = amxb_list(handle->bus_ctx, path, flags, list_callback, &cb_data);

    if (ret != 0 || cb_data.status != OS_TR181_SUCCESS)
    {
        /* Clean up on error */
        if (cb_data.params)
        {
            for (int i = 0; i < cb_data.count; i++)
            {
                free(cb_data.params[i].name);
            }
            free(cb_data.params);
        }
        return cb_data.status != OS_TR181_SUCCESS ? cb_data.status : OS_TR181_ERROR_NOT_FOUND;
    }

    *params = cb_data.params;
    *count = cb_data.count;

    return OS_TR181_SUCCESS;
}

void os_tr181_free_list(os_tr181_param_info_t *params, int count)
{
    if (params)
    {
        for (int i = 0; i < count; i++)
        {
            if (params[i].name)
            {
                free(params[i].name);
            }
        }
        free(params);
    }
}

/* Forward declaration */
static void subs_event_handler(const char *const sig_name, const amxc_var_t *const data, void *const priv);

/* Helper function to free subscription entry and all its fields */
static void free_subscription_entry(struct subscription_entry *sub)
{
    if (!sub) return;
    free(sub->filter_param);
    free(sub->subscribe_path);
    free(sub->path);
    free(sub);
}

/* Cleanup subscriptions marked for removal during notification callback dispatch */
static void cleanup_marked_subscriptions(os_tr181_handle_t *handle)
{
    struct subscription_entry **sub_ptr;
    struct subscription_entry *sub;

    if (!handle)
    {
        return;
    }

    sub_ptr = &handle->subscriptions;
    while (*sub_ptr)
    {
        sub = *sub_ptr;
        if (sub->marked_for_removal)
        {
            /* Unsubscribe from Ambiorix */
            amxb_unsubscribe(handle->bus_ctx, sub->subscribe_path, subs_event_handler, sub);

            LOGD("Cleanup: unsubscribed from %s (object: %s)", sub->path, sub->subscribe_path);

            /* Remove from list and free */
            *sub_ptr = sub->next;
            free_subscription_entry(sub);
        }
        else
        {
            sub_ptr = &sub->next;
        }
    }
}

/* Event callback handler for Ambiorix */
static void subs_event_handler(const char *const sig_name, const amxc_var_t *const data, void *const priv)
{
    (void)sig_name; /* unused */
    struct subscription_entry *sub = (struct subscription_entry *)priv;
    os_tr181_handle_t *handle;

    LOGD("%s %s %p %p", __func__, sig_name, data, priv);

    if (!sub || !sub->handle || !data)
    {
        return;
    }

    handle = sub->handle;

    /*amxc_var_dump_stream(data, stderr);*/
    /*
    Sample event structure:
        {
            eobject = "MyApp.Sample.",
            notification = "dm:object-changed",
            object = "MyApp.Sample.",
            parameters = {
                text = {
                    from = "a1",
                    to = "a2"
                }
            },
            path = "MyApp.Sample."
        }
     */

    /* Extract object path from event data */
    /* from amxd_object_event.h:
     * It is recommended to use the path and avoid to use the object or eobject. The
     * path is compatible with USP specifications while object and eobject paths can
     * only be used in ambiorix APIs.
     */

    amxc_var_t *obj_var = amxc_var_get_path(data, "path", AMXC_VAR_FLAG_DEFAULT);
    if (!obj_var)
    {
        return;
    }
    const char *object_path = amxc_var_constcast(cstring_t, obj_var);
    if (!object_path)
    {
        return;
    }
    // LOGD("object_path=%s", object_path);

    /* Extract parameters hash table */
    amxc_var_t *params_var = amxc_var_get_path(data, "parameters", AMXC_VAR_FLAG_DEFAULT);
    if (!params_var || amxc_var_type_of(params_var) != AMXC_VAR_ID_HTABLE)
    {
        return;
    }

    /* Iterate over changed parameters */
    amxc_htable_t *params_htable = (amxc_htable_t *)amxc_var_constcast(amxc_htable_t, params_var);
    amxc_htable_iterate(it, params_htable)
    {
        const char *param_name = amxc_htable_it_get_key(it);
        amxc_var_t *param_data = amxc_var_from_htable_it(it);

        /* Get the new value ("to" field) */
        amxc_var_t *to_var = amxc_var_get_path(param_data, "to", AMXC_VAR_FLAG_DEFAULT);
        if (!to_var)
        {
            continue;
        }

        /* Build full parameter path */
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s%s", object_path, param_name);

        /* Convert amxc_var_t to os_tr181_val_t */
        os_tr181_val_t val = OS_VAL_INIT();
        if (os_tr181_val_from_amx(&val, to_var) != OS_TR181_SUCCESS)
        {
            continue;
        }

        /* Skip if this subscription is marked for removal */
        if (sub->marked_for_removal)
        {
            os_val_free(&val);
            continue;
        }

        /* Check if this subscription matches this specific parameter event */
        bool should_notify = false;
        if (sub->filter_param)
        {
            /* Parameter subscription - exact match only */
            if (strcmp(sub->path, full_path) == 0)
            {
                LOGD("Parameter match: %s", full_path);
                should_notify = true;
            }
        }
        else
        {
            /* Object/wildcard subscription - prefix match */
            size_t sub_len = strlen(sub->path);
            if (strncmp(sub->path, full_path, sub_len) == 0)
            {
                LOGD("Wildcard match: %s (sub: %s)", full_path, sub->path);
                should_notify = true;
            }
        }

        if (should_notify)
        {
            /* Mark that we're executing user callback */
            handle->in_callback = true;

            /* Dispatch to this subscription's callback */
            sub->callback(full_path, &val, sub->user_data);

            /* Clear callback flag */
            handle->in_callback = false;

            /* Clean up any subscriptions that were marked during callback */
            cleanup_marked_subscriptions(handle);
        }

        os_val_free(&val);
    }
}

os_tr181_error_t os_tr181_subscribe(
        os_tr181_handle_t *handle,
        const char *path,
        os_tr181_event_cb_t callback,
        void *user_data,
        os_tr181_sub_handle_t *sub_handle)
{
    struct subscription_entry *sub;
    int ret;
    size_t path_len;
    bool is_object;

    if (!handle || !handle->bus_ctx || !path || !callback || !sub_handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    path_len = strlen(path);
    if (path_len == 0)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Determine if path is an object (ends with .) or parameter */
    is_object = (path[path_len - 1] == '.');

    /* Create new subscription entry (calloc zeroes memory) */
    sub = calloc(1, sizeof(struct subscription_entry));
    if (!sub)
    {
        return OS_TR181_ERROR;
    }

    sub->path = strdup(path);
    if (!sub->path)
    {
        free(sub);
        return OS_TR181_ERROR;
    }
    sub->handle = handle;
    sub->callback = callback;
    sub->user_data = user_data;

    if (is_object)
    {
        /* Object/wildcard subscription - subscribe directly */
        sub->subscribe_path = strdup(path);
        if (!sub->subscribe_path)
        {
            free_subscription_entry(sub);
            return OS_TR181_ERROR;
        }
        /* filter_param remains NULL (already zeroed by calloc) */
        LOGD("Object subscription: %s", path);
    }
    else
    {
        /* Parameter subscription - extract parent object and parameter name */
        const char *last_dot = strrchr(path, '.');
        if (!last_dot)
        {
            /* No dot found - invalid path */
            free_subscription_entry(sub);
            return OS_TR181_ERROR_INVALID;
        }

        /* Extract parent object path (up to and including the last dot) */
        size_t parent_len = last_dot - path + 1;
        sub->subscribe_path = malloc(parent_len + 1);
        if (!sub->subscribe_path)
        {
            free_subscription_entry(sub);
            return OS_TR181_ERROR;
        }
        strncpy(sub->subscribe_path, path, parent_len);
        sub->subscribe_path[parent_len] = '\0';

        /* Extract parameter name (after the last dot) */
        sub->filter_param = strdup(last_dot + 1);
        if (!sub->filter_param)
        {
            free_subscription_entry(sub);
            return OS_TR181_ERROR;
        }

        LOGD("Parameter subscription: %s (object: %s, param: %s)", path, sub->subscribe_path, sub->filter_param);
    }

    /* Add to list */
    sub->next = handle->subscriptions;
    handle->subscriptions = sub;

    /* Subscribe to Ambiorix events on the object path */
    ret = amxb_subscribe(handle->bus_ctx, sub->subscribe_path, NULL, subs_event_handler, sub);
    if (ret != 0)
    {
        LOGE("amxb_subscribe failed for %s: %d", sub->subscribe_path, ret);
        /* Remove from list on failure */
        handle->subscriptions = sub->next;
        free_subscription_entry(sub);
        return OS_TR181_ERROR;
    }

    *sub_handle = (os_tr181_sub_handle_t)sub;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_unsubscribe(os_tr181_handle_t *handle, os_tr181_sub_handle_t sub_handle)
{
    struct subscription_entry **sub_ptr;
    struct subscription_entry *sub;

    if (!handle || !handle->bus_ctx || !sub_handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    sub = (struct subscription_entry *)sub_handle;

    /* Validate that subscription belongs to this handle */
    if (sub->handle != handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* If we're executing callbacks, defer removal */
    if (handle->in_callback)
    {
        sub->marked_for_removal = true;
        LOGD("Marked subscription for removal: %s (will be cleaned up after callbacks complete)", sub->path);
        return OS_TR181_SUCCESS;
    }

    /* Not in callback - remove immediately */
    sub_ptr = &handle->subscriptions;
    while (*sub_ptr)
    {
        if (*sub_ptr == sub)
        {
            /* Unsubscribe from Ambiorix using the subscribed object path */
            amxb_unsubscribe(handle->bus_ctx, sub->subscribe_path, subs_event_handler, sub);

            LOGD("Unsubscribed from %s (object: %s)", sub->path, sub->subscribe_path);

            *sub_ptr = sub->next;
            free_subscription_entry(sub);
            return OS_TR181_SUCCESS;
        }
        sub_ptr = &(*sub_ptr)->next;
    }

    return OS_TR181_ERROR_NOT_FOUND;
}

os_tr181_error_t os_tr181_wait_event(os_tr181_handle_t *handle, int timeout_ms)
{
    int ret;

    if (!handle || !handle->bus_ctx)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Wait for events on the bus and signal manager */
    struct timeval tv;
    fd_set readfds;
    int bus_fd = amxb_get_fd(handle->bus_ctx);
    int sig_fd = amxp_signal_fd();
    int max_fd;

    if (bus_fd < 0 || sig_fd < 0)
    {
        return OS_TR181_ERROR;
    }

    max_fd = (bus_fd > sig_fd) ? bus_fd : sig_fd;

    FD_ZERO(&readfds);
    FD_SET(bus_fd, &readfds);
    FD_SET(sig_fd, &readfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(max_fd + 1, &readfds, NULL, NULL, &tv);

    if (ret == 0)
    {
        return OS_TR181_ERROR_TIMEOUT;
    }
    else if (ret < 0)
    {
        return OS_TR181_ERROR;
    }

    /* Process bus events */
    if (FD_ISSET(bus_fd, &readfds))
    {
        ret = amxb_read(handle->bus_ctx);
        if (ret != 0)
        {
            return OS_TR181_ERROR;
        }
    }

    /* Process signal events */
    if (FD_ISSET(sig_fd, &readfds))
    {
        ret = amxp_signal_read();
        if (ret != 0)
        {
            return OS_TR181_ERROR;
        }
    }

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_register_object(os_tr181_handle_t *handle, const char *object_path)
{
    struct object_entry *obj;
    amxd_object_t *amx_obj = NULL;
    amxd_object_t *parent_obj = NULL;
    amxd_status_t status;
    char *path_copy;
    char *token;
    char *saveptr;
    char current_path[512] = "";

    if (!handle || !object_path)
    {
        LOGE("Invalid parameters for register_object");
        return OS_TR181_ERROR_INVALID;
    }

    /* Check if already registered */
    for (obj = handle->objects; obj != NULL; obj = obj->next)
    {
        if (strcmp(obj->path, object_path) == 0)
        {
            LOGE("Object %s already registered", object_path);
            return OS_TR181_ERROR;
        }
    }

    /* Create full object hierarchy */
    path_copy = strdup(object_path);
    if (!path_copy)
    {
        return OS_TR181_ERROR;
    }

    /* Remove trailing dot if present */
    size_t len = strlen(path_copy);
    if (len > 0 && path_copy[len - 1] == '.')
    {
        path_copy[len - 1] = '\0';
    }

    parent_obj = amxd_dm_get_root(&handle->dm);

    /* Build object hierarchy by tokenizing the path */
    token = strtok_r(path_copy, ".", &saveptr);
    while (token)
    {
        amxd_object_t *existing_obj;

        /* Build current path */
        if (strlen(current_path) > 0)
        {
            strcat(current_path, ".");
        }
        strcat(current_path, token);

        /* Check if this object already exists in our tree */
        existing_obj = amxd_object_get_child(parent_obj, token);

        if (!existing_obj)
        {
            /* Create new object */
            status = amxd_object_new(&amx_obj, amxd_object_singleton, token);
            if (status != amxd_status_ok)
            {
                LOGE("Failed to create object %s: %d", token, status);
                free(path_copy);
                return OS_TR181_ERROR;
            }

            /* Add to parent */
            status = amxd_object_add_object(parent_obj, amx_obj);
            if (status != amxd_status_ok)
            {
                LOGE("Failed to add object %s to parent: %d", token, status);
                amxd_object_delete(&amx_obj);
                free(path_copy);
                return OS_TR181_ERROR;
            }

            /* Make sure object is public (not private or protected) */
            amxd_object_set_attr(amx_obj, amxd_oattr_private, false);
            amxd_object_set_attr(amx_obj, amxd_oattr_protected, false);

            LOGD("Created object: %s", current_path);
            parent_obj = amx_obj;
        }
        else
        {
            LOGD("Object already exists: %s", current_path);
            parent_obj = existing_obj;
            amx_obj = existing_obj;
        }

        token = strtok_r(NULL, ".", &saveptr);
    }

    free(path_copy);

    /* Store in our object list */
    obj = malloc(sizeof(struct object_entry));
    if (!obj)
    {
        LOGE("Failed to allocate memory for object entry");
        return OS_TR181_ERROR;
    }

    obj->path = strdup(object_path);
    obj->amx_object = amx_obj; /* This is the leaf object */
    obj->next = handle->objects;
    handle->objects = obj;

    LOGD("Registered object: %s", object_path);

    /* Don't register with bus yet - wait until parameters are added */
    /* Registration will be done by os_tr181_publish_objects() */

    return OS_TR181_SUCCESS;
}

/* Callback for handling parameter read requests */
static amxd_status_t param_read_cb(
        amxd_object_t *const object,
        amxd_param_t *const param,
        amxd_action_t reason,
        const amxc_var_t *const args,
        amxc_var_t *const retval,
        void *priv)
{
    struct param_entry *pe = (struct param_entry *)priv;
    int ret;
    const char *param_name = param ? amxd_param_get_name(param) : NULL;
    char full_path[512];
    os_tr181_val_t val = OS_VAL_INIT();

    (void)args;
    (void)reason;

    LOGD("param_read_cb called for parameter: %s pe->path=%s", param_name ?: "", pe ? pe->path ?: "" : "");

    if (!pe || !pe->get_cb)
    {
        LOGD("No callback registered");
        return amxd_status_function_not_implemented;
    }

    /* Build actual parameter path from object path and parameter name */
    if (object && param_name)
    {
        char *obj_path = amxd_object_get_path(object, AMXD_OBJECT_INDEXED | AMXD_OBJECT_TERMINATE);
        if (obj_path)
        {
            snprintf(full_path, sizeof(full_path), "%s%s", obj_path, param_name);
            free(obj_path);
        }
        else
        {
            /* Fallback to registered path if we can't get object path */
            snprintf(full_path, sizeof(full_path), "%s", pe->path);
        }
    }
    else
    {
        snprintf(full_path, sizeof(full_path), "%s", pe->path);
    }

    LOGD("Reading parameter: %s", full_path);

    /* Initialize value and call user's get callback */
    ret = pe->get_cb(full_path, &val, pe->user_data);
    if (ret != OS_TR181_SUCCESS)
    {
        LOGD("User callback failed: %d", ret);
        os_val_free(&val);
        return amxd_status_unknown_error;
    }

    /* Convert os_tr181_val_t to amxc_var_t */
    if (os_tr181_val_to_amx(&val, retval) != OS_TR181_SUCCESS)
    {
        os_val_free(&val);
        return amxd_status_invalid_type;
    }

    os_val_free(&val);

    /* Update the parameter value */
    amxd_param_set_value(param, retval);

    return amxd_status_ok;
}

/* Callback for handling parameter write requests */
static amxd_status_t param_write_cb(
        amxd_object_t *const object,
        amxd_param_t *const param,
        amxd_action_t reason,
        const amxc_var_t *const args,
        amxc_var_t *const retval,
        void *priv)
{
    struct param_entry *pe = (struct param_entry *)priv;
    os_tr181_val_t val = OS_VAL_INIT();
    int ret;
    const char *param_name = param ? amxd_param_get_name(param) : NULL;
    char full_path[512];

    (void)reason;
    (void)retval;

    LOGD("param_write_cb called for parameter: %s pe->path=%s", param_name ?: "", pe ? pe->path ?: "" : "");

    if (!pe || !pe->set_cb)
    {
        return amxd_status_read_only;
    }

    /* Build actual parameter path from object path and parameter name */
    if (object && param_name)
    {
        char *obj_path = amxd_object_get_path(object, AMXD_OBJECT_INDEXED | AMXD_OBJECT_TERMINATE);
        if (obj_path)
        {
            snprintf(full_path, sizeof(full_path), "%s%s", obj_path, param_name);
            free(obj_path);
        }
        else
        {
            /* Fallback to registered path if we can't get object path */
            snprintf(full_path, sizeof(full_path), "%s", pe->path);
        }
    }
    else
    {
        snprintf(full_path, sizeof(full_path), "%s", pe->path);
    }

    LOGD("Writing parameter: %s", full_path);

    /* Convert amxc_var_t to os_tr181_val_t */
    if (os_tr181_val_from_amx(&val, args) != OS_TR181_SUCCESS)
    {
        LOGE("Failed to convert parameter value");
        os_val_free(&val);
        return amxd_status_invalid_type;
    }

    /* Call user's set callback */
    ret = pe->set_cb(full_path, &val, pe->user_data);

    os_val_free(&val);

    if (ret != OS_TR181_SUCCESS)
    {
        return amxd_status_unknown_error;
    }

    /* if successfull, set the param value, so that the event
     * dm:object-changed can be propagated on detected change by the
     * underlying libamxd */
    if (amxc_var_convert(&param->value, args, amxc_var_type_of(&param->value)) != 0)
    {
        return amxd_status_invalid_value;
    }

    return amxd_status_ok;
}

os_tr181_error_t os_tr181_register_parameter(
        os_tr181_handle_t *handle,
        const char *param_path,
        os_tr181_param_type_t type,
        int writable,
        os_tr181_get_cb_t get_callback,
        os_tr181_set_cb_t set_callback,
        void *user_data)
{
    struct param_entry *param;
    amxd_object_t *obj = NULL;
    amxd_param_t *amx_param = NULL;
    amxd_status_t status;
    char *obj_path, *param_name;
    char *path_copy;
    uint32_t amxc_type;

    if (!handle || !param_path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Check if already registered */
    for (param = handle->parameters; param != NULL; param = param->next)
    {
        if (strcmp(param->path, param_path) == 0)
        {
            return OS_TR181_ERROR;
        }
    }

    /* Parse param_path to extract object path and parameter name */
    path_copy = strdup(param_path);
    if (!path_copy)
    {
        return OS_TR181_ERROR;
    }

    param_name = strrchr(path_copy, '.');
    if (!param_name)
    {
        free(path_copy);
        return OS_TR181_ERROR_INVALID;
    }
    *param_name = '\0';
    param_name++;
    obj_path = path_copy;

    /* Check if this is a table instance parameter (contains {i}) */
    if (strstr(param_path, "{i}"))
    {
        /* This is an instance parameter - find the table object */
        char *bracket = strstr(obj_path, ".{i}");
        if (bracket)
        {
            *bracket = '\0'; /* Truncate at {i} to get table path */

            /* Add trailing dot to table path for lookup */
            char table_path_lookup[512];
            snprintf(table_path_lookup, sizeof(table_path_lookup), "%s.", obj_path);

            /* Find table object */
            for (struct table_entry *te = handle->tables; te != NULL; te = te->next)
            {
                if (strcmp(te->path, table_path_lookup) == 0)
                {
                    obj = te->amx_object;
                    LOGD("Found table object for parameter: %s", table_path_lookup);
                    break;
                }
            }
        }
    }
    else
    {
        /* Regular parameter - find the parent object */
        for (struct object_entry *oe = handle->objects; oe != NULL; oe = oe->next)
        {
            if (strstr(oe->path, obj_path) || strstr(obj_path, oe->path))
            {
                obj = oe->amx_object;
                break;
            }
        }
    }

    if (!obj)
    {
        LOGE("Parent object not found for parameter %s", param_path);
        free(path_copy);
        return OS_TR181_ERROR;
    }

    /* Convert os_tr181 type to amxc type */
    amxc_type = os_tr181_type_to_amxc(type);

    /* Create parameter */
    status = amxd_param_new(&amx_param, param_name, amxc_type);
    if (status != amxd_status_ok || !amx_param)
    {
        LOGE("Failed to create parameter %s: %d", param_name, status);
        free(path_copy);
        return OS_TR181_ERROR;
    }

    /* Set parameter attributes */
    if (!writable)
    {
        amxd_param_set_attr(amx_param, amxd_pattr_read_only, true);
    }

    /* Make sure parameter is public (not private or protected) */
    amxd_param_set_attr(amx_param, amxd_pattr_private, false);
    amxd_param_set_attr(amx_param, amxd_pattr_protected, false);

    /* Add parameter to object */
    status = amxd_object_add_param(obj, amx_param);
    if (status != amxd_status_ok)
    {
        LOGE("Failed to add parameter to object: %d", status);
        amxd_param_delete(&amx_param);
        free(path_copy);
        return OS_TR181_ERROR;
    }

    free(path_copy);

    /* Store parameter info */
    param = malloc(sizeof(struct param_entry));
    if (!param)
    {
        return OS_TR181_ERROR;
    }

    param->path = strdup(param_path);
    param->type = type;
    param->writable = writable;
    param->get_cb = get_callback;
    param->set_cb = set_callback;
    param->user_data = user_data;
    param->next = handle->parameters;
    handle->parameters = param;

    /* Add action callbacks for read on the parameter */
    if (get_callback)
    {
        status = amxd_param_add_action_cb(amx_param, action_param_read, param_read_cb, param);
        if (status != amxd_status_ok)
        {
            LOGE("Failed to add read callback: %d", status);
        }
    }

    /* Add action callback for write if writable */
    if (writable && set_callback)
    {
        status = amxd_param_add_action_cb(amx_param, action_param_write, param_write_cb, param);
        if (status != amxd_status_ok)
        {
            LOGE("Failed to add write callback: %d", status);
        }
    }

    return OS_TR181_SUCCESS;
}

/* Callback for handling instance add requests */
static amxd_status_t table_add_cb(
        amxd_object_t *const object,
        amxd_param_t *const param,
        amxd_action_t reason,
        const amxc_var_t *const args,
        amxc_var_t *const retval,
        void *priv)
{
    struct table_entry *te = (struct table_entry *)priv;
    int instance_num = 0;
    int ret;

    (void)object;
    (void)param;
    (void)reason;
    (void)args;

    if (!te || !te->add_cb)
    {
        return amxd_status_function_not_implemented;
    }

    /* Call user's add callback */
    ret = te->add_cb(te->path, &instance_num, te->user_data);
    if (ret != OS_TR181_SUCCESS)
    {
        return amxd_status_unknown_error;
    }

    /* Return instance number */
    amxc_var_set(uint32_t, retval, (uint32_t)instance_num);

    return amxd_status_ok;
}

/* Callback for handling instance delete requests */
static amxd_status_t table_del_cb(
        amxd_object_t *const object,
        amxd_param_t *const param,
        amxd_action_t reason,
        const amxc_var_t *const args,
        amxc_var_t *const retval,
        void *priv)
{
    struct table_entry *te = (struct table_entry *)priv;
    int instance_num = 0;
    int ret;
    char inst_path[512];

    (void)object;
    (void)param;
    (void)reason;
    (void)retval;

    if (!te || !te->del_cb)
    {
        return amxd_status_function_not_implemented;
    }

    /* Extract instance number from args */
    instance_num = amxc_var_constcast(uint32_t, args);

    /* Build instance path */
    snprintf(inst_path, sizeof(inst_path), "%s%d.", te->path, instance_num);

    /* Call user's delete callback */
    ret = te->del_cb(inst_path, instance_num, te->user_data);
    if (ret != OS_TR181_SUCCESS)
    {
        return amxd_status_unknown_error;
    }

    return amxd_status_ok;
}

os_tr181_error_t os_tr181_register_table(
        os_tr181_handle_t *handle,
        const char *table_path,
        os_tr181_add_cb_t add_callback,
        os_tr181_del_cb_t del_callback,
        void *user_data)
{
    struct table_entry *table;
    amxd_object_t *amx_obj = NULL;
    amxd_object_t *parent_obj = NULL;
    amxd_status_t status;
    char *obj_name;
    char *path_copy;
    char *parent_path;

    if (!handle || !table_path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Check if already registered */
    for (table = handle->tables; table != NULL; table = table->next)
    {
        if (strcmp(table->path, table_path) == 0)
        {
            return OS_TR181_ERROR;
        }
    }

    /* Extract parent path and table name from path */
    path_copy = strdup(table_path);
    if (!path_copy)
    {
        return OS_TR181_ERROR;
    }

    /* Remove trailing dot if present */
    size_t len = strlen(path_copy);
    if (len > 0 && path_copy[len - 1] == '.')
    {
        path_copy[len - 1] = '\0';
    }

    /* Find last component (table name) */
    obj_name = strrchr(path_copy, '.');
    if (obj_name)
    {
        *obj_name = '\0'; /* Terminate parent path */
        obj_name++;       /* Point to table name */
        parent_path = path_copy;
    }
    else
    {
        /* No parent path - shouldn't happen for valid TR-181 paths */
        LOGE("Invalid table path (no parent): %s", table_path);
        free(path_copy);
        return OS_TR181_ERROR_INVALID;
    }

    /* Find the parent object */
    for (struct object_entry *oe = handle->objects; oe != NULL; oe = oe->next)
    {
        /* Match parent path - add trailing dot for comparison */
        char parent_with_dot[512];
        snprintf(parent_with_dot, sizeof(parent_with_dot), "%s.", parent_path);
        if (strcmp(oe->path, parent_with_dot) == 0)
        {
            parent_obj = oe->amx_object;
            LOGD("Found parent object: %s", oe->path);
            break;
        }
    }

    if (!parent_obj)
    {
        LOGE("Parent object not found for table %s (parent: %s)", table_path, parent_path);
        free(path_copy);
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Create multi-instance table object */
    status = amxd_object_new(&amx_obj, amxd_object_template, obj_name);

    if (status != amxd_status_ok || !amx_obj)
    {
        LOGE("Failed to create Ambiorix table object %s: %d", obj_name, status);
        free(path_copy);
        return OS_TR181_ERROR;
    }

    LOGD("Created table object: %s (type=template)", obj_name);

    /* Add table as child of parent object */
    status = amxd_object_add_object(parent_obj, amx_obj);
    if (status != amxd_status_ok)
    {
        LOGE("Failed to add table to parent object: %d", status);
        amxd_object_delete(&amx_obj);
        free(path_copy);
        return OS_TR181_ERROR;
    }

    LOGD("Added table %s to parent object", obj_name);
    free(path_copy);

    /* Store table info */
    table = malloc(sizeof(struct table_entry));
    if (!table)
    {
        return OS_TR181_ERROR;
    }

    table->path = strdup(table_path);
    table->amx_object = amx_obj;
    table->add_cb = add_callback;
    table->del_cb = del_callback;
    table->user_data = user_data;
    table->next = handle->tables;
    handle->tables = table;

    /* NOTE: For now, we let Ambiorix handle add/delete automatically.
     * The callbacks are stored but not currently hooked up.
     * TODO: Implement proper event-based notification when instances are added/deleted
     * by listening to dm:instance-added and dm:instance-removed signals */
    /* void reference to address compiler warning of unused function */
    (void)table_add_cb;
    (void)table_del_cb;

    LOGD("Table registered successfully: %s", table_path);
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_publish_objects(os_tr181_handle_t *handle)
{
    int ret;

    if (!handle || !handle->bus_ctx)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Register the entire data model with the bus */
    ret = amxb_register(handle->bus_ctx, &handle->dm);
    if (ret != 0)
    {
        LOGE("Failed to publish data model to bus: %d", ret);
        return OS_TR181_ERROR;
    }

    LOGD("Data model published to bus successfully");
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_notify_changed(
        os_tr181_handle_t *handle,
        const char *param_path,
        const os_tr181_val_t *old_value,
        const os_tr181_val_t *new_value)
{
    amxd_object_t *obj;
    amxd_param_t *param;
    amxc_var_t event_data;
    char *obj_path = NULL;
    char *param_name = NULL;
    const char *last_dot;

    if (!handle || !param_path || !old_value || !new_value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Split param_path into object path and parameter name */
    last_dot = strrchr(param_path, '.');
    if (!last_dot || last_dot == param_path)
    {
        LOGE("Invalid parameter path: %s", param_path);
        return OS_TR181_ERROR_INVALID;
    }

    /* Extract object path (everything up to and including the last dot) */
    size_t obj_path_len = last_dot - param_path + 1;
    obj_path = malloc(obj_path_len + 1);
    if (!obj_path)
    {
        return OS_TR181_ERROR;
    }
    strncpy(obj_path, param_path, obj_path_len);
    obj_path[obj_path_len] = '\0';

    /* Parameter name is after the last dot */
    param_name = strdup(last_dot + 1);
    if (!param_name)
    {
        free(obj_path);
        return OS_TR181_ERROR;
    }

    LOGD("notify_changed: obj_path='%s', param='%s', type=%s",
         obj_path,
         param_name,
         os_tr181_type_to_string(old_value->type));

    /* Find the object in the data model */
    obj = amxd_dm_findf(&handle->dm, "%s", obj_path);
    if (!obj)
    {
        LOGE("Object not found: %s", obj_path);
        free(obj_path);
        free(param_name);
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Verify parameter exists */
    param = amxd_object_get_param_def(obj, param_name);
    if (!param)
    {
        LOGE("Parameter not found: %s in %s", param_name, obj_path);
        free(obj_path);
        free(param_name);
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Build event data manually with proper structure
     * Format: parameters = { param_name = { from = old_value, to = new_value } }
     * Values must be in their native type (int, string, bool, etc.) */
    amxc_var_init(&event_data);
    amxc_var_set_type(&event_data, AMXC_VAR_ID_HTABLE);

    /* Add 'parameters' hash table */
    amxc_var_t *params = amxc_var_add_new_key(&event_data, "parameters");
    amxc_var_set_type(params, AMXC_VAR_ID_HTABLE);

    /* Add parameter change structure: { from = old, to = new } */
    amxc_var_t *param_change = amxc_var_add_new_key(params, param_name);
    amxc_var_set_type(param_change, AMXC_VAR_ID_HTABLE);

    /* Add "from" value with proper type */
    amxc_var_t *from_var = amxc_var_add_new_key(param_change, "from");
    if (os_tr181_val_to_amx(old_value, from_var) != OS_TR181_SUCCESS)
    {
        LOGE("Failed to convert old value");
        amxc_var_clean(&event_data);
        free(obj_path);
        free(param_name);
        return OS_TR181_ERROR_INVALID;
    }

    /* Add "to" value with proper type */
    amxc_var_t *to_var = amxc_var_add_new_key(param_change, "to");
    if (os_tr181_val_to_amx(new_value, to_var) != OS_TR181_SUCCESS)
    {
        LOGE("Failed to convert new value");
        amxc_var_clean(&event_data);
        free(obj_path);
        free(param_name);
        return OS_TR181_ERROR_INVALID;
    }

    LOGD("Sending change event for %s: %s (type=%s)", obj_path, param_name, os_tr181_type_to_string(new_value->type));
    /*amxc_var_dump_stream(&event_data, stderr);*/

    /* Send the event using low-level signal API with our pre-built structure
     * amxd_object_send_signal will add the base event fields (path, object, etc.) */
    amxd_object_send_signal(obj, "dm:object-changed", &event_data, false);

    amxc_var_clean(&event_data);
    free(obj_path);
    free(param_name);

    return OS_TR181_SUCCESS;
}
os_tr181_error_t os_tr181_process_requests(os_tr181_handle_t *handle, int timeout_ms)
{
    int ret;

    if (!handle || !handle->bus_ctx)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Process Ambiorix bus requests and signals */
    struct timeval tv;
    fd_set readfds;
    int bus_fd = amxb_get_fd(handle->bus_ctx);
    int sig_fd = amxp_signal_fd();
    int max_fd;

    if (bus_fd < 0 || sig_fd < 0)
    {
        return OS_TR181_ERROR;
    }

    max_fd = (bus_fd > sig_fd) ? bus_fd : sig_fd;

    FD_ZERO(&readfds);
    FD_SET(bus_fd, &readfds);
    FD_SET(sig_fd, &readfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(max_fd + 1, &readfds, NULL, NULL, &tv);

    if (ret == 0)
    {
        /* Timeout - no data available */
        return OS_TR181_ERROR_TIMEOUT;
    }
    else if (ret < 0)
    {
        /* Error */
        return OS_TR181_ERROR;
    }

    /* Process bus data */
    if (FD_ISSET(bus_fd, &readfds))
    {
        ret = amxb_read(handle->bus_ctx);
        if (ret != 0)
        {
            LOGD("amxb_read failed: %d", ret);
            return OS_TR181_ERROR;
        }
        LOGD("amxb_read processed data");
    }

    /* Process signal data */
    if (FD_ISSET(sig_fd, &readfds))
    {
        ret = amxp_signal_read();
        if (ret != 0)
        {
            LOGD("amxp_signal_read failed: %d", ret);
            return OS_TR181_ERROR;
        }
        LOGD("amxp_signal_read processed");
    }

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_add_instance(os_tr181_handle_t *handle, const char *object_path, int *instance_number)
{
    amxc_var_t values;
    amxc_var_t ret;
    int rv;
    int result = OS_TR181_ERROR;

    if (!handle || !handle->bus_ctx || !object_path || !instance_number)
    {
        return OS_TR181_ERROR_INVALID;
    }

    amxc_var_init(&values);
    amxc_var_init(&ret);

    /* Set up empty values hash table - let backend assign index and name */
    amxc_var_set_type(&values, AMXC_VAR_ID_HTABLE);

    LOGD("Calling amxb_add for path: %s", object_path);
    rv = amxb_add(handle->bus_ctx, object_path, 0, NULL, &values, &ret, OS_TR181_DEFAULT_TIMEOUT_MS / 1000);
    LOGD("amxb_add returned: %d", rv);

    if (rv == 0)
    {
        /* Result is an array with first element containing the response */
        amxc_var_t *response = amxc_var_get_index(&ret, 0, AMXC_VAR_FLAG_DEFAULT);
        if (response && amxc_var_type_of(response) == AMXC_VAR_ID_HTABLE)
        {
            const char *path_str = GET_CHAR(response, "path");
            LOGD("Response path: %s", path_str ? path_str : "NULL");

            if (path_str)
            {
                /* Parse instance number from path like "Device.WiFi.SSID.3." */
                const char *last_dot = strrchr(path_str, '.');
                if (last_dot && last_dot > path_str)
                {
                    const char *ptr = last_dot - 1;
                    while (ptr > path_str && isdigit(*ptr))
                    {
                        ptr--;
                    }
                    if (*ptr == '.' && ptr < last_dot - 1)
                    {
                        *instance_number = atoi(ptr + 1);
                        LOGD("Extracted instance number: %d", *instance_number);
                        result = OS_TR181_SUCCESS;
                    }
                }
            }
        }
        else
        {
            LOGE("Response is not a hash table or index 0 not found");
        }
    }
    else
    {
        LOGE("amxb_add failed with code: %d", rv);
    }

    amxc_var_clean(&values);
    amxc_var_clean(&ret);
    return result;
}

os_tr181_error_t os_tr181_delete_instance(os_tr181_handle_t *handle, const char *instance_path)
{
    amxc_var_t args;
    int result = OS_TR181_ERROR;

    if (!handle || !handle->bus_ctx || !instance_path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    amxc_var_init(&args);
    amxc_var_set_type(&args, AMXC_VAR_ID_HTABLE);

    /* Call _del function on the instance */
    if (amxb_del(handle->bus_ctx, instance_path, 0, NULL, &args, OS_TR181_DEFAULT_TIMEOUT_MS / 1000) == 0)
    {
        result = OS_TR181_SUCCESS;
    }

    amxc_var_clean(&args);
    return result;
}

os_tr181_error_t os_tr181_get_instances(
        os_tr181_handle_t *handle,
        const char *object_path,
        int **instance_numbers,
        int *count)
{
    amxc_var_t result;
    int *inst_array = NULL;
    int inst_count = 0;
    int ret = OS_TR181_ERROR;

    if (!handle || !handle->bus_ctx || !object_path || !instance_numbers || !count)
    {
        return OS_TR181_ERROR_INVALID;
    }

    *instance_numbers = NULL;
    *count = 0;

    amxc_var_init(&result);

    /* Get all instances under the object path */
    if (amxb_get(handle->bus_ctx, object_path, 0, &result, OS_TR181_DEFAULT_TIMEOUT_MS / 1000) == 0)
    {
        /*amxc_var_dump_stream(&result, stderr);*/
        /* Result is typically a list containing a hash table */
        amxc_var_t *first_level = amxc_var_get_index(&result, 0, AMXC_VAR_FLAG_DEFAULT);
        const amxc_htable_t *htable = first_level ? amxc_var_constcast(amxc_htable_t, first_level) : NULL;

        if (htable)
        {
            amxc_htable_iterate(it, htable)
            {
                const char *key = amxc_htable_it_get_key(it);
                if (key)
                {
                    /* Extract instance number from key */
                    /* Key format: "Device.WiFi.SSID.1." or just "1" */
                    const char *last_dot = strrchr(key, '.');
                    if (last_dot && last_dot > key)
                    {
                        const char *ptr = last_dot - 1;
                        while (ptr > key && isdigit(*ptr))
                        {
                            ptr--;
                        }
                        if (ptr < last_dot - 1)
                        {
                            int instance_num = atoi(ptr + 1);

                            /* Expand array */
                            int *new_array = realloc(inst_array, (inst_count + 1) * sizeof(int));
                            if (!new_array)
                            {
                                free(inst_array);
                                amxc_var_clean(&result);
                                return OS_TR181_ERROR;
                            }
                            inst_array = new_array;
                            inst_array[inst_count++] = instance_num;
                        }
                    }
                }
            }

            /* Success - return instance list (may be empty) */
            *instance_numbers = inst_array;
            *count = inst_count;
            ret = OS_TR181_SUCCESS;
        }
    }

    amxc_var_clean(&result);
    return ret;
}

/*
 * Helper: Check if instance exists
 */
static int check_instance_exists_amx(amxb_bus_ctx_t *ctx, const char *instance_path)
{
    amxc_var_t result;
    int exists = 0;

    if (!ctx || !instance_path)
    {
        return 0;
    }

    amxc_var_init(&result);

    /* Try to get the instance */
    if (amxb_get(ctx, instance_path, 0, &result, OS_TR181_DEFAULT_TIMEOUT_MS / 1000) == 0)
    {
        exists = 1;
    }

    amxc_var_clean(&result);
    return exists;
}

/*
 * Event-based verification for Ambiorix
 */
static int wait_for_instance_event_amx(
        os_tr181_handle_t *handle,
        const char *object_path,
        int instance_number,
        int timeout_ms)
{
    char instance_path[256];
    struct timeval start, now;
    int elapsed_ms = 0;

    snprintf(instance_path, sizeof(instance_path), "%s%d.", object_path, instance_number);

    gettimeofday(&start, NULL);

    /* Poll to check if instance exists */
    while (elapsed_ms < timeout_ms)
    {
        if (check_instance_exists_amx(handle->bus_ctx, instance_path))
        {
            return OS_TR181_SUCCESS;
        }

        /* Process any pending events */
        amxp_signal_read();

        /* Sleep briefly */
        usleep(50000); /* 50ms */

        gettimeofday(&now, NULL);
        elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000;
    }

    return OS_TR181_ERROR_TIMEOUT;
}

os_tr181_error_t os_tr181_add_instance_wait(
        os_tr181_handle_t *handle,
        const char *object_path,
        int *instance_number,
        int timeout_ms)
{
    int result;
    char instance_path[256];

    if (!handle || !handle->bus_ctx || !object_path || !instance_number)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Use timeout_ms if provided, otherwise use default */
    if (timeout_ms <= 0)
    {
        timeout_ms = OS_TR181_DEFAULT_TIMEOUT_MS;
    }

    /* Add the instance using the base function */
    result = os_tr181_add_instance(handle, object_path, instance_number);
    if (result != OS_TR181_SUCCESS)
    {
        return result;
    }

    /* Verify instance exists */
    snprintf(instance_path, sizeof(instance_path), "%s%d.", object_path, *instance_number);

    if (check_instance_exists_amx(handle->bus_ctx, instance_path))
    {
        LOGD("Instance %s exists immediately after add", instance_path);
        return OS_TR181_SUCCESS;
    }

    /* Fall back to event-based wait if not immediately available */
    LOGD("Instance not immediately available, waiting for event");
    result = wait_for_instance_event_amx(handle, object_path, *instance_number, timeout_ms);
    return result;
}

const char *os_tr181_get_backend_name(void)
{
    return "ambiorix";
}
