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
#include "unit_test_utils.h"
#include "dpi_intf.h"
#include "dpi_intf_internals.h"
#include "test_dpi_intf.h"

#define CMD_BUF_LEN 1024


/**
 * @brief breaks the ev loop to terminate a test
 */
static void
dpi_intf_test_timeout_cb(EV_P_ ev_timer *w, int revents)
{
    LOGI("%s: here", __func__);
    ev_break(EV_A_ EVBREAK_ONE);
    LOGI("%s: done", __func__);
}


static int
dpi_intf_ev_test_setup(double timeout)
{
    struct dpi_intf_test_mgr *mgr;
    ev_timer *p_timeout_watcher;

    mgr = dpi_intf_test_get_mgr();

    /* Set up the timer killing the ev loop, indicating the end of the test */
    p_timeout_watcher = &mgr->timeout_watcher;

    ev_timer_init(p_timeout_watcher, dpi_intf_test_timeout_cb, timeout, 0.);
    ev_timer_start(mgr->loop, p_timeout_watcher);

    return 0;
}


static
struct dpi_intf_ut_cleanup dpi_intf_ut_clean_all[] =
{
    {
        .table = "Dpi_Interface_Map",
        .field = "tap_if_name",
        .id = "tap_if_1",
    },
};


static void
dpi_intf_test_clean_ovsdb_entries(struct dpi_intf_ut_cleanup array[], size_t nelems)
{
    struct dpi_intf_ut_cleanup *entry;
    struct dpi_intf_test_mgr *mgr;
    char cmd[CMD_BUF_LEN];
    size_t i;

    mgr = dpi_intf_test_get_mgr();
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
add_dpi_intf_cb(EV_P_ ev_timer *w, int revents)
{
    char cmd[CMD_BUF_LEN];
    char *tap_if_name;
    char *tap_tx_name;
    int rc;

    tap_if_name = "tap_if_1";
    tap_tx_name = "tap_tx_1";
    MEMZERO(cmd);
    snprintf(cmd, sizeof(cmd),
             "ovsh i Dpi_Interface_Map "
             "tap_if_name:=%s "
             "tx_if_name:=%s ",
             tap_if_name, tap_tx_name);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);
}


static void
setup_dpi_intf_test_add(void)
{
    struct dpi_intf_test_mgr *mgr;
    struct test_timers *t;
    struct ev_loop *loop;

    mgr = dpi_intf_test_get_mgr();

    t = &mgr->dpi_intf_test_timers;
    loop = mgr->loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add,
                  add_dpi_intf_cb,
                  mgr->g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
}


static void
delete_dpi_intf_cb(EV_P_ ev_timer *w, int revents)
{
    char cmd[CMD_BUF_LEN];
    char *tap_if_name;
    int rc;

    tap_if_name = "tap_if_1";
    MEMZERO(cmd);
    snprintf(cmd, sizeof(cmd),
             "ovsh d Dpi_Interface_Map "
             "-w tap_if_name==%s ",
             tap_if_name);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    sleep(1);
}


static void
setup_dpi_intf_test_delete(void)
{
    struct dpi_intf_test_mgr *mgr;
    struct test_timers *t;
    struct ev_loop *loop;

    mgr = dpi_intf_test_get_mgr();

    t = &mgr->dpi_intf_test_timers;
    loop = mgr->loop;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete,
                  delete_dpi_intf_cb,
                  mgr->g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_delete);
}


static void
test_dpi_intf_register_context(void)
{
    struct dpi_intf_registration registrar;
    struct dpi_intf_test_mgr *mgr;

    mgr = dpi_intf_test_get_mgr();
    MEMZERO(registrar);
    registrar.loop = mgr->loop;
    registrar.id = "ut_dpi_intf";

    dpi_intf_register_context(&registrar);
}


static void
test_events(void)
{
    struct dpi_intf_test_mgr *mgr;

    mgr = dpi_intf_test_get_mgr();
    if (mgr->has_ovsdb == false) return;

    setup_dpi_intf_test_add();

    /* Set overall test duration */
    dpi_intf_ev_test_setup(++mgr->g_timeout);

    /* Start the main loop */
    LOGI("%s: ****************  Calling ev_run for addition", __func__);
    ev_run(mgr->loop, 0);
    LOGI("%s: ****************  Done with the addition", __func__);

    /* The ev loop was broken by the timeout */

    sleep(1);

    test_dpi_intf_register_context();

    mgr->g_timeout = 0;
    setup_dpi_intf_test_delete();

    /* Set overall test duration */
    dpi_intf_ev_test_setup(++mgr->g_timeout);

    /* Start the main loop */
    LOGI("%s: ****************  Calling ev_run for deletion", __func__);
    ev_run(mgr->loop, 0);
    LOGI("%s: ****************  Done with the deletion", __func__);
}


static void
dpi_intf_test_clean_all_ovsdb_entries(void)
{
    dpi_intf_test_clean_ovsdb_entries(dpi_intf_ut_clean_all,
                                      ARRAY_SIZE(dpi_intf_ut_clean_all));
}


void
run_dpi_intf_test_ovsdb(void)
{
    struct dpi_intf_test_mgr *mgr;

    mgr = dpi_intf_test_get_mgr();

    if (mgr->has_ovsdb == false) return;

    dpi_intf_test_register_cleanup(dpi_intf_test_clean_all_ovsdb_entries);
    RUN_TEST(test_events);
}
