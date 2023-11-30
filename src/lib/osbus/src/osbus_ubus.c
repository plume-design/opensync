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

#include "log.h"
#include "os.h"
#include "util.h"
#include "memutil.h"

#include "osbus.h"
#include "osbus_priv.h"
#include "osbus_ubus.h"
#include "osbus_msg_ubus.h"

#include <libubus.h>
#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>

#define MODULE_ID LOG_MODULE_ID_OSBUS

// shared global context, single loop
static struct ubus_context *_g_ub_ctx = NULL;
static int                  _g_ub_ref_count = 0;
static struct ev_io         _g_ub_ev_io;
static struct ev_loop      *_g_ub_loop;

typedef struct osbus_ubus_async_invoke
{
    osbus_async_invoke_t osbus_async_invoke;
    struct ubus_request ubus_req;
} osbus_ubus_async_invoke_t;

typedef struct osbus_ubus_async_reply
{
    osbus_async_reply_t osbus_async_reply;
    struct ubus_request_data ubus_req_data;
} osbus_ubus_async_reply_t;

typedef struct osbus_ubus_event_handler
{
    osbus_event_handler_t osbus_event;
    osbus_handle_t handle;
    struct ubus_subscriber ubus_sub;
} osbus_ubus_event_handler_t;

typedef struct osbus_ubus_topic_handler
{
    osbus_topic_handler_t osbus_topic;
    struct ubus_event_handler ubus_ev;
} osbus_ubus_topic_handler_t;

typedef struct osbus_ubus_object_container
{
    osbus_handle_t handle;
    struct ubus_object u_obj;
} osbus_ubus_object_container_t;

// ubus specific osbus_handle->bus_data
typedef struct osbus_ubus_handle_data
{
    osbus_ubus_object_container_t *obj_container;
} osbus_ubus_handle_data_t;


static void _osbus_ubus_object_container_free(osbus_ubus_object_container_t **obj_container);
static bool _osbus_ubus_object_recreate(osbus_handle_t handle);


osbus_error_t osbus_error_from_ubus(enum ubus_msg_status error)
{
    switch (error) {
        case UBUS_STATUS_OK:                return OSBUS_ERROR_SUCCESS;
        case UBUS_STATUS_INVALID_COMMAND:   return OSBUS_ERROR_INVALID_OPERATION;
        case UBUS_STATUS_INVALID_ARGUMENT:  return OSBUS_ERROR_INVALID_ARGUMENT;
        case UBUS_STATUS_METHOD_NOT_FOUND:  return OSBUS_ERROR_INVALID_METHOD;
        case UBUS_STATUS_NOT_FOUND:         return OSBUS_ERROR_NOT_FOUND;
        case UBUS_STATUS_NO_DATA:           return OSBUS_ERROR_NOT_FOUND;
        case UBUS_STATUS_PERMISSION_DENIED: return OSBUS_ERROR_PERMISSION_DENIED;
        case UBUS_STATUS_TIMEOUT:           return OSBUS_ERROR_TIMEOUT;
        case UBUS_STATUS_NOT_SUPPORTED:     return OSBUS_ERROR_INVALID_OPERATION;
        case UBUS_STATUS_UNKNOWN_ERROR:     return OSBUS_ERROR_GENERAL;
        case UBUS_STATUS_CONNECTION_FAILED: return OSBUS_ERROR_CONNECTION;
#ifdef UBUS_HAS_STATUS_SYSTEM_ERROR
        // older ubus versions don't have some status enum values
        case UBUS_STATUS_NO_MEMORY:         return OSBUS_ERROR_NO_RESOURCES;
        case UBUS_STATUS_PARSE_ERROR:       return OSBUS_ERROR_GENERAL;
        case UBUS_STATUS_SYSTEM_ERROR:      return OSBUS_ERROR_GENERAL;
#endif
        default:                            return OSBUS_ERROR_GENERAL;
    }
}

enum ubus_msg_status osbus_error_to_ubus(osbus_error_t error)
{
    switch (error) {
        case OSBUS_ERROR_SUCCESS:           return UBUS_STATUS_OK;
        case OSBUS_ERROR_GENERAL:           return UBUS_STATUS_UNKNOWN_ERROR;
        case OSBUS_ERROR_CONNECTION:        return UBUS_STATUS_CONNECTION_FAILED;
#ifdef UBUS_HAS_STATUS_SYSTEM_ERROR
        case OSBUS_ERROR_NO_RESOURCES:      return UBUS_STATUS_NO_MEMORY;
#else
        case OSBUS_ERROR_NO_RESOURCES:      return UBUS_STATUS_UNKNOWN_ERROR;
#endif
        case OSBUS_ERROR_INVALID_ARGUMENT:  return UBUS_STATUS_INVALID_ARGUMENT;
        case OSBUS_ERROR_INVALID_OPERATION: return UBUS_STATUS_INVALID_COMMAND;
        case OSBUS_ERROR_INVALID_METHOD:    return UBUS_STATUS_METHOD_NOT_FOUND;
        case OSBUS_ERROR_INVALID_RESPONSE:  return UBUS_STATUS_UNKNOWN_ERROR;
        case OSBUS_ERROR_EXISTS:            return UBUS_STATUS_UNKNOWN_ERROR;
        case OSBUS_ERROR_NOT_FOUND:         return UBUS_STATUS_NOT_FOUND;
        case OSBUS_ERROR_PERMISSION_DENIED: return UBUS_STATUS_PERMISSION_DENIED;
        case OSBUS_ERROR_TIMEOUT:           return UBUS_STATUS_TIMEOUT;
    }
    return UBUS_STATUS_UNKNOWN_ERROR;
}

void osbus_ubus_error_set_and_log(osbus_handle_t handle, enum ubus_msg_status ubus_error,
        const char *func, const char *param)
{
    osbus_error_set_and_log(handle, osbus_error_from_ubus(ubus_error),
            ubus_strerror(ubus_error), func, param);
}

