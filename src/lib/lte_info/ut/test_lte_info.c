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

#include "lte_info.h"
#include "memutil.h"
#include "log.h"
#include "unity.h"
#include "target.h"
#include "qm_conn.h"
#include "unit_test_utils.h"

const char *test_name = "lte_info_tests";

struct lte_net_neighbor_cell_info *g_neigh_cell = NULL;
struct lte_info_packed_buffer *g_serialized = NULL;
struct lte_info_report *g_report = NULL;

char *g_deployment = "dog1";
char g_mqtt_topic[256];

struct lte_common_header g_common_header =
{
    .request_id = 1,
    .if_name = "wwan0",
    .node_id = "HC8490008E",
    .location_id = "602e11e768b6592af397e9f2",
    .imei = "861364040104042",
    .imsi = "222013410161198",
    .reported_at = 0,
};

struct lte_net_info g_lte_net_info =
{
    .net_status = LTE_NET_REG_STAT_REG,
    .mcc = 310,
    .mnc = 410,
    .tac = 8,
    .service_provider = "AT&T",
    .sim_type = LTE_SIM_TYPE_PSIM,
    .sim_status = LTE_SIM_STATUS_INSERTED,
    .active_sim_slot = 1,
    .rssi = -71,
    .ber = 99,
    .last_healthcheck_success = 123456789,
    .healthcheck_failures = 1,
};

struct lte_data_usage g_lte_data_usage =
{
    .rx_bytes = 123456789,
    .tx_bytes = 0xABCDEF123,
    .failover_start = 0,
    .failover_end = 90,
    .failover_count = 1,
};

struct lte_net_serving_cell_info g_srv_cell_info =
{
    .state = LTE_SERVING_CELL_NOCONN,
    .mode = LTE_CELL_MODE_LTE,
    .fdd_tdd_mode = LTE_MODE_TDD,
    .cellid = 0xA1FBF11,
    .pcid = 218,
    .uarfcn = 0,
    .earfcn = 5110,
    .freq_band = 12,
    .ul_bandwidth = LTE_BANDWIDTH_10_MHZ,
    .dl_bandwidth = LTE_BANDWIDTH_10_MHZ,
    .tac = 0x8B1E,
    .rsrp = -101,
    .rsrq = -15,
    .rssi = -69,
    .sinr = 10,
    .srxlev = 22,
};


struct lte_net_neighbor_cell_info g_neigh_cells[] =
{
    {
        .mode = LTE_CELL_MODE_LTE,
        .freq_mode = LTE_FREQ_MODE_INTRA,
        .earfcn = 5035,
        .uarfcn = 0,
        .pcid = 159,
        .rsrq = -15,
        .rsrp = -111,
        .rssi = -81,
        .sinr = 0,
        .srxlev = 13,
        .cell_resel_priority = 1,
        .s_non_intra_search = 8,
        .thresh_serving_low = 0,
        .s_intra_search = 46,
        .thresh_x_low = 0,
        .thresh_x_high = 0,
        .psc = 0,
        .rscp = 0,
        .ecno = 0,
        .cell_set = LTE_NEIGHBOR_CELL_SET_ACTIVE_SET,
        .rank = -190,
        .cellid = 0,
        .inter_freq_srxlev = -90,
    },
    {
        .mode = LTE_CELL_MODE_WCDMA,
        .freq_mode = LTE_FREQ_MODE_INTER,
        .earfcn = 5035,
        .uarfcn = 0,
        .pcid = 159,
        .rsrq = -15,
        .rsrp = -111,
        .rssi = -81,
        .sinr = 0,
        .srxlev = 13,
        .cell_resel_priority = 1,
        .s_non_intra_search = 8,
        .thresh_serving_low = 0,
        .s_intra_search = 46,
        .thresh_x_low = 0,
        .thresh_x_high = 0,
        .psc = 0,
        .rscp = 0,
        .ecno = 0,
        .cell_set = LTE_NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR,
        .rank = -190,
        .cellid = 0,
        .inter_freq_srxlev = -90,
    }
};

char *pb_file = "/tmp/lte_ut_proto.bin";

static void
test_lte_send_report(char *topic, struct lte_info_packed_buffer *pb)
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
    printf("%s: topic[%s]", __func__, topic);
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, topic,
                              pb->buf, pb->len, &res);
    if (!ret) LOGE("error sending mqtt with topic %s", topic);
#endif

    return;
}

/**
 * @brief writes the contents of a serialized buffer in a file
 *
 * @param pb serialized buffer to be written
 * @param fpath target file path
 *
 * @return returns the number of bytes written
 */
static size_t
pb2file(struct lte_info_packed_buffer *pb, char *fpath)
{
    FILE *f = fopen(fpath, "w");
    size_t nwrite = fwrite(pb->buf, 1, pb->len, f);
    fclose(f);

    return nwrite;
}


/**
 * @brief called by the Unity framework after every single test
 */
