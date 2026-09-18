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
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <amxc/amxc.h>
#include <amxc/amxc_variant.h>
#include <amxp/amxp.h>
#include <amxd/amxd_types.h>
#include <amxd/amxd_dm.h>
#include <amxd/amxd_object.h>
#include <amxd/amxd_action.h>
#include <amxd/amxd_object_event.h>
#include <amxd/amxd_object_function.h>
#include <amxd/amxd_function.h>
#include <amxd/amxd_parameter.h>
#include <amxd/amxd_action.h>
#include <amxb/amxb.h>
#include <amxb/amxb_register.h>
#include <amxo/amxo.h>
#include <amxo/amxo_save.h>

#include "os_tr181.h"
#include "os_tr181_val_amxc.h"
#include "os_tr181_internal.h"
#include "util.h"
#include "log.h"

/*
 * Ambiorix API Version note
 *
 * The Ambiorix library changed its write-once parameter API between versions:
 *
 * OLD VERSION (PRPL QSDK):
 *   - Attribute: amxd_pattr_mutable
 *   - Semantics: mutable=false means write-once
 *   - Implementation: key=true + mutable=false
 *
 * NEW VERSION (upstream):
 *   - Attribute: amxd_pattr_write_once
 *   - Semantics: write_once=true means write-once
 *   - Implementation: key=true + write_once=true
 *
 * Detection is handled by build system which should define
 * AMXD_HAS_WRITE_ONCE=1 if amxd_pattr_write_once is available.
 * If not defined or 0, we fall back to amxd_pattr_mutable
 */

/* Bus connection URIs */
#define UBUS_SOCKET_URI           "/var/run/ubus/ubus.sock"
#define USP_BROKER_CONTROLLER_URI "usp:/var/run/usp/broker_controller_path"

/* USP reconnection parameters */
#define USP_RETRY_INITIAL_MS 2000  /* 2 seconds initial delay */
#define USP_RETRY_MAX_MS     15000 /* 15 seconds max delay */
#define USP_RETRY_TOTAL_TIME 0     /* Total retry period in ms (0 = retry indefinitely) */

/* Persistence defaults */
#define OS_TR181_PERSIST_STORAGE_DIR_DEFAULT "/etc/config/os_tr181"
#define OS_TR181_PERSIST_SAVE_DELAY_MS       1000 /* Debounce: wait 1 s after last change */
#define OS_TR181_PERSIST_INIT_DELAY_MS       5000 /* Delay first save 5 s after startup */

/* System services */
#define PROXY_MANAGER_SERVICE "ProxyManager"

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
    uint32_t access_flags;
    bool persistent; /* True if OS_TR181_ACCESS_PERSISTENT_FLAG is set */
    os_tr181_get_cb_t get_cb;
    os_tr181_set_cb_t set_cb;
    void *user_data;
    os_tr181_handle_t *handle; /* Back-reference for persistence scheduling */
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
    os_tr181_handle_t *handle; /* Back-reference */
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

struct os_tr181_handle_s
{
    amxb_bus_ctx_t *bus_ctx; /* Primary bus connection (ubus/rbus, required) */
    amxb_bus_ctx_t *usp_ctx; /* USP connection (optional, provider only) */
    amxc_var_t config;       /* Configuration stored for backend (never cleaned until close) */
    amxd_dm_t dm;
    struct subscription_entry *subscriptions;
    struct object_entry *objects;
    struct param_entry *parameters;
    struct table_entry *tables;
    struct method_entry *methods;
    bool in_callback; /* True when executing user callback functions */
    bool published;   /* True after os_tr181_publish_objects() is called */
    bool proxy_exists;

    /* USP connection state */
    bool usp_connected;        /* True if currently connected */
    bool usp_reconnect_active; /* True if reconnection logic should run */
    bool usp_config_done;      /* True after one-time config initialization */

    /* USP reconnection state */
    uint64_t usp_disconnect_time_ms; /* Timestamp when disconnect occurred (ms) */
    uint64_t usp_next_retry_time_ms; /* Timestamp when next retry should be attempted (ms) */
    uint32_t usp_retry_delay_ms;     /* Current retry delay (exponential backoff) */

    /* Event loop integration */
    os_tr181_fd_change_callback_t fd_change_cb; /* FD change notification callback */
    void *fd_change_user_data;                  /* User data for FD change callback */
    void *loop_ctx;                             /* Event loop context (struct os_tr181_libev_context*) */

    /* ODL persistence */
    struct
    {
        amxo_parser_t parser;   /* Reused across save/load calls */
        uint64_t save_due_ms;   /* Absolute time (ms) when next save fires; 0 = idle */
        char storage_dir[256];  /* Directory for .odl save files */
        uint32_t save_delay_ms; /* Debounce window after last change */
        uint32_t init_delay_ms; /* Delay before very first save */
        bool active;            /* True when at least one param is persistent */
        bool save_initialized;  /* True after the first save has fired */
        bool restoring;         /* True while amxo_parser_parse_file is running */
    } persistence;
};

enum path_drop_termination
{
    PATH_DROP_DOT,
    PATH_KEEP_DOT,
};
typedef enum path_drop_termination path_drop_termination_t;

/* Forward declarations for USP reconnection helpers */
static void usp_init_reconnection_state(os_tr181_handle_t *handle);
static int usp_load_and_config(os_tr181_handle_t *handle);
static int usp_connect_and_register(os_tr181_handle_t *handle);

/* Forward declarations for persistence helpers */
static uint64_t get_current_time_ms(void);
static void persist_config_init(os_tr181_handle_t *handle);
static void persist_do_save(os_tr181_handle_t *handle);
static void persist_schedule_save(os_tr181_handle_t *handle);
static void persist_setup(os_tr181_handle_t *handle);

static const char *g_os_tr181_ubus_backends[] = {
    "mod-amxb-ubus.so",
    "/usr/bin/mods/amxb/mod-amxb-ubus.so",
    NULL,
};

static const char *g_os_tr181_usp_backends[] = {
    "mod-amxb-usp.so",
    "/usr/bin/mods/usp/mod-amxb-usp.so",
    NULL,
};

static int os_tr181_be_load(const char *paths[])
{
    size_t i;
    int last_error = -1;
    for (i = 0; paths != NULL && paths[i] != NULL; i++)
    {
        last_error = amxb_be_load(paths[i]);
        if (last_error == 0)
        {
            LOGD("Loaded backend: %s", paths[i]);
            return 0;
        }
    }
    return last_error;
}

static bool os_tr181_usp_is_enabled(void)
{
    if (atoi(getenv("OS_TR181_DISABLE_USP") ?: "0") != 0)
    {
        LOGI("USP support disabled via OS_TR181_DISABLE_USP environment variable");
        return false;
    }

    return true;
}

/* Type translation helpers */
os_tr181_param_type_t amxc_type_to_os_tr181(uint32_t amxc_type)
{
    switch (amxc_type)
    {
        case AMXC_VAR_ID_NULL:
            return OS_TR181_TYPE_NONE;
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
        case AMXC_VAR_ID_FLOAT:
        case AMXC_VAR_ID_DOUBLE:
            return OS_TR181_TYPE_DOUBLE;
        case AMXC_VAR_ID_TIMESTAMP:
            return OS_TR181_TYPE_DATETIME;
        case AMXC_VAR_ID_HTABLE:
            return OS_TR181_TYPE_DICT;
        case AMXC_VAR_ID_LIST:
            return OS_TR181_TYPE_LIST;
        case AMXC_VAR_ID_CSTRING:
            return OS_TR181_TYPE_STRING;
        default:
            return OS_TR181_TYPE_STRING;
    }
}

uint32_t os_tr181_type_to_amxc(os_tr181_param_type_t type)
{
    switch (type)
    {
        case OS_TR181_TYPE_NONE:
            return AMXC_VAR_ID_NULL;
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
        case OS_TR181_TYPE_DOUBLE:
            return AMXC_VAR_ID_DOUBLE;
        case OS_TR181_TYPE_DATETIME:
            return AMXC_VAR_ID_TIMESTAMP;
        case OS_TR181_TYPE_STRING:
        case OS_TR181_TYPE_BASE64:
            return AMXC_VAR_ID_CSTRING;
        case OS_TR181_TYPE_DICT:
            return AMXC_VAR_ID_HTABLE;
        case OS_TR181_TYPE_LIST:
            return AMXC_VAR_ID_LIST;
        /* path types */
        case OS_TR181_TYPE_PROPERTY:
        case OS_TR181_TYPE_OBJECT:
        case OS_TR181_TYPE_TABLE:
        case OS_TR181_TYPE_INSTANCE:
        case OS_TR181_TYPE_METHOD:
        case OS_TR181_TYPE_EVENT:
            return AMXC_VAR_ID_INVALID;
    }
    return AMXC_VAR_ID_INVALID;
}

/* Helper function to map amxd_object_type_t to os_tr181_param_type_t */
static os_tr181_param_type_t amxd_object_type_to_os_tr181_type(uint32_t obj_type)
{
    switch (obj_type)
    {
        case amxd_object_template:
            return OS_TR181_TYPE_TABLE;
        case amxd_object_instance:
            return OS_TR181_TYPE_INSTANCE;
        default: /* amxd_object_singleton, amxd_object_root, amxd_object_mib */
            return OS_TR181_TYPE_OBJECT;
    }
}

/* Map amxd_status codes to os_tr181 error codes */
static os_tr181_error_t amxd_error_to_os_tr181(amxd_status_t amxd_status)
{
    switch (amxd_status)
    {
        case amxd_status_ok:
            return OS_TR181_SUCCESS;

        /* Path/object/function/parameter not found */
        case amxd_status_object_not_found:
        case amxd_status_function_not_found:
        case amxd_status_parameter_not_found:
        case amxd_status_file_not_found:
            return OS_TR181_ERROR_NOT_FOUND;

        /* Invalid input/arguments */
        case amxd_status_invalid_function:
        case amxd_status_invalid_function_argument:
        case amxd_status_invalid_name:
        case amxd_status_invalid_attr:
        case amxd_status_invalid_value:
        case amxd_status_invalid_action:
        case amxd_status_invalid_type:
        case amxd_status_invalid_arg:
        case amxd_status_invalid_path:
        case amxd_status_invalid_expr:
            return OS_TR181_ERROR_INVALID;

        /* Timeout */
        case amxd_status_timeout:
            return OS_TR181_ERROR_TIMEOUT;

        /* Not supported/implemented */
        case amxd_status_function_not_implemented:
        case amxd_status_not_supported:
            return OS_TR181_ERROR_NOT_IMPLEMENTED;

        /* Generic errors */
        case amxd_status_unknown_error:
        case amxd_status_duplicate:
        case amxd_status_deferred:
        case amxd_status_read_only:
        case amxd_status_missing_key:
        case amxd_status_out_of_mem:
        case amxd_status_recursion:
        case amxd_status_permission_denied:
        case amxd_status_not_instantiated:
        case amxd_status_not_a_template:
        default:
            return OS_TR181_ERROR;
    }
}

/* Map os_tr181 error codes to amxd_status codes (reverse of above) */
static amxd_status_t os_tr181_to_amxd_error(os_tr181_error_t os_error)
{
    switch (os_error)
    {
        case OS_TR181_SUCCESS:
            return amxd_status_ok;

        case OS_TR181_ERROR_NOT_FOUND:
            return amxd_status_function_not_found;

        case OS_TR181_ERROR_INVALID:
            return amxd_status_invalid_function_argument;

        case OS_TR181_ERROR_TIMEOUT:
            return amxd_status_timeout;

        case OS_TR181_ERROR_NOT_IMPLEMENTED:
            return amxd_status_function_not_implemented;

        case OS_TR181_ERROR_INIT:
        case OS_TR181_ERROR_OVERFLOW:
        case OS_TR181_ERROR:
        default:
            return amxd_status_unknown_error;
    }
}

/* Convert amxc_var_t to os_tr181_val_t with automatic type conversion
 * This handles cases where USP sends numeric values as strings
 * and converts them to the registered parameter type using Ambiorix's conversion */
static os_tr181_error_t os_tr181_val_from_amx_convert_type(
        os_tr181_val_t *val,
        const amxc_var_t *var,
        os_tr181_param_type_t target_type)
{
    if (!val || !var)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Get target amxc type */
    uint32_t target_amxc_type = os_tr181_type_to_amxc(target_type);

    /* If already the right type, use direct conversion */
    if (amxc_var_type_of(var) == target_amxc_type)
    {
        return os_tr181_val_from_amx(val, var);
    }

    /* Convert using amxc_var_convert, then use standard conversion */
    amxc_var_t converted;
    amxc_var_init(&converted);

    int ret = amxc_var_convert(&converted, var, target_amxc_type);
    if (ret != 0)
    {
        LOGW("Failed to convert type %d to %d", amxc_var_type_of(var), target_amxc_type);
        amxc_var_clean(&converted);
        return OS_TR181_ERROR_INVALID;
    }

    /* Now convert the properly-typed amxc_var_t to os_tr181_val_t */
    os_tr181_error_t err = os_tr181_val_from_amx(val, &converted);
    amxc_var_clean(&converted);

    return err;
}

/* Helper function to append USP configuration with translate mapping to existing config */
/* Build USP translate config for all top-level DM objects in handle->dm.
 * Populates handle->config with translate mappings: "X_DEMO." -> "Device.X_DEMO." */
static int build_usp_config(os_tr181_handle_t *handle)
{
    amxc_var_t *usp_config = NULL;
    amxc_var_t *translate = NULL;
    amxd_object_t *root_obj;

    if (!handle)
    {
        return -1;
    }

    amxc_var_t *config = &handle->config;
    /* Ensure config is a hash table */
    if (amxc_var_type_of(config) != AMXC_VAR_ID_HTABLE)
    {
        amxc_var_set_type(config, AMXC_VAR_ID_HTABLE);
    }

    /* Get or create the "usp" section */
    usp_config = GET_ARG(config, "usp");
    if (usp_config == NULL)
    {
        usp_config = amxc_var_add_key(amxc_htable_t, config, "usp", NULL);
        if (!usp_config)
        {
            return -1;
        }
    }

    /* Get or create the "translate" section */
    translate = GET_ARG(usp_config, "translate");
    if (translate == NULL)
    {
        translate = amxc_var_add_key(amxc_htable_t, usp_config, "translate", NULL);
        if (!translate)
        {
            return -1;
        }
    }

    /* Add a translate entry for each top-level DM object */
    root_obj = amxd_dm_get_root(&handle->dm);
    amxc_llist_for_each(it, &root_obj->objects)
    {
        char local_path[OS_TR181_PATH_MAX];
        char device_path[OS_TR181_PATH_MAX];
        amxd_object_t *top = amxc_container_of(it, amxd_object_t, it);

        snprintf(local_path, sizeof(local_path), "%s.", top->name);
        snprintf(device_path, sizeof(device_path), "Device.%s.", top->name);

        if (GET_ARG(translate, local_path) == NULL)
        {
            amxc_var_add_key(cstring_t, translate, local_path, device_path);
        }
    }

    return 0;
}

