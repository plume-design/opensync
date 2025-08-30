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

#include <sys/mman.h>

#include "log.h"

#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "osa_assert.h"

#include "arena_base.h"

static void arena_dyn_del(arena_t *a);
static void arena_static_del(arena_t *a);
static struct arena_chunk *arena_chunk_new(size_t sz);
static void *arena_chunk_push(struct arena_chunk *chunk, size_t sz);
static size_t arena_chunk_pop(struct arena_chunk *chunk, size_t sz);
static void arena_chunk_del(struct arena_chunk *chunk);
static bool arena_chunk_check_resize(struct arena_chunk *ac, void *ptr, size_t sz);
static bool arena_defer_cleanup(arena_t *arena, struct arena_chunk *chunk);

/*
 * =============================================================================
 * Public arena allocator API
 * =============================================================================
 */

/*
 * Create a new arena on buffer `buf`. `buf` will be used to hold internal
 * structures to represent the arena so the total number of available bytes
 * for allocation will be `sz` - ARENA_STATIC_SZ.
 */
arena_t *arena_new_static(void *buf, size_t sz)
{
    if (sz < ARENA_STATIC_SZ) return NULL;

    struct arena_static *as = buf;

    memset(as, 0, sizeof(*as));
    as->as_chunk.ac_size = sz - ARENA_STATIC_SZ;
    as->as_arena.a_size = sz - ARENA_STATIC_SZ;
    as->as_arena.a_top = &as->as_chunk;

    return &as->as_arena;
}

/*
 * Create and return a new arena object.
 *
 * If sz is 0, the arena will be dynamically growing. Otherwise a static arena
 * of size `sz` will be allocated.
 */
arena_t *arena_new(size_t sz)
{
    arena_t *a = NULL;

    if (sz == 0)
    {
        /*
         * Dynamic arena case -- allocate just the arena_t structure, everything
         * else will be allocated dynamically using mmap()
         */
        a = calloc(1, sizeof(arena_t));
        if (a == NULL) return NULL;
        a->a_del_fn = arena_dyn_del;
    }
    else
    {
        /*
         * Static arena case -- allocate the arena in a single memory chunk
         */
        struct arena_static *as = malloc(ARENA_STATIC_SZ + sz);
        if (as == NULL) return NULL;

        a = arena_new_static(as, ARENA_STATIC_SZ + sz);
        if (a == NULL)
        {
            free(as);
            return NULL;
        }
        a->a_del_fn = arena_static_del;
    }

    return a;
}

/*
 * Destroy an arena object. This also executes any deferred handlers.
 *
 * Returns the number of bytes freed from the arena or ARENA_ERROR in case of
 * error.
 */
size_t arena_del(arena_t *a)
{
    size_t rc = arena_pop(a, SIZE_MAX);
    if (a->a_del_fn != NULL) a->a_del_fn(a);
    return rc;
}

/*
 * Callback for dynamic arena deallocation
 */
void arena_dyn_del(arena_t *a)
{
    free(a);
}

/*
 * Callback for static arena deallocation
 */
void arena_static_del(arena_t *a)
{
    struct arena_static *as = CONTAINER_OF(a, struct arena_static, as_arena);
    free(as);
}

/*
 * Extend the arena boundary by `sz` bytes.
 *
 * If the arena is dynamic, this can allocate new memory.
 */
void *arena_push(arena_t *arena, size_t sz)
{
    void *ptr = arena_chunk_push(arena->a_top, sz);
    if (ptr != NULL)
    {
        arena->a_pos += sz;
        return ptr;
    }

    /* Static arena, just return an error */
    if (arena->a_size > 0) return NULL;

    /*
     * We need to grow the arena, add a new chunk. To be sure, allocate an arena
     * that is at least sz*1.5 in size.
     */
    size_t rsz = (SIZE_MAX - (sz >> 1)) < sz ? SIZE_MAX : (sz + (sz >> 1));
    struct arena_chunk *new = arena_chunk_new(rsz);
    if (new == NULL)
    {
        LOG(ERR, "arena: Out of memory, cannot extend arena.");
        return NULL;
    }

    /* Add the new chunk to the arena */
    new->ac_next = arena->a_top;
    arena->a_top = new;

    ptr = arena_chunk_push(new, sz);
    if (ptr == NULL) return NULL;
    arena->a_pos += sz;
    return ptr;
}

/*
 * Reduce the arena boundary by `sz` bytes. This function can reclaim memory.
 *
 * This function returns the actual number of bytes by which the arena has
 * shrunk or ARENA_ERROR on error.
 */
