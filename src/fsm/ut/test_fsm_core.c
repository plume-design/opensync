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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>

#include "fsm.h"
#include "fsm_internal.h"
#include "log.h"
#include "memutil.h"
#include "network_metadata_report.h"
#include "policy_tags.h"
#include "target.h"
#include "unity.h"
#include "unit_test_utils.h"
#include "fsm_fn_trace.h"
#include "kconfig.h"
#include "nfe.h"

#include "pcap.c"

const char *ut_name = "fsm_core_tests";
extern int nfe_conntrack_icmp_timeout;
extern int nfe_conntrack_udp_timeout;
extern int nfe_conntrack_tcp_midflow;
extern int nfe_conntrack_tcp_timeout_est;
extern int nfe_conntrack_ether_timeout;
struct fsm_mgr *g_mgr;
uint32_t g_dispatch_tap_type;
bool g_identical_plugin_enabled = false;
bool g_identical_plugin_enabled1 = false;

struct schema_Openflow_Tag g_tags[] =
{
    {
        .name_exists = true,
        .name = "tag_1",
        .device_value_len = 6,
        .device_value =
        {
            "00:50:b6:0d:b4:fa",
            "00:25:90:87:17:5c",
            "00:25:90:87:17:5b",
            "44:32:c8:80:00:7c",
            "50:6a:03:ba:67:fb",
            "da:89:0c:05:39:50",
        },
        .cloud_value_len = 4,
        .cloud_value =
        {
            "13:13:13:13:13:13",
            "14:14:14:14:14:14",
            "15:15:15:15:15:15",
            "00:e1:03:00:16:80",
        },
    },
    {
        .name_exists = true,
        .name = "tag_2",
        .device_value_len = 4,
        .device_value =
        {
            "21:21:21:21:21:21",
            "22:22:22:22:22:22",
            "a0:ce:c8:d6:66:7f",
            "e6:26:86:f1:0d:cf",
        },
        .cloud_value_len = 5,
        .cloud_value =
        {
            "23:23:23:23:23:23",
            "24:24:24:24:24:24",
            "25:25:25:25:25:25",
            "00:e1:03:00:16:81",
            "60:b4:f7:fc:33:8c",
        },
    },
    {
        .name_exists = true,
        .name = "dev_ut_attributes",
        .device_value_len = 2,
        .device_value =
        {
            "foo.http_dev",
            "foo.sni_dev",
        },
        .cloud_value_len = 3,
        .cloud_value =
        {
            "foo.http_cloud",
            "foo.sni_cloud",
            "foo.app_cloud",
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
        "@tag_1",
        "tag_2",
    }
};

/**
 * @brief a set of sessions as delivered by the ovsdb API
 */
struct schema_Flow_Service_Manager_Config g_confs[] =
{
    /* parser plugin, no type provided, idx: 0 */
    {
        .handler = "fsm_session_test_0",
        .plugin = "plugin_0",
        .pkt_capt_filter = "bpf_filter_0",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "policy_table",                 /* FSM policy entry */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/0",      /* topic */
            "test_dso_init",                /* plugin init routine */
            "test_policy",                  /* FSM policy entry */
        },
        .other_config_len = 3,
    },

    /* parser plugin, type provided, idx: 1 */
    {
        .handler = "fsm_session_test_1",
        .plugin = "plugin_1",
        .pkt_capt_filter = "bpf_filter_1",
        .type = "parser",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "provider",                     /* service provider */
            "policy_table",                 /* FSM policy entry */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/1",      /* topic */
            "test_dso_init",                /* plugin init routine */
            "test_provider",                /* service provider */
            "test_policy",                  /* FSM policy entry */
        },
        .other_config_len = 4,
    },

    /* web categorization plugin, idx: 2 */
    {
        .handler = "fsm_session_test_2",
        .plugin = "plugin_2",
        .type = "web_cat_provider",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/2",      /* topic */
            "test_dso_init",                /* plugin init routine */
        },
        .other_config_len = 2,
    },

    /* dpi plugin, idx: 3 */
    {
        .handler = "fsm_session_test_3",
        .plugin = "plugin_3",
        .type = "dpi",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/3",      /* topic */
            "test_dso_init",                /* plugin init routine */
        },
        .other_config_len = 2,
    },

    /* bogus plugin, idx: 4 */
    {
        .handler = "fsm_bogus_plugin",
        .plugin = "bogus",
        .type = "bogus",
        .other_config_len = 0,
    },

    /* idx: 5 */
    {
        .handler = "fsm_session_test_5",
        .plugin = "plugin_5",
        .pkt_capt_filter = "bpf_filter_5",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "policy_table",                 /* FSM policy entry */
            "tx_intf",                      /* Plugin tx interface */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/5",      /* topic */
            "test_dso_init",                /* plugin init routine */
            "test_policy",                  /* FSM policy entry */
            "test_intf.tx",                 /* Plugin tx interface */
        },
        .other_config_len = 4,
    },

    /* dpi dispatch plugin, idx: 6 */
    {
        .handler = "fsm_session_test_6",
        .plugin = "plugin_6",
        .type = "dpi_dispatcher",
        .other_config_keys =
        {
            "dso_init",                     /* plugin init routine */
            "tap_type",
        },
        .other_config =
        {
            "test_6_dso_init",              /* plugin init routine */
            "fsm_tap_nfqueues",
        },
        .other_config_len = 2,
    },

    /* dpi plugin, idx: 7 */
    {
        .handler = "fsm_session_test_7",
        .plugin = "plugin_7",
        .type = "dpi_plugin",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "dpi_dispatcher",               /* dpi dispatcher */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/7",      /* topic */
            "test_7_dso_init",              /* plugin init routine */
            "fsm_session_test_6"            /* dpi dispatcher */
        },
        .other_config_len = 3,
    },

    /* parser plugin, type provided, idx: 8 */
    {
        .handler = "fsm_session_test_8",
        .plugin = "plugin_8",
        .pkt_capt_filter = "bpf_filter_8",
        .type = "parser",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "provider_plugin",              /* service provider */
            "policy_table",                 /* FSM policy entry */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/8",      /* topic */
            "test_dso_init",                /* plugin init routine */
            "fsm_session_provider_plugin",  /* service provider */
            "fsm_test_policy",              /* FSM policy entry */
        },
        .other_config_len = 4,
    },

    /* web categorization plugin, idx: 9 */
    {
        .handler = "fsm_session_provider_plugin",
        .plugin = "plugin_9",
        .type = "web_cat_provider",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/9",      /* topic */
            "test_dso_init",                /* plugin init routine */
        },
        .other_config_len = 2,
    },

    /* dpi dispatch plugin, idx: 10 */
    {
        .handler = "fsm_session_test_10",
        .plugin = "plugin_10",
        .type = "dpi_dispatcher",
        .other_config_keys =
        {
            "dso_init",                     /* plugin init routine */
            "included_devices",
        },
        .other_config =
        {
            "test_10_dso_init",             /* plugin init routine */
            "${@tag_1}",
        },
        .other_config_len = 2,
    },

    /* dpi dispatch plugin, idx: 11 */
    {
        .handler = "fsm_session_test_11",
        .plugin = "plugin_11",
        .type = "dpi_plugin",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "dpi_dispatcher",               /* dpi dispatcher */
            "targeted_devices",
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/11",    /* topic */
            "test_10_dso_init",            /* plugin init routine */
            "fsm_session_test_10",         /* dpi dispatcher */
            "$[group_tag]",
        },
        .other_config_len = 4,
    },

     /* dpi dispatch plugin, idx: 12 */
    {
        .handler = "fsm_session_test_12",
        .plugin = "plugin_12",
        .type = "dpi_dispatcher",
        .other_config_keys =
        {
            "dso_init",                     /* plugin init routine */
            "tap_type",
        },
        .other_config =
        {
            "test_12_dso_init",              /* plugin init routine */
            "fsm_tap_nfqueues",
        },
        .other_config_len = 2,
    },

    /* dpi plugin, idx: 13 */
    {
        .handler = "fsm_session_test_13",
        .plugin = "plugin_13",
        .type = "dpi_plugin",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "dpi_dispatcher",               /* dpi dispatcher */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/13",    /* topic */
            "test_13_dso_init",             /* plugin init routine */
            "fsm_session_test_6",           /* dpi dispatcher */
        },
        .other_config_len = 3,
    },

    /* dpi dispatch plugin, idx: 14 */
    {
        .handler = "fsm_session_test_14",
        .plugin = "plugin_14",
        .type = "dpi_client",
        .other_config_keys =
        {
            "dso_init",                     /* plugin init routine */
            "dpi_plugin",
            "flow_attributes",
        },
        .other_config =
        {
            "test_14_dso_init",              /* plugin init routine */
            "fsm_session_test_13",
            "${dev_ut_attributes}",
        },
        .other_config_len = 3,
    },

    /* dpi dispatch plugin, idx: 15 */
    {
        .handler = "fsm_session_test_15",
        .plugin = "plugin_15",
        .type = "dpi_client",
        .other_config_keys =
        {
            "dso_init",                     /* plugin init routine */
            "dpi_plugin",
            "flow_attributes",
        },
        .other_config =
        {
            "test_15_dso_init",              /* plugin init routine */
            "fsm_session_test_13",
            "$[group_tag]",
        },
        .other_config_len = 3,
    },

    /* dpi dispatch plugin, idx: 16 */
    {
        .handler = "fsm_session_test_16",
        .plugin = "plugin_16",
        .type = "dpi_dispatcher",
        .other_config_keys =
        {
            "dso_init",                     /* plugin init routine */
            "excluded_devices",
        },
        .other_config =
        {
            "test_16_dso_init",             /* plugin init routine */
            "${@tag_1}",
        },
        .other_config_len = 2,
    },

    /* dpi dispatch plugin, idx: 17 */
    {
        .handler = "fsm_session_test_17",
        .plugin = "plugin_17",
        .type = "dpi_plugin",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "dpi_dispatcher",               /* dpi dispatcher */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/14",    /* topic */
            "test_16_dso_init",             /* plugin init routine */
            "fsm_session_test_16",          /* dpi dispatcher */
        },
        .other_config_len = 3,
    },

    /* dpi dispatch plugin, idx: 18 */
    {
        .handler = "fsm_session_test_18",
        .plugin = "plugin_18",
        .type = "dpi_dispatcher",
        .other_config_keys =
        {
            "dso_init",                     /* plugin init routine */
            "excluded_devices",
            "included_devices",
        },
        .other_config =
        {
            "test_18_dso_init",             /* plugin init routine */
            "${@tag_1}",
            "${@tag_2}",
        },
        .other_config_len = 3,
    },

    /* dpi plugin, idx: 19 */
    {
        .handler = "fsm_session_test_19",
        .plugin = "plugin_19",
        .type = "dpi_plugin",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "dpi_dispatcher",               /* dpi dispatcher */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/15",    /* topic */
            "test_16_dso_init",             /* plugin init routine */
            "fsm_session_test_18",          /* dpi dispatcher */
        },
        .other_config_len = 3,
    },

    /* web categorization plugin, idx: 20 */
    {
        .handler = "test_provider",
        .plugin = "test_provider",
        .type = "web_cat_provider",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "wc_health_stats_topic",
            "wc_hero_stats_topic",
            "wc_health_stats_interval_secs",
            "wc_hero_stats_interval_secs",
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/20",      /* topic */
            "test_dso_init",                /* plugin init routine */
            "WC/Stats/Health/dog1/620ac9f536d2348e7eb6788a/AAAAAAAAA",
            "WC/Stats/Hero/dog1/620ac9f536d2348e7eb6788a/AAAAAAAAA",
            "900",
            "900",
        },
        .other_config_len = 5,
    },
    /* dpi dispatch plugin, idx: 21 */
    {
        .handler = "fsm_session_test_21",
        .plugin = "plugin_21",
        .type = "dpi_dispatcher",
        .other_config_keys =
        {
            "dso_init",                     /* plugin init routine */
            "included_devices",
            "excluded_devices",
        },
        .other_config =
        {
            "test_10_dso_init",             /* plugin init routine */
            "${tag_1}",
            "${tag_2}",
        },
        .other_config_len = 3,
    },
    /* dpi dispatch plugin, idx: 22 */
    {
        .handler = "fsm_session_test_22",
        .plugin = "plugin_21",
        .type = "dpi_plugin",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "dpi_dispatcher",               /* dpi dispatcher */
            "targeted_devices",
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/11",    /* topic */
            "test_10_dso_init",            /* plugin init routine */
            "fsm_session_test_10",         /* dpi dispatcher */
            "tag1",
        },
        .other_config_len = 4,
    },
    /* dpi dispatch plugin, idx: 23 */
    {
        .handler = "fsm_session_test_23",
        .plugin = "plugin_23",
        .type = "dpi_dispatcher",
        .other_config_keys =
        {
            "dso_init",                     /* plugin init routine */
            "included_devices",
        },
        .other_config =
        {
            "test_23_dso_init",             /* plugin init routine */
            "${tag_2}",
        },
        .other_config_len = 2,
    },
    /* dpi plugin, idx: 14 */
    {
        .handler = "fsm_session_test_24",
        .plugin = "plugin_24",
        .type = "dpi_plugin",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "dpi_dispatcher",               /* dpi dispatcher */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/24",    /* topic */
            "test_24_dso_init",             /* plugin init routine */
            "fsm_session_test_23",          /* dpi dispatcher */
        },
        .other_config_len = 3,
    },
    /* dpi plugin, idx: 15 */
    {
        .handler = "fsm_session_test_25",
        .plugin = "plugin_25",
        .type = "dpi_plugin",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "dpi_dispatcher",               /* dpi dispatcher */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/25",    /* topic */
            "test_25_dso_init",             /* plugin init routine */
            "fsm_session_test_23",          /* dpi dispatcher */
        },
        .other_config_len = 3,
    },
    /* dpi plugin, idx: 16 */
    {
        .handler = "fsm_session_test_26",
        .plugin = "plugin_26",
        .type = "dpi_plugin",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "dpi_dispatcher",               /* dpi dispatcher */
        },
        .other_config =
        {
            "dev-test/IP/Flows/ut/0/26",    /* topic */
            "test_26_dso_init",             /* plugin init routine */
            "fsm_session_test_23",          /* dpi dispatcher */
        },
        .other_config_len = 3,
    },
};

