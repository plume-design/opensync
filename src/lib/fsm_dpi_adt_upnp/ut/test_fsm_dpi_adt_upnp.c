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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "adt_upnp_curl.h"
#include "adt_upnp_json_report.h"
#include "adv_data_typing.pb-c.h"
#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dpi_adt_upnp.h"
#include "fsm_dpi_client_plugin.h"
#include "json_util.h"
#include "log.h"
#include "memutil.h"
#include "network_metadata_report.h"
#include "os.h"
#include "os_nif.h"
#include "os_types.h"
#include "qm_conn.h"
#include "unity.h"
#include "unity_internals.h"

#define TEST_NETWORK_ID "test_network_id"

static union fsm_plugin_ops g_plugin_ops =
{
    .web_cat_ops =
    {
        .categories_check = NULL,
        .risk_level_check = NULL,
        .cat2str = NULL,
        .get_stats = NULL,
        .dns_response = NULL,
        .gatekeeper_req = NULL,
    },
};

static struct fsm_session_conf g_conf =
{
    .handler = "upnp_test_session_0",
};

static struct fsm_session g_session =
{
    .node_id = "NODE_ID",
    .location_id = "LOCATION_ID",
    .topic = "ADT_TOPIC",
    .name = "ADT_UPNP",
    .type = FSM_WEB_CAT,
    .conf = &g_conf,
};

struct fsm_dpi_adt_upnp_root_desc g_adt_upnp_data =
{
    .url = "http://10.1.0.48:8080/description.xml",
    .dev_type = "ut_upnp_dev_type",
    .friendly_name = "ut_upnp_friendly_name",
    .manufacturer = "ut_upnp_manufacturer",
    .manufacturer_url = "https://ut_upnp.opensync",
    .model_desc = "ut_upnp_model_desc",
    .model_name = "ut_upnp_model_name",
    .model_num = "ut_upnp_model_1",
    .model_url = "https://ut_upnp.opensync/ut_upnp_model_1",
    .serial_num = "ut_upnp_model_1_SN",
    .udn = "ut_upnp_udn",
    .upc = "ut_upnp_upc"
};

static char *
mock_get_config(struct fsm_session *session, char *key)
{
    (void)session;

    return key;
}

