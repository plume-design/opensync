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
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pcap.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <limits.h>

#include "os.h"
#include "util.h"
#include "log.h"
#include "mem_monitor.h"

struct mem_monitor_mgr mgr = {
    .initialized = 0,
    .max_lines = 0,
    .written_lines = 0,
    .fd = -1,
};

static inline struct mem_monitor_mgr *mem_monitor_get_mgr(void)
{
    return &mgr;
}

static inline bool mem_monitor_initialized(void)
{
    struct mem_monitor_mgr *mgr;

    mgr = mem_monitor_get_mgr();
    return (mgr->initialized != 0);
}

static inline bool mem_monitor_stopped(void)
{
    struct mem_monitor_mgr *mgr;

    mgr = mem_monitor_get_mgr();
    if (mgr->initialized == 0) return true;
    if (mgr->written_lines >= mgr->max_lines) return true;

    return false;
}

int mem_monitor_init(int fd, int max_lines)
{
    struct mem_monitor_mgr *mgr;

    if (mem_monitor_initialized()) return 0;

    mgr = mem_monitor_get_mgr();
    mgr->fd = fd;
    mgr->max_lines = max_lines;
    mgr->initialized = 1;

    return 0;
}

int mem_monitor_reinit(int fd, int max_lines)
{
    struct mem_monitor_mgr *mgr;

    mgr = mem_monitor_get_mgr();
    mgr->fd = fd;
    mgr->max_lines = max_lines;
    mgr->initialized = 1;

    return 0;
}

void mem_monitor_log(const char *to_log)
{
    struct mem_monitor_mgr *mgr;
    int ret;

    if (mem_monitor_stopped()) return;

    mgr = mem_monitor_get_mgr();

    mgr = mem_monitor_get_mgr();
    ret = write(mgr->fd, to_log, strlen(to_log));
    if (ret == -1) LOGE("%s: could not  write log: %s", __func__, strerror(errno));

    mgr->written_lines += 1;
}

void mem_monitor_free(void *ptr, const char *caller, const char *file, const int line)
{
    char to_log[MEM_LOG_ITEM_LEN];
    struct mem_monitor_mgr *mgr;
    struct timespec now;
    uint64_t ts;
    int ret;

    if (mem_monitor_stopped())
    {
        free(ptr);
        return;
    }

    clock_gettime(CLOCK_MONOTONIC, &now);
    ts = ((uint64_t)now.tv_sec * 1000000) + (now.tv_nsec / 1000);

    MEMZERO(to_log);
    snprintf(to_log, MEM_LOG_ITEM_LEN, "%s,%" PRIu64 ",%s,%s,%d,%p\n", __func__, ts, caller, file, line, ptr);
    mgr = mem_monitor_get_mgr();
    ret = write(mgr->fd, to_log, strlen(to_log));
    mgr->written_lines += 1;
    if (ret == -1) LOGE("%s: could not  write log: %s", __func__, strerror(errno));
    free(ptr);

    return;
}

void *mem_monitor_malloc(size_t sz, const char *caller, const char *file, const int line)
{
    char to_log[MEM_LOG_ITEM_LEN];
    struct mem_monitor_mgr *mgr;
    struct timespec now;
    uint64_t ts;
    void *ptr;
    int ret;

    ptr = memutil_inline_malloc(sz, caller, file, line);
    if (mem_monitor_stopped()) return ptr;

    clock_gettime(CLOCK_MONOTONIC, &now);
    ts = ((uint64_t)now.tv_sec * 1000000) + (now.tv_nsec / 1000);

    MEMZERO(to_log);
    snprintf(to_log, MEM_LOG_ITEM_LEN, "%s,%" PRIu64 ",%s,%s,%d,%p,%zu\n", __func__, ts, caller, file, line, ptr, sz);
    mgr = mem_monitor_get_mgr();
    ret = write(mgr->fd, to_log, strlen(to_log));
    mgr->written_lines += 1;
    if (ret == -1) LOGE("%s: could not  write log: %s", __func__, strerror(errno));

    return ptr;
}

void *mem_monitor_calloc(size_t n, size_t sz, const char *caller, const char *file, int const line)
{
    char to_log[MEM_LOG_ITEM_LEN];
    struct mem_monitor_mgr *mgr;
    struct timespec now;
    uint64_t ts;
    void *ptr;
    int ret;

    ptr = memutil_inline_calloc(n, sz, caller, file, line);
    if (mem_monitor_stopped()) return ptr;

    clock_gettime(CLOCK_MONOTONIC, &now);
    ts = ((uint64_t)now.tv_sec * 1000000) + (now.tv_nsec / 1000);

    MEMZERO(to_log);
    snprintf(
            to_log,
            MEM_LOG_ITEM_LEN,
            "%s,%" PRIu64 ",%s,%s,%d,%p,%zu\n",
            __func__,
            ts,
            caller,
            file,
            line,
            ptr,
            n * sz);
    mgr = mem_monitor_get_mgr();
    ret = write(mgr->fd, to_log, strlen(to_log));
    mgr->written_lines += 1;
    if (ret == -1) LOGE("%s: could not  write log: %s", __func__, strerror(errno));

    return ptr;
}

