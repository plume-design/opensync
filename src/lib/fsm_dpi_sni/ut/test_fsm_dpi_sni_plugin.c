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
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>


#include "dns_cache.h"
#include "fsm_dns_utils.h"
#include "gatekeeper_cache.h"
#include "dns_parse.h"
#include "fsm_dpi_sni.h"
#include "fsm_policy.h"
#include "memutil.h"
#include "network_metadata_report.h"
#include "network_metadata_utils.h"
#include "os.h"
#include "os_types.h"
#include "sockaddr_storage.h"
#include "unity.h"
#include "unity_internals.h"

os_macaddr_t g_src_mac =
{
    .addr = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 },
};

extern int fsm_session_cmp(void *a, void *b);

static struct fsm_session_conf global_confs[1] =
{
    /* entry 0 */
    {
        .handler = "gatekeeper_session",
    }
};

static struct fsm_session global_sessions[] =
{
    {
        .type = FSM_WEB_CAT,
        .conf = &global_confs[0],
    },
};

struct schema_FSM_Policy spolicies[] =
{
    { /* entry 0 */
        .policy_exists = true,
        .policy = "gatekeeper_p",
        .name = "fqdn_in",
        .mac_op_exists = true,
        .mac_op = "out",
        .macs_len = 3,
        .macs =
        {
            "00:00:00:00:00:00",
            "11:22:33:44:55:66",
            "22:33:44:55:66:77"
        },
        .idx = 0,
        .fqdn_op_exists = true,
        .fqdn_op = "in",
        .fqdns_len = 7,
        .fqdns =
        {
            "www.cnn.com",
            "www.google.com",
            "test-host.com",
            "signal",
            "localhost",
            "www.whitehouse.info",
            "adult.com",
        },
        .fqdncat_op_exists = false,
        .risk_op_exists = false,
        .ipaddr_op_exists = false,
        .ipaddrs_len = 0,
        .action_exists = true,
        .action = "gatekeeper",
        .other_config_len = 2,
        .other_config_keys = {"tagv4_name", "tagv6_name",},
        .other_config = { "my_v4_tag", "my_v6_tag"},
    },
};

bool
dummy_gatekeeper_get_verdict(struct fsm_policy_req *req,
                             struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;

    url_reply = CALLOC(1, sizeof(*url_reply));

    req_info->reply = url_reply;

    url_reply->service_id = URL_GK_SVC;

    policy_reply->categorized = FSM_FQDN_CAT_SUCCESS;

    if (strcmp(req_info->url, "adult.com") == 0)
    {
        policy_reply->action = FSM_REDIRECT;
        policy_reply->redirect = true;
        STRSCPY(policy_reply->redirects[0], "A-18.204.152.241");
        STRSCPY(policy_reply->redirects[1], "4A-1:2::3");
    }
    else
    {
        policy_reply->action = FSM_ALLOW;
    }

    req_info->reply->gk.gk_policy = strdup("gk_policy");
    req_info->reply->gk.confidence_level = 90;
    req_info->reply->gk.category_id = 2;

    return true;
}

union fsm_plugin_ops p_ops;

struct fsm_web_cat_ops g_plugin_ops1 =
{
    .categories_check = NULL,
    .risk_level_check = NULL,
    .cat2str = NULL,
    .get_stats = NULL,
    .dns_response = NULL,
    .gatekeeper_req = dummy_gatekeeper_get_verdict,
};

