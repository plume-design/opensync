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
#include "policy_tags.h"
#include "qm_conn.h"
#include "dppline.h"

#define MODULE_ID LOG_MODULE_ID_OVSDB

ovsdb_table_t table_AWLAN_Node;
ovsdb_table_t table_Flow_Service_Manager_Config;
ovsdb_table_t table_Openflow_Tag;
ovsdb_table_t table_Openflow_Tag_Group;

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

static bool fsm_set_param(char *param_name, char *param,
                          size_t param_len, char *key, char *val) {
    size_t len = 0;

    if (strcmp(key, param_name) != 0) {
        return false;
    }
    len = strlen(val) + 1;

    if (len > param_len) {
        LOGE("%s: %s too long (max %zu)",
             __func__, val, sizeof(param_len));
        return false;
    }

    memset(param, 0, param_len);
    strncpy(param, val, param_len);
    return true;
}

/**
 * fsm_parse_topic: parse the session's other config map to find the
 * mqtt topic values
 * @session: the session to parse
 * @conf: the ovsdb content
 */
static bool fsm_parse_topic(struct fsm_session *session,
                            struct schema_Flow_Service_Manager_Config *conf) {
    bool ret = false;
    int i;

    if (conf->other_config_present == false) {
        return false;
    }

    LOGT("%s: setting mqtt topic parameter", __func__);
    for (i = 0; i < conf->other_config_len; i++) {
        char *key = conf->other_config_keys[i];
        char *val = conf->other_config[i];
        bool found = false;

        found = fsm_set_param("mqtt_v", session->mqtt_val,
                              sizeof(session->mqtt_val),
                              key, val);
        if (found == true) {
            break;
        }
    }
    LOGT("%s: session %s set mqtt_val to %s", __func__,
         conf->handler,
         strlen(session->mqtt_val) != 0 ? session->mqtt_val : "None");

    ret = (strlen(session->mqtt_val) != 0);
    if (ret == true) {
        session->topic = session->mqtt_val;
    }

    return ret;
}

/**
 * fsm_parse_dl_init: parse the session's other config map to find the
 * dl_init entry point
 * @session: the session to parse
 * @conf: the ovsdb content
 */
static bool fsm_parse_dl_init(struct fsm_session *session,
                              struct schema_Flow_Service_Manager_Config *conf) {
    bool ret = false;
    int i;

    if (conf->other_config_present == false) {
        return false;
    }

    LOGT("%s: setting dl init parameter", __func__);
    for (i = 0; i < conf->other_config_len; i++) {
        char *key = conf->other_config_keys[i];
        char *val = conf->other_config[i];
        bool found = false;

        found = fsm_set_param("dso_init", session->dso_init,
                              sizeof(session->dso_init),
                              key, val);
        if (found == true) {
            break;
        }
    }
    LOGT("%s: session %s set dso_init to %s", __func__,
         conf->handler,
         strlen(session->dso_init) != 0 ? session->dso_init : "None");

    ret = (strlen(session->dso_init) != 0);

    return ret;
}

/**
 * fsm_parse_dso: parse the session's other config map to find the
 * plugin dso
 * @session: the session to parse
 * @conf: the ovsdb content
 */
static bool fsm_parse_dso(struct fsm_session *session,
                          struct schema_Flow_Service_Manager_Config *conf) {
    if (strlen(conf->plugin) != 0) {
        LOGI("%s: plugin: %s", __func__, conf->plugin);
        strncpy(session->dso, conf->plugin, sizeof(session->dso));
    } else {
        char *dir = "/usr/plume/lib";

        snprintf(session->dso, sizeof(session->dso),
                 "%s/libfsm_%s.so", dir, session->name);
    }
    LOGT("%s: session %s set dso path to %s", __func__,
         session->name,
         strlen(session->dso) != 0 ? session->dso : "None");

    return true;
}

/**
 * fsm_init_plugin: initialize a plugin
 * @session: the session to initialize
 * @conf: the ovsdb content
 */
static bool fsm_init_plugin(struct fsm_session *session,
                            struct schema_Flow_Service_Manager_Config *conf) {
    void (*init)(struct fsm_session *session);
    char *error;
    bool ret = false;

    dlerror();
    session->handle = dlopen(session->dso, RTLD_NOW);
    if (session->handle == NULL) {
        LOGE("%s: dlopen %s failed: %s", __func__, session->dso, dlerror());
        return false;
    }
    dlerror();

    ret = fsm_parse_dl_init(session, conf);
    if (ret == false) {
        snprintf(session->dso_init, sizeof(session->dso_init),
                 "%s_plugin_init", session->name);
    }

    *(void **)(&init) = dlsym(session->handle, session->dso_init);
    error = dlerror();
    if (error != NULL) {
        LOGE("%s: could not get init symbol %s: %s",
             session->dso_init, __func__, error);
        dlclose(session->handle);
        return false;
    }
    init(session);
    return true;
}