void
test_fsm_dpi_adt_upnp_init_exit(void)
{
    struct fsm_dpi_adt_upnp_session *adt_upnp_session;
    struct fsm_dpi_client_session *new_client_session;
    struct fsm_dpi_adt_upnp_report_aggregator *aggr;
    struct fsm_dpi_client_session *client_session;
    struct fsm_session working_session;
    struct fsm_session broken_session;
    int ret;

    client_session = (struct fsm_dpi_client_session *)g_session.handler_ctxt;
    TEST_ASSERT_NULL(client_session);

    fsm_dpi_adt_upnp_exit(&g_session);

    /* Check the correctness of the legacy entry point */
    ret = dpi_adt_upnp_plugin_init(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    broken_session.location_id = NULL;
    broken_session.node_id = NULL;
    ret = fsm_dpi_adt_upnp_init(&broken_session);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    broken_session.location_id = "location";
    broken_session.node_id = NULL;
    ret = fsm_dpi_adt_upnp_init(&broken_session);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    broken_session.location_id = NULL;
    broken_session.node_id = "node";
    ret = fsm_dpi_adt_upnp_init(&broken_session);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    /* Still missing handler_context */
    working_session = g_session;
    ret = fsm_dpi_adt_upnp_init(&working_session);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    /* Don't forget to mock the required ops */
    working_session.ops.get_config = mock_get_config;

    /* add a handler_context, now we're complete */
    working_session.p_ops = &g_plugin_ops;

    new_client_session = CALLOC(1, sizeof(*new_client_session));
    working_session.handler_ctxt = new_client_session;

    ret = fsm_dpi_adt_upnp_init(&working_session);
    TEST_ASSERT_EQUAL_INT(0, ret);
    client_session = (struct fsm_dpi_client_session *)working_session.handler_ctxt;
    TEST_ASSERT_NOT_NULL(client_session);
    adt_upnp_session = (struct fsm_dpi_adt_upnp_session *)client_session->private_session;
    TEST_ASSERT_NOT_NULL(adt_upnp_session);
    aggr = adt_upnp_session->adt_upnp_aggr;
    TEST_ASSERT_TRUE(aggr->initialized);

    /* Already initialized: should return 1 */
    ret = fsm_dpi_adt_upnp_init(&working_session);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* Cleanup */
    fsm_dpi_adt_upnp_exit(&working_session);
    FREE(new_client_session);
}

void
test_fsm_dpi_adt_upnp_process_attr(void)
{
    // struct fsm_dpi_adt_upnp_session *adt_upnp_session;
    int fail_return = FSM_DPI_PASSTHRU;
    struct fsm_session this_session;
    char attribute[16];
    char value[32];
    int length;
    int ret;

    /* First some broken cases */
    ret = fsm_dpi_adt_upnp_process_attr(NULL, NULL, 0, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(fail_return, ret);

    /* Check on the attribute */
    ret = fsm_dpi_adt_upnp_process_attr(&this_session, NULL, 0, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(fail_return, ret);
    attribute[0] = '\0';
    ret = fsm_dpi_adt_upnp_process_attr(&this_session, NULL, 0, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(fail_return, ret);

    STRSCPY(attribute, "key");
    /* check on value */
    ret = fsm_dpi_adt_upnp_process_attr(&this_session, attribute, 0, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(fail_return, ret);

    STRSCPY(value, "the_value");
    ret = fsm_dpi_adt_upnp_process_attr(&this_session, attribute, 0, 0, value, NULL);
    TEST_ASSERT_EQUAL_INT(fail_return, ret);

    length = strlen(value);
    ret = fsm_dpi_adt_upnp_process_attr(&this_session, attribute, 0, length, value, NULL);
    TEST_ASSERT_EQUAL_INT(fail_return, ret);
}

void
test_notify_dev(void)
{
    struct fsm_dpi_adt_upnp_session adt_upnp_session;
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_client_session client_session;
    struct net_md_stats_accumulator acc;
    struct fsm_session working_session;
    struct net_md_flow_key key;
    int ret;

    MEMZERO(working_session);
    MEMZERO(client_session);
    MEMZERO(adt_upnp_session);
    MEMZERO(pkt_info);

    working_session.handler_ctxt = &client_session;
    client_session.private_session = &adt_upnp_session;
    adt_upnp_session.initialized = true;

    ret = fsm_dpi_adt_upnp_process_notify(&working_session, "the_upnp_url", &pkt_info);
    TEST_ASSERT_EQUAL_INT32(-1, ret);

    MEMZERO(key);
    key.ip_version = 4;
    key.direction = NET_MD_ACC_INBOUND_DIR;

    MEMZERO(acc);
    acc.key = &key;
    pkt_info.acc = &acc;

    ret = fsm_dpi_adt_upnp_process_notify(&working_session, "the_upnp_url", &pkt_info);
    TEST_ASSERT_EQUAL_INT32(-2, ret);
}

extern int fsm_dpi_adt_upnp_dev_id_cmp(const void *a, const void *b);
void
test_fsm_dpi_adt_upnp_get_device(void)
{
    struct fsm_dpi_adt_upnp_session adt_upnp_session;
    struct fsm_dpi_adt_upnp_root_desc *ret1;
    struct fsm_dpi_adt_upnp_root_desc *ret2;
    char *calling_function = "foo";

    os_macaddr_t mac1 = { .addr = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};
    os_macaddr_t mac2 = { .addr = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}};
    char *url1 = "http://IP/rootdesc.xml";
    char *url2 = "http://ANOTHER_IP/desc.xml";

    MEMZERO(adt_upnp_session);
    ds_tree_init(&adt_upnp_session.session_upnp_devices, fsm_dpi_adt_upnp_dev_id_cmp,
                 struct upnp_device, next_node);
    adt_upnp_session.initialized = true;

    ret1 = fsm_dpi_adt_upnp_get_device(&adt_upnp_session, &mac1, url1);
    TEST_ASSERT_NOT_NULL(ret1);
    TEST_ASSERT_EQUAL_STRING(url1, ret1->url);

    ret2 = fsm_dpi_adt_upnp_get_device(&adt_upnp_session, &mac2, url2);
    TEST_ASSERT_NOT_NULL(ret1);
    TEST_ASSERT_EQUAL_STRING(url2, ret2->url);

    ret1 = fsm_dpi_adt_upnp_get_device(&adt_upnp_session, &mac2, url1);
    TEST_ASSERT_NOT_NULL(ret1);
    TEST_ASSERT_EQUAL_STRING(url1, ret1->url);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_ADT_UPNP_INIT, ret1->state);

    /* Check if the state is properly stored */
    ret1->state = FSM_DPI_ADT_UPNP_STARTED;
    ret2 = fsm_dpi_adt_upnp_get_device(&adt_upnp_session, &mac2, url1);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_ADT_UPNP_STARTED, ret2->state);

    /* Check if state is really per stored record */
    ret2 = fsm_dpi_adt_upnp_get_device(&adt_upnp_session, &mac2, url2);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_ADT_UPNP_INIT, ret2->state);

    STRSCPY(ret2->friendly_name, "My friendly name");
    /* Using the oppoertunity to make sure the next function doesn't crash */
    /* Start with corner cases */
    dump_upnp_cache(NULL, NULL);
    dump_upnp_cache(calling_function, NULL);

    /* Effectively dump the cache previously populated */
    dump_upnp_cache(__func__, &adt_upnp_session.session_upnp_devices);

    /* Clean things up */
    fsm_dpi_adt_free_cache(&adt_upnp_session.session_upnp_devices);
}

void
test_jencode_adt_upnp_report(void)
{
    struct adt_upnp_report to_report;
    struct fsm_session *session;
    char *report;
    char *ret;

    /* address the corner case first */
    ret = jencode_adt_upnp_report(NULL, NULL);
    TEST_ASSERT_NULL(ret);

    session = CALLOC(1, sizeof(*session));
    ret = jencode_adt_upnp_report(session, NULL);
    TEST_ASSERT_NULL(ret);

    MEMZERO(to_report);
    ret = jencode_adt_upnp_report(session, &to_report);
    TEST_ASSERT_NULL(ret);

    FREE(session);

    /* Now we can handle a known working case */
    session = &g_session;
    to_report.url = &g_adt_upnp_data;
    to_report.url->udev = CALLOC(1, sizeof(*to_report.url->udev));
    os_nif_macaddr_from_str(&to_report.url->udev->device_mac, "00:01:02:03:04:05");
    to_report.url->session = session;
    adt_upnp_init_elements(&g_adt_upnp_data);
    to_report.nelems = 11;
    to_report.first = adt_upnp_get_elements();
    report = jencode_adt_upnp_report(session, &to_report);
    TEST_ASSERT_NOT_NULL(report);

    LOGD("%s: %s", __func__, report);

    /* Cleanup */
    json_free(report);
    FREE(to_report.url->udev);
}

/* Truly nothing much to test except the absence of a crash */
void
test_hex_dump(void)
{
    char function[] = "the_function";
    char buffer[] = "abcdefABCDEF-?\t\n";
    size_t l = sizeof(buffer);

    hexdump(NULL, buffer, l);

    hexdump(NULL, NULL, 1);
    hexdump(function, buffer, 0);

    hexdump(function, buffer, l-5);
    hexdump(function, buffer, l);
}

void
run_test_adt_upnp(void)
{
    RUN_TEST(test_fsm_dpi_adt_upnp_init_exit);
    RUN_TEST(test_notify_dev);
    RUN_TEST(test_fsm_dpi_adt_upnp_process_attr);

    RUN_TEST(test_jencode_adt_upnp_report);
    RUN_TEST(test_fsm_dpi_adt_upnp_get_device);

    RUN_TEST(test_hex_dump);
}
