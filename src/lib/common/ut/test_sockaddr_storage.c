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
#include <stdbool.h>

#include "memutil.h"
#include "os.h"
#include "sockaddr_storage.h"
#include "unity.h"

void
test_sockaddr_storage_create(void)
{
    struct sockaddr_storage *ip_v4;
    struct sockaddr_storage *ip_v6;
    struct sockaddr_storage *ip_broken;
    struct sockaddr_in6 *in_ip6;
    struct sockaddr_in *in_ip4;
    char *ip_v4_str = "1.2.3.4";
    char *ip_v6_str = "fe80::225:90ff:fe87:175d";
    char ip_str[INET6_ADDRSTRLEN];

    ip_v4 = sockaddr_storage_create(AF_INET, ip_v4_str);
    in_ip4 = (struct sockaddr_in *)ip_v4;
    inet_ntop(AF_INET, &in_ip4->sin_addr, ip_str, INET_ADDRSTRLEN);
    TEST_ASSERT_EQUAL_STRING(ip_v4_str, ip_str);

    ip_v6 = sockaddr_storage_create(AF_INET6, ip_v6_str);
    in_ip6 = (struct sockaddr_in6 *)ip_v6;
    inet_ntop(AF_INET6, &in_ip6->sin6_addr, ip_str, INET6_ADDRSTRLEN);
    TEST_ASSERT_EQUAL_STRING(ip_v6_str, ip_str);

    /* Break the af_family */
    ip_broken = sockaddr_storage_create(0, ip_v6_str);
    TEST_ASSERT_NULL(ip_broken);

    FREE(ip_v6);
    FREE(ip_v4);
}

void
test_sockaddr_storage_equals(void)
{
    char ipv6_1[] = "2001:0000:3238:DFE1:0063:0000:0000:FEFB";
    char ipv6_2[] = "2001:0000:3238:DFE1:0063:0000:0000:FFFF";
    char ipv4_1[] = "1.2.3.4";
    char ipv4_2[] = "1.2.3.255";
    struct sockaddr_storage *ss_ipv4_1;
    struct sockaddr_storage *ss_ipv4_2;
    struct sockaddr_storage *ss_ipv6_1;
    struct sockaddr_storage *ss_ipv6_2;
    bool ret;

    ss_ipv4_1 = sockaddr_storage_create(AF_INET, ipv4_1);
    ss_ipv4_2 = sockaddr_storage_create(AF_INET, ipv4_2);
    ss_ipv6_1 = sockaddr_storage_create(AF_INET6, ipv6_1);
    ss_ipv6_2 = sockaddr_storage_create(AF_INET6, ipv6_2);

    /* find comparator now (IN or OUT should behave the same) */
    ret = sockaddr_storage_equals(ss_ipv4_1, ss_ipv4_1);
    TEST_ASSERT_TRUE(ret);
    ret = sockaddr_storage_equals(ss_ipv4_1, ss_ipv4_2);
    TEST_ASSERT_FALSE(ret);
    ret = sockaddr_storage_equals(ss_ipv6_1, ss_ipv6_1);
    TEST_ASSERT_TRUE(ret);
    ret = sockaddr_storage_equals(ss_ipv6_1, ss_ipv6_2);
    TEST_ASSERT_FALSE(ret);
    ret = sockaddr_storage_equals(ss_ipv4_1, ss_ipv6_1);
    TEST_ASSERT_FALSE(ret);
    ret = sockaddr_storage_equals(ss_ipv6_1, ss_ipv4_1);
    TEST_ASSERT_FALSE(ret);

    /* Break the af_family */
    ss_ipv6_1->ss_family = 0;
    ret = sockaddr_storage_equals(ss_ipv6_1, ss_ipv6_1);
    TEST_ASSERT_FALSE(ret);

    FREE(ss_ipv6_2);
    FREE(ss_ipv6_1);
    FREE(ss_ipv4_2);
    FREE(ss_ipv4_1);
}

void
test_sockaddr_storage_equals_addr(void)
{
    struct sockaddr_storage *ip_v4;
    struct sockaddr_storage *ip_v6;
    struct in6_addr in_ip6;
    struct in_addr in_ip;
    char *ip_v4_str = "1.2.3.4";
    char *ip_v6_str = "fe80::225:90ff:fe87:175d";
    bool rc;

    ip_v4 = sockaddr_storage_create(AF_INET, ip_v4_str);
    inet_pton(AF_INET, ip_v4_str, &in_ip);
    rc = sockaddr_storage_equals_addr(ip_v4, (uint8_t *)&in_ip.s_addr, 4);
    TEST_ASSERT_TRUE(rc);

    ip_v6 = sockaddr_storage_create(AF_INET6, ip_v6_str);
    inet_pton(AF_INET6, ip_v6_str, &in_ip6);
    rc = sockaddr_storage_equals_addr(ip_v6, (uint8_t *)&in_ip6, 16);
    TEST_ASSERT_TRUE(rc);

    rc = sockaddr_storage_equals_addr(ip_v4, (uint8_t *)&in_ip6, 16);
    TEST_ASSERT_FALSE(rc);
    rc = sockaddr_storage_equals_addr(ip_v6, (uint8_t *)&in_ip, 4);
    TEST_ASSERT_FALSE(rc);

    /* Break the af_family */
    ip_v4->ss_family = 0;
    rc = sockaddr_storage_equals_addr(ip_v4, (uint8_t *)&in_ip.s_addr, 4);
    TEST_ASSERT_FALSE(rc);

    FREE(ip_v6);
    FREE(ip_v4);
}

