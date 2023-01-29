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
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdlib.h>

#include "log.h"
#include "os.h"
#include "memutil.h"
#include "lte_info.h"
#include "ltem_mgr.h"
#include "ltem_lte_ut.h"
#include "target.h"
#include "unity.h"
#include "osn_lte_modem.h"
#include "unit_test_utils.h"

static ltem_mgr_t ltem_mgr;

const char *test_name = "ltem_tests";

struct schema_AWLAN_Node g_awlan_nodes_new[] =
{
    {
        .mqtt_headers_keys =
        {
            "nodeId",
            "locationId",
        },
        .mqtt_headers =
        {
            "HC83C0005B",
            "602e11e768b6592af397e9f2",
        },
        .mqtt_headers_len = 2,

        .mqtt_topics_keys =
        {
            "Crash.Reports",
            "DHCP.Signatures",
            "DNS.Queries",
            "HTTP.Requests",
            "LteStats",
            "ObjectStore",
            "UPnP.Devices",
            "WifiBlaster.Results",
            "aggregatedStats",
        },
        .mqtt_topics =
        {
            "Crash/Reports/dog1/HC83C0005B/602e11e768b6592af397e9f2",
            "DHCP/Signatures/dog1/HC83C0005B/602e11e768b6592af397e9f2",
            "DNS/Queries/dog1/HC83C0005B/602e11e768b6592af397e9f2",
            "HTTP/Requests/dog1/HC83C0005B/602e11e768b6592af397e9f2",
            "dev-test/LteStats/dog1/HC83C0005B/602e11e768b6592af397e9f2",
            "ObjectStore/dog1/HC83C0005B/602e11e768b6592af397e9f2",
            "UPnP/Devices/dog1/HC83C0005B/602e11e768b6592af397e9f2",
            "WifiBlaster/dog1/HC83C0005B/602e11e768b6592af397e9f2",
            "aggregatedStats/dog1/HC83C0005B/602e11e768b6592af397e9f2",
        },
        .mqtt_topics_len = 9,
    },
};

struct schema_AWLAN_Node g_node_conf_old[] =
{
    {
        .mqtt_headers_keys =
        {
            "nodeId",
            "locationId",
        },
        .mqtt_headers =
        {
            "0",
            "1",
        },
        .mqtt_headers_len = 2,

        .mqtt_topics_keys =
        {
            "TDF1993",
        },
        .mqtt_topics =
        {
            "EpoHghTst",
        },
        .mqtt_topics_len = 1,
    },
};

ltem_mgr_t *
ltem_get_mgr(void)
{
    return &ltem_mgr;
}

void
ltem_setup_modem_info(osn_lte_modem_info_t *modem_info)
{
    strcpy(modem_info->iccid, "80000000000000000003");
    strcpy(modem_info->imei, "860000000000012");
    strcpy(modem_info->imsi, "120000000000111");
}

void
ltem_populate_config(lte_config_info_t *lte_config)
{
    STRSCPY(lte_config->if_name, "wwan0");
    lte_config->manager_enable = true;
    lte_config->lte_failover_enable = true;
    lte_config->ipv4_enable = true;
    lte_config->ipv6_enable = false;
    lte_config->force_use_lte = false;
    lte_config->active_simcard_slot = 0;
    lte_config->modem_enable = true;
    lte_config->report_interval = 60;
    STRSCPY(lte_config->apn, "Broadband");
}

void
ltem_populate_state(lte_state_info_t *lte_state)
{
    lte_state->lte_failover_active = false;
    lte_state->lte_failover_start = 100;
    lte_state->lte_failover_end = 200;
    lte_state->lte_failover_count = 1;
}

bool
ltem_ut_mgr_init(struct ev_loop *loop)
{
    lte_config_info_t *lte_config;
    lte_state_info_t *lte_state;
    lte_route_info_t *lte_route;

    ltem_mgr_t *mgr = ltem_get_mgr();
    mgr->modem_info = osn_get_modem_info();

    ltem_setup_modem_info(mgr->modem_info);

    mgr->loop = loop;

    lte_config = CALLOC(1, sizeof(lte_config_info_t));
    ltem_populate_config(lte_config);

    lte_state = CALLOC(1, sizeof(lte_state_info_t));
    ltem_populate_state(lte_state);

    lte_route = CALLOC(1, sizeof(lte_route_info_t));

    mgr->lte_config_info = lte_config;
    mgr->lte_state_info = lte_state;
    mgr->lte_route = lte_route;

    return true;
}

int
ut_system(const char *cmd)
{
    return 0;
}

void
ltem_setup_ut_handlers(ltem_mgr_t *mgr)
{
    ltem_handlers_t *handlers;

    handlers = &mgr->handlers;

    handlers->ltem_mgr_init = ltem_ut_mgr_init;
    handlers->system_call = ut_system;
}

