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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ev.h>
#include <time.h>

#include "log.h"
#include "target.h"
#include "unity.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"

#include "pcap.c"

#include "mdns_plugin.h"


#define OTHER_CONFIG_NELEMS 4
#define OTHER_CONFIG_NELEM_SIZE 128

extern void callback_Service_Announcement(ovsdb_update_monitor_t *mon,
                         struct schema_Service_Announcement *old_rec,
                         struct schema_Service_Announcement *new_rec);

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
        },
        {
            "192.168.1.90",
            "br-home.tx",
        },
    },
};

struct fsm_session_conf g_confs[] =
{
    /* entry 0 */
    {
        .handler = "mdns_plugin_session_0",
    }
};

struct fsm_session g_sessions[] =
{
    {
        .type = FSM_PARSER,
        .conf = &g_confs[0],
    }
};

ovsdb_update_monitor_t g_mon;

struct mdns_plugin_mgr *mgr;
const char *test_name = "mdns_tests";
void ut_ovsdb_init(void)
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


void setUp(void)
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

    return;
}

void tearDown(void)
{
    struct fsm_session *session = &g_sessions[0];

    free_str_tree(session->conf->other_config);

    return;
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

    mdns_plugin_init(session);
    pctxt = mgr->ctxt;

    TEST_ASSERT_NOT_NULL(pctxt);


    /* Add the service entry 0 */
    pconf = &g_mdns_ann[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Service_Announcement(&g_mon, NULL, pconf);

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
    callback_Service_Announcement(&g_mon, NULL, pconf);

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

    mdns_plugin_init(session);
    pctxt = mgr->ctxt;

    TEST_ASSERT_NOT_NULL(pctxt);

    /* Add the service entry 0 */
    pconf = &g_mdns_ann[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Service_Announcement(&g_mon, NULL, pconf);

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
    callback_Service_Announcement(&g_mon, NULL, pconf);

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
    callback_Service_Announcement(&g_mon, pconf, NULL);

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

    mdns_plugin_init(session);
    pctxt = mgr->ctxt;

    TEST_ASSERT_NOT_NULL(pctxt);

    /* Add the service entry 0 */
    pconf = &g_mdns_ann[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Service_Announcement(&g_mon, NULL, pconf);

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
    callback_Service_Announcement(&g_mon, NULL, pconf);

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
    callback_Service_Announcement(&g_mon, pconf, pnew);

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

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    RUN_TEST(test_load_unload_plugin);
    RUN_TEST(test_add_mdnsd_service);
    RUN_TEST(test_del_mdnsd_service);
    RUN_TEST(test_modify_mdnsd_service);

    return UNITY_END();
}
