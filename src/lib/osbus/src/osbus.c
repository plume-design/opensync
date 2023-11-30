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

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stddef.h>
#include <assert.h>

#include "log.h"
#include "os.h"
#include "util.h"
#include "memutil.h"
#include "kconfig.h"

#include "osbus.h"
#include "osbus_priv.h"
#include "osbus_ubus.h"
#include "osbus_rbus.h"

#define MODULE_ID LOG_MODULE_ID_OSBUS


static osbus_handle_t _g_osbus_default_handle = NULL;

_osbus_list_t _g_osbus_handles = _OSBUS_LIST_INIT("osbus_handles", sizeof(struct _osbus_handle));

osbus_bus_type_t osbus_get_default_type(void)
{
    if (kconfig_enabled(CONFIG_OSBUS_DEFAULT_UBUS)) {
        if (kconfig_enabled(CONFIG_OSBUS_UBUS)) {
            return OSBUS_BUS_TYPE_UBUS;
        }
    } else if (kconfig_enabled(CONFIG_OSBUS_DEFAULT_RBUS)) {
        if (kconfig_enabled(CONFIG_OSBUS_RBUS)) {
            return OSBUS_BUS_TYPE_RBUS;
        }
    }
    return OSBUS_BUS_TYPE_NONE;
}

// translate default and check if supported
osbus_bus_type_t osbus_get_type(osbus_bus_type_t bus_type)
{
    if (bus_type == OSBUS_BUS_TYPE_DEFAULT) {
        return osbus_get_default_type();
    }
    switch (bus_type) {
        case OSBUS_BUS_TYPE_UBUS:
            if (kconfig_enabled(CONFIG_OSBUS_UBUS)) {
                return bus_type;
            }
            break;
        case OSBUS_BUS_TYPE_RBUS:
            if (kconfig_enabled(CONFIG_OSBUS_RBUS)) {
                return bus_type;
            }
            break;
        default: break;
    }
    return OSBUS_BUS_TYPE_NONE;
}

char* osbus_bus_type_str(osbus_bus_type_t type)
{
    switch (type) {
        case OSBUS_BUS_TYPE_NONE: return "NONE";
        case OSBUS_BUS_TYPE_DEFAULT: return "DEFAULT";
        case OSBUS_BUS_TYPE_UBUS: return "UBUS";
        case OSBUS_BUS_TYPE_RBUS: return "RBUS";
        default: return "UNK";
    }
}

struct osbus_ops *osbus_get_ops(osbus_bus_type_t bus_type)
{
    switch (bus_type) {
        case OSBUS_BUS_TYPE_UBUS:
            if (kconfig_enabled(CONFIG_OSBUS_UBUS)) {
                return osbus_ubus_get_ops();
            }
            break;
        case OSBUS_BUS_TYPE_RBUS:
            if (kconfig_enabled(CONFIG_OSBUS_RBUS)) {
                return osbus_rbus_get_ops();
            }
            break;
        default: break;
    }
    return NULL;
}

bool osbus_set_type_and_ops(osbus_handle_t handle, osbus_bus_type_t bus_type)
{
    if (!handle) return false;
    handle->bus_type = osbus_get_type(bus_type);
    if (handle->bus_type == OSBUS_BUS_TYPE_NONE) {
        LOGE("bus %s not supported", osbus_bus_type_str(bus_type));
        return false;
    }
    handle->ops = osbus_get_ops(handle->bus_type);
    if (!handle->ops) {
        LOGE("bus %s no ops", osbus_bus_type_str(bus_type));
        return false;
    }
    return true;
}

osbus_error_t osbus_error_get(osbus_handle_t handle)
{
    if (!handle) return OSBUS_ERROR_INVALID_ARGUMENT;
    return handle->error;
}

char* osbus_error_get_str(osbus_handle_t handle)
{
    return osbus_error_str(handle ? handle->error : OSBUS_ERROR_INVALID_ARGUMENT);
}


void osbus_error_set(osbus_handle_t handle, osbus_error_t error)
{
    if (!handle) return;
    handle->error = error;
}

void osbus_error_general_if_unset(osbus_handle_t handle)
{
    if (!handle) return;
    if (handle->error == OSBUS_ERROR_SUCCESS) {
        handle->error = OSBUS_ERROR_GENERAL;
    }
}

