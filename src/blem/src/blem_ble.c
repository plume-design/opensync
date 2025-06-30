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

#include <stddef.h>
#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
#include <jansson.h>
#endif

#include "blem.h"
#include "log.h"
#include "kconfig.h"
#include "const.h"
#include "util.h"
#include "os_types.h"
#include "osp_ble.h"
#include "osp_unit.h"
#include "ble_adv_data.h"
#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
#include "os_random.h"
#include "json_util.h"
#include "blem_wan_config.h"
#endif



/** Main event loop used for timer operations */
static struct ev_loop *g_loop = NULL;

/** Timer used for rotating Proximity Beacon Minor IDs */
static ev_timer g_proximity_timer;
/** Proximity Beacon Minor IDs to rotate (0 = unused), in host byte order */
static uint16_t g_proximity_minors[ARRAY_SIZE(((blem_ble_proximity_config_t *)NULL)->minors)];


static void callback_on_fatal_error(void)
{
    LOGE("Breaking ev loop because of fatal ble_config error");
    ev_break(g_loop, EVBREAK_ALL);
}

/**
 * Set advertising or scan response payload via OSP layer
 *
 * @param[in] data           Payload data to set (which, if valid, begins with the AD element).
 * @param[in] max_len        Maximum length of the payload data.
 *                           The actual length of the payload is calculated using @ref ble_adv_data_get_length.
 * @param[in] scan_response  True if the payload is a scan response and @ref osp_ble_set_scan_response_data
 *                           shall be used to set it, false if @ref osp_ble_set_advertising_data shall be
 *                           used instead.
 *
 * @return true for success. Failure is logged internally. In case of failure when setting
 *         the data to the BT chip (in contrast to just the advertising data being invalid),
 *         @ref callback_on_fatal_error is also called internally.
 */
static bool blem_osp_set_data(const ble_ad_structure_t *data, const size_t max_len, const bool scan_response)
{
    const char *const type_str = scan_response ? "scan response" : "advertising";
    uint8_t actual_len;

    actual_len = ble_adv_data_get_length(data, max_len);
    if (actual_len == 0)
    {
        return false;
    }

    if (!(scan_response ? osp_ble_set_scan_response_data((const uint8_t *)data, actual_len)
                        : osp_ble_set_advertising_data((const uint8_t *)data, actual_len)))
    {
        const size_t str_len = 2 * max_len + 1;
        char *str = MALLOC(str_len);

        if (bin2hex((const uint8_t *)data, max_len, str, str_len) < 0)
        {
            str[0] = '\0';
        }
        LOGE("Could not set %d/%d B of %s payload %s", actual_len, max_len, type_str, str);

        FREE(str);
        callback_on_fatal_error();
        return false;
    }

    LOGD("Set %d/%d B of %s payload", actual_len, max_len, type_str);
    return true;
}

/**
 * Apply advertising data and parameters specified by the advertising set `adv`
 *
 * @param[in]  adv           Advertising set to apply.
 * @param[out] set_tx_power  Optional, @see osp_ble_set_advertising_tx_power
 *
 * @return true for success. Failure is logged internally.
 */
