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

#include "test_ble_handler.h"
#include "iotm.h"

static struct ev_loop *loop;
static int num_events = 0;


struct iotm_event *get_null_event(struct iotm_session *session, char *ev)
{
    return NULL;
}

void *g_ctx = NULL;
void **test_get(struct tl_context_tree_t *tree, char *key)
{
    return &g_ctx;
}

struct tl_context_tree_t g_tl =
{
    .get = test_get,
};

static struct iotm_session g_session =
{
    .name = "test_session",
    .ops = { .get_event = get_null_event },
    .tl_ctx_tree = &g_tl,
};

void tearDown(void)
{
    iotm_ble_handler_exit(&g_session);
}

void setUp(void)
{
    loop = EV_DEFAULT;
    num_events = 0;
    g_session.loop = loop;
    iotm_ble_handler_init(&g_session);
}


void test_iotm_handler_lookup_session(void)
{
    struct iotm_session session =
    {
        .name = "test_session_name",
    };

    struct iotm_ble_handler_session *lookup = NULL;

    lookup = iotm_ble_handler_lookup_session(&session);

    TEST_ASSERT_NOT_NULL(lookup);

    iotm_ble_handler_delete_session(&session);
}

void test_emit(struct iotm_session *session, struct plugin_event_t *event)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(event, "Should have emitted an event");
    num_events += 1;
}

void test_ble_handler_init_invalid_input(void) 
{
    int ret = iotm_ble_handler_init(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, ret, "Should have returned an error as no session is being passed");
}

void test_ble_handler_init_valid_input(void)
{
    struct iotm_session session =
    {
        .name = "ble_handler",
        .topic = "locId/ble/nodeId",
        .location_id = "locationId",
        .node_id = "nodeId",
        .dso = "/usr/plume/lib/ble_handler.so",
        .report_count = 0,
        .loop = loop,
        .ops =
        {
            .emit = test_emit,
            .get_event = get_null_event,
        },
    };
    int ret = -1;

    ret = iotm_ble_handler_init(&session);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ret, "Should have initialized plugin with valid session");

    session.ops.exit(&session);
}

void test_ble_handler_event_type_conversion(void)
{
    char *ovsdb_val = "ble_advertised";

    event_type ev = ble_event_type(ovsdb_val);
    TEST_ASSERT_EQUAL_INT_MESSAGE(BLE_ADVERTISED, ev, "Should have converted the event to an advertised enum.");
}

static int scan_filter_foreach_num_hit = 0;
void scan_filter_foreach(
        struct iotm_event *self, 
        void (*cb)(ds_list_t *, struct iotm_value_t *, void *), 
        void *ctx)
{
    scan_filter_foreach_num_hit += 1;
    struct iotm_value_t vals[3] =
    {
        {
            .key = MAC_KEY,
            .value = "AA:BB:CC:DD:EE:FF"
        },
        {
            .key = SERV_UUID,
            .value = "12345",
        },
        {
            .key = SERV_UUID,
            .value = "45678",
        }
    };
    for (int i = 0; i < 3; i++)
    {
        cb(NULL, &vals[i], ctx);
    }
}

void test_scan_filter_generation(void)
{
    ble_discovery_scan_params_t params;
    memset(&params, 0, sizeof(params));
    int ret = -1;

    // Setup event and rules
    struct iotm_event event =
    {
        .event = "ble_advertised",
        .num_rules = 1,
        .foreach_filter = scan_filter_foreach,
    };

    ret = get_scan_params(&event, &params);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ret, "Should have been able to load whitelist with rule.");

    TEST_ASSERT_EQUAL_INT(1, params.num_mac_filters);
    TEST_ASSERT_EQUAL_INT(2, params.num_uuid_filters);

    TEST_ASSERT_NOT_NULL(params.mac_filter);
    TEST_ASSERT_NOT_NULL(params.uuid_filter);
    if (params.mac_filter) free(params.mac_filter);
    if (params.uuid_filter) free(params.uuid_filter);
}

