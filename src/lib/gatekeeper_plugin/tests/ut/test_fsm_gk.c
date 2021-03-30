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
#include <sys/socket.h>
#include <curl/curl.h>
#include <netdb.h>

#include "gatekeeper_multi_curl.h"
#include "gatekeeper_single_curl.h"
#include "gatekeeper_cache.h"
#include "fsm_dpi_sni.h"
#include "gatekeeper_data.h"
#include "gatekeeper.h"
#include "wc_telemetry.h"
#include "json_util.h"
#include "json_mqtt.h"
#include "schema.h"
#include "target.h"
#include "unity.h"
#include "fsm.h"
#include "log.h"

#define OTHER_CONFIG_NELEMS 3
#define OTHER_CONFIG_NELEM_SIZE 128

char *g_location_id = "foo";
char *g_node_id = "bar";

const char *test_name           = "fsm_gk_tests";
static const char *g_server_url = "https://ovs_dev.plume.com:443/";
static char *g_certs_file = "/tmp/cacert.pem";
static bool g_is_connected;

char *
gk_get_other_config_val(struct fsm_session *session, char *key);

char g_other_configs[][3][OTHER_CONFIG_NELEMS][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "gk_url",
            "cacert",
            "mqtt_v"
        },
        {
            "https://ovs_dev.plume.com:443/",
            "/tmp/cacert.pem",
            "dev-test/gk_ut_topic",
        },
    },
};

static
void send_report(struct fsm_session *session, char *report)
{

}

struct fsm_session_ops g_ops =
{
    .get_config = gk_get_other_config_val,
    .send_report = send_report,
};

struct fsm_session_conf g_confs[1] =
{
    /* entry 1 */
    {
        .handler = "gatekeeper_session",
    }
};

struct fsm_session g_sessions[1] =
{
    {
        .type = FSM_WEB_CAT,
        .conf = &g_confs[0],
    },
};

union fsm_plugin_ops g_plugin_ops =
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

struct schema_FSM_Policy spolicies[] =
{
    { /* entry 0 */
        .policy_exists = true,
        .policy = "dev_plume_ipthreat",
        .name = "RuleIpThreat0",
        .idx = 9,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .risk_op_exists = false,
        .risk_op = "lte",
        .risk_level = 7,
        .ipaddr_op_exists = true,
        .ipaddr_op = "in",
        .ipaddrs_len = 2,
        .ipaddrs =
        {
            "1.2.3.5",
            "::1",
        },
        .action_exists = true,
        .action = "gatekeeper",
        .log_exists = true,
        .log = "blocked",
    },
    { /* entry 1 */
        .policy_exists = true,
        .policy = "policy_dev_gatekeeper",
        .name = "dev_fqdn_in",
        .mac_op_exists = true,
        .mac_op = "in",
        .macs_len = 3,
        .macs =
        {
            "00:00:00:00:00:00",
            "11:22:33:44:55:66",
            "22:33:44:55:66:77"
        },
        .idx = 10,
        .fqdn_op_exists = true,
        .fqdn_op = "in",
        .fqdns_len = 6,
        .fqdns = {"www.cnn.com", "www.google.com", "test_host", "signal", "localhost", "http://whitehouse.info"},
        .fqdncat_op_exists = false,
        .risk_op_exists = false,
        .ipaddr_op_exists = false,
        .ipaddrs_len = 0,
        .action_exists = true,
        .action = "gatekeeper",
        .other_config_len = 2,
        .other_config_keys = {"tagv4_name", "tagv6_name",},
        .other_config = { "my_v4_tag", "my_v6_tag"},
    }
};

static bool
file_present(const char *filename)
{
    struct stat buffer;

    return (stat(filename, &buffer) == 0);
}

static bool
check_connection(void)
{
    CURL *curl;
    CURLcode response;

    LOGN("checking server connection..server url %s, certs path %s", g_server_url, g_certs_file);

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, g_server_url);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(curl, CURLOPT_CAINFO, g_certs_file);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 3L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);

    /* don't write output to stdout */
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

    /* Perform the request */
    response = curl_easy_perform(curl);

    if (response != CURLE_OK) {
    LOGN("%s(): curl_easy_perform failed: ret code: %d (%s)",
             __func__, response, curl_easy_strerror(response));
    }


    /* always cleanup */
    curl_easy_cleanup(curl);

    LOGN("server connection: %s",
         (response == CURLE_OK ? "success" : "Failed"));

    return (response == CURLE_OK) ? 1 : 0;
}

char *
gk_get_other_config_val(struct fsm_session *session, char *key)
{
    struct fsm_session_conf *fconf;
    struct str_pair *pair;
    ds_tree_t *tree;

    if (session == NULL) return NULL;

    fconf = session->conf;
    if (fconf == NULL) return NULL;

    tree = fconf->other_config;
    if (tree == NULL) return NULL;

    pair = ds_tree_find(tree, key);
    if (pair == NULL) return NULL;

    return pair->value;
}

