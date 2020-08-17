#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "dns_parse.h"
#include "fsm_policy.h"
#include "json_util.h"
#include "log.h"
#include "qm_conn.h"
#include "target.h"
#include "unity.h"

#include "pcap.c"
/**
 * @brief a set of sessions as delivered by the ovsdb API
 */
struct schema_Flow_Service_Manager_Config g_confs[] =
{
    /* parser plugin, type provided */
    {
        .handler = "fsm_session_test_1",
        .plugin = "plugin_1",
        .pkt_capt_filter = "bpf_filter_1",
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
            "dev-test/fsm_core_ut/topic_1", /* topic */
            "test_dso_init",                /* plugin init routine */
            "test_provider",                /* service provider */
            "test_policy",                  /* FSM policy entry */
        },
        .other_config_len = 4,
    },

    /* web categorization plugin */
    {
        .handler = "test_provider",
        .plugin = "plugin_2",
        .type = "web_cat_provider",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
        },
        .other_config =
        {
            "dev-test/fsm_core_ut/topic_2", /* topic */
            "test_dso_init",                /* plugin init routine */
        },
        .other_config_len = 2,
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
    { /* entry 0. Always matching, no action */
        .policy_exists = true,
        .policy = "test_policy",
        .name = "test_policy_observe",
        .idx = 0,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .action_exists = false, /* pass through */
        .log_exists = true,
        .log = "all",
    },
    { /* entry 1. Always matching, action is set. */
        .policy_exists = true,
        .policy = "test_policy",
        .name = "test_policy_allow",
        .idx = 1,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .action_exists = true,
        .action = "allow",
        .log_exists = true,
        .log = "all",
    },
    { /* entry 2. Always matching, action is update tag. */
        .policy_exists = true,
        .policy = "test_policy",
        .name = "test_policy_update",
        .idx = 0,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .action_exists = true,
        .action = "update_tag",
        .log_exists = true,
        .log = "all",
        .other_config_len = 2,
        .other_config_keys = { "tagv4_name", "tagv6_name",},
        .other_config = { "upd_v4_tag", "upd_v6_tag"},
    }
};


struct fsm_session *g_fsm_wet_cat_provider;
struct fsm_session *g_fsm_parser;

static bool
test_init_plugin(struct fsm_session *session)
{
    return true;
}


static bool
test_flood_mod(struct fsm_session *session)
{
    return true;
}

static const char g_test_br[16] = "test_br";

static int
test_get_br(char *if_name, char *bridge, size_t len)
{
    strscpy(bridge, g_test_br, len);
    return 0;
}


static void
test_send_report(struct fsm_session *session, char *report)
{
#ifndef ARCH_X86
    qm_response_t res;
    bool ret = false;
#endif

    LOGT("%s: msg len: %zu, msg: %s\n, topic: %s",
         __func__, report ? strlen(report) : 0,
         report ? report : "None", session->topic);
    if (report == NULL) return;

#ifndef ARCH_X86
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_DISABLE, session->topic,
                                       report, strlen(report), &res);
    if (!ret) LOGE("error sending mqtt with topic %s", session->topic);
#endif
    json_free(report);

    return;
}


union fsm_plugin_ops g_plugin_ops =
{
    .web_cat_ops =
    {
        .categories_check = NULL,
        .risk_level_check = NULL,
        .cat2str = NULL,
        .get_stats = NULL,
        .dns_response = NULL,
    },
};


int g_ipv4_cnt;

struct dns_cache *g_dns_mgr;
struct fsm_mgr *g_fsm_mgr;

const char *test_name = "dns_tests";

/**
 * @brief Converts a bytes array in a hex dump file wireshark can import.
 *
 * Dumps the array in a file that can then be imported by wireshark.
 * The file can also be translated to a pcap file using the text2pcap command.
 * Useful to visualize the packet content.
 */
void create_hex_dump(const char *fname, const uint8_t *buf, size_t len)
{
    int line_number = 0;
    bool new_line = true;
    size_t i;
    FILE *f;

    f = fopen(fname, "w+");

    if (f == NULL) return;

    for (i = 0; i < len; i++)
    {
	 new_line = (i == 0 ? true : ((i % 8) == 0));
	 if (new_line)
	 {
	      if (line_number) fprintf(f, "\n");
	      fprintf(f, "%06x", line_number);
	      line_number += 8;
	 }
         fprintf(f, " %02x", buf[i]);
    }
    fprintf(f, "\n");
    fclose(f);

    return;
}

