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

#include "const.h"
#include "log.h"
#include "unit_test_utils.h"
#include "unity.h"

#include "arena_base.h"
#include "arena_string.h"
#include "arena_util.h"

/*
 * =============================================================================
 * Test basic arena functionality: creation, deletion, push, pop
 * =============================================================================
 */
void test_arena_basic(void)
{
    arena_t *c = arena_new(1024);
    TEST_ASSERT_FALSE(c == NULL);
    TEST_ASSERT_FALSE(arena_push(c, 512) == NULL);
    TEST_ASSERT_FALSE(arena_push(c, 512) == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, 0) == 0);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) == 1024);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) == 0);
    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test arena corner cases around out of memory conditions (in terms of arena
 * space, not system wide)
 *
 * Allocate a chunk below page size, so the arena uses malloc
 * =============================================================================
 */
void test_arena_oom_malloc(void)
{
    arena_t *c = arena_new(1024);
    TEST_ASSERT_FALSE(c == NULL);
    TEST_ASSERT_FALSE(arena_push(c, 512) == NULL);
    TEST_ASSERT_FALSE(arena_push(c, 511) == NULL);
    TEST_ASSERT_FALSE(arena_push(c, 1) == NULL);
    /* The next allocation should fail */
    TEST_ASSERT_TRUE(arena_push(c, 1) == NULL);
    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test arena corner cases around push/pop
 * =============================================================================
 */
void test_arena_push_pop(void)
{
    arena_t *c = arena_new(32);
    TEST_ASSERT_FALSE(c == NULL);
    TEST_ASSERT_FALSE(arena_push(c, 16) == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, 16) == 16);
    TEST_ASSERT_FALSE(arena_push(c, 32) == NULL);
    TEST_ASSERT_TRUE(arena_push(c, 1) == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, 1) == 1);
    TEST_ASSERT_FALSE(arena_push(c, 1) == NULL);
    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test arena corner cases around trim
 * =============================================================================
 */
void test_arena_set_get(void)
{
    arena_t *c;
    void *p0, *p1, *p2;

    c = arena_new(0);
    TEST_ASSERT_FALSE(c == NULL);

    /* Test some corner cases first */
    TEST_ASSERT_TRUE(arena_set(c, (void *)0xdeadbeef) == ARENA_ERROR);
    TEST_ASSERT_TRUE(arena_set(c, NULL) == ARENA_ERROR);

    /* Test trimming of a contiguous buffer */
    p0 = arena_push(c, 100);
    TEST_ASSERT_FALSE(p0 == NULL);
    TEST_ASSERT_TRUE(arena_set(c, p0 + 101) == ARENA_ERROR);
    TEST_ASSERT_TRUE(arena_set(c, p0 + 100) == 0);
    TEST_ASSERT_TRUE(arena_set(c, p0 + 10) != ARENA_ERROR);
    TEST_ASSERT_TRUE(arena_get(c) == (p0 + 10));
    /* Ensure only 10 bytes were left */
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) == 10);
    TEST_ASSERT_TRUE(c->a_top == NULL);

    /* Allocate the first chunk */
    p0 = arena_get(c);
    TEST_ASSERT_FALSE(p0 == NULL);

    TEST_ASSERT_FALSE(arena_push(c, 1) == NULL);
    p1 = arena_get(c);
    TEST_ASSERT_FALSE(p1 == NULL);

    /* Allocate a region that is larger than the default chunk size so push
     * is force to allocate a new chunk */
    p2 = arena_push(c, ARENA_GROW_SIZE);
    TEST_ASSERT_FALSE(p2 == NULL);

    /* Now trimming of p2, should delete the top most chunk */
    TEST_ASSERT_TRUE(arena_set(c, p2) != ARENA_ERROR);
    TEST_ASSERT_FALSE(p2 == c->a_top->ac_data);
    /* Do it once more to verify it doesnt mess up the current chunk and an error is reported */
    TEST_ASSERT_TRUE(arena_set(c, p2) == ARENA_ERROR);

    /*  Now trim to p1, since there's 1 more byte on the arena it should stay stable */
    TEST_ASSERT_TRUE(arena_set(c, p1) != ARENA_ERROR);
    TEST_ASSERT_TRUE(p1 == arena_get(c));
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) == 1);

    /* Test trimming across chunks */
    p0 = arena_push(c, 100);
    TEST_ASSERT_FALSE(p0 == NULL);
    p1 = arena_push(c, ARENA_GROW_SIZE);
    TEST_ASSERT_FALSE(p1 == NULL);
    TEST_ASSERT_TRUE(arena_set(c, p0 + 1) == (ARENA_GROW_SIZE + 99));
    TEST_ASSERT_TRUE(arena_get(c) == (p0 + 1));

    TEST_ASSERT_TRUE(arena_set(c, p0) == 1);
    /* Arena should be empty */
    TEST_ASSERT_TRUE(c->a_top == NULL);
    TEST_ASSERT_TRUE(c->a_pos == 0);

    /* Same as above, but trim to 0 */
    p0 = arena_push(c, 100);
    TEST_ASSERT_FALSE(p0 == NULL);
    p1 = arena_push(c, ARENA_GROW_SIZE);
    TEST_ASSERT_FALSE(p1 == NULL);
    TEST_ASSERT_TRUE(arena_set(c, p0) == (ARENA_GROW_SIZE + 100));
    TEST_ASSERT_TRUE(c->a_top == NULL);
    TEST_ASSERT_TRUE(c->a_pos == 0);

    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}
