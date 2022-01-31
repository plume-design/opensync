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

#include "fsm.h"
#include "fsm_dpi_dns.h"
#include "fsm_dpi_sni.h"
#include "os.h"
#include "unity.h"
#include "unit_test_utils.h"

#include "pcap.c"

struct schema_Openflow_Tag g_tags[] =
{
    {
        .name_exists = true,
        .name = "regular_tag_1",
    },
};


struct schema_Openflow_Local_Tag g_ltags[] =
{
    {
        .name_exists = true,
        .name = "upd_v4_tag",
    },
    {
        .name_exists = true,
        .name = "upd_v6_tag",
    }
};

char *begin = "begin";
char *dns_qname = "dns.qname";
char *dns_type = "dns.type";
char *dns_ttl = "dns.ttl";
char *dns_a = "dns.a";
char *dns_a_offset = "dns.a_offset";
char *dns_aaaa = "dns.aaaa";
char *dns_aaaa_offset = "dns.aaaa_offset";
char *end = "end";

char *fqdn = "www.google.com";
char *ipv4 = "abcd";
char *ipv6 = "0123456789abcdef";
int64_t type = 65;
int64_t ttl = 300;
int64_t offset_v4 = 100;
int64_t offset_v6 = 300;


void
test_fsm_dpi_sni_init_exit(void)
{
    struct fsm_session sess;
    bool rc;

    rc = fsm_dpi_dns_init(NULL);
    TEST_ASSERT_FALSE(rc);

    rc = fsm_dpi_dns_exit(NULL);
    TEST_ASSERT_TRUE(rc);

    sess.name = "TESTING";
    rc = fsm_dpi_dns_init(&sess);
    TEST_ASSERT_TRUE(rc);

}

