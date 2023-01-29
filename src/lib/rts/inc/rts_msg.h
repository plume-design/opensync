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

#ifndef RTS_MSG_H
#define RTS_MSG_H

#include "rts_mpmc.h"

static inline void
rts_msg_schedule(struct rts_mpmc *q, struct rts_mpmc_node *node, unsigned short pid, unsigned short group,
    void (*dispatch)(const struct rts_mpmc_node *, void *), void (*free)(struct rts_mpmc_node *))
{
    node->type = 0;
    node->next = 0;
    node->refcount = 0;
    node->pid = pid;
    node->group = group;
    node->dispatch = dispatch;
    node->free = free;
    rts_mpmc_push(q, node);
}

static inline void
rts_msg_dispatch(struct rts_thread *thread)
{
    struct rts_mpmc_node *node;
    while ((node = rts_mpmc_read(&thread->mqh)) != NULL) {
        /* count this event */
        thread->mqh.events++;
        /* filter messages from the creator */
        if (node->pid == thread->pid)
            continue;
        /* filter if the group is not the broadcast and it doesn't
         * match the threads group */
        if (node->group && node->group != thread->group)
            continue;
        /* must be a a broadcast or destined for this group */
        if (node->dispatch)
            node->dispatch(node, thread);
    }
}

/* Racey, but when running single-threaded, this guard can help
 * prevent unnecessary shared memory allocations. */
static inline bool
rts_msg_consumer(struct rts_thread *thread)
{
    return thread->mqh.consumer > 1;
}

static inline bool
rts_msg_broadcast(struct rts_thread *thread)
{
    return rts_handle_isolate == 0 && rts_msg_consumer(thread);
}

#endif