void
setUp(void)
{
    struct fsm_session *session = &g_sessions[0];
    struct fsm_policy_session *mgr;
    struct str_pair *pair;
    struct ev_loop *loop;

    loop = ev_default_loop(0);

    mgr = fsm_policy_get_mgr();
    if (!mgr->initialized) fsm_init_manager();

    session->conf = &g_confs[0];
    session->ops  = g_ops;
    session->name = g_confs[0].handler;
    session->conf->other_config = schema2tree(OTHER_CONFIG_NELEM_SIZE,
                                              OTHER_CONFIG_NELEM_SIZE,
                                              OTHER_CONFIG_NELEMS,
                                              g_other_configs[0][0],
                                              g_other_configs[0][1]);
    session->p_ops = &g_plugin_ops;
    pair = ds_tree_find(session->conf->other_config, "mqtt_v");
    session->topic = pair->value;
    session->location_id = g_location_id;
    session->node_id = g_node_id;
    session->loop =  loop;

    gatekeeper_module_init(session);
    ev_run(loop, 0);
}

void tearDown(void)
{
    struct fsm_session *session = &g_sessions[0];

    free_str_tree(session->conf->other_config);
    gk_cache_cleanup();

    gatekeeper_exit(session);
    return;
}

bool
dummy_gatekeeper_get_verdict(struct fsm_session *session,
                             struct fsm_policy_req *req)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_curl_easy_info *ecurl_info;
    struct gatekeeper_offline *offline;
    struct fsm_policy_req *policy_req;
    struct fqdn_pending_req *fqdn_req;
    struct fsm_gk_verdict *gk_verdict;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;
    struct fsm_url_stats *stats;
    bool ret = true;
    int gk_response;

    fsm_gk_session = gatekeeper_lookup_session(session);
    if (!fsm_gk_session) return false;

    gk_verdict = calloc(1, sizeof(*gk_verdict));
    if (gk_verdict == NULL) return false;

    gk_verdict->policy_req = req;
    policy_req = gk_verdict->policy_req;
    fqdn_req = policy_req->fqdn_req;
    req_info = fqdn_req->req_info;

    url_reply = calloc(1, sizeof(struct fsm_url_reply));
    if (url_reply == NULL) return false;

    req_info->reply = url_reply;

    url_reply->service_id = URL_GK_SVC;

    stats = &fsm_gk_session->health_stats;
    offline = &fsm_gk_session->gk_offline;

    ret = gk_check_policy_in_cache(req);
    if (ret == true)
    {
        stats->cache_hits++;
        LOGN("%s found in cache, return action %d from cache", req->url, req->reply.action);
        free(gk_verdict);
        return true;
    }
    if (offline->provider_offline)
    {
        time_t now = time(NULL);
        bool backoff;

        backoff = ((now - offline->offline_ts) < offline->check_offline);

        if (backoff)
        {
            free(gk_verdict);
            return false;
        }
        offline->provider_offline = false;
    }
    ecurl_info = &fsm_gk_session->ecurl;
    ecurl_info->cert_path = g_certs_file;

    LOGT("%s: url:%s path:%s", __func__, ecurl_info->server_url, ecurl_info->cert_path);

    gk_verdict->gk_pb = gatekeeper_get_req(session, req);
    if (gk_verdict->gk_pb == NULL)
    {
        LOGN("%s() curl request serialization failed", __func__);
        ret = false;
        goto error;
    }

    fqdn_req->categorized = FSM_FQDN_CAT_SUCCESS;

#ifdef CURL_MULTI
    gk_new_conn(req->url);
#else
    gk_response = gk_send_request(session, fsm_gk_session, gk_verdict);
    if (gk_response != GK_LOOKUP_SUCCESS)
    {
        fqdn_req->categorized = FSM_FQDN_CAT_FAILED;
        /* if connection error, start backoff timer */
        if (gk_response == GK_CONNECTION_ERROR)
        {
            offline->provider_offline = true;
            offline->offline_ts = time(NULL);
            offline->connection_failures++;
        }
        LOGN("%s() curl error not updating cache", __func__);
        ret = false;
        goto error;
    }
#endif

    gk_add_policy_to_cache(req);

error:
    free_gk_verdict(gk_verdict);
    return ret;
}

void
test_curl_fqdn(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fsm_policy_rules *rules;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_reply *reply;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session *session;
    struct str_set *macs_set;
    struct str_set *fqdns_set;
    size_t i;

    session = &g_sessions[0];
    LOGN("Starting test %s()", __func__);
    if (g_is_connected == false) return;

    /* Initialize local structures */
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    spolicy = &spolicies[1];

    /* Validate access to the fsm policy */
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* Validate mac content */
    rules    = &fpolicy->rules;
    macs_set = rules->macs;
    TEST_ASSERT_NOT_NULL(macs_set);
    TEST_ASSERT_EQUAL_INT(spolicy->macs_len, macs_set->nelems);
    for (i = 0; i < macs_set->nelems; i++)
    {
        TEST_ASSERT_EQUAL_STRING(spolicy->macs[i], macs_set->array[i]);
    }

    /* Validate FQDNs content */
    fqdns_set = rules->fqdns;
    TEST_ASSERT_NOT_NULL(fqdns_set);

    mgr   = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    req.device_id = &dev_mac;

    /* Build the request elements */
    STRSCPY(req_info.url, "www.google.com");
    req.url                   = "www.google.com";
    fqdn_req.req_info         = &req_info;
    fqdn_req.numq             = 1;
    fqdn_req.policy_table     = table;
    fqdn_req.gatekeeper_req   = dummy_gatekeeper_get_verdict;
    fqdn_req.req_type         = FSM_SNI_REQ;
    req.fqdn_req              = &fqdn_req;
    fsm_apply_policies(session, &req);
    reply = &req.reply;


    free(reply->rule_name);
    free(reply->policy);
    fsm_free_url_reply(fqdn_req.req_info->reply);

    LOGN("Finishing test %s()", __func__);
}

