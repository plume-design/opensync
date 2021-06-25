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

#include <libmnl/libmnl.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ev.h>

#include "network_metadata_report.h"
#include "network_metadata.h"
#include "lan_stats.h"
#include "os_types.h"
#include "target.h"
#include "unity.h"
#include "log.h"
#include "fcm.h"
#include "memutil.h"

const char *test_name = "fcm_lan_stats_tests";

char *g_node_id = "4C7770146A";
char *g_loc_id = "5f93600e05bf767503dbbfe1";
char *g_mqtt_topic = "lan/dog1/5f93600e05bf767503dbbfe1/07";
char *g_default_dpctl_f = "/tmp/stats.txt";

struct test_timers
{
    ev_timer timeout_flow_parse;        /* Parse flows */
    ev_timer timeout_flow_parse_again;  /* Parse flows again */
    ev_timer timeout_flow_report;       /* Report flows */
    ev_timer timeout_flow_report_again; /* Report flows again */
};

struct test_mgr
{
    struct ev_loop *loop;
    ev_timer timeout_watcher;
    double g_timeout;
    struct test_timers flow_timers;
    char *dpctl_file;
} g_test_mgr;


/**
 * @brief breaks the ev loop to terminate a test
 */
static void
timeout_cb(EV_P_ ev_timer *w, int revents)
{
    ev_break(EV_A_ EVBREAK_ONE);
}

int
lan_stats_ev_test_setup(double timeout)
{
    ev_timer *p_timeout_watcher;

    /* Set up the timer killing the ev loop, indicating the end of the test */
    p_timeout_watcher = &g_test_mgr.timeout_watcher;

    ev_timer_init(p_timeout_watcher, timeout_cb, timeout, 0.);
    ev_timer_start(g_test_mgr.loop, p_timeout_watcher);

    return 0;
}

void
test_lan_stats_global_setup(void)
{
    g_test_mgr.loop = EV_DEFAULT;
    g_test_mgr.g_timeout = 1.0;
    g_test_mgr.dpctl_file = g_default_dpctl_f;
}


char *
test_get_other_config_0(fcm_collect_plugin_t *plugin, char *key)
{
    return NULL;
}

char *
test_get_other_config_1(fcm_collect_plugin_t *plugin, char *key)
{
    if (!strcmp(key, "active")) return "true";

    return NULL;
}

char *
test_get_other_config_2(fcm_collect_plugin_t *plugin, char *key)
{
    return NULL;
}

fcm_collect_plugin_t g_collector_tbl[3] =
{
    {
        .name = "lan_stats_0",
        .get_other_config = test_get_other_config_0,
        .sample_interval = 1,
        .report_interval = 5,
    },
    {
        .name = "lan_stats_1",
        .get_other_config = test_get_other_config_1,
        .sample_interval = 1,
        .report_interval = 5,
    },
    {
        .name = "lan_stats_2",
        .get_other_config = test_get_other_config_2,
        .sample_interval = 1,
        .report_interval = 5,
    },
};

char *
test_get_mqtt_hdr_node_id(void)
{
    return g_node_id;
}

char *
test_get_mqtt_hdr_loc_id(void)
{
    return g_loc_id;
}


static void
test_lan_stats_collect_flows(lan_stats_instance_t *lan_stats_instance)
{
    FILE *fp = NULL;
    char line_buf[LINE_BUFF_LEN] = {0,};
    char *file_path;

    file_path = g_test_mgr.dpctl_file;
    TEST_ASSERT_NOT_NULL(file_path);

    fp = fopen(file_path, "r");
    if (fp == NULL) return;

    while (fgets(line_buf, LINE_BUFF_LEN, fp) != NULL)
    {
        LOGD("ovs-dpctl dump line %s", line_buf);
        lan_stats_parse_flows(lan_stats_instance, line_buf);
        lan_stats_flows_filter(lan_stats_instance);
        memset(line_buf, 0, sizeof(line_buf));
    }

    fclose(fp);
}


