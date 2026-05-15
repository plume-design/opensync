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
 * os_tr181 - CCSP implementation
 */

#define _GNU_SOURCE /* For asprintf */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>

// debug log options
// #define OVERRIDE_RBUS_LOG
// #define OVERRIDE_RDK_LOG
#ifdef OVERRIDE_RDK_LOG
#define RDK_DEBUG_DEFINE_STRINGS
#include <rdk_debug.h>
#endif

#include <dbus/dbus.h>
#include <rbus/rbus.h>
#include <rtmessage/rtLog.h>
#include <ccsp/ccsp_message_bus.h>
#include <ccsp/ccsp_base_api.h>
#include <ccsp/ccsp_dm_api.h>
#include <ccsp/ccsp_trace.h>

#include "os_tr181.h"
#include "log.h"

#define COMPONENT_NAME    "com.cisco.spvtg.ccsp.tr181lib"
#define CR_COMPONENT_ID   "com.cisco.spvtg.ccsp.CR"
#define COMPONENT_VERSION 1
#define DBUS_PATH         "/com/cisco/spvtg/ccsp/tr181lib"
#define SUBSYSTEM_PREFIX  "eRT."

/* Subscription entry */
struct subscription_entry
{
    os_tr181_handle_t *handle; /* Back-reference to parent handle */
    char *path;
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
    struct object_entry *next;
};

/* Registered table entry */
struct table_entry
{
    char *path;
    os_tr181_add_cb_t add_cb;
    os_tr181_del_cb_t del_cb;
    void *user_data;
    struct table_entry *next;
};

struct os_tr181_handle_s
{
    void *bus_handle;
    struct subscription_entry *subscriptions;
    struct object_entry *objects;
    struct param_entry *parameters;
    struct table_entry *tables;
    int published;    /* Flag to track if objects have been published */
    bool in_callback; /* True when executing user callback functions */
};

/* Type translation helpers */
static os_tr181_param_type_t ccsp_type_to_os_tr181(enum dataType_e ccsp_type)
{
    switch (ccsp_type)
    {
        case ccsp_boolean:
            return OS_TR181_TYPE_BOOL;
        case ccsp_int:
            return OS_TR181_TYPE_INT;
        case ccsp_unsignedInt:
            return OS_TR181_TYPE_UINT;
        case ccsp_long:
            return OS_TR181_TYPE_INT64;
        case ccsp_unsignedLong:
            return OS_TR181_TYPE_UINT64;
        case ccsp_dateTime:
            return OS_TR181_TYPE_DATETIME;
        case ccsp_base64:
            return OS_TR181_TYPE_BASE64;
        case ccsp_string:
        default:
            return OS_TR181_TYPE_STRING;
    }
}

static enum dataType_e os_tr181_type_to_ccsp(os_tr181_param_type_t type)
{
    switch (type)
    {
        case OS_TR181_TYPE_BOOL:
            return ccsp_boolean;
        case OS_TR181_TYPE_INT:
            return ccsp_int;
        case OS_TR181_TYPE_UINT:
            return ccsp_unsignedInt;
        case OS_TR181_TYPE_INT64:
            return ccsp_long;
        case OS_TR181_TYPE_UINT64:
            return ccsp_unsignedLong;
        case OS_TR181_TYPE_DATETIME:
            return ccsp_dateTime;
        case OS_TR181_TYPE_BASE64:
            return ccsp_base64;
        case OS_TR181_TYPE_STRING:
        default:
            return ccsp_string;
    }
}

/* ========================================================================
 * Value Conversion Functions (os_tr181_val_t <-> parameterValStruct_t)
 * ======================================================================== */

/*
 * Convert os_tr181_val_t to CCSP parameterValStruct_t
 * Allocates string representation of value
 * Caller must free param->parameterValue
 */
os_tr181_error_t os_tr181_val_to_ccsp(const os_tr181_val_t *val, parameterValStruct_t *param)
{
    if (!val || !param)
    {
        return OS_TR181_ERROR_INVALID;
    }

    param->parameterName = NULL; /* Caller sets this */
    param->parameterValue = NULL;
    param->type = os_tr181_type_to_ccsp(val->type);

    /* Convert value to string (CCSP always uses strings) */
    char *str = NULL;
    os_tr181_error_t err = os_val_to_str(val, &str);
    if (err != OS_TR181_SUCCESS)
    {
        return err;
    }

    param->parameterValue = str;
    return OS_TR181_SUCCESS;
}

/*
 * Convert CCSP parameterValStruct_t to os_tr181_val_t
 * Parses string value based on type
 */
os_tr181_error_t os_tr181_val_from_ccsp(os_tr181_val_t *val, const parameterValStruct_t *param)
{
    if (!val || !param)
    {
        return OS_TR181_ERROR_INVALID;
    }

    os_tr181_param_type_t type = ccsp_type_to_os_tr181(param->type);
    return os_val_from_str(val, param->parameterValue, type);
}

#ifdef OVERRIDE_RDK_LOG

void rdk_dbg_MsgRaw1(rdk_LogLevel level, const char *module, const char *format, va_list args)
{
    (void)module;
    const char *levelstr = "";
    char msg[256] = "";
    if (level >= 0 && level < ENUM_RDK_LOG_COUNT)
    {
        levelstr = rdk_logLevelStrings[level];
    }
    vsnprintf(msg, sizeof(msg), format, args);
    int len = strlen(msg);
    // trim trailing newlines
    while ((len > 0) && (msg[len - 1] == '\n'))
        msg[--len] = 0;
    LOGT("RDK_%s: %s", levelstr, msg);
}

void rdk_dbg_MsgRaw(rdk_LogLevel level, const char *module, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    rdk_dbg_MsgRaw1(level, module, format, ap);
    va_end(ap);
}

#endif

void os_tr181_rbus_log_handler(rtLogLevel level, const char *file, int line, int threadId, char *message)
{
    (void)file;
    (void)line;
    (void)threadId;
    const char *rlevelstr = rtLogLevelToString(level);
    char *msg = message ?: "";
    int len = strlen(msg);
    // trim trailing newlines
    while ((len > 0) && (msg[len - 1] == '\n'))
        len--;
    LOGT("[%d] RBUS_%s: %.*s", threadId, rlevelstr ?: "", len, msg);
}

/*
 * Helper function to duplicate a string using CCSP allocator
 * Returns allocated string or NULL on failure
 */
static char *bus_strdup(CCSP_MESSAGE_BUS_INFO *bus_info, const char *str)
{
    if (!bus_info || !str)
    {
        return NULL;
    }

    size_t len = strlen(str);
    char *dup = bus_info->mallocfunc(len + 1);
    if (dup)
    {
        strcpy(dup, str);
    }
    return dup;
}

os_tr181_error_t os_tr181_init(os_tr181_handle_t **handle)
{
    os_tr181_handle_t *h = malloc(sizeof(os_tr181_handle_t));
    int ret;
    char *pCfg = CCSP_MSG_BUS_CFG;

    if (!h)
    {
        return OS_TR181_ERROR_INIT;
    }

    h->bus_handle = NULL;
    h->subscriptions = NULL;
    h->objects = NULL;
    h->parameters = NULL;
    h->tables = NULL;
    h->published = 0;
    h->in_callback = false;

    AnscSetTraceLevel(CCSP_TRACE_LEVEL_DEBUG);

    ret = CCSP_Message_Bus_Init(COMPONENT_NAME, pCfg, &h->bus_handle, malloc, free);
    if (ret != 0)
    {
        LOGE("CCSP_Message_Bus_Init failed: %d", ret);
        free(h);
        return OS_TR181_ERROR_INIT;
    }

    /*
    ret = Cdm_Init(&h->bus_handle, NULL, NULL, NULL, COMPONENT_NAME);
    if (ret != CCSP_SUCCESS)
    {
        LOGE("Cdm_Init failed: %d %s", ret, Cdm_StrError(ret));
        return false;
    }
    */
#ifdef OVERRIDE_RBUS_LOG
    rtLogSetLogHandler(&os_tr181_rbus_log_handler);
#endif

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

        if (handle->bus_handle)
        {
            /* Cdm_Term(); */
            CCSP_Message_Bus_Exit(handle->bus_handle);
        }
        free(handle);
    }
}

