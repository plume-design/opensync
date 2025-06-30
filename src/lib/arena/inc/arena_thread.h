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

#include <pthread.h>
#include <stdlib.h>

#include "arena.h"
#include "log.h"

/*
 * =============================================================================
 * Initialize a thread-aware scratch arenas
 *
 * This file needs to be included only once (most likely from the main module
 * of the application) if using threads.
 *
 * What this header is that it overrides the arena_scratch_init() function to
 * install pthread hooks (using pthread_setspecific()) to destroy scratch
 * arenas upon thread exit.
 * =============================================================================
 */
void arena_scratch_thread_cleanup(void *p)
{
    (void)p;
    arena_scratch_destroy();
}

pthread_key_t scratch_thread_key;
void arena_scratch_init_once(void)
{
    LOG(INFO, "Multi-threaded mode.");
    pthread_key_create(&scratch_thread_key, arena_scratch_thread_cleanup);
    atexit(arena_scratch_destroy);
}

void arena_scratch_init(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    (void)pthread_once(&once, arena_scratch_init_once);
    pthread_setspecific(scratch_thread_key, (void *)0xdeadbeef);
}
