#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "const.h"
#include "fsm.h"
#include "http_parse.h"
#include "json_util.h"
#include "log.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "ovsdb_utils.h"
#include "qm_conn.h"
#include "unit_test_utils.h"
#include "unity.h"

#include "pcap.c"

const char *ut_name = "http_parse_tests";

#define OTHER_CONFIG_NELEMS 4
#define OTHER_CONFIG_NELEM_SIZE 32

union fsm_plugin_ops p_ops;
struct http_cache *g_mgr;

char g_other_configs[][2][OTHER_CONFIG_NELEMS][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "mqtt_v",
        },
        {
            "dev-test/http_ut_topic",
        },
    },
};

struct fsm_session_conf g_confs[2] =
{
    {
        .handler = "http_test_session_0",
    },
    {
        .handler = "http_test_session_1",
    }
};


static void send_report(struct fsm_session *session, char *report)
{
#ifndef ARCH_X86
    qm_response_t res;
    bool ret = false;
#endif

    LOGT("%s: msg len: %zu, msg: %s\n, topic: %s",
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


void global_test_init(void)
{
    size_t n_sessions;
    size_t i;

    g_mgr = NULL;

    /* Reset sessions, register them to the plugin */
    n_sessions = ARRAY_SIZE(g_sessions);
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
                                                  g_other_configs[0][0],
                                                  g_other_configs[0][1]);
        pair = ds_tree_find(session->conf->other_config, "mqtt_v");
        session->topic = pair->value;
    }
}

void global_test_exit(void)
{
    size_t n_sessions, i;

    g_mgr = NULL;

    /* Reset sessions, register them to the plugin */
    n_sessions = ARRAY_SIZE(g_sessions);
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        free_str_tree(session->conf->other_config);
    }
}

void
http_parse_setUp(void)
{
    size_t n_sessions;
    size_t i;

    g_mgr = NULL;

    /* Reset sessions, register them to the plugin */
    n_sessions = ARRAY_SIZE(g_sessions);
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        http_plugin_init(session);
    }
    g_mgr = http_get_mgr();

    return;
}

void
http_parse_tearDown(void)
{
    size_t n_sessions;
    size_t i;

    /* Reset sessions, unregister them */
    n_sessions = ARRAY_SIZE(g_sessions);
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        http_plugin_exit(session);
    }
    g_mgr = NULL;

    return;
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
}


void test_http_get_user_agent(void)
{
    struct fsm_session *session;
    struct http_session *h_session;
    struct fsm_http_parser *parser;
    struct net_header_parser *net_parser;
    struct http_device *hdev;
    struct http_parse_report *http_report;
    char *expected_user_agent = "test_fsm_1";
    size_t len;

    ut_prepare_pcap(__func__);

    session = &g_sessions[0];
    h_session = http_lookup_session(session);
    TEST_ASSERT_NOT_NULL(h_session);

    parser = &h_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    parser->net_parser = net_parser;
    UT_CREATE_PCAP_PAYLOAD(pkt372, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = http_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt372), net_parser->packet_len);
    http_process_message(h_session);

    /* Look up expected user agent */
    hdev = http_lookup_device(h_session);
    TEST_ASSERT_NOT_NULL(hdev);
    http_report = http_lookup_report(hdev, expected_user_agent);
    TEST_ASSERT_NOT_NULL(http_report);

    ut_cleanup_pcap();

    FREE(net_parser);
}


int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name, global_test_init, global_test_exit);
    ut_setUp_tearDown(ut_name, http_parse_setUp, http_parse_tearDown);

    RUN_TEST(test_load_unload_plugin);
    RUN_TEST(test_http_get_user_agent);

    return ut_fini();
}
