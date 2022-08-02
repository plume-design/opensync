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
#include <stdio.h>
#include <string.h>

#include "const.h"
#include "fsm.h"
#include "fsm_demo_plugin.h"
#include "json_util.h"
#include "log.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "network_metadata_report.h"
#include "ovsdb_utils.h"
#include "qm_conn.h"
#include "unit_test_utils.h"
#include "unity.h"

#include "pcap.c"


const char *ut_name = "fsm_demo_plugin_tests";

#define OTHER_CONFIG_NELEMS 4
#define OTHER_CONFIG_NELEM_SIZE 128

struct fsm_demo_plugin_cache *g_mgr;
union fsm_plugin_ops p_ops;
char *g_location_id = "foo";
char *g_node_id = "bar";

char g_other_configs[][2][OTHER_CONFIG_NELEMS][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "mqtt_v",
        },
        {
            "dev-test/fsm_demo_plugin_0/4C70D0007B",
        },
    },
    {
        {
            "mqtt_v",
        },
        {
            "dev-test/fsm_demo_plugin_1/4C70D0007B",
        },
    },
};


/**
 * @brief a set of sessions as delivered by the ovsdb API
 */
struct fsm_session_conf g_confs[2] =
{
    {
        .handler = "fsm_demo_test_0",
    },
    {
        .handler = "fsm_demo_test_1",
    }
};


struct fsm_session g_sessions[2] =
{
    {
        .type = FSM_WEB_CAT,
        .conf = &g_confs[0],
    },
    {
        .type = FSM_WEB_CAT,
        .conf = &g_confs[1],
    }
};

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
};


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
        session->topic = pair->value;
        session->location_id = g_location_id;
        session->node_id = g_node_id;
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
void
fsm_demo_plugin_setUp(void)
{
    size_t n_sessions;
    size_t i;

    g_mgr = NULL;

    /* Reset sessions, register them to the plugin */
    n_sessions = ARRAY_SIZE(g_sessions);
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        fsm_demo_plugin_init(session);
    }
    g_mgr = fsm_demo_get_mgr();

    return;
}

/**
 * @brief called by the Unity framework after every single test
 */
void
fsm_demo_plugin_tearDown(void)
{
    size_t n_sessions;
    size_t i;

    /* Reset sessions, unregister them */
    n_sessions = ARRAY_SIZE(g_sessions);
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        fsm_demo_plugin_exit(session);
    }
    g_mgr = NULL;

    return;
}

/**
 * @brief emits a protobuf report
 *
 * Assumes the presence of QM to send the report on non native platforms,
 * simply resets the aggregator content for native target.
 * @param aggr the aggregator
 */
static void test_emit_report(struct fsm_session *session,
                             struct net_md_aggregator *aggr)
{
#ifndef ARCH_X86
    bool ret;

    /* Send the report */
    ret = net_md_send_report(aggr, session->topic);
    TEST_ASSERT_TRUE(ret);
#else
    struct packed_buffer *pb;
    pb = serialize_flow_report(aggr->report);

    /* Free the serialized container */
    free_packed_buffer(pb);
    FREE(pb);
    net_md_reset_aggregator(aggr);
#endif
}

/**
 * @brief validate that no session provided is handled correctly
 */
void test_no_session(void)
{
    int ret;

    ret = fsm_demo_plugin_init(NULL);
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


/**
 * @brief validate the generation of a json mqtt report
 */
void test_json_report(void)
{
    struct fsm_session *session;
    char *report;

    /* Select the first active session */
    session = &g_sessions[0];

    /* Generate a report */
    report = demo_jencode_demo_event(session);

    /* Validate that the report was generated */
    TEST_ASSERT_NOT_NULL(report);

    /* Send the report */
    session->ops.send_report(session, report);
}

/**
 * @brief validate message parsing
 */
void test_process_msg(void)
{
    struct net_header_parser *net_parser;
    struct fsm_demo_session *f_session;
    struct fsm_demo_parser *parser;
    struct fsm_session *session;
    size_t len;

    ut_prepare_pcap(__func__);

    /* Select the first active session */
    session = &g_sessions[0];
    f_session = fsm_demo_lookup_session(session);
    TEST_ASSERT_NOT_NULL(f_session);

    parser = &f_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    parser->net_parser = net_parser;
    UT_CREATE_PCAP_PAYLOAD(pkt9568, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = fsm_demo_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    fsm_demo_process_message(f_session);
    net_md_close_active_window(f_session->aggr);
    test_emit_report(session, f_session->aggr);
    FREE(net_parser);

    ut_cleanup_pcap();
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name, global_test_init, global_test_exit);
    ut_setUp_tearDown(ut_name, fsm_demo_plugin_setUp, fsm_demo_plugin_tearDown);

    RUN_TEST(test_no_session);
    RUN_TEST(test_load_unload_plugin);
    RUN_TEST(test_json_report);
    RUN_TEST(test_process_msg);

    return ut_fini();
}
