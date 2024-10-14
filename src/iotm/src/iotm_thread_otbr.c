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
#include <inttypes.h>
#include <string.h>
#include "iotm.h"
#include "log.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "const.h"
#include "ovsdb_update.h"
#include "schema.h"
#include "osp_otbr.h"
#include "qm_conn.h"
#include "thread_network_info.pb-c.h"


#define MODULE_ID LOG_MODULE_ID_MAIN

/**
 * Convert 64-bit integer to 16-char lowercase hex string
 *
 * @param[in]  value_        Value to convert.
 * @param[out] char_buffer_  Buffer to store the result to, must be at least 17 bytes long.
 *
 * @return Pointer to the `char_buffer_`.
 */
#define UINT64_TO_HEX(value_, char_buffer_) ({                                                  \
        C_STATIC_ASSERT((sizeof(char_buffer_) >= 17), "16+1 chars required for 64-bit hex");    \
        snprintf((char_buffer_), sizeof(char_buffer_), "%016" PRIx64, (value_));                \
        (char_buffer_);                                                                         \
    })

/** Set `TO_SCHEMA->FIELD` to `FROM_SCHEMA->FIELD` if the latter is not empty, otherwise unset it */
#define SCHEMA_MOVE_STR_NONEMPTY(FROM_SCHEMA, TO_SCHEMA, FIELD) \
    SCHEMA_SET_STR_NONEMPTY((TO_SCHEMA)->FIELD, (FROM_SCHEMA)->FIELD)


static ovsdb_table_t table_IP_Interface;
static ovsdb_table_t table_Thread_Radio_Config;
static ovsdb_table_t table_Thread_Radio_State;
static ovsdb_table_t table_Thread_Devices;
static ovsdb_table_t table_AWLAN_Node;

static struct
{
    char node_id[C_MAXPATH_LEN];
    char location_id[C_MAXPATH_LEN];
    /** MQTT topics for Thread Network status reporting */
    struct
    {
        /** AWLAN_Node::mqtt_topics::ThreadNetwork.Scan */
        char scan[C_MAXPATH_LEN];
    } topics;
} g_mqtt;

/** Callback invoked from OSP when the dataset changes */
static osp_otbr_on_dataset_change_cb_t on_dataset_change_cb;
/** Callback invoked from OSP when the new network topology report is available */
static osp_otbr_on_network_topology_cb_t on_network_topology_cb;
/** Callback invoked from OSP when the new network scan or discovery result is available */
static osp_otbr_on_network_scan_result_cb_t on_network_scan_result_cb;


/** Update the state table or break the event loop on failure */
static void update_state(struct schema_Thread_Radio_State *const state)
{
    if (!ovsdb_table_upsert(&table_Thread_Radio_State, state, false))
    {
        LOGE("Could not update %s", SCHEMA_TABLE(Thread_Radio_State));
        ev_break(EV_DEFAULT, EVBREAK_ONE);
    }
}

static void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old,
        struct schema_AWLAN_Node *new)
{
    (void)old;

    if (mon->mon_type == OVSDB_UPDATE_ERROR)
    {
        return;
    }

    if (new != NULL)
    {
        STRSCPY_WARN(g_mqtt.node_id, SCHEMA_KEY_VAL(new->mqtt_headers, "nodeId"));
        STRSCPY_WARN(g_mqtt.location_id, SCHEMA_KEY_VAL(new->mqtt_headers, "locationId"));
        STRSCPY_WARN(g_mqtt.topics.scan, SCHEMA_KEY_VAL(new->mqtt_topics, "ThreadNetwork.Scan"));
    }
    else
    {
        g_mqtt.node_id[0] = '\0';
        g_mqtt.location_id[0] = '\0';
        g_mqtt.topics.scan[0] = '\0';
    }
}

