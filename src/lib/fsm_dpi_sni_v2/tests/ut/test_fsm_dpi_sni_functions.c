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

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fsm.h"
#include "fsm_dpi_sni.h"
#include "fsm_policy.h"
#include "log.h"
#include "memutil.h"
#include "os_nif.h"
#include "os_types.h"
#include "unity.h"
#include "unity_internals.h"

extern int fsm_session_cmp(void *a, void *b);

static struct fsm_session_conf g_confs[] =
{
    /* entry 0 */
    {
        .handler = "mdns_plugin_session_0",
        .if_name = "foo",
    }
};

static struct fsm_session g_sessions[] =
{
    {
        .type = FSM_PARSER,
        .conf = &g_confs[0],
        .node_id = "1S6D808DB4",
        .location_id = "5e3a194bb03594384016458",
        .name = "TEST_SESSION",
    }
};

struct fsm_web_cat_ops g_plugin_ops =
{
    .categories_check = NULL,
    .risk_level_check = NULL,
    .cat2str = NULL,
    .get_stats = NULL,
    .dns_response = NULL,
    .gatekeeper_req = NULL,
};

union fsm_plugin_ops g_p_ops;

char *get_config(struct fsm_session *session, char *key)
{
    (void)session;
    (void)key;

    return "";
}

void
test_get_mgr(void)
{
    struct fsm_dpi_sni_cache *mgr;

    mgr = fsm_dpi_sni_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);
    TEST_ASSERT_FALSE(mgr->initialized);

    mgr = fsm_dpi_sni_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);
    TEST_ASSERT_FALSE(mgr->initialized);
}

void
test_fsm_req_type(void)
{
   int ret;

   ret = dpi_sni_get_req_type("http.host");
   TEST_ASSERT_EQUAL_INT(FSM_HOST_REQ, ret);

   ret = dpi_sni_get_req_type("tls.sni");
   TEST_ASSERT_EQUAL_INT(FSM_SNI_REQ, ret);

   ret = dpi_sni_get_req_type("http.url");
   TEST_ASSERT_EQUAL_INT(FSM_URL_REQ, ret);

   ret = dpi_sni_get_req_type("tag");
   TEST_ASSERT_EQUAL_INT(FSM_APP_REQ, ret);

   ret = dpi_sni_get_req_type(NULL);
   TEST_ASSERT_EQUAL_INT(FSM_UNKNOWN_REQ_TYPE, ret);
   ret = dpi_sni_get_req_type("");
   TEST_ASSERT_EQUAL_INT(FSM_UNKNOWN_REQ_TYPE, ret);
   ret = dpi_sni_get_req_type("random_stuff");
   TEST_ASSERT_EQUAL_INT(FSM_UNKNOWN_REQ_TYPE, ret);
}

void
test_fsm_session_cmp(void)
{
    struct fsm_session s1;
    struct fsm_session s2;
    int ret1;
    int ret2;

    ret1 = fsm_session_cmp(&s1, &s2);
    ret2 = fsm_session_cmp(&s2, &s1);
    TEST_ASSERT_TRUE(ret1 == -ret2);

    ret1 = fsm_session_cmp(&s1, &s1);
    TEST_ASSERT_TRUE(ret1 == 0);

    ret1 = fsm_session_cmp(NULL, NULL);
    TEST_ASSERT_TRUE(ret1 == 0);
}


/**
 * @brief validate that no session provided is handled correctly
 */
void
test_fsm_dpi_plugin_init_exit(void)
{
    struct fsm_dpi_sni_cache *mgr;
    struct fsm_session *session;
    int ret;

    mgr = fsm_dpi_sni_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);
    TEST_ASSERT_FALSE(mgr->initialized);

    session = NULL;

    ret = fsm_dpi_sni_init(session);
    TEST_ASSERT_EQUAL_INT(-1, ret);
    TEST_ASSERT_FALSE(mgr->initialized);

    /* Exit the plugin */
    fsm_dpi_sni_exit(session);

    /* Pass a session now */
    g_sessions[0].ops.get_config = &get_config;
    ret = fsm_dpi_sni_init(&g_sessions[0]);
    /* no p_ops => fail */
    TEST_ASSERT_EQUAL_INT(-1, ret);

    g_sessions[0].p_ops = &g_p_ops;
    g_sessions[0].name = "test_dpi_sni";

    ret = fsm_dpi_sni_init(&g_sessions[0]);
    TEST_ASSERT_EQUAL_INT(0, ret);
    /* Try once more. fsm_dpi_sni_session already initialized. */
    ret = fsm_dpi_sni_init(&g_sessions[0]);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* Exit the plugin */
    fsm_dpi_sni_exit(&g_sessions[0]);
}

void
test_fsm_dpi_sni_plugin_periodic(void)
{
    struct fsm_dpi_sni_session u_session;
    struct fsm_session *session;
    time_t new_ttl;
    time_t now;

    session = &g_sessions[0];
    session->handler_ctxt = NULL;

    /* Test corner case */
    fsm_dpi_sni_periodic(session);
    /* Nothing to check on... but this should never crash */

    /* Pass a complete session */
    now = time(NULL);
    u_session.timestamp = now;
    // u_session.session_type = FSM_SESSION_TYPE_SNI;
    session->handler_ctxt = &u_session;
    fsm_dpi_sni_periodic(session);
    TEST_ASSERT_EQUAL(now, u_session.timestamp);

    /* Changing TTL for SNI should not impact client TTL */
    new_ttl = 2;
    fsm_dpi_sni_set_ttl(session, new_ttl);
    LOGD("Sleeping %d seconds to hit TTL (%d seconds can lead to some false positives with TTL==%d)",
         (int)(new_ttl + 2), (int)(new_ttl + 1), (int)new_ttl);
    sleep(new_ttl + 2);
    fsm_dpi_sni_periodic(session);
    now = time(NULL);
    TEST_ASSERT_EQUAL(now, u_session.timestamp);
}

