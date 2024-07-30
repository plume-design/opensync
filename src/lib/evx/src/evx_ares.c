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

#include <evx.h>
#include <ares.h>
#include <inttypes.h>
#include "log.h"
#include "const.h"
#include "util.h"

#define MODULE_ID LOG_MODULE_ID_ARES

#define ARES_PROCESS_TIMEOUT 10

static void timeout_cb(EV_P_ ev_timer *t, int revents)
{
    struct evx_ares   *eares_p;
    struct timeval    tv = {0, 0};
    struct timeval    tv_poll = {0, 0};
    struct timeval    *tvp;
    fd_set            readers, writers;
    int               nfds, count;

    eares_p = container_of(t, struct evx_ares, tw);
    FD_ZERO(&readers);
    FD_ZERO(&writers);

    nfds = ares_fds(eares_p->ares.channel, &readers, &writers);
    if (nfds == 0) {
        LOGT("evx: ares[%s]: no nfds available", eares_p->server);
        evx_stop_ares(eares_p);
        return;
    }

    tvp = ares_timeout(eares_p->ares.channel, NULL, &tv);

    // ares_timeout() will return either the tv or maxtv which is NULL in
    // the above call. select() with timeout=NULL would block indefinitely,
    // but this timeout handler should not block here. It will be triggered
    // again in next ev timer interval so use a timeout of 0 to poll the
    // fds and process eventual ares timeouts
    // tv and tvp are used for log information only

    count = select(nfds, &readers, &writers, NULL, &tv_poll);

    LOGT("evx: ares[%s] timeout: count: %d tv: %"PRId64".%06ld %s",
         eares_p->server, count,
         (int64_t)tv.tv_sec, (long)tv.tv_usec,
         tvp ? "" : "tvp=NULL");

    // even if no fds are ready (count=0) we need to call ares_process()
    // to handle ares request timeouts

    if (eares_p->timeout_user_cb)
        eares_p->timeout_user_cb();

    ares_process(eares_p->ares.channel, &readers, &writers);
}

static void io_cb (EV_P_ ev_io *w, int revents)
{
    struct ares_ctx *ctx;
    ares_socket_t    rfd;
    ares_socket_t    wfd;

    ctx = (struct ares_ctx *) w;
    rfd = ARES_SOCKET_BAD;
    wfd = ARES_SOCKET_BAD;

    LOGT("evx: ares[%s] %s: fd: %d revents: %d", ctx->eares->server, __func__, w->fd, revents);

    if (revents & EV_READ)
        rfd = w->fd;

    if (revents & EV_WRITE)
        wfd = w->fd;

    ares_process_fd(ctx->eares->ares.channel, rfd, wfd);
    ev_timer_again(ctx->eares->loop, &ctx->eares->tw);
}

static struct ares_ctx* lookup(struct evx_ares *eares, int fd)
{
    int i;

    for (i = 0; i < (int) ARRAY_SIZE(eares->ctx); i++)
        if (eares->ctx[i].io.fd == fd)
            return eares->ctx + i;

    return NULL;
}

int evx_ares_get_count_busy_fds(struct evx_ares *eares)
{
    int count;
    int i;

    count = 0;

    for (i = 0; i < (int) ARRAY_SIZE(eares->ctx); i++)
        if (eares->ctx[i].io.fd != 0 &&
            eares->ctx[i].io.fd != ARES_SOCKET_BAD)
            count++;

    return count;
}

static struct ares_ctx* allocctx(struct evx_ares *eares, int fd)
{
    struct ares_ctx *ctx;
    int             i;

    for (i = 0; i <(int) ARRAY_SIZE(eares->ctx); i++) {
        if (eares->ctx[i].io.fd == ARES_SOCKET_BAD)
            break;

        if (eares->ctx[i].io.fd == 0)
            break;
    }

    if (i == ARRAY_SIZE(eares->ctx)) {
        LOGW("evx: ares[%s]: ctx out of memory", eares->server);
        return NULL;
    }

    ctx = eares->ctx + i;
    ctx->eares = eares;
    ev_io_init(&ctx->io, io_cb, fd, EV_READ);
    return ctx;
}