void
test_redirected_flow_v6(void)
{
    struct ip2action_req *ip_cache_req;
    struct net_md_flow_info brk_info;
    struct net_md_flow_info info;
    uint32_t cache_v6_ip[4] = { 0 };
    int rc;

    cache_v6_ip[0] = 0x01020304;
    cache_v6_ip[1] = 0x06060606;
    cache_v6_ip[2] = 0x06060606;
    cache_v6_ip[3] = 0x06060606;

    ip_cache_req = CALLOC(sizeof(struct ip2action_req), 1);
    TEST_ASSERT_NOT_NULL(ip_cache_req);
    ip_cache_req->ip_addr = CALLOC(1, sizeof(struct sockaddr_storage));
    TEST_ASSERT_NOT_NULL(ip_cache_req->ip_addr);
    ip_cache_req->device_mac = str2os_mac("01:02:03:04:05:06");

    sockaddr_storage_populate(AF_INET6, &cache_v6_ip, ip_cache_req->ip_addr);
    ip_cache_req->action = FSM_REDIRECT;
    ip_cache_req->cache_ttl = 500;
    ip_cache_req->redirect_flag = true;
    ip_cache_req->service_id = IP2ACTION_GK_SVC;
    ip_cache_req->direction = NET_MD_ACC_OUTBOUND_DIR;

    rc = dns_cache_add_entry(ip_cache_req);
    TEST_ASSERT_TRUE(rc);

    /* Test corner cases */
    rc = dpi_sni_is_redirected_flow(NULL);
    TEST_ASSERT_FALSE(rc);

    brk_info.local_mac = NULL;
    brk_info.remote_ip = NULL;
    rc = dpi_sni_is_redirected_flow(&brk_info);
    TEST_ASSERT_FALSE(rc);

    brk_info.local_mac = &g_src_mac;
    rc = dpi_sni_is_redirected_flow(&brk_info);
    TEST_ASSERT_FALSE(rc);

    /* Now try with the real thing */
    info.local_mac = &g_src_mac;
    info.remote_ip = CALLOC(1, 16);
    TEST_ASSERT_NOT_NULL(info.remote_ip);
    rc = inet_pton(AF_INET6, "403:201:606:606:606:606:606:606", info.remote_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);
    info.ip_version = 6;
    info.direction = NET_MD_ACC_OUTBOUND_DIR;

    /* ip address present in cache */
    rc = dpi_sni_is_redirected_flow(&info);
    TEST_ASSERT_TRUE(rc);

    FREE(ip_cache_req->ip_addr);
    FREE(ip_cache_req->device_mac);
    FREE(ip_cache_req);
    FREE(info.remote_ip);
}

void
test_redirect_cache(void)
{
    struct dns_cache_param param;
    struct fqdn_pending_req fqdn_req;
    struct fsm_url_request req_info;
    struct sockaddr_storage ipaddr;
    struct fsm_url_reply reply;
    uint32_t cache_v6_ip[4] = { 0 };
    uint8_t *cache_ipv4;
    bool rc;

    cache_ipv4 = CALLOC(1, 4);
    TEST_ASSERT_NOT_NULL(cache_ipv4);

    memset(&fqdn_req, 0, sizeof(fqdn_req));
    memset(&req_info, 0, sizeof(req_info));
    memset(&reply, 0, sizeof(reply));

    inet_pton(AF_INET, "1.2.3.4", cache_ipv4);
    sockaddr_storage_populate(AF_INET, cache_ipv4, &ipaddr);

    fqdn_req.dev_id = g_src_mac;
    reply.service_id = IP2ACTION_GK_SVC;
    req_info.reply = &reply;
    fqdn_req.req_info = &req_info;

    param.req = &fqdn_req;
    param.ipaddr = &ipaddr;
    rc = fsm_dns_cache_add_redirect_entry(&param);
    TEST_ASSERT_TRUE(rc);
    dns_cache_cleanup();

    reply.service_id = IP2ACTION_BC_SVC;
    rc = fsm_dns_cache_add_redirect_entry(&param);
    TEST_ASSERT_TRUE(rc);
    dns_cache_cleanup();

    reply.service_id = IP2ACTION_BC_SVC;
    rc = fsm_dns_cache_add_redirect_entry(&param);
    TEST_ASSERT_TRUE(rc);
    dns_cache_cleanup();

    cache_v6_ip[0] = 0x01020304;
    cache_v6_ip[1] = 0x06060606;
    cache_v6_ip[2] = 0x06060606;
    cache_v6_ip[3] = 0x06060606;

    sockaddr_storage_populate(AF_INET6, &cache_v6_ip, &ipaddr);
    reply.service_id = IP2ACTION_GK_SVC;
    req_info.reply = &reply;
    fqdn_req.req_info = &req_info;

    rc = fsm_dns_cache_add_redirect_entry(&param);
    TEST_ASSERT_TRUE(rc);
    dns_cache_cleanup();

    reply.service_id = IP2ACTION_BC_SVC;
    rc = fsm_dns_cache_add_redirect_entry(&param);
    TEST_ASSERT_TRUE(rc);
    dns_cache_cleanup();

    reply.service_id = IP2ACTION_BC_SVC;
    rc = fsm_dns_cache_add_redirect_entry(&param);
    TEST_ASSERT_TRUE(rc);
    dns_cache_cleanup();

    FREE(cache_ipv4);
}

