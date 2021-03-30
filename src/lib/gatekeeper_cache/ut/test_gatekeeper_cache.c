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
#include <arpa/inet.h>
#include <linux/random.h>
#include <ctype.h>

#include "log.h"
#include "ovsdb.h"
#include "os.h"
#include "os_types.h"
#include "target.h"
#include "unity.h"
#include "schema.h"
#include "gatekeeper_cache.h"
#include "fsm_policy.h"
#include "ds_tree.h"
#include "memutil.h"

const char *test_name = "gk_cache_tests";

#ifndef IP_STR_LEN
#define IP_STR_LEN          INET6_ADDRSTRLEN
#endif /* IP_STR_LEN */

#define MAX_CACHE_ENTRIES 100001

struct sample_attribute_entries
{
    char mac_str[18];
    int attribute_type;                        /* request type */
    char attr_name[256];                           /* attribute name */
    uint64_t cache_ttl;   /* TTL value that should be set */
    uint8_t action;       /* action req when adding will be set when lookup is
                            performed */
};

struct sample_flow_entries
{
    char mac_str[18];
    char src_ip_addr[IP_STR_LEN];     /* src ip in Network byte order */
    char dst_ip_addr[IP_STR_LEN];     /* dst ip in Network byte order */
    uint8_t ip_version;       /* ipv4 (4), ipv6 (6) */
    uint16_t src_port;        /* source port value */
    uint16_t dst_port;        /* dst port value */
    uint8_t protocol;         /* protocol value  */
    uint8_t direction;        /* used to check inbound/outbound cache */
    uint64_t cache_ttl;       /* TTL value that should be set */
    uint8_t action;           /* action req when adding will be set when lookup is
                                 performed */
    uint64_t hit_counter;     /* will be updated when lookup is performed */
};

struct sample_attribute_entries test_attr_entries[MAX_CACHE_ENTRIES];
struct sample_flow_entries test_flow_entries[MAX_CACHE_ENTRIES];

void
populate_sample_attribute_entries(void)
{
    FILE *fp;
    char line[1024];
    int i = 0;

    /* move genmac.txt file to /tmp/ */
    fp = fopen("/tmp/genmac.txt", "r");
    if (fp == NULL)
    {
        printf("fopen failed !!\n");
        return;
    }

    while (fgets(line, 100, fp))
    {
        if (i >= MAX_CACHE_ENTRIES) return;

        sscanf(line, "%s %d %s ", test_attr_entries[i].mac_str, &test_attr_entries[i].attribute_type, test_attr_entries[i].attr_name);
        i++;
    }
}

void populate_sample_flow_entries(void)
{
    int i;

    for (i = 0; i < MAX_CACHE_ENTRIES; i++)
    {
        strcpy(test_flow_entries[i].mac_str, test_attr_entries[i].mac_str);
        strcpy(test_flow_entries[i].src_ip_addr, "1.1.1.1");
        strcpy(test_flow_entries[i].dst_ip_addr, "2.2.2.2");
        test_flow_entries[i].ip_version = 4;
        test_flow_entries[i].src_port = 443;
        test_flow_entries[i].dst_port = 8888;
        test_flow_entries[i].protocol = 16;
        test_flow_entries[i].direction = GKC_FLOW_DIRECTION_INBOUND;
        test_flow_entries[i].action = FSM_ALLOW;
    }
    
}

struct gk_attr_cache_interface *entry1, *entry2, *entry3, *entry4, *entry5;
struct gkc_ip_flow_interface *flow_entry1, *flow_entry2, *flow_entry3,
    *flow_entry4;

