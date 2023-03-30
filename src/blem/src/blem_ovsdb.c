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
#include "log.h"
#include "schema.h"
#include "const.h"
#include "ovsdb_table.h"
#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
#include "osp_led.h"
#endif


#define MODULE_ID LOG_MODULE_ID_MAIN


/*****************************************************************************/

// BLE Commands
static c_item_t map_ble_command[] = {
    C_ITEM_STR(0x00, "on_boarding"),
    C_ITEM_STR(0x01, "diagnostic"),
    C_ITEM_STR(0x02, "locate"),
};

static ovsdb_table_t table_AW_Bluetooth_Config;
static ovsdb_table_t table_AW_Bluetooth_State;
#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
static ovsdb_table_t table_AWLAN_Node;
#endif

/*****************************************************************************/

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED

static void blem_update_led_state(bool connectable)
{
    char *filter[] = {"+", SCHEMA_COLUMN(AWLAN_Node, led_config), NULL};
    struct schema_AWLAN_Node awlan_node = { 0 };

    SCHEMA_KEY_VAL_APPEND(awlan_node.led_config, "state", osp_led_state_to_str(OSP_LED_ST_BTCONNECTABLE));
    if (!connectable)
    {
        SCHEMA_KEY_VAL_APPEND(awlan_node.led_config, "clear", "true");
    }
    ovsdb_table_update_f(&table_AWLAN_Node, &awlan_node, filter);
}

#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

/*****************************************************************************/

void callback_AW_Bluetooth_Config(ovsdb_update_monitor_t *mon,
        struct schema_AW_Bluetooth_Config *old_rec,
        struct schema_AW_Bluetooth_Config *config)
{
    (void)mon;
    (void)old_rec;

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
    /* Saved state of AW_Bluetooth_Config.connectable used to avoid spamming AWLAN_Node.led_config */
    static int connectable_state = -1; /* unknown (-1), true (1), false (0) */
    bool connectable = false;
#else
    const bool connectable = false;
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

    if (strcmp(config->mode, "on") == 0)
    {
        const c_item_t *command;
        unsigned int consumed;
        uint8_t msg[6];
        int rv;

        // Get appropriate command
        command = c_get_item_by_str(map_ble_command, config->command);
        if (command == NULL)
        {
            LOGE("Unknown command: %s", config->command);
            return;
        }

        // Parse message
        rv = sscanf(config->payload,
                    "%02" SCNx8 ":%02" SCNx8 ":%02" SCNx8 ":%02" SCNx8 ":%02" SCNx8 ":%02" SCNx8 "%n",
                    &msg[0], &msg[1], &msg[2], &msg[3], &msg[4], &msg[5], &consumed);
        if ((rv != 6) || (consumed != strlen(config->payload)))
        {
            LOGE("Invalid payload '%s'", config->payload);
            return;
        }

        if (config->txpower != 0)
        {
            LOGD("TX power value %d ignored", config->txpower);
        }

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
        // Optional connectable field
        connectable = config->connectable_exists && config->connectable;
#else
        if (config->connectable_exists && config->connectable)
        {
            LOGW("Connectable BLE mode is not supported");
            config->connectable = false;
        }
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

        if (!blem_ble_enable(connectable,
                             config->interval_millis ? config->interval_millis : (CONFIG_BLEM_ADVERTISING_INTERVAL),
                             (uint8_t) (int)command->key,
                             msg))
        {
            LOGE("Could not start BLE advertising");
            return;
        }
    }
    else
    {
        blem_ble_disable();
    }

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
    if (connectable_state != (int)connectable)
    {
        blem_update_led_state(connectable);
        connectable_state = (int)connectable;
    }
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

    // Update state table
    if (!ovsdb_table_upsert(&table_AW_Bluetooth_State, config, false))
    {
        LOGE("Unable to update AW_Bluetooth_State table!");
    }
}

void blem_ovsdb_init(void)
{
    LOGI("Initializing BLEM tables");

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(AW_Bluetooth_Config);
    OVSDB_TABLE_INIT_NO_KEY(AW_Bluetooth_State);
#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
#endif

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(AW_Bluetooth_Config, true);  // Trigger initial callback
}

