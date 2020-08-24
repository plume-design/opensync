#include "test_iotm.h"
#include "iotm_plug_command.h"

void test_alloc_plugin_command_free(void)
{
    struct plugin_command_t *cmd = NULL;

    cmd = plugin_command_new();
    TEST_ASSERT_NOT_NULL_MESSAGE(cmd, "Should have allocated a command.");
    TEST_ASSERT_NOT_NULL(cmd->ops.get_params);
    TEST_ASSERT_NOT_NULL(cmd->ops.get_param);
    plugin_command_free(cmd);
}

void test_add_param(void)
{
    struct plugin_command_t *cmd = NULL;
    char *check = NULL;

    cmd = plugin_command_new();

    char *key = "char_uuid";

    struct iotm_value_t *param = calloc(1, sizeof(struct iotm_value_t));
    param->key = strdup(key);
    param->value = strdup("fakeuuid");

    plugin_command_add(cmd, key, param);

    check = plugin_command_get(cmd, key);
    TEST_ASSERT_EQUAL_STRING("fakeuuid", check);

    plugin_command_free(cmd);
}

void test_plugin_command_get_no_param(void)
{
    struct plugin_command_t *cmd = NULL;
    char *check = NULL;

    cmd = plugin_command_new();

    check = plugin_command_get(cmd, "somekey");
    TEST_ASSERT_NULL_MESSAGE(check, "Shouldn't have gotten value in empty list");

    plugin_command_free(cmd);
}


void test_iotm_plug_command_suite()
{
    RUN_TEST(test_alloc_plugin_command_free);
    RUN_TEST(test_add_param);
    RUN_TEST(test_plugin_command_get_no_param);
}
