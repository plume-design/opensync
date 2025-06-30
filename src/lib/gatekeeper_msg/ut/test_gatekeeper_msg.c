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
#include "gatekeeper_cache.h"
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

    /* Initialize the reply structure */
    memset(&gk_reply, 0, sizeof(gk_reply));

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


    gk_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
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


/**
 * @brief Test populating and parsing HTTP URL entries in a bulk reply
 * 
 * This test validates that HTTP URL entries can be correctly populated 
 * in a bulk reply and then parsed by gk_parse_reply.
 */
void test_populate_http_host_bulk_reply(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply;
    struct gk_reply reply;
    uint8_t device_id_data[6];
    char *url1 = "http://example.com/page1.html";
    char *url2 = "http://test.org/search?q=stuff";

    /* Initialize the reply structure */
    memset(&gk_pb_reply, 0, sizeof(gk_pb_reply));
    
    /* Create bulk reply structure */
    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);
    
    /* Prepare the HTTP URL entries */
    gk_pb_reply.bulk_reply->n_reply_http_url = 2;
    gk_pb_reply.bulk_reply->reply_http_url = 
        CALLOC(gk_pb_reply.bulk_reply->n_reply_http_url, 
               sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpUrlReply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_url);
    
    /* Create first URL entry */
    gk_pb_reply.bulk_reply->reply_http_url[0] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpUrlReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_url[0]);
    
    gk_pb_reply.bulk_reply->reply_http_url[0]->http_url = STRDUP(url1);
    gk_pb_reply.bulk_reply->reply_http_url[0]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_url[0]->header);
    
    /* Set header fields for first URL */
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT;
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->category_id = 123;
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->confidence_level = 90;
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->ttl = 3600;
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->policy = STRDUP("test_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Create second URL entry */
    gk_pb_reply.bulk_reply->reply_http_url[1] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpUrlReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_url[1]);
    
    gk_pb_reply.bulk_reply->reply_http_url[1]->http_url = STRDUP(url2);
    gk_pb_reply.bulk_reply->reply_http_url[1]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_url[1]->header);
    
    /* Set header fields for second URL */
    gk_pb_reply.bulk_reply->reply_http_url[1]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_BLOCK;
    gk_pb_reply.bulk_reply->reply_http_url[1]->header->category_id = 456;
    gk_pb_reply.bulk_reply->reply_http_url[1]->header->confidence_level = 95;
    gk_pb_reply.bulk_reply->reply_http_url[1]->header->ttl = 7200;
    gk_pb_reply.bulk_reply->reply_http_url[1]->header->policy = STRDUP("block_policy");
    
    memcpy(device_id_data, g_test_mac2.addr, sizeof(g_test_mac2.addr));
    gk_pb_reply.bulk_reply->reply_http_url[1]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Set up the reply to parse with FSM_BULK_REQ type */
    reply.type = FSM_BULK_REQ;
    
    /* Parse the reply */
    bool result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);
    
    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(2, bulk_reply->n_devices);
    
    /* Validate first URL entry */
    TEST_ASSERT_NOT_NULL(bulk_reply->devices[0]);
    TEST_ASSERT_EQUAL_INT(GK_ENTRY_TYPE_URL, bulk_reply->devices[0]->type);
    TEST_ASSERT_EQUAL_STRING(url1, bulk_reply->devices[0]->url);
    TEST_ASSERT_EQUAL_INT(FSM_ALLOW, bulk_reply->devices[0]->header->action);
    TEST_ASSERT_EQUAL_INT(123, bulk_reply->devices[0]->header->category_id);
    
    /* Validate second URL entry */
    TEST_ASSERT_NOT_NULL(bulk_reply->devices[1]);
    TEST_ASSERT_EQUAL_INT(GK_ENTRY_TYPE_URL, bulk_reply->devices[1]->type);
    TEST_ASSERT_EQUAL_STRING(url2, bulk_reply->devices[1]->url);
    TEST_ASSERT_EQUAL_INT(FSM_BLOCK, bulk_reply->devices[1]->header->action);
    TEST_ASSERT_EQUAL_INT(456, bulk_reply->devices[1]->header->category_id);
    
    /* Clean up */
    gk_clear_bulk_responses(&reply);
    
    /* Clean up protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_http_url[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[0]->http_url);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[0]);
    
    FREE(gk_pb_reply.bulk_reply->reply_http_url[1]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[1]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[1]->header);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[1]->http_url);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[1]);
    
    FREE(gk_pb_reply.bulk_reply->reply_http_url);
    FREE(gk_pb_reply.bulk_reply);
}

/**
 * @brief Test populating and parsing HTTPS SNI entries in a bulk reply
 * 
 * This test validates that HTTPS SNI entries can be correctly populated 
 * in a bulk reply and then parsed by gk_parse_reply.
 */
