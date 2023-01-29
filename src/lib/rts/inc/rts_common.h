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

#ifndef RTS_COMMON_H
#define RTS_COMMON_H

#define EXPORT __attribute__((visibility("default")))

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "rts_config.h"

#define XSTR(s) STR(s)
#define STR(s)  #s

#ifdef KERNEL
#define rts_assert(cond) (void)(cond)
#define rts_assert_msg(cond, msg, ...) (void)(cond); (void)msg
#define rts_printf(...)
#else
#ifdef NDEBUG
#define rts_assert(cond) (void)(cond)
#define rts_assert_msg(cond, msg, ...) (void)(cond); (void)msg
#define rts_printf(...) \
    do { \
        if (rts_ext_log) { \
            char _buf[512]; \
            __builtin_snprintf(_buf, sizeof(_buf), ##__VA_ARGS__); \
            rts_ext_log(_buf); \
        } \
    } while (0)

#else
#include <assert.h>
#include <stdio.h>
#define rts_assert(cond) \
    do { \
        if (rts_ext_log) { \
            if (!(cond)) rts_ext_log(__FILE__ ":" XSTR(__LINE__) ": Assertion '" #cond "' failed."); \
        } else { \
            assert(cond); \
        } \
    } while (0)

#define rts_assert_msg(cond, msg, ...) \
    do { \
        if (rts_ext_log) { \
            if (!(cond)) { \
                char _buf[512]; \
                __builtin_snprintf(_buf, sizeof(_buf), __FILE__ ":" XSTR(__LINE__) ": Assertion '" #cond "' failed (" msg ").", ##__VA_ARGS__); \
                rts_ext_log(_buf); \
            } \
        } else { \
            if (!(cond)) \
                __builtin_fprintf(stderr, __FILE__ ":" XSTR(__LINE__) ": Assertion '" #cond "' failed (" msg ").\n", ##__VA_ARGS__); \
            assert(cond); \
        } \
    } while (0)

#define rts_printf(...) \
    do { \
        if (rts_ext_log) { \
            char _buf[512]; \
            __builtin_snprintf(_buf, sizeof(_buf), ##__VA_ARGS__); \
            rts_ext_log(_buf); \
        } else { \
            __builtin_fprintf(stderr, ##__VA_ARGS__); \
        } \
    } while (0)

#endif
#endif

#define rts_static_assert(cond, msg) _Static_assert(cond, msg)

#define rts_container_of(ptr, type, member) \
    (type *)((char *)ptr - __builtin_offsetof(type, member))

#define rts_array_size(x) (sizeof(x) / sizeof((x)[0]))

#endif
