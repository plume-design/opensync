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
#include <zmq.h>

#include "imc.h"
#include "log.h"


static void
s_idle_cb(struct ev_loop *loop, ev_idle *w, int revents)
{
}


static void
s_io_cb(struct ev_loop *loop, ev_io *w, int revents)
{
}


static int
s_get_revents(void *zsock, int events)
{
    int zmq_events;
    size_t optlen;
    int revents;
    int rc;

    revents = 0;
    optlen = sizeof(zmq_events);
    rc = zmq_getsockopt(zsock, ZMQ_EVENTS, &zmq_events, &optlen);

    if (rc == -1)
    {
        rc = errno;
        LOGE("%s: zmq_getsockopt failed: %s", __func__,
             strerror(rc));
        /* on error, make callback get called */
        return events;
    }

    if (zmq_events & ZMQ_POLLOUT) revents |= events & EV_WRITE;
    if (zmq_events & ZMQ_POLLIN) revents |= events & EV_READ;

    return revents;
}


static void
s_prepare_cb(struct ev_loop *loop, ev_prepare *w, int revents)
{
    struct imc_context *context;
    size_t offset;

    offset = offsetof(struct imc_context, w_prepare);
    context = (struct imc_context *)(((char *)w) - offset);

    revents = s_get_revents(context->zsock, context->events);
    if (revents)
    {
        /* idle ensures that libev will not block */
        ev_idle_start(loop, &context->w_idle);
    }
}


static void
s_check_cb(struct ev_loop *loop, ev_check *w, int revents)
{
    struct imc_context *context;
    size_t offset;

    offset = offsetof(struct imc_context, w_check);
    context = (struct imc_context *)(((char *)w) - offset);

    ev_idle_stop(loop, &context->w_idle);

    revents = s_get_revents(context->zsock, context->events);
    if (revents)
    {
        context->imc_ev_cb(loop, context, revents);
    }
}


/**
 * @brief initiates the ev context of an imc socket
 *
 * @param context the imc context
 */
int
ev_imc_init(struct imc_context *context)
{
    ev_prepare *pw_prepare;
    zmq_pollitem_t item;
    ev_check *pw_check;
    ev_idle *pw_idle;
    size_t optlen;
    ev_io *pw_io;
    int fd;
    int rc;

    pw_prepare = &context->w_prepare;
    ev_prepare_init(pw_prepare, s_prepare_cb);

    pw_check = &context->w_check;
    ev_check_init(pw_check, s_check_cb);

    pw_idle = &context->w_idle;
    ev_idle_init(pw_idle, s_idle_cb);

    optlen = sizeof(item.fd);
    rc = zmq_getsockopt(context->zsock, ZMQ_FD, &item.fd, &optlen);
    if (rc == -1)
    {
        rc = errno;
        LOGE("%s: zmq_getsockopt failed: %s", __func__,
             strerror(rc));

        return -1;
    }

    fd = item.fd;

    pw_io = &context->w_io;
    ev_io_init(pw_io, s_io_cb, fd, context->events ? EV_READ : 0);

    return 0;
}


/**
 * @brief starts the ev context of an imc socket
 *
 * @param context the imc context
 */
void
ev_imc_start(struct imc_context *context)
{
    ev_prepare_start(context->loop, &context->w_prepare);
    ev_check_start(context->loop, &context->w_check);
    ev_io_start(context->loop, &context->w_io);
}

/**
 * @brief ev callback for data reception
 *
 * This callback is registered to the ev framework and triggered
 * when data is received on on the zmq socket.
 * The ev callback then calls the user provided receive routine.
 *
 * Memory management:
 * The receive buffer is allocated by the zmq framework.
 * The receive buffer is freed upon the call to zmq_msg_close()
 * at the end of the function.
 */
static void
imc_ev_recv_cb(struct ev_loop *loop, struct imc_context *context, int revents)
{
    zmq_msg_t msg;
    int rc;

    rc = zmq_msg_init(&msg);
    if (rc == -1)
    {
        LOGE("%s: failed to initialize msg", __func__);
        return;
    }

    rc = zmq_msg_recv(&msg, context->zsock, 0);
    if (rc == -1)
    {
        LOGE("%s: failed to receive data: %s", __func__,
             strerror(errno));
        goto err_recv;
    }

    /* Call the user receive routine */
    context->recv_fn(zmq_msg_data(&msg), zmq_msg_size(&msg));

err_recv:
    zmq_msg_close(&msg);
}


