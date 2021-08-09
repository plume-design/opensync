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
#include <string.h>

#include "fsm_policy.h"
#include "gatekeeper_cache.h"
#include "gatekeeper_cache_cmp.h"
#include "log.h"
#include "memutil.h"
#include "unity.h"

#include "test_gatekeeper_cache.h"

/* Allow for easy cleanup of fsm_policy_rules variables */
static void
free_policy_rules(struct fsm_policy_rules *fpr)
{
    size_t i;

    if (fpr->ipaddrs)
    {
        for (i = 0; i < fpr->ipaddrs->nelems; i++)
            FREE(fpr->ipaddrs->array[i]);
        FREE(fpr->ipaddrs->array);
    }
    FREE(fpr->ipaddrs);

    if (fpr->fqdns)
    {
        for (i = 0; i < fpr->fqdns->nelems; i++)
            FREE(fpr->fqdns->array[i]);
        FREE(fpr->fqdns->array);
    }
    FREE(fpr->fqdns);

    if (fpr->apps)
    {
        for (i = 0; i < fpr->apps->nelems; i++)
            FREE(fpr->apps->array[i]);
        FREE(fpr->apps->array);
    }
    FREE(fpr->apps);

    if (fpr->macs)
    {
        for (i = 0; i < fpr->macs->nelems; i++)
            FREE(fpr->macs->array[i]);
        FREE(fpr->macs->array);
    }
    FREE(fpr->macs);
}

/*
 * Since we have performed UT on each individual comparator separately,
 * we can focus on the simplest case with "IN" and "OUT" sets.
 */

void
test_gkc_flush_all(void)
{
    struct fsm_policy_rules fpr;
    int ret, cache_entry_count;

    memset(&fpr, 0, sizeof(fpr));

    gkc_add_attribute_entry(entry1);
    gkc_add_attribute_entry(entry2);
    gkc_add_attribute_entry(entry3);
    gkc_add_attribute_entry(entry5);

    gkc_add_flow_entry(flow_entry1);

    cache_entry_count = gk_get_cache_count();
    TEST_ASSERT_EQUAL_INT(5, cache_entry_count);

    ret = gkc_flush_all();
    TEST_ASSERT_EQUAL_INT(cache_entry_count, ret);
    cache_entry_count = gk_get_cache_count();
    TEST_ASSERT_EQUAL_INT(0, cache_entry_count);
}

void
test_gkc_flush_rules_params(void)
{
    struct fsm_policy_rules fpr;
    int ret;

    /* Check for broken stuff */
    ret = gkc_flush_rules(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    /* No cache mgr yet */
    gkc_cleanup_mgr();
    memset(&fpr, 0, sizeof(fpr));
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    /* Keep tearDown() happy */
    gk_cache_init();
}

void
test_gkc_flush_rules_macs(void)
{
    struct gk_attr_cache_interface *entry;
    struct fsm_policy_rules fpr;
    int ret, cache_entry_count;

    memset(&fpr, 0, sizeof(fpr));
    entry = CALLOC(1, sizeof(*entry));

    /* entry is FQDN "www.test.com" for device AA:AA:AA:AA:AA:01 */
    /* fpr has no MAC rule, nor hostname rule  => nothing is removed */
    memcpy(entry, entry1, sizeof(*entry));
    gkc_add_attribute_entry(entry);
    cache_entry_count = gk_get_cache_count();
    TEST_ASSERT_EQUAL_INT(1, cache_entry_count);
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* fpr points to an INCORRECT MAC => nothing is removed */
    fpr.mac_rule_present = true;
    fpr.mac_op = MAC_OP_IN;
    fpr.macs = CALLOC(1, sizeof(*fpr.macs));
    fpr.macs->nelems = 2;
    fpr.macs->array = CALLOC(2, sizeof(*fpr.macs->array));
    fpr.macs->array[0] = CALLOC(32, sizeof(char));
    strcpy(fpr.macs->array[0], "AA:AA:AA:AA:AA:00");
    fpr.macs->array[1] = CALLOC(32, sizeof(char));
    strcpy(fpr.macs->array[1], "BROKEN_MAC");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* fpr points to the CORRECT MAC => everything gets removed */
    strcpy(fpr.macs->array[0], "AA:AA:AA:AA:AA:01");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* add entry again, and check OUT */
    gkc_add_attribute_entry(entry);
    fpr.mac_op = MAC_OP_OUT;
    strcpy(fpr.macs->array[0], "AA:AA:AA:AA:AA:01");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);

    strcpy(fpr.macs->array[0], "AA:AA:AA:AA:AA:00");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* cleanup */
    free_policy_rules(&fpr);
    FREE(entry);
}