void wild_scan_filter_foreach(
        struct iotm_event *self, 
        void (*cb)(ds_list_t *, struct iotm_value_t *, void *),
        void *ctx)
{
    struct iotm_value_t vals[3] =
    {
        {
            .key = MAC_KEY,
            .value = "AA:BB:CC:DD:EE:FF"
        },
        {
            .key = MAC_KEY,
            .value = "*",
        },
        {
            .key = SERV_UUID,
            .value = "45678",
        }
    };
    for (int i = 0; i < 3; i++)
    {
        cb(NULL, &vals[i], ctx);
    }
}

void test_wildcard_scan_values(void)
{
    ble_discovery_scan_params_t params;
    memset(&params, 0, sizeof(params));
    int ret = -1;

    // Setup event and rules
    struct iotm_event event =
    {
        .event = "ble_advertised",
        .num_rules = 1,
        .foreach_filter = wild_scan_filter_foreach,
    };

    ret = get_scan_params(&event, &params);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ret, "Should have been able to load whitelist with rule.");
    TEST_ASSERT_EQUAL_INT(0, params.num_mac_filters);
    TEST_ASSERT_EQUAL_INT(1, params.num_uuid_filters);
    TEST_ASSERT_NULL_MESSAGE(params.mac_filter, "MAC filter should be populated with one UUID");
    TEST_ASSERT_NOT_NULL_MESSAGE(params.uuid_filter, "UUID filter should be populated with one UUID");

    if (params.uuid_filter) free(params.uuid_filter);
}

static struct iotm_event *iot_ev = NULL;
struct iotm_event *test_get_plugin_event(struct iotm_session *session, char *ev)
{
    iot_ev = calloc(1, sizeof(struct iotm_event));
    iot_ev->foreach_filter = scan_filter_foreach;
    return iot_ev;
}

    void
test_handler_rule_update(void)
{
    struct iotm_session session =
    {
        .ops =
        {
            .get_event = test_get_plugin_event,
        }
    };

    struct iotm_rule rule =
    {
        .event = "ble_advertised",
    };
    scan_filter_foreach_num_hit = 0;
    iotm_ble_handler_rule_update(&session, NULL, &rule);
    TEST_ASSERT_EQUAL_INT(2, scan_filter_foreach_num_hit);
    if (iot_ev) free(iot_ev);
}

void test_cmd_from_string(void)
{
    const struct ble_cmd_t *cmd = ble_cmd_from_string("ble_connect_device");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
            BLE_CONNECT_DEVICE,
            cmd->command_type,
            "Should have set the correct command type");
}


char *test_iotm_handle_get_param(struct plugin_command_t *plg, char *param)
{
    if (strcmp(P_MAC, param) == 0) return "mytestmac";
    if (strcmp(PUBLIC_ADDR, param) == 0) return "true";
    return "unknown";
}

void test_iotm_ble_handle_valid(void)
{
    struct plugin_command_t cmd =
    {
        .action = "ble_connect_device",
        .ops =
        {
            .get_param = test_iotm_handle_get_param,
        }
    };

    iotm_ble_handle(NULL, &cmd);
}

char *test_connect_get_param(struct plugin_command_t *cmd, char *key)
{
    return "false";
}

void test_handle_connect(void)
{
    struct iotm_session sess;
    memset(&sess, 0, sizeof(sess));

    struct plugin_command_t cmd =
    {
        .ops =
        {
            .get_param = test_connect_get_param,
        }
    };

    ble_connect_params_t params;
    memset(&params, 0, sizeof(params));

    get_connect_params(&sess, &cmd, &params);

    TEST_ASSERT_FALSE_MESSAGE(params.is_public_addr, "Should have set public address to false");
}

