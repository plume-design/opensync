#include "test_iotm.h"
#include "iotm_session.h"
#include "iotm_session_private.h"
#include "iotm_ovsdb.h"
#include "iotm_ovsdb_private.h"
#include "iotm_tag.h"

void test_iotm_session_update_valid()
{
    bool ret;
    struct iotm_session sess =
    {
        .name = "test",
        .location_id = "FakeLocation",
        .node_id = "FakeNode",
    };

    struct schema_IOT_Manager_Config conf =
    {
        .handler = "test",
        .plugin = "test",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "policy_table",                 /* IOT policy entry */
        },
        .other_config =
        {
            "dev-test/ble_core_ut/topic_0", /* topic */
            "test_dso_init",                /* plugin init routine */
            "test_policy",                  /* IOT policy entry */
        },
        .other_config_len = 3,
    };

    ret = iotm_session_update(&sess, &conf);
    TEST_ASSERT_TRUE_MESSAGE(ret, "Should be able to update the config for a session.");
    TEST_ASSERT_NOT_NULL_MESSAGE(sess.conf, "Should have loaded information into the session configuration.");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(conf.handler, sess.conf->handler, "Should have loaded information into the session configuration.");

    iotm_free_session_conf(sess.conf);

}

void test_iotm_alloc_session_valid()
{
    struct iotm_session *cur;
    struct schema_IOT_Manager_Config conf =
    {
        .handler = "test",
        .plugin = "test",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "policy_table",                 /* IOT policy entry */
        },
        .other_config =
        {
            "dev-test/ble_core_ut/topic_0", /* topic */
            "test_dso_init",                /* plugin init routine */
            "test_policy",                  /* IOT policy entry */
        },
        .other_config_len = 3,
    };

    cur = iotm_alloc_session(&conf);
    TEST_ASSERT_NOT_NULL_MESSAGE(cur, "Should ahve allocated a new session.");

    iotm_free_session(cur);

}


void test_iotm_alloc_session_invalid()
{
    struct iotm_session *cur;
    struct schema_IOT_Manager_Config conf;
    memset(&conf, 0, sizeof(conf));

    cur = iotm_alloc_session(&conf);
    TEST_ASSERT_NULL_MESSAGE(cur, "Should not allocate a session with an invalid config.");
}


void test_add_session_valid()
{
    ds_tree_t *sessions;
    struct iotm_session *session;
    struct schema_IOT_Manager_Config conf =
    {
        .handler = "test",
        .plugin = "test_parser",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "policy_table",                 /* IOT policy entry */
        },
        .other_config =
        {
            "dev-test/ble_core_ut/topic_0", /* topic */
            "test_dso_init",                /* plugin init routine */
            "test_policy",                  /* IOT policy entry */
        },
        .other_config_len = 3,
    };

    struct schema_AWLAN_Node awlan =
    {
        .mqtt_headers_keys =
        {
            "locationId",
            "nodeId",
        },
        .mqtt_headers =
        {
            "59efd33d2c93832025330a3e",
            "4C718002B3",
        },
        .mqtt_headers_len = 2,
    };

    iotm_get_awlan_headers(&awlan);
    iotm_add_session(&conf);

    sessions = iotm_get_sessions();
    session = ds_tree_find(sessions, conf.handler);
    TEST_ASSERT_NOT_NULL_MESSAGE(session, "Should have found an allocated session.");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(conf.handler, session->name, "Session name should have been configured");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(conf.plugin, session->dso, "Should have loaded the plugin to the session DSO.");
    TEST_ASSERT_NOT_NULL_MESSAGE(session->conf, "OVDSB Configuration should be populated");
    TEST_ASSERT_NOT_NULL_MESSAGE(session->mqtt_headers, "Should load in the manager's mqtt headers");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(conf.handler, session->name, "Session name should have been loaded");
    TEST_ASSERT_NOT_NULL_MESSAGE(session->conf, "FConf should be loaded.");
    TEST_ASSERT_NOT_NULL_MESSAGE(session->conf->other_config, "Other_Config should not be null as it was passed.");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("dev-test/ble_core_ut/topic_0", session->topic, "Topic should be loaded to session");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("59efd33d2c93832025330a3e", session->location_id, "Location should be set for session");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("4C718002B3", session->node_id, "Node should be set for session");
    TEST_ASSERT_NOT_NULL_MESSAGE(session->loop, "Loop should be initialized");
}