static bool advertising_apply_set(ble_advertising_set_t *const adv, int16_t *set_tx_power)
{
    int16_t actual_tx_power = 0;

    /* If mode is being changed, disable the advertising first, to ensure
     * all parameters are set while the advertising is disabled.
     * It will be enabled at the end anyway, if configured as so. */
    if (!adv->mode.enabled || adv->mode.changed)
    {
        if (!osp_ble_set_advertising_params(false, false, adv->interval))
        {
            LOGE("Could not disable advertising");
            return false;
        }
        if (!osp_ble_set_connectable(false))
        {
            LOGE("Could not disable connectable mode");
            return false;
        }
    }
    /* All parameters are set when the advertising is enabled, so there
     * is no need to do anything more, if the advertising is disabled. */
    if (!adv->mode.enabled)
    {
        LOGD("Advertising disabled");
        return true;
    }

    /* Advertising payload */
    if (adv->adv.changed || adv->mode.changed)
    {
        if (!blem_osp_set_data(&adv->adv.data.ad, sizeof(adv->adv.data), false))
        {
            return false;
        }
        adv->adv.changed = false;
    }

    /* Scan response payload */
    if (adv->scan_rsp.enabled && (adv->scan_rsp.changed || adv->mode.changed))
    {
        if (!blem_osp_set_data(&adv->scan_rsp.data.ad, sizeof(adv->scan_rsp.data), true))
        {
            return false;
        }
        adv->scan_rsp.changed = false;
    }

    /* Not all platforms support setting the advertising TX power, so allow this function to fail */
    if (osp_ble_set_advertising_tx_power(adv->adv_tx_power, &actual_tx_power))
    {
        /* But if the power can be set, warn if the requested power was out of range */
        if (abs(adv->adv_tx_power - actual_tx_power) > 1)
        {
            LOGW("Requested TX power %d dBm, set %d dBm", adv->adv_tx_power, actual_tx_power);
        }
        if (set_tx_power != NULL)
        {
            *set_tx_power = actual_tx_power;
        }
        /* Adjust the power to applied value to avoid repeated warnings */
        adv->adv_tx_power = actual_tx_power;
    }
    else if (set_tx_power != NULL)
    {
        *set_tx_power = 0;
    }

    if (adv->mode.changed)
    {
        if (!osp_ble_set_connectable(adv->mode.connectable))
        {
            LOGE("Could not %sable connectable", (adv->mode.connectable ? "en" : "dis"));
            return false;
        }

        if (!osp_ble_set_advertising_params(adv->mode.enabled, adv->scan_rsp.enabled, adv->interval))
        {
            LOGE("Could not set advertising params (%d, %d, %d)",
                 adv->mode.enabled,
                 adv->scan_rsp.enabled,
                 adv->interval);
            return false;
        }
        adv->mode.changed = false;
    }

    LOGI("Advertising enabled (sr=%d, conn=%d, i=%dms)", adv->scan_rsp.enabled, adv->mode.connectable, adv->interval);
    return true;
}

/**
 * Apply advertising data and parameters specified in @ref g_advertising_sets to the BT chip
 */
static bool advertising_apply(void)
{
    /* If only the active set changes, but not its data, not all parameters would
     * be applied, so track the last active set to force mode change. */
    static int last_active_set = -1;
    int active_set = -1;
    bool success = false;

    /* Advertising set priority is defined by the order in the
     * array, so the first enabled set becomes the active one. */
    for (int i = 0; i < ARRAY_LEN(g_advertising_sets); i++)
    {
        ble_advertising_set_t *const adv = &g_advertising_sets[i];
        /* Copy each set before applying it, because "changed" flags
         * are cleared when the set is applied, but we want to keep
         * the info about the state which caused changes. */
        ble_advertising_set_t adv_copy = *adv;
        int16_t actual_tx_power = 0;

        /* Only apply the highest-priority active (the first enabled) advertising set */
        if ((active_set < 0) && adv->mode.enabled)
        {
            if (i != last_active_set)
            {
                LOGD("Switch adv. set from %d to %d", last_active_set, i);
                adv->mode.changed = true;
            }
            else
            {
                LOGD("Reapply adv. set %d", i);
            }
            active_set = i;
            success = advertising_apply_set(adv, &actual_tx_power);
        }
        /* Call callbacks of all sets, but only provide set power of the actually active set,
         * to not confuse the upper layer with changed power value, when it was actually not
         * even applied. */
        if (adv->on_state_cb != NULL)
        {
            if (i == active_set)
            {
                adv->on_state_cb(&adv_copy, true, actual_tx_power);
            }
            else
            {
                adv->on_state_cb(&adv_copy, false, adv_copy.adv_tx_power);
            }
        }
    }

    /* If no set is enabled, use the temporary set to disable advertising */
    if (active_set < 0)
    {
        ble_advertising_set_t adv = {
            .mode = {.enabled = false, .connectable = false, .changed = true},
            .interval = 1000 /*< Arbitrary interval to prevent warnings about an invalid interval when using OSP API */
        };
        LOGT("No enabled adv. set");
        success = advertising_apply_set(&adv, NULL);
    }

    last_active_set = active_set;
    return success;
}

