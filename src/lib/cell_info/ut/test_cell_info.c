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

#include "cell_info.h"
#include "memutil.h"
#include "log.h"
#include "unity.h"
#include "target.h"
#include "qm_conn.h"
#include "unit_test_utils.h"

const char *test_name = "cell_info_tests";

struct cell_net_neighbor_cell_info *g_neigh_cell = NULL;
struct cell_net_lte_sca_info *g_lte_sca_info = NULL;
struct cell_pdp_ctx_dynamic_params_info *g_pdp_params_info;
struct cell_info_packed_buffer *g_serialized = NULL;
struct cell_nr5g_cell_info *g_nrg_sca_info = NULL;
struct cell_info_report *g_report = NULL;

char *g_deployment = "dog1";
char g_mqtt_topic[256];

#define NEIGH_CELL_COUNT 4
#define LTE_SCA_CELL_COUNT 4
#define PDP_CELL_COUNT 4
#define NRG_SCA_CELL_COUNT 4

struct cell_common_header g_common_header =
{
    .request_id = 1,
    .if_name = "rmnet_data0",
    .node_id = "HC8490008E",
    .location_id = "602e11e768b6592af397e9f2",
    .imei = "861364040104042",
    .imsi = "222013410161198",
    .iccid = "89011703278749636455",
    .modem_info = "Acme 12345X Revision: Acme123456789X",
    .reported_at = 0,
};

struct cell_net_info g_cell_net_info =
{
    .net_status = CELL_NET_REG_STAT_REG,
    .mcc = 310,
    .mnc = 410,
    .tac = 8,
    .service_provider = "T-Mobile",
    .sim_type = CELL_SIM_TYPE_PSIM,
    .sim_status = CELL_SIM_STATUS_INSERTED,
    .active_sim_slot = 1,
    .rssi = -71,
    .ber = 99,
    .last_healthcheck_success = 123456789,
    .healthcheck_failures = 0,
    .endc = CELL_ENDC_NOT_SUPPORTED,
    .mode = CELL_MODE_NR5G_SA,
};

struct cell_data_usage g_cell_data_usage =
{
    .rx_bytes = 123456789,
    .tx_bytes = 0xABCDEF123,
};

enum cellular_mode g_mode = CELL_MODE_NR5G_SA;

struct lte_serving_cell_info g_srv_cell_info =
{
    .state = SERVING_CELL_NOCONN,
    .fdd_tdd_mode = FDD_TDD_MODE_TDD,
    .cellid = 0xA1FBF11,
    .pcid = 218,
    .uarfcn = 0,
    .earfcn = 5110,
    .band = 12,
    .ul_bandwidth = CELL_BANDWIDTH_10_MHZ,
    .dl_bandwidth = CELL_BANDWIDTH_10_MHZ,
    .tac = 0x8B1E,
    .rsrp = -101,
    .rsrq = -15,
    .rssi = -69,
    .sinr = 10,
    .srxlev = 22,
    .endc = CELL_ENDC_SUPPORTED,
};


struct cell_net_neighbor_cell_info g_neigh_cells[] =
{
    {
        .freq_mode = CELL_FREQ_MODE_INTRA,
        .earfcn = 5035,
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
        .cell_set = CELL_NEIGHBOR_CELL_SET_ACTIVE_SET,
        .rank = -190,
        .cellid = 0,
        .inter_freq_srxlev = -90,
    },
    {
        .freq_mode = CELL_FREQ_MODE_INTER,
        .earfcn = 5035,
        .pcid = 160,
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
        .cell_set = CELL_NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR,
        .rank = -190,
        .cellid = 0,
        .inter_freq_srxlev = -90,
    },
    {
        .freq_mode = CELL_FREQ_MODE_WCDMA,
        .earfcn = 5035,
        .pcid = 161,
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
        .cell_set = CELL_NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR,
        .rank = -190,
        .cellid = 0,
        .inter_freq_srxlev = -90,
    },
    {

        .freq_mode = CELL_FREQ_MODE_WCDMA_LTE,
        .earfcn = 5035,
        .pcid = 162,
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
        .cell_set = CELL_NEIGHBOR_CELL_SET_ASYNC_NEIGHBOR,
        .rank = -190,
        .cellid = 0,
        .inter_freq_srxlev = -90,
    }
};