char *test_get_param(struct plugin_command_t *cmd, char *key)
{
    if (strcmp(key, "serv_uuid") == 0) return "fakeservuuid";
    if (strcmp(key, "char_uuid") == 0) return "fakecharuuid";
    if (strcmp(key, "desc_uuid") == 0) return "fakedescuuid";
    if (strcmp(key, "is_primary") == 0) return "false";
    if (strcmp(key, "decode_type") == 0) return "hex";
    if (strcmp(key, "data") == 0) return "DEadbeef10203040b00b1e50";
    return "none";
}

void test_handle_disable_char_notifications(void)
{
    struct iotm_session sess;
    memset(&sess, 0, sizeof(sess));

    struct plugin_command_t cmd =
    {
        .ops =
        {
            .get_param = test_get_param,
        }
    };

    ble_characteristic_notification_params params;
    memset(&params, 0, sizeof(params));
    get_char_notification_params(&sess, &cmd, &params);

    TEST_ASSERT_EQUAL_STRING_MESSAGE("fakecharuuid", params.char_uuid, "Should have set the Char UUID");
}

void test_handle_char_discovery(void)
{
    struct iotm_session sess;
    memset(&sess, 0, sizeof(sess));

    struct plugin_command_t cmd =
    {
        .ops =
        {
            .get_param = test_get_param,
        }
    };

    ble_characteristic_discovery_params_t params;
    memset(&params, 0, sizeof(params));
    get_char_discovery_params(&sess, &cmd, &params);

    TEST_ASSERT_EQUAL_STRING_MESSAGE("fakeservuuid", params.serv_uuid, "Should have set the Serv UUID");
}

void foreach_service(struct iotm_list_t *lists, 
        void(*cb)(ds_list_t *, struct iotm_value_t *, void*), 
        void *ctx)
{
    struct iotm_value_t vals[3] =
    {
        {
            .key = SERV_UUID,
            .value = "12345",
        },
        {
            .key = SERV_UUID,
            .value = "45678",
        },
        {
            .key = SERV_UUID,
            .value = "984331",
        }
    };

    for (int i = 0; i < 3; i++)
    {
        cb(NULL, &vals[i], ctx);
    }
}

struct iotm_list_t *serv_list = NULL;
struct iotm_list_t *get_param_serv_disc(
        struct plugin_command_t *cmd,
        char *key)
{
    serv_list = (struct iotm_list_t *)calloc(1, sizeof(struct iotm_list_t));
    serv_list->len = 3;
    serv_list->foreach = foreach_service;
    return serv_list;
}

void test_handle_serv_discovery(void)
{
    struct iotm_session sess;
    memset(&sess, 0, sizeof(sess));

    struct plugin_command_t cmd =
    {
        .ops =
        {
            .get_param = test_get_param,
            .get_params = get_param_serv_disc,
        },
    };

    ble_service_discovery_params_t params;
    memset(&params, 0, sizeof(params));
    get_serv_discovery_params(&sess, &cmd, &params);

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, params.num_filters, "Should have set number of filters to 1");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("12345", params.filter[0].uuid, "Should have set the Char UUID");
    // clean up
    if (params.filter) free(params.filter);
    if (serv_list) free(serv_list);
}


void test_handle_char_read(void)
{
    struct iotm_session sess;
    memset(&sess, 0, sizeof(sess));

    struct plugin_command_t cmd =
    {
        .ops =
        {
            .get_param = test_get_param,
        }
    };

    ble_read_characteristic_params_t params;
    memset(&params, 0, sizeof(params));
    get_char_read_params(&sess, &cmd, &params);

    TEST_ASSERT_EQUAL_STRING_MESSAGE("fakecharuuid", params.char_uuid, "Should have set the Char UUID");
}

void test_handle_desc_read(void)
{
    struct iotm_session sess;
    memset(&sess, 0, sizeof(sess));

    struct plugin_command_t cmd =
    {
        .ops =
        {
            .get_param = test_get_param,
        },
    };

    ble_read_descriptor_params_t params;
    memset(&params, 0, sizeof(params));
    get_desc_read_params(&sess, &cmd, &params);

    TEST_ASSERT_EQUAL_STRING_MESSAGE("fakecharuuid", params.char_uuid, "Should have set the Char UUID");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("fakedescuuid", params.desc_uuid, "Should have set the Char UUID");
}

