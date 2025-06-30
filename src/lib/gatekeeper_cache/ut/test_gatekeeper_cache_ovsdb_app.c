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

#include "gatekeeper_cache.h"
#include "log.h"
#include "memutil.h"
#include "sockaddr_storage.h"
#include "unity.h"
#include "os.h"
#include "os_nif.h"

#include "test_gatekeeper_cache.h"
#include "gatekeeper_cache_internals.h"
#include "unit_test_utils.h"
#include "unity.h"

#define CMD_BUF_LEN 1024

struct gk_attr_cache_interface **entry;
size_t num_attr_entries = 3;

static void
create_default_attr_entries(void)
{
    bool ret;

    gk_cache_cleanup();

    entry = CALLOC(num_attr_entries, sizeof(*entry));

    entry[0] = CALLOC(1, sizeof(*entry[0]));
    entry[0]->device_mac = str2os_mac("AA:AA:AA:AA:AA:04");
    entry[0]->attribute_type = GK_CACHE_REQ_TYPE_APP;
    entry[0]->cache_ttl = 1000;
    entry[0]->action = FSM_BLOCK;
    entry[0]->attr_name = STRDUP("testapp_0");
    entry[0]->gk_policy = "GK_POLICY";
    ret = gkc_add_attribute_entry(entry[0]);
    TEST_ASSERT_TRUE(ret);

    entry[1] = CALLOC(1, sizeof(*entry[1]));
    entry[1]->action = 1;
    entry[1]->device_mac = str2os_mac("AA:AA:AA:AA:AA:04");
    entry[1]->attribute_type = GK_CACHE_REQ_TYPE_APP;
    entry[1]->cache_ttl = 1000;
    entry[1]->action = FSM_BLOCK;
    entry[1]->attr_name = STRDUP("testapp_1");
    entry[1]->gk_policy = "GK_POLICY";
    ret = gkc_add_attribute_entry(entry[1]);
    TEST_ASSERT_TRUE(ret);

    entry[2] = CALLOC(1, sizeof(*entry[2]));
    entry[2]->action = 1;
    entry[2]->device_mac = str2os_mac("AA:AA:AA:AA:AA:40");
    entry[2]->attribute_type = GK_CACHE_REQ_TYPE_APP;
    entry[2]->cache_ttl = 1000;
    entry[2]->action = FSM_BLOCK;
    entry[2]->attr_name = STRDUP("testapp_1");
    entry[2]->gk_policy = "GK_POLICY";
    ret = gkc_add_attribute_entry(entry[2]);
    TEST_ASSERT_TRUE(ret);
}


static void
delete_default_attr_entries(void)
{
    size_t i;

    for (i = 0; i < num_attr_entries; i++)
    {
        FREE(entry[i]->device_mac);
        FREE(entry[i]->attr_name);
        FREE(entry[i]);
    }
    FREE(entry);
}


/**
 * @brief breaks the ev loop to terminate a test
 */
static void
gkc_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    LOGI("%s: here", __func__);
    ev_break(EV_A_ EVBREAK_ONE);
    LOGI("%s: done", __func__);
}


static int
gkc_ev_test_setup(double timeout)
{
    ev_timer *p_timeout_watcher;
    struct gkc_test_mgr *mgr;

    mgr = gkc_get_test_mgr();

    /* Set up the timer killing the ev loop, indicating the end of the test */
    p_timeout_watcher = &mgr->timeout_watcher;

    ev_timer_init(p_timeout_watcher, gkc_timeout_cb, timeout, 0.);
    ev_timer_start(mgr->loop, p_timeout_watcher);

    return 0;
}

static
struct gkc_ut_cleanup gkc_ut_clean_all[] =
{
    {
        .table = "FSM_Policy",
        .field = "policy",
        .id = "ut_gkc_flush_app_1",
    },
    {
        .table = "FSM_Policy",
        .field = "policy",
        .id = "ut_gkc_flush_app_2",
    },
};


static void
gkc_tests_clean_ovsdb_entries(struct gkc_ut_cleanup array[], size_t nelems)
{
    struct gkc_ut_cleanup *entry;
    struct gkc_test_mgr *mgr;
    char cmd[CMD_BUF_LEN];
    size_t i;

    mgr = gkc_get_test_mgr();
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


static void
gkc_tests_clean_all_ovsdb_entries(void)
{
    gkc_tests_clean_ovsdb_entries(gkc_ut_clean_all, ARRAY_SIZE(gkc_ut_clean_all));
}


static void
add_flush_cache_one_entry_cb(EV_P_ ev_timer *w, int revents)
{
    char cmd[CMD_BUF_LEN];
    int rc;

    MEMZERO(cmd);
    snprintf(cmd, sizeof(cmd),
             "ovsh i FSM_Policy "
             "policy:=ut_gkc_flush_app_1 "
             "idx:=0 "
             "log:=all "
             "macs:=AA:AA:AA:AA:AA:04 "
             "mac_op:=in "
             "app_op:=out "
             "action:=flush "
             "name:=ut_gkc_flush_app_1_rule_1 ");
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);
    sleep(1);
}


