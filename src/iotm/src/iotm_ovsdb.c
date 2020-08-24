/*
Copyright (c) 2020, Charter Communications Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   3. Neither the name of the Charter Communications Inc. nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Charter Communications Inc. BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <stddef.h>
#include <time.h>
#include <dlfcn.h>

#include "const.h"
#include "log.h"
#include "assert.h"
#include "json_util.h"
#include "ovsdb.h"
#include "ovsdb_cache.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "schema.h"

#include "iotm.h"
#include "iotm_ovsdb.h"
#include "iotm_ovsdb_private.h"
#include "iotm_tag.h"
#include "iotm_event.h"
#include "iotm_rule.h"
#include "iotm_session.h"
#include "iotm_service.h"
#include "iotm_tl.h"

#define TAG_TABLE "Openflow_Tag"
#define RULE_TABLE "IOT_Rule_Config"

/* Log entries from this file will contain "OVSDB" */
#define MODULE_ID LOG_MODULE_ID_OVSDB

// static ovsdb_update_monitor_t ofc_client_ovsdb_mon;
ovsdb_table_t table_IOT_Rule_Config;
ovsdb_table_t table_IOT_Manager_Config;
ovsdb_table_t table_Openflow_Tag;
ovsdb_table_t table_Openflow_Tag_Group;
ovsdb_table_t table_AWLAN_Node;

/* Begin helpers for Data Structures */
static struct iotm_mgr iotm_mgr;

static int iotm_events_cmp(void *a, void *b)
{
    return strcmp(a, b);
}


static int iotm_session_cmp(void *a, void *b)
{
    return strcmp(a, b);
}

struct iotm_mgr *iotm_get_mgr(void) 
{
    return &iotm_mgr;
}

struct iotm_tree_t *iotm_get_tags(void)
{
    struct iotm_mgr *mgr;
    mgr = iotm_get_mgr();
    return mgr->tags;
}

ds_tree_t *iotm_get_events()
{
    struct iotm_mgr *mgr;
    mgr = iotm_get_mgr();
    return &mgr->events;
}

/**
 * @brief iotm sessions tree accessor
 */
    ds_tree_t *
iotm_get_sessions(void)
{
    struct iotm_mgr *mgr;

    mgr = iotm_get_mgr();
    return &mgr->iotm_sessions;
}

void iotm_init_mgr(struct ev_loop *loop)
{
    struct iotm_mgr *mgr;

    mgr = iotm_get_mgr();
    memset(mgr, 0, sizeof(*mgr));
    mgr->loop = loop;
    mgr->tags = iotm_tree_new();
    mgr->tl_ctx_tree = tl_tree_new();
    snprintf(mgr->pid, sizeof(mgr->pid), "%d", (int)getpid());

    ds_tree_init(&mgr->events, iotm_events_cmp,
            struct iotm_event, iotm_event_node);

    ds_tree_init(&mgr->iotm_sessions, iotm_session_cmp,
            struct iotm_session, iotm_sess_node);
}


int iotm_teardown_mgr(void)
{
    iotm_rm_awlan_headers();
    struct iotm_session *last = NULL;

    ds_tree_t *sessions = iotm_get_sessions();
    struct iotm_session *session = ds_tree_head(sessions);
    while (session != NULL)
    {
        last = session;
        session = ds_tree_next(sessions, session);
        iotm_free_session(last);
    }

    ds_tree_t *events = iotm_get_events();
    if ( events == NULL ) return 0;
    struct iotm_event *e_last = NULL;
    struct iotm_event *event = ds_tree_head(events);
    while (event != NULL)
    {
        e_last = event;
        event = ds_tree_next(events, event);
        iotm_event_free(e_last);
    }

    struct iotm_mgr *mgr = NULL;
    mgr = iotm_get_mgr();
    if (mgr->tags) iotm_tree_free(mgr->tags);

    tl_tree_free(mgr->tl_ctx_tree);

    return 0;
}

struct iotm_rule *iotm_get_rule(char *name, char *ev_key)
{
    ds_tree_t *rules;
    struct iotm_event *event;

    event = iotm_event_get(ev_key);
    if ( event == NULL ) return NULL;

    rules = &event->rules;
    if ( rules == NULL ) return NULL;

    return ds_tree_find(rules, name);
}

/**
 * @brief send a json report over mqtt
 *
 * Emits and frees a json report
 * @param session the iotm session emitting the report
 * @param report the report to emit
 */
