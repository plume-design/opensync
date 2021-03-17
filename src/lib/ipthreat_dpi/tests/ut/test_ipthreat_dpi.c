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
            "inbound_policy_table",
            "outbound_policy_table",
            "mqtt_v",
        },
        {
            "test_provider",
            "inbound_ipthreat",
            "outbound_ipthreat",
            "dev-test/ipthreat_ut/topic_1",
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

struct schema_FSM_Policy spolicies[] =
{
    { /* entry 0 */
        .policy_exists = true,
        .policy = "inbound_ipthreat",
        .name = "inbound_ipthreat",
        .idx = 1,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .risk_op_exists = false,
        .risk_op = "lte",
        .risk_level = 7,
        .ipaddr_op_exists = false,
        .action_exists = true,
        .action = "drop",
        .log_exists = true,
        .log = "blocked",
    },
    { /* entry 1 */
        .policy_exists = true,
        .policy = "outbound_ipthreat",
        .name = "outbound_ipthreat",
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .risk_op = "lte",
        .risk_level = 7,
        .ipaddr_op_exists = false,
        .idx = 2,
        .action_exists = true,
        .action = "drop",
        .log_exists = true,
        .log = "blocked",
    },
    { /* entry 2 */
        .policy_exists = true,
        .policy = "inbound_ipthreat",
        .name = "inbound_ipthreat",
        .idx = 3,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .risk_op_exists = false,
        .risk_op = "lte",
        .risk_level = 7,
        .ipaddr_op_exists = false,
        .action_exists = true,
        .action = "allow",
        .log_exists = true,
        .log = "all",
    },
    { /* entry 3 */
        .policy_exists = true,
        .policy = "outbound_ipthreat",
        .name = "outbound_ipthreat",
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .risk_op = "lte",
        .risk_level = 7,
        .ipaddr_op_exists = false,
        .idx = 4,
        .action_exists = true,
        .action = "allow",
        .log_exists = true,
        .log = "all",
    }
};

struct test_tuple
{
    int family;
    uint16_t transport;
    char *src_ip;
    char *dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    int direction;
    int originator;
};

struct test_tuple g_v4_tuple =
{
    .family = AF_INET,
    .transport = IPPROTO_UDP,
    .src_ip = "1.2.3.4",
    .dst_ip = "8.7.6.5",
    .src_port = 12345,
    .dst_port = 34512,
    .direction = NET_MD_ACC_OUTBOUND_DIR,
    .originator = NET_MD_ACC_ORIGINATOR_SRC,
};

struct test_tuple g_v4_inbound_tuple =
{
    .family = AF_INET,
    .transport = IPPROTO_UDP,
    .src_ip = "8.7.6.5",
    .dst_ip = "1.2.3.4",
    .src_port = 34512,
    .dst_port = 12345,
    .direction = NET_MD_ACC_INBOUND_DIR,
    .originator = NET_MD_ACC_ORIGINATOR_SRC,
};

struct test_tuple g_v6_tuple =
{
    .family = AF_INET6,
    .transport = IPPROTO_UDP,
    .src_ip = "1:2::3",
    .dst_ip = "4:5::6",
    .src_port = 12345,
    .dst_port = 34512,
    .direction = NET_MD_ACC_OUTBOUND_DIR,
    .originator = NET_MD_ACC_ORIGINATOR_SRC,
};

struct test_tuple g_v6_inbound_tuple =
{
    .family = AF_INET6,
    .transport = IPPROTO_UDP,
    .src_ip = "4:5::6",
    .dst_ip = "1:2::3",
    .src_port = 34512,
    .dst_port = 12345,
    .direction = NET_MD_ACC_INBOUND_DIR,
    .originator = NET_MD_ACC_ORIGINATOR_SRC,
};

os_macaddr_t g_src_mac =
{
    .addr = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 },
};

os_macaddr_t g_dst_mac =
{
    .addr = { 0x77, 0x88, 0x99, 0x11, 0x22, 0x33 },
};

