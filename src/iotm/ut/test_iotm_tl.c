#include "test_iotm.h"
#include "iotm_tl.h"

void test_alloc_free(void)
{
    struct tl_context_tree_t *tree = tl_tree_new();
	TEST_ASSERT_NOT_NULL(tree);
	tl_tree_free(tree);
}

void test_get_context(void)
{
    struct tl_context_tree_t *tree = tl_tree_new();
	void **ctx = NULL;

	ctx = tree->get(tree, "testkey");
	TEST_ASSERT_NULL(*ctx);
	*ctx = strdup("test");

	ctx = tree->get(tree, "testkey");
	TEST_ASSERT_EQUAL_STRING("test", *ctx);
	free(*ctx);

	tl_tree_free(tree);
}

void test_iot_tl_suite()
{
    RUN_TEST(test_alloc_free);
	RUN_TEST(test_get_context);
}
