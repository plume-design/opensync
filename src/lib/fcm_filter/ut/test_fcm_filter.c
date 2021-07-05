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
#include <arpa/inet.h>

#include "fcm.h"
#include "target.h"
#include "log.h"
#include "unity.h"
#include "ovsdb_update.h"
#include "memutil.h"

#include "fcm_filter.h"
#include "network_metadata.h"

extern void callback_FCM_Filter(ovsdb_update_monitor_t *mon,
                                struct schema_FCM_Filter *old_rec,
                                struct schema_FCM_Filter *new_rec);

extern void callback_Openflow_Tag(ovsdb_update_monitor_t *mon,
                                  struct schema_Openflow_Tag *old_rec,
                                  struct schema_Openflow_Tag *tag);

extern void callback_Openflow_Tag_Group(ovsdb_update_monitor_t *mon,
                                        struct schema_Openflow_Tag_Group *old_rec,
                                        struct schema_Openflow_Tag_Group *tag);
void setup_alloc_flow_tag(struct flow_key *fkey, char *appname,
                          char *sub_tag0, char *sub_tag1);
void free_flow_tag(struct flow_key *fkey);
const char *test_name = "fcm_filter_ut";

struct fcm_session *session = NULL;
struct fcm_filter_client *client = NULL;
struct fcm_filter_client *dev_webpulse_client = NULL;

