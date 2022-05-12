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

#include <libgen.h>

#include "network_metadata_utils.h"
#include "network_metadata_report.h"
#include "memutil.h"
#include "unity.h"
#include "log.h"

static char* short_string = "short_string";
static char* long_string =
    "0123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789"; /* 280 characters */

static os_ufid_t ufid[] =
{
  {
    .u32[0] = 1942808158,
    .u32[1] = 2090304818,
    .u32[2] = 1862539095,
    .u32[3] = 1411918068,
  },
  {
    .u32[0] = 1293127805,
    .u32[1] = 1805432549,
    .u32[2] = 1562745426,
    .u32[3] = 1934593071,
  },
};

static uint8_t src_ip[]  = { 192, 168, 0, 1 };
static char *src_ip_txt  = "192.168.0.1";
static uint8_t dst_ip[]  = { 127,   0, 0, 1 };
static char *dst_ip_txt  = "127.0.0.1";
static uint8_t src_mac[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
static char *src_mac_txt = "00:11:22:33:44:55";
static uint8_t dst_mac[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
static char *dst_mac_txt = "aa:bb:cc:dd:ee:ff";

/**
 * @brief tests the str2os_mac() utility
 */
void test_utils_str2os_mac(void)
{
    char *mac1 = "fe:dc:ba:01:23:45";
    char *mac2 = "FE:DC:BA:01:23:45";
    char *mac3 = "fe:dc:ba";
    char *mac4 = "ze:dc:ba:01:23:45";
    char *mac5 = "fe:dc:ba:01:23:4:";
    char *mac6 = "fe:dc:ba:01:23:456";
    os_macaddr_t *mac;
    char check_mac[32] = { 0 };
    int cmp;

    /* Validate correct mac in lower case */
    mac = str2os_mac(mac1);
    TEST_ASSERT_NOT_NULL(mac);
    snprintf(check_mac, sizeof(check_mac), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(mac));
    cmp = strncmp(check_mac, mac1, sizeof(check_mac));
    TEST_ASSERT_EQUAL_INT(0, cmp);
    FREE(mac);

    /* Validate correct mac in upper case */
    mac = str2os_mac(mac2);
    TEST_ASSERT_NOT_NULL(mac);
    memset(check_mac, 0, sizeof(check_mac));
    snprintf(check_mac, sizeof(check_mac), PRI_os_macaddr_t,
             FMT_os_macaddr_pt(mac));
    cmp = strncmp(check_mac, mac2, sizeof(check_mac));
    TEST_ASSERT_EQUAL_INT(0, cmp);
    FREE(mac);

    /* Validate NULL argument */
    mac = str2os_mac(NULL);
    TEST_ASSERT_NULL(mac);

    /* Validate too short of a string */
    mac = str2os_mac(mac3);
    TEST_ASSERT_NULL(mac);

    /* Validate wrong string (contains a 'z') */
    mac = str2os_mac(mac4);
    TEST_ASSERT_NULL(mac);

    /* Validate wrong string (last character is ':') */
    mac = str2os_mac(mac5);
    TEST_ASSERT_NULL(mac);

    /* Validate too long of a string */
    mac = str2os_mac(mac6);
    TEST_ASSERT_NULL(mac);
}

void
test_utils_net_md_set_str(void)
{
    char *output;

    output = net_md_set_str(NULL);
    TEST_ASSERT_NULL(output);

    output = net_md_set_str("");
    TEST_ASSERT_NULL(output);

    output = net_md_set_str(short_string);
    TEST_ASSERT_EQUAL_STRING(short_string, output);
    TEST_ASSERT_EQUAL(strlen(short_string), strlen(output));
    FREE(output);

    output = net_md_set_str(long_string);
    TEST_ASSERT_EQUAL(MD_MAX_STRLEN, strlen(output));
    FREE(output);
}

void
test_utils_net_md_set_ufid(void)
{
    os_ufid_t in = {
        .u32 = {1, 2, 3, 4}
    };
    os_ufid_t *out;

    out = net_md_set_ufid(NULL);
    TEST_ASSERT_NULL(out);

    out = net_md_set_ufid(&in);
    TEST_ASSERT_EQUAL_UINT32_ARRAY(in.u32, out->u32, sizeof(in.u32)/sizeof(in.u32[0]));
    FREE(out);
}

void
test_utils_net_md_set_os_macaddr(void)
{
    os_macaddr_t in = {
        .addr = {00, 11, 22, 33, 44, 55}
    };
    os_macaddr_t *out;

    out = net_md_set_os_macaddr(NULL);
    TEST_ASSERT_NULL(out);

    out = net_md_set_os_macaddr(&in);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.addr, out->addr, sizeof(in.addr)/sizeof(in.addr[0]));
    FREE(out);
}

void
test_utils_net_md_set_ip(void)
{
    uint8_t ipv4[4]  = {1, 2, 3, 4};
    uint8_t ipv6[16] =
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};
    uint8_t *out;
    bool ret;

    ret = net_md_set_ip(4, ipv4, NULL);
    TEST_ASSERT_FALSE(ret);

    ret = net_md_set_ip(5, ipv4, &out);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NULL(out);

    ret = net_md_set_ip(4, ipv4, &out);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ipv4, out, sizeof(ipv4));
    FREE(out);

    ret = net_md_set_ip(6, ipv6, &out);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ipv6, out, sizeof(ipv6));
    FREE(out);

    /* Frankenstein tests. This should never happen. But it should
     * also be guaranteed to not break.
     */
    ret = net_md_set_ip(4, ipv6, &out);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ipv6, out, sizeof(ipv4));
    FREE(out);

    /* We can't test this "READ overflow" condition properly as UT will ABORT().
     * This is not a crash-bug, but could lead to some garbage data.
     *
    ret = net_md_set_ip(6, ipv4, &out);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ipv4, out, sizeof(ipv4));
    FREE(out);
     */
}

