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

/**
 * SM Latency OVSDB adapter
 *
 * Plumbs OVSDB as configuration driver for Core
 * and hooks up Mqtt for reporting.
 *
 * Technically this can be thought of as Opensync
 * plumbing, not just OVSDB plumbing.
 *
 * This maintains Core streams per UUID that has a
 * supported stats_type.
 *
 * It is expected for AWLAN_Node::mqtt_topics to
 * provide the MQTT topic name where Latency reports
 * should be pushed to. This is conveyed to sm_lat_mqtt.
 *
 * Example for testing:
 *
 *  # First make sure MQTT topic is defined. Without this no
 *  # stats will be sent out. They will only be logged in
 *  # system logger.
 *
 *  $ ovsh s AWLAN_Node mqtt_topics
 *
 *  # .. check what other stats topics look like, eg
 *  #    aggregatedStats/dog1/AAA/BBB
 *  #                    ^^^^^^^^^^^^
 *  #                      |__ this is the important bit
 *  #                          that is per pod-location
 *  #
 *
 *  $ ovsh u AWLAN_Node \
 *        'mqtt_topics:ins:["map", [["Latency", "Latency/dog1/AAA/BBB"]]]'
 *
 *  # If you want to collect latency on eth0 and h-24
 *  # you can do this (note: radio_type is a dummy):
 *
 *  $ ovsh i Wifi_Stats_Config \
 *        stats_type:=latency \
 *        radio_type:=2.4G \
 *        if_name:='["set", ["h-24", "eth0"]]' \
 *        reporting_interval:=60 \
 *        sampling_interval:=5 \
 *        sample_policy:=separate \
 *        latency_dscp:=report_per_dscp \
 *        latency_kinds:='["set", ["min", "max", "avg", "last", "num"]]' \
 *
 */

#include <ds_tree.h>
#include <memutil.h>
#include <schema_consts.h>
#include <osp_unit.h>

#define SM_LAT_OVSDB_AWLAN_MQTT_TOPIC_KEY "Latency"

#define LOG_PREFIX(fmt, ...) "sm: lat: ovsdb: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_OVSDB(o, fmt, ...) LOG_PREFIX("%p: " fmt, (o), ##__VA_ARGS__)

#define LOG_PREFIX_ENTRY(e, fmt, ...) LOG_PREFIX_OVSDB((e)->o, "%s: " fmt, (e)->uuid, ##__VA_ARGS__)

#include "sm_lat_ovsdb.h"
#include "sm_lat_core.h"
#include "sm_lat_mqtt.h"

struct sm_lat_ovsdb
{
    sm_lat_core_t *core;
    sm_lat_mqtt_t *mqtt;
    ovsdb_table_t table_Wifi_Stats_Config;
    ds_tree_t entries; /* sm_lat_ovsdb_entry (node) */
};

struct sm_lat_ovsdb_entry
{
    ds_tree_node_t node; /* sm_lat_ovsdb (entries) */
    sm_lat_ovsdb_t *o;
    char *uuid;
    sm_lat_core_stream_t *st;
};

typedef struct sm_lat_ovsdb sm_lat_ovsdb_t;
typedef struct sm_lat_ovsdb_entry sm_lat_ovsdb_entry_t;

static bool sm_lat_ovsdb_get(sm_lat_ovsdb_t *o, const char *uuid, struct schema_Wifi_Stats_Config *row)
{
    const char *column = "_uuid";
    json_t *where = where = ovsdb_tran_cond(OCLM_UUID, column, OFUNC_EQ, uuid);
    const bool ok = ovsdb_table_select_one_where(&o->table_Wifi_Stats_Config, where, row);
    return ok;
}

static bool sm_lat_ovsdb_row_type_is_supported(const struct schema_Wifi_Stats_Config *row)
{
    if (row->stats_type_exists == false) return false;
    if (strcmp(row->stats_type, SCHEMA_CONSTS_REPORT_TYPE_LATENCY) == 0) return true;
    return false;
}

static void sm_lat_ovsdb_entry_report_cb(void *priv, const sm_lat_core_host_t *const *hosts, size_t count)
{
    sm_lat_ovsdb_entry_t *e = priv;
    sm_lat_mqtt_report(e->o->mqtt, hosts, count);
}

