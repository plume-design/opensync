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
#include <string.h>
#include <jansson.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "module.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "schema.h"
#include "json_util.h"
#include "const.h"
#include "util.h"
#include "log.h"
#include "osp_ps.h"
#include "os.h"
#include "memutil.h"
#include "kconfig.h"


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
#define PS_KEY_INET_CONFIG_UPLINK   "inet_config_uplink"
#define PS_KEY_RADIO_CONFIG         "radio_config"
#define PS_KEY_INET_CONFIG_HOME_APS "inet_config_home_aps"
#define PS_KEY_RADIO_IF_NAMES       "radio_if_names"
#define PS_KEY_DHCP_RESERVED_IP     "dhcp_reserved_ip"
#define PS_KEY_OF_CONFIG            "openflow_config"
#define PS_KEY_OF_TAG               "openflow_tag"
#define PS_KEY_OF_TAG_GROUP         "openflow_tag_group"
#define PS_KEY_IPV6_CFG             "ipv6_cfg"
#define PS_KEY_INET_CONFIG_VLAN     "inet_config_vlan"
#define PS_KEY_IGMP_CONFIG          "igmp_config"
#define PS_KEY_MLD_CONFIG           "mld_config"
#define PS_KEY_ROUTE_CONFIG         "route_config"
#define PS_KEY_PORT_MCAST           "port_mcast"
#define PS_KEY_PORT_LANBR           "port_lanbridge"
#define PS_KEY_PORT_FRONTHAULS      "port_fronthauls"
#define PS_KEY_INTF_FRONTHAULS      "interface_fronthauls"
#define PS_KEY_NETFILTER            "netfilter"

#define PS_KEY_OFFLINE_CFG          KEY_OFFLINE_CFG

#define TIMEOUT_NO_CFG_CHANGE      15
#define DEBOUNCE_DHCP_OPTION       4

#if defined(CONFIG_TARGET_LAN_BRIDGE_NAME)
#define LAN_BRIDGE   CONFIG_TARGET_LAN_BRIDGE_NAME
#else
#define LAN_BRIDGE   SCHEMA_CONSTS_BR_NAME_HOME
#endif

/*
 * Generate the PJS structure struct gw_offline_ipv6_cfg
 * for json convert functions:
 */
#include "pm_gw_offline_cfg_pjs.h"
#include "pjs_gen_h.h"

#include "pm_gw_offline_cfg_pjs.h"
#include "pjs_gen_c.h"

struct gw_offline_cfg
{
    json_t *vif_config;
    json_t *inet_config;
    json_t *inet_config_uplink;
    json_t *radio_config;
    json_t *inet_config_home_aps;
    json_t *radio_if_names;
    json_t *dhcp_reserved_ip;
    json_t *openflow_config;
    json_t *openflow_tag;
    json_t *openflow_tag_group;
    json_t *ipv6_cfg;
    json_t *inet_config_vlan;
    json_t *igmp_config;
    json_t *mld_config;
    json_t *route_config;
    json_t *port_mcast;
    json_t *port_lanbridge;
    json_t *port_fronthauls;
    json_t *interface_fronthauls;
    json_t *netfilter;
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
static ovsdb_table_t table_Connection_Manager_Uplink;
static ovsdb_table_t table_Openflow_Config;
static ovsdb_table_t table_Openflow_Tag;
static ovsdb_table_t table_Openflow_Tag_Group;
static ovsdb_table_t table_IPv6_Address;
static ovsdb_table_t table_DHCPv6_Server;
static ovsdb_table_t table_IPv6_RouteAdv;
static ovsdb_table_t table_IPv6_Prefix;
static ovsdb_table_t table_DHCP_Option;
static ovsdb_table_t table_IP_Interface;
static ovsdb_table_t table_IGMP_Config;
static ovsdb_table_t table_MLD_Config;
static ovsdb_table_t table_Wifi_Route_Config;
static ovsdb_table_t table_Bridge;
static ovsdb_table_t table_Port;
static ovsdb_table_t table_Interface;
static ovsdb_table_t table_Netfilter;

static ev_timer timeout_no_cfg_change;
static ev_timer debounce_dco;

static struct gw_offline_cfg cfg_cache;

static bool gw_offline_cfg;
static bool gw_offline_mon;
static bool gw_offline;
static enum gw_offline_stat gw_offline_stat;

static bool gw_offline_ps_store(const char *ps_key, const json_t *config);
static bool gw_offline_cfg_ps_store(const struct gw_offline_cfg *cfg);
static bool gw_offline_ps_load(const char *ps_key, json_t **config);
static bool gw_offline_cfg_ps_load(struct gw_offline_cfg *cfg);
static bool gw_offline_ps_erase(void);

static bool gw_offline_cfg_ovsdb_read(struct gw_offline_cfg *cfg);
static bool gw_offline_cfg_ovsdb_apply(const struct gw_offline_cfg *cfg);
static bool gw_offline_cfg_ipv6_provision(const struct gw_offline_cfg *cfg);
static bool gw_offline_cfg_home_restore_home_bridge(const struct gw_offline_cfg *cfg);

static bool gw_offline_uplink_bridge_set(const struct gw_offline_cfg *cfg);
static bool gw_offline_uplink_ifname_get(char *if_name_buf, size_t len);
static bool gw_offline_uplink_config_set_current(const struct gw_offline_cfg *cfg);
static bool gw_offline_uplink_config_clear_previous(const char *if_name);

static json_t *util_vif_get_mld_ifname(const char *vif_if_name);
static bool util_array_has_json_with_if_name(const json_t *array, const char *if_name);

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

static void delete_ovsdb_column(json_t *rows, const char *column)
{
    size_t index;
    json_t *row;

    json_array_foreach(rows, index, row)
    {
        json_object_del(row, column);
    }
}

static void gw_offline_cfg_release(struct gw_offline_cfg *cfg)
{
    json_decref(cfg->vif_config);
    json_decref(cfg->inet_config);
    json_decref(cfg->inet_config_uplink);
    json_decref(cfg->radio_config);
    json_decref(cfg->inet_config_home_aps);
    json_decref(cfg->radio_if_names);
    json_decref(cfg->dhcp_reserved_ip);
    json_decref(cfg->openflow_config);
    json_decref(cfg->openflow_tag);
    json_decref(cfg->openflow_tag_group);
    json_decref(cfg->ipv6_cfg);
    json_decref(cfg->inet_config_vlan);
    json_decref(cfg->igmp_config);
    json_decref(cfg->mld_config);
    json_decref(cfg->route_config);
    json_decref(cfg->port_mcast);
    json_decref(cfg->port_lanbridge);
    json_decref(cfg->port_fronthauls);
    json_decref(cfg->interface_fronthauls);
    json_decref(cfg->netfilter);

    memset(cfg, 0, sizeof(*cfg));
}

static void gw_offline_cfg_delete_special_keys(struct gw_offline_cfg *cfg)
{
    delete_special_ovsdb_keys(cfg->vif_config);
    delete_special_ovsdb_keys(cfg->inet_config);
    delete_special_ovsdb_keys(cfg->inet_config_uplink);
    delete_special_ovsdb_keys(cfg->radio_config);
    delete_special_ovsdb_keys(cfg->inet_config_home_aps);
    delete_special_ovsdb_keys(cfg->dhcp_reserved_ip);
    delete_special_ovsdb_keys(cfg->openflow_config);
    delete_special_ovsdb_keys(cfg->openflow_tag);
    delete_special_ovsdb_keys(cfg->openflow_tag_group);
    delete_special_ovsdb_keys(cfg->ipv6_cfg);
    delete_special_ovsdb_keys(cfg->inet_config_vlan);
    delete_special_ovsdb_keys(cfg->igmp_config);
    delete_special_ovsdb_keys(cfg->mld_config);
    delete_special_ovsdb_keys(cfg->route_config);
    delete_special_ovsdb_keys(cfg->port_mcast);
    delete_special_ovsdb_keys(cfg->port_lanbridge);
    delete_special_ovsdb_keys(cfg->port_fronthauls);
    delete_special_ovsdb_keys(cfg->interface_fronthauls);
    delete_special_ovsdb_keys(cfg->netfilter);
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
        LOG(DEBUG, "offline_cfg: %s() called, but gw_offline_cfg=%d, gw_offline_mon=%d. Ignoring",
                __func__, gw_offline_cfg, gw_offline_mon);
        return;
    }

    pm_gw_offline_read_and_store_config();
}

static bool pm_node_config_set(const char *key, const char *value, bool persist)
{
    struct schema_Node_Config node_config;
    json_t *where;

    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("module", OFUNC_EQ, PM_MODULE_NAME));
    json_array_append_new(where, ovsdb_tran_cond_single("key", OFUNC_EQ, (char *)key));

    MEMZERO(node_config);
    SCHEMA_SET_STR(node_config.module, PM_MODULE_NAME);
    SCHEMA_SET_STR(node_config.key, key);

    if (value != NULL)
    {
        SCHEMA_SET_STR(node_config.value, value);
        if (persist)
            SCHEMA_SET_BOOL(node_config.persist, persist);

        ovsdb_table_upsert_where(&table_Node_Config, where, &node_config, false);
    }
    else
    {
        ovsdb_table_delete_where(&table_Node_Config, where);
    }
    return true;
}

static bool pm_node_state_set(const char *key, const char *value, bool persist)
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
        if (persist)
            SCHEMA_SET_BOOL(node_state.persist, persist);

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

static void callback_Openflow_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Openflow_Config *old_rec,
        struct schema_Openflow_Config *config)
{
    on_configuration_updated();
}

static void callback_Openflow_Tag(
        ovsdb_update_monitor_t *mon,
        struct schema_Openflow_Tag *old_rec,
        struct schema_Openflow_Tag *config)
{
    on_configuration_updated();
}

static void callback_Openflow_Tag_Group(
        ovsdb_update_monitor_t *mon,
        struct schema_Openflow_Tag_Group *old_rec,
        struct schema_Openflow_Tag_Group *config)
{
    on_configuration_updated();
}

static void callback_DHCPv6_Server(
        ovsdb_update_monitor_t *mon,
        struct schema_DHCPv6_Server *old_rec,
        struct schema_DHCPv6_Server *config)
{
    on_configuration_updated();
}

static void callback_IPv6_RouteAdv(
        ovsdb_update_monitor_t *mon,
        struct schema_IPv6_RouteAdv *old_rec,
        struct schema_IPv6_RouteAdv *config)
{
    on_configuration_updated();
}

static void callback_IPv6_Prefix(
        ovsdb_update_monitor_t *mon,
        struct schema_IPv6_Prefix *old_rec,
        struct schema_IPv6_Prefix *config)
{
    on_configuration_updated();
}

static void callback_IGMP_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_IGMP_Config *old_rec,
        struct schema_IGMP_Config *config)
{
    on_configuration_updated();
}

static void callback_MLD_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_MLD_Config *old_rec,
        struct schema_MLD_Config *config)
{
    on_configuration_updated();
}

static void callback_Wifi_Route_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Route_Config *old_rec,
        struct schema_Wifi_Route_Config *config)
{
    on_configuration_updated();
}

static void callback_Netfilter(
        ovsdb_update_monitor_t *mon,
        struct schema_Netfilter *old_rec,
        struct schema_Netfilter *config)
{
    on_configuration_updated();
}

static void callback_Port(
        ovsdb_update_monitor_t *mon,
        struct schema_Port *old_rec,
        struct schema_Port *config)
{
    on_configuration_updated();
}

static void callback_Interface(
        ovsdb_update_monitor_t *mon,
        struct schema_Interface *old_rec,
        struct schema_Interface *config)
{
    on_configuration_updated();
}

static bool gw_offline_enable_cfg_mon()
{
    static bool inited;

    if (inited)
    {
        on_configuration_updated();
        return true;
    }

    OVSDB_TABLE_MONITOR(Wifi_VIF_Config, true);
    OVSDB_TABLE_MONITOR(Wifi_Inet_Config, true);
    OVSDB_TABLE_MONITOR(Wifi_Radio_Config, true);
    OVSDB_TABLE_MONITOR(DHCP_reserved_IP, true);
    OVSDB_TABLE_MONITOR(Openflow_Config, true);
    OVSDB_TABLE_MONITOR(Openflow_Tag, true);
    OVSDB_TABLE_MONITOR(Openflow_Tag_Group, true);

    OVSDB_TABLE_MONITOR(DHCPv6_Server, true);
    OVSDB_TABLE_MONITOR(IPv6_RouteAdv, true);
    OVSDB_TABLE_MONITOR(IPv6_Prefix, true);

    OVSDB_TABLE_MONITOR(IGMP_Config, true);
    OVSDB_TABLE_MONITOR(MLD_Config, true);
    OVSDB_TABLE_MONITOR(Wifi_Route_Config, true);

    OVSDB_TABLE_MONITOR(Port, true);
    OVSDB_TABLE_MONITOR(Interface, true);

    OVSDB_TABLE_MONITOR(Netfilter, true);

    ev_timer_init(&timeout_no_cfg_change, on_timeout_cfg_no_change, TIMEOUT_NO_CFG_CHANGE, 0.0);

    inited = true;
    return true;
}

