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

#ifndef _GNU_SOURCE /* NOLINT(*-reserved-identifier) */
#define _GNU_SOURCE /* for memmem() in string.h */
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h> /*< AF_UNIX, SOCK_STREAM, socket, connect */
#include <sys/un.h>     /*< struct sockaddr_un */
#include <unistd.h>     /*< close */

#include "const.h"
#include "log.h"
#include "otbr_socket.h"
#include "otbr_timeout.h"

/** Set to 1 to enable trace prints for socket data sent, received and discarded */
#define OTBR_SOCKET_DEBUG_DATA 0

/** Set to 1 to enable additional checks for determining whether the socket is open */
#define OTBR_SOCKET_ADVANCED_OPEN_CHECK 0

static inline int NONNULL(1) socket_fd_get(const otbr_socket_t *const ctx)
{
    return ctx->io.fd;
}

static inline void NONNULL(1) socket_fd_reset(otbr_socket_t *const ctx)
{
    ctx->io.fd = -1;
}

/**
 * Set socket to non-blocking mode
 *
 * @param[in] socket_fd  File descriptor of the socket for which to enable non-blocking mode.
 *
 * @return true on success. Failure is logged internally.
 */
static bool socket_set_non_blocking(const int socket_fd)
{
    const int flags = fcntl(socket_fd, F_GETFL, 0);

    if (flags == -1)
    {
        LOGE("Socket fcntl get failed (%s)", strerror(errno));
        return false;
    }
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        LOGE("Socket fcntl set failed (%s)", strerror(errno));
        return false;
    }

    return true;
}

/**
 * Set socket receive and transmit buffer size
 *
 * @param[in] socket_fd  File descriptor of the socket for which to set the buffer sizes.
 * @param[in] size       Buffer size to set.
 *
 * @return true on success. Failure is logged internally.
 */
static bool socket_set_buffer_size(const int socket_fd, const int size)
{
    const int options[2] = {SO_RCVBUF, SO_SNDBUF};

    for (size_t i = 0; i < ARRAY_SIZE(options); i++)
    {
        if (setsockopt(socket_fd, SOL_SOCKET, options[i], &size, sizeof(size)) != 0)
        {
            LOGE("Socket option set (%d, %d) failed (%s)", options[i], size, strerror(errno));
            return false;
        }
    }

    return true;
}

static void NONNULL(1, 2) ev_io_socket_callback(struct ev_loop *const loop, ev_io *const w, const int r_events)
{
    otbr_socket_t *const ctx = w->data;
    (void)loop;

    /* Nothing to do if the socket is not readable (without errors) */
    if (r_events & EV_ERROR)
    {
        LOGE("OTBR socket IO: read error");
        return;
    }
    if (!(r_events & EV_READ))
    {
        LOGE("OTBR socket IO: not readable");
        return;
    }

    if (ctx->response.len >= sizeof(ctx->response.buffer))
    {
        LOGE("OTBR socket IO: response buffer overflow (%u B)", ctx->response.len);
        return;
    }

    /* Append all (possible) new data from the socket */
    otbr_socket_read_rsp(ctx);
}

