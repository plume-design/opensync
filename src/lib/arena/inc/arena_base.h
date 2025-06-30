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

#ifndef ARENA_BASE_H_INCLUDED
#define ARENA_BASE_H_INCLUDED

#include <assert.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "const.h"

#define ARENA_ERROR SIZE_MAX

/* Arena magic number */
#if UINTPTR_MAX <= 0xFFFFFFFF
#define ARENA_MAGIC 0xAA55AA55
#else
#define ARENA_MAGIC 0xAA55AA55AA55AA55
#endif

/*
 * Dynamic arenas are expanded by at least ARENA_GROW_SIZE bytes
 */
#define ARENA_GROW_SIZE (16 * 1024)

/*
 * The alignment used by arena_malloc()
 */
#define ARENA_ALIGN _Alignof(max_align_t)

#define arena_auto_t       c_auto(arena_t)
#define arena_frame_auto_t c_auto(arena_frame_t)

/* Number of scratch arenas */
#define ARENA_SCRATCH_NUM 4

/*
 * Overhead of a static arena allocation -- when initializing an arena from
 * a buffer, this is the number of bytes the arena will use for internal
 * structures on the buffer
 */
#define ARENA_STATIC_SZ (sizeof(struct arena_static))

/*
 * Shortcut for the common pattern of allocating a scope-bound scratch
 * arena
 */
#define ARENA_SCRATCH(x, ...)                \
    arena_t *x = arena_scratch(__VA_ARGS__); \
    assert(x != NULL);                       \
    arena_frame_auto_t C_CONCAT(__auto_, __LINE__) = arena_save(x)

#define ARENA_STACK(x, sz)                                                                         \
    arena_auto_t *x = arena_new_static((char[(sz) + ARENA_STATIC_SZ]){0}, (sz) + ARENA_STATIC_SZ); \
    assert(x != NULL)

typedef struct arena arena_t;
typedef struct arena_frame arena_frame_t;
typedef void arena_defer_fn_t(void *data);
typedef void arena_del_fn_t(arena_t *a);

struct arena
{
    struct arena_chunk *a_top;   /* Top most (most recent) chunk */
    size_t a_pos;                /* Current arena position */
    size_t a_size;               /* 0 if dynamic, otherwise the size of the static arena */
    struct arena_defer *a_defer; /* Deferer list */
    arena_del_fn_t *a_del_fn;    /* Delete function */
};

struct arena_chunk
{
    size_t ac_pos;               /* Current chunk position */
    size_t ac_size;              /* Chunk size */
    struct arena_chunk *ac_next; /* Next chunk */
    uint8_t ac_data[0];          /* Chunk data */
};

/* Structure for static arena allocations */
struct arena_static
{
    arena_t as_arena;
    struct arena_chunk as_chunk;
};

struct arena_frame
{
    uintptr_t af_magic;
    arena_t *af_arena;
    size_t af_pos;
};

struct arena_defer
{
    uintptr_t ad_magic;
    arena_defer_fn_t *ad_fn;
    void *ad_data;
    void *ad_next;
};

/*
 * Public arena API
 */
arena_t *arena_new_static(void *buf, size_t sz);
arena_t *arena_new(size_t sz);
size_t arena_del(arena_t *arena);
void *arena_push(arena_t *arena, size_t sz);
size_t arena_pop(arena_t *arena, size_t sz);
void *arena_get(arena_t *arena);
size_t arena_set(arena_t *arena, void *ptr);
arena_frame_t arena_save(arena_t *arena);
void arena_restore(arena_frame_t frame);
bool arena_defer(arena_t *arena, arena_defer_fn_t *fn, void *data);
bool arena_defer_copy(arena_t *arena, arena_defer_fn_t *fn, void *data, size_t sz);
void *arena_malloc(arena_t *arena, size_t sz);
void *arena_calloc(arena_t *arena, size_t sz);
void *arena_mresize(arena_t *arena, void *ptr, size_t oldsz, size_t newsz);
void *arena_mappend(arena_t *arena, void *ptr, size_t membsz, size_t cur, size_t grow);

/* Auto-cleanup function for arena and arena frames */
void arena_t_cleanup(void *);
void arena_frame_t_cleanup(void *);
bool __arena_pop(arena_t *arena, size_t sz);
bool __arena_restore(arena_frame_t frame);

/*
 * Scratch arena API
 */
void arena_scratch_destroy(void);
void arena_scratch_init(void);
/* Wrapper around __arena_scratch() */
#define arena_scratch(...) __arena_scratch(C_XPACK(arena_t *[], NULL, ##__VA_ARGS__))
arena_t *__arena_scratch(arena_t **conflicts);

#endif /* ARENA_BASE_H_INCLUDED */