size_t arena_pop(arena_t *arena, size_t sz)
{
    /* Static arena case */
    if (arena->a_size > 0)
    {
        size_t psz = arena_chunk_pop(arena->a_top, sz);
        arena->a_pos -= psz;

        if (!arena_defer_cleanup(arena, arena->a_top))
        {
            return ARENA_ERROR;
        }

        return psz;
    }

    /* Purge unused chunks from the top of the arena */
    size_t psz = sz;
    while (arena->a_top != NULL)
    {
        size_t csz = arena_chunk_pop(arena->a_top, psz);
        arena->a_pos -= csz;
        psz -= csz;

        if (!arena_defer_cleanup(arena, arena->a_top))
        {
            return ARENA_ERROR;
        }

        /* Empty chunk */
        if (arena->a_top->ac_pos == 0)
        {
            struct arena_chunk *chunk = arena->a_top;
            arena->a_top = chunk->ac_next;
            arena_chunk_del(chunk);
        }

        if (psz == 0) break;
    }

    return sz - psz;
}

/*
 * Return the current end of the arena
 */
void *arena_get(arena_t *arena)
{
    return arena_push(arena, 0);
}

/*
 * Free arena memory until it reaches the position at `ptr`, which must be a
 * valid pointer within the arena.
 *
 * Note: With dynamic arenas, `ptr` may be invalidated if it points exactly
 * to a chunk boundary (ie. freeing non-contiguous memory).
 *
 * This function returns ARENA_ERROR on error or the number of bytes removed
 * from the arena on success.
 */
size_t arena_set(arena_t *arena, void *ptr)
{
    struct arena_chunk *chunk;
    uint8_t *uptr = ptr;

    size_t popcount = 0;

    /* Find out whether ptr is inside the arena and count the number of bytes to pop */
    for (chunk = arena->a_top; chunk != NULL; chunk = chunk->ac_next)
    {
        if ((uptr >= &chunk->ac_data[0]) && (uptr <= &chunk->ac_data[chunk->ac_pos]))
        {
            popcount += &chunk->ac_data[chunk->ac_pos] - uptr;
            break;
        }
        popcount += chunk->ac_pos;
    }

    if (chunk == NULL) return ARENA_ERROR;

    return arena_pop(arena, popcount);
}

/*
 * malloc() arena equivalent. This allocates and returns a memory aligned memory
 * address inside the arena.
 *
 * arena_malloc(0) can be used to acquire a 0-sized memory aligned buffer,
 * which can be subsequentially resized with arena_mresize().
 */
void *arena_malloc(arena_t *arena, size_t sz)
{
    uint8_t *ptr;

    if (sz > (SIZE_MAX - (ARENA_ALIGN - 1))) return NULL;

    ptr = arena_push(arena, sz + ARENA_ALIGN - 1);
    if (ptr == NULL) return NULL;

    size_t pop_cnt = ((uintptr_t)ptr - 1) & (ARENA_ALIGN - 1);
    ptr += (ARENA_ALIGN - 1) ^ pop_cnt;
    arena->a_pos -= arena_chunk_pop(arena->a_top, pop_cnt);

    return (void *)ptr;
}

/*
 * Same as arena_malloc() except it initializes the allocated memory
 * to 0.
 */
void *arena_calloc(arena_t *arena, size_t sz)
{
    void *ptr = arena_malloc(arena, sz);
    if (ptr == NULL) return NULL;

    memset(ptr, 0, sz);

    return ptr;
}

/*
 * Resize pointer `ptr`/`ptrsz` to `newsz`. Portable code should not assume
 * that `ptr` stays stable.
 *
 * This functionr returns NULL on error. In this case the buffer pointed to
 * by `ptr` reimains unchanged.
 */
