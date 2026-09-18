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
 * os_tr181_internal.h - Internal helper functions and structures
 *
 * This header is for internal use only. Not part of public API.
 * Contains shared internal structures and helpers used across backends.
 */

#ifndef OS_TR181_INTERNAL_H
#define OS_TR181_INTERNAL_H

#include "os_tr181.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Internal Structures
 * ======================================================================== */

/**
 * Async request structure (client-side)
 * Tracks an async method invocation
 */
struct os_tr181_async_request
{
    void *platform_handle;        /* amxb_request_t* or NULL for RBUS */
    os_tr181_async_cb_t callback; /* User callback */
    void *priv;                   /* User private data */
    char *method_name;            /* Method name (for callback) */
    os_tr181_handle_t *handle;    /* TR-181 handle */
    bool cancelled;               /* True if request was cancelled */
};

/**
 * Async method context structure (provider-side)
 * Tracks a deferred method response
 */
struct os_tr181_async_method_ctx
{
    union
    {
        void *platform_handle; /* rbusMethodAsyncHandle_t (RBUS) */
        uint64_t platform_id;  /* uint64_t call_id (Ambiorix) */
    };
    char *method_name;         /* Method name */
    os_tr181_handle_t *handle; /* TR-181 handle */
};

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

/**
 * Allocate async request structure (internal)
 */
os_tr181_async_request_t *os_tr181_async_request_alloc(
        os_tr181_handle_t *handle,
        const char *method_name,
        os_tr181_async_cb_t callback,
        void *priv);

/**
 * Free async request structure (internal)
 */
void os_tr181_async_request_free(os_tr181_async_request_t *req);

/**
 * Allocate async method context structure (internal)
 */
os_tr181_async_method_ctx_t *os_tr181_async_method_ctx_alloc(
        os_tr181_handle_t *handle,
        const char *method_name,
        void *platform_handle);

/**
 * Free async method context structure (internal)
 */
void os_tr181_async_method_ctx_free(os_tr181_async_method_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* OS_TR181_INTERNAL_H */
