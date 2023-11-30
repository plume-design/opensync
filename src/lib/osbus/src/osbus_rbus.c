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

#include <unistd.h> // gettid
#include <syscall.h> // gettid
#include <rbus.h>
#include <rtMemory.h>
#include <rbuscore_message.h>
#include <rbuscore.h>
#include <rtLog.h>

#include "log.h"
#include "os.h"
#include "util.h"
#include "memutil.h"
#include "os_time.h"

#include "osbus.h"
#include "osbus_priv.h"
#include "osbus_rbus.h"
#include "osbus_msg_rbus.h"

#define MODULE_ID LOG_MODULE_ID_OSBUS

static int g_ev_pipe[2] = { -1, -1 };
static struct ev_loop *_g_rb_loop;
static struct ev_io _g_rb_ev_io;
static int _g_rb_ref_count = 0;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_cond_main = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_main_locked = false;

typedef struct osbus_rbus_async_invoke
{
    osbus_async_invoke_t osbus_async_invoke;
    bool cancelled;
} osbus_rbus_async_invoke_t;

typedef struct osbus_rbus_async_reply
{
    osbus_async_reply_t osbus_async_reply;
    rbusMethodAsyncHandle_t rbus_async_handle;
} osbus_rbus_async_reply_t;

static bool _osbus_rbus_attach_loop(struct ev_loop *loop);
static bool _osbus_rbus_detach_loop();

osbus_error_t osbus_error_from_rbus(rbusError_t error)
{
    switch (error) {
        case RBUS_ERROR_SUCCESS:                    return OSBUS_ERROR_SUCCESS;
        case RBUS_ERROR_BUS_ERROR:                  return OSBUS_ERROR_GENERAL;
        case RBUS_ERROR_INVALID_INPUT:              return OSBUS_ERROR_INVALID_ARGUMENT;
        case RBUS_ERROR_NOT_INITIALIZED:            return OSBUS_ERROR_CONNECTION;
        case RBUS_ERROR_OUT_OF_RESOURCES:           return OSBUS_ERROR_NO_RESOURCES;
        case RBUS_ERROR_DESTINATION_NOT_FOUND:      return OSBUS_ERROR_NOT_FOUND;
        case RBUS_ERROR_DESTINATION_NOT_REACHABLE:  return OSBUS_ERROR_NOT_FOUND;
        case RBUS_ERROR_DESTINATION_RESPONSE_FAILURE: return OSBUS_ERROR_INVALID_RESPONSE;
        case RBUS_ERROR_INVALID_RESPONSE_FROM_DESTINATION: return OSBUS_ERROR_INVALID_RESPONSE;
        case RBUS_ERROR_INVALID_OPERATION:          return OSBUS_ERROR_INVALID_OPERATION;
        case RBUS_ERROR_INVALID_EVENT:              return OSBUS_ERROR_NOT_FOUND;
        case RBUS_ERROR_INVALID_HANDLE:             return OSBUS_ERROR_NOT_FOUND;
        case RBUS_ERROR_SESSION_ALREADY_EXIST:      return OSBUS_ERROR_GENERAL;
        case RBUS_ERROR_COMPONENT_NAME_DUPLICATE:   return OSBUS_ERROR_EXISTS;
        case RBUS_ERROR_ELEMENT_NAME_DUPLICATE:     return OSBUS_ERROR_EXISTS;
        case RBUS_ERROR_ELEMENT_NAME_MISSING:       return OSBUS_ERROR_GENERAL;
        case RBUS_ERROR_COMPONENT_DOES_NOT_EXIST:   return OSBUS_ERROR_GENERAL;
        case RBUS_ERROR_ELEMENT_DOES_NOT_EXIST:     return OSBUS_ERROR_GENERAL;
        case RBUS_ERROR_ACCESS_NOT_ALLOWED:         return OSBUS_ERROR_PERMISSION_DENIED;
        case RBUS_ERROR_INVALID_CONTEXT:            return OSBUS_ERROR_GENERAL;
        case RBUS_ERROR_TIMEOUT:                    return OSBUS_ERROR_TIMEOUT;
        case RBUS_ERROR_ASYNC_RESPONSE:             return OSBUS_ERROR_GENERAL;
        case RBUS_ERROR_INVALID_METHOD:             return OSBUS_ERROR_INVALID_METHOD;
        case RBUS_ERROR_NOSUBSCRIBERS:              return OSBUS_ERROR_GENERAL;
        case RBUS_ERROR_SUBSCRIPTION_ALREADY_EXIST: return OSBUS_ERROR_GENERAL;
        default:                                    return OSBUS_ERROR_GENERAL;
    }
}

rbusError_t osbus_error_to_rbus(osbus_error_t error)
{
    switch (error) {
        case OSBUS_ERROR_SUCCESS:           return RBUS_ERROR_SUCCESS;
        case OSBUS_ERROR_GENERAL:           return RBUS_ERROR_BUS_ERROR;
        case OSBUS_ERROR_CONNECTION:        return RBUS_ERROR_NOT_INITIALIZED;
        case OSBUS_ERROR_NO_RESOURCES:      return RBUS_ERROR_OUT_OF_RESOURCES;
        case OSBUS_ERROR_INVALID_ARGUMENT:  return RBUS_ERROR_INVALID_INPUT;
        case OSBUS_ERROR_INVALID_OPERATION: return RBUS_ERROR_INVALID_OPERATION;
        case OSBUS_ERROR_INVALID_METHOD:    return RBUS_ERROR_INVALID_METHOD;
        case OSBUS_ERROR_INVALID_RESPONSE:  return RBUS_ERROR_INVALID_RESPONSE_FROM_DESTINATION;
        case OSBUS_ERROR_EXISTS:            return RBUS_ERROR_ELEMENT_NAME_DUPLICATE;
        case OSBUS_ERROR_NOT_FOUND:         return RBUS_ERROR_DESTINATION_NOT_FOUND;
        case OSBUS_ERROR_PERMISSION_DENIED: return RBUS_ERROR_ACCESS_NOT_ALLOWED;
        case OSBUS_ERROR_TIMEOUT:           return RBUS_ERROR_TIMEOUT;
    }
    return RBUS_ERROR_BUS_ERROR;
}