void
test_utils_set_node_info(void)
{
    struct node_info info;
    struct node_info *out;

    out = net_md_set_node_info(NULL);
    TEST_ASSERT_NULL(out);

    info.node_id     = NULL;
    info.location_id = NULL;
    out = net_md_set_node_info(&info);
    TEST_ASSERT_NULL(out);

    info.node_id = "NODE_ID";
    out = net_md_set_node_info(&info);
    TEST_ASSERT_NULL(out);

    info.location_id = "LOCATION_ID";
    out = net_md_set_node_info(&info);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_STRING(info.node_id, out->node_id);
    TEST_ASSERT_EQUAL_STRING(info.node_id, out->node_id);

    free_node_info(out);
    FREE(out);
}


void
test_utils_set_net_md_flow_key(void)
{
    struct net_md_flow_key *in;
    struct net_md_flow_key *out;
    int ret;

    /* Make sure to wipe out the 'in' parameter */
    in = CALLOC(1, sizeof(*in));

    /* Straight copy of the struct. No memory allocation involved. */
    out = set_net_md_flow_key(in);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_MEMORY_ARRAY(in, out, sizeof(in), 1);
    free_net_md_flow_key(out);
    FREE(out);

    /* Start populating things some more */
    in->ufid = net_md_set_ufid(&ufid[0]);
    TEST_ASSERT_NOT_NULL(in->ufid);
    out = set_net_md_flow_key(in);
    TEST_ASSERT_NOT_NULL(out->ufid);
    ret = net_md_eth_cmp(in, out);
    TEST_ASSERT_EQUAL_INT(0, ret);
    free_net_md_flow_key(out);
    FREE(out);

    /* Add smac */
    in->smac = CALLOC(1, sizeof(*in->smac));
    TEST_ASSERT_NOT_NULL(in->smac);
    in->smac->addr[0] = 0xAA;
    out = set_net_md_flow_key(in);
    TEST_ASSERT_EQUAL(in->smac->addr[0], out->smac->addr[0]);
    free_net_md_flow_key(out);
    FREE(out);

    /* Add dmac */
    in->dmac = CALLOC(1, sizeof(*in->smac));
    TEST_ASSERT_NOT_NULL(in->dmac);
    in->dmac->addr[0] = 0x55;
    out = set_net_md_flow_key(in);
    TEST_ASSERT_EQUAL(in->dmac->addr[0], out->dmac->addr[0]);
    free_net_md_flow_key(out);
    FREE(out);

    /* Using wrong IP version */
    in->src_ip = CALLOC(16, sizeof(*in->src_ip));
    TEST_ASSERT_NOT_NULL(in->src_ip);
    in->src_ip[0] = 192;
    out = set_net_md_flow_key(in);
    TEST_ASSERT_NULL(out->src_ip);
    free_net_md_flow_key(out);
    FREE(out);

    /* IPv4 */
    in->ip_version = 4;
    out = set_net_md_flow_key(in);
    TEST_ASSERT_EQUAL(in->src_ip[0], out->src_ip[0]);
    TEST_ASSERT_NULL(out->dst_ip);
    free_net_md_flow_key(out);
    FREE(out);

    /* dest IP */
    in->dst_ip = CALLOC(16, sizeof(*in->dst_ip));
    TEST_ASSERT_NOT_NULL(in->dst_ip);
    in->dst_ip[0] = 127;
    out = set_net_md_flow_key(in);
    TEST_ASSERT_EQUAL(in->dst_ip[0], out->dst_ip[0]);
    free_net_md_flow_key(out);
    FREE(out);

    /* Final cleanup */
    free_net_md_flow_key(in);
    FREE(in);
}

