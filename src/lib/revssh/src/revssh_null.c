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

#include "revssh.h"

struct revssh
{

};

revssh_t *revssh_new(void)
{
    revssh_t *self = CALLOC(1, sizeof(revssh_t));
    return self;
}

bool revssh_server_params_set(revssh_t *self, const char *host, int port, const char *user)
{
    (void)self;
    (void)host;
    (void)port;
    (void)user;

    return true;
}

bool revssh_authorized_keys_add(revssh_t *self, const char *pubkey)
{
    (void)self;
    (void)pubkey;

    return true;
}

bool revssh_tunnel_params_set(
        revssh_t *self,
        osn_ipany_addr_t *remote_bind_addr,
        int remote_bind_port,
        osn_ipany_addr_t *local_addr,
        int local_port)
{
    (void)self;
    (void)remote_bind_addr;
    (void)remote_bind_port;
    (void)local_addr;
    (void)local_port;

    return true;
}

bool revssh_timeout_set(revssh_t *self, int session_max_time, int idle_timeout)
{
    (void)self;
    (void)session_max_time;
    (void)idle_timeout;

    return true;
}

bool revssh_notify_status_callback_set(revssh_t *self, revssh_status_fn_t *status_fn_cb)
{
    (void)self;
    (void)status_fn_cb;

    return true;
}

// revssh_node_gen_tmpkey_set_params(type, bits);    // TODO

bool revssh_start(revssh_t *self)
{
    (void)self;

    return false;
}

bool revssh_node_pubkey_get(revssh_t *self, char *pubkey, size_t len)
{
    (void)self;
    (void)pubkey;
    (void)len;

    return false;
}

bool revssh_del(revssh_t *self)
{
    FREE(self);
    return true;
}

void revssh_cleanup_dangling_sessions(void)
{

}