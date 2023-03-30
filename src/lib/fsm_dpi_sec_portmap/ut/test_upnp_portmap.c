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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "const.h"
#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dpi_client_plugin.h"
#include "fsm_dpi_sec_portmap.h"
#include "log.h"
#include "memutil.h"
#include "os.h"
#include "ovsdb_update.h"
#include "qm_conn.h"
#include "sockaddr_storage.h"
#include "unit_test_utils.h"
#include "unity.h"
#include "upnp_portmap.h"
#include "upnp_portmap.pb-c.h"
#include "upnp_portmap_pb.h"
#include "upnp_report_aggregator.h"

void
test_upnp_portmap_cmp(void)
{
    struct mapped_port_t *p1;
    struct mapped_port_t *p2;
    int ret;

    p1 = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL, "123", "10.0.0.1", "123", "TCP", "port 1", "0", "", "2000");
    p2 = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL, "123", "10.0.0.1", "123", "TCP", "port 1", "0", "", "2000");
    ret = upnp_portmap_compare_record(p1, p2);
    TEST_ASSERT_EQUAL_INT(0, ret);
    upnp_portmap_delete_record(p2);

    p2 = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL, "987", "10.0.0.1", "987", "TCP", "port 1", "0", "", "2000");
    ret = upnp_portmap_compare_record(p1, p2);
    TEST_ASSERT_NOT_EQUAL_INT(0, ret);

    // Still need to go thru all the branches of the comparator

    /* cleanup */
    upnp_portmap_delete_record(p2);
    upnp_portmap_delete_record(p1);
}

void
test_upnp_portmap_compare_snapshots(void)
{
    struct mapped_port_t *p;
    ds_tree_t *snapshot1;
    ds_tree_t *snapshot2;
    bool ret;

    snapshot1 = upnp_portmap_create_snapshot();
    snapshot2 = upnp_portmap_create_snapshot();

    p = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL, "123", "10.0.0.1", "123", "TCP", "port 1", "0", "", "2000");
    ds_tree_insert(snapshot1, p, p);
    p = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL, "123", "10.0.0.1", "123", "TCP", "port 1", "0", "", "2000");
    ds_tree_insert(snapshot2, p, p);
    // S1=<123>   S2=<123>
    ret = upnp_portmap_compare_snapshot(snapshot1, snapshot2);
    TEST_ASSERT_FALSE(ret);

    p = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL, "555", "10.0.0.1", "555", "TCP", "port 2", "0", "", "2000");
    ds_tree_insert(snapshot2, p, p);
    // S1=<123> S2=<123,555>
    ret = upnp_portmap_compare_snapshot(snapshot1, snapshot2);
    TEST_ASSERT_TRUE(ret);

    p = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL, "999", "10.0.0.1", "555", "UDP", "port 3", "0", "", "2000");
    ds_tree_insert(snapshot1, p, p);
    // S1=<123,999> S2=<123,555>
    ret = upnp_portmap_compare_snapshot(snapshot1, snapshot2);
    TEST_ASSERT_TRUE(ret);

    p = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL, "555", "10.0.0.1", "555", "TCP", "port 2", "0", "", "2000");
    ds_tree_insert(snapshot1, p, p);
    // S1=<123,555,999> S2=<123,555>
    ret = upnp_portmap_compare_snapshot(snapshot1, snapshot2);
    TEST_ASSERT_TRUE(ret);

    p = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL, "999", "10.0.0.1", "555", "UDP", "port 3", "0", "", "2000");
    ds_tree_insert(snapshot2, p, p);
    // S1=<123,555,999> S2=<123,555,999>
    ret = upnp_portmap_compare_snapshot(snapshot1, snapshot2);
    TEST_ASSERT_FALSE(ret);

    /* Cleanup */
    upnp_portmap_delete_snapshot(snapshot2);
    upnp_portmap_delete_snapshot(snapshot1);
}

