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

#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <ev.h>

#include "network_zone.h"
#include "network_zone_internals.h"
#include "log.h"
#include "memutil.h"
#include "target.h"
#include "unity.h"
#include "os_nif.h"
#include "policy_tags.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_utils.h"
#include "test_network_zone.h"
#include "unit_test_utils.h"

#define CMD_BUF_LEN 1024

static ovsdb_table_t table_Openflow_Tag;
static ovsdb_table_t table_Openflow_Tag_Group;



/**
 * @brief breaks the ev loop to terminate a test
 */
static void
nz_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    LOGI("%s: here", __func__);
    ev_break(EV_A_ EVBREAK_ONE);
    LOGI("%s: done", __func__);
}


static int
network_zone_ev_test_setup(double timeout)
{
    ev_timer *p_timeout_watcher;
    struct nz_test_mgr *mgr;

    mgr = nz_get_test_mgr();

    /* Set up the timer killing the ev loop, indicating the end of the test */
    p_timeout_watcher = &mgr->timeout_watcher;

    ev_timer_init(p_timeout_watcher, nz_timeout_cb, timeout, 0.);
    ev_timer_start(mgr->loop, p_timeout_watcher);

    return 0;
}


static void
network_zone_ut_walk_tags_tree(ds_tree_t *tree)
{
    om_tag_list_entry_t *item;

    if (tree == NULL)
    {
        LOGI("%s: tree is empty", __func__);
        return;
    }

    ds_tree_foreach(tree, item)
    {
        LOGI("%s: tag value %s",
             __func__, item->value);
    }
}


static bool
network_zone_ut_tag_update_cb(om_tag_t *tag,
                              struct ds_tree *removed,
                              struct ds_tree *added,
                              struct ds_tree *updated)
{
    LOGI("%s: tag name: %s, group: %s", __func__,
         tag->name, tag->group ? "yes" : "false");
    LOGI("%s: ***** removed:", __func__);
    network_zone_ut_walk_tags_tree(removed);
    LOGI("%s: ***** added:", __func__);
    network_zone_ut_walk_tags_tree(added);
    LOGI("%s: ***** updated:", __func__);
    network_zone_ut_walk_tags_tree(updated);
    LOGI("%s: ***** values:", __func__);
    network_zone_ut_walk_tags_tree(&tag->values);

    network_zone_tag_update_cb(tag, removed, added, updated);

    return true;
}


void
network_zone_ovsdb_monitor_tags(void)
{
    struct tag_mgr network_zone_ut_tagmgr;

    memset(&network_zone_ut_tagmgr, 0, sizeof(network_zone_ut_tagmgr));
    network_zone_ut_tagmgr.service_tag_update = network_zone_ut_tag_update_cb;
    om_tag_init(&network_zone_ut_tagmgr);

    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag_Group);

    om_standard_callback_openflow_tag(&table_Openflow_Tag);
    om_standard_callback_openflow_tag_group(&table_Openflow_Tag_Group);
}