void test_populate_sni_bulk_reply(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply;
    struct gk_reply reply;
    uint8_t device_id_data[6];
    char *sni1 = "example.com";
    char *sni2 = "secure.test.org";
    
    /* Initialize the reply structure */
    memset(&gk_pb_reply, 0, sizeof(gk_pb_reply));
    
    /* Create bulk reply structure */
    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);
    
    /* Prepare the HTTPS SNI entries */
    gk_pb_reply.bulk_reply->n_reply_https_sni = 2;
    gk_pb_reply.bulk_reply->reply_https_sni = 
        CALLOC(gk_pb_reply.bulk_reply->n_reply_https_sni, 
               sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpsSniReply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_https_sni);
    
    /* Create first SNI entry */
    gk_pb_reply.bulk_reply->reply_https_sni[0] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpsSniReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_https_sni[0]);
    
    gk_pb_reply.bulk_reply->reply_https_sni[0]->https_sni = STRDUP(sni1);
    gk_pb_reply.bulk_reply->reply_https_sni[0]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_https_sni[0]->header);
    
    /* Set header fields for first SNI */
    gk_pb_reply.bulk_reply->reply_https_sni[0]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT;
    gk_pb_reply.bulk_reply->reply_https_sni[0]->header->category_id = 123;
    gk_pb_reply.bulk_reply->reply_https_sni[0]->header->confidence_level = 90;
    gk_pb_reply.bulk_reply->reply_https_sni[0]->header->ttl = 3600;
    gk_pb_reply.bulk_reply->reply_https_sni[0]->header->flow_marker = 1001;
    gk_pb_reply.bulk_reply->reply_https_sni[0]->header->policy = STRDUP("sni_allow_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_https_sni[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Create second SNI entry */
    gk_pb_reply.bulk_reply->reply_https_sni[1] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpsSniReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_https_sni[1]);
    
    gk_pb_reply.bulk_reply->reply_https_sni[1]->https_sni = STRDUP(sni2);
    gk_pb_reply.bulk_reply->reply_https_sni[1]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_https_sni[1]->header);
    
    /* Set header fields for second SNI */
    gk_pb_reply.bulk_reply->reply_https_sni[1]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_BLOCK;
    gk_pb_reply.bulk_reply->reply_https_sni[1]->header->category_id = 456;
    gk_pb_reply.bulk_reply->reply_https_sni[1]->header->confidence_level = 95;
    gk_pb_reply.bulk_reply->reply_https_sni[1]->header->ttl = 7200;
    gk_pb_reply.bulk_reply->reply_https_sni[1]->header->flow_marker = 1002;
    gk_pb_reply.bulk_reply->reply_https_sni[1]->header->policy = STRDUP("sni_block_policy");
    
    memcpy(device_id_data, g_test_mac2.addr, sizeof(g_test_mac2.addr));
    gk_pb_reply.bulk_reply->reply_https_sni[1]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Set up the reply to parse with FSM_BULK_REQ type */
    reply.type = FSM_BULK_REQ;
    
    /* Parse the reply */
    bool result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);
    
    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(2, bulk_reply->n_devices);
    
    /* Check that entries are created as expected - should be handled using a lookup by type 
       since gk_parse_bulk_reply doesn't guarantee any specific order */
    int sni_entries_found = 0;
    
    for (size_t i = 0; i < bulk_reply->n_devices; i++) {
        struct gk_device2app_repl *entry = bulk_reply->devices[i];
        TEST_ASSERT_NOT_NULL(entry);
        
        /* Skip non-SNI entries */
        if (entry->type != GK_ENTRY_TYPE_SNI) continue;
        
        sni_entries_found++;
        
        /* Check if this is the first or second SNI entry based on the name */
        if (strcmp(entry->https_sni, sni1) == 0) {
            TEST_ASSERT_EQUAL_INT(FSM_ALLOW, entry->header->action);
            TEST_ASSERT_EQUAL_INT(123, entry->header->category_id);
            // TEST_ASSERT_EQUAL_INT(90, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(1001, entry->header->flow_marker);
        }
        else if (strcmp(entry->https_sni, sni2) == 0) {
            TEST_ASSERT_EQUAL_INT(FSM_BLOCK, entry->header->action);
            TEST_ASSERT_EQUAL_INT(456, entry->header->category_id);
            // TEST_ASSERT_EQUAL_INT(95, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(1002, entry->header->flow_marker);
        }
        else {
            /* Unexpected SNI name */
            TEST_FAIL_MESSAGE("Unexpected SNI name in parsed result");
        }
    }
    
    /* Verify we found both SNI entries */
    TEST_ASSERT_EQUAL_INT(2, sni_entries_found);
    
    /* Clean up */
    gk_clear_bulk_responses(&reply);
    
    /* Clean up protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_https_sni[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_https_sni[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_https_sni[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_https_sni[0]->https_sni);
    FREE(gk_pb_reply.bulk_reply->reply_https_sni[0]);
    
    FREE(gk_pb_reply.bulk_reply->reply_https_sni[1]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_https_sni[1]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_https_sni[1]->header);
    FREE(gk_pb_reply.bulk_reply->reply_https_sni[1]->https_sni);
    FREE(gk_pb_reply.bulk_reply->reply_https_sni[1]);
    
    FREE(gk_pb_reply.bulk_reply->reply_https_sni);
    FREE(gk_pb_reply.bulk_reply);
}

/**
 * @brief Test populating and parsing HTTP Host entries in a bulk reply
 * 
 * This test validates that HTTP Host entries can be correctly populated 
 * in a bulk reply and then parsed by gk_parse_reply.
 */
void test_populate_reply_host_reply(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply;
    struct gk_reply reply;
    uint8_t device_id_data[6];
    char *host1 = "host1.example.com";
    char *host2 = "host2.test.org";
    
    /* Initialize the reply structure */
    memset(&gk_pb_reply, 0, sizeof(gk_pb_reply));
    
    /* Create bulk reply structure */
    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);
    
    /* Prepare the HTTP Host entries */
    gk_pb_reply.bulk_reply->n_reply_http_host = 2;
    gk_pb_reply.bulk_reply->reply_http_host = 
        CALLOC(gk_pb_reply.bulk_reply->n_reply_http_host, 
               sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpHostReply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_host);
    
    /* Create first HTTP Host entry */
    gk_pb_reply.bulk_reply->reply_http_host[0] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpHostReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_host[0]);
    
    gk_pb_reply.bulk_reply->reply_http_host[0]->http_host = STRDUP(host1);
    gk_pb_reply.bulk_reply->reply_http_host[0]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_host[0]->header);
    
    /* Set header fields for first HTTP Host */
    gk_pb_reply.bulk_reply->reply_http_host[0]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT;
    gk_pb_reply.bulk_reply->reply_http_host[0]->header->category_id = 123;
    gk_pb_reply.bulk_reply->reply_http_host[0]->header->confidence_level = 90;
    gk_pb_reply.bulk_reply->reply_http_host[0]->header->ttl = 3600;
    gk_pb_reply.bulk_reply->reply_http_host[0]->header->flow_marker = 2001;
    gk_pb_reply.bulk_reply->reply_http_host[0]->header->policy = STRDUP("host_allow_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_http_host[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Create second HTTP Host entry */
    gk_pb_reply.bulk_reply->reply_http_host[1] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpHostReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_host[1]);
    
    gk_pb_reply.bulk_reply->reply_http_host[1]->http_host = STRDUP(host2);
    gk_pb_reply.bulk_reply->reply_http_host[1]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_host[1]->header);
    
    /* Set header fields for second HTTP Host */
    gk_pb_reply.bulk_reply->reply_http_host[1]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_BLOCK;
    gk_pb_reply.bulk_reply->reply_http_host[1]->header->category_id = 456;
    gk_pb_reply.bulk_reply->reply_http_host[1]->header->confidence_level = 95;
    gk_pb_reply.bulk_reply->reply_http_host[1]->header->ttl = 7200;
    gk_pb_reply.bulk_reply->reply_http_host[1]->header->flow_marker = 2002;
    gk_pb_reply.bulk_reply->reply_http_host[1]->header->policy = STRDUP("host_block_policy");
    
    memcpy(device_id_data, g_test_mac2.addr, sizeof(g_test_mac2.addr));
    gk_pb_reply.bulk_reply->reply_http_host[1]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Set up the reply to parse with FSM_BULK_REQ type */
    reply.type = FSM_BULK_REQ;
    
    /* Parse the reply */
    bool result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);
    
    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(2, bulk_reply->n_devices);
    
    /* Check that entries are created as expected - should be handled using a lookup by type
       since gk_parse_bulk_reply doesn't guarantee any specific order */
    int host_entries_found = 0;
    
    for (size_t i = 0; i < bulk_reply->n_devices; i++) {
        struct gk_device2app_repl *entry = bulk_reply->devices[i];
        TEST_ASSERT_NOT_NULL(entry);
        
        /* Skip non-HOST entries */
        if (entry->type != GK_ENTRY_TYPE_HOST) continue;
        
        host_entries_found++;
        
        /* Check if this is the first or second HTTP Host entry based on the name */
        if (strcmp(entry->http_host, host1) == 0) {
            TEST_ASSERT_EQUAL_INT(FSM_ALLOW, entry->header->action);
            TEST_ASSERT_EQUAL_INT(123, entry->header->category_id);
            // TEST_ASSERT_EQUAL_INT(90, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(2001, entry->header->flow_marker);
        }
        else if (strcmp(entry->http_host, host2) == 0) {
            TEST_ASSERT_EQUAL_INT(FSM_BLOCK, entry->header->action);
            TEST_ASSERT_EQUAL_INT(456, entry->header->category_id);
            // TEST_ASSERT_EQUAL_INT(95, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(2002, entry->header->flow_marker);
        }
        else {
            /* Unexpected HTTP Host name */
            TEST_FAIL_MESSAGE("Unexpected HTTP Host name in parsed result");
        }
    }
    
    /* Verify we found both HTTP Host entries */
    TEST_ASSERT_EQUAL_INT(2, host_entries_found);
    
    /* Clean up */
    gk_clear_bulk_responses(&reply);
    
    /* Clean up protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_http_host[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_http_host[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_http_host[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_http_host[0]->http_host);
    FREE(gk_pb_reply.bulk_reply->reply_http_host[0]);
    
    FREE(gk_pb_reply.bulk_reply->reply_http_host[1]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_http_host[1]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_http_host[1]->header);
    FREE(gk_pb_reply.bulk_reply->reply_http_host[1]->http_host);
    FREE(gk_pb_reply.bulk_reply->reply_http_host[1]);
    
    FREE(gk_pb_reply.bulk_reply->reply_http_host);
    FREE(gk_pb_reply.bulk_reply);
}

/**
 * @brief Test populating and parsing FQDN entries in a bulk reply
 * 
 * This test validates that FQDN entries can be correctly populated 
 * in a bulk reply and then parsed by gk_parse_reply.
 */
