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

#include "iotm_ble_handler.h"

static struct iotm_ble_handler_cache
cache_mgr =
{
    .initialized = false,
};

    struct iotm_ble_handler_cache *
iotm_ble_handler_get_mgr(void)
{
    return &cache_mgr;
}

char *bool_to_str(bool val)
{
    return (val) ? "true" : "false";
}

bool str_to_bool(char *val)
{
    return strcasecmp("true", val) == 0;
}

void *get_ctx(struct iotm_session *session)
{
    if (session == NULL || session->tl_ctx_tree == NULL) return NULL;
    void **ctx_ptr = NULL;
    struct tl_context_tree_t *tree = session->tl_ctx_tree;
    ctx_ptr = tree->get(tree, TL_KEY);
    return *ctx_ptr;
}

void **get_ctx_ptr(struct iotm_session *session)
{
    if (session == NULL || session->tl_ctx_tree == NULL) return NULL;
    void **ctx_ptr = NULL;
    struct tl_context_tree_t *tree = session->tl_ctx_tree;
    ctx_ptr = tree->get(tree, TL_KEY);
    return ctx_ptr;
}


/**
 * @brief Decoder and decoder helpers
 */
///{@
int bin2hex(unsigned char * in, size_t insz, char * out, size_t outsz)
{
    if (insz * 2 > outsz) return -1;

    unsigned char * pin = in;
    const char * hex = "0123456789ABCDEF";
    char * pout = out;
    for(; pin < in+insz; pout +=2, pin++)
    {
        pout[0] = hex[(*pin>>4) & 0xF];
        pout[1] = hex[ *pin     & 0xF];
        if ((size_t)( pout + 2 - out ) > outsz) break;
    }
    out[insz * 2] = '\0';
    return 0;
}

int hex2bin(char *source_str, unsigned char *dest_buffer)
{
    char *line = source_str;
    char *data = line;
    int offset;
    int read_byte;
    int data_len = 0;

    while (sscanf(data, " %02x%n", &read_byte, &offset) == 1)
    {
        dest_buffer[data_len++] = read_byte;
        data += offset;
    }
    return data_len;
}

int not_impl_to_bytes(char *pos, unsigned char *val)
{
    LOGE("%s: type not implemented yet.\n", __func__);
    return -1;
}
///@}


/**
 * @brief mappings from strings to OVSDB type
 *
 * @note keeps magic strings out of plugin, converted with ble_event_type()
 */
static struct decode_type decode_map[] =
{
    {
        .ovsdb_type = "hex",
        .type = HEX,
        .decoder = hex2bin,
    },
    {
        .ovsdb_type = "UTF8",
        .type = UTF8,
        .decoder = not_impl_to_bytes,
    },
};

/**
 * @brief mappings from strings to OVSDB type
 *
 * @note keeps magic strings out of plugin, converted with ble_event_type()
 */
static const struct ble_type events_map[] =
{
    {
        .ovsdb_type = "error",
        .event_type = ERROR,
        .add_params = default_add,
    },
    {
        .ovsdb_type = "ble_unknown",
        .event_type = BLE_UNKNOWN,
        .add_params = default_add,
    },
    {
        .ovsdb_type = "ble_error",
        .event_type = BLE_ERROR,
        .add_params = default_add,
    },
    {
        .ovsdb_type = "ble_advertised",
        .event_type = BLE_ADVERTISED,
        .add_params = advertised_add,
    },
    {
        .ovsdb_type = "ble_connected",
        .event_type = BLE_CONNECTED,
        .add_params = add_connected_filters,
    },
    {
        .ovsdb_type = "ble_disconnected",
        .event_type = BLE_DISCONNECTED,
        .add_params = add_disconnected_params,
    },
    {
        .ovsdb_type = "ble_serv_discovered",
        .event_type = BLE_SERV_DISCOVERED,
        .add_params = add_service_discovery,
    },
    {
        .ovsdb_type = "ble_char_discovered",
        .event_type = BLE_CHAR_DISCOVERED,
        .add_params = add_characteristic_discovery,
    },
    {
        .ovsdb_type = "ble_desc_discovered",
        .event_type = BLE_DESC_DISCOVERED,
        .add_params = add_descriptor_discovery,
    },
    {
        .ovsdb_type = "ble_char_updated",
        .event_type = BLE_CHAR_UPDATED,
        .add_params = add_characteristic_updated,
    },
    {
        .ovsdb_type = "ble_desc_updated",
        .event_type = BLE_DESC_UPDATED,
        .add_params = default_add,
    },
    {
        .ovsdb_type = "ble_char_write_success",
        .event_type = BLE_CHAR_WRITE_SUCCESS,
        .add_params = add_characteristic_write_success,
    },
    {
        .ovsdb_type = "ble_desc_write_success",
        .event_type = BLE_DESC_WRITE_SUCCESS,
        .add_params = add_descriptor_write_success,
    },
    {
        .ovsdb_type = "ble_char_notify_success",
        .event_type = BLE_CHAR_NOTIFY_SUCCESS,
        .add_params = add_char_notify_success,
    },
};

/**
 * @brief mappings from strings to OVSDB type
 *
 * @note keeps magic strings out of plugin, converted with ble_event_type()
 */