/*
 * =============================================================================
 * Test arena frame save/restore
 * =============================================================================
 */
void test_arena_frame(void)
{
    arena_frame_t f;
    int *a;

    arena_t *c = arena_new(32);
    TEST_ASSERT_FALSE(c == NULL);
    TEST_ASSERT_FALSE(arena_push(c, 1) == NULL);

    f = arena_save(c);
    void *p1 = arena_push(c, 1);
    TEST_ASSERT_FALSE(p1 == NULL);
    TEST_ASSERT_FALSE(arena_push(c, 10) == NULL);
    arena_restore(f);
    void *p2 = arena_push(c, 1);
    TEST_ASSERT_FALSE(p2 == NULL);
    TEST_ASSERT_TRUE(p1 == p2);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);
    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);

    c = arena_new(0);
    a = arena_malloc(c, 16 * sizeof(int));
    TEST_ASSERT_FALSE(a == NULL);
    for (int ii = 0; ii < 16; ii++)
    {
        a[ii] = ii;
    }

    /*
     * If arena_save()/arena_restore() dont properly cleanup the arena, the
     * `a` buffer may get corrupted. This happened during unit testing when
     * the arena_malloc() wasn't properly keeping account of `arena->a_pos`.
     */
    f = arena_save(c);
    for (int ii = 1; ii < 64; ii++)
    {
        char *p = arena_malloc(c, ii);
        TEST_ASSERT_TRUE(p != NULL);
        memset(p, 0xCC, ii);
    }
    arena_restore(f);
    char *p = arena_malloc(c, 16);
    TEST_ASSERT_TRUE(p != NULL);
    memset(p, 0xCC, 16);
    for (int ii = 0; ii < 16; ii++)
    {
        TEST_ASSERT_TRUE(a[ii] == ii);
    }

    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test arena corner cases around memory alignment
 * =============================================================================
 */
