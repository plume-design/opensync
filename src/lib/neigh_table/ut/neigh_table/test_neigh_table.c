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
#include "neigh_table.h"
#include "ds_tree.h"

#include "nf_utils.h"

const char *test_name = "neigh_table_tests";

// v4 entries.
struct neighbour_entry *entry1;
struct neighbour_entry *entry2;
struct neighbour_entry *entry5;
// v6 entries.
struct neighbour_entry *entry3;
struct neighbour_entry *entry4;


struct test_timers
{
    ev_timer timeout_watcher_add;           /* Add entries */
    ev_timer timeout_watcher_add_cache;     /* Validate added entries */
    ev_timer timeout_watcher_delete;        /* Delete entries */
    ev_timer timeout_watcher_delete_cache;  /* Validate added entries */
    ev_timer timeout_watcher_update;        /* Update entries */
    ev_timer timeout_watcher_update_cache;  /* Validate updated entries */
};

struct test_mgr
{
    struct ev_loop *loop;
    ev_timer timeout_watcher;
    bool has_ovsdb;
    bool has_net_dummy;
    bool expected;
    int expected_v4_source;
    int expected_v6_source;
    struct test_timers system;
    struct test_timers neigh_add;
    struct test_timers dhcp_timers;
    struct test_timers ipv4_neighbors_timers;
    struct test_timers ipv6_neighbors_timers;
    struct test_timers intf_timers;
    int ut_ifindex;
    double g_timeout;
} g_test_mgr;

char *g_dummy_intf = "ut_neigh0";

/**
 * @brief breaks the ev loop to terminate a test
 */
static void
timeout_cb(EV_P_ ev_timer *w, int revents)
{
    ev_break(EV_A_ EVBREAK_ONE);
}

#if !defined(__x86_64__)
/**
 * @brief called by the unity framework at the start of each test
 */
int
neigh_ovsdb_test_setup(void)
{
    int rc;

    /* Connect to ovsdb */
    rc = ovsdb_init_loop(g_test_mgr.loop, test_name);
    if (!rc)
    {
        LOGE("%s: Failed to initialize OVSDB", __func__);
        return -1;
    }

    g_test_mgr.has_ovsdb = true;
    return 0;
}

#else
int neigh_ovsdb_test_setup(void)
{
    g_test_mgr.has_ovsdb = false;
    return 0;
}
#endif

int neigh_ev_test_setup(double timeout)
{
    ev_timer *p_timeout_watcher;

    /* Set up the timer killing the ev loop, indicating the end of the test */
    p_timeout_watcher = &g_test_mgr.timeout_watcher;

    ev_timer_init(p_timeout_watcher, timeout_cb, timeout, 0.);
    ev_timer_start(g_test_mgr.loop, p_timeout_watcher);

    return 0;
}

void neigh_global_test_setup(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    unsigned int ifindex;
    char ifname[32];
    char cmd[256];
    char *ifn;
    int rc;

    g_test_mgr.has_ovsdb = false;
    g_test_mgr.has_net_dummy = false;
    g_test_mgr.loop = EV_DEFAULT;
    g_test_mgr.g_timeout = 1.0;
    rc = neigh_ovsdb_test_setup();
    TEST_ASSERT_EQUAL_INT(0, rc);

    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ip link add %s type dummy", g_dummy_intf);
    rc = cmd_log(cmd);
    if (!rc)
    {
        memset(cmd, 0 , sizeof(cmd));
        snprintf(cmd, sizeof(cmd), "ip link set %s up", g_dummy_intf);
        rc = cmd_log(cmd);
        if (rc)
        {
            LOGI("%s: failed to bring up ut dummy interface", __func__);
        }
        else
        {
            g_test_mgr.has_net_dummy = true;

            ifindex = if_nametoindex(g_dummy_intf);
            ifn = if_indextoname(ifindex, ifname);
            LOGI("%s: ifindex %u ifname %s", __func__,
                 ifindex, ifn ? ifn : "none");
        }
    }

    neigh_table_init_manager();
    neigh_table_init_monitor(g_test_mgr.loop,
                             g_test_mgr.has_net_dummy,
                             g_test_mgr.has_ovsdb);

    // if (g_test_mgr.has_ovsdb) neigh_src_init();
    mgr->initialized = true;
}


