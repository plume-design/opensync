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

#include <stdlib.h>
#include <stdio.h>
#include <jansson.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "module.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "schema.h"
#include "json_util.h"
#include "util.h"
#include "log.h"
#include "osp_ps.h"
#include "os.h"


#define KEY_OFFLINE_CFG             "gw_offline_cfg"
#define KEY_OFFLINE_MON             "gw_offline_mon"
#define KEY_OFFLINE                 "gw_offline"
#define KEY_OFFLINE_STATUS          "gw_offline_status"

#define VAL_OFFLINE_ON              "true"
#define VAL_OFFLINE_OFF             "false"
#define VAL_STATUS_READY            "ready"
#define VAL_STATUS_ACTIVE           "active"
#define VAL_STATUS_ENABLED          "enabled"
#define VAL_STATUS_DISABLED         "disabled"
#define VAL_STATUS_ERROR            "error"

#define PM_MODULE_NAME              "PM"

#define PS_STORE_GW_OFFLINE         "pm_gw_offline_store"

#define PS_KEY_VIF_CONFIG           "vif_config"
#define PS_KEY_INET_CONFIG          "inet_config"
#define PS_KEY_RADIO_CONFIG         "radio_config"
#define PS_KEY_INET_CONFIG_HOME_APS "inet_config_home_aps"
#define PS_KEY_RADIO_IF_NAMES       "radio_if_names"
#define PS_KEY_DHCP_RESERVED_IP     "dhcp_reserved_ip"

#define PS_KEY_OFFLINE_CFG          KEY_OFFLINE_CFG

#define TIMEOUT_NO_CFG_CHANGE      30

#if defined(CONFIG_TARGET_LAN_BRIDGE_NAME)
#define LAN_BRIDGE   CONFIG_TARGET_LAN_BRIDGE_NAME
#else
#define LAN_BRIDGE   SCHEMA_CONSTS_BR_NAME_HOME
#endif

struct gw_offline_cfg
{
    json_t *vif_config;
    json_t *inet_config;
    json_t *radio_config;
    json_t *inet_config_home_aps;
    json_t *radio_if_names;
    json_t *dhcp_reserved_ip;
};

enum gw_offline_stat
{
    status_disabled = 0,
    status_enabled,
    status_ready,
    status_active,
    status_error
};

MODULE(pm_gw_offline, pm_gw_offline_init, pm_gw_offline_fini)

static ovsdb_table_t table_Node_Config;
static ovsdb_table_t table_Node_State;
static ovsdb_table_t table_Wifi_VIF_Config;
static ovsdb_table_t table_Wifi_Inet_Config;
static ovsdb_table_t table_Wifi_Radio_Config;
static ovsdb_table_t table_DHCP_reserved_IP;

static ev_timer timeout_no_cfg_change;

static struct gw_offline_cfg cfg_cache;

static bool gw_offline_cfg;
static bool gw_offline_mon;
static bool gw_offline;
static enum gw_offline_stat gw_offline_stat;

static bool gw_offline_ps_store(const char *ps_key, const json_t *config);
static bool gw_offline_cfg_ps_store(const struct gw_offline_cfg *cfg);
static bool gw_offline_ps_load(const char *ps_key, json_t **config);
static bool gw_offline_cfg_ps_load(struct gw_offline_cfg *cfg);

static bool gw_offline_cfg_ovsdb_read(struct gw_offline_cfg *cfg);
static bool gw_offline_cfg_ovsdb_apply(const struct gw_offline_cfg *cfg);

static bool gw_offline_uplink_bridge_set(const struct gw_offline_cfg *cfg);

bool pm_gw_offline_cfg_is_available();
bool pm_gw_offline_load_and_apply_config();
bool pm_gw_offline_read_and_store_config();

static void delete_special_ovsdb_keys(json_t *rows)
{
    size_t index;
    json_t *row;

    json_array_foreach(rows, index, row)
    {
        json_object_del(row, "_uuid");
        json_object_del(row, "_version");
    }
}

static void gw_offline_cfg_release(struct gw_offline_cfg *cfg)
{
    json_decref(cfg->vif_config);
    json_decref(cfg->inet_config);
    json_decref(cfg->radio_config);
    json_decref(cfg->inet_config_home_aps);
    json_decref(cfg->radio_if_names);
    json_decref(cfg->dhcp_reserved_ip);
}

static void gw_offline_cfg_delete_special_keys(struct gw_offline_cfg *cfg)
{
    delete_special_ovsdb_keys(cfg->vif_config);
    delete_special_ovsdb_keys(cfg->inet_config);
    delete_special_ovsdb_keys(cfg->radio_config);
    delete_special_ovsdb_keys(cfg->inet_config_home_aps);
    delete_special_ovsdb_keys(cfg->dhcp_reserved_ip);
}

/* Determine if the saved config is "bridge config": */
static bool gw_offline_cfg_is_bridge(const struct gw_offline_cfg *cfg)
{
    json_t *row;
    const char *ip_assign_scheme;

    row = json_array_get(cfg->inet_config, 0);
    ip_assign_scheme = json_string_value(json_object_get(row, "ip_assign_scheme"));

    if (ip_assign_scheme != NULL && strcmp(ip_assign_scheme, "dhcp") == 0)
        return true;

    return false;
}

