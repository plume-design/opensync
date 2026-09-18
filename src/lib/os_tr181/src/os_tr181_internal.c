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
 * os_tr181_internal.c - Internal helper functions and structures
 *
 * Shared internal implementation used across backends.
 */

#include <stdlib.h>
#include <string.h>
#include "os_tr181.h"
#include "os_tr181_internal.h"
#include "log.h"

/* ========================================================================
 * Request Handle Management (Client-Side)
 * ======================================================================== */

/**
 * Allocate async request structure
 */
os_tr181_async_request_t *os_tr181_async_request_alloc(
        os_tr181_handle_t *handle,
        const char *method_name,
        os_tr181_async_cb_t callback,
        void *priv)
{
    os_tr181_async_request_t *req = NULL;

    if (!handle || !method_name || !callback)
    {
        return NULL;
    }

    req = (os_tr181_async_request_t *)calloc(1, sizeof(os_tr181_async_request_t));
    if (!req)
    {
        LOGE("Failed to allocate async request");
        return NULL;
    }

    req->method_name = strdup(method_name);
    if (!req->method_name)
    {
        LOGE("Failed to duplicate method name");
        free(req);
        return NULL;
    }

    req->handle = handle;
    req->callback = callback;
    req->priv = priv;
    req->platform_handle = NULL;
    req->cancelled = false;

    return req;
}

/**
 * Free async request structure
 */
void os_tr181_async_request_free(os_tr181_async_request_t *req)
{
    if (!req)
    {
        return;
    }

    free(req->method_name);
    free(req);
}

/* ========================================================================
 * Method Context Management (Provider-Side)
 * ======================================================================== */

/**
 * Allocate async method context structure
 */
os_tr181_async_method_ctx_t *os_tr181_async_method_ctx_alloc(
        os_tr181_handle_t *handle,
        const char *method_name,
        void *platform_handle)
{
    os_tr181_async_method_ctx_t *ctx = NULL;

    if (!handle || !method_name)
    {
        return NULL;
    }

    ctx = (os_tr181_async_method_ctx_t *)calloc(1, sizeof(os_tr181_async_method_ctx_t));
    if (!ctx)
    {
        LOGE("Failed to allocate async method context");
        return NULL;
    }

    if (method_name)
    {
        ctx->method_name = strdup(method_name);
        if (!ctx->method_name)
        {
            LOGE("Failed to duplicate method name");
            free(ctx);
            return NULL;
        }
    }

    ctx->handle = handle;
    ctx->platform_handle = platform_handle;

    return ctx;
}

/**
 * Free async method context structure
 */
void os_tr181_async_method_ctx_free(os_tr181_async_method_ctx_t *ctx)
{
    if (!ctx)
    {
        return;
    }

    free(ctx->method_name);
    free(ctx);
}
