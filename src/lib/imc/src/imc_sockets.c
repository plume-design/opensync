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

#include <arpa/inet.h>
#include <ev.h>
#include <errno.h>

#include "imc_sockets.h"
#include "log.h"
#include "memutil.h"
#include "util.h"

#define MAX_BUFFER_SIZE 65536


static struct unix_context g_imc_socket_client =
{
    .endpoint = "/tmp/unix_fsm2fcm",
};

static struct unix_context g_imc_socket_server =
{
    .endpoint = "/tmp/unix_fsm2fcm",
};

struct unix_context * imc_get_socket_client(void)
{
    return &g_imc_socket_client;
}

struct unix_context * imc_get_socket_server(void)
{
    return &g_imc_socket_server;
}

void
imc_socket_config_client_endpoint(struct imc_dso *imc, char *endpoint)
{
    struct unix_context *client;

    imc->imc_socket = imc_get_socket_client();
    client = imc->imc_socket;

    client->endpoint = endpoint;
}

static void
unix_ev_recv_cb(EV_P_ ev_io *ev, int revents)
{
    (void)loop;
    (void)revents;

    struct sockaddr_un server;
    struct unix_context *context;
    socklen_t serv_len;
    char buf[MAX_BUFFER_SIZE];
    int rc;

    context = ev->data;
    serv_len = sizeof(server);
    rc = recvfrom(context->sock_fd, &buf, sizeof(buf), 0, (struct sockaddr *)&server, &serv_len);
    if (rc == -1)
    {
        LOGE("%s: failed to receive data: %s", __func__,
             strerror(errno));
        return;
    }

    /* Call the user receive routine */
    context->recv_fn(buf, rc);
}

/**
 * @brief initiates a unix server
 *
 * @param server the server context
 * @param loop the ev loop
 * @param recv_cb user provided data processing routine
 */
int
imc_socket_init_server(struct imc_dso *imc, struct ev_loop *loop,
                       unix_recv recv_cb)
{
    struct unix_context *unix_server;
    struct sockaddr_un server;
    int sock;

    imc->imc_socket = imc_get_socket_server();
    unix_server = imc->imc_socket;
    unix_server->recv_fn = recv_cb;

    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        LOGE("%s: failed opening a unix socket", __func__);
        return -1;
    }

    unix_server->sock_fd = sock;

    server.sun_family = AF_UNIX;
    STRSCPY(server.sun_path, unix_server->endpoint);

    /* unlink socket */
    unlink(unix_server->endpoint);

    if (bind(sock, (struct sockaddr *) &server, sizeof(struct sockaddr_un)))
    {
        LOGE("%s: failed binding unix socket", __func__);
        goto sock_err;
    }

    /* set socket to non blocking mode */
    fcntl(sock, F_SETFL, O_NONBLOCK);

    unix_server->events = EV_READ;
    unix_server->loop = loop;

    ev_io_init(&unix_server->w_io, unix_ev_recv_cb,
               unix_server->sock_fd, EV_READ);

    unix_server->w_io.data = (void *)unix_server;

    ev_io_start(loop, &unix_server->w_io);

    imc->initialized = true;

    return 0;

sock_err:
    close(sock);
    unlink(unix_server->endpoint);
    return -1;
}

/**
 * @brief terminates a unix server and frees its resources
 *
 * @param server the server to terminate
 */
void
imc_socket_terminate_server(struct imc_dso *imc)
{
    struct unix_context *server;

    server = imc->imc_socket;
    if (ev_is_active(&server->w_io))
    {
        ev_io_stop(server->loop, &server->w_io);
    }

    close(server->sock_fd);
    unlink(server->endpoint);
    imc->initialized = false;
    return;
}

/**
 * @brief initiates a unix client and connects to a server
 *
 * @param client the client context
 *
 */
int
imc_socket_init_client(struct imc_dso *imc, imc_free_sndmsg free_snd_msg,
                  void *free_msg_hint)
{
    struct unix_context *client;
    struct sockaddr_un server;
    int sock;

    imc->imc_socket = imc_get_socket_client();
    client = imc->imc_socket;

    sock = socket(AF_UNIX, SOCK_DGRAM, 0);

    if (sock < 0)
    {
        LOGE("%s: failed unix socket creation", __func__);
        return -1;
    }

    client->sock_fd = sock;
    server.sun_family = AF_UNIX;
    STRSCPY(server.sun_path, client->endpoint);

    imc->imc_free_sndmsg = free_snd_msg;
    imc->initialized = true;

    return 0;
}

/**
 * @brief terminates a unix client
 *
 * @param client the client context
 */
void
imc_socket_terminate_client(struct imc_dso *imc)
{
    struct unix_context *client;

    client = imc->imc_socket;
    if (imc->initialized == false) return;

    close(client->sock_fd);
    imc->initialized = false;

    return;
}

/**
 * @brief send data to a unix server
 *
 * @param client the socket context
 * @param buf the buffer to send
 * @param len the buffer size
 * @flags the transmit flags
 */
int
imc_socket_send(struct imc_dso *imc, void *buf, size_t buflen, int flags)
{
    struct unix_context *client;
    struct sockaddr_un server;
    socklen_t serv_len = sizeof(server);
    int rc;

    client = imc->imc_socket;
    server.sun_family = AF_UNIX;
    STRSCPY(server.sun_path, client->endpoint);

    rc = sendto(client->sock_fd, buf, buflen, MSG_DONTWAIT,
                (const struct sockaddr *)&server, serv_len);

    imc->imc_free_sndmsg(buf, NULL);

    if (rc == -1)
    {
        rc = errno;
        LOGD("%s: failed to send data to %s: %s", __func__,
             client->endpoint, strerror(rc));
        return -1;
    }

    return 0;
}