struct schema_FCM_Filter g_fcm_filter[] =
{   /* entry 0 */
    {
        .name = "fcm_filter_1",
        .index = 1,
        .smac_len = 1,
        .smac_op_exists = true,
        .smac[0] = "11:22:33:44:55:66",
        .dmac_op_exists = true,
        .dmac_len = 1,
        .dmac[0] = "A6:55:44:33:22:1A",
        .smac_op = "in",
        .dmac_op = "in",

        .pktcnt_op_exists = true,
        .pktcnt = 20,
        .pktcnt_op = "gt",

        .vlanid_op_exists = false,
        .src_ip_op_exists = false,
        .dst_ip_op_exists = false,
        .src_port_op_exists = false,
        .dst_port_op_exists = false,
        .proto_op_exists = false,

        .action = "include",
    },
    /* entry 1 */
    {
        .name = "fcm_filter_1",
        .index = 2,
        .smac_len = 1,
        .smac_op_exists = true,
        .smac[0] = "A6:55:44:33:22:1A",
        .dmac_len = 1,
        .dmac_op_exists = true,
        .dmac[0] = "11:22:33:44:55:66",
        .smac_op = "in",
        .dmac_op = "in",

        .pktcnt_op_exists = true,
        .pktcnt = 20,
        .pktcnt_op = "gt",

        .vlanid_op_exists = false,
        .src_ip_op_exists = false,
        .dst_ip_op_exists = false,
        .src_port_op_exists = false,
        .dst_port_op_exists = false,
        .proto_op_exists = false,

        .action = "include",
    },
    /* entry 2 */
    /* ip related filter */
    {
        .name = "fcm_filter_1",
        .index = 3,
        .src_ip_op_exists = true,
        .src_ip_len = 1,
        .src_ip[0] = "192.168.40.12",
        .dst_ip_op_exists = true,
        .dst_ip_len = 1,
        .dst_ip[0] = "10.2.20.121",
        .src_ip_op = "in",
        .dst_ip_op = "in",

        .smac_op_exists = false,
        .dmac_op_exists = false,
        .vlanid_op_exists = false,
        .src_port_op_exists = false,
        .dst_port_op_exists = false,
        .proto_op_exists = false,

        .action = "include",
    },
    /* entry 3 */
    {
        .name = "fcm_filter_1",
        .index = 4,
        .src_ip_op_exists = true,
        .src_ip_len = 1,
        .src_ip[0] = "192.168.40.121",
        .dst_ip_op_exists = true,
        .dst_ip_len = 1,
        .dst_ip[0] = "10.2.20.32",
        .src_ip_op = "in",
        .dst_ip_op = "in",

        .smac_op_exists = false,
        .dmac_op_exists = false,
        .vlanid_op_exists = false,
        .src_port_op_exists = false,
        .dst_port_op_exists = false,
        .proto_op_exists = false,

        .action = "include",
    },
    /* entry 4 */
    {
        .name = "fcm_filter_1",
        .index = 5,
        .src_ip_op_exists = true,
        .src_ip_len = 1,
        .src_ip[0] = "10.2.20.32",
        .dst_ip_op_exists = true,
        .dst_ip_len = 1,
        .dst_ip[0] = "192.168.40.12",
        .src_ip_op = "in",
        .dst_ip_op = "in",

        .smac_op_exists = false,
        .dmac_op_exists = false,
        .vlanid_op_exists = false,
        .src_port_op_exists = false,
        .dst_port_op_exists = false,
        .proto_op_exists = false,

        .action = "include",
    },
    /* entry 5 */
    {
        .name = "fcm_filter_app1",
        .index = 6,
        .src_ip_op_exists = true,
        .src_ip_len = 1,
        .src_ip[0] = "157.240.22.35",
        .dst_ip_op_exists = true,
        .dst_ip_len = 1,
        .dst_ip[0] = "192.168.40.2",
        .src_ip_op = "in",
        .dst_ip_op = "in",
        .appnames_present = true,
        .appnames[0] = "invalid_name",
        .appnames[1] = "wrong_name",
        .appnames[2] = "facebook",
        .appnames_len = 3,
        .appname_op_exists = true,
        .appname_op = "in",
        .apptags[0] = "invalid_video",
        .apptags[1] = "wrong_video",
        .apptags[2] = "facebook_video",
        .apptag_op = "in",
        .apptags_len = 3,

        .smac_op_exists = false,
        .dmac_op_exists = false,
        .vlanid_op_exists = false,
        .src_port_op_exists = false,
        .dst_port_op_exists = false,
        .proto_op_exists = false,

        .action = "include",
    },
    /* entry 6 */
    {
        .name = "fcm_filter_app2",
        .index = 7,
        .src_ip_op_exists = true,
        .src_ip_len = 1,
        .src_ip[0] = "172.2.3.4",
        .dst_ip_op_exists = true,
        .dst_ip_len = 1,
        .dst_ip[0] = "192.168.40.3",
        .src_ip_op = "in",
        .dst_ip_op = "in",
        .appnames_present = true,
        .appnames[0] = "invalid_name1",
        .appnames[1] = "wrong_name1",
        .appnames[2] = "youtube",
        .appnames_len = 3,
        .appname_op_exists = true,
        .appname_op = "in",
        .apptags[0] = "invalid_ads",
        .apptags[1] = "wrong_ads",
        .apptags[2] = "youtube_ads",
        .apptag_op = "in",
        .apptags_len = 3,

        .smac_op_exists = false,
        .dmac_op_exists = false,
        .vlanid_op_exists = false,
        .src_port_op_exists = false,
        .dst_port_op_exists = false,
        .proto_op_exists = false,

        .action = "include",
    },
    /* entry 7 */
    {
        .name = "dev_webpulse",
        .index = 8,
        .src_ip_op_exists = true,
        .src_ip_len = 1,
        .src_ip[0] = "192.168.40.121",
        .dst_ip_op_exists = true,
        .dst_ip_len = 1,
        .dst_ip[0] = "10.2.20.32",
        .src_ip_op = "in",
        .dst_ip_op = "in",

        .smac_op_exists = false,
        .dmac_op_exists = false,
        .vlanid_op_exists = false,
        .src_port_op_exists = false,
        .dst_port_op_exists = false,
        .proto_op_exists = false,

        .action_exists = true,
        .action = "drop",
        .other_config_len = 1,
        .other_config_keys = { "rd_ttl", },
        .other_config = { "5" },

    },
    /* entry 8 */
    {
        .name = "Table1",
        .index = 9,
        .src_ip_op_exists = true,
        .src_ip_len = 1,
        .src_ip[0] = "192.168.40.12",
        .dst_ip_op_exists = true,
        .dst_ip_len = 1,
        .dst_ip[0] = "10.2.20.121",
        .src_ip_op = "in",
        .dst_ip_op = "in",

        .smac_op_exists = false,
        .dmac_op_exists = false,
        .vlanid_op_exists = false,
        .src_port_op_exists = false,
        .dst_port_op_exists = false,
        .proto_op_exists = false,

    }
};