void
run_fqdn_query(char *input_url)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fsm_policy_rules *rules;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_reply *reply;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session *session;
    struct str_set *macs_set;
    struct str_set *fqdns_set;
    size_t i;

    session = &g_sessions[0];
    if (g_is_connected == false) return;

    /* Initialize local structures */
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    spolicy = &spolicies[1];

    /* Validate access to the fsm policy */
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* Validate mac content */
    rules    = &fpolicy->rules;
    macs_set = rules->macs;
    TEST_ASSERT_NOT_NULL(macs_set);
    TEST_ASSERT_EQUAL_INT(spolicy->macs_len, macs_set->nelems);
    for (i = 0; i < macs_set->nelems; i++)
    {
        TEST_ASSERT_EQUAL_STRING(spolicy->macs[i], macs_set->array[i]);
    }

    /* Validate FQDNs content */
    fqdns_set = rules->fqdns;
    TEST_ASSERT_NOT_NULL(fqdns_set);

    mgr   = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    req.device_id = &dev_mac;

    /* Build the request elements */
    STRSCPY(req_info.url, input_url);
    req.url                   = input_url;
    fqdn_req.req_info         = &req_info;
    fqdn_req.numq             = 1;
    fqdn_req.policy_table     = table;
    fqdn_req.gatekeeper_req   = dummy_gatekeeper_get_verdict;
    fqdn_req.req_type         = FSM_FQDN_REQ;
    req.fqdn_req              = &fqdn_req;
    fsm_apply_policies(session, &req);
    reply = &req.reply;

    free(reply->rule_name);
    free(reply->policy);
    fsm_free_url_reply(fqdn_req.req_info->reply);

}

void
test_curl_multi(void)
{
    struct net_md_stats_accumulator acc;
    struct fsm_session *session;
    struct schema_FSM_Policy *spolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_session *mgr;
    struct fsm_policy_reply *reply;
    struct sockaddr_storage ss_ip;
    struct net_md_flow_key key;
    struct policy_table *table;
    struct fsm_policy *fpolicy;
    struct fsm_policy_req req;
    char *ip_str = "1.2.3.5";
    struct sockaddr_in *in4;
    os_macaddr_t dev_mac;
    struct in_addr in_ip;
    int rc;

    if (g_is_connected == false) return;

    /* Initialize local structures */
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));
    memset(&ss_ip, 0, sizeof(ss_ip));
    memset(&acc, 0, sizeof(acc));
    memset(&key, 0, sizeof(key));

    /* Insert IP policy */
    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    mgr     = fsm_policy_get_mgr();
    table   = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    session = &g_sessions[0];

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    rc = inet_pton(AF_INET, ip_str, &in_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    in4 = (struct sockaddr_in *)&ss_ip;
    memset(in4, 0, sizeof(struct sockaddr_in));
    in4->sin_family = AF_INET;
    memcpy(&in4->sin_addr, &in_ip, sizeof(in4->sin_addr));

    fqdn_req.req_info = &req_info;
    rc = getnameinfo((struct sockaddr *)&ss_ip, sizeof(ss_ip),
                     fqdn_req.req_info->url, sizeof(fqdn_req.req_info->url),
                     0, 0, NI_NUMERICHOST);
    TEST_ASSERT_EQUAL_INT(0, rc);

    rc = strcmp(ip_str, fqdn_req.req_info->url);
    TEST_ASSERT_EQUAL_INT(0, rc);

    req.device_id = &dev_mac;
    req.fqdn_req  = &fqdn_req;

    key.src_ip     = (uint8_t *)&in_ip.s_addr;
    key.ip_version = 4;
    acc.key        = &key;
    acc.direction  = NET_MD_ACC_OUTBOUND_DIR;
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;
    /* Build the request elements */
    fqdn_req.numq             = 1;
    fqdn_req.policy_table     = table;
    fqdn_req.gatekeeper_req   = dummy_gatekeeper_get_verdict;
    req.fqdn_req              = &fqdn_req;
    req.device_id             = &dev_mac;
    req.ip_addr               = &ss_ip;
    req.acc                   = &acc;
    req.url                   = fqdn_req.req_info->url;
    fsm_apply_policies(session, &req);
    reply = &req.reply;

    free(reply->rule_name);
    free(reply->policy);
    fsm_free_url_reply(fqdn_req.req_info->reply);
}

