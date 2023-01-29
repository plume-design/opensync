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

#include "rts_slob.h"
#include "rts_lock.h"
#include "rts_priv.h"

#define SLOB_DATA_SIZE (32)

/* Represents the smallest memory unit. For example, with a 16 byte data
 * section, the largest allocation a single slob can satisfy is 16 bytes.
 * To allocate a 17th byte would require a super slob made up from 2
 * slobs. The second slob would provide an additional 32 bytes of storage. */
struct rts_slob {
    struct rts_list_head list;
    unsigned char data[SLOB_DATA_SIZE];
};

static inline size_t
get_nslobs(size_t size)
{
    return (size + sizeof(struct rts_list_head) - 1) / sizeof(struct rts_slob);
}

/*
struct rts_pool {
   struct rts_list_head list;
   struct rts_slob *head;
   struct rts_slob *tail;
};
*/

/* Free one or more slobs backing @data */
static inline void
rts_slob_free(struct rts_pool *pool, struct rts_slob *slob)
{
    struct rts_slob *last;

    last = rts_container_of(slob->list.prev, struct rts_slob, list);
    rts_assert(slob->list.next == 0);
    rts_assert(slob >= pool->head && slob < pool->tail);

    rts_assert(pool->stats.curr_alloc >= sizeof(struct rts_slob) * (last - slob + 1));
    pool->stats.curr_alloc -= sizeof(struct rts_slob) * (last - slob + 1);

    do {
        rts_list_insert(&pool->list, &slob->list);
    } while (last != slob++);
}

/* Find a linear span of @n slobs from @head. On success this will return
 * the last slob in the set. */
static inline struct rts_slob *
rts_slob_alloc_linear_tail(struct rts_slob *head, struct rts_slob *end, size_t n)
{
    struct rts_slob *tail;
    size_t m = 0;

    if ((tail = head + 1) == end)
        return NULL;

    while (tail->list.next) {

        rts_assert(tail->list.prev != &tail->list);

        if (++m == n) {
            return tail;
        }
        if (++tail == end)
            break;
    }
    return NULL;
}

/* Backend allocator for requests that exceed SLOB_DATA_SIZE and require
 * multiple blocks to satisfy. The method is simple: Take item from free
 * list, then manually walk the linear buffer looking for adjacent items
 * that are ok to combine into a super slob. */
static inline struct rts_slob *
rts_slob_alloc_linear(struct rts_pool *pool, size_t n)
{
    struct rts_slob *head, *tail, *iter;

    /* Scan the free list, looking for other slobs */
    rts_list_for_each_entry(head, &pool->list, list) {
        /* Found a tail */
        if ((tail = rts_slob_alloc_linear_tail(head, pool->tail, n)) != NULL) {
            /* Unlink */
            for (iter = head; iter != tail + 1; iter++) {
                rts_list_remove(&iter->list);
            }
            /* Set magic markers */
            head->list.next = 0;
            head->list.prev = &tail->list;

            pool->stats.curr_alloc += sizeof(struct rts_slob) * (n + 1);
            pool->stats.peak_alloc = rts_max(pool->stats.curr_alloc, pool->stats.peak_alloc);
            return head;
        }
    }
    return NULL;
}

static inline size_t
rts_slob_capacity(struct rts_slob *slob)
{
    struct rts_slob *last = rts_container_of(slob->list.prev, struct rts_slob, list);
    rts_assert(slob->list.next == NULL);
    rts_assert(slob->list.prev != NULL);
    return (last - slob) * sizeof(*slob) + sizeof(slob->data);
}

static inline struct rts_slob *
rts_slob_alloc(struct rts_pool *pool, size_t n)
{
    struct rts_slob *slob;

    if (rts_list_empty(&pool->list))
        return NULL;

    if (n)
        return rts_slob_alloc_linear(pool, n);

    slob = rts_container_of(pool->list.next, struct rts_slob, list);
    rts_list_remove(&slob->list);
    slob->list.next = 0;
    slob->list.prev = &slob->list;

    pool->stats.curr_alloc += sizeof(struct rts_slob);
    pool->stats.peak_alloc = rts_max(pool->stats.curr_alloc, pool->stats.peak_alloc);
    return slob;
}

