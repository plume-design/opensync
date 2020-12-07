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

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "log.h"
#include "os.h"
#include "qm_conn.h"
#include "dppline.h"
#include "network_metadata.h"
#include "fcm.h"
#include "fcm_priv.h"
#include "fcm_mgr.h"

/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB

ovsdb_table_t table_AWLAN_Node;
ovsdb_table_t table_FCM_Collector_Config;
ovsdb_table_t table_FCM_Report_Config;
ovsdb_table_t table_Node_State;
ovsdb_table_t table_Node_Config;

#define FCM_NODE_MODULE "fcm"
#define FCM_NODE_STATE_MEM_KEY "max_mem"

/**
 * fcm_get_awlan_header_id: maps key to header_id
 * @key: key
 */
static fcm_header_ids fcm_get_awlan_header_id(const char *key)
{
    int val;

    val = strcmp(key, "locationId");
    if (val == 0) return FCM_HEADER_LOCATION_ID;

    val = strcmp(key, "nodeId");
    if (val == 0) return FCM_HEADER_NODE_ID;

    return FCM_NO_HEADER;
}

/**
 * @brief Upserts the Node_State ovsdb table entry for fcm's max mem limit
 *
 * Advertizes in Node_State the max amount of memory FCM is allowed to use
 * @param module the Node_State module name
 * @param key the Node_State key
 * @param key the Node_State value
 */
void
fcm_set_node_state(const char *module, const char *key, const char *value)
{
    struct schema_Node_State node_state;
    json_t *where;
    json_t *cond;

    where = json_array();

    cond = ovsdb_tran_cond_single("module", OFUNC_EQ, (char *)module);
    json_array_append_new(where, cond);

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, module);
    SCHEMA_SET_STR(node_state.key, key);
    SCHEMA_SET_STR(node_state.value, value);
    ovsdb_table_upsert_where(&table_Node_State, where, &node_state, false);

    json_decref(where);

    return;
}

/**
 * @brief processes the removal of an entry in Node_Config
 */
void
fcm_rm_node_config(struct schema_Node_Config *old_rec)
{
    fcm_mgr_t *mgr;
    char str_value[32];
    char *module;
    int rc;

    module = old_rec->module;
    rc = strcmp("fcm", module);
    if (rc != 0) return;

    /* Get the manager */
    mgr = fcm_get_mgr();
    if (mgr->sysinfo.totalram == 0) return;

    mgr->max_mem = (mgr->sysinfo.totalram * mgr->sysinfo.mem_unit) / 2;
    mgr->max_mem /= 1000; /* kB */
    LOGI("%s: fcm default max memory usage: %" PRIu64 " kB", __func__,
         mgr->max_mem);

    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    fcm_set_node_state(FCM_NODE_MODULE, FCM_NODE_STATE_MEM_KEY, str_value);
}

/**
 * @brief processes the addition of an entry in Node_Config
 */
void
fcm_get_node_config(struct schema_Node_Config *node_cfg)
{
    fcm_mgr_t *mgr;
    char str_value[32];
    char *module;
    long value;
    char *key;

    int rc;

    module = node_cfg->module;
    rc = strcmp("fcm", module);
    if (rc != 0) return;

    key = node_cfg->key;
    rc = strcmp("max_mem_percent", key);
    if (rc != 0) return;

    errno = 0;
    value = strtol(node_cfg->value, NULL, 10);
    if (errno != 0)
    {
        LOGE("%s: error reading value %s: %s", __func__,
             node_cfg->value, strerror(errno));
        return;
    }

    if (value < 0) return;
    if (value > 100) return;

    /* Get the manager */
    mgr = fcm_get_mgr();

    if (mgr->sysinfo.totalram == 0) return;

    mgr->max_mem = mgr->sysinfo.totalram * mgr->sysinfo.mem_unit;
    mgr->max_mem = (mgr->max_mem * value) / 100;
    mgr->max_mem /= 1000; /* kB */

    LOGI("%s: set fcm max mem usage to %" PRIu64 " kB", __func__,
         mgr->max_mem);
    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    fcm_set_node_state(FCM_NODE_MODULE, FCM_NODE_STATE_MEM_KEY, str_value);
}

/**
 * @brief processes the removal of an entry in Node_Config
 */
