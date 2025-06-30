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

#ifndef ARENA_H_INCLUDED
#define ARENA_H_INCLUDED

#include <stdbool.h>

#include "log.h"
#include "osa_assert.h"

#include "arena_base.h"
#include "arena_string.h"

static inline void *arena_warn_on_null(void *ptr, const char *file, int line, const char *descr)
{
    if (ptr == NULL)
    {
        LOG(ERR, "arena: Error at %s:%d: %s", file, line, descr);
    }
    return ptr;
}

static inline bool arena_warn_on_false(bool cond, const char *file, int line, const char *descr)
{
    if (!cond)
    {
        LOG(ERR, "arena: Error at %s:%d: %s", file, line, descr);
    }
    return cond;
}

static inline void *arena_assert_on_null(void *ptr, const char *file, int line, const char *descr)
{
    if (ptr == NULL)
    {
        char msg[512];
        snprintf(msg, sizeof(msg), "Assert at %s:%d: %s", file, line, descr);
        LOG(EMERG, "arena: %s", msg);
        ASSERT(false, "Arena FATAL error");
    }
    return ptr;
}

static inline size_t arena_assert_on_size(size_t val, size_t rc, const char *file, int line, const char *descr)
{
    if (val == rc)
    {
        LOG(EMERG, "arena: Assert at %s:%d: %s", file, line, descr);
        ASSERT(false, "Arena FATAL error");
    }

    return rc;
}

#define arena_static_new(...) \
    ((arena_t *)arena_warn_on_null(arena_static_new(__VA_ARGS__), __FILE__, __LINE__, "arena_static_new() failed."))
#define arena_new(...) \
    ((arena_t *)arena_warn_on_null(arena_new(__VA_ARGS__), __FILE__, __LINE__, "arena_new() failed."))
#define arena_del(...) \
    arena_assert_on_size(ARENA_ERROR, arena_del(__VA_ARGS__), __FILE__, __LINE__, "area_del() failed.")
#define arena_push(...) arena_warn_on_null(arena_push(__VA_ARGS__), __FILE__, __LINE__, "arena_push() failed.")
#define arena_pop(...) \
    arena_assert_on_size(ARENA_ERROR, arena_pop(__VA_ARGS__), __FILE__, __LINE__, "area_pop() failed.")
#define arena_get(...)   arena_warn_on_null(arena_get(__VA_ARGS__), __FILE__, __LINE__, "arena_get() failed.")
#define arena_set(...)   arena_assert_on_null(arena_set(__VA_ARGS__), __FILE__, __LINE__, "area_set() failed.")
#define arena_defer(...) arena_warn_on_false(arena_defer(__VA_ARGS__), __FILE__, __LINE__, "arena_defer() failed.")
#define arena_defer_copy(...) \
    arena_warn_on_false(arena_defer_copy(__VA_ARGS__), __FILE__, __LINE__, "arena_defer_copy() failed.")
#define arena_malign(...)  arena_warn_on_null(arena_malign(__VA_ARGS__), __FILE__, __LINE__, "arena_malign() failed.")
#define arena_malloc(...)  arena_warn_on_null(arena_malloc(__VA_ARGS__), __FILE__, __LINE__, "arena_malloc() failed.")
#define arena_calloc(...)  arena_warn_on_null(arena_calloc(__VA_ARGS__), __FILE__, __LINE__, "arena_calloc() failed.")
#define arena_mresize(...) arena_warn_on_null(arena_mresize(__VA_ARGS__), __FILE__, __LINE__, "arena_mresize() failed.")
#define arena_mappend(...) arena_warn_on_null(arena_mappend(__VA_ARGS__), __FILE__, __LINE__, "arena_mappend() failed.")
#define __arena_scratch(...) \
    ((arena_t *)arena_assert_on_null(__arena_scratch(__VA_ARGS__), __FILE__, __LINE__, "__arena_scratch() failed."))
#define arena_memdup(...) \
    ((char *)arena_warn_on_null(arena_memdup(__VA_ARGS__), __FILE__, __LINE__, "arena_memdup() failed."))
#define arena_memcat(...) \
    ((char *)arena_warn_on_null(arena_memcat(__VA_ARGS__), __FILE__, __LINE__, "arena_memcat() failed."))
#define arena_strdup(...) \
    ((char *)arena_warn_on_null(arena_strdup(__VA_ARGS__), __FILE__, __LINE__, "arena_strdup() failed."))
#define arena_strcat(...) \
    ((char *)arena_warn_on_null(arena_strcat(__VA_ARGS__), __FILE__, __LINE__, "arena_strcat() failed."))
#define arena_vprintf(...) \
    ((char *)arena_warn_on_null(arena_vprintf(__VA_ARGS__), __FILE__, __LINE__, "arena_sprintf() failed."))
#define arena_sprintf(...) \
    ((char *)arena_warn_on_null(arena_sprintf(__VA_ARGS__), __FILE__, __LINE__, "arena_sprintf() failed."))
#define arena_vcprintf(...) \
    ((char *)arena_warn_on_null(arena_vcprintf(__VA_ARGS__), __FILE__, __LINE__, "arena_vcprintf() failed."))
#define arena_scprintf(...) \
    ((char *)arena_warn_on_null(arena_scprintf(__VA_ARGS__), __FILE__, __LINE__, "arena_scprintf() failed."))

#endif /* ARENA_H_INCLUDED */
