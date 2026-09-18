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
 * os_tr181_libev.c - libev event loop integration
 *
 * Provides API functions for libev integration.
 */

#include <stdlib.h>
#include <string.h>
#include <ev.h>
#include "os_tr181.h"
#include "log.h"

/* ========================================================================
 * Backend Accessors (implemented in backend files)
 * ======================================================================== */

/* Get event loop context from handle */
extern void *os_tr181_get_loop_ctx(os_tr181_handle_t *handle);

/* Set event loop context in handle */
extern void os_tr181_set_loop_ctx(os_tr181_handle_t *handle, void *ctx);

/* ========================================================================
 * libev Context Structure
 * ======================================================================== */

/**
 * libev Context for a TR181 handle
 */
struct os_tr181_libev_context
{
    ev_io io_watchers[OS_TR181_MAX_FDS]; /* IO watchers for file descriptors */
    size_t num_watchers;                 /* Number of active IO watchers */
    ev_timer timer;                      /* Timer for timeout-based events */
    os_tr181_handle_t *handle;           /* TR181 handle */
    struct ev_loop *loop;                /* libev event loop */
};

/* ========================================================================
 * Forward Declarations
 * ======================================================================== */

static void libev_io_cb(EV_P_ ev_io *w, int revents);
static void libev_timer_cb(EV_P_ ev_timer *w, int revents);
static void libev_fd_change_cb(os_tr181_handle_t *handle, void *user_data);
static void libev_update_watchers(struct os_tr181_libev_context *ctx, int *fds, size_t num_fds);
static void libev_stop_all_watchers(struct os_tr181_libev_context *ctx);
static void libev_reschedule_timer(struct os_tr181_libev_context *ctx);

/* ========================================================================
 * libev Callback Implementations
 * ======================================================================== */

/* Stop any running timer and restart it if there are pending events. */
static void libev_reschedule_timer(struct os_tr181_libev_context *ctx)
{
    ev_timer_stop(ctx->loop, &ctx->timer);

    int timeout_ms = os_tr181_get_poll_timeout(ctx->handle);
    if (timeout_ms >= 0)
    {
        ev_timer_set(&ctx->timer, timeout_ms / 1000.0, 0.0);
        ev_timer_start(ctx->loop, &ctx->timer);
        LOGD("libev timer scheduled for %d ms", timeout_ms);
    }
    else
    {
        LOGD("libev timer stopped (no pending events)");
    }
}

/**
 * IO watcher callback - called when file descriptor is readable
 */
static void libev_io_cb(EV_P_ ev_io *w, int revents)
{
    struct os_tr181_libev_context *ctx = (struct os_tr181_libev_context *)w->data;

    (void)loop;    /* Unused */
    (void)revents; /* We only watch EV_READ */

    /* Process events with zero timeout (only read FDs that are actually ready) */
    os_tr181_process_requests(ctx->handle, 0);

    /* Reschedule the libev timer: an IO event may have armed a new internal timer
     * (e.g. persistence debounce) that the ev_timer must now reflect. */
    libev_reschedule_timer(ctx);
}

/**
 * Timer callback - called when timeout expires
 */
static void libev_timer_cb(EV_P_ ev_timer *w, int revents)
{
    struct os_tr181_libev_context *ctx = (struct os_tr181_libev_context *)w->data;

    (void)loop;    /* Unused */
    (void)revents; /* Unused */

    /* Process events with zero timeout (will attempt USP reconnection if due) */
    os_tr181_process_requests(ctx->handle, 0);

    /* Reschedule timer if there are pending events */
    libev_reschedule_timer(ctx);
}

/**
 * FD change callback - called when file descriptors change
 * This is critical for handling USP connection after attach_loop()
 */
static void libev_fd_change_cb(os_tr181_handle_t *handle, void *user_data)
{
    struct os_tr181_libev_context *ctx = (struct os_tr181_libev_context *)user_data;
    int fds[OS_TR181_MAX_FDS];
    size_t num_fds;
    os_tr181_error_t ret;

    LOGD("libev FD change callback triggered");

    /* Get updated FD list */
    ret = os_tr181_get_fds(handle, fds, OS_TR181_MAX_FDS, &num_fds);
    if (ret != OS_TR181_SUCCESS)
    {
        LOGE("os_tr181_get_fds failed: %d", ret);
        return;
    }

    /* Update libev watchers */
    libev_update_watchers(ctx, fds, num_fds);

    /* Arm/disarm reconnect timer based on current pending-event state. */
    libev_reschedule_timer(ctx);
}

/**
 * Update libev IO watchers with new FD list
 */