int
ut_check_modem_status(void)
{
    char *at_resp;
    int res;
    int i;

    char *cmd = "at";
    at_resp = lte_ut_run_microcom_cmd(cmd);
    res = osn_lte_parse_at_output(at_resp);
    if (!res) return res;
    for (i = 0; i < 10; i++)
    {
        sleep(1);
        at_resp = lte_ut_run_microcom_cmd(cmd);
        res = osn_lte_parse_at_output(at_resp);
        if (!res) break;
    }
    return res;
}

int
ut_lte_read_modem(void)
{
    int res;
    osn_lte_modem_info_t *modem_info;
    char *at_resp;
    lte_chip_info_t chip_info;
    lte_sim_insertion_status_t sim_status;
    lte_imei_t imei;
    lte_imsi_t imsi;
    lte_iccid_t iccid;
    lte_reg_status_t reg_status;
    lte_sig_qual_t sig_qual;
    lte_byte_counts_t byte_counts;
    lte_sim_slot_t sim_slot;
    lte_operator_t operator;
    lte_srv_cell_t srv_cell;
    lte_srv_cell_wcdma_t srv_cell_wcdma;
    lte_neigh_cell_intra_t neigh_cell_intra;
    gen_resp_tokens resp_tokens[MAX_RESP_TOKENS];

    modem_info = osn_get_modem_info();
    res = ut_check_modem_status();
    if (!res)
    {
        LOGI("%s: modem status: OK", __func__);
        modem_info->modem_present = true;
    }
    else
    {
        LOGI("%s: modem status: Not OK", __func__);
        modem_info->modem_present = false;
    }

    modem_info->sim_inserted = true;

    char *ati_cmd = "ati";
    at_resp = lte_ut_run_microcom_cmd(ati_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_chip_info(at_resp, &chip_info);
    if (res != 0)
    {
        LOGE("osn_lte_parse_chip_info:failed");
    }
    osn_lte_save_chip_info(&chip_info, modem_info);

    char *qsimstat_cmd = "at+qsimstat?";
    at_resp = lte_ut_run_microcom_cmd(qsimstat_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_sim_status(at_resp, &sim_status);
    if (res != 0)
    {
        LOGE("osn_lte_parse_sim_status:failed");
    }
    osn_lte_save_sim_status(&sim_status, modem_info);

    char *gsn_cmd = "at+gsn";
    at_resp = lte_ut_run_microcom_cmd(gsn_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_imei(at_resp, &imei);
    if (res != 0)
    {
        LOGE("osn_lte_parse_imei:failed");
    }
    osn_lte_save_imei(&imei, modem_info);

    char *imsi_cmd = "at+cimi";
    at_resp = lte_ut_run_microcom_cmd(imsi_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_imsi(at_resp, &imsi);
    if (res != 0)
    {
        LOGE("osn_lte_parse_imsi:failed");
    }
    osn_lte_save_imsi(&imsi, modem_info);

    char *iccid_cmd = "at+qccid";
    at_resp = lte_ut_run_microcom_cmd(iccid_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_iccid(at_resp, &iccid);
    if (res != 0)
    {
        LOGE("osn_lte_parse_iccid:failed");
    }
    osn_lte_save_iccid(&iccid, modem_info);

    char *creg_cmd = "at+creg?"; // net reg status
    at_resp = lte_ut_run_microcom_cmd(creg_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_reg_status(at_resp, &reg_status);
    if (res != 0)
    {
        LOGE("osn_lte_parse_reg_status:failed");
    }
    osn_lte_save_reg_status(&reg_status, modem_info);

    char *csq_cmd = "at+csq"; // rssi, ber
    at_resp = lte_ut_run_microcom_cmd(csq_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_sig_qual(at_resp, &sig_qual);
    if (res != 0)
    {
        LOGE("osn_lte_parse_sig_qual:failed");
    }
    osn_lte_save_sig_qual(&sig_qual, modem_info);

    char *qgdcnt_cmd = "at+qgdcnt?"; //tx/rx bytes
    at_resp = lte_ut_run_microcom_cmd(qgdcnt_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_byte_counts(at_resp, &byte_counts);
    if (res != 0)
    {
        LOGE("osn_lte_parse_byte_counts:failed");
    }
    osn_lte_save_byte_counts(&byte_counts, modem_info);

    char *qdsim_cmd = "at+qdsim?";
    at_resp = lte_ut_run_microcom_cmd(qdsim_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_sim_slot(at_resp, &sim_slot);
    if (res != 0)
    {
        LOGE("osn_lte_parse_sim_slot:failed");
    }
    osn_lte_save_sim_slot(&sim_slot, modem_info);

    char *cops_cmd = "at+cops?";
    at_resp = lte_ut_run_microcom_cmd(cops_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_operator(at_resp, &operator);
    if (res != 0)
    {
        LOGE("osn_lte_parse_operator:failed");
    }
    osn_lte_save_operator(&operator, modem_info);

    char *nr_5g_sa_srv_cell_cmd = "nr_5g_sa_at+qeng=\\\"servingcell\\\"";
    at_resp = lte_ut_run_microcom_cmd(nr_5g_sa_srv_cell_cmd);
    if (!at_resp) return -1;
    res = osn_gen_parser_sa(at_resp, resp_tokens);
    if (res != 0)
    {
        LOGE("osn_lte_parse_s5g_sa_erving_cell:failed");
    }
    osn_nr5g_save_serving_cell_5g_sa(resp_tokens);

    char *srv_cell_cmd = "at+qeng=\\\"servingcell\\\"";
    at_resp = lte_ut_run_microcom_cmd(srv_cell_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_serving_cell(at_resp, &srv_cell, &srv_cell_wcdma);
    if (res != 0)
    {
        LOGE("osn_lte_parse_serving_cell:failed");
    }
    osn_lte_save_serving_cell(&srv_cell, &srv_cell_wcdma, modem_info);

    char *neigh_cell_cmd = "at+qeng=\\\"neighbourcell\\\"";
    at_resp = lte_ut_run_microcom_cmd(neigh_cell_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_neigh_cell_intra(at_resp, &neigh_cell_intra);
    if (res != 0)
    {
        LOGE("osn_lte_parse_neigh_cell_intra:failed");
    }
    osn_lte_save_neigh_cell_intra(&neigh_cell_intra, modem_info);

    lte_neigh_cell_inter_t neigh_cell_inter;
    res = osn_lte_parse_neigh_cell_inter(at_resp, &neigh_cell_inter);
    if (res != 0)
    {
        LOGE("osn_lte_parse_neigh_cell_inter:failed");
    }
    osn_lte_save_neigh_cell_inter(&neigh_cell_inter, modem_info);

    lte_pdp_context_t  pdp_context;
    char *pdp_cmd = "at+cgdcont?";
    at_resp = lte_ut_run_microcom_cmd(pdp_cmd);
    if (!at_resp) return -1;
    res = osn_lte_parse_pdp_context(at_resp, &pdp_context);
    if (res != 0)
    {
        LOGE("osn_lte_parse_pdp_context:failed");
    }

    return 0;
}

int
ltem_ut_build_mqtt_report(time_t now)
{
    int res;

    res = ut_lte_read_modem();
    if (res < 0) return res;

    res = lte_serialize_report();

    lte_mqtt_cleanup();

    return res;
}

void
test_ltem_build_mqtt_report(void)
{
    int ret;
    time_t now = time(NULL);

    ret = ltem_ut_build_mqtt_report(now);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

/**
 * @brief update mqtt_headers
 *
 * setUp() sets the mqtt headers. Validate the original values,
 * update and validate.
 */
void
test_add_awlan_headers(void)
{
    ltem_mgr_t *mgr;
    char *expected;
    char *node_id;
    char *location_id;
    char *topic;
    char *key;

    mgr = ltem_get_mgr();

    /* Validate original headers */
    node_id = mgr->node_id;
    TEST_ASSERT_NOT_NULL(node_id);
    expected = g_awlan_nodes_new[0].mqtt_headers[0];
    TEST_ASSERT_EQUAL_STRING(expected, node_id);
    location_id = mgr->location_id;
    TEST_ASSERT_NOT_NULL(location_id);
    expected = g_awlan_nodes_new[0].mqtt_headers[1];
    TEST_ASSERT_EQUAL_STRING(expected, location_id);

    topic = mgr->topic;
    TEST_ASSERT_NOT_NULL(topic);
    expected = g_awlan_nodes_new[0].mqtt_topics[4];
    TEST_ASSERT_EQUAL_STRING(expected, topic);
    key = "LteStats";
    TEST_ASSERT_NOT_NULL(key);
    expected = g_awlan_nodes_new[0].mqtt_topics_keys[4];
    TEST_ASSERT_EQUAL_STRING(expected, key);
}

int
main(int argc, char **argv)
{
    ltem_mgr_t *mgr;
    struct ev_loop *loop = EV_DEFAULT;
    bool rc;

    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, NULL, NULL);

    ltem_set_lte_state(LTEM_LTE_STATE_INIT);

    mgr = ltem_get_mgr();
    memset(mgr, 0, sizeof(ltem_mgr_t));

    ltem_setup_ut_handlers(mgr);
    rc = mgr->handlers.ltem_mgr_init(loop);
    if (!rc) return -1;

    ltem_ovsdb_update_awlan_node(&g_awlan_nodes_new[0]);

    RUN_TEST(test_ltem_build_mqtt_report);

    FREE(mgr->lte_config_info);
    FREE(mgr->lte_state_info);
    FREE(mgr->lte_route);

    return ut_fini();
}


