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
#include "log.h"
#include "network_metadata_report.h"
#include "gatekeeper.pb-c.h"
#include "os_types.h"
#include "target.h"
#include "unity.h"
#include "memutil.h"

#include "gatekeeper.pb-c.h"
#include "gatekeeper_msg.h"
#include "gatekeeper_bulk_reply_msg.h"

#include "test_gatekeeper_msg.h"
#include "unit_test_utils.h"

os_macaddr_t g_test_mac =
{
    .addr = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 },
};

os_macaddr_t g_test_mac2 =
{
    .addr = { 0x22, 0x22, 0x22, 0x22, 0x22, 0x22 },
};

struct gk_req_header g_gk_req_header =
{
    .location_id = "59f39f5acbb22513f0ae5e17",
    .policy_rule = "test_policy_rule",
    .node_id = "4C718002B3",
    .dev_id = &g_test_mac,
    .req_id = 1,
    .network_id = "test_network"
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
    "/tmp/gk_fqdn.bin",      /* 0 */
    "/tmp/gk_https_sni.bin", /* 1 */
    "/tmp/gk_http_url.bin",  /* 2 */
    "/tmp/gk_http_host.bin", /* 3 */
    "/tmp/gk_app.bin",       /* 4 */
    "/tmp/gk_ipv4.bin",      /* 5 */
    "/tmp/gk_ipv6.bin",      /* 6 */
    "/tmp/gk_ipv4_flow.bin", /* 7 */
    "/tmp/gk_ipv6_flow.bin", /* 8 */
    "/tmp/gk_policy_apps.bin", /* 9 */
    "/tmp/gk_bulk_req.bin",    /* 10 */
};


/**
 * @brief writes the contents of a serialized buffer in a file
 *
 * @param pb serialized buffer to be written
 * @param fpath target file path
 *
 * @return returns the number of bytes written
 */
static size_t
pb2file(struct gk_packed_buffer *pb, char *fpath)
{
    FILE *f = fopen(fpath, "w");
    size_t nwrite = fwrite(pb->buf, 1, pb->len, f);
    fclose(f);

    return nwrite;
}