void osbus_error_clear(osbus_handle_t handle)
{
    if (!handle) return;
    handle->error = OSBUS_ERROR_SUCCESS;
}

char* osbus_error_str(osbus_error_t error)
{
    switch (error) {
        case OSBUS_ERROR_SUCCESS:           return "SUCCESS";
        case OSBUS_ERROR_GENERAL:           return "ERROR_GENERAL";
        case OSBUS_ERROR_CONNECTION:        return "ERROR_CONNECTION";
        case OSBUS_ERROR_NO_RESOURCES:      return "ERROR_NO_RESOURCES";
        case OSBUS_ERROR_INVALID_ARGUMENT:  return "ERROR_INVALID_ARGUMENT";
        case OSBUS_ERROR_INVALID_OPERATION: return "ERROR_INVALID_OPERATION";
        case OSBUS_ERROR_INVALID_METHOD:    return "ERROR_INVALID_METHOD";
        case OSBUS_ERROR_INVALID_RESPONSE:  return "ERROR_INVALID_RESPONSE";
        case OSBUS_ERROR_EXISTS:            return "ERROR_EXISTS";
        case OSBUS_ERROR_NOT_FOUND:         return "ERROR_NOT_FOUND";
        case OSBUS_ERROR_PERMISSION_DENIED: return "ERROR_PERMISSION_DENIED";
        case OSBUS_ERROR_TIMEOUT:           return "ERROR_TIMEOUT";
    }
    return "ERROR_UNKNOWN";
}

