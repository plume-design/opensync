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

#include "memutil.h"
#include "nfm_osfw.h"
#include "nfm_ovsdb.h"
#include "osn_fw_pri.h"
#include "ovsdb_update.h"
#include "policy_tags.h"
#include "nfm_rule.h"
#include "unity.h"
#include "target.h"
#include "log.h"

extern struct nfm_osfw_base nfm_osfw_base;
extern struct nfm_osfw_eb_base nfm_osfw_eb_base;

const char *test_name = "nfm_tests";
ovsdb_update_monitor_t g_mon;

static void
test_nfm_osfw_on_reschedule(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    return;
}

static void
test_nfm_osfw_eb_on_reschedule(struct ev_loop *loop, ev_timer *watcher, int revents)
{
    return;
}

void
nfm_dump_file(char *filename)
{
    FILE *fptr;
    signed char c;

    fptr = fopen(filename, "r");
    if (fptr == NULL)
    {
        LOGT("%s(): cannot open file %s ", __func__, filename);
        return;
    }

    c = fgetc(fptr);
    while (c != EOF)
    {
        printf("%c", c);
        c = fgetc(fptr);
    }

    fclose(fptr);
    return;
}

bool
test_osfw_init(struct ev_loop *loop)
{
    bool errcode = true;

    memset(&nfm_osfw_base, 0, sizeof(nfm_osfw_base));
    nfm_osfw_base.loop = loop;
    ev_timer_init(&nfm_osfw_base.timer, test_nfm_osfw_on_reschedule, 0, 0);

    memset(&nfm_osfw_eb_base, 0, sizeof(nfm_osfw_eb_base));
    nfm_osfw_eb_base.loop = loop;
    ev_timer_init(&nfm_osfw_eb_base.timer, test_nfm_osfw_eb_on_reschedule, 0, 0);

    errcode = osfw_eb_init(NULL);
    if (!errcode)
    {
        LOGE("osfw_eb_init failed");
        return false;
    }

    osfw_init(NULL);

    return true;
}

void
setUp(void)
{
    TRACE();
    struct ev_loop *loop;
    memset(&g_mon, 0, sizeof(g_mon));
    log_severity_set(LOG_SEVERITY_TRACE);
    loop = ev_default_loop(0);

    test_osfw_init(loop);
    return;
}

void
tearDown(void)
{
    TRACE();
    osfw_eb_fini();
    TRACE();
    return;
}

void
test_ebtables_rule_add(void)
{
    struct schema_Netfilter nf_schema[] = {
        {
        .name = "broute.brouting",
        .enable = true,
        .chain = "BROUTING",
        .priority = 1,
        .protocol = "eth",
        .rule = "-i home-ap-u50",
        .status = "enabled",
        .table = "broute",
        .target = "ACCEPT"
        },
        {
        .name = "filter.forward",
        .enable = true,
        .chain = "FORWARD",
        .priority = 100,
        .protocol = "eth",
        .rule = "-p ARP",
        .status = "enabled",
        .table = "filter",
        .target = "ACCEPT"
        }
    };

    bool result;
    TRACE();

    result = nfm_osfw_add_rule(&nf_schema[0]);
    TEST_ASSERT_TRUE(result);
    osfw_eb_apply();
    nfm_osfw_del_rule(&nf_schema[0]);

    /* filter table, Forward chain */
    LOGT("%s(): filter table, forward chain ", __func__ );
    result = nfm_osfw_add_rule(&nf_schema[1]);
    TEST_ASSERT_TRUE(result);
    osfw_eb_apply();
    nfm_osfw_del_rule(&nf_schema[1]);
}