static void libev_update_watchers(struct os_tr181_libev_context *ctx, int *fds, size_t num_fds)
{
    size_t i;

    if (num_fds > OS_TR181_MAX_FDS)
    {
        LOGE("Too many file descriptors: %zu (max %d)", num_fds, OS_TR181_MAX_FDS);
        num_fds = OS_TR181_MAX_FDS;
    }

    /* Stop all existing watchers */
    for (i = 0; i < ctx->num_watchers; i++)
    {
        ev_io_stop(ctx->loop, &ctx->io_watchers[i]);
    }

    /* Start watchers for new FDs */
    char fd_list[256] = "";
    int o = 0;
    for (i = 0; i < num_fds; i++)
    {
        ev_io_init(&ctx->io_watchers[i], libev_io_cb, fds[i], EV_READ);
        ctx->io_watchers[i].data = ctx;
        ev_io_start(ctx->loop, &ctx->io_watchers[i]);
        o += snprintf(fd_list + o, sizeof(fd_list) - o, "%s%d", i ? ", " : "", fds[i]);
    }

    ctx->num_watchers = num_fds;
    LOGD("libev watching %zu FDs: %s", num_fds, fd_list);
}

/**
 * Stop all libev watchers (IO and timer)
 */
static void libev_stop_all_watchers(struct os_tr181_libev_context *ctx)
{
    size_t i;

    /* Stop all IO watchers */
    for (i = 0; i < ctx->num_watchers; i++)
    {
        ev_io_stop(ctx->loop, &ctx->io_watchers[i]);
    }
    ctx->num_watchers = 0;

    /* Stop timer watcher */
    ev_timer_stop(ctx->loop, &ctx->timer);
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

os_tr181_error_t os_tr181_attach_loop(os_tr181_handle_t *handle, struct ev_loop *loop)
{
    struct os_tr181_libev_context *ctx;
    int fds[OS_TR181_MAX_FDS];
    size_t num_fds;
    os_tr181_error_t ret;

    if (!handle || !loop)
    {
        return OS_TR181_ERROR_INVALID;
    }

    /* Check if already attached */
    if (os_tr181_get_loop_ctx(handle) != NULL)
    {
        LOGE("Handle already attached to libev loop");
        return OS_TR181_ERROR;
    }

    /* Allocate context */
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        LOGE("Failed to allocate libev context");
        return OS_TR181_ERROR;
    }

    ctx->handle = handle;
    ctx->loop = loop;
    ctx->num_watchers = 0;

    /* Get initial FDs */
    ret = os_tr181_get_fds(handle, fds, OS_TR181_MAX_FDS, &num_fds);
    if (ret != OS_TR181_SUCCESS)
    {
        LOGE("os_tr181_get_fds failed: %d", ret);
        free(ctx);
        return ret;
    }

    LOGD("libev attach_loop: initial FDs: %zu", num_fds);

    /* Set up IO watchers for initial FDs */
    libev_update_watchers(ctx, fds, num_fds);

    /* Set up timer watcher */
    ev_timer_init(&ctx->timer, libev_timer_cb, 0.0, 0.0);
    ctx->timer.data = ctx;

    /* Start timer with initial timeout */
    libev_reschedule_timer(ctx);

    /* Register FD change callback (CRITICAL for USP) */
    ret = os_tr181_register_fd_change_callback(handle, libev_fd_change_cb, ctx);
    if (ret != OS_TR181_SUCCESS)
    {
        LOGE("Failed to register FD change callback: %d", ret);
        libev_stop_all_watchers(ctx);
        free(ctx);
        return ret;
    }

    /* Store context in handle */
    os_tr181_set_loop_ctx(handle, ctx);

    LOGD("libev loop attached successfully");
    return OS_TR181_SUCCESS;
}

os_tr181_error_t os_tr181_detach_loop(os_tr181_handle_t *handle)
{
    struct os_tr181_libev_context *ctx;

    if (!handle)
    {
        return OS_TR181_ERROR_INVALID;
    }

    ctx = (struct os_tr181_libev_context *)os_tr181_get_loop_ctx(handle);
    if (!ctx)
    {
        /* Not attached - this is OK, return success */
        return OS_TR181_SUCCESS;
    }

    LOGD("Detaching libev loop");

    /* Unregister FD change callback */
    os_tr181_register_fd_change_callback(handle, NULL, NULL);

    /* Stop all watchers */
    libev_stop_all_watchers(ctx);

    /* Free context */
    free(ctx);
    os_tr181_set_loop_ctx(handle, NULL);

    LOGD("libev loop detached successfully");
    return OS_TR181_SUCCESS;
}
