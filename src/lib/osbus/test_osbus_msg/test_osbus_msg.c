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
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <values.h>
#include <limits.h>
#include <libgen.h>
#include <jansson.h>

#include "kconfig.h"
#include "osbus_msg.h"
#include "log.h"
#include "json_util.h"
#include "unity.h"
#include "unit_test_utils.h"

#ifdef CONFIG_OSBUS_UBUS
#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include "osbus_msg_ubus.h"
#endif

#ifdef CONFIG_OSBUS_RBUS
#include <rbus.h>
#include "osbus_msg_rbus.h"
#endif

osbus_msg_t *g_msg = NULL;
log_severity_t opt_severity = LOG_SEVERITY_WARN;

void data_test_basic(void)
{
    osbus_msg_t *d;
    osbus_msg_t *e;
    char *str;
    int64_t max_int64 = 0x7FFFFFFFFFFFFFFF; // 9223372036854775807
    uint64_t max_uint64 = 0xFFFFFFFFFFFFFFFF;
    unsigned int uint_max = UINT_MAX;
    bool res = true;
    LOGN("uint_max = %u\n", uint_max);
    LOGN("max_int64 = %"PRIx64" %"PRId64"\n", max_int64, max_int64);
    LOGN("max_uint64 = %"PRIx64" %"PRIu64"\n", max_uint64, max_uint64);
    LOGN("\n");
    d = osbus_msg_new_object();
    TEST_ASSERT_TRUE(d != NULL);
    res = res && osbus_msg_set_prop_string(d, "prop_str", "Abc");
    res = res && osbus_msg_set_prop_int(d, "prop_int", 55);
    res = res && osbus_msg_set_prop_int(d, "prop_uint", uint_max);
    res = res && osbus_msg_set_prop_int64(d, "prop_i64", max_int64);
    res = res && osbus_msg_set_prop_int64(d, "prop_u64", max_uint64);
    res = res && osbus_msg_set_prop_double(d, "prop_dbl1", 1);
    res = res && osbus_msg_set_prop_double(d, "prop_dbl2", M_PI);
    res = res && osbus_msg_set_prop_double(d, "prop_dbl3", DBL_MAX);
    res = res && osbus_msg_set_prop_null(d, "prop_null");
    res = res && osbus_msg_set_prop_bool(d, "prop_bool", true);

    e = osbus_msg_set_prop_array(d, "prop_array");
    TEST_ASSERT_TRUE(e != NULL);
    res = res && osbus_msg_add_item_string(e, "x");
    res = res && osbus_msg_add_item_string(e, "y");
    res = res && osbus_msg_add_item_int(e, 66);

    e = osbus_msg_set_prop_object(d, "prop_object");
    TEST_ASSERT_TRUE(e != NULL);
    res = res && osbus_msg_set_prop_string(e, "pa", "i");
    res = res && osbus_msg_set_prop_string(e, "pb", "j");
    res = res && osbus_msg_set_prop_int(e, "pn", 77);

    e = osbus_msg_set_prop_array(d, "empty_array");
    TEST_ASSERT_TRUE(e != NULL);
    e = osbus_msg_set_prop_object(d, "empty_object");
    TEST_ASSERT_TRUE(e != NULL);
    res = res && osbus_msg_set_prop_binary(d, "prop_bin", (uint8_t*)"sample\n", 7);
    TEST_ASSERT_TRUE(res);
    res = res && osbus_msg_set_prop_binary(d, "empty_bin", NULL, 0);
    TEST_ASSERT_TRUE(res);


    const char *s_aaa = NULL;
    int i = 0;
    unsigned int u = 0;
    int64_t i64 = 0;
    uint64_t u64 = 0;
    const uint8_t *s_bin = NULL;
    const uint8_t *s_empty_bin = NULL;
    int size_bin = 0;
    int size_empty_bin = 0;
    res = true;
    res = res && osbus_msg_get_prop_string(d, "prop_str", &s_aaa);
    res = res && osbus_msg_get_prop_int(d, "prop_int", &i);
    res = res && osbus_msg_get_prop_int(d, "prop_uint", (int*)&u);
    res = res && osbus_msg_get_prop_int64(d, "prop_i64", &i64);
    res = res && osbus_msg_get_prop_int64(d, "prop_u64", (int64_t*)&u64);
    res = res && osbus_msg_get_prop_binary(d, "prop_bin", &s_bin, &size_bin);
    res = res && osbus_msg_get_prop_binary(d, "empty_bin", &s_empty_bin, &size_empty_bin);
    TEST_ASSERT_TRUE(res);

    LOGN("prop_str = %s\n", s_aaa);
    LOGN("prop_int = %d\n", i);
    LOGN("prop_uint = %u\n", u);
    LOGN("prop_i64 = %"PRIx64" %"PRId64"\n", i64, i64);
    LOGN("prop_u64 = %"PRIx64" %"PRIu64"\n", u64, u64);
    LOGN("prop_bin = [%d] '%.*s'\n", size_bin, size_bin, (char*)s_bin);
    LOGN("\n");

    osbus_msg_foreach(d, e) {
        str = osbus_msg_to_dbg_str(e);
        LOGN("%-6s '%s' = %s\n",
                osbus_msg_type_str(osbus_msg_get_type(e)),
                osbus_msg_get_name(e), str);
        free(str);
    }
    LOGN("\n");

    osbus_msg_foreach_item(d, i, e) {
        str = osbus_msg_to_dbg_str(e);
        LOGN("[%d] = %s\n", i, str);
        free(str);
    }
    LOGN("\n");

    char *key;
    osbus_msg_foreach_prop(d, key, e) {
        str = osbus_msg_to_dbg_str(e);
        LOGN("\"%s\" = %s\n", key, str);
        free(str);
    }
    LOGN("\n");

    osbus_msg_to_dbg_str_flags_t flags = {.compact=1};
    str = osbus_msg_to_dbg_str_flags(d, flags);
    LOGN("%s\n\n", str);
    free(str);

    str = osbus_msg_to_dbg_str(d);
    TEST_ASSERT_TRUE(str != NULL);
    //LOGN("%s\n\n", str);
    free(str);

    str = osbus_msg_to_dbg_str_flags(d, (osbus_msg_to_dbg_str_flags_t){.compact=1,.indent=4});
    TEST_ASSERT_TRUE(str != NULL);
    //LOGN("%s\n\n", str);
    free(str);

    str = osbus_msg_to_dbg_str_flags(d, (osbus_msg_to_dbg_str_flags_t){.indent=4});
    TEST_ASSERT_TRUE(str != NULL);
    LOGN("%s\n\n", str);
    free(str);

    g_msg = d;
}

