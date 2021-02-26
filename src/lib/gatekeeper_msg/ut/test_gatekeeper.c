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
#include <netinet/in.h>
#include <stdint.h>

#include "fsm_policy.h"
#include "gatekeeper.pb-c.h"
#include "gatekeeper_msg.h"
#include "log.h"
#include "network_metadata_report.h"
#include "os_types.h"
#include "target.h"
#include "unity.h"

const char *test_name = "gatekeeper_tests";

os_macaddr_t g_test_mac =
{
    .addr = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 },
};


struct gk_req_header g_gk_req_header =
{
    .location_id = "59f39f5acbb22513f0ae5e17",
    .policy_rule = "test_policy_rule",
    .node_id = "4C718002B3",
    .dev_id = &g_test_mac,
    .req_id = 1,
};

struct test_tuple
{
    int family;
    uint16_t transport;
    char *src_ip;
    char *dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    int direction;
    int originator;
};
struct test_tuple g_v4_tuple =
{
    .family = AF_INET,
    .transport = IPPROTO_UDP,
    .src_ip = "1.2.3.4",
    .dst_ip = "8.7.6.5",
    .src_port = 12345,
    .dst_port = 32190,
    .direction = NET_MD_ACC_OUTBOUND_DIR,
    .originator = NET_MD_ACC_ORIGINATOR_SRC,
};


struct test_tuple g_v6_tuple =
{
    .family = AF_INET6,
    .transport = IPPROTO_UDP,
    .src_ip = "1:2::3",
    .dst_ip = "2:4::3",
    .src_port = 12345,
    .dst_port = 32190,
    .direction = NET_MD_ACC_OUTBOUND_DIR,
    .originator = NET_MD_ACC_ORIGINATOR_SRC,
};


struct net_md_stats_accumulator g_v4_acc;
struct net_md_flow_key g_v4_key;

struct net_md_stats_accumulator g_v6_acc;
struct net_md_flow_key g_v6_key;

char *pb_files[] =
{
    "/tmp/gk_fqdn.bin",       /* 0 */
    "/tmp/gk_https_sni.bin",  /* 1 */
    "/tmp/gk_http_url.bin",   /* 2 */
    "/tmp/gk_http_host.bin",  /* 3 */
    "/tmp/gk_app.bin",        /* 4 */
    "/tmp/gk_ipv4.bin",       /* 5 */
    "/tmp/gk_ipv6.bin",       /* 6 */
};


/**
 * @brief writes the contents of a serialized buffer in a file
 *
 * @param pb serialized buffer to be written
 * @param fpath target file path
 *
 * @return returns the number of bytes written
 */
static size_t pb2file(struct gk_packed_buffer *pb, char *fpath)
{
    FILE *f = fopen(fpath, "w");
    size_t nwrite = fwrite(pb->buf, 1, pb->len, f);
    fclose(f);

    return nwrite;
}


