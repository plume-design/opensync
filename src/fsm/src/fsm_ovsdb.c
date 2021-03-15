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
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <pcap.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <limits.h>

#include "os.h"
#include "util.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "schema.h"
#include "log.h"
#include "ds.h"
#include "json_util.h"
#include "target.h"
#include "target_common.h"
#include "fsm.h"
#include "fsm_oms.h"
#include "policy_tags.h"
#include "qm_conn.h"
#include "dppline.h"
#include "fsm_dpi_utils.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

static ovsdb_table_t table_AWLAN_Node;
static ovsdb_table_t table_Flow_Service_Manager_Config;
static ovsdb_table_t table_Openflow_Tag;
static ovsdb_table_t table_Openflow_Local_Tag;
static ovsdb_table_t table_Openflow_Tag_Group;
static ovsdb_table_t table_Node_Config;
static ovsdb_table_t table_Node_State;

/**
 * This file manages the OVSDB updates for fsm.
 * fsm monitors two tables:
 * - AWLAN_Node
 * - Flow_Service_Manager_Config table
 * AWLAN_node stores the mqtt topics used by various handlers (http, dns, ...)
 * and mqtt invariants (location id, node id)
 * The Flow_Service_Manager table stores the configuration of each handler:
 * - handler string identifier (dns, http, ...)
 * - tapping interface
 * - optional BPF filter
 *
 * The FSM manager hosts a DS tree of sessions, each session targeting
 *  a specific type of traffic. The DS tree organizing key is the handler string
 * identifier aforementioned (dns, http, ...)
 */


/**
 * @brief fsm manager.
 *
 * Container of fsm sessions
 */
static struct fsm_mgr fsm_mgr;

/**
 * @brief fsm manager accessor
 */
struct fsm_mgr *
fsm_get_mgr(void)
{
    return &fsm_mgr;
}

/**
 * @brief fsm sessions tree accessor
 */
ds_tree_t *
fsm_get_sessions(void)
{
    struct fsm_mgr *mgr;

    mgr = fsm_get_mgr();
    return &mgr->fsm_sessions;
}

static int
fsm_sessions_cmp(void *a, void *b)
{
    return strcmp(a, b);
}

#define FSM_NODE_MODULE "fsm"
#define FSM_NODE_STATE_MEM_KEY "max_mem"

/**
 * @brief fsm manager init routine
 */
void
fsm_init_mgr(struct ev_loop *loop)
{
    struct fsm_mgr *mgr;
    int rc;

    mgr = fsm_get_mgr();
    memset(mgr, 0, sizeof(*mgr));
    mgr->loop = loop;
    snprintf(mgr->pid, sizeof(mgr->pid), "%d", (int)getpid());
    ds_tree_init(&mgr->fsm_sessions, fsm_sessions_cmp,
                 struct fsm_session, fsm_node);

    /* Check the max amount of memory available */
    rc = sysinfo(&mgr->sysinfo);
    if (rc != 0)
    {
        rc = errno;
        LOGE("%s: sysinfo failed: %s", __func__, strerror(rc));
        memset(&mgr->sysinfo, 0, sizeof(mgr->sysinfo));
        mgr->max_mem = INT_MAX;
    }
    else
    {
        /* Set default to 50% of the max mem available */
        mgr->max_mem = (mgr->sysinfo.totalram * mgr->sysinfo.mem_unit) / 2;
        mgr->max_mem /= 1000; /* kB */
        LOGI("%s: fsm default max memory usage: %" PRIu64 " kB", __func__,
             mgr->max_mem);
    }
}


/**
 * @brief fsm manager reset routine
 */
void
fsm_reset_mgr(void)
{
    struct fsm_session *session;
    struct fsm_session *remove;
    struct fsm_session *next;
    ds_tree_t *sessions;
    struct fsm_mgr *mgr;

    mgr = fsm_get_mgr();
    sessions = fsm_get_sessions();
    session = ds_tree_head(sessions);
    while (session != NULL)
    {
        next = ds_tree_next(sessions, session);
        remove = session;
        ds_tree_remove(sessions, remove);
        fsm_free_session(remove);
        session = next;
    }
    free_str_tree(mgr->mqtt_headers);
}


/**
 * @brief walks the tree of sessions
 *
 * Debug function, logs each tree entry
 */