void
test_curl_sni(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_reply *reply;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session *session;

    LOGN("Starting test %s()", __func__);
    session = &g_sessions[0];
    if (g_is_connected == false) return;

    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    spolicy = &spolicies[1];

    /* Validate access to the fsm policy */
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    /* Validate mac content */
    mgr           = fsm_policy_get_mgr();
    table         = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    req.device_id = &dev_mac;

    /* Build the request elements */
    STRSCPY(req_info.url, "www.google.com");
    req.url                   = "www.google.com";
    fqdn_req.req_info         = &req_info;
    fqdn_req.numq             = 1;
    fqdn_req.policy_table     = table;
    fqdn_req.gatekeeper_req   = dummy_gatekeeper_get_verdict;
    fqdn_req.req_type         = FSM_SNI_REQ;
    req.fqdn_req              = &fqdn_req;
    fsm_apply_policies(session, &req);
    reply = &req.reply;

    free(reply->rule_name);
    free(reply->policy);
    fsm_free_url_reply(fqdn_req.req_info->reply);

    LOGN("Finishing test %s()", __func__);
}

void
test_curl_url(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_reply *reply;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session *session;

    if (g_is_connected == false) return;
    session = &g_sessions[0];
    LOGN("Starting test %s()", __func__);

    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    spolicy = &spolicies[1];

    /* Validate access to the fsm policy */
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    mgr           = fsm_policy_get_mgr();
    table         = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    req.device_id = &dev_mac;

    /* Build the request elements */
    STRSCPY(req_info.url, "www.google.com");
    req.url                   = "www.google.com";
    fqdn_req.req_info         = &req_info;
    fqdn_req.numq             = 1;
    fqdn_req.policy_table     = table;
    fqdn_req.gatekeeper_req   = dummy_gatekeeper_get_verdict;
    fqdn_req.req_type         = FSM_URL_REQ;
    req.fqdn_req              = &fqdn_req;
    fsm_apply_policies(session, &req);
    reply = &req.reply;

    free(reply->rule_name);
    free(reply->policy);
    fsm_free_url_reply(fqdn_req.req_info->reply);


    LOGN("Finishing test %s()", __func__);
}

void
test_curl_host(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_reply *reply;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session *session;

    if (g_is_connected == false) return;
    LOGN("Starting test %s()", __func__);
    session = &g_sessions[0];

    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    spolicy = &spolicies[1];

    /* Validate access to the fsm policy */
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    mgr           = fsm_policy_get_mgr();
    table         = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    req.device_id = &dev_mac;

    /* Build the request elements */
    STRSCPY(req_info.url, "test_host");
    req.url                   = "test_host";
    fqdn_req.req_info         = &req_info;
    fqdn_req.numq             = 1;
    fqdn_req.policy_table     = table;
    fqdn_req.gatekeeper_req   = dummy_gatekeeper_get_verdict;
    fqdn_req.req_type         = FSM_HOST_REQ;
    req.fqdn_req              = &fqdn_req;
    fsm_apply_policies(session, &req);
    reply = &req.reply;

    free(reply->rule_name);
    free(reply->policy);
    fsm_free_url_reply(req_info.reply);

    LOGN("Finishing test %s()", __func__);
}

void
test_curl_app(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_reply *reply;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session *session;

    if (g_is_connected == false) return;
    session = &g_sessions[0];
    LOGN("Starting test %s()", __func__);

    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    spolicy = &spolicies[1];

    /* Validate access to the fsm policy */
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    mgr           = fsm_policy_get_mgr();
    table         = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    req.device_id = &dev_mac;

    /* Build the request elements */
    STRSCPY(req_info.url, "signal");
    req.url                   = "signal";
    fqdn_req.req_info         = &req_info;
    fqdn_req.numq             = 1;
    fqdn_req.policy_table     = table;
    fqdn_req.gatekeeper_req   = dummy_gatekeeper_get_verdict;
    fqdn_req.req_type         = FSM_APP_REQ;
    req.fqdn_req              = &fqdn_req;
    fsm_apply_policies(session, &req);
    reply = &req.reply;

    free(reply->rule_name);
    free(reply->policy);
    fsm_free_url_reply(fqdn_req.req_info->reply);

    LOGN("Finishing test %s()", __func__);
}