void neigh_global_test_teardown(void)
{
    char cmd[256];

    g_test_mgr.has_ovsdb = false;

    if (g_test_mgr.has_net_dummy)
    {
        memset(cmd, 0 , sizeof(cmd));
        snprintf(cmd, sizeof(cmd), "ip link del %s", g_dummy_intf);
        cmd_log(cmd);
    }
    if (g_test_mgr.has_net_dummy || g_test_mgr.has_ovsdb)
    {
        nf_neigh_exit();
    }
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
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    uint32_t v4dstip1 = htonl(0x04030201);
    uint32_t v4dstip2 = htonl(0x04030202);
    uint32_t v4dstip5 = htonl(0x04030205);

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

    // No ovsdb support
    mgr->update_ovsdb_tables = NULL;

    entry1 = calloc(sizeof(struct neighbour_entry ), 1);
    entry1->ipaddr = calloc(sizeof(struct sockaddr_storage), 1);
    entry1->mac = calloc(sizeof(os_macaddr_t), 1);
    util_populate_sockaddr(AF_INET, &v4dstip1, entry1->ipaddr);
    entry1->mac->addr[0] = 0xaa;
    entry1->mac->addr[1] = 0xaa;
    entry1->mac->addr[2] = 0xaa;
    entry1->mac->addr[3] = 0xaa;
    entry1->mac->addr[4] = 0xaa;
    entry1->mac->addr[5] = 0x01;
    entry1->source = NEIGH_SRC_NOT_SET;

    entry2 = calloc(sizeof(struct neighbour_entry ), 1);
    entry2->ipaddr = calloc(1, sizeof(struct sockaddr_storage));
    entry2->mac = calloc(1, sizeof(os_macaddr_t));
    util_populate_sockaddr(AF_INET, &v4dstip2, entry2->ipaddr);
    entry2->mac->addr[0] = 0xaa;
    entry2->mac->addr[1] = 0xaa;
    entry2->mac->addr[2] = 0xaa;
    entry2->mac->addr[3] = 0xaa;
    entry2->mac->addr[4] = 0xaa;
    entry2->mac->addr[5] = 0x02;
    entry2->source = NEIGH_SRC_NOT_SET;

    entry3 = calloc(sizeof(struct neighbour_entry ), 1);
    entry3->ipaddr = calloc(1, sizeof(struct sockaddr_storage));
    entry3->mac = calloc(1, sizeof(os_macaddr_t));
    util_populate_sockaddr(AF_INET6, &v6dstip1, entry3->ipaddr);
    entry3->mac->addr[0] = 0x66;
    entry3->mac->addr[1] = 0x66;
    entry3->mac->addr[2] = 0x66;
    entry3->mac->addr[3] = 0x66;
    entry3->mac->addr[4] = 0x66;
    entry3->mac->addr[5] = 0x01;
    entry3->source = NEIGH_SRC_NOT_SET;

    entry4 = calloc(sizeof(struct neighbour_entry ), 1);
    entry4->ipaddr = calloc(1, sizeof(struct sockaddr_storage));
    entry4->mac = calloc(1, sizeof(os_macaddr_t));
    util_populate_sockaddr(AF_INET6, &v6dstip2, entry4->ipaddr);
    entry4->mac->addr[0] = 0x77;
    entry4->mac->addr[1] = 0x77;
    entry4->mac->addr[2] = 0x77;
    entry4->mac->addr[3] = 0x77;
    entry4->mac->addr[4] = 0x77;
    entry4->mac->addr[5] = 0x02;
    entry4->source = NEIGH_SRC_NOT_SET;

    entry5 = calloc(sizeof(struct neighbour_entry ), 1);
    entry5->ipaddr = calloc(sizeof(struct sockaddr_storage), 1);
    entry5->mac = calloc(sizeof(os_macaddr_t), 1);
    util_populate_sockaddr(AF_INET, &v4dstip5, entry5->ipaddr);
    entry5->mac->addr[0] = 0xaa;
    entry5->mac->addr[1] = 0xaa;
    entry5->mac->addr[2] = 0xaa;
    entry5->mac->addr[3] = 0xaa;
    entry5->mac->addr[4] = 0xaa;
    entry5->mac->addr[5] = 0x05;
    entry5->source = NEIGH_SRC_NOT_SET;
}

void tearDown(void)
{
    neigh_table_cache_cleanup();

    LOGI("Tearing down the test...");
    free_neigh_entry(entry1);
    free_neigh_entry(entry2);
    free_neigh_entry(entry3);
    free_neigh_entry(entry4);
    free_neigh_entry(entry5);
}


void test_add_neigh_entry(void)
{
    struct neighbour_entry  *entry = NULL;
    uint32_t v4udstip = htonl(0x04030201);
    struct sockaddr_storage key;
    uint32_t v6udstip[4] = {0};
    os_macaddr_t mac_out;
    bool rc_lookup;
    bool rc_add;
    int cmp;

    LOGI("\n******************** %s: starting ****************\n", __func__);
    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    /* Add the neighbour entry */
    entry = entry1;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    // fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);

    /* V6 ip test */
    /* Add the neighbour entry */
    entry = entry3;
    rc_add = neigh_table_add_to_cache(entry);
    TEST_ASSERT_TRUE(rc_add);

    // fill sockaddr
    memset(&mac_out, 0, sizeof(os_macaddr_t));
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(0, cmp);

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void test_del_neigh_entry(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry  *entry = NULL;
    struct sockaddr_storage key;
    uint32_t  v4udstip = htonl(0x04030201);
    uint32_t v6udstip[4] = {0};
    bool rc_lookup = false;
    os_macaddr_t mac_out;
    bool rc_add;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    mgr->update_ovsdb_tables = NULL;
    /* Add the neighbour entry */
    entry = entry1;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Add the neighbour entry */
    entry = entry2;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Del the neighbour entry */
    entry = entry1;
    neigh_table_delete(entry);

    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);

    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_FALSE(rc_lookup);

    /* V6 ip tests*/

    /* Add the neighbour entry */
    entry = entry3;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Add the neighbour entry */
    entry = entry4;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Del the neighbour entry */
    entry = entry3;
    neigh_table_delete(entry);

    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);

    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_FALSE(rc_lookup);

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void test_upd_neigh_entry(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry  *entry = NULL;
    struct sockaddr_storage key;
    uint32_t  v4udstip = htonl(0x04030201);
    uint32_t v6udstip[4] = {0};
    os_macaddr_t mac_out;
    bool rc_lookup;
    bool rc_cache;
    bool rc_add;
    int cmp;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    mgr->update_ovsdb_tables = NULL;
    /* Add the neighbour entry */
    entry = entry1;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Add the neighbour entry */
    entry = entry2;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Upd the neighbour entry */
    entry = entry1;
    entry->mac->addr[3] = 0x33;
    rc_cache = neigh_table_cache_update(entry);
    TEST_ASSERT_TRUE(rc_cache);

    // fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);

    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate the mac */
    cmp = memcmp(&mac_out, entry->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);

    /* V6 test ips.*/

    /* Add the neighbour entry */
    entry = entry3;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Add the neighbour entry */
    entry = entry4;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Upd the neighbour entry */
    entry = entry3;
    entry->mac->addr[3] = 0x33;
    rc_cache = neigh_table_cache_update(entry);
    TEST_ASSERT_TRUE(rc_cache);

    // fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);

    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate the mac */
    cmp = memcmp(&mac_out, entry->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);

    LOGI("\n******************** %s: completed ****************\n", __func__);
}