static sm_lat_ovsdb_entry_t *sm_lat_ovsdb_entry_alloc(sm_lat_ovsdb_t *o, const char *uuid)
{
    sm_lat_ovsdb_entry_t *e = CALLOC(1, sizeof(*e));
    e->uuid = STRDUP(uuid);
    e->o = o;
    e->st = sm_lat_core_stream_alloc(o->core);
    sm_lat_core_stream_set_report_fn(e->st, sm_lat_ovsdb_entry_report_cb, e);
    ds_tree_insert(&o->entries, e, e->uuid);
    return e;
}

static void sm_lat_ovsdb_entry_drop(sm_lat_ovsdb_entry_t *e)
{
    ds_tree_remove(&e->o->entries, e);
    sm_lat_core_stream_drop(e->st);
    FREE(e->uuid);
    FREE(e);
}

void sm_lat_ovsdb_entry_apply(
        sm_lat_ovsdb_entry_t *e,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Stats_Config *old_row,
        const struct schema_Wifi_Stats_Config *new_row)
{
    if (WARN_ON(new_row == NULL)) return;
    sm_lat_core_stream_t *st = e->st;
    bool updated = false;
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Stats_Config, reporting_interval)))
    {
        const uint32_t ms = new_row->reporting_interval * 1000;
        sm_lat_core_stream_set_report_ms(st, ms);
        updated = true;
    }
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Stats_Config, sampling_interval)))
    {
        const uint32_t ms = new_row->sampling_interval * 1000;
        sm_lat_core_stream_set_poll_ms(st, ms);
        updated = true;
    }
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Stats_Config, latency_dscp)))
    {
        if (!strcmp(new_row->latency_dscp, SCHEMA_CONSTS_LATENCY_DSCP_TYPE_DO_NOT_REPORT))
        {
            sm_lat_core_stream_set_dscp(st, false);
        }
        else if (!strcmp(new_row->latency_dscp, SCHEMA_CONSTS_LATENCY_DSCP_TYPE_REPORT_PER_DSCP))
        {
            sm_lat_core_stream_set_dscp(st, true);
        }
        else
        {
            LOGW(LOG_PREFIX_ENTRY(e, "latency_dscp is not recognized: %s", new_row->latency_dscp));
        }
        updated = true;
    }
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Stats_Config, sample_policy)))
    {
        if (!strcmp(new_row->sample_policy, SCHEMA_CONSTS_SAMPLE_POLICY_SEPARATE))
        {
            sm_lat_core_stream_set_sampling(st, SM_LAT_CORE_SAMPLING_SEPARATE);
        }
        else if (!strcmp(new_row->sample_policy, SCHEMA_CONSTS_SAMPLE_POLICY_MERGE))
        {
            sm_lat_core_stream_set_sampling(st, SM_LAT_CORE_SAMPLING_MERGE);
        }
        else
        {
            LOGW(LOG_PREFIX_ENTRY(e, "sample_policy is not recognized: %s", new_row->sample_policy));
        }
        updated = true;
    }
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Stats_Config, latency_kinds)))
    {
        int i;
        if (old_row != NULL)
        {
            for (i = 0; i < old_row->latency_kinds_len; i++)
            {
                const char *s = old_row->latency_kinds[i];
                if (!strcmp(s, SCHEMA_CONSTS_LATENCY_KIND_MIN)) sm_lat_core_stream_set_kind_min(st, false);
                if (!strcmp(s, SCHEMA_CONSTS_LATENCY_KIND_MAX)) sm_lat_core_stream_set_kind_max(st, false);
                if (!strcmp(s, SCHEMA_CONSTS_LATENCY_KIND_AVG)) sm_lat_core_stream_set_kind_avg(st, false);
                if (!strcmp(s, SCHEMA_CONSTS_LATENCY_KIND_NUM)) sm_lat_core_stream_set_kind_num_pkts(st, false);
                if (!strcmp(s, SCHEMA_CONSTS_LATENCY_KIND_LAST)) sm_lat_core_stream_set_kind_last(st, false);
            }
        }
        for (i = 0; i < new_row->latency_kinds_len; i++)
        {
            const char *s = new_row->latency_kinds[i];
            if (!strcmp(s, SCHEMA_CONSTS_LATENCY_KIND_MIN)) sm_lat_core_stream_set_kind_min(st, true);
            if (!strcmp(s, SCHEMA_CONSTS_LATENCY_KIND_MAX)) sm_lat_core_stream_set_kind_max(st, true);
            if (!strcmp(s, SCHEMA_CONSTS_LATENCY_KIND_AVG)) sm_lat_core_stream_set_kind_avg(st, true);
            if (!strcmp(s, SCHEMA_CONSTS_LATENCY_KIND_NUM)) sm_lat_core_stream_set_kind_num_pkts(st, true);
            if (!strcmp(s, SCHEMA_CONSTS_LATENCY_KIND_LAST)) sm_lat_core_stream_set_kind_last(st, true);
        }
        updated = true;
    }
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Stats_Config, if_name)))
    {
        int i;
        if (old_row != NULL)
        {
            for (i = 0; i < old_row->if_name_len; i++)
            {
                sm_lat_core_stream_set_ifname(e->st, old_row->if_name[i], false);
            }
        }
        for (i = 0; i < new_row->if_name_len; i++)
        {
            sm_lat_core_stream_set_ifname(e->st, new_row->if_name[i], true);
        }
        updated = true;
    }
    if (updated == false)
    {
        LOGW(LOG_PREFIX_ENTRY(e, "no updates - someone is tweaking other columns that have no effect?"));
    }
}

