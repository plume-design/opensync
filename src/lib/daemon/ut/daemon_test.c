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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include "log.h"
#include "unity.h"
#include "daemon.h"
#include "unit_test_utils.h"

#define PR(...) do { if (opt_verbose) LOG(INFO, __VA_ARGS__); } while (0)

int opt_verbose = 0;
char *test_name = "daemon_test";

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
 *  Daemon testing
 * ===========================================================================
 */
bool test_atexit_triggered = false;

bool test_atexit(daemon_t *pr)
{
    PR("atexit() handler called.\n");
    test_atexit_triggered = true;

    return true;
}

void test_daemon_basic(void)
{
    int ii;
    daemon_t pr;

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_init(&pr, "/bin/ls", DAEMON_LOG_ALL),
            "Unable to create process for executable /bin/ls");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_arg_add(&pr, "--long", "--test"),
            "Error adding arguments.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_arg_add(&pr, "/", "/tmp"),
            "Error adding arguments.");

    PR("Argument list:\n");
    for (ii = 0; ii < pr.dn_argc; ii++)
    {
        PR("\t• %s\n", pr.dn_argv[ii]);
    }

    TEST_ASSERT_TRUE_MESSAGE(
            pr.dn_argv[pr.dn_argc] == NULL,
            "Argument list is not terminated with NULL.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_start(&pr),
            "Error starting process");

    /* Run the EV loop here until ls terminates */
    if (!daemon_wait(&pr, 5.0))
    {
        PR("Timeout reached\n");
    }

    daemon_stop(&pr);

    PR("Done!\n");
}

void test_daemon_wait(void)
{
    daemon_t pr;

    /* Run a sleep of 6 seconds and test that daemon_wait() properly timeouts with an error code */
    PR("Testing daemon_wait()  ...\n");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_init(&pr, "/bin/sleep", DAEMON_LOG_ALL),
            "Unable to create process for executable /bin/sleep");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_arg_add(&pr, "3"),
            "Unable to add arguments.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_start(&pr),
            "Unable to start process.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_wait(&pr, 5.0),
            "daemon_wait() should have returned false (timeout)");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_stop(&pr),
            "Unable to stop process");

    daemon_fini(&pr);
}

void test_daemon_wait_neg(void)
{
    daemon_t pr;
    /* Run a sleep of 6 seconds and test that daemon_wait() properly timeouts with an error code */
    PR("Testing daemon_wait() negative test case  ...\n");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_init(&pr, "/bin/sleep", DAEMON_LOG_ALL),
            "Unable to create process for executable /bin/sleep");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_arg_add(&pr, "6"),
            "Unable to add arguments.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_start(&pr),
            "Unable to start process.");

    TEST_ASSERT_FALSE_MESSAGE(
            daemon_wait(&pr, 5.0),
            "daemon_wait() should have returned false (timeout)");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_stop(&pr),
            "Unable to stop process");

    daemon_fini(&pr);
}

void test_daemon_term(void)
{
    daemon_t pr;
    /**
     *  Set SIGTERM to 0 and kill the process. This tests if we properly wait for a timeout if SIGTERM fails to terminate the process.
     */
    PR("Testing SIGTERM ...\n");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_init(&pr, "/bin/sleep", DAEMON_LOG_ALL),
            "Unable to create process for executable /bin/sleep");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_signal_set(&pr, 0, -1),
            "Unable to set custom SIGTERM");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_arg_add(&pr, "30"),
            "Unable to add arguments.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_start(&pr),
            "Unable to start process.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_stop(&pr),
            "Unable to stop process.");

    daemon_fini(&pr);
}

void test_daemon_kill(void)
{
    daemon_t pr;
    /**
     *  Same as above, but override both SIGTERM and SIGKILL -- the consequence is that daemon_stop() fails to kill the process
     *  and should return an error (false).
     */
    PR("Testing SIGKILL ...\n");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_init(&pr, "/bin/sleep", DAEMON_LOG_ALL),
            "Unable to create process for executable /bin/sleep");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_signal_set(&pr, 0, 0),
            "Unable to set custom SIGTERM");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_arg_add(&pr, "30"),
            "Unable to add arguments.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_start(&pr),
            "Unable to start process.");

    TEST_ASSERT_FALSE_MESSAGE(
            daemon_stop(&pr),
            "Unable to stop process.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_signal_set(&pr, -1, -1),
            "Unable to restore custom SIGTERM, SIGKILL");

    daemon_fini(&pr);
}