static void
add_nz_cb(EV_P_ ev_timer *w, int revents)
{
    char cmd[CMD_BUF_LEN];
    int priority;
    char *oftag;
    char *name;
    char *macs;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    /* Create Openflow tags */
    name = "dev_foo_tag";
    oftag = "\'[\"set\",[\"66:55:44:33:22:11\",\"99:88:77:66:55:44\"]]\'";
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Openflow_Tag "
             "name:=%s "
             "device_value:=%s",
             name, oftag);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

    name = "dev_bar_tag_1";
    oftag = "\'[\"set\",[\"aa:bb:cc:dd:ee:11\",\"11:ee:dd:cc:bb:aa\"]]\'";
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Openflow_Tag "
             "name:=%s "
             "device_value:=%s",
             name, oftag);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

    name = "dev_bar_tag_2";
    oftag = "\'[\"set\",[\"bb:cc:dd:ee:11:22\",\"22:11:ee:dd:cc:bb\"]]\'";
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Openflow_Tag "
             "name:=%s "
             "device_value:=%s",
             name, oftag);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Different tag, same macs as dev_bar_tag_2 */
    name = "dev_doe_tag_1";
    oftag = "\'[\"set\",[\"bb:cc:dd:ee:11:22\",\"22:11:ee:dd:cc:bb\"]]\'";
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Openflow_Tag "
             "name:=%s "
             "device_value:=%s",
             name, oftag);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

    name = "dev_doe_tag_2";
    oftag = "\'[\"set\",[\"cc:dd:ee:11:22:33\",\"33:22:11:ee:dd:cc\"]]\'";
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Openflow_Tag "
             "name:=%s "
             "device_value:=%s",
             name, oftag);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

    name = "dev_ut_nz_1";
    priority = 1;
    macs = "\'[\"set\",[\"${dev_foo_tag}\",\"$[dev_bar_gtag]\",\"22:33:44:55:66:77\"]]\'";

    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Network_Zone "
             "name:=%s "
             "priority:=%d "
             "macs:=%s",
             name, priority, macs);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

    /* Add the group tag after the zone creation */
    name = "dev_doe_gtag";
    oftag = "\'[\"set\",[\"dev_doe_tag_1\"]]\'";
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Openflow_Tag_Group "
             "name:=%s "
             "tags:=%s",
             name, oftag);
    rc = cmd_log(cmd);

    sleep(1);

    TEST_ASSERT_EQUAL_INT(0, rc);
    name = "dev_ut_nz_2";
    priority = 2;
    macs = "\'[\"set\",[\"$[dev_doe_gtag]\",\"${dev_doe_tag_2}\"]]\'";

    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Network_Zone "
             "name:=%s "
             "priority:=%d "
             "macs:=%s",
             name, priority, macs);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

    /* Add the group tag after the zone creation */
    name = "dev_bar_gtag";
    oftag = "\'[\"set\",[\"dev_bar_tag_1\",\"dev_bar_tag_2\"]]\'";
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Openflow_Tag_Group "
             "name:=%s "
             "tags:=%s",
             name, oftag);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}

/* Macs unique to zone 1 */
struct nz_mac_formats to_check_nz_1[] =
{
    {
        .mac_str = "66:55:44:33:22:11",
    },
    {
        .mac_str = "99:88:77:66:55:44",
    },
    {
        .mac_str = "aa:bb:cc:dd:ee:11",
    },
    {
        .mac_str = "11:ee:dd:cc:bb:aa",
    },
    {
        .mac_str = "22:33:44:55:66:77",
    },
};

/* Macs common to zone 1 and 2 */
struct nz_mac_formats to_check_nz_2_1[] =
{
    {
        .mac_str = "bb:cc:dd:ee:11:22",
    },
    {
        .mac_str = "22:11:ee:dd:cc:bb",
    },
};


/* Macs unique to zone 2 */
struct nz_mac_formats to_check_nz_2_2[] =
{
    {
        .mac_str = "cc:dd:ee:11:22:33",
    },
    {
        .mac_str = "33:22:11:ee:dd:cc",
    },
};

/* Macs added to zone 2 through an update */
struct nz_mac_formats to_check_nz_2_3[] =
{
    {
        .mac_str = "99:99:99:99:99:99",
    },
};


void
nz_validate_mac_zones(struct nz_mac_formats to_check[], size_t nelems,
                      char *expected_zone)
{
    char *actual_zone;
    bool ret;
    size_t i;

    for (i = 0; i < nelems; i++)
    {
        ret = os_nif_macaddr_from_str(&to_check[i].mac_bin, to_check[i].mac_str);
        TEST_ASSERT_TRUE(ret);
        actual_zone = network_zone_get_zone(&to_check[i].mac_bin);
        LOGI("%s: mac getting checked: %s", __func__, to_check[i].mac_str);
        LOGI("%s: mac: %s, expected_zone: %s, actual zone: %s", __func__,
             to_check[i].mac_str,
             (expected_zone == NULL ? "None" : expected_zone),
             (actual_zone == NULL ? "None" : actual_zone));
        if (expected_zone != NULL)
        {
            TEST_ASSERT_NOT_NULL(actual_zone);
            TEST_ASSERT_EQUAL_STRING(expected_zone, actual_zone);
        }
        else
        {
            TEST_ASSERT_NULL(actual_zone);
        }
    }
}


