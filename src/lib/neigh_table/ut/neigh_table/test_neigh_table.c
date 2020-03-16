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

#include "log.h"
#include "os.h"
#include "os_types.h"
#include "target.h"
#include "unity.h"
#include "schema.h"
#include "neigh_table.h"
#include "ds_tree.h"


const char *test_name = "neigh_table_tests";

// v4 entries.
struct neighbour_entry *entry1;
struct neighbour_entry *entry2;
// v6 entries.
struct neighbour_entry *entry3;
struct neighbour_entry *entry4;

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
    uint32_t v4dstip1 = 0x01010101;
    uint32_t v4dstip2 = 0x01010102;

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

    neigh_table_init();

    // No ovsdb support
    mgr->lookup_ovsdb_tables = NULL;
    mgr->update_ovsdb_tables = NULL;
    mgr->lookup_kernel_entry = NULL;
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

    LOGT("Test setup done...");
}

void tearDown(void)
{

    neigh_table_cleanup();

    LOGT("Tearing down the test...");
    free_neigh_entry(entry1);
    free_neigh_entry(entry2);
    free_neigh_entry(entry3);
    free_neigh_entry(entry4);
    neigh_table_cleanup();
}


void test_add_neigh_entry(void)
{
    struct neighbour_entry  *entry = NULL;
    struct sockaddr_storage key;
    uint32_t v4udstip = 0x01010101;
    uint32_t v6udstip[4] = {0};

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    os_macaddr_t mac_out;
    bool rc_lookup = false;
    int cmp = -1;

    /* Add the neighbour entry */
    entry = entry1;
    neigh_table_add(entry);
    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);


    /* V6 ip test*/
    /* Add the neighbour entry */
    entry = entry3;
    neigh_table_add(entry);

    //fill sockaddr
    memset(&mac_out, 0, sizeof(os_macaddr_t));
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);
}

void test_del_neigh_entry(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry  *entry = NULL;
    struct sockaddr_storage key;
    uint32_t  v4udstip = 0x01010101;
    uint32_t v6udstip[4] = {0};
    bool rc_lookup = false;

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    os_macaddr_t mac_out;

    mgr->update_ovsdb_tables = NULL;
    /* Add the neighbour entry */
    entry = entry1;
    neigh_table_add(entry);

    /* Add the neighbour entry */
    entry = entry2;
    neigh_table_add(entry);


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
    neigh_table_add(entry);

    /* Add the neighbour entry */
    entry = entry4;
    neigh_table_add(entry);



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


}

void test_upd_neigh_entry(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry  *entry = NULL;
    struct sockaddr_storage key;
    uint32_t  v4udstip = 0x01010101;
    uint32_t v6udstip[4] = {0};

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    os_macaddr_t mac_out;
    bool rc_lookup = false;
    int cmp = -1;

    mgr->update_ovsdb_tables = NULL;
    /* Add the neighbour entry */
    entry = entry1;
    neigh_table_add(entry);

    /* Add the neighbour entry */
    entry = entry2;
    neigh_table_add(entry);



    /* Upd the neighbour entry */
    entry = entry1;
    entry->mac->addr[3] = 0x33;
    neigh_table_cache_update(entry);

    //fill sockaddr
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
    neigh_table_add(entry);

    /* Add the neighbour entry */
    entry = entry4;
    neigh_table_add(entry);

    /* Upd the neighbour entry */
    entry = entry3;
    entry->mac->addr[3] = 0x33;
    neigh_table_cache_update(entry);

    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);

    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate the mac */
    cmp = memcmp(&mac_out, entry->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);
}