static void sm_lat_ovsdb_update_stats_new(
        sm_lat_ovsdb_t *o,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Stats_Config *row)
{
    if (sm_lat_ovsdb_row_type_is_supported(row) == false) return;
    sm_lat_ovsdb_entry_t *e = ds_tree_find(&o->entries, row->_uuid.uuid);
    const bool already_exists = (e != NULL);
    if (WARN_ON(already_exists)) return;
    e = sm_lat_ovsdb_entry_alloc(o, mon->mon_uuid);
    sm_lat_ovsdb_entry_apply(e, mon, NULL, row);
}

static void sm_lat_ovsdb_update_stats_del(
        sm_lat_ovsdb_t *o,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Stats_Config *row)
{
    if (sm_lat_ovsdb_row_type_is_supported(row) == false) return;
    sm_lat_ovsdb_entry_t *e = ds_tree_find(&o->entries, row->_uuid.uuid);
    const bool doesnt_exist = (e == NULL);
    if (WARN_ON(doesnt_exist)) return;
    sm_lat_ovsdb_entry_drop(e);
}

static void sm_lat_ovsdb_update_stats_mod(
        sm_lat_ovsdb_t *o,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Stats_Config *old_row,
        const struct schema_Wifi_Stats_Config *new_row)
{
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Stats_Config, stats_type)))
    {
        if (sm_lat_ovsdb_row_type_is_supported(new_row))
        {
            LOGI(LOG_PREFIX_OVSDB(o, "%s: became supported", mon->mon_uuid));
            struct schema_Wifi_Stats_Config row;
            const bool ok = sm_lat_ovsdb_get(o, mon->mon_uuid, &row);
            if (WARN_ON(!ok)) return;
            sm_lat_ovsdb_update_stats_new(o, mon, &row);
        }
        else
        {
            LOGI(LOG_PREFIX_OVSDB(o, "%s: became unsupported", mon->mon_uuid));
            sm_lat_ovsdb_update_stats_del(o, mon, old_row);
        }
    }
    else
    {
        if (sm_lat_ovsdb_row_type_is_supported(new_row))
        {
            sm_lat_ovsdb_entry_t *e = ds_tree_find(&o->entries, mon->mon_uuid);
            const bool doesnt_exist = (e == NULL);
            if (WARN_ON(doesnt_exist)) return;
            sm_lat_ovsdb_entry_apply(e, mon, old_row, new_row);
        }
        else
        {
            LOGD(LOG_PREFIX_OVSDB(
                    o,
                    "%s: ignoring - other stats_type may be handled by other entities",
                    mon->mon_uuid));
        }
    }
}

