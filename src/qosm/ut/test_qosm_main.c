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

#include "ovsdb_update.h"
#include "memutil.h"
#include "json_util.h"
#include "unity.h"
#include "target.h"
#include "policy_tags.h"
#include "log.h"
#include "util.h"

#include "qosm_filter.h"
#include "qosm_filter_internal.h"
#include "qosm_ip_iface.h"
#include "qosm_ic_template.h"
#include "qosm_interface_classifier.h"

extern void __test_qosm_filter_callback_IP_Interface(
    ovsdb_update_monitor_t *mon,
    struct schema_IP_Interface *old_rec,
    struct schema_IP_Interface *new_rec);

extern void __test_qosm_filter_callback_Interface_Classifier(
    ovsdb_update_monitor_t *mon,
    struct schema_Interface_Classifier *old_rec,
    struct schema_Interface_Classifier *new_rec);

extern void __test_qosm_filter_callback_Openflow_Tag(
    ovsdb_update_monitor_t *mon,
    struct schema_Openflow_Tag *old_rec,
    struct schema_Openflow_Tag *tag);

const char *test_name = "qosm_tests";
ovsdb_update_monitor_t g_mon;
static struct tag_mgr tag_mgr;

struct schema_Openflow_Tag g_tags[] =
{
    {
        .name_exists = true,
        .name = "tag_1",
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

void __ev_wait_dummy(EV_P_ ev_timer *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;
    TRACE();
}

bool ev_wait(int *trigger, double timeout)
{
    bool retval = false;

    /* the dnsmasq process is started with a short delay, sleep for few seconds before continuing */
    ev_timer ev;
    ev_timer_init(&ev, __ev_wait_dummy, timeout, 0.0);

    ev_timer_start(EV_DEFAULT, &ev);

    while (ev_run(EV_DEFAULT, EVRUN_ONCE))
    {
        if (!ev_is_active(&ev))
        {
            break;
        }

        if (trigger != NULL && *trigger != 0)
        {
            retval = true;
            break;
        }
    }

    if (ev_is_active(&ev)) ev_timer_stop(EV_DEFAULT, &ev);

    return retval;
}

void qosm_test_tc_commit(struct qosm_intf_classifier *ic, bool ingress)
{
    LOGT("%s(): interface classifier: %s match: '%s', direction: '%s'",
         __func__,
         ic->ic_token,
         ic->ic_match,
         (ingress == true ? "ingress" : "egress"));
}

void setUp(void)
{
    target_log_open("TEST", 0);
    memset(&g_mon, 0, sizeof(g_mon));
    log_severity_set(LOG_SEVERITY_TRACE);
    qosm_filter_init();

    memset(&tag_mgr, 0, sizeof(tag_mgr));
    tag_mgr.service_tag_update = qosm_ic_template_tag_update;
    om_tag_init(&tag_mgr);

    return;
}

void tearDown(void)
{
    return;
}


void
test_multiple_Interface_Classifier(void)
{
    TRACE();

    struct schema_IP_Interface ip_iface[] = {
        {
        ._uuid = { "uuid_ip_classifier1" },
        .if_name = "br-home",
        .name = "brhome",
        .ingress_classifier = {{"uuid_inclassifier"}, {"uuid_inclassifier2"}},
        .ingress_classifier_len = 2,
        .egress_classifier = {{"uuid_inclassifier"}},
        .egress_classifier_len = 1,
        },
        {
        ._uuid = { "uuid_ip_classifier2" },
        .if_name = "eth0",
        .name = "eth0",
        .ingress_classifier = {{"uuid_inclassifier2"}},
        .ingress_classifier_len = 1,
        }
    };

    struct schema_Interface_Classifier ic[] =
    {
        {
            ._uuid = {"uuid_inclassifier"},
            .token = "test1",
            .match = "arp u32 match u16 0002 000f at 6 action mirred egress mirror dev br-home.ndp",
            .match_exists = true,
            .action = "mirror",
            .action_exists = true,
        },
        {
            ._uuid = {"uuid_inclassifier2"},
            .token = "test2",
            .match = "dhcp u32 match u16 0002 000f at 6 action mirred egress mirror dev br-home.dhcp",
            .match_exists = true,
            .action = "mirror",
            .action_exists = true,
        }
    };

    /* configure interface classifier */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    g_mon.mon_uuid = ic[0]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, NULL, &ic[0]);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    g_mon.mon_uuid = ic[1]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, NULL, &ic[1]);

    g_mon.mon_uuid = ip_iface[0]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, NULL, &ip_iface[0]);

    ev_wait(NULL, 10.0);

    /* Delete interface */

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ip_iface[0]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, &ip_iface[0], &ip_iface[0]);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ic[0]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, &ic[0], &ic[0]);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ic[1]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, &ic[1], &ic[1]);

}

