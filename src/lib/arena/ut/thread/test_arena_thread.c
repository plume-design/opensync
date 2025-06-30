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

#include <unistd.h>

#include "arena_thread.h"
#include "log.h"
#include "unit_test_utils.h"
#include "unity.h"

/*
 * =============================================================================
 * Test whether scratch arenas are destroyed when a thread is terminated
 * =============================================================================
 */
static void test_thread_defer_fn(void *data)
{
    int *a = data;
    (*a)++;
}

static void *test_arena_thread_fn(void *data)
{
    arena_t *s1 = arena_scratch();
    arena_t *s2 = arena_scratch(s1);
    arena_t *s3 = arena_scratch(s1, s2);

    arena_defer(s1, test_thread_defer_fn, data);
    arena_defer(s2, test_thread_defer_fn, data);
    arena_defer(s3, test_thread_defer_fn, data);

    return NULL;
}

void test_arena_thread(void)
{
    pthread_t t;

    int a = 0;

    TEST_ASSERT_TRUE(ARENA_SCRATCH_NUM >= 3);
    TEST_ASSERT_TRUE(pthread_create(&t, NULL, test_arena_thread_fn, &a) == 0);
    TEST_ASSERT_TRUE(pthread_join(t, NULL) == 0);
    TEST_ASSERT_TRUE(a == 3);
}

int main(int argc, char *argv[])
{
    log_open("test_arena_thread", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_DEBUG);

    ut_init("test_arena_thread", NULL, NULL);

    RUN_TEST(test_arena_thread);

    ut_fini();
}
