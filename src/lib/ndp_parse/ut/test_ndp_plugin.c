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

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "const.h"
#include "fsm.h"
#include "json_util.h"
#include "log.h"
#include "memutil.h"
#include "ndp_parse.h"
#include "neigh_table.h"
#include "net_header_parse.h"
#include "os_types.h"
#include "ovsdb_utils.h"
#include "qm_conn.h"
#include "unit_test_utils.h"
#include "unity.h"

#include "pcap.c"

const char *ut_name = "ndp_plugin_tests";

#define OTHER_CONFIG_NELEMS 4
#define OTHER_CONFIG_NELEM_SIZE 128

union fsm_plugin_ops p_ops;
struct ndp_cache *g_mgr;
char *g_location_id = "foo";
char *g_node_id = "bar";

char g_other_configs[][2][OTHER_CONFIG_NELEMS][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "mqtt_v",
        },
        {
            "dev-test/ndp_plugin_0/4C70D0007B",
        },
    },
    {
        {
            "mqtt_v",
        },
        {
            "dev-test/ndp_1/4C70D0007B",
        },
    },
};


/**
 * @brief a set of sessions as delivered by the ovsdb API
 */
struct fsm_session_conf g_confs[2] =
{
    {
        .handler = "ndp_test_0",
        .if_name = "ndp_ut_intf_0",
    },
    {
        .handler = "ndp_test_1",
        .if_name = "ndp_ut_intf_1",
    }
};


struct fsm_session g_sessions[2] =
{
    {
        .type = FSM_PARSER,
        .conf = &g_confs[0],
    },
    {
        .type = FSM_PARSER,
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

char *
get_other_config_val(struct fsm_session *session, char *key)
{
    return NULL;
}

struct fsm_session_ops g_ops =
{
    .send_report = send_report,
    .get_config = get_other_config_val,
};


void
global_test_init(void)
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

void
global_test_exit(void)
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
ndp_plugin_setUp(void)
{
#ifdef ARCH_X86
    struct neigh_table_mgr *neigh_mgr;
#endif
    size_t n_sessions;
    size_t i;

    g_mgr = NULL;

    /* Reset sessions, register them to the plugin */
    n_sessions = ARRAY_SIZE(g_sessions);
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        ndp_plugin_init(session);
    }
    g_mgr = ndp_get_mgr();

    neigh_table_init();

#ifdef ARCH_X86
    neigh_mgr = neigh_table_get_mgr();
    neigh_mgr->update_ovsdb_tables = NULL;
#endif

    ut_prepare_pcap(Unity.CurrentTestName);
}

/**
 * @brief called by the Unity framework after every single test
 */
void
ndp_plugin_tearDown(void)
{
    size_t n_sessions;
    size_t i;

    /* Reset sessions, unregister them */
    n_sessions = ARRAY_SIZE(g_sessions);
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        ndp_plugin_exit(session);
    }
    g_mgr = NULL;
    neigh_table_cleanup();

    ut_cleanup_pcap();
}


/**
 * @brief log ip mac mapping
 */
void
log_ip_mac_mapping(struct ndp_parser *parser)
{
    char ipstr[INET6_ADDRSTRLEN];
    os_macaddr_t null_mac = {{ 0 }};
    os_macaddr_t *mac;

    mac = parser->sender.mac;
    if (mac == NULL)
    {
        LOGI("%s: sender: no IP mac mapping available", __func__);
        mac = &null_mac;
    }

    memset(ipstr, 0, sizeof(ipstr));
    getnameinfo((struct sockaddr *)(parser->sender.ipaddr),
                sizeof(struct sockaddr_storage), ipstr, sizeof(ipstr),
                0, 0, NI_NUMERICHOST);

    LOGI("%s: sender ip: %s, sender mac: "PRI_os_macaddr_lower_t,
         __func__, ipstr, FMT_os_macaddr_pt(mac));

    mac = parser->target.mac;
    if (mac == NULL)
    {
        LOGI("%s: target: no IP mac mapping available", __func__);
        mac = &null_mac;
    }

    memset(ipstr, 0, sizeof(ipstr));
    getnameinfo((struct sockaddr *)(parser->target.ipaddr),
                sizeof(struct sockaddr_storage), ipstr, sizeof(ipstr),
                0, 0, NI_NUMERICHOST);

    LOGI("%s: target ip: %s, target mac: "PRI_os_macaddr_lower_t,
         __func__, ipstr, FMT_os_macaddr_pt(mac));

    mac = parser->entry.mac;
    if (mac == NULL)
    {
        LOGI("%s: no IP mac mapping available", __func__);
        mac = &null_mac;
    }

    memset(ipstr, 0, sizeof(ipstr));
    getnameinfo((struct sockaddr *)(parser->entry.ipaddr),
                sizeof(struct sockaddr_storage), ipstr, sizeof(ipstr),
                0, 0, NI_NUMERICHOST);

    LOGI("%s: ip: %s, mac: "PRI_os_macaddr_lower_t,
         __func__, ipstr, FMT_os_macaddr_pt(mac));

}