void
test_curl_ipv4_flow(void)
{
    struct net_md_stats_accumulator acc;
    struct fsm_session *session;
    struct schema_FSM_Policy *spolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_session *mgr;
    struct fsm_policy_reply *reply;
    struct sockaddr_storage ss_ip;
    struct net_md_flow_key key;
    struct policy_table *table;
    struct fsm_policy *fpolicy;
    struct fsm_policy_req req;
    char *ip_str = "1.2.3.5";
    struct sockaddr_in *in4;
    os_macaddr_t dev_mac;
    struct in_addr in_ip;
    int rc;

    if (g_is_connected == false) return;
    LOGN("Starting test %s()", __func__);
    session = &g_sessions[0];

    /* Initialize local structures */
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));
    memset(&ss_ip, 0, sizeof(ss_ip));
    memset(&acc, 0, sizeof(acc));
    memset(&key, 0, sizeof(key));

    /* Insert IP policy */
    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    mgr   = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate access to the fsm policy */
    TEST_ASSERT_NOT_NULL(fpolicy);

    rc = inet_pton(AF_INET, ip_str, &in_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    in4 = (struct sockaddr_in *)&ss_ip;

    memset(in4, 0, sizeof(struct sockaddr_in));
    in4->sin_family = AF_INET;
    memcpy(&in4->sin_addr, &in_ip, sizeof(in4->sin_addr));

    fqdn_req.req_info = &req_info;
    rc = getnameinfo((struct sockaddr *)&ss_ip, sizeof(ss_ip),
                     fqdn_req.req_info->url, sizeof(fqdn_req.req_info->url),
                     0, 0, NI_NUMERICHOST);
    TEST_ASSERT_EQUAL_INT(0, rc);

    rc = strcmp(ip_str, fqdn_req.req_info->url);
    TEST_ASSERT_EQUAL_INT(0, rc);

    req.device_id = &dev_mac;
    req.fqdn_req  = &fqdn_req;

    key.src_ip     = (uint8_t *)&in_ip.s_addr;
    key.dst_ip     = (uint8_t *)&in_ip.s_addr;
    key.ip_version = 4;
    acc.key        = &key;
    acc.direction  = NET_MD_ACC_OUTBOUND_DIR;
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;
    /* Build the request elements */
    fqdn_req.numq             = 1;
    fqdn_req.policy_table     = table;
    fqdn_req.gatekeeper_req   = dummy_gatekeeper_get_verdict;

    fqdn_req.req_type = FSM_IPV4_REQ;
    req.fqdn_req      = &fqdn_req;
    req.device_id     = &dev_mac;
    req.acc           = &acc;
    req.url           = fqdn_req.req_info->url;
    fsm_apply_policies(session, &req);
    reply = &req.reply;

    free(reply->rule_name);
    free(reply->policy);
    fsm_free_url_reply(fqdn_req.req_info->reply);

    LOGN("Finishing test %s()", __func__);
}

