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

#include <stdbool.h>

#include "fsm.h"
#include "log.h"
#include "memutil.h"
#include "os.h"
#include "unity.h"

void
test_fsm_parse_dso(void)
{
    struct fsm_session session;
    bool rc;
    log_severity_t old_log;

    old_log = log_severity_get();
    log_severity_set(LOG_SEVERITY_TRACE);

    MEMZERO(session);

    rc = fsm_parse_dso(&session);
    TEST_ASSERT_FALSE(rc);
    TEST_ASSERT_NULL(session.dso);

    session.name = "TEST";
    rc = fsm_parse_dso(&session);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL_STRING(CONFIG_INSTALL_PREFIX"/lib/libfsm_TEST.so", session.dso);
    FREE(session.dso);

    session.conf = CALLOC(1, sizeof(*session.conf));

    session.conf->plugin = "/ABSOLUTE/PATH/libfoo.so";
    rc = fsm_parse_dso(&session);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL_STRING("/ABSOLUTE/PATH/libfoo.so", session.dso);
    FREE(session.dso);

    session.conf->plugin = "RELATIVE/PATH/libfoo.so";
    rc = fsm_parse_dso(&session);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL_STRING(CONFIG_INSTALL_PREFIX"/RELATIVE/PATH/libfoo.so", session.dso);
    FREE(session.dso);

    FREE(session.conf);

    log_severity_set(old_log);
}

void
run_test_fsm_ovsdb(void)
{
    RUN_TEST(test_fsm_parse_dso);
}