void
test_fsm_dpi_sni_process_attr_wrong_type(void)
{
    int ret;

    ret = fsm_dpi_dns_process_attr(NULL, begin, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, begin, RTS_TYPE_STRING, strlen(fqdn), fqdn, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    ret = fsm_dpi_dns_process_attr(NULL, end, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, end, RTS_TYPE_STRING, strlen(fqdn), fqdn, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    ret = fsm_dpi_dns_process_attr(NULL, dns_qname, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_a, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_aaaa, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_type, RTS_TYPE_STRING, strlen(fqdn), fqdn, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_ttl, RTS_TYPE_STRING, strlen(fqdn), fqdn, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

}

void
test_fsm_dpi_dns_process_attr_unlikely(void)
{
    int ret;

    ret = fsm_dpi_dns_process_attr(NULL, "RANDOM", RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    ret = fsm_dpi_dns_process_attr(NULL, begin, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, begin, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    ret = fsm_dpi_dns_process_attr(NULL, begin, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, end, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
}

void
test_fsm_dpi_dns_process_attr_simple(void)
{
    int ret;

    ret = fsm_dpi_dns_process_attr(NULL, begin, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_qname, RTS_TYPE_STRING, strlen(fqdn), fqdn, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_type, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_ttl, RTS_TYPE_NUMBER, sizeof(ttl), &ttl, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_a, RTS_TYPE_BINARY, strlen(ipv4), ipv4, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_a_offset, RTS_TYPE_NUMBER, sizeof(offset_v4), &offset_v4, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_type, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_ttl, RTS_TYPE_NUMBER, sizeof(ttl), &ttl, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_aaaa, RTS_TYPE_BINARY, strlen(ipv6), ipv6, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_aaaa_offset, RTS_TYPE_NUMBER, sizeof(offset_v6), &offset_v6, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, end, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
}

void
test_fsm_dpi_dns_process_attr_no_begin(void)
{
    int ret;

    ret = fsm_dpi_dns_process_attr(NULL, dns_qname, RTS_TYPE_STRING, strlen(fqdn), fqdn, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, end, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
}

void
test_fsm_dpi_dns_process_attr_no_response(void)
{
    int ret;

    ret = fsm_dpi_dns_process_attr(NULL, begin, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_qname, RTS_TYPE_STRING, strlen(fqdn), fqdn, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, end, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
}

void
test_fsm_dpi_dns_process_attr_no_qname(void)
{
    int ret;

    ret = fsm_dpi_dns_process_attr(NULL, begin, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_type, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_ttl, RTS_TYPE_NUMBER, sizeof(ttl), &ttl, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_a, RTS_TYPE_BINARY, strlen(ipv4), ipv4, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_aaaa, RTS_TYPE_BINARY, strlen(ipv6), ipv6, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, end, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
}

void
test_fsm_dpi_dns_process_attr_incomplete_response(void)
{
    int ret;

    ret = fsm_dpi_dns_process_attr(NULL, begin, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_qname, RTS_TYPE_STRING, strlen(fqdn), fqdn, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_type, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, end, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    ret = fsm_dpi_dns_process_attr(NULL, begin, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_qname, RTS_TYPE_STRING, strlen(fqdn), fqdn, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, dns_ttl, RTS_TYPE_NUMBER, sizeof(ttl), &ttl, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_dns_process_attr(NULL, end, RTS_TYPE_STRING, 3, "dns", NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
}

void
populate_dns_record(char *qname, size_t idx, uint8_t type, uint8_t ip_version,
                    uint16_t ttl, void *value, uint16_t length, uint16_t offset)
{
    struct dpi_dns_client *mgr;
    struct dns_record *rec;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized)
    {
        LOGD("%s: not initialized", __func__);
        return;
    }

    rec = &mgr->curr_rec_processed;
    rec->idx = idx;
    STRSCPY(rec->qname, qname);
    rec->resp[rec->idx].type = type;
    rec->resp[rec->idx].ttl = ttl;
    rec->resp[rec->idx].ip_v = ip_version;
    memcpy(rec->resp[rec->idx].address, value, length);
    rec->resp[rec->idx].offset = offset;
    rec->idx++;
}

void
test_fsm_dpi_dns_update_v4_tag_generation(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct schema_Openflow_Tag *regular_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s dns_response;
    char regular_tag_name[64];
    char local_tag_name[64];
    size_t max_capacity;
    bool rc;
    int i;

    regular_tag = CALLOC(1, sizeof(*regular_tag));
    local_tag = CALLOC(1, sizeof(*local_tag));

    memset(&policy_reply, 0, sizeof(policy_reply));

    /* dns record details */
    struct dpi_dns_client *mgr;
    char qname[] = "google.com";
    void *value = "d83ac2ae";
    uint8_t ip_version = 4;
    uint16_t length = 4;
    uint16_t offset = 40;
    uint16_t ttl = 109;
    uint8_t type = 1;
    size_t idx = 0;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized)
        mgr->initialized = true;

    for (i = 0; i < 8; i++)
    {
        uint32_t addr;
        addr = htonl(i);

        value = &addr;
        idx = i;

        populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);
    }

    policy_reply.action = FSM_UPDATE_TAG;
    MEMZERO(dns_response);
    fsm_dpi_dns_populate_response_ips(&dns_response);
    TEST_ASSERT_EQUAL_INT(8, dns_response.ipv4_cnt);

    max_capacity = 2;
    memset(regular_tag_name, 0, sizeof(regular_tag_name));
    snprintf(regular_tag_name, sizeof(regular_tag_name), "${@%s}", g_tags[0].name);
    policy_reply.updatev4_tag = regular_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     regular_tag->device_value,
                                     &regular_tag->device_value_len,
                                     max_capacity, 4);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL(max_capacity, regular_tag->device_value_len);

    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev4_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 4);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL(max_capacity, local_tag->values_len);

    fsm_dpi_dns_free_dns_response_ips(&dns_response);
    FREE(regular_tag);
    FREE(local_tag);
}

void
test_fsm_dpi_dns_update_v6_tag_generation(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct schema_Openflow_Tag *regular_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s dns_response;
    char regular_tag_name[64];
    char local_tag_name[64];
    size_t max_capacity;
    bool rc;
    int i;

    regular_tag = CALLOC(1, sizeof(*regular_tag));
    local_tag = CALLOC(1, sizeof(*local_tag));

    memset(&policy_reply, 0, sizeof(policy_reply));

    /* dns record details */
    struct dpi_dns_client *mgr;
    char qname[] = "google.com";
    void *value = "d83ac2ae";
    uint8_t ip_version = 6;
    uint16_t length = 16;
    uint16_t offset = 40;
    uint16_t ttl = 109;
    uint8_t type = 1;
    size_t idx = 0;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized)
        mgr->initialized = true;

    for (i = 0; i < 8; i++)
    {
        uint32_t addr[4];

        addr[3] = i;
        value = &addr;
        idx = i;

        populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);
    }

    policy_reply.action = FSM_UPDATE_TAG;
    MEMZERO(dns_response);
    fsm_dpi_dns_populate_response_ips(&dns_response);
    TEST_ASSERT_EQUAL_INT(8, dns_response.ipv6_cnt);

    max_capacity = 2;
    memset(regular_tag_name, 0, sizeof(regular_tag_name));
    snprintf(regular_tag_name, sizeof(regular_tag_name), "${@%s}", g_tags[0].name);
    policy_reply.updatev6_tag = regular_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     regular_tag->device_value,
                                     &regular_tag->device_value_len,
                                     max_capacity, 6);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL(max_capacity, regular_tag->device_value_len);

    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev6_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 6);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL(max_capacity, local_tag->values_len);

    fsm_dpi_dns_free_dns_response_ips(&dns_response);
    FREE(regular_tag);
    FREE(local_tag);
}

void
test_fsm_dpi_dns_update_v4_tag_duplicates(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s dns_response;
    char local_tag_name[64];
    size_t max_capacity;
    bool rc;
    int i;

    local_tag = CALLOC(1, sizeof(*local_tag));

    memset(&policy_reply, 0, sizeof(policy_reply));

    /* dns record details */
    struct dpi_dns_client *mgr;
    char qname[] = "google.com";
    void *value = "d83ac2ae";
    uint8_t ip_version = 4;
    uint16_t length = 4;
    uint16_t offset = 40;
    uint16_t ttl = 109;
    uint8_t type = 1;
    size_t idx = 0;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized)
        mgr->initialized = true;

    for (i = 0; i < 8; i++)
    {
        uint32_t addr;
        addr = htonl(0x12345);

        value = &addr;
        idx = i;

        populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);
    }

    policy_reply.action = FSM_UPDATE_TAG;
    MEMZERO(dns_response);
    fsm_dpi_dns_populate_response_ips(&dns_response);
    TEST_ASSERT_EQUAL_INT(8, dns_response.ipv4_cnt);

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev4_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 4);
    TEST_ASSERT_TRUE(rc);
    /* Only one address should be present in the generated tag */
    TEST_ASSERT_EQUAL(1, local_tag->values_len);

    fsm_dpi_dns_free_dns_response_ips(&dns_response);
    FREE(local_tag);
}

void
test_fsm_dpi_dns_update_v6_tag_duplicates(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s dns_response;
    char local_tag_name[64];
    size_t max_capacity;
    bool rc;
    int i;

    local_tag = CALLOC(1, sizeof(*local_tag));

    memset(&policy_reply, 0, sizeof(policy_reply));

    /* dns record details */
    struct dpi_dns_client *mgr;
    char qname[] = "google.com";
    void *value = "d83ac2ae";
    uint8_t ip_version = 6;
    uint16_t length = 16;
    uint16_t offset = 40;
    uint16_t ttl = 109;
    uint8_t type = 1;
    size_t idx = 0;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized)
        mgr->initialized = true;

    for (i = 0; i < 8; i++)
    {
        uint32_t addr[4];

        addr[3] = 0x5678;
        value = &addr;
        idx = i;

        populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);
    }

    policy_reply.action = FSM_UPDATE_TAG;
    MEMZERO(dns_response);
    fsm_dpi_dns_populate_response_ips(&dns_response);
    TEST_ASSERT_EQUAL_INT(8, dns_response.ipv6_cnt);

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev6_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 6);
    TEST_ASSERT_TRUE(rc);
    /* Only one address should be present in the generated tag */
    TEST_ASSERT_EQUAL(1, local_tag->values_len);

    fsm_dpi_dns_free_dns_response_ips(&dns_response);
    FREE(local_tag);
}