static struct nz_ut_cleanup nz_ut_clean_all[] =
{
    {
        .table = "Openflow_Tag",
        .field = "name",
        .id = "dev_bar_tag_1",
    },
    {
        .table = "Openflow_Tag",
        .field = "name",
        .id = "dev_bar_tag_2",
    },
    {
        .table = "Openflow_Tag_Group",
        .field = "name",
        .id = "dev_bar_gtag",
    },
    {
        .table = "Network_Zone",
        .field = "name",
        .id = "dev_ut_nz_1",
    },
    {
        .table = "Openflow_Tag",
        .field = "name",
        .id = "dev_foo_tag",
    },
    {
        .table = "Network_Zone",
        .field = "name",
        .id = "dev_ut_nz_2",
    },
    {
        .table = "Openflow_Tag_Group",
        .field = "name",
        .id = "dev_doe_gtag",
    },
    {
        .table = "Openflow_Tag",
        .field = "name",
        .id = "dev_doe_tag_1",
    },
    {
        .table = "Openflow_Tag",
        .field = "name",
        .id = "dev_doe_tag_2",
    },
};


static void
nz_tests_clean_ovsdb_entries(struct nz_ut_cleanup array[], size_t nelems)
{
    struct nz_ut_cleanup *entry;
    struct nz_test_mgr *mgr;
    char cmd[CMD_BUF_LEN];
    size_t i;

    mgr = nz_get_test_mgr();
    if (mgr->has_ovsdb == false) return;

    for (i = 0; i < nelems; i++)
    {
        entry = &array[i];
        memset(cmd, 0 , sizeof(cmd));
        snprintf(cmd, sizeof(cmd),
                 "ovsh d %s "
                 "-w %s==%s ",
                 entry->table, entry->field, entry->id);
        cmd_log(cmd);
        sleep(1);
    }
}


void
nz_tests_clean_all_ovsdb_entries(void)
{
    nz_tests_clean_ovsdb_entries(nz_ut_clean_all, ARRAY_SIZE(nz_ut_clean_all));
}


static void
delete_all_nz_cb(EV_P_ ev_timer *w, int revents)
{
    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    nz_tests_clean_all_ovsdb_entries();

    LOGI("\n***** %s: done\n", __func__);
}

static void
update_nz_cb(EV_P_ ev_timer *w, int revents)
{
    struct nz_test_mgr *mgr;
    char cmd[CMD_BUF_LEN];
    int priority;
    char *name;
    char *macs;
    int rc;

    mgr = nz_get_test_mgr();
    if (mgr->has_ovsdb == false) return;

    name = "dev_ut_nz_1";
    priority = 3;

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh u Network_Zone "
             "-w name==%s "
             "priority:=%d ",
             name, priority);

    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

    name = "dev_ut_nz_2";
    macs = "\'[\"set\",[\"99:99:99:99:99:99\"]]\'";

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh u Network_Zone "
             "-w name==%s "
             "macs:ins:%s",
             name, macs);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

}


static struct nz_ut_cleanup nz_ut_clean_gtag[] =
{
    {
        .table = "Openflow_Tag_Group",
        .field = "name",
        .id = "dev_doe_gtag",
    },
};


static void
delete_one_nz_cb(EV_P_ ev_timer *w, int revents)
{
    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    nz_tests_clean_ovsdb_entries(nz_ut_clean_gtag, ARRAY_SIZE(nz_ut_clean_gtag));

    LOGI("\n***** %s: done\n", __func__);
}


static void
setup_network_zone_test_add(void)
{
    struct nz_test_mgr *mgr;
    struct test_timers *t;
    struct ev_loop *loop;

    mgr = nz_get_test_mgr();

    t = &mgr->network_zone_test_timers;
    loop = mgr->loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add,
                  add_nz_cb,
                  mgr->g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
}


static void
setup_network_zone_test_update(void)
{
    struct nz_test_mgr *mgr;
    struct test_timers *t;
    struct ev_loop *loop;

    mgr = nz_get_test_mgr();

    t = &mgr->network_zone_test_timers;
    loop = mgr->loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_update_add,
                  update_nz_cb,
                  mgr->g_timeout++, 0);
    t->timeout_watcher_update_add.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_update_add);
}


static void
setup_one_network_zone_test_delete(void)
{
    struct nz_test_mgr *mgr;
    struct test_timers *t;
    struct ev_loop *loop;

    mgr = nz_get_test_mgr();

    t = &mgr->network_zone_test_timers;
    loop = mgr->loop;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete,
                  delete_one_nz_cb,
                  mgr->g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_delete);
}