/**
 * Disable advertising of all @ref g_advertising_sets advertising sets
 *
 * This function does not apply the changes, use @ref advertising_apply() for that.
 */
static void advertising_disable_all(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(g_advertising_sets); i++)
    {
        ble_advertising_set_t *const adv = &g_advertising_sets[i];

        if (adv->mode.enabled)
        {
            adv->mode.enabled = false;
            adv->mode.changed = true;
        }
    }
}


#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED

/**
 * Generate pairing passkey using provided pairing token
 *
 * @param[in] token  Random pairing token used for passkey generation,
 *                   stability bits will be cleared before the passkey generation.
 *
 * @return 6-digit BLE Passkey Entry pairing passkey (0-999999) or negative value on error.
 */
static int get_pairing_passkey(const uint8_t *token)
{
    /* Stability bits of the pairing token must be cleared before use */
    uint8_t token_copy[C_FIELD_SZ(ble_adv_data_general_t, msg.pairing_token)];
    uint32_t passkey;

    memcpy(token_copy, token, sizeof(token_copy));
    token_copy[sizeof(token_copy) - 1] &= ~0b111u;

    return osp_ble_calculate_pairing_passkey(token_copy, &passkey) ? (int)passkey : -1;
}

/**
 * Generate pairing token and enable periodic generation of new tokens
 *
 * @param skip_apply    If true, new pairing token will be written to advertisement data, but
 *                      such updated advertisement data will not be sent to the BT chip.
 *
 * @return true for success, false if token could not or shall not be generated.
 */
static bool enable_pairing_tokens(bool skip_apply)
{
    ble_advertising_set_t *const adv = &g_advertising_sets[BLE_ADVERTISING_SET_GENERAL];
    ble_adv_data_general_t *const adv_data = &adv->adv.data.general;
    uint8_t token[sizeof(adv_data->msg.pairing_token)];
    bool passkey_ok = false;
    int passkey;

    while (!passkey_ok)
    {
        char psk[7];
        unsigned int i;

        /* Generate new random token */
        for (i = 0; i < sizeof(token); i++)
        {
            token[i] = (uint8_t)os_random_range(0, UINT8_MAX);
        }

        /* Test generate pin using this token */
        passkey = get_pairing_passkey(token);
        if ((passkey < 0) || (passkey > 999999))
        {
            LOGE("Could not generate valid passkey from token %02x%02x%02x%02x (%d)",
                 token[0], token[1], token[2], token[3], passkey);
            return false;
        }
        /* Reject the token if it causes the generated passkey to be all same or all consecutive digits */
        SPRINTF(psk, "%06d", passkey);

        for (i = 2; i < strlen(psk); i++)
        {
            /* Do not check only 2 chars as this also prevents OK pins, e.g. 112584 or 597614 */
            const char c = psk[i];
            const char c1 = psk[i - 1];
            const char c2 = psk[i - 2];
            if (((c != c1) || (c != c2)) &&              /* 000000, 111111, ..., 999999 */
                ((c != (c1 - 1)) || (c != (c2 - 2))) &&  /* 012345, 123456, ..., 456789 */
                ((c != (c1 + 1)) || (c != (c2 + 2))))    /* 987654, 876543, ..., 543210 */
            {
                /* Passkey does not contain group of 3 same/consecutive characters */
                passkey_ok = true;
                break;
            }
        }
    }

    /* Clear stability bits mask (before logging) */
    token[sizeof(token) - 1] &= ~0b111u;
    LOGD("Pairing token %02x%02x%02x%02x, passkey %06d active", token[0], token[1], token[2], token[3], passkey);
    /* Mark token as valid stable token (for infinite time if token changing is not enabled, or for a
     * limited amount of time (stable period) if enabled - it is the same at this point). */
    token[sizeof(token) - 1] |= 0b110u;

    memcpy(adv_data->msg.pairing_token, token, sizeof(adv_data->msg.pairing_token));
    adv->adv.changed = true;

    if (!skip_apply)
    {
        /* Apply new advertising payload (as token is part of it) */
        advertising_apply();
    }
    else
    {
        LOGD("Skipped apply of updated token in advertisement data");
    }

    if (!osp_ble_set_pairing_passkey(passkey))
    {
        LOGE("Could not set pairing passkey");
        return false;
    }

    LOGD("Pairing tokens enabled");
    return true;
}