void osbus_ubus_error_set_and_logERR(osbus_handle_t handle, enum ubus_msg_status ubus_error,
        const char *func, const char *param)
{
    osbus_error_set_and_log_lvl(handle, osbus_error_from_ubus(ubus_error),
            LOG_SEVERITY_ERROR, ubus_strerror(ubus_error), func, param);
}


bool osbus_ubus_path_fmt(osbus_handle_t handle, osbus_path_t path, char *str, int size)
{
    return osbus_path_cmn_fmt(handle, path, str, size);
}

char* _osbus_ubus_path_fmt(osbus_handle_t handle, osbus_path_t path, char *str, int size)
{
    if (osbus_ubus_path_fmt(handle, path, str, size)) {
        return str;
    }
    return NULL;
}

#define osbus_ubus_path_fmta(H, P) _osbus_ubus_path_fmt(H, P, alloca(OSBUS_NAME_SIZE), OSBUS_NAME_SIZE)

void osbus_ubus_handle_disconnect(struct ubus_context *ctx)
{
    // abort so that we get restarted by dm
    // and then we can reconnect to the bus
    bool connected = false;
    ASSERT(connected, "ubus disconnected");
}

void _osbus_ubus_ev_read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
    struct ubus_context *ctx = watcher->data;
    if (ctx) {
        LOGT("%s %p %p", __func__, watcher, ctx);
        ubus_handle_event(ctx);
        if (ctx->sock.eof) {
            osbus_ubus_handle_disconnect(ctx);
        }
    } else {
        LOGE("%s %p", __func__, watcher);
    }
}

bool _osbus_ubus_attach_loop(struct ev_loop *loop)
{
    struct ubus_context *ctx = _g_ub_ctx;
    struct ev_io *ev = &_g_ub_ev_io;
    int fd = ctx->sock.fd;
    _g_ub_loop = loop;
    LOGT("%s %p %p %p %d", __func__, ctx, loop, ev, fd);
    ev_io_init(ev, _osbus_ubus_ev_read_cb, fd, EV_READ);
    ev->data = ctx;
    ev_io_start(loop, ev);
    return true;
}

bool _osbus_ubus_detach_loop()
{
    struct ev_loop *loop = _g_ub_loop;
    struct ev_io *ev = &_g_ub_ev_io;
    LOGT("%s %p", __func__, loop);
    if (ev_is_active(ev)) {
        ev_io_stop(loop, ev);
    }
    _g_ub_loop = NULL;
    return true;
}

bool osbus_ubus_connect(osbus_handle_t handle)
{
    bool retval = false;
    if (!handle) return false;
    if (_g_ub_ctx) {
        // check that loop matches
        if (handle->loop != _g_ub_loop) {
            LOGE("multiple loop not implemented %p %p", handle->loop, _g_ub_loop);
            return false;
        }
    } else {
        // ubus_connect calls uloop_init which overrides some signal handlers
        // as we're not using uloop, revert default signal handlers
        struct sigaction sigint_action;
        struct sigaction sigterm_action;
        struct sigaction sigchld_action;

        sigaction(SIGINT, NULL, &sigint_action);
        sigaction(SIGTERM, NULL, &sigterm_action);
        sigaction(SIGCHLD, NULL, &sigchld_action);

        _g_ub_ctx = ubus_connect(NULL);
        LOGD("ubus_connect: %p", _g_ub_ctx);

        sigaction(SIGINT, &sigint_action, NULL);
        sigaction(SIGTERM, &sigterm_action, NULL);
        sigaction(SIGCHLD, &sigchld_action, NULL);

        if (!_g_ub_ctx) goto out;

        _g_ub_ctx->connection_lost = osbus_ubus_handle_disconnect;

        _osbus_ubus_attach_loop(handle->loop);
    }
    _g_ub_ref_count++;
    handle->bus_handle = _g_ub_ctx;
    handle->async_invoke_list.node_size = sizeof(osbus_ubus_async_invoke_t);
    handle->async_reply_list.node_size = sizeof(osbus_ubus_async_reply_t);
    handle->event_handler_list.node_size = sizeof(osbus_ubus_event_handler_t);
    handle->topic_handler_list.node_size = sizeof(osbus_ubus_topic_handler_t);
    handle->bus_data = CALLOC(sizeof(osbus_ubus_handle_data_t), 1);
    retval = _osbus_ubus_object_recreate(handle);
out:
    LOGT("%s %s %p ubus_ctx: %p", __func__, osbus_handle_get_name(handle), handle, _g_ub_ctx);
    return retval;
}

bool osbus_ubus_disconnect(osbus_handle_t handle)
{
    LOGT("%s: %p %p", __func__, handle, _g_ub_ctx);

    if (!handle) return false;

    osbus_ubus_handle_data_t *bus_data = handle->bus_data;
    if (bus_data) {
        if (bus_data->obj_container) {
            struct ubus_object *u_obj = &bus_data->obj_container->u_obj;
            LOGT("ubus_remove_object(%p %p)", _g_ub_ctx, u_obj);
            ubus_remove_object(_g_ub_ctx, u_obj);
            _osbus_ubus_object_container_free(&bus_data->obj_container);
        }
        free(handle->bus_data);
        handle->bus_data = NULL;
    }

    if (_g_ub_ref_count > 0) {
        _g_ub_ref_count--;
        if (_g_ub_ref_count == 0 && _g_ub_ctx) {
            LOGD("%s ubus_free(%p)", __func__, _g_ub_ctx);
            // ubus_free() calls ubus_shutdown() and releases the handle
            ubus_free(_g_ub_ctx);
            _g_ub_ctx = NULL;
            _osbus_ubus_detach_loop();
        }
    }
    return true;
}


