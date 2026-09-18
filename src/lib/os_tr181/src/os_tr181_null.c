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
 * os_tr181_null_impl.c - Null implementation for compile verification
 *
 * This implementation returns OS_TR181_ERROR_NOT_IMPLEMENTED for all APIs.
 * Used for build verification without requiring actual backend libraries.
 */

#include "os_tr181.h"
#include <stdlib.h>

/* Dummy handle structure */
struct os_tr181_handle_s
{
    int dummy;
    /* Event loop integration */
    os_tr181_fd_change_callback_t fd_change_cb; /* FD change notification callback */
    void *fd_change_user_data;                  /* User data for FD change callback */
    void *loop_ctx;                             /* Event loop context (struct os_tr181_libev_context*) */
};

os_tr181_error_t os_tr181_init_ex(os_tr181_handle_t **handle, const char *component_name)
{
    (void)handle;         /* unused */
    (void)component_name; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

void os_tr181_close(os_tr181_handle_t *handle)
{
    /* Detach from event loop if attached */
    os_tr181_detach_loop(handle);

    (void)handle; /* unused */
}

os_tr181_error_t os_tr181_get_val(os_tr181_handle_t *handle, const char *param_name, os_tr181_val_t *value)
{
    (void)handle;     /* unused */
    (void)param_name; /* unused */
    (void)value;      /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_set_val(os_tr181_handle_t *handle, const char *param_name, const os_tr181_val_t *value)
{
    (void)handle;     /* unused */
    (void)param_name; /* unused */
    (void)value;      /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_list(
        os_tr181_handle_t *handle,
        const char *path,
        uint32_t flags,
        os_tr181_param_info_t **params,
        int *count)
{
    (void)handle; /* unused */
    (void)path;   /* unused */
    (void)flags;  /* unused */
    (void)params; /* unused */
    (void)count;  /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

void os_tr181_free_list(os_tr181_param_info_t *params, int count)
{
    (void)params; /* unused */
    (void)count;  /* unused */
}

os_tr181_error_t os_tr181_subscribe(
        os_tr181_handle_t *handle,
        const char *path,
        os_tr181_event_cb_t callback,
        void *user_data,
        os_tr181_sub_handle_t *sub_handle)
{
    (void)handle;     /* unused */
    (void)path;       /* unused */
    (void)callback;   /* unused */
    (void)user_data;  /* unused */
    (void)sub_handle; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_unsubscribe(os_tr181_handle_t *handle, os_tr181_sub_handle_t sub_handle)
{
    (void)handle;     /* unused */
    (void)sub_handle; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_register_object(os_tr181_handle_t *handle, const char *object_path)
{
    (void)handle;      /* unused */
    (void)object_path; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
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
    (void)handle;       /* unused */
    (void)param_path;   /* unused */
    (void)type;         /* unused */
    (void)access_flags; /* unused */
    (void)get_callback; /* unused */
    (void)set_callback; /* unused */
    (void)user_data;    /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_register_table(
        os_tr181_handle_t *handle,
        const char *table_path,
        os_tr181_add_cb_t add_callback,
        os_tr181_del_cb_t del_callback,
        void *user_data)
{
    (void)handle;       /* unused */
    (void)table_path;   /* unused */
    (void)add_callback; /* unused */
    (void)del_callback; /* unused */
    (void)user_data;    /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_register_method(
        os_tr181_handle_t *handle,
        const char *path,
        os_tr181_method_cb_t method_cb,
        void *user_data,
        const os_tr181_param_schema_t *params,
        unsigned int flags)
{
    (void)handle;
    (void)path;
    (void)method_cb;
    (void)user_data;
    (void)params;
    (void)flags;
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_register_event(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_param_schema_t *params)
{
    (void)handle; /* unused */
    (void)path;   /* unused */
    (void)params; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_emit_event(os_tr181_handle_t *handle, const char *path, const os_tr181_val_t *data)
{
    (void)handle; /* unused */
    (void)path;   /* unused */
    (void)data;   /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_publish_objects(os_tr181_handle_t *handle)
{
    (void)handle; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_notify_changed(
        os_tr181_handle_t *handle,
        const char *param_path,
        const os_tr181_val_t *old_value,
        const os_tr181_val_t *new_value)
{
    (void)handle;
    (void)param_path;
    (void)old_value;
    (void)new_value;
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_process_requests(os_tr181_handle_t *handle, int timeout_ms)
{
    (void)handle;     /* unused */
    (void)timeout_ms; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_add_instance_ex(
        os_tr181_handle_t *handle,
        const char *object_path,
        uint32_t index,
        const char *alias_value,
        const os_tr181_val_t *values,
        int *instance_number)
{
    (void)handle;          /* unused */
    (void)object_path;     /* unused */
    (void)index;           /* unused */
    (void)alias_value;     /* unused */
    (void)values;          /* unused */
    (void)instance_number; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_add_instance_wait(
        os_tr181_handle_t *handle,
        const char *object_path,
        int *instance_number,
        int timeout_ms)
{
    (void)handle;          /* unused */
    (void)object_path;     /* unused */
    (void)instance_number; /* unused */
    (void)timeout_ms;      /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_delete_instance(os_tr181_handle_t *handle, const char *instance_path)
{
    (void)handle;        /* unused */
    (void)instance_path; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_invoke(
        os_tr181_handle_t *handle,
        const char *path,
        const os_tr181_val_t *args,
        os_tr181_val_t *result,
        uint32_t timeout_sec)
{
    (void)handle;      /* unused */
    (void)path;        /* unused */
    (void)args;        /* unused */
    (void)result;      /* unused */
    (void)timeout_sec; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_get_instances(
        os_tr181_handle_t *handle,
        const char *object_path,
        int **instance_numbers,
        int *count)
{
    (void)handle;           /* unused */
    (void)object_path;      /* unused */
    (void)instance_numbers; /* unused */
    (void)count;            /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

const char *os_tr181_get_backend_name(void)
{
    return "null";
}

/* ========================================================================
 * Async Method Invocation Stubs
 * ======================================================================== */

os_tr181_error_t os_tr181_invoke_async(
        os_tr181_handle_t *handle,
        const char *method,
        const os_tr181_val_t *args,
        os_tr181_async_cb_t callback,
        void *priv,
        int timeout_sec,
        os_tr181_async_request_t **req)
{
    (void)handle;      /* unused */
    (void)method;      /* unused */
    (void)args;        /* unused */
    (void)callback;    /* unused */
    (void)priv;        /* unused */
    (void)timeout_sec; /* unused */
    (void)req;         /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_invoke_async_cancel(os_tr181_async_request_t *req)
{
    (void)req; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

os_tr181_error_t os_tr181_method_respond(
        os_tr181_async_method_ctx_t *async_ctx,
        os_tr181_error_t error,
        os_tr181_val_t *result)
{
    (void)async_ctx; /* unused */
    (void)error;     /* unused */
    (void)result;    /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

/* ========================================================================
 * Event Loop Integration API - NULL Backend (Stubs)
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
    (void)handle;
    (void)fds;
    (void)max_fds;

    if (num_fds)
    {
        *num_fds = 0;
    }

    return OS_TR181_SUCCESS;
}

int os_tr181_get_poll_timeout(os_tr181_handle_t *handle)
{
    (void)handle;
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
