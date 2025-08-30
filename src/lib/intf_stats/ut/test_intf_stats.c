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

#include "intf_stats.h"
#include "unity.h"
#include "log.h"
#include "target.h"
#include "qm_conn.h"
#include "memutil.h"
#include "unit_test_utils.h"

char *test_name = "test_intf_stats";

struct intf_stats_test_mgr
{
    bool                        initialized;
    bool                        has_qm;
    char                        *f_name;
    intf_stats_report_data_t    report;
} g_test_mgr;

/******************************************************************************
 * Helper functions
******************************************************************************/

/**
 * @brief sends a serialized buffer over MQTT
 *
 * @param pb serialized buffer
 */
void
emit_report(packed_buffer_t *pb)
{
    qm_response_t res;
    bool ret;

    if (!g_test_mgr.has_qm) return;

    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG,
                              "dev-test/interfaceStats/a/b/c",
                              pb->buf, pb->len, &res);
    TEST_ASSERT_TRUE(ret);
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
pb2file(packed_buffer_t *pb, char *fpath)
{
    FILE *f = fopen(fpath, "w");
    size_t nwrite = fwrite(pb->buf, 1, pb->len, f);
    fclose(f);

    return nwrite;
}

/**
 * @brief reads the contents of a serialized buffer from a file
 *
 * @param fpath target file path
 * @param pb serialized buffer to be filled
 *
 * @return returns the number of bytes written
 */
static size_t
file2pb(char *fpath, packed_buffer_t *pb)
{
    FILE *f = fopen(fpath, "rb");
    size_t nread = fread(pb->buf, 1, pb->len, f);
    fclose(f);

    return nread;
}

/*****************************************************************************/