static void write_current_config_to_gatt_json(void)
{
    size_t swan_sz;

    json_t *jwan = NULL;
    char *swan = NULL;

    jwan = ble_wan_config_get();
    if (jwan == NULL)
    {
        LOGE("Error acquiring current WAN configuration.");
        goto exit;
    }

    swan = json_dumps(jwan, JSON_COMPACT);
    swan_sz = strlen(swan) + 1;
    if (!osp_ble_set_gatt_json(swan, swan_sz))
    {
        LOGE("Could not write %zu B of existing config to Local-config JSON GATT", strlen(swan) + 1);
        goto exit;
    }
    LOGD("Loaded %zu B of existing config to Local-config JSON GATT: %s", swan_sz, swan);

exit:
    if (swan != NULL) json_free(swan);
    json_decref(jwan);
}

static bool callback_on_device_connected(uint8_t bd_address[6])
{
    const ble_adv_data_general_t *const adv_data = &g_advertising_sets[BLE_ADVERTISING_SET_GENERAL].adv.data.general;
    const os_macaddr_t *const addr = (os_macaddr_t *)bd_address;

    LOGI("BT device " PRI_os_macaddr_lower_t " connected, use passkey %06d to pair",
         FMT_os_macaddr_pt(addr),
         get_pairing_passkey(adv_data->msg.pairing_token));

    /* Data will be loaded to characteristic after device is paired */
    return true;
}

static bool callback_on_device_disconnected(void)
{
    ble_advertising_set_t *const adv = &g_advertising_sets[BLE_ADVERTISING_SET_GENERAL];

    LOGI("BT device disconnected");

    /* Always get a new token for every future new device connection */
    if (!enable_pairing_tokens(false))
    {
        /* Connectable mode is not possible if the pairing token cannot be generated */
        adv->mode.connectable = false;
    }
    /* Restart advertising (if enabled) because device connection stopped it */
    if (adv->mode.enabled)
    {
        adv->mode.changed = true;
    }
    advertising_apply();

    return true;
}

static bool callback_on_pairing_status(bool paired)
{
    if (paired)
    {
        LOGI("BT device paired");
        /* Load existing configuration in the characteristic */
        write_current_config_to_gatt_json();
    }
    else
    {
        LOGI("BT device failed to pair");
    }
    /* In both cases, immediately invalidate pairing token on BT chip, so it cannot be used anymore.
     * Setting 0 as a passkey with reject any further pairing requests until new token/passkey is set. */
    osp_ble_set_pairing_passkey(0);
    return true;
}

static bool callback_on_gatt_json(char *value, uint16_t len)
{
    json_error_t err;

    bool retval = false;
    json_t *jwan = NULL;

    LOGD("BLE received JSON: '%.*s'", (int) len, value);

    jwan = json_loadb(value, len, 0, &err);
    if (jwan == NULL)
    {
        LOGE("JSON parsing error on %d:%d: %s", err.line, err.column, err.text);
        goto exit;
    }

    if (!ble_wan_config_set(jwan))
    {
        LOG(ERR, "Error setting WAN configuration.");
        goto exit;
    }

    retval = true;

exit:
    json_decref(jwan);
    return retval;
}