/**
 * @brief a AWLAN_Node structure
 */
struct schema_AWLAN_Node g_awlan_nodes[] =
{
    {
        .mqtt_headers_keys =
        {
            "locationId",
            "nodeId",
        },
        .mqtt_headers =
        {
            "59efd33d2c93832025330a3e",
            "4C718002B3",
        },
        .mqtt_headers_len = 2,
    },
    {
        .mqtt_headers_keys =
        {
            "locationId",
            "nodeId",
        },
        .mqtt_headers =
        {
            "59efd33d2c93832025330a3e",
            "4C7XXXXXXX",
        },
        .mqtt_headers_len = 2,
    },
};


struct schema_FSM_Policy g_spolicies[] =
{
    { /* entry 1. Always matching, action is set. */
        .policy_exists = true,
        .policy = "fsm_test_policy",
        .name = "block_facebook",
        .idx = 1,
        .mac_op_exists = false,
        .fqdn_op_exists = true,
        .fqdn_op = "in",
        .fqdns_len = 1,
        .fqdns =
        {
            "facebook",
        },
        .fqdncat_op_exists = false,
        .action_exists = true,
        .action = "block",
        .log_exists = true,
        .log = "all",
    }
};



bool
test_register_client(struct fsm_session *dpi_plugin,
                     struct fsm_session *dpi_client,
                     char *attribute)
{
    LOGI("%s: %s: registering client %s for attribute %s", __func__,
         dpi_plugin->name, dpi_client->name, attribute);

    return true;
}


bool
test_unregister_client(struct fsm_session *dpi_plugin,
                       char *attribute)
{
    LOGI("%s: %s: unregistering attribute %s", __func__,
         dpi_plugin->name, attribute);

    return true;
}


int
test_cmp_flow_attributes(const void *a, const void *b)
{
    const char *str_a = a;
    const char *str_b = b;
    int cmp;

    cmp = strcmp(str_a, str_b);
    return cmp;
}


void
test_notify_client(struct fsm_session *client, char *attribute, char *value)
{
    LOGI("%s: calling client %s for attribute %s", __func__,
         client->name, attribute);
}

void
test_fsm_notify_dispatcher_tap_type(struct fsm_session *session, uint32_t tap_type)
{
    LOGI("%s: tap_type: %d", __func__, tap_type);
    g_dispatch_tap_type = tap_type;
}

int
test_7_dso_init(struct fsm_session *session)
{
    session->ops.notify_dispatcher_tap_type = test_fsm_notify_dispatcher_tap_type;
    LOGI("%s: here  for session %s", __func__, session->name);
    return 0;
}



int
test_13_dso_init(struct fsm_session *session)
{
    struct fsm_dpi_plugin_ops *ops;

    ops = &session->p_ops->dpi_plugin_ops;
    ops->register_client = test_register_client;
    ops->unregister_client = test_unregister_client;
    ops->flow_attr_cmp = test_cmp_flow_attributes;
    LOGI("%s: here  for session %s", __func__, session->name);

    return 0;
}


int
test_client_callback(struct fsm_session *client, const char *attr,
                     uint8_t type, uint16_t length, const void *value,
                     struct fsm_dpi_plugin_client_pkt_info *pkt_info)
{
    LOGI("%s: calling client %s for attribute %s", __func__,
         client->name, attr);

    return 0;
}


int
test_14_dso_init(struct fsm_session *session)
{
    struct fsm_dpi_plugin_client_ops *ops;

    LOGI("%s: here  for session %s", __func__, session->name);

    ops = &session->p_ops->dpi_plugin_client_ops;
    ops->process_attr = test_client_callback;

    return 0;
}

void
test_fsm_notify_identical_plugin_status(struct fsm_session *session, bool status)
{
    g_identical_plugin_enabled = status;
    LOGN("%s: identical plugin enabled : %s", __func__, status ? "true" : "false");
}

int
test_25_dso_init(struct fsm_session *session)
{
    session->ops.notify_identical_sessions = test_fsm_notify_identical_plugin_status;
    session->plugin_id = FSM_DNS_PLUGIN;
    LOGI("%s: here  for session %s", __func__, session->name);
    return 0;
}

void
test_fsm_notify_identical_plugin_status1(struct fsm_session *session, bool status)
{
    g_identical_plugin_enabled1 = status;
    LOGN("%s: identical plugin enabled : %s", __func__, status ? "true" : "false");
}

int
test_26_dso_init(struct fsm_session *session)
{
    session->ops.notify_identical_sessions = test_fsm_notify_identical_plugin_status1;
    session->plugin_id = FSM_DPI_DNS_PLUGIN;
    LOGI("%s: here  for session %s", __func__, session->name);
    return 0;
}

typedef int (*dso_init)(struct fsm_session *session);

/**
 * @brief maps function name to actual function
 * Used to mock plugins' init function
 */
struct ut_dso_init
{
    char *fname;
    dso_init fn;
} g_dso_inits[] =
{
    {
        .fname = "test_7_dso_init",
        .fn = test_7_dso_init,
    },
    {
        .fname = "test_13_dso_init",
        .fn = test_13_dso_init,
    },
    {
        .fname = "test_14_dso_init",
        .fn = test_14_dso_init,
    },
    {
        .fname = "test_25_dso_init",
        .fn = test_25_dso_init,
    },
    {
        .fname = "test_26_dso_init",
        .fn = test_26_dso_init,
    },
};


static bool
test_init_plugin(struct fsm_session *session)
{
    struct ut_dso_init *init;
    size_t nelems;
    char *fname;
    size_t i;

    fname = session->ops.get_config(session, "dso_init");
    if (fname == NULL) return true;

    nelems = sizeof(g_dso_inits) / sizeof(g_dso_inits[0]);
    for (i = 0; i < nelems; i++)
    {
        bool rc;
        int cmp;

        init = &g_dso_inits[i];
        cmp = strcmp(fname, init->fname);
        if (cmp != 0) continue;

        if (init->fn == NULL) continue;
        rc = init->fn(session);
        if (rc) return false;
    }

    fsm_wrap_init_plugin(session);

    return true;
}

static bool
test_init_plugin_fail(struct fsm_session *session)
{
    return false;
}


static int
test_set_dpi_mark(struct net_header_parser *net_hdr, struct dpi_mark_policy *mark_policy)
{
    return 1;
}

static const char g_test_br[16] = "test_br";
static int
test_get_br(char *if_name, char *bridge, size_t len)
{
    strscpy(bridge, g_test_br, len);
    return 0;
}


static bool
test_send_report(struct net_md_aggregator *aggr, char *mqtt_topic)
{
    if (aggr == NULL) return false;
    if (mqtt_topic == NULL) return false;

    aggr->held_flows = 0;

    /* Reset the aggregator */
    net_md_reset_aggregator(aggr);

    return true;
}


static bool
test_update_session_tap(struct fsm_session *session)
{
    /* update session tap type */
    session->tap_type = fsm_session_tap_mode(session);
    session->set_dpi_mark = test_set_dpi_mark;
    return true;
}


