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
};

os_tr181_error_t os_tr181_init(os_tr181_handle_t **handle)
{
    (void)handle; /* unused */
    return OS_TR181_ERROR_NOT_IMPLEMENTED;
}

void os_tr181_close(os_tr181_handle_t *handle)
{
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
        bool recursive,
        os_tr181_param_info_t **params,
        int *count)
{
    (void)handle;    /* unused */
    (void)path;      /* unused */
    (void)recursive; /* unused */
    (void)params;    /* unused */
    (void)count;     /* unused */
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

os_tr181_error_t os_tr181_wait_event(os_tr181_handle_t *handle, int timeout_ms)
{
    (void)handle;     /* unused */
    (void)timeout_ms; /* unused */
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
        int writable,
        os_tr181_get_cb_t get_callback,
        os_tr181_set_cb_t set_callback,
        void *user_data)
{
    (void)handle;       /* unused */
    (void)param_path;   /* unused */
    (void)type;         /* unused */
    (void)writable;     /* unused */
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

os_tr181_error_t os_tr181_add_instance(os_tr181_handle_t *handle, const char *object_path, int *instance_number)
{
    (void)handle;          /* unused */
    (void)object_path;     /* unused */
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
