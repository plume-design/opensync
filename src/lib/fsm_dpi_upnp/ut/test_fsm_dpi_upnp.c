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
#include <stdio.h>
#include <string.h>

#include "fsm.h"
#include "fsm_dpi_upnp.h"
#include "log.h"
#include "os.h"
#include "unity.h"
#include "util.h"

static union fsm_plugin_ops g_plugin_ops =
{
    .web_cat_ops =
    {
        .categories_check = NULL,
        .risk_level_check = NULL,
        .cat2str = NULL,
        .get_stats = NULL,
        .dns_response = NULL,
        .gatekeeper_req = NULL,
    },
};

static struct fsm_session g_session =
{
    .node_id = "NODE_ID",
    .location_id = "LOCATION_ID",
    .topic = "UPNP_AVS_TOPIC",
};

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

char *upnp_attr = "upnp";
char *begin = "begin";
char *end = "end";

char *upnp_action = "upnp.action";
char *upnp_protocol = "upnp.protocol";
char *upnp_ext_port = "upnp.ext_port";
char *upnp_int_port = "upnp.int_port";
char *upnp_int_client = "upnp.int_client";
char *upnp_duration = "upnp.duration";
char *upnp_description = "upnp.description";

char *wrong_value = "wrong_value";
int64_t wrong_type = 0;


static char *
mock_get_config(struct fsm_session *session, char *key)
{
    (void)session;
    LOGD("%s: looking for %s", __func__, key);

    return key;
}

void
test_fsm_dpi_upnp_init_exit(void)
{
    struct fsm_dpi_upnp_session *u_client;
    struct fsm_session sess;
    int rc;

    sess = g_session;
    sess.ops.get_config = mock_get_config;
    sess.p_ops = &g_plugin_ops;

    rc = fsm_dpi_upnp_init(NULL);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    fsm_dpi_upnp_exit(NULL);

    sess.name = "TESTING";
    rc = fsm_dpi_upnp_init(&sess);
    TEST_ASSERT_EQUAL_INT(0, rc);
    fsm_dpi_upnp_exit(&sess);
    u_client = fsm_dpi_upnp_get_session(&sess);
    TEST_ASSERT_FALSE(u_client->initialized);

    rc = dpi_upnp_plugin_init(&sess);
    TEST_ASSERT_EQUAL_INT(0, rc);

    u_client = fsm_dpi_upnp_get_session(&sess);
    TEST_ASSERT_TRUE(u_client->initialized);

    rc = dpi_upnp_plugin_init(&sess);
    TEST_ASSERT_EQUAL_INT(-1, rc);

    fsm_dpi_upnp_exit(&sess);
}

