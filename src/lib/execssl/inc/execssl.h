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

#ifndef EXECSSL_H_INCLUDED
#define EXECSSL_H_INCLUDED

#include "arena.h"
#include "const.h"

/*
 * Convenient wrapper for sslwrap_a(), which packs the variable arguments
 * of the macro into a NULL-terminated char *[] array.
 *
 * Example:
 *
 * sslwrap(stdin_buffer, "arg`", "arg2", "arg3")
 *
 * will roughly translate to
 *
 * sslwrap_a(stdin_buffer, (const char *[]){ "arg1", "arg2", "arg3", NULL })
 */
#define execssl(stdin, ...) execssl_a((stdin), C_CVPACK(__VA_ARGS__))

/*
 * Arena variant wrapper
 */
#define execssl_arena(arena, stdin, ...) execssl_arena_a((arena), (stdin), C_CVPACK(__VA_ARGS__))

/*
 * Execute `openssl` with arguments `args`. `cstdin` is used as openssl's
 * stdin. The function returns an allocated string buffer which represents
 * openssl's output.
 *
 * stderr is logged at INFO level to the logger.
 *
 * In case openssl exits a non-zero return value, this function returns NULL.
 */
char *execssl_a(const char *cstdin, const char *argv[]);

/*
 * Arena allocator variant of execcsl_a()
 */
char *execssl_arena_a(arena_t *arena, const char *cstdin, const char *argv[]);

#endif /* EXECSSL_H */