void test_arena_align(void)
{
    void *p;

    arena_t *c = arena_new(ARENA_ALIGN * 4);
    TEST_ASSERT_FALSE(c == NULL);

    p = arena_push(c, ARENA_ALIGN - 1);
    TEST_ASSERT_TRUE(p != NULL);
    p = arena_malloc(c, 0);
    TEST_ASSERT_TRUE((((intptr_t)p) & (ARENA_ALIGN - 1)) == 0);
    TEST_ASSERT_TRUE(arena_pop(c, ARENA_ALIGN - 1) != ARENA_ERROR);
    p = arena_malloc(c, 0);
    TEST_ASSERT_TRUE((((intptr_t)p) & (ARENA_ALIGN - 1)) == 0);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    TEST_ASSERT_FALSE(arena_push(c, ARENA_ALIGN * 3 + 1) == NULL);
    p = arena_malloc(c, ARENA_ALIGN);
    TEST_ASSERT_TRUE(p == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    TEST_ASSERT_FALSE(arena_push(c, ARENA_ALIGN * 3 + 1) == NULL);
    p = arena_malloc(c, 0);
    TEST_ASSERT_FALSE(p == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    TEST_ASSERT_FALSE(arena_push(c, ARENA_ALIGN * 3 + 2) == NULL);
    p = arena_malloc(c, 0);
    TEST_ASSERT_TRUE(p == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test arena corner cases around mresize/mappend
 * =============================================================================
 */
void test_arena_mresize_mappend(void)
{
    void *p = NULL;

    arena_t *c = arena_new(31);
    TEST_ASSERT_FALSE(c == NULL);

    /*
     * mresize() tests
     */
    TEST_ASSERT_TRUE(arena_mresize(c, (void *)NULL, 0, 0) == NULL);
    TEST_ASSERT_TRUE(arena_mresize(c, NULL, 32, 3) == NULL);
    TEST_ASSERT_TRUE(arena_mresize(c, (void *)0xdeadbeef, 0, 3) == NULL);
    TEST_ASSERT_TRUE(arena_mresize(c, (void *)NULL, 0, 3) == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /* Test handling of invalid sizes */
    p = arena_push(c, 10);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 10, 11);
    TEST_ASSERT_FALSE(p == NULL);
    /* Test handling of invalid sizes */
    p = arena_mresize(c, p, 10, 11);
    TEST_ASSERT_TRUE(p == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    p = arena_push(c, 10);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 10, 10);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 10, 20);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 20, 30);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 30, 31);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 31, 32);
    TEST_ASSERT_TRUE(p == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    p = arena_push(c, 0);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 0, 31);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 31, 32); /* Reallocate to out of buond */
    TEST_ASSERT_TRUE(p == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /* Test shrinking */
    p = arena_push(c, 0);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 0, 31);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 31, 15);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_mresize(c, p, 15, 0);
    TEST_ASSERT_FALSE(p == NULL);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /*
     * Test mappend() allocation and OOM handling
     */
    p = arena_push(c, 0);
    TEST_ASSERT_FALSE(p == NULL);
    TEST_ASSERT_TRUE(arena_mappend(c, p, 7, 0, 2));
    TEST_ASSERT_TRUE(arena_mappend(c, p, 7, 2, 2));
    TEST_ASSERT_FALSE(arena_mappend(c, p, 7, 4, 1));
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /*
     * Test mappend end pointer validity
     */
    p = arena_malloc(c, 10);
    TEST_ASSERT_FALSE(p == NULL);
    TEST_ASSERT_FALSE(arena_mappend(c, p, 7, 1, 2));
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /*
     * Test allocation from zero -> OOM
     */
    p = arena_push(c, 0);
    TEST_ASSERT_FALSE(p == NULL);
    TEST_ASSERT_FALSE(arena_mappend(c, p, 7, 0, 5));
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test basic functions with 0 sizes
 * =============================================================================
 */