static void on_timeout_cfg_no_change(struct ev_loop *loop, ev_timer *watcher, int revent)
{
    if (!(gw_offline_cfg && gw_offline_mon))
    {
        LOG(TRACE, "offline_cfg: %s() called, but gw_offline_cfg=%d, gw_offline_mon=%d. Ignoring",
                __func__, gw_offline_cfg, gw_offline_mon);
        return;
    }

    pm_gw_offline_read_and_store_config();
}

static bool pm_node_state_set(const char *key, const char *value)
{
    struct schema_Node_State node_state;
    json_t *where;

    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("module", OFUNC_EQ, PM_MODULE_NAME));
    json_array_append_new(where, ovsdb_tran_cond_single("key", OFUNC_EQ, (char *)key));

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, PM_MODULE_NAME);
    SCHEMA_SET_STR(node_state.key, key);

    if (value != NULL)
    {
        SCHEMA_SET_STR(node_state.value, value);
        ovsdb_table_upsert_where(&table_Node_State, where, &node_state, false);
    }
    else
    {
        ovsdb_table_delete_where(&table_Node_State, where);
    }

    if (strcmp(key, KEY_OFFLINE_STATUS) == 0)
    {
        if (strcmp(value, VAL_STATUS_DISABLED) == 0) gw_offline_stat = status_disabled;
        else if (strcmp(value, VAL_STATUS_ENABLED) == 0) gw_offline_stat = status_enabled;
        else if (strcmp(value, VAL_STATUS_READY) == 0) gw_offline_stat = status_ready;
        else if (strcmp(value, VAL_STATUS_ACTIVE) == 0) gw_offline_stat = status_active;
        else if (strcmp(value, VAL_STATUS_ERROR) == 0) gw_offline_stat = status_error;
    }
    return true;
}

static void on_configuration_updated()
{
    if (gw_offline_cfg && gw_offline_mon)
    {
        LOG(DEBUG, "offline_cfg: Feature and monitoring enabled. "
                    "Will read && store config after timeout if no additional config change.");

        /* On each OVSDB config change we're interested in, we restart the
         * current timer so that we read the whole configuration not at every
         * ovsdb monitor callback, but after a "cool down period". */
        ev_timer_stop(EV_DEFAULT, &timeout_no_cfg_change);
        ev_timer_set(&timeout_no_cfg_change, TIMEOUT_NO_CFG_CHANGE, 0.0);
        ev_timer_start(EV_DEFAULT, &timeout_no_cfg_change);
    }
    else
    {
        LOG(DEBUG, "offline_cfg: gw_offline_cfg=%d, gw_offline_mon=%d. Ignore this configuration update.",
                   gw_offline_cfg, gw_offline_mon);
    }
}

static void callback_Wifi_VIF_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_VIF_Config *old_rec,
        struct schema_Wifi_VIF_Config *config)
{
    on_configuration_updated();
}

static void callback_Wifi_Inet_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Inet_Config *old_rec,
        struct schema_Wifi_Inet_Config *config)
{
    on_configuration_updated();
}

static void callback_Wifi_Radio_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Radio_Config *old_rec,
        struct schema_Wifi_Radio_Config *config)
{
    on_configuration_updated();
}

static void callback_DHCP_reserved_IP(
        ovsdb_update_monitor_t *mon,
        struct schema_DHCP_reserved_IP *old_rec,
        struct schema_DHCP_reserved_IP *config)
{
    on_configuration_updated();
}

static bool gw_offline_enable_cfg_mon()
{
    static bool inited;

    if (inited)
        return true;

    OVSDB_TABLE_MONITOR(Wifi_VIF_Config, true);
    OVSDB_TABLE_MONITOR(Wifi_Inet_Config, true);
    OVSDB_TABLE_MONITOR(Wifi_Radio_Config, true);
    OVSDB_TABLE_MONITOR(DHCP_reserved_IP, true);

    ev_timer_init(&timeout_no_cfg_change, on_timeout_cfg_no_change, TIMEOUT_NO_CFG_CHANGE, 0.0);

    inited = true;
    return true;
}

static void callback_Node_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Node_Config *old_rec,
        struct schema_Node_Config *config)
{
    pjs_errmsg_t perr;
    json_t *row;

    if (mon->mon_type == OVSDB_UPDATE_ERROR)
        return;
    if (!(config->module_exists && strcmp(config->module, PM_MODULE_NAME) == 0))
        return;