void test_handle_char_write(void)
{
    struct iotm_session sess;
    memset(&sess, 0, sizeof(sess));

    struct plugin_command_t cmd =
    {
        .ops =
        {
            .get_param = test_get_param,
        },
    };

    ble_write_characteristic_params_t params;
    memset(&params, 0, sizeof(params));

    barray_t barray;
    memset(&barray, 0, sizeof(barray));
    params.barray = &barray;

    get_char_write_params(&sess, &cmd, &params);

    TEST_ASSERT_EQUAL_STRING_MESSAGE("fakecharuuid", params.char_uuid, "Should have set the Char UUID");

    char check[1024];
    bin2hex(params.barray->data, params.barray->data_length, check, 1024);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DEADBEEF10203040B00B1E50", check, "Should have properly parsed the data to a byte array.");

    if(params.barray->data) free(params.barray->data);
}

void test_handle_desc_write(void)
{
    struct iotm_session sess;
    memset(&sess, 0, sizeof(sess));

    struct plugin_command_t cmd =
    {
        .ops =
        {
            .get_param = test_get_param,
        },
    };

    ble_write_descriptor_params_t params;
    memset(&params, 0, sizeof(params));

    barray_t barray;
    memset(&barray, 0, sizeof(barray));
    params.barray = &barray;

    get_desc_write_params(&sess, &cmd, &params);

    TEST_ASSERT_EQUAL_STRING_MESSAGE("fakecharuuid", params.char_uuid, "Should have set the Char UUID");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("fakedescuuid", params.desc_uuid, "Should have set the desc UUID");

    char check[1024];

    bin2hex(params.barray->data, params.barray->data_length, check, 1024);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DEADBEEF10203040B00B1E50", check, "Should have properly parsed the data to a byte array.");

    if(params.barray->data) free(params.barray->data);
}



static struct plugin_event_t *curr;
void test_emit_adv(struct iotm_session *session, struct plugin_event_t *event)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(event, "Should have emitted an event");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ble_advertised", event->name, "Should have loaded the name of the event.");
    num_events += 1;
    curr = event;
}

static int emit_ad_count = 0;
int test_emit_advertise_param(struct plugin_event_t *ev, char *key, char *val)
{
    emit_ad_count += 1;
    return 0;
}

struct plugin_event_t *ev = NULL;
struct plugin_event_t *test_plugin_event_new()
{
    ev = calloc(1, sizeof(struct plugin_event_t));
    ev->ops.add_param_str = test_emit_advertise_param;
    return ev;
}

void test_emit_advertise(void) 
{

    int err;
    void *caller_ctx = NULL;
    uint8_t l_data[2] = { 0x02, 0x02 };
    struct iotm_session session =
    {
        .topic = "locId/ble/nodeId",
        .location_id = "locationId",
        .node_id = "nodeId",
        .dso = "/usr/plume/lib/ble_handler.so",
        .report_count = 0,
        .loop = loop,
        .ops =
        {
            .emit = test_emit_adv,
            .plugin_event_new = test_plugin_event_new,
            .get_event = get_null_event,
        },
    };

    err = iotm_ble_handler_init(&session);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, err, "Should have initialized plugin with valid session");

    barray_t data =
    {
        .data = l_data,
        .data_length = 2,
    };

    ble_advertisement_t contents =
    {
        .mac = "BE:EF:BE:EF:BE:EF",
        .is_public_address = true,
        .name = "MyTestName",
        .is_complete_name = true,
        .service_uuids = NULL,
        .num_services = 0,
        .data = &data,
    };

    struct ble_event_t event =
    {
        .type = BLE_ADVERTISED,
        .mac = "BE:EF:BE:EF:BE:EF",
        .op =
        {
            .advertise =
            {
                .contents = &contents,
            },
        },
    };

    caller_ctx = &session;
    emit_ad_count = 0;
    event_cb(caller_ctx, &event);

    // cleanup
    session.ops.exit(&session);

    if(ev) free(ev);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, num_events, "Should have emitted one event");
    TEST_ASSERT_EQUAL_INT(2, emit_ad_count);
}

