#include <stdarg.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>

#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "ovsdb_cache.h"
#include "schema.h"
#include "log.h"
#include "json_util.h"

#include "osn_types.h"
#include "nm2_ripv2.h"
#include "nm2_iface.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB
#define RIPv2_MAX_CMD_LEN 256
#define RIPv2_LOG_FILE "/var/etc/quagga/messages"
#define RIPv2_AUTH_KEY_INDEX 0

/*
 * ===========================================================================
 *  Global variables
 * ===========================================================================
 */

static ovsdb_table_t table_RIPv2_Global_Config;
static ovsdb_table_t table_RIPv2_Interface_Config;
static ovsdb_table_t table_RIPv2_Global_State;
static ovsdb_table_t table_RIPv2_Interface_State;
static ovsdb_table_t table_Wifi_Inet_State;

static bool wanIntfIP = false;
static bool RIPv2_global_enable = false;
static bool RIPv2_intf_enable = false;
static char ripv2_status[5] = "down";
static struct schema_RIPv2_Global_Config RIPv2_global_config_local = {0};
static struct schema_RIPv2_Interface_Config RIPv2_interface_config_local = {0};
static struct schema_RIPv2_Interface_State RIPv2_interface_state = {0};

/*
 * ===========================================================================
 *  Forward declarations
 * ===========================================================================
 */
static void callback_RIPv2_Global_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_RIPv2_Global_Config *old_rec,
        struct schema_RIPv2_Global_Config *conf);
static void callback_RIPv2_Interface_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_RIPv2_Interface_Config *old_rec,
        struct schema_RIPv2_Interface_Config *conf);
static void callback_Wifi_Inet_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Inet_State *old_rec,
        struct schema_Wifi_Inet_State *iconf);

static bool nm2_ripv2_global_state_update(struct schema_RIPv2_Global_Config *conf);
static bool nm2_ripv2_interface_state_update(struct schema_RIPv2_Interface_Config *conf);

static void nm2_set_ripv2_global_enable(bool val)
{
    RIPv2_global_enable = val;
    return;
}

static bool nm2_is_ripv2_global_enabled(void)
{
    return RIPv2_global_enable;
}

static void nm2_set_ripv2_intf_enable(bool val)
{
    RIPv2_intf_enable = val;
    return;
}

static bool nm2_is_ripv2_intf_enabled(void)
{
    return RIPv2_intf_enable;
}

void nm2_ripd_start(void)
{
    char cmd[RIPv2_MAX_CMD_LEN] = {0};

    if (!strncmp("up", ripv2_status, sizeof("up")))
        strscpy(cmd, "/etc/init.d/quagga restart", sizeof(cmd));
    else
        strscpy(cmd, "/etc/init.d/quagga start", sizeof(cmd));
    
    if (-1 == system(cmd))
    {
        LOG(ERR, "nm2_ripv2_config: Failed to start ripd");
        return;
    }

    strscpy(ripv2_status, "up", sizeof(ripv2_status));

    return;
}

void nm2_ripd_stop(void)
{
    char cmd[RIPv2_MAX_CMD_LEN] ={0};

    strscpy(cmd,"/etc/init.d/quagga stop", sizeof(cmd));
    
    if (-1 == system(cmd))
    {
        LOG(ERR, "nm2_ripv2_config: Failed to stop ripd");
        return;
    }

    strscpy(ripv2_status, "down", sizeof(ripv2_status));

    return;
}

