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

/*
 * ===========================================================================
 *  BLE WAN Configuration Management
 * ===========================================================================
 */
#include "execsh.h"
#include "json_util.h"
#include "log.h"
#include "osp_ps.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "schema.h"

#include "blem_wan_config.h"

/*
 * Import the JSON schema definition
 */
#include "wan_local_config.pjs.h"
#include "pjs_gen_h.h"

#include "wan_local_config.pjs.h"
#include "pjs_gen_c.h"

static bool ble_wan_config_upgrade(void);
static bool ble_wan_config_restart(void);
static bool ble_wan_config_ovsdb_get(struct wan_local_config *wc);
static bool ble_wan_config_ovsdb_set(struct wan_local_config *wc);
static bool ble_wan_config_ps_get(struct wan_local_config *wc);
static bool ble_wan_config_ps_erase(void);
static bool ble_wan_config_is_empty(struct wan_local_config *wc);
static json_t *ble_wan_config_ovsdb_row(bool enable, char *type, ...);

static const char *wan_config_other_config_get(
        struct schema_WAN_Config *wan_config,
        const char *key);

/*
 * ===========================================================================
 *  Main API
 * ===========================================================================
 */

/*
 * Initialize the BLE WAN configuration subsystem.
 *
 * The main purpose of this function (for now) is to upgrade the legacy
 * persistent storage database to the new WAN_Config persistent OVSDB table.
 */
void ble_wan_config_init(void)
{
    ble_wan_config_upgrade();
}

/*
 * Retrieve the current WAN configuration and return it in the JSON BLE format
 */
json_t *ble_wan_config_get(void)
{
    struct wan_local_config wc;
    pjs_errmsg_t err;
    json_t *jwc;

    if (!ble_wan_config_ovsdb_get(&wc))
    {
        LOG(ERR, "Unable to retrieve current WAN config from OVSDB.");
        return NULL;
    }

    jwc = wan_local_config_to_json(&wc, err);
    if (jwc == NULL)
    {
        LOG(WARN, "Error creating WAN local_config JSON.");
        return NULL;
    }

    return jwc;
}

/*
 * Set the current WAN configuration. This function takes a WAN configuration
 * in the BLE JSON format and writes it to OVSDB.
 */
bool ble_wan_config_set(json_t *jwan)
{
    struct wan_local_config wc;
    pjs_errmsg_t perr;

    if (!wan_local_config_from_json(&wc, jwan, false, perr))
    {
        LOG(ERR, "Error parsing WAN localconfig: %s", perr);
        return false;
    }

    if (!ble_wan_config_ovsdb_set(&wc))
    {
        LOG(ERR, "Error setting WAN configuration in OVSDB.");
        return false;
    }

    if (!ble_wan_config_restart())
    {
        LOG(ERR, "Error restarting WAN (from config_set).");
        return false;
    }

    return true;
}

/*
 * ===========================================================================
 *  Helper functions
 * ===========================================================================
 */

/*
 * Upgrade the persistent storage WAN config database (pre 4.2.0) to the
 * OVSDB WAN_Config persistent table.
 *
 * The upgrade will be performed only when the current WAN_Config is empty
 * and there's a valid local_config:wan persistent storage database.
 *
 * If the upgrade is successful, the local_config:wan store will be wiped clean.
 *
 * The upgrade algorithm is as follows:
 * +-------------------------------------------------------+
 * | WAN_Config   | Legacy Config |           Action       |
 * +-------------------------------------------------------+
 * |      X       |       X       |  Erase legacy config   |
 * +-------------------------------------------------------+
 * |              |       X       |  Upgrade then erase    |
 * +-------------------------------------------------------+
 * |      X       |               |      No action         |
 * +-------------------------------------------------------+
 */