void
test_Intf__Stats__Report(void)
{
    char *location_id = "intf_stats_test_location";
    char *node_id = "4C718002B3";

    Interfaces__IntfStats__ObservationWindow **obs_pb_tbl;
    Interfaces__IntfStats__ObservationWindow  *obs_pb;
    Interfaces__IntfStats__ObservationPoint   *obs_p;
    Interfaces__IntfStats__IntfStats         **stats_pb_tbl;
    Interfaces__IntfStats__IntfStats          *stats_pb;
    packed_buffer_t                           *serialized;
    Interfaces__IntfStats__IntfReport         *report;

    size_t num_intfs_w0;
    size_t num_w;
    size_t len;
    void   *buf;

    /* Allocate serialization output structure */
    serialized = CALLOC(sizeof(packed_buffer_t), 1);

    /* Allocate protobuf */
    report = CALLOC(1, sizeof(*report));

    /* Initialize protobuf */
    interfaces__intf_stats__intf_report__init(report);

    /* Set reported field */
    report->has_reported_at = true;
    report->reported_at = 10;

    /* Set observation point */
    obs_p = CALLOC(1, sizeof(*obs_p));

    /* Initialize the observation point */
    interfaces__intf_stats__observation_point__init(obs_p);

    /* set observation point node id */
    obs_p->node_id = strdup(node_id);
    TEST_ASSERT_NOT_NULL(obs_p->node_id);

    /* set observation point location id */
    obs_p->location_id = strdup(location_id);
    TEST_ASSERT_NOT_NULL(obs_p->location_id);

    report->observation_point = obs_p;

    /* Allocate the observation windows table, it will carry one entry */
    num_w = 1;
    report->n_observation_windows = num_w;
    obs_pb_tbl = CALLOC(num_w, sizeof(*obs_pb_tbl));
    report->observation_windows = obs_pb_tbl;

    /* Allocate the unique entry of the observation windows table */
    obs_pb = CALLOC(1, sizeof(*obs_pb));

    /* Initialize the observation window */
    interfaces__intf_stats__observation_window__init(obs_pb);
    obs_pb_tbl[0] = obs_pb;

    /* Allocate the interface stats table with 2 entries */
    num_intfs_w0 = 2;
    obs_pb->n_intf_stats = num_intfs_w0;
    stats_pb_tbl = CALLOC(num_intfs_w0, sizeof(*stats_pb_tbl));

    /* Assign the stats table to the observation window */
    obs_pb->intf_stats = stats_pb_tbl;

    /* Allocate the first stats entry */
    stats_pb = CALLOC(1, sizeof(*stats_pb));

    /* Initialize the first stats entry */
    interfaces__intf_stats__intf_stats__init(stats_pb);

    /* Fill up the first stats entry */
    stats_pb->if_name = strdup("test_intf1");
    stats_pb->role    = strdup("uplink");
    TEST_ASSERT_NOT_NULL(stats_pb->if_name);
    TEST_ASSERT_NOT_NULL(stats_pb->role);
    stats_pb->has_tx_bytes = true;
    stats_pb->tx_bytes = 10;
    stats_pb->has_rx_bytes = true;
    stats_pb->rx_bytes = 20;
    stats_pb->has_tx_packets = true;
    stats_pb->tx_packets = 1;
    stats_pb->has_rx_packets = true;
    stats_pb->rx_packets = 1;

    /* Assign the first interface stats to the stats table */
    stats_pb_tbl[0] = stats_pb;

    /* Allocate the second stats entry */
    stats_pb = CALLOC(1, sizeof(*stats_pb));

    /* Initialize the second stats entry */
    interfaces__intf_stats__intf_stats__init(stats_pb);

    /* Fill up the second stats entry */
    stats_pb->if_name = strdup("test_intf2");
    stats_pb->role    = strdup("onboarding");
    TEST_ASSERT_NOT_NULL(stats_pb->if_name);
    TEST_ASSERT_NOT_NULL(stats_pb->role);
    stats_pb->has_tx_bytes = true;
    stats_pb->tx_bytes = 100;
    stats_pb->has_rx_bytes = true;
    stats_pb->rx_bytes = 200;
    stats_pb->has_tx_packets = true;
    stats_pb->tx_packets = 40;
    stats_pb->has_rx_packets = true;
    stats_pb->rx_packets = 50;

    /* Assign the first interface stats to the stats table */
    stats_pb_tbl[1] = stats_pb;

    /* Get serialization length */
    len = interfaces__intf_stats__intf_report__get_packed_size(report);
    TEST_ASSERT_TRUE(len != 0);

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    serialized->len = interfaces__intf_stats__intf_report__pack(report, buf);
    serialized->buf = buf;

    emit_report(serialized);

    /* Free the serialized protobuf after it was emitted */
    intf_stats_free_packed_buffer(serialized);

    /* Free the protobuf resources */

    /* free first stats resources */
    stats_pb = stats_pb_tbl[0];
    FREE(stats_pb->if_name);
    FREE(stats_pb->role);
    FREE(stats_pb);

    /* free second stats resources */
    stats_pb = stats_pb_tbl[1];
    FREE(stats_pb->if_name);
    FREE(stats_pb->role);
    FREE(stats_pb);

    /* free stats table */
    FREE(stats_pb_tbl);

    /* free the observation window */
    obs_pb = obs_pb_tbl[0];
    FREE(obs_pb);

    /* free the observation window table */
    FREE(obs_pb_tbl);

    /* free the observation point */
    FREE(obs_p->location_id);
    FREE(obs_p->node_id);
    FREE(obs_p);

    /* free the report */
    FREE(report);
}

/**
 * @brief validates the contents of an observation point protobuf
 *
 * @param node expected node info
 * @param op observation point protobuf to validate
 */
static void 
validate_node_info(node_info_t *node, Interfaces__IntfStats__ObservationPoint *op)
{
    TEST_ASSERT_EQUAL_STRING(node->node_id, op->node_id);
    TEST_ASSERT_EQUAL_STRING(node->location_id, op->location_id);
}

