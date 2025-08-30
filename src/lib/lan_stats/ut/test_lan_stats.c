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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>
#include <libmnl/libmnl.h>
#include <ev.h>

#include "ovsdb_utils.h"
#include "network_metadata_report.h"
#include "network_metadata.h"
#include "lan_stats.h"
#include "os_types.h"
#include "target.h"
#include "unity.h"
#include "log.h"
#include "fcm.h"
#include "fcm_filter.h"
#include "memutil.h"
#include "kconfig.h"
#include "fcm_priv.h"
#include "fcm_mgr.h"
#include "policy_tags.h"
#include "unit_test_utils.h"
#include "nf_utils.h"

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


const char *test_name = "fcm_lan_stats_tests";

char *g_node_id = "ES83F0006A";
char *g_loc_id = "5f93600e05bf767503dbbfe1";
char *g_mqtt_topic = "lan/dog1/5f93600e05bf767503dbbfe1/07";
char *g_default_dpctl_f[] = { "./data/stats.txt",
                              "./data/stats_2.txt",
                              "./data/stats_3.txt",
                              "./data/stats_4.txt",
                              "./data/stats_5.txt",
                              "./data/stats_6.txt"};

struct fcm_session *session = NULL;
struct fcm_filter_client *c_client = NULL;
struct fcm_filter_client *r_client = NULL;


/* Gathered at sample collection. See mnl_cb_run() implementation for details */
int g_portid = 18404;
int g_seq = 1581959512;

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

struct schema_Openflow_Tag g_tags[] =
{
    {
        .name_exists = true,
        .name = "eth_clients",
        .device_value_len = 3,
        .device_value =
        {
            "00:25:90:87:17:5c",
            "00:25:90:87:17:5b",
            "44:32:c8:80:00:7c",
        },
        .cloud_value_len = 3,
        .cloud_value =
        {
            "13:13:13:13:13:13",
            "14:14:14:14:14:14",
            "15:15:15:15:15:15",
        },
    },
    {
        .name_exists = true,
        .name = "clients",
        .device_value_len = 2,
        .device_value =
        {
            "21:21:21:21:21:21",
            "22:22:22:22:22:22",
        },
        .cloud_value_len = 3,
        .cloud_value =
        {
            "23:23:23:23:23:23",
            "24:24:24:24:24:24",
            "25:25:25:25:25:25",
        },
    },
};


struct schema_Openflow_Tag_Group g_tag_group =
{
    .name_exists = true,
    .name = "group_tag",
    .tags_len = 2,
    .tags =
    {
        "eth_clients",
        "clients",
    }
};

void ut_ovsdb_init(void) {}
void ut_ovsdb_exit(void) {}

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
    g_test_mgr.dpctl_file = g_default_dpctl_f[0];
}


char *
test_get_other_config(fcm_collect_plugin_t *plugin, char *key)
{
    fcm_collector_t *collector = NULL;
    struct str_pair *pair;

    collector = plugin->fcm;
    if ((collector == NULL) || (key == NULL)) return NULL;
    if (collector->collect_conf.other_config == NULL) return NULL;

    pair = ds_tree_find(collector->collect_conf.other_config, key);

    if (pair == NULL) return NULL;

    return pair->value;
}

#define OTHER_CONFIG_NELEM_SIZE 128
char g_other_configs[][2][4][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "parent_tag",
        },
        {
            "$[group_tag]",
        }
    },
    {
        {
            "parent_tag",
            "active"
        },
        {
            "${eth_clients}",
            "true"
        },
    },
    {
        {
            "parent_tag",
        },
        {
            "${@eth_clients}",
        }
    },
    {
        {
            "parent_tag",
        },
        {
            "${#eth_clients}",
        },
    }
};