// list


struct osbus_ubus_list_req
{
    bool include_elements;
    osbus_msg_t *list;
};

static void _osbus_ubus_receive_list_result_cb(struct ubus_context *ctx, struct ubus_object_data *obj, void *priv)
{
    struct osbus_ubus_list_req *req = priv;
    osbus_msg_t *list = req->list;
    osbus_msg_t *item = osbus_msg_add_item_object(list);
    osbus_msg_t *prop = osbus_msg_set_prop_object(item, (char*)obj->path);
    osbus_msg_set_prop_string(prop, "type", "object");
    if (req->include_elements) {
        osbus_msg_t *elements = osbus_msg_set_prop_object(prop, "elements");
        struct blob_attr *pos = NULL;
        size_t rem = 0;
        blob_for_each_attr(pos, obj->signature, rem) {
            osbus_msg_t *m = osbus_msg_set_prop_object(elements, (char*)blobmsg_name(pos));
            osbus_msg_set_prop_string(m, "type", "method");
            osbus_msg_t *args = osbus_msg_new_object();
            struct blob_attr *pos2 = NULL;
            size_t rem2 = 0;
            rem2 = blobmsg_data_len(pos);
            __blob_for_each_attr(pos2, blobmsg_data(pos), rem2) {
                enum blobmsg_type btype = blobmsg_get_u32(pos2);
                char *type_str = osbus_msg_type_str(osbus_msg_type_from_blobmsg_type(btype));
                osbus_msg_set_prop_string(args, (char*)blobmsg_name(pos2), type_str);
            }
            osbus_msg_set_prop(m, "args", args);
        }
    }
}

bool osbus_ubus_list(
        osbus_handle_t handle,
        osbus_path_t path,
        bool include_elements,
        osbus_msg_t **list)
{
    struct ubus_context *ctx = handle->bus_handle;
    *list = osbus_msg_new_array();
    struct osbus_ubus_list_req req = {.list = *list, .include_elements = include_elements};
    char *path_str = osbus_ubus_path_fmta(handle, path);

    if (!path_str) return false;
    int ret = ubus_lookup(ctx, path_str, _osbus_ubus_receive_list_result_cb, &req);
    if (ret == UBUS_STATUS_NO_DATA) return true;
    if (ret != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, ret, "ubus_lookup", path_str);
        return false;
    }
    return true;
}


// method


static bool _osbus_ubus_send_reply(
        osbus_handle_t handle,
        const char *method_name,
        const osbus_msg_t *reply,
        struct ubus_request_data *req,
        enum ubus_msg_status *ret_ubus_status)
{
    struct ubus_context *ctx = handle->bus_handle;
    struct blob_buf *bb = NULL;
    enum ubus_msg_status ubus_status = UBUS_STATUS_UNKNOWN_ERROR;
    char dbg_str[OSBUS_DBG_STR_SIZE];
    bool retval = false;

    if (!osbus_msg_to_blob_buf(reply, &bb)) {
        ubus_status = UBUS_STATUS_INVALID_ARGUMENT;
        osbus_ubus_error_set_and_log(handle, ubus_status,
                "osbus_msg_to_blob_buf", strfmta("%s, %s", handle->component_path, method_name));
        goto out;
    }
    osbus_msg_to_dbg_str_fixed(reply, dbg_str, sizeof(dbg_str));
    LOGT("%s(%s) sending reply %s", __func__, method_name, dbg_str);
    ubus_status = ubus_send_reply(ctx, req, bb->head);
    if (ubus_status != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, ubus_status,
                "ubus_send_reply", strfmta("%s, %s", handle->component_path, method_name));
        goto out;
    }
    retval = true;
out:
    if (bb) {
        blob_buf_free(bb);
        free(bb);
    }
    *ret_ubus_status = ubus_status;
    return retval;
}


int osbus_ubus_method_handler(
        struct ubus_context *ctx,
        struct ubus_object *obj,
        struct ubus_request_data *req,
        const char *method,
        struct blob_attr *msg)
{
    LOGT("%s(%s ctx:%p obj:%p %p)", __func__, method, ctx, obj, obj->path);
    enum ubus_msg_status ubus_status = UBUS_STATUS_UNKNOWN_ERROR;
    osbus_ubus_object_container_t *obj_container = container_of(obj, osbus_ubus_object_container_t, u_obj);
    osbus_handle_t handle = obj_container->handle;
    if (!_osbus_handle_is_valid(handle, __func__)) return ubus_status;
    osbus_method_handler_t *m = _osbus_method_handler_find_name(handle, method);
    osbus_msg_t *d = NULL;
    osbus_msg_t *reply = NULL;
    osbus_ubus_async_reply_t *ubus_reply_handle = NULL;
    osbus_async_reply_t *reply_handle = NULL;
    char dbg_str[OSBUS_DBG_STR_SIZE];
    bool defer_reply = false;
    bool ret;

    if (!m || !m->handler_fn) {
        LOGE("%s(%s) not found", __func__, method);
        return UBUS_STATUS_METHOD_NOT_FOUND;
    }
    if (msg) {
        // msg can be null if no arg provided
        if (!osbus_msg_from_blob_attr(&d, msg)) {
            LOGE("%s(%s) inv arg", __func__, method);
            return UBUS_STATUS_INVALID_ARGUMENT;
        }
    }
    osbus_msg_to_dbg_str_fixed(d, dbg_str, sizeof(dbg_str));
    LOGD("handle %s method: %s msg: %s", handle->component_path, method, dbg_str);
    LOGT("%s(%s) call %p", __func__, method, m->handler_fn);

