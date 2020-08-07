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

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <net/if.h>

#include "ds_tree.h"
#include "log.h"
#include "oms.h"
#include "oms_report.h"
#include "ovsdb.h"
#include "os.h"
#include "os_types.h"
#include "qm_conn.h"
#include "schema.h"
#include "target.h"
#include "unity.h"



const char *test_name = "oms_tests";

/**
 * @brief a set of config entries as would be delivered through ovsdb
 */
static struct
schema_OMS_Config g_confs[] =
{
    {
        .object_name = "test_object_1",
        .object_name_present = true,
        .version = "9999.8888",
        .version_present = true,
        .other_config_present = false,
    },
};


/**
 * @brief a set of state entries as would be delivered through ovsdb
 */
static struct
schema_Object_Store_State g_states[] =
{
    {
        .name = "test_object_1",
        .name_present = true,
        .version = "9999.8888",
        .version_present = true,
        .status = "active",
        .status_present = true,
    },
    {
        .name = "test_object_2",
        .name_present = true,
        .version = "9999.8888.7",
        .version_present = true,
        .status = "error",
        .status_present = true,
    },
    {
        .name = "test_object_3",
        .name_present = true,
        .version = "9999.8888.7777",
        .version_present = true,
        .status = "active",
        .status_present = true,
    }
};


struct test_timers
{
    ev_timer timeout_watcher_add;              /* Add entries */
    ev_timer timeout_watcher_validate_add;     /* Validate added entries */
    ev_timer timeout_watcher_delete;           /* Delete entries */
    ev_timer timeout_watcher_validate_delete;  /* Validate added entries */
    ev_timer timeout_watcher_update;           /* Update entries */
    ev_timer timeout_watcher_validate_update;  /* Validate updated entries */
};


struct test_mgr
{
    struct ev_loop *loop;
    ev_timer timeout_watcher;
    bool has_ovsdb;
    bool has_qm;
    bool expected;
    struct test_timers oms_config_test;
    struct test_timers oms_state_test;
    double g_timeout;
    char *mqtt_topic;
} g_test_mgr;


/**
 * @brief breaks the ev loop to terminate a test
 */
static void
timeout_cb(EV_P_ ev_timer *w, int revents)
{
    ev_break(EV_A_ EVBREAK_ONE);
}


#if !defined(__x86_64__)
/**
 * @brief called by the unity framework at the start of each test
 */
int
oms_ovsdb_test_setup(void)
{
    int rc;

    /* Connect to ovsdb */
    rc = ovsdb_init_loop(g_test_mgr.loop, test_name);
    if (!rc)
    {
        LOGE("%s: Failed to initialize OVSDB", __func__);
        return -1;
    }

    g_test_mgr.has_ovsdb = true;
    return 0;
}

#else
int oms_ovsdb_test_setup(void)
{
    g_test_mgr.has_ovsdb = false;
    return 0;
}
#endif


int
oms_ev_test_setup(double timeout)
{
    ev_timer *p_timeout_watcher;

    /* Set up the timer killing the ev loop, indicating the end of the test */
    p_timeout_watcher = &g_test_mgr.timeout_watcher;

    ev_timer_init(p_timeout_watcher, timeout_cb, timeout, 0.);
    ev_timer_start(g_test_mgr.loop, p_timeout_watcher);

    return 0;
}


bool
accept_id_test(const char *object)
{
    return true;
}


void
config_cb_test(struct oms_config_entry *entry, int event)
{
    LOGI("%s: object: %s,  event: %d", __func__,
         entry->object, event);
}


void
state_cb_test(struct oms_state_entry *entry, int event)
{
    LOGI("%s: object: %s,  event: %d", __func__,
         entry->object, event);
}


bool
report_cb_test(struct oms_state_entry *entry)
{
    bool ret;
    int rc;

    rc = strcmp("error", entry->state);
    ret = (rc != 0);
    LOGI("%s: object: %s,  state: %s, reporting: %s", __func__,
         entry->object, entry->state, ret ? "true" : "false");

    return ret;
}


