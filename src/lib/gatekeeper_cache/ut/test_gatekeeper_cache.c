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

#include <stdbool.h>
#include <stdint.h>

#include "gatekeeper_cache.h"
#include "log.h"
#include "memutil.h"
#include "sockaddr_storage.h"
#include "unity.h"
#include "ds_tree.h"
#include "os.h"

#include "test_gatekeeper_cache.h"

void
test_lookup(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *cached_entry;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    /* check for attribute present */
    entry = entry1;
    entry->cache_ttl = 10000;
    gkc_add_attribute_entry(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    /* Lookup again and check the counter has the correct value !!! */
    cached_entry = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(cached_entry); /* Make sure to use the variable */

    /* entry1 is a FQDN... short circuit the union */
    TEST_ASSERT_EQUAL_UINT64(3, cached_entry->attr.host_name->count_fqdn.total);
    TEST_ASSERT_EQUAL_UINT64(0, cached_entry->attr.host_name->count_sni.total);
    TEST_ASSERT_EQUAL_UINT64(0, cached_entry->attr.host_name->count_host.total);

    /* Test the other 2 types that are aggregated */
    entry->attribute_type = GK_CACHE_REQ_TYPE_SNI;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);
    cached_entry = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_EQUAL_UINT64(3, cached_entry->attr.host_name->count_fqdn.total);
    TEST_ASSERT_EQUAL_UINT64(1, cached_entry->attr.host_name->count_sni.total);
    TEST_ASSERT_EQUAL_UINT64(0, cached_entry->attr.host_name->count_host.total);

    entry->attribute_type = GK_CACHE_REQ_TYPE_HOST;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_UINT64(3, cached_entry->attr.host_name->count_fqdn.total);
    TEST_ASSERT_EQUAL_UINT64(1, cached_entry->attr.host_name->count_sni.total);
    TEST_ASSERT_EQUAL_UINT64(1, cached_entry->attr.host_name->count_host.total);

    /* check for attribute not present */
    entry = entry2;
    /* should not be found, as the entry is not added */
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_FALSE(ret);

    /* check for attribute present by attribute type is different */
    entry = entry1;
    entry->attribute_type = GK_CACHE_REQ_TYPE_URL;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* check for invalid attribute type */
    entry = entry1;
    entry->attribute_type = GK_CACHE_MAX_REQ_TYPES;
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_EQUAL_INT(0, ret);

    LOGI("ending test: %s", __func__);
}

void
test_hit_counter(void)
{
    struct gk_attr_cache_interface *entry;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    entry = entry1;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_INT(2, entry->hit_counter);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_INT(3, entry->hit_counter);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_INT(4, entry->hit_counter);

    LOGI("ending test: %s", __func__);
}

void
test_delete_attr(void)
{
    struct gk_attr_cache_interface *entry;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    entry = entry1;
    /*
     * Try to del entry without adding.
     */
    ret = gkc_del_attribute(entry);
    TEST_ASSERT_FALSE(ret);

    /*
     * Add and delete entry
     */

    /* add attribute entry */
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);
    /* check if attribute is present */
    ret = gkc_lookup_attribute_entry(entry, true);
    /* entry should be present */
    TEST_ASSERT_TRUE(ret);

    /* remove the entry */
    gkc_del_attribute(entry);
    /* check if attribute is present */
    ret = gkc_lookup_attribute_entry(entry, true);
    /* entry should not be present */
    TEST_ASSERT_FALSE(ret);

    /*
     * Add and try to remove invalid entry
     */
    entry = entry2;
    gkc_add_attribute_entry(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    entry->attribute_type = GK_CACHE_MAX_REQ_TYPES;
    ret = gkc_del_attribute(entry);
    TEST_ASSERT_FALSE(ret);

    struct gk_attr_cache_interface *new_entry;
    new_entry = CALLOC(1, sizeof(*new_entry));
    /* remove empty attribute */
    ret = gkc_del_attribute(new_entry);
    TEST_ASSERT_FALSE(ret);
    FREE(new_entry);

    LOGI("ending test: %s", __func__);
}

void
test_app_name(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *cached_entry;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    entry = entry5;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    cached_entry = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(cached_entry); /* Make sure to use the variable */
    TEST_ASSERT_EQUAL_UINT64(3, cached_entry->attr.app_name->hit_count.total);

    TEST_ASSERT_EQUAL_size_t(1, gk_get_cache_count());

    gkc_del_attribute(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_FALSE(ret);

    TEST_ASSERT_EQUAL_size_t(0, gk_get_cache_count());

    LOGI("ending test: %s", __func__);
}

void
test_host_name(void)
{
    struct gk_attr_cache_interface *entry;
    bool ret;

    LOGI("starting test: %s ...", __func__);
    entry = entry3;
    entry->attribute_type = GK_CACHE_REQ_TYPE_HOST;
    gkc_add_attribute_entry(entry);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    gkc_del_attribute(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_FALSE(ret);

    LOGI("ending test: %s", __func__);
}

void
test_host_entry_in_fqdn(void)
{
    struct gk_attr_cache_interface *entry;
    int ret;

    LOGN("starting test: %s ...", __func__);

    entry = CALLOC(1, sizeof(*entry));
    entry->action = FSM_ALLOW;
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
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

    // Add entry to the HOST cache
    // Lookup with FQDN and SNI with same value as FQDN
    // should be successful.
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

    // Add entry to the SNI cache
    // Lookup with FQDN and HOST with same value as SNI
    // should be successful.
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

    FREE(entry->device_mac);
    FREE(entry->attr_name);
    FREE(entry);

    LOGN("ending test: %s", __func__);
}

void
test_ipv4_attr(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *out;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    entry = CALLOC(1, sizeof(*entry));
    entry->action = 1;
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry->cache_ttl = 1000;
    entry->action = FSM_BLOCK;
    entry->attr_name = "1.2.3.4";
    entry->ip_addr = sockaddr_storage_create(AF_INET, entry->attr_name);

    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    FREE(entry->ip_addr);
    entry->cache_key = 0;

    entry->attr_name = "5.6.7.8";
    entry->ip_addr = sockaddr_storage_create(AF_INET, entry->attr_name);
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    out = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(out);
    ret = sockaddr_storage_equals(&out->attr.ipv4->ip_addr, entry->ip_addr);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_del_attribute(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_FALSE(ret);

    FREE(entry->ip_addr);
    FREE(entry->device_mac);
    FREE(entry);

    LOGI("ending test: %s", __func__);
}

void
test_ipv6_attr(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *out;
    bool ret;

    entry = CALLOC(1, sizeof(*entry));
    entry->action = 1;
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    entry->cache_ttl = 1000;
    entry->action = FSM_BLOCK;
    entry->attr_name = "2001:0000:3238:DFE1:0063:0000:0000:FEFA";
    entry->ip_addr = sockaddr_storage_create(AF_INET6, entry->attr_name);

    LOGI("starting test: %s ...", __func__);
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    FREE(entry->ip_addr);
    entry->cache_key = 0;

    entry->attr_name = "2001:0000:3238:DFE1:0063:0000:0000:FEFB";
    entry->ip_addr = sockaddr_storage_create(AF_INET6, entry->attr_name);
    gkc_add_attribute_entry(entry);
    gkc_print_cache_entries();

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    out = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(out);
    ret = sockaddr_storage_equals(&out->attr.ipv6->ip_addr, entry->ip_addr);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_del_attribute(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_FALSE(ret);

    FREE(entry->ip_addr);
    FREE(entry->device_mac);
    FREE(entry);

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
test_get_attr_key(void)
{
    struct gk_attr_cache_interface entry;
    uint64_t ret;

    MEMZERO(entry);

    ret = get_attr_key(&entry);
    TEST_ASSERT_EQUAL(0, ret);

    entry.ip_addr = CALLOC(1, sizeof(*entry.ip_addr));
    entry.attribute_type = GK_CACHE_REQ_TYPE_APP;
    ret = get_attr_key(&entry);
    TEST_ASSERT_EQUAL(0, ret);
    FREE(entry.ip_addr);
    entry.ip_addr = NULL;

    entry.attr_name = "foo";
    entry.attribute_type = GK_CACHE_REQ_TYPE_APP;
    ret = get_attr_key(&entry);
    TEST_ASSERT_NOT_EQUAL(0, ret);
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    ret = get_attr_key(&entry);
    TEST_ASSERT_NOT_EQUAL(0, ret);
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    ret = get_attr_key(&entry);
    TEST_ASSERT_NOT_EQUAL(0, ret);
    entry.attr_name = NULL;

    /* Some random bytes, don't care about value. */
    entry.ip_addr = MALLOC(sizeof(*entry.ip_addr));
    entry.attribute_type = GK_CACHE_REQ_TYPE_APP;
    ret = get_attr_key(&entry);
    TEST_ASSERT_EQUAL(0, ret);
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    ret = get_attr_key(&entry);
    TEST_ASSERT_NOT_EQUAL(0, ret);
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    ret = get_attr_key(&entry);
    TEST_ASSERT_NOT_EQUAL(0, ret);

    /* cleanup */
    FREE(entry.ip_addr);
}

void
test_add_gk_cache(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *from_cache;
    struct attr_cache *lookup_out;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    /* This is an FQDN, do direction won't matter */
    entry = entry1;
    entry->direction = GKC_FLOW_DIRECTION_OUTBOUND;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    /* re-using entry requires recalculating the key */
    entry->cache_key = 0;
    entry->direction = GKC_FLOW_DIRECTION_INBOUND;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_FALSE(ret);

    entry = entry2;
    FREE(entry->device_mac);
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    entry = entry3;
    FREE(entry->device_mac);
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    entry = entry4;
    FREE(entry->device_mac);
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry1, false);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, false);
    TEST_ASSERT_TRUE(ret);

    /* Verify the entry is already inserted. */
    entry = entry3;
    FREE(entry->device_mac);
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_SNI;
    ret = gkc_lookup_attribute_entry(entry, false);
    TEST_ASSERT_TRUE(ret);

    /* We only have this entry INSERTED once. Counter should be 0 */
    /* Get a reference to the entry in cache. Don't free it ! */
    from_cache = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(from_cache);
    TEST_ASSERT_EQUAL(GK_CACHE_REQ_TYPE_SNI, entry->attribute_type);
    TEST_ASSERT_EQUAL_STRING(entry->attr_name, from_cache->attr.host_name->name);
    TEST_ASSERT_EQUAL_UINT64(0, from_cache->attr.host_name->count_sni.total);

    /* try to insert a second time */
    gkc_add_attribute_entry(entry);

    /* Now counter should be increased */
    from_cache = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(from_cache);
    TEST_ASSERT_EQUAL(GK_CACHE_REQ_TYPE_SNI, entry->attribute_type);
    TEST_ASSERT_EQUAL_STRING(entry->attr_name, from_cache->attr.host_name->name);
    TEST_ASSERT_EQUAL_UINT64(1, from_cache->attr.host_name->count_sni.total);

    /* Try once more... */
    gkc_add_attribute_entry(entry);

    /* Get a reference to the entry in cache. Don't free it ! */
    /* User counter should be unchanged as we were "too soon" */
    from_cache = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(from_cache);
    TEST_ASSERT_EQUAL_UINT64(2, from_cache->attr.host_name->count_sni.total);

    /* Same with a HOST request */
    entry = entry2;
    FREE(entry->device_mac);
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_HOST;
    gkc_add_attribute_entry(entry);

    from_cache = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(from_cache);
    TEST_ASSERT_EQUAL(GK_CACHE_REQ_TYPE_HOST, entry->attribute_type);
    TEST_ASSERT_EQUAL_STRING(entry->attr_name, from_cache->attr.host_name->name);
    TEST_ASSERT_EQUAL_UINT64(1, from_cache->attr.host_name->count_host.total);

    entry = entry4;
    FREE(entry->device_mac);
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    gkc_add_attribute_entry(entry);

    entry = entry4;
    FREE(entry->device_mac);
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:02");
    gkc_add_attribute_entry(entry);

    /* Exercise other cases */
    entry = entry5;
    lookup_out = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NULL(lookup_out);
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);
    lookup_out = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(lookup_out);

    entry->attribute_type = GK_CACHE_REQ_TYPE_URL;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);
    lookup_out = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(lookup_out);

    gkc_print_cache_entries();

    /* Cleanup */
    gkc_cleanup_mgr();

    LOGI("ending test: %s", __func__);
}

/* Flow related test cases */
void
test_add_flow(void)
{
    struct gkc_ip_flow_interface *flow_entry;
    bool ret = false;

    LOGI("starting test: %s ...", __func__);

    flow_entry = flow_entry1;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);

    flow_entry = flow_entry2;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);

    flow_entry = flow_entry3;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_FALSE(ret);

    LOGI("ending test: %s", __func__);
}

