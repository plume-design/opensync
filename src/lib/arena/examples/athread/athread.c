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
 * Example how to use the arena allocator in threaded mode
 * =============================================================================
 */

#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

/*
 * Only the main module should ever include "arena_thread.h"; this header
 * overrides the initialization function for scratch arenas to use
 * a thread safe variant. However, this is available only when linking
 * with pthreads
 */
#include "arena_thread.h"
#include "arena.h"
#include "log.h"

void thread_pr_exit(arena_t *a, void *data)
{
    (void)a;
    intptr_t id = (intptr_t)data;

    LOG(INFO, "Thread [%" PRIdPTR "] exiting ...", id);
}

void *thread_fn(void *data)
{
    /*
     * Allocate a defer handler on the scratch arena -- it should be when the
     * scratch arena is destroyed on thread exit.
     */
    arena_t *s1 = arena_scratch();
    arena_defer(s1, thread_pr_exit, data);
    usleep(random() % 1000000);
    return NULL;
}

#define NTHREADS 16

int main(int argc, char *argv[])
{
    intptr_t ii;

    srandom(getpid());

    log_open("athread", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_DEBUG);

    pthread_t thr[NTHREADS];
    for (ii = 0; ii < NTHREADS; ii++)
    {
        pthread_create(&thr[ii], NULL, thread_fn, (void *)ii);
    }

    for (ii = 0; ii < NTHREADS; ii++)
    {
        pthread_join(thr[ii], NULL);
    }

    return 0;
}