void add_neigh_entry_in_kernel_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n****** %s: entering", __func__);

    mgr->update_ovsdb_tables = NULL;

    /* Add entry in the kernel */
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ip neigh add 4.3.2.1 lladdr aa:aa:aa:aa:aa:1 dev %s nud reachable",
             g_dummy_intf);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* IPv6 Test */
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ip -6 neigh add 606:606:606:606:606:606:606:606 "
             "lladdr 66:66:66:66:66:01 dev %s nud reachable", g_dummy_intf);
    rc = cmd_log(cmd);

    g_test_mgr.expected = true;
    g_test_mgr.expected_v4_source = NEIGH_TBL_SYSTEM;
    g_test_mgr.expected_v6_source = NEIGH_TBL_SYSTEM;
    LOGI("\n****** %s: done", __func__);
}

void lookup_neigh_entry_in_cache_cb(EV_P_ ev_timer *w, int revents)
{
    uint32_t  v4udstip = htonl(0x04030201);
    struct sockaddr_storage key;
    uint32_t v6udstip[4] = {0};
    os_macaddr_t mac_out;
    bool rc_lookup;
    int cmp;

    LOGI("\n\n\n\n****** %s: entering\n", __func__);
    LOGI("%s: expected: %s", __func__, g_test_mgr.expected ? "true" : "false");
    // fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup == g_test_mgr.expected);

    if (g_test_mgr.expected)
    {
        struct neighbour_entry *lookup;
        struct neighbour_entry entry;

        /* Validate mac content */
        cmp = memcmp(&mac_out, entry1->mac, sizeof(os_macaddr_t));
        TEST_ASSERT_EQUAL_INT(cmp, 0);

        /* Retrieve cached entry to validate its source */
        memset(&entry, 0, sizeof(entry));
        entry.ipaddr = &key;
        entry.mac = &mac_out;
        entry.ifname = NULL;
        entry.source = g_test_mgr.expected_v4_source;
        LOGI("%s: expected source: %s",
             __func__, neigh_table_get_source(entry.source));
        neigh_table_set_entry(&entry);
        lookup = neigh_table_cache_lookup(&entry);
        TEST_ASSERT_NOT_NULL(lookup);
    }

    /* ipv6 tests */
    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    // fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup == g_test_mgr.expected);

    if (g_test_mgr.expected)
    {
        struct neighbour_entry *lookup;
        struct neighbour_entry entry;

        /* Validate mac content */
        cmp = memcmp(&mac_out, entry3->mac, sizeof(os_macaddr_t));
        TEST_ASSERT_EQUAL_INT(cmp, 0);

        /* Retrieve cached entry to validate its source */
        memset(&entry, 0, sizeof(entry));
        entry.ipaddr = &key;
        entry.mac = &mac_out;
        entry.ifname = NULL;
        entry.source = g_test_mgr.expected_v6_source;
        neigh_table_set_entry(&entry);
        lookup = neigh_table_cache_lookup(&entry);
        TEST_ASSERT_NOT_NULL(lookup);
    }
    LOGI("\n****** %s: done\n", __func__);
}

void delete_neigh_entry_in_kernel_cb(EV_P_ ev_timer *w, int revents)
{
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n****** %s: entering\n", __func__);

    /* Add entry in the kernel */
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ip neigh del 4.3.2.1 lladdr aa:aa:aa:aa:aa:1 dev %s",
             g_dummy_intf);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* IPv6 Test */
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ip -6 neigh del 606:606:606:606:606:606:606:606 "
             "lladdr 66:66:66:66:66:01 dev %s", g_dummy_intf);
    rc = cmd_log(cmd);

    g_test_mgr.expected = false;
    LOGI("\n******* %s: done\n", __func__);
}