void
test_redirected_flow_gatekeeper_cache(void)
{
    struct gk_attr_cache_interface entry;
    struct sockaddr_storage ipaddr;
    struct net_md_flow_info info;
    char buf[128] = { 0 };
    uint8_t *cache_ip;
    bool rc;
    int ret;

    info.local_mac = &g_src_mac;
    info.ip_version = 4;
    info.remote_ip = CALLOC(1, 4);
    TEST_ASSERT_NOT_NULL(info.remote_ip);
    cache_ip = CALLOC(1, 4);
    TEST_ASSERT_NOT_NULL(cache_ip);
    inet_pton(AF_INET, "1.2.3.4", info.remote_ip);
    info.direction = NET_MD_ACC_OUTBOUND_DIR;

    dns_cache_disable();
    rc = is_dns_cache_disabled();
    TEST_ASSERT_TRUE(rc);

    /* Init gatekeeper cache */
    gk_cache_init(CONFIG_GATEKEEPER_CACHE_LRU_SIZE);

    inet_pton(AF_INET, "1.2.3.4", cache_ip);
    sockaddr_storage_populate(AF_INET, cache_ip, &ipaddr);
    MEMZERO(entry);

    entry.device_mac = &g_src_mac;
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry.ip_addr = &ipaddr;
    entry.cache_ttl = DNS_REDIRECT_TTL;
    entry.action = FSM_ALLOW;
    entry.direction = NET_MD_ACC_OUTBOUND_DIR;
    entry.redirect_flag = true;
    entry.category_id = 15; /* GK_NOT_RATED */
    entry.confidence_level = 0;
    entry.categorized = FSM_FQDN_CAT_SUCCESS;
    entry.is_private_ip = false;
    rc = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(rc);

    rc = dpi_sni_is_redirected_flow(&info);
    TEST_ASSERT_TRUE(rc);

    ret = gkc_del_attribute(&entry);
    TEST_ASSERT_TRUE(ret);

    FREE(info.remote_ip);
    FREE(cache_ip);

    /* v6 addr */
    uint32_t cache_v6_ip[4] = { 0 };
    cache_v6_ip[0] = 0x01020304;
    cache_v6_ip[1] = 0x06060606;
    cache_v6_ip[2] = 0x06060606;
    cache_v6_ip[3] = 0x06060606;

    sockaddr_storage_populate(AF_INET6, &cache_v6_ip, &ipaddr);
    MEMZERO(entry);

    entry.device_mac = &g_src_mac;
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    entry.ip_addr = &ipaddr;
    entry.cache_ttl = DNS_REDIRECT_TTL;
    entry.action = FSM_ALLOW;
    entry.direction = NET_MD_ACC_OUTBOUND_DIR;
    entry.redirect_flag = true;
    entry.category_id = 15; /* GK_NOT_RATED */
    entry.confidence_level = 0;
    entry.categorized = FSM_FQDN_CAT_SUCCESS;
    entry.is_private_ip = false;
    rc = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(rc);

    inet_ntop(AF_INET6, cache_v6_ip, buf, sizeof(buf));

    MEMZERO(info);
    info.local_mac = &g_src_mac;
    info.ip_version = 6;
    info.remote_ip = CALLOC(1, 16);
    TEST_ASSERT_NOT_NULL(info.remote_ip);
    inet_pton(AF_INET6, buf, info.remote_ip);
    info.direction = NET_MD_ACC_OUTBOUND_DIR;

    rc = dpi_sni_is_redirected_flow(&info);
    TEST_ASSERT_TRUE(rc);
    FREE(info.remote_ip);

    ret = gkc_del_attribute(&entry);
    TEST_ASSERT_TRUE(ret);
}

static char *
test_session_name(struct fsm_policy_client *client)
{
    if (client->name != NULL) return client->name;

    return "gatekeeper_p";
}

