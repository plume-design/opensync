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

#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>

#include "blem.h"
#include "blem_connectivity_status.h"
#include "log.h"
#include "schema.h"
#include "const.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "os_util.h"
#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
#include "osp_led.h"
#endif

/* Constants */

/** Map for converting `AW_Bluetooth_Config::command` to BLE beacon values */
static c_item_t map_ble_command[] = {
    C_ITEM_STR(0x00, "on_boarding"),
    C_ITEM_STR(0x01, "diagnostic"),
    C_ITEM_STR(0x02, "locate"),
};

/* Variables */

/** AW_Bluetooth_Config OVSDB table object */
static ovsdb_table_t table_AW_Bluetooth_Config;
/** AW_Bluetooth_State OVSDB table object */
static ovsdb_table_t table_AW_Bluetooth_State;

/** Flag indicating cloud connectivity status */
static bool g_cloud_is_connected = false;
/** The most recent local BLE configuration */
static struct schema_AW_Bluetooth_Config g_aw_bluetooth_config;

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
/** Timer used to enable Advanced Onboarding (BLE connectable mode) after period of no internet connectivity */
static ev_timer g_no_internet_timeout_timer;
#endif

/* Helper functions */

static bool bluetooth_config_table_exists(void)
{
    /* Optimized (no conversion of unused result) version of:
     *   ovsdb_table_select_one_where(&table_AW_Bluetooth_Config, NULL, &(struct schema_AW_Bluetooth_Config){0})
     */
    json_t *const rows = ovsdb_sync_select_where(SCHEMA_TABLE(AW_Bluetooth_Config), NULL);
    const bool rc = json_array_size(rows) > 0;
    json_decref(rows);
    return rc;
}

static bool parse_aw_bluetooth_config_command(
        const struct schema_AW_Bluetooth_Config *const config,
        struct schema_AW_Bluetooth_State *const state,
        uint8_t *const msg_type)
{
    const c_item_t *command;

    if (config->command_exists && (strnlen(config->command, sizeof(config->command)) > 0))
    {
        command = c_get_item_by_str(map_ble_command, config->command);
        if (command == NULL)
        {
            LOGE("Unknown command '%s'", config->command);
            return false;
        }
    }
    else
    {
        /* Default to on_boarding */
        command = c_get_item_by_key(map_ble_command, 0x00);
        LOGT("No command, using '%s'", (char *)(command->data));
    }

    *msg_type = (uint8_t)command->key;
    SCHEMA_SET_STR(state->command, (char *)command->data);
    return true;
}

static bool parse_aw_bluetooth_config_payload(
        const struct schema_AW_Bluetooth_Config *const config,
        struct schema_AW_Bluetooth_State *const state,
        uint8_t msg[6])
{
    if (config->payload_exists && (strnlen(config->payload, sizeof(config->payload)) > 0))
    {
        unsigned int consumed;
        const int rv =
                sscanf(config->payload,
                       "%02" SCNx8 ":%02" SCNx8 ":%02" SCNx8 ":%02" SCNx8 ":%02" SCNx8 ":%02" SCNx8 "%n",
                       &msg[0],
                       &msg[1],
                       &msg[2],
                       &msg[3],
                       &msg[4],
                       &msg[5],
                       &consumed);
        if ((rv != 6) || (consumed != strnlen(config->payload, sizeof(config->payload))))
        {
            LOGE("Invalid payload '%s'", config->payload);
            return false;
        }
        SCHEMA_SET_STR(state->payload, config->payload);
    }
    else
    {
        memset(msg, 0, 6);
        SCHEMA_SET_STR(state->payload, "00:00:00:00:00:00");
        LOGT("No payload, using '%s'", state->payload);
    }
    return true;
}