os_tr181_error_t os_tr181_get_val(os_tr181_handle_t *handle, const char *param_name, os_tr181_val_t *value)
{
    int ret;
    int size = 0;
    parameterValStruct_t **parameterVal = NULL;
    char *parameterNames[1];
    componentStruct_t **components = NULL;
    int component_size = 0;

    if (!handle || !handle->bus_handle || !param_name || !value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Find component owning this parameter */
    ret = CcspBaseIf_discComponentSupportingNamespace(
            handle->bus_handle,
            CR_COMPONENT_ID,
            param_name,
            "",
            &components,
            &component_size);

    if (ret != CCSP_SUCCESS || component_size == 0)
    {
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Get parameter value */
    parameterNames[0] = (char *)param_name;
    ret = CcspBaseIf_getParameterValues(
            handle->bus_handle,
            components[0]->componentName,
            components[0]->dbusPath,
            parameterNames,
            1,
            &size,
            &parameterVal);

    if (ret == CCSP_SUCCESS && size > 0)
    {
        /* Convert CCSP value to os_tr181_val_t */
        os_tr181_error_t err = os_tr181_val_from_ccsp(value, parameterVal[0]);

        free_parameterValStruct_t(handle->bus_handle, size, parameterVal);
        free_componentStruct_t(handle->bus_handle, component_size, components);

        return err;
    }
    else
    {
        LOGD("Get %s failed: %d %s", param_name, ret, Cdm_StrError(ret));
        free_componentStruct_t(handle->bus_handle, component_size, components);
        return OS_TR181_ERROR;
    }
}

os_tr181_error_t os_tr181_set_val(os_tr181_handle_t *handle, const char *param_name, const os_tr181_val_t *value)
{
    int ret;
    parameterValStruct_t param;
    componentStruct_t **components = NULL;
    int component_size = 0;
    char *faultParam = NULL;
    CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)handle;

    if (!handle || !handle->bus_handle || !param_name || !value)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Find component owning this parameter */
    ret = CcspBaseIf_discComponentSupportingNamespace(
            handle->bus_handle,
            CR_COMPONENT_ID,
            param_name,
            "",
            &components,
            &component_size);

    if (ret != CCSP_SUCCESS || component_size == 0)
    {
        LOGD("Set %s: not found", param_name);
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Convert os_tr181_val_t to CCSP format */
    os_tr181_error_t conv_err = os_tr181_val_to_ccsp(value, &param);
    if (conv_err != OS_TR181_SUCCESS)
    {
        free_componentStruct_t(handle->bus_handle, component_size, components);
        return conv_err;
    }

    /* Set parameter name */
    param.parameterName = (char *)param_name;

    /* Set parameter */
    ret = CcspBaseIf_setParameterValues(
            handle->bus_handle,
            components[0]->componentName,
            components[0]->dbusPath,
            0,
            0,
            &param,
            1,
            1,
            &faultParam);
    if (ret != CCSP_SUCCESS)
    {
        LOGD("Set %s failed: %d %s invalid param: %s", param_name, ret, Cdm_StrError(ret), faultParam ?: "");
    }

    /* Clean up */
    free(param.parameterValue);

    if (faultParam)
    {
        bus_info->freefunc(faultParam);
    }

    free_componentStruct_t(handle->bus_handle, component_size, components);

    return (ret == CCSP_SUCCESS) ? OS_TR181_SUCCESS : OS_TR181_ERROR;
}

os_tr181_error_t os_tr181_list(
        os_tr181_handle_t *handle,
        const char *path,
        bool recursive,
        os_tr181_param_info_t **params,
        int *count)
{
    int ret;
    componentStruct_t **components = NULL;
    int component_size = 0;
    parameterInfoStruct_t **parameterInfo = NULL;
    int param_size = 0;
    int i;

    if (!handle || !handle->bus_handle || !path || !params || !count)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Find component owning this namespace */
    ret = CcspBaseIf_discComponentSupportingNamespace(
            handle->bus_handle,
            CR_COMPONENT_ID,
            path,
            "",
            &components,
            &component_size);

    if (ret != CCSP_SUCCESS || component_size == 0)
    {
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Get parameter names - nextlevel: 0=recursive, 1=next level only */
    ret = CcspBaseIf_getParameterNames(
            handle->bus_handle,
            components[0]->componentName,
            components[0]->dbusPath,
            (char *)path,
            recursive ? 0 : 1,
            &param_size,
            &parameterInfo);

    if (ret == CCSP_SUCCESS && param_size > 0)
    {
        os_tr181_param_info_t *list = malloc(sizeof(os_tr181_param_info_t) * param_size);
        if (list)
        {
            /* First pass: determine types using CcspBaseIf_getObjType for objects/instances */
            for (i = 0; i < param_size; i++)
            {
                char buf[CCSP_BASE_PARAM_LENGTH];
                int inst_num = 0;
                int obj_type = CcspBaseIf_getObjType((char *)path, parameterInfo[i]->parameterName, &inst_num, buf);

                list[i].name = strdup(parameterInfo[i]->parameterName);
                if (!list[i].name)
                {
                    /* Clean up previously allocated names */
                    for (int j = 0; j < i; j++)
                    {
                        free(list[j].name);
                    }
                    free(list);
                    free_parameterInfoStruct_t(handle->bus_handle, param_size, parameterInfo);
                    return OS_TR181_ERROR;
                }
                list[i].writable = parameterInfo[i]->writable;

                /* Map CCSP object type to our type */
                if (obj_type == CCSP_BASE_INSTANCE)
                {
                    list[i].type = OS_TR181_TYPE_INSTANCE;
                }
                else if (obj_type == CCSP_BASE_OBJECT)
                {
                    list[i].type = OS_TR181_TYPE_OBJECT;
                }
                else
                {
                    /* It's a parameter - will get actual type from getParameterValues */
                    list[i].type = OS_TR181_TYPE_STRING; /* Default, will update below */
                }
            }

            /* Second pass: get actual parameter types for non-objects */
            /* Build list of parameter names (exclude objects/instances) */
            char **param_names = malloc(sizeof(char *) * param_size);
            int *param_indices = malloc(sizeof(int) * param_size);
            int param_count = 0;

            if (!param_names || !param_indices)
            {
                /* Clean up on allocation failure */
                free(param_names);
                free(param_indices);
                for (i = 0; i < param_size; i++)
                {
                    free(list[i].name);
                }
                free(list);
                free_parameterInfoStruct_t(handle->bus_handle, param_size, parameterInfo);
                return OS_TR181_ERROR;
            }

            if (param_names && param_indices)
            {
                for (i = 0; i < param_size; i++)
                {
                    /* Only query values for actual parameters, not objects/instances */
                    if (list[i].type == OS_TR181_TYPE_STRING)
                    {
                        param_names[param_count] = parameterInfo[i]->parameterName;
                        param_indices[param_count] = i;
                        param_count++;
                    }
                }

                if (param_count > 0)
                {
                    /* Get parameter values to extract type information */
                    parameterValStruct_t **parameterVal = NULL;
                    int val_size = 0;
                    int type_ret = CcspBaseIf_getParameterValues(
                            handle->bus_handle,
                            components[0]->componentName,
                            components[0]->dbusPath,
                            param_names,
                            param_count,
                            &val_size,
                            &parameterVal);

                    if (type_ret == CCSP_SUCCESS && parameterVal)
                    {
                        for (i = 0; i < val_size && i < param_count; i++)
                        {
                            int list_idx = param_indices[i];
                            list[list_idx].type = ccsp_type_to_os_tr181(parameterVal[i]->type);
                        }
                        free_parameterValStruct_t(handle->bus_handle, val_size, parameterVal);
                    }
                }

                free(param_names);
                free(param_indices);
            }

            *params = list;
            *count = param_size;
        }
        else
        {
            ret = OS_TR181_ERROR;
        }

        free_parameterInfoStruct_t(handle->bus_handle, param_size, parameterInfo);
    }
    else
    {
        *params = NULL;
        *count = 0;
        ret = OS_TR181_ERROR;
    }

    free_componentStruct_t(handle->bus_handle, component_size, components);

    return (ret == CCSP_SUCCESS) ? OS_TR181_SUCCESS : ret;
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

/* Convert rbusValue_t to os_tr181_val_t */
static os_tr181_error_t rbus_value_to_val(rbusValue_t value, os_tr181_val_t *out_val)
{
    rbusValueType_t value_type;

    if (!value || !out_val)
    {
        return OS_TR181_ERROR_INVALID;
    }

    value_type = rbusValue_GetType(value);

    /* Convert rbus type to os_tr181_val_t */
    switch (value_type)
    {
        case RBUS_BOOLEAN:
            *out_val = OS_VAL_BOOL(rbusValue_GetBoolean(value));
            break;

        case RBUS_INT32:
            *out_val = OS_VAL_INT(rbusValue_GetInt32(value));
            break;

        case RBUS_UINT32:
            *out_val = OS_VAL_UINT(rbusValue_GetUInt32(value));
            break;

        case RBUS_INT64:
            *out_val = OS_VAL_INT64(rbusValue_GetInt64(value));
            break;

        case RBUS_UINT64:
            *out_val = OS_VAL_UINT64(rbusValue_GetUInt64(value));
            break;

        case RBUS_SINGLE:
        case RBUS_DOUBLE:
            *out_val = OS_VAL_DOUBLE(rbusValue_GetDouble(value));
            break;

        case RBUS_STRING: {
            const char *str = rbusValue_GetString(value, NULL);
            return os_val_set_str_dup(out_val, str);
        }

        case RBUS_DATETIME: {
            const char *str = rbusValue_GetString(value, NULL);
            os_tr181_val_t temp = OS_VAL_INIT();
            os_tr181_error_t err = os_val_from_str(&temp, str, OS_TR181_TYPE_DATETIME);
            if (err == OS_TR181_SUCCESS)
            {
                *out_val = temp;
            }
            return err;
        }

        case RBUS_BYTES: {
            const char *str = rbusValue_GetString(value, NULL);
            return os_val_set_str_dup(out_val, str); /* Store as string for now */
        }

        default: {
            /* Fallback: convert to string representation */
            char *str = rbusValue_ToString(value, NULL, 0);
            os_tr181_error_t err = os_val_set_str_dup(out_val, str ? str : "");
            free(str);
            return err;
        }
    }

    return OS_TR181_SUCCESS;
}

/* Forward declaration */
static void rbus_event_handler(
        rbusHandle_t rbus_handle,
        rbusEvent_t const *event,
        rbusEventSubscription_t *subscription);

/* Cleanup subscriptions marked for removal during notification callback dispatch */
static void cleanup_marked_subscriptions(os_tr181_handle_t *handle)
{
    struct subscription_entry **sub_ptr;
    struct subscription_entry *sub;
    CCSP_MESSAGE_BUS_INFO *bus_info;

    if (!handle || !handle->bus_handle)
    {
        return;
    }

    bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;

    sub_ptr = &handle->subscriptions;
    while (*sub_ptr)
    {
        sub = *sub_ptr;
        if (sub->marked_for_removal)
        {
            /* Unsubscribe via RBUS */
            if (bus_info->rbus_handle)
            {
                rbusEventSubscription_t subscription;
                memset(&subscription, 0, sizeof(subscription));
                subscription.eventName = sub->path;
                subscription.handler = (void *)rbus_event_handler;
                subscription.userData = sub;

                rbusEvent_UnsubscribeEx(bus_info->rbus_handle, &subscription, 1);
            }

            LOGD("Cleanup: unsubscribed from %s", sub->path);

            /* Remove from list and free */
            *sub_ptr = sub->next;
            free(sub->path);
            free(sub);
        }
        else
        {
            sub_ptr = &sub->next;
        }
    }
}

/* RBUS event callback handler */
static void rbus_event_handler(
        rbusHandle_t rbus_handle,
        rbusEvent_t const *event,
        rbusEventSubscription_t *subscription)
{
    struct subscription_entry *sub = (struct subscription_entry *)subscription->userData;
    os_tr181_handle_t *handle;
    const char *param_name;
    os_tr181_val_t val = OS_VAL_INIT();

    (void)rbus_handle; /* unused */

    if (!sub || !sub->handle || !event)
    {
        return;
    }

    handle = sub->handle;
    param_name = event->name;

    /* Extract value from event data (rbusObject_t) */
    if (event->data)
    {
        rbusValue_t value = rbusObject_GetValue(event->data, "value");
        if (value)
        {
            /* Convert RBUS value to os_tr181_val_t */
            if (rbus_value_to_val(value, &val) != OS_TR181_SUCCESS)
            {
                LOGD("Failed to convert RBUS value for %s", param_name);
            }
        }
    }

    if (val.type == OS_TR181_TYPE_NONE)
    {
        LOGD("Failed to extract value from event for %s", param_name);
        return;
    }

    /* Skip if this subscription is marked for removal */
    if (sub->marked_for_removal)
    {
        os_val_free(&val);
        return;
    }

    /* Check if this subscription matches this specific parameter event */
    size_t sub_len = strlen(sub->path);
    bool should_notify = false;

    /* Check for exact match or wildcard match */
    if (strcmp(sub->path, param_name) == 0
        || (sub->path[sub_len - 1] == '.' && strncmp(sub->path, param_name, sub_len) == 0))
    {
        should_notify = true;
    }

    if (should_notify)
    {
        /* Mark that we're executing user callback */
        handle->in_callback = true;

        /* Dispatch to this subscription's callback */
        sub->callback(param_name, &val, sub->user_data);

        /* Clear callback flag */
        handle->in_callback = false;

        /* Clean up any subscriptions that were marked during callback */
        cleanup_marked_subscriptions(handle);
    }

    os_val_free(&val);
}

os_tr181_error_t os_tr181_subscribe(
        os_tr181_handle_t *handle,
        const char *path,
        os_tr181_event_cb_t callback,
        void *user_data,
        os_tr181_sub_handle_t *sub_handle)
{
    struct subscription_entry *sub;
    CCSP_MESSAGE_BUS_INFO *bus_info;
    rbusEventSubscription_t subscription;
    int ret;

    if (!handle || !handle->bus_handle || !path || !callback || !sub_handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;

    if (!bus_info->rbus_handle)
    {
        LOGE("RBUS handle not initialized");
        return OS_TR181_ERROR_INIT;
    }

    /* Create new subscription entry */
    sub = malloc(sizeof(struct subscription_entry));
    if (!sub)
    {
        return OS_TR181_ERROR;
    }

    sub->path = strdup(path);
    sub->handle = handle;
    sub->callback = callback;
    sub->user_data = user_data;
    sub->marked_for_removal = false;
    sub->next = handle->subscriptions;
    handle->subscriptions = sub;

    /* Subscribe via RBUS */
    memset(&subscription, 0, sizeof(subscription));
    subscription.eventName = path;
    subscription.filter = NULL;
    subscription.interval = 0;
    subscription.duration = 0;
    subscription.handler = (void *)rbus_event_handler;
    subscription.userData = sub;
    subscription.publishOnSubscribe = 0;

    ret = rbusEvent_SubscribeEx(bus_info->rbus_handle, &subscription, 1, 0);

    if (ret != RBUS_ERROR_SUCCESS)
    {
        LOGE("rbusEvent_SubscribeEx failed for %s: error %d", path, ret);
        /* Remove from list on failure */
        handle->subscriptions = sub->next;
        free(sub->path);
        free(sub);
        return OS_TR181_ERROR;
    }

    LOGD("Subscribed to %s via RBUS", path);
    *sub_handle = (os_tr181_sub_handle_t)sub;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_unsubscribe(os_tr181_handle_t *handle, os_tr181_sub_handle_t sub_handle)
{
    struct subscription_entry **sub_ptr;
    struct subscription_entry *sub;
    CCSP_MESSAGE_BUS_INFO *bus_info;
    rbusEventSubscription_t subscription;
    int ret;

    if (!handle || !handle->bus_handle || !sub_handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;
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
            /* Unsubscribe via RBUS */
            if (bus_info->rbus_handle)
            {
                memset(&subscription, 0, sizeof(subscription));
                subscription.eventName = sub->path;
                subscription.handler = (void *)rbus_event_handler;
                subscription.userData = sub;

                ret = rbusEvent_UnsubscribeEx(bus_info->rbus_handle, &subscription, 1);

                if (ret != RBUS_ERROR_SUCCESS)
                {
                    LOGE("rbusEvent_UnsubscribeEx failed for %s: error %d", sub->path, ret);
                }
            }

            *sub_ptr = sub->next;
            LOGD("Unsubscribed from %s", sub->path);
            free(sub->path);
            free(sub);
            return OS_TR181_SUCCESS;
        }
        sub_ptr = &(*sub_ptr)->next;
    }

    return OS_TR181_ERROR_NOT_FOUND;
}

os_tr181_error_t os_tr181_wait_event(os_tr181_handle_t *handle, int timeout_ms)
{
    if (!handle || !handle->bus_handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* RBUS handles event dispatch internally via its own threading.
     * Events are delivered asynchronously via the registered handler.
     * This function provides a simple blocking wait for compatibility.
     */

    if (timeout_ms < 0)
    {
        /* Infinite wait - sleep in loop */
        while (1)
        {
            sleep(1);
        }
    }
    else if (timeout_ms > 0)
    {
        /* Sleep for specified timeout */
        usleep(timeout_ms * 1000);
        return OS_TR181_ERROR_TIMEOUT;
    }

    /* timeout_ms == 0: non-blocking, return immediately */
    return OS_TR181_SUCCESS;
}

/* DBUS message handler for incoming requests */
static DBusHandlerResult dbus_message_handler(DBusConnection *conn, DBusMessage *message, void *user_data)
{
    os_tr181_handle_t *handle = (os_tr181_handle_t *)user_data;
    const char *interface;
    const char *method;
    const char *path;

    (void)conn;

    if (!handle)
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    interface = dbus_message_get_interface(message);
    method = dbus_message_get_member(message);
    path = dbus_message_get_path(message);

    if (!interface || !method)
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    LOGD("Received DBUS message: interface=%s method=%s path=%s", interface, method, path ? path : "(null)");

    /* Handle CCSP base interface methods */
    if (strcmp(interface, "com.cisco.spvtg.ccsp.baseInterface") == 0)
    {
        if (strcmp(method, "getParameterValues") == 0)
        {
            /* Handle get parameter request */
            /* This would extract parameter names from message and call get callbacks */
            LOGD("getParameterValues request received");
            /* For now, let CCSP framework handle it through Cdm */
        }
        else if (strcmp(method, "setParameterValues") == 0)
        {
            /* Handle set parameter request */
            LOGD("setParameterValues request received");
            /* For now, let CCSP framework handle it through Cdm */
        }
    }

    /* Let CCSP framework handle the message */
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

os_tr181_error_t os_tr181_register_object(os_tr181_handle_t *handle, const char *object_path)
{
    struct object_entry *obj;

    if (!handle || !object_path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    for (obj = handle->objects; obj != NULL; obj = obj->next)
    {
        if (strcmp(obj->path, object_path) == 0)
        {
            return OS_TR181_ERROR;
        }
    }

    obj = malloc(sizeof(struct object_entry));
    if (!obj)
    {
        return OS_TR181_ERROR;
    }

    obj->path = strdup(object_path);
    obj->next = handle->objects;
    handle->objects = obj;

    /* Register with CCSP Component Registrar */
    /* This would use CcspBaseIf_registerCapabilities */

    return OS_TR181_SUCCESS;
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

    if (!handle || !param_path || !get_callback)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (writable && !set_callback)
    {
        return OS_TR181_ERROR_INVALID;
    }

    for (param = handle->parameters; param != NULL; param = param->next)
    {
        if (strcmp(param->path, param_path) == 0)
        {
            return OS_TR181_ERROR;
        }
    }

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

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_register_table(
        os_tr181_handle_t *handle,
        const char *table_path,
        os_tr181_add_cb_t add_callback,
        os_tr181_del_cb_t del_callback,
        void *user_data)
{
    struct table_entry *table;

    if (!handle || !table_path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    for (table = handle->tables; table != NULL; table = table->next)
    {
        if (strcmp(table->path, table_path) == 0)
        {
            return OS_TR181_ERROR;
        }
    }

    table = malloc(sizeof(struct table_entry));
    if (!table)
    {
        return OS_TR181_ERROR;
    }

    table->path = strdup(table_path);
    table->add_cb = add_callback;
    table->del_cb = del_callback;
    table->user_data = user_data;
    table->next = handle->tables;
    handle->tables = table;

    return OS_TR181_SUCCESS;
}

/* CCSP Base Interface Callback Implementations */
static int ccsp_getParameterValues_cb(
        unsigned int writeID,
        char *parameterNames[],
        int size,
        int *val_size,
        parameterValStruct_t ***val,
        void *user_data)
{
    os_tr181_handle_t *handle = (os_tr181_handle_t *)user_data;
    CCSP_MESSAGE_BUS_INFO *bus_info;
    parameterValStruct_t **result_array = NULL;
    int result_count = 0;
    int i;

    (void)writeID;

    LOGD("%s: size=%d", __func__, size);

    if (!handle || !handle->bus_handle || !parameterNames || size <= 0)
    {
        LOGE("Invalid parameters");
        return CCSP_ERR_INVALID_ARGUMENTS;
    }

    bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;

    /* Allocate result array */
    result_array = (parameterValStruct_t **)bus_info->mallocfunc(sizeof(parameterValStruct_t *) * size);
    if (!result_array)
    {
        LOGE("Failed to allocate result array");
        return CCSP_ERR_MEMORY_ALLOC_FAIL;
    }

    /* Process each requested parameter */
    for (i = 0; i < size; i++)
    {
        struct param_entry *param;
        char *param_name = parameterNames[i];
        int found = 0;

        LOGD("  Requested parameter: %s", param_name);

        /* Search for this parameter in our registered list */
        for (param = handle->parameters; param != NULL; param = param->next)
        {
            if (strcmp(param->path, param_name) == 0)
            {
                found = 1;

                /* Call user's get callback first */
                if (param->get_cb)
                {
                    os_tr181_val_t val = OS_VAL_INIT();
                    int ret = param->get_cb(param_name, &val, param->user_data);
                    if (ret == 0)
                    {
                        /* Convert value to CCSP format */
                        parameterValStruct_t temp_param;
                        if (os_tr181_val_to_ccsp(&val, &temp_param) == OS_TR181_SUCCESS)
                        {
                            /* Success - allocate and populate result structure */
                            result_array[result_count] =
                                    (parameterValStruct_t *)bus_info->mallocfunc(sizeof(parameterValStruct_t));
                            if (result_array[result_count])
                            {
                                /* Set parameter name */
                                result_array[result_count]->parameterName = bus_strdup(bus_info, param_name);

                                /* Set parameter value */
                                result_array[result_count]->parameterValue =
                                        bus_strdup(bus_info, temp_param.parameterValue);
                                result_array[result_count]->type = temp_param.type;
                                result_count++;
                                LOGD("  Got value: %s = %s", param_name, temp_param.parameterValue);
                            }
                            else
                            {
                                LOGE("Failed to allocate parameterValStruct_t for %s", param_name);
                            }
                            free(temp_param.parameterValue);
                        }
                        else
                        {
                            LOGE("  Value conversion failed for %s", param_name);
                        }
                        os_val_free(&val);
                    }
                    else
                    {
                        LOGE("  Get callback failed for %s", param_name);
                        os_val_free(&val);
                    }
                }
                else
                {
                    LOGE("  No get callback for %s", param_name);
                }
                break;
            }
        }

        if (!found)
        {
            LOGE("  Parameter not found: %s", param_name);
        }
    }

    /* Return results */
    *val = result_array;
    *val_size = result_count;

    LOGD("%s: Returning %d values", __func__, result_count);

    if (result_count == 0)
    {
        return CCSP_ERR_UNSUPPORTED_PROTOCOL;
    }

    return CCSP_SUCCESS;
}

static int ccsp_setParameterValues_cb(
        int sessionId,
        unsigned int writeID,
        parameterValStruct_t *val,
        int size,
        dbus_bool commit,
        char **invalidParameterName,
        void *user_data)
{
    os_tr181_handle_t *handle = (os_tr181_handle_t *)user_data;
    int i;
    int success_count = 0;

    (void)sessionId;
    (void)writeID;
    (void)commit;

    LOGD("%s: size=%d", __func__, size);

    if (!handle || !val || size <= 0)
    {
        LOGE("Invalid parameters");
        return CCSP_ERR_INVALID_ARGUMENTS;
    }

    /* Process each parameter value */
    for (i = 0; i < size; i++)
    {
        struct param_entry *param;
        char *param_name = val[i].parameterName;
        char *param_value = val[i].parameterValue;
        int found = 0;

        LOGD("  Set parameter: %s = %s", param_name, param_value);

        /* Search for this parameter in our registered list */
        for (param = handle->parameters; param != NULL; param = param->next)
        {
            if (strcmp(param->path, param_name) == 0)
            {
                found = 1;

                /* Check if parameter is writable */
                if (!param->writable)
                {
                    LOGE("  Parameter %s is read-only", param_name);
                    if (invalidParameterName)
                    {
                        *invalidParameterName = param_name;
                    }
                    return CCSP_ERR_NOT_WRITABLE;
                }

                /* Call user's set callback */
                if (param->set_cb)
                {
                    /* Convert CCSP value to os_tr181_val_t */
                    os_tr181_val_t os_val = OS_VAL_INIT();
                    if (os_tr181_val_from_ccsp(&os_val, &val[i]) == OS_TR181_SUCCESS)
                    {
                        int ret = param->set_cb(param_name, &os_val, param->user_data);
                        if (ret == 0)
                        {
                            success_count++;
                            LOGD("  Successfully set %s", param_name);
                        }
                        else
                        {
                            LOGE("  Set callback failed for %s", param_name);
                            if (invalidParameterName)
                            {
                                *invalidParameterName = param_name;
                            }
                            os_val_free(&os_val);
                            return CCSP_ERR_INVALID_PARAMETER_VALUE;
                        }
                        os_val_free(&os_val);
                    }
                    else
                    {
                        LOGE("  Value conversion failed for %s", param_name);
                        os_val_free(&os_val);
                        if (invalidParameterName)
                        {
                            *invalidParameterName = param_name;
                        }
                        return CCSP_ERR_INVALID_PARAMETER_VALUE;
                    }
                }
                else
                {
                    LOGE("  No set callback for %s", param_name);
                    if (invalidParameterName)
                    {
                        *invalidParameterName = param_name;
                    }
                    return CCSP_ERR_INTERNAL_ERROR;
                }
                break;
            }
        }

        if (!found)
        {
            LOGE("  Parameter not found: %s", param_name);
            if (invalidParameterName)
            {
                *invalidParameterName = param_name;
            }
            return CCSP_ERR_INVALID_PARAMETER_NAME;
        }
    }

    LOGD("%s: Successfully set %d parameters", __func__, success_count);

    return CCSP_SUCCESS;
}

static int ccsp_setCommit_cb(int sessionId, unsigned int writeID, dbus_bool commit, void *user_data)
{
    (void)sessionId;
    (void)writeID;
    (void)commit;
    (void)user_data;
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static int ccsp_setParameterAttributes_cb(int sessionId, parameterAttributeStruct_t *val, int size, void *user_data)
{
    (void)sessionId;
    (void)val;
    (void)size;
    (void)user_data;
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static int ccsp_getParameterAttributes_cb(
        char *parameterNames[],
        int size,
        int *val_size,
        parameterAttributeStruct_t ***val,
        void *user_data)
{
    (void)parameterNames;
    (void)size;
    (void)val_size;
    (void)val;
    (void)user_data;
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static int ccsp_AddTblRow_cb(int sessionId, char *objectName, int *instanceNumber, void *user_data)
{
    (void)sessionId;
    (void)objectName;
    (void)instanceNumber;
    (void)user_data;
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static int ccsp_DeleteTblRow_cb(int sessionId, char *objectName, void *user_data)
{
    (void)sessionId;
    (void)objectName;
    (void)user_data;
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static int ccsp_getParameterNames_cb(
        char *parameterName,
        dbus_bool nextLevel,
        int *size,
        parameterInfoStruct_t ***val,
        void *user_data)
{
    os_tr181_handle_t *handle = (os_tr181_handle_t *)user_data;
    CCSP_MESSAGE_BUS_INFO *bus_info;
    parameterInfoStruct_t **result_array = NULL;
    struct param_entry *param;
    int result_count = 0;
    int param_name_len;
    int max_results = 100;      /* Reasonable limit */
    char **unique_paths = NULL; /* For tracking unique intermediate paths when nextLevel=true */

    LOGD("%s: parameterName=%s, nextLevel=%d", __func__, parameterName, nextLevel);

    if (!handle || !handle->bus_handle || !parameterName || !size || !val)
    {
        LOGE("Invalid parameters");
        return CCSP_ERR_INVALID_ARGUMENTS;
    }

    bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;
    param_name_len = strlen(parameterName);

    /* Allocate result array */
    result_array = (parameterInfoStruct_t **)bus_info->mallocfunc(sizeof(parameterInfoStruct_t *) * max_results);
    if (!result_array)
    {
        LOGE("Failed to allocate result array");
        return CCSP_ERR_MEMORY_ALLOC_FAIL;
    }

    /* Allocate array to track unique paths for nextLevel queries */
    if (nextLevel)
    {
        unique_paths = (char **)malloc(sizeof(char *) * max_results);
        if (!unique_paths)
        {
            bus_info->freefunc(result_array);
            return CCSP_ERR_MEMORY_ALLOC_FAIL;
        }
        memset(unique_paths, 0, sizeof(char *) * max_results);
    }

    /* Iterate through registered parameters */
    for (param = handle->parameters; param != NULL && result_count < max_results; param = param->next)
    {
        int match = 0;
        char intermediate_path[512];

        /* Check if this parameter matches the query */
        if (param_name_len == 0)
        {
            /* Empty string matches all */
            match = 1;
        }
        else if (strncmp(param->path, parameterName, param_name_len) == 0)
        {
            /* Parameter path starts with requested name */
            if (nextLevel)
            {
                /* For nextLevel, extract the immediate child path */
                const char *remainder = param->path + param_name_len;
                char *next_dot = strchr(remainder, '.');

                if (next_dot)
                {
                    /* Found a dot - extract intermediate path */
                    int intermediate_len = (next_dot - param->path) + 1; /* Include the dot */
                    if (intermediate_len < (int)sizeof(intermediate_path))
                    {
                        strncpy(intermediate_path, param->path, intermediate_len);
                        intermediate_path[intermediate_len] = '\0';

                        /* Check if we've already added this intermediate path */
                        int already_added = 0;
                        int i;
                        for (i = 0; i < result_count; i++)
                        {
                            if (unique_paths[i] && strcmp(unique_paths[i], intermediate_path) == 0)
                            {
                                already_added = 1;
                                break;
                            }
                        }

                        if (!already_added)
                        {
                            /* This is a new unique intermediate path */
                            unique_paths[result_count] = strdup(intermediate_path);
                            if (!unique_paths[result_count])
                            {
                                /* Clean up on allocation failure */
                                for (int j = 0; j < result_count; j++)
                                {
                                    if (result_array[j])
                                    {
                                        if (result_array[j]->parameterName)
                                            bus_info->freefunc(result_array[j]->parameterName);
                                        bus_info->freefunc(result_array[j]);
                                    }
                                    if (unique_paths[j]) free(unique_paths[j]);
                                }
                                bus_info->freefunc(result_array);
                                free(unique_paths);
                                return CCSP_ERR_MEMORY_ALLOC_FAIL;
                            }
                            match = 1;
                        }
                    }
                }
                else
                {
                    /* No more dots - this is a leaf parameter at this level */
                    strncpy(intermediate_path, param->path, sizeof(intermediate_path) - 1);
                    intermediate_path[sizeof(intermediate_path) - 1] = '\0';
                    match = 1;
                }
            }
            else
            {
                /* Not nextLevel - return all matching parameters */
                strncpy(intermediate_path, param->path, sizeof(intermediate_path) - 1);
                intermediate_path[sizeof(intermediate_path) - 1] = '\0';
                match = 1;
            }
        }

        if (match)
        {
            /* Allocate result structure */
            result_array[result_count] = (parameterInfoStruct_t *)bus_info->mallocfunc(sizeof(parameterInfoStruct_t));
            if (!result_array[result_count])
            {
                LOGE("Failed to allocate parameterInfoStruct_t");
                continue;
            }

            /* Set parameter name */
            const char *name_to_use = nextLevel ? intermediate_path : param->path;
            result_array[result_count]->parameterName = bus_strdup(bus_info, name_to_use);

            /* Set writable flag - for intermediate paths (ending with .), mark as read-only */
            if (name_to_use[strlen(name_to_use) - 1] == '.')
            {
                result_array[result_count]->writable = 0; /* Objects are read-only */
            }
            else
            {
                result_array[result_count]->writable = param->writable;
            }

            result_count++;
            LOGD("  Found: %s (writable=%d)", name_to_use, result_array[result_count - 1]->writable);
        }
    }

    /* Clean up unique paths tracking */
    if (unique_paths)
    {
        int i;
        for (i = 0; i < result_count; i++)
        {
            if (unique_paths[i])
            {
                free(unique_paths[i]);
            }
        }
        free(unique_paths);
    }

    /* Return results */
    *val = result_array;
    *size = result_count;

    LOGD("%s: Returning %d parameter names", __func__, result_count);

    return CCSP_SUCCESS;
}

static void ccsp_currentSessionIDSignal_cb(int priority, int sessionID, void *user_data)
{
    (void)priority;
    (void)sessionID;
    (void)user_data;
    LOGD("%s", __func__);
}

static int ccsp_initialize_cb(void *user_data)
{
    (void)user_data;
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static int ccsp_finalize_cb(void *user_data)
{
    (void)user_data;
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static int ccsp_freeResources_cb(int priority, void *user_data)
{
    (void)priority;
    (void)user_data;
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static int ccsp_busCheck_cb(void *user_data)
{
    (void)user_data;
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static void ccsp_componentDie_cb(char *Name, int start_die, void *user_data)
{
    (void)Name;
    (void)start_die;
    (void)user_data;
    LOGD("%s", __func__);
}

static int ccsp_getHealth_cb(void)
{
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static int ccsp_restartBootstrap_cb(void *user_data)
{
    (void)user_data;
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static void ccsp_parameterValueChangeSignal_cb(parameterSigStruct_t *val, int size, void *user_data)
{
    (void)val;
    (void)size;
    (void)user_data;
    LOGD("%s", __func__);
}

static void ccsp_deviceProfileChangeSignal_cb(
        char *component_name,
        char *component_dbus_path,
        dbus_bool isAvailable,
        void *user_data)
{
    (void)component_name;
    (void)component_dbus_path;
    (void)isAvailable;
    (void)user_data;
    LOGD("%s", __func__);
}

static void ccsp_diagCompleteSignal_cb(void *user_data)
{
    (void)user_data;
    LOGD("%s", __func__);
}

static void ccsp_systemReadySignal_cb(void *user_data)
{
    (void)user_data;
    LOGD("%s", __func__);
}

static void ccsp_systemRebootSignal_cb(void *user_data)
{
    (void)user_data;
    LOGD("%s", __func__);
}

static void ccsp_webconfigSignal_cb(char *webconfig_data, void *user_data)
{
    (void)webconfig_data;
    (void)user_data;
    LOGD("%s", __func__);
}

static void ccsp_registerCaps_cb(char *component_name, void *user_data)
{
    (void)component_name;
    (void)user_data;
    LOGD("%s", __func__);
}

static int ccsp_isSystemReady_cb(void)
{
    LOGD("%s", __func__);
    return CCSP_SUCCESS;
}

static void ccsp_multiCompBroadCastSignal_cb(char *data, void *user_data)
{
    (void)data;
    (void)user_data;
    LOGD("%s", __func__);
}

static void ccsp_multiCompMasterProcessSignal_cb(char *data, void *user_data)
{
    (void)data;
    (void)user_data;
    LOGD("%s", __func__);
}

static void ccsp_multiCompSlaveProcessSignal_cb(char *data, void *user_data)
{
    (void)data;
    (void)user_data;
    LOGD("%s", __func__);
}

static void ccsp_TunnelStatus_cb(char *data, void *user_data)
{
    (void)data;
    (void)user_data;
    LOGD("%s", __func__);
}

static void ccsp_WifiDbStatus_cb(char *data, void *user_data)
{
    (void)data;
    (void)user_data;
    LOGD("%s", __func__);
}

void os_tr181_fill_ccsp_func_cb(os_tr181_handle_t *handle, CCSP_Base_Func_CB *cb)
{
    cb->getParameterValues = ccsp_getParameterValues_cb;
    cb->getParameterValues_data = handle;
    cb->setParameterValues = ccsp_setParameterValues_cb;
    cb->setParameterValues_data = handle;
    cb->setCommit = ccsp_setCommit_cb;
    cb->setCommit_data = handle;
    cb->setParameterAttributes = ccsp_setParameterAttributes_cb;
    cb->setParameterAttributes_data = handle;
    cb->getParameterAttributes = ccsp_getParameterAttributes_cb;
    cb->getParameterAttributes_data = handle;
    cb->AddTblRow = ccsp_AddTblRow_cb;
    cb->AddTblRow_data = handle;
    cb->DeleteTblRow = ccsp_DeleteTblRow_cb;
    cb->DeleteTblRow_data = handle;
    cb->getParameterNames = ccsp_getParameterNames_cb;
    cb->getParameterNames_data = handle;
    cb->currentSessionIDSignal = ccsp_currentSessionIDSignal_cb;
    cb->currentSessionIDSignal_data = handle;
    cb->initialize = ccsp_initialize_cb;
    cb->initialize_data = handle;
    cb->finalize = ccsp_finalize_cb;
    cb->finalize_data = handle;
    cb->freeResources = ccsp_freeResources_cb;
    cb->freeResources_data = handle;
    cb->busCheck = ccsp_busCheck_cb;
    cb->busCheck_data = handle;
    cb->componentDie = ccsp_componentDie_cb;
    cb->componentDie_data = handle;
    cb->getHealth = ccsp_getHealth_cb;
    cb->getHealth_data = handle;
    cb->restartBootstrap = ccsp_restartBootstrap_cb;
    cb->restartBootstrap_data = handle;
    cb->parameterValueChangeSignal = ccsp_parameterValueChangeSignal_cb;
    cb->parameterValueChangeSignal_data = handle;
    cb->deviceProfileChangeSignal = ccsp_deviceProfileChangeSignal_cb;
    cb->deviceProfileChangeSignal_data = handle;
    cb->diagCompleteSignal = ccsp_diagCompleteSignal_cb;
    cb->diagCompleteSignal_data = handle;
    cb->systemReadySignal = ccsp_systemReadySignal_cb;
    cb->systemReadySignal_data = handle;
    cb->systemRebootSignal = ccsp_systemRebootSignal_cb;
    cb->systemRebootSignal_data = handle;
    cb->webconfigSignal = ccsp_webconfigSignal_cb;
    cb->webconfigSignal_data = handle;
    cb->registerCaps = ccsp_registerCaps_cb;
    cb->registerCaps_data = handle;
    cb->isSystemReady = ccsp_isSystemReady_cb;
    cb->isSystemReady_data = handle;
    cb->multiCompBroadCastSignal = ccsp_multiCompBroadCastSignal_cb;
    cb->multiCompBroadCastSignal_data = handle;
    cb->multiCompMasterProcessSignal = ccsp_multiCompMasterProcessSignal_cb;
    cb->multiCompMasterProcessSignal_data = handle;
    cb->multiCompSlaveProcessSignal = ccsp_multiCompSlaveProcessSignal_cb;
    cb->multiCompSlaveProcessSignal_data = handle;
    cb->TunnelStatus = ccsp_TunnelStatus_cb;
    cb->TunnelStatus_data = handle;
    cb->WifiDbStatus = ccsp_WifiDbStatus_cb;
    cb->WifiDbStatus_data = handle;
}

os_tr181_error_t os_tr181_publish_objects(os_tr181_handle_t *handle)
{
    name_spaceType_t *namespaces = NULL;
    int namespace_count = 0;
    struct param_entry *param;
    CCSP_Base_Func_CB cb;
    int ret;
    int i;

    if (!handle || !handle->bus_handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Check if already published */
    if (handle->published)
    {
        LOGD("Objects already published");
        return OS_TR181_SUCCESS;
    }

    /* Count parameters to register */
    for (param = handle->parameters; param != NULL; param = param->next)
    {
        namespace_count++;
    }

    if (namespace_count == 0)
    {
        LOGD("No parameters to publish");
        handle->published = 1;
        return OS_TR181_SUCCESS;
    }

    /* Setup CCSP callback structure */
    memset(&cb, 0, sizeof(cb));
    os_tr181_fill_ccsp_func_cb(handle, &cb);

    /* Register callbacks with CCSP base interface */
    CcspBaseIf_SetCallback(handle->bus_handle, &cb);
    LOGD("CCSP base interface callbacks registered");

    /* Register DBUS message path handler */
    ret = CCSP_Message_Bus_Register_Path(handle->bus_handle, DBUS_PATH, dbus_message_handler, handle);

    if (ret != CCSP_Message_Bus_OK)
    {
        LOGE("CCSP_Message_Bus_Register_Path failed: %d", ret);
        return OS_TR181_ERROR;
    }

    /* Allocate namespace array */
    namespaces = malloc(sizeof(name_spaceType_t) * namespace_count);
    if (!namespaces)
    {
        LOGE("Failed to allocate namespace array");
        return OS_TR181_ERROR;
    }

    /* Fill namespace array with parameter paths and types */
    i = 0;
    for (param = handle->parameters; param != NULL; param = param->next)
    {
        namespaces[i].name_space = strdup(param->path);
        namespaces[i].dataType = os_tr181_type_to_ccsp(param->type);
        i++;
    }

    /* Register capabilities with Component Registrar */
    ret = CcspBaseIf_registerCapabilities(
            handle->bus_handle,
            CR_COMPONENT_ID,
            COMPONENT_NAME,
            COMPONENT_VERSION,
            DBUS_PATH,
            SUBSYSTEM_PREFIX,
            namespaces,
            namespace_count);

    /* Clean up namespace array */
    for (i = 0; i < namespace_count; i++)
    {
        if (namespaces[i].name_space)
        {
            free(namespaces[i].name_space);
        }
    }
    free(namespaces);

    if (ret != CCSP_SUCCESS)
    {
        LOGE("CcspBaseIf_registerCapabilities failed: %d %s", ret, Cdm_StrError(ret));
        return OS_TR181_ERROR;
    }

    LOGD("Published %d parameters to Component Registrar with callbacks", namespace_count);
    handle->published = 1;

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_notify_changed(
        os_tr181_handle_t *handle,
        const char *param_path,
        const os_tr181_val_t *old_value,
        const os_tr181_val_t *new_value)
{
    /* CCSP automatically sends notifications on parameter changes
     * This function is not needed for CCSP backend */
    (void)handle;
    (void)param_path;
    (void)old_value;
    (void)new_value;
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_process_requests(os_tr181_handle_t *handle, int timeout_ms)
{
    CCSP_MESSAGE_BUS_INFO *bus_info;
    DBusConnection *conn;
    int i;
    int processed = 0;

    if (!handle || !handle->bus_handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;

    /* RBUS handles incoming requests internally via callbacks registered
     * with the CCSP framework. However, DBUS connections are still used
     * for CCSP component registration and some legacy operations.
     * Process any pending DBUS messages for backward compatibility.
     */
    for (i = 0; i < CCSP_MESSAGE_BUS_MAX_CONNECTION; i++)
    {
        conn = bus_info->connection[i].conn;
        if (!conn)
        {
            continue;
        }

        /* Check if connection is valid */
        if (!dbus_connection_get_is_connected(conn))
        {
            continue;
        }

        /* Read and dispatch messages with timeout (only on first connection) */
        if (i == 0 && timeout_ms > 0)
        {
            /* This blocks and waits for messages, then dispatches them */
            if (dbus_connection_read_write_dispatch(conn, timeout_ms))
            {
                processed++;
            }
        }
        else
        {
            /* Non-blocking dispatch for other connections */
            DBusDispatchStatus status;

            /* Try to read without blocking */
            dbus_connection_read_write(conn, 0);

            /* Dispatch any pending messages */
            do
            {
                status = dbus_connection_dispatch(conn);
                if (status == DBUS_DISPATCH_DATA_REMAINS)
                {
                    processed++;
                }
            } while (status == DBUS_DISPATCH_DATA_REMAINS);
        }
    }

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_add_instance(os_tr181_handle_t *handle, const char *object_path, int *instance_number)
{
    int ret;
    componentStruct_t **components = NULL;
    int component_size = 0;
    int inst_num = 0;

    if (!handle || !handle->bus_handle || !object_path || !instance_number)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Find component supporting this namespace */
    ret = CcspBaseIf_discComponentSupportingNamespace(
            handle->bus_handle,
            CR_COMPONENT_ID,
            object_path,
            "",
            &components,
            &component_size);

    if (ret != CCSP_SUCCESS || component_size == 0)
    {
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Add object instance */
    ret = CcspBaseIf_AddTblRow(
            handle->bus_handle,
            components[0]->componentName,
            components[0]->dbusPath,
            0, /* Session ID */
            (char *)object_path,
            &inst_num);

    free_componentStruct_t(handle->bus_handle, component_size, components);

    if (ret == CCSP_SUCCESS)
    {
        *instance_number = inst_num;
        return OS_TR181_SUCCESS;
    }

    return OS_TR181_ERROR;
}

os_tr181_error_t os_tr181_delete_instance(os_tr181_handle_t *handle, const char *instance_path)
{
    int ret;
    componentStruct_t **components = NULL;
    int component_size = 0;

    if (!handle || !handle->bus_handle || !instance_path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Find component supporting this namespace */
    ret = CcspBaseIf_discComponentSupportingNamespace(
            handle->bus_handle,
            CR_COMPONENT_ID,
            instance_path,
            "",
            &components,
            &component_size);

    if (ret != CCSP_SUCCESS || component_size == 0)
    {
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Delete object instance */
    ret = CcspBaseIf_DeleteTblRow(
            handle->bus_handle,
            components[0]->componentName,
            components[0]->dbusPath,
            0, /* Session ID */
            (char *)instance_path);

    free_componentStruct_t(handle->bus_handle, component_size, components);

    return (ret == CCSP_SUCCESS) ? OS_TR181_SUCCESS : OS_TR181_ERROR;
}

os_tr181_error_t os_tr181_get_instances(
        os_tr181_handle_t *handle,
        const char *object_path,
        int **instance_numbers,
        int *count)
{
    int ret;
    componentStruct_t **components = NULL;
    int component_size = 0;
    unsigned int inst_count = 0;
    unsigned int *inst_array = NULL;
    int *result_array = NULL;
    unsigned int i;

    if (!handle || !handle->bus_handle || !object_path || !instance_numbers || !count)
    {
        return OS_TR181_ERROR_INVALID;
    }

    *instance_numbers = NULL;
    *count = 0;

    /* Find component supporting this namespace */
    ret = CcspBaseIf_discComponentSupportingNamespace(
            handle->bus_handle,
            CR_COMPONENT_ID,
            object_path,
            "",
            &components,
            &component_size);

    if (ret != CCSP_SUCCESS || component_size == 0)
    {
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Use CCSP API to get instance numbers */
    ret = CcspBaseIf_GetNextLevelInstances(
            handle->bus_handle,
            components[0]->componentName,
            components[0]->dbusPath,
            (char *)object_path,
            &inst_count,
            &inst_array);

    free_componentStruct_t(handle->bus_handle, component_size, components);

    if (ret != CCSP_SUCCESS)
    {
        if (inst_array)
        {
            free(inst_array);
        }
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Empty table is valid - return success with count=0 */
    if (inst_count == 0)
    {
        if (inst_array)
        {
            free(inst_array);
        }
        *instance_numbers = NULL;
        *count = 0;
        return OS_TR181_SUCCESS;
    }

    /* Convert unsigned int array to int array */
    result_array = malloc(inst_count * sizeof(int));
    if (!result_array)
    {
        free(inst_array);
        return OS_TR181_ERROR;
    }

    for (i = 0; i < inst_count; i++)
    {
        result_array[i] = (int)inst_array[i];
    }

    free(inst_array);

    *instance_numbers = result_array;
    *count = (int)inst_count;
    return OS_TR181_SUCCESS;
}

/*
 * Helper: Check if instance exists by trying to get a parameter
 */
static int check_instance_exists_ccsp(void *bus_handle, const char *instance_path)
{
    int ret;
    componentStruct_t **components = NULL;
    int component_size = 0;
    parameterInfoStruct_t **params = NULL;
    int param_size = 0;
    int exists = 0;

    if (!bus_handle || !instance_path)
    {
        return 0;
    }

    /* Find component */
    ret = CcspBaseIf_discComponentSupportingNamespace(
            bus_handle,
            CR_COMPONENT_ID,
            instance_path,
            "",
            &components,
            &component_size);

    if (ret != CCSP_SUCCESS || component_size == 0)
    {
        return 0;
    }

    /* Try to get parameter names under the instance */
    ret = CcspBaseIf_getParameterNames(
            bus_handle,
            components[0]->componentName,
            components[0]->dbusPath,
            (char *)instance_path,
            0, /* Next level */
            &param_size,
            &params);

    if (ret == CCSP_SUCCESS && param_size >= 0)
    {
        exists = 1;
    }

    if (params)
    {
        free_parameterInfoStruct_t(bus_handle, param_size, params);
    }
    free_componentStruct_t(bus_handle, component_size, components);

    return exists;
}

/*
 * Event-based verification for CCSP
 */
static int wait_for_instance_event_ccsp(
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
        if (check_instance_exists_ccsp(handle->bus_handle, instance_path))
        {
            return OS_TR181_SUCCESS;
        }

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
    int ret;
    componentStruct_t **components = NULL;
    int component_size = 0;
    int inst_num = 0;
    char instance_path[256];

    if (!handle || !handle->bus_handle || !object_path || !instance_number)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Find component */
    ret = CcspBaseIf_discComponentSupportingNamespace(
            handle->bus_handle,
            CR_COMPONENT_ID,
            object_path,
            "",
            &components,
            &component_size);

    if (ret != CCSP_SUCCESS || component_size == 0)
    {
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* CCSP AddTblRow is synchronous - it waits for completion */
    ret = CcspBaseIf_AddTblRow(
            handle->bus_handle,
            components[0]->componentName,
            components[0]->dbusPath,
            0, /* Session ID */
            (char *)object_path,
            &inst_num);

    free_componentStruct_t(handle->bus_handle, component_size, components);

    if (ret != CCSP_SUCCESS)
    {
        return OS_TR181_ERROR;
    }

    *instance_number = inst_num;

    /* Verify instance exists - CCSP should be synchronous but verify anyway */
    snprintf(instance_path, sizeof(instance_path), "%s%d.", object_path, inst_num);

    if (check_instance_exists_ccsp(handle->bus_handle, instance_path))
    {
        return OS_TR181_SUCCESS;
    }

    /* If immediate check fails, fall back to event-based wait */
    return wait_for_instance_event_ccsp(handle, object_path, inst_num, timeout_ms);
}

const char *os_tr181_get_backend_name(void)
{
    return "ccsp";
}
