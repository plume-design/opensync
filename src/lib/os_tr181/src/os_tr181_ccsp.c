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
#include <fcntl.h>
#include <pthread.h>
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
#include <rbus/rbuscore.h>
#include <rbus/rbuscore_types.h>
#include <rtmessage/rtLog.h>
#include <rtmessage/rtMessage.h>
#include <rtmessage/rtMemory.h>
#include <ccsp/ccsp_message_bus.h>
#include <ccsp/ccsp_base_api.h>
#include <ccsp/ccsp_dm_api.h>
#include <ccsp/ccsp_trace.h>
#include <ccsp/ccsp_psm_helper.h>

#include "os_tr181.h"
#include "os_tr181_internal.h"
#include "os_tr181_val_rbus.h"
#include "log.h"

/* GNU extension for program name */
extern const char *__progname;

/* RBUS internal structure - defined in rbus.c */
struct _rbusMethodAsyncHandle
{
    rtMessageHeader hdr;
};

#define CR_COMPONENT_ID   "com.cisco.spvtg.ccsp.CR"
#define COMPONENT_VERSION 1
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
    uint32_t access_flags;
    bool written_once; /* Tracks if write-once parameter has been written */
    bool persistent;   /* True if OS_TR181_ACCESS_PERSISTENT_FLAG is set */
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

/* Per-parent-path instance tracking.  Nested tables share one table_entry
 * (keyed by template path like "Table.{i}.Numbers."), but each instantiated
 * parent (e.g. "Table.1.Numbers.", "Table.2.Numbers.") needs its own
 * sequential counter and instance list. */
struct table_instance_set
{
    char *parent_path; /* Full instance path of this table, e.g. "Table.1.Numbers." */
    int *instances;    /* Dynamic array of active instance IDs */
    int instance_count;
    int instance_capacity;
    int last_index; /* Last assigned index for this parent */
    struct table_instance_set *next;
};

struct table_entry
{
    char *path;
    os_tr181_add_cb_t add_cb;
    os_tr181_del_cb_t del_cb;
    void *user_data;

    /* Per-parent instance tracking (one set per instantiated parent path) */
    struct table_instance_set *instance_sets;

    struct table_entry *next;
};

/* Registered method entry */
struct method_entry
{
    char *path;
    os_tr181_method_cb_t method_cb;
    void *user_data;
    os_tr181_handle_t *handle; /* Backpointer to handle for callback */
    struct method_entry *next;
};

struct event_entry
{
    char *path;
    struct event_entry *next;
};

/* ========================================================================
 * Invoke Request List - Tracks pending async method invocations
 * ======================================================================== */

/* List entry - one per pending async method invoke request */
struct invoke_request_entry
{
    struct invoke_request_entry *next; /* Next entry in list */
    os_tr181_async_request_t *request; /* The async request (contains method_name) */
};

/* Invoke request list - maintains all pending async method invocations */
struct invoke_request_list
{
    struct invoke_request_entry *head; /* Head of linked list */
    struct invoke_request_entry *tail; /* Tail for O(1) append */
    int count;                         /* Number of pending requests */
};

/* Global list of all handles (for reverse lookup from rbusHandle_t) */
static os_tr181_handle_t *g_handle_list = NULL;

/* Internal RBUS functions not in public headers */
extern void rbusObject_initFromMessage(rbusObject_t *obj, rbusMessage msg);
extern void rbusObject_appendToMessage(rbusObject_t obj, rbusMessage msg);

/* Forward declarations */
static void add_handle_to_list(os_tr181_handle_t *handle);
static void remove_handle_from_list(os_tr181_handle_t *handle);
static os_tr181_handle_t *find_handle_by_rbus_handle(rbusHandle_t rbus_handle);
static void rbus_lock_main(os_tr181_handle_t *handle);
static void rbus_unlock_main(os_tr181_handle_t *handle);
static void ccsp_main_thread_park(os_tr181_handle_t *handle);
static struct param_entry *find_param_entry(os_tr181_handle_t *handle, const char *path);
static struct table_entry *find_table_entry(os_tr181_handle_t *handle, const char *path);
static struct table_instance_set *find_instance_set(struct table_entry *table, const char *parent_path);
static struct table_instance_set *find_or_create_instance_set(struct table_entry *table, const char *parent_path);
static void add_instance_to_table(struct table_entry *table, const char *parent_path, int inst_num);
static void remove_instance_from_table(struct table_entry *table, const char *parent_path, int inst_num);
static void extract_table_path(const char *inst_path, char *table_path, size_t len);
static void path_to_template(const char *path, char *tmpl, size_t len);
static void psm_restore_param(os_tr181_handle_t *handle, struct param_entry *param, const char *concrete_path);
static void psm_persist_value(
        os_tr181_handle_t *handle,
        const char *param_path,
        os_tr181_param_type_t type,
        const os_tr181_val_t *val);
static void psm_persist_instance(os_tr181_handle_t *handle, struct table_entry *table, const char *inst_path);
static void psm_delete_instance(os_tr181_handle_t *handle, struct table_entry *table, const char *inst_path);

/* Invoke request list management */
static os_tr181_error_t invoke_list_init(os_tr181_handle_t *handle);
static void invoke_list_cleanup(os_tr181_handle_t *handle);
static os_tr181_error_t invoke_list_add(os_tr181_handle_t *handle, os_tr181_async_request_t *request);
static os_tr181_async_request_t *invoke_list_find_and_remove(os_tr181_handle_t *handle, const char *method_name);
static os_tr181_error_t invoke_list_remove(os_tr181_handle_t *handle, os_tr181_async_request_t *request);
static int invoke_list_count(os_tr181_handle_t *handle);

struct os_tr181_handle_s
{
    void *bus_handle;
    char *component_name; /* Dynamically allocated component name */
    struct subscription_entry *subscriptions;
    struct object_entry *objects;
    struct param_entry *parameters;
    struct table_entry *tables;
    struct method_entry *methods;
    struct event_entry *events;
    struct invoke_request_list *invoke_list; /* Pending async invoke requests */
    bool published;                          /* Flag to track if objects have been published */
    bool in_callback;                        /* True when executing user callback functions */

    /* CCSP-specific: rbus handle */
    rbusHandle_t rbus_handle; /* Saved for reverse lookup */

    /* Global list linkage (for handle lookup) */
    struct os_tr181_handle_s *next;

    /* Event loop integration */
    os_tr181_fd_change_callback_t fd_change_cb; /* FD change notification callback */
    void *fd_change_user_data;                  /* User data for FD change callback */
    void *loop_ctx;                             /* Event loop context (struct os_tr181_libev_context*) */
    int wakeup_pipe[2];                         /* Self-pipe for cross-thread wakeup: [0]=read, [1]=write */

    /* RBUS thread / main thread synchronization (mutex/condvar handshake) */
    pthread_mutex_t rbus_lock; /* Protects rbus_main_locked and cond vars */
    pthread_cond_t rbus_cond;  /* RBUS thread waits here until main is parked */
    pthread_cond_t main_cond;  /* Main thread waits here while RBUS runs callbacks */
    bool rbus_main_locked;     /* True when main thread is parked */
};

/* Type translation helpers */
static os_tr181_param_type_t ccsp_type_to_os_tr181(enum dataType_e ccsp_type)
{
    switch (ccsp_type)
    {
        case ccsp_string:
            return OS_TR181_TYPE_STRING;
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
        case ccsp_double:
            return OS_TR181_TYPE_DOUBLE;
        case ccsp_float:
            return OS_TR181_TYPE_DOUBLE;
        case ccsp_byte:
            return OS_TR181_TYPE_UINT;
        case ccsp_none:
            return OS_TR181_TYPE_NONE;
    }
    return OS_TR181_TYPE_NONE;
}

static enum dataType_e os_tr181_type_to_ccsp(os_tr181_param_type_t type)
{
    switch (type)
    {
        case OS_TR181_TYPE_STRING:
            return ccsp_string;
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
        case OS_TR181_TYPE_DOUBLE:
            return ccsp_double;
        case OS_TR181_TYPE_DATETIME:
            return ccsp_dateTime;
        case OS_TR181_TYPE_BASE64:
            return ccsp_base64;
        case OS_TR181_TYPE_NONE:
            return ccsp_none;
        /* Structural types have no CCSP equivalent */
        case OS_TR181_TYPE_DICT:
        case OS_TR181_TYPE_LIST:
        case OS_TR181_TYPE_OBJECT:
        case OS_TR181_TYPE_TABLE:
        case OS_TR181_TYPE_INSTANCE:
        case OS_TR181_TYPE_METHOD:
        case OS_TR181_TYPE_EVENT:
        case OS_TR181_TYPE_PROPERTY:
            return ccsp_none;
    }
    return ccsp_none;
}

/* ========================================================================
 * Error Code Mapping (RBUS -> os_tr181)
 * ======================================================================== */

/*
 * Map RBUS error codes to os_tr181 error codes
 * Provides consistent error reporting across backends
 */
static os_tr181_error_t rbus_error_to_os_tr181(rbusError_t rbus_err)
{
    switch (rbus_err)
    {
        case RBUS_ERROR_SUCCESS:
            return OS_TR181_SUCCESS;

        /* Path/element/component not found */
        case RBUS_ERROR_DESTINATION_NOT_FOUND:
        case RBUS_ERROR_ELEMENT_DOES_NOT_EXIST:
        case RBUS_ERROR_COMPONENT_DOES_NOT_EXIST:
            return OS_TR181_ERROR_NOT_FOUND;

        /* Invalid input/arguments */
        case RBUS_ERROR_INVALID_INPUT:
        case RBUS_ERROR_INVALID_HANDLE:
        case RBUS_ERROR_INVALID_EVENT:
        case RBUS_ERROR_INVALID_OPERATION:
        case RBUS_ERROR_INVALID_RESPONSE_FROM_DESTINATION:
        case RBUS_ERROR_INVALID_CONTEXT:
        case RBUS_ERROR_INVALID_METHOD:
        case RBUS_ERROR_INVALID_NAMESPACE:
        case RBUS_ERROR_ELEMENT_NAME_MISSING:
            return OS_TR181_ERROR_INVALID;

        /* Timeout */
        case RBUS_ERROR_TIMEOUT:
            return OS_TR181_ERROR_TIMEOUT;

        /* Initialization/connection errors */
        case RBUS_ERROR_NOT_INITIALIZED:
        case RBUS_ERROR_DIRECT_CON_NOT_EXIST:
            return OS_TR181_ERROR_INIT;

        /* Generic errors */
        case RBUS_ERROR_BUS_ERROR:
        case RBUS_ERROR_OUT_OF_RESOURCES:
        case RBUS_ERROR_DESTINATION_NOT_REACHABLE:
        case RBUS_ERROR_DESTINATION_RESPONSE_FAILURE:
        case RBUS_ERROR_SESSION_ALREADY_EXIST:
        case RBUS_ERROR_COMPONENT_NAME_DUPLICATE:
        case RBUS_ERROR_ELEMENT_NAME_DUPLICATE:
        case RBUS_ERROR_ACCESS_NOT_ALLOWED:
        case RBUS_ERROR_ASYNC_RESPONSE:
        case RBUS_ERROR_NOSUBSCRIBERS:
        case RBUS_ERROR_SUBSCRIPTION_ALREADY_EXIST:
        default:
            return OS_TR181_ERROR;
    }
}

/* Map os_tr181 error codes to RBUS error codes (reverse of above) */
static rbusError_t os_tr181_to_rbus_error(os_tr181_error_t os_error)
{
    switch (os_error)
    {
        case OS_TR181_SUCCESS:
            return RBUS_ERROR_SUCCESS;

        case OS_TR181_ERROR_NOT_FOUND:
            return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;

        case OS_TR181_ERROR_INVALID:
            return RBUS_ERROR_INVALID_INPUT;

        case OS_TR181_ERROR_TIMEOUT:
            return RBUS_ERROR_TIMEOUT;

        case OS_TR181_ERROR_NOT_IMPLEMENTED:
            return RBUS_ERROR_INVALID_OPERATION;

        case OS_TR181_ERROR_INIT:
            return RBUS_ERROR_NOT_INITIALIZED;

        case OS_TR181_ERROR_OVERFLOW:
        case OS_TR181_ERROR:
        default:
            return RBUS_ERROR_BUS_ERROR;
    }
}

/* Forward declaration for event subscription handler */
static rbusError_t event_sub_handler(
        rbusHandle_t rbus_handle,
        rbusEventSubAction_t action,
        const char *event_name,
        rbusFilter_t filter,
        int32_t interval,
        bool *auto_publish);

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

/* Generate unique component name using process name and PID */
static char *generate_component_name(void)
{
    char *name = NULL;

    /* Format: os_tr181.<PROCESSNAME>.<PID> */
    if (asprintf(&name, "os_tr181.%s.%d", __progname, (int)getpid()) < 0) return NULL;

    return name;
}