void
test_ebtables_chain_add(void)
{
    struct schema_Netfilter netfilter[] =
    {
        {
        .name = "dpi.nfqueue_ipv4",
        .chain = "DPI_NFQUEUE",
        .enable = true,
        .priority = 1,
        .protocol = "ipv4",
        .rule = "-m connmark --mark 0x1 --queue-num 0 --queue-bypass",
        .status = "enabled",
        .table = "mangle",
        .target = "NFQUEUE"
        },
        {
        .name = "dpi.nfqueue_ipv4",
        .chain = "DPI_NFQUEUE",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-m connmark --mark 0x1 --queue-num 0 --queue-bypass",
        .status = "enabled",
        .table = "filter",
        .target = "NFQUEUE"
        },
        {
        .name = "rule_3",
        .enable = true,
        .chain = "FORWARD",
        .priority = 100,
        .protocol = "eth",
        .rule = "-p ARP",
        .status = "enabled",
        .table = "filter",
        .target = "ACCEPT"
        }
    };

    TRACE();
    struct nfm_rule *self;

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[1]);
    self = nfm_rule_get(netfilter[1].name);
    TEST_ASSERT_NOT_NULL(self);
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Netfilter(&g_mon, &netfilter[1], NULL);

    osfw_eb_apply();

    LOGT("%s(): adding rule 2", __func__ );
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[2]);
    LOGT("%s(): deleting rule 2", __func__ );
    self = nfm_rule_get(netfilter[2].name);
    TEST_ASSERT_NOT_NULL(self);
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Netfilter(&g_mon, &netfilter[2], NULL);
}

void
test_ebtables_nat_table(void)
{
    struct schema_Netfilter netfilter[] =
    {
        { /* rule 0 */
        .name = "nat.prerouting",
        .chain = "PREROUTING",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-p arp",
        .status = "enabled",
        .table = "nat",
        .target = "ACCEPT"
        },
        { /* rule 1 */
        .name = "nat.output",
        .chain = "OUTPUT",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-p arp",
        .status = "enabled",
        .table = "nat",
        .target = "DROP"
        },
        { /* rule 2 */
        .name = "nat.POSTROUTING",
        .chain = "POSTROUTING",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-p arp",
        .status = "enabled",
        .table = "nat",
        .target = "DROP"
        },
        { /* rule 3 */
        .name = "nat.tag",
        .chain = "OUTPUT",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-s ${all_gateways}",
        .status = "enabled",
        .table = "nat",
        .target = "DROP"
        }
    };

    struct schema_Netfilter modify_nf = {
        .name = "nat.POSTROUTING",
        .chain = "POSTROUTING",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-p tcp",
        .status = "enabled",
        .rule_changed = true,
        .table = "nat",
        .target = "ACCEPT"
    };

    struct nfm_rule *self;

    /* table NAT, chain: PREROUTING */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[0]);
    self = nfm_rule_get(netfilter[0].name);
    TEST_ASSERT_NOT_NULL(self);

    /* table NAT, chain: OUTPUT */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[1]);
    self = nfm_rule_get(netfilter[1].name);
    TEST_ASSERT_NOT_NULL(self);

    /* table NAT, chain: POSTROUTING */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[2]);
    self = nfm_rule_get(netfilter[2].name);
    TEST_ASSERT_NOT_NULL(self);

    LOGT("%s(): table NAT, chain: POSTROUTING update", __func__ );
    /* table NAT, chain: POSTROUTING update*/
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_Netfilter(&g_mon, &netfilter[2], &modify_nf);
    self = nfm_rule_get(netfilter[2].name);
    TEST_ASSERT_NOT_NULL(self);

    /* Delete all the rules. */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Netfilter(&g_mon, &netfilter[0], NULL);
    self = nfm_rule_get(netfilter[0].name);
    TEST_ASSERT_NULL(self);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Netfilter(&g_mon, &netfilter[1], NULL);
    self = nfm_rule_get(netfilter[1].name);
    TEST_ASSERT_NULL(self);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Netfilter(&g_mon, &netfilter[2], NULL);
    self = nfm_rule_get(netfilter[2].name);
    TEST_ASSERT_NULL(self);
}