static bool parse_aw_bluetooth_config_txi(
        const struct schema_AW_Bluetooth_Config *const config,
        struct schema_AW_Bluetooth_State *const state,
        int *const txi)
{
    if (config->interval_millis_exists && (config->interval_millis != 0))
    {
        if (config->interval_millis < 0)
        {
            LOGE("Invalid interval_millis %d", config->interval_millis);
            return false;
        }
        *txi = config->interval_millis;
    }
    else
    {
        *txi = CONFIG_BLEM_ADVERTISING_INTERVAL;
        LOGT("No interval_millis, using %d", *txi);
    }
    SCHEMA_SET_INT(state->interval_millis, *txi);
    return true;
}

/* Main logic */

static void apply_aw_bluetooth_config(struct schema_AW_Bluetooth_Config *const config)
{
    struct schema_AW_Bluetooth_State state = {0};

    if (strcmp(config->mode, "on") == 0)
    {
        uint8_t msg_type;
        uint8_t msg[6];
        int tx_interval;

        SCHEMA_SET_STR(state.mode, "on");

        /* All AW_Bluetooth_Config fields are optional, handle default values */
        if (!parse_aw_bluetooth_config_command(config, &state, &msg_type))
        {
            return;
        }
        if (!parse_aw_bluetooth_config_payload(config, &state, msg))
        {
            return;
        }
        if (!parse_aw_bluetooth_config_txi(config, &state, &tx_interval))
        {
            return;
        }
        if (config->txpower_exists && (config->txpower != 0))
        {
            LOGD("TX power value %d ignored", config->txpower);
        }

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
        SCHEMA_SET_BOOL(state.connectable, config->connectable_exists && config->connectable);
#else
        if (config->connectable_exists && config->connectable)
        {
            LOGW("Connectable BLE mode is not supported");
        }
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

        if (!blem_ble_enable(state.connectable, tx_interval, msg_type, msg))
        {
            LOGE("Could not start BLE advertising");
            return;
        }
    }
    else
    {
        blem_ble_disable();
        SCHEMA_SET_STR(state.mode, "off");
    }

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
    /* BLEM sets BTCONNECTABLE LED state only when connectable is enabled locally,
     * otherwise the cloud handles the LED state together with connectable mode. */
    if (!g_cloud_is_connected && config->connectable_changed)
    {
        if (state.connectable)
        {
            osp_led_ovsdb_add_led_config(OSP_LED_ST_BTCONNECTABLE, OSP_LED_PRIORITY_DEFAULT, OSP_LED_POSITION_DEFAULT);
        }
        config->connectable_changed = false;
    }
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

    if (!ovsdb_table_upsert(&table_AW_Bluetooth_State, &state, false))
    {
        LOGE("Unable to update %s", SCHEMA_TABLE(AW_Bluetooth_State));
    }
}

void callback_AW_Bluetooth_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_AW_Bluetooth_Config *old_rec,
        struct schema_AW_Bluetooth_Config *config)
{
    (void)mon;
    (void)old_rec;

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        if (!g_cloud_is_connected)
        {
            /* This is result of deletion in `blem_connectivity_status_updated()` */
            LOGD("%s delete ignored", SCHEMA_TABLE(AW_Bluetooth_Config));
            return;
        }
        config = old_rec;
        MEMZERO(*config);
    }

    LOGI("%s %s '%.*s' conn=%d '%.*s' '%.*s' %d ms",
         SCHEMA_TABLE(AW_Bluetooth_Config),
         ovsdb_update_type_to_str(mon->mon_type),
         sizeof(config->mode),
         config->mode,
         config->connectable,
         sizeof(config->command),
         config->command,
         sizeof(config->payload),
         config->payload,
         config->interval_millis);

    apply_aw_bluetooth_config(config);
}

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
static void no_internet_timeout_timer_callback(struct ev_loop *const loop, ev_timer *const timer, const int r_events)
{
    (void)loop;
    (void)timer;
    (void)r_events;

    LOGI("Offline for %d s, enable connectable mode", CONFIG_BLEM_CONFIG_VIA_BLE_OFFLINE_TIMEOUT);

    SCHEMA_SET_BOOL(g_aw_bluetooth_config.connectable, true);
    g_aw_bluetooth_config.connectable_changed = true;

    /* Additional safety check to prevent BLE control when cloud is connected */
    if (g_cloud_is_connected)
    {
        LOGW("Cloud connected while offline timeout");
    }
    else
    {
        apply_aw_bluetooth_config(&g_aw_bluetooth_config);
    }
}