void osbus_rbus_error_set_and_log(osbus_handle_t handle, rbusError_t rbus_error,
        const char *func, const char *param)
{
    osbus_error_set_and_log(handle, osbus_error_from_rbus(rbus_error),
            rbusError_ToString(rbus_error), func, param);
}

void osbus_rbus_error_set_and_logERR(osbus_handle_t handle, rbusError_t rbus_error,
        const char *func, const char *param)
{
    osbus_error_set_and_log_lvl(handle, osbus_error_from_rbus(rbus_error),
            LOG_SEVERITY_ERROR, rbusError_ToString(rbus_error), func, param);
}

bool osbus_rbus_path_fmt(osbus_handle_t handle, osbus_path_t path, char *str, int size)
{
    return osbus_path_cmn_fmt(handle, path, str, size);
}

char* _osbus_rbus_path_fmt(osbus_handle_t handle, osbus_path_t path, char *str, int size)
{
    if (osbus_rbus_path_fmt(handle, path, str, size)) {
        return str;
    }
    return NULL;
}

#define osbus_rbus_path_fmta(H, P) _osbus_rbus_path_fmt(H, P, alloca(OSBUS_NAME_SIZE), OSBUS_NAME_SIZE)

char *osbus_rbus_element_from_path(osbus_handle_t handle, const char *path)
{
    // extract element name from full path
    int len = strlen(handle->component_path);
    if (!strncmp(handle->component_path, path, len)) {
        char *e = (char*)path + len;
        if (*e == '.') e++;
        return e;
    }
    return (char*)path;
}

bool osbus_rbus_method_name_fmt(osbus_handle_t handle, char *name, char *str, int size)
{
    int n = snprintf(str, size, "%s.%s", handle->component_path, name);
    if (n < 0 || n >= size) return false;
    return true;
}

void osbus_rbus_handle_disconnect(void)
{
    // abort so that we get restarted by dm
    // and then we can reconnect to the bus
    bool connected = false;
    ASSERT(connected, "rbus disconnected");
}


void osbus_rbus_log_handler(rtLogLevel level, const char* file, int line, int threadId, char* message)
{
    int ll;
    // map rbus log level to a lower severity
    // when `rbuscli method_noargs` is called, rbus will log these errors:
    // rbuscore_message.c:217 [3023] rbusMessage_GetInt32 failed to unpack next item
    // rbuscore_message.c:217 [3023] rbusMessage_GetInt32 failed to unpack next item
    // which are harmless
    switch (level) {
        case RT_LOG_DEBUG:  ll = LOG_SEVERITY_TRACE; break;
        case RT_LOG_INFO:   ll = LOG_SEVERITY_TRACE; break;
        case RT_LOG_WARN:   ll = LOG_SEVERITY_DEBUG; break;
        case RT_LOG_ERROR:  ll = LOG_SEVERITY_DEBUG; break;
        case RT_LOG_FATAL:  ll = LOG_SEVERITY_WARN;  break;
        default:            ll = LOG_SEVERITY_DEBUG; break;
    }
    // const char *level_str = rtLogLevelToString(level);
    // not including rbus level as string as it's misleading and makes it harder to parse
    LOG_SEVERITY(ll, "RBUS<%d> %s:%d [%d] %s", level, file, line, threadId, message);

    // in case of a rtrouted crash we get these log messages:
    // RBUS<3> rtConnection.c:522 [3082] Failed to read error : Transport endpoint is not connected
    // RBUS<3> rtConnection.c:1849 [3082] Reader failed with error 0x9.
    // there is no other callback notification so we'll use this
    if (strstr(message, "endpoint is not connected") ||
            strstr(message, "Reader failed with error"))
    {
        osbus_rbus_handle_disconnect();
    }
}

bool osbus_rbus_connect(osbus_handle_t handle)
{
    if (!handle) return false;

    if (_g_rb_ref_count > 0) {
        if (handle->loop != _g_rb_loop) {
            LOGE("multiple loop not implemented %p %p", handle->loop, _g_rb_loop);
            return false;
        }
    }

    rtLogSetLogHandler(&osbus_rbus_log_handler);

    rbusHandle_t rbus_handle;
    int rc = rbus_open(&rbus_handle, handle->component_path);
    if (rc != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rc, "rbus_open", handle->component_path);
        return false;
    }
    handle->bus_handle = rbus_handle;
    handle->async_invoke_list.node_size = sizeof(osbus_rbus_async_invoke_t);
    handle->async_reply_list.node_size = sizeof(osbus_rbus_async_reply_t);
    handle->event_handler_list.node_size = sizeof(osbus_event_handler_t);
    handle->topic_handler_list.node_size = sizeof(osbus_topic_handler_t);

    if (_g_rb_ref_count == 0) {
        _osbus_rbus_attach_loop(handle->loop);
    }
    _g_rb_ref_count++;

    return true;
}

bool osbus_rbus_disconnect(osbus_handle_t handle)
{
    if (!handle) return false;
    LOGT("%s %p %p %d", __func__, handle, handle->bus_handle, _g_rb_ref_count);
    bool retval = true;
    rbusHandle_t rbus_handle = handle->bus_handle;
    rbusError_t rc = rbus_close(rbus_handle);
    LOGT("%s(%p) rbus_close(%p) = %d", __func__, handle, rbus_handle, rc);
    if (rc != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rc, "rbus_close", handle->component_path);
        retval = false;
    }
    if (_g_rb_ref_count > 0) {
        _g_rb_ref_count--;
        if (_g_rb_ref_count == 0) {
            _osbus_rbus_detach_loop();
        }
    }
    return retval;
}

static inline int _my_gettid(void)
{
    return syscall(SYS_gettid);
}

void _osbus_rbus_lock_main(void)
{
    int n;
    char c = 'L';
    //LOGT("rbus_lock: %d thread: locking main\n", _my_gettid());
    //LOGT("rbus_lock: %d thread: write (%d)\n", _my_gettid(), g_ev_pipe[1]);
    n = write(g_ev_pipe[1], &c, 1);
    (void)n;
    //LOGT("rbus_lock: %d thread: write (%d) = %d\n", _my_gettid(), g_ev_pipe[1], n);
    //LOGT("rbus_lock: %d thread: mutex_lock\n", _my_gettid());
    pthread_mutex_lock(&g_lock);
    //LOGT("rbus_lock: %d thread: cond_wait\n", _my_gettid());
    while (!g_main_locked) {
        pthread_cond_wait(&g_cond, &g_lock);
        //LOGT("rbus_lock: %d thread: cond_wait main locked: %d\n", _my_gettid(), g_main_locked);
    }
    LOGT("rbus_lock: %d thread: main locked\n", _my_gettid());
}

