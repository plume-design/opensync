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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#include "ds_list.h"

/**
 * @brief Receive callback provided by the manager
 *
 */
typedef void (*imc_recv)(void *, size_t);
typedef void (*unix_recv)(void *, size_t);

struct imc_context;
struct imc_dso;
struct imc_sockoption;
struct unix_context;

typedef void (*imc_ev_cbfn)(struct ev_loop *, struct imc_context *, int);

typedef void (*imc_free_sndmsg)(void *, void *);

typedef int (*imc_init_client)(struct imc_dso *, imc_free_sndmsg free_snd_msg, void *free_msg_hint);
typedef void (*imc_terminate_client)(struct imc_dso *);
typedef int (*imc_client_send)(struct imc_dso *, void *, size_t , int);
typedef int (*imc_init_server)(struct imc_dso *, struct ev_loop *, unix_recv);
typedef void (*imc_terminate_server)(struct imc_dso *);
typedef void (*imc_config_client_endpoint)(struct imc_dso *, char *endpoint);

typedef void (*init)(struct imc_dso *);

struct imc_dso
{
    union
    {
        struct imc_context *imc_zmq;
        struct unix_context *imc_socket;
    };

    bool initialized;
    uint64_t io_success_cnt;
    uint64_t io_failure_cnt;
    imc_free_sndmsg imc_free_sndmsg;
    void *free_msg_hint;

    void *handle;

    init init;

    imc_config_client_endpoint imc_config_client_endpoint;
    imc_init_client imc_init_client;
    imc_client_send imc_client_send;
    imc_terminate_client imc_terminate_client;

    imc_init_server imc_init_server;
    imc_terminate_server imc_terminate_server;
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
    void *zctx;
    void *zsock;
    int ztype;
    int events;
    ev_prepare w_prepare;
    ev_check w_check;
    ev_idle w_idle;
    ev_io w_io;
    ds_list_t options;
    uint64_t io_success_cnt;
    uint64_t io_failure_cnt;
};


struct unix_context
{
    struct ev_loop *loop;
    void *data;
    unix_recv recv_fn;
    char *endpoint;
    int sock_fd;
    ev_io w_io;
    int events;
};

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
dummy_imc_init_client(struct imc_dso *imc, imc_free_sndmsg free_snd_msg,
                      void *free_msg_hint)
{
    return 0;
}

static inline void
dummy_imc_terminate_client(struct imc_dso *imc) {}

static inline int
dummy_imc_init_server(struct imc_dso *imc, struct ev_loop *loop,
                      unix_recv recv_cb)
{
    return 0;
}

static inline void
dummy_imc_terminate_server(struct imc_dso *imc) {}

static inline int
dummy_imc_send(struct imc_dso *imc, void *buf, size_t buflen, int flags)
{
    return 0;
}

static inline void
dummy_imc_config_client_endpoint(struct imc_dso *imc, char *endpoint) {};

#endif /* IMC_H_INCLUDED */
