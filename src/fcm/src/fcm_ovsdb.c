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
#include "memutil.h"
#include "const.h"
#include "policy_tags.h"
#include "data_report_tags.h"
#include "gatekeeper_ecurl.h"

#include "fsm_policy.h"

/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB

ovsdb_table_t table_Flow_Service_Manager_Config;
ovsdb_table_t table_FCM_Collector_Config;
ovsdb_table_t table_FCM_Report_Config;
ovsdb_table_t table_AWLAN_Node;
ovsdb_table_t table_Node_State;
ovsdb_table_t table_Node_Config;
ovsdb_table_t table_SSL;

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
bool
fcm_set_node_state(const char *module, const char *key, const char *value)
{
    struct schema_Node_State node_state;
    fcm_mgr_t *mgr;
    json_t *where;
    json_t *cond;
    bool   ret;

    mgr = fcm_get_mgr();
    where = json_array();

    cond = ovsdb_tran_cond_single("module", OFUNC_EQ, (char *)module);
    json_array_append_new(where, cond);

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, module);
    SCHEMA_SET_STR(node_state.key, key);
    SCHEMA_SET_STR(node_state.value, value);

    ret = mgr->cb_ovsdb_table_upsert_where(&table_Node_State, where, &node_state,
          false);
    if (!ret)
    {
        LOGD("%s : Error upserting Node state", __func__);
        return false;
    }

    return true;
}


/**
 * @brief Set the initial memory usage threshold.
 *
 * It can be overridden through the ovsdb Node_Config entries
 */
void
fcm_set_max_mem(void)
{
    fcm_mgr_t *mgr;
    int rc;

    mgr = fcm_get_mgr();

    /* Stash the max amount of memory available */
    rc = sysinfo(&mgr->sysinfo);
    if (rc != 0)
    {
        rc = errno;
        LOGE("%s: sysinfo failed: %s", __func__, strerror(rc));
        memset(&mgr->sysinfo, 0, sizeof(mgr->sysinfo));
    }

    mgr->max_mem = CONFIG_FCM_MEM_MAX * 1024;

    LOGI("%s: fcm default max memory usage: %" PRIu64 " kB", __func__,
         mgr->max_mem);
}


/**
 * @brief callback function invoked when tag value is
 *        changed.
 */
static bool
fcm_tag_update_cb(om_tag_t *tag,
                  struct ds_tree *removed,
                  struct ds_tree *added,
                  struct ds_tree *updated)
{
    data_report_tags_update_cb(tag, removed, added, updated);

    return true;
}


/**
 * @brief register callback for receiving tag value updates
 */
bool
fcm_tag_update_init(void)
{
    struct tag_mgr fcm_tagmgr;

    memset(&fcm_tagmgr, 0, sizeof(fcm_tagmgr));
    fcm_tagmgr.service_tag_update = fcm_tag_update_cb;
    om_tag_init(&fcm_tagmgr);
    return true;
}

/**
 * @brief processes the removal of an entry in Node_Config
 */
void
fcm_rm_node_config(struct schema_Node_Config *old_rec)
{
    char str_value[32];
    fcm_mgr_t *mgr;
    char *module;
    bool ret;
    int rc;

    module = old_rec->module;
    rc = strcmp("fcm", module);
    if (rc != 0) return;

    /* Reset the memory */
    fcm_set_max_mem();

    mgr = fcm_get_mgr();

    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    ret = fcm_set_node_state(FCM_NODE_MODULE, FCM_NODE_STATE_MEM_KEY, str_value);
    if (!ret)
    {
        LOGD("%s : Upserting Node state Failed",__func__);
        return;
    }
}

/**
 * @brief processes the addition of an entry in Node_Config
 */