static void
fsm_walk_sessions_tree(void)
{
    ds_tree_t *sessions = fsm_get_sessions();
    struct fsm_session *session = ds_tree_head(sessions);

    LOGT("Walking sessions tree");
    while (session != NULL) {
        LOGT(", handler: %s, topic: %s",
             session->name ? session->name : "None",
             session->topic ? session->topic : "None");
        session = ds_tree_next(sessions, session);
    }
}


/**
 * @brief validate an ovsdb to private object conversion
 *
 * Expects a non NULL converted object if the number of elements
 * was not null, a null object otherwise.
 * @param converted converted object
 * @param len number of elements of the ovsdb object
 */
bool
fsm_core_check_conversion(void *converted, int len)
{
    if (len == 0) return true;
    if (converted == NULL) return false;

    return true;
}


/**
 * @brief retrieves the value from the provided key in the
 * other_config value
 *
 * @param session the fsm session owning the other_config
 * @param conf_key other_config key to look up
 */
char *
fsm_get_other_config_val(struct fsm_session *session, char *key)
{
    struct fsm_session_conf *fconf;
    struct str_pair *pair;
    ds_tree_t *tree;

    if (session == NULL) return NULL;

    fconf = session->conf;
    if (fconf == NULL) return NULL;

    tree = fconf->other_config;
    if (tree == NULL) return NULL;

    pair = ds_tree_find(tree, key);
    if (pair == NULL) return NULL;

    return pair->value;
}


static int
get_home_bridge(char *if_name, char *bridge, size_t len)
{
    char shell_cmd[1024];
    char buf[1024];

    FILE *fcmd = NULL;
    int rc =  -1;

    if (if_name == NULL) return 0;
    if (bridge == NULL) return -1;

    snprintf(shell_cmd, sizeof(shell_cmd),
             "ovs-vsctl port-to-br %s",
             if_name);

    fcmd = popen(shell_cmd, "r");
    if (fcmd ==  NULL) {
        LOG(DEBUG, "Error executing command.::shell_cmd=%s", shell_cmd);
        goto exit;
    }

    LOGT("Executing command.::shell_cmd=%s ", shell_cmd);

    while (fgets(buf, sizeof(buf), fcmd) != NULL) {
        LOGI("%s: home bridge: %s", __func__, buf);
    }

    if (ferror(fcmd)) {
        LOGE("%s: fgets() failed", __func__);
        goto exit;
    }

    rc = pclose(fcmd);
    fcmd = NULL;

    strchomp(buf, " \t\r\n");
    strscpy(bridge, buf, len);

exit:
    if (fcmd != NULL) {
        pclose(fcmd);
    }

    return rc;
}

static bool
fsm_trigger_flood_mod(struct fsm_session *session)
{
    char shell_cmd[1024] = { 0 };

    if (!fsm_plugin_has_intf(session)) return true;

    LOGT("%s: session %s set tap_flood to %s", __func__,
         session->name, session->flood_tap ? "true" : "false");

    snprintf(shell_cmd, sizeof(shell_cmd),
             "%s mod-port %s %s %s",
             "ovs-ofctl",
             session->bridge, session->conf->if_name,
             session->flood_tap ? "flood" : "no-flood");
    LOGT("%s: command %s", __func__, shell_cmd);
    return !cmd_log(shell_cmd);
}


/**
 * @brief set the tx interface a plugin might use
 *
 * @param session the fsm session
 */
static void
fsm_set_tx_intf(struct fsm_session *session)
{
    const char *name;
    size_t ret;

    name = fsm_get_other_config_val(session, "tx_intf");
    if (name != NULL)
    {
        STRSCPY(session->tx_intf, name);
        return;
    }

#if defined(CONFIG_TARGET_LAN_BRIDGE_NAME)
    name = CONFIG_TARGET_LAN_BRIDGE_NAME;
#else
    name = session->bridge;
    if ((name == NULL) || (name[0] == 0)) return;
#endif

    ret = snprintf(session->tx_intf, sizeof(session->tx_intf), "%s.tx", name);

    if (ret >= sizeof(session->tx_intf))
    {
        LOGW("%s: failed to assemble tx mirror from %s (%s), too long",
             __func__, name, session->tx_intf);
        session->tx_intf[0] = 0;
    }
}