/* ========================================================================
 * ProxyManager Helper Functions
 * ======================================================================== */

/* Register a Device.* proxy alias via ProxyManager
 * proxy_path: e.g., "Device.X_DEMO."
 * real_path: e.g., "X_DEMO."
 * Returns: 0 on success, -1 on failure */
static int proxy_manager_register(amxb_bus_ctx_t *bus_ctx, const char *proxy_path, const char *real_path)
{
    amxc_var_t args, result;
    int ret;

    if (!bus_ctx || !proxy_path || !real_path)
    {
        return -1;
    }

    amxc_var_init(&args);
    amxc_var_init(&result);

    amxc_var_set_type(&args, AMXC_VAR_ID_HTABLE);
    amxc_var_add_key(cstring_t, &args, "proxy", proxy_path);
    amxc_var_add_key(cstring_t, &args, "real", real_path);

    ret = amxb_call(bus_ctx, PROXY_MANAGER_SERVICE, "register", &args, &result, 5);
    if (ret == 0)
    {
        LOGI("Registered Device.* proxy: %s -> %s", proxy_path, real_path);
    }
    else
    {
        LOGW("Failed to register proxy with ProxyManager: %d (Device.* path may not work)", ret);
    }

    amxc_var_clean(&args);
    amxc_var_clean(&result);

    return ret;
}

/* Unregister a Device.* proxy alias from ProxyManager
 * proxy_path: e.g., "Device.X_DEMO."
 * Returns: 0 on success, -1 on failure */
static int proxy_manager_unregister(amxb_bus_ctx_t *bus_ctx, const char *proxy_path)
{
    amxc_var_t args, result;
    int ret;

    if (!bus_ctx || !proxy_path)
    {
        return -1;
    }

    amxc_var_init(&args);
    amxc_var_init(&result);

    amxc_var_set_type(&args, AMXC_VAR_ID_HTABLE);
    amxc_var_add_key(cstring_t, &args, "proxy", proxy_path);

    ret = amxb_call(bus_ctx, PROXY_MANAGER_SERVICE, "unregister", &args, &result, 5);
    if (ret == 0)
    {
        LOGI("Unregistered proxy from ProxyManager: %s", proxy_path);
    }
    else
    {
        LOGW("Failed to unregister proxy from ProxyManager: %d", ret);
    }

    amxc_var_clean(&args);
    amxc_var_clean(&result);

    return ret;
}

/* Register Device.* proxy aliases with ProxyManager for all top-level DM objects. */
static void proxy_manager_register_all(os_tr181_handle_t *handle)
{
    char device_path[OS_TR181_PATH_MAX];
    char real_path[OS_TR181_PATH_MAX];
    amxd_object_t *root_obj;

    if (!handle->proxy_exists)
    {
        return;
    }

    root_obj = amxd_dm_get_root(&handle->dm);
    amxc_llist_for_each(it, &root_obj->objects)
    {
        amxd_object_t *top = amxc_container_of(it, amxd_object_t, it);
        snprintf(device_path, sizeof(device_path), "Device.%s.", top->name);
        snprintf(real_path, sizeof(real_path), "%s.", top->name);
        proxy_manager_register(handle->bus_ctx, device_path, real_path);
    }
}

/* Unregister Device.* proxy aliases from ProxyManager for all top-level DM objects. */
static void proxy_manager_unregister_all(os_tr181_handle_t *handle)
{
    char device_path[OS_TR181_PATH_MAX];
    amxd_object_t *root_obj;

    if (!handle->proxy_exists)
    {
        return;
    }

    root_obj = amxd_dm_get_root(&handle->dm);
    amxc_llist_for_each(it, &root_obj->objects)
    {
        amxd_object_t *top = amxc_container_of(it, amxd_object_t, it);
        snprintf(device_path, sizeof(device_path), "Device.%s.", top->name);
        proxy_manager_unregister(handle->bus_ctx, device_path);
    }
}

/* ========================================================================
 * Path Helper Functions
 * ======================================================================== */

/* Converts, eg.
 *   "Device.X_DEMO.Numbers.{i}.Name" -> "Device.X_DEMO.Numbers.Name"
 *   "Device.X_DEMO.Numbers.{i}.Names.{i}.Enable" -> "Device.X_DEMO.Numbers.Names.Enable"
 */
static void path_make_searchable(char *path)
{
    const char *placeholder_str = ".{i}";
    const size_t placeholder_len = strlen(placeholder_str);
    char *placeholder;
    while ((placeholder = strstr(path, placeholder_str)) != NULL)
    {
        memmove(placeholder, placeholder + placeholder_len, strlen(placeholder + placeholder_len) + 1);
    }
}

/* Converts, eg.
 *
 * For PATH_DROP_DOT:
 *   "Device.X_DEMO.Sample.Enable" -> "Device.X_DEMO.Sample"
 *   "Device.X_DEMO.Sample." -> "Device.X_DEMO"
 *   "Device.X_DEMO.Samples.4.Name" -> "Device.X_DEMO.Samples.4"
 *
 * For PATH_KEEP_DOT:
 *  "Device.X_DEMO.Sample.Enable" -> "Device.X_DEMO.Sample."
 *  "Device.X_DEMO.Sample." -> "Device.X_DEMO."
 *  "Device.X_DEMO.Samples.4.Name" -> "Device.X_DEMO.Samples.4."
 */
static void path_drop_last_part(char *path, path_drop_termination_t termination)
{
    char *last_dot = strrchr(path, '.');
    if (last_dot == NULL)
    {
        return;
    }
    if (strlen(last_dot) == 0)
    {
        /* Path ends with a dot - remove it and find the previous dot */
        *last_dot = '\0';
        last_dot = strrchr(path, '.');
    }
    if (last_dot != NULL)
    {
        switch (termination)
        {
            case PATH_DROP_DOT:
                /* Eg. "Device.X_DEMO.Sample.Enable" -> "Device.X_DEMO.Sample" (drop trailing dot) */
                *last_dot = '\0';
                break;
            case PATH_KEEP_DOT:
                /* Eg. "Device.X_DEMO.Sample.Enable" -> "Device.X_DEMO.Sample." (keep trailing dot) */
                last_dot[1] = '\0';
                break;
        }
    }
}

/* Strip "Device." prefix from a path
 * Returns: pointer to path without "Device." prefix, or original path if no prefix */
static const char *path_strip_device_prefix(const char *path)
{
    if (path && strncmp(path, "Device.", 7) == 0)
    {
        return path + 7; /* Skip "Device." */
    }
    return path;
}

static bool path_is_local(os_tr181_handle_t *handle, const char *path)
{
    const char *stripped = path_strip_device_prefix(path);
    if (stripped == NULL)
    {
        return false;
    }

    char *root_name = strdup(stripped);
    if (root_name == NULL)
    {
        return false;
    }

    char *dot = strchr(root_name, '.');
    if (dot)
    {
        *dot = '\0';
    }

    amxd_object_t *obj = amxd_dm_get_object(&handle->dm, root_name);
    free(root_name);
    return obj != NULL;
}

static bool proxy_manager_check_if_exists(os_tr181_handle_t *handle)
{
    const uint32_t flags = 0;
    os_tr181_param_info_t *params = NULL;
    int count = 0;
    const os_tr181_error_t err = os_tr181_list(handle, PROXY_MANAGER_SERVICE, flags, &params, &count);
    os_tr181_free_list(params, count);

    return (err == OS_TR181_SUCCESS && count > 0);
}

/* Removes Device. prefix conditionally.
 *
 * Device. prefix is kept only if ProxyManager is present and the path
 * is to a non-local object.
 *
 * This is important because if ProxyManager is present and we want to
 * access local objects, we need to use the non-proxied path. Otherwise
 * we'll deadlock (and eventually timeout and fail) on synchronous
 * calls.
 *
 * If there's no ProxyManager then no paths can be really expected to be
 * Device. prefix accessible.
 */
static const char *device_path_fixup(os_tr181_handle_t *handle, const char *path)
{
    if (handle == NULL)
    {
        return path;
    }

    if (path == NULL)
    {
        return NULL;
    }

    if (handle->proxy_exists && !path_is_local(handle, path))
    {
        return path;
    }

    const bool path_starts_with_device = (strncmp(path, "Device.", 7) == 0);
    if (!path_starts_with_device)
    {
        return path;
    }

    return path_strip_device_prefix(path);
}

/* Convert an amxd_object_t path to a Device.* path for TR-181
 * If the object path does not start with "Device.", it will be prefixed
 * Returns: OS_TR181_SUCCESS on success, error code otherwise
 * Note: path_buf is recommended to be OS_TR181_PATH_MAX bytes */
static os_tr181_error_t path_from_amxd_object(const amxd_object_t *obj, char *path_buf, size_t path_buf_size)
{
    char *object_path_raw = amxd_object_get_path(obj, AMXD_OBJECT_INDEXED | AMXD_OBJECT_TERMINATE);
    if (object_path_raw == NULL)
    {
        LOGE("Failed to resolve object path");
        return OS_TR181_ERROR;
    }

    const char *prefix = "Device.";
    const bool prefix_needed = (strncmp(object_path_raw, prefix, strlen(prefix)) != 0);
    const ssize_t len = snprintf(path_buf, path_buf_size, "%s%s", prefix_needed ? prefix : "", object_path_raw);
    const bool truncated = (len < 0 || (size_t)len >= path_buf_size);
    if (truncated)
    {
        LOGE("Object path is too long after prefixing: %s", object_path_raw);
        free(object_path_raw);
        return OS_TR181_ERROR_INVALID;
    }
    free(object_path_raw);
    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

os_tr181_error_t os_tr181_init_ex(os_tr181_handle_t **handle, const char *component_name)
{
    /* Use calloc to zero-initialize all fields */
    os_tr181_handle_t *h = calloc(1, sizeof(os_tr181_handle_t));

    /* component_name parameter ignored on ambiorix backend */
    (void)component_name;

    if (!h)
    {
        return OS_TR181_ERROR_INIT;
    }

    /* Initialize data model */
    amxd_dm_init(&h->dm);

    /* Initialize configuration (persists for lifetime of handle) */
    amxc_var_init(&h->config);
    amxc_var_set_type(&h->config, AMXC_VAR_ID_HTABLE);

    /* Load ambiorix ubus backend */
    if (os_tr181_be_load(g_os_tr181_ubus_backends) != 0)
    {
        LOGE("Failed to load ubus backend");
        amxc_var_clean(&h->config);
        amxd_dm_clean(&h->dm);
        free(h);
        return OS_TR181_ERROR_INIT;
    }

    const char *ubus_sock_path = getenv("OS_TR181_UBUS_SOCK") ?: UBUS_SOCKET_URI;
    char uri[256];
    snprintf(uri, sizeof(uri), "ubus:%s", ubus_sock_path);

    /* Connect to primary bus (required) */
    if (amxb_connect(&h->bus_ctx, uri) != 0)
    {
        LOGE("Failed to connect to primary bus (ubus)");
        amxc_var_clean(&h->config);
        amxd_dm_clean(&h->dm);
        free(h);
        return OS_TR181_ERROR_INIT;
    }

    /* Set access level to PUBLIC to only see public functions/parameters
     * AMXB_PUBLIC  - shows only public functions
     * AMXB_PROTECTED - also shows protected functions
     */
    amxb_set_access(h->bus_ctx, AMXB_PUBLIC);

    LOGD("Connected to primary bus (ubus)");

    /* Note: USP connection will be established in os_tr181_publish_objects() */
    /* when we know the root object name for translate mapping */

    h->proxy_exists = proxy_manager_check_if_exists(h);
    LOGD("ProxyManager presence: %s", h->proxy_exists ? "detected" : "not detected");

    /* Initialize persistence config from environment or hard-coded defaults */
    persist_config_init(h);

    *handle = h;
    return OS_TR181_SUCCESS;
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

        /* Unregister proxy aliases from ProxyManager before disconnecting */
        if (handle->published && handle->bus_ctx)
        {
            proxy_manager_unregister_all(handle);
        }

        /* Persistence cleanup: flush any pending save, clean parser */
        if (handle->persistence.active && handle->published)
        {
            /* If a debounce due time is pending there are unsaved changes — flush now */
            if (handle->persistence.save_due_ms != 0) persist_do_save(handle);

            amxo_parser_clean(&handle->persistence.parser);
        }

        if (handle->bus_ctx)
        {
            amxb_disconnect(handle->bus_ctx);
            amxb_free(&handle->bus_ctx);
        }

        if (handle->usp_ctx)
        {
            amxb_disconnect(handle->usp_ctx);
            amxb_free(&handle->usp_ctx);
        }

        /* Clean up data model */
        amxd_dm_clean(&handle->dm);

        /* Clean up configuration */
        amxc_var_clean(&handle->config);

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

    param_name = device_path_fixup(handle, param_name);
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
    param_name = device_path_fixup(handle, param_name);
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
        uint32_t access_flags)
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
    cb_data->params[cb_data->count].access_flags = access_flags;
    cb_data->count++;

    return OS_TR181_SUCCESS;
}

/**
 * Convert Ambiorix parameter attributes to os_tr181 access flags
 *
 * @param param_attrs Parameter attributes from Ambiorix (may be NULL)
 * @return Access flags (OS_TR181_ACCESS_*)
 */