void
test_flow_lookup(void)
{
    struct gkc_ip_flow_interface *flow_entry;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    flow_entry = flow_entry1;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);

    /* search for the added flow */
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(true, ret);

    /* change the protocol value and search */
    flow_entry = flow_entry1;
    flow_entry->dst_port = 123;
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_FALSE(ret);

    /* change the src port value and search */
    flow_entry = flow_entry1;
    flow_entry->src_port = 4444;
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_FALSE(ret);

    /* change the protocol value and search */
    flow_entry = flow_entry1;
    flow_entry->protocol = 44;
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_FALSE(ret);

    /* search for added value */
    flow_entry = flow_entry2;
    gkc_add_flow_entry(flow_entry);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_TRUE(ret);

    TEST_ASSERT_EQUAL_UINT64(2, gk_get_cache_count());

    LOGI("ending test: %s", __func__);
}

void
test_flow_delete(void)
{
    struct gkc_ip_flow_interface *flow_entry;
    bool ret;

    LOGI("starting test: %s ...", __func__);
    flow_entry = flow_entry1;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_TRUE(ret);

    flow_entry = flow_entry2;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_TRUE(ret);

    flow_entry = flow_entry3;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_TRUE(ret);

    TEST_ASSERT_EQUAL_UINT64(3, gk_get_cache_count());

    /* delete the flow and check */
    ret = gkc_del_flow(flow_entry);
    TEST_ASSERT_TRUE(ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_FALSE(ret);
    TEST_ASSERT_EQUAL_UINT64(2, gk_get_cache_count());

    /* try to delete already deleted flow */
    ret = gkc_del_flow(flow_entry);
    TEST_ASSERT_FALSE(ret);
    TEST_ASSERT_EQUAL_UINT64(2, gk_get_cache_count());

    LOGI("ending test: %s", __func__);
}

void
test_flow_hit_counter(void)
{
    struct gkc_ip_flow_interface *flow_entry;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    flow_entry = flow_entry1;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_INT(2, flow_entry->hit_counter);

    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_INT(3, flow_entry->hit_counter);

    gkc_lookup_flow(flow_entry, true);
    gkc_lookup_flow(flow_entry, true);
    gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(6, flow_entry->hit_counter);

    gkc_print_cache_entries();

    LOGI("ending test: %s", __func__);
}

void
test_counters(void)
{
    struct gk_attr_cache_interface *entry, *entry2;
    uint64_t count;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    /* add entry with device mac: AA:AA:AA:AA:AA:01 */
    entry = entry1;
    entry->action = FSM_ALLOW;
    entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);
    count = gkc_get_allowed_counter(entry->device_mac, GK_CACHE_REQ_TYPE_FQDN);
    TEST_ASSERT_EQUAL_INT(1, count);
    count = gkc_get_blocked_counter(entry->device_mac, GK_CACHE_REQ_TYPE_FQDN);
    TEST_ASSERT_EQUAL_INT(0, count);

    /* add entry to the same device. */
    entry2 = CALLOC(1, sizeof(*entry2));
    entry2->action = FSM_ALLOW;
    entry2->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry2->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
    entry2->cache_ttl = 1000;
    entry2->action = FSM_ALLOW;
    entry2->attr_name = strdup("www.entr2.com");
    ret = gkc_add_attribute_entry(entry2);
    TEST_ASSERT_TRUE(ret);
    count = gkc_get_allowed_counter(entry2->device_mac, GK_CACHE_REQ_TYPE_FQDN);
    TEST_ASSERT_EQUAL_INT(2, count);
    count = gkc_get_blocked_counter(entry2->device_mac, GK_CACHE_REQ_TYPE_FQDN);
    TEST_ASSERT_EQUAL_INT(0, count);

    FREE(entry2->device_mac);
    FREE(entry2->attr_name);
    FREE(entry2);

    LOGI("ending test: %s", __func__);
}

void
test_duplicate_entries_fqdn(void)
{
    struct gk_attr_cache_interface *entry;
    uint64_t count;
    bool ret;

    LOGI("starting test: %s ...", __func__);
    entry = entry1;
    entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;

    /* entries should be added only once */
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);
    count = gk_get_device_count();
    TEST_ASSERT_EQUAL_INT(1, count);

    /* This will count as a cache hit ! */
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_FALSE(ret);
    count = gk_get_device_count();
    TEST_ASSERT_EQUAL_INT(1, count);

    count = gk_get_cache_count();
    TEST_ASSERT_EQUAL_INT(1, count);

    LOGI("ending test: %s", __func__);
}

void
test_duplicate_entries_ipv4(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *cached_entry;
    uint64_t count;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    entry = entry9;

    /* entries should be added only once */
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);
    count = gk_get_device_count();
    TEST_ASSERT_EQUAL_INT(1, count);

    /* This will count as a cache hit ! */
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_FALSE(ret);
    count = gk_get_device_count();
    TEST_ASSERT_EQUAL_INT(1, count);

    /* This will count as a cache miss ! */
    entry->direction = NET_MD_ACC_OUTBOUND_DIR;
    entry->cache_key = 0;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);
    count = gk_get_device_count();
    TEST_ASSERT_EQUAL_INT(1, count);

    cached_entry = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_EQUAL_INT(1, cached_entry->attr.ipv4->hit_count.total);

    count = gk_get_cache_count();
    TEST_ASSERT_EQUAL_INT(2, count);

    LOGI("ending test: %s", __func__);
}