fcm_collect_plugin_t g_collector_tbl[4] =
{
    {
        .name = "lan_stats_0",
        .get_other_config = test_get_other_config,
        .sample_interval = 1,
        .report_interval = 5,
    },
    {
        .name = "lan_stats_1",
        .get_other_config = test_get_other_config,
        .sample_interval = 1,
        .report_interval = 5,
    },
    {
        .name = "lan_stats_2",
        .get_other_config = test_get_other_config,
        .sample_interval = 1,
        .report_interval = 5,
    },
    {
        .name = "lan_stats_3",
        .get_other_config = test_get_other_config,
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

void
test_nf_ct_get_flow(struct net_md_aggregator *aggr)
{

    struct mnl_buf *p_mnl;
    uint32_t portid;
    uint32_t seq;
    bool loop;
    int idx;
    int ret;

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
                         nf_process_ct_cb, aggr);
        if (ret == -1)
        {
            ret = errno;
            LOGE("%s: mnl_cb_run failed: %s", __func__, strerror(ret));
            loop = false;
        }
        else if (ret <= MNL_CB_STOP) loop = false;
        idx++;
    }
}

void
test_lan_stats_collect_flows(struct net_md_aggregator *aggr)
{
    test_nf_ct_get_flow(aggr);

    return;
}


/**
 * @brief emits a protobuf report
 *
 * Assumes the presence of QM to send the report on non native platforms,
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
lan_stats_setUp(void)
{
    ds_tree_t *other_config = NULL;
    struct ev_loop *loop = EV_DEFAULT;
    fcm_collector_t *collector;
    lan_stats_mgr_t *mgr;

    size_t num_c;
    size_t i;
    bool ret;

    mgr = lan_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);


    nf_ct_init(loop, NULL);
    lan_stats_init_mgr(EV_DEFAULT);
    mgr->ovsdb_init = ut_ovsdb_init;
    mgr->ovsdb_exit = ut_ovsdb_exit;
    num_c = sizeof(g_collector_tbl) / sizeof(g_collector_tbl[0]);
    for (i = 0; i < num_c; i++)
    {
        collector = calloc(1, sizeof(*collector));
        g_collector_tbl[i].mqtt_topic = g_mqtt_topic;
        g_collector_tbl[i].loop = EV_DEFAULT;
        g_collector_tbl[i].get_mqtt_hdr_node_id = test_get_mqtt_hdr_node_id;
        g_collector_tbl[i].get_mqtt_hdr_loc_id = test_get_mqtt_hdr_loc_id;
        other_config = schema2tree(OTHER_CONFIG_NELEM_SIZE,
                                   OTHER_CONFIG_NELEM_SIZE,
                                   4,
                                   g_other_configs[i][0],
                                   g_other_configs[i][1]);
        collector->collect_conf.other_config = other_config;
        g_collector_tbl[i].fcm = collector;
    }

    num_c = sizeof(g_tags) / sizeof(*g_tags);
    for (i = 0; i < num_c; i++)
    {
        ret = om_tag_add_from_schema(&g_tags[i]);
        TEST_ASSERT_TRUE(ret);
    }

    ret = om_tag_group_add_from_schema(&g_tag_group);
    TEST_ASSERT_TRUE(ret);


}

void
lan_stats_tearDown(void)
{
    fcm_collector_t *collector = NULL;
    ds_tree_t *other_config;
    size_t num_c;
    size_t i;
    bool ret;

    num_c = sizeof(g_collector_tbl) / sizeof(g_collector_tbl[0]);
    for (i = 0; i < num_c; i++)
    {
        lan_stats_plugin_exit(&g_collector_tbl[i]);
        collector = g_collector_tbl[i].fcm;
        other_config = collector->collect_conf.other_config;
        free_str_tree(other_config);
        free(collector);
    }

    num_c = sizeof(g_tags) / sizeof(*g_tags);
    for (i = 0; i < num_c; i++)
    {
        ret = om_tag_remove_from_schema(&g_tags[i]);
        TEST_ASSERT_TRUE(ret);
    }
    ret = om_tag_group_remove_from_schema(&g_tag_group);
    TEST_ASSERT_TRUE(ret);

    nf_ct_exit();
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

/**
 * @brief verifies link status are same in stats->uplink_if_type
 * and if_name
 */
