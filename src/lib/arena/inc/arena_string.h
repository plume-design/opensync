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

#ifndef ARENA_STRING_H_INCLUDED
#define ARENA_STRING_H_INCLUDED

#include <stdarg.h>

#include "arena_base.h"

/*
 * =============================================================================
 * String utilities
 * =============================================================================
 */
void *arena_memdup(arena_t *arena, const void *src, size_t srcsz);
void *arena_memcat(arena_t *arena, void *dst, size_t dstsz, const void *src, size_t srcsz);
char *arena_strdup(arena_t *arena, const char *src);
char *arena_strcat(arena_t *arena, char *dst, const char *src);
char *arena_vprintf(arena_t *arena, const char *fmt, va_list ap) c_fmt_arg(2);
char *arena_sprintf(arena_t *arena, const char *fmt, ...) c_fmt(2, 3);
char *arena_vcprintf(arena_t *arena, char *src, const char *fmt, va_list ap);
char *arena_scprintf(arena_t *arena, char *src, const char *fmt, ...) c_fmt(3, 4);

#endif
