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

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h> // AF_UNIX

#include <os_uds_link.h>
#include <ds_dlist.h>
#include <ds.h>
#include <log.h>
#include "memutil.h"
#include "util.h"

#define IPC_MSG_LINK_ABSTRACT
#include "udsock_link.h"

// datagram resending timeout
#ifndef USD_LINK_RESEND_TIMEOUT
#define USD_LINK_RESEND_TIMEOUT 1.0 // in sec
#endif

struct ipc_msg_link_impl
{
    ipc_msg_link_t ifc;
    uds_link_t socklink; // unix datagram socket link
    ds_dlist_t txdg; // bufferred datagrams for transmission
    size_t txdg_cnt; // num. of bufferred dgrams for transmission
    size_t txdg_max; // max no. of bufferred datagrams
    struct ev_loop *evloop;
    struct ev_timer resend_tim; // repeated send attempt timer
    ipc_msg_received_func *on_msg_rcvd_fp;
    void *subscr_ptr;
    const char *dest_addr;
};

// Defined datagram item for the list collection
typedef struct linked_udgram
{
    ds_dlist_node_t node;
    udgram_t dgram;
    uint8_t buf[0]; // buffer for datagram payload

} linked_udgram_t;

static void eh_on_dgram_received(uds_link_t *uds_link, const udgram_t *dg)
{
    ipc_msg_link_impl_t *self = CONTAINER_OF(uds_link, ipc_msg_link_impl_t, socklink);
    if (self->on_msg_rcvd_fp != NULL)
    {
        ipc_msg_t msg = { .addr = (char *)dg->addr.sun_path, .data = dg->data, .size = dg->size };
        self->on_msg_rcvd_fp(&self->ifc, self->subscr_ptr, &msg);
    }
}

static bool udslink_subscribe_receive(ipc_msg_link_impl_t *self, void *subscr, ipc_msg_received_func *pfn)
{
    if (self->evloop != NULL)
    {
        self->subscr_ptr = subscr;
        self->on_msg_rcvd_fp = pfn;
        return true;
    }
    return false;
}

static bool send_bufferred(ipc_msg_link_impl_t *self)
{
    if (0 == self->txdg_cnt) return true;

    while (!ds_dlist_is_empty(&self->txdg))
    {
        linked_udgram_t *ldg = (linked_udgram_t *)ds_dlist_remove_head(&self->txdg);

        if (!uds_link_sendto(&self->socklink, &ldg->dgram, false))
        {
            // put back to buffer when cannot send
            ds_dlist_insert_head(&self->txdg, ldg);
            return false;
        }
        else
        {
            self->txdg_cnt--;
            FREE(ldg);
        }
    }
    return true;
}

static bool send_later(ipc_msg_link_impl_t *self, const udgram_t *pdg)
{
    // check if buffering is still active
    if (self->txdg_cnt >= self->txdg_max)
    {
        LOG(WARN, "DGRAM sending failed, tx buffer full or disabled");
        return false;
    }

    size_t req_size = sizeof(linked_udgram_t) + pdg->size;
    linked_udgram_t *ldg = MALLOC(req_size);

    // copy datagram to the buffer
    ldg->dgram = *pdg;
    ldg->dgram.data = ldg->buf;
    memcpy(ldg->buf, pdg->data, pdg->size);
    ds_dlist_insert_tail(&self->txdg, ldg);
    self->txdg_cnt++;

    if (self->evloop != NULL)
    { // restart resending timer on every bufferred send request
        ev_timer_again(self->evloop, &self->resend_tim);
    }
    return true;
}

static bool udslink_sendto(ipc_msg_link_impl_t *self, const ipc_msg_t *msg)
{
    udgram_t dgram;
    // use default dest addr if not provided
    const char *da = msg->addr ? msg->addr : self->dest_addr;
    // check dest addr
    if (da == NULL || strlen(da) >= sizeof(dgram.addr.sun_path))
    {
        LOG(ERR, "Invalid socket dest address = %s", da ? da : "(null)");
        return false;
    }

    dgram.addr.sun_family = AF_UNIX;
    STRSCPY(dgram.addr.sun_path, da);
    // support abstract namespaces
    if (dgram.addr.sun_path[0] == '@') dgram.addr.sun_path[0] = 0;

    dgram.data = msg->data;
    dgram.size = msg->size;

    if (send_bufferred(self))
    {
        if (uds_link_sendto(&self->socklink, &dgram, false))
        {
            return true;
        }
        else
        {
            return send_later(self, &dgram);
        }
    }
    else
    {
        return send_later(self, &dgram);
    }
}