void
validate_uplink(lan_stats_instance_t *lan_stats_instance, uplink_iface_type if_name)
{
    lan_stats_mgr_t *mgr;

    mgr = lan_stats_get_mgr();

    TEST_ASSERT_NOT_NULL(lan_stats_instance);

    TEST_ASSERT_EQUAL_INT(mgr->uplink_if_type, if_name);
}



/**
 * @brief verifies link status as "eth"
 */
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

    session = CALLOC(1, sizeof(*session));
    TEST_ASSERT_NOT_NULL(session);

    c_client = CALLOC(1, sizeof(*c_client));
    TEST_ASSERT_NOT_NULL(c_client);
    session->handler_ctxt = c_client;

    r_client = CALLOC(1, sizeof(*r_client));
    TEST_ASSERT_NOT_NULL(r_client);
    session->handler_ctxt = r_client;

    lan_stats_instance->session = session;
    lan_stats_instance->r_client =  r_client;
    lan_stats_instance->c_client = c_client;

    /* Update the flow collector routine */
    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);

    test_lan_stats_collect_flows(aggr);
    lan_stats_collect_cb(collector);

    /* Update the reporting routine */
    aggr->send_report = test_emit_report;

    /* collect LAN stats and report it */
    link_stats_collect_cb(IFTYPE_ETH);
    collector->collect_periodic(collector);
    collector->send_report(collector);
    validate_uplink(lan_stats_instance, IFTYPE_ETH);

    FREE(session);
    FREE(c_client);
    FREE(r_client);
}

/**
 * @brief verifies link status as "lte"
 */
void
test_data_collection_v1(void)
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

    session = CALLOC(1, sizeof(*session));
    TEST_ASSERT_NOT_NULL(session);

    c_client = CALLOC(1, sizeof(*c_client));
    TEST_ASSERT_NOT_NULL(c_client);
    session->handler_ctxt = c_client;

    r_client = CALLOC(1, sizeof(*r_client));
    TEST_ASSERT_NOT_NULL(r_client);
    session->handler_ctxt = r_client;

    lan_stats_instance->session = session;
    lan_stats_instance->r_client =  r_client;
    lan_stats_instance->c_client = c_client;

    /* Update the flow collector routine */
    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    test_lan_stats_collect_flows(aggr);

    lan_stats_collect_cb(collector);

    /* Update the reporting routine */
    aggr->send_report = test_emit_report;

    /* collect LAN stats and report it */
    link_stats_collect_cb(IFTYPE_LTE);
    collector->collect_periodic(collector);
    collector->send_report(collector);
    validate_uplink(lan_stats_instance, IFTYPE_LTE);

    FREE(session);
    FREE(c_client);
    FREE(r_client);
}


/**
 * @brief verifies link status by altering from
 * eth -> lte
 */
void
test_data_collection_v2(void)
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

    session = CALLOC(1, sizeof(*session));
    TEST_ASSERT_NOT_NULL(session);

    c_client = CALLOC(1, sizeof(*c_client));
    TEST_ASSERT_NOT_NULL(c_client);
    session->handler_ctxt = c_client;

    r_client = CALLOC(1, sizeof(*r_client));
    TEST_ASSERT_NOT_NULL(r_client);
    session->handler_ctxt = r_client;

    lan_stats_instance->session = session;
    lan_stats_instance->r_client = r_client;
    lan_stats_instance->c_client = c_client;

    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);

    test_lan_stats_collect_flows(aggr);
    lan_stats_collect_cb(collector);

    aggr->send_report = test_emit_report;

    /* collect LAN stats and report it */
    link_stats_collect_cb(IFTYPE_ETH);
    collector->collect_periodic(collector);
    collector->send_report(collector);
    validate_uplink(lan_stats_instance, IFTYPE_ETH);

    /*continue on eth line*/
    collector->collect_periodic(collector);
    collector->send_report(collector);
    validate_uplink(lan_stats_instance, IFTYPE_ETH);

    /*shift to lte*/
    link_stats_collect_cb(IFTYPE_LTE);
    collector->collect_periodic(collector);
    collector->send_report(collector);
    validate_uplink(lan_stats_instance, IFTYPE_LTE);

    /*continue on lte line*/
    collector->collect_periodic(collector);
    collector->send_report(collector);
    validate_uplink(lan_stats_instance, IFTYPE_LTE);

    FREE(session);
    FREE(c_client);
    FREE(r_client);
}


