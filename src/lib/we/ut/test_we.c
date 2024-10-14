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

#include <errno.h>
#include <libgen.h>

#include "log.h"
#include "target.h"
#include "unity.h"

#include "vm.h"
#include "we.h"

void (*g_setUp)(void) = NULL;
void (*g_tearDown)(void) = NULL;

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_create_and_destroy()
{
    we_state_t state;
    TEST_ASSERT_TRUE(we_create(&state, 32) == 0);
    TEST_ASSERT_TRUE(we_destroy(state) == 0);
}

static void test_add()
{
    int64_t result;
    const uint8_t insn[] = {WE_OP_ADD, WE_OP_HLT};
    we_state_t state;
    TEST_ASSERT_TRUE(we_create(&state, 32) == 0);
    TEST_ASSERT_TRUE(we_pushbuf(state, 2, (void *)insn) == 0);
    TEST_ASSERT_TRUE(we_pushnum(state, 11) == 1);
    TEST_ASSERT_TRUE(we_pushnum(state, 39) == 2);
    TEST_ASSERT_TRUE(we_call(&state, NULL) == 0);
    TEST_ASSERT_TRUE(we_top(state) == 1);
    TEST_ASSERT_TRUE(we_read(state, we_top(state), WE_NUM, &result) == 8);
    TEST_ASSERT_TRUE(result == 50);
    TEST_ASSERT_TRUE(we_destroy(state) == 0);
}

static void test_sub()
{
    int64_t result;
    const uint8_t insn[] = {WE_OP_SUB, WE_OP_HLT};
    we_state_t state;
    TEST_ASSERT_TRUE(we_create(&state, 32) == 0);
    TEST_ASSERT_TRUE(we_pushbuf(state, 2, (void *)insn) == 0);
    TEST_ASSERT_TRUE(we_pushnum(state, 11) == 1);
    TEST_ASSERT_TRUE(we_pushnum(state, 39) == 2);
    TEST_ASSERT_TRUE(we_call(&state, NULL) == 0);
    TEST_ASSERT_TRUE(we_top(state) == 1);
    TEST_ASSERT_TRUE(we_read(state, we_top(state), WE_NUM, &result) == 8);
    TEST_ASSERT_TRUE(result == -28);
    TEST_ASSERT_TRUE(we_destroy(state) == 0);
}

static void test_mul()
{
    int64_t result;
    const uint8_t insn[] = {WE_OP_MUL, WE_OP_HLT};
    we_state_t state;
    TEST_ASSERT_TRUE(we_create(&state, 32) == 0);
    TEST_ASSERT_TRUE(we_pushbuf(state, 2, (void *)insn) == 0);
    TEST_ASSERT_TRUE(we_pushnum(state, 11) == 1);
    TEST_ASSERT_TRUE(we_pushnum(state, 39) == 2);
    TEST_ASSERT_TRUE(we_call(&state, NULL) == 0);
    TEST_ASSERT_TRUE(we_top(state) == 1);
    TEST_ASSERT_TRUE(we_read(state, we_top(state), WE_NUM, &result) == 8);
    TEST_ASSERT_TRUE(result == 429);
    TEST_ASSERT_TRUE(we_destroy(state) == 0);
}

static void test_cmp()
{
    int64_t result;
    const uint8_t insn[] = {WE_OP_CMP, WE_OP_HLT};
    we_state_t state;
    TEST_ASSERT_TRUE(we_create(&state, 32) == 0);
    TEST_ASSERT_TRUE(we_pushbuf(state, 2, (void *)insn) == 0);
    TEST_ASSERT_TRUE(we_pushnum(state, 11) == 1);
    TEST_ASSERT_TRUE(we_pushnum(state, 39) == 2);
    TEST_ASSERT_TRUE(we_call(&state, NULL) == 0);
    TEST_ASSERT_TRUE(we_top(state) == 1);
    TEST_ASSERT_TRUE(we_read(state, we_top(state), WE_NUM, &result) == 8);
    TEST_ASSERT_TRUE(result == 1);
    TEST_ASSERT_TRUE(we_destroy(state) == 0);
}

static int test_ext_cb(we_state_t state, void *user)
{
    (void)state;
    int64_t *p = (int64_t *)user;
    *p = 9;
    return 0;
}

static void test_ext()
{
    const uint8_t insn[] = {WE_OP_EXT, 42, WE_OP_HLT};
    we_state_t state;
    int64_t userdata = 8;
    we_setup(42, test_ext_cb);
    TEST_ASSERT_TRUE(we_create(&state, 32) == 0);
    TEST_ASSERT_TRUE(we_pushbuf(state, 2, (void *)insn) == 0);
    TEST_ASSERT_TRUE(we_call(&state, &userdata) == 0);
    TEST_ASSERT_TRUE(we_destroy(state) == 0);
    TEST_ASSERT_TRUE(userdata == 9);
}

static void test_einval()
{
    we_state_t state;
    TEST_ASSERT_TRUE(we_create(&state, 32) == 0);
    TEST_ASSERT_TRUE(we_pushnum(state, 1) == 0);
    TEST_ASSERT_TRUE(we_move(state, -100) == -EINVAL);
    TEST_ASSERT_TRUE(we_top(state) == 0);
    TEST_ASSERT_TRUE(we_destroy(state) == 0);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("libwe", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_INFO);

    UnityBegin(basename(__FILE__));

    RUN_TEST(test_create_and_destroy);
    RUN_TEST(test_add);
    RUN_TEST(test_sub);
    RUN_TEST(test_cmp);
    RUN_TEST(test_mul);
    RUN_TEST(test_ext);
    RUN_TEST(test_einval);

    return UNITY_END();
}
