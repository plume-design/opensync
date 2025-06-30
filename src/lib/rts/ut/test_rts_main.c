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

#include <libgen.h>
#include <stddef.h>
#include <stdbool.h>

#include "rts.h"
#include "rts_slob.h"
#include "log.h"
#include "target.h"
#include "unity.h"
#include "rts_priv.h"
#include "rts_slob.h"

void (*g_setUp)(void) = NULL;
void (*g_tearDown)(void) = NULL;

void setUp(void)
{
    LOGI("%s: Unit test setup", __func__);
}

void tearDown(void)
{
    LOGI("%s: Unit test tear down", __func__);
    TEST_ASSERT_TRUE(true);
}

void test_rts_pool_alloc(void)
{
    const size_t POOL_SIZE = 512;
    struct rts_pool pool;
    uint8_t *mem;

    mem = rts_ext_alloc(POOL_SIZE);
    TEST_ASSERT_NOT_NULL(mem);

    rts_pool_init(&pool, mem, POOL_SIZE);

    // Verify initial pool statistics
    TEST_ASSERT_EQUAL_UINT(0, pool.stats.curr_alloc);
    TEST_ASSERT_EQUAL_UINT(0, pool.stats.peak_alloc);
    TEST_ASSERT_EQUAL_UINT(0, pool.stats.fail_alloc);

    // Test 1: Small allocation (less than SLOB_DATA_SIZE)
    void *ptr1 = rts_pool_alloc(&pool, 16);
    TEST_ASSERT_NOT_NULL(ptr1);

    // Verify pool statistics after small allocation
    TEST_ASSERT_GREATER_THAN(0, pool.stats.curr_alloc);
    TEST_ASSERT_EQUAL(pool.stats.curr_alloc, pool.stats.peak_alloc);
    TEST_ASSERT_EQUAL_UINT(0, pool.stats.fail_alloc);

    // Store the current allocation size for later comparison
    size_t alloc_size_after_first = pool.stats.curr_alloc;

    // Test 2: Another small allocation
    void *ptr2 = rts_pool_alloc(&pool, 24);
    TEST_ASSERT_NOT_NULL(ptr2);
    TEST_ASSERT_NOT_EQUAL(ptr1, ptr2);  // Should be different memory locations

    // Verify pool statistics after second allocation
    TEST_ASSERT_GREATER_THAN(alloc_size_after_first, pool.stats.curr_alloc);
    TEST_ASSERT_EQUAL(pool.stats.curr_alloc, pool.stats.peak_alloc);

    // Test 3: Larger allocation (requiring multiple blocks)
    void *ptr3 = rts_pool_alloc(&pool, 64);
    TEST_ASSERT_NOT_NULL(ptr3);

    // Verify pool statistics after large allocation
    TEST_ASSERT_GREATER_THAN(0, pool.stats.curr_alloc);
    TEST_ASSERT_EQUAL(pool.stats.curr_alloc, pool.stats.peak_alloc);

    // Test 4: Free memory and reallocate
    rts_pool_free(&pool, ptr1);
    size_t curr_alloc_after_free = pool.stats.curr_alloc;
    TEST_ASSERT_LESS_THAN(pool.stats.peak_alloc, curr_alloc_after_free);

    void *ptr4 = rts_pool_alloc(&pool, 16);
    TEST_ASSERT_NOT_NULL(ptr4);

    // The peak allocation should remain the same
    TEST_ASSERT_GREATER_THAN(curr_alloc_after_free, pool.stats.curr_alloc);

    // Test 5: Zero-size allocation (should still allocate minimum block)
    void *ptr5 = rts_pool_alloc(&pool, 0);
    TEST_ASSERT_NOT_NULL(ptr5);

    rts_pool_free(&pool, ptr2);
    rts_pool_free(&pool, ptr3);
    rts_pool_free(&pool, ptr4);
    rts_pool_free(&pool, ptr5);

    // Verify all memory is freed
    TEST_ASSERT_EQUAL_UINT(0, pool.stats.curr_alloc);

    // Clean up
    rts_pool_exit(&pool);
    rts_ext_free(mem);
}

void test_alloc(void)
{
    const size_t POOL_SIZE = 256;
    struct rts_pool mem_pool;
    int *integer_data;
    uint8_t *mem;

    /* Allocate pool memory */
    mem = rts_ext_alloc(POOL_SIZE);

    /* Initialize memory pool */
    rts_pool_init(&mem_pool, mem, 200);

    /* Verify initial statistics */
    TEST_ASSERT_EQUAL_UINT(0, mem_pool.stats.curr_alloc);
    TEST_ASSERT_EQUAL_UINT(0, mem_pool.stats.peak_alloc);
    TEST_ASSERT_EQUAL_UINT(0, mem_pool.stats.fail_alloc);

    /* Allocate 40 bytes from the memory pool */
    integer_data = rts_pool_alloc(&mem_pool, 40);
    TEST_ASSERT_NOT_NULL(integer_data);
    *integer_data = 10;

    /* Verify stats after first allocation */
    TEST_ASSERT_GREATER_THAN(0, mem_pool.stats.curr_alloc);
    TEST_ASSERT_EQUAL(mem_pool.stats.curr_alloc, mem_pool.stats.peak_alloc);
    TEST_ASSERT_EQUAL_UINT(0, mem_pool.stats.fail_alloc);

    /* Second allocation */
    void *ptr1 = rts_pool_alloc(&mem_pool, 20);
    TEST_ASSERT_NOT_NULL(ptr1);

    /* Free memory and send back to pool */
    rts_pool_free(&mem_pool, integer_data);
    rts_pool_free(&mem_pool, ptr1);

    /* Verify freeing worked */
    TEST_ASSERT_EQUAL_UINT(0, mem_pool.stats.curr_alloc);
    TEST_ASSERT_GREATER_THAN(0, mem_pool.stats.peak_alloc);

    /* Free memory */
    rts_ext_free(mem);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("walleye", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_INFO);

    UnityBegin(basename(__FILE__));

    RUN_TEST(test_alloc);
    RUN_TEST(test_rts_pool_alloc);

    return UNITY_END();
}
