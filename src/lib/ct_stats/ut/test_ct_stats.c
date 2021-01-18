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

#include <errno.h>
#include <ev.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libmnl/libmnl.h>

#include "ct_stats.h"
#include "os_types.h"
#include "target.h"
#include "unity.h"
#include "log.h"
#include "fcm_filter.h"
#include "fcm.h"
#include "neigh_table.h"

struct mnl_buf
{
    uint32_t portid;
    uint32_t seq;
	size_t len;
	uint8_t data[4096];
};

#include "mnl_dump_2.c"
#include "mnl_dump_2_zones.c"
#include "mnl_dump_10.c"
#include "mnl_dump_10_zones.c"

extern ds_tree_t flow_tracker_list;
void process_merged_multi_zonestats(ds_dlist_t *list, ds_tree_t *tree);
void flow_free_merged_multi_zonestats(ds_tree_t *tree);
const char *test_name = "fcm_ct_stats_tests";


char *g_node_id = "4C718002B3";
char *g_loc_id = "59efd33d2c93832025330a3e";
char *g_mqtt_topic = "dev-test/ct_stats/4C718002B3/59efd33d2c93832025330a3e";

/* Gathered at sample colletion. See mnl_cb_run() implementation for details */
int g_portid = 18404;
int g_seq = 1581959512;


char *
test_get_other_config_0(fcm_collect_plugin_t *plugin, char *key)
{
    if (!strcmp(key, "name")) return "ct_stats_0";

    return NULL;
}


char *
test_get_other_config_1(fcm_collect_plugin_t *plugin, char *key)
{
    if (!strcmp(key, "name")) return "ct_stats_1";
    if (!strcmp(key, "active")) return "true";

    return NULL;
}

char *
test_get_other_config_2(fcm_collect_plugin_t *plugin, char *key)
{
    if (!strcmp(key, "name")) return "ct_stats_2";

    return NULL;
}

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

bool
test_send_report(struct net_md_aggregator *aggr, char *mqtt_topic)
{
#ifndef ARCH_X86
    bool ret;

    /* Send the report */
    ret = net_md_send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);
    return ret;
#else
    struct packed_buffer *pb;
    pb = serialize_flow_report(aggr->report);

    /* Free the serialized container */
    free_packed_buffer(pb);
    net_md_reset_aggregator(aggr);

    return true;
#endif
}

fcm_collect_plugin_t g_collector_tbl[3] =
{
    {
        .get_other_config = test_get_other_config_0,
    },
    {
        .get_other_config = test_get_other_config_1,
    },
    {
        .get_other_config = test_get_other_config_2,
    },
};

void
setUp(void)
{
    struct net_md_aggregator *aggr;
    flow_stats_t *ct_stats;
    flow_stats_mgr_t *mgr;
    size_t num_c;
    size_t i;
    int rc;

    mgr = ct_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    neigh_table_init();

    num_c = sizeof(g_collector_tbl) / sizeof(g_collector_tbl[0]);
    for (i = 0; i < num_c; i++)
    {
        g_collector_tbl[i].mqtt_topic = g_mqtt_topic;
        g_collector_tbl[i].loop = EV_DEFAULT;
        g_collector_tbl[i].get_mqtt_hdr_node_id = test_get_mqtt_hdr_node_id;
        g_collector_tbl[i].get_mqtt_hdr_loc_id = test_get_mqtt_hdr_loc_id;

        rc = ct_stats_plugin_init(&g_collector_tbl[i]);
        if ((int)i < mgr->max_sessions) TEST_ASSERT_EQUAL_INT(0, rc);
        else TEST_ASSERT_EQUAL_INT(-1, rc);
    }

    ct_stats = ct_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(ct_stats);

    aggr = ct_stats->aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    aggr->send_report = test_send_report;
}


void
tearDown(void)
{
    size_t num_c;
    size_t i;

    num_c = sizeof(g_collector_tbl) / sizeof(g_collector_tbl[0]);
    for (i = 0; i < num_c; i++)
    {
        ct_stats_plugin_exit(&g_collector_tbl[i]);
    }
    neigh_table_cleanup();
}