bool sm_lat_ovsdb_update_stats(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon)
{
    if (o == NULL) return false;

    struct schema_Wifi_Stats_Config old_row;
    struct schema_Wifi_Stats_Config new_row;
    pjs_errmsg_t perr;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            if (WARN_ON(!schema_Wifi_Stats_Config_from_json(&new_row, mon->mon_json_new, false, perr))) break;
            sm_lat_ovsdb_update_stats_new(o, mon, &new_row);
            return sm_lat_ovsdb_row_type_is_supported(&new_row);
        case OVSDB_UPDATE_MODIFY:
            if (WARN_ON(!schema_Wifi_Stats_Config_from_json(&old_row, mon->mon_json_old, true, perr))) break;
            if (WARN_ON(!schema_Wifi_Stats_Config_from_json(&new_row, mon->mon_json_new, true, perr))) break;
            sm_lat_ovsdb_update_stats_mod(o, mon, &old_row, &new_row);
            return sm_lat_ovsdb_row_type_is_supported(&old_row) || sm_lat_ovsdb_row_type_is_supported(&new_row);
        case OVSDB_UPDATE_DEL:
            if (WARN_ON(!schema_Wifi_Stats_Config_from_json(&old_row, mon->mon_json_old, false, perr))) break;
            sm_lat_ovsdb_update_stats_del(o, mon, &old_row);
            return sm_lat_ovsdb_row_type_is_supported(&old_row);
        default:
            break;
    }
    return false;
}

static const char *sm_lat_ovsdb_awlan_get_mqtt_topic(const struct schema_AWLAN_Node *row)
{
    return SCHEMA_KEY_VAL_NULL(row->mqtt_topics, SM_LAT_OVSDB_AWLAN_MQTT_TOPIC_KEY);
}

static void sm_lat_ovsdb_update_awlan_new(
        sm_lat_ovsdb_t *o,
        ovsdb_update_monitor_t *mon,
        const struct schema_AWLAN_Node *row)
{
    const char *topic = sm_lat_ovsdb_awlan_get_mqtt_topic(row);
    sm_lat_mqtt_set_topic(o->mqtt, topic);
}

static void sm_lat_ovsdb_update_awlan_del(
        sm_lat_ovsdb_t *o,
        ovsdb_update_monitor_t *mon,
        const struct schema_AWLAN_Node *row)
{
    sm_lat_mqtt_set_topic(o->mqtt, NULL);
}

static void sm_lat_ovsdb_update_awlan_mod(
        sm_lat_ovsdb_t *o,
        ovsdb_update_monitor_t *mon,
        const struct schema_AWLAN_Node *old_row,
        const struct schema_AWLAN_Node *new_row)
{
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, mqtt_topics)))
    {
        const char *topic = sm_lat_ovsdb_awlan_get_mqtt_topic(new_row);
        sm_lat_mqtt_set_topic(o->mqtt, topic);
    }
}

void sm_lat_ovsdb_update_awlan(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon)
{
    if (o == NULL) return;

    struct schema_AWLAN_Node old_row;
    struct schema_AWLAN_Node new_row;
    pjs_errmsg_t perr;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            if (WARN_ON(!schema_AWLAN_Node_from_json(&new_row, mon->mon_json_new, false, perr))) break;
            sm_lat_ovsdb_update_awlan_new(o, mon, &new_row);
            break;
        case OVSDB_UPDATE_MODIFY:
            if (WARN_ON(!schema_AWLAN_Node_from_json(&old_row, mon->mon_json_old, true, perr))) break;
            if (WARN_ON(!schema_AWLAN_Node_from_json(&new_row, mon->mon_json_new, true, perr))) break;
            sm_lat_ovsdb_update_awlan_mod(o, mon, &old_row, &new_row);
            break;
        case OVSDB_UPDATE_DEL:
            if (WARN_ON(!schema_AWLAN_Node_from_json(&old_row, mon->mon_json_old, false, perr))) break;
            sm_lat_ovsdb_update_awlan_del(o, mon, &old_row);
            break;
        default:
            break;
    }
}

