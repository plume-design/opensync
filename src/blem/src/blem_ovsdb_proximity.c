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

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "blem.h"
#include "log.h"
#include "osa_assert.h"
#include "os_util.h"
#include "ovsdb_table.h"
#include "schema.h"

/* Helper macros */

/**
 * Assert that a value is within a given range
 *
 * @param[in] value          Value to check.
 * @param[in] min_inclusive  Minimum value (inclusive).
 * @param[in] max_inclusive  Maximum value (inclusive).
 *
 * @see ASSERT
 */
#define ASSERT_IN_RANGE(value, min_inclusive, max_inclusive)             \
    ASSERT(((min_inclusive) <= (value)) && ((value) <= (max_inclusive)), \
           "%s %d not in range [%d, %d]",                                \
           (#value),                                                     \
           (value),                                                      \
           (min_inclusive),                                              \
           (max_inclusive))

/* Helper functions */

/**
 * Convert a string representation of a UUID to its binary form
 *
 * The string is expected to be Version 4 UUID, that is 32 hexadecimal
 * digits separated with up to 4 non-hexadecimal characters.
 *
 * @param[in]  uuid_str  UUID string.
 * @param[out] uuid      UUID in binary form.
 *
 * @return true if the conversion was successful, false otherwise.
 */
static bool uuid_str_to_bin(const char *const uuid_str, uint8_t uuid[16])
{
    /* Allow separator between each UUID byte, but make sure that the string always represents exactly 16 bytes */
    const char *const str_end = uuid_str + strnlen(uuid_str, PJS_OVS_UUID_SZ - 1);
    const char *p_str = uuid_str;
    size_t uuid_i = 0;

    while ((str_end - p_str >= 2) && (uuid_i < 16))
    {
        /* Copy two characters to a temporary buffer, to add null terminator and avoid using scanf */
        char octet_str[3] = {p_str[0], p_str[1], '\0'};
        long val;

        if (isxdigit(*p_str) == 0)
        {
            p_str++;
            continue;
        }
        if (!os_strtoul(octet_str, &val, 16))
        {
            break;
        }
        uuid[uuid_i] = (uint8_t)val;
        p_str += 2;
        uuid_i += 1;
    }

    return (uuid_i == 16) && (p_str == str_end);
}

/* Variables */

/** BLE_Proximity_Config OVSDB table object */
static ovsdb_table_t table_BLE_Proximity_Config;
/** BLE_Proximity_State OVSDB table object */
static ovsdb_table_t table_BLE_Proximity_State;

/* Main logic */

static void blem_ble_proximity_on_state(ble_advertising_set_t *adv_set, bool active, int16_t adv_tx_power)
{
    struct schema_BLE_Proximity_State state = {0};

    SCHEMA_SET_INT(state.adv_tx_power, adv_tx_power);
    SCHEMA_SET_BOOL(state.ibeacon_enable, adv_set->mode.enabled);
    SCHEMA_SET_BOOL(state.ibeacon_active, active);

    if (!ovsdb_table_upsert(&table_BLE_Proximity_State, &state, false))
    {
        LOGE("Could not update %s", SCHEMA_TABLE(BLE_Proximity_State));
    }
}

/** Callback invoked when the BLE_Proximity_Config table changes */
static void callback_BLE_Proximity_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_BLE_Proximity_Config *old_rec,
        struct schema_BLE_Proximity_Config *config)
{
    blem_ble_proximity_config_t cfg = {0};
    (void)mon;

    if (config == NULL)
    {
        if (old_rec == NULL)
        {
            return;
        }
        config = old_rec;
        config->ibeacon_enable = false;
    }
    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        config->ibeacon_enable = false;
    }

    /* OVSDB schema has strict constraints on the fields, so fail if they are not in range */
    cfg.enable = config->ibeacon_enable;

    ASSERT_IN_RANGE(config->adv_tx_power, INT8_MIN, INT8_MAX);
    cfg.adv_tx_power = config->adv_tx_power;

    ASSERT_IN_RANGE(config->adv_interval, 0, UINT16_MAX);
    cfg.adv_interval = config->adv_interval;

    ASSERT(uuid_str_to_bin(config->ibeacon_uuid.uuid, cfg.uuid), "Invalid UUID '%s'", config->ibeacon_uuid.uuid);

    ASSERT_IN_RANGE(config->ibeacon_major, 0, UINT16_MAX);
    cfg.major = config->ibeacon_major;

    ASSERT_IN_RANGE(config->ibeacon_minor_len, 1, ARRAY_LEN(config->ibeacon_minor));
    for (int i = 0; i < config->ibeacon_minor_len; i++)
    {
        ASSERT_IN_RANGE(config->ibeacon_minor[i], 0, UINT16_MAX);
        cfg.minors[i] = config->ibeacon_minor[i];
    }

    ASSERT_IN_RANGE(config->ibeacon_minor_interval, 0, UINT16_MAX);
    cfg.minor_interval = config->ibeacon_minor_interval;

    ASSERT_IN_RANGE(config->ibeacon_power, INT8_MIN, INT8_MAX);
    cfg.meas_power = config->ibeacon_power;

    blem_ble_proximity_configure(&cfg, blem_ble_proximity_on_state);
}

void blem_ovsdb_proximity_init(void)
{
    LOGD("Init proximity tables");

    OVSDB_TABLE_INIT_NO_KEY(BLE_Proximity_Config);
    OVSDB_TABLE_INIT_NO_KEY(BLE_Proximity_State);

    OVSDB_TABLE_MONITOR(BLE_Proximity_Config, false);
}

void blem_ovsdb_proximity_fini(void)
{
    ovsdb_table_fini(&table_BLE_Proximity_Config);
    ovsdb_table_fini(&table_BLE_Proximity_State);
}
