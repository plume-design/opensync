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

#include "data_report_tags.h"
#include "data_report_tags_internals.h"
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
#include "test_data_report_tags.h"
#include "unit_test_utils.h"

#define CMD_BUF_LEN 1024

/**
 * @brief breaks the ev loop to terminate a test
 */
static void
drt_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    LOGI("%s: here", __func__);
    ev_break(EV_A_ EVBREAK_ONE);
    LOGI("%s: done", __func__);
}


static int
drt_ev_test_setup(double timeout)
{
    ev_timer *p_timeout_watcher;
    struct drt_test_mgr *mgr;

    mgr = drt_get_test_mgr();

    /* Set up the timer killing the ev loop, indicating the end of the test */
    p_timeout_watcher = &mgr->timeout_watcher;

    ev_timer_init(p_timeout_watcher, drt_timeout_cb, timeout, 0.);
    ev_timer_start(mgr->loop, p_timeout_watcher);

    return 0;
}


static
struct drt_ut_cleanup drt_ut_clean_all[] =
{
    {
        .table = "Openflow_Tag",
        .field = "name",
        .id = "dev_tag_single_add_included",
    },
    {
        .table = "Openflow_Tag",
        .field = "name",
        .id = "dev_tag_single_add_excluded",
    },
    {
        .table = "Data_Report_Tags",
        .field = "name",
        .id = "dev_drt_ut_single_add_prec_include",
    },
    {
        .table = "Data_Report_Tags",
        .field = "name",
        .id = "dev_drt_ut_single_add_prec_exclude",
    },
};


static void
drt_tests_clean_ovsdb_entries(struct drt_ut_cleanup array[], size_t nelems)
{
    struct drt_ut_cleanup *entry;
    struct drt_test_mgr *mgr;
    char cmd[CMD_BUF_LEN];
    size_t i;

    mgr = drt_get_test_mgr();
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
drt_tests_clean_all_ovsdb_entries(void)
{
    drt_tests_clean_ovsdb_entries(drt_ut_clean_all, ARRAY_SIZE(drt_ut_clean_all));
}


static void
add_drt_cb(EV_P_ ev_timer *w, int revents)
{
    char cmd[CMD_BUF_LEN];
    char *included_macs;
    char *excluded_macs;
    char *precedence;
    char *oftag;
    char *name;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    /* Create Openflow tags */
    name = "dev_tag_single_add_included";
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

    name = "dev_tag_single_add_excluded";
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

    name = "dev_drt_ut_single_add_prec_include";
    included_macs = "\'[\"set\",[\"${dev_tag_single_add_included}\"]]\'";
    excluded_macs = "\'[\"set\",[\"${dev_tag_single_add_excluded}\"]]\'";
    precedence = "include";
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Data_Report_Tags "
             "name:=%s "
             "precedence:=%s "
             "included_macs:=%s "
             "excluded_macs:=%s",
             name, precedence, included_macs, excluded_macs);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

    name = "dev_drt_ut_single_add_prec_exclude";
    included_macs = "\'[\"set\",[\"${dev_tag_single_add_included}\"]]\'";
    excluded_macs = "\'[\"set\",[\"${dev_tag_single_add_excluded}\"]]\'";
    precedence = "exclude";
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Data_Report_Tags "
             "name:=%s "
             "precedence:=%s "
             "included_macs:=%s "
             "excluded_macs:=%s",
             name, precedence, included_macs, excluded_macs);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

    LOGI("\n***** %s: done\n", __func__);
}


static void
delete_drt_cb(EV_P_ ev_timer *w, int revents)
{
    char cmd[CMD_BUF_LEN];
    char *name;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    name = "dev_drt_ut_single_add_prec_include";
    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh d Data_Report_Tags "
             "-w name==%s ",
             name);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);

    LOGI("\n***** %s: done\n", __func__);
}


static void
setup_drt_test_add(void)
{
    struct drt_test_mgr *mgr;
    struct test_timers *t;
    struct ev_loop *loop;

    mgr = drt_get_test_mgr();

    t = &mgr->drt_test_timers;
    loop = mgr->loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add,
                  add_drt_cb,
                  mgr->g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
}