/**
 * @brief emits a protobuf report
 *
 * Assumes the presenece of QM to send the report on non native platforms,
 * simply resets the aggregator content for native target.
 * @param aggr the aggregator
 */
static bool
test_emit_report(struct net_md_aggregator *aggr, char *topic)
{
#ifndef ARCH_X86
    bool ret;

    /* Send the report */
    ret = net_md_send_report(aggr, topic);
    TEST_ASSERT_TRUE(ret);
#else
    struct packed_buffer *pb;

    pb = serialize_flow_report(aggr->report);

    /* Free the serialized container */
    free_packed_buffer(pb);
    FREE(pb);
    net_md_reset_aggregator(aggr);
#endif

    return true;
}


void
setUp(void)
{
    lan_stats_mgr_t *mgr;
    size_t num_c;
    size_t i;

    mgr = lan_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    num_c = sizeof(g_collector_tbl) / sizeof(g_collector_tbl[0]);
    for (i = 0; i < num_c; i++)
    {
        g_collector_tbl[i].mqtt_topic = g_mqtt_topic;
        g_collector_tbl[i].loop = EV_DEFAULT;
        g_collector_tbl[i].get_mqtt_hdr_node_id = test_get_mqtt_hdr_node_id;
        g_collector_tbl[i].get_mqtt_hdr_loc_id = test_get_mqtt_hdr_loc_id;
    }
}

void
tearDown(void)
{
    size_t num_c;
    size_t i;

    num_c = sizeof(g_collector_tbl) / sizeof(g_collector_tbl[0]);
    for (i = 0; i < num_c; i++)
    {
        lan_stats_plugin_exit(&g_collector_tbl[i]);
    }
}

void
test_active_session(void)
{
    lan_stats_instance_t *lan_stats_instance;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    lan_stats_mgr_t *mgr;
    int rc;

    collector = &g_collector_tbl[0];

    mgr = lan_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    /* add 1st instance */
    rc = lan_stats_plugin_init(collector);
    TEST_ASSERT_EQUAL_INT(0, rc);

    lan_stats_instance = lan_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(lan_stats_instance);

    /* Update the flow collector routine */
    lan_stats_instance->collect_flows = test_lan_stats_collect_flows;

    /* Update the reporting routine */
    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    aggr->send_report = test_emit_report;

    /* active flag is not set in the config, but since there
     * is only 1 instance, it will be the active instance */
    TEST_ASSERT_EQUAL_STRING(g_collector_tbl[0].name, lan_stats_instance->name);

    /* add the second instance */
    rc = lan_stats_plugin_init(&g_collector_tbl[1]);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* since "active" config is set, the new config will now
     * be the active instance.
     */
    lan_stats_instance = lan_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(lan_stats_instance);
    TEST_ASSERT_EQUAL_STRING(g_collector_tbl[1].name, lan_stats_instance->name);

}

void
test_max_session(void)
{
    lan_stats_mgr_t *mgr;
    int rc;

    mgr = lan_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    /* add 1st instance */
    rc = lan_stats_plugin_init(&g_collector_tbl[0]);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(1, mgr->num_sessions);

    /* add the second instance */
    rc = lan_stats_plugin_init(&g_collector_tbl[1]);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(2, mgr->num_sessions);

    /* maximum 2 instances are allowed, trying to add another one
     * should fail */
    rc = lan_stats_plugin_init(&g_collector_tbl[2]);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_EQUAL_INT(2, mgr->num_sessions);
}

void
test_data_collection(void)
{
    lan_stats_instance_t *lan_stats_instance;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    int rc;

    collector = &g_collector_tbl[0];

    /* add 1st instance */
    rc = lan_stats_plugin_init(collector);
    TEST_ASSERT_EQUAL_INT(0, rc);

    lan_stats_instance = lan_stats_get_active_instance();

    /* Update the flow collector routine */
    lan_stats_instance->collect_flows = test_lan_stats_collect_flows;

    /* Update the reporting routine */
    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    aggr->send_report = test_emit_report;

    /* collect LAN stats and report it */
    collector->collect_periodic(collector);
    collector->send_report(collector);
}