void
add_flow_stats_cb(EV_P_ ev_timer *w, int revents)
{
    lan_stats_instance_t *lan_stats_instance;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    lan_stats_mgr_t *mgr;

    collector = w->data;

    mgr = lan_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    lan_stats_instance = lan_stats_get_active_instance();
    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);

    c_client = calloc(1, sizeof(*c_client));
    TEST_ASSERT_NOT_NULL(c_client);

    lan_stats_instance->c_client = c_client;

    /* collect LAN stats */
    collector->collect_periodic(collector);
    LOGI("%s: total flows: %zu, flows to report: %zu", __func__,
         aggr->total_flows, aggr->total_report_flows);

    net_md_log_aggr(aggr);


    FREE(c_client);
}


void
report_flow_stats_cb(EV_P_ ev_timer *w, int revents)
{
    lan_stats_instance_t *lan_stats_instance;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;

    collector = w->data;

    lan_stats_instance = lan_stats_get_active_instance();
    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);

    collector->send_report(collector);

    net_md_log_aggr(aggr);

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

    g_test_mgr.dpctl_file = g_default_dpctl_f[1];

    /* add 1st instance */
    rc = lan_stats_plugin_init(collector);
    TEST_ASSERT_EQUAL_INT(0, rc);

    lan_stats_instance = lan_stats_get_active_instance();

    session = calloc(1, sizeof(*session));
    TEST_ASSERT_NOT_NULL(session);

    c_client = calloc(1, sizeof(*c_client));
    TEST_ASSERT_NOT_NULL(c_client);
    session->handler_ctxt = c_client;

    r_client = calloc(1, sizeof(*r_client));
    TEST_ASSERT_NOT_NULL(r_client);
    session->handler_ctxt = r_client;

    lan_stats_instance->session = session;
    lan_stats_instance->r_client =  r_client;
    lan_stats_instance->c_client = c_client;

    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    test_lan_stats_collect_flows(aggr);
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

    FREE(session);
    FREE(c_client);
    FREE(r_client);
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

void
test_parent_group_tag(void)
{
    lan_stats_instance_t *lan_stats_instance;
    struct schema_Openflow_Tag *ovsdb_tag;
    fcm_collect_plugin_t *collector;
    lan_stats_mgr_t *mgr;
    char *value;
    int rc;

    collector = &g_collector_tbl[0];

    mgr = lan_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    /* add instance */
    rc = lan_stats_plugin_init(collector);
    TEST_ASSERT_EQUAL_INT(0, rc);

    lan_stats_instance = lan_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(lan_stats_instance);

    TEST_ASSERT_EQUAL_STRING(g_other_configs[0][1], lan_stats_instance->parent_tag);
    TEST_ASSERT_EQUAL_STRING(g_collector_tbl[0].name, lan_stats_instance->name);

    ovsdb_tag = &g_tags[1];

    value = ovsdb_tag->device_value[1];

    rc = om_tag_in(value, lan_stats_instance->parent_tag);
    TEST_ASSERT_TRUE(rc);

    value = ovsdb_tag->cloud_value[1];
    rc = om_tag_in(value, lan_stats_instance->parent_tag);
    TEST_ASSERT_TRUE(rc);
}