/**
 * @brief validates the contents of a intf stat counters protobuf
 *
 * @param intf stats info
 * @param intf stats protobuf to validate
 */
static void
validate_intf_stats(intf_stats_t *intf, Interfaces__IntfStats__IntfStats *stats_pb)
{
    TEST_ASSERT_EQUAL_STRING(intf->ifname, stats_pb->if_name);
    TEST_ASSERT_EQUAL_STRING(intf->role  , stats_pb->role);

    TEST_ASSERT_EQUAL_UINT(intf->tx_bytes, stats_pb->tx_bytes);
    TEST_ASSERT_EQUAL_UINT(intf->rx_bytes, stats_pb->rx_bytes);

    TEST_ASSERT_EQUAL_UINT(intf->tx_packets, stats_pb->tx_packets);
    TEST_ASSERT_EQUAL_UINT(intf->rx_packets, stats_pb->rx_packets);
}

/**
 * @brief validates the contents of an observation window protobuf
 *
 * @param node expected node info
 * @param op observation point protobuf to validate
 */
static void
validate_observation_window(intf_stats_window_t *window, Interfaces__IntfStats__ObservationWindow *window_pb)
{
    TEST_ASSERT_EQUAL_UINT(window->started_at, window_pb->started_at);
    TEST_ASSERT_EQUAL_UINT(window->ended_at, window_pb->ended_at);
    TEST_ASSERT_EQUAL_UINT(window->num_intfs, window_pb->n_intf_stats);
}

/**
 * @brief validates the contents of a flow windows protobuf
 *
 * @param node expected Intf Stat report info
 * @param report_pb Intf Stat report protobuf to validate
 */
static void
validate_windows(intf_stats_report_data_t *report, Interfaces__IntfStats__IntfReport *report_pb)
{
    Interfaces__IntfStats__ObservationWindow **windows_pb_tbl;
    Interfaces__IntfStats__ObservationWindow **window_pb;

    intf_stats_list_t               *window_list = &report->window_list;
    intf_stats_window_list_t        *window = NULL;
    intf_stats_window_t             *window_entry = NULL;
    size_t i;

    ds_dlist_iter_t                 win_iter;

    TEST_ASSERT_EQUAL_UINT(report->num_windows, report_pb->n_observation_windows);

    windows_pb_tbl = intf_stats_set_pb_windows(report);
    window_pb = windows_pb_tbl;

    for( window = ds_dlist_ifirst(&win_iter, window_list);
         window != NULL;
         window = ds_dlist_inext(&win_iter))
    {
        window_entry = &window->entry;

        validate_observation_window(window_entry, *window_pb);
        window_pb++;
    }

    /* Free validation structure */
    for (i = 0; i < report->num_windows; i++)
    {
        intf_stats_free_pb_window(windows_pb_tbl[i]);
    }

    FREE(windows_pb_tbl);
}

/**
 * @brief validates the contents of a flow report protobuf
 *
 * @param node expected flow report info
 * @param report_pb flow report protobuf to validate
 */
static void
validate_report(intf_stats_report_data_t *report, Interfaces__IntfStats__IntfReport *report_pb)
{
    TEST_ASSERT_EQUAL_UINT(report->reported_at, report_pb->reported_at);
    validate_node_info(&report->node_info, report_pb->observation_point);
    validate_windows(report, report_pb);
}

/**
 * @brief tests serialize_node_info() when passed a NULL pointer
 */
void test_serialize_node_info_null_ptr(void)
{
    node_info_t     *node = NULL;
    packed_buffer_t *pb;

    pb = intf_stats_serialize_node_info(node);

    /* Basic validation */
    TEST_ASSERT_NULL(pb);
}

/**
 * @brief tests serialize_node_info() when provided an empty node info
 */