void setup_lookup_neigh_entry_in_kernel(void)
{
    struct test_timers *t;
    struct ev_loop *loop;

    t = &g_test_mgr.system;
    if (!g_test_mgr.has_net_dummy)
    {
        LOGI("%s: no dummy interface, bypassing test", __func__);
        return;
    }

    loop = g_test_mgr.loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add, add_neigh_entry_in_kernel_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    /* Arm the cache lookup execution timer validating the cache addition */
    ev_timer_init(&t->timeout_watcher_add_cache, lookup_neigh_entry_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add_cache.data = NULL;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete, delete_neigh_entry_in_kernel_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    /* Arm the cache lookup execution timer validating the cache deletion */
    ev_timer_init(&t->timeout_watcher_delete_cache, lookup_neigh_entry_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete_cache.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
    ev_timer_start(loop, &t->timeout_watcher_add_cache);
    ev_timer_start(loop, &t->timeout_watcher_delete);
    ev_timer_start(loop, &t->timeout_watcher_delete_cache);
}

void test_lookup_neigh_entry_not_in_cache(void)
{
    uint32_t  v4udstip = htonl(0x04030201);
    struct sockaddr_storage key;
    uint32_t v6udstip[4] = {0};
    os_macaddr_t mac_out;
    bool rc_lookup;
    int cmp;

    LOGI("\n******************** %s: starting ****************\n", __func__);

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    // fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    memset(&mac_out, 0, sizeof(os_macaddr_t));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_FALSE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry1->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_TRUE(cmp != 0);

    /* v6 tests */
    // fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    memset(&mac_out, 0, sizeof(os_macaddr_t));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_FALSE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry3->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_TRUE(cmp != 0);

    LOGI("\n******************** %s: completed ****************", __func__);
}


void test_neigh_table_cache_update(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry key;
    uint32_t v6udstip[4] = {0};
    bool rc;

    v6udstip[0] = 0xdeadbeef;
    v6udstip[1] = 0xdeadbeef;
    v6udstip[2] = 0xdeadbeef;
    v6udstip[3] = 0xdeadbeef;

    memset(&key, 0, sizeof(struct neighbour_entry));

    key.ipaddr = calloc(sizeof(struct sockaddr_storage), 1);
    TEST_ASSERT_NOT_NULL(key.ipaddr);

    /* fill sockaddr */
    util_populate_sockaddr(AF_INET6, &v6udstip, key.ipaddr);

    key.mac = calloc(sizeof(os_macaddr_t), 1);
    TEST_ASSERT_NOT_NULL(key.mac);

    key.mac->addr[3] = 0x66;

    /* Add the neighbour entry, no ovsdb update */
    rc = neigh_table_add(&key);
    TEST_ASSERT_TRUE(rc);

    /* Now allow ovsdb updates */
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    /* Push the key again, this time with an interface name */
    key.ifname = strdup("intf.ut");
    TEST_ASSERT_NOT_NULL(key.ifname);
    rc = neigh_table_cache_update(&key);
    TEST_ASSERT_TRUE(rc);

    /* Delete the key */
    neigh_table_delete(&key);

    free(key.ifname);
    free(key.ipaddr);
    free(key.mac);
}


void test_source_map(void)
{
    struct neighbour_entry *entry;
    char *str_source;

    entry = entry1;
    entry->source = OVSDB_ARP;

    str_source = neigh_table_get_source(entry->source);
    TEST_ASSERT_NOT_NULL(str_source);

    LOGI("%s: str_source: %s", __func__, str_source);
}


void add_neigh_entry_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *entry;
    bool rc_add;

    LOGI("\n\n\n\n****** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    /* Add entry in ovsdb */
    entry = entry1;
    entry->ifname = strdup(g_dummy_intf);
    TEST_ASSERT_NOT_NULL(entry->ifname);
    entry->source = FSM_ARP;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* IPv6 Test */
    entry = entry3;
    entry->ifname = strdup(g_dummy_intf);
    TEST_ASSERT_NOT_NULL(entry->ifname);
    entry->source = FSM_NDP;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    g_test_mgr.expected = true;
    g_test_mgr.expected_v4_source = FSM_ARP;
    g_test_mgr.expected_v6_source = FSM_NDP;
    LOGI("\n****** %s: done\n", __func__);
}

void delete_neigh_entry_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry *entry;

    LOGI("\n\n\n\n****** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    /* Add entry in ovsdb */
    entry = entry1;
    entry->ifname = strdup(g_dummy_intf);
    TEST_ASSERT_NOT_NULL(entry->ifname);
    entry->source = FSM_ARP;
    neigh_table_delete(entry);

    /* IPv6 Test */
    entry = entry3;
    entry->ifname = strdup(g_dummy_intf);
    TEST_ASSERT_NOT_NULL(entry->ifname);
    entry->source = FSM_NDP;
    neigh_table_delete(entry);

    g_test_mgr.expected = false;
    LOGI("\n****** %s: done\n", __func__);
}

void setup_lookup_neigh_entry_in_ovsdb(void)
{
    struct test_timers *t;
    struct ev_loop *loop;

    t = &g_test_mgr.neigh_add;
    if (!g_test_mgr.has_net_dummy)
    {
        LOGI("%s: no dummy interface, bypassing test", __func__);
        return;
    }

    loop = g_test_mgr.loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add, add_neigh_entry_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    /* Arm the cache lookup execution timer validating the cache addition */
    ev_timer_init(&t->timeout_watcher_add_cache, lookup_neigh_entry_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add_cache.data = NULL;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete, delete_neigh_entry_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    /* Arm the cache lookup execution timer validating the cache deletion */
    ev_timer_init(&t->timeout_watcher_delete_cache, lookup_neigh_entry_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete_cache.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
    ev_timer_start(loop, &t->timeout_watcher_add_cache);
    ev_timer_start(loop, &t->timeout_watcher_delete);
    ev_timer_start(loop, &t->timeout_watcher_delete_cache);
}


void add_dhcp_entry_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    char ipstr[INET_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    /* Add entry in ovsdb */
    entry = entry1;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh i DHCP_leased_IP inet_addr:=%s hwaddr:=" PRI_os_macaddr_t,
             ipstr, FMT_os_macaddr_pt(entry->mac));
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    g_test_mgr.expected_v4_source = OVSDB_DHCP_LEASE;
    g_test_mgr.expected = true;
    LOGI("\n***** %s: done\n", __func__);
}

void delete_dhcp_entry_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    char ipstr[INET_ADDRSTRLEN] = { 0 };
    struct neighbour_entry olookup;
    struct neighbour_entry *entry;
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;
    memset(&olookup, 0, sizeof(struct neighbour_entry));

    olookup.ipaddr = calloc(sizeof(struct sockaddr_storage), 1);
    olookup.mac = calloc(sizeof(os_macaddr_t), 1);

    /* Add entry in ovsdb */
    entry = entry1;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh d DHCP_leased_IP -w inet_addr==%s", ipstr);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    g_test_mgr.expected = false;
    LOGI("***** %s: done", __func__);
}

void lookup_neigh_dhcp_in_cache_cb(EV_P_ ev_timer *w, int revents)
{
    uint32_t  v4udstip = htonl(0x04030201);
    struct sockaddr_storage key;
    os_macaddr_t mac_out;
    bool rc_lookup;
    int cmp;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    // fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);
    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup == g_test_mgr.expected);

    if (g_test_mgr.expected)
    {
        struct neighbour_entry *lookup;
        struct neighbour_entry entry;

        /* Validate mac content */
        cmp = memcmp(&mac_out, entry1->mac, sizeof(os_macaddr_t));
        TEST_ASSERT_EQUAL_INT(cmp, 0);

        /* Retrieve cached entry to validate its source */
        memset(&entry, 0, sizeof(entry));
        entry.ipaddr = &key;
        entry.mac = &mac_out;
        entry.ifname = NULL;
        entry.source = g_test_mgr.expected_v4_source;
        neigh_table_set_entry(&entry);
        lookup = neigh_table_cache_lookup(&entry);
        TEST_ASSERT_NOT_NULL(lookup);
    }
    LOGI("\n***** %s: done\n", __func__);
}

void update_dhcp_entry_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    os_macaddr_t mac = { { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66} };
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    uint32_t v4udstip = htonl(0x04030210);
    char ipstr[INET_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    /* Add entry1 in ovsdb */
    entry = entry1;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh i DHCP_leased_IP inet_addr:=%s hwaddr:=" PRI_os_macaddr_t,
             ipstr, FMT_os_macaddr_pt(entry->mac));
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Add entry2 in ovsdb */
    entry = entry2;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh i DHCP_leased_IP inet_addr:=%s hwaddr:=" PRI_os_macaddr_t,
             ipstr, FMT_os_macaddr_pt(entry->mac));
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Update entry1 mac address */
    entry = entry1;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh u DHCP_leased_IP -w inet_addr==%s hwaddr:=" PRI_os_macaddr_t,
             ipstr, FMT_os_macaddr_t(mac));
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Update entry2 ip address */
    entry = entry2;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, &v4udstip, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh u DHCP_leased_IP -w hwaddr==" PRI_os_macaddr_t
             " inet_addr:=%s",
             FMT_os_macaddr_pt(entry->mac), ipstr);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    g_test_mgr.expected_v4_source = OVSDB_DHCP_LEASE;
    LOGI("\n***** %s: done", __func__);
}