void
test_gkc_flush_rules_fqdn(void)
{
    struct fsm_policy_rules fpr;
    int ret;
    memset(&fpr, 0, sizeof(fpr));

    gkc_add_attribute_entry(entry1);
    gkc_add_attribute_entry(entry2);
    gkc_add_attribute_entry(entry3);
    gkc_add_attribute_entry(entry5);

    /* we'll be testing entry3 with MAC address AA:AA:AA:AA:AA:03 */
    fpr.mac_rule_present = true;
    fpr.mac_op = MAC_OP_IN;
    fpr.macs = CALLOC(1, sizeof(*fpr.macs));
    fpr.macs->nelems = 1;
    fpr.macs->array = CALLOC(1, sizeof(*fpr.macs->array));
    fpr.macs->array[0] = STRDUP("AA:AA:AA:AA:AA:03");

    fpr.fqdn_rule_present = true;
    fpr.fqdn_op = FQDN_OP_IN;

    /* Test empty set */
    fpr.fqdns = NULL;
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);
    FREE(fpr.fqdns);

    /* Now have a set of FQDNs */
    fpr.fqdns = CALLOC(1, sizeof(*fpr.fqdns));
    fpr.fqdns->nelems = 1;
    fpr.fqdns->array = CALLOC(1, sizeof(*fpr.fqdns->array));
    fpr.fqdns->array[0] = CALLOC(64, sizeof(char));

    /* This name is not present */
    strcpy(fpr.fqdns->array[0], "www.FOO.com");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* This hostname is present */
    strcpy(fpr.fqdns->array[0], "www.entr3.com");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* re-add the entry */
    gkc_add_attribute_entry(entry3);
    /* Now wirh _OUT */
    fpr.fqdn_op = FQDN_OP_OUT;
    strcpy(fpr.fqdns->array[0], "www.entr3.com");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);

    strcpy(fpr.fqdns->array[0], "www.FOO.com");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* Cleanup */
    free_policy_rules(&fpr);
}

void
test_gkc_flush_rules_app(void)
{
    struct fsm_policy_rules fpr;
    int ret;
    memset(&fpr, 0, sizeof(fpr));

    gkc_add_attribute_entry(entry1);
    gkc_add_attribute_entry(entry2);
    gkc_add_attribute_entry(entry3);
    gkc_add_attribute_entry(entry5);

    /* we'll be testing entry5 with MAC address AA:AA:AA:AA:AA:04 */
    fpr.mac_rule_present = true;
    fpr.mac_op = MAC_OP_IN;
    fpr.macs = CALLOC(1, sizeof(*fpr.macs));
    fpr.macs->nelems = 1;
    fpr.macs->array = CALLOC(1, sizeof(*fpr.macs->array));
    fpr.macs->array[0] = STRDUP("AA:AA:AA:AA:AA:04");

    fpr.app_rule_present = true;
    fpr.app_op = APP_OP_IN;

    /* Test empty set */
    fpr.apps = NULL;
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);
    FREE(fpr.apps);

    fpr.apps = CALLOC(1, sizeof(*fpr.apps));
    fpr.apps->nelems = 1;
    fpr.apps->array = CALLOC(1, sizeof(*fpr.apps->array));
    fpr.apps->array[0] = CALLOC(64, sizeof(char));

    /* This name is not present */
    strcpy(fpr.apps->array[0], "wrong_app_name");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* This hostname is present */
    strcpy(fpr.apps->array[0], entry5->attr_name);
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* re-add the entry */
    gkc_add_attribute_entry(entry5);

    /* Now wirh _OUT */
    fpr.app_op = APP_OP_OUT;
    strcpy(fpr.apps->array[0], entry5->attr_name);
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);

    strcpy(fpr.apps->array[0], "another_app_name");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(1, ret);

    /* Cleanup */
    free_policy_rules(&fpr);
}

static void
insert_ipv4_and_ipv6(void)
{
    struct gk_attr_cache_interface *entry;
    struct fsm_policy_rules fpr;
    struct sockaddr_in6 *in6;
    struct sockaddr_in *in4;
    struct in6_addr in_ip6;
    struct in_addr in_ip;

    memset(&fpr, 0, sizeof(fpr));

    entry = CALLOC(1, sizeof(*entry));
    entry->action = 1;
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:01");
    entry->attribute_type = GK_CACHE_REQ_TYPE_IPV4;
    entry->cache_ttl = 1000;
    entry->action = FSM_BLOCK;
    entry->attr_name = STRDUP("1.2.3.4");
    /* Populate the IPv4 */
    inet_pton(AF_INET, entry->attr_name, &in_ip);
    entry->ip_addr = CALLOC(1, sizeof(*entry->ip_addr));
    in4 = (struct sockaddr_in *)entry->ip_addr;
    in4->sin_family = AF_INET;
    memcpy(&in4->sin_addr, &in_ip, sizeof(in4->sin_addr));

    gkc_add_attribute_entry(entry);
    FREE(entry->ip_addr);
    FREE(entry->attr_name);
    FREE(entry->device_mac);

    entry->action = 1;
    entry->device_mac = str2os_mac("AA:AA:AA:AA:AA:02");
    entry->attribute_type = GK_CACHE_REQ_TYPE_IPV6;
    entry->cache_ttl = 1000;
    entry->action = FSM_BLOCK;
    entry->attr_name = STRDUP("2001:0000:3238:DFE1:0063:0000:0000:FEFB");
    /* Populate the IPv6 */
    entry->ip_addr = CALLOC(1, sizeof(*entry->ip_addr));
    inet_pton(AF_INET6, entry->attr_name, &in_ip6);
    in6 = (struct sockaddr_in6 *)entry->ip_addr;
    in6->sin6_family = AF_INET6;
    memcpy(&in6->sin6_addr, &in_ip6, sizeof(in6->sin6_addr));

    gkc_add_attribute_entry(entry);
    FREE(entry->ip_addr);
    FREE(entry->attr_name);
    FREE(entry->device_mac);
    FREE(entry);
}