static void
setup_gkc_test_flush_one_entry_add(void)
{
    struct gkc_test_mgr *mgr;
    struct test_timers *t;
    struct ev_loop *loop;

    mgr = gkc_get_test_mgr();

    t = &mgr->gkc_test_timers;
    loop = mgr->loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add,
                  add_flush_cache_one_entry_cb,
                  mgr->g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
}


static void
test_events_flush_one_entry(void)
{
    struct per_device_cache *dcache;
    struct gkc_test_mgr *mgr;
    ds_tree_t *app_tree;
    os_macaddr_t mac;
    bool ret;

    MEMZERO(mac);

    mgr = gkc_get_test_mgr();
    if (mgr->has_ovsdb == false) return;

    create_default_attr_entries();
    gkc_print_cache_entries();

    setup_gkc_test_flush_one_entry_add();

    /* Set overall test duration */
    gkc_ev_test_setup(++mgr->g_timeout);

    /* Start the main loop */
    LOGI("%s: ****************  Calling ev_run for addition", __func__);
    ev_run(mgr->loop, 0);
    LOGI("%s: ****************  Done with the addition", __func__);

    /* The ev loop was broken by the timeout */

    gkc_print_cache_entries();
    /* Validate the cache entries */
    ret = os_nif_macaddr_from_str(&mac, "AA:AA:AA:AA:AA:04");
    TEST_ASSERT_TRUE(ret);
    dcache = gkc_lookup_device_tree(&mac);
    app_tree = &dcache->app_tree;
    TEST_ASSERT_TRUE(ds_tree_is_empty(app_tree));
    TEST_ASSERT_NOT_NULL(dcache);

    ret = os_nif_macaddr_from_str(&mac, "AA:AA:AA:AA:AA:40");
    TEST_ASSERT_TRUE(ret);
    dcache = gkc_lookup_device_tree(&mac);
    app_tree = &dcache->app_tree;
    TEST_ASSERT_FALSE(ds_tree_is_empty(app_tree));
    TEST_ASSERT_NOT_NULL(dcache);

    delete_default_attr_entries();
}

static void
add_flush_cache_all_entries_cb(EV_P_ ev_timer *w, int revents)
{
    char cmd[CMD_BUF_LEN];
    int rc;

    MEMZERO(cmd);
    snprintf(cmd, sizeof(cmd),
             "ovsh i FSM_Policy "
             "policy:=ut_gkc_flush_app_2 "
             "idx:=0 "
             "log:=all "
             "app_op:=out "
             "action:=flush "
             "name:=ut_gkc_flush_all_apps_rule_1 ");
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);
    sleep(1);
}


static void
setup_gkc_test_flush_all_entries_add(void)
{
    struct gkc_test_mgr *mgr;
    struct test_timers *t;
    struct ev_loop *loop;

    mgr = gkc_get_test_mgr();

    t = &mgr->gkc_test_timers;
    loop = mgr->loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add,
                  add_flush_cache_all_entries_cb,
                  mgr->g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
}


static void
test_events_flush_all_entries(void)
{
    struct per_device_cache *dcache;
    struct gkc_test_mgr *mgr;
    ds_tree_t *app_tree;
    os_macaddr_t mac;
    bool ret;

    MEMZERO(mac);

    mgr = gkc_get_test_mgr();
    if (mgr->has_ovsdb == false) return;

    create_default_attr_entries();
    gkc_print_cache_entries();

    setup_gkc_test_flush_all_entries_add();

    /* Set overall test duration */
    gkc_ev_test_setup(++mgr->g_timeout);

    /* Start the main loop */
    LOGI("%s: ****************  Calling ev_run for addition", __func__);
    ev_run(mgr->loop, 0);
    LOGI("%s: ****************  Done with the addition", __func__);

    /* The ev loop was broken by the timeout */

    gkc_print_cache_entries();
    /* Validate the cache entries */
    ret = os_nif_macaddr_from_str(&mac, "AA:AA:AA:AA:AA:04");
    TEST_ASSERT_TRUE(ret);
    dcache = gkc_lookup_device_tree(&mac);
    TEST_ASSERT_NOT_NULL(dcache);
    app_tree = &dcache->app_tree;
    TEST_ASSERT_TRUE(ds_tree_is_empty(app_tree));

    ret = os_nif_macaddr_from_str(&mac, "AA:AA:AA:AA:AA:40");
    TEST_ASSERT_TRUE(ret);
    dcache = gkc_lookup_device_tree(&mac);
    TEST_ASSERT_NOT_NULL(dcache);
    app_tree = &dcache->app_tree;
    TEST_ASSERT_TRUE(ds_tree_is_empty(app_tree));

    delete_default_attr_entries();
}


void
run_gk_cache_ovsdb_app(void)
{
    struct gkc_test_mgr *mgr;

    mgr = gkc_get_test_mgr();

    if (mgr->has_ovsdb == false) return;

    gkc_tests_register_cleanup(gkc_tests_clean_all_ovsdb_entries);

    RUN_TEST(test_events_flush_one_entry);
    RUN_TEST(test_events_flush_all_entries);
    return;
}