void test_populate_bulk_fqdn_reply(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply;
    struct gk_reply reply;
    uint8_t device_id_data[6];
    char *fqdn1 = "example.com";
    char *fqdn2 = "test.org";
    
    /* Initialize the reply structure */
    memset(&gk_pb_reply, 0, sizeof(gk_pb_reply));
    
    /* Create bulk reply structure */
    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);
    
    /* Prepare the FQDN entries */
    gk_pb_reply.bulk_reply->n_reply_fqdn = 2;
    gk_pb_reply.bulk_reply->reply_fqdn = 
        CALLOC(gk_pb_reply.bulk_reply->n_reply_fqdn, 
               sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnReply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn);
    
    /* Create first FQDN entry */
    gk_pb_reply.bulk_reply->reply_fqdn[0] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[0]);
    
    gk_pb_reply.bulk_reply->reply_fqdn[0]->query_name = STRDUP(fqdn1);
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[0]->header);
    
    /* Set header fields for first FQDN */
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->category_id = 123;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->confidence_level = 90;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->ttl = 3600;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->flow_marker = 3001;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->policy = STRDUP("fqdn_allow_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));

    /* Create FQDN redirect for first entry */
    gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnRedirectReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect);
    
    gatekeeper__southbound__v1__gatekeeper_fqdn_redirect_reply__init(
        gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect);
        
    gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect->redirect_cname = STRDUP("redirect.example.com");
    
    /* Set redirect IPv4 address */
    struct in_addr ipv4_addr;
    inet_pton(AF_INET, "192.168.1.1", &ipv4_addr);
    gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect->redirect_ipv4 = ipv4_addr.s_addr;
    
    /* Create second FQDN entry */
    gk_pb_reply.bulk_reply->reply_fqdn[1] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[1]);
    
    gk_pb_reply.bulk_reply->reply_fqdn[1]->query_name = STRDUP(fqdn2);
    gk_pb_reply.bulk_reply->reply_fqdn[1]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[1]->header);
    
    /* Set header fields for second FQDN */
    gk_pb_reply.bulk_reply->reply_fqdn[1]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_BLOCK;
    gk_pb_reply.bulk_reply->reply_fqdn[1]->header->category_id = 456;
    gk_pb_reply.bulk_reply->reply_fqdn[1]->header->confidence_level = 95;
    gk_pb_reply.bulk_reply->reply_fqdn[1]->header->ttl = 7200;
    gk_pb_reply.bulk_reply->reply_fqdn[1]->header->flow_marker = 3002;
    gk_pb_reply.bulk_reply->reply_fqdn[1]->header->policy = STRDUP("fqdn_block_policy");
    
    memcpy(device_id_data, g_test_mac2.addr, sizeof(g_test_mac2.addr));
    gk_pb_reply.bulk_reply->reply_fqdn[1]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Set up the reply to parse with FSM_BULK_REQ type */
    reply.type = FSM_BULK_REQ;
    
    /* Parse the reply */
    bool result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);
    
    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(2, bulk_reply->n_devices);
    
    /* Check that entries are created as expected - should be handled using a lookup by type 
       since gk_parse_bulk_reply doesn't guarantee any specific order */
    int fqdn_entries_found = 0;
    
    for (size_t i = 0; i < bulk_reply->n_devices; i++) {
        struct gk_device2app_repl *entry = bulk_reply->devices[i];
        TEST_ASSERT_NOT_NULL(entry);
        
        /* Skip non-FQDN entries */
        if (entry->type != GK_ENTRY_TYPE_FQDN) continue;
        
        fqdn_entries_found++;
        
        /* Check if this is the first or second FQDN entry based on the name */
        if (strcmp(entry->fqdn, fqdn1) == 0) {
            TEST_ASSERT_EQUAL_INT(FSM_ALLOW, entry->header->action);
            TEST_ASSERT_EQUAL_INT(123, entry->header->category_id);
            // TEST_ASSERT_EQUAL_INT(90, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(3001, entry->header->flow_marker);
            
            /* Check redirect info */
            TEST_ASSERT_NOT_NULL(entry->fqdn_redirect);
            
            /* The A- prefix is added by the parsing function */
            char expected_redirect[256];
            snprintf(expected_redirect, sizeof(expected_redirect), "A-192.168.1.1");
            TEST_ASSERT_EQUAL_STRING(expected_redirect, entry->fqdn_redirect->redirect_ips[0]);
        }
        else if (strcmp(entry->fqdn, fqdn2) == 0) {
            TEST_ASSERT_EQUAL_INT(FSM_BLOCK, entry->header->action);
            TEST_ASSERT_EQUAL_INT(456, entry->header->category_id);
            // TEST_ASSERT_EQUAL_INT(95, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(3002, entry->header->flow_marker);
        }
        else {
            /* Unexpected FQDN name */
            TEST_FAIL_MESSAGE("Unexpected FQDN name in parsed result");
        }
    }
    
    /* Verify we found both FQDN entries */
    TEST_ASSERT_EQUAL_INT(2, fqdn_entries_found);
    
    /* Clean up */
    gk_clear_bulk_responses(&reply);
    
    /* Clean up protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect->redirect_cname);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->query_name);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]);
    
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[1]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[1]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[1]->header);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[1]->query_name);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[1]);
    
    FREE(gk_pb_reply.bulk_reply->reply_fqdn);
    FREE(gk_pb_reply.bulk_reply);
}

/**
 * @brief Test populating and parsing IPv4 entries in a bulk reply
 * 
 * This test validates that IPv4 entries can be correctly populated 
 * in a bulk reply and then parsed by gk_parse_reply.
 */