fcm_filter_l2_info_t g_flow_l2[] =
{
    {
        .smac_op_exists = true,
        .dmac_op_exists = true,
        .vlanid_op_exists = true,
        .src_mac = "11:22:33:44:55:66",
        .dst_mac = "A6:55:44:33:22:1A",
        .vlan_id = 123,
        .eth_type = 0x0800
    },
    {
        .smac_op_exists = true,
        .dmac_op_exists = true,
        .vlanid_op_exists = true,
        .src_mac = "A6:55:44:33:22:1A",
        .dst_mac = "11:22:33:44:55:66",
        .vlan_id = 123,
        .eth_type = 0x0800
    },
    {
        .smac_op_exists = true,
        .dmac_op_exists = true,
        .vlanid_op_exists = true,
        .src_mac = "11:22:33:44:55:77",
        .dst_mac = "A6:55:44:33:22:88",
        .vlan_id = 123,
        .eth_type = 0x0800
    },
};

fcm_filter_l3_info_t g_flow_l3[] =
{
    {
        .src_ip_op_exists = true,
        .dst_ip_op_exists = true,
        .src_port_op_exists = true,
        .dst_port_op_exists = true,
        .proto_op_exists = true,

        .src_ip = "192.168.40.121",
        .dst_ip = "10.2.20.32",
        .sport = 67,
        .dport = 68,
        .l4_proto = 17,
        .ip_type = AF_INET
    },
    {
        .src_ip_op_exists = true,
        .dst_ip_op_exists = true,
        .src_port_op_exists = true,
        .dst_port_op_exists = true,
        .proto_op_exists = true,

        .src_ip = "192.168.40.121",
        .dst_ip = "10.2.20.32",
        .sport = 1234,
        .dport = 80,
        .l4_proto = 6,
        .ip_type = AF_INET
    },
    {
        .src_ip_op_exists = true,
        .dst_ip_op_exists = true,
        .src_port_op_exists = true,
        .dst_port_op_exists = true,
        .proto_op_exists = true,

        .src_ip = "192.168.40.121",
        .dst_ip = "10.2.20.32",
        .sport = 2123,
        .dport = 8855,
        .l4_proto = 17,
        .ip_type = AF_INET
    },
    {
        .src_ip_op_exists = true,
        .dst_ip_op_exists = true,
        .src_port_op_exists = true,
        .dst_port_op_exists = true,
        .proto_op_exists = true,

        .src_ip = "10.2.20.32",
        .dst_ip = "192.168.40.121",
        .sport = 80,
        .dport = 1234,
        .l4_proto = 6,
        .ip_type = AF_INET
    },
};

fcm_filter_stats_t g_flow_pkt[] =
{
    {
        .pktcnt_op_exists = true,
        .pkt_cnt = 50,
        .bytes = 100
    },
    {
        .pktcnt_op_exists = true,
        .pkt_cnt = 10,
        .bytes = 1000
    },
    {
        .pktcnt_op_exists = true,
        .pkt_cnt = 30,
        .bytes = 100
    },
    {
        .pktcnt_op_exists = true,
        .pkt_cnt = 20,
        .bytes = 100
    },

    {
        .pktcnt_op_exists = true,
        .pkt_cnt = 40,
        .bytes = 1000
    },
    {
        .pktcnt_op_exists = true,
        .pkt_cnt = 60,
        .bytes = 3400
    },
    {
        .pktcnt_op_exists = true,
        .pkt_cnt = 70,
        .bytes = 67800
    },
    {
        .pktcnt_op_exists = true,
        .pkt_cnt = 1000,
        .bytes = 70000
    },
};

struct flow_key g_fkey[] =
{
    {
        .num_tags = 3,
    },
    {
        .num_tags = 3,
    }
};

ovsdb_update_monitor_t g_mon;

static void ut_ovsdb_init(void) {}

void setUp(void)
{
    struct fcm_filter_mgr *mgr;

    mgr = get_filter_mgr();
    mgr->ovsdb_init = ut_ovsdb_init;
    memset(&g_mon, 0, sizeof(g_mon));
    fcm_filter_init();

    setup_alloc_flow_tag(&g_fkey[0], "facebook", "facebook_video", "facebook_audio");
    setup_alloc_flow_tag(&g_fkey[0], "xyz", "xyz_video", "xyz_audio");
    setup_alloc_flow_tag(&g_fkey[0], "abc", "abc_video", "abc_audio");

    setup_alloc_flow_tag(&g_fkey[1], "youtube", "youtube_content", "youtube_ad");
    setup_alloc_flow_tag(&g_fkey[1], "def", "def_content", "def_ad");
    setup_alloc_flow_tag(&g_fkey[1], "hij", "hij_content", "hij_ad");

    return;
}


