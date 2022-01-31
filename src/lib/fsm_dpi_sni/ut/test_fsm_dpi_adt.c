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

#include "fsm_dpi_adt.h"
#include "fsm_dpi_sni.h"
#include "memutil.h"
#include "os.h"
#include "os_nif.h"
#include "unity.h"

static union fsm_plugin_ops g_plugin_ops =
{
    .web_cat_ops =
    {
        .categories_check = NULL,
        .risk_level_check = NULL,
        .cat2str = NULL,
        .get_stats = NULL,
        .dns_response = NULL,
        .gatekeeper_req = NULL,
    },
};

static struct fsm_session g_session =
{
    .node_id = "NODE_ID",
    .location_id = "LOCATION_ID",
    .topic = "ADT_TOPIC",
};

static bool g_mock_enabled;

/**
 * @brief converts the protobuf from os_macaddr into the
 *        representing string.
 *
 * @param serialized_mac the protobuf byte array
 * @param str_mac ptr to the ALLOCATED string to receive
 *        the textual MAC
 */
void
os_mac2str(ProtobufCBinaryData *serialized_mac, char* str_mac)
{
    const char *hex = "0123456789ABCDEF";
    uint8_t *pin = serialized_mac->data;
    size_t i;

    for (i = 0; i < serialized_mac->len - 1; i++)
    {
        *str_mac++ = hex[(*pin >> 4) & 0xF];
        *str_mac++ = hex[(*pin++) & 0xF];
        *str_mac++ = ':';
    }
    *str_mac++ = hex[(*pin >> 4) & 0xF];
    *str_mac++ = hex[(*pin) & 0xF];
    *str_mac = 0;
}

/**
 * @brief Unit test replacement for the actual sending over MQTT.
 *        The serialized protobuf in data in saved to /tmp/output<X>.pb
 *        and we perform some basic checks on the protobuf itself.
 *
 * @params: prototype is identical as the one of @see qm_conn_send_direct
 */
bool
mock_ut_qm_conn_send_direct(qm_compress_t compress, char *topic,
                            void *data, int data_size, qm_response_t *res)
{
    char filename[100];
    char str_mac[32];
    static size_t i;
    FILE *fd;

    Interfaces__Adt__AdtReport *report;

    sprintf(filename, "/tmp/%s_%zu.pb", topic, i++);

    LOGI("Sending report to file %s", filename);

    fd = fopen(filename, "wb");
    TEST_ASSERT_NOT_NULL(fd);
    fwrite((char *)data, data_size, 1, fd);
    fclose(fd);

    /* perform some minimal check on the protobuf */
    report = interfaces__adt__adt_report__unpack(NULL, data_size, data);
    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT_NOT_NULL(report->observation_point);

    TEST_ASSERT_NOT_NULL(report->observation_point->location_id);
    TEST_ASSERT_EQUAL_STRING(g_session.location_id, report->observation_point->location_id);

    TEST_ASSERT_NOT_NULL(report->observation_point->node_id);
    TEST_ASSERT_EQUAL_STRING(g_session.node_id, report->observation_point->node_id);

    TEST_ASSERT_NOT_NULL(report->data);
    TEST_ASSERT_EQUAL_INT(1, report->n_data);

    TEST_ASSERT_NOT_NULL(report->data[0]->device_id.data);
    os_mac2str(&report->data[0]->device_id, str_mac);
    LOGI("From protobuf: data[0], device_id=%s", str_mac);

    interfaces__adt__adt_report__free_unpacked(report, NULL);

    return true;
}

char *
mock_get_config(struct fsm_session *session, char *key)
{
    (void)session;

    return key;
}

