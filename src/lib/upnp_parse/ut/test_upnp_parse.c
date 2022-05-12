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
#include "json_util.h"
#include "log.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "os_nif.h"
#include "ovsdb_utils.h"
#include "qm_conn.h"
#include "unit_test_utils.h"
#include "unity.h"
#include "upnp_curl.h"
#include "upnp_parse.h"
#include "json_mqtt.h"

#include "pcap.c"

const char *ut_name = "upnp_parse_tests";

#define OTHER_CONFIG_NELEMS 4
#define OTHER_CONFIG_NELEM_SIZE 64

union fsm_plugin_ops p_ops;
struct upnp_cache *g_mgr;

char g_other_configs[][2][OTHER_CONFIG_NELEMS][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "mqtt_v",
        },
        {
            "dev-ut/UPnP/Devices/ut_depl/ut_node_id_1/ut_location_id",
        },
    },
};


struct fsm_session_conf g_confs[2] =
{
    {
        .handler = "upnp_test_session_0",
    },
    {
        .handler = "upnp_test_session_1",
    }
};


struct upnp_device_url g_upnp_data =
{
    .url = "http://10.1.0.48:8080/description.xml",
    .dev_type = "ut_upnp_dev_type",
    .friendly_name = "ut_upnp_friendly_name",
    .manufacturer = "ut_upnp_manufacturer",
    .manufacturer_url = "https://ut_upnp.opensync",
    .model_desc = "ut_upnp_model_desc",
    .model_name = "ut_upnp_model_name",
    .model_num = "ut_upnp_model_1",
    .model_url = "https://ut_upnp.opensync/ut_upnp_model_1",
    .serial_num = "ut_upnp_model_1_SN",
    .udn = "ut_upnp_udn",
    .upc = "ut_upnp_upc"
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

static char *
test_fsm_get_network_id(struct fsm_session *session, os_macaddr_t *mac)
{
    return "test_network_id";
}


struct fsm_session_ops g_ops =
{
    .send_report = send_report,
    .get_network_id = test_fsm_get_network_id,
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
                                                  g_other_configs[0][0],
                                                  g_other_configs[0][1]);
        pair = ds_tree_find(session->conf->other_config, "mqtt_v");
        session->topic = pair->value;
        session->node_id = "ut_upnp_node_id";
        session->location_id = "ut_upnp_location_id";
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

void
upnp_setUp(void)
{
    size_t n_sessions;
    size_t i;

    g_mgr = NULL;
    n_sessions = ARRAY_SIZE(g_sessions);

    /* Reset sessions, register them to the plugin */
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        session->name = g_confs[i].handler;
        session->ops  = g_ops;
        upnp_plugin_init(session);
    }
    g_mgr = upnp_get_mgr();

    return;
}

void
upnp_tearDown(void)
{
    size_t n_sessions;
    size_t i;

    n_sessions = ARRAY_SIZE(g_sessions);

    /* Reset sessions, unregister them */
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        upnp_plugin_exit(session);
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

    /* More testing to come */
}


void test_upnp_get_url(void)
{
    struct fsm_session *session;
    struct upnp_session *u_session;
    struct upnp_parser *parser;
    struct net_header_parser *net_parser;
    struct upnp_device_url *url;
    char *expected_url = "http://10.1.0.48:8080/description.xml";
    size_t len;

    ut_prepare_pcap(__func__);

    session = &g_sessions[0];
    u_session = upnp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(u_session);

    parser = &u_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    parser->net_parser = net_parser;
    UT_CREATE_PCAP_PAYLOAD(pkt322, net_parser);

    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = upnp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt322), net_parser->packet_len);

    url = upnp_get_url(u_session);
    TEST_ASSERT_NOT_NULL(url);
    TEST_ASSERT_EQUAL_STRING(expected_url, url->url);

    ut_cleanup_pcap();
    FREE(net_parser);
}


void
test_upnp_report(void)
{
    struct upnp_report to_report;
    struct fsm_session *session;
    char *report;

    session = &g_sessions[0];
    to_report.url = &g_upnp_data;
    to_report.url->udev = CALLOC(1, sizeof(*to_report.url->udev));
    os_nif_macaddr_from_str(&to_report.url->udev->device_mac, "00:01:02:03:04:05");
    to_report.url->session = session;
    upnp_init_elements(&g_upnp_data);
    to_report.nelems = 11;
    to_report.first = upnp_get_elements();
    report = jencode_upnp_report(session, &to_report);
    TEST_ASSERT_NOT_NULL(report);
    session->ops.send_report(session, report);
    FREE(to_report.url->udev);
}



int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name, global_test_init, global_test_exit);
    ut_setUp_tearDown(ut_name, upnp_setUp, upnp_tearDown);

    RUN_TEST(test_load_unload_plugin);
    RUN_TEST(test_upnp_get_url);
    RUN_TEST(test_upnp_report);

    return ut_fini();
}