static int advert_params = 0;
int test_add_param_str_advertise(struct plugin_event_t *ev, char *key, char *val)
{
    advert_params += 1;
    if (strcmp(key, NAME_KEY) == 0) TEST_ASSERT_EQUAL_STRING(val, "testname");
    else if (strcmp(key, SERV_UUID) == 0)
    {
        bool match_one = (strcmp(val,"00001810-0000-1000-8000-00805F9B34FB")
                || strcmp(val, "0000FFF0-0000-1000-8000-00805F9B34FB"));
        TEST_ASSERT_TRUE_MESSAGE(match_one, "Should have matched one of our service UUIDs");
    }
    else if (strcmp(key, CONNECT_KEY) == 0) TEST_ASSERT_EQUAL_STRING(val, "success");
    else TEST_ASSERT_TRUE_MESSAGE(false, "Did not match any value we were looking for.");
    return 0;
}

void test_advertisded_add_no_services(void)
{

    int err;
    ble_advertisement_t contents =
    {
        .mac = "testmac",
        .is_public_address = true,
        .name = "testname",
        .is_complete_name = true,
        .service_uuids = NULL,
        .num_services = 0,
        .data = NULL,
    };

    struct ble_event_t event =
    {
        .type = BLE_ADVERTISED,
        .mac = "testmac",
        .op =
        {
            .advertise =
            {
                .contents = &contents,
            },
        },
    };

    struct iotm_session session;

    struct plugin_event_t iot_event =
    {
        .ops =
        {
            .add_param_str = test_add_param_str_advertise,
        }
    };

    advert_params = 0;
    err = advertised_add(&session, &iot_event, &event);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, err, "Should have added the advertise without error.");
    TEST_ASSERT_EQUAL_INT(1, advert_params);
}

void test_advertisded_add_two_services(void)
{

    ble_uuid_t services[2] =
    {
        "00001810-0000-1000-8000-00805F9B34FB",
        "0000FFF0-0000-1000-8000-00805F9B34FB"
    };

    int err;
    ble_advertisement_t contents =
    {
        .mac = "testmac",
        .is_public_address = true,
        .name = "testname",
        .is_complete_name = true,
        .service_uuids = services,
        .num_services = 2,
        .data = NULL,
    };

    struct ble_event_t event =
    {
        .type = BLE_ADVERTISED,
        .mac = "testmac",
        .op =
        {
            .advertise =
            {
                .contents = &contents,
            },
        },
    };

    struct iotm_session session;
    struct plugin_event_t iot_event =
    {
        .ops =
        {
            .add_param_str = test_add_param_str_advertise,
        }
    };

    advert_params = 0;
    err = advertised_add(&session, &iot_event, &event);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, err, "Should have added the advertise without error.");
    TEST_ASSERT_EQUAL_INT(3, advert_params);
}


void test_advertisded_add_uninitialized_iot_rule(void)
{

    int err;
    ble_advertisement_t contents =
    {
        .mac = "testmac",
        .is_public_address = true,
        .name = "testname",
        .is_complete_name = true,
        .service_uuids = NULL,
        .num_services = 0,
        .data = NULL,
    };

    struct ble_event_t event =
    {
        .type = BLE_ADVERTISED,
        .mac = "testmac",
        .op =
        {
            .advertise =
            {
                .contents = &contents,
            },
        },
    };

    struct iotm_session session;
    memset(&session, 0, sizeof(session));

    struct plugin_event_t iotev;
    memset(&iotev, 0, sizeof(iotev));

    err = advertised_add(&session, &iotev, &event);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, err, "Should have returned error with no function pointers loaded.");
}


