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

#ifndef IMC_H_INCLUDED
#define IMC_H_INCLUDED

#include <ev.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "ds_list.h"

/**
 * Derived from https://github.com/pijyoi/zmq_libev,
 * distributed under a MIT license
 */

/**
 * @brief Receive callback provided by the manager
 *
 */
typedef void (*imc_recv)(void *, size_t);

struct imc_context;
struct imc_dso;
struct imc_sockoption;

typedef void (*imc_ev_cbfn)(struct ev_loop *, struct imc_context *, int);

typedef void (*imc_free_sndmsg)(void *, void *);

typedef int (*init_client)(struct imc_context *, imc_free_sndmsg, void *);
typedef void (*terminate_client)(struct imc_context *);
typedef int (*client_send)(struct imc_context *, void *, size_t , int);

typedef int (*init_server)(struct imc_context *, struct ev_loop *, imc_recv);
typedef void (*terminate_server)(struct imc_context *);

typedef int  (*add_sockopt)(struct imc_context *, struct imc_sockoption *);
typedef void (*init_context)(struct imc_context *);
typedef void (*reset_context)(struct imc_context *);

typedef void (*init)(struct imc_dso *);

struct imc_dso
{
    void *handle;

    init init;

    init_client init_client;
    terminate_client terminate_client;
    client_send client_send;

    init_server init_server;
    terminate_server terminate_server;

    init_context init_context;
    reset_context reset_context;
    add_sockopt add_sockopt;
};


struct imc_sockoption
{
    int option_name;
    void *value;
    size_t len;
};


struct imc_sockoption_node
{
    struct imc_sockoption option;
    ds_list_node_t option_node;
};


struct imc_context
{
    struct ev_loop *loop;
    void *data;
    char *endpoint;
    imc_recv recv_fn;
    imc_ev_cbfn imc_ev_cb;
    imc_free_sndmsg imc_free_sndmsg;
    void *free_msg_hint;
    void *zctx;
    void *zsock;
    int ztype;
    int events;
    ev_prepare w_prepare;
    ev_check w_check;
    ev_idle w_idle;
    ev_io w_io;
    bool initialized;
    ds_list_t options;
    uint64_t io_success_cnt;
    uint64_t io_failure_cnt;
};


#if defined(CONFIG_TARGET_IMC)

#include <zmq.h>
enum
{
    IMC_PULL = ZMQ_PULL,
    IMC_PUSH = ZMQ_PUSH,
    IMC_DONTWAIT = ZMQ_DONTWAIT,
    IMC_SNDHWM = ZMQ_SNDHWM,
    IMC_RCVHWM = ZMQ_RCVHWM,
    IMC_LINGER = ZMQ_LINGER,
};


/**
 * @brief initializes an imc context
 *
 * @param context the imc context
 */
void
imc_init_context(struct imc_context *context);


/**
 * @brief resets an imc context
 *
 * @param context the imc context
 * @param frees non network resources of the imc context
 */
void
imc_reset_context(struct imc_context *context);


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
imc_add_sockopt(struct imc_context *context, struct imc_sockoption *option);


/**
 * @brief initiates a imc server
 *
 * @param server the server context
 * @param loop the ev loop
 * @param recv_cb user provided data processing routine
 */
int
imc_init_server(struct imc_context *server, struct ev_loop *loop,
                imc_recv recv_cb);


/**
 * @brief terminates a imc server
 *
 * @param server the server to terminate
 */
void
imc_terminate_server(struct imc_context *server);


/**
 * @brief initiates a imc client and connects to a server
 *
 * @param endpoint the client context
 */
int
imc_init_client(struct imc_context *client, imc_free_sndmsg free_msg,
                void *free_msg_hint);


/**
 * @brief initiates a imc client and connects to a server
 *
 * @param endpoint the client end point
 * @param free_snd_msg callback routine freeing the transmitted message
 * @param free_msg_hint argument passed to the free_snd_msg() callback
 *
 * Please refer to zmq_msg_send() documentation for further details.
 */
void
imc_terminate_client(struct imc_context *client);


/**
 * @brief send data to a imc server
 *
 * @param context the socket context
 * @param buf the buffer to send
 * @param len the buffer size
 * @flags the transmit flags
 */
int
imc_send(struct imc_context *context, void *buf, size_t buflen, int flags);


void
imc_init_dso(struct imc_dso *dso);

#else /* CONFIG_TARGET_IMC */

enum
{
    IMC_PULL = 0,
    IMC_PUSH,
    IMC_DONTWAIT,
    IMC_SNDHWM,
    IMC_RCVHWM,
    IMC_LINGER,
};


static inline int
imc_init_context(struct imc_context *context)
{
    return 0;
}


static inline int
imc_add_sockopt(struct imc_context *context, struct imc_sockoption *option)
{
    return 0;
}


static inline int
imc_init_server(struct imc_context *server, struct ev_loop *loop,
                imc_recv recv_cb)
{
    return 0;
}


static inline void
imc_terminate_server(struct imc_context *server) {}


static inline int
imc_init_client(struct imc_context *client, imc_free_sndmsg free_snd_msg,
                void *free_msg_int)
{
    return 0;
}


static inline void
imc_terminate_client(struct imc_context *client) {}


static inline int
imc_send(struct imc_context *context, void *buf, size_t buflen, int flags)
{
    return 0;
}

static inline void
imc_init_dso(struct imc_dso *dso) {}

#endif /* CONFIG_TARGET_IMC */
#endif /* IMC_H_INCLUDED */
