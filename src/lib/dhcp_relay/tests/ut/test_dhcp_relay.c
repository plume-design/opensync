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
#include <net/if.h>

#include "log.h"
#include "target.h"
#include "unity.h"
#include "json_util.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "os.h"
#include "os_types.h"
#include "dhcp_relay.h"

#include "pcap.c"
#include "dhcpv6.c"

#define OTHER_CONFIG_NELEMS 4
#define OTHER_CONFIG_NELEM_SIZE 128

char *test_name = "test_dhcp_relay";
char *g_dhcp_relay_conf = "/tmp/dhcp_relay.conf";

struct dhcp_relay_mgr       *g_mgr;
struct ev_loop              *g_loop;
struct net_header_parser    *net_parser;

#ifndef ARCH_X86
os_macaddr_t                src_eth_addr;
#endif

void dhcp_test_handler(struct fsm_session *session, struct net_header_parser *net_parser);
char *util_get_other_config_val(struct fsm_session *session, char *key);

/*************************************************************************************************/

char g_other_configs[][2][OTHER_CONFIG_NELEMS][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "mqtt_v",
            "tx_intf",
        },
        {
            "dev-test/dhcp_session_0/4C70D0007B",
            CONFIG_TARGET_LAN_BRIDGE_NAME".tx",
        },
    },
};

struct fsm_session_conf g_confs[] =
{
    /* entry 0*/
    {
        .handler = "dhcp_test_session_0",
        .if_name = "foo"
    },
};

struct fsm_session g_sessions[] =
{
    {
        .type = FSM_PARSER,
        .conf = &g_confs[0],
        .node_id = "4C77701479",
        .location_id = "6018b9b632a8be0c9c4aabc4",
    }

};

union fsm_plugin_ops g_plugin_ops =
{
    .parser_ops =
    {
        .get_service = NULL,
        .handler = dhcp_test_handler,
    },
};


struct fsm_session_ops g_ops =
{
    .get_config = util_get_other_config_val,
};


/*************************************************************************************************/

void
dhcp_test_handler(struct fsm_session *session, struct net_header_parser *net_parser)
{
    LOGI("%s: here", __func__);
    net_header_logi(net_parser);
}

char *
util_get_other_config_val(struct fsm_session *session, char *key)
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
 * @brief Converts a bytes array in a hex dump file wireshark can import.
 *
 * Dumps the array in a file that can then be imported by wireshark.
 * The file can also be translated to a pcap file using the text2pcap command.
 * Useful to visualize the packet content.
 */
void create_hex_dump(const char *fname, const uint8_t *buf, size_t len)
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

/**
 * @brief Convenient wrapper
 *
 * Dumps the packet content in tmp/<tests_name>_<pkt name>.txtpcap
 * for wireshark consumption and sets g_parser data fields.
 * @params pkt the C structure containing an exported packet capture
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

/*************************************************************************************************/

void test_dhcpv4_DISCOVER_pkt(void)
{
    struct fsm_session          *session;
    struct dhcp_session         *d_session;
    struct dhcp_parser          *parser;
#ifndef ARCH_X86
    struct eth_header           *eth_hdr;
#endif
    size_t                      len = 0;
    bool                        ret;

    uint8_t                     pkt_cpy[1024];

    session = &g_sessions[0];
    d_session = dhcp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(d_session);

    parser = &d_session->parser;
    parser->net_parser = net_parser;

    parser->relay_options_len = 0;

    // DHCP_DISCOVER
    memset(pkt_cpy, 0, sizeof(pkt_cpy));
    memcpy(pkt_cpy, pkt120, sizeof(pkt120));

    PREPARE_UT(pkt_cpy, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

#ifndef ARCH_X86
    eth_hdr = net_header_get_eth(net_parser);
#endif

    len = dhcp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt120), net_parser->packet_len);

    ret = dhcp_relay_check_option82(d_session);
    TEST_ASSERT_FALSE(ret);

    /* Insert option82 */
    dhcp_relay_process_dhcpv4_message(d_session);
    ret = dhcp_relay_check_option82(d_session);
    TEST_ASSERT_TRUE(ret);

#ifndef ARCH_X86
    dhcp_prepare_forward(d_session, (uint8_t *)net_parser->start);

    len = memcmp(d_session->raw_dst.sll_addr, eth_hdr->dstmac, sizeof(os_macaddr_t));
    TEST_ASSERT_TRUE(len == 0);

    len = memcmp(eth_hdr->srcmac, src_eth_addr.addr, sizeof(src_eth_addr.addr));
    TEST_ASSERT_TRUE(len == 0);
#endif

    return;
}