static const struct ble_cmd_t commands_map[] =
{
    {
        .ovsdb_type = "ble_connect_device",
        .command_type = BLE_CONNECT_DEVICE,
        .handle_cmd = handle_connect,
    },
    {
        .ovsdb_type = "ble_disable_characteristic_notifications",
        .command_type = BLE_DISABLE_CHARACTERISTIC_NOTIFICATIONS,
        .handle_cmd = handle_disable_char_notifications,
    },
    {
        .ovsdb_type = "ble_enable_characteristic_notifications",
        .command_type = BLE_ENABLE_CHARACTERISTIC_NOTIFICATIONS,
        .handle_cmd = handle_enable_char_notifications,
    },
    {
        .ovsdb_type = "ble_disconnect_device",
        .command_type = BLE_DISCONNECT_DEVICE,
        .handle_cmd = handle_disconnect_device,
    },
    {
        .ovsdb_type = "ble_discover_characteristics",
        .command_type = BLE_DISCOVER_CHARACTERISTICS,
        .handle_cmd = handle_discover_chars,
    },
    {
        .ovsdb_type = "ble_discover_services",
        .command_type = BLE_DISCOVER_SERVICES,
        .handle_cmd =  handle_discover_servs,
    },
    {
        .ovsdb_type = "ble_read_characteristic",
        .command_type = BLE_READ_CHARACTERISTIC,
        .handle_cmd = handle_char_read,
    },
    {
        .ovsdb_type = "ble_read_descriptor",
        .command_type = BLE_READ_DESCRIPTOR,
        .handle_cmd = handle_desc_read,
    },
    {
        .ovsdb_type = "ble_write_characteristic",
        .command_type = BLE_WRITE_CHARACTERISTIC,
        .handle_cmd = handle_char_write,
    },
    {
        .ovsdb_type = "ble_write_descriptor",
        .command_type = BLE_WRITE_DESCRIPTOR,
        .handle_cmd = handle_desc_write,
    },
};

struct decode_type *decode_from_str(char *c_type)
{
    struct decode_type *map;
    size_t nelems;
    size_t i;
    int cmp;

    /* Walk the known encodings */
    nelems = (sizeof(decode_map) / sizeof(decode_map[0]));
    map = decode_map;
    for (i = 0; i < nelems; i++)
    {
        cmp = strcmp(c_type, map->ovsdb_type);
        if (cmp == 0) return map;
        map++;
    }

    /* No known type recognized */
    return NULL;
}

struct decode_type *decode_from_type(int type)
{
    struct decode_type *map;
    size_t nelems;
    size_t i;

    /* Walk the known encodings */
    nelems = (sizeof(decode_map) / sizeof(decode_map[0]));
    map = decode_map;
    for (i = 0; i < nelems; i++)
    {
        if (type == map->type) return map;
        map++;
    }

    /* No known type recognized */
    return NULL;
}

const struct ble_cmd_t *ble_cmd_from_string(char *c_type)
{
    const struct ble_cmd_t *map;
    size_t nelems;
    size_t i;
    int cmp;

    /* Walk the known commands */
    nelems = (sizeof(commands_map) / sizeof(commands_map[0]));
    map = commands_map;
    for (i = 0; i < nelems; i++)
    {
        cmp = strcmp(c_type, map->ovsdb_type);
        if (cmp == 0) return map;

        map++;
    }

    /* No known type recognized */
    return NULL;
}

const struct ble_type *ble_event_from_type(const enum event_type type)
{
    const struct ble_type *map;
    size_t nelems;
    size_t i;

    /* Walk the known events */
    nelems = (sizeof(events_map) / sizeof(events_map[0]));
    map = events_map;
    for (i = 0; i < nelems; i++)
    {
        if (type == map->event_type) return map;

        map++;
    }

    /* No known type recognized */
    return NULL;
}

event_type ble_event_type(char *c_type)
{
    const struct ble_type *map;
    size_t nelems;
    size_t i;
    int cmp;

    /* Walk the known events */
    nelems = (sizeof(events_map) / sizeof(events_map[0]));
    map = events_map;
    for (i = 0; i < nelems; i++)
    {
        cmp = strcmp(c_type, map->ovsdb_type);
        if (cmp == 0) return map->event_type;

        map++;
    }

    /* No known type recognized */
    return BLE_UNKNOWN;
}

    int
iotm_ble_handler_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

    void
iotm_ble_handler_free_session(struct iotm_ble_handler_session *i_session)
{
    // cleanup rules if necessary
    if (i_session) free(i_session);
}

    void
iotm_ble_handler_delete_session(struct iotm_session *session)
{
    if (session == NULL) return;
    struct iotm_ble_handler_cache *mgr;
    struct iotm_ble_handler_session *i_session;
    ds_tree_t *sessions;

    mgr = iotm_ble_handler_get_mgr();
    sessions = &mgr->iotm_sessions;

    if (sessions == NULL) return;

    i_session = ds_tree_find(sessions, session);
    if (i_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, i_session);
    iotm_ble_handler_free_session(i_session);

    return;
}

    struct iotm_ble_handler_session *
iotm_ble_handler_lookup_session(struct iotm_session *session)
{
    struct iotm_ble_handler_cache *mgr;
    struct iotm_ble_handler_session *i_session;
    ds_tree_t *sessions;

    mgr = iotm_ble_handler_get_mgr();
    sessions = &mgr->iotm_sessions;
    if (sessions == NULL) return NULL;

    i_session = ds_tree_find(sessions, session);
    if (i_session != NULL) return i_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    i_session = calloc(1, sizeof(struct iotm_ble_handler_session));
    if (i_session == NULL) return NULL;

    ds_tree_insert(sessions, i_session, session);

    return i_session;
}

    void
iotm_ble_handler_periodic(struct iotm_session *session)
{
    if (session->topic == NULL) return;
    // any periodic work here
    return;
}

    void
iotm_ble_handler_exit(struct iotm_session *session)
{
    bool ret = false;
    if (session == NULL) return;

    void *tl_context = get_ctx(session);
    struct iotm_ble_handler_cache *mgr;
    mgr = iotm_ble_handler_get_mgr();
    if (!mgr->initialized) return;

    // cleanup logic if necessary here
    ret = ble_exit(tl_context);

    if (ret) tl_context = NULL;
    iotm_ble_handler_delete_session(session);
}

    void