/** Get IP interface by UUID */
static bool get_ip_if(const ovs_uuid_t *const ip_if_uuid, struct schema_IP_Interface *const ip_interface)
{
    if (!ovsdb_table_select_one_where(&table_IP_Interface,
                                      ovsdb_where_uuid("_uuid", ip_if_uuid->uuid),
                                      ip_interface))
    {
        LOGE("No IP interface '%s'", ip_if_uuid->uuid);
        return false;
    }
    return true;
}

/** Apply new general configuration from the config table to the OSP layer, and update state table accordingly */
static bool apply_config_enable_and_interfaces(const struct schema_Thread_Radio_Config *const config,
                                               struct schema_Thread_Radio_State *const state)
{
    if (!config->enable)
    {
        /* Disabled - nothing else to do */
        if (config->enable_changed)
        {
            osp_otbr_set_thread_radio(false); /*< Ignore result */
            if (!osp_otbr_stop())
            {
                LOGE("Could not stop OTBR");
                return false;
            }
        }

        SCHEMA_SET_BOOL(state->enable, false);
        state->_partial_update = false;
    }
    else if (config->enable_changed)
    {
        /* Became enabled */
        struct schema_IP_Interface ip_interface = {0};
        const char *network_interface;
        uint64_t eui64 = 0;
        uint64_t ext_addr = 0;
        char str[17];

        /* thread_interface is mandatory, network_interface is optional */
        if (config->network_interface_exists)
        {
            if (!get_ip_if(&config->network_interface, &ip_interface))
            {
                return false;
            }
            network_interface = ip_interface.if_name;
        }
        else
        {
            network_interface = NULL;
        }

        if (!osp_otbr_start(config->thread_interface, network_interface, &eui64, &ext_addr))
        {
            LOGE("Could not start OTBR");
            return false;
        }
        if (!osp_otbr_set_thread_radio(true))
        {
            LOGE("Could not enable Thread radio");
            osp_otbr_stop();
            return false;
        }

        SCHEMA_SET_STR(state->thread_interface, config->thread_interface);
        if (config->network_interface_exists)
        {
            SCHEMA_SET_UUID(state->network_interface, ip_interface._uuid.uuid);
        }
        else
        {
            SCHEMA_UNSET_FIELD(state->network_interface);
        }

        SCHEMA_SET_STR(state->eui64, UINT64_TO_HEX(eui64, str));
        SCHEMA_SET_STR(state->ext_address, UINT64_TO_HEX(ext_addr, str));

        SCHEMA_SET_BOOL(state->enable, true);
        state->_partial_update = false;
    }
    return true;
}

/** Apply new dataset from the config table to the OSP layer, and update state table accordingly */
static bool apply_config_dataset(const struct schema_Thread_Radio_Config *config,
                                 struct schema_Thread_Radio_State *state)
{
    struct osp_otbr_dataset_tlvs_s tlvs;
    size_t ds_str_len;

    if (!config->dataset_changed)
    {
        return true;
    }

    ds_str_len = strlen(config->dataset);
    if (ds_str_len > 2 * sizeof(tlvs.tlvs))
    {
        LOGE("Dataset too big (%zu > 2*%zu)", ds_str_len, sizeof(tlvs.tlvs));
        return false;
    }
    tlvs.len = (uint8_t) (ds_str_len / 2);

    if (hex2bin(config->dataset, ds_str_len, tlvs.tlvs, sizeof(tlvs.tlvs)) != tlvs.len)
    {
        LOGE("Could not convert dataset to binary (%s)", config->dataset);
        return false;
    }

    if (!osp_otbr_set_dataset(&tlvs, true))
    {
        LOGE("Could not set dataset");
        return false;
    }

    SCHEMA_SET_STR(state->dataset, config->dataset);
    return true;
}

