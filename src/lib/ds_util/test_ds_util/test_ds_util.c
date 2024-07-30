/*
Copyright (c) 2015, Plume Design Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Plume Design Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Plume Design Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <unistd.h>

#include "ds_util.h"
#include "log.h"
#include "unit_test_utils.h"
#include "unity.h"

log_severity_t opt_severity = LOG_SEVERITY_WARN;

void dump_map_str(ds_map_str_t *map)
{
    ds_map_str_iter_t iter;
    LOGN("size: %d", ds_map_str_size(map));
    ds_map_str_foreach(map, iter)
    {
        LOGN("\"%s\"=\"%s\"", iter.key, iter.val);
    }
    LOGN("---\n");
}

void test_ds_map_str(void)
{
    ds_map_str_t *map = NULL;
    ds_map_str_t *map2 = NULL;
    ds_map_str_iter_t iter;

    int schema_len = 3;
    char schema_keys[3][32] = {"ABC", "GHI", "DEF"};
    char schema[3][32] = {"abc", "ghi", "def"};
    char *v;

    map = ds_map_str_new();
    TEST_ASSERT_TRUE(map != NULL);
    TEST_ASSERT_TRUE(ds_map_str_insert(map, "one", "111"));
    TEST_ASSERT_TRUE(ds_map_str_insert(map, "two", "222"));
    TEST_ASSERT_TRUE(ds_map_str_insert(map, "three", "333"));
    TEST_ASSERT_TRUE(!ds_map_str_insert(map, "null", NULL));
    TEST_ASSERT_TRUE(!ds_map_str_insert(map, NULL, "null"));
    TEST_ASSERT_TRUE(!ds_map_str_insert(map, NULL, NULL));
    TEST_ASSERT_TRUE(ds_map_str_size(map) == 3);
    TEST_ASSERT_TRUE(!ds_map_str_empty(map));
    dump_map_str(map);
    TEST_ASSERT_TRUE(ds_map_str_find(map, "none", NULL) == false);
    TEST_ASSERT_TRUE(ds_map_str_find(map, "one", NULL) == true);
    TEST_ASSERT_TRUE(!ds_map_str_insert(map, "one", "1"));
    TEST_ASSERT_TRUE(ds_map_str_find(map, "one", &v) && strcmp(v, "111") == 0);
    TEST_ASSERT_TRUE(ds_map_str_set(map, "one", "1"));
    TEST_ASSERT_TRUE(ds_map_str_find(map, "one", &v) && strcmp(v, "1") == 0);
    TEST_ASSERT_TRUE(!ds_map_str_remove(map, "none"));
    TEST_ASSERT_TRUE(ds_map_str_remove(map, "two"));
    TEST_ASSERT_TRUE(ds_map_str_set(map, "two", "22"));
    dump_map_str(map);
    TEST_ASSERT_TRUE(ds_map_str_clear(map));
    TEST_ASSERT_TRUE(ds_map_str_size(map) == 0);
    TEST_ASSERT_TRUE(ds_map_str_empty(map));
    dump_map_str(map);
    TEST_ASSERT_TRUE(ds_map_str_insert_schema_map(map, schema, 0, NULL));
    dump_map_str(map);
    map2 = ds_map_str_new();
    TEST_ASSERT_TRUE(ds_map_str_insert(map2, "ABC", "abc"));
    TEST_ASSERT_TRUE(ds_map_str_insert(map2, "DEF", "def"));
    TEST_ASSERT_TRUE(ds_map_str_insert(map2, "GHI", "ghi"));
    TEST_ASSERT_TRUE(ds_map_str_compare(map, map2) == 0);
    TEST_ASSERT_TRUE(ds_map_str_remove(map2, "ABC"));
    TEST_ASSERT_TRUE(ds_map_str_compare(map, map2) != 0);
    ds_map_str_foreach(map, iter)
    {
        TEST_ASSERT_TRUE(ds_map_str_iter_remove(&iter));
    }
    dump_map_str(map);
    TEST_ASSERT_TRUE(ds_map_str_delete(&map));
    TEST_ASSERT_TRUE(map == NULL);
    TEST_ASSERT_TRUE(ds_map_str_delete(&map2));
}

void dump_map_int(ds_map_int_t *map)
{
    ds_map_int_iter_t iter;
    LOGN("size: %d", ds_map_int_size(map));
    ds_map_int_foreach(map, iter)
    {
        LOGN("%d=%d", iter.key, iter.val);
    }
    LOGN("---\n");
}

void test_ds_map_int(void)
{
    ds_map_int_t *map = NULL;
    ds_map_int_t *map2 = NULL;
    ds_map_int_iter_t iter;

    int schema_len = 3;
    int schema_keys[3] = {111, 222, 333};
    int schema[3] = {1000, 2000, 3000};
    int v;

    map = ds_map_int_new();
    TEST_ASSERT_TRUE(map != NULL);
    TEST_ASSERT_TRUE(ds_map_int_insert(map, 1, 111));
    TEST_ASSERT_TRUE(ds_map_int_insert(map, 2, 222));
    TEST_ASSERT_TRUE(ds_map_int_insert(map, 3, 333));
    TEST_ASSERT_TRUE(ds_map_int_insert(map, 0, 0));
    TEST_ASSERT_TRUE(ds_map_int_insert(map, -1, -111));
    TEST_ASSERT_TRUE(ds_map_int_size(map) == 5);
    TEST_ASSERT_TRUE(!ds_map_int_empty(map));
    dump_map_int(map);
    TEST_ASSERT_TRUE(ds_map_int_find(map, -2, NULL) == false);
    TEST_ASSERT_TRUE(ds_map_int_find(map, -1, NULL) == true);
    TEST_ASSERT_TRUE(ds_map_int_find(map, 0, NULL) == true);
    TEST_ASSERT_TRUE(ds_map_int_find(map, 1, NULL) == true);
    TEST_ASSERT_TRUE(ds_map_int_find(map, 99, NULL) == false);
    TEST_ASSERT_TRUE(ds_map_int_find(map, 0, &v) && (v == 0));
    TEST_ASSERT_TRUE(!ds_map_int_insert(map, 1, 1));
    TEST_ASSERT_TRUE(ds_map_int_find(map, 1, &v) && (v == 111));
    TEST_ASSERT_TRUE(ds_map_int_set(map, 1, 1));
    TEST_ASSERT_TRUE(ds_map_int_find(map, 1, &v) && (v == 1));
    TEST_ASSERT_TRUE(ds_map_int_remove(map, 0));
    TEST_ASSERT_TRUE(!ds_map_int_remove(map, 99));
    TEST_ASSERT_TRUE(ds_map_int_remove(map, 2));
    TEST_ASSERT_TRUE(ds_map_int_set(map, 2, 22));
    dump_map_int(map);
    TEST_ASSERT_TRUE(ds_map_int_clear(map));
    TEST_ASSERT_TRUE(ds_map_int_size(map) == 0);
    TEST_ASSERT_TRUE(ds_map_int_empty(map));
    dump_map_int(map);
    TEST_ASSERT_TRUE(ds_map_int_insert_schema_map(map, schema, 0, NULL));
    dump_map_int(map);
    map2 = ds_map_int_new();
    TEST_ASSERT_TRUE(ds_map_int_insert(map2, 111, 1000));
    TEST_ASSERT_TRUE(ds_map_int_insert(map2, 222, 2000));
    TEST_ASSERT_TRUE(ds_map_int_insert(map2, 333, 3000));
    TEST_ASSERT_TRUE(ds_map_int_compare(map, map2) == 0);
    TEST_ASSERT_TRUE(ds_map_int_remove(map2, 111));
    TEST_ASSERT_TRUE(ds_map_int_compare(map, map2) != 0);
    ds_map_int_foreach(map, iter)
    {
        TEST_ASSERT_TRUE(ds_map_int_iter_remove(&iter));
    }
    dump_map_int(map);
    TEST_ASSERT_TRUE(ds_map_int_delete(&map));
    TEST_ASSERT_TRUE(map == NULL);
    TEST_ASSERT_TRUE(ds_map_int_delete(&map2));
}

void dump_map_str_int(ds_map_str_int_t *map)
{
    ds_map_str_int_iter_t iter;
    LOGN("size: %d", ds_map_str_int_size(map));
    ds_map_str_int_foreach(map, iter) LOGN("\"%s\"=%d", iter.key, iter.val);
    LOGN("---\n");
}

void test_ds_map_str_int(void)
{
    ds_map_str_int_t *map = NULL;
    int schema_len = 3;
    char schema_keys[3][32] = {"ABC", "GHI", "DEF"};
    int schema[3] = {111, 222, 333};
    map = ds_map_str_int_new();
    TEST_ASSERT_TRUE(ds_map_str_int_insert_schema_map(map, schema, 0, NULL));
    dump_map_str_int(map);
    TEST_ASSERT_TRUE(ds_map_str_int_delete(&map));
    TEST_ASSERT_TRUE(map == NULL);
}

void dump_map_int_str(ds_map_int_str_t *map)
{
    ds_map_int_str_iter_t iter;
    LOGN("size: %d", ds_map_int_str_size(map));
    ds_map_int_str_foreach(map, iter) LOGN("%d=\"%s\"", iter.key, iter.val);
    LOGN("---\n");
}

void test_ds_map_int_str(void)
{
    ds_map_int_str_t *map = NULL;
    int schema_len = 3;
    int schema_keys[3] = {111, 222, 333};
    char schema[3][32] = {"abc", "ghi", "def"};
    map = ds_map_int_str_new();
    TEST_ASSERT_TRUE(ds_map_int_str_insert_schema_map(map, schema, 0, NULL));
    dump_map_int_str(map);
    TEST_ASSERT_TRUE(ds_map_int_str_delete(&map));
    TEST_ASSERT_TRUE(map == NULL);
}

void dump_set_str(ds_set_str_t *set)
{
    ds_set_str_iter_t iter;
    LOGN("size: %d", ds_set_str_size(set));
    ds_set_str_foreach(set, iter)
    {
        LOGN("- \"%s\"", iter.key);
    }
    LOGN("---\n");
}

void test_ds_set_str(void)
{
    ds_set_str_t *set = NULL;
    ds_set_str_t *set2 = NULL;
    ds_set_str_iter_t iter;

    int schema_len = 3;
    char schema[3][32] = {"abc", "ghi", "def"};

    set = ds_set_str_new();
    TEST_ASSERT_TRUE(set != NULL);
    TEST_ASSERT_TRUE(ds_set_str_insert(set, "one"));
    TEST_ASSERT_TRUE(ds_set_str_insert(set, "two"));
    TEST_ASSERT_TRUE(ds_set_str_insert(set, "three"));
    TEST_ASSERT_TRUE(ds_set_str_insert(set, "four"));
    TEST_ASSERT_TRUE(ds_set_str_size(set) == 4);
    TEST_ASSERT_TRUE(!ds_set_str_empty(set));
    dump_set_str(set);
    TEST_ASSERT_TRUE(ds_set_str_find(set, "none") == false);
    TEST_ASSERT_TRUE(ds_set_str_find(set, "one") == true);
    TEST_ASSERT_TRUE(!ds_set_str_insert(set, "one"));
    TEST_ASSERT_TRUE(ds_set_str_find(set, "one") == true);
    TEST_ASSERT_TRUE(!ds_set_str_remove(set, "none"));
    TEST_ASSERT_TRUE(ds_set_str_remove(set, "two"));
    TEST_ASSERT_TRUE(ds_set_str_insert(set, "two2"));
    dump_set_str(set);
    TEST_ASSERT_TRUE(ds_set_str_clear(set));
    TEST_ASSERT_TRUE(ds_set_str_size(set) == 0);
    TEST_ASSERT_TRUE(ds_set_str_empty(set));
    dump_set_str(set);
    TEST_ASSERT_TRUE(ds_set_str_insert_schema_set(set, schema, 0, NULL));
    dump_set_str(set);
    set2 = ds_set_str_new();
    TEST_ASSERT_TRUE(ds_set_str_insert(set2, "abc"));
    TEST_ASSERT_TRUE(ds_set_str_insert(set2, "def"));
    TEST_ASSERT_TRUE(ds_set_str_insert(set2, "ghi"));
    TEST_ASSERT_TRUE(ds_set_str_compare(set, set2) == 0);
    TEST_ASSERT_TRUE(ds_set_str_remove(set2, "abc"));
    TEST_ASSERT_TRUE(ds_set_str_compare(set, set2) != 0);
    ds_set_str_foreach(set, iter)
    {
        TEST_ASSERT_TRUE(ds_set_str_iter_remove(&iter));
    }
    dump_set_str(set);
    TEST_ASSERT_TRUE(ds_set_str_delete(&set));
    TEST_ASSERT_TRUE(set == NULL);
    TEST_ASSERT_TRUE(ds_set_str_delete(&set2));
}

void dump_set_int(ds_set_int_t *set)
{
    ds_set_int_iter_t iter;
    LOGN("size: %d", ds_set_int_size(set));
    ds_set_int_foreach(set, iter)
    {
        LOGN("- %d", iter.key);
    }
    LOGN("---\n");
}

void test_ds_set_int(void)
{
    ds_set_int_t *set = NULL;
    ds_set_int_t *set2 = NULL;
    ds_set_int_iter_t iter;

    int schema_len = 3;
    int schema[3] = {111, 222, 333};

    set = ds_set_int_new();
    TEST_ASSERT_TRUE(set != NULL);
    TEST_ASSERT_TRUE(ds_set_int_insert(set, 0));
    TEST_ASSERT_TRUE(ds_set_int_insert(set, 1));
    TEST_ASSERT_TRUE(ds_set_int_insert(set, 2));
    TEST_ASSERT_TRUE(ds_set_int_insert(set, 3));
    TEST_ASSERT_TRUE(ds_set_int_insert(set, 4));
    TEST_ASSERT_TRUE(ds_set_int_size(set) == 5);
    TEST_ASSERT_TRUE(!ds_set_int_empty(set));
    dump_set_int(set);
    TEST_ASSERT_TRUE(ds_set_int_find(set, 99) == false);
    TEST_ASSERT_TRUE(ds_set_int_find(set, 0) == true);
    TEST_ASSERT_TRUE(ds_set_int_find(set, 1) == true);
    TEST_ASSERT_TRUE(!ds_set_int_insert(set, 1));
    TEST_ASSERT_TRUE(ds_set_int_find(set, 1) == true);
    TEST_ASSERT_TRUE(!ds_set_int_remove(set, 99));
    TEST_ASSERT_TRUE(ds_set_int_remove(set, 2));
    TEST_ASSERT_TRUE(ds_set_int_insert(set, 22));
    dump_set_int(set);
    TEST_ASSERT_TRUE(ds_set_int_clear(set));
    TEST_ASSERT_TRUE(ds_set_int_size(set) == 0);
    TEST_ASSERT_TRUE(ds_set_int_empty(set));
    dump_set_int(set);
    TEST_ASSERT_TRUE(ds_set_int_insert_schema_set(set, schema, 0, NULL));
    dump_set_int(set);
    set2 = ds_set_int_new();
    TEST_ASSERT_TRUE(ds_set_int_insert(set2, 111));
    TEST_ASSERT_TRUE(ds_set_int_insert(set2, 222));
    TEST_ASSERT_TRUE(ds_set_int_insert(set2, 333));
    TEST_ASSERT_TRUE(ds_set_int_compare(set, set2) == 0);
    TEST_ASSERT_TRUE(ds_set_int_remove(set2, 111));
    TEST_ASSERT_TRUE(ds_set_int_compare(set, set2) != 0);
    ds_set_int_foreach(set, iter)
    {
        TEST_ASSERT_TRUE(ds_set_int_iter_remove(&iter));
    }
    dump_set_int(set);
    TEST_ASSERT_TRUE(ds_set_int_delete(&set));
    TEST_ASSERT_TRUE(set == NULL);
    TEST_ASSERT_TRUE(ds_set_int_delete(&set2));
}

void test_ds_util(void)
{
    RUN_TEST(test_ds_map_str);
    RUN_TEST(test_ds_map_int);
    RUN_TEST(test_ds_map_str_int);
    RUN_TEST(test_ds_map_int_str);
    RUN_TEST(test_ds_set_str);
    RUN_TEST(test_ds_set_int);
}

int main(int argc, char *argv[])
{
    int opt;
    char *test_name = "test_ds_util";

    log_open(test_name, 0);
    while ((opt = getopt(argc, argv, "v")) != -1)
    {
        switch (opt)
        {
            case 'v':
                if (opt_severity < LOG_SEVERITY_TRACE) opt_severity++;
                break;
        }
    }

    ut_init(test_name, NULL, NULL);
    log_severity_set(opt_severity);
    test_ds_util();
    ut_fini();

    return 0;
}