void
fcm_get_node_config(struct schema_Node_Config *node_cfg)
{
    char str_value[32];
    fcm_mgr_t *mgr;
    char *module;
    long value;
    char *key;
    bool ret;
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

    ret = fcm_set_node_state(FCM_NODE_MODULE, FCM_NODE_STATE_MEM_KEY, str_value);
    if (!ret)
    {
        LOGD("%s: Upserting Node-state failed", __func__);
        return;
    }
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

        mgr->mqtt_headers[id] = CALLOC(1, sizeof(awlan->mqtt_headers[0]));
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

char *policy_session = "fcm_policy_session";
char *policy_name = "gatekeeper";

static void
fcm_update_client(void *context, struct policy_table *table)
{
    return;
}


static char *
fcm_session_name(struct fsm_policy_client *client)
{
    return policy_name;
}

static int
fcm_flush_cache(void *context, struct fsm_policy *policy)
{
    fcm_collector_t *collector = NULL;
    ds_tree_t *collect_tree = NULL;
    fcm_collect_plugin_t *plugin;
    fcm_mgr_t *mgr = NULL;
    int nrecs = 0;

    mgr = fcm_get_mgr();
    collect_tree = &mgr->collect_tree;

    ds_tree_foreach(collect_tree, collector)
    {
        plugin = &collector->plugin;
        if (plugin->process_flush_cache) nrecs = plugin->process_flush_cache(policy);
    }
    return nrecs;
}

static void
fcm_register_policy_client(void)
{
    struct fsm_policy_client *client;

    client = CALLOC(1, sizeof(*client));
    client->session = (void *)policy_session;
    client->name = policy_name;
    client->update_client = fcm_update_client;
    client->session_name = fcm_session_name;
    client->flush_cache = fcm_flush_cache;

    fsm_policy_register_client(client);
}

/**
 * @brief registered callback for SSL events
 */
static void callback_SSL(ovsdb_update_monitor_t *mon, struct schema_SSL *old_rec, struct schema_SSL *ssl)
{
    struct gk_server_info *gk_conf;
    fcm_mgr_t *mgr;

    if (!ssl || !ssl->certificate_exists || !ssl->private_key_exists) return;

    mgr = fcm_get_mgr();
    gk_conf = &mgr->gk_conf;

    snprintf(gk_conf->ssl_cert, sizeof(gk_conf->ssl_cert), "%s", ssl->certificate);
    snprintf(gk_conf->ssl_key, sizeof(gk_conf->ssl_key), "%s", ssl->private_key);
    snprintf(gk_conf->ca_path, sizeof(gk_conf->ca_path), "%s", ssl->ca_cert);

    LOGD("%s(): ssl cert %s, priv key %s, ca_path: %s",
         __func__,
         gk_conf->ssl_cert,
         gk_conf->ssl_key,
         gk_conf->ca_path);
}

/*
 * Return a value from the `other_config` map from a Flow_Service_Manager_Config row
 */
static char *fcm_get_config_other(struct schema_Flow_Service_Manager_Config *config, const char *key)
{
    int i;

    for (i = 0; i < config->other_config_len; i++)
    {
        if (strcmp(config->other_config_keys[i], key) == 0)
        {
            return config->other_config[i];
        }
    }

    return NULL;
}

void fcm_set_gk_url(struct schema_Flow_Service_Manager_Config *conf)
{
    struct gk_server_info *gk_conf;
    fcm_mgr_t *mgr;
    char *gk_url;
    int cmp;

    /* check if the handler is gatekeeper */
    cmp = strcmp(conf->handler, "gatekeeper");
    if (cmp != 0) return;

    mgr = fcm_get_mgr();
    gk_conf = &mgr->gk_conf;

    gk_url = fcm_get_config_other(conf, "gk_url");
    if (gk_url == NULL)
    {
        LOGD("%s(): failed to read gatekeeper url", __func__);
        return;
    }
    STRSCPY(gk_conf->gk_url, gk_url);

    LOGD("%s(): setting gatekeeper url %s", __func__, gk_conf->gk_url);
}

static void fcm_del_gk_url(struct schema_Flow_Service_Manager_Config *conf)
{
    struct gk_server_info *gk_conf;
    fcm_mgr_t *mgr;
    int cmp;

    /* check if the handler is gatekeeper */
    cmp = strcmp(conf->handler, "gatekeeper");
    if (cmp != 0) return;

    mgr = fcm_get_mgr();
    gk_conf = &mgr->gk_conf;

    gk_conf->server_url = NULL;
    MEMZERO(gk_conf->gk_url);
    LOGD("%s(): gatekeeper url deleted", __func__);
}

static void callback_Flow_Service_Manager_Config(
        ovsdb_update_monitor_t *mon,
        struct schema_Flow_Service_Manager_Config *old_rec,
        struct schema_Flow_Service_Manager_Config *conf)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) fcm_set_gk_url(conf);
    if (mon->mon_type == OVSDB_UPDATE_DEL) fcm_del_gk_url(old_rec);
    if (mon->mon_type == OVSDB_UPDATE_MODIFY) fcm_set_gk_url(conf);
}

int fcm_ovsdb_init(void)
{
    char str_value[32] = { 0 };
    fcm_mgr_t *mgr;
    bool ret;

    LOGI("Initializing FCM tables");

    mgr = fcm_get_mgr();

    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_INIT_NO_KEY(FCM_Collector_Config);
    OVSDB_TABLE_INIT_NO_KEY(FCM_Report_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);
    OVSDB_TABLE_INIT_NO_KEY(Flow_Service_Manager_Config);
    OVSDB_TABLE_INIT_NO_KEY(SSL);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(AWLAN_Node, false);
    OVSDB_TABLE_MONITOR(FCM_Collector_Config, false);
    OVSDB_TABLE_MONITOR(FCM_Report_Config, false);
    OVSDB_TABLE_MONITOR(Node_Config, false);
    OVSDB_TABLE_MONITOR(Flow_Service_Manager_Config, false);
    OVSDB_TABLE_MONITOR_F(SSL, ((char*[]){"ca_cert", "certificate", "private_key", NULL}));

    // Advertize default memory limit usage
    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    ret = fcm_set_node_state(FCM_NODE_MODULE, FCM_NODE_STATE_MEM_KEY, str_value);
    if (!ret)
    {
        LOGD("%s: Upserting Node-state fail", __func__);
        return -1;
    }

    fsm_policy_init();
    fcm_register_policy_client();

    return 0;
}