static void manage_no_internet_timeout_timer(const bool internet_is_connected)
{
    /* Stop the offline timeout timer if internet connectivity is established/restored */
    if (internet_is_connected)
    {
        if (ev_is_active(&g_no_internet_timeout_timer))
        {
            LOGI("Online, cancel %.3f s timeout", ev_timer_remaining(EV_DEFAULT, &g_no_internet_timeout_timer));
            ev_timer_stop(EV_DEFAULT, &g_no_internet_timeout_timer); /*< Keep the `.at` value (used in else below) */
            WARN_ON(g_aw_bluetooth_config.connectable);              /*< Only enabled when the timer fires */
        }
        else if (g_aw_bluetooth_config.connectable)
        {
            LOGI("Online, disable connectable");
            SCHEMA_SET_BOOL(g_aw_bluetooth_config.connectable, false);
            g_aw_bluetooth_config.connectable_changed = true;
        }
        else
        {
            LOGD("Still online");
        }
    }
    else if (ev_is_active(&g_no_internet_timeout_timer))
    {
        LOGI("Still offline, %.3f s until connectable", ev_timer_remaining(EV_DEFAULT, &g_no_internet_timeout_timer));
    }
    else if (g_aw_bluetooth_config.connectable)
    {
        LOGI("Still offline, connectable");
    }
    else
    {
        LOGI("Offline, start %d s connectable timeout", CONFIG_BLEM_CONFIG_VIA_BLE_OFFLINE_TIMEOUT);
        ev_timer_set(&g_no_internet_timeout_timer, CONFIG_BLEM_CONFIG_VIA_BLE_OFFLINE_TIMEOUT, 0);
        ev_timer_start(EV_DEFAULT, &g_no_internet_timeout_timer);
    }
}
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

