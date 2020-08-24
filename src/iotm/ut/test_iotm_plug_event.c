#include "test_iotm.h"
#include "iotm_plug_event.h"

void test_alloc_free_plugin_event(void)
{
    struct plugin_event_t *event = plugin_event_new();
    TEST_ASSERT_NOT_NULL_MESSAGE(event, "Should have allocated the plugin event without error");
    plugin_event_free(event);
}

void test_param_operations(void)
{

    char *key = "testparamkey";
    struct plugin_event_t *event;
    struct iotm_value_t *check;
    struct iotm_list_t *list;
    event = plugin_event_new();

    struct iotm_value_t *param = calloc(1, sizeof(struct iotm_value_t));
    param->key = strdup(key);
    param->value = strdup("testval");
    plugin_event_add(event, "test", param);

    check = NULL;
    check = plugin_event_get(event, "test");
    TEST_ASSERT_EQUAL_STRING("testval", check->value);

    struct iotm_value_t *other = calloc(1, sizeof(struct iotm_value_t));
    other->key = strdup(key);
    other->value = strdup("anotherval");

    plugin_event_add(event, "test", other);
    check = plugin_event_get(event, "test");
    TEST_ASSERT_EQUAL_STRING("anotherval", check->value);

    plugin_event_add_str(event, "newkey", "someval");
    list = plugin_event_get_list(event, "newkey");
    TEST_ASSERT_EQUAL_INT(1, list->len);

    plugin_event_add_str(event, "newkey", "otherval");
    list = plugin_event_get_list(event, "newkey");
    TEST_ASSERT_EQUAL_INT(2, list->len);

    plugin_event_free(event);
}



void test_iotm_plug_event_suite()
{
    test_alloc_free_plugin_event();
    test_param_operations();
}