void
test_upnp_portmap_alloc_portmap(void)
{
    Upnp__Portmap__Portmap *port_pb;
    struct sockaddr_storage *addr;
    struct mapped_port_t port;

    MEMZERO(port);
    port_pb = upnp_portmap_alloc_portmap(&port);
    TEST_ASSERT_NULL(port_pb);

    port.source = UPNP_SOURCE_PKT_INSPECTION_DEL;
    port_pb = upnp_portmap_alloc_portmap(&port);
    TEST_ASSERT_NOT_NULL(port_pb);
    TEST_ASSERT_EQUAL(UPNP__PORTMAP__CAPTURE_SOURCE__PKT_INSPECTION_DEL, port_pb->source);
    upnp_portmap_free_portmap(port_pb);

    port.source = UPNP_SOURCE_IGD_POLL;
    port_pb = upnp_portmap_alloc_portmap(&port);
    TEST_ASSERT_NULL(port_pb);

    /* Now setup int_client for the rest of the tests */
    addr = sockaddr_storage_create(AF_INET, "192.168.0.1");
    port.intClient = addr;

    port.source = UPNP_SOURCE_IGD_POLL;
    port_pb = upnp_portmap_alloc_portmap(&port);
    TEST_ASSERT_NOT_NULL(port_pb);
    TEST_ASSERT_EQUAL(UPNP__PORTMAP__CAPTURE_SOURCE__IGD_POLL, port_pb->source);
    upnp_portmap_free_portmap(port_pb);

    port.source = UPNP_SOURCE_PKT_INSPECTION_ADD;
    port_pb = upnp_portmap_alloc_portmap(&port);
    TEST_ASSERT_NOT_NULL(port_pb);
    TEST_ASSERT_EQUAL(UPNP__PORTMAP__CAPTURE_SOURCE__PKT_INSPECTION_ADD, port_pb->source);
    upnp_portmap_free_portmap(port_pb);

    port.source = UPNP_SOURCE_OVSDB_STATIC;
    port.protocol = UPNP_MAPPING_PROTOCOL_TCP;
    port_pb = upnp_portmap_alloc_portmap(&port);
    TEST_ASSERT_NOT_NULL(port_pb);
    TEST_ASSERT_EQUAL(UPNP__PORTMAP__CAPTURE_SOURCE__OVSDB_STATIC, port_pb->source);
    TEST_ASSERT_EQUAL(UPNP__PORTMAP__PROTOCOLS__TCP, port_pb->protocol);
    upnp_portmap_free_portmap(port_pb);

    port_pb = upnp_portmap_alloc_portmap(&port);
    TEST_ASSERT_NOT_NULL(port_pb);
    upnp_portmap_free_portmap(port_pb);

    port.protocol = UPNP_MAPPING_PROTOCOL_UDP;
    port_pb = upnp_portmap_alloc_portmap(&port);
    TEST_ASSERT_NOT_NULL(port_pb);
    TEST_ASSERT_EQUAL(UPNP__PORTMAP__CAPTURE_SOURCE__OVSDB_STATIC, port_pb->source);
    TEST_ASSERT_EQUAL(UPNP__PORTMAP__PROTOCOLS__UDP, port_pb->protocol);
    upnp_portmap_free_portmap(port_pb);

    /* Cleanup */
    FREE(addr);
}

static void send_report_protobuf(struct fsm_session *session, char *mqtt_channel, void *data, size_t data_len)
{
#ifndef ARCH_X86
    qm_response_t res;
    bool ret = false;
#endif

    LOGT("%s: protobuf msg len: %zu, topic: %s", __func__, data_len, mqtt_channel);

#ifndef ARCH_X86
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_DISABLE, mqtt_channel, data, data_len, &res);
    if (!ret) LOGE("error sending mqtt with topic %s", mqtt_channel);
#endif
}

