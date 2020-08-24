#include "test_iotm.h"
#include "iotm_router.h"
#include "iotm_router_private.h"
#include "iotm_session.h"
#include "iotm_tag.h"
#include "iotm.h"

void test_cmp_cb()
{
    struct cmp_t compare = 
    {
        .to = "match",
        .result = true,
    };

    struct iotm_value_t checking = 
    {
        .value = "match",
    };

    cmp_cb(NULL, &checking, &compare);
    TEST_ASSERT_TRUE(compare.result);
}

struct iotm_tree_t *get_test_rule_action_tree()
{
    struct iotm_tree_t *actions = iotm_tree_new();
    iotm_tree_add_str(actions, "ble_default", "connect");
    iotm_tree_add_str(actions, "ble_sniff", "advertisement");
    iotm_tree_add_str(actions, "ble_default", "connect");
    return actions;
}

struct iotm_tree_t *get_test_rule_param_tree()
{
    struct iotm_tree_t *tree = iotm_tree_new();
    iotm_tree_add_str(tree, "data", "DEADBEEF");
    iotm_tree_add_str(tree, "is_public", "true");
    return tree;
}

struct iotm_tree_t *get_fake_plugin_param_tree()
{
    // event was emitted with these parameters
    struct iotm_tree_t *params = iotm_tree_new();
    iotm_tree_add_str(params, "mac", "fakemac");
    iotm_tree_add_str(params, "serv_uuid", "fakeservuuid");
    iotm_tree_add_str(params, "serv_uuid", "anotherservuuid");
    iotm_tree_add_str(params, "char_uuid", "fakecharuuid");
    return params;
}

void test_add_matched_actions_cb(void)
{
    ds_list_t *dl = NULL;
    struct iotm_value_t val = 
    {
        .key = "mac",
        .value = "fakemac",
    };

    struct plugin_event_t plug = 
    {
        .params = get_fake_plugin_param_tree(),
    };

    struct iotm_rule rule = 
    {
        .actions = get_test_rule_action_tree(),
        .params = get_test_rule_param_tree(),
    };

    struct emit_data_t data = 
    {
        .plug = &plug,
        .rule = &rule,
        .actions = iotm_tree_new(),
        .match = true,
    };
    struct iotm_tree_t *actions = data.rule->actions;

    find_parameter_matches(dl, &val, &data);
    TEST_ASSERT_TRUE(data.match);

    iotm_tree_free(actions);
    iotm_tree_free(rule.params);
    iotm_tree_free(data.actions);
    iotm_tree_free(plug.params);
}


static struct iotm_rule first_rule;
static struct schema_IOT_Rule_Config first_row = 
{
    .filter_keys = 
    {
        "mac",
        "serv_uuid",
    },
    .filter = 
    {
        "testmac",
        "testserv",
    },
    .filter_len = 2,
    .actions_keys = 
    {
        "ble_default",
        "ble_sniffer",
    },
    .actions = 
    {
        "connect",
        "sniff_advert",
    },
    .actions_len = 2,
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


void fake_foreach_rule(struct iotm_event *self, void (*cb)(struct iotm_rule *, void *), void *ctx)
{
    first_rule.filter = schema2iotmtree(sizeof(first_row.filter_keys[0]),
            sizeof(first_row.filter[0]),
            first_row.filter_len,
            first_row.filter_keys,
            first_row.filter);

    first_rule.actions = schema2iotmtree(sizeof(first_row.actions_keys[0]),
            sizeof(first_row.actions[0]),
            first_row.actions_len,
            first_row.actions_keys,
            first_row.actions);

    first_rule.params = schema2iotmtree(sizeof(first_row.params_keys[0]),
            sizeof(first_row.params[0]),
            first_row.params_len,
            first_row.params_keys,
            first_row.params);
    cb(&first_rule, ctx);
}

static struct iotm_event fake_event = 
{
    .foreach_rule = fake_foreach_rule,
};
struct iotm_event *fake_get_event(struct iotm_session *sess, char *ev_key)
{
    return &fake_event;
}

static int handle_hit = 0;
void handle_test(struct iotm_session *sess, struct plugin_command_t *command)
{
    TEST_ASSERT_NOT_NULL(sess);
    TEST_ASSERT_NOT_NULL(command);
    handle_hit += 1;
}


void test_route_command(void)
{
    struct iotm_session fake_default_session = 
    {
        .name = "ble_default",
        .ops = 
        {
            .handle = handle_test,
        },
    };

    struct iotm_session fake_sniff_session = 
    {
        .name = "ble_sniff",
        .ops = 
        {
            .handle = handle_test,
        },
    };

    struct plugin_command_t default_cmd = 
    {
        .action = "connect",
    };



    // insert sessions
    ds_tree_t *sessions = iotm_get_sessions();
    ds_tree_insert(sessions, &fake_default_session, "ble_default");
    ds_tree_insert(sessions, &fake_sniff_session, "ble_sniff");

    route("ble_default", &default_cmd);
    route("ble_sniff", &default_cmd);

    TEST_ASSERT_EQUAL_INT(2, handle_hit);

    ds_tree_remove(sessions, &fake_default_session);
    ds_tree_remove(sessions, &fake_sniff_session);
}

struct iotm_tree_t *get_rule_filter_matching_tag()
{
    struct iotm_tree_t *tree = iotm_tree_new();
    iotm_tree_add_str(tree, "mac", "${othertag}");
    return tree;
}

struct iotm_tree_t *get_event_params()
{
    // event was emitted with these parameters
    struct iotm_tree_t *params = iotm_tree_new();
    iotm_tree_add_str(params, "mac", "matchthistag");
    iotm_tree_add_str(params, "serv_uuid", "testservuuid");
    return params;
}
void test_iterate_with_loaded_tag(void)
{
    struct iotm_mgr *mgr = iotm_get_mgr();
    struct iotm_tree_t *tree = mgr->tags;

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
        }, {
            .name = "othertag",
            .device_value_len = 1,
            .device_value = 
            {
                "matchthistag",
            },
            .cloud_value_len = 0,
        },
    };

    add_tag_to_tree(tree, &tags[0]);
    add_tag_to_tree(tree, &tags[1]);

    struct plugin_event_t plug = 
    {
        .params = get_event_params(),
    };

    struct iotm_rule rule = 
    {
        .actions = get_test_rule_action_tree(),
        .filter = get_rule_filter_matching_tag(),
    };

    struct emit_data_t data = 
    {
        .plug = &plug,
        .rule = &rule,
        .actions = iotm_tree_new(),
    };
    struct iotm_tree_t *actions = data.actions;

    get_matched_rule_actions(&rule, &data);

    TEST_ASSERT_EQUAL_INT(2, actions->len);

    iotm_tree_free(rule.actions);
    iotm_tree_free(rule.filter);
    iotm_tree_free(data.actions);
    iotm_tree_free(plug.params);

}

