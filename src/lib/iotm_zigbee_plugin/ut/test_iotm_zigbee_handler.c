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

#include "test_zigbee_handler.h"

static struct ev_loop *loop;
static int num_events = 0;

int fake_get_param_type(struct plugin_command_t *cmd, char *key, int type, void *val)
{
    int err = 0;
    switch(type)
    {
        case UINT8:
            *(uint8_t *)val = 0xab;
            err = 0;
            break;
        case UINT16:
            *(uint16_t *)val = 0xabcd;
            err = 0;
            break;
        case STRING:
            strcpy(val, "0xabcd");
            err = 0;
            break;
        default:
            err = -1;
    }
    return err;
}

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
    // pass
    iotm_zigbee_handler_exit(&g_session);
}

void setUp(void)
{
    loop = EV_DEFAULT;
    num_events = 0;
    g_session.loop = loop;
    iotm_zigbee_handler_init(&g_session);
}

void test_emit(struct iotm_session *session, struct plugin_event_t *event)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(event, "Should have emitted an event");
    num_events += 1;
}

void test_zigbee_init_invalid_input(void) 
{
    int ret = iotm_zigbee_handler_init(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, ret, "Should have returned an error as no session is being passed");
}

struct iotm_event *fake_get_event(struct iotm_session *_, char *key)
{
    return NULL;
}

void test_zigbee_init_valid_input(void)
{
    struct iotm_session session = {
        .name = "zigbee_handler",
        .topic = "locId/ble/nodeId",
        .location_id = "locationId",
        .node_id = "nodeId",
        .dso = "/usr/plume/lib/zigbee_handler.so",
        .report_count = 0,
        .loop = loop,
        .ops = 
        {
            .emit = test_emit,
            .get_event = fake_get_event,
        },
    };
    int ret = -1;

    printf("before init\n");
    ret = iotm_zigbee_handler_init(&session);
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

void test_log_zigbee_handler(void)
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
        .action = "zigbee_permit_joining",
        .ops = 
        { 
            .get_param_type = fake_get_param_type,
        },
    };

    iotm_zigbee_handler(&session, &cmd);
}

void test_event_getter_error(void)
{
    const struct zigbee_ev_t *check = zigbee_event_from_type(ZIGBEE_DEVICE_ANNCED);
    TEST_ASSERT_EQUAL_STRING("zigbee_device_annced", check->ovsdb_type);
}

static int add_param_calls = 0;
int fake_add_param_type(struct plugin_event_t *iot_ev, char *key, int type, void *input)
{
    add_param_calls += 1;
    return 0;
}

void test_add_device_annced(void)
{

    struct plugin_event_t event =
    {
        .ops =
        {
            .add_param_type = fake_add_param_type,
        },
    };

    struct zigbee_event_t zb_event =
    {
        .op =
        {
            .device_annced =
            {
                .contents =
                {
                    .node_addr = 0x0001,
                },
            },
        },
    };

    add_param_calls = 0;
    add_zigbee_device_annced(&event, &zb_event);
    TEST_ASSERT_EQUAL_INT(1, add_param_calls);
}


void test_add_zigbee_ep_discovered(void)
{
    struct plugin_event_t event =
    {
        .ops =
        {
            .add_param_type = fake_add_param_type,
        },
    };

    zigbee_cluster_id_t input[2] =
    {
        0x0001,
        0x0002,
    };

    zigbee_cluster_id_t output[1] =
    {
        0x0003,
    };

    zigbee_ep_id_t ep_filter[1] =
    {
        0x03,
    };

    struct zigbee_event_t zb_event =
    {
        .op = 
        {
            .ep_discovered = 
            {
                .contents =
                {
                    .ep = 0x01,
                    .device_id = 0x0231,
                    .profile_id = 0x42,
                    .input_clusters = input,
                    .input_cluster_count = 2,
                    .output_clusters = output,
                    .output_cluster_count = 1,
                },
                .params =
                { 
                    .num_endpoint_filters = 1,
                    .endpoint_filter = ep_filter,
                },
            }
        }
    };

    add_param_calls = 0;
    add_zigbee_ep_discovered(&event, &zb_event);
    TEST_ASSERT_EQUAL_INT(7, add_param_calls);
}