void
test_max_flow_entries(void)
{
    struct gkc_ip_flow_interface *flow_entry;
    unsigned long num_entries;
    size_t i;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    flow_entry = CALLOC(1, sizeof(*flow_entry));
    for (i = 0; i < OVER_MAX_CACHE_ENTRIES; i++)
    {
        flow_entry->device_mac = str2os_mac(test_flow_entries[i].mac_str);
        flow_entry->direction = test_flow_entries[i].direction;
        flow_entry->src_port = test_flow_entries[i].src_port;
        flow_entry->dst_port = test_flow_entries[i].dst_port;
        flow_entry->ip_version = test_flow_entries[i].ip_version;
        flow_entry->protocol = test_flow_entries[i].protocol;
        flow_entry->action = test_flow_entries[i].action;
        flow_entry->hit_counter = 0;
        flow_entry->src_ip_addr = CALLOC(1, sizeof(struct in6_addr));
        inet_pton(AF_INET, test_flow_entries[i].src_ip_addr, flow_entry->src_ip_addr);

        flow_entry->dst_ip_addr = CALLOC(1, sizeof(struct in6_addr));
        inet_pton(AF_INET, test_flow_entries[i].dst_ip_addr, flow_entry->dst_ip_addr);

        ret = gkc_add_flow_entry(flow_entry);
        /* We can only insert gk_cache_get_size(), so insert fails at that point */
        TEST_ASSERT_TRUE( (ret && i < gk_cache_get_size()) || (!ret && i == gk_cache_get_size()) );

        FREE(flow_entry->device_mac);
        FREE(flow_entry->src_ip_addr);
        FREE(flow_entry->dst_ip_addr);
    }

    FREE(flow_entry);

    num_entries = gk_get_cache_count();
    LOGN("number of entries %lu \n", num_entries);
    TEST_ASSERT_EQUAL_UINT64(gk_cache_get_size(), num_entries);

    clear_gatekeeper_cache();

    LOGI("ending test: %s", __func__);
}

void
test_max_attr_entries(void)
{
    LOGI("starting test: %s ...", __func__);
    struct gk_attr_cache_interface *entry;
    int current_count = 0;
    size_t i;
    bool ret;

    entry = CALLOC(1, sizeof(*entry));
    for (i = 0; i < OVER_MAX_CACHE_ENTRIES; ++i)
    {
        entry->action = 1;
        entry->device_mac = str2os_mac(test_attr_entries[i].mac_str);
        entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
        entry->cache_ttl = 1000;
        entry->action = FSM_BLOCK;
        entry->attr_name = test_attr_entries[i].attr_name;

        ret = gkc_add_attribute_entry(entry);
        TEST_ASSERT_TRUE(ret);

        FREE(entry->device_mac);
    }

    current_count = gk_get_cache_count();
    LOGN("number of entries (1) %lu \n", gk_get_cache_count());
    TEST_ASSERT_EQUAL_INT(gk_cache_get_size(), current_count);

    for (i = 0; i < OVER_MAX_CACHE_ENTRIES; ++i)
    {
        entry->action = 1;
        entry->device_mac = str2os_mac(test_attr_entries[i].mac_str);
        entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
        entry->cache_ttl = 1000;
        entry->action = FSM_BLOCK;
        entry->attr_name = test_attr_entries[i].attr_name;
        gkc_del_attribute(entry);

        FREE(entry->device_mac);
    }

    LOGN("number of entries (2) %lu \n", gk_get_cache_count());
    TEST_ASSERT_EQUAL_INT(current_count - OVER_MAX_CACHE_ENTRIES, gk_get_cache_count());

    FREE(entry);
    clear_gatekeeper_cache();

    LOGI("ending test: %s", __func__);
}

void
test_flow_ttl(void)
{
    bool ret;
    struct gkc_ip_flow_interface *flow_entry;

    LOGI("starting test: %s ...", __func__);

    flow_entry = flow_entry1;
    flow_entry->cache_ttl = 1000;

    gkc_add_flow_entry(flow_entry);

    flow_entry = flow_entry2;
    flow_entry->cache_ttl = 1000;

    gkc_add_flow_entry(flow_entry);

    flow_entry = flow_entry3;
    flow_entry->cache_ttl = 2;

    gkc_add_flow_entry(flow_entry);

    flow_entry = flow_entry4;
    flow_entry->cache_ttl = 2;

    gkc_add_flow_entry(flow_entry);

    sleep(3);

    /* clean up entries with expired entry */
    gkc_ttl_cleanup();

    /* 2 entries should be deleted due to expired time */
    ret = gkc_lookup_flow(flow_entry3, true);
    TEST_ASSERT_FALSE(ret);
    ret = gkc_lookup_flow(flow_entry4, true);
    TEST_ASSERT_FALSE(ret);
    ret = gkc_lookup_flow(flow_entry2, true);
    TEST_ASSERT_TRUE(ret);
    ret = gkc_lookup_flow(flow_entry1, true);
    TEST_ASSERT_TRUE(ret);

    LOGI("ending test: %s", __func__);
}

void
test_fqdn_redirect_entry(void)
{
    struct gk_attr_cache_interface *entry;
    bool ret;

    LOGN("starting test: %s ...", __func__);

    entry = entry1;
    entry->fqdn_redirect = CALLOC(1, sizeof(*entry->fqdn_redirect));
    if (entry->fqdn_redirect == NULL) goto error;

    entry->fqdn_redirect->redirect = 1;
    entry->fqdn_redirect->redirect_ttl = 10;

    STRSCPY(entry->fqdn_redirect->redirect_ips[0], "1.2.3.4");
    STRSCPY(entry->fqdn_redirect->redirect_ips[1], "1.2.3.4");
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

error:
    /* Nothing to cleanup */

    LOGN("ending test: %s", __func__);
}

void
test_fqdn_redirect_copy_in_populate_cache_entry(void)
{
    LOGN("starting test: %s ...", __func__);

    struct gk_reply_header header;
    memset(&header, 0, sizeof(header));
    header.dev_id = STRDUP("AA:AA:AA:AA:AA:01");
    header.action = FSM_ALLOW;
    header.ttl = 3600;
    header.policy = STRDUP("test_policy");
    header.category_id = 1;
    header.confidence_level = 80;
    header.flow_marker = 123;

    // Create source device reply
    struct gk_device2app_repl dev_repl;
    memset(&dev_repl, 0, sizeof(dev_repl));
    dev_repl.header = &header;
    dev_repl.type = GK_ENTRY_TYPE_FQDN;
    dev_repl.fqdn = STRDUP("example.com");

    // Create and initialize fqdn_redirect with test data
    struct fqdn_redirect_s fqdn_redirect;
    memset(&fqdn_redirect, 0, sizeof(fqdn_redirect));
    fqdn_redirect.redirect = true;
    fqdn_redirect.redirect_ttl = 300;
    fqdn_redirect.redirect_cname = STRDUP("redirect.example.com");
    STRSCPY(fqdn_redirect.redirect_ips[0], "192.168.1.1");
    STRSCPY(fqdn_redirect.redirect_ips[1], "192.168.1.2");

    dev_repl.fqdn_redirect = &fqdn_redirect;

    struct gk_attr_cache_interface entry;
    memset(&entry, 0, sizeof(entry));

    bool success = gk_populate_cache_entry(&dev_repl, &entry);
    TEST_ASSERT_TRUE(success);

    // Validate that the function copied all fields properly
    TEST_ASSERT_EQUAL(GK_CACHE_REQ_TYPE_FQDN, entry.attribute_type);
    TEST_ASSERT_EQUAL_STRING("example.com", entry.attr_name);

    // Verify fqdn_redirect was copied correctly
    TEST_ASSERT_NOT_NULL(entry.fqdn_redirect);
    TEST_ASSERT_EQUAL(true, entry.fqdn_redirect->redirect);
    TEST_ASSERT_EQUAL(300, entry.fqdn_redirect->redirect_ttl);
    TEST_ASSERT_NOT_NULL(entry.fqdn_redirect->redirect_cname);
    TEST_ASSERT_EQUAL_STRING("redirect.example.com", entry.fqdn_redirect->redirect_cname);
    TEST_ASSERT_EQUAL_STRING("192.168.1.1", entry.fqdn_redirect->redirect_ips[0]);
    TEST_ASSERT_EQUAL_STRING("192.168.1.2", entry.fqdn_redirect->redirect_ips[1]);

    // Clean up
    gk_free_cache_interface_entry(&entry);
    FREE(dev_repl.fqdn);
    FREE(fqdn_redirect.redirect_cname);
    FREE(header.dev_id);
    FREE(header.policy);

    LOGN("ending test: %s", __func__);
}


