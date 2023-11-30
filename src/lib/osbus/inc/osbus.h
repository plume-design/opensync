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

#ifndef OSBUS_H_INCLUDED
#define OSBUS_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include <ev.h>

#include "osbus_msg.h"
#include "ds_tree.h"

#define OSBUS_DEFAULT_TIMEOUT        3000   // 3 sec
#define OSBUS_DEFAULT_TIMEOUT_ASYNC 60000   // 1 min

#define OSBUS_NAME_SIZE 256

typedef enum osbus_bus_type
{
    OSBUS_BUS_TYPE_NONE = 0,
    OSBUS_BUS_TYPE_DEFAULT = 1,
    OSBUS_BUS_TYPE_UBUS = 2,
    OSBUS_BUS_TYPE_RBUS = 3,
} osbus_bus_type_t;

typedef enum osbus_error
{
    OSBUS_ERROR_SUCCESS = 0,
    OSBUS_ERROR_GENERAL,
    OSBUS_ERROR_CONNECTION,
    OSBUS_ERROR_NO_RESOURCES,
    OSBUS_ERROR_INVALID_ARGUMENT,
    OSBUS_ERROR_INVALID_OPERATION,
    OSBUS_ERROR_INVALID_METHOD,
    OSBUS_ERROR_INVALID_RESPONSE,
    OSBUS_ERROR_EXISTS,
    OSBUS_ERROR_NOT_FOUND,
    OSBUS_ERROR_PERMISSION_DENIED,
    OSBUS_ERROR_TIMEOUT,
} osbus_error_t;

typedef enum osbus_namespace
{
    OSBUS_NS_GLOBAL = 0,
    OSBUS_NS_OPENSYNC = 1,
} osbus_namespace_t;

typedef struct _osbus_handle* osbus_handle_t;
typedef struct osbus_method osbus_method_t;
typedef struct osbus_async_invoke osbus_async_invoke_t;
typedef struct osbus_async_reply osbus_async_reply_t;
typedef struct _osbus_node _osbus_node_t;

typedef struct _osbus_node
{
    ds_tree_node_t  node_self;
    ds_tree_node_t  node_name;
    ds_tree_node_t  node_ptr;
    int             size;
    char            *name;
} _osbus_node_t;


typedef struct {
    osbus_namespace_t ns;
    char *component;
    char *element;
} osbus_path_t;

typedef bool osbus_method_handler_fn_t(
        osbus_handle_t handle,
        char *method_name,
        osbus_msg_t *msg,
        osbus_msg_t **reply,
        bool *defer_reply,
        osbus_async_reply_t *reply_handle);

typedef bool osbus_topic_handler_fn_t(
        osbus_handle_t handle,
        char *topic_path,
        osbus_msg_t *msg,
        void *user_data);

typedef bool osbus_event_handler_fn_t(
        osbus_handle_t handle,
        char *path,
        char *event_name,
        osbus_msg_t *msg,
        void *user_data);

typedef bool osbus_method_invoke_async_handler_fn_t(
        osbus_handle_t handle,
        char *method_name,
        bool status,
        osbus_msg_t *reply,
        void *user_data);

typedef struct osbus_async_invoke
{
    _osbus_node_t       node;
    osbus_handle_t      handle;
    osbus_method_invoke_async_handler_fn_t *reply_handler_fn;
    void                *user_data;
    void                *bus_data;
} osbus_async_invoke_t;

typedef struct osbus_async_reply
{
    _osbus_node_t       node;
    osbus_handle_t      handle;
    void                *bus_data;
} osbus_async_reply_t;

struct osbus_method
{
    char                        *name;
    osbus_method_handler_fn_t   *handler_fn;
    osbus_msg_policy_t          *policy;
    int                         n_policy;
};

#define OSBUS_DEFAULT (osbus_default_handle())