void tearDown(void)
{
    fcm_filter_cleanup();

    free_flow_tag(&g_fkey[0]);
    free_flow_tag(&g_fkey[1]);
    return;
}

void free_flow_tag(struct flow_key *fkey)
{
    struct flow_tags **key_tags;
    struct flow_tags *tag;
    size_t i;
    size_t j;

    for (i = 0; i < fkey->num_tags; i++)
    {
        key_tags = &fkey->tags[i];
        tag = (*key_tags);
        for (j = 0; j < tag->nelems; j++)
            FREE(tag->tags[j]);
        FREE(tag->tags);
        FREE(tag->app_name);
        FREE(tag->vendor);
        FREE(tag);
    }
    FREE(fkey->tags);
    fkey->tags = NULL;
}

void setup_alloc_flow_tag(struct flow_key *fkey, char *appname,
                          char *sub_tag0, char *sub_tag1)
{
    struct flow_tags **key_tags;
    struct flow_tags *tag;
    size_t i;
    static size_t j = 0;

    if (!fkey->tags)
    {
        key_tags = CALLOC(fkey->num_tags, sizeof(*key_tags));
        fkey->tags = key_tags;
    }
    else
    {
        key_tags = fkey->tags;
    }

    if (key_tags == NULL) return;

    tag = CALLOC(1, sizeof(*tag));

    tag->vendor = strdup("walleye");
    if (tag->vendor == NULL) return;

    tag->app_name = strdup(appname);
    if (tag->app_name == NULL) goto err_free_vendor;

    tag->nelems = 2;
    tag->tags = CALLOC(tag->nelems, sizeof(tag->tags));
    tag->tags[0] = strdup(sub_tag0);
    if (tag->tags[0] == NULL) goto err_free_tag_tags;

    tag->tags[1] = strdup(sub_tag1);
    if (tag->tags[1] == NULL) goto err_free_tag_tags_0;

    key_tags[j] = tag;

    j++;
    if (j == fkey->num_tags) j = 0;

    return;

err_free_tag_tags_0:
    FREE(tag->tags[0]);

err_free_tag_tags:
    for (i = 0; i < tag->nelems; i++) FREE(tag->tags[i]);
    FREE(tag->tags);

err_free_app_name:
    FREE(tag->app_name);

err_free_vendor:
    FREE(tag->vendor);

err_free_key_tags:
    FREE(key_tags);

}

static void test_update_client(struct fcm_session *session,
                               struct filter_table *table)
{
    struct fcm_filter_client *client;

    client = (struct fcm_filter_client *)session->handler_ctxt;
    TEST_ASSERT_NOT_NULL(client);
    client->table = table;
}

void test_fcm_filter_client(void)
{
    struct schema_FCM_Filter *sfilter;
    char *default_name = "fcm_filter_1";
    struct filter_table *table;
    struct fcm_filter_mgr *mgr;

    /* Insert default filter */
    sfilter = &g_fcm_filter[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sfilter);

    session = CALLOC(1, sizeof(*session));

    client = CALLOC(1, sizeof(*client));
    session->handler_ctxt = client;
    client->session = session;
    client->update_client = test_update_client;

    /* Register the client. Its table pointer should be set */
    fcm_filter_register_client(client);
    TEST_ASSERT_NOT_NULL(client->table);

    mgr = get_filter_mgr();
    table = ds_tree_find(&mgr->fcm_filters, default_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == client->table);

    fcm_filter_deregister_client(client);

    /* Register the client with no matching filter yet */
    sfilter = &g_fcm_filter[8];
    client->name = strdup(sfilter->name);
    client->session = session;
    TEST_ASSERT_NOT_NULL(client->name);
    fcm_filter_register_client(client);
    TEST_ASSERT_NULL(client->table);

    /* Insert matching filter */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sfilter);

    table = ds_tree_find(&mgr->fcm_filters, sfilter->name);
    TEST_ASSERT_NOT_NULL(table);

    /* Validate that the client's table pointer was updated */
    TEST_ASSERT_NOT_NULL(client->table);
    TEST_ASSERT_TRUE(table == client->table);
    fcm_filter_deregister_client(client);

    FREE(client->name);
    FREE(client);
    FREE(session);
}

