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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "1035.h"
#include "const.h"
#include "ds_tree.h"
#include "fsm.h"
#include "log.h"
#include "mdns_plugin.h"
#include "mdns_records.h"
#include "mdnsd.h"
#include "memutil.h"
#include "net_header_parse.h"
#include "os.h"
#include "ovsdb_update.h"
#include "ovsdb_utils.h"
#include "sockaddr_storage.h"
#include "test_mdns.h"
#include "unit_test_utils.h"
#include "unity.h"

#include "pcap.c"
#include "pcap_map.c"

const char *ut_name = "mdns_plugin_tests";

#define OTHER_CONFIG_NELEMS 4
#define OTHER_CONFIG_NELEM_SIZE 128

ovsdb_update_monitor_t g_mon;
struct mdns_plugin_mgr *mgr;

struct schema_Service_Announcement g_mdns_ann[] =
{
    /* entry 0 */
    {
        .name = "bw.plume",
        .protocol_present = true,
        .protocol = "_http._tcp",
        .port = 22,
        .txt_keys =
        {
            "v1",
        },
        .txt =
        {
            "https://bw.plume.com/test/api/v1",
        },
        .txt_len = 1,
    },
    /* entry 1 */
    {
        .name = "bd.plume",
        .protocol_present = true,
        .protocol = "_smb._udp",
        .port = 139,
        .txt_keys =
        {
            "v2",
        },
        .txt =
        {
            "https://bd.plume.com/test/api/v2",
        },
        .txt_len = 1,
    }

};

char g_other_configs[][2][OTHER_CONFIG_NELEMS][OTHER_CONFIG_NELEM_SIZE] =
{
    {
        {
            "mdns_src_ip",
            "tx_intf",
            "records_report_interval",
            "report_records",
        },
        {
            "192.168.1.90",
            "foo",//CONFIG_TARGET_LAN_BRIDGE_NAME".tx",
            "60",
            "true",
        },
    },
};

struct fsm_session_conf g_confs[] =
{
    /* entry 0 */
    {
        .handler = "mdns_plugin_session_0",
        .if_name = "foo",
    }
};

struct fsm_session g_sessions[] =
{
    {
        .type = FSM_PARSER,
        .conf = &g_confs[0],
        .node_id = "1S6D808DB4",
        .location_id = "5e3a194bb03594384016458",
    }
};

void ut_ovsdb_init(void)
{
    return;
}

void ut_ovsdb_exit(void)
{
    return;
}


char *
util_get_other_config_val(struct fsm_session *session, char *key)
{
    struct fsm_session_conf *fconf;
    struct str_pair *pair;
    ds_tree_t *tree;

    if (session == NULL) return NULL;

    fconf = session->conf;
    if (fconf == NULL) return NULL;

    tree = fconf->other_config;
    if (tree == NULL) return NULL;

    pair = ds_tree_find(tree, key);
    if (pair == NULL) return NULL;

    return pair->value;
}


struct fsm_session_ops g_ops =
{
    .get_config = util_get_other_config_val,
};

time_t timer_start;
time_t timer_stop;
struct ev_loop  *g_loop;


void
mdns_test_handler(struct fsm_session *session, struct net_header_parser *net_parser)
{
    LOGI("%s: here", __func__);
    net_header_logi(net_parser);
}


union fsm_plugin_ops g_plugin_ops =
{
    .parser_ops =
    {
        .get_service = NULL,
        .handler = mdns_test_handler,
    },
};


void
mdns_plugin_setUp(void)
{
    g_loop = EV_DEFAULT;
    struct fsm_session *session = &g_sessions[0];

    session->conf = &g_confs[0];
    session->ops  = g_ops;
    session->name = g_confs[0].handler;
    session->conf->other_config = schema2tree(OTHER_CONFIG_NELEM_SIZE,
                                              OTHER_CONFIG_NELEM_SIZE,
                                              OTHER_CONFIG_NELEMS,
                                              g_other_configs[0][0],
                                              g_other_configs[0][1]);
    session->loop =  g_loop;
    session->p_ops = &g_plugin_ops;

    /* Setup test_mgr for mdns records reporting */
    setup_mdns_records_report();
}

void
mdns_plugin_tearDown(void)
{
    struct fsm_session *session = &g_sessions[0];

    free_str_tree(session->conf->other_config);

    /* Free the mdns records reporting test mgr */
    teardown_mdns_records_report();
}


/**
 * @brief test plugin init()/exit() sequence
 *
 * Validate plugin reference counts and pointers
 */
void test_load_unload_plugin(void)
{
    struct fsm_session *session = &g_sessions[0];

    mdns_mgr_init();
    mgr = mdns_get_mgr();
    mgr->ovsdb_init = ut_ovsdb_init;
    mgr->ovsdb_exit = ut_ovsdb_exit;

    mdns_plugin_init(session);

    TEST_ASSERT_NOT_NULL(mgr->ctxt);

    mdns_plugin_exit(session);
}