static int
get_classifier_tree_counts(struct qosm_ip_iface *ipi, bool ingress)
{
    struct intf_classifier_entry *ic;
    int count = 0;

    ic = ds_tree_head(&ipi->ipi_intf_classifier_tree);
    while (ic != NULL)
    {
        if (ingress == ic->ingress) count++;
        ic = ds_tree_next(&ipi->ipi_intf_classifier_tree, ic);
    }

    return count;
}

void
test_IP_Interface_changes(void)
{
    struct qosm_ip_iface *test_ipi;
    struct qosm_intf_classifier *ictest;
    struct qosm_filter *qosm_filter;
    int count;
    struct schema_IP_Interface ip_iface[] = {
        {
        ._uuid = { "uuid_ip_classifier1" },
        .if_name = "br-home",
        .name = "brhome",
        .ingress_classifier = {{"uuid_inclassifier"}},
        .ingress_classifier_len = 1,
        },
        {
        ._uuid = { "uuid_ip_classifier2" },
        .if_name = "eth0",
        .name = "eth0",
        .ingress_classifier = {{"uuid_inclassifier2"}},
        .ingress_classifier_len = 1,
        }
    };

    struct schema_Interface_Classifier ic[] =
    {
        {
            ._uuid = {"uuid_inclassifier"},
            .token = "test1",
            .match = "arp u32 match u16 0002 000f at 6 action mirred egress mirror dev br-home.ndp",
            .match_exists = true
        },
        {
            ._uuid = {"uuid_inclassifier2"},
            .token = "test2",
            .match = "dhcp u32 match u16 0002 000f at 6 action mirred egress mirror dev br-home.dhcp",
            .match_exists = true
        }
    };

    qosm_filter = qosm_filter_get();

    /* configure interface classifier */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    g_mon.mon_uuid = ic[0]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, NULL, &ic[0]);
    ictest = ds_tree_find(&qosm_filter->qosm_intf_classifier_tree, (void *)ic[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(ictest);
    TEST_ASSERT_EQUAL_UINT64(1, ictest->ic_reflink.rl_refcount);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    g_mon.mon_uuid = ic[1]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, NULL, &ic[1]);
    ictest = ds_tree_find(&qosm_filter->qosm_intf_classifier_tree, (void *)ic[1]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(ictest);
    TEST_ASSERT_EQUAL_UINT64(1, ictest->ic_reflink.rl_refcount);

    g_mon.mon_uuid = ip_iface[0]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, NULL, &ip_iface[0]);
    test_ipi = ds_tree_find(&qosm_filter->qosm_ip_iface_tree, ip_iface[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(test_ipi);

    g_mon.mon_uuid = ip_iface[1]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, NULL, &ip_iface[1]);
    test_ipi = ds_tree_find(&qosm_filter->qosm_ip_iface_tree, ip_iface[1]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(test_ipi);

    ictest = ds_tree_find(&qosm_filter->qosm_intf_classifier_tree, &ip_iface[1].ingress_classifier[0]);
    TEST_ASSERT_NOT_NULL(ictest);
    /* count should be 1 as only 1 ingress classifier is configured */
    count = get_classifier_tree_counts(test_ipi, true);
    TEST_ASSERT_EQUAL_INT32(1, count);
    /* no egress classifiers are configured */
    count = get_classifier_tree_counts(test_ipi, false);
    TEST_ASSERT_EQUAL_INT32(0, count);

    ev_wait(NULL, 10.0);

    /* Delete interface */
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ip_iface[0]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, &ip_iface[0], &ip_iface[0]);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ip_iface[1]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, &ip_iface[1], &ip_iface[1]);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ic[0]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, &ic[0], NULL);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ic[1]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, &ic[1], NULL);
}