void
test_sockaddr_storage_populate(void)
{
    struct sockaddr_storage *ip_v4;
    struct sockaddr_storage *ip_v6;
    struct sockaddr_storage *dest;
    struct sockaddr_in6 *in6;
    char *ip_v4_str = "1.2.3.4";
    char *ip_v6_str = "fe80::225:90ff:fe87:175d";
    uint32_t ipv4 = 0x04030201;
    bool rc;
    int cmp;

    dest = CALLOC(1, sizeof(*dest));

    /* IPv4 first */
    ip_v4 = sockaddr_storage_create(AF_INET, ip_v4_str);
    sockaddr_storage_populate(AF_INET, &ipv4, dest);
    rc = sockaddr_storage_equals(dest, ip_v4);
    TEST_ASSERT_TRUE(rc);

    /* IPv6 needs to be built a bit... */
    ip_v6 = sockaddr_storage_create(AF_INET6, ip_v6_str);
    /* get the IPv6 address in there in binary form */
    in6 = (struct sockaddr_in6 *)ip_v6;
    sockaddr_storage_populate(AF_INET6, &in6->sin6_addr, dest);
    rc = sockaddr_storage_equals(dest, ip_v6);
    TEST_ASSERT_TRUE(rc);

    /* Finish with the corner case */
    MEMZERO(*dest);
    sockaddr_storage_populate(AF_INET, NULL, ip_v4);
    MEMZERO(*dest);
    cmp = memcmp(dest, ip_v4, sizeof(*dest));
    TEST_ASSERT_EQUAL(0, cmp);

    /* Cleanup */
    FREE(ip_v6);
    FREE(ip_v4);
    FREE(dest);
}

void
test_sockaddr_storage_copy(void)
{
    struct sockaddr_storage *ip_v4_1;
    struct sockaddr_storage *ip_v4_2;
    struct sockaddr_storage *ip_v6_1;
    struct sockaddr_storage *ip_v6_2;
    struct sockaddr_storage *dest;
    char *ip_v4_str_1 = "1.2.3.4";
    char *ip_v4_str_2 = "127.0.0.1";
    char *ip_v6_str_1 = "fe80::225:90ff:fe87:175d";
    char *ip_v6_str_2 = "fe80::1225:90ff:fe88:175d";
    char output_1[128];
    char output_2[128];
    bool rc;
    int cmp;

    dest = CALLOC(1, sizeof(*dest));

    ip_v4_1 = sockaddr_storage_create(AF_INET, ip_v4_str_1);
    ip_v4_2 = sockaddr_storage_create(AF_INET, ip_v4_str_2);
    ip_v6_1 = sockaddr_storage_create(AF_INET6, ip_v6_str_1);
    ip_v6_2 = sockaddr_storage_create(AF_INET6, ip_v6_str_2);

    sockaddr_storage_copy(ip_v4_1, dest);
    rc = sockaddr_storage_equals(dest, ip_v4_1);
    TEST_ASSERT_TRUE(rc);
    rc = sockaddr_storage_equals(dest, ip_v4_2);
    TEST_ASSERT_FALSE(rc);
    rc = sockaddr_storage_equals(dest, ip_v6_2);
    TEST_ASSERT_FALSE(rc);

    sockaddr_storage_copy(ip_v6_1, dest);
    rc = sockaddr_storage_equals(dest, ip_v6_1);
    TEST_ASSERT_TRUE(rc);
    rc = sockaddr_storage_equals(dest, ip_v6_2);
    TEST_ASSERT_FALSE(rc);
    rc = sockaddr_storage_equals(dest, ip_v4_2);
    TEST_ASSERT_FALSE(rc);

    /* make sure we don't crash */
    sockaddr_storage_copy(dest, NULL);

    /* copy NULL should leave destination ZEROED OUT */
    sockaddr_storage_copy(NULL, ip_v4_1);
    MEMZERO(*dest);
    cmp = memcmp(dest, ip_v4_1, sizeof(*dest));
    TEST_ASSERT_EQUAL(0, cmp);

    /* Check output order */
    sockaddr_storage_str(ip_v4_2, output_1);
    sockaddr_storage_copy(ip_v4_2, dest);
    sockaddr_storage_str(dest, output_2);
    TEST_ASSERT_EQUAL_STRING(output_1, output_2);

    sockaddr_storage_str(ip_v6_2, output_1);
    sockaddr_storage_copy(ip_v6_2, dest);
    sockaddr_storage_str(dest, output_2);
    TEST_ASSERT_EQUAL_STRING(output_1, output_2);

    /* cleanup */
    FREE(ip_v6_2);
    FREE(ip_v6_1);
    FREE(ip_v4_2);
    FREE(ip_v4_1);
    FREE(dest);
}

void
run_test_sockaddr_storage(void)
{
    RUN_TEST(test_sockaddr_storage_create);
    RUN_TEST(test_sockaddr_storage_equals);
    RUN_TEST(test_sockaddr_storage_equals_addr);
    RUN_TEST(test_sockaddr_storage_populate);
    RUN_TEST(test_sockaddr_storage_copy);
}
