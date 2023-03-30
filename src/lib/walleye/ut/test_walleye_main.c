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

#include "fsm.h"
#include "log.h"
#include "target.h"
#include "unity.h"
#include "walleye_dpi_plugin.h"

void (*g_setUp)(void) = NULL;
void (*g_tearDown)(void) = NULL;

void
setUp(void)
{
    LOGI("%s: Unit test setup", __func__);
}

void
tearDown(void)
{
    LOGI("%s: Unit test tear down", __func__);
    TEST_ASSERT_TRUE(true);
}

void
test_dummy(void)
{
    struct fsm_session *session;
    int rc;

    session = NULL;
    rc = walleye_dpi_plugin_init(session);
    TEST_ASSERT_FALSE(rc == 0);
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("walleye", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_INFO);

    UnityBegin(basename(__FILE__));

    RUN_TEST(test_dummy);

    return UNITY_END();
}
