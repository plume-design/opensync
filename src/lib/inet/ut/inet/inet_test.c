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
#include <string.h>

#include "unity.h"
#include "inet.h"
#include "inet_base.h"
#include "inet_unit.h"
#include "daemon.h"

#include "../tests_common.c"

/*
 * ===========================================================================
 *  INET UNIT test -- TODO
 * ===========================================================================
 */

const intptr_t UNIT_INTERFACE = (intptr_t)"Interface";
const intptr_t UNIT_NETWORK   = (intptr_t)"Network";
const intptr_t UNIT_ERROR     = (intptr_t)"Error";
const intptr_t UNIT_NAT       = (intptr_t)"NAT";
const intptr_t UNIT_OK        = (intptr_t)"OK";

void setUp() {}
void tearDown() {}

bool pr_walk(inet_unit_t *unit, void *ctx, bool descend)
{
    int *level = ctx;

    if (descend) (*level)++;
    if (!descend)
    {
        (*level)--;
        return true;
    }

    char *status = unit->un_status ? "✔" : "✘";
    char *action = " ";

    if (unit->un_error)
    {
        action = "⚠";
    }
    else if (unit->un_enabled && unit->un_status && unit->un_pending)
    {
        action = "↻";
    }
    else if (unit->un_enabled && !unit->un_status)
    {
        action = "⬆";
    }
    else if (!unit->un_enabled && unit->un_status)
    {
        action = "⬇";
    }
    else
    {
        action = " ";
    }

    printf("%*s %s %s %s\n",
            ((*level) - 1) * 5,
            "",
            status,
            (char *)unit->un_id,
            action);


    return true;
}

bool commit_fn(void *ctx, intptr_t unit_id, bool enable)
{
    (void)ctx;

    bool retval = true;

    char *desc = enable ? "▶" : "◼";

    if (unit_id == UNIT_ERROR)
    {
        desc = "⚠";
        retval = false;
    }

    printf("+ %s %s\n", desc, (char *)unit_id);

    return retval;
}

void test_inet_unit(void)
{
    /*
     * Define the unit hierarchy for this interface
     */
    inet_unit_t *unit =
        inet_unit(UNIT_INTERFACE,
                inet_unit_s(UNIT_NETWORK,
                        inet_unit(UNIT_NAT, NULL),
                        inet_unit((intptr_t)"Address",
                                inet_unit_s((intptr_t)"DHCPC", NULL),
                                inet_unit_s((intptr_t)"DHCPD", NULL),
                                inet_unit_s((intptr_t)"DNS", NULL),
                                NULL),
                        NULL),
                inet_unit(UNIT_ERROR,
                        inet_unit(UNIT_OK, NULL),
                        NULL),
                NULL);

    printf("+ Enabling Interface\n");
    inet_unit_start(unit, UNIT_INTERFACE);

    printf("+ Commit 1\n");
    inet_unit_commit(unit, commit_fn, NULL);

    int level = 0;
    inet_unit_walk(unit, pr_walk, &level);

    printf("+ Enabling NAT ...\n");
    if (!inet_unit_start(unit, UNIT_NAT))
    {
        printf("Error enabling NAT\n");
        return;
    }

    printf("+ Enabling Network ...\n");
    if (!inet_unit_start(unit, UNIT_NETWORK))
    {
        return;
    }

    inet_unit_walk(unit, pr_walk, &level);

    printf("+ Commit 2\n");
    inet_unit_commit(unit, commit_fn, NULL);

    printf("+ Commit 3\n");
    inet_unit_commit(unit, commit_fn, NULL);

    printf("+ Disabling Network\n");
    if (!inet_unit_stop(unit, UNIT_NETWORK))
    {
        return;
    }

    inet_unit_walk(unit, pr_walk, &level);

    printf("+ Commit 4\n");
    inet_unit_commit(unit, commit_fn, NULL);

    printf("+ Re-enabling Network\n");
    if (!inet_unit_start(unit, UNIT_NETWORK))
    {
        return;
    }

    inet_unit_walk(unit, pr_walk, &level);

    printf("+ Commit 5\n");
    inet_unit_commit(unit, commit_fn, NULL);

    printf("+ Commit 6\n");
    inet_unit_commit(unit, commit_fn, NULL);

    printf("+ Re-re-enabling Network\n");
    if (!inet_unit_start(unit, UNIT_NETWORK))
    {
        return;
    }

    inet_unit_walk(unit, pr_walk, &level);

    printf("+ Commit 7\n");
    inet_unit_commit(unit, commit_fn, NULL);

    printf("+ Enabling Error\n");
    if (!inet_unit_start(unit, UNIT_ERROR))
    {
        return;
    }

    printf("+ Enabling OK\n");
    if (!inet_unit_start(unit, UNIT_OK))
    {
        return;
    }

    inet_unit_walk(unit, pr_walk, &level);

    printf("+ Commit 8\n");
    inet_unit_commit(unit, commit_fn, NULL);

    inet_unit_walk(unit, pr_walk, &level);

    inet_unit_free(unit);

    return;
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
        log_open("INET_TEST", LOG_OPEN_STDOUT);

    UNITY_BEGIN();

#if 0
    RUN_TEST(test_inet_iflist);
    RUN_TEST(test_inet_test);
    RUN_TEST(test_inet_unit);
#endif

    return UNITY_END();
}