#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

static void proximity_timer_callback(struct ev_loop *const loop, ev_timer *const timer, const int r_events)
{
    (void)loop;
    (void)timer;
    (void)r_events;
    /* Load the next Minor ID to the advertisement data */
    ble_advertising_set_t *const adv = &g_advertising_sets[BLE_ADVERTISING_SET_PROXIMITY];
    ble_adv_data_proximity_t *const adv_data = &adv->adv.data.proximity;
    const uint16_t minor_id = ntohs(adv_data->minor);
    size_t i;

    /* Timer is only started from `blem_ble_proximity_configure()`, only if the proximity advertising is enabled */
    ASSERT(adv->mode.enabled, "Invalid Prox. advertising state");

    /* First, find the current minor ID */
    for (i = 0; i < ARRAY_SIZE(g_proximity_minors); i++)
    {
        if (g_proximity_minors[i] == minor_id)
        {
            break;
        }
    }
    ASSERT(i < ARRAY_SIZE(g_proximity_minors), "Minor ID %d not found", minor_id);

    /* Set the next Minor ID, rotating through all valid Minor IDs */
    i++;
    if ((i >= ARRAY_SIZE(g_proximity_minors)) || (g_proximity_minors[i] == 0))
    {
        i = 0;
    }

    adv_data->minor = htons(g_proximity_minors[i]);
    adv->adv.changed = true;

    LOGD("Switching Prox. Minor ID from %d to %d", minor_id, g_proximity_minors[i]);
    advertising_apply();
}

bool blem_ble_init(struct ev_loop *p_loop)
{
    ble_advertising_set_t *const adv = &g_advertising_sets[BLE_ADVERTISING_SET_GENERAL];
    ble_adv_data_general_t *const adv_data = &adv->adv.data.general;
    ble_scan_response_data_t *const sr_data = &adv->scan_rsp.data;
    char serial[sizeof(adv_data->serial_num) + 1] = "";
    uint16_t uuid = CONFIG_BLEM_GATT_SERVICE_UUID;

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
    ble_wan_config_init();
#endif

    if (!osp_ble_init(p_loop,
#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
                      callback_on_device_connected,
                      callback_on_device_disconnected,
                      callback_on_pairing_status,
                      callback_on_gatt_json
#else
                      NULL, NULL, NULL, NULL
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */
       ))
    {
        LOGE("BT driver init failed");
        return false;
    }

    advertising_disable_all();

    /* The serial number in the advertising data is not null-delimited and does not change, set it */
    osp_unit_serial_get(serial, sizeof(serial));
    /* Use `strncpy` instead of `STRSCPY` because the SN in the advertising data does not need to be null-terminated */
    strncpy(adv_data->serial_num, serial, sizeof(adv_data->serial_num));

    /* "Complete List of 16-bit Service UUIDs" value is also static in the advertising data - set it */
    LOGD("Using %s UUID 0x%04x", osp_ble_get_service_uuid(&uuid) ? "provisioned" : "default", uuid);
    adv_data->service.uuids[0] = TO_LE16(uuid);

    /* Also prepare scan response data if eventually used */
    SPRINTF(sr_data->cln.name, "Pod %.*s", sizeof(adv_data->serial_num), adv_data->serial_num);
    sr_data->cln.len = (uint8_t)(1 + strnlen(sr_data->cln.name, sizeof(sr_data->cln.name)));

#if defined(CONFIG_BLEM_ADVERTISE_NAME_WHEN_CONNECTABLE) || defined(CONFIG_BLEM_ADVERTISE_NAME_WHEN_BROADCASTING)
    osp_ble_set_device_name(sr_data->cln.name);
#endif /* CONFIG_BLEM_ADVERTISE_NAME_ */

    g_loop = p_loop;

    ev_timer_init(&g_proximity_timer, proximity_timer_callback, 0, 0);

    LOGI("BLE initialized (connectable=%d, name=%d/%d)",
         kconfig_enabled(CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED),
         kconfig_enabled(CONFIG_BLEM_ADVERTISE_NAME_WHEN_BROADCASTING),
         kconfig_enabled(CONFIG_BLEM_ADVERTISE_NAME_WHEN_CONNECTABLE));

    return true;
}