void
fsm_core_setUp(void)
{
    struct fsm_policy_session *policy_mgr;
    size_t nelems;
    size_t i;
    bool ret;

    g_dispatch_tap_type = 0;
    g_identical_plugin_enabled = false;
    g_identical_plugin_enabled1 = false;
    fsm_init_mgr(NULL);
    g_mgr->init_plugin = test_init_plugin;
    g_mgr->get_br = test_get_br;
    g_mgr->update_session_tap = test_update_session_tap;
    nelems = ARRAY_SIZE(g_confs);
    for (i = 0; i < nelems; i++)
    {
        struct schema_Flow_Service_Manager_Config *conf;

        conf = &g_confs[i];
        memset(conf->if_name, 0, sizeof(conf->if_name));
    }
    fsm_get_awlan_headers(&g_awlan_nodes[0]);

    nelems = sizeof(g_tags) / sizeof(*g_tags);
    for (i = 0; i < nelems; i++)
    {
        ret = om_tag_add_from_schema(&g_tags[i]);
        TEST_ASSERT_TRUE(ret);
    }

    ret = om_tag_group_add_from_schema(&g_tag_group);
    TEST_ASSERT_TRUE(ret);

    policy_mgr = fsm_policy_get_mgr();
    if (!policy_mgr->initialized) fsm_init_manager();

    /* This will be creating a temp folder for each of the tests in the UT */
    ut_prepare_pcap(Unity.CurrentTestName);

    /* Enable tcp  mid flow processing by nfe*/
    nfe_conntrack_tcp_midflow = 1;
}


void
fsm_core_tearDown(void)
{
    struct fsm_policy_session *policy_mgr = fsm_policy_get_mgr();
    struct policy_table *table, *t_to_remove;
    struct fsm_policy *fpolicy, *p_to_remove;
    ds_tree_t *tables_tree, *policies_tree;
    size_t len;
    size_t i;
    bool ret;

    g_dispatch_tap_type = 0;
    g_identical_plugin_enabled = false;
    g_identical_plugin_enabled1 = false;
    len = ARRAY_SIZE(g_tags);
    for (i = 0; i < len; i++)
    {
        ret = om_tag_remove_from_schema(&g_tags[i]);
        TEST_ASSERT_TRUE(ret);
    }
    ret = om_tag_group_remove_from_schema(&g_tag_group);
    TEST_ASSERT_TRUE(ret);

    fsm_reset_mgr();

    /* Clean up policies */
    tables_tree = &policy_mgr->policy_tables;
    table = ds_tree_head(tables_tree);
    while (table != NULL)
    {
        policies_tree = &table->policies;
        fpolicy = ds_tree_head(policies_tree);
        while (fpolicy != NULL)
        {
            p_to_remove = fpolicy;
            fpolicy = ds_tree_next(policies_tree, fpolicy);
            fsm_free_policy(p_to_remove);
        }
        t_to_remove = table;
        table = ds_tree_next(tables_tree, table);
        ds_tree_remove(tables_tree, t_to_remove);
        FREE(t_to_remove);
    }

    g_mgr->init_plugin = NULL;

    ut_cleanup_pcap();
}


/**
 * @brief update mqtt_headers
 *
 * setUp() sets the mqtt headers. Validate the original values,
 * update and validate.
 */
void
test_add_awlan_headers(void)
{
    struct fsm_mgr *mgr;
    char *location_id;
    char *expected;
    char *node_id;

    mgr = fsm_get_mgr();

    /* Validate original headers */
    location_id = mgr->location_id;
    TEST_ASSERT_NOT_NULL(location_id);
    expected = g_awlan_nodes[0].mqtt_headers[0];
    TEST_ASSERT_EQUAL_STRING(expected, location_id);
    node_id = mgr->node_id;
    TEST_ASSERT_NOT_NULL(node_id);
    expected = g_awlan_nodes[0].mqtt_headers[1];
    TEST_ASSERT_EQUAL_STRING(expected, node_id);

    /* Update headers, validate again */
    fsm_get_awlan_headers(&g_awlan_nodes[1]);

    location_id = mgr->location_id;
    TEST_ASSERT_NOT_NULL(location_id);
    expected = g_awlan_nodes[1].mqtt_headers[0];
    TEST_ASSERT_EQUAL_STRING(expected, location_id);
    node_id = mgr->node_id;
    TEST_ASSERT_NOT_NULL(node_id);
    expected = g_awlan_nodes[1].mqtt_headers[1];
    TEST_ASSERT_EQUAL_STRING(expected, node_id);
}


/**
 * @brief create a fsm_session
 *
 * Create a fsm session after AWLAN_node was populated
 */
void
test_add_session_after_awlan(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session_conf *fconf;
    struct fsm_session *session;
    ds_tree_t *sessions;
    char *topic;

    conf = &g_confs[0];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);

    /* Validate session configuration */
    fconf = session->conf;
    TEST_ASSERT_NOT_NULL(fconf);
    TEST_ASSERT_EQUAL_STRING(conf->handler, fconf->handler);
    TEST_ASSERT_EQUAL_STRING(conf->handler, session->name);
    topic = session->ops.get_config(session, "mqtt_v");
    TEST_ASSERT_EQUAL_STRING(conf->other_config[0], topic);
}


/**
 * @brief validate plugin types
 */
void
test_plugin_types(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    int type;

    /* Parser type, not explicitly configured */
    conf = &g_confs[0];
    type = fsm_service_type(conf);
    TEST_ASSERT_EQUAL_INT(FSM_PARSER, type);

    /* Parser type, explicitly configured */
    conf = &g_confs[1];
    type = fsm_service_type(conf);
    TEST_ASSERT_EQUAL_INT(FSM_PARSER, type);

    /* web categorization type */
    conf = &g_confs[2];
    type = fsm_service_type(conf);
    TEST_ASSERT_EQUAL_INT(FSM_WEB_CAT, type);

    /* dpi type */
    conf = &g_confs[3];
    type = fsm_service_type(conf);
    TEST_ASSERT_EQUAL_INT(FSM_DPI, type);

    /* dpi dispatch type */
    conf = &g_confs[6];
    type = fsm_service_type(conf);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_DISPATCH, type);

    /* dpi plugin type */
    conf = &g_confs[7];
    type = fsm_service_type(conf);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_PLUGIN, type);

    /* dpi dispatch type */
    conf = &g_confs[10];
    type = fsm_service_type(conf);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_DISPATCH, type);

    /* dpi plugin type */
    conf = &g_confs[11];
    type = fsm_service_type(conf);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_PLUGIN, type);

    /* bogus type */
    conf = &g_confs[4];
    type = fsm_service_type(conf);
    TEST_ASSERT_EQUAL_INT(FSM_UNKNOWN_SERVICE, type);

    /* dpi dispatch type */
    conf = &g_confs[12];
    type = fsm_service_type(conf);
    TEST_ASSERT_EQUAL_INT(FSM_DPI_DISPATCH, type);
}


/**
 * @brief validate session duplication
 */
void
test_duplicate_session(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session *session;
    ds_tree_t *sessions;
    char *service_name;
    bool ret;

    /* Add a session with an explicit provider */
    conf = &g_confs[1];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);

    /* Create the related service provider */
    ret = fsm_dup_web_cat_session(session);
    TEST_ASSERT_TRUE(ret);

    service_name = session->ops.get_config(session, "provider");
    TEST_ASSERT_NOT_NULL(service_name);
    session = ds_tree_find(sessions, service_name);
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_EQUAL_INT(FSM_WEB_CAT, session->type);
    TEST_ASSERT_EQUAL_STRING(CONFIG_INSTALL_PREFIX"/lib/libfsm_test_provider.so",
                             session->dso);
}


/**
 * @brief validate session duplication failure
 */
void
test_duplicate_session_fail(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session *session;
    ds_tree_t *sessions;

    /* Force init_plugin() failure */
    g_mgr->init_plugin = test_init_plugin_fail;

    /* Add a session with an explicit provider */
    conf = &g_confs[1];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NULL(session);
}


/**
 * @brief validate session tx interface when provided by the controller
 */
void
test_tx_intf_controller(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session *session;
    ds_tree_t *sessions;

    conf = &g_confs[5];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_NOT_EQUAL(0, strlen(session->tx_intf));
    TEST_ASSERT_EQUAL_STRING("test_intf.tx", session->tx_intf);
}


/**
 * @brief validate session tx interface when provided through Kconfig
 */
void
test_tx_intf_kconfig(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session *session;
    ds_tree_t *sessions;
    char tx_intf[IFNAMSIZ];

#if !defined(CONFIG_TARGET_LAN_BRIDGE_NAME)
#define FORCE_BRIDGE_NAME 1
#define CONFIG_TARGET_LAN_BRIDGE_NAME "kconfig_br"
#endif

    memset(tx_intf, 0, sizeof(tx_intf));
    snprintf(tx_intf, sizeof(tx_intf), "%s", CONFIG_TARGET_LAN_BRIDGE_NAME);

    LOGI("%s: CONFIG_TARGET_LAN_BRIDGE_NAME == %s", __func__, CONFIG_TARGET_LAN_BRIDGE_NAME);

    conf = &g_confs[0];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_NOT_EQUAL(0, strlen(session->tx_intf));
    TEST_ASSERT_EQUAL_STRING(tx_intf, session->tx_intf);

#if defined(FORCE_BRIDGE_NAME)
#undef CONFIG_TARGET_LAN_BRIDGE_NAME
#undef FORCE_BRIDGE_NAME
#endif
}


/**
 * @brief validate the creation of a dpi dispatch plugin
 */
void
test_1_dpi_dispatcher(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_session *session;
    ds_tree_t *sessions;

    conf = &g_confs[6];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_TRUE(fsm_is_dpi(session));

    dispatcher_dpi_context = session->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    TEST_ASSERT_NOT_NULL(dpi_dispatcher->session);
    TEST_ASSERT_TRUE(dpi_dispatcher->session = session);
}


/**
 * @brief validate the creation of a dpi plugin
 */
void
test_1_dpi_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_plugin *dpi_plugin;
    struct fsm_session *session;
    ds_tree_t *sessions;

    conf = &g_confs[7];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_TRUE(fsm_is_dpi(session));

    plugin_dpi_context = session->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);

    dpi_plugin = &plugin_dpi_context->plugin;
    TEST_ASSERT_NOT_NULL(dpi_plugin->session);
    TEST_ASSERT_TRUE(dpi_plugin->session == session);
    TEST_ASSERT_FALSE(dpi_plugin->bound);
}


/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi dispatch plugin is registered first.
 * The dpi plugin is registered thereafter.
 */
void
test_1_dpi_dispatcher_and_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_plugin *plugin_lookup;
    struct fsm_session *dispatcher;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Add a dpi plugin session */
    conf = &g_confs[7];
    fsm_add_session(conf);
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(plugin));

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);
}


/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 */
void
test_2_dpi_dispatcher_and_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_plugin *plugin_lookup;
    struct fsm_session *dispatcher;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;

    /* Add a dpi plugin session */
    conf = &g_confs[7];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(plugin));

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);
}


void
test_fsm_dpi_handler(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_session *session;
    ds_tree_t *sessions;
    size_t len;

    conf = &g_confs[6];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);

    dispatcher_dpi_context = session->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;

    UT_CREATE_PCAP_PAYLOAD(pkt372, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    dispatch_ops = &session->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(session, net_parser);
}

/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * A packet is then handled, creating a flow record
 * The dpi plugin is then removed.
 */
