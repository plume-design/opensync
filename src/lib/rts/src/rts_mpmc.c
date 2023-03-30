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

#include "rts_mpmc.h"
#include "rts_lock.h"
#include "rts_common.h"
#include "rts.h"

enum {
    RTS_MPMC_STUB =  0,
    RTS_MPMC_INIT = -1,
    RTS_MPMC_EXIT = -2,
};

struct rts_mpmc_ctrl_node {
    struct rts_mpmc_node node;
    unsigned consumer;
};

static void
rts_mpmc_free_stub(struct rts_mpmc_node *node)
{
    (void) node;
    /* intentionally empty */
}

static void
rts_mpmc_free_ctrl(struct rts_mpmc_node *node)
{
    rts_ext_free(rts_container_of(node, struct rts_mpmc_ctrl_node, node));
}

void
rts_mpmc_init(struct rts_mpmc *rts_mpmc)
{
    rts_mpmc->spinlock = 0;
    rts_mpmc->consumer = 0;

    rts_mpmc->stub.type = RTS_MPMC_STUB;
    rts_mpmc->stub.free = rts_mpmc_free_stub;
    rts_mpmc->stub.refcount = 0;
    rts_mpmc->stub.next = 0;

    rts_mpmc->tail = &rts_mpmc->stub;
}

void
rts_mpmc_exit(struct rts_mpmc *rts_mpmc)
{
    if (rts_mpmc->tail != &rts_mpmc->stub)
        rts_mpmc->tail->free(rts_mpmc->tail);
}

static inline void
rts_mpmc_release(struct rts_mpmc_handle *h, struct rts_mpmc_node *node)
{
    if (__sync_add_and_fetch(&node->refcount, 1) == h->consumer)
        node->free(node);
}

static struct rts_mpmc_node *
rts_mpmc_ctrl_create(int type, unsigned consumer)
{
    struct rts_mpmc_ctrl_node *ctrl = rts_ext_alloc(sizeof(*ctrl));
    if (ctrl) {
        ctrl->node.type = type;
        ctrl->node.free = rts_mpmc_free_ctrl;
        ctrl->node.refcount = 0;
        ctrl->consumer = consumer;
        return &ctrl->node;
    }

    return NULL;
}

struct rts_mpmc_node *
rts_mpmc_push(struct rts_mpmc *rts_mpmc, struct rts_mpmc_node *node)
{
    struct rts_mpmc_node *tail;

    rts_assert(rts_mpmc->consumer);

    node->next = 0;

    do {
        tail = rts_mpmc->tail;
    } while (!__sync_bool_compare_and_swap(&rts_mpmc->tail, tail, node));

    tail->next = node;
    return tail;
}

struct rts_mpmc_node *
rts_mpmc_read(struct rts_mpmc_handle *h)
{
    struct rts_mpmc_node *node;
    struct rts_mpmc_ctrl_node *ctrl;

    while (h->head->next != 0) {
        node = h->head;
        h->head = h->head->next;

        if (h->head->type == RTS_MPMC_INIT) {
            ctrl = rts_container_of(h->head, struct rts_mpmc_ctrl_node, node);
            h->consumer = ctrl->consumer;
        }

        rts_mpmc_release(h, node);

        if (h->head->type == RTS_MPMC_EXIT) {
            ctrl = rts_container_of(h->head, struct rts_mpmc_ctrl_node, node);
            h->consumer = ctrl->consumer;
        } else if (h->head->type != RTS_MPMC_INIT) {
            return h->head;
        }
    }
    return 0;
}

bool
rts_mpmc_handle_init(struct rts_mpmc *rts_mpmc, struct rts_mpmc_handle *h)
{
    struct rts_mpmc_node *init;

    h->consumer = 0;
    h->events = 0;

    spinlock_lock(&rts_mpmc->spinlock);
    init = rts_mpmc_ctrl_create(RTS_MPMC_INIT, ++rts_mpmc->consumer);
    if (!init) {
        spinlock_unlock(&rts_mpmc->spinlock);
        return false;
    }

    h->exit = rts_mpmc_ctrl_create(RTS_MPMC_EXIT, 0);
    if (!h->exit) {
        init->free(init);
        spinlock_unlock(&rts_mpmc->spinlock);
        return false;
    }

    h->head = rts_mpmc_push(rts_mpmc, init);
    spinlock_unlock(&rts_mpmc->spinlock);

    return true;
}

void
rts_mpmc_handle_exit(struct rts_mpmc *rts_mpmc, struct rts_mpmc_handle *h)
{
    struct rts_mpmc_node *node;
    struct rts_mpmc_ctrl_node *ctrl;

    spinlock_lock(&rts_mpmc->spinlock);
    ctrl = rts_container_of(h->exit, struct rts_mpmc_ctrl_node, node);
    ctrl->consumer = rts_mpmc->consumer - 1;
    rts_mpmc_push(rts_mpmc, h->exit);
    rts_mpmc->consumer--;
    spinlock_unlock(&rts_mpmc->spinlock);

    while (h->head->next != h->exit) {
        if (!h->head->next)
            continue;

        node = h->head;
        h->head = h->head->next;

        if (h->head->type == RTS_MPMC_INIT) {
            ctrl = rts_container_of(h->head, struct rts_mpmc_ctrl_node, node);
            h->consumer = ctrl->consumer;
        }

        rts_mpmc_release(h, node);

        if (h->head->type == RTS_MPMC_EXIT) {
            ctrl = rts_container_of(h->head, struct rts_mpmc_ctrl_node, node);
            h->consumer = ctrl->consumer;
        }
    }
    rts_mpmc_release(h, h->head);

#if 0
    spinlock_lock(&rts_mpmc->spinlock);
    if (!rts_mpmc->consumer) {
        rts_assert (rts_mpmc->tail);
        rts_assert (rts_mpmc->tail->next == NULL);
        if (rts_mpmc->tail != &rts_mpmc->stub) {
            rts_mpmc->tail->free(rts_mpmc->tail);
            rts_mpmc->tail = &rts_mpmc->stub;
        }
    }
    spinlock_unlock(&rts_mpmc->spinlock);
#endif
}

/* One message queue per process. */
struct rts_mpmc mq = {
    .spinlock = 0,
    .consumer = 0,
    .stub = {
        .type = RTS_MPMC_STUB,
        .free = rts_mpmc_free_stub,
        .refcount = 0,
        .next = 0,
    },
    .tail = &mq.stub
};