void
fcm_update_node_config(struct schema_Node_Config *node_cfg)
{
    fcm_get_node_config(node_cfg);
}


/**
 * fcm_get_awlan_headers: gather mqtt records from AWLAN_Node's
 * mqtt headers table
 * @awlan: AWLAN_Node record
 *
 * Parses the given record, looks up a matching FCM session, updates it if found
 * or allocates and inserts in sessions's tree if not.
 */
static void fcm_get_awlan_headers(struct schema_AWLAN_Node *awlan)
{
    fcm_mgr_t *mgr = fcm_get_mgr();

    int i = 0;

    LOGT("%s %d", __FUNCTION__,
         awlan ? awlan->mqtt_headers_len : 0);

    for (i = 0; i < awlan->mqtt_headers_len; i++)
    {
        char *key = awlan->mqtt_headers_keys[i];
        char *val = awlan->mqtt_headers[i];
        fcm_header_ids id = FCM_NO_HEADER;

        LOGT("mqtt_headers[%s]='%s'", key, val);

        id = fcm_get_awlan_header_id(key);
        if (id == FCM_NO_HEADER)
        {
            LOG(ERR, "%s: invalid mqtt_headers key", key);
            continue;
        }

        mgr->mqtt_headers[id] = calloc(1, sizeof(awlan->mqtt_headers[0]));
        if (mgr->mqtt_headers[id] == NULL)
        {
            LOGE("Could not allocate memory for mqtt header %s:%s",
                 key, val);
        }
        memcpy(mgr->mqtt_headers[id], val, sizeof(awlan->mqtt_headers[0]));
    }
}

void callback_AWLAN_Node(
        ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old_rec,
        struct schema_AWLAN_Node *awlan)
{
    if (mon->mon_type != OVSDB_UPDATE_DEL)
    {
        fcm_get_awlan_headers(awlan);
    }
}



void callback_FCM_Collector_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_FCM_Collector_Config *old_rec,
        struct schema_FCM_Collector_Config *conf)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        if (init_collect_config(conf) == false)
        {
            LOGE("%s: FCM collector plugin init failed: name %s\n",
                __func__, conf->name);
            return;
        }
        LOGD("%s: FCM collector config entry added: name  %s\n",
             __func__, conf->name);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        delete_collect_config(conf);
        LOGD("%s: FCM collector config entry deleted: name:  %s\n",
             __func__, conf->name);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        update_collect_config(conf);
        LOGD("%s: FCM collector config entry updated: name: %s\n",
             __func__, conf->name);
    }
}

void callback_FCM_Report_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_FCM_Report_Config *old_rec,
        struct schema_FCM_Report_Config *conf)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        init_report_config(conf);
        LOGD("%s: FCM reporter config entry added: name: %s",
             __func__, conf->name);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        delete_report_config(conf);
        LOGD("%s: FCM reporter config entry deleted: name: %s",
             __func__, conf->name);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        update_report_config(conf);
        LOGD("%s: FCM reporter config entry updated: name: %s",
             __func__, conf->name);
    }
}

/**
 * @brief registered callback for Node_Config events
 */
static void
callback_Node_Config(ovsdb_update_monitor_t *mon,
                    struct schema_Node_Config *old_rec,
                    struct schema_Node_Config *node_cfg)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW)
    {
        fcm_get_node_config(node_cfg);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        fcm_rm_node_config(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        fcm_update_node_config(node_cfg);
    }
}

int fcm_ovsdb_init(void)
{
    fcm_mgr_t *mgr;
    char str_value[32] = { 0 };

    LOGI("Initializing FCM tables");

    mgr = fcm_get_mgr();

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_INIT_NO_KEY(FCM_Collector_Config);
    OVSDB_TABLE_INIT_NO_KEY(FCM_Report_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(AWLAN_Node, false);
    OVSDB_TABLE_MONITOR(FCM_Collector_Config, false);
    OVSDB_TABLE_MONITOR(FCM_Report_Config, false);
    OVSDB_TABLE_MONITOR(Node_Config, false);

    // Advertize default memory limit usage
    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    fcm_set_node_state(FCM_NODE_MODULE, FCM_NODE_STATE_MEM_KEY, str_value);

    return 0;
}
