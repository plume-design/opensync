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

// private helper functions

extern _osbus_list_t _g_osbus_handles;

// generic list data type, with multiple searchable keys

void _osbus_list_init(_osbus_list_t *list, const char *list_name, int node_size)
{
    *list = _OSBUS_LIST_INIT(list_name, node_size);
}

_osbus_node_t *_osbus_list_alloc_size(_osbus_list_t *list, int size)
{
    _osbus_node_t *p;
    int min_size = MAX(list->node_size, (int)sizeof(_osbus_node_t));
    ASSERT(size >= min_size, "%s invalid size %d %d", list->list_name, size, min_size);
    p = CALLOC(size, 1);
    p->size = size;
    return p;
}

_osbus_node_t* _osbus_list_alloc_node(_osbus_list_t *list)
{
    return _osbus_list_alloc_size(list, list->node_size);
}

void _osbus_list_insert(_osbus_list_t *list, _osbus_node_t *node)
{
    ASSERT(node->node_self.otn_key == NULL, "%s already in list %p %p", __func__, list, node);
    ds_tree_insert(&list->tree_node, node, node);
}

_osbus_node_t *_osbus_list_new_node(_osbus_list_t *list)
{
    _osbus_node_t *node = _osbus_list_alloc_node(list);
    _osbus_list_insert(list, node);
    return node;
}

_osbus_node_t* _osbus_list_new_named(_osbus_list_t *list, const char *name)
{
    _osbus_node_t *node = _osbus_list_new_node(list);
    _osbus_list_set_name(list, node, name);
    return node;
}

void _osbus_list_set_name(_osbus_list_t *list, _osbus_node_t *node, const char *name)
{
    if (node->node_name.otn_key) {
        ds_tree_remove(&list->tree_name, node);
        node->node_name.otn_key = NULL;
    }
    if (node->name) {
        free(node->name);
        node->name = NULL;
    }
    if (name) {
        node->name = STRDUP(name);
        ds_tree_insert(&list->tree_name, node, node->name);
    }
}

void _osbus_list_set_ptr(_osbus_list_t *list, _osbus_node_t *node, void *ptr)
{
    if (node->node_ptr.otn_key) {
        ds_tree_remove(&list->tree_ptr, node);
        node->node_ptr.otn_key = NULL;
    }
    if (ptr) {
        ds_tree_insert(&list->tree_ptr, node, ptr);
    }
}

_osbus_node_t* _osbus_list_find_node(_osbus_list_t *list, _osbus_node_t *node)
{
    return (_osbus_node_t*)ds_tree_find(&list->tree_node, node);
}

_osbus_node_t* _osbus_list_find_name(_osbus_list_t *list, const char *name)
{
    return (_osbus_node_t*)ds_tree_find(&list->tree_name, name);
}

_osbus_node_t* _osbus_list_find_ptr(_osbus_list_t *list, void *ptr)
{
    return (_osbus_node_t*)ds_tree_find(&list->tree_ptr, ptr);
}

bool _osbus_list_remove(_osbus_list_t *list, _osbus_node_t *node)
{
    // safety check
    if (!_osbus_list_find_node(list, node)) {
        LOGE("%s(%p %p): invalid node", __func__, list, node);
        return false;
    }
    _osbus_list_set_name(list, node, NULL);
    _osbus_list_set_ptr(list, node, NULL);
    ds_tree_remove(&list->tree_node, node);
    node->node_self.otn_key = NULL;
    return true;
}

void _osbus_list_free(_osbus_node_t *node)
{
    if (node->name) {
        free(node->name);
        node->name = NULL;
    }
    free(node);
}

void _osbus_list_delete(_osbus_list_t *list, _osbus_node_t *node)
{
    _osbus_list_remove(list, node);
    _osbus_list_free(node);
}

void _osbus_list_delete_all(_osbus_list_t *list)
{
    _osbus_node_t *node;
    while ((node = ds_tree_head(&list->tree_node)) != NULL)
    {
        _osbus_list_delete(list, node);
    }
}

bool _osbus_list_node_is_valid(_osbus_list_t *list, _osbus_node_t *node, const char *func)
{
    // integrity check - check that node is part of list and has correct size
    if (!list) {
        LOGE("%s: invalid list %p %p", func, list, node);
        return false;
    }
    if (!_osbus_list_find_node(list, node)) {
        LOGE("%s: invalid node %s %p %p", func, list->list_name, list, node);
        return false;
    }
    if (node->size != list->node_size) {
        LOGE("%s: invalid size %s %p %p %d %d", func, list->list_name, list, node, list->node_size, node->size);
        return false;
    }
    return true;
}

int _osbus_list_len(_osbus_list_t *list)
{
    return ds_tree_len(&list->tree_node);
}


// osbus_handle