void
add_flow_stats_cb(EV_P_ ev_timer *w, int revents)
{
    lan_stats_instance_t *lan_stats_instance;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    lan_stats_mgr_t *mgr;

    LOGI("****** %s: entering\n", __func__);
    collector = w->data;

    mgr = lan_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    lan_stats_instance = lan_stats_get_active_instance();
    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);

    /* collect LAN stats */
    collector->collect_periodic(collector);
    LOGI("%s: total flows: %zu, flows to report: %zu", __func__,
         aggr->total_flows, aggr->total_report_flows);

    net_md_log_aggr(aggr);

    LOGI("****** %s: exiting\n", __func__);
}


void
report_flow_stats_cb(EV_P_ ev_timer *w, int revents)
{
    lan_stats_instance_t *lan_stats_instance;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;

    LOGI("****** %s: entering\n", __func__);

    collector = w->data;

    lan_stats_instance = lan_stats_get_active_instance();
    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);

    collector->send_report(collector);

    TEST_ASSERT_EQUAL_INT(1, aggr->total_eth_pairs);

    LOGD("%s: *************************** final printing *************", __func__);
    net_md_log_aggr(aggr);

    LOGI("****** %s: exiting\n", __func__);
}


void
setup_add_and_let_age_flows(void)
{
    lan_stats_instance_t *lan_stats_instance;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    struct test_timers *t;
    struct ev_loop *loop;
    int rc;

    collector = &g_collector_tbl[0];

    g_test_mgr.dpctl_file = "/tmp/stats_2.txt";

    /* add 1st instance */
    rc = lan_stats_plugin_init(collector);
    TEST_ASSERT_EQUAL_INT(0, rc);

    lan_stats_instance = lan_stats_get_active_instance();

    /* Update the flow collector routine */
    lan_stats_instance->collect_flows = test_lan_stats_collect_flows;

    /* Update the reporting routine */
    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    aggr->send_report = test_emit_report;

    /* Prepare the testing sequence */
    t = &g_test_mgr.flow_timers;
    loop = g_test_mgr.loop;

    /* Set first collection */
    g_test_mgr.g_timeout += collector->sample_interval;
    ev_timer_init(&t->timeout_flow_parse, add_flow_stats_cb,
                  g_test_mgr.g_timeout, 0);
    t->timeout_flow_parse.data = collector;

    /* Set second collection */
    g_test_mgr.g_timeout += collector->sample_interval;
    ev_timer_init(&t->timeout_flow_parse_again, add_flow_stats_cb,
                  g_test_mgr.g_timeout, 0);
    t->timeout_flow_parse_again.data = collector;

    /* Set reporting */
    g_test_mgr.g_timeout += collector->report_interval;
    ev_timer_init(&t->timeout_flow_report, report_flow_stats_cb,
                  g_test_mgr.g_timeout, 0);
    t->timeout_flow_report.data = collector;

    /* Set second reporting */
    g_test_mgr.g_timeout += collector->report_interval;
    ev_timer_init(&t->timeout_flow_report_again, report_flow_stats_cb,
                  g_test_mgr.g_timeout, 0);
    t->timeout_flow_report_again.data = collector;

    ev_timer_start(loop, &t->timeout_flow_parse);
    ev_timer_start(loop, &t->timeout_flow_parse_again);
    ev_timer_start(loop, &t->timeout_flow_report);
    ev_timer_start(loop, &t->timeout_flow_report_again);
}


void
test_events(void)
{
    setup_add_and_let_age_flows();

    /* Set overall test duration */
    lan_stats_ev_test_setup(++g_test_mgr.g_timeout);

    /* Start the main loop */
    ev_run(g_test_mgr.loop, 0);
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    test_lan_stats_global_setup();

    RUN_TEST(test_active_session);
    RUN_TEST(test_max_session);
    RUN_TEST(test_data_collection);
    RUN_TEST(test_events);

    lan_stats_exit_mgr();

    return UNITY_END();
}
