#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "const.h"
#include "dns_parse.h"
#include "ds_tree.h"
#include "fsm.h"
#include "fsm_dns_utils.h"
#include "fsm_policy.h"
#include "json_util.h"
#include "log.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "os.h"
#include "policy_tags.h"
#include "qm_conn.h"
#include "unit_test_utils.h"
#include "unity.h"
#include "util.h"

#include "pcap.c"

const char *ut_name = "dns_parse_tests";

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
        .idx = 2,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .action_exists = true,
        .action = "update_tag",
        .log_exists = true,
        .log = "all",
        .other_config_len = 2,
        .other_config_keys = { "tagv4_name", "tagv6_name",},
        .other_config = { "${*upd_v4_tag}", "${*upd_v6_tag}"},
    },
    { /* entry 3*/
        .policy_exists = true,
        .policy = "test_policy",
        .name = "test_policy_allow",
        .idx = 3,
        .mac_op_exists = false,
        .fqdn_op_exists = false,
        .fqdncat_op_exists = false,
        .action_exists = true,
        .action = "gatekeeper",
        .log_exists = true,
        .log = "all",
    }
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


struct fsm_session *g_fsm_wet_cat_provider;
struct fsm_session *g_fsm_parser;

static bool
test_init_plugin(struct fsm_session *session)
{
    return true;
}

static bool
test_update_tap(struct fsm_session *session)
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

bool
dummy_gatekeeper_get_verdict(struct fsm_policy_req *req,
                             struct fsm_policy_reply *policy_reply)
{
    struct fqdn_pending_req *fqdn_req;
    struct fsm_url_request *req_info;
    struct fsm_url_reply *url_reply;

    fqdn_req = req->fqdn_req;
    req_info = fqdn_req->req_info;

    url_reply = CALLOC(1, sizeof(*url_reply));

    req_info->reply = url_reply;

    url_reply->service_id = URL_GK_SVC;

    policy_reply->categorized = FSM_FQDN_CAT_SUCCESS;
    req_info->reply->gk.gk_policy = strdup("gk_policy");
    req_info->reply->gk.confidence_level = 90;
    req_info->reply->gk.category_id = 2;

   return true;
}


union fsm_plugin_ops p_ops;

struct fsm_web_cat_ops g_plugin_ops =
{
    .categories_check = NULL,
    .risk_level_check = NULL,
    .cat2str = NULL,
    .get_stats = NULL,
    .dns_response = NULL,
    .gatekeeper_req = dummy_gatekeeper_get_verdict,
};

int g_ipv4_cnt;

struct dns_cache *g_dns_mgr;
struct fsm_mgr *g_fsm_mgr;


void
test_dns_forward(struct dns_session *dns_session, dns_info *dns_info,
                 uint8_t *buf, int len)
{
    LOGI("%s: here", __func__);
    TEST_ASSERT_EQUAL_INT(g_ipv4_cnt, dns_session->req->dns_response.ipv4_cnt);
}