void test_arena_sz0(void)
{
    void *p1;
    void *p2;
    void *p3;
    void *p4;

    arena_t *c = arena_new(31);
    TEST_ASSERT_FALSE(c == NULL);

    p1 = arena_push(c, 0);
    TEST_ASSERT_FALSE(p1 == NULL);

    p2 = arena_malloc(c, 0);
    TEST_ASSERT_FALSE(p2 == NULL);

    p3 = arena_malloc(c, 0);
    TEST_ASSERT_FALSE(p3 == NULL);

    p4 = arena_mresize(c, p3, 0, 0);
    TEST_ASSERT_FALSE(p3 == NULL);

    TEST_ASSERT_TRUE(p2 == p3);
    TEST_ASSERT_TRUE(p2 == p4);
    TEST_ASSERT_TRUE(p3 == p4);

    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test dynamic arena allocations.
 * =============================================================================
 */
void test_arena_dynamic(void)
{
    char *s1;
    char *s2;
    void *p1;
    void *p2;

    const size_t chunk_boundary = ARENA_GROW_SIZE - sizeof(struct arena_chunk);

    arena_t *c = arena_new(0);
    /* Popping from an empty arena should allocate a new chunk */
    TEST_ASSERT_TRUE(arena_pop(c, 0) != ARENA_ERROR);
    TEST_ASSERT_TRUE(c->a_top == NULL);
    /*
     * Pushing 0 bytes to an arena should create a new chunk and return a valid
     * address so that it can be used with mresize()
     */
    TEST_ASSERT_TRUE(arena_push(c, 0) != NULL);
    TEST_ASSERT_FALSE(c->a_top == NULL);
    /* Popping 0 bytes should free the chunk */
    TEST_ASSERT_TRUE(arena_pop(c, 0) != ARENA_ERROR);
    TEST_ASSERT_TRUE(c->a_top == NULL);

    /* Check if we're able to allocate a full chunk without allocating it */
    p1 = arena_push(c, 0);
    TEST_ASSERT_FALSE(p1 == NULL);
    p2 = arena_push(c, ARENA_GROW_SIZE - sizeof(struct arena_chunk));
    TEST_ASSERT_FALSE(p2 == NULL);
    TEST_ASSERT_TRUE(p1 == p2);

    /* This should not realllocate the buffer to a new chunk */
    p1 = arena_push(c, 0);
    TEST_ASSERT_FALSE(p1 == NULL);
    TEST_ASSERT_TRUE(((uint8_t *)p2 + chunk_boundary) == p1);

    /* Now allocate a new chunk */
    p1 = arena_push(c, 1);
    TEST_ASSERT_FALSE(p1 == NULL);
    TEST_ASSERT_FALSE(((uint8_t *)p2 + chunk_boundary + 1) == p1);

    /* Popping 2 bytes should free the chunk and return us to the previous one */
    TEST_ASSERT_TRUE(arena_pop(c, 2) != ARENA_ERROR);
    p1 = arena_push(c, 0);
    TEST_ASSERT_FALSE(p1 == NULL);
    TEST_ASSERT_TRUE(((uint8_t *)p2 + chunk_boundary - 1) == p1);

    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /*
     * Test resizing beyond/below chunk boundary
     */

    /* Do a small push so the first chunk gets allocated. If the first push is too big,
     * more than ARENA_GROW_SIZE can be allocated */
    p1 = arena_push(c, 0);
    TEST_ASSERT_FALSE(p1 == NULL);
    p1 = arena_push(c, chunk_boundary - 1);
    TEST_ASSERT_FALSE(p1 == NULL);
    p2 = arena_mresize(c, p1, chunk_boundary - 1, chunk_boundary - 1);
    TEST_ASSERT_FALSE(p2 == NULL);
    TEST_ASSERT_TRUE(p1 == p2);

    p2 = arena_mresize(c, p1, chunk_boundary - 1, chunk_boundary);
    TEST_ASSERT_FALSE(p2 == NULL);
    TEST_ASSERT_TRUE(p1 == p2);

    p2 = arena_mresize(c, p1, chunk_boundary, chunk_boundary + 1);
    TEST_ASSERT_FALSE(p2 == NULL);
    TEST_ASSERT_TRUE(p1 != p2);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /*
     * Test string operations
     */
    p1 = arena_push(c, 1);  // Small push first, see above
    TEST_ASSERT_FALSE(p1 == NULL);
    p1 = arena_push(c, chunk_boundary - 2);
    TEST_ASSERT_FALSE(p1 == NULL);
    s1 = arena_push(c, 0);
    TEST_ASSERT_FALSE(s1 == NULL);
    s2 = arena_strdup(c, "");
    TEST_ASSERT_FALSE(s2 == NULL);
    TEST_ASSERT_TRUE(s1 == s2);
    s2 = arena_strcat(c, s1, "Hello new chunk");
    TEST_ASSERT_FALSE(s2 == NULL);
    TEST_ASSERT_FALSE(s1 == s2);
    TEST_ASSERT_TRUE(strcmp(s2, "Hello new chunk") == 0);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /*
     * Test memdup() alignment across chunk boundary
     */
    p1 = arena_push(c, 1);  // Small push first, see above
    TEST_ASSERT_FALSE(p1 == NULL);
    p1 = arena_push(c, chunk_boundary - ARENA_ALIGN - 1);  // Small push first, see above
    TEST_ASSERT_FALSE(p1 == NULL);

    p1 = arena_push(c, 0);
    TEST_ASSERT_FALSE(p1 == NULL);
    p2 = arena_memdup(c, &(uint8_t[]){0xCC}, 1);
    TEST_ASSERT_FALSE(p2 == NULL);
    TEST_ASSERT_TRUE(p1 == p2);

    p1 = arena_push(c, 0);
    TEST_ASSERT_FALSE(p1 == NULL);
    p2 = arena_memdup(c, &(uint8_t[]){0xCC}, 1);
    TEST_ASSERT_FALSE(p2 == NULL);
    TEST_ASSERT_FALSE(p1 == p2);
    TEST_ASSERT_TRUE(*(uint8_t *)p2 == 0xCC);

    p1 = arena_push(c, 0);
    TEST_ASSERT_FALSE(p1 == NULL);
    p2 = arena_memdup(c, &(uint8_t[]){0xCC}, 1);
    TEST_ASSERT_FALSE(p2 == NULL);
    TEST_ASSERT_FALSE(p1 == p2);
    TEST_ASSERT_TRUE(*(uint8_t *)p2 == 0xCC);

    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /*
     * Test memcat() across chunk boundary
     */
    char buf[ARENA_ALIGN];
    memset(buf, 0xCC, sizeof(buf));

    p1 = arena_push(c, 1);  // Small push first, see above
    TEST_ASSERT_FALSE(p1 == NULL);
    p1 = arena_push(c, chunk_boundary - ARENA_ALIGN - 1);  // Small push first, see above
    TEST_ASSERT_FALSE(p1 == NULL);

    p1 = arena_push(c, 0);
    TEST_ASSERT_FALSE(p1 == NULL);
    p2 = arena_memdup(c, &(uint8_t[]){0xCC}, 1);
    TEST_ASSERT_FALSE(p2 == NULL);
    TEST_ASSERT_TRUE(p1 == p2);

    p2 = arena_memcat(c, p2, 1, buf, sizeof(buf));
    TEST_ASSERT_FALSE(p2 == NULL);
    TEST_ASSERT_FALSE(p1 == p2);
    TEST_ASSERT_TRUE(memcmp(p2, buf, sizeof(buf)) == 0);

    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /*
     * Test memappend
     */
    int64_t *px = NULL;
    for (size_t x = 0; x < ARENA_GROW_SIZE; x++)
    {
        px = arena_mappend(c, px, sizeof(int64_t), x, 1);
        TEST_ASSERT_FALSE(px == NULL);
        px[x] = ((ARENA_MAGIC ^ x) >> (x % 8)) | ((ARENA_MAGIC ^ x) << (7 - (x % 8)));
    }

    for (size_t x = 0; x < ARENA_GROW_SIZE; x++)
    {
        int64_t t = ((ARENA_MAGIC ^ x) >> (x % 8)) | ((ARENA_MAGIC ^ x) << (7 - (x % 8)));
        TEST_ASSERT_TRUE(px[x] == t);
    }

    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test arena defer callbacks and defer block corruption cases
 * =============================================================================
 */
void test_arena_defer_fn(void *p)
{
    (*((int *)p))++;
}

void test_arena_defer(void)
{
    arena_t *c;
    int a;

    c = arena_new(64);
    TEST_ASSERT_FALSE(c == NULL);

    a = 0;
    TEST_ASSERT_TRUE(arena_defer(c, test_arena_defer_fn, &a));
    TEST_ASSERT_TRUE(arena_pop(c, 1) != ARENA_ERROR);
    TEST_ASSERT_TRUE(a == 1);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    a = 0;
    TEST_ASSERT_TRUE(arena_defer(c, test_arena_defer_fn, &a));
    TEST_ASSERT_TRUE(arena_pop(c, 0) != ARENA_ERROR);
    /* arena_pop(c, 0) should not call the defer */
    TEST_ASSERT_TRUE(a == 0);
    TEST_ASSERT_TRUE(arena_push(c, 1) != NULL);
    TEST_ASSERT_TRUE(arena_pop(c, 1) == 1);
    TEST_ASSERT_TRUE(a == 0);
    TEST_ASSERT_TRUE(arena_pop(c, 1) == 1);
    TEST_ASSERT_TRUE(a == 1);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    a = 0;
    arena_frame_t t = arena_save(c);
    TEST_ASSERT_TRUE(arena_defer(c, test_arena_defer_fn, &a));
    TEST_ASSERT_FALSE(arena_malloc(c, 5) == NULL);
    arena_restore(t);
    TEST_ASSERT_TRUE(a == 1);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /*
     * Test OOM condition while try do a defer; the callback should still be
     * called, but the function arena_defer() function should return false
     */
    a = 0;
    arena_pop(c, SIZE_MAX);
    TEST_ASSERT_FALSE(arena_push(c, 63) == NULL);
    TEST_ASSERT_FALSE(arena_defer(c, test_arena_defer_fn, &a));
    TEST_ASSERT_TRUE(a == 1);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /* Test corrupt deffer - the defer callback should not be called */
    a = 0;
    TEST_ASSERT_TRUE(arena_defer(c, test_arena_defer_fn, &a));
    uint8_t *p = arena_push(c, 4);
    TEST_ASSERT_FALSE(p == NULL);
    p[-1] ^= 255; /* Intentionally corrupt the defer block */
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) == ARENA_ERROR);
    TEST_ASSERT_TRUE(a == 0);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    /* Test deferral on arena delete */
    a = 0;
    TEST_ASSERT_TRUE(arena_defer(c, test_arena_defer_fn, &a));
    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
    TEST_ASSERT_TRUE(a == 1);
}