void
test_IP_Interface_multiple_ic(void)
{
    struct qosm_intf_classifier *ictest;
    struct qosm_ip_iface *ipitest;
    struct qosm_filter *qosm_filter;
    struct schema_IP_Interface ip_iface[] = {
        {
        ._uuid = { "uuid_ip_classifier1" },
        .if_name = "br-home",
        .name = "brhome",
        .ingress_classifier = {{"uuid_inclassifier"}, {"uuid_inclassifier2"}},
        .ingress_classifier_len = 2,
        .egress_classifier = {{"uuid_inclassifier"}},
        .egress_classifier_len = 1,
        },
        {
        ._uuid = { "uuid_ip_classifier2" },
        .if_name = "eth0",
        .name = "eth0",
        .ingress_classifier = {{"uuid_inclassifier2"}},
        .ingress_classifier_len = 1,
        }
    };

    struct schema_Interface_Classifier ic[] =
    {
        {
            ._uuid = {"uuid_inclassifier"},
            .token = "test1",
            .match = "arp u32 match u16 0002 000f at 6 action mirred egress mirror dev br-home.ndp",
            .match_exists = true
        },
        {
            ._uuid = {"uuid_inclassifier2"},
            .token = "test2",
            .match = "dhcp u32 match u16 0002 000f at 6 action mirred egress mirror dev br-home.dhcp",
            .match_exists = true
        }
    };

    qosm_filter = qosm_filter_get();
    /* configure interface classifier */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    g_mon.mon_uuid = ic[0]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, NULL, &ic[0]);
    ictest = ds_tree_find(&qosm_filter->qosm_intf_classifier_tree, (void *)ic[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(ictest);
    TEST_ASSERT_EQUAL_UINT64(1, ictest->ic_reflink.rl_refcount);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    g_mon.mon_uuid = ic[1]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, NULL, &ic[1]);
    ictest = ds_tree_find(&qosm_filter->qosm_intf_classifier_tree, (void *)ic[1]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(ictest);
    TEST_ASSERT_EQUAL_UINT64(1, ictest->ic_reflink.rl_refcount);

    g_mon.mon_uuid = ip_iface[0]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, NULL, &ip_iface[0]);
    ipitest = ds_tree_find(&qosm_filter->qosm_ip_iface_tree, (void *)ip_iface[0]._uuid.uuid);
    TEST_ASSERT_NOT_NULL(ipitest);


    g_mon.mon_uuid = ip_iface[1]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, NULL, &ip_iface[1]);

    ev_wait(NULL, 10.0);

    /* Delete interface */

    LOGT("%s(): deleting IP interface", __func__ );
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ip_iface[0]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, &ip_iface[0], &ip_iface[0]);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ip_iface[1]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, &ip_iface[1], &ip_iface[1]);

    LOGT("%s(): deleting interface classifier", __func__ );
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ic[0]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, &ic[0], NULL);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ic[1]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, &ic[1], NULL);

}

void
update_tag(void)
{
    struct schema_Openflow_Tag ovsdb_tags =
    {
        .name_exists = true,
        .name = "tag_1",
        .device_value_len = 3,
        .device_value =
        {
            "11:11:11:11:11:11",
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
    };

    om_tag_update_from_schema(&ovsdb_tags);
}

void
test_template_rule(void)
{
    LOGT("%s(): starting test...", __func__);

    struct schema_IP_Interface ip_iface[] = {
        {
        ._uuid = { "uuid_ip_classifier1" },
        .if_name = "br-home",
        .name = "brhome",
        .ingress_classifier = {{"uuid_inclassifier"}},
        .ingress_classifier_len = 1,
        },
        {
        ._uuid = { "uuid_ip_classifier2" },
        .if_name = "eth0",
        .name = "eth0",
        .ingress_classifier = {{"uuid_inclassifier2"}},
        .ingress_classifier_len = 1,
        }
    };

    struct schema_Interface_Classifier ic[] =
    {
        {
            ._uuid = {"uuid_inclassifier"},
            .token = "test1",
            .match = "arp u32 match u16 0002 000f at 6 action mirred egress mirror dev ${@tag_1}",
            .match_exists = true,
            .action = "mirror",
            .action_exists = true,
        },
        {
            ._uuid = {"uuid_inclassifier2"},
            .token = "test2",
            .match = "dhcp u32 match u16 0002 000f at 6 action mirred egress mirror dev br-home.dhcp",
            .match_exists = true,
            .action = "redirect",
            .action_exists = true,
        }
    };

    /* configure Openflow Tags */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    __test_qosm_filter_callback_Openflow_Tag(&g_mon, NULL, &g_tags[0]);

    /* configure interface classifier */
    g_mon.mon_type = OVSDB_UPDATE_NEW;
    g_mon.mon_uuid = ic[0]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, NULL, &ic[0]);

    g_mon.mon_type = OVSDB_UPDATE_NEW;
    g_mon.mon_uuid = ic[1]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, NULL, &ic[1]);

    g_mon.mon_uuid = ip_iface[0]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, NULL, &ip_iface[0]);

    g_mon.mon_uuid = ip_iface[1]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, NULL, &ip_iface[1]);

    ev_wait(NULL, 10.0);

    update_tag();

    ev_wait(NULL, 5);

    LOGT("%s(): deleting IP interface", __func__ );
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ip_iface[0]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, &ip_iface[0], &ip_iface[0]);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ip_iface[1]._uuid.uuid;
    __test_qosm_filter_callback_IP_Interface(&g_mon, &ip_iface[1], &ip_iface[1]);

    LOGT("%s(): deleting interface classifier", __func__ );
    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ic[0]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, &ic[0], &ic[0]);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    g_mon.mon_uuid = ic[1]._uuid.uuid;
    __test_qosm_filter_callback_Interface_Classifier(&g_mon, &ic[1], &ic[1]);

    g_mon.mon_type = OVSDB_UPDATE_DEL;
    __test_qosm_filter_callback_Openflow_Tag(&g_mon, &g_tags[0], &g_tags[0]);

}

void
run_qosm_tests(void)
{
    RUN_TEST(test_IP_Interface_changes);
    RUN_TEST(test_IP_Interface_multiple_ic);
    RUN_TEST(test_multiple_Interface_Classifier);
    RUN_TEST(test_template_rule);
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    run_qosm_tests();

    return UNITY_END();
}
