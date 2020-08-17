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
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "network_metadata_report.h"
#include "target.h"
#include "unity.h"
#include "test_network_metadata.h"
#include "qm_conn.h"

struct test_network_data_report g_nd_test;

struct in_key in_keys[] =
{
    {
        .smac = "11:22:33:44:55:66",
        .dmac = "dd:ee:ff:cc:bb:aa",
        .vlan_id = 0,
        .ethertype = 0,
        .ip_version = 0
    },
    {
        .smac = "11:22:33:44:55:66",
        .dmac = "dd:ee:ff:cc:bb:aa",
        .vlan_id = 0,
        .ethertype = 1,
        .ip_version = 0
    },
    {
        .smac = "11:22:33:44:55:66",
        .dmac = "dd:ee:ff:cc:bb:aa",
        .vlan_id = 1,
        .ethertype = 0x8000,
        .ip_version = 0
    },
    {
        .ip_version = 4,
        .src_ip = "192.168.40.1",
        .dst_ip = "32.33.34.35",
        .ipprotocol = 2,
    },
    {
        .ip_version = 4,
        .src_ip = "192.168.40.1",
        .dst_ip = "32.33.34.35",
        .ipprotocol = 17,
        .sport = 36000,
        .dport = 1234,
    },
    {
        .smac = "11:22:33:44:55:66",
        .dmac = "dd:ee:ff:cc:bb:aa",
        .vlan_id = 0,
        .ethertype = 0,
        .ip_version = 4,
        .src_ip = "192.168.40.1",
        .dst_ip = "32.33.34.35",
        .ipprotocol = 17,
        .sport = 12345,
        .dport = 53,
    },
    {
        .smac = "11:22:33:44:55:66",
        .dmac = "dd:ee:ff:cc:bb:aa",
        .vlan_id = 0,
        .ethertype = 0,
        .ip_version = 4,
        .src_ip = "192.168.40.1",
        .dst_ip = "32.33.34.35",
        .ipprotocol = 17,
        .sport = 12346,
        .dport = 53,
    },
    {
        .smac = "11:22:33:44:55:66",
        .dmac = "dd:ee:ff:cc:bb:aa",
        .vlan_id = 100,
        .ethertype = 0,
        .ip_version = 0
    },
    {
        .smac = "22:33:44:55:66:77",
        .dmac = "dd:ee:ff:cc:bb:aa",
        .vlan_id = 100,
        .ethertype = 0,
        .ip_version = 0
    },
    {
        .smac = "11:22:33:44:55:66",
        .vlan_id = 0,
        .ethertype = 0,
        .ip_version = 4,
        .src_ip = "192.168.40.1",
        .dst_ip = "32.33.34.35",
        .ipprotocol = 17,
        .sport = 12345,
        .dport = 53,
    },
    {
        .dmac = "dd:ee:ff:cc:bb:aa",
        .vlan_id = 0,
        .ethertype = 0,
        .ip_version = 4,
        .src_ip = "192.168.40.1",
        .dst_ip = "32.33.34.35",
        .ipprotocol = 17,
        .sport = 12345,
        .dport = 53,
    },
    {
        .smac = "11:22:33:44:55:66",
        .vlan_id = 0,
        .ethertype = 0,
        .ip_version = 4,
        .src_ip = "192.168.40.1",
        .dst_ip = "32.33.34.35",
        .ipprotocol = 17,
        .sport = 60078,
        .dport = 5001,
    },
    {
        .dmac = "11:22:33:44:55:66",
        .vlan_id = 0,
        .ethertype = 0,
        .ip_version = 4,
        .dst_ip = "192.168.40.1",
        .src_ip = "32.33.34.35",
        .ipprotocol = 17,
        .sport = 5001,
        .dport = 60078,
    },
    {
        .smac = "11:22:33:44:55:66",
        .vlan_id = 0,
        .ethertype = 0,
        .ip_version = 4,
        .src_ip = "192.168.40.1",
        .dst_ip = "32.33.34.35",
        .ipprotocol = 17,
        .sport = 42081,
        .dport = 5001,
    },
    {
        .dmac = "11:22:33:44:55:66",
        .vlan_id = 0,
        .ethertype = 0,
        .ip_version = 4,
        .dst_ip = "192.168.40.1",
        .src_ip = "32.33.34.35",
        .ipprotocol = 17,
        .sport = 5001,
        .dport = 42081,
    },
};


/**
 * @brief: translates reader friendly in_key structure in a net_md_flow_key
 *
 * @param in_key the reader friendly key2net
 * @return a pointer to a net_md_flow_key
 */
static struct net_md_flow_key * in_key2net_md_key(struct in_key *in_key)
{
    struct net_md_flow_key *key;
    int domain, version, ret;
    bool err;

    key = calloc(1, sizeof(*key));
    if (key == NULL) return NULL;

    key->smac = str2os_mac(in_key->smac);
    err = ((in_key->smac != NULL) && (key->smac == NULL));
    if (err) goto err_free_key;

    key->dmac = str2os_mac(in_key->dmac);
    err = ((in_key->dmac != NULL) && (key->dmac == NULL));
    if (err) goto err_free_smac;

    key->vlan_id = in_key->vlan_id;
    key->ethertype = in_key->ethertype;
    key->ip_version = in_key->ip_version;

    version = key->ip_version;
    if ((version != 4) && (version != 6)) return key;

    domain = ((version == 4) ? AF_INET : AF_INET6);

    key->src_ip = calloc(1, sizeof(struct in6_addr));
    if (key->src_ip == NULL) goto err_free_dmac;

    ret = inet_pton(domain, in_key->src_ip, key->src_ip);
    if (ret != 1) goto err_free_src_ip;

    key->dst_ip = calloc(1, sizeof(struct in6_addr));
    if (key->dst_ip == NULL) goto err_free_src_ip;

    ret = inet_pton(domain, in_key->dst_ip, key->dst_ip);
    if (ret != 1) goto err_free_dst_ip;

    key->ipprotocol = in_key->ipprotocol;
    key->sport = htons(in_key->sport);
    key->dport = htons(in_key->dport);

    return key;

err_free_dst_ip:
    free(key->dst_ip);

err_free_src_ip:
    free(key->src_ip);

err_free_dmac:
    free(key->dmac);

err_free_smac:
    free(key->smac);

err_free_key:
    free(key);

    return NULL;
}


/**
 * @brief sets up the global test structure
 *
 * Called in @see @setUp()
 */
void test_net_md_report_setup(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_flow_key *iter_key;
    struct net_md_flow_key **key;
    struct in_key *in_key;
    size_t allocated;
    size_t nelems;
    size_t i;

    g_nd_test.initialized = false;
    g_nd_test.nelems = 0;

    nelems = sizeof(in_keys) / sizeof(struct in_key);
    g_nd_test.net_md_keys = calloc(nelems, sizeof(*key));
    if (g_nd_test.net_md_keys == NULL) return;

    allocated = 0;
    key = g_nd_test.net_md_keys;
    in_key = in_keys;
    for (i = 0; i < nelems; i++)
    {
        *key = in_key2net_md_key(in_key);
        if (*key == NULL) goto err_free_keys;
        allocated++;
        key++;
        in_key++;
    }

    snprintf(g_nd_test.node_id, sizeof(g_nd_test.node_id), "4C718002B3");
    snprintf(g_nd_test.location_id, sizeof(g_nd_test.location_id),
             "59efd33d2c93832025330a3e");
    snprintf(g_nd_test.mqtt_topic, sizeof(g_nd_test.mqtt_topic),
             "dev-test/network_metadata/%s", g_nd_test.node_id);
    g_nd_test.node_info.node_id = g_nd_test.node_id;
    g_nd_test.node_info.location_id = g_nd_test.location_id;
    g_nd_test.initialized = true;
    g_nd_test.nelems = nelems;
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->info = &g_nd_test.node_info;
    aggr_set->num_windows = 1;
    aggr_set->acc_ttl = INT32_MAX;
    aggr_set->report_type = NET_MD_REPORT_ABSOLUTE;
    aggr_set->report_filter = NULL;
    aggr_set->send_report = net_md_send_report;

    return;

err_free_keys:
    key = g_nd_test.net_md_keys;
    for (i = 0; i < allocated; i++)
    {
        iter_key = *key;
        free(iter_key->smac);
        free(iter_key->dmac);
        free(iter_key->src_ip);
        free(iter_key->dst_ip);
        free(iter_key);
        key++;
    }

    memset(&g_nd_test, 0, sizeof(g_nd_test));
    g_nd_test.initialized = false;
}


/**
 * @brief tears down the global test structure
 *
 * Called in @see @tearDown()
 */
void test_net_md_report_teardown(void)
{
    struct net_md_flow_key **key;
    struct net_md_flow_key *iter_key;
    size_t i;

    if (g_nd_test.initialized == false) return;

    key = g_nd_test.net_md_keys;
    for (i = 0; i < g_nd_test.nelems; i++)
    {
        iter_key = *key;
        free(iter_key->smac);
        free(iter_key->dmac);
        free(iter_key->src_ip);
        free(iter_key->dst_ip);
        free(iter_key);
        key++;
    }
    free(g_nd_test.net_md_keys);
    memset(&g_nd_test, 0, sizeof(g_nd_test));
    g_nd_test.initialized = false;
}