void lookup_dhcp_update_in_cache_cb(EV_P_ ev_timer *w, int revents)
{
    // os_macaddr_t mac = { { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66} };
    uint32_t v4udstip = htonl(0x04030210);
    char ipstr[INET_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering", __func__);

    /* Delete entry1 mac address */
    entry = entry1;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh d DHCP_leased_IP -w inet_addr==%s", ipstr);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Delete entry2 ip address */
    entry = entry2;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, &v4udstip, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh d DHCP_leased_IP -w hwaddr==" PRI_os_macaddr_t,
             FMT_os_macaddr_pt(entry->mac));
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}

void setup_lookup_dhcp_entry_in_ovsdb(void)
{
    struct test_timers *t;
    struct ev_loop *loop;

    if (!g_test_mgr.has_ovsdb)
    {
        LOGI("%s: ovsdb support, bypassing test", __func__);
        return;
    }
    t = &g_test_mgr.dhcp_timers;
    loop = g_test_mgr.loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add,
                  add_dhcp_entry_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    /* Arm the cache lookup execution timer validating the cache addition */
    ev_timer_init(&t->timeout_watcher_add_cache,
                  lookup_neigh_dhcp_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add_cache.data = NULL;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete,
                  delete_dhcp_entry_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    /* Arm the cache lookup execution timer validating the cache deletion */
    ev_timer_init(&t->timeout_watcher_delete_cache,
                  lookup_neigh_dhcp_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete_cache.data = NULL;

    /* Arm the update execution timer */
    ev_timer_init(&t->timeout_watcher_update,
                  update_dhcp_entry_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_update.data = NULL;

    /* Arm the cache lookup execution timer validating the cache update */
    ev_timer_init(&t->timeout_watcher_update_cache,
                  lookup_dhcp_update_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete_cache.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
    ev_timer_start(loop, &t->timeout_watcher_add_cache);
    ev_timer_start(loop, &t->timeout_watcher_delete);
    ev_timer_start(loop, &t->timeout_watcher_delete_cache);
    ev_timer_start(loop, &t->timeout_watcher_update);
    ev_timer_start(loop, &t->timeout_watcher_update_cache);
}


void add_ipv4_neigh_entry_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    char ipstr[INET_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    /* Add entry in ovsdb */
    entry = entry1;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh i IPv4_Neighbors address:=%s hwaddr:=" PRI_os_macaddr_t
             " if_name:=%s",
             ipstr, FMT_os_macaddr_pt(entry->mac), g_dummy_intf);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    g_test_mgr.expected_v4_source = OVSDB_ARP;
    g_test_mgr.expected = true;
    LOGI("\n***** %s: done\n", __func__);
}

void delete_ipv4_neigh_entry_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    char ipstr[INET_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    /* Add entry in ovsdb */
    entry = entry1;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh d IPv4_Neighbors -w address==%s", ipstr);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    g_test_mgr.expected = false;
    LOGI("\n***** %s: done\n", __func__);
}

void lookup_ipv4_neigh_in_cache_cb(EV_P_ ev_timer *w, int revents)
{
    uint32_t  v4udstip = htonl(0x04030201);
    struct sockaddr_storage key;
    os_macaddr_t mac_out;
    bool rc_lookup;
    int cmp;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    // fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);
    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup == g_test_mgr.expected);

    if (g_test_mgr.expected)
    {
        struct neighbour_entry *lookup;
        struct neighbour_entry entry;

        /* Validate mac content */
        cmp = memcmp(&mac_out, entry1->mac, sizeof(os_macaddr_t));
        TEST_ASSERT_EQUAL_INT(cmp, 0);

        /* Retrieve cached entry to validate its source */
        memset(&entry, 0, sizeof(entry));
        entry.ipaddr = &key;
        entry.mac = &mac_out;
        entry.ifname = NULL;
        entry.source = g_test_mgr.expected_v4_source;
        neigh_table_set_entry(&entry);
        lookup = neigh_table_cache_lookup(&entry);
        TEST_ASSERT_NOT_NULL(lookup);
    }
    LOGI("\n***** %s: done\n", __func__);
}

void update_ipv4_neigh_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    os_macaddr_t mac = { { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66} };
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    uint32_t v4udstip = htonl(0x04030210);
    char ipstr[INET_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    /* Add entry1 in ovsdb */
    entry = entry1;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh i IPv4_Neighbors address:=%s  hwaddr:=" PRI_os_macaddr_t
             " if_name:=%s source:=UT",
             ipstr, FMT_os_macaddr_pt(entry->mac), g_dummy_intf);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Add entry2 in ovsdb */
    entry = entry2;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh i IPv4_Neighbors address:=%s  hwaddr:=" PRI_os_macaddr_t
             " if_name:=%s source:=UT",
             ipstr, FMT_os_macaddr_pt(entry->mac), g_dummy_intf);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Update entry1 mac address */
    entry = entry1;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh u IPv4_Neighbors -w address==%s hwaddr:=" PRI_os_macaddr_t,
             ipstr, FMT_os_macaddr_t(mac));
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Update entry2 ip address */
    entry = entry2;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, &v4udstip, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh u IPv4_Neighbors -w hwaddr==" PRI_os_macaddr_t
             " address:=%s",
             FMT_os_macaddr_pt(entry->mac), ipstr);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    g_test_mgr.expected_v4_source = OVSDB_ARP;
    LOGI("\n***** %s: done\n", __func__);
}