bool otbr_socket_open(
        otbr_socket_t *const ctx,
        struct ev_loop *const loop,
        const char *const thread_iface,
        const float timeout,
        const float interval)
{
    struct sockaddr_un sock_addr = {.sun_family = AF_UNIX};
    int socket_fd;
    bool success;

    if (otbr_socket_is_open(ctx))
    {
        LOGE("OTBR socket already opened, closing first");
        otbr_socket_close(ctx);
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->loop = loop;

    if (snprintf(
                sock_addr.sun_path,
                sizeof(sock_addr.sun_path),
                CONFIG_OTBR_CLI_AGENT_DAEMON_SOCKET_NAME_TEMPLATE,
                thread_iface)
        >= (int)sizeof(sock_addr.sun_path))
    {
        LOGE("OTBR socket name '%s'(%s) too long", CONFIG_OTBR_CLI_AGENT_DAEMON_SOCKET_NAME_TEMPLATE, thread_iface);
        return false;
    }

    /* Create a new socket for communication with the OTBR agent daemon */
    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        LOGE("OTBR socket creation failed (%s)", strerror(errno));
        return false;
    }
    if (!(socket_set_non_blocking(socket_fd) && socket_set_buffer_size(socket_fd, OTBR_CLI_LINE_BUFFER_SIZE)))
    {
        close(socket_fd);
        return false;
    }

    /* Common timeout timer is started on-demand */
    otbr_timeout_init(loop, &ctx->timer);

    otbr_timeout_start(&ctx->timer, timeout);
    success = false;
    while (true)
    {
        if (connect(socket_fd, (const struct sockaddr *)&sock_addr, sizeof(sock_addr)) == 0)
        {
            success = true;
            break;
        }

        /* Only "Connection refused" and "No such file or directory" are expected errors, and only while waiting */
        if ((timeout == 0) || ((errno != ECONNREFUSED) && (errno != ENOENT)))
        {
            LOGE("OTBR socket failed to connect to '%s' (%s)", sock_addr.sun_path, strerror(errno));
            break;
        }

        if (!otbr_timeout_tick(&ctx->timer, false, NULL))
        {
            LOGE("OTBR socket failed to connect to '%s' in %.3f s", sock_addr.sun_path, timeout);
            break;
        }

        if (interval > 0)
        {
            otbr_timeout_sleep(&ctx->timer, interval);
        }
    }
    otbr_timeout_stop(&ctx->timer);

    if (!success)
    {
        close(socket_fd);
        return false;
    }

    ev_io_init(&ctx->io, ev_io_socket_callback, socket_fd, EV_READ);
    ctx->io.data = ctx;
    /* IO watcher is started in otbr_socket_reader_activate() or otbr_socket_readline() */

    LOGD("OTBR socket connected to '%s'", sock_addr.sun_path);
    return true;
}

bool otbr_socket_is_open(const otbr_socket_t *const ctx)
{
    const int fd = socket_fd_get(ctx);
#if OTBR_SOCKET_ADVANCED_OPEN_CHECK
    int err = 0;
    socklen_t len = sizeof(err);
#endif /* OTBR_SOCKET_ADVANCED_OPEN_CHECK */

    if ((ctx->loop == NULL) || (fd < 0))
    {
        return false;
    }

#if OTBR_SOCKET_ADVANCED_OPEN_CHECK
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0)
    {
        LOGW("OTBR socket %d get opt failed (%s)", fd, strerror(errno));
        return false;
    }
    if (err != 0)
    {
        LOGT("OTBR socket %d error (%s)", fd, strerror(err));
        return false;
    }
#endif /* OTBR_SOCKET_ADVANCED_OPEN_CHECK */

    return true;
}

bool otbr_socket_write_cmd(otbr_socket_t *const ctx)
{
    const char *buffer = ctx->command.buffer;
    const char *const org_buffer = buffer;
    const size_t org_len = ctx->command.len;

    while (ctx->command.len > 0)
    {
        const ssize_t written = write(socket_fd_get(ctx), buffer, ctx->command.len);

        if (written >= 0)
        {
            ctx->command.len -= written;
            buffer += written;
        }
        else if (errno != EINTR)
        {
            break;
        }
    }

    if (ctx->command.len > 0)
    {
        LOGE("OTBR socket failed to write '%.*s' (%d of %d left, %s)",
             org_len,
             org_buffer,
             ctx->command.len,
             org_len,
             strerror(errno));
        return false;
    }

#if OTBR_SOCKET_DEBUG_DATA
    LOGT("OTBR socket wrote %d: '%.*s'", org_len, org_len, org_buffer);
#endif
    return true;
}

bool otbr_socket_read_rsp(otbr_socket_t *const ctx)
{
#if DEBUG_SOCKET_DATA
    const size_t org_len = ctx->response.len;
#endif
    ssize_t num_read;

    if (ctx->response.len >= sizeof(ctx->response.buffer))
    {
        LOGE("OTBR socket response buffer overflow (%u B)", ctx->response.len);
        return false;
    }

    /* Read all data from the socket to the end of the buffer (replace null-terminator) */
    num_read =
            recv(socket_fd_get(ctx),
                 ctx->response.buffer + ctx->response.len,
                 sizeof(ctx->response.buffer) - ctx->response.len - 1,
                 0);

    if (num_read < 0)
    {
        ctx->response.last_read_ok = ((errno == EAGAIN) || (errno == EWOULDBLOCK));
        num_read = 0;
    }
    else
    {
        ctx->response.last_read_ok = true;
    }
    ctx->response.len += num_read;
    ctx->response.buffer[ctx->response.len] = '\0';

    if (!ctx->response.last_read_ok)
    {
        LOGE("OTBR socket read failed (%s)", strerror(errno));
    }
#if OTBR_SOCKET_DEBUG_DATA
    else
    {
        LOGT("OTBR socket read %d+%d B: '%s'", org_len, num_read, ctx->response.buffer);
    }
#endif /* OTBR_SOCKET_DEBUG_DATA */

    return ctx->response.last_read_ok;
}