void test_serialize_node_info_no_field_set(void)
{
    node_info_t     node = { 0 };
    packed_buffer_t *pb;

    /* Serialize the observation point */
    pb = intf_stats_serialize_node_info(&node);

    /* Basic validation */
    TEST_ASSERT_NULL(pb);
}

/**
 * @brief tests serialize_node_info() when provided a valid node info
 */
void
test_serialize_node_info(void)
{
    node_info_t                             *node;
    packed_buffer_t                         *pb;
    packed_buffer_t                          pb_r = { 0 };
    uint8_t                                  rbuf[4096];
    size_t                                   nread = 0;
    Interfaces__IntfStats__ObservationPoint *op;

    TEST_ASSERT_TRUE(g_test_mgr.initialized);

    node = &g_test_mgr.report.node_info;

    /* Serialize the observation point */
    pb = intf_stats_serialize_node_info(node);


    /* Basic Validation */
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    /* Save the serialized protobuf to file*/
    pb2file(pb, g_test_mgr.f_name);

    /* Free the serialied container */
    intf_stats_free_packed_buffer(pb);

    /* Read back the serialized protobuf */
    pb_r.buf = rbuf;
    pb_r.len = sizeof(rbuf);
    nread = file2pb(g_test_mgr.f_name, &pb_r);
    op = interfaces__intf_stats__observation_point__unpack(NULL, nread, rbuf);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(op);
    validate_node_info(node, op);

    /* Free the deserialized content */
    interfaces__intf_stats__observation_point__free_unpacked(op, NULL);
}

/**
 * @brief tests intf_stats_serialize_intf_stats() when provided with a valid stats pointer
 */
void
test_intf_stats(intf_stats_t *intf)
{
    packed_buffer_t                  *pb;
    packed_buffer_t                   pb_r = { 0 };
    uint8_t                           rbuf[4096];
    size_t                            nread = 0;
    Interfaces__IntfStats__IntfStats *stats_pb;

    /* Serialize the intf stats data */
    pb = intf_stats_serialize_intf_stats(intf);

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    /* Save the serialized protobuf to file */
    pb2file(pb, g_test_mgr.f_name);

    /* Free the serialized container */
    intf_stats_free_packed_buffer(pb);

    /* Read back the serialized protobuf */
    pb_r.buf = rbuf;
    pb_r.len = sizeof(rbuf);
    nread    = file2pb(g_test_mgr.f_name, &pb_r);
    stats_pb = interfaces__intf_stats__intf_stats__unpack(NULL, nread, rbuf);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(stats_pb);
    validate_intf_stats(intf, stats_pb);

    /* Free the deserialized content */
    interfaces__intf_stats__intf_stats__free_unpacked(stats_pb, NULL);
}

/**
 * @brief tests intf_stats_serialize_intf_stats() when provided a valid single stats pointer
 */
void
test_serialize_intf_stats(void)
{
    intf_stats_report_data_t *report = &g_test_mgr.report;
    intf_stats_window_list_t *window = NULL;
    intf_stats_window_t      *window_entry = NULL;

    intf_stats_t             *intf_entry = NULL;

    window = ds_dlist_head(&report->window_list);
    TEST_ASSERT_NOT_NULL(window);

    window_entry = &window->entry;
    TEST_ASSERT_NOT_NULL(window_entry);

    intf_entry = ds_dlist_head(&window_entry->intf_list);
    TEST_ASSERT_NOT_NULL(intf_entry);

    test_intf_stats(intf_entry);
}

/**
 * @brief tests intf_stats_serialize_intf_stats() when provided a table of stats pointers
 */