void _osbus_rbus_unlock_main(void)
{
    //LOGT("rbus_lock: %d thread: unlocking main\n", _my_gettid());
    g_main_locked = false;
    pthread_cond_signal(&g_cond_main);
    pthread_mutex_unlock(&g_lock);
    LOGT("rbus_lock: %d thread: main unlocked\n", _my_gettid());
}

static void _osbus_rbus_cond_main_read_fd(int fd)
{
    int n;
    char c;
    //n = recv(fd, &c, 1, 0);
    n = read(fd, &c, 1);
    (void)n;
    //LOGT("rbus_lock: %d read: %d '%c'\n", _my_gettid(), n, c);
    pthread_mutex_lock(&g_lock);
    g_main_locked = true;
    LOGT("rbus_lock: %d main: locked\n", _my_gettid());
    pthread_cond_signal(&g_cond);
    while (g_main_locked) {
        pthread_cond_wait(&g_cond_main, &g_lock);
        //LOGT("rbus_lock: %d main: cond_wait main locked: %d\n", _my_gettid(), g_main_locked);
    }
    pthread_mutex_unlock(&g_lock);
    LOGT("rbus_lock: %d main: unlocked\n", _my_gettid());
}

static bool _osbus_rbus_cond_main_wait_lock_unlock(int timeout_ms)
{
    int fd = g_ev_pipe[0];
    uint64_t t1 = clock_mono_ms();
    int remain_ms = timeout_ms;
    struct timeval tv, *ptv = &tv;
    fd_set rfd;
retry:
    FD_ZERO(&rfd);
    FD_SET(g_ev_pipe[0], &rfd);
    if (timeout_ms == 0) {
        ptv = NULL;
    } else {
        tv.tv_sec = remain_ms / 1000;
        tv.tv_usec = (remain_ms % 1000) * 1000;
    }
    int ret = select(fd + 1, &rfd, NULL, NULL, ptv);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            if (timeout_ms) {
                remain_ms = timeout_ms - (clock_mono_ms() - t1);
                if (remain_ms <= 0) return false;
            }
            goto retry;
        }
        LOGD("rbus_lock: %s %d %d", __func__, ret, errno);
        return false;
    } else if (ret > 0) {
        if (FD_ISSET(fd, &rfd)) {
            _osbus_rbus_cond_main_read_fd(fd);
            return true;
        }
    }
    // timeout
    return false;
}

static void _osbus_rbus_ev_read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
    _osbus_rbus_cond_main_read_fd(watcher->fd);
}

static bool _osbus_rbus_attach_loop(struct ev_loop *loop)
{
    if (!loop) return false;
    pipe(g_ev_pipe);
    ev_io_init(&_g_rb_ev_io, _osbus_rbus_ev_read_cb, g_ev_pipe[0], EV_READ);
    ev_io_start(loop, &_g_rb_ev_io);
    _g_rb_loop = loop;
    return true;
}

static bool _osbus_rbus_detach_loop()
{
    struct ev_loop *loop = _g_rb_loop;
    struct ev_io *ev = &_g_rb_ev_io;
    LOGT("%s %p", __func__, loop);
    if (ev_is_active(ev)) {
        ev_io_stop(loop, ev);
    }
    close(g_ev_pipe[0]);
    close(g_ev_pipe[1]);
    g_ev_pipe[0] = -1;
    g_ev_pipe[1] = -1;
    _g_rb_loop = NULL;
    return true;
}

// list

bool osbus_rbus_list(
        osbus_handle_t handle,
        osbus_path_t path,
        bool include_elements,
        osbus_msg_t **list)
{
    rbusHandle_t rbusHandle = handle->bus_handle;
    rbusCoreError_t rc;
    rbusError_t r;
    int count = 0;
    char **cnames = NULL;
    int num_elements = 0;
    char **rbus_elements = NULL;
    rbusElementInfo_t *einfo = NULL;
    int i, j;
    char *path_str = osbus_rbus_path_fmta(handle, path);
    if (!path_str) return false;

    if (*path_str) {
        rc = rbus_discoverWildcardDestinations(path_str, &count, &cnames);
    } else {
        rc = rbus_discoverRegisteredComponents(&count, &cnames);
    }
    if (rc != RBUSCORE_SUCCESS) return false;
    *list = osbus_msg_new_array();
    for (i = 0; i < count; i++) {
        LOGT("component: %d %s", i, cnames[i]);
        osbus_msg_t *item = osbus_msg_add_item_object(*list);
        osbus_msg_t *prop = osbus_msg_set_prop_object(item, cnames[i]);
        osbus_msg_set_prop_string(prop, "type", "object");
        if (include_elements) {
            num_elements = 0;
            rbus_elements = NULL;
            r = rbus_discoverComponentDataElements(rbusHandle, cnames[i], false, &num_elements, &rbus_elements);
            if (r == RBUS_ERROR_SUCCESS) {
                osbus_msg_t *elements = osbus_msg_set_prop_object(prop, "elements");
                for (j = 0; j < num_elements; j++) {
                    LOGT("element: %d %d %s", i, j, rbus_elements[j]);
                    osbus_msg_t *e = osbus_msg_set_prop_object(elements, rbus_elements[j]);
                    einfo = NULL;
                    char *type_str = NULL;
                    if (!strcmp(cnames[i], rbus_elements[j])) {
                        type_str = "object";
                        // query object with rbusElementInfo_get will hang
                    } else {
                        r = rbusElementInfo_get(rbusHandle, rbus_elements[j], 0, &einfo);
                        if (r == RBUS_ERROR_SUCCESS) {
                            switch (einfo->type) {
                                case RBUS_ELEMENT_TYPE_PROPERTY: type_str = "prop"; break;
                                case RBUS_ELEMENT_TYPE_TABLE: type_str = "table"; break;
                                case RBUS_ELEMENT_TYPE_EVENT: type_str = "event"; break;
                                case RBUS_ELEMENT_TYPE_METHOD: type_str = "method"; break;
                                default: type_str = NULL; break;
                            }
                            free(einfo);
                        }
                    }
                    if (type_str) osbus_msg_set_prop_string(e, "type", type_str);
                    free(rbus_elements[j]);
                }
                free(rbus_elements);
            }
        }
        free(cnames[i]);
    }
    free(cnames);
    return true;
}