struct cell_net_pca_info g_pca_info =
{
    .lcc = CELL_PCC,
    .freq = 101,
    .bandwidth = CELL_BANDWIDTH_20_MHZ,
    .pcell_state = CELL_REGISTERED,
    .pcid = 123,
    .rsrp = -99,
    .rsrq = -129,
    .rssi = -69,
    .sinr = 5,
};

struct cell_net_lte_sca_info g_lte_sca_cells[] =
{
    {
        .lcc = CELL_SCC,
        .freq = 101,
        .bandwidth = CELL_BANDWIDTH_20_MHZ,
        .scell_state = CELL_CONFIGURERD_ACTIVATED,
        .pcid = 222,
        .rsrp = -75,
        .rsrq = -101,
        .rssi = -76,
        .sinr = 15,
    },
    {
        .lcc = CELL_SCC,
        .freq = 101,
        .bandwidth = CELL_BANDWIDTH_30_MHZ,
        .scell_state = CELL_CONFIGURERD_ACTIVATED,
        .pcid = 100,
        .rsrp = -90,
        .rsrq = -101,
        .rssi = -76,
        .sinr = 15,
    },
    {
        .lcc = CELL_SCC,
        .freq = 101,
        .bandwidth = CELL_BANDWIDTH_60_MHZ,
        .scell_state = CELL_CONFIGURERD_ACTIVATED,
        .pcid = 400,
        .rsrp = -100,
        .rsrq = -101,
        .rssi = -76,
        .sinr = 15,
    },
    {
        .lcc = CELL_SCC,
        .freq = 101,
        .bandwidth = CELL_BANDWIDTH_400_MHZ,
        .scell_state = CELL_CONFIGURERD_ACTIVATED,
        .pcid = 10,
        .rsrp = -110,
        .rsrq = -101,
        .rssi = -76,
        .sinr = 15,
    }
};

struct cell_pdp_ctx_dynamic_params_info g_pdp_params_cells[] =
{
    {
        .cid = 1,
        .bearer_id = 5,
        .apn = "ATT",
        .local_addr = "100.111.126.222",
        .subnetmask = "38.0.16.16.176.43.232.65.0.0.0.34.166.143.216.1",
        .gw_addr = "198.224.173.135",
        .dns_prim_addr = "32.1.72.136.0.104.255.0.6.8.0.13.0.0.0.0",
        .dns_sec_addr = "",
        .p_cscf_prim_addr = "",
        .p_cscf_sec_addr = "",
        .im_cn_signalling_flag = 0,
        .lipaindication = 0,
    },
    {
        .cid = 2,
        .bearer_id = 5,
        .apn = "VZWINTERNET",
        .local_addr = "100.111.126.222",
        .subnetmask = "38.0.16.16.176.43.232.65.0.0.0.34.166.143.216.1",
        .gw_addr = "198.224.173.135",
        .dns_prim_addr = "32.1.72.136.0.104.255.0.6.8.0.13.0.0.0.0",
        .dns_sec_addr = "",
        .p_cscf_prim_addr = "",
        .p_cscf_sec_addr = "",
        .im_cn_signalling_flag = 0,
        .lipaindication = 0,
    },
    {
        .cid = 3,
        .bearer_id = 5,
        .apn = "TMOBILE",
        .local_addr = "100.111.126.222",
        .subnetmask = "38.0.16.16.176.43.232.65.0.0.0.34.166.143.216.1",
        .gw_addr = "198.224.173.135",
        .dns_prim_addr = "32.1.72.136.0.104.255.0.6.8.0.13.0.0.0.0",
        .dns_sec_addr = "",
        .p_cscf_prim_addr = "",
        .p_cscf_sec_addr = "",
        .im_cn_signalling_flag = 0,
        .lipaindication = 0,
    },
    {
        .cid = 4,
        .bearer_id = 5,
        .apn = "TPG",
        .local_addr = "100.111.126.222",
        .subnetmask = "38.0.16.16.176.43.232.65.0.0.0.34.166.143.216.1",
        .gw_addr = "198.224.173.135",
        .dns_prim_addr = "32.1.72.136.0.104.255.0.6.8.0.13.0.0.0.0",
        .dns_sec_addr = "",
        .p_cscf_prim_addr = "",
        .p_cscf_sec_addr = "",
        .im_cn_signalling_flag = 0,
        .lipaindication = 0,
    }
};

