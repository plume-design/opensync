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

#include <stdlib.h>
#include <string.h>

static inline void* memutil_inline_malloc(
        size_t sz,
        const char *func,
        const char *file,
        const int line)
{
    void *ptr = malloc(sz);
    if (ptr == NULL)
    {
        osa_assert_dump("malloc() returned NULL", func, file, line, "Out of memory.");
    }

    return ptr;
}

static inline void* memutil_inline_calloc(
        size_t n,
        size_t sz,
        const char *func,
        const char *file,
        const int line)
{
    void *ptr = calloc(n, sz);
    if (ptr == NULL)
    {
        osa_assert_dump("calloc() returned NULL", func, file, line, "Out of memory.");
    }

    return ptr;
}

static inline void* memutil_inline_realloc(
        void *cptr,
        size_t sz,
        const char *func,
        const char *file,
        const int line)
{
    void *ptr = realloc(cptr, sz);
    if (ptr == NULL && sz > 0)
    {
        osa_assert_dump("realloc() returned NULL", func, file, line, "Out of memory.");
    }

    return ptr;
}

static inline void* memutil_inline_strdup(
        const char *str,
        const char *func,
        const char *file,
        const int line)
{
    char *ptr = strdup(str);
    if (ptr == NULL)
    {
        osa_assert_dump("strdup() returned NULL", func, file, line, "Out of memory.");
    }

    return ptr;
}