iotm_ble_handler_update(struct iotm_session *session)
{
    return;
}

int alloc_scan_filter_params(
        size_t num_macs,
        size_t num_uuids,
        ble_discovery_scan_params_t *params)
{
    if (num_macs > 0)
    {
        params->mac_filter = (ble_mac_t*)malloc(params->num_mac_filters * sizeof(ble_mac_t));
    }

    if (num_uuids > 0)
    {
        params->uuid_filter = (ble_uuid_t*)malloc(params->num_uuid_filters * sizeof(ble_uuid_t));
    }

    return 0;
};

int free_scan_filter_params(ble_discovery_scan_params_t *params)
{
    if (params == NULL) return -1;
    if (params->mac_filter) free(params->mac_filter);
    if (params->uuid_filter) free(params->uuid_filter);
    return 0;
}

void get_filter_sizes(
        ds_list_t *dl,
        struct iotm_value_t *val,
        void *ctx
)
{
    struct adv_sizes_t *sizes;
    char *value = val->value;
    char *key = val->key;

    sizes = (struct adv_sizes_t *) ctx;

    if (strcmp(key, MAC_KEY) == 0)
    {
        if (strcmp(value, WLD_KEY) == 0) sizes->mac_wld = true;
        sizes->mac_len += 1;
    }
    if (strcmp(key, SERV_UUID) == 0)
    {
        if (strcmp(value, WLD_KEY) == 0) sizes->uuid_wld = true;
        sizes->uuid_len += 1;
    }
}

void load_filters(
        ds_list_t *dl,
        struct iotm_value_t *val,
        void *ctx
)
{
    struct adv_contents_t *contents;
    struct ble_discovery_scan_params_t *params = NULL;

    char *value = val->value;
    char *key = val->key;
    contents = (struct adv_contents_t *) ctx;
    params = contents->params;

    if (params->num_mac_filters <= 0 && strcmp(key, MAC_KEY) == 0) return;
    if (params->num_uuid_filters <= 0 && strcmp(key, SERV_UUID) == 0) return;

    if (strcmp(key, MAC_KEY) == 0)
    {
        strcpy(params->mac_filter[contents->mac_index], value);
        contents->mac_index += 1;
    }
    if (strcmp(key, SERV_UUID) == 0)
    {
        strcpy(params->uuid_filter[contents->uuid_index], value);
        contents->uuid_index += 1;
    }
}

int get_scan_params(
        struct iotm_event *event,
        ble_discovery_scan_params_t *params
)
{
    int err;
    void *size_ctx = NULL;
    struct adv_sizes_t *sizes;
    void *load_ctx = NULL;
    struct adv_contents_t *loaded;

    size_ctx = calloc(1, sizeof(adv_sizes_t));
    event->foreach_filter(event, get_filter_sizes, size_ctx);

    sizes = (struct adv_sizes_t *) size_ctx;

    if (sizes->mac_len == 0 && sizes->uuid_len == 0) return 1;

    // Allocate based off matches in rules, should be NULL if wildcard was
    // found
    params->num_mac_filters = (sizes->mac_wld) ? 0 : sizes->mac_len;
    params->num_uuid_filters = (sizes->uuid_wld) ? 0 : sizes->uuid_len;

    err = alloc_scan_filter_params(
            params->num_mac_filters,
            params->num_uuid_filters,
            params);

    if (err)
    {
        free_scan_filter_params(params);
        return -1;
    }

    load_ctx = calloc(1, sizeof(adv_contents_t));
    loaded = (struct adv_contents_t *) load_ctx;
    loaded->params = params;

    event->foreach_filter(event, load_filters, load_ctx);

    if (size_ctx) free(size_ctx);
    if (load_ctx) free(load_ctx);
    return 0;
}

struct discovery_timer_t
{
    size_t retries;
    bool uart_ready;
    size_t count;
    ble_discovery_scan_params_t *params;
    void *tl_context;
    struct ev_loop *loop;
};

int start_scan(void *tl_context, ble_discovery_scan_params_t *params)
{
    bool ret = false;
    ret = ble_enable_discovery_scan(
            tl_context, 
            params);
    if (!ret)
    {
        LOGE("%s: Error enabling a discovery scan.\n", __func__);
        return -1;
    }
    else
    {
        LOGI("Started new scan for  macs->[%d] uuids->[%d]\n",
                params->num_mac_filters,
                params->num_uuid_filters);
        for (int i = 0; i < params->num_mac_filters; i++)
        {
            LOGI("scanning for mac: [%s]\n",
                    params->mac_filter[i]);
        }

        LOGI("%s: Updated the discovery scan parameters.\n", __func__);
        return 0;
    }
}

ev_timer scan_timer;
static struct discovery_timer_t timer_data;
static void validate_uart_cb(EV_P_ ev_timer *w, int revents)
{
    if (timer_data.count >= timer_data.retries)
    {
        LOGE("%s: Never able to establish UART connection. Done trying.\n",
                __func__);
        return;
    }

    LOGI("%s: UART was unavailible, retrying connection... \n", __func__);

    timer_data.count += 1;
    timer_data.uart_ready = ble_disable_discovery_scan(timer_data.tl_context);
    if (!timer_data.uart_ready)
    {
        ev_timer_again(timer_data.loop, &scan_timer);
    }
    else
    {
        start_scan(timer_data.tl_context, timer_data.params);
    }
}

int refresh_discovery_scan(
        ble_discovery_scan_params_t *params,
        void *tl_context,
        struct ev_loop *loop)
{