struct cell_nr5g_cell_info g_nr5g_sa_srv_cell_info =
{
    .state = SERVING_CELL_NOCONN,
    .fdd_tdd_mode = FDD_TDD_MODE_TDD,
    .mcc = 460,
    .mnc = 11,
    .cellid = 0xB38751,
    .pcid = 573,
    .tac = 0xfffffe,
    .arfcn = 633984,
    .band = 78,
    .ul_bandwidth = 3,
    .dl_bandwidth = 3,
    .rsrp = -100,
    .rsrq = -14,
    .sinr = 5,
    .scs = NR_SCS_15_KHZ,
    .srxlev = 1288,
    .layers = 4,
    .mcs = 31,
    .modulation = 3,
};

struct cell_nr5g_cell_info g_nr5g_nsa_srv_cell_info =
{
    .state = SERVING_CELL_NOCONN,
    .fdd_tdd_mode = FDD_TDD_MODE_TDD,
    .mcc = 460,
    .mnc = 11,
    .cellid = 0xB38751,
    .pcid = 573,
    .arfcn = 633984,
    .tac = 0xfffffe,
    .band = 78,
    .rsrp = -100,
    .rsrq = -14,
    .rssi = -69,
    .sinr = 10,
    .cqi = 15,
    .tx_power = 19,
    .ul_bandwidth = 3,
    .dl_bandwidth = 3,
    .scs = NR_SCS_30_KHZ,
    .layers = 1,
    .mcs = 10,
    .modulation = 6,
};

struct cell_nr5g_cell_info g_nrg_sca_cells[] =
{
    {
        .state = SERVING_CELL_NOCONN,
        .fdd_tdd_mode = FDD_TDD_MODE_TDD,
        .mcc = 460,
        .mnc = 11,
        .cellid = 0xB38751,
        .pcid = 573,
        .tac = 0xfffffe,
        .arfcn = 633984,
        .band = 78,
        .ul_bandwidth = 3,
        .dl_bandwidth = 3,
        .rsrp = -100,
        .rsrq = -14,
        .sinr = 5,
        .scs = NR_SCS_30_KHZ,
        .srxlev = 1288,
    },
    {
        .state = SERVING_CELL_NOCONN,
        .fdd_tdd_mode = FDD_TDD_MODE_TDD,
        .mcc = 460,
        .mnc = 11,
        .cellid = 0xB38751,
        .pcid = 573,
        .tac = 0xfffffe,
        .arfcn = 633984,
        .band = 78,
        .ul_bandwidth = 3,
        .dl_bandwidth = 3,
        .rsrp = -100,
        .rsrq = -14,
        .sinr = 5,
        .scs = NR_SCS_30_KHZ,
        .srxlev = 1288,
    },
    {
        .state = SERVING_CELL_NOCONN,
        .fdd_tdd_mode = FDD_TDD_MODE_TDD,
        .mcc = 460,
        .mnc = 11,
        .cellid = 0xB38751,
        .pcid = 573,
        .tac = 0xfffffe,
        .arfcn = 633984,
        .band = 78,
        .ul_bandwidth = 3,
        .dl_bandwidth = 3,
        .rsrp = -100,
        .rsrq = -14,
        .sinr = 5,
        .scs = NR_SCS_30_KHZ,
        .srxlev = 1288,
    },
    {
        .state = SERVING_CELL_NOCONN,
        .fdd_tdd_mode = FDD_TDD_MODE_TDD,
        .mcc = 460,
        .mnc = 11,
        .cellid = 0xB38751,
        .pcid = 573,
        .tac = 0xfffffe,
        .arfcn = 633984,
        .band = 78,
        .ul_bandwidth = 3,
        .dl_bandwidth = 3,
        .rsrp = -100,
        .rsrq = -14,
        .sinr = 5,
        .scs = NR_SCS_30_KHZ,
        .srxlev = 1288,
    }
};

char *pb_file = "/tmp/cell_ut_proto.bin";