void test_fcm_filter_clients_same_session(void)
{
    struct schema_FCM_Filter *sfilter;
    struct fcm_filter_client *default_filter_client;
    char *default_name = "fcm_filter_1";
    char *other_name = "dev_webpulse";
    struct filter_table *table;
    struct fcm_filter_mgr *mgr;

    /* Insert default filter */
    sfilter = &g_fcm_filter[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sfilter);

    /* Insert dev_webpulse filter */
    sfilter = &g_fcm_filter[7];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sfilter);

    session = CALLOC(1, sizeof(*session));

    default_filter_client = CALLOC(1, sizeof(*default_filter_client));
    session->handler_ctxt = default_filter_client;
    default_filter_client->session = session;
    default_filter_client->update_client = test_update_client;

    /* Register the client. Its table pointer should be set */
    fcm_filter_register_client(default_filter_client);
    TEST_ASSERT_NOT_NULL(default_filter_client->table);

    mgr = get_filter_mgr();
    table = ds_tree_find(&mgr->fcm_filters, default_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == default_filter_client->table);

    fcm_filter_deregister_client(default_filter_client);
    TEST_ASSERT_NULL(default_filter_client->table);
    FREE(default_filter_client->name);
    FREE(default_filter_client);

    dev_webpulse_client = CALLOC(1, sizeof(*dev_webpulse_client));
    dev_webpulse_client->name = strdup(other_name);
    TEST_ASSERT_NOT_NULL(dev_webpulse_client->name);
    session->handler_ctxt = default_filter_client;
    dev_webpulse_client->session = session;
    dev_webpulse_client->update_client = test_update_client;

    /* Register the client. Its table pointer should be set */
    fcm_filter_register_client(dev_webpulse_client);
    TEST_ASSERT_NOT_NULL(dev_webpulse_client->table);

    table = ds_tree_find(&mgr->fcm_filters, other_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == dev_webpulse_client->table);

    fcm_filter_deregister_client(dev_webpulse_client);
    TEST_ASSERT_NULL(dev_webpulse_client->table);
    FREE(dev_webpulse_client->name);
    FREE(dev_webpulse_client);

    FREE(session);
}

void test_fcm_filter_init_deinit(void)
{
    struct fcm_filter_mgr *mgr = get_filter_mgr();
    int ret;

    /* setUp()_ called init already */
    ret = fcm_filter_init();
    TEST_ASSERT_EQUAL_INT(1, ret);

    fcm_filter_cleanup();
    TEST_ASSERT_EQUAL_INT(0, mgr->initialized);
}

void test_add_sfilter(void)
{
    struct schema_FCM_Filter *sch_filter;
    struct fcm_filter *ffilter, *sfilter;
    struct filter_table *table;

     /* Add a legit rules */
    sch_filter = &g_fcm_filter[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);
    sfilter = fcm_filter_lookup(sch_filter);

    /* Validate access to the fcm filter */
    TEST_ASSERT_NOT_NULL(sfilter);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(sch_filter->name, sfilter->filter_rule.name);

    /* Validate table name */
    table = sfilter->table;
    TEST_ASSERT_EQUAL_STRING("fcm_filter_1", table->name);

    /* Free the filter */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_FCM_Filter(&g_mon, sch_filter, NULL);

    /* Add the filter and validate all over again */
    sch_filter = &g_fcm_filter[1];
    g_mon.mon_type = OVSDB_UPDATE_NEW;

    callback_FCM_Filter(&g_mon, NULL, sch_filter);
    ffilter = fcm_filter_lookup(sch_filter);
    TEST_ASSERT_NOT_NULL(ffilter);
    TEST_ASSERT_EQUAL_STRING(sch_filter->name, ffilter->filter_rule.name);

    /* Free the filter */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_FCM_Filter(&g_mon, sch_filter, NULL);
}

void test_add_sfilter1(void)
{
    struct schema_FCM_Filter *sch_filter;
    struct fcm_filter *sfilter;
    struct filter_table *table;

    /* Add a legit rules */
    sch_filter = &g_fcm_filter[2];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);
    sfilter = fcm_filter_lookup(sch_filter);

    /* Validate access to the fcm filter */
    TEST_ASSERT_NOT_NULL(sfilter);

    /* Validate rule name */
    TEST_ASSERT_EQUAL_STRING(sch_filter->name, sfilter->filter_rule.name);

    /* Validate table name */
    table = sfilter->table;
    TEST_ASSERT_EQUAL_STRING("fcm_filter_1", table->name);

    /* Free the filter */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_FCM_Filter(&g_mon, sch_filter, NULL);
}