void
test_gkc_new_flow_entry(void)
{
    struct gkc_ip_flow_interface *in;
    struct ip_flow_cache *out;

    uint8_t src_ipv4[] = { 127,   0, 0, 1 };
    uint8_t dst_ipv4[] = { 192, 168, 0, 1 };

    in = CALLOC(1, sizeof(*in));

    out = gkc_new_flow_entry(in);
    TEST_ASSERT_NULL(out);

    /* IPV4 */
    in->ip_version = 4;
    in->src_ip_addr = CALLOC(4, sizeof(*in->src_ip_addr));
    memcpy(in->src_ip_addr, src_ipv4, 4);
    out = gkc_new_flow_entry(in);
    TEST_ASSERT_NULL(out);

    in->dst_ip_addr = CALLOC(4, sizeof(*in->dst_ip_addr));
    memcpy(in->dst_ip_addr, dst_ipv4, 4);
    out = gkc_new_flow_entry(in);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL(4, out->ip_version);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src_ipv4, out->src_ip_addr, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dst_ipv4, out->dst_ip_addr, 4);
    gkc_free_flow_members(out);
    FREE(out);

    /* IPv6 */
    in->ip_version = 6;
    FREE(in->src_ip_addr);
    in->src_ip_addr = CALLOC(16, sizeof(*in->src_ip_addr));
    in->src_ip_addr[0] = 1;
    FREE(in->dst_ip_addr);
    in->dst_ip_addr = CALLOC(16, sizeof(*in->dst_ip_addr));
    in->dst_ip_addr[0] = 2;
    out = gkc_new_flow_entry(in);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL(6, out->ip_version);
    TEST_ASSERT_EQUAL(1, out->src_ip_addr[0]);
    TEST_ASSERT_EQUAL(2, out->dst_ip_addr[0]);
    gkc_free_flow_members(out);
    FREE(out);

    free_flow_interface(in);
}

void
test_gkc_is_flow_valid(void)
{
    struct gkc_ip_flow_interface flow;
    bool res;

    res = gkc_is_flow_valid(NULL);
    TEST_ASSERT_FALSE(res);

    memset(&flow, 0, sizeof(flow));
    res = gkc_is_flow_valid(&flow);
    TEST_ASSERT_FALSE(res);

    flow.device_mac = CALLOC(1, sizeof(*flow.device_mac));
    res = gkc_is_flow_valid(&flow);
    TEST_ASSERT_FALSE(res);

    flow.direction = 234; /* some random value */
    res = gkc_is_flow_valid(&flow);
    TEST_ASSERT_FALSE(res);

    flow.direction = GKC_FLOW_DIRECTION_INBOUND;
    flow.ip_version = 4;
    flow.src_ip_addr = CALLOC(1, 16); /* only interested in non NULL */
    flow.dst_ip_addr = CALLOC(1, 16); /* only interested in non NULL */
    res = gkc_is_flow_valid(&flow);
    TEST_ASSERT_TRUE(res);

    flow.direction = GKC_FLOW_DIRECTION_OUTBOUND;
    res = gkc_is_flow_valid(&flow);
    TEST_ASSERT_TRUE(res);

    flow.ip_version = 5;
    res = gkc_is_flow_valid(&flow);
    TEST_ASSERT_FALSE(res);

    flow.ip_version = 6;
    res = gkc_is_flow_valid(&flow);
    TEST_ASSERT_TRUE(res);

    FREE(flow.dst_ip_addr);
    flow.dst_ip_addr = NULL;
    res = gkc_is_flow_valid(&flow);
    TEST_ASSERT_FALSE(res);

    FREE(flow.src_ip_addr);
    flow.src_ip_addr = NULL;
    res = gkc_is_flow_valid(&flow);
    TEST_ASSERT_FALSE(res);

    FREE(flow.device_mac);
 }

void
test_gkc_new_attr_entry(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *out;

    entry = entry1;
    entry->attribute_type = 12345; /* random value out of range */
    out = gkc_new_attr_entry(NULL, entry);
    TEST_ASSERT_NULL(out);
}

void
test_cache_size(void)
{
    struct gk_attr_cache_interface *entry;
    size_t cache_size;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    cache_size = gk_cache_get_size();

    /* cache is just tinitialized, we can change the size */
    gk_cache_set_size(10);
    TEST_ASSERT_EQUAL_size_t(10, gk_cache_get_size());

    entry = entry1;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_UINT64(1, gk_get_cache_count());

    /* cache is not empty: can NOT change the size */
    gk_cache_set_size(100);
    TEST_ASSERT_EQUAL_size_t(10, gk_cache_get_size());

    ret = gkc_del_attribute(entry);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_UINT64(0, gk_get_cache_count());

    /* cache is empty again: changing size is OK */
    gk_cache_set_size(100);
    TEST_ASSERT_EQUAL_size_t(100, gk_cache_get_size());

    /* Reset the cache size to its original value */
    gk_cache_set_size(cache_size);
    TEST_ASSERT_EQUAL_size_t(cache_size, gk_cache_get_size());
    LOGN("ending test: %s", __func__);
}

/* Only the corner cases here */
void
test_allow_blocked_counters(void)
{
    enum gk_cache_request_type attr_type;
    os_macaddr_t *mac;

    attr_type = 12345; /* random value not in enum */

    /* Need to check this to tame cross-compiler GCC. Otherwise, it
     * complains the variable is not used in the TEST_ASSERT_EQUAL_UINT64
     */
    TEST_ASSERT_EQUAL(12345, attr_type);

    TEST_ASSERT_EQUAL_UINT64(0, gkc_get_allowed_counter(NULL, attr_type));
    TEST_ASSERT_EQUAL_UINT64(0, gkc_get_blocked_counter(NULL, attr_type));

    mac = str2os_mac("AA:AA:AA:AA:AA:01");
    TEST_ASSERT_EQUAL_UINT64(0, gkc_get_allowed_counter(mac, attr_type));
    TEST_ASSERT_EQUAL_UINT64(0, gkc_get_blocked_counter(mac, attr_type));

    attr_type = GK_CACHE_REQ_TYPE_FQDN;
    TEST_ASSERT_EQUAL_UINT64(0, gkc_get_allowed_counter(mac, attr_type));
    TEST_ASSERT_EQUAL_UINT64(0, gkc_get_blocked_counter(mac, attr_type));

    /* cleanup */
    FREE(mac);
}

