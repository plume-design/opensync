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

#define __GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ev.h>
#include <time.h>

#include "log.h"
#include "target.h"
#include "unity.h"
#include "qm_conn.h"
#include "memutil.h"

#include "test_mdns.h"


mdns_records_test_mgr g_test_mgr;

/***********************************************************************************************************
 *  Helper functions
 ***********************************************************************************************************/

/**
 * @brief sends a serialized buffer over MQTT
 *
 * @param pb serialized buffer
 */
void
emit_report(packed_buffer_t *pb)
{
    qm_response_t           res;
    bool                    ret;

    if (!g_test_mgr.has_qm) return;

    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG,
                              "dev-test/MDNS/Records/dev_mdns/a/b",
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

/***********************************************************************************************************
 * Unit Tests
 ***********************************************************************************************************/


/***********************************************************************************************************
 * Validation functions
 ***********************************************************************************************************/

/**
 * @brief validates the contents of an observation point protobuf
 *
 * @param node expected node info
 * @param op observation point protobuf to validate
 */
static void
validate_node_info(node_info_t *node, Interfaces__MdnsRecordsTelemetry__ObservationPoint *op)
{
    TEST_ASSERT_EQUAL_STRING(node->node_id, op->node_id);
    TEST_ASSERT_EQUAL_STRING(node->location_id, op->location_id);
}

/**
 * @brief validates the contents of an observation window protobuf
 *
 * @param window expected node info
 * @param window_pb observation window protobuf to validate
 */
static void
validate_observation_window(observation_window_t *window, Interfaces__MdnsRecordsTelemetry__ObservationWindow *window_pb)
{
    TEST_ASSERT_EQUAL_UINT(window->started_at, window_pb->started_at);
    TEST_ASSERT_EQUAL_UINT(window->ended_at  , window_pb->ended_at);
}

/**
  * @brief validates the contents of a mdns record protobuf
  *
  * @param mdns_records_t  rec info
  * @param mdns_record protobuf to validate
  */
static void
validate_mdns_record(mdns_records_t *rec, Interfaces__MdnsRecordsTelemetry__MdnsRecord *record_pb)
{
    struct resource *res = &rec->resource;
    TEST_ASSERT_NOT_NULL(res);

    TEST_ASSERT_EQUAL_STRING(res->name, record_pb->owner_name);

    switch (res->type)
    {
        case QTYPE_A:
        {
            TEST_ASSERT_EQUAL_UINT(INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_A, record_pb->type);
            TEST_ASSERT_EQUAL_STRING(res->known.a.name, record_pb->domain_name);
            TEST_ASSERT_EQUAL_STRING(inet_ntoa(res->known.a.ip), record_pb->ip);
            break;
        }

        case QTYPE_NS:
        {
            TEST_ASSERT_EQUAL_UINT(INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_NS, record_pb->type);
            TEST_ASSERT_EQUAL_STRING(res->known.ns.name, record_pb->domain_name);
            break;
        }

        case QTYPE_CNAME:
        {
            TEST_ASSERT_EQUAL_UINT(INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_CNAME, record_pb->type);
            TEST_ASSERT_EQUAL_STRING(res->known.cname.name, record_pb->domain_name);
            break;
        }

        case QTYPE_PTR:
        {
            TEST_ASSERT_EQUAL_UINT(INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_PTR, record_pb->type);
            TEST_ASSERT_EQUAL_STRING(res->known.ptr.name, record_pb->domain_name);
            break;
        }

        case QTYPE_TXT:
        {
            TEST_ASSERT_EQUAL_UINT(INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_TXT, record_pb->type);
            TEST_ASSERT_EQUAL(res->rdata, record_pb->res_desc.data);
            break;
        }

        case QTYPE_SRV:
        {
            TEST_ASSERT_EQUAL_UINT(INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_SRV, record_pb->type);
            TEST_ASSERT_EQUAL_STRING(res->known.srv.name, record_pb->domain_name);
            TEST_ASSERT_EQUAL_UINT(res->known.srv.priority, record_pb->priority);
            TEST_ASSERT_EQUAL_UINT(res->known.srv.weight, record_pb->weight);
            TEST_ASSERT_EQUAL_UINT(res->known.srv.port, record_pb->port);
            break;
        }

        default:
        {
            TEST_FAIL_MESSAGE("MDNS record type is unspecified");
            break;
        }
    }
}

/**
  * @brief validates the contents of an mdns client protobuf
  *
  * @param client - expected client info
  * @param pb - client protobuf to validate
  */
static void
validate_mdns_client(mdns_client_t *client, Interfaces__MdnsRecordsTelemetry__MdnsClient *client_pb)
{
    TEST_ASSERT_EQUAL_STRING(client->mac_str, client_pb->mac);
    TEST_ASSERT_EQUAL_STRING(client->ip_str , client_pb->ip);
    TEST_ASSERT_EQUAL_UINT(client->num_records, client_pb->n_mdns_records);
}

/**
 * @brief validates the contents of a client protobuf
 *
 * @param report expected report info
 * @param report_pb mdns record report protobuf to validate
 */
static void
validate_clients(mdns_records_report_data_t *report, Interfaces__MdnsRecordsTelemetry__MdnsRecordsReport *report_pb)
{
    Interfaces__MdnsRecordsTelemetry__MdnsClient    **clients_pb_tbl;
    Interfaces__MdnsRecordsTelemetry__MdnsClient    **client_pb;

    ds_tree_t                                        *clients;
    ds_tree_iter_t                                    client_iter;
    mdns_client_t                                    *client;
    size_t                                            i, num_clients;

    clients = &report->staged_clients;

    num_clients = mdns_records_get_num_clients(clients);
    TEST_ASSERT_EQUAL_UINT(num_clients, report_pb->n_clients);

    clients_pb_tbl = mdns_records_set_pb_clients(report);
    client_pb = clients_pb_tbl;

    client = ds_tree_ifirst(&client_iter, clients);
    while (client)
    {
        validate_mdns_client(client, *client_pb);
        client_pb++;

        client = ds_tree_inext(&client_iter);
    }

    /* Free validation structure */
    for (i = 0; i < num_clients; i++)
    {
        mdns_records_free_pb_client(clients_pb_tbl[i]);
    }

    FREE(clients_pb_tbl);
}

/**
 * @brief validates the contents of a mdns records report protobuf
 *
 * @param node expected mdns records report info
 * @param report_pb report protobuf to validate
 */
static void
validate_report(mdns_records_report_data_t *report, Interfaces__MdnsRecordsTelemetry__MdnsRecordsReport *report_pb)
{
    validate_node_info(&report->node_info, report_pb->observation_point);
    validate_observation_window(&report->obs_w, report_pb->observation_window);
    validate_clients(report, report_pb);
}

/***********************************************************************************************************
 * Test functions
 ***********************************************************************************************************/

// Node info 

/**
 * @brief tests serialize_node_info() when passed a NULL pointer
 */
void test_serialize_node_info_null_ptr(void)
{
    node_info_t     *node = NULL;
    packed_buffer_t *pb;

    pb = mdns_records_serialize_node_info(node);

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
    pb = mdns_records_serialize_node_info(&node);

    /* Basic validation */
    TEST_ASSERT_NULL(pb);
}

/**
 * @brief tests serialize_node_info() when provided with a valid node info
 */
void
test_serialize_node_info(void)
{
    node_info_t                                        *node;
    packed_buffer_t                                    *pb;
    packed_buffer_t                                     pb_r = { 0 };
    uint8_t                                             rbuf[4096];
    size_t                                              n_read = 0;
    Interfaces__MdnsRecordsTelemetry__ObservationPoint *op;

    TEST_ASSERT_TRUE(g_test_mgr.initialized);

    node = &(g_test_mgr.report.node_info);

    /* Serialize the observation point */
    pb = mdns_records_serialize_node_info(node);

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    /* Save the serialized buffer to the file */
    pb2file(pb, g_test_mgr.f_name);

    /* Free the serialized container */
    mdns_records_free_packed_buffer(pb);

    /* Read back the serialized protobuf */
    pb_r.buf = rbuf;
    pb_r.len = sizeof(rbuf);
    n_read   = file2pb(g_test_mgr.f_name, &pb_r);
    op       = interfaces__mdns_records_telemetry__observation_point__unpack(NULL, n_read, rbuf);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(op);
    validate_node_info(node, op);

    /* Free the deserialized content */
    interfaces__mdns_records_telemetry__observation_point__free_unpacked(op, NULL);
}

//---------------------------------------------------------------------------------------------------------------------
// Observation Window

/**
  * @brief tests serialize_observation_window when passed a NULL pointer
  */
void
test_serialize_observation_window_null_ptr(void)
{
    observation_window_t    *window = NULL;
    packed_buffer_t         *pb;

    /* Serialized the observation window */
    pb = mdns_records_serialize_window(window);

    /* Basic validation */
    TEST_ASSERT_NULL(pb);
}

/**
  * @brief tests serialize_observation_window when provided an empty window
  */
void
test_serialize_observation_window_no_field_set(void)
{
    observation_window_t    window = { 0 };
    packed_buffer_t         *pb;

    /* Serialized the observation window */
    pb = mdns_records_serialize_window(&window);

    /* Basic validation */
    TEST_ASSERT_NULL(pb);
}

/**
 * @brief tests serialize_window() when provided with a valid observation window
 */
void
test_serialize_observation_window(void)
{
    observation_window_t                                *window;
    packed_buffer_t                                     *pb;
    packed_buffer_t                                      pb_r = { 0 };
    uint8_t                                              rbuf[4096];
    size_t                                               n_read = 0;
    Interfaces__MdnsRecordsTelemetry__ObservationWindow *window_pb;

    TEST_ASSERT_TRUE(g_test_mgr.initialized);

    window = &(g_test_mgr.report.obs_w);
    TEST_ASSERT_NOT_NULL(window);

    /* Serialize the observation window */
    pb = mdns_records_serialize_window(window);

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    /* Save the serialized buffer to the file */
    pb2file(pb, g_test_mgr.f_name);

    /* Free the serialized container */
    mdns_records_free_packed_buffer(pb);

    /* Read back the seiralized protobuf */
    pb_r.buf  = rbuf;
    pb_r.len  = sizeof(rbuf);
    n_read    = file2pb(g_test_mgr.f_name, &pb_r);
    window_pb = interfaces__mdns_records_telemetry__observation_window__unpack(NULL, n_read, rbuf);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(window_pb);
    validate_observation_window(window, window_pb);

    /* Free the deserialized content */
    interfaces__mdns_records_telemetry__observation_window__free_unpacked(window_pb, NULL);
}

//---------------------------------------------------------------------------------------------------------------------
// Mdns Record

/**
  * @brief tests mdns_records_serialize_record() when provided with a single valid mdns record pointer
  */
static void
test_mdns_record(mdns_records_t *rec)
{
    packed_buffer_t                              *pb;
    packed_buffer_t                               pb_r = { 0 };
    uint8_t                                       rbuf[4096];
    size_t                                        nread = 0;
    Interfaces__MdnsRecordsTelemetry__MdnsRecord *record_pb;

    /* Serialize the mdns record data */
    pb = mdns_records_serialize_record(rec);

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    /* Save the serialized protobuf to file */
    pb2file(pb, g_test_mgr.f_name);

    /* Free the serialized container */
    mdns_records_free_packed_buffer(pb);

    /* Read back the serialized protobuf */
    pb_r.buf  = rbuf;
    pb_r.len  = sizeof(rbuf);
    nread     = file2pb(g_test_mgr.f_name, &pb_r);
    record_pb = interfaces__mdns_records_telemetry__mdns_record__unpack(NULL, nread, rbuf);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(record_pb);
    validate_mdns_record(rec, record_pb);

    /* Free the deserialized content */
    interfaces__mdns_records_telemetry__mdns_record__free_unpacked(record_pb, NULL);
}

/**
  * @brief tests mdns_records_serialize_record() when provided with a single valid mdns record pointer
  */
void
test_serialize_record(void)
{
    mdns_records_report_data_t      *report = &g_test_mgr.report;
    mdns_client_t                   *client;
    mdns_records_list_t             *records_list;
    mdns_records_t                  *rec;

    TEST_ASSERT_TRUE(g_test_mgr.initialized);

    client = ds_tree_head(&report->staged_clients);
    TEST_ASSERT_NOT_NULL(client);

    records_list = &client->records_list;
    TEST_ASSERT_NOT_NULL(records_list);

    rec = ds_dlist_head(records_list);
    TEST_ASSERT_NOT_NULL(rec);

    test_mdns_record(rec);
}

/**
 * @brief tests mdns_records_serialize_record() when provided a table of record pointers
 */
void
test_set_records(void)
{
    mdns_records_report_data_t                       *report = &g_test_mgr.report;
    mdns_client_t                                    *client;
    mdns_records_list_t                              *records_list;
    mdns_records_t                                   *rec;

    ds_dlist_iter_t                                   rec_iter;

    Interfaces__MdnsRecordsTelemetry__MdnsRecord    **records_pb_tbl;
    Interfaces__MdnsRecordsTelemetry__MdnsRecord    **record_pb;

    TEST_ASSERT_TRUE(g_test_mgr.initialized);

    client = ds_tree_head(&report->staged_clients);
    TEST_ASSERT_NOT_NULL(client);

    records_list = &client->records_list;

    /* Generate the table of mdns record pointers */
    records_pb_tbl = mdns_records_set_pb_mdns_records(client);

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(records_pb_tbl);

    /* Validate each of the mdns record pb entries */
    record_pb = records_pb_tbl;
    for ( rec = ds_dlist_ifirst(&rec_iter, records_list);
          rec != NULL;
          rec = ds_dlist_inext(&rec_iter))
    {
        /* Validate the mdns record protobuf content */
        TEST_ASSERT_NOT_NULL(record_pb);
        validate_mdns_record(rec, *record_pb);

        /* Free the current record_pb */
        mdns_records_free_pb_record(*record_pb);

        record_pb++;
    }

    /* Free the pointers table */
    FREE(records_pb_tbl);
}

//---------------------------------------------------------------------------------------------------------------------
// Mdns Client

/**
  * @brief tests mdns_records_serialize_client() when provided with a single valid mdns client pointer
  */
static void
test_mdns_client(mdns_client_t *client)
{
    packed_buffer_t                               *pb;
    packed_buffer_t                               pb_r = { 0 };
    uint8_t                                       rbuf[4096];
    size_t                                        nread = 0;
    Interfaces__MdnsRecordsTelemetry__MdnsClient *client_pb;

    /* Serialze the mdns client */
    pb = mdns_records_serialize_client(client);

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    /* Save the serialized protobuf to file */
    pb2file(pb, g_test_mgr.f_name);

    /* Free the serialized protobuf */
    mdns_records_free_packed_buffer(pb);

    /* Read back the serialized protobuf */
    pb_r.buf  = rbuf;
    pb_r.len  = sizeof(rbuf);
    nread     = file2pb(g_test_mgr.f_name, &pb_r);
    client_pb = interfaces__mdns_records_telemetry__mdns_client__unpack(NULL, nread, rbuf);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(client_pb);
    validate_mdns_client(client, client_pb);

    /* Free the deserialized content */
    interfaces__mdns_records_telemetry__mdns_client__free_unpacked(client_pb, NULL);
}

/**
  * @brief tests mdns_records_serialize_client() when provided a valid client pointer
  */
void
test_serialize_client(void)
{
    mdns_records_report_data_t              *report = &g_test_mgr.report;
    mdns_client_t                           *client;

    TEST_ASSERT_TRUE(g_test_mgr.initialized);

    client = ds_tree_head(&report->staged_clients);
    TEST_ASSERT_NOT_NULL(client);

    test_mdns_client(client);
}

/**
  * @brief tests mdns_records_set_pb_clients() when provided with a table of mdns_client pointers
  */
void
test_set_serialization_clients(void)
{
    mdns_records_report_data_t                    *report = &g_test_mgr.report;
    Interfaces__MdnsRecordsTelemetry__MdnsClient **clients_pb_tbl;
    Interfaces__MdnsRecordsTelemetry__MdnsClient **client_pb;

    ds_tree_t                                     *clients;
    mdns_client_t                                 *client;
    ds_tree_iter_t                                 client_iter;

    TEST_ASSERT_TRUE(g_test_mgr.initialized);

    /* Generate a table of client pointers */
    clients_pb_tbl = mdns_records_set_pb_clients(report);

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(clients_pb_tbl);

    /* Validate each of the client entries */
    client_pb = clients_pb_tbl;

    clients = &report->staged_clients;
    client  = ds_tree_ifirst(&client_iter, clients);
    while (client)
    {
        validate_mdns_client(client, *client_pb);

        /* Free the current client */
        mdns_records_free_pb_client(*client_pb);

        client_pb++;
        client = ds_tree_inext(&client_iter);
    }

    /* Free the clients pointers tbl */
    FREE(clients_pb_tbl);
}

//---------------------------------------------------------------------------------------------------------------------
// Mdns Records Report

/**
  * @brief test serialize_report when provided a valid report data
  */
void
test_serialize_report(void)
{
    mdns_records_report_data_t                          *report = &g_test_mgr.report;
    packed_buffer_t                                     *pb     = NULL;
    packed_buffer_t                                      pb_r   = { 0 };
    uint8_t                                              rbuf[4096];
    size_t                                               nread = 0;
    Interfaces__MdnsRecordsTelemetry__MdnsRecordsReport *report_pb = NULL;

    TEST_ASSERT_TRUE(g_test_mgr.initialized);

    /* Validate the serialized protobuf */
    pb = mdns_records_serialize_report(report);

#ifndef ARCH_X86
    emit_report(pb);
#endif

    /* Basic validation */
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    /* Save the serialized protobuf to file */
    pb2file(pb, g_test_mgr.f_name);

    /* Free the serialized container */
    mdns_records_free_packed_buffer(pb);

    /* Read back the serialized protobuf */
    pb_r.buf  = rbuf;
    pb_r.len  = sizeof(rbuf);
    nread     = file2pb(g_test_mgr.f_name, &pb_r);
    report_pb = interfaces__mdns_records_telemetry__mdns_records_report__unpack(NULL, nread, rbuf);
    TEST_ASSERT_NOT_NULL(report_pb);

    /* Validate the deserialized report */
    validate_report(report, report_pb);

    /* Free the deserialized container */
    interfaces__mdns_records_telemetry__mdns_records_report__free_unpacked(report_pb, NULL);

    return;
}

void
test_Mdns_Records_Report(void)
{
    char *location_id = "mdns_records_test_location";
    char *node_id     = "1S6D808DB4";
    char *txt         = "https://bw.plume.com/test/api/v1";

    packed_buffer_t                                     *serialized;
    Interfaces__MdnsRecordsTelemetry__MdnsRecordsReport *report;
    Interfaces__MdnsRecordsTelemetry__ObservationWindow *obs_w;
    Interfaces__MdnsRecordsTelemetry__ObservationPoint  *obs_p;
    Interfaces__MdnsRecordsTelemetry__MdnsClient        **clients_pb_tbl;
    Interfaces__MdnsRecordsTelemetry__MdnsClient        *clients_pb;
    Interfaces__MdnsRecordsTelemetry__MdnsRecord        **records_pb_tbl;
    Interfaces__MdnsRecordsTelemetry__MdnsRecord        *records_pb;

    size_t  num_clients;
    size_t  num_records;
    size_t  len;
    void    *buf;

    /* Allocate serialization output structure */
    serialized = CALLOC(sizeof(packed_buffer_t), 1);

    /* Allocate protobuf */
    report = CALLOC(1, sizeof(*report));

    /* Initialize protobuf */
    interfaces__mdns_records_telemetry__mdns_records_report__init(report);

    /* Set observation point */
    obs_p = CALLOC(1, sizeof(*obs_p));

    /* Initialize the observation point */
    interfaces__mdns_records_telemetry__observation_point__init(obs_p);

    /* set observation point node id */
    obs_p->node_id = strdup(node_id);
    TEST_ASSERT_NOT_NULL(obs_p->node_id);

    /* set observation point location id */
    obs_p->location_id = strdup(location_id);
    TEST_ASSERT_NOT_NULL(obs_p->location_id);

    report->observation_point = obs_p;

    /* Set observation window */
    obs_w = CALLOC(1, sizeof(*obs_w));

    /* Initialize the observation window */
    interfaces__mdns_records_telemetry__observation_window__init(obs_w);

    /* set observation window fields */
    obs_w->started_at     = 10;
    obs_w->has_started_at = true;

    obs_w->ended_at       = 20;
    obs_w->has_ended_at   = true;

    report->observation_window = obs_w;

    /* Allocate the clients table, it will carry one entry */
    num_clients = 1;
    report->n_clients = num_clients;
    clients_pb_tbl = CALLOC(num_clients, sizeof(*clients_pb_tbl));
    report->clients = clients_pb_tbl;

    /* Allocate the unique entry of the clients table */
    clients_pb = CALLOC(1, sizeof(*clients_pb));

    /* Initialize the client */
    interfaces__mdns_records_telemetry__mdns_client__init(clients_pb);
    clients_pb_tbl[0] = clients_pb;

    /* set client fields */
    clients_pb->mac = strdup("aa:bb:cc:11:22:33");
    clients_pb->ip  = strdup("10.0.0.100");

    /* Allocate the mdns records table with 2 entries */
    num_records = 2;
    clients_pb->n_mdns_records = num_records;
    records_pb_tbl = CALLOC(num_records, sizeof(*records_pb_tbl));

    /* Assign the records table to the client */
    clients_pb->mdns_records = records_pb_tbl;

    /* Allocate the first records entry */
    records_pb = CALLOC(1, sizeof(*records_pb));

    /* Initialize the first records entry */
    interfaces__mdns_records_telemetry__mdns_record__init(records_pb);

    /* Fill up the first records entry */
    records_pb->type = INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_PTR;
    records_pb->has_type = true;
    records_pb->owner_name = strdup("115.0.168.192.in-addr.arpa");
    TEST_ASSERT_NOT_NULL(records_pb->owner_name);
    records_pb->domain_name = strdup("Amitiel.local");
    TEST_ASSERT_NOT_NULL(records_pb->domain_name);

    /* Assign the first record to the records table */
    records_pb_tbl[0] = records_pb;

    /* Allocate the second records entry */
    records_pb = CALLOC(1, sizeof(*records_pb));

    /* Initialize the seconds records entry */
    interfaces__mdns_records_telemetry__mdns_record__init(records_pb);

    /* Fill up the second records entry */
    records_pb->type = INTERFACES__MDNS_RECORDS_TELEMETRY__MDNS_RECORD_TYPE__MDNS_RECORD_TYPE_TXT;
    records_pb->has_type = true;
    records_pb->owner_name = strdup("bw.plume._http._tcp");
    TEST_ASSERT_NOT_NULL(records_pb->owner_name);
    records_pb->res_desc.data = MALLOC(strlen(txt));
    memcpy(records_pb->res_desc.data, txt, strlen(txt));
    records_pb->has_res_desc = true;

    /* Assign the second record to the records table */
    records_pb_tbl[1] = records_pb;

    /* Get serialization length */
    len = interfaces__mdns_records_telemetry__mdns_records_report__get_packed_size(report);
    TEST_ASSERT_TRUE(len != 0);

    /* Allocate space for the serialized buffer */
    buf = MALLOC(len);

    serialized->len = interfaces__mdns_records_telemetry__mdns_records_report__pack(report, buf);
    serialized->buf = buf;

    /* Send the MQTT report */
    emit_report(serialized);

    /* Free the serialized protobuf after it was emitted */
    mdns_records_free_packed_buffer(serialized);

    /* Free the protobuf resources */

    /* free first record resources */
    records_pb = records_pb_tbl[0];
    FREE(records_pb->owner_name);
    FREE(records_pb->domain_name);
    FREE(records_pb);

    /* free second record resources */
    records_pb = records_pb_tbl[1];
    FREE(records_pb->owner_name);
    FREE(records_pb->res_desc.data);
    FREE(records_pb);

    /* free records table */
    FREE(records_pb_tbl);

    /* free client resources */
    clients_pb = clients_pb_tbl[0];

    FREE(clients_pb->mac);
    FREE(clients_pb->ip);
    FREE(clients_pb);

    /* free clients table */
    FREE(clients_pb_tbl);

    /* free observation point */
    FREE(obs_p->location_id);
    FREE(obs_p->node_id);
    FREE(obs_p);

    /* free observation window */
    FREE(obs_w);

    /* free the report */
    FREE(report);

}

/***********************************************************************************************************
 * Setup and Tear down 
 ***********************************************************************************************************/

static void
setup_mdns_report_clients(void)
{
    mdns_records_report_data_t  *report;
    ds_tree_t                   *clients;
    mdns_records_list_t         *records_list;
    mdns_client_t               *client;
    mdns_records_t              *rec;
    struct resource             *res;

    uint32_t                    ip = 2110443574;
    struct in_addr              ip_addr;
    char                       *txt = "https://bw.plume.com/test/api/v1";

    report  = &g_test_mgr.report;
    clients = &report->staged_clients;

    /* Allocate the first client */
    client = CALLOC(1, sizeof(mdns_client_t));

    STRSCPY(client->mac_str, "aa:bb:cc:dd:ee:ff");
    STRSCPY(client->ip_str, "10.0.0.100");

    ds_dlist_init(&client->records_list, mdns_records_t, node);

    ds_tree_insert(clients, client, client->mac_str);

    /* Allocate two records for this client */
    client->num_records = 2;
    records_list = &client->records_list;

    /* Allocate the first record */
    rec = mdns_records_alloc_record();
    res = &rec->resource;

    (void)mdns_records_str_duplicate("115.0.168.192.in-addr.arpa", &res->name);
    (void)mdns_records_str_duplicate("Amitiel.local", &res->known.ptr.name);

    res->type  = QTYPE_PTR;
    res->class = 1;
    res->ttl   = 4500;

    ds_dlist_insert_tail(records_list, rec);

    /* Allocate the second record */
    rec = mdns_records_alloc_record();
    res = &rec->resource;

    (void)mdns_records_str_duplicate("bw.plume._hhtp._tcp", &res->name);
    (void)mdns_records_str_duplicate(txt, (char **)&res->rdata);
    res->rdlength = strlen(txt);

    res->type  = QTYPE_TXT;
    res->class = 1;
    res->ttl   = 4500;

    ds_dlist_insert_tail(records_list, rec);

    /* Allocate the second client */
    client = CALLOC(1, sizeof(mdns_client_t));

    STRSCPY(client->mac_str, "11:22:33:44:55:66");
    STRSCPY(client->ip_str, "10.0.0.50");

    ds_dlist_init(&client->records_list, mdns_records_t, node);

    ds_tree_insert(clients, client, client->mac_str);

    /* Allocate three records for this client */
    client->num_records = 3;
    records_list = &client->records_list;

    /* Allocate the first record */
    rec = mdns_records_alloc_record();
    res = &rec->resource;

    (void)mdns_records_str_duplicate("wemo_mini.local", &res->name);
    (void)mdns_records_str_duplicate("54.208.202.125", &res->known.a.name);

    ip_addr.s_addr = ip;
    memcpy(&res->known.a.ip, &ip_addr, sizeof(struct in_addr));

    res->type  = QTYPE_A;
    res->class = 1;
    res->ttl   = 1200;

    ds_dlist_insert_tail(records_list, rec);
    
    /* Allocate the second record */
    rec = mdns_records_alloc_record();
    res = &rec->resource;

    (void)mdns_records_str_duplicate("Airplay", &res->name);
    (void)mdns_records_str_duplicate("_homekit_.local", &res->known.ptr.name);

    res->type  = QTYPE_CNAME;
    res->class = 1;
    res->ttl   = 1200;

    ds_dlist_insert_tail(records_list, rec);

    /* Allocate the third record */
    rec = mdns_records_alloc_record();
    res = &rec->resource;

    (void)mdns_records_str_duplicate("wemo_mini.local", &res->name);
    (void)mdns_records_str_duplicate("Closet Plug", &res->known.srv.name);

    res->type  = QTYPE_SRV;
    res->class = 1;
    res->ttl   = 2400;

    res->known.srv.weight   = 0;
    res->known.srv.port     = 0;
    res->known.srv.priority = 45478;

    ds_dlist_insert_tail(records_list, rec);

    return;
}

void
teardown_mdns_report_clients(void)
{
    mdns_records_report_data_t  *report;
    ds_tree_t                   *clients;

    report  = &g_test_mgr.report;
    clients = &report->staged_clients;

    mdns_records_clear_clients(clients);

    report->initialized = false;

    return;
}

void
setup_mdns_records_report(void)
{
    mdns_records_report_data_t  *report     = NULL;
    node_info_t                 *node_info  = NULL;
    observation_window_t        *obs_w      = NULL;

    memset(&g_test_mgr, 0, sizeof(mdns_records_test_mgr));
    g_test_mgr.f_name = strdup("/tmp/serial_proto");
    TEST_ASSERT_NOT_NULL(g_test_mgr.f_name);

#if !defined(ARCH_X86)
    g_test_mgr.has_qm = true;
#else
    g_test_mgr.has_qm = false;
#endif

    report    = &g_test_mgr.report;
    node_info = &report->node_info;
    obs_w     = &report->obs_w;

    /* Initialize the report */
    ds_tree_init(&report->staged_clients, (ds_key_cmp_t *)strcmp, mdns_client_t, dst_node);

    /* Set the node_info */
    node_info->node_id     = strdup("1S6D808DB4");
    node_info->location_id = strdup("5e3a194bb0359438401645851");
    TEST_ASSERT_NOT_NULL(node_info->node_id);
    TEST_ASSERT_NOT_NULL(node_info->location_id);

    /* Set the observation window */
    obs_w->started_at = time(NULL);
    obs_w->ended_at   = obs_w->started_at + 60;

    setup_mdns_report_clients();

    report->initialized = true;
    g_test_mgr.initialized = true;

    return;
}

void
teardown_mdns_records_report(void)
{
    mdns_records_report_data_t  *report     = NULL;
    node_info_t                 *node_info  = NULL;

    report    = &g_test_mgr.report;
    node_info = &report->node_info;

    teardown_mdns_report_clients();

    FREE(node_info->node_id);
    FREE(node_info->location_id);
    FREE(g_test_mgr.f_name);

    g_test_mgr.initialized = false;

    return;
}
