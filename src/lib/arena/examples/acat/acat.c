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

/*
 * =============================================================================
 * Simple cat-like utility implemented using the arena allocator for
 * illustration purposes.
 * =============================================================================
 */

#include <stdio.h>
#include <unistd.h>

#include "arena.h"
#include "arena_util.h"
#include "log.h"

/*
 * Read a file to a buffer that is dynamically allocated
 * from arena `arena`.
 *
 * This function should produce no memory or resource leaks:
 *
 *  - a scoped scratch arena is created just for the purpose
 *    of installing a defer handler for fopen(). The context
 *    of the scratch arena is always destroyed on function
 *    exit so the FILE object is always cleaned up
 *
 *  - the context of the `arena` is remembered at the beginning
 *    of the function (arena_frame_auto_t). Whenever the
 *    function returns due to an error, the arena is restored
 *    to the state prior to the function call. When we're done
 *    reading the file, the current arena context, which includes
 *    the newly created buffer, is saved preserving it on
 *    function return.
 */
char *get_file(arena_t *arena, const char *path)
{
    /*
     * Get a scoped scratch arena just for fclose defer handler.
     */
    ARENA_SCRATCH(scratch, arena);

    /* Restore the arena state on exit */
    arena_frame_auto_t af = arena_save(arena);

    FILE *f;
    char buf[64];
    char *str;

    f = fopen(path, "r");
    if (f == NULL)
    {
        LOG(ERR, "Error: Unable to open file: %s", path);
        return NULL;
    }

    if (!arena_defer_fclose(scratch, f))
    {
        return NULL;
    }

    str = arena_strdup(arena, "");
    if (str == NULL)
    {
        LOG(ERR, "Unable to allocate initial buffer, out of memory.");
        return NULL;
    }

    while (fgets(buf, sizeof(buf), f) != NULL)
    {
        str = arena_strcat(arena, str, buf);
        if (str == NULL)
        {
            LOG(ERR, "File too big, out of memory.");
            return NULL;
        }
    }

    if (!feof(f))
    {
        LOG(ERR, "Error reading file: %s", path);
        return NULL;
    }

    /*
     * Remember the current state of the arena so the buffer is not cleared
     * when we go out of scope (remember that arena_frame_auto_t) at the
     * beginning of the function?).
     */
    af = arena_save(arena);

    return str;
}

int main(int argc, char *argv[])
{
    log_open("acat", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_DEBUG);

    if (argc < 2)
    {
        LOG(NOTICE, "Not enough parameters. Please specify a file.");
        return 1;
    }

    arena_t *arena = arena_new(2048);
    char *buf = get_file(arena, argv[1]);
    if (buf != NULL)
    {
        write(1, buf, strlen(buf));
    }
    arena_del(arena);
    return 0;
}