static void fsm_send_report(struct fsm_session *session, char *report) {
    qm_response_t res;
    bool ret = false;

    LOGT("%s: msg len: %zu, msg: %s\n, topic: %s",
         __func__, report ? strlen(report) : 0,
         report ? report : "None", session->topic);
    if (report == NULL) {
        LOGT("No message content to send");
        return;
    }
    ret = qm_conn_send_direct(QM_REQ_COMPRESS_DISABLE, session->topic,
                                       report, strlen(report), &res);
    if (ret == false) {
        LOGE("error sending mqtt with topic %s",
             session->topic);
    }
    session->report_count++;
    json_free(report);
    return;
}

/**
 * fsm_alloc_session: allocates FSM session (DNS, HTTP ...)
 * @conf: pointer to a FSM record to store
 */
static struct fsm_session *
fsm_alloc_session(struct schema_Flow_Service_Manager_Config *conf) {
    struct fsm_session *session = NULL;
    struct bpf_program *bpf = NULL;
    struct fsm_pcaps *pcaps = NULL;
    struct fsm_mgr *mgr = fsm_get_mgr();
    bool dso_present = false;
    int i;

    session = calloc(sizeof(struct fsm_session), 1);
    if (session == NULL) {
        goto err;
    }

    pcaps = calloc(sizeof(struct fsm_pcaps), 1);
    if (pcaps == NULL) {
        goto free_session;
    }

    bpf = calloc(sizeof(struct bpf_program), 1);
    if (bpf == NULL) {
        goto free_pcaps;
    }
    pcaps->bpf = bpf;
    session->pcaps = pcaps;
    for (i = 0; i < FSM_NUM_HEADER_IDS; i++) {
        session->session_mqtt_headers[i] = mgr->mqtt_headers[i];
    }
    session->has_awlan_headers = true;
    session->loop = mgr->loop;
    session->conf = calloc(sizeof(struct schema_Flow_Service_Manager_Config), 1);
    if (session->conf == NULL) {
        goto free_bpf;
    }

    strncpy(session->name, conf->handler, sizeof(session->name));
    session->has_topic = fsm_parse_topic(session, conf);
    dso_present = fsm_parse_dso(session, conf);
    if (dso_present == false) {
        LOGE("%s: No DSO provided for handler %s",
             __func__, conf->handler);
        goto free_bpf;
    }
    memcpy(session->conf, conf, sizeof(*conf));
    session->send_report = fsm_send_report;
    return session;

  free_bpf:
    free(bpf);

  free_pcaps:
    free(pcaps);

  free_session:
    free(session);

  err:
    return NULL;
}

/**
 * fsm_free_session: frees a FSM session
 * @session: pointer to a FSM session to free
 *
 */
static void fsm_free_session(struct fsm_session *session) {
    struct fsm_pcaps *pcaps = NULL;
    if (session == NULL) {
        return;
    }
    pcaps = session->pcaps;
    if (pcaps != NULL) {
        fsm_pcap_close(session);
        free(pcaps);
    }

    if (session->exit) {
        session->exit(session);
    }

    if (session->handle != NULL) {
        dlclose(session->handle);
    }

    if (session->conf != NULL) {
        free(session->conf);
    }
    free(session);
}

/**
 * fsm_walk_sessions_tree: walks the tree of sessions
 *
 * Debug function, logs each tree entry
 */
static void fsm_walk_sessions_tree(void) {
    ds_tree_t *sessions = fsm_get_sessions();
    struct fsm_session *session = ds_tree_head(sessions);
    struct schema_Flow_Service_Manager_Config *conf = NULL;

    LOGT("Walking sessions tree");
    while (session != NULL) {
        conf = session->conf;
        LOGT("uuid: %s, key: %s, handler: %s, topic: %s",
             conf ? conf->_uuid.uuid : "None",
             session->fsm_node.otn_key ?
             (char *)session->fsm_node.otn_key : "None",
             conf ? conf->handler : "None",
             session->topic ? session->topic : "None");
        session = ds_tree_next(sessions, session);
    }
}

static void update_other_config(struct fsm_session *session,
                                struct schema_Flow_Service_Manager_Config *p) {
    struct schema_Flow_Service_Manager_Config *conf = session->conf;
    int i;

    for (i = p->other_config_len; i < conf->other_config_len; i++) {
        conf->other_config_keys[i][0] = '\0';
        conf->other_config[i][0] = '\0';
    }

    for (i = 0; i < p->other_config_len; i++) {
        char *key = p->other_config_keys[i];
        char *val = p->other_config[i];

        strncpy(conf->other_config_keys[i], key,
                sizeof(conf->other_config_keys[i]));
        strncpy(conf->other_config[i], val,
                sizeof(conf->other_config[i]));
    }
    conf->other_config_len = p->other_config_len;
}

void callback_Flow_Service_Manager_Config(ovsdb_update_monitor_t *mon,
        struct schema_Flow_Service_Manager_Config *old_rec,
        struct schema_Flow_Service_Manager_Config *conf)
{
    ds_tree_t *sessions = fsm_get_sessions();
    struct fsm_session *session = ds_tree_find(sessions, conf->handler);