void
test_curl_ipv6_flow(void)
{
    struct net_md_stats_accumulator acc;
    struct fsm_session *session;
    struct schema_FSM_Policy *spolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_session *mgr;
    struct fsm_policy_reply *reply;
    struct sockaddr_storage ss_ip;
    struct net_md_flow_key v6_key;
    struct policy_table *table;
    struct fsm_policy *fpolicy;
    struct fsm_policy_req req;
    char *ip_str = "1.2.3.5";
    struct sockaddr_in *in4;
    os_macaddr_t dev_mac;
    struct in_addr in_ip;

    int rc;

    if (g_is_connected == false) return;
    LOGN("Starting test %s()", __func__);
    session = &g_sessions[0];
    /* Initialize local structures */
    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));
    memset(&ss_ip, 0, sizeof(ss_ip));
    memset(&acc, 0, sizeof(acc));
    memset(&v6_key, 0, sizeof(v6_key));

    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    mgr   = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    rc = inet_pton(AF_INET, ip_str, &in_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    in4 = (struct sockaddr_in *)&ss_ip;

    memset(in4, 0, sizeof(struct sockaddr_in));
    in4->sin_family = AF_INET;
    memcpy(&in4->sin_addr, &in_ip, sizeof(in4->sin_addr));

    fqdn_req.req_info = &req_info;
    rc = getnameinfo((struct sockaddr *)&ss_ip, sizeof(ss_ip),
                     fqdn_req.req_info->url, sizeof(fqdn_req.req_info->url),
                     0, 0, NI_NUMERICHOST);
    TEST_ASSERT_EQUAL_INT(0, rc);

    req.device_id = &dev_mac;
    req.fqdn_req  = &fqdn_req;

    /* check for IPv6 */
    v6_key.src_ip = calloc(1, 16);
    rc            = inet_pton(AF_INET6, "1:2::3", v6_key.src_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    v6_key.dst_ip = calloc(1, 16);
    TEST_ASSERT_NOT_NULL(v6_key.dst_ip);
    rc = inet_pton(AF_INET6, "2:2::2", v6_key.dst_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);
    v6_key.ip_version = 6;

    acc.key        = &v6_key;
    acc.direction  = NET_MD_ACC_OUTBOUND_DIR;
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;

    fqdn_req.numq           = 1;
    fqdn_req.policy_table   = table;
    fqdn_req.gatekeeper_req = dummy_gatekeeper_get_verdict;

    fqdn_req.req_type = FSM_IPV6_REQ;

    req.fqdn_req  = &fqdn_req;
    req.device_id = &dev_mac;
    req.ip_addr   = &ss_ip;
    req.acc       = &acc;
    req.url       = fqdn_req.req_info->url;
    fsm_apply_policies(session, &req);
    reply = &req.reply;
    // TEST_ASSERT_EQUAL_INT(FSM_ALLOW, reply->action);

    free(reply->rule_name);
    free(reply->policy);
    fsm_free_url_reply(fqdn_req.req_info->reply);
    free(v6_key.src_ip);
    free(v6_key.dst_ip);

    LOGN("Finishing test %s()", __func__);
}

void
test_health_stats_report(void)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_curl_easy_info *ecurl_info;
    struct fsm_session *session;
    struct fsm_url_stats *stats;
    struct wc_health_stats hs;

    LOGN("**** starting test %s ***** ", __func__);
    memset(&hs, 0, sizeof(hs));
    session        = &g_sessions[0];
    fsm_gk_session = gatekeeper_lookup_session(session);
    stats = &fsm_gk_session->health_stats;

    /* check for attribute type app,
     * since it is not present in the cache it should
     * do remote lookup and add to cache.
     */
    test_curl_app();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, hs.cached_entries);
    TEST_ASSERT_EQUAL_INT(1, stats->cloud_lookups);
    TEST_ASSERT_EQUAL_INT(0, stats->cache_hits);
    TEST_ASSERT_EQUAL_INT(1, hs.total_lookups);
    TEST_ASSERT_EQUAL_INT(1, hs.remote_lookups);
    TEST_ASSERT_EQUAL_INT(0, hs.connectivity_failures);
    TEST_ASSERT_EQUAL_INT(100000, hs.cache_size);

    /* doing a lookup again should return the action
     * from cache, the cache count should be the same
     */
    test_curl_app();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, hs.cached_entries);
    TEST_ASSERT_EQUAL_INT(1, stats->cloud_lookups);
    TEST_ASSERT_EQUAL_INT(1, stats->cache_hits);
    TEST_ASSERT_EQUAL_INT(2, hs.total_lookups);
    TEST_ASSERT_EQUAL_INT(1, hs.remote_lookups);
    TEST_ASSERT_EQUAL_INT(0, hs.connectivity_failures);

    /* checking for new attribute type should
     * trigger cloud lookup and add to cache.
     * cache entry should be incremented by 1
     */
    test_curl_url();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(2, hs.cached_entries);
    TEST_ASSERT_EQUAL_INT(2, stats->cloud_lookups);
    TEST_ASSERT_EQUAL_INT(1, stats->cache_hits);
    TEST_ASSERT_EQUAL_INT(3, hs.total_lookups);
    TEST_ASSERT_EQUAL_INT(2, hs.remote_lookups);
    TEST_ASSERT_EQUAL_INT(0, hs.connectivity_failures);

    /* doing a lookup again should return the action
     * from cache, the cache count should be the same
     */
    test_curl_url();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(2, hs.cached_entries);
    TEST_ASSERT_EQUAL_INT(2, stats->cloud_lookups);
    TEST_ASSERT_EQUAL_INT(2, stats->cache_hits);
    TEST_ASSERT_EQUAL_INT(4, hs.total_lookups);
    TEST_ASSERT_EQUAL_INT(2, hs.remote_lookups);
    TEST_ASSERT_EQUAL_INT(0, hs.connectivity_failures);


    ecurl_info = &fsm_gk_session->ecurl;
    /* providing invalid endpoint will result in
     * service failure
     */
    ecurl_info->server_url = "https://ovs_dev.plume.com:443/xyz";
    test_curl_host();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(0, hs.connectivity_failures);
    TEST_ASSERT_EQUAL_INT(1, hs.service_failures);
    /* other counters remain the same */
    TEST_ASSERT_EQUAL_INT(2, hs.cached_entries);
    TEST_ASSERT_EQUAL_INT(3, stats->cloud_lookups);
    TEST_ASSERT_EQUAL_INT(2, stats->cache_hits);
    TEST_ASSERT_EQUAL_INT(5, hs.total_lookups);
    TEST_ASSERT_EQUAL_INT(3, hs.remote_lookups);

    /* check failure condition, set server_url to invalid entry
     * curl request should fail, and connectivity_failure count
     * should be incremented.
     */
    ecurl_info->server_url = "1.2.3.4";
    test_curl_host();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, hs.connectivity_failures);
    /* service counter should not be incremented */
    TEST_ASSERT_EQUAL_INT(1, hs.service_failures);
    /* other counters remain the same */
    TEST_ASSERT_EQUAL_INT(2, hs.cached_entries);
    TEST_ASSERT_EQUAL_INT(3, stats->cloud_lookups);
    TEST_ASSERT_EQUAL_INT(2, stats->cache_hits);
    TEST_ASSERT_EQUAL_INT(5, hs.total_lookups);
    TEST_ASSERT_EQUAL_INT(3, hs.remote_lookups);

    LOGN("**** Ending test %s ***** ", __func__);
}

void
test_connection_timeout(void)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_curl_easy_info *ecurl_info;
    struct fsm_session *session;
    struct wc_health_stats hs;

    LOGN("**** starting test %s ***** ", __func__);
    memset(&hs, 0, sizeof(hs));
    session        = &g_sessions[0];
    fsm_gk_session = gatekeeper_lookup_session(session);

    /* check failure condition, set server_url to invalid entry
     * curl request should fail, and connectivity_failure count
     * should be incremented.
     */

    ecurl_info = &fsm_gk_session->ecurl;
    ecurl_info->server_url = "1.2.3.4";
    time_t start = time(NULL);
    test_curl_app();
    time_t end = time(NULL);
    double diff_time;

    diff_time = difftime(end, start);
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    /* connection timeout value is set to 2 seconds, it should
     * not block for more than 2 secs.
     */
    TEST_ASSERT_LESS_THAN(3, diff_time);
    TEST_ASSERT_EQUAL_INT(1, hs.connectivity_failures);

    LOGN("**** Ending test %s ***** ", __func__);
}

