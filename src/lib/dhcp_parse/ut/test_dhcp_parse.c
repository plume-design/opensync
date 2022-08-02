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
#include <stdlib.h>
#include <string.h>

#include "dhcp_parse.h"
#include "json_util.h"
#include "log.h"
#include "memutil.h"
#include "qm_conn.h"
#include "unity.h"
#include "unit_test_utils.h"
#include "target.h"

#include "pcap.c"

const char *ut_name = "dhcp_parse_tests";

#define OTHER_CONFIG_NELEMS 4
#define OTHER_CONFIG_NELEM_SIZE 128

union fsm_plugin_ops p_ops;
struct dhcp_parse_mgr *g_mgr;

char g_other_configs[][2][OTHER_CONFIG_NELEMS][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "mqtt_v",
            "update_ovsdb",
        },
        {
            "dev-test/dhcp_session_0/4C70D0007B",
            "true",
        },
    },
    {
        {
            "mqtt_v",
            "update_ovsdb",
        },
        {
            "dev-test/dhcp_session_1/4C70D0007B",
            "false",
        },
    },
};

struct fsm_session_conf g_confs[2] =
{
    /* entry 1 */
    {
        .handler = "dhcp_test_session_0",
    },
    /* entry 2 */
    {
        .handler = "dhcp_test_session_1",
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


static void send_report(struct fsm_session *session, char *report) {
#ifndef ARCH_X86
    qm_response_t res;
    bool ret = false;
#endif
    LOGT("%s: msg len: %zu, msg: %s\n, topic: %s",
                     __func__, report ? strlen(report) : 0,
                              report ? report : "None", session->topic);
#ifndef ARCH_X86
        ret = qm_conn_send_direct(QM_REQ_COMPRESS_DISABLE, session->topic,
                                               report, strlen(report), &res);
            if (!ret) LOGE("error sending mqtt with topic %s", session->topic);
#endif
    if (report == NULL) return;
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
dhcp_parse_setUp(void)
{
    size_t n_sessions;
    size_t i;

    g_mgr = NULL;

    /* Reset sessions, register them to the plugin */
    n_sessions = ARRAY_SIZE(g_sessions);
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        dhcp_plugin_init(session);
    }
    g_mgr = dhcp_get_mgr();

    ut_prepare_pcap(Unity.CurrentTestName);

    return;
}

void
dhcp_parse_tearDown(void)
{
    size_t n_sessions;
    size_t i;


    /* Reset sessions, unregister them */
    n_sessions = ARRAY_SIZE(g_sessions);
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];
        dhcp_plugin_exit(session);
    }
    g_mgr = NULL;

    ut_cleanup_pcap();

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

void test_dhcp_parse_pkt(void)
{
    struct fsm_session              *session;
    struct dhcp_session             *d_session;
    struct dhcp_parser              *parser;
    struct net_header_parser        *net_parser;
    struct dhcp_lease               *lease;
    struct dhcp_local_domain        *domain;
    struct schema_DHCP_leased_IP    *dlip;
    size_t                          len;
    size_t                          ncnt = 0;
    ds_tree_t                       *tree;

    const char* const exp_domain_list[] ={"aws.wildfire.exchange",
                                          "corp.pa.plume.tech",
                                          "inf.pa.ops.plume.tech",
                                          "lj.wildfire.exchange",
                                          "pa.ops.plume.tech",
                                          "sf.wildfire.exchange",
                                          "wifi.pa.plume.tech"};

    session = &g_sessions[1];
    d_session = dhcp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(d_session);

    parser = &d_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    parser->net_parser = net_parser;
    // DHCP DISCOVER
    UT_CREATE_PCAP_PAYLOAD(pkt120, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = dhcp_parse_message(parser);

    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt120), net_parser->packet_len);

    dhcp_process_message(d_session);

    lease = ds_tree_head(&d_session->dhcp_leases);
    TEST_ASSERT_TRUE(lease != NULL);

    dlip = &lease->dlip;

    TEST_ASSERT_EQUAL_STRING("00:e1:33:00:0a:a5", dlip->hwaddr);
    TEST_ASSERT_EQUAL_UINT(7776000, dlip->lease_time);

    // DHCP OFFER
    UT_CREATE_PCAP_PAYLOAD(pkt125, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = dhcp_parse_message(parser);

    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt125), net_parser->packet_len);

    dhcp_process_message(d_session);

    lease = ds_tree_head(&d_session->dhcp_leases);
    TEST_ASSERT_TRUE(lease != NULL);

    dlip = &lease->dlip;
    TEST_ASSERT_EQUAL_UINT(86400, dlip->lease_time);
    // DHCP REQUEST
    UT_CREATE_PCAP_PAYLOAD(pkt139, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = dhcp_parse_message(parser);

    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt139), net_parser->packet_len);

    dhcp_process_message(d_session);

    lease = ds_tree_head(&d_session->dhcp_leases);
    TEST_ASSERT_TRUE(lease != NULL);

    dlip = &lease->dlip;
    TEST_ASSERT_EQUAL_STRING("192.168.1.23", dlip->inet_addr);
    // DHCP ACK
    UT_CREATE_PCAP_PAYLOAD(pkt140, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = dhcp_parse_message(parser);

    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt140), net_parser->packet_len);

    dhcp_process_message(d_session);

    lease = ds_tree_head(&d_session->dhcp_leases);
    TEST_ASSERT_TRUE(lease != NULL);

    dlip = &lease->dlip;

    TEST_ASSERT_EQUAL_STRING("192.168.1.23", dlip->inet_addr);
    TEST_ASSERT_EQUAL_STRING("00:e1:33:00:0a:a5", dlip->hwaddr);
    TEST_ASSERT_EQUAL_UINT(86400, dlip->lease_time);
    TEST_ASSERT_EQUAL_STRING("Shadowfax", dlip->hostname);
    TEST_ASSERT_EQUAL_STRING("1,121,3,6,15,119,252,95,44,46", dlip->fingerprint);

    // DHCP ACK with Option 119
    UT_CREATE_PCAP_PAYLOAD(pkt119, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = dhcp_parse_message(parser);

    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt119), net_parser->packet_len);

    dhcp_process_message(d_session);

    tree = &d_session->dhcp_local_domains;
    domain = ds_tree_head(tree);
    TEST_ASSERT_TRUE(domain != NULL);
    while (ncnt < d_session->num_local_domains)
    {
        TEST_ASSERT_EQUAL_STRING(exp_domain_list[ncnt], domain->name);
        domain = ds_tree_next(tree, domain);
        ncnt++;
    }

    // DHCP RELEASE
    UT_CREATE_PCAP_PAYLOAD(pkt353, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = dhcp_parse_message(parser);

    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt353), net_parser->packet_len);

    dhcp_process_message(d_session);
    FREE(net_parser);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name, global_test_init, global_test_exit);
    ut_setUp_tearDown(ut_name, dhcp_parse_setUp, dhcp_parse_tearDown);

    RUN_TEST(test_load_unload_plugin);
    RUN_TEST(test_dhcp_parse_pkt);

    return ut_fini();
}