#define OSBUS_METHOD_ENTRY(PREFIX, NAME) \
    { .name = #NAME, .handler_fn = PREFIX##_method_##NAME, .policy = PREFIX##_policy_##NAME, .n_policy = ARRAY_LEN(PREFIX##_policy_##NAME) }

#define OSBUS_METHOD_NO_POLICY(PREFIX, NAME) \
    { .name = #NAME, .handler_fn = PREFIX##_method_##NAME }


bool osbus_init(void); // init default handle

bool osbus_init_name(osbus_path_t path);

bool osbus_init_ex(
        struct ev_loop *loop,
        osbus_bus_type_t bus_type,
        osbus_path_t path);

bool osbus_set_default(osbus_handle_t handle);

osbus_handle_t osbus_default_handle(void);

bool osbus_connect(
        osbus_handle_t *handle,
        struct ev_loop *loop,
        osbus_bus_type_t bus_type,
        osbus_path_t path);

bool osbus_disconnect(osbus_handle_t handle);

bool osbus_close(void); // disconnect default handle

osbus_error_t osbus_error_get(osbus_handle_t handle);
void  osbus_error_set(osbus_handle_t handle, osbus_error_t error);
void  osbus_error_clear(osbus_handle_t handle);
char* osbus_error_str(osbus_error_t error);
char* osbus_error_get_str(osbus_handle_t handle);
void  osbus_error_general_if_unset(osbus_handle_t handle);
__attribute__((format(printf, 4, 5)))
void  osbus_error_set_and_log_fmt(osbus_handle_t handle, osbus_error_t error,
        log_severity_t level, const char *fmt, ...);
void  osbus_error_set_and_log_lvl(osbus_handle_t handle, osbus_error_t error,
        log_severity_t level, const char *errstr, const char *func, const char *param);
void  osbus_error_set_and_log(osbus_handle_t handle, osbus_error_t error,
        const char *errstr, const char *func, const char *param);

static inline osbus_path_t osbus_path(
    osbus_namespace_t ns,
    char *component,
    char *element)
{
    osbus_path_t p = {
        .ns = ns,
        .component = component,
        .element = element,
    };
    return p;
}

static inline osbus_path_t osbus_path_gbl(char *component, char *element)
{
    return osbus_path(OSBUS_NS_GLOBAL, component, element);
}

static inline osbus_path_t osbus_path_os(char *component, char *element)
{
    return osbus_path(OSBUS_NS_OPENSYNC, component, element);
}

osbus_path_t osbus_handle_get_path(osbus_handle_t handle);
const char* osbus_handle_get_name(osbus_handle_t handle);

bool osbus_path_fmt(
        osbus_handle_t handle,
        osbus_path_t path,
        char *path_str,
        int str_size);

bool osbus_list_msg(
        osbus_handle_t handle,
        osbus_path_t path,
        bool include_elements,
        osbus_msg_t **list);

// methods provide a "1:1" communication concept

bool osbus_method_register(
        osbus_handle_t handle,
        const osbus_method_t *methods,
        int n_methods);

bool osbus_method_unregister(
        osbus_handle_t handle,
        const osbus_method_t *methods,
        int n_methods);

bool osbus_method_invoke(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *message,
        osbus_msg_t **reply);

bool osbus_method_invoke_async(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *message,
        osbus_method_invoke_async_handler_fn_t *reply_handler_fn,
        void *user_data,
        osbus_async_invoke_t **async);

#if 0
// async_wait disabled, not considered a proper approach
bool osbus_method_invoke_async_wait(
        osbus_handle_t handle,
        osbus_async_invoke_t *async,
        int timeout);
#endif

bool osbus_method_invoke_async_cancel(
        osbus_handle_t handle,
        osbus_async_invoke_t *async);

bool osbus_method_reply_async(
        osbus_handle_t handle,
        osbus_async_reply_t *reply_handle,
        bool status,
        const osbus_msg_t *reply);


// events provide a "1:N" communication concept
// and are tied to an object which has to be registered first
bool osbus_event_register(
        osbus_handle_t handle,
        osbus_path_t path);

bool osbus_event_subscribe(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_event_handler_fn_t *event_handler_fn,
        void *user_data);

bool osbus_event_publish(
        osbus_handle_t handle,
        osbus_path_t path,
        const char *event_name,
        const osbus_msg_t *msg);


// topics provide a "M:N" communication concept
// the topic does not need to be registered upfront
// anyone can send and listen to same topic
bool osbus_topic_listen(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_topic_handler_fn_t *topic_handler_fn,
        void *user_data);

bool osbus_topic_unlisten(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_topic_handler_fn_t *topic_handler_fn);

bool osbus_topic_send(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *msg);

// data model

bool osbus_dm_get(
        osbus_handle_t handle,
        osbus_path_t path,
        osbus_msg_t **value);

bool osbus_dm_set(
        osbus_handle_t handle,
        osbus_path_t path,
        const osbus_msg_t *value);


#endif /* OSBUS_H_INCLUDED */