void *mem_monitor_realloc(void *cptr, size_t sz, const char *caller, const char *file, int const line)
{
    char to_log[MEM_LOG_ITEM_LEN];
    struct mem_monitor_mgr *mgr;
    struct timespec now;
    uint64_t ts;
    void *ptr;
    int ret;

    ptr = memutil_inline_realloc(cptr, sz, caller, file, line);
    if (mem_monitor_stopped()) return ptr;

    clock_gettime(CLOCK_MONOTONIC, &now);
    ts = ((uint64_t)now.tv_sec * 1000000) + (now.tv_nsec / 1000);

    MEMZERO(to_log);
    snprintf(
            to_log,
            MEM_LOG_ITEM_LEN,
            "%s,%" PRIu64 ",%s,%s,%d,%p,%zu,%p\n",
            __func__,
            ts,
            caller,
            file,
            line,
            ptr,
            sz,
            cptr);
    mgr = mem_monitor_get_mgr();
    ret = write(mgr->fd, to_log, strlen(to_log));
    mgr->written_lines += 1;
    if (ret == -1) LOGE("%s: could not  write log: %s", __func__, strerror(errno));

    return ptr;
}

void *mem_monitor_strdup(const char *str, const char *caller, const char *file, const int line)
{
    char to_log[MEM_LOG_ITEM_LEN];
    struct mem_monitor_mgr *mgr;
    struct timespec now;
    uint64_t ts;
    void *ptr;
    int ret;

    ptr = memutil_inline_strdup(str, caller, file, line);
    if (mem_monitor_stopped()) return ptr;

    clock_gettime(CLOCK_MONOTONIC, &now);
    ts = ((uint64_t)now.tv_sec * 1000000) + (now.tv_nsec / 1000);

    MEMZERO(to_log);
    snprintf(
            to_log,
            MEM_LOG_ITEM_LEN,
            "%s,%" PRIu64 ",%s,%s,%d,%p,%zu\n",
            __func__,
            ts,
            caller,
            file,
            line,
            ptr,
            strlen(str));
    mgr = mem_monitor_get_mgr();
    ret = write(mgr->fd, to_log, strlen(to_log));
    mgr->written_lines += 1;
    if (ret == -1) LOGE("%s: could not  write log: %s", __func__, strerror(errno));

    return ptr;
}

void *mem_monitor_strndup(const char *str, size_t n, const char *caller, const char *file, const int line)
{
    char to_log[MEM_LOG_ITEM_LEN];
    struct mem_monitor_mgr *mgr;
    struct timespec now;
    uint64_t ts;
    void *ptr;
    int ret;

    ptr = memutil_inline_strndup(str, n, caller, file, line);
    if (mem_monitor_stopped()) return ptr;

    clock_gettime(CLOCK_MONOTONIC, &now);
    ts = ((uint64_t)now.tv_sec * 1000000) + (now.tv_nsec / 1000);

    MEMZERO(to_log);
    snprintf(to_log, MEM_LOG_ITEM_LEN, "%s,%" PRIu64 ",%s,%s,%d,%p,%zu\n", __func__, ts, caller, file, line, ptr, n);
    mgr = mem_monitor_get_mgr();
    ret = write(mgr->fd, to_log, strlen(to_log));
    mgr->written_lines += 1;
    if (ret == -1) LOGE("%s: could not  write log: %s", __func__, strerror(errno));

    return ptr;
}

void *mem_monitor_memndup(const void *data, size_t n, const char *caller, const char *file, const int line)
{
    char to_log[MEM_LOG_ITEM_LEN];
    struct mem_monitor_mgr *mgr;
    struct timespec now;
    uint64_t ts;
    void *ptr;
    int ret;

    ptr = memutil_inline_memndup(data, n, caller, file, line);
    if (mem_monitor_stopped()) return memcpy(ptr, data, n);

    clock_gettime(CLOCK_MONOTONIC, &now);
    ts = ((uint64_t)now.tv_sec * 1000000) + (now.tv_nsec / 1000);

    MEMZERO(to_log);
    snprintf(to_log, MEM_LOG_ITEM_LEN, "%s,%" PRIu64 ",%s,%s,%d,%p,%zu\n", __func__, ts, caller, file, line, ptr, n);
    mgr = mem_monitor_get_mgr();
    ret = write(mgr->fd, to_log, strlen(to_log));
    mgr->written_lines += 1;
    if (ret == -1) LOGE("%s: could not  write log: %s", __func__, strerror(errno));

    return memcpy(ptr, data, n);
}