/**
 * @brief tests the str2os_mac() utility
 */
void test_str2mac(void)
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
    free(mac);

    /* Validate correct mac in upper case */
    mac = str2os_mac(mac2);
    TEST_ASSERT_NOT_NULL(mac);
    memset(check_mac, 0, sizeof(check_mac));
    snprintf(check_mac, sizeof(check_mac), PRI_os_macaddr_t,
             FMT_os_macaddr_pt(mac));
    cmp = strncmp(check_mac, mac2, sizeof(check_mac));
    TEST_ASSERT_EQUAL_INT(0, cmp);
    free(mac);

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


/**
 * @brief emits a protobuf report
 *
 * Assumes the presenece of QM to send the report on non native platforms,
 * simply resets the aggregator content for native target.
 * @param aggr the aggregator
 */
static void test_emit_report(struct net_md_aggregator *aggr)
{
#ifndef ARCH_X86
    bool ret;

    /* Send the report */
    ret = net_md_send_report(aggr, g_nd_test.mqtt_topic);
    TEST_ASSERT_TRUE(ret);
#else
    struct packed_buffer *pb;
    pb = serialize_flow_report(aggr->report);

    /* Free the serialized container */
    free_packed_buffer(pb);
    net_md_reset_aggregator(aggr);
#endif
}

/**
 * @brief tests the allocation of an aggregator
 *
 * Validates the settings of the aggregator after allocation
 */
void test_net_md_allocate_aggregator(void)
{
    struct net_md_aggregator *aggr;
    struct flow_report *report;

    aggr = net_md_allocate_aggregator(&g_nd_test.aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);
    TEST_ASSERT_NOT_NULL(aggr->report);

    report = aggr->report;
    TEST_ASSERT_EQUAL_STRING(g_nd_test.node_id, report->node_info->node_id);
    TEST_ASSERT_EQUAL_STRING(g_nd_test.location_id,
                             report->node_info->location_id);
    net_md_free_aggregator(aggr);
}

/**
 * @brief validates the content of a search key
 *
 * Obviously needs filling ...
 */
static void validate_net_md_key(struct net_md_flow_key *expected,
                                struct net_md_flow_key *actual)
{
    return;
}

/**
 * @brief validates the content of a flow counter
 *
 * @param expected flow counter stashing expected values
 * @param actual flow counter stashing actual values
 */
static void validate_counters(struct flow_counters *expected,
                              struct flow_counters *actual)
{
    TEST_ASSERT_EQUAL_INT(expected->bytes_count, actual->bytes_count);
    TEST_ASSERT_EQUAL_INT(expected->packets_count, actual->packets_count);
}


/**
 * @brief add sets of (key, counters) to the aggregator's active window
 *
 * Add 2 samples sharing the same key to the aggregator's active window.
 * Expects the targeted accumulator's first counters to match the first sample,
 * and the targeted accumulator's counters to match the last sample
 *
 * @param aggr the aggregator
 * @param key the flow key
 * @param counters the set of counters to be added
 */
static void validate_sampling_one_key(struct net_md_aggregator *aggr,
                                      struct net_md_flow_key *key,
                                      struct flow_counters *counters)
{
    struct net_md_stats_accumulator *acc;
    struct flow_counters *init_counters, *update_counters;
    struct flow_counters zero_counters = { 0 };
    bool ret;

    init_counters = counters++;
    update_counters = counters;

    /* add the init counter */
    ret = net_md_add_sample(aggr, key, init_counters);
    TEST_ASSERT_TRUE(ret);

    /* Validate access and state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    TEST_ASSERT_EQUAL_INT(ACC_STATE_WINDOW_ACTIVE, acc->state);

    /* Validate the accumulator's first counters */
    validate_net_md_key(NULL, NULL);
    LOGD("%s: validating acc %p first counters from first sample", __func__, acc);
    validate_counters(&zero_counters, &acc->first_counters);
    LOGD("%s: validating acc %p counters from first sample", __func__, acc);
    validate_counters(init_counters, &acc->counters);

    /* Add the second sample */
    ret = net_md_add_sample(aggr, key, counters);
    TEST_ASSERT_TRUE(ret);

    /* Validate the accumulator's init and current counters */
    validate_net_md_key(NULL, NULL);
    LOGD("%s: validating acc %p first counters from second sample", __func__, acc);
    validate_counters(&zero_counters, &acc->first_counters);
    LOGD("%s: validating acc %p counters from second sample", __func__, acc);
    validate_counters(update_counters, &acc->counters);
}


/**
 * @brief Validates activate window/add samples/close window/send report
 *
 * Activates the aggregator window, add 2 samples sharing the same key,
 * close the window, send the report.
 *
 * @param aggr the aggregator
 * @param key_idx the global key table reference
 * @param report_type cumulative or absolute report type
 */
static void validate_add_samples_one_key(struct net_md_aggregator *aggr,
                                         size_t key_idx, int report_type)
{
    struct flow_counters counters[2];
    struct flow_counters *init_counters, *update_counters, *counter;
    struct flow_counters report_counters;
    struct net_md_stats_accumulator *acc;
    struct net_md_flow_key *key;
    bool ret;
    size_t act_total_flow_report;

    LOGD("%s: key index: %zu", __func__, key_idx);

    /* Set testing counters */
    counter = counters;
    init_counters = counter++;
    init_counters->bytes_count = 10000;
    init_counters->packets_count = 10;

    update_counters = counter;
    update_counters->bytes_count = 30000;
    update_counters->packets_count = 30;

    /* Activate the aggregator's window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Validate the samples'addition */
    key = g_nd_test.net_md_keys[key_idx];
    validate_sampling_one_key(aggr, key, counters);

    /* Close the active window */
    LOGD("%s: key idx %zu, closing window", __func__, key_idx);
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    LOGD("%s: acc %p", __func__, acc);
    TEST_ASSERT_EQUAL_INT(ACC_STATE_WINDOW_RESET, acc->state);

    /* Validate report counters */
    report_counters = *update_counters;

    LOGD("%s: acc %p init bytes count %" PRIu64
         " bytes count %" PRIu64 " report bytes count %" PRIu64,
         __func__, acc, acc->first_counters.bytes_count, acc->counters.bytes_count,
         acc->report_counters.bytes_count);
    LOGD("%s: validating acc %p report counters", __func__, acc);
    validate_counters(&report_counters, &acc->report_counters);

    /* One flow to be reported */
    act_total_flow_report = net_md_get_total_flows(aggr);
    TEST_ASSERT_EQUAL_INT(1, act_total_flow_report);

    /* Emit the report */
    test_emit_report(aggr);

    /* After report total_flows should be 0 */
    act_total_flow_report = net_md_get_total_flows(aggr);
    TEST_ASSERT_EQUAL_INT(0, act_total_flow_report);
}


/**
 * @brief validates adding 2 samples per key in one active window for all keys
 *
 * Allocates aggregator set with the requested report type, add 2 samples in the
 * the active window for each available and check the counters to be reported.
 */
void activate_add_samples_close_send_report(int report_type)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_aggregator *aggr;
    size_t key_idx;

    TEST_ASSERT_TRUE(g_nd_test.initialized);

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = report_type;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /*
     * For each key, activate window, add 2 samples,
     * close window and send report.
     * Do it in both cumulative and delta modes.
     */
    for (key_idx = 0; key_idx < g_nd_test.nelems; key_idx++)
    {
        LOGD("%s: key idx: %zu, test type %s", __func__, key_idx,
             report_type == NET_MD_REPORT_ABSOLUTE ? "absolute" : "relative");
        validate_add_samples_one_key(aggr, key_idx, report_type);
    }

    /* Free the aggregator */
    net_md_free_aggregator(aggr);
}


/**
 * @brief add samples and send report per global key
 */
void test_activate_add_samples_close_send_report(void)
{
    activate_add_samples_close_send_report(NET_MD_REPORT_ABSOLUTE);
    activate_add_samples_close_send_report(NET_MD_REPORT_RELATIVE);
}


/**
 * @brief add samples for each global key, then send report
 */
void test_add_2_samples_all_keys(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_aggregator *aggr;
    struct flow_counters counters[2];
    struct flow_counters *init_counters, *update_counters, *counter;
    struct flow_counters report_counters;
    struct net_md_flow_key *key;
    size_t key_idx;
    bool ret;

    TEST_ASSERT_TRUE(g_nd_test.initialized);

    counter = counters;
    init_counters = counter++;
    init_counters->bytes_count = 10000;
    init_counters->packets_count = 10;

    update_counters = counter;
    update_counters->bytes_count = 30000;
    update_counters->packets_count = 30;

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add the 2 samples per key */
    for (key_idx = 0; key_idx < g_nd_test.nelems; key_idx++)
    {
        LOGD("%s:%d: key idx: %zu", __func__, __LINE__, key_idx);
        key = g_nd_test.net_md_keys[key_idx];
        validate_sampling_one_key(aggr, key, counters);
    }

    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    report_counters = *update_counters;
    for (key_idx = 0; key_idx < g_nd_test.nelems; key_idx++)
    {
        struct net_md_stats_accumulator *acc;

        LOGD("%s:%d: key idx: %zu", __func__, __LINE__, key_idx);
        key = g_nd_test.net_md_keys[key_idx];
        acc = net_md_lookup_acc(aggr, key);
        TEST_ASSERT_NOT_NULL(acc);
        TEST_ASSERT_EQUAL_INT(ACC_STATE_WINDOW_RESET, acc->state);

        validate_counters(&report_counters, &acc->report_counters);
    }

    /* Emit the report */
    test_emit_report(aggr);

    net_md_free_aggregator(aggr);
}

