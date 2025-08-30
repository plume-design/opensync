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

#include <stdio.h>
#include <stdlib.h>

#include "const.h"
#include "log.h"

#include "arena_base.h"

c_thread_local arena_t *arena_scratch_list[ARENA_SCRATCH_NUM] = {0};

void arena_scratch_destroy(void)
{
    for (int s = 0; s < ARRAY_LEN(arena_scratch_list); s++)
    {
        if (arena_scratch_list[s] == NULL) continue;
        arena_del(arena_scratch_list[s]);
        arena_scratch_list[s] = NULL;
    }
}

/*
 * Return a scratch arena. Make sure that the returned
 * arena is not present in the NULL-terminated list `conflicts`
 *
 * Returns NULL on error.
 */
arena_t *__arena_scratch(arena_t **conflicts)
{
    int s = 0;

    if (conflicts != NULL)
    {
        for (s = 0; s < ARENA_SCRATCH_NUM; s++)
        {
            arena_t **c = conflicts;
            for (; *c != NULL; c++)
            {
                if (*c == arena_scratch_list[s]) break;
            }
            /* No conflicts found, break out */
            if (*c == NULL) break;
        }
        if (s >= ARENA_SCRATCH_NUM)
        {
            return NULL;
        }
    }

    if (arena_scratch_list[s] == NULL)
    {
        arena_scratch_init();
        arena_scratch_list[s] = arena_new(0);
        if (arena_scratch_list[s] == NULL)
        {
            return NULL;
        }
    }

    return arena_scratch_list[s];
}

void c_weak arena_scratch_init(void)
{
    static bool init = false;
    for (; !init; init = true)
    {
        LOG(INFO, "arena: Single-threaded mode.");
        atexit(arena_scratch_destroy);
    }
}