void
oms_global_test_setup(void)
{
    struct oms_ovsdb_set oms_set;
    struct oms_mgr *mgr;
    int rc;

    g_test_mgr.has_ovsdb = false;
    g_test_mgr.loop = EV_DEFAULT;
    g_test_mgr.g_timeout = 1.0;
    rc = oms_ovsdb_test_setup();
    TEST_ASSERT_EQUAL_INT(0, rc);

#if !defined(ARCH_X86)
    g_test_mgr.has_qm = true;
#else
    g_test_mgr.has_qm = false;
#endif

    g_test_mgr.mqtt_topic = strdup("dev-ut/object_status_report");
    TEST_ASSERT_NOT_NULL(g_test_mgr.mqtt_topic);

    oms_init_manager();
    memset(&oms_set, 0, sizeof(oms_set));
    oms_set.monitor_config = g_test_mgr.has_ovsdb;
    oms_set.monitor_state = g_test_mgr.has_ovsdb;
    oms_set.monitor_awlan = g_test_mgr.has_ovsdb;
    oms_set.accept_id = accept_id_test;
    oms_set.config_cb = config_cb_test;
    oms_set.state_cb = state_cb_test;
    oms_set.report_cb = report_cb_test;
    oms_ovsdb_init(&oms_set);

    mgr = oms_get_mgr();
    mgr->node_id = "4C718002B3";
    mgr->location_id = "59f39f5acbb22513f0ae5e17";
}


/**
 * @brief sends a serialized buffer over MQTT
 *
 * @param pb serialized buffer
 */
static void
oms_test_emit_report(struct packed_buffer *pb)
{
    qm_response_t res;
    bool ret;

    if (!g_test_mgr.has_qm) return;

    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG,
                              g_test_mgr.mqtt_topic,
                              pb->buf, pb->len, &res);
    TEST_ASSERT_TRUE(ret);
}

void
oms_global_test_teardown(void)
{
    g_test_mgr.has_ovsdb = false;
}


void
setUp(void)
{}


void
tearDown(void)
{}


void
test_ovsdb_add_config(void)
{
    struct schema_OMS_Config *config;
    struct oms_config_entry *entry;
    struct oms_config_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;

    mgr = oms_get_mgr();
    tree = &mgr->config;

    config = &g_confs[0];
    oms_ovsdb_add_config_entry(config);

    lookup.object = config->object_name;
    lookup.version = config->version;

    entry = ds_tree_find(tree, &lookup);
    TEST_ASSERT_NOT_NULL(entry);

    /* Clean up */
    oms_delete_config_entries();
}


void
test_ovsdb_add_state(void)
{
    struct schema_Object_Store_State *state;
    struct oms_state_entry *entry;
    struct oms_state_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;

    mgr = oms_get_mgr();
    tree = &mgr->state;

    state = &g_states[0];
    oms_ovsdb_add_state_entry(state);

    /* Validate the number of states */
    TEST_ASSERT_EQUAL_UINT(1, mgr->num_states);

    lookup.object = state->name;
    lookup.version = state->version;
    entry = ds_tree_find(tree, &lookup);
    TEST_ASSERT_NOT_NULL(entry);

    /* Clean up */
    oms_delete_state_entries();

    /* Validate the number of states */
    TEST_ASSERT_EQUAL_UINT(0, mgr->num_states);
}


void
test_add_and_delete_config_entry(void)
{
    struct oms_config_entry entry;

    if (!g_test_mgr.has_ovsdb) return;

    memset(&entry, 0, sizeof(entry));

    entry.object = "dpi signatures";
    entry.version = "1.0.1";

    oms_add_config_entry(&entry);

    oms_delete_config_entry(&entry);
}


void
test_add_and_delete_state_entry(void)
{
    struct oms_state_entry entry;

    if (!g_test_mgr.has_ovsdb) return;

    memset(&entry, 0, sizeof(entry));

    entry.object = "dpi signatures";
    entry.version = "1.0.1";
    entry.state = "active";

    oms_add_state_entry(&entry);

    oms_delete_state_entry(&entry);
}


void
add_config_cb(EV_P_ ev_timer *w, int revents)
{
    char *version;
    char cmd[256];
    char *object;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    object = "ut";
    version = "1.99.0";

    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i OMS_Config "
             "object_name:=%s "
             "version:=%s",
             object, version);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}


void
lookup_added_config_cb(EV_P_ ev_timer *w, int revents)
{
    struct oms_config_entry *entry;
    struct oms_config_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    char *version;
    char *object;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr = oms_get_mgr();
    tree = &mgr->config;

    object = "ut";
    version = "1.99.0";

    lookup.object = object;
    lookup.version = version;
    entry = ds_tree_find(tree, &lookup);
    TEST_ASSERT_NOT_NULL(entry);

    LOGI("\n***** %s: done\n", __func__);
}


