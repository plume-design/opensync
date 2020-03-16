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

#include "target.h"
#include "log.h"
#include "unity.h"
#include "ovsdb_update.h"

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


struct schema_FCM_Filter g_fcm_filter[] =
{   /* entry 0 */
    {
        .name = "fcm_filter_1",
        .index = 1,
        .smac_len = 1,
        .smac[0] = "11:22:33:44:55:66",
        .dmac_len = 1,
        .dmac[0] = "A6:55:44:33:22:1A",
        .smac_op = "in",
        .dmac_op = "in",

        .pktcnt = 20,
        .pktcnt_op = "gt",

        .action = "include",
    },
    /* entry 1 */
    {
        .name = "fcm_filter_1",
        .index = 2,
        .smac_len = 1,
        .smac[0] = "A6:55:44:33:22:1A",
        .dmac_len = 1,
        .dmac[0] = "11:22:33:44:55:66",
        .smac_op = "in",
        .dmac_op = "in",

        .pktcnt = 20,
        .pktcnt_op = "gt",

        .action = "include",
    },
    /* entry 2 */
    /* ip related filter */
    {
        .name = "fcm_filter_1",
        .index = 3,
        .src_ip_len = 1,
        .src_ip[0] = "192.168.40.12",
        .dst_ip_len = 1,
        .dst_ip[0] = "10.2.20.121",
        .src_ip_op = "in",
        .dst_ip_op = "in",

        .action = "include",
    },
    /* entry 3 */
    {
        .name = "fcm_filter_1",
        .index = 4,
        .src_ip_len = 1,
        .src_ip[0] = "192.168.40.121",
        .dst_ip_len = 1,
        .dst_ip[0] = "10.2.20.32",
        .src_ip_op = "in",
        .dst_ip_op = "in",

        .action = "include",
    },
    /* entry 4 */
    {
        .name = "fcm_filter_1",
        .index = 5,
        .src_ip_len = 1,
        .src_ip[0] = "10.2.20.32",
        .dst_ip_len = 1,
        .dst_ip[0] = "192.168.40.12",
        .src_ip_op = "in",
        .dst_ip_op = "in",

        .action = "include",
    },
    /* entry 5 */
    {
        .name = "fcm_filter_app1",
        .index = 6,
        .src_ip_len = 1,
        .src_ip[0] = "157.240.22.35",
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

        .action = "include",
    },
    /* entry 6 */
    {
        .name = "fcm_filter_app2",
        .index = 7,
        .src_ip_len = 1,
        .src_ip[0] = "172.2.3.4",
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

        .action = "include",
    }
};

fcm_filter_l2_info_t g_flow_l2[] =
{
    {
        .src_mac = "11:22:33:44:55:66",
        .dst_mac = "A6:55:44:33:22:1A",
        .vlan_id = 123,
        .eth_type = 0x0800
    },
    {
        .src_mac = "A6:55:44:33:22:1A",
        .dst_mac = "11:22:33:44:55:66",
        .vlan_id = 123,
        .eth_type = 0x0800
    },
    {
        .src_mac = "11:22:33:44:55:77",
        .dst_mac = "A6:55:44:33:22:88",
        .vlan_id = 123,
        .eth_type = 0x0800
    },
};


fcm_filter_l3_info_t g_flow_l3[] =
{
    {
        .src_ip = "192.168.40.121",
        .dst_ip = "10.2.20.32",
        .sport = 67,
        .dport = 68,
        .l4_proto = 17,
        .ip_type = AF_INET
    },
    {
        .src_ip = "192.168.40.121",
        .dst_ip = "10.2.20.32",
        .sport = 1234,
        .dport = 80,
        .l4_proto = 6,
        .ip_type = AF_INET
    },
    {
        .src_ip = "192.168.40.121",
        .dst_ip = "10.2.20.32",
        .sport = 2123,
        .dport = 8855,
        .l4_proto = 17,
        .ip_type = AF_INET
    },
    {
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
        .pkt_cnt = 50,
        .bytes = 100
    },
    {
        .pkt_cnt = 10,
        .bytes = 1000
    },
    {
        .pkt_cnt = 30,
        .bytes = 100
    },
    {
        .pkt_cnt = 20,
        .bytes = 100
    },

    {
        .pkt_cnt = 40,
        .bytes = 1000
    },
    {
        .pkt_cnt = 60,
        .bytes = 3400
    },
    {
        .pkt_cnt = 70,
        .bytes = 67800
    },
    {
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
            free(tag->tags[j]);
        free(tag->tags);
        free(tag->app_name);
        free(tag->vendor);
        free(tag);
    }
    free(fkey->tags);
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
        key_tags = calloc(fkey->num_tags, sizeof(*key_tags));
        fkey->tags = key_tags;
    }
    else
    {
        key_tags = fkey->tags;
    }

    if (key_tags == NULL) return;

    tag = calloc(1, sizeof(*tag));
    if (tag == NULL) goto err_free_key_tags;

    tag->vendor = strdup("walleye");
    if (tag->vendor == NULL) return;

    tag->app_name = strdup(appname);
    if (tag->app_name == NULL) goto err_free_vendor;

    tag->nelems = 2;
    tag->tags = calloc(tag->nelems, sizeof(tag->tags));
    if (tag->tags == NULL) goto err_free_app_name;
    tag->tags[0] = strdup(sub_tag0);
    if (tag->tags[0] == NULL) goto err_free_tag_tags;

    tag->tags[1] = strdup(sub_tag1);
    if (tag->tags[1] == NULL) goto err_free_tag_tags_0;

    key_tags[j] = tag;

    j++;
    if (j == fkey->num_tags) j = 0;


    return;