    LOGI("%s Scan paremeters exist, setting up scan.\n", __func__);
    int err = -1;
    timer_data.retries = SCAN_RETRY_COUNT,
        timer_data.uart_ready = false,
        timer_data.count = 0,
        timer_data.params = params,
        timer_data.tl_context = tl_context,
        timer_data.loop = loop,


        timer_data.uart_ready = ble_disable_discovery_scan(tl_context);

    if (!timer_data.uart_ready)
    {
        ev_timer_init(&scan_timer, validate_uart_cb, SCAN_BACKOFF_TIME, 0.);
        ev_timer_start(loop, &scan_timer);
    }
    else
    {
        err = start_scan(tl_context, params);
        if (!err)
        {
            LOGI("%s: Started a discovery scan.\n", __func__);
            return 0;
        }
        else
        {
            LOGE("%s: Error starting the discover scan, return code: [%d]\n",
                    __func__, err);
            return -1;
        }
    }
    return -1;
}

void reload_scan(struct iotm_session *session)
{    
    struct iotm_ble_handler_session *ble_session = NULL;
    int err = -1;
    ble_discovery_scan_params_t *params = NULL;
    void *tl_context = NULL;

    tl_context = get_ctx(session);

    ble_session = iotm_ble_handler_lookup_session(session);
    if (ble_session == NULL) return;
    const struct ble_type *m_type = ble_event_from_type(BLE_ADVERTISED);

    // Get any current advertise events
    struct iotm_event *event = session->ops.get_event(session, m_type->ovsdb_type);
    if (event == NULL)
    {
        LOGI("%s: Found no rules matching [%s] event. Disabled scan.\n",
                __func__, m_type->ovsdb_type);
        ble_disable_discovery_scan(tl_context);
        return;
    }

    params = &ble_session->current_scan_params;
    free_scan_filter_params(params);

    err = get_scan_params(event, params);
    if (err == 1)
    {
        // not scanning for anything, turn off scan and return
        LOGI("%s: No values of interest, disabling BLE Scan.\n",
                __func__);
        ble_disable_discovery_scan(tl_context);
        return;
    }

    if (err)
    {
        LOGE("%s: Failed to get the scan parameters\n", __func__);
        free_scan_filter_params(params);
        return;
    }

    err = refresh_discovery_scan(params, tl_context, session->loop);

    if (err)
    {
        LOGE("%s: Could not refresh the discovery scan params.",
                __func__);
    }
}

void iotm_ble_handler_tag_update(struct iotm_session *session)
{
    LOGI("Reloading due to tag update\n");
    reload_scan(session);
}

void iotm_ble_handler_rule_update(
        struct iotm_session *session,
        ovsdb_update_monitor_t *mon,
        struct iotm_rule *rule)
{
    event_type type = ble_event_type(rule->event);
    if (type != BLE_ADVERTISED)
    {
        LOGI("%s: Rule update caught, not advertise rule, doing nothing.\n", __func__);
        return;
    }

    LOGI("Reloading due to rule update\n");
    reload_scan(session);
}

    void
iotm_ble_handle(
        struct iotm_session *session,
        struct plugin_command_t *iot_cmd)
{
    if (session == NULL) return;
    if (iot_cmd == NULL) return;

    const struct ble_cmd_t *cmd = ble_cmd_from_string(iot_cmd->action);
    if (cmd == NULL) return;

    cmd->handle_cmd(session, iot_cmd);
    LOGI("%s: routed command: %s\n", __func__, iot_cmd->action);
}

void event_cb(void *context, ble_event_t *event)
{
    struct iotm_ble_handler_session *ble_session = NULL;
    struct iotm_session *session = NULL;
    struct plugin_event_t *iot_event = NULL;
    int err = -1;

    LOGD("%s: BLE Target Layer emitted an event.\n", __func__);

    session = (struct iotm_session *) context;
    ble_session = iotm_ble_handler_lookup_session(session);

    if (ble_session == NULL)
    {
        LOGE("%s: could not allocate iotm_ble_handler parser", __func__);
        return;
    }

    LOGD("%s: About to make call to session op.\n", __func__);
    iot_event = session->ops.plugin_event_new();

    // load in event string name
    LOGD("%s: Finding an event matching the type [%d]\n", __func__, event->type);
    const struct ble_type *type = ble_event_from_type(event->type);
    LOGD("%s: OVSDB type found: [%s]\n", __func__, type->ovsdb_type);
    strcpy(iot_event->name, type->ovsdb_type);

    // Add all parameters for the specific event type
    err = type->add_params(session, iot_event, event);
    if (err)
    {
        LOGE("%s: Failed to add filters to the IoT event.\n", __func__);
    }

    // Add the mac to the event, this should be for every event
    LOGD("%s: adding the mac address [%s] to the event.\n", __func__, event->mac);
    iot_event->ops.add_param_str(iot_event, MAC_KEY, event->mac);

    LOGD("%s: sending event to IoT manager for routing.\n", __func__);
    session->ops.emit(session, iot_event);
    return;
}

