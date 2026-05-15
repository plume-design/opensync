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

#include <const.h>
#include <memutil.h>
#include <ds_tree.h>
#include <ovsdb_cache.h>

#include <osw_types.h>
#include <osw_module.h>
#include <ow_steer_hs.h>
#include "ow_ovsdb_hs.h"
#include "ow_hs_mqtt.h"
#include "schema.h"

#define LOG_PREFIX(fmt, ...)          "ow: ovsdb: hs: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_VIF(vif, fmt, ...) LOG_PREFIX("vif: %s: " fmt, (vif)->uuid ?: "uuid_missing", ##__VA_ARGS__)

#define OW_OVSDB_HS_AWLAN_MQTT_TOPIC_KEY "HS.Steering"
#define OW_OVSDB_HS_NODE_CONFIG_MODULE   "OW"
#define OW_OVSDB_HS_NODE_CONFIG_KEY      "hs_steer_report_interval"

struct ow_ovsdb_hs
{
    ow_steer_hs_t *m_hs;
    ds_tree_t vifs;
    ovsdb_table_t table_Hotspot_Steering;
    ovsdb_table_t table_AWLAN_Node;
    ovsdb_table_t table_Node_Config;
    bool is_topic;
    uint32_t interval_sec;
};

struct ow_ovsdb_hs_vif
{
    ds_tree_node_t node;
    ow_ovsdb_hs_t *m;
    ow_steer_hs_vif_t *vif;
    char *if_name;
    char *uuid;
};

typedef struct ow_ovsdb_hs_vif ow_ovsdb_hs_vif_t;

static void ow_ovsdb_hs_vif_set_if_name(ow_ovsdb_hs_vif_t *vif, const char *if_name)
{
    const bool changed = (STRSCMP(vif->if_name, if_name) != 0);
    if (changed)
    {
        LOGI(LOG_PREFIX_VIF(vif, "if_name: '%s' -> '%s'", vif->if_name ?: "(none)", if_name ?: "(none)"));

        FREE(vif->if_name);
        vif->if_name = if_name ? STRDUP(if_name) : NULL;

        ow_steer_hs_vif_drop(vif->vif);
        vif->vif = vif->if_name ? ow_steer_hs_vif_alloc(vif->m->m_hs, vif->if_name) : NULL;
    }
}

static void ow_ovsdb_hs_stats_steering_recalc(ow_ovsdb_hs_t *m)
{
    const bool vifs_exists = !ds_tree_is_empty(&m->vifs);
    if (vifs_exists && m->is_topic && m->interval_sec > 0)
    {
        ow_steer_hs_set_mqtt_interval(m->m_hs, m->interval_sec);
        return;
    }
    ow_steer_hs_set_mqtt_interval(m->m_hs, 0);
}

static ow_ovsdb_hs_vif_t *ow_ovsdb_hs_vif_alloc(ow_ovsdb_hs_t *m, const char *uuid)
{
    ow_ovsdb_hs_vif_t *vif = CALLOC(1, sizeof(*vif));
    vif->uuid = STRDUP(uuid);
    vif->m = m;
    ds_tree_insert(&m->vifs, vif, vif->uuid);
    LOGI(LOG_PREFIX_VIF(vif, "allocated"));
    ow_ovsdb_hs_stats_steering_recalc(m);
    return vif;
}

static void ow_ovsdb_hs_vif_drop(ow_ovsdb_hs_vif_t *vif)
{
    if (vif == NULL) return;
    if (WARN_ON(vif->m == NULL)) return;
    LOGI(LOG_PREFIX_VIF(vif, "dropping"));
    ds_tree_remove(&vif->m->vifs, vif);
    ow_steer_hs_vif_drop(vif->vif);
    FREE(vif->if_name);
    FREE(vif);
}

static uint8_t ow_ovsdb_hs_dbm_to_db(const int dbm)
{
    const int nf = osw_channel_nf_20mhz_fixup(0); /* -96 */
    const int db = dbm - nf;                      /* eg. -80 - (-96) = 16 */
    WARN_ON(db < 0);
    return db >= 0 ? db : 0;
}

