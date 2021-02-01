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

#include "gatekeeper.h"
#include "gatekeeper_curl.h"
#include "json_util.h"
#include "log.h"
#include "target.h"
#include "unity.h"


static void send_report(struct fsm_session *session, char *report)
{
    LOGN("%s()sending report", __func__);
}

struct fsm_session_conf g_confs[2] =
{
    {
        .handler = "upnp_test_session_0",
    },
    {
        .handler = "upnp_test_session_1",
    }
};

struct fsm_session_ops g_ops =
{
    .send_report = send_report,
};

union fsm_plugin_ops g_plugin_ops;

struct fsm_session g_sessions[2] =
{
    {
        .type = FSM_WEB_CAT,
        .conf = &g_confs[0],
        .p_ops = &g_plugin_ops,
    },
    {
        .type = FSM_WEB_CAT,
        .conf = &g_confs[1],
        .p_ops = &g_plugin_ops,
    }
};

const char *test_name = "fsm_gk_tests";
struct ev_loop  *g_loop;

void setUp(void)
{
    size_t i;
    g_loop = EV_DEFAULT;

    for (i = 0; i < 1; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        session->name = g_confs[i].handler;
        session->ops  = g_ops;
        session->loop =  g_loop;
        gatekeeper_plugin_init(session);
    }

    return;
}

void tearDown(void)
{
    return;
}


void
test_init(void)
{
    struct fsm_session *session = &g_sessions[0];
    struct fsm_web_cat_ops *cat_ops;
    struct fsm_policy_req req;
    struct fsm_policy fpolicy;

    memset(&fpolicy, 0, sizeof(fpolicy));
    memset(&req, 0, sizeof(req));
    req.url = "https://34.70.91.156";

    cat_ops = &session->p_ops->web_cat_ops;
    cat_ops->categories_check(session, &req, &fpolicy);

    ev_run(session->loop, 0);

}


/**
 * @brief check if curl exit is successful when invoked
 */
void
test_curl_exit(void)
{
    bool ret = false;

    struct fsm_session *session = &g_sessions[0];

    TEST_ASSERT_NOT_NULL(session);
    ret = gk_curl_exit();
    TEST_ASSERT_TRUE(ret);
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_NOTICE);

    UnityBegin(test_name);

    RUN_TEST(test_init);
    RUN_TEST(test_curl_exit);

    return UNITY_END();

    return 0;
}
