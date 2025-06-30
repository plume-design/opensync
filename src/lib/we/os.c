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

#include "memutil.h"
#include <stdint.h>
#include <kconfig.h>
#include "vm.h"
#include "log.h"

/**
 * Allocate memory from the WE memory pool
 *
 * @param sz Size in bytes to allocate
 * @return Pointer to allocated memory or NULL if allocation failed
 */
static void *we_allocate_using_mem_pool(size_t sz)
{
    struct we_mem_pool *mem_pool;
    void *ptr;

    mem_pool = we_get_mem_pool_mgr();
    ptr = rts_pool_alloc(&mem_pool->we_mem_pool, sz);

    return ptr;
}

/**
 * Allocate and zero-initialize memory from the WE memory pool
 *
 * @param sz Size in bytes to allocate
 * @return Pointer to allocated memory or NULL if allocation failed
 */
static void *we_calloc_using_mem_pool(size_t sz)
{
    void *ptr;

    ptr = we_allocate_using_mem_pool(sz);
    if (ptr == NULL) return NULL;

    memset(ptr, 0, sz);
    return ptr;
}

/**
 * Free memory previously allocated from the WE memory pool
 *
 * @param data Pointer to memory to free
 */
static void we_mem_pool_free(void *data)
{
    struct we_mem_pool *mem_pool;

    if (data == NULL) return;

    mem_pool = we_get_mem_pool_mgr();
    rts_pool_free(&mem_pool->we_mem_pool, data);
}

__attribute__((weak)) void *we_malloc(size_t sz)
{
    if (kconfig_enabled(CONFIG_WE_AGENT_MEMPOOL))
    {
        return we_allocate_using_mem_pool(sz);
    }
    else
    {
        return MALLOC(sz);
    }
}

__attribute__((weak)) void *we_calloc(size_t nmemb, size_t sz)
{
    if (kconfig_enabled(CONFIG_WE_AGENT_MEMPOOL))
    {
        return we_calloc_using_mem_pool(sz * nmemb);
    }
    else
    {
        return CALLOC(nmemb, sz);
    }
}

__attribute__((weak)) void we_free(void *p)
{
    if (kconfig_enabled(CONFIG_WE_AGENT_MEMPOOL))
    {
        we_mem_pool_free(p);
    }
    else
    {
        FREE(p);
    }
}