int iotm_ble_handler_init(struct iotm_session *session)
{
    if (session == NULL)
    {
        LOGE("%s: No session passed, exiting initialization.\n",
                __func__);
        return -1;
    }

    struct iotm_ble_handler_cache *mgr;
    struct iotm_ble_handler_session *ble_session;
    bool ret = false;
    void **tl_context_ptr = NULL;

    mgr = iotm_ble_handler_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(
                &mgr->iotm_sessions,
                iotm_ble_handler_session_cmp,
                struct iotm_ble_handler_session,
                session_node);

        mgr->initialized = true;
    }

    /* Look up the iotm ble_handler session */
    ble_session= iotm_ble_handler_lookup_session(session);
    if (ble_session == NULL)
    {
        LOGE("%s: could not allocate iotm_ble_handler parser", __func__);
        return -1;
    }

    // Load plugin defined function pointers
    session->ops.periodic = iotm_ble_handler_periodic;
    session->ops.update = iotm_ble_handler_update;
    session->ops.rule_update = iotm_ble_handler_rule_update;
    session->ops.tag_update = iotm_ble_handler_tag_update;
    session->ops.exit = iotm_ble_handler_exit;
    session->ops.handle = iotm_ble_handle;

    tl_context_ptr = get_ctx_ptr(session);

    // Initialize the target layer
    if (tl_context_ptr != NULL && *tl_context_ptr == NULL)
    {
        ret = ble_init(tl_context_ptr, session, session->loop, &event_cb);

        if (!ret)
        {
            LOGE("%s: failed to load the BLE Default Plugin.\n", __func__);
            return -1;
        };
        LOGI("%s: Successfully initialized the BLE Plugin.\n", __func__);
    }
    else LOGI("%s: BLE Target layer already initialized, not re initializing.\n", __func__);

    // Check if any rules for advertising are installed, if so start scan
    LOGI("%s: Loading any current data pertaining to advertising scanning...\n",
            __func__);

    reload_scan(session);
    LOGI("%s: Reloaded the scan, init finished, returning.\n", __func__);
    return 0;
}


/**
 * @ Begin handler functions
 */
void handle_cmd_default(struct iotm_session *sess, struct plugin_command_t *cmd)
{
    LOGE("[] -- %s: command not implemented, calling default: %s\n", __func__, cmd->action);
}

int get_connect_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_connect_params_t *params)
{
    if (sess == NULL) return -1;
    if (cmd == NULL) return -1;
    if (params == NULL) return -1;

    char *is_public_addr = cmd->ops.get_param(cmd, PUBLIC_ADDR);
    // default to true unless specified
    bool pub_addr = true;
    if (is_public_addr != NULL)
    {
        pub_addr = str_to_bool(is_public_addr);
    }
    params->is_public_addr = pub_addr;
    return 0;
}

void handle_connect(
        struct iotm_session *sess,
        struct plugin_command_t *cmd)
{
    void *tl_context = NULL;
    int err;
    char *mac = cmd->ops.get_param(cmd, P_MAC);

    ble_connect_params_t params;
    memset(&params, 0, sizeof(params));

    err = get_connect_params(sess, cmd, &params);
    if (err) return;

    tl_context = get_ctx(sess);
    bool ret = ble_connect_device(tl_context, mac, &params);
    if (!ret)
    {
        LOGE("%s: Error connecting to device : [%s]", __func__, mac);
    }
    else
    {
        LOGD("%s: Sent a connect request to the device: [%s]\n", __func__, mac);
    }
}


int get_char_notification_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_characteristic_notification_params *params)
{
    if (sess == NULL) return -1;
    if (cmd == NULL) return -1;
    if (params == NULL) return -1;
    char *char_uuid = cmd->ops.get_param(cmd, CHAR_UUID);
    if (char_uuid != NULL)
    {
        strcpy(params->char_uuid, char_uuid);
        return 0;
    }
    return -1;
}
void handle_disable_char_notifications(
        struct iotm_session *sess,
        struct plugin_command_t *cmd)
{

    void *tl_context = NULL;
    int err;
    char *mac = cmd->ops.get_param(cmd, P_MAC);

    tl_context = get_ctx(sess);
    ble_characteristic_notification_params params;
    memset(&params, 0, sizeof(params));

    err = get_char_notification_params(sess, cmd, &params);
    if (err) return;

    bool ret = ble_disable_characteristic_notifications(tl_context, mac, &params);
    if (!ret)
    {
        LOGE("%s: Error connecting to device : [%s]", __func__, mac);
    }
    else
    {
        LOGD("%s: Disabled characteristic notifications for device [%s]\n", __func__, mac);
    }
}

void handle_enable_char_notifications(
        struct iotm_session *sess,
        struct plugin_command_t *cmd)
{

    int err;
    void *tl_context = NULL;
    char *mac = cmd->ops.get_param(cmd, P_MAC);

    ble_characteristic_notification_params params;
    memset(&params, 0, sizeof(params));

    err = get_char_notification_params(sess, cmd, &params);
    if (err) return;

    tl_context = get_ctx(sess);
    bool ret = ble_enable_characteristic_notifications(tl_context, mac, &params);
    if (!ret) LOGE("%s: Error connecting to device : [%s]", __func__, mac);
    else LOGD("%s: Enabled characteristic notifications for device [%s]\n", __func__, mac);
}

void handle_disconnect_device(
        struct iotm_session *sess,
        struct plugin_command_t *cmd)
{
    void *tl_context = NULL;
    char *mac = cmd->ops.get_param(cmd, P_MAC);

    tl_context = get_ctx(sess);
    bool ret = ble_disconnect_device(tl_context, mac);
    if (!ret) LOGE("%s: Error disconnecting from device : [%s]", __func__, mac);
    else LOGD("%s: Disconnection sent to device [%s]\n", __func__, mac);
}

int get_char_discovery_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_characteristic_discovery_params_t *params)
{
    if (sess == NULL) return -1;
    if (cmd == NULL) return -1;
    if (params == NULL) return -1;
    char *serv_uuid = cmd->ops.get_param(cmd, SERV_UUID);
    if (serv_uuid != NULL)
    {
        strcpy(params->serv_uuid, serv_uuid);
        return 0;
    }
    return -1;
}

void handle_discover_chars(
        struct iotm_session *sess,
        struct plugin_command_t *cmd)
{

    void *tl_context = NULL;
    int err;
    char *mac = cmd->ops.get_param(cmd, P_MAC);

    ble_characteristic_discovery_params_t params;
    memset(&params, 0, sizeof(params));

    err = get_char_discovery_params(sess, cmd, &params);
    if (err) return;