err_free_tag_tags_0:
    free(tag->tags[0]);

err_free_tag_tags:
    for (i = 0; i < tag->nelems; i++) free(tag->tags[i]);
    free(tag->tags);

err_free_app_name:
    free(tag->app_name);

err_free_vendor:
    free(tag->vendor);

err_free_key_tags:
    free(key_tags);

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

void test_fcm_filter_add(void)
{
    struct schema_FCM_Filter *sch_filter;
    bool allow = false;
    fcm_filter_l2_info_t *l2_filter_info;
    fcm_filter_stats_t *l2_filter_pkts;

    /* Add a legit rules */
    sch_filter = &g_fcm_filter[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);
    /* checks with proper value that pass the rule */
    l2_filter_info = &g_flow_l2[0];
    l2_filter_pkts = &g_flow_pkt[0];
    fcm_filter_layer2_apply("fcm_filter_1",
                             l2_filter_info,
                             l2_filter_pkts,
                             &allow);
    TEST_ASSERT_TRUE(allow);
    /* checks with mac that is not present in rule */
    l2_filter_info = &g_flow_l2[2];
    l2_filter_pkts = &g_flow_pkt[2];
    fcm_filter_layer2_apply("fcm_filter_1",
                            l2_filter_info,
                            l2_filter_pkts,
                            &allow);
    TEST_ASSERT_FALSE(allow);

    /* checks packets size with lower value than the rule */
    l2_filter_info = &g_flow_l2[0];
    l2_filter_pkts = &g_flow_pkt[1];
    fcm_filter_layer2_apply("fcm_filter_1",
                            l2_filter_info,
                            l2_filter_pkts,
                            &allow);
    TEST_ASSERT_FALSE(allow);
}

void test_fcm_filter_delete(void)
{

    struct schema_FCM_Filter *sch_filter;
    bool allow = false;
    fcm_filter_l2_info_t *l2_filter_info;
    fcm_filter_stats_t *l2_filter_pkts;

    /* Add a legit rules */
    sch_filter = &g_fcm_filter[1];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    /* checks with proper value that pass the rule */
    l2_filter_info = &g_flow_l2[1];
    l2_filter_pkts = &g_flow_pkt[0];
    fcm_filter_layer2_apply("fcm_filter_1",
                            l2_filter_info,
                            l2_filter_pkts,
                            &allow);
    TEST_ASSERT_TRUE(allow);

    /* Remove the entries */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_FCM_Filter(&g_mon, sch_filter, NULL);

    /* check the same rule above */
    fcm_filter_layer2_apply("fcm_filter_1",
                            l2_filter_info,
                            l2_filter_pkts,
                            &allow);
    TEST_ASSERT_TRUE(allow);
}