void
test_backoff_on_connection_failure(void)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_curl_easy_info *ecurl_info;
    struct fsm_session *session;
    struct fsm_url_stats *stats;
    struct wc_health_stats hs;

    LOGN("**** starting test %s ***** ", __func__);
    memset(&hs, 0, sizeof(hs));
    session        = &g_sessions[0];
    fsm_gk_session = gatekeeper_lookup_session(session);
    stats = &fsm_gk_session->health_stats;

    /* check failure condition, set server_url to invalid entry
     * curl request should fail, and connectivity_failure count
     * should be incremented. Backoff logic should kick in.
     * It should not try to connect to cloud for 30 secs
     */

    ecurl_info = &fsm_gk_session->ecurl;
    ecurl_info->server_url = "1.2.3.4";
    test_curl_app();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    /* connection fail counter should be set */
    TEST_ASSERT_EQUAL_INT(1, hs.connectivity_failures);
    TEST_ASSERT_EQUAL_INT(0, stats->cloud_lookups);

    /* Set the correct server url, it should connect now */
    ecurl_info->server_url = session->ops.get_config(session, "gk_url");
    sleep(5);
    test_curl_app();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    /* cloud lookup should still be 0, as still back-off logic is in place */
    TEST_ASSERT_EQUAL_INT(0, stats->cloud_lookups);

    sleep(30);
    /* backoff time 30 secs is expired now. */
    test_curl_app();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, stats->cloud_lookups);

    LOGN("**** Ending test %s ***** ", __func__);

}

void
test_send_report(void)
{
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy *fpolicy;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct fsm_policy_req req;
    os_macaddr_t dev_mac;
    struct fsm_policy_reply *reply;
    struct fsm_url_reply *url_reply;
    struct fsm_policy_session *mgr;
    struct policy_table *table;
    struct fsm_session *session;
    int len;

    if (g_is_connected == false) return;
    session = &g_sessions[0];
    LOGN("Starting test %s()", __func__);

    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&req, 0, sizeof(req));
    memset(&dev_mac, 0, sizeof(dev_mac));

    spolicy = &spolicies[1];

    /* Validate access to the fsm policy */
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    mgr           = fsm_policy_get_mgr();
    table         = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    req.device_id = &dev_mac;

    /* Build the request elements */
    STRSCPY(req_info.url, "www.google.com");
    req.url                 = "www.google.com";
    fqdn_req.req_info       = &req_info;
    fqdn_req.numq           = 1;
    fqdn_req.policy_table   = table;
    fqdn_req.gatekeeper_req = dummy_gatekeeper_get_verdict;
    fqdn_req.req_type       = FSM_URL_REQ;
    req.fqdn_req            = &fqdn_req;
    fsm_apply_policies(session, &req);
    reply = &req.reply;

    url_reply = req_info.reply;
    len       = strlen(url_reply->reply_info.gk_info.gk_policy);
    TEST_ASSERT_GREATER_THAN(0, url_reply->reply_info.gk_info.category_id);
    TEST_ASSERT_GREATER_THAN(0, url_reply->reply_info.gk_info.confidence_level);
    TEST_ASSERT_GREATER_THAN(0, len);

    free(reply->rule_name);
    free(reply->policy);
    fsm_free_url_reply(fqdn_req.req_info->reply);

    LOGN("Finishing test %s()", __func__);
}

void
test_fqdn_cache(void)
{
    test_curl_multi();
    test_curl_multi();
    test_curl_multi();
}

void
test_url_cache(void)
{
    test_curl_url();
    test_curl_url();
    test_curl_url();
}

void
test_host_cache(void)
{
    test_curl_host();
    test_curl_host();
    test_curl_host();
}

void
test_app_cache(void)
{
    test_curl_app();
    test_curl_app();
    test_curl_app();
}

/**
 * @brief test if gk cache is able to clear the
 * cache entries and then it can add entries to the
 * cache again.
 */
void
test_cache_clear(void)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_session *session;
    struct wc_health_stats hs;

    LOGN("**** starting test %s ***** ", __func__);
    /* start from clean cache */
    gk_cache_cleanup();
    memset(&hs, 0, sizeof(hs));
    session = &g_sessions[0];
    fsm_gk_session = gatekeeper_lookup_session(session);

    /* check for attribute type app,
     * since it is not present in the cache it should
     * do remote lookup and add to cache.
     */
    test_curl_app();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, hs.cached_entries);

    /* entry is present in the cache, no lookup will be performed */
    test_curl_app();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, hs.cached_entries);

    /* on clearing the entries in the cache should be 0 */
    clear_gatekeeper_cache();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(0, hs.cached_entries);

    /* lookup should be performed as cache is cleared */
    test_curl_app();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, hs.cached_entries);
}