void
setUp(void)
{
    int rc;

    /* Create an ipv4 accumulator */
    memset(&g_v4_acc, 0, sizeof(g_v4_acc));
    memset(&g_v4_key, 0, sizeof(g_v4_key));

    g_v4_key.src_ip = calloc(1, 4);
    TEST_ASSERT_NOT_NULL(g_v4_key.src_ip);
    rc = inet_pton(AF_INET, g_v4_tuple.src_ip, g_v4_key.src_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v4_key.dst_ip = calloc(1, 4);
    TEST_ASSERT_NOT_NULL(g_v4_key.dst_ip);
    rc = inet_pton(AF_INET, g_v4_tuple.dst_ip, g_v4_key.dst_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v4_key.ip_version = 4;
    g_v4_key.ipprotocol = g_v4_tuple.transport;
    g_v4_key.sport = htons(g_v4_tuple.src_port);
    g_v4_key.dport = htons(g_v4_tuple.dst_port);

    g_v4_acc.key = &g_v4_key;
    g_v4_acc.direction = g_v4_tuple.direction;
    g_v4_acc.originator = g_v4_tuple.originator;

    /* Create an ipv6 accumulator */
    memset(&g_v6_acc, 0, sizeof(g_v6_acc));
    memset(&g_v6_key, 0, sizeof(g_v6_key));

    g_v6_key.src_ip = calloc(1, 16);
    TEST_ASSERT_NOT_NULL(g_v6_key.src_ip);
    rc = inet_pton(AF_INET6, g_v6_tuple.src_ip, g_v6_key.src_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v6_key.dst_ip = calloc(1, 16);
    TEST_ASSERT_NOT_NULL(g_v6_key.dst_ip);
    rc = inet_pton(AF_INET6, g_v6_tuple.dst_ip, g_v6_key.dst_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v6_key.ip_version = 6;
    g_v6_key.ipprotocol = g_v6_tuple.transport;
    g_v6_key.sport = htons(g_v6_tuple.src_port);
    g_v6_key.dport = htons(g_v6_tuple.dst_port);

    g_v6_acc.key = &g_v6_key;
    g_v6_acc.direction = g_v6_tuple.direction;
    g_v6_acc.originator = g_v6_tuple.originator;

    return;
}


void
tearDown(void)
{
    free(g_v4_key.src_ip);
    free(g_v4_key.dst_ip);
    memset(&g_v4_key, 0, sizeof(g_v4_key));

    memset(&g_v4_acc, 0, sizeof(g_v4_acc));

    free(g_v6_key.src_ip);
    free(g_v6_key.dst_ip);
    memset(&g_v6_key, 0, sizeof(g_v6_key));

    memset(&g_v6_acc, 0, sizeof(g_v6_acc));
}


void
test_serialize_fqdn_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_fqdn_request *gk_fqdn_req;
    struct gk_packed_buffer *gk_pb;
    char *fqdn = "www.plume.com";
    union gk_data_req *req_data;
    struct gk_request req;

    memset(&req, 0, sizeof(req));
    req.type = FSM_FQDN_REQ;
    req_data = &req.req;
    gk_fqdn_req = &req_data->gk_fqdn_req;
    gk_fqdn_req->header = &g_gk_req_header;
    gk_fqdn_req->fqdn = fqdn;

    /* Serialize the fqdn request */
    gk_pb = gk_serialize_request(&req);
    TEST_ASSERT_NOT_NULL(gk_pb);
    TEST_ASSERT_TRUE(gk_pb->len != 0);

    /* Deserialize buffer */
    unpacked_req = gatekeeper__southbound__v1__gatekeeper_req__unpack(NULL,
                                                                      gk_pb->len,
                                                                      gk_pb->buf);

    /* Save the serialized protobuf in a file */
    pb2file(gk_pb, pb_files[0]);

    /* Free packed buffer */
    gk_free_packed_buffer(gk_pb);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(unpacked_req);

    /* Free the unpacked protobuf */
    gatekeeper__southbound__v1__gatekeeper_req__free_unpacked(unpacked_req, NULL);
}


void
test_serialize_https_sni_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_sni_request *gk_sni_req;
    struct gk_packed_buffer *gk_pb;
    char *sni = "sni_foo";
    union gk_data_req *req_data;
    struct gk_request req;

    memset(&req, 0, sizeof(req));
    req.type = FSM_SNI_REQ;
    req_data = &req.req;
    gk_sni_req = &req_data->gk_sni_req;
    gk_sni_req->header = &g_gk_req_header;
    gk_sni_req->sni = sni;

    /* Serialize the fqdn request */
    gk_pb = gk_serialize_request(&req);
    TEST_ASSERT_NOT_NULL(gk_pb);
    TEST_ASSERT_TRUE(gk_pb->len != 0);

    /* Save the serialized protobuf in a file */
    pb2file(gk_pb, pb_files[1]);

    /* Deserialize buffer */
    unpacked_req = gatekeeper__southbound__v1__gatekeeper_req__unpack(NULL,
                                                                      gk_pb->len,
                                                                      gk_pb->buf);

    /* Free packed buffer */
    gk_free_packed_buffer(gk_pb);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(unpacked_req);

    /* Free the unpacked protobuf */
    gatekeeper__southbound__v1__gatekeeper_req__free_unpacked(unpacked_req, NULL);
}


void
test_serialize_http_host_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_host_request *gk_host_req;
    struct gk_packed_buffer *gk_pb;
    char *host = "host_foo";
    union gk_data_req *req_data;
    struct gk_request req;

    memset(&req, 0, sizeof(req));
    req.type = FSM_HOST_REQ;
    req_data = &req.req;
    gk_host_req = &req_data->gk_host_req;
    gk_host_req->header = &g_gk_req_header;
    gk_host_req->host = host;

    /* Serialize the fqdn request */
    gk_pb = gk_serialize_request(&req);
    TEST_ASSERT_NOT_NULL(gk_pb);
    TEST_ASSERT_TRUE(gk_pb->len != 0);

    /* Deserialize buffer */
    unpacked_req = gatekeeper__southbound__v1__gatekeeper_req__unpack(NULL,
                                                                      gk_pb->len,
                                                                      gk_pb->buf);

    /* Save the serialized protobuf in a file */
    pb2file(gk_pb, pb_files[3]);

    /* Free packed buffer */
    gk_free_packed_buffer(gk_pb);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(unpacked_req);

    /* Free the unpacked protobuf */
    gatekeeper__southbound__v1__gatekeeper_req__free_unpacked(unpacked_req, NULL);
}


void
test_serialize_http_url_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_url_request *gk_url_req;
    struct gk_packed_buffer *gk_pb;
    char *url = "host_url";
    union gk_data_req *req_data;
    struct gk_request req;

    memset(&req, 0, sizeof(req));
    req.type = FSM_URL_REQ;
    req_data = &req.req;
    gk_url_req = &req_data->gk_url_req;
    gk_url_req->header = &g_gk_req_header;
    gk_url_req->url = url;

    /* Serialize the fqdn request */
    gk_pb = gk_serialize_request(&req);
    TEST_ASSERT_NOT_NULL(gk_pb);
    TEST_ASSERT_TRUE(gk_pb->len != 0);

    /* Deserialize buffer */
    unpacked_req = gatekeeper__southbound__v1__gatekeeper_req__unpack(NULL,
                                                                      gk_pb->len,
                                                                      gk_pb->buf);

    /* Save the serialized protobuf in a file */
    pb2file(gk_pb, pb_files[2]);

    /* Free packed buffer */
    gk_free_packed_buffer(gk_pb);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(unpacked_req);

    /* Free the unpacked protobuf */
    gatekeeper__southbound__v1__gatekeeper_req__free_unpacked(unpacked_req, NULL);
}