os_tr181_error_t os_tr181_init_ex(os_tr181_handle_t **handle, const char *component_name)
{
    /* Use calloc to zero-initialize all fields */
    os_tr181_handle_t *h = calloc(1, sizeof(os_tr181_handle_t));
    CCSP_MESSAGE_BUS_INFO *bus_info = NULL;
    componentStruct_t **components = NULL;
    int component_size = 0;
    int ret;
    int i;
    char *pCfg = CCSP_MSG_BUS_CFG;

    if (!h)
    {
        return OS_TR181_ERROR_INIT;
    }

    /* pipe() FDs are 0 when zeroed by calloc, but 0 is a valid FD (stdin).
     * Initialise to -1 so the error handler can safely close only real FDs. */
    h->wakeup_pipe[0] = h->wakeup_pipe[1] = -1;
    pthread_mutex_init(&h->rbus_lock, NULL);
    pthread_cond_init(&h->rbus_cond, NULL);
    pthread_cond_init(&h->main_cond, NULL);

    /* Set component name (auto-generate if NULL) */
    h->component_name = component_name ? strdup(component_name) : generate_component_name();
    if (!h->component_name)
    {
        LOGE("Failed to allocate component name");
        goto error;
    }

    LOGD("Initializing with component name: %s", h->component_name);

    AnscSetTraceLevel(CCSP_TRACE_LEVEL_DEBUG);

    ret = CCSP_Message_Bus_Init(h->component_name, pCfg, &h->bus_handle, malloc, free);
    bus_info = (CCSP_MESSAGE_BUS_INFO *)h->bus_handle;
    if (ret != 0 || bus_info == NULL)
    {
        LOGE("CCSP_Message_Bus_Init failed: %d", ret);
        goto error;
    }

    /* Check if component name already exists */
    ret = CcspBaseIf_discComponentSupportingNamespace(
            bus_info,
            CR_COMPONENT_ID,
            "Device.", /* Query root to see all components */
            "",
            &components,
            &component_size);

    if (ret == CCSP_SUCCESS && components)
    {
        for (i = 0; i < component_size; i++)
        {
            if (strcmp(components[i]->componentName, h->component_name) == 0)
            {
                LOGE("Component name already in use: %s", h->component_name);
                free_componentStruct_t(bus_info, component_size, components);
                goto error;
            }
        }
        free_componentStruct_t(bus_info, component_size, components);
    }

#ifdef OVERRIDE_RBUS_LOG
    rtLogSetLogHandler(&os_tr181_rbus_log_handler);
#endif

    /* Replace the CCSP-tainted rbus handle with a fresh native one.
     *
     * CCSP_Message_Bus_Init installs cssp_event_subscribe_override_handler_rbus
     * as the subscribe callback, which returns RBUSCORE_ERROR_SUBSCRIBE_NOT_HANDLED
     * for all '!' events.  The subscriber reads that raw integer as rbusError_t
     * (14 = RBUS_ERROR_ELEMENT_NAME_DUPLICATE), breaking subscriptions.  It also
     * prevents rbus from populating el->subscriptions, so rbusEvent_Publish finds
     * no subscribers.  Additionally, table row auto-instantiation (instantiateTableRow)
     * only clones {i} sub-elements — including events — on the handle that owns the
     * table; with a split-handle scheme that never works cross-handle.
     *
     * Closing and reopening with rbus_open installs the native _callback_handler,
     * which handles subscriptions, GET/SET and table operations correctly.
     * bus_info->rbus_handle is read by pointer in all ccsp_base_api functions and
     * is safe to replace. */
    rbusHandle_t native_handle = NULL;
    rbusError_t rbus_err;

    if (bus_info->rbus_handle)
    {
        rbus_err = rbus_close(bus_info->rbus_handle);
        if (rbus_err != RBUS_ERROR_SUCCESS) LOGW("rbus_close failed: %d (continuing)", rbus_err);
        bus_info->rbus_handle = NULL;
    }

    rbus_err = rbus_open(&native_handle, h->component_name);
    if (rbus_err != RBUS_ERROR_SUCCESS)
    {
        LOGE("rbus_open failed: %d", rbus_err);
        goto error;
    }

    bus_info->rbus_handle = native_handle;
    h->rbus_handle = native_handle;
    LOGD("Native rbus handle opened: %s", h->component_name);

    /* Initialize invoke request list */
    ret = invoke_list_init(h);
    if (ret != OS_TR181_SUCCESS)
    {
        LOGE("Failed to initialize invoke request list");
        goto error;
    }

    /* Add handle to global list for reverse lookup */
    add_handle_to_list(h);

    /* Create self-pipe for cross-thread wakeup (RBUS callbacks → main thread) */
    if (pipe(h->wakeup_pipe) != 0)
    {
        LOGE("Failed to create wakeup pipe: %s", strerror(errno));
        goto error;
    }
    /* Make write end non-blocking so dispatcher never blocks if pipe fills up */
    fcntl(h->wakeup_pipe[1], F_SETFL, O_NONBLOCK);

    *handle = h;
    return OS_TR181_SUCCESS;

error:
    remove_handle_from_list(h);
    invoke_list_cleanup(h);
    if (h->rbus_handle) rbus_close(h->rbus_handle);
    if (h->bus_handle) CCSP_Message_Bus_Exit(h->bus_handle);
    if (h->wakeup_pipe[0] >= 0) close(h->wakeup_pipe[0]);
    if (h->wakeup_pipe[1] >= 0) close(h->wakeup_pipe[1]);
    pthread_mutex_destroy(&h->rbus_lock);
    pthread_cond_destroy(&h->rbus_cond);
    pthread_cond_destroy(&h->main_cond);
    free(h->component_name);
    free(h);
    return OS_TR181_ERROR_INIT;
}