void test_dhcpv4_REQUEST_pkt(void)
{
    struct fsm_session          *session;
    struct dhcp_session         *d_session;
    struct dhcp_parser          *parser;
#ifndef ARCH_X86
    struct eth_header           *eth_hdr;
#endif
    size_t                      len = 0;
    bool                        ret;

    uint8_t                     pkt_cpy[1024];

    session = &g_sessions[0];
    d_session = dhcp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(d_session);

    parser = &d_session->parser;
    parser->net_parser = net_parser;

    parser->relay_options_len = 0;

    // DHCP REQUEST
    memset(pkt_cpy, 0, sizeof(pkt_cpy));
    memcpy(pkt_cpy, pkt139, sizeof(pkt139));

    PREPARE_UT(pkt_cpy, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

#ifndef ARCH_X86
    eth_hdr = net_header_get_eth(net_parser);
#endif

    len = dhcp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt139), net_parser->packet_len);

    ret = dhcp_relay_check_option82(d_session);
    TEST_ASSERT_FALSE(ret);

    /* Insert option82 */
    dhcp_relay_process_dhcpv4_message(d_session);
    ret = dhcp_relay_check_option82(d_session);
    TEST_ASSERT_TRUE(ret);

#ifndef ARCH_X86
    dhcp_prepare_forward(d_session, (uint8_t *)net_parser->start);

    len = memcmp(d_session->raw_dst.sll_addr, eth_hdr->dstmac, sizeof(os_macaddr_t));
    TEST_ASSERT_TRUE(len == 0);

    len = memcmp(eth_hdr->srcmac, src_eth_addr.addr, sizeof(src_eth_addr.addr));
    TEST_ASSERT_TRUE(len == 0);
#endif

    return;
}

void test_dhcpv6_SOLICIT_pkt(void)
{
    struct fsm_session      *session;
    struct dhcp_session     *d_session;
    struct dhcp_parser      *parser;
#ifndef ARCH_X86
    struct eth_header       *eth_hdr;
#endif
    size_t                  len = 0;
    bool                    ret;

    uint8_t                 pkt_cpy[1024];

    session = &g_sessions[0];
    d_session = dhcp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(d_session);

    parser = &d_session->parser;
    parser->net_parser = net_parser;

    parser->relay_options_len = 0;

    // DHCPv6 SOLICIT packet
    memset(pkt_cpy, 0, sizeof(pkt_cpy));
    memcpy(pkt_cpy, pkt46, sizeof(pkt46));

    PREPARE_UT(pkt_cpy, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

#ifndef ARCH_X86
    eth_hdr = net_header_get_eth(net_parser);
#endif

    len = dhcp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt46), net_parser->packet_len);

    /* Check DHCPv6 options */
    ret = dhcp_relay_check_dhcpv6_option(d_session, DHCPv6_INTERFACE_ID);
    TEST_ASSERT_FALSE(ret);

    ret = dhcp_relay_check_dhcpv6_option(d_session, DHCPv6_REMOTE_ID);
    TEST_ASSERT_FALSE(ret);

    /* Insert options */
    dhcp_relay_process_dhcpv6_message(d_session);

    /* Check options again */
    ret = dhcp_relay_check_dhcpv6_option(d_session, DHCPv6_REMOTE_ID);
    TEST_ASSERT_TRUE(ret);

    ret = dhcp_relay_check_dhcpv6_option(d_session, DHCPv6_INTERFACE_ID);
    TEST_ASSERT_TRUE(ret);

#ifndef ARCH_X86
    dhcp_prepare_forward(d_session, (uint8_t *)net_parser->start);

    len = memcmp(d_session->raw_dst.sll_addr, eth_hdr->dstmac, sizeof(os_macaddr_t));
    TEST_ASSERT_TRUE(len == 0);

    len = memcmp(eth_hdr->srcmac, src_eth_addr.addr, sizeof(src_eth_addr.addr));
    TEST_ASSERT_TRUE(len == 0);
#endif

    return;
}