bool nm2_ripv2_config_write(void)
{
    FILE *fconf;
    bool ret = true;
    struct schema_RIPv2_Interface_Config *intf_conf;
    void *conf_p;
    int count = 0;
    int i;

    /*
     * Write out the RIPv2 configuration file
     */
    fconf = fopen(CONFIG_RIPV2_ETC_PATH, "w");
    if (fconf == NULL)
    {
        LOG(ERR, "nm2_ripv2_config: Error opening config file for writing: %s", CONFIG_RIPV2_ETC_PATH);
        ret = false;
        goto exit;
    }

    fprintf(fconf, "!\n");
    fprintf(fconf, "log file %s\n", RIPv2_LOG_FILE);
    fprintf(fconf, "log syslog errors\n");
    fprintf(fconf, "!\n");
    fprintf(fconf, "debug rip events\n");
    fprintf(fconf, "!\n");

    if (true == nm2_is_ripv2_intf_enabled())
    {
        if (strncmp(RIPv2_interface_config_local.auth_key, "", sizeof(RIPv2_interface_config_local.auth_key)))
        {
            if (!strncmp(RIPv2_interface_config_local.auth_type, "crypt", sizeof("crypt")))
            {
                fprintf(fconf, "key chain RIP\n");
                fprintf(fconf, " key %d\n", RIPv2_AUTH_KEY_INDEX);
                fprintf(fconf, "  key-string %s\n", RIPv2_interface_config_local.auth_key);
                fprintf(fconf, "!\n");
            }
        }
        if (strncmp(RIPv2_interface_config_local.interface, "", sizeof(RIPv2_interface_config_local.interface)))
        {
            fprintf(fconf, "interface %s\n", RIPv2_interface_config_local.interface);

            if (!strncmp(RIPv2_interface_config_local.auth_type, "crypt", sizeof("crypt")))
            {
                fprintf(fconf, " ip rip authentication mode md5 auth-length old-ripd\n");
                if (strncmp (RIPv2_interface_config_local.auth_key, "", sizeof(RIPv2_interface_config_local.auth_key)))
                    fprintf(fconf, " ip rip authentication key-chain RIP\n");
            }
            else if (!strncmp(RIPv2_interface_config_local.auth_type, "simple", sizeof("simple")))
            {
                fprintf(fconf, " ip rip authentication mode text\n");
                if (strncmp (RIPv2_interface_config_local.auth_key, "", sizeof(RIPv2_interface_config_local.auth_key)))
                    fprintf(fconf, " ip rip authentication string %s\n", RIPv2_interface_config_local.auth_key);
            }
            // Do nothing for auth type = none

            fprintf(fconf, "!\n");
        }
    }

    fprintf(fconf, "router rip\n");
    fprintf(fconf, " version 2\n");

    fprintf(fconf, " network %s\n", CONFIG_TARGET_WAN_BRIDGE_NAME);

    if (true == nm2_is_ripv2_intf_enabled())
    {
        conf_p = ovsdb_table_select_where(&table_RIPv2_Interface_Config,
                                          NULL,
                                          &count);
        for (i = 0; i < count; i++) {
            intf_conf = (struct schema_RIPv2_Interface_Config *) (conf_p + table_RIPv2_Interface_Config.schema_size * i);
            if (strncmp (intf_conf->network, "", sizeof(intf_conf->network)))
                fprintf(fconf, " network %s\n", intf_conf->network);
            fprintf(fconf, " passive-interface %s%d\n", "br-home.public", i+1);
        }
        if (conf_p)
            FREE(conf_p);
    }

    if (strncmp (RIPv2_global_config_local.redistribute, "", sizeof(RIPv2_global_config_local.redistribute)))
        fprintf(fconf, " redistribute %s\n", RIPv2_global_config_local.redistribute);

    fprintf(fconf, "!\n");

    fflush(fconf);

exit:
    if (fconf != NULL) fclose(fconf);

    return ret;
}

bool nm2_ripv2_global_state_update(struct schema_RIPv2_Global_Config *conf)
{
    struct schema_RIPv2_Global_State RIPv2_global_state = {0};
    bool ripv2_global_enable = nm2_is_ripv2_global_enabled();

    SCHEMA_SET_BOOL(RIPv2_global_state.enable, ripv2_global_enable);

    if (strncmp(conf->network, "", sizeof(conf->network)))
        SCHEMA_SET_STR(RIPv2_global_state.network, conf->network);

    if (strncmp(conf->redistribute, "", sizeof(conf->redistribute)))
        SCHEMA_SET_STR(RIPv2_global_state.redistribute, conf->redistribute);

    RIPv2_global_state._partial_update = true;
    if (!ovsdb_table_upsert(&table_RIPv2_Global_State, &RIPv2_global_state, false))
        LOG(ERR, "nm2_ripv2_config: Unable to update RIPv2_Global_State.");

    return true;
}

