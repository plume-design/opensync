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
#include <stdbool.h>
#include <string.h>

#include "target.h"
#include "const.h"
#include "log.h"
#include "unity.h"
#include "schema.h"
#include "unit_test_utils.h"

static const char *test_name = "schema_subset_tests";

#define SIZE 32
#define LIST(_var, ...) char _var[][SIZE] = { __VA_ARGS__ }
#define SET(...) LIST(set, ##__VA_ARGS__ )
#define SUBSET(...) LIST(subset, ##__VA_ARGS__)
#define DEF_TEST(_name, _result, _set, _subset) \
    void _name(void) { \
        SET _set; \
        SUBSET _subset; \
        TEST_ASSERT_TRUE( \
            schema_changed_subset( \
                set, subset, \
                ARRAY_SIZE(set), ARRAY_SIZE(subset), \
                SIZE, \
                strncmp) == _result); \
    }

DEF_TEST(test_1, false, ("a", "b"), ("a"))
DEF_TEST(test_2, false, ("a", "b"), ("b"))
DEF_TEST(test_3, true,  ("a", "b"), ())
DEF_TEST(test_4, true,  ("a", "b"), ("a", "c"))
DEF_TEST(test_5, true,  ("a", "b"), ("c"))
DEF_TEST(test_6, false, (), ())
DEF_TEST(test_7, true,  (), ("a"))

int
main(int argc, char *argv[])
{
    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, NULL, NULL);

    RUN_TEST(test_1);
    RUN_TEST(test_2);
    RUN_TEST(test_3);
    RUN_TEST(test_4);
    RUN_TEST(test_5);
    RUN_TEST(test_6);
    RUN_TEST(test_7);

    return ut_fini();
}