static void ow_ovsdb_hs_vif_set(ow_ovsdb_hs_t *m, const char *uuid, const struct schema_Hotspot_Steering *rec)
{
    if (m == NULL) return;
    if (uuid == NULL) return;
    if (WARN_ON(strlen(uuid) == 0)) return;
    ow_ovsdb_hs_vif_t *vif = ds_tree_find(&m->vifs, uuid) ?: ow_ovsdb_hs_vif_alloc(m, uuid);
    ow_ovsdb_hs_vif_set_if_name(vif, rec->if_name_exists ? rec->if_name : NULL);

    const uint8_t soft = rec->soft_snr_dbm_exists ? ow_ovsdb_hs_dbm_to_db(rec->soft_snr_dbm) : 0;
    const uint8_t hard = rec->hard_snr_dbm_exists ? ow_ovsdb_hs_dbm_to_db(rec->hard_snr_dbm) : 0;
    ow_steer_hs_vif_set_soft_snr_db(vif->vif, soft);
    ow_steer_hs_vif_set_hard_snr_db(vif->vif, hard);
}

static void ow_ovsdb_hs_vif_del(ow_ovsdb_hs_t *m, const char *uuid)
{
    ow_ovsdb_hs_vif_t *vif = ds_tree_find(&m->vifs, uuid);
    ow_ovsdb_hs_vif_drop(vif);
    ow_ovsdb_hs_stats_steering_recalc(m);
}

static void ow_ovsdb_hs_table_cb(
        ovsdb_update_monitor_t *mon,
        const struct schema_Hotspot_Steering *old,
        const struct schema_Hotspot_Steering *rec,
        ovsdb_cache_row_t *row)
{
    ovsdb_table_t *table = mon->mon_data;
    ow_ovsdb_hs_t *m = container_of(table, typeof(*m), table_Hotspot_Steering);
    const char *uuid = mon->mon_uuid;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_hs_vif_set(m, uuid, rec);
            break;
        case OVSDB_UPDATE_DEL:
            ow_ovsdb_hs_vif_del(m, uuid);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static const char *ow_ovsdb_hs_awlan_get_mqtt_topic(const struct schema_AWLAN_Node *row)
{
    return SCHEMA_KEY_VAL_NULL(row->mqtt_topics, OW_OVSDB_HS_AWLAN_MQTT_TOPIC_KEY);
}

static void ow_ovsdb_hs_awlan_update_topic(ow_ovsdb_hs_t *m, const char *topic)
{
    if (m == NULL) return;
    ow_steer_hs_set_mqtt_topic(m->m_hs, topic);
    m->is_topic = topic != NULL;
    ow_ovsdb_hs_stats_steering_recalc(m);
}

static void ow_ovsdb_hs_update_awlan_new(
        ow_ovsdb_hs_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_AWLAN_Node *row)
{
    if (m == NULL) return;
    const char *topic = ow_ovsdb_hs_awlan_get_mqtt_topic(row);
    ow_ovsdb_hs_awlan_update_topic(m, topic);
}

static void ow_ovsdb_hs_update_awlan_del(
        ow_ovsdb_hs_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_AWLAN_Node *row)
{
    if (m == NULL) return;
    ow_ovsdb_hs_awlan_update_topic(m, NULL);
}

static void ow_ovsdb_hs_update_awlan_mod(
        ow_ovsdb_hs_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_AWLAN_Node *old_row,
        const struct schema_AWLAN_Node *new_row)
{
    if (m == NULL) return;
    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(AWLAN_Node, mqtt_topics)))
    {
        const char *topic = ow_ovsdb_hs_awlan_get_mqtt_topic(new_row);
        ow_ovsdb_hs_awlan_update_topic(m, topic);
    }
}

static void ow_ovsdb_hs_awlan_node_cb(
        ovsdb_update_monitor_t *mon,
        const struct schema_AWLAN_Node *old,
        const struct schema_AWLAN_Node *anode)
{
    ovsdb_table_t *table = mon->mon_data;
    ow_ovsdb_hs_t *m = container_of(table, typeof(*m), table_AWLAN_Node);

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            ow_ovsdb_hs_update_awlan_new(m, mon, anode);
            break;
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_hs_update_awlan_mod(m, mon, old, anode);
            break;
        case OVSDB_UPDATE_DEL:
            ow_ovsdb_hs_update_awlan_del(m, mon, old);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static const char *ow_ovsdb_hs_node_config_get_module(
        ovsdb_update_monitor_t *mon,
        const struct schema_Node_Config *old_rec,
        const struct schema_Node_Config *new_rec)
{
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_ERROR:
            return NULL;
        case OVSDB_UPDATE_DEL:
            return old_rec->module_exists ? old_rec->module : NULL;
        case OVSDB_UPDATE_NEW:
            return new_rec->module_exists ? new_rec->module : NULL;
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Node_Config, value)))
            {
                return new_rec->module_exists ? new_rec->module : NULL;
            }
            else
            {
                return old_rec->module_exists ? old_rec->module : NULL;
            }
            break;
    }
    return NULL;
}

