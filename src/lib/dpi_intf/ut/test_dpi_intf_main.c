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
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "ovsdb.h"
#include "ovsdb_utils.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_utils.h"
#include "schema.h"
#include "log.h"
#include "memutil.h"
#include "os_nif.h"
#include "target.h"
#include "unity.h"
#include "unit_test_utils.h"

#include "dpi_intf.h"
#include "dpi_intf_internals.h"
#include "test_dpi_intf.h"

const char *test_name = "dpi_intf_tests";

static struct dpi_intf_test_mgr test_mgr;

struct dpi_intf_test_mgr *
dpi_intf_test_get_mgr(void)
{
    return &test_mgr;
};


static bool
dpi_intf_test_init_forward_context(struct dpi_intf_entry *entry)
{
    LOGI("%s: called", __func__);
    return true;
}


static bool
dpi_intf_test_enable_pcap(struct dpi_intf_entry *entry)
{
    LOGI("%s: called", __func__);
    return true;
}


static void
dpi_intf_test_disable_pcap(struct dpi_intf_entry *entry)
{
    LOGI("%s: called", __func__);
    return;
}


static void
init_dpi_intf_lib(bool has_ovsdb)
{
    struct dpi_intf_mgr *mgr;

    if (has_ovsdb)
    {
        dpi_intf_init();
    }
    else
    {
        dpi_intf_init_manager();
    }

    /* Overwrite the dpi intf manager's ops */
    mgr = dpi_intf_get_mgr();
    mgr->ops.init_forward_context = dpi_intf_test_init_forward_context;
    mgr->ops.enable_pcap = dpi_intf_test_enable_pcap;
    mgr->ops.disable_pcap = dpi_intf_test_disable_pcap;

    return;
}


static void
dpi_intf_ovsdb_test_setup(void)
{
    struct dpi_intf_test_mgr *mgr;

    mgr = dpi_intf_test_get_mgr();

    mgr->has_ovsdb = unit_test_check_ovs();
    if (!mgr->has_ovsdb)
    {
        LOGI("%s: no ovsdb available", __func__);
        goto no_ovsdb;
    }

    /* Proceed to settings and connecting to the ovsdb server */
    mgr->loop = EV_DEFAULT;
    mgr->g_timeout = 1.0;

    mgr->has_ovsdb = ovsdb_init_loop(mgr->loop, "UT_DPI_INTF_TAGS");
    if (!mgr->has_ovsdb)
    {
        LOGI("%s: failed to initialize ovsdb framework", __func__);
    }

no_ovsdb:
    init_dpi_intf_lib(mgr->has_ovsdb);
}


void
dpi_intf_test_register_cleanup(cleanup_callback_t cleanup)
{
    struct dpi_intf_test_cleanup_entry *entry;
    struct dpi_intf_test_mgr *mgr;

    mgr = dpi_intf_test_get_mgr();

    entry = CALLOC(1, sizeof(*entry));
    entry->callback = cleanup;

    ds_dlist_insert_tail(&mgr->cleanup, entry);
}


static void
dpi_intf_test_global_init(void)
{
    struct dpi_intf_test_mgr *mgr;

    mgr = dpi_intf_test_get_mgr();

    ds_dlist_init(&mgr->cleanup, struct dpi_intf_test_cleanup_entry, node);

    dpi_intf_ovsdb_test_setup();
}


static void
dpi_intf_test_global_exit(void)
{
    struct dpi_intf_test_cleanup_entry *cleanup_entry;
    struct dpi_intf_test_mgr *mgr;

    mgr = dpi_intf_test_get_mgr();

    if (!ovsdb_stop_loop(mgr->loop))
    {
        LOGE("%s: Failed to stop OVSDB", __func__);
    }

    ev_loop_destroy(mgr->loop);

    cleanup_entry = ds_dlist_head(&mgr->cleanup);
    while (cleanup_entry != NULL)
    {
        struct dpi_intf_test_cleanup_entry *remove;
        struct dpi_intf_test_cleanup_entry *next;

        next = ds_dlist_next(&mgr->cleanup, cleanup_entry);
        remove = cleanup_entry;
        cleanup_entry = next;

        remove->callback();
        ds_dlist_remove(&mgr->cleanup, remove);
        FREE(remove);
    }

    dpi_intf_exit();

    return;
}


static void
dpi_intf_test_setUp(void)
{
    return;
}


static void
dpi_intf_test_tearDown(void)
{
    return;
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, dpi_intf_test_global_init, dpi_intf_test_global_exit);

    ut_setUp_tearDown(test_name, dpi_intf_test_setUp, dpi_intf_test_tearDown);

    run_dpi_intf_test_ovsdb();

    return ut_fini();
}