void
test_fsm_dpi_adt_init(void)
{
    struct fsm_dpi_sni_session *new_adt_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_sni_session *adt_session;
    struct fsm_session working_session;
    struct fsm_session broken_session;
    bool ret;

    adt_session = (struct fsm_dpi_sni_session *)g_session.handler_ctxt;
    TEST_ASSERT_NULL(adt_session);

    ret = fsm_dpi_adt_exit(&g_session);
    TEST_ASSERT_FALSE(ret);

    ret = fsm_dpi_adt_init(NULL);
    TEST_ASSERT_FALSE(ret);

    broken_session.location_id = NULL;
    broken_session.node_id = NULL;
    ret = fsm_dpi_adt_init(&broken_session);
    TEST_ASSERT_FALSE(ret);

    broken_session.location_id = "location";
    broken_session.node_id = NULL;
    ret = fsm_dpi_adt_init(&broken_session);
    TEST_ASSERT_FALSE(ret);

    broken_session.location_id = NULL;
    broken_session.node_id = "node";
    ret = fsm_dpi_adt_init(&broken_session);
    TEST_ASSERT_FALSE(ret);

    /* Still missing handler_context */
    working_session = g_session;
    ret = fsm_dpi_adt_init(&working_session);
    TEST_ASSERT_FALSE(ret);

    /* add a handler_context, now we're complete */
    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    working_session.handler_ctxt = new_adt_session;

    ret = fsm_dpi_adt_init(&working_session);
    TEST_ASSERT_TRUE(ret);
    adt_session = (struct fsm_dpi_sni_session *)working_session.handler_ctxt;
    TEST_ASSERT_NOT_NULL(adt_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_TRUE(aggr->initialized);

    ret = fsm_dpi_adt_init(&working_session);
    TEST_ASSERT_TRUE(ret);

    /* Cleanup */
    ret = fsm_dpi_adt_exit(&working_session);
    TEST_ASSERT_TRUE(ret);

    FREE(working_session.handler_ctxt);
}

void
test_fsm_dpi_adt_store(void)
{
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_sni_session *new_adt_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_sni_session *adt_session;
    struct net_md_stats_accumulator acc;
    struct fsm_session this_session;
    bool ret;

    this_session = g_session;
    this_session.name = "dpi_adt";

    MEMZERO(acc);
    pkt_info.acc = &acc;

    /* No session */
    ret = fsm_dpi_adt_store(NULL, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* incomplete session */
    ret = fsm_dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Not initialized yet */
    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    this_session.handler_ctxt = new_adt_session;
    ret = fsm_dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Initializing aggregator */
    ret = fsm_dpi_adt_init(&this_session);
    TEST_ASSERT_TRUE(ret);
    adt_session = (struct fsm_dpi_sni_session *)this_session.handler_ctxt;
    TEST_ASSERT_NOT_NULL(adt_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    TEST_ASSERT_TRUE(aggr->initialized);
    aggr->send_report = mock_ut_qm_conn_send_direct;

    MEMZERO(acc);

    /* Validate the parameters */
    ret = fsm_dpi_adt_store(&this_session, NULL, RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);
    ret = fsm_dpi_adt_store(&this_session, "", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);
    ret = fsm_dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, 0, NULL, &pkt_info);
    TEST_ASSERT_FALSE(ret);
    ret = fsm_dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, 0, "", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Break on incomplete acc */
    ret = fsm_dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Populate acc !!!!  There has to be a better way */
    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.key = CALLOC(1, sizeof(*acc.key));
    acc.key->direction = NET_MD_ACC_OUTBOUND_DIR;

    ret = fsm_dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    /* Clean things up */
    FREE(acc.key);
    ret = fsm_dpi_adt_exit(&this_session);
    TEST_ASSERT_TRUE(ret);
    FREE(this_session.handler_ctxt);
}

void
test_fsm_dpi_adt_store_to_proto(void)
{
    struct fsm_dpi_sni_session *new_adt_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_sni_session *adt_session;
    Interfaces__Adt__AdtReport report_pb;
    struct fsm_session this_session;
    bool ret;

    this_session = g_session;
    this_session.name = "dpi_adt";
    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    this_session.handler_ctxt = new_adt_session;

    /* No destination memory */
    ret = fsm_dpi_adt_store2proto(NULL, NULL);
    TEST_ASSERT_FALSE(ret);

    /* No aggregator */
    ret = fsm_dpi_adt_store2proto(NULL, &report_pb);
    TEST_ASSERT_FALSE(ret);

    /* provide aggregator */
    adt_session = (struct fsm_dpi_sni_session *)this_session.handler_ctxt;
    TEST_ASSERT_NOT_NULL(adt_session);
    fsm_dpi_adt_init(&this_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    aggr->send_report = mock_ut_qm_conn_send_direct;

    /* Nothing to report */
    ret = fsm_dpi_adt_store2proto(&this_session, &report_pb);
    TEST_ASSERT_FALSE(ret);

    /* Cleanup */
    ret = fsm_dpi_adt_exit(&this_session);
    TEST_ASSERT_TRUE(ret);
    FREE(this_session.handler_ctxt);
}

void
test_fsm_dpi_adt_send_report_v4(void)
{
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_sni_session *new_adt_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_sni_session *adt_session;
    struct net_md_stats_accumulator acc;
    struct fsm_session this_session;
    bool ret;

    this_session = g_session;
    this_session.name = "dpi_adt";
    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    this_session.handler_ctxt = new_adt_session;

    ret = fsm_dpi_adt_init(&this_session);
    TEST_ASSERT_TRUE(ret);

    adt_session = (struct fsm_dpi_sni_session *)this_session.handler_ctxt;
    TEST_ASSERT_NOT_NULL(adt_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_NOT_NULL(aggr);

    /* attach a "fake" sender */
    if (g_mock_enabled) aggr->send_report = mock_ut_qm_conn_send_direct;

    /* nothing to report */
    ret = fsm_dpi_adt_send_report(&this_session);
    if (g_mock_enabled) TEST_ASSERT_TRUE(ret);

    MEMZERO(acc);
    pkt_info.acc = &acc;

    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.key = CALLOC(1, sizeof(*acc.key));
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;

    ret = fsm_dpi_adt_store(&this_session, "KEY", RTS_TYPE_STRING, strlen("VALUE"), "VALUE", &pkt_info);
    TEST_ASSERT_FALSE(ret);

    acc.key->smac = CALLOC(1, sizeof(*acc.key->smac));
    os_nif_macaddr_from_str(acc.key->smac, "00:11:22:33:44:55");

    acc.key->dmac = CALLOC(1, sizeof(*acc.key->dmac));
    os_nif_macaddr_from_str(acc.key->dmac, "AA:BB:CC:DD:EE:FF");

    acc.key->src_ip = CALLOC(4, sizeof(*acc.key->src_ip));
    inet_pton(AF_INET, "1.2.3.4", acc.key->src_ip);
    acc.key->sport = 401;
    acc.key->dst_ip = CALLOC(4, sizeof(*acc.key->dst_ip));
    inet_pton(AF_INET, "9.8.7.6", acc.key->dst_ip);
    acc.key->dport = 499;

    /* First with a bogus ip_version */
    acc.key->ip_version = 0;
    ret = fsm_dpi_adt_store(&this_session, "the_key", RTS_TYPE_STRING, strlen("a_value"), "a_value", &pkt_info);
    TEST_ASSERT_TRUE(ret);
    ret = fsm_dpi_adt_send_report(&this_session);
    /* Do not check when using qm_conn_send_report() as it will fail on x64 */
    if (g_mock_enabled) TEST_ASSERT_FALSE(ret);

    /* This time with the correct ip_version */
    acc.key->ip_version = 4;
    ret = fsm_dpi_adt_store(&this_session, "the_key", RTS_TYPE_STRING, strlen("a_value"), "a_value", &pkt_info);
    TEST_ASSERT_TRUE(ret);
    ret = fsm_dpi_adt_send_report(&this_session);
    /* Do not check when using qm_conn_send_report() as it will fail on x64 */
    if (g_mock_enabled) TEST_ASSERT_TRUE(ret);

    /* Clean up */
    FREE(acc.key->dmac);
    FREE(acc.key->smac);
    FREE(acc.key->src_ip);
    FREE(acc.key->dst_ip);
    FREE(acc.key);
    ret = fsm_dpi_adt_exit(&this_session);
    TEST_ASSERT_TRUE(ret);
    FREE(this_session.handler_ctxt);
}

void
test_fsm_dpi_adt_send_report_v6(void)
{
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_sni_session *new_adt_session;
    struct fsm_dpi_adt_report_aggregator *aggr;
    struct fsm_dpi_sni_session *adt_session;
    struct net_md_stats_accumulator acc;
    struct fsm_session this_session;
    bool ret;

    this_session = g_session;
    this_session.name = "dpi_adt";
    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    this_session.handler_ctxt = new_adt_session;

    ret = fsm_dpi_adt_init(&this_session);
    TEST_ASSERT_TRUE(ret);

    adt_session = (struct fsm_dpi_sni_session *)this_session.handler_ctxt;
    TEST_ASSERT_NOT_NULL(adt_session);
    aggr = adt_session->adt_aggr;
    TEST_ASSERT_NOT_NULL(aggr);

    /* attach a "fake" sender */
    if (g_mock_enabled) aggr->send_report = mock_ut_qm_conn_send_direct;

    MEMZERO(acc);
    pkt_info.acc = &acc;

    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.key = CALLOC(1, sizeof(*acc.key));
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;

    acc.key->ip_version = 6;

    acc.key->smac = CALLOC(1, sizeof(*acc.key->smac));
    os_nif_macaddr_from_str(acc.key->smac, "00:11:22:33:44:55");

    acc.key->dmac = CALLOC(1, sizeof(*acc.key->dmac));
    os_nif_macaddr_from_str(acc.key->dmac, "AA:BB:CC:DD:EE:FF");

    acc.key->src_ip = CALLOC(16, sizeof(*acc.key->src_ip));
    inet_pton(AF_INET6, "1:2::3", acc.key->src_ip);
    acc.key->sport = 601;
    acc.key->dst_ip = CALLOC(16, sizeof(*acc.key->dst_ip));
    acc.key->dport = 699;
    inet_pton(AF_INET6, "2:4::3", acc.key->dst_ip);

    ret = fsm_dpi_adt_store(&this_session, "the_key_16", RTS_TYPE_STRING, strlen("a_value_16"), "a_value_16", &pkt_info);
    TEST_ASSERT_TRUE(ret);
    ret = fsm_dpi_adt_send_report(&this_session);
    /* Do not check when using qm_conn_send_report() as it will fail on x64 */
    if (g_mock_enabled) TEST_ASSERT_TRUE(ret);

    /* Clean up */
    ret = fsm_dpi_adt_exit(&this_session);
    TEST_ASSERT_TRUE(ret);
    FREE(this_session.handler_ctxt);
    FREE(acc.key->dmac);
    FREE(acc.key->smac);
    FREE(acc.key->src_ip);
    FREE(acc.key->dst_ip);
    FREE(acc.key);
}

void
test_fsm_dpi_adt_from_top(void)
{
    struct fsm_dpi_sni_session *new_adt_session;
    struct fsm_session this_session;
    int ret;

    this_session = g_session;
    this_session.name = "dpi_adt";
    new_adt_session = CALLOC(1, sizeof(*new_adt_session));
    this_session.handler_ctxt = new_adt_session;
    this_session.p_ops = &g_plugin_ops;
    this_session.ops.get_config = mock_get_config;

    ret = dpi_sni_plugin_init(&this_session);
    TEST_ASSERT_EQUAL_INT(0, ret);

    fsm_dpi_sni_plugin_exit(&this_session);

    /* Cleanup */
    FREE(new_adt_session);
}

void
run_test_adt(void)
{
    RUN_TEST(test_fsm_dpi_adt_init);
    RUN_TEST(test_fsm_dpi_adt_store);
    RUN_TEST(test_fsm_dpi_adt_store_to_proto);

    /* first time with the mock function writing to disk */
    g_mock_enabled = true;
    RUN_TEST(test_fsm_dpi_adt_send_report_v4);
    RUN_TEST(test_fsm_dpi_adt_send_report_v6);

    /* second time will send using the actual qm_conn_send_report */
    g_mock_enabled = false;
    RUN_TEST(test_fsm_dpi_adt_send_report_v4);
    RUN_TEST(test_fsm_dpi_adt_send_report_v6);

    RUN_TEST(test_fsm_dpi_adt_from_top);
}