/** Create new network with params from the config table (state shall be updated via dataset changed callback) */
static bool apply_config_network_params(const struct schema_Thread_Radio_Config *const config)
{
    struct otbr_osp_network_params_s params = {0};
    struct osp_otbr_dataset_tlvs_s dataset = {0};

    /* Ensure that name is null-terminated */
    if (STRSCPY(params.network_name, config->network_name) <= 0)
    {
        LOGE("network_name '%.*s' invalid (1-16 chars)", (int) sizeof(config->network_name), config->network_name);
        return false;
    }
    params.pan_id = config->pan_id_exists ? (uint16_t) config->pan_id : UINT16_MAX;
    params.ext_pan_id = config->ext_pan_id_exists ? strtoull(config->ext_pan_id, NULL, 16) : UINT64_MAX;
    if (config->network_key_exists &&
        hex2bin(config->network_key, strlen(config->network_key),
                params.network_key, sizeof(params.network_key)) != sizeof(params.network_key))
    {
        LOGE("Invalid network_key '%.*s'", (int) sizeof(config->network_key), config->network_key);
        return false;
    }
    if (config->mesh_local_prefix_exists &&
        !osn_ip6_addr_from_str(&params.mesh_local_prefix, config->mesh_local_prefix))
    {
        LOGE("Invalid mesh_local_prefix '%s'", config->mesh_local_prefix);
        return false;
    }
    params.channel = config->channel_exists ? (uint8_t) config->channel : UINT8_MAX;
    params.channel_mask = config->channel_mask_exists ? (uint32_t) config->channel_mask : UINT32_MAX;
    params.commissioning_psk = config->commissioning_psk_exists ? config->commissioning_psk : NULL;

    if (!osp_otbr_create_network(&params, &dataset))
    {
        LOGE("Could not create dataset from provided Thread network parameters");
        return false;
    }
    if (!osp_otbr_set_dataset(&dataset, true))
    {
        LOGE("Could not set dataset");
        return false;
    }

    return true;
}

/**
 * Apply new configuration to the OSP layer
 *
 * @param[in]  config  New configuration to apply.
 * @param[out] state   State to be updated (with `_partial_update` set to true).
 *
 * @return true on success.
 */
static bool apply_config(struct schema_Thread_Radio_Config *config, struct schema_Thread_Radio_State *state)
{
    bool network_params_exist;

    /* OTBR needs to be restarted if the interfaces change */
    if (config->enable && !config->enable_changed &&
        (config->thread_interface_changed || config->network_interface_changed))
    {
        LOGI("Restarting OTBR due to interface change");
        config->enable_changed = true;
    }

    if (!apply_config_enable_and_interfaces(config, state))
    {
        return false;
    }

    network_params_exist = (config->network_name_exists ||
                            config->pan_id_exists ||
                            config->ext_pan_id_exists ||
                            config->network_key_exists ||
                            config->mesh_local_prefix_exists ||
                            config->channel_exists ||
                            config->channel_mask_exists ||
                            config->commissioning_psk_exists);

    if (config->dataset_exists)
    {
        if (network_params_exist)
        {
            LOGW("Ignoring other Thread Network parameters when the dataset is set");
        }

        if (!apply_config_dataset(config, state))
        {
            return false;
        }
    }
    else if (network_params_exist && !apply_config_network_params(config))
    {
        return false;
    }

    if (config->reporting_interval_changed)
    {
        if (!osp_otbr_set_report_interval(config->reporting_interval, config->reporting_interval))
        {
            LOGE("Could not set %d s report interval", config->reporting_interval);
            return false;
        }
        SCHEMA_SET_INT(state->reporting_interval, config->reporting_interval);
    }

    return true;
}

/** Callback invoked from OVSDB when the config table changes */
static void callback_Thread_Radio_Config(ovsdb_update_monitor_t *mon,
                                         struct schema_Thread_Radio_Config *old_rec,
                                         struct schema_Thread_Radio_Config *config)
{
    struct schema_Thread_Radio_State state = {._partial_update = true};
    (void) old_rec;

    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        /* If this is an initial invocation of this callback, mark all currently present rows as changed to apply
         * complete initial state of the table, and force overwrite complete state table to remove stale data. */
        schema_Thread_Radio_Config_mark_changed(config, config);
        state._partial_update = false;
    }
    else if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        /* When/If this feature is disabled, the config table gets deleted */
        config->enable = false;
        config->enable_changed = true;
    }

    apply_config(config, &state);
    update_state(&state);
}