void test_update_filter(void)
{
    struct schema_FCM_Filter *sch_filter;
    struct fcm_filter *sfilter;

    /* Add a legit rules */
    sch_filter = &g_fcm_filter[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);
    sfilter = fcm_filter_lookup(sch_filter);

    /* Validate access to the fcm filter */
    TEST_ASSERT_NOT_NULL(sfilter);

    /* Now update / modify the existing rule and check for fail*/

    struct schema_FCM_Filter fcm_filter_modify = {
        .name = "fcm_filter_1",
        .index = 1,
        .smac_op_exists = true,
        .smac_len = 1,
        .smac[0] = "11:22:33:77:88:99",    /* change from 44:55:66 */
        .dmac_op_exists = true,
        .dmac_len = 1,
        .dmac[0] = "A6:55:44:33:22:1A",
        .smac_op = "in",
        .dmac_op = "in",

        .pktcnt = 20,
        .pktcnt_op = "gt",

        .action = "include",
    };

    /* Update the fcm_filter with new modified rule which makes test fail */
    sch_filter = &g_fcm_filter[0];
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_FCM_Filter(&g_mon, sch_filter, &fcm_filter_modify);
    sfilter = fcm_filter_lookup(sch_filter);

    /* Free the filter */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_FCM_Filter(&g_mon, sch_filter, NULL);
}

void test_fcm_filter_ip(void)
{
    fcm_filter_l3_info_t *l3_filter_info;
    fcm_filter_stats_t *l2_filter_pkts;
    struct schema_FCM_Filter *sch_filter;
    char *default_name = "fcm_filter_1";
    struct filter_table *table;
    struct fcm_filter_mgr *mgr;
    struct fcm_filter_req *req;

    /* Add a legit rules */
    sch_filter = &g_fcm_filter[2];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    /* Add a legit rules */
    sch_filter = &g_fcm_filter[3];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    /* Add a legit rules */
    sch_filter = &g_fcm_filter[4];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    session = CALLOC(1, sizeof(*session));

    client = CALLOC(1, sizeof(*client));
    session->handler_ctxt = client;
    client->session = session;
    client->update_client = test_update_client;

    /* Register the client. Its table pointer should be set */
    fcm_filter_register_client(client);
    TEST_ASSERT_NOT_NULL(client->table);

    mgr = get_filter_mgr();
    table = ds_tree_find(&mgr->fcm_filters, default_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == client->table);

    /* checks with proper value that pass the rule */
    l3_filter_info = &g_flow_l3[0];
    l2_filter_pkts = &g_flow_pkt[0];

    req = CALLOC(1, sizeof(struct fcm_filter));
    req->pkts =  l2_filter_pkts;
    req->l3_info = l3_filter_info;
    req->table = client->table;
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    l3_filter_info = &g_flow_l3[1];
    l2_filter_pkts = &g_flow_pkt[0];

    req->pkts =  l2_filter_pkts;
    req->l3_info = l3_filter_info;
    req->table = client->table;
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    l3_filter_info = &g_flow_l3[2];
    l2_filter_pkts = &g_flow_pkt[0];

    req->pkts =  l2_filter_pkts;
    req->l3_info = l3_filter_info;
    req->table = client->table;
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    fcm_filter_deregister_client(client);
    FREE(client->name);
    FREE(client);
    FREE(session);
    FREE(req);
}

void test_fcm_apply_filter_check_7tuple_apply(void)
{
    fcm_filter_l3_info_t *l3_filter_info;
    fcm_filter_stats_t *l2_filter_pkts;
    struct schema_FCM_Filter *sfilter;
    char *default_name = "fcm_filter_1";
    struct filter_table *table;
    struct fcm_filter_mgr *mgr;
    struct fcm_filter_req *req;

    /* Insert 7tuplefilter */
    sfilter = &g_fcm_filter[3];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sfilter);

    session = CALLOC(1, sizeof(*session));

    client = CALLOC(1, sizeof(*client));
    session->handler_ctxt = client;
    client->session = session;
    client->update_client = test_update_client;

    /* Register the client. Its table pointer should be set */
    fcm_filter_register_client(client);
    TEST_ASSERT_NOT_NULL(client->table);

    mgr = get_filter_mgr();
    table = ds_tree_find(&mgr->fcm_filters, default_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == client->table);

    /* checks with proper value that pass the rule */
    l3_filter_info = &g_flow_l3[0];
    l2_filter_pkts = &g_flow_pkt[0];

    req = CALLOC(1, sizeof(struct fcm_filter));
    req->pkts =  l2_filter_pkts;
    req->l3_info = l3_filter_info;
    req->table = client->table;
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    l3_filter_info = &g_flow_l3[1];
    l2_filter_pkts = &g_flow_pkt[0];

    req->pkts =  l2_filter_pkts;
    req->l3_info = l3_filter_info;
    req->table = client->table;
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    l3_filter_info = &g_flow_l3[2];
    l2_filter_pkts = &g_flow_pkt[0];

    req->pkts =  l2_filter_pkts;
    req->l3_info = l3_filter_info;
    req->table = client->table;
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    fcm_filter_deregister_client(client);
    FREE(client->name);
    FREE(client);
    FREE(session);
    FREE(req);
}

