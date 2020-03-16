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

#include "log.h"
#include "os.h"
#include "os_types.h"
#include "om.h"
#include "target.h"
#include "unity.h"
#include "schema.h"


const char *test_name = "om_tests";
static struct tag_mgr tag_mgr;

void setUp(void)
{
    // pass
}

void tearDown(void)
{
    // pass
}


static int
get_range_rules_len(ds_list_t *range_rules)
{
    int                 counter = 0;
    struct om_rule_node *data;

    ds_list_foreach(range_rules, data) {
        counter++;
    }

    return counter;
}

static void
print_rules_test(ds_list_t *range_rules)
{
    struct om_rule_node *data;

    ds_list_foreach(range_rules, data) {
        LOGD("Rule: %s", data->rule.rule);
    }

    return;
}

static bool
pattern_is_in_rules(ds_list_t *list, char *rule)
{
    struct om_rule_node *data;
    ds_list_iter_t      iter;

    for (data = ds_list_ifirst(&iter, list); data != NULL; data = ds_list_inext(&iter))
    {
        if ( strstr(data->rule.rule, rule) != NULL) {
            return true;
        } 
    }

    return false;
}

static void
test_linked_list_operations(void)
{
    bool ret;
    ds_list_t *range_rules = om_range_get_range_rules();

    struct schema_Openflow_Config conf = {
            .table = 0,
            .bridge = "br-home",
            .priority = 100,
            .action = "normal",
            .token = "12345",
            .rule = "tcp"
    };
    
    struct schema_Openflow_Config second = {
            .table = 0,
            .bridge = "br-home",
            .priority = 100,
            .action = "normal",
            .token = "45678",
            .rule = "udp"
    };

    ret = om_range_add_range_rule(&conf);
    TEST_ASSERT_TRUE(ret);

    ret = om_range_add_range_rule(&second);
    TEST_ASSERT_TRUE(ret);

    int count = get_range_rules_len(range_rules);
    TEST_ASSERT_EQUAL_INT(2, count);

    ret = om_range_clear_range_rules();
    TEST_ASSERT_TRUE(ret);
}

static void
test_generate_port_range_rules(void)
{
    bool        ret, exists;
    int         count;
    ds_list_t   *range_rules = om_range_get_range_rules();

    struct schema_Openflow_Config conf = {
            .table = 0,
            .bridge = "br-home",
            .priority = 100,
            .action = "normal",
            .token = "12345",
            .rule = "tcp,tp_src=$<1-3>,tp_dst=$<1-2>"
    };

    ret   = om_range_generate_range_rules(&conf);
    count = get_range_rules_len(range_rules);

    LOGD("Count:%d, Generated rules: ", count);
    print_rules_test(range_rules);

    ds_list_t *list = om_range_get_range_rules();
    exists          = pattern_is_in_rules(list, "tcp,tp_src=1");
    TEST_ASSERT_TRUE(exists);

    ret = om_range_clear_range_rules();
    TEST_ASSERT_TRUE(ret);

    TEST_ASSERT_EQUAL_INT(6, count);
}

static void
test_generate_ipv4_range_rules(void)
{
    bool        ret, exists;
    int         count;
    ds_list_t   *range_rules = om_range_get_range_rules();

    struct schema_Openflow_Config conf = {
            .table = 0,
            .bridge = "br-home",
            .priority = 100,
            .action = "normal",
            .token = "12345",
            .rule = "ip,nw_src=$<192.168.1.1-192.168.1.5>,nw_dst=192.168.1.68"
    };

    ret   = om_range_generate_range_rules(&conf);
    count = get_range_rules_len(range_rules);

    LOGD("Count=%d, Generated rules: ", count);
    print_rules_test(range_rules);

    ds_list_t *list = om_range_get_range_rules();
    exists          = false;
    exists          = pattern_is_in_rules(list, "nw_src=192.168.1.1");
    TEST_ASSERT_TRUE(exists);
     
    exists          = pattern_is_in_rules(list, "nw_src=192.168.1.3");
    TEST_ASSERT_TRUE(exists);

    exists          = pattern_is_in_rules(list, "nw_src=192.168.1.5");
    TEST_ASSERT_TRUE(exists);

    exists          = !pattern_is_in_rules(list, "nw_src=192.168.1.6");
    TEST_ASSERT_TRUE(exists);

    ret = om_range_clear_range_rules();
    TEST_ASSERT_TRUE(ret);

    TEST_ASSERT_EQUAL_INT(5, count);
}

static void
test_generate_ipv6_range_rules(void)
{
    bool ret, exists;
    int  count;
    ds_list_t *range_rules = om_range_get_range_rules();

    struct schema_Openflow_Config conf = {
            .table = 0,
            .bridge = "br-home",
            .priority = 100,
            .action = "normal",
            .token = "12345",
            .rule = "dl_type=0x86DD,"
                     "ipv6_src=$<2a03:6300:1:103:219:5bff:fe31:13e1"
                     "-2a03:6300:1:103:219:5bff:fe31:13f4>"
    };

    ret   = om_range_generate_range_rules(&conf);
    count = get_range_rules_len(range_rules);

    LOGD("Count:%d, Generated rules: ", count);
    print_rules_test(range_rules);

    ds_list_t *list = om_range_get_range_rules();
    exists          = false;
    exists          = pattern_is_in_rules(list, "ipv6_src=2a03:6300:1:103:219:5bff:fe31:13e1");
    TEST_ASSERT_TRUE(exists);
     
    exists          = pattern_is_in_rules(list, "ipv6_src=2a03:6300:1:103:219:5bff:fe31:13e3");
    TEST_ASSERT_TRUE(exists);

    exists          = pattern_is_in_rules(list, "ipv6_src=2a03:6300:1:103:219:5bff:fe31:13f4");
    TEST_ASSERT_TRUE(exists);


    exists          = !pattern_is_in_rules(list, "ipv6_src=2a03:6300:1:103:219:5bff:fe31:13f5");
    TEST_ASSERT_TRUE(exists);

    ret = om_range_clear_range_rules();
    TEST_ASSERT_TRUE(ret);

    TEST_ASSERT_EQUAL_INT(20, count);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    memset(&tag_mgr, 0, sizeof(tag_mgr));
    tag_mgr.service_tag_update = om_template_tag_update;
    om_tag_init(&tag_mgr);
    
    UnityBegin(test_name);

    RUN_TEST(test_linked_list_operations);
    RUN_TEST(test_generate_port_range_rules);
    RUN_TEST(test_generate_ipv4_range_rules);
    RUN_TEST(test_generate_ipv6_range_rules);

    return UNITY_END();
}