    tl_context = get_ctx(sess);
    bool ret = ble_discover_characteristics(tl_context, mac, &params);
    if (!ret) LOGE("%s: Error connecting to device : [%s]", __func__, mac);
    else LOGD("%s: Characteristic discovery sent to device [%s]\n", __func__, mac);
}

void service_cb(
        ds_list_t *dl,
        struct iotm_value_t *val,
        void *ctx)
{
    ble_service_discovery_params_t *params = (ble_service_discovery_params_t *) ctx;

    if (params == NULL) return;

    int i = params->num_filters;
    strcpy(params->filter[i].uuid, val->value);
    params->filter[i].is_primary = true; // TODO : pass actual primary through context
    params->num_filters += 1;
}

int get_serv_discovery_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_service_discovery_params_t *params)
{
    if (sess == NULL) return -1;
    if (cmd == NULL) return -1;
    if (params == NULL) return -1;

    iotm_list_t *services;
    services = cmd->ops.get_params(cmd, SERV_UUID);
    if (services != NULL)
    {
        params->num_filters = 0; // set to zero, foreach will iterate
        params->filter = calloc(1,
                (services->len * sizeof(struct ble_service_t)));

        services->foreach(services, service_cb, params);
    }
    return 0;
}

void handle_discover_servs(
        struct iotm_session *sess,
        struct plugin_command_t *cmd)
{
    void *tl_context = NULL;
    int err;
    char *mac = cmd->ops.get_param(cmd, P_MAC);

    ble_service_discovery_params_t params;
    memset(&params, 0, sizeof(params));

    err = get_serv_discovery_params(sess, cmd, &params);
    if (err) return;

    tl_context = get_ctx(sess);
    bool ret = ble_discover_services(tl_context, mac, &params);
    if (!ret) LOGE("%s: Error connecting to device : [%s]", __func__, mac);
    else LOGD("%s: Discovering services for device [%s]\n", __func__, mac);
    // cleanup
    if (params.filter) free(params.filter);
}

int get_char_read_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_read_characteristic_params_t *params)
{
    if (sess == NULL) return -1;
    if (cmd == NULL) return -1;
    if (params == NULL) return -1;
    char *char_uuid = cmd->ops.get_param(cmd, CHAR_UUID);
    if (char_uuid != NULL)
    {
        strcpy(params->char_uuid, char_uuid);
        return 0;
    }
    return -1;
}

void handle_char_read(
        struct iotm_session *sess,
        struct plugin_command_t *cmd)
{

    void *tl_context = NULL;
    int err;
    char *mac = cmd->ops.get_param(cmd, P_MAC);

    ble_read_characteristic_params_t params;
    memset(&params, 0, sizeof(params));

    err = get_char_read_params(sess, cmd, &params);
    if (err) return;

    tl_context = get_ctx(sess);
    bool ret = ble_read_characteristic(tl_context, mac, &params);
    if (!ret) LOGE("%s: Error connecting to device : [%s]", __func__, mac);
    else LOGD("%s: Reading characteristic for device [%s]\n", __func__, mac);
}


int get_desc_read_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_read_descriptor_params_t *params)
{

    if (sess == NULL) return -1;
    if (cmd == NULL) return -1;
    if (params == NULL) return -1;
    char *char_uuid = cmd->ops.get_param(cmd, CHAR_UUID);
    char *desc_uuid = cmd->ops.get_param(cmd, DESC_UUID);
    if (char_uuid != NULL && desc_uuid != NULL)
    {
        strcpy(params->char_uuid, char_uuid);
        strcpy(params->desc_uuid, desc_uuid);
        return 0;
    }
    return -1;
}

void handle_desc_read(
        struct iotm_session *sess,
        struct plugin_command_t *cmd)
{

    void *tl_context = NULL;
    int err;
    char *mac = cmd->ops.get_param(cmd, P_MAC);

    ble_read_descriptor_params_t params;
    memset(&params, 0, sizeof(params));

    err = get_desc_read_params(sess, cmd, &params);
    if (err) return;

    tl_context = get_ctx(sess);
    bool ret = ble_read_descriptor(tl_context, mac, &params);
    if (!ret) LOGE("%s: Error connecting to device : [%s]", __func__, mac);
    else LOGD("%s: Reading descriptor for device [%s]\n", __func__, mac);
}

int decode_data_helper(
        struct plugin_command_t *cmd,
        barray_t *barray)
{
    char *data = cmd->ops.get_param(cmd, DATA);
    char *decode_str = cmd->ops.get_param(cmd, DECODE_TYPE);
    if (data == NULL) return -1;

    struct decode_type *decoder = decode_from_str(decode_str);
    if (decoder == NULL) decoder = decode_from_type(HEX);

    barray->data = (uint8_t *)calloc(1, strlen(data));
    int length = decoder->decoder(data, barray->data);
    barray->data_length = length;
    return 0;
}

int get_char_write_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_write_characteristic_params_t *params)
{

    if (sess == NULL) return -1;
    if (cmd == NULL) return -1;
    if (params == NULL) return -1;
    if (params->barray == NULL) return -1;

    int err;
    char *char_uuid = cmd->ops.get_param(cmd, CHAR_UUID);

    if (char_uuid == NULL) return -1;

    err = decode_data_helper(cmd, params->barray);

    if (!err)
    {
        strcpy(params->char_uuid, char_uuid);
        return 0;
    }

    if (params->barray->data) free(params->barray->data);
    return -1;
}

void handle_char_write(
        struct iotm_session *sess,
        struct plugin_command_t *cmd)
{
    void *tl_context = NULL;
    bool ret = false;
    int err = -1;
    ble_write_characteristic_params_t params;
    memset(&params, 0, sizeof(params));