void *arena_mresize(arena_t *arena, void *ptr, size_t ptrsz, size_t newsz)
{
    if (ptr == NULL || arena->a_top == NULL || !arena_chunk_check_resize(arena->a_top, ptr, ptrsz))
    {
        return NULL;
    }

    /*
     * Check easy cases first shrinking and allocations that fit
     * in the current arena chunk
     */
    if (ptrsz > newsz)
    {
        if (arena_pop(arena, ptrsz - newsz) == ARENA_ERROR) return NULL;

        if (newsz == 0)
        {
            /*
             * This function should always return a valid pointer -- arena_pop()
             * may destroy the chunk of memory referenced by `ptr` if newsz == 0,
             * so just return the current end of arena.
             */
            return arena_push(arena, 0);
        }

        return ptr;
    }
    else if (arena_chunk_push(arena->a_top, newsz - ptrsz) != NULL)
    {
        arena->a_pos += newsz - ptrsz;
        return ptr;
    }

    if (arena->a_size > 0)
    {
        return NULL;
    }

    /*
     * Not enough space in the current chunk, we need to move the data to a new
     * chunk. Allocate a chunk that is at least 1.5 times bigger than `newsz`.
     */
    size_t rsz = (SIZE_MAX - (newsz >> 1)) < newsz ? SIZE_MAX : (newsz + (newsz >> 1));
    struct arena_chunk *chunk = arena_chunk_new(rsz);
    if (chunk == NULL)
    {
        LOG(ERR, "arena: Out of memory, cannot resize arena.");
        return NULL;
    }

    /* Move the data to the new chunk */
    void *dst = arena_chunk_push(chunk, newsz);
    memcpy(dst, ptr, ptrsz);

    struct arena_chunk *cchunk = arena->a_top;
    /* Shrink the current chunk. If it is empty, we can remove it from the arena. */
    arena_chunk_pop(cchunk, ptrsz);
    if (cchunk->ac_pos == 0)
    {
        arena->a_top = cchunk->ac_next;
        arena_chunk_del(cchunk);
    }

    chunk->ac_next = arena->a_top;
    arena->a_top = chunk;
    arena->a_pos += newsz - ptrsz;

    return dst;
}

/*
 * Append memory to the buffer pointed to by `ptr`. The ergonomics of this
 * function are designed for resizing growable arrays. If `ptr` is NULL
 * a new memory-aligned buffer is allocated.
 *
 * `ptr` + `membsz` * `cur` must point exactly to the end of arena. The buffer
 * is expanded by `grow` * `membsz` bytes.
 *
 * If This function fails, the buffer is left intact.
 *
 * Note: This function cannot shrink the memory region pointed to by `ptr`.
 */
void *arena_mappend(arena_t *arena, void *ptr, size_t membsz, size_t cur, size_t grow)
{
    if (ptr == NULL)
    {
        ptr = arena_malloc(arena, 0);
    }

    return arena_mresize(arena, ptr, membsz * cur, membsz * (cur + grow));
}

/*
 * Save current arena boundary, calling arena_restore() afterwards will
 * free everything that was allocated between arena_save() and arena_restore()
 */
arena_frame_t arena_save(arena_t *arena)
{
    if (arena == NULL) return (arena_frame_t){.af_arena = NULL};

    return (arena_frame_t){
        .af_magic = (ARENA_MAGIC ^ (uintptr_t)arena ^ arena->a_pos),
        .af_arena = arena,
        .af_pos = arena->a_pos};
}

/*
 * Restore saved arena boundary (free elements between arena_save() and arena_restore())
 */
void arena_restore(arena_frame_t frame)
{
    ASSERT(__arena_restore(frame), "arena_restore() fatal error.");
}

/*
 * Same as arena_restore() except it returns an error whenever there's a problem
 * restoring the arena frame. Note that this usually means that the defer stack
 * was clobbered -- probably its safer to assert than attempt to recover from
 * this. The reason this function exists is so it can be wrapped inside a
 * macro that shows the file/line number when asserting.
 */
bool __arena_restore(arena_frame_t frame)
{
    if (frame.af_arena == NULL) return true;
    if ((frame.af_magic ^ (uintptr_t)frame.af_arena ^ frame.af_pos) != ARENA_MAGIC) return false;
    if (frame.af_pos > frame.af_arena->a_pos) return true;

    size_t rc = arena_pop(frame.af_arena, frame.af_arena->a_pos - frame.af_pos);
    return (rc != ARENA_ERROR);
}

/*
 * Allocate a defer action on the current arena. If the defer action block
 * is freed, the defer callback is called.
 *
 * As a minor security mitigation, the defer block is protected by a checksum
 * just in case the defer block is somehow corrupted.
 */
bool arena_defer(arena_t *arena, arena_defer_fn_t *fn, void *data)
{
    struct arena_defer *defer;

    defer = arena_malloc(arena, sizeof(*defer));
    if (defer == NULL)
    {
        fn(data);
        return false;
    }

    defer->ad_fn = fn;
    defer->ad_data = data;
    defer->ad_next = arena->a_defer;

    defer->ad_magic = 0;
    defer->ad_magic ^= ARENA_MAGIC;
    defer->ad_magic ^= (uintptr_t)defer;
    defer->ad_magic ^= (uintptr_t)defer->ad_next;
    defer->ad_magic ^= (uintptr_t)defer->ad_fn;
    defer->ad_magic ^= (uintptr_t)defer->ad_data;

    arena->a_defer = defer;

    return true;
}