/**
 * @brief test the aggregation of ethernet samples over multiple reports
 */
void validate_aggregate_one_key(struct net_md_aggregator *aggr, size_t key_idx,
                                struct flow_counters *counters,
                                struct flow_counters *validation_counters)
{
    struct flow_counters *report_1_counters, *report_2_counters, *counter;
    struct net_md_flow_key *key;
    struct net_md_eth_pair *eth_pair;
    struct net_md_stats_accumulator *eth_acc;
    bool ret;

    counter = counters;
    report_1_counters = counter++;
    report_2_counters = counter;

    key = g_nd_test.net_md_keys[key_idx];
    /* Check if the key matches the test */
    ret = is_eth_only(key);
    ret &= (!aggr->report_all_samples);

    TEST_ASSERT_TRUE(ret);

    /* Activate window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add first sample */
    ret = net_md_add_sample(aggr, key, report_1_counters);
    TEST_ASSERT_TRUE(ret);

    /* Close the aggregator window */
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Emit the report */
    test_emit_report(aggr);

    /* Activate window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add second sample */
    ret = net_md_add_sample(aggr, key, report_2_counters);
    TEST_ASSERT_TRUE(ret);

    /* Close the aggregator window */
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Emit the report */
    test_emit_report(aggr);

    eth_pair = net_md_lookup_eth_pair(aggr, key);
    TEST_ASSERT_NOT_NULL(eth_pair);
    eth_acc = eth_pair->mac_stats;
    LOGD("%s: eth acc %p", __func__, eth_acc);
    validate_counters(validation_counters, &eth_acc->report_counters);
}


void test_ethernet_aggregate_one_key(void)
{
    struct net_md_aggregator *absolute_aggr, *relative_aggr;
    struct net_md_aggregator_set *aggr_set;
    struct flow_counters counters[3];
    size_t key_idx;

    TEST_ASSERT_TRUE(g_nd_test.initialized);
    counters[0].bytes_count = 10000; /* First report counters */
    counters[0].packets_count = 100;
    counters[1].bytes_count = 30000; /* Second report counters */
    counters[1].packets_count = 300;
    counters[2].bytes_count = 30000; /* validation counters, absolute mode */
    counters[2].packets_count = 300;
    counters[2].bytes_count = 20000; /* validation counters, relative mode */
    counters[2].packets_count = 200;

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    relative_aggr = net_md_allocate_aggregator(aggr_set);

    aggr_set->report_type = NET_MD_REPORT_ABSOLUTE;
    absolute_aggr = net_md_allocate_aggregator(aggr_set);
    key_idx = 0;
    LOGD("%s: validating absolute aggregation", __func__);
    validate_aggregate_one_key(absolute_aggr, key_idx, counters, &counters[1]);
    LOGD("%s: validating relative aggregation", __func__);
    validate_aggregate_one_key(relative_aggr, key_idx, counters, &counters[2]);

    net_md_free_aggregator(relative_aggr);
    net_md_free_aggregator(absolute_aggr);
}


/**
 * @brief test the aggregation of ethernet samples over multiple reports
 */
void validate_aggregate_two_keys(struct net_md_aggregator *aggr,
                                 struct flow_counters *counters,
                                 struct flow_counters *validation_counters)
{
    struct flow_counters *key0_report_1_counters, *key0_report_2_counters;
    struct flow_counters *key1_report_1_counters;
    struct flow_counters *counter;
    struct net_md_flow_key *key0, *key1;
    struct net_md_eth_pair *eth_pair;
    struct net_md_stats_accumulator *eth_acc;
    bool ret;
    size_t act_total_flow_report;

    counter = counters;
    key0_report_1_counters = counter++;
    key1_report_1_counters = counter++;
    key0_report_2_counters = counter++;

    key0 = g_nd_test.net_md_keys[0];
    /* Check if the key matches the test */
    ret = is_eth_only(key0);
    ret &= (!aggr->report_all_samples);
    TEST_ASSERT_TRUE(ret);

    key1 = g_nd_test.net_md_keys[1];
    /* Check if the key matches the test */
    ret = is_eth_only(key1);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the keys belong to the same ethernet pair */
    eth_pair = net_md_lookup_eth_pair(aggr, key0);
    TEST_ASSERT_NOT_NULL(eth_pair);
    TEST_ASSERT_TRUE(eth_pair == net_md_lookup_eth_pair(aggr, key1));
    eth_acc = eth_pair->mac_stats;
    LOGD("%s: eth acc %p", __func__, eth_acc);

    /* Activate window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add first samples */
    ret = net_md_add_sample(aggr, key0, key0_report_1_counters);
    TEST_ASSERT_TRUE(ret);

    ret = net_md_add_sample(aggr, key1, key1_report_1_counters);
    TEST_ASSERT_TRUE(ret);

    /* Close the aggregator window */
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* One flow to be reported */
    act_total_flow_report = net_md_get_total_flows(aggr);
    TEST_ASSERT_EQUAL_INT(1, act_total_flow_report);

    /* Emit the report */
    test_emit_report(aggr);

    /* After report total_flows should be 0 */
    act_total_flow_report = net_md_get_total_flows(aggr);
    TEST_ASSERT_EQUAL_INT(0, act_total_flow_report);

    validate_counters(validation_counters, &eth_acc->report_counters);
    validation_counters++;

    /* Activate window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add second sample */
    ret = net_md_add_sample(aggr, key0, key0_report_2_counters);
    TEST_ASSERT_TRUE(ret);

    /* Close the aggregator window */
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* One flow to be reported */
    act_total_flow_report = net_md_get_total_flows(aggr);
    TEST_ASSERT_EQUAL_INT(1, act_total_flow_report);

    /* Emit the report */
    test_emit_report(aggr);

    /* After report total_flows should be 0 */
    act_total_flow_report = net_md_get_total_flows(aggr);
    TEST_ASSERT_EQUAL_INT(0, act_total_flow_report);

    validate_counters(validation_counters, &eth_acc->report_counters);
}

/**
 * @brief validates reports generated from 2 keys from the same eth pair.
 *
 * The ethernet only keys carry a different ethertype and
 * their counters are tracked in different accumulators.
 * The test validates the accuracy of the aggregated ethernet pair counters.
 */
void test_ethernet_aggregate_two_keys(void)
{
    struct net_md_aggregator *absolute_aggr, *relative_aggr;
    struct net_md_aggregator_set *aggr_set;
    struct flow_counters counters[7];

    TEST_ASSERT_TRUE(g_nd_test.initialized);
    counters[0].bytes_count = 10000; /* Key 0, First report counters */
    counters[0].packets_count = 100;
    counters[1].bytes_count = 30000; /* Key 1, First report counters */
    counters[1].packets_count = 300;
    counters[2].bytes_count = 50000; /* Key 0, Second report counters */
    counters[2].packets_count = 500;

    /*
     * First report counters, absolute mode.
     * Received a total of 10000 bytes for key 0, 30000 bytes for key 1.
     */
    counters[3].bytes_count = 40000;
    counters[3].packets_count = 400;

    /*
     * Second report counters, absolute mode.
     * Received a total of 50000 bytes for key 0, 30000 bytes for key 1.
     */
    counters[4].bytes_count = 80000;
    counters[4].packets_count = 800;

    /*
     * First report counters, relative mode.
     * First report, so previous report values a zeros.
     */
    counters[5].bytes_count = 40000;
    counters[5].packets_count = 400;


    /*
     * Second report counters, relative mode.
     * Difference between the absolute values of first and second report
     */
    counters[6].bytes_count = 40000;
    counters[6].packets_count = 400;

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    relative_aggr = net_md_allocate_aggregator(aggr_set);
    aggr_set->report_type = NET_MD_REPORT_ABSOLUTE;
    absolute_aggr = net_md_allocate_aggregator(aggr_set);

    LOGD("%s: validating absolute aggregation", __func__);
    validate_aggregate_two_keys(absolute_aggr, counters, &counters[3]);
    LOGD("%s: validating relative aggregation", __func__);
    validate_aggregate_two_keys(relative_aggr, counters, &counters[5]);

    net_md_free_aggregator(relative_aggr);
    net_md_free_aggregator(absolute_aggr);
}


/**
 * @brief add samples for each global key, then send report
 */
