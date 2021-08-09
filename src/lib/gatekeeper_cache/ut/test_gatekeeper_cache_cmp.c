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
#include "fsm_utils.h"
#include "gatekeeper_cache.h"
#include "gatekeeper_cache_cmp.h"
#include "log.h"
#include "memutil.h"
#include "unity.h"

#include "test_gatekeeper_cache.h"

void
test_mac_comparator(void)
{
    mac_comparator mac_cmp_fct;
    os_macaddr_t *a, *b, *c;
    bool ret;

    /* have some valid values */
    a = str2os_mac("aa:bb:cc:dd:ee:ff");
    b = str2os_mac("00:11:22:33:44:55");
    c = a;

    /* should return a blank comparator ALWAYS true */
    mac_cmp_fct = get_mac_cmp(-1);
    ret = mac_cmp_fct(a, b);
    TEST_ASSERT_TRUE(ret);
    ret = mac_cmp_fct(a, c);
    TEST_ASSERT_TRUE(ret);

    /* find comparator now (IN or OUT should behave the same) */
    mac_cmp_fct = get_mac_cmp(MAC_OP_IN);
    ret = mac_cmp_fct(a, b);
    TEST_ASSERT_FALSE(ret);
    ret = mac_cmp_fct(a, c);
    TEST_ASSERT_TRUE(ret);

    mac_cmp_fct = get_mac_cmp(MAC_OP_OUT);
    ret = mac_cmp_fct(a, b);
    TEST_ASSERT_FALSE(ret);

    ret = mac_cmp_fct(a, c);
    TEST_ASSERT_TRUE(ret);

    /* Cleanup */
    FREE(b);
    FREE(a);
}

void
test_hostname_comparator(void)
{
    hostname_comparator hostname_cmp_fct;
    char start_with[] = "start";
    char start_1[]    = "start_1";
    char end_with[]   = "end";
    char end_1[]      = "1_end";
    char wild[]       = "s*t*";
    char wild_err[]   = "s[t";  /* broken regex */
    bool ret;

    /* should return a blank comparator */
    hostname_cmp_fct = get_hostname_cmp(-1);
    ret = hostname_cmp_fct("foo", "bar");
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct("bar", "bar");
    TEST_ASSERT_TRUE(ret);

    /* Check each possible option (IN or out should behave the same) */
    /* all the _IN ... */
    hostname_cmp_fct = get_hostname_cmp(FQDN_OP_IN);
    ret = hostname_cmp_fct(start_with, start_with);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(start_with, end_with);
    TEST_ASSERT_FALSE(ret);

    hostname_cmp_fct = get_hostname_cmp(FQDN_OP_SFL_IN);
    ret = hostname_cmp_fct(start_with, start_with);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(start_1, start_with);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(start_with, end_with);
    TEST_ASSERT_FALSE(ret);

    hostname_cmp_fct = get_hostname_cmp(FQDN_OP_SFR_IN);
    ret = hostname_cmp_fct(end_with, end_with);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(end_1, end_with);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(start_with, end_with);
    TEST_ASSERT_FALSE(ret);

    hostname_cmp_fct = get_hostname_cmp(FQDN_OP_WILD_IN);
    ret = hostname_cmp_fct(start_1, wild);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(start_with, wild);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(end_1, wild);
    TEST_ASSERT_FALSE(ret);

    /* ... and all the _OUT */
    hostname_cmp_fct = get_hostname_cmp(FQDN_OP_OUT);
    ret = hostname_cmp_fct(start_with, start_with);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(start_with, end_with);
    TEST_ASSERT_FALSE(ret);

    hostname_cmp_fct = get_hostname_cmp(FQDN_OP_SFL_OUT);
    ret = hostname_cmp_fct(start_with, start_with);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(start_1, start_with);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(start_with, end_with);
    TEST_ASSERT_FALSE(ret);

    hostname_cmp_fct = get_hostname_cmp(FQDN_OP_SFR_OUT);
    ret = hostname_cmp_fct(end_with, end_with);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(end_1, end_with);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(start_with, end_with);
    TEST_ASSERT_FALSE(ret);

    hostname_cmp_fct = get_hostname_cmp(FQDN_OP_WILD_OUT);
    ret = hostname_cmp_fct(start_1, wild);
    TEST_ASSERT_TRUE(ret);
    ret = hostname_cmp_fct(end_1, wild);
    TEST_ASSERT_FALSE(ret);
    ret = hostname_cmp_fct(end_1, wild_err);
    TEST_ASSERT_FALSE(ret);
}