void lookup_ipv4_update_in_cache_cb(EV_P_ ev_timer *w, int revents)
{
    uint32_t v4udstip = htonl(0x04030210);
    char ipstr[INET_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    /* Delete entry1 mac address */
    entry = entry1;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh d IPv4_Neighbors -w address==%s", ipstr);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Delete entry2 ip address */
    entry = entry2;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, &v4udstip, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh d IPv4_Neighbors -w hwaddr==" PRI_os_macaddr_t,
             FMT_os_macaddr_pt(entry->mac));
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}

void setup_lookup_ipv4_neigh_entry_in_ovsdb(void)
{
    struct test_timers *t;
    struct ev_loop *loop;

    if (!g_test_mgr.has_ovsdb)
    {
        LOGI("%s: ovsdb support, bypassing test", __func__);
        return;
    }

    t = &g_test_mgr.ipv4_neighbors_timers;
    loop = g_test_mgr.loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add, add_ipv4_neigh_entry_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    /* Arm the cache lookup execution timer validating the cache addition */
    ev_timer_init(&t->timeout_watcher_add_cache, lookup_ipv4_neigh_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add_cache.data = NULL;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete, delete_ipv4_neigh_entry_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    /* Arm the cache lookup execution timer validating the cache deletion */
    ev_timer_init(&t->timeout_watcher_delete_cache, lookup_ipv4_neigh_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete_cache.data = NULL;

    /* Arm the update execution timer */
    ev_timer_init(&t->timeout_watcher_update,
                  update_ipv4_neigh_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_update.data = NULL;

    /* Arm the cache lookup execution timer validating the cache update */
    ev_timer_init(&t->timeout_watcher_update_cache,
                  lookup_ipv4_update_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_update_cache.data = NULL;

    t->timeout_watcher_delete_cache.data = NULL;
    ev_timer_start(loop, &t->timeout_watcher_add);
    ev_timer_start(loop, &t->timeout_watcher_add_cache);
    ev_timer_start(loop, &t->timeout_watcher_delete);
    ev_timer_start(loop, &t->timeout_watcher_delete_cache);
    ev_timer_start(loop, &t->timeout_watcher_update);
    ev_timer_start(loop, &t->timeout_watcher_update_cache);
}


void add_ipv6_neigh_entry_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    char ipstr[INET6_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    /* Add entry in ovsdb */
    entry = entry3;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh i IPv6_Neighbors address:=%s hwaddr:=" PRI_os_macaddr_t
             " if_name:=%s",
             ipstr, FMT_os_macaddr_pt(entry->mac), g_dummy_intf);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    g_test_mgr.expected_v6_source = OVSDB_NDP;
    g_test_mgr.expected = true;
    LOGI("\n***** %s: done\n", __func__);
}

void delete_ipv6_neigh_entry_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    char ipstr[INET6_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    /* Add entry in ovsdb */
    entry = entry3;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh d IPv6_Neighbors -w address==%s", ipstr);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    g_test_mgr.expected = false;
    LOGI("***** %s: done\n", __func__);
}

void lookup_ipv6_neigh_in_cache_cb(EV_P_ ev_timer *w, int revents)
{
    struct sockaddr_storage key;
    os_macaddr_t mac_out;
    uint32_t v6udstip[4];
    bool rc_lookup;
    int cmp;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    // fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);
    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup == g_test_mgr.expected);

    if (g_test_mgr.expected)
    {
        struct neighbour_entry *lookup;
        struct neighbour_entry entry;

        /* Validate mac content */
        cmp = memcmp(&mac_out, entry3->mac, sizeof(os_macaddr_t));
        TEST_ASSERT_EQUAL_INT(cmp, 0);

        /* Retrieve cached entry to validate its source */
        memset(&entry, 0, sizeof(entry));
        entry.ipaddr = &key;
        entry.mac = &mac_out;
        entry.ifname = NULL;
        entry.source = g_test_mgr.expected_v6_source;
        neigh_table_set_entry(&entry);
        lookup = neigh_table_cache_lookup(&entry);
        TEST_ASSERT_NOT_NULL(lookup);
    }
    LOGI("***** %s: done\n", __func__);
}

void update_ipv6_neigh_into_ovsdb_cb(EV_P_ ev_timer *w, int revents)
{
    os_macaddr_t mac = { { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66} };
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    char ipstr[INET6_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    uint32_t v6udstip[4];
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    v6udstip[0] = 0x0a0b0c0d;
    v6udstip[1] = 0x0e0f0a0b;
    v6udstip[2] = 0x0c0d0e0f;
    v6udstip[3] = 0x0a0b0c0d;

    /* Add entry3 in ovsdb */
    entry = entry3;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh i IPv6_Neighbors address:=%s  hwaddr:=" PRI_os_macaddr_t
             " if_name:=%s",
             ipstr, FMT_os_macaddr_pt(entry->mac), g_dummy_intf);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Add entry4 in ovsdb */
    entry = entry4;
    neigh_table_set_entry(entry);
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh i IPv6_Neighbors address:=%s  hwaddr:=" PRI_os_macaddr_t
             " if_name:=%s",
             ipstr, FMT_os_macaddr_pt(entry->mac), g_dummy_intf);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Update entry4 mac address */
    entry = entry3;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh u IPv6_Neighbors -w address==%s hwaddr:=" PRI_os_macaddr_t,
             ipstr, FMT_os_macaddr_t(mac));
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Update entry4 ip address */
    entry = entry4;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, &v6udstip, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh u IPv6_Neighbors -w hwaddr==" PRI_os_macaddr_t
             " address:=%s",
             FMT_os_macaddr_pt(entry->mac), ipstr);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    g_test_mgr.expected_v6_source = OVSDB_NDP;
    LOGI("\n***** %s: done\n", __func__);
}