static void blem_connectivity_status_updated(const uint8_t status_old, const uint8_t status)
{
    const bool cloud_was_connected = blem_connectivity_status_is_connected_to_cloud(status_old);

    g_cloud_is_connected = blem_connectivity_status_is_connected_to_cloud(status);

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
    manage_no_internet_timeout_timer(blem_connectivity_status_is_connected_to_internet(status));

    /* As BLEM is the only one setting BTCONNECTABLE state, it is also responsible for
     * clearing it when connectable mode is disabled locally - which is implicitly also
     * when cloud is connected. Do this here instead of in `apply_aw_bluetooth_config()`
     * to clear locally set LED state even if cloud is connected and the config table
     * already pushed, which skips the call to `apply_aw_bluetooth_config()` below. */
    if (g_aw_bluetooth_config.connectable_changed && !g_aw_bluetooth_config.connectable)
    {
        osp_led_ovsdb_delete_led_config(OSP_LED_ST_BTCONNECTABLE, OSP_LED_POSITION_DEFAULT);
    }
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

    /* The cloud, when connected, is the source of truth for the BLE configuration
     * and has full control over it, even if it does not match the local state. */
    if (g_cloud_is_connected)
    {
        /* Strictly ignoring CSb updates always when the "Connected to Cloud" bit
         * is set would render this bit useless, as it would never be advertised.
         * Moreover, the cloud usually performs other tasks with higher priority
         * first, and then disables BLE advertising after a short period.
         * Detect the scenario when the cloud was just connected and not yet pushed
         * the config table, and update the advertised CSb value accordingly.
         * Relying on solely the absence of the config table is not sufficient,
         * as cloud can also delete it anytime (to disable BLE advertising). */
        if (cloud_was_connected)
        {
            LOGI("CSb 0x%02X ignored (cloud control)", status);
            return;
        }
        if (bluetooth_config_table_exists())
        {
            LOGI("CSb 0x%02X ignored (cloud connected, config exists)", status);
            return;
        }
        LOGI("CSb 0x%02X (cloud connected)", status);
    }
    else if (cloud_was_connected)
    {
        /* When the cloud is not connected, BLEM takes over the BLE configuration.
         * Although it never writes to `AW_Bluetooth_Config` table locally (only
         * cloud writes to it), it always updates the `AW_Bluetooth_State` table
         * to reflect the actual current BLE state.
         * This would mean that the actual BLE state (and thus the state table)
         * could differ from what is written in the config table. This normally
         * means error condition (the BLE configuration from config could not be
         * applied and therefore not reflected in the state table). To avoid this,
         * delete the config table when switching to local control. This also
         * indicates (to any local observer, also used for testing) that the
         * cloud (via OVSDB) is currently not in control.
         * However, do this only once - when cloud disconnects, to enable test
         * tools to recreate the table later, taking control of BLE via OVSDB
         * even if the cloud is not connected. */
        LOGI("CSb 0x%02X (cloud disconnected, delete config)", status);
        ovsdb_table_delete(&table_AW_Bluetooth_Config, NULL);
    }
    else
    {
        LOGI("CSb 0x%02X (local control)", status);
    }

    /* Only update the connectivity status bitmask byte */
    SPRINTF(g_aw_bluetooth_config.payload, "%02X:00:00:00:00:00", status);
    SCHEMA_MARK_SET(g_aw_bluetooth_config.payload);

    /* Do not upsert AW_Bluetooth_Config, only apply to BLE (which updates `AW_Bluetooth_State`) */
    apply_aw_bluetooth_config(&g_aw_bluetooth_config);
}

void blem_ovsdb_onboarding_init(void)
{
    LOGD("Init onboarding tables");

    /* During boot, each node broadcasts Onboarding Bluetooth beacons, which are
     * later turned off by the cloud once the node is onboarded and connected.
     * This is why locally, the beaconing is always enabled. */
    MEMZERO(g_aw_bluetooth_config);
    SCHEMA_SET_STR(g_aw_bluetooth_config.mode, "on");
    SCHEMA_SET_STR(g_aw_bluetooth_config.command, "on_boarding");
    SCHEMA_SET_STR(g_aw_bluetooth_config.payload, "00:00:00:00:00:00");
    SCHEMA_SET_INT(g_aw_bluetooth_config.interval_millis, CONFIG_BLEM_ADVERTISING_INTERVAL);

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
    ev_timer_init(&g_no_internet_timeout_timer, no_internet_timeout_timer_callback, 0, 0);
    /* By default, connectable mode is disabled and locally only enabled in `no_internet_timeout_timer_callback()` */
    osp_led_ovsdb_delete_led_config(OSP_LED_ST_BTCONNECTABLE, OSP_LED_POSITION_DEFAULT);
#endif

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(AW_Bluetooth_Config);
    OVSDB_TABLE_INIT_NO_KEY(AW_Bluetooth_State);

    OVSDB_TABLE_MONITOR(AW_Bluetooth_Config, false);

    blem_connectivity_status_init(
            blem_connectivity_status_updated,
            CONFIG_BLEM_CONNECTIVITY_STATUS_DEBOUNCE_TIME / 1000.0f);
}

void blem_ovsdb_onboarding_fini(void)
{
#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
    ev_timer_stop(EV_DEFAULT, &g_no_internet_timeout_timer);
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

    blem_connectivity_status_fini();

    ovsdb_table_fini(&table_AW_Bluetooth_Config);
    ovsdb_table_fini(&table_AW_Bluetooth_State);
}