void
test_set_intf_stats(void)
{
    intf_stats_report_data_t *report       = &g_test_mgr.report;
    intf_stats_window_list_t *window       = NULL;
    intf_stats_window_t      *window_entry = NULL;

    intf_stats_t             *intf         = NULL;
    ds_dlist_iter_t           intf_iter;

    Interfaces__IntfStats__IntfStats  **stats_pb_tbl;
    Interfaces__IntfStats__IntfStats  **stats_pb;

    window = ds_dlist_head(&report->window_list);
    TEST_ASSERT_NOT_NULL(window);

    window_entry = &window->entry;
    TEST_ASSERT_NOT_NULL(window_entry);

    /* Generate the table of intf stat pointers */
    stats_pb_tbl = intf_stats_set_pb_intf_stats(window_entry);

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(stats_pb_tbl);

    /* Validate each of the intf stats entries */
    stats_pb = stats_pb_tbl;
    for ( intf = ds_dlist_ifirst(&intf_iter, &window_entry->intf_list);
          intf != NULL;
          intf = ds_dlist_inext(&intf_iter))
    {
        /* Validate the intf stats protobuf content */
        validate_intf_stats(intf, *stats_pb);

        /* Free the current intf entry */
        intf_stats_free_pb_intf_stats(*stats_pb);

        stats_pb++;
    }

    /* Free the pointers table */
    FREE(stats_pb_tbl);
}

/**
 * @brief tests intf_stats_serialize__window() when provided a single window pointer
 */
void
test_observation_window(intf_stats_window_t *window)
{
    packed_buffer_t                          *pb;
    packed_buffer_t                           pb_r = { 0 };
    uint8_t                                   rbuf[4096];
    size_t                                    nread = 0;
    Interfaces__IntfStats__ObservationWindow *window_pb;

    /* Serialize the observation window */
    pb = intf_stats_serialize_window(window);

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    /* Save the serialized protobuf to file */
    pb2file(pb, g_test_mgr.f_name);

    /* Free the serialized container */
    intf_stats_free_packed_buffer(pb);

    /* Read back the serialized protobuf */
    pb_r.buf  = rbuf;
    pb_r.len  = sizeof(rbuf);
    nread     = file2pb(g_test_mgr.f_name, &pb_r);
    window_pb = interfaces__intf_stats__observation_window__unpack(NULL, nread, rbuf);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(window_pb);
    validate_observation_window(window, window_pb);

    /* Free the deserialized content */
    interfaces__intf_stats__observation_window__free_unpacked(window_pb, NULL);
}

/**
 * @brief tests intf_stats_serialize_window() when provided a valid window pointer
 */
void
test_serialize_observation_windows(void)
{
    intf_stats_report_data_t *report = &g_test_mgr.report;
    intf_stats_window_list_t *window = NULL;
    intf_stats_window_t      *window_entry = NULL;

    window = ds_dlist_head(&report->window_list);
    TEST_ASSERT_NOT_NULL(window);

    window_entry = &window->entry;
    TEST_ASSERT_NOT_NULL(window_entry);

    test_observation_window(window_entry);
}

/**
 * @brief tests intf_stats_set_pb_windows() when provided a table of window pointers
 */
void
test_set_serialization_windows(void)
{
    intf_stats_report_data_t                   *report = &g_test_mgr.report;
    Interfaces__IntfStats__ObservationWindow  **windows_pb_tbl;
    Interfaces__IntfStats__ObservationWindow  **window_pb;

    intf_stats_list_t                *window_list = &report->window_list;
    intf_stats_window_list_t         *window = NULL;
    intf_stats_window_t              *window_entry = NULL;

    ds_dlist_iter_t                   win_iter;

    /* Generate a table of observation window pointers */
    windows_pb_tbl = intf_stats_set_pb_windows(report);

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(windows_pb_tbl);

    /* Validate each of the intf stats entries */
    window_pb = windows_pb_tbl;

    for( window = ds_dlist_ifirst(&win_iter, window_list);
         window != NULL;
         window = ds_dlist_inext(&win_iter))
    {
        window_entry = &window->entry;

        /* Validate the observation window protobuf content */
        validate_observation_window(window_entry, *window_pb);

        /* Free the current window entry */
        intf_stats_free_pb_window(*window_pb);

        window_pb++;
    }

    /* Free the windows pointers tbl */
    FREE(windows_pb_tbl);
}