    barray_t barray;
    memset(&barray, 0, sizeof(barray));
    params.barray = &barray;

    char *mac = cmd->ops.get_param(cmd, P_MAC);

    err = get_char_write_params(sess, cmd, &params);

    if (!err)
    {
        tl_context = get_ctx(sess);
        ret = ble_write_characteristic(
                tl_context,
                mac,
                &params);
        if (ret) LOGI("%s: Sent write_characteristic to device [%s]", __func__, mac);
    }

    if(params.barray->data) free(params.barray->data);
    if  (err || !ret) LOGE("%s: Failed to send write for device [%s]", __func__, mac);
    else LOGD("%s: Write a characteristic for device [%s]\n", __func__, mac);
}

int get_desc_write_params(
        struct iotm_session *sess,
        struct plugin_command_t *cmd,
        ble_write_descriptor_params_t *params)
{

    if (sess == NULL) return -1;
    if (cmd == NULL) return -1;
    if (params == NULL) return -1;
    if (params->barray == NULL) return -1;

    int err;
    char *char_uuid = cmd->ops.get_param(cmd, CHAR_UUID);
    char *desc_uuid = cmd->ops.get_param(cmd, DESC_UUID);

    if (char_uuid == NULL || desc_uuid == NULL) return -1;

    err = decode_data_helper(cmd, params->barray);

    if (!err)
    {
        strcpy(params->char_uuid, char_uuid);
        strcpy(params->desc_uuid, desc_uuid);
        return 0;
    }

    if (params->barray->data) free(params->barray->data);
    return -1;
}

void handle_desc_write(
        struct iotm_session *sess,
        struct plugin_command_t *cmd)
{
    void *tl_context = NULL;
    bool ret = false;
    int err = -1;
    ble_write_descriptor_params_t params;
    memset(&params, 0, sizeof(params));

    barray_t barray;
    memset(&barray, 0, sizeof(barray));
    params.barray = &barray;

    char *mac = cmd->ops.get_param(cmd, P_MAC);

    err = get_desc_write_params(sess, cmd, &params);

    if (!err)
    {
        tl_context = get_ctx(sess);
        ret = ble_write_descriptor(
                tl_context,
                mac,
                &params);
    }

    if(params.barray->data) free(params.barray->data);
    if  (err || !ret) LOGE("%s: Failed to send write for device [%s]", __func__, mac);
    else LOGD("%s: Wrote descriptor to device [%s]\n", __func__, mac);
}

// Filter Adding Helpers

int default_add(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    LOGI("Not implemented!\n");
    return -1;
}

int advertised_add(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    if (session == NULL) return -1;
    if (iot_ev == NULL) return -1;
    if (event == NULL) return -1;
    if (event->op.advertise.contents == NULL) return -1;
    if (iot_ev->ops.add_param_str == NULL) return -1;

    ble_advertisement_t *cont = event->op.advertise.contents;

    if (cont->name) iot_ev->ops.add_param_str(iot_ev, NAME_KEY, cont->name);

    if (cont->num_services == 0) return 0;
    for (size_t i = 0; i < cont->num_services; i++)
    {
        iot_ev->ops.add_param_str(iot_ev, SERV_UUID, cont->service_uuids[i]);
    }
    return 0;
}

int add_connected_filters(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    if (session == NULL) return -1;
    if (iot_ev == NULL) return -1;
    if (event == NULL) return -1;
    int err = 0;

    ble_connect_params_t *params = &event->op.connection.params;
    struct ble_connect_t *conn = &event->op.connection.connection;

    if (conn == NULL) return -1;

    iot_ev->ops.add_param_str(
            iot_ev,
            PUBLIC_ADDR,
            bool_to_str(params->is_public_addr));

    char *status_str = NULL;
    switch(conn->status)
    {
        case Ble_Success:
            status_str = "success";
            break;
        case Ble_NotReady:
            status_str = "not_ready";
            break;
        case Ble_Failed:
            status_str = "failed";
            break;
        case Ble_InProgress:
            status_str = "in_progress";
            break;
        case Ble_AlreadyConnected:
            status_str = "already_connected";
            break;
        case Ble_ServiceResolveFailure:
            status_str = "service_resolve_failure";
            break;
        default:
            status_str = "unknown";
            err = -1;
    }

    iot_ev->ops.add_param_str(iot_ev, CONNECT_KEY, status_str);
    return err;
}

int add_disconnected_params(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    int err = add_connected_filters(session, iot_ev, event);
    return err;
}



int add_service_discovery(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    if (session == NULL) return -1;
    if (iot_ev == NULL) return -1;
    if (event == NULL) return -1;

    ble_service_t *service = &event->op.s_discovered.service;

    iot_ev->ops.add_param_str(iot_ev, SERV_UUID, service->uuid);
    iot_ev->ops.add_param_str(iot_ev, IS_PRIMARY, bool_to_str(service->is_primary));
    return 0;
}