bool ble_wan_config_upgrade(void)
{
    struct wan_local_config ps_wc;
    struct wan_local_config ovsdb_wc;

    if (!ble_wan_config_ps_get(&ps_wc))
    {
        LOG(NOTICE, "Legacy WAN configuration not present. Not upgrading.");
        /* No persistent storage WAN configuration available, just return success */
        return true;
    }

    /* Retrieve the current OVSDB configuration and check if it is not empty */
    if (!ble_wan_config_ovsdb_get(&ovsdb_wc))
    {
        LOG(ERR, "WAN configuration upgrade: Cannot retrieve current OVSDB WAN configuration. Aborting.");
        return false;
    }

    /* Perform the upgrade only if the current WAN configuration is empty.
     *
     * The upgrade process is as follows:
     *   - Exit on error
     *   - If the current OVSDB WAN Configuration is not empty, then
     *     write the persistent storage configuration to OVSDB
     *   - Wipe clean the current persistent storage config
     */
    if (!ble_wan_config_is_empty(&ovsdb_wc))
    {
        LOG(NOTICE, "WAN_Config is not empty. Legacy WAN configuration will not be applied.");
    }
    else if (ble_wan_config_ovsdb_set(&ps_wc))
    {
        LOG(NOTICE, "Legacy WAN configuration was transferred to WAN_Config.");
        if (!ble_wan_config_restart())
        {
            LOG(WARN, "Error restarting WAN (from upgrade).");
        }
    }
    else
    {
        LOG(ERR, "WAN configuration upgrade: Error writing OVSDB configuration.");
        return false;
    }

    LOG(NOTICE, "Erasing legacy WAN configuration.");
    if (!ble_wan_config_ps_erase())
    {
        LOG(ERR, "WAN configuration upgrade: Error erasing old configuration.");
        return false;
    }

    return true;
}

/*
 * Signal WANO that the WAN configuration should be restarted.
 *
 * This is done by sending the HUP signal to the WANO process.
 */
bool ble_wan_config_restart(void)
{
    if (EXECSH_LOG(DEBUG, SHELL(killall -HUP wano)) != 0)
    {
        LOG(DEBUG, "Error signalling WANO to restart WAN provisioning.");
        return false;
    }
    return true;
}


/*
 * Get the current WAN configuration from the `WAN_Config` table.
 *
 * This function reads the current WAN configuration from OVSDB and converts it
 * to the BLE format (wan_local_config -- C structure representing the BLE
 * JSON schema).
 *
 * Note: The wan_local_config has a limitation that it supports a single entry
 * per WAN type. The OVSDB table on the other hand supports multiple entries.
 * This function returns the highest priority entry.
 */