void
test_utils_net_md_set_flow_key(void)
{
    struct net_md_flow_key *in;
    struct flow_key *out;

    in = CALLOC(1, sizeof(*in));

    in->src_ip = CALLOC(4, sizeof(*in->src_ip));
    TEST_ASSERT_NOT_NULL(in->src_ip);
    memcpy(in->src_ip, src_ip, 4);
    in->dst_ip = CALLOC(4, sizeof(*in->dst_ip));
    TEST_ASSERT_NOT_NULL(in->dst_ip);
    memcpy(in->dst_ip, dst_ip, 4);

    in->ip_version = 0;
    out = net_md_set_flow_key(in);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_NULL(out->smac);
    TEST_ASSERT_NULL(out->dmac);
    TEST_ASSERT_NULL(out->src_ip);
    TEST_ASSERT_NULL(out->dst_ip);
    free_flow_key(out);
    FREE(out);

    in->smac = CALLOC(1, sizeof(*in->smac));
    TEST_ASSERT_NOT_NULL(in->smac);
    memcpy(in->smac->addr, src_mac, sizeof(in->smac->addr)/sizeof(in->smac->addr[0]));
    in->dmac = CALLOC(1, sizeof(*in->dmac));
    TEST_ASSERT_NOT_NULL(in->dmac);
    memcpy(in->dmac->addr, dst_mac, sizeof(in->dmac->addr)/sizeof(in->dmac->addr[0]));
    in->ip_version = 4;
    out = net_md_set_flow_key(in);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_STRING(src_mac_txt, out->smac);
    TEST_ASSERT_EQUAL_STRING(dst_mac_txt, out->dmac);
    free_flow_key(out);
    FREE(out);

    free_net_md_flow_key(in);
    FREE(in);
}

void
test_utils_net_md_set_acc(void)
{
    struct net_md_aggregator        *aggr = NULL;
    struct net_md_flow_key          *key  = NULL;
    struct net_md_stats_accumulator *out;

    out = net_md_set_acc(aggr, key);
    TEST_ASSERT_NULL(out);

    /* Test for both branches in sanity check */
    key = CALLOC(1, sizeof(*key));
    TEST_ASSERT_NOT_NULL(key);
    out = net_md_set_acc(aggr, key);
    TEST_ASSERT_NULL(out);
    FREE(key);
    key = NULL;

    aggr = CALLOC(1, sizeof(*aggr));
    TEST_ASSERT_NOT_NULL(aggr);
    out = net_md_set_acc(aggr, key);
    TEST_ASSERT_NULL(out);
    FREE(aggr);

    /* Create aggr and key for further testing */
    aggr = CALLOC(1, sizeof(*aggr));
    TEST_ASSERT_NOT_NULL(aggr);
    aggr->report = CALLOC(1, sizeof(*aggr->report));

    key = CALLOC(1, sizeof(*key));
    TEST_ASSERT_NOT_NULL(key);
    key->ip_version = 4;
    key->src_ip = CALLOC(4, sizeof(*key->src_ip));
    TEST_ASSERT_NOT_NULL(key->src_ip);
    memcpy(key->src_ip, src_ip, 4);
    key->dst_ip = CALLOC(4, sizeof(*key->dst_ip));
    TEST_ASSERT_NOT_NULL(key->dst_ip);
    memcpy(key->dst_ip, dst_ip, 4);

    out = net_md_set_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_PTR(aggr, out->aggr);
    TEST_ASSERT_EQUAL_UINT8(key->ip_version, out->key->ip_version);
    TEST_ASSERT_EQUAL_UINT8(key->ip_version, out->fkey->ip_version);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(key->src_ip, out->key->src_ip, 4);
    TEST_ASSERT_EQUAL_STRING(src_ip_txt, out->fkey->src_ip);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(key->dst_ip, out->key->dst_ip, 4);
    TEST_ASSERT_EQUAL_STRING(dst_ip_txt, out->fkey->dst_ip);
    net_md_free_acc(out);
    FREE(out);

    /* Test corner case */
    key->ip_version = 0;
    out = net_md_set_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_NULL(out->key->src_ip);
    TEST_ASSERT_NULL(out->fkey->src_ip);

    /* Final cleanup */
    net_md_free_acc(out);
    FREE(out);
    net_md_free_aggregator(aggr); /* aggr linked into 'out' : order of free matters */
    FREE(aggr);
    free_net_md_flow_key(key);
    FREE(key);
}

