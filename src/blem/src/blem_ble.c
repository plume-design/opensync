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
#include "osp_ble.h"
#include "osp_unit.h"
#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
#include "os_random.h"
#include "json_util.h"
#include "blem_wan_config.h"
#endif



/** Main event loop used for timer operations */
static struct ev_loop *g_loop = NULL;

/** Current state of BLE advertising (either connectable or non-connectable) */
static bool g_adv_enabled = false;
/** Current advertising payload (variable fields: serial_num, msg_type, msg) */
static ble_advertising_data_t g_adv_data = {
    .len_uuid = 3u,
    .ad_type_uuid = 0x03u,
    .service_uuid = CONFIG_BLEM_GATT_SERVICE_UUID,
    .len_manufacturer = sizeof(ble_advertising_data_t) - offsetof(ble_advertising_data_t, ad_type_manufacturer),
    .ad_type_manufacturer = 0xFFu,
    .company_id = CONFIG_BLEM_MANUFACTURER_DATA_COMPANY_ID,
    .version = 5u
};
C_STATIC_ASSERT(sizeof(ble_advertising_data_t) == 28u,
                "ble_advertising_data_t is not packed to 28 B");
C_STATIC_ASSERT(sizeof(ble_advertising_data_t) - offsetof(ble_advertising_data_t, ad_type_manufacturer) == 23u,
                "ble_advertising_data_t Manufacturer Specific Data is not packed to 23 B");
#if defined(CONFIG_BLEM_ADVERTISE_NAME_WHEN_CONNECTABLE) || defined(CONFIG_BLEM_ADVERTISE_NAME_WHEN_BROADCASTING)
/**
 * Current scan response payload
 *
 * Name shall be copied to `complete_local_name` (not null-terminated), then its length added
 * to the field `len_complete_local_name` (`len_complete_local_name` += strlen(`complete_local_name`);).
 */
static ble_scan_response_data_t g_scan_response_data = {
    .len_complete_local_name = 1u,
    .ad_type_complete_local_name = 0x09u,
    .complete_local_name = ""
};
#endif /* CONFIG_BLEM_ADVERTISE_NAME_WHEN_* */
/** Flag indicating whether currently advertising as connectable, as set via @ref blem_ble_enable() */
static bool g_connectable = false;
/** Last set interval_ms, set only in @ref blem_ble_enable() */
static int g_adv_interval_ms = CONFIG_BLEM_ADVERTISING_INTERVAL;

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED

/**
 * Send advertising data (advertising packet payload) to the BT chip
 *
 * @param     connectable  Used to ensure that the pairing token is not overwritten if `msg` is not NULL.
 * @param[in] msg_type     Copied to @ref g_adv_data, if not NULL.
 * @param[in] msg          Copied to @ref g_adv_data, if not NULL.
 *
 * @return true for success
 */
static bool apply_advertising_data(bool connectable, const uint8_t *msg_type, const uint8_t msg[6]);
/** Set connectable mode */
static bool apply_advertising_mode(bool connectable);
/**
 * Apply advertising parameters saved in @ref blem_ble_enable()
 *
 * @param connectable  Advertise as connectable (true) or non-connectable (false), used for deciding whether
 *                     to enable scan response or not.
 * @param adv_enabled  Enable/start BLE advertising (true) or disable/stop it (false). Scan responses may also
 *                     be enabled, depending on `connectable` and BLEM_ADVERTISE_NAME_WHEN_* Kconfig values.
 */
static bool apply_advertising_params(bool connectable, bool adv_enable);


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
    uint8_t token_copy[sizeof(g_adv_data.msg.pairing_token)];
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
    uint8_t token[sizeof(g_adv_data.msg.pairing_token)];
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
        snprintf(psk, sizeof(psk), "%06d", passkey);

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

    memcpy(g_adv_data.msg.pairing_token, token, sizeof(g_adv_data.msg.pairing_token));

    if (!skip_apply)
    {
        /* Apply new advertising payload (as token is part of it) */
        apply_advertising_data(g_connectable, NULL, NULL);
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
    LOGI("BT device %02x:%02x:%02x:%02x:%02x:%02x connected, use passkey %06d to pair",
         bd_address[0], bd_address[1], bd_address[2], bd_address[3], bd_address[4], bd_address[5],
         get_pairing_passkey(g_adv_data.msg.pairing_token));
    /* Data will be loaded to characteristic after device is paired */
    return true;
}