void sm_lat_ovsdb_update_wifi_inet_config(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon)
{
    if (o == NULL) return;

    struct schema_Wifi_Inet_Config old_row;
    struct schema_Wifi_Inet_Config new_row;
    pjs_errmsg_t perr;
    const char *if_name = NULL;
    const char *if_role = NULL;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            if (WARN_ON(!schema_Wifi_Inet_Config_from_json(&new_row, mon->mon_json_new, false, perr))) break;
            if_name = new_row.if_name;
            if_role = new_row.role_exists ? new_row.role : NULL;
            break;
        case OVSDB_UPDATE_MODIFY:
            if (WARN_ON(!schema_Wifi_Inet_Config_from_json(&old_row, mon->mon_json_old, true, perr))) break;
            if (WARN_ON(!schema_Wifi_Inet_Config_from_json(&new_row, mon->mon_json_new, true, perr))) break;
            if_name = new_row.if_name;
            if_role = new_row.role_exists ? new_row.role : NULL;
            break;
        case OVSDB_UPDATE_DEL:
            if (WARN_ON(!schema_Wifi_Inet_Config_from_json(&old_row, mon->mon_json_old, false, perr))) break;
            if_name = old_row.if_name;
            if_role = NULL;
            break;
        default:
            break;
    }

    sm_lat_mqtt_set_if_role(o->mqtt, if_name, if_role);
}

void sm_lat_ovsdb_update_wifi_vif_state(sm_lat_ovsdb_t *o, ovsdb_update_monitor_t *mon)
{
    if (o == NULL) return;

    struct schema_Wifi_VIF_State old_row;
    struct schema_Wifi_VIF_State new_row;
    pjs_errmsg_t perr;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            if (WARN_ON(!schema_Wifi_VIF_State_from_json(&new_row, mon->mon_json_new, false, perr))) break;
            const char *mld_if_name =
                    (new_row.mld_if_name_exists && strlen(new_row.mld_if_name) > 0) ? new_row.mld_if_name : NULL;
            sm_lat_core_set_vif_mld_if_name(o->core, new_row.if_name, mld_if_name);
            break;
        case OVSDB_UPDATE_MODIFY:
            if (WARN_ON(!schema_Wifi_VIF_State_from_json(&old_row, mon->mon_json_old, true, perr))) break;
            if (WARN_ON(!schema_Wifi_VIF_State_from_json(&new_row, mon->mon_json_new, true, perr))) break;
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, mld_if_name)))
            {
                const char *mld_if_name =
                        (new_row.mld_if_name_exists && strlen(new_row.mld_if_name) > 0) ? new_row.mld_if_name : NULL;
                sm_lat_core_set_vif_mld_if_name(o->core, new_row.if_name, mld_if_name);
            }
            break;
        case OVSDB_UPDATE_DEL:
            if (WARN_ON(!schema_Wifi_VIF_State_from_json(&old_row, mon->mon_json_old, false, perr))) break;
            sm_lat_core_set_vif_mld_if_name(o->core, old_row.if_name, NULL);
            break;
        default:
            break;
    }
    return;
}

sm_lat_ovsdb_t *sm_lat_ovsdb_alloc(void)
{
    char node_id[128];
    MEMZERO(node_id);
    const bool node_id_ok = osp_unit_id_get(node_id, sizeof(node_id) - 1);
    if (WARN_ON(node_id_ok == false)) return NULL;

    sm_lat_ovsdb_t *o = CALLOC(1, sizeof(*o));
    ds_tree_init(&o->entries, ds_str_cmp, sm_lat_ovsdb_entry_t, node);
    OVSDB_TABLE_VAR_INIT(&o->table_Wifi_Stats_Config, Wifi_Stats_Config, _uuid);
    o->core = sm_lat_core_alloc();
    o->mqtt = sm_lat_mqtt_alloc();
    sm_lat_mqtt_set_node_id(o->mqtt, node_id);
    LOGI(LOG_PREFIX_OVSDB(o, "allocated"));
    return o;
}

static void sm_lat_ovsdb_drop_entries(sm_lat_ovsdb_t *o)
{
    sm_lat_ovsdb_entry_t *e;
    while ((e = ds_tree_head(&o->entries)) != NULL)
    {
        sm_lat_ovsdb_entry_drop(e);
    }
}

void sm_lat_ovsdb_drop(sm_lat_ovsdb_t *o)
{
    if (o == NULL) return;
    sm_lat_ovsdb_drop_entries(o);
    sm_lat_core_drop(o->core);
    sm_lat_mqtt_drop(o->mqtt);
    FREE(o);
}