__attribute__((format(printf, 4, 5)))
void osbus_error_set_and_log_fmt(osbus_handle_t handle, osbus_error_t error,
        log_severity_t level, const char *fmt, ...)
{
    if (error != OSBUS_ERROR_SUCCESS) {
        char msg[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        LOG_SEVERITY(level, "%s", msg);
    }
    osbus_error_set(handle, error);
}

void osbus_error_set_and_log_lvl(osbus_handle_t handle, osbus_error_t error,
        log_severity_t level, const char *errstr, const char *func, const char *param)
{
    osbus_error_set_and_log_fmt(handle, error, level,
            "%s(%s): %s %s", func, param ?: "", osbus_error_str(error), errstr ?: "");
}

void osbus_error_set_and_log(osbus_handle_t handle, osbus_error_t error,
        const char *errstr, const char *func, const char *param)
{
    osbus_error_set_and_log_fmt(handle, error, LOG_SEVERITY_DEBUG,
            "%s(%s): %s %s", func, param ?: "", osbus_error_str(error), errstr ?: "");
}


char* osbus_default_component_name_fmt(char *str, int size)
{
    char *log_name = (char*)log_get_name();
    // if log name not defined use "process_name-pid"
    *str = 0;
    if (!log_name || !*log_name) {
#ifdef __USE_GNU
        char *pn = program_invocation_short_name;
#else
        char *pn = "pid";
#endif
        snprintf(str, size, "%s-%u", pn, getpid());
    } else {
        snprintf(str, size, "%s", log_name);
    }
    return str;
}

char* osbus_default_component_name(void)
{
    static char name[OSBUS_NAME_SIZE];
    return osbus_default_component_name_fmt(name, sizeof(name));
}

bool _osbus_handle_set_path(osbus_handle_t handle, osbus_path_t path)
{
    char path_str[OSBUS_NAME_SIZE];
    if (!handle) return false;
    if (!path.component) path.component = osbus_default_component_name();
    if (!OSBUS_CHECK_OP(handle, op_osbus_path_fmt)) return false;
    path.element = NULL; // ignore element
    handle->component_name = STRDUP(path.component);
    handle->path = osbus_path(path.ns, handle->component_name, NULL);
    handle->ops->op_osbus_path_fmt(handle, path, path_str, sizeof(path_str));
    _osbus_list_set_name(&_g_osbus_handles, &handle->node, path_str);
    handle->component_path = handle->node.name;
    return true;
}

void _osbus_handle_free_path(osbus_handle_t handle)
{
    if (!handle) return;
    free(handle->component_name);
    handle->component_name = NULL;
    handle->path = osbus_path(0, NULL, NULL);
    _osbus_list_set_name(&_g_osbus_handles, &handle->node, NULL);
    handle->component_path = NULL;
}

bool _osbus_handle_init(
        osbus_handle_t handle,
        struct ev_loop *loop,
        osbus_bus_type_t bus_type,
        osbus_path_t path)
{
    if (!handle) return false;
    if (!osbus_set_type_and_ops(handle, bus_type)) return false;
    if (!_osbus_handle_set_path(handle, path)) return false;
    handle->loop = loop;
    // list node size can be overriden by bus specific connect
    _osbus_list_init(&handle->method_handler_list,"method_handler",sizeof(osbus_method_handler_t));
    _osbus_list_init(&handle->async_invoke_list,  "async_invoke",  sizeof(osbus_async_invoke_t));
    _osbus_list_init(&handle->async_reply_list,   "async_reply",   sizeof(osbus_async_reply_t));
    _osbus_list_init(&handle->event_reg_list,     "event_reg",     sizeof(osbus_event_reg_t));
    _osbus_list_init(&handle->event_handler_list, "event_handler", sizeof(osbus_event_handler_t));
    _osbus_list_init(&handle->topic_handler_list, "topic_handler", sizeof(osbus_topic_handler_t));
    return true;
}

void _osbus_handle_deinit(osbus_handle_t handle)
{
    if (!handle) return;
    _osbus_handle_free_path(handle);
    _osbus_list_delete_all(&handle->method_handler_list);
    _osbus_list_delete_all(&handle->async_invoke_list);
    _osbus_list_delete_all(&handle->async_reply_list);
    _osbus_list_delete_all(&handle->event_reg_list);
    _osbus_list_delete_all(&handle->event_handler_list);
    _osbus_list_delete_all(&handle->topic_handler_list);
    handle->ops = NULL;
}

bool osbus_connect(
        osbus_handle_t *phandle,
        struct ev_loop *loop,
        osbus_bus_type_t bus_type,
        osbus_path_t path)
{
    LOGT("%s(%s)", __func__, path.component);
    if (!phandle) return false;
    *phandle = NULL;
    osbus_handle_t h = _osbus_handle_new();
    if (!_osbus_handle_init(h, loop, bus_type, path)) goto err;
    if (!osbus_connect_bus(h)) goto err;
    *phandle = h;
    LOGI("%s(%s %s %p) = %p success", __func__,
            osbus_bus_type_str(h->bus_type), h->component_path, loop, h);
    return true;
err:
    LOGE("%s(%s) fail", __func__, path.component);
    _osbus_handle_delete(h);
    return false;
}

bool osbus_disconnect(osbus_handle_t handle)
{
    LOGD("%s(%p %s)", __func__, handle, osbus_handle_get_name(handle));
    if (!handle) return false;
    if (!osbus_disconnect_bus(handle)) return false;
    _osbus_handle_deinit(handle);
    _osbus_handle_delete(handle);
    return true;
}


bool osbus_set_default(osbus_handle_t handle)
{
    LOGT("%s(%p)", __func__, handle);
    _g_osbus_default_handle = handle;
    return true;
}

bool osbus_init_ex(
        struct ev_loop *loop,
        osbus_bus_type_t bus_type,
        osbus_path_t path)
{
    if (_g_osbus_default_handle) {
        LOGW("%s(%s) default connection already initialized", __func__, path.component);
        return false;
    }
    osbus_handle_t handle = NULL;
    if (!osbus_connect(&handle, loop, bus_type, path)) {
        return false;
    }
    osbus_set_default(handle);
    return true;
}

bool osbus_init_name(osbus_path_t path)
{
    return osbus_init_ex(EV_DEFAULT, OSBUS_BUS_TYPE_DEFAULT, path);
}

bool osbus_init(void)
{
    return osbus_init_ex(EV_DEFAULT, OSBUS_BUS_TYPE_DEFAULT, osbus_path_os(NULL, NULL));
}

osbus_handle_t osbus_default_handle(void)
{
    return _g_osbus_default_handle;
}

#define _OSBUS_API_IMPL_1(PATH_STR, OP, ...) \
    bool ret; \
    char *pstr = PATH_STR, *delim = " "; \
    if (!pstr) { pstr = delim = ""; } \
    LOGT("%s(%p%s%s)", __func__, handle, delim, pstr); \
    if (!OSBUS_CHECK_OP(handle, OP)) return false; \
    osbus_error_clear(handle); \
    ret = handle->ops->OP(__VA_ARGS__); \
    if (ret) { \
        osbus_error_clear(handle); \
    } else { \
        osbus_error_general_if_unset(handle); \
        LOGE("%s(%p%s%s): %s", __func__, handle, delim, pstr, \
                osbus_error_get_str(handle)); \
    }

#define _OSBUS_API_IMPL(OP,...) \
    _OSBUS_API_IMPL_1(osbus_path_dbg_fmta(path), OP, __VA_ARGS__) \
    return ret

#define _OSBUS_API_IMPL_NP(OP,...) \
    _OSBUS_API_IMPL_1(NULL, OP, __VA_ARGS__) \
    return ret

bool osbus_close(void)
{
    bool ret;
    ret = osbus_disconnect(OSBUS_DEFAULT);
    _g_osbus_default_handle = NULL;
    return ret;
}

osbus_path_t osbus_handle_get_path(osbus_handle_t handle)
{
    return handle->path;
}

const char* osbus_handle_get_name(osbus_handle_t handle)
{
    return handle ? handle->component_path : NULL;
}

bool osbus_path_fmt(
        osbus_handle_t handle,
        osbus_path_t path,
        char *path_str,
        int str_size)
{
    bool ret;
    if (!OSBUS_CHECK_OP(handle, op_osbus_path_fmt)) return false;
    ret = handle->ops->op_osbus_path_fmt(handle, path, path_str, str_size);
    return ret;
}

// if component == NULL, use defualt - self component name
// if an empty path is needed use an empty string ""

bool osbus_path_cmn_fmt(osbus_handle_t handle, osbus_path_t path, char *str, int size)
{
    int ret;
    bool dot = false;
    if (!str || !size) return false;
    osbus_namespace_t ns = path.ns;
    char *component      = path.component;
    char *element        = path.element;
    if (component == NULL) {
        if (handle) {
            ns = handle->path.ns;
            component = handle->component_name;
        } else {
            component = osbus_default_component_name();
        }
    }
    *str = 0;
    if (ns == OSBUS_NS_OPENSYNC) {
        ret = snprintf(str, size, "OpenSync");
        if (ret >= size) goto err_size;
        str += ret;
        size -= ret;
        dot = true;
    }
    if (component && *component) {
        ret = snprintf(str, size, "%s%s", dot?".":"", component);
        if (ret >= size) goto err_size;
        str += ret;
        size -= ret;
        dot = true;
    }
    if (element && *element) {
        ret = snprintf(str, size, "%s%s", dot?".":"", element);
        if (ret >= size) goto err_size;
    }
    return true;
err_size:
    LOGD("%s(%d,%s,%s): size %d >= %d", __func__, ns, component, element, ret, size);
    return false;
}

char *osbus_ns_name(osbus_namespace_t ns)
{
    switch (ns) {
        case OSBUS_NS_GLOBAL: return "GBL";
        case OSBUS_NS_OPENSYNC: return "OSYNC";
        default: return "UNK";
    }
}

char* osbus_path_dbg_fmt(osbus_path_t path, char *str, int str_size)
{
    if (path.element) {
        snprintf(str, str_size, "%s:%s:%s", osbus_ns_name(path.ns), path.component, path.element);
    } else {
        snprintf(str, str_size, "%s:%s", osbus_ns_name(path.ns), path.component);
    }
    return str;
}

static inline bool _osbus_handle_check_null(osbus_handle_t handle, const char *func)
{
    if (!handle) {
        LOGE("%s handle = NULL", func);
        return false;
    }
    return true;
}

bool osbus_connect_bus(osbus_handle_t handle)
{
    if (!_osbus_handle_check_null(handle, __func__)) return false;
    _OSBUS_API_IMPL_1(handle->component_path, op_osbus_connect_bus, handle);
    if (ret) _osbus_handle_set_bus_ctx(handle, handle->bus_handle);
    return ret;
}

bool osbus_disconnect_bus(osbus_handle_t handle)
{
    if (!_osbus_handle_check_null(handle, __func__)) return false;
    _OSBUS_API_IMPL_1(handle->component_path, op_osbus_disconnect_bus, handle);
    _osbus_handle_set_bus_ctx(handle, NULL);
    return ret;
}


// list

bool osbus_list_msg(
        osbus_handle_t handle,
        osbus_path_t path,
        bool include_elements,
        osbus_msg_t **list)
{
    _OSBUS_API_IMPL(op_osbus_list_msg, handle, path, include_elements, list);
}


// method

bool osbus_method_register(
        osbus_handle_t handle,
        const osbus_method_t *methods,
        int n_methods)
{
    _OSBUS_API_IMPL_NP(op_osbus_method_register, handle, methods, n_methods);
}

bool osbus_method_unregister(
        osbus_handle_t handle,
        const osbus_method_t *methods,
        int n_methods)
{
    _OSBUS_API_IMPL_NP(op_osbus_method_unregister, handle, methods, n_methods);
}

bool osbus_method_invoke(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *message,
        osbus_msg_t **reply)
{
    _OSBUS_API_IMPL(op_osbus_method_invoke, handle, path, message, reply);
}

bool osbus_method_invoke_async(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *message,
        osbus_method_invoke_async_handler_fn_t *reply_handler_fn,
        void *user_data,
        osbus_async_invoke_t **async)
{
    _OSBUS_API_IMPL(op_osbus_method_invoke_async, handle, path, message, reply_handler_fn, user_data, async);
}

#if 0
// Disabled. Waiting on an async invocation is not considered
// a proper approach for handling async methods
bool osbus_method_invoke_async_wait(
        osbus_handle_t handle,
        osbus_async_invoke_t *async,
        int timeout)
{
    if (!_osbus_async_invoke_is_valid(handle, async, __func__)) return false;
    _OSBUS_API_IMPL_NP(op_osbus_method_invoke_async_wait, handle, async, timeout);
}
#endif

bool osbus_method_invoke_async_cancel(
        osbus_handle_t handle,
        osbus_async_invoke_t *async)
{
    if (!_osbus_async_invoke_is_valid(handle, async, __func__)) return false;
    _OSBUS_API_IMPL_NP(op_osbus_method_invoke_async_cancel, handle, async);
}

bool osbus_method_reply_async(
        osbus_handle_t handle,
        osbus_async_reply_t *reply_handle,
        bool status,
        const osbus_msg_t *reply)
{
    if (!_osbus_async_reply_is_valid(handle, reply_handle, __func__)) return false;
    _OSBUS_API_IMPL_NP(op_osbus_method_reply_async, handle, reply_handle, status, reply);
}


// events provide a "1:N" communication concept
// and are tied to an object which has to be registered first
bool osbus_event_register(
        osbus_handle_t handle,
        osbus_path_t path)
{
    _OSBUS_API_IMPL(op_osbus_event_register, handle, path);
}

bool osbus_event_subscribe(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_event_handler_fn_t *event_handler_fn,
        void *user_data)
{
    _OSBUS_API_IMPL(op_osbus_event_subscribe, handle, path, event_handler_fn, user_data);
}

bool osbus_event_publish(
        osbus_handle_t handle,
        osbus_path_t path,
        const char *event_name,
        const osbus_msg_t *msg)
{
    _OSBUS_API_IMPL(op_osbus_event_publish, handle, path, event_name, msg);
}


// topics provide a "M:N" communication concept
// the topic does not need to be registered upfront
// anyone can send and listen to same topic
bool osbus_topic_listen(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_topic_handler_fn_t *topic_handler_fn,
        void *user_data)
{
    _OSBUS_API_IMPL(op_osbus_topic_listen, handle, path, topic_handler_fn, user_data);
}

bool osbus_topic_unlisten(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_topic_handler_fn_t *topic_handler_fn)
{
    _OSBUS_API_IMPL(op_osbus_topic_unlisten, handle, path, topic_handler_fn);
}

bool osbus_topic_send(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *msg)
{
    _OSBUS_API_IMPL(op_osbus_topic_send, handle, path, msg);
}


// data model

bool osbus_dm_get(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_msg_t **value)
{
    _OSBUS_API_IMPL(op_osbus_dm_get, handle, path, value);
}

bool osbus_dm_set(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *value)
{
    _OSBUS_API_IMPL(op_osbus_dm_set, handle, path, value);
}