bool ble_wan_config_ovsdb_get(struct wan_local_config *wc)
{
    struct schema_WAN_Config wan_config;
    pjs_errmsg_t err;
    json_t *row;
    size_t ii;

    int64_t last_pppoe_priority = INT64_MIN;
    int64_t last_vlan_priority = INT64_MIN;
    int64_t last_static_priority = INT64_MIN;
    json_t *select = NULL;

    /*
     * Convert the schema to the wan_local_config structure
     */
    memset(wc, 0, sizeof(*wc));
    STRSCPY(wc->wanConnectionType, "dynamic");

    /*
     * Get the current WAN configuration from the `WAN_Config` table.
     */
    select = ovsdb_sync_select_where2(
            SCHEMA_TABLE(WAN_Config),
            ovsdb_where_simple_typed(
                    SCHEMA_COLUMN(WAN_Config, os_persist),
                    "true",
                    OCLM_BOOL));
    if (select == NULL)
    {
        LOG(ERR, "Unable to retrieve OVSDB WAN configuration. Select failed.");
        return false;
    }

    json_array_foreach(select, ii, row)
    {
        if (!schema_WAN_Config_from_json(&wan_config, row, true, err))
        {
            LOG(ERR, "Error parsing WAN_Config: %s", err);
            continue;
        }

        if (strcmp(wan_config.type, "pppoe") == 0)
        {
            /* Process only the highest priority entry */
            if (last_pppoe_priority > wan_config.priority) continue;
            last_pppoe_priority = wan_config.priority;

            const char *username = wan_config_other_config_get(&wan_config, "username");
            const char *password = wan_config_other_config_get(&wan_config, "password");
            if (username == NULL || password == NULL)
            {
                LOG(ERR, "PPPoE WAN config is missing the `username` or `password` setting. Ignoring.");
                continue;
            }

            wc->PPPoE_exists = true;
            wc->PPPoE.enabled = wan_config.enable;
            STRSCPY(wc->PPPoE.username, username);
            STRSCPY(wc->PPPoE.password, password);
        }
        else if (strcmp(wan_config.type, "vlan") == 0)
        {
            /* Process only the highest priority entry */
            if (last_vlan_priority > wan_config.priority) continue;
            last_vlan_priority = wan_config.priority;

            const char *svlan = wan_config_other_config_get(&wan_config, "vlan_id");
            if (svlan == NULL)
            {
                LOG(ERR, "VLAN WAN config is missing the `vlan_id` setting. Ignoring.");
                continue;
            }

            wc->DataService_exists = true;
            wc->DataService.enabled = wan_config.enable;
            wc->DataService.VLAN = atoi(svlan);
        }
        else if (strcmp(wan_config.type, "static_ipv4") == 0)
        {
            /* Process only the highest priority entry */
            if (last_static_priority > wan_config.priority) continue;
            last_static_priority = wan_config.priority;

            const char *ip = wan_config_other_config_get(&wan_config, "ip");
            const char *subnet = wan_config_other_config_get(&wan_config, "subnet");
            const char *gateway = wan_config_other_config_get(&wan_config, "gateway");
            const char *primary_dns = wan_config_other_config_get(&wan_config, "primary_dns");
            const char *secondary_dns = wan_config_other_config_get(&wan_config, "secondary_dns");

            if (ip == NULL || subnet == NULL || gateway == NULL || primary_dns == NULL)
            {
                LOG(ERR, "Static IPv4 WAN config is missing the"
                        " `ip`"
                        " `subnet`"
                        " `gateway`"
                        " or the `primary_dns` settings. Ignoring.");
                continue;
            }

            wc->staticIPv4_exists = true;
            wc->staticIPv4.enabled = wan_config.enable;
            STRSCPY(wc->staticIPv4.ip, ip);
            STRSCPY(wc->staticIPv4.subnet, subnet);
            STRSCPY(wc->staticIPv4.gateway, gateway);
            STRSCPY(wc->staticIPv4.primaryDns, primary_dns);
            if (secondary_dns != NULL)
            {
                STRSCPY(wc->staticIPv4.secondaryDns, secondary_dns);
            }
        }
        else if (strcmp(wan_config.type, "dhcp") == 0)
        {
            /*
             * DHCP is the default type, so it should be equivalent to using
             * an empty WAN config
             */
            if (last_static_priority > wan_config.priority) continue;
            last_static_priority = wan_config.priority;
        }
        else
        {
            LOG(WARN, "Unknown WAN type: %s", wan_config.type);
        }
    }

    json_decref(select);

    return true;
}