void
msg_setUp(void)
{
    int rc;

    /* Create an ipv4 accumulator */
    memset(&g_v4_acc, 0, sizeof(g_v4_acc));
    memset(&g_v4_key, 0, sizeof(g_v4_key));

    g_v4_key.src_ip = CALLOC(1, 4);
    rc = inet_pton(AF_INET, g_v4_tuple.src_ip, g_v4_key.src_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v4_key.dst_ip = CALLOC(1, 4);
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

    g_v6_key.src_ip = CALLOC(1, 16);
    rc = inet_pton(AF_INET6, g_v6_tuple.src_ip, g_v6_key.src_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    g_v6_key.dst_ip = CALLOC(1, 16);
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
msg_tearDown(void)
{
    FREE(g_v4_key.src_ip);
    FREE(g_v4_key.dst_ip);
    memset(&g_v4_key, 0, sizeof(g_v4_key));

    memset(&g_v4_acc, 0, sizeof(g_v4_acc));

    FREE(g_v6_key.src_ip);
    FREE(g_v6_key.dst_ip);
    memset(&g_v6_key, 0, sizeof(g_v6_key));

    memset(&g_v6_acc, 0, sizeof(g_v6_acc));
}

void
test_serialize_fqdn_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_fqdn_request *gk_fqdn_req;
    struct gk_packed_buffer *gk_pb;
    union gk_data_req *req_data;
    struct gk_request req;
    char *fqdn = "www.plume.com";

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
    union gk_data_req *req_data;
    struct gk_request req;
    char *sni = "sni_foo";

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
    union gk_data_req *req_data;
    struct gk_request req;
    char *host = "host_foo";

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
    union gk_data_req *req_data;
    struct gk_request req;
    char *url = "host_url";

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
    union gk_data_req *req_data;
    struct gk_request req;
    char *app = "app_foo";

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

void
test_serialize_ipv4_flow_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_ip_request *gk_ip_req;
    struct gk_packed_buffer *gk_pb;
    union gk_data_req *req_data;
    struct gk_request req;

    memset(&req, 0, sizeof(req));
    req.type = FSM_IPV4_FLOW_REQ;
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
    pb2file(gk_pb, pb_files[7]);

    /* Free packed buffer */
    gk_free_packed_buffer(gk_pb);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(unpacked_req);

    /* Free the unpacked protobuf */
    gatekeeper__southbound__v1__gatekeeper_req__free_unpacked(unpacked_req, NULL);
}

void
test_serialize_ipv6_flow_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_ip_request *gk_ip_req;
    struct gk_packed_buffer *gk_pb;
    union gk_data_req *req_data;
    struct gk_request req;

    memset(&req, 0, sizeof(req));
    req.type = FSM_IPV6_FLOW_REQ;
    req_data = &req.req;

    /* Serialize a broken fqdn request (no gk_ip_req) */
    gk_pb = gk_serialize_request(&req);
    TEST_ASSERT_NULL(gk_pb);

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
    pb2file(gk_pb, pb_files[8]);

    /* Free packed buffer */
    gk_free_packed_buffer(gk_pb);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(unpacked_req);

    /* Free the unpacked protobuf */
    gatekeeper__southbound__v1__gatekeeper_req__free_unpacked(unpacked_req, NULL);
}

static char *test_create_app_req(int i)
{
    const char *app_names[] = {"app_0", "app_1"};
    char *app;

    if (i < 0 || i > 1) {
        return NULL;
    }

    app = strdup(app_names[i]);

    return app;
}

static struct gk_device2app_req*
test_create_device_req(int i)
{
    if (i > 1) return NULL;

    struct gk_device2app_req *dev_app = MALLOC(sizeof(struct gk_device2app_req));

    dev_app->n_apps = 2;
    dev_app->apps = MALLOC(2 * sizeof(char *));

    dev_app->header = &g_gk_req_header;
    dev_app->header->dev_id = i == 0 ? &g_test_mac : &g_test_mac2;

    for(size_t j = 0; j < dev_app->n_apps; j++)
    {
        dev_app->apps[j] = test_create_app_req(j);
    }
    return dev_app;
}

void
test_populate_sample_request(struct gk_bulk_request *bulk_req)
{
    bulk_req->req_type = FSM_APP_REQ;
    bulk_req->n_devices = 2;
    bulk_req->devices = CALLOC(bulk_req->n_devices, sizeof(*bulk_req->devices));

    for (size_t i = 0; i < bulk_req->n_devices; i++)
    {
        bulk_req->devices[i] = test_create_device_req(i);
    }

}

static void free_app_action(char *app)
{
    FREE(app);
}

static void free_device_app(struct gk_device2app_req *dev_app)
{
    for (size_t i = 0; i < dev_app->n_apps; i++)
    {
        free_app_action(dev_app->apps[i]);
    }

    free(dev_app->apps);
    free(dev_app);
}

void fcm_free_bulk_request(struct gk_bulk_request *req)
{
    /* free struct devices request  */
    for (size_t i = 0; i < req->n_devices; i++)
    {
        free_device_app(req->devices[i]);
    }

    FREE(req->devices);
}

void test_serialize_bulk_request(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReq *unpacked_req;
    struct gk_bulk_request *gk_bulk_req;
    struct gk_packed_buffer *gk_pb;
    union gk_data_req *req_data;
    struct gk_request req;

    memset(&req, 0, sizeof(req));
    req.type = FSM_BULK_REQ;
    req_data = &req.req;

    gk_bulk_req = &req_data->gk_bulk_req;

    test_populate_sample_request(gk_bulk_req);

    /* Serialize the bulk request */
    gk_pb = gk_serialize_request(&req);
    TEST_ASSERT_NOT_NULL(gk_pb);
    TEST_ASSERT_TRUE(gk_pb->len != 0);

    /* Deserialize buffer */
    unpacked_req = gatekeeper__southbound__v1__gatekeeper_req__unpack(NULL,
                                                                      gk_pb->len,
                                                                      gk_pb->buf);

    /* Save the serialized protobuf in a file */
    pb2file(gk_pb, pb_files[10]);

    /* Validate the deserialized content */
    TEST_ASSERT_NOT_NULL(unpacked_req);

    fcm_free_bulk_request(gk_bulk_req);

    /* Free packed buffer */
    gk_free_packed_buffer(gk_pb);

    /* Free the unpacked protobuf */
    gatekeeper__southbound__v1__gatekeeper_req__free_unpacked(unpacked_req, NULL);
}

struct ProtobufCBinaryData create_protobuf_c_binary_data(const uint8_t *data, size_t len) {
    struct ProtobufCBinaryData binary_data;
    binary_data.len = len;
    binary_data.data = (uint8_t *)malloc(len);
    memcpy(binary_data.data, data, len);
    return binary_data;
}


static void dump_response(struct gk_reply *reply)
{
    struct gk_bulk_reply *bulk_reply;

    bulk_reply = (struct gk_bulk_reply *)&reply->data_reply;

    for (size_t i = 0; i < bulk_reply->n_devices; i++)
    {
        LOGN("APP: %s, MAC: %s action %d", bulk_reply->devices[i]->app_name, bulk_reply->devices[i]->header->dev_id, bulk_reply->devices[i]->header->action);
    }
}

void test_bulk_verdict(void)
{
    struct gk_request request;
    uint8_t device_id_data[6];
    struct gk_reply reply;
    char *app = "TestApp";

    request.type = FSM_BULK_REQ;
    Gatekeeper__Southbound__V1__GatekeeperReply gk_reply;

    /* initialize bulk request */
    struct gk_bulk_request *bulk_request = &request.req.gk_bulk_req;

    // Initialize devices
    bulk_request->n_devices = 1;
    bulk_request->devices = MALLOC(sizeof(struct gk_device2app_req*) * bulk_request->n_devices);

    // Initialize a single gk_device2app_req
    bulk_request->devices[0] = MALLOC(sizeof(struct gk_device2app_req));
    bulk_request->devices[0]->n_apps = 1;
    bulk_request->devices[0]->apps = MALLOC(sizeof(struct gk_app_info*) * bulk_request->devices[0]->n_apps);
    bulk_request->devices[0]->apps[0] = app;


    gk_reply.bulk_reply = MALLOC(sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    gk_reply.bulk_reply->n_reply_app = 1;
    gk_reply.bulk_reply->reply_app = MALLOC(sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReply*) * gk_reply.bulk_reply->n_reply_app);

    gk_reply.bulk_reply->reply_app[0] = MALLOC(sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReply));
    gk_reply.bulk_reply->reply_app[0]->app_name = app;
    gk_reply.bulk_reply->reply_app[0]->header = MALLOC(sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    gk_reply.bulk_reply->reply_app[0]->header->request_id = 1234;
    gk_reply.bulk_reply->reply_app[0]->header->action = 1;
    gk_reply.bulk_reply->reply_app[0]->header->ttl = 60;
    gk_reply.bulk_reply->reply_app[0]->header->policy = g_gk_req_header.policy_rule;
    gk_reply.bulk_reply->reply_app[0]->header->category_id = 5678;
    gk_reply.bulk_reply->reply_app[0]->header->confidence_level = 90;
    gk_reply.bulk_reply->reply_app[0]->header->flow_marker = 7890;

    // Initialize device_id
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_reply.bulk_reply->reply_app[0]->header->device_id = create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));

    reply.type = FSM_BULK_REQ;

    /* parse gk_reply and populte reply */
    gk_parse_reply(&reply, &gk_reply);

    dump_response(&reply);

    /* clean up */
    gk_clear_bulk_responses(&reply);

    FREE(bulk_request->devices[0]->apps);
    FREE(bulk_request->devices[0]);
    FREE(bulk_request->devices);
    FREE(gk_reply.bulk_reply->reply_app[0]->header->device_id.data);
    FREE(gk_reply.bulk_reply->reply_app[0]->header);
    FREE(gk_reply.bulk_reply->reply_app[0]);
    FREE(gk_reply.bulk_reply->reply_app);
    FREE(gk_reply.bulk_reply);
}