static bool is_eth_type(const char *if_type)
{
    return (strcmp(if_type, "eth") == 0);
}

static bool insert_port_into_bridge(char *bridge, char *port)
{
    char command[512];

    if (!is_input_shell_safe(port)) return false;

    snprintf(command, sizeof(command),
             "brctl show %s | grep %s || brctl addif %s %s",
             LAN_BRIDGE, port, LAN_BRIDGE, port);

    LOG(DEBUG, "offline_cfg: Insert port into bridge, running cmd: %s", command);
    return (cmd_log(command) == 0);
}

static void gw_offline_handle_eth_clients(
        ovsdb_update_monitor_t *mon,
        struct schema_Connection_Manager_Uplink *old_rec,
        struct schema_Connection_Manager_Uplink *link)
{
    if (gw_offline_stat != status_active)  // Ignore if not in active gw_offline mode
        return;
    if (link->is_used)                     // Ignore if this is a known uplink
        return;
    if (!is_eth_type(link->if_type))       // Ignore if this is not eth interface
        return;


    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2))
            || ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3))
            || ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, loop))
            || ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, eth_client)))

    {
        /* For non-uplink in the active gw_offline mode handle an
         * ethernet client (add port to bridge): */
        if (is_eth_type(link->if_type)
                && link->has_L2_exists && link->has_L2
                && link->has_L3_exists && !link->has_L3
                && link->loop_exists && !link->loop
                && link->eth_client_exists && link->eth_client)
        {
            LOG(NOTICE, "offline_cfg: eth-client detected. Inserting port %s into lan bridge %s",
                    link->if_name, LAN_BRIDGE);

            if (!insert_port_into_bridge(LAN_BRIDGE, link->if_name))
                LOG(ERR, "offline_cfg: Error inserting port into bridge");
        }
    }
}

static void gw_offline_handle_uplink_change(
        ovsdb_update_monitor_t *mon,
        struct schema_Connection_Manager_Uplink *old_rec,
        struct schema_Connection_Manager_Uplink *link)
{
    bool rv;

    if (gw_offline_stat != status_active)  // Ignore if not in active gw_offline mode
        return;
    if (!is_eth_type(link->if_type))       // Ignore if this is not eth interface
        return;

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, is_used)))
    {
        if (link->is_used_exists && link->is_used)
        {
            LOG(INFO, "offline_cfg: New uplink detected: %s. Will restore saved uplink config",
                    link->if_name);

            /* Restore saved uplink config (from saved config cache since we're
             * in active gw_offline mode) to current uplink: */
            rv = gw_offline_uplink_config_set_current(&cfg_cache);
            if (!rv)
            {
                LOG(ERR, "offline_cfg: Error setting current uplink config");
            }
        }
        else
        {
            LOG(INFO, "offline_cfg: No longer uplink: %s. Will clear any uplink config previously set",
                    link->if_name);

            rv = gw_offline_uplink_config_clear_previous(link->if_name);
            if (!rv)
            {
                LOG(ERR, "offline_cfg: Error clearing previous uplink config");
            }
        }
    }
}

static void callback_Connection_Manager_Uplink(
        ovsdb_update_monitor_t *mon,
        struct schema_Connection_Manager_Uplink *old_rec,
        struct schema_Connection_Manager_Uplink *link)
{
    gw_offline_handle_eth_clients(mon, old_rec, link);

    gw_offline_handle_uplink_change(mon, old_rec, link);
}

static bool gw_offline_enable_eth_clients_handling()
{
    static bool inited;

    if (inited)
        return true;

    OVSDB_TABLE_MONITOR(Connection_Manager_Uplink, false);

    inited = true;
    return true;
}

static bool gw_offline_openflow_set_offline_mode(bool offline_mode)
{
    if (offline_mode)
    {
        /* In offline mode the "offline_mode" tag should be configured to
         * contain a single space string: */
        json_t *device_value = json_pack("{ s : s, s : s }",
                "name", "offline_mode", "device_value", " ");

        ovsdb_sync_upsert(
                SCHEMA_TABLE(Openflow_Tag), SCHEMA_COLUMN(Openflow_Tag, name),
                "offline_mode", device_value, NULL);
    }
    else
    {
        /* and outside offline mode it should be empty */
        json_t *device_value = json_pack("{ s : s, s : o }",
                "name", "offline_mode", "device_value", json_pack("[ s ,[]]", "set"));

        ovsdb_sync_upsert(SCHEMA_TABLE(Openflow_Tag), SCHEMA_COLUMN(Openflow_Tag, name),
                "offline_mode", device_value, NULL);
    }
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
            STRSCPY(config->value, VAL_OFFLINE_OFF);

        // Set enable/disable flag:
        if (strcmp(config->value, VAL_OFFLINE_ON) == 0)
        {
            gw_offline_cfg = true;
            LOG(INFO, "offline_cfg: Feature enabled.");
        }
        else
        {
            gw_offline_cfg = false;
            LOG(INFO, "offline_cfg: Feature disabled.");
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
        pm_node_state_set(KEY_OFFLINE_CFG, config->value, (config->persist_exists && config->persist));

        // Indicate ready if enabled and config already available:
        if (gw_offline_cfg)
        {
            if (pm_gw_offline_cfg_is_available())
            {
                LOG(INFO, "offline_cfg: Config already available in persistent storage.");
                pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_READY, false);
            }
            else
            {
                LOG(INFO, "offline_cfg: Config not yet available in persistent storage.");
                pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ENABLED, false);
            }
        }
        else
        {
            pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_DISABLED, false);

            // When feature disabled, erase the saved persistent data:
            if (gw_offline_ps_erase())
                LOG(NOTICE, "offline_cfg: Config erased from persistent storage.");
            else
                LOG(ERR, "offline_cfg: Error erasing config from persistent storage.");
        }
    }

    /* Enabling/disabling config monitoring and storing (Cloud): */
    if (strcmp(config->key, KEY_OFFLINE_MON) == 0)
    {
        if (mon->mon_type == OVSDB_UPDATE_DEL)
            STRSCPY(config->value, VAL_OFFLINE_OFF);

        // Set enable/disable config monitoring flag:
        if (strcmp(config->value, VAL_OFFLINE_ON) == 0)
        {
            gw_offline_mon = true;

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

            if (gw_offline_stat != status_ready)
            {
                LOG(WARN, "offline_cfg: Offline config mode triggered, but gw_offline_status "\
                           "!= ready. status=%d. Ignoring", gw_offline_stat);
                return;
            }

            gw_offline = true;
            LOG(NOTICE, "offline_cfg: gw_offline mode activated");

            if (gw_offline_mon)
            {
                /* if gw_offline_mon==true --> device is already configured by Cloud
                 * only enter state=active here, no need to configure device
                 * from stored config, but PM may be responsible for some offline
                 * tasks in this state.
                 */
                pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ACTIVE, false);
            }
            else
            {
                LOG(NOTICE, "offline_cfg: Load && apply persistent config triggered.");
                pm_gw_offline_load_and_apply_config();
            }

            if (gw_offline_stat == status_active)
            {
                /* If active gw_offline mode successfully entered, enable
                 * eth clients handling:  */
                gw_offline_enable_eth_clients_handling();

                /* and configure "offline_mode" openflow tag: */
                gw_offline_openflow_set_offline_mode(true);
            }
            else
            {
                gw_offline = false;
            }
        }
        else
        {
            if (!gw_offline)
            {
                LOG(INFO, "offline_cfg: Not in offline config mode and exit triggered. Ignoring.");
                return;
            }

            /* Disabled by CM when Cloud/Internet connectivity reestablished.
             *
             * In this case, PM does nothing -- Cloud will push proper ovsdb
             * config that should cleanly overwrite the applied persistent config.
             * If not, then restart of managers will be required.
             */
            gw_offline = false;

            // Deconfigure "offline_mode" openflow tag:
            gw_offline_openflow_set_offline_mode(false);

            // Set state accordingly:
            if (pm_gw_offline_cfg_is_available())
                pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_READY, false);
            else
                pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ENABLED, false);

            LOG(NOTICE, "offline_cfg: gw_offline mode exited");
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
    const char *first_key = NULL;

    json_array_foreach(cfg->radio_config, index, radio_row)
    {
        json_t *vif_configs = json_object_get(radio_row, "vif_configs");

        first_key = json_string_value(json_array_get(vif_configs, 0));

        if (strcmp(first_key, "uuid") == 0)
        {
            /* Single GW location each radio only has one VIF config. */
            /* The format of JSON is different from radio with multiple VIFs. */
            /* Just get the second element of the array, which is the actual UUID string. */
            const char *str_uuid = json_string_value(json_array_get(vif_configs, 1));

            if (str_uuid != NULL)
            {
                if (strcmp(str_uuid, vif_str_uuid) == 0)
                {
                    /* For VIF with vif_str_uuid we've found the corresponding radio if_name */
                    radio_if_name = json_string_value(json_object_get(radio_row, "if_name"));
                    return radio_if_name;
                }
            }
        }
        else
        {
            json_array_foreach(json_array_get(vif_configs, 1), index2, a_vif_config)
            {
                const char *str_uuid = json_string_value(json_array_get(a_vif_config, 1));
                if (str_uuid != NULL)
                {
                    if (strcmp(str_uuid, vif_str_uuid) == 0)
                    {
                        /* For VIF with vif_str_uuid we've found the corresponding radio if_name */
                        radio_if_name = json_string_value(json_object_get(radio_row, "if_name"));
                        return radio_if_name;
                    }
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
            /* A valid VIF that is up should have a corresponding radio that
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
    json_t *mutation;
    json_t *result;
    json_t *value;
    json_t *where;
    json_t *rows;
    int cnt;

    LOG(DEBUG, "offline_cfg: Adding uuid=%s to Wifi_Radio_Config::vif_configs -where if_name==%s", uuid.uuid, if_name);

    value = json_pack("[ s, s ]", "uuid", uuid.uuid);
    if (value == NULL)
    {
        LOG(ERR, "offline_cfg: Error packing vif_configs json value");
        return false;
    }

    where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), if_name);
    if (where == NULL)
    {
        LOG(WARN, "offline_cfg: Error creating ovsdb where simple: if_name==%s", if_name);
        json_decref(value);
        return false;
    }

    mutation = ovsdb_mutation(SCHEMA_COLUMN(Wifi_Radio_Config, vif_configs), json_string("insert"), value);
    rows = json_array();
    json_array_append_new(rows, mutation);

    result = ovsdb_tran_call_s(SCHEMA_TABLE(Wifi_Radio_Config), OTR_MUTATE, where, rows);
    if (result == NULL)
    {
        LOG(WARN, "offline_cfg: Failed to execute ovsdb transact");
        return false;
    }
    cnt = ovsdb_get_update_result_count(result, SCHEMA_TABLE(Wifi_Radio_Config), "mutate");

    LOG(DEBUG, "offline_cfg: Successful OVSDB mutate, cnt=%d", cnt);
    return true;
}

/* Initiate this module. */
void pm_gw_offline_init(void *data)
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
    OVSDB_TABLE_INIT(Connection_Manager_Uplink, if_name);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Config);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag_Group);

    OVSDB_TABLE_INIT_NO_KEY(IPv6_Address);
    OVSDB_TABLE_INIT(IPv6_Prefix, address);
    OVSDB_TABLE_INIT(IPv6_RouteAdv, interface);
    OVSDB_TABLE_INIT(DHCPv6_Server, interface);
    OVSDB_TABLE_INIT_NO_KEY(DHCP_Option);

    OVSDB_TABLE_INIT(IP_Interface, name);

    OVSDB_TABLE_INIT_NO_KEY(IGMP_Config);
    OVSDB_TABLE_INIT_NO_KEY(MLD_Config);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Route_Config);

    OVSDB_TABLE_INIT(Bridge, name);
    OVSDB_TABLE_INIT(Port, name);
    OVSDB_TABLE_INIT(Interface, name);

    OVSDB_TABLE_INIT(Netfilter, name);

    // Always install Node_Config monitor, other monitors installed when enabled.
    OVSDB_TABLE_MONITOR(Node_Config, true);

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
        LOG(INFO, "offline_cfg: Feature enable flag set in persistent storage");

        if (!pm_gw_offline_cfg_is_available())
        {
            LOG(WARN, "offline_cfg: Feature enable persistent flag set "
                      "but config not yet available in persistent storage.");
        }

        pm_node_config_set(KEY_OFFLINE_CFG, VAL_OFFLINE_ON, true);
    }
    else
    {
        LOG(DEBUG, "offline_cfg: Feature disabled (flag not set or not present in persistent storage)");
    }
}