    /* Enabling/Disabling the feature (Cloud): */
    if (strcmp(config->key, KEY_OFFLINE_CFG) == 0)
    {
        if (mon->mon_type == OVSDB_UPDATE_DEL)
            strcpy(config->value, VAL_OFFLINE_OFF);

        // Set enable/disable flag:
        if (strcmp(config->value, VAL_OFFLINE_ON) == 0)
        {
            gw_offline_cfg = true;
            LOG(INFO, "offline_cfg: Cloud enabled feature.");
        }
        else
        {
            gw_offline_cfg = false;
            LOG(INFO, "offline_cfg: Cloud disabled feature.");
        }

        // Remember enable/disable flag:
        if (config->persist_exists && config->persist)
        {
            row = schema_Node_Config_to_json(config, perr);
            if (row == NULL)
            {
                LOG(ERR, "offline_cfg: Error converting to json: %s", perr);
                return;
            }
            if (!gw_offline_ps_store(PS_KEY_OFFLINE_CFG, row))
                LOG(ERR, "offline_cfg: Error storing gw_offline_cfg flag to persistent storage");
            json_decref(row);
        }

        // Reflect enable/disable flag in Node_State:
        pm_node_state_set(KEY_OFFLINE_CFG, config->value);

        // Indicate ready if enabled and config already available:
        if (gw_offline_cfg)
        {
            if (pm_gw_offline_cfg_is_available())
                pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_READY);
            else
                pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ENABLED);
        }
        else
        {
            pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_DISABLED);
        }
    }

    /* Enabling/disabling config monitoring and storing (Cloud): */
    if (strcmp(config->key, KEY_OFFLINE_MON) == 0)
    {
        if (mon->mon_type == OVSDB_UPDATE_DEL)
            strcpy(config->value, VAL_OFFLINE_OFF);

        // Set enable/disable config monitoring flag:
        if (strcmp(config->value, VAL_OFFLINE_ON) == 0)
        {
            if (!gw_offline_cfg)
            {
                LOG(WARN, "offline_cfg: Cloud tried to enable config monitoring but feature is disabled. Ignoring.");
                return;
            }
            gw_offline_mon = true;

            // Do the initial read config and store to persistent storage:
            pm_gw_offline_read_and_store_config();

            // Enable monitoring of ovsdb config subset:
            gw_offline_enable_cfg_mon();

            LOG(INFO, "offline_cfg: Cloud enabled config monitoring");
        }
        else
        {
            gw_offline_mon = false;
            LOG(INFO, "offline_cfg: Cloud disabled config monitoring.");
        }
    }

    /* Triggering restoring of config from persistent storage (CM): */
    if (strcmp(config->key, KEY_OFFLINE) == 0)
    {
        if (strcmp(config->value, VAL_OFFLINE_ON) == 0
                && (mon->mon_type == OVSDB_UPDATE_NEW || mon->mon_type == OVSDB_UPDATE_MODIFY))
        {
            /* Enabled by CM when no Cloud/Internet connectivity and other
             * conditions are met for certain amount of time. */

            if (gw_offline_mon)
            {
                LOG(WARN, "offline_cfg: Offline config mode triggered, but config monitoring is enabled. Ignoring.");
                return;
            }
            if (gw_offline_stat != status_ready)
            {
                LOG(WARN, "offline_cfg: Offline config mode triggered, but gw_offline_status "\
                           "!= ready. status=%d. Ignoring", gw_offline_stat);
                return;
            }

            gw_offline = true;
            LOG(INFO, "offline_cfg: Offline config mode enabled. Load && apply persistent config triggered.");
            pm_gw_offline_load_and_apply_config();
        }
        else
        {
            if (!gw_offline)
            {
                LOG(WARN, "offline_cfg: Not in offline config mode and exit triggered. Ignoring.");
                return;
            }

            /* Disabled by CM when Cloud/Internet connectivity reestablished.
             *
             * In this case, PM does nothing -- Cloud will push proper ovsdb
             * config that should cleanly overwrite the applied persistent config.
             * If not, then restart of managers will be required.
             */
            gw_offline = false;
            pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_DISABLED);
            LOG(INFO, "offline_cfg: Offline config mode disabled.");
        }
    }
}

static const char *find_radio_if_name_for_vif_with_uuid(
        const struct gw_offline_cfg *cfg,
        const char *vif_str_uuid)
{
    size_t index;
    size_t index2;
    json_t *radio_row;
    json_t *a_vif_config;
    const char *radio_if_name = NULL;

    json_array_foreach(cfg->radio_config, index, radio_row)
    {
        json_t *vif_configs = json_object_get(radio_row, "vif_configs");

        json_array_foreach(json_array_get(vif_configs, 1), index2, a_vif_config)
        {
            const char *str_uuid = json_string_value(json_array_get(a_vif_config, 1));
            if (str_uuid != NULL)
            {
                if (strcmp(str_uuid, vif_str_uuid) == 0)
                {
                    /* For VIF with vif_str_uuid we've found the corresponding
                     * radio if_name: */
                    radio_if_name = json_string_value(json_object_get(radio_row, "if_name"));
                    return radio_if_name;
                }
            }
        }
    }
    return NULL;
}

/* In array 'radio_if_names' at index n set radio if_name for the corresponding
 * VIF at index n in array 'vif_config'. */
static bool gw_offline_cfg_set_radio_if_names(struct gw_offline_cfg *cfg)
{
    size_t index;
    json_t *vif_row;
    json_t *juuid = NULL;
    const char *vif_str_uuid = NULL;

    json_decref(cfg->radio_if_names);
    cfg->radio_if_names = json_array();

    // Traverse VIFs and find out their radio:
    json_array_foreach(cfg->vif_config, index, vif_row)
    {
        juuid = json_object_get(vif_row, "_uuid");
        vif_str_uuid = json_string_value(json_array_get(juuid, 1));

        const char *radio_if_name = find_radio_if_name_for_vif_with_uuid(cfg, vif_str_uuid);
        if (radio_if_name == NULL)
        {
            /* A valid VIF that is up should have a correspoinding radio that
             * is referencing that VIF with uuiid, so issue at least a warning. */
            LOG(WARN, "offline_cfg: NOT FOUND radio if_name for VIF with uuid=%s", vif_str_uuid);
            return false;
        }
        LOG(DEBUG, "offline_cfg: For VIF with uuid=%s found radio if_name=%s", vif_str_uuid, radio_if_name);
        json_array_append_new(cfg->radio_if_names, json_string(radio_if_name));
    }
    return true;
}