void
test_3_dpi_dispatcher_and_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    size_t len;

    /* Add a dpi plugin session */
    conf = &g_confs[7];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(plugin));

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;

    UT_CREATE_PCAP_PAYLOAD(pkt372, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Remove the dpi plugin session */
    conf = &g_confs[7];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);
}


void
dummy_dpi_plugin_free_resources(struct fsm_session *session)
{
    LOGI("%s : Free dpi_plugin resources", __func__);
}


union fsm_plugin_ops *p_ops;
struct fsm_dpi_plugin_ops g_plugin_ops =
{
    .dpi_free_resources = dummy_dpi_plugin_free_resources,
};


/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * A packet is then handled, creating a flow record
 * The dpi plugin is then removed.
 */
void
test_dpi_dispatch_delete(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    size_t len;

    /* Add a dpi plugin session */
    conf = &g_confs[7];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(plugin));
    plugin->p_ops->dpi_plugin_ops = g_plugin_ops;

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;

    UT_CREATE_PCAP_PAYLOAD(pkt372, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Remove the dpi dispatch session */
    conf = &g_confs[6];
    fsm_delete_session(conf);

    /* Remove the dpi plugin session */
    conf = &g_confs[7];
    fsm_delete_session(conf);
}


/**
 * @brief validate the timing out of a flow
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * A packet is then handled, creating a flow record.
 * The periodic routine is called twice with a delay greater than the
 * the programmed life of a flow. Validate that the flow is removed.
 * The dpi plugin is then removed.
 */
void
test_4_dpi_dispatcher_and_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    size_t len;

    /* Add a dpi plugin session */
    conf = &g_confs[7];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(plugin));

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

    /* Set the acc ttl to 1 second */
    aggr->acc_ttl = 1;
    nfe_conntrack_icmp_timeout = 1;
    nfe_conntrack_udp_timeout = 1;
    nfe_conntrack_tcp_timeout_est = 1;
    nfe_conntrack_update_timeouts(aggr->nfe_ct);
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;

    UT_CREATE_PCAP_PAYLOAD(pkt372, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* call the periodic routine to trigger a report */
    fsm_dpi_periodic(dispatcher);

    /* sleep for 2 seconds */
    sleep(2);

    /* call the periodic routine, the original accumulator should be removed */
    fsm_dpi_periodic(dispatcher);
    fsm_dpi_recycle_nfe_conns();

    TEST_ASSERT_EQUAL_INT(0, aggr->total_flows);

    /* Remove the dpi plugin session */
    conf = &g_confs[7];
    fsm_delete_session(conf);
}


/**
 * @brief validate the timing out of a flow
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * A packet is then handled, creating a flow record.
 * Bump up the flow's ref count so it's not removed when outdated.
 * The periodic routine is called twice with a delay greater than the
 * the programmed life of a flow. Validate that the flow is not removed.
 * Reset the flow' refcount. Validate it's getting removed once outdated.
 * The dpi plugin is then removed.
 */
void
test_5_dpi_dispatcher_and_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct net_md_stats_accumulator *acc;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    size_t len;

    /* Add a dpi plugin session */
    conf = &g_confs[7];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(plugin));

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

    /* Set the acc ttl to 1 second */
    aggr->acc_ttl = 1;
    nfe_conntrack_icmp_timeout = 1;
    nfe_conntrack_udp_timeout = 1;
    nfe_conntrack_tcp_timeout_est = 1;
    nfe_conntrack_update_timeouts(aggr->nfe_ct);
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;

    UT_CREATE_PCAP_PAYLOAD(pkt372, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);
    acc = net_parser->acc;

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* call the periodic routine to trigger a report */
    fsm_dpi_periodic(dispatcher);

    /* sleep for 2 seconds */
    sleep(2);

    /*
     * call the periodic routine, the original accumulator should still exist
     * as its ref count is not null
     */
    fsm_dpi_periodic(dispatcher);

    /* re process the packet */
    dispatch_ops->handler(dispatcher, net_parser);

    TEST_ASSERT_EQUAL_INT(1, aggr->total_flows);

    /* Validate that the accumulator is the same as  the original */
    TEST_ASSERT_NOT_NULL(net_parser->acc);
    TEST_ASSERT_FALSE(acc == net_parser->acc);

    /* call the periodic routine to trigger a report */
    fsm_dpi_periodic(dispatcher);

    /* sleep for 2 seconds */
    sleep(2);

    /*
     * call the periodic routine, the original accumulator should be deleted
     * as its ref count is not null
     */
    fsm_dpi_periodic(dispatcher);
    fsm_dpi_recycle_nfe_conns();
    TEST_ASSERT_EQUAL_INT(0, aggr->total_flows);

    /* Remove the dpi plugin session */
    conf = &g_confs[7];
    fsm_delete_session(conf);
}


/**
 * @brief validates the sequence policy table/parser/web cat service
 */
void
test_6_service_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_web_cat_ops *provider_ops;
    struct fsm_session *web_cat_plugin;
    struct fsm_session *parser_plugin;
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy_client *client;
    struct policy_table *table;
    struct fsm_policy *fpolicy;
    ds_tree_t *sessions;

    /* Add the parser plugin */
    conf = &g_confs[8];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    parser_plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(parser_plugin);
    TEST_ASSERT_NULL(parser_plugin->service);
    TEST_ASSERT_NULL(parser_plugin->provider_plugin);

    /* Add web cat plugin */
    conf = &g_confs[9];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    web_cat_plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(web_cat_plugin);

    /* Validate the service provider settings */
    TEST_ASSERT_NOT_NULL(parser_plugin->service);
    TEST_ASSERT_TRUE(parser_plugin->service == web_cat_plugin);
    TEST_ASSERT_NOT_NULL(parser_plugin->provider_plugin);
    TEST_ASSERT_TRUE(parser_plugin->provider_plugin == web_cat_plugin);
    provider_ops = &web_cat_plugin->p_ops->web_cat_ops;
    TEST_ASSERT_TRUE(parser_plugin->provider_ops == provider_ops);

    /* Add policy */
    spolicy = &g_spolicies[0];
    fsm_add_policy(spolicy);
    fpolicy = fsm_policy_lookup(spolicy);
    TEST_ASSERT_NOT_NULL(fpolicy);

    client = &parser_plugin->policy_client;
    table = fpolicy->table;
    TEST_ASSERT_TRUE(client->table == table);
}

/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * TCP packets are handled.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_7_dpi_dispatcher_and_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct net_md_stats_accumulator *acc;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    char *mqtt_topic;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[11];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[10];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    /* TCP SYN Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt858, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate the originator for syn packet */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    /* Validate the direction of flow for SYN packet */
    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* TCP SYN_ACK PKT */
    UT_CREATE_PCAP_PAYLOAD(pkt862, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate the originator for SYN ACK packet */
    acc = net_parser->acc;
    TEST_ASSERT_EQUAL_INT(NET_MD_ACC_FIRST_LEG, acc->flags & NET_MD_ACC_FIRST_LEG);


    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* TCP DATA PKT */
    UT_CREATE_PCAP_PAYLOAD(pkt372, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate the originator and direction */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);
    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* TCP SYN PKT and macs are known */
    UT_CREATE_PCAP_PAYLOAD(pkt869, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate the originator and direction */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);
    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_LAN2LAN_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* TCP SYN_ACK PKT and macs are known */
    UT_CREATE_PCAP_PAYLOAD(pkt870, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate the originator and direction */
    acc = net_parser->acc;
    TEST_ASSERT_EQUAL_INT(NET_MD_ACC_FIRST_LEG, acc->flags & NET_MD_ACC_FIRST_LEG);
    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_LAN2LAN_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[11];
    fsm_delete_session(conf);
    acc = net_parser->acc;
    TEST_ASSERT_EQUAL_INT(NET_MD_ACC_FIRST_LEG,  acc->flags & NET_MD_ACC_FIRST_LEG);
    TEST_ASSERT_NOT_NULL(acc->dpi_plugins);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);

    /* Force to expire lingering connections.*/
    nfe_conntrack_icmp_timeout = 0;
    nfe_conntrack_udp_timeout = 0;
    nfe_conntrack_tcp_timeout_est = 0;
    nfe_conntrack_ether_timeout = 0;
    nfe_conntrack_update_timeouts(aggr->nfe_ct);
    fsm_dpi_periodic(dispatcher);
    fsm_dpi_recycle_nfe_conns();
}

/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * UDP packets are handled.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_8_dpi_dispatcher_udp_non_reserved(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    char *mqtt_topic;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[11];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[10];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    /* UDP PKT SRC MAC is known. ports are non reserved */
    UT_CREATE_PCAP_PAYLOAD(pkt98, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate the originator and direction */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);
    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* UDP PKT SRC MAC is known. ports are non reserved */
    MEMZERO(dpi_dispatcher->net_parser);
    UT_CREATE_PCAP_PAYLOAD(pkt1, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate the originator and direction */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);
    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_INBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* UDP Unknown macs and ports are non reserved */
    MEMZERO(dpi_dispatcher->net_parser);
    UT_CREATE_PCAP_PAYLOAD(pkt18, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that no accumulator was created */
    TEST_ASSERT_NULL(net_parser->acc);

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[11];
    fsm_delete_session(conf);

    /* Force to expire lingering connections.*/
    nfe_conntrack_icmp_timeout = 0;
    nfe_conntrack_udp_timeout = 0;
    nfe_conntrack_tcp_timeout_est = 0;
    nfe_conntrack_update_timeouts(aggr->nfe_ct);
    fsm_dpi_periodic(dispatcher);
    fsm_dpi_recycle_nfe_conns();
}

/**
 * @brief validate fsm session tap type
 */
void
test_fsm_tap_type_validation(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_session *session;
    ds_tree_t *sessions;
    int tap_type;

    conf = &g_confs[12];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_TRUE(fsm_is_dpi(session));

    dispatcher_dpi_context = session->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    TEST_ASSERT_NOT_NULL(dpi_dispatcher->session);
    TEST_ASSERT_TRUE(dpi_dispatcher->session = session);

    tap_type = session->tap_type;
    TEST_ASSERT_EQUAL_INT(FSM_TAP_NFQ, tap_type);
}


/**
 * @brief validate the registration of a dpi client plugin
 *
 * The dpi plugin is registered first.
 * The dpi client plugin is registered thereafter.
 */
void
test_1_dpi_plugin_and_client_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct reg_client_session *client_head;
    struct fsm_session *dpi_plugin_client;
    struct fsm_dpi_plugin *plugin_lookup;
    struct fsm_session *dispatcher;
    struct fsm_session *dpi_plugin;
    om_tag_list_entry_t *tag_item;
    struct dpi_client *client;
    log_severity_t old_log;
    ds_tree_t *dpi_plugins;
    ds_tree_t *tag_values;
    char *attributes_tag;
    ds_tree_t *sessions;
    ds_tree_t *attrs;
    om_tag_t *tag;

    sessions = fsm_get_sessions();

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Add a dpi plugin session */
    conf = &g_confs[13];
    fsm_add_session(conf);
    dpi_plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dpi_plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(dpi_plugin));

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = dpi_plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, dpi_plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == dpi_plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    /* Add a dpi client_plugin session */
    conf = &g_confs[14];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    dpi_plugin_client = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dpi_plugin_client);
    TEST_ASSERT_TRUE(fsm_is_dpi_client(dpi_plugin_client));

    /* Access the tag containing the attributes we care about */
    attributes_tag = dpi_plugin_client->ops.get_config(dpi_plugin_client,
                                                       "flow_attributes");
    TEST_ASSERT_NOT_NULL(attributes_tag);

    /* Access the dpi plugin's flow attributes tree */
    attrs = &dpi_plugin->dpi->plugin.dpi_clients;

    /* Makes the internal 'print' visible */
    old_log = log_severity_get();
    log_severity_set(LOG_SEVERITY_TRACE);

    /* Get the actual tag from its name */
    tag = om_tag_find(attributes_tag);
    TEST_ASSERT_NOT_NULL(tag);
    tag_values = &tag->values;
    ds_tree_foreach(tag_values, tag_item)
    {
        client = ds_tree_find(attrs, tag_item->value);
        TEST_ASSERT_NOT_NULL(client);
        TEST_ASSERT_EQUAL(1, client->num_sessions);
        fsm_print_one_dpi_client(client);
        /* We are sure there is only one entry, so simply fetch the head */
        client_head = ds_tree_head(&client->reg_sessions);
        TEST_ASSERT_EQUAL(dpi_plugin_client, client_head->session);
        dpi_plugin->p_ops->dpi_plugin_ops.notify_client(dpi_plugin,
                                                        tag_item->value,
                                                        0, 0, NULL, NULL);
    }

    /* Restore log level */
    log_severity_set(old_log);

    /* Remove the dpi client session */
    conf = &g_confs[14];
    fsm_delete_session(conf);
}