void
test_upnp_portmap_send_report(void)
{
    struct fsm_dpi_sec_portmap_session *u_session;
    struct mapped_port_t *port;
    struct fsm_session fsm;
    int ret;

    fsm.node_id = "NODE_ID";
    fsm.location_id = "LOCATION_ID";
    fsm.ops.send_pb_report = send_report_protobuf;
    fsm.handler_ctxt = CALLOC(1, sizeof(struct fsm_dpi_client_session));

    u_session = fsm_dpi_sec_portmap_get_session(&fsm);

    upnp_portmap_init(&fsm);

    u_session->mqtt_topic = STRDUP("MQTT_REPORT_TOPIC");
    port = upnp_portmap_create_record(UPNP_SOURCE_IGD_POLL, "123", "10.0.0.1", "123",
                                      "TCP", "port 1", "0", "", "2000");
    ds_tree_insert(u_session->mapped_ports, port, port);
    u_session->n_mapped_ports++;

    LOGD("test: n_mapped_ports = %zu", u_session->n_mapped_ports);

    upnp_report_aggregator_add_port(u_session->aggr, port);

    ret = upnp_portmap_send_report(&fsm);
    TEST_ASSERT_EQUAL_INT(0, ret);
    (void)ret;

    /* Cleanup */
    upnp_portmap_exit(&fsm);
    FREE(u_session);
    FREE(fsm.handler_ctxt);
}

extern ds_tree_t *local_copy;
extern void callback_IP_Port_Forward(ovsdb_update_monitor_t *mon,
                                     struct schema_IP_Port_Forward *old_rec,
                                     struct schema_IP_Port_Forward *port_info);

void
test_callback_IP_Port_Forward(void)
{
    struct ovsdb_portmap_cache_t *cache = get_portmap_from_ovsdb();
    struct schema_IP_Port_Forward rec_one;
    struct schema_IP_Port_Forward rec_two;
    ovsdb_update_monitor_t mon;

    cache->store = upnp_portmap_create_snapshot();  /* Avoid the OVSDB stuff */
    cache->refcount = 1;
    cache->initialized = true;

    MEMZERO(rec_one);
    strcpy(rec_one.protocol, "tcp");
    strcpy(rec_one.dst_ipaddr, "10.0.0.1");
    rec_one.dst_port = 12345;
    rec_one.src_port = 9876;

    mon.mon_type = OVSDB_UPDATE_NEW;
    callback_IP_Port_Forward(&mon, NULL, &rec_one);

    mon.mon_type = OVSDB_UPDATE_DEL;
    callback_IP_Port_Forward(&mon, NULL, &rec_one);

    /* Perform the update using rec_two */
    MEMZERO(rec_one);
    strcpy(rec_one.protocol, "tcp");
    strcpy(rec_one.dst_ipaddr, "10.0.0.1");
    rec_one.dst_port = 12345;
    rec_one.src_port = 9876;

    mon.mon_type = OVSDB_UPDATE_NEW;
    callback_IP_Port_Forward(&mon, NULL, &rec_one);

    MEMZERO(rec_one);
    strcpy(rec_one.protocol, "tcp");
    strcpy(rec_one.dst_ipaddr, "10.0.0.1");
    rec_one.src_port = 9876;
    rec_one.dst_port = 12345;

    MEMZERO(rec_two);
    strcpy(rec_two.dst_ipaddr, "192.168.0.1");
    rec_two.dst_ipaddr_changed = true;

    mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_IP_Port_Forward(&mon, &rec_one, &rec_two);

    cache->initialized = false;
    cache->refcount = 0;
    upnp_portmap_delete_snapshot(cache->store);
}

//////////////////////////////////////////////
// TODO:
// The following test is DEVELOPMENT CODE !!
// We'll keep this around until things settle