    if (mon->mon_type == OVSDB_UPDATE_DEL) {
        if (session == NULL) {
            LOGE("Could not find session for handler %s",
                 conf->handler);
            return;
        }
        ds_tree_remove(sessions, session);
        fsm_free_session(session);
        fsm_walk_sessions_tree();
    }

    if (mon->mon_type == OVSDB_UPDATE_NEW) {
        bool ret = false;

        if (session != NULL) {
            LOGE("%s: session for handler %s already exists",
                 __func__, session->conf->handler);
            return;
        }

        if (session == NULL) {
            /* Allocate a new session, insert it to the sessions tree */
            session = fsm_alloc_session(conf);
            if (session == NULL) {
                LOGE("Could not allocate session for handler %s",
                     conf->handler);
                return;
            }
            ds_tree_insert(sessions, session, session->conf->handler);
        }
        ret = fsm_pcap_open(session);
        if (ret == false) {
            LOGE("pcap open failed for handler %s",
                 session->conf->handler);
            ds_tree_remove(sessions, session);
            fsm_free_session(session);
            return;
        }
        ret = fsm_init_plugin(session, conf);
        if (ret == false) {
            LOGE("%s: plugin handler %s initialization failed",
                 __func__, session->conf->handler);
            ds_tree_remove(sessions, session);
            fsm_free_session(session);
            return;
        }
        fsm_walk_sessions_tree();
    }

    if (mon->mon_type == OVSDB_UPDATE_MODIFY) {
        if (session == NULL) {
            LOGE("Could not find session for handler %s",
                 conf->handler);
            return;
        }
        update_other_config(session, conf);
        if (session->update != NULL) {
            session->update(session);
        }
    }
}

/**
 * fsm_get_awlan_header_id: maps key to header_id
 * @key: key
 */
static fsm_header_ids fsm_get_awlan_header_id(const char *key) {
    int val = strcmp(key, "locationId");

    if (val == 0) {
        return FSM_HEADER_LOCATION_ID;
    }

    val = strcmp(key, "nodeId");
    if (val == 0) {
        return FSM_HEADER_NODE_ID;
    }

    return FSM_NO_HEADER;
}

/**
 * fsm_get_awlan_headers: gather mqtt records from AWLAN_Node's
 * mqtt headers table
 * @awlan: AWLAN_Node record
 *
 * Parses the given record, looks up a matching fsm session, updates it if found
 * or allocates and inserts in sessions's tree if not.
 */
static void fsm_get_awlan_headers(struct schema_AWLAN_Node *awlan) {
    ds_tree_t *sessions = fsm_get_sessions();
    struct fsm_session *session = ds_tree_head(sessions);
    struct fsm_mgr *mgr = fsm_get_mgr();
    int i = 0;

    LOGT("%s %d", __FUNCTION__,
         awlan ? awlan->mqtt_headers_len : 0);

    for (i = 0; i < awlan->mqtt_headers_len; i++) {
        char *key = awlan->mqtt_headers_keys[i];
        char *val = awlan->mqtt_headers[i];
        fsm_header_ids id = FSM_NO_HEADER;

        LOGT("mqtt_headers[%s]='%s'", key, val);

        id = fsm_get_awlan_header_id(key);
        if (id == FSM_NO_HEADER) {
            LOG(ERR, "%s: invalid mqtt_headers key", key);
            continue;
        }
        mgr->mqtt_headers[id] = calloc(sizeof(awlan->mqtt_headers[0]), 1);
        if (mgr->mqtt_headers[id] == NULL) {
            LOGE("Could not allocate memory for mqtt header %s:%s",
                 key, val);
            i = 0; i = i / i; /* crash */
        }
        memcpy(mgr->mqtt_headers[id], val, sizeof(awlan->mqtt_headers[0]));
    }
    /* Lookup sessions, update their header info */
    while (session != NULL) {
        for (i = 0; i < FSM_NUM_HEADER_IDS; i++) {
            session->session_mqtt_headers[i] = mgr->mqtt_headers[i];
        }
        session->has_awlan_headers = true;
        session = ds_tree_next(sessions, session);
    }
}

void callback_AWLAN_Node(ovsdb_update_monitor_t *mon,
                         struct schema_AWLAN_Node *old_rec,
                         struct schema_AWLAN_Node *awlan)
{
    if (mon->mon_type != OVSDB_UPDATE_DEL) {
        fsm_get_awlan_headers(awlan);
    }
}

void callback_Openflow_Tag(ovsdb_update_monitor_t *mon,
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

void callback_Openflow_Tag_Group(ovsdb_update_monitor_t *mon,
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

int fsm_ovsdb_init(void) {
    LOGI("Initializing FSM tables");
    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);
    OVSDB_TABLE_INIT_NO_KEY(Flow_Service_Manager_Config);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag_Group);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(AWLAN_Node, false);
    OVSDB_TABLE_MONITOR(Flow_Service_Manager_Config, false);
    OVSDB_TABLE_MONITOR(Openflow_Tag, false);
    OVSDB_TABLE_MONITOR(Openflow_Tag_Group, false);
    return 0;
}
