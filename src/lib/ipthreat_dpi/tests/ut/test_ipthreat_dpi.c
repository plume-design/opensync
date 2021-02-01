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

#include "ipthreat_dpi.h"
#include "json_util.h"
#include "log.h"
#include "qm_conn.h"
#include "target.h"
#include "unity.h"

const char *test_name = "ipthreat_dpi_tests";

#define OTHER_CONFIG_NELEMS 5
#define OTHER_CONFIG_NELEM_SIZE 128

char g_other_configs[][2][OTHER_CONFIG_NELEMS][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "provider_plugin",
        },
        {
            "test_provider",
        }
    },
    {
        {
            "mqtt_v",
        },
        {
            "dev-test/ipthreat_dpi_1/4C70D0007B",
        },
    },
    {
        {
            "mqtt_v",
        },
        {
            "dev-test/fsm_core_ut/topic_2",
        },
    }
};

/**
 * @brief a set of sessions as delivered by the ovsdb API
 */
struct fsm_session_conf g_confs[3] =
{
    {
        .handler = "fsm_iptd_test_0",
    },
    {
        .handler = "fsm_iptd_test_1",
    },
    {
        .handler = "test_provider",
    }
};


union fsm_plugin_ops p_ops;


struct fsm_session g_sessions[3] =
{
    {
        .type = FSM_DPI_PLUGIN,
        .conf = &g_confs[0],
    },
    {
        .type = FSM_DPI_PLUGIN,
        .conf = &g_confs[1],
    },
    {
        .type = FSM_WEB_CAT,
        .conf = &g_confs[2],
    }
};

char *
get_other_config_val(struct fsm_session *session, char *key)
{
    struct fsm_session_conf *fconf;
    struct str_pair *pair;
    ds_tree_t *tree;

    if (session == NULL) return NULL;

    fconf = session->conf;
    if (fconf == NULL) return NULL;

    tree = fconf->other_config;
    if (tree == NULL) return NULL;

    pair = ds_tree_find(tree, key);
    if (pair == NULL) return NULL;

    return pair->value;
}

/**
 * @brief sends a json mqtt report
 *
 * Sends and frees a json report over mqtt.
 * when running UTs on the pod, actually send the report
 * as QM is expected to be running.
 * When running UTs on X86 platforms, simply free the report.
 * @params session the fsm session owning the mqtt topic.
 * @params the json report.
 */
static void
send_report(struct fsm_session *session, char *report)
{
#ifndef ARCH_X86
    qm_response_t res;
    bool ret = false;
#endif

    LOGD("%s: msg len: %zu, msg: %s\n, topic: %s",
         __func__, report ? strlen(report) : 0,
         report ? report : "None", session->topic);
    if (report == NULL) return;

#ifndef ARCH_X86
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_DISABLE, session->topic,
                              report, strlen(report), &res);
    if (!ret) LOGE("error sending mqtt with topic %s", session->topic);
#endif
    json_free(report);

    return;
}

struct fsm_session_ops g_ops =
{
    .send_report = send_report,
    .get_config = get_other_config_val,
};



struct ipthreat_dpi_cache *g_mgr;

char *g_location_id = "foo";
char *g_node_id = "bar";

void global_test_init(void)
{
    size_t n_sessions, i;

    g_mgr = NULL;
    n_sessions = sizeof(g_sessions) / sizeof(struct fsm_session);

    /* Reset sessions, register them to the plugin */
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];
        struct str_pair *pair;

        session->conf = &g_confs[i];
        session->ops  = g_ops;
        session->p_ops = &p_ops;
        session->name = g_confs[i].handler;
        session->conf->other_config = schema2tree(OTHER_CONFIG_NELEM_SIZE,
                                                  OTHER_CONFIG_NELEM_SIZE,
                                                  OTHER_CONFIG_NELEMS,
                                                  g_other_configs[i][0],
                                                  g_other_configs[i][1]);
        pair = ds_tree_find(session->conf->other_config, "mqtt_v");
        if (pair) session->topic = pair->value;
        session->location_id = g_location_id;
        session->node_id = g_location_id;
    }
}

void global_test_exit(void)
{
    size_t n_sessions, i;

    g_mgr = NULL;
    n_sessions = sizeof(g_sessions) / sizeof(struct fsm_session);

    /* Reset sessions, register them to the plugin */
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        free_str_tree(session->conf->other_config);
    }
}

/**
 * @brief called by the Unity framework before every single test
 */
void setUp(void)
{
    size_t n_sessions, i;

    g_mgr = NULL;
    n_sessions = sizeof(g_sessions) / sizeof(struct fsm_session);

    /* Reset sessions, register them to the plugin */
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        ipthreat_dpi_plugin_init(session);
    }
    g_mgr = ipthreat_dpi_get_mgr();

    return;
}

/**
 * @brief called by the Unity framework after every single test
 */
void tearDown(void)
{
    size_t n_sessions, i;

    n_sessions = sizeof(g_sessions) / sizeof(struct fsm_session);

    /* Reset sessions, unregister them */
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        ipthreat_dpi_plugin_exit(session);
    }
    g_mgr = NULL;

    return;
}

/**
 * @brief validate that no session provided is handled correctly
 */
void test_no_session(void)
{
    int ret;

    ret = ipthreat_dpi_plugin_init(NULL);
    TEST_ASSERT_TRUE(ret == -1);
}


/**
 * @brief test plugin init()/exit() sequence
 *
 * Validate plugin reference counts and pointers
 */
void test_load_unload_plugin(void)
{
    /* SetUp() has called init(). Validate settings */
    TEST_ASSERT_NOT_NULL(g_mgr);

    /*
     * tearDown() will call exit().
     * ASAN enabled run of the test will validate that
     * there is no memory leak.
     */
}

int main(int argc, char *argv[])
{
    /* Set the logs to stdout */
    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    global_test_init();

    RUN_TEST(test_no_session);
    RUN_TEST(test_load_unload_plugin);

    global_test_exit();

    return UNITY_END();
}
