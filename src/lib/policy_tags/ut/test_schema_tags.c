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

#include "json_util.h"
#include "log.h"
#include "policy_tags.h"
#include "target.h"
#include "unity.h"

char *
g_test_name = "test_policy_tags";


struct schema_Openflow_Tag g_tags[] =
{
    {
        .name_exists = true,
        .name = "tag_1",
        .device_value_len = 2,
        .device_value =
        {
            "tag_1_dev_val_1",
            "tag_1_dev_val_2",
        },
        .cloud_value_len = 3,
        .cloud_value =
        {
            "tag_1_cloud_val_1",
            "tag_1_cloud_val_2",
            "tag_1_cloud_val_3",
        },
    },
    {
        .name_exists = true,
        .name = "tag_2",
        .device_value_len = 2,
        .device_value =
        {
            "tag_2_dev_val_1",
            "tag_2_dev_val_2",
        },
        .cloud_value_len = 3,
        .cloud_value =
        {
            "tag_2_cloud_val_1",
            "tag_2_cloud_val_2",
            "tag_2_cloud_val_3",
        },
    },
    {
        .name_exists = true,
        .name = "tag_3",
        .device_value_len = 2,
        .device_value =
        {
            "tag_3_dev_val_1",
            "tag_3_dev_val_2",
        },
        .cloud_value_len = 3,
        .cloud_value =
        {
            "tag_3_cloud_val_1",
            "tag_3_cloud_val_2",
            "tag_3_cloud_val_3",
        },
    },
};


struct schema_Openflow_Tag_Group g_tag_group =
{
    .name_exists = true,
    .name = "group_tag",
    .tags_len = 2,
    .tags =
    {
        "#tag_1",
        "@tag_2",
        "tag_3",
    }
};


void
test_tag_type(void)
{
    struct tag_validate
    {
        char *name;
        int type;
    } tag_types[] =
      {
          {
              .name = "coucou",
              .type = NOT_A_OPENSYNC_TAG,
          },
          {
              .name = "${",
              .type = NOT_A_OPENSYNC_TAG,
          },
          {
              .name = "${}",
              .type = OPENSYNC_TAG,
          },
          {
              .name = "$[]",
              .type = OPENSYNC_GROUP_TAG,
          },
          {
              .name = "${]",
              .type = NOT_A_OPENSYNC_TAG,
          },
          {
              .name = "$[}",
              .type = NOT_A_OPENSYNC_TAG,
          },
          {
              .name = "$[#blah]",
              .type = OPENSYNC_GROUP_TAG,
          },
      };

    int expected;
    char *name;
    size_t len;
    int type;
    size_t i;

    len = sizeof(tag_types) / sizeof(tag_types[0]);

    for (i = 0; i < len; i++)
    {
        name = tag_types[i].name;
        expected = tag_types[i].type;
        type = om_tag_get_type(name);
        TEST_ASSERT_EQUAL_INT(expected, type);
    }
}

void
test_type_of_tag(void)
{
    struct tag_validate
    {
        char *name;
        int flag;
    } tag_types[] =
      {
          {
              .name = "coucou",
              .flag = OM_TLE_FLAG_NONE,
          },
          {
              .name = "${&}",
              .flag = OM_TLE_FLAG_NONE,
          },
          {
              .name = "${@}",
              .flag = OM_TLE_FLAG_DEVICE,
          },
          {
              .name = "${#}",
              .flag = OM_TLE_FLAG_CLOUD,
          },
          {
              .name = "${*}",
              .flag = OM_TLE_FLAG_LOCAL,
          },
          {
              .name = "$[&]",
              .flag = OM_TLE_FLAG_NONE,
          },
          {
              .name = "$[@]",
              .flag = OM_TLE_FLAG_DEVICE,
          },
          {
              .name = "$[#]",
              .flag = OM_TLE_FLAG_CLOUD,
          },
          {
              .name = "$[*]",
              .flag = OM_TLE_FLAG_LOCAL,
          },
      };

    int expected;
    char *name;
    size_t len;
    int flag;
    size_t i;

    len = sizeof(tag_types) / sizeof(tag_types[0]);

    for (i = 0; i < len; i++)
    {
        name = tag_types[i].name;
        expected = tag_types[i].flag;
        flag = om_get_type_of_tag(name);
        TEST_ASSERT_EQUAL_INT(expected, flag);
    }
}