void
lte_info_tearDown(void)
{
    lte_info_free_report(g_report);
    g_report = NULL;

    lte_info_free_neigh_cell_info(g_neigh_cell);
    g_neigh_cell = NULL;

    lte_info_free_packed_buffer(g_serialized);
    g_serialized = NULL;

    return;
}


/**
 * @brief test setting the common header of a report
 */
void
test_lte_set_common_header(void)
{
    bool ret;
    time_t now = time(NULL);

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    g_common_header.reported_at = now;
    ret = lte_info_set_common_header (&g_common_header, g_report);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test setting the lte_net_info field of a report
 */
void
test_lte_set_net_info(void)
{
    bool ret;

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    ret = lte_info_set_net_info(&g_lte_net_info, g_report);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test setting the lte_data_usage field of a report
 */
void
test_lte_set_data_usage(void)
{
    bool ret;
    time_t now = time(NULL);

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    g_lte_data_usage.failover_start += now;
    g_lte_data_usage.failover_end += now;
    ret = lte_info_set_data_usage(&g_lte_data_usage, g_report);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test setting a neighbor cell info
 */
void
test_lte_set_neigh_cell_info(void)
{
    bool ret;

    g_neigh_cell = CALLOC(1, sizeof(*g_neigh_cell));
    TEST_ASSERT_NOT_NULL(g_neigh_cell);

    ret = lte_info_set_neigh_cell_info(&g_neigh_cells[0], g_neigh_cell);
    TEST_ASSERT_TRUE(ret);
}


/**
 * @brief test setting a serving cell info
 */
void
test_lte_set_srv_cell(void)
{
    bool ret;

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    ret = lte_info_set_serving_cell(&g_srv_cell_info, g_report);
    TEST_ASSERT_TRUE(ret);
}


/**
 * @brief set a full report
 */
void
lte_ut_set_report(void)
{
    bool ret;

    /* Allocate a report provisioning 2 neighbor cells */
    g_report = lte_info_allocate_report(2);
    TEST_ASSERT_NOT_NULL(g_report);

    /* Add one report cell */
    ret = lte_info_add_neigh_cell_info(&g_neigh_cells[0], g_report);

    /* validate the addition of the neighbor cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->lte_neigh_cell_info[0]);
    TEST_ASSERT_EQUAL_UINT(1, g_report->cur_neigh_cell_idx);

    /* Add another report cell */
    ret = lte_info_add_neigh_cell_info(&g_neigh_cells[1], g_report);

    /* validate the addition of the neighbor cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->lte_neigh_cell_info[1]);
    TEST_ASSERT_EQUAL_UINT(2, g_report->cur_neigh_cell_idx);

    /* Validate the addition of the common header */
    ret = lte_info_set_common_header(&g_common_header, g_report);

    /* Validate the addition of the common header */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->header);

    /* Add the lte net info */
    ret = lte_info_set_net_info(&g_lte_net_info, g_report);

    /* Validate the addition of the lte net info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->lte_net_info);

    /* Add the lte data usage */
    time_t now = time(NULL);
    g_lte_data_usage.failover_start += now;
    g_lte_data_usage.failover_end += now;
    ret = lte_info_set_data_usage(&g_lte_data_usage, g_report);

    /* Validate the addition of the lte data usage */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->lte_data_usage);

    /* Add the serving cell info */
    ret = lte_info_set_serving_cell(&g_srv_cell_info, g_report);

    /* Validate the addition of the serving cell info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->lte_srv_cell);
}


/**
 * @brief test setting a full report
 */
void
test_lte_set_report(void)
{
    lte_ut_set_report();
}


/**
 * @brief test seerializing a full report
 */
void
test_lte_serialize_report(void)
{
    lte_ut_set_report();
    g_serialized = serialize_lte_info(g_report);

    /* Save the serialized protobuf in a file */
    pb2file(g_serialized, pb_file);
    test_lte_send_report(g_mqtt_topic, g_serialized);
}


/**
 * @brief create mqtt topic
 */
void
test_lte_set_mqtt_topic(void)
{
    int rc;

    rc = snprintf(g_mqtt_topic, sizeof(g_mqtt_topic), "dev-test/LteStats/%s/%s/%s",
                  g_deployment, g_common_header.node_id,
                  g_common_header.location_id);
    TEST_ASSERT_NOT_EQUAL_INT(0, rc);
}


int
main(int argc, char *argv[])
{
    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, NULL, lte_info_tearDown);

    RUN_TEST(test_lte_set_mqtt_topic);
    RUN_TEST(test_lte_set_common_header);
    RUN_TEST(test_lte_set_net_info);
    RUN_TEST(test_lte_set_data_usage);
    RUN_TEST(test_lte_set_neigh_cell_info);
    RUN_TEST(test_lte_set_srv_cell);
    RUN_TEST(test_lte_set_report);
    RUN_TEST(test_lte_serialize_report);

    return ut_fini();
}