/**
 * Initialize missing mandatory fields the Config table with default values
 *
 * @return true on success.
 */
static bool initialize_config_table(void)
{
    struct schema_Thread_Radio_Config config = {0};

    /* The Config table may not exist yet */
    ovsdb_table_select_one_where(&table_Thread_Radio_Config, NULL, &config);

    /* Populate the mandatory fields with default values */
    if (!config.enable_exists)
    {
        SCHEMA_SET_BOOL(config.enable, false);
        config._partial_update = true;
    }

    if (!config.thread_interface_exists)
    {
        SCHEMA_SET_STR(config.thread_interface, CONFIG_IOTM_THREAD_INTERFACE_NAME);
        config._partial_update = true;
    }

    if (!config.network_interface_exists)
    {
        ovs_uuid_t if_uuid;

        if (ovsdb_sync_get_uuid(SCHEMA_TABLE(IP_Interface),
                                SCHEMA_COLUMN(IP_Interface, if_name),
                                CONFIG_TARGET_LAN_BRIDGE_NAME,
                                &if_uuid))
        {
            SCHEMA_SET_UUID(config.network_interface, if_uuid.uuid);
            config._partial_update = true;
        }
        else
        {
            LOGI("IP interface '%s' does not exist", CONFIG_TARGET_LAN_BRIDGE_NAME);
        }
    }

    if (!config.reporting_interval_exists)
    {
        SCHEMA_SET_INT(config.reporting_interval, CONFIG_IOTM_THREAD_SCAN_INTERVAL);
        config._partial_update = true;
    }

    /* Only update if required */
    if (config._partial_update && !ovsdb_table_upsert(&table_Thread_Radio_Config, &config, false))
    {
        LOGE("Could not update Thread_Radio_Config");
        return false;
    }

    return true;
}

/** Initialize the IoTM - OpenThread Border Router module */
bool iotm_thread_otbr_init(struct ev_loop *loop)
{
    LOGI("Initializing IoTM Thread OTBR");

    if (!osp_otbr_init(loop, on_dataset_change_cb, on_network_topology_cb, on_network_scan_result_cb))
    {
        LOGE("OSP OTBR init failed");
        return false;
    }

    MEMZERO(g_mqtt);

    /* Initialize OVSDB tables */
    OVSDB_TABLE_INIT_NO_KEY(IP_Interface);
    OVSDB_TABLE_INIT_NO_KEY(Thread_Radio_Config);
    OVSDB_TABLE_INIT_NO_KEY(Thread_Radio_State);
    OVSDB_TABLE_INIT_NO_KEY(Thread_Devices);
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);

    /* Initialize OVSDB monitor callbacks */
    OVSDB_TABLE_MONITOR(Thread_Radio_Config, false);
    OVSDB_TABLE_MONITOR_F(
            AWLAN_Node,
            C_VPACK(SCHEMA_COLUMN(AWLAN_Node, mqtt_headers), SCHEMA_COLUMN(AWLAN_Node, mqtt_topics)));

    return initialize_config_table();
}

/** Close the IoTM - OpenThread Border Router module */
void iotm_thread_otbr_close(void)
{
    osp_otbr_close();
    MEMZERO(g_mqtt);
}


/** Callback invoked from OSP when the dataset changes */
static void on_dataset_change_cb(struct osp_otbr_dataset_tlvs_s *dataset)
{
    struct schema_Thread_Radio_State state = {._partial_update = true};

    if (dataset->len > 0)
    {
        char str[sizeof(dataset->tlvs) * 2 + 1];

        str[0] = '\0';
        bin2hex(dataset->tlvs, dataset->len, str, sizeof(str));
        SCHEMA_SET_STR(state.dataset, str);
    }
    else
    {
        SCHEMA_UNSET_FIELD(state.dataset);
    }

    update_state(&state);
}