void test_add_attr_discovered(void)
{
    struct plugin_event_t event =
    {
        .ops =
        {
            .add_param_type = fake_add_param_type,
        },
    };

    struct zigbee_event_t zb_event =
    {
        .op =
        {
            .attr_discovered =
            {
                .contents =
                {
                    .cluster = { .ep_id = 0x1, .cl_id = 0x2 },
                    .attr_id = 0x02,
                    .attr_data_type = 0x03,
                },
                .params =
                {
                    .start_attribute_id = 0x05,
                    .max_attributes = 10,
                },
            },
        },
    };

    
    add_param_calls = 0;
    add_zigbee_attr_discovered(&event, &zb_event);
    TEST_ASSERT_EQUAL_INT(6, add_param_calls);
}

static int num_comm_recv_added = 0;
int test_add_comm_recv_str(struct plugin_event_t *event, char *key, char *val)
{
    num_comm_recv_added += 1;
    return 0;
}
void test_add_comm_recv_discovered(void)
{
    struct plugin_event_t event =
    {
        .ops =
        {
            .add_param_type = fake_add_param_type,
        },
    };

    zigbee_event_t zb_event =
    {
        .op = 
        { 
            .comm_recv_discovered =
            {
                .contents =
                {
                    .cluster = { .ep_id = 0x05, .cl_id = 0x04 },
                    .comm_id = 0x11,
                },
                .params =
                {
                    .start_command_id = 0x013,
                    .max_commands = 10,
                },
            },
        },
    };

    add_param_calls = 0;
    add_zigbee_comm_recv_discovered(&event, &zb_event);
    TEST_ASSERT_EQUAL_INT(5, add_param_calls);
}

void test_add_zigbee_comm_gen_discovered(void)
{
    struct plugin_event_t event =
    {
        .ops =
        {
            .add_param_type = fake_add_param_type,
        },
    };

    zigbee_event_t  zb_event =
    {
        .op =
        {
            .comm_gen_discovered =
            {
                .contents =
                {
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
                    .comm_id = 0x03,
                },
                .params =
                {
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
                    .start_command_id = 0x01,
                    .max_commands = 10,
                },
            },
        },
    };
    add_param_calls = 0;
    add_zigbee_comm_gen_discovered(&event, &zb_event);
    TEST_ASSERT_EQUAL_INT(5, add_param_calls);
}


static int num_attr_val_recv_added = 0;
int test_add_attr_val_recv_str(struct plugin_event_t *event, char *key, char *val)
{
    num_attr_val_recv_added += 1;
    return 0;
}

void test_add_zigbee_attr_value_recieved(void)
{
    struct plugin_event_t event =
    {
        .ops =
        {
            .add_param_str = test_add_attr_val_recv_str,
            .add_param_type = fake_add_param_type,
        },
    };

    // data to configure reporting
    uint8_t r_data[2] = { 0x05, 0xa2 };
    zb_barray_t rc_data =
    {
        .data = r_data,
        .data_length = 2,
    };

    // data recieved from report
    uint8_t l_data[2] = { 0x02, 0x02 };
    zb_barray_t data =
    {
        .data = l_data,
        .data_length = 2,
    };

    zigbee_attr_value_t attr_value =
    {
        .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
        .attr_id = 0x04,
        .attr_data_type = 0x05,
        .attr_value = data,
    };

    struct zigbee_event_t zb_event =
    {
        .op =
        {
            .attr_value =
            {
                .is_report = true,
                .r_params =
                {
                    .data = rc_data,
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02},
                },
                .contents = attr_value,
            }
        }
    };

    add_param_calls = 0;
    add_zigbee_attr_value_received(&event, &zb_event);
    TEST_ASSERT_EQUAL_INT(3, num_attr_val_recv_added);
    TEST_ASSERT_EQUAL_INT(6, add_param_calls);
}