bool nm2_ripv2_interface_state_update(struct schema_RIPv2_Interface_Config *conf)
{
    struct schema_RIPv2_Interface_State RIPv2_interface_state = {0};
    bool ripv2_intf_enable = nm2_is_ripv2_intf_enabled();
    json_t *where;

    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("network", OFUNC_EQ, conf->network));

    SCHEMA_SET_BOOL(RIPv2_interface_state.enable, ripv2_intf_enable);

    if (strncmp(conf->interface, "", sizeof(conf->interface)))
        SCHEMA_SET_STR(RIPv2_interface_state.interface, conf->interface);

    if (strncmp(conf->network, "", sizeof(conf->network)))
        SCHEMA_SET_STR(RIPv2_interface_state.network, conf->network);

    SCHEMA_SET_BOOL(RIPv2_interface_state.passive, conf->passive);

    if (conf->cost)
        SCHEMA_SET_INT(RIPv2_interface_state.cost, conf->cost);

    if (strncmp(conf->auth_type, "", sizeof(conf->auth_type)))
        SCHEMA_SET_STR(RIPv2_interface_state.auth_type, conf->auth_type);

    if (strncmp(conf->auth_key, "", sizeof(conf->auth_key)))
        SCHEMA_SET_STR(RIPv2_interface_state.auth_key, conf->auth_key);

    if (strncmp(ripv2_status, "", sizeof(ripv2_status)))
        SCHEMA_SET_STR(RIPv2_interface_state.status, ripv2_status);

    RIPv2_interface_state._partial_update = true;
    if (!ovsdb_table_upsert_where(&table_RIPv2_Interface_State, where, &RIPv2_interface_state, false))
        LOG(ERR, "nm2_ripv2_config: Unable to update RIPv2_Interface_State.");

    return true;
}

/*
 * OVSDB RIPv2_Global_Config table update handler
 */
void callback_RIPv2_Global_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_RIPv2_Global_Config *old_rec,
        struct schema_RIPv2_Global_Config *conf)
{
    struct schema_RIPv2_Global_State RIPv2_global_state = {0};

    nm2_set_ripv2_global_enable(conf->enable);

    switch(mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            memcpy(&RIPv2_global_config_local, conf, sizeof(struct schema_RIPv2_Global_Config));

            if(true == nm2_is_ripv2_global_enabled())
            {
                if (!nm2_ripv2_config_write())
                    return;
                nm2_ripd_start();
            }
            else
            {
                nm2_ripd_stop();
                nm2_ripv2_nfm_rules_del();
            }

            nm2_ripv2_global_state_update(conf);
            break;
        case OVSDB_UPDATE_DEL:
            MEMZERO(RIPv2_global_config_local);

            nm2_ripd_stop();
            nm2_ripv2_nfm_rules_del();

            if(!ovsdb_table_delete(&table_RIPv2_Global_State, old_rec))
                LOG(ERR, "nm2_ripv2_config: Unable to delete RIPv2_Global_State.");

            break;
        case OVSDB_UPDATE_ERROR:
        default:
            LOG(ERR, "nm2_ripv2_config: Invalid RIPv2_Global_Config mon_type(%d)", mon->mon_type);
    }

    if(true == wanIntfIP && (true == nm2_is_ripv2_global_enabled()))
    {
        nm2_ripv2_nfm_rules_del();
        if(!nm2_ripv2_nfm_rules_add())
            LOG(ERR, "nm2_ripv2_config: Unable to update Netfilter.");
    }

    SCHEMA_SET_STR(RIPv2_interface_state.status, ripv2_status);

    RIPv2_interface_state._partial_update = true;
    if(!ovsdb_table_update(&table_RIPv2_Interface_State, &RIPv2_interface_state))
        LOG(ERR, "nm2_ripv2_config: Unable to update RIPv2_Interface_State.");

    return;
}

/*
 * OVSDB RIPv2_Interface_Config table update handler
 */
void callback_RIPv2_Interface_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_RIPv2_Interface_Config *old_rec,
        struct schema_RIPv2_Interface_Config *conf)
{
    MEMZERO(RIPv2_interface_state);

    nm2_set_ripv2_intf_enable(conf->enable);

    switch(mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            memcpy(&RIPv2_interface_config_local, conf, sizeof(struct schema_RIPv2_Interface_Config));

            if (true == nm2_is_ripv2_global_enabled())
            {
                if (!nm2_ripv2_config_write())
                    return;
                nm2_ripd_start();
            } else
                LOG(DEBUG, "nm2_ripv2_config: RIPv2_Global_Config is disabled");

            nm2_ripv2_interface_state_update(conf);

            break;
        case OVSDB_UPDATE_DEL:
            MEMZERO(RIPv2_interface_config_local);

            if (true == nm2_is_ripv2_global_enabled())
            {
                if (!nm2_ripv2_config_write())
                    return;
                nm2_ripd_start();
            }
        
            if (!ovsdb_table_delete_simple(
                        &table_RIPv2_Interface_State,
                        SCHEMA_COLUMN(RIPv2_Interface_State, network),
                        old_rec->network))
                LOG(ERR, "nm2_ripv2_config: Unable to delete RIPv2_Interface_State.");
            break;
        case OVSDB_UPDATE_ERROR:
        default:
            LOG(ERR, "nm2_ripv2_config: Invalid RIPv2_Interface_Config mon_type(%d)", mon->mon_type);
    }