void test_large_loop(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct flow_counters counters[20];
    struct net_md_aggregator *aggr;
    struct net_md_flow_key *key;
    size_t key_idx, i, n, ncnt, ntimes;
    bool ret;

    TEST_ASSERT_TRUE(g_nd_test.initialized);

    /* Set up counters */
    ncnt = sizeof(counters) / sizeof(counters[0]);
    for (i = 0; i < ncnt; i++)
    {
        counters[i].packets_count = 100 + i * 100;
        counters[i].bytes_count = 10000 + i * 2000;
    }

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    ntimes = 60;
    for (n = 0; n < ntimes; n++)
    {
        /* Activate aggregator window */
        ret = net_md_activate_window(aggr);
        TEST_ASSERT_TRUE(ret);

        for (key_idx = 0; key_idx < g_nd_test.nelems; key_idx++)
        {
            key = g_nd_test.net_md_keys[key_idx];
            for (i = 0; i < ncnt; i++)
            {
                counters[i].packets_count += (i * 100 + key_idx * 10 + n * 1000);
                counters[i].bytes_count += (i * 2000 + key_idx * 100 + n * 10000);
                ret = net_md_add_sample(aggr, key, &counters[i]);
            }
        }

        /* Close the aggregator window */
        ret = net_md_close_active_window(aggr);
        TEST_ASSERT_TRUE(ret);

        /* Emit the report */
        test_emit_report(aggr);
    }

    /* Free aggregator */
    net_md_free_aggregator(aggr);
}


/**
 * @brief adding and removing a flow from the aggregator
 */
void test_add_remove_flows(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct flow_counters counters[20];
    struct net_md_aggregator *aggr;
    struct net_md_flow_key *key;
    size_t key_idx, i, n, ntimes, ncnt;
    bool ret;

    TEST_ASSERT_TRUE(g_nd_test.initialized);

    /* Set up counters */
    ncnt = sizeof(counters) / sizeof(counters[0]);
    for (i = 0; i < ncnt; i++)
    {
        counters[i].packets_count = 100 + i * 100;
        counters[i].bytes_count = 10000 + i * 2000;
    }

    /* Allocate aggregator with a time-to-live set to 1 second */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr_set->acc_ttl = 1;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    ntimes = 4;
    for (n = 0; n < ntimes; n++)
    {
        /* Populate all flows but the last one */
        for (key_idx = 0; key_idx < g_nd_test.nelems - 1; key_idx++)
        {
            key = g_nd_test.net_md_keys[key_idx];
            for (i = 0; i < ncnt; i++)
            {
                counters[i].packets_count += (i * 100 + key_idx * 10 + n * 1000);
                counters[i].bytes_count += (i * 2000 + key_idx * 100 + n * 10000);
                ret = net_md_add_sample(aggr, key, &counters[i]);
            }
        }

        /* Close the aggregator window */
        ret = net_md_close_active_window(aggr);
        TEST_ASSERT_TRUE(ret);

        /* Emit the report */
        test_emit_report(aggr);

        /* Sleep 2 seconds  and polulate the last flow */
        sleep(2);

        key_idx = g_nd_test.nelems - 1;
        key = g_nd_test.net_md_keys[key_idx];
        for (i = 0; i < ncnt; i++)
        {
            counters[i].packets_count += (i * 100 + key_idx * 10 + n * 1000);
            counters[i].bytes_count += (i * 2000 + key_idx * 100 + n * 10000);
            ret = net_md_add_sample(aggr, key, &counters[i]);
        }

        /* Close the aggregator window */
        ret = net_md_close_active_window(aggr);
        TEST_ASSERT_TRUE(ret);

        /* Emit the report */
        test_emit_report(aggr);
    }

    /* Free aggregator */
    net_md_free_aggregator(aggr);
}


/**
 * @brief add samples for each global key in multiple windows
 */
void test_multiple_windows(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct flow_counters counters[20];
    struct net_md_aggregator *aggr;
    struct net_md_flow_key *key;
    size_t key_idx, i, n, ncnt;
    bool ret;

    TEST_ASSERT_TRUE(g_nd_test.initialized);

    /* Set up counters */
    ncnt = sizeof(counters) / sizeof(counters[0]);
    for (i = 0; i < ncnt; i++)
    {
        counters[i].packets_count = 100 + i * 100;
        counters[i].bytes_count = 10000 + i * 2000;
    }

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->num_windows = g_nd_test.nelems;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    n = 0;
    for (key_idx = 0; key_idx < g_nd_test.nelems; key_idx++)
    {
        /* Activate aggregator window */
        ret = net_md_activate_window(aggr);
        TEST_ASSERT_TRUE(ret);

        key = g_nd_test.net_md_keys[key_idx];
        for (i = 0; i < ncnt; i++)
        {
            counters[i].packets_count += (i * 100 + key_idx * 10 + n * 1000);
            counters[i].bytes_count += (i * 2000 + key_idx * 100 + n * 10000);
            ret = net_md_add_sample(aggr, key, &counters[i]);
        }
        /* Close the aggregator window */
        ret = net_md_close_active_window(aggr);
        TEST_ASSERT_TRUE(ret);

        n++;
    }

    /* Emit the report */
    test_emit_report(aggr);

    /* Free aggregator */
    net_md_free_aggregator(aggr);
}

/**
 * @brief filter sample to report based on source mac address presence and value
 *
 * Returns true if the accumulator smac matched in_key[8].smac,
 * unique to the set of provisioned keys for the UT.
 *
 * @param acc the the flow accumulator to filter
 */
bool report_filter(struct net_md_stats_accumulator *acc)
{
    char *smac;
    int cmp;

    TEST_ASSERT_NOT_NULL(acc);
    TEST_ASSERT_NOT_NULL(acc->fkey);

    smac = acc->fkey->smac;
    if (smac == NULL) return false;

    cmp = memcmp(smac, in_keys[8].smac, strlen(smac));
    if (cmp != 0) return false;

    return true;
}

void test_report_filter(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct flow_counters counters[20];
    struct net_md_aggregator *aggr;
    struct net_md_flow_key *key;
    struct flow_window *window;
    struct flow_stats *stats;
    size_t key_idx, i, ncnt;
    char *smac;
    bool ret;
    int cmp;

    TEST_ASSERT_TRUE(g_nd_test.initialized);

    /* Set up counters */
    ncnt = sizeof(counters) / sizeof(counters[0]);
    for (i = 0; i < ncnt; i++)
    {
        counters[i].packets_count = 100 + i * 100;
        counters[i].bytes_count = 10000 + i * 2000;
    }

    /* Allocate aggregator, set a report_filter routine */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr_set->report_filter = report_filter;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    for (key_idx = 0; key_idx < g_nd_test.nelems; key_idx++)
    {
        key = g_nd_test.net_md_keys[key_idx];
        for (i = 0; i < ncnt; i++)
        {
            counters[i].packets_count += (i * 100 + key_idx * 10);
            counters[i].bytes_count += (i * 2000 + key_idx * 100);
            ret = net_md_add_sample(aggr, key, &counters[i]);
        }
    }

    /* Get a handle on the active window before closing it */
    window = net_md_active_window(aggr);

    /* Close the aggregator window */
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Validate the number of reported flow stats in the window */
    TEST_ASSERT_EQUAL_INT(1, window->num_stats);
    stats = window->flow_stats[0];
    smac = stats->key->smac;
    TEST_ASSERT_NOT_NULL(smac);
    cmp = memcmp(smac, in_keys[8].smac, strlen(smac));
    TEST_ASSERT_EQUAL_INT(0, cmp);

    /* Emit the report */
    test_emit_report(aggr);

    /* Free aggregator */
    net_md_free_aggregator(aggr);
}


void test_activate_and_free_aggr(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_aggregator *aggr;
    bool ret;

    TEST_ASSERT_TRUE(g_nd_test.initialized);

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Free aggregator */
    net_md_free_aggregator(aggr);
}

/**
 * @brief Variant of adding and removing a flow from the aggregator
 *
 * Request a sample TTL lower than the window activate/close span.
 * Expect the flows to be reported.
 */
void test_bogus_ttl(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_aggregator *aggr;
    struct flow_counters counter;
    struct net_md_flow_key *key;
    struct flow_window *window;
    bool ret;

    TEST_ASSERT_TRUE(g_nd_test.initialized);

    /* Allocate aggregator with a time-to-live set to 1 second */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->acc_ttl = 1;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add the sample */
    key = g_nd_test.net_md_keys[3];
    counter.packets_count =  1000;
    counter.bytes_count = 10000;
    ret = net_md_add_sample(aggr, key, &counter);

    /* Sleep 2 seconds */
    sleep(2);

    /* Get a handle on the active window before closing it */
    window = net_md_active_window(aggr);

    /* Close the aggregator window */
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Validate the number of reported flow stats in the window */
    TEST_ASSERT_EQUAL_INT(1, window->num_stats);

    /* Emit the report */
    test_emit_report(aggr);

    /* Free aggregator */
    net_md_free_aggregator(aggr);
}

/**
 * @brief add a flow_tag to a key
 */