void os_tr181_close(os_tr181_handle_t *handle)
{
    if (handle)
    {
        /* Detach from event loop if attached */
        os_tr181_detach_loop(handle);

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
            struct table_instance_set *iset = table->instance_sets;
            while (iset)
            {
                struct table_instance_set *inext = iset->next;
                free(iset->parent_path);
                free(iset->instances);
                free(iset);
                iset = inext;
            }
            free(table);
            table = next;
        }

        /* Clean up registered methods */
        struct method_entry *method = handle->methods;
        while (method)
        {
            struct method_entry *next = method->next;
            free(method->path);
            free(method);
            method = next;
        }

        /* Clean up registered events */
        struct event_entry *event = handle->events;
        while (event)
        {
            struct event_entry *enext = event->next;
            free(event->path);
            free(event);
            event = enext;
        }

        /* Clean up invoke request list */
        invoke_list_cleanup(handle);

        /* Remove from global handle list */
        remove_handle_from_list(handle);

        /* Clear handle-specific fields */
        handle->rbus_handle = NULL;

        if (handle->bus_handle)
        {
            /* Cdm_Term(); */
            CCSP_Message_Bus_Exit(handle->bus_handle);
        }

        /* Free component name */
        free(handle->component_name);

        /* Close wakeup pipe and destroy synchronization primitives */
        if (handle->wakeup_pipe[0] >= 0) close(handle->wakeup_pipe[0]);
        if (handle->wakeup_pipe[1] >= 0) close(handle->wakeup_pipe[1]);
        pthread_mutex_destroy(&handle->rbus_lock);
        pthread_cond_destroy(&handle->rbus_cond);
        pthread_cond_destroy(&handle->main_cond);

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

/* Helper to detect if a name represents an instance (ends with digits before trailing dot) */
static bool is_instance_name(const char *name)
{
    size_t len = strlen(name);

    /* Must end with '.' */
    if (len < 2 || name[len - 1] != '.') return false;

    /* Walk backwards from second-to-last char, checking if all are digits */
    size_t i = len - 2;
    while (i > 0 && name[i] != '.')
    {
        if (name[i] < '0' || name[i] > '9') return false;
        i--;
    }

    /* If we found a dot and there were digits after it, it's an instance */
    return (name[i] == '.' && i < len - 2);
}

/*
 * Return true if an element name is a row-template node that appears inside
 * an instantiated row, e.g. "Table.1.Numbers.{i}." or "Table.1.Numbers.{i}.Value".
 * We detect this by requiring a dot-bounded numeric component (e.g. ".1.") before
 * the ".{i}" marker.  This correctly ignores digits that are part of a component
 * name such as "IPv6" or "X_RDKCENTRAL_COM_IPv4" and only matches actual instance
 * numbers like "Table.1.Numbers.{i}".
 */
static bool is_nested_template_path(const char *name)
{
    const char *brace = strstr(name, ".{i}");
    if (!brace) return false;
    /* Scan for a dot-bounded all-digit component before the .{i} */
    for (const char *p = name; p < brace; p++)
    {
        if (*p != '.') continue;
        const char *start = p + 1;
        const char *end = start;
        while (end < brace && *end != '.')
            end++;
        if (end > start && end < brace && *end == '.')
        {
            /* Check if every character in start..end is a digit */
            bool all_digits = true;
            for (const char *d = start; d < end; d++)
            {
                if (!isdigit((unsigned char)*d))
                {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits && end > start) return true;
        }
    }
    return false;
}

/* Helper to fetch detailed type information for properties */
static void fetch_parameter_details(
        os_tr181_param_info_t *list,
        int elem_count,
        const char *path,
        CCSP_MESSAGE_BUS_INFO *bus_info)
{
    char **param_names = NULL;
    int *param_indices = NULL;
    int param_count = 0;
    int i;

    /* Build list of parameter names to query */
    param_names = calloc(elem_count, sizeof(char *));
    param_indices = calloc(elem_count, sizeof(int));

    if (!param_names || !param_indices)
    {
        free(param_names);
        free(param_indices);
        return; /* Silently fail - leave types as PROPERTY */
    }

    /* Collect parameters that need type lookup (properties with PROPERTY type) */
    for (i = 0; i < elem_count; i++)
    {
        if (list[i].type == OS_TR181_TYPE_PROPERTY)
        {
            param_names[param_count] = list[i].name;
            param_indices[param_count] = i;
            param_count++;
        }
    }

    /* Query parameter values to get actual types */
    if (param_count > 0)
    {
        componentStruct_t **components = NULL;
        int component_size = 0;

        /* Find component for value queries */
        int ret = CcspBaseIf_discComponentSupportingNamespace(
                bus_info,
                CR_COMPONENT_ID,
                path,
                "",
                &components,
                &component_size);

        if (ret == CCSP_SUCCESS && component_size > 0)
        {
            parameterValStruct_t **parameterVal = NULL;
            int val_size = 0;
            int type_ret = CcspBaseIf_getParameterValues(
                    bus_info,
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
                free_parameterValStruct_t(bus_info, val_size, parameterVal);
            }

            free_componentStruct_t(bus_info, component_size, components);
        }
    }

    free(param_names);
    free(param_indices);
}

os_tr181_error_t os_tr181_list(
        os_tr181_handle_t *handle,
        const char *path,
        uint32_t flags,
        os_tr181_param_info_t **params,
        int *count)
{
    rbusError_t rbus_ret;
    rbusElementInfo_t *elems = NULL;
    rbusElementInfo_t *elem = NULL;
    int elem_count = 0;
    int i;
    bool recursive = (flags & OS_TR181_LIST_RECURSIVE) != 0;
    bool fetch_details = (flags & OS_TR181_LIST_DETAILS) != 0;
    CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;

    if (!handle || !handle->bus_handle || !handle->rbus_handle || !path || !params || !count)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /*
     * Note: CcspBaseIf_getParameterNames() and CcspBaseIf_getObjType() don't provide
     * method typing information. Therefore we're using rbusElementInfo_get() which
     * provides accurate type information including RBUS_ELEMENT_TYPE_METHOD.
     */

    /* Get element info using RBUS API
     * depth: negative value for next level only, 0 for exact element, positive for recursive */
    int depth = recursive ? RBUS_MAX_NAME_DEPTH : -1;
    rbus_ret = rbusElementInfo_get(handle->rbus_handle, path, depth, &elems);

    if (rbus_ret != RBUS_ERROR_SUCCESS || !elems)
    {
        *params = NULL;
        *count = 0;
        return (rbus_ret == RBUS_ERROR_DESTINATION_NOT_FOUND) ? OS_TR181_ERROR_NOT_FOUND : OS_TR181_ERROR;
    }

    /* Count elements, skipping row-template nodes inside instantiated rows
     * (e.g. "Table.1.Numbers.{i}." and "Table.1.Numbers.{i}.Value").
     * rbus exposes these as part of the in-memory element tree but they are
     * not real objects/parameters — they are the template used to instantiate
     * further nested-table rows. */
    elem = elems;
    while (elem)
    {
        if (!is_nested_template_path(elem->name)) elem_count++;
        elem = elem->next;
    }

    if (elem_count == 0)
    {
        rbusElementInfo_free(handle->rbus_handle, elems);
        *params = NULL;
        *count = 0;
        return OS_TR181_SUCCESS;
    }

    /* Allocate result list */
    os_tr181_param_info_t *list = calloc(elem_count, sizeof(os_tr181_param_info_t));
    if (!list)
    {
        rbusElementInfo_free(handle->rbus_handle, elems);
        return OS_TR181_ERROR;
    }

    /* First pass: populate names, types, and writable flags from RBUS element info */
    elem = elems;
    i = 0;
    while (elem && i < elem_count)
    {
        /* Skip row-template nodes inside instantiated rows (see filter above) */
        if (is_nested_template_path(elem->name))
        {
            elem = elem->next;
            continue;
        }

        list[i].name = strdup(elem->name);
        if (!list[i].name)
        {
            /* Clean up on allocation failure */
            for (int j = 0; j < i; j++)
            {
                free(list[j].name);
            }
            free(list);
            rbusElementInfo_free(handle->rbus_handle, elems);
            return OS_TR181_ERROR;
        }

        /* Map RBUS element type to our type */
        switch (elem->type)
        {
            case RBUS_ELEMENT_TYPE_TABLE:
                list[i].type = OS_TR181_TYPE_TABLE;
                break;
            case RBUS_ELEMENT_TYPE_METHOD:
                list[i].type = OS_TR181_TYPE_METHOD;
                break;
            case RBUS_ELEMENT_TYPE_EVENT:
                list[i].type = OS_TR181_TYPE_EVENT;
                break;
            case RBUS_ELEMENT_TYPE_PROPERTY:
                /* Property - type will be OS_TR181_TYPE_PROPERTY unless details are fetched */
                list[i].type = OS_TR181_TYPE_PROPERTY;
                break;
            default: /* RBUS_ELEMENT_TYPE_OBJECT or unknown */
                /* Check if it's an instance (ends with numeric.{i} pattern) */
                if (is_instance_name(elem->name))
                {
                    list[i].type = OS_TR181_TYPE_INSTANCE;
                }
                else
                {
                    list[i].type = OS_TR181_TYPE_OBJECT;
                }
                break;
        }

        /* Determine access flags from RBUS access bits */
        list[i].access_flags = (elem->access & RBUS_ACCESS_SET) ? OS_TR181_ACCESS_READWRITE : OS_TR181_ACCESS_READONLY;

        elem = elem->next;
        i++;
    }

    /* Fetch detailed type information if requested */
    if (fetch_details)
    {
        fetch_parameter_details(list, elem_count, path, bus_info);
    }

    rbusElementInfo_free(handle->rbus_handle, elems);

    *params = list;
    *count = elem_count;

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

    /* USP custom event: deliver all event data fields as a dict */
    if (event->type == RBUS_EVENT_GENERAL)
    {
        os_tr181_val_t event_val = OS_VAL_INIT();
        os_val_set_dict(&event_val);

        if (event->data)
        {
            rbusProperty_t prop = rbusObject_GetProperties(event->data);
            while (prop)
            {
                const char *prop_name = rbusProperty_GetName(prop);
                rbusValue_t rval = rbusProperty_GetValue(prop);
                if (prop_name && rval)
                {
                    os_tr181_val_t entry = OS_VAL_INIT();
                    if (rbus_value_to_val(rval, &entry) == OS_TR181_SUCCESS)
                    {
                        os_val_dict_set(&event_val, prop_name, &entry);
                    }
                    os_val_free(&entry);
                }
                prop = rbusProperty_GetNext(prop);
            }
        }

        rbus_lock_main(handle);
        handle->in_callback = true;
        sub->callback(param_name, &event_val, sub->user_data);
        handle->in_callback = false;
        cleanup_marked_subscriptions(handle);
        rbus_unlock_main(handle);

        os_val_free(&event_val);
        return;
    }

    /* Value-change event: extract the "value" property */
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
        rbus_lock_main(handle);

        /* Mark that we're executing user callback */
        handle->in_callback = true;

        /* Dispatch to this subscription's callback */
        sub->callback(param_name, &val, sub->user_data);

        /* Clear callback flag */
        handle->in_callback = false;

        /* Clean up any subscriptions that were marked during callback */
        cleanup_marked_subscriptions(handle);

        rbus_unlock_main(handle);
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
    sub = calloc(1, sizeof(struct subscription_entry));
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

    obj = calloc(1, sizeof(struct object_entry));
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
        uint32_t access_flags,
        os_tr181_get_cb_t get_callback,
        os_tr181_set_cb_t set_callback,
        void *user_data)
{
    struct param_entry *param;

    if (!handle || !param_path || !get_callback)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if ((access_flags & OS_TR181_ACCESS_WRITE_FLAG) && !set_callback)
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

    param = calloc(1, sizeof(struct param_entry));
    if (!param)
    {
        return OS_TR181_ERROR;
    }

    param->path = strdup(param_path);
    param->type = type;
    param->access_flags = access_flags;
    param->written_once = false; /* Initialize write-once tracking */
    param->persistent = (access_flags & OS_TR181_ACCESS_PERSISTENT_FLAG) != 0;
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

    table = calloc(1, sizeof(struct table_entry));
    if (!table)
    {
        return OS_TR181_ERROR;
    }

    table->path = strdup(table_path);
    table->add_cb = add_callback;
    table->del_cb = del_callback;
    table->user_data = user_data;
    table->instance_sets = NULL;

    /* Append to maintain registration order so parent tables are registered
     * before child tables.  rbus insertElement() for TABLE type overwrites
     * currentNode->child with the new {i} row-template, so a child table
     * must be inserted AFTER its parent's {i} subtree already exists. */
    if (handle->tables == NULL)
    {
        handle->tables = table;
    }
    else
    {
        struct table_entry *last = handle->tables;
        while (last->next)
            last = last->next;
        last->next = table;
    }

    return OS_TR181_SUCCESS;
}

/* Add handle to global list */
static void add_handle_to_list(os_tr181_handle_t *handle)
{
    if (!handle) return;

    handle->next = g_handle_list;
    g_handle_list = handle;
}

/* Remove handle from global list */
static void remove_handle_from_list(os_tr181_handle_t *handle)
{
    os_tr181_handle_t **pp;

    if (!handle) return;

    for (pp = &g_handle_list; *pp != NULL; pp = &(*pp)->next)
    {
        if (*pp == handle)
        {
            *pp = handle->next;
            handle->next = NULL;
            return;
        }
    }
}

/* Find handle by rbusHandle_t (for method_invoke_cb) */
static os_tr181_handle_t *find_handle_by_rbus_handle(rbusHandle_t rbus_handle)
{
    os_tr181_handle_t *h;

    for (h = g_handle_list; h != NULL; h = h->next)
    {
        if (h->rbus_handle == rbus_handle) return h;
    }

    return NULL;
}

/* ========================================================================
 * Invoke Request List Management
 * ======================================================================== */

/* Initialize invoke request list */
static os_tr181_error_t invoke_list_init(os_tr181_handle_t *handle)
{
    struct invoke_request_list *list;

    if (!handle) return OS_TR181_ERROR_INVALID;

    list = (struct invoke_request_list *)calloc(1, sizeof(struct invoke_request_list));
    if (!list)
    {
        LOGE("Failed to allocate invoke request list");
        return OS_TR181_ERROR;
    }

    list->head = NULL;
    list->tail = NULL;
    list->count = 0;

    handle->invoke_list = list;

    LOGD("Invoke request list initialized");
    return OS_TR181_SUCCESS;
}

/* Cleanup invoke request list - frees all pending requests */
static void invoke_list_cleanup(os_tr181_handle_t *handle)
{
    struct invoke_request_list *list;
    struct invoke_request_entry *entry, *next;

    if (!handle || !handle->invoke_list) return;

    list = handle->invoke_list;

    LOGD("Cleaning up invoke request list (%d pending requests)", list->count);

    /* Free all entries and their requests */
    entry = list->head;
    while (entry)
    {
        next = entry->next;

        if (entry->request)
        {
            LOGW("Freeing orphaned async request for method: %s", entry->request->method_name ?: "unknown");
            os_tr181_async_request_free(entry->request);
        }

        free(entry);
        entry = next;
    }

    /* Free list structure */
    free(list);
    handle->invoke_list = NULL;
}

/* Add request to list (append to tail for FIFO) */
static os_tr181_error_t invoke_list_add(os_tr181_handle_t *handle, os_tr181_async_request_t *request)
{
    struct invoke_request_list *list;
    struct invoke_request_entry *entry;

    if (!handle || !request) return OS_TR181_ERROR_INVALID;

    list = handle->invoke_list;
    if (!list)
    {
        LOGE("Invoke list not initialized");
        return OS_TR181_ERROR;
    }

    /* Allocate new entry */
    entry = (struct invoke_request_entry *)calloc(1, sizeof(struct invoke_request_entry));
    if (!entry)
    {
        LOGE("Failed to allocate invoke request entry");
        return OS_TR181_ERROR;
    }

    entry->request = request;
    entry->next = NULL;

    /* Append to tail */
    if (list->tail)
    {
        list->tail->next = entry;
        list->tail = entry;
    }
    else
    {
        /* Empty list */
        list->head = entry;
        list->tail = entry;
    }

    list->count++;

    LOGD("Added async invoke request for method: %s (count=%d)", request->method_name ?: "unknown", list->count);

    return OS_TR181_SUCCESS;
}

/* Find and remove request by method name (FIFO - returns first match) */
static os_tr181_async_request_t *invoke_list_find_and_remove(os_tr181_handle_t *handle, const char *method_name)
{
    struct invoke_request_list *list;
    struct invoke_request_entry *entry, *prev;
    os_tr181_async_request_t *request;

    if (!handle || !method_name) return NULL;

    list = handle->invoke_list;
    if (!list || !list->head) return NULL;

    prev = NULL;
    entry = list->head;

    /* Search for matching method name */
    while (entry)
    {
        if (entry->request && entry->request->method_name && strcmp(entry->request->method_name, method_name) == 0)
        {
            /* Found match - remove from list */
            if (prev)
            {
                prev->next = entry->next;
            }
            else
            {
                /* Removing head */
                list->head = entry->next;
            }

            /* Update tail if removing last entry */
            if (entry == list->tail)
            {
                list->tail = prev;
            }

            list->count--;

            request = entry->request;
            free(entry);

            LOGD("Found and removed async invoke request for method: %s (count=%d)", method_name, list->count);

            return request;
        }

        prev = entry;
        entry = entry->next;
    }

    /* Not found */
    return NULL;
}

/* Remove specific request from list (kept for future use) */
__attribute__((unused)) static os_tr181_error_t invoke_list_remove(
        os_tr181_handle_t *handle,
        os_tr181_async_request_t *request)
{
    struct invoke_request_list *list;
    struct invoke_request_entry *entry, *prev;

    if (!handle || !request) return OS_TR181_ERROR_INVALID;

    list = handle->invoke_list;
    if (!list || !list->head) return OS_TR181_ERROR_INVALID;

    prev = NULL;
    entry = list->head;

    /* Search for specific request pointer */
    while (entry)
    {
        if (entry->request == request)
        {
            /* Found match - remove from list */
            if (prev)
            {
                prev->next = entry->next;
            }
            else
            {
                /* Removing head */
                list->head = entry->next;
            }

            /* Update tail if removing last entry */
            if (entry == list->tail)
            {
                list->tail = prev;
            }

            list->count--;

            free(entry);

            LOGD("Removed specific async invoke request (count=%d)", list->count);

            return OS_TR181_SUCCESS;
        }

        prev = entry;
        entry = entry->next;
    }

    /* Not found */
    return OS_TR181_ERROR_INVALID;
}

/* Get count of pending requests (kept for future use) */
__attribute__((unused)) static int invoke_list_count(os_tr181_handle_t *handle)
{
    if (!handle || !handle->invoke_list) return 0;

    return handle->invoke_list->count;
}

/* Find a registered method by name, falling back to template matching for instance paths */
static struct method_entry *find_registered_method(os_tr181_handle_t *handle, const char *name)
{
    struct method_entry *me;
    char template_path[OS_TR181_PATH_MAX];

    if (!handle || !name) return NULL;

    /* Try exact match first */
    for (me = handle->methods; me != NULL; me = me->next)
    {
        if (strcmp(me->path, name) == 0) return me;
    }

    /* Fall back to template match: convert instance path to template and retry.
     * e.g. "Table.1.Numbers.1.ObjectB.MethodB()" -> "Table.{i}.Numbers.{i}.ObjectB.MethodB()" */
    path_to_template(name, template_path, sizeof(template_path));
    if (strcmp(template_path, name) != 0)
    {
        for (me = handle->methods; me != NULL; me = me->next)
        {
            if (strcmp(me->path, template_path) == 0) return me;
        }
    }

    return NULL;
}

/*
 * Native rbus GET handler — called by _get_callback_handler on the rbus worker
 * thread for each get request targeting a registered property.  Acquires the
 * main-thread lock to safely invoke the user's get callback.
 */
static rbusError_t param_get_rbus_cb(rbusHandle_t handle, rbusProperty_t property, rbusGetHandlerOptions_t *options)
{
    os_tr181_handle_t *os_handle = find_handle_by_rbus_handle(handle);
    const char *name = rbusProperty_GetName(property);
    struct param_entry *param;
    os_tr181_val_t val = OS_VAL_INIT();
    os_tr181_error_t os_ret;
    rbusValue_t rbus_val = NULL;

    (void)options;

    if (!os_handle || !name) return RBUS_ERROR_INVALID_INPUT;

    param = find_param_entry(os_handle, name);
    if (!param || !param->get_cb)
    {
        LOGE("param_get_rbus_cb: param not found or no get_cb: %s", name);
        return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    rbus_lock_main(os_handle);
    os_ret = param->get_cb(name, &val, param->user_data);
    rbus_unlock_main(os_handle);

    if (os_ret != OS_TR181_SUCCESS)
    {
        LOGE("param_get_rbus_cb: get_cb failed for %s: %s", name, os_tr181_error_string(os_ret));
        return os_tr181_to_rbus_error(os_ret);
    }

    if (os_tr181_val_to_rbus(&val, &rbus_val) != OS_TR181_SUCCESS || !rbus_val)
    {
        LOGE("param_get_rbus_cb: value conversion failed for %s", name);
        os_val_free(&val);
        return RBUS_ERROR_BUS_ERROR;
    }

    rbusProperty_SetValue(property, rbus_val);
    rbusValue_Release(rbus_val);
    os_val_free(&val);
    return RBUS_ERROR_SUCCESS;
}

/*
 * Native rbus SET handler — called by _set_callback_handler on the rbus worker
 * thread.  Acquires the main-thread lock to safely invoke the user's set callback.
 */
static rbusError_t param_set_rbus_cb(rbusHandle_t handle, rbusProperty_t property, rbusSetHandlerOptions_t *options)
{
    os_tr181_handle_t *os_handle = find_handle_by_rbus_handle(handle);
    const char *name = rbusProperty_GetName(property);
    struct param_entry *param;
    rbusValue_t rbus_val;
    os_tr181_val_t val = OS_VAL_INIT();
    os_tr181_error_t os_ret;

    (void)options;

    if (!os_handle || !name) return RBUS_ERROR_INVALID_INPUT;

    param = find_param_entry(os_handle, name);
    if (!param)
    {
        LOGE("param_set_rbus_cb: param not found: %s", name);
        return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    if (!(param->access_flags & OS_TR181_ACCESS_WRITE_FLAG))
    {
        LOGE("param_set_rbus_cb: param is read-only: %s", name);
        return RBUS_ERROR_ACCESS_NOT_ALLOWED;
    }

    if ((param->access_flags & OS_TR181_ACCESS_WRITE_ONCE_FLAG) && param->written_once)
    {
        LOGE("param_set_rbus_cb: write-once param already set: %s", name);
        return RBUS_ERROR_ACCESS_NOT_ALLOWED;
    }

    if (!param->set_cb)
    {
        LOGE("param_set_rbus_cb: no set_cb for: %s", name);
        return RBUS_ERROR_INVALID_OPERATION;
    }

    rbus_val = rbusProperty_GetValue(property);
    if (!rbus_val) return RBUS_ERROR_INVALID_INPUT;

    if (os_tr181_val_from_rbus(&val, rbus_val) != OS_TR181_SUCCESS)
    {
        LOGE("param_set_rbus_cb: value conversion failed for %s", name);
        return RBUS_ERROR_INVALID_INPUT;
    }

    rbus_lock_main(os_handle);
    os_ret = param->set_cb(name, &val, param->user_data);
    rbus_unlock_main(os_handle);

    /* Persist the new value to PSM */
    if (os_ret == OS_TR181_SUCCESS && param->persistent) psm_persist_value(os_handle, name, param->type, &val);

    os_val_free(&val);

    if (os_ret != OS_TR181_SUCCESS)
    {
        LOGE("param_set_rbus_cb: set_cb failed for %s: %s", name, os_tr181_error_string(os_ret));
        return os_tr181_to_rbus_error(os_ret);
    }

    if (param->access_flags & OS_TR181_ACCESS_WRITE_ONCE_FLAG) param->written_once = true;

    return RBUS_ERROR_SUCCESS;
}

/*
 * Native rbus table ADD ROW handler — called by _table_add_row_callback_handler
 * after a client sends METHOD_ADDTBLROW.  rbus automatically calls registerTableRow
 * (instantiating the {i} template including events) after this handler returns
 * RBUS_ERROR_SUCCESS.  Do NOT call rbusTable_registerRow here.
 */
static rbusError_t table_add_row_rbus_cb(
        rbusHandle_t handle,
        char const *tableName,
        char const *aliasName,
        uint32_t *instNum)
{
    os_tr181_handle_t *os_handle = find_handle_by_rbus_handle(handle);
    struct table_entry *table;
    uint32_t inst_num;
    os_tr181_error_t os_ret;

    (void)aliasName;

    if (!os_handle || !tableName || !instNum) return RBUS_ERROR_INVALID_INPUT;

    table = find_table_entry(os_handle, tableName);
    if (!table)
    {
        LOGE("table_add_row_rbus_cb: table not found: %s", tableName);
        return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    if (!table->add_cb)
    {
        LOGE("table_add_row_rbus_cb: no add_cb for table: %s", tableName);
        return RBUS_ERROR_INVALID_OPERATION;
    }

    {
        struct table_instance_set *iset = find_or_create_instance_set(table, tableName);

        inst_num = iset ? (uint32_t)(iset->last_index + 1) : 1;
    }

    rbus_lock_main(os_handle);
    os_ret = table->add_cb(tableName, (int)inst_num, NULL, table->user_data);

    if (os_ret != OS_TR181_SUCCESS)
    {
        rbus_unlock_main(os_handle);
        LOGE("table_add_row_rbus_cb: add_cb failed for %s%u: %s", tableName, inst_num, os_tr181_error_string(os_ret));
        return RBUS_ERROR_BUS_ERROR;
    }

    add_instance_to_table(table, tableName, (int)inst_num);
    {
        struct table_instance_set *iset = find_instance_set(table, tableName);
        if (iset) iset->last_index = (int)inst_num;
    }
    *instNum = inst_num;

    /* Persist the instance
     * The lock is held across psm_persist_instance so that get_cb reads
     * stable app state initialised by add_cb above. */
    char inst_path[OS_TR181_PATH_MAX];
    snprintf(inst_path, sizeof(inst_path), "%s%u.", tableName, inst_num);
    psm_persist_instance(os_handle, table, inst_path);

    rbus_unlock_main(os_handle);

    LOGD("table_add_row_rbus_cb: added %s%u.", tableName, inst_num);
    return RBUS_ERROR_SUCCESS;
}

/*
 * Native rbus table REMOVE ROW handler — called by _table_remove_row_callback_handler.
 * rbus automatically calls unregisterTableRow after this handler returns success.
 * Do NOT call rbusTable_unregisterRow here.
 */
static rbusError_t table_del_row_rbus_cb(rbusHandle_t handle, char const *rowName)
{
    os_tr181_handle_t *os_handle = find_handle_by_rbus_handle(handle);
    struct table_entry *table;
    char table_path[OS_TR181_PATH_MAX];
    int inst_num;
    os_tr181_error_t os_ret = OS_TR181_SUCCESS;

    if (!os_handle || !rowName) return RBUS_ERROR_INVALID_INPUT;

    inst_num = os_tr181_parse_instance(rowName);
    if (inst_num <= 0)
    {
        LOGE("table_del_row_rbus_cb: invalid instance path: %s", rowName);
        return RBUS_ERROR_INVALID_INPUT;
    }

    extract_table_path(rowName, table_path, sizeof(table_path));
    if (table_path[0] == '\0')
    {
        LOGE("table_del_row_rbus_cb: failed to extract table path from: %s", rowName);
        return RBUS_ERROR_INVALID_INPUT;
    }

    table = find_table_entry(os_handle, table_path);
    if (!table)
    {
        LOGE("table_del_row_rbus_cb: table not found: %s", table_path);
        return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    if (table->del_cb)
    {
        rbus_lock_main(os_handle);
        os_ret = table->del_cb(table_path, inst_num, table->user_data);
        rbus_unlock_main(os_handle);
    }

    if (os_ret != OS_TR181_SUCCESS)
    {
        LOGE("table_del_row_rbus_cb: del_cb failed for %s: %s", rowName, os_tr181_error_string(os_ret));
        return RBUS_ERROR_BUS_ERROR;
    }

    remove_instance_from_table(table, table_path, inst_num);

    /* Clean up all PSM records for the deleted instance */
    psm_delete_instance(os_handle, table, rowName);

    LOGD("table_del_row_rbus_cb: deleted %s", rowName);
    return RBUS_ERROR_SUCCESS;
}

/* Callback for handling method invocation requests from RBUS */
static rbusError_t method_invoke_cb(
        rbusHandle_t handle,
        char const *methodName,
        rbusObject_t inParams,
        rbusObject_t outParams,
        rbusMethodAsyncHandle_t asyncHandle)
{
    struct method_entry *me = NULL;
    os_tr181_handle_t *os_handle;
    os_tr181_val_t input_args = OS_VAL_INIT();
    os_tr181_val_t result = OS_VAL_INIT();
    os_tr181_error_t os_ret;
    rbusError_t rbus_err = RBUS_ERROR_SUCCESS;
    rbusValue_t result_val = NULL;

    (void)asyncHandle; /* Not using async responses for now */

    LOGD("Method invocation callback: methodName=%s", methodName);

    /* Find our handle by rbus handle (RBUS doesn't pass user context) */
    os_handle = find_handle_by_rbus_handle(handle);
    if (!os_handle)
    {
        LOGE("Handle not found in method callback");
        return RBUS_ERROR_INVALID_HANDLE;
    }

    /* Find the method_entry by path */
    me = find_registered_method(os_handle, methodName);

    if (!me || !me->method_cb)
    {
        LOGE("Method callback not found for: %s", methodName);
        return RBUS_ERROR_ELEMENT_DOES_NOT_EXIST;
    }

    LOGD("Method invoked: %s", me->path);

    /* Convert input parameters from rbusObject_t to os_tr181_val_t */
    if (inParams)
    {
        rbusValue_t in_val = rbusObject_GetValue(inParams, NULL);
        if (in_val)
        {
            os_ret = os_tr181_val_from_rbus(&input_args, in_val);
            if (os_ret != OS_TR181_SUCCESS)
            {
                LOGE("Failed to convert input arguments");
                return RBUS_ERROR_INVALID_INPUT;
            }
        }
        else
        {
            /* If no value, convert the object itself */
            rbusValue_Init(&in_val);
            rbusValue_SetObject(in_val, inParams);
            os_ret = os_tr181_val_from_rbus(&input_args, in_val);
            rbusValue_Release(in_val);

            if (os_ret != OS_TR181_SUCCESS)
            {
                LOGE("Failed to convert input object");
                return RBUS_ERROR_INVALID_INPUT;
            }
        }
    }

    /* Create async_ctx wrapper for asyncHandle (always provided) */
    os_tr181_async_method_ctx_t *async_ctx = NULL;
    async_ctx = os_tr181_async_method_ctx_alloc(os_handle, methodName, asyncHandle);
    if (!async_ctx)
    {
        LOGE("Failed to allocate async method context");
        os_val_free(&input_args);
        return RBUS_ERROR_BUS_ERROR;
    }

    /* Call user's method callback with main thread parked */
    rbus_lock_main(os_handle);
    os_ret = me->method_cb(os_handle, me->path, &input_args, &result, async_ctx, me->user_data);
    rbus_unlock_main(os_handle);

    /* Free input args */
    os_val_free(&input_args);

    if (os_ret == OS_TR181_ERROR_DEFERRED)
    {
        /* User will call os_tr181_method_respond later with async_ctx */
        /* Don't free async_ctx - user owns it now */
        LOGD("Method deferred, async response will be sent later");
        return RBUS_ERROR_ASYNC_RESPONSE;
    }

    /* Sync response - free async_ctx since user didn't need it */
    os_tr181_async_method_ctx_free(async_ctx);

    if (os_ret != OS_TR181_SUCCESS)
    {
        LOGE("Method callback failed: %s", os_tr181_error_string(os_ret));
        os_val_free(&result);
        /* Convert error code to rbusError_t */
        return os_ret == OS_TR181_ERROR_NOT_FOUND ? RBUS_ERROR_ELEMENT_DOES_NOT_EXIST
               : os_ret == OS_TR181_ERROR_INVALID ? RBUS_ERROR_INVALID_INPUT
                                                  : RBUS_ERROR_BUS_ERROR;
    }

    /* Convert result from os_tr181_val_t to rbusObject_t */
    if (result.type != OS_TR181_TYPE_NONE && outParams)
    {
        os_ret = os_tr181_val_to_rbus(&result, &result_val);
        if (os_ret != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert result");
            os_val_free(&result);
            return RBUS_ERROR_INVALID_RESPONSE_FROM_DESTINATION;
        }

        /* If result is an object, copy its properties to outParams */
        if (rbusValue_GetType(result_val) == RBUS_OBJECT)
        {
            rbusObject_t result_obj = rbusValue_GetObject(result_val);
            if (result_obj)
            {
                /* Iterate through all properties and copy to outParams */
                rbusProperty_t prop = rbusObject_GetProperties(result_obj);
                while (prop)
                {
                    const char *prop_name = rbusProperty_GetName(prop);
                    rbusValue_t prop_val = rbusProperty_GetValue(prop);
                    if (prop_name && prop_val)
                    {
                        rbusObject_SetValue(outParams, prop_name, prop_val);
                    }
                    prop = rbusProperty_GetNext(prop);
                }
            }
        }
        else
        {
            /* Single value result - set as unnamed value */
            rbusObject_SetValue(outParams, "value", result_val);
        }

        rbusValue_Release(result_val);
    }

    os_val_free(&result);
    return rbus_err;
}

os_tr181_error_t os_tr181_register_method(
        os_tr181_handle_t *handle,
        const char *path,
        os_tr181_method_cb_t method_cb,
        void *user_data,
        const os_tr181_param_schema_t *params,
        unsigned int flags)
{
    struct method_entry *method;

    /* params and flags are ignored: rbus has no method
     * parameter schema or equivalent attribute concept */
    (void)params;
    (void)flags;

    if (!handle || !path || !method_cb)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Check if already registered */
    for (method = handle->methods; method != NULL; method = method->next)
    {
        if (strcmp(method->path, path) == 0)
        {
            LOGE("Method already registered: %s", path);
            return OS_TR181_ERROR;
        }
    }

    /* Create and store method entry */
    method = calloc(1, sizeof(struct method_entry));
    if (!method)
    {
        return OS_TR181_ERROR;
    }

    /* Register method path as-is (user controls "()" usage) */
    method->path = strdup(path);

    if (!method->path)
    {
        free(method);
        return OS_TR181_ERROR;
    }

    method->method_cb = method_cb;
    method->user_data = user_data;
    method->handle = handle;

    /* Append to maintain registration order */
    if (handle->methods == NULL)
    {
        handle->methods = method;
    }
    else
    {
        struct method_entry *last = handle->methods;
        while (last->next)
            last = last->next;
        last->next = method;
    }

    LOGD("Method registered: %s (will be published in os_tr181_publish_objects)", path);
    return OS_TR181_SUCCESS;
}

/*
 * rbus event subscription handler — called when consumers subscribe/unsubscribe.
 * We accept all subscriptions unconditionally.
 */
static rbusError_t event_sub_handler(
        rbusHandle_t rbus_handle,
        rbusEventSubAction_t action,
        const char *event_name,
        rbusFilter_t filter,
        int32_t interval,
        bool *auto_publish)
{
    (void)rbus_handle;
    (void)filter;
    (void)interval;
    if (auto_publish) *auto_publish = false;
    LOGD("Event subscription %s: %s", action == RBUS_EVENT_ACTION_SUBSCRIBE ? "add" : "remove", event_name);
    return RBUS_ERROR_SUCCESS;
}

os_tr181_error_t os_tr181_register_event(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_param_schema_t *params)
{
    struct event_entry *event;

    /* rbus has no event argument schema concept */
    (void)params;

    if (!handle || !path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Check if already registered */
    for (event = handle->events; event != NULL; event = event->next)
    {
        if (strcmp(event->path, path) == 0)
        {
            LOGE("Event already registered: %s", path);
            return OS_TR181_ERROR;
        }
    }

    event = calloc(1, sizeof(struct event_entry));
    if (!event) return OS_TR181_ERROR;

    event->path = strdup(path);
    if (!event->path)
    {
        free(event);
        return OS_TR181_ERROR;
    }

    /* Append to maintain registration order */
    if (handle->events == NULL)
    {
        handle->events = event;
    }
    else
    {
        struct event_entry *last = handle->events;
        while (last->next)
            last = last->next;
        last->next = event;
    }

    LOGD("Event registered: %s (will be published in os_tr181_publish_objects)", path);
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_emit_event(os_tr181_handle_t *handle, const char *path, const os_tr181_val_t *data)
{
    CCSP_MESSAGE_BUS_INFO *bus_info;
    rbusObject_t event_obj = NULL;
    rbusEvent_t rbus_event;
    rbusError_t rbus_err;

    if (!handle || !path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;
    if (!bus_info || !bus_info->rbus_handle)
    {
        LOGE("RBUS handle not available");
        return OS_TR181_ERROR_INIT;
    }

    rbusObject_Init(&event_obj, NULL);

    if (data && data->type != OS_TR181_TYPE_DICT)
    {
        LOGE("os_tr181_emit_event: data must be a dict or NULL (got type %d)", data->type);
        rbusObject_Release(event_obj);
        return OS_TR181_ERROR_INVALID;
    }

    /* Populate event object from dict entries */
    if (data && data->type == OS_TR181_TYPE_DICT)
    {
        os_val_iter_t it;
        const char *key;
        os_tr181_val_t *entry_val;

        os_val_foreach_dict(it, key, entry_val, (os_tr181_val_t *)data)
        {
            rbusValue_t rval;
            if (os_tr181_val_to_rbus(entry_val, &rval) == OS_TR181_SUCCESS)
            {
                rbusObject_SetValue(event_obj, key, rval);
                rbusValue_Release(rval);
            }
        }
    }

    memset(&rbus_event, 0, sizeof(rbus_event));
    rbus_event.name = path;
    rbus_event.type = RBUS_EVENT_GENERAL;
    rbus_event.data = event_obj;

    rbus_err = rbusEvent_Publish(bus_info->rbus_handle, &rbus_event);
    rbusObject_Release(event_obj);

    if (rbus_err == RBUS_ERROR_NOSUBSCRIBERS)
    {
        LOGD("Event emitted (no subscribers): %s", path);
        return OS_TR181_SUCCESS;
    }

    if (rbus_err != RBUS_ERROR_SUCCESS)
    {
        LOGE("rbusEvent_Publish failed for %s: %s (code %d)", path, rbusError_ToString(rbus_err), rbus_err);
        return OS_TR181_ERROR;
    }

    LOGD("Event emitted: %s", path);
    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * Helper Functions for template paths
 * ======================================================================== */

/*
 * Convert an instance path to its template form by replacing numeric instance
 * components with {i}.  For example:
 *   "Table.1.Numbers.2.Value" → "Table.{i}.Numbers.{i}.Value"
 */
static void path_to_template(const char *path, char *tmpl, size_t len)
{
    const char *src = path;
    char *dst = tmpl;
    size_t remaining = len - 1;

    while (*src && remaining > 0)
    {
        if (isdigit((unsigned char)*src))
        {
            const char *digit_start = src;
            while (isdigit((unsigned char)*src))
                src++;

            /* Replace only dot-bounded all-numeric components (e.g. ".1."),
             * not digits embedded in component names (e.g. "IPv6."). */
            bool dot_bounded = (digit_start == path || *(digit_start - 1) == '.');
            if (dot_bounded && (*src == '.' || *src == '\0'))
            {
                if (remaining >= 3)
                {
                    *dst++ = '{';
                    *dst++ = 'i';
                    *dst++ = '}';
                    remaining -= 3;
                }
                else
                    break;
            }
            else
            {
                /* Digits are part of a non-numeric name — copy verbatim */
                while (digit_start < src && remaining > 0)
                {
                    *dst++ = *digit_start++;
                    remaining--;
                }
            }
        }
        else
        {
            *dst++ = *src++;
            remaining--;
        }
    }
    *dst = '\0';
}

/* Find table entry by path (handles both template and instance paths) */
static struct table_entry *find_table_entry(os_tr181_handle_t *handle, const char *path)
{
    struct table_entry *table;

    if (!handle || !path) return NULL;

    for (table = handle->tables; table; table = table->next)
        if (strcmp(table->path, path) == 0) return table;

    /* If path contains instance numbers, retry with template path */
    if (strpbrk(path, "0123456789"))
    {
        char template_path[OS_TR181_PATH_MAX];
        path_to_template(path, template_path, sizeof(template_path));
        for (table = handle->tables; table; table = table->next)
            if (strcmp(table->path, template_path) == 0) return table;
    }

    return NULL;
}

/* Find parameter entry by path (handles both template and instance paths) */
static struct param_entry *find_param_entry(os_tr181_handle_t *handle, const char *path)
{
    struct param_entry *param;

    if (!handle || !path) return NULL;

    for (param = handle->parameters; param; param = param->next)
        if (strcmp(param->path, path) == 0) return param;

    /* If path contains instance numbers, retry with template path */
    if (strpbrk(path, "0123456789"))
    {
        char template_path[OS_TR181_PATH_MAX];
        path_to_template(path, template_path, sizeof(template_path));
        for (param = handle->parameters; param; param = param->next)
            if (strcmp(param->path, template_path) == 0) return param;
    }

    return NULL;
}
/* Find the instance set for a given parent path (read-only, returns NULL if not found) */
static struct table_instance_set *find_instance_set(struct table_entry *table, const char *parent_path)
{
    struct table_instance_set *iset;

    for (iset = table->instance_sets; iset != NULL; iset = iset->next)
    {
        if (strcmp(iset->parent_path, parent_path) == 0) return iset;
    }
    return NULL;
}

/* Find or create the instance set for a given parent path */
static struct table_instance_set *find_or_create_instance_set(struct table_entry *table, const char *parent_path)
{
    struct table_instance_set *iset = find_instance_set(table, parent_path);

    if (iset) return iset;

    iset = calloc(1, sizeof(struct table_instance_set));
    if (!iset) return NULL;

    iset->parent_path = strdup(parent_path);
    if (!iset->parent_path)
    {
        free(iset);
        return NULL;
    }

    iset->next = table->instance_sets;
    table->instance_sets = iset;
    return iset;
}

static void add_instance_to_table(struct table_entry *table, const char *parent_path, int inst_num)
{
    struct table_instance_set *iset;
    int i;

    if (!table || !parent_path) return;

    iset = find_or_create_instance_set(table, parent_path);
    if (!iset)
    {
        LOGE("Failed to get instance set for %s", parent_path);
        return;
    }

    /* Check if already exists */
    for (i = 0; i < iset->instance_count; i++)
    {
        if (iset->instances[i] == inst_num)
        {
            LOGW("Instance %d already tracked in %s", inst_num, parent_path);
            return;
        }
    }

    /* Expand array if needed */
    if (iset->instance_count >= iset->instance_capacity)
    {
        int new_capacity = iset->instance_capacity == 0 ? 8 : iset->instance_capacity * 2;
        int *new_array = realloc(iset->instances, new_capacity * sizeof(int));

        if (!new_array)
        {
            LOGE("Failed to expand instance array for %s", parent_path);
            return;
        }

        iset->instances = new_array;
        iset->instance_capacity = new_capacity;
    }

    iset->instances[iset->instance_count++] = inst_num;
    LOGD("Tracked instance %d in %s (count=%d)", inst_num, parent_path, iset->instance_count);
}

static void remove_instance_from_table(struct table_entry *table, const char *parent_path, int inst_num)
{
    struct table_instance_set *iset;
    struct table_instance_set *prev = NULL;
    int i;

    if (!table || !parent_path) return;

    /* Find iset and its predecessor for potential unlink */
    for (iset = table->instance_sets; iset != NULL; prev = iset, iset = iset->next)
    {
        if (strcmp(iset->parent_path, parent_path) == 0) break;
    }

    if (!iset)
    {
        LOGW("Instance set not found for %s", parent_path);
        return;
    }

    for (i = 0; i < iset->instance_count; i++)
    {
        if (iset->instances[i] == inst_num)
        {
            memmove(&iset->instances[i], &iset->instances[i + 1], (iset->instance_count - i - 1) * sizeof(int));
            iset->instance_count--;
            LOGD("Removed instance %d from %s (count=%d)", inst_num, parent_path, iset->instance_count);

            /* Free the set when it becomes empty — the parent row has been deleted
             * and will never be repopulated, so reclaim the memory now. */
            if (iset->instance_count == 0)
            {
                if (prev)
                    prev->next = iset->next;
                else
                    table->instance_sets = iset->next;
                free(iset->instances);
                free(iset->parent_path);
                free(iset);
                LOGD("Freed empty instance set for %s", parent_path);
            }
            return;
        }
    }

    LOGW("Instance %d not found in %s", inst_num, parent_path);
}

/* Extract table path from template parameter path
 * "Device.X_DEMO.Sample.Table.{i}.Name" → "Device.X_DEMO.Sample.Table."
 */
static void extract_table_path(const char *inst_path, char *table_path, size_t len)
{
    const char *last_dot;
    const char *ptr;
    size_t table_len;

    if (!inst_path || !table_path || len == 0)
    {
        if (table_path && len > 0)
        {
            table_path[0] = '\0';
        }
        return;
    }

    /* Find last dot */
    last_dot = strrchr(inst_path, '.');
    if (!last_dot || last_dot == inst_path)
    {
        table_path[0] = '\0';
        return;
    }

    /* Walk backwards from last_dot to find the instance number */
    ptr = last_dot - 1;
    while (ptr > inst_path && isdigit(*ptr))
    {
        ptr--;
    }

    /* ptr should now point to the dot before the instance number */
    if (*ptr == '.' && ptr < last_dot - 1)
    {
        table_len = (ptr - inst_path) + 1;
        if (table_len < len)
        {
            memcpy(table_path, inst_path, table_len);
            table_path[table_len] = '\0';
            return;
        }
    }

    table_path[0] = '\0';
}

/* ---------------------------------------------------------------------------
 * PSM (Persistent Storage Manager) helpers
 *
 * "dmsb" (Data Model Storage Backend) is the conventional RDK-B namespace
 * prefix for TR-181 data model values stored in PSM.  It replaces the
 * "Device." root, distinguishing data model records from other PSM keys
 * (e.g. component metadata stored under "eRT.com.cisco.spvtg.ccsp.*").
 * --------------------------------------------------------------------------- */

/* Derive a PSM record key from a TR-181 parameter path.
 * Strips the "Device." prefix and prepends "dmsb.", preserving original casing.
 * Example: "Device.X_DEMO.Sample.Text" -> "dmsb.X_DEMO.Sample.Text"
 * Returns false if buf is too small. */
static bool psm_key_for_path(const char *tr181_path, char *buf, size_t bufsz)
{
    const char *src = tr181_path;
    if (strncmp(src, "Device.", 7) == 0) src += 7;
    if (snprintf(buf, bufsz, "dmsb.%s", src) >= (int)bufsz) return false;
    return true;
}

/* Write a parameter value to PSM after a successful set. */
static void psm_persist_value(
        os_tr181_handle_t *handle,
        const char *param_path,
        os_tr181_param_type_t type,
        const os_tr181_val_t *val)
{
    char psm_key[256];
    char *val_str = NULL;

    if (!psm_key_for_path(param_path, psm_key, sizeof(psm_key)))
    {
        LOGW("psm_persist: PSM key too long for %s, skipping", param_path);
        return;
    }

    if (os_val_to_str(val, &val_str) != OS_TR181_SUCCESS)
    {
        LOGW("psm_persist: value to string failed for %s", param_path);
        return;
    }

    int rc = PSM_Set_Record_Value2(
            handle->bus_handle,
            SUBSYSTEM_PREFIX,
            psm_key,
            (unsigned int)os_tr181_type_to_ccsp(type),
            val_str);
    if (rc != CCSP_SUCCESS)
        LOGW("psm_persist: PSM write failed for %s: %d", param_path, rc);
    else
        LOGD("psm_persist: saved %s = %s", param_path, val_str);

    free(val_str);
}

/*
 * Restore a single persistent param by concrete path (no {i}) from PSM.
 * Called from psm_restore_table_at for each instance's direct params.
 */
static void psm_restore_param(os_tr181_handle_t *handle, struct param_entry *param, const char *concrete_path)
{
    CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;
    char psm_key[256];

    if (!psm_key_for_path(concrete_path, psm_key, sizeof(psm_key))) return;

    char *psm_val = NULL;
    unsigned int psm_type = 0;
    int rc = PSM_Get_Record_Value2(handle->bus_handle, SUBSYSTEM_PREFIX, psm_key, &psm_type, &psm_val);
    if (rc != CCSP_SUCCESS || psm_val == NULL)
    {
        LOGD("psm_restore: no saved value for %s (rc=%d)", concrete_path, rc);
        return;
    }

    os_tr181_val_t val = OS_VAL_INIT();
    if (os_val_from_str(&val, psm_val, param->type) == OS_TR181_SUCCESS)
    {
        os_tr181_error_t set_ret = param->set_cb(concrete_path, &val, param->user_data);
        if (set_ret != OS_TR181_SUCCESS)
            LOGW("psm_restore: restore set_cb failed for %s: %s", concrete_path, os_tr181_error_string(set_ret));
        else
            LOGD("psm_restore: restored %s = %s", concrete_path, psm_val);
        os_val_free(&val);
    }
    else
    {
        LOGW("psm_restore: value conversion failed for %s (psm_val=%s)", concrete_path, psm_val);
    }

    bus_info->freefunc(psm_val);
}

/*
 * Check if a template parameter path is a direct child of the given instance prefix.
 * "Direct child" means: after replacing the template prefix with the concrete instance
 * prefix, the remaining suffix contains no further {i} markers (i.e. not a deeper table).
 *
 * Examples (concrete_inst_prefix = "Device.X_DEMO.Sample.Table.1."):
 *   "Device.X_DEMO.Sample.Table.{i}.Name"           → true,  concrete = "...Table.1.Name"
 *   "Device.X_DEMO.Sample.Table.{i}.Numbers.{i}.Val" → false (deeper {i} in suffix)
 *
 * On success returns true and writes the concrete param path into *out_buf.
 */
static bool expand_param_for_instance(
        const char *param_template,
        const char *concrete_inst_prefix,
        char *out_buf,
        size_t out_sz)
{
    char inst_tmpl[OS_TR181_PATH_MAX];

    /* Convert concrete prefix to its template form.
     * E.g. "Device.X_DEMO.Sample.Table.1." → "Device.X_DEMO.Sample.Table.{i}." */
    path_to_template(concrete_inst_prefix, inst_tmpl, sizeof(inst_tmpl));

    size_t tmpl_len = strlen(inst_tmpl);
    if (strncmp(param_template, inst_tmpl, tmpl_len) != 0) return false;

    /* Suffix after the instance template prefix, e.g. "Name" or "Numbers.{i}.Val" */
    const char *suffix = param_template + tmpl_len;

    /* Only direct children — no deeper {i} in the suffix */
    if (strstr(suffix, "{i}") != NULL) return false;

    if (snprintf(out_buf, out_sz, "%s%s", concrete_inst_prefix, suffix) >= (int)out_sz) return false;

    return true;
}

/*
 * Persist the current (default) values of all persistent params that belong to
 * a newly-created instance.  Called from table_add_row_rbus_cb after add_cb
 * succeeds so that instances added with only default values still appear in
 * PSM and are thus restored on the next restart.
 *
 * inst_path: concrete instance path with trailing dot,
 */
static void psm_persist_instance(os_tr181_handle_t *handle, struct table_entry *table, const char *inst_path)
{
    (void)table; /* path matching uses expand_param_for_instance, not table->path */

    for (struct param_entry *param = handle->parameters; param != NULL; param = param->next)
    {
        char concrete_param[OS_TR181_PATH_MAX];
        os_tr181_val_t val = OS_VAL_INIT();

        if (!param->persistent || !param->get_cb) continue;

        if (!expand_param_for_instance(param->path, inst_path, concrete_param, sizeof(concrete_param))) continue;

        os_tr181_error_t ret = param->get_cb(concrete_param, &val, param->user_data);
        if (ret != OS_TR181_SUCCESS)
        {
            LOGW("psm_persist_instance: get_cb failed for %s: %s", concrete_param, os_tr181_error_string(ret));
            continue;
        }

        psm_persist_value(handle, concrete_param, param->type, &val);
        os_val_free(&val);
    }
}

/*
 * Delete all PSM records belonging to a table instance that is being removed.
 * Handles direct params and recurses into nested sub-table instances.
 *
 * inst_path: concrete instance path with trailing dot,
 */
static void psm_delete_instance(os_tr181_handle_t *handle, struct table_entry *table, const char *inst_path)
{
    /* Part A: delete direct persistent params (and sub-object params) */
    for (struct param_entry *param = handle->parameters; param != NULL; param = param->next)
    {
        char concrete_param[OS_TR181_PATH_MAX];
        char psm_key[256];

        if (!param->persistent) continue;

        if (!expand_param_for_instance(param->path, inst_path, concrete_param, sizeof(concrete_param))) continue;

        if (!psm_key_for_path(concrete_param, psm_key, sizeof(psm_key))) continue;

        int rc = PSM_Del_Record(handle->bus_handle, SUBSYSTEM_PREFIX, psm_key);
        if (rc != CCSP_SUCCESS)
            LOGD("psm_delete_instance: no PSM record for %s (rc=%d)", concrete_param, rc);
        else
            LOGD("psm_delete_instance: deleted PSM record for %s", concrete_param);
    }

    /* Part B: recurse into nested sub-table instances stored in PSM */
    char inst_tmpl[OS_TR181_PATH_MAX];
    path_to_template(inst_path, inst_tmpl, sizeof(inst_tmpl));
    size_t tmpl_len = strlen(inst_tmpl);

    for (struct table_entry *child = handle->tables; child != NULL; child = child->next)
    {
        if (child == table) continue;

        /* Child table template must start with this instance's template prefix */
        if (strncmp(child->path, inst_tmpl, tmpl_len) != 0) continue;

        /* The child-table suffix after the instance template prefix, e.g. "Numbers." */
        const char *child_suffix = child->path + tmpl_len;

        /* Only direct children — grandchildren contain another "{i}" and are handled by recursion */
        if (strstr(child_suffix, "{i}") != NULL) continue;

        /* Build concrete child table base path for the direct child: inst_path + child_suffix */
        char concrete_child_table[OS_TR181_PATH_MAX];
        snprintf(concrete_child_table, sizeof(concrete_child_table), "%s%s", inst_path, child_suffix);

        /* Enumerate sub-instances from PSM and recurse */
        char psm_child_key[256];
        if (!psm_key_for_path(concrete_child_table, psm_child_key, sizeof(psm_child_key))) continue;

        unsigned int sub_count = 0;
        unsigned int *sub_array = NULL;
        int rc = PsmGetNextLevelInstances(handle->bus_handle, SUBSYSTEM_PREFIX, psm_child_key, &sub_count, &sub_array);
        if (rc != CCSP_SUCCESS || sub_array == NULL || sub_count == 0) continue;

        CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;

        for (unsigned int j = 0; j < sub_count; j++)
        {
            char sub_inst_path[OS_TR181_PATH_MAX + 16]; /* +16: room for inst-num and trailing dot */
            snprintf(sub_inst_path, sizeof(sub_inst_path), "%s%u.", concrete_child_table, sub_array[j]);
            psm_delete_instance(handle, child, sub_inst_path);
        }

        bus_info->freefunc(sub_array);
    }
}

/*
 * Recursively restore instances (and their params) for one table at a concrete
 * table path.  Handles both top-level and nested tables.
 *
 * concrete_table_path: fully-resolved path with no {i}, e.g.:
 *   "Device.X_DEMO.Sample.Table."
 *   "Device.X_DEMO.Sample.Table.1.Numbers."
 */
static void psm_restore_table_at(os_tr181_handle_t *handle, struct table_entry *table, const char *concrete_table_path)
{
    char inst_prefix[OS_TR181_PATH_MAX];

    /*
     * Use PsmGetNextLevelInstances to enumerate instances stored in PSM.
     * In the rbus implementation this invokes GetPSMRecordName() directly on
     * the PSM component, which queries the XML data store rather than the
     * rbus element tree — so it correctly sees dmsb.* key-value records.
     */
    char psm_table_key[256];
    if (!psm_key_for_path(concrete_table_path, psm_table_key, sizeof(psm_table_key)))
    {
        LOGW("psm_restore: PSM key too long for %s, skipping", concrete_table_path);
        return;
    }

    unsigned int inst_count = 0;
    unsigned int *inst_array = NULL;
    int rc = PsmGetNextLevelInstances(handle->bus_handle, SUBSYSTEM_PREFIX, psm_table_key, &inst_count, &inst_array);
    if (rc != CCSP_SUCCESS || inst_array == NULL || inst_count == 0)
    {
        LOGD("psm_restore: no instances found under %s (rc=%d)", concrete_table_path, rc);
        return;
    }

    CCSP_MESSAGE_BUS_INFO *bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;

    for (unsigned int i = 0; i < inst_count; i++)
    {
        unsigned int inst_num = inst_array[i];

        snprintf(inst_prefix, sizeof(inst_prefix), "%s%u.", concrete_table_path, inst_num);

        /* 1. Notify app: recreate this instance (mirrors table_add_row_rbus_cb) */
        os_tr181_error_t ret = table->add_cb(concrete_table_path, (int)inst_num, NULL, table->user_data);
        if (ret != OS_TR181_SUCCESS)
        {
            LOGW("psm_restore: app rejected instance %s%u (%s) — removing from PSM",
                 concrete_table_path,
                 inst_num,
                 os_tr181_error_string(ret));
            psm_delete_instance(handle, table, inst_prefix);
            continue;
        }

        /* 2. Register the row with rbus so consumers can see it */
        rbusError_t rbus_err = rbusTable_registerRow(handle->rbus_handle, concrete_table_path, inst_num, NULL);
        if (rbus_err != RBUS_ERROR_SUCCESS)
        {
            LOGW("psm_restore: failed to register %s%u on bus (%d) — discarding instance",
                 concrete_table_path,
                 inst_num,
                 rbus_err);
            /* Undo app state created above and purge PSM so it won't reappear on restart */
            if (table->del_cb) table->del_cb(concrete_table_path, (int)inst_num, table->user_data);
            psm_delete_instance(handle, table, inst_prefix);
            continue;
        }

        /* 3. Track instance in our internal table_instance_set */
        add_instance_to_table(table, concrete_table_path, (int)inst_num);
        {
            struct table_instance_set *iset = find_instance_set(table, concrete_table_path);
            if (iset && (int)inst_num > iset->last_index) iset->last_index = (int)inst_num;
        }

        LOGD("psm_restore: recreated instance %s", inst_prefix);

        /* 4. Restore this instance's direct persistent params from PSM */
        for (struct param_entry *param = handle->parameters; param != NULL; param = param->next)
        {
            char concrete_param[OS_TR181_PATH_MAX];

            if (!param->persistent || !param->set_cb) continue;

            if (!expand_param_for_instance(param->path, inst_prefix, concrete_param, sizeof(concrete_param))) continue;

            psm_restore_param(handle, param, concrete_param);
        }

        /* 5. Recurse into child tables registered under this instance */
        char inst_tmpl[OS_TR181_PATH_MAX];
        path_to_template(inst_prefix, inst_tmpl, sizeof(inst_tmpl));
        size_t tmpl_len = strlen(inst_tmpl);

        for (struct table_entry *child = handle->tables; child != NULL; child = child->next)
        {
            if (child == table) continue;

            /* Child table template must start with this instance's template prefix.
             * E.g. inst_tmpl = "Device.X_DEMO.Sample.Table.{i}."
             *      child->path = "Device.X_DEMO.Sample.Table.{i}.Numbers." → match */
            if (strncmp(child->path, inst_tmpl, tmpl_len) != 0) continue;

            /* The child table suffix after the instance template */
            const char *child_suffix = child->path + tmpl_len;

            /* Skip if suffix itself has {i} — that means it's a grandchild table
             * that will be handled by the recursive call for the child */
            if (strstr(child_suffix, "{i}") != NULL) continue;

            /* Build concrete child table path */
            char concrete_child[OS_TR181_PATH_MAX];
            snprintf(concrete_child, sizeof(concrete_child), "%s%s", inst_prefix, child_suffix);

            psm_restore_table_at(handle, child, concrete_child);
        }
    } /* for each instance from PsmGetNextLevelInstances */

    bus_info->freefunc(inst_array);
}

/* Restore all persistent state from PSM: table instances first (so that
 * instance-specific params can be set), then scalar params. */
static void psm_restore(os_tr181_handle_t *handle)
{
    /* Top-level tables only; nested tables handled recursively by psm_restore_table_at(). */
    for (struct table_entry *table = handle->tables; table != NULL; table = table->next)
    {
        if (strstr(table->path, "{i}") != NULL) continue; /* nested — handled recursively */
        psm_restore_table_at(handle, table, table->path);
    }

    /* Restore persistent scalar parameter values from PSM. */
    for (struct param_entry *param = handle->parameters; param != NULL; param = param->next)
    {
        if (!param->persistent || !param->set_cb) continue;

        /* Skip template params (e.g. "...Table.{i}.Name") — those are restored
         * with their instances above. */
        if (strstr(param->path, "{i}") != NULL) continue;

        psm_restore_param(handle, param, param->path);
    }
}

os_tr181_error_t os_tr181_publish_objects(os_tr181_handle_t *handle)
{
    struct param_entry *param;
    struct table_entry *table;
    rbusDataElement_t el;
    rbusError_t rbus_err;

    if (!handle || !handle->bus_handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (handle->published)
    {
        LOGE("Objects already published - multiple publish calls not supported");
        return OS_TR181_ERROR;
    }

    /* Register tables FIRST in the native rbus element tree.
     *
     * Tables MUST precede their child params because rbus insertElement() for
     * RBUS_ELEMENT_TYPE_TABLE creates the {i} row-template node (and sets
     * currentNode->child = rowTemplate), which is the parent all child params
     * are inserted under.  If params were registered first the subsequent table
     * registration would overwrite currentNode->child, detaching the already-
     * inserted param nodes from the tree. */
    for (table = handle->tables; table != NULL; table = table->next)
    {
        memset(&el, 0, sizeof(el));
        el.name = table->path;
        el.type = RBUS_ELEMENT_TYPE_TABLE;
        el.cbTable.tableAddRowHandler = table_add_row_rbus_cb;
        el.cbTable.tableRemoveRowHandler = table_del_row_rbus_cb;
        rbus_err = rbus_regDataElements(handle->rbus_handle, 1, &el);
        if (rbus_err != RBUS_ERROR_SUCCESS)
        {
            LOGE("rbus_regDataElements failed for table %s: %d", table->path, rbus_err);
            return OS_TR181_ERROR;
        }
        LOGD("Registered native rbus table: %s", table->path);
    }

    /* Register params after tables so child params land under the {i} row-template
     * nodes already created above. */
    for (param = handle->parameters; param != NULL; param = param->next)
    {
        memset(&el, 0, sizeof(el));
        el.name = param->path;
        el.type = RBUS_ELEMENT_TYPE_PROPERTY;
        el.cbTable.getHandler = param_get_rbus_cb;
        if (param->set_cb) el.cbTable.setHandler = param_set_rbus_cb;
        rbus_err = rbus_regDataElements(handle->rbus_handle, 1, &el);
        if (rbus_err != RBUS_ERROR_SUCCESS)
        {
            LOGE("rbus_regDataElements failed for param %s: %d", param->path, rbus_err);
            return OS_TR181_ERROR;
        }
        LOGD("Registered native rbus param: %s", param->path);
    }

    /* Register methods after tables+params so method nodes land under the correct
     * {i} subtree (not orphaned by a later table overwriting currentNode->child). */
    for (struct method_entry *method = handle->methods; method != NULL; method = method->next)
    {
        memset(&el, 0, sizeof(el));
        el.name = method->path;
        el.type = RBUS_ELEMENT_TYPE_METHOD;
        el.cbTable.methodHandler = method_invoke_cb;
        rbus_err = rbus_regDataElements(handle->rbus_handle, 1, &el);
        if (rbus_err != RBUS_ERROR_SUCCESS)
        {
            LOGE("rbus_regDataElements failed for method %s: %d", method->path, rbus_err);
            return OS_TR181_ERROR;
        }
        LOGD("Registered native rbus method: %s", method->path);
    }

    /* Register events after tables+params for the same reason. */
    for (struct event_entry *event = handle->events; event != NULL; event = event->next)
    {
        memset(&el, 0, sizeof(el));
        el.name = event->path;
        el.type = RBUS_ELEMENT_TYPE_EVENT;
        el.cbTable.eventSubHandler = event_sub_handler;
        rbus_err = rbus_regDataElements(handle->rbus_handle, 1, &el);
        if (rbus_err != RBUS_ERROR_SUCCESS)
        {
            LOGE("rbus_regDataElements failed for event %s: %d", event->path, rbus_err);
            return OS_TR181_ERROR;
        }
        LOGD("Registered native rbus event: %s", event->path);
    }

    /* Register with CCSP Component Registrar for SystemReady event tracking
     * and CCSP-style component discovery. */
    {
        rbusObject_t in_params = NULL, out_params = NULL;
        rbusValue_t val;
        rbusObject_Init(&in_params, NULL);
        rbusValue_Init(&val);
        rbusValue_SetString(val, handle->component_name);
        rbusObject_SetValue(in_params, "name", val);
        rbusValue_Release(val);
        rbus_err = rbusMethod_Invoke(handle->rbus_handle, "Device.CR.RegisterComponent()", in_params, &out_params);
        rbusObject_Release(in_params);
        if (out_params) rbusObject_Release(out_params);
        if (rbus_err != RBUS_ERROR_SUCCESS)
            LOGW("CR registration failed: %d (non-fatal, rbus routing still active)", rbus_err);
        else
            LOGD("Registered with CCSP Component Registrar");
    }

    handle->published = true;

    /* Restore any persistent state from PSM now that all rbus elements
     * are registered and provider set callbacks are in place. */
    psm_restore(handle);

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
    fd_set rfds;
    struct timeval tv;

    if (!handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Wait for RBUS thread to signal via the wakeup pipe, then park this
     * (main) thread while RBUS runs its callbacks safely, then resume.
     * In libev mode this is called with timeout_ms=0 from the ev_io callback
     * after libev already detected the pipe is readable; select returns
     * immediately and we proceed straight to ccsp_main_thread_park(). */
    FD_ZERO(&rfds);
    FD_SET(handle->wakeup_pipe[0], &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(handle->wakeup_pipe[0] + 1, &rfds, NULL, NULL, &tv) > 0)
    {
        ccsp_main_thread_park(handle);
    }

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_add_instance_ex(
        os_tr181_handle_t *handle,
        const char *object_path,
        uint32_t index,
        const char *alias_value,
        const os_tr181_val_t *values,
        int *instance_number)
{
    int ret;
    componentStruct_t **components = NULL;
    int component_size = 0;
    int inst_num = 0;
    os_tr181_error_t result = OS_TR181_ERROR;
    char param_path[OS_TR181_PATH_MAX];

    /* CCSP backend does not support explicit index - always auto-assigns */
    (void)index;

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

    /* Step 1: Create instance using CCSP API */
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
    result = OS_TR181_SUCCESS;

    /* Step 2: Set Alias if provided */
    if (alias_value && alias_value[0] != '\0')
    {
        snprintf(param_path, sizeof(param_path), "%s%d.Alias", object_path, inst_num);

        os_tr181_val_t alias_val = OS_VAL_INIT();
        os_val_set_str_dup(&alias_val, alias_value);

        ret = os_tr181_set_val(handle, param_path, &alias_val);
        os_val_free(&alias_val);

        if (ret != OS_TR181_SUCCESS)
        {
            LOGW("Failed to set Alias to '%s': %s", alias_value, os_tr181_error_string(ret));
            /* Don't rollback - instance already created, Alias may have auto-value */
        }
    }

    /* Step 3: Set other initial values from dict */
    if (values && values->type == OS_TR181_TYPE_DICT)
    {
        os_val_iter_t it;
        const char *key;
        os_tr181_val_t *value;
        os_val_foreach_dict(it, key, value, (os_tr181_val_t *)values)
        {
            /* Skip Alias if we already set it via alias_value parameter */
            if (alias_value && alias_value[0] != '\0' && strcmp(key, "Alias") == 0)
            {
                continue;
            }

            snprintf(param_path, sizeof(param_path), "%s%d.%s", object_path, inst_num, key);

            ret = os_tr181_set_val(handle, param_path, value);
            if (ret != OS_TR181_SUCCESS)
            {
                LOGW("Failed to set initial value for %s: %s", param_path, os_tr181_error_string(ret));
                /* Log warning but continue with other parameters */
            }
        }
    }

    return result;
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
    result_array = calloc(inst_count, sizeof(int));
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
    char instance_path[OS_TR181_PATH_MAX];
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
    char instance_path[OS_TR181_PATH_MAX];

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

os_tr181_error_t os_tr181_invoke(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        uint32_t timeout_sec)
{
    CCSP_MESSAGE_BUS_INFO *bus_info = NULL;
    rbusObject_t inParams = NULL;
    rbusObject_t outParams = NULL;
    size_t path_len = strlen(path);
    char method_name[path_len + 3]; /* +2 for "()", +1 for '\0' */
    rbusError_t rbus_err;
    os_tr181_error_t err = OS_TR181_SUCCESS;

    (void)timeout_sec; /* RBUS doesn't support timeout in rbusMethod_Invoke */

    if (!handle || !path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;
    if (!bus_info || !bus_info->rbus_handle)
    {
        LOGE("RBUS handle not initialized");
        return OS_TR181_ERROR_INIT;
    }

    /* Flexible method name handling:
     * Try as-is first, then try alternate form (with/without "()")
     * Examples:
     *   "Device.SoftwareModules.InstallDU()" - try as-is, then without "()"
     *   "Device.WiFi.Reset" - try as-is, then with "()"
     */

    /* Copy path to stack buffer with room for "()" */
    strcpy(method_name, path);

    LOGD("Invoking method: %s", method_name);

    /* Convert arguments to rbusObject_t */
    if (args != NULL)
    {
        /* Convert dict to rbusObject */
        rbusValue_t rbus_val = NULL;
        err = os_tr181_val_to_rbus(args, &rbus_val);
        if (err != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert arguments to rbusValue_t");
            goto cleanup;
        }

        /* Extract rbusObject from rbusValue if it's an object type */
        if (rbusValue_GetType(rbus_val) == RBUS_OBJECT)
        {
            inParams = rbusValue_GetObject(rbus_val);
            rbusObject_Retain(inParams); /* Retain since we'll release rbus_val */
        }
        rbusValue_Release(rbus_val);

        if (!inParams)
        {
            LOGE("Arguments must be a dictionary/object type");
            err = OS_TR181_ERROR_INVALID;
            goto cleanup;
        }
    }
    /* else: NULL arguments are OK (no inParams) */

    /* Try invoking the method as-is */
    rbus_err = rbusMethod_Invoke(bus_info->rbus_handle, method_name, inParams, &outParams);

    /* If method not found, try alternate form (with/without "()")
     * Only retry for RBUS_ERROR_DESTINATION_NOT_FOUND (method path invalid)
     * Don't retry for other errors like RBUS_ERROR_INVALID_INPUT (wrong parameters)
     */
    if (rbus_err == RBUS_ERROR_DESTINATION_NOT_FOUND)
    {
        /* Check if ends with "()" */
        if (path_len >= 2 && strcmp(&method_name[path_len - 2], "()") == 0)
        {
            /* Remove "()" in-place and retry */
            method_name[path_len - 2] = '\0';
        }
        else
        {
            /* Add "()" in-place and retry */
            strcat(method_name, "()");
        }

        LOGD("Method not found, retrying with: %s", method_name);
        rbus_err = rbusMethod_Invoke(bus_info->rbus_handle, method_name, inParams, &outParams);
    }

    if (rbus_err != RBUS_ERROR_SUCCESS)
    {
        const char *rbus_err_str = rbusError_ToString(rbus_err);
        char *err_details = NULL;

        /* Extract error details from outParams if available */
        if (outParams != NULL)
        {
            os_tr181_val_t err_val = OS_VAL_INIT();

            if (os_tr181_val_from_rbus_object(&err_val, outParams) == OS_TR181_SUCCESS)
            {
                os_val_to_json_string(&err_val, &err_details);
                os_val_free(&err_val);
            }
        }

        LOGE("rbusMethod_Invoke failed: %s (code %d) '%s'", rbus_err_str, rbus_err, err_details ?: "");
        free(err_details);

        err = rbus_error_to_os_tr181(rbus_err);
        goto cleanup;
    }

    LOGD("Method invoked successfully");

    /* Convert result if requested */
    if (result != NULL && outParams != NULL)
    {
        err = os_tr181_val_from_rbus_object(result, outParams);

        if (err != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert result from rbusObject");
            goto cleanup;
        }
    }

cleanup:
    if (inParams) rbusObject_Release(inParams);
    if (outParams) rbusObject_Release(outParams);

    return err;
}

/* ========================================================================
 * Asynchronous Method Invocation (RBUS Backend)
 * ======================================================================== */

/**
 * Internal callback wrapper for rbusMethod_InvokeAsync
 */
static void rbus_async_method_callback(
        rbusHandle_t handle,
        char const *methodName,
        rbusError_t error,
        rbusObject_t params)
{
    os_tr181_handle_t *os_handle = NULL;
    os_tr181_async_request_t *request = NULL;
    os_tr181_val_t result = OS_VAL_INIT();
    os_tr181_error_t os_err = OS_TR181_SUCCESS;

    LOGD("Async method callback: method=%s, error=%d", methodName, error);

    /* Find our handle from RBUS handle */
    os_handle = find_handle_by_rbus_handle(handle);
    if (!os_handle)
    {
        LOGE("Failed to find os_tr181_handle from rbus handle");
        return;
    }

    rbus_lock_main(os_handle);

    /* Find and remove request from tracking list */
    request = invoke_list_find_and_remove(os_handle, methodName);
    if (!request)
    {
        LOGW("Async callback for method '%s' but no pending request found (orphaned or duplicate)", methodName);
        goto done;
    }

    /* Check if request was cancelled */
    if (request->cancelled)
    {
        LOGD("Request was cancelled, skipping callback: %s", methodName);
        os_tr181_async_request_free(request);
        request = NULL;
        goto done;
    }

    /* Convert RBUS error to os_tr181 error */
    if (error != RBUS_ERROR_SUCCESS)
    {
        LOGE("Async method failed: %s (code %d)", rbusError_ToString(error), error);
        os_err = rbus_error_to_os_tr181(error);
    }
    else if (params)
    {
        /* Convert result from rbusObject_t to os_tr181_val_t */
        os_err = os_tr181_val_from_rbus_object(&result, params);
        if (os_err != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert async result");
        }
    }

    /* Call user callback */
    if (request->callback)
    {
        LOGD("Dispatching async callback to user: method=%s, error=%d", methodName, os_err);
        request->callback(request->method_name, os_err, &result, request->priv);
    }

    /* Cleanup */
    os_val_free(&result);
    os_tr181_async_request_free(request);

done:
    rbus_unlock_main(os_handle);
}

os_tr181_error_t os_tr181_invoke_async(
        os_tr181_handle_t *handle,
        const char *method,
        const os_tr181_val_t *args,
        os_tr181_async_cb_t callback,
        void *priv,
        int timeout_sec,
        os_tr181_async_request_t **req)
{
    CCSP_MESSAGE_BUS_INFO *bus_info = NULL;
    rbusObject_t inParams = NULL;
    rbusError_t rbus_err;
    os_tr181_error_t err = OS_TR181_SUCCESS;
    os_tr181_async_request_t *request = NULL;

    if (!handle || !method || !callback)
    {
        return OS_TR181_ERROR_INVALID;
    }

    bus_info = (CCSP_MESSAGE_BUS_INFO *)handle->bus_handle;
    if (!bus_info || !bus_info->rbus_handle)
    {
        LOGE("RBUS not initialized");
        return OS_TR181_ERROR_INIT;
    }

    LOGD("Async invoke: method=%s, timeout=%d", method, timeout_sec);

    /* Allocate request structure */
    request = os_tr181_async_request_alloc(handle, method, callback, priv);
    if (!request)
    {
        return OS_TR181_ERROR;
    }

    /* Convert args to rbusObject_t if provided */
    if (args && args->type != OS_TR181_TYPE_NONE)
    {
        rbusValue_t args_val = NULL;
        err = os_tr181_val_to_rbus(args, &args_val);
        if (err != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert arguments");
            os_tr181_async_request_free(request);
            return err;
        }

        /* Wrap value in rbusObject */
        rbusObject_Init(&inParams, NULL);
        if (rbusValue_GetType(args_val) == RBUS_OBJECT)
        {
            /* If args is already an object, copy its properties */
            rbusObject_t args_obj = rbusValue_GetObject(args_val);
            rbusProperty_t prop = rbusObject_GetProperties(args_obj);
            while (prop)
            {
                rbusObject_SetValue(inParams, rbusProperty_GetName(prop), rbusProperty_GetValue(prop));
                prop = rbusProperty_GetNext(prop);
            }
        }
        else
        {
            /* Single value - set as unnamed parameter */
            rbusObject_SetValue(inParams, NULL, args_val);
        }
        rbusValue_Release(args_val);
    }
    /* else: NULL arguments are OK (no inParams) - matches sync invoke behavior */

    /* Invoke async method */
    rbus_err = rbusMethod_InvokeAsync(
            bus_info->rbus_handle,
            (char *)method, /* RBUS doesn't use const */
            inParams,       /* NULL if no args provided - matches sync behavior */
            rbus_async_method_callback,
            timeout_sec > 0 ? timeout_sec : 30 /* Default 30 sec timeout */
    );

    if (inParams)
    {
        rbusObject_Release(inParams);
    }

    if (rbus_err != RBUS_ERROR_SUCCESS)
    {
        LOGE("rbusMethod_InvokeAsync failed: %s (code %d)", rbusError_ToString(rbus_err), rbus_err);
        os_tr181_async_request_free(request);

        switch (rbus_err)
        {
            case RBUS_ERROR_ELEMENT_DOES_NOT_EXIST:
                return OS_TR181_ERROR_NOT_FOUND;
            case RBUS_ERROR_INVALID_INPUT:
                return OS_TR181_ERROR_INVALID;
            case RBUS_ERROR_TIMEOUT:
                return OS_TR181_ERROR_TIMEOUT;
            default:
                return OS_TR181_ERROR;
        }
    }

    /* Add request to tracking list for callback dispatch */
    err = invoke_list_add(handle, request);
    if (err != OS_TR181_SUCCESS)
    {
        LOGE("Failed to add request to tracking list");
        os_tr181_async_request_free(request);
        return err;
    }

    /* Store request handle if requested */
    if (req)
    {
        *req = request;
    }

    LOGD("Async invoke started successfully");
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_invoke_async_cancel(os_tr181_async_request_t *req)
{
    if (!req)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Check if already cancelled */
    if (req->cancelled)
    {
        LOGD("Request already cancelled: %s", req->method_name ?: "unknown");
        return OS_TR181_SUCCESS;
    }

    LOGD("Cancelling async request: %s", req->method_name ?: "unknown");

    /* Mark as cancelled */
    req->cancelled = true;

    /* Detach callback and priv to allow caller to free resources */
    req->callback = NULL;
    req->priv = NULL;

    /* RBUS doesn't support true cancellation, but the callback won't fire */
    /* The request remains in tracking list until backend responds */

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_method_respond(
        os_tr181_async_method_ctx_t *async_ctx,
        os_tr181_error_t error,
        os_tr181_val_t *result)
{
    rbusMethodAsyncHandle_t asyncHandle = NULL;
    rbusObject_t outParams = NULL;
    rbusError_t rbus_err;
    os_tr181_error_t os_ret = OS_TR181_SUCCESS;

    if (!async_ctx)
    {
        return OS_TR181_ERROR_INVALID;
    }

    asyncHandle = (rbusMethodAsyncHandle_t)async_ctx->platform_handle;
    if (!asyncHandle)
    {
        LOGE("Invalid async handle");
        return OS_TR181_ERROR_INVALID;
    }

    LOGD("Sending async method response: method=%s, error=%d", async_ctx->method_name ?: "unknown", error);

    /* Initialize output params */
    rbusObject_Init(&outParams, NULL);

    /* Convert result if provided and successful */
    if (error == OS_TR181_SUCCESS && result && result->type != OS_TR181_TYPE_NONE)
    {
        rbusValue_t result_val = NULL;
        os_ret = os_tr181_val_to_rbus(result, &result_val);
        if (os_ret != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert result");
            rbusObject_Release(outParams);
            os_tr181_async_method_ctx_free(async_ctx);
            return os_ret;
        }

        /* If result is an object, copy its properties to outParams */
        if (rbusValue_GetType(result_val) == RBUS_OBJECT)
        {
            rbusObject_t result_obj = rbusValue_GetObject(result_val);
            if (result_obj)
            {
                rbusProperty_t prop = rbusObject_GetProperties(result_obj);
                while (prop)
                {
                    const char *prop_name = rbusProperty_GetName(prop);
                    rbusValue_t prop_val = rbusProperty_GetValue(prop);
                    if (prop_name && prop_val)
                    {
                        rbusObject_SetValue(outParams, prop_name, prop_val);
                    }
                    prop = rbusProperty_GetNext(prop);
                }
            }
        }
        else
        {
            /* Single value result */
            rbusObject_SetValue(outParams, "value", result_val);
        }

        rbusValue_Release(result_val);
    }

    /* Send async response */
    rbus_err = rbusMethod_SendAsyncResponse(
            asyncHandle,
            error == OS_TR181_SUCCESS ? RBUS_ERROR_SUCCESS : RBUS_ERROR_BUS_ERROR,
            outParams);

    rbusObject_Release(outParams);

    if (rbus_err != RBUS_ERROR_SUCCESS)
    {
        LOGE("rbusMethod_SendAsyncResponse failed: %s (code %d)", rbusError_ToString(rbus_err), rbus_err);
        os_tr181_async_method_ctx_free(async_ctx);
        return OS_TR181_ERROR;
    }

    /* Free result if provided (ownership transferred) */
    if (result)
    {
        os_val_free(result);
    }

    /* Free async context */
    os_tr181_async_method_ctx_free(async_ctx);

    LOGD("Async response sent successfully");
    return OS_TR181_SUCCESS;
}

const char *os_tr181_get_backend_name(void)
{
    return "ccsp";
}

/* ========================================================================
 * Event Loop Integration API - CCSP Backend
 * ========================================================================
 *
 * CCSP/RBUS runs in its own internal thread. A wakeup pipe is used to
 * signal the application's event loop when RBUS activity requires the
 * main thread to park (see rbus_lock_main / ccsp_main_thread_park).
 * os_tr181_get_fds() returns the read end of this pipe; the application
 * must monitor it and call os_tr181_process_requests() when it becomes
 * readable. os_tr181_register_fd_change_callback() is a stub — the pipe
 * FD is fixed for the lifetime of the handle and never changes.
 */

/* Called from RBUS thread BEFORE invoking user callbacks.
 * Writes to the wakeup pipe (waking the main thread's select or ev_io),
 * then waits until the main thread signals that it is parked.
 * Returns with rbus_lock held — RBUS thread may now safely run callbacks. */
static void rbus_lock_main(os_tr181_handle_t *handle)
{
    ssize_t w = write(handle->wakeup_pipe[1], ".", 1);
    (void)w;
    pthread_mutex_lock(&handle->rbus_lock);
    while (!handle->rbus_main_locked)
        pthread_cond_wait(&handle->rbus_cond, &handle->rbus_lock);
    LOGT("rbus_lock_main: main thread parked");
}

/* Called from RBUS thread AFTER user callbacks complete.
 * Releases the main thread and unlocks rbus_lock. */
static void rbus_unlock_main(os_tr181_handle_t *handle)
{
    handle->rbus_main_locked = false;
    pthread_cond_signal(&handle->main_cond);
    pthread_mutex_unlock(&handle->rbus_lock);
    LOGT("rbus_unlock_main: main thread released");
}

/* Called from main thread when the wakeup pipe is readable (from select or ev_io).
 * Drains the pipe, signals the RBUS thread that it may proceed, then parks
 * until the RBUS thread completes its callbacks and calls rbus_unlock_main(). */
static void ccsp_main_thread_park(os_tr181_handle_t *handle)
{
    char buf[64];
    ssize_t r = read(handle->wakeup_pipe[0], buf, sizeof(buf));
    (void)r;
    pthread_mutex_lock(&handle->rbus_lock);
    handle->rbus_main_locked = true;
    pthread_cond_signal(&handle->rbus_cond); /* wake RBUS thread: main is parked */
    while (handle->rbus_main_locked)
        pthread_cond_wait(&handle->main_cond, &handle->rbus_lock);
    pthread_mutex_unlock(&handle->rbus_lock);
    LOGT("ccsp_main_thread_park: resumed");
}

os_tr181_error_t os_tr181_register_fd_change_callback(
        os_tr181_handle_t *handle,
        os_tr181_fd_change_callback_t callback,
        void *user_data)
{
    if (!handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Pipe FD is fixed for the handle lifetime, so this callback is never invoked */
    handle->fd_change_cb = callback;
    handle->fd_change_user_data = user_data;

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_get_fds(os_tr181_handle_t *handle, int *fds, size_t max_fds, size_t *num_fds)
{
    if (!handle || !fds || !num_fds)
    {
        return OS_TR181_ERROR_INVALID;
    }

    if (max_fds < 1)
    {
        *num_fds = 0;
        return OS_TR181_ERROR_INVALID;
    }

    /* Return the wakeup pipe read FD - written by RBUS thread via method dispatcher */
    fds[0] = handle->wakeup_pipe[0];
    *num_fds = 1;
    return OS_TR181_SUCCESS;
}

int os_tr181_get_poll_timeout(os_tr181_handle_t *handle)
{
    (void)handle;

    /* No timeout needed - RBUS manages its own timers */
    return -1;
}

/* ========================================================================
 * Event Loop Context Accessors (for os_tr181_libev.c)
 * ======================================================================== */

void *os_tr181_get_loop_ctx(os_tr181_handle_t *handle)
{
    return handle ? handle->loop_ctx : NULL;
}

void os_tr181_set_loop_ctx(os_tr181_handle_t *handle, void *ctx)
{
    if (handle)
    {
        handle->loop_ctx = ctx;
    }
}
