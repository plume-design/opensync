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

#ifndef RTS_SLOB_H
#define RTS_SLOB_H

/* Necessary for rts_memstats and because rts_priv.h requires this file. */
#define RTS_H /* Act as rts.h for private, internal use */
#include "rts_types.h"
#undef RTS_H

#include "rts_list.h"
#include "rts_common.h"

struct rts_memstats {
    unsigned peak_alloc;
    unsigned curr_alloc;
    unsigned fail_alloc;
};

struct rts_pool {
    struct rts_list_head list;
    struct rts_slob *head;
    struct rts_slob *tail;
    struct rts_memstats stats;
};

/*
struct rts_shm_pool {
    unsigned spinlock;
    struct rts_pool pool;
};
*/
void *rts_pool_alloc(struct rts_pool *pool, size_t size);
void *rts_pool_realloc(struct rts_pool *pool, void *data, size_t size, size_t *alloc);
void rts_pool_free(struct rts_pool *pool, void *data);
void rts_pool_init(struct rts_pool *pool, void *data, size_t len);
void rts_pool_exit(struct rts_pool *pool);

/*
void *rts_shm_pool_alloc(struct rts_shm_pool *shm, size_t size);
void rts_shm_pool_free(struct rts_shm_pool *shm, void *data);
void rts_shm_pool_init(struct rts_shm_pool *shm, void *data, size_t len);
void rts_shm_pool_exit(struct rts_shm_pool *shm);
*/
#endif