void
test_fsm_dpi_dns_update_v4_tag_ip_expiration(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s dns_response;
    struct dns_record  *rec;
    char local_tag_name[64];
    size_t max_capacity;
    bool rc;
    int i;

    /* First case where number of ip address are less than max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    memset(&policy_reply, 0, sizeof(policy_reply));

    /* dns record details */
    struct dpi_dns_client *mgr;
    char qname[] = "google.com";
    void *value = "d83ac2ae";
    uint8_t ip_version = 4;
    uint16_t length = 4;
    uint16_t offset = 40;
    uint16_t ttl = 109;
    uint8_t type = 1;
    size_t idx = 0;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized)
        mgr->initialized = true;

    rec = &mgr->curr_rec_processed;

    for (i = 0; i < 8; i++)
    {
        uint32_t addr;
        addr = htonl(i);

        value = &addr;
        idx = i;

        populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);
    }

    policy_reply.action = FSM_UPDATE_TAG;
    MEMZERO(dns_response);
    fsm_dpi_dns_populate_response_ips(&dns_response);
    TEST_ASSERT_EQUAL_INT(8, dns_response.ipv4_cnt);

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev4_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 4);
    TEST_ASSERT_TRUE(rc);
    /* Expected length index is 8 in the generated tag */
    TEST_ASSERT_EQUAL(8, local_tag->values_len);

    /* Free allocated resources */
    MEMZERO(rec);
    fsm_dpi_dns_free_dns_response_ips(&dns_response);
    FREE(local_tag);

    /* Second case where number of ip address are equal to max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    memset(&policy_reply, 0, sizeof(policy_reply));

    for (i = 0; i < 16; i++)
    {
        uint32_t addr;
        addr = htonl(i);

        value = &addr;
        idx = i;

        populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);
    }

    policy_reply.action = FSM_UPDATE_TAG;
    MEMZERO(dns_response);
    fsm_dpi_dns_populate_response_ips(&dns_response);

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev4_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 4);

    TEST_ASSERT_TRUE(rc);
    /* Expected length index is 16 in the generated tag */
    TEST_ASSERT_EQUAL(16, local_tag->values_len);

    /* Free allocated resources */
    MEMZERO(rec);
    fsm_dpi_dns_free_dns_response_ips(&dns_response);
    FREE(local_tag);

    /* Third case where number of ip address are greater than max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    memset(&policy_reply, 0, sizeof(policy_reply));

    for (i = 0; i < 20; i++)
    {
        uint32_t addr;
        addr = htonl(i);

        value = &addr;
        idx = i;

        populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);
    }

    policy_reply.action = FSM_UPDATE_TAG;
    MEMZERO(dns_response);
    fsm_dpi_dns_populate_response_ips(&dns_response);

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev4_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 4);

    TEST_ASSERT_TRUE(rc);
    /* Expected length index is 4 in the generated tag */
    TEST_ASSERT_EQUAL(4, local_tag->values_len);

    /* Free allocated resources */
    MEMZERO(rec);
    fsm_dpi_dns_free_dns_response_ips(&dns_response);
    FREE(local_tag);
}

