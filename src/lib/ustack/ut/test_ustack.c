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

#include <arpa/inet.h>
#include <net/if_arp.h>
#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <string.h>
#include <sys/socket.h>

#include "log.h"
#include "net_header_parse.h"
#include "network_metadata_report.h"
#include "unit_test_utils.h"
#include "unity.h"

#include "pcap.c"

const char *ut_name = "ustack_tests";

struct net_header_parser g_parser;

#define NET_HDR_BUFF_SIZE 128 + (2 * INET6_ADDRSTRLEN + 128) + 256

static char log_buf[NET_HDR_BUFF_SIZE] = { 0 };

void ustack_setUp(void)
{
    memset(&g_parser, 0, sizeof(g_parser));
    ut_prepare_pcap(Unity.CurrentTestName);
}

void ustack_tearDown(void)
{
    ut_cleanup_pcap();
    memset(&g_parser, 0, sizeof(g_parser));
}

/**
 * @brief Parsing pcap.c's pkt16574.
 *
 * pkt16574 is a ICMpv6 packet over vlan4 with multiple ipv6 extension headers.
 * See pcap.json for details.
 */
void test_icmpv6_pcap_parse(void)
{
    struct net_header_parser *parser;
    struct eth_header *eth_header;
    struct ip6_hdr *hdr;
    struct icmp6_hdr *icmphdr;
    size_t len;

    UT_CREATE_PCAP_PAYLOAD(pkt16574, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* validate vlan id */
    eth_header = &parser->eth_header;
    TEST_ASSERT_EQUAL_UINT(4, eth_header->vlan_id);

    /* Validate hopping through ipv6 extension headers */
    hdr = net_header_get_ipv6_hdr(parser);
    TEST_ASSERT_NOT_NULL(hdr);
    TEST_ASSERT_EQUAL_INT(IPPROTO_ICMPV6, parser->ip_protocol);

    /* Validate the icmpv6 request */
    icmphdr = parser->ip_pld.icmp6hdr;
    TEST_ASSERT_EQUAL_UINT(parser->packet_len, parser->parsed);
    TEST_ASSERT_EQUAL_UINT(ICMP6_ECHO_REQUEST, icmphdr->icmp6_type);
}

/**
 * @brief Parsing pcap.c's pkt16608
 *
 * pkt16608 is a TCP/IPv4 packet over vlan4.
 * It carries the data "Bonjour"
 * See pcap.json for details.
 */
void test_tcp_ipv4(void)
{
    struct net_header_parser *parser;
    struct eth_header *eth_header;
    struct iphdr *hdr;
    size_t len;
    char *expected_data = "Bonjour\n";
    char data[strlen(expected_data) + 1];

    UT_CREATE_PCAP_PAYLOAD(pkt16608, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* validate vlan id */
    eth_header = &parser->eth_header;
    TEST_ASSERT_EQUAL_UINT(4, eth_header->vlan_id);

    hdr = net_header_get_ipv4_hdr(parser);
    TEST_ASSERT_NOT_NULL(hdr);
    TEST_ASSERT_EQUAL_INT(IPPROTO_TCP, parser->ip_protocol);

    memset(data, 0, sizeof(data));
    memcpy(data, parser->data, strlen(expected_data));
    TEST_ASSERT_EQUAL_STRING(expected_data, data);
}


/**
 * @brief Parsing pcap.c's pkt1200
 *
 * pkt1200 is a TCP/IPv6 packet over vlan4.
 * It carries the data "Bonjour"
 * See pcap.json for details.
 */
void test_tcp_ipv6(void)
{
    struct net_header_parser *parser;
    struct eth_header *eth_header;
    struct ip6_hdr *hdr;
    size_t len;
    char *expected_data = "Bonjour\n";
    char data[strlen(expected_data) + 1];

    UT_CREATE_PCAP_PAYLOAD(pkt1200, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* validate vlan id */
    eth_header = &parser->eth_header;
    TEST_ASSERT_EQUAL_UINT(4, eth_header->vlan_id);

    hdr = net_header_get_ipv6_hdr(parser);
    TEST_ASSERT_NOT_NULL(hdr);
    TEST_ASSERT_EQUAL_INT(IPPROTO_TCP, parser->ip_protocol);

    memset(data, 0, sizeof(data));
    memcpy(data, parser->data, strlen(expected_data));
    TEST_ASSERT_EQUAL_STRING(expected_data, data);
}


/**
 * @brief Parsing pcap.c's pkt9568
 *
 * pkt9568 is a UDP/IPv4 packet over vlan4.
 * It carries the data "Bonjour"
 * See pcap.json for details.
 */
void test_udp_ipv4(void)
{
    struct net_header_parser *parser;
    struct eth_header *eth_header;
    struct iphdr *hdr;
    size_t len;
    char *expected_data = "Bonjour\n";
    char data[strlen(expected_data) + 1];

    UT_CREATE_PCAP_PAYLOAD(pkt9568, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* validate vlan id */
    eth_header = &parser->eth_header;
    TEST_ASSERT_EQUAL_UINT(4, eth_header->vlan_id);

    hdr = net_header_get_ipv4_hdr(parser);
    TEST_ASSERT_NOT_NULL(hdr);
    TEST_ASSERT_EQUAL_INT(IPPROTO_UDP, parser->ip_protocol);

    memset(data, 0, sizeof(data));
    memcpy(data, parser->data, strlen(expected_data));
    TEST_ASSERT_EQUAL_STRING(expected_data, data);
    TEST_ASSERT_TRUE(parser->packet_len != parser->parsed);
}


/**
 * @brief Parsing pcap.c's pkt12176
 *
 * pkt12176 is a ARP request over vlan4.
 * See pcap.json for details.
 */
void test_arp_request(void)
{
    struct net_header_parser *parser;
    struct eth_header *eth_header;
    struct arphdr *hdr;
    int ethertype;
    size_t len;

    UT_CREATE_PCAP_PAYLOAD(pkt12176, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* validate vlan id */
    eth_header = &parser->eth_header;
    TEST_ASSERT_EQUAL_UINT(4, eth_header->vlan_id);

    ethertype = net_header_get_ethertype(parser);
    TEST_ASSERT_EQUAL_INT(ETH_P_ARP, ethertype);
    hdr = (struct arphdr *)(parser->eth_pld.payload);
    TEST_ASSERT_EQUAL_UINT(ARPOP_REQUEST, ntohs(hdr->ar_op));
}

/**
 * @brief Parsing pcap.c's pkt244
 *
 * pkt244 is a ICMP4 request
 * See pcap.json for details.
 */
void test_icmp4_request(void)
{
    struct net_header_parser *parser;
    size_t len;
    struct iphdr *ip_hdr;
    char *src_ip = "10.2.22.238";
    char *dst_ip = "69.147.88.8";
    struct in_addr src_addr;
    struct in_addr dst_addr;
    struct icmphdr *icmp_hdr;

    UT_CREATE_PCAP_PAYLOAD(pkt244, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* validate ip hdr */
    ip_hdr = net_header_get_ipv4_hdr(parser);
    TEST_ASSERT_NOT_NULL(ip_hdr);
    TEST_ASSERT_EQUAL_INT(IPPROTO_ICMP, parser->ip_protocol);
    inet_pton(AF_INET, src_ip, &src_addr);
    inet_pton(AF_INET, dst_ip, &dst_addr);
    LOGD("src addr exp: %d act: %d", src_addr.s_addr, ip_hdr->saddr);
    TEST_ASSERT_EQUAL_UINT32(src_addr.s_addr, ip_hdr->saddr);
    LOGD("dst addr exp: %d act: %d", dst_addr.s_addr, ip_hdr->daddr);
    TEST_ASSERT_EQUAL_UINT32(dst_addr.s_addr, ip_hdr->daddr);

    /* icmp hdr validation */
    TEST_ASSERT_NOT_NULL(parser->eth_pld.payload);
    icmp_hdr = parser->ip_pld.icmphdr;
    TEST_ASSERT_EQUAL_UINT8(8, icmp_hdr->type);
    TEST_ASSERT_EQUAL_UINT8(0, icmp_hdr->code);
    TEST_ASSERT_EQUAL_UINT16(3320, icmp_hdr->un.echo.id);
}

/**
 * @brief Parsing pcap.c's pkt245
 *
 * pkt245 is a ICMP4 reply.
 * See pcap.json for details.
 */
void test_icmp4_reply(void)
{
    struct net_header_parser *parser;
    size_t len;
    struct iphdr *ip_hdr;
    char *src_ip = "69.147.88.8";
    char *dst_ip = "10.2.22.238";
    struct in_addr src_addr;
    struct in_addr dst_addr;
    struct icmphdr *icmp_hdr;

    UT_CREATE_PCAP_PAYLOAD(pkt245, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* validate ip hdr */
    ip_hdr = net_header_get_ipv4_hdr(parser);
    TEST_ASSERT_NOT_NULL(ip_hdr);
    TEST_ASSERT_EQUAL_INT(IPPROTO_ICMP, parser->ip_protocol);
    inet_pton(AF_INET, src_ip, &src_addr);
    inet_pton(AF_INET, dst_ip, &dst_addr);
    TEST_ASSERT_EQUAL_UINT32(src_addr.s_addr, ip_hdr->saddr);
    TEST_ASSERT_EQUAL_UINT32(dst_addr.s_addr, ip_hdr->daddr);

    /* icmp hdr validation */
    TEST_ASSERT_NOT_NULL(parser->eth_pld.payload);
    icmp_hdr = parser->ip_pld.icmphdr;
    TEST_ASSERT_EQUAL_UINT8(0, icmp_hdr->type);
    TEST_ASSERT_EQUAL_UINT8(0, icmp_hdr->code);
}


/**
 * @brief Parsing pcap.c's pkt_udp_no_data
 *
 * pkt_udp_no_data is a udp packet over IPv4 with no data.
 */
void test_udp_ipv4_no_data(void)
{
    struct net_header_parser *parser;
    struct eth_header *eth_header;
    char *src_ip = "10.2.0.43";
    char *dst_ip = "10.2.0.2";
    struct in_addr src_addr;
    struct in_addr dst_addr;
    struct iphdr *ip_hdr;
    size_t len;

    UT_CREATE_PCAP_PAYLOAD(pkt_udp_no_data, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* validate vlan id */
    eth_header = &parser->eth_header;
    TEST_ASSERT_EQUAL_UINT(0, eth_header->vlan_id);

    ip_hdr = net_header_get_ipv4_hdr(parser);
    TEST_ASSERT_NOT_NULL(ip_hdr);
    TEST_ASSERT_EQUAL_INT(IPPROTO_UDP, parser->ip_protocol);

    inet_pton(AF_INET, src_ip, &src_addr);
    inet_pton(AF_INET, dst_ip, &dst_addr);
    TEST_ASSERT_EQUAL_UINT32(src_addr.s_addr, ip_hdr->saddr);
    TEST_ASSERT_EQUAL_UINT32(dst_addr.s_addr, ip_hdr->daddr);
    TEST_ASSERT_TRUE(parser->packet_len == parser->parsed);
}


/**
 * @brief print net_header_parser with and without flow details
 *
 */
void test_flow_details(void)
{
    struct net_md_stats_accumulator acc;
    struct net_header_parser *parser;
    size_t len;

    /* IPv6 Packet parse */
    UT_CREATE_PCAP_PAYLOAD(pkt16574, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    memset(&acc, 0, sizeof(acc));

    /* add accumulator details */
    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;
    parser->acc = &acc;

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* IPv4 Packet parse */
    memset(&g_parser, 0, sizeof(g_parser));
    UT_CREATE_PCAP_PAYLOAD(pkt16608, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* add accumulator details */
    parser->acc = &acc;

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* UDP Packet parse */
    memset(&g_parser, 0, sizeof(g_parser));
    UT_CREATE_PCAP_PAYLOAD(pkt_udp_no_data, &g_parser);
    parser = &g_parser;

    /* Validate parsing success */
    len = net_header_parse(parser);
    TEST_ASSERT_TRUE(len != 0);

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));

    /* add accumulator details */
    parser->acc = &acc;

    LOGI("%s: %s", __func__,
         net_header_fill_info_buf(log_buf, NET_HDR_BUFF_SIZE, parser));
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name);
    ut_setUp_tearDown(ut_name, ustack_setUp, ustack_tearDown);

    RUN_TEST(test_tcp_ipv4);
    RUN_TEST(test_tcp_ipv6);
    RUN_TEST(test_udp_ipv4);
    RUN_TEST(test_arp_request);
    RUN_TEST(test_icmpv6_pcap_parse);
    RUN_TEST(test_icmp4_request);
    RUN_TEST(test_icmp4_reply);
    RUN_TEST(test_udp_ipv4_no_data);
    RUN_TEST(test_flow_details);

    ut_fini();

    return UNITY_END();
}
