#include "test_iotm.h"
#include "iotm_list.h"
#include "iotm_list_private.h"

void test_alloc_iotm_list_free(void)
{
    char *key = "testkey";
    struct iotm_list_t *lists = iotm_list_new(key);
    TEST_ASSERT_NOT_NULL_MESSAGE(lists, "Should have allocated and initialized lists.");
    iotm_list_free(lists);
}

static int num_iterations = 0;
void iterator(ds_list_t *dlist, struct iotm_value_t *list, void *ctx)
{
    num_iterations += 1;
}

void test_iotm_list_add(void)
{
    char *key = "key";
    struct iotm_list_t *list = iotm_list_new(key);

    iotm_list_add_str(list, "testmac");
    iotm_list_add_str(list, "anotherval");
    iotm_list_add_str(list, "anotherval");
    iotm_list_add_str(list, "anotherval");
    iotm_list_add_str(list, "anotherval");
    iotm_list_add_str(list, "anotherval");
    iotm_list_add_str(list, "anotherval");
    iotm_list_add_str(list, "anotherval");
    iotm_list_add_str(list, "anotherval");
    iotm_list_add_str(list, "anotherval");
    iotm_list_add_str(list, "anotherval");
    iotm_list_add_str(list, "anotherval");

    iotm_list_foreach(list, iterator, NULL);

    TEST_ASSERT_EQUAL_INT_MESSAGE(12, num_iterations, "Should have iterated through the two elements in the list.");

    iotm_list_free(list);
}

void test_get_str_ops(void)
{
    char *key = "strkey";
    struct iotm_list_t *list = iotm_list_new(key);
    list->add_str(list, "value");
    char *check = list->get_head_str(list);
    TEST_ASSERT_EQUAL_STRING("value", check);

    list->free(list);
}

void test_iotm_set_add(void)
{
    char *key = "listkey";
    struct iotm_list_t *list = iotm_list_new(key);

    iotm_set_add_str(list, "test");
    iotm_set_add_str(list, "test");
    iotm_set_add_str(list, "another");
    TEST_ASSERT_EQUAL_INT(list->len, 2);
    iotm_list_free(list);
}

void test_is_in_list(void)
{
    char *key = "inlistkey";
    struct iotm_list_t *list = iotm_list_new(key);

    iotm_list_add_str(list, "test");
    iotm_list_add_str(list, "test");
    iotm_list_add_str(list, "another");
    TEST_ASSERT_TRUE(is_in_list_str(list, "test"));
    TEST_ASSERT_TRUE(is_in_list_str(list, "another"));
    TEST_ASSERT_FALSE(is_in_list_str(list, "notinlist"));
    iotm_list_free(list);
}

void test_list_suite()
{
    RUN_TEST(test_alloc_iotm_list_free);
    RUN_TEST(test_iotm_list_add);
    RUN_TEST(test_get_str_ops);
    RUN_TEST(test_iotm_set_add);
    RUN_TEST(test_is_in_list);
}