osbus_handle_t _osbus_handle_new(void)
{
#ifdef static_assert
    static_assert(offsetof(struct _osbus_handle, node) == 0, "osbus_handle_t node not aligned");
#endif
    return (osbus_handle_t)_osbus_list_new_node(&_g_osbus_handles);
}

void _osbus_handle_set_bus_ctx(osbus_handle_t h, void *bus_ctx)
{
    _osbus_list_set_ptr(&_g_osbus_handles, &h->node, bus_ctx);
}

osbus_handle_t _osbus_handle_find_bus_ctx(void *bus_ctx)
{
    return (osbus_handle_t)_osbus_list_find_ptr(&_g_osbus_handles, bus_ctx);
}

bool _osbus_handle_is_valid(osbus_handle_t h, const char *func)
{
    if (!h || !_osbus_list_find_node(&_g_osbus_handles, &h->node)) {
        LOGE("%s: invalid handle %p", func, h);
        return false;
    }
    return true;
}

void _osbus_handle_delete(osbus_handle_t h)
{
    _osbus_list_delete(&_g_osbus_handles, &h->node);
}


// method handler list


osbus_msg_policy_t* _osbus_method_policy_copy(osbus_msg_policy_t *policy, int n_policy)
{
    osbus_msg_policy_t *copy = CALLOC(sizeof(osbus_msg_policy_t), n_policy);
    int i;
    for (i=0; i<n_policy; i++) {
        copy[i].name     = STRDUP(policy[i].name);
        copy[i].type     = policy[i].type;
        copy[i].required = policy[i].required;
    }
    return copy;
}

void _osbus_method_policy_free(osbus_msg_policy_t *policy, int n_policy)
{
    int i;
    for (i=0; i<n_policy; i++) free(policy[i].name);
    free(policy);
}

osbus_method_handler_t* _osbus_method_handler_new(osbus_handle_t handle, char *node_name)
{
    return (osbus_method_handler_t*)_osbus_list_new_named(&handle->method_handler_list, node_name);
}

void _osbus_method_handler_set_info(osbus_method_handler_t *method_handler, const osbus_method_t *info)
{
    // method_handler->node.name is searchable and can be full expanded path
    // method_handler->method_name is original name
    method_handler->method_name = STRDUP(info->name);
    method_handler->handler_fn = info->handler_fn;
    method_handler->policy = _osbus_method_policy_copy(info->policy, info->n_policy);
    method_handler->n_policy = info->n_policy;
}

void _osbus_method_handler_free_info(osbus_method_handler_t *method_handler)
{
    free(method_handler->method_name);
    method_handler->method_name = NULL;
    method_handler->handler_fn = NULL;
    _osbus_method_policy_free(method_handler->policy, method_handler->n_policy);
    method_handler->policy = NULL;
    method_handler->n_policy = 0;
}

osbus_method_handler_t* _osbus_method_handler_find_name(osbus_handle_t handle, const char *name)
{
    return (osbus_method_handler_t*)_osbus_list_find_name(&handle->method_handler_list, name);
}

bool _osbus_method_handler_is_valid(osbus_handle_t handle, osbus_method_handler_t *method_handler, const char *func)
{
    if (!_osbus_list_node_is_valid(&handle->method_handler_list, &method_handler->node, func)) {
        osbus_error_set(handle, OSBUS_ERROR_INVALID_ARGUMENT);
        return false;
    }
    return true;
}

void _osbus_method_handler_delete(osbus_handle_t handle, osbus_method_handler_t *method_handler)
{
    _osbus_method_handler_free_info(method_handler);
    _osbus_list_delete(&handle->method_handler_list, &method_handler->node);
}

// method_table helpers

bool _osbus_method_table_validate(osbus_handle_t handle, const osbus_method_t *method_table, int n_methods)
{
    const osbus_method_t *m;
    char *estr = NULL;
    int i, j;
    for (i=0; i<n_methods; i++) {
        m = &method_table[i];
        if (!m->name) { estr = "name"; goto err; }
        if (!m->handler_fn) { estr = "handler_fn"; goto err; }
        for (j=0; j<m->n_policy; j++) {
            if (!m->policy[j].name) { estr = "policy"; goto err; }
        }
    }
    return true;
err:
    osbus_error_set_and_log(handle, OSBUS_ERROR_INVALID_ARGUMENT, estr, __func__, NULL);
    return false;
}