// method


rbusError_t _osbus_rbus_method_handler(
        rbusHandle_t rbusHandle,
        char const* methodName,
        rbusObject_t inParams,
        rbusObject_t outParams,
        rbusMethodAsyncHandle_t asyncHandle)
{
    LOGT("%s r:%p m:%s i:%p o:%p a:%p", __func__,
            rbusHandle, methodName, inParams, outParams, asyncHandle);

    rbusError_t retval = RBUS_ERROR_BUS_ERROR;
    osbus_handle_t handle = NULL;
    osbus_method_handler_t *m = NULL;
    osbus_msg_t *d = NULL;
    osbus_msg_t *reply = NULL;
    osbus_rbus_async_reply_t *rbus_reply_handle = NULL;
    osbus_async_reply_t *reply_handle = NULL;
    char dbg_str[OSBUS_DBG_STR_SIZE];
    char *short_method_name = NULL;
    bool defer_reply = false;
    bool ret;

    _osbus_rbus_lock_main();

    handle = _osbus_handle_find_bus_ctx(rbusHandle);
    m = _osbus_method_handler_find_name(handle, methodName);
    if (!m || !m->handler_fn) {
        LOGE("method '%s' not found", methodName);
        retval = RBUS_ERROR_INVALID_METHOD;
        goto err;
    }
    if (inParams) {
        if (!osbus_msg_from_rbus_object(&d, inParams)) {
            LOGE("method '%s' invalid input", methodName);
            retval = RBUS_ERROR_INVALID_INPUT;
            goto err;
        }
    }
    short_method_name = m->method_name;
    osbus_msg_to_dbg_str_fixed(d, dbg_str, sizeof(dbg_str));
    LOGT("%s %s method: %s msg: %s", __func__, handle->component_path, short_method_name, dbg_str);

    osbus_error_clear(handle);
    reply_handle = _osbus_async_reply_alloc(handle);
    rbus_reply_handle = _OSBUS_LIST_CAST(osbus_rbus_async_reply_t, reply_handle);
    ret = m->handler_fn(handle, short_method_name, d, &reply, &defer_reply, reply_handle);
    if (!ret) {
        retval = osbus_error_to_rbus(osbus_error_get(handle));
        if (retval == RBUS_ERROR_SUCCESS) retval = RBUS_ERROR_BUS_ERROR;
        // ASYNC_RESPONSE is a special case don't send it through
        if (retval == RBUS_ERROR_ASYNC_RESPONSE) retval = RBUS_ERROR_BUS_ERROR;
    } else if (reply && defer_reply) {
        LOGT("%s(%s) error: both reply and defer set", __func__, methodName);
        retval = RBUS_ERROR_INVALID_INPUT;
        defer_reply = false;
        ret = false;
    }
    if (!ret) goto err;
    if (reply) {
        osbus_msg_to_dbg_str_fixed(reply, dbg_str, sizeof(dbg_str));
        LOGT("%s(%s) reply: %s", __func__, methodName, dbg_str);
        if (!osbus_msg_append_to_rbus_object(reply, outParams)) {
            retval = RBUS_ERROR_INVALID_INPUT;
            goto err;
        }
        retval = RBUS_ERROR_SUCCESS;
    } else if (defer_reply) {
        LOGT("%s(%s) deferred reply %p %p", __func__, methodName, reply_handle, asyncHandle);
        _osbus_async_reply_insert(handle, reply_handle, methodName);
        rbus_reply_handle->rbus_async_handle = asyncHandle;
        // RBUS_ERROR_ASYNC_RESPONSE: The method request
        // will be handle asynchronously by provider
        retval = RBUS_ERROR_ASYNC_RESPONSE;
        goto out;
    } else {
        LOGT("%s(%s) no reply", __func__, methodName);
        retval = RBUS_ERROR_SUCCESS;
    }
err:
    if (retval != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_logERR(handle, retval, __func__,
                strfmta("%s, %s", handle->component_path, methodName));
    }
out:
    if (!defer_reply) _osbus_async_reply_free(reply_handle);
    osbus_error_clear(handle);
    osbus_msg_free(reply);
    _osbus_rbus_unlock_main();
    return retval;
}


bool osbus_rbus_method_reply_async(
        osbus_handle_t handle,
        osbus_async_reply_t *reply_handle,
        bool status,
        const osbus_msg_t *reply)
{
    LOGT("%s(%p, %p, %d)", __func__, handle, reply_handle, status);
    if (!handle) return false;
    //rbusHandle_t rbusHandle = handle->bus_handle;
    rbusError_t rbus_status;
    rbusError_t rbus_retval;
    rbusObject_t outParams = NULL;
    osbus_rbus_async_reply_t *rbus_reply_handle = (osbus_rbus_async_reply_t*)reply_handle;
    char *methodName = reply_handle->node.name;
    bool retval = true;

    if (status) {
        rbus_status = RBUS_ERROR_SUCCESS;
    } else {
        rbus_status = osbus_error_to_rbus(osbus_error_get(handle));
        if (rbus_status == RBUS_ERROR_SUCCESS) rbus_status = RBUS_ERROR_BUS_ERROR;
    }
    if (status && reply) {
        retval = osbus_msg_to_rbus_object(reply, &outParams);
        if (!retval) {
            rbus_status = RBUS_ERROR_INVALID_INPUT;
            osbus_rbus_error_set_and_log(handle, rbus_status, __func__,
                    strfmta("%s, %s", handle->component_path, methodName));
        }
    }
    /*rbusError_t rbusMethod_SendAsyncResponse(
      rbusMethodAsyncHandle_t asyncHandle,
      rbusError_t error,
      rbusObject_t outParams);*/
    rbus_retval = rbusMethod_SendAsyncResponse(
            rbus_reply_handle->rbus_async_handle,
            rbus_status,
            outParams);
    if (rbus_retval != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rbus_retval, __func__,
                strfmta("%s, %s", handle->component_path, methodName));
        retval = false;
    }
    if (outParams) rbusObject_Release(outParams);
    _osbus_async_reply_delete(handle, reply_handle);
    return retval;
}