bool ble_wan_config_ovsdb_set(struct wan_local_config *wc)
{
    json_t *trow;
    json_t *val;
    size_t ii;

    json_t *result = NULL;
    bool success = false;
    json_t *trans = NULL;

    /*
     * The first operation in the transaction is to delete the whole table. If
     * subsequent insert operations fail, the whole transaction will be reverted.
     * This guarantees that there's no partial data is present WAN_Config.
     */
    trans = ovsdb_tran_multi(NULL, NULL, SCHEMA_TABLE(WAN_Config), OTR_DELETE, NULL, NULL);

    if (wc->PPPoE_exists)
    {
        /*
         * Format of the PPPoE object:
         *
         * "PPPoE": {                       # If this object is not present, "enabled" is assumed false
         *    "enabled": true,              # Set by partner, meaning customer shall fill in username and password
         *    "username": "plume",          # Filled in by the customer
         *    "password": "Plume1234!"      # Filled in by the customer
         * }
         */
        trow = ble_wan_config_ovsdb_row(
                wc->PPPoE.enabled,
                "pppoe",
                "username", wc->PPPoE.username,
                "password", wc->PPPoE.password,
                NULL);

        trans = ovsdb_tran_multi(trans, NULL, SCHEMA_TABLE(WAN_Config), OTR_INSERT, NULL, trow);
        if (trans == NULL)
        {
            LOG(ERR, "Error appending PPPoE object to transaction: %s",
                    json_dumps_static(trow, 0));
            goto exit;
        }
    }

    if (wc->DataService_exists)
    {
        char vlan[C_VLAN_LEN];
        /*
         * Format of the DataService object:
         *
         * "DataService": {                 # If this object is not present, "enabled" is assumed false
         *     "enabled": true,
         *     "VLAN": 0,                   # Uplink VLAN tagging
         *     "QoS": 0                     # QoS tagging to apply
         * }
         */
        snprintf(vlan, sizeof(vlan), "%d", wc->DataService.VLAN);
        trow = ble_wan_config_ovsdb_row(
                wc->DataService.enabled,
                "vlan",
                "vlan_id", vlan,
                NULL);

        trans = ovsdb_tran_multi(trans, NULL, SCHEMA_TABLE(WAN_Config), OTR_INSERT, NULL, trow);
        if (trans == NULL)
        {
            LOG(ERR, "Error appending DataService object to transaction: %s",
                    json_dumps_static(trow, 0));
            goto exit;
        }
    }

    if (wc->staticIPv4_exists)
    {
        /*
         * Format of the staticIPv4 object:
         *
         * "staticIPv4": {                  # If this object is not present, "enabled" is assumed false
         *      "enabled": true,            # Set by partner, meaning customer shall fill in any missing fields below
         *      "ip": "35.222.58.226",      # Filled in by partner or customer
         *      "subnet": "255.255.255.0",  # Filled in by partner or customer
         *      "gateway": "35.222.58.1",   # Filled in by partner or customer
         *      "primaryDns": "1.1.1.1",    # Filled in by partner or customer
         *      "secondaryDns": "1.0.0.1"   # Filled in by partner or customer
         * }
         */
        trow = ble_wan_config_ovsdb_row(
                wc->staticIPv4.enabled,
                "static_ipv4",
                "ip", wc->staticIPv4.ip,
                "subnet", wc->staticIPv4.subnet,
                "gateway", wc->staticIPv4.gateway,
                "primary_dns", wc->staticIPv4.primaryDns,
                "secondary_dns", wc->staticIPv4.secondaryDns,
                NULL);

        trans = ovsdb_tran_multi(trans, NULL, SCHEMA_TABLE(WAN_Config), OTR_INSERT, NULL, trow);
        if (trans == NULL)
        {
            LOG(ERR, "Error appending staticIPv4 object to transaction: %s",
                    json_dumps_static(trow, 0));
            goto exit;
        }
    }

    result = ovsdb_method_send_s(MT_TRANS, trans);
    /*
     * ovsdb_method_send_s() steals the reference to `trans`. Set it to null afterwards
     * so we don't free it below.
     */
    trans = NULL;

    /* Check if any of the inserts failed */
    json_array_foreach(result, ii, val)
    {
        if (json_object_get(val, "error") != NULL)
        {
            LOG(ERR, "Error inserting WAN configuration: %s", json_dumps_static(val, 0));
            goto exit;
        }
    }

    success = true;

exit:
    json_decref(trans);

    return success;
}

/*
 * Retrieve the WAN configuration from persistent storage
 */