bool _osbus_method_handler_check_duplicate(osbus_handle_t handle, const osbus_method_t *method_table, int n_methods)
{
    _osbus_list_t tmp = _OSBUS_LIST_INIT("tmp", 0);
    bool retval = false;
    typeof(handle->ops->op_osbus_method_name_fmt) fmt_fn = handle->ops->op_osbus_method_name_fmt;
    char method_name[OSBUS_NAME_SIZE];
    char *name = NULL;
    int i;
    for (i=0; i<n_methods; i++) {
        if (fmt_fn) {
            fmt_fn(handle, method_table[i].name, method_name, sizeof(method_name));
            name = method_name;
        } else {
            name = method_table[i].name;
        }
        if (_osbus_method_handler_find_name(handle, name) != NULL) goto out;
        if (_osbus_list_find_name(&tmp, name)) goto out;
        _osbus_list_new_named(&tmp, name);
    }
    retval = true;
out:
    _osbus_list_delete_all(&tmp);
    if (!retval) {
        osbus_error_set_and_log(handle, OSBUS_ERROR_EXISTS, "duplicate method", __func__, name);
    }
    return retval;
}

bool _osbus_method_handler_remove_from_table(osbus_handle_t handle, const osbus_method_t *method_table, int n_methods)
{
    osbus_method_handler_t *mh;
    const osbus_method_t *in;
    typeof(handle->ops->op_osbus_method_name_fmt) fmt_fn = handle->ops->op_osbus_method_name_fmt;
    char method_name[OSBUS_NAME_SIZE];
    char *name = NULL;
    int i;
    for (i=0; i<n_methods; i++) {
        in = &method_table[i];
        if (fmt_fn) {
            fmt_fn(handle, in->name, method_name, sizeof(method_name));
            name = method_name;
        } else {
            name = in->name;
        }
        if ((mh = _osbus_method_handler_find_name(handle, name)) != NULL) {
            _osbus_method_handler_delete(handle, mh);
        }
    }
    return true;
}

bool _osbus_method_handler_add_from_table(osbus_handle_t handle, const osbus_method_t *method_table, int n_methods,
        bool (*filter_fn)(osbus_handle_t handle, osbus_method_handler_t *mh, int i, void *user_ptr), void *user_ptr)
{
    osbus_method_handler_t *mh;
    const osbus_method_t *in;
    typeof(handle->ops->op_osbus_method_name_fmt) fmt_fn = handle->ops->op_osbus_method_name_fmt;
    char method_name[OSBUS_NAME_SIZE];
    char *name = NULL;
    int i;
    for (i=0; i<n_methods; i++) {
        in = &method_table[i];
        if (fmt_fn) {
            fmt_fn(handle, in->name, method_name, sizeof(method_name));
            name = method_name;
        } else {
            name = in->name;
        }
        if ((mh = _osbus_method_handler_find_name(handle, name)) != NULL) {
            osbus_error_set_and_log(handle, OSBUS_ERROR_EXISTS, "method exists", __func__, name);
            goto err;
        }
        mh = _osbus_method_handler_new(handle, name);
        _osbus_method_handler_set_info(mh, in);
        if (filter_fn) {
            if (!filter_fn(handle, mh, i, user_ptr)) { i++; goto err; }
        }
    }
    return true;
err:
    // remove all added so far (up to i-1)
    _osbus_method_handler_remove_from_table(handle, method_table, i);
    return false;
}


// async invoke


osbus_async_invoke_t* _osbus_async_invoke_new(osbus_handle_t handle, const char *name,
        osbus_method_invoke_async_handler_fn_t *reply_handler_fn)
{
    osbus_async_invoke_t *async = (osbus_async_invoke_t*)_osbus_list_new_named(&handle->async_invoke_list, name);
    async->handle = handle;
    async->reply_handler_fn = reply_handler_fn;
    return async;
}

osbus_async_invoke_t* _osbus_async_invoke_find_name(osbus_handle_t handle, const char *name)
{
    return (osbus_async_invoke_t*)_osbus_list_find_name(&handle->async_invoke_list, name);
}

bool _osbus_async_invoke_is_valid(osbus_handle_t handle, osbus_async_invoke_t *async, const char *func)
{
    // integrity check, because there are multiple ways a async handle can be invalidated
    // (completed, timed-out, cancelled)
    if (!_osbus_handle_is_valid(handle, func)) return false;
    if (!_osbus_list_node_is_valid(&handle->async_invoke_list, &async->node, func)) {
        osbus_error_set(handle, OSBUS_ERROR_INVALID_ARGUMENT);
        return false;
    }
    return true;
}

void _osbus_async_invoke_delete(osbus_handle_t handle, osbus_async_invoke_t *async)
{
    _osbus_list_delete(&handle->async_invoke_list, &async->node);
}


// async reply


osbus_async_reply_t* _osbus_async_reply_alloc(osbus_handle_t handle)
{
    return (osbus_async_reply_t*)_osbus_list_alloc_node(&handle->async_reply_list);
}

void _osbus_async_reply_free(osbus_async_reply_t *async)
{
    free(async);
}

void _osbus_async_reply_insert(osbus_handle_t handle, osbus_async_reply_t *async, const char *name)
{
    _osbus_list_insert(&handle->async_reply_list, &async->node);
    _osbus_list_set_name(&handle->async_reply_list, &async->node, name);
    async->handle = handle;
}