void
test_tag_update(void)
{
    struct schema_Openflow_Tag ovsdb_tag =
        { /* Used to test entry 2 update */
            .name_exists = true,
            .name = "dev_ut_attributes",
            .device_value_len = 3,
            .device_value =
            {
                "foo.http_dev",
                "foo.sni_dev",
                "foo.app_dev",
            },
            .cloud_value_len = 2,
            .cloud_value =
            {
                "foo.http_cloud",
                "foo.sni_cloud",
            },
        };

    om_tag_update_from_schema(&ovsdb_tag);
}

/**
 * @brief validate the registration of a dpi client plugin
 *
 * The dpi client plugin is registered first.
 * The dpi plugin is registered thereafter.
 */
void
test_2_dpi_plugin_and_client_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct reg_client_session *client_head;
    struct fsm_session *dpi_plugin_client;
    struct fsm_dpi_plugin *plugin_lookup;
    struct fsm_session *dispatcher;
    struct fsm_session *dpi_plugin;
    om_tag_list_entry_t *tag_item;
    struct dpi_client *client;
    ds_tree_t *dpi_plugins;
    log_severity_t old_log;
    ds_tree_t *tag_values;
    char *attributes_tag;
    ds_tree_t *sessions;
    ds_tree_t *attrs;
    om_tag_t *tag;

    sessions = fsm_get_sessions();

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Add a dpi client_plugin session */
    conf = &g_confs[14];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    dpi_plugin_client = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dpi_plugin_client);
    TEST_ASSERT_TRUE(fsm_is_dpi_client(dpi_plugin_client));

    /* Add a dpi plugin session */
    conf = &g_confs[13];
    fsm_add_session(conf);
    dpi_plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dpi_plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(dpi_plugin));

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = dpi_plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, dpi_plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == dpi_plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    /* Access the tag containing the attributes we care about */
    attributes_tag = dpi_plugin_client->ops.get_config(dpi_plugin_client,
                                                       "flow_attributes");
    TEST_ASSERT_NOT_NULL(attributes_tag);

    /* Access the dpi plugin's flow attributes tree */
    attrs = &dpi_plugin->dpi->plugin.dpi_clients;

    /* Makes the internal 'print' visible */
    old_log = log_severity_get();
    log_severity_set(LOG_SEVERITY_TRACE);

    /* Get the actual tag from its name */
    tag = om_tag_find(attributes_tag);
    TEST_ASSERT_NOT_NULL(tag);
    tag_values = &tag->values;
    ds_tree_foreach(tag_values, tag_item)
    {
        client = ds_tree_find(attrs, tag_item->value);
        TEST_ASSERT_NOT_NULL(client);
        TEST_ASSERT_EQUAL(1, client->num_sessions);
        fsm_print_one_dpi_client(client);

        /* We are sure there is only one entry, so simply fetch the head */
        client_head = ds_tree_head(&client->reg_sessions);
        TEST_ASSERT_EQUAL(dpi_plugin_client, client_head->session);
        dpi_plugin->p_ops->dpi_plugin_ops.notify_client(dpi_plugin,
                                                        tag_item->value,
                                                        0, 0, NULL, NULL);
    }

    /* Restore log level */
    log_severity_set(old_log);

    /* Remove the dpi client session */
    conf = &g_confs[14];
    fsm_delete_session(conf);
}

/**
 * @brief check if tags are added to monitor list
 *
 */
void
test_tags_added_to_monitor_list(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session *dpi_plugin_client;
    struct fsm_dpi_client_tags *dpi_tag;
    struct fsm_session *dpi_plugin;
    struct fsm_session *dispatcher;
    char *attributes_tag;
    ds_tree_t *sessions;
    struct fsm_mgr *mgr;
    om_tag_t *tag;

    sessions = fsm_get_sessions();

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Add a dpi client_plugin session */
    conf = &g_confs[14];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    dpi_plugin_client = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dpi_plugin_client);
    TEST_ASSERT_TRUE(fsm_is_dpi_client(dpi_plugin_client));

    /* Add a dpi plugin session */
    conf = &g_confs[13];
    fsm_add_session(conf);
    dpi_plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dpi_plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(dpi_plugin));

    /* Access the tag containing the attributes we care about */
    attributes_tag = dpi_plugin_client->ops.get_config(dpi_plugin_client,
                                                       "flow_attributes");
    tag = om_tag_find(attributes_tag);

    /* verify the tag is monitor list for updates */
    mgr = fsm_get_mgr();
    dpi_tag = ds_tree_find(&mgr->dpi_client_tags_tree, tag->name);
    TEST_ASSERT_NOT_NULL(dpi_tag);

    /* Remove the dpi client session */
    conf = &g_confs[14];
    fsm_delete_session(conf);
}

/**
 * @brief check if group tags are added to monitor list
 *
 */
void
test_adding_group_tags(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session *dpi_plugin_client;
    struct fsm_session *dpi_plugin;
    struct fsm_session *dispatcher;
    struct fsm_dpi_client_tags *dpi_tag;
    char *attributes_tag;
    ds_tree_t *sessions;
    struct fsm_mgr *mgr;
    om_tag_t *tag;

    sessions = fsm_get_sessions();

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Add a dpi client_plugin session */
    conf = &g_confs[15];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    dpi_plugin_client = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dpi_plugin_client);
    TEST_ASSERT_TRUE(fsm_is_dpi_client(dpi_plugin_client));

    /* Add a dpi plugin session */
    conf = &g_confs[13];
    fsm_add_session(conf);
    dpi_plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dpi_plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(dpi_plugin));

    /* Access the tag containing the attributes we care about */
    attributes_tag = dpi_plugin_client->ops.get_config(dpi_plugin_client,
                                                       "flow_attributes");
    tag = om_tag_find(attributes_tag);

    /* verify the tag is monitor list for updates */
    mgr = fsm_get_mgr();
    dpi_tag = ds_tree_find(&mgr->dpi_client_tags_tree, tag->name);
    TEST_ASSERT_NOT_NULL(dpi_tag);

    /* Remove the dpi client session */
    conf = &g_confs[15];
    fsm_delete_session(conf);
}

/**
 * @brief helper function to create the client session
 *        and dpi lugin session
 */
void
create_client_plugin_session(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session *dpi_plugin_client;
    struct fsm_session *dpi_plugin;
    struct fsm_session *dispatcher;

    ds_tree_t *sessions;

    sessions = fsm_get_sessions();

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Add a dpi client_plugin session */
    conf = &g_confs[14];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    dpi_plugin_client = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dpi_plugin_client);
    TEST_ASSERT_TRUE(fsm_is_dpi_client(dpi_plugin_client));

    /* Add a dpi plugin session */
    conf = &g_confs[13];
    fsm_add_session(conf);
    dpi_plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dpi_plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(dpi_plugin));
}

/**
+ * @brief helper function to delete dpi_client_plugin session
+ *
+ */
void
free_client_plugin_session(void)
{
    struct schema_Flow_Service_Manager_Config *conf;

    conf = &g_confs[14];
    fsm_delete_session(conf);
}

/**
 * @brief test if new flow attribute is registered
 *        with dpi plugin
 * initial flow attribute values:
 * foo.http_dev, foo.sni_dev, foo.http_cloud, foo.sni_cloud
 * foo.app_cloud
 */
void
test_add_new_tag_value(void)
{
    struct schema_Openflow_Tag ovsdb_tag =
    {
        .name_exists = true,
        .name = "dev_ut_attributes",
        .device_value_len = 3,
        .device_value =
        {
            "foo.http_dev",
            "foo.sni_dev",
            "foo.new_app", /* new entry */
        },
        .cloud_value_len = 3,
        .cloud_value =
        {
            "foo.http_cloud",
            "foo.sni_cloud",
            "foo.app_cloud",
        },
    };

    create_client_plugin_session();
    om_tag_update_from_schema(&ovsdb_tag);
    free_client_plugin_session();
}

/**
 * @brief test if deleted flow attribute is unregistered
 *        with dpi plugin
 * initial flow attribute values:
 * foo.http_dev, foo.sni_dev, foo.http_cloud, foo.sni_cloud
 * foo.app_cloud
 */
void
test_del_tag_value(void)
{
    struct schema_Openflow_Tag ovsdb_tag =
    {
        .name_exists = true,
        .name = "dev_ut_attributes",
        .device_value_len = 3,
        .device_value =
        {
            "foo.http_dev",
            "foo.sni_dev",
            "foo.app_dev", /* added attribute */
        },
        .cloud_value_len = 2,
        .cloud_value =
        {
            "foo.http_cloud",
            "foo.sni_cloud",
            /* "foo.app_cloud", */
        },
    };

    create_client_plugin_session();
    om_tag_update_from_schema(&ovsdb_tag);
    free_client_plugin_session();
}

/**
 * @brief test for update flow attributes
 *        deleted attributes should be unregistered
 *        added attributes should be registered
 * initial flow attribute values:
 * foo.http_dev, foo.sni_dev, foo.http_cloud, foo.sni_cloud
 * foo.app_cloud
 */