void test_add_mdnsd_service(void)
{
    struct fsm_session *session = &g_sessions[0];
    struct schema_Service_Announcement *pconf;
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context   *pctxt;
    char   hlocal[256] = {0};

    mdns_record_t *r = NULL;
    const mdns_answer_t *an = NULL;

    mdns_mgr_init();

    mgr = mdns_get_mgr();
    mgr->ovsdb_init = ut_ovsdb_init;
    mgr->ovsdb_exit = ut_ovsdb_exit;

    mdns_plugin_init(session);
    pctxt = mgr->ctxt;

    TEST_ASSERT_NOT_NULL(pctxt);


    /* Add the service entry 0 */
    pconf = &g_mdns_ann[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    mgr->service_announcement(&g_mon, NULL, pconf);

    snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", pconf->name, pconf->protocol);
    r = mdnsd_find(pctxt->dmn, hlocal, QTYPE_TXT);
    TEST_ASSERT_NOT_NULL(r);

    an = mdnsd_record_data(r);
    TEST_ASSERT_NOT_NULL(an);

    TEST_ASSERT_EQUAL_STRING(hlocal, an->name);

    memset(hlocal, 0, sizeof(hlocal));

    /* Add the service entry 1 */
    pconf = &g_mdns_ann[1];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    mgr->service_announcement(&g_mon, NULL, pconf);

    snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", pconf->name, pconf->protocol);
    r = mdnsd_find(pctxt->dmn, hlocal, QTYPE_TXT);
    TEST_ASSERT_NOT_NULL(r);

    an = mdnsd_record_data(r);
    TEST_ASSERT_NOT_NULL(an);

    TEST_ASSERT_EQUAL_STRING(hlocal, an->name);

#if !defined(__x86_64__)
    ev_run(mgr->loop, 0);
#endif

    mdns_plugin_exit(session);
}

void test_del_mdnsd_service(void)
{
    struct fsm_session *session = &g_sessions[0];
    struct schema_Service_Announcement *pconf;
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context   *pctxt;
    char   hlocal[256] = {0};

    mdns_record_t *r = NULL;
    const mdns_answer_t *an = NULL;

    mdns_mgr_init();

    mgr = mdns_get_mgr();
    mgr->ovsdb_init = ut_ovsdb_init;
    mgr->ovsdb_exit = ut_ovsdb_exit;

    mdns_plugin_init(session);
    pctxt = mgr->ctxt;

    TEST_ASSERT_NOT_NULL(pctxt);

    /* Add the service entry 0 */
    pconf = &g_mdns_ann[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    mgr->service_announcement(&g_mon, NULL, pconf);

    // Check if entry is present.
    snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", pconf->name, pconf->protocol);
    r = mdnsd_find(pctxt->dmn, hlocal, QTYPE_TXT);
    TEST_ASSERT_NOT_NULL(r);

    an = mdnsd_record_data(r);
    TEST_ASSERT_NOT_NULL(an);

    TEST_ASSERT_EQUAL_STRING(hlocal, an->name);

    memset(hlocal, 0, sizeof(hlocal));
    /* Add the service entry 1 */
    pconf = &g_mdns_ann[1];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    mgr->service_announcement(&g_mon, NULL, pconf);

    // Check if entry is present.
    snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", pconf->name, pconf->protocol);
    r = mdnsd_find(pctxt->dmn, hlocal, QTYPE_TXT);
    TEST_ASSERT_NOT_NULL(r);

    an = mdnsd_record_data(r);
    TEST_ASSERT_NOT_NULL(an);

    TEST_ASSERT_EQUAL_STRING(hlocal, an->name);

    memset(hlocal, 0, sizeof(hlocal));
    /* Del the service entry 0 */
    pconf = &g_mdns_ann[0];
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    mgr->service_announcement(&g_mon, pconf, NULL);

    // Check to make sure entry is deleted.
    snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", pconf->name, pconf->protocol);
    r = mdnsd_find(pctxt->dmn, hlocal, QTYPE_TXT);
    TEST_ASSERT_NULL(r);

#if !defined(__x86_64__)
    ev_run(mgr->loop, 0);
#endif

    mdns_plugin_exit(session);
}

void test_modify_mdnsd_service(void)
{
    struct fsm_session *session = &g_sessions[0];
    struct schema_Service_Announcement *pconf;
    struct mdns_plugin_mgr *mgr = mdns_get_mgr();
    struct mdnsd_context   *pctxt;
    char   hlocal[256] = {0};
    char   txt_str[128] = {0};
    int    wr_len= -1;

    mdns_record_t *r = NULL;
    const mdns_answer_t *an = NULL;

    mdns_mgr_init();

    mgr = mdns_get_mgr();
    mgr->ovsdb_init = ut_ovsdb_init;
    mgr->ovsdb_exit = ut_ovsdb_exit;

    mdns_plugin_init(session);
    pctxt = mgr->ctxt;

    TEST_ASSERT_NOT_NULL(pctxt);

    /* Add the service entry 0 */
    pconf = &g_mdns_ann[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    mgr->service_announcement(&g_mon, NULL, pconf);

    // Check if entry is present.
    snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", pconf->name, pconf->protocol);
    r = mdnsd_find(pctxt->dmn, hlocal, QTYPE_TXT);
    TEST_ASSERT_NOT_NULL(r);

    an = mdnsd_record_data(r);
    TEST_ASSERT_NOT_NULL(an);

    TEST_ASSERT_EQUAL_STRING(hlocal, an->name);

    memset(hlocal, 0, sizeof(hlocal));
    /* Add the service entry 1 */
    pconf = &g_mdns_ann[1];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    mgr->service_announcement(&g_mon, NULL, pconf);

    // Check if entry is present.
    snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", pconf->name, pconf->protocol);
    r = mdnsd_find(pctxt->dmn, hlocal, QTYPE_TXT);
    TEST_ASSERT_NOT_NULL(r);

    an = mdnsd_record_data(r);
    TEST_ASSERT_NOT_NULL(an);

    TEST_ASSERT_EQUAL_STRING(hlocal, an->name);

    memset(hlocal, 0, sizeof(hlocal));
    /* Modify service entry 0 */
    struct schema_Service_Announcement mdns_ann_modify[] =
    {
        /* entry 0 */
        {
            .name = "bw.plume",
            .protocol_present = true,
            .protocol = "_http._tcp",
            .port = 22,
            .txt_keys =
            {
                "v20",
            },
            .txt =
            {
                "https://bw.plume.com/test/api/v20",
            },
            .txt_len = 1,
        }
    };
    struct schema_Service_Announcement *pnew = &mdns_ann_modify[0];
    pconf = &g_mdns_ann[0];
    pnew = &mdns_ann_modify[0];
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    mgr->service_announcement(&g_mon, pconf, pnew);

    // Check to make sure entry is modified.
    wr_len = snprintf(txt_str, sizeof(txt_str), "%s=%s",  pnew->txt_keys[0], pnew->txt[0]);
    snprintf(hlocal, sizeof(hlocal), "%s.%s.local.", pconf->name, pconf->protocol);
    r = mdnsd_find(pctxt->dmn, hlocal, QTYPE_TXT);
    TEST_ASSERT_NOT_NULL(r);

    an = mdnsd_record_data(r);
    TEST_ASSERT_NOT_NULL(an);

    TEST_ASSERT_EQUAL_MEMORY(txt_str, &an->rdata[1], wr_len);
#if !defined(__x86_64__)
    ev_run(mgr->loop, 0);
#endif

    mdns_plugin_exit(session);
}

void
test_mdns_parser(void)
{
    struct net_header_parser *net_parser;
    struct fsm_session *session;
    struct mdns_plugin_mgr *mgr;
    struct mdnsd_context *pctxt;
    struct sockaddr_storage ss;
    uint16_t mdns_default_port;
    struct udphdr *hdr;
    unsigned char *data;
    uint16_t ethertype;
    struct message m;
    size_t nelems;
    int ip_protocol;
    bool is_ip = false;
    size_t len;
    size_t pld_len;
    size_t i;
    int rc;

    ut_prepare_pcap(__func__);

    /* Prepare the mdns manager */
    mdns_mgr_init();
    mgr = mdns_get_mgr();
    mgr->ovsdb_init = ut_ovsdb_init;
    mgr->ovsdb_exit = ut_ovsdb_exit;

    /* Prepare the mdns fsm session */
    session = &g_sessions[0];
    mdns_plugin_init(session);

    /* validate the existence of the mdnsd context */
    pctxt = mgr->ctxt;
    TEST_ASSERT_NOT_NULL(pctxt);

    /* Allocate a net parser */
    net_parser = CALLOC(1, sizeof(*net_parser));

    nelems = ARRAY_SIZE(pmap);
    for (i = 0; i < nelems; i++)
    {
        LOGI("%s: processing packet %s", __func__, pmap[i].name);
        /* Don't use the MACRO as we have explicit names */
        ut_create_pcap_payload(pmap[i].name, pmap[i].pkt, pmap[i].len, net_parser);

        len = net_header_parse(net_parser);
        if (len == 0)
        {
            LOGE("%s: Not processing packet %s", __func__, pmap[i].name);
            continue;
        }
        net_header_logi(net_parser);

        /* Some basic validation */
        /* Check ethertype */
        ethertype = net_header_get_ethertype(net_parser);
        is_ip = ((ethertype == ETH_P_IP) || (ethertype == ETH_P_IPV6));
        if (!is_ip) continue;

        /* Check for UDP protocol */
        ip_protocol = net_parser->ip_protocol;
        if (ip_protocol != IPPROTO_UDP) continue;

        /* Check the UDP src and dst ports (both need to be 5353) */
        hdr = net_parser->ip_pld.udphdr;
        mdns_default_port = htons(5353);
        if (!(hdr->source == mdns_default_port && hdr->dest == mdns_default_port))
        {
            LOGI("%s: Not a mdns packet",__func__);
            continue;
        }

        memset(&m, 0, sizeof(m));

        /* Access udp data */
        data = net_parser->ip_pld.payload;
        data += sizeof(struct udphdr);

        pld_len = net_parser->packet_len - net_parser->parsed;
        /* Parse the message */
        message_parse(&m, data, pld_len);
        rc = mdnsd_in(pctxt->dmn, &m, &ss);
        TEST_ASSERT_EQUAL(0, rc);
    }

    ut_cleanup_pcap();

    FREE(net_parser);
}

void
test_mdns_records_send_records(void)
{
    struct sockaddr_storage *ipv4_1;
    struct sockaddr_storage *ipv6_1;
    struct mdns_session md_session;
    struct fsm_session *session;
    struct resource res;
    bool rc;

    MEMZERO(md_session);

    /* No return value to be tested, but should not crash */
    LOGD("%s: mdns_records_send_records(NULL)", __func__);
    mdns_records_send_records(NULL);

    /* No initialized g_record */
    md_session.records_report_ts = time(NULL);
    md_session.records_report_interval = 2UL;
    mdns_records_send_records(&md_session);

    /* Initialize stuff */
    rc = mdns_records_init(NULL);
    TEST_ASSERT_FALSE(rc);

    /* incomplete md_session */
    rc = mdns_records_init(&md_session);
    TEST_ASSERT_FALSE(rc);

    /* We are NOT reporting, but we are still partially initialized */
    md_session.session = CALLOC(1, sizeof(*md_session.session));
    md_session.report_records = false;
    rc = mdns_records_init(&md_session);
    TEST_ASSERT_TRUE(rc);

    /* we have no node_id, location_id in session */
    md_session.report_records = true;
    rc = mdns_records_init(&md_session);
    TEST_ASSERT_FALSE(rc);

    /* Now with a clean md_session */
    session = md_session.session;
    session->node_id = "NODE_ID";
    session->location_id = "LOCATION_ID";
    rc = mdns_records_init(&md_session);
    TEST_ASSERT_TRUE(rc);

    /* initialized md_session */
    mdns_records_send_records(&md_session);

    ipv4_1 = sockaddr_storage_create(AF_INET, "192.168.0.1");
    ipv6_1 = sockaddr_storage_create(AF_INET6, "0:0:0:0:0:FFFF:204.152.189.116");

    MEMZERO(res);
    res.type = QTYPE_A;
    res.name = "REQUIRED_TO_BE_NON_NULL";
    mdns_records_collect_record(&res, NULL, ipv4_1);
    mdns_records_collect_record(&res, NULL, ipv6_1);

    LOGD("%s: mdns_records_send_records(POPULATED)", __func__);
    mdns_records_send_records(&md_session);

    /* terminate properly */
    mdns_records_exit();

    /* cleanup */
    FREE(ipv6_1);
    FREE(ipv4_1);
    FREE(md_session.session);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(ut_name, NULL, NULL);
    ut_setUp_tearDown(ut_name, mdns_plugin_setUp, mdns_plugin_tearDown);
    /* Node Info(Observation Point) tests */
    RUN_TEST(test_serialize_node_info);
    RUN_TEST(test_serialize_node_info_null_ptr);
    RUN_TEST(test_serialize_node_info_no_field_set);

    /* Observation window tests */
    RUN_TEST(test_serialize_observation_window);
    RUN_TEST(test_serialize_observation_window_null_ptr);
    RUN_TEST(test_serialize_observation_window_no_field_set);

    /* Mdns Record tests */
    RUN_TEST(test_serialize_record);
    RUN_TEST(test_set_records);
    RUN_TEST(test_mdns_records_send_records);

    /* Mdns client tests */
    RUN_TEST(test_serialize_client);
    RUN_TEST(test_set_serialization_clients);

    RUN_TEST(test_serialize_report);
    RUN_TEST(test_Mdns_Records_Report);

    RUN_TEST(test_load_unload_plugin);
    RUN_TEST(test_add_mdnsd_service);
    RUN_TEST(test_del_mdnsd_service);
    RUN_TEST(test_modify_mdnsd_service);
    /* Test parser */
    RUN_TEST(test_mdns_parser);

    return ut_fini();
}
