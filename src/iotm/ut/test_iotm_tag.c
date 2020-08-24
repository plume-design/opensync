#include "test_iotm.h"
#include "iotm_tag.h"
#include "iotm_tag_private.h"

void test_loading_tags(void)
{
    struct iotm_tree_t *tree = iotm_tree_new();

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
            .device_value_len = 2,
            .device_value = 
            {
                "AA:BB:CC:DD:EE:FF",
                "*",
            },
            .cloud_value_len = 0,
        },
    };

    add_tag_to_tree(tree, &tags[0]);
    struct iotm_list_t *mytag = iotm_tree_find(tree, "mytag");
    TEST_ASSERT_EQUAL_INT(mytag->len, 3);

    add_tag_to_tree(tree, &tags[1]);
    struct iotm_list_t *other = iotm_tree_find(tree, "othertag");
    TEST_ASSERT_EQUAL_INT(other->len, 2);

    iotm_tree_free(tree);
}

void test_str_has_template(void)
{
    char *str_with_template = "${template_here}";
    char *str_without_template = "notemplate";

    TEST_ASSERT_TRUE_MESSAGE(str_has_template(str_with_template), "Should have detected a template");
    TEST_ASSERT_TRUE_MESSAGE(!str_has_template(str_without_template), "Should have detected a template");
}

void test_tag_extract_vars(void)
{
    struct iotm_list_t *vars = iotm_list_new("vars");

    tag_extract_vars(
            "$[test],$[another]",
            TEMPLATE_VAR_CHAR,
            TEMPLATE_GROUP_BEGIN,
            TEMPLATE_GROUP_END,
            OM_TLE_FLAG_GROUP,
            vars);

    TEST_ASSERT_EQUAL_INT(2, vars->len);
    iotm_list_free(vars);
}

void test_add_several_and_remove(void)
{
    struct schema_Openflow_Tag tags[3] =
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
        {
            .name = "thirdtag",
            .device_value_len = 1,
            .device_value =
            {
                "*"
            },
            .cloud_value_len = 0,
        }
    };
    struct iotm_tree_t *tree = iotm_tree_new();

    add_tag_to_tree(tree, &tags[0]);
    TEST_ASSERT_EQUAL_INT(3, tree->len);

    add_tag_to_tree(tree, &tags[1]);
    TEST_ASSERT_EQUAL_INT(5, tree->len);

    add_tag_to_tree(tree, &tags[2]);


    TEST_ASSERT_EQUAL_INT(6, tree->len);

    remove_tag_from_tree(tree, &tags[0]);
    TEST_ASSERT_EQUAL_INT(3, tree->len);

    remove_tag_from_tree(tree, &tags[1]);
    TEST_ASSERT_EQUAL_INT(1, tree->len);

    remove_tag_from_tree(tree, &tags[2]);
    TEST_ASSERT_EQUAL_INT(0, tree->len);

    iotm_tree_free(tree);
}

void test_iot_tag_suite()
{
    RUN_TEST(test_loading_tags);
    RUN_TEST(test_str_has_template);
    RUN_TEST(test_tag_extract_vars);
    RUN_TEST(test_add_several_and_remove);
}
