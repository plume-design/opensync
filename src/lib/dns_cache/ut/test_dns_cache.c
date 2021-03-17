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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <net/if.h>

#include "log.h"
#include "ovsdb.h"
#include "os.h"
#include "os_types.h"
#include "target.h"
#include "unity.h"
#include "schema.h"
#include "dns_cache.h"
#include "ds_tree.h"
#include "fsm_policy.h"


const char *test_name = "dns_cache_tests";

// v4 entries.
struct ip2action_req *entry1;
struct ip2action_req *entry2;
struct ip2action_req *entry5;
// v6 entries.
struct ip2action_req *entry3;
struct ip2action_req *entry4;
//cache entries
struct ip2action_req *entry6;
struct ip2action_req *entry7;
struct ip2action_req *entry8;

struct test_timers
{
    ev_timer timeout_watcher_add;           /* Add entries */
    ev_timer timeout_watcher_add_cache;     /* Validate added entries */
    ev_timer timeout_watcher_delete;        /* Delete entries */
    ev_timer timeout_watcher_delete_cache;  /* Validate deleted entries */
    ev_timer timeout_watcher_update;        /* Update entries */
    ev_timer timeout_watcher_update_cache;  /* Validate updated entries */
};

struct test_mgr
{
    struct ev_loop *loop;
    ev_timer timeout_watcher;
    bool expected;
    struct test_timers system;
    struct test_timers dns_cache_add;
    struct test_timers ipv4_cache_timers;
    struct test_timers ipv6_cache_timers;
    double g_timeout;
} g_test_mgr;

/**
 * @brief breaks the ev loop to terminate a test
 */
static void
timeout_cb(EV_P_ ev_timer *w, int revents)
{
    ev_break(EV_A_ EVBREAK_ONE);
}

int dns_cache_ev_test_setup(double timeout)
{
    ev_timer *p_timeout_watcher;

    /* Set up the timer killing the ev loop, indicating the end of the test */
    p_timeout_watcher = &g_test_mgr.timeout_watcher;

    ev_timer_init(p_timeout_watcher, timeout_cb, timeout, 0.);
    ev_timer_start(g_test_mgr.loop, p_timeout_watcher);

    return 0;
}

void dns_cache_global_test_setup(void)
{

    g_test_mgr.loop = EV_DEFAULT;
    g_test_mgr.g_timeout = 1.0;

    dns_cache_init();
}

void dns_cache_global_test_teardown(void)
{
    dns_cache_cleanup_mgr();
}

void util_populate_sockaddr(int af, void *ip, struct sockaddr_storage *dst)
{
    if (af == AF_INET)
    {
        struct sockaddr_in *in4 = (struct sockaddr_in *)dst;

        memset(in4, 0, sizeof(struct sockaddr_in));
        in4->sin_family = af;
        memcpy(&in4->sin_addr, ip, sizeof(in4->sin_addr));
    } else if (af == AF_INET6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)dst;

        memset(in6, 0, sizeof(struct sockaddr_in6));
        in6->sin6_family = af;
        memcpy(&in6->sin6_addr, ip, sizeof(in6->sin6_addr));
    }
    return;
}