void test_fcm_apply_filter_check_l2_apply(void)
{
    fcm_filter_l2_info_t *l2_filter_info;
    fcm_filter_stats_t *l2_filter_pkts;
    struct schema_FCM_Filter *sfilter;
    char *default_name = "fcm_filter_1";
    struct filter_table *table;
    struct fcm_filter_mgr *mgr;
    struct fcm_filter_req *req;

    /* Insert l2 filter */
    sfilter = &g_fcm_filter[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sfilter);

    session = CALLOC(1, sizeof(*session));

    client = CALLOC(1, sizeof(*client));
    session->handler_ctxt = client;
    client->session = session;
    client->update_client = test_update_client;

    /* Register the client. Its table pointer should be set */
    fcm_filter_register_client(client);
    TEST_ASSERT_NOT_NULL(client->table);

    mgr = get_filter_mgr();
    table = ds_tree_find(&mgr->fcm_filters, default_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == client->table);


    /* checks with proper value that pass the rule */
    l2_filter_info = &g_flow_l2[0];
    l2_filter_pkts = &g_flow_pkt[0];

    req = CALLOC(1, sizeof(struct fcm_filter));
    req->pkts =  l2_filter_pkts;
    req->l2_info = l2_filter_info;
    req->table = client->table;
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    fcm_filter_deregister_client(client);
    FREE(client->name);
    FREE(client);
    FREE(session);
    FREE(req);
}

void test_fcm_filter_app_add(void)
{
    struct schema_FCM_Filter *sfilter;
    char *default_name = "fcm_filter_app1";
    struct filter_table *table;
    struct fcm_filter_mgr *mgr;
    struct fcm_filter_req *req;

    /* Insert app filter rule */
    sfilter = &g_fcm_filter[5];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sfilter);

    session = CALLOC(1, sizeof(*session));

    client = CALLOC(1, sizeof(*client));
    session->handler_ctxt = client;
    client->session = session;
    client->name = strdup(default_name);
    client->update_client = test_update_client;

    /* Register the client. Its table pointer should be set */
    fcm_filter_register_client(client);
    TEST_ASSERT_NOT_NULL(client->table);

    mgr = get_filter_mgr();
    table = ds_tree_find(&mgr->fcm_filters, default_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == client->table);

    req = CALLOC(1, sizeof(struct fcm_filter));
    req->fkey = &g_fkey[0];
    req->table = client->table;

    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    /* checks with app that is not present in rule */
    req->fkey = &g_fkey[1];
    req->table = client->table;
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action == false);

    fcm_filter_deregister_client(client);
    FREE(client->name);
    FREE(client);
    FREE(session);
    FREE(req);
}

void test_fcm_filter_app_delete(void)
{
    struct schema_FCM_Filter *sch_filter;
    char *default_name = "fcm_filter_app1";
    struct filter_table *table;
    struct fcm_filter_mgr *mgr;
    struct fcm_filter_req *req;

    /* Insert app filter rule */
    /* Add app rules */
    sch_filter = &g_fcm_filter[5];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    session = CALLOC(1, sizeof(*session));

    client = CALLOC(1, sizeof(*client));
    session->handler_ctxt = client;
    client->session = session;
    client->name = strdup(default_name);
    client->update_client = test_update_client;

    /* Register the client. Its table pointer should be set */
    fcm_filter_register_client(client);
    TEST_ASSERT_NOT_NULL(client->table);

    mgr = get_filter_mgr();
    table = ds_tree_find(&mgr->fcm_filters, default_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == client->table);

    req = CALLOC(1, sizeof(struct fcm_filter));
    req->fkey = &g_fkey[0];
    req->table = client->table;

    /* checks with proper value that pass the rule */
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    /* Delete app rules */
    sch_filter = &g_fcm_filter[5];
    /* Free the filter */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_FCM_Filter(&g_mon, sch_filter, NULL);

    /* Add exclude rules */
    sch_filter = &g_fcm_filter[5];
    STRSCPY(sch_filter->appname_op, "out");
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    req->fkey = &g_fkey[0];
    req->table = client->table;

    /* checks with app not present in rule */
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action == false);

    fcm_filter_deregister_client(client);
    FREE(client->name);
    FREE(client);
    FREE(session);
    FREE(req);

    // Reset back the input to original.
    STRSCPY(sch_filter->appname_op, "in");
}