void
test_gkc_add_to_cache_delete_entry(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *ret;

    /* Make entry match exactly entry1, only replacing the attr_name ptr */
    entry = CALLOC(1, sizeof(*entry));
    memcpy(entry, entry1, sizeof(*entry));
    entry->attr_name = STRDUP(entry1->attr_name);

    gkc_add_attribute_entry(entry);

    /* Free the ptr... the cache should still be valid */
    FREE(entry->attr_name);
    ret = gkc_fetch_attribute_entry(entry1);
    TEST_ASSERT_TRUE(ret);
    TEST_ASSERT_EQUAL_STRING(entry1->attr_name, ret->attr.host_name->name);

    FREE(entry);
}
void
test_gkc_private_ipv4_attr_entry(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *cached_entry;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    entry = CALLOC(1, sizeof(*entry));
    entry->action = 1;
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry->cache_ttl = 1000;
    entry->action = FSM_BLOCK;
    entry->attr_name = "192.168.40.1";
    entry->ip_addr = sockaddr_storage_create(AF_INET, entry->attr_name);
    entry->is_private_ip = is_private_ip(entry->attr_name);

    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    cached_entry = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(cached_entry); /* Make sure to use the variable */
    TEST_ASSERT_EQUAL_UINT64(1, cached_entry->attr.ipv4->hit_count.total);

    gkc_print_cache_entries();
    gkc_del_attribute(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_FALSE(ret);
    gkc_print_cache_entries();

    FREE(entry->ip_addr);
    FREE(entry->device_mac);
    FREE(entry);
    LOGI("ending test: %s", __func__);
}

void
test_gkc_private_ipv6_attr_entry(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *cached_entry;
    bool ret;

    entry = CALLOC(1, sizeof(*entry));
    entry->action = 1;
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    entry->cache_ttl = 1000;
    entry->action = FSM_BLOCK;
    entry->attr_name = "fe80::200:5aee:feaa:20a2";
    entry->ip_addr = sockaddr_storage_create(AF_INET6, entry->attr_name);
    entry->is_private_ip = is_private_ip(entry->attr_name);

    LOGI("starting test: %s ...", __func__);
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    gkc_print_cache_entries();

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_TRUE(ret);

    cached_entry = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(cached_entry); /* Make sure to use the variable */
    TEST_ASSERT_EQUAL_UINT64(1, cached_entry->attr.ipv6->hit_count.total);

    gkc_print_cache_entries();
    gkc_del_attribute(entry);
    ret = gkc_lookup_attribute_entry(entry, true);
    TEST_ASSERT_FALSE(ret);
    gkc_print_cache_entries();

    FREE(entry->ip_addr);
    FREE(entry->device_mac);
    FREE(entry);
    LOGI("ending test: %s", __func__);
}

void
test_gkc_private_ip_flow_entry(void)
{
    struct gkc_ip_flow_interface *flow_entry;
    char  ipstr[INET6_ADDRSTRLEN] = { 0 };
    bool ret = false;

    LOGI("starting test: %s ...", __func__);

    flow_entry = flow_entry5;
    if (flow_entry->direction  == GKC_FLOW_DIRECTION_INBOUND)
        inet_ntop(AF_INET, flow_entry->src_ip_addr, ipstr, sizeof(ipstr));
    else
        inet_ntop(AF_INET, flow_entry->dst_ip_addr, ipstr, sizeof(ipstr));

    /** Check private ip */
    flow_entry->is_private_ip =  is_private_ip(ipstr);
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);
    gkc_print_cache_entries();

    /** Add second time */
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_FALSE(ret);

    /* search for the added flow */
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_EQUAL_INT(true, ret);
    ret = gkc_lookup_flow(flow_entry, true);
    ret = gkc_lookup_flow(flow_entry, true);
    ret = gkc_lookup_flow(flow_entry, true);
    ret = gkc_lookup_flow(flow_entry, true);
    ret = gkc_lookup_flow(flow_entry, true);
    ret = gkc_lookup_flow(flow_entry, false);
    TEST_ASSERT_EQUAL_INT(1, flow_entry->hit_counter);

    /* delete the flow and check */
    ret = gkc_del_flow(flow_entry);
    TEST_ASSERT_TRUE(ret);
    ret = gkc_lookup_flow(flow_entry, true);
    TEST_ASSERT_FALSE(ret);
    gkc_print_cache_entries();

    flow_entry = flow_entry5;
    flow_entry->is_private_ip = false;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);
    gkc_print_cache_entries();

    /** Add second time */
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_FALSE(ret);

    gkc_print_cache_entries();
    /* search for the added flow */
    gkc_lookup_flow(flow_entry, true);
    gkc_lookup_flow(flow_entry, true);
    gkc_lookup_flow(flow_entry, true);
    gkc_lookup_flow(flow_entry, true);
    gkc_lookup_flow(flow_entry, true);
    gkc_print_cache_entries();
    TEST_ASSERT_EQUAL_INT(6, flow_entry->hit_counter);
}


void
test_ipv4_upsert(void)
{
    struct gk_attr_cache_interface entry;
    struct attr_cache *out1;
    struct attr_cache *out2;
    char *ipv4 = "1.2.3.4";
    bool ret;

    LOGI("starting test: %s ...", __func__);

    MEMZERO(entry);
    entry.action = FSM_BLOCK;
    entry.device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry.cache_ttl = 1000;
    entry.action = FSM_BLOCK;
    entry.direction = NET_MD_ACC_OUTBOUND_DIR;
    entry.gk_policy = STRDUP("gk_ut_block");
    entry.network_id = "home--1";
    entry.ip_addr = sockaddr_storage_create(AF_INET, ipv4);

    /* Fist validate that the entry is not yet cached */
    out1 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NULL(out1);

    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    out1 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NOT_NULL(out1);

    TEST_ASSERT_EQUAL(entry.action, out1->action);
    TEST_ASSERT_EQUAL_STRING(entry.gk_policy, out1->gk_policy);
    ret = sockaddr_storage_equals(entry.ip_addr, &out1->attr.ipv4->ip_addr);
    TEST_ASSERT_TRUE(ret);

    /* Update the entry */
    entry.action = FSM_BLOCK;
    FREE(entry.gk_policy);
    entry.gk_policy = STRDUP("gk_ut_allow");
    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    out2 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NOT_NULL(out2);

    /* Validate that the same entry was fetched */
    TEST_ASSERT_EQUAL(out1, out2);

    TEST_ASSERT_EQUAL(entry.action, out2->action);
    TEST_ASSERT_EQUAL_STRING(entry.gk_policy, out2->gk_policy);
    ret = sockaddr_storage_equals(entry.ip_addr, &out2->attr.ipv4->ip_addr);
    TEST_ASSERT_TRUE(ret);

    gkc_print_cache_entries();

    ret = gkc_del_attribute(&entry);
    TEST_ASSERT_TRUE(ret);

    gkc_print_cache_entries();

    FREE(entry.ip_addr);
    FREE(entry.gk_policy);
    FREE(entry.device_mac);

    LOGI("ending test: %s", __func__);
}

void
test_flow_entry_removed_from_cache_after_ttl_expires(void)
{
    struct gk_attr_cache_interface entry;
    struct attr_cache *out1;
    struct attr_cache *out2;
    char *ipv4 = "1.2.3.4";
    bool ret;

    LOGI("starting test: %s ...", __func__);

    MEMZERO(entry);
    entry.action = FSM_BLOCK;
    entry.device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry.cache_ttl = 10;
    entry.action = FSM_BLOCK;
    entry.direction = NET_MD_ACC_OUTBOUND_DIR;
    entry.gk_policy = STRDUP("gk_ut_block");
    entry.ip_addr = sockaddr_storage_create(AF_INET, ipv4);

    /* Fist validate that the entry is not yet cached */
    out1 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NULL(out1);

    /* add the entry */
    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    /* TTL was set to 10 secs, entry should be found */
    sleep(7);
    gkc_ttl_cleanup();
    out2 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NOT_NULL(out2);

    /* upsert operation should not refresh TTL */
    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);
    sleep(5);

    /* since TTL is not refreshed, the entry should be removed
     * because of expired TTL */
    gkc_ttl_cleanup();
    out2 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NULL(out2);

    FREE(entry.ip_addr);
    FREE(entry.gk_policy);
    FREE(entry.device_mac);

    LOGI("ending test: %s", __func__);

}

void
test_ipv6_upsert(void)
{
    struct gk_attr_cache_interface entry;
    struct attr_cache *out1;
    struct attr_cache *out2;
    char *ipv6 = "fe80::f0b4:f7ff:fef0:3582";
    bool ret;

    LOGI("starting test: %s ...", __func__);

    MEMZERO(entry);
    entry.action = FSM_BLOCK;
    entry.device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    entry.cache_ttl = 1000;
    entry.action = FSM_BLOCK;
    entry.direction = NET_MD_ACC_OUTBOUND_DIR;
    entry.gk_policy = STRDUP("gk_ut_block");
    entry.network_id = "home--2";
    entry.ip_addr = sockaddr_storage_create(AF_INET6, ipv6);

    /* Fist validate that the entry is not yet cached */
    out1 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NULL(out1);

    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    out1 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NOT_NULL(out1);

    TEST_ASSERT_EQUAL(entry.action, out1->action);
    TEST_ASSERT_EQUAL_STRING(entry.gk_policy, out1->gk_policy);
    ret = sockaddr_storage_equals(entry.ip_addr, &out1->attr.ipv6->ip_addr);
    TEST_ASSERT_TRUE(ret);

    /* Update the entry */
    entry.action = FSM_BLOCK;
    FREE(entry.gk_policy);
    entry.gk_policy = STRDUP("gk_ut_allow");
    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    out2 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NOT_NULL(out2);

    /* Validate that the same entry was fetched */
    TEST_ASSERT_EQUAL(out1, out2);

    TEST_ASSERT_EQUAL(entry.action, out2->action);
    TEST_ASSERT_EQUAL_STRING(entry.gk_policy, out2->gk_policy);
    ret = sockaddr_storage_equals(entry.ip_addr, &out2->attr.ipv6->ip_addr);
    TEST_ASSERT_TRUE(ret);

    gkc_print_cache_entries();

    ret = gkc_del_attribute(&entry);
    TEST_ASSERT_TRUE(ret);

    gkc_print_cache_entries();

    FREE(entry.ip_addr);
    FREE(entry.gk_policy);
    FREE(entry.device_mac);

    LOGI("ending test: %s", __func__);
}