/**
 * test_serialize_flow_report: tests
 * intf_stats_serialize_flow_report() behavior when provided a valid node info
 */
void
test_serialize_report(void)
{
    intf_stats_report_data_t          *report = &g_test_mgr.report;
    packed_buffer_t                   *pb = NULL;
    packed_buffer_t                    pb_r = { 0 };
    uint8_t                            rbuf[4096];
    size_t                             nread = 0;
    Interfaces__IntfStats__IntfReport *pb_report = NULL;

    /* Validate the serialized content */
    pb = intf_stats_serialize_report(report);

#ifndef ARCH_X86
    emit_report(pb);
#endif

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    /* Save the serialized protobuf to file */
    pb2file(pb, g_test_mgr.f_name);

    /* Free the serialized container */
    intf_stats_free_packed_buffer(pb);

    /* Read back the serialized protobuf */
    pb_r.buf = rbuf;
    pb_r.len = sizeof(rbuf);
    nread = file2pb(g_test_mgr.f_name, &pb_r);
    pb_report = interfaces__intf_stats__intf_report__unpack(NULL, nread, rbuf);
    TEST_ASSERT_NOT_NULL(pb_report);

    /* Validate the de-serialized content */
    validate_report(report, pb_report);

    /* Free the dserialized content */
    interfaces__intf_stats__intf_report__free_unpacked(pb_report, NULL);

    return;
}

/******************************************************************************
 * Test setup and tear down
******************************************************************************/
static void
intf_stats_test_setup_window(intf_stats_window_t *window)
{
    intf_stats_t    *intf_entry = NULL;
    ds_dlist_t      *window_intf_list = NULL;

    if (!window) return;

    window_intf_list = &window->intf_list;

    /* Allocate first interface */
    intf_entry = intf_stats_intf_alloc();
    TEST_ASSERT_NOT_NULL(intf_entry);

    /* Fill up the interface entry */
    STRSCPY(intf_entry->ifname, "test_intf1");
    STRSCPY(intf_entry->role, "uplink");

    intf_entry->tx_bytes = 100;
    intf_entry->rx_bytes = 200;

    intf_entry->tx_packets = 40;
    intf_entry->rx_packets = 50;

    ds_dlist_insert_tail(window_intf_list, intf_entry);

    /* Allocate second interface */
    intf_entry = intf_stats_intf_alloc();
    TEST_ASSERT_NOT_NULL(intf_entry);

    /* Fill up the interface entry */
    STRSCPY(intf_entry->ifname, "test_intf2");
    STRSCPY(intf_entry->role, "onboarding");

    intf_entry->tx_bytes = 300;
    intf_entry->rx_bytes = 400;

    intf_entry->tx_packets = 10;
    intf_entry->rx_packets = 20;

    ds_dlist_insert_tail(window_intf_list, intf_entry);

    /* Allocate second interface */
    intf_entry = intf_stats_intf_alloc();
    TEST_ASSERT_NOT_NULL(intf_entry);

    /* Fill up the interface entry */
    STRSCPY(intf_entry->ifname, "test_intf3");

    intf_entry->tx_bytes = 3000;
    intf_entry->rx_bytes = 3000;

    intf_entry->tx_packets = 300;
    intf_entry->rx_packets = 300;

    ds_dlist_insert_tail(window_intf_list, intf_entry);

    /* Fill up the remaining window details */
    window->num_intfs  = 3;

    return;
}

/**
 * test_serialize_flow_report: tests
 * intf_stats_serialize_flow_report() behavior when provided a valid node info
 */