static void evx_ares_sock_state_cb(void *data, int s, int read, int write)
{
    struct ares_ctx  *ctx;
    struct evx_ares  *eares;
    ev_io            *io_p;

    eares = (struct evx_ares *) data;
    ctx = lookup(eares, s);

    if (!ctx)
       ctx = allocctx(eares, s);

    if (!ctx)
       return;

    io_p = &ctx->io;

    LOGT("evx: ares[%s]: read: %d write: %d s: %d fd: %d", eares->server, read, write, s, io_p->fd);

    if (ev_is_active(io_p) && io_p->fd != s) {
        LOGE("evx: ares[%s]: %s: different socket id", eares->server, __func__);
        return;
    }

    if (read || write) {
        ev_io_set(io_p, s, (read ? EV_READ : 0) | (write ? EV_WRITE : 0) );
        ev_io_start(eares->loop, io_p);
        ev_timer_again(eares->loop, &eares->tw);
    }
    else {
        ev_io_stop(eares->loop, io_p);
        ev_io_set(io_p, ARES_SOCKET_BAD, 0);
    }
}

int evx_init_ares(struct ev_loop * loop, struct evx_ares *eares_p, void (*timeout_user_cb)())
{
    int optmask;
    int status;

    memset(eares_p, 0,sizeof(*eares_p));

    /* In this place the good idea is used ares_library
     * initialized function.
     * This function was first introduced in c-ares version 1.11.0
     * It could be done if we switch all ares lib to version 1.11.0
     */
    status = ares_library_init(ARES_LIB_INIT_ALL);
    if (status != ARES_SUCCESS)
            return -1;

    optmask = ARES_OPT_SOCK_STATE_CB;
    eares_p->loop = loop;
    eares_p->ares.options.sock_state_cb_data = eares_p;
    eares_p->ares.options.sock_state_cb = evx_ares_sock_state_cb;
    eares_p->ares.options.flags =  optmask;

    ev_timer_init(&eares_p->tw, timeout_cb, 0, ARES_PROCESS_TIMEOUT);
    eares_p->chan_initialized = 0;
    eares_p->timeout_user_cb = timeout_user_cb;

    LOGI("evx: ares version: %s, max sockets = %d",
         ares_version(NULL), ARES_GETSOCK_MAXNUM);

    return 0;
}

int evx_ares_set_server(struct evx_ares *eares_p, char *server)
{
    if (eares_p->chan_initialized) {
        // set_server should be called between init and start
        LOGE("%s(%s): channel already started", __func__, server);
        return -1;
    }
    if (strscpy(eares_p->server, server, sizeof(eares_p->server)) < 0) {
        *eares_p->server = 0;
        LOGE("%s(%s): too long", __func__, server);
        return -1;
    }
    return 0;
}

int evx_start_ares(struct evx_ares *eares_p)
{
    int status;

    if (!eares_p->chan_initialized) {
        status = ares_init_options(&eares_p->ares.channel,
                                   &eares_p->ares.options,
                                   ARES_OPT_SOCK_STATE_CB);
        if (status != ARES_SUCCESS) {
            LOGW("%s ares[%s] failed = %d", __func__, eares_p->server, status);
            return -1;
        }
        if (*eares_p->server) {
            status = ares_set_servers_csv(eares_p->ares.channel, eares_p->server);
            if (status != ARES_SUCCESS) {
                LOGW("%s ares[%s] set server failed = %d", __func__, eares_p->server, status);
                return -1;
            }
        }
    }
    eares_p->chan_initialized = 1;
    return 0;
}

void evx_stop_ares(struct evx_ares *eares_p)
{
    LOGT("evx: ares[%s]: stop ares, channel state: %d",
         eares_p->server, eares_p->chan_initialized);

    if (eares_p->chan_initialized)
        ares_destroy(eares_p->ares.channel);

    eares_p->chan_initialized = 0;
    ev_timer_stop(eares_p->loop, &eares_p->tw);
}

void evx_close_ares(struct evx_ares *eares_p)
{
    evx_stop_ares(eares_p);
    ares_library_cleanup();
}