bool socket_discard(otbr_socket_t *const ctx)
{
    ASSERT(ctx->response.len <= sizeof(ctx->response.buffer), "Invalid response data count");

    do
    {
        /* There might be some data left from the previous read, so log first, then read new */
#if OTBR_SOCKET_DEBUG_DATA
        if (ctx->response.len > 0)
        {
            LOGT("Discarding %d B of pending data: '%.*s'", ctx->response.len, ctx->response.len, ctx->response.buffer);
            ctx->response.len = 0;
        }
#else
        ctx->response.len = 0;
#endif /* OTBR_SOCKET_DEBUG_DATA */

        if (!otbr_socket_read_rsp(ctx))
        {
            return false;
        }
    } while (ctx->response.len > 0);

    return true;
}

bool otbr_socket_reader_activate(otbr_socket_t *const ctx, const bool activate)
{
    if (activate && !ev_is_active(&ctx->io))
    {
        ev_io_start(ctx->loop, &ctx->io);
        return true;
    }
    else if (!activate && ev_is_active(&ctx->io))
    {
        ev_io_stop(ctx->loop, &ctx->io);
        return true;
    }
    return false;
}

bool otbr_socket_is_reader_active(const otbr_socket_t *const ctx)
{
    return ev_is_active(&ctx->io);
}

ssize_t otbr_socket_readline(otbr_socket_t *const ctx, char *const buffer, const size_t buffer_size)
{
    const bool ev_io_started = otbr_socket_reader_activate(ctx, true);
    bool time_remaining;
    ssize_t ret;
    size_t len;

    time_remaining = true;
    ret = -1;
    len = 0;
    while (ret < 0)
    {
        const char *end;
        size_t line_len;

        /* Data can be received in the same loop iteration as the timer runs out,
         * so do not check for the timeout on the first iteration. */
        if (!time_remaining)
        {
            LOGE("OTBR socket readline timeout");
            break;
        }
        time_remaining = otbr_timeout_tick(&ctx->timer, false, NULL);

        if (!ctx->response.last_read_ok)
        {
            /* Socket failure */
            break;
        }
        if (len == ctx->response.len)
        {
            /* Nothing to do, waiting for received data or timeout */
            continue;
        }
        len = ctx->response.len;

        /* Only new data could be searched (haystack `buffer + len - last_read` with length `last_read`),
         * but that is unreliable if the data is fragmented (e.g. "\r" and "\n" split in two separate reads).
         * As the responses are generally short, always checking the complete response is not a performance concern. */
        end =
                memmem(ctx->response.buffer,
                       ctx->response.len,
                       OTBR_CLI_RESPONSE_END_LINE,
                       STRLEN(OTBR_CLI_RESPONSE_END_LINE));
        if (end == NULL)
        {
            continue;
        }

        /* Line delimiter is not copied to the user buffer */
        line_len = end - ctx->response.buffer;
        if (line_len >= buffer_size)
        {
            LOGE("OTBR socket response line too long (%u >= %u)", line_len, buffer_size);
            break;
        }
        memcpy(buffer, ctx->response.buffer, end - ctx->response.buffer);
        buffer[line_len] = '\0';
        ret = (ssize_t)line_len;

        /* More data might have been read from the socket, requested in the next call to this function */
        line_len += STRLEN(OTBR_CLI_RESPONSE_END_LINE);
        memmove(ctx->response.buffer, ctx->response.buffer + line_len, ctx->response.len - line_len);
        ctx->response.len -= line_len;

        LOGT("OTBR socket received %d B line (%d left): '%s'", ret, ctx->response.len, buffer);
    }

    if (ev_io_started)
    {
        otbr_socket_reader_activate(ctx, false);
    }

    return ret;
}

void otbr_socket_close(otbr_socket_t *const ctx)
{
    if (ev_is_active(&ctx->io))
    {
        ev_io_stop(ctx->loop, &ctx->io);
    }
    if (socket_fd_get(ctx) >= 0)
    {
        close(socket_fd_get(ctx));
        socket_fd_reset(ctx);
    }
    otbr_timeout_stop(&ctx->timer);

    ctx->io.data = NULL;
    ctx->loop = NULL;

    LOGD("OTBR socket closed");
}