void lookup_ipv6_update_in_cache_cb(EV_P_ ev_timer *w, int revents)
{
    // os_macaddr_t mac = { { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66} };
    char ipstr[INET6_ADDRSTRLEN] = { 0 };
    struct neighbour_entry *entry;
    uint32_t v6udstip[4];
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    v6udstip[0] = 0x0a0b0c0d;
    v6udstip[1] = 0x0e0f0a0b;
    v6udstip[2] = 0x0c0d0e0f;
    v6udstip[3] = 0x0a0b0c0d;

    /* Delete entry1 mac address */
    entry = entry3;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, entry->ip_tbl, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh d IPv6_Neighbors -w address==%s", ipstr);
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Delete entry4 ip address */
    entry = entry4;
    memset(cmd, 0, sizeof(cmd));
    inet_ntop(entry->af_family, &v6udstip, ipstr, sizeof(ipstr));
    snprintf(cmd, sizeof(cmd),
             "ovsh d IPv6_Neighbors -w hwaddr==" PRI_os_macaddr_t,
             FMT_os_macaddr_pt(entry->mac));
    LOGI("%s: cmd: %s", __func__, cmd);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}


void setup_lookup_ipv6_neigh_entry_in_ovsdb(void)
{
    struct test_timers *t;
    struct ev_loop *loop;

    if (!g_test_mgr.has_ovsdb)
    {
        LOGI("%s: ovsdb support, bypassing test", __func__);
        return;
    }

    t = &g_test_mgr.ipv6_neighbors_timers;
    loop = g_test_mgr.loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add,
                  add_ipv6_neigh_entry_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    /* Arm the cache lookup execution timer validating the cache addition */
    ev_timer_init(&t->timeout_watcher_add_cache,
                  lookup_ipv6_neigh_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add_cache.data = NULL;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete,
                  delete_ipv6_neigh_entry_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    /* Arm the cache lookup execution timer validating the cache deletion */
    ev_timer_init(&t->timeout_watcher_delete_cache,
                  lookup_ipv6_neigh_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete_cache.data = NULL;

    /* Arm the update execution timer */
    ev_timer_init(&t->timeout_watcher_update,
                  update_ipv6_neigh_into_ovsdb_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_update.data = NULL;

    /* Arm the cache lookup execution timer validating the cache update */
    ev_timer_init(&t->timeout_watcher_update_cache,
                  lookup_ipv6_update_in_cache_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_update_cache.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
    ev_timer_start(loop, &t->timeout_watcher_add_cache);
    ev_timer_start(loop, &t->timeout_watcher_delete);
    ev_timer_start(loop, &t->timeout_watcher_delete_cache);
    ev_timer_start(loop, &t->timeout_watcher_update);
    ev_timer_start(loop, &t->timeout_watcher_update_cache);
}

void add_intf_and_neighs_cb(EV_P_ ev_timer *w, int revents)
{
    // struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    char ut_intf[] = "ut_neigh2";
    unsigned int ifindex;
    char ifname[32];
    char cmd[256];
    char *ifn;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ip link add %s type dummy", ut_intf);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ip link set %s up", ut_intf);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    ifindex = if_nametoindex(ut_intf);
    ifn = if_indextoname(ifindex, ifname);
    LOGI("%s: ifindex %u ifname %s", __func__,
         ifindex, ifn ? ifn : "none");
    g_test_mgr.ut_ifindex = ifindex;
    /* Add entry in the kernel */
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ip neigh add 4.3.2.1 lladdr aa:aa:aa:aa:aa:1 dev %s nud reachable",
             ut_intf);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* IPv6 Test */
    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ip -6 neigh add 606:606:606:606:606:606:606:606 "
             "lladdr 66:66:66:66:66:01 dev %s nud reachable", ut_intf);
    rc = cmd_log(cmd);

    LOGI("\n***** %s: done\n", __func__);
}

void lookup_added_intf_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_interface *intf;
    char ut_intf[] = "ut_neigh2";
    int ifindex;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    ifindex = if_nametoindex(ut_intf);
    TEST_ASSERT_TRUE(ifindex != 0);

    intf = neigh_table_lookup_intf(ifindex);
    TEST_ASSERT_NOT_NULL(intf);
    TEST_ASSERT_EQUAL_INT(2, intf->entries_count);

    LOGI("\n***** %s: done\n", __func__);
}

