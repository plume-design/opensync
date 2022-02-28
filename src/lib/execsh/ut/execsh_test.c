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
#include <getopt.h>

#include "unity.h"
#include "log.h"
#include "execsh.h"
#include "unit_test_utils.h"

#define PR(...) do { if (opt_verbose) LOG(INFO, __VA_ARGS__); } while (0)

int opt_verbose = 0;
char *test_name = "EXECSH_TEST";

bool parse_opts(int argc, char *argv[])
{
    int o;

    static struct option opts[] =
    {
        {   "verbose",      no_argument,    NULL,   'v' },
        {   NULL,           0,              NULL,   0   }
    };

    while ((o = getopt_long(argc, argv, "v", opts, NULL)) != -1)
    {
        switch (o)
        {
            case 'v':
                opt_verbose = 1;
                break;

            default:
                //fprintf(stderr, "Invalid option: %s\n", argv[optind - 1]);
                return false;
        }
    }

    return true;
}

bool pr_fn(void *ctx, enum execsh_io type, const char *buf)
{
    PR("%c %s", type == EXECSH_IO_STDOUT ? '>' : '|', buf);

    return true;
}

void test_execsh_fn_true(void)
{
    int status;

    status = execsh_fn(pr_fn, NULL, "true");
    TEST_ASSERT_TRUE_MESSAGE(
            status == 0,
            "Process exit status is not 0.");
}

void test_execsh_fn_false(void)
{
    int status;

    status = execsh_fn(pr_fn, NULL, "false");
    TEST_ASSERT_TRUE_MESSAGE(
            status != 0,
            "Process exit status is 0.");
}

void test_execsh_fn_long_output(void)
{
    int status;

    status = execsh_fn(pr_fn, NULL, "find /usr");
    TEST_ASSERT_TRUE_MESSAGE(
            status == 0,
            "Process exit status is not 0.");
}

bool count_fn(void *ctx, enum execsh_io io_type, const char *buf)
{
    int ii;
    int *cnt = ctx;

    if (io_type != EXECSH_IO_STDOUT)
    {
        PR("%s\n", buf);
        return true;
    }

    ii = atoi(buf);

    if (*cnt != ii)
    {
        PR("Error: Counting lines %d != %d, error line: %s", *cnt, ii, buf);
        return false;
    }

    (*cnt)++;

    return true;
}

void test_execsh_fn_long_output2(void)
{
    int status;

    int cnt = 0;

    status = execsh_fn(count_fn, &cnt, "seq 0 9999");
    TEST_ASSERT_TRUE_MESSAGE(
            status == 0,
            "Process exit status is not 0.");

    TEST_ASSERT_TRUE_MESSAGE(
            cnt == 10000,
            "Count is not 10000.");
}

bool null_fn(void *ctx, enum execsh_io type, const char *buf)
{
    (void)ctx;
    (void)type;
    (void)buf;

    return true;
}

void test_execsh_fn_long_output3(void)
{
    int status;

    /* This returns garbage, but make sure we dont crash */
    /* Make sure the execution always succeeds */
    status = execsh_fn(null_fn, NULL, "find /usr/bin -type f -exec cat {} \\;");
    TEST_ASSERT_TRUE_MESSAGE(
            status == 0,
            "Process exit status is not 0.");
}

void test_execsh_fn_long_input(void)
{
    int status;
    int ii;

    ssize_t bufsz = strlen("echo 10000\n") * 10000;
    ssize_t nc;

    char *buf = malloc(bufsz);
    TEST_ASSERT_NOT_NULL(buf);

    /* Generate a long input */
    nc = 0;
    for (ii = 0; ii < 10000; ii++)
    {
        nc += snprintf(buf + nc, bufsz - nc, "echo %d\n", ii);
    }

    int cnt = 0;
    /* This returns garbage, but make sure we dont crash */
    status = execsh_fn(count_fn, &cnt, buf);
    TEST_ASSERT_TRUE_MESSAGE(
            status == 0,
            "Process exit status is not 0.");

    TEST_ASSERT_TRUE_MESSAGE(
            cnt == 10000,
            "Count is not 10000.");

    /* Cleanup */
    free(buf);
}

void test_execsh_fn_args(void)
{
    int status;

    int cnt = 0;
    /* This returns garbage, but make sure we dont crash */
    status = execsh_fn(count_fn, &cnt, "for x in \"$@\"; do echo $x; done",
            "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
            "10", "11", "12", "13", "14", "15", "16", "17", "18", "19",
            "20", "21", "22", "23", "24", "25", "26", "27", "28", "29",
            "30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
            "40", "41", "42", "43", "44", "45", "46", "47", "48", "49");

    TEST_ASSERT_TRUE_MESSAGE(
            status == 0,
            "Process exit status is not 0.");

    PR("Count is %d\n", cnt);
    TEST_ASSERT_TRUE_MESSAGE(
            cnt == 50,
            "Count is not 50.");
}

void run_test_execsh(void)
{
    RUN_TEST(test_execsh_fn_true);
    RUN_TEST(test_execsh_fn_false);
    RUN_TEST(test_execsh_fn_long_output);
    RUN_TEST(test_execsh_fn_long_output2);
    RUN_TEST(test_execsh_fn_long_output3);
    RUN_TEST(test_execsh_fn_long_input);
    RUN_TEST(test_execsh_fn_args);

    fflush(stderr);
    fflush(stdout);
}

/*
 * ===========================================================================
 *  MAIN
 * ===========================================================================
 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, NULL, NULL);

    if (!parse_opts(argc, argv))
    {
        return false;
    }

    if (opt_verbose)
        log_open(test_name, LOG_OPEN_STDOUT);

    run_test_execsh();

    return ut_fini();
}
