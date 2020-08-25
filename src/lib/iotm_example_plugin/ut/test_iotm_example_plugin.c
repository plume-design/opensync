/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "test_example_plugin.h"

static struct ev_loop *loop;
static int num_events = 0;
static struct iotm_session g_session =
{
    .name = "test_session",
};

    void
tearDown(void)
{
    // pass
    iotm_example_plugin_exit(&g_session);
}

    void
setUp(void)
{
    loop = EV_DEFAULT;
    num_events = 0;
    g_session.loop = loop;
    iotm_example_plugin_init(&g_session);
}

void test_emit(struct iotm_session *session, struct plugin_event_t *event)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(event, "Should have emitted an event");
    num_events += 1;
}

void test_example_init_invalid_input(void) 
{
    int ret = iotm_example_plugin_init(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, ret, "Should have returned an error as no session is being passed");
}

void test_example_init_valid_input(void)
{
    struct iotm_session session = {
        .name = "example_plugin",
        .topic = "locId/ble/nodeId",
        .location_id = "locationId",
        .node_id = "nodeId",
        .dso = "/usr/plume/lib/example_plugin.so",
        .report_count = 0,
        .loop = loop,
        .ops = {
            .emit = test_emit,
        },
    };
    int ret = -1;

    printf("before init\n");
    ret = iotm_example_plugin_init(&session);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ret, "Should have initialized plugin with valid session");

    session.ops.exit(&session);
}

struct plugin_event_t *get_null_plug_event()
{
    return NULL;
}


void test_foreach_val(struct iotm_tree_t *self,
        void(*cb)(ds_list_t *, struct iotm_value_t *, void *),
        void *ctx)
{
    struct iotm_value_t first_val =
    {
        .key = "mac",
        .value = "AA:BB:CC:DD:EE:FF",
    };
    cb(NULL, &first_val, NULL);
}

void test_log_example_plugin(void)
{
    struct iotm_session session;
    memset(&session, 0, sizeof(session));
    struct iotm_tree_t tree =
    {
        .foreach_val = test_foreach_val,
    };

    struct plugin_command_t cmd =
    {
        .params = &tree,
    };

    iotm_example_plugin_handle(&session, &cmd);
}

    int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    target_log_open("EXAMPLE_PLUGIN_TEST", LOG_OPEN_STDOUT_QUIET);
    log_severity_set(LOG_SEVERITY_TRACE);


    UnityBegin(test_name);

    RUN_TEST(test_example_init_invalid_input);
    RUN_TEST(test_example_init_valid_input);
    RUN_TEST(test_log_example_plugin);

    return UNITY_END();
}