/**
 * @brief Convenient wrapper
 *
 * Dumps the packet content in /tmp/<tests_name>_<pkt name>.txtpcap
 * for wireshark consumption and sets g_parser data fields.
 * @params pkt the C structure containing an exported packet capture
 */
#define PREPARE_UT(pkt, parser)                                 \
    {                                                           \
        char fname[128];                                        \
        size_t len = sizeof(pkt);                               \
                                                                \
        snprintf(fname, sizeof(fname), "/tmp/%s_%s.txtpcap",    \
                 test_name, #pkt);                              \
        create_hex_dump(fname, pkt, len);                       \
        parser->packet_len = len;                               \
        parser->caplen = len;                                   \
        parser->data = (uint8_t *)pkt;                          \
    }



void
test_dns_forward(struct dns_session *dns_session, dns_info *dns_info,
                 uint8_t *buf, int len)
{
    LOGI("%s: here", __func__);
    TEST_ASSERT_EQUAL_INT(g_ipv4_cnt, dns_session->req->ipv4_cnt);
}


void
test_dns_update_tag(struct fqdn_pending_req *req)
{
    LOGI("%s: here", __func__);
    TEST_ASSERT_EQUAL_STRING(req->updatev4_tag, "upd_v4_tag");
}


void
test_dns_policy_init(void)
{
    fsm_init_manager();
}

int
test_set_fwd_context(struct fsm_session *session)
{
    LOGI("%s: here", __func__);
    return 0;
}


void setUp(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy_session *mgr;
    ds_tree_t *sessions;
    size_t nelems;
    size_t i;

    fsm_init_mgr(NULL);
    g_fsm_mgr->init_plugin = test_init_plugin;
    g_fsm_mgr->flood_mod = test_flood_mod;
    g_fsm_mgr->get_br = test_get_br;

    mgr = fsm_policy_get_mgr();
    if (!mgr->initialized) fsm_init_manager();

    nelems = (sizeof(g_spolicies) / sizeof(g_spolicies[0]));
    for (i = 0; i < nelems; i++)
    {
        spolicy = &g_spolicies[i];

        fsm_add_policy(spolicy);
    }

    nelems = (sizeof(g_confs) / sizeof(g_confs[0]));
    for (i = 0; i < nelems; i++)
    {
        struct schema_Flow_Service_Manager_Config *conf;

        conf = &g_confs[i];
        memset(conf->if_name, 0, sizeof(conf->if_name));
    }
    fsm_get_awlan_headers(&g_awlan_nodes[0]);

    conf = &g_confs[0];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    g_fsm_parser = ds_tree_find(sessions, conf->handler);
    g_fsm_parser->ops.send_report = test_send_report;
    TEST_ASSERT_NOT_NULL(g_fsm_parser);

    dns_mgr_init();
    g_dns_mgr = dns_get_mgr();
    g_dns_mgr->set_forward_context = test_set_fwd_context;
    g_dns_mgr->forward = test_dns_forward;
    g_dns_mgr->update_tag = test_dns_update_tag;
    g_dns_mgr->policy_init = test_dns_policy_init;

    dns_plugin_init(g_fsm_parser);

    g_ipv4_cnt = 0;

    return;
}


void tearDown(void)
{
    struct fsm_policy_session *policy_mgr = fsm_policy_get_mgr();
    struct schema_Flow_Service_Manager_Config *conf;
    struct policy_table *table, *t_to_remove;
    struct fsm_policy *fpolicy, *p_to_remove;
    ds_tree_t *tables_tree, *policies_tree;

    dns_plugin_exit(g_fsm_parser);

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
        free(t_to_remove);
    }

    conf = &g_confs[0];
    fsm_delete_session(conf);
    g_dns_mgr = NULL;

    fsm_reset_mgr();
    g_fsm_mgr->init_plugin = NULL;
    return;
}


/**
 * @brief test plugin init()/exit() sequence
 *
 * Validate plugin reference counts and pointers
 */
void
test_load_unload_plugin(void)
{
    /* SetUp() has called init(). Validate settings */
    TEST_ASSERT_NOT_NULL(g_dns_mgr);
}


/**
 * @brief test type A dns query
 */
