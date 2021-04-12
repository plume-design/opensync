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

const char *test_name = "lte_info_tests";

struct lte_net_neighbor_cell_info *g_neigh_cell = NULL;
struct lte_info_packed_buffer *g_serialized = NULL;
struct lte_info_report *g_report = NULL;
char *g_ifname = "lte_ut_ifname";

struct lte_info g_lte_info =
{
    .prod_id_info = "lte_ut_id",
    .chip_serial = "lte_ut_12345",
    .imei = "lte_ut_IMEI12345",
    .imsi = "lte_ut_IMSI12345",
    .iccid = "lte_ut_iccid",
    .sim_status = "lte_ut_sim_enabled",
    .net_reg_status = "lte_ut_reg_enabled",
    .service_provider_name = "lte_ut_lte_plume",
    .sim_slot = "lte_ut_sim_slot_1",
};


struct lte_sig_qual g_sig_qual_info =
{
    .rssi = "lte_ut_rssi",
    .ber = "lte_ut_ber",
};


struct lte_net_serving_cell_info g_srv_cell_info =
{
    .cell_type = "lte_ut_cell_type",
    .state = "lte_ut_state",
    .is_tdd = "lte_ut_is_tdd",
    .mcc = "lte_ut_mcc",
    .mnc = "lte_ut_mnc",
    .cellid = "lte_ut_cellid",
    .pcid = "lte_ut_pcid",
    .uarfcn = "lte_ut_uarfcn",
    .earfcn = "lte_ut_earfcn",
    .freq_band = "lte_ut_freq_band",
    .ul_bandwidth = "lte_ut_ul_bandwidth",
    .dl_bandwidth = "lte_ut_dl_bandwidth",
    .tac = "lte_ut_tac",
    .rsrp = "lte_ut_rsrp",
    .rsrq = "lte_ut_rsrq",
    .rssi = "lte_ut_rssi",
    .sinr = "lte_ut_sinr",
    .srxlev = "lte_ut_srxlev",
};


struct lte_net_neighbor_cell_info g_neigh_cells[] =
{
    {
        .mode = LTE_CELL_MODE_LTE,
        .freq_mode = LTE_FREQ_MODE_INTRA,
        .earfcn = "lte_ut_earfcn_0",
        .uarfcn = "lte_ut_uarfcn_0",
        .pcid = "lte_ut_pcid_0",
        .rsrq = "lte_ut_rsrq_0",
        .rsrp = "lte_ut_rsrp_0",
        .rssi = "lte_ut_rssi_0",
        .sinr = "lte_ut_sinr_0",
        .srxlev_base_station = "lte_ut_srxlev_base_station_0",
        .cell_resel_priority = "lte_ut_cell_resel_priority_0",
        .s_non_intra_search = "lte_ut_s_non_intra_search_0",
        .thresh_serving_low = "lte_ut_thresh_serving_low_0",
        .s_intra_search = "lte_ut_s_intra_search_0",
        .thresh_x_low = "lte_ut_thresh_x_low_0",
        .thresh_x_high = "lte_ut_thresh_x_high_0",
        .psc = "lte_ut_psc",
        .rscp = "lte_ut_rscp_0",
        .ecno = "lte_ut_ecno_0",
        .set = "lte_ut_set_0",
        .rank = "lte_ut_rank_0",
        .cellid = "lte_ut_cellid_0",
        .srxlev_inter_freq = "lte_ut_srxlev_inter_freq_0",
    },
    {
        .mode = LTE_CELL_MODE_WCDMA,
        .freq_mode = LTE_FREQ_MODE_INTER,
        .earfcn = "lte_ut_earfcn_1",
        .uarfcn = "lte_ut_uarfcn_1",
        .pcid = "lte_ut_pcid_1",
        .rsrq = "lte_ut_rsrq_1",
        .rsrp = "lte_ut_rsrp_1",
        .rssi = "lte_ut_rssi_1",
        .sinr = "lte_ut_sinr_1",
        .srxlev_base_station = "lte_ut_srxlev_base_station_1",
        .cell_resel_priority = "lte_ut_cell_resel_priority_1",
        .s_non_intra_search = "lte_ut_s_non_intra_search_1",
        .thresh_serving_low = "lte_ut_thresh_serving_low_1",
        .s_intra_search = "lte_ut_s_intra_search_1",
        .thresh_x_low = "lte_ut_thresh_x_low_1",
        .thresh_x_high = "lte_ut_thresh_x_high_1",
        .psc = "lte_ut_psc",
        .rscp = "lte_ut_rscp_1",
        .ecno = "lte_ut_ecno_1",
        .set = "lte_ut_set_1",
        .rank = "lte_ut_rank_1",
        .cellid = "lte_ut_cellid_1",
        .srxlev_inter_freq = "lte_ut_srxlev_inter_freq_1",
    }
};

char *pb_file = "lte_ut_proto.bin";


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
 * @brief called by the Unity framework before every single test
 */
void
setUp(void)
{
    return;
}


/**
 * @brief called by the Unity framework after every single test
 */
void
tearDown(void)
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
 * @brief test setting the lte_info field of a report
 */
void
test_lte_set_info(void)
{
    bool ret;

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    ret = lte_info_set_info(&g_lte_info, g_report);
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
 * @brief test setting a signal quality info
 */
void
test_lte_set_sig_qual(void)
{
    bool ret;

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    ret = lte_info_set_lte_sig_qual(&g_sig_qual_info, g_report);
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

    /* Add the interface name */
    ret = lte_info_set_if_name(g_ifname, g_report);

    /* Validate the addition of the interface name */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->if_name);

    /* Add the signal quality info */
    ret = lte_info_set_lte_sig_qual(&g_sig_qual_info, g_report);

    /* Validate the addition of the signal quality info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->lte_sig_qual);

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
}


int
main(int argc, char *argv[])
{
    /* Set the logs to stdout */
    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    RUN_TEST(test_lte_set_info);
    RUN_TEST(test_lte_set_neigh_cell_info);
    RUN_TEST(test_lte_set_sig_qual);
    RUN_TEST(test_lte_set_srv_cell);
    RUN_TEST(test_lte_set_report);
    RUN_TEST(test_lte_serialize_report);

    return UNITY_END();
}