struct iotm_tree_t *get_rule_filter_no_match()
{
    struct iotm_tree_t *tree = iotm_tree_new();
    iotm_tree_add_str(tree, "mac", "nomatches");
    return tree;
}
void test_iterate_with_loaded_tag_no_match(void)
{
    struct iotm_mgr *mgr = iotm_get_mgr();
    struct iotm_tree_t *tree = mgr->tags;

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
        }, {
            .name = "othertag",
            .device_value_len = 1,
            .device_value = 
            {
                "dontmatchthistag",
            },
            .cloud_value_len = 0,
        },
    };

    add_tag_to_tree(tree, &tags[0]);
    add_tag_to_tree(tree, &tags[1]);

    struct plugin_event_t plug = 
    {
        .params = get_event_params(),
    };

    struct iotm_rule rule = 
    {
        .actions = get_test_rule_action_tree(),
        .filter = get_rule_filter_no_match(),
    };

    struct emit_data_t data = 
    {
        .plug = &plug,
        .rule = &rule,
        .actions = iotm_tree_new(),
    };
    struct iotm_tree_t *actions = data.actions;


    get_matched_rule_actions(&rule, &data);

    TEST_ASSERT_EQUAL_INT(0, actions->len);

    iotm_tree_free(rule.actions);
    iotm_tree_free(rule.filter);
    iotm_tree_free(data.actions);
    iotm_tree_free(plug.params);
}


struct iotm_tree_t *get_rule_filter_matching_one_tag()
{
    struct iotm_tree_t *tree = iotm_tree_new();
    iotm_tree_add_str(tree, "mac", "matchthistag");
    iotm_tree_add_str(tree, "serv_uuid", "dontmatchserv");
    return tree;
}
void test_iterate_with_loaded_tag_one_match(void)
{
    struct plugin_event_t plug;
    plug.params = iotm_tree_new();
    iotm_tree_add_str(plug.params, "mac", "18:93:D7:76:1D:AD");
    iotm_tree_add_str(plug.params, "char_uuid", "0000FFF1-0000-1000-8000-00805F9B34FB");
    iotm_tree_add_str(plug.params, "serv_uuid", "00001810-0000-1000-8000-00805F9B34FB");


    struct iotm_rule rule = 
    {
        .actions = get_test_rule_action_tree(),
        .filter = iotm_tree_new(),
    };

    iotm_tree_add_str(rule.filter, "mac", "18:93:D7:76:1D:AD");
    iotm_tree_add_str(rule.filter, "char_uuid", "00002A35-0000-1000-8000-00805F9B34FB");

    struct emit_data_t data = 
    {
        .plug = &plug,
        .rule = &rule,
        .actions = iotm_tree_new(),
    };

    get_matched_rule_actions(&rule, &data);
    TEST_ASSERT_EQUAL_INT(0, data.actions->len);

    iotm_tree_free(rule.actions);
    iotm_tree_free(rule.filter);
    iotm_tree_free(data.actions);
    iotm_tree_free(plug.params);

}

struct iotm_event *get_null_event(struct iotm_session *sess, char *ev_key)
{
    return NULL;
}
void test_emit_no_event(void)
{
    struct iotm_session sess =
    {
        .ops =
        {
            .get_event = get_null_event,
        },
    };
    struct plugin_event_t event;
    emit(&sess, &event);
    // should exit with no problems
}

void test_iot_router_suite()
{
    RUN_TEST(test_cmp_cb);
    RUN_TEST(test_add_matched_actions_cb);
    RUN_TEST(test_route_command);
    RUN_TEST(test_iterate_with_loaded_tag);
    RUN_TEST(test_iterate_with_loaded_tag_no_match);
    RUN_TEST(test_iterate_with_loaded_tag_one_match);
    RUN_TEST(test_emit_no_event);
}
