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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "arena_base.h"

/*
 * =============================================================================
 * Public API
 * =============================================================================
 */

/*
 * Create a copy of buffer `src` allocated on the arena.
 */
void *arena_memdup(arena_t *arena, const void *src, size_t srcsz)
{
    void *dst;

    dst = arena_malloc(arena, srcsz);
    if (dst == NULL) return NULL;

    memcpy(dst, src, srcsz);
    return dst;
}

/*
 * Append memory buffer `src` to memory buffer `dst`. The `dst` buffer is resized
 * to fit the new buffer. `dst` + `dstsz` must point to the end of the arena or
 * this function will fail.
 *
 * If this function fails, the `dst` buffer is left intact.
 */
void *arena_memcat(arena_t *arena, void *dst, size_t dstsz, const void *src, size_t srcsz)
{
    dst = arena_mresize(arena, dst, dstsz, dstsz + srcsz);
    if (dst == NULL) return NULL;

    memcpy(dst + dstsz, src, srcsz);
    return dst;
}

/*
 * Allocate a string on the arena
 */
char *arena_strdup(arena_t *arena, const char *src)
{
    char *dst;

    size_t ssz = strlen(src) + 1;

    dst = arena_push(arena, ssz);
    if (dst == NULL) return NULL;

    return strcpy(dst, src);
}

/*
 * Append string to src. This is possible only if `src` sits at the end of the
 * arena.
 *
 * If this function fails, it returns NULL and the `dst` buffer is left intact.
 */
char *arena_strcat(arena_t *arena, char *dst, const char *src)
{
    dst = arena_mresize(arena, dst, strlen(dst) + 1, strlen(dst) + strlen(src) + 1);
    if (dst == NULL) return NULL;

    strcat(dst, src);
    return dst;
}

/*
 * printf function that allocates space for the output string on the arena
 */
char *arena_vprintf(arena_t *arena, const char *fmt, va_list va)
{
    char *p;
    char sbuf[128];
    int len;

    /*
     * The idea here is to first print to a small buffer. If the string fits,
     * just copy it to the arena.
     *
     * If it fails, we know the size of the string, so we can allocate the right
     * amount of memory and do a 2nd call to vsnprintf() with the correct length.
     */
    len = vsnprintf(sbuf, sizeof(sbuf), fmt, va);
    if (len < 0) return NULL;

    /* Allocate memory on the arena */
    p = arena_push(arena, len + 1);
    if (p == NULL) return NULL;

    if ((size_t)len < sizeof(sbuf))
    {
        /* Simply copy the data */
        strcpy(p, sbuf);
    }
    else
    {
        /*
         * The `sbuf` buffer is truncated, but at least we know the size.
         * Allocate enough memory on the arena and re-run vsnprintf().
         */
        vsnprintf(p, len + 1, fmt, va);
    }

    return p;
}

/*
 * Print string to allocated arena buffer
 */
char *arena_sprintf(arena_t *arena, const char *fmt, ...)
{
    char *retval;
    va_list va;

    va_start(va, fmt);
    retval = arena_vprintf(arena, fmt, va);
    va_end(va);

    return retval;
}

/*
 * Similar to arena_vprintf(), except the string is concatenated to `str`. The
 * null terminator of the string `str` must point to the end of the arena.
 *
 * If this function fails, the `str` buffer is left intact.
 */
char *arena_vcprintf(arena_t *arena, char *str, const char *fmt, va_list va)
{
    char sbuf[128];
    int len;

    /*
     * The idea here is to first print to a small buffer. If the string fits,
     * just copy it to the arena.
     *
     * If it fails, we know the size of the string, so we can allocate the right
     * amount of memory and do a 2nd call to vsnprintf() with the correct length.
     */
    len = vsnprintf(sbuf, sizeof(sbuf), fmt, va);
    if (len < 0)
    {
        return NULL;
    }

    size_t slen = (str == NULL) ? 0 : strlen(str);
    str = arena_mresize(arena, str, slen + 1, slen + len + 1);
    if (str == NULL) return NULL;

    if ((size_t)len < sizeof(sbuf))
    {
        memcpy(str + slen, sbuf, len + 1);
    }
    else
    {
        /*
         * The `sbuf` buffer is truncated, but at least we know the size.
         * Allocate enough memory on the arena and re-run vsnprintf().
         */
        vsnprintf(str + strlen(str), len + 1, fmt, va);
    }

    return str;
}

/*
 * Concatenate printed string to arena buffer; The null terminator of the string
 * `str` must point to the end of the arena.
 *
 * If this function fails, the buffer `src` is left intact.
 */
char *arena_scprintf(arena_t *arena, char *str, const char *fmt, ...)
{
    va_list va;

    va_start(va, fmt);
    str = arena_vcprintf(arena, str, fmt, va);
    va_end(va);

    return str;
}