void delete_intf_cb(EV_P_ ev_timer *w, int revents)
{
    char ut_intf[] = "ut_neigh2";
    char cmd[256];
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "ip link del %s type dummy", ut_intf);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}

void lookup_deleted_intf_cb(EV_P_ ev_timer *w, int revents)
{
    struct neigh_interface *intf;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    intf = neigh_table_lookup_intf(g_test_mgr.ut_ifindex);
    TEST_ASSERT_NULL(intf);

    LOGI("\n***** %s: done\n", __func__);
}

void setup_intf_events_test(void)
{
    struct test_timers *t;
    struct ev_loop *loop;

    if (!g_test_mgr.has_ovsdb)
    {
        LOGI("%s: ovsdb support, bypassing test", __func__);
        return;
    }

    t = &g_test_mgr.intf_timers;
    loop = g_test_mgr.loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add,
                  add_intf_and_neighs_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    /* Arm the cache lookup execution timer validating the cache addition */
    ev_timer_init(&t->timeout_watcher_add_cache,
                  lookup_added_intf_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add_cache.data = NULL;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete,
                  delete_intf_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    /* Arm the cache lookup execution timer validating the cache deletion */
    ev_timer_init(&t->timeout_watcher_delete_cache,
                  lookup_deleted_intf_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete_cache.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
    ev_timer_start(loop, &t->timeout_watcher_add_cache);
    ev_timer_start(loop, &t->timeout_watcher_delete);
    ev_timer_start(loop, &t->timeout_watcher_delete_cache);
}


void test_events(void)
{
    if (!(g_test_mgr.has_ovsdb || g_test_mgr.has_net_dummy)) return;

    setup_lookup_neigh_entry_in_kernel();
    setup_lookup_neigh_entry_in_ovsdb();
    setup_lookup_dhcp_entry_in_ovsdb();
    setup_lookup_ipv4_neigh_entry_in_ovsdb();
    setup_lookup_ipv6_neigh_entry_in_ovsdb();
    setup_intf_events_test();

    /* Test overall test duration */
    neigh_ev_test_setup(++g_test_mgr.g_timeout);

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

    neigh_global_test_setup();

    RUN_TEST(test_add_neigh_entry);
    RUN_TEST(test_del_neigh_entry);
    RUN_TEST(test_upd_neigh_entry);
    RUN_TEST(test_source_map);
    RUN_TEST(test_lookup_neigh_entry_not_in_cache);

    RUN_TEST(test_events);

    neigh_global_test_teardown();
    return UNITY_END();
}