    osbus_error_clear(handle);
    reply_handle = _osbus_async_reply_alloc(handle);
    ubus_reply_handle = _OSBUS_LIST_CAST(osbus_ubus_async_reply_t, reply_handle);
    ret = m->handler_fn(handle, (char*)method, d, &reply, &defer_reply, reply_handle);
    if (!ret) {
        ubus_status = osbus_error_to_ubus(osbus_error_get(handle));
        if (ubus_status == UBUS_STATUS_OK) ubus_status = UBUS_STATUS_UNKNOWN_ERROR;
    } else if (reply && defer_reply) {
        LOGT("%s(%s) error: both reply and defer set", __func__, method);
        ubus_status = UBUS_STATUS_INVALID_ARGUMENT;
        defer_reply = false;
        ret = false;
    }
    if (!ret) goto out;
    ubus_status = UBUS_STATUS_OK;
    if (reply) {
        _osbus_ubus_send_reply(handle, method, reply, req, &ubus_status);
    } else if (defer_reply) {
        LOGT("%s(%s) deferred reply", __func__, method);
        _osbus_async_reply_insert(handle, reply_handle, method);
        ubus_defer_request(ctx, req, &ubus_reply_handle->ubus_req_data);
    } else {
        LOGT("%s(%s) no reply", __func__, method);
    }
out:
    if (ubus_status != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_logERR(handle, ubus_status, __func__,
                strfmta("%s, %s", handle->component_path, method));
    }
    if (!defer_reply) _osbus_async_reply_free(reply_handle);
    osbus_error_clear(handle);
    osbus_msg_free(reply);
    return ubus_status;
}

bool osbus_ubus_method_reply_async(
        osbus_handle_t handle,
        osbus_async_reply_t *reply_handle,
        bool status,
        const osbus_msg_t *reply)
{
    LOGT("%s(%p, %p, %d)", __func__, handle, reply_handle, status);
    if (!handle) return false;
    struct ubus_context *ctx = handle->bus_handle;
    bool retval = true;
    enum ubus_msg_status ubus_status;
    osbus_ubus_async_reply_t *ubus_reply_handle = _OSBUS_LIST_CAST(osbus_ubus_async_reply_t, reply_handle);
    char *method_name = reply_handle->node.name;

    if (status) {
        ubus_status = UBUS_STATUS_OK;
    } else {
        ubus_status = osbus_error_to_ubus(osbus_error_get(handle));
        if (ubus_status == UBUS_STATUS_OK) ubus_status = UBUS_STATUS_UNKNOWN_ERROR;
    }
    if (status && reply) {
        retval = _osbus_ubus_send_reply(handle, method_name, reply,
                &ubus_reply_handle->ubus_req_data, &ubus_status);
    }
    //void ubus_complete_deferred_request(struct ubus_context *ctx,
    //                  struct ubus_request_data *req, int ret);
    ubus_complete_deferred_request(ctx, &ubus_reply_handle->ubus_req_data, ubus_status);
    _osbus_async_reply_delete(handle, reply_handle);
    return retval;
}


static void _osbus_ubus_object_methods_free(struct ubus_object *u_obj)
{
    const struct ubus_method *um;
    int i, j;

    for (i=0; i < u_obj->n_methods; i++) {
        um = &u_obj->methods[i];
        free((char*)um->name);
        for (j=0; j < um->n_policy; j++) {
            free((char*)um->policy[j].name);
        }
        free((struct blobmsg_policy*)um->policy);
    }
    free((struct ubus_method*)u_obj->methods);
    u_obj->methods = NULL;
    u_obj->n_methods = 0;
}

static void _osbus_ubus_object_container_free(osbus_ubus_object_container_t **obj_container)
{
    if (!*obj_container) return;
    struct ubus_object *u_obj = &(*obj_container)->u_obj;
    _osbus_ubus_object_methods_free(u_obj);
    free(u_obj->type);
    u_obj->type = NULL;
    free(*obj_container);
    *obj_container = NULL;
}

bool _osbus_ubus_object_container_from_method_handler_list(osbus_handle_t handle)
{
    osbus_ubus_handle_data_t *bus_data = handle->bus_data;
    if (bus_data->obj_container) {
        osbus_error_set_and_log(handle, OSBUS_ERROR_EXISTS, "obj_container", __func__, handle->component_path);
        return false;
    }
    osbus_ubus_object_container_t *obj_con = NULL;
    osbus_method_handler_t *mh = NULL;
    _osbus_node_t *nodep = NULL;
    struct ubus_method      *u_methods = NULL;
    struct ubus_object_type *u_objtype = NULL;
    struct ubus_object      *u_obj = NULL;
    struct blobmsg_policy   *u_policy = NULL;
    struct ubus_method      *um;
    char *errstr = NULL;
    int n_methods = _osbus_list_len(&handle->method_handler_list);
    int i, j;

    obj_con = CALLOC(sizeof(osbus_ubus_object_container_t), 1);
    obj_con->handle = handle;
    u_obj = &obj_con->u_obj;
    u_objtype = CALLOC(sizeof(*u_objtype), 1);
    u_methods = CALLOC(sizeof(*u_methods), n_methods);

    i = 0;
    _osbus_list_foreach(&handle->method_handler_list, nodep)
    {
        ASSERT(i < n_methods, "n_methods %d %d", i, n_methods);
        mh = (osbus_method_handler_t*)nodep;
        um = &u_methods[i];
        LOGT("register %s method: %s %d", handle->component_path, mh->method_name, mh->n_policy);
        if (!mh->method_name) { errstr = "name"; goto err; }
        um->name = STRDUP(mh->method_name);
        um->handler = osbus_ubus_method_handler;
        if (mh->n_policy) {
            u_policy = CALLOC(sizeof(*u_policy), mh->n_policy);
            for (j=0; j < mh->n_policy; j++) {
                if (!mh->policy[j].name) { errstr = "policy"; goto err; }
                u_policy[j].name = STRDUP(mh->policy[j].name);
                u_policy[j].type = osbus_msg_type_to_blobmsg_type(mh->policy[j].type);
            }
            um->policy = u_policy;
            um->n_policy = mh->n_policy;
        }
        i++;
    }
    u_objtype->name = handle->component_path;
    u_objtype->id = 0;
    u_objtype->methods = u_methods;
    u_objtype->n_methods = n_methods;

    u_obj->name = handle->component_path;
    u_obj->type = u_objtype;
    u_obj->methods = u_methods;
    u_obj->n_methods = n_methods;

    bus_data->obj_container = obj_con;
    return true;
err:
    osbus_ubus_error_set_and_log(handle, UBUS_STATUS_INVALID_ARGUMENT, __func__, errstr);
    return false;
}

