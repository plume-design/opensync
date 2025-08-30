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

#include <ev.h>
#include "log.h"
#include "schema.h"
#include "log.h"
#include "ovsdb_update.h"
#include "os_types.h"
#include "target.h"
#include "unity.h"
#include "ds_dlist.h"
#include "ds_tree.h"
#include "fcm_priv.h"
#include "fcm_mgr.h"
#include "fcm_filter.h"
#include "unit_test_utils.h"

#define FCM_MAX_MEM   CONFIG_FCM_MEM_MAX * 1024

extern fcm_collect_plugin_t *test_plugin;
static const char *test_name = "fcm_mgr_ut";

struct test_mgr
{
    bool has_ovsdb;
} g_test_mgr;

static struct schema_FCM_Collector_Config test_collect[] =
{
    {
        .name = "test_FCM_collector",
        .interval_present = true,
        .interval = 0,
        .report_name_present = true,
        .report_name = "test_report",
        .other_config_present = true,
        .other_config_keys[0] = "dso_path",
        .other_config[0] = "testplugin",
        .other_config_keys[1] = "dso_init",
        .other_config[1] = "test_plugin_init",
        .other_config_keys[2] = "dso",
        .other_config[2] = "test_FCM_collector.so",
        .other_config_len = 3,
    },
    {
        .name = "test_FCM_collector_1",
        .interval_present = true,
        .interval = 0,
        .report_name_present = true,
        .report_name = "test_report",
        .other_config_present = true,
        .other_config_keys[0] = "dso_path",
        .other_config[0] =  "testplugin",
        .other_config_keys[1] = "dso_init",
        .other_config[1] = "test_plugin_init",
        .other_config_keys[2] = "dso",
        .other_config[2] = "test_FCM_collector_1.so",
        .other_config_len = 3,
        .filter_name_present = true,
        .filter_name = "test_collect_filter",
    },
    {
        .name = "test_FCM_collector",
        .interval_present = true,
        .interval = 0,
        .report_name_present = true,
        .report_name = "test_report_1",
        .other_config_present = true,
        .other_config_keys[0] = "dso_path",
        .other_config[0] = "testplugin",
        .other_config_keys[1] = "dso_init",
        .other_config[1] = "test_plugin_init",
        .other_config_keys[2] = "dso",
        .other_config[2] = "test_FCM_collector.so",
        .other_config_len = 3,
    }
};

static struct schema_Node_Config test_nodecfg[] =
{
    {
        .persist = true,
        .module  = "fcm",
        .key     = "max_mem_percent",
        .value   = "20",
    },
    {
        .persist = true,
        .module  = "fcm",
        .key     = "max_mem_percent",
        .value   = "40",
    }
};

static struct schema_FCM_Report_Config test_report[] =
{
    {
        .name = "test_report",
        .interval_present = true,
        .interval = 30,
        .format_present = true,
        .format = "delta",
        .mqtt_topic_present = true,
        .mqtt_topic = "test_fcm_mqtt_topic",
        .other_config_present = true,
        .other_config_keys[0] = "dummy_key",
        .other_config[0] = "dummy_value",
        .other_config_len = 1,
    },
    {
        .name = "test_report_1",
        .interval_present = true,
        .interval = 30,
        .format_present = true,
        .format = "delta",
        .mqtt_topic_present = true,
        .mqtt_topic = "test_fcm_mqtt_topic",
        .report_filter_present = true,
        .report_filter = "test_report_filter_1",
    },
    {
        .name = "test_report",
        .interval_present = true,
        .interval = 30,
        .format_present = true,
        .format = "delta",
        .mqtt_topic_present = true,
        .mqtt_topic = "test_fcm_mqtt_topic",
        .report_filter_present = true,
        .report_filter = "test_report_filter",
    }
};

/*
 *Dummy Upsert function - perform decref on 'where'
 *@param : incref on 'where' is in fcm_set_node_state.
 */
bool native_ovsdb_table_upsert_where(ovsdb_table_t *table, json_t *where, void *record, bool update_uuid)
{
    LOGD("%s: Upsert operation for native platform", __func__);
    json_decref(where);
    return true;
}

int fcm_ovsdb_test_setup(void)
{
#if !defined(__x86_64__)
    g_test_mgr.has_ovsdb = true;
#else
    g_test_mgr.has_ovsdb = false;
#endif
    return 0;
}

void fcm_test_init(void)
{
    struct ev_loop *loop = EV_DEFAULT;
    fcm_mgr_t *mgr;

    mgr = fcm_get_mgr();
    fcm_init_mgr(loop);
    fcm_event_init();
    fcm_ovsdb_test_setup();

    if (g_test_mgr.has_ovsdb)
    {
        mgr->cb_ovsdb_table_upsert_where = ovsdb_table_upsert_where;
    }
    else
    {
        mgr->cb_ovsdb_table_upsert_where = native_ovsdb_table_upsert_where;
    }

    fcm_ovsdb_init();
    fcm_filter_init();
}

void test_add_collect_config(void)
{
    bool ret;
    ret = init_collect_config(&test_collect[0]);
    TEST_ASSERT_EQUAL_INT(1, ret);
    LOGD ("Plugin not initialized as report_config not available");
    TEST_ASSERT_NULL(test_plugin);
}