void
test_dns_update_tag(struct fqdn_pending_req *req, struct fsm_policy_reply *policy_reply)
{
    LOGI("%s: here", __func__);
    TEST_ASSERT_EQUAL_STRING(policy_reply->updatev4_tag, "upd_v4_tag");
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

    net_parser = CALLOC(1, sizeof(*net_parser));
    UT_CREATE_PCAP_PAYLOAD(pkt46, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    FREE(net_parser);
}


void
test_type_PTR_query(void)
{
    struct net_header_parser *net_parser;
    struct dns_session *dns_session;
    size_t len;

    dns_session = dns_lookup_session(g_fsm_parser);
    TEST_ASSERT_NOT_NULL(dns_session);

    net_parser = CALLOC(1, sizeof(*net_parser));
    UT_CREATE_PCAP_PAYLOAD(pkt806, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    FREE(net_parser);
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

    net_parser = CALLOC(1, sizeof(*net_parser));

    /* Process query */
    UT_CREATE_PCAP_PAYLOAD(pkt46, net_parser);
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
    UT_CREATE_PCAP_PAYLOAD(pkt47, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    FREE(net_parser);
}

/**
 * @brief test type A dns query and response
 */
void
test_type_A_query_response_update_tag(void)
{
    struct net_header_parser *net_parser;
    struct schema_FSM_Policy *spolicy;
    struct dns_session *dns_session;
    size_t nelems;
    size_t len;
    size_t i;

    /* Delete all policies */
    nelems = (sizeof(g_spolicies) / sizeof(g_spolicies[0]));
    for (i = 0; i < nelems; i++)
    {
        spolicy = &g_spolicies[i];

        fsm_delete_policy(spolicy);
    }

    /* Add the update_tag policy */
    spolicy = &g_spolicies[2];
    fsm_add_policy(spolicy);
    dns_session = dns_lookup_session(g_fsm_parser);
    TEST_ASSERT_NOT_NULL(dns_session);

    net_parser = CALLOC(1, sizeof(*net_parser));

    /* Process query */
    UT_CREATE_PCAP_PAYLOAD(pkt46, net_parser);
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
    UT_CREATE_PCAP_PAYLOAD(pkt47, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    FREE(net_parser);
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

    net_parser = CALLOC(1, sizeof(*net_parser));

    /* Process query */
    UT_CREATE_PCAP_PAYLOAD(pkt46, net_parser);
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
    UT_CREATE_PCAP_PAYLOAD(pkt47, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    /*
     * Only one answer was seen, the dedup logic will keep a refcount
     * to the req, which eventually times out.
     */
    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    FREE(net_parser);
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

    net_parser = CALLOC(1, sizeof(*net_parser));

    /* Process query */
    UT_CREATE_PCAP_PAYLOAD(pkt46, net_parser);
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
    UT_CREATE_PCAP_PAYLOAD(pkt47, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    /* Duplicate query */
    dns_handler(g_fsm_parser, net_parser);

    FREE(net_parser);
}


/**
 * @brief test update v4 tag generation
 */
void
test_update_v4_tag_generation(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct schema_Openflow_Tag *regular_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s *dns_response;
    struct fqdn_pending_req req;
    char regular_tag_name[64];
    char local_tag_name[64];
    size_t max_capacity;
    const char *res;
    bool rc;
    int i;

    regular_tag = CALLOC(1, sizeof(*regular_tag));
    local_tag = CALLOC(1, sizeof(*local_tag));

    /* Prepare a request */
    memset(&req, 0, sizeof(req));
    memset(&policy_reply, 0, sizeof(policy_reply));
    policy_reply.action = FSM_UPDATE_TAG;
    req.dns_response.ipv4_cnt = 8;
    for (i = 0; i < req.dns_response.ipv4_cnt; i++)
    {
        char ipv4_addr[INET_ADDRSTRLEN];
        uint32_t addr;

        addr = htonl(i);
        res = inet_ntop(AF_INET, &addr, ipv4_addr, INET_ADDRSTRLEN);
        TEST_ASSERT_NOT_NULL(res);
        req.dns_response.ipv4_addrs[i] = strdup(ipv4_addr);
    }

    max_capacity = 2;
    memset(regular_tag_name, 0, sizeof(regular_tag_name));
    snprintf(regular_tag_name, sizeof(regular_tag_name), "${@%s}", g_tags[0].name);
    policy_reply.updatev4_tag = regular_tag_name;
    dns_response = &req.dns_response;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     regular_tag->device_value,
                                     &regular_tag->device_value_len,
                                     max_capacity, 4);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL(max_capacity, regular_tag->device_value_len);

    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev4_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 4);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL(max_capacity, local_tag->values_len);

    /* Free allocated resources */
    for (i = 0; i < req.dns_response.ipv4_cnt; i++)
    {
        FREE(req.dns_response.ipv4_addrs[i]);
    }
    FREE(regular_tag);
    FREE(local_tag);
}


/**
 * @brief test update v6 tag generation
 */
void
test_update_v6_tag_generation(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct schema_Openflow_Tag *regular_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s *dns_response;
    struct fqdn_pending_req req;
    char regular_tag_name[64];
    char local_tag_name[64];
    size_t max_capacity;
    const char *res;
    bool rc;
    int i;

    regular_tag = CALLOC(1, sizeof(*regular_tag));
    local_tag = CALLOC(1, sizeof(*local_tag));

    /* Prepare a request */
    memset(&req, 0, sizeof(req));
    memset(&policy_reply, 0, sizeof(policy_reply));
    policy_reply.action = FSM_UPDATE_TAG;
    req.dns_response.ipv6_cnt = 8;
    for (i = 0; i < req.dns_response.ipv6_cnt; i++)
    {
        char ipv6_addr[INET6_ADDRSTRLEN];
        uint32_t addr[4];

        addr[3] = i;
        res = inet_ntop(AF_INET6, &addr, ipv6_addr, INET6_ADDRSTRLEN);
        TEST_ASSERT_NOT_NULL(res);
        req.dns_response.ipv6_addrs[i] = strdup(ipv6_addr);
    }

    max_capacity = 2;
    memset(regular_tag_name, 0, sizeof(regular_tag_name));
    snprintf(regular_tag_name, sizeof(regular_tag_name), "${@%s}", g_tags[0].name);
    policy_reply.updatev6_tag = regular_tag_name;
    dns_response = &req.dns_response;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     regular_tag->device_value,
                                     &regular_tag->device_value_len,
                                     max_capacity, 6);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL(max_capacity, regular_tag->device_value_len);

    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[1].name);
    policy_reply.updatev6_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 6);
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_EQUAL(max_capacity, local_tag->values_len);

    /* Free allocated resources */
    for (i = 0; i < req.dns_response.ipv6_cnt; i++) FREE(req.dns_response.ipv6_addrs[i]);
    FREE(regular_tag);
    FREE(local_tag);
}


/**
 * @brief test update v4 tag generation with duplicates
 */
void
test_update_v4_tag_generation_with_duplicates(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct fqdn_pending_req req;
    struct dns_response_s *dns_response;
    struct fsm_policy_reply policy_reply;
    char local_tag_name[64];
    size_t max_capacity;
    const char *res;
    bool rc;
    int i;

    local_tag = CALLOC(1, sizeof(*local_tag));

    /* Prepare a request */
    memset(&req, 0, sizeof(req));
    memset(&policy_reply, 0, sizeof(policy_reply));
    policy_reply.action = FSM_UPDATE_TAG;
    req.dns_response.ipv4_cnt = 8;

    /* Duplicate the addresses */
    for (i = 0; i < req.dns_response.ipv4_cnt; i++)
    {
        char ipv4_addr[INET_ADDRSTRLEN];
        uint32_t addr;

        addr = htonl(0x12345);
        res = inet_ntop(AF_INET, &addr, ipv4_addr, INET_ADDRSTRLEN);
        TEST_ASSERT_NOT_NULL(res);
        req.dns_response.ipv4_addrs[i] = strdup(ipv4_addr);
    }

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev4_tag = local_tag_name;
    dns_response = &req.dns_response;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 4);
    TEST_ASSERT_TRUE(rc);

    /* Only one address should be present in the generated tag */
    TEST_ASSERT_EQUAL(1, local_tag->values_len);

    /* Free allocated resources */
    for (i = 0; i < req.dns_response.ipv4_cnt; i++)
    {
        FREE(req.dns_response.ipv4_addrs[i]);
    }
    FREE(local_tag);
}