static bool _osbus_ubus_object_recreate(osbus_handle_t handle)
{
    if (!handle) return false;
    LOGT("%s(%p, %s)", __func__, handle, handle->component_path);
    struct ubus_context *ctx = handle->bus_handle;
    osbus_ubus_handle_data_t *bus_data = handle->bus_data;
    struct ubus_object *u_obj = NULL;
    int ret = UBUS_STATUS_UNKNOWN_ERROR;

    if (bus_data->obj_container) {
        // if object already exist remove first
        u_obj = &bus_data->obj_container->u_obj;
        LOGT("ubus_remove_object(%p %p)", ctx, u_obj);
        ubus_remove_object(ctx, u_obj);
        _osbus_ubus_object_container_free(&bus_data->obj_container);
    }
    if (!_osbus_ubus_object_container_from_method_handler_list(handle)) {
        return false;
    }
    u_obj = &bus_data->obj_container->u_obj;
    ret = ubus_add_object(ctx, u_obj);
    LOGT("ubus_add_object(ctx:%p, obj:%p) ret=%d", ctx, u_obj, ret);
    if (ret != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, ret, "ubus_add_object", NULL);
        return false;
    }
    return true;
}

bool osbus_ubus_method_register(osbus_handle_t handle, const osbus_method_t *methods, int n_methods)
{
    LOGT("%s(%p, %p, %d)", __func__, handle, methods, n_methods);
    if (!handle || n_methods < 0) return false;

    if (!_osbus_method_table_validate(handle, methods, n_methods)) {
        return false;
    }
    if (!_osbus_method_handler_check_duplicate(handle, methods, n_methods)) {
        return false;
    }
    if (!_osbus_method_handler_add_from_table(handle, methods, n_methods, NULL, NULL)) {
        return false;
    }
    return _osbus_ubus_object_recreate(handle);
}

bool osbus_ubus_method_unregister(osbus_handle_t handle, const osbus_method_t *methods, int n_methods)
{
    LOGT("%s(%p, %p, %d)", __func__, handle, methods, n_methods);
    if (!handle || n_methods < 0) return false;

    if (!_osbus_method_handler_remove_from_table(handle, methods, n_methods)) {
        return false;
    }
    return _osbus_ubus_object_recreate(handle);
}


void osbus_ubus_receive_reply(struct ubus_request *req, int type, struct blob_attr *msg)
{
    LOGT("%s:%d %d", __func__, __LINE__, type);
    char *str;
    if (!msg) return;
    osbus_msg_t *d = NULL;
    osbus_msg_from_blob_attr(&d, msg);

    // type 2 = UBUS_MSG_DATA
    str = blobmsg_format_json_indent(msg, true, -1);
    LOGT("%s: %d '%s'", __func__, type, str);
    free(str);

    osbus_msg_t **reply = req->priv;
    if (reply) {
        *reply = d;
    } else {
        osbus_msg_free(d);
    }
}

bool osbus_ubus_method_invoke(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *message,
        osbus_msg_t **reply)
{
    struct ubus_context *ctx = handle->bus_handle;
    uint32_t id = 0;
    int ret = 0;
    struct blob_buf *b = NULL;
    struct blob_attr *b_attr = NULL;
    bool retval = false;
    if (!ctx) return false;
    if (reply) *reply = NULL;
    char dbg_str[OSBUS_DBG_STR_SIZE];
    char *method = path.element;
    path.element = NULL;
    char *path_str = osbus_ubus_path_fmta(handle, path);

    osbus_msg_to_dbg_str_fixed(message, dbg_str, sizeof(dbg_str));
    LOGT("%s invoke: %s %s %s", __func__, path_str, method, dbg_str);

    ret = ubus_lookup_id(ctx, path_str, &id);
    if (ret != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, ret, "ubus_lookup_id", path_str);
        return false;
    }
    LOGT("%sd lookup %s id=%d", __func__, path_str, id);

    if (message) {
        if (!osbus_msg_to_blob_buf(message, &b)) {
            osbus_ubus_error_set_and_log(handle, UBUS_STATUS_INVALID_ARGUMENT,
                    "osbus_msg_to_blob_buf", strfmta("%s, %s", path_str, method));
            goto out;
        }
    }
    b_attr = b ? b->head : NULL;
    ret = ubus_invoke(ctx, id, method, b_attr, osbus_ubus_receive_reply, reply, OSBUS_DEFAULT_TIMEOUT);
    LOGT("%s ubus_invoke '%s' ret=%d", __func__, method, ret);
    if (ret != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, ret, "ubus_invoke", strfmta("%s, %s", path_str, method));
        goto out;
    }
    retval = true;

out:
    if (b) {
        blob_buf_free(b);
        free(b);
        b = NULL;
    }
    return retval;
}

