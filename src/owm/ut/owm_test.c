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

#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <unity.h>

#ifdef _GNU_SOURCE
#undef _GNU_SOURCE
#endif
#include <unit_test_utils.h>

static char *test_name = "owm_test";
static char owm_cmd[512];

static void
test_wrap(void)
{
    TEST_ASSERT_TRUE(system(owm_cmd) == 0);
}

static void
prep(const char *argv0)
{
    char *copy = strdup(argv0);
    char *test_dir = dirname(copy);
    char *utest_dir = dirname(test_dir);
    char *bin_dir = dirname(utest_dir);
    snprintf(owm_cmd, sizeof(owm_cmd), "%s/owm -t", bin_dir);
    free(copy);
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, NULL, NULL);
    ut_setUp_tearDown(test_name, NULL, NULL);
    prep(argv[0]);
    RUN_TEST(test_wrap);

    return ut_fini();
}