void data_test_json(void)
{
    osbus_msg_t *d = g_msg;
    osbus_msg_t *e = NULL;
    osbus_msg_t *d2 = NULL;
    json_t *json = NULL;
    char *str = NULL;
    bool res = false;

    LOGN("\n\n === data_to_json === \n\n");
    json = osbus_msg_to_json(d);
    TEST_ASSERT_TRUE(json != NULL);
    if (!json) goto out;

    str = json_dumps(json, JSON_COMPACT);
    TEST_ASSERT_TRUE(str != NULL);
    LOGN("%s\n\n", str);
    json_free(str);

    /*str = json_dumps(json, 0);
      LOGN("%s\n\n", str);
      json_free(str);

      str = json_dumps(json, JSON_COMPACT | JSON_INDENT(4));
      LOGN("%s\n\n", str);
      json_free(str);*/

    str = json_dumps(json, JSON_INDENT(4));
    LOGN("%s\n\n", str);
    json_free(str);

    LOGN("\n\n === data_from_json === \n\n");

    d2 = osbus_msg_from_json(json);
    TEST_ASSERT_TRUE(d2 != NULL);

    str = osbus_msg_to_dbg_str_flags(d, (osbus_msg_to_dbg_str_flags_t){.indent=4});
    LOGN("%s\n\n", str);
    free(str);

    osbus_msg_foreach(d2, e) {
        str = osbus_msg_to_dbg_str(e);
        LOGN("%-6s '%s' = %s\n",
                osbus_msg_type_str(osbus_msg_get_type(e)),
                osbus_msg_get_name(e), str);
        free(str);
    }
    LOGN("\n");

    const uint8_t *s_bin = NULL;
    int size_bin;
    res = osbus_msg_get_prop_binary(d2, "prop_bin", &s_bin, &size_bin);
    TEST_ASSERT_TRUE(res && s_bin != NULL && size_bin != 0);
    LOGN("prop_bin = '%.*s'\n", size_bin, (char*)s_bin);
    LOGN("\n");
out:
    if (json) json_decref(json);
    osbus_msg_free(d2);
}

void data_test_ubus(void)
{
#ifdef CONFIG_OSBUS_UBUS
    osbus_msg_t *d = g_msg;
    char *str;
    osbus_msg_t *d3 = NULL;
    struct blob_buf *b = NULL;
    bool res;
    LOGN("\n\n === data_to_blob_buf === \n\n");
    res = osbus_msg_to_blob_buf(d, &b);
    TEST_ASSERT_TRUE(res && b != NULL);
    if (!b) goto out;

    str = blobmsg_format_json_indent(b->head, true, 0);
    LOGN("%s\n\n", str);
    free(str);

    LOGN("\n\n === data_from_blob_buf === \n\n");
    res = osbus_msg_from_blob_buf(&d3, b);
    TEST_ASSERT_TRUE(res && d3 != NULL);
    if (!d3) goto out;

    str = osbus_msg_to_dbg_str_indent(d3, 4);
    LOGN("%s\n\n", str);
    free(str);
    LOGN("prop_bin type = %s\n", osbus_msg_type_str(
                osbus_msg_get_type(osbus_msg_get_prop(d3, "prop_bin"))));
out:
    if (b) {
        blob_buf_free(b);
        free(b);
    }
    osbus_msg_free(d3);
#endif
}