void
test_serialize_app_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_app_request *gk_app_req;
    struct gk_packed_buffer *gk_pb;
    char *app = "app_foo";
    union gk_data_req *req_data;
    struct gk_request req;

    memset(&req, 0, sizeof(req));
    req.type = FSM_APP_REQ;
    req_data = &req.req;
    gk_app_req = &req_data->gk_app_req;
    gk_app_req->header = &g_gk_req_header;
    gk_app_req->appname = app;

    /* Serialize the fqdn request */
    gk_pb = gk_serialize_request(&req);
    TEST_ASSERT_NOT_NULL(gk_pb);
    TEST_ASSERT_TRUE(gk_pb->len != 0);

    /* Deserialize buffer */
    unpacked_req = gatekeeper__southbound__v1__gatekeeper_req__unpack(NULL,
                                                                      gk_pb->len,
                                                                      gk_pb->buf);

    /* Save the serialized protobuf in a file */
    pb2file(gk_pb, pb_files[4]);

    /* Free packed buffer */
    gk_free_packed_buffer(gk_pb);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(unpacked_req);

    /* Free the unpacked protobuf */
    gatekeeper__southbound__v1__gatekeeper_req__free_unpacked(unpacked_req, NULL);
}


void
test_serialize_ipv4_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_ip_request *gk_ip_req;
    struct gk_packed_buffer *gk_pb;
    union gk_data_req *req_data;
    struct gk_request req;

    memset(&req, 0, sizeof(req));
    req.type = FSM_IPV4_REQ;
    req_data = &req.req;
    gk_ip_req = &req_data->gk_ip_req;
    gk_ip_req->header = &g_gk_req_header;
    gk_ip_req->acc = &g_v4_acc;

    /* Serialize the fqdn request */
    gk_pb = gk_serialize_request(&req);
    TEST_ASSERT_NOT_NULL(gk_pb);
    TEST_ASSERT_TRUE(gk_pb->len != 0);

    /* Deserialize buffer */
    unpacked_req = gatekeeper__southbound__v1__gatekeeper_req__unpack(NULL,
                                                                      gk_pb->len,
                                                                      gk_pb->buf);

    /* Save the serialized protobuf in a file */
    pb2file(gk_pb, pb_files[5]);

    /* Free packed buffer */
    gk_free_packed_buffer(gk_pb);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(unpacked_req);

    /* Free the unpacked protobuf */
    gatekeeper__southbound__v1__gatekeeper_req__free_unpacked(unpacked_req, NULL);
}


void
test_serialize_ipv6_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_ip_request *gk_ip_req;
    struct gk_packed_buffer *gk_pb;
    union gk_data_req *req_data;
    struct gk_request req;

    memset(&req, 0, sizeof(req));
    req.type = FSM_IPV6_REQ;
    req_data = &req.req;
    gk_ip_req = &req_data->gk_ip_req;
    gk_ip_req->header = &g_gk_req_header;
    gk_ip_req->acc = &g_v6_acc;

    /* Serialize the fqdn request */
    gk_pb = gk_serialize_request(&req);
    TEST_ASSERT_NOT_NULL(gk_pb);
    TEST_ASSERT_TRUE(gk_pb->len != 0);

    /* Deserialize buffer */
    unpacked_req = gatekeeper__southbound__v1__gatekeeper_req__unpack(NULL,
                                                                      gk_pb->len,
                                                                      gk_pb->buf);

    /* Save the serialized protobuf in a file */
    pb2file(gk_pb, pb_files[6]);

    /* Free packed buffer */
    gk_free_packed_buffer(gk_pb);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(unpacked_req);

    /* Free the unpacked protobuf */
    gatekeeper__southbound__v1__gatekeeper_req__free_unpacked(unpacked_req, NULL);
}


int
main(int argc, char *argv[])
{
    /* Set the logs to stdout */
    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    RUN_TEST(test_serialize_fqdn_request);
    RUN_TEST(test_serialize_https_sni_request);
    RUN_TEST(test_serialize_http_host_request);
    RUN_TEST(test_serialize_http_url_request);
    RUN_TEST(test_serialize_app_request);
    RUN_TEST(test_serialize_ipv4_request);
    RUN_TEST(test_serialize_ipv6_request);

    return UNITY_END();
}