void test_populate_bulk_ipv4_reply(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply;
    struct gk_reply reply;
    uint8_t device_id_data[6];
    struct in_addr ipv4_addr1, ipv4_addr2;
    char ipv4_str1[INET_ADDRSTRLEN];
    char ipv4_str2[INET_ADDRSTRLEN];
    
    /* Initialize the IPv4 addresses */
    inet_pton(AF_INET, "192.168.1.10", &ipv4_addr1);
    inet_pton(AF_INET, "10.0.0.1", &ipv4_addr2);
    
    /* Save string representations for test verification */
    inet_ntop(AF_INET, &ipv4_addr1, ipv4_str1, sizeof(ipv4_str1));
    inet_ntop(AF_INET, &ipv4_addr2, ipv4_str2, sizeof(ipv4_str2));
    
    /* Initialize the reply structure */
    memset(&gk_pb_reply, 0, sizeof(gk_pb_reply));
    
    /* Create bulk reply structure */
    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);
    
    /* Prepare the IPv4 entries */
    gk_pb_reply.bulk_reply->n_reply_ipv4 = 2;
    gk_pb_reply.bulk_reply->reply_ipv4 = 
        CALLOC(gk_pb_reply.bulk_reply->n_reply_ipv4, 
               sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv4Reply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv4);
    
    /* Create first IPv4 entry */
    gk_pb_reply.bulk_reply->reply_ipv4[0] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv4Reply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv4[0]);
    
    gk_pb_reply.bulk_reply->reply_ipv4[0]->addr_ipv4 = ipv4_addr1.s_addr;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv4[0]->header);
    
    /* Set header fields for first IPv4 */
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->category_id = 111;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->confidence_level = 85;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->ttl = 1800;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->flow_marker = 4001;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->policy = STRDUP("ipv4_allow_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Create second IPv4 entry */
    gk_pb_reply.bulk_reply->reply_ipv4[1] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv4Reply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv4[1]);
    
    gk_pb_reply.bulk_reply->reply_ipv4[1]->addr_ipv4 = ipv4_addr2.s_addr;
    gk_pb_reply.bulk_reply->reply_ipv4[1]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv4[1]->header);
    
    /* Set header fields for second IPv4 */
    gk_pb_reply.bulk_reply->reply_ipv4[1]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_BLOCK;
    gk_pb_reply.bulk_reply->reply_ipv4[1]->header->category_id = 222;
    gk_pb_reply.bulk_reply->reply_ipv4[1]->header->confidence_level = 98;
    gk_pb_reply.bulk_reply->reply_ipv4[1]->header->ttl = 3600;
    gk_pb_reply.bulk_reply->reply_ipv4[1]->header->flow_marker = 4002;
    gk_pb_reply.bulk_reply->reply_ipv4[1]->header->policy = STRDUP("ipv4_block_policy");
    
    memcpy(device_id_data, g_test_mac2.addr, sizeof(g_test_mac2.addr));
    gk_pb_reply.bulk_reply->reply_ipv4[1]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Set up the reply to parse with FSM_BULK_REQ type */
    reply.type = FSM_BULK_REQ;
    
    /* Parse the reply */
    bool result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);
    
    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(2, bulk_reply->n_devices);
    
    /* Check that entries are created as expected */
    int ipv4_entries_found = 0;
    
    for (size_t i = 0; i < bulk_reply->n_devices; i++) {
        struct gk_device2app_repl *entry = bulk_reply->devices[i];
        TEST_ASSERT_NOT_NULL(entry);
        
        /* Skip non-IPv4 entries */
        if (entry->type != GK_ENTRY_TYPE_IPV4) continue;
        
        ipv4_entries_found++;
        
        /* Check IPv4 address and other fields */
        struct in_addr addr;
        addr.s_addr = entry->ipv4_addr;
        char curr_addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, curr_addr, sizeof(curr_addr));
        
        if (addr.s_addr == ipv4_addr1.s_addr) {
            TEST_ASSERT_EQUAL_STRING(ipv4_str1, curr_addr);
            TEST_ASSERT_EQUAL_INT(FSM_ALLOW, entry->header->action);
            TEST_ASSERT_EQUAL_INT(111, entry->header->category_id);
            // TEST_ASSERT_EQUAL_INT(85, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(4001, entry->header->flow_marker);
        }
        else if (addr.s_addr == ipv4_addr2.s_addr) {
            TEST_ASSERT_EQUAL_STRING(ipv4_str2, curr_addr);
            TEST_ASSERT_EQUAL_INT(FSM_BLOCK, entry->header->action);
            TEST_ASSERT_EQUAL_INT(222, entry->header->category_id);
            // TEST_ASSERT_EQUAL_INT(98, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(4002, entry->header->flow_marker);
        }
        else {
            /* Unexpected IPv4 address */
            TEST_FAIL_MESSAGE("Unexpected IPv4 address in parsed result");
        }
    }
    
    /* Verify we found both IPv4 entries */
    TEST_ASSERT_EQUAL_INT(2, ipv4_entries_found);
    
    /* Clean up */
    gk_clear_bulk_responses(&reply);
    
    /* Clean up protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[0]);
    
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[1]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[1]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[1]->header);
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[1]);
    
    FREE(gk_pb_reply.bulk_reply->reply_ipv4);
    FREE(gk_pb_reply.bulk_reply);
}

/**
 * @brief Test populating and parsing IPv6 entries in a bulk reply
 * 
 * This test validates that IPv6 entries can be correctly populated 
 * in a bulk reply and then parsed by gk_parse_reply.
 */