static int conn_params = 0;
int test_add_param_str_connect(struct plugin_event_t *ev, char *key, char *val)
{
    conn_params += 1;
    if (strcmp(key, PUBLIC_ADDR) == 0) TEST_ASSERT_EQUAL_STRING(val, "true");
    else if (strcmp(key, CONNECT_KEY) == 0) TEST_ASSERT_EQUAL_STRING(val, "success");
    else TEST_ASSERT_TRUE_MESSAGE(false, "Did not match any value we were looking for.");
    return 0;
}

void test_advertisded_add_connect_params(void)
{

    int err;

    struct ble_event_t event =
    {
        .type = BLE_CONNECTED,
        .mac = "testmac",
        .op =
        {
            .connection =
            {
                .params =
                {
                    .is_public_addr = true,
                },
                .connection =
                {
                    .status = Ble_Success,
                },
            },
        },
    };

    struct iotm_session session;

    struct plugin_event_t iot_event =
    {
        .ops =
        {
            .add_param_str = test_add_param_str_connect,
        }
    };

    err = add_connected_filters(&session, &iot_event, &event);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, err, "Should have added the filters without error.");
    TEST_ASSERT_EQUAL_INT(2, conn_params);
}


static int serv_discov_params = 0;
int test_add_param_serv_disc(struct plugin_event_t *ev, char *key, char *val)
{
    serv_discov_params += 1;
    if (strcmp(key, SERV_UUID) == 0) TEST_ASSERT_EQUAL_STRING(val, "MyTestUUID");
    else if (strcmp(key, IS_PRIMARY) == 0) TEST_ASSERT_EQUAL_STRING(val, "false");
    else TEST_ASSERT_TRUE_MESSAGE(false, "Did not match any value we were looking for.");
    return 0;
}
void test_add_service_discovery(void)
{
    int err;
    struct ble_event_t event =
    {
        .type = BLE_SERV_DISCOVERED,
        .mac = "ServDiscMac",
        .op =
        {
            .s_discovered =
            {
                .service =
                {
                    .uuid = "MyTestUUID",
                    .is_primary = false,
                },
            },
        },
    };

    struct iotm_session session;
    struct plugin_event_t iot_event =
    {
        .ops =
        {
            .add_param_str = test_add_param_serv_disc,
        }
    };
    serv_discov_params = 0;
    err = add_service_discovery(&session, &iot_event, &event);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, serv_discov_params, "Should have loaded all parameters.");
}

static int char_discov_params = 0;
int test_add_param_char_disc(struct plugin_event_t *ev, char *key, char *val)
{
    char_discov_params += 1;
    if (strcmp(key, CHAR_UUID) == 0) TEST_ASSERT_EQUAL_STRING(val, "MyCharUUID");
    else if (strcmp(key, C_FLAG) == 0) TEST_ASSERT_EQUAL_STRING(val, "ble_char_notify");
    else if (strcmp(key, SERV_UUID) == 0) TEST_ASSERT_EQUAL_STRING(val, "MyServCharDiscUUID");
    else TEST_ASSERT_TRUE_MESSAGE(false, "Did not match any value we were looking for.");
    return 0;
}

void test_add_characteristic_discovery(void)
{

    int err;
    struct ble_event_t event =
    {
        .type = BLE_CHAR_DISCOVERED,
        .mac = "CharDiscMac",
        .op =
        {
            .c_discovered =
            {
                .params =
                {
                    .serv_uuid = "MyServCharDiscUUID",
                },
                .characteristic =
                {
                    .uuid = "MyCharUUID",
                    .flags = BLE_Char_Notify,
                },
            },
        },
    };

    struct iotm_session session;
    struct plugin_event_t iot_event =
    {
        .ops =
        {
            .add_param_str = test_add_param_char_disc,
        }
    };
    char_discov_params = 0;
    err = add_characteristic_discovery(&session, &iot_event, &event);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, char_discov_params, "Should have added flag and UUID.");
}