void test_invalid_session() {
    ds_tree_t *sessions;
    struct iotm_session *session;
    sessions = iotm_get_sessions();

    struct schema_IOT_Manager_Config conf;
    memset(&conf, 0, sizeof(conf));

    iotm_add_session(&conf);
    session = ds_tree_find(sessions, conf.handler);

    TEST_ASSERT_NULL_MESSAGE(session, "Should have not created an empty configuration");
}


void test_add_duplicate_session() {
    ds_tree_t *sessions = iotm_get_sessions();
    struct iotm_session *session;

    struct schema_IOT_Manager_Config conf =
    {
        .handler = "test",
        .plugin = "test_parser",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "policy_table",                 /* IOT policy entry */
        },
        .other_config =
        {
            "dev-test/ble_core_ut/topic_0", /* topic */
            "test_dso_init",                /* plugin init routine */
            "test_policy",                  /* IOT policy entry */
        },
        .other_config_len = 3,
    };

    iotm_add_session(&conf);
    session = ds_tree_find(sessions, conf.handler);

    TEST_ASSERT_NOT_NULL_MESSAGE(session, "Should have found an allocated session.");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(conf.plugin, session->dso, "Should have loaded the plugin to the session DSO.");

    strcpy(conf.plugin,  "some_new_plugin");

    iotm_add_session(&conf);
    session = ds_tree_find(sessions, conf.handler);

    TEST_ASSERT_NOT_NULL_MESSAGE(session, "Should have found an allocated session.");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test_parser", session->dso, "Should not have updated the \
            plugin as this is a duplicate row, not a new one.");

}

static int num_updates = 0;
void test_rule_update(struct iotm_session *sess, ovsdb_update_monitor_t *mon, struct iotm_rule *rule)
{
    num_updates += 1;
    return;
}

void test_iotm_rule_update_hits_plugin_callbacks(void)
{
    ds_tree_t *sessions;
    ovsdb_update_monitor_t mon;
    mon.mon_type = OVSDB_UPDATE_NEW;

    struct schema_IOT_Rule_Config rule =
    {
        .name = "mac_whitelist",
        .event = "ble_advertised",
        .filter_keys =
        {
            "src",
            "s_uuid",
        },
        .filter =
        {
            "AA:BB:CC:DD:EE:FF",
            "${ble_whitelist_service}",
        },
        .filter_len = 2,
        .actions_keys =
        {
            "command"
        },
        .actions =
        {
            "ble_connect",
        },
        .actions_len = 1,
        .params_keys =
        {
            "mac",
        },
        .params =
        {
            "testmac",
        },
        .params_len = 1,
    };

    struct iotm_session session =
    {
        .name = "mytestsession",
        .ops = { .rule_update = test_rule_update },
    };
    sessions = iotm_get_sessions();
    ds_tree_insert(sessions, &session, "mytestsession");
    callback_IOT_Rule_Config(&mon, NULL, &rule);

    // make sure rule is removed
    ds_tree_remove(sessions, &session);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, num_updates, "Should have called the update callback");
}

static int num_tags = 0;
void test_tag_cb(char *name, char *value, void *ctx)
{
    num_tags += 1;
}
void test_tag_iteration(void)
{
    struct schema_Openflow_Tag tags[2] =
    {
        {
            .name = "mytag",
            .device_value_len = 1,
            .device_value =
            {
                "testval"
            },
            .cloud_value_len = 2,
            .cloud_value =
            {
                "cloudval",
                "anothercloudval",
            },
        },
        {
            .name = "othertag",
            .device_value_len = 2,
            .device_value =
            {
                "AA:BB:CC:DD:EE:FF",
                "*",
            },
            .cloud_value_len = 0,
        },
    };
    struct iotm_tree_t *tree = iotm_get_tags();
    add_tag_to_tree(tree, &tags[0]);
    add_tag_to_tree(tree, &tags[1]);

    num_tags = 0;
    session_foreach_tag("mytag", test_tag_cb, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
            3,
            num_tags,
            "Should iterate over values matching the key 'mytag'");

    num_tags = 0;
    session_foreach_tag(NULL, test_tag_cb, NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
            5,
            num_tags,
            "Should iterate over all tag values as a NULL key was passed");
}

void test_iot_session_suite() {
    // session test suite
    RUN_TEST(test_iotm_session_update_valid);
    RUN_TEST(test_iotm_alloc_session_valid);
    RUN_TEST(test_add_session_valid);
    RUN_TEST(test_iotm_alloc_session_invalid);
    RUN_TEST(test_add_duplicate_session);
    RUN_TEST(test_invalid_session);
    RUN_TEST(test_iotm_rule_update_hits_plugin_callbacks);
    RUN_TEST(test_tag_iteration);
}