void
fsm_process_provider(struct fsm_session *session)
{
    struct fsm_session *service;
    bool reset;

    if (session->type == FSM_WEB_CAT) return;

    /* If no provider plugin yet, attempt update */
    if (session->provider_plugin == NULL)
    {
        service = session->service;
        session->provider_plugin = service;
        if (service != NULL)
        {
            session->provider = strdup(service->name);
            session->provider_ops = &service->p_ops->web_cat_ops;
        }
        return;
    }

    LOGI("%s: original provider %s, new provider %s", __func__,
         session->provider, session->service ? session->service->name : "none");

    /* Provider was set, check if it changed */
    reset = (session->provider_plugin != session->service);

    if (reset)
    {
        sleep(2);
        LOGEM("%s: provider change detected. Restarting", __func__);
        exit(EXIT_SUCCESS);
    }
}


void
fsm_update_client(struct fsm_session *session,
                  struct policy_table *table)
{
    struct fsm_policy_client *client;

    if (session == NULL) return;

    client = &session->policy_client;
    client->table = table;
    if (session->ops.update != NULL) session->ops.update(session);
}


void
fsm_update_client_table(struct fsm_session *session, char *table_name)
{
    struct policy_table *table;
    int cmp;

    table = fsm_policy_find_table(table_name);

    /* Handle the case with no previous table set */
    if (session->policy_client.name == NULL) goto update;

    /* Handle the case with a table reset */
    if (table_name == NULL) goto update;

    /* No update */
    cmp = strcmp(session->policy_client.name, table_name);
    if (cmp == 0) return;

update:
    session->policy_client.name = table_name;
    fsm_update_client(session, table);
}


/**
 * @brief update the ovsdb configuration of the session
 *
 * Copy ovsdb fields in their fsm_session recipient
 * @param session the fsm session to update
 * @param conf a pointer to the ovsdb provided configuration
 */
bool
fsm_session_update(struct fsm_session *session,
                   struct schema_Flow_Service_Manager_Config *conf)
{
    struct fsm_session_conf *fconf;
    ds_tree_t *other_config;
    struct fsm_mgr *mgr;
    char *policy_table;
    char *flood;
    bool check;
    bool ret;

    mgr = fsm_get_mgr();

    fconf = session->conf;

    /* Free old conf. Could be more efficient */
    fsm_free_session_conf(fconf);
    session->conf = NULL;

    fconf = calloc(1, sizeof(*fconf));
    if (fconf == NULL) return false;
    session->conf = fconf;

    if (strlen(conf->handler) == 0) goto err_free_fconf;

    fconf->handler = strdup(conf->handler);
    if (fconf->handler == NULL) goto err_free_fconf;

    if (strlen(conf->if_name) != 0)
    {
        fconf->if_name = strdup(conf->if_name);
        if (fconf->if_name == NULL) goto err_free_fconf;
    }

    if (strlen(conf->pkt_capt_filter) != 0)
    {
        fconf->pkt_capt_filter = strdup(conf->pkt_capt_filter);
        if (fconf->pkt_capt_filter == NULL) goto err_free_fconf;
    }

    if (strlen(conf->plugin) != 0)
    {
        fconf->plugin = strdup(conf->plugin);
        if (fconf->plugin == NULL) goto err_free_fconf;
    }

    /* Get new conf */
    other_config = schema2tree(sizeof(conf->other_config_keys[0]),
                               sizeof(conf->other_config[0]),
                               conf->other_config_len,
                               conf->other_config_keys,
                               conf->other_config);
    check = fsm_core_check_conversion(session->conf, conf->other_config_len);
    if (!check) goto err_free_fconf;

    fconf->other_config = other_config;
    session->topic = fsm_get_other_config_val(session, "mqtt_v");

    if (fsm_plugin_has_intf(session))
    {
        ret = mgr->get_br(session->conf->if_name,
                          session->bridge,
                          sizeof(session->bridge));
        if (ret)
        {
            LOGE("%s: home bridge name not found for %s",
                 __func__, session->conf->if_name);
            goto err_free_fconf;
        }
    }

    flood = fsm_get_other_config_val(session, "flood");
    if (flood != NULL)
    {
        int cmp_yes, cmp_no;

        cmp_yes = strcmp(flood, "yes");
        cmp_no = strcmp(flood, "no");
        if (cmp_yes == 0) session->flood_tap = true;
        else if (cmp_no == 0) session->flood_tap = false;
        else
        {
            LOGD("%s: session %s: value %s invalid for key flood",
                 __func__, conf->handler, flood);
            session->flood_tap = false;
        }
    }

    ret = mgr->flood_mod(session);
    if (!ret)
    {
        LOGE("%s: flood settings for %s failed",
             __func__, session->name);
        goto err_free_fconf;
    }

