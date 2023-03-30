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

#ifndef RTS_MPMC_H
#define RTS_MPMC_H

#include "rts_common.h"
#include "rts_slob.h"

struct rts_mpmc_node {
    int type;
    unsigned refcount;
    unsigned pid;
    unsigned group;
    struct rts_mpmc_node *next;
    void (*dispatch)(const struct rts_mpmc_node *, void *);
    void (*free)(struct rts_mpmc_node *);
};

struct rts_mpmc {
    unsigned spinlock;
    unsigned consumer;
    struct rts_mpmc_node  stub;
    struct rts_mpmc_node *tail;
};

struct rts_mpmc_handle {
    struct rts_mpmc_node *head;
    struct rts_mpmc_node *exit;
    unsigned consumer;

    /* stats */
    unsigned events;
};

void rts_mpmc_init(struct rts_mpmc *rts_mpmc);
void rts_mpmc_exit(struct rts_mpmc *rts_mpmc);

struct rts_mpmc_node *rts_mpmc_push(struct rts_mpmc *rts_mpmc, struct rts_mpmc_node *node);
struct rts_mpmc_node *rts_mpmc_read(struct rts_mpmc_handle *h);

bool rts_mpmc_handle_init(struct rts_mpmc *rts_mpmc, struct rts_mpmc_handle *h);
void rts_mpmc_handle_exit(struct rts_mpmc *rts_mpmc, struct rts_mpmc_handle *h);

extern struct rts_mpmc mq;

#endif
