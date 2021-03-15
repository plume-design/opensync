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

#ifndef MEMUTIL_H_INCLUDED
#define MEMUTIL_H_INCLUDED

#include <stddef.h>

#include "osa_assert.h"

#define MEMUTIL_MAGIC       0xdeadbeef

/*
 * ===========================================================================
 *  Memory wrappers. Rationale:
 *
 *  Some systems have memory overcommit enabled, which guarantees that malloc
 *  will never ever return NULL. However on systems where malloc does properly
 *  return NULL, handling these errors may just cause the program to crash
 *  elsewhere or cause a general malfunction of the process.
 *
 *  It seems that the best scenario is to just log the condition and crash the
 *  process as soon as this happens.
 * ===========================================================================
 */

#define MALLOC(sz)      memutil_inline_malloc(sz, __func__, __FILE__, __LINE__)
#define CALLOC(n, sz)   memutil_inline_calloc(n, sz, __func__, __FILE__, __LINE__)
#define REALLOC(sz)     memutil_inline_realloc(sz, __func__, __FILE__, __LINE__)
#define STRDUP(sz)      memutil_inline_strdup(sz, __func__, __FILE__, __LINE__)

/**
 * Perform a double free sanity check by setting the freed pointer value to
 * a magic number. If the value of the pointer is unchanged, a second call
 * to this function will result in an assert.
 *
 * FREE() cannot be used on non-pointers. Therefore the @p ptr argument must be
 * a pointer variable. For other cases, free() should be used.
 *
 * The double pointer below serves two purposes:
 * - Preventing a double expansion
 * - Making sure FREE() is used on l-values
 *   (For example, this is invalid: `FREE(get_alloc_ptr())`)
 *
 */
#define FREE(ptr)                               \
do                                              \
{                                               \
    void **cptr = (void **)&(ptr);              \
    if (*cptr == NULL) break;                   \
    if (*cptr == (void *)MEMUTIL_MAGIC)         \
    {                                           \
        osa_assert_dump(                        \
                "double free()",                \
                __func__,                       \
                __FILE__,                       \
                __LINE__,                       \
                "Double free detected.");       \
    }                                           \
                                                \
    free(*cptr);                                \
    *cptr = (void *)MEMUTIL_MAGIC;              \
}                                               \
while (0)

/*
 * Calculate a size that is larger or equal to @p req_size, and is optimized
 * for appending to a buffer (to reduce the number of reallocations).
 * The same approach can be used for allocating larger-than-needed buffers
 * for short-lived allocations to reduce the chance for heap fragmentation.
 */
size_t mem_optimized_size(size_t req_size);

/*
 * Allocate a contiguous memory region and return an address of @p sz bytes at
 * the end of the region specified by @p base and @p cur. The memory region
 * pointed to by @p base is reallocated if needed.
 *
 * Due to reallocations the return values from previous calls may be invalidated
 * by subsequent calls to this function.
 *
 * Example:
 *
 *      char *p;
 *      int ii;
 *
 *      char *buf = NULL;
 *      char *buf_e = NULL;
 *
 *      for (ii = 0; ii < argc; ii++)
 *      {
 *          p = mem_append(&buf, &buf_e, strlen(argv[ii]) + 1);
 *          strcpy(p, argv[ii]);
 *      }
 *
 *      for (p = buf; p < buf_e; p += strlen(p) + 1)
 *      {
 *          printf("ARG: %s\n", p);
 *      }
 *
 *      free(buf);
 *
 *
 * To initialize an existing buffer for appending, first call mem_append() with
 * @p cur set to NULL and specify the array length in @p sz.
 *
 * Example:
 *
 *      char *p = strdup("hello");
 *      char *e = NULL;
 *
 *      mem_append(&p, &e, strlen(p) + 1);
 */

#define MEM_APPEND(base, cur, sz) mem_append((void **)(base), (void **)(cur), (sz))
void* mem_append(void **base, void **cur, size_t sz);

/* Pull in static inline functions */
#include "../src/memutil.c.h"

#endif /* MEMUTIL_H_INCLUDED */