void
test_parent_tag(void)
{
    lan_stats_instance_t *lan_stats_instance;
    struct schema_Openflow_Tag *ovsdb_tag;
    fcm_collect_plugin_t *collector;
    lan_stats_mgr_t *mgr;
    char *value;
    int rc;

    collector = &g_collector_tbl[1];

    mgr = lan_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    /* add instance */
    rc = lan_stats_plugin_init(collector);
    TEST_ASSERT_EQUAL_INT(0, rc);

    lan_stats_instance = lan_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(lan_stats_instance);

    TEST_ASSERT_EQUAL_STRING(g_other_configs[1][1], lan_stats_instance->parent_tag);
    TEST_ASSERT_EQUAL_STRING(g_collector_tbl[1].name, lan_stats_instance->name);

    ovsdb_tag = &g_tags[0];

    value = ovsdb_tag->device_value[1];

    rc = om_tag_in(value, lan_stats_instance->parent_tag);
    TEST_ASSERT_TRUE(rc);

    value = ovsdb_tag->cloud_value[1];
    rc = om_tag_in(value, lan_stats_instance->parent_tag);
    TEST_ASSERT_TRUE(rc);
}

void
test_parent_device_tag(void)
{
    lan_stats_instance_t *lan_stats_instance;
    struct schema_Openflow_Tag *ovsdb_tag;
    fcm_collect_plugin_t *collector;
    lan_stats_mgr_t *mgr;
    char *value;
    int rc;

    collector = &g_collector_tbl[2];

    mgr = lan_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    /* add instance */
    rc = lan_stats_plugin_init(collector);
    TEST_ASSERT_EQUAL_INT(0, rc);

    lan_stats_instance = lan_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(lan_stats_instance);

    TEST_ASSERT_EQUAL_STRING(g_other_configs[2][1], lan_stats_instance->parent_tag);
    TEST_ASSERT_EQUAL_STRING(g_collector_tbl[2].name, lan_stats_instance->name);

    ovsdb_tag = &g_tags[0];

    value = ovsdb_tag->device_value[1];

    rc = om_tag_in(value, lan_stats_instance->parent_tag);
    TEST_ASSERT_TRUE(rc);

    value = ovsdb_tag->cloud_value[1];
    rc = om_tag_in(value, lan_stats_instance->parent_tag);
    TEST_ASSERT_FALSE(rc);
}

void
test_parent_cloud_tag(void)
{
    lan_stats_instance_t *lan_stats_instance;
    struct schema_Openflow_Tag *ovsdb_tag;
    fcm_collect_plugin_t *collector;
    lan_stats_mgr_t *mgr;
    char *value;
    int rc;

    collector = &g_collector_tbl[3];

    mgr = lan_stats_get_mgr();
    TEST_ASSERT_NOT_NULL(mgr);

    /* add instance */
    rc = lan_stats_plugin_init(collector);
    TEST_ASSERT_EQUAL_INT(0, rc);

    lan_stats_instance = lan_stats_get_active_instance();
    TEST_ASSERT_NOT_NULL(lan_stats_instance);

    TEST_ASSERT_EQUAL_STRING(g_other_configs[3][1], lan_stats_instance->parent_tag);
    TEST_ASSERT_EQUAL_STRING(g_collector_tbl[3].name, lan_stats_instance->name);

    ovsdb_tag = &g_tags[0];

    value = ovsdb_tag->device_value[1];

    rc = om_tag_in(value, lan_stats_instance->parent_tag);
    TEST_ASSERT_FALSE(rc);

    value = ovsdb_tag->cloud_value[1];
    rc = om_tag_in(value, lan_stats_instance->parent_tag);
    TEST_ASSERT_TRUE(rc);
}

/**
 * @brief validates packets & bytes
 *
 * Validates packets and bytes in aggr with actual bytes & packets flow.
 */