static os_macaddr_t *
gkc_str2os_mac(char *strmac)
{
    os_macaddr_t *mac;
    size_t len, i, j;

    if (strmac == NULL) return NULL;

    /* Validate the input string */
    len = strlen(strmac);
    if (len != 17) return NULL;

    mac = calloc(1, sizeof(*mac));
    if (mac == NULL) return NULL;

    i = 0;
    j = 0;
    do
    {
        char a = strmac[i++];
        char b = strmac[i++];
        uint8_t v;

        if (!isxdigit(a)) goto err_free_mac;
        if (!isxdigit(b)) goto err_free_mac;

        v = (isdigit(a) ? (a - '0') : (toupper(a) - 'A' + 10));
        v *= 16;
        v += (isdigit(b) ? (b - '0') : (toupper(b) - 'A' + 10));
        mac->addr[j] = v;

        if (i == len) break;
        if (strmac[i++] != ':') goto err_free_mac;
        j++;
    } while (i < len);

    return mac;

err_free_mac:
    free(mac);

    return NULL;
}

static void
create_default_attr_entries(void)
{
    entry1 = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry1->action = 1;
    entry1->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:01");
    entry1->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry1->cache_ttl = 1000;
    entry1->action = FSM_BLOCK;
    entry1->attr_name = strdup("www.test.com");


    entry2 = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry2->action = 1;
    entry2->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:02");
    entry2->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry2->cache_ttl = 1000;
    entry2->action = FSM_ALLOW;
    entry2->attr_name = strdup("www.entr2.com");


    entry3 = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry3->action = 1;
    entry3->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:03");
    entry3->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry3->cache_ttl = 1000;
    entry3->action = FSM_ALLOW;
    entry3->attr_name = strdup("www.entr3.com");


    entry4 = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry4->action = 1;
    entry4->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:04");
    entry4->attribute_type = GK_CACHE_REQ_TYPE_URL;
    entry4->cache_ttl = 1000;
    entry4->action = FSM_BLOCK;
    entry4->attr_name = strdup("https://www.google.com");

    entry5 = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry5->action = 1;
    entry5->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:04");
    entry5->attribute_type = GK_CACHE_REQ_TYPE_APP;
    entry5->cache_ttl = 1000;
    entry5->action = FSM_BLOCK;
    entry5->attr_name = strdup("testapp");
}

static void
create_default_flow_entries(void)
{
    flow_entry1 = calloc(sizeof(struct gkc_ip_flow_interface), 1);
    flow_entry1->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:01");
    flow_entry1->direction = GKC_FLOW_DIRECTION_INBOUND;
    flow_entry1->src_port = 80;
    flow_entry1->dst_port = 8002;

    flow_entry1->ip_version = 4;
    flow_entry1->protocol = 16;
    flow_entry1->cache_ttl = 1000;
    flow_entry1->action = FSM_BLOCK;
    flow_entry1->src_ip_addr = calloc(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "1.1.1.1", flow_entry1->src_ip_addr);

    flow_entry1->dst_ip_addr = calloc(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "10.2.4.3", flow_entry1->dst_ip_addr);

    flow_entry2 = calloc(sizeof(struct gkc_ip_flow_interface), 1);
    flow_entry2->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:02");
    flow_entry2->direction = GKC_FLOW_DIRECTION_INBOUND;
    flow_entry2->src_port = 443;
    flow_entry2->dst_port = 8888;
    flow_entry2->ip_version = 4;
    flow_entry2->protocol = 16;
    flow_entry2->cache_ttl = 1000;
    flow_entry2->action = FSM_BLOCK;
    flow_entry2->src_ip_addr = calloc(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "2.2.2.2", flow_entry2->src_ip_addr);

    flow_entry2->dst_ip_addr = calloc(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "10.2.2.2", flow_entry2->dst_ip_addr);

    flow_entry3 = calloc(sizeof(struct gkc_ip_flow_interface), 1);
    flow_entry3->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:02");
    flow_entry3->direction = GKC_FLOW_DIRECTION_INBOUND;
    flow_entry3->src_port = 22;
    flow_entry3->dst_port = 3333;
    flow_entry3->ip_version = 6;
    flow_entry3->protocol = 16;
    flow_entry3->cache_ttl = 1000;
    flow_entry3->src_ip_addr = calloc(1, sizeof(struct in6_addr));
    inet_pton(
        AF_INET6, "0:0:0:0:0:FFFF:204.152.189.116", flow_entry3->src_ip_addr);
    flow_entry3->dst_ip_addr = calloc(1, sizeof(struct in6_addr));
    inet_pton(AF_INET6, "1:0:0:0:0:0:0:8", flow_entry3->dst_ip_addr);

    flow_entry4 = calloc(sizeof(struct gkc_ip_flow_interface), 1);
    flow_entry4->device_mac = calloc(sizeof(os_macaddr_t), 1);
    flow_entry4->device_mac->addr[0] = 0xaa;
    flow_entry4->device_mac->addr[1] = 0xaa;
    flow_entry4->device_mac->addr[2] = 0xaa;
    flow_entry4->device_mac->addr[3] = 0xaa;
    flow_entry4->device_mac->addr[4] = 0xaa;
    flow_entry4->device_mac->addr[5] = 0x03;
    flow_entry4->direction = GKC_FLOW_DIRECTION_OUTBOUND;
    flow_entry4->src_port = 16;
    flow_entry4->dst_port = 444;
    flow_entry4->action = FSM_BLOCK;
    flow_entry4->src_ip_addr = calloc(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "1.1.1.1", flow_entry4->src_ip_addr);

    flow_entry4->dst_ip_addr = calloc(1, sizeof(struct in6_addr));
    inet_pton(AF_INET, "10.2.4.3", flow_entry4->dst_ip_addr);
}