void test_fcm_filter_update(void)
{

    struct schema_FCM_Filter *sch_filter;
    bool allow = false;
    fcm_filter_l2_info_t *l2_filter_info;
    fcm_filter_stats_t *l2_filter_pkts;

    /* Add a legit rules */
    sch_filter = &g_fcm_filter[0];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    /* checks with proper value that pass the rule */
    l2_filter_info = &g_flow_l2[0];
    l2_filter_pkts = &g_flow_pkt[0];
    fcm_filter_layer2_apply("fcm_filter_1",
                            l2_filter_info,
                            l2_filter_pkts,
                            &allow);
    TEST_ASSERT_TRUE(allow);
    /* Now update / modify the existing rule and check for fail*/

    struct schema_FCM_Filter fcm_filter_modify = {
        .name = "fcm_filter_1",
        .index = 1,
        .smac_len = 1,
        .smac[0] = "11:22:33:77:88:99",    /* change from 44:55:66 */
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

    /* same values should fail now due to rule modified */
    l2_filter_info = &g_flow_l2[1];
    l2_filter_pkts = &g_flow_pkt[0];
    fcm_filter_layer2_apply("fcm_filter_1",
                            l2_filter_info,
                            l2_filter_pkts,
                            &allow);
    TEST_ASSERT_FALSE(allow);

    fcm_filter_l2_info_t new_flow =
    {
        .src_mac = "11:22:33:77:88:99",  /* same mac as flow to check success*/
        .dst_mac = "A6:55:44:33:22:1A",
        .vlan_id = 123,
        .eth_type = 0x0800
    };

    /*filter should success now */
    l2_filter_info = &new_flow;
    l2_filter_pkts = &g_flow_pkt[0];
    fcm_filter_layer2_apply("fcm_filter_1",
                            l2_filter_info,
                            l2_filter_pkts,
                            &allow);
    TEST_ASSERT_TRUE(allow);
}


void test_fcm_filter_ip(void)
{
    struct schema_FCM_Filter *sch_filter;
    bool allow = false;
    fcm_filter_l3_info_t *l3_filter_info;
    fcm_filter_stats_t *l2_filter_pkts;

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


    /* ip filter test */

    /* checks with proper value that pass the rule */
    l3_filter_info = &g_flow_l3[0];
    l2_filter_pkts = &g_flow_pkt[0];

    fcm_filter_7tuple_apply("fcm_filter_1",
                            NULL,
                            l3_filter_info,
                            l2_filter_pkts,
                            NULL,
                            &allow);

    TEST_ASSERT_TRUE(allow);

    l3_filter_info = &g_flow_l3[1];
    l2_filter_pkts = &g_flow_pkt[0];

    fcm_filter_7tuple_apply("fcm_filter_1",
                            NULL,
                            l3_filter_info,
                            l2_filter_pkts,
                            NULL,
                            &allow);
    TEST_ASSERT_TRUE(allow);

    l3_filter_info = &g_flow_l3[2];
    l2_filter_pkts = &g_flow_pkt[0];

    fcm_filter_7tuple_apply("fcm_filter_1",
                            NULL,
                            l3_filter_info,
                            l2_filter_pkts,
                            NULL,
                            &allow);

}

void test_fcm_filter_app_add(void)
{
    struct schema_FCM_Filter *sch_filter;
    bool allow = false;

    /* Add app rules */
    sch_filter = &g_fcm_filter[5];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    fcm_filter_print();

    /* checks with proper value that pass the rule */
    fcm_filter_app_apply("fcm_filter_app1",
                          &g_fkey[0], &allow);
    TEST_ASSERT_TRUE(allow);

    /* checks with app that is not present in rule */
    fcm_filter_app_apply("fcm_filter_app1",
                          &g_fkey[1], &allow);
    TEST_ASSERT_FALSE(allow);
}

void test_fcm_filter_app_delete(void)
{

    struct schema_FCM_Filter *sch_filter;
    bool allow = false;

    /* Add app rules */
    sch_filter = &g_fcm_filter[5];
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    /* checks with proper value that pass the rule */
    fcm_filter_app_apply("fcm_filter_app1",
                          &g_fkey[0], &allow);
    TEST_ASSERT_TRUE(allow);

    /* Delete app rules */
    sch_filter = &g_fcm_filter[5];
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_FCM_Filter(&g_mon, sch_filter, NULL);

    /* Add exclude rules */
    sch_filter = &g_fcm_filter[5];
    STRSCPY(sch_filter->appname_op, "out");
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_FCM_Filter(&g_mon, NULL, sch_filter);

    /* check for app not present in rule */
    fcm_filter_app_apply("fcm_filter_app1",
                          &g_fkey[0], &allow);
    TEST_ASSERT_FALSE(allow);

    // Reset back the input to original.
    STRSCPY(sch_filter->appname_op, "in");
}

void test_fcm_filter_app_update(void)
{
    struct schema_FCM_Filter *sch_filter;
    bool allow = false;

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

    /* checks with proper value that pass the rule */
    fcm_filter_app_apply("fcm_filter_app1",
                          &g_fkey[0], &allow);
    TEST_ASSERT_TRUE(allow);

    /* Test appname_op_exists false */
    sch_filter = &g_fcm_filter[5];
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    fcm_filter_app_mod.appname_op_exists = false;
    callback_FCM_Filter(&g_mon, sch_filter, &fcm_filter_app_mod);

    /* check for app not present in rule */
    fcm_filter_app_apply("fcm_filter_app1",
                          &g_fkey[0], &allow);
    TEST_ASSERT_TRUE(allow);

    // Reset back the appname_op_exists.
    fcm_filter_app_mod.appname_op_exists = true;

    /* Update the fcm_filter with new modified rule which makes test fail */
    sch_filter = &g_fcm_filter[5];
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_FCM_Filter(&g_mon, sch_filter, &fcm_filter_app_mod);

    /* same values should fail now due to rule modified */
    fcm_filter_app_apply("fcm_filter_app1",
                          &g_fkey[0], &allow);
    TEST_ASSERT_FALSE(allow);

    /*filter should succeed now */
    fcm_filter_app_apply("fcm_filter_app1",
                          &g_fkey[1], &allow);
    TEST_ASSERT_TRUE(allow);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_INFO);

    UnityBegin(test_name);

    RUN_TEST(test_fcm_filter_init_deinit);
    RUN_TEST(test_fcm_filter_add);
    RUN_TEST(test_fcm_filter_delete);
    RUN_TEST(test_fcm_filter_update);
    RUN_TEST(test_fcm_filter_ip);
    // App filter tests.
    RUN_TEST(test_fcm_filter_app_add);
    RUN_TEST(test_fcm_filter_app_delete);
    RUN_TEST(test_fcm_filter_app_update);

    return UNITY_END();
}