void
test_utils_net_md_set_eth_pair(void)
{
    struct net_md_aggregator *aggr = NULL;
    struct net_md_flow_key   *key  = NULL;
    struct net_md_eth_pair   *out;

    out = net_md_set_eth_pair(aggr, key);
    TEST_ASSERT_NULL(out);

    key = CALLOC(1, sizeof(*key));
    TEST_ASSERT_NOT_NULL(key);
    out = net_md_set_eth_pair(aggr, key);
    TEST_ASSERT_NULL(out);

    key->flags = NET_MD_ACC_LOOKUP_ONLY;
    out = net_md_set_eth_pair(aggr, key);
    TEST_ASSERT_NULL(out);

    key->flags = NET_MD_ACC_CREATE;
    aggr = CALLOC(1, sizeof(*aggr));
    TEST_ASSERT_NOT_NULL(aggr);
    aggr->report = CALLOC(1, sizeof(*aggr->report));
    TEST_ASSERT_NOT_NULL(aggr->report);

    key->ip_version = 4;
    key->src_ip = CALLOC(4, sizeof(*key->src_ip));
    TEST_ASSERT_NOT_NULL(key->src_ip);
    memcpy(key->src_ip, src_ip, 4);
    key->dst_ip = CALLOC(4, sizeof(*key->dst_ip));
    TEST_ASSERT_NOT_NULL(key->dst_ip);
    memcpy(key->dst_ip, dst_ip, 4);

    out = net_md_set_eth_pair(aggr, key);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_PTR(aggr, out->mac_stats->aggr);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(key->src_ip, out->mac_stats->key->src_ip, 4);
    TEST_ASSERT_EQUAL_STRING(src_ip_txt, out->mac_stats->fkey->src_ip);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(key->dst_ip, out->mac_stats->key->dst_ip, 4);
    TEST_ASSERT_EQUAL_STRING(dst_ip_txt, out->mac_stats->fkey->dst_ip);

    TEST_ASSERT_EQUAL_INT(0, ds_tree_check(&out->ethertype_flows));
    TEST_ASSERT_EQUAL_INT(0, ds_tree_check(&out->five_tuple_flows));
    TEST_ASSERT_NULL(out->ethertype_flows.ot_root);
    TEST_ASSERT_NULL(out->five_tuple_flows.ot_root);
    TEST_ASSERT_NOT_NULL(out->ethertype_flows.ot_cmp_fn);
    TEST_ASSERT_NOT_NULL(out->five_tuple_flows.ot_cmp_fn);

    /* Final cleanup */
    net_md_free_eth_pair(out);
    FREE(out);
    net_md_free_aggregator(aggr); /* aggr linked into 'out->mac_stats' */
    FREE(aggr);
    free_net_md_flow_key(key);
    FREE(key);
}