static void gw_offline_cfg_cleanup_radio_config(struct gw_offline_cfg *cfg)
{
    json_t *row;
    size_t index;

    json_array_foreach(cfg->radio_config, index, row)
    {
        // channel_mode: cloud --> manual
        json_object_set_new(row, "channel_mode", json_string("manual"));
        /*
         * Cleanup vif_configs (radio --> VIF uuids) references as these are
         * valid only for this runtime config and does not make sense storing
         * them to persistent storage.. When loading stored config,
         * these references will be setup according to state remembered in
         * 'radio_if_names'.
         */
        json_object_del(row, "vif_configs");
    }
}

/* Add uuid (of a VIF) to Wifi_Radio_Config's 'vif_configs' for radio 'if_name': */
static bool ovsdb_add_uuid_to_radio_config(const char *if_name, ovs_uuid_t uuid)
{
    json_t *row = NULL;
    int rv;

    LOG(DEBUG, "offline_cfg: Adding uuid=%s to Wifi_Radio_Config -where if_name==%s", uuid.uuid, if_name);

    row = json_pack("{ s : [ s, s ] }", "vif_configs", "uuid", uuid.uuid);
    if (row == NULL)
    {
        LOG(ERR, "offline_cfg: Error packing vif_configs json row");
        return false;
    }

    rv = ovsdb_sync_update("Wifi_Radio_Config", "if_name", if_name, row);
    if (rv != 1)
    {
        LOG(ERR, "offline_cfg: Error updating Wifi_Radio_Config: rv=%d", rv);
        return false;
    }
    return true;
}

/* Initiate this module. */
void pm_gw_offline_init(void)
{
    json_t *json_en = NULL;

    LOG(INFO, "offline_cfg: %s()", __func__);

    // Init OVSDB:
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);

    OVSDB_TABLE_INIT_NO_KEY(Node_State);
    OVSDB_TABLE_INIT(Wifi_VIF_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Inet_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_Radio_Config, if_name);
    OVSDB_TABLE_INIT(DHCP_reserved_IP, hw_addr);

    // Check feature enable flag (persistent):
    if (gw_offline_ps_load(PS_KEY_OFFLINE_CFG, &json_en) && json_en != NULL)
    {
        const char *en_value;
        if ((en_value = json_string_value(json_object_get(json_en, "value"))) != NULL)
        {
            if (strcmp(en_value, VAL_OFFLINE_ON) == 0)
            {
                gw_offline_cfg = true;
            }
        }
        json_decref(json_en);
    }

    if (gw_offline_cfg)
    {
        LOG(INFO, "offline_cfg: Feature enabled (flag set in persistent storage)");
        pm_node_state_set(KEY_OFFLINE_CFG, VAL_OFFLINE_ON);

        if (pm_gw_offline_cfg_is_available())
        {
            LOG(INFO, "offline_cfg: Config available in persistent storage.");
            // Indicate the feature is "ready" (enabled && persistent config available):
            pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_READY);
        }
        else
        {
            LOG(INFO, "offline_cfg: Config NOT available in persistent storage.");
            // Indicate the feature is "enabled" (but no persistent config available):
            pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ENABLED);
        }
    }
    else
    {
        LOG(DEBUG, "offline_cfg: Feature disabled (flag not set or not present in persistent storage)");
    }

    // Always install Node_Config monitor, other monitors installed when enabled.
    OVSDB_TABLE_MONITOR(Node_Config, true);
}

void pm_gw_offline_fini(void)
{
    LOG(INFO, "offline_cfg: %s()", __func__);
}

static bool gw_offline_ps_store(const char *ps_key, const json_t *config)
{
    ssize_t str_size;
    bool rv = false;
    char *config_str = NULL;
    osp_ps_t *ps = NULL;

    if (config == NULL)
        return true;

    ps = osp_ps_open(PS_STORE_GW_OFFLINE, OSP_PS_RDWR);
    if (ps == NULL)
    {
        LOG(ERR, "offline_cfg: Error opening %s persistent store.", PS_STORE_GW_OFFLINE);
        goto exit;
    }
    LOG(DEBUG, "offline_cfg: Persisten storage %s opened", PS_STORE_GW_OFFLINE);

    config_str = json_dumps(config, JSON_COMPACT);
    if (config_str == NULL)
    {
        LOG(ERR, "offline_cfg: Error converting %s JSON to string.", ps_key);
        goto exit;
    }

    str_size = (ssize_t)strlen(config_str) + 1;
    if (osp_ps_set(ps, ps_key, config_str, (size_t)str_size) < str_size)
    {
        LOG(ERR, "offline_cfg: Error storing %s to persistent storage.", ps_key);
        goto exit;
    }
    LOG(DEBUG, "offline_cfg: Stored %s to persistent storage %s.", ps_key, PS_STORE_GW_OFFLINE);

    rv = true;
exit:
    if (config_str != NULL) json_free(config_str);
    if (ps != NULL) osp_ps_close(ps);
    return rv;
}