static int num_attr_write_suc_added = 0;
int test_add_attr_write_suc_str(struct plugin_event_t *event, char *key, char *val)
{
    num_attr_write_suc_added += 1;
    return 0;
}
void test_add_zigbee_attr_write_success(void)
{
    struct plugin_event_t event =
    {
        .ops =
        {
            .add_param_str = test_add_attr_write_suc_str,
            .add_param_type = fake_add_param_type,
        },
    };

    uint8_t l_data[2] = { 0x02, 0x02 };
    zb_barray_t data =
    {
        .data = l_data,
        .data_length = 2,
    };

    struct zigbee_event_t zb_event =
    {
        .op = 
        {
            .attr_write =
            {
                .params =
                {
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
                    .data = data,
                },
                .contents =
                {
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
                    .attr_id = 0x03,
                    .write = { .s_code = 0x05 },
                },
            },
        },
    };

    add_param_calls = 0;
    add_zigbee_attr_write_success(&event, &zb_event);
    TEST_ASSERT_EQUAL_INT(1, num_attr_write_suc_added);
    TEST_ASSERT_EQUAL_INT(6, add_param_calls);
}

static int num_report_configured_success_added = 0;
int test_add_zigbee_report_configured_success_str(struct plugin_event_t *event, char *key, char *val)
{
    num_report_configured_success_added += 1;
    return 0;
}
void test_add_zigbee_report_configured_success(void)
{
    struct plugin_event_t event =
    {
        .ops =
        {
            .add_param_str = test_add_zigbee_report_configured_success_str,
            .add_param_type = fake_add_param_type,
        },
    };

    uint8_t l_data[2] = { 0x02, 0x02 };
    zb_barray_t data =
    {
        .data = l_data,
        .data_length = 2,
    };
    zigbee_event_t zb_event =
    {
        .op =
        {
            .report_success =
            {
                .params =
                {
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
                    .data = data,
                },
                .contents =
                {
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
                    .attr_id = 0x03,
                    .report = { .s_code = 0x04 },
                },
            },
        },
    };

    add_param_calls = 0;
    add_zigbee_report_configed_success(&event, &zb_event);
    TEST_ASSERT_EQUAL_INT(1, num_report_configured_success_added);
    TEST_ASSERT_EQUAL_INT(6, add_param_calls);
}

static int num_report_conf_recv_added = 0;
int test_add_report_conf_recv_str(struct plugin_event_t *event, char *key, char *val)
{
    num_report_conf_recv_added += 1;
    return 0;
}
void test_add_zigbee_report_config_received(void)
{
    struct plugin_event_t event =
    {
        .ops =
        {
            .add_param_str = test_add_report_conf_recv_str,
            .add_param_type = fake_add_param_type,
        },
    };

    zigbee_attr_id_t attribute[1] =
    {
        0x023,
    };

    zigbee_report_config_record_t record =
    {
        .attr_data_type = 0x01,
        .min_report_interval = 2,
        .max_report_interval = 60,
        .timeout_period = 30,
    };

    zigbee_event_t zb_event =
    {
        .op =
        {
            .report_config =
            {
                .params =
                {
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
                    .attribute = attribute,
                    .num_attributes = 1
                },
                .contents =
                {
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
                    .attr_id = 0x023,
                    .r_configured = true,
                    .record = &record,
                },
            },
        }
    };

    add_param_calls = 0;
    add_zigbee_report_config_received(&event, &zb_event);
    TEST_ASSERT_EQUAL_INT(1, num_report_conf_recv_added);
    TEST_ASSERT_EQUAL_INT(10, add_param_calls);
}