void
test_tag_update_value(void)
{
    struct schema_Openflow_Tag ovsdb_tag =
    {
        .name_exists = true,
        .name = "dev_ut_attributes",
        .device_value_len = 3,
        .device_value =
        {
            "foo.http_dev",
            "foo.sni_dev",
            "foo.app_dev",    /* added attribute */
        },
        .cloud_value_len = 2,
        .cloud_value =
        {
            "foo.http_cloud",
            "foo.sni_cloud",
            /* "foo.app_cloud", */ /* deleted attribute */
        },
    };

    create_client_plugin_session();
    om_tag_update_from_schema(&ovsdb_tag);
    free_client_plugin_session();
}

/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * TCP packets are handled.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_9_dpi_dispatcher_excluded_devices(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    char *mqtt_topic;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[17];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[16];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    /* TCP SYN Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt858, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate the originator for syn packet */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    /* Validate the direction of flow for SYN packet */
    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_INBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[17];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);
}


/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * UDP packets are handled.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_10_dpi_dispatcher_included_excluded_devices(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    char *mqtt_topic;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[19];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[18];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    /* TCP SYN Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt858, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    net_header_logi(net_parser);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* source mac is in the excluded devices */
    /* Validate that an accumulator not was created */
    TEST_ASSERT_NULL(net_parser->acc);

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[19];
    fsm_delete_session(conf);
}


/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * UDP & TCP packets with reserved ports are handled.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_11_dpi_dispatcher_reserved_port_originator(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    char *mqtt_topic;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[19];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[18];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    /* UDP port 50 inbound Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt200, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    net_header_logi(net_parser);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate the originator for inbound packet */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_INBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* UDP port 50 Outbound Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt201, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate the originator for outbound packet */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* TCP port 50 inbound Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt202, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate the originator for inbound packet */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_INBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* TCP port 50 Outbound Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt201, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate the originator for outbound packet */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[19];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);
}


/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * Verify protocol is ICMP or not.
 * Validate ICMP type.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_12_dpi_dispatcher_icmp_req_reply(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    char *mqtt_topic;
    size_t icmp_type;
    size_t protocol;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[19];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[18];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    /* OUTBOUND PING REQUEST */
    /* ICMP ECHO Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt183, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    net_header_logi(net_parser);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is ICMP or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_ICMP);

    /* Validate ICMP type */
    icmp_type = net_parser->acc->key->icmp_type;
    TEST_ASSERT_EQUAL_INT(icmp_type, ICMP_ECHO);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* ICMP Reply Packet */
    MEMZERO(dpi_dispatcher->net_parser);
    UT_CREATE_PCAP_PAYLOAD(pkt184, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    net_header_logi(net_parser);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is ICMP or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_ICMP);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* INBOUND PING REQUEST */
    /* ICMP ECHO Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt185, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is ICMP or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_ICMP);

    /* Validate ICMP type */
    icmp_type = net_parser->acc->key->icmp_type;
    TEST_ASSERT_EQUAL_INT(icmp_type, ICMP_ECHO);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_INBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* ICMP Reply Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt186, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is ICMP or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_ICMP);


    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[19];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);
}


/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * Verify protocol is ICMPv6 or not.
 * Validate ICMPv6 type.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_13_dpi_dispatcher_icmpv6_req_reply(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    char *mqtt_topic;
    size_t icmp_type;
    size_t protocol;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[19];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[18];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    /* OUTBOUND PING6 packet */
    /* ICMPv6 ECHO Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt10, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is ICMPv6 or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_ICMPV6);

    /* Validate ICMPv6 type */
    icmp_type = net_parser->acc->key->icmp_type;
    TEST_ASSERT_EQUAL_INT(icmp_type, ICMP6_ECHO_REQUEST);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* ICMPv6 Reply Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt11, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);


    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* INBOUND PING6 REQUEST */
    /* ICMPv6 ECHO Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt12, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is ICMPv6 or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_ICMPV6);

    /* Validate ICMPv6 type */
    icmp_type = net_parser->acc->key->icmp_type;
    TEST_ASSERT_EQUAL_INT(icmp_type, ICMP6_ECHO_REQUEST);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_INBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* ICMPv6 Reply Packet */
    UT_CREATE_PCAP_PAYLOAD(pkt13, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);


    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[19];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);

    /* Force to expire lingering connections.*/
    nfe_conntrack_icmp_timeout = 0;
    nfe_conntrack_udp_timeout = 0;
    nfe_conntrack_tcp_timeout_est = 0;
    nfe_conntrack_update_timeouts(aggr->nfe_ct);
    fsm_dpi_periodic(dispatcher);
    fsm_dpi_recycle_nfe_conns();
}

void
test_fsm_tap_type_from_str(void)
{
    uint32_t retval;

    retval = fsm_tap_type_from_str(NULL);
    TEST_ASSERT_EQUAL(FSM_TAP_PCAP, retval);

    retval = fsm_tap_type_from_str("");
    TEST_ASSERT_EQUAL(FSM_TAP_PCAP, retval);

    retval = fsm_tap_type_from_str("fsm_tap_pcap");
    TEST_ASSERT_EQUAL(FSM_TAP_PCAP, retval);

    retval = fsm_tap_type_from_str("fsm_tap_nfqueues");
    TEST_ASSERT_EQUAL(FSM_TAP_NFQ, retval);

    retval = fsm_tap_type_from_str("fsm_tap_pcap,fsm_tap_nfqueues");
    TEST_ASSERT_EQUAL((FSM_TAP_PCAP | FSM_TAP_NFQ), retval);

    retval = fsm_tap_type_from_str("broken");
    TEST_ASSERT_EQUAL(FSM_TAP_PCAP, retval);

    retval = fsm_tap_type_from_str("tsm_tap_pcap, fsm_tap_nfqueues");
    TEST_ASSERT_EQUAL(FSM_TAP_PCAP, retval);

    retval = fsm_tap_type_from_str("tsm_tap_pcap ,fsm_tap_nfqueues");
    TEST_ASSERT_EQUAL(FSM_TAP_NFQ, retval);
}

/**
 * @brief validate the registration of a dpi plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * UDP packets are handled.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_dpi_dispatcher_udp_reserved(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    char *mqtt_topic;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[11];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[10];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    /* UDP PKT dport is reserved */
    UT_CREATE_PCAP_PAYLOAD(pkt41, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate the originator and direction */
    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);
    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* UDP PKT sport is reserved */
    UT_CREATE_PCAP_PAYLOAD(pkt42, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);


    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[11];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);

    /* Force to expire lingering connections.*/
    nfe_conntrack_icmp_timeout = 0;
    nfe_conntrack_udp_timeout = 0;
    nfe_conntrack_tcp_timeout_est = 0;
    nfe_conntrack_update_timeouts(aggr->nfe_ct);

    fsm_dpi_periodic(dispatcher);
    fsm_dpi_recycle_nfe_conns();
}

/**
 * @brief validate direction for dns request
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * Validate sport sport is 53 or not.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_dpi_dispatcher_dns_request(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    uint16_t port_num;
    char *mqtt_topic;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[24];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[23];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    UT_CREATE_PCAP_PAYLOAD(pkt46, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    net_header_logi(net_parser);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /** Validate dns dst port number */
    port_num = net_parser->acc->key->dport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 53);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[24];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);
}

/**
 * @brief validate direction for dns response
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * Validate dport sport is 53 or not.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_dpi_dispatcher_dns_response(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    uint16_t port_num;
    char *mqtt_topic;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[24];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[23];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    UT_CREATE_PCAP_PAYLOAD(pkt47, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    net_header_logi(net_parser);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /** Validate dns dst port number */
    port_num = net_parser->acc->key->sport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 53);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_DST);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[24];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);
}

/**
 * @brief validate direction for tcp http request & response
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * Validate protocol is tcp or not.
 * Validate dport sport/dport is 80 or not.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_dpi_dispatcher_tcp_http_req_response(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    uint16_t port_num;
    char *mqtt_topic;
    size_t protocol;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[24];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[23];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    UT_CREATE_PCAP_PAYLOAD(pkt3502, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    net_header_logi(net_parser);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is TCP or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_TCP);

    /** Validate http dst port number */
    port_num = net_parser->acc->key->dport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 80);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    MEMZERO(dpi_dispatcher->net_parser);
    UT_CREATE_PCAP_PAYLOAD(pkt3501, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is TCP or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_TCP);

    /** Validate http src port number */
    port_num = net_parser->acc->key->dport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 80);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[24];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);


    /* Force to expire lingering connections.*/
    nfe_conntrack_icmp_timeout = 0;
    nfe_conntrack_udp_timeout = 0;
    nfe_conntrack_tcp_timeout_est = 0;
    nfe_conntrack_update_timeouts(aggr->nfe_ct);

    fsm_dpi_periodic(dispatcher);
    fsm_dpi_recycle_nfe_conns();
}

/**
 * @brief validate direction for tcp https request & response
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * Validate protocol is tcp or not.
 * Validate dport sport/dport is 443 or not.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_dpi_dispatcher_tcp_https_req_response(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    uint16_t port_num;
    char *mqtt_topic;
    size_t protocol;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[24];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[23];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    UT_CREATE_PCAP_PAYLOAD(pkt442, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is TCP or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_TCP);

    /** Validate http dst port number */
    port_num = net_parser->acc->key->dport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 443);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    MEMZERO(dpi_dispatcher->net_parser);
    UT_CREATE_PCAP_PAYLOAD(pkt443, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is TCP or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_TCP);

    /** Validate http src port number */
    port_num = net_parser->acc->key->dport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 443);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[24];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);


    /* Force to expire lingering connections.*/
    nfe_conntrack_icmp_timeout = 0;
    nfe_conntrack_udp_timeout = 0;
    nfe_conntrack_tcp_timeout_est = 0;
    nfe_conntrack_update_timeouts(aggr->nfe_ct);

    fsm_dpi_periodic(dispatcher);
    fsm_dpi_recycle_nfe_conns();
}

/**
 * @brief validate direction for udp https request & response
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * Validate protocol is udp or not.
 * Validate dport sport/dport is 443 or not.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_dpi_dispatcher_udp_https_req_response(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    uint16_t port_num;
    char *mqtt_topic;
    size_t protocol;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[24];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[23];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    UT_CREATE_PCAP_PAYLOAD(pkt667, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is UDP or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_UDP);

    /** Validate http dst port number */
    port_num = net_parser->acc->key->dport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 443);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_SRC);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    MEMZERO(dpi_dispatcher->net_parser);
    UT_CREATE_PCAP_PAYLOAD(pkt668, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /* Validate packet is UDP or not */
    protocol = net_parser->acc->key->ipprotocol;
    TEST_ASSERT_EQUAL_INT(protocol, IPPROTO_UDP);

    /** Validate http src port number */
    port_num = net_parser->acc->key->sport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 443);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[24];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);

    /* Force to expire lingering connections.*/
    nfe_conntrack_icmp_timeout = 0;
    nfe_conntrack_udp_timeout = 0;
    nfe_conntrack_tcp_timeout_est = 0;
    nfe_conntrack_update_timeouts(aggr->nfe_ct);

    fsm_dpi_periodic(dispatcher);
    fsm_dpi_recycle_nfe_conns();

}