void test_populate_bulk_ipv6_reply(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply;
    struct gk_reply reply;
    uint8_t device_id_data[6];
    struct in6_addr ipv6_addr1, ipv6_addr2;
    char ipv6_str1[INET6_ADDRSTRLEN];
    char ipv6_str2[INET6_ADDRSTRLEN];
    
    /* Initialize the IPv6 addresses */
    inet_pton(AF_INET6, "2001:db8::1", &ipv6_addr1);
    inet_pton(AF_INET6, "fe80::1234:5678:9abc:def0", &ipv6_addr2);
    
    /* Save string representations for test verification */
    inet_ntop(AF_INET6, &ipv6_addr1, ipv6_str1, sizeof(ipv6_str1));
    inet_ntop(AF_INET6, &ipv6_addr2, ipv6_str2, sizeof(ipv6_str2));
    
    /* Initialize the reply structure */
    memset(&gk_pb_reply, 0, sizeof(gk_pb_reply));
    
    /* Create bulk reply structure */
    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);
    
    /* Prepare the IPv6 entries */
    gk_pb_reply.bulk_reply->n_reply_ipv6 = 2;
    gk_pb_reply.bulk_reply->reply_ipv6 = 
        CALLOC(gk_pb_reply.bulk_reply->n_reply_ipv6, 
               sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv6Reply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv6);
    
    /* Create first IPv6 entry */
    gk_pb_reply.bulk_reply->reply_ipv6[0] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv6Reply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv6[0]);
    
    /* Set IPv6 address data */
    gk_pb_reply.bulk_reply->reply_ipv6[0]->addr_ipv6.len = sizeof(ipv6_addr1);
    gk_pb_reply.bulk_reply->reply_ipv6[0]->addr_ipv6.data = MALLOC(sizeof(ipv6_addr1));
    memcpy(gk_pb_reply.bulk_reply->reply_ipv6[0]->addr_ipv6.data, &ipv6_addr1, sizeof(ipv6_addr1));
    
    /* Create and set header fields for first IPv6 */
    gk_pb_reply.bulk_reply->reply_ipv6[0]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv6[0]->header);
    
    gk_pb_reply.bulk_reply->reply_ipv6[0]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT;
    gk_pb_reply.bulk_reply->reply_ipv6[0]->header->category_id = 11;
    gk_pb_reply.bulk_reply->reply_ipv6[0]->header->confidence_level = 75;
    gk_pb_reply.bulk_reply->reply_ipv6[0]->header->ttl = 1800;
    gk_pb_reply.bulk_reply->reply_ipv6[0]->header->flow_marker = 5001;
    gk_pb_reply.bulk_reply->reply_ipv6[0]->header->policy = STRDUP("ipv6_allow_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_ipv6[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Create second IPv6 entry */
    gk_pb_reply.bulk_reply->reply_ipv6[1] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv6Reply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv6[1]);
    
    /* Set IPv6 address data */
    gk_pb_reply.bulk_reply->reply_ipv6[1]->addr_ipv6.len = sizeof(ipv6_addr2);
    gk_pb_reply.bulk_reply->reply_ipv6[1]->addr_ipv6.data = MALLOC(sizeof(ipv6_addr2));
    memcpy(gk_pb_reply.bulk_reply->reply_ipv6[1]->addr_ipv6.data, &ipv6_addr2, sizeof(ipv6_addr2));
    
    /* Create and set header fields for second IPv6 */
    gk_pb_reply.bulk_reply->reply_ipv6[1]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv6[1]->header);
    
    gk_pb_reply.bulk_reply->reply_ipv6[1]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_BLOCK;
    gk_pb_reply.bulk_reply->reply_ipv6[1]->header->category_id = 22;
    gk_pb_reply.bulk_reply->reply_ipv6[1]->header->confidence_level = 88;
    gk_pb_reply.bulk_reply->reply_ipv6[1]->header->ttl = 3600;
    gk_pb_reply.bulk_reply->reply_ipv6[1]->header->flow_marker = 5002;
    gk_pb_reply.bulk_reply->reply_ipv6[1]->header->policy = STRDUP("ipv6_block_policy");
    
    memcpy(device_id_data, g_test_mac2.addr, sizeof(g_test_mac2.addr));
    gk_pb_reply.bulk_reply->reply_ipv6[1]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Set up the reply to parse with FSM_BULK_REQ type */
    reply.type = FSM_BULK_REQ;
    
    /* Parse the reply */
    bool result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);
    
    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(2, bulk_reply->n_devices);
    
    /* Check that entries are created as expected */
    int ipv6_entries_found = 0;
    
    for (size_t i = 0; i < bulk_reply->n_devices; i++) {
        struct gk_device2app_repl *entry = bulk_reply->devices[i];
        TEST_ASSERT_NOT_NULL(entry);
        
        /* Skip non-IPv6 entries */
        if (entry->type != GK_ENTRY_TYPE_IPV6) continue;
        
        ipv6_entries_found++;
        
        /* Check that IPv6 data exists */
        TEST_ASSERT_NOT_NULL(entry->ipv6_addr.data);
        TEST_ASSERT_EQUAL_INT(sizeof(struct in6_addr), entry->ipv6_addr.len);
        
        /* Convert entry IPv6 to string for comparison */
        char curr_addr[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, entry->ipv6_addr.data, curr_addr, sizeof(curr_addr));
        
        /* Check which IPv6 entry this is */
        if (strcmp(curr_addr, ipv6_str1) == 0) {
            TEST_ASSERT_EQUAL_INT(FSM_ALLOW, entry->header->action);
            TEST_ASSERT_EQUAL_INT(11, entry->header->category_id);
            // TEST_ASSERT_EQUAL_INT(75, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(5001, entry->header->flow_marker);
        }
        else if (strcmp(curr_addr, ipv6_str2) == 0) {
            TEST_ASSERT_EQUAL_INT(FSM_BLOCK, entry->header->action);
            TEST_ASSERT_EQUAL_INT(22, entry->header->category_id);
            // TEST_ASSERT_EQUAL_INT(88, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(5002, entry->header->flow_marker);
        }
        else {
            /* Unexpected IPv6 address */
            TEST_FAIL_MESSAGE("Unexpected IPv6 address in parsed result");
        }
    }
    
    /* Verify we found both IPv6 entries */
    TEST_ASSERT_EQUAL_INT(2, ipv6_entries_found);
    
    /* Clean up */
    gk_clear_bulk_responses(&reply);
    
    /* Clean up protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_ipv6[0]->addr_ipv6.data);
    FREE(gk_pb_reply.bulk_reply->reply_ipv6[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_ipv6[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_ipv6[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_ipv6[0]);
    
    FREE(gk_pb_reply.bulk_reply->reply_ipv6[1]->addr_ipv6.data);
    FREE(gk_pb_reply.bulk_reply->reply_ipv6[1]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_ipv6[1]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_ipv6[1]->header);
    FREE(gk_pb_reply.bulk_reply->reply_ipv6[1]);
    
    FREE(gk_pb_reply.bulk_reply->reply_ipv6);
    FREE(gk_pb_reply.bulk_reply);
}


void
test_populate_bulk_app_reply(void)
{
    struct gk_reply reply;
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply = GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_REPLY__INIT;
    uint8_t device_id_data[6];
    bool result;
    
    /* Initialize the reply structure */
    memset(&reply, 0, sizeof(reply));
    
    /* Create the bulk reply */
    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);
    
    /* Set up empty arrays */
    gk_pb_reply.bulk_reply->n_reply_app = 2;
    gk_pb_reply.bulk_reply->reply_app = CALLOC(gk_pb_reply.bulk_reply->n_reply_app, 
                                              sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_app);
    
    /* Create first App entry */
    gk_pb_reply.bulk_reply->reply_app[0] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_app[0]);
    
    /* Set App name data */
    gk_pb_reply.bulk_reply->reply_app[0]->app_name = STRDUP("Facebook");

    /* Create and set header fields for first App */
    gk_pb_reply.bulk_reply->reply_app[0]->header =
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_app[0]->header);
    
    gk_pb_reply.bulk_reply->reply_app[0]->header->action =
    GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT;
    gk_pb_reply.bulk_reply->reply_app[0]->header->category_id = 10;
    gk_pb_reply.bulk_reply->reply_app[0]->header->confidence_level = 90;
    gk_pb_reply.bulk_reply->reply_app[0]->header->ttl = 3600;
    gk_pb_reply.bulk_reply->reply_app[0]->header->flow_marker = 7001;
    gk_pb_reply.bulk_reply->reply_app[0]->header->policy = STRDUP("app_allow_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_app[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Create second App entry */
    gk_pb_reply.bulk_reply->reply_app[1] = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_app[1]);
    
    /* Set App name data */
    gk_pb_reply.bulk_reply->reply_app[1]->app_name = STRDUP("YouTube");
    
    /* Create and set header fields for second App */
    gk_pb_reply.bulk_reply->reply_app[1]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_app[1]->header);
    
    gk_pb_reply.bulk_reply->reply_app[1]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_BLOCK;
    gk_pb_reply.bulk_reply->reply_app[1]->header->category_id = 20;
    gk_pb_reply.bulk_reply->reply_app[1]->header->confidence_level = 85;
    gk_pb_reply.bulk_reply->reply_app[1]->header->ttl = 7200;
    gk_pb_reply.bulk_reply->reply_app[1]->header->flow_marker = 7002;
    gk_pb_reply.bulk_reply->reply_app[1]->header->policy = STRDUP("app_block_policy");
    
    memcpy(device_id_data, g_test_mac2.addr, sizeof(g_test_mac2.addr));
    gk_pb_reply.bulk_reply->reply_app[1]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Set up the reply to parse with FSM_BULK_REQ type */
    reply.type = FSM_BULK_REQ;
    
    /* Parse the reply */
    result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);
    
    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(2, bulk_reply->n_devices);
    
    /* Check that entries are created as expected */
    int app_entries_found = 0;
    
    for (size_t i = 0; i < bulk_reply->n_devices; i++) {
        struct gk_device2app_repl *entry = bulk_reply->devices[i];
        TEST_ASSERT_NOT_NULL(entry);
        
        /* Skip non-App entries */
        if (entry->type != GK_ENTRY_TYPE_APP) continue;
        
        app_entries_found++;
        
        /* Check that App name exists */
        TEST_ASSERT_NOT_NULL(entry->app_name);
        
        /* Check which App entry this is */
        if (strcmp(entry->app_name, "Facebook") == 0) {
            TEST_ASSERT_EQUAL_INT(FSM_ALLOW, entry->header->action);
            TEST_ASSERT_EQUAL_INT(10, entry->header->category_id);
            TEST_ASSERT_EQUAL_INT(90, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(7001, entry->header->flow_marker);
            TEST_ASSERT_EQUAL_INT(3600, entry->header->ttl);
            TEST_ASSERT_NOT_NULL(entry->header->policy);
            TEST_ASSERT_EQUAL_STRING("app_allow_policy", entry->header->policy);
        }
        else if (strcmp(entry->app_name, "YouTube") == 0) {
            TEST_ASSERT_EQUAL_INT(FSM_BLOCK, entry->header->action);
            TEST_ASSERT_EQUAL_INT(20, entry->header->category_id);
            TEST_ASSERT_EQUAL_INT(85, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(7002, entry->header->flow_marker);
            TEST_ASSERT_EQUAL_INT(7200, entry->header->ttl);
            TEST_ASSERT_NOT_NULL(entry->header->policy);
            TEST_ASSERT_EQUAL_STRING("app_block_policy", entry->header->policy);
        }
        else {
            /* Unexpected app name */
            TEST_FAIL_MESSAGE("Unexpected app name in parsed result");
        }
    }
    
    /* Verify we found both App entries */
    TEST_ASSERT_EQUAL_INT(2, app_entries_found);
    
    /* Clean up */
    gk_clear_bulk_responses(&reply);
    
    /* Clean up protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_app[0]->app_name);
    FREE(gk_pb_reply.bulk_reply->reply_app[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_app[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_app[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_app[0]);
    
    FREE(gk_pb_reply.bulk_reply->reply_app[1]->app_name);
    FREE(gk_pb_reply.bulk_reply->reply_app[1]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_app[1]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_app[1]->header);
    FREE(gk_pb_reply.bulk_reply->reply_app[1]);
    
    FREE(gk_pb_reply.bulk_reply->reply_app);
    FREE(gk_pb_reply.bulk_reply);
}

void
test_populate_empty_bulk_reply(void)
{
    struct gk_reply reply;
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply = GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_REPLY__INIT;
    bool result;

    /* Initialize the reply structure */
    memset(&reply, 0, sizeof(reply));

    /* Create the bulk reply with no entries */
    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);

    /* Set up empty arrays - explicitly set n_ fields to 0 */
    gk_pb_reply.bulk_reply->n_reply_http_url = 0;
    gk_pb_reply.bulk_reply->n_reply_app = 0;
    gk_pb_reply.bulk_reply->n_reply_fqdn = 0;
    gk_pb_reply.bulk_reply->n_reply_ipv4 = 0;
    gk_pb_reply.bulk_reply->n_reply_ipv6 = 0;
    gk_pb_reply.bulk_reply->n_reply_http_host = 0;
    gk_pb_reply.bulk_reply->n_reply_https_sni = 0;

    /* Set up the reply to parse with FSM_BULK_REQ type */
    reply.type = FSM_BULK_REQ;
    
    /* Parse the reply */
    result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);

    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(0, bulk_reply->n_devices);

    /* Clean up */
    gk_clear_bulk_responses(&reply);
    FREE(gk_pb_reply.bulk_reply);
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
test_populate_mixed_bulk_reply(void)
{
    struct gk_reply reply;
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply = GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_REPLY__INIT;
    uint8_t device_id_data[6];
    uint32_t ipv4_addr = 0x01020304; /* 1.2.3.4 */
    bool result;
    
    /* Initialize the reply structure */
    memset(&reply, 0, sizeof(reply));
    
    /* Create the bulk reply */
    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);

    /* Create one URL entry */
    gk_pb_reply.bulk_reply->n_reply_http_url = 1;
    gk_pb_reply.bulk_reply->reply_http_url = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpUrlReply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_url);
    
    gk_pb_reply.bulk_reply->reply_http_url[0] = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpUrlReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_url[0]);
    
    gk_pb_reply.bulk_reply->reply_http_url[0]->http_url = STRDUP("https://www.example.com/page");
    
    gk_pb_reply.bulk_reply->reply_http_url[0]->header =
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_http_url[0]->header);

    gk_pb_reply.bulk_reply->reply_http_url[0]->header->action =
    GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT;
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->category_id = 30;
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->confidence_level = 95;
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->ttl = 3600;
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->flow_marker = 8001;
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->policy = STRDUP("url_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_http_url[0]->header->device_id =
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Create one App entry */
    gk_pb_reply.bulk_reply->n_reply_app = 1;
    gk_pb_reply.bulk_reply->reply_app = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_app);
    
    gk_pb_reply.bulk_reply->reply_app[0] = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperAppReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_app[0]);
    
    gk_pb_reply.bulk_reply->reply_app[0]->app_name = STRDUP("Twitter");
    
    gk_pb_reply.bulk_reply->reply_app[0]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_app[0]->header);
    
    gk_pb_reply.bulk_reply->reply_app[0]->header->action = 
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_BLOCK;
    gk_pb_reply.bulk_reply->reply_app[0]->header->category_id = 40;
    gk_pb_reply.bulk_reply->reply_app[0]->header->confidence_level = 80;
    gk_pb_reply.bulk_reply->reply_app[0]->header->ttl = 7200;
    gk_pb_reply.bulk_reply->reply_app[0]->header->flow_marker = 8002;
    gk_pb_reply.bulk_reply->reply_app[0]->header->policy = STRDUP("app_policy");
    
    memcpy(device_id_data, g_test_mac2.addr, sizeof(g_test_mac2.addr));
    gk_pb_reply.bulk_reply->reply_app[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Create one IPv4 entry */
    gk_pb_reply.bulk_reply->n_reply_ipv4 = 1;
    gk_pb_reply.bulk_reply->reply_ipv4 = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv4Reply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv4);
    
    gk_pb_reply.bulk_reply->reply_ipv4[0] = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv4Reply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv4[0]);
    
    gk_pb_reply.bulk_reply->reply_ipv4[0]->addr_ipv4 = ipv4_addr;
    
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_ipv4[0]->header);
    
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->action = 
    GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_ACCEPT;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->category_id = 50;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->confidence_level = 70;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->ttl = 3600;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->flow_marker = 8003;
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->policy = STRDUP("ipv4_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_ipv4[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Set up the reply to parse with FSM_BULK_REQ type */
    reply.type = FSM_BULK_REQ;
    
    /* Parse the reply */
    result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);
    
    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(3, bulk_reply->n_devices);
    
    /* Check that all entries are created as expected */
    int url_entries_found = 0;
    int app_entries_found = 0;
    int ipv4_entries_found = 0;
    
    for (size_t i = 0; i < bulk_reply->n_devices; i++) {
        struct gk_device2app_repl *entry = bulk_reply->devices[i];
        TEST_ASSERT_NOT_NULL(entry);
        
        if (entry->type == GK_ENTRY_TYPE_URL) {
            url_entries_found++;
            TEST_ASSERT_NOT_NULL(entry->url);
            TEST_ASSERT_EQUAL_STRING("https://www.example.com/page", entry->url);
            TEST_ASSERT_EQUAL_INT(FSM_ALLOW, entry->header->action);
            TEST_ASSERT_EQUAL_INT(30, entry->header->category_id);
            TEST_ASSERT_EQUAL_INT(95, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(8001, entry->header->flow_marker);
        }
        else if (entry->type == GK_ENTRY_TYPE_APP) {
            app_entries_found++;
            TEST_ASSERT_NOT_NULL(entry->app_name);
            TEST_ASSERT_EQUAL_STRING("Twitter", entry->app_name);
            TEST_ASSERT_EQUAL_INT(FSM_BLOCK, entry->header->action);
            TEST_ASSERT_EQUAL_INT(40, entry->header->category_id);
            TEST_ASSERT_EQUAL_INT(80, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(8002, entry->header->flow_marker);
        }
        else if (entry->type == GK_ENTRY_TYPE_IPV4) {
            ipv4_entries_found++;
            TEST_ASSERT_EQUAL_UINT32(ipv4_addr, entry->ipv4_addr);
            TEST_ASSERT_EQUAL_INT(FSM_ALLOW, entry->header->action);
            TEST_ASSERT_EQUAL_INT(50, entry->header->category_id);
            TEST_ASSERT_EQUAL_INT(70, entry->header->confidence_level);
            TEST_ASSERT_EQUAL_INT(8003, entry->header->flow_marker);
        }
    }
    
    /* Verify we found all entries */
    TEST_ASSERT_EQUAL_INT(1, url_entries_found);
    TEST_ASSERT_EQUAL_INT(1, app_entries_found);
    TEST_ASSERT_EQUAL_INT(1, ipv4_entries_found);
    
    /* Clean up */
    gk_clear_bulk_responses(&reply);
    
    /* Clean up URL protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_http_url[0]->http_url);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_http_url[0]);
    FREE(gk_pb_reply.bulk_reply->reply_http_url);
    
    /* Clean up App protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_app[0]->app_name);
    FREE(gk_pb_reply.bulk_reply->reply_app[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_app[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_app[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_app[0]);
    FREE(gk_pb_reply.bulk_reply->reply_app);
    
    /* Clean up IPv4 protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_ipv4[0]);
    FREE(gk_pb_reply.bulk_reply->reply_ipv4);
    
    FREE(gk_pb_reply.bulk_reply);
}


/**
 * It creates a reply with both IPv4 and IPv6
 * redirect addresses and ensures both are parsed.
 */
void test_fqdn_redirect_with_ipv6(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply;
    struct gk_reply reply;
    uint8_t device_id_data[6];
    char *fqdn = "dual.stack.example.com";
    struct in_addr ipv4_addr;
    struct in6_addr ipv6_addr;
    uint8_t ipv6_bytes[16];
    
    memset(&gk_pb_reply, 0, sizeof(gk_pb_reply));

    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);

    gk_pb_reply.bulk_reply->n_reply_fqdn = 1;
    gk_pb_reply.bulk_reply->reply_fqdn =
        CALLOC(gk_pb_reply.bulk_reply->n_reply_fqdn,
               sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnReply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn);

    /* Create FQDN entry */
    gk_pb_reply.bulk_reply->reply_fqdn[0] =
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[0]);

    gk_pb_reply.bulk_reply->reply_fqdn[0]->query_name = STRDUP(fqdn);
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header =
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[0]->header);

    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->action =
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_REDIRECT;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->category_id = 789;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->confidence_level = 98;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->ttl = 1800;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->flow_marker = 4001;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->policy = STRDUP("dual_stack_redirect_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Create FQDN redirect with both IPv4 and IPv6 addresses */
    gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnRedirectReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect);
    
    gatekeeper__southbound__v1__gatekeeper_fqdn_redirect_reply__init(
        gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect);
        
    inet_pton(AF_INET, "203.0.113.42", &ipv4_addr);
    gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect->redirect_ipv4 = ipv4_addr.s_addr;
    
    inet_pton(AF_INET6, "2001:db8::42", &ipv6_addr);
    memcpy(ipv6_bytes, &ipv6_addr, sizeof(ipv6_bytes));
    gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect->redirect_ipv6 = 
        create_protobuf_c_binary_data(ipv6_bytes, sizeof(ipv6_bytes));
    
    reply.type = FSM_BULK_REQ;

    /* Parse the reply */
    bool result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);
    
    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(1, bulk_reply->n_devices);

    /* Check the FQDN entry */
    struct gk_device2app_repl *entry = bulk_reply->devices[0];
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_INT(GK_ENTRY_TYPE_FQDN, entry->type);
    TEST_ASSERT_EQUAL_STRING(fqdn, entry->fqdn);
    
    TEST_ASSERT_EQUAL_INT(FSM_REDIRECT, entry->header->action);
    TEST_ASSERT_EQUAL_INT(789, entry->header->category_id);
    TEST_ASSERT_EQUAL_INT(4001, entry->header->flow_marker);
    
    /* Check redirect info - should have both IPv4 and IPv6 entries */
    TEST_ASSERT_NOT_NULL(entry->fqdn_redirect);
    TEST_ASSERT_TRUE(entry->fqdn_redirect->redirect);
    
    /* Verify IPv4 redirect - should be in redirect_ips[0] */
    char ipv4_expected[256];
    snprintf(ipv4_expected, sizeof(ipv4_expected), "A-203.0.113.42");
    TEST_ASSERT_EQUAL_STRING(ipv4_expected, entry->fqdn_redirect->redirect_ips[0]);
    
    /* Verify IPv6 redirect - should be in redirect_ips[1] */
    char ipv6_expected[256];
    snprintf(ipv6_expected, sizeof(ipv6_expected), "AAAA-2001:db8::42");
    TEST_ASSERT_EQUAL_STRING(ipv6_expected, entry->fqdn_redirect->redirect_ips[1]);
    
    gk_clear_bulk_responses(&reply);

    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect->redirect_ipv6.data);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->query_name);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn);
    FREE(gk_pb_reply.bulk_reply);
}