static int desc_discov_params = 0;
int test_add_param_desc_disc(struct plugin_event_t *ev, char *key, char *val)
{
    desc_discov_params += 1;
    if (strcmp(key, CHAR_UUID) == 0) TEST_ASSERT_EQUAL_STRING(val, "MyCharUUID");
    else if (strcmp(key, DESC_UUID) == 0) TEST_ASSERT_EQUAL_STRING(val, "MyDescUUID");
    else TEST_ASSERT_TRUE_MESSAGE(false, "Did not match any value we were looking for.");
    return 0;
}

void test_add_descriptor_discovery(void)
{

    int err;
    struct ble_descriptor_discovery_params_t params =
    {
        .char_uuid = "MyCharUUID",
    };
    struct ble_event_t event =
    {
        .type = BLE_DESC_DISCOVERED,
        .mac = "DescDiscMac",
        .op =
        {
            .d_discovered =
            {
                .params = params,
                .descriptor =
                {
                    .uuid = "MyDescUUID",
                },
            },
        },
    };

    struct iotm_session session;
    struct plugin_event_t iot_event =
    {
        .ops =
        {
            .add_param_str = test_add_param_desc_disc,
        }
    };
    char_discov_params = 0;
    err = add_descriptor_discovery(&session, &iot_event, &event);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(2, desc_discov_params);
}

static int char_update_params = 0;
int test_add_param_char_update(struct plugin_event_t *ev, char *key, char *val)
{
    char_update_params += 1;
    if (strcmp(key, CHAR_UUID) == 0) TEST_ASSERT_EQUAL_STRING(val, "MyCharUpdateUUID");
    else if (strcmp(key, DATA) == 0) TEST_ASSERT_EQUAL_STRING(val, "00010A0B");
    else if (strcmp(key, IS_NOTIFICATION) == 0) TEST_ASSERT_EQUAL_STRING(val, "true");
    else TEST_ASSERT_TRUE_MESSAGE(false, "Did not match any value we were looking for.");
    return 0;
}
void test_add_characteristic_updated(void)
{

    int err;
    uint8_t l_data[4] = {0, 1, 10, 11};
    barray_t barray =
    {
        .data_length = 4,
        .data = l_data,
    };

    struct ble_event_t event =
    {
        .type = BLE_CHAR_UPDATED,
        .mac = "CharUpdateMac",
        .op =
        {
            .c_updated =
            {
                .is_notification = true,
                .characteristic =
                {
                    .uuid = "MyCharUpdateUUID",
                    .data = &barray,
                },
            },
        },
    };

    struct iotm_session session;
    struct plugin_event_t iot_event =
    {
        .ops =
        {
            .add_param_str = test_add_param_char_update,
        }
    };
    char_update_params = 0;
    err = add_characteristic_updated(&session, &iot_event, &event);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, char_update_params, "Should have added flag and UUID.");
}

static int char_written_params = 0;
int test_add_param_char_written(struct plugin_event_t *ev, char *key, char *val)
{
    char_written_params += 1;
    if (strcmp(key, CHAR_UUID) == 0) TEST_ASSERT_EQUAL_STRING(val, "MyCharWroteUUID");
    else if (strcmp(key, DATA) == 0) TEST_ASSERT_EQUAL_STRING(val, "090A0B0C");
    else if (strcmp(key, S_CODE) == 0) TEST_ASSERT_EQUAL_STRING(val, "30");
    else TEST_ASSERT_TRUE_MESSAGE(false, "Did not match any value we were looking for.");
    return 0;
}

void test_add_characteristic_written(void)
{

    int err;
    uint8_t l_data[4] = {9, 10, 11, 12};
    barray_t barray =
    {
        .data_length = 4,
        .data = l_data,
    };

    struct ble_write_characteristic_params_t params =
    {
        .barray = &barray,
        .char_uuid = "MyCharWroteUUID"
    };

    struct ble_event_t event =
    {
        .type = BLE_CHAR_WRITE_SUCCESS,
        .mac = "CharWroteMac",
        .op =
        {
            .c_written =
            {
                .params = &params,
                .write =
                {
                    .s_code = 30,
                },
            },
        },
    };

    struct iotm_session session;
    struct plugin_event_t iot_event =
    {
        .ops =
        {
            .add_param_str = test_add_param_char_written,
        }
    };

    err = add_characteristic_write_success(&session, &iot_event, &event);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, char_written_params, "Should have added flag and UUID.");

}