void osbus_ubus_receive_reply_async(struct ubus_request *req, int type, struct blob_attr *msg)
{
    LOGT("%s:%d %d", __func__, __LINE__, type);
    char *str;
    if (!msg) return;
    osbus_msg_t *reply = NULL;

    osbus_ubus_async_invoke_t *ubus_async = container_of(req, osbus_ubus_async_invoke_t, ubus_req);
    // integrity check
    if (ubus_async != req->priv) {
        LOGE("%s: invalid req %p", __func__, req);
        return;
    }
    osbus_async_invoke_t *async = &ubus_async->osbus_async_invoke;
    osbus_handle_t handle = async->handle;
    // integrity check
    if (!_osbus_async_invoke_is_valid(handle, async, __func__)) return;

    str = blobmsg_format_json_indent(msg, true, -1);
    // type 2 = UBUS_MSG_DATA
    char *method_name = async->node.name;
    LOGT("reply: %s %d %d '%s'\n", method_name, type, req->status_code, str);
    free(str);

    if (!async->reply_handler_fn) {
        LOGD("%s: %s no reply handler", __func__, method_name);
    } else {
        osbus_error_clear(handle);
        osbus_msg_from_blob_attr(&reply, msg);
        bool ret = async->reply_handler_fn(async->handle, method_name, true, reply, async->user_data);
        if (!ret) {
            LOGE("%s(%s) error", __func__, method_name);
        }
        osbus_msg_free(reply);
    }
    _osbus_async_invoke_delete(handle, async);
}

bool osbus_ubus_method_invoke_async(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *message,
        osbus_method_invoke_async_handler_fn_t *reply_handler,
        void *user_data,
        osbus_async_invoke_t **async)
{
    if (!handle) return false;
    if (!async) return false;
    struct ubus_context *ctx = handle->bus_handle;
    if (!ctx) return false;
    uint32_t id = 0;
    int ret = 0;
    struct blob_buf *b = NULL;
    struct blob_attr *b_attr = NULL;
    bool retval = false;
    char dbg_str[OSBUS_DBG_STR_SIZE];
    //char *component = path.component;
    char *method = path.element;
    path.element = NULL;
    char *path_str = osbus_ubus_path_fmta(handle, path);

    osbus_msg_to_dbg_str_fixed(message, dbg_str, sizeof(dbg_str));
    LOGT("%s invoke: %s %s %s", __func__, path_str, method, dbg_str);

    ret = ubus_lookup_id(ctx, path_str, &id);
    if (ret != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, ret, "ubus_lookup_id", path_str);
        return false;
    }
    LOGT("%s lookup %s id=%d", __func__, path_str, id);

    if (message) {
        if (!osbus_msg_to_blob_buf(message, &b)) {
            osbus_ubus_error_set_and_log(handle, UBUS_STATUS_INVALID_ARGUMENT,
                    "osbus_msg_to_blob_buf", strfmta("%s, %s", path_str, method));
            goto out;
        }
    }

    // prepare async handle
    osbus_async_invoke_t *osbus_async = _osbus_async_invoke_new(handle, method, reply_handler);
    osbus_ubus_async_invoke_t *ubus_async = _OSBUS_LIST_CAST(osbus_ubus_async_invoke_t, osbus_async);
    struct ubus_request *ubus_req = &ubus_async->ubus_req;

    b_attr = b ? b->head : NULL;
    // int ubus_invoke_async(struct ubus_context *ctx, uint32_t obj, const char *method,
    //                  struct blob_attr *msg, struct ubus_request *req)
    ret = ubus_invoke_async(ctx, id, method, b_attr, ubus_req);
    if (ret != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, ret, "ubus_invoke_async", strfmta("%s, %s", path_str, method));
        _osbus_async_invoke_delete(handle, osbus_async);
        goto out;
    }
    ubus_req->data_cb = osbus_ubus_receive_reply_async;
    ubus_req->priv = ubus_async;
    //void ubus_complete_request_async(struct ubus_context *ctx, struct ubus_request *req);
    ubus_complete_request_async(ctx, ubus_req);
    osbus_async->user_data = user_data;
    *async = osbus_async;
    retval = true;

out:
    LOGT("%s invoke '%s' ret=%d", __func__, method, ret);
    if (b) {
        blob_buf_free(b);
        free(b);
        b = NULL;
    }
    return retval;
}

bool osbus_ubus_async_invoke_wait(osbus_handle_t handle, osbus_async_invoke_t *async, int timeout_ms)
{
    bool retval = false;
    if (timeout_ms == -1) {
        timeout_ms = OSBUS_DEFAULT_TIMEOUT_ASYNC;
    }
    struct ubus_context *ctx = handle->bus_handle;
    osbus_ubus_async_invoke_t *ubus_async = _OSBUS_LIST_CAST(osbus_ubus_async_invoke_t, async);
    //int ubus_complete_request(struct ubus_context *ctx, struct ubus_request *req, int timeout);
    int rc = ubus_complete_request(ctx, &ubus_async->ubus_req, timeout_ms);
    if (rc != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, rc, "ubus_complete_request", async->node.name);
    } else {
        retval = true;
    }
    _osbus_async_invoke_delete(handle, async);
    return retval;
}

bool osbus_ubus_async_invoke_cancel(osbus_handle_t handle, osbus_async_invoke_t *async)
{
    struct ubus_context *ctx = handle->bus_handle;
    osbus_ubus_async_invoke_t *ubus_async = _OSBUS_LIST_CAST(osbus_ubus_async_invoke_t, async);
    //void ubus_abort_request(struct ubus_context *ctx, struct ubus_request *req);
    ubus_abort_request(ctx, &ubus_async->ubus_req);
    _osbus_async_invoke_delete(handle, async);
    return true;
}


// event

int _osbus_ubus_event_receive_message(
        struct ubus_context *ctx,
        struct ubus_object *obj,
        struct ubus_request_data *req,
        const char *name,
        struct blob_attr *msg)
{
    bool ret = false;
    osbus_ubus_event_handler_t *ubus_event_handler = container_of(obj, osbus_ubus_event_handler_t, ubus_sub.obj);
    osbus_handle_t handle = ubus_event_handler->handle;
    osbus_event_handler_t *info = &ubus_event_handler->osbus_event;
    osbus_msg_t *d = NULL;