bool blem_ble_enable(bool connectable, int interval_ms, uint8_t msg_type, const uint8_t msg[6])
{
    ble_advertising_set_t *const adv = &g_advertising_sets[BLE_ADVERTISING_SET_GENERAL];
    ble_adv_data_general_t *const adv_data = &adv->adv.data.general;
    const char *log_info;

    /* Advertising params are applied in apply_advertising() */

    adv->interval = (uint16_t)MIN(interval_ms, UINT16_MAX);

    /* Do not reset advertising if just the advertising payload is changed, because setting the mode will cause
     * disconnection of any currently connected device (which can be), or changing the pairing token to soon
     * will prevent successful pairing. */
    if (!adv->mode.enabled || (connectable != adv->mode.connectable))
    {
#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
        /* Advertisement payload is applied below, don't apply it here - this is just to check
         * whether pairing tokens can be generated and therefore connectable mode even possible. */
        if (connectable && !enable_pairing_tokens(true))
        {
            LOGE("Pairing not possible - fallback to non-connectable");
            connectable = false;
        }
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */
        adv->mode.connectable = connectable;
        adv->mode.changed = true;
        log_info = "new";
    }
    else
    {
        /* Mode was not changed, only data, which is set below in any case */
        log_info = "update";
    }

    adv->mode.enabled = true;
    adv_data->msg_type = msg_type;
    /* When advertising as connectable, protect bytes reserved for pairing token, otherwise copy whole `msg` */
    if (adv->mode.connectable)
    {
        const int n = offsetof(ble_adv_data_general_t, msg.pairing_token) - offsetof(ble_adv_data_general_t, msg);
        /* Copy msg bytes up to the pairing token */
        memcpy(&adv_data->msg, msg, n);
        /* Additionally, warn if the remaining, reserved bytes are actually being used */
        if (!memcmp_b(msg + n, 0, sizeof(adv_data->msg.pairing_token)))
        {
            LOGW("Ignoring msg[%d...]=0x%02x... because advertising as connectable", n, msg[n]);
        }
    }
    else
    {
        memcpy(&adv_data->msg, msg, sizeof(adv_data->msg));
    }
    adv->adv.changed = true;

#if defined(CONFIG_BLEM_ADVERTISE_NAME_WHEN_CONNECTABLE)
    adv->scan_rsp.enabled = adv->mode.connectable;
#elif defined(CONFIG_BLEM_ADVERTISE_NAME_WHEN_BROADCASTING)
    adv->scan_rsp.enabled = true;
#else
    adv->scan_rsp.enabled = false;
#endif
    adv->scan_rsp.changed = true;

    LOGI("BLE enabled (%s, con=%d, i=%dms, type=%d, status=0x%02x)",
         log_info, connectable, interval_ms, msg_type, msg[0]);

    advertising_apply();
    return true;
}

void blem_ble_disable(void)
{
    g_advertising_sets[BLE_ADVERTISING_SET_GENERAL].mode.enabled = false;
    advertising_apply();
    LOGI("BLE disabled");
}