    ret = fsm_get_web_cat_service(session);
    if (!ret)
    {
        LOGE("%s: web cataegorization settings for %s failed",
             __func__, session->name);
        goto err_free_fconf;
    }

    policy_table = fsm_get_other_config_val(session, "policy_table");
    fsm_update_client_table(session, policy_table);

    fsm_process_provider(session);
    fsm_set_tx_intf(session);

    ret = fsm_is_dpi(session);
    if (ret)
    {
        ret = fsm_update_dpi_context(session);
        if (!ret) goto err_free_fconf;
    }

    ret = fsm_pcap_update(session);
    if (!ret) goto err_free_fconf;

    return true;

err_free_fconf:
    fsm_free_session_conf(fconf);
    session->conf =  NULL;
    session->topic = NULL;

    return false;
}


/**
 * @brief free the ovsdb configuration of the session
 *
 * Frees the fsm_session ovsdb conf settings
 * @param session the fsm session to update
 */
void
fsm_free_session_conf(struct fsm_session_conf *conf)
{
    if (conf == NULL) return;

    free(conf->handler);
    free(conf->if_name);
    free(conf->pkt_capt_filter);
    free(conf->plugin);
    free_str_tree(conf->other_config);
    free(conf);
}


/**
 * @brief parse the session's other config map to find the
 * plugin dso
 *
 * @param session the session to parse
 */
bool
fsm_parse_dso(struct fsm_session *session)
{
    char *plugin;

    plugin = session->conf->plugin;
    if (plugin != NULL)
    {
        LOGI("%s: plugin: %s", __func__, plugin);
        session->dso = strdup(plugin);
    }
    else
    {
        char *dir = CONFIG_INSTALL_PREFIX"/lib";
        char dso[256];

        memset(dso, 0, sizeof(dso));
        snprintf(dso, sizeof(dso), "%s/libfsm_%s.so", dir, session->name);
        session->dso = strdup(dso);
    }

    LOGT("%s: session %s set dso path to %s", __func__,
         session->name, session->dso != NULL ? session->dso : "None");

    return (session->dso != NULL ? true : false);
}


/**
 * @brief initialize a plugin
 *
 * @param the session to initialize
 */
bool
fsm_init_plugin(struct fsm_session *session)
{
    int (*init)(struct fsm_session *session);
    char *dso_init;
    char init_fn[256];
    char *error;
    int rc;

    if (session->type == FSM_DPI_DISPATCH) return true;

    dlerror();
    session->handle = dlopen(session->dso, RTLD_NOW);
    if (session->handle == NULL)
    {
        LOGE("%s: dlopen %s failed: %s", __func__, session->dso, dlerror());
        return false;
    }
    dlerror();

    LOGI("%s: session name: %s, dso %s", __func__, session->name, session->dso);
    dso_init = fsm_get_other_config_val(session, "dso_init");
    if (dso_init == NULL)
    {
        memset(init_fn, 0, sizeof(init_fn));
        snprintf(init_fn, sizeof(init_fn), "%s_plugin_init", session->name);
        dso_init = init_fn;
    }

    if (dso_init == NULL) return false;

    *(int **)(&init) = dlsym(session->handle, dso_init);
    error = dlerror();
    if (error != NULL) {
        LOGE("%s: could not get init symbol %s: %s",
             __func__, dso_init, error);
        dlclose(session->handle);
        return false;
    }
    rc = init(session);

    return (rc == 0);
}

#define FSM_QM_BACKOFF_INTERVAL 20

/**
 * @brief send data to QM provided no prior connect error.
 *
 * @param compression flag
 * @param topic the mqtt topic
 * @param data to send
 * @param data_size data length
 * @return true if the data was successfully sent to QM, false otherwise
 *
 * If connecting to QM fails with the specific connect error,
 * do not attempt to reconnnect for a while.
 */
static bool
fsm_send_to_qm(qm_compress_t compress, char *topic, void *data, int data_size)
{
    struct fsm_mgr *mgr;
    qm_response_t res;
    bool backoff;
    time_t now;
    bool ret;

    mgr = fsm_get_mgr();
    if (mgr->qm_backoff != 0)
    {
	 now = time(NULL);
	 backoff = ((now - mgr->qm_backoff) < FSM_QM_BACKOFF_INTERVAL);
	 if (backoff) LOGD("%s: in back off since %ld seconds", __func__,
			   (now - mgr->qm_backoff));
	 if (backoff) return false;

	 /* Reflect that we are out of back off */
	 LOGD("%s: out of back off since %ld seconds", __func__,
	      (now - mgr->qm_backoff) - FSM_QM_BACKOFF_INTERVAL);
	 mgr->qm_backoff = 0;
    }

    ret = qm_conn_send_direct(compress, topic, data, data_size, &res);
    if (ret) return true;

    LOGE("%s: error sending mqtt with topic %s: response: %u, error: %u",
	 __func__, topic, res.response, res.error);

    if (res.error != QM_ERROR_CONNECT) return false;

    now = time(NULL);
    mgr->qm_backoff = now;

    return false;
}


