#include "test_iotm.h"
#include "iotm_session.h"
#include "iotm_ovsdb.h"
#include "iotm_ovsdb_private.h"
#include "iotm_rule.h"
#include "iotm_event.h"
#include "iotm_event_private.h"

void test_event_insert_remove_cleaning()
{
    struct iotm_event *event = NULL;
    struct schema_IOT_Rule_Config rule = 
    {
        .name = "service_whitelist",
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
            "command",
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
    iotm_add_rule(&rule);
    event = iotm_event_get(rule.event);
    TEST_ASSERT_NOT_NULL_MESSAGE(event, "Should have found the event after adding the rule.");

    iotm_delete_rule(&rule);
    event = iotm_event_get(rule.event);
    TEST_ASSERT_NULL_MESSAGE(event, "Should have removed the event now that the rule has been removed.");
}

void test_alloc_plugin_event_free(void)
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
    struct iotm_event *check = iotm_event_alloc(&row);
    TEST_ASSERT_NOT_NULL_MESSAGE(check, "Should have initialized an event.");
    iotm_event_free(check);
}

static int foreach_filter_counter = 0;
void test_foreach_filter_cb(ds_list_t *dl, struct iotm_value_t *val, void *ctx)
{
    foreach_filter_counter += 1;
    switch(foreach_filter_counter) {
        case 1:
            TEST_ASSERT_EQUAL_STRING("*", val->value);
            break;
        case 2:
            TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", val->value);
            break;
        case 3:
            TEST_ASSERT_EQUAL_STRING("12345", val->value);
            break;
        default:
            TEST_ASSERT_TRUE_MESSAGE(false, "Should have hit a case, instead hit default.");
    }
}

void test_foreach_filter(void)
{
    struct schema_IOT_Rule_Config row = 
    {
        .name = "ble_sniff_adv_and_connect",
        .event = "ble_advertise",
        .filter_keys = 
        {
            "mac",
            "mac",
            "serv_uuid",
        },
        .filter = 
        {
            "*",
            "AA:BB:CC:DD:EE:FF",
            "12345",
        },
        .filter_len = 3,
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

    iotm_add_rule(&row);

    struct iotm_event *check = iotm_event_get("ble_advertise");

    check->foreach_filter(check, test_foreach_filter_cb, NULL);
    TEST_ASSERT_EQUAL_INT(3, foreach_filter_counter);

    iotm_delete_rule(&row);
}

static int unique_filter_counter = 0;
void test_unique_filter_cb(ds_list_t *dl, struct iotm_value_t *val, void *ctx)
{
    unique_filter_counter += 1;
}

void test_unique_filter_counter()
{
    struct schema_IOT_Rule_Config rows[2] = 
    {
        {
            .name = "ble_sniff_adv_and_connect",
            .event = "ble_advertise",
            .filter_keys = 
            {
                "mac",
                "mac",
                "serv_uuid",
            },
            .filter = 
            {
                "*",
                "AA:BB:CC:DD:EE:FF",
                "12345",
            },
            .filter_len = 3,
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
        },
        {
            .name = "ble_another_rule",
            .event = "ble_advertise",
            .filter_keys = 
            {
                "mac",
                "mac",
                "serv_uuid",
            },
            .filter = 
            {
                "different",
                "AA:BB:CC:DD:EE:FF", // same
                "12345", // same
            },
            .filter_len = 3,
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

        }
    };

    struct schema_Openflow_Tag tag =
    {
        .name = "test_tag",
        .device_value =
        {
            "sometestvalue"
        },
        .device_value_len = 1,
    };

    ovsdb_update_monitor_t mon = 
    {
        .mon_type = OVSDB_UPDATE_NEW,
    };

    callback_Openflow_Tag(&mon, NULL, &tag);
    iotm_add_rule(&rows[0]);
    iotm_add_rule(&rows[1]);

    struct iotm_event *check = iotm_event_get("ble_advertise");

    foreach_unique_filter_in_event(check, test_unique_filter_cb, NULL);
    TEST_ASSERT_EQUAL_INT(4, unique_filter_counter);

    iotm_delete_rule(&rows[0]);
    iotm_delete_rule(&rows[1]);
}

void test_iterating_over_tags(void)
{
    struct schema_IOT_Rule_Config rows[2] = 
    {
        {
            .name = "ble_sniff_adv_and_connect",
            .event = "ble_advertise",
            .filter_keys = 
            {
                "mac",
            },
            .filter = 
            {
                "${test_tag}",
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
        },
        {
            .name = "other_rule",
            .event = "ble_advertise",
            .filter_keys = 
            {
                "mac",
            },
            .filter = 
            {
                "${test_tag}",
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
        }
    };

    struct schema_Openflow_Tag tag =
    {
        .name = "test_tag",
        .device_value =
        {
            "firstval",
            "secondval",
            "thirdval",
        },
        .device_value_len = 3,
    };

    ovsdb_update_monitor_t mon = 
    {
        .mon_type = OVSDB_UPDATE_NEW,
    };

    callback_IOT_Rule_Config(&mon, NULL, &rows[0]);
    callback_IOT_Rule_Config(&mon, NULL, &rows[1]);
    callback_Openflow_Tag(&mon, NULL, &tag);

    struct iotm_mgr *mgr = iotm_get_mgr();
    TEST_ASSERT_EQUAL_INT(3, mgr->tags->len);

    struct iotm_event *check = iotm_event_get("ble_advertise");
    TEST_ASSERT_NOT_NULL(check);
    unique_filter_counter = 0;
    check->foreach_filter(check, test_unique_filter_cb, NULL);
    TEST_ASSERT_EQUAL_INT(3, unique_filter_counter);

    mon.mon_type = OVSDB_UPDATE_DEL;
    callback_IOT_Rule_Config(&mon, &rows[0], NULL);
    callback_IOT_Rule_Config(&mon, &rows[1], NULL);
}

void test_event_suite()
{
    RUN_TEST(test_alloc_plugin_event_free);
    RUN_TEST(test_event_insert_remove_cleaning);
    RUN_TEST(test_foreach_filter);
    RUN_TEST(test_unique_filter_counter);
    RUN_TEST(test_iterating_over_tags);
}