void
test_ipv4_upsert_action_by_name(void)
{
    struct gk_attr_cache_interface entry;
    struct attr_cache *out1;
    struct attr_cache *out2;
    char *ipv4 = "1.2.3.4";
    bool ret;

    LOGI("starting test: %s ...", __func__);

    MEMZERO(entry);
    entry.action_by_name = FSM_BLOCK;
    entry.device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry.cache_ttl = 1000;
    entry.direction = NET_MD_ACC_OUTBOUND_DIR;
    entry.ip_addr = sockaddr_storage_create(AF_INET, ipv4);

    /* Fist validate that the entry is not yet cached */
    out1 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NULL(out1);

    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    out1 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NOT_NULL(out1);
    TEST_ASSERT_EQUAL(entry.action, FSM_ACTION_NONE);
    TEST_ASSERT_EQUAL(entry.action_by_name, FSM_BLOCK);

    if (entry.gk_policy != NULL)
        TEST_ASSERT_EQUAL_STRING(entry.gk_policy, out1->gk_policy);

    ret = sockaddr_storage_equals(entry.ip_addr, &out1->attr.ipv4->ip_addr);
    TEST_ASSERT_TRUE(ret);

    /* Lookup fail since action_by_name is block, action none */
    ret = gkc_lookup_attribute_entry(&entry, true);
    TEST_ASSERT_FALSE(ret);

    /* Update the entry action & policy */
    entry.action = FSM_BLOCK;
    entry.gk_policy = NULL;
    entry.gk_policy = "gk_block";
    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    out2 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NOT_NULL(out2);

    TEST_ASSERT_EQUAL(entry.action, FSM_BLOCK);
    TEST_ASSERT_EQUAL(entry.action_by_name, FSM_BLOCK);
    ret = sockaddr_storage_equals(entry.ip_addr, &out2->attr.ipv4->ip_addr);
    TEST_ASSERT_TRUE(ret);

    /* validate the policy name */
    ret = strcmp(entry.gk_policy, "gk_block");
    TEST_ASSERT_TRUE(ret == 0);

    /* update gk_policy */
    entry.gk_policy = "gk_block_update";
    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(&entry, false);
    TEST_ASSERT_TRUE(ret);

    TEST_ASSERT_EQUAL(entry.action, FSM_BLOCK);
    TEST_ASSERT_EQUAL(entry.action_by_name, FSM_BLOCK);
    ret = sockaddr_storage_equals(entry.ip_addr, &out2->attr.ipv4->ip_addr);
    TEST_ASSERT_TRUE(ret);

    /* validate gk_policy is updated or not */
    ret = strcmp(entry.gk_policy, "gk_block_update");
    TEST_ASSERT_TRUE(ret == 0);

    entry.gk_policy = NULL;
    ret = gkc_lookup_attribute_entry(&entry, false);
    TEST_ASSERT_TRUE(ret);

    /* validate gk_policy is updated or not */
    ret = strcmp(entry.gk_policy, "gk_block_update");
    TEST_ASSERT_TRUE(ret == 0);
    FREE(entry.gk_policy);

    ret = gkc_del_attribute(&entry);
    TEST_ASSERT_TRUE(ret);

    FREE(entry.ip_addr);
    FREE(entry.device_mac);
    LOGI("ending test: %s", __func__);
}

void
test_ipv6_upsert_action_by_name(void)
{
    struct gk_attr_cache_interface entry;
    struct attr_cache *out1;
    struct attr_cache *out2;
    char *ipv6 = "fe80::f0b4:f7ff:fef0:3582";
    bool ret;

    LOGI("starting test: %s ...", __func__);

    MEMZERO(entry);
    entry.action_by_name = FSM_BLOCK;
    entry.device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry.attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    entry.cache_ttl = 1000;
    entry.direction = NET_MD_ACC_OUTBOUND_DIR;
    entry.ip_addr = sockaddr_storage_create(AF_INET6, ipv6);

    /* Fist validate that the entry is not yet cached */
    out1 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NULL(out1);

    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    out1 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NOT_NULL(out1);
    TEST_ASSERT_EQUAL(entry.action, FSM_ACTION_NONE);
    TEST_ASSERT_EQUAL(entry.action_by_name, FSM_BLOCK);
    if (entry.gk_policy != NULL)
        TEST_ASSERT_EQUAL_STRING(entry.gk_policy, out1->gk_policy);
    ret = sockaddr_storage_equals(entry.ip_addr, &out1->attr.ipv6->ip_addr);
    TEST_ASSERT_TRUE(ret);

    /* Lookup fail since action_by_name is block, action none */
    ret = gkc_lookup_attribute_entry(&entry, true);
    TEST_ASSERT_FALSE(ret);

    /* Update the entry action & policy*/
    entry.action = FSM_BLOCK;
    entry.gk_policy = NULL;
    entry.gk_policy = "gk_block";
    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    out2 = gkc_fetch_attribute_entry(&entry);
    TEST_ASSERT_NOT_NULL(out2);

    TEST_ASSERT_EQUAL(entry.action, FSM_BLOCK);
    TEST_ASSERT_EQUAL(entry.action_by_name, FSM_BLOCK);
    ret = sockaddr_storage_equals(entry.ip_addr, &out2->attr.ipv4->ip_addr);
    TEST_ASSERT_TRUE(ret);

    /* validate the policy name */
    ret = strcmp(entry.gk_policy, "gk_block");
    TEST_ASSERT_TRUE(ret == 0);

    /* update gk_policy */
    entry.gk_policy = "gk_block_update";
    ret = gkc_upsert_attribute_entry(&entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(&entry, false);
    TEST_ASSERT_TRUE(ret);

    TEST_ASSERT_EQUAL(entry.action, FSM_BLOCK);
    TEST_ASSERT_EQUAL(entry.action_by_name, FSM_BLOCK);
    ret = sockaddr_storage_equals(entry.ip_addr, &out2->attr.ipv4->ip_addr);
    TEST_ASSERT_TRUE(ret);

    /* validate gk_policy is updated or not */
    ret = strcmp(entry.gk_policy, "gk_block_update");
    TEST_ASSERT_TRUE(ret == 0);

    entry.gk_policy = NULL;
    ret = gkc_lookup_attribute_entry(&entry, false);
    TEST_ASSERT_TRUE(ret);

    /* validate gk_policy is updated or not */
    ret = strcmp(entry.gk_policy, "gk_block_update");
    TEST_ASSERT_TRUE(ret == 0);
    FREE(entry.gk_policy);

    ret = gkc_del_attribute(&entry);
    TEST_ASSERT_TRUE(ret);

    FREE(entry.ip_addr);
    FREE(entry.device_mac);
    LOGI("ending test: %s", __func__);
}

void
test_attr_entry_network_id(void)
{
    struct gk_attr_cache_interface *entry;
    struct attr_cache *cached_entry;
    bool ret;

    LOGI("starting test: %s ...", __func__);

    entry = entry6;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, false);
    TEST_ASSERT_TRUE(ret);
    gkc_print_cache_entries();

    cached_entry = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(cached_entry);

    TEST_ASSERT_EQUAL_STRING(entry->network_id, cached_entry->network_id);

    entry = entry7;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, false);
    TEST_ASSERT_TRUE(ret);
    gkc_print_cache_entries();

    cached_entry = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(cached_entry);

    TEST_ASSERT_EQUAL_STRING(entry->network_id, cached_entry->network_id);

    entry = entry6;
    ret = gkc_del_attribute(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, false);
    TEST_ASSERT_FALSE(ret);

    entry = entry8;
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, false);
    TEST_ASSERT_TRUE(ret);
    gkc_print_cache_entries();

    cached_entry = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(cached_entry);

    TEST_ASSERT_EQUAL_STRING(entry->network_id, cached_entry->network_id);

    entry = entry7;
    ret = gkc_del_attribute(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, false);
    TEST_ASSERT_FALSE(ret);

    entry = entry8;
    ret = gkc_del_attribute(entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_attribute_entry(entry, false);
    TEST_ASSERT_FALSE(ret);

    LOGI("ending test: %s", __func__);
}