void iotm_send_report(struct iotm_session *session, char *report)
{
    qm_response_t res;
    bool ret = false;

    LOGT("%s: msg len: %zu, msg: %s\n, topic: %s",
            __func__, report ? strlen(report) : 0,
            report ? report : "None", session->topic ? session->topic : "None");

    if (report == NULL) return;
    if (session->topic == NULL) goto free_report;

    ret = qm_conn_send_direct(QM_REQ_COMPRESS_DISABLE, session->topic,
            report, strlen(report), &res);
    if (ret == false)
    {
        LOGE("%s: error sending mqtt with topic %s", __func__, session->topic);
    }
    session->report_count++;

free_report:
    json_free(report);
    return;
}

void iotm_send_pb_report(struct iotm_session *session, char *topic,
        void *pb_report, size_t pb_len)
{
    qm_response_t res;
    bool ret = false;

    LOGT("%s: msg len: %zu, topic: %s",
            __func__, pb_len, topic ? topic: "None");

    if (pb_report == NULL) return;
    if (topic == NULL) return;

    ret = qm_conn_send_direct(QM_REQ_COMPRESS_IF_CFG, topic,
            pb_report, pb_len, &res);
    if (ret == false) 
    {
        LOGE("%s: error sending mqtt with topic %s", __func__, topic);
    }
    session->report_count++;

    return;
}

int ovsdb_upsert_tag(char *tag, struct schema_Openflow_Tag *row)
{
    bool ret = false;
    json_t *where = NULL;
    json_t *j_row = NULL;
    pjs_errmsg_t perr;

    where = ovsdb_tran_cond(
            OCLM_STR,
            "name",
            OFUNC_EQ,
            tag);

    j_row = schema_Openflow_Tag_to_json(row, perr);

    if (j_row == NULL)
    {
        LOGE("%s: error converting tag row to json: %s",
                __func__, perr);
        return -1;
    }

    ret = ovsdb_sync_upsert_where(TAG_TABLE, where, j_row, NULL);

    if (ret)
    {
        LOGI("%s: Updated the openflow_tag entry for [%s]",
                __func__, row->name);
        return 0;
    }

    LOGE("%s: Failed to upsert the row into the table %s.",
            __func__, TAG_TABLE);
    return -1;
}

int ovsdb_upsert_rules(
        struct schema_IOT_Rule_Config rows[],
        size_t num_rules)
{
    bool ret = false;
    json_t *where = NULL;
    json_t *j_row = NULL;
    pjs_errmsg_t perr;

    for (size_t i = 0; i < num_rules; i++)
    {
        struct schema_IOT_Rule_Config *row = &rows[i];
        schema_IOT_Rule_Config_mark_all_present(row);

        where = ovsdb_tran_cond(
                OCLM_STR,
                "name",
                OFUNC_EQ,
                row->name);

        j_row = schema_IOT_Rule_Config_to_json(row, perr);

        if (j_row == NULL)
        {
            LOGE("%s: error converting tag row to json: %s",
                    __func__, perr);
            return -1;
        }

        ret = ovsdb_sync_upsert_where(RULE_TABLE, where, j_row, NULL);

        if (!ret)
        {
            LOGE("%s: Failed to upsert the row into the table %s.",
                    __func__, RULE_TABLE);
            return -1;
        }
        LOGI("%s: Updated the IOT Rule entry for [%s]",
                __func__, row->name);
    }
    return 0;
}

int ovsdb_remove_rules(
        struct schema_IOT_Rule_Config rows[],
        size_t num_rules)
{
    json_t *where = NULL;
    bool ret = false;

    for (size_t i = 0; i < num_rules; i++)
    {
        struct schema_IOT_Rule_Config *row = &rows[i];

        where = ovsdb_tran_cond(
                OCLM_STR,
                "name",
                OFUNC_EQ,
                row->name);

        ret = ovsdb_sync_delete_where(RULE_TABLE, where);

        if (!ret)
        {
            LOGE("%s: Failed to remove the row [%s] into the table.",
                    __func__, RULE_TABLE);
            return -1;
        }
        LOGI("%s: removed the IOT rule entry for [%s].",
                __func__, row->name);

    }
    return 0;
}