/**
 * @brief test update v6 tag generation with duplicates
 */
void
test_update_v6_tag_generation_with_duplicates(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s *dns_response;
    struct fqdn_pending_req req;
    char local_tag_name[64];
    size_t max_capacity;
    const char *res;
    bool rc;
    int i;

    local_tag = CALLOC(1, sizeof(*local_tag));

    /* Prepare a request */
    memset(&req, 0, sizeof(req));
    memset(&policy_reply, 0, sizeof(policy_reply));
    policy_reply.action = FSM_UPDATE_TAG;
    req.dns_response.ipv6_cnt = 8;
    for (i = 0; i < req.dns_response.ipv6_cnt; i++)
    {
        char ipv6_addr[INET6_ADDRSTRLEN];
        uint32_t addr[4];

        addr[3] = 0x5678;
        res = inet_ntop(AF_INET6, &addr, ipv6_addr, INET6_ADDRSTRLEN);
        TEST_ASSERT_NOT_NULL(res);
        req.dns_response.ipv6_addrs[i] = strdup(ipv6_addr);
    }

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[1].name);
    policy_reply.updatev6_tag = local_tag_name;
    dns_response = &req.dns_response;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 6);
    TEST_ASSERT_TRUE(rc);

    /* Only one address should be present in the generated tag */
    TEST_ASSERT_EQUAL(1, local_tag->values_len);

    /* Free allocated resources */
    for (i = 0; i < req.dns_response.ipv6_cnt; i++) FREE(req.dns_response.ipv6_addrs[i]);
    FREE(local_tag);
}