void
test_utils_net_md_lookup_eth_pair(void)
{
    struct net_md_aggregator_set    *aggr_set = NULL;
    struct node_info n_info = {
        .node_id = "SERIAL_NUM",
        .location_id = "AT_HOME"
    };
    struct net_md_aggregator        *aggr = NULL;
    struct net_md_flow_key          *key  = NULL;
    struct net_md_eth_pair *out;
    struct net_md_eth_pair *out_copy;

    key = CALLOC(1, sizeof(*key));
    TEST_ASSERT_NOT_NULL(key);
    key->ip_version = 4;
    key->src_ip = CALLOC(4, sizeof(*key->src_ip));
    TEST_ASSERT_NOT_NULL(key->src_ip);
    memcpy(key->src_ip, src_ip, 4);
    key->dst_ip = CALLOC(4, sizeof(*key->dst_ip));
    TEST_ASSERT_NOT_NULL(key->dst_ip);
    memcpy(key->dst_ip, dst_ip, 4);

    /* Create a valid aggregator_set for the constructor */
    aggr_set = CALLOC(1, sizeof(*aggr_set));
    aggr_set->info = &n_info;
    aggr_set->num_windows = 1;
    aggr_set->acc_ttl = INT32_MAX;
    aggr_set->report_type = NET_MD_REPORT_ABSOLUTE;
    aggr_set->report_filter = NULL;
    aggr_set->send_report = net_md_send_report;

    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* Broken input */
    out = net_md_lookup_eth_pair(NULL, key);
    TEST_ASSERT_NULL(out);

    /* since key has no smac/dmac, this should return NULL */
    out = net_md_lookup_eth_pair(aggr, key);
    TEST_ASSERT_NULL(out);

    key->smac = CALLOC(1, sizeof(*key->smac));
    TEST_ASSERT_NOT_NULL(key->smac);
    memcpy(key->smac, src_mac, sizeof(*key->smac));
    key->dmac = CALLOC(1, sizeof(*key->dmac));
    TEST_ASSERT_NOT_NULL(key->dmac);
    memcpy(key->dmac, dst_mac, sizeof(*key->dmac));
    key->ip_version = 0;

    /* Lookup twice (insert, then fetch record) */
    key->flags = NET_MD_ACC_CREATE;

    out = net_md_lookup_eth_pair(aggr, key);
    TEST_ASSERT_NOT_NULL(out);

    out_copy = net_md_lookup_eth_pair(aggr, key);
    TEST_ASSERT_NOT_NULL(out_copy);
    TEST_ASSERT_EQUAL_PTR(out, out_copy);

    /* Cleanup */
    FREE(aggr_set);
    net_md_free_aggregator(aggr);
    FREE(aggr);
    free_net_md_flow_key(key);
    FREE(key);
}

void
test_utils_net_md_lookup_acc_from_pair(void)
{
    struct net_md_aggregator_set    *aggr_set = NULL;
    struct node_info n_info = {
        .node_id = "SERIAL_NUM",
        .location_id = "AT_HOME"
    };
    struct net_md_aggregator        *aggr = NULL;
    struct net_md_eth_pair          *pair = NULL;
    struct net_md_flow_key          *key  = NULL;
    struct net_md_stats_accumulator *out;
    struct net_md_stats_accumulator *out_copy;

    key = CALLOC(1, sizeof(*key));
    TEST_ASSERT_NOT_NULL(key);
    key->ip_version = 4;
    key->src_ip = CALLOC(4, sizeof(*key->src_ip));
    TEST_ASSERT_NOT_NULL(key->src_ip);
    memcpy(key->src_ip, src_ip, 4);
    key->dst_ip = CALLOC(4, sizeof(*key->dst_ip));
    TEST_ASSERT_NOT_NULL(key->dst_ip);
    memcpy(key->dst_ip, dst_ip, 4);

    /* Create a valid aggregator_set for the constructor */
    aggr_set = CALLOC(1, sizeof(*aggr_set));
    aggr_set->info = &n_info;
    aggr_set->num_windows = 1;
    aggr_set->acc_ttl = INT32_MAX;
    aggr_set->report_type = NET_MD_REPORT_ABSOLUTE;
    aggr_set->report_filter = NULL;
    aggr_set->send_report = net_md_send_report;

    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    key->smac = CALLOC(1, sizeof(*key->smac));
    TEST_ASSERT_NOT_NULL(key->smac);
    memcpy(key->smac, src_mac, sizeof(*key->smac));
    key->dmac = CALLOC(1, sizeof(*key->dmac));
    TEST_ASSERT_NOT_NULL(key->dmac);
    memcpy(key->dmac, dst_mac, sizeof(*key->dmac));
    key->ip_version = 0;
    key->flags = NET_MD_ACC_CREATE;

    pair = net_md_lookup_eth_pair(aggr, key);
    TEST_ASSERT_NOT_NULL(pair);

    /* Lookup twice (insert, then fetch record) */
    out = net_md_lookup_acc_from_pair(aggr, pair, key);
    TEST_ASSERT_NOT_NULL(out);

    /* we already inserted this, so we should get a ptr to the previous entry */
    out_copy = net_md_lookup_acc_from_pair(aggr, pair, key);
    TEST_ASSERT_NOT_NULL(out_copy);
    TEST_ASSERT_EQUAL_PTR(out, out_copy);

    /* Cleanup */
    FREE(aggr_set);
    net_md_free_aggregator(aggr);
    FREE(aggr);
    free_net_md_flow_key(key);
    FREE(key);
}