void
test_categorization_count(void)
{
    struct fsm_gk_session *fsm_gk_session;
    struct gk_curl_easy_info *ecurl_info;
    struct fsm_session *session;
    struct wc_health_stats hs;

    LOGN("**** starting test %s ***** ", __func__);
    memset(&hs, 0, sizeof(hs));
    session        = &g_sessions[0];
    fsm_gk_session = gatekeeper_lookup_session(session);

    ecurl_info = &fsm_gk_session->ecurl;
    run_fqdn_query("www.google.com");
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(0, hs.service_failures);

    /* check failure condition, set server_url to invalid end point
     * curl will reply but reply cannot be processed.
     */

    ecurl_info = &fsm_gk_session->ecurl;
    /* invalid end point */
    ecurl_info->server_url = "https://ovs_dev.plume.com:443/xxxx";

    test_curl_host();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, hs.service_failures);
}

void
test_uncategory_count(void)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_session *session;
    struct wc_health_stats hs;

    LOGN("**** starting test %s ***** ", __func__);
    /* start from clean cache */
    gk_cache_cleanup();
    memset(&hs, 0, sizeof(hs));
    session = &g_sessions[0];
    fsm_gk_session = gatekeeper_lookup_session(session);

    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(0, hs.uncategorized);

    run_fqdn_query("localhost");
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, hs.uncategorized);

    run_fqdn_query("http://whitehouse.info");
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(2, hs.uncategorized);
}

void
test_cache_entry_report(void)
{
    struct fsm_gk_session *fsm_gk_session;
    struct fsm_session *session;
    struct wc_health_stats hs;

    LOGN("**** starting test %s ***** ", __func__);
    /* start from clean cache */
    gk_cache_cleanup();
    memset(&hs, 0, sizeof(hs));
    session        = &g_sessions[0];
    fsm_gk_session = gatekeeper_lookup_session(session);

    /* check for attribute type app,
     * since it is not present in the cache it should
     * do remote lookup and add to cache.
     */
    test_curl_app();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, hs.cached_entries);
    TEST_ASSERT_EQUAL_INT(100000, hs.cache_size);

    /* doing a lookup again should return the action
     * from cache, the cache count should be the same
     */
    test_curl_app();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(1, hs.cached_entries);
    TEST_ASSERT_EQUAL_INT(100000, hs.cache_size);

    /* checking for new attribute type should
     * trigger cloud lookup and add to cache.
     * cache entry should be incremented by 1
     */
    test_curl_url();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(2, hs.cached_entries);
    TEST_ASSERT_EQUAL_INT(100000, hs.cache_size);

    /* doing a lookup again should return the action
     * from cache, the cache count should be the same
     */
    test_curl_url();
    gatekeeper_report_compute_health_stats(fsm_gk_session, &hs);
    TEST_ASSERT_EQUAL_INT(2, hs.cached_entries);
    TEST_ASSERT_EQUAL_INT(100000, hs.cache_size);

    LOGN("**** Ending test %s ***** ", __func__);
}

void
test_endpoint_url(void)
{
    const char *endpoint_urls[] = { "https://gatekeeper.plume.com/fqdn",
                                    "https://gatekeeper.plume.com/http_url",
                                    "https://gatekeeper.plume.com/http_host",
                                    "https://gatekeeper.plume.com/https_sni",
                                    "https://gatekeeper.plume.com/ipv4",
                                    "https://gatekeeper.plume.com/ipv6",
                                    "https://gatekeeper.plume.com/app",
                                    "https://gatekeeper.plume.com/ipv4_tuple",
                                    "https://gatekeeper.plume.com/ipv6_tuple" };

    const char *server_url = "https://gatekeeper.plume.com";
    char url[1024];
    int i;

    LOGN("**** starting test %s ***** ", __func__);

    for (i = FSM_FQDN_REQ; i <= FSM_IPV6_FLOW_REQ; i++)
    {
        snprintf(url, sizeof(url), "%s/%s", server_url, gk_request_str(i));
        TEST_ASSERT_EQUAL_STRING(endpoint_urls[i], url);
    }

    LOGN("**** Ending test %s ***** ", __func__);
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    bool ret;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    ret = file_present(g_certs_file);
    if (ret == false)
    {
        LOGN("%s() certs not present at %s", __func__, g_certs_file);
        return 0;
    }

    ret = check_connection();
    if (ret == true) g_is_connected = true;

    RUN_TEST(test_curl_multi);
    RUN_TEST(test_curl_fqdn);
    RUN_TEST(test_curl_url);
    RUN_TEST(test_curl_host);
    RUN_TEST(test_curl_sni);
    RUN_TEST(test_curl_ipv4_flow);
    RUN_TEST(test_curl_ipv6_flow);
    RUN_TEST(test_curl_app);
    RUN_TEST(test_fqdn_cache);
    RUN_TEST(test_url_cache);
    RUN_TEST(test_host_cache);
    RUN_TEST(test_health_stats_report);
    RUN_TEST(test_connection_timeout);
    RUN_TEST(test_backoff_on_connection_failure);
    RUN_TEST(test_send_report);
    RUN_TEST(test_app_cache);
    RUN_TEST(test_cache_entry_report);
    RUN_TEST(test_cache_clear);
    RUN_TEST(test_uncategory_count);
    RUN_TEST(test_categorization_count);
    RUN_TEST(test_endpoint_url);

    return UNITY_END();
}
