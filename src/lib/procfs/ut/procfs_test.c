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

#include <sys/wait.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include "log.h"
#include "unity.h"

#include "procfs.h"

#define PR(...) do { if (opt_verbose) LOG(INFO, __VA_ARGS__); } while (0)

int opt_verbose = 0;

void setUp() {}
void tearDown() {}

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


/*
 * ===========================================================================
 *  Procfs testing
 * ===========================================================================
 */
void test_procfs_list(void)
{
    procfs_t pf;
    procfs_entry_t *pe;

    TEST_ASSERT_TRUE_MESSAGE(procfs_open(&pf), "procfs_open() failed");
    while ((pe = procfs_read(&pf)) != NULL)
    {
        TEST_ASSERT_TRUE_MESSAGE(pe->pe_cmdline != NULL, "process entry without cmdline");
        PR("Pid:%jd Process:%s State:%s PPid:%jd\n", (intmax_t)pe->pe_pid, pe->pe_name, pe->pe_state, (intmax_t)pe->pe_ppid);
    }

    TEST_ASSERT_TRUE_MESSAGE(procfs_close(&pf), "procfs_close() failed");
}

/* GCC is complaining about clobbered variables after the fork() or
 * long_jump() inserted by TEST_PROTECT().
 */
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wclobbered"
#endif

void test_procfs_getpid(void)
{
    procfs_entry_t *pe = NULL;
    pid_t child = -1;

    if (TEST_PROTECT())
    {
        child = fork();
        if (child == 0)
        {
            /* Xargs will literally accept any kind of argument and block while waiting for stdin ... perfect for our case */
            LOG(INFO, "fork OK");
            execlp("xargs", "xargs", "1", "getpid_test", "2", "3", NULL);
            PR("Error %s!", strerror(errno));
            exit(1);
        }

        usleep(100000);

        int status;
        TEST_ASSERT_TRUE_MESSAGE(waitpid(child, &status, WNOHANG) != -1, "waitpid() failed");
        TEST_ASSERT_FALSE_MESSAGE(WIFEXITED(child), "Child exited unexpectedly.");

        pe = procfs_entry_getpid(child);
        TEST_ASSERT_TRUE_MESSAGE(pe != NULL, "procfs_entry_getpid() failed");
        TEST_ASSERT_TRUE(strcmp(pe->pe_name, "xargs") == 0);

        TEST_ASSERT_TRUE_MESSAGE(pe->pe_cmdline != NULL, "Process contains no arguments");

        char **parg = pe->pe_cmdline;
        while (pe->pe_cmdline != NULL && *parg != NULL)
        {
            PR("+ %s\n", *parg);
            parg++;
        }

        PR("NARGS = %zd\n", parg - pe->pe_cmdline);
        /* We should have 6 arguments */
        TEST_ASSERT_TRUE_MESSAGE((parg - pe->pe_cmdline) == 5, "Invalid number of arguments");

        TEST_ASSERT_TRUE_MESSAGE(strcmp("xargs", pe->pe_cmdline[0]) == 0, "ARG0 should be xargs");
        TEST_ASSERT_TRUE_MESSAGE(strcmp("1", pe->pe_cmdline[1]) == 0, "ARG1 should be 1");
        TEST_ASSERT_TRUE_MESSAGE(strcmp("getpid_test", pe->pe_cmdline[2]) == 0, "ARG2 should be getpid_test");
        TEST_ASSERT_TRUE_MESSAGE(strcmp("2", pe->pe_cmdline[3]) == 0, "ARG3 should be 2");
        TEST_ASSERT_TRUE_MESSAGE(strcmp("3", pe->pe_cmdline[4]) == 0, "ARG4 should be 3");

        /* xargs is waiting on input, so it should be in the sleeping state */
        TEST_ASSERT_TRUE_MESSAGE(pe->pe_state[0] == 'S', "process not in sleeping state");

        procfs_entry_del(pe);

        kill(child, SIGKILL);
        usleep(100000);

        /* Kill the child, check if it is a zombie now */
        pe = procfs_entry_getpid(child);
        PR("Process state after kill = %s\n", pe->pe_state);

        TEST_ASSERT_TRUE_MESSAGE(pe->pe_state[0] == 'Z', "process not zombie");
    }

    if (pe != NULL) procfs_entry_del(pe);
    if (child != -1)
    {
        int status;
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
}

void test_procfs_getpid_neg(void)
{
    int status;

    procfs_entry_t *pe = NULL;
    pid_t child = -1;

    if (TEST_PROTECT())
    {
        child = fork();
        if (child == 0)
        {
            _exit(0);
        }

        TEST_ASSERT_TRUE(waitpid(child, &status, 0) >= 0);

        /* We're sure that the child's PID doesn't exists anymore */
        pe = procfs_entry_getpid(child);
        TEST_ASSERT_TRUE(pe == NULL);
    }

    if (pe != NULL) procfs_entry_del(pe);
    if (child != -1)
    {
        int status;
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
}

/* Restore the warning we had been ignoring */
#ifndef __clang__
#pragma GCC diagnostic pop
#endif

void run_test_procfs(void)
{
    RUN_TEST(test_procfs_list);
    RUN_TEST(test_procfs_getpid);
    RUN_TEST(test_procfs_getpid_neg);
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

    if (!parse_opts(argc, argv))
    {
        return false;
    }

    if (opt_verbose)
        log_open("PROCFS_TEST", LOG_OPEN_STDOUT);

    UNITY_BEGIN();

    run_test_procfs();

    return UNITY_END();
}