void pm_gw_offline_fini(void *data)
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
    bool rv = false;

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

    if (!json_equal(cfg->inet_config_uplink, cfg_cache.inet_config_uplink))
    {
        rv = gw_offline_ps_store(PS_KEY_INET_CONFIG_UPLINK, cfg->inet_config_uplink);
        if (!rv) goto exit;
        json_decref(cfg_cache.inet_config_uplink);
        cfg_cache.inet_config_uplink = json_incref(cfg->inet_config_uplink);
    } else LOG(DEBUG, "offline_cfg: inet_config_uplink: cached==stored. Skipped storing.");

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

    if (!json_equal(cfg->openflow_config, cfg_cache.openflow_config))
    {
        rv = gw_offline_ps_store(PS_KEY_OF_CONFIG, cfg->openflow_config);
        if (!rv) goto exit;
        json_decref(cfg_cache.openflow_config);
        cfg_cache.openflow_config = json_incref(cfg->openflow_config);
    } else LOG(DEBUG, "offline_cfg: openflow_config: cached==stored. Skipped storing.");

    if (!json_equal(cfg->openflow_tag, cfg_cache.openflow_tag))
    {
        rv = gw_offline_ps_store(PS_KEY_OF_TAG, cfg->openflow_tag);
        if (!rv) goto exit;
        json_decref(cfg_cache.openflow_tag);
        cfg_cache.openflow_tag = json_incref(cfg->openflow_tag);
    } else LOG(DEBUG, "offline_cfg: openflow_tag: cached==stored. Skipped storing.");

    if (!json_equal(cfg->openflow_tag_group, cfg_cache.openflow_tag_group))
    {
        rv = gw_offline_ps_store(PS_KEY_OF_TAG_GROUP, cfg->openflow_tag_group);
        if (!rv) goto exit;
        json_decref(cfg_cache.openflow_tag_group);
        cfg_cache.openflow_tag_group = json_incref(cfg->openflow_tag_group);
    } else LOG(DEBUG, "offline_cfg: openflow_tag_group: cached==stored. Skipped storing.");

    if (!json_equal(cfg->ipv6_cfg, cfg_cache.ipv6_cfg))
    {
        rv = gw_offline_ps_store(PS_KEY_IPV6_CFG, cfg->ipv6_cfg);
        if (!rv) goto exit;
        json_decref(cfg_cache.ipv6_cfg);
        cfg_cache.ipv6_cfg = json_incref(cfg->ipv6_cfg);
    } else LOG(DEBUG, "offline_cfg: ipv6_cfg: cached==stored. Skipped storing.");

    if (!json_equal(cfg->inet_config_vlan, cfg_cache.inet_config_vlan))
    {
        rv = gw_offline_ps_store(PS_KEY_INET_CONFIG_VLAN, cfg->inet_config_vlan);
        if (!rv) goto exit;
        json_decref(cfg_cache.inet_config_vlan);
        cfg_cache.inet_config_vlan = json_incref(cfg->inet_config_vlan);
    } else LOG(DEBUG, "offline_cfg: inet_config_vlan: cached==stored. Skipped storing.");

    if (!json_equal(cfg->igmp_config, cfg_cache.igmp_config))
    {
        rv = gw_offline_ps_store(PS_KEY_IGMP_CONFIG, cfg->igmp_config);
        if (!rv) goto exit;
        json_decref(cfg_cache.igmp_config);
        cfg_cache.igmp_config = json_incref(cfg->igmp_config);
    } else LOG(DEBUG, "offline_cfg: igmp_config: cached==stored. Skipped storing.");

    if (!json_equal(cfg->mld_config, cfg_cache.mld_config))
    {
        rv = gw_offline_ps_store(PS_KEY_MLD_CONFIG, cfg->mld_config);
        if (!rv) goto exit;
        json_decref(cfg_cache.mld_config);
        cfg_cache.mld_config = json_incref(cfg->mld_config);
    } else LOG(DEBUG, "offline_cfg: mld_config: cached==stored. Skipped storing.");

    if (!json_equal(cfg->route_config, cfg_cache.route_config))
    {
        rv = gw_offline_ps_store(PS_KEY_ROUTE_CONFIG, cfg->route_config);
        if (!rv) goto exit;
        json_decref(cfg_cache.route_config);
        cfg_cache.route_config = json_incref(cfg->route_config);
    } else LOG(DEBUG, "offline_cfg: route_config: cached==stored. Skipped storing.");

    if (!json_equal(cfg->port_mcast, cfg_cache.port_mcast))
    {
        rv = gw_offline_ps_store(PS_KEY_PORT_MCAST, cfg->port_mcast);
        if (!rv) goto exit;
        json_decref(cfg_cache.port_mcast);
        cfg_cache.port_mcast = json_incref(cfg->port_mcast);
    } else LOG(DEBUG, "offline_cfg: port_mcast: cached==stored. Skipped storing.");

    if (!json_equal(cfg->port_lanbridge, cfg_cache.port_lanbridge))
    {
        rv = gw_offline_ps_store(PS_KEY_PORT_LANBR, cfg->port_lanbridge);
        if (!rv) goto exit;
        json_decref(cfg_cache.port_lanbridge);
        cfg_cache.port_lanbridge = json_incref(cfg->port_lanbridge);
    } else LOG(DEBUG, "offline_cfg: port_lanbridge: cached==stored. Skipped storing.");

    if (!json_equal(cfg->port_fronthauls, cfg_cache.port_fronthauls))
    {
        rv = gw_offline_ps_store(PS_KEY_PORT_FRONTHAULS, cfg->port_fronthauls);
        if (!rv) goto exit;
        json_decref(cfg_cache.port_fronthauls);
        cfg_cache.port_fronthauls = json_incref(cfg->port_fronthauls);
    } else LOG(DEBUG, "offline_cfg: port_fronthauls: cached==stored. Skipped storing.");

    if (!json_equal(cfg->interface_fronthauls, cfg_cache.interface_fronthauls))
    {
        rv = gw_offline_ps_store(PS_KEY_INTF_FRONTHAULS, cfg->interface_fronthauls);
        if (!rv) goto exit;
        json_decref(cfg_cache.interface_fronthauls);
        cfg_cache.interface_fronthauls = json_incref(cfg->interface_fronthauls);
    } else LOG(DEBUG, "offline_cfg: interface_fronthauls: cached==stored. Skipped storing.");

    if (!json_equal(cfg->netfilter, cfg_cache.netfilter))
    {
        rv = gw_offline_ps_store(PS_KEY_NETFILTER, cfg->netfilter);
        if (!rv) goto exit;
        json_decref(cfg_cache.netfilter);
        cfg_cache.netfilter = json_incref(cfg->netfilter);
    } else LOG(DEBUG, "offline_cfg: netfilter: cached==stored. Skipped storing.");

    rv = true;
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
    config_str = MALLOC((size_t)str_size);
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
    if (config_str != NULL) FREE(config_str);
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

    rv = gw_offline_ps_load(PS_KEY_INET_CONFIG_UPLINK, &cfg->inet_config_uplink);
    if (!rv) goto exit;
    json_decref(cfg_cache.inet_config_uplink);
    cfg_cache.inet_config_uplink = json_incref(cfg->inet_config_uplink);

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

    rv = gw_offline_ps_load(PS_KEY_OF_CONFIG, &cfg->openflow_config);
    if (!rv) goto exit;
    json_decref(cfg_cache.openflow_config);
    cfg_cache.openflow_config = json_incref(cfg->openflow_config);

    rv = gw_offline_ps_load(PS_KEY_OF_TAG, &cfg->openflow_tag);
    if (!rv) goto exit;
    json_decref(cfg_cache.openflow_tag);
    cfg_cache.openflow_tag = json_incref(cfg->openflow_tag);

    rv = gw_offline_ps_load(PS_KEY_OF_TAG_GROUP, &cfg->openflow_tag_group);
    if (!rv) goto exit;
    json_decref(cfg_cache.openflow_tag_group);
    cfg_cache.openflow_tag_group = json_incref(cfg->openflow_tag_group);

    rv = gw_offline_ps_load(PS_KEY_IPV6_CFG, &cfg->ipv6_cfg);
    if (!rv) goto exit;
    json_decref(cfg_cache.ipv6_cfg);
    cfg_cache.ipv6_cfg = json_incref(cfg->ipv6_cfg);

    rv = gw_offline_ps_load(PS_KEY_INET_CONFIG_VLAN, &cfg->inet_config_vlan);
    if (!rv) goto exit;
    json_decref(cfg_cache.inet_config_vlan);
    cfg_cache.inet_config_vlan = json_incref(cfg->inet_config_vlan);

    rv = gw_offline_ps_load(PS_KEY_IGMP_CONFIG, &cfg->igmp_config);
    if (!rv) goto exit;
    json_decref(cfg_cache.igmp_config);
    cfg_cache.igmp_config = json_incref(cfg->igmp_config);

    rv = gw_offline_ps_load(PS_KEY_MLD_CONFIG, &cfg->mld_config);
    if (!rv) goto exit;
    json_decref(cfg_cache.mld_config);
    cfg_cache.mld_config = json_incref(cfg->mld_config);

    rv = gw_offline_ps_load(PS_KEY_ROUTE_CONFIG, &cfg->route_config);
    if (!rv) goto exit;
    json_decref(cfg_cache.route_config);
    cfg_cache.route_config = json_incref(cfg->route_config);

    rv = gw_offline_ps_load(PS_KEY_PORT_MCAST, &cfg->port_mcast);
    if (!rv) goto exit;
    json_decref(cfg_cache.port_mcast);
    cfg_cache.port_mcast = json_incref(cfg->port_mcast);

    rv = gw_offline_ps_load(PS_KEY_PORT_LANBR, &cfg->port_lanbridge);
    if (!rv) goto exit;
    json_decref(cfg_cache.port_lanbridge);
    cfg_cache.port_lanbridge = json_incref(cfg->port_lanbridge);

    rv = gw_offline_ps_load(PS_KEY_PORT_FRONTHAULS, &cfg->port_fronthauls);
    if (!rv) goto exit;
    json_decref(cfg_cache.port_fronthauls);
    cfg_cache.port_fronthauls = json_incref(cfg->port_fronthauls);

    rv = gw_offline_ps_load(PS_KEY_INTF_FRONTHAULS, &cfg->interface_fronthauls);
    if (!rv) goto exit;
    json_decref(cfg_cache.interface_fronthauls);
    cfg_cache.interface_fronthauls = json_incref(cfg->interface_fronthauls);

    rv = gw_offline_ps_load(PS_KEY_NETFILTER, &cfg->netfilter);
    if (!rv) goto exit;
    json_decref(cfg_cache.netfilter);
    cfg_cache.netfilter = json_incref(cfg->netfilter);

exit:
    return rv;
}

static bool gw_offline_ps_erase(void)
{
    LOG(INFO, "offline_cfg: Erasing persistent store: %s", PS_STORE_GW_OFFLINE);
    return osp_ps_erase_store_name(PS_STORE_GW_OFFLINE, 0);
}

static const char *util_get_mcast_intf(const struct gw_offline_cfg *cfg)
{
    size_t index;
    json_t *igmp;
    json_t *map_array;
    json_t *map_entry;

    if (json_array_size(cfg->igmp_config) == 0)
    {
        return NULL;
    }

    igmp = json_array_get(cfg->igmp_config, 0);
    map_array = json_array_get(json_object_get(igmp, SCHEMA_COLUMN(IGMP_Config, other_config)), 1);

    json_array_foreach(map_array, index, map_entry)
    {
        if (strcmp(json_string_value(json_array_get(map_entry, 0)), "mcast_interface") == 0)
        {
            return json_string_value(json_array_get(map_entry, 1));
        }
    }
    return NULL;
}