void
test_ip_comparator(void)
{
    char ipv6_1[] = "2001:0000:3238:DFE1:0063:0000:0000:FEFB";
    char ipv6_2[] = "2001:0000:3238:DFE1:0063:0000:0000:FFFF";
    char ipv4_1[] = "1.2.3.4";
    char ipv4_2[] = "1.2.3.255";
    struct sockaddr_storage *ss_ipv4_1;
    struct sockaddr_storage *ss_ipv4_2;
    struct sockaddr_storage *ss_ipv6_1;
    struct sockaddr_storage *ss_ipv6_2;
    ip_comparator ip_cmp_fct;
    bool ret;

    ss_ipv4_1 = sockaddr_storage_create(AF_INET, ipv4_1);
    ss_ipv4_2 = sockaddr_storage_create(AF_INET, ipv4_2);
    ss_ipv6_1 = sockaddr_storage_create(AF_INET6, ipv6_1);
    ss_ipv6_2 = sockaddr_storage_create(AF_INET6, ipv6_2);

    /* should return a blank comparator */
    ip_cmp_fct = get_ip_cmp(-1);
    ret = ip_cmp_fct(ss_ipv4_1, ss_ipv4_1);
    TEST_ASSERT_TRUE(ret);
    ret = ip_cmp_fct(ss_ipv6_1, ss_ipv6_1);
    TEST_ASSERT_TRUE(ret);
    ret = ip_cmp_fct(ss_ipv4_1, ss_ipv6_1);
    TEST_ASSERT_TRUE(ret);
    ret = ip_cmp_fct(ss_ipv6_1, ss_ipv4_1);
    TEST_ASSERT_TRUE(ret);

    /* find comparator now (IN or OUT should behave the same) */
    ip_cmp_fct = get_ip_cmp(IP_OP_IN);
    ret = ip_cmp_fct(ss_ipv4_1, ss_ipv4_1);
    TEST_ASSERT_TRUE(ret);
    ret = ip_cmp_fct(ss_ipv4_1, ss_ipv4_2);
    TEST_ASSERT_FALSE(ret);
    ret = ip_cmp_fct(ss_ipv6_1, ss_ipv6_1);
    TEST_ASSERT_TRUE(ret);
    ret = ip_cmp_fct(ss_ipv6_1, ss_ipv6_2);
    TEST_ASSERT_FALSE(ret);
    ret = ip_cmp_fct(ss_ipv4_1, ss_ipv6_1);
    TEST_ASSERT_FALSE(ret);
    ret = ip_cmp_fct(ss_ipv6_1, ss_ipv4_1);
    TEST_ASSERT_FALSE(ret);

    ip_cmp_fct = get_ip_cmp(IP_OP_OUT);
    ret = ip_cmp_fct(ss_ipv4_1, ss_ipv4_1);
    TEST_ASSERT_TRUE(ret);
    ret = ip_cmp_fct(ss_ipv4_1, ss_ipv4_2);
    TEST_ASSERT_FALSE(ret);
    ret = ip_cmp_fct(ss_ipv6_1, ss_ipv6_1);
    TEST_ASSERT_TRUE(ret);
    ret = ip_cmp_fct(ss_ipv6_1, ss_ipv6_2);
    TEST_ASSERT_FALSE(ret);
    ret = ip_cmp_fct(ss_ipv4_1, ss_ipv6_1);
    TEST_ASSERT_FALSE(ret);
    ret = ip_cmp_fct(ss_ipv6_1, ss_ipv4_1);
    TEST_ASSERT_FALSE(ret);

    FREE(ss_ipv6_2);
    FREE(ss_ipv6_1);
    FREE(ss_ipv4_2);
    FREE(ss_ipv4_1);
}

