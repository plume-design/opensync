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

#include <netinet/icmp6.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>

#include "neigh_table.h"
#include "arp_parse.h"
#include "json_util.h"
#include "log.h"
#include "qm_conn.h"
#include "target.h"
#include "unity.h"
#include "pcap.c"

const char *test_name = "arp_plugin_tests";

#define OTHER_CONFIG_NELEMS 4
#define OTHER_CONFIG_NELEM_SIZE 128

char g_other_configs[][2][OTHER_CONFIG_NELEMS][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "mqtt_v",
        },
        {
            "dev-test/arp_plugin_0/4C70D0007B",
        },
    },
    {
        {
            "mqtt_v",
        },
        {
            "dev-test/arp_1/4C70D0007B",
        },
    },
};


/**
 * @brief a set of sessions as delivered by the ovsdb API
 */
struct fsm_session_conf g_confs[2] =
{
    {
        .handler = "arp_test_0",
        .if_name = "arp_ut_intf_0",
    },
    {
        .handler = "arp_test_1",
        .if_name = "arp_ut_intf_1",
    }
};


union fsm_plugin_ops p_ops;


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


struct fsm_session_ops g_ops =
{
    .send_report = send_report,
};


struct arp_cache *g_mgr;

/**
 * @brief Converts a bytes array in a hex dump file wireshark can import.
 *
 * Dumps the array in a file that can then be imported by wireshark.
 * The file can also be translated to a pcap file using the text2pcap command.
 * Useful to visualize the packet content.
 * @param fname the file recipient of the hex dump
 * @param buf the buffer to dump
 * @param length the length of the buffer to dump
 */
void
create_hex_dump(const char *fname, const uint8_t *buf, size_t len)
{
    int line_number = 0;
    bool new_line = true;
    size_t i;
    FILE *f;

    f = fopen(fname, "w+");

    if (f == NULL) return;

    for (i = 0; i < len; i++)
    {
	 new_line = (i == 0 ? true : ((i % 8) == 0));
	 if (new_line)
	 {
	      if (line_number) fprintf(f, "\n");
	      fprintf(f, "%06x", line_number);
	      line_number += 8;
	 }
         fprintf(f, " %02x", buf[i]);
    }
    fprintf(f, "\n");
    fclose(f);

    return;
}


char *g_location_id = "foo";
char *g_node_id = "bar";

/**
 * @brief Convenient wrapper
 *
 * Dumps the packet content in /tmp/<tests_name>_<pkt name>.txtpcap
 * for wireshark consumption and sets the given parser's data fields.
 * @param pkt the C structure containing an exported packet capture
 * @param parser theparser structure to set
 */
#define PREPARE_UT(pkt, parser)                                 \
    {                                                           \
        char fname[128];                                        \
        size_t len = sizeof(pkt);                               \
                                                                \
        snprintf(fname, sizeof(fname), "/tmp/%s_%s.txtpcap",    \
                 test_name, #pkt);                              \
        create_hex_dump(fname, pkt, len);                       \
        parser->packet_len = len;                               \
        parser->data = (uint8_t *)pkt;                          \
    }


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
        session->node_id = g_location_id;
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
setUp(void)
{
#ifdef ARCH_X86
    struct neigh_table_mgr *neigh_mgr;
#endif
    size_t n_sessions, i;

    g_mgr = NULL;
    n_sessions = sizeof(g_sessions) / sizeof(struct fsm_session);

    /* Reset sessions, register them to the plugin */
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        arp_plugin_init(session);
    }
    g_mgr = arp_get_mgr();

    neigh_table_init();

#ifdef ARCH_X86
    neigh_mgr = neigh_table_get_mgr();
    neigh_mgr->update_ovsdb_tables = NULL;
#endif

    return;
}

/**
 * @brief called by the Unity framework after every single test
 */
void
tearDown(void)
{
    size_t n_sessions, i;

    n_sessions = sizeof(g_sessions) / sizeof(struct fsm_session);

    /* Reset sessions, unregister them */
    for (i = 0; i < n_sessions; i++)
    {
        struct fsm_session *session = &g_sessions[i];

        arp_plugin_exit(session);
    }
    g_mgr = NULL;
    neigh_table_cleanup();

    return;
}


/**
 * @brief log ip mac mapping
 */
void
log_ip_mac_mapping(struct arp_parser *parser)
{
    char ipstr[INET6_ADDRSTRLEN];
    os_macaddr_t null_mac = { 0 };
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

}


/**
 * @brief validate that no session provided is handled correctly
 */
void
test_no_session(void)
{
    int ret;

    ret = arp_plugin_init(NULL);
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
    struct arp_session *a_session;
    struct fsm_session *session;
    struct arp_parser *parser;
    size_t len;
    bool ret;

    /* Select the first active session */
    session = &g_sessions[0];
    a_session = arp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(a_session);

    parser = &a_session->parser;
    net_parser = calloc(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    parser->net_parser = net_parser;
    PREPARE_UT(pkt134, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    len = arp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);

    ret = arp_parse_is_gratuitous(&parser->arp);
    TEST_ASSERT_FALSE(ret);

    arp_process_message(a_session);
    log_ip_mac_mapping(parser);

    free(net_parser);
}


/**
 * @brief test arp request parsing
 *
 */
void test_arp_reply(void)
{
    struct net_header_parser *net_parser;
    struct arp_session *a_session;
    struct fsm_session *session;
    struct arp_parser *parser;
    size_t len;
    bool ret;

    /* Select the first active session */
    session = &g_sessions[0];
    a_session = arp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(a_session);

    parser = &a_session->parser;
    net_parser = calloc(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    parser->net_parser = net_parser;
    PREPARE_UT(pkt135, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    len = arp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);

    ret = arp_parse_is_gratuitous(&parser->arp);
    TEST_ASSERT_FALSE(ret);

    arp_process_message(a_session);
    log_ip_mac_mapping(parser);

    free(net_parser);
}


/**
 * @brief test arp request parsing
 *
 */
void test_gratuitous_arp_reply(void)
{
    struct net_header_parser *net_parser;
    struct arp_session *a_session;
    struct fsm_session *session;
    struct arp_parser *parser;
    size_t len;
    bool ret;

    /* Select the first active session */
    session = &g_sessions[0];
    a_session = arp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(a_session);

    parser = &a_session->parser;
    net_parser = calloc(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    parser->net_parser = net_parser;
    PREPARE_UT(pkt42, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    len = arp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);

    ret = arp_parse_is_gratuitous(&parser->arp);
    TEST_ASSERT_TRUE(ret);

    arp_process_message(a_session);
    log_ip_mac_mapping(parser);

    free(net_parser);
}

int
main(int argc, char *argv[])
{
    /* Set the logs to stdout */
    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    global_test_init();

    RUN_TEST(test_no_session);
    RUN_TEST(test_load_unload_plugin);
    RUN_TEST(test_arp_req);
    RUN_TEST(test_arp_reply);
    RUN_TEST(test_gratuitous_arp_reply);

    global_test_exit();

    return UNITY_END();
}