void
test_collect(void)
{
    flow_stats_t *ct_stats;

    ct_stats = ct_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(ct_stats);
    ct_stats_collect_cb(ct_stats->collector);
}


void
test_process_v4(void)
{
    fcm_collect_plugin_t *collector;
    ctflow_info_t *flow_info;
    flow_stats_t *ct_stats;
    struct mnl_buf *p_mnl;
    flow_stats_mgr_t *mgr;
    uint32_t portid;
    uint32_t seq;
    bool loop;
    int idx;
    int ret;

    mgr = ct_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    ct_stats = ct_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(ct_stats);

    collector = ct_stats->collector;
    TEST_ASSERT_NOT_NULL(collector);

    loop = true;
    idx = 0;
    while (loop)
    {
        p_mnl = &g_mnl_buf_ipv4[idx];
        portid = g_portid;
        if (p_mnl->portid != 0) portid = p_mnl->portid;
        seq = g_seq;
        if (p_mnl->seq != 0) seq = p_mnl->seq;
        ret = mnl_cb_run(p_mnl->data, p_mnl->len, seq, portid,
                         data_cb, ct_stats);
        if (ret == -1)
        {
            ret = errno;
            LOGE("%s: mnl_cb_run failed: %s", __func__, strerror(ret));
            loop = false;
        }
        else if (ret <= MNL_CB_STOP) loop = false;
        idx++;
    }

    ds_dlist_foreach(&ct_stats->ctflow_list, flow_info)
    {
        ct_stats_print_contrack(&flow_info->flow);
    }

    ct_flow_add_sample(ct_stats);
    collector->send_report(collector);
}


void
test_process_v6(void)
{
    fcm_collect_plugin_t *collector;
    ctflow_info_t *flow_info;
    flow_stats_t *ct_stats;
    flow_stats_mgr_t *mgr;
    struct mnl_buf *p_mnl;
    bool loop;
    int idx;
    int ret;

    mgr = ct_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    ct_stats = ct_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(ct_stats);

    collector = ct_stats->collector;
    TEST_ASSERT_NOT_NULL(collector);

    loop = true;
    idx = 0;
    while (loop)
    {
        p_mnl = &g_mnl_buf_ipv6[idx];
        ret = mnl_cb_run(p_mnl->data, p_mnl->len, g_seq, g_portid,
                         data_cb, ct_stats);
        if (ret == -1)
        {
            ret = errno;
            LOGE("%s: mnl_cb_run failed: %s", __func__, strerror(ret));
            loop = false;
        }
        else if (ret <= MNL_CB_STOP) loop = false;
        idx++;
    }

    ds_dlist_foreach(&ct_stats->ctflow_list, flow_info)
    {
        ct_stats_print_contrack(&flow_info->flow);
    }

    ct_flow_add_sample(ct_stats);
    collector->send_report(collector);
}

void
test_process_v4_zones(void)
{
    fcm_collect_plugin_t *collector;
    ctflow_info_t *flow_info;
    flow_stats_t *ct_stats;
    struct mnl_buf *p_mnl;
    flow_stats_mgr_t *mgr;
    uint32_t portid;
    uint32_t seq;
    bool loop;
    int idx;
    int ret;

    mgr = ct_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    ct_stats = ct_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(ct_stats);

    collector = ct_stats->collector;
    TEST_ASSERT_NOT_NULL(collector);
    ct_stats->ct_zone = USHRT_MAX;

    loop = true;
    idx = 0;
    while (loop)
    {
        p_mnl = &g_mnl_buf_zones_ipv4[idx];
        portid = g_portid;
        if (p_mnl->portid != 0) portid = p_mnl->portid;
        seq = g_seq;
        if (p_mnl->seq != 0) seq = p_mnl->seq;
        ret = mnl_cb_run(p_mnl->data, p_mnl->len, seq, portid,
                         data_cb, ct_stats);
        if (ret == -1)
        {
            ret = errno;
            LOGE("%s: mnl_cb_run failed: %s", __func__, strerror(ret));
            loop = false;
        }
        else if (ret <= MNL_CB_STOP) loop = false;
        idx++;
    }

    ds_dlist_foreach(&ct_stats->ctflow_list, flow_info)
    {
        ct_stats_print_contrack(&flow_info->flow);
    }

    if (ct_stats->ct_zone == USHRT_MAX)
        process_merged_multi_zonestats(&ct_stats->ctflow_list,
                                       &flow_tracker_list);

    flow_free_merged_multi_zonestats(&flow_tracker_list);

    ct_flow_add_sample(ct_stats);
    collector->send_report(collector);
}