void
test_serialize_report_1(void)
{
    intf_stats_report_data_t    report;
    node_info_t                 *node_info;
    packed_buffer_t             *pb = NULL;
    intf_stats_window_t         *window_entry = NULL;

    memset(&report, 0, sizeof(report));
    report.num_windows = 0;
    ds_dlist_init(&report.window_list, intf_stats_window_list_t, node);
    node_info = &report.node_info;
    node_info->node_id = strdup("1S6D808DB4");
    node_info->location_id = strdup("5bf5fc908a4eeb5622aa1217");

    /* Fill up the interface entry */
    intf_stats_activate_window(&report);
    window_entry = intf_stats_get_current_window(&report);
    if (!window_entry)
    {
        LOGE("%s: Unable to get current active window", __func__);
        return;
    }

    /* increment num_intfs count*/
    window_entry->num_intfs = 1;
    intf_stats_close_window(&report);
    intf_stats_dump_report(&report);

    /* Validate the serialized content */
    pb = intf_stats_serialize_report(&report);
    intf_stats_free_packed_buffer(pb);

    intf_stats_reset_report(&report);

    free(node_info->node_id);
    free(node_info->location_id);

    return;
}

/**
 * @brief See unity documentation/exmaples
 */
void
intf_stats_setUp(void)
{
    intf_stats_report_data_t    *report       = NULL;
    intf_stats_window_t         *window_entry = NULL;
    node_info_t                 *node_info    = NULL;

    LOGI("%s: setting up the test", __func__);

    memset(&g_test_mgr, 0, sizeof(g_test_mgr));
    g_test_mgr.f_name = strdup("/tmp//serial_proto");
    TEST_ASSERT_NOT_NULL(g_test_mgr.f_name);

#if !defined(ARCH_X86)
    g_test_mgr.has_qm = true;
#else
    g_test_mgr.has_qm = false;
#endif

    report = &g_test_mgr.report;
    node_info = &report->node_info;

    /* Initialize the report */
    ds_dlist_init(&report->window_list, intf_stats_window_list_t, node);
    report->num_windows = 0;

    /* Activate an observation window */
    intf_stats_activate_window(report);
    window_entry = intf_stats_get_current_window(report);
    if (!window_entry)
    {
        LOGE("%s: Unable to get current active window", __func__);
        return;
    }

    intf_stats_test_setup_window(window_entry);
    intf_stats_close_window(report);

    report->reported_at = time(NULL);

    /* Set the node_info */
    node_info->node_id = strdup("1S6D808DB4");
    node_info->location_id = strdup("5bf5fc908a4eeb5622aa1217");
    TEST_ASSERT_NOT_NULL(node_info->node_id);
    TEST_ASSERT_NOT_NULL(node_info->location_id);

    intf_stats_dump_report(report);

    g_test_mgr.initialized = true;
}

/**
 * @brief See unity documentation/exmaples
 */
void
intf_stats_tearDown(void)
{
    node_info_t *node_info = &g_test_mgr.report.node_info;

    LOGI("%s: tearing down the test", __func__);

    intf_stats_reset_report(&g_test_mgr.report);
    if (g_test_mgr.f_name) FREE(g_test_mgr.f_name);

    FREE(node_info->node_id);
    FREE(node_info->location_id);

    g_test_mgr.initialized = false;
}

/*****************************************************************************/
int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, intf_stats_setUp, intf_stats_tearDown);

    /* Node Info(Observation Point) tests */
    RUN_TEST(test_serialize_node_info);
    RUN_TEST(test_serialize_node_info_null_ptr);
    RUN_TEST(test_serialize_node_info_no_field_set);

    /* Intf Stats tests */
    RUN_TEST(test_serialize_intf_stats);
    RUN_TEST(test_set_intf_stats);

    /* Observation Window tests */
    RUN_TEST(test_serialize_observation_windows);
    RUN_TEST(test_set_serialization_windows);

    /* Complete Intf Stat report test */
    RUN_TEST(test_serialize_report);
    RUN_TEST(test_Intf__Stats__Report);
    RUN_TEST(test_serialize_report_1);

    return ut_fini();
}
