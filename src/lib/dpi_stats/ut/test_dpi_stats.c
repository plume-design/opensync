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

#include "dpi_stats.h"
#include "memutil.h"
#include "log.h"
#include "unity.h"
#include "target.h"
#include "qm_conn.h"
#include "unit_test_utils.h"

const char *test_name = "test_dpi_stats";

struct dpi_stats_packed_buffer *g_serialized = NULL;
char *g_location_id = "59f39f5acbb22513f0ae5e17";
char *g_node_id = "4C718002B3";
char *g_deployment = "dog1";
char g_mqtt_topic[256];

static void
test_dpi_stats_send_report(char *topic, struct dpi_stats_packed_buffer *pb)
{
#ifndef ARCH_X86
    qm_response_t res;
    bool ret = false;
#endif

    TEST_ASSERT_NOT_NULL(topic);
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    LOGD("%s: msg len: %zu, topic: %s",
         __func__, pb->len, topic);

#ifndef ARCH_X86
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, topic,
                              pb->buf, pb->len, &res);
    if (!ret) LOGE("error sending mqtt with topic %s", topic);
#endif

    return;
}

static bool
dpi_test_send_report(qm_compress_t compress, char *topic,
                     void *data, int data_size, qm_response_t *res)
{
#ifndef ARCH_X86
    bool ret = false;
#endif

    TEST_ASSERT_NOT_NULL(topic);
    TEST_ASSERT_NOT_NULL(data);

    LOGD("%s: msg len: %d, topic: %s",
         __func__, data_size, topic);

#ifndef ARCH_X86
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, topic,
                              data, data_size, res);
    if (!ret) LOGE("error sending mqtt with topic %s", topic);
#endif

    return true;
}

void
test_dpi_stats_serialize_report(void)
{
    struct dpi_engine_counters *counters;
    struct dpi_stats_report report;

    memset(&report, 0, sizeof(report));
    report.location_id = g_location_id;
    report.node_id = g_node_id;
    report.plugin = "ut_dpi_plugin";

    /* Set a few counters */
    counters = &report.counters;
    counters->curr_alloc = 10;
    counters->err_scan = 3;

    g_serialized = dpi_stats_serialize_report(&report);
    TEST_ASSERT_NOT_NULL(g_serialized);

    test_dpi_stats_send_report(g_mqtt_topic, g_serialized);
    dpi_stats_free_packed_buffer(g_serialized);
}


/**
 * @brief create mqtt topic
 */
void
test_lte_set_mqtt_topic(void)
{
    int rc;

    rc = snprintf(g_mqtt_topic, sizeof(g_mqtt_topic), "dev-test/DpiStats/%s/%s/%s",
                  g_deployment, g_node_id,
                  g_location_id);
    TEST_ASSERT_NOT_EQUAL_INT(0, rc);
}

void
test_dpi_stats_serialize_nfq_pcap_report(void)
{
    struct nfqnl_counters nfq_stats;
    struct pcap_stat pcap_stat;
    char *ifname;
    struct fsm_session session = {
        .location_id = g_location_id,
        .node_id = g_node_id,
        .name = "test_plugin",
    };
    struct dpi_stats_report *gptr;

    /* store 1st record, key is queue no */
    nfq_stats.copy_mode = 12;
    nfq_stats.queue_num = 13;
    nfq_stats.queue_total = 14;
    nfq_stats.copy_range = 15;
    nfq_stats.queue_dropped = 16;

    dpi_stats_store_nfq_stats(&nfq_stats);
    TEST_ASSERT_EQUAL_INT(1, dpi_stats_get_nfq_stats_count());

    /* store 2nd record */
    nfq_stats.copy_mode = 22;
    nfq_stats.queue_num = 23;
    nfq_stats.queue_total = 24;
    nfq_stats.copy_range = 25;
    nfq_stats.queue_dropped = 26;

    dpi_stats_store_nfq_stats(&nfq_stats);
    TEST_ASSERT_EQUAL_INT(2, dpi_stats_get_nfq_stats_count());

    pcap_stat.ps_recv = 12;
    pcap_stat.ps_drop = 13;
    ifname = "br-test1";
    dpi_stats_store_pcap_stats(&pcap_stat, ifname);
    TEST_ASSERT_EQUAL_INT(1, dpi_stats_get_pcap_stats_count());
    TEST_ASSERT_EQUAL_INT(2, dpi_stats_get_nfq_stats_count());

    pcap_stat.ps_recv = 22;
    pcap_stat.ps_drop = 33;
    ifname = "br-test2";
    dpi_stats_store_pcap_stats(&pcap_stat, ifname);
    TEST_ASSERT_EQUAL_INT(2, dpi_stats_get_pcap_stats_count());
    TEST_ASSERT_EQUAL_INT(2, dpi_stats_get_nfq_stats_count());

    gptr = dpi_stats_get_global_report();
    gptr->send_report = dpi_test_send_report;

    /* send the report */
    // char *topic ="DpiStats/dog1/64777ef8e77ced000ab57892/LG8C401377";
    dpi_stats_report_nfq_stats(&session, g_mqtt_topic);
    TEST_ASSERT_EQUAL_INT(0, dpi_stats_get_pcap_stats_count());
    TEST_ASSERT_EQUAL_INT(0, dpi_stats_get_nfq_stats_count());
}