void
delete_config_cb(EV_P_ ev_timer *w, int revents)
{
    char *version;
    char cmd[256];
    char *object;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    object = "ut";
    version = "1.99.0";

    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh d OMS_Config "
             "-w object_name==%s "
             "-w version==%s",
             object, version);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}


void
lookup_deleted_config_cb(EV_P_ ev_timer *w, int revents)
{
    struct oms_config_entry *entry;
    struct oms_config_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    char *version;
    char *object;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr = oms_get_mgr();
    tree = &mgr->config;
    object = "ut";
    version = "1.99.0";

    lookup.object = object;
    lookup.version = version;
    entry = ds_tree_find(tree, &lookup);
    TEST_ASSERT_NULL(entry);

    LOGI("\n***** %s: done\n", __func__);
}


void
setup_config_event_tests(void)
{
    struct test_timers *t;
    struct ev_loop *loop;

    if (!g_test_mgr.has_ovsdb)
    {
        LOGI("%s: ovsdb support, bypassing test", __func__);
        return;
    }

    t = &g_test_mgr.oms_config_test;
    loop = g_test_mgr.loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add,
                  add_config_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    /* Arm the cache lookup execution timer validating the cache addition */
    ev_timer_init(&t->timeout_watcher_validate_add,
                  lookup_added_config_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_validate_add.data = NULL;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete,
                  delete_config_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    /* Arm the cache lookup execution timer validating the cache deletion */
    ev_timer_init(&t->timeout_watcher_validate_delete,
                  lookup_deleted_config_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_validate_delete.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
    ev_timer_start(loop, &t->timeout_watcher_validate_add);
    ev_timer_start(loop, &t->timeout_watcher_delete);
    ev_timer_start(loop, &t->timeout_watcher_validate_delete);
}


void
add_state_cb(EV_P_ ev_timer *w, int revents)
{
    char *version;
    char cmd[256];
    char *object;
    char *state;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    object = "ut";
    version = "1.100.0";
    state = "install-done";

    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh i Object_Store_State "
             "name:=%s "
             "version:=%s "
             "status:=%s",
             object, version, state);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}


void
lookup_added_state_cb(EV_P_ ev_timer *w, int revents)
{
    struct oms_state_entry *entry;
    struct oms_state_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    char *version;
    char *object;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr = oms_get_mgr();
    tree = &mgr->state;

    object = "ut";
    version = "1.100.0";

    lookup.object = object;
    lookup.version = version;
    entry = ds_tree_find(tree, &lookup);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("install-done", entry->state);

    LOGI("\n***** %s: done\n", __func__);
}


void
update_state_cb(EV_P_ ev_timer *w, int revents)
{
    char *version;
    char cmd[256];
    char *object;
    char *state;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    object = "ut";
    version = "1.100.0";
    state = "active";

    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh u Object_Store_State "
             "-w name==%s "
             "-w version==%s "
             "status:=%s",
             object, version, state);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}


void
lookup_updated_state_cb(EV_P_ ev_timer *w, int revents)
{
    struct oms_state_entry *entry;
    struct oms_state_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    char *version;
    char *object;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr = oms_get_mgr();
    tree = &mgr->state;

    object = "ut";
    version = "1.100.0";

    lookup.object = object;
    lookup.version = version;
    entry = ds_tree_find(tree, &lookup);
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING("install-done", entry->prev_state);
    TEST_ASSERT_EQUAL_STRING("active", entry->state);
    LOGI("\n***** %s: done\n", __func__);
}


void
delete_state_cb(EV_P_ ev_timer *w, int revents)
{
    char *version;
    char cmd[256];
    char *object;
    int rc;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);

    object = "ut";
    version = "1.100.0";

    memset(cmd, 0 , sizeof(cmd));
    snprintf(cmd, sizeof(cmd),
             "ovsh d Object_Store_State "
             "-w name==%s "
             "-w version==%s",
             object, version);
    rc = cmd_log(cmd);
    TEST_ASSERT_EQUAL_INT(0, rc);

    LOGI("\n***** %s: done\n", __func__);
}