void
test_redirected_flow_attr(void)
{
    struct fsm_dpi_sni_redirect_flow_request request;
    struct net_md_stats_accumulator acc;
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy_client *client;
    struct fsm_policy_session *mgr;
    struct net_md_flow_info info;
    struct fsm_session *session;
    struct policy_table *table;
    struct net_md_flow_key key;
    struct fsm_policy *fpolicy;
    char val[255];
    bool rc;


    session = &global_sessions[0];
    session->conf = &global_confs[0];
    session->provider_ops = &g_plugin_ops1;
    session->p_ops = &p_ops;
    session->name = global_confs[0].handler;
    session->service = session;

    memset(&acc, 0, sizeof(acc));
    memset(&key, 0, sizeof(key));

    acc.key = &key;
    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;
    key.ip_version = 4;
    key.smac = &g_src_mac;
    key.dst_ip = CALLOC(1, 4);
    inet_pton(AF_INET, "18.204.152.241", key.dst_ip);

    MEMZERO(info);
    rc = net_md_get_flow_info(&acc, &info);

    STRSCPY(val, "http://adult.com/show");
    request.acc = &acc;
    request.session = session;
    request.info = &info;
    request.attribute_value = val;
    request.req_type = FSM_URL_REQ;

    mgr = fsm_policy_get_mgr();
    if (!mgr->initialized) fsm_init_manager();

    /* add fsm policy */
    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    client = &session->policy_client;
    client->session = session;
    client->update_client = NULL;
    client->session_name = test_session_name;
    client->name = "gatekeeper_p";
    fsm_policy_register_client(&session->policy_client);

    rc = dpi_sni_is_redirected_attr(&request);
    TEST_ASSERT_TRUE(rc);

    fsm_policy_deregister_client(client);
    TEST_ASSERT_NULL(client->table);

    FREE(info.remote_ip);
}

void
test_redirected_flow_attr_v6(void)
{
    struct fsm_dpi_sni_redirect_flow_request request;
    struct net_md_stats_accumulator acc;
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy_client *client;
    struct fsm_policy_session *mgr;
    struct net_md_flow_info info;
    struct fsm_session *session;
    struct net_md_flow_key key;
    struct policy_table *table;
    struct fsm_policy *fpolicy;
    char val[255];
    bool rc;


    session = &global_sessions[0];
    session->conf = &global_confs[0];
    session->provider_ops = &g_plugin_ops1;
    session->p_ops = &p_ops;
    session->name = global_confs[0].handler;
    session->service = session;

    memset(&acc, 0, sizeof(acc));
    memset(&key, 0, sizeof(key));

    acc.key = &key;
    acc.direction = NET_MD_ACC_OUTBOUND_DIR;
    acc.originator = NET_MD_ACC_ORIGINATOR_SRC;
    key.ip_version = 6;
    key.smac = &g_src_mac;
    key.dst_ip = CALLOC(1, 16);
    inet_pton(AF_INET6, "1:2::3", key.dst_ip);

    MEMZERO(info);
    rc = net_md_get_flow_info(&acc, &info);

    STRSCPY(val, "http://adult.com/show");
    request.acc = &acc;
    request.session = session;
    request.info = &info;
    request.attribute_value = val;
    request.req_type = FSM_URL_REQ;

    mgr = fsm_policy_get_mgr();
    if (!mgr->initialized) fsm_init_manager();

    /* add fsm policy */
    spolicy = &spolicies[0];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);

    TEST_ASSERT_NOT_NULL(fpolicy);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(spolicy->name, fpolicy->rule_name);

    mgr = fsm_policy_get_mgr();
    table = ds_tree_find(&mgr->policy_tables, spolicy->policy);
    TEST_ASSERT_NOT_NULL(table);

    client = &session->policy_client;
    client->session = session;
    client->update_client = NULL;
    client->session_name = test_session_name;
    client->name = "gatekeeper_p";
    fsm_policy_register_client(&session->policy_client);

    rc = dpi_sni_is_redirected_attr(&request);
    TEST_ASSERT_TRUE(rc);

    fsm_policy_deregister_client(client);
    TEST_ASSERT_NULL(client->table);

    FREE(info.remote_ip);
}