static void
test_free_app_reply(Gatekeeper__Southbound__V1__GatekeeperAppReply *app_reply)
{
    if (app_reply == NULL) return;

    FREE(app_reply->app_name);
    if (app_reply->header)
    {
        FREE(app_reply->header->policy);
        FREE(app_reply->header->device_id.data);
        FREE(app_reply->header);
    }
    FREE(app_reply);
}


static void
test_free_bulk_reply(Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply)
{
    if (bulk_reply == NULL) return;

    for(size_t i = 0; i < bulk_reply->n_reply_app; i++)
    {
        test_free_app_reply(bulk_reply->reply_app[i]);
    }

    bulk_reply->n_reply_app = 0;
    FREE(bulk_reply->reply_app);
}

static void
test_free_gk_reply(Gatekeeper__Southbound__V1__GatekeeperReply *gk_pb_reply)
{
    if (gk_pb_reply == NULL) return;

    if (gk_pb_reply->bulk_reply)
    {
        test_free_bulk_reply(gk_pb_reply->bulk_reply);
        FREE(gk_pb_reply->bulk_reply);
    }
}

static void
test_populate_app_reply(Gatekeeper__Southbound__V1__GatekeeperAppReply *app_reply, size_t i)
{
    uint8_t device_id_data[6];

    app_reply->app_name = test_create_app_req(i);

    app_reply->header = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    app_reply->header->request_id = 1;
    app_reply->header->action = 1;
    app_reply->header->ttl = 1;
    app_reply->header->policy = STRDUP("test_policy");

    if (i == 0)
        memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    else
        memcpy(device_id_data, g_test_mac2.addr, sizeof(g_test_mac2.addr));
    app_reply->header->device_id = create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));

}