void
setUp(void)
{
    create_default_attr_entries();
    create_default_flow_entries();
    gk_cache_init();
    populate_sample_attribute_entries();
    populate_sample_flow_entries();
}

void
free_flow_interface(struct gkc_ip_flow_interface *entry)
{
    if (!entry) return;

    free(entry->device_mac);
    free(entry->src_ip_addr);
    free(entry->dst_ip_addr);
    free(entry);
}

void
del_default_flow_entries(void)
{
    free_flow_interface(flow_entry1);
    free_flow_interface(flow_entry2);
    free_flow_interface(flow_entry3);
    free_flow_interface(flow_entry4);
}

void
free_cache_interface(struct gk_attr_cache_interface *entry)
{
    if (!entry) return;

    FREE(entry->device_mac);
    FREE(entry->attr_name);
    FREE(entry->fqdn_redirect);
    FREE(entry);
}

void
del_default_attr_entries(void)
{
    free_cache_interface(entry1);
    free_cache_interface(entry2);
    free_cache_interface(entry3);
    free_cache_interface(entry4);
    free_cache_interface(entry5);
}

void
tearDown(void)
{
    gk_cache_cleanup();
    del_default_attr_entries();
    del_default_flow_entries();
}

void
test_lookup(void)
{
    struct gk_attr_cache_interface *entry;
    int ret;

    LOGI("starting test: %s ...", __func__);

    /* check for attribute present */
    entry            = entry1;
    entry->cache_ttl = 10000;
    LOGN("adding attribute %s", entry->attr_name);
    gkc_add_attribute_entry(entry);
    LOGN("checking if attribute %s is present", entry->attr_name);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* check for attribute not present */
    entry = entry2;
    LOGN("checking if attribute %s is present", entry->attr_name);
    /* should not be found, as the entry is not added */
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* check for attribute present by attribute type is different */
    entry                 = entry1;
    entry->attribute_type = GK_CACHE_REQ_TYPE_URL;
    ret                   = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* check for invalid attribute type */
    entry                 = entry1;
    entry->attribute_type = GK_CACHE_MAX_REQ_TYPES;
    ret                   = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    LOGI("ending test: %s", __func__);
}

void
test_hit_counter(void)
{
    struct gk_attr_cache_interface *entry;
    entry = entry1;
    int ret;

    LOGI("starting test: %s ...", __func__);

    gkc_add_attribute_entry(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);
    TEST_ASSERT_EQUAL_INT(1, entry->hit_counter);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);
    TEST_ASSERT_EQUAL_INT(2, entry->hit_counter);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);
    TEST_ASSERT_EQUAL_INT(3, entry->hit_counter);

    LOGI("ending test: %s", __func__);
}