/* Store config to persistent storage. */
static bool gw_offline_cfg_ps_store(const struct gw_offline_cfg *cfg)
{
    bool rv;

    if (!json_equal(cfg->vif_config, cfg_cache.vif_config))
    {
        rv = gw_offline_ps_store(PS_KEY_VIF_CONFIG, cfg->vif_config);
        if (!rv) goto exit;
        json_decref(cfg_cache.vif_config);
        cfg_cache.vif_config = json_incref(cfg->vif_config);
    } else LOG(DEBUG, "offline_cfg: vif_config: cached==stored. Skipped storing.");

    if (!json_equal(cfg->inet_config, cfg_cache.inet_config))
    {
        rv = gw_offline_ps_store(PS_KEY_INET_CONFIG, cfg->inet_config);
        if (!rv) goto exit;
        json_decref(cfg_cache.inet_config);
        cfg_cache.inet_config = json_incref(cfg->inet_config);
    } else LOG(DEBUG, "offline_cfg: inet_config: cached==stored. Skipped storing.");

    if (!json_equal(cfg->radio_config, cfg_cache.radio_config))
    {
        rv = gw_offline_ps_store(PS_KEY_RADIO_CONFIG, cfg->radio_config);
        if (!rv) goto exit;
        json_decref(cfg_cache.radio_config);
        cfg_cache.radio_config = json_incref(cfg->radio_config);
    } else LOG(DEBUG, "offline_cfg: radio_config: cached==stored. Skipped storing.");

    if (!json_equal(cfg->inet_config_home_aps, cfg_cache.inet_config_home_aps))
    {
        rv = gw_offline_ps_store(PS_KEY_INET_CONFIG_HOME_APS, cfg->inet_config_home_aps);
        if (!rv) goto exit;
        json_decref(cfg_cache.inet_config_home_aps);
        cfg_cache.inet_config_home_aps = json_incref(cfg->inet_config_home_aps);
    } else LOG(DEBUG, "offline_cfg: inet_config_home_aps: cached==stored. Skipped storing.");

    if (!json_equal(cfg->radio_if_names, cfg_cache.radio_if_names))
    {
        rv = gw_offline_ps_store(PS_KEY_RADIO_IF_NAMES, cfg->radio_if_names);
        if (!rv) goto exit;
        json_decref(cfg_cache.radio_if_names);
        cfg_cache.radio_if_names = json_incref(cfg->radio_if_names);
    } else LOG(DEBUG, "offline_cfg: radio_if_names: cached==stored. Skipped storing.");

    if (!json_equal(cfg->dhcp_reserved_ip, cfg_cache.dhcp_reserved_ip))
    {
        rv = gw_offline_ps_store(PS_KEY_DHCP_RESERVED_IP, cfg->dhcp_reserved_ip);
        if (!rv) goto exit;
        json_decref(cfg_cache.dhcp_reserved_ip);
        cfg_cache.dhcp_reserved_ip = json_incref(cfg->dhcp_reserved_ip);
    } else LOG(DEBUG, "offline_cfg: dhcp_reserved_ip: cached==stored. Skipped storing.");

exit:
    return rv;
}

static bool gw_offline_ps_load(const char *ps_key, json_t **config)
{
    ssize_t str_size;
    bool rv = false;
    char *config_str = NULL;
    json_t *config_json = NULL;
    osp_ps_t *ps = NULL;

    ps = osp_ps_open(PS_STORE_GW_OFFLINE, OSP_PS_RDWR);
    if (ps == NULL)
    {
        LOG(DEBUG, "offline_cfg: Failed opening %s persistent store. It may not exist yet.",
                PS_STORE_GW_OFFLINE);
        goto exit;
    }
    LOG(DEBUG, "offline_cfg: Persisten storage %s opened", PS_STORE_GW_OFFLINE);

    str_size = osp_ps_get(ps, ps_key, NULL, 0);
    if (str_size < 0)
    {
        LOG(ERR, "offline_cfg: Error fetching %s key size.", ps_key);
        goto exit;
    }
    else if (str_size == 0)
    {
        LOG(DEBUG, "offline_cfg: Read 0 bytes for %s from persistent storage. The record does not exist yet.", ps_key);
        rv = true;
        goto exit;
    }

    /* Fetch the "config" data */
    config_str = malloc((size_t)str_size);
    if (osp_ps_get(ps, ps_key, config_str, (size_t)str_size) != str_size)
    {
        LOG(ERR, "offline_cfg: Error retrieving persistent %s key.", ps_key);
        goto exit;
    }
    LOG(DEBUG, "offline_cfg: Loaded %s string from persistent storage. str=%s", ps_key, config_str);

    /* Convert it to JSON */
    config_json = json_loads(config_str, 0, NULL);
    if (config_json == NULL)
    {
        LOG(ERR, "offline_cfg: Error parsing JSON: %s", config_str);
        goto exit;
    }
    LOG(DEBUG, "offline_cfg: Loaded %s json from persistent storage %s.", ps_key, PS_STORE_GW_OFFLINE);

    *config = config_json;
    rv = true;
exit:
    if (config_str != NULL) free(config_str);
    if (ps != NULL) osp_ps_close(ps);

    return rv;
}

