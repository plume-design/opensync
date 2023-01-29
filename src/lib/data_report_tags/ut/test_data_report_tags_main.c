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

#include "data_report_tags.h"
#include "ovsdb.h"
#include "ovsdb_utils.h"
#include "log.h"
#include "memutil.h"
#include "os_nif.h"
#include "target.h"
#include "unity.h"

#include "test_data_report_tags.h"
#include "data_report_tags_internals.h"
#include "unit_test_utils.h"

const char *test_name = "data_report_tags_tests";

static ovsdb_table_t table_Openflow_Tag;
static ovsdb_table_t table_Openflow_Tag_Group;

static struct drt_test_mgr drt_ovsdb_test_mgr;


struct drt_test_mgr *
drt_get_test_mgr(void)
{
    return &drt_ovsdb_test_mgr;
};


static bool
drt_ut_tag_update_cb(om_tag_t *tag,
                     struct ds_tree *removed,
                     struct ds_tree *added,
                     struct ds_tree *updated)
{
    LOGI("%s: tag name: %s, group: %s", __func__,
         tag->name, tag->group ? "yes" : "false");

    data_report_tags_update_cb(tag, removed, added, updated);

    return true;
}


void
drt_ovsdb_monitor_tags(void)
{
    struct tag_mgr drt_ut_tagmgr;

    memset(&drt_ut_tagmgr, 0, sizeof(drt_ut_tagmgr));
    drt_ut_tagmgr.service_tag_update = drt_ut_tag_update_cb;
    om_tag_init(&drt_ut_tagmgr);

    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag_Group);

    om_standard_callback_openflow_tag(&table_Openflow_Tag);
    om_standard_callback_openflow_tag_group(&table_Openflow_Tag_Group);
}


static void
drt_ovsdb_test_setup(void)
{
    struct drt_test_mgr *mgr;

    mgr = drt_get_test_mgr();

    mgr->has_ovsdb = unit_test_check_ovs();
    if (mgr->has_ovsdb == false)
    {
        LOGI("%s: no ovsdb available", __func__);
        goto no_ovsdb;
    }

    /* Proceed to settings and connecting to the ovsdb server */
    mgr->loop = EV_DEFAULT;
    mgr->g_timeout = 1.0;

    if (!ovsdb_init_loop(mgr->loop, "UT_DATA_REPORT_TAGS"))
    {
        LOGI("%s: failed to initialize ovsdb framework", __func__);

        goto no_ovsdb;
    }
    mgr->has_ovsdb = true;
    drt_ovsdb_monitor_tags();
    data_report_tags_init();

    return;

no_ovsdb:
    mgr->has_ovsdb = false;
    data_report_tags_init_manager();
}


void
drt_tests_register_cleanup(cleanup_callback_t cleanup)
{
    struct drt_tests_cleanup_entry *entry;
    struct drt_test_mgr *mgr;

    mgr = drt_get_test_mgr();

    entry = CALLOC(1, sizeof(*entry));
    entry->callback = cleanup;

    ds_dlist_insert_tail(&mgr->cleanup, entry);
}



static void
drt_test_global_init(void)
{
    struct drt_test_mgr *mgr;

    mgr = drt_get_test_mgr();
    ds_dlist_init(&mgr->cleanup, struct drt_tests_cleanup_entry, node);
    drt_ovsdb_test_setup();
}


static void
drt_test_global_exit(void)
{
    struct drt_tests_cleanup_entry *cleanup_entry;
    struct drt_test_mgr *mgr;

    mgr = drt_get_test_mgr();

    if (!ovsdb_stop_loop(mgr->loop))
    {
        LOGE("%s: Failed to stop OVSDB", __func__);
    }

    ev_loop_destroy(mgr->loop);

    cleanup_entry = ds_dlist_head(&mgr->cleanup);
    while (cleanup_entry != NULL)
    {
        struct drt_tests_cleanup_entry *remove;
        struct drt_tests_cleanup_entry *next;

        next = ds_dlist_next(&mgr->cleanup, cleanup_entry);
        remove = cleanup_entry;
        cleanup_entry = next;

        remove->callback();
        ds_dlist_remove(&mgr->cleanup, remove);
        FREE(remove);
    }

    data_report_tags_exit();

    return;
}


static void
drt_ut_validate_features_set(char *feature, char *device, bool to_be_found)
{
    struct str_set *features_set;
    os_macaddr_t lookup;
    bool found;
    bool ret;
    size_t i;

    LOGI("%s: validating feature %s for device %s", __func__, feature, device);
    ret = os_nif_macaddr_from_str(&lookup, device);
    TEST_ASSERT_TRUE(ret);

    features_set = data_report_tags_get_tags(&lookup);

    if (to_be_found)
    {
        TEST_ASSERT_NOT_NULL(features_set);
    }

    if (features_set == NULL)
    {
        TEST_ASSERT_FALSE(to_be_found);
        return;
    }

    found = false;
    LOGI("%s: features set nelems: %zu", __func__, features_set->nelems);
    for (i = 0; i < features_set->nelems; i++)
    {
        char *feature_item;
        int rc;

        feature_item = features_set->array[i];
        LOGI("%s: feature_item: %s", __func__, feature_item);
        rc = strcmp(feature, feature_item);
        found = (rc == 0);
        if (found) break;
    }
    if (to_be_found)
    {
        TEST_ASSERT_TRUE(found);
    }
    else
    {
        TEST_ASSERT_FALSE(found);
    }
}

void
drt_ut_validate_devices_features(struct drt_ut_validation to_validate[], size_t nelems)
{
    size_t i;

    for (i = 0; i < nelems; i++)
    {
        drt_ut_validate_features_set(to_validate[i].feature,
                                     to_validate[i].device,
                                     to_validate[i].to_be_found);
    }
}


static void
drt_setUp(void)
{
    return;
}


static void
drt_tearDown(void)
{
    return;
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    ut_init(test_name, drt_test_global_init, drt_test_global_exit);

    ut_setUp_tearDown(test_name, drt_setUp, drt_tearDown);

    run_data_report_tags_ovsdb();
    run_data_report_tags_single_add();
    run_data_report_tags_update_drts();
    run_data_report_tags_update_tags();

    return ut_fini();
}