static bool _osbus_rbus_DataElement_from_method(osbus_handle_t handle, osbus_method_handler_t *mh, int i, void *user_ptr)
{
    rbusDataElement_t *rm = ((rbusDataElement_t*)user_ptr) + i;
    rm->name = mh->node.name;
    rm->type = RBUS_ELEMENT_TYPE_METHOD;
    rm->cbTable.methodHandler = _osbus_rbus_method_handler;
    return true;
}

bool osbus_rbus_method_register(osbus_handle_t handle, const osbus_method_t *methods, int n_methods)
{
    LOGT("%s:%d", __func__, __LINE__);
    if (!handle) return false;
    rbusHandle_t        rbusHandle = handle->bus_handle;
    rbusDataElement_t   *rbus_de = NULL;
    bool retval = false;

    if (!_osbus_method_table_validate(handle, methods, n_methods)) {
        return false;
    }
    if (!_osbus_method_handler_check_duplicate(handle, methods, n_methods)) {
        return false;
    }

    rbus_de = CALLOC(sizeof(*rbus_de), n_methods);

    if (!_osbus_method_handler_add_from_table(handle, methods, n_methods,
                _osbus_rbus_DataElement_from_method, rbus_de))
    {
        goto out;
    }

    rbusError_t rc = rbus_regDataElements(rbusHandle, n_methods, rbus_de);
    if (rc != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rc, "rbus_regDataElements", handle->component_path);
        goto out;
    }
    retval = true;

out:
    free(rbus_de);
    return retval;
}

bool osbus_rbus_method_unregister(osbus_handle_t handle, const osbus_method_t *methods, int n_methods)
{
    LOGT("%s:%d", __func__, __LINE__);
    if (!handle) return false;
    rbusHandle_t        rbusHandle = handle->bus_handle;
    rbusDataElement_t   *rbus_de = NULL;
    osbus_method_handler_t *mh = NULL;
    const osbus_method_t *in = NULL;
    char method_name[OSBUS_NAME_SIZE];
    bool retval = false;
    int n_found = 0;
    int i;
    // prepare element list
    rbus_de = CALLOC(sizeof(*rbus_de), n_methods);
    for (i=0; i<n_methods; i++) {
        in = &methods[i];
        osbus_rbus_method_name_fmt(handle, in->name, method_name, sizeof(method_name));
        if ((mh = _osbus_method_handler_find_name(handle, method_name)) != NULL) {
            _osbus_rbus_DataElement_from_method(handle, mh, n_found, rbus_de);
            n_found++;
        }
    }
    // unregister found methods
    rbusError_t rc = rbus_unregDataElements(rbusHandle, n_found, rbus_de);
    if (rc != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rc, "rbus_unregDataElements", handle->component_path);
        goto out;
    }
    retval = true;
out:
    _osbus_method_handler_remove_from_table(handle, methods, n_methods);
    free(rbus_de);
    return retval;
}

void osbus_rbus_MethodAsyncRespHandler(
        rbusHandle_t rbusHandle,
        char const* methodName,
        rbusError_t error,
        rbusObject_t params)
{
    osbus_msg_t *reply = NULL;
    osbus_async_invoke_t *async = NULL;
    char dbg_str[OSBUS_DBG_STR_SIZE];

    _osbus_rbus_lock_main();
    osbus_handle_t h = _osbus_handle_find_bus_ctx(rbusHandle);
    async = _osbus_async_invoke_find_name(h, (char*)methodName);
    if (!async) {
        LOGE("%s method '%s' not found", __func__, methodName);
        goto out;
    }
    osbus_rbus_async_invoke_t *rbus_async = _OSBUS_LIST_CAST(osbus_rbus_async_invoke_t, async);
    if (rbus_async->cancelled) {
        // cancelled with osbus_rbus_async_invoke_cancel
        // ignore reply - don't call the reply_handler
        LOGT("%s method '%s' cancelled", __func__, methodName);
        goto out;
    }

    if (params) {
        if (!osbus_msg_from_rbus_object(&reply, params)) {
             if (error == RBUS_ERROR_SUCCESS) {
                 error = RBUS_ERROR_INVALID_INPUT;
             }
        }
    }
    char *short_method_name = osbus_rbus_element_from_path(h, methodName);
    osbus_msg_to_dbg_str_fixed(reply, dbg_str, sizeof(dbg_str));
    LOGT("asyncResp %s method: %s e: %d msg: %s", h->component_path, short_method_name, error, dbg_str);
    if (async->reply_handler_fn) {
        osbus_rbus_error_set_and_log(h, error, "reply_handler", (char*)methodName);
        bool status = (error == RBUS_ERROR_SUCCESS);
        bool ret = async->reply_handler_fn(async->handle, short_method_name, status, reply, async->user_data);
        if (!ret) {
            LOGE("%s(%s) error", __func__, methodName);
        }
    }
out:
    _osbus_async_invoke_delete(h, async);
    osbus_msg_free(reply);
    osbus_error_clear(h);
    _osbus_rbus_unlock_main();
}