/* Load config from persistent storage. */
static bool gw_offline_cfg_ps_load(struct gw_offline_cfg *cfg)
{
    bool rv = false;

    rv = gw_offline_ps_load(PS_KEY_VIF_CONFIG, &cfg->vif_config);
    if (!rv) goto exit;
    json_decref(cfg_cache.vif_config);
    cfg_cache.vif_config = json_incref(cfg->vif_config);

    rv = gw_offline_ps_load(PS_KEY_INET_CONFIG, &cfg->inet_config);
    if (!rv) goto exit;
    json_decref(cfg_cache.inet_config);
    cfg_cache.inet_config = json_incref(cfg->inet_config);

    rv = gw_offline_ps_load(PS_KEY_RADIO_CONFIG, &cfg->radio_config);
    if (!rv) goto exit;
    json_decref(cfg_cache.radio_config);
    cfg_cache.radio_config = json_incref(cfg->radio_config);

    rv = gw_offline_ps_load(PS_KEY_INET_CONFIG_HOME_APS, &cfg->inet_config_home_aps);
    if (!rv) goto exit;
    json_decref(cfg_cache.inet_config_home_aps);
    cfg_cache.inet_config_home_aps = json_incref(cfg->inet_config_home_aps);

    rv = gw_offline_ps_load(PS_KEY_RADIO_IF_NAMES, &cfg->radio_if_names);
    if (!rv) goto exit;
    json_decref(cfg_cache.radio_if_names);
    cfg_cache.radio_if_names = json_incref(cfg->radio_if_names);

    rv = gw_offline_ps_load(PS_KEY_DHCP_RESERVED_IP, &cfg->dhcp_reserved_ip);
    if (!rv) goto exit;
    json_decref(cfg_cache.dhcp_reserved_ip);
    cfg_cache.dhcp_reserved_ip = json_incref(cfg->dhcp_reserved_ip);

exit:
    return rv;
}

/* Read the current subset of OVSDB config. */
static bool gw_offline_cfg_ovsdb_read(struct gw_offline_cfg *cfg)
{
    size_t index;
    json_t *json_res;
    json_t *row;

    memset(cfg, 0, sizeof(*cfg));

    /* Select home AP VIFs from Wifi_VIF_Config: */
    cfg->vif_config = ovsdb_sync_select("Wifi_VIF_Config", "bridge", LAN_BRIDGE);
    if (cfg->vif_config == NULL)
    {
        LOG(ERR, "offline_cfg: Error selecting from Wifi_VIF_Config");
        goto exit_failure;
    }

    /* For each home AP: find a corresponding entry in Wifi_Inet_Config: */
    json_array_foreach(cfg->vif_config, index, row)
    {
        const char *if_name = json_string_value(json_object_get(row, "if_name"));

        json_res = ovsdb_sync_select("Wifi_Inet_Config", "if_name", if_name);
        if (json_res == NULL || json_array_size(json_res) != 1)
        {
            LOG(ERR, "offline_cfg: Error selecting from Wifi_Inet_Config");
            goto exit_failure;
        }
        if (cfg->inet_config_home_aps == NULL)
            cfg->inet_config_home_aps = json_array();

        json_array_append(cfg->inet_config_home_aps, json_array_get(json_res, 0));
        json_decref(json_res);
    }

    /* lan bridge config from Wifi_Inet_Config: */
    cfg->inet_config = ovsdb_sync_select("Wifi_Inet_Config", "if_name", LAN_BRIDGE);
    if (cfg->inet_config == NULL)
    {
        LOG(ERR, "offline_cfg: Error selecting from Wifi_Inet_Config");
        goto exit_failure;
    }

    /* Remember radio config: */
    cfg->radio_config = ovsdb_sync_select_where("Wifi_Radio_Config", NULL);
    if (cfg->radio_config == NULL)
    {
        LOG(ERR, "offline_cfg: Error selecting from Wifi_Radio_Config");
        goto exit_failure;
    }

    /* Determine and save radio if_names for VIFs: */
    gw_offline_cfg_set_radio_if_names(cfg);

    /* Cleanup values in Wifi_Radio_Config that should not be stored: */
    gw_offline_cfg_cleanup_radio_config(cfg);

    /* DHCP reservations: */
    cfg->dhcp_reserved_ip = ovsdb_sync_select_where("DHCP_reserved_IP", NULL);
    if (cfg->dhcp_reserved_ip == NULL)
    {
        LOG(DEBUG, "offline_cfg: DHCP_reserved_IP: NO rows in the table or error.");
        cfg->dhcp_reserved_ip = json_array();
    }

    /* Delete special ovsdb keys like _uuid, etc, these should not be stored: */
    gw_offline_cfg_delete_special_keys(cfg);

    return true;
exit_failure:
    gw_offline_cfg_release(cfg);
    return false;
}

