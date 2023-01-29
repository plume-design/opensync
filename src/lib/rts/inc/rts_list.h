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

#ifndef RTS_LIST_H
#define RTS_LIST_H

#include "rts_common.h"

struct rts_list_head {
    struct rts_list_head *prev;
    struct rts_list_head *next;
};

static inline void
rts_list_init(struct rts_list_head *list)
{
    list->next = list;
    list->prev = list;
}

static inline void
rts_list_insert(struct rts_list_head *list, struct rts_list_head *item)
{
    item->next = list;
    item->prev = list->prev;
    item->next->prev = item;
    item->prev->next = item;
}

static inline void
rts_list_remove(struct rts_list_head *item)
{
    item->next->prev = item->prev;
    item->prev->next = item->next;
}

static inline int
rts_list_empty(struct rts_list_head *list)
{
    return list->next == list;
}

static inline void
rts_list_move(struct rts_list_head *list, struct rts_list_head *head)
{
    rts_list_remove(list);
    rts_list_insert(head, list);
}

#define rts_list_entry rts_container_of

#define rts_list_for_each(ptr, head) \
    for (ptr = (head)->next; ptr != (head); ptr = ptr->next)

#define rts_list_for_each_entry(ptr, head, member) \
    for (ptr = rts_list_entry((head)->next, typeof(*ptr), member); \
        &ptr->member != (head); \
        ptr = rts_list_entry(ptr->member.next, typeof(*ptr), member))

#define rts_list_for_each_entry_safe(ptr, tmp, head, member) \
    for (ptr = rts_list_entry((head)->next, typeof(*ptr), member), \
        tmp = rts_list_entry(ptr->member.next, typeof(*ptr), member); \
        &ptr->member != (head); \
        ptr = tmp, tmp = rts_list_entry(tmp->member.next, typeof(*tmp), member))

#endif