void setUp(void)
{
    uint32_t v4dstip1 = htonl(0x04030201);
    uint32_t v4dstip2 = htonl(0x04030202);
    uint32_t v4dstip5 = htonl(0x04030205);
    uint32_t v4dstip6 = htonl(0x04030206);
    uint32_t v4dstip7 = htonl(0x04030207);
    uint32_t v4dstip8 = htonl(0x04030208);

    uint32_t v6dstip1[4] = {0};
    uint32_t v6dstip2[4] = {0};

    v6dstip1[0] = 0x06060606;
    v6dstip1[1] = 0x06060606;
    v6dstip1[2] = 0x06060606;
    v6dstip1[3] = 0x06060606;


    v6dstip2[0] = 0x07070707;
    v6dstip2[1] = 0x07070707;
    v6dstip2[2] = 0x07070707;
    v6dstip2[3] = 0x07070707;

    entry1 = calloc(sizeof(struct ip2action_req), 1);
    entry1->ip_addr = calloc(sizeof(struct sockaddr_storage), 1);
    entry1->device_mac = calloc(sizeof(os_macaddr_t), 1);
    util_populate_sockaddr(AF_INET, &v4dstip1, entry1->ip_addr);
    entry1->device_mac->addr[0] = 0xaa;
    entry1->device_mac->addr[1] = 0xaa;
    entry1->device_mac->addr[2] = 0xaa;
    entry1->device_mac->addr[3] = 0xaa;
    entry1->device_mac->addr[4] = 0xaa;
    entry1->device_mac->addr[5] = 0x01;
    entry1->action              = FSM_BLOCK;
    entry1->cache_ttl           = 600;
    entry1->policy_idx          = 2;

    entry2 = calloc(sizeof(struct ip2action_req), 1);
    entry2->ip_addr = calloc(1, sizeof(struct sockaddr_storage));
    entry2->device_mac = calloc(1, sizeof(os_macaddr_t));
    util_populate_sockaddr(AF_INET, &v4dstip2, entry2->ip_addr);
    entry2->device_mac->addr[0] = 0xaa;
    entry2->device_mac->addr[1] = 0xaa;
    entry2->device_mac->addr[2] = 0xaa;
    entry2->device_mac->addr[3] = 0xaa;
    entry2->device_mac->addr[4] = 0xaa;
    entry2->device_mac->addr[5] = 0x02;
    entry2->action              = FSM_ALLOW;
    entry2->cache_ttl           = 550;

    entry3 = calloc(sizeof(struct ip2action_req), 1);
    entry3->ip_addr = calloc(1, sizeof(struct sockaddr_storage));
    entry3->device_mac = calloc(1, sizeof(os_macaddr_t));
    util_populate_sockaddr(AF_INET6, &v6dstip1, entry3->ip_addr);
    entry3->device_mac->addr[0] = 0x66;
    entry3->device_mac->addr[1] = 0x66;
    entry3->device_mac->addr[2] = 0x66;
    entry3->device_mac->addr[3] = 0x66;
    entry3->device_mac->addr[4] = 0x66;
    entry3->device_mac->addr[5] = 0x01;
    entry3->action              = FSM_OBSERVED;
    entry3->cache_ttl           = 500;

    entry4 = calloc(sizeof(struct ip2action_req), 1);
    entry4->ip_addr = calloc(1, sizeof(struct sockaddr_storage));
    entry4->device_mac = calloc(1, sizeof(os_macaddr_t));
    util_populate_sockaddr(AF_INET6, &v6dstip2, entry4->ip_addr);
    entry4->device_mac->addr[0] = 0x77;
    entry4->device_mac->addr[1] = 0x77;
    entry4->device_mac->addr[2] = 0x77;
    entry4->device_mac->addr[3] = 0x77;
    entry4->device_mac->addr[4] = 0x77;
    entry4->device_mac->addr[5] = 0x02;
    entry4->action              = FSM_REDIRECT;
    entry4->cache_ttl           = 400;

    entry5 = calloc(sizeof(struct ip2action_req), 1);
    entry5->ip_addr = calloc(sizeof(struct sockaddr_storage), 1);
    entry5->device_mac = calloc(sizeof(os_macaddr_t), 1);
    util_populate_sockaddr(AF_INET, &v4dstip5, entry5->ip_addr);
    entry5->device_mac->addr[0] = 0xaa;
    entry5->device_mac->addr[1] = 0xaa;
    entry5->device_mac->addr[2] = 0xaa;
    entry5->device_mac->addr[3] = 0xaa;
    entry5->device_mac->addr[4] = 0xaa;
    entry5->device_mac->addr[5] = 0x05;
    entry5->action              = FSM_FORWARD;
    entry4->cache_ttl           = 800;

    entry6 = calloc(sizeof(struct ip2action_req), 1);
    entry6->ip_addr = calloc(sizeof(struct sockaddr_storage), 1);
    entry6->device_mac = calloc(sizeof(os_macaddr_t), 1);
    util_populate_sockaddr(AF_INET, &v4dstip6, entry6->ip_addr);
    entry6->device_mac->addr[0] = 0xaa;
    entry6->device_mac->addr[1] = 0xaa;
    entry6->device_mac->addr[2] = 0xaa;
    entry6->device_mac->addr[3] = 0xaa;
    entry6->device_mac->addr[4] = 0xaa;
    entry6->device_mac->addr[5] = 0x06;
    entry6->action              = FSM_BLOCK;
    entry6->cache_ttl           = 6;
    entry6->policy_idx          = 6;
    entry6->service_id          = 0;
    entry6->nelems              = 1;
    entry6->categories[0]       = 6;
    entry6->cache_bc.confidence_levels[0]    = 6;
    entry6->cache_bc.reputation              = 6;

    entry7 = calloc(sizeof(struct ip2action_req), 1);
    entry7->ip_addr = calloc(sizeof(struct sockaddr_storage), 1);
    entry7->device_mac = calloc(sizeof(os_macaddr_t), 1);
    util_populate_sockaddr(AF_INET, &v4dstip7, entry7->ip_addr);
    entry7->device_mac->addr[0] = 0xaa;
    entry7->device_mac->addr[1] = 0xaa;
    entry7->device_mac->addr[2] = 0xaa;
    entry7->device_mac->addr[3] = 0xaa;
    entry7->device_mac->addr[4] = 0xaa;
    entry7->device_mac->addr[5] = 0x07;
    entry7->action              = FSM_ALLOW;
    entry7->cache_ttl           = 7;
    entry7->policy_idx          = 7;
    entry7->service_id          = 1;
    entry7->nelems              = 1;
    entry7->categories[0]       = 7;
    entry7->cache_wb.risk_level = 7;

    entry8 = calloc(sizeof(struct ip2action_req), 1);
    entry8->ip_addr = calloc(sizeof(struct sockaddr_storage), 1);
    entry8->device_mac = calloc(sizeof(os_macaddr_t), 1);
    util_populate_sockaddr(AF_INET, &v4dstip8, entry8->ip_addr);
    entry8->device_mac->addr[0] = 0xaa;
    entry8->device_mac->addr[1] = 0xaa;
    entry8->device_mac->addr[2] = 0xaa;
    entry8->device_mac->addr[3] = 0xaa;
    entry8->device_mac->addr[4] = 0xaa;
    entry8->device_mac->addr[5] = 0x08;
    entry8->action              = FSM_ALLOW;
    entry8->cache_ttl           = 8;
    entry8->policy_idx          = 8;
    entry8->service_id          = 2;
    entry8->nelems              = 1;
    entry8->categories[0]       = 7;
    entry8->cache_gk.confidence_level = 7;
    entry8->cache_gk.category_id = 7;
    entry8->cache_gk.gk_policy = strdup("gk_policy");
}

