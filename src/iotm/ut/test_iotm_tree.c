#include "test_iotm.h"
#include "iotm_tree.h"
#include "iotm_data_types.h"

void test_alloc_iotm_tree_free(void)
{
    struct iotm_tree_t *tree = iotm_tree_new();
    TEST_ASSERT_NOT_NULL_MESSAGE(tree, "Should have allocated a tree.");
    tree->free(tree);
}

void test_add_to_tree(void)
{
    char *key = "addtreekey";
    struct iotm_tree_t *tree = iotm_tree_new();
    tree->add_val_str(tree, key, "someval");

    struct iotm_value_t *check = iotm_tree_get_single(tree, key);

    TEST_ASSERT_NOT_NULL_MESSAGE(check, "Should have found the value in the tree.");
    TEST_ASSERT_EQUAL_STRING("someval", check->value);

    tree->free(tree);
}

void test_filter_interactions()
{
    struct iotm_tree_t *tree = NULL;
    struct schema_IOT_Rule_Config row = 
    {
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
    };

    tree = schema2iotmtree(sizeof(row.filter_keys[0]),
            sizeof(row.filter[0]),
            row.filter_len,
            row.filter_keys,
            row.filter);

    TEST_ASSERT_NOT_NULL(tree);
    char *val = tree->get_val(tree, "src");
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", val);

    struct iotm_list_t *list = tree->get_list(tree, "src");
    TEST_ASSERT_EQUAL_INT(1, list->len);

    tree->free(tree);
}


void test_add_val_str(void)
{
    struct iotm_tree_t *tree = iotm_tree_new();
    tree->add_val_str(tree, "key1", "val1");
    tree->add_val_str(tree, "key1", "val2");
    tree->add_val_str(tree, "key2", "val3");

    struct iotm_list_t *check = tree->get_list(tree, "key1");
    TEST_ASSERT_EQUAL_INT(2, check->len);

    check = tree->get_list(tree, "key2");
    TEST_ASSERT_EQUAL_INT(1, check->len);

    tree->free(tree);
}

static int foreach_val_count = 0;
void foreach_val_test(ds_list_t *dl, struct iotm_value_t *value, void *ctx)
{
    foreach_val_count += 1;
}

void test_tree_val_iter(void)
{
    struct iotm_tree_t *tree = iotm_tree_new();
    tree->add_val_str(tree, "key1", "val1");
    tree->add_val_str(tree, "key1", "val2");
    tree->add_val_str(tree, "key2", "val3");
    tree->add_val_str(tree, "key2", "val3");
    tree->add_val_str(tree, "key2", "val3");
    tree->add_val_str(tree, "key2", "val3");
    tree->add_val_str(tree, "key2", "val3");
    tree->add_val_str(tree, "key2", "val3");
    tree->add_val_str(tree, "key2", "val3");
    tree->add_val_str(tree, "key2", "val3");
    tree->add_val_str(tree, "key2", "val3");
    tree->add_val_str(tree, "key2", "val3");
    tree->add_val_str(tree, "key2", "val3");

    tree->foreach_val(tree, foreach_val_test, NULL);
    TEST_ASSERT_EQUAL_INT(13, foreach_val_count);

    tree->free(tree);
}

void test_add_tree_set_str(void)
{
    struct iotm_tree_t *tree = iotm_tree_new();
    iotm_tree_set_add_str(tree, "one", "aval");
    iotm_tree_set_add_str(tree, "one", "aval");
    iotm_tree_set_add_str(tree, "one", "aval");
    iotm_tree_set_add_str(tree, "one", "aval");
    iotm_tree_set_add_str(tree, "one", "aval");
    iotm_tree_set_add_str(tree, "one", "anotherval");
    iotm_tree_set_add_str(tree, "two", "anotherval");
    iotm_tree_set_add_str(tree, "two", "anotherval");
    iotm_tree_set_add_str(tree, "two", "anotherval");
    iotm_tree_set_add_str(tree, "two", "anotherval");
    iotm_tree_set_add_str(tree, "two", "anotherval");
    iotm_tree_set_add_str(tree, "two", "anotherval");

    struct iotm_list_t *first = iotm_tree_find(tree, "one");
    TEST_ASSERT_EQUAL_INT(2, first->len);

    struct iotm_list_t *second = iotm_tree_find(tree, "two");
    TEST_ASSERT_EQUAL_INT(1, second->len);

    TEST_ASSERT_EQUAL_INT(3, tree->len);

    iotm_tree_free(tree);
}