void
test_flow_entry_network_id(void)
{
    struct gkc_ip_flow_interface *flow_entry;
    bool ret = false;

    LOGI("starting test: %s ...", __func__);

    flow_entry = flow_entry6;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_flow(flow_entry, false);
    TEST_ASSERT_TRUE(ret);

    flow_entry = flow_entry7;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_flow(flow_entry, false);
    TEST_ASSERT_TRUE(ret);

    flow_entry = flow_entry6;
    ret = gkc_del_flow(flow_entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_flow(flow_entry, false);
    TEST_ASSERT_FALSE(ret);

    flow_entry = flow_entry8;
    ret = gkc_add_flow_entry(flow_entry);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_lookup_flow(flow_entry, false);
    TEST_ASSERT_TRUE(ret);

    flow_entry = flow_entry7;
    ret = gkc_del_flow(flow_entry);
    TEST_ASSERT_TRUE(ret);

    flow_entry = flow_entry8;
    ret = gkc_del_flow(flow_entry);
    TEST_ASSERT_TRUE(ret);

    LOGI("ending test: %s", __func__);
}


void
test_lru_recycle(void)
{
    LOGI("starting test: %s ...", __func__);
    struct gk_attr_cache_interface *entry;
    int current_count = 0;
    size_t i;
    bool ret;

    LOGI("starting test: %s ...", __func__);
    clear_gatekeeper_cache();

    TEST_ASSERT_EQUAL_INT(0, gk_get_cache_count());
    TEST_ASSERT_EQUAL_INT(0, gk_cache_get_recycled_count());

    gkc_print_cache_entries();

    ret = gkc_add_attribute_entry(entry4);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_add_attribute_entry(entry5);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_add_attribute_entry(entry9);
    TEST_ASSERT_TRUE(ret);

    ret = gkc_add_attribute_entry(entry12);
    TEST_ASSERT_TRUE(ret);

    entry = CALLOC(1, sizeof(*entry));
    for (i = 0; i < OVER_MAX_CACHE_ENTRIES * 2; ++i)
    {
        entry->action = 1;
        entry->device_mac = str2os_mac(test_attr_entries[i].mac_str);
        entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
        entry->cache_ttl = 1000;
        entry->action = FSM_BLOCK;
        entry->attr_name = test_attr_entries[i].attr_name;

        ret = gkc_add_attribute_entry(entry);
        TEST_ASSERT_TRUE(ret);

        FREE(entry->device_mac);
    }

    gkc_print_cache_entries();

    current_count = gk_get_cache_count();
    LOGN("number of entries (1) %lu \n", gk_get_cache_count());
    TEST_ASSERT_EQUAL_INT(gk_cache_get_size(), current_count);

    for (i = 0; i < OVER_MAX_CACHE_ENTRIES * 2; ++i)
    {
        entry->action = 1;
        entry->device_mac = str2os_mac(test_attr_entries[i].mac_str);
        entry->attribute_type = GK_CACHE_REQ_TYPE_FQDN;
        entry->cache_ttl = 1000;
        entry->action = FSM_BLOCK;
        entry->attr_name = test_attr_entries[i].attr_name;
        gkc_del_attribute(entry);

        FREE(entry->device_mac);
    }

    gkc_print_cache_entries();

    LOGN("number of entries (2) %lu \n", gk_get_cache_count());
    TEST_ASSERT_EQUAL_INT(current_count - OVER_MAX_CACHE_ENTRIES, gk_get_cache_count());

    FREE(entry);
    clear_gatekeeper_cache();

    LOGI("ending test: %s", __func__);
}

static struct gk_attr_cache_interface *create_test_ipv6_entry(char *ip_str)
{
    struct gk_attr_cache_interface *attr_entry = CALLOC(1, sizeof(*attr_entry));

    /* Set other attributes */
    attr_entry->attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    attr_entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    attr_entry->action = FSM_ALLOW; /* Accept */
    attr_entry->cache_ttl = 7200;
    attr_entry->category_id = 2;
    attr_entry->confidence_level = 3;
    attr_entry->flow_marker = 2;
    attr_entry->ip_addr = sockaddr_storage_create(AF_INET6, ip_str);

    return attr_entry;
}

static struct gk_attr_cache_interface *create_test_url_entry(char *ip_str)
{
    struct gk_attr_cache_interface *attr_entry = CALLOC(1, sizeof(*attr_entry));

    /* Set other attributes */
    attr_entry->attribute_type = GK_CACHE_REQ_TYPE_URL;
    attr_entry->attr_name = STRDUP(ip_str);

    attr_entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    attr_entry->action = FSM_ALLOW; /* Accept */
    attr_entry->cache_ttl = 7200;
    attr_entry->category_id = 2;
    attr_entry->confidence_level = 3;
    attr_entry->flow_marker = 2;

    return attr_entry;
}

static struct gk_attr_cache_interface *create_test_ipv4_entry(char *ip_str)
{
    struct gk_attr_cache_interface *entry = CALLOC(1, sizeof(*entry));
    if (entry == NULL) return NULL;

    /* Set up the interface structure */
    entry->attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry->action = FSM_ALLOW; /* Accept */
    entry->cache_ttl = 3600;
    entry->category_id = 1;
    entry->confidence_level = 1;
    entry->flow_marker = 1;
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attr_name = NULL;
    entry->ip_addr = sockaddr_storage_create(AF_INET, ip_str);

    return entry;
}

void test_process_ipv4_entries_valid(void)
{
    struct attr_cache *from_cache;

    /* Create test IPv4 entries */
    char *test_ips[] = {"192.168.1.1", "10.0.0.1", "172.16.0.1"};

    for (size_t i = 0; i < sizeof(test_ips) / sizeof(test_ips[0]); i++)
    {
        struct gk_attr_cache_interface *entry = create_test_ipv4_entry(test_ips[i]);
        assert(entry != NULL);
        gkc_add_attribute_entry(entry);
        from_cache = gkc_fetch_attribute_entry(entry);
        TEST_ASSERT_NOT_NULL(from_cache);

        /* Clean up resources */
        gk_free_cache_interface_entry(entry);
        FREE(entry);
    }

    /* Create bulk reply structure */
    Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply = NULL;
    bulk_reply = CALLOC(1, sizeof(*bulk_reply));

    gatekeeper__southbound__v1__gatekeeper_bulk_reply__init(bulk_reply);

    /* Allocate space for IPv4 replies */
    bulk_reply->n_reply_ipv4 = 3;
    bulk_reply->reply_ipv4 = CALLOC(3, sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv4Reply *));

    struct gk_cache_mgr *mgr;
    ds_tree_t *device_tree;

    mgr = gk_cache_get_mgr();

    device_tree = &mgr->per_device_tree;

    /* Process IPv4 entries */
    bool result = gk_populate_bulk_reply_arrays(device_tree, bulk_reply);
    assert(result == true);

    /* validate that bulk_reply->reply_ipv4 entries are filled correctly */
    for (size_t i = 0; i < bulk_reply->n_reply_ipv4; i++)
    {
        TEST_ASSERT_NOT_NULL(bulk_reply->reply_ipv4[i]);
        TEST_ASSERT_NOT_NULL(bulk_reply->reply_ipv4[i]->header);
        TEST_ASSERT_NOT_EQUAL(0, bulk_reply->reply_ipv4[i]->addr_ipv4);
    }
    gk_cleanup_bulk_reply(bulk_reply);
}

void test_process_ipv4_entries_invalid_family(void)
{
    struct attr_cache *from_cache;

    /* Create an IPv4 entry with invalid address family */
    struct gk_attr_cache_interface *entry= create_test_ipv4_entry("192.168.1.1");
    assert(entry != NULL);
    entry->ip_addr->ss_family = AF_INET6; /* Set wrong family */

    gkc_add_attribute_entry(entry);

    from_cache = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(from_cache);

    gk_free_cache_interface_entry(entry);
    FREE(entry);

    /* Create bulk reply structure */
    Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply = CALLOC(1, sizeof(*bulk_reply));
    assert(bulk_reply != NULL);
    gatekeeper__southbound__v1__gatekeeper_bulk_reply__init(bulk_reply);

    /* Allocate space for IPv4 replies */
    bulk_reply->n_reply_ipv4 = 1;
    bulk_reply->reply_ipv4 = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv4Reply *));
    assert(bulk_reply->reply_ipv4 != NULL);

    struct gk_cache_mgr *mgr;
    ds_tree_t *device_tree;

    mgr = gk_cache_get_mgr();

    device_tree = &mgr->per_device_tree;

    /* Process IPv4 entries */
    bool result = gk_populate_bulk_reply_arrays(device_tree, bulk_reply);
    /* result should be false since it is a invalid IP family */
    assert(result == false);

    gk_cleanup_bulk_reply(bulk_reply);
}

void test_process_ipv6_entries_invalid_family(void)
{
    struct attr_cache *from_cache;

    LOGD("starting test: %s ...", __func__);

    /* Create an IPv6 entry with invalid address family */
    struct gk_attr_cache_interface *entry= create_test_ipv6_entry("2001:db8::1");
    assert(entry != NULL);
    entry->ip_addr->ss_family = AF_INET; /* Set wrong family */

    gkc_add_attribute_entry(entry);

    from_cache = gkc_fetch_attribute_entry(entry);
    TEST_ASSERT_NOT_NULL(from_cache);

    gk_free_cache_interface_entry(entry);
    FREE(entry);

    /* Create bulk reply structure */
    Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply = CALLOC(1, sizeof(*bulk_reply));
    assert(bulk_reply != NULL);
    gatekeeper__southbound__v1__gatekeeper_bulk_reply__init(bulk_reply);

    /* Allocate space for IPv6 replies */
    bulk_reply->n_reply_ipv6 = 1;
    bulk_reply->reply_ipv6 = CALLOC(1, sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv6Reply *));
    assert(bulk_reply->reply_ipv6 != NULL);

    struct gk_cache_mgr *mgr;
    ds_tree_t *device_tree;

    mgr = gk_cache_get_mgr();

    device_tree = &mgr->per_device_tree;

    /* Process IPv4 entries */
    bool result = gk_populate_bulk_reply_arrays(device_tree, bulk_reply);
    /* result should be false since it is a invalid IP family */
    assert(result == false);

    gk_cleanup_bulk_reply(bulk_reply);
}