void free_dns_cache_entry(struct ip2action_req *req)
{
    if (!req) return;

    free(req->ip_addr);
    free(req->device_mac);
    if (req->service_id == IP2ACTION_GK_SVC)
    {
        free(req->cache_gk.gk_policy);
    }
    free(req);
}

void tearDown(void)
{
    dns_cache_cleanup();

    LOGI("Tearing down the test...");

    free_dns_cache_entry(entry1);
    free_dns_cache_entry(entry2);
    free_dns_cache_entry(entry3);
    free_dns_cache_entry(entry4);
    free_dns_cache_entry(entry5);
    free_dns_cache_entry(entry6);
    free_dns_cache_entry(entry7);
    free_dns_cache_entry(entry8);
}


void test_add_dns_cache(void)
{
    struct ip2action_req *entry = NULL;
    uint32_t v4udstip = htonl(0x04030201);
    struct ip2action_req  key;
    struct sockaddr_storage ip;
    os_macaddr_t mac;
    uint32_t v6udstip[4] = {0};
    bool rc_lookup;
    bool rc_add;
    int i;

    LOGI("\n******************** %s: starting ****************\n", __func__);
    /* Add the ip2action entry */
    entry = entry1;
    entry->service_id = 0;
    entry->cache_bc.reputation = 3;
    entry->nelems = 2;
    for (i = 0; i < entry->nelems; i++)
    {
        entry->cache_bc.confidence_levels[i] = i+1;
    }

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x01;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);
    TEST_ASSERT_EQUAL_INT(FSM_BLOCK, key.action);

    /* V6 ip test */
    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;


    /* Add the ip2action entry */
    entry = entry3;
    entry->service_id = 0;
    entry->cache_bc.reputation = 3;
    entry->nelems = 2;
    for (i = 0; i < entry->nelems; i++)
    {
        entry->cache_bc.confidence_levels[i] = i+1;
    }

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    memset(&key, 0, sizeof(struct ip2action_req));
    key.ip_addr = &ip;
    util_populate_sockaddr(AF_INET6, &v6udstip, key.ip_addr);
    mac.addr[0] = 0x66;
    mac.addr[1] = 0x66;
    mac.addr[2] = 0x66;
    mac.addr[3] = 0x66;
    mac.addr[4] = 0x66;
    mac.addr[5] = 0x01;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);
    /* Validate result of lookup to dns_cache entry */
    TEST_ASSERT_EQUAL_INT(FSM_OBSERVED, key.action);

    dns_cache_cleanup();
    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void test_del_dns_cache(void)
{
    struct ip2action_req *entry = NULL;
    struct ip2action_req  key;
    struct sockaddr_storage ip;
    os_macaddr_t mac;

    uint32_t  v4udstip = htonl(0x04030201);
    uint32_t v6udstip[4] = {0};
    bool rc_lookup = false;
    bool rc_add;
    int i;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    entry = entry1;
    entry->service_id = 0;
    entry->cache_bc.reputation = 3;
    entry->nelems = 2;
    for (i = 0; i < entry->nelems; i++)
    {
        entry->cache_bc.confidence_levels[i] = i+1;
    }

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    entry = entry2;
    entry->service_id = 1;
    entry->cache_bc.reputation = 3;
    entry->nelems = 2;
    for (i = 0; i < entry->nelems; i++)
    {
        entry->cache_bc.confidence_levels[i] = i+1;
    }
    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);
    /* Del the neighbour entry */
    entry = entry1;
    dns_cache_del_entry(entry);

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x01;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    TEST_ASSERT_FALSE(rc_lookup);

    /* V6 ip tests*/
    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;


    /* Add the neighbour entry */
    entry = entry3;
    entry->service_id = 0;
    entry->cache_bc.reputation = 3;
    entry->nelems = 2;
    for (i = 0; i < entry->nelems; i++)
    {
        entry->cache_bc.confidence_levels[i] = i+1;
    }

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Add the neighbour entry */
    entry = entry4;
    entry->service_id = 1;
    entry->cache_bc.reputation = 3;
    entry->nelems = 2;
    for (i = 0; i < entry->nelems; i++)
    {
        entry->cache_bc.confidence_levels[i] = i+1;
    }

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Del the neighbour entry */
    entry = entry3;
    dns_cache_del_entry(entry);

    memset(&key, 0, sizeof(struct ip2action_req));
    key.ip_addr = &ip;
    util_populate_sockaddr(AF_INET6, &v6udstip, key.ip_addr);
    mac.addr[0] = 0x66;
    mac.addr[1] = 0x66;
    mac.addr[2] = 0x66;
    mac.addr[3] = 0x66;
    mac.addr[4] = 0x66;
    mac.addr[5] = 0x01;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    TEST_ASSERT_FALSE(rc_lookup);

    dns_cache_cleanup();
    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void test_upd_dns_cache(void)
{
    struct ip2action_req  *entry = NULL;
    struct ip2action_req  key;
    struct sockaddr_storage ip;
    os_macaddr_t mac;

    uint32_t  v4udstip = htonl(0x04030201);
    uint32_t v6udstip[4] = {0};
    bool rc_lookup;
    bool rc_cache;
    bool rc_add;
    int i;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    entry = entry1;
    entry->service_id = 0;
    entry->cache_bc.reputation = 3;
    entry->nelems = 2;
    for (i = 0; i < entry->nelems; i++)
    {
        entry->cache_bc.confidence_levels[i] = i+1;
    }

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    entry = entry2;
    entry->service_id = 1;
    entry->cache_bc.reputation = 3;
    entry->nelems = 2;
    for (i = 0; i < entry->nelems; i++)
    {
        entry->cache_bc.confidence_levels[i] = i+1;
    }

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    print_dns_cache();
    /* Upd the neighbour entry */
    entry = entry1;
    entry->action = FSM_ALLOW;
    entry->cache_ttl = 1000;
    rc_cache = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_cache);

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x01;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);
    TEST_ASSERT_EQUAL_INT(FSM_ALLOW, key.action);
    TEST_ASSERT_EQUAL_INT(1000, key.cache_ttl);

    /* V6 test ips.*/
    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    entry = entry3;
    entry->service_id = 0;
    entry->cache_bc.reputation = 3;
    entry->nelems = 2;
    for (i = 0; i < entry->nelems; i++)
    {
        entry->cache_bc.confidence_levels[i] = i+1;
    }

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    entry = entry4;
    entry->service_id = 1;
    entry->cache_bc.reputation = 3;
    entry->nelems = 2;
    for (i = 0; i < entry->nelems; i++)
    {
        entry->cache_bc.confidence_levels[i] = i+1;
    }

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    entry = entry3;
    entry->action = FSM_ALLOW;
    entry->cache_ttl = 2000;
    rc_cache = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_cache);

    memset(&key, 0, sizeof(struct ip2action_req));
    key.ip_addr = &ip;
    util_populate_sockaddr(AF_INET6, &v6udstip, key.ip_addr);
    mac.addr[0] = 0x66;
    mac.addr[1] = 0x66;
    mac.addr[2] = 0x66;
    mac.addr[3] = 0x66;
    mac.addr[4] = 0x66;
    mac.addr[5] = 0x01;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    TEST_ASSERT_TRUE(rc_lookup);
    TEST_ASSERT_EQUAL_INT(FSM_ALLOW, key.action);
    TEST_ASSERT_EQUAL_INT(2000, key.cache_ttl);

    dns_cache_cleanup();
    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void test_dns_cache_ref_count(void)
{
    LOGI("\n******************** %s: starting ****************\n", __func__);
    uint8_t refcount = 0;
    refcount = dns_cache_get_refcount();
    TEST_ASSERT_EQUAL_INT(1, refcount);

    dns_cache_init();
    refcount = dns_cache_get_refcount();
    TEST_ASSERT_EQUAL_INT(2, refcount);

    dns_cache_cleanup_mgr();
    refcount = dns_cache_get_refcount();
    TEST_ASSERT_EQUAL_INT(1, refcount);

    dns_cache_cleanup_mgr();
    refcount = dns_cache_get_refcount();
    TEST_ASSERT_EQUAL_INT(0, refcount);

    dns_cache_cleanup_mgr();
    refcount = dns_cache_get_refcount();
    TEST_ASSERT_EQUAL_INT(0, refcount);

    dns_cache_init();
    refcount = dns_cache_get_refcount();
    TEST_ASSERT_EQUAL_INT(1, refcount);
    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void test_bc_dns_cache(void)
{
    struct ip2action_req *entry = NULL;
    uint32_t v4udstip = htonl(0x04030206);
    struct ip2action_req  key;
    struct sockaddr_storage ip;
    os_macaddr_t mac;
    bool rc_lookup;
    bool rc_add;
    bool rc;
    int nelem;

    LOGI("\n******************** %s: starting ****************\n", __func__);
    /* Add the ip2action entry */

    /* Case : missing categorization details in brightcloud service */
    entry = entry1;
    entry->service_id = 0;

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_FALSE(rc_add);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 0);

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x01;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    TEST_ASSERT_FALSE(rc_lookup);

    /* Case : available categorization details in brightcloud service */
    entry = entry6;
    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);
    print_dns_cache();

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x06;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);

    TEST_ASSERT_EQUAL_INT(FSM_BLOCK, key.action);
    TEST_ASSERT_EQUAL_INT(entry->cache_ttl, key.cache_ttl);
    TEST_ASSERT_EQUAL_INT(entry->policy_idx, key.policy_idx);
    TEST_ASSERT_EQUAL_INT(entry->service_id, key.service_id);
    TEST_ASSERT_EQUAL_INT(entry->nelems, key.nelems);
    for (nelem = 0; nelem < key.nelems; nelem++)
    {
        TEST_ASSERT_EQUAL_INT(entry->categories[nelem], key.categories[nelem]);
        TEST_ASSERT_EQUAL_INT(entry->cache_bc.confidence_levels[nelem],
                              key.cache_bc.confidence_levels[nelem]);
    }
    TEST_ASSERT_EQUAL_INT(entry->cache_bc.reputation, key.cache_bc.reputation);

    /* sleep for 1 seconds */
    sleep(1);
    rc = dns_cache_ttl_cleanup();
    TEST_ASSERT_TRUE(rc);
    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);

    /* Update TTL and action */
    entry = entry6;
    entry->action = FSM_ALLOW;
    entry->cache_ttl = 1;

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);
    print_dns_cache();

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);

    TEST_ASSERT_EQUAL_INT(FSM_ALLOW, key.action);
    TEST_ASSERT_EQUAL_INT(entry->cache_ttl, key.cache_ttl);
    TEST_ASSERT_EQUAL_INT(entry->policy_idx, key.policy_idx);
    TEST_ASSERT_EQUAL_INT(entry->service_id, key.service_id);
    TEST_ASSERT_EQUAL_INT(entry->nelems, key.nelems);
    for (nelem = 0; nelem < key.nelems; nelem++)
    {
        TEST_ASSERT_EQUAL_INT(entry->categories[nelem], key.categories[nelem]);
        TEST_ASSERT_EQUAL_INT(entry->cache_bc.confidence_levels[nelem],
                              key.cache_bc.confidence_levels[nelem]);
    }
    TEST_ASSERT_EQUAL_INT(entry->cache_bc.reputation, key.cache_bc.reputation);

    /* sleep for 1 seconds */
    sleep(1);
    rc = dns_cache_ttl_cleanup();
    TEST_ASSERT_TRUE(rc);

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_FALSE(rc_lookup);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 0);
    print_dns_cache();
    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void test_wp_dns_cache(void)
{
    struct ip2action_req *entry = NULL;
    uint32_t v4udstip = htonl(0x04030207);
    struct ip2action_req  key;
    struct sockaddr_storage ip;
    os_macaddr_t mac;
    bool rc_lookup;
    bool rc_add;
    bool rc;
    int nelem;

    LOGI("\n******************** %s: starting ****************\n", __func__);
    /* Add the ip2action entry */

    /* Case : missing categorization details in webpulse service */
    entry = entry1;
    entry->service_id = 1;

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_FALSE(rc_add);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 0);

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x01;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    TEST_ASSERT_FALSE(rc_lookup);

    /* Case : available categorization details in webpulse service */
    entry = entry7;
    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x07;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);

    TEST_ASSERT_EQUAL_INT(FSM_ALLOW, key.action);
    TEST_ASSERT_EQUAL_INT(entry->cache_ttl, key.cache_ttl);
    TEST_ASSERT_EQUAL_INT(entry->policy_idx, key.policy_idx);
    TEST_ASSERT_EQUAL_INT(entry->service_id, key.service_id);
    TEST_ASSERT_EQUAL_INT(entry->nelems, key.nelems);
    for (nelem = 0; nelem < key.nelems; nelem++)
    {
        TEST_ASSERT_EQUAL_INT(entry->categories[nelem], key.categories[nelem]);
    }
    TEST_ASSERT_EQUAL_INT(entry->cache_wb.risk_level, key.cache_wb.risk_level);

    /* sleep for 1 seconds */
    sleep(1);
    rc = dns_cache_ttl_cleanup();
    TEST_ASSERT_TRUE(rc);
    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);

    /* Update TTL and action */
    entry = entry7;
    entry->action = FSM_BLOCK;
    entry->cache_ttl = 1;

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);
    print_dns_cache();

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);

    TEST_ASSERT_EQUAL_INT(FSM_BLOCK, key.action);
    TEST_ASSERT_EQUAL_INT(entry->cache_ttl, key.cache_ttl);
    TEST_ASSERT_EQUAL_INT(entry->policy_idx, key.policy_idx);
    TEST_ASSERT_EQUAL_INT(entry->service_id, key.service_id);
    TEST_ASSERT_EQUAL_INT(entry->nelems, key.nelems);
    for (nelem = 0; nelem < key.nelems; nelem++)
    {
        TEST_ASSERT_EQUAL_INT(entry->categories[nelem], key.categories[nelem]);
    }
    TEST_ASSERT_EQUAL_INT(entry->cache_wb.risk_level, key.cache_wb.risk_level);

    /* sleep for 1 seconds */
    sleep(1);
    rc = dns_cache_ttl_cleanup();
    TEST_ASSERT_TRUE(rc);

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_FALSE(rc_lookup);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 0);
    print_dns_cache();
    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void test_gk_dns_cache(void)
{
    struct ip2action_req *entry = NULL;
    uint32_t v4udstip = htonl(0x04030208);
    struct ip2action_req  key;
    struct sockaddr_storage ip;
    os_macaddr_t mac;
    bool rc_lookup;
    bool rc_add;
    bool rc;
    int nelem;
    int ret;

    LOGI("\n******************** %s: starting ****************\n", __func__);
    /* Add the ip2action entry */

    /* Case : missing categorization details in gatekeeper service */
    entry = entry1;
    entry->service_id = 2;

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_FALSE(rc_add);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 0);

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x01;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    TEST_ASSERT_FALSE(rc_lookup);

    /* Case : available categorization details in gatekeeper service */
    entry = entry8;
    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x08;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);

    TEST_ASSERT_EQUAL_INT(FSM_ALLOW, key.action);
    TEST_ASSERT_EQUAL_INT(entry->cache_ttl, key.cache_ttl);
    TEST_ASSERT_EQUAL_INT(entry->policy_idx, key.policy_idx);
    TEST_ASSERT_EQUAL_INT(entry->service_id, key.service_id);
    TEST_ASSERT_EQUAL_INT(entry->nelems, key.nelems);
    for (nelem = 0; nelem < key.nelems; nelem++)
    {
        TEST_ASSERT_EQUAL_INT(entry->categories[nelem], key.categories[nelem]);
    }
    TEST_ASSERT_EQUAL_INT(entry->cache_gk.confidence_level, key.cache_gk.confidence_level);
    TEST_ASSERT_EQUAL_INT(entry->cache_gk.category_id, key.cache_gk.category_id);
    ret = strcmp(entry->cache_gk.gk_policy, key.cache_gk.gk_policy);
    TEST_ASSERT_EQUAL_INT(ret, 0);
    free(key.cache_gk.gk_policy);

    /* sleep for 1 seconds */
    sleep(1);
    rc = dns_cache_ttl_cleanup();
    TEST_ASSERT_TRUE(rc);
    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);

    /* Update TTL and action */
    entry = entry8;
    entry->action = FSM_BLOCK;
    entry->cache_ttl = 1;

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);
    print_dns_cache();

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);

    TEST_ASSERT_EQUAL_INT(FSM_BLOCK, key.action);
    TEST_ASSERT_EQUAL_INT(entry->cache_ttl, key.cache_ttl);
    TEST_ASSERT_EQUAL_INT(entry->policy_idx, key.policy_idx);
    TEST_ASSERT_EQUAL_INT(entry->service_id, key.service_id);
    TEST_ASSERT_EQUAL_INT(entry->nelems, key.nelems);
    for (nelem = 0; nelem < key.nelems; nelem++)
    {
        TEST_ASSERT_EQUAL_INT(entry->categories[nelem], key.categories[nelem]);
    }
    TEST_ASSERT_EQUAL_INT(entry->cache_gk.confidence_level, key.cache_gk.confidence_level);
    TEST_ASSERT_EQUAL_INT(entry->cache_gk.category_id, key.cache_gk.category_id);
    ret = strcmp(entry->cache_gk.gk_policy, key.cache_gk.gk_policy);
    TEST_ASSERT_EQUAL_INT(ret, 0);

    free(key.cache_gk.gk_policy);

    /* sleep for 1 seconds */
    sleep(1);
    rc = dns_cache_ttl_cleanup();
    TEST_ASSERT_TRUE(rc);

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_FALSE(rc_lookup);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 0);
    print_dns_cache();
    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void test_dns_cache_entries(void)
{
    struct ip2action_req *entry = NULL;
    uint32_t v4udstip6 = htonl(0x04030206);
    uint32_t v4udstip7 = htonl(0x04030207);
    uint32_t v4udstip8 = htonl(0x04030208);
    struct ip2action_req  key;
    struct sockaddr_storage ip;
    os_macaddr_t mac;
    bool rc_lookup;
    bool rc_add;
    bool rc;
    int nelem;
    int ret;

    LOGI("\n******************** %s: starting ****************\n", __func__);
    /* Add the ip2action entry */
    entry = entry6;
    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);

    entry = entry7;
    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 2);

    entry = entry8;
    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 3);
    print_dns_cache();


    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip8, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x08;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);

    TEST_ASSERT_EQUAL_INT(FSM_ALLOW, key.action);
    TEST_ASSERT_EQUAL_INT(entry->cache_ttl, key.cache_ttl);
    TEST_ASSERT_EQUAL_INT(entry->policy_idx, key.policy_idx);
    TEST_ASSERT_EQUAL_INT(entry->service_id, key.service_id);
    TEST_ASSERT_EQUAL_INT(entry->nelems, key.nelems);
    for (nelem = 0; nelem < key.nelems; nelem++)
    {
        TEST_ASSERT_EQUAL_INT(entry->categories[nelem], key.categories[nelem]);
    }
    TEST_ASSERT_EQUAL_INT(entry->cache_gk.confidence_level, key.cache_gk.confidence_level);
    TEST_ASSERT_EQUAL_INT(entry->cache_gk.category_id, key.cache_gk.category_id);
    ret = strcmp(entry->cache_gk.gk_policy, key.cache_gk.gk_policy);
    TEST_ASSERT_EQUAL_INT(ret, 0);
    free(key.cache_gk.gk_policy);

    /* sleep for 1 seconds */
    sleep(1);
    rc = dns_cache_ttl_cleanup();
    TEST_ASSERT_TRUE(rc);
    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 3);

    /* Update TTL and action */
    entry = entry8;
    entry->action = FSM_BLOCK;
    entry->cache_ttl = 1;

    rc_add = dns_cache_add_entry(entry);
    TEST_ASSERT_TRUE(rc_add);
    print_dns_cache();

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 3);

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);

    TEST_ASSERT_EQUAL_INT(FSM_BLOCK, key.action);
    TEST_ASSERT_EQUAL_INT(entry->cache_ttl, key.cache_ttl);
    TEST_ASSERT_EQUAL_INT(entry->policy_idx, key.policy_idx);
    TEST_ASSERT_EQUAL_INT(entry->service_id, key.service_id);
    TEST_ASSERT_EQUAL_INT(entry->nelems, key.nelems);
    for (nelem = 0; nelem < key.nelems; nelem++)
    {
        TEST_ASSERT_EQUAL_INT(entry->categories[nelem], key.categories[nelem]);
    }
    TEST_ASSERT_EQUAL_INT(entry->cache_gk.confidence_level, key.cache_gk.confidence_level);
    TEST_ASSERT_EQUAL_INT(entry->cache_gk.category_id, key.cache_gk.category_id);
    ret = strcmp(entry->cache_gk.gk_policy, key.cache_gk.gk_policy);
    TEST_ASSERT_EQUAL_INT(ret, 0);
    free(key.cache_gk.gk_policy);

    /* sleep for 1 seconds */
    sleep(1);
    rc = dns_cache_ttl_cleanup();
    TEST_ASSERT_TRUE(rc);

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_FALSE(rc_lookup);

    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 2);
    print_dns_cache();

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip6, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x06;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);

    entry = entry6;
    dns_cache_del_entry(entry);
    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 1);
    print_dns_cache();

    memset(&key, 0, sizeof(struct ip2action_req));
    util_populate_sockaddr(AF_INET, &v4udstip7, &ip);
    key.ip_addr = &ip;
    mac.addr[0] = 0xaa;
    mac.addr[1] = 0xaa;
    mac.addr[2] = 0xaa;
    mac.addr[3] = 0xaa;
    mac.addr[4] = 0xaa;
    mac.addr[5] = 0x07;
    key.device_mac = &mac;

    rc_lookup = dns_cache_ip2action_lookup(&key);
    /* Validate lookup to the dns_cache entry */
    TEST_ASSERT_TRUE(rc_lookup);

    entry = entry7;
    dns_cache_del_entry(entry);
    nelem = dns_cache_get_size();
    TEST_ASSERT_EQUAL_INT(nelem, 0);
    print_dns_cache();

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void test_events(void)
{
    /* Test overall test duration */
    dns_cache_ev_test_setup(++g_test_mgr.g_timeout);

    /* Start the main loop */
    ev_run(g_test_mgr.loop, 0);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);
    UnityBegin(test_name);

    dns_cache_global_test_setup();

    RUN_TEST(test_add_dns_cache);
    RUN_TEST(test_del_dns_cache);
    RUN_TEST(test_upd_dns_cache);
    RUN_TEST(test_dns_cache_ref_count);
    RUN_TEST(test_bc_dns_cache);
    RUN_TEST(test_wp_dns_cache);
    RUN_TEST(test_gk_dns_cache);
    RUN_TEST(test_dns_cache_entries);

    dns_cache_global_test_teardown();
    return UNITY_END();
}