void
test_ebtables_filter_table(void)
{
    struct schema_Netfilter netfilter[] =
    {
        { /* rule 0 */
        .name = "filter.forward",
        .chain = "FORWARD",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-p arp",
        .status = "enabled",
        .table = "filter",
        .target = "ACCEPT"
        },
        { /* rule 1 */
        .name = "filter.input",
        .chain = "INPUT",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-p arp",
        .status = "enabled",
        .table = "filter",
        .target = "DROP"
        },
        { /* rule 2 */
        .name = "filter.output",
        .chain = "OUTPUT",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-p arp",
        .status = "enabled",
        .table = "filter",
        .target = "DROP"
        }
    };

    struct nfm_rule *self;

    /* table filter, chain: FORWARD */
    LOGT("%s(): configuring filter table with FORWARD chain", __func__);
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[0]);
    self = nfm_rule_get(netfilter[0].name);
    TEST_ASSERT_NOT_NULL(self);

    /* table filter, chain: INPUT */
    LOGT("%s(): configuring filter table with INPUT chain", __func__ );
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[1]);
    self = nfm_rule_get(netfilter[1].name);
    TEST_ASSERT_NOT_NULL(self);

    /* table filter, chain: OUTPUT */
    LOGT("%s(): configuring filter table with OUTPUT chain", __func__ );
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[2]);
    self = nfm_rule_get(netfilter[2].name);
    TEST_ASSERT_NOT_NULL(self);

    /* Delete all the rules. */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Netfilter(&g_mon, &netfilter[0], NULL);
    self = nfm_rule_get(netfilter[0].name);
    TEST_ASSERT_NULL(self);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Netfilter(&g_mon, &netfilter[1], NULL);
    self = nfm_rule_get(netfilter[1].name);
    TEST_ASSERT_NULL(self);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Netfilter(&g_mon, &netfilter[2], NULL);
    self = nfm_rule_get(netfilter[2].name);
    TEST_ASSERT_NULL(self);
}

void
test_ebtables_broute_table(void)
{
    struct schema_Netfilter netfilter[] =
    {
        { /* rule 0 */
        .name = "broute.brouting",
        .chain = "BROUTING",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-p tcp",
        .status = "enabled",
        .table = "broute",
        .target = "ACCEPT"
        }
    };

    struct schema_Netfilter modify_nf = {
        .name = "broute.brouting",
        .chain = "BROUTING",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-p ip",
        .status = "enabled",
        .rule_changed = true,
        .table = "broute",
        .target = "ACCEPT"
    };

    struct nfm_rule *self;

    /* table broute, chain: BROUTING */
    LOGT("%s(): configuring broute table with BROUTING chain", __func__);
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[0]);
    self = nfm_rule_get(netfilter[0].name);
    TEST_ASSERT_NOT_NULL(self);

    LOGT("%s(): modifying broute table, chain: BROUTING", __func__ );
    /* table broute, chain: BROUTING update*/
    g_mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_Netfilter(&g_mon, &netfilter[0], &modify_nf);
    self = nfm_rule_get(netfilter[0].name);
    TEST_ASSERT_NOT_NULL(self);

    /* Delete all the rules. */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Netfilter(&g_mon, &netfilter[0], NULL);
    self = nfm_rule_get(netfilter[0].name);
    TEST_ASSERT_NULL(self);
}

void
test_ebtables_with_tag(void)
{
    int ret;
    struct schema_Openflow_Tag hp_tags[] =
    {
        {
            .name_exists = true,
            .name = "home-1",
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
    };

    struct schema_Netfilter netfilter[] =
    {
        { /* rule 0 */
        .name = "dev_sys_home-ap-24",
        .chain = "BROUTING",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-i home-ap-24",
        .status = "enabled",
        .table = "broute",
        .target = "homepass"
        },
        {
        .name = "dev_hp_dest_shared",
        .chain = "homepass",
        .enable = true,
        .priority = 1,
        .protocol = "eth",
        .rule = "-d ${home-1}",
        .status = "enabled",
        .table = "broute",
        .target = "guest_dst_100"
        }
    };

    /* configure openflow tags */
    ret = om_tag_add_from_schema(&hp_tags[0]);
    TEST_ASSERT_TRUE(ret);

    struct nfm_rule *self;

    /* table broute, chain: BROUTING */
    LOGT("%s(): configuring broute table with BROUTING chain", __func__);
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[0]);
    self = nfm_rule_get(netfilter[0].name);
    TEST_ASSERT_NOT_NULL(self);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    callback_Netfilter(&g_mon, NULL, &netfilter[1]);

    /* Delete the added rules */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    callback_Netfilter(&g_mon, &netfilter[0], NULL);
    callback_Netfilter(&g_mon, &netfilter[1], NULL);
    om_tag_remove_from_schema(&hp_tags[0]);
}

void
run_nfm_tests(void)
{
    RUN_TEST(test_ebtables_chain_add);
    RUN_TEST(test_ebtables_rule_add);
    RUN_TEST(test_ebtables_nat_table);
    RUN_TEST(test_ebtables_filter_table);
    RUN_TEST(test_ebtables_broute_table);
    RUN_TEST(test_ebtables_with_tag);
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    run_nfm_tests();

    return UNITY_END();
}