/* *** Thread Network topology reporting *** */

/** Copy data from OSP device structure to OVSDB device table entry */
static void osp_otbr_device_to_thread_device(const struct osp_otbr_device_s *const ospd,
                                             struct schema_Thread_Devices *const std)
{
    static const char *const roles[] = {
            [OSP_OTBR_DEVICE_ROLE_DISABLED] = "disabled",
            [OSP_OTBR_DEVICE_ROLE_DETACHED] = "detached",
            [OSP_OTBR_DEVICE_ROLE_CHILD] = "child",
            [OSP_OTBR_DEVICE_ROLE_ROUTER] = "router",
            [OSP_OTBR_DEVICE_ROLE_LEADER] = "leader",
    };
    char str[OSN_IP6_ADDR_LEN];

    ASSERT(ospd->role < ARRAY_SIZE(roles), "Invalid role %s", ospd->role);

    SCHEMA_SET_STR(std->ext_address, UINT64_TO_HEX(ospd->ext_addr, str));
    SCHEMA_SET_INT(std->rloc16, ospd->rloc16);
    SCHEMA_SET_STR(std->role, roles[ospd->role]);

    for (size_t i_ip = 0; i_ip < ospd->ip_addresses.count; i_ip++)
    {
        snprintf(str, sizeof(str), PRI_osn_ip6_addr, FMT_osn_ip6_addr(ospd->ip_addresses.addr[i_ip]));
        SCHEMA_VAL_APPEND(std->ip_addresses, str);
    }

    if (ospd->version != 0xFFFF)
    {
        SCHEMA_SET_INT(std->version, ospd->version);
    }
    SCHEMA_MOVE_STR_NONEMPTY(ospd, std, vendor_name);
    SCHEMA_MOVE_STR_NONEMPTY(ospd, std, vendor_model);
    SCHEMA_MOVE_STR_NONEMPTY(ospd, std, vendor_sw_version);
    SCHEMA_MOVE_STR_NONEMPTY(ospd, std, thread_stack_version);

    if (ospd->role == OSP_OTBR_DEVICE_ROLE_CHILD)
    {
        char u16_hex[4 + 1];

        SCHEMA_SET_BOOL(std->full_thread_device, ospd->child.mode.full_thread_device);
        SCHEMA_SET_BOOL(std->rx_on_when_idle, ospd->child.mode.rx_on_when_idle);
        SCHEMA_SET_BOOL(std->full_network_data, ospd->child.mode.full_network_data);
        SCHEMA_SET_BOOL(std->is_border_router, false);

        snprintf(u16_hex, sizeof(u16_hex), "%04X", ospd->child.parent.rloc16);
        SCHEMA_KEY_VAL_APPEND_INT(std->links, u16_hex, ospd->child.parent.lq_in);
    }
    else if (ospd->role >= OSP_OTBR_DEVICE_ROLE_ROUTER)
    {
        SCHEMA_SET_BOOL(std->full_thread_device, true);
        SCHEMA_SET_BOOL(std->rx_on_when_idle, true);
        SCHEMA_SET_BOOL(std->full_network_data, true);
        SCHEMA_SET_BOOL(std->is_border_router, ospd->router.is_border_router);

        for (size_t i_link = 0; i_link < ospd->router.neighbors.count; i_link++)
        {
            char u16_hex[4 + 1];

            snprintf(u16_hex, sizeof(u16_hex), "%04X", ospd->router.neighbors.links[i_link].rloc16);
            SCHEMA_KEY_VAL_APPEND_INT(std->links, u16_hex, ospd->router.neighbors.links[i_link].lq_in);
        }
    }
}