void
test_delete_attr(void)
{
    struct gk_attr_cache_interface *entry;
    int ret;

    LOGI("starting test: %s ...", __func__);

    entry = entry1;
    /*
     * Try to del entry without adding.
     */
    ret = gkc_del_attribute(entry);
    TEST_ASSERT_EQUAL_INT(false, ret);

    /*
     * Add and delete entry
     */

    /* add attribute entry */
    gkc_add_attribute_entry(entry);
    /* check if attribute is present */
    ret = gkc_lookup_attribute_entry(entry, true);
    /* entry should be present */
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* remove the entry */
    gkc_del_attribute(entry);
    /* check if attribute is present */
    ret = gkc_lookup_attribute_entry(entry, true);
    /* entry should not be present */
    TEST_ASSERT_EQUAL_INT(0, ret);

    LOGI("ending test: %s", __func__);

    /*
     * Add and try to remove invalid entry
     */
    entry = entry2;
    gkc_add_attribute_entry(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    entry->attribute_type = GK_CACHE_MAX_REQ_TYPES;
    ret = gkc_del_attribute(entry);
    TEST_ASSERT_EQUAL_INT(false, ret);

    struct gk_attr_cache_interface *new_entry;
    new_entry = calloc(sizeof(struct gk_attr_cache_interface), 1);
    /* remove empty attribute */
    ret = gkc_del_attribute(new_entry);
    TEST_ASSERT_EQUAL_INT(false, ret);
    free(new_entry);
}

void
test_app_name(void)
{
    struct gk_attr_cache_interface *entry;
    int ret;

    LOGI("starting test: %s ...", __func__);
    entry = entry5;
    gkc_add_attribute_entry(entry);

    gkc_cache_entries();
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    gkc_del_attribute(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    LOGI("ending test: %s", __func__);
}

void
test_host_name(void)
{
    struct gk_attr_cache_interface *entry;
    int ret;

    LOGI("starting test: %s ...", __func__);
    entry = entry5;
    entry->attribute_type = GK_CACHE_REQ_TYPE_HOST;
    gkc_add_attribute_entry(entry);

    gkc_cache_entries();
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    gkc_del_attribute(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    LOGI("ending test: %s", __func__);
}

void
test_host_entry_in_fqdn(void)
{
    struct gk_attr_cache_interface *entry;
    int ret;

    LOGN("starting test: %s ...", __func__);

    entry = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry->action = FSM_ALLOW;
    entry->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry->cache_ttl = 1000;
    entry->action = FSM_ALLOW;
    entry->attr_name = strdup("www.entr2.com");
    gkc_add_attribute_entry(entry);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* check SNI with same value as FQDN */
    entry->attribute_type = GK_CACHE_REQ_TYPE_SNI;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* check HOST with same value as FQDN */
    entry->attribute_type = GK_CACHE_REQ_TYPE_HOST;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* Delete entry in FQDN
    *  HOST and SNI lookup should also fail.
    */
    entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    /* del from fqdn tree */
    gkc_del_attribute(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    /* check deletion success */
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* check in SNI tree, should not find an entry */
    entry->attribute_type = GK_CACHE_REQ_TYPE_SNI;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* check in HOST tree, should not find an entry */
    entry->attribute_type = GK_CACHE_REQ_TYPE_HOST;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Add entry to the HOST cache
    *  Lookup with FQDN and SNI with same value as FQDN
    *  should be successfull.
    */
    entry->attribute_type = GK_CACHE_REQ_TYPE_HOST;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* check SNI with same value as HOST */
    entry->attribute_type = GK_CACHE_REQ_TYPE_SNI;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* check FQDN with same value as HOST */
    entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* Delete entry from the HOST cache */
    gkc_del_attribute(entry);

    /* Add entry to the SNI cache
    *  Lookup with FQDN and HOST with same value as SNI
    *  should be successfull.
    */
    entry->attribute_type = GK_CACHE_REQ_TYPE_SNI;
    gkc_add_attribute_entry(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* check HOST with same value as SNI */
    entry->attribute_type = GK_CACHE_REQ_TYPE_HOST;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* check FQDN with same value as SNI */
    entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    free(entry->device_mac);
    free(entry->attr_name);
    free(entry);

    LOGN("ending test: %s", __func__);
}

void
test_ipv4_attr(void)
{
    struct gk_attr_cache_interface *entry;

    entry = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry->action = 1;
    entry->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry->cache_ttl = 1000;
    entry->action = FSM_BLOCK;
    entry->attr_name = strdup("1.1.1.1");
    int ret;

    LOGI("starting test: %s ...", __func__);
    gkc_add_attribute_entry(entry);

    gkc_cache_entries();

    free(entry->attr_name);
    entry->attr_name = strdup("2.2.2.2");
    gkc_add_attribute_entry(entry);
    gkc_cache_entries();

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    gkc_del_attribute(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    free(entry->device_mac);
    free(entry->attr_name);
    free(entry);

    LOGI("ending test: %s", __func__);
}

void
test_ipv6_attr(void)
{
    struct gk_attr_cache_interface *entry;

    entry = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry->action = 1;
    entry->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    entry->cache_ttl = 1000;
    entry->action = FSM_BLOCK;
    entry->attr_name = strdup("2001:0000:3238:DFE1:0063:0000:0000:FEFB");
    int ret;

    LOGI("starting test: %s ...", __func__);
    gkc_add_attribute_entry(entry);

    gkc_cache_entries();

    free(entry->attr_name);
    entry->attr_name = strdup("2001:0000:3238:DFE1:0063:0000:0000:FEFA");
    gkc_add_attribute_entry(entry);
    gkc_cache_entries();

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    gkc_del_attribute(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    free(entry->device_mac);
    free(entry->attr_name);
    free(entry);

    LOGI("ending test: %s", __func__);
}

void
test_check_ttl(void)
{
    struct gk_attr_cache_interface *entry;
    int ret, count;

    LOGI("starting test: %s ...", __func__);
    entry = entry1;
    entry->cache_ttl = 10000;
    gkc_add_attribute_entry(entry);

    entry = entry2;
    entry->cache_ttl = 10000;
    gkc_add_attribute_entry(entry);

    /* set ttl to 2 secs for entry 3 and 4 */
    entry = entry3;
    entry->cache_ttl = 2;
    gkc_add_attribute_entry(entry);

    entry = entry4;
    entry->cache_ttl = 2;
    gkc_add_attribute_entry(entry);

    /* all 4 entries should be present */
    count = gk_get_device_count();
    TEST_ASSERT_EQUAL_INT(4, count);

    /* sleep for 3 secs for ttl with value 2 to expire */
    sleep(3);

    /* clean up entries with expired entry */
    gkc_ttl_cleanup();

    /* 2 entries should be deleted due to expired time */
    ret = gkc_lookup_attribute_entry(entry3, true);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = gkc_lookup_attribute_entry(entry4, true);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = gkc_lookup_attribute_entry(entry1, true);
    TEST_ASSERT_EQUAL_INT(1, ret);
    ret = gkc_lookup_attribute_entry(entry2, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    LOGI("ending test: %s", __func__);
}

void
test_add_gk_cache(void)
{
    struct gk_attr_cache_interface *entry1, *entry2, *entry3, *entry4;

    LOGI("starting test: %s ...", __func__);

    entry1 = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry1->action = 1;
    entry1->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:01");
    entry1->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry1->attr_name = strdup("www.test.com");

    gkc_add_attribute_entry(entry1);

    entry2 = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry2->action = 1;
    entry2->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:02");
    entry2->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry2->attr_name = strdup("www.entr2.com");

    gkc_add_attribute_entry(entry2);

    entry3 = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry3->action = 1;
    entry3->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:02");
    entry3->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry3->attr_name = strdup("www.entr3.com");

    gkc_add_attribute_entry(entry3);


    entry4 = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry4->action = 1;
    entry4->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:02");
    entry4->attribute_type = GK_CACHE_REQ_TYPE_URL;
    entry4->attr_name = strdup("https://www.google.com");

    gkc_add_attribute_entry(entry4);

    gkc_cache_entries();

    free_cache_interface(entry1);
    free_cache_interface(entry2);
    free_cache_interface(entry3);
    free_cache_interface(entry4);

    LOGI("ending test: %s", __func__);
}

/* Flow related test cases */
void
test_add_flow(void)
{
    struct gkc_ip_flow_interface *flow_entry;

    LOGI("starting test: %s ...", __func__);
    // flow_entry = calloc(sizeof(struct gkc_ip_flow_interface), 1);
    flow_entry = flow_entry1;
    gkc_add_flow_entry(flow_entry);

    flow_entry = flow_entry2;
    gkc_add_flow_entry(flow_entry);

    flow_entry = flow_entry3;
    gkc_add_flow_entry(flow_entry);

    gkc_cache_entries();

    LOGI("ending test: %s", __func__);
}

void
test_flow_lookup(void)
{
    struct gkc_ip_flow_interface *flow_entry;
    int ret;

    LOGI("starting test: %s ...", __func__);
    flow_entry = flow_entry1;
    gkc_add_flow_entry(flow_entry);

    /* search for the added flow */
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(true, ret);

    /* change the protocol value and search */
    flow_entry           = flow_entry1;
    flow_entry->dst_port = 123;
    ret                  = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(false, ret);

    /* change the src port value and search */
    flow_entry           = flow_entry1;
    flow_entry->src_port = 4444;
    ret                  = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(false, ret);

    /* change the protocol value and search */
    flow_entry           = flow_entry1;
    flow_entry->protocol = 44;
    ret                  = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(false, ret);

    /* search for added value */
    flow_entry = flow_entry2;
    gkc_add_flow_entry(flow_entry);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(true, ret);

    LOGI("ending test: %s", __func__);
}

void
test_flow_delete(void)
{
    LOGI("starting test: %s ...", __func__);
    struct gkc_ip_flow_interface *flow_entry;
    int ret;

    LOGI("starting test: %s ...", __func__);
    flow_entry = flow_entry1;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_EQUAL_INT(true, ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(true, ret);

    flow_entry = flow_entry2;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_EQUAL_INT(true, ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(true, ret);

    flow_entry = flow_entry3;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_EQUAL_INT(true, ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(true, ret);

    /* delete the flow and check */
    ret = gkc_del_flow(flow_entry);
    TEST_ASSERT_EQUAL_INT(true, ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(false, ret);

    /* try to delete already deleted flow */
    ret = gkc_del_flow(flow_entry);
    TEST_ASSERT_EQUAL_INT(false, ret);

    gkc_cache_entries();

    LOGI("ending test: %s", __func__);
}

void
test_flow_hit_counter(void)
{
    struct gkc_ip_flow_interface *flow_entry;
    int ret;

    LOGI("starting test: %s ...", __func__);

    flow_entry = flow_entry1;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_EQUAL_INT(true, ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(true, ret);
    TEST_ASSERT_EQUAL_INT(1, flow_entry->hit_counter);

    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(true, ret);
    TEST_ASSERT_EQUAL_INT(2, flow_entry->hit_counter);

    gkc_lookup_flow(flow_entry, true);
    gkc_lookup_flow(flow_entry, true);
    gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(5, flow_entry->hit_counter);

    LOGI("ending test: %s", __func__);
}

void
test_counters(void)
{
    struct gk_attr_cache_interface *entry, *entry2;
    uint64_t count;

    LOGI("starting test: %s ...", __func__);

    /* add entry with device mac: AA:AA:AA:AA:AA:01 */
    entry = entry1;
    entry->action = FSM_ALLOW;
    entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    gkc_add_attribute_entry(entry);
    count = gkc_get_allowed_counter(entry->device_mac, GK_CACHE_REQ_TYPE_FQDN);
    TEST_ASSERT_EQUAL_INT(1, count);
    count = gkc_get_blocked_counter(entry->device_mac, GK_CACHE_REQ_TYPE_FQDN);
    TEST_ASSERT_EQUAL_INT(0, count);

    /* add entry to the same device. */
    entry2 = calloc(sizeof(struct gk_attr_cache_interface), 1);
    entry2->action = FSM_ALLOW;
    entry2->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:01");
    entry2->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry2->cache_ttl = 1000;
    entry2->action = FSM_ALLOW;
    entry2->attr_name = strdup("www.entr2.com");
    gkc_add_attribute_entry(entry2);
    count = gkc_get_allowed_counter(entry2->device_mac, GK_CACHE_REQ_TYPE_FQDN);
    TEST_ASSERT_EQUAL_INT(2, count);
    count = gkc_get_blocked_counter(entry2->device_mac, GK_CACHE_REQ_TYPE_FQDN);
    TEST_ASSERT_EQUAL_INT(0, count);

    free(entry2->device_mac);
    free(entry2->attr_name);
    free(entry2);

    LOGI("ending test: %s", __func__);
}

void
test_duplicate_entries(void)
{
    struct gk_attr_cache_interface *entry;
    uint64_t count;

    LOGI("starting test: %s ...", __func__);
    entry = entry1;
    /* entries should be added only once */
    gkc_add_attribute_entry(entry);
    count = gk_get_device_count();
    TEST_ASSERT_EQUAL_INT(1, count);

    gkc_add_attribute_entry(entry);
    count = gk_get_device_count();
    TEST_ASSERT_EQUAL_INT(1, count);

    LOGI("ending test: %s", __func__);
}

void
test_max_flow_entries(void)
{
    int i;
    struct gkc_ip_flow_interface *flow_entry;

    flow_entry = calloc(sizeof(struct gkc_ip_flow_interface), 1);
    LOGI("starting test: %s ...", __func__);
    for (i = 0; i < MAX_CACHE_ENTRIES; i++)
    // for (i = 0; i < 20000; i++)
    {
        flow_entry->device_mac =gkc_str2os_mac(test_flow_entries[i].mac_str);
        flow_entry->direction = test_flow_entries[i].direction;
        flow_entry->src_port = test_flow_entries[i].src_port;
        flow_entry->dst_port = test_flow_entries[i].dst_port;
        flow_entry->ip_version = test_flow_entries[i].ip_version;
        flow_entry->protocol = test_flow_entries[i].protocol;
        flow_entry->action = test_flow_entries[i].action;
        flow_entry->src_ip_addr = calloc(1, sizeof(struct in6_addr));
        // flow_entry->hit_counter = 0;
        inet_pton(AF_INET, test_flow_entries[i].src_ip_addr, flow_entry->src_ip_addr);

        flow_entry->dst_ip_addr = calloc(1, sizeof(struct in6_addr));
        inet_pton(AF_INET, test_flow_entries[i].dst_ip_addr, flow_entry->dst_ip_addr);

        gkc_add_flow_entry(flow_entry);

        free(flow_entry->device_mac);
        free(flow_entry->src_ip_addr);
        free(flow_entry->dst_ip_addr);
    }
    LOGN("number of entries %lu \n", gk_get_cache_count());

    free(flow_entry);
    // clear_gatekeeper_cache();
    LOGI("ending test: %s", __func__);
}

void
test_max_attr_entries(void)
{
    LOGI("starting test: %s ...", __func__);
    struct gk_attr_cache_interface *entry;
    int current_count;
    int ret;
    int i;

    ret = access("/tmp/genmac.txt", F_OK);
    if (ret != 0) return;

    entry = calloc(sizeof(struct gk_attr_cache_interface), 1);
    for (i = 0; i < MAX_CACHE_ENTRIES; ++i)
    {
        entry->action         = 1;
        entry->device_mac     = gkc_str2os_mac(test_attr_entries[i].mac_str);
        entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
        entry->cache_ttl      = 1000;
        entry->action         = FSM_BLOCK;
        entry->attr_name      = strdup(test_attr_entries[i].attr_name);
        gkc_add_attribute_entry(entry);

        free(entry->device_mac);
        free(entry->attr_name);
    }

    current_count = gk_get_cache_count();

    for (i = 0; i < 10; i++)
    {
        entry->action         = 1;
        entry->device_mac     = gkc_str2os_mac(test_attr_entries[i].mac_str);
        entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
        entry->cache_ttl      = 1000;
        entry->action         = FSM_BLOCK;
        entry->attr_name      = strdup(test_attr_entries[i].attr_name);
        gkc_del_attribute(entry);

        free(entry->device_mac);
        free(entry->attr_name);
    }

    LOGN("number of entries %lu \n", gk_get_cache_count());
    TEST_ASSERT_EQUAL_INT(current_count - 10, gk_get_cache_count());

    free(entry);
    // clear_gatekeeper_cache();

    LOGI("ending test: %s", __func__);
}

void
test_flow_ttl(void)
{
    int ret;
    struct gkc_ip_flow_interface *flow_entry;

    LOGI("starting test: %s ...", __func__);

    flow_entry            = flow_entry1;
    flow_entry->cache_ttl = 1000;

    gkc_add_flow_entry(flow_entry);

    flow_entry            = flow_entry2;
    flow_entry->cache_ttl = 1000;

    gkc_add_flow_entry(flow_entry);

    flow_entry            = flow_entry3;
    flow_entry->cache_ttl = 2;

    gkc_add_flow_entry(flow_entry);

    flow_entry            = flow_entry4;
    flow_entry->cache_ttl = 2;

    gkc_add_flow_entry(flow_entry);

    sleep(3);

    /* clean up entries with expired entry */
    gkc_ttl_cleanup();

    /* 2 entries should be deleted due to expired time */
    ret = gkc_lookup_flow(flow_entry3, true);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = gkc_lookup_flow(flow_entry4, true);
    TEST_ASSERT_EQUAL_INT(0, ret);
    ret = gkc_lookup_flow(flow_entry2, true);
    TEST_ASSERT_EQUAL_INT(1, ret);
    ret = gkc_lookup_flow(flow_entry1, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

    LOGI("ending test: %s", __func__);
}

void
test_fqdn_redirect_entry(void)
{
    struct gk_attr_cache_interface *entry;
    int ret;

    LOGN("starting test: %s ...", __func__);

    entry = CALLOC(1, sizeof(struct gk_attr_cache_interface));
    entry->action = FSM_ALLOW;
    entry->device_mac =gkc_str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry->cache_ttl = 1000;
    entry->action = FSM_ALLOW;
    entry->attr_name = strdup("www.entr2.com");
    entry->fqdn_redirect = CALLOC(1, sizeof(struct fqdn_redirect_s));
    if (entry->fqdn_redirect == NULL) goto error;

    entry->fqdn_redirect->redirect = 1;
    entry->fqdn_redirect->redirect_ttl = 10;

    STRSCPY(entry->fqdn_redirect->redirect_ips[0], "1.2.3.4");
    STRSCPY(entry->fqdn_redirect->redirect_ips[1], "1.2.3.4");
    gkc_add_attribute_entry(entry);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(1, ret);

error:
    FREE(entry->device_mac);
    FREE(entry->attr_name);
    FREE(entry->fqdn_redirect);
    FREE(entry);

    LOGN("ending test: %s", __func__);

}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);
    UnityBegin(test_name);

    RUN_TEST(test_add_gk_cache);
    RUN_TEST(test_check_ttl);
    RUN_TEST(test_lookup);
    RUN_TEST(test_hit_counter);
    RUN_TEST(test_delete_attr);
    RUN_TEST(test_app_name);
    RUN_TEST(test_host_name);
    RUN_TEST(test_ipv4_attr);
    RUN_TEST(test_ipv6_attr);
    RUN_TEST(test_add_flow);
    RUN_TEST(test_flow_lookup);
    RUN_TEST(test_flow_delete);
    RUN_TEST(test_flow_hit_counter);
    RUN_TEST(test_flow_ttl);
    RUN_TEST(test_fqdn_redirect_entry);

    RUN_TEST(test_counters);
    RUN_TEST(test_duplicate_entries);
    RUN_TEST(test_max_attr_entries);
    RUN_TEST(test_max_flow_entries);
    RUN_TEST(test_host_entry_in_fqdn);
    return UNITY_END();
}