/* Read IPv6-related OVSDB tables with relevant fields. */
static bool gw_offline_cfg_ipv6_read(struct gw_offline_cfg *cfg)
{
    struct gw_offline_ipv6_cfg ipv6_cfg = { 0 };
    struct schema_IPv6_RouteAdv ipv6_routeAdv;
    struct schema_DHCPv6_Server dhcpv6_server;
    struct schema_IPv6_Prefix ipv6_prefix;
    pjs_errmsg_t perr;
    json_t *where;

    /* IPv6_Prefix: (for now we assume cloud behaviour: either 0 or 1 row for IPv6 prefix configured) */
    where = ovsdb_where_simple_typed(SCHEMA_COLUMN(IPv6_Prefix, enable), "true", OCLM_BOOL);
    if (ovsdb_table_select_one_where(&table_IPv6_Prefix, where, &ipv6_prefix))
    {
        ipv6_cfg.prefix_is_set = true;

        ipv6_cfg.prefix_autonomous = ipv6_prefix.autonomous;
    }

    /* IPv6_RouteAdv: */
    if (ovsdb_table_select_one_where(&table_IPv6_RouteAdv, NULL, &ipv6_routeAdv))
    {
        ipv6_cfg.routeAdv_is_set = true;

        ipv6_cfg.routeAdv_managed = ipv6_routeAdv.managed;
        ipv6_cfg.routeAdv_other_config = ipv6_routeAdv.other_config;
        ipv6_cfg.routeAdv_prefixes_is_set = ipv6_routeAdv.prefixes_present && ipv6_routeAdv.prefixes_len > 0;
        ipv6_cfg.routeAdv_rdnss_is_set = ipv6_routeAdv.rdnss_present && ipv6_routeAdv.rdnss_len > 0;
        ipv6_cfg.routeAdv_dnssl_is_set = ipv6_routeAdv.dnssl_present && ipv6_routeAdv.dnssl_len > 0;
    }

    /* DHCPv6_Server: */
    if (ovsdb_table_select_one_where(&table_DHCPv6_Server, NULL, &dhcpv6_server))
    {
        ipv6_cfg.dhcpv6_server_is_set = true;
        ipv6_cfg.dhcpv6_server_prefixes_is_set = dhcpv6_server.prefixes_present && dhcpv6_server.prefixes_len > 0;
    }

    /* Convert IPv6 config to json: */
    cfg->ipv6_cfg = gw_offline_ipv6_cfg_to_json(&ipv6_cfg, perr);
    if (cfg->ipv6_cfg == NULL)
    {
        LOG(ERR, "offline_cfg: Error converting gw_offline_ipv6_cfg structure to JSON: %s", perr);
        return false;
    }

    return true;
}

/*
 * Read some fields of the Interface table for AP fronthauls.
 */
static bool gw_offline_cfg_interface_table_read(struct gw_offline_cfg *cfg)
{
    char *filter_columns[] = { "+",
                                SCHEMA_COLUMN(Interface, ofport_request),
                                SCHEMA_COLUMN(Interface, name),
                                NULL };
    json_t *json_res;
    json_t *row;
    size_t index;
    bool rv = true;

    /*
     * Read Interface->ofport_request for all fronthaul APs.
     * (They are assumed/expected to be in LAN_BRIGE.)
     *
     * By default ofport values are dynamic. If specific ofport values were
     * requested via ofport_request we must restore them in order to not break
     * certain OpenFlow rules.
     */

    cfg->interface_fronthauls = json_array();

    json_array_foreach(cfg->vif_config, index, row) // For all fronthaul APs
    {
        const char *if_name = json_string_value(json_object_get(row, "if_name"));
        json_t *mld_if_name = util_vif_get_mld_ifname(if_name);

        if (mld_if_name != NULL)
        {
            /* This VIF is part of a MLO group. */
            LOG(DEBUG, "offline_cfg: if_name=%s, mld_if_name=%s (MLO)", if_name, json_string_value(mld_if_name));
            if_name = json_string_value(mld_if_name); // need to save Inet_Config for mld_if_name
        }

        if (!util_array_has_json_with_if_name(cfg->interface_fronthauls, if_name)) // no point in saving duplicates
        {
            /* Read this fronthaul AP's Interface(ofport_request, name). */
            json_res = ovsdb_sync_select_where2(SCHEMA_TABLE(Interface),
                        ovsdb_where_simple(SCHEMA_COLUMN(Interface, name), if_name));
            if (json_res != NULL && json_array_size(json_res) == 1)
            {
                ovsdb_table_filter_row(json_array_get(json_res, 0), filter_columns);
                json_array_append(cfg->interface_fronthauls, json_array_get(json_res, 0));
            }
            else
            {
                LOG(WARN, "offline_cfg: Error selecting from Interface table for name==%s - not in bridge?",
                        if_name);
                rv = false;
            }
            json_decref(json_res);
        }

        json_decref(mld_if_name);
        if (!rv) return rv;
    }
    return rv;
}

/*
 * Read some fields of the Port table for some of the interfaces:
 *  - fronthauls
 *  - mcast interface
 *  - LAN_BRIDGE interface
 */
static bool gw_offline_cfg_port_table_read(struct gw_offline_cfg *cfg)
{
    char *filter_columns[] = { "+",
                                SCHEMA_COLUMN(Port, tag),
                                SCHEMA_COLUMN(Port, other_config),
                                SCHEMA_COLUMN(Port, name),
                                SCHEMA_COLUMN(Port, vlan_mode),
                                NULL };
    const char *mcast_intf;
    json_t *json_res;
    json_t *row;
    size_t index;
    bool rv = true;

    /* All fronthauls: obtain Port(tag, other_config): */
    cfg->port_fronthauls = json_array();
    json_array_foreach(cfg->vif_config, index, row)
    {
        const char *if_name = json_string_value(json_object_get(row, "if_name"));
        json_t *mld_if_name = util_vif_get_mld_ifname(if_name);

        if (mld_if_name != NULL)
        {
            /* This VIF is part of a MLO group. */
            LOG(DEBUG, "offline_cfg: if_name=%s, mld_if_name=%s (MLO)", if_name, json_string_value(mld_if_name));
            if_name = json_string_value(mld_if_name); // need to save Inet_Config for mld_if_name
        }

        if (!util_array_has_json_with_if_name(cfg->port_fronthauls, if_name)) // no point in saving duplicates
        {
            json_res = ovsdb_sync_select_where2(SCHEMA_TABLE(Port),
                        ovsdb_where_simple(SCHEMA_COLUMN(Port, name), if_name));
            if (json_res != NULL && json_array_size(json_res) == 1)
            {
                ovsdb_table_filter_row(json_array_get(json_res, 0), filter_columns);
                json_array_append(cfg->port_fronthauls, json_array_get(json_res, 0));
            }
            else
            {
                LOG(ERR, "offline_cfg: Error selecting from Port table for %s", if_name);
                rv = false;
            }
            json_decref(json_res);
        }

        json_decref(mld_if_name);
        if (!rv) return rv;
    }

    /* mcast interface: obtain Port(tag, other_config) */
    mcast_intf = util_get_mcast_intf(cfg);
    if (mcast_intf != NULL)
    {
        json_res = ovsdb_sync_select_where2(SCHEMA_TABLE(Port),
                       ovsdb_where_simple(SCHEMA_COLUMN(Port, name), mcast_intf));
        if (json_res == NULL || json_array_size(json_res) > 1)
        {
            LOG(ERR, "offline_cfg: Error selecting from Port table for: %s", mcast_intf);
            json_decref(json_res);
            return false;
        }
        /* Note: if mcast interface is not in bridge LAN_BRIDGE (e.g. with
         * routed services), the JSON here will be an empty JSON array and that
         * is what we save. */
        ovsdb_table_filter_row(json_array_get(json_res, 0), filter_columns);
        cfg->port_mcast = json_res;
    }

    /* LAN_BRIDGE interface: obtain Port(tag, other_config): */
    json_res = ovsdb_sync_select_where2(SCHEMA_TABLE(Port),
                   ovsdb_where_simple(SCHEMA_COLUMN(Port, name), LAN_BRIDGE));
    if (json_res == NULL || json_array_size(json_res) > 1)
    {
        LOG(ERR, "offline_cfg: Error selecting from Port table");
        json_decref(json_res);
        return false;
    }

    ovsdb_table_filter_row(json_array_get(json_res, 0), filter_columns);
    cfg->port_lanbridge = json_res;

    return true;
}

/*
 * Restore a Port row saved config.
 *
 * A Port row for the interface (name field match) is assumed to already exist.
 *
 * @param[in]  port_row  the saved JSON (having fields that we are saving/restoring)
 *                       for this Port. The 'name' field must be present as it
 *                       identifies for which interface we're restoring the
 *                       Port row.
 */
static bool util_restore_port_row(json_t *port_row)
{
    const char *name = NULL;
    bool rv = false;

    name = json_string_value(json_object_get(port_row, SCHEMA_COLUMN(Port, name)));
    if (name == NULL)
    {
        LOG(ERR, "offline_cfg: Unexpected: No name field in saved Port row JSON");
        return false;
    }

    /* 'name' is immutable in the schema, must not be part of update */
    name = strdup(name);
    json_object_del(port_row, SCHEMA_COLUMN(Port, name));

    /* Port row update: */
    if (ovsdb_sync_update(
            SCHEMA_TABLE(Port),
            SCHEMA_COLUMN(Port, name),
            name,
            json_incref(port_row)) != 1)
    {
        LOG(ERR, "offline_cfg: Error updating Port table for: %s", name);
        goto out;
    }

    LOG(DEBUG, "offline_cfg: Updated Port for: %s", name);
    rv = true;
out:
    free((void *)name);
    return rv;
}

/* In the @param rows_array find and return a row with a 'name' field
 * matching @param name. */
static json_t *util_get_row_by_name(json_t *rows_array, const char *name)
{
    json_t *row;
    size_t index;

    json_array_foreach(rows_array, index, row)
    {
        const char *saved_name = json_string_value(json_object_get(row, "name"));
        if (saved_name == NULL)
        {
            LOG(ERR, "offline_cfg: Unexpected: No name field in saved row JSON");
            return NULL;
        }

        if (strcmp(name, saved_name) == 0)
        {
            return row; // return the matching row
        }
    }
    return NULL;
}

static json_t *util_ovsdb_uuid_name(const char *uuid_name)
{
    json_t *obj = json_object();
    json_object_set_new(obj, "uuid-name", json_string(uuid_name));
    return obj;
}

/*
 * Create OVSDB transaction for Bridge/Port/Interface hierarchy setup and
 * config restoring for bridge @param br_name and interface @param intf_name
 * added to that bridge.
 *
 * Bridge row for @param br_name is assumed to already exist.
 *
 * Interface and Port are inserted as per @param _interface_row and @param _port_row
 * saved fields.
 *
 * @return OVSDB transaction json for Bridge/Port/Interface hierarchy restoring.
 */
static json_t *gw_offline_ovsdb_bridge_port_interface_restore_transaction_get(
    const char *br_name,
    const char *intf_name,
    const json_t *_port_row,
    const json_t *_interface_row)
{
    json_t *port_row;
    json_t *interface_row;
    json_t *jarray = NULL;

    port_row = json_deep_copy(_port_row);

    if (_interface_row != NULL)
    {
        interface_row = json_deep_copy(_interface_row);
    }
    else /* No special Interface config saved, but we still need a default Interface row, create one: */
    {
        interface_row = json_pack("{s:s}", "name", intf_name);
    }

    /* Interface row insert: */
    jarray = ovsdb_tran_multi(
            jarray,
            util_ovsdb_uuid_name("new_interface"),
            SCHEMA_TABLE(Interface),
            OTR_INSERT,
            NULL,
            interface_row);

    /* Port row insert: */

    /* Set Port->interfaces to the uuid of the Interface row we have just inserted: */
    json_object_set_new(port_row, "interfaces", json_pack("[s, s]", "named-uuid", "new_interface"));

    jarray = ovsdb_tran_multi(
            jarray,
            util_ovsdb_uuid_name("new_port"),
            SCHEMA_TABLE(Port),
            OTR_INSERT,
            NULL,
            port_row);

    /* Bridge table 'ports' mutation: */

    /* Mutate Bridge -w name==<br_name> (assumed to exist) ports: insert uuid of the just created Port row */
    json_t *bridge_ports_mutation = json_pack("[[s, s, [s, [[s, s]]]]]", "ports", "insert", "set", "named-uuid", "new_port");

    jarray = ovsdb_tran_multi(
            jarray,
            NULL,
            SCHEMA_TABLE(Bridge),
            OTR_MUTATE,
            ovsdb_where_simple(SCHEMA_COLUMN(Bridge, name), br_name),
            bridge_ports_mutation);

    return jarray;
}

