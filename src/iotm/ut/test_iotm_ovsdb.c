#include "test_iotm.h"
#include "iotm_ovsdb.h"
#include "iotm_ovsdb_private.h"

    void
test_add_iot_rule_config() 
{
    struct iotm_rule *cur;
    struct schema_IOT_Rule_Config row = 
    {
        .name = "ble_sniff_adv_and_connect",
        .event = "ble_advertise",
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
            "route",
        },
        .actions = 
        {
            "ble_connect",
            "ble-advert-parse",

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

    // Insert a row
    ovsdb_update_monitor_t mon;
    mon.mon_type = OVSDB_UPDATE_NEW;
    callback_IOT_Rule_Config(&mon, NULL, &row);

    cur = iotm_get_rule(row.name, row.event);
    TEST_ASSERT_NOT_NULL_MESSAGE(cur, "Should be able to insert a row.");


    struct schema_IOT_Rule_Config update = 
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

    // Update row
    mon.mon_type = OVSDB_UPDATE_MODIFY;
    callback_IOT_Rule_Config(&mon, NULL, &update);
    cur = iotm_get_rule(update.name, update.event);
    TEST_ASSERT_NOT_NULL_MESSAGE(cur, "Should be able to update a row that exists");

    mon.mon_type = OVSDB_UPDATE_DEL;
    callback_IOT_Rule_Config(&mon, &row, NULL);
    cur = iotm_get_rule(update.name, update.event);
    TEST_ASSERT_NULL_MESSAGE(cur, "Should remove a rule on delete.");
}

void test_iot_manager_row()
{
    struct schema_IOT_Manager_Config conf = 
    {
        .handler = "testadd",
        .plugin = "add_parser",
        .other_config_keys =
        {
            "mqtt_v",                       /* topic */
            "dso_init",                     /* plugin init routine */
            "policy_table",                 /* IOT policy entry */
        },
        .other_config =
        {
            "dev-test/ble_ovsdb_ut/topic_0", /* topic */
            "test_dso_init",                /* plugin init routine */
            "test_policy",                  /* IOT policy entry */
        },
        .other_config_len = 3,
    };

    ovsdb_update_monitor_t mon;
    mon.mon_type = OVSDB_UPDATE_NEW;
    callback_IOT_Manager_Config( &mon, NULL, &conf);

    mon.mon_type = OVSDB_UPDATE_MODIFY;
    strcpy(conf.plugin, "new_plugin");
    callback_IOT_Manager_Config(&mon, NULL, &conf);

    mon.mon_type = OVSDB_UPDATE_DEL;
    callback_IOT_Manager_Config( &mon, &conf, NULL);
}

void test_add_openflow_two_tags(void)
{
    struct schema_Openflow_Tag rows[2] = 
    {
        {
            .name = "dev_mytag",
            .cloud_value = 
            {
                "firstval",
                "secondval",
            },
            .cloud_value_len = 2,
            .device_value = 
            {
                "devvalone",
            },
            .device_value_len = 1,
        }, {
            .name = "dev_anothertag",
            .cloud_value_len = 0,
            .device_value = 
            {
                "somedevval",
                "anotherdevval",
            },
            .device_value_len = 2,
        }
    };

    ovsdb_update_monitor_t mon = 
    {
        .mon_type = OVSDB_UPDATE_NEW,
    };

    callback_Openflow_Tag(&mon, NULL, &rows[0]);
    callback_Openflow_Tag(&mon, NULL, &rows[1]);
}

void test_add_tag_one_device_value(void)
{
    struct schema_Openflow_Tag row = 
    {
        .name = "test_tag",
        .device_value =
        {
            "testmac",
        },
        .device_value_len = 1,
    };

    ovsdb_update_monitor_t mon = 
    {
        .mon_type = OVSDB_UPDATE_NEW,
    };
    callback_Openflow_Tag(&mon, NULL, &row);
}

void test_awlan_load_unload(void)
{

    struct iotm_mgr *mgr = iotm_get_mgr();
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
    TEST_ASSERT_NOT_NULL(mgr->mqtt_headers);
    iotm_rm_awlan_headers();
    TEST_ASSERT_NULL(mgr->mqtt_headers);
}

void test_install_multiple_tags(void)
{
    struct schema_Openflow_Tag tags[2] =
    {
        {
            .name = "test_tag",
            .device_value =
            {
                "firstval",
                "secondval",
                "thirdval",
            },
            .device_value_len = 3,
        },
        {
            .name = "other_tag",
            .device_value =
            {
                "firstval",
                "secondval",
                "thirdval",
            },
            .device_value_len = 3,
        }
    };

    ovsdb_update_monitor_t mon = 
    {
        .mon_type = OVSDB_UPDATE_NEW,
    };

    callback_Openflow_Tag(&mon, NULL, &tags[0]);
    callback_Openflow_Tag(&mon, NULL, &tags[1]);

    struct schema_IOT_Rule_Config update = 
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

    // Update row
    mon.mon_type = OVSDB_UPDATE_NEW;
    callback_IOT_Rule_Config(&mon, NULL, &update);


    struct iotm_mgr *mgr = iotm_get_mgr();
    TEST_ASSERT_EQUAL_INT(6, mgr->tags->len);
}

void test_iot_rule_ovsdb_suite()
{
    // Rule config test suite
    RUN_TEST(test_add_iot_rule_config);
    RUN_TEST(test_iot_manager_row);
    RUN_TEST(test_add_openflow_two_tags);
    RUN_TEST(test_add_tag_one_device_value);
    RUN_TEST(test_awlan_load_unload);
    RUN_TEST(test_install_multiple_tags);
}