static int num_add_zb_default_added = 0;
int test_add_add_zb_default_str(struct plugin_event_t *event, char *key, char *val)
{
    num_add_zb_default_added += 1;
    return 0;
}
void test_add_zigbee_default_response(void)
{
    struct plugin_event_t event =
    {
        .ops =
        {
            .add_param_str = test_add_add_zb_default_str,
            .add_param_type = fake_add_param_type,
        },
    };

    uint8_t l_data[2] = { 0x02, 0x02 };
    zb_barray_t data =
    {
        .data = l_data,
        .data_length = 2,
    };
    zigbee_command_id_t cmd_id = 0x04;
    zigbee_event_t zb_event =
    {
        .op =
        {
            .default_response =
            {
                .params =
                {
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
                    .command_id = cmd_id,
                    .data = &data,
                },
                .contents =
                {
                    .cluster = { .ep_id = 0x01, .cl_id = 0x02 },
                    .comm_id = cmd_id,
                    .status = { .s_code = 0x05 },
                },
            },
        },
    };

    add_param_calls = 0;
    add_zigbee_default_response(&event, &zb_event);
    TEST_ASSERT_EQUAL_INT(1, num_add_zb_default_added);
    TEST_ASSERT_EQUAL_INT(4, add_param_calls);
}

char *fake_cluster_params(struct plugin_command_t *_, char *key)
{
    if (strcmp(key, ZB_EP) == 0) return "0x01";
    else if (strcmp(key, ZB_CLUSTER_ID) == 0) return "0x0002";
    else return "unknown_param";
}

void test_get_cluster_param(void)
{
    int err = -1;
    struct plugin_command_t cmd =
    {
        .ops = { .get_param = fake_cluster_params, .get_param_type = fake_get_param_type }
    };
    zigbee_cluster_t cluster;
    memset(&cluster, 0, sizeof(cluster));

    err = get_cluster_param(&cmd, &cluster);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT8(0xab, cluster.ep_id);
    TEST_ASSERT_EQUAL_INT16(0xabcd, cluster.cl_id);
}

char *fake_data_params(struct plugin_command_t *_, char *key)
{
    if (strcmp(key, DECODE_TYPE) == 0) return "hex";
    else if (strcmp(key, ZB_DATA) == 0) return "0x00ab";
    else return "unknown";
}

void test_get_data_param(void)
{
    int err = -1;
    struct plugin_command_t cmd =
    {
        .ops = { .get_param = fake_data_params, .get_param_type = fake_get_param_type },
    };
    zb_barray_t zb_barray;
    memset(&zb_barray, 0, sizeof(zb_barray));
    err = get_data_param(&cmd, &zb_barray);
    TEST_ASSERT_EQUAL_INT(0, err);

    unsigned char check[2] = { 0x00, 0xab };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(check, zb_barray.data, 2);
    TEST_ASSERT_EQUAL_INT(2, zb_barray.data_length);

    free_data_param(&zb_barray);
}

void test_get_uint16(void)
{
    int err = -1;
    struct plugin_command_t cmd = { .ops = { .get_param_type = fake_get_param_type } };
    zigbee_attr_id_t attr;
    err = get_uint16_param(&cmd, ZB_ATTR_ID, &attr);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT16(0xabcd, attr);
}

void fake_foreach_param_type_uint16(
        struct plugin_command_t *self,
        char *key,
        int type,
        void (*cb)(char *key, void *val, void *ctx),
        void *ctx)
{
    uint16_t vars[3] =
    {
        0x0ab1,
        0x0002,
        0xffff,
    };
    cb("tmp", &vars[0], ctx);
    cb("tmp", &vars[1], ctx);
    cb("tmp", &vars[2], ctx);
}

void test_load_uint16_params(void)
{
    uint16_t *output = NULL;
    uint8_t num = -1;
    int err = -1;
    struct iotm_tree_t tree =
    {
        .len = 3,
    };
    struct plugin_command_t cmd =
    {
        .ops =
        {
            .foreach_param_type = fake_foreach_param_type_uint16,
        },
        .params = &tree,
    };

    err = alloc_and_load_uint16_params(&cmd, "doesntmatter", &output, &num);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(3, num);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_EQUAL_UINT16(0x0ab1, output[0]);
    TEST_ASSERT_EQUAL_UINT16(0x0002, output[1]);
    TEST_ASSERT_EQUAL_UINT16(0xffff, output[2]);
    free(output);
}

