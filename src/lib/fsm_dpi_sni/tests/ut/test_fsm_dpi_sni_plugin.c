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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include "dns_cache.h"
#include "dns_parse.h"
#include "fsm_dns_utils.h"
#include "fsm_dpi_sni.h"
#include "gatekeeper_cache.h"
#include "memutil.h"
#include "sockaddr_storage.h"
#include "unity.h"

os_macaddr_t g_src_mac =
{
    .addr = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 },
};

void
test_redirected_flow_v6(void)
{
    struct ip2action_req *ip_cache_req;
    struct net_md_flow_info brk_info;
    struct net_md_flow_info info;
    uint32_t cache_v6_ip[4] = { 0 };
    char *attr;
    int rc;

    cache_v6_ip[0] = 0x01020304;
    cache_v6_ip[1] = 0x06060606;
    cache_v6_ip[2] = 0x06060606;
    cache_v6_ip[3] = 0x06060606;

    ip_cache_req = CALLOC(sizeof(struct ip2action_req), 1);
    TEST_ASSERT_NOT_NULL(ip_cache_req);
    ip_cache_req->ip_addr = CALLOC(1, sizeof(struct sockaddr_storage));
    TEST_ASSERT_NOT_NULL(ip_cache_req->ip_addr);
    ip_cache_req->device_mac = CALLOC(1, sizeof(os_macaddr_t));
    TEST_ASSERT_NOT_NULL(ip_cache_req->device_mac);

    sockaddr_storage_populate(AF_INET6, &cache_v6_ip, ip_cache_req->ip_addr);
    ip_cache_req->device_mac->addr[0] = 0x01;
    ip_cache_req->device_mac->addr[1] = 0x02;
    ip_cache_req->device_mac->addr[2] = 0x03;
    ip_cache_req->device_mac->addr[3] = 0x04;
    ip_cache_req->device_mac->addr[4] = 0x05;
    ip_cache_req->device_mac->addr[5] = 0x06;
    ip_cache_req->action = FSM_REDIRECT;
    ip_cache_req->cache_ttl = 500;
    ip_cache_req->redirect_flag = true;
    ip_cache_req->service_id = IP2ACTION_GK_SVC;

    rc = dns_cache_add_entry(ip_cache_req);
    TEST_ASSERT_TRUE(rc);

    /* Test corner cases */
    attr = NULL;
    rc = is_redirected_flow(NULL, attr);
    TEST_ASSERT_FALSE(rc);
    brk_info.local_mac = NULL;
    brk_info.remote_ip = NULL;
    rc = is_redirected_flow(&brk_info, attr);
    TEST_ASSERT_FALSE(rc);

    brk_info.local_mac = &g_src_mac;
    rc = is_redirected_flow(&brk_info, attr);
    TEST_ASSERT_FALSE(rc);

    /* Now try with the real thing */
    info.local_mac = &g_src_mac;
    info.remote_ip = CALLOC(1, 16);
    TEST_ASSERT_NOT_NULL(info.remote_ip);
    rc = inet_pton(AF_INET6, "403:201:606:606:606:606:606:606", info.remote_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);
    info.ip_version = 6;

    attr = "http.host";
    /* ip address present in cache */
    rc = is_redirected_flow(&info, attr);
    TEST_ASSERT_EQUAL_INT(1, rc);

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

    reply.service_id = IP2ACTION_WP_SVC;
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

    reply.service_id = IP2ACTION_WP_SVC;
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
    char *attr;
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
    gk_cache_init();

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

    attr = "http.url";
    rc = is_redirected_flow(&info, attr);
    TEST_ASSERT_EQUAL_INT(1, rc);

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

    attr = "http.url";
    rc = is_redirected_flow(&info, attr);
    FREE(info.remote_ip);
    TEST_ASSERT_EQUAL_INT(1, rc);

    ret = gkc_del_attribute(&entry);
    TEST_ASSERT_TRUE(ret);

}

void
test_redirected_flow_dns_cache(void)
{
    struct ip2action_req ip_cache_req;
    struct sockaddr_storage ipaddr;
    struct net_md_flow_info info;
    uint8_t *cache_ip;
    char *attr;
    bool rc;

    info.local_mac = &g_src_mac;
    info.ip_version = 4;
    info.remote_ip = CALLOC(1, 4);
    TEST_ASSERT_NOT_NULL(info.remote_ip);
    cache_ip = CALLOC(1, 4);
    TEST_ASSERT_NOT_NULL(cache_ip);
    inet_pton(AF_INET, "1.2.3.4", info.remote_ip);

    /* flow should not be checked for tls.sni */
    attr = "tls.sni";
    rc = is_redirected_flow(&info, attr);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* flow should not be checked for tag */
    attr = "tag";
    rc = is_redirected_flow(&info, attr);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* valid attribute, IP not present in cache. */
    attr = "http.host";
    rc = is_redirected_flow(&info, attr);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* IP address present in cache. */
    inet_pton(AF_INET, "1.2.3.4", cache_ip);
    sockaddr_storage_populate(AF_INET, cache_ip, &ipaddr);
    memset(&ip_cache_req, 0, sizeof(ip_cache_req));
    ip_cache_req.device_mac = &g_src_mac;
    ip_cache_req.ip_addr = &ipaddr;
    ip_cache_req.service_id = 2;
    ip_cache_req.action = FSM_REDIRECT;
    ip_cache_req.redirect_flag = true;
    ip_cache_req.cache_ttl = 1000;
    ip_cache_req.cache_info.gk_info.gk_policy = "test";
    rc = dns_cache_add_entry(&ip_cache_req);
    TEST_ASSERT_EQUAL_INT(1, rc);
    attr = "http.host";
    rc = is_redirected_flow(&info, attr);
    TEST_ASSERT_EQUAL_INT(1, rc);

    FREE(info.remote_ip);
    FREE(cache_ip);
}


void
run_test_plugin(void)
{
    RUN_TEST(test_redirect_cache);
    RUN_TEST(test_redirected_flow_dns_cache);
    RUN_TEST(test_redirected_flow_gatekeeper_cache);
    RUN_TEST(test_redirected_flow_v6);
}