static const char *ow_ovsdb_hs_node_config_get_key(
        ovsdb_update_monitor_t *mon,
        const struct schema_Node_Config *old_rec,
        const struct schema_Node_Config *new_rec)
{
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_ERROR:
            return NULL;
        case OVSDB_UPDATE_DEL:
            return old_rec->key_exists ? old_rec->key : NULL;
        case OVSDB_UPDATE_NEW:
            return new_rec->key_exists ? new_rec->key : NULL;
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Node_Config, value)))
            {
                return new_rec->key_exists ? new_rec->key : NULL;
            }
            else
            {
                return old_rec->key_exists ? old_rec->key : NULL;
            }
            break;
    }
    return NULL;
}
static const char *ow_ovsdb_hs_node_config_get_value(
        ovsdb_update_monitor_t *mon,
        const struct schema_Node_Config *old_rec,
        const struct schema_Node_Config *new_rec)
{
    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_ERROR:
            return NULL;
        case OVSDB_UPDATE_DEL:
            return NULL;
        case OVSDB_UPDATE_NEW:
            return new_rec->value_exists ? new_rec->value : NULL;
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Node_Config, value)))
            {
                return new_rec->value_exists ? new_rec->value : NULL;
            }
            else
            {
                return old_rec->value_exists ? old_rec->value : NULL;
            }
            break;
    }
    return NULL;
}

static void ow_ovsdb_hs_node_config_update_interval(ow_ovsdb_hs_t *m, const uint32_t interval_sec)
{
    if (m == NULL) return;
    m->interval_sec = interval_sec;
    LOGI(LOG_PREFIX("node_config: hs steering stats interval changed: %us", interval_sec));
    ow_ovsdb_hs_stats_steering_recalc(m);
}

static void ow_ovsdb_hs_node_config_set(ow_ovsdb_hs_t *m, const char *interval_str)
{
    if (m == NULL) return;
    const uint32_t interval_sec = interval_str != NULL ? atoi(interval_str) : 0;
    ow_ovsdb_hs_node_config_update_interval(m, interval_sec);
}

static void ow_ovsdb_hs_node_config_cb(
        ovsdb_update_monitor_t *mon,
        const struct schema_Node_Config *old,
        const struct schema_Node_Config *nconf)
{
    ovsdb_table_t *table = mon->mon_data;
    ow_ovsdb_hs_t *m = container_of(table, typeof(*m), table_Node_Config);

    const char *module = ow_ovsdb_hs_node_config_get_module(mon, old, nconf);
    if (module == NULL) return;

    const char *key = ow_ovsdb_hs_node_config_get_key(mon, old, nconf);
    if (key == NULL) return;

    const char *value = ow_ovsdb_hs_node_config_get_value(mon, old, nconf);

    if (strcmp(module, OW_OVSDB_HS_NODE_CONFIG_MODULE) != 0) return;
    if (strcmp(key, OW_OVSDB_HS_NODE_CONFIG_KEY) != 0) return;

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Node_Config, value))) ow_ovsdb_hs_node_config_set(m, value);
}

static void ow_ovsdb_hs_init(ow_ovsdb_hs_t *m)
{
    OVSDB_TABLE_VAR_INIT(&m->table_Hotspot_Steering, Hotspot_Steering, _uuid);
    ds_tree_init(&m->vifs, ds_str_cmp, ow_ovsdb_hs_vif_t, node);
    OVSDB_TABLE_VAR_INIT(&m->table_AWLAN_Node, AWLAN_Node, _uuid);
    OVSDB_TABLE_VAR_INIT(&m->table_Node_Config, Node_Config, _uuid);
}

static void ow_ovsdb_hs_attach(ow_ovsdb_hs_t *m)
{
    m->m_hs = OSW_MODULE_LOAD(ow_steer_hs);
}

void ow_ovsdb_hs_start(ow_ovsdb_hs_t *m)
{
    ovsdb_cache_monitor(&m->table_Hotspot_Steering, (void *)ow_ovsdb_hs_table_cb, true);
    ovsdb_table_monitor(&m->table_AWLAN_Node, (void *)ow_ovsdb_hs_awlan_node_cb, true);
    ovsdb_table_monitor(&m->table_Node_Config, (void *)ow_ovsdb_hs_node_config_cb, true);
}

OSW_MODULE(ow_ovsdb_hs)
{
    static struct ow_ovsdb_hs m;
    ow_ovsdb_hs_init(&m);
    ow_ovsdb_hs_attach(&m);
    return &m;
}