static bool callback_on_device_disconnected(void)
{
    LOGI("BT device disconnected");

    /* Always get a new token for every future new device connection */
    if (!enable_pairing_tokens(false))
    {
        /* Connectable mode is not possible if the pairing token cannot be generated */
        g_connectable = false;
    }
    /* Restart advertising because device connection stopped it */
    if (g_adv_enabled)
    {
        apply_advertising_data(g_connectable, NULL, NULL);
        apply_advertising_mode(g_connectable);
        apply_advertising_params(g_connectable, true);
    }

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

static void callback_on_fatal_error(void)
{
    LOGE("Breaking ev loop because of fatal ble_config error");
    ev_break(g_loop, EVBREAK_ALL);
}

static bool apply_advertising_data(bool connectable, const uint8_t *msg_type, const uint8_t msg[6])
{
    if (msg_type != NULL)
    {
        g_adv_data.msg_type = *msg_type;
    }
    if (msg != NULL)
    {
        if (connectable)
        {
            /* Protect bytes reserved for pairing token */
            int n = sizeof(g_adv_data.msg) - sizeof(g_adv_data.msg.pairing_token);
            memcpy(&g_adv_data.msg, msg, n);
            /* But also warn if these bytes are actually being used */
            for (; n < (int)sizeof(g_adv_data.msg); n++)
            {
                if (msg[n] != 0)
                {
                    LOGW("Ignoring msg[%d...]=0x%02x... because advertising as connectable", n, msg[n]);
                    break;
                }
            }
        }
        else
        {
            memcpy(&g_adv_data.msg, msg, sizeof(g_adv_data.msg));
        }
    }

    if (osp_ble_set_advertising_data((uint8_t *) &g_adv_data, sizeof(g_adv_data)))
    {
        char msg_hex[sizeof(g_adv_data.msg) * 2 + 1] = "";

        bin2hex((uint8_t *)&g_adv_data.msg, sizeof(g_adv_data.msg), msg_hex, sizeof(msg_hex));

        LOGI("Advertising configured (connectable=%d, 0x%02x/%s)", connectable, g_adv_data.msg_type, msg_hex);
        return true;
    }
    else
    {
        LOGE("Could not set advertising payload");
        callback_on_fatal_error();
        return false;
    }
}

static bool apply_advertising_mode(bool connectable)
{
    if (!osp_ble_set_connectable(connectable))
    {
        LOGE("Could not %sable connectable", (connectable ? "en" : "dis"));
        callback_on_fatal_error();
        return false;
    }

    return true;
}

static bool apply_advertising_params(bool connectable, bool adv_enable)
{
    const bool sr_enable = connectable ? kconfig_enabled(CONFIG_BLEM_ADVERTISE_NAME_WHEN_CONNECTABLE)
                                       : kconfig_enabled(CONFIG_BLEM_ADVERTISE_NAME_WHEN_BROADCASTING);

    if (!osp_ble_set_advertising_params(adv_enable, sr_enable, g_adv_interval_ms))
    {
        LOGE("Could not set advertising params (%d, %d, %d)", adv_enable, sr_enable, g_adv_interval_ms);
        return false;
    }

    return true;
}


bool blem_ble_init(struct ev_loop *p_loop)
{
    char serial[sizeof(g_adv_data.serial_num) + 1] = "";
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

    /* Serial number in the advertising data is not null-delimited and does not change, set it */
    osp_unit_serial_get(serial, sizeof(serial));
    strncpy(g_adv_data.serial_num, serial, sizeof(g_adv_data.serial_num));

    /* "Complete List of 16-bit Service UUIDs" value is static in the advertising data - set it.
     * Note that g_adv_data.service_uuid cannot be used directly, because taking address of a
     * packed member directly may result in an unaligned pointer value. */
    LOGD("Using %s UUID 0x%04x", osp_ble_get_service_uuid(&uuid) ? "provisioned" : "default", uuid);
    g_adv_data.service_uuid = uuid;

#if defined(CONFIG_BLEM_ADVERTISE_NAME_WHEN_CONNECTABLE) || defined(CONFIG_BLEM_ADVERTISE_NAME_WHEN_BROADCASTING)
    /* Also prepare scan response data if eventually used */
    snprintf(g_scan_response_data.complete_local_name, sizeof(g_scan_response_data.complete_local_name),
             "Pod %.*s", (int)strnlen(g_adv_data.serial_num, sizeof(g_adv_data.serial_num)),
             g_adv_data.serial_num);
    g_scan_response_data.len_complete_local_name = (uint8_t)(1 + strlen(g_scan_response_data.complete_local_name));

    osp_ble_set_device_name(g_scan_response_data.complete_local_name);
#endif /* CONFIG_BLEM_ADVERTISE_NAME_WHEN_* */

    g_loop = p_loop;

    LOGI("BLE initialized (connectable=%d, name=%d/%d)",
         kconfig_enabled(CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED),
         kconfig_enabled(CONFIG_BLEM_ADVERTISE_NAME_WHEN_BROADCASTING),
         kconfig_enabled(CONFIG_BLEM_ADVERTISE_NAME_WHEN_CONNECTABLE));

    return true;
}

bool blem_ble_enable(bool connectable, int interval_ms, uint8_t msg_type, const uint8_t msg[6])
{
    const char *log_info;
    bool update_params = false;

    /* Advertising params are applied in apply_advertising_params() */
    if (g_adv_interval_ms != interval_ms)
    {
        g_adv_interval_ms = interval_ms;
        update_params = true;
    }

    /* Do not reset advertising if just the advertising payload is changed, because setting the mode will cause
     * disconnection of any currently connected device (which can be), or premature change of the pairing token
     * will prevent successful pairing. */
    if (!g_adv_enabled || (connectable != g_connectable))
    {
        /* Stop any previous advertising */
        apply_advertising_params(false, false);
        update_params = true;

#ifdef CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED
        /* Advertisement payload is applied below, don't apply it here - this is just to check
         * whether pairing tokens can be generated and therefore connectable mode even possible. */
        if (connectable && !enable_pairing_tokens(true))
        {
            LOGE("Pairing not possible - fallback to non-connectable");
            connectable = false;
        }
#endif /* CONFIG_BLEM_CONFIG_VIA_BLE_ENABLED */

        if (connectable)
        {
            /* Pairing tokens are already enabled from call to enable_pairing_tokens above */
            if (!(apply_advertising_data(true, &msg_type, msg) && apply_advertising_mode(true)))
            {
                return false;
            }
            g_connectable = true;
        }
        else {
            g_connectable = false;
            if (!(apply_advertising_data(false, &msg_type, msg) && apply_advertising_mode(false)))
            {
                return false;
            }
        }

#if defined(CONFIG_BLEM_ADVERTISE_NAME_WHEN_CONNECTABLE) || defined(CONFIG_BLEM_ADVERTISE_NAME_WHEN_BROADCASTING)
        /* Don't use sizeof as actual data can be shorter (note that adv. len does not include type) */
        if (!osp_ble_set_scan_response_data((uint8_t *)&g_scan_response_data,
                                            1 + g_scan_response_data.len_complete_local_name))
        {
            LOGE("Could not set scan response data");
        }
#endif /* CONFIG_BLEM_ADVERTISE_NAME_WHEN_* */

        g_adv_enabled = true;
        log_info = "new";
    }
    else
    {
        /* Mode was not changed, only data */
        if (!apply_advertising_data(g_connectable, &msg_type, msg))
        {
            return false;
        }
        log_info = "update";
    }

    /* Only set advertising parameters if actually changed to prevent disconnection of potentially connected device */
    if (update_params && !apply_advertising_params(g_connectable, true))
    {
        return false;
    }

    LOGI("BLE enabled (%s, con=%d, i=%dms, type=%d, status=0x%02x)",
         log_info, connectable, interval_ms, msg_type, msg[0]);

    return true;
}

void blem_ble_disable(void)
{
    apply_advertising_params(false, false);
    g_connectable = false;
    g_adv_enabled = false;
    LOGI("BLE disabled");
}

void blem_ble_close(void)
{
    blem_ble_disable();
    osp_ble_close();

    g_loop = NULL;
}