static void
test_cell_send_report(char *topic, struct cell_info_packed_buffer *pb)
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
pb2file(struct cell_info_packed_buffer *pb, char *fpath)
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
cell_info_tearDown(void)
{
    cell_info_free_report(g_report);
    g_report = NULL;

    cell_info_free_neigh_cell(g_neigh_cell);
    g_neigh_cell = NULL;

    cell_info_free_lte_sca(g_lte_sca_info);
    g_lte_sca_info = NULL;

    cell_info_free_pdp_ctx(g_pdp_params_info);
    g_pdp_params_info = NULL;

    cell_info_free_nrg_sca(g_nrg_sca_info);
    g_nrg_sca_info = NULL;

    cell_info_free_packed_buffer(g_serialized);
    g_serialized = NULL;

    return;
}


/**
 * @brief test setting the common header of a report
 */
void
test_cell_set_common_header(void)
{
    bool ret;
    time_t now = time(NULL);

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    g_common_header.reported_at = now;
    ret = cell_info_set_common_header (&g_common_header, g_report);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test setting the cell_net_info field of a report
 */
void
test_cell_set_net_info(void)
{
    bool ret;

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    ret = cell_info_set_net_info(&g_cell_net_info, g_report);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test setting the cell_data_usage field of a report
 */
void
test_cell_set_data_usage(void)
{
    bool ret;

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    ret = cell_info_set_data_usage(&g_cell_data_usage, g_report);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test setting a neighbor cell info
 */
void
test_cell_set_neigh_cell_info(void)
{
    bool ret;

    g_neigh_cell = CALLOC(1, sizeof(*g_neigh_cell));
    TEST_ASSERT_NOT_NULL(g_neigh_cell);

    ret = cell_info_set_neigh_cell(&g_neigh_cells[0], g_neigh_cell);
    TEST_ASSERT_TRUE(ret);
}


/**
 * @brief test setting a serving cell info
 */
void
test_cell_set_srv_cell(void)
{
    bool ret;

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    ret = cell_info_set_serving_cell(&g_srv_cell_info, g_report);
    TEST_ASSERT_TRUE(ret);
}


/**
 * @brief test setting a primary carrier aggregation
 */
void
test_cell_set_pca(void)
{
    bool ret;

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    ret = cell_info_set_pca(&g_pca_info, g_report);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test setting lte secondary carrier aggregation
 */
void
test_cell_set_lte_sca(void)
{
    bool ret;

    g_lte_sca_info = CALLOC(1, sizeof(*g_lte_sca_info));
    TEST_ASSERT_NOT_NULL(g_lte_sca_info);

    ret = cell_info_set_lte_sca(&g_lte_sca_cells[0], g_lte_sca_info);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test dynamic pdp context parameters
 */
void
test_cell_pdp_ctx_dyn_info(void)
{
    bool ret;

    g_pdp_params_info = CALLOC(1, sizeof(*g_pdp_params_info));
    TEST_ASSERT_NOT_NULL(g_pdp_params_info);

    ret = cell_info_set_pdp_ctx(&g_pdp_params_cells[0], g_pdp_params_info);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test setting for nr5g sa serving cell info
 */
void
test_cell_set_nr5g_sa_srv_cell(void)
{
    bool ret;

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    ret = cell_info_set_nr5g_sa_serving_cell(&g_nr5g_sa_srv_cell_info, g_report);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test setting for nr5g nsa serving cell info
 */
void
test_cell_set_nr5g_nsa_srv_cell(void)
{
    bool ret;

    g_report = CALLOC(1, sizeof(*g_report));
    TEST_ASSERT_NOT_NULL(g_report);

    ret = cell_info_set_nr5g_nsa_serving_cell(&g_nr5g_nsa_srv_cell_info, g_report);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief test setting nrg secondary carrier aggregation
 */
void
test_cell_set_nrg_sca(void)
{
    bool ret;

    g_nrg_sca_info = CALLOC(1, sizeof(*g_nrg_sca_info));
    TEST_ASSERT_NOT_NULL(g_nrg_sca_info);

    ret = cell_info_set_nrg_sca(&g_nrg_sca_cells[0], g_nrg_sca_info);
    TEST_ASSERT_TRUE(ret);
}

/**
 * @brief set a full report
 */
void
cell_ut_set_report(void)
{
    bool ret;

    /* Allocate a report provisioning NEIGH_CELL_COUNT neighbor cells */
    g_report = cell_info_allocate_report(NEIGH_CELL_COUNT, LTE_SCA_CELL_COUNT, PDP_CELL_COUNT, NRG_SCA_CELL_COUNT);
    TEST_ASSERT_NOT_NULL(g_report);

    /* Add one report cell */
    ret = cell_info_add_neigh_cell(&g_neigh_cells[0], g_report);

    /* validate the addition of the neighbor cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_neigh_cell_info[0]);
    TEST_ASSERT_EQUAL_UINT(1, g_report->cur_neigh_cell_idx);

    /* Add another report cell */
    ret = cell_info_add_neigh_cell(&g_neigh_cells[1], g_report);

    /* validate the addition of the neighbor cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_neigh_cell_info[1]);
    TEST_ASSERT_EQUAL_UINT(2, g_report->cur_neigh_cell_idx);

    /* Add another report cell */
    ret = cell_info_add_neigh_cell(&g_neigh_cells[2], g_report);

    /* validate the addition of the neighbor cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_neigh_cell_info[2]);
    TEST_ASSERT_EQUAL_UINT(3, g_report->cur_neigh_cell_idx);

    /* Add another report cell */
    ret = cell_info_add_neigh_cell(&g_neigh_cells[3], g_report);

    /* validate the addition of the neighbor cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_neigh_cell_info[3]);
    TEST_ASSERT_EQUAL_UINT(4, g_report->cur_neigh_cell_idx);

    /* Validate the addition of the common header */
    ret = cell_info_set_common_header(&g_common_header, g_report);

    /* Validate the addition of the common header */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->header);

    /* Add the cell net info */
    ret = cell_info_set_net_info(&g_cell_net_info, g_report);

    /* Validate the addition of the cell net info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_net_info);

    /* Add the cell data usage */
    ret = cell_info_set_data_usage(&g_cell_data_usage, g_report);

    /* Validate the addition of the cell data usage */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_data_usage);

    /* Add the serving cell info */
    ret = cell_info_set_serving_cell(&g_srv_cell_info, g_report);

    /* Validate the addition of the serving cell info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_srv_cell);

    /* Add one sca report cell */
    ret = cell_info_add_lte_sca(&g_lte_sca_cells[0], g_report);

    /* validate the addition of the sca cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_lte_sca_info[0]);
    TEST_ASSERT_EQUAL_UINT(1, g_report->cur_lte_sca_cell_idx);

    /* Add another report cell */
    ret = cell_info_add_lte_sca(&g_lte_sca_cells[1], g_report);

    /* validate the addition of the sca cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_lte_sca_info[1]);
    TEST_ASSERT_EQUAL_UINT(2, g_report->cur_lte_sca_cell_idx);

    /* Add another report cell */
    ret = cell_info_add_lte_sca(&g_lte_sca_cells[2], g_report);

    /* validate the addition of the sca cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_lte_sca_info[2]);
    TEST_ASSERT_EQUAL_UINT(3, g_report->cur_lte_sca_cell_idx);

    /* Add another report cell */
    ret = cell_info_add_lte_sca(&g_lte_sca_cells[3], g_report);

    /* validate the addition of the sca cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_lte_sca_info[3]);
    TEST_ASSERT_EQUAL_UINT(4, g_report->cur_lte_sca_cell_idx);

    /* Add pdp context dynamic parameters */
    ret = cell_info_add_pdp_ctx(&g_pdp_params_cells[0], g_report);

    /* Validate the addition of pdp context info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_pdp_ctx_info[0]);
    TEST_ASSERT_EQUAL_UINT(1, g_report->cur_pdp_idx);

    /* Add another pdp context */
    ret = cell_info_add_pdp_ctx(&g_pdp_params_cells[1], g_report);

    /* Validate the addition of pdp context info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_pdp_ctx_info[1]);
    TEST_ASSERT_EQUAL_UINT(2, g_report->cur_pdp_idx);

    /* Add pdp context dynamic parameters */
    ret = cell_info_add_pdp_ctx(&g_pdp_params_cells[2], g_report);

    /* Validate the addition of pdp context info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_pdp_ctx_info[2]);
    TEST_ASSERT_EQUAL_UINT(3, g_report->cur_pdp_idx);

    /* Add pdp context dynamic parameters */
    ret = cell_info_add_pdp_ctx(&g_pdp_params_cells[3], g_report);

    /* Validate the addition of the serving cell info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_pdp_ctx_info[3]);
    TEST_ASSERT_EQUAL_UINT(4, g_report->cur_pdp_idx);

    /* Add nr5g sa info */
    ret = cell_info_set_nr5g_sa_serving_cell(&g_nr5g_sa_srv_cell_info, g_report);

    /* Validate the addition of the serving cell info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->nr5g_sa_srv_cell);

    /* Add nr5g nsa info */
    ret = cell_info_set_nr5g_nsa_serving_cell(&g_nr5g_nsa_srv_cell_info, g_report);

    /* Validate the addition of the nsa serving cell info */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->nr5g_nsa_srv_cell);

    /* Add one sca report cell */
    ret = cell_info_add_nrg_sca(&g_nrg_sca_cells[0], g_report);

    /* validate the addition of the sca cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_nrg_sca_info[0]);
    TEST_ASSERT_EQUAL_UINT(1, g_report->cur_nrg_sca_cell_idx);


    /* Add another report cell */
    ret = cell_info_add_nrg_sca(&g_nrg_sca_cells[1], g_report);

    /* validate the addition of the sca cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_nrg_sca_info[1]);
    TEST_ASSERT_EQUAL_UINT(2, g_report->cur_nrg_sca_cell_idx);

    /* Add another report cell */
    ret = cell_info_add_nrg_sca(&g_nrg_sca_cells[2], g_report);

    /* validate the addition of the sca cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_nrg_sca_info[2]);
    TEST_ASSERT_EQUAL_UINT(3, g_report->cur_nrg_sca_cell_idx);

    /* Add another report cell */
    ret = cell_info_add_nrg_sca(&g_nrg_sca_cells[3], g_report);

    /* validate the addition of the sca cell */
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_NOT_NULL(g_report->cell_nrg_sca_info[3]);
    TEST_ASSERT_EQUAL_UINT(4, g_report->cur_nrg_sca_cell_idx);
}


/**
 * @brief test setting a full report
 */
void
test_cell_set_report(void)
{
    cell_ut_set_report();
}


/**
 * @brief test serializing a full report
 */
void
test_cell_serialize_report(void)
{
    cell_ut_set_report();
    g_serialized = serialize_cell_info(g_report);

    /* Save the serialized protobuf in a file */
    pb2file(g_serialized, pb_file);
    test_cell_send_report(g_mqtt_topic, g_serialized);
}


/**
 * @brief create mqtt topic
 */
void
test_cell_set_mqtt_topic(void)
{
    int rc;

    rc = snprintf(g_mqtt_topic, sizeof(g_mqtt_topic), "dev-test/CellStats/%s/%s/%s",
                  g_deployment, g_common_header.node_id,
                  g_common_header.location_id);
    TEST_ASSERT_NOT_EQUAL_INT(0, rc);
}


int
main(int argc, char *argv[])
{
    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, NULL, cell_info_tearDown);

    RUN_TEST(test_cell_set_mqtt_topic);
    RUN_TEST(test_cell_set_common_header);
    RUN_TEST(test_cell_set_net_info);
    RUN_TEST(test_cell_set_data_usage);
    RUN_TEST(test_cell_set_neigh_cell_info);
    RUN_TEST(test_cell_set_srv_cell);
    RUN_TEST(test_cell_set_pca);
    RUN_TEST(test_cell_set_lte_sca);
    RUN_TEST(test_cell_pdp_ctx_dyn_info);
    RUN_TEST(test_cell_set_nr5g_sa_srv_cell);
    RUN_TEST(test_cell_set_nr5g_nsa_srv_cell);
    RUN_TEST(test_cell_set_nrg_sca);
    RUN_TEST(test_cell_set_report);
    RUN_TEST(test_cell_serialize_report);

    return ut_fini();
}