void test_fqdn_redirect_ipv6_only(void)
{
    Gatekeeper__Southbound__V1__GatekeeperReply gk_pb_reply;
    struct gk_reply reply;
    uint8_t device_id_data[6];
    char *fqdn = "ipv6.only.example.com";
    struct in6_addr ipv6_addr;
    uint8_t ipv6_bytes[16];
    
    memset(&gk_pb_reply, 0, sizeof(gk_pb_reply));
    
    gk_pb_reply.bulk_reply = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperBulkReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply);
    
    /* Prepare the FQDN entry */
    gk_pb_reply.bulk_reply->n_reply_fqdn = 1;
    gk_pb_reply.bulk_reply->reply_fqdn = 
        CALLOC(gk_pb_reply.bulk_reply->n_reply_fqdn, 
               sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnReply *));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn);
    
    gk_pb_reply.bulk_reply->reply_fqdn[0] =
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[0]);
    
    gk_pb_reply.bulk_reply->reply_fqdn[0]->query_name = STRDUP(fqdn);
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperCommonReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[0]->header);
    
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->action =
        GATEKEEPER__SOUTHBOUND__V1__GATEKEEPER_ACTION__GATEKEEPER_ACTION_REDIRECT;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->category_id = 456;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->confidence_level = 80;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->ttl = 1200;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->flow_marker = 4002;
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->policy = STRDUP("ipv6_only_redirect_policy");
    
    memcpy(device_id_data, g_test_mac.addr, sizeof(g_test_mac.addr));
    gk_pb_reply.bulk_reply->reply_fqdn[0]->header->device_id = 
        create_protobuf_c_binary_data(device_id_data, sizeof(device_id_data));
    
    /* Create FQDN redirect with only IPv6 address */
    gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect = 
        CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnRedirectReply));
    TEST_ASSERT_NOT_NULL(gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect);
    
    gatekeeper__southbound__v1__gatekeeper_fqdn_redirect_reply__init(
        gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect);
        
    /* Set only redirect IPv6 address, no IPv4 */
    inet_pton(AF_INET6, "2001:db8::1:2:3:4", &ipv6_addr);
    memcpy(ipv6_bytes, &ipv6_addr, sizeof(ipv6_bytes));
    gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect->redirect_ipv6 = 
        create_protobuf_c_binary_data(ipv6_bytes, sizeof(ipv6_bytes));
    
    reply.type = FSM_BULK_REQ;
    
    bool result = gk_parse_reply(&reply, &gk_pb_reply);
    TEST_ASSERT_TRUE(result);

    /* Validate the parse results */
    struct gk_bulk_reply *bulk_reply = &reply.data_reply.bulk_reply;
    TEST_ASSERT_EQUAL_INT(1, bulk_reply->n_devices);

    /* Check the FQDN entry */
    struct gk_device2app_repl *entry = bulk_reply->devices[0];
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_INT(GK_ENTRY_TYPE_FQDN, entry->type);
    TEST_ASSERT_EQUAL_STRING(fqdn, entry->fqdn);

    TEST_ASSERT_EQUAL_INT(FSM_REDIRECT, entry->header->action);
    TEST_ASSERT_EQUAL_INT(456, entry->header->category_id);
    TEST_ASSERT_EQUAL_INT(4002, entry->header->flow_marker);

    /* Check redirect info - should have IPv6 entry only */
    TEST_ASSERT_NOT_NULL(entry->fqdn_redirect);
    TEST_ASSERT_TRUE(entry->fqdn_redirect->redirect);

    char ipv6_expected[256];
    snprintf(ipv6_expected, sizeof(ipv6_expected), "AAAA-2001:db8::1:2:3:4");
    TEST_ASSERT_EQUAL_STRING(ipv6_expected, entry->fqdn_redirect->redirect_ips[0]);
    
    /* The second slot should be empty */
    TEST_ASSERT_EQUAL_STRING("", entry->fqdn_redirect->redirect_ips[1]);
    
    /* Clean up */
    gk_clear_bulk_responses(&reply);
    
    /* Clean up protobuf structures */
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect->redirect_ipv6.data);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->redirect);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->header->device_id.data);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->header->policy);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->header);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]->query_name);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn[0]);
    FREE(gk_pb_reply.bulk_reply->reply_fqdn);
    FREE(gk_pb_reply.bulk_reply);
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
    RUN_TEST(test_populate_http_host_bulk_reply);
    RUN_TEST(test_populate_sni_bulk_reply);
    RUN_TEST(test_populate_reply_host_reply);
    RUN_TEST(test_populate_bulk_fqdn_reply);
    RUN_TEST(test_populate_bulk_ipv4_reply);
    RUN_TEST(test_populate_bulk_ipv6_reply);
    RUN_TEST(test_populate_bulk_app_reply);
    RUN_TEST(test_populate_empty_bulk_reply);
    RUN_TEST(test_populate_mixed_bulk_reply);
    RUN_TEST(test_fqdn_redirect_with_ipv6);
    RUN_TEST(test_fqdn_redirect_ipv6_only);

    ut_setUp_tearDown(NULL, NULL, NULL);
}
