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

#include <stdlib.h>

#include <log.h>
#include "memutil.h"

#define IPC_MSG_LINK_ABSTRACT
#include "mque_link.h"

struct ipc_msg_link_impl
{
    ipc_msg_link_t ifc;
    mq_link_t *mqlink;
    void *subscr;
    ipc_msg_received_func *pfn;
};

static void mqlink_close(ipc_msg_link_impl_t *self)
{
    if (!self) return;
    mq_link_close(self->mqlink, false/*allow future links to access this queue, helpful on process restart*/);
    self->mqlink = NULL;
    FREE(self);
}

static const char *mqlink_addr(const ipc_msg_link_impl_t *self)
{
    return mq_link_name(self->mqlink);
}

static bool mqlink_can_send(const ipc_msg_link_impl_t *self)
{
    return mq_link_can_send(self->mqlink);
}

static bool mqlink_can_receive(const ipc_msg_link_impl_t *self)
{
    return mq_link_can_receive(self->mqlink);
}

static size_t mqlink_max_msize(const ipc_msg_link_impl_t *self)
{
    return mq_link_max_msize(self->mqlink);
}

static void eh_on_mq_message_received(mq_link_t *mq_link, void *subscr, const uint8_t *msg, size_t mlen, mq_priority_t mpri)
{
    ipc_msg_link_impl_t *self = (ipc_msg_link_impl_t *)subscr;
    (void)mpri;
    if (self->pfn != NULL)
    {
        ipc_msg_t ipc_msg = { .addr = NULL, .data = (uint8_t *)msg, .size = mlen };
        self->pfn(&self->ifc, self->subscr, &ipc_msg);
    }
}

static bool mqlink_subscribe_receive(ipc_msg_link_impl_t *self, void *subscr, ipc_msg_received_func *pfn)
{
    self->subscr = subscr;
    self->pfn = pfn;
    return mq_link_subscribe_receive(self->mqlink, subscr ? self : NULL, pfn ? &eh_on_mq_message_received : NULL);
}

static bool mqlink_sendto(ipc_msg_link_impl_t *self, const ipc_msg_t *msg)
{
    return mq_link_sendto(self->mqlink, msg->data, msg->size, 0/*pri not used*/);
}

static bool mqlink_receive(ipc_msg_link_impl_t *self, ipc_msg_t *msg)
{
    if (msg->addr) msg->addr[0] = 0;
    return mq_link_receive(self->mqlink, msg->data, &msg->size, NULL);
}

static const ipc_msg_link_vft_t mque_link_VFT =
{
    .close = &mqlink_close,
    .get_addr = &mqlink_addr,
    .can_send = &mqlink_can_send,
    .can_receive = &mqlink_can_receive,
    .get_max_msize = &mqlink_max_msize,
    .subscribe_receive = &mqlink_subscribe_receive,
    .sendto = &mqlink_sendto,
    .receive = &mqlink_receive
};

ipc_msg_link_t *mque_link_open(const char *name, struct ev_loop *evloop, 
        enum mq_link_mode mode, size_t max_msize, size_t max_mcount)
{
    mq_link_t *mql = mq_link_open(name, evloop, mode, max_msize, max_mcount);
    if (mql == NULL) return NULL;

    ipc_msg_link_impl_t *self = CALLOC(1, sizeof(*self));

    self->mqlink = mql;
    self->ifc.pVFT = &mque_link_VFT;
    self->ifc.p_impl = self;
    return &self->ifc;
}