/**
 * @brief send a json report over mqtt
 *
 * Emits and frees a json report
 * @param session the fsm session emitting the report
 * @param report the report to emit
 */
void
fsm_send_report(struct fsm_session *session, char *report)
{
    bool ret;

    LOGT("%s: msg len: %zu, msg: %s\n, topic: %s",
         __func__, report ? strlen(report) : 0,
         report ? report : "None", session->topic ? session->topic : "None");

    if (report == NULL) return;
    if (session->topic == NULL) goto free_report;

    ret = fsm_send_to_qm(QM_REQ_COMPRESS_DISABLE, session->topic,
			 report, strlen(report));
    if (ret) session->report_count++;

free_report:
    json_free(report);
    return;
}


/**
 * @brief send a protobuf report over mqtt
 *
 * Emits a protobuf report. Does not free the protobuf.
 * @param session the fsm session emitting the report
 * @param report the report to emit
 */
void
fsm_send_pb_report(struct fsm_session *session, char *topic,
                   void *pb_report, size_t pb_len)
{
    bool ret;

    LOGT("%s: msg len: %zu, topic: %s",
         __func__, pb_len, topic ? topic: "None");

    if (pb_report == NULL) return;
    if (topic == NULL) return;

    ret = fsm_send_to_qm(QM_REQ_COMPRESS_IF_CFG, topic,pb_report, pb_len);
    if (ret) session->report_count++;
}


/**
 * @brief allocates a FSM session
 *
 * @param conf pointer to a ovsdb record
 */
struct fsm_session *
fsm_alloc_session(struct schema_Flow_Service_Manager_Config *conf)
{
    struct fsm_session *session;
    union fsm_plugin_ops *plugin_ops;
    struct bpf_program *bpf;
    struct fsm_pcaps *pcaps;
    struct fsm_mgr *mgr;
    bool ret;

    mgr = fsm_get_mgr();

    session = calloc(1, sizeof(struct fsm_session));
    if (session == NULL) return NULL;

    session->name = strdup(conf->handler);
    if (session->name == NULL) goto err_free_session;

    session->type = fsm_service_type(conf);
    if (session->type == FSM_UNKNOWN_SERVICE) goto err_free_name;

    plugin_ops = calloc(1, sizeof(*plugin_ops));
    if (plugin_ops == NULL) goto err_free_name;

    session->p_ops = plugin_ops;
    session->mqtt_headers = mgr->mqtt_headers;
    session->location_id = mgr->location_id;
    session->node_id = mgr->node_id;
    session->loop = mgr->loop;
    session->flood_tap = false;

    session->ops.send_report = fsm_send_report;
    session->ops.send_pb_report = fsm_send_pb_report;
    session->ops.get_config = fsm_get_other_config_val;
    session->ops.state_cb = fsm_set_object_state;
    session->ops.latest_obj_cb = fsm_oms_get_highest_version;
    session->ops.last_active_obj_cb = fsm_oms_get_last_active_version;

    pcaps = NULL;
    bpf = NULL;
    if (fsm_plugin_has_intf(session))
    {
        pcaps = calloc(1, sizeof(struct fsm_pcaps));
        if (pcaps == NULL) goto err_free_plugin_ops;

        bpf = calloc(1, sizeof(struct bpf_program));
        if (bpf == NULL) goto err_free_pcaps;

        pcaps->bpf = bpf;
        session->pcaps = pcaps;
        session->p_ops->parser_ops.get_service = fsm_get_web_cat_service;
    }

    ret = fsm_session_update(session, conf);
    if (!ret) goto err_free_bpf;

    ret = fsm_parse_dso(session);
    if (!ret) goto err_free_dso;

    return session;

err_free_dso:
    free(session->dso);

err_free_bpf:
    free(bpf);

err_free_pcaps:
    free(pcaps);

err_free_plugin_ops:
    free(plugin_ops);

err_free_name:
    free(session->name);

err_free_session:
    free(session);

    return NULL;
}


