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
#include <sys/wait.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <inttypes.h>
#include <jansson.h>

#include "json_util.h"
#include "ds_list.h"
#include "evsched.h"
#include "schema.h"
#include "log.h"
#include "wm2.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "target.h"

// Defines
#define MODULE_ID LOG_MODULE_ID_MAIN

// OVSDB constants
#define OVSDB_CLIENTS_TABLE                 "Wifi_Associated_Clients"
#define OVSDB_CLIENTS_PARENT                "Wifi_VIF_State"
#define OVSDB_OPENFLOW_TAG_TABLE            "Openflow_Tag"

#define OVSDB_CLIENTS_PARENT_COL            "associated_clients"

/******************************************************************************
 *  PRIVATE definitions
 *****************************************************************************/
static int
wm2_clients_oftag_from_key_id(const char *cloud_vif_ifname,
                              const char *key_id,
                              char *oftag,
                              int len)
{
    struct schema_Wifi_VIF_Config vconf;
    ovsdb_table_t table_Wifi_VIF_Config;
    char oftagkey[32];
    char *ptr;
    bool ok;

    OVSDB_TABLE_INIT(Wifi_VIF_Config, if_name);
    ok = ovsdb_table_select_one(&table_Wifi_VIF_Config,
                                SCHEMA_COLUMN(Wifi_VIF_Config, if_name),
                                cloud_vif_ifname,
                                &vconf);
    if (!ok) {
        LOGW("%s: failed to lookup in table", cloud_vif_ifname);
        return -1;
    }

    if (strlen(SCHEMA_KEY_VAL(vconf.security, "oftag")) == 0) {
        LOGD("%s: no main oftag found, assuming backhaul/non-home interface, ignoring",
             cloud_vif_ifname);
        return 0;
    }

    if (strstr(key_id, "key-") == key_id)
        snprintf(oftagkey, sizeof(oftagkey), "oftag-%s", key_id);
    else
        snprintf(oftagkey, sizeof(oftagkey), "oftag");

    ptr = SCHEMA_KEY_VAL(vconf.security, oftagkey);
    if (!ptr || strlen(ptr) == 0)
        return -1;

    snprintf(oftag, len, "%s", ptr);
    return 0;
}

static int
wm2_clients_oftag_set(const char *mac,
                      const char *oftag)
{
    json_t *result;
    json_t *where;
    json_t *rows;
    json_t *row;
    int cnt;

    if (strlen(oftag) == 0) {
        LOGD("%s: oftag is empty, expect openflow/firewall issues", mac);
        return 0;
    }

    LOGD("%s: setting oftag='%s'", mac, oftag);

    where = ovsdb_where_simple("name", oftag);
    if (!where) {
        LOGW("%s: failed to allocate ovsdb condition, oom?", mac);
        return -1;
    }

    row = ovsdb_mutation("device_value",
                         json_string("insert"),
                         json_string(mac));
    if (!row) {
        LOGW("%s: failed to allocate ovsdb mutation, oom?", mac);
        return -1;
    }

    rows = json_array();
    if (!rows) {
        LOGW("%s: failed to allocate ovsdb mutation list, oom?", mac);
        return -1;
    }

    json_array_append_new(rows, row);

    result = ovsdb_tran_call_s(OVSDB_OPENFLOW_TAG_TABLE,
                               OTR_MUTATE,
                               where,
                               rows);
    if (!result) {
        LOGW("%s: failed to execute ovsdb transact", mac);
        return -1;
    }

    cnt = ovsdb_get_update_result_count(result,
                                        OVSDB_OPENFLOW_TAG_TABLE,
                                        "mutate");

    return cnt;
}