void test_process_ipv6_entries_valid(void)
{
    struct attr_cache *from_cache;

    /* Create test IPv6 entries */
    char *test_ips[] = {"2001:db8::1", "fe80::1", "2001:4860:4860::8888"};

    LOGD("starting test: %s ...", __func__);

    for (size_t i = 0; i < sizeof(test_ips) / sizeof(test_ips[0]); i++)
    {
        struct gk_attr_cache_interface *entry = create_test_ipv6_entry(test_ips[i]);
        assert(entry != NULL);
        gkc_add_attribute_entry(entry);
        from_cache = gkc_fetch_attribute_entry(entry);
        TEST_ASSERT_NOT_NULL(from_cache);

        /* Clean up resources */
        gk_free_cache_interface_entry(entry);
        FREE(entry);
    }

    /* Create bulk reply structure */
    Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply = CALLOC(1, sizeof(*bulk_reply));
    assert(bulk_reply != NULL);
    gatekeeper__southbound__v1__gatekeeper_bulk_reply__init(bulk_reply);

    /* Allocate space for IPv6 replies */
    bulk_reply->n_reply_ipv6 = 3;
    bulk_reply->reply_ipv6 = CALLOC(3, sizeof(Gatekeeper__Southbound__V1__GatekeeperIpv4Reply *));

    struct gk_cache_mgr *mgr;
    ds_tree_t *device_tree;

    mgr = gk_cache_get_mgr();

    device_tree = &mgr->per_device_tree;

    /* Process IPv4 entries */
    bool result = gk_populate_bulk_reply_arrays(device_tree, bulk_reply);
    assert(result == true);

    /* validate that bulk_reply->reply_ipv4 entries are filled correctly */
    for (size_t i = 0; i < bulk_reply->n_reply_ipv6; i++)
    {
        TEST_ASSERT_NOT_NULL(bulk_reply->reply_ipv6[i]);
        TEST_ASSERT_NOT_NULL(bulk_reply->reply_ipv6[i]->header);
        TEST_ASSERT_NOT_EQUAL(0, bulk_reply->reply_ipv6[i]->addr_ipv6.len);
    }
    gk_cleanup_bulk_reply(bulk_reply);
}

void test_process_url_entries_valid(void)
{
    struct attr_cache *from_cache;

    LOGD("starting test: %s ...", __func__);

    /* Create test IPv4 entries */
    char *test_urls[] = {"https://example.com", "https://test.org/page.html", "http://domain.net/path/to/resource"};

    for (size_t i = 0; i < sizeof(test_urls) / sizeof(test_urls[0]); i++)
    {
        struct gk_attr_cache_interface *entry = create_test_url_entry(test_urls[i]);
        assert(entry != NULL);
        gkc_add_attribute_entry(entry);
        from_cache = gkc_fetch_attribute_entry(entry);
        TEST_ASSERT_NOT_NULL(from_cache);

        /* Clean up resources */
        gk_free_cache_interface_entry(entry);
        FREE(entry);
    }

    /* Create bulk reply structure */
    Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply = NULL;
    bulk_reply = CALLOC(1, sizeof(*bulk_reply));

    gatekeeper__southbound__v1__gatekeeper_bulk_reply__init(bulk_reply);

    /* Allocate space for URL replies */
    bulk_reply->n_reply_http_url = 3;
    bulk_reply->reply_http_url = CALLOC(3, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpUrlReply *));


    struct gk_cache_mgr *mgr;
    ds_tree_t *device_tree;

    mgr = gk_cache_get_mgr();

    device_tree = &mgr->per_device_tree;

    /* Process url entries */
    bool result = gk_populate_bulk_reply_arrays(device_tree, bulk_reply);
    assert(result == true);

    /* validate that bulk_reply->reply_http_url entries are filled correctly */
    for (size_t i = 0; i < bulk_reply->n_reply_http_url; i++)
    {
        TEST_ASSERT_NOT_NULL(bulk_reply->reply_http_url[i]);
        TEST_ASSERT_NOT_NULL(bulk_reply->reply_http_url[i]->header);
        TEST_ASSERT_NOT_NULL(bulk_reply->reply_http_url[i]->http_url);
    }
    gk_cleanup_bulk_reply(bulk_reply);
}

void test_process_hostname_entries_valid(void)
{
    Gatekeeper__Southbound__V1__GatekeeperBulkReply *bulk_reply = NULL;
    struct gk_attr_cache_interface *entry;
    struct gk_cache_mgr *mgr;
    ds_tree_t *device_tree;
    bool ret;

    LOGD("starting test: %s ...", __func__);

    /* Add an FQDN entry */
    ret = gkc_add_attribute_entry(entry3);
    TEST_ASSERT_TRUE(ret);

    /* Add an HOST entry */
    entry = entry4;
    entry->attribute_type = GK_CACHE_REQ_TYPE_HOST;  // change to HOST
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    /* Add an SNI entry */
    entry = entry6;
    entry->attribute_type = GK_CACHE_REQ_TYPE_SNI;  // change to SNI
    ret = gkc_add_attribute_entry(entry);
    TEST_ASSERT_TRUE(ret);

    /* Create bulk reply structure */

    bulk_reply = CALLOC(1, sizeof(*bulk_reply));
    gatekeeper__southbound__v1__gatekeeper_bulk_reply__init(bulk_reply);

    /* Allocate space for FQDN replies */
    bulk_reply->n_reply_fqdn = 1;
    bulk_reply->reply_fqdn = CALLOC(3, sizeof(Gatekeeper__Southbound__V1__GatekeeperFqdnReply *));

    /* Allocate space for HTTP HOST replies */
    bulk_reply->n_reply_http_host = 1;
    bulk_reply->reply_http_host = CALLOC(3, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpHostReply *));

    /* Allocate space for SNI replies */
    bulk_reply->n_reply_https_sni = 1;
    bulk_reply->reply_https_sni = CALLOC(3, sizeof(Gatekeeper__Southbound__V1__GatekeeperHttpsSniReply *));

    mgr = gk_cache_get_mgr();

    device_tree = &mgr->per_device_tree;

    /* Process fqdn entries */
    bool result = gk_populate_bulk_reply_arrays(device_tree, bulk_reply);
    assert(result == true);

    /* validate that bulk_reply->reply_http_url entries are filled correctly */
    TEST_ASSERT_NOT_NULL(bulk_reply->reply_fqdn[0]);
    TEST_ASSERT_NOT_NULL(bulk_reply->reply_fqdn[0]->header);
    TEST_ASSERT_NOT_NULL(bulk_reply->reply_fqdn[0]->query_name);

    TEST_ASSERT_NOT_NULL(bulk_reply->reply_http_host[0]);
    TEST_ASSERT_NOT_NULL(bulk_reply->reply_http_host[0]->header);
    TEST_ASSERT_NOT_NULL(bulk_reply->reply_http_host[0]->http_host);

    TEST_ASSERT_NOT_NULL(bulk_reply->reply_https_sni[0]);
    TEST_ASSERT_NOT_NULL(bulk_reply->reply_https_sni[0]->header);
    TEST_ASSERT_NOT_NULL(bulk_reply->reply_https_sni[0]->https_sni);

    gk_cleanup_bulk_reply(bulk_reply);
}

void
run_gk_cache(void)
{
    RUN_TEST(test_gkc_new_flow_entry);
    RUN_TEST(test_fqdn_redirect_copy_in_populate_cache_entry);
    RUN_TEST(test_gkc_new_attr_entry);
    RUN_TEST(test_get_attr_key);
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
    RUN_TEST(test_duplicate_entries_fqdn);
    RUN_TEST(test_duplicate_entries_ipv4);
    RUN_TEST(test_max_attr_entries);
    RUN_TEST(test_max_flow_entries);
    RUN_TEST(test_host_entry_in_fqdn);
    RUN_TEST(test_gkc_is_flow_valid);
    RUN_TEST(test_cache_size);
    RUN_TEST(test_allow_blocked_counters);
    RUN_TEST(test_gkc_add_to_cache_delete_entry);
    RUN_TEST(test_gkc_private_ipv4_attr_entry);
    RUN_TEST(test_gkc_private_ipv6_attr_entry);
    RUN_TEST(test_gkc_private_ip_flow_entry);
    RUN_TEST(test_ipv4_upsert);
    RUN_TEST(test_flow_entry_removed_from_cache_after_ttl_expires);
    RUN_TEST(test_ipv6_upsert);
    RUN_TEST(test_ipv4_upsert_action_by_name);
    RUN_TEST(test_ipv6_upsert_action_by_name);
    RUN_TEST(test_attr_entry_network_id);
    RUN_TEST(test_flow_entry_network_id);
    RUN_TEST(test_lru_recycle);
    RUN_TEST(test_process_ipv4_entries_valid);
    RUN_TEST(test_process_ipv4_entries_invalid_family);
    RUN_TEST(test_process_ipv6_entries_valid);
    RUN_TEST(test_process_ipv6_entries_invalid_family);
    RUN_TEST(test_process_url_entries_valid);
    RUN_TEST(test_process_hostname_entries_valid);
}