void test_lookup_neigh_entry_in_kernel(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct sockaddr_storage key;
    uint32_t  v4udstip = 0x01010101;
    uint32_t v6udstip[4] = {0};
    bool rc_lookup = false;
    int cmp = -1;

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    os_macaddr_t mac_out;

    mgr->lookup_kernel_entry = lookup_entry_in_kernel;
    /*Add entry in the kernel*/
    system("ip neigh add 1.1.1.1 lladdr aa:aa:aa:aa:aa:1 dev br-wan");
    system("ip -6 neigh add 606:606:606:606:606:606:606:606 lladdr 66:66:66:66:66:01 dev br-home");

    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    LOGT("mac_out[5]:%x entrymac[5]:%x",mac_out.addr[5], entry1->mac->addr[5]);
    cmp = memcmp(&mac_out, entry1->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);

    /* ipv6 tests */
    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry3->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);


    /* Cleanup the table */
    system("ip neigh del 1.1.1.1 lladdr aa:aa:aa:aa:aa:1 dev br-wan");
    system("ip -6 neigh del 606:606:606:606:606:606:606:606 lladdr 66:66:66:66:66:01 dev br-home");
}

void test_lookup_neigh_entry_not_in_kernel(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct sockaddr_storage key;
    uint32_t  v4udstip = 0x01010101;
    uint32_t v6udstip[4] = {0};
    bool rc_lookup = false;
    int cmp = -1;

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    os_macaddr_t mac_out;

    mgr->lookup_kernel_entry = lookup_entry_in_kernel;
    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    memset(&mac_out, 0, sizeof(os_macaddr_t));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_FALSE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry1->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_TRUE(cmp != 0)

    /*v6 tests*/
    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    memset(&mac_out, 0, sizeof(os_macaddr_t));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_FALSE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry3->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_TRUE(cmp != 0)

}

void test_expired_cache_neigh_entry_in_kernel(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct sockaddr_storage key;
    struct neighbour_entry *entry;
    uint32_t v4udstip = 0x01010101;
    uint32_t v6udstip[4] = {0};
    bool rc_lookup = false;
    bool rc_add;
    int cmp = -1;

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;
    os_macaddr_t mac_out;

    mgr->lookup_kernel_entry = lookup_entry_in_kernel;
    /*Add entry in the kernel*/
    system("ip neigh add 1.1.1.1 lladdr aa:aa:aa:aa:aa:1 dev br-home");
    system("ip -6 neigh add 606:606:606:606:606:606:606:606 lladdr 66:66:66:66:66:01 dev br-home");

    /* Add the neighbour entry */
    entry = entry1;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    entry = ds_tree_find(&mgr->neigh_table, entry->ipaddr);
    TEST_ASSERT_NOT_NULL(entry);
    entry->cache_valid_ts -= (NEIGH_CACHE_INTERVAL + 10);

    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry1->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);

    /* ipv6 tests*/
    /* Add the neighbour entry */
    entry = entry3;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    entry = ds_tree_find(&mgr->neigh_table, entry->ipaddr);
    TEST_ASSERT_NOT_NULL(entry);
    entry->cache_valid_ts -= (NEIGH_CACHE_INTERVAL + 10);

    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;
    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry3->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);


    /* Cleanup the neigh entry */
    system("ip neigh del 1.1.1.1 lladdr aa:aa:aa:aa:aa:1 dev br-home");
    system("ip -6 neigh del 606:606:606:606:606:606:606:606 lladdr 66:66:66:66:66:01 dev br-home");
}

void test_expired_cache_neigh_entry_not_in_kernel(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct sockaddr_storage key;
    struct neighbour_entry *entry;
    uint32_t v4udstip = 0x01010101;
    uint32_t v6udstip[4] = {0};
    bool rc_lookup = false;
    bool rc_add;

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    os_macaddr_t mac_out;

    mgr->lookup_kernel_entry = lookup_entry_in_kernel;
    /* Add the neighbour entry */
    entry = entry1;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    entry = ds_tree_find(&mgr->neigh_table, entry->ipaddr);
    TEST_ASSERT_NOT_NULL(entry);
    entry->cache_valid_ts -= (NEIGH_CACHE_INTERVAL + 10);

    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &v4udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_FALSE(rc_lookup);

    /* ipv6 tests*/
    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET6, &v6udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_FALSE(rc_lookup);
}

