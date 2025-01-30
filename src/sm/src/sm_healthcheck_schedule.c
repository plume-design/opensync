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

#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ev.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <netdb.h>
#include "execsh.h"

#include "sm.h"
#include "memutil.h"
#include "ds.h"
#include "ds_list.h"
#include <arpa/inet.h>

#define MODULE_ID LOG_MODULE_ID_MAIN
#define SM_MAX_SECRET_LEN           65
#define RADCLIENT_COMMAND           "echo \"Message-Authenticator = 0x00\" | radclient  %s:%d status %s"
#define RADCLIENT_COMMAND_IPV6      "echo \"Message-Authenticator = 0x00\" | radclient -6 [%s]:%d status %s"

struct sm_healthcheck_schedule_ctx
{
    uint32_t                     interval;                  // healthcheck interval in seconds
    char                         server[INET6_ADDRSTRLEN];  // server IP
    uint16_t                     port;                      // server port
    ev_timer                     schedule_timer;            // timer for auto execution
    execsh_async_t               radclient_execsh;          // execsh struct for async exec of radclient
    char                         secret[SM_MAX_SECRET_LEN]; // server secret
    bool                         last_healthy;              // cache value so we don't do dead ovsdb updates
    bool                         use_cache;                 // use cache for health value
    ds_list_t                    node;
};

typedef struct sm_healthcheck_schedule_ctx* sm_healthcheck_ctx_p;

static sm_healthcheck_update_cb_t   g_update_ovsdb_cb = NULL;
static ds_list_t                    g_sm_healthcheck_servers = DS_LIST_INIT(struct sm_healthcheck_schedule_ctx, node);

/*****************************************************************************/

static
sm_healthcheck_ctx_p sm_healthcheck_find_ctx(const char* ip, uint16_t port)
{
    sm_healthcheck_ctx_p ctx_curr;
    ds_list_iter_t iter;

    for (ctx_curr = ds_list_ifirst(&iter, &g_sm_healthcheck_servers); ctx_curr != NULL; ) {
        if ((ctx_curr->port == port) && (strcmp(ctx_curr->server, ip) == 0)) {
            return ctx_curr;
        }
        ctx_curr = ds_list_inext(&iter);
    }

    return NULL;
}

static
sm_healthcheck_ctx_p sm_healthcheck_find_ctx_by_esa(execsh_async_t *esa)
{
    sm_healthcheck_ctx_p ctx_curr;
    ds_list_iter_t iter;

    for (ctx_curr = ds_list_ifirst(&iter, &g_sm_healthcheck_servers); ctx_curr != NULL; ) {
        if (&ctx_curr->radclient_execsh == esa) {
            return ctx_curr;
        }
        ctx_curr = ds_list_inext(&iter);
    }

    return NULL;
}

/*****************************************************************************/

void sm_healthcheck_schedule_stop(sm_healthcheck_ctx_p ctx)
{
    if (!ctx)
        return;

    ev_timer_stop(EV_DEFAULT, &ctx->schedule_timer);
    if (ctx->radclient_execsh.esa_running) {
        execsh_async_stop(&ctx->radclient_execsh);
    }
}

void sm_healthcheck_schedule_start(sm_healthcheck_ctx_p ctx)
{
    if (!ctx)
        return;

    ctx->schedule_timer.repeat = ctx->interval;
    ev_timer_again(EV_DEFAULT, &ctx->schedule_timer);
}

/*****************************************************************************/

void radclient_execsh_fn(execsh_async_t *esa, int exit_status)
{
    sm_healthcheck_ctx_p ctx;
    bool new_healthy;

    ctx = sm_healthcheck_find_ctx_by_esa(esa);
    if (!ctx)
        return;

    sm_healthcheck_schedule_start(ctx);
    if (exit_status) {
        LOGN("RADIUS healthcheck failed, exit code: %d", exit_status);
    }

    new_healthy = exit_status == 0;
    // if health changed from the last time or it's first health check
    // ever, set last_health to current state, update ovsdb and start
    // using cached value.
    if (!ctx->use_cache || (new_healthy != ctx->last_healthy)) {
        g_update_ovsdb_cb(ctx->server, ctx->port, new_healthy);
        ctx->last_healthy = new_healthy;
        ctx->use_cache = true;
    }
}