static int desc_written_params = 0;
int test_add_param_desc_written(struct plugin_event_t *ev, char *key, char *val)
{
    desc_written_params += 1;
    if (strcmp(key, CHAR_UUID) == 0) TEST_ASSERT_EQUAL_STRING(val, "MyDescWroteCharUUID");
    else if (strcmp(key, DESC_UUID) == 0) TEST_ASSERT_EQUAL_STRING(val, "MyDescWroteDescUUID");
    else if (strcmp(key, DATA) == 0) TEST_ASSERT_EQUAL_STRING(val, "090A0B0D");
    else if (strcmp(key, S_CODE) == 0) TEST_ASSERT_EQUAL_STRING(val, "40");
    else TEST_ASSERT_TRUE_MESSAGE(false, "Did not match any value we were looking for.");
    return 0;
}

void test_add_descriptor_written(void)
{

    int err;
    uint8_t l_data[4] = {9, 10, 11, 13};
    barray_t barray =
    {
        .data_length = 4,
        .data = l_data,
    };

    struct ble_write_descriptor_params_t params =
    {
        .barray = &barray,
        .desc_uuid = "MyDescWroteDescUUID",
        .char_uuid = "MyDescWroteCharUUID",
    };

    struct ble_event_t event =
    {
        .type = BLE_DESC_WRITE_SUCCESS,
        .op =
        {
            .d_written =
            {
                .params = &params,
                .write =
                {
                    .s_code = 40,
                },
            },
        },
    };

    struct iotm_session session;
    struct plugin_event_t iot_event =
    {
        .ops =
        {
            .add_param_str = test_add_param_desc_written,
        }
    };

    err = add_descriptor_write_success(&session, &iot_event, &event);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(4, desc_written_params);

}


int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    log_open("BLE_HANDLER_TEST", LOG_OPEN_STDOUT_QUIET);
    log_severity_set(LOG_SEVERITY_TRACE);


    UnityBegin(test_name);

    RUN_TEST(test_iotm_handler_lookup_session);
    RUN_TEST(test_ble_handler_init_invalid_input);
    RUN_TEST(test_ble_handler_init_valid_input);

    RUN_TEST(test_ble_handler_event_type_conversion);
    RUN_TEST(test_scan_filter_generation);
    RUN_TEST(test_wildcard_scan_values);
    RUN_TEST(test_handler_rule_update); 

    RUN_TEST(test_cmd_from_string);

    RUN_TEST(test_iotm_ble_handle_valid);
    RUN_TEST(test_handle_connect);
    RUN_TEST(test_handle_disable_char_notifications);
    RUN_TEST(test_handle_char_discovery);
    RUN_TEST(test_handle_serv_discovery);
    RUN_TEST(test_handle_char_read);
    RUN_TEST(test_handle_desc_read);
    RUN_TEST(test_handle_char_write);
    RUN_TEST(test_handle_desc_write);

    RUN_TEST(test_emit_advertise);

    RUN_TEST(test_advertisded_add_no_services);
    RUN_TEST(test_advertisded_add_two_services);
    RUN_TEST(test_advertisded_add_uninitialized_iot_rule);
    RUN_TEST(test_advertisded_add_connect_params);
    RUN_TEST(test_add_service_discovery);
    RUN_TEST(test_add_characteristic_discovery);
    RUN_TEST(test_add_descriptor_discovery);
    RUN_TEST(test_add_characteristic_updated);
    RUN_TEST(test_add_characteristic_written);
    RUN_TEST(test_add_descriptor_written);
    return UNITY_END();
}