    if (!_osbus_handle_is_valid(handle, __func__)) return UBUS_STATUS_UNKNOWN_ERROR;
    LOGT("%s %p %p %s %s %s %p", __func__, ctx, obj, obj->name, obj->path, name, ubus_event_handler);
    char *str = blobmsg_format_json(msg, true);
    LOGT("received event '%s': %s", name, str);
    free(str);

    if (!handle) goto out;
    if (!_osbus_event_handler_is_valid(handle, info, __func__)) goto out;
    if (!info->event_handler_fn) goto out;
    if (!osbus_msg_from_blob_attr(&d, msg)) goto out;
    ret = info->event_handler_fn(handle, info->node.name, (char*)name, d, info->user_data);
out:
    if (!ret) LOGW("%s(%s)", __func__, name);
    osbus_msg_free(d);
    return 0;
}

bool osbus_ubus_event_subscribe(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_event_handler_fn_t *event_handler_fn,
        void *user_data)
{
    LOGT("%s(%p, %s)", __func__, handle, osbus_path_dbg_fmta(path));
    if (!handle || !event_handler_fn) return false;
    bool ret = false;
    int rc = 0;
    struct ubus_context *ctx = handle->bus_handle;
    char *path_str = osbus_ubus_path_fmta(handle, path);
    osbus_event_handler_t *tinfo = NULL;
    struct ubus_subscriber *ubus_sub = NULL;
    uint32_t id;

    if (!path_str) goto out;
    tinfo = _osbus_event_handler_new(handle, path_str, event_handler_fn);
    tinfo->user_data = user_data;
    osbus_ubus_event_handler_t *ubus_event_handler = _OSBUS_LIST_CAST(osbus_ubus_event_handler_t, tinfo);
    ubus_event_handler->handle = handle;
    ubus_sub = &ubus_event_handler->ubus_sub;
    ubus_sub->cb = _osbus_ubus_event_receive_message;

    LOGT("ubus_register_subscriber(%p, %p) %p", ctx, ubus_sub, tinfo);
    rc = ubus_register_subscriber(ctx, ubus_sub);
    if (rc != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, rc, "ubus_register_subscriber", path_str);
        goto out;
    }
    rc = ubus_lookup_id(ctx, path_str, &id);
    if (rc != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, rc, "ubus_lookup_id", path_str);
        goto out;
    }
    LOGT("ubus_subscribe(%p, %p, %x)", ctx, ubus_sub, id);
    rc = ubus_subscribe(ctx, ubus_sub, id);
    if (rc != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, rc, "ubus_subscribe", path_str);
        goto out;
    }
    ret = true;
out:
    if (!ret) {
        if (tinfo) _osbus_event_handler_delete(handle, tinfo);
    } else {
        LOGD("%s(%s)", __func__, path_str);
    }
    return ret;
}

bool osbus_ubus_event_register(
        osbus_handle_t handle,
        osbus_path_t path)
{
    bool ret = false;
    struct ubus_context     *ctx = handle->bus_handle;
    struct ubus_object_type *u_objtype = NULL;
    struct ubus_object      *u_obj = NULL;
    int rc = UBUS_STATUS_INVALID_ARGUMENT;
    char *path_str = osbus_ubus_path_fmta(handle, path);

    if (!path_str) goto out;
    osbus_event_reg_t *e = _osbus_event_reg_new(handle, path_str);

    u_obj = CALLOC(sizeof(*u_obj), 1);
    u_objtype = CALLOC(sizeof(*u_objtype), 1);

    u_objtype->name = e->node.name;
    u_objtype->methods = NULL;
    u_objtype->n_methods = 0;
    u_objtype->id = 0;

    u_obj->name = e->node.name;
    u_obj->methods = NULL;
    u_obj->n_methods = 0;
    u_obj->type = u_objtype;

    e->bus_data = u_obj;

    rc = ubus_add_object(ctx, u_obj);
    LOGT("%s:%d(%p) ubus_add_object(%p, %p)=%d", __func__, __LINE__, handle, ctx, u_obj, rc);
    if (rc) {
        LOGE("UBUS: Failed to add object: %s\n", ubus_strerror(rc));
        return false;
    }
    ret = true;
out:
    return ret;
}

bool osbus_ubus_event_publish(
        osbus_handle_t handle,
        osbus_path_t path,
        const char *event_name,
        const osbus_msg_t *msg)
{
    if (!handle || !msg) return false;
    bool ret = false;
    struct ubus_context *ctx = handle->bus_handle;
    struct ubus_object *u_obj = NULL;
    osbus_event_reg_t *e = NULL;
    struct blob_buf *bb = NULL;
    char *path_str = osbus_ubus_path_fmta(handle, path);
    int rc;

    if (!path_str) goto out;
    LOGT("%s(%p, %s, %s)", __func__, handle, path_str, event_name);
    e = _osbus_event_reg_find_name(handle, path_str);
    if (!e) {
        LOGE("%s find(%s)", __func__, path_str);
        goto out;
    }
    u_obj = e->bus_data;
    if (!ctx || !u_obj) {
        LOGE("%s(%s,%s) ctx=%p obj=%p", __func__, path_str, event_name, ctx, u_obj);
        goto out;
    }
    if (!osbus_msg_to_blob_buf(msg, &bb)) goto out;
    LOGT("ubus_notify(%p, %p, %s, %p, %d)",
            ctx, u_obj, event_name, bb->head, OSBUS_DEFAULT_TIMEOUT);
    rc = ubus_notify(ctx, u_obj, event_name, bb->head, OSBUS_DEFAULT_TIMEOUT);
    if (rc != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, rc, "ubus_notify", strfmta("%s, %s", path_str, event_name));
        goto out;
    }
    ret = true;
