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

#include "log.h"

#if defined(CONFIG_IMC_ZMQ)
#include "imc_zmq.h"
#elif defined(CONFIG_IMC_SOCKETS)
#include "imc_sockets.h"
#else
#include "imc.h"
#endif

void
imc_init_dso(struct imc_dso *dso)
{
#if defined(CONFIG_IMC_ZMQ)
    dso->imc_init_client = imc_zmq_init_client;
    dso->imc_terminate_client = imc_zmq_terminate_client;
    dso->imc_client_send = imc_zmq_send;
    dso->imc_init_server = imc_zmq_init_server;
    dso->imc_terminate_server = imc_zmq_terminate_server;
    dso->imc_config_client_endpoint = imc_zmq_config_client_endpoint;
#elif defined(CONFIG_IMC_SOCKETS)
    dso->imc_init_client = imc_socket_init_client;
    dso->imc_terminate_client = imc_socket_terminate_client;
    dso->imc_client_send = imc_socket_send;
    dso->imc_init_server = imc_socket_init_server;
    dso->imc_terminate_server = imc_socket_terminate_server;
    dso->imc_config_client_endpoint = imc_socket_config_client_endpoint;
#else
    dso->imc_init_client = dummy_imc_init_client;
    dso->imc_terminate_client = dummy_imc_terminate_client;
    dso->imc_client_send = dummy_imc_send;
    dso->imc_init_server = dummy_imc_init_server;
    dso->imc_terminate_server = dummy_imc_terminate_server;
    dso->imc_config_client_endpoint = dummy_imc_config_client_endpoint;
#endif
}