bool osbus_rbus_method_invoke_2(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *message,
        bool use_async,
        osbus_msg_t **reply,
        osbus_method_invoke_async_handler_fn_t *reply_handler,
        void *user_data,
        osbus_async_invoke_t **async)
{
    if (!handle) return false;
    rbusHandle_t rbusHandle = handle->bus_handle;
    if (!rbusHandle) return false;
    rbusObject_t robj = NULL;
    rbusObject_t reply_obj = NULL;
    char dbg_str[OSBUS_DBG_STR_SIZE];
    int ret = 0;
    bool retval = false;

    if (reply) *reply = NULL;
    char *path_str = osbus_rbus_path_fmta(handle, path);

    osbus_msg_to_dbg_str_fixed(message, dbg_str, sizeof(dbg_str));
    LOGT("%s %s %s", __func__, path_str, dbg_str);

    if (message) {
        osbus_msg_to_rbus_object(message, &robj);
    } else {
        /*
         * sending a NULL robj is the equivalent of 'rbuscli method_noargs'
         * however it produces some warnings in the logs:
         * RBUS<3> rbuscore_message.c:217 [3044] rbusMessage_GetInt32 unexpected date type 5
         * RBUS<3> rbuscore_message.c:217 [3044] rbusMessage_GetInt32 unexpected date type 5
         * RBUS<3> rbuscore_message.c:192 [3044] rbusMessage_GetString failed to unpack next item
         * so we'll send an empty object instead:
         */
        rbusObject_Init(&robj, NULL);
    }

    if (!use_async) {
        ret = rbusMethod_Invoke(rbusHandle, path_str, robj, &reply_obj);
        LOGT("%s:%d invoke '%s' ret=%d", __func__, __LINE__, path_str, ret);
        if (ret != RBUS_ERROR_SUCCESS) {
            osbus_rbus_error_set_and_log(handle, ret, "rbusMethod_Invoke", path_str);
            goto out;
        }
        if (reply_obj && reply) {
            retval = osbus_msg_from_rbus_object(reply, reply_obj);
        } else {
            retval = true;
        }
    } else {
        // prepare async handle
        osbus_async_invoke_t *osbus_async = _osbus_async_invoke_new(handle, path_str, reply_handler);
        /** rbusError_t rbusMethod_InvokeAsync(
         *          rbusHandle_t handle,
         *          char* methodName,
         *          rbusObject_t inParams,
         *          rbusMethodAsyncRespHandler_t callback,
         *          int timeout)
         */
        ret = rbusMethod_InvokeAsync(rbusHandle, path_str, robj, osbus_rbus_MethodAsyncRespHandler, 0);
        if (ret != RBUS_ERROR_SUCCESS) {
            _osbus_async_invoke_delete(handle, osbus_async);
            osbus_rbus_error_set_and_log(handle, ret, "rbusMethod_InvokeAsync", path_str);
            goto out;
        }
        osbus_async->user_data = user_data;
        *async = osbus_async;
        retval = true;
    }

out:
    rbusObject_Release(robj);
    rbusObject_Release(reply_obj);
    return retval;
}

bool osbus_rbus_method_invoke(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *message,
        osbus_msg_t **reply)
{
    return osbus_rbus_method_invoke_2(handle, path, message, false, reply, NULL, NULL, NULL);
}

bool osbus_rbus_method_invoke_async(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *message,
        osbus_method_invoke_async_handler_fn_t *reply_handler,
        void *user_data,
        osbus_async_invoke_t **async)
{
    return osbus_rbus_method_invoke_2(handle, path, message, true, NULL, reply_handler, user_data, async);
}

bool osbus_rbus_async_invoke_wait(osbus_handle_t handle, osbus_async_invoke_t *async, int timeout_ms)
{
    // timeout > 0  - timeout in ms
    // timeout = 0  - no timeout - wait forever
    // timeout = -1 - default timeout
    if (timeout_ms == -1) timeout_ms = OSBUS_DEFAULT_TIMEOUT_ASYNC;
    uint64_t t1 = clock_mono_ms();
    int remain_ms = timeout_ms;
    do {
        if (!_osbus_rbus_cond_main_wait_lock_unlock(remain_ms)) {
            // timeout or other error
            break;
        }
        if (!_osbus_list_find_node(&handle->async_invoke_list, &async->node)) {
            // reply was received and removed from list
            return true;
        }
        remain_ms = timeout_ms - (clock_mono_ms() - t1);
    } while (timeout_ms == 0 || remain_ms > 0);
    osbus_rbus_error_set_and_log(handle, RBUS_ERROR_TIMEOUT, __func__, NULL);
    return false;
}

bool osbus_rbus_async_invoke_cancel(osbus_handle_t handle, osbus_async_invoke_t *async)
{
    // rbus doesn't provide a cancellation method, so we'll just
    // mark the async handle as cancelled and ignore the reply
    // when it eventually triggers
    osbus_rbus_async_invoke_t *rbus_async = _OSBUS_LIST_CAST(osbus_rbus_async_invoke_t, async);
    rbus_async->cancelled = true;
    return true;
}

// event

rbusError_t osbus_rbus_event_sub_handler(
        rbusHandle_t handle,
        rbusEventSubAction_t action,
        const char* eventName,
        rbusFilter_t filter,
        int32_t interval,
        bool* autoPublish)
{
    char *action_str = action == RBUS_EVENT_ACTION_SUBSCRIBE ? "subscribed" : "unsubscribed";
    LOGT("rbus: event '%s' %s", eventName, action_str);
    return RBUS_ERROR_SUCCESS;
}

void _osbus_rbus_event_receive(
        rbusHandle_t                rbus_handle,
        rbusEvent_t const*          rbus_event,
        rbusEventSubscription_t*    subscription)
{
    if (!rbus_handle || !rbus_event || !subscription) {
        LOGE("%s", __func__);
        return;
    }
    bool ret = false;
    osbus_msg_t *msg = NULL;
    char *type_str = "UNK";
    char *event_name = (char*)rbus_event->name;
    osbus_event_handler_t *info = subscription->userData;
    if (!info) goto out;
    osbus_handle_t handle = info->handle;
    if (!handle) goto out;
    if (!info->event_handler_fn) goto out;
    if (!osbus_msg_from_rbus_object(&msg, rbus_event->data)) goto out;
    switch(rbus_event->type) {
        case RBUS_EVENT_OBJECT_CREATED: type_str = "OBJECT_CREATED"; break;
        case RBUS_EVENT_OBJECT_DELETED: type_str = "OBJECT_DELETED"; break;
        case RBUS_EVENT_VALUE_CHANGED:  type_str = "VALUE_CHANGED"; break;
        case RBUS_EVENT_GENERAL:        type_str = "GENERAL"; break;
        //case RBUS_EVENT_INITIAL_VALUE:  type_str = "INITIAL_VALUE"; break;
        //case RBUS_EVENT_INTERVAL:       type_str = "INTERVAL"; break;
        default: type_str = "UNK"; break;
    }
    ret = info->event_handler_fn(handle, event_name, type_str, msg, info->user_data);
out:
    if (!ret) LOGW("%s(%s %s)", __func__, event_name, type_str);
    osbus_msg_free(msg);
}