void
test_type_A_query(void)
{
    struct net_header_parser *net_parser;
    struct dns_session *dns_session;
    size_t len;

    dns_session = dns_lookup_session(g_fsm_parser);
    TEST_ASSERT_NOT_NULL(dns_session);

    net_parser = calloc(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    PREPARE_UT(pkt46, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    free(net_parser);
}


void
test_type_PTR_query(void)
{
    struct net_header_parser *net_parser;
    struct dns_session *dns_session;
    size_t len;

    dns_session = dns_lookup_session(g_fsm_parser);
    TEST_ASSERT_NOT_NULL(dns_session);

    net_parser = calloc(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);
    PREPARE_UT(pkt806, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    free(net_parser);
}

/**
 * @brief test type A dns query and response
 */
void
test_type_A_query_response(void)
{
    struct net_header_parser *net_parser;
    struct dns_session *dns_session;
    size_t len;

    dns_session = dns_lookup_session(g_fsm_parser);
    TEST_ASSERT_NOT_NULL(dns_session);

    net_parser = calloc(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);

    /* Process query */
    PREPARE_UT(pkt46, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    /*
     * The captured dns answer has 8 resolved IP addresses.
     * Set validation expectations
     */
    g_ipv4_cnt = 8;

    /* Process response */
    memset(net_parser, 0, sizeof(*net_parser));
    PREPARE_UT(pkt47, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    free(net_parser);
}

/**
 * @brief test type A dns query and response
 */
void
test_type_A_query_response_update_tag(void)
{
    struct net_header_parser *net_parser;
    struct dns_session *dns_session;
    size_t len;

    dns_session = dns_lookup_session(g_fsm_parser);
    TEST_ASSERT_NOT_NULL(dns_session);

    net_parser = calloc(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);

    /* Process query */
    PREPARE_UT(pkt46, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    /*
     * The captured dns answer has 8 resolved IP addresses.
     * Set validation expectations
     */
    g_ipv4_cnt = 8;

    /* Process response */
    memset(net_parser, 0, sizeof(*net_parser));
    PREPARE_UT(pkt47, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    free(net_parser);
}

/**
 * @brief test type A duplicate dns query single response
 */
void
test_type_A_duplicate_query_response(void)
{
    struct net_header_parser *net_parser;
    struct dns_session *dns_session;
    size_t len;

    dns_session = dns_lookup_session(g_fsm_parser);
    TEST_ASSERT_NOT_NULL(dns_session);

    net_parser = calloc(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);

    /* Process query */
    PREPARE_UT(pkt46, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    /* Duplicate query */
    dns_handler(g_fsm_parser, net_parser);

    /*
     * The captured dns answer has 8 resolved IP addresses.
     * Set validation expectations
     */
    g_ipv4_cnt = 8;

    /* Process response */
    memset(net_parser, 0, sizeof(*net_parser));
    PREPARE_UT(pkt47, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    /*
     * Only one answer was seen, the dedup logic will keep a refcount
     * to the req, which eventually times out.
     */
    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    free(net_parser);
}


/**
 * @brief test type A duplicate dns query single response
 */
void
test_type_A_duplicate_query_duplicate_response(void)
{
    struct net_header_parser *net_parser;
    struct dns_session *dns_session;
    size_t len;

    dns_session = dns_lookup_session(g_fsm_parser);
    TEST_ASSERT_NOT_NULL(dns_session);

    net_parser = calloc(1, sizeof(*net_parser));
    TEST_ASSERT_NOT_NULL(net_parser);

    /* Process query */
    PREPARE_UT(pkt46, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    /* Duplicate query */
    dns_handler(g_fsm_parser, net_parser);

    /*
     * The captured dns answer has 8 resolved IP addresses.
     * Set validation expectations
     */
    g_ipv4_cnt = 8;

    /* Process response */
    memset(net_parser, 0, sizeof(*net_parser));
    PREPARE_UT(pkt47, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    /* Duplicate query */
    dns_handler(g_fsm_parser, net_parser);

    free(net_parser);
}


int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    g_fsm_mgr = fsm_get_mgr();

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    RUN_TEST(test_load_unload_plugin);
    RUN_TEST(test_type_A_query);
    RUN_TEST(test_type_PTR_query);
    RUN_TEST(test_type_A_query_response);
    RUN_TEST(test_type_A_query_response_update_tag);
    RUN_TEST(test_type_A_duplicate_query_response);
    RUN_TEST(test_type_A_duplicate_query_duplicate_response);

    return UNITY_END();
}