bool ble_wan_config_ps_get(struct wan_local_config *wc)
{
    json_error_t jerr;
    pjs_errmsg_t perr;
    ssize_t pwan_sz;
    ssize_t rc;

    bool retval = false;
    osp_ps_t *ps = NULL;
    json_t *jwan = NULL;
    char *pwan = NULL;

    ps = osp_ps_open("local_config", OSP_PS_READ | OSP_PS_PRESERVE);
    if (ps == NULL)
    {
        goto exit;
    }

    pwan_sz = osp_ps_get(ps, "wan", NULL, 0);
    pwan = MALLOC(pwan_sz + 2);

    rc = osp_ps_get(ps, "wan", pwan, pwan_sz);
    if (rc <= 0 || rc > pwan_sz)
    {
        LOG(DEBUG, "No legacy WAN configuration present.");
        goto exit;
    }

    /*
     * json_loadb() freaks out if there's a \x00 at the end of string, so strip
     * it out
     */
    for (; pwan_sz > 0; pwan_sz--)
    {
        if (pwan[pwan_sz - 1] != '\0') break;
    }

    jwan = json_loadb(pwan, pwan_sz, 0, &jerr);
    if (jwan == NULL)
    {
        LOG(ERR, "Error parsing JSON from persistent store WAN: %s (line %d).",
                jerr.text,
                jerr.line);
        goto exit;
    }

    memset(wc, 0, sizeof(*wc));
    if (!wan_local_config_from_json(wc, jwan, false, perr))
    {
        LOG(ERR, "Error parsing persistent store WAN: %s", perr);
        goto exit;
    }

    retval = true;

exit:
    if (ps != NULL) osp_ps_close(ps);
    FREE(pwan);
    json_decref(jwan);

    return retval;
}

/*
 * Erase the current persistent storage configuration
 */
bool ble_wan_config_ps_erase(void)
{
    bool retval = false;
    osp_ps_t *ps = NULL;

    ps = osp_ps_open("local_config", OSP_PS_WRITE | OSP_PS_PRESERVE);
    if (ps == NULL)
    {
        goto exit;
    }

    if (osp_ps_set(ps, "wan", NULL, 0) < 0)
    {
        LOG(ERR, "Error erasing store local_config:wan");
        goto exit;
    }

    retval = true;

exit:
    if (ps != NULL) osp_ps_close(ps);

    return retval;
}


/*
 * Return a value from the `other_config` map from a WAN_Config row
 */
const char *wan_config_other_config_get(
        struct schema_WAN_Config *wan_config,
        const char *key)
{
    int ii;

    for (ii = 0; ii < wan_config->other_config_len; ii++)
    {
        if (strcmp(wan_config->other_config_keys[ii], key) == 0)
        {
            return wan_config->other_config[ii];
        }
    }

    return NULL;
}

/*
 * Return true whether the configuration is empty
 */
bool ble_wan_config_is_empty(struct wan_local_config *wc)
{
    if (wc->PPPoE_exists) return false;
    if (wc->DataService_exists) return false;
    if (wc->staticIPv4_exists) return false;

    return true;
}

/*
 * Construct a `WAN_Config` row representation in JSON that can be used in
 * an OVSDB transaction.
 *
 * Options after `enable` represent key/value paris for the `other_config` column
 * and must be specified in pairs.
 *
 * If a value is NULL, the key/pair will be skipped.
 *
 * This function returns a JSON object with a reference count of 1.
 */
json_t *ble_wan_config_ovsdb_row(bool enable, char *type, ...)
{
    va_list va;
    json_t *other_config;
    json_t *map;

    /* Parse variable arguments and create a map out of it */
    map = json_array();
    va_start(va, type);
    while (true)
    {
        const char *key = va_arg(va, const char *);
        if (key == NULL) break;

        const char *val = va_arg(va, const char *);
        if (val == NULL) continue;

        json_array_append_new(map, json_pack("[s, s]", key, val));
    }
    va_end(va);

    other_config = json_pack("[s,o]", "map", map);

    json_t *row = json_object();
    json_object_set_new(row, "type", json_string(type));
    json_object_set_new(row, "enable", json_boolean(enable));
    json_object_set_new(row, "os_persist", json_true());
    json_object_set_new(row, "other_config", other_config);

    return row;
}