void
test_app_comparator(void)
{
    app_comparator app_cmp_fct;
    bool ret;

    /* should return a blank comparator */
    app_cmp_fct = get_app_cmp(-1);
    ret = app_cmp_fct("foo", "bar");
    TEST_ASSERT_TRUE(ret);
    ret = app_cmp_fct("bar", "bar");
    TEST_ASSERT_TRUE(ret);

    /* find comparator now (IN or OUT should behave the same) */
    app_cmp_fct = get_app_cmp(APP_OP_IN);
    ret = app_cmp_fct("foo", "bar");
    TEST_ASSERT_FALSE(ret);
    ret = app_cmp_fct("bar", "bar");
    TEST_ASSERT_TRUE(ret);

    app_cmp_fct = get_app_cmp(APP_OP_OUT);
    ret = app_cmp_fct("foo", "bar");
    TEST_ASSERT_FALSE(ret);
    ret = app_cmp_fct("bar", "bar");
    TEST_ASSERT_TRUE(ret);
}

void
test_cat_comparator(void)
{
    cat_comparator cat_cmp_fct;
    struct int_set cat_set;
    bool ret;

    /* Set up the category set */
    cat_set.array = CALLOC(10, sizeof(*cat_set.array));
    cat_set.array[0] = 1;
    cat_set.array[1] = 3;
    cat_set.array[2] = 5;
    cat_set.nelems = 3;

    /* should return a blank comparator */
    cat_cmp_fct = get_cat_cmp(-1);
    ret = cat_cmp_fct(2, &cat_set);
    TEST_ASSERT_TRUE(ret);
    ret = cat_cmp_fct(5, &cat_set);
    TEST_ASSERT_TRUE(ret);

    /* Test the empty set */
    cat_cmp_fct = get_cat_cmp(CAT_OP_IN);
    ret = cat_cmp_fct(2, NULL);
    TEST_ASSERT_FALSE(ret);
     
    cat_cmp_fct = get_cat_cmp(CAT_OP_OUT);
    ret = cat_cmp_fct(2, NULL);
    TEST_ASSERT_TRUE(ret);
  
    /* IN and OUT do not behave the same !! */
    /* CAT_OP_IN */
    cat_cmp_fct = get_cat_cmp(CAT_OP_IN);
    ret = cat_cmp_fct(2, &cat_set);
    TEST_ASSERT_FALSE(ret);
    ret = cat_cmp_fct(5, &cat_set);
    TEST_ASSERT_TRUE(ret);

    /* CAT_OP_OUT */
    cat_cmp_fct = get_cat_cmp(CAT_OP_OUT);
    ret = cat_cmp_fct(2, &cat_set);
    TEST_ASSERT_TRUE(ret);
    ret = cat_cmp_fct(5, &cat_set);
    TEST_ASSERT_FALSE(ret);

    /* Cleanup */
    FREE(cat_set.array);
}