/*
 * Restore Bridge/Port/Interface hierarchy and saved config for interface
 * @param intf_name in bridge LAN_BRIDGE, by using saved fields from
 * @param port_row and @param interface_row.
 */
static bool gw_offline_cfg_home_restore_home_bridge_intf(
    const struct gw_offline_cfg *cfg,
    const char *intf_name,
    const json_t *port_row,
    const json_t *interface_row)
{
    json_t *transaction;
    json_t *response;

    transaction = gw_offline_ovsdb_bridge_port_interface_restore_transaction_get(LAN_BRIDGE, intf_name, port_row, interface_row);
    if (transaction == NULL)
    {
        LOG(ERROR, "offline_cfg: Failed to create Bridge/Port/Interface OVSDB transaction for %s", intf_name);
        return false;
    }

    response = ovsdb_method_send_s(MT_TRANS, transaction);
    if (response == NULL)
    {
        LOG(ERROR, "offline_cfg: Failed to restore Bridge/Port/Interface for %s: failed sending transaction to OVSDB", intf_name);
        return false;
    }
    LOG(DEBUG, "offline_cfg: responsec: %s", json_dumps(response, 0));

    json_decref(response);

    return true;
}

/* Restore Bridge/Port/Interface hierarchy and saved fields. */
static bool gw_offline_cfg_home_restore_home_bridge(const struct gw_offline_cfg *cfg)
{
    json_t *port_row;
    json_t *interface_row;
    size_t index;
    const char *name;

    /* Restore all fronthauls Bridge/Port/Interface config: */

    LOG(DEBUG, "Restoring fronthauls Bridge/Port/Interface...");
    json_array_foreach(cfg->port_fronthauls, index, port_row)
    {
        name = json_string_value(json_object_get(port_row, "name"));
        if (name == NULL)
        {
            LOG(ERR, "offline_cfg: Unexpected: No name field in saved Port row JSON");
            return false;
        }

        interface_row = util_get_row_by_name(cfg->interface_fronthauls, name); // OK to be NULL in some cases

        if (!gw_offline_cfg_home_restore_home_bridge_intf(cfg, name, port_row, interface_row))
        {
            LOG(ERROR, "offline_cfg: Failed to restore Bridge/Port/Interface for %s", name);
            return false;
        }
        LOG(DEBUG, "offline_cfg: Restored Bridge/Port/Interface for %s", name);
    }

    /* Restore mcast interface Port row config: */
    if (json_array_size(cfg->port_mcast) == 1)
    {
        LOG(DEBUG, "Restoring mcast Bridge/Port/Interface...");

        port_row = json_array_get(cfg->port_mcast, 0);
        name = json_string_value(json_object_get(port_row, "name"));
        if (name == NULL)
        {
            LOG(ERR, "offline_cfg: Unexpected: No name field in saved Port row JSON");
            return false;
        }

        if (!gw_offline_cfg_home_restore_home_bridge_intf(cfg, name, port_row, NULL))
        {
            LOG(ERROR, "offline_cfg: Failed to restore Bridge/Port/Interface for %s", name);
            return false;
        }
        LOG(DEBUG, "offline_cfg: Restored Bridge/Port/Interface for mcast interface %s", name);
    }

    /* Restore LAN_BRIDGE interface Port row config: */

    if (json_array_size(cfg->port_lanbridge) == 1)
    {
        LOG(DEBUG, "Restoring LAN_BRIDGE Port config");

        if (!util_restore_port_row(json_array_get(cfg->port_lanbridge, 0)))
        {
            LOG(ERROR, "offline_cfg: Failed to restore Port for %s", LAN_BRIDGE);
        }
    }

    return true;
}

/*
 * Check if a VIF named vif_if_name is part of a MLO group.
 *
 * If yes, return the MLD interface name (as json).
 * If no (or error), return NULL.
 */
static json_t *util_vif_get_mld_ifname(const char *vif_if_name)
{
    json_t *mld_if_name = NULL;
    json_t *json_res;
    json_t *json_row;

    json_res = ovsdb_sync_select("Wifi_VIF_State", "if_name", vif_if_name);
    json_row = json_array_get(json_res, 0);
    if (json_row == NULL)
    {
        LOG(ERR, "offline_cfg: %s: Error selecting from Wifi_VIF_State where if_name==%s",
                __func__, vif_if_name);
    }
    mld_if_name = json_string(json_string_value(json_object_get(json_row, "mld_if_name")));

    json_decref(json_res);
    return mld_if_name;
}

/* In the array 'array', is there a json with "if_name" object's value that equals *if_name ? */
static bool util_array_has_json_with_if_name(const json_t *array, const char *if_name)
{
    size_t index;
    json_t *elt;

    json_array_foreach(array, index, elt)
    {
        const char *elt_if_name = json_string_value(json_object_get(elt, "if_name"));
        if (elt_if_name != NULL && STRSCMP(elt_if_name, if_name) == 0)
        {
            return true;
        }
    }
    return false;
}