void test_tree_concat_str(void)
{
    struct iotm_tree_t *src = iotm_tree_new();
    iotm_tree_add_str(src, "one", "1");
    iotm_tree_add_str(src, "one", "2");
    iotm_tree_add_str(src, "two", "3");

    struct iotm_tree_t *dst = iotm_tree_new();
    iotm_tree_add_str(dst, "some", "4");
    iotm_tree_add_str(dst, "one", "5");

    iotm_tree_concat_str(dst, src);

    struct iotm_list_t *check = iotm_tree_find(dst, "one");
    TEST_ASSERT_EQUAL_INT(check->len, 3);

    iotm_tree_free(src);
    iotm_tree_free(dst);

}

void test_iotm_tree_remove_list(void)
{
    struct iotm_tree_t *tree = iotm_tree_new();
    iotm_tree_add_str(tree, "one", "1");
    iotm_tree_add_str(tree, "one", "2");
    iotm_tree_add_str(tree, "two", "3");

    struct iotm_list_t *check = iotm_tree_find(tree, "one");
    TEST_ASSERT_EQUAL_INT(check->len, 2);

    iotm_tree_remove_list(tree, "one");
    check = iotm_tree_find(tree, "one");
    TEST_ASSERT_NULL_MESSAGE(check, "Should have removed the list from the tree.");

    iotm_tree_free(tree);

}

void test_iotm_tree_add_two_set(void)
{
    struct iotm_tree_t *tree = iotm_tree_new();
    iotm_tree_set_add_str(tree, "mac", "1234");
    iotm_tree_set_add_str(tree, "mac", "1234");

    TEST_ASSERT_EQUAL_INT(1, tree->len);

    iotm_tree_free(tree);
}

void test_iotm_tree_get_single_uint16(void)
{
		int err = -1;
		struct iotm_tree_t *tree = iotm_tree_new();
		iotm_tree_add_str(tree, "uuid", "0x0ad8");

		uint16_t check = 0x0000;
		err = iotm_tree_get_single_type(tree, "uuid", UINT16, &check);
		TEST_ASSERT_EQUAL_INT(0, err);
		TEST_ASSERT_EQUAL_UINT16(0x0ad8, check);
		iotm_tree_free(tree);
}

static int num_foreach_type_uint16 = 0;
void test_foreach_type_uint16(char *key, void *val, void *ctx)
{
    uint16_t check = *(uint16_t *)val;
	if (num_foreach_type_uint16 == 0) TEST_ASSERT_EQUAL_UINT16(0xffff, check);
	else TEST_ASSERT_EQUAL_UINT16(0x01a8, check);
	num_foreach_type_uint16++;
}
void test_iotm_tree_foreach_type(void)
{
	struct iotm_tree_t *tree = iotm_tree_new();
	iotm_tree_add_str(tree, "uuid", "0x01a8");
	iotm_tree_add_str(tree, "uuid", "0xffff");

	iotm_tree_foreach_type(tree, "uuid", UINT16, test_foreach_type_uint16, NULL);
	TEST_ASSERT_EQUAL_INT(2, num_foreach_type_uint16);
	iotm_tree_free(tree);
}

void test_iotm_tree_add_type(void)
{
    struct iotm_tree_t *tree = iotm_tree_new();
	uint8_t test = 0xab;
	iotm_tree_add_type(tree, "uuid", UINT8, &test);

	char *check = iotm_tree_get_single_str(tree, "uuid");
	TEST_ASSERT_EQUAL_STRING("AB", check);

	iotm_tree_free(tree);
}

void test_iotm_tree_set_add_type(void)
{
    struct iotm_tree_t *tree = iotm_tree_new();
	uint8_t test = 0xab;
	iotm_tree_set_add_type(tree, "uuid", UINT8, &test);
	iotm_tree_set_add_type(tree, "uuid", UINT8, &test);
	iotm_tree_set_add_type(tree, "uuid", UINT8, &test);
	TEST_ASSERT_EQUAL_INT(1, tree->len);

	iotm_tree_free(tree);
}

void test_tree_suite()
{
    RUN_TEST(test_alloc_iotm_tree_free);
    RUN_TEST(test_add_to_tree);
    RUN_TEST(test_filter_interactions);
    RUN_TEST(test_add_val_str);
    RUN_TEST(test_add_tree_set_str);
    RUN_TEST(test_tree_concat_str);
    RUN_TEST(test_iotm_tree_remove_list);
    RUN_TEST(test_iotm_tree_add_two_set);
	RUN_TEST(test_iotm_tree_get_single_uint16);
	RUN_TEST(test_iotm_tree_foreach_type);
	RUN_TEST(test_iotm_tree_add_type);
	RUN_TEST(test_iotm_tree_set_add_type);
}