/**
 * @brief validate that no session provided is handled correctly
 */
void
test_no_session(void)
{
    int ret;

    ret = ndp_plugin_init(NULL);
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
 * @brief test arp request parsing
 *
 */
void test_arp_req(void)
{
    struct net_header_parser *net_parser;
    struct ndp_session *n_session;
    struct fsm_session *session;
    struct ndp_parser *parser;
    size_t len;
    bool ret;

    /* Select the first active session */
    session = &g_sessions[0];
    n_session = ndp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(n_session);

    parser = &n_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    parser->net_parser = net_parser;
    UT_CREATE_PCAP_PAYLOAD(pkt134, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    len = ndp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_TRUE(parser->type == NEIGH_ARP);

    ret = arp_parse_is_gratuitous(&parser->arp);
    TEST_ASSERT_FALSE(ret);

    arp_process_message(n_session);
    log_ip_mac_mapping(parser);

    FREE(net_parser);
}


/**
 * @brief test arp reply parsing
 *
 */
void test_arp_reply(void)
{
    struct net_header_parser *net_parser;
    struct ndp_session *n_session;
    struct fsm_session *session;
    struct ndp_parser *parser;
    size_t len;
    bool ret;

    /* Select the first active session */
    session = &g_sessions[0];
    n_session = ndp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(n_session);

    parser = &n_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    parser->net_parser = net_parser;
    UT_CREATE_PCAP_PAYLOAD(pkt135, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    len = ndp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_TRUE(parser->type == NEIGH_ARP);

    ret = arp_parse_is_gratuitous(&parser->arp);
    TEST_ASSERT_FALSE(ret);

    arp_process_message(n_session);
    log_ip_mac_mapping(parser);

    FREE(net_parser);
}


/**
 * @brief test arp request parsing
 *
 */
void test_gratuitous_arp_reply(void)
{
    struct net_header_parser *net_parser;
    struct ndp_session *n_session;
    struct fsm_session *session;
    struct ndp_parser *parser;
    size_t len;
    bool ret;

    /* Select the first active session */
    session = &g_sessions[0];
    n_session = ndp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(n_session);

    parser = &n_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    parser->net_parser = net_parser;
    UT_CREATE_PCAP_PAYLOAD(pkt42, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    len = ndp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_TRUE(parser->type == NEIGH_ARP);

    ret = arp_parse_is_gratuitous(&parser->arp);
    TEST_ASSERT_TRUE(ret);

    arp_process_message(n_session);
    log_ip_mac_mapping(parser);

    FREE(net_parser);
}

/**
 * @brief test ip to mac mapping
 *
 */
void test_ip_mac_mapping(void)
{
    struct net_header_parser *net_parser;
    struct ndp_session *n_session;
    struct fsm_session *session;
    struct ndp_parser *parser;
    os_macaddr_t mac_out;
    uint32_t ip_addr;
    bool rc_lookup;
    size_t len;
    bool ret;
    int cmp;

    /* Select the first active session */
    session = &g_sessions[0];
    n_session = ndp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(n_session);

    parser = &n_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    parser->net_parser = net_parser;
    UT_CREATE_PCAP_PAYLOAD(pkt134, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    len = ndp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_TRUE(parser->type == NEIGH_ARP);

    ret = arp_parse_is_gratuitous(&parser->arp);
    TEST_ASSERT_FALSE(ret);

    arp_process_message(n_session);

    /* fill sockaddr */
    ip_addr = parser->arp.s_ip;
    rc_lookup = neigh_table_lookup_af(AF_INET, &ip_addr, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, parser->arp.s_eth, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(0, cmp);
    log_ip_mac_mapping(parser);

    FREE(net_parser);
}



/**
 * @brief validate neighbour solicitation message parsing
 */
void
test_solicitation_msg(void)
{
    struct net_header_parser *net_parser;
    struct ndp_session *n_session;
    struct fsm_session *session;
    struct ndp_parser *parser;
    struct ip6_hdr *hdr;
    uint16_t ethertype;
    size_t len;

    /* Select the first active session */
    session = &g_sessions[0];
    n_session = ndp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(n_session);

    parser = &n_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    parser->net_parser = net_parser;
    UT_CREATE_PCAP_PAYLOAD(pkt3, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    len = ndp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_TRUE(parser->type == NEIGH_ICMPv6);

    ethertype = net_header_get_ethertype(net_parser);
    TEST_ASSERT_EQUAL_INT(ETH_P_IPV6, ethertype);

    hdr = net_header_get_ipv6_hdr(net_parser);
    TEST_ASSERT_NOT_NULL(hdr);
    TEST_ASSERT_EQUAL_INT(IPPROTO_ICMPV6, net_parser->ip_protocol);

    log_ip_mac_mapping(parser);
    icmpv6_process_message(n_session);

    FREE(net_parser);
}


/**
 * @brief validate neighbour advertizment message parsing
 */
void
test_advertizment_msg(void)
{
    struct net_header_parser *net_parser;
    struct ndp_session *n_session;
    struct fsm_session *session;
    struct ndp_parser *parser;
    struct ip6_hdr *hdr;
    uint16_t ethertype;
    size_t len;

    /* Select the first active session */
    session = &g_sessions[0];
    n_session = ndp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(n_session);

    parser = &n_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    parser->net_parser = net_parser;
    UT_CREATE_PCAP_PAYLOAD(pkt4, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = ndp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_TRUE(parser->type == NEIGH_ICMPv6);

    ethertype = net_header_get_ethertype(net_parser);
    TEST_ASSERT_EQUAL_INT(ETH_P_IPV6, ethertype);

    hdr = net_header_get_ipv6_hdr(net_parser);
    TEST_ASSERT_NOT_NULL(hdr);
    TEST_ASSERT_EQUAL_INT(IPPROTO_ICMPV6, net_parser->ip_protocol);

    log_ip_mac_mapping(parser);
    icmpv6_process_message(n_session);

    FREE(net_parser);
}


/**
 * @brief validate neighbour advertizment message parsing
 *
 * This message contains an optional destination link address
 */
void
test_advertizment_msg_v2(void)
{
    struct net_header_parser *net_parser;
    struct ndp_session *n_session;
    struct fsm_session *session;
    struct ndp_parser *parser;
    struct ip6_hdr *hdr;
    uint16_t ethertype;
    size_t len;

    /* Select the first active session */
    session = &g_sessions[0];
    n_session = ndp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(n_session);

    parser = &n_session->parser;
    net_parser = CALLOC(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    parser->net_parser = net_parser;
    UT_CREATE_PCAP_PAYLOAD(pkt6578, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    len = ndp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_TRUE(parser->type == NEIGH_ICMPv6);

    ethertype = net_header_get_ethertype(net_parser);
    TEST_ASSERT_EQUAL_INT(ETH_P_IPV6, ethertype);

    hdr = net_header_get_ipv6_hdr(net_parser);
    TEST_ASSERT_NOT_NULL(hdr);
    TEST_ASSERT_EQUAL_INT(IPPROTO_ICMPV6, net_parser->ip_protocol);

    log_ip_mac_mapping(parser);
    icmpv6_process_message(n_session);

    FREE(net_parser);
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name, global_test_init, global_test_exit);
    ut_setUp_tearDown(ut_name, ndp_plugin_setUp, ndp_plugin_tearDown);

    RUN_TEST(test_no_session);
    RUN_TEST(test_load_unload_plugin);
    RUN_TEST(test_arp_req);
    RUN_TEST(test_arp_reply);
    RUN_TEST(test_gratuitous_arp_reply);
    RUN_TEST(test_ip_mac_mapping);
    RUN_TEST(test_solicitation_msg);
    RUN_TEST(test_advertizment_msg);
    RUN_TEST(test_advertizment_msg_v2);

    return ut_fini();
}
