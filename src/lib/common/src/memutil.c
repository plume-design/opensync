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

#include <stddef.h>
#include <stdlib.h>

#include "memutil.h"

size_t mem_optimized_size(size_t req_size)
{
    size_t nsz = 16;
    size_t m = 16;

    /*
     * Grow buffer using a Fibonacci sequence where nsz and m are the starting
     * parameters. This creates a growth ratio of Phi (golden ratio), which,
     * according to some quick investigation, seems to be a better factor than 2.
     * For reference, it seems that Java and .NET use 1.5 for resizing their
     * arrays.
     */
    while (nsz < req_size)
    {
        nsz += m;
        m = nsz - m;
    }

    return nsz;
}

void* mem_append(void **base, void **cur, size_t sz)
{
    size_t nsz;
    size_t csz;

    if (*cur == NULL) *cur = *base;

    csz = *cur - *base;
    nsz = mem_optimized_size(csz);

    /* Resize the region if needed */
    if (nsz < (csz + sz) || csz == 0)
    {
        nsz = mem_optimized_size(csz + sz);

        /* Reallocate the buffer, adjust the 'base' and 'end' pointers */
        *base = realloc(*base, nsz);
        *cur = *base + csz;
    }

    *cur += sz;
    return *cur - sz;
}