void
test_upnp_portmap_fetch_static(void)
{
    struct ovsdb_portmap_cache_t *cache = get_portmap_from_ovsdb();
    struct fsm_dpi_sec_portmap_session *u_session;
    struct schema_IP_Port_Forward rec_one;
    ovsdb_update_monitor_t mon;
    struct fsm_session fsm;

    cache->store = upnp_portmap_create_snapshot();  /* Avoid the OVSDB stuff */
    cache->refcount = 1;
    cache->initialized = true;

    MEMZERO(rec_one);
    strcpy(rec_one.protocol, "tcp");
    strcpy(rec_one.dst_ipaddr, "10.0.0.1");
    rec_one.dst_port = 12345;
    rec_one.src_port = 9876;

    mon.mon_type = OVSDB_UPDATE_NEW;
    callback_IP_Port_Forward(&mon, NULL, &rec_one);

    MEMZERO(rec_one);
    strcpy(rec_one.protocol, "udp");
    strcpy(rec_one.dst_ipaddr, "10.0.0.222");
    rec_one.dst_port = 12345;
    rec_one.src_port = 9876;

    mon.mon_type = OVSDB_UPDATE_NEW;
    callback_IP_Port_Forward(&mon, NULL, &rec_one);

    MEMZERO(fsm);
    fsm.node_id = "NODE_ID";
    fsm.location_id = "LOCATION_ID";
    fsm.ops.send_pb_report = send_report_protobuf;
    fsm.name = "FSM_SESSION_NAME";
    fsm.handler_ctxt = CALLOC(1, sizeof(struct fsm_dpi_client_session));

    u_session = fsm_dpi_sec_portmap_get_session(&fsm);
    TEST_ASSERT_NOT_NULL(u_session);

    upnp_portmap_init(&fsm);

    upnp_portmap_fetch_static(&fsm);
    upnp_report_aggregator_add_snapshot(u_session->aggr, u_session->static_ports);
    TEST_ASSERT_EQUAL(2, u_session->aggr->n_ports);
    upnp_report_aggregator_dump(u_session->aggr);

    upnp_portmap_exit(&fsm);

    /* Cleanup */
    FREE(u_session);
    FREE(fsm.handler_ctxt);
}

void
test_upnp_portmap_fetch_mapped_from_file(void)
{
    struct entries
    {
        char *oneLine;
        char *protocol;
        char *desc;
    } entries[] =
    {
        {"TCP:9999:192.168.40.128:2222:0:both active", "TCP", "both active"},
        {"TCP:9999:192.168.40.128:2222:0:both active", "TCP", "both active"},         // Should NOT be counted as double
        {"UDP:7654:192.168.40.128:3456:0:udp port", "UDP", "udp port"},
        {"UDP:5555:192.168.40.128:1234:0:udp with extra : colon", "UDP", "udp with extra : colon"},
        {"UDP:8888:192.168.40.128:6666:0:", "UDP", ""},
        {"BADLY_BROKEN", "", ""},
        {"UDP:7654:192.168.40.128:3457:", "", ""},
        {"UDP:7654:192.168.40.128:", "", ""},
        {"UDP:7654:", "", ""},
        {"UDP:", "", ""},
    };
    char *temp_filename = "/tmp/upnp_portmap.leases";
    ds_tree_t *new_snapshot;
    int retval;
    FILE *fp;
    size_t i;

    /* create a small temp file with possible entries */
    fp = fopen(temp_filename, "w");
    for (i = 0; i < ARRAY_SIZE(entries); i++)
        fprintf(fp, "%s\n", entries[i].oneLine);
    fclose(fp);

    /* check content */
    new_snapshot = CALLOC(1, sizeof(*new_snapshot));
    ds_tree_init(new_snapshot, upnp_portmap_compare_record,
                 struct mapped_port_t, node);

    retval = upnp_portmap_fetch_mapped_from_file(new_snapshot, temp_filename);

    TEST_ASSERT_EQUAL_INT(4, retval);

    /* THIS IS NOT A TEST! But it helps :) */
    upnp_portmap_dump_snapshot(new_snapshot);

    /* Cleanup */
    upnp_portmap_delete_snapshot(new_snapshot);
    unlink(temp_filename);
}

//////////////////////////////////////////////

void
run_test_upnp_portmap(void)
{
    ut_setUp_tearDown(__func__, NULL, NULL);

    RUN_TEST(test_upnp_portmap_cmp);
    RUN_TEST(test_upnp_portmap_compare_snapshots);
    RUN_TEST(test_upnp_portmap_alloc_portmap);
    RUN_TEST(test_upnp_portmap_send_report);
    RUN_TEST(test_callback_IP_Port_Forward);
    RUN_TEST(test_upnp_portmap_fetch_static);
    RUN_TEST(test_upnp_portmap_fetch_mapped_from_file);
}