void
test_fsm_dpi_dns_update_v6_tag_ip_expiration(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s dns_response;
    struct dns_record *rec;
    char local_tag_name[64];
    size_t max_capacity;
    bool rc;
    int i;

    /* First case where number of ip address are less than max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    memset(&policy_reply, 0, sizeof(policy_reply));

    /* dns record details */
    struct dpi_dns_client *mgr;
    char qname[] = "google.com";
    void *value = "d83ac2ae";
    uint8_t ip_version = 6;
    uint16_t length = 16;
    uint16_t offset = 40;
    uint16_t ttl = 109;
    uint8_t type = 1;
    size_t idx = 0;

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized)
        mgr->initialized = true;

    rec = &mgr->curr_rec_processed;

    for (i = 0; i < 8; i++)
    {
        uint32_t addr[4];

        addr[3] = i;
        value = &addr;
        idx = i;

        populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);
    }

    policy_reply.action = FSM_UPDATE_TAG;
    MEMZERO(dns_response);
    fsm_dpi_dns_populate_response_ips(&dns_response);
    TEST_ASSERT_EQUAL_INT(8, dns_response.ipv6_cnt);

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev6_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 6);
    TEST_ASSERT_TRUE(rc);
    /* Expected length index is 8 in the generated tag */
    TEST_ASSERT_EQUAL(8, local_tag->values_len);

    /* Free allocated resources */
    MEMZERO(rec);
    fsm_dpi_dns_free_dns_response_ips(&dns_response);
    FREE(local_tag);

    /* Second case where number of ip address are equal to max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    memset(&policy_reply, 0, sizeof(policy_reply));

    for (i = 0; i < 16; i++)
    {
        uint32_t addr[4];

        addr[3] = i;
        value = &addr;
        idx = i;

        populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);
    }

    policy_reply.action = FSM_UPDATE_TAG;
    MEMZERO(dns_response);
    fsm_dpi_dns_populate_response_ips(&dns_response);
    TEST_ASSERT_EQUAL_INT(16, dns_response.ipv6_cnt);

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev6_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 6);
    TEST_ASSERT_TRUE(rc);
    /* Expected length index is 16 in the generated tag */
    TEST_ASSERT_EQUAL(16, local_tag->values_len);

    /* Free allocated resources */
    MEMZERO(rec);
    fsm_dpi_dns_free_dns_response_ips(&dns_response);
    FREE(local_tag);

    /* Third case where number of ip address are greater than max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    memset(&policy_reply, 0, sizeof(policy_reply));

    for (i = 0; i < 20; i++)
    {
        uint32_t addr[4];

        addr[3] = i;
        value = &addr;
        idx = i;

        populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);
    }

    policy_reply.action = FSM_UPDATE_TAG;
    MEMZERO(dns_response);
    fsm_dpi_dns_populate_response_ips(&dns_response);
    TEST_ASSERT_EQUAL_INT(20, dns_response.ipv6_cnt);

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev6_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(&dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 6);
    TEST_ASSERT_TRUE(rc);
    /* Expected length index is 4 in the generated tag */
    TEST_ASSERT_EQUAL(4, local_tag->values_len);

    /* Free allocated resources */
    MEMZERO(rec);
    fsm_dpi_dns_free_dns_response_ips(&dns_response);
    FREE(local_tag);
}