void
test_gkc_flush_ipv4_and_ipv6(void)
{
    struct fsm_policy_rules fpr;
    int ret;

    insert_ipv4_and_ipv6();
    gkc_print_cache_entries();

    memset(&fpr, 0, sizeof(fpr));

    /* Perform this across ALL devices */
    /* Start with OUT and check for everything. Nothing should be deleted */
    fpr.ip_rule_present = true;
    fpr.ip_op = IP_OP_OUT;
    fpr.ipaddrs = CALLOC(1, sizeof(*fpr.ipaddrs));
    fpr.ipaddrs->nelems = 4;
    fpr.ipaddrs->array = CALLOC(4, sizeof(*fpr.ipaddrs->array));
    fpr.ipaddrs->array[0] = CALLOC(64, sizeof(char));
    strcpy(fpr.ipaddrs->array[0], "1.2.3.4");
    fpr.ipaddrs->array[1] = CALLOC(64, sizeof(char));
    strcpy(fpr.ipaddrs->array[1], "127.0.0.1");
    fpr.ipaddrs->array[2] = CALLOC(64, sizeof(char));
    strcpy(fpr.ipaddrs->array[2], "2001:0000:3238:DFE1:0063:0000:0000:FEFB");
    fpr.ipaddrs->array[3] = CALLOC(64, sizeof(char));
    strcpy(fpr.ipaddrs->array[3], "2001:0000:3238:DFE1:0063:0000:0000:ABCD");
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Now with IN, we should delete the 2 entries we added */
    fpr.ip_op = IP_OP_IN;
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(2, ret);

    /* Cleanup */
    free_policy_rules(&fpr);
}

void
test_gkc_flush_ipv4_and_ipv6_empty_ip_set(void)
{
    struct fsm_policy_rules fpr;
    int ret;

    memset(&fpr, 0, sizeof(fpr));

    insert_ipv4_and_ipv6();
    gkc_print_cache_entries();

    memset(&fpr, 0, sizeof(fpr));

    /* Perform this across ALL devices */
    /* Start with IN a NULL/empty ip set. Nothing should be deleted */
    fpr.ip_rule_present = true;
    fpr.ip_op = IP_OP_IN;

    /* Test empty set */
    fpr.ipaddrs = NULL;
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(0, ret);
    FREE(fpr.ipaddrs);
    fpr.ipaddrs = NULL;
    
    /* Now with OUT of the empty set, we should delete the 2 entries we added */
    fpr.ip_op = IP_OP_OUT;
    ret = gkc_flush_rules(&fpr);
    TEST_ASSERT_EQUAL_INT(2, ret);

    /* Cleanup */
    free_policy_rules(&fpr);
}

void
test_gkc_flush_client(void)
{
    struct fsm_session session;
    struct fsm_policy policy;
    int ret;

    session.name = "my_session";

    ret = gkc_flush_client(NULL, NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    memset(&policy, 0, sizeof(policy));
    ret = gkc_flush_client(NULL, &policy);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    policy.action = FSM_FLUSH_ALL_CACHE;
    ret = gkc_flush_client(NULL, &policy);
    TEST_ASSERT_EQUAL_INT(0, ret);

    policy.action = FSM_FLUSH_CACHE;
    ret = gkc_flush_client(&session, &policy);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void
run_gk_cache_flush(void)
{
    RUN_TEST(test_gkc_flush_all);
    RUN_TEST(test_gkc_flush_rules_params);
    RUN_TEST(test_gkc_flush_rules_macs);
    RUN_TEST(test_gkc_flush_rules_fqdn);
    RUN_TEST(test_gkc_flush_rules_app);
    RUN_TEST(test_gkc_flush_ipv4_and_ipv6);
    RUN_TEST(test_gkc_flush_ipv4_and_ipv6_empty_ip_set);
    RUN_TEST(test_gkc_flush_client);
}
