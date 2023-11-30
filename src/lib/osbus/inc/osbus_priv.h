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

#ifndef OSBUS_PRIV_H_INCLUDED
#define OSBUS_PRIV_H_INCLUDED

#define _OSBUS_LIST_INIT(LIST_NAME, NODE_SIZE) (_osbus_list_t){ \
    .tree_node = DS_TREE_INIT(ds_void_cmp, _osbus_node_t, node_self), \
    .tree_name = DS_TREE_INIT(ds_str_cmp,  _osbus_node_t, node_name), \
    .tree_ptr  = DS_TREE_INIT(ds_void_cmp, _osbus_node_t, node_ptr), \
    .list_name = LIST_NAME, \
    .node_size = NODE_SIZE == 0 ? (int)sizeof(_osbus_node_t) : (int)NODE_SIZE, \
}

#define _OSBUS_LIST_CAST(TYPE, PTR) \
    ((sizeof(TYPE) != PTR->node.size) ? \
     (osa_assert_dump("invalid size", __FUNCTION__, __FILE__, __LINE__, "%s %d != %d %s", \
        #TYPE, sizeof(TYPE), PTR->node.size, #PTR), NULL) : \
     (TYPE*)PTR)

#define OSBUS_HAVE_OP(HANDLE, OP) (HANDLE && HANDLE->ops && HANDLE->ops->OP)

#define OSBUS_CHECK_OP(HANDLE, OP) ((OSBUS_HAVE_OP(HANDLE, OP)) ? true : \
        (LOGE("Not impl %s:%s", HANDLE ? osbus_bus_type_str(HANDLE->bus_type) : "UNK", #OP), false))

#define _osbus_list_foreach(osbus_list, nodep) \
    for (nodep = (void*)ds_tree_head(&(osbus_list)->tree_node); \
            nodep != NULL; nodep = (void*)ds_tree_next(&(osbus_list)->tree_node, &nodep->node_self))

typedef struct _osbus_list
{
    ds_tree_t       tree_node;
    ds_tree_t       tree_name;
    ds_tree_t       tree_ptr;
    const char      *list_name;
    int             node_size;
} _osbus_list_t;

typedef struct osbus_method_handler
{
    _osbus_node_t               node;
    char                        *method_name;
    osbus_method_handler_fn_t   *handler_fn;
    osbus_msg_policy_t          *policy;
    int                         n_policy;
    void                        *bus_data;
} osbus_method_handler_t;

typedef struct osbus_event_reg
{
    _osbus_node_t               node;
    osbus_handle_t              handle;
    void                        *bus_data;
} osbus_event_reg_t;

typedef struct osbus_event_handler
{
    _osbus_node_t               node;
    osbus_handle_t              handle;
    osbus_event_handler_fn_t    *event_handler_fn;
    void                        *bus_data;
    void                        *user_data;
} osbus_event_handler_t;

typedef struct osbus_topic_handler
{
    _osbus_node_t               node;
    osbus_handle_t              handle;
    osbus_topic_handler_fn_t    *topic_handler_fn;
    void                        *bus_data;
    void                        *user_data;
} osbus_topic_handler_t;


struct _osbus_handle
{
    _osbus_node_t       node;
    osbus_bus_type_t    bus_type;
    osbus_error_t       error;
    osbus_path_t        path;
    char                *component_name;
    char                *component_path;
    struct ev_loop      *loop;
    struct ev_io        ev_io;
    void                *bus_handle;
    void                *bus_data;
    void                *user_data;
    struct osbus_ops    *ops;
    _osbus_list_t       method_handler_list;
    _osbus_list_t       async_invoke_list;
    _osbus_list_t       async_reply_list;
    _osbus_list_t       event_reg_list;     // list of registered events
    _osbus_list_t       event_handler_list; // list of subscribed events
    _osbus_list_t       topic_handler_list;
};

struct osbus_ops
{
    bool (*op_osbus_path_fmt)(
            osbus_handle_t handle,
            osbus_path_t path,
            char *str,
            int size);

    bool (*op_osbus_method_name_fmt)(
            osbus_handle_t handle,
            char *name,
            char *str,
            int size);

    bool (*op_osbus_connect_bus)(
            osbus_handle_t handle);

    bool (*op_osbus_disconnect_bus)(
            osbus_handle_t handle);

    bool (*op_osbus_list_msg)(
            osbus_handle_t handle,
            osbus_path_t path,
            bool include_elements,
            osbus_msg_t **list);

    bool (*op_osbus_method_register)(
            osbus_handle_t handle,
            const osbus_method_t *methods,
            int n_methods);

    bool (*op_osbus_method_unregister)(
            osbus_handle_t handle,
            const osbus_method_t *methods,
            int n_methods);

    bool (*op_osbus_method_reply_async)(
            osbus_handle_t handle,
            osbus_async_reply_t *reply_handle,
            bool status,
            const osbus_msg_t *reply);

    bool (*op_osbus_method_invoke)(
            osbus_handle_t handle,
            osbus_path_t path,
            const osbus_msg_t *message,
            osbus_msg_t **reply);

    bool (*op_osbus_method_invoke_async)(
            osbus_handle_t handle,
            osbus_path_t path,
            const osbus_msg_t *message,
            osbus_method_invoke_async_handler_fn_t *reply_handler_fn,
            void *user_data,
            osbus_async_invoke_t **async);

    bool (*op_osbus_method_invoke_async_wait)(
            osbus_handle_t handle,
            osbus_async_invoke_t *async,
            int timeout_ms);

    bool (*op_osbus_method_invoke_async_cancel)(
            osbus_handle_t handle,
            osbus_async_invoke_t *async);

    bool (*op_osbus_event_register)(
            osbus_handle_t handle,
            osbus_path_t path);

    bool (*op_osbus_event_subscribe)(
            osbus_handle_t handle,
            osbus_path_t path,
            osbus_event_handler_fn_t *event_handler_fn,
            void *user_data);

    bool (*op_osbus_event_publish)(
            osbus_handle_t handle,
            osbus_path_t path,
            const char *event_name,
            const osbus_msg_t *msg);

    bool (*op_osbus_topic_listen)(
            osbus_handle_t handle,
            osbus_path_t path,
            osbus_topic_handler_fn_t *topic_handler_fn,
            void *user_data);

    bool (*op_osbus_topic_unlisten)(
            osbus_handle_t handle,
            osbus_path_t path,
            osbus_topic_handler_fn_t *topic_handler_fn);

    bool (*op_osbus_topic_send)(
            osbus_handle_t handle,
            osbus_path_t path,
            const osbus_msg_t *msg);

    bool (*op_osbus_dm_get)(
            osbus_handle_t handle,
            osbus_path_t path,
            osbus_msg_t **value);

    bool (*op_osbus_dm_set)(
            osbus_handle_t handle,
            osbus_path_t path,
            const osbus_msg_t *value);

};

bool osbus_path_cmn_fmt(osbus_handle_t handle, osbus_path_t path, char *str, int size);
char* osbus_path_dbg_fmt(osbus_path_t path, char *str, int str_size);
#define osbus_path_dbg_fmta(P) osbus_path_dbg_fmt(P, alloca(OSBUS_NAME_SIZE), OSBUS_NAME_SIZE)

bool osbus_connect_bus(osbus_handle_t handle);
bool osbus_disconnect_bus(osbus_handle_t handle);
bool osbus_attach_loop(osbus_handle_t handle);
bool osbus_detach_loop(osbus_handle_t handle);


void            _osbus_list_init(_osbus_list_t *list, const char *list_name, int node_size);
_osbus_node_t*  _osbus_list_alloc_node(_osbus_list_t *list);
_osbus_node_t*  _osbus_list_alloc_size(_osbus_list_t *list, int size);
void            _osbus_list_insert(_osbus_list_t *list, _osbus_node_t *node);
_osbus_node_t*  _osbus_list_new_node(_osbus_list_t *list);
_osbus_node_t*  _osbus_list_new_named(_osbus_list_t *list, const char *name);
void            _osbus_list_set_name(_osbus_list_t *list, _osbus_node_t *node, const char *name);
void            _osbus_list_set_ptr(_osbus_list_t *list, _osbus_node_t *node, void *ptr);
_osbus_node_t*  _osbus_list_find_node(_osbus_list_t *list, _osbus_node_t *node);
_osbus_node_t*  _osbus_list_find_name(_osbus_list_t *list, const char *name);
_osbus_node_t*  _osbus_list_find_ptr(_osbus_list_t *list, void *ptr);
bool            _osbus_list_remove(_osbus_list_t *list, _osbus_node_t *node);
void            _osbus_list_free(_osbus_node_t *node);
void            _osbus_list_delete(_osbus_list_t *list, _osbus_node_t *node);
void            _osbus_list_delete_all(_osbus_list_t *list);
bool            _osbus_list_node_is_valid(_osbus_list_t *list, _osbus_node_t *node, const char *func);
int             _osbus_list_len(_osbus_list_t *list);


osbus_handle_t          _osbus_handle_new(void);
void                    _osbus_handle_set_bus_ctx(osbus_handle_t h, void *bus_ctx);
osbus_handle_t          _osbus_handle_find_bus_ctx(void *bus_ctx);
bool                    _osbus_handle_is_valid(osbus_handle_t h, const char *func);
void                    _osbus_handle_delete(osbus_handle_t h);


osbus_method_handler_t* _osbus_method_handler_new(osbus_handle_t handle, char *node_name);
void                    _osbus_method_handler_set_info(osbus_method_handler_t *method_handler, const osbus_method_t *info);
void                    _osbus_method_handler_free_info(osbus_method_handler_t *method_handler);
osbus_method_handler_t* _osbus_method_handler_find_name(osbus_handle_t handle, const char *name);
bool                    _osbus_method_handler_is_valid(osbus_handle_t handle, osbus_method_handler_t *method_handler, const char *func);
void                    _osbus_method_handler_delete(osbus_handle_t handle, osbus_method_handler_t *method_handler);

bool                    _osbus_method_table_validate(osbus_handle_t handle, const osbus_method_t *method_table, int n_methods);
bool                    _osbus_method_handler_check_duplicate(osbus_handle_t handle, const osbus_method_t *method_table, int n_methods);
bool                    _osbus_method_handler_remove_from_table(osbus_handle_t handle, const osbus_method_t *method_table, int n_methods);
bool                    _osbus_method_handler_add_from_table(osbus_handle_t handle, const osbus_method_t *method_table, int n_methods,
                        bool (*filter_fn)(osbus_handle_t handle, osbus_method_handler_t *mh, int i, void *user_ptr), void *user_ptr);


osbus_async_invoke_t*   _osbus_async_invoke_new(osbus_handle_t handle, const char *name,
                                osbus_method_invoke_async_handler_fn_t *reply_handler_fn);
osbus_async_invoke_t*   _osbus_async_invoke_find_name(osbus_handle_t handle, const char *name);
bool                    _osbus_async_invoke_is_valid(osbus_handle_t handle, osbus_async_invoke_t *async, const char *func);
void                    _osbus_async_invoke_delete(osbus_handle_t handle, osbus_async_invoke_t *async);


osbus_async_reply_t*    _osbus_async_reply_alloc(osbus_handle_t handle);
void                    _osbus_async_reply_free(osbus_async_reply_t *async);
void                    _osbus_async_reply_insert(osbus_handle_t handle, osbus_async_reply_t *async, const char *name);
osbus_async_reply_t*    _osbus_async_reply_new(osbus_handle_t handle, const char *name);
osbus_async_reply_t*    _osbus_async_reply_find_name(osbus_handle_t handle, const char *name);
bool                    _osbus_async_reply_is_valid(osbus_handle_t handle, osbus_async_reply_t *async, const char *func);
void                    _osbus_async_reply_delete(osbus_handle_t handle, osbus_async_reply_t *async);


osbus_event_handler_t*  _osbus_event_handler_new(osbus_handle_t handle, const char *name,
                                osbus_event_handler_fn_t *event_handler_fn);
osbus_event_handler_t*  _osbus_event_handler_find_name(osbus_handle_t handle, const char *name);
bool                    _osbus_event_handler_is_valid(osbus_handle_t handle, osbus_event_handler_t *event_handler, const char *func);
void                    _osbus_event_handler_delete(osbus_handle_t handle, osbus_event_handler_t *event_handler);


osbus_event_reg_t*      _osbus_event_reg_new(osbus_handle_t handle, const char *name);
osbus_event_reg_t*      _osbus_event_reg_find_name(osbus_handle_t handle, const char *name);
bool                    _osbus_event_reg_is_valid(osbus_handle_t handle, osbus_event_reg_t *event_reg, const char *func);
void                    _osbus_event_reg_delete(osbus_handle_t handle, osbus_event_reg_t *event_reg);


osbus_topic_handler_t*  _osbus_topic_handler_new(osbus_handle_t handle, const char *name,
                                osbus_topic_handler_fn_t *topic_handler_fn);
osbus_topic_handler_t*  _osbus_topic_handler_find_name(osbus_handle_t handle, const char *name);
bool                    _osbus_topic_handler_is_valid(osbus_handle_t handle, osbus_topic_handler_t *topic_handler, const char *func);
void                    _osbus_topic_handler_delete(osbus_handle_t handle, osbus_topic_handler_t *topic_handler);


#endif /* OSBUS_PRIV_H_INCLUDED */