    if((true == wanIntfIP) && (true == nm2_is_ripv2_global_enabled()))
    {
        nm2_ripv2_nfm_rules_del();
        if(!nm2_ripv2_nfm_rules_add())
            LOG(ERR, "nm2_ripv2_config: Unable to update Netfilter.");
    }

    return;
}

/*
 * OVSDB Wifi_Inet_State table update handler
 */
void callback_Wifi_Inet_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_Inet_State *old_rec,
        struct schema_Wifi_Inet_State *conf)
{
    switch(mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            if(!strncmp(conf->if_name, CONFIG_TARGET_WAN_BRIDGE_NAME, sizeof(conf->if_name)))
            {
                if(!strncmp(conf->inet_addr,"0.0.0.0", sizeof(conf->inet_addr)))
                {
                    nm2_ripv2_nfm_rules_del();
                    wanIntfIP = false;
                } else {
                    LOG(DEBUG, "nm2_ripv2_config: WAN interface %s acquired IPv4 address %s", CONFIG_TARGET_WAN_BRIDGE_NAME, conf->inet_addr);
                    wanIntfIP = true;
                }
            }
            if(!strncmp(conf->if_name, CONFIG_STATIC_IP_LAN_INTF1, sizeof(conf->if_name))
                || !strncmp(conf->if_name, CONFIG_STATIC_IP_LAN_INTF2, sizeof(conf->if_name))
                || !strncmp(conf->if_name, CONFIG_STATIC_IP_LAN_INTF3, sizeof(conf->if_name)))
            {
                if(!strncmp(conf->inet_addr,"0.0.0.0", sizeof(conf->inet_addr)))
                    nm2_ripv2_nfm_rules_del();
            }
            break;
        case OVSDB_UPDATE_DEL:
            if (!strncmp(old_rec->if_name, CONFIG_TARGET_WAN_BRIDGE_NAME, sizeof(old_rec->if_name)))
            {
                nm2_ripv2_nfm_rules_del();
                wanIntfIP = false;
            }

            if(!strncmp(conf->if_name, CONFIG_STATIC_IP_LAN_INTF1, sizeof(conf->if_name))
                || !strncmp(conf->if_name, CONFIG_STATIC_IP_LAN_INTF2, sizeof(conf->if_name))
                || !strncmp(conf->if_name, CONFIG_STATIC_IP_LAN_INTF3, sizeof(conf->if_name)))
            {
                LOG(INFO, "nm2_ripv2_config: RIP Interface %s removed, removing RIPv2 NAT rule for the interface", conf->if_name);
                nm2_ripv2_nfm_rules_del();
                if(!nm2_ripv2_nfm_rules_add())
                    LOG(ERR, "nm2_ripv2_config: Unable to update Netfilter.");
            }
            break;
        case OVSDB_UPDATE_ERROR:
        default:
            LOG(ERR, "nm2_ripv2_config: Invalid Wifi_Inet_State mon_type(%d)", mon->mon_type);
    }

    return;
}

/*
 * ===========================================================================
 * Initialize RIPv2 Config
 * ===========================================================================
 */

void nm2_ripv2_init(void)
{
    LOG(INFO, "Initializing NM Static Inet Config");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(RIPv2_Global_Config);
    OVSDB_TABLE_INIT_NO_KEY(RIPv2_Interface_Config);
    OVSDB_TABLE_INIT_NO_KEY(RIPv2_Global_State);
    OVSDB_TABLE_INIT_NO_KEY(RIPv2_Interface_State);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Inet_State);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(RIPv2_Global_Config, false);
    OVSDB_TABLE_MONITOR(RIPv2_Interface_Config, false);
    OVSDB_TABLE_MONITOR(Wifi_Inet_State, false);

    nm2_ripv2_nfm_init();

    return;
}
