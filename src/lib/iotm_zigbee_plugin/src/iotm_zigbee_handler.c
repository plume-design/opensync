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

#include "iotm_zigbee_handler.h"
#include "iotm_zigbee_handler_private.h"

static struct iotm_zigbee_handler_cache
cache_mgr =
{
    .initialized = false,
};

struct iotm_zigbee_handler_cache *
iotm_zigbee_get_mgr(void)
{
    return &cache_mgr;
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
int not_impl_to_bytes(char *pos, unsigned char *val)
{
    LOGE("%s: type not implemented yet.", __func__);
    return -1;
}

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
    if (strstr(line, "0x")) line += 2;  // 0x01 -> 01

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

enum
{
    UNKNOWN,
    HEX,
    UTF8
};

struct decode_type
{
    char *ovsdb_type;
    int type;
    int (*decoder)(char *in, unsigned char *out); /**< decodes string to byte based off encoding */
};
/**
 * @brief mappings from strings to OVSDB type
 *
 * @note keeps magic strings out of plugin, converted with zigbee_event_type()
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
///@}


/**
 * @brief mappings from strings to OVSDB type
 *
 * @note keeps magic strings out of plugin, converted with zigbee_event_type()
 */
static const struct zigbee_ev_t events_map[] =
{
    {
        .ovsdb_type = "zigbee_unknown",
        .event_type = ZIGBEE_UNKNOWN,
        .add_params = add_zigbee_unknown,
    },
    {
        .ovsdb_type = "zigbee_error",
        .event_type = ZIGBEE_ERROR,
        .add_params = add_zigbee_error,
    },
    {
        .ovsdb_type = "zigbee_device_annced",
        .event_type = ZIGBEE_DEVICE_ANNCED,
        .add_params = add_zigbee_device_annced,
    },
    {
        .ovsdb_type = "zigbee_ep_discovered",
        .event_type = ZIGBEE_EP_DISCOVERED,
        .add_params = add_zigbee_ep_discovered,
    },
    {
        .ovsdb_type = "zigbee_attr_discovered",
        .event_type = ZIGBEE_ATTR_DISCOVERED,
        .add_params = add_zigbee_attr_discovered,
    },
    {
        .ovsdb_type = "zigbee_comm_recv_discovered",
        .event_type = ZIGBEE_COMM_RECV_DISCOVERED,
        .add_params = add_zigbee_comm_recv_discovered,
    },
    {
        .ovsdb_type = "zigbee_comm_gen_discovered",
        .event_type = ZIGBEE_COMM_GEN_DISCOVERED,
        .add_params = add_zigbee_comm_gen_discovered,
    },
    {
        .ovsdb_type = "zigbee_attr_value_received",
        .event_type = ZIGBEE_ATTR_VALUE_RECEIVED,
        .add_params = add_zigbee_attr_value_received,
    },
    {
        .ovsdb_type = "zigbee_attr_write_success",
        .event_type = ZIGBEE_ATTR_WRITE_SUCCESS,
        .add_params = add_zigbee_attr_write_success,
    },
    {
        .ovsdb_type = "zigbee_report_configed_success",
        .event_type = ZIGBEE_REPORT_CONFIGED_SUCCESS,
        .add_params = add_zigbee_report_configed_success,
    },
    {
        .ovsdb_type = "zigbee_report_config_received",
        .event_type = ZIGBEE_REPORT_CONFIG_RECEIVED,
        .add_params = add_zigbee_report_config_received,
    },
    {
        .ovsdb_type = "zigbee_default_response",
        .event_type = ZIGBEE_DEFAULT_RESPONSE,
        .add_params = add_zigbee_default_response,
    },
};

const struct zigbee_ev_t *zigbee_event_from_type(zigbee_event_type type)
{
    const struct zigbee_ev_t *map;
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

static const struct zigbee_cmd_t commands_map[] =
{
    {
        .ovsdb_type = "zigbee_configure_reporting",
        .command_type = ZIGBEE_CONFIGURE_REPORTING,
        .handle_cmd = handle_configure_reporting,
    },
    {
        .ovsdb_type = "zigbee_discover_attributes",
        .command_type = ZIGBEE_DISCOVER_ATTRIBUTES,
        .handle_cmd = handle_discover_attributes,
    },
    {
        .ovsdb_type = "zigbee_discover_commands_generated",
        .command_type = ZIGBEE_DISCOVER_COMMANDS_GENERATED,
        .handle_cmd = handle_discover_commands_generated,
    },
    {
        .ovsdb_type = "zigbee_discover_commands_received",
        .command_type = ZIGBEE_DISCOVER_COMMANDS_RECEIVED,
        .handle_cmd = handle_discover_commands_received,
    },
    {
        .ovsdb_type = "zigbee_discover_endpoints",
        .command_type = ZIGBEE_DISCOVER_ENDPOINTS,
        .handle_cmd = handle_discover_endpoints,
    },
    {
        .ovsdb_type = "zigbee_read_attributes",
        .command_type = ZIGBEE_READ_ATTRIBUTES,
        .handle_cmd = handle_read_attributes,
    },
    {
        .ovsdb_type = "zigbee_read_reporting_configuration",
        .command_type = ZIGBEE_READ_REPORTING_CONFIGURATION,
        .handle_cmd = handle_read_reporting_configuration,
    },
    {
        .ovsdb_type = "zigbee_send_cluster_specific_command",
        .command_type = ZIGBEE_SEND_CLUSTER_SPECIFIC_COMMAND,
        .handle_cmd = handle_send_cluster_specific_command,
    },
    {
        .ovsdb_type = "zigbee_send_network_leave",
        .command_type = ZIGBEE_SEND_NETWORK_LEAVE,
        .handle_cmd = handle_send_network_leave,
    },
    {
        .ovsdb_type = "zigbee_write_attributes",
        .command_type = ZIGBEE_WRITE_ATTRIBUTES,
        .handle_cmd = handle_write_attributes,
    },
};

const struct zigbee_cmd_t *zigbee_cmd_from_string(char *c_type)
{
    const struct zigbee_cmd_t *map;
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
/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int iotm_zigbee_session_cmp(void *a, void *b)
{
    uintptr_t p_a = (uintptr_t)a;
    uintptr_t p_b = (uintptr_t)b;

    if (p_a ==  p_b) return 0;
    if (p_a < p_b) return -1;
    return 1;
}

/**
 * @brief compare sessions
 *
 * @param a session pointer
 * @param b session pointer
 * @return 0 if sessions matches
 */
static int timer_cmp(void *a, void *b)
{
    if ( *(long *)a == *(long *)b) return 0;
    return -1;
}

/**
 * @brief Frees a iotm zigbee session
 *
 * @param i_session the iotm zigbee session to delete
 */
void iotm_zigbee_free_session(struct iotm_zigbee_session *i_session)
{
    // cleanup rules if necessary
    free(i_session->timers);
    free(i_session);
}

/**
 * @brief deletes a session
 *
 * @param session the iotm session keying the iot session to delete
 */
void iotm_zigbee_delete_session(struct iotm_session *session)
{
    struct iotm_zigbee_handler_cache *mgr;
    struct iotm_zigbee_session *i_session;
    ds_tree_t *sessions;

    mgr = iotm_zigbee_get_mgr();
    sessions = &mgr->iotm_sessions;

    i_session = ds_tree_find(sessions, session);
    if (i_session == NULL) return;

    LOGD("%s: removing session %s", __func__, session->name);
    ds_tree_remove(sessions, i_session);
    iotm_zigbee_free_session(i_session);

    return;
}

/**
 * @brief looks up a session
 *
 * Looks up a session, and allocates it if not found.
 * @param session the session to lookup
 * @return the found/allocated session, or NULL if the allocation failed
 */
struct iotm_zigbee_session *iotm_zigbee_lookup_session(
        struct iotm_session *session)
{
    struct iotm_zigbee_handler_cache *mgr;
    struct iotm_zigbee_session *i_session;
    ds_tree_t *sessions;

    mgr = iotm_zigbee_get_mgr();
    sessions = &mgr->iotm_sessions;

    i_session = ds_tree_find(sessions, session);
    if (i_session != NULL) return i_session;

    LOGD("%s: Adding new session %s", __func__, session->name);
    i_session = calloc(1, sizeof(struct iotm_zigbee_session));
    if (i_session == NULL) return NULL;
    i_session->session = session;

    i_session->timers = calloc(1, sizeof(ds_tree_t));
    ds_tree_init(i_session->timers, timer_cmp, struct pairing_node_t, timer_node);

    ds_tree_insert(sessions, i_session, session);

    return i_session;
}

void iotm_zigbee_handler_periodic(struct iotm_session *session)
{
    // NO-OP
}

void iotm_zigbee_handler_exit(struct iotm_session *session)
{
    LOGI("%s: Cleaning and exit from the zigbee plugin session.",
            __func__);
    struct iotm_zigbee_handler_cache *mgr;
    mgr = iotm_zigbee_get_mgr();
    if (!mgr->initialized) return;

    // cleanup logic if necessary here
    iotm_zigbee_delete_session(session);
}

void iotm_zigbee_handler_update(struct iotm_session *session)
{
    // NO-OP
}

/**
 * @brief handler for when events are passed from IOTM
 */
void iotm_zigbee_handler(
        struct iotm_session *session,
        struct plugin_command_t *command)
{
    LOGI("in %s",  __func__);
    if ( command == NULL ) return;
    if (command->action == NULL) return;

    LOGI("%s: Information from: [%s]", 
            __func__, command->action);

    const struct zigbee_cmd_t *cmd = zigbee_cmd_from_string(command->action);
    if (cmd == NULL) return;

    cmd->handle_cmd(session, command);
    LOGI("%s: Routed command [%s] to Zigbee Target Layer.", __func__, command->action);
}

void no_op_tag_update(struct iotm_session *session)
{
    // NO-OP
}

long get_key(struct timer_data_t *data)
{
    return (data->start_time % data->duration);
}

int init_pairing_node(
        struct iotm_zigbee_session *zb_session,
        time_t start_time,
        int duration,
        struct pairing_node_t **pair_node)
{
    *pair_node = calloc(1, sizeof(struct pairing_node_t));
    struct pairing_node_t *node = *pair_node;
    struct timer_data_t *data = NULL;
    data = calloc(1, sizeof(struct timer_data_t));
    data->zb_session = zb_session;
    data->start_time = start_time;
    data->duration = duration;
    data->parent = node;
    node->watcher.data = data;

    node->key = get_key(data);
    return 0;
}

void free_pairing_node(struct pairing_node_t *timer)
{
    if (timer == NULL) return;
    free(timer->watcher.data);
    free(timer);
}

int add_timer_node(struct iotm_zigbee_session *session, struct pairing_node_t *node)
{
    struct pairing_node_t *lookup = ds_tree_find(session->timers, &node->key);
    if (lookup != NULL)
    {
        LOGI("%s: Item already in tree, not adding.", __func__);
        return -1;
    }

    ds_tree_insert(session->timers, node, &node->key);
    return 0;
}

void remove_timer_node(struct iotm_zigbee_session *session, struct pairing_node_t *node)
{
    ds_tree_remove(session->timers, &node->key);
    free_pairing_node(node);
}

int permit_joining(struct timer_data_t *pair)
{
    bool ret = false;
    void *tl_context = NULL;
    if (time(NULL) > pair->start_time + pair->duration)
    {
        LOGD("%s: Rule is set for time in past, ignoring.", __func__);
        return -1;
    }

    if (time(NULL) > pair->start_time + 5 || time(NULL) < pair->start_time - 5)
    {
        LOGD("%s: Start time not within (+/-) 5 seconds of current time. Not starting scan.",
                __func__);
        return -1;
    }

    if (pair->zb_session->join_until > 0)
    {
        if (pair->zb_session->join_until - (pair->start_time + pair->duration) >= 0)
        {
            LOGI("%s: Join enabled for longer due to another rule, not doing anything.", __func__);
            return -1;
        }
    }

    tl_context = get_ctx(pair->zb_session->session);
    zigbee_permit_join_params_t params =
    {
        .duration = pair->duration,
    };

    pair->zb_session->join_until = time(NULL) + pair->duration;
    ret = zigbee_permit_join(
            tl_context,
            params);

    if (ret)
    {
        LOGI("%s: Zigbee pairing enabled for [%d] seconds.",
                __func__, pair->duration);
        return 0;
    }

    LOGE("%s: Failed to enable pairing.", __func__);
    return -1;
}

static void pairing_timer_cb(EV_P_ ev_timer *w, int revents)
{
    int err = -1;
    struct timer_data_t *data = (struct timer_data_t *)w->data;
    // Do pairing things
    err = permit_joining(data);
    if (err)
    {
        LOGE("%s: Failed to start pairing.", __func__);
    }

    // clean up timer
    remove_timer_node(data->zb_session, data->parent);
}

int get_pairing_params(
        struct iotm_rule *rule,
        time_t *start,
        int *duration
        )
{
    int err = -1;
    err = rule->params->get_type(rule->params, ZB_PAIRING_START, LONG, start);

    if (err) 
    {
        LOGE("%s: Failed to get start time for pairing rule: [%s], not moving on.",
            __func__, rule->name);
    }

    err = rule->params->get_type(rule->params, ZB_PAIRING_DURATION, INT, duration);
    if (err)
    {
        LOGD("%s: No timeout defined, defaulting to 30s.", __func__);
        *duration = 30;
        err = 0;
    }
    return err;
}

bool is_in_past(time_t start)
{
    if (start < time(NULL) - 5) return true;
    return false;
}

int queue_timer(struct iotm_zigbee_session *zb_session, struct pairing_node_t *node)
{
    int err = -1;
    if (zb_session == NULL
            || node == NULL
            || zb_session->session == NULL)
    {
        LOGE("%s: invalid input parameters", __func__);
        return -1;
    }

    struct  timer_data_t *data = (struct timer_data_t *)node->watcher.data;
    if (is_in_past(data->start_time + data->duration))
    {
        LOGI("%s: Time for requested timer has passed. Not queueing.", __func__);
        return -1;
    }

    err = add_timer_node(zb_session, node);
    if (err)
    {
        LOGE("%s: Adding pairing rule returned an error, not continuing.",
                __func__);
        return err;
    }
    long delay = data->start_time - time(NULL);
    ev_timer_init (&node->watcher, pairing_timer_cb, delay, 0.);
    ev_timer_start (zb_session->session->loop, &node->watcher);
    LOGI("%s: Timer initialized, will start pairing in [%ld] seconds.",
            __func__, delay);
    return err;
}

void zigbee_rule_update(
        struct iotm_session *session,
        ovsdb_update_monitor_t *mon,
        struct iotm_rule *rule)
{
    int err = -1;
    time_t start_time = -1;
    int timeout = -1;
    struct iotm_zigbee_session *iotm_zigbee_session = NULL;

    if (strcmp(rule->event, ZB_PAIRING_ENABLE) != 0)
    {
        LOGD("%s: Rule not a pairing event, moving on.", __func__);
        return;
    }


    iotm_zigbee_session = iotm_zigbee_lookup_session(session);
    if (iotm_zigbee_session == NULL)
    {
        LOGE("%s: Error getting zigbee session. Can't continue.", __func__);
        return;
    }

    err = get_pairing_params(rule, &start_time, &timeout);
    if (err)
    {
        LOGE("%s: Couldn't get parameters for pairing for rule [%s].", __func__, rule->name);
        return;
    }

    if (is_in_past(start_time + timeout))
    {
        LOGD("%s: Rule [%s] for pairing in past, ignoring.",
                __func__, rule->name);
        return;
    }

    struct pairing_node_t *node = NULL;
    err = init_pairing_node(iotm_zigbee_session, start_time, timeout, &node);
    if (err)
    {
        LOGE("%s: Couldn't initialize a timer node, not able to run rule: [%s]", __func__, rule->name);
        return;
    }

    LOGI("%s: Setting up timer to start pairing rule [%s]",
            __func__, rule->name);
    queue_timer(iotm_zigbee_session, node);
}

void zigbee_event_cb(void *context, zigbee_event_t *event)
{
    struct iotm_session *session = NULL;
    struct plugin_event_t *iot_event = NULL;
    int err = -1;

    LOGD("%s: Zigbee Target Layer emitted an event.", __func__);
    session = (struct iotm_session *) context;

    LOGD("%s: About to make call to session op.", __func__);
    iot_event = session->ops.plugin_event_new();

    // load in event string name
    LOGD("%s: Finding an event matching the type [%d]", __func__, event->type);
    const struct zigbee_ev_t *type = zigbee_event_from_type(event->type);
    LOGD("%s: OVSDB type found: [%s]", __func__, type->ovsdb_type);
    strcpy(iot_event->name, type->ovsdb_type);

    // Add all parameters for the specific event type
    err = type->add_params(iot_event, event);
    if (err)
    {
        LOGE("%s: Failed to add filters to the IoT event.", __func__);
    }

    // Add the mac to the event, this should be for every event
    LOGD("%s: adding the mac address [%s] to the event.", __func__, event->mac);
    iot_event->ops.add_param_str(iot_event, MAC_KEY, event->mac);

    LOGD("%s: sending event to IoT manager for routing.", __func__);
    session->ops.emit(session, iot_event);
    return;
}

void pass_update_cb(struct iotm_rule *rule, void *ctx)
{
    struct iotm_session *session = (struct iotm_session *)ctx;
    ovsdb_update_monitor_t mon =
    {
        .mon_type = OVSDB_UPDATE_NEW,
    };
    zigbee_rule_update(session, &mon, rule);
}

void setup_initial_pair_rules(struct iotm_session *session)
{
    if (session == NULL) return;
    struct iotm_zigbee_session *zb_session = NULL;

    zb_session = iotm_zigbee_lookup_session(session);
    if (zb_session == NULL) return;

    // get any rules matching device pairing
    struct iotm_event *event = session->ops.get_event(session, ZB_PAIRING_ENABLE);
    if (event == NULL) return;

    event->foreach_rule(event, pass_update_cb, session);

}

/**
 * @brief session initialization entry point
 *
 * Initializes the plugin specific fields of the session,
 * like the event handler and the periodic routines called
 * by iotm.
 * @param session pointer provided by iotm
 *
 * @note init name loaded in IOT_Manager_Config for other_config_value
 * ['dso_init']
 *
 * @note if ['dso_init'] is not set the default will be the <name>_plugin_init
 */
int
iotm_zigbee_handler_init(struct iotm_session *session)
{
    LOGI("%s: Loading the Zigbee Handler Plugin version [%s]", __func__, ZB_VERSION);
    if (session == NULL)
    {
        LOGE("%s: No session passed, exiting initialization.",
                __func__);
        return -1;
    }

    struct iotm_zigbee_handler_cache *mgr;
    struct iotm_zigbee_session *iotm_zigbee_session;
    void **tl_context_ptr = NULL;
    bool ret = false;

    if (session == NULL) return -1;

    mgr = iotm_zigbee_get_mgr();

    /* Initialize the manager on first call */
    if (!mgr->initialized)
    {
        ds_tree_init(&mgr->iotm_sessions, iotm_zigbee_session_cmp,
                     struct iotm_zigbee_session, session_node);

        mgr->initialized = true;
    }

    /* Look up the iotm zigbee session */
    iotm_zigbee_session= iotm_zigbee_lookup_session(session);

    if (iotm_zigbee_session == NULL)
    {
        LOGE("%s: could not allocate iotm_zigbee parser", __func__);
        return -1;
    }

    // Load plugin defined function pointers
    session->ops.periodic = iotm_zigbee_handler_periodic;
    session->ops.update = iotm_zigbee_handler_update;
    session->ops.exit = iotm_zigbee_handler_exit;
    session->ops.handle = iotm_zigbee_handler;
    session->ops.rule_update = zigbee_rule_update;
    session->ops.tag_update = no_op_tag_update;

    tl_context_ptr = get_ctx_ptr(session);
    if (tl_context_ptr != NULL && *tl_context_ptr == NULL)
    {
        ret = zigbee_init(tl_context_ptr, session, session->loop, &zigbee_event_cb);

        if (!ret)
        {
            LOGE("%s: Failed to initialize Zigbee Target Layer.", __func__);
            return -1;
        }
        LOGI("%s: Initialized the Zigbee Target Layer.", __func__);
    }
    else LOGI("%s: Zigbee Target Layer already initialized.", __func__);

    setup_initial_pair_rules(session);

    return 0;
}



bool valid_add_params(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event)
{
    if (iot_ev == NULL) return false;
    if (event == NULL) return false;
    if (iot_ev->ops.add_param_type == NULL) return false;
    return true;
}

int add_default(
        struct plugin_event_t *iot_ev,
        zigbee_event_t *event)
{
    LOGI("%s: Not implemented!", __func__);
    return -1;
}

int add_zigbee_unknown(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{
    LOGE("%s: Received unknown event from target layer, unable to route.",
            __func__);
    return -1;
}

int add_zigbee_error(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{
    LOGE("%s: Recieved an event matching the error enum from the target layer.",
            __func__);
    zigbee_error_t *err = &event->op.error;
    iot_ev->ops.add_param_str(iot_ev, ZB_ERR, err->error);
    // TODO : pull param functions into helper methods, add to error
    return 0;
}

int add_zigbee_device_annced(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{
    if (!valid_add_params(iot_ev, event)) return -1;
    int err = -1;

    zigbee_device_annced_t *announce = &event->op.device_annced;

    err = iot_ev->ops.add_param_type(iot_ev, ZB_NODE_ADDRESS, UINT16, &announce->contents.node_addr);

    return err;
}

int add_zigbee_ep_discovered(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{
    if (!valid_add_params(iot_ev, event)) return -1;

    int err = -1;

    zigbee_endpoint_t *ep = &event->op.ep_discovered.contents;
    zigbee_discover_endpoints_params_t *params = &event->op.ep_discovered.params;

    if (ep->input_clusters != NULL)
    {
        for ( int i = 0; i < ep->input_cluster_count; i++ )
        {
            err = iot_ev->ops.add_param_type(iot_ev, ZB_INPUT_CLUSTER, UINT8, &ep->input_clusters[i]);
            if (err) LOGE("%s: Couldn't add uint8 param", __func__);
        }
    }

    if (ep->output_clusters != NULL)
    {
        for ( int i = 0; i < ep->output_cluster_count; i++ )
        {
            err = iot_ev->ops.add_param_type(iot_ev, ZB_INPUT_CLUSTER, UINT16, &ep->output_clusters[i]);
            if (err) LOGE("%s: Couldn't convert [%s]", __func__, ZB_INPUT_CLUSTER);
        }
    }

    iot_ev->ops.add_param_type(iot_ev, ZB_EP, UINT8, &ep->ep);
    iot_ev->ops.add_param_type(iot_ev, ZB_PROFILE_ID, UINT16, &ep->profile_id);
    iot_ev->ops.add_param_type(iot_ev, ZB_DEVICE_ID, UINT16, &ep->device_id);

    if (params->endpoint_filter != NULL)
    {
        for ( int i = 0; i < params->num_endpoint_filters; i++ )
        {
            iot_ev->ops.add_param_type(iot_ev, ZB_EP_FILT, UINT8, &params->endpoint_filter[i]);
        }
    }

    return 0;
}

int add_zigbee_cluster(
        struct plugin_event_t *plugin,
        zigbee_cluster_t *cluster)
{

    int err = -1;
    err = plugin->ops.add_param_type(plugin, ZB_EP, UINT8, &cluster->ep_id);
    err = plugin->ops.add_param_type(plugin, ZB_CLUSTER_ID, UINT16, &cluster->ep_id) || err;
    return err;
}

int add_data_array(
        struct plugin_event_t *plugin,
        char *key,
        zb_barray_t *data)
{
    int err;
    char check[1024];
    err = bin2hex(data->data, data->data_length, check, (size_t)1024);
    if (!err)
    {
        plugin->ops.add_param_str(
                plugin,
                key,
                check);
    }
    return err;
}

int add_zigbee_attr_discovered(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{

    zigbee_attribute_t *attr = &event->op.attr_discovered.contents;
    zigbee_discover_attributes_params_t *params = &event->op.attr_discovered.params;

    add_zigbee_cluster(iot_ev, &attr->cluster);

    iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_ID, UINT16, &attr->attr_id);
    iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_DATA_TYPE, UINT8, &attr->attr_data_type);
    iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_START_ID, UINT16, &params->start_attribute_id);
    iot_ev->ops.add_param_type(iot_ev, ZB_MAX_ATTR, UINT8, &params->max_attributes);

    return 0;
}

int add_zigbee_comm_recv_discovered(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{
    int err = -1;
    zigbee_comm_recv_t *comm = &event->op.comm_recv_discovered.contents;
    zigbee_discover_commands_received_params_t *params = &event->op.comm_recv_discovered.params;

    err = add_zigbee_cluster(iot_ev, &comm->cluster);

    err = iot_ev->ops.add_param_type(iot_ev, ZB_CMD_ID, UINT8, &comm->comm_id) || err;
    err = iot_ev->ops.add_param_type(iot_ev, ZB_START_CMD_ID, UINT8, &params->start_command_id) || err;
    err = iot_ev->ops.add_param_type(iot_ev, ZB_MAX_CMDS, UINT8, &params->max_commands) || err;

    return err;
}

int add_zigbee_comm_gen_discovered(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{

    zigbee_discover_commands_generated_params_t *params = &event->op.comm_gen_discovered.params;
    zigbee_comm_gen_t *comm = &event->op.comm_gen_discovered.contents;

    add_zigbee_cluster(iot_ev, &comm->cluster);

    iot_ev->ops.add_param_type(iot_ev, ZB_CMD_ID, UINT8, &comm->comm_id);
    iot_ev->ops.add_param_type(iot_ev, ZB_START_CMD_ID, UINT8, &params->start_command_id);
    iot_ev->ops.add_param_type(iot_ev, ZB_MAX_CMDS, UINT8, &params->max_commands);

    return 0;
}

int add_zigbee_attr_value_received(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{
    int err = -1;
    zigbee_attr_value_received_t *attr_val = &event->op.attr_value;
    // Add params for either a read or a report update
    if (attr_val->is_report)
    {
        iot_ev->ops.add_param_str(iot_ev, ZB_IS_REPORT, "true");
        zigbee_configure_reporting_params_t *params = &attr_val->r_params;
        err = add_zigbee_cluster(iot_ev, &params->cluster);
        if (err) LOGE("%s: Failed to add zigbee cluster data", __func__);
        err = add_data_array(iot_ev, ZB_PARAM_DATA, &params->data);
        if (err) LOGE("%s: Failed to parse report data array from parameters", __func__);
    }
    else
    {
        iot_ev->ops.add_param_str(iot_ev, ZB_IS_REPORT, "false");
        zigbee_read_attributes_params_t *params = &attr_val->a_params;
        err = add_zigbee_cluster(iot_ev, &params->cluster);
        if (err) LOGE("%s: Failed to parse zigbee cluster from read params", __func__);
        for ( int i = 0; i < params->num_attributes; i++ )
        {
            iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_ID, UINT16, &params->attribute[i]);
        }
    }

    zigbee_attr_value_t *value = &attr_val->contents;
    err = add_zigbee_cluster(iot_ev, &value->cluster);
    if (err) LOGE("%s: Failed to convert attribute cluster to key/value", __func__);

    iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_ID, UINT16, &value->attr_id);

    err = add_data_array(iot_ev, ZB_DATA, &value->attr_value);
    if (err) LOGE("%s: Failed to add data array from attribute value", __func__);

    iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_DATA_TYPE, UINT8, &value->attr_data_type);

    return err;
}

int add_zigbee_attr_write_success(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{
    int err = -1;
    zigbee_write_attributes_params_t *params = &event->op.attr_write.params;
    zigbee_attr_write_t *attr_write = &event->op.attr_write.contents;
    err = add_zigbee_cluster(iot_ev, &params->cluster);
    if (err) LOGE("%s: Failed to add zigbee cluster from parameter.", __func__);

    err = add_data_array(iot_ev, ZB_PARAM_DATA, &params->data);
    
    err = add_zigbee_cluster(iot_ev, &attr_write->cluster);

    iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_ID, UINT16, &attr_write->attr_id);

    err = iot_ev->ops.add_param_type(iot_ev, ZB_STATUS, UINT8, &attr_write->write.s_code);
    if (err) LOGE("%s: Failed to convert status code to string.", __func__);

    return err;
}

int add_zigbee_report_configed_success(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{
    int err = -1;

    zigbee_configure_reporting_params_t *params = &event->op.report_success.params;
    zigbee_report_configured_t *conf_suc = &event->op.report_success.contents;
    err = add_zigbee_cluster(iot_ev, &params->cluster);
    if (err) LOGE("%s: Failed to extract cluster from configured report parameters", __func__);

    err = add_data_array(iot_ev, ZB_PARAM_DATA, &params->data);
    if (err) LOGE("%s: Failed to get data string from parameter data", __func__);

    err = add_zigbee_cluster(iot_ev, &conf_suc->cluster);
    if (err) LOGE("%s: Failed to extract cluster from configured report contents", __func__);

    err = iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_ID, UINT16, &conf_suc->attr_id) || err;
    err = iot_ev->ops.add_param_type(iot_ev, ZB_STATUS, UINT8, &conf_suc->report.s_code) || err;

    return err;
}

int add_zigbee_report_config_received(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{
    int err = -1;

    zigbee_read_reporting_configuration_params_t *params = &event->op.report_config.params;
    zigbee_report_config_t *conf = &event->op.report_config.contents;

    err = add_zigbee_cluster(iot_ev, &params->cluster);
    if (err) LOGE("%s: Failed to load cluster from params into args", __func__);

    for ( int i = 0; i < params->num_attributes; i++ )
    {
       iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_ID, UINT16, &params->attribute[i]);
    }

    err = add_zigbee_cluster(iot_ev, &conf->cluster);
    if (err) LOGE("%s: Failed to get cluster from report config", __func__);

    iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_ID, UINT16, &conf->attr_id);

    if (conf->r_configured) iot_ev->ops.add_param_str(iot_ev, ZB_IS_REPORT_CONF, "true");
    else iot_ev->ops.add_param_str(iot_ev, ZB_IS_REPORT_CONF, "false");

    zigbee_report_config_record_t *record = conf->record;

    err = iot_ev->ops.add_param_type(iot_ev, ZB_ATTR_DATA_TYPE, UINT8, &record->attr_data_type) || err;
    err = iot_ev->ops.add_param_type(iot_ev, ZB_MIN_REPORT_INT, UINT16, &record->min_report_interval) || err;
    err = iot_ev->ops.add_param_type(iot_ev, ZB_MAX_REPORT_INT, UINT16, &record->max_report_interval) || err;
    err = iot_ev->ops.add_param_type(iot_ev, ZB_TIMEOUT, UINT16, &record->timeout_period) || err;
    return err;
}

int add_zigbee_default_response(
        struct plugin_event_t *iot_ev,
        struct zigbee_event_t *event)
{
    int err = -1;
    zigbee_send_cluster_specific_command_params_t *params = &event->op.default_response.params;
    zigbee_default_response_t *response = &event->op.default_response.contents;

    LOGD("%s: Parsing the cluster from the params...", __func__);
    err = add_zigbee_cluster(iot_ev, &params->cluster);
    if (err) LOGE("%s: Unable to get cluster from parameters of event", __func__);

    LOGD("%s: Parsing the command id from the params...", __func__);
    iot_ev->ops.add_param_type(iot_ev, ZB_CMD_ID, UINT8, &params->command_id);

    if (params->data != NULL) 
    {
        LOGD("%s: Parsing the data from the params...", __func__);
        err = add_data_array(iot_ev, ZB_PARAM_DATA, params->data);
        if (err) LOGE("%s: Couldnt add data byte array from parameter.", __func__);
    }

    err = iot_ev->ops.add_param_type(iot_ev, ZB_STATUS, UINT8, &response->status.s_code) || err;
    return err;
}

/**
 * @brief Zigbee Handlers
 */
int get_uint8_param(
        struct plugin_command_t *cmd,
        char *key,
        uint8_t *output)
{
    int err = -1;

    err = cmd->ops.get_param_type(cmd, key, UINT8, output);
    if (err)
    {
        LOGE("%s: Failed to get key [%s] as uint8",
                __func__, key);
    }
    return err;
}

int get_uint16_param(
        struct plugin_command_t *cmd,
        char *key,
        uint16_t *output)
{
    int err = -1;

    err = cmd->ops.get_param_type(cmd, key, UINT16, output);
    if (err)
    {
        LOGE("%s: Failed to get key [%s] as uint16",
                __func__, key);
    }
    return err;
}

struct uint_list_t
{
    int type;
    uint16_t *uint16_items;
    uint8_t *uint8_items;
    size_t num;
};

void load_uint_param_cb(
        char *key,
        void *val,
        void *ctx
)
{
    struct uint_list_t *list = (struct uint_list_t *)ctx;
    if (list->type == UINT16)
    {
        list->uint16_items[list->num] = *(uint16_t *)val;
    }
    else
    {
        list->uint8_items[list->num] = *(uint8_t *)val;
    } 
    list->num += 1;
}

int alloc_and_load_uint_params(
        struct plugin_command_t *cmd,
        char *key,
        int type,
        uint8_t **uint8_out,
        uint16_t **uint16_out,
        uint8_t *num_out)
{
    if (cmd == NULL || key == NULL) return -1;
    struct uint_list_t list;
    memset(&list, 0, sizeof(list));

    list.type = type;
    if (type == UINT8)
    {
        list.uint8_items = calloc(1, (cmd->params->len * sizeof(uint8_t)));
        cmd->ops.foreach_param_type(cmd, key, UINT8, load_uint_param_cb, &list);
    }
    else
    {
        list.uint16_items = calloc(1, (cmd->params->len * sizeof(uint16_t)));
        cmd->ops.foreach_param_type(cmd, key, UINT16, load_uint_param_cb, &list);
    }

    if (type == UINT16) *uint16_out = list.uint16_items;
    else *uint8_out = list.uint8_items;

    *num_out = list.num;
    return 0;
}

int alloc_and_load_uint16_params(
        struct plugin_command_t *cmd,
        char *key,
        uint16_t **output,
        uint8_t *num_out)
{
    if (cmd == NULL || key == NULL) return -1;
    int err = -1;

    err = alloc_and_load_uint_params(
            cmd,
            key,
            UINT16,
            NULL,
            output,
            num_out);

    return err;
}

int alloc_and_load_uint8_params(
        struct plugin_command_t *cmd,
        char *key,
        uint8_t **output,
        uint8_t *num_out)
{
    if (cmd == NULL || key == NULL) return -1;
    int err = -1;


    err = alloc_and_load_uint_params(
            cmd,
            key,
            UINT8,
            output,
            NULL,
            num_out);

    return err;
}

int get_cluster_param(
        struct plugin_command_t *cmd,
        zigbee_cluster_t *cluster)
{
    int err = -1;
    char *ep_str = NULL;
    char *cl_str = NULL;

    err = cmd->ops.get_param_type(cmd, ZB_EP, UINT8, &cluster->ep_id);
    err = cmd->ops.get_param_type(cmd, ZB_CLUSTER_ID, UINT16, &cluster->cl_id) || err;

    if (err) LOGE("%s: Failed to translate cluster elements: ep->[%s], cl->[%s]",
            __func__, ep_str, cl_str);

    return err;
}

int get_data_param(
        struct plugin_command_t *cmd,
        zb_barray_t *zb_barray)
{
    char *decode_str = cmd->ops.get_param(cmd, DECODE_TYPE);
    char *data = cmd->ops.get_param(cmd, ZB_DATA);
    if (data == NULL) return -1;

    struct decode_type *decoder = decode_from_str(decode_str);
    if (decoder == NULL) decoder = decode_from_type(HEX);
    zb_barray->data = (uint8_t *)calloc(1, strlen(data));
    int length = decoder->decoder(data, zb_barray->data);
    zb_barray->data_length = length;
    return 0;
}

void free_data_param(zb_barray_t *zb_barray)
{
    if (zb_barray == NULL) return;
    free(zb_barray->data);
}

int get_mac(
        struct plugin_command_t *cmd,
        char **mac)
{
    *mac = cmd->ops.get_param(cmd, ZB_MAC);
    if (mac == NULL)
    {
        LOGE("%s: Could not get a mac from the command, returning an error.",
                __func__);
        return -1;
    }
    return 0;
}

void handle_configure_reporting(
        struct iotm_session *session,
        struct plugin_command_t *cmd)
{
    void *tl_context = NULL;
    bool ret = false;
    char *mac = NULL;
    int err;
    zigbee_cluster_t cluster;
    zb_barray_t zb_barray;

    tl_context = get_ctx(session);
    err = get_mac(cmd, &mac);
    mac = cmd->ops.get_param(cmd, ZB_MAC);

    memset(&cluster, 0, sizeof(cluster));
    get_cluster_param(cmd, &cluster);

    memset(&zb_barray, 0, sizeof(zb_barray));
    get_data_param(cmd, &zb_barray);

    zigbee_configure_reporting_params_t params = 
    {
        .cluster = cluster,
        .data = zb_barray,
    };
    ret = zigbee_configure_reporting(
            tl_context,
            mac,
            params);

    free_data_param(&zb_barray);

    if (!ret) LOGE("%s: Failed to configure reporting for device [%s]",
            __func__, mac);
}


void handle_discover_attributes(
        struct iotm_session *session,
        struct plugin_command_t *cmd)
{
    int err = -1;
    bool ret = false;
    char *mac = NULL;
    void *tl_context = NULL;
    zigbee_attr_id_t start_attr;
    uint8_t max_attributes = DEFAULT_MAX_ATTR;
    zigbee_cluster_t cluster;
    memset(&cluster, 0, sizeof(cluster));

    tl_context = get_ctx(session);

    err = get_mac(cmd, &mac);
    if (err)
    {
        LOGE("%s: Couldn't get mac, not continuing.",
                __func__);
        return;
    }

    err = get_uint16_param(cmd, ZB_ATTR_ID, &start_attr);
    if (err)
    {
        LOGE("%s: Failed to get the attribute ID from the commnad, can't continue.",
                __func__);
        return;
    }

    err = get_cluster_param(cmd, &cluster);
    if (err)
    {
        LOGE("%s: Failed to get cluster from command, can't continue.",
                __func__);
        return;
    }

    err = get_uint8_param(cmd, ZB_MAX_ATTR, &max_attributes);
    if (err)
    {
        LOGE("%s: Couldn't load max attributes, keeping as default.",
                __func__);
    }

    zigbee_discover_attributes_params_t params =
    {
        .cluster = cluster,
        .start_attribute_id = start_attr,
        .max_attributes = max_attributes,
    };

    ret = zigbee_discover_attributes(
            tl_context,
            mac,
            params);

    if (!ret)
    {
        LOGE("%s: Failed to discover attributes for device [%s], target layer returned an error.",
                __func__, mac);
    }
    else LOGI("%s: Sent attribute discovery request for device [%s]",
            __func__, mac);
}

void handle_discover_commands_generated(
        struct iotm_session *session,
        struct plugin_command_t *cmd)
{
    int err = -1;
    bool ret = false;
    char *mac = NULL;
    void *tl_context = NULL;
    zigbee_discover_commands_generated_params_t params;
    memset(&params, 0, sizeof(params));

    tl_context = get_ctx(session);
    err = get_mac(cmd, &mac);
    if (err)
    {
        LOGE("%s: Error getting mac for device",
                __func__);
        return;
    }

    err = get_cluster_param(cmd, &params.cluster);
    if (err)
    {
        LOGE("%s: Error getting cluster for device [%s]",
                __func__, mac);
        return;
    }

    err = get_uint8_param(cmd, ZB_CMD_ID, &params.start_command_id);
    if (err)
    {
        LOGE("%s: Error getting start command id for device [%s]",
                __func__, mac);
    }

    err = get_uint8_param(cmd, ZB_MAX_CMDS, &params.max_commands);
    if (err)
    {
        LOGE("%s: Error getting max commands for device [%s]",
                __func__, mac);
    }

    ret = zigbee_discover_commands_generated(
            tl_context,
            mac,
            params);

    if (!ret) LOGE("%s: Failed to discover commands generated for device [%s]", __func__, mac);
    else LOGI("%s: Sent command to discover commands generated to device [%s]", __func__, mac);
}

void handle_discover_commands_received(
        struct iotm_session *session,
        struct plugin_command_t *cmd)
{
    int err = -1;
    bool ret = false;
    char *mac = NULL;
    void *tl_context = NULL;
    zigbee_discover_commands_received_params_t params;
    memset(&params, 0, sizeof(params));

    tl_context = get_ctx(session);
    err = get_mac(cmd, &mac);
    if (err)
    {
        LOGE("%s: Error getting mac for device",
                __func__);
        return;
    }

    err = get_cluster_param(cmd, &params.cluster);
    if (err)
    {
        LOGE("%s: Error getting cluster for device [%s]",
                __func__, mac);
        return;
    }

    err = get_uint8_param(cmd, ZB_CMD_ID, &params.start_command_id);
    if (err)
    {
        LOGE("%s: Error getting start command id for device [%s]",
                __func__, mac);
    }

    err = get_uint8_param(cmd, ZB_MAX_CMDS, &params.max_commands);
    if (err)
    {
        LOGE("%s: Error getting max commands for device [%s]",
                __func__, mac);
    }

    ret = zigbee_discover_commands_received(
            tl_context,
            mac,
            params);

    if (!ret) LOGE("%s: Failed to discover commands generated for device [%s]", __func__, mac);
    else LOGI("%s: Sent command to discover commands generated to device [%s]", __func__, mac);
}

void handle_discover_endpoints(
        struct iotm_session *session,
        struct plugin_command_t *cmd)
{
    int err = -1;
    bool ret = false;
    char *mac = NULL;
    void *tl_context = NULL;
    zigbee_discover_endpoints_params_t params;
    memset(&params, 0, sizeof(params));

    tl_context = get_ctx(session);
    err = get_mac(cmd, &mac);
    if (err)
    {
        LOGE("%s: Error getting mac for device",
                __func__);
        return;
    }

    err = alloc_and_load_uint8_params(
            cmd,
            ZB_EP_FILT,
            &params.endpoint_filter,
            &params.num_endpoint_filters);

    if (err)
    {
        LOGE("%s: Couldn't get endpoint filter", __func__);
        params.endpoint_filter = NULL;
        params.num_endpoint_filters = 0;
    } 

    LOGD("%s: About to discover the endpoints of the device [%s]", __func__, mac);
    ret = zigbee_discover_endpoints(
            tl_context,
            mac,
            params);
    
    if (!ret) LOGE("%s: Error sending endpoint discovery payload for device [%s]", __func__, mac);
    else LOGI("%s: Sent endpoint discovery request for device [%s]", __func__, mac);
}

void handle_read_attributes(
        struct iotm_session *session,
        struct plugin_command_t *cmd)
{
    int err = -1;
    bool ret = false;
    char *mac = NULL;
    void *tl_context = NULL;
    zigbee_read_attributes_params_t params;
    memset(&params, 0, sizeof(params));

    tl_context = get_ctx(session);
    err = get_mac(cmd, &mac);
    if (err)
    {
        LOGE("%s: Error getting mac for device",
                __func__);
        return;
    }

    err = get_cluster_param(cmd, &params.cluster);
    if (err) LOGE("%s: Failed to get cluster parameter for device [%s]", __func__, mac);

    err = alloc_and_load_uint16_params(
            cmd,
            ZB_ATTR_ID,
            &params.attribute,
            &params.num_attributes);

    if (err)
    {
        LOGE("%s: No attributes set to be read for device [%s], not continuing.", __func__, mac);
        return;
    }

    ret = zigbee_read_attributes(
            tl_context,
            mac,
            params);

    if (!ret) LOGE("%s: Failed to send read attributes request for device [%s]", __func__, mac);
    else LOGI("%s: Sent read attributes request to device [%s]", __func__, mac);
}

void handle_read_reporting_configuration(
        struct iotm_session *session,
        struct plugin_command_t *cmd)
{
    int err = -1;
    bool ret = false;
    char *mac = NULL;
    void *tl_context = NULL;
    zigbee_read_reporting_configuration_params_t params;
    memset(&params, 0, sizeof(params));

    tl_context = get_ctx(session);
    err = get_mac(cmd, &mac);
    if (err)
    {
        LOGE("%s: Error getting mac for device",
                __func__);
        return;
    }

    err = get_cluster_param(cmd, &params.cluster);
    if (err) LOGE("%s: Couldn't get cluster from parameters for device [%s]", __func__, mac);


    err = alloc_and_load_uint16_params(
            cmd,
            ZB_ATTR_ID,
            &params.attribute,
            &params.num_attributes);

    if (err)
    {
        LOGE("%s: No attributes in parameter to configure reporting for on device [%s]. Not sending request.",
                __func__, mac);
        return;
    }


    ret = zigbee_read_reporting_configuration(
            tl_context,
            mac,
            params);

    if (!ret) LOGE("%s: Failed to send request to configure reporting for device [%s]", __func__, mac);
    else LOGI("%s: Sent request to configure reporting to device [%s]", __func__, mac);
}

void handle_send_cluster_specific_command(
        struct iotm_session *session,
        struct plugin_command_t *cmd)
{
    int err = -1;
    bool ret = false;
    char *mac = NULL;
    void *tl_context = NULL;
    zigbee_send_cluster_specific_command_params_t params;
    memset(&params, 0, sizeof(params));

    LOGD("%s: about to gather parameters.", __func__);
    tl_context = get_ctx(session);
    err = get_mac(cmd, &mac);
    if (err)
    {
        LOGE("%s: Error getting mac for device",
                __func__);
        return;
    }

    err = get_cluster_param(cmd, &params.cluster);
    if (err) LOGE("%s: Failed to get cluster parameter for device [%s]", __func__, mac);

    err = get_uint8_param(cmd, ZB_CMD_ID, &params.command_id);
    if (err) LOGE("%s: Failed to get command ID from parameter list for device [%s]", __func__, mac);

    err = get_data_param(cmd, params.data);
    if (err) LOGI("%s: no data to be send for cluster specific command for device [%s]", __func__, mac);

    ret = zigbee_send_cluster_specific_command(
            tl_context,
            mac,
            params);

    if (!ret) LOGE("%s: Failed to send cluster specific command to device [%s]", __func__, mac);
    else LOGI("%s: Sent cluster specific command to device [%s]", __func__, mac);
}

void handle_send_network_leave(
        struct iotm_session *session,
        struct plugin_command_t *cmd)
{
    int err = -1;
    bool ret = false;
    char *mac = NULL;
    void *tl_context = NULL;

    tl_context = get_ctx(session);
    err = get_mac(cmd, &mac);
    if (err)
    {
        LOGE("%s: Error getting mac for device",
                __func__);
        return;
    }

    ret = zigbee_send_network_leave(
            tl_context,
            mac);

    if (!ret) LOGE("%s: Failed to send network leave to device [%s]", __func__, mac);
    else LOGI("%s: Sent network leave request to device [%s]", __func__, mac);
}

void handle_write_attributes(
        struct iotm_session *session,
        struct plugin_command_t *cmd)
{
    int err = -1;
    bool ret = false;
    char *mac = NULL;
    void *tl_context = NULL;
    zigbee_write_attributes_params_t params;
    memset(&params, 0, sizeof(params));

    tl_context = get_ctx(session);
    err = get_mac(cmd, &mac);
    if (err)
    {
        LOGE("%s: Error getting mac for device",
                __func__);
        return;
    }

    err = get_cluster_param(cmd, &params.cluster);
    if (err) LOGE("%s: Couldn't get cluster from parameters for device [%s]", __func__, mac);

    err = get_data_param(cmd, &params.data);
    if (err) LOGI("%s: no data to be send for cluster specific command for device [%s]", __func__, mac);

    ret = zigbee_write_attributes(
            tl_context,
            mac,
            params);

    if (!ret) LOGE("%s: Failed to send write attributes request to device [%s]", __func__, mac);
    else LOGI("%s: Sent write attributes request to device [%s]", __func__, mac);
}