void test_add_report_config(void)
{
    init_report_config(&test_report[0]);
    TEST_ASSERT_NOT_NULL(test_plugin);
    TEST_ASSERT_NULL(test_plugin->plugin_ctx);
    TEST_ASSERT_NULL(test_plugin->plugin_fcm_ctx);
    TEST_ASSERT_NULL(test_plugin->fcm_plugin_ctx);
    TEST_ASSERT_EQUAL_INT(FCM_RPT_FMT_DELTA, test_plugin->fmt);
    TEST_ASSERT_EQUAL_STRING(
            test_report[0].mqtt_topic,test_plugin->mqtt_topic);
    TEST_ASSERT_NOT_NULL(test_plugin->fcm);
    TEST_ASSERT_EQUAL_INT(
            test_collect[0].interval, test_plugin->sample_interval);
    TEST_ASSERT_EQUAL_INT(
            test_report[0].interval, test_plugin->report_interval);
}

void test_del_report_config(void)
{
    delete_report_config(&test_report[0]);
}

void test_del_collect_config(void)
{
    delete_collect_config(&test_collect[0]);
}

void test_get_default_mem(void)
{
   fcm_mgr_t *mgr;

   mgr = fcm_get_mgr();
   TEST_ASSERT_EQUAL_INT(FCM_MAX_MEM, mgr->max_mem);
}

void test_add_node_cfg(void)
{
   fcm_mgr_t *mgr;
   mgr = fcm_get_mgr();
   fcm_get_node_config(&test_nodecfg[0]);
   TEST_ASSERT_GREATER_THAN_UINT(100000, mgr->max_mem);
}

void test_del_node_cfg(void)
{
   fcm_mgr_t *mgr;
   mgr = fcm_get_mgr();
   fcm_rm_node_config(&test_nodecfg[0]);
   TEST_ASSERT_EQUAL_INT(FCM_MAX_MEM, mgr->max_mem);
}

void test_update_node_cfg(void)
{
   fcm_mgr_t *mgr;
   mgr = fcm_get_mgr();
   fcm_get_node_config(&test_nodecfg[0]);
   TEST_ASSERT_GREATER_THAN_UINT(100000, mgr->max_mem);
   fcm_update_node_config(&test_nodecfg[1]);
   TEST_ASSERT_GREATER_THAN_UINT(200000, mgr->max_mem);
}

void test_null_collect_report_filter_client(void)
{
    bool ret;
    init_report_config(&test_report[0]);
    ret = init_collect_config(&test_collect[0]);
    TEST_ASSERT_EQUAL_INT(1, ret);
    delete_report_config(&test_report[0]);
    delete_collect_config(&test_collect[0]);
}

void test_null_collect_filter_client(void)
{
    bool ret;
    init_report_config(&test_report[1]);
    ret = init_collect_config(&test_collect[2]);
    TEST_ASSERT_EQUAL_INT(1, ret);
    delete_report_config(&test_report[1]);
    delete_collect_config(&test_collect[2]);
}

void test_null_report_filter_client(void)
{
    bool ret;
    init_report_config(&test_report[0]);
    ret = init_collect_config(&test_collect[1]);
    TEST_ASSERT_EQUAL_INT(1, ret);
    delete_report_config(&test_report[0]);
    delete_collect_config(&test_collect[1]);
}

void test_collect_report_filter_client(void)
{
    bool ret;
    init_report_config(&test_report[2]);
    ret = init_collect_config(&test_collect[1]);
    TEST_ASSERT_EQUAL_INT(1, ret);
    delete_report_config(&test_report[2]);
    delete_collect_config(&test_collect[1]);
}

void test_fcm_set_gk_url_null_gk_url(void)
{
    struct schema_Flow_Service_Manager_Config conf = {.handler = "gatekeeper"};
    fcm_mgr_t *mgr;

    mgr = fcm_get_mgr();
    fcm_set_gk_url(&conf);
    TEST_ASSERT_EQUAL_STRING("", mgr->gk_conf.gk_url);
}

void test_fcm_set_gk_url_non_gatekeeper_handler(void)
{
    struct schema_Flow_Service_Manager_Config conf = {.handler = "not_gatekeeper"};
    fcm_mgr_t *mgr;

    mgr = fcm_get_mgr();
    fcm_set_gk_url(&conf);
    TEST_ASSERT_EQUAL_STRING("", mgr->gk_conf.gk_url);
}

void test_fcm_set_gk_url_success(void)
{
    struct schema_Flow_Service_Manager_Config conf = {
        .handler = "gatekeeper",
        .other_config_keys = {"mqtt_v", "gk_url", "policy_table"},
        .other_config = {"dev-test/IP/Flows/ut/0/0", "http://plume.com/gatekeeper", "test_policy"},
        .other_config_len = 3,
    };
    fcm_mgr_t *mgr;

    const char *expected_url = "http://plume.com/gatekeeper";
    mgr = fcm_get_mgr();

    fcm_set_gk_url(&conf);
    TEST_ASSERT_EQUAL_STRING(expected_url, mgr->gk_conf.gk_url);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, NULL, NULL);

    ut_setUp_tearDown(test_name, NULL, NULL);

#ifndef CONFIG_FCM_NO_DSO
    fcm_test_init();
    RUN_TEST(test_add_collect_config);
    RUN_TEST(test_add_report_config);
    RUN_TEST(test_del_report_config);
    RUN_TEST(test_del_collect_config);
    RUN_TEST(test_get_default_mem);
    RUN_TEST(test_add_node_cfg);
    RUN_TEST(test_del_node_cfg);
    RUN_TEST(test_update_node_cfg);
    RUN_TEST(test_null_collect_report_filter_client);
    RUN_TEST(test_null_collect_filter_client);
    RUN_TEST(test_null_report_filter_client);
    RUN_TEST(test_collect_report_filter_client);
    RUN_TEST(test_fcm_set_gk_url_non_gatekeeper_handler);
    RUN_TEST(test_fcm_set_gk_url_null_gk_url);
    RUN_TEST(test_fcm_set_gk_url_success);
#endif

    return ut_fini();
}