/* Read the current subset of OVSDB config. */
static bool gw_offline_cfg_ovsdb_read(struct gw_offline_cfg *cfg)
{
    char uplink[C_IFNAME_LEN];
    size_t index;
    json_t *json_res;
    json_t *where;
    json_t *row;

    memset(cfg, 0, sizeof(*cfg));

    /* Select home AP VIFs from Wifi_VIF_Config: */
    cfg->vif_config = ovsdb_sync_select("Wifi_VIF_Config", "bridge", LAN_BRIDGE);
    if (cfg->vif_config == NULL)
    {
        LOG(ERR, "offline_cfg: Error selecting from Wifi_VIF_Config");
        goto exit_failure;
    }

    /* Inet_Config of VIFs (or MLD interfaces if VIFs part of MLO groups) */
    cfg->inet_config_home_aps = json_array();
    json_array_foreach(cfg->vif_config, index, row)
    {
        const char *if_name = json_string_value(json_object_get(row, "if_name"));
        json_t *mld_if_name = util_vif_get_mld_ifname(if_name);

        if (mld_if_name != NULL)
        {
            /* This VIF is part of a MLO group. */
            LOG(DEBUG, "offline_cfg: if_name=%s, mld_if_name=%s (MLO)", if_name, json_string_value(mld_if_name));
            if_name = json_string_value(mld_if_name); // need to save Inet_Config for mld_if_name
        }

        if (!util_array_has_json_with_if_name(cfg->inet_config_home_aps, if_name)) // no point in saving duplicates
        {
            json_res = ovsdb_sync_select("Wifi_Inet_Config", "if_name", if_name);
            if (json_res != NULL && json_array_size(json_res) == 1)
            {
                json_array_append(cfg->inet_config_home_aps, json_array_get(json_res, 0));
            }
            else
            {
                LOG(WARN, "offline_cfg: Error selecting from Wifi_Inet_Config: no row for if_name=%s", if_name);
            }
            json_decref(json_res);
        }

        json_decref(mld_if_name);
    }

    /* lan bridge config from Wifi_Inet_Config: */
    cfg->inet_config = ovsdb_sync_select("Wifi_Inet_Config", "if_name", LAN_BRIDGE);
    if (cfg->inet_config == NULL)
    {
        LOG(ERR, "offline_cfg: Error selecting from Wifi_Inet_Config -w if_name==%s", LAN_BRIDGE);
        goto exit_failure;
    }

    /* save certain uplink config from Wifi_Inet_Config */
    if (gw_offline_uplink_ifname_get(uplink, sizeof(uplink)))
    {
        /* Currently we remember the following uplink's settings: upnp_mode  */
        char *filter_columns[] = { "+", SCHEMA_COLUMN(Wifi_Inet_Config, upnp_mode), NULL };
        json_t *row_uplink;
        json_t *rows;

        LOG(DEBUG, "offline_cfg: Uplink known=%s. will save uplink settings", uplink);

        rows = ovsdb_sync_select(SCHEMA_TABLE(Wifi_Inet_Config), SCHEMA_COLUMN(Wifi_Inet_Config, if_name), uplink);
        if (rows == NULL)
        {
            LOG(ERR, "offline_cfg: Error selecting from Wifi_Inet_Config -w if_name == %s", uplink);
            goto exit_failure;
        }
        row_uplink = json_array_get(rows, 0);
        json_incref(row_uplink);
        json_decref(rows);

        row_uplink = ovsdb_table_filter_row(row_uplink, filter_columns);

        cfg->inet_config_uplink = json_array();
        json_array_append_new(cfg->inet_config_uplink, row_uplink);

        LOG(DEBUG, "offline_cfg: inet_config uplink config (from %s) that will be saved: %s",
                uplink, json_dumps_static(row_uplink, 0));
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

    /* Openflow_Config: */
    cfg->openflow_config = ovsdb_sync_select_where(SCHEMA_TABLE(Openflow_Config), NULL);
    if (cfg->openflow_config == NULL)
    {
        LOG(DEBUG, "offline_cfg: Openflow_Config: NO rows in the table or error.");
        cfg->openflow_config = json_array();
    }

    /* Openflow_Tag: */
    cfg->openflow_tag = ovsdb_sync_select_where(SCHEMA_TABLE(Openflow_Tag), NULL);
    if (cfg->openflow_tag == NULL)
    {
        LOG(DEBUG, "offline_cfg: Openflow_Tag: NO rows in the table or error.");
        cfg->openflow_tag = json_array();
    }

    /* Openflow_Tag_Group: */
    cfg->openflow_tag_group = ovsdb_sync_select_where(SCHEMA_TABLE(Openflow_Tag_Group), NULL);
    if (cfg->openflow_tag_group == NULL)
    {
        LOG(DEBUG, "offline_cfg: Openflow_Tag_Group: NO rows in the table or error.");
        cfg->openflow_tag_group = json_array();
    }

    if (!gw_offline_cfg_ipv6_read(cfg))
    {
        LOG(WARN, "offline_cfg: Failed obtaining IPv6 configuration");
    }

    /* Delete special ovsdb keys like _uuid, etc, these should not be stored: */
    gw_offline_cfg_delete_special_keys(cfg);

    /* Openflow_Tag rows should be saved and restored without device_value: */
    delete_ovsdb_column(cfg->openflow_tag, "device_value");

    /* IGMP Config: */
    cfg->igmp_config = ovsdb_sync_select_where(SCHEMA_TABLE(IGMP_Config), NULL);
    if (cfg->igmp_config == NULL)
    {
        LOG(DEBUG, "offline_cfg: IGMP_Config: No rows or error");
        cfg->igmp_config = json_array();
    }

    /* MLD Config: */
    cfg->mld_config = ovsdb_sync_select_where(SCHEMA_TABLE(MLD_Config), NULL);
    if (cfg->mld_config == NULL)
    {
        LOG(DEBUG, "offline_cfg: MLD_Config: No rows or error");
        cfg->mld_config = json_array();
    }

    /* Wifi_Inet_Config VLAN Interfaces: We want to save the mcast vlan interface inet config: */
    if (json_array_size(cfg->igmp_config) != 0)
    {
        where = json_array();
        json_array_append_new(
                where,
                ovsdb_tran_cond_single(SCHEMA_COLUMN(Wifi_Inet_Config, if_type), OFUNC_EQ, "vlan"));

        const char *mcast_intf = util_get_mcast_intf(cfg);
        if (mcast_intf == NULL)
        {
            LOG(ERR, "offline_cfg: Cannot determine multicast interface.");
            goto exit_failure;
        }
        LOG(DEBUG, "offline_cfg: mcast interface: %s", mcast_intf);
        json_array_append_new(
                where,
                ovsdb_tran_cond_single(SCHEMA_COLUMN(Wifi_Inet_Config, if_name), OFUNC_EQ, (char *)mcast_intf));

        cfg->inet_config_vlan = ovsdb_sync_select_where(
                SCHEMA_TABLE(Wifi_Inet_Config),
                where);
        if (cfg->inet_config_vlan == NULL)
        {
            LOG(DEBUG, "offline_cfg: Wifi_Inet_Config: No if_type==vlan mcast interfaces or error");
            cfg->inet_config_vlan = json_array();
        }
    }
    else
    {
        cfg->inet_config_vlan = json_array();
    }

    /* Wifi_Route_Config: We want to save all static routes possibly pushed by
     * cloud. We do not want to save any default routes (dest 0.0.0.0): */
    where = json_array();
    json_array_append_new(
            where,
            ovsdb_tran_cond_single(SCHEMA_COLUMN(Wifi_Route_Config, dest_addr), OFUNC_NEQ, "0.0.0.0"));
    cfg->route_config = ovsdb_sync_select_where(
            SCHEMA_TABLE(Wifi_Route_Config),
            where);
    if (cfg->route_config == NULL)
    {
        LOG(DEBUG, "offline_cfg: Wifi_Route_Config: No rows or error");
        cfg->route_config = json_array();
    }

    /* Remember netfilter config: */
    cfg->netfilter = ovsdb_sync_select_where("Netfilter", NULL);
    if (cfg->netfilter == NULL)
    {
        LOG(ERR, "offline_cfg: Error selecting from Netfilter");
        goto exit_failure;
    }

    /* Read and save some rows and some fields of Port table: */
    if (!gw_offline_cfg_port_table_read(cfg))
    {
        LOG(WARN, "offline_cfg: Failed obtaining Port table config");
    }

    /* Read and save some rows and some fields of Interface table: */
    if (!gw_offline_cfg_interface_table_read(cfg))
    {
        LOG(WARN, "offline_cfg: Failed obtaining Interface table config");
    }

    return true;
exit_failure:
    gw_offline_cfg_release(cfg);
    return false;
}

/*
 * Determine current uplink if known (in offline mode this may not always be
 * the case) and set current uplink config from saved uplink config.
 */
static bool gw_offline_uplink_config_set_current(const struct gw_offline_cfg *cfg)
{
    char uplink[C_IFNAME_LEN];
    int rc;

    if (gw_offline_uplink_ifname_get(uplink, sizeof(uplink)))
    {
        LOG(INFO, "offline_cfg: Uplink known=%s. Restore uplink settings.", uplink);

        if (cfg->inet_config_uplink == NULL)
        {
            LOG(ERR, "offline_cfg: %s: No saved uplink configuration, cannot restore it", __func__);
            return false;
        }
        LOG(DEBUG, "offline_cfg: Saved uplink config = %s", json_dumps_static(cfg->inet_config_uplink, 0));

        if ((rc = ovsdb_sync_update(
                SCHEMA_TABLE(Wifi_Inet_Config),
                SCHEMA_COLUMN(Wifi_Inet_Config, if_name), uplink,
                json_incref(json_array_get(cfg->inet_config_uplink, 0)))) != 1)
        {
            LOG(ERR, "offline_cfg: Error updating Wifi_Inet_Config for %s, rc=%d", uplink, rc);
            return false;
        }
        LOG(DEBUG, "offline_cfg: Updated %d row(s) in Wifi_Inet_Config -w if_name==%s", rc, uplink);
    }

    return true;
}

/*
 * Clear any settings that were possibly set on a previous uplink if_name.
 */
static bool gw_offline_uplink_config_clear_previous(const char *if_name)
{
    int rc;
    json_t *upnp_mode;

    /*
     * Currently the following settings need to be cleared:
     * - upnp_mode
     */
    upnp_mode = json_pack("{ s : [ s , []] }", "upnp_mode", "set");
    if (upnp_mode == NULL)
    {
        LOG(ERR, "offline_cfg: %s: Error packing json", __func__);
        return false;
    }
    LOG(DEBUG, "offline_cfg: Clear previous uplink %s' upnp_mode config", if_name);

    rc = ovsdb_sync_update(
            SCHEMA_TABLE(Wifi_Inet_Config),
            SCHEMA_COLUMN(Wifi_Inet_Config, if_name),
            if_name,
            upnp_mode);
    if (rc == -1)
    {
        LOG(ERR, "offline_cfg: Error clearing upnp_mode setting in Wifi_Inet_Config for %s",
                 if_name);
        return false;
    }

    return true;
}

/* Check if stored config has IPv6 enabled: */
static bool util_cfg_is_ipv6_enabled(const struct gw_offline_cfg *cfg)
{
    struct gw_offline_ipv6_cfg ipv6_cfg;
    pjs_errmsg_t perr;

    if (!gw_offline_ipv6_cfg_from_json(&ipv6_cfg, cfg->ipv6_cfg, false, perr))
    {
        LOG(ERR, "offline_cfg: Error converting JSON ipv6_cfg to gw_offline_ipv6_cfg structure: %s", perr);
        return false;
    }
    if (ipv6_cfg.routeAdv_is_set && ipv6_cfg.prefix_is_set)
    {
        return true;
    }
    return false;
}

static struct schema_DHCP_Option *util_dhcp_option_get(
        struct schema_DHCP_Option *dhcp_options,
        int n,
        int tag)
{
    struct schema_DHCP_Option *dco;
    int i;

    for (i = 0; i < n; i++)
    {
        dco = &dhcp_options[i];
        if (dco->tag_exists && dco->tag == tag)
        {
            return dco;
        }
    }
    return NULL;
}

static bool util_dhcpv6_optval_get(char *buf, size_t len,
        const struct schema_DHCP_Option *opt, unsigned index)
{
    char opt_val[sizeof(opt->value)];
    char *tok;
    unsigned n;

    STRSCPY(opt_val, opt->value);

    n = 0;
    tok = strtok(opt_val, ", \t\n");
    while (tok != NULL)
    {
        if (n++ == index) goto out;
        tok = strtok(NULL, ", \t\n");

    };
    return false;
out:
    strscpy(buf, tok, len);
    return true;
}

/* Take prefix from DHCPv6 option 26 (in the form 2001:ee2:1704:99ff::/64)
 * and make up a suitable address for LAN_BRIDGE interface. */
static bool util_brlan_addr_from_prefix(char *addr_buf, size_t len, const char *prefix)
{
    char prefix_buf[strlen(prefix)+1];
    char *pref;
    char *pref_len;

    /* strtok() modifies the string, so make a copy first: */
    strscpy(prefix_buf, prefix, strlen(prefix)+1);

    pref = strtok(prefix_buf, "/");
    if (pref == NULL) return false;
    LOG(TRACE, "offline_cfg: %s(): parsed prefix=<%s>", __func__, pref);

    pref_len = strtok(NULL, " ");
    if (pref_len == NULL) return false;
    LOG(TRACE, "offline_cfg: %s(): parsed pref_len=<%s>", __func__, pref_len);

    /* From prefix and prefix_len, make an address for LAN_BRIDGE: */
    snprintf(addr_buf, len, "%s%s/%s", pref, "1", pref_len);
    LOG(TRACE, "offline_cfg: %s(): LAN_BRIDGE address=<%s>", __func__, addr_buf);

    return true;
}

static bool gw_offline_cfg_ipv6_disable()
{
    ovsdb_table_delete(&table_IPv6_Prefix, NULL);
    ovsdb_table_delete(&table_IPv6_RouteAdv, NULL);
    ovsdb_table_delete(&table_DHCPv6_Server, NULL);
    ovsdb_table_delete_where(
            &table_IP_Interface,
            ovsdb_where_simple(SCHEMA_COLUMN(IP_Interface, name), LAN_BRIDGE));

    LOG(INFO, "offline_cfg: Disabled IPv6.");
    return true;
}

static void handle_dhcp_option_update(struct ev_loop *loop, ev_timer *watcher, int revent)
{
    if (gw_offline_stat == status_active)
    {
        /* Provision (either enable or disable) IPv6 according to the
         * currently received DHCPv6 options: */
        gw_offline_cfg_ipv6_provision(&cfg_cache);
    }
}

static void callback_DHCP_Option(
        ovsdb_update_monitor_t *mon,
        struct schema_DHCP_Option *old_rec,
        struct schema_DHCP_Option *dco)
{
    if (mon->mon_type == OVSDB_UPDATE_ERROR)
        return;
    if (gw_offline_stat != status_active) // Ignore if not in active gw_offline mode
        return;

    /* If this is not DHCPv6 and RX-type option, ignore it: */
    if (!(strcmp(dco->version, "v6") == 0 && strcmp(dco->type, "rx") == 0))
    {
        return;
    }
    LOG(DEBUG, "offline_cfg: DHCP_Option change.");

    ev_timer_stop(EV_DEFAULT, &debounce_dco);
    ev_timer_set(&debounce_dco, DEBOUNCE_DHCP_OPTION, 0.0);
    ev_timer_start(EV_DEFAULT, &debounce_dco);
}

static bool gw_offline_monitor_dhcp_option()
{
    static bool inited;

    if (inited) return true;

    OVSDB_TABLE_MONITOR(DHCP_Option, true);

    ev_timer_init(&debounce_dco, handle_dhcp_option_update, DEBOUNCE_DHCP_OPTION, 0.0);

    inited = true;
    return true;
}

static bool gw_offline_cfg_ipv6_provision(const struct gw_offline_cfg *cfg)
{
    struct gw_offline_ipv6_cfg ipv6_cfg;            // Stored IPv6 config
    struct schema_DHCP_Option *dhcp_options = NULL; // all DHCP v6 rx options
    struct schema_DHCP_Option *dco26;               // DHCPv6 opt 26
    struct schema_DHCP_Option *dco23;               // DHCPv6 opt 23
    struct schema_DHCP_Option *dco24;               // DHCPv6 opt 24
    struct schema_IPv6_RouteAdv ipv6_routeAdv;
    struct schema_DHCPv6_Server dhcpv6_server;
    struct schema_IPv6_Prefix ipv6_prefix;
    struct schema_IPv6_Address ipv6_addr;
    struct schema_DHCP_Option dhcp_option;
    struct schema_IP_Interface ip_intf;
    ovs_uuid_t uuid_ip_intf_lanbr;                  //  LAN_BRIDGE uuid of IP_Interface
    pjs_errmsg_t perr;
    json_t *where;
    bool rv = false;
    int n;

    /* Take stored IPv6 config JSON and convert it to gw_offline_ipv6_cfg config struct: */
    if (!gw_offline_ipv6_cfg_from_json(&ipv6_cfg, cfg->ipv6_cfg, false, perr))
    {
        LOG(ERR, "offline_cfg: Error converting JSON ipv6_cfg to gw_offline_ipv6_cfg structure: %s", perr);
        return true;
    }

    /* Check if stored config has IPv6 enabled: */
    if (!(ipv6_cfg.routeAdv_is_set && ipv6_cfg.prefix_is_set))
    {
        LOG(DEBUG, "offline_cfg: IPv6 not enabled in stored config");
        return true;
    }
    LOG(DEBUG, "offline_cfg: IPv6 enabled in stored config");

    /* Get received DHCPv6 options: */
    where = ovsdb_where_multi(
            ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCP_Option, enable), "true", OCLM_BOOL),
            ovsdb_where_simple(SCHEMA_COLUMN(DHCP_Option, type), "rx"),
            ovsdb_where_simple(SCHEMA_COLUMN(DHCP_Option, version), "v6"),
            NULL);

    dhcp_options = ovsdb_table_select_where(&table_DHCP_Option, where, &n);

    /* DHCPv6 option 26 (IAPREFIX) has to be present to provision IPv6: */
    if (dhcp_options == NULL || n == 0 || (dco26 = util_dhcp_option_get(dhcp_options, n, 26)) == NULL)
    {
        LOG(DEBUG, "offline_cfg: No DHCP option 26 currently present. Cannot provision IPv6");
        gw_offline_cfg_ipv6_disable();
        goto out;
    }
    if ((dco23 = util_dhcp_option_get(dhcp_options, n, 23)) == NULL)
    {
        LOG(DEBUG, "offline_cfg: No DHCP option 23 present");
    }
    if ((dco24 = util_dhcp_option_get(dhcp_options, n, 24)) == NULL)
    {
        LOG(DEBUG, "offline_cfg: No DHCP option 24 present");
    }

    LOG(INFO, "offline_cfg: Provisioning IPv6...");

    /* Prepare IPv6_Address for br-home: */
    memset(&ipv6_addr, 0, sizeof(ipv6_addr));
    if (!util_dhcpv6_optval_get(ipv6_addr.prefix, sizeof(ipv6_addr.prefix), dco26, 0))
    {
        LOG(ERR, "offline_cfg: Error parsing prefix from DHCPv6 option 26");
        goto out;
    }
    ipv6_addr.prefix_exists = true;
    if (!util_dhcpv6_optval_get(ipv6_addr.preferred_lifetime, sizeof(ipv6_addr.preferred_lifetime), dco26, 1))
    {
        LOG(ERR, "offline_cfg: Error parsing preferred_lifetime from DHCPv6 option 26");
        goto out;
    }
    ipv6_addr.preferred_lifetime_exists = true;
    if (!util_dhcpv6_optval_get(ipv6_addr.valid_lifetime, sizeof(ipv6_addr.valid_lifetime), dco26, 2))
    {
        LOG(ERR, "offline_cfg: Error parsing valid_lifetime from DHCPv6 option 26");
        goto out;
    }
    ipv6_addr.valid_lifetime_exists = true;
    if (!util_brlan_addr_from_prefix(ipv6_addr.address, sizeof(ipv6_addr.address), ipv6_addr.prefix))
    {
        LOG(ERR, "offline_cfg: Error getting IPv6 lan address from prefix");
        goto out;
    }
    ipv6_addr.address_exists = true;
    STRSCPY(ipv6_addr.status, "enabled");
    ipv6_addr.status_exists = true;
    STRSCPY(ipv6_addr.origin, "static");
    ipv6_addr.origin_exists = true;
    STRSCPY(ipv6_addr.address_status, "preferred");
    ipv6_addr.address_status_exists = true;
    ipv6_addr.enable = true;
    ipv6_addr.enable_exists = true;

    /* Upsert IP_Interface (with primary key == name) row for br-home -- initial values: */
    if (!ovsdb_table_select_one_where(
            &table_IP_Interface,
            ovsdb_where_simple(SCHEMA_COLUMN(IP_Interface, name), LAN_BRIDGE), &ip_intf))
    {
        memset(&ip_intf, 0, sizeof(ip_intf));
        STRSCPY(ip_intf.name, LAN_BRIDGE); // name is immutable, only set it for insert
        ip_intf.name_exists = true;
    }
    else
    {
        memset(&ip_intf, 0, sizeof(ip_intf));
        ip_intf._partial_update = true;
    }
    STRSCPY(ip_intf.if_name, LAN_BRIDGE);
    ip_intf.if_name_exists = true;
    ip_intf.if_name_present = true;
    ip_intf.enable = true;
    ip_intf.enable_exists = true;
    ip_intf.enable_present = true;

    if (!ovsdb_table_upsert_where(
            &table_IP_Interface,
            ovsdb_where_simple(SCHEMA_COLUMN(IP_Interface, name), LAN_BRIDGE),
            &ip_intf, false))
    {
        LOG(ERR, "offline_cfg: IPv6: Error upserting IP_Interface");
        goto out;
    }

    /* Upsert IPv6_Address with parent IP_interface(name==LAN_BRIDGE)::ipv6_addr.
     *
     * This will set global IPv6 address to LAN_BRIDGE interface. */
    if (!ovsdb_table_upsert_with_parent_where(
            &table_IPv6_Address,
            ovsdb_where_simple(SCHEMA_COLUMN(IPv6_Address, address), ipv6_addr.address),
            &ipv6_addr,
            false,
            NULL,
            SCHEMA_TABLE(IP_Interface),
            ovsdb_where_simple(SCHEMA_COLUMN(IP_Interface, name), LAN_BRIDGE),
            SCHEMA_COLUMN(IP_Interface, ipv6_addr)))
    {
        LOG(ERROR, "offline_cfg: Error upserting IPv6_Address with parent IP_Interface");
        goto out;
    }

    /* IP_Interface (if_name==LAN_BRIDGE) uuid: (needed later to set references): */
    if (!ovsdb_sync_get_uuid(
            SCHEMA_TABLE(IP_Interface),
            SCHEMA_COLUMN(IP_Interface, if_name),
            LAN_BRIDGE,
            &uuid_ip_intf_lanbr))
    {
        LOG(ERR, "offline_cfg: Error getting row for lan bridge uuid in IP_Interface");
        goto out;
    }

    /* IPv6_RouteAdv: intial values: */
    memset(&ipv6_routeAdv, 0, sizeof(ipv6_routeAdv));
    ipv6_routeAdv._partial_update = true;

    ipv6_routeAdv.interface = uuid_ip_intf_lanbr;      // on LAN_BRIDGE IP_Interface
    ipv6_routeAdv.interface_exists = true;
    ipv6_routeAdv.interface_present = true;
    ipv6_routeAdv.managed = ipv6_cfg.routeAdv_managed; // managed flag from config
    ipv6_routeAdv.managed_exists = true;
    ipv6_routeAdv.managed_present = true;
    STRSCPY(ipv6_routeAdv.preferred_router, "high");
    ipv6_routeAdv.preferred_router_exists = true;
    ipv6_routeAdv.preferred_router_present = true;
    ipv6_routeAdv.other_config = ipv6_cfg.routeAdv_other_config; // other_config flag from config
    ipv6_routeAdv.other_config_exists = true;
    ipv6_routeAdv.other_config_present = true;
    STRSCPY(ipv6_routeAdv.status, "enabled");
    ipv6_routeAdv.status_exists = true;
    ipv6_routeAdv.status_present = true;
    if (dco24 != NULL)
    {
        int n_max = sizeof(ipv6_routeAdv.dnssl) / sizeof(ipv6_routeAdv.dnssl[n]);
        n = 0;
        while (n < n_max && util_dhcpv6_optval_get(ipv6_routeAdv.dnssl[n], sizeof(ipv6_routeAdv.dnssl[n]), dco24, n))
        {
            n++;
            ipv6_routeAdv.dnssl_len = n;
        }
    }
    ipv6_routeAdv.dnssl_present = true;

    if (!ovsdb_table_upsert_where(
            &table_IPv6_RouteAdv,
            ovsdb_where_simple_typed(SCHEMA_COLUMN(IPv6_RouteAdv, interface), &uuid_ip_intf_lanbr, OCLM_UUID),
            &ipv6_routeAdv,
            false))
    {
        LOG(ERR, "offline_cfg: Error inserting IPv6_RouteAdv");
        goto out;
    }

    /* DHCPv6_Server */
    if (ipv6_cfg.dhcpv6_server_is_set) // if config has DHCPv6_Server configured
    {
        /* DHCPv6_Server initial upsert: */
        memset(&dhcpv6_server, 0, sizeof(dhcpv6_server));
        dhcpv6_server._partial_update = true;

        dhcpv6_server.prefix_delegation = false;
        dhcpv6_server.prefix_delegation_exists = true;
        dhcpv6_server.prefix_delegation_present = true;
        STRSCPY(dhcpv6_server.status, "enabled");
        dhcpv6_server.status_exists = true;
        dhcpv6_server.status_present = true;
        dhcpv6_server.interface = uuid_ip_intf_lanbr; // on LAN_BRIDGE IP_Interface
        dhcpv6_server.interface_exists = true;
        dhcpv6_server.interface_present = true;
        dhcpv6_server.options_present = true;

        if (!ovsdb_table_upsert_where(
                &table_DHCPv6_Server,
                ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCPv6_Server, interface), &uuid_ip_intf_lanbr, OCLM_UUID),
                &dhcpv6_server,
                false))
        {
            LOG(ERR, "offline_cfg: Error upserting DHCPv6_Server");
            goto out;
        }

        /* For received DHCPv6 option 23, configure a corresponding TX DHCPv6 option 23: */
        if (dco23 != NULL)
        {
            memset(&dhcp_option, 0, sizeof(dhcp_option));
            STRSCPY(dhcp_option.version, "v6");
            dhcp_option.version_exists = true;
            dhcp_option.tag = 23;
            dhcp_option.tag_exists = true;
            STRSCPY(dhcp_option.value, dco23->value); // copy value from RX DHCPv6 option 23
            dhcp_option.value_exists = true;
            STRSCPY(dhcp_option.type, "tx");          // TX option type
            dhcp_option.type_exists = true;

            where = ovsdb_where_multi(
                    ovsdb_where_simple(SCHEMA_COLUMN(DHCP_Option, type), "tx"),
                    ovsdb_where_simple(SCHEMA_COLUMN(DHCP_Option, version), "v6"),
                    ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCP_Option, value), "23", OCLM_INT),
                    NULL);

            /* Upsert DHCP_Option row with parent DHCPv6_Server(interface==LAN_BRIDGE)::options: */
            if (!ovsdb_table_upsert_with_parent_where(
                    &table_DHCP_Option,
                    where,
                    &dhcp_option,
                    false,
                    NULL,
                    SCHEMA_TABLE(DHCPv6_Server),
                    ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCPv6_Server, interface), &uuid_ip_intf_lanbr, OCLM_UUID),
                    SCHEMA_COLUMN(DHCPv6_Server, options)))
            {
                LOG(ERROR, "offline_cfg: Error upserting DHCP_Option (23) with parent DHCPv6_Server");
                goto out;
            }
        }

        /* For received DHCPv6 option 24, configure a corresponding TX DHCPv6 option 24: */
        if (dco24 != NULL)
        {
            memset(&dhcp_option, 0, sizeof(dhcp_option));
            STRSCPY(dhcp_option.version, "v6");
            dhcp_option.version_exists = true;
            dhcp_option.tag = 24;
            dhcp_option.tag_exists = true;
            STRSCPY(dhcp_option.value, dco24->value); // copy value from RX DHCPv6 option 24
            dhcp_option.value_exists = true;
            STRSCPY(dhcp_option.type, "tx");          // TX option type
            dhcp_option.type_exists = true;

            where = ovsdb_where_multi(
                    ovsdb_where_simple(SCHEMA_COLUMN(DHCP_Option, type), "tx"),
                    ovsdb_where_simple(SCHEMA_COLUMN(DHCP_Option, version), "v6"),
                    ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCP_Option, value), "24", OCLM_INT),
                    NULL);

            /* Upsert DHCP_Option row with parent DHCPv6_Server(interface==LAN_BRIDGE)::options: */
            if (!ovsdb_table_upsert_with_parent_where(
                    &table_DHCP_Option,
                    where,
                    &dhcp_option,
                    false,
                    NULL,
                    SCHEMA_TABLE(DHCPv6_Server),
                    ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCPv6_Server, interface), &uuid_ip_intf_lanbr, OCLM_UUID),
                    SCHEMA_COLUMN(DHCPv6_Server, options)))
            {
                LOG(ERROR, "offline_cfg: Error upserting DHCP_Option (24) with parent DHCPv6_Server");
                goto out;
            }
        }

    }

    /* Configure rdnss: */
    if (dco23 != NULL)
    {
        memset(&ipv6_addr, 0, sizeof(ipv6_addr));
        if (!util_dhcpv6_optval_get(ipv6_addr.prefix, sizeof(ipv6_addr.prefix), dco26, 0))
        {
            LOG(ERR, "offline_cfg: Error parsing prefix from DHCPv6 option 26");
            goto out;
        }
        ipv6_addr.prefix_exists = true;
        if (!util_dhcpv6_optval_get(ipv6_addr.preferred_lifetime, sizeof(ipv6_addr.preferred_lifetime), dco26, 1))
        {
            LOG(ERR, "offline_cfg: Error parsing preferred_lifetime from DHCPv6 option 26");
            goto out;
        }
        ipv6_addr.preferred_lifetime_exists = true;
        if (!util_dhcpv6_optval_get(ipv6_addr.valid_lifetime, sizeof(ipv6_addr.valid_lifetime), dco26, 2))
        {
            LOG(ERR, "offline_cfg: Error parsing valid_lifetime from DHCPv6 option 26");
            goto out;
        }
        ipv6_addr.valid_lifetime_exists = true;
        if (!util_dhcpv6_optval_get(ipv6_addr.address, sizeof(ipv6_addr.address), dco23, 0))
        {
            LOG(ERR, "offline_cfg: Error parsing value from DHCPv6 option 23");
            goto out;
        }
        ipv6_addr.address_exists = true;
        STRSCPY(ipv6_addr.status, "enabled");
        ipv6_addr.status_exists = true;
        STRSCPY(ipv6_addr.origin, "static");
        ipv6_addr.origin_exists = true;
        STRSCPY(ipv6_addr.address_status, "inaccessible");
        ipv6_addr.address_status_exists = true;
        ipv6_addr.enable = true;
        ipv6_addr.enable_exists = true;

        /* Upsert IPv6_Address with parent IPv6_RouteAdv(interface==LAN_BRIDGE)::rdnss */
        if (!ovsdb_table_upsert_with_parent_where(
                &table_IPv6_Address,
                ovsdb_where_simple(SCHEMA_COLUMN(IPv6_Address, address), ipv6_addr.address),
                &ipv6_addr,
                false,
                NULL,
                SCHEMA_TABLE(IPv6_RouteAdv),
                ovsdb_where_simple_typed(SCHEMA_COLUMN(IPv6_RouteAdv, interface), &uuid_ip_intf_lanbr, OCLM_UUID),
                SCHEMA_COLUMN(IPv6_RouteAdv, rdnss)))
        {
            LOG(ERROR, "offline_cfg: Error upserting IPv6_Address with parent IPv6_RouteAdv");
            goto out;
        }
    }

    /* Configure IPv6_Prefix: */
    memset(&ipv6_prefix, 0, sizeof(ipv6_prefix));
    if (!util_dhcpv6_optval_get(ipv6_prefix.address, sizeof(ipv6_prefix.address), dco26, 0))
    {
        LOG(ERR, "offline_cfg: Error parsing prefix from DHCPv6 option 26");
        goto out;
    }
    ipv6_prefix.address_exists = true;
    if (!util_dhcpv6_optval_get(ipv6_prefix.preferred_lifetime, sizeof(ipv6_prefix.preferred_lifetime), dco26, 1))
    {
        LOG(ERR, "offline_cfg: Error parsing preferred_lifetime from DHCPv6 option 26");
        goto out;
    }
    ipv6_prefix.preferred_lifetime_exists = true;
    if (!util_dhcpv6_optval_get(ipv6_prefix.valid_lifetime, sizeof(ipv6_prefix.valid_lifetime), dco26, 2))
    {
        LOG(ERR, "offline_cfg: Error parsing valid_lifetime from DHCPv6 option 26");
        goto out;
    }
    ipv6_prefix.valid_lifetime_exists = true;
    ipv6_prefix.on_link = true;
    ipv6_prefix.on_link_exists = true;
    STRSCPY(ipv6_prefix.prefix_status, "preferred");
    ipv6_prefix.prefix_status_exists = true;
    STRSCPY(ipv6_prefix.static_type, "static");
    ipv6_prefix.static_type_exists = true;
    STRSCPY(ipv6_prefix.origin, "ra");
    ipv6_prefix.origin_exists = true;
    ipv6_prefix.autonomous = ipv6_cfg.prefix_autonomous; // from stored config
    ipv6_prefix.autonomous_exists = true;
    ipv6_prefix.enable = true;
    ipv6_prefix.enable_exists = true;

    /* Configure prefix to DHCPv6_Server if config demands it: */
    if (ipv6_cfg.dhcpv6_server_is_set && ipv6_cfg.dhcpv6_server_prefixes_is_set)
    {
        /* Upsert IPv6_Prefix (primary key interface)
         * with parent DHCPv6_Server(interface==LAN_BRIDGE)::prefixes: */
        if (!ovsdb_table_upsert_with_parent(
                &table_IPv6_Prefix,
                &ipv6_prefix,
                false,
                NULL,
                SCHEMA_TABLE(DHCPv6_Server),
                ovsdb_where_simple_typed(SCHEMA_COLUMN(DHCPv6_Server, interface), &uuid_ip_intf_lanbr, OCLM_UUID),
                SCHEMA_COLUMN(DHCPv6_Server, prefixes)))
        {
            LOG(ERROR, "offline_cfg: Error upserting IPv6_Prefix with parent DHCPv6_Server");
            goto out;
        }
    }

    /* Configure prefix to IPv6_RouteAdv if config demands it: */
    if (ipv6_cfg.routeAdv_prefixes_is_set)
    {
        /* Upsert IPv6_Prefix (primary key interface)
         * with parent IPv6_RouteAdv(interface==LAN_BRIDGE)::prefixes: */
        if (!ovsdb_table_upsert_with_parent(
                &table_IPv6_Prefix,
                &ipv6_prefix,
                false,
                NULL,
                SCHEMA_TABLE(IPv6_RouteAdv),
                ovsdb_where_simple_typed(SCHEMA_COLUMN(IPv6_RouteAdv, interface), &uuid_ip_intf_lanbr, OCLM_UUID),
                SCHEMA_COLUMN(IPv6_RouteAdv, prefixes)))
        {
            LOG(ERROR, "offline_cfg: Error upserting IPv6_Prefix with parent IPv6_RouteAdv");
            goto out;
        }
    }

    /* IPv6 provisioning done. */
    LOG(INFO, "offline_cfg: IPv6 enabled and provisioned.");
    rv = true;