void
test_url_to_fqdn_regex_extract(void)
{
    char fqdn_val[C_FQDN_LEN];
    char *attribute_name;
    bool rc;

    attribute_name  = "http://adult.com";
    rc = dpi_sni_fetch_fqdn_from_url_attr(attribute_name, fqdn_val, sizeof(fqdn_val));
    if (rc) TEST_ASSERT_EQUAL_STRING("adult.com", fqdn_val);

    MEMZERO(fqdn_val);
    attribute_name  = "http://pornhub.com/show";
    rc = dpi_sni_fetch_fqdn_from_url_attr(attribute_name, fqdn_val, sizeof(fqdn_val));
    if (rc) TEST_ASSERT_EQUAL_STRING("pornhub.com", fqdn_val);

    MEMZERO(fqdn_val);
    attribute_name  = "http://google.com";
    rc = dpi_sni_fetch_fqdn_from_url_attr(attribute_name, fqdn_val, sizeof(fqdn_val));
    if (rc) TEST_ASSERT_EQUAL_STRING("google.com", fqdn_val);

    MEMZERO(fqdn_val);
    attribute_name  = "http://cplusplus.com";
    rc = dpi_sni_fetch_fqdn_from_url_attr(attribute_name, fqdn_val, sizeof(fqdn_val));
    if (rc) TEST_ASSERT_EQUAL_STRING("cplusplus.com", fqdn_val);

    MEMZERO(fqdn_val);
    attribute_name  = "http://www.soccer.com/show";
    rc = dpi_sni_fetch_fqdn_from_url_attr(attribute_name, fqdn_val, sizeof(fqdn_val));
    if (rc) TEST_ASSERT_EQUAL_STRING("www.soccer.com", fqdn_val);

    MEMZERO(fqdn_val);
    attribute_name  = "http://xadultadult.com";
    rc = dpi_sni_fetch_fqdn_from_url_attr(attribute_name, fqdn_val, sizeof(fqdn_val));
    if (rc) TEST_ASSERT_EQUAL_STRING("xadultadult.com", fqdn_val);

    MEMZERO(fqdn_val);
    attribute_name  = "http://liberation.fr";
    rc = dpi_sni_fetch_fqdn_from_url_attr(attribute_name, fqdn_val, sizeof(fqdn_val));
    if (rc) TEST_ASSERT_EQUAL_STRING("liberation.fr", fqdn_val);
}

void
test_redirected_flow(void)
{
    struct ip2action_req ip_cache_req;
    struct sockaddr_storage ipaddr;
    struct net_md_flow_info info;
    uint8_t *cache_ip;
    bool rc;

    MEMZERO(info);
    info.local_mac = &g_src_mac;
    info.ip_version = 4;
    info.remote_ip = CALLOC(1, 4);
    TEST_ASSERT_NOT_NULL(info.remote_ip);
    cache_ip = CALLOC(1, 4);
    TEST_ASSERT_NOT_NULL(cache_ip);
    inet_pton(AF_INET, "1.2.3.4", info.remote_ip);

    /* flow should not be checked */
    rc = dpi_sni_is_redirected_flow(&info);
    TEST_ASSERT_FALSE(rc);

    /* IP address present in cache. */
    inet_pton(AF_INET, "1.2.3.4", cache_ip);
    sockaddr_storage_populate(AF_INET, cache_ip, &ipaddr);
    memset(&ip_cache_req, 0, sizeof(ip_cache_req));
    ip_cache_req.device_mac = &g_src_mac;
    ip_cache_req.ip_addr = &ipaddr;
    ip_cache_req.service_id = URL_GK_SVC;
    ip_cache_req.action = FSM_REDIRECT;
    ip_cache_req.redirect_flag = true;
    ip_cache_req.cache_ttl = 1000;
    ip_cache_req.cache_info.gk_info.gk_policy = "test";
    rc = dns_cache_add_entry(&ip_cache_req);
    TEST_ASSERT_EQUAL_INT(1, rc);

    rc = dpi_sni_is_redirected_flow(&info);
    TEST_ASSERT_TRUE(rc);

    FREE(info.remote_ip);
    FREE(cache_ip);
}


void
run_test_plugin(void)
{
    RUN_TEST(test_redirect_cache);
    RUN_TEST(test_redirected_flow);
    RUN_TEST(test_redirected_flow_gatekeeper_cache);
    RUN_TEST(test_redirected_flow_v6);
    RUN_TEST(test_redirected_flow_attr);
    RUN_TEST(test_redirected_flow_attr_v6);
    RUN_TEST(test_url_to_fqdn_regex_extract);
}