static int
wm2_clients_oftag_unset(const char *mac)
{
    json_t *result;
    json_t *where;
    json_t *rows;
    json_t *row;
    char col[32];
    char val[32];
    int cnt;

    LOGD("%s: removing oftag", mac);

    snprintf(col, sizeof(col), "device_value");
    tsnprintf(val, sizeof(val), "%s", mac);

    where = ovsdb_tran_cond(OCLM_STR, col, OFUNC_INC, val);
    if (!where) {
        LOGW("%s: failed to allocate ovsdb condition, oom?", mac);
        return -1;
    }

    row = ovsdb_mutation("device_value",
                         json_string("delete"),
                         json_string(mac));
    if (!row) {
        LOGW("%s: failed to allocate ovsdb mutation, oom?", mac);
        return -1;
    }

    rows = json_array();
    if (!rows) {
        LOGW("%s: failed to allocate ovsdb mutation list, oom?", mac);
        return -1;
    }

    json_array_append_new(rows, row);

    result = ovsdb_tran_call_s(OVSDB_OPENFLOW_TAG_TABLE,
                               OTR_MUTATE,
                               where,
                               rows);
    if (!result) {
        LOGW("%s: failed to execute ovsdb transact", mac);
        return -1;
    }

    cnt = ovsdb_get_update_result_count(result,
                                        OVSDB_OPENFLOW_TAG_TABLE,
                                        "mutate");

    return cnt;
}

bool
wm2_clients_update(struct schema_Wifi_Associated_Clients *schema, char *ifname, bool status)
{
    json_t                                 *where;
    json_t                                 *pwhere;
    json_t                                 *row;
    bool                                    ret;
    char                                    oftag[32];
    int                                     err;

    oftag[0] = 0;

    if (schema->key_id_exists) {
        err = wm2_clients_oftag_from_key_id(ifname,
                                            schema->key_id,
                                            oftag,
                                            sizeof(oftag));
        if (err)
            LOGW("%s: failed to convert key '%s' to oftag (%s), expect openflow/firewall issues",
                 ifname, schema->key_id, oftag);

        LOGD("%s: key_id '%s' => oftag '%s'",
             schema->mac, schema->key_id, oftag);
    }

    LOGN("Client '%s' %s %s (keyid '%s')",
          schema->mac,
          status ? "connected on" : "disconnected",
          ifname,
          schema->key_id);

    pwhere = ovsdb_tran_cond(OCLM_STR, "if_name", OFUNC_EQ,
            ifname);
    if (status) {
        // Insert client
        row = json_object();
        json_object_set_new(row, "mac", json_string(schema->mac));
        json_object_set_new(row, "key_id", json_string(schema->key_id));
        json_object_set_new(row, "state", json_string(schema->state));

        where = ovsdb_where_simple("mac", schema->mac);

        ret = ovsdb_sync_upsert_with_parent(OVSDB_CLIENTS_TABLE,
                where,
                row,
                NULL,
                OVSDB_CLIENTS_PARENT,
                pwhere,
                OVSDB_CLIENTS_PARENT_COL);
        if (!ret) {
            LOGE("Updating client %s (Failed to insert entry)",
                schema->mac);
        }

        wm2_clients_oftag_set(schema->mac, oftag);
    }
    else  {
        // Remove client
        ret = ovsdb_delete_with_parent_s(OVSDB_CLIENTS_TABLE,
                ovsdb_tran_cond(OCLM_STR,
                    "mac",
                    OFUNC_EQ,
                    (char *)schema->mac),
                OVSDB_CLIENTS_PARENT,
                pwhere,
                OVSDB_CLIENTS_PARENT_COL);
        if (!ret) {
            LOGE("Updating client %s (Failed to remove entry)",
                schema->mac);
        }

        wm2_clients_oftag_unset(schema->mac);
    }

    return true;
}

/******************************************************************************
 *  PUBLIC definitions
 *****************************************************************************/
bool
wm2_clients_init(char *if_name)
{
    if (!if_name) {
        LOGE("Initializing clients (input validation failed)" );
        return false;
    }

    if (false == target_clients_register(if_name, wm2_clients_update)) {
        return false;
    }

    return true;
}
