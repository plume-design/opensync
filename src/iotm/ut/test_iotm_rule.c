#include "test_iotm.h"
#include "iotm_rule.h"
#include "iotm_tag.h"

void test_alloc_free_rule(void)
{
    struct schema_IOT_Rule_Config row =
    {
        .name = "ble_sniff_adv_and_connect",
        .event = "ble_advertise",
        .filter_keys =
        {
            "src"
        },
        .filter =
        {
            "*"
        },
        .filter_len = 1,
        .actions_keys =
        {
            "route",
        },
        .actions =
        {
            "ble-advert-parse",
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
    struct iotm_rule *rule = iotm_alloc_rule(&row);
    TEST_ASSERT_NOT_NULL_MESSAGE(rule, "Should have allocated a rule.");
    iotm_free_rule(rule);
}

void test_apply_rule_to_connected(void)
{
    struct iotm_rule *rule = NULL;
    struct schema_Openflow_Tag tag =
    {
        .name = CONNECT_TAG,
        .device_value_len = 1,
        .device_value =
        {
            "connectedmac",
        },
    };
    struct iotm_tree_t *tags = iotm_get_tags();
    add_tag_to_tree(tags, &tag);

    struct schema_IOT_Rule_Config ovsdb_rule =
    {
        .name = "newlyinstalledrule",
        .event = "ble_connect",
        .filter_keys =
        {
            "mac"
        },
        .filter_len = 1,
        .filter =
        { 
            "connectedmac",
        },
        .actions_len = 2,
        .actions_keys =
        {
            "first_plugin",
            "second_plugin",
        },
        .actions =
        {
            "some_action",
            "another_action",
        },
    };

    rule = iotm_alloc_rule(&ovsdb_rule);

    struct iotm_tree_t *routable_actions = iotm_tree_new();
    iotm_get_connected_routable_actions(rule, routable_actions);
    TEST_ASSERT_EQUAL_INT(2, routable_actions->len);

    iotm_tree_free(routable_actions);
    iotm_free_rule(rule);
}

void test_iotm_rule_suite()
{
    RUN_TEST(test_alloc_free_rule);
    RUN_TEST(test_apply_rule_to_connected);
}