osbus_async_reply_t* _osbus_async_reply_new(osbus_handle_t handle, const char *name)
{
    osbus_async_reply_t *async = _osbus_async_reply_alloc(handle);
    _osbus_async_reply_insert(handle, async, name);
    return async;
}

osbus_async_reply_t* _osbus_async_reply_find_name(osbus_handle_t handle, const char *name)
{
    return (osbus_async_reply_t*)_osbus_list_find_name(&handle->async_reply_list, name);
}

bool _osbus_async_reply_is_valid(osbus_handle_t handle, osbus_async_reply_t *async, const char *func)
{
    // integrity check
    if (!_osbus_handle_is_valid(handle, func)) return false;
    if (!_osbus_list_node_is_valid(&handle->async_reply_list, &async->node, func)) {
        osbus_error_set(handle, OSBUS_ERROR_INVALID_ARGUMENT);
        return false;
    }
    return true;
}

void _osbus_async_reply_delete(osbus_handle_t handle, osbus_async_reply_t *async)
{
    _osbus_list_delete(&handle->async_reply_list, &async->node);
}


// list of registered events


osbus_event_reg_t* _osbus_event_reg_new(osbus_handle_t handle, const char *name)
{
    osbus_event_reg_t *event_reg = (osbus_event_reg_t*)_osbus_list_new_named(&handle->event_reg_list, name);
    event_reg->handle = handle;
    return event_reg;
}

osbus_event_reg_t* _osbus_event_reg_find_name(osbus_handle_t handle, const char *name)
{
    return (osbus_event_reg_t*)_osbus_list_find_name(&handle->event_reg_list, name);
}

bool _osbus_event_reg_is_valid(osbus_handle_t handle, osbus_event_reg_t *event_reg, const char *func)
{
    if (!_osbus_list_node_is_valid(&handle->event_reg_list, &event_reg->node, func))
    {
        osbus_error_set(handle, OSBUS_ERROR_INVALID_ARGUMENT);
        return false;
    }
    return true;
}

void _osbus_event_reg_delete(osbus_handle_t handle, osbus_event_reg_t *event_reg)
{
    _osbus_list_delete(&handle->event_reg_list, &event_reg->node);
}


// list of subscribed event handlers


osbus_event_handler_t* _osbus_event_handler_new(osbus_handle_t handle, const char *name,
        osbus_event_handler_fn_t *event_handler_fn)
{
    osbus_event_handler_t *event_handler = (osbus_event_handler_t*)_osbus_list_new_named(&handle->event_handler_list, name);
    event_handler->handle = handle;
    event_handler->event_handler_fn = event_handler_fn;
    return event_handler;
}

osbus_event_handler_t* _osbus_event_handler_find_name(osbus_handle_t handle, const char *name)
{
    return (osbus_event_handler_t*)_osbus_list_find_name(&handle->event_handler_list, name);
}

bool _osbus_event_handler_is_valid(osbus_handle_t handle, osbus_event_handler_t *event_handler, const char *func)
{
    if (!_osbus_list_node_is_valid(&handle->event_handler_list, &event_handler->node, func))
    {
        osbus_error_set(handle, OSBUS_ERROR_INVALID_ARGUMENT);
        return false;
    }
    return true;
}

void _osbus_event_handler_delete(osbus_handle_t handle, osbus_event_handler_t *event_handler)
{
    _osbus_list_delete(&handle->event_handler_list, &event_handler->node);
}


// topic handler


osbus_topic_handler_t* _osbus_topic_handler_new(osbus_handle_t handle, const char *name,
        osbus_topic_handler_fn_t *topic_handler_fn)
{
    osbus_topic_handler_t *topic_handler =
        (osbus_topic_handler_t*)_osbus_list_new_named(&handle->topic_handler_list, name);
    topic_handler->handle = handle;
    topic_handler->topic_handler_fn = topic_handler_fn;
    return topic_handler;
}

osbus_topic_handler_t* _osbus_topic_handler_find_name(osbus_handle_t handle, const char *name)
{
    return (osbus_topic_handler_t*)_osbus_list_find_name(&handle->topic_handler_list, name);
}

bool _osbus_topic_handler_is_valid(osbus_handle_t handle, osbus_topic_handler_t *topic_handler, const char *func)
{
    if (!_osbus_list_node_is_valid(&handle->topic_handler_list, &topic_handler->node, func))
    {
        osbus_error_set(handle, OSBUS_ERROR_INVALID_ARGUMENT);
        return false;
    }
    return true;
}

void _osbus_topic_handler_delete(osbus_handle_t handle, osbus_topic_handler_t *topic_handler)
{
    _osbus_list_delete(&handle->topic_handler_list, &topic_handler->node);
}