void
setUp(void)
{
    size_t len;
    size_t i;
    bool ret;

    len = sizeof(g_tags) / sizeof(*g_tags);
    for (i = 0; i < len; i++)
    {
        ret = om_tag_add_from_schema(&g_tags[i]);
        TEST_ASSERT_TRUE(ret);
    }

    ret = om_tag_group_add_from_schema(&g_tag_group);
    TEST_ASSERT_TRUE(ret);
}


void
tearDown(void)
{
    size_t len;
    size_t i;
    bool ret;

    len = sizeof(g_tags) / sizeof(*g_tags);
    for (i = 0; i < len; i++)
    {
        ret = om_tag_remove_from_schema(&g_tags[i]);
        TEST_ASSERT_TRUE(ret);
    }
    ret = om_tag_group_remove_from_schema(&g_tag_group);
    TEST_ASSERT_TRUE(ret);
}


void
test_val_in_tag(void)
{
    struct schema_Openflow_Tag *ovsdb_tag;
    char *value;
    bool ret;

    ovsdb_tag = &g_tags[0];

    /* Get a device value from tag_1 */
    value = ovsdb_tag->device_value[1];

    /* Validate that the value is in the tag group */
    ret = om_tag_in(value, "${tag_1}");
    TEST_ASSERT_TRUE(ret);

    /* Validate that the value is in the tag's device values */
    ret = om_tag_in(value, "${@tag_1}");
    TEST_ASSERT_TRUE(ret);

    /* Validate that the value is not in the tag's cloud values */
    ret = om_tag_in(value, "${#tag_1}");
    TEST_ASSERT_FALSE(ret);

    /* Get a cloud value from tag_1 */
    value = ovsdb_tag->cloud_value[2];

    /* Validate that the value is in the tag */
    ret = om_tag_in(value, "${tag_1}");
    TEST_ASSERT_TRUE(ret);

    /* Validate that the value is not in the tag's device values */
    ret = om_tag_in(value, "${@tag_1}");
    TEST_ASSERT_FALSE(ret);

    /* Validate that the value is in the tag's cloud values */
    ret = om_tag_in(value, "${#tag_1}");
    TEST_ASSERT_TRUE(ret);
}


void test_val_in_tag_group(void)
{
    struct schema_Openflow_Tag *ovsdb_tag;
    char *value;
    bool ret;

    /*
     * tag_1 is listed in the tag group for its cloud values,
     * tag_2 is listed in the tag group for its device vaues,
     * tag_3 is listed in the tag group for all its values
     */

    /* Get a device value from tag_1 */
    ovsdb_tag = &g_tags[0];
    value = ovsdb_tag->device_value[1];

    /* Validate that the value is not in the group tag */
    ret = om_tag_in(value, "$[group_tag]");
    TEST_ASSERT_FALSE(ret);

    /* Validate that the value is not in the tag group's device values */
    ret = om_tag_in(value, "$[@group_tag]");
    TEST_ASSERT_FALSE(ret);

    /* Validate that the value is not in the tag group's cloud values */
    ret = om_tag_in(value, "$[#group_tag]");
    TEST_ASSERT_FALSE(ret);

    /* Get a cloud value from tag_1 */
    value = ovsdb_tag->cloud_value[2];

    /* Validate that the value is in the tag as part of tag_1's cloud values */
    ret = om_tag_in(value, "$[group_tag]");
    TEST_ASSERT_TRUE(ret);

    /* Validate that the value is not in the tag's device values */
    ret = om_tag_in(value, "$[@group_tag]");
    TEST_ASSERT_FALSE(ret);

    /* Validate that the value is in the tag's cloud values */
    ret = om_tag_in(value, "$[#group_tag]");
    TEST_ASSERT_TRUE(ret);
}


int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("TEST", LOG_OPEN_STDOUT);
    log_severity_set(LOG_SEVERITY_INFO);

    UnityBegin(g_test_name);

    RUN_TEST(test_tag_type);
    RUN_TEST(test_type_of_tag);
    RUN_TEST(test_val_in_tag);
    RUN_TEST(test_val_in_tag_group);

    return UNITY_END();
}