void
test_risk_comparator(void)
{
    risk_comparator risk_cmp_fct;
    bool ret;

    /* should return a blank comparator */
    risk_cmp_fct = get_risk_cmp(-1);
    ret = risk_cmp_fct(1, 2);
    TEST_ASSERT_TRUE(ret);
    ret = risk_cmp_fct(2, 2);
    TEST_ASSERT_TRUE(ret);

    /* go thru each possible operation */
    risk_cmp_fct = get_risk_cmp(RISK_OP_EQ);
    ret = risk_cmp_fct(1, 2);
    TEST_ASSERT_FALSE(ret);
    ret = risk_cmp_fct(2, 2);
    TEST_ASSERT_TRUE(ret);

    risk_cmp_fct = get_risk_cmp(RISK_OP_NEQ);
    ret = risk_cmp_fct(1, 2);
    TEST_ASSERT_TRUE(ret);
    ret = risk_cmp_fct(2, 2);
    TEST_ASSERT_FALSE(ret);

    risk_cmp_fct = get_risk_cmp(RISK_OP_LT);
    ret = risk_cmp_fct(1, 2);
    TEST_ASSERT_TRUE(ret);
    ret = risk_cmp_fct(2, 2);
    TEST_ASSERT_FALSE(ret);
    ret = risk_cmp_fct(3, 2);
    TEST_ASSERT_FALSE(ret);

    risk_cmp_fct = get_risk_cmp(RISK_OP_GT);
    ret = risk_cmp_fct(1, 2);
    TEST_ASSERT_FALSE(ret);
    ret = risk_cmp_fct(2, 2);
    TEST_ASSERT_FALSE(ret);
    ret = risk_cmp_fct(3, 2);
    TEST_ASSERT_TRUE(ret);

    risk_cmp_fct = get_risk_cmp(RISK_OP_LTE);
    ret = risk_cmp_fct(1, 2);
    TEST_ASSERT_TRUE(ret);
    ret = risk_cmp_fct(2, 2);
    TEST_ASSERT_TRUE(ret);
    ret = risk_cmp_fct(3, 2);
    TEST_ASSERT_FALSE(ret);

    risk_cmp_fct = get_risk_cmp(RISK_OP_GTE);
    ret = risk_cmp_fct(1, 2);
    TEST_ASSERT_FALSE(ret);
    ret = risk_cmp_fct(2, 2);
    TEST_ASSERT_TRUE(ret);
    ret = risk_cmp_fct(3, 2);
    TEST_ASSERT_TRUE(ret);
}

/* Implemented in gatekeeper_cache.c */
void
test_gkc_flow_entry_cmp(void)
{
    struct ip_flow_cache a;
    struct ip_flow_cache b;
    int ret;

    MEMZERO(a);
    MEMZERO(b);

    a.ip_version = 4;
    a.src_ip_addr = (unsigned char*)"1234";
    a.dst_ip_addr = (unsigned char*)"1234";

    b.ip_version = 4;
    b.src_ip_addr = (unsigned char*)"1234";
    b.dst_ip_addr = (unsigned char*)"1234";

    ret = gkc_flow_entry_cmp(&a, &b);
    TEST_ASSERT_EQUAL_INT(0, ret);

    a.ip_version = 6;
    a.src_ip_addr = (unsigned char*)"01234567890abcdef";
    a.dst_ip_addr = (unsigned char*)"01234567890abcdef";

    b.ip_version = 6;
    b.src_ip_addr = (unsigned char*)"01234567890abcdef";
    b.dst_ip_addr = (unsigned char*)"01234567890abcdef";

    ret = gkc_flow_entry_cmp(&a, &b);
    TEST_ASSERT_EQUAL_INT(0, ret);

    a.ip_version = 5;
    a.src_ip_addr = (unsigned char*)"12345";
    a.dst_ip_addr = (unsigned char*)"1234f";    /* This should still be equal !! */

    b.ip_version = 5;
    b.src_ip_addr = (unsigned char*)"12345";
    b.dst_ip_addr = (unsigned char*)"12345";

    ret = gkc_flow_entry_cmp(&a, &b);
    TEST_ASSERT_EQUAL_INT(0, ret);
}


void
run_gk_cache_cmp(void)
{
    RUN_TEST(test_mac_comparator);
    RUN_TEST(test_hostname_comparator);
    RUN_TEST(test_ip_comparator);
    RUN_TEST(test_app_comparator);
    RUN_TEST(test_cat_comparator);
    RUN_TEST(test_risk_comparator);

    RUN_TEST(test_gkc_flow_entry_cmp);
}