void test_daemon_restart(void)
{
    daemon_t pr;
    PR("Testing restart ...\n");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_init(&pr, "/bin/sleep", DAEMON_LOG_ALL),
            "Unable to create process for executable /bin/sleep");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_arg_add(&pr, "1"),
            "Unable to add arguments.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_start(&pr),
            "Unable to start process.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_restart_set(&pr, true, 1.0, 3),
            "Enabling process restart");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_wait(&pr, 30.0),
            "Unable to wait process.");

    daemon_fini(&pr);
}

void test_daemon_atexit(void)
{
    daemon_t pr;

    PR("Testing atexit callback ...\n");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_init(&pr, "/bin/sleep", DAEMON_LOG_ALL),
            "Unable to create process for executable /bin/sleep");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_arg_add(&pr, "1"),
            "Unable to add arguments.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_start(&pr),
            "Unable to start process.");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_atexit(&pr, test_atexit),
            "Setting atexit handler");

    TEST_ASSERT_TRUE_MESSAGE(
            daemon_wait(&pr, 30.0),
            "Unable to wait process.");

    daemon_fini(&pr);

    TEST_ASSERT_TRUE_MESSAGE(
            test_atexit_triggered,
            "atexit() handler did not trigger");
}

int child_status = 0;

/* Install a child watcher */
void stale_child_ev(struct ev_loop *loop, ev_child *w, int revent)
{
    ev_child_stop(loop, w);

    child_status = w->rstatus;
}

/* GCC is complaining about clobbered variables after the fork() or
 * long_jump() inserted by TEST_PROTECT().
 * Ignore this warning.
 */
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wclobbered"
#endif

void test_daemon_stale_instance(void)
{
    FILE *f = NULL;
    pid_t child = -1;
    ev_child w;


    if (TEST_PROTECT())
    {
        child = fork();

        if (child == -1)
        {
            TEST_FAIL_MESSAGE("Unable to spawn a new child.");
        }
        else if (child == 0)
        {
            struct sigaction sa;

            sa.sa_handler = SIG_IGN;
            /* Ignore SIGTERM, just for kicks */
            sigaction(SIGTERM, &sa, NULL);

            while (true)
            {
                sleep(1);
            }

            exit(1);
        }

        /* Install a libev child handler  -- do not use waitpid() as it has funny effects with libev -- long story */
        ev_child_init(&w, stale_child_ev, child, 0);
        ev_child_start(EV_DEFAULT, &w);

        /* Give the child some time to execute */
        sleep(1);

        /* Write the child's pid to the pid file */
        f = fopen("/tmp/stale.pid", "w");
        if (f == NULL)
        {
            TEST_FAIL_MESSAGE("Unable to write the fake PID file");
        }

        fprintf(f, "%jd\n", (intmax_t)child);
        fflush(f);

        /*
         * Simulate a stale instance of a daemon and see if the daemon library properly kills it
         */
        daemon_t pr;

        PR("Testing stale instance kill ...\n");

        TEST_ASSERT_TRUE_MESSAGE(
                daemon_init(&pr, "/bin/sleep",DAEMON_LOG_ALL),
                "Unable to create process for executable /bin/sleep");

        TEST_ASSERT_TRUE_MESSAGE(
                daemon_arg_add(&pr, "1"),
                "Unable to add arguments.");

        TEST_ASSERT_TRUE_MESSAGE(
                daemon_pidfile_set(&pr, "/tmp/stale.pid", false),
                "Unable to set the pid file");

        TEST_ASSERT_TRUE_MESSAGE(
                daemon_start(&pr),
                "Unable to start process.");

        /* cleanup */
        TEST_ASSERT_TRUE_MESSAGE(
                daemon_wait(&pr, 30.0),
                "Unable to wait process.");

        daemon_fini(&pr);
    }

    if (child != -1) kill(child, SIGKILL);
    if (f != NULL) fclose(f);
}
/* Restore the warning we had been ignoring */
#ifndef __clang__
#pragma GCC diagnostic pop
#endif

void run_test_daemon(void)
{
    RUN_TEST(test_daemon_basic);
    RUN_TEST(test_daemon_wait);
    RUN_TEST(test_daemon_wait_neg);
    RUN_TEST(test_daemon_term);
    RUN_TEST(test_daemon_kill);
    RUN_TEST(test_daemon_restart);
    RUN_TEST(test_daemon_atexit);
    RUN_TEST(test_daemon_stale_instance);
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
        log_open("DAEMON_TEST", LOG_OPEN_STDOUT);

    run_test_daemon();

    return ut_fini();
}