struct net_md_stats_accumulator g_v4_outbound_acc, g_v4_inbound_acc;
struct net_md_stats_accumulator g_v6_outbound_acc, g_v6_inbound_acc;
struct net_md_flow_key g_v4_outbound_key, g_v4_inbound_key;
struct net_md_flow_key g_v6_outbound_key, g_v6_inbound_key;
struct eth_header outbound_eth6_header, inbound_eth6_header;
struct eth_header outbound_eth_header, inbound_eth_header;

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

union fsm_plugin_ops p_ops;

struct fsm_web_cat_ops g_plugin_ops =
{
    .categories_check = NULL,
    .risk_level_check = NULL,
    .cat2str = NULL,
    .get_stats = NULL,
    .dns_response = NULL,
    .gatekeeper_req = NULL,
};

struct fsm_session_ops g_ops =
{
    .send_report = send_report,
    .get_config = get_other_config_val,
};

char *g_location_id = "foo";
char *g_node_id = "bar";

struct ipthreat_dpi_cache *g_mgr;

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
        session->provider_ops = &g_plugin_ops;
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
    struct fsm_policy_session *mgr;
    int rc;

    mgr = fsm_policy_get_mgr();
    if (!mgr->initialized) fsm_init_manager();

    /* Create an ipv4 outbound accumulator */
    memset(&g_v4_outbound_acc, 0, sizeof(g_v4_outbound_acc));
    memset(&g_v4_outbound_key, 0, sizeof(g_v4_outbound_key));
    memset(&outbound_eth_header, 0, sizeof(outbound_eth_header));

    outbound_eth_header.srcmac = &g_src_mac;
    outbound_eth_header.dstmac = &g_dst_mac;
    outbound_eth_header.ethertype = 0x0800;
    g_v4_outbound_key.src_ip = calloc(1, 4);
    TEST_ASSERT_NOT_NULL(g_v4_outbound_key.src_ip);
    rc = inet_pton(AF_INET, g_v4_tuple.src_ip, g_v4_outbound_key.src_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v4_outbound_key.dst_ip = calloc(1, 4);
    TEST_ASSERT_NOT_NULL(g_v4_outbound_key.dst_ip);
    rc = inet_pton(AF_INET, g_v4_tuple.dst_ip, g_v4_outbound_key.dst_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v4_outbound_key.ip_version = 4;
    g_v4_outbound_key.ipprotocol = g_v4_tuple.transport;
    g_v4_outbound_key.sport = htons(g_v4_tuple.src_port);
    g_v4_outbound_key.dport = htons(g_v4_tuple.dst_port);

    g_v4_outbound_acc.key = &g_v4_outbound_key;
    g_v4_outbound_acc.direction = g_v4_tuple.direction;
    g_v4_outbound_acc.originator = g_v4_tuple.originator;

    /* Create an ipv4 inbound accumulator */
    memset(&g_v4_inbound_acc, 0, sizeof(g_v4_inbound_acc));
    memset(&g_v4_inbound_key, 0, sizeof(g_v4_inbound_key));
    memset(&inbound_eth_header, 0, sizeof(inbound_eth_header));

    inbound_eth_header.srcmac = &g_dst_mac;
    inbound_eth_header.dstmac = &g_src_mac;
    inbound_eth_header.ethertype = 0x0800;
    g_v4_inbound_key.src_ip = calloc(1, 4);
    TEST_ASSERT_NOT_NULL(g_v4_inbound_key.src_ip);
    rc = inet_pton(AF_INET, g_v4_inbound_tuple.src_ip, g_v4_inbound_key.src_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v4_inbound_key.dst_ip = calloc(1, 4);
    TEST_ASSERT_NOT_NULL(g_v4_inbound_key.dst_ip);
    rc = inet_pton(AF_INET, g_v4_inbound_tuple.dst_ip, g_v4_inbound_key.dst_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v4_inbound_key.ip_version = 4;
    g_v4_inbound_key.ipprotocol = g_v4_inbound_tuple.transport;
    g_v4_inbound_key.sport = htons(g_v4_inbound_tuple.src_port);
    g_v4_inbound_key.dport = htons(g_v4_inbound_tuple.dst_port);

    g_v4_inbound_acc.key = &g_v4_inbound_key;
    g_v4_inbound_acc.direction = g_v4_inbound_tuple.direction;
    g_v4_inbound_acc.originator = g_v4_inbound_tuple.originator;

    /* Create an ipv6 outbound accumulator */
    memset(&g_v6_outbound_acc, 0, sizeof(g_v6_outbound_acc));
    memset(&g_v6_outbound_key, 0, sizeof(g_v6_outbound_key));
    memset(&outbound_eth6_header, 0, sizeof(outbound_eth6_header));

    outbound_eth6_header.srcmac = &g_src_mac;
    outbound_eth6_header.dstmac = &g_dst_mac;
    outbound_eth6_header.ethertype = 0x086DD;
    g_v6_outbound_key.src_ip = calloc(1, 16);
    TEST_ASSERT_NOT_NULL(g_v6_outbound_key.src_ip);
    rc = inet_pton(AF_INET6, g_v6_tuple.src_ip, g_v6_outbound_key.src_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v6_outbound_key.dst_ip = calloc(1, 16);
    TEST_ASSERT_NOT_NULL(g_v6_outbound_key.dst_ip);
    rc = inet_pton(AF_INET6, g_v6_tuple.dst_ip, g_v6_outbound_key.dst_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v6_outbound_key.ip_version = 6;
    g_v6_outbound_key.ipprotocol = g_v6_tuple.transport;
    g_v6_outbound_key.sport = htons(g_v6_tuple.src_port);
    g_v6_outbound_key.dport = htons(g_v6_tuple.dst_port);

    g_v6_outbound_acc.key = &g_v6_outbound_key;
    g_v6_outbound_acc.direction = g_v6_tuple.direction;
    g_v6_outbound_acc.originator = g_v6_tuple.originator;

    /* Create an ipv6 inbound accumulator */
    memset(&g_v6_inbound_acc, 0, sizeof(g_v6_inbound_acc));
    memset(&g_v6_inbound_key, 0, sizeof(g_v6_inbound_key));
    memset(&inbound_eth6_header, 0, sizeof(inbound_eth6_header));

    inbound_eth6_header.srcmac = &g_dst_mac;
    inbound_eth6_header.dstmac = &g_src_mac;
    inbound_eth6_header.ethertype = 0x086DD;
    g_v6_inbound_key.src_ip = calloc(1, 16);
    TEST_ASSERT_NOT_NULL(g_v6_inbound_key.src_ip);
    rc = inet_pton(AF_INET6, g_v6_inbound_tuple.src_ip, g_v6_inbound_key.src_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v6_inbound_key.dst_ip = calloc(1, 16);
    TEST_ASSERT_NOT_NULL(g_v6_inbound_key.dst_ip);
    rc = inet_pton(AF_INET6, g_v6_inbound_tuple.dst_ip, g_v6_inbound_key.dst_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v6_inbound_key.ip_version = 6;
    g_v6_inbound_key.ipprotocol = g_v6_inbound_tuple.transport;
    g_v6_inbound_key.sport = htons(g_v6_inbound_tuple.src_port);
    g_v6_inbound_key.dport = htons(g_v6_inbound_tuple.dst_port);

    g_v6_inbound_acc.key = &g_v6_inbound_key;
    g_v6_inbound_acc.direction = g_v6_inbound_tuple.direction;
    g_v6_inbound_acc.originator = g_v6_inbound_tuple.originator;

    g_mgr = ipthreat_dpi_get_mgr();

    return;
}

/**
 * @brief called by the Unity framework after every single test
 */
void tearDown(void)
{
    /* free v4 accumulator */
    free(g_v4_outbound_key.src_ip);
    free(g_v4_outbound_key.dst_ip);
    memset(&g_v4_outbound_key, 0, sizeof(g_v4_outbound_key));
    memset(&g_v4_outbound_acc, 0, sizeof(g_v4_outbound_acc));

    free(g_v4_inbound_key.src_ip);
    free(g_v4_inbound_key.dst_ip);
    memset(&g_v4_inbound_key, 0, sizeof(g_v4_inbound_key));
    memset(&g_v4_inbound_acc, 0, sizeof(g_v4_inbound_acc));

    /* free v6 accumulator */
    free(g_v6_outbound_key.src_ip);
    free(g_v6_outbound_key.dst_ip);
    memset(&g_v6_outbound_key, 0, sizeof(g_v6_outbound_key));
    memset(&g_v6_outbound_acc, 0, sizeof(g_v6_outbound_acc));

    free(g_v6_inbound_key.src_ip);
    free(g_v6_inbound_key.dst_ip);
    memset(&g_v6_inbound_key, 0, sizeof(g_v6_inbound_key));
    memset(&g_v6_inbound_acc, 0, sizeof(g_v6_inbound_acc));

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
    TEST_ASSERT_NOT_NULL(g_mgr);
}

/**
 * @brief validate add and delete session
 */
void test_add_del_session(void)
{
    struct fsm_session *session;
    struct ipthreat_dpi_session *ds_session;
    struct ipthreat_dpi_cache *mgr;
    ds_tree_t *sessions;
    int ret;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    mgr = ipthreat_dpi_get_mgr();
    sessions = &mgr->ipt_sessions;

    session = &g_sessions[0];
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    /* add ipthreat session */
    ret = ipthreat_dpi_plugin_init(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NOT_NULL(ds_session);

    session = &g_sessions[1];
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    /* add ipthreat session */
    ret = ipthreat_dpi_plugin_init(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NOT_NULL(ds_session);

    /* free ipthreat session */
    ipthreat_dpi_plugin_exit(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    session = &g_sessions[2];
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    /* add ipthreat session */
    ret = ipthreat_dpi_plugin_init(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NOT_NULL(ds_session);

    /* free ipthreat session */
    ipthreat_dpi_plugin_exit(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    /* free ipthreat session */
    session = &g_sessions[0];
    ipthreat_dpi_plugin_exit(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

/**
 * @brief validate traffic on unavailablity of policy
 */
void test_ipthreat_no_policy(void)
{
    struct ipthreat_dpi_session *ds_session;
    struct net_header_parser *net_parser;
    struct ipthreat_dpi_parser  *parser;
    struct ipthreat_dpi_cache *mgr;
    struct fsm_session *session;
    struct udphdr udphdr;
    ds_tree_t *sessions;
    struct iphdr iphdr;
    int ret;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    mgr = ipthreat_dpi_get_mgr();
    sessions = &mgr->ipt_sessions;

    /* add ipthreat session */
    session = &g_sessions[0];
    ret = ipthreat_dpi_plugin_init(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NOT_NULL(ds_session);

    /* populate net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = outbound_eth_header;
    net_parser->ip_version = 4;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&iphdr.saddr, g_v4_outbound_key.src_ip, 4);
    memcpy(&iphdr.daddr, g_v4_outbound_key.dst_ip, 4);
    udphdr.source = g_v4_outbound_key.sport;
    udphdr.dest = g_v4_outbound_key.dport;
    net_parser->eth_pld.ip.iphdr = &iphdr;
    net_parser->ip_pld.udphdr = &udphdr;
    net_parser->acc = &g_v4_outbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* free ipthreat session */
    ipthreat_dpi_plugin_exit(session);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

/**
 * @brief validate traffic on unavailablity of policy_table in other_config
 */
void test_ipthreat_no_policy_table(void)
{
    struct ipthreat_dpi_session *ds_session;
    struct net_header_parser *net_parser;
    struct ipthreat_dpi_parser  *parser;
    struct schema_FSM_Policy *spolicy;
    struct ipthreat_dpi_cache *mgr;
    struct fsm_session *session;
    struct fsm_policy *fpolicy;
    struct udphdr udphdr;
    ds_tree_t *sessions;
    struct iphdr iphdr;
    int ret;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    mgr = ipthreat_dpi_get_mgr();
    sessions = &mgr->ipt_sessions;

    /* add ipthreat session */
    session = &g_sessions[1];
    ret = ipthreat_dpi_plugin_init(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NOT_NULL(ds_session);

    /* add fsm policy */
    spolicy = &spolicies[1];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* populate net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = outbound_eth_header;
    net_parser->ip_version = 4;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&iphdr.saddr, g_v4_outbound_key.src_ip, 4);
    memcpy(&iphdr.daddr, g_v4_outbound_key.dst_ip, 4);
    udphdr.source = g_v4_outbound_key.sport;
    udphdr.dest = g_v4_outbound_key.dport;
    net_parser->eth_pld.ip.iphdr = &iphdr;
    net_parser->ip_pld.udphdr = &udphdr;
    net_parser->acc = &g_v4_outbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* delete fsm policy */
    fsm_delete_policy(spolicy);

    /* free ipthreat session */
    ipthreat_dpi_plugin_exit(session);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

/**
 * @brief validate block inbound traffic
 */
void test_ipthreat_inbound_block(void)
{
    struct ipthreat_dpi_session *ds_session;
    struct net_header_parser *net_parser;
    struct ipthreat_dpi_parser  *parser;
    struct schema_FSM_Policy *spolicy;
    struct ipthreat_dpi_cache *mgr;
    struct fsm_session *session;
    struct fsm_policy *fpolicy;
    struct ip6_hdr ip6hdr;
    struct udphdr udphdr;
    ds_tree_t *sessions;
    struct iphdr iphdr;
    int ret;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    mgr = ipthreat_dpi_get_mgr();
    sessions = &mgr->ipt_sessions;

    /* add ipthreat session */
    session = &g_sessions[0];
    ret = ipthreat_dpi_plugin_init(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NOT_NULL(ds_session);

    /* add fsm policy */
    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* populate net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = inbound_eth_header;
    net_parser->ip_version = 4;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&iphdr.saddr, g_v4_inbound_key.src_ip, 4);
    memcpy(&iphdr.daddr, g_v4_inbound_key.dst_ip, 4);
    udphdr.source = g_v4_inbound_key.sport;
    udphdr.dest = g_v4_inbound_key.dport;
    net_parser->eth_pld.ip.iphdr = &iphdr;
    net_parser->ip_pld.udphdr = &udphdr;
    net_parser->acc = &g_v4_inbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* populate v6 net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = inbound_eth6_header;
    net_parser->ip_version = 6;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&ip6hdr.ip6_src, g_v6_inbound_key.src_ip, 16);
    memcpy(&ip6hdr.ip6_dst, g_v6_inbound_key.dst_ip, 16);
    udphdr.source = g_v6_inbound_key.sport;
    udphdr.dest = g_v6_inbound_key.dport;
    net_parser->eth_pld.ip.ipv6hdr = &ip6hdr;
    net_parser->ip_pld.udphdr = &udphdr;
    net_parser->acc = &g_v6_inbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* delete fsm policy */
    fsm_delete_policy(spolicy);

    /* free ipthreat session */
    ipthreat_dpi_plugin_exit(session);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

/**
 * @brief validate block outbound traffic
 */
void test_ipthreat_outbound_block(void)
{
    struct ipthreat_dpi_session *ds_session;
    struct net_header_parser *net_parser;
    struct ipthreat_dpi_parser  *parser;
    struct schema_FSM_Policy *spolicy;
    struct ipthreat_dpi_cache *mgr;
    struct fsm_session *session;
    struct fsm_policy *fpolicy;
    struct ip6_hdr ip6hdr;
    struct udphdr udphdr;
    ds_tree_t *sessions;
    struct iphdr iphdr;
    int ret;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    mgr = ipthreat_dpi_get_mgr();
    sessions = &mgr->ipt_sessions;

    /* add ipthreat session */
    session = &g_sessions[0];
    ret = ipthreat_dpi_plugin_init(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NOT_NULL(ds_session);

    /* add fsm policy */
    spolicy = &spolicies[1];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* populate v4 net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = outbound_eth_header;
    net_parser->ip_version = 4;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&iphdr.saddr, g_v4_outbound_key.src_ip, 4);
    memcpy(&iphdr.daddr, g_v4_outbound_key.dst_ip, 4);
    udphdr.source = g_v4_outbound_key.sport;
    udphdr.dest = g_v4_outbound_key.dport;
    net_parser->eth_pld.ip.iphdr = &iphdr;
    net_parser->ip_pld.udphdr = &udphdr;
    net_parser->acc = &g_v4_outbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* populate v6 net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = outbound_eth6_header;
    net_parser->ip_version = 6;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&ip6hdr.ip6_src, g_v6_outbound_key.src_ip, 16);
    memcpy(&ip6hdr.ip6_dst, g_v6_outbound_key.dst_ip, 16);
    udphdr.source = g_v6_outbound_key.sport;
    udphdr.dest = g_v6_outbound_key.dport;
    net_parser->eth_pld.ip.ipv6hdr = &ip6hdr;
    net_parser->ip_pld.udphdr = &udphdr;
    net_parser->acc = &g_v6_outbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* delete fsm policy */
    fsm_delete_policy(spolicy);

    /* free ipthreat session */
    ipthreat_dpi_plugin_exit(session);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

/**
 * @brief validate allow inbound traffic
 */
void test_ipthreat_inbound_allow(void)
{
    struct ipthreat_dpi_session *ds_session;
    struct net_header_parser *net_parser;
    struct ipthreat_dpi_parser  *parser;
    struct schema_FSM_Policy *spolicy;
    struct ipthreat_dpi_cache *mgr;
    struct fsm_session *session;
    struct fsm_policy *fpolicy;
    struct ip6_hdr ip6hdr;
    struct udphdr udphdr;
    ds_tree_t *sessions;
    struct iphdr iphdr;
    int ret;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    mgr = ipthreat_dpi_get_mgr();
    sessions = &mgr->ipt_sessions;

    /* add ipthreat session */
    session = &g_sessions[0];
    ret = ipthreat_dpi_plugin_init(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NOT_NULL(ds_session);

    /* add fsm policy */
    spolicy = &spolicies[2];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* populate net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = inbound_eth_header;
    net_parser->ip_version = 4;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&iphdr.saddr, g_v4_inbound_key.src_ip, 4);
    memcpy(&iphdr.daddr, g_v4_inbound_key.dst_ip, 4);
    udphdr.source = g_v4_inbound_key.sport;
    udphdr.dest = g_v4_inbound_key.dport;
    net_parser->eth_pld.ip.iphdr = &iphdr;
    net_parser->ip_pld.udphdr = &udphdr;
    net_parser->acc = &g_v4_inbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* populate v6 net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = inbound_eth6_header;
    net_parser->ip_version = 6;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&ip6hdr.ip6_src, g_v6_inbound_key.src_ip, 16);
    memcpy(&ip6hdr.ip6_dst, g_v6_inbound_key.dst_ip, 16);
    udphdr.source = g_v6_inbound_key.sport;
    udphdr.dest = g_v6_inbound_key.dport;
    net_parser->eth_pld.ip.ipv6hdr = &ip6hdr;
    net_parser->ip_pld.udphdr = &udphdr;
    net_parser->acc = &g_v6_inbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* delete fsm policy */
    fsm_delete_policy(spolicy);

    /* free ipthreat session */
    ipthreat_dpi_plugin_exit(session);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

/**
 * @brief validate allow outbound traffic
 */
void test_ipthreat_outbound_allow(void)
{
    struct ipthreat_dpi_session *ds_session;
    struct net_header_parser *net_parser;
    struct ipthreat_dpi_parser  *parser;
    struct schema_FSM_Policy *spolicy;
    struct ipthreat_dpi_cache *mgr;
    struct fsm_session *session;
    struct fsm_policy *fpolicy;
    struct ip6_hdr ip6hdr;
    struct udphdr udphdr;
    ds_tree_t *sessions;
    struct iphdr iphdr;
    int ret;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    mgr = ipthreat_dpi_get_mgr();
    sessions = &mgr->ipt_sessions;

    /* add ipthreat session */
    session = &g_sessions[0];
    ret = ipthreat_dpi_plugin_init(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NOT_NULL(ds_session);

    /* add fsm policy */
    spolicy = &spolicies[3];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* populate net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = outbound_eth_header;
    net_parser->ip_version = 4;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&iphdr.saddr, g_v4_outbound_key.src_ip, 4);
    memcpy(&iphdr.daddr, g_v4_outbound_key.dst_ip, 4);
    udphdr.source = g_v4_outbound_key.sport;
    udphdr.dest = g_v4_outbound_key.dport;
    net_parser->eth_pld.ip.iphdr = &iphdr;
    net_parser->ip_pld.udphdr = &udphdr;
    net_parser->acc = &g_v4_outbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* populate v6 net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = outbound_eth6_header;
    net_parser->ip_version = 6;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&ip6hdr.ip6_src, g_v6_outbound_key.src_ip, 16);
    memcpy(&ip6hdr.ip6_dst, g_v6_outbound_key.dst_ip, 16);
    udphdr.source = g_v6_outbound_key.sport;
    udphdr.dest = g_v6_outbound_key.dport;
    net_parser->eth_pld.ip.ipv6hdr = &ip6hdr;
    net_parser->ip_pld.udphdr = &udphdr;
    net_parser->acc = &g_v6_outbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* delete fsm policy */
    fsm_delete_policy(spolicy);

    /* free ipthreat session */
    ipthreat_dpi_plugin_exit(session);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

/**
 * @brief validate lan2lan traffic
 */
void test_ipthreat_lan2lan_traffic(void)
{
    struct ipthreat_dpi_session *ds_session;
    struct net_header_parser *net_parser;
    struct ipthreat_dpi_parser  *parser;
    struct schema_FSM_Policy *spolicy;
    struct ipthreat_dpi_cache *mgr;
    struct fsm_session *session;
    struct fsm_policy *fpolicy;
    struct ip6_hdr ip6hdr;
    struct udphdr udphdr;
    ds_tree_t *sessions;
    struct iphdr iphdr;
    int ret;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    mgr = ipthreat_dpi_get_mgr();
    sessions = &mgr->ipt_sessions;

    /* add ipthreat session */
    session = &g_sessions[0];
    ret = ipthreat_dpi_plugin_init(session);
    TEST_ASSERT_TRUE(ret == 0);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NOT_NULL(ds_session);

    /* add fsm policy */
    spolicy = &spolicies[3];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* populate net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = outbound_eth_header;
    net_parser->ip_version = 4;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&iphdr.saddr, g_v4_outbound_key.src_ip, 4);
    memcpy(&iphdr.daddr, g_v4_outbound_key.dst_ip, 4);
    udphdr.source = g_v4_outbound_key.sport;
    udphdr.dest = g_v4_outbound_key.dport;
    net_parser->eth_pld.ip.iphdr = &iphdr;
    net_parser->ip_pld.udphdr = &udphdr;
    g_v4_outbound_acc.direction = NET_MD_ACC_LAN2LAN_DIR;
    net_parser->acc = &g_v4_outbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* populate v6 net_parser */
    net_parser = calloc(1, sizeof(struct net_header_parser));
    net_parser->eth_header = outbound_eth6_header;
    net_parser->ip_version = 6;
    net_parser->ip_protocol = IPPROTO_UDP;
    memcpy(&ip6hdr.ip6_src, g_v6_outbound_key.src_ip, 16);
    memcpy(&ip6hdr.ip6_dst, g_v6_outbound_key.dst_ip, 16);
    udphdr.source = g_v6_outbound_key.sport;
    udphdr.dest = g_v6_outbound_key.dport;
    net_parser->eth_pld.ip.ipv6hdr = &ip6hdr;
    net_parser->ip_pld.udphdr = &udphdr;
    g_v6_outbound_acc.direction = NET_MD_ACC_LAN2LAN_DIR;
    net_parser->acc = &g_v6_outbound_acc;
    parser = &ds_session->parser;
    parser->net_parser = net_parser;

    ipthreat_dpi_process_message(ds_session);
    free(net_parser);

    /* delete fsm policy */
    fsm_delete_policy(spolicy);

    /* free ipthreat session */
    ipthreat_dpi_plugin_exit(session);
    ds_session = ds_tree_find(sessions, session);
    TEST_ASSERT_NULL(ds_session);

    LOGI("\n******************** %s: completed ****************\n", __func__);
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
    RUN_TEST(test_add_del_session);
    RUN_TEST(test_ipthreat_no_policy);
    RUN_TEST(test_ipthreat_no_policy_table);
    RUN_TEST(test_ipthreat_inbound_block);
    RUN_TEST(test_ipthreat_outbound_block);
    RUN_TEST(test_ipthreat_inbound_allow);
    RUN_TEST(test_ipthreat_outbound_allow);
    RUN_TEST(test_ipthreat_lan2lan_traffic);

    global_test_exit();

    return UNITY_END();
}
