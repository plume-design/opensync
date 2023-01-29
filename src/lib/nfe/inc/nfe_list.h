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

#ifndef NFE_LIST_H
#define NFE_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include "nfe_priv.h"

#define LIST_HEAD_INIT(name) { &(name), &(name) }

struct nfe_list_head {
    struct nfe_list_head *prev;
    struct nfe_list_head *next;
};

struct nfe_hash_list {
    struct nfe_hash_list *next;
};

static inline void
nfe_list_init(struct nfe_list_head *list)
{
    list->next = list;
    list->prev = list;
}

static inline void
nfe_list_insert(struct nfe_list_head *list, struct nfe_list_head *item)
{
    item->next = list;
    item->prev = list->prev;
    item->next->prev = item;
    item->prev->next = item;
}

static inline void
nfe_list_remove(struct nfe_list_head *item)
{
    item->next->prev = item->prev;
    item->prev->next = item->next;

    item->prev = item;
    item->next = item;
}

static inline bool
nfe_list_empty(struct nfe_list_head *list)
{
    return list->next == list;
}

static inline void
nfe_list_splice(struct nfe_list_head *list, struct nfe_list_head *head)
{
    struct nfe_list_head *prev, *next;

    if (nfe_list_empty(list))
        return;

    prev = head;
    next = head->next;

    list->next->prev = prev;
    prev->next = list->next;

    list->prev->next = next;
    next->prev = list->prev;
}

#define container_of(ptr, type, member) \
    (type *)((char *)ptr - offsetof(type, member))

#define nfe_list_entry container_of

#define nfe_list_for_each(ptr, head) \
    for (ptr = (head)->next; ptr != (head); ptr = ptr->next)

#define nfe_list_for_each_entry(ptr, head, member) \
    for (ptr = nfe_list_entry((head)->next, typeof(*ptr), member); \
        &ptr->member != (head); \
        ptr = nfe_list_entry(ptr->member.next, typeof(*ptr), member))

#define nfe_list_for_each_entry_safe(ptr, tmp, head, member) \
    for (ptr = nfe_list_entry((head)->next, typeof(*ptr), member), \
        tmp = nfe_list_entry(ptr->member.next, typeof(*ptr), member); \
        &ptr->member != (head); \
        ptr = tmp, tmp = nfe_list_entry(tmp->member.next, typeof(*tmp), member))

#endif