/** Callback invoked from OSP when the new network topology report is available */
static void on_network_topology_cb(enum osp_otbr_device_role_e role, struct osp_otbr_devices_s *devices)
{
    /* There is no ovsdb_tran_multi() for upsert - get all existing devices first,
     * as they are required for later deletion of the ones not present anymore.
     * Also use this info to decide whether to update or insert. */
    const char *const pk_column = SCHEMA_COLUMN(Thread_Devices, ext_address);
    json_t *existing;
    json_t *tran = NULL;
    json_t *result;
    size_t i_row;
    json_t *row;

    LOGI("Device has role %d, %d devices in the network", role, devices ? (int) devices->count : -1);

    /* 1. Get all rows currently in the table */
    existing = ovsdb_sync_select_where2(SCHEMA_TABLE(Thread_Devices), NULL);

    /* 2. Update existing rows (and delete them from `existing`), insert new ones (with the link to parent table) */
    for (size_t i_dev = 0; devices && (i_dev < devices->count); i_dev++)
    {
        /* This callback always contains full network topology, so update (_partial_update = true)
         * all columns of existing devices (if there are any), including the deletion of the ones
         * not present anymore - as function osp_otbr_device_to_thread_device() only adds currently
         * present properties, mark all as present to achieve this. */
        struct schema_Thread_Devices std = {
                ._partial_update = true
        };
        json_t *where = NULL;

        schema_Thread_Devices_mark_all_present(&std);
        osp_otbr_device_to_thread_device(&devices->devices[i_dev], &std);

        /* If this device is already present in the table, generate a `where` condition to update it */
        json_array_foreach(existing, i_row, row)
        {
            const char *const pk = json_string_value(json_object_get(row, pk_column));

            if (strcmp(pk, std.ext_address) == 0)
            {
                where = ovsdb_where_simple(pk_column, pk);
                /* `ext_address` is unique, so there is no need to search this device again.
                 * Additionally, all devices left in the `existing` are deleted below. */
                json_array_remove(existing, i_row);
                break;
            }
        }

        row = ovsdb_table_to_json(&table_Thread_Devices, &std);
        if (where == NULL)
        {
            LOGD("Inserting %s", std.ext_address);
            tran = ovsdb_tran_multi_insert_with_parent(
                    tran,
                    SCHEMA_TABLE(Thread_Devices), row,
                    SCHEMA_TABLE(Thread_Radio_State), NULL, SCHEMA_COLUMN(Thread_Radio_State, thread_devices)
            );
            /* ovsdb_tran_multi_insert_with_parent() takes ownership of the `row` reference */
        }
        else
        {
            LOGD("Updating %s", std.ext_address);
            /* Update operation does not change the UUID, so there is no need to update the parent table reference */
            tran = ovsdb_tran_multi(tran, NULL, SCHEMA_TABLE(Thread_Devices), OTR_UPDATE, where, row);
            /* ovsdb_tran_multi() takes ownership of the `where` and `row` reference */
        }
    }

    /* 3. Delete everything that is left in `existing`, as it is not present in the current network topology */
    if (json_array_size(existing) > 0)
    {
        json_t *uuids = json_array();

        json_array_foreach(existing, i_row, row)
        {
            LOGD("Deleting %s", json_string_value(json_object_get(row, pk_column)));
            json_array_append(uuids, json_object_get(row, "_uuid")); /*< Increment ref. count */
        }

        tran = ovsdb_tran_multi_delete_with_parent(
                tran,
                SCHEMA_TABLE(Thread_Devices), uuids,
                SCHEMA_TABLE(Thread_Radio_State), NULL, SCHEMA_COLUMN(Thread_Radio_State, thread_devices)
        );
        /* ovsdb_tran_multi_delete_with_parent() takes ownership of the `uuids` reference */
    }
    json_decref(existing);

    if (json_array_size(tran) < 1)
    {
        LOGD("No changes to %s", SCHEMA_TABLE(Thread_Devices));
        json_decref(tran);
        return;
    }

    /* 4. Execute prepared OVSDB transactions */
    result = ovsdb_method_send_s(MT_TRANS, tran); /*< Takes over `tran` reference */

    /* Check if any operation failed */
    json_array_foreach(result, i_row, row)
    {
        if (json_object_get(row, "error") != NULL)
        {
            LOGE("Error updating %s: %s", SCHEMA_TABLE(Thread_Devices), json_dumps_static(row, 0));
        }
    }
    json_decref(result);
}