void test_expired_cache_neigh_entry_in_ovsdb(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct sockaddr_storage key;
    struct neighbour_entry *entry;
    uint32_t  udstip = 0x01010101;
    os_macaddr_t mac_out;
    bool rc_lookup = false;
    bool rc_add;
    int cmp = -1;

    mgr->lookup_ovsdb_tables = lookup_ip_in_ovsdb_table;
    /*Add entry in the ovsdb*/
    system("ovsh i DHCP_leased_IP inet_addr:=1.1.1.1 hwaddr:=aa:aa:aa:aa:aa:01 lease_time:=7200");

    /* Add the neighbour entry */
    entry = entry1;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);
    /* Wait for cache to expire */
    entry = ds_tree_find(&mgr->neigh_table, entry->ipaddr);
    TEST_ASSERT_NOT_NULL(entry);
    entry->cache_valid_ts -= (NEIGH_CACHE_INTERVAL + 10);

    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(&mac_out, entry1->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);

    /* Cleanup the neigh entry */
    system("ovsh d DHCP_leased_IP -w inet_addr==1.1.1.1");
}

void test_expired_cache_neigh_entry_not_in_ovsdb(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct sockaddr_storage key;
    struct neighbour_entry *entry;
    uint32_t  udstip = 0x01010101;
    os_macaddr_t mac_out;
    bool rc_lookup = false;
    bool rc_add;

    mgr->lookup_ovsdb_tables = lookup_ip_in_ovsdb_table;
    /* Add the neighbour entry */
    entry = entry1;
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Update cached entry time stamp in an ancient past */
    entry = ds_tree_find(&mgr->neigh_table, entry->ipaddr);
    TEST_ASSERT_NOT_NULL(entry);
    entry->cache_valid_ts -= (NEIGH_CACHE_INTERVAL + 10);

    //fill sockaddr
    memset(&key, 0, sizeof(struct sockaddr_storage));
    util_populate_sockaddr(AF_INET, &udstip, &key);
    /* Lookup the neighbour entry */
    rc_lookup = neigh_table_lookup(&key, &mac_out);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_FALSE(rc_lookup);
}

void test_add_neigh_entry_into_ovsdb(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry  *entry = NULL;
    struct neighbour_entry key;
    uint32_t v6udstip[4] = {0};
    bool rc_lookup = false;
    bool rc_add;
    int cmp = -1;
    char *ifname;

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    mgr->lookup_ovsdb_tables = lookup_ip_in_ovsdb_table;
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;
    memset(&key, 0, sizeof(struct neighbour_entry));
    key.ipaddr = calloc(sizeof(struct sockaddr_storage), 1);
    key.mac = calloc(sizeof(os_macaddr_t), 1);

    /*  ovsdb access is on. Make sure to have a ifname */
    ifname = "if.ut";
    // key.ifname = strdup(ifname);
    // TEST_ASSERT_NOT_NULL(key.ifname);

    /* Add the neighbour entry */
    /* V6 ip test*/
    /* Add the neighbour entry */
    entry = entry3;
    entry->ifname = strdup(ifname);
    TEST_ASSERT_NOT_NULL(entry->ifname);
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    //fill sockaddr
    util_populate_sockaddr(AF_INET6, &v6udstip, key.ipaddr);

    rc_lookup = mgr->lookup_ovsdb_tables(&key);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate mac content */
    cmp = memcmp(key.mac, entry->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);

    free(key.ifname);
    free(key.ipaddr);
    free(key.mac);
}