void
test_fsm_dpi_sni_create_request(void)
{
    struct fsm_request_args request_args;
    struct fsm_policy_req *request;

    g_sessions[0].service = &g_sessions[0];
    g_sessions[0].provider_ops = &g_plugin_ops;
    request_args.session = &g_sessions[0];
    request_args.device_id = CALLOC(1, sizeof(*request_args.device_id));
    os_nif_macaddr_from_str(request_args.device_id, "00:11:22:33:44:55");

    request = dpi_sni_create_request(NULL, NULL);
    TEST_ASSERT_NULL(request);

    request = dpi_sni_create_request(&request_args, NULL);
    TEST_ASSERT_NULL(request);

    request = dpi_sni_create_request(&request_args, "ATTR_VALUE");
    TEST_ASSERT_NOT_NULL(request);
    TEST_ASSERT_EQUAL_STRING("ATTR_VALUE", request->url);
    TEST_ASSERT_EQUAL_STRING("ATTR_VALUE", request->fqdn_req->req_info->url);
    TEST_ASSERT_EQUAL_INT(1, request->fqdn_req->numq);

    /* TODO: More validation required */

    /* clean things up */
    FREE(request->url);
    fsm_policy_free_request(request);
    FREE(request_args.device_id);
}

void
test_fsm_dpi_sni_create_reply(void)
{
    struct fsm_request_args request_args;
    struct fsm_policy_reply *reply;

    reply = dpi_sni_create_reply(NULL);
    TEST_ASSERT_NULL(reply);

    memset(&request_args, 0, sizeof(request_args));
    reply = dpi_sni_create_reply(&request_args);
    TEST_ASSERT_NULL(reply);

    g_sessions[0].service = &g_sessions[0];
    g_sessions[0].provider_ops = &g_plugin_ops;
    request_args.session = &g_sessions[0];
    reply = dpi_sni_create_reply(&request_args);
    TEST_ASSERT_NOT_NULL(reply);

    /* clean things up */
    fsm_policy_free_reply(reply);
}

void
test_fsm_dpi_sni_process_attr(void)
{
    struct fsm_dpi_plugin_client_pkt_info pkt_info;
    struct fsm_dpi_sni_session u_session;
    struct net_md_stats_accumulator acc;
    struct net_md_flow_key key;
    struct fsm_session session;
    int64_t num;
    char *attr;
    int ret;

    memset(&u_session, 0, sizeof(u_session));

    /* corner cases */
    ret = fsm_dpi_sni_process_attr(NULL, NULL, 0, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    memset(&acc, 0, sizeof(acc));
    pkt_info.acc = &acc;
    acc.originator = NET_MD_ACC_UNKNOWN_ORIGINATOR;
    ret = fsm_dpi_sni_process_attr(NULL, NULL, 0, 0, NULL, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;
    ret = fsm_dpi_sni_process_attr(NULL, NULL, 0, 0, NULL, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    acc.key = NULL;
    ret = fsm_dpi_sni_process_attr(NULL, NULL, 0, 0, NULL, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    memset(&key, 0, sizeof(key));
    acc.key = &key;
    memset(&session, 0, sizeof(session));
    ret = fsm_dpi_sni_process_attr(&session, NULL, 0, 0, NULL, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    session.handler_ctxt = &u_session;
    ret = fsm_dpi_sni_process_attr(&session, NULL, 0, 0, NULL, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    /* populate acc */
    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;
    key.ip_version = 4;
    key.smac = CALLOC(1, sizeof(*key.smac));
    os_nif_macaddr_from_str(key.smac, "00:11:22:33:44:55");

    ret = fsm_dpi_sni_process_attr(&session, NULL, 0, 0, NULL, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    u_session.excluded_devices = "$[all_clients]";
    u_session.included_devices = NULL;
    ret = fsm_dpi_sni_process_attr(&session, NULL, 0, 0, NULL, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    u_session.excluded_devices = NULL;
    u_session.included_devices = "$[all_clients]";
    ret = fsm_dpi_sni_process_attr(&session, NULL, 0, 0, NULL, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    /* instantiate the service */
    u_session.excluded_devices = "$[all_clients]";
    u_session.included_devices = NULL;
    session.service = &session;
    ret = fsm_dpi_sni_process_attr(&session, NULL, 0, 0, NULL, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    /* Add the request info */
    attr = "http.host";

    /* test with wrong type */
    num = 123;
    ret = fsm_dpi_sni_process_attr(&session, attr, RTS_TYPE_NUMBER, sizeof(int64_t), &num, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    ret = fsm_dpi_sni_process_attr(&session, attr, RTS_TYPE_STRING, 0, NULL, &pkt_info);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    /* No need to test with a 'value' as the case is covered
     * in testing dpi_sni_policy_req()
     */

    /* Cleanup */
    FREE(key.smac);
}

void
run_test_functions(void)
{
    RUN_TEST(test_get_mgr);
    RUN_TEST(test_fsm_req_type);
    RUN_TEST(test_fsm_session_cmp);
    RUN_TEST(test_fsm_dpi_plugin_init_exit);
    RUN_TEST(test_fsm_dpi_sni_plugin_periodic);
    RUN_TEST(test_fsm_dpi_sni_create_request);
    RUN_TEST(test_fsm_dpi_sni_create_reply);
    RUN_TEST(test_fsm_dpi_sni_process_attr);
}