void iotm_get_awlan_headers(struct schema_AWLAN_Node *awlan)
{
    char *location = "locationId";
    struct iotm_session *session;
    struct str_pair *pair;
    char *node = "nodeId";
    struct iotm_mgr *mgr;
    ds_tree_t *sessions;
    size_t key_size;
    size_t val_size;
    size_t nelems;

    /* Get the manager */
    mgr = iotm_get_mgr();

    /* Free previous headers if any */
    free_str_tree(mgr->mqtt_headers);

    /* Get AWLAN_Node's mqtt_headers element size */
    key_size = sizeof(awlan->mqtt_headers_keys[0]);
    val_size = sizeof(awlan->mqtt_headers[0]);

    /* Get AWLAN_Node's number of elements */
    nelems = awlan->mqtt_headers_len;

    mgr->mqtt_headers = schema2tree(
            key_size,
            val_size,
            nelems,
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
    sessions = iotm_get_sessions();
    session = ds_tree_head(sessions);
    while (session != NULL)
    {
        session->mqtt_headers = mgr->mqtt_headers;
        session->location_id  = mgr->location_id;
        session->node_id  = mgr->node_id;
        session = ds_tree_next(sessions, session);
    }
}

void iotm_rm_awlan_headers(void)
{
    struct iotm_session *session;
    struct iotm_mgr *mgr;
    ds_tree_t *sessions;

    /* Get the manager */
    mgr = iotm_get_mgr();

    /* Free previous headers if any */
    free_str_tree(mgr->mqtt_headers);
    mgr->mqtt_headers = NULL;

    /* Lookup sessions, update their header info */
    sessions = iotm_get_sessions();
    session = ds_tree_head(sessions);
    while (session != NULL)
    {
        session->mqtt_headers = NULL;
        session = ds_tree_next(sessions, session);
    }
}


void iotm_notify_tag_update()
{
    ds_tree_t *sessions = iotm_get_sessions();
    struct iotm_session *session = ds_tree_head(sessions);
    while (session != NULL)
    {
        LOGI("%s : sent tag update to handler [%s].\n", __func__, session->name);
        if ( session->ops.tag_update ) session->ops.tag_update(session);
        session = ds_tree_next(sessions, session);
    }
}

void iotm_notify_rule_update(ovsdb_update_monitor_t *mon, struct iotm_rule *rule)
{
    ds_tree_t *sessions = iotm_get_sessions();
    struct iotm_session *session = ds_tree_head(sessions);
    while (session != NULL)
    {
        LOGI("%s : sent rule update to handler [%s].\n", __func__, session->name);
        if ( session->ops.rule_update ) session->ops.rule_update(session, mon, rule);
        session = ds_tree_next(sessions, session);
    }
    return;
}

/* End helpers for Data Structures */

void callback_IOT_Rule_Config(ovsdb_update_monitor_t *mon,
        struct schema_IOT_Rule_Config *old_rec,
        struct schema_IOT_Rule_Config *conf) {

    struct schema_IOT_Rule_Config *row = ( mon->mon_type == OVSDB_UPDATE_DEL ) ? old_rec : conf;
    struct iotm_rule *rule = NULL; // struct to send to plugin to inform update

    if ( mon->mon_type == OVSDB_UPDATE_NEW )
    {
        LOGD("%s: new IOT config entry: name %s, event: %s",
                __func__, row->name, row->event);
        iotm_add_rule(row);
        rule = iotm_get_rule(row->name, row->event);
        if ( rule != NULL ) iotm_notify_rule_update(mon, rule);
    }
    else if ( mon->mon_type == OVSDB_UPDATE_MODIFY )
    {
        LOGD("%s: updated node config entry: name %s, event: %s",
                __func__, row->name, row->event);
        iotm_update_rule(row);
        rule = iotm_get_rule(row->name, row->event);
        if ( rule != NULL ) iotm_notify_rule_update(mon, rule);
    }
    else if ( mon->mon_type == OVSDB_UPDATE_DEL )
    {
        LOGD("%s: removed node config entry: name %s, event: %s",
                __func__, row->name, row->event);
        rule = iotm_get_rule(row->name, row->event);
        if ( rule != NULL )
        {
            struct iotm_rule pass_rule =
            {
                .name = rule->name,
                .event = rule->event,
            };
            iotm_delete_rule(row);
            iotm_notify_rule_update(mon, &pass_rule);
        }
        else iotm_delete_rule(row);
    }
    else
    {
        LOGI("%s: OVSDB Transaction, no transaction type of interest.\n", __func__);
    }

}

void callback_IOT_Manager_Config(ovsdb_update_monitor_t *mon,
        struct schema_IOT_Manager_Config *old_rec,
        struct schema_IOT_Manager_Config *conf)
{

    struct schema_IOT_Manager_Config *row = ( mon->mon_type == OVSDB_UPDATE_DEL ) ? old_rec : conf;

    if ( mon->mon_type == OVSDB_UPDATE_NEW )
    {
        LOGD("%s: new IOT config entry: handler %s, plugin: %s",
                __func__, row->handler, row->plugin);
        iotm_add_session(row);
    }
    else if ( mon->mon_type == OVSDB_UPDATE_MODIFY )
    {
        LOGD("%s: Updated IOT config entry: handler %s, plugin: %s",
                __func__, row->handler, row->plugin);
        iotm_modify_session(row);
    }
    else if ( mon->mon_type == OVSDB_UPDATE_DEL )
    {
        LOGD("%s: Removed IOT config entry: handler %s, plugin: %s",
                __func__, row->handler, row->plugin);
        iotm_delete_session(row);
    }
    else
    {
        LOGI("No matches that we are interested in.\n");
    }
}

void callback_Openflow_Tag_Group(
        ovsdb_update_monitor_t *mon,
        struct schema_Openflow_Tag_Group *old_rec,
        struct schema_Openflow_Tag_Group *conf) 
{
    // TODO : implement
}

void callback_Openflow_Tag(
        ovsdb_update_monitor_t *mon,
        struct schema_Openflow_Tag *old_rec,
        struct schema_Openflow_Tag *conf) 
{
    struct schema_Openflow_Tag *row = ( mon->mon_type == OVSDB_UPDATE_DEL ) ? old_rec : conf;
    struct iotm_mgr *mgr = NULL;
    struct iotm_tree_t *tags = NULL;

    mgr = iotm_get_mgr();
    tags = mgr->tags;

    switch(mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            LOGI("%s: New entry for tag - [%s]\n",
                    __func__, row->name);
            add_tag_to_tree(tags, row);
            break;
        case OVSDB_UPDATE_MODIFY:
            LOGI("%s: Modified entry for tag - [%s]\n",
                    __func__, row->name);
            remove_tag_from_tree(tags, row);
            add_tag_to_tree(tags, row);
            break;
        case OVSDB_UPDATE_DEL:
            LOGI("%s: Removed entry for tag - [%s]\n",
                    __func__, row->name);
            remove_tag_from_tree(tags, row);
            break;
        default:
            LOGI("%s: OVSDB transaction that isn't being interacted with: [%d]\n", __func__, mon->mon_type);
            break;
    }
    tag_print_tree(tags);
    iotm_notify_tag_update();
}

/**
 * @brief registered callback for AWLAN_Node events
 */
    void
callback_AWLAN_Node(ovsdb_update_monitor_t *mon,
        struct schema_AWLAN_Node *old_rec,
        struct schema_AWLAN_Node *awlan)
{
    if (mon->mon_type == OVSDB_UPDATE_NEW) iotm_get_awlan_headers(awlan);
    if (mon->mon_type == OVSDB_UPDATE_DEL) iotm_rm_awlan_headers();
    if (mon->mon_type == OVSDB_UPDATE_MODIFY) iotm_get_awlan_headers(awlan);
}




int iotm_ovsdb_init(void) {
    // Initialize OVSDB tables
    OVSDB_TABLE_INIT_NO_KEY(IOT_Rule_Config);
    OVSDB_TABLE_INIT_NO_KEY(IOT_Manager_Config);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag);
    OVSDB_TABLE_INIT_NO_KEY(Openflow_Tag_Group);
    OVSDB_TABLE_INIT_NO_KEY(AWLAN_Node);

    // Initialize OVSDB monitor callbacks
    OVSDB_TABLE_MONITOR(IOT_Rule_Config, false);
    OVSDB_TABLE_MONITOR(IOT_Manager_Config, false);
    OVSDB_TABLE_MONITOR(Openflow_Tag, false);
    OVSDB_TABLE_MONITOR(Openflow_Tag_Group, false);
    OVSDB_TABLE_MONITOR(AWLAN_Node, false);

    // Initialize the plugin loader routine
    struct iotm_mgr *mgr;
    mgr = iotm_get_mgr();
    mgr->init_plugin = iotm_init_plugin;
    return 0;
}