/* Apply the provided subset of config (obtained from persistent storage) to OVSDB. */
static bool gw_offline_cfg_ovsdb_apply(const struct gw_offline_cfg *cfg)
{
    ovs_uuid_t uuid;
    size_t index;
    int rc;
    json_t *row;

    /* Delete all rows in Wifi_VIF_Config to start clean: */
    rc = ovsdb_sync_delete_where("Wifi_VIF_Config", NULL);
    if (rc == -1)
    {
        LOG(ERR, "offline_cfg: Error deleting Wifi_VIF_Config");
        return false;
    }

    /* Update inet config: */
    if ((rc = ovsdb_sync_update("Wifi_Inet_Config", "if_name", LAN_BRIDGE,
                      json_incref(json_array_get(cfg->inet_config, 0)))) != 1)
    {
        LOG(ERR, "offline_cfg: Error updating Wifi_Inet_Config, rc=%d", rc);
        return false;
    }
    LOG(DEBUG, "offline_cfg: Updated %d row(s) in Wifi_Inet_Config", rc);

    /* Update radio config: */
    json_array_foreach(cfg->radio_config, index, row)
    {
        const char *if_name = json_string_value(json_object_get(row, "if_name"));

        if (ovsdb_sync_update("Wifi_Radio_Config", "if_name", if_name, json_incref(row)) == -1)
        {
            LOG(ERR, "offline_cfg: Error updating Wifi_Radio_Config");
            return false;
        }
    }

    /* Create VIF interfaces... */
    json_array_foreach(cfg->vif_config, index, row)
    {
        char cmd_vif_to_br[128];
        const char *if_name = json_string_value(json_object_get(row, "if_name"));

        /* json object might get modified later, localy remember this if_name
         * and already construct a shell command to be used later: */
        snprintf(cmd_vif_to_br, sizeof(cmd_vif_to_br),
                 "ovs-vsctl add-port %s %s", LAN_BRIDGE, if_name);

        /* Insert a row for this VIF: */
        if (ovsdb_sync_insert("Wifi_VIF_Config", json_incref(row), &uuid))
        {
            /* Add created vif uuid to Wifi_Radio_Config... */
            const char *radio_if_name = json_string_value(json_array_get(cfg->radio_if_names, index));
            LOG(DEBUG, "offline_cfg:   if_name=%s is at radio_if_name=%s", if_name, radio_if_name);
            ovsdb_add_uuid_to_radio_config(radio_if_name, uuid);

            /* Add this home VIF interface to lan bridge: */
            rc = cmd_log(cmd_vif_to_br);
            if (rc == 0)
                LOG(INFO, "offline_cfg: ovs-vsctl: added %s to %s", if_name, LAN_BRIDGE);
            else
                LOG(ERR, "offline_cfg: ovs-vsctl: Error adding %s to %s", if_name, LAN_BRIDGE);
        }
        else
        {
            LOG(ERR, "offline_cfg: Error inserting into Wifi_VIF_Config: row=%s", json_dumps_static(row, 0));
            return false;
        }
    }

    /* Add home-aps entries to Wifi_Inet_Config... */
    json_array_foreach(cfg->inet_config_home_aps, index, row)
    {
        const char *if_name = json_string_value(json_object_get(row, "if_name"));

        if (!ovsdb_sync_upsert("Wifi_Inet_Config", "if_name", if_name, json_incref(row), NULL))
        {
            LOG(ERR, "offline_cfg: Error inserting into Wifi_Inet_Config: row=%s", json_dumps_static(row, 0));
            return false;
        }
    }

    /* Configure DHCP reservations: */
    rc = ovsdb_sync_delete_where("DHCP_reserved_IP", NULL);
    if (rc == -1)
    {
        LOG(ERR, "offline_cfg: Error deleting DHCP_reserved_IP");
        return false;
    }
    json_array_foreach(cfg->dhcp_reserved_ip, index, row)
    {
        if (!ovsdb_sync_insert("DHCP_reserved_IP", json_incref(row), NULL))
        {
            LOG(ERR, "offline_cfg: Error inserting into DHCP_reserved_IP: row=%s", json_dumps_static(row, 0));
            return false;
        }
    }

    return true;
}

/* Get uplink interface name */
static bool gw_offline_uplink_ifname_get(char *if_name_buf, size_t len)
{
    const char *if_name;
    json_t *json;
    bool rv = false;

    json = ovsdb_sync_select_where("Connection_Manager_Uplink",
                       ovsdb_where_simple_typed("is_used", "true", OCLM_BOOL));

    if (json == NULL || json_array_size(json) != 1)
    {
        LOG(WARN, "offline_cfg: Cannot determine GW's uplink interface");
        goto err_out;
    }

    if_name = json_string_value(json_object_get(json_array_get(json, 0), "if_name"));
    if (if_name != NULL)
    {
        LOG(INFO, "offline_cfg: This GW's uplink interface=%s", if_name);
        strscpy(if_name_buf, if_name, len);
    }

    rv = true;
err_out:
    json_decref(json);
    return rv;
}

/* Check if interface has IP assigned */
static bool gw_offline_intf_is_ip_set(const char *if_name)
{
    json_t *json;
    json_t *inet_addr;
    bool rv = false;

    json = ovsdb_sync_select("Wifi_Inet_State", "if_name", if_name);
    inet_addr = json_object_get(json_array_get(json, 0), "inet_addr");
    LOG(DEBUG, "offline_cfg: if_name=%s, IP=%s", if_name, json_string_value(inet_addr));

    if (inet_addr != NULL && strcmp(json_string_value(inet_addr), "0.0.0.0") != 0)
        rv = true;

    json_decref(json);
    return rv;
}

/* Set specified uplink to LAN_BRIDGE */
static bool gw_offline_uplink_set_lan_bridge(const char *uplink)
{
    json_t *row;
    int rv;

    row = json_pack("{ s : s }", "bridge", LAN_BRIDGE);
    if (row == NULL)
    {
        LOG(ERR, "offline_cfg: %s: Error packing json", __func__);
        return false;
    }
    rv = ovsdb_sync_update("Connection_Manager_Uplink", "if_name", uplink, row);
    if (rv != 1)
    {
        LOG(ERR, "offline_cfg: %s: Error updating Connection_Manager_Uplink: rv=%d",
                __func__, rv);
        return false;
    }
    return true;
}