/**
 * @brief initializes an imc context
 *
 * @param context the imc context
 */
void
imc_init_context(struct imc_context *context)
{
    ds_list_init(&context->options, struct imc_sockoption_node, option_node);
}


/**
 * @brief resets an imc context
 *
 * @param context the imc context
 * @param frees non network resources of the imc context
 */
void
imc_reset_context(struct imc_context *context)
{
    struct imc_sockoption_node *node;

    ds_list_t *list;

    list = &context->options;
    while (!ds_list_is_empty(list))
    {
        node = ds_list_remove_head(list);
        free(node->option.value);
        free(node);
    }
}


/**
 * @brief add a socket to the imc context
 * @param context the imc context
 * @param the option container
 *
 * Memory usage: The option container is duplicated and
 * the duplicate is stored in the context.
 * This routine ought to be called brior to calling either
 * @see imc_init_server or @see imc_init_client for the options to take effect.
 * Call the routine for each option you wish to set.
 */

int
imc_add_sockopt(struct imc_context *context, struct imc_sockoption *option)
{
    struct imc_sockoption_node *node;
    struct imc_sockoption *node_option;

    /* Safety checks */
    if (option == NULL) return -1;
    if (option->len == 0) return -1;
    if (option->value == NULL) return -1;

    node = calloc(1, sizeof(*node));
    if (node == NULL) return -1;

    node_option = &node->option;

    node_option->option_name = option->option_name;
    node_option->value = calloc(1, option->len);
    if (node_option == NULL) goto err_opt;
    node_option->len = option->len;
    memcpy(node_option->value, option->value, node_option->len);

    ds_list_insert_tail(&context->options, node_option);

    return 0;

err_opt:
    free(node);
    return -1;
}


/**
 * @brief apply options
 */
static int
imc_apply_options(struct imc_context *context)
{
    struct imc_sockoption_node *node;
    int rc;

    ds_list_foreach(&context->options, node)
    {
        rc = zmq_setsockopt(context->zsock, node->option.option_name,
                             node->option.value, node->option.len);
        if (rc)
        {
            LOGE("%s: zmq_setsockopt %d failed: %s", __func__,
                 node->option.option_name,
                 strerror(errno));
            return -1;
        }
    }

    return 0;
}


/**
 * @brief initiates a imc server
 *
 * @param server the server context
 * @param loop the ev loop
 * @param recv_cb user provided data processing routine
 */
int
imc_init_server(struct imc_context *server, struct ev_loop *loop,
                imc_recv recv_cb)
{
    void *zsock;
    void *zctx;
    int rc;

    server->recv_fn = recv_cb;
    server->imc_ev_cb = imc_ev_recv_cb;

    /* Allocate a zmq context */
    zctx = zmq_ctx_new();
    if (zctx == NULL)
    {
        rc = errno;
        LOGE("%s: failed to create a zmq context: %s", __func__,
             strerror(rc));

        return -1;
    }

    server->zctx = zctx;

    /* Allocate a zmq socket */
    zsock = zmq_socket(zctx, server->ztype);
    if (zsock == NULL)
    {
        rc = errno;
        LOGE("%s: failed to create a zmq socket: %s", __func__,
             strerror(rc));

        goto err_free_zctx;
    }

    server->zsock = zsock;

    /* Apply options */
    rc = imc_apply_options(server);
    if (rc) goto err_free_zsock;

    rc = zmq_bind(server->zsock, server->endpoint);
    if (rc == -1)
    {
        rc = errno;
        LOGE("%s: failed to bind the zmq socket to %s: %s", __func__,
             server->endpoint, strerror(rc));
        goto err_free_zsock;
    }

    server->events = EV_READ;
    server->loop = loop;
    rc = ev_imc_init(server);
    if (rc != 0) goto err_free_zsock;

    server->initialized = true;
    ev_imc_start(server);

    return 0;

err_free_zsock:
    zmq_close(server->zsock);

err_free_zctx:
    zmq_ctx_shutdown(server->zctx);
    zmq_ctx_term(server->zctx);
    server->zsock = NULL;
    server->zctx = NULL;

    return -1;
}