/*
 * =============================================================================
 * Test arena_frame_auto_t with arena frames
 * =============================================================================
 */
void test_arena_cauto(void)
{
    int a = 0;

    {
        arena_auto_t *c = arena_new(64);
        TEST_ASSERT_TRUE(arena_defer(c, test_arena_defer_fn, &a));
    }

    TEST_ASSERT_TRUE(a == 1);
}

/*
 * =============================================================================
 * Test arena_frame_auto_t with arena frames
 * =============================================================================
 */
void test_arena_cauto_frame(void)
{
    arena_t *c;
    int a;

    c = arena_new(64);
    TEST_ASSERT_FALSE(c == NULL);

    a = 0;
    {
        arena_frame_auto_t f = arena_save(c);
        TEST_ASSERT_TRUE(arena_defer(c, test_arena_defer_fn, &a));
    }
    TEST_ASSERT_TRUE(a == 1);

    a = 0;
    for (int ii = 0; ii < 5; ii++)
    {
        arena_frame_auto_t f = arena_save(c);
        TEST_ASSERT_TRUE(arena_defer(c, test_arena_defer_fn, &a));
    }
    TEST_ASSERT_TRUE(a == 5);

    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test scratch arena allocation
 * =============================================================================
 */
void test_arena_scratch(void)
{
    arena_t *s1, *s2, *s3;
    TEST_ASSERT_TRUE(ARENA_SCRATCH_NUM >= 3);

    s1 = arena_scratch();
    TEST_ASSERT_FALSE(s1 == NULL);
    s2 = arena_scratch(s1);
    TEST_ASSERT_FALSE(s2 == NULL);
    s3 = arena_scratch(s1, s2);
    TEST_ASSERT_FALSE(s3 == NULL);

    TEST_ASSERT_FALSE(s1 == s2);
    TEST_ASSERT_FALSE(s1 == s3);
    TEST_ASSERT_FALSE(s2 == s3);

    TEST_ASSERT_TRUE(s1 == arena_scratch());
    TEST_ASSERT_TRUE(s2 == arena_scratch(s1));
    TEST_ASSERT_TRUE(s1 == arena_scratch(s2));
    TEST_ASSERT_TRUE(s3 == arena_scratch(s1, s2));
    TEST_ASSERT_TRUE(s3 == arena_scratch(s2, s1));

    arena_t *s[ARENA_SCRATCH_NUM + 1];
    memset(s, 0, sizeof(s));

    for (int ii = 0; ii < ARENA_SCRATCH_NUM; ii++)
    {
        s[ii] = __arena_scratch(s);

        for (int ij = 0; ij < ii - 1; ij++)
        {
            TEST_ASSERT_FALSE(s[ii] == s[ij]);
        }
    }

    TEST_ASSERT_TRUE(__arena_scratch(s) == NULL);
}

void test_arena_scratch_scope(void)
{
    int a = 0;
    {
        TEST_ASSERT_TRUE(ARENA_SCRATCH_NUM >= 3);

        ARENA_SCRATCH(s1);
        ARENA_SCRATCH(s2, s1);
        ARENA_SCRATCH(s3, s1, s2);

        TEST_ASSERT_TRUE(arena_defer(s1, test_arena_defer_fn, &a));
        TEST_ASSERT_TRUE(arena_defer(s2, test_arena_defer_fn, &a));
        TEST_ASSERT_TRUE(arena_defer(s3, test_arena_defer_fn, &a));
    }

    TEST_ASSERT_TRUE(a == 3);
}

void test_print_dot(void *data)
{
    (void)data;

    write(1, ".", 1);
}

void arena_scratch_atexit_fork(void)
{
    arena_t *s1 = arena_scratch();
    arena_t *s2 = arena_scratch(s1);
    arena_t *s3 = arena_scratch(s2);

    arena_defer(s1, test_print_dot, NULL);
    arena_defer(s2, test_print_dot, NULL);
    arena_defer(s3, test_print_dot, NULL);

    exit(0);
}

char atexit_cmd[512];
void test_arena_scratch_atexit_init(const char *cmd)
{
    snprintf(atexit_cmd, sizeof(atexit_cmd), "%s fork", cmd);
}

void test_arena_scratch_atexit(void)
{
    ARENA_SCRATCH(s1);

    FILE *f;
    char buf[1024];

    f = popen(atexit_cmd, "r");
    TEST_ASSERT_FALSE(f == NULL);
    arena_defer_fclose(s1, f);

    TEST_ASSERT_FALSE(fgets(buf, sizeof(buf), f) == NULL);
    TEST_ASSERT_TRUE((buf == NULL) ? false : (strcmp(buf, "...") == 0));
}

/*
 * =============================================================================
 * Test stack-based arenas
 * =============================================================================
 */
void test_arena_stack(void)
{
    {
        ARENA_STACK(c, 1024);
        TEST_ASSERT_FALSE(c == NULL);
        TEST_ASSERT_FALSE(arena_push(c, 512) == NULL);
        TEST_ASSERT_FALSE(arena_push(c, 512) == NULL);
        TEST_ASSERT_TRUE(arena_push(c, 1) == NULL);
        TEST_ASSERT_TRUE(arena_pop(c, 0) == 0);
        TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) == 1024);
        TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) == 0);
        TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
    }

    int a = 0;
    {
        ARENA_STACK(c, 1024);
        TEST_ASSERT_FALSE(c == NULL);
        TEST_ASSERT_FALSE(arena_push(c, 16) == NULL);
        TEST_ASSERT_TRUE(arena_defer(c, test_arena_defer_fn, &a));
    }

    TEST_ASSERT_TRUE(a != 0);
}