static void blem_ble_log_proximity_configuration()
{
    const ble_advertising_set_t *const adv = &g_advertising_sets[BLE_ADVERTISING_SET_PROXIMITY];
    const ble_adv_data_proximity_t *const adv_data = &adv->adv.data.proximity;
    char uuid_str[37];
    char minors_str[ARRAY_SIZE(g_proximity_minors) * 6 + sizeof("[] each 65535 s")];
    char *minors_str_ptr;
    size_t minors_str_size;
    size_t num_minors;

    uuid_str[0] = '\0';
    bin2hex(adv_data->proximity_uuid, sizeof(adv_data->proximity_uuid), uuid_str, sizeof(uuid_str));

    num_minors = 0;
    minors_str_ptr = minors_str;
    minors_str_size = sizeof(minors_str);
    csnprintf(&minors_str_ptr, &minors_str_size, "[");
    for (size_t i = 0; i < ARRAY_SIZE(g_proximity_minors); i++)
    {
        if (g_proximity_minors[i] == 0)
        {
            break;
        }
        csnprintf(&minors_str_ptr, &minors_str_size, "%s%d", (num_minors > 0 ? "," : ""), g_proximity_minors[i]);
        num_minors++;
    }
    csnprintf(&minors_str_ptr, &minors_str_size, "]");

    if ((num_minors > 1) && (g_proximity_timer.repeat > 0))
    {
        csnprintf(&minors_str_ptr, &minors_str_size, " %d s each", (int)g_proximity_timer.repeat);
    }

    LOGI("BLE iBeacon configured (enable=%d, major=%d, minors=%s, meas_power=%d, UUID=%s), %.1f dBm, %d ms",
         adv->mode.enabled,
         ntohs(adv_data->major),
         minors_str,
         adv_data->measured_power,
         uuid_str,
         adv->adv_tx_power * 0.1f,
         adv->interval);
}

void blem_ble_proximity_configure(blem_ble_proximity_config_t *config, blem_ble_adv_on_state_t on_state_change)
{
    ble_advertising_set_t *const adv = &g_advertising_sets[BLE_ADVERTISING_SET_PROXIMITY];
    ble_adv_data_proximity_t *const adv_data = &adv->adv.data.proximity;
    size_t num_minors;

    if (ev_is_active(&g_proximity_timer))
    {
        ev_timer_stop(g_loop, &g_proximity_timer);
    }
    ev_timer_set(&g_proximity_timer, 0, 0);
    adv->mode.enabled = false;

    /* Check for multiple Minor IDs */
    memset(g_proximity_minors, 0, sizeof(g_proximity_minors));
    num_minors = 0;
    for (size_t i = 0; i < ARRAY_SIZE(config->minors); i++)
    {
        if (config->minors[i] != 0)
        {
            g_proximity_minors[num_minors++] = config->minors[i];
        }
    }
    if (num_minors < 1)
    {
        LOGE("No valid iBeacon Minor IDs");
        return;
    }
    if (num_minors > 1)
    {
        if (config->minor_interval > 0)
        {
            ev_timer_set(&g_proximity_timer, 0, config->minor_interval);
        }
        else
        {
            LOGW("No minor_interval, ignoring %d additional iBeacon Minor IDs", num_minors - 1);
        }
    }
    else if (config->minor_interval > 0)
    {
        LOGW("Ignoring minor_interval because of single iBeacon Minor ID");
    }

    memcpy(adv_data->proximity_uuid, config->uuid, sizeof(adv_data->proximity_uuid));
    adv_data->major = htons(config->major);
    adv_data->minor = htons(g_proximity_minors[0]); /*< Load the first minor value */
    adv_data->measured_power = config->meas_power;

    adv->interval = config->adv_interval;
    adv->adv_tx_power = config->adv_tx_power;
    adv->on_state_cb = on_state_change;
    adv->mode.enabled = config->enable;
    adv->mode.changed = true; /*< Trigger force refresh of all parameters */

    blem_ble_log_proximity_configuration();

    if (advertising_apply() && config->enable && (g_proximity_timer.repeat > 0))
    {
        ev_timer_again(g_loop, &g_proximity_timer);
    }
}

void blem_ble_close(void)
{
    if (ev_is_active(&g_proximity_timer))
    {
        ev_timer_stop(g_loop, &g_proximity_timer);
    }
    advertising_disable_all();
    advertising_apply();
    osp_ble_close();

    g_loop = NULL;
}