/* Determine uplink and set uplink bridge to LAN_BRIDGE */
static bool gw_offline_uplink_bridge_set(const struct gw_offline_cfg *cfg)
{
    char uplink[32];

    // Config must be BRIDGE config, otherwise ignore:
    if (!gw_offline_cfg_is_bridge(cfg))
        return false;

    // Determine uplink interface name:
    if (!gw_offline_uplink_ifname_get(uplink, sizeof(uplink)))
        return false;

    // Check if uplink interface has IP assigned:
    if (!gw_offline_intf_is_ip_set(uplink))
        return false;

    // Set uplink to LAN_BRIDGE:
    if (!gw_offline_uplink_set_lan_bridge(uplink))
        return false;

    return true;
}

/* Is persistent-storage config available? */
bool pm_gw_offline_cfg_is_available()
{
    struct gw_offline_cfg gw_cfg = { 0 };
    bool avail = false;
    bool rv;

    rv = gw_offline_cfg_ps_load(&gw_cfg);
    if (!rv) goto exit;
    do
    {
        // The following have to be present to declare availability:
        if (gw_cfg.vif_config == NULL || json_array_size(gw_cfg.vif_config) == 0)
            break;
        if (gw_cfg.inet_config == NULL || json_array_size(gw_cfg.inet_config) == 0)
            break;
        if (gw_cfg.radio_config == NULL || json_array_size(gw_cfg.radio_config) == 0)
            break;
        if (gw_cfg.inet_config_home_aps == NULL || json_array_size(gw_cfg.inet_config_home_aps) == 0)
            break;
        if (gw_cfg.radio_if_names == NULL || json_array_size(gw_cfg.radio_if_names) == 0)
            break;

        avail = true;
    } while (0);

exit:
    gw_offline_cfg_release(&gw_cfg);
    return avail;
}

/* Read current subset of OVSDB config and store it to persistent storage. */
bool pm_gw_offline_read_and_store_config()
{
    struct gw_offline_cfg gw_cfg = { 0 };
    bool rv = false;

    if (!(gw_offline_cfg && gw_offline_mon))
    {
        LOG(WARN, "offline_cfg: %s() called, but gw_offline_cfg=%d, gw_offline_mon=%d. Ignoring.",
                __func__, gw_offline_cfg, gw_offline_mon);
        return false;
    }

    /* Read subset of current ovsdb config: */
    if (!gw_offline_cfg_ovsdb_read(&gw_cfg))
    {
        LOG(ERR, "offline_cfg: Error reading current config.");
        goto exit;
    }
    LOG(INFO, "offline_cfg: Read config from OVSDB.");

    /* Store the config to persistent storage: */
    if (!gw_offline_cfg_ps_store(&gw_cfg))
    {
        LOG(ERR, "offline_cfg: Error storing current config.");
        goto exit;
    }
    LOG(INFO, "offline_cfg: Stored current config to persistent storage.");

    rv = true;
exit:
    /* Indicate to CM that peristent storage is "ready":  */
    if (rv)
        pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_READY);
    else
        pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ERROR);

    gw_offline_cfg_release(&gw_cfg);
    return rv;
}

/* Load persistent storage config and apply config to OVSDB. */
bool pm_gw_offline_load_and_apply_config()
{
    struct gw_offline_cfg gw_cfg = { 0 };
    bool rv = false;

    if (!(gw_offline_cfg && gw_offline))
    {
        LOG(WARN, "offline_cfg: %s() should only be triggered (by CM) via Node_Config "
                "(when the feature is enabled). Ignoring.", __func__);
        return false;
    }

    /* Load config from persistent storage: */
    if (!gw_offline_cfg_ps_load(&gw_cfg))
    {
        LOG(ERR, "offline_cfg: Error loading config from persistent storage.");
        goto exit;
    }
    LOG(INFO, "offline_cfg: Loaded stored config from persistent storage.");

    /* Apply the stored config to OVSDB: */
    if (!gw_offline_cfg_ovsdb_apply(&gw_cfg))
    {
        LOG(ERR, "offline_cfg: Error applying stored config to OVSDB.");
        goto exit;
    }
    LOG(INFO, "offline_cfg: Applied config to OVSDB.");

    if (gw_offline_cfg_is_bridge(&gw_cfg))
    {
        LOG(DEBUG, "offline_cfg: This is BRIDGE config, will set uplink to LAN_BRIDGE (%s)", LAN_BRIDGE);
        if (!gw_offline_uplink_bridge_set(&gw_cfg))
        {   LOG(ERR, "offline_cfg: Error seting uplink to LAN_BRIDGE (%s)", LAN_BRIDGE);
            goto exit;
        }
        LOG(INFO, "offline_cfg: Uplink set to LAN_BRIDGE (%s)", LAN_BRIDGE);
    }

    rv = true;
exit:
    gw_offline_cfg_release(&gw_cfg);

    if (rv)
    {
        /* Indicate in Node_State that the feature is "active"
         * (enabled && ps config applied): */
        pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ACTIVE);
    }
    else
    {
        /* Indicate error in Node_State: */
        pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ERROR);
    }

    return rv;
}