void
test_flow_tags_one_key(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_stats_accumulator *acc;
    struct flow_counters counters[1];
    struct net_md_aggregator *aggr;
    struct net_md_flow_key *key;
    struct flow_tags **tags;
    struct flow_tags *tag;
    struct flow_key *fkey;
    bool ret;

    TEST_ASSERT_TRUE(g_nd_test.initialized);
    counters[0].bytes_count = 10000;
    counters[0].packets_count = 100;

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add one sample */
    key = g_nd_test.net_md_keys[5];
    ret = net_md_add_sample(aggr, key, counters);
    TEST_ASSERT_TRUE(ret);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Add a flow tag to the key */
    fkey->num_tags = 1;
    fkey->tags = calloc(fkey->num_tags, sizeof(*fkey->tags));
    TEST_ASSERT_NOT_NULL(fkey->tags);

    tag = calloc(1, sizeof(*tag));
    TEST_ASSERT_NOT_NULL(tag);

    tag->vendor = strdup("Plume");
    TEST_ASSERT_NOT_NULL(tag->vendor);

    tag->app_name = strdup("Plume App");
    TEST_ASSERT_NOT_NULL(tag->app_name);

    tag->nelems = 2;
    tag->tags = calloc(tag->nelems, sizeof(tags));
    TEST_ASSERT_NOT_NULL(tag->tags);

    tag->tags[0] = strdup("Plume Tag0");
    TEST_ASSERT_NOT_NULL(tag->tags[0]);

    tag->tags[1] = strdup("Plume Tag1");
    TEST_ASSERT_NOT_NULL(tag->tags[1]);

    *(fkey->tags) = tag;

    /* Close the active window */
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Emit the report */
    test_emit_report(aggr);

    /* Free aggregator */
    net_md_free_aggregator(aggr);
}


void
test_vendor_data_one_key(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_stats_accumulator *acc;
    struct flow_counters counters[1];
    struct net_md_aggregator *aggr;
    struct flow_vendor_data *vd1;
    struct flow_vendor_data *vd2;
    struct net_md_flow_key *key;
    struct flow_key *fkey;
    bool ret;

    struct vendor_data_kv_pair vd1_kps[] =
    {
        {
            .key = "vd1_key1",
            .value_type = NET_VENDOR_STR,
            .str_value = "vd1_key1_val1",
        },
        {
            .key = "vd1_key2",
            .value_type = NET_VENDOR_U32,
            .u32_value = 12345,
        },
        {
            .key = "vd1_key3",
            .value_type = NET_VENDOR_U64,
            .u64_value = 12345678,
        },
    };

    struct vendor_data_kv_pair vd2_kps[] =
    {
        {
            .key = "vd2_key1",
            .value_type = NET_VENDOR_STR,
            .str_value = "vd2_key1_val1",
        },
        {
            .key = "vd2_key2",
            .value_type = NET_VENDOR_U32,
            .u32_value = 54321,
        },
        {
            .key = "vd2_key3",
            .value_type = NET_VENDOR_U64,
            .u64_value = 87654321,
        },
    };

    struct vendor_data_kv_pair **kvps1;
    struct vendor_data_kv_pair **kvps2;
    struct vendor_data_kv_pair *kvp;
    size_t nelems;
    size_t i;

    TEST_ASSERT_TRUE(g_nd_test.initialized);
    counters[0].bytes_count = 10000;
    counters[0].packets_count = 100;

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add one sample */
    key = g_nd_test.net_md_keys[5];
    ret = net_md_add_sample(aggr, key, counters);
    TEST_ASSERT_TRUE(ret);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Add vendor data to the key */
    fkey->num_vendor_data = 2;
    fkey->vdr_data = calloc(fkey->num_vendor_data,
                            sizeof(*fkey->vdr_data));
    TEST_ASSERT_NOT_NULL(fkey->vdr_data);

    nelems = 3;

    kvps1 = calloc(nelems, sizeof(struct vendor_data_kv_pair *));
    TEST_ASSERT_NOT_NULL(kvps1);
    for (i = 0; i < nelems; i++)
    {
        kvps1[i] = calloc(1, sizeof(struct vendor_data_kv_pair));
        kvp = kvps1[i];
        TEST_ASSERT_NOT_NULL(kvp);
        kvp->key = strdup(vd1_kps[i].key);
        if (vd1_kps[i].value_type == NET_VENDOR_STR)
        {
            kvp->value_type = NET_VENDOR_STR;
            kvp->str_value = strdup(vd1_kps[i].str_value);
            TEST_ASSERT_NOT_NULL(kvp->str_value);
        }
        else if (vd1_kps[i].value_type == NET_VENDOR_U32)
        {
            kvp->value_type = NET_VENDOR_U32;
            kvp->u32_value = vd1_kps[i].u32_value;
        }
        else
        {
            kvp->value_type = NET_VENDOR_U64;
            kvp->u64_value = vd1_kps[i].u64_value;
        }
        kvp++;
    }
    vd1 = calloc(1, sizeof(struct flow_vendor_data));
    vd1->vendor = strdup("vendor1");
    TEST_ASSERT_NOT_NULL(vd1->vendor);
    vd1->nelems = 3;
    vd1->kv_pairs = kvps1;

    kvps2 = calloc(nelems, sizeof(struct vendor_data_kv_pair *));
    TEST_ASSERT_NOT_NULL(kvps2);
    for (i = 0; i < nelems; i++)
    {
        kvps2[i] = calloc(1, sizeof(struct vendor_data_kv_pair));
        kvp = kvps2[i];
        TEST_ASSERT_NOT_NULL(kvp);
        kvp->key = strdup(vd2_kps[i].key);
        if (vd2_kps[i].value_type == NET_VENDOR_STR)
        {
            kvp->str_value = strdup(vd2_kps[i].str_value);
            TEST_ASSERT_NOT_NULL(kvp->str_value);
        }
        else if (vd2_kps[i].value_type == NET_VENDOR_U32)
        {
            kvp->value_type = NET_VENDOR_U32;
            kvp->u32_value = vd2_kps[i].u32_value;
        }
        else
        {
            kvp->value_type = NET_VENDOR_U64;
            kvp->u64_value = vd2_kps[i].u64_value;
        }
        kvp++;
    }
    vd2 = calloc(1, sizeof(struct flow_vendor_data));
    vd2->vendor = strdup("vendor2");
    TEST_ASSERT_NOT_NULL(vd2->vendor);
    vd2->nelems = 3;
    vd2->kv_pairs = kvps2;

    fkey->vdr_data[0] = vd1;
    fkey->vdr_data[1] = vd2;
    fkey->num_vendor_data = 2;

    /* Close the active window */
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Emit the report */
    test_emit_report(aggr);

    /* Free aggregator */
    net_md_free_aggregator(aggr);
}