static uint32_t amx_attrs_to_access_flags(amxc_var_t *param_attrs)
{
    /* All parameters are readable */
    uint32_t access_flags = OS_TR181_ACCESS_READ_FLAG;

    if (param_attrs)
    {
        uint32_t read_only = GET_UINT32(param_attrs, "read-only");
        uint32_t is_key = GET_UINT32(param_attrs, "key");

        if (is_key)
        {
            /* Add key flag */
            access_flags |= OS_TR181_ACCESS_KEY_FLAG;

            /* Check write-once (runtime check with fallback) */
            amxc_var_t *write_once_var = amxc_var_get_key(param_attrs, "write-once", AMXC_VAR_FLAG_DEFAULT);
            if (write_once_var)
            {
                /* New API: use write-once attribute */
                uint32_t is_write_once = amxc_var_dyncast(uint32_t, write_once_var);
                if (is_write_once)
                {
                    access_flags |= OS_TR181_ACCESS_WRITE_ONCE_FLAG;
                }
            }
            else
            {
                /* Fallback to old API: use mutable attribute */
                amxc_var_t *mutable_var = amxc_var_get_key(param_attrs, "mutable", AMXC_VAR_FLAG_DEFAULT);
                if (mutable_var)
                {
                    uint32_t is_mutable = amxc_var_dyncast(uint32_t, mutable_var);
                    if (!is_mutable)
                    {
                        /* mutable=false means write-once */
                        access_flags |= OS_TR181_ACCESS_WRITE_ONCE_FLAG;
                    }
                }
            }
        }

        /* Check if writable (not read-only) */
        if (!read_only)
        {
            access_flags |= OS_TR181_ACCESS_WRITE_FLAG;
        }
    }
    else
    {
        /* No attributes: read-only flag not present - default to writable */
        access_flags |= OS_TR181_ACCESS_WRITE_FLAG;
    }

    return access_flags;
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

        /* Describe this object to get its parameters and functions */
        amxc_var_init(&describe_result);
        ret = amxb_describe(
                cb_data->bus_ctx,
                obj_path,
                AMXB_FLAG_PARAMETERS | AMXB_FLAG_FUNCTIONS | AMXB_FLAG_EVENTS,
                &describe_result,
                5);

        if (ret != 0)
        {
            LOGD("  amxb_describe failed for %s: %d", obj_path, ret);
            amxc_var_clean(&describe_result);
            continue;
        }

        /*amxc_var_dump_stream(&describe_result, stderr);*/

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
        ret = add_param_to_list(cb_data, obj_path, obj_type, OS_TR181_ACCESS_READWRITE);
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
                char full_path[OS_TR181_PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s%s", obj_path, param_name);

                /* Get parameter type using existing conversion function */
                uint32_t param_type_id = GET_UINT32(param_var, "type_id");
                os_tr181_param_type_t param_type = amxc_type_to_os_tr181(param_type_id);

                /* Build access flags from attributes */
                uint32_t access_flags = amx_attrs_to_access_flags(param_attrs);

                LOGD("  param: %s, type_id=%u, access_flags=0x%x", param_name, param_type_id, access_flags);

                /* Add parameter to list */
                ret = add_param_to_list(cb_data, full_path, param_type, access_flags);
                if (ret != OS_TR181_SUCCESS)
                {
                    cb_data->status = ret;
                    amxc_var_clean(&describe_result);
                    return;
                }
            }
        }

        /* Now extract all functions/methods from this object */
        amxc_var_t *functions_table = GET_ARG(obj_info, "functions");
        if (functions_table && amxc_var_type_of(functions_table) == AMXC_VAR_ID_HTABLE)
        {
            const amxc_htable_t *functions_ht = amxc_var_constcast(amxc_htable_t, functions_table);

            amxc_htable_iterate(it, functions_ht)
            {
                const char *func_name = amxc_htable_it_get_key(it);
                amxc_var_t *func_var = amxc_var_from_htable_it(it);

                if (!func_name || !func_var)
                {
                    continue;
                }

                /* Check if function is protected - skip if so */
                amxc_var_t *func_attrs = GET_ARG(func_var, "attributes");
                if (func_attrs)
                {
                    uint32_t func_protected = GET_UINT32(func_attrs, "protected");
                    if (func_protected)
                    {
                        LOGD("  function %s is protected, skipping", func_name);
                        continue;
                    }
                }

                /* Build full function path (include parentheses for methods) */
                char full_path[OS_TR181_PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s%s()", obj_path, func_name);

                LOGD("  function: %s", func_name);

                /* Add function to list with type METHOD */
                ret = add_param_to_list(cb_data, full_path, OS_TR181_TYPE_METHOD, OS_TR181_ACCESS_READONLY);
                if (ret != OS_TR181_SUCCESS)
                {
                    cb_data->status = ret;
                    amxc_var_clean(&describe_result);
                    return;
                }
            }
        }

        /* Now extract all events/signals from this object */
        amxc_var_t *events_table = GET_ARG(obj_info, "events");
        if (events_table && amxc_var_type_of(events_table) == AMXC_VAR_ID_HTABLE)
        {
            const amxc_htable_t *events_ht = amxc_var_constcast(amxc_htable_t, events_table);

            amxc_htable_iterate(it, events_ht)
            {
                const char *event_name = amxc_htable_it_get_key(it);
                if (!event_name) continue;
                /* Build full event path */
                char full_path[OS_TR181_PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s%s", obj_path, event_name);

                LOGD("  event: %s", event_name);

                ret = add_param_to_list(cb_data, full_path, OS_TR181_TYPE_EVENT, OS_TR181_ACCESS_READONLY);
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
        uint32_t list_flags,
        os_tr181_param_info_t **params,
        int *count)
{
    int ret;
    uint32_t flags;
    bool recursive = (list_flags & OS_TR181_LIST_RECURSIVE) != 0;
    /* Note: ambiorix provides accurate type info natively, DETAILS flag not needed */
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

    /* Set flags: list objects/instances (not individual parameters)
     * Note: AMXB_FLAG_FUNCTIONS is NOT used here because functions
     * are already included in amxb_describe() of parent object
     */
    flags = AMXB_FLAG_OBJECTS | AMXB_FLAG_INSTANCES;
    if (!recursive)
    {
        flags |= AMXB_FLAG_FIRST_LVL;
    }

    LOGD("amxb_list path='%s', flags=0x%x (recursive=%d)", path, flags, recursive);

    /* List objects - callback will describe each object to get parameters */
    path = device_path_fixup(handle, path);
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

    /* Check notification type to dispatch to value-change or custom event path */
    amxc_var_t *notif_var = amxc_var_get_path(data, "notification", AMXC_VAR_FLAG_DEFAULT);
    const char *notification = notif_var ? amxc_var_constcast(cstring_t, notif_var) : NULL;

    /*
     * Ambiorix internal events (value-change, instance add/remove, …) all use
     * a "dm:" prefix (e.g. "dm:object-changed", "dm:instance-added").
     * Everything else — whether it ends with "!" (USP events like
     * "SendInformMessage!") or not (platform events like "ButtonEvent") — is
     * a custom event and is delivered to the caller's callback.
     */
    bool is_dm_internal = (!notification || strncmp(notification, "dm:", 3) == 0);

    if (!is_dm_internal)
    {
        /*
         * Custom event (USP "EventName!" or platform "EventName").
         * If this subscription is for a specific parameter (filter_param set but
         * not an event name), discard custom events — the caller subscribed to
         * value-changes only, not to sibling events on the same object.
         */
        if (sub->filter_param && notification && strcmp(sub->filter_param, notification) != 0)
        {
            return;
        }

        /*
         * Build full event path: object path + notification name.
         *
         * Extract event payload.  Providers follow different conventions:
         *   - Some wrap args under a "data" key:
         *       { data={ arg1=v1, ... }, notification="...", path="..." }
         *   - Some pass args flat at the top level:
         *       { arg1=v1, notification="...", path="...", object="...", eobject="..." }
         *
         * Try "data" first (PRPL convention).  If absent, collect all
         * top-level keys except the Ambiorix metadata fields.
         */
        char full_event_path[OS_TR181_PATH_MAX];
        snprintf(full_event_path, sizeof(full_event_path), "%s%s", object_path, notification);

        os_tr181_val_t event_val = OS_VAL_INIT();

        amxc_var_t *data_var = amxc_var_get_path(data, "data", AMXC_VAR_FLAG_DEFAULT);
        if (data_var)
        {
            os_tr181_val_from_amx(&event_val, data_var);
        }
        else
        {
            /*
             * Flat format: copy all keys except the Ambiorix metadata fields
             * into a new dict and deliver that as the event value.
             */
            static const char *const meta_keys[] = {"notification", "path", "object", "eobject", NULL};
            amxc_var_t flat_args;
            amxc_var_init(&flat_args);
            amxc_var_set_type(&flat_args, AMXC_VAR_ID_HTABLE);

            const amxc_htable_t *htbl = amxc_var_constcast(amxc_htable_t, data);
            if (htbl)
            {
                amxc_htable_iterate(hit, htbl)
                {
                    const char *key = amxc_htable_it_get_key(hit);
                    bool is_meta = false;
                    for (int i = 0; meta_keys[i]; i++)
                    {
                        if (strcmp(key, meta_keys[i]) == 0)
                        {
                            is_meta = true;
                            break;
                        }
                    }
                    if (!is_meta)
                    {
                        amxc_var_t *src = amxc_var_from_htable_it(hit);
                        amxc_var_set_key(&flat_args, key, src, AMXC_VAR_FLAG_COPY);
                    }
                }
            }

            os_tr181_val_from_amx(&event_val, &flat_args);
            amxc_var_clean(&flat_args);
        }

        handle->in_callback = true;
        sub->callback(full_event_path, &event_val, sub->user_data);
        handle->in_callback = false;

        bool sub_was_marked = sub->marked_for_removal;
        cleanup_marked_subscriptions(handle);
        os_val_free(&event_val);

        if (sub_was_marked)
        {
            return;
        }
        return;
    }

    /* Value-change event (dm:object-changed) */

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
        char full_path[OS_TR181_PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s%s", object_path, param_name);

        /* Convert amxc_var_t to os_tr181_val_t */
        os_tr181_val_t val = OS_VAL_INIT();
        if (os_tr181_val_from_amx(&val, to_var) != OS_TR181_SUCCESS)
        {
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

            /* Save marked_for_removal flag before cleanup (sub may be freed) */
            bool sub_was_marked = sub->marked_for_removal;

            /* Clean up any subscriptions that were marked during callback */
            cleanup_marked_subscriptions(handle);

            /* Check if THIS subscription was removed - if so, stop processing */
            if (sub_was_marked)
            {
                os_val_free(&val);
                break;
            }
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

    path = device_path_fixup(handle, path);
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

    /* Build expression filter only for explicit USP event subscriptions (path ends with '!').
     * Parameter subscriptions must NOT use this filter: value-change notifications arrive as
     * notification="dm:object-changed", so filtering on notification=='paramname' would silently
     * drop them before subs_event_handler is ever called.  Parameter filtering is handled
     * client-side inside subs_event_handler instead. */
    const char *expression = NULL;
    char expr_buf[OS_TR181_PATH_MAX];
    size_t filter_len = sub->filter_param ? strlen(sub->filter_param) : 0;
    if (filter_len > 0 && sub->filter_param[filter_len - 1] == '!')
    {
        snprintf(expr_buf, sizeof(expr_buf), "notification == '%s'", sub->filter_param);
        expression = expr_buf;
        LOGD("Event subscription with expression: %s", expr_buf);
    }

    /* Subscribe to Ambiorix events on the object path */
    ret = amxb_subscribe(handle->bus_ctx, sub->subscribe_path, expression, subs_event_handler, sub);
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

os_tr181_error_t os_tr181_register_object(os_tr181_handle_t *handle, const char *object_path)
{
    struct object_entry *obj;
    amxd_object_t *amx_obj = NULL;
    amxd_object_t *parent_obj = NULL;
    amxd_status_t status;
    char *path_copy;
    char *token;
    char *saveptr;
    char current_path[OS_TR181_PATH_MAX] = "";
    const char *local_path;

    if (!handle || !object_path)
    {
        LOGE("Invalid parameters for register_object");
        return OS_TR181_ERROR_INVALID;
    }

    /* Strip "Device." prefix for data model - dm uses local names */
    local_path = path_strip_device_prefix(object_path);

    LOGD("Registering object: %s (dm path: %s)", object_path, local_path);

    /* Check if already registered (use original path for lookup) */
    for (obj = handle->objects; obj != NULL; obj = obj->next)
    {
        if (strcmp(obj->path, object_path) == 0)
        {
            LOGE("Object %s already registered", object_path);
            return OS_TR181_ERROR;
        }
    }

    /* Create dm object hierarchy using local path (without "Device.") */
    path_copy = strdup(local_path);
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

        /* skip instance placeholders {i} */
        do
        {
            token = strtok_r(NULL, ".", &saveptr);
        } while (token && strcmp(token, "{i}") == 0);
    }

    free(path_copy);

    /* Store in our object list */
    obj = malloc(sizeof(struct object_entry));
    if (!obj)
    {
        LOGE("Failed to allocate memory for object entry");
        return OS_TR181_ERROR;
    }

    obj->path = strdup(object_path); /* Store original path for API lookups */
    obj->amx_object = amx_obj;       /* This is the leaf object (dm uses local path) */
    obj->next = handle->objects;
    handle->objects = obj;

    LOGD("Registered object: %s (dm: %s)", object_path, local_path);

    /* Don't register with bus yet - wait until parameters are added */
    /* Registration will be done by os_tr181_publish_objects() */

    return OS_TR181_SUCCESS;
}

/* Build full parameter path for callbacks
 * Tries to build path from dm object + param name, falls back to registered path
 * full_path: output buffer for the path
 * full_path_size: size of the output buffer
 * object: dm object (may be NULL)
 * param_name: parameter name (may be NULL)
 * pe: registered parameter entry (for fallback path)
 */
static void build_full_parameter_path(
        char *full_path,
        size_t full_path_size,
        amxd_object_t *const object,
        const char *param_name,
        struct param_entry *pe)
{
    char *obj_path = NULL;

    if (object && param_name)
    {
        obj_path = amxd_object_get_path(object, AMXD_OBJECT_INDEXED | AMXD_OBJECT_TERMINATE);
    }

    if (obj_path)
    {
        /* obj_path from dm is local (e.g., "X_DEMO.Sample.")
         * prepend "Device." to get full API path */
        snprintf(full_path, full_path_size, "Device.%s%s", obj_path, param_name);
        free(obj_path);
    }
    else
    {
        /* Fallback to registered path if we can't get object path */
        snprintf(full_path, full_path_size, "%s", pe->path);
    }
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
    char full_path[OS_TR181_PATH_MAX];
    os_tr181_val_t val = OS_VAL_INIT();

    (void)args;
    (void)reason;

    LOGD("param_read_cb called for parameter: %s pe->path=%s", param_name ?: "", pe ? pe->path ?: "" : "");

    if (!pe || !pe->get_cb)
    {
        LOGD("No callback registered");
        return amxd_status_function_not_implemented;
    }

    build_full_parameter_path(full_path, sizeof(full_path), object, param_name, pe);

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
    os_tr181_val_t backup_val = OS_VAL_INIT();
    int ret;
    const char *param_name = param ? amxd_param_get_name(param) : NULL;
    char full_path[OS_TR181_PATH_MAX];
    amxd_status_t status;
    amxc_var_t backup;

    (void)reason;
    (void)retval;

    amxc_var_init(&backup);

    LOGD("param_write_cb called for parameter: %s pe->path=%s", param_name ?: "", pe ? pe->path ?: "" : "");

    if (!pe || !pe->set_cb)
    {
        status = amxd_status_read_only;
        goto exit;
    }

    build_full_parameter_path(full_path, sizeof(full_path), object, param_name, pe);

    LOGD("Writing parameter: %s", full_path);

    /* Save backup of current value for potential rollback */
    amxc_var_copy(&backup, &param->value);

    /* Convert amxc_var_t to os_tr181_val_t with automatic type conversion
     * (handles USP sending numeric values as strings) */
    if (os_tr181_val_from_amx_convert_type(&val, args, pe->type) != OS_TR181_SUCCESS)
    {
        LOGE("Failed to convert parameter value to type %d", pe->type);
        status = amxd_status_invalid_type;
        goto exit;
    }

    /* Call user's set callback first - allows app to validate and reject
     * before committing to Ambiorix storage */
    ret = pe->set_cb(full_path, &val, pe->user_data);

    if (ret != OS_TR181_SUCCESS)
    {
        LOGD("User callback rejected write: %d", ret);
        status = amxd_status_unknown_error;
        goto exit;
    }

    /* Call default handler to:
     * - Check uniqueness for key parameters
     * - Set read-only flag for write-once parameters (after first write)
     * - Store value in Ambiorix internal storage
     */
    status = amxd_action_param_write(object, param, reason, args, retval, NULL);
    if (status != amxd_status_ok)
    {
        LOGD("Default write handler rejected write: %d - rolling back app state", status);

        /* Rollback: restore app state to previous value */
        if (os_tr181_val_from_amx_convert_type(&backup_val, &backup, pe->type) == OS_TR181_SUCCESS)
        {
            LOGD("Rolling back parameter: %s", full_path);
            pe->set_cb(full_path, &backup_val, pe->user_data);
        }
        else
        {
            LOGE("Failed to convert backup value for rollback");
        }
        goto exit;
    }

    status = amxd_status_ok;

    /* Schedule a debounced save if this parameter is marked persistent */
    if (pe->persistent) persist_schedule_save(pe->handle);

exit:
    os_val_free(&val);
    os_val_free(&backup_val);
    amxc_var_clean(&backup);
    return status;
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
    amxd_object_t *obj = NULL;
    amxd_param_t *amx_param = NULL;
    amxd_status_t status;
    char *param_name;
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

    char *table_path_lookup = strdup(param_path);
    if (table_path_lookup == NULL)
    {
        free(path_copy);
        return OS_TR181_ERROR;
    }
    path_make_searchable(table_path_lookup);
    path_drop_last_part(table_path_lookup, PATH_KEEP_DOT);
    obj = amxd_object_findf(amxd_dm_get_root(&handle->dm), "%s", path_strip_device_prefix(table_path_lookup));
    free(table_path_lookup);

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

    /* Set parameter attributes based on access flags */
    if (access_flags & OS_TR181_ACCESS_KEY_FLAG)
    {
        /* Table instance key parameter - always unique per TR-181 standard */
        amxd_param_set_attr(amx_param, amxd_pattr_key, true);
        amxd_param_set_attr(amx_param, amxd_pattr_unique, true);
        LOGD("Key parameter registered: %s", param_path);

        /* Set write-once for keys if requested */
        if (access_flags & OS_TR181_ACCESS_WRITE_ONCE_FLAG)
        {
#if AMXD_HAS_WRITE_ONCE
            /* New API: write_once attribute with direct semantics */
            amxd_param_set_attr(amx_param, amxd_pattr_write_once, true);
            LOGD("  Write-once key (new API)");
#else
            /* Old API: mutable=false means write-once for keys */
            amxd_param_set_attr(amx_param, amxd_pattr_mutable, false);
            LOGD("  Write-once key (old API: mutable=false)");
#endif
        }
        /* Note: If neither READ nor WRITE flags set with KEY, parameter is effectively read-only key */
    }
    else if (!(access_flags & OS_TR181_ACCESS_WRITE_FLAG))
    {
        /* Read-only parameter (non-key) */
        amxd_param_set_attr(amx_param, amxd_pattr_read_only, true);
    }
    /* else: read-write parameter (default, no special attributes needed) */

    /* Make sure parameter is public (not private or protected) */
    amxd_param_set_attr(amx_param, amxd_pattr_private, false);
    amxd_param_set_attr(amx_param, amxd_pattr_protected, false);

    /* Mark persistent on the amxd parameter so amxo_parser_save_object includes it */
    if (access_flags & OS_TR181_ACCESS_PERSISTENT_FLAG)
    {
        amxd_param_set_attr(amx_param, amxd_pattr_persistent, true);
    }

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
    param->access_flags = access_flags;
    param->persistent = (access_flags & OS_TR181_ACCESS_PERSISTENT_FLAG) != 0;
    param->get_cb = get_callback;
    param->set_cb = set_callback;
    param->user_data = user_data;
    param->handle = handle;
    param->next = handle->parameters;
    handle->parameters = param;

    /* Note that this handle has at least one persistent parameter */
    if (param->persistent) handle->persistence.active = true;

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
    if ((access_flags & OS_TR181_ACCESS_WRITE_FLAG) && set_callback)
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
    uint32_t requested_index = 0;
    char object_path[OS_TR181_PATH_MAX];
    os_tr181_error_t err;
    int ret;

    if (!te || !te->add_cb)
    {
        return amxd_status_function_not_implemented;
    }

    err = path_from_amxd_object(object, object_path, sizeof(object_path));
    if (err != OS_TR181_SUCCESS)
    {
        LOGE("Failed to get object path");
        return os_tr181_to_amxd_error(err);
    }

    /* Extract requested index from args (0 = auto-assign) */
    requested_index = GET_UINT32(args, "index");

    /* Assign instance number: use requested index if provided, otherwise auto-assign */
    if (requested_index == 0)
    {
        /* Auto-assign: use next available index (never reuse deleted indices) */
        /* using amx object last_index tracking */
        instance_num = object->last_index + 1;
        LOGD("Auto-assigning instance number: %d", instance_num);
    }
    else
    {
        /* Client requested specific index */
        instance_num = requested_index;
        LOGD("Using client-requested instance number: %d", instance_num);
    }

    amxc_var_t *parameters = GET_ARG(args, "parameters");
    os_tr181_val_t initial_values = OS_VAL_INIT();
    if (parameters)
    {
        ret = os_tr181_val_from_amx(&initial_values, parameters);
        if (ret != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert initial parameter values from AMX");
            return amxd_status_invalid_type;
        }
    }
    else
    {
        os_val_set_dict(&initial_values);
    }

    /* The add_cb() needs to be called first because
     * amxd_action_object_add_inst() will result in initial
     * parameter set_cb() calls.
     *
     * This is necessary to meet callee's expectations of
     * instance lifecycle: add, set, set, set, ... ,del. In
     * other words set_cb() is not expected to happen
     * outside of add_cb()..del_cb().
     */
    ret = te->add_cb(object_path, instance_num, &initial_values, te->user_data);
    os_val_free(&initial_values);
    if (ret != OS_TR181_SUCCESS)
    {
        LOGD("Instance add: user callback rejected instance %s%d", object_path, instance_num);
        return amxd_status_unknown_error;
    }

    amxc_var_t args_copy;
    amxc_var_init(&args_copy);
    amxc_var_copy(&args_copy, args);
    amxc_var_t *old_index = amxc_var_get_key(&args_copy, "index", AMXC_VAR_FLAG_DEFAULT);
    amxc_var_delete(&old_index);
    amxc_var_add_key(uint32_t, &args_copy, "index", instance_num);

    ret = amxd_action_object_add_inst(object, param, reason, &args_copy, retval, priv);
    amxc_var_clean(&args_copy);

    if (ret != amxd_status_ok)
    {
        LOGD("Default add instance handler failed: %d, rolling back", ret);
        if (te->del_cb)
        {
            char inst_path[OS_TR181_PATH_MAX];
            int del_ret;

            snprintf(inst_path, sizeof(inst_path), "%s", object_path);
            LOGD("Rolling back instance addition: %s%d", inst_path, instance_num);
            del_ret = te->del_cb(inst_path, instance_num, te->user_data);
            if (del_ret != OS_TR181_SUCCESS)
            {
                LOGE("Failed to rollback instance addition: %d, probably leaked resources", del_ret);
                /* This means the callee technically has
                 * leaked resources (memory, descriptors),
                 * and we can't really do anything about it.
                 */
            }
        }
        return ret;
    }

    if (te->handle && te->handle->persistence.active) persist_schedule_save(te->handle);

    LOGD("Instance created successfully: %s%d", object_path, instance_num);
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
    char object_path[OS_TR181_PATH_MAX];
    os_tr181_error_t err;

    if (!te || !te->del_cb)
    {
        return amxd_status_function_not_implemented;
    }

    /* Extract instance number from args */
    instance_num = GET_UINT32(args, "index");

    /* Build instance path */
    err = path_from_amxd_object(object, object_path, sizeof(object_path));
    if (err != OS_TR181_SUCCESS)
    {
        LOGE("Failed to get object path");
        return os_tr181_to_amxd_error(err);
    }

    /* The order is exactly what it needs. del_cb() can
     * still refuse deletion after
     * amxd_action_object_del_inst() took action.
     *
     * If del_cb() returns an error it will cause amx to
     * abort the transaction and thus discard actions
     * implied by amxd_action_object_del_inst(). In other
     * words, if del_cb() returns an error it can withhold
     * the deletion, and the instance will be retained.
     */
    ret = amxd_action_object_del_inst(object, param, reason, args, retval, priv);
    if (ret != amxd_status_ok)
    {
        LOGD("Default delete instance handler failed: %d", ret);
        return ret;
    }

    ret = te->del_cb(object_path, instance_num, te->user_data);
    if (ret != OS_TR181_SUCCESS)
    {
        return amxd_status_unknown_error;
    }

    if (te->handle && te->handle->persistence.active) persist_schedule_save(te->handle);

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

    /* Match parent path - add trailing dot for comparison */
    char parent_with_dot[OS_TR181_PATH_MAX];
    snprintf(parent_with_dot, sizeof(parent_with_dot), "%s.", parent_path);

    /* Find the parent object */
    for (struct object_entry *oe = handle->objects; oe != NULL; oe = oe->next)
    {
        LOGT("Checking object: %s against parent: %s", oe->path, parent_with_dot);
        if (strcmp(oe->path, parent_with_dot) == 0)
        {
            parent_obj = oe->amx_object;
            LOGD("Found parent object: %s", oe->path);
            break;
        }
    }

    if (!parent_obj)
    {
        for (struct table_entry *te = handle->tables; te != NULL; te = te->next)
        {
            char table_path_indexed[OS_TR181_PATH_MAX];
            snprintf(table_path_indexed, sizeof(table_path_indexed), "%s{i}.", te->path);
            LOGT("Checking table: %s against parent: %s", table_path_indexed, parent_with_dot);
            if (strcmp(table_path_indexed, parent_with_dot) == 0)
            {
                parent_obj = te->amx_object;
                LOGD("Found parent table object: %s", te->path);
                break;
            }
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
    table->handle = handle;
    table->next = handle->tables;

    status = amxd_object_add_action_cb(amx_obj, action_object_add_inst, table_add_cb, table);
    if (status != amxd_status_ok)
    {
        LOGE("Failed to add instance add callback: %d", status);
        amxd_object_delete(&amx_obj);
        free(table);
        return OS_TR181_ERROR;
    }

    status = amxd_object_add_action_cb(amx_obj, action_object_del_inst, table_del_cb, table);
    if (status != amxd_status_ok)
    {
        LOGE("Failed to add instance add callback: %d", status);
        amxd_object_delete(&amx_obj);
        free(table);
        return OS_TR181_ERROR;
    }

    handle->tables = table;

    LOGD("Table registered successfully: %s", table_path);
    return OS_TR181_SUCCESS;
}

/* Callback for handling method invocation requests from Ambiorix */
static amxd_status_t method_invoke_cb(amxd_object_t *object, amxd_function_t *func, amxc_var_t *args, amxc_var_t *ret)
{
    struct method_entry *me = (struct method_entry *)func->priv;
    os_tr181_val_t input_args = OS_VAL_INIT();
    os_tr181_val_t result = OS_VAL_INIT();
    os_tr181_error_t os_ret;

    (void)object;

    if (!me || !me->method_cb)
    {
        const char *method_name = amxd_function_get_name(func);
        LOGE("Method callback not found for function: %s", method_name ?: "unknown");
        return amxd_status_function_not_implemented;
    }

    LOGD("Method invoked: %s", me->path);

    /* Resolve the concrete invocation path from the Ambiorix object.
     * me->path is the registered template path and may contain {i} placeholders.
     * path_from_amxd_object() uses AMXD_OBJECT_INDEXED which walks the full parent
     * chain and substitutes all instance numbers. */
    char obj_path[OS_TR181_PATH_MAX];
    char invoke_path[OS_TR181_PATH_MAX];
    if (path_from_amxd_object(object, obj_path, sizeof(obj_path)) != OS_TR181_SUCCESS)
    {
        LOGE("Method invoke: failed to resolve object path for %s", me->path);
        return amxd_status_unknown_error;
    }
    /* Build the concrete invocation path: resolved object path + method name
     * taken verbatim from me->path (preserves the "()" style of the registration). */
    const char *p = strrchr(me->path, '.');
    const char *registered_method_name = p ? p + 1 : me->path;
    snprintf(invoke_path, sizeof(invoke_path), "%s%s", obj_path, registered_method_name);
    LOGD("Method resolved to path: %s", invoke_path);

    /* Convert input arguments from amxc_var_t to os_tr181_val_t */
    if (args && amxc_var_type_of(args) != AMXC_VAR_ID_NULL)
    {
        os_ret = os_tr181_val_from_amx(&input_args, args);
        if (os_ret != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert input arguments");
            return amxd_status_invalid_type;
        }
    }

    /* Create async_ctx wrapper for deferred responses (always provided) */
    os_tr181_async_method_ctx_t *async_ctx = NULL;
    async_ctx = os_tr181_async_method_ctx_alloc(me->handle, invoke_path, (void *)func);
    if (!async_ctx)
    {
        LOGE("Failed to allocate async method context");
        os_val_free(&input_args);
        return amxd_status_unknown_error;
    }

    /* Call user's method callback */
    os_ret = me->method_cb(me->handle, invoke_path, &input_args, &result, async_ctx, me->user_data);

    /* Free input args */
    os_val_free(&input_args);

    if (os_ret == OS_TR181_ERROR_DEFERRED)
    {
        /* User will call os_tr181_method_respond() later to complete the call */
        LOGD("Method deferred: %s", me->path);

        /* Call amxd_function_defer() to get call_id */
        uint64_t call_id = 0;
        amxd_status_t amxd_ret = amxd_function_defer(func, &call_id, ret, NULL, NULL);
        if (amxd_ret != amxd_status_ok)
        {
            LOGE("amxd_function_defer failed: %d", amxd_ret);
            os_tr181_async_method_ctx_free(async_ctx);
            os_val_free(&result);
            return amxd_ret;
        }

        /* Store call_id directly in async_ctx */
        async_ctx->platform_id = call_id;

        /* Clear input arguments to prevent them from being echoed in response */
        amxc_var_clean(args);
        amxc_var_set_type(args, AMXC_VAR_ID_HTABLE);

        /* Don't free async_ctx - user owns it now and will pass to method_respond() */
        os_val_free(&result);
        return amxd_status_deferred;
    }

    /* Sync response - free async_ctx */
    os_tr181_async_method_ctx_free(async_ctx);

    if (os_ret != OS_TR181_SUCCESS)
    {
        LOGE("Method callback failed: %s", os_tr181_error_string(os_ret));
        os_val_free(&result);
        /* Convert os_tr181 error code to amxd_status */
        return os_tr181_to_amxd_error(os_ret);
    }

    /* Place result into args (out_args slot) so named output params appear as
     * flat named keys in USP output_args. Clear input args first so they are
     * not echoed in the response. */
    amxc_var_clean(args);
    amxc_var_set_type(args, AMXC_VAR_ID_HTABLE);

    if (result.type != OS_TR181_TYPE_NONE)
    {
        os_ret = os_tr181_val_to_amx(&result, args);
        if (os_ret != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert result");
            os_val_free(&result);
            return amxd_status_invalid_type;
        }
    }

    os_val_free(&result);

    return amxd_status_ok;
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
    amxd_function_t *amx_func = NULL;
    amxd_object_t *parent_obj = NULL;
    amxd_status_t status;
    char *method_name;
    char *path_copy;
    char *parent_path;

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

    /* Parse path to extract parent object and method name */
    path_copy = strdup(path);
    if (!path_copy)
    {
        return OS_TR181_ERROR;
    }

    /* Remove trailing "()" if present */
    size_t len = strlen(path_copy);
    if (len > 2 && path_copy[len - 1] == ')' && path_copy[len - 2] == '(')
    {
        path_copy[len - 2] = '\0';
    }

    /* Find last component (method name) */
    method_name = strrchr(path_copy, '.');
    if (method_name)
    {
        *method_name = '\0'; /* Terminate parent path */
        method_name++;       /* Point to method name */
        parent_path = path_copy;
    }
    else
    {
        /* No parent path - shouldn't happen for valid TR-181 paths */
        LOGE("Invalid method path (no parent): %s", path);
        free(path_copy);
        return OS_TR181_ERROR_INVALID;
    }

    /* Find the parent object using the DM-native lookup (same as register_parameter).
     * Construct "parent_path." then strip all {i} placeholders before querying
     * amxd_object_findf, which searches the actual Ambiorix DM tree directly.
     * This handles template parents (e.g. "FirmwareImage.{i}") at any nesting depth,
     * unlike a linked-list scan of handle->objects which misses table templates. */
    {
        /* Match parent path - add trailing dot for comparison */
        char parent_with_dot[OS_TR181_PATH_MAX];
        snprintf(parent_with_dot, sizeof(parent_with_dot), "%s.", parent_path);
        path_make_searchable(parent_with_dot); /* strips all ".{i}" occurrences */
        parent_obj = amxd_object_findf(amxd_dm_get_root(&handle->dm), "%s", path_strip_device_prefix(parent_with_dot));
        if (parent_obj)
        {
            LOGD("Found parent object: %s", parent_path);
        }
    }

    if (!parent_obj)
    {
        LOGE("Parent object not found for method %s (parent: %s)", path, parent_path);
        free(path_copy);
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /* Create method entry first (needed for callback) */
    method = malloc(sizeof(struct method_entry));
    if (!method)
    {
        free(path_copy);
        return OS_TR181_ERROR;
    }

    method->path = strdup(path);
    if (!method->path)
    {
        free(method);
        free(path_copy);
        return OS_TR181_ERROR;
    }

    method->method_cb = method_cb;
    method->user_data = user_data;
    method->handle = handle; /* Store handle backpointer for callback */

    /* Create Ambiorix function - return type is htable (dict/object) */
    status = amxd_function_new(&amx_func, method_name, AMXC_VAR_ID_HTABLE, method_invoke_cb);
    if (status != amxd_status_ok || !amx_func)
    {
        LOGE("Failed to create Ambiorix function %s: %d", method_name, status);
        free(method->path);
        free(method);
        free(path_copy);
        return OS_TR181_ERROR;
    }

    LOGD("Created function: %s (return type=htable)", method_name);

    /* Set method entry as private data for callback */
    amx_func->priv = method;

    /* Register input/output parameter schema if provided */
    if (params)
    {
        const os_tr181_param_schema_t *p;
        for (p = params; p->name != NULL; p++)
        {
            status = amxd_function_new_arg(amx_func, p->name, os_tr181_type_to_amxc(p->type), NULL);
            if (status != amxd_status_ok)
            {
                LOGW("Failed to add arg '%s' to function %s: %d", p->name, method_name, status);
                continue;
            }
            if (p->flags & OS_TR181_PARAM_IN) amxd_function_arg_set_attr(amx_func, p->name, amxd_aattr_in, true);
            if (p->flags & OS_TR181_PARAM_OUT) amxd_function_arg_set_attr(amx_func, p->name, amxd_aattr_out, true);
            if (p->flags & OS_TR181_PARAM_MANDATORY)
                amxd_function_arg_set_attr(amx_func, p->name, amxd_aattr_mandatory, true);
            if (p->flags & OS_TR181_PARAM_STRICT)
                amxd_function_arg_set_attr(amx_func, p->name, amxd_aattr_strict, true);
            LOGD("  arg: %s flags=0x%x", p->name, p->flags);
        }
    }

    /* Apply method-level attributes */
    if (flags & OS_TR181_METHOD_ASYNC) amxd_function_set_attr(amx_func, amxd_fattr_async, true);
    if (flags & OS_TR181_METHOD_PRIVATE) amxd_function_set_attr(amx_func, amxd_fattr_private, true);
    if (flags & OS_TR181_METHOD_PROTECTED) amxd_function_set_attr(amx_func, amxd_fattr_protected, true);

    /* Add function to parent object */
    status = amxd_object_add_function(parent_obj, amx_func);
    if (status != amxd_status_ok)
    {
        LOGE("Failed to add function to parent object: %d", status);
        amxd_function_delete(&amx_func);
        free(method->path);
        free(method);
        free(path_copy);
        return OS_TR181_ERROR;
    }

    LOGD("Added method %s to parent object", method_name);
    free(path_copy);

    /* Add to method list */
    method->next = handle->methods;
    handle->methods = method;

    LOGD("Method registered successfully: %s", path);
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_register_event(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_param_schema_t *params)
{
    amxd_object_t *parent_obj = NULL;
    amxd_status_t status;
    char *path_copy;
    char *event_name;

    if (!handle || !path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    path_copy = strdup(path);
    if (!path_copy)
    {
        return OS_TR181_ERROR;
    }

    /* Split "Object.Path.EventName!" into parent "Object.Path." and "EventName!" */
    event_name = strrchr(path_copy, '.');
    if (!event_name)
    {
        LOGE("Invalid event path (no parent object): %s", path);
        free(path_copy);
        return OS_TR181_ERROR_INVALID;
    }
    event_name++; /* point to "EventName!" */

    /* Null-terminate path_copy at the dot, separating parent path from event name */
    path_copy[event_name - path_copy - 1] = '\0';
    /* Build parent path with trailing dot */
    size_t parent_len = event_name - path_copy - 1; /* length without trailing dot */
    char parent_with_dot[OS_TR181_PATH_MAX];
    snprintf(parent_with_dot, sizeof(parent_with_dot), "%.*s.", (int)parent_len, path_copy);

    /* Find parent object in DM */
    {
        char searchable[OS_TR181_PATH_MAX];
        snprintf(searchable, sizeof(searchable), "%s", parent_with_dot);
        path_make_searchable(searchable);
        parent_obj = amxd_object_findf(amxd_dm_get_root(&handle->dm), "%s", path_strip_device_prefix(searchable));
    }

    if (!parent_obj)
    {
        LOGE("Parent object not found for event: %s (parent: %s)", path, parent_with_dot);
        free(path_copy);
        return OS_TR181_ERROR_NOT_FOUND;
    }

    if (params)
    {
        /*
         * Build an AMXC_VAR_ID_HTABLE mapping arg_name → amxc_type_id.
         * amxd_object_add_event_ext stores the pointer directly (no copy) and
         * takes ownership — use amxc_var_new for heap allocation and do NOT
         * free on success. On failure, amxd_object_add_event_ext frees it.
         */
        amxc_var_t *event_template = NULL;
        amxc_var_new(&event_template);
        amxc_var_set_type(event_template, AMXC_VAR_ID_HTABLE);

        for (const os_tr181_param_schema_t *p = params; p->name != NULL; p++)
        {
            uint32_t amxc_type = os_tr181_type_to_amxc((os_tr181_param_type_t)p->type);
            amxc_var_add_key(uint32_t, event_template, p->name, amxc_type);
        }

        status = amxd_object_add_event_ext(parent_obj, event_name, event_template);
        /* event_template ownership transferred on success; freed by amxd on failure */
    }
    else
    {
        status = amxd_object_add_event(parent_obj, event_name);
    }

    free(path_copy);

    if (status != amxd_status_ok)
    {
        LOGE("amxd_object_add_event failed for %s: %d", path, status);
        return OS_TR181_ERROR;
    }

    LOGD("Event registered: %s", path);
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_emit_event(os_tr181_handle_t *handle, const char *path, const os_tr181_val_t *data)
{
    amxd_object_t *parent_obj = NULL;
    char *path_copy;
    char *event_name;

    if (!handle || !path)
    {
        return OS_TR181_ERROR_INVALID;
    }

    path_copy = strdup(path);
    if (!path_copy)
    {
        return OS_TR181_ERROR;
    }

    /* Split into parent path and event name (same as register_event) */
    event_name = strrchr(path_copy, '.');
    if (!event_name)
    {
        LOGE("Invalid event path (no parent object): %s", path);
        free(path_copy);
        return OS_TR181_ERROR_INVALID;
    }
    event_name++;

    size_t parent_len = event_name - path_copy - 1;
    char parent_with_dot[OS_TR181_PATH_MAX];
    snprintf(parent_with_dot, sizeof(parent_with_dot), "%.*s.", (int)parent_len, path_copy);

    {
        char searchable[OS_TR181_PATH_MAX];
        snprintf(searchable, sizeof(searchable), "%s", parent_with_dot);
        path_make_searchable(searchable);
        parent_obj = amxd_object_findf(amxd_dm_get_root(&handle->dm), "%s", path_strip_device_prefix(searchable));
    }

    if (!parent_obj)
    {
        LOGE("Parent object not found for emit: %s", path);
        free(path_copy);
        return OS_TR181_ERROR_NOT_FOUND;
    }

    /*
     * Pass the event args as a flat HTABLE.  amxd_dm_event() merges HTABLE
     * keys directly into the top-level event packet alongside the Ambiorix
     * metadata fields (notification, path, object, eobject).  For non-HTABLE
     * (scalar) data Ambiorix automatically wraps the value under a "data" key.
     *
     * subs_event_handler handles both the flat and the "data"-wrapped formats.
     */
    amxc_var_t data_var;
    amxc_var_init(&data_var);

    if (data)
    {
        if (os_tr181_val_to_amx(data, &data_var) != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert event data for %s", path);
            amxc_var_clean(&data_var);
            free(path_copy);
            return OS_TR181_ERROR;
        }
    }
    else
    {
        amxc_var_set_type(&data_var, AMXC_VAR_ID_HTABLE);
    }

    amxd_object_emit_signal(parent_obj, event_name, &data_var);

    amxc_var_clean(&data_var);
    free(path_copy);

    LOGD("Event emitted: %s", path);
    return OS_TR181_SUCCESS;
}

/* ---------------------------------------------------------------------------
 * Persistence helpers (ODL file-based, via libamxo)
 * --------------------------------------------------------------------------- */

/* Build the ODL save file path for a specific top-level DM object.
 * Format: <storage_dir>/<obj_name>.odl
 * Returns false on error */
static bool persist_object_save_path(os_tr181_handle_t *handle, const char *obj_name, char *buf, size_t bufsz)
{
    if (strchr(obj_name, '/') != NULL)
    {
        LOGE("Skipping persistence for object with invalid name: '%s'", obj_name);
        return false;
    }
    int n = snprintf(buf, bufsz, "%s/%s.odl", handle->persistence.storage_dir, obj_name);
    if (n < 0 || (size_t)n >= bufsz)
    {
        LOGE("Persistence path too long for object '%s'", obj_name);
        return false;
    }
    return true;
}

/* Propagate amxd_oattr_persistent up the ancestor chain so
 * amxo_parser_save_object descends into the right nodes. */
static void persist_mark_ancestors(amxd_object_t *obj)
{
    amxd_object_t *cur = obj;
    while (cur != NULL)
    {
        if (amxd_object_is_attr_set(cur, amxd_oattr_persistent)) break; /* already done */
        amxd_object_set_attr(cur, amxd_oattr_persistent, true);
        cur = amxd_object_get_parent(cur);
    }
}

/* Perform the actual ODL save — one file per top-level DM object,
 * each written atomically via tmp file + rename. */
static void persist_do_save(os_tr181_handle_t *handle)
{
    amxd_object_t *root = amxd_dm_get_root(&handle->dm);

    amxc_llist_for_each(it, &root->objects)
    {
        amxd_object_t *top = amxc_llist_it_get_data(it, amxd_object_t, it);
        const char *obj_name = amxd_object_get_name(top, AMXD_OBJECT_NAMED);

        char save_path[OS_TR181_PATH_MAX];
        char tmp_path[OS_TR181_PATH_MAX + 8];
        if (!persist_object_save_path(handle, obj_name, save_path, sizeof(save_path))) continue;
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", save_path);

        int ret = amxo_parser_save_object(&handle->persistence.parser, tmp_path, top, false);
        if (ret != 0)
        {
            LOGE("Failed to save %s to %s: %d", obj_name, tmp_path, ret);
            unlink(tmp_path);
            continue;
        }

        if (rename(tmp_path, save_path) != 0)
        {
            LOGE("Failed to rename %s -> %s: %s", tmp_path, save_path, strerror(errno));
            unlink(tmp_path);
        }
        else
        {
            LOGD("Persisted %s to %s", obj_name, save_path);
        }
    }
}

/* (Re)arm the persistence debounce due time.
 * Called from param_write_cb (external set) and os_tr181_notify_changed (internal change).
 * Sets save_due_ms to now + delay; process_requests fires the save when it expires. */
static void persist_schedule_save(os_tr181_handle_t *handle)
{
    if (!handle->persistence.active) return;
    if (handle->persistence.restoring) return; /* Suppress saves during restore */

    uint32_t delay = handle->persistence.save_initialized ? handle->persistence.save_delay_ms
                                                          : handle->persistence.init_delay_ms;
    handle->persistence.save_due_ms = get_current_time_ms() + delay;
    LOGT("persist_schedule_save: save due in %u ms", delay);
}

/* Check if the persistence due time has passed and flush if so.
 * Called unconditionally from os_tr181_process_requests. */
static void persist_check_due_time(os_tr181_handle_t *handle)
{
    if (!handle->persistence.active) return;
    if (handle->persistence.save_due_ms == 0) return;

    uint64_t now_ms = get_current_time_ms();
    if (now_ms >= handle->persistence.save_due_ms)
    {
        LOGT("persist_check_due_time: due time reached, saving");
        handle->persistence.save_due_ms = 0;
        persist_do_save(handle);
        handle->persistence.save_initialized = true;
    }
}

/* Initialise persistence config from environment variables, falling back to
 * hard-coded defaults.  Called once from os_tr181_init_ex().
 *
 * Environment variables (Ambiorix backend only):
 *   OS_TR181_AMX_PERSIST_STORAGE_DIR  - directory for ODL save files
 *   OS_TR181_AMX_PERSIST_SAVE_DELAY_MS - debounce window after last change (ms)
 *   OS_TR181_AMX_PERSIST_INIT_DELAY_MS - delay before very first save (ms)
 */
static void persist_config_init(os_tr181_handle_t *handle)
{
    const char *env;

    /* Storage directory */
    const char *storage_dir = OS_TR181_PERSIST_STORAGE_DIR_DEFAULT;
    env = getenv("OS_TR181_AMX_PERSIST_STORAGE_DIR");
    if (env != NULL && env[0] != '\0')
    {
        storage_dir = env;
        LOGD("Persistence storage dir from env: %s", storage_dir);
    }
    STRSCPY(handle->persistence.storage_dir, storage_dir);

    /* Save debounce delay */
    handle->persistence.save_delay_ms = OS_TR181_PERSIST_SAVE_DELAY_MS;
    env = getenv("OS_TR181_AMX_PERSIST_SAVE_DELAY_MS");
    if (env != NULL && env[0] != '\0')
    {
        long v = strtol(env, NULL, 10);
        if (v > 0)
        {
            handle->persistence.save_delay_ms = (uint32_t)v;
            LOGD("Persistence save delay from env: %u ms", handle->persistence.save_delay_ms);
        }
        else
        {
            LOGW("OS_TR181_AMX_PERSIST_SAVE_DELAY_MS invalid ('%s'), using default %u ms",
                 env,
                 handle->persistence.save_delay_ms);
        }
    }

    /* Init delay */
    handle->persistence.init_delay_ms = OS_TR181_PERSIST_INIT_DELAY_MS;
    env = getenv("OS_TR181_AMX_PERSIST_INIT_DELAY_MS");
    if (env != NULL && env[0] != '\0')
    {
        long v = strtol(env, NULL, 10);
        if (v > 0)
        {
            handle->persistence.init_delay_ms = (uint32_t)v;
            LOGD("Persistence init delay from env: %u ms", handle->persistence.init_delay_ms);
        }
        else
        {
            LOGW("OS_TR181_AMX_PERSIST_INIT_DELAY_MS invalid ('%s'), using default %u ms",
                 env,
                 handle->persistence.init_delay_ms);
        }
    }
}

/* Initialise ODL persistence after publish: mark ancestor objects, create storage
 * directory, init parser, and restore previously saved values. */
static void persist_setup(os_tr181_handle_t *handle)
{
    /* Set up persistence if any parameters are marked persistent */
    if (!handle->persistence.active) return;

    /* Propagate amxd_oattr_persistent up the ancestor chain for each
     * persistent parameter so amxo_parser_save_object descends correctly. */
    for (struct param_entry *pe = handle->parameters; pe != NULL; pe = pe->next)
    {
        if (!pe->persistent) continue;
        /* Find the parent object of this parameter in the dm */
        char obj_path[OS_TR181_PATH_MAX];
        STRSCPY(obj_path, pe->path);
        path_make_searchable(obj_path);
        path_drop_last_part(obj_path, PATH_KEEP_DOT);
        /* Strip Device. prefix — dm uses local paths */
        const char *local_path = path_strip_device_prefix(obj_path);
        amxd_object_t *obj = amxd_object_findf(amxd_dm_get_root(&handle->dm), "%s", local_path);
        if (obj != NULL) persist_mark_ancestors(obj);
    }

    /* Init the amxo parser (reused across all save/load calls) */
    amxo_parser_init(&handle->persistence.parser);

    /* Ensure the storage directory exists before any load or save */
    if (mkdir(handle->persistence.storage_dir, 0755) != 0 && errno != EEXIST)
        LOGW("Could not create persistence directory %s: %s", handle->persistence.storage_dir, strerror(errno));

    /* Load previously saved values — one file per top-level DM object */
    amxd_object_t *root = amxd_dm_get_root(&handle->dm);
    handle->persistence.restoring = true;
    amxc_llist_for_each(it, &root->objects)
    {
        amxd_object_t *top = amxc_llist_it_get_data(it, amxd_object_t, it);
        const char *obj_name = amxd_object_get_name(top, AMXD_OBJECT_NAMED);

        char save_path[OS_TR181_PATH_MAX];
        if (!persist_object_save_path(handle, obj_name, save_path, sizeof(save_path))) continue;

        if (access(save_path, R_OK) != 0) continue;

        int ret = amxo_parser_parse_file(&handle->persistence.parser, save_path, root);
        if (ret == 0)
            LOGD("Restored persistent values from %s", save_path);
        else
            LOGW("Failed to restore persistent values from %s: %d", save_path, ret);
    }
    handle->persistence.restoring = false;

    LOGD("ODL persistence active, storage directory: %s", handle->persistence.storage_dir);
}

os_tr181_error_t os_tr181_publish_objects(os_tr181_handle_t *handle)
{
    int ret;

    if (!handle || !handle->bus_ctx)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Check if already published - enforce publish-once pattern */
    if (handle->published)
    {
        LOGE("Objects already published - multiple publish calls not supported");
        return OS_TR181_ERROR;
    }

    if (!handle->objects || !handle->objects->path)
    {
        LOGE("No objects registered");
        return OS_TR181_ERROR_INVALID;
    }

    /* Log all top-level data model roots */
    {
        amxd_object_t *root_obj = amxd_dm_get_root(&handle->dm);
        amxc_llist_for_each(it, &root_obj->objects)
        {
            amxd_object_t *top = amxc_container_of(it, amxd_object_t, it);
            LOGI("Data model root: %s", top->name);
        }
    }

    /* Try to connect to USP on first publish if not already attempted */
    if (!handle->usp_reconnect_active && os_tr181_usp_is_enabled())
    {
        handle->usp_reconnect_active = true;

        /* One-time: load backend and configure for all roots */
        ret = usp_load_and_config(handle);
        if (ret == 0)
        {
            /* Attempt connection and registration */
            ret = usp_connect_and_register(handle);
        }

        if (ret != 0)
        {
            LOGW("USP connection failed: %d (%s), continuing with primary bus only", ret, amxb_get_error(ret));
            usp_init_reconnection_state(handle);
        }
        else
        {
            LOGD("Connected to USP broker, dual-backend mode enabled");
        }
    }

    /* Register the entire data model with the primary bus */
    ret = amxb_register(handle->bus_ctx, &handle->dm);
    if (ret != 0)
    {
        LOGE("Failed to publish data model to primary bus: %d", ret);
        return OS_TR181_ERROR;
    }

    LOGD("Data model published to primary bus successfully");

    /* Register Device.* proxy aliases via ProxyManager for all roots */
    proxy_manager_register_all(handle);

    /* Mark as published to prevent multiple calls */
    handle->published = true;

    /* Set up persistence */
    persist_setup(handle);

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

    /* Strip "Device." prefix for dm lookup (dm uses local paths) */
    const char *local_obj_path = path_strip_device_prefix(obj_path);

    /* Find the object in the data model */
    obj = amxd_dm_findf(&handle->dm, "%s", local_obj_path);
    if (!obj)
    {
        LOGE("Object not found: %s (dm path: %s)", obj_path, local_obj_path);
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

    /* Schedule a debounced save if this parameter is marked persistent */
    if (amxd_param_is_attr_set(param, amxd_pattr_persistent)) persist_schedule_save(handle);

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

/* ========================================================================
 * USP Reconnection Helper Functions
 * ========================================================================
 *
 * USP Connection Lifecycle and State Management
 *
 * The library supports automatic connection and reconnection to the USP broker
 * (obuspa), handling two scenarios:
 *
 * SCENARIO 1: Provider starts before obuspa (initial connection fails)
 * ---------------------------------------------------------------------
 * 1. os_tr181_publish_objects() called → sets usp_reconnect_active=true
 * 2. usp_load_and_config() succeeds (one-time backend setup)
 * 3. usp_connect_and_register() fails → usp_connected=false
 * 4. usp_init_reconnection_state() initializes retry timestamps
 * 5. os_tr181_process_requests() periodically calls usp_try_reconnect()
 * 6. When obuspa starts → usp_connect_and_register() succeeds → usp_connected=true
 *
 * SCENARIO 2: Obuspa restarts while provider is running (disconnect)
 * -------------------------------------------------------------------
 * 1. Normal operation: usp_reconnect_active=true, usp_connected=true
 * 2. amxb_read() fails → usp_disconnect() called
 * 3. usp_disconnect() sets usp_connected=false, initializes retry state
 * 4. os_tr181_process_requests() periodically calls usp_try_reconnect()
 * 5. When obuspa ready → usp_connect_and_register() succeeds → usp_connected=true
 *
 * State Flags:
 * -----------
 * usp_connected         - True if currently connected and data model registered
 * usp_reconnect_active  - True if reconnection logic should run
 *
 * State Combinations:
 *
 *   usp_reconnect_active | usp_connected | Meaning
 *   ---------------------|---------------|----------------------------------
 *   false                | false         | Never initialized, or gave up
 *   true                 | false         | Retrying connection (backoff)
 *   true                 | true          | Connected and working normally
 *
 * Reconnection Behavior:
 * ---------------------
 * - Initial delay: 2 seconds after disconnect/failure
 * - Exponential backoff: doubles each failure (2s → 4s → 8s → 10s → 10s...)
 * - Maximum delay: 10 seconds (capped)
 * - Total retry period: 10 minutes
 * - After timeout: sets usp_reconnect_active=false (gives up)
 * - On successful reconnection: data model is re-registered with amxb_register()
 *
 * The primary bus (ubus) continues to work independently of USP connection state.
 * ======================================================================== */

/* Helper to get current time in milliseconds */
static uint64_t get_current_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/* Initialize reconnection state (shared by disconnect and initial failure) */
static void usp_init_reconnection_state(os_tr181_handle_t *handle)
{
    handle->usp_disconnect_time_ms = get_current_time_ms();
    handle->usp_retry_delay_ms = USP_RETRY_INITIAL_MS;
    handle->usp_next_retry_time_ms = handle->usp_disconnect_time_ms + handle->usp_retry_delay_ms;
    if (handle->fd_change_cb)
    {
        handle->fd_change_cb(handle, handle->fd_change_user_data);
    }
}

/* Helper to apply exponential backoff and schedule next retry */
static void usp_schedule_retry(os_tr181_handle_t *handle, uint64_t now_ms)
{
    handle->usp_retry_delay_ms *= 2;
    if (handle->usp_retry_delay_ms > USP_RETRY_MAX_MS)
    {
        handle->usp_retry_delay_ms = USP_RETRY_MAX_MS;
    }
    handle->usp_next_retry_time_ms = now_ms + handle->usp_retry_delay_ms;
    if (handle->fd_change_cb)
    {
        handle->fd_change_cb(handle, handle->fd_change_user_data);
    }
}

/* Load USP backend and configure (called once on first publish) */
static int usp_load_and_config(os_tr181_handle_t *handle)
{
    int ret;

    if (!handle)
    {
        return -1;
    }

    /* If already configured skip */
    if (handle->usp_config_done)
    {
        return 0;
    }

    /* Load USP backend module */
    ret = os_tr181_be_load(g_os_tr181_usp_backends);
    if (ret != 0)
    {
        LOGW("Failed to load USP backend module: %d", ret);
        return ret;
    }

    /* Build USP translate config for every top-level DM object */
    if (build_usp_config(handle) != 0)
    {
        LOGW("Failed to build USP configuration");
        return -1;
    }

    /* Log the resulting config */
    {
        os_tr181_val_t cfg_val;
        os_val_init(&cfg_val);
        char *cfg_json = NULL;
        if (os_tr181_val_from_amx(&cfg_val, &handle->config) == OS_TR181_SUCCESS
            && os_val_to_json_string(&cfg_val, &cfg_json) == OS_TR181_SUCCESS)
        {
            LOGD("USP config: %s", cfg_json);
        }
        free(cfg_json);
        os_val_free(&cfg_val);
    }

    /* Set configuration (backends store pointer, so config must persist) */
    ret = amxb_set_config(&handle->config);
    if (ret != 0)
    {
        LOGW("Failed to set USP configuration: %d", ret);
        return ret;
    }

    /* Mark as done */
    handle->usp_config_done = true;

    return 0;
}

/* Connect to USP broker and register data model */
static int usp_connect_and_register(os_tr181_handle_t *handle)
{
    int ret;

    /* Connect to USP broker controller path */
    ret = amxb_connect(&handle->usp_ctx, USP_BROKER_CONTROLLER_URI);
    if (ret != 0)
    {
        LOGW("Failed to connect to USP broker: %d (%s)", ret, amxb_get_error(ret));
        return ret;
    }

    /* Set access level to PUBLIC */
    amxb_set_access(handle->usp_ctx, AMXB_PUBLIC);

    /* Register data model with USP bus */
    ret = amxb_register(handle->usp_ctx, &handle->dm);
    if (ret != 0)
    {
        LOGE("Failed to register data model with USP bus: %d", ret);
        /* Registration failed - clean up connection */
        amxb_disconnect(handle->usp_ctx);
        amxb_free(&handle->usp_ctx);
        handle->usp_ctx = NULL;
        return ret;
    }

    /* Success - mark as connected */
    handle->usp_connected = true;

    /* Notify FD change callback (USP FD added) */
    if (handle->fd_change_cb)
    {
        handle->fd_change_cb(handle, handle->fd_change_user_data);
    }

    return 0;
}

/* Disconnect from USP broker and initialize reconnection state */
static void usp_disconnect(os_tr181_handle_t *handle)
{
    LOGW("USP connection lost, will attempt reconnection");

    /* Close broken connection */
    if (handle->usp_ctx)
    {
        amxb_disconnect(handle->usp_ctx);
        amxb_free(&handle->usp_ctx);
        handle->usp_ctx = NULL;
    }

    /* Disable USP and initialize reconnection state */
    handle->usp_connected = false;
    usp_init_reconnection_state(handle);

    /* Notify FD change callback (USP FD removed) */
    if (handle->fd_change_cb)
    {
        handle->fd_change_cb(handle, handle->fd_change_user_data);
    }
}

/* Try to reconnect to USP broker with exponential backoff */
static void usp_try_reconnect(os_tr181_handle_t *handle)
{
    uint64_t now_ms = get_current_time_ms();
    uint64_t time_since_disconnect = now_ms - handle->usp_disconnect_time_ms;

    /* Check if total retry time exceeded (if enabled) */
    if (USP_RETRY_TOTAL_TIME > 0)
    {
        if (time_since_disconnect > USP_RETRY_TOTAL_TIME)
        {
            LOGW("USP reconnection failed after %" PRIu64 " ms total, giving up", time_since_disconnect);
            /* Stop reconnection attempts */
            handle->usp_reconnect_active = false;
            return;
        }
    }

    /* Check if it's time to retry */
    if (now_ms < handle->usp_next_retry_time_ms)
    {
        return; /* Not yet time to retry */
    }

    /* Attempt reconnection */
    LOGD("Attempting USP reconnection (delay was %u ms, elapsed %" PRIu64 " ms)",
         handle->usp_retry_delay_ms,
         time_since_disconnect);

    int ret = usp_connect_and_register(handle);
    if (ret != 0)
    {
        LOGW("USP reconnection failed: %d (%s)", ret, amxb_get_error(ret));
        usp_schedule_retry(handle, now_ms);
    }
    else
    {
        LOGI("USP reconnection successful after %" PRIu64 " ms, data model re-registered", time_since_disconnect);
        handle->usp_retry_delay_ms = USP_RETRY_INITIAL_MS; /* Reset delay */
    }
}

os_tr181_error_t os_tr181_process_requests(os_tr181_handle_t *handle, int timeout_ms)
{
    int ret;
    os_tr181_error_t result = OS_TR181_SUCCESS;

    if (!handle || !handle->bus_ctx)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Try to reconnect to USP if disconnected */
    if (!handle->usp_connected && handle->usp_reconnect_active)
    {
        usp_try_reconnect(handle);
    }

    /* Process Ambiorix bus requests and signals */
    struct timeval tv;
    fd_set readfds;
    int bus_fd = amxb_get_fd(handle->bus_ctx);
    int usp_fd = -1;
    int sig_fd = amxp_signal_fd();
    int max_fd = -1;

    if (bus_fd < 0 || sig_fd < 0)
    {
        return OS_TR181_ERROR;
    }

    /* Get USP file descriptor if connected */
    if (handle->usp_connected && handle->usp_ctx)
    {
        usp_fd = amxb_get_fd(handle->usp_ctx);
    }

    /* Find max fd for select */
    if (bus_fd > max_fd) max_fd = bus_fd;
    if (usp_fd > max_fd) max_fd = usp_fd;
    if (sig_fd > max_fd) max_fd = sig_fd;

    FD_ZERO(&readfds);
    FD_SET(bus_fd, &readfds);
    FD_SET(sig_fd, &readfds);
    if (usp_fd >= 0)
    {
        FD_SET(usp_fd, &readfds);
    }

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(max_fd + 1, &readfds, NULL, NULL, &tv);

    if (ret == 0)
    {
        result = OS_TR181_ERROR_TIMEOUT;
        goto done;
    }
    else if (ret < 0)
    {
        result = OS_TR181_ERROR;
        goto done;
    }

    /* Process primary bus data */
    if (FD_ISSET(bus_fd, &readfds))
    {
        ret = amxb_read(handle->bus_ctx);
        if (ret != 0)
        {
            LOGD("amxb_read (primary bus) failed: %d", ret);
            result = OS_TR181_ERROR;
            goto done;
        }
        LOGT("amxb_read (primary bus) processed data");
    }

    /* Process USP bus data */
    if (usp_fd >= 0 && FD_ISSET(usp_fd, &readfds))
    {
        ret = amxb_read(handle->usp_ctx);
        if (ret != 0)
        {
            LOGD("amxb_read (USP bus) failed: %d", ret);
            /* USP connection lost - trigger reconnection */
            usp_disconnect(handle);
        }
        else
        {
            LOGT("amxb_read (USP bus) processed data");
        }
    }

    /* Process signal data */
    if (FD_ISSET(sig_fd, &readfds))
    {
        ret = amxp_signal_read();
        if (ret != 0)
        {
            LOGD("amxp_signal_read failed: %d", ret);
            result = OS_TR181_ERROR;
            goto done;
        }
        LOGT("amxp_signal_read processed");
    }

done:
    /* Fire persistence save if the debounce due time has passed.
     * Called on all post-select paths: persistence is independent of bus state. */
    persist_check_due_time(handle);

    return result;
}

os_tr181_error_t os_tr181_add_instance_ex(
        os_tr181_handle_t *handle,
        const char *object_path,
        uint32_t index,
        const char *alias_value,
        const os_tr181_val_t *values,
        int *instance_number)
{
    amxc_var_t amx_values;
    amxc_var_t ret;
    int rv;
    int result = OS_TR181_ERROR;
    int timeout_sec = OS_TR181_DEFAULT_TIMEOUT_MS / 1000;

    if (!handle || !handle->bus_ctx || !object_path || !instance_number)
    {
        return OS_TR181_ERROR_INVALID;
    }

    amxc_var_init(&amx_values);
    amxc_var_init(&ret);

    /* Convert values dict to Ambiorix htable */
    if (values && values->type == OS_TR181_TYPE_DICT)
    {
        if (os_tr181_val_to_amx(values, &amx_values) != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert values dict to amxc variant");
            amxc_var_clean(&amx_values);
            amxc_var_clean(&ret);
            return OS_TR181_ERROR_INVALID;
        }
    }
    else
    {
        /* Empty htable if no values provided */
        amxc_var_set_type(&amx_values, AMXC_VAR_ID_HTABLE);
    }

    LOGD("Calling amxb_add for path: %s, index: %u, alias: %s", object_path, index, alias_value ? alias_value : "NULL");

    /* Call amxb_add with:
     * - object_path: table path
     * - index: requested index (0 = auto-assign)
     * - alias_value: sets Alias parameter (NULL = auto "cpe-Parent-N")
     * - amx_values: htable of initial parameter values
     */
    object_path = device_path_fixup(handle, object_path);
    rv = amxb_add(handle->bus_ctx, object_path, index, alias_value, &amx_values, &ret, timeout_sec);
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

    amxc_var_clean(&amx_values);
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
    instance_path = device_path_fixup(handle, instance_path);
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
    object_path = device_path_fixup(handle, object_path);
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
static int check_instance_exists_amx(os_tr181_handle_t *handle, const char *instance_path)
{
    amxc_var_t result;
    int exists = 0;

    if (!handle || !handle->bus_ctx || !instance_path)
    {
        return 0;
    }

    amxc_var_init(&result);

    /* Try to get the instance */
    instance_path = device_path_fixup(handle, instance_path);
    if (amxb_get(handle->bus_ctx, instance_path, 0, &result, OS_TR181_DEFAULT_TIMEOUT_MS / 1000) == 0)
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
    char instance_path[OS_TR181_PATH_MAX];
    struct timeval start, now;
    int elapsed_ms = 0;

    snprintf(instance_path, sizeof(instance_path), "%s%d.", object_path, instance_number);

    gettimeofday(&start, NULL);

    /* Poll to check if instance exists */
    while (elapsed_ms < timeout_ms)
    {
        if (check_instance_exists_amx(handle, instance_path))
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
    char instance_path[OS_TR181_PATH_MAX];

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

    if (check_instance_exists_amx(handle, instance_path))
    {
        LOGD("Instance %s exists immediately after add", instance_path);
        return OS_TR181_SUCCESS;
    }

    /* Fall back to event-based wait if not immediately available */
    LOGD("Instance not immediately available, waiting for event");
    result = wait_for_instance_event_amx(handle, object_path, *instance_number, timeout_ms);
    return result;
}

/* Helper: Parse method path into object path and method name
 * Examples:
 *   "Device.WiFi.Reset()" -> object="Device.WiFi." method="Reset"
 *   "Device.WiFi.Reset"   -> object="Device.WiFi." method="Reset"
 */
static os_tr181_error_t parse_method_path(const char *path, char **object_path_out, char **method_name_out)
{
    char *working_copy = NULL;
    char *object_path = NULL;
    const char *method_ptr = NULL;
    size_t path_len;
    os_tr181_error_t err = OS_TR181_SUCCESS;

    if (!path || !object_path_out || !method_name_out)
    {
        return OS_TR181_ERROR_INVALID;
    }

    *object_path_out = NULL;
    *method_name_out = NULL;

    /* Make a working copy of the path */
    working_copy = strdup(path);
    if (!working_copy)
    {
        return OS_TR181_ERROR;
    }

    /* Strip trailing "()" if present */
    path_len = strlen(working_copy);
    if (path_len >= 2 && strcmp(&working_copy[path_len - 2], "()") == 0)
    {
        working_copy[path_len - 2] = '\0';
    }

    /* Find the last '.' to split object path and method name */
    method_ptr = strrchr(working_copy, '.');
    if (!method_ptr)
    {
        LOGE("Invalid method path (no '.' found): %s", path);
        err = OS_TR181_ERROR_INVALID;
        goto cleanup;
    }

    /* Allocate object path (include the trailing '.') */
    size_t obj_len = method_ptr - working_copy + 2; /* +1 for '.', +1 for '\0' */
    object_path = malloc(obj_len);
    if (!object_path)
    {
        err = OS_TR181_ERROR;
        goto cleanup;
    }

    strncpy(object_path, working_copy, obj_len - 1);
    object_path[obj_len - 1] = '\0';

    /* Move method_ptr past the '.' and duplicate */
    method_ptr++;
    *method_name_out = strdup(method_ptr);
    if (!*method_name_out)
    {
        err = OS_TR181_ERROR;
        goto cleanup;
    }

    *object_path_out = object_path;
    object_path = NULL; /* Transfer ownership */

cleanup:
    free(working_copy);
    free(object_path);
    return err;
}

/* Returns true if var contains no meaningful result:
 * NULL type, empty string, or empty htable. */
static bool amxc_var_is_result_empty(const amxc_var_t *var)
{
    if (var == NULL) return true;
    int type = amxc_var_type_of(var);
    if (type == AMXC_VAR_ID_NULL) return true;
    if (type == AMXC_VAR_ID_CSTRING)
    {
        const char *s = amxc_var_constcast(cstring_t, var);
        return (s == NULL) || *s == '\0';
    }
    if (type == AMXC_VAR_ID_HTABLE)
    {
        const amxc_htable_t *ht = amxc_var_constcast(amxc_htable_t, var);
        return (ht == NULL) || amxc_htable_is_empty(ht);
    }
    return false;
}

os_tr181_error_t os_tr181_invoke(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        uint32_t timeout_sec)
{
    amxb_bus_ctx_t *bus_ctx = NULL;
    amxc_var_t amx_args;
    amxc_var_t amx_result;
    char *method_name = NULL;
    char *object_path = NULL;
    const char *method_ptr = NULL;
    size_t path_len;
    int ret;
    os_tr181_error_t err = OS_TR181_SUCCESS;

    amxc_var_init(&amx_args);
    amxc_var_init(&amx_result);

    if (!handle || !path)
    {
        err = OS_TR181_ERROR_INVALID;
        goto cleanup;
    }

    bus_ctx = handle->bus_ctx;
    if (!bus_ctx)
    {
        LOGE("Bus context not initialized");
        err = OS_TR181_ERROR_INIT;
        goto cleanup;
    }

    path = device_path_fixup(handle, path);

    /* Parse path to extract object path and method name
     * Examples:
     *   "Device.WiFi.Reset()" -> object="Device.WiFi." method="Reset"
     *   "Device.WiFi.Reset"   -> object="Device.WiFi." method="Reset"
     */

    /* Make a working copy of the path */
    method_name = strdup(path);
    if (!method_name)
    {
        err = OS_TR181_ERROR_INVALID;
        goto cleanup;
    }

    /* Strip trailing "()" if present */
    path_len = strlen(method_name);
    if (path_len >= 2 && strcmp(&method_name[path_len - 2], "()") == 0)
    {
        method_name[path_len - 2] = '\0';
        path_len -= 2;
    }

    /* Find the last '.' to split object path and method name */
    method_ptr = strrchr(method_name, '.');
    if (!method_ptr)
    {
        LOGE("Invalid method path (no '.' found): %s", path);
        err = OS_TR181_ERROR_INVALID;
        goto cleanup;
    }

    /* Allocate object path (include the trailing '.') */
    size_t obj_len = method_ptr - method_name + 2; /* +1 for '.', +1 for '\0' */
    object_path = malloc(obj_len);
    if (!object_path)
    {
        err = OS_TR181_ERROR_INVALID;
        goto cleanup;
    }

    strncpy(object_path, method_name, obj_len - 1);
    object_path[obj_len - 1] = '\0';

    /* Move method_ptr past the '.' */
    method_ptr++;

    LOGD("Invoking method: object='%s' method='%s'", object_path, method_ptr);

    /* Convert arguments to amxc_var_t (must be HTABLE type) */
    if (args != NULL)
    {
        err = os_tr181_val_to_amx(args, &amx_args);
        if (err != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert arguments to amxc_var_t");
            goto cleanup;
        }

        /* Ensure it's a hash table */
        if (amxc_var_type_of(&amx_args) != AMXC_VAR_ID_HTABLE)
        {
            LOGE("Arguments must be a dictionary/object type");
            err = OS_TR181_ERROR_INVALID;
            goto cleanup;
        }
    }
    else
    {
        /* No arguments - use empty hash table */
        amxc_var_set_type(&amx_args, AMXC_VAR_ID_HTABLE);
    }

    /* If timeout_sec is <= 0, use default */
    if (timeout_sec <= 0) timeout_sec = (OS_TR181_DEFAULT_TIMEOUT_MS / 1000);

    /* Call the method */
    ret = amxb_call(bus_ctx, object_path, method_ptr, &amx_args, &amx_result, timeout_sec);

    if (ret != 0)
    {
        /* IMPORTANT: amxb_call() returns amxd_status_t values, NOT AMXB_ERROR_* codes! */
        const char *amxd_err_str = amxd_status_string((amxd_status_t)ret);
        char *err_details = NULL;

        /* Extract error details from result structure (handles NULL gracefully) */
        os_tr181_val_t err_val = OS_VAL_INIT();

        if (os_tr181_val_from_amx(&err_val, &amx_result) == OS_TR181_SUCCESS)
        {
            os_val_to_json_string(&err_val, &err_details);
            os_val_free(&err_val);
        }

        LOGE("amxb_call failed: %s (code %d) '%s'", amxd_err_str, ret, err_details ?: "");
        free(err_details);

        err = amxd_error_to_os_tr181((amxd_status_t)ret);
        goto cleanup;
    }

    LOGD("Method invoked successfully");

    /* Convert result if requested */
    if (result != NULL)
    {
        /* amxb_call returns RPC response as list: [ret, out_args, error-code]
         * - ret (index 0): return value (any type); maps to _retval in USP
         * - out_args (index 1): named output params; maps to flat keys in USP output_args
         *
         * PRPL services are inconsistent in practice: well-behaved services place named
         * output params in out_args (index 1), while some place them in ret (index 0).
         * The merge strategy below handles both patterns transparently.
         * The "_retval" naming mirrors what amxb_usp itself uses when forwarding ret
         * to USP output_args.
         *
         * Merge strategy:
         * - Only index 1 non-empty: use index 1 (named output params, flat)
         * - Only index 0 non-empty: use index 0 (covers providers using the ret slot)
         * - Both non-empty: add ret as "_retval" into out_args, use index 1 */
        const amxc_var_t *actual_result = &amx_result;

        if (amxc_var_type_of(&amx_result) == AMXC_VAR_ID_LIST)
        {
            amxc_var_t *ret_var = amxc_var_get_index(&amx_result, 0, AMXC_VAR_FLAG_DEFAULT);
            amxc_var_t *out_args = amxc_var_get_index(&amx_result, 1, AMXC_VAR_FLAG_DEFAULT);

            bool ret_empty = amxc_var_is_result_empty(ret_var);
            bool out_empty = amxc_var_is_result_empty(out_args);

            if (!ret_empty && !out_empty)
            {
                /* Both populated: add ret as "_retval" into out_args in place */
                amxc_var_t *retval_entry = amxc_var_add_new_key(out_args, "_retval");
                if (retval_entry) amxc_var_copy(retval_entry, ret_var);
                actual_result = out_args;
            }
            else if (!out_empty)
            {
                actual_result = out_args;
            }
            else if (ret_var)
            {
                actual_result = ret_var;
            }
            LOGD("Extracted result from RPC response list");
        }

        err = os_tr181_val_from_amx(result, actual_result);
        if (err != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert result from amxc_var_t");
            goto cleanup;
        }
    }

cleanup:
    amxc_var_clean(&amx_args);
    amxc_var_clean(&amx_result);
    free(method_name);
    free(object_path);

    return err;
}

/* ========================================================================
 * Async Method Invocation (Client-Side)
 * ======================================================================== */

/* Ambiorix result callback - called for each result item */
static void amxb_result_callback(const amxb_bus_ctx_t *bus_ctx, const amxc_var_t *const data, void *priv)
{
    (void)bus_ctx;
    (void)data;
    (void)priv;

    /* We handle results in the done callback */
}

/* Ambiorix done callback - called when request completes */
static void amxb_done_callback(const amxb_bus_ctx_t *bus_ctx, amxb_request_t *req, int status, void *priv)
{
    os_tr181_async_request_t *request = (os_tr181_async_request_t *)priv;
    os_tr181_val_t result = OS_VAL_INIT();
    os_tr181_error_t error = OS_TR181_SUCCESS;

    (void)bus_ctx;

    LOGD("Async method completed: %s, status=%d", request->method_name ?: "unknown", status);

    /* Check if request was cancelled */
    if (request->cancelled)
    {
        LOGD("Request was cancelled, skipping callback: %s", request->method_name ?: "unknown");
        os_tr181_async_request_free(request);
        return;
    }

    if (status != 0)
    {
        error = amxd_error_to_os_tr181((amxd_status_t)status);
    }
    else
    {
        /* Get result data from request */
        const amxc_var_t *result_data = req ? req->result : NULL;
        if (result_data && amxc_var_type_of(result_data) != AMXC_VAR_ID_NULL)
        {
            /* amxb_async_invoke returns RPC response as list: [result, args, error-code]
             * Extract first element (the actual result) to match CCSP behavior */
            const amxc_var_t *actual_result = result_data;

            if (amxc_var_type_of(result_data) == AMXC_VAR_ID_LIST)
            {
                actual_result = amxc_var_get_index(result_data, 0, AMXC_VAR_FLAG_DEFAULT);
                if (!actual_result)
                {
                    LOGE("Failed to extract result from RPC response list");
                    error = OS_TR181_ERROR;
                }
                else
                {
                    LOGD("Extracted result from RPC response list");
                }
            }

            /* Convert result if extraction succeeded */
            if (error == OS_TR181_SUCCESS && actual_result)
            {
                error = os_tr181_val_from_amx(&result, actual_result);
                if (error != OS_TR181_SUCCESS)
                {
                    LOGE("Failed to convert async result");
                }
            }
        }
    }

    /* Call user callback */
    if (request->callback)
    {
        request->callback(request->method_name, error, &result, request->priv);
    }

    /* Cleanup */
    os_val_free(&result);
    os_tr181_async_request_free(request);
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
    amxb_bus_ctx_t *bus_ctx = NULL;
    amxb_invoke_t *invoke_ctx = NULL;
    amxc_var_t amx_args;
    amxb_request_t *amxb_req = NULL;
    os_tr181_async_request_t *request = NULL;
    os_tr181_error_t os_ret = OS_TR181_SUCCESS;
    int amxb_ret;

    amxc_var_init(&amx_args);

    if (!handle || !method || !callback)
    {
        return OS_TR181_ERROR_INVALID;
    }

    method = device_path_fixup(handle, method);
    LOGD("Async invoke: method=%s, timeout=%d", method, timeout_sec);

    /* Allocate request structure */
    request = os_tr181_async_request_alloc(handle, method, callback, priv);
    if (!request)
    {
        return OS_TR181_ERROR;
    }

    /* Get bus context */
    bus_ctx = handle->bus_ctx;
    if (!bus_ctx)
    {
        LOGE("Bus context not available");
        os_tr181_async_request_free(request);
        return OS_TR181_ERROR;
    }

    /* Parse method path and create invoke context */
    char *object_path = NULL;
    char *method_name = NULL;
    os_ret = parse_method_path(method, &object_path, &method_name);
    if (os_ret != OS_TR181_SUCCESS)
    {
        os_tr181_async_request_free(request);
        return os_ret;
    }

    /* Create invoke context */
    amxb_ret = amxb_new_invoke(&invoke_ctx, bus_ctx, object_path, NULL, method_name);
    if (amxb_ret != 0)
    {
        LOGE("amxb_new_invoke failed: %d", amxb_ret);
        free(method_name);
        free(object_path);
        os_tr181_async_request_free(request);
        return OS_TR181_ERROR;
    }

    free(method_name);
    free(object_path);

    /* Convert args to amxc_var_t if provided */
    if (args && args->type != OS_TR181_TYPE_NONE)
    {
        os_ret = os_tr181_val_to_amx(args, &amx_args);
        if (os_ret != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert arguments");
            amxb_free_invoke(&invoke_ctx);
            os_tr181_async_request_free(request);
            return os_ret;
        }
    }

    /* Invoke asynchronously */
    amxb_ret = amxb_async_invoke(invoke_ctx, &amx_args, amxb_result_callback, amxb_done_callback, request, &amxb_req);
    if (amxb_ret != 0)
    {
        LOGE("amxb_async_invoke failed: %d", amxb_ret);
        amxc_var_clean(&amx_args);
        amxb_free_invoke(&invoke_ctx);
        os_tr181_async_request_free(request);
        return amxd_error_to_os_tr181((amxd_status_t)amxb_ret);
    }

    /* Store request handle */
    request->platform_handle = amxb_req;

    /* Return request to caller if requested */
    if (req)
    {
        *req = request;
    }

    amxc_var_clean(&amx_args);
    amxb_free_invoke(&invoke_ctx);

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_invoke_async_cancel(os_tr181_async_request_t *req)
{
    amxb_request_t *amxb_req = NULL;

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

    /* For Ambiorix, close the backend request to actually cancel it */
    amxb_req = (amxb_request_t *)req->platform_handle;
    if (amxb_req)
    {
        amxb_close_request(&amxb_req);
        req->platform_handle = NULL;
    }

    return OS_TR181_SUCCESS;
}

/* ========================================================================
 * Async Method Response (Provider-Side)
 * ======================================================================== */

os_tr181_error_t os_tr181_method_respond(
        os_tr181_async_method_ctx_t *async_ctx,
        os_tr181_error_t error,
        os_tr181_val_t *result)
{
    uint64_t call_id;
    amxc_var_t amx_result;
    amxd_status_t amxd_status;
    os_tr181_error_t os_ret = OS_TR181_SUCCESS;

    amxc_var_init(&amx_result);

    if (!async_ctx)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Get call_id directly from union */
    call_id = async_ctx->platform_id;

    LOGD("Sending async method response: method=%s, error=%d, call_id=%" PRIu64,
         async_ctx->method_name ?: "unknown",
         error,
         call_id);

    /* Convert result to amxc_var_t if provided */
    if (result && result->type != OS_TR181_TYPE_NONE && error == OS_TR181_SUCCESS)
    {
        os_ret = os_tr181_val_to_amx(result, &amx_result);
        if (os_ret != OS_TR181_SUCCESS)
        {
            LOGE("Failed to convert result");
            amxc_var_clean(&amx_result);
            os_tr181_async_method_ctx_free(async_ctx);
            return os_ret;
        }
    }

    /* Convert error to amxd_status */
    amxd_status = os_tr181_to_amxd_error(error);

    /* Send deferred response: out_args carries the output dict
     * amxd_status_t amxd_function_deferred_done(
     *   uint64_t call_id, amxd_status_t status, amxc_var_t* out_args, amxc_var_t* ret);
     */
    amxd_status_t ret = amxd_function_deferred_done(call_id, amxd_status, &amx_result, NULL);
    if (ret != amxd_status_ok)
    {
        LOGE("amxd_function_deferred_done failed: %d", ret);
        amxc_var_clean(&amx_result);
        os_tr181_async_method_ctx_free(async_ctx);
        return OS_TR181_ERROR;
    }

    /* Cleanup */
    amxc_var_clean(&amx_result);
    os_tr181_async_method_ctx_free(async_ctx);

    return OS_TR181_SUCCESS;
}

const char *os_tr181_get_backend_name(void)
{
    return "ambiorix";
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

/* ========================================================================
 * Event Loop Integration API - Ambiorix Backend
 * ======================================================================== */

os_tr181_error_t os_tr181_register_fd_change_callback(
        os_tr181_handle_t *handle,
        os_tr181_fd_change_callback_t callback,
        void *user_data)
{
    if (!handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    handle->fd_change_cb = callback;
    handle->fd_change_user_data = user_data;

    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_get_fds(os_tr181_handle_t *handle, int *fds, size_t max_fds, size_t *num_fds)
{
    size_t count = 0;
    os_tr181_error_t ret = OS_TR181_SUCCESS;

    if (!handle || !fds || !num_fds)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Primary bus FD (ubus) */
    int bus_fd = amxb_get_fd(handle->bus_ctx);
    if (bus_fd >= 0)
    {
        if (count >= max_fds) goto overflow;
        fds[count++] = bus_fd;
    }

    /* Signal FD (amxp) */
    int sig_fd = amxp_signal_fd();
    if (sig_fd >= 0)
    {
        if (count >= max_fds) goto overflow;
        fds[count++] = sig_fd;
    }

    /* USP bus FD (if connected) */
    if (handle->usp_connected && handle->usp_ctx)
    {
        int usp_fd = amxb_get_fd(handle->usp_ctx);
        if (usp_fd >= 0)
        {
            if (count >= max_fds) goto overflow;
            fds[count++] = usp_fd;
        }
    }

    goto done;

overflow:
    LOGD("os_tr181_get_fds: fds array too small (max_fds=%zu), some FDs not returned", max_fds);
    ret = OS_TR181_ERROR_OVERFLOW;

done:
    *num_fds = count;
    return ret;
}

int os_tr181_get_poll_timeout(os_tr181_handle_t *handle)
{
    int timeout_ms = -1;

    if (!handle)
    {
        return -1;
    }

    /* Check if USP reconnection is pending */
    if (!handle->usp_connected && handle->usp_reconnect_active)
    {
        uint64_t now_ms = get_current_time_ms();
        int usp_ms;

        /* Time until next retry */
        if (now_ms < handle->usp_next_retry_time_ms)
        {
            usp_ms = (int)(handle->usp_next_retry_time_ms - now_ms);
        }
        else
        {
            usp_ms = 0; /* Retry due now */
        }

        if (timeout_ms < 0 || usp_ms < timeout_ms) timeout_ms = usp_ms;
    }

    /* Check if a persistence save due is pending */
    if (handle->persistence.active && handle->persistence.save_due_ms != 0)
    {
        uint64_t now_ms = get_current_time_ms();
        int persist_ms;

        if (handle->persistence.save_due_ms > now_ms)
        {
            persist_ms = (int)(handle->persistence.save_due_ms - now_ms);
        }
        else
        {
            persist_ms = 0; /* Save due now */
        }

        if (timeout_ms < 0 || persist_ms < timeout_ms) timeout_ms = persist_ms;
    }

    return timeout_ms;
}