/**
 * @brief test update v4 tag generation for ip expiration
 */
void
test_update_v4_tag_generation_ip_expiration(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s *dns_response;
    struct fqdn_pending_req req;
    char local_tag_name[64];
    size_t max_capacity;
    const char *res;
    bool rc;
    int i;


    /* First case where number of ip address are less than max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    /* Prepare a request */
    memset(&req, 0, sizeof(req));
    memset(&policy_reply, 0, sizeof(policy_reply));
    policy_reply.action = FSM_UPDATE_TAG;
    req.dns_response.ipv4_cnt = 8;

    /* Duplicate the addresses */
    for (i = 0; i < req.dns_response.ipv4_cnt; i++)
    {
        char ipv4_addr[INET_ADDRSTRLEN];
        uint32_t addr;

        addr = htonl(i);
        res = inet_ntop(AF_INET, &addr, ipv4_addr, INET_ADDRSTRLEN);
        TEST_ASSERT_NOT_NULL(res);
        req.dns_response.ipv4_addrs[i] = strdup(ipv4_addr);
    }

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev4_tag = local_tag_name;
    dns_response = &req.dns_response;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 4);
    TEST_ASSERT_TRUE(rc);

    /* Expected length index is 8 in the generated tag */
    TEST_ASSERT_EQUAL(8, local_tag->values_len);

    /* Free allocated resources */
    for (i = 0; i < req.dns_response.ipv4_cnt; i++)
    {
        FREE(req.dns_response.ipv4_addrs[i]);
    }

    FREE(local_tag);

    /* Second case where number of ip address are equal to max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    /* Prepare a request */
    memset(&req, 0, sizeof(req));
    policy_reply.action = FSM_UPDATE_TAG;
    req.dns_response.ipv4_cnt = 16;

    /* Duplicate the addresses */
    for (i = 0; i < req.dns_response.ipv4_cnt; i++)
    {
        char ipv4_addr[INET_ADDRSTRLEN];
        uint32_t addr;

        addr = htonl(i);
        res = inet_ntop(AF_INET, &addr, ipv4_addr, INET_ADDRSTRLEN);
        TEST_ASSERT_NOT_NULL(res);
        req.dns_response.ipv4_addrs[i] = strdup(ipv4_addr);
    }

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev4_tag = local_tag_name;
    dns_response = &req.dns_response;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 4);
    TEST_ASSERT_TRUE(rc);

    /* Expected length index is 16 in the generated tag */
    TEST_ASSERT_EQUAL(16, local_tag->values_len);

    /* Free allocated resources */
    for (i = 0; i < req.dns_response.ipv4_cnt; i++)
    {
        FREE(req.dns_response.ipv4_addrs[i]);
    }

    FREE(local_tag);

    /* Third case where number of ip address are greater than max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    /* Prepare a request */
    memset(&req, 0, sizeof(req));
    policy_reply.action = FSM_UPDATE_TAG;
    req.dns_response.ipv4_cnt = 20;

    /* Duplicate the addresses */
    for (i = 0; i < req.dns_response.ipv4_cnt; i++)
    {
        char ipv4_addr[INET_ADDRSTRLEN];
        uint32_t addr;

        addr = htonl(i);
        res = inet_ntop(AF_INET, &addr, ipv4_addr, INET_ADDRSTRLEN);
        TEST_ASSERT_NOT_NULL(res);
        req.dns_response.ipv4_addrs[i] = strdup(ipv4_addr);
    }

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[0].name);
    policy_reply.updatev4_tag = local_tag_name;
    dns_response = &req.dns_response;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 4);
    TEST_ASSERT_TRUE(rc);

    /* Expected length index is 4 in the generated tag */
    TEST_ASSERT_EQUAL(4, local_tag->values_len);

    /* Free allocated resources */
    for (i = 0; i < req.dns_response.ipv4_cnt; i++)
    {
        FREE(req.dns_response.ipv4_addrs[i]);
    }

    FREE(local_tag);

}