void
validate_flow_packets_bytes(void)
{
    lan_stats_instance_t *lan_stats_instance;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    int rc;

    collector = &g_collector_tbl[0];
    g_test_mgr.dpctl_file = g_default_dpctl_f[2];

    /* add instance */
    rc = lan_stats_plugin_init(collector);
    TEST_ASSERT_EQUAL_INT(0, rc);

    lan_stats_instance = lan_stats_get_active_instance();

    session = calloc(1, sizeof(*session));
    TEST_ASSERT_NOT_NULL(session);

    c_client = calloc(1, sizeof(*c_client));
    TEST_ASSERT_NOT_NULL(c_client);
    session->handler_ctxt = c_client;

    r_client = calloc(1, sizeof(*r_client));
    TEST_ASSERT_NOT_NULL(r_client);
    session->handler_ctxt = r_client;

    lan_stats_instance->session = session;
    lan_stats_instance->r_client =  r_client;
    lan_stats_instance->c_client = c_client;

    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    test_lan_stats_collect_flows(aggr);
    aggr->send_report = test_emit_report;

    /* collect LAN stats and report it */
    collector->collect_periodic(collector);
    collector->send_report(collector);

    FREE(session);
    FREE(c_client);
    FREE(r_client);
}


/**
 * @brief validates packets & bytes
 *
 * Validates packets and bytes in aggr with actual bytes & packets flow.
 */
void
parse_flow_packets_bytes(char *file)
{
    lan_stats_instance_t *lan_stats_instance;
    fcm_collect_plugin_t *collector;
    struct net_md_aggregator *aggr;
    int rc;

    collector = &g_collector_tbl[0];
    g_test_mgr.dpctl_file = file;

    /* add instance */
    rc = lan_stats_plugin_init(collector);
    TEST_ASSERT_EQUAL_INT(0, rc);

    lan_stats_instance = lan_stats_get_active_instance();

    session = calloc(1, sizeof(*session));
    TEST_ASSERT_NOT_NULL(session);

    c_client = calloc(1, sizeof(*c_client));
    TEST_ASSERT_NOT_NULL(c_client);
    session->handler_ctxt = c_client;

    r_client = calloc(1, sizeof(*r_client));
    TEST_ASSERT_NOT_NULL(r_client);
    session->handler_ctxt = r_client;

    lan_stats_instance->session = session;
    lan_stats_instance->r_client =  r_client;
    lan_stats_instance->c_client = c_client;

    aggr = lan_stats_instance->aggr;
    TEST_ASSERT_NOT_NULL(aggr);
    test_lan_stats_collect_flows(aggr);
    aggr->send_report = test_emit_report;

    /* collect LAN stats and report it */
    collector->collect_periodic(collector);
    collector->send_report(collector);

    FREE(session);
    FREE(c_client);
    FREE(r_client);
}

void test_flow_packets_bytes()
{
    parse_flow_packets_bytes(g_default_dpctl_f[3]);
    parse_flow_packets_bytes(g_default_dpctl_f[4]);
    parse_flow_packets_bytes(g_default_dpctl_f[5]);
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, test_lan_stats_global_setup, NULL);

    ut_setUp_tearDown(test_name, lan_stats_setUp, lan_stats_tearDown);

    size_t i;
    int ret;
    /*
     * This is a requirement: Do NOT proceed if the file is missing.
     * File presence will not be tested any further.
     */
    if (chdir(dirname(argv[0])) != 0)
    {
        LOGW("chdir(\"%s\") failed", argv[0]);
        return ut_fini();
    }

    for (i = 0; i < 6; i++)
    {
        ret = access(g_default_dpctl_f[i], F_OK);
        if (ret != 0)
        {
            LOGI("%s file is missing", g_default_dpctl_f[i]);
            return ut_fini();
        }
    }

    RUN_TEST(test_active_session);
    RUN_TEST(test_max_session);
    RUN_TEST(test_data_collection);
    RUN_TEST(test_data_collection_v1);
    RUN_TEST(test_data_collection_v2);
    RUN_TEST(test_events);
    RUN_TEST(test_parent_group_tag);
    RUN_TEST(test_parent_tag);
    RUN_TEST(test_parent_device_tag);
    RUN_TEST(test_parent_cloud_tag);
    RUN_TEST(validate_flow_packets_bytes);
    RUN_TEST(test_flow_packets_bytes);
    lan_stats_exit_mgr();

    return ut_fini();
}