bool osbus_rbus_event_subscribe(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_event_handler_fn_t *event_handler_fn,
        void *user_data)
{
    LOGT("%s(%p, %s)", __func__, handle, osbus_path_dbg_fmta(path));
    if (!handle || !event_handler_fn) return false;
    bool ret = false;
    char *path_str = osbus_rbus_path_fmta(handle, path);
    rbusHandle_t rbus_handle = handle->bus_handle;
    osbus_event_handler_t *info = NULL;
    rbusError_t rc;
    int timeout = 1;

    if (!path_str) goto out;
    info = _osbus_event_handler_new(handle, path_str, event_handler_fn);
    info->user_data = user_data;
    rc = rbusEvent_Subscribe(rbus_handle, path_str, _osbus_rbus_event_receive, info, timeout);
    if (rc != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rc, "rbusEvent_Subscribe", path_str);
        goto out;
    }
    ret = true;
out:
    return ret;
}

bool osbus_rbus_event_register(
        osbus_handle_t handle,
        osbus_path_t path)
{
    if (!handle) return false;
    bool ret = false;
    char *path_str = osbus_rbus_path_fmta(handle, path);
    rbusHandle_t rbus_handle = handle->bus_handle;
    rbusError_t rc;

    LOGT("%s(%p, %s)", __func__, handle, path_str);
    if (!path_str) goto out;
    rbusDataElement_t rbus_elem = {0};
    rbus_elem.type = RBUS_ELEMENT_TYPE_EVENT;
    rbus_elem.name = path_str;
    rbus_elem.cbTable.eventSubHandler = osbus_rbus_event_sub_handler;
    rc = rbus_regDataElements(rbus_handle, 1, &rbus_elem);
    if (rc != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rc, "rbus_regDataElements", path_str);
        goto out;
    }
    ret = true;
out:
    return ret;
}

bool osbus_rbus_event_publish(
        osbus_handle_t handle,
        osbus_path_t path,
        const char *event_name,
        const osbus_msg_t *msg)
{
    if (!handle || !msg) return false;
    bool ret = false;
    char *path_str = osbus_rbus_path_fmta(handle, path);
    rbusError_t rc;
    rbusHandle_t rbus_handle = handle->bus_handle;
    rbusObject_t rbus_obj = NULL;
    rbusEvent_t rbus_event = {0};
    LOGT("%s(%p, %s, %s)", __func__, handle, path_str, event_name);

    if (!path_str) goto out;
    if (!osbus_msg_to_rbus_object(msg, &rbus_obj)) {
        LOGE("%s(%s) msg", __func__, path_str);
        goto out;
    }
    rbus_event.name = path_str;
    rbus_event.type = RBUS_EVENT_GENERAL;
    rbus_event.data = rbus_obj;
    rc = rbusEvent_Publish(rbus_handle, &rbus_event);
    if (rc == RBUS_ERROR_NOSUBSCRIBERS) {
        // ignore no subscribers but log
        LOGT("rbusEvent_Publish(%s): no subscribers", path_str);
    } else if (rc != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rc, "rbusEvent_Publish", path_str);
        goto out;
    }
    ret = true;
out:
    rbusObject_Release(rbus_obj);
    return ret;
}

// topic

void _osbus_rbus_topic_receive_message(
        rbusHandle_t rbus_handle,
        rbusMessage_t *rbus_msg,
        void *rbus_user_data)
{
    bool ret = false;
    if (!rbus_msg || !rbus_user_data) return;
    osbus_topic_handler_t *tinfo = rbus_user_data;
    osbus_handle_t handle = tinfo->handle;
    osbus_msg_t *d = NULL;

    LOGT("received topic '%s': %d\n", rbus_msg->topic, rbus_msg->length);
    if (!(d = osbus_msg_from_json_string_buf((char*)rbus_msg->data, rbus_msg->length))) goto out;
    _osbus_rbus_lock_main();
    ret = tinfo->topic_handler_fn(handle, (char*)rbus_msg->topic, d, tinfo->user_data);
    _osbus_rbus_unlock_main();
out:
    if (!ret) LOGW("%s(%s)", __func__, rbus_msg->topic);
    osbus_msg_free(d);
    return;
}

bool osbus_rbus_topic_listen(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_topic_handler_fn_t *topic_handler_fn,
        void *user_data)
{
    if (!handle || !topic_handler_fn) return false;
    bool ret = false;
    int rc;
    rbusHandle_t rbus_handle = handle->bus_handle;
    osbus_topic_handler_t *tinfo = NULL;
    char *path_str = osbus_rbus_path_fmta(handle, path);

    LOGT("%s(%p, %s)", __func__, handle, path_str);
    if (!path_str) goto out;
    tinfo = _osbus_topic_handler_new(handle, path_str, topic_handler_fn);
    tinfo->user_data = user_data;
    rc = rbusMessage_AddListener(
            rbus_handle,
            path_str,
            _osbus_rbus_topic_receive_message,
            tinfo);
    if (rc != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rc, "rbusMessage_AddListener", path_str);
        goto out;
    }
    ret = true;
out:
    if (!ret) {
        if (tinfo) _osbus_topic_handler_delete(handle, tinfo);
    } else {
        LOGD("%s(%s)", __func__, path_str);
    }
    return ret;
}

bool osbus_rbus_topic_unlisten(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_topic_handler_fn_t *topic_handler_fn)
{
    if (!handle || !topic_handler_fn) return false;
    bool ret = false;
    int rc;
    rbusHandle_t rbus_handle = handle->bus_handle;
    osbus_topic_handler_t *tinfo = NULL;
    char *path_str = osbus_rbus_path_fmta(handle, path);

    LOGT("%s(%p, %s)", __func__, handle, path_str);
    if (!path_str) goto out;
    tinfo = _osbus_topic_handler_find_name(handle, path_str);
    if (!tinfo) {
        LOGE("%s: '%s' topic not found", __func__, path_str);
        goto out;
    }
    if (topic_handler_fn != tinfo->topic_handler_fn) {
        LOGE("%s: %s fn mismatch %p %p", __func__, path_str, topic_handler_fn, tinfo->topic_handler_fn);
        goto out;
    }
    rc = rbusMessage_RemoveListener(
            rbus_handle,
            path_str);
    if (rc != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rc, "rbusMessage_RemoveListener", path_str);
        goto out;
    }
    _osbus_topic_handler_delete(handle, tinfo);
    ret = true;
out:
    LOGD("%s(%s): %d", __func__, path_str, ret);
    return ret;
}