out:
    FREE(dhcp_options);
    return rv;
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
        const char *if_name = json_string_value(json_object_get(row, "if_name"));

        /* Insert a row for this VIF: */
        if (ovsdb_sync_insert("Wifi_VIF_Config", json_incref(row), &uuid))
        {
            /* Add created vif uuid to Wifi_Radio_Config... */
            const char *radio_if_name = json_string_value(json_array_get(cfg->radio_if_names, index));
            LOG(DEBUG, "offline_cfg:   if_name=%s is at radio_if_name=%s", if_name, radio_if_name);
            ovsdb_add_uuid_to_radio_config(radio_if_name, uuid);
        }
        else
        {
            LOG(ERR, "offline_cfg: Error inserting into Wifi_VIF_Config: row=%s", json_dumps_static(row, 0));
            return false;
        }
    }

    LOG(DEBUG, "offline_cfg: Restoring Bridge/Port/Interface ...");
    if (!gw_offline_cfg_home_restore_home_bridge(cfg))
    {
        LOG(ERR, "offline_cfg: Error restoring home bridge config");
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

    /* Configure vlan interfaces in Wifi_Inet_Config... */
    json_array_foreach(cfg->inet_config_vlan, index, row)
    {
        const char *if_name = json_string_value(json_object_get(row, SCHEMA_COLUMN(Wifi_Inet_Config, if_name)));

        if (!ovsdb_sync_upsert(
                SCHEMA_TABLE(Wifi_Inet_Config),
                SCHEMA_COLUMN(Wifi_Inet_Config, if_name),
                if_name,
                json_incref(row), NULL))
        {
            LOG(ERR, "offline_cfg: Error upserting into Wifi_Inet_Config: row=%s", json_dumps_static(row, 0));
            return false;
        }
    }

    /* Configure IGMP Config: */
    rc = ovsdb_sync_delete_where(SCHEMA_TABLE(IGMP_Config), NULL);
    if (rc == -1)
    {
        LOG(ERR, "offline_cfg: Error deleting IGMP_Config");
        return false;
    }
    json_array_foreach(cfg->igmp_config, index, row)
    {
        if (!ovsdb_sync_insert(SCHEMA_TABLE(IGMP_Config), json_incref(row), NULL))
        {
            LOG(ERR, "offline_cfg: Error inserting into IGMP_Config: row=%s", json_dumps_static(row, 0));
            return false;
        }
    }

    /* Configure MLD Config: */
    rc = ovsdb_sync_delete_where(SCHEMA_TABLE(MLD_Config), NULL);
    if (rc == -1)
    {
        LOG(ERR, "offline_cfg: Error deleting MLD_Config");
        return false;
    }
    json_array_foreach(cfg->mld_config, index, row)
    {
        if (!ovsdb_sync_insert(SCHEMA_TABLE(MLD_Config), json_incref(row), NULL))
        {
            LOG(ERR, "offline_cfg: Error inserting into MLD_Config: row=%s", json_dumps_static(row, 0));
            return false;
        }
    }

    /* Configure Wifi_Route_Config: */
    rc = ovsdb_sync_delete_where(SCHEMA_TABLE(Wifi_Route_Config), NULL);
    if (rc == -1)
    {
        LOG(ERR, "offline_cfg: Error deleting Wifi_Route_Config");
        return false;
    }
    json_array_foreach(cfg->route_config, index, row)
    {
        if (!ovsdb_sync_insert(SCHEMA_TABLE(Wifi_Route_Config), json_incref(row), NULL))
        {
            LOG(ERR, "offline_cfg: Error inserting into Wifi_Route_Config: row=%s", json_dumps_static(row, 0));
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

    /* Replay Openflow_Config */
    rc = ovsdb_sync_delete_where(SCHEMA_TABLE(Openflow_Config), NULL);
    if (rc == -1)
    {
        LOG(ERR, "offline_cfg: Error deleting Openflow_Config");
        return false;
    }
    json_array_foreach(cfg->openflow_config, index, row)
    {
        if (!ovsdb_sync_insert(SCHEMA_TABLE(Openflow_Config), json_incref(row), NULL))
        {
            LOG(ERR, "offline_cfg: Error inserting into Openflow_Config: row=%s", json_dumps_static(row, 0));
            return false;
        }
    }

    /* Replay Openflow_Tag */
    rc = ovsdb_sync_delete_where(SCHEMA_TABLE(Openflow_Tag), NULL);
    if (rc == -1)
    {
        LOG(ERR, "offline_cfg: Error deleting Openflow_Tag");
        return false;
    }
    json_array_foreach(cfg->openflow_tag, index, row)
    {
        if (!ovsdb_sync_insert(SCHEMA_TABLE(Openflow_Tag), json_incref(row), NULL))
        {
            LOG(ERR, "offline_cfg: Error inserting into Openflow_Tag: row=%s", json_dumps_static(row, 0));
            return false;
        }
    }

    /* Replay Openflow_Tag_Group */
    rc = ovsdb_sync_delete_where(SCHEMA_TABLE(Openflow_Tag_Group), NULL);
    if (rc == -1)
    {
        LOG(ERR, "offline_cfg: Error deleting Openflow_Tag_Group");
        return false;
    }
    json_array_foreach(cfg->openflow_tag_group, index, row)
    {
        if (!ovsdb_sync_insert(SCHEMA_TABLE(Openflow_Tag_Group), json_incref(row), NULL))
        {
            LOG(ERR, "offline_cfg: Error inserting into Openflow_Tag_Group: row=%s", json_dumps_static(row, 0));
            return false;
        }
    }

    /* Replay Netfilter */
    rc = ovsdb_sync_delete_where(SCHEMA_TABLE(Netfilter), NULL);
    if (rc == -1)
    {
        LOG(ERR, "offline_cfg: Error deleting Netfilter");
        return false;
    }
    json_array_foreach(cfg->netfilter, index, row)
    {
        if (!ovsdb_sync_insert(SCHEMA_TABLE(Netfilter), json_incref(row), NULL))
        {
            LOG(ERR, "offline_cfg: Error inserting into Netfilter: row=%s", json_dumps_static(row, 0));
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

    /* Determine current uplink from OVSDB (only if it is eth type): */
    json = ovsdb_sync_select_where(SCHEMA_TABLE(Connection_Manager_Uplink),
               ovsdb_where_multi(
                   ovsdb_where_simple_typed(SCHEMA_COLUMN(Connection_Manager_Uplink, is_used), "true", OCLM_BOOL),
                   ovsdb_where_simple(SCHEMA_COLUMN(Connection_Manager_Uplink, if_type), "eth"),
                   NULL)
               );

    if (json == NULL || json_array_size(json) != 1)
        goto err_out;

    if_name = json_string_value(json_object_get(json_array_get(json, 0), "if_name"));
    if (if_name != NULL)
        strscpy(if_name_buf, if_name, len);

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
    char uplink[C_IFNAME_LEN];

    // Config must be BRIDGE config, otherwise ignore:
    if (!gw_offline_cfg_is_bridge(cfg))
        return false;

    // Determine uplink interface name:
    if (!gw_offline_uplink_ifname_get(uplink, sizeof(uplink)))
    {
        LOG(WARN, "offline_cfg: Cannot determine GW's uplink interface");
        return false;
    }
    LOG(INFO, "offline_cfg: This GW's uplink interface=%s", uplink);

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
        {
            LOG(WARN, "offline_cfg: No inet_config home APs rows in stored config");
        }
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
    if (gw_offline_stat == status_active)
    {
        LOG(DEBUG, "offline_cfg: %s(): active gw_offline mode. Ignoring.", __func__);
        return true;
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
        pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_READY, false);
    else
        pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ERROR, false);

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

    /* Restore uplink config from Wifi_Inet_Config to current uplink: */
    if (!gw_offline_uplink_config_set_current(&gw_cfg))
    {
        LOG(ERR, "offline_cfg: Error restoring uplink's settings for the current uplink");
        goto exit;
    }

    /* IPv6 configuration and provisioning (if IPv6 enabled in stored config): */
    if (util_cfg_is_ipv6_enabled(&gw_cfg))
    {
        /* Monitor DHCP_Option table. */
        gw_offline_monitor_dhcp_option();
    }

    rv = true;
exit:
    gw_offline_cfg_release(&gw_cfg);

    if (rv)
    {
        /* Indicate in Node_State that the feature is "active"
         * (enabled && ps config applied): */
        pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ACTIVE, false);
    }
    else
    {
        /* Indicate error in Node_State: */
        pm_node_state_set(KEY_OFFLINE_STATUS, VAL_STATUS_ERROR, false);
    }

    return rv;
}
