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

#include <unistd.h>

#include "log.h"
#include "memutil.h"

#include "arena_util.h"

static void arena_defer_free_fn(void *ptr);
static void arena_defer_close_fn(void *ptr);
static void arena_defer_fclose_fn(void *ptr);
static void arena_defer_arena_fn(void *ptr);

/*
 * =============================================================================
 * Public interface
 * =============================================================================
 */

/*
 * Defer a FREE call
 */
bool arena_defer_free(arena_t *a, void *ptr)
{
    if (ptr == NULL) return false;

    return arena_defer(a, arena_defer_free_fn, ptr);
}

/*
 * Defer another arena
 */
bool arena_defer_arena(arena_t *a, arena_t *defer_arena)
{
    if (defer_arena == NULL) return false;

    return arena_defer(a, arena_defer_arena_fn, defer_arena);
}

/*
 * Defer handler for a file descriptor
 */
bool arena_defer_close(arena_t *a, int fd)
{
    return arena_defer(a, arena_defer_close_fn, (void *)(uintptr_t)fd);
}

/*
 * Defer handler for a FILE object
 */
bool arena_defer_fclose(arena_t *a, FILE *f)
{
    if (f == NULL) return false;

    return arena_defer(a, arena_defer_fclose_fn, f);
}

/*
 * =============================================================================
 * Private functions and helpers
 * =============================================================================
 */
static void arena_defer_free_fn(void *ptr)
{
    FREE(ptr);
}

static void arena_defer_arena_fn(void *ptr)
{
    if (arena_del((arena_t *)ptr) == ARENA_ERROR)
    {
        LOG(ERR, "arena: Deletion of deferred arena failed.");
    }
}

static void arena_defer_close_fn(void *ptr)
{
    close((uintptr_t)ptr);
}

static void arena_defer_fclose_fn(void *ptr)
{
    fclose((FILE *)ptr);
}