void
lookup_deleted_state_cb(EV_P_ ev_timer *w, int revents)
{
    struct oms_state_entry *entry;
    struct oms_state_entry lookup;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    char *version;
    char *object;

    LOGI("\n\n\n\n ***** %s: entering\n", __func__);
    mgr = oms_get_mgr();
    tree = &mgr->state;
    object = "ut";
    version = "1.100.0";

    lookup.object = object;
    lookup.version = version;
    entry = ds_tree_find(tree, &lookup);
    TEST_ASSERT_NULL(entry);

    LOGI("\n***** %s: done\n", __func__);
}


void
setup_state_event_tests(void)
{
    struct test_timers *t;
    struct ev_loop *loop;

    if (!g_test_mgr.has_ovsdb)
    {
        LOGI("%s: ovsdb support, bypassing test", __func__);
        return;
    }

    t = &g_test_mgr.oms_state_test;
    loop = g_test_mgr.loop;

    /* Arm the addition execution timer */
    ev_timer_init(&t->timeout_watcher_add,
                  add_state_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_add.data = NULL;

    /* Arm the cache lookup execution timer validating the cache addition */
    ev_timer_init(&t->timeout_watcher_validate_add,
                  lookup_added_state_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_validate_add.data = NULL;

    /* Arm the update execution timer */
    ev_timer_init(&t->timeout_watcher_update,
                  update_state_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_update.data = NULL;

    /* Arm the cache lookup execution timer validating the cache addition */
    ev_timer_init(&t->timeout_watcher_validate_update,
                  lookup_updated_state_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_validate_update.data = NULL;

    /* Arm the deletion execution timer */
    ev_timer_init(&t->timeout_watcher_delete,
                  delete_state_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_delete.data = NULL;

    /* Arm the cache lookup execution timer validating the cache deletion */
    ev_timer_init(&t->timeout_watcher_validate_delete,
                  lookup_deleted_state_cb,
                  g_test_mgr.g_timeout++, 0);
    t->timeout_watcher_validate_delete.data = NULL;

    ev_timer_start(loop, &t->timeout_watcher_add);
    ev_timer_start(loop, &t->timeout_watcher_validate_add);
    ev_timer_start(loop, &t->timeout_watcher_update);
    ev_timer_start(loop, &t->timeout_watcher_validate_update);
    ev_timer_start(loop, &t->timeout_watcher_delete);
    ev_timer_start(loop, &t->timeout_watcher_validate_delete);
}


void
test_events(void)
{
    if (!g_test_mgr.has_ovsdb) return;

    setup_config_event_tests();
    setup_state_event_tests();

    /* Test overall test duration */
    oms_ev_test_setup(++g_test_mgr.g_timeout);

    /* Start the main loop */
    ev_run(g_test_mgr.loop, 0);
}


void
test_serialize_report(void)
{
    struct schema_Object_Store_State *state;
    struct oms_state_entry *entry;
    struct oms_state_entry lookup;
    struct packed_buffer *pb;
    struct oms_mgr *mgr;
    ds_tree_t *tree;
    size_t nelems;
    size_t i;

    mgr = oms_get_mgr();
    tree = &mgr->state;

    /* Populate the states */
    nelems = sizeof(g_states) / sizeof(g_states[0]);
    for (i = 0; i < nelems; i++)
    {
        state = &g_states[i];
        oms_ovsdb_add_state_entry(state);

        lookup.object = state->name;
        lookup.version = state->version;
        entry = ds_tree_find(tree, &lookup);
        TEST_ASSERT_NOT_NULL(entry);
    }

    /* Validate the number of states */
    TEST_ASSERT_EQUAL_UINT(nelems, mgr->num_states);

    pb = oms_report_serialize_report();
    TEST_ASSERT_NOT_NULL(pb);
    TEST_ASSERT_NOT_NULL(pb->buf);

    /* Validate the number of states to report (state == 3 filtered out) */
    TEST_ASSERT_EQUAL_UINT(nelems - 1, mgr->num_reports);

    oms_test_emit_report(pb);

    /* Clean up */
    oms_report_free_packed_buffer(pb);
    oms_delete_state_entries();
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_TRACE);

    UnityBegin(test_name);

    oms_global_test_setup();

    RUN_TEST(test_ovsdb_add_config);
    RUN_TEST(test_ovsdb_add_state);
    RUN_TEST(test_add_and_delete_config_entry);
    RUN_TEST(test_add_and_delete_state_entry);
    RUN_TEST(test_serialize_report);
    RUN_TEST(test_events);

    oms_global_test_teardown();

    return UNITY_END();
}