/* *** Thread Network scan/discovery result reporting *** */

static Otbr__ThreadNetwork *pb_set_thread_network(struct osp_otbr_scan_result_s *const network)
{
    Otbr__ThreadNetwork *pb;

    pb = MALLOC(sizeof(*pb));
    otbr__thread_network__init(pb);

    pb->ext_addr = network->ext_addr;
    pb->ext_pan_id = network->ext_pan_id;
    pb->pan_id = network->pan_id;
    pb->name = network->name;
    pb->steering_data.len = network->steering_data.length;
    pb->steering_data.data = network->steering_data.data;
    pb->channel = network->channel;
    pb->rssi = network->rssi;
    pb->lqi = network->lqi;
    pb->joiner_udp_port = network->joiner_udp_port;
    pb->version = network->version;
    pb->native = network->native;
    pb->discover = network->discover;

    return pb;
}

static Otbr__ThreadNetwork **pb_set_thread_networks(const struct osp_otbr_scan_results_s *const networks)
{
    Otbr__ThreadNetwork **pb_arr;

    pb_arr = CALLOC(networks->count, sizeof(*pb_arr));

    for (size_t i = 0; i < networks->count; i++)
    {
        pb_arr[i] = pb_set_thread_network(&networks->networks[i]);
    }

    return pb_arr;
}

static Otbr__ObservationPoint *pb_set_observation_point(void)
{
    Otbr__ObservationPoint *pb;

    pb = MALLOC(sizeof(*pb));
    otbr__observation_point__init(pb);

    pb->node_id = g_mqtt.node_id;
    pb->location_id = g_mqtt.location_id;

    return pb;
}

static Otbr__ThreadNetworkScan *pb_serialize_thread_network_scan(const struct osp_otbr_scan_results_s *const networks)
{
    Otbr__ThreadNetworkScan *pb;

    pb = MALLOC(sizeof(*pb));
    otbr__thread_network_scan__init(pb);

    if (networks->count > 0)
    {
        pb->networks = pb_set_thread_networks(networks);
        pb->n_networks = networks->count;
    }

    pb->observation_point = pb_set_observation_point();

    return pb;
}

static void pb_free_thread_network_scan(Otbr__ThreadNetworkScan *const pb)
{
    while (pb->n_networks > 0)
    {
        FREE(pb->networks[--pb->n_networks]);
    }
    FREE(pb->networks);
    FREE(pb->observation_point);
    FREE(pb);
}

static void on_network_scan_result_cb(struct osp_otbr_scan_results_s *const networks)
{
    Otbr__ThreadNetworkScan *pb;
    uint8_t *buff;
    size_t buff_len;
    qm_response_t res;

    if (strnlen(g_mqtt.topics.scan, sizeof(g_mqtt.topics.scan)) == 0)
    {
        LOGD("ThreadNetwork.Scan reporting disabled");
        return;
    }

    /* Serialize protobuf structures */
    pb = pb_serialize_thread_network_scan(networks);
    buff = MALLOC(otbr__thread_network_scan__get_packed_size(pb));
    buff_len = otbr__thread_network_scan__pack(pb, buff);
    pb_free_thread_network_scan(pb);

    /* Send the serialized data */
    if (!qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, g_mqtt.topics.scan, buff, (int)buff_len, &res))
    {
        LOGE("ThreadNetworkScan publish failed (%d, %d)", res.response, res.error);
    }
    FREE(buff);
}