void data_test_rbus(void)
{
#ifdef CONFIG_OSBUS_RBUS
    osbus_msg_t *d = g_msg;
    char *str;
    osbus_msg_t *d4 = NULL;
    bool res;
    LOGN("\n\n === data_to_rbusObject === \n\n");

    rbusObject_t robj = NULL;
    res = osbus_msg_to_rbus_object(d, &robj);
    TEST_ASSERT_TRUE(res && robj != NULL);
    if (!robj) goto out;

    if (opt_severity >= LOG_SEVERITY_NOTICE) {
        rbusObject_fwrite(robj, 1, stdout);
    }
    LOGN("\n\n === data_from_rbusObject === \n\n");
    res = osbus_msg_from_rbus_object(&d4, robj);
    TEST_ASSERT_TRUE(res && d4 != NULL);
    if (!d4) goto out;

    str = osbus_msg_to_dbg_str_indent(d4, 4);
    LOGN("%s\n\n", str);
    free(str);
    LOGN("prop_bin type = %s\n", osbus_msg_type_str(
                osbus_msg_get_type(osbus_msg_get_prop(d4, "prop_bin"))));
    LOGN("\n\n");
out:
    if (robj) rbusObject_Release(robj);
    osbus_msg_free(d4);
#endif
}

#define _QUOTE(...) #__VA_ARGS__

void data_test_util(void)
{
    char *jstr = _QUOTE({"a":{"b":{"c":[1,{"d":{"e":123}},2]}}});
    osbus_msg_t *d = NULL;
    osbus_msg_t *e = NULL;
    char str[256];
    bool res;
    int cmp;
    d = osbus_msg_from_json_string(jstr);
    TEST_ASSERT_TRUE(d != NULL);
    if (!d) return;
    osbus_msg_to_dbg_str_fixed(d, str, sizeof(str));
    LOGN("nested: %s\n", str);
    char *lookup = "a.b.c.[1].d.e";
    e = osbus_msg_lookup(d, lookup);
    TEST_ASSERT_TRUE(e != NULL);
    osbus_msg_to_dbg_str_fixed(e, str, sizeof(str));
    LOGN("lookup: %s = %s\n", lookup, str);
    osbus_msg_free(d);

    char *path = "x.y.z.[1].q.w";
    d = osbus_msg_new_null();
    e = osbus_msg_mkpath(d, path);
    TEST_ASSERT_TRUE(e != NULL);
    osbus_msg_to_dbg_str_fixed(d, str, sizeof(str));
    LOGN("mkpath %s:\n", path);
    LOGN("      = %s\n", str);
    res = osbus_msg_assign(e, osbus_msg_new_string("abc"));
    TEST_ASSERT_TRUE(res);
    osbus_msg_to_dbg_str_fixed(d, str, sizeof(str));
    LOGN("assign: %s\n", str);
    osbus_msg_t *copy;
    copy = osbus_msg_copy(d);
    TEST_ASSERT_TRUE(copy != NULL);
    cmp = osbus_msg_compare(d, copy);
    LOGN("cmp: %d", cmp);
    TEST_ASSERT_TRUE(cmp == 0);
    res = osbus_msg_assign(e, osbus_msg_new_string("xyz"));
    TEST_ASSERT_TRUE(res);
    cmp = osbus_msg_compare(d, copy);
    LOGN("cmp: %d", cmp);
    TEST_ASSERT_TRUE(cmp != 0);
}

void data_test(void)
{
    RUN_TEST(data_test_basic);
    RUN_TEST(data_test_json);
    RUN_TEST(data_test_ubus);
    RUN_TEST(data_test_rbus);
    RUN_TEST(data_test_util);
    osbus_msg_free(g_msg);
}

int main(int argc, char *argv[])
{
    int opt;
    char *test_name = "test_osbus_msg";

    log_open(test_name, 0);
    while ((opt = getopt(argc, argv, "v")) != -1) {
        switch (opt) {
            case 'v':
                if (opt_severity < LOG_SEVERITY_TRACE) opt_severity++;
                break;
        }
    }

    ut_init(test_name, NULL, NULL);
    ut_setUp_tearDown(test_name, NULL, NULL);
    log_severity_set(opt_severity);
    data_test();
    ut_fini();

    return 0;
}