/**
 * @brief validate direction for dhcp discover
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * Validate sport dport is 67, 68 and vice versa.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_dpi_dispatcher_udp_dhcp_discover(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    uint16_t port_num;
    char *mqtt_topic;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[17];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[16];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    UT_CREATE_PCAP_PAYLOAD(pkt120, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /** Validate dns dst port number */
    port_num = net_parser->acc->key->dport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 67);

    /** Validate dns src port number */
    port_num = net_parser->acc->key->sport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 68);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(NET_MD_ACC_ORIGINATOR_SRC, originator);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(NET_MD_ACC_LAN2LAN_DIR, direction);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[17];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);
}

/**
 * @brief validate direction for dhcp offer
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * Validate sport dport is 67, 68 and vice versa.
 * Verify flow direction and originator of each packet.
 * Send Protobuf to cloud.
 * The dpi plugin is then removed.
 */
void
test_dpi_dispatcher_udp_dhcp_offer(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_dpi_flow_info *info;
    struct fsm_session *dispatcher;
    struct net_md_aggregator *aggr;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    uint16_t originator;
    uint16_t direction;
    uint16_t port_num;
    char *mqtt_topic;
    size_t len;
    bool ret;

    /* Add a dpi plugin session */
    conf = &g_confs[17];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[16];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;
    aggr = dpi_dispatcher->aggr;

#if defined(__x86_64__)
    /* Set the send_report routine of the aggregator */
    aggr->send_report = test_send_report;
#endif

    UT_CREATE_PCAP_PAYLOAD(pkt125, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Validate that an accumulator was created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);

    /* Validate that the accumulator has plugins recorded */
    TEST_ASSERT_NOT_NULL(net_parser->acc->dpi_plugins);

    /* Validate that the accumulator is aware of the plugin */
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(info->session == plugin);

    /** Validate dns dst port number */
    port_num = net_parser->acc->key->dport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 68);

    /** Validate dns src port number */
    port_num = net_parser->acc->key->sport;
    TEST_ASSERT_EQUAL_INT(htons(port_num), 67);

    originator = net_parser->acc->originator;
    TEST_ASSERT_EQUAL_INT(originator, NET_MD_ACC_ORIGINATOR_DST);

    direction = net_parser->acc->direction;
    TEST_ASSERT_EQUAL_INT(direction, NET_MD_ACC_OUTBOUND_DIR);

    net_parser->acc->report = true;

    /* Close the flows observation window */
    net_md_close_active_window(aggr);

    mqtt_topic = plugin->ops.get_config(plugin, "mqtt_v");
    ret = aggr->send_report(aggr, mqtt_topic);
    TEST_ASSERT_TRUE(ret);

    /* Remove the dpi plugin session */
    conf = &g_confs[17];
    fsm_delete_session(conf);
    info = ds_tree_find(net_parser->acc->dpi_plugins, plugin);
    TEST_ASSERT_NULL(info);
}

extern void run_test_fsm_ovsdb(void);

/**
 * @brief validate acc for ethernet packet
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 * Pass ethernet packet to dispatcher and verify acc
 * The dpi plugin is then removed.
 * Pass ethernet packet to dispacther
 */
void
test_dpi_dispatcher_no_ip_pkt(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct fsm_dpi_plugin *plugin_lookup;
    struct net_header_parser *net_parser;
    struct fsm_parser_ops *dispatch_ops;
    struct fsm_session *dispatcher;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;
    size_t len;
    bool ret;

    log_severity_set(LOG_SEVERITY_TRACE);
    /* Add a dpi plugin session */
    conf = &g_confs[17];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    ret = fsm_is_dpi(plugin);
    TEST_ASSERT_TRUE(ret);
    LOGI("DPI Plugin is success ");

    /* Add a dpi dispatcher session */
    conf = &g_confs[16];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    ret = fsm_is_dpi(dispatcher);
    TEST_ASSERT_TRUE(ret);

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;

    UT_CREATE_PCAP_PAYLOAD(pkt100, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);

    /* Call the dispatcher's packet handler */
    dispatch_ops = &dispatcher->p_ops->parser_ops;
    TEST_ASSERT_NOT_NULL(dispatch_ops->handler);
    dispatch_ops->handler(dispatcher, net_parser);

    /* Accumulator should be created */
    TEST_ASSERT_NOT_NULL(net_parser->acc);
    TEST_ASSERT_EQUAL(NET_MD_ACC_ETH, net_parser->acc->flags & NET_MD_ACC_ETH);
    TEST_ASSERT_EQUAL(0, net_parser->acc->flags & NET_MD_ACC_FIVE_TUPLE);
    /* Remove the dpi plugin session */
    conf = &g_confs[17];
    fsm_delete_session(conf);

    fsm_dpi_periodic(dispatcher);
    fsm_dpi_recycle_nfe_conns();
}


/**
 * @brief parser_webcat session
 *
 * The parser plugin is registered first.
 * it adds webcat plugin if it is not added
 * Add webcat plugin as new plugin
 * Verify webcat plugin is updated or not.
 */
void
test_parser_webcat_session(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session *session;
    char *health_stats_topic;
    char *hero_stats_topic;
    ds_tree_t *sessions;
    char *service_name;
    char *topic;
    bool ret;

    /* Add a session with an explicit provider */
    conf = &g_confs[1];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);

    /* Create the related service provider */
    ret = fsm_dup_web_cat_session(session);
    TEST_ASSERT_TRUE(ret);

    service_name = session->ops.get_config(session, "provider");
    TEST_ASSERT_NOT_NULL(service_name);
    session = ds_tree_find(sessions, service_name);
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_EQUAL_INT(FSM_WEB_CAT, session->type);
    TEST_ASSERT_EQUAL_STRING(CONFIG_INSTALL_PREFIX"/lib/libfsm_test_provider.so",
                             session->dso);
    topic = session->ops.get_config(session, "mqtt_v");
    health_stats_topic = session->ops.get_config(session, "wc_health_stats_topic");
    TEST_ASSERT_NULL(health_stats_topic);
    hero_stats_topic = session->ops.get_config(session, "wc_hero_stats_topic");
    TEST_ASSERT_NULL(hero_stats_topic);
    LOGI("%s: service provider mqtt: %s health_stats_topic: %s hero_stats_topic: %s", __func__, topic, health_stats_topic, hero_stats_topic);

    /* Add same webcat session as new plugin */
    conf = &g_confs[20];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_EQUAL_INT(FSM_WEB_CAT, session->type);
    TEST_ASSERT_EQUAL_STRING(CONFIG_INSTALL_PREFIX"/lib/libfsm_test_provider.so",
                             session->dso);
    topic = session->ops.get_config(session, "mqtt_v");
    health_stats_topic = session->ops.get_config(session, "wc_health_stats_topic");
    TEST_ASSERT_NOT_NULL(health_stats_topic);
    hero_stats_topic = session->ops.get_config(session, "wc_hero_stats_topic");
    TEST_ASSERT_NOT_NULL(hero_stats_topic);
    LOGI("%s: updated service provider mqtt: %s health_stats_topic: %s hero_stats_topic: %s", __func__, topic, health_stats_topic, hero_stats_topic);
}


/**
 * @brief webcat_parser session
 *
 * Register webcat plugin.
 * Register parser plugin.
 * Verify webcat plugin is updated or not.
 */
void
test_webcat_parser_session(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session *session;
    char *health_stats_topic;
    char *hero_stats_topic;
    ds_tree_t *sessions;
    char *service_name;
    char *topic;
    bool ret;

    /* Add webcat session */
    conf = &g_confs[20];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_EQUAL_INT(FSM_WEB_CAT, session->type);
    topic = session->ops.get_config(session, "mqtt_v");
    health_stats_topic = session->ops.get_config(session, "wc_health_stats_topic");
    TEST_ASSERT_NOT_NULL(health_stats_topic);
    hero_stats_topic = session->ops.get_config(session, "wc_hero_stats_topic");
    TEST_ASSERT_NOT_NULL(hero_stats_topic);
    LOGI("%s: webcat session mqtt: %s health_stats_topic: %s hero_stats_topic: %s", __func__, topic, health_stats_topic, hero_stats_topic);

    /* Add a session with an explicit provider */
    conf = &g_confs[1];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);

    /* Create the related service provider */
    ret = fsm_dup_web_cat_session(session);
    TEST_ASSERT_TRUE(ret);

    service_name = session->ops.get_config(session, "provider");
    TEST_ASSERT_NOT_NULL(service_name);
    session = ds_tree_find(sessions, service_name);
    TEST_ASSERT_NOT_NULL(session);
    TEST_ASSERT_EQUAL_INT(FSM_WEB_CAT, session->type);
    topic = session->ops.get_config(session, "mqtt_v");
    health_stats_topic = session->ops.get_config(session, "wc_health_stats_topic");
    TEST_ASSERT_NOT_NULL(health_stats_topic);
    hero_stats_topic = session->ops.get_config(session, "wc_hero_stats_topic");
    TEST_ASSERT_NOT_NULL(hero_stats_topic);
    LOGI("%s: service provider mqtt: %s health_stats_topic: %s hero_stats_topic: %s", __func__, topic, health_stats_topic, hero_stats_topic);
}


/**
 * @brief test the dispatcher include exclude settings
 */
void
test_dispatcher_device_process(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    struct fsm_dpi_dispatcher *dpi_dispatcher;
    struct net_header_parser *net_parser;
    struct fsm_session *session;
    ds_tree_t *sessions;
    bool process;
    size_t len;
    int rc;

    /* Add a dpi dispatcher session */
    conf = &g_confs[21];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(session);

    dispatcher_dpi_context = session->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);

    dpi_dispatcher = &dispatcher_dpi_context->dispatch;
    net_parser = &dpi_dispatcher->net_parser;

    TEST_ASSERT_NOT_NULL(net_parser);
    /* The packet is multicast, the source mac is part of included devices */
    UT_CREATE_PCAP_PAYLOAD(pkt11055, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    net_header_logi(net_parser);

    /* Validate the included/excluded devices tag settings */
    rc = strncmp(dpi_dispatcher->included_devices, "${tag_1}", strlen("${tag_1}"));
    TEST_ASSERT_EQUAL(0, rc);

    rc = strncmp(dpi_dispatcher->excluded_devices, "${tag_2}", strlen("${tag_2}"));
    TEST_ASSERT_EQUAL(0, rc);

    /* The source mac is part of tag_1, destination mac is multicast */

    /* smac is included devices, expect processing */
    process = fsm_dpi_should_process(net_parser, "${tag_1}", "${tag_2}");
    TEST_ASSERT_TRUE(process);

    /* No included devices, smac not in excluded devices. Expect processing */
    process = fsm_dpi_should_process(net_parser, NULL, "${tag_2}");
    TEST_ASSERT_TRUE(process);

    /* No excluded devices, smac in included devices. Expect processing */
    process = fsm_dpi_should_process(net_parser, "${tag_1}", NULL);
    TEST_ASSERT_TRUE(process);

    /* source mac part of excluded targets. Do not expect processing */
    process = fsm_dpi_should_process(net_parser, "${tag_2}", "${tag_1}");
    TEST_ASSERT_FALSE(process);

    /* No included devices, source mac part of excluded targets. Do not expect processing */
    process = fsm_dpi_should_process(net_parser, NULL, "${tag_1}");
    TEST_ASSERT_FALSE(process);

    /* No excluded devices, smac not in included devices. Do not expect processing */
    process = fsm_dpi_should_process(net_parser, "${tag_2}", NULL);
    TEST_ASSERT_FALSE(process);
}


