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

#ifndef IMC_SOCKETS_H_INCLUDED
#define IMC_SOCKETS_H_INCLUDED
#include "imc.h"

/**
 * @brief initiates a unix server
 *
 * @param server the server context
 * @param loop the ev loop
 * @param recv_cb user provided data processing routine
 */
int
imc_socket_init_server(struct imc_dso *imc, struct ev_loop *loop,
                       unix_recv recv_cb);

/**
 * @brief terminates a unix server
 *
 * @param server the server to terminate
 */
void
imc_socket_terminate_server(struct imc_dso *imc);

/**
 * @brief initiates a unix client and connects to a server
 *
 * @param endpoint the client context
 */
int
imc_socket_init_client(struct imc_dso *client, imc_free_sndmsg free_msg,
                       void *free_msg_hint);

bool
unix_socket_initialized(struct imc_dso *client);

/**
 * @brief terminates a unix client and connected to a server
 *
 * @param endpoint the client context
 */
void
imc_socket_terminate_client(struct imc_dso *imc);

/**
 * @brief send data to a unix server
 *
 * @param context the socket context
 * @param buf the buffer to send
 * @param len the buffer size
 * @flags the transmit flags
 */
int
imc_socket_send(struct imc_dso *imc, void *buf, size_t buflen, int flags);

void
imc_init_dso(struct imc_dso *dso);

void
imc_socket_config_client_endpoint(struct imc_dso *imc, char *endpoint);

#endif /* IMC_SOCKETS_H_INCLUDED */