void
test_flow_key_to_net_md_key(void)
{
    Traffic__FlowKey pb_keys[] =
        {
            {    /* 0 */
                .srcmac = "11:22:33:44:55:66",
                .dstmac = "dd:ee:ff:cc:bb:aa",
                .vlanid = 0,
                .ethertype = 0,
            },
            {    /* 1 */
                .srcmac = "11:22:33:44:55:66",
                .dstmac = "dd:ee:ff:cc:bb:aa",
                .vlanid = 0,
                .ethertype = 1,
            },
            {    /* 2 */
                .srcmac = "11:22:33:44:55:66",
                .dstmac = "dd:ee:ff:cc:bb:aa",
                .vlanid = 1,
                .ethertype = 0x8000,
            },
            {   /* 3 */
                .srcip = "192.168.40.1",
                .dstip = "32.33.34.35",
                .ipprotocol = 2,
            },
            {    /* 4 */
                .srcip = "192.168.40.1",
                .dstip = "32.33.34.35",
                .ipprotocol = 17,
                .tptsrcport = 36000,
                .tptdstport = 1234,
            },
            {   /* 5 */
                .srcmac = "11:22:33:44:55:66",
                .dstmac = "dd:ee:ff:cc:bb:aa",
                .vlanid = 0,
                .ethertype = 0,
                .srcip = "::1",
                .dstip = "fe80::42:dbff:fe68:586",
                .ipprotocol = 17,
                .tptsrcport = 12346,
                .tptdstport = 53,
            },
        };

    struct net_md_aggregator aggr;
    struct net_md_flow_key *key;
    Traffic__FlowKey *pb_key;
    const char *rcs;
    char str[256];
    int domain;
    int rc;

    memset(&aggr, 0, sizeof(aggr));
    /* Validate key 0 translation */
    pb_key = &pb_keys[0];
    key = pbkey2net_md_key(&aggr, pb_key);
    TEST_ASSERT_NOT_NULL(key);

    /* Validate source mac */
    snprintf(str, sizeof(str), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(key->smac));
    rc = strcmp(str, pb_key->srcmac);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Validate destination mac */
    snprintf(str, sizeof(str), PRI_os_macaddr_lower_t,
             FMT_os_macaddr_pt(key->dmac));
    rc = strcmp(str, pb_key->dstmac);
    TEST_ASSERT_EQUAL_INT(0, rc);

    free_net_md_flow_key(key);

    /* Validate key 1 translation */
    pb_key = &pb_keys[1];
    key = pbkey2net_md_key(&aggr, pb_key);
    TEST_ASSERT_NOT_NULL(key);

    /* validate vlan id */
    TEST_ASSERT_EQUAL_INT(pb_key->vlanid, key->vlan_id);

    /* validate ethertype */
    TEST_ASSERT_EQUAL_UINT(pb_key->ethertype, key->ethertype);

    free_net_md_flow_key(key);

    /* Validate key 2 translation */
    pb_key = &pb_keys[2];
    key = pbkey2net_md_key(&aggr, pb_key);
    TEST_ASSERT_NOT_NULL(key);

    /* validate vlan id */
    TEST_ASSERT_EQUAL_INT(pb_key->vlanid, key->vlan_id);

    /* validate ethertype */
    TEST_ASSERT_EQUAL_UINT(pb_key->ethertype, key->ethertype);

    free_net_md_flow_key(key);

    /* Validate key 3 translation */
    pb_key = &pb_keys[3];
    key = pbkey2net_md_key(&aggr, pb_key);
    TEST_ASSERT_NOT_NULL(key);

    /* Validate source mac */
    TEST_ASSERT_NULL(key->smac);

    /* Validate destination mac */
    TEST_ASSERT_NULL(key->dmac);

    /* Validate ip version */
    TEST_ASSERT_EQUAL_INT(4, key->ip_version);

    /* Validate source ip */
    domain = (key->ip_version == 4 ? AF_INET : AF_INET6);
    rcs = inet_ntop(domain, key->src_ip, str, sizeof(str));
    if (rcs == NULL) LOGI("%s: errno: %s", __func__, strerror(errno));
    TEST_ASSERT_NOT_NULL(rcs);

    rc = strcmp(str, pb_key->srcip);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Validate destination ip */
    rcs = inet_ntop(domain, key->dst_ip, str, sizeof(str));
    if (rcs == NULL) LOGI("%s: errno: %s", __func__, strerror(errno));
    TEST_ASSERT_NOT_NULL(rcs);
    rc = strcmp(str, pb_key->dstip);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Validate ip protocol */
    TEST_ASSERT_EQUAL_INT(pb_key->ipprotocol, key->ipprotocol);

    free_net_md_flow_key(key);

    /* Validate key 4 translation */
    pb_key = &pb_keys[4];
    key = pbkey2net_md_key(&aggr, pb_key);
    TEST_ASSERT_NOT_NULL(key);

    /* Validate ip version */
    TEST_ASSERT_EQUAL_INT(4, key->ip_version);

    /* Validate ip protocol */
    TEST_ASSERT_EQUAL_INT(pb_key->ipprotocol, key->ipprotocol);

    /* Validate source port */
    TEST_ASSERT_EQUAL_UINT(pb_key->tptsrcport, ntohs(key->sport));

    /* Validate destination port */
    TEST_ASSERT_EQUAL_UINT(pb_key->tptdstport, ntohs(key->dport));

    free_net_md_flow_key(key);

    /* Validate key 5 translation */
    pb_key = &pb_keys[5];
    key = pbkey2net_md_key(&aggr, pb_key);
    TEST_ASSERT_NOT_NULL(key);

    /* Validate ip version */
    TEST_ASSERT_EQUAL_INT(6, key->ip_version);

    /* Validate source ip */
    domain = (key->ip_version == 4 ? AF_INET : AF_INET6);
    rcs = inet_ntop(domain, key->src_ip, str, sizeof(str));
    if (rcs == NULL) LOGI("%s: errno: %s", __func__, strerror(errno));
    TEST_ASSERT_NOT_NULL(rcs);
    rc = strcmp(str, pb_key->srcip);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Validate destination ip */
    domain = (key->ip_version == 4 ? AF_INET : AF_INET6);
    rcs = inet_ntop(domain, key->dst_ip, str, sizeof(str));
    if (rcs == NULL) LOGI("%s: errno: %s", __func__, strerror(errno));
    TEST_ASSERT_NOT_NULL(rcs);
    rc = strcmp(str, pb_key->dstip);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Validate ip protocol */
    TEST_ASSERT_EQUAL_INT(pb_key->ipprotocol, key->ipprotocol);

    free_net_md_flow_key(key);
}


void
test_vendor_data_serialize_deserialize(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_stats_accumulator *acc;
    struct flow_counters counters[1];
    struct net_md_aggregator *aggr_out;
    struct net_md_aggregator *aggr_in;
    struct flow_vendor_data *vd1;
    struct flow_vendor_data *vd2;
    struct net_md_flow_key *key;
    struct flow_key *fkey;
    bool ret;

    struct vendor_data_kv_pair vd1_kps[] =
    {
        {
            .key = "vd1_key1",
            .value_type = NET_VENDOR_STR,
            .str_value = "vd1_key1_val1",
        },
        {
            .key = "vd1_key2",
            .value_type = NET_VENDOR_U32,
            .u32_value = 12345,
        },
        {
            .key = "vd1_key3",
            .value_type = NET_VENDOR_U64,
            .u64_value = 12345678,
        },
    };

    struct vendor_data_kv_pair vd2_kps[] =
    {
        {
            .key = "vd2_key1",
            .value_type = NET_VENDOR_STR,
            .str_value = "vd2_key1_val1",
        },
        {
            .key = "vd2_key2",
            .value_type = NET_VENDOR_U32,
            .u32_value = 54321,
        },
        {
            .key = "vd2_key3",
            .value_type = NET_VENDOR_U64,
            .u64_value = 87654321,
        },
    };
    struct vendor_data_kv_pair **kvps1;
    struct vendor_data_kv_pair **kvps2;
    struct vendor_data_kv_pair *kvp;
    struct packed_buffer recv_pb;
    struct packed_buffer *pb;
    struct flow_tags **tags;
    struct flow_tags *tag;
    size_t nelems;
    size_t i;

    TEST_ASSERT_TRUE(g_nd_test.initialized);
    counters[0].bytes_count = 10000;
    counters[0].packets_count = 100;

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr_in = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr_in);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr_in);
    TEST_ASSERT_TRUE(ret);

    /* Add one sample */
    key = g_nd_test.net_md_keys[5];
    ret = net_md_add_sample(aggr_in, key, counters);
    TEST_ASSERT_TRUE(ret);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr_in, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Add vendor data to the key */
    fkey->num_vendor_data = 2;
    fkey->vdr_data = calloc(fkey->num_vendor_data,
                            sizeof(*fkey->vdr_data));
    TEST_ASSERT_NOT_NULL(fkey->vdr_data);

    nelems = 3;

    kvps1 = calloc(nelems, sizeof(struct vendor_data_kv_pair *));
    TEST_ASSERT_NOT_NULL(kvps1);
    for (i = 0; i < nelems; i++)
    {
        kvps1[i] = calloc(1, sizeof(struct vendor_data_kv_pair));
        kvp = kvps1[i];
        TEST_ASSERT_NOT_NULL(kvp);
        kvp->key = strdup(vd1_kps[i].key);
        if (vd1_kps[i].value_type == NET_VENDOR_STR)
        {
            kvp->value_type = NET_VENDOR_STR;
            kvp->str_value = strdup(vd1_kps[i].str_value);
            TEST_ASSERT_NOT_NULL(kvp->str_value);
        }
        else if (vd1_kps[i].value_type == NET_VENDOR_U32)
        {
            kvp->value_type = NET_VENDOR_U32;
            kvp->u32_value = vd1_kps[i].u32_value;
        }
        else
        {
            kvp->value_type = NET_VENDOR_U64;
            kvp->u64_value = vd1_kps[i].u64_value;
        }
        kvp++;
    }
    vd1 = calloc(1, sizeof(struct flow_vendor_data));
    vd1->vendor = strdup("vendor1");
    TEST_ASSERT_NOT_NULL(vd1->vendor);
    vd1->nelems = 3;
    vd1->kv_pairs = kvps1;

    kvps2 = calloc(nelems, sizeof(struct vendor_data_kv_pair *));
    TEST_ASSERT_NOT_NULL(kvps2);
    for (i = 0; i < nelems; i++)
    {
        kvps2[i] = calloc(1, sizeof(struct vendor_data_kv_pair));
        kvp = kvps2[i];
        TEST_ASSERT_NOT_NULL(kvp);
        kvp->key = strdup(vd2_kps[i].key);
        if (vd2_kps[i].value_type == NET_VENDOR_STR)
        {
            kvp->str_value = strdup(vd2_kps[i].str_value);
            TEST_ASSERT_NOT_NULL(kvp->str_value);
        }
        else if (vd2_kps[i].value_type == NET_VENDOR_U32)
        {
            kvp->value_type = NET_VENDOR_U32;
            kvp->u32_value = vd2_kps[i].u32_value;
        }
        else
        {
            kvp->value_type = NET_VENDOR_U64;
            kvp->u64_value = vd2_kps[i].u64_value;
        }
        kvp++;
    }
    vd2 = calloc(1, sizeof(struct flow_vendor_data));
    vd2->vendor = strdup("vendor2");
    TEST_ASSERT_NOT_NULL(vd2->vendor);
    vd2->nelems = 3;
    vd2->kv_pairs = kvps2;

    fkey->vdr_data[0] = vd1;
    fkey->vdr_data[1] = vd2;
    fkey->num_vendor_data = 2;

    /* Add a flow tag to the key */
    fkey->num_tags = 1;
    fkey->tags = calloc(fkey->num_tags, sizeof(*fkey->tags));
    TEST_ASSERT_NOT_NULL(fkey->tags);

    tag = calloc(1, sizeof(*tag));
    TEST_ASSERT_NOT_NULL(tag);

    tag->vendor = strdup("Plume");
    TEST_ASSERT_NOT_NULL(tag->vendor);

    tag->app_name = strdup("Plume App");
    TEST_ASSERT_NOT_NULL(tag->app_name);

    tag->nelems = 2;
    tag->tags = calloc(tag->nelems, sizeof(tags));
    TEST_ASSERT_NOT_NULL(tag->tags);

    tag->tags[0] = strdup("Plume Tag0");
    TEST_ASSERT_NOT_NULL(tag->tags[0]);

    tag->tags[1] = strdup("Plume Tag1");
    TEST_ASSERT_NOT_NULL(tag->tags[1]);

    *(fkey->tags) = tag;

    /* Close the active window */
    ret = net_md_close_active_window(aggr_in);
    TEST_ASSERT_TRUE(ret);

    /* Prepare the transfer to the receiving aggregator */
    pb = serialize_flow_report(aggr_in->report);

    recv_pb.len = pb->len;
    recv_pb.buf = pb->buf;

    test_emit_report(aggr_in);

    /* Allocate the receiving aggregator */
    aggr_out = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr_out);

    /* Activate the receiving aggregator window */
    ret = net_md_activate_window(aggr_out);
    TEST_ASSERT_TRUE(ret);

    /* Transfer tags and vendor data */
    net_md_update_aggr(aggr_out, &recv_pb);

    /* Free the serialized container */
    free_packed_buffer(pb);
    ret = net_md_close_active_window(aggr_out);

    test_emit_report(aggr_out);
    /* Free aggregators */
    net_md_free_aggregator(aggr_in);
    net_md_free_aggregator(aggr_out);
}


