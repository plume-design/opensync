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

#ifndef MEM_MONITOR_H_INCLUDED
#define MEM_MONITOR_H_INCLUDED

#define MEM_LOG_ITEM_LEN 512

#ifdef CONFIG_MEM_MONITOR

#include <stddef.h>

struct mem_monitor_mgr
{
    int initialized;
    int fd;
    int max_lines;
    int written_lines;
};

int mem_monitor_init(int fd, int max_lines);

int mem_monitor_reinit(int fd, int max_lines);

void mem_monitor_log(const char *to_log);

void mem_monitor_free(void *ptr, const char *caller, const char *file, const int line);
void *mem_monitor_malloc(size_t sz, const char *caller, const char *file, const int line);
void *mem_monitor_calloc(size_t n, size_t sz, const char *caller, const char *file, const int line);
void *mem_monitor_realloc(void *cptr, size_t sz, const char *caller, const char *file, const int line);
void *mem_monitor_strdup(const char *str, const char *caller, const char *file, const int line);
void *mem_monitor_strndup(const char *str, size_t n, const char *caller, const char *file, const int line);
void *mem_monitor_memndup(const void *data, size_t n, const char *caller, const char *file, const int line);

#undef FREE
#define FREE(ptr)                                              \
    do                                                         \
    {                                                          \
        void **cptr = (void **)&(ptr);                         \
        if (*cptr == NULL) break;                              \
                                                               \
        CHECK_DOUBLE_FREE(*cptr);                              \
                                                               \
        mem_monitor_free(*cptr, __func__, __FILE__, __LINE__); \
        *cptr = (void *)MEMUTIL_MAGIC;                         \
    } while (0)

#undef MALLOC
#define MALLOC(sz) mem_monitor_malloc(sz, __func__, __FILE__, __LINE__)

#undef CALLOC
#define CALLOC(n, sz) mem_monitor_calloc(n, sz, __func__, __FILE__, __LINE__)

#undef REALLOC
#define REALLOC(cptr, sz) mem_monitor_realloc(cptr, sz, __func__, __FILE__, __LINE__)

#undef STRDUP
#define STRDUP(str) mem_monitor_strdup(str, __func__, __FILE__, __LINE__)

#undef STRNDUP
#define STRNDUP(str, n) mem_monitor_strndup(str, n, __func__, __FILE__, __LINE__)

#undef MEMNDUP
#define MEMNDUP(data, n) mem_monitor_memndup(data, n, __func__, __FILE__, __LINE__)

#else
static inline int mem_monitor_init(int fd, int max_lines)
{
    return 0;
}

static inline int mem_monitor_reinit(int fd, int max_lines)
{
    return 0;
}

static inline void mem_monitor_log(const char *to_log)
{
    return;
}

#endif /* CONFIG_MEM_MONITOR */

#endif /* MEM_MONITOR_H_INCLUDED */