/**
 * @brief frees a FSM session
 *
 * @param session a pointer to a FSM session to free
 */
void
fsm_free_session(struct fsm_session *session)
{
    struct fsm_pcaps *pcaps;
    bool ret;

    if (session == NULL) return;

    /* Free pcap resources */
    pcaps = session->pcaps;
    if (pcaps != NULL)
    {
        fsm_pcap_close(session);
        free(pcaps);
    }

    /* Call the session exit routine */
    if (session->ops.exit != NULL) session->ops.exit(session);

    /* Free optional dpi context */
    ret = fsm_is_dpi(session);
    if (ret) fsm_free_dpi_context(session);

    /* Close the dynamic library handler */
    if (session->handle != NULL) dlclose(session->handle);

    /* Free the config settings */
    fsm_free_session_conf(session->conf);

    /* Free the dso path string */
    free(session->dso);

    /* Free the plugin ops */
    free(session->p_ops);

    /* Free the session name */
    free(session->name);

    /* Free the session provider */
    free(session->provider);

    /* Finally free the session */
    free(session);
}


/**
 * @brief add a fsm session
 *
 * @param conf the ovsdb Flow_Service_Manager_Config entry
 */
void
fsm_add_session(struct schema_Flow_Service_Manager_Config *conf)
{
    struct fsm_policy_client *client;
    struct fsm_session *session;
    struct fsm_mgr *mgr;
    ds_tree_t *sessions;
    bool ret;

    mgr = fsm_get_mgr();
    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);

    if (session != NULL) return;

    /* Allocate a new session, insert it to the sessions tree */
    session = fsm_alloc_session(conf);
    if (session == NULL)
    {
        LOGE("Could not allocate session for handler %s",
             conf->handler);
        return;
    }
    ds_tree_insert(sessions, session, session->name);

    ret = mgr->init_plugin(session);
    if (!ret)
    {
        LOGE("%s: plugin handler %s initialization failed",
             __func__, session->name);
        goto err_free_session;

    }

    if (session->type == FSM_WEB_CAT)
    {
        fsm_web_cat_service_update(session, FSM_SERVICE_ADD);
    }

    client = &session->policy_client;
    client->session = session;
    client->update_client = fsm_update_client;
    fsm_policy_register_client(&session->policy_client);

    fsm_walk_sessions_tree();
    return;

err_free_session:
    ds_tree_remove(sessions, session);
    fsm_free_session(session);

    return;
}


/**
 * @brief delete a fsm session
 *
 * @param conf the ovsdb Flow_Service_Manager_Config entry
 */
void
fsm_delete_session(struct schema_Flow_Service_Manager_Config *conf)
{
    struct fsm_session *session;
    ds_tree_t *sessions;

    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);

    if (session == NULL) return;

    /* Notify parser plugins of the web categorization plugin removal */
    if (session->type == FSM_WEB_CAT)
    {
        fsm_web_cat_service_update(session, FSM_SERVICE_DELETE);
    }

    fsm_policy_deregister_client(&session->policy_client);
    ds_tree_remove(sessions, session);
    fsm_free_session(session);
    fsm_walk_sessions_tree();
}


/**
 * @brief modify a fsm session
 *
 * @param conf the ovsdb Flow_Service_Manager_Config entry
 */
void
fsm_modify_session(struct schema_Flow_Service_Manager_Config *conf)
{
    struct fsm_session *session;
    ds_tree_t *sessions;

    sessions = fsm_get_sessions();
    session = ds_tree_find(sessions, conf->handler);

    if (session == NULL) return;

    fsm_session_update(session, conf);
    if (session->ops.update != NULL) session->ops.update(session);
}


/**
 * @brief registered callback for OVSDB Flow_Service_Manager_Config events
 */
static void
callback_Flow_Service_Manager_Config(ovsdb_update_monitor_t *mon,
                                     struct schema_Flow_Service_Manager_Config *old_rec,
                                     struct schema_Flow_Service_Manager_Config *conf)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) fsm_add_session(conf);
    if (mon->mon_type == OVSDB_UPDATE_DEL) fsm_delete_session(old_rec);
    if (mon->mon_type == OVSDB_UPDATE_MODIFY) fsm_modify_session(conf);
}


/**
 * @brief gather mqtt records from AWLAN_Node's mqtt headers table
 *
 * Records the mqtt_headers (locationId, nodeId) in the fsm manager
 * Update existing fsm sessions to point to the manager's records.
 * @param awlan AWLAN_Node record
 */