void test_del_neigh_entry_from_ovsdb(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry  *entry = NULL;
    struct neighbour_entry  key;
    uint32_t v6udstip[4] = {0};
    bool rc_lookup = false;
    bool rc_add;
    char *ifname;

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    mgr->lookup_ovsdb_tables = lookup_ip_in_ovsdb_table;
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;

    memset(&key, 0, sizeof(struct neighbour_entry));
    key.ipaddr = calloc(sizeof(struct sockaddr_storage), 1);
    key.mac = calloc(sizeof(os_macaddr_t), 1);

    /* Add the neighbour entry */

    /* V6 ip tests*/

    /* Add the neighbour entry */
    entry = entry3;
    /* ovsdb access is on. Make sure to have a ifname */
    ifname = "if.ut";
    entry->ifname = strdup(ifname);
    TEST_ASSERT_NOT_NULL(entry->ifname);
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Add the neighbour entry */
    entry = entry4;
    entry->ifname = strdup(ifname);
    TEST_ASSERT_NOT_NULL(entry->ifname);
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Del the neighbour entry */
    entry = entry3;
    neigh_table_delete(entry);

    //fill sockaddr
    util_populate_sockaddr(AF_INET6, &v6udstip, key.ipaddr);

    /* Lookup the neighbour entry */
    rc_lookup = mgr->lookup_ovsdb_tables(&key);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_FALSE(rc_lookup);

    free(key.ipaddr);
    free(key.mac);
}

void test_upd_neigh_entry_into_ovsdb(void)
{
    struct neigh_table_mgr *mgr = neigh_table_get_mgr();
    struct neighbour_entry  *entry = NULL;
    struct neighbour_entry key;
    uint32_t v6udstip[4] = {0};
    bool rc_lookup = false;
    bool rc_add;
    char *ifname;
    int cmp = -1;

    v6udstip[0] = 0x06060606;
    v6udstip[1] = 0x06060606;
    v6udstip[2] = 0x06060606;
    v6udstip[3] = 0x06060606;

    mgr->lookup_ovsdb_tables = lookup_ip_in_ovsdb_table;
    mgr->update_ovsdb_tables = update_ip_in_ovsdb_table;
    memset(&key, 0, sizeof(struct neighbour_entry));

    key.ipaddr = calloc(sizeof(struct sockaddr_storage), 1);
    key.mac = calloc(sizeof(os_macaddr_t), 1);
    /* Add the neighbour entry */
    /* V6 test ips.*/

    /* Add the neighbour entry */
    entry = entry3;
    /* ovsdb access is on. Make sure to have a ifname */
    ifname = "if.ut";
    entry->ifname = strdup(ifname);
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Add the neighbour entry */
    entry = entry4;
    entry->ifname = strdup(ifname);
    rc_add = neigh_table_add(entry);
    TEST_ASSERT_TRUE(rc_add);

    /* Upd the neighbour entry */
    entry = entry3;
    entry->mac->addr[3] = 0x33;
    neigh_table_cache_update(entry);

    //fill sockaddr
    util_populate_sockaddr(AF_INET6, &v6udstip, key.ipaddr);

    /* Lookup the neighbour entry */
    rc_lookup = mgr->lookup_ovsdb_tables(&key);

    /* Validate lookup to the neighbour entry */
    TEST_ASSERT_TRUE(rc_lookup);

    /* Validate the mac */
    cmp = memcmp(key.mac, entry->mac, sizeof(os_macaddr_t));
    TEST_ASSERT_EQUAL_INT(cmp, 0);

    free(key.ipaddr);
    free(key.mac);
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
    mgr->lookup_ovsdb_tables = lookup_ip_in_ovsdb_table;
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


int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    RUN_TEST(test_add_neigh_entry);
    RUN_TEST(test_del_neigh_entry);
    RUN_TEST(test_upd_neigh_entry);
#if !defined(__x86_64__)
//    RUN_TEST(test_lookup_neigh_entry_in_kernel);
//    RUN_TEST(test_lookup_neigh_entry_not_in_kernel);
//    RUN_TEST(test_expired_cache_neigh_entry_in_kernel);
//    RUN_TEST(test_expired_cache_neigh_entry_not_in_kernel);
    RUN_TEST(test_expired_cache_neigh_entry_in_ovsdb);
    RUN_TEST(test_expired_cache_neigh_entry_not_in_ovsdb);
    RUN_TEST(test_add_neigh_entry_into_ovsdb);
    RUN_TEST(test_del_neigh_entry_from_ovsdb);
    RUN_TEST(test_upd_neigh_entry_into_ovsdb);
    RUN_TEST(test_neigh_table_cache_update);
#endif
    return UNITY_END();
}