void
test_fsm_dpi_dns_update_response_ips(void)
{
    struct net_header_parser *net_parser;
    struct net_md_stats_accumulator acc;
    struct fsm_policy_reply policy_reply;
    char ipv4_addr[INET_ADDRSTRLEN] = "";
    char replaced_ip[] = "1.2.3.4";
    char buf[] = "A-1.2.3.4";
    size_t parsed;
    uint8_t * packet;
    size_t len;
    bool rc;

    /* dns record details */
    struct dpi_dns_client *mgr;
    char qname[] = "google.com";
    void *value = "d83ac2ae";
    uint8_t ip_version = 4;
    uint16_t length = 4;
    uint16_t offset = 40;
    uint16_t ttl = 109;
    uint8_t type = 1;
    size_t idx = 0;

    ut_prepare_pcap(__func__);

    mgr = fsm_dpi_dns_get_mgr();
    if (!mgr->initialized)
        mgr->initialized = true;

    populate_dns_record(qname, idx, type, ip_version, ttl, value, length, offset);

    policy_reply.redirect = true;
    STRSCPY(policy_reply.redirects[0], buf);

    /* DNS response packet */
    acc.key = CALLOC(1, sizeof(struct net_md_flow_key));
    acc.key->ipprotocol = IPPROTO_UDP;

    policy_reply.redirect = true;
    STRSCPY(policy_reply.redirects[0], buf);

    net_parser = CALLOC(1, sizeof(*net_parser));
    UT_CREATE_PCAP_PAYLOAD(pkt12, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    packet = (uint8_t *)net_parser->start;
    parsed = net_parser->parsed;
    inet_ntop(AF_INET,
              packet + parsed + offset, ipv4_addr, sizeof(ipv4_addr));
    LOGI("%s: Before updating response ipaddr: %s", __func__, ipv4_addr);

    rc = fsm_dpi_dns_update_response_ips(net_parser, &acc, &policy_reply);
    TEST_ASSERT_TRUE(rc);

    inet_ntop(AF_INET,
              packet + parsed + offset, ipv4_addr, sizeof(ipv4_addr));
    LOGI("%s: After updating response ipaddr: %s", __func__, ipv4_addr);

    len = strcmp(ipv4_addr, replaced_ip);
    TEST_ASSERT_TRUE(len == 0);

    fsm_dpi_dns_reset_state();

    FREE(net_parser);
    FREE(acc.key);

    ut_cleanup_pcap();
}


void
run_test_dns(void)
{
    RUN_TEST(test_fsm_dpi_sni_init_exit);

    RUN_TEST(test_fsm_dpi_sni_process_attr_wrong_type);
    RUN_TEST(test_fsm_dpi_dns_process_attr_unlikely);
    RUN_TEST(test_fsm_dpi_dns_process_attr_simple);
    RUN_TEST(test_fsm_dpi_dns_process_attr_no_begin);
    RUN_TEST(test_fsm_dpi_dns_process_attr_no_response);
    RUN_TEST(test_fsm_dpi_dns_process_attr_no_qname);
    RUN_TEST(test_fsm_dpi_dns_process_attr_incomplete_response);
    RUN_TEST(test_fsm_dpi_dns_update_response_ips);
    RUN_TEST(test_fsm_dpi_dns_update_v4_tag_generation);
    RUN_TEST(test_fsm_dpi_dns_update_v6_tag_generation);
    RUN_TEST(test_fsm_dpi_dns_update_v4_tag_duplicates);
    RUN_TEST(test_fsm_dpi_dns_update_v6_tag_duplicates);
    RUN_TEST(test_fsm_dpi_dns_update_v4_tag_ip_expiration);
    RUN_TEST(test_fsm_dpi_dns_update_v6_tag_ip_expiration);
}