void
test_unit_net_md_lookup_eth_acc(void)
{
    struct net_md_aggregator_set    *aggr_set = NULL;
    struct node_info n_info = {
        .node_id = "SERIAL_NUM",
        .location_id = "AT_HOME"
    };
    struct net_md_aggregator        *aggr = NULL;
    struct net_md_flow_key          *key  = NULL;
    struct net_md_stats_accumulator *out;
    struct net_md_stats_accumulator *out_copy;

    key = CALLOC(1, sizeof(*key));
    TEST_ASSERT_NOT_NULL(key);
    key->ip_version = 4;
    key->src_ip = CALLOC(4, sizeof(*key->src_ip));
    TEST_ASSERT_NOT_NULL(key->src_ip);
    memcpy(key->src_ip, src_ip, 4);
    key->dst_ip = CALLOC(4, sizeof(*key->dst_ip));
    TEST_ASSERT_NOT_NULL(key->dst_ip);
    memcpy(key->dst_ip, dst_ip, 4);

    /* Create a valid aggregator_set for the constructor */
    aggr_set = CALLOC(1, sizeof(*aggr_set));
    aggr_set->info = &n_info;
    aggr_set->num_windows = 1;
    aggr_set->acc_ttl = INT32_MAX;
    aggr_set->report_type = NET_MD_REPORT_ABSOLUTE;
    aggr_set->report_filter = NULL;
    aggr_set->send_report = net_md_send_report;

    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* broken input */
    out = net_md_lookup_eth_acc(NULL, key);
    TEST_ASSERT_NULL(out);

    /* No eth from key */
    out = net_md_lookup_eth_acc(aggr, key);
    TEST_ASSERT_NULL(out);

    /* we got eth info */
    key->smac = CALLOC(1, sizeof(*key->smac));
    TEST_ASSERT_NOT_NULL(key->smac);
    memcpy(key->smac, src_mac, sizeof(*key->smac));
    key->dmac = CALLOC(1, sizeof(*key->dmac));
    TEST_ASSERT_NOT_NULL(key->dmac);
    memcpy(key->dmac, dst_mac, sizeof(*key->dmac));
    key->ip_version = 0;
    key->flags = NET_MD_ACC_CREATE;

    out = net_md_lookup_eth_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_PTR(aggr, out->aggr);
    /* No need to clean `out` as it is a reference to ds_tree entry */

    out_copy = net_md_lookup_eth_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(out_copy);
    TEST_ASSERT_EQUAL_PTR(out, out_copy);

    /* Cleanup */
    FREE(aggr_set);
    net_md_free_aggregator(aggr);
    FREE(aggr);
    free_net_md_flow_key(key);
    FREE(key);
}

void
test_network_metadata_utils(void)
{
    char *filename = STRDUP(__FILE__);
    const char *this_filename = basename(filename);
    const char *old_filename = Unity.TestFile;
    UnitySetTestFile(this_filename);

    RUN_TEST(test_utils_str2os_mac);
    RUN_TEST(test_utils_net_md_set_str);
    RUN_TEST(test_utils_net_md_set_ufid);
    RUN_TEST(test_utils_net_md_set_os_macaddr);
    RUN_TEST(test_utils_net_md_set_ip);
    RUN_TEST(test_utils_set_node_info);

    RUN_TEST(test_utils_set_net_md_flow_key);   /* watchout for the function name! */
    RUN_TEST(test_utils_net_md_set_flow_key);   /* watchout for the function name! */

    RUN_TEST(test_utils_net_md_set_acc);
    RUN_TEST(test_utils_net_md_set_eth_pair);

    RUN_TEST(test_utils_net_md_lookup_eth_pair);
    RUN_TEST(test_utils_net_md_lookup_acc_from_pair);
    RUN_TEST(test_unit_net_md_lookup_eth_acc);

    UnitySetTestFile(old_filename);
    FREE(filename);
}