void fake_foreach_param_type_uint8(
        struct plugin_command_t *self,
        char *key,
        int type,
        void (*cb)(char *key, void *val, void *ctx),
        void *ctx)
{
    uint8_t vars[3] =
    {
        0x0a,
        0x02,
        0xff,
    };
    cb("tmp", &vars[0], ctx);
    cb("tmp", &vars[1], ctx);
    cb("tmp", &vars[2], ctx);
}

void test_load_uint8_params(void)
{
    uint8_t *output = NULL;
    uint8_t num = -1;
    int err = -1;
    struct iotm_tree_t tree =
    {
        .len = 3,
    };
    struct plugin_command_t cmd =
    {
        .ops =
        {
            .foreach_param_type = fake_foreach_param_type_uint8,
        },
        .params = &tree,
    };

    err = alloc_and_load_uint8_params(&cmd, "doesntmatter", &output, &num);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(3, num);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_EQUAL_UINT8(0x0a, output[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, output[1]);
    TEST_ASSERT_EQUAL_UINT8(0xff, output[2]);
    free(output);
}

static bool ran_timer = false;
static void test_timer_cb (EV_P_ ev_timer *w, int revents)
{
    struct timer_data_t *data = (struct timer_data_t *)w->data;
    if (data != NULL) 
    {
        TEST_ASSERT_TRUE(true);
    }
    else
    {
        TEST_ASSERT_TRUE_MESSAGE(false, "Data should not have been NULL");
    }
    ran_timer = true;
    free_pairing_node(data->parent);
}
void test_timer_node_alloc_free(void)
{
    struct pairing_node_t *timers = NULL;
    int err = -1;
    struct iotm_zigbee_session zb_session;
    time_t start = time(NULL);
    int duration = 35;

    err = init_pairing_node(&zb_session, start, duration, &timers);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_NOT_NULL(timers);
    ev_timer_init (&timers->watcher, test_timer_cb, 0.00, 0.);
    ev_timer_start (loop, &timers->watcher);
    ev_run(loop, 0);
    TEST_ASSERT_TRUE(ran_timer);
}

int fake_get_pairing_type(struct iotm_tree_t *self, char *key, int type, void *out)
{
    if (strcmp(key, ZB_PAIRING_START) == 0)
    {
        *(long *)out = 123456;
        return 0;
    }
    
    if (strcmp(key, ZB_PAIRING_DURATION) == 0)
    {
        *(int *)out = 45;
        return 0;
    }
    return -1;
}
void test_get_pairing_params(void)
{
    struct iotm_tree_t params =
    {
        .get_type = fake_get_pairing_type,
    };
    struct iotm_rule rule =
    {
        .params = &params,
    };

    time_t check_start;
    int check_dur;
    int err;

    err = get_pairing_params(&rule, &check_start, &check_dur);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(123456, check_start);
    TEST_ASSERT_EQUAL_INT(45, check_dur);
}

void test_is_in_past(void)
{
    bool check = false;
    time_t past = time(NULL) - 45;
    time_t current = time(NULL);

    check = is_in_past(past);
    TEST_ASSERT_TRUE(check);

    check = is_in_past(current);
    TEST_ASSERT_TRUE(!check);
}

void test_queue_timer(void)
{
    int err = -1;
    struct iotm_session session = 
    {
        .loop = EV_DEFAULT,
    };


    time_t now = time(NULL);
    int duration = 45;
    struct iotm_zigbee_session *zb_session = NULL;

    zb_session = iotm_zigbee_lookup_session(&session);
    TEST_ASSERT_NOT_NULL(zb_session);
    TEST_ASSERT_NOT_NULL(zb_session->session);

    struct pairing_node_t *node = NULL;
    init_pairing_node(zb_session, now, duration, &node);

    err = queue_timer(zb_session, node);
    ev_run(loop, 0);
    TEST_ASSERT_EQUAL_INT(0, err);
    TEST_ASSERT_EQUAL_INT(now + duration, zb_session->join_until);
}

void test_permit_joining_other_pairing_running(void)
{
    int err = -1;
    struct iotm_session session = 
    {
        .loop = EV_DEFAULT,
    };

    time_t now = time(NULL);
    int duration = 45;
    struct iotm_zigbee_session *zb_session = NULL;
    zb_session = iotm_zigbee_lookup_session(&session);
    TEST_ASSERT_NOT_NULL(zb_session);
    TEST_ASSERT_NOT_NULL(zb_session->session);

    struct timer_data_t data =
    {
        .zb_session = zb_session,
        .start_time = now,
        .duration = duration,
        .parent = NULL,
    };

    // pairing is enabled until 3 seconds longer than current request
    long fake_prior_pairing = now + duration + 3;
    zb_session->join_until = fake_prior_pairing;

    err = permit_joining(&data);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
            -1,
            err,
            "Should not have enabled pairing as there is another active request for longer.");

    TEST_ASSERT_EQUAL_INT_MESSAGE(
            fake_prior_pairing,
            zb_session->join_until,
            "Pair until shoud not have been updated to different time.");
}