void
fsm_get_awlan_headers(struct schema_AWLAN_Node *awlan)
{
    char *location = "locationId";
    struct fsm_session *session;
    struct str_pair *pair;
    char *node = "nodeId";
    struct fsm_mgr *mgr;
    ds_tree_t *sessions;
    size_t key_size;
    size_t val_size;
    size_t nelems;

    /* Get the manager */
    mgr = fsm_get_mgr();

    /* Free previous headers if any */
    free_str_tree(mgr->mqtt_headers);

    /* Get AWLAN_Node's mqtt_headers element size */
    key_size = sizeof(awlan->mqtt_headers_keys[0]);
    val_size = sizeof(awlan->mqtt_headers[0]);

    /* Get AWLAN_Node's number of elements */
    nelems = awlan->mqtt_headers_len;

    mgr->mqtt_headers = schema2tree(key_size, val_size, nelems,
                                    awlan->mqtt_headers_keys,
                                    awlan->mqtt_headers);
    if (mgr->mqtt_headers == NULL)
    {
        mgr->location_id = NULL;
        mgr->node_id = NULL;
        goto update_sessions;
    }

    /* Check the presence of locationId in the mqtt headers */
    pair = ds_tree_find(mgr->mqtt_headers, location);
    if (pair != NULL) mgr->location_id = pair->value;
    else
    {
        free_str_tree(mgr->mqtt_headers);
        mgr->mqtt_headers = NULL;
        mgr->location_id = NULL;
        mgr->node_id = NULL;
        goto update_sessions;
    }

    /* Check the presence of nodeId in the mqtt headers */
    pair = ds_tree_find(mgr->mqtt_headers, node);
    if (pair != NULL) mgr->node_id = pair->value;
    else
    {
        free_str_tree(mgr->mqtt_headers);
        mgr->mqtt_headers = NULL;
        mgr->location_id = NULL;
        mgr->node_id = NULL;
    }

update_sessions:
    /* Lookup sessions, update their header info */
    sessions = fsm_get_sessions();
    session = ds_tree_head(sessions);
    while (session != NULL) {
        session->mqtt_headers = mgr->mqtt_headers;
        session->location_id  = mgr->location_id;
        session->node_id  = mgr->node_id;
        session = ds_tree_next(sessions, session);
    }
}


/**
 * @brief delete recorded mqtt headers
 */
void
fsm_rm_awlan_headers(void)
{
    struct fsm_session *session;
    struct fsm_mgr *mgr;
    ds_tree_t *sessions;

    /* Get the manager */
    mgr = fsm_get_mgr();

    /* Free previous headers if any */
    free_str_tree(mgr->mqtt_headers);
    mgr->mqtt_headers = NULL;

    /* Lookup sessions, update their header info */
    sessions = fsm_get_sessions();
    session = ds_tree_head(sessions);
    while (session != NULL) {
        session->mqtt_headers = NULL;
        session = ds_tree_next(sessions, session);
    }
}


/**
 * @brief registered callback for AWLAN_Node events
 */
static void
callback_AWLAN_Node(ovsdb_update_monitor_t *mon,
                    struct schema_AWLAN_Node *old_rec,
                    struct schema_AWLAN_Node *awlan)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) fsm_get_awlan_headers(awlan);
    if (mon->mon_type == OVSDB_UPDATE_DEL) fsm_rm_awlan_headers();
    if (mon->mon_type == OVSDB_UPDATE_MODIFY) fsm_get_awlan_headers(awlan);
}


/**
 * @brief registered callback for Openflow_Local_Tag events
 */
static void
callback_Openflow_Local_Tag(ovsdb_update_monitor_t *mon,
                            struct schema_Openflow_Local_Tag *old_rec,
                            struct schema_Openflow_Local_Tag *tag)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) {
        om_local_tag_add_from_schema(tag);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL) {
        om_local_tag_remove_from_schema(tag);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY) {
        om_local_tag_update_from_schema(tag);
    }
}



/**
 * @brief registered callback for Openflow_Tag events
 */
static void
callback_Openflow_Tag(ovsdb_update_monitor_t *mon,
                      struct schema_Openflow_Tag *old_rec,
                      struct schema_Openflow_Tag *tag)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) {
        om_tag_add_from_schema(tag);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL) {
        om_tag_remove_from_schema(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY) {
        om_tag_update_from_schema(tag);
    }
}


/**
 * @brief registered callback for Openflow_Tag_Group events
 */