out:
    if (!ret) {
        LOGE("%s(%s, %s)", __func__, path_str, event_name);
    }
    osbus_msg_free_blob_buf(bb);

    return ret;
}


// topic


void _osbus_ubus_topic_receive_message(
        struct ubus_context *ctx,
        struct ubus_event_handler *ev,
        const char *type,
        struct blob_attr *msg)
{
    bool ret = false;
    osbus_ubus_topic_handler_t *ubus_topic_handler = container_of(ev, osbus_ubus_topic_handler_t, ubus_ev);
    osbus_topic_handler_t *tinfo = &ubus_topic_handler->osbus_topic;
    osbus_handle_t handle = tinfo->handle;
    osbus_msg_t *d = NULL;

    char *str = blobmsg_format_json(msg, true);
    LOGT("received topic '%s': %s\n", type, str);
    free(str);

    if (!handle || !tinfo->topic_handler_fn) goto out;
    if (!osbus_msg_from_blob_attr(&d, msg)) goto out;
    ret = tinfo->topic_handler_fn(handle, (char*)type, d, tinfo->user_data);
out:
    if (!ret) LOGW("%s(%s)", __func__, type);
    osbus_msg_free(d);
    return;
}

bool osbus_ubus_topic_listen(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_topic_handler_fn_t *topic_handler_fn,
        void *user_data)
{
    if (!handle || !topic_handler_fn) return false;
    bool ret = false;
    int rc = 0;
    struct ubus_context *ctx = handle->bus_handle;
    osbus_topic_handler_t *tinfo = NULL;
    struct ubus_event_handler *ubus_ev = NULL;
    char *path_str = osbus_ubus_path_fmta(handle, path);

    LOGT("%s(%p, %s)", __func__, handle, path_str);
    if (!path_str) goto out;
    tinfo = _osbus_topic_handler_new(handle, path_str, topic_handler_fn);
    tinfo->user_data = user_data;
    osbus_ubus_topic_handler_t *ubus_tinfo = _OSBUS_LIST_CAST(osbus_ubus_topic_handler_t, tinfo);
    ubus_ev = &ubus_tinfo->ubus_ev;
    ubus_ev->cb = _osbus_ubus_topic_receive_message;
    rc = ubus_register_event_handler(ctx, ubus_ev, path_str);
    if (rc != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, rc, "ubus_register_event_handler", path_str);
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

bool osbus_ubus_topic_unlisten(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_topic_handler_fn_t *topic_handler_fn)
{
    if (!handle || !topic_handler_fn) return false;
    bool ret = false;
    int rc = 0;
    struct ubus_context *ctx = handle->bus_handle;
    osbus_topic_handler_t *tinfo = NULL;
    struct ubus_event_handler *ubus_ev = NULL;
    char *path_str = osbus_ubus_path_fmta(handle, path);

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
    osbus_ubus_topic_handler_t *ubus_tinfo = _OSBUS_LIST_CAST(osbus_ubus_topic_handler_t, tinfo);
    ubus_ev = &ubus_tinfo->ubus_ev;
    rc = ubus_unregister_event_handler(ctx, ubus_ev);
    if (rc != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, rc, "ubus_unregister_event_handler", path_str);
        goto out;
    }
    _osbus_topic_handler_delete(handle, tinfo);
    ret = true;
out:
    LOGD("%s(%s): %d", __func__, path_str, ret);
    return ret;
}

bool osbus_ubus_topic_send(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *msg)
{
    if (!handle || !msg) return false;
    bool ret = false;
    struct ubus_context *ctx = handle->bus_handle;
    struct blob_buf *bb = NULL;
    char *path_str = osbus_ubus_path_fmta(handle, path);
    int rc;

    LOGT("%s(%p, %s)", __func__, handle, path_str);
    if (!path_str) goto out;
    if (!osbus_msg_to_blob_buf(msg, &bb)) goto out;
    rc = ubus_send_event(ctx, path_str, bb->head);
    if (rc != UBUS_STATUS_OK) {
        osbus_ubus_error_set_and_log(handle, rc, "ubus_send_event", path_str);
        goto out;
    }
    ret = true;
out:
    if (!ret) {
        LOGE("%s(%s)", __func__, path_str);
    }
    osbus_msg_free_blob_buf(bb);

    return ret;
}

struct osbus_ops osbus_ubus_ops = {
    .op_osbus_path_fmt              = osbus_ubus_path_fmt,
    .op_osbus_connect_bus           = osbus_ubus_connect,
    .op_osbus_disconnect_bus        = osbus_ubus_disconnect,
    .op_osbus_list_msg              = osbus_ubus_list,
    .op_osbus_method_register       = osbus_ubus_method_register,
    .op_osbus_method_unregister     = osbus_ubus_method_unregister,
    .op_osbus_method_reply_async    = osbus_ubus_method_reply_async,
    .op_osbus_method_invoke         = osbus_ubus_method_invoke,
    .op_osbus_method_invoke_async   = osbus_ubus_method_invoke_async,
    .op_osbus_method_invoke_async_wait   = osbus_ubus_async_invoke_wait,
    .op_osbus_method_invoke_async_cancel = osbus_ubus_async_invoke_cancel,
    .op_osbus_event_register        = osbus_ubus_event_register,
    .op_osbus_event_subscribe       = osbus_ubus_event_subscribe,
    .op_osbus_event_publish         = osbus_ubus_event_publish,
    .op_osbus_topic_listen          = osbus_ubus_topic_listen,
    .op_osbus_topic_unlisten        = osbus_ubus_topic_unlisten,
    .op_osbus_topic_send            = osbus_ubus_topic_send,
};

struct osbus_ops *osbus_ubus_get_ops(void)
{
    return &osbus_ubus_ops;
}