void
test_dpi_stats_serialize_nfq_report(void)
{
    struct nfqnl_counters nfq_stats;
    struct dpi_stats_report *gptr;
    struct fsm_session session = {
        .location_id = g_location_id,
        .node_id = g_node_id,
        .name = "test_plugin",
    };

    /* store 1st record, key is queue no */
    nfq_stats.copy_mode = 12;
    nfq_stats.queue_num = 13;
    nfq_stats.queue_total = 14;
    nfq_stats.copy_range = 15;
    nfq_stats.queue_dropped = 16;

    dpi_stats_store_nfq_stats(&nfq_stats);
    TEST_ASSERT_EQUAL_INT(1, dpi_stats_get_nfq_stats_count());

    /* store 2nd record */
    nfq_stats.copy_mode = 22;
    nfq_stats.queue_num = 23;
    nfq_stats.queue_total = 24;
    nfq_stats.copy_range = 25;
    nfq_stats.queue_dropped = 26;

    dpi_stats_store_nfq_stats(&nfq_stats);
    TEST_ASSERT_EQUAL_INT(2, dpi_stats_get_nfq_stats_count());

    gptr = dpi_stats_get_global_report();
    gptr->send_report = dpi_test_send_report;

    /* send the report */
    dpi_stats_report_nfq_stats(&session, g_mqtt_topic);
    TEST_ASSERT_EQUAL_INT(0, dpi_stats_get_nfq_stats_count());
}

void
test_dpi_stats_serialize_pcap_report(void)
{
    struct pcap_stat pcap_stat;
    char *ifname;
    struct fsm_session session = {
        .location_id = g_location_id,
        .node_id = g_node_id,
        .name = "test_plugin",
    };
    struct dpi_stats_report *gptr;

    pcap_stat.ps_recv = 12;
    pcap_stat.ps_drop = 13;
    ifname = "br-test1";
    dpi_stats_store_pcap_stats(&pcap_stat, ifname);
    TEST_ASSERT_EQUAL_INT(1, dpi_stats_get_pcap_stats_count());
    TEST_ASSERT_EQUAL_INT(0, dpi_stats_get_nfq_stats_count());

    pcap_stat.ps_recv = 22;
    pcap_stat.ps_drop = 33;
    ifname = "br-test2";
    dpi_stats_store_pcap_stats(&pcap_stat, ifname);
    TEST_ASSERT_EQUAL_INT(2, dpi_stats_get_pcap_stats_count());
    TEST_ASSERT_EQUAL_INT(0, dpi_stats_get_nfq_stats_count());

    gptr = dpi_stats_get_global_report();
    gptr->send_report = dpi_test_send_report;

    /* send the report */
    dpi_stats_report_pcap_stats(&session, g_mqtt_topic);
    TEST_ASSERT_EQUAL_INT(0, dpi_stats_get_pcap_stats_count());
    TEST_ASSERT_EQUAL_INT(0, dpi_stats_get_nfq_stats_count());
}

int
main(int argc, char *argv[])
{
    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, NULL, NULL);

    RUN_TEST(test_lte_set_mqtt_topic);
    RUN_TEST(test_dpi_stats_serialize_report);
    RUN_TEST(test_dpi_stats_serialize_nfq_report);
    RUN_TEST(test_dpi_stats_serialize_pcap_report);
    RUN_TEST(test_dpi_stats_serialize_nfq_pcap_report);

    return ut_fini();
}