/**
 * @brief validate dpi_dispatch tap_type notification in dpi_plugin
 *
 * The dpi dispatch plugin is registered first.
 * The dpi plugin is registered thereafter.
 */
void
test_notify_dispatcher_tap_type(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_plugin *plugin_lookup;
    struct fsm_session *dispatcher;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Add a dpi plugin session */
    conf = &g_confs[7];
    fsm_add_session(conf);
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(plugin));

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    /* validate dpi_plugin tap type */
    TEST_ASSERT_EQUAL_INT(g_dispatch_tap_type, 2);
}


/**
 * @brief validate dpi_dispatch tap_type notification in dpi_plugin
 *
 * The dpi plugin is registered first.
 * The dpi dispatch plugin is registered thereafter.
 */
void
test_1_notify_dispatcher_tap_type(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    union fsm_dpi_context *dispatcher_dpi_context;
    union fsm_dpi_context *plugin_dpi_context;
    struct fsm_dpi_plugin *plugin_lookup;
    struct fsm_session *dispatcher;
    struct fsm_session *plugin;
    ds_tree_t *dpi_plugins;
    ds_tree_t *sessions;

    /* Add a dpi plugin session */
    conf = &g_confs[7];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(plugin));

    /* validate dpi_plugin tap type */
    TEST_ASSERT_EQUAL_INT(g_dispatch_tap_type, 0);

    /* Add a dpi dispatcher session */
    conf = &g_confs[6];
    fsm_add_session(conf);
    dispatcher = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(dispatcher);
    TEST_ASSERT_TRUE(fsm_is_dpi(dispatcher));

    /* Validate that the dpi plugin is registered to the dispatcher */
    dispatcher_dpi_context = dispatcher->dpi;
    TEST_ASSERT_NOT_NULL(dispatcher_dpi_context);
    plugin_dpi_context = plugin->dpi;
    TEST_ASSERT_NOT_NULL(plugin_dpi_context);
    dpi_plugins = &dispatcher_dpi_context->dispatch.plugin_sessions;
    plugin_lookup = ds_tree_find(dpi_plugins, plugin->name);
    TEST_ASSERT_NOT_NULL(plugin_lookup);
    TEST_ASSERT_TRUE(plugin_lookup->session == plugin);
    TEST_ASSERT_TRUE(plugin_lookup->bound);

    /* validate dpi_plugin tap type */
    TEST_ASSERT_EQUAL_INT(g_dispatch_tap_type, 2);
}


/**
 * @brief validate identical plugin info
 *
 * Register dns_plugin.
 * Register dpi_dns_plugin.
 * Verify identical plugin status.
 * Unregister dpi_dns plugin
 * Verify identical plugin status.
 */
void
test_notify_identical_plugin(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct fsm_session *plugin;
    ds_tree_t *sessions;

    /* Add a dns plugin */
    conf = &g_confs[25];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(plugin));

    /* validate identifical_plugin_enabled or not */
    TEST_ASSERT_FALSE(g_identical_plugin_enabled);

    /* Add a dpi_dns plugin */
    conf = &g_confs[26];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NOT_NULL(plugin);
    TEST_ASSERT_TRUE(fsm_is_dpi(plugin));

    /* validate identifical_plugin_enabled or not in dns_session */
    TEST_ASSERT_TRUE(g_identical_plugin_enabled);

    /* validate identifical_plugin_enabled or not in dpi_dns session */
    TEST_ASSERT_TRUE(g_identical_plugin_enabled1);

    /* Remove the dns plugin */
    conf = &g_confs[25];
    fsm_delete_session(conf);
    plugin = ds_tree_find(sessions, conf->handler);
    TEST_ASSERT_NULL(plugin);

    /* validate identifical_plugin_enabled or not */
    TEST_ASSERT_FALSE(g_identical_plugin_enabled);

    /* validate identifical_plugin_enabled or not */
    TEST_ASSERT_FALSE(g_identical_plugin_enabled1);
}

void
test_check_ip_multicast(void)
{
    uint8_t src_buf[sizeof(struct in6_addr)];
    uint8_t dst_buf[sizeof(struct in6_addr)];
    struct net_md_flow_key key;
    size_t len;
    size_t i;
    int s;
    int rc;

    struct test_ip
    {
        char *src_ip;
        char *dst_ip;
        int ip_version;
        int af;
        bool expect;
    } test_ips[] =
    {
        {
            .src_ip = "192.168.40.2",
            .dst_ip = "1.2.3.4",
            .ip_version = 4,
            .af = AF_INET,
            .expect = false,
        },
        {
            .src_ip = "192.168.40.2",
            .dst_ip = "224.0.0.1",
            .ip_version = 4,
            .af = AF_INET,
            .expect = true,
        },
        {
            .src_ip = "192.168.40.2",
            .dst_ip = "255.255.1.1",
            .ip_version = 4,
            .af = AF_INET,
            .expect = true,
        },
        {
            .src_ip = "2601:646:8a00:9c4::6746:8e26",
            .dst_ip = "2a03:2880:f031:13:face:b00c:0:1823",
            .ip_version = 6,
            .af = AF_INET6,
            .expect = false,
        },
        {
            .src_ip = "2601:646:8a00:9c4::6746:8e26",
            .dst_ip = "fe80::225:90ff:fe87:175d",
            .ip_version = 6,
            .af = AF_INET6,
            .expect = false,
        },
        {
            .src_ip = "2601:646:8a00:9c4::6746:8e26",
            .dst_ip = "ff02::1",
            .ip_version = 6,
            .af = AF_INET6,
            .expect = true,
        },
        {
            .src_ip = "1.2.3.4",
            .dst_ip = "1.2.3.4",
            .ip_version = 4,
            .af = AF_INET,
            .expect = false,
        }
    };

    memset(&key, 0, sizeof(key));
    len = sizeof(test_ips) / sizeof(test_ips[0]);
    for (i = 0; i < len; i++)
    {
        struct test_ip *t_ip;

        t_ip = &test_ips[i];

        /* set key */
        key.ip_version = t_ip->ip_version;

        s = inet_pton(t_ip->af, t_ip->src_ip, src_buf);
        TEST_ASSERT_GREATER_OR_EQUAL(1, s);
        key.src_ip = src_buf;

        s = inet_pton(t_ip->af, t_ip->dst_ip, dst_buf);
        TEST_ASSERT_GREATER_OR_EQUAL(1, s);
        key.dst_ip = dst_buf;

        rc = fsm_dpi_is_multicast_ip(&key);
        LOGN("[%s:%d] is_multicast_ip returned %d for IP %s", __func__, __LINE__, rc, t_ip->dst_ip);
        TEST_ASSERT_EQUAL(t_ip->expect, rc);
    }
}

static void
test_fsm_core_global_init(void)
{
    fsm_fn_tracer_init();
}

static void
test_fsm_core_global_exit(void)
{
    return;
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name, test_fsm_core_global_init, test_fsm_core_global_exit);
    ut_setUp_tearDown(ut_name, fsm_core_setUp, fsm_core_tearDown);
    // ut_keep_temp_folder(true); /* For reference */
    g_mgr = fsm_get_mgr();

    RUN_TEST(test_add_awlan_headers);
    RUN_TEST(test_add_session_after_awlan);
    RUN_TEST(test_plugin_types);
    RUN_TEST(test_duplicate_session);
    RUN_TEST(test_duplicate_session_fail);
    RUN_TEST(test_tx_intf_controller);
    RUN_TEST(test_tx_intf_kconfig);
    RUN_TEST(test_1_dpi_dispatcher);
    RUN_TEST(test_1_dpi_plugin);
    RUN_TEST(test_1_dpi_dispatcher_and_plugin);
    RUN_TEST(test_2_dpi_dispatcher_and_plugin);
    RUN_TEST(test_fsm_dpi_handler);
    RUN_TEST(test_3_dpi_dispatcher_and_plugin);
    RUN_TEST(test_4_dpi_dispatcher_and_plugin);
    RUN_TEST(test_5_dpi_dispatcher_and_plugin);
    RUN_TEST(test_6_service_plugin);
    RUN_TEST(test_7_dpi_dispatcher_and_plugin);
    RUN_TEST(test_8_dpi_dispatcher_udp_non_reserved);
    RUN_TEST(test_fsm_tap_type_validation);
    RUN_TEST(test_1_dpi_plugin_and_client_plugin);
    RUN_TEST(test_2_dpi_plugin_and_client_plugin);
    RUN_TEST(test_tags_added_to_monitor_list);
    RUN_TEST(test_adding_group_tags);
    RUN_TEST(test_add_new_tag_value);
    RUN_TEST(test_del_tag_value);
    RUN_TEST(test_tag_update_value);
    RUN_TEST(test_9_dpi_dispatcher_excluded_devices);
    RUN_TEST(test_10_dpi_dispatcher_included_excluded_devices);
    RUN_TEST(test_11_dpi_dispatcher_reserved_port_originator);
    RUN_TEST(test_12_dpi_dispatcher_icmp_req_reply);
    RUN_TEST(test_13_dpi_dispatcher_icmpv6_req_reply);
    RUN_TEST(test_dpi_dispatcher_udp_reserved);
    RUN_TEST(test_dpi_dispatcher_dns_request);
    RUN_TEST(test_dpi_dispatcher_dns_response);
    RUN_TEST(test_dpi_dispatcher_tcp_http_req_response);
    RUN_TEST(test_dpi_dispatcher_tcp_https_req_response);
    RUN_TEST(test_dpi_dispatcher_udp_https_req_response);
    RUN_TEST(test_dpi_dispatcher_udp_dhcp_discover);
    RUN_TEST(test_dpi_dispatcher_udp_dhcp_offer);
    RUN_TEST(test_dpi_dispatcher_no_ip_pkt);
    RUN_TEST(test_fsm_tap_type_from_str);
    RUN_TEST(test_dpi_dispatch_delete);
    RUN_TEST(test_parser_webcat_session);
    RUN_TEST(test_webcat_parser_session);
    RUN_TEST(test_dispatcher_device_process);
    RUN_TEST(test_notify_dispatcher_tap_type);
    RUN_TEST(test_1_notify_dispatcher_tap_type);
    RUN_TEST(test_notify_identical_plugin);
    RUN_TEST(test_check_ip_multicast);

    run_test_fsm_ovsdb();

    return ut_fini();
}
