/**
* Copyright (c) 2020, Charter Communications Inc. All rights reserved.
* 
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*    1. Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*    2. Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*    3. Neither the name of the Charter Communications Inc. nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
* 
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "test_connected_devices.h"

static struct ev_loop *loop;
static int num_events = 0;

static bool ran_rule_interaction = false;
int test_interact_rules(struct schema_IOT_Rule_Config rows[], size_t num_rules)
{
    ran_rule_interaction = true;
    TEST_ASSERT_EQUAL_INT(4, num_rules);
    return 0;
}

static struct iotm_session g_session =
{
    .name = "test_session",
    .ops = 
    {
        .update_rules = test_interact_rules,
        .remove_rules = test_interact_rules,
    },
};

void tearDown(void)
{
    iotm_connected_devices_exit(&g_session);
}

void setUp(void)
{
    loop = EV_DEFAULT;
    num_events = 0;
    g_session.loop = loop;
    iotm_connected_devices_init(&g_session);
}

void test_emit(struct iotm_session *session, struct plugin_event_t *event)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(event, "Should have emitted an event");
    num_events += 1;
}

void test_connected_init_invalid_input(void) 
{
    int ret = iotm_connected_devices_init(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, ret, "Should have returned an error as no session is being passed");
}


void test_connected_init_valid_input(void)
{
    struct iotm_session session = {
        .name = "connected_devices",
        .topic = "locId/ble/nodeId",
        .location_id = "locationId",
        .node_id = "nodeId",
        .dso = "/usr/plume/lib/connected_devices.so",
        .report_count = 0,
        .loop = loop,
        .ops =
        {
            .emit = test_emit,
            .update_rules = test_interact_rules,
            .remove_rules = test_interact_rules,
        },
    };
    int ret = -1;

    ran_rule_interaction = false;
    ret = iotm_connected_devices_init(&session);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ret, "Should have initialized plugin with valid session");
    TEST_ASSERT_TRUE_MESSAGE(ran_rule_interaction, "Should have attemtped to install the ovsdb rules.");

    ran_rule_interaction = false;
    session.ops.exit(&session);
    TEST_ASSERT_TRUE_MESSAGE(ran_rule_interaction, "Should have attemtped to install the ovsdb rules.");
}

struct plugin_event_t *get_null_plug_event()
{
    return NULL;
}

void test_foreach_tag(
        char *tag,
        void (*cb)(char *, char *, void *),
        void *ctx)
{
    cb(CONNECT_TAG, "firstmac", ctx);
    cb(CONNECT_TAG, "secondmac", ctx);
    cb(CONNECT_TAG, "thirdmac", ctx);
}

char *test_log_get_command(struct plugin_command_t *cmd, char *key)
{
    return "new_connected_mac";
}

static bool ran_update = false;
int test_update_tag(char *name, struct schema_Openflow_Tag *row)
{
    ran_update = true;
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
            CONNECT_TAG,
            row->name,
            "Should have loaded the tag name into the row");
    TEST_ASSERT_EQUAL_INT(4, row->device_value_len);
    return 0;
}

void test_log_connected_devices(void)
{
    struct iotm_session session =
    {
        .ops = 
        { 
            .foreach_tag = test_foreach_tag,
            .update_tag = test_update_tag,
        },
    };

    struct plugin_command_t cmd =
    {
        .action = CONNECT,
        .ops = { .get_param = test_log_get_command },
    };

    iotm_connected_devices_handle(&session, &cmd);
    TEST_ASSERT_TRUE_MESSAGE(ran_update, "Should have attempted to update OVSDB.");
}

void test_disconnect_parsing_found_match(void)
{
    bool disconnect = false;

    char *disconnect_type = DISCONNECT;
    disconnect = is_disconnect(disconnect_type);
    TEST_ASSERT_TRUE_MESSAGE(disconnect, "Should have identified disconnect string.");
}

void test_disconnect_parsing_no_match(void)
{
    bool disconnect = false;

    char *disconnect_type = CONNECT;
    disconnect = is_disconnect(disconnect_type);
    TEST_ASSERT_FALSE_MESSAGE(disconnect, "Should not have found disconnect match");
}

void test_connect_parsing_found_match(void)
{
    bool connect = false;

    char *connect_type = CONNECT;
    connect = is_connect(connect_type);
    TEST_ASSERT_TRUE_MESSAGE(connect, "Should have identified connect string.");
}

void test_connect_parsing_no_match(void)
{
    bool connect = false;

    char *connect_type = DISCONNECT;
    connect = is_connect(connect_type);
    TEST_ASSERT_FALSE_MESSAGE(connect, "Should not have found disconnect match");
}

char *test_tag_get_command(struct plugin_command_t *cmd, char *key)
{
    return "new_connected_mac";
}

void test_build_tag_row_update_device_connected(void)
{
    struct iotm_session session =
    {
        .ops = { .foreach_tag = test_foreach_tag },
    };

    struct plugin_command_t cmd =
    {
        .action = CONNECT,
        .ops = { .get_param = test_tag_get_command },
    };

    struct schema_Openflow_Tag tag_row;
    memset(&tag_row, 0, sizeof(tag_row));

    build_tag_row_update(&session, &cmd, &tag_row);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
            4,
            tag_row.device_value_len,
            "Should have installed all macs, including additional connected mac.");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
            CONNECT_TAG,
            tag_row.name,
            "Should have added the connect tag to the row");
}

char *test_tag_get_command_disconnect(struct plugin_command_t *cmd, char *key)
{
    return "firstmac";
}
void test_build_tag_row_update_device_disconnected(void)
{
    struct iotm_session session =
    {
        .ops = { .foreach_tag = test_foreach_tag },
    };

    struct plugin_command_t cmd =
    {
        .action = DISCONNECT,
        .ops = { .get_param = test_tag_get_command_disconnect },
    };

    struct schema_Openflow_Tag tag_row;
    memset(&tag_row, 0, sizeof(tag_row));

    build_tag_row_update(&session, &cmd, &tag_row);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
            2,
            tag_row.device_value_len,
            "Should have not included the disconnected mac in the row");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
            CONNECT_TAG,
            tag_row.name,
            "Should have added the connect tag to the row");
}

    int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("CONNECTED_DEVICES_TEST", LOG_OPEN_STDOUT_QUIET);
    log_severity_set(LOG_SEVERITY_TRACE);


    UnityBegin(test_name);

    RUN_TEST(test_connected_init_invalid_input);
    RUN_TEST(test_connected_init_valid_input);
    RUN_TEST(test_log_connected_devices);
    RUN_TEST(test_disconnect_parsing_found_match);
    RUN_TEST(test_disconnect_parsing_no_match);
    RUN_TEST(test_connect_parsing_found_match);
    RUN_TEST(test_connect_parsing_no_match);
    RUN_TEST(test_build_tag_row_update_device_connected);
    RUN_TEST(test_build_tag_row_update_device_disconnected);

    return UNITY_END();
}