/**
 * @brief terminates a imc server and frees its resources
 *
 * @param server the server to terminate
 */
void
imc_terminate_server(struct imc_context *server)
{
    ev_prepare_stop(server->loop, &server->w_prepare);
    ev_check_stop(server->loop, &server->w_check);
    ev_idle_stop(server->loop, &server->w_idle);
    ev_io_stop(server->loop, &server->w_io);

    zmq_close(server->zsock);
    zmq_ctx_shutdown(server->zctx);
    zmq_ctx_term(server->zctx);
    server->zsock = NULL;
    server->zctx = NULL;
    server->initialized = false;
    return;
}


/**
 * @brief initiates a imc client and connects to a server
 *
 * @param endpoint the client end point
 * @param free_snd_msg callback routine freeing the transmitted message
 * @param free_msg_hint argument passed to the free_snd_msg() callback
 *
 * Please refer to zmq_msg_send() documentation for further details.
 */
int
imc_init_client(struct imc_context *client, imc_free_sndmsg free_snd_msg,
                void *free_msg_hint)
{
    void *zsock;
    void *zctx;
    int rc;

    /* Allocate a zmq context */
    zctx = zmq_ctx_new();
    if (zctx == NULL)
    {
        rc = errno;
        LOGE("%s: failed to create a zmq context: %s", __func__,
             strerror(rc));

        return -1;
    }

    client->zctx = zctx;

    /* Allocate a zmq socket */
    zsock = zmq_socket(zctx, client->ztype);
    if (zsock == NULL)
    {
        rc = errno;
        LOGE("%s: failed to create a zmq socket: %s", __func__,
             strerror(rc));

        goto err_free_zctx;
    }

    client->zsock = zsock;

    /* Apply options */
    rc = imc_apply_options(client);
    if (rc) goto err_free_zsock;

    /* Connect the socket to the endpoint */
    rc = zmq_connect(client->zsock, client->endpoint);
    if (rc == -1)
    {
        rc = errno;
        LOGE("%s: failed to connect the zmq socket to %s: %s", __func__,
             client->endpoint, strerror(rc));
        goto err_free_zsock;
    }

    client->imc_free_sndmsg = free_snd_msg;
    client->free_msg_hint = free_msg_hint;
    client->initialized = true;

    return 0;

err_free_zsock:
    zmq_close(client->zsock);

err_free_zctx:
    zmq_ctx_term(client->zctx);
    client->zsock = NULL;
    client->zctx = NULL;

    return -1;
}


/**
 * @brief terminates a imc client
 *
 * @param loop the ev loop
 */
void
imc_terminate_client(struct imc_context *client)
{
    if (client->zctx == NULL) return;

    zmq_close(client->zsock);
    zmq_ctx_term(client->zctx);
    client->zsock = NULL;
    client->zctx = NULL;
    client->initialized = false;

    return;
}


/**
 * @brief send data to a imc server
 *
 * The data to be sent must be dynamically allocated as opposed to be
 * coming from the stack, as the zmq_msg_send() might have just queued
 * the message for transmission (see man page)
 *
 * @param context the socket context
 * @param buf the buffer to send
 * @param len the buffer size
 * @flags the transmit flags
 */
int
imc_send(struct imc_context *client, void *buf, size_t buflen, int flags)
{
    zmq_msg_t msg;
    int rc;

    zmq_msg_init_data(&msg, buf, buflen, client->imc_free_sndmsg, NULL);
    rc = zmq_msg_send(&msg, client->zsock, flags);
    if (rc == -1)
    {
        rc = errno;
        LOGD("%s: failed to send data to %s: %s", __func__,
             client->endpoint, strerror(rc));
        zmq_msg_close(&msg);
        return -1;
    }

    return 0;
}

void
imc_init_dso(struct imc_dso *dso)
{
    dso->init_client = imc_init_client;
    dso->terminate_client = imc_terminate_client;
    dso->client_send = imc_send;
    dso->add_sockopt = imc_add_sockopt;
    dso->init_server = imc_init_server;
    dso->terminate_server = imc_terminate_server;
    dso->add_sockopt = imc_add_sockopt;
    dso->init_context = imc_init_context;
    dso->reset_context = imc_reset_context;
}