static inline struct rts_slob *
rts_slob_expand(struct rts_pool *pool, struct rts_slob *slob, size_t n)
{
    struct rts_slob *tail;
    size_t capacity = rts_slob_capacity(slob);
    size_t m = n - get_nslobs(capacity);

    rts_assert(m);

    /* Attempt to grow if the adjacent higher address blocks are free */
    tail = rts_container_of(slob->list.prev, struct rts_slob, list);
    if ((tail = rts_slob_alloc_linear_tail(tail, pool->tail, m)) != NULL) {

        /* Unlink from one _after_ old tail to new tail */
        struct rts_slob *iter = rts_container_of(slob->list.prev, struct rts_slob, list) + 1;
        for (; iter != tail + 1; iter++) {
            rts_list_remove(&iter->list);
        }

        /* Update magic markers */
        rts_assert(slob->list.next == 0);
        slob->list.prev = &tail->list;

        pool->stats.curr_alloc += sizeof(struct rts_slob) * m;
        pool->stats.peak_alloc = rts_max(pool->stats.curr_alloc, pool->stats.peak_alloc);
        return slob;
    }
    return NULL;
}

void
rts_pool_free(struct rts_pool *pool, void *data)
{
    rts_slob_free(pool, rts_container_of(data, struct rts_slob, data));
}

void *
rts_pool_alloc(struct rts_pool *pool, size_t size)
{
    struct rts_slob *slob;
    if ((slob = rts_slob_alloc(pool, get_nslobs(size))) == NULL) {
        pool->stats.fail_alloc++;
        rts_assert_msg(0, "fail_alloc: size %zu", size);
        return NULL;
    }
    return slob->data;
}

void *
rts_pool_realloc(struct rts_pool *pool, void *data, size_t size, size_t *alloc)
{
    struct rts_slob *slob, *next;
    size_t cap, n = get_nslobs(size);

    /* Allocation */
    if (!data) {
        if ((slob = rts_slob_alloc(pool, n)) == NULL) {
            pool->stats.fail_alloc++;
            rts_assert_msg(0, "fail_alloc: size %zu", size);
            return NULL;
        }
        *alloc = rts_slob_capacity(slob);
        return slob->data;
    }

    slob = rts_container_of(data, struct rts_slob, data);
    cap = rts_slob_capacity(slob);

    /* No need to realloc */
    if (size <= cap) {
        *alloc = cap;
        return data;
    }

    rts_assert(n > 0);

    /* Expansion */
    if ((next = rts_slob_expand(pool, slob, n)) != NULL) {
        rts_assert(slob == next);
        *alloc = rts_slob_capacity(slob);
        return slob->data;
    }

    /* Reallocation */
    if ((next = rts_slob_alloc(pool, n)) != NULL) {
        rts_assert(rts_slob_capacity(next) > cap);
        __builtin_memcpy(next->data, slob->data, cap);
        rts_slob_free(pool, slob);
        *alloc = rts_slob_capacity(next);
        return next->data;
    }

    pool->stats.fail_alloc++;
    rts_assert_msg(0, "fail_alloc: size %zu", size);
    return NULL;
}

void
rts_pool_init(struct rts_pool *pool, void *data, size_t len)
{
    struct rts_slob *slob = data;

    pool->head = slob;
    pool->tail = slob + (len / sizeof(*pool->tail));

    rts_list_init(&pool->list);
    while (slob != pool->tail) {
        rts_list_insert(&pool->list, &slob->list);
        ++slob;
    }

    pool->stats.curr_alloc = 0;
    pool->stats.peak_alloc = 0;
    pool->stats.fail_alloc = 0;
}

void
rts_pool_exit(struct rts_pool *pool)
{
    pool->head = NULL;
    pool->tail = NULL;
    rts_list_init(&pool->list);
}

/*
void
rts_shm_pool_init(struct rts_shm_pool *shm, void *data, size_t len)
{
    shm->spinlock = 0;
    rts_pool_init(&shm->pool, data, len);
}

void
rts_shm_pool_exit(struct rts_shm_pool *shm)
{
    rts_pool_exit(&shm->pool);
    shm->spinlock = 0;
}

void *
rts_shm_pool_alloc(struct rts_shm_pool *shm, size_t size)
{
    void *data;
    spinlock_lock(&shm->spinlock);
    data = rts_pool_alloc(&shm->pool, size);
    spinlock_unlock(&shm->spinlock);
    return data;
}

void
rts_shm_pool_free(struct rts_shm_pool *shm, void *data)
{
    spinlock_lock(&shm->spinlock);
    rts_pool_free(&shm->pool, data);
    spinlock_unlock(&shm->spinlock);
}

*/