/**
 * @brief add a flow_tag to a key
 */
void
test_update_flow_tags(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_stats_accumulator *acc;
    struct net_md_aggregator *alt_aggr;
    struct flow_counters counters[1];
    struct net_md_aggregator *aggr;
    struct net_md_flow_key *key;
    struct packed_buffer *pb;
    struct flow_tags **tags;
    struct flow_tags *tag;
    struct flow_key *fkey;
    bool ret;

    TEST_ASSERT_TRUE(g_nd_test.initialized);
    counters[0].bytes_count = 10000;
    counters[0].packets_count = 100;

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add one sample */
    key = g_nd_test.net_md_keys[5];
    ret = net_md_add_sample(aggr, key, counters);
    TEST_ASSERT_TRUE(ret);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Add a flow tag to the key */
    fkey->num_tags = 1;
    fkey->tags = calloc(fkey->num_tags, sizeof(*fkey->tags));
    TEST_ASSERT_NOT_NULL(fkey->tags);

    tag = calloc(1, sizeof(*tag));
    TEST_ASSERT_NOT_NULL(tag);

    tag->vendor = strdup("Plume");
    TEST_ASSERT_NOT_NULL(tag->vendor);

    tag->app_name = strdup("Plume App");
    TEST_ASSERT_NOT_NULL(tag->app_name);

    tag->nelems = 2;
    tag->tags = calloc(tag->nelems, sizeof(tags));
    TEST_ASSERT_NOT_NULL(tag->tags);

    tag->tags[0] = strdup("Plume Tag0");
    TEST_ASSERT_NOT_NULL(tag->tags[0]);

    tag->tags[1] = strdup("Plume Tag1");
    TEST_ASSERT_NOT_NULL(tag->tags[1]);

    *(fkey->tags) = tag;

    /* Close the active window */
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Keep access to the protobuf to test adding the same flow tag */
    pb = serialize_flow_report(aggr->report);
    net_md_reset_aggregator(aggr);

    net_md_update_aggr(aggr, pb);

    /* Free the serialized container */
    free_packed_buffer(pb);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Validate the number of flow tags */
    TEST_ASSERT_EQUAL_INT(1, fkey->num_tags);

    /*
     * Allocate alternative aggregator.
     * Same settings as the original aggregator, different vendor for flow tags.
     */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    alt_aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(alt_aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(alt_aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add one sample */
    key = g_nd_test.net_md_keys[5];
    ret = net_md_add_sample(alt_aggr, key, counters);
    TEST_ASSERT_TRUE(ret);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(alt_aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Add a flow tag to the key */
    fkey->num_tags = 1;
    fkey->tags = calloc(fkey->num_tags, sizeof(*fkey->tags));
    TEST_ASSERT_NOT_NULL(fkey->tags);

    tag = calloc(1, sizeof(*tag));
    TEST_ASSERT_NOT_NULL(tag);

    tag->vendor = strdup("NotPlume");
    TEST_ASSERT_NOT_NULL(tag->vendor);

    tag->app_name = strdup("NotPlume App");
    TEST_ASSERT_NOT_NULL(tag->app_name);

    tag->nelems = 2;
    tag->tags = calloc(tag->nelems, sizeof(tags));
    TEST_ASSERT_NOT_NULL(tag->tags);

    tag->tags[0] = strdup("NotPlume Tag0");
    TEST_ASSERT_NOT_NULL(tag->tags[0]);

    tag->tags[1] = strdup("NotPlume Tag1");
    TEST_ASSERT_NOT_NULL(tag->tags[1]);

    *(fkey->tags) = tag;

    /* Close the active window */
    ret = net_md_close_active_window(alt_aggr);
    TEST_ASSERT_TRUE(ret);

    /* Keep access to the protobuf to test adding the same flow tag */
    pb = serialize_flow_report(alt_aggr->report);
    net_md_reset_aggregator(alt_aggr);

    /* Update the original aggregator with the alt report */
    net_md_update_aggr(aggr, pb);

    /* Free the serialized container */
    free_packed_buffer(pb);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Validate the number of flow tags */
    TEST_ASSERT_EQUAL_INT(2, fkey->num_tags);

    /* Free aggregators */
    net_md_free_aggregator(alt_aggr);
    net_md_free_aggregator(aggr);
}


void
test_update_vendor_data(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_stats_accumulator *acc;
    struct flow_counters counters[1];
    struct net_md_aggregator *aggr_out;
    struct net_md_aggregator *aggr_in;
    struct flow_vendor_data *vd1;
    struct flow_vendor_data *vd2;
    struct net_md_flow_key *key;
    struct flow_key *fkey;
    bool ret;

    struct vendor_data_kv_pair vd1_kps[] =
    {
        {
            .key = "vd1_key1",
            .value_type = NET_VENDOR_STR,
            .str_value = "vd1_key1_val1",
        },
        {
            .key = "vd1_key2",
            .value_type = NET_VENDOR_U32,
            .u32_value = 12345,
        },
        {
            .key = "vd1_key3",
            .value_type = NET_VENDOR_U64,
            .u64_value = 12345678,
        },
    };

    struct vendor_data_kv_pair vd2_kps[] =
    {
        {
            .key = "vd2_key1",
            .value_type = NET_VENDOR_STR,
            .str_value = "vd2_key1_val1",
        },
        {
            .key = "vd2_key2",
            .value_type = NET_VENDOR_U32,
            .u32_value = 54321,
        },
        {
            .key = "vd2_key3",
            .value_type = NET_VENDOR_U64,
            .u64_value = 87654321,
        },
    };
    struct vendor_data_kv_pair **kvps1;
    struct vendor_data_kv_pair **kvps2;
    struct vendor_data_kv_pair *kvp;
    struct packed_buffer recv_pb;
    struct packed_buffer *pb;
    struct flow_tags **tags;
    struct flow_tags *tag;
    size_t nelems;
    size_t i;

    TEST_ASSERT_TRUE(g_nd_test.initialized);
    counters[0].bytes_count = 10000;
    counters[0].packets_count = 100;

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr_in = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr_in);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr_in);
    TEST_ASSERT_TRUE(ret);

    /* Add one sample */
    key = g_nd_test.net_md_keys[5];
    ret = net_md_add_sample(aggr_in, key, counters);
    TEST_ASSERT_TRUE(ret);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr_in, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Add vendor data to the key */
    fkey->num_vendor_data = 2;
    fkey->vdr_data = calloc(fkey->num_vendor_data,
                            sizeof(*fkey->vdr_data));
    TEST_ASSERT_NOT_NULL(fkey->vdr_data);

    nelems = 3;

    kvps1 = calloc(nelems, sizeof(struct vendor_data_kv_pair *));
    TEST_ASSERT_NOT_NULL(kvps1);
    for (i = 0; i < nelems; i++)
    {
        kvps1[i] = calloc(1, sizeof(struct vendor_data_kv_pair));
        kvp = kvps1[i];
        TEST_ASSERT_NOT_NULL(kvp);
        kvp->key = strdup(vd1_kps[i].key);
        if (vd1_kps[i].value_type == NET_VENDOR_STR)
        {
            kvp->value_type = NET_VENDOR_STR;
            kvp->str_value = strdup(vd1_kps[i].str_value);
            TEST_ASSERT_NOT_NULL(kvp->str_value);
        }
        else if (vd1_kps[i].value_type == NET_VENDOR_U32)
        {
            kvp->value_type = NET_VENDOR_U32;
            kvp->u32_value = vd1_kps[i].u32_value;
        }
        else
        {
            kvp->value_type = NET_VENDOR_U64;
            kvp->u64_value = vd1_kps[i].u64_value;
        }
        kvp++;
    }
    vd1 = calloc(1, sizeof(struct flow_vendor_data));
    vd1->vendor = strdup("vendor1");
    TEST_ASSERT_NOT_NULL(vd1->vendor);
    vd1->nelems = 3;
    vd1->kv_pairs = kvps1;

    kvps2 = calloc(nelems, sizeof(struct vendor_data_kv_pair *));
    TEST_ASSERT_NOT_NULL(kvps2);
    for (i = 0; i < nelems; i++)
    {
        kvps2[i] = calloc(1, sizeof(struct vendor_data_kv_pair));
        kvp = kvps2[i];
        TEST_ASSERT_NOT_NULL(kvp);
        kvp->key = strdup(vd2_kps[i].key);
        if (vd2_kps[i].value_type == NET_VENDOR_STR)
        {
            kvp->str_value = strdup(vd2_kps[i].str_value);
            TEST_ASSERT_NOT_NULL(kvp->str_value);
        }
        else if (vd2_kps[i].value_type == NET_VENDOR_U32)
        {
            kvp->value_type = NET_VENDOR_U32;
            kvp->u32_value = vd2_kps[i].u32_value;
        }
        else
        {
            kvp->value_type = NET_VENDOR_U64;
            kvp->u64_value = vd2_kps[i].u64_value;
        }
        kvp++;
    }
    vd2 = calloc(1, sizeof(struct flow_vendor_data));
    vd2->vendor = strdup("vendor2");
    TEST_ASSERT_NOT_NULL(vd2->vendor);
    vd2->nelems = 3;
    vd2->kv_pairs = kvps2;

    fkey->vdr_data[0] = vd1;
    fkey->vdr_data[1] = vd2;
    fkey->num_vendor_data = 2;

    /* Add a flow tag to the key */
    fkey->num_tags = 1;
    fkey->tags = calloc(fkey->num_tags, sizeof(*fkey->tags));
    TEST_ASSERT_NOT_NULL(fkey->tags);

    tag = calloc(1, sizeof(*tag));
    TEST_ASSERT_NOT_NULL(tag);

    tag->vendor = strdup("Plume");
    TEST_ASSERT_NOT_NULL(tag->vendor);

    tag->app_name = strdup("Plume App");
    TEST_ASSERT_NOT_NULL(tag->app_name);

    tag->nelems = 2;
    tag->tags = calloc(tag->nelems, sizeof(tags));
    TEST_ASSERT_NOT_NULL(tag->tags);

    tag->tags[0] = strdup("Plume Tag0");
    TEST_ASSERT_NOT_NULL(tag->tags[0]);

    tag->tags[1] = strdup("Plume Tag1");
    TEST_ASSERT_NOT_NULL(tag->tags[1]);

    *(fkey->tags) = tag;

    /* Close the active window */
    ret = net_md_close_active_window(aggr_in);
    TEST_ASSERT_TRUE(ret);

    /* Prepare the transfer to the receiving aggregator */
    pb = serialize_flow_report(aggr_in->report);

    recv_pb.len = pb->len;
    recv_pb.buf = pb->buf;

    test_emit_report(aggr_in);

    /* Allocate the receiving aggregator */
    aggr_out = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr_out);

    /* Activate the receiving aggregator window */
    ret = net_md_activate_window(aggr_out);
    TEST_ASSERT_TRUE(ret);

    /* Transfer tags and vendor data */
    net_md_update_aggr(aggr_out, &recv_pb);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr_out, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    TEST_ASSERT_EQUAL_INT(2, fkey->num_vendor_data);

    /* Transfer again tags and vendor data */
    net_md_update_aggr(aggr_out, &recv_pb);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr_out, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    TEST_ASSERT_EQUAL_INT(2, fkey->num_vendor_data);

    /* Free the serialized container */
    free_packed_buffer(pb);
    ret = net_md_close_active_window(aggr_out);

    test_emit_report(aggr_out);
    /* Free aggregators */
    net_md_free_aggregator(aggr_in);
    net_md_free_aggregator(aggr_out);
}