bool osbus_rbus_topic_send(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *msg)
{
    if (!handle || !msg) return false;
    bool ret = false;
    char *msg_str = NULL;
    int rc;
    rbusHandle_t rbus_handle = handle->bus_handle;
    rbusMessage_t rbus_msg = {0};
    char *path_str = osbus_rbus_path_fmta(handle, path);

    LOGT("%s(%p, %s)", __func__, handle, path_str);
    if (!path_str) goto out;
    if (!(msg_str = osbus_msg_to_json_string(msg))) goto out;

    rbus_msg.topic = path_str;
    rbus_msg.length = strlen(msg_str);
    rbus_msg.data = (uint8_t*)msg_str;
    // rbusMessageSendOptions_t:
    // - RBUS_MESSAGE_NONE: no confirmation of delivery
    // - RBUS_MESSAGE_CONFIRM_RECEIPT: wait confirmation of delivery from listener
    rc = rbusMessage_Send(rbus_handle, &rbus_msg, RBUS_MESSAGE_NONE);
    if (rc != RBUS_ERROR_SUCCESS) {
        osbus_rbus_error_set_and_log(handle, rc, "rbusMessage_Send", path_str);
        goto out;
    }
    ret = true;
out:
    if (!ret) {
        LOGE("%s(%s)", __func__, path_str);
    } else {
        LOGD("%s(%s)", __func__, path_str);
    }
    free(msg_str);
    return ret;
}

// data model

/*
rbus_regDataElements()
rbus_unregDataElements()
rbus_get()
rbus_set()
rbusTable_addRow()
rbusTable_removeRow()
RbusTable_getRowNames()
rbusElementInfo_get()
*/


bool osbus_rbus_dm_register(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_msg_type type,
        //osbus_dm_handler handler
        void* handler)
{
    if (!handle) return false;
    bool ret = false;
    char path_str[OSBUS_NAME_SIZE];
    rbusHandle_t rbus_handle = handle->bus_handle;
    rbusDataElement_t rbus_elem = {0};
    osbus_path_fmt(handle, path, path_str, sizeof(path_str));
    //rbus_elem.type = RBUS_ELEMENT_TYPE_TABLE;
    rbus_elem.type = RBUS_ELEMENT_TYPE_PROPERTY;
    rbus_elem.name = path_str;
    rbus_elem.cbTable.eventSubHandler = osbus_rbus_event_sub_handler;
    //rbus_elem.cbTable.getHandler = ;
    //rbus_elem.cbTable.setHandler = ;
    //rbus_elem.cbTable.tableAddRowHandler = ;
    //rbus_elem.cbTable.tableRemoveRowHandler =;
    rbusError_t rc = rbus_regDataElements(rbus_handle, 1, &rbus_elem);
    if (rc != RBUS_ERROR_SUCCESS) {
        LOGE("rbus_regDataElements(%s): %d", path_str, rc);
        goto out;
    }
    ret = true;
out:
    return ret;
}

bool osbus_rbus_dm_get(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_msg_t **value)
{
    if (!handle) return false;
    bool ret = false;
    char path_str[OSBUS_NAME_SIZE];
    rbusHandle_t rbus_handle = handle->bus_handle;
    rbusValue_t rbus_val = NULL;
    osbus_path_fmt(handle, path, path_str, sizeof(path_str));
    rbusError_t rc = rbus_get(rbus_handle, path_str, &rbus_val);
    if (rc != RBUS_ERROR_SUCCESS) {
        LOGE("rbus_set(%s): %d", path_str, rc);
        goto out;
    }
    ret = osbus_msg_from_rbus_value(value, rbus_val);
out:
    if (rbus_val) rbusValue_Release(rbus_val);
    return ret;
}

bool osbus_rbus_dm_set(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *value)
{
    if (!handle) return false;
    bool ret = false;
    char path_str[OSBUS_NAME_SIZE];
    rbusHandle_t rbus_handle = handle->bus_handle;
    rbusValue_t rbus_val = NULL;
    char dbg_str[OSBUS_DBG_STR_SIZE];

    osbus_path_fmt(handle, path, path_str, sizeof(path_str));
    osbus_msg_to_dbg_str_fixed(value, dbg_str, sizeof(dbg_str));
    LOGT("%s(%s, %s)", __func__, path_str, dbg_str);
    if (!osbus_msg_to_rbus_value(value, &rbus_val)) {
        LOGE("%s %s %s", "data_to_rbus", path_str, dbg_str);
        goto out;
    }
    rbusError_t rc = rbus_set(rbus_handle, path_str, rbus_val, NULL);
    if (rc != RBUS_ERROR_SUCCESS) {
        LOGE("rbus_set(%s, %s): %d", path_str, dbg_str, rc);
        goto out;
    }
    ret = true;
out:
    if (rbus_val) rbusValue_Release(rbus_val);
    return ret;
}


struct osbus_ops osbus_rbus_ops = {
    .op_osbus_path_fmt              = osbus_rbus_path_fmt,
    .op_osbus_method_name_fmt       = osbus_rbus_method_name_fmt,
    .op_osbus_connect_bus           = osbus_rbus_connect,
    .op_osbus_disconnect_bus        = osbus_rbus_disconnect,
    .op_osbus_list_msg              = osbus_rbus_list,
    .op_osbus_method_register       = osbus_rbus_method_register,
    .op_osbus_method_unregister     = osbus_rbus_method_unregister,
    .op_osbus_method_reply_async    = osbus_rbus_method_reply_async,
    .op_osbus_method_invoke         = osbus_rbus_method_invoke,
    .op_osbus_method_invoke_async   = osbus_rbus_method_invoke_async,
    .op_osbus_method_invoke_async_wait   = osbus_rbus_async_invoke_wait,
    .op_osbus_method_invoke_async_cancel = osbus_rbus_async_invoke_cancel,
    .op_osbus_event_register        = osbus_rbus_event_register,
    .op_osbus_event_subscribe       = osbus_rbus_event_subscribe,
    .op_osbus_event_publish         = osbus_rbus_event_publish,
    .op_osbus_topic_listen          = osbus_rbus_topic_listen,
    .op_osbus_topic_unlisten        = osbus_rbus_topic_unlisten,
    .op_osbus_topic_send            = osbus_rbus_topic_send,
    .op_osbus_dm_get                = osbus_rbus_dm_get,
    .op_osbus_dm_set                = osbus_rbus_dm_set,
};

struct osbus_ops *osbus_rbus_get_ops(void)
{
    return &osbus_rbus_ops;
}