static void
callback_Openflow_Tag_Group(ovsdb_update_monitor_t *mon,
                            struct schema_Openflow_Tag_Group *old_rec,
                            struct schema_Openflow_Tag_Group *tag)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) {
        om_tag_group_add_from_schema(tag);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL) {
        om_tag_group_remove_from_schema(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY) {
        om_tag_group_update_from_schema(tag);
    }
}

/**
 * @brief Upserts the Node_State ovsdb table entry for fsm's max mem limit
 *
 * Advertizes in Node_State the max amount of memory FSM is allowed to use
 * @param module the Node_State module name
 * @param key the Node_State key
 * @param key the Node_State value
 */
void
fsm_set_node_state(const char *module, const char *key, const char *value)
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
 * @brief processes the addition of an entry in Node_Config
 */
void
fsm_get_node_config(struct schema_Node_Config *node_cfg)
{
    struct fsm_mgr *mgr;
    char str_value[32];
    char *module;
    long value;
    char *key;

    int rc;

    module = node_cfg->module;
    rc = strcmp("fsm", module);
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
    mgr = fsm_get_mgr();

    if (mgr->sysinfo.totalram == 0) return;

    mgr->max_mem = mgr->sysinfo.totalram * mgr->sysinfo.mem_unit;
    mgr->max_mem = (mgr->max_mem * value) / 100;
    mgr->max_mem /= 1000; /* kB */

    LOGI("%s: set fsm max mem usage to %" PRIu64 " kB", __func__,
         mgr->max_mem);
    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    fsm_set_node_state(FSM_NODE_MODULE, FSM_NODE_STATE_MEM_KEY, str_value);
}


/**
 * @brief processes the removal of an entry in Node_Config
 */
void
fsm_rm_node_config(struct schema_Node_Config *old_rec)
{
    struct fsm_mgr *mgr;
    char str_value[32];
    char *module;
    int rc;

    module = old_rec->module;
    rc = strcmp("fsm", module);
    if (rc != 0) return;

    /* Get the manager */
    mgr = fsm_get_mgr();
    if (mgr->sysinfo.totalram == 0) return;

    mgr->max_mem = (mgr->sysinfo.totalram * mgr->sysinfo.mem_unit) / 2;
    mgr->max_mem /= 1000; /* kB */
    LOGI("%s: fsm default max memory usage: %" PRIu64 " kB", __func__,
         mgr->max_mem);

    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    fsm_set_node_state(FSM_NODE_MODULE, FSM_NODE_STATE_MEM_KEY, str_value);
}


/**
 * @brief processes the removal of an entry in Node_Config
 */
void
fsm_update_node_config(struct schema_Node_Config *node_cfg)
{
    fsm_get_node_config(node_cfg);
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
        fsm_get_node_config(node_cfg);
    }

    if (mon->mon_type == OVSDB_UPDATE_DEL)
    {
        fsm_rm_node_config(old_rec);
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY)
    {
        fsm_update_node_config(node_cfg);
    }
}


/**
 * @brief register ovsdb callback events
 */
int
fsm_ovsdb_init(void)
{
    struct fsm_mgr *mgr;
    char str_value[32];

    LOGI("Initializing FSM tables");
    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_INIT_NO_KEY(Flow_Service_Manager_Config);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Local_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag_Group);
    OVSDB_TABLE_INIT_NO_KEY(Node_Config);
    OVSDB_TABLE_INIT_NO_KEY(Node_State);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(AWLAN_Node, false);
    OVSDB_TABLE_MONITOR(Flow_Service_Manager_Config, false);
    OVSDB_TABLE_MONITOR(Openflow_Tag, false);
    OVSDB_TABLE_MONITOR(Openflow_Local_Tag, false);
    OVSDB_TABLE_MONITOR(Openflow_Tag_Group, false);
    OVSDB_TABLE_MONITOR(Node_Config, false);

    // Initialize the plugin loader routine
    mgr = fsm_get_mgr();
    mgr->init_plugin = fsm_init_plugin;
    mgr->flood_mod = fsm_trigger_flood_mod;
    mgr->get_br = get_home_bridge;
    mgr->set_dpi_state = fsm_set_dpi_state;
    fsm_policy_init();

    // Advertize default memory limit usage
    snprintf(str_value, sizeof(str_value), "%" PRIu64 " kB", mgr->max_mem);
    fsm_set_node_state(FSM_NODE_MODULE, FSM_NODE_STATE_MEM_KEY, str_value);

    return 0;
}