/**
 * @brief test update v6 tag generation ip expiration
 */
void
test_update_v6_tag_generation_ip_expiration(void)
{
    struct schema_Openflow_Local_Tag *local_tag;
    struct fsm_policy_reply policy_reply;
    struct dns_response_s *dns_response;
    struct fqdn_pending_req req;
    char local_tag_name[64];
    size_t max_capacity;
    const char *res;
    bool rc;
    int i;

    /* First case where number of ip address are less than max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    /* Prepare a request */
    memset(&req, 0, sizeof(req));
    memset(&policy_reply, 0, sizeof(policy_reply));
    policy_reply.action = FSM_UPDATE_TAG;
    req.dns_response.ipv6_cnt = 8;
    for (i = 0; i < req.dns_response.ipv6_cnt; i++)
    {
        char ipv6_addr[INET6_ADDRSTRLEN];
        uint32_t addr[4];

        addr[3] = i;
        res = inet_ntop(AF_INET6, &addr, ipv6_addr, INET6_ADDRSTRLEN);
        TEST_ASSERT_NOT_NULL(res);
        req.dns_response.ipv6_addrs[i] = strdup(ipv6_addr);
    }

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[1].name);
    policy_reply.updatev6_tag = local_tag_name;
    dns_response = &req.dns_response;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 6);
    TEST_ASSERT_TRUE(rc);

    /* Expected length index is 8 in the generated tag */
    TEST_ASSERT_EQUAL(8, local_tag->values_len);

    /* Free allocated resources */
    for (i = 0; i < req.dns_response.ipv6_cnt; i++) FREE(req.dns_response.ipv6_addrs[i]);
    FREE(local_tag);

    /* Second case where number of ip address are equal to max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    /* Prepare a request */
    memset(&req, 0, sizeof(req));
    policy_reply.action = FSM_UPDATE_TAG;
    req.dns_response.ipv6_cnt = 16;
    for (i = 0; i < req.dns_response.ipv6_cnt; i++)
    {
        char ipv6_addr[INET6_ADDRSTRLEN];
        uint32_t addr[4];

        addr[3] = i;
        res = inet_ntop(AF_INET6, &addr, ipv6_addr, INET6_ADDRSTRLEN);
        TEST_ASSERT_NOT_NULL(res);
        req.dns_response.ipv6_addrs[i] = strdup(ipv6_addr);
    }

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[1].name);
    policy_reply.updatev6_tag = local_tag_name;
    dns_response = &req.dns_response;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 6);

    TEST_ASSERT_TRUE(rc);

    /* Expected length index is 16 in the generated tag */
    TEST_ASSERT_EQUAL(16, local_tag->values_len);

    /* Free allocated resources */
    for (i = 0; i < req.dns_response.ipv6_cnt; i++) FREE(req.dns_response.ipv6_addrs[i]);
    FREE(local_tag);

    /* Third case where number of ip address are greater than max capacity */
    local_tag = CALLOC(1, sizeof(*local_tag));

    /* Prepare a request */
    memset(&req, 0, sizeof(req));
    policy_reply.action = FSM_UPDATE_TAG;
    req.dns_response.ipv6_cnt = 20;
    for (i = 0; i < req.dns_response.ipv6_cnt; i++)
    {
        char ipv6_addr[INET6_ADDRSTRLEN];
        uint32_t addr[4];

        addr[3] = i;
        res = inet_ntop(AF_INET6, &addr, ipv6_addr, INET6_ADDRSTRLEN);
        TEST_ASSERT_NOT_NULL(res);
        req.dns_response.ipv6_addrs[i] = strdup(ipv6_addr);
    }

    max_capacity = 16;
    memset(local_tag_name, 0, sizeof(local_tag_name));
    snprintf(local_tag_name, sizeof(local_tag_name), "${*%s}", g_ltags[1].name);
    policy_reply.updatev6_tag = local_tag_name;
    rc = fsm_dns_generate_update_tag(dns_response, &policy_reply,
                                     local_tag->values,
                                     &local_tag->values_len,
                                     max_capacity, 6);

    TEST_ASSERT_TRUE(rc);

    /* Expected length index is 4 in the generated tag */
    TEST_ASSERT_EQUAL(4, local_tag->values_len);

    /* Free allocated resources */
    for (i = 0; i < req.dns_response.ipv6_cnt; i++) FREE(req.dns_response.ipv6_addrs[i]);

    FREE(local_tag);
}