void test_permit_joining_valid(void)
{
    int err = -1;
    struct iotm_session session = 
    {
        .loop = EV_DEFAULT,
    };

    time_t now = time(NULL);
    int duration = 45;
    struct iotm_zigbee_session *zb_session = NULL;
    zb_session = iotm_zigbee_lookup_session(&session);
    TEST_ASSERT_NOT_NULL(zb_session);
    TEST_ASSERT_NOT_NULL(zb_session->session);

    struct timer_data_t data =
    {
        .zb_session = zb_session,
        .start_time = now,
        .duration = duration,
        .parent = NULL,
    };

    // pairing is enabled until 3 seconds shorter than current request
    long fake_prior_pairing = now + duration - 3;
    zb_session->join_until = fake_prior_pairing;

    err = permit_joining(&data);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
            0,
            err,
            "Should have updated pairing timer");

    TEST_ASSERT_EQUAL_INT_MESSAGE(
            now + duration,
            zb_session->join_until,
            "Join until should reflect current request");
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    log_open("ZIGBEE_HANDLER_TEST", LOG_OPEN_STDOUT_QUIET);
    log_severity_set(LOG_SEVERITY_TRACE);


    UnityBegin(test_name);

    RUN_TEST(test_timer_node_alloc_free);
    RUN_TEST(test_zigbee_init_invalid_input);
    RUN_TEST(test_zigbee_init_valid_input);
    RUN_TEST(test_log_zigbee_handler);
    RUN_TEST(test_event_getter_error);
    RUN_TEST(test_add_device_annced);
    RUN_TEST(test_add_zigbee_ep_discovered);
    RUN_TEST(test_add_attr_discovered);
    RUN_TEST(test_add_comm_recv_discovered);
    RUN_TEST(test_add_zigbee_comm_gen_discovered);
    RUN_TEST(test_add_zigbee_attr_value_recieved);
    RUN_TEST(test_add_zigbee_attr_write_success);
    RUN_TEST(test_add_zigbee_report_configured_success);
    RUN_TEST(test_add_zigbee_report_config_received);
    RUN_TEST(test_add_zigbee_default_response);
    RUN_TEST(test_get_cluster_param);
    RUN_TEST(test_get_data_param);
    RUN_TEST(test_get_uint16);
    RUN_TEST(test_load_uint16_params);
    RUN_TEST(test_load_uint8_params);
    RUN_TEST(test_get_pairing_params);
    RUN_TEST(test_is_in_past);
    RUN_TEST(test_queue_timer);
    RUN_TEST(test_permit_joining_other_pairing_running);
    RUN_TEST(test_permit_joining_valid);

    return UNITY_END();
}
