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
 * Platform Manager - Fronthaul Role Config (pm_node_fh_config)
 *
 * Implements the device side of KV-config based fronthaul role enable/disable
 * for the gateway node.
 *
 * Controller → device:
 *   The controller takes all necessary actions, updating Wifi_VIF_Config and
 *   Wifi_Inet_Config as required, and reports the resolved state to the device
 *   via cloud_fh_config.  The device reads this state on demand via
 *   pm_cloud_fh_config_get().
 *
 * Device → controller:
 *   A caller invokes pm_node_fh_config_request(role, enable), which upserts a
 *   Node_Config row:
 *        module = "node_fh_config"
 *        key    = "role_enable"
 *        value  = "<role>.<bool>"   (e.g. "hs.false")
 *   The controller detects the new row, fires GatewayFhChangeRequest, clears
 *   the entry via a KvConfig task, and reports the resolved state back to the
 *   device via cloud_fh_config.
 */

#include <string.h>
#include <stdbool.h>

#include "log.h"
#include "util.h"
#include "module.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "schema.h"

#include "pm_node_fh_config.h"

#define LOG_PREFIX "[PM:FH] "

#define PM_FH_ROLE_HS_STR       "hs"
#define PM_FH_ROLE_IOT_STR      "iot"
#define PM_FH_ROLE_GUEST_STR    "guest"
#define PM_FH_ROLE_EMPLOYEE_STR "employee"
#define PM_FH_ROLE_FLEX_STR     "flex"

/*
 * Returns true if role is one of the predefined fronthaul role strings.
 */
static bool pm_fh_role_is_valid(const char *role)
{
    if (role == NULL) return false;
    return strcmp(role, PM_FH_ROLE_HS_STR) == 0 || strcmp(role, PM_FH_ROLE_IOT_STR) == 0
           || strcmp(role, PM_FH_ROLE_GUEST_STR) == 0 || strcmp(role, PM_FH_ROLE_EMPLOYEE_STR) == 0
           || strcmp(role, PM_FH_ROLE_FLEX_STR) == 0;
}

/*
 * Public API: request a fronthaul role enable/disable change from the
 * controller.
 *
 * Upserts a Node_Config row (module=node_fh_config, key=role_enable,
 * value=<role>.<bool>).  Role must be one of the predefined role strings
 * (hs, iot, guest, employee, flex).
 */
bool pm_node_fh_config_request(const char *role, bool enable)
{
    /* Guard: only predefined role strings are accepted. */
    if (!pm_fh_role_is_valid(role))
    {
        LOGE(LOG_PREFIX "request: invalid role '%s'; must be a predefined fronthaul role",
             role != NULL ? role : "(null)");
        return false;
    }

    const char *value = strfmta("%s.%s", role, enable ? "true" : "false");
    LOGI(LOG_PREFIX "requesting fronthaul change: value='%s'", value);

    const char *column_module = SCHEMA_COLUMN(Node_Config, module);
    const char *column_key = SCHEMA_COLUMN(Node_Config, key);
    const char *column_value = SCHEMA_COLUMN(Node_Config, value);
    const char *table_node_config = SCHEMA_TABLE(Node_Config);

    json_t *where = ovsdb_where_multi(
            ovsdb_where_simple(column_module, PM_NODE_FH_CONFIG_MODULE),
            ovsdb_where_simple(column_key, PM_NODE_FH_CONFIG_KEY),
            NULL);

    json_t *row = json_object();
    json_object_set_new(row, column_module, json_string(PM_NODE_FH_CONFIG_MODULE));
    json_object_set_new(row, column_key, json_string(PM_NODE_FH_CONFIG_KEY));
    json_object_set_new(row, column_value, json_string(value));

    const bool ok = ovsdb_sync_upsert_where(table_node_config, where, row, NULL);
    if (!ok)
    {
        LOGE(LOG_PREFIX "failed to upsert Node_Config node_fh_config row");
        return false;
    }

    return true;
}

/*
 * Public API: read the resolved fronthaul role state reported by the
 * controller via cloud_fh_config.
 *
 * Reads the Node_Config row (module=cloud_fh_config, key=role_enable,
 * value=<role>.<bool>) written by the controller after processing a
 * GatewayFhChangeRequest.  Returns false if no row is present, the row
 * is malformed, or the bool field is not strictly "true" or "false".
 */
bool pm_cloud_fh_config_get(char *role, size_t role_size, bool *enable)
{
    if (role == NULL || role_size == 0 || enable == NULL) return false;

    memset(role, 0, role_size);
    *enable = false;

    const char *column_name = SCHEMA_COLUMN(Node_Config, value);
    json_t *rows = ovsdb_sync_select_where(
            SCHEMA_TABLE(Node_Config),
            ovsdb_where_multi(
                    ovsdb_where_simple(SCHEMA_COLUMN(Node_Config, module), PM_CLOUD_FH_CONFIG_MODULE),
                    ovsdb_where_simple(SCHEMA_COLUMN(Node_Config, key), PM_NODE_FH_CONFIG_KEY),
                    NULL));

    json_t *first_row = json_array_get(rows, 0);
    json_t *cell = json_object_get(first_row, column_name);
    const char *value = json_string_value(cell);
    char *owned_on_stack = strdupa(value ?: "");
    json_decref(rows);

    char *ptr = owned_on_stack;
    const char *before_dot = strsep(&ptr, ".");
    const char *after_dot = ptr;

    const bool is_true = strcmp(after_dot ?: "", "true") == 0;
    const bool is_false = strcmp(after_dot ?: "", "false") == 0;
    const bool valid_bool = is_true ^ is_false;

    if (!valid_bool) return false;
    if (*before_dot == '\0') return false;

    strscpy(role, before_dot, role_size);
    *enable = is_true;
    return true;
}

static void pm_node_fh_config_init(void *data)
{
    (void)data;
    LOGI(LOG_PREFIX "starting module");
}

static void pm_node_fh_config_fini(void *data)
{
    (void)data;
}

MODULE(pm_node_fh_config, pm_node_fh_config_init, pm_node_fh_config_fini)