/**
 * @brief test gk cache entry
 */
void
test_gk_dns_cache(void)
{
    struct net_header_parser *net_parser;
    struct schema_FSM_Policy *spolicy;
    struct dns_session *dns_session;
    size_t nelems;
    size_t len;
    size_t i;

    /* Delete all policies */
    nelems = (sizeof(g_spolicies) / sizeof(g_spolicies[0]));
    for (i = 0; i < nelems; i++)
    {
        spolicy = &g_spolicies[i];

        fsm_delete_policy(spolicy);
    }

    /* Add the update_tag policy */
    spolicy = &g_spolicies[3];
    fsm_add_policy(spolicy);
    dns_session = dns_lookup_session(g_fsm_parser);
    TEST_ASSERT_NOT_NULL(dns_session);

    net_parser = CALLOC(1, sizeof(*net_parser));

    /* Process query */
    UT_CREATE_PCAP_PAYLOAD(pkt46, net_parser);
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
    UT_CREATE_PCAP_PAYLOAD(pkt47, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    FREE(net_parser);
}


/**
 * @brief Basic test processing a reverse look up (type 12)
 */
void
test_reverse_lookup(void)
{
    struct net_header_parser *net_parser;
    struct schema_FSM_Policy *spolicy;
    struct dns_session *dns_session;
    size_t nelems;
    size_t len;
    size_t i;

    /* Delete all policies */
    nelems = (sizeof(g_spolicies) / sizeof(g_spolicies[0]));
    for (i = 0; i < nelems; i++)
    {
        spolicy = &g_spolicies[i];

        fsm_delete_policy(spolicy);
    }

    /* Add the update_tag policy */
    spolicy = &g_spolicies[3];
    fsm_add_policy(spolicy);
    dns_session = dns_lookup_session(g_fsm_parser);
    TEST_ASSERT_NOT_NULL(dns_session);

    net_parser = CALLOC(1, sizeof(*net_parser));

    /* Process query */
    UT_CREATE_PCAP_PAYLOAD(pkt1, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    /*
     * The captured dns answer has 8 resolved IP addresses.
     * Set validation expectations
     */
    g_ipv4_cnt = 0;

    /* Process response */
    memset(net_parser, 0, sizeof(*net_parser));
    UT_CREATE_PCAP_PAYLOAD(pkt2, net_parser);
    len = net_header_parse(net_parser);
    TEST_ASSERT_TRUE(len != 0);
    dns_handler(g_fsm_parser, net_parser);

    g_dns_mgr->req_cache_ttl = 0;
    dns_retire_reqs(g_fsm_parser);

    FREE(net_parser);
}

void
dns_parse_setUp(void)
{
    struct schema_Flow_Service_Manager_Config *conf;
    struct schema_FSM_Policy *spolicy;
    struct fsm_policy_session *mgr;
    ds_tree_t *sessions;
    size_t nelems;
    bool ret;
    size_t i;

    fsm_init_mgr(NULL);
    g_fsm_mgr->init_plugin = test_init_plugin;
    g_fsm_mgr->update_session_tap = test_update_tap;
    g_fsm_mgr->get_br = test_get_br;

    mgr = fsm_policy_get_mgr();
    if (!mgr->initialized) fsm_init_manager();

    nelems = ARRAY_SIZE(g_spolicies);
    for (i = 0; i < nelems; i++)
    {
        spolicy = &g_spolicies[i];

        fsm_add_policy(spolicy);
    }

    nelems = ARRAY_SIZE(g_confs);
    for (i = 0; i < nelems; i++)
    {
        struct schema_Flow_Service_Manager_Config *conf;

        conf = &g_confs[i];
        MEMZERO(conf->if_name);
    }

    nelems = ARRAY_SIZE(g_tags);
    for (i = 0; i < nelems; i++)
    {
        ret = om_tag_add_from_schema(&g_tags[i]);
        TEST_ASSERT_TRUE(ret);
    }

    nelems = ARRAY_SIZE(g_ltags);
    for (i = 0; i < nelems; i++)
    {
        ret = om_local_tag_add_from_schema(&g_ltags[i]);
        TEST_ASSERT_TRUE(ret);
    }

    fsm_get_awlan_headers(&g_awlan_nodes[0]);

    conf = &g_confs[0];
    fsm_add_session(conf);
    sessions = fsm_get_sessions();
    g_fsm_parser = ds_tree_find(sessions, conf->handler);
    g_fsm_parser->provider_ops = &g_plugin_ops;
    g_fsm_parser->ops.send_report = test_send_report;
    TEST_ASSERT_NOT_NULL(g_fsm_parser);

    dns_mgr_init();
    g_dns_mgr = dns_get_mgr();
    g_dns_mgr->set_forward_context = test_set_fwd_context;
    g_dns_mgr->forward = test_dns_forward;
    g_dns_mgr->update_tag = fsm_dns_update_tag;
    g_dns_mgr->policy_init = test_dns_policy_init;

    dns_plugin_init(g_fsm_parser);

    g_ipv4_cnt = 0;

    ut_prepare_pcap(Unity.CurrentTestName);
}

void
dns_parse_tearDown(void)
{
    struct fsm_policy_session *policy_mgr = fsm_policy_get_mgr();
    struct policy_table *table, *t_to_remove;
    struct fsm_policy *fpolicy, *p_to_remove;
    ds_tree_t *tables_tree, *policies_tree;
    size_t nelems;
    size_t i;
    bool ret;

    dns_plugin_exit(g_fsm_parser);

    nelems = ARRAY_SIZE(g_tags);
    for (i = 0; i < nelems; i++)
    {
        ret = om_tag_remove_from_schema(&g_tags[i]);
        TEST_ASSERT_TRUE(ret);
    }

    nelems = ARRAY_SIZE(g_ltags);
    for (i = 0; i < nelems; i++)
    {
        ret = om_local_tag_remove_from_schema(&g_ltags[i]);
        TEST_ASSERT_TRUE(ret);
    }

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

    g_dns_mgr = NULL;

    g_fsm_mgr->init_plugin = NULL;

    ut_cleanup_pcap();
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name);
    ut_setUp_tearDown(ut_name, dns_parse_setUp, dns_parse_tearDown);

    g_fsm_mgr = fsm_get_mgr();

    RUN_TEST(test_load_unload_plugin);
    RUN_TEST(test_type_A_query);
    RUN_TEST(test_type_PTR_query);
    RUN_TEST(test_type_A_query_response);
    RUN_TEST(test_type_A_query_response_update_tag);
    RUN_TEST(test_update_v4_tag_generation);
    RUN_TEST(test_update_v6_tag_generation);
    RUN_TEST(test_update_v4_tag_generation_with_duplicates);
    RUN_TEST(test_update_v6_tag_generation_with_duplicates);
    RUN_TEST(test_type_A_duplicate_query_response);
    RUN_TEST(test_type_A_duplicate_query_duplicate_response);
    RUN_TEST(test_update_v4_tag_generation_ip_expiration);
    RUN_TEST(test_update_v6_tag_generation_ip_expiration);
    RUN_TEST(test_gk_dns_cache);
    RUN_TEST(test_reverse_lookup);

    ut_fini();

    return UNITY_END();
}