static void eh_on_resend_timeout(struct ev_loop *loop, ev_timer *w, int revents)
{
    ipc_msg_link_impl_t *self = CONTAINER_OF(w, ipc_msg_link_impl_t, resend_tim);

    /* Try to send all bufferred dgrams, if something left, restart the timer
     * to try again after some period of time */
    if (!send_bufferred(self))
    {
        ev_timer_again(self->evloop, &self->resend_tim);
    }
    else
    { // all sent - stop resend timer
        ev_timer_stop(self->evloop, &self->resend_tim);
    }
}

static void udslink_close(ipc_msg_link_impl_t *self)
{
    void *temp = NULL;
    if (!self) return;
    uds_link_fini(&self->socklink);
    if (self->evloop)
    {
        ev_timer_stop(self->evloop, &self->resend_tim);
    }
    while (!ds_dlist_is_empty(&self->txdg))
    {
        temp = ds_dlist_remove_head(&self->txdg);
        FREE(temp);
    }
    FREE(self);
}

static bool udslink_receive(ipc_msg_link_impl_t *self, ipc_msg_t *msg)
{
    if (!msg || !msg->data || msg->size == 0)
    {
        LOG(ERR, "Blocking reception failed, dest message buffer is NULL");
        return false;
    }

    udgram_t dg = { .data = msg->data, .size = msg->size };
    if (uds_link_receive(&self->socklink, &dg))
    {
        if (dg.size > msg->size)
        {
            LOG(ERR, "Dest msg buffer too small, required=%zu, is=%zu", dg.size, msg->size);
            return false;
        }

        if (msg->addr != NULL)
        {
            strscpy(msg->addr, dg.addr.sun_path, sizeof(dg.addr.sun_path));
        }
        memcpy(msg->data, dg.data, dg.size);
        msg->size = dg.size;
        return true;
    }
    return false;
}

static bool udslink_can_send(const ipc_msg_link_impl_t *self)
{
    (void)self;
    return true;
}

static bool udslink_can_receive(const ipc_msg_link_impl_t *self)
{
    (void)self;
    return true;
}

static const char *udslink_addr(const ipc_msg_link_impl_t *self)
{
    return uds_link_socket_name(&self->socklink);
}

static size_t udslink_max_msize(const ipc_msg_link_impl_t *self)
{
    return uds_link_get_max_dgsize(&self->socklink);
}

static const ipc_msg_link_vft_t uds_link_VFT =
{
    .close = &udslink_close,
    .get_addr = &udslink_addr,
    .can_send = &udslink_can_send,
    .can_receive = &udslink_can_receive,
    .get_max_msize = &udslink_max_msize,
    .subscribe_receive = &udslink_subscribe_receive,
    .sendto = &udslink_sendto,
    .receive = &udslink_receive
};

ipc_msg_link_t *udsock_link_open(const char *addr, const char *dest_addr, 
                                struct ev_loop *evloop, size_t max_msize, size_t max_mcount)
{
    ipc_msg_link_impl_t *self = (ipc_msg_link_impl_t *)CALLOC(1, sizeof(*self));

    self->ifc.pVFT = &uds_link_VFT;
    self->ifc.p_impl = self;

    if (!uds_link_init(&self->socklink, addr, evloop, max_msize))
    {
        FREE(self);
        return NULL;
    }

    // receive datagrams via event loop ?
    self->evloop = evloop;
    if (self->evloop != NULL)
    {
        uds_link_subscribe_datagram_read(&self->socklink, &eh_on_dgram_received);
        ev_timer_init(&self->resend_tim, &eh_on_resend_timeout, 0, USD_LINK_RESEND_TIMEOUT);
    }

    self->dest_addr = dest_addr;
    ds_dlist_init(&self->txdg, linked_udgram_t, node);
    self->txdg_cnt = 0;
    self->txdg_max = max_mcount;

    return &self->ifc;
}