/*
 * =============================================================================
 * Test arena string/memory support functions
 * =============================================================================
 */
void test_arena_memdup_memcat(void)
{
    arena_t *c;
    uint8_t *p;

    const uint8_t l[4] = {0xCC, 0xCC, 0xCC, 0xCC};
    const uint8_t u[4] = {0xAA, 0xAA, 0xAA, 0xAA};
    const uint8_t lu[8] = {0xCC, 0xCC, 0xCC, 0xCC, 0xAA, 0xAA, 0xAA, 0xAA};

    c = arena_new(ARENA_ALIGN * 4);
    TEST_ASSERT_FALSE(c == NULL);

    p = arena_memdup(c, l, sizeof(l));
    TEST_ASSERT_FALSE(p == NULL);
    TEST_ASSERT_TRUE(memcmp(p, l, sizeof(l)) == 0);

    p = arena_memdup(c, lu, sizeof(u));
    TEST_ASSERT_FALSE(p == NULL);
    TEST_ASSERT_TRUE(memcmp(p, lu, sizeof(u)) == 0);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    TEST_ASSERT_TRUE(arena_memcat(c, NULL, 0, NULL, 0) == NULL);
    TEST_ASSERT_TRUE(arena_memcat(c, NULL, 1, (void *)0xdeadbeef, 5) == NULL);

    p = arena_memdup(c, l, sizeof(l));
    TEST_ASSERT_FALSE(p == NULL);
    TEST_ASSERT_TRUE(memcmp(p, l, sizeof(l)) == 0);
    TEST_ASSERT_TRUE(arena_memcat(c, p, sizeof(l), u, sizeof(u)));
    TEST_ASSERT_TRUE(memcmp(p, lu, sizeof(lu)) == 0);
    TEST_ASSERT_TRUE(arena_pop(c, SIZE_MAX) != ARENA_ERROR);

    p = arena_push(c, 0);
    TEST_ASSERT_FALSE(p == NULL);
    p = arena_memcat(c, p, 0, lu, sizeof(lu));
    TEST_ASSERT_FALSE(p == NULL);
    TEST_ASSERT_TRUE(memcmp(p, lu, sizeof(lu)) == 0);

    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test arena string functions
 * =============================================================================
 */
#define STRL8 "01234567"
#define STRU8 "89ABCDEF"
#define STR16 STRL8 STRU8

void test_arena_strdup_strcat(void)
{
    char *s;

    arena_t *c = arena_new(sizeof(STR16));
    TEST_ASSERT_FALSE(c == NULL);
    arena_frame_t f = arena_save(c);

    /* Test arena bounds in combination with strdup */
    TEST_ASSERT_FALSE(arena_strdup(c, STR16) == NULL);
    TEST_ASSERT_TRUE(arena_strdup(c, "") == NULL);
    arena_restore(f);

    TEST_ASSERT_TRUE(arena_strdup(c, "X" STR16) == NULL);
    s = arena_strdup(c, STRL8);
    TEST_ASSERT_FALSE(s == NULL);
    TEST_ASSERT_FALSE(arena_strcat(c, s, STR16));
    arena_restore(f);

    /* Test arena bounds in combination with strcar */
    s = arena_strdup(c, STR16);
    TEST_ASSERT_FALSE(s == NULL);
    TEST_ASSERT_TRUE(arena_strcat(c, s, ""));
    TEST_ASSERT_FALSE(arena_strcat(c, s, "X"));
    arena_restore(f);

    /* Test that concatenation actually works */
    s = arena_strdup(c, STRL8);
    TEST_ASSERT_FALSE(s == NULL);
    TEST_ASSERT_TRUE(arena_strcat(c, s, STRU8));
    TEST_ASSERT_TRUE(strcmp(s, STR16) == 0);
    arena_restore(f);

    /* Test appends on strings that are not at the end of the arena (should produce an error) */
    s = arena_strdup(c, STRL8);
    TEST_ASSERT_FALSE(s == NULL);
    TEST_ASSERT_FALSE(arena_push(c, 1) == NULL);
    TEST_ASSERT_FALSE(arena_strcat(c, s, "X"));
    arena_restore(f);

    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

/*
 * =============================================================================
 * Test arena printf functions
 * =============================================================================
 */
void test_arena_printf(void)
{
    arena_frame_t f;
    char *s;

    arena_t *c = arena_new(65);
    TEST_ASSERT_FALSE(c == NULL);
    f = arena_save(c);

    s = arena_sprintf(c, "Hello %d", 5);
    TEST_ASSERT_FALSE(s == NULL);
    s = arena_scprintf(c, s, " world %d", 6);
    TEST_ASSERT_FALSE(s == NULL);
    TEST_ASSERT_TRUE(strcmp(s, "Hello 5 world 6") == 0);
    arena_restore(f);

    s = arena_sprintf(c, "sprintf %d", 1);
    TEST_ASSERT_FALSE(s == NULL);
    TEST_ASSERT_TRUE(arena_strcat(c, s, " strcat 2"));
    TEST_ASSERT_TRUE(arena_scprintf(c, s, " scprintf %d", 3));
    TEST_ASSERT_TRUE(strcmp(s, "sprintf 1 strcat 2 scprintf 3") == 0);
    arena_restore(f);

    TEST_ASSERT_TRUE(arena_push(c, 60) != NULL);
    s = arena_sprintf(c, "123%c", '4');
    TEST_ASSERT_FALSE(s == NULL);
    TEST_ASSERT_TRUE(strcmp(s, "1234") == 0);
    s = arena_sprintf(c, "%s", "");
    TEST_ASSERT_TRUE(s == NULL);
    arena_restore(f);

    TEST_ASSERT_TRUE(arena_push(c, 60) != NULL);
    s = arena_sprintf(c, "12");
    TEST_ASSERT_FALSE(s == NULL);
    TEST_ASSERT_TRUE(arena_scprintf(c, s, "3%c", '4'));
    TEST_ASSERT_TRUE(strcmp(s, "1234") == 0);
    TEST_ASSERT_TRUE(arena_scprintf(c, s, "%s", ""));
    TEST_ASSERT_FALSE(arena_scprintf(c, s, "%s", "1"));
    arena_restore(f);

    TEST_ASSERT_FALSE(arena_push(c, 64) == NULL);
    s = arena_sprintf(c, "%s", "");
    TEST_ASSERT_FALSE(s == NULL);
    TEST_ASSERT_TRUE(arena_scprintf(c, s, "%s", ""));
    arena_restore(f);

    TEST_ASSERT_FALSE(arena_push(c, 60) == NULL);
    s = arena_sprintf(c, "%s", "Hello world");
    TEST_ASSERT_TRUE(s == NULL);
    arena_restore(f);

    TEST_ASSERT_FALSE(arena_push(c, 60) == NULL);
    s = arena_sprintf(c, "%s", "He");
    TEST_ASSERT_FALSE(s == NULL);
    TEST_ASSERT_FALSE(arena_scprintf(c, s, "%s", "llloooo"));
    arena_restore(f);

    s = arena_scprintf(c, NULL, "%s", "Hello");
    TEST_ASSERT_TRUE(s == NULL);

    TEST_ASSERT_TRUE(arena_push(c, 59) != NULL);
    s = arena_strdup(c, "");
    TEST_ASSERT_FALSE(s == NULL);
    s = arena_scprintf(c, s, "%s", "Hello");
    TEST_ASSERT_FALSE(s == NULL);
    arena_restore(f);

    TEST_ASSERT_TRUE(arena_push(c, 60) != NULL);
    s = arena_strdup(c, "");
    TEST_ASSERT_FALSE(s == NULL);
    s = arena_scprintf(c, s, "%s", "Hello");
    TEST_ASSERT_TRUE(s == NULL);
    arena_restore(f);

    TEST_ASSERT_TRUE(arena_del(c) != ARENA_ERROR);
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "fork") == 0)
    {
        arena_scratch_atexit_fork();
    }

    log_open("test_arena", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_DEBUG);

    ut_init("test_arena", NULL, NULL);

    RUN_TEST(test_arena_basic);
    RUN_TEST(test_arena_oom_malloc);
    RUN_TEST(test_arena_push_pop);
    RUN_TEST(test_arena_set_get);
    RUN_TEST(test_arena_frame);
    RUN_TEST(test_arena_align);
    RUN_TEST(test_arena_mresize_mappend);
    RUN_TEST(test_arena_sz0);
    RUN_TEST(test_arena_dynamic);
    RUN_TEST(test_arena_defer);
    RUN_TEST(test_arena_cauto);
    RUN_TEST(test_arena_cauto_frame);
    RUN_TEST(test_arena_scratch);
    RUN_TEST(test_arena_scratch_scope);
    test_arena_scratch_atexit_init(argv[0]);
    RUN_TEST(test_arena_scratch_atexit);
    RUN_TEST(test_arena_stack);
    RUN_TEST(test_arena_memdup_memcat);
    RUN_TEST(test_arena_strdup_strcat);
    RUN_TEST(test_arena_printf);

    ut_fini();
}