void
test_fsm_dpi_upnp_process_attr_wrong_type(void)
{
    int ret;

    ret = fsm_dpi_upnp_process_attr(NULL, begin, RTS_TYPE_NUMBER, sizeof(wrong_type), &wrong_type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_upnp_process_attr(NULL, begin, RTS_TYPE_STRING, strlen(wrong_value), wrong_value, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    ret = fsm_dpi_upnp_process_attr(NULL, end, RTS_TYPE_NUMBER, sizeof(wrong_type), &wrong_type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_upnp_process_attr(NULL, end, RTS_TYPE_STRING, strlen(wrong_value), wrong_value, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);

    ret = fsm_dpi_upnp_process_attr(NULL, upnp_action, RTS_TYPE_NUMBER, sizeof(wrong_type), &wrong_type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_upnp_process_attr(NULL, upnp_protocol, RTS_TYPE_NUMBER, sizeof(wrong_type), &wrong_type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_upnp_process_attr(NULL, upnp_ext_port, RTS_TYPE_NUMBER, sizeof(wrong_type), &wrong_type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_upnp_process_attr(NULL, upnp_int_port, RTS_TYPE_NUMBER, sizeof(wrong_type), &wrong_type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_upnp_process_attr(NULL, upnp_int_client, RTS_TYPE_NUMBER, sizeof(wrong_type), &wrong_type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_upnp_process_attr(NULL, upnp_duration, RTS_TYPE_NUMBER, sizeof(wrong_type), &wrong_type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_upnp_process_attr(NULL, upnp_description, RTS_TYPE_NUMBER, sizeof(wrong_type), &wrong_type, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
}

void
test_fsm_dpi_upnp_process_attr_simple(void)
{
    int ret;

    ret = fsm_dpi_upnp_process_attr(NULL, begin, RTS_TYPE_STRING, strlen(upnp_attr), upnp_attr, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    // ret = fsm_dpi_upnp_process_attr(NULL, dns_qname, RTS_TYPE_STRING, strlen(fqdn), fqdn, NULL);
    // TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    // ret = fsm_dpi_upnp_process_attr(NULL, dns_type, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    // TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    // ret = fsm_dpi_upnp_process_attr(NULL, dns_ttl, RTS_TYPE_NUMBER, sizeof(ttl), &ttl, NULL);
    // TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    // ret = fsm_dpi_upnp_process_attr(NULL, dns_a, RTS_TYPE_BINARY, strlen(ipv4), ipv4, NULL);
    // TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    // ret = fsm_dpi_upnp_process_attr(NULL, dns_a_offset, RTS_TYPE_NUMBER, sizeof(offset_v4), &offset_v4, NULL);
    // TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    // ret = fsm_dpi_upnp_process_attr(NULL, dns_type, RTS_TYPE_NUMBER, sizeof(type), &type, NULL);
    // TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    // ret = fsm_dpi_upnp_process_attr(NULL, dns_ttl, RTS_TYPE_NUMBER, sizeof(ttl), &ttl, NULL);
    // TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    // ret = fsm_dpi_upnp_process_attr(NULL, dns_aaaa, RTS_TYPE_BINARY, strlen(ipv6), ipv6, NULL);
    // TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    // ret = fsm_dpi_upnp_process_attr(NULL, dns_aaaa_offset, RTS_TYPE_NUMBER, sizeof(offset_v6), &offset_v6, NULL);
    // TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
    ret = fsm_dpi_upnp_process_attr(NULL, end, RTS_TYPE_STRING, strlen(upnp_attr), upnp_attr, NULL);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_IGNORED, ret);
}

void
test_check_upnp_record(void)
{
    struct fsm_upnp_record rec;
    bool ret;

    ret = fsm_dpi_upnp_check_record(NULL);
    TEST_ASSERT_FALSE(ret);

    MEMZERO(rec);

    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_FALSE(ret);

    STRSCPY(rec.action, "some action");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_FALSE(ret);

    /* Action is AddPortMapping */
    STRSCPY(rec.action, "AddPortMapping");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_FALSE(ret);

    STRSCPY(rec.protocol, "11111");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_FALSE(ret);

    STRSCPY(rec.ext_port, "11111");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_FALSE(ret);

    STRSCPY(rec.int_port, "22222");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_FALSE(ret);

    STRSCPY(rec.int_client, "192.168.1.2");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_FALSE(ret);

    STRSCPY(rec.duration, "5000");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_TRUE(ret);

    STRSCPY(rec.description, "Some description text");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_TRUE(ret);

    /* Action is DeletePortMapping */
    MEMZERO(rec);
    STRSCPY(rec.action, "DeletePortMapping");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_FALSE(ret);

    STRSCPY(rec.protocol, "11111");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_FALSE(ret);

    STRSCPY(rec.ext_port, "11111");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_TRUE(ret);

    STRSCPY(rec.int_port, "22222");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_TRUE(ret);

    STRSCPY(rec.int_client, "192.168.1.2");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_TRUE(ret);

    STRSCPY(rec.duration, "5000");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_TRUE(ret);

    STRSCPY(rec.description, "Some description text");
    ret = fsm_dpi_upnp_check_record(&rec);
    TEST_ASSERT_TRUE(ret);
}

void
run_test_upnp(void)
{
    // RUN_TEST(test_fsm_dpi_upnp_init_exit);    // NEED FIXING

    RUN_TEST(test_fsm_dpi_upnp_process_attr_wrong_type);
    RUN_TEST(test_fsm_dpi_upnp_process_attr_simple);
    RUN_TEST(test_check_upnp_record);
}