static void
test_populate_app_bulk_reply(Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_app_reply)
{
    bulk_app_reply->n_reply_app = 2;
    bulk_app_reply->reply_app = CALLOC(bulk_app_reply->n_reply_app, sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReply *));

    for (size_t i = 0; i < bulk_app_reply->n_reply_app; i++)
    {
        bulk_app_reply->reply_app[i] = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReply));
        test_populate_app_reply(bulk_app_reply->reply_app[i], i);
    }
}

static void test_populate_bulk_reply(Gatekeeper__Southbound__V1__GatekeeperReply *gk_pb_reply)
{
    gk_pb_reply->bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    test_populate_app_bulk_reply(gk_pb_reply->bulk_reply);
}

void test_parse_bulk_reply(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply;
    struct gk_reply reply;

    memset(&gk_pb_reply, 0, sizeof(gk_pb_reply));

    test_populate_bulk_reply(&gk_pb_reply);

    reply.type = FSM_BULK_REQ;
    gk_parse_reply(&reply, &gk_pb_reply);

    dump_response(&reply);
    gk_clear_bulk_responses(&reply);
    test_free_gk_reply(&gk_pb_reply);
}

void
test_serialize_broken_request(void)
{
    struct gk_packed_buffer *gk_pb;

    gk_pb = gk_serialize_request(NULL);
    TEST_ASSERT_NULL(gk_pb);
}

void
run_test_gatekeeper_msg(void)
{
    ut_setUp_tearDown(__func__, msg_setUp, msg_tearDown);

    RUN_TEST(test_serialize_broken_request);
    RUN_TEST(test_serialize_fqdn_request);
    RUN_TEST(test_serialize_https_sni_request);
    RUN_TEST(test_serialize_http_host_request);
    RUN_TEST(test_serialize_http_url_request);
    RUN_TEST(test_serialize_app_request);
    RUN_TEST(test_serialize_ipv4_request);
    RUN_TEST(test_serialize_ipv6_request);
    RUN_TEST(test_serialize_ipv4_flow_request);
    RUN_TEST(test_serialize_ipv6_flow_request);
    RUN_TEST(test_serialize_bulk_request);
    RUN_TEST(test_parse_bulk_reply);
    RUN_TEST(test_bulk_verdict);

    ut_setUp_tearDown(NULL, NULL, NULL);
}
