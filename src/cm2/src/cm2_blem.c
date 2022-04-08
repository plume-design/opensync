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

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/sysinfo.h>

#include "memutil.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "log.h"
#include "target.h"
#include "cm2.h"
#include "kconfig.h"


/* BLE definitions */
#define CM2_BLE_INTERVAL_VALUE_DEFAULT  0
#define CM2_BLE_TXPOWER_VALUE_DEFAULT   0
#define CM2_BLE_MODE_OFF                "off"
#define CM2_BLE_MODE_ON                 "on"
#define CM2_BLE_MSG_ONBOARDING          "on_boarding"

static ovsdb_table_t table_AW_Bluetooth_Config;
static ovsdb_table_t table_Connection_Manager_Uplink;

int cm2_ovsdb_ble_config_update(uint8_t ble_status)
{
    struct schema_AW_Bluetooth_Config ble;
    char   *filter[] = { "+",
                         SCHEMA_COLUMN(AW_Bluetooth_Config, mode),
                         SCHEMA_COLUMN(AW_Bluetooth_Config, command),
                         SCHEMA_COLUMN(AW_Bluetooth_Config, payload),
                         SCHEMA_COLUMN(AW_Bluetooth_Config, interval_millis),
                         SCHEMA_COLUMN(AW_Bluetooth_Config, txpower),
                         NULL };
    int    ret;

    memset(&ble, 0, sizeof(ble));

    SCHEMA_SET_STR(ble.mode, CM2_BLE_MODE_ON);
    SCHEMA_SET_STR(ble.command, CM2_BLE_MSG_ONBOARDING);
    SCHEMA_SET_INT(ble.interval_millis, CM2_BLE_INTERVAL_VALUE_DEFAULT);
    SCHEMA_SET_INT(ble.txpower, CM2_BLE_TXPOWER_VALUE_DEFAULT);
    snprintf(ble.payload, sizeof(ble.payload), "%02x:00:00:00:00:00", ble_status);
    ble.payload_exists = true;
    ble.payload_present = true;

    ret = ovsdb_table_upsert_simple_f(&table_AW_Bluetooth_Config,
                                      SCHEMA_COLUMN(AW_Bluetooth_Config, command),
                                      ble.command,
                                      &ble,
                                      NULL,
                                      filter);
    if (!ret)
        LOGE("%s Insert new row failed for %s", __func__, ble.command);

    return ret == 1;
}

int
cm2_ovsdb_ble_set_connectable(bool state)
{
    struct schema_AW_Bluetooth_Config ble;
    char   *filter[] = { "+",
                       SCHEMA_COLUMN(AW_Bluetooth_Config, connectable),
                       NULL };

    memset(&ble, 0, sizeof(ble));
    SCHEMA_SET_INT(ble.connectable, state);

    LOGI("Changing ble connectable state: %d", state);

    return  ovsdb_table_update_where_f(&table_AW_Bluetooth_Config,
                 ovsdb_where_simple(SCHEMA_COLUMN(AW_Bluetooth_Config, command), CM2_BLE_MSG_ONBOARDING),
                 &ble, filter);
}

void cm2_set_ble_onboarding_link_state(bool state, char *if_type, char *if_name)
{
    if (g_state.connected)
        return;
    if (cm2_is_eth_type(if_type)) {
        cm2_ble_onboarding_set_status(state, BLE_ONBOARDING_STATUS_ETHERNET_LINK);
    } else if (cm2_is_wifi_type(if_type)) {
        cm2_ble_onboarding_set_status(state, BLE_ONBOARDING_STATUS_WIFI_LINK);
    }
    cm2_ble_onboarding_apply_config();
}

void cm2_ble_onboarding_set_status(bool state, cm2_ble_onboarding_status_t status)
{
    if (state)
        g_state.ble_status |= (1 << status);
    else
        g_state.ble_status &= ~(1 << status);

    LOGI("Set BT status = %x", g_state.ble_status);
}

void cm2_ble_onboarding_apply_config(void)
{
    cm2_ovsdb_ble_config_update(g_state.ble_status);
}

void cm2_set_backhaul_update_ble_state(void) {
    bool eth_type;

    eth_type = !strcmp(g_state.link.if_type, ETH_TYPE_NAME) ||
               !strcmp(g_state.link.if_type, VLAN_TYPE_NAME);

    g_state.ble_status = 0;
    if (eth_type) {
        cm2_ble_onboarding_set_status(true,
                                      BLE_ONBOARDING_STATUS_ETHERNET_LINK);
        cm2_ble_onboarding_set_status(true,
                                      BLE_ONBOARDING_STATUS_ETHERNET_BACKHAUL);
    }  else {
        cm2_ble_onboarding_set_status(true,
                                      BLE_ONBOARDING_STATUS_WIFI_LINK);
        cm2_ble_onboarding_set_status(true,
                                      BLE_ONBOARDING_STATUS_WIFI_BACKHAUL);
    }
    cm2_ble_onboarding_apply_config();
}

void cm2_set_ble_state(bool state, cm2_ble_onboarding_status_t status) {
    cm2_ble_onboarding_set_status(state, status);
    cm2_ble_onboarding_apply_config();
}

void cm2_ovsdb_connection_update_ble_phy_link(void) {
    struct schema_Connection_Manager_Uplink *uplink;
    void                                    *uplink_p;
    bool                                    state;
    int                                     wifi_cnt;
    int                                     eth_cnt;
    int                                     count;
    int                                     i;

    uplink_p = ovsdb_table_select_typed(&table_Connection_Manager_Uplink,
                                        SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2),
                                        OCLM_BOOL,
                                        (void *) &state,
                                        &count);

    LOGI("BLE active phy links: %d",  count);

    wifi_cnt = 0;
    eth_cnt  = 0;

    if (uplink_p) {
        for (i = 0; i < count; i++) {
            uplink = (struct schema_Connection_Manager_Uplink *) (uplink_p + table_Connection_Manager_Uplink.schema_size * i);
            LOGI("Link %d: ifname = %s iftype = %s active state= %d", i, uplink->if_name, uplink->if_type, uplink->has_L2);

            if (cm2_is_eth_type(uplink->if_type))
                eth_cnt++;
            else
                wifi_cnt++;
        }
        FREE(uplink_p);
    }

    state = eth_cnt > 0 ? true : false;
    cm2_ble_onboarding_set_status(state, BLE_ONBOARDING_STATUS_ETHERNET_LINK);
    state = wifi_cnt > 0 ? true : false;
    cm2_ble_onboarding_set_status(state, BLE_ONBOARDING_STATUS_WIFI_LINK);
    cm2_ble_onboarding_apply_config();
}

void cm2_ovsdb_ble_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(AW_Bluetooth_Config);
    OVSDB_TABLE_INIT_NO_KEY(Connection_Manager_Uplink);
}