char *c_flag_to_str(ble_C_Flags flag)
{
    char *c_str;
    switch(flag)
    {
        case BLE_Char_Broadcast:
            c_str = "ble_char_broadcast";
            break;
        case BLE_Char_Read:
            c_str = "ble_char_read";
            break;
        case BLE_Char_Write_without_response:
            c_str = "ble_char_write_without_response";
            break;
        case BLE_Char_Write:
            c_str = "ble_char_write";
            break;
        case BLE_Char_Notify:
            c_str = "ble_char_notify";
            break;
        case BLE_Char_Indicate:
            c_str = "ble_char_indicate";
            break;
        case BLE_Char_Authenticated_signed_writes:
            c_str = "ble_char_authenticated_signed_writes";
            break;
        case BLE_Char_Extended_properties:
            c_str = "ble_char_extended_properties";
            break;
        case BLE_Char_Reliable_write:
            c_str = "ble_char_reliable_write";
            break;
        case BLE_Char_Writable_auxiliaries:
            c_str = "ble_char_writable_auxiliaries";
            break;
        case BLE_Char_Encrypt_read:
            c_str = "ble_char_encrypt_read";
            break;
        case BLE_Char_Encrypt_write:
            c_str = "ble_char_encrypt_write";
            break;
        case BLE_Char_Encrypt_authenticated_read:
            c_str = "ble_char_encrypt_authenticated_read";
            break;
        case BLE_Char_Encrypt_authenticated_write:
            c_str = "ble_char_encrypt_authenticated_write";
            break;
        case BLE_Char_Secure_read:
            c_str = "ble_char_secure_read";
            break;
        case BLE_Char_Secure_write:
            c_str = "ble_char_secure_write";
            break;
        case BLE_Char_Authorize:
            c_str = "ble_char_authorize";
            break;
        default:
            c_str = "unknown";
    }
    return c_str;
}

int add_characteristic_discovery(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    if (session == NULL) return -1;
    if (iot_ev == NULL) return -1;
    if (event == NULL) return -1;

    ble_characteristic_discovery_params_t *params = &event->op.c_discovered.params;
    ble_characteristic_discovery_t *char_disc = &event->op.c_discovered.characteristic;
    iot_ev->ops.add_param_str(iot_ev, SERV_UUID, params->serv_uuid);
    iot_ev->ops.add_param_str(iot_ev, CHAR_UUID, char_disc->uuid);
    iot_ev->ops.add_param_str(iot_ev, C_FLAG, c_flag_to_str(char_disc->flags));
    return 0;
}

int add_descriptor_discovery(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    if (session == NULL) return -1;
    if (iot_ev == NULL) return -1;
    if (event == NULL) return -1;
    ble_descriptor_discovery_params_t *params = &event->op.d_discovered.params;
    ble_descriptor_discovery_t *desc_disc = &event->op.d_discovered.descriptor;
    iot_ev->ops.add_param_str(iot_ev, CHAR_UUID, params->char_uuid);
    iot_ev->ops.add_param_str(iot_ev, DESC_UUID, desc_disc->uuid);
    return 0;
}

int add_characteristic_updated(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    if (session == NULL) return -1;
    if (iot_ev == NULL) return -1;
    if (event == NULL) return -1;
    int err;

    ble_characteristic_updated_t *char_updated = &event->op.c_updated;

    iot_ev->ops.add_param_str(iot_ev, CHAR_UUID, char_updated->characteristic.uuid);
    iot_ev->ops.add_param_str(iot_ev, IS_NOTIFICATION, bool_to_str(char_updated->is_notification));

    barray_t *data = char_updated->characteristic.data;
    char check[1024];
    err = bin2hex(data->data, data->data_length, check, (size_t)1024);
    if (!err)
    {
        iot_ev->ops.add_param_str(
                iot_ev,
                DATA,
                check);
    }
    return 0;
}


int add_characteristic_write_success(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    if (session == NULL) return -1;
    if (iot_ev == NULL) return -1;
    if (event == NULL) return -1;
    int err = -1;

    ble_char_write_success_t *char_write = &event->op.c_written;
    // convert to string
    int code = char_write->write.s_code;
    int length = snprintf(NULL, 0, "%d", code);
    char* s_code = malloc(length + 1);
    snprintf(s_code, length + 1, "%d", code);

    iot_ev->ops.add_param_str(iot_ev, S_CODE, s_code);
    free(s_code);
    iot_ev->ops.add_param_str(iot_ev, CHAR_UUID, char_write->params->char_uuid);

    barray_t *data = char_write->params->barray;
    char check[1024];
    err = bin2hex(data->data, data->data_length, check, (size_t)1024);
    if (!err)
    {
        iot_ev->ops.add_param_str(
                iot_ev,
                DATA,
                check);
    }
    return err;
}

int add_descriptor_write_success(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    if (session == NULL) return -1;
    if (iot_ev == NULL) return -1;
    if (event == NULL) return -1;
    int err = -1;

    ble_desc_write_success_t *desc_write = &event->op.d_written;
    // convert to string
    int code = desc_write->write.s_code;
    int length = snprintf(NULL, 0, "%d", code);
    char* s_code = malloc(length + 1);
    snprintf(s_code, length + 1, "%d", code);

    iot_ev->ops.add_param_str(iot_ev, S_CODE, s_code);
    free(s_code);

    iot_ev->ops.add_param_str(iot_ev, CHAR_UUID, desc_write->params->char_uuid);
    iot_ev->ops.add_param_str(iot_ev, DESC_UUID, desc_write->params->desc_uuid);

    barray_t *data = desc_write->params->barray;
    char check[1024];
    err = bin2hex(data->data, data->data_length, check, (size_t)1024);
    if (!err)
    {
        iot_ev->ops.add_param_str(
                iot_ev,
                DATA,
                check);
    }
    return err;
}

int add_char_notify_success(
        struct iotm_session *session,
        struct plugin_event_t *iot_ev,
        ble_event_t *event)
{
    if (session == NULL) return -1;
    if (iot_ev == NULL) return -1;
    if (event == NULL) return -1;

    ble_char_notification_success_t *notify_success = &event->op.c_notify;
    // convert to string
    int code = notify_success->notification.s_code;
    int length = snprintf(NULL, 0, "%d", code);
    char* s_code = malloc(length + 1);
    snprintf(s_code, length + 1, "%d", code);

    iot_ev->ops.add_param_str(iot_ev, S_CODE, s_code);
    free(s_code);

    iot_ev->ops.add_param_str(iot_ev, CHAR_UUID, notify_success->params->char_uuid);
    return 0;
}