/*
 * Same as arena_defer() except the handler is called with a copy
 * of `data`. The copy is allocated on the arena.
 *
 * This is useful when passing several arguments to the cleanup handler using
 * a context structure:
 *
 *  struct ctx
 *  {
 *      .param1 = NULL,
 *      .param2 = 5,
 *      .param6 = 6.0
 *  };
 *
 * arena_defer_copy(arena, cleanup, &ctx, sizeof(ctx));
 */
bool arena_defer_copy(arena_t *arena, arena_defer_fn_t *fn, void *data, size_t sz)
{
    void *copy = arena_malloc(arena, sz);
    if (copy == NULL)
    {
        fn(data);
        return false;
    }
    memcpy(copy, data, sz);
    return arena_defer(arena, fn, copy);
}

/*
 * =============================================================================
 * Arena chunk handling
 * =============================================================================
 */
struct arena_chunk *arena_chunk_new(size_t sz)
{
    sz += sizeof(struct arena_chunk);
    /* Align the size to ARENA_GROW_SIZE */
    sz = (sz + (ARENA_GROW_SIZE - 1)) & ~(ARENA_GROW_SIZE - 1);

    struct arena_chunk *chunk = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (chunk == MAP_FAILED)
    {
        return NULL;
    }

    chunk->ac_pos = 0;
    chunk->ac_size = sz - sizeof(struct arena_chunk);
    return chunk;
}

void arena_chunk_del(struct arena_chunk *ac)
{
    if (munmap(ac, ac->ac_size + sizeof(struct arena_chunk)) != 0)
    {
        LOG(ERR, "arena: Error unmapping arena chunk %p.", ac);
    }
}

void *arena_chunk_push(struct arena_chunk *chunk, size_t sz)
{
    if (chunk == NULL) return NULL;

    if (chunk->ac_pos > (SIZE_MAX - sz))
    {
        LOG(ERR, "arena: Allocation too big (exceeds SIZE_MAX).");
        return NULL;
    }

    if ((chunk->ac_pos + sz) > chunk->ac_size)
    {
        return NULL;
    }

    void *ptr = &chunk->ac_data[chunk->ac_pos];
    chunk->ac_pos += sz;
    return ptr;
}

/*
 * Pop `sz` bytes from the arena. Return the number of bytes that were actually
 * popped.
 */
size_t arena_chunk_pop(struct arena_chunk *chunk, size_t sz)
{
    if (chunk == NULL) return 0;

    if (chunk->ac_pos < sz)
    {
        size_t rc = chunk->ac_pos;
        chunk->ac_pos = 0;
        return rc;
    }

    chunk->ac_pos -= sz;
    return sz;
}

/*
 * Verify whether it is possible to resize the buffer `ptr`/`sz. With arena
 * allocators, this is possible only when `ptr` is exactly at the end of the
 * arena.
 */
bool arena_chunk_check_resize(struct arena_chunk *ac, void *ptr, size_t sz)
{
    if (ac->ac_pos >= sz && (&ac->ac_data[ac->ac_pos - sz] == ptr))
    {
        return true;
    }

    return false;
}

/*
 * =============================================================================
 * Private/support functions
 * =============================================================================
 */

/*
 * Execute defer handlers on the current arena chunk (any handler that has a
 * frame position that is above the current chunk position).
 */
bool arena_defer_cleanup(arena_t *arena, struct arena_chunk *chunk)
{
    while (arena->a_defer != NULL)
    {
        uintptr_t magic = 0;
        magic ^= arena->a_defer->ad_magic;
        magic ^= (uintptr_t)arena->a_defer;
        magic ^= (uintptr_t)arena->a_defer->ad_next;
        magic ^= (uintptr_t)arena->a_defer->ad_fn;
        magic ^= (uintptr_t)arena->a_defer->ad_data;
        if (magic != ARENA_MAGIC)
        {
            arena->a_defer = NULL;
            return false;
        }

        /* Pointer to start and end of the defer block */
        uint8_t *sdefer = (uint8_t *)arena->a_defer;
        uint8_t *edefer = (uint8_t *)&arena->a_defer[1];

        /* Check if defer block is inside this chunk */
        if (sdefer < &chunk->ac_data[0]) return true;
        if (edefer > &chunk->ac_data[chunk->ac_size]) return true;

        /* If the defer is still in allocated space, return */
        if (edefer <= &chunk->ac_data[chunk->ac_pos]) return true;

        arena->a_defer->ad_fn(arena->a_defer->ad_data);
        arena->a_defer = arena->a_defer->ad_next;
    }

    return true;
}

void arena_frame_t_cleanup(void *frame)
{
    arena_restore(*(arena_frame_t *)frame);
}

void arena_t_cleanup(void *arena)
{
    if (arena == NULL) return;
    arena_del(*(arena_t **)arena);
}