static
void trigger_healthcheck_cb(EV_P_ ev_timer *w, int revents)
{
    char command[255] = {0};
    sm_healthcheck_ctx_p ctx = (sm_healthcheck_ctx_p)w->data;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_RAW;
    hints.ai_flags = AI_NUMERICHOST;
    int val = getaddrinfo(ctx->server, NULL, &hints, &res);
    if (val) {
        LOGW("RADIUS healthckech failed, unable to parse server address (%s)", ctx->server);
        return;
    }

    sm_healthcheck_schedule_stop(ctx);

    switch (res->ai_family) {
        case AF_INET:
            snprintf(command,
                     sizeof(command),
                     RADCLIENT_COMMAND,
                     ctx->server,
                     ctx->port,
                     ctx->secret);
            break;
        case AF_INET6:
            snprintf(command,
                     sizeof(command),
                     RADCLIENT_COMMAND_IPV6,
                     ctx->server,
                     ctx->port,
                     ctx->secret);
            break;
    }

    LOGD("checking health of %s:%d", ctx->server, ctx->port);
    LOGT("triggering healthcheck command \"%s\"", command);
    freeaddrinfo(res);

    execsh_async_init(&ctx->radclient_execsh, radclient_execsh_fn);
    execsh_async_start(&ctx->radclient_execsh, command);
}

/*****************************************************************************/

void sm_healthcheck_update_server(
    sm_healthcheck_ctx_p        ctx,
    uint32_t                    timeout,
    const char                 *server,
    const char                 *secret,
    uint16_t                    port,
    bool                        healthy)
{
    STRSCPY_WARN(ctx->server, server);
    STRSCPY_WARN(ctx->secret, secret);
    ctx->interval = timeout;
    ctx->port = port;
    ctx->last_healthy = healthy;
    ctx->use_cache = false;
}

sm_healthcheck_ctx_p sm_healthcheck_new_server(
    uint32_t                    timeout,
    char                       *server,
    char                       *secret,
    uint16_t                    port,
    bool                        healthy)
{
    sm_healthcheck_ctx_p newctx = NULL;

    newctx = CALLOC(1, sizeof(*newctx));
    sm_healthcheck_update_server(newctx, timeout, server, secret, port, healthy);
    ev_init(&newctx->schedule_timer, trigger_healthcheck_cb);
    newctx->schedule_timer.data = newctx;

    return newctx;
}

/*****************************************************************************/

void sm_healthcheck_schedule_init(sm_healthcheck_update_cb_t update_cb)
{
    g_update_ovsdb_cb = update_cb;
    return;
}

void sm_healthcheck_schedule_update(
        uint32_t                    timeout,
        char                       *server,
        char                       *secret,
        uint16_t                    port,
        bool                        healthy)
{
    sm_healthcheck_ctx_p ctx = sm_healthcheck_find_ctx(server, port);
    if (!ctx) {
        LOGI("Scheduling healthcheck of new server %s:%d", server, port);
        ctx = sm_healthcheck_new_server(timeout, server, secret, port, healthy);
        ds_list_insert_head(&g_sm_healthcheck_servers, ctx);
    } else {
        LOGI("Updating healthcheck of new server %s:%d", server, port);
        sm_healthcheck_schedule_stop(ctx);
        sm_healthcheck_update_server(ctx, timeout, server, secret, port, healthy);
    }
    if (ctx->interval > 0) {
        sm_healthcheck_schedule_start(ctx);
    }
    return;
}

void sm_healthcheck_set_health_cache(const char* ip, uint16_t port, bool healthy)
{
    sm_healthcheck_ctx_p ctx = sm_healthcheck_find_ctx(ip, port);

    if (!ctx)
        return;

    ctx->last_healthy = healthy;
}

void sm_healthcheck_remove(const char* ip, uint16_t port)
{
    sm_healthcheck_ctx_p ctx = sm_healthcheck_find_ctx(ip, port);
    sm_healthcheck_ctx_p ctx_curr;
    ds_list_iter_t iter;

    if (!ctx)
        return;

    sm_healthcheck_schedule_stop(ctx);
    ds_list_foreach_iter(&g_sm_healthcheck_servers, ctx_curr, iter) {
        if ((ctx_curr->port == port) && (strcmp(ctx_curr->server, ip) == 0)) {
            ds_list_iremove(&iter);
            FREE(ctx_curr);
            break;
        }
    }
}

void sm_healthcheck_stop_all(void)
{
    sm_healthcheck_ctx_p ctx_curr;
    ds_list_iter_t iter;

    ds_list_foreach_iter(&g_sm_healthcheck_servers, ctx_curr, iter) {
        sm_healthcheck_schedule_stop(ctx_curr);
    }
}