/**
 * @brief collector filter
 */
static bool
test_collect_filter_flow(struct net_md_aggregator *aggr,
                         struct net_md_flow_key *key)
{
    return false;
}

/**
 * @brief Test updating an aggregator from a protobuf with filtering
 */
void
test_update_filter_flow_tags(void)
{
    struct net_md_aggregator_set *aggr_set;
    struct net_md_stats_accumulator *acc;
    struct net_md_aggregator *alt_aggr;
    struct flow_counters counters[1];
    struct net_md_aggregator *aggr;
    struct net_md_flow_key *key;
    struct packed_buffer *pb;
    struct flow_tags **tags;
    struct flow_tags *tag;
    struct flow_key *fkey;
    bool ret;

    TEST_ASSERT_TRUE(g_nd_test.initialized);
    counters[0].bytes_count = 10000;
    counters[0].packets_count = 100;

    /* Allocate aggregator */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    aggr_set->collect_filter = test_collect_filter_flow;
    aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add one sample */
    key = g_nd_test.net_md_keys[5];
    ret = net_md_add_sample(aggr, key, counters);
    TEST_ASSERT_TRUE(ret);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Add a flow tag to the key */
    fkey->num_tags = 1;
    fkey->tags = calloc(fkey->num_tags, sizeof(*fkey->tags));
    TEST_ASSERT_NOT_NULL(fkey->tags);

    tag = calloc(1, sizeof(*tag));
    TEST_ASSERT_NOT_NULL(tag);

    tag->vendor = strdup("Plume");
    TEST_ASSERT_NOT_NULL(tag->vendor);

    tag->app_name = strdup("Plume App");
    TEST_ASSERT_NOT_NULL(tag->app_name);

    tag->nelems = 2;
    tag->tags = calloc(tag->nelems, sizeof(tags));
    TEST_ASSERT_NOT_NULL(tag->tags);

    tag->tags[0] = strdup("Plume Tag0");
    TEST_ASSERT_NOT_NULL(tag->tags[0]);

    tag->tags[1] = strdup("Plume Tag1");
    TEST_ASSERT_NOT_NULL(tag->tags[1]);

    *(fkey->tags) = tag;

    /* Close the active window */
    ret = net_md_close_active_window(aggr);
    TEST_ASSERT_TRUE(ret);

    /* Keep access to the protobuf to test adding the same flow tag */
    pb = serialize_flow_report(aggr->report);
    net_md_reset_aggregator(aggr);

    net_md_update_aggr(aggr, pb);

    /* Free the serialized container */
    free_packed_buffer(pb);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Validate the number of flow tags */
    TEST_ASSERT_EQUAL_INT(1, fkey->num_tags);

    /*
     * Allocate alternative aggregator.
     * Same settings as the original aggregator, different vendor for flow tags.
     */
    aggr_set = &g_nd_test.aggr_set;
    aggr_set->report_type = NET_MD_REPORT_RELATIVE;
    alt_aggr = net_md_allocate_aggregator(aggr_set);
    TEST_ASSERT_NOT_NULL(alt_aggr);

    /* Activate aggregator window */
    ret = net_md_activate_window(alt_aggr);
    TEST_ASSERT_TRUE(ret);

    /* Add one sample */
    key = g_nd_test.net_md_keys[5];
    ret = net_md_add_sample(alt_aggr, key, counters);
    TEST_ASSERT_TRUE(ret);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(alt_aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Add a flow tag to the key */
    fkey->num_tags = 1;
    fkey->tags = calloc(fkey->num_tags, sizeof(*fkey->tags));
    TEST_ASSERT_NOT_NULL(fkey->tags);

    tag = calloc(1, sizeof(*tag));
    TEST_ASSERT_NOT_NULL(tag);

    tag->vendor = strdup("NotPlume");
    TEST_ASSERT_NOT_NULL(tag->vendor);

    tag->app_name = strdup("NotPlume App");
    TEST_ASSERT_NOT_NULL(tag->app_name);

    tag->nelems = 2;
    tag->tags = calloc(tag->nelems, sizeof(tags));
    TEST_ASSERT_NOT_NULL(tag->tags);

    tag->tags[0] = strdup("NotPlume Tag0");
    TEST_ASSERT_NOT_NULL(tag->tags[0]);

    tag->tags[1] = strdup("NotPlume Tag1");
    TEST_ASSERT_NOT_NULL(tag->tags[1]);

    *(fkey->tags) = tag;

    /* Close the active window */
    ret = net_md_close_active_window(alt_aggr);
    TEST_ASSERT_TRUE(ret);

    /* Keep access to the protobuf to test adding the same flow tag */
    pb = serialize_flow_report(alt_aggr->report);
    net_md_reset_aggregator(alt_aggr);

    /*
     * Update the original aggregator with the alt report.
     * The collector filter will reject all flows
     */
    net_md_update_aggr(aggr, pb);

    /* Free the serialized container */
    free_packed_buffer(pb);

    /* Validate the state of the accumulator bound to the key */
    acc = net_md_lookup_acc(aggr, key);
    TEST_ASSERT_NOT_NULL(acc);
    fkey = acc->fkey;
    TEST_ASSERT_NOT_NULL(fkey);

    /* Validate the number of flow tags */
    TEST_ASSERT_EQUAL_INT(1, fkey->num_tags);

    /* Free aggregators */
    net_md_free_aggregator(alt_aggr);
    net_md_free_aggregator(aggr);
}