void test_fcm_filter_app_update(void)
{
    struct schema_FCM_Filter *sch_filter;
    char *default_name = "fcm_filter_app1";
    struct filter_table *table;
    struct fcm_filter_mgr *mgr;
    struct fcm_filter_req *req;

    /* Modify app rules */
    struct schema_FCM_Filter fcm_filter_app_mod = {
        .name = "fcm_filter_app1",
        .index = 6,
        .src_ip_len = 1,
        .src_ip[0] = "157.240.22.35",
        .dst_ip_len = 1,
        .dst_ip[0] = "192.168.40.2",
        .src_ip_op = "in",
        .dst_ip_op = "in",
        .appnames_present = true,
        .appnames[0] = "syz",
        .appnames[1] = "youtube",
        .appnames_len = 2,
        .appname_op_exists = true,
        .appname_op = "in",
        .apptags[0] = "youtube_content",
        .apptag_op = "out",
        .apptags_len = 1,

        .action = "include"
    };

    /* Add app rules */
    sch_filter = &g_fcm_filter[5];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    session = CALLOC(1, sizeof(*session));

    client = CALLOC(1, sizeof(*client));
    session->handler_ctxt = client;
    client->session = session;
    client->name = strdup(default_name);
    client->update_client = test_update_client;

    /* Register the client. Its table pointer should be set */
    fcm_filter_register_client(client);
    TEST_ASSERT_NOT_NULL(client->table);

    mgr = get_filter_mgr();
    table = ds_tree_find(&mgr->fcm_filters, default_name);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE(table == client->table);

    req = CALLOC(1, sizeof(struct fcm_filter));
    req->fkey = &g_fkey[0];
    req->table = client->table;

    /* checks with proper value that pass the rule */
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    /* Test appname_op_exists false */
    sch_filter = &g_fcm_filter[5];
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    fcm_filter_app_mod.appname_op_exists = false;
    callback_FCM_Filter(&g_mon, sch_filter, &fcm_filter_app_mod);

    req->fkey = &g_fkey[0];
    req->table = client->table;

    /* check for app not present in rule */
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    // Reset back the appname_op_exists.
    fcm_filter_app_mod.appname_op_exists = true;

    /* Update the fcm_filter with new modified rule which makes test fail */
    sch_filter = &g_fcm_filter[5];
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_FCM_Filter(&g_mon, sch_filter, &fcm_filter_app_mod);

    req->fkey = &g_fkey[0];
    req->table = client->table;

    /* same values should fail now due to rule modified */
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action == false);

    req->fkey = &g_fkey[1];
    req->table = client->table;

    /*filter should succeed now */
    fcm_apply_filter(session, req);
    TEST_ASSERT_TRUE(req->action);

    fcm_filter_deregister_client(client);
    FREE(client->name);
    FREE(client);
    FREE(session);
    FREE(req);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_INFO);

    UnityBegin(test_name);
    RUN_TEST(test_fcm_filter_init_deinit);
    RUN_TEST(test_add_sfilter);
    RUN_TEST(test_add_sfilter1);
    RUN_TEST(test_update_filter);
    RUN_TEST(test_fcm_filter_ip);

    // Client tests
    RUN_TEST(test_fcm_filter_client);
    RUN_TEST(test_fcm_filter_clients_same_session);

    // App filter tests.
    RUN_TEST(test_fcm_filter_app_add);
    RUN_TEST(test_fcm_filter_app_delete);
    RUN_TEST(test_fcm_filter_app_update);

    // Test fcm_apply_filter
    RUN_TEST(test_fcm_apply_filter_check_7tuple_apply);
    RUN_TEST(test_fcm_apply_filter_check_l2_apply);

    return UNITY_END();
}