void test_dhcpv6_REQUEST_pkt(void)
{
    struct fsm_session      *session;
    struct dhcp_session     *d_session;
    struct dhcp_parser      *parser;
#ifndef ARCH_X86
    struct eth_header       *eth_hdr;
#endif
    size_t                  len = 0;
    bool                    ret;

    uint8_t                 pkt_cpy[1024];

    session = &g_sessions[0];
    d_session = dhcp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(d_session);

    parser = &d_session->parser;
    parser->net_parser = net_parser;

    parser->relay_options_len = 0;

    // DHCPv6 REQUEST packet
    memset(pkt_cpy, 0, sizeof(pkt_cpy));
    memcpy(pkt_cpy, pkt51, sizeof(pkt51));

    PREPARE_UT(pkt_cpy, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

#ifndef ARCH_X86
    eth_hdr = net_header_get_eth(net_parser);
#endif

    len = dhcp_parse_message(parser);
    TEST_ASSERT_TRUE(len != 0);
    TEST_ASSERT_EQUAL_UINT(sizeof(pkt51), net_parser->packet_len);

    /* Check DHCPv6 options */
    ret = dhcp_relay_check_dhcpv6_option(d_session, DHCPv6_INTERFACE_ID);
    TEST_ASSERT_FALSE(ret);

    ret = dhcp_relay_check_dhcpv6_option(d_session, DHCPv6_REMOTE_ID);
    TEST_ASSERT_FALSE(ret);

    /* Insert options */
    dhcp_relay_process_dhcpv6_message(d_session);

    /* Check options again */
    ret = dhcp_relay_check_dhcpv6_option(d_session, DHCPv6_REMOTE_ID);
    TEST_ASSERT_TRUE(ret);

    ret = dhcp_relay_check_dhcpv6_option(d_session, DHCPv6_INTERFACE_ID);
    TEST_ASSERT_TRUE(ret);

#ifndef ARCH_X86
    dhcp_prepare_forward(d_session, (uint8_t *)net_parser->start);

    len = memcmp(d_session->raw_dst.sll_addr, eth_hdr->dstmac, sizeof(os_macaddr_t));
    TEST_ASSERT_TRUE(len == 0);

    len = memcmp(eth_hdr->srcmac, src_eth_addr.addr, sizeof(src_eth_addr.addr));
    TEST_ASSERT_TRUE(len == 0);
#endif

    return;
}

/*************************************************************************************************/

void global_test_init(void)
{
#ifndef ARCH_X86
    int                  sock_fd = 0, rc = 0;
    struct ifreq         ifreq_c;
#endif

    struct str_pair     *pair;
    struct fsm_session  *session = &g_sessions[0];
    struct dhcp_session *d_session;

    g_loop = EV_DEFAULT;
    session->conf = &g_confs[0];
    session->ops  = g_ops;
    session->name = g_confs[0].handler;
    session->conf->other_config = schema2tree(OTHER_CONFIG_NELEM_SIZE,
                                              OTHER_CONFIG_NELEM_SIZE,
                                              OTHER_CONFIG_NELEMS,
                                              g_other_configs[0][0],
                                              g_other_configs[0][1]);
    session->loop = g_loop;
    session->p_ops = &g_plugin_ops;

    pair = ds_tree_find(session->conf->other_config, "mqtt_v");
    session->topic = pair->value;

    pair = ds_tree_find(session->conf->other_config, "tx_intf");
    STRSCPY(session->tx_intf, pair->value);

    net_parser = CALLOC(1, sizeof(struct net_header_parser));
    if (!net_parser)
    {
        LOGE("%s: net_parser allocation failed", __func__);
        TEST_FAIL();
    }

    dhcp_relay_plugin_init(session);

    d_session = dhcp_lookup_session(session);
    TEST_ASSERT_NOT_NULL(d_session);

#ifndef ARCH_X86
    g_mgr = dhcp_get_mgr();

    rc = g_mgr->set_forward_context(session);
    if (rc != 0)
    {
        LOGE("%s: packet forwarding context initialized failed", __func__);
        FREE(net_parser);
        TEST_FAIL();
    }
    d_session->forward_ctxt_initialized = true;

    memset(&ifreq_c, 0, sizeof(ifreq_c));
    STRSCPY(ifreq_c.ifr_name, session->tx_intf);

    sock_fd = socket(AF_PACKET, SOCK_RAW, 0);
    if (sock_fd < 0)
    {
        LOGE("%s: socket failed()", __func__);
        FREE(net_parser);
        TEST_FAIL();
    }

    if ((ioctl(sock_fd, SIOCGIFHWADDR, &ifreq_c)) < 0)
    {
        LOGE("%s: error in SIOCGIFHWADDR ioctl reading", __func__);
        FREE(net_parser);
        TEST_FAIL();
    }

    memcpy(src_eth_addr.addr, ifreq_c.ifr_hwaddr.sa_data, 6);
#endif

}

void global_test_exit(void)
{
    struct fsm_session *session = &g_sessions[0];

    free_str_tree(session->conf->other_config);
    if (net_parser)     FREE(net_parser);

    dhcp_relay_plugin_exit(session);
    g_mgr = NULL;
}

void setUp(void)
{
    return;
}

void tearDown(void)
{
    return;
}

int main(int argc, char *argv[])
{
    bool ret;

    (void)argc;
    (void)argv;

    target_log_open(test_name, LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    /*
     * This is a requirement: Do NOT proceed if the file is missing.
     * File presence will not be tested any further.
     */
    ret = access(g_dhcp_relay_conf, F_OK);
    if (ret != 0)
    {
        LOGW("In %s: test requires %s", test_name, g_dhcp_relay_conf);
        Unity.TestFailed[Unity.TestFailures] = strdup(__func__);
        Unity.TestFailures++;
        return UNITY_END();
    }


    global_test_init();

    RUN_TEST(test_dhcpv4_DISCOVER_pkt);
    RUN_TEST(test_dhcpv4_REQUEST_pkt);

    RUN_TEST(test_dhcpv6_SOLICIT_pkt);
    RUN_TEST(test_dhcpv6_REQUEST_pkt);

    global_test_exit();

    return UNITY_END();
}