void
test_process_v6_zones(void)
{
    fcm_collect_plugin_t *collector;
    ctflow_info_t *flow_info;
    flow_stats_t *ct_stats;
    flow_stats_mgr_t *mgr;
    struct mnl_buf *p_mnl;
    uint32_t portid;
    uint32_t seq;
    bool loop;
    int idx;
    int ret;

    mgr = ct_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    ct_stats = ct_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(ct_stats);

    collector = ct_stats->collector;
    TEST_ASSERT_NOT_NULL(collector);
    ct_stats->ct_zone = USHRT_MAX;

    loop = true;
    idx = 0;
    while (loop)
    {
        p_mnl = &g_mnl_buf_zones_ipv6[idx];
        portid = g_portid;
        if (p_mnl->portid != 0) portid = p_mnl->portid;
        seq = g_seq;
        if (p_mnl->seq != 0) seq = p_mnl->seq;

        ret = mnl_cb_run(p_mnl->data, p_mnl->len, seq, portid,
                         data_cb, ct_stats);
        if (ret == -1)
        {
            ret = errno;
            LOGE("%s: mnl_cb_run failed: %s", __func__, strerror(ret));
            loop = false;
        }
        else if (ret <= MNL_CB_STOP) loop = false;
        idx++;
    }

    ds_dlist_foreach(&ct_stats->ctflow_list, flow_info)
    {
        ct_stats_print_contrack(&flow_info->flow);
    }

    if (ct_stats->ct_zone == USHRT_MAX)
        process_merged_multi_zonestats(&ct_stats->ctflow_list,
                                       &flow_tracker_list);

    flow_free_merged_multi_zonestats(&flow_tracker_list);

    ct_flow_add_sample(ct_stats);
    collector->send_report(collector);
}



void
test_ct_stat_v4(void)
{
    fcm_collect_plugin_t *collector;
    ctflow_info_t *flow_info;
    flow_stats_t *ct_stats;
    flow_stats_mgr_t *mgr;
    int ret;

    mgr = ct_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    ct_stats = ct_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(ct_stats);

    collector = ct_stats->collector;
    TEST_ASSERT_NOT_NULL(collector);

    ret = ct_stats_get_ct_flow(AF_INET);
    TEST_ASSERT_EQUAL_INT(ret, 0);

    ds_dlist_foreach(&ct_stats->ctflow_list, flow_info)
    {
        ct_stats_print_contrack(&flow_info->flow);
    }

    ct_flow_add_sample(ct_stats);
    collector->send_report(collector);
}


void
test_ct_stat_v6(void)
{
    fcm_collect_plugin_t *collector;
    ctflow_info_t *flow_info;
    flow_stats_t *ct_stats;
    flow_stats_mgr_t *mgr;
    int ret;

    mgr = ct_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    ct_stats = ct_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(ct_stats);

    collector = ct_stats->collector;
    TEST_ASSERT_NOT_NULL(collector);

    ret = ct_stats_get_ct_flow(AF_INET6);
    TEST_ASSERT_EQUAL_INT(ret, 0);

    ds_dlist_foreach(&ct_stats->ctflow_list, flow_info)
    {
        ct_stats_print_contrack(&flow_info->flow);
    }

    ct_flow_add_sample(ct_stats);
    collector->send_report(collector);
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_INFO);

    UnityBegin(test_name);

    RUN_TEST(test_process_v4);
    RUN_TEST(test_process_v6);
    RUN_TEST(test_process_v4_zones);
    RUN_TEST(test_process_v6_zones);
#if !defined(__x86_64__)
    RUN_TEST(test_ct_stat_v4);
    RUN_TEST(test_ct_stat_v6);
#endif

    ct_stats_exit_mgr();

    return UNITY_END();
}