static void
setup_network_zone_test_delete_all(void)
{
    struct nz_test_mgr *mgr;
    struct test_timers *t;
    struct ev_loop *loop;

    mgr = nz_get_test_mgr();

    t = &mgr->network_zone_test_timers;
    loop = mgr->loop;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete,
                  delete_all_nz_cb,
                  mgr->g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_delete);
}


static void
test_events(void)
{
    struct nz_test_mgr *mgr;

    mgr = nz_get_test_mgr();
    if (mgr->has_ovsdb == false) return;

    setup_network_zone_test_add();

    /* Set overall test duration */
    network_zone_ev_test_setup(++mgr->g_timeout);

    /* Start the main loop */
    LOGI("%s: ****************  Calling ev_run for addition", __func__);
    ev_run(mgr->loop, 0);
    LOGI("%s: ****************  Done with the addition", __func__);

    /* The ev loop was broken by the timeout */
    /* Validate the zones */
    nz_validate_mac_zones(to_check_nz_1, ARRAY_SIZE(to_check_nz_1), "dev_ut_nz_1");
    nz_validate_mac_zones(to_check_nz_2_1, ARRAY_SIZE(to_check_nz_2_1), "dev_ut_nz_2");
    nz_validate_mac_zones(to_check_nz_2_1, ARRAY_SIZE(to_check_nz_2_2), "dev_ut_nz_2");

    /* Delete the gtag belonging to ut_nz_2 */
    mgr->g_timeout = 0;
    setup_one_network_zone_test_delete();

    /* Set overall test duration */
    network_zone_ev_test_setup(++mgr->g_timeout);

    /* Start the main loop */
    LOGI("%s: ****************  Calling ev_run for one gtag deletion", __func__);
    ev_run(mgr->loop, 0);
    LOGI("%s: ****************  Done with the gtag deletion", __func__);

    /* The ev loop was broken by the timeout */
    /* Validate the zones */
    nz_validate_mac_zones(to_check_nz_1, ARRAY_SIZE(to_check_nz_1), "dev_ut_nz_1");
    nz_validate_mac_zones(to_check_nz_2_1, ARRAY_SIZE(to_check_nz_2_1), "dev_ut_nz_1");
    nz_validate_mac_zones(to_check_nz_2_2, ARRAY_SIZE(to_check_nz_2_2), "dev_ut_nz_2");

    /* Update the network zones */
    mgr->g_timeout = 0;
    setup_network_zone_test_update();

    /* Set overall test duration */
    network_zone_ev_test_setup(++mgr->g_timeout);

    /* Start the main loop */
    LOGI("%s: ****************  Calling ev_run for network zones updates", __func__);
    ev_run(mgr->loop, 0);
    LOGI("%s: ****************  Done with network zones updates", __func__);

    /* The ev loop was broken by the timeout */
    /* Validate the zones */
    nz_validate_mac_zones(to_check_nz_1, ARRAY_SIZE(to_check_nz_1), "dev_ut_nz_1");
    nz_validate_mac_zones(to_check_nz_2_1, ARRAY_SIZE(to_check_nz_2_1), "dev_ut_nz_1");
    nz_validate_mac_zones(to_check_nz_2_2, ARRAY_SIZE(to_check_nz_2_2), "dev_ut_nz_2");
    nz_validate_mac_zones(to_check_nz_2_3, ARRAY_SIZE(to_check_nz_2_3), "dev_ut_nz_2");

    /* Delete all entries */
    mgr->g_timeout = 0;
    setup_network_zone_test_delete_all();

    /* Set overall test duration */
    network_zone_ev_test_setup(++mgr->g_timeout);

    /* Start the main loop */
    LOGI("%s: ****************  Calling ev_run for full deletion", __func__);
    ev_run(mgr->loop, 0);
    LOGI("%s: ****************  Done with the full deletion", __func__);

    /* Validate the zones */
    nz_validate_mac_zones(to_check_nz_1, ARRAY_SIZE(to_check_nz_1), NULL);
    nz_validate_mac_zones(to_check_nz_2_1, ARRAY_SIZE(to_check_nz_2_1), NULL);
    nz_validate_mac_zones(to_check_nz_2_2, ARRAY_SIZE(to_check_nz_2_2), NULL);
}


void
run_network_zone_ovsdb(void)
{
    struct nz_test_mgr *mgr;

    mgr = nz_get_test_mgr();

    if (mgr->has_ovsdb == false) return;
    RUN_TEST(test_events);
}