static void
setup_drt_test_delete(void)
{
    struct drt_test_mgr *mgr;
    struct test_timers *t;
    struct ev_loop *loop;

    mgr = drt_get_test_mgr();

    t = &mgr->drt_test_timers;
    loop = mgr->loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_delete,
                  delete_drt_cb,
                  mgr->g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_delete);
}


static struct drt_ut_validation to_validate_after_addition[] =
{
    {
        .feature = "dev_drt_ut_single_add_prec_include",
        .device = "66:55:44:33:22:11",
        .to_be_found = true,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_include",
        .device = "99:88:77:66:55:44",
        .to_be_found = true,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_include",
        .device = "aa:bb:cc:dd:ee:11",
        .to_be_found = false,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_include",
        .device = "11:ee:dd:cc:bb:aa",
        .to_be_found = false,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_exclude",
        .device = "66:55:44:33:22:11",
        .to_be_found = true,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_exclude",
        .device = "99:88:77:66:55:44",
        .to_be_found = true,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_exclude",
        .device = "aa:bb:cc:dd:ee:11",
        .to_be_found = false,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_exclude",
        .device = "11:ee:dd:cc:bb:aa",
        .to_be_found = false,
    },
};


static struct drt_ut_validation to_validate_after_deletion[] =
{
    {
        .feature = "dev_drt_ut_single_add_prec_include",
        .device = "66:55:44:33:22:11",
        .to_be_found = false,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_include",
        .device = "99:88:77:66:55:44",
        .to_be_found = false,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_include",
        .device = "aa:bb:cc:dd:ee:11",
        .to_be_found = false,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_include",
        .device = "11:ee:dd:cc:bb:aa",
        .to_be_found = false,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_exclude",
        .device = "66:55:44:33:22:11",
        .to_be_found = true,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_exclude",
        .device = "99:88:77:66:55:44",
        .to_be_found = true,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_exclude",
        .device = "aa:bb:cc:dd:ee:11",
        .to_be_found = false,
    },
    {
        .feature = "dev_drt_ut_single_add_prec_exclude",
        .device = "11:ee:dd:cc:bb:aa",
        .to_be_found = false,
    },
};


static void
test_events(void)
{
    struct drt_test_mgr *mgr;

    mgr = drt_get_test_mgr();
    if (mgr->has_ovsdb == false) return;

    setup_drt_test_add();

    /* Set overall test duration */
    drt_ev_test_setup(++mgr->g_timeout);

    /* Start the main loop */
    LOGI("%s: ****************  Calling ev_run for addition", __func__);
    ev_run(mgr->loop, 0);
    LOGI("%s: ****************  Done with the addition", __func__);

    drt_walk_caches();

    /* The ev loop was broken by the timeout */
    /* Validate the devices' features list */
    drt_ut_validate_devices_features(to_validate_after_addition,
                                     ARRAY_SIZE(to_validate_after_addition));

    /* Delete the first drt entry */
    mgr->g_timeout = 0;
    setup_drt_test_delete();

    /* Set overall test duration */
    drt_ev_test_setup(++mgr->g_timeout);

    /* Start the main loop */
    LOGI("%s: ****************  Calling ev_run for deletion", __func__);
    ev_run(mgr->loop, 0);
    LOGI("%s: ****************  Done with the deletion", __func__);

    drt_walk_caches();

    /* Validate the devices' features list */
    drt_ut_validate_devices_features(to_validate_after_deletion,
                                     ARRAY_SIZE(to_validate_after_deletion));
    LOGI("%s: ************* Validation after deletion completed", __func__);
}


void
run_data_report_tags_single_add(void)
{
    struct drt_test_mgr *mgr;

    mgr = drt_get_test_mgr();

    if (mgr->has_ovsdb == false) return;

    drt_tests_register_cleanup(drt_tests_clean_all_ovsdb_entries);
    RUN_TEST(test_events);
}
