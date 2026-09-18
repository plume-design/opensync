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

#include <stdbool.h>

#include <ev.h>

#include <ds_tree.h>
#include <memutil.h>
#include <log.h>
#include <ovsdb_table.h>
#include <ovsdb_sync.h>
#include <schema_consts.h>

#include "cm2_bh_macros.h"
#include "cm2_bh_cmu.h"

#define CM2_BH_CMU_BACKOFF_SEC  3.0
#define CM2_BH_CMU_DEADLINE_SEC 3.0

/* Node_Config/Node_State knobs for requesting
 * CM BH CMU disable and reporting status. Unset means enabled. */
#define CM2_BH_CMU_NODE_MODULE       "CM"
#define CM2_BH_CMU_NODE_CFG_DISABLE  "bh_cmu_disable"
#define CM2_BH_CMU_NODE_STATE_STATUS "bh_cmu_disable_status"

#define LOG_PREFIX(m, fmt, ...) "cm2: bh: cmu: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_VIF(vif, fmt, ...)             \
    LOG_PREFIX(                                   \
            (vif)->m,                             \
            "%s [%s%s%s%s%s%s%s%s]: " fmt,        \
            (vif)->vif_name,                      \
            (vif)->work ? "W" : " ",              \
            (vif)->report.wvs_sta ? "S" : " ",    \
            (vif)->report.wvs_4addr ? "4" : " ",  \
            (vif)->report.wvs_mld ? "M" : " ",    \
            (vif)->report.wms_active ? "A" : " ", \
            (vif)->report.cmu_exists ? "E" : " ", \
            (vif)->report.cmu_has_l2 ? "2" : " ", \
            (vif)->report.cmu_has_l3 ? "3" : " ", \
            ##__VA_ARGS__)

#define LOG_PREFIX_GRE(gre, fmt, ...)                    \
    LOG_PREFIX(                                          \
            (gre)->m,                                    \
            "%s [%s%s%s%s%s%s%s]: " fmt,                 \
            (gre)->gre_name,                             \
            (gre)->work ? "W" : " ",                     \
            (gre)->report.wic_exists ? "E" : " ",        \
            (gre)->report.wms_active ? "A" : " ",        \
            (gre)->report.parent_wms_active ? "P" : " ", \
            (gre)->report.cmu_exists ? "E" : " ",        \
            (gre)->report.cmu_has_l2 ? "2" : " ",        \
            (gre)->report.cmu_has_l3 ? "3" : " ",        \
            ##__VA_ARGS__)

extern ovsdb_table_t table_Wifi_VIF_State;
extern ovsdb_table_t table_Wifi_Master_State;
extern ovsdb_table_t table_Connection_Manager_Uplink;
extern ovsdb_table_t table_Wifi_Inet_Config;

/* The `need_delete` is a workaround against current CM link
 * selection logic implementation. It's non-trivial to
 * untangle that. For the time being make this has to do.
 */

struct cm2_bh_cmu_gre
{
    cm2_bh_cmu_t *m;
    ds_tree_node_t node;
    ds_tree_node_t node_by_parent;
    ev_idle recalc;
    ev_timer deadline;
    ev_timer backoff;
    char *gre_name;
    char *parent_name;
    bool work;
    bool need_delete;
    bool enabled;
    bool owned;

    struct
    {
        bool wic_exists;
        bool parent_wms_active;
        bool wms_active;
        bool cmu_exists;
        bool cmu_has_l2;
        bool cmu_has_l3;
    } report;
};

struct cm2_bh_cmu_vif
{
    cm2_bh_cmu_t *m;
    ds_tree_node_t node;
    ev_idle recalc;
    ev_timer deadline;
    ev_timer backoff;
    char *vif_name;
    bool work;
    bool need_delete;
    bool enabled;
    /* The CMU row belongs to this module. Rows appearing while
     * not enabled belong to an external entity and are left alone. */
    bool owned;

    struct
    {
        bool wvs_sta;
        bool wvs_4addr;
        bool wvs_mld;
        bool wms_active;
        bool cmu_exists;
        bool cmu_has_l2;
        bool cmu_has_l3;
    } report;
};

struct cm2_bh_cmu
{
    struct ev_loop *loop;
    ds_tree_t vifs;
    ds_tree_t gres;
    ds_tree_t gres_by_parent;
    ev_idle recalc;
    bool enabled;
    bool reported_enabled;
    bool idle;
};

static bool cm2_bh_cmu_node_config_enabled_get(void)
{
    json_t *where = ovsdb_where_multi(
            ovsdb_where_simple(SCHEMA_COLUMN(Node_Config, module), CM2_BH_CMU_NODE_MODULE),
            ovsdb_where_simple(SCHEMA_COLUMN(Node_Config, key), CM2_BH_CMU_NODE_CFG_DISABLE),
            NULL);
    json_t *rows = ovsdb_sync_select_where(SCHEMA_TABLE(Node_Config), where);
    const char *value = json_string_value(json_object_get(json_array_get(rows, 0), SCHEMA_COLUMN(Node_Config, value)));
    const bool disable = (value != NULL) && (strcmp(value, "true") == 0);
    json_decref(rows);
    return (disable == false);
}

static void cm2_bh_cmu_report_enabled(cm2_bh_cmu_t *m)
{
    const bool disabled = (m->enabled == false);
    json_t *where = ovsdb_where_multi(
            ovsdb_where_simple(SCHEMA_COLUMN(Node_State, module), CM2_BH_CMU_NODE_MODULE),
            ovsdb_where_simple(SCHEMA_COLUMN(Node_State, key), CM2_BH_CMU_NODE_STATE_STATUS),
            NULL);
    json_t *row = json_pack(
            "{s:s, s:s, s:s}",
            SCHEMA_COLUMN(Node_State, module),
            CM2_BH_CMU_NODE_MODULE,
            SCHEMA_COLUMN(Node_State, key),
            CM2_BH_CMU_NODE_STATE_STATUS,
            SCHEMA_COLUMN(Node_State, value),
            disabled ? "true" : "false");
    const bool ok = ovsdb_sync_upsert_where(SCHEMA_TABLE(Node_State), where, row, NULL);
    WARN_ON(ok == false);
    m->reported_enabled = m->enabled;
    LOGN(LOG_PREFIX(m, "reported %s: %s", CM2_BH_CMU_NODE_STATE_STATUS, BOOL_CSTR(disabled)));
}

static bool cm2_bh_cmu_vif_is_idle(const cm2_bh_cmu_vif_t *vif)
{
    if (vif->work) return false;
    if (ev_is_active(&vif->recalc)) return false;
    if (ev_is_active(&vif->deadline)) return false;
    if (ev_is_active(&vif->backoff)) return false;
    return true;
}

static bool cm2_bh_cmu_gre_is_idle(const cm2_bh_cmu_gre_t *gre)
{
    if (gre->work) return false;
    if (ev_is_active(&gre->recalc)) return false;
    if (ev_is_active(&gre->deadline)) return false;
    if (ev_is_active(&gre->backoff)) return false;
    return true;
}

static bool cm2_bh_cmu_is_idle(cm2_bh_cmu_t *m)
{
    cm2_bh_cmu_gre_t *gre;
    ds_tree_foreach (&m->gres, gre)
    {
        if (cm2_bh_cmu_gre_is_idle(gre) == false) return false;
    }
    cm2_bh_cmu_vif_t *vif;
    ds_tree_foreach (&m->vifs, vif)
    {
        if (cm2_bh_cmu_vif_is_idle(vif) == false) return false;
    }
    return true;
}

static void cm2_bh_cmu_recalc(cm2_bh_cmu_t *m)
{
    if (m->idle == false) return;
    if (m->enabled == m->reported_enabled) return;
    cm2_bh_cmu_report_enabled(m);
}

static void cm2_bh_cmu_recalc_cb(struct ev_loop *l, ev_idle *i, int mask)
{
    ev_idle_stop(l, i);
    cm2_bh_cmu_t *m = i->data;
    cm2_bh_cmu_recalc(m);
}

static void cm2_bh_cmu_schedule(cm2_bh_cmu_t *m)
{
    ev_idle_start(m->loop, &m->recalc);
}

static void cm2_bh_cmu_set_idle(cm2_bh_cmu_t *m, bool idle)
{
    if (m->idle == idle) return;
    LOGI(LOG_PREFIX(m, "idle: %s -> %s", BOOL_CSTR(m->idle), BOOL_CSTR(idle)));
    m->idle = idle;
    cm2_bh_cmu_schedule(m);
}

static void cm2_bh_cmu_update_idle(cm2_bh_cmu_t *m)
{
    cm2_bh_cmu_set_idle(m, cm2_bh_cmu_is_idle(m));
}

static bool cm2_bh_cmu_wms_active_to_need_delete(bool active)
{
    /* This is intended to consider port_state blips
     * (true->false->true) as a trigger to force remove a
     * given CMU row, to prompt CM link selector
     * implementation to re-process and re-add it into
     * bridge when necessary.
     */
    return (active == false);
}

static void cm2_bh_cmu_gre_deadline_arm(cm2_bh_cmu_gre_t *gre)
{
    if (ev_is_active(&gre->deadline)) return;
    ev_timer_set(&gre->deadline, CM2_BH_CMU_DEADLINE_SEC, 0);
    ev_timer_start(gre->m->loop, &gre->deadline);
}

static void cm2_bh_cmu_gre_recalc_arm(cm2_bh_cmu_gre_t *gre)
{
    cm2_bh_cmu_gre_deadline_arm(gre);
    ev_idle_start(gre->m->loop, &gre->recalc);
}

static void cm2_bh_cmu_gre_schedule(cm2_bh_cmu_gre_t *gre)
{
    if (gre == NULL) return;
    gre->work = true;
    cm2_bh_cmu_gre_recalc_arm(gre);
    cm2_bh_cmu_update_idle(gre->m);
}

static void cm2_bh_cmu_vif_deadline_arm(cm2_bh_cmu_vif_t *vif)
{
    if (ev_is_active(&vif->deadline)) return;
    ev_idle_start(vif->m->loop, &vif->recalc);
    ev_timer_set(&vif->deadline, CM2_BH_CMU_DEADLINE_SEC, 0);
    ev_timer_start(vif->m->loop, &vif->deadline);
}

static void cm2_bh_cmu_vif_recalc_arm(cm2_bh_cmu_vif_t *vif)
{
    cm2_bh_cmu_vif_deadline_arm(vif);
    ev_idle_start(vif->m->loop, &vif->recalc);
}

static void cm2_bh_cmu_vif_schedule(cm2_bh_cmu_vif_t *vif)
{
    vif->work = true;
    cm2_bh_cmu_vif_recalc_arm(vif);
    cm2_bh_cmu_update_idle(vif->m);
}

static void cm2_bh_cmu_vif_set_need_delete(cm2_bh_cmu_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->need_delete == v) return;
    if (v == false) return;
    LOGI(LOG_PREFIX_VIF(vif, "set: need_delete"));
    vif->need_delete = v;
    cm2_bh_cmu_vif_schedule(vif);
}

static void cm2_bh_cmu_gre_set_need_delete(cm2_bh_cmu_gre_t *gre, bool v)
{
    if (gre == NULL) return;
    if (gre->need_delete == v) return;
    if (v == false) return;
    LOGI(LOG_PREFIX_GRE(gre, "set: need_delete"));
    gre->need_delete = v;
    cm2_bh_cmu_gre_schedule(gre);
}

static void cm2_bh_cmu_set_has_l2(const char *if_name, bool has_l2)
{
    const int num_changed = ovsdb_sync_update_where(
            SCHEMA_TABLE(Connection_Manager_Uplink),
            json_pack("[[s, s, s]]", SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), "==", if_name),
            json_pack("{s:b}", SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2), has_l2));
    WARN_ON(num_changed != 1);
}

static void cm2_bh_cmu_set_has_l3(const char *if_name, bool has_l3)
{
    const int num_changed = ovsdb_sync_update_where(
            SCHEMA_TABLE(Connection_Manager_Uplink),
            json_pack("[[s, s, s]]", SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), "==", if_name),
            json_pack("{s:b}", SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3), has_l3));
    WARN_ON(num_changed != 1);
}

static bool cm2_bh_cmu_insert(const char *if_name, const char *if_type, bool has_l2, bool has_l3)
{
    const bool ok = ovsdb_sync_insert(
            SCHEMA_TABLE(Connection_Manager_Uplink),
            json_pack(
                    "{s:s, s:s, s:b, s:b}",
                    SCHEMA_COLUMN(Connection_Manager_Uplink, if_name),
                    if_name,
                    SCHEMA_COLUMN(Connection_Manager_Uplink, if_type),
                    if_type,
                    SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2),
                    has_l2,
                    SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3),
                    has_l3),
            NULL);
    return ok;
}

static void cm2_bh_cmu_delete(const char *if_name)
{
    ovsdb_sync_delete_where(
            SCHEMA_TABLE(Connection_Manager_Uplink),
            json_pack("[[s, s, s]]", SCHEMA_COLUMN(Connection_Manager_Uplink, if_name), "==", if_name));
}

static void cm2_bh_cmu_flush_type(cm2_bh_cmu_t *m, const char *if_type)
{
    const int num_changed = ovsdb_sync_delete_where(
            SCHEMA_TABLE(Connection_Manager_Uplink),
            json_pack("[[s, s, s]]", SCHEMA_COLUMN(Connection_Manager_Uplink, if_type), "==", if_type));
    LOGI(LOG_PREFIX(m, "flushed: %d '%s'", num_changed, if_type));
}

static void cm2_bh_cmu_flush(cm2_bh_cmu_t *m)
{
    LOGI(LOG_PREFIX(m, "flushing"));
    cm2_bh_cmu_flush_type(m, SCHEMA_CONSTS_IF_TYPE_GRE);
    cm2_bh_cmu_flush_type(m, SCHEMA_CONSTS_IF_TYPE_VIF);
}

static void cm2_bh_cmu_vif_set_enabled(cm2_bh_cmu_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->enabled == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "enabled: %s -> %s", BOOL_CSTR(vif->enabled), BOOL_CSTR(v)));
    vif->enabled = v;
    cm2_bh_cmu_vif_schedule(vif);
}

static void cm2_bh_cmu_gre_set_enabled(cm2_bh_cmu_gre_t *gre, bool v)
{
    if (gre == NULL) return;
    if (gre->enabled == v) return;
    LOGI(LOG_PREFIX_GRE(gre, "enabled: %s -> %s", BOOL_CSTR(gre->enabled), BOOL_CSTR(v)));
    gre->enabled = v;
    cm2_bh_cmu_gre_schedule(gre);
}

static void cm2_bh_cmu_set_enabled(cm2_bh_cmu_t *m, bool v)
{
    cm2_bh_cmu_gre_t *gre;
    ds_tree_foreach (&m->gres, gre)
        cm2_bh_cmu_gre_set_enabled(gre, v);
    cm2_bh_cmu_vif_t *vif;
    ds_tree_foreach (&m->vifs, vif)
        cm2_bh_cmu_vif_set_enabled(vif, v);

    if (m->enabled == v) return;
    LOGN(LOG_PREFIX(m, "enabled: %s -> %s", BOOL_CSTR(m->enabled), BOOL_CSTR(v)));
    m->enabled = v;
    cm2_bh_cmu_schedule(m);
}

void cm2_bh_cmu_vif_report_wvs_sta(cm2_bh_cmu_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.wvs_sta == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: wvs_sta: %s -> %s", BOOL_CSTR(vif->report.wvs_sta), BOOL_CSTR(v)));
    vif->report.wvs_sta = v;
    cm2_bh_cmu_vif_schedule(vif);
}

void cm2_bh_cmu_vif_report_wvs_4addr(cm2_bh_cmu_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.wvs_4addr == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: wvs_4addr: %s -> %s", BOOL_CSTR(vif->report.wvs_4addr), BOOL_CSTR(v)));
    vif->report.wvs_4addr = v;
    cm2_bh_cmu_vif_schedule(vif);
}

void cm2_bh_cmu_vif_report_wvs_mld(cm2_bh_cmu_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.wvs_mld == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: wvs_mld: %s -> %s", BOOL_CSTR(vif->report.wvs_mld), BOOL_CSTR(v)));
    vif->report.wvs_mld = v;
    cm2_bh_cmu_vif_schedule(vif);
}

static void cm2_bh_cmu_vif_report_wms_active(cm2_bh_cmu_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.wms_active == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: wms_active: %s -> %s", BOOL_CSTR(vif->report.wms_active), BOOL_CSTR(v)));
    vif->report.wms_active = v;
    cm2_bh_cmu_vif_set_need_delete(vif, cm2_bh_cmu_wms_active_to_need_delete(v));
    cm2_bh_cmu_vif_schedule(vif);
}

static void cm2_bh_cmu_vif_report_cmu_exists(cm2_bh_cmu_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.cmu_exists == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: cmu_exists: %s -> %s", BOOL_CSTR(vif->report.cmu_exists), BOOL_CSTR(v)));
    vif->report.cmu_exists = v;
    cm2_bh_cmu_vif_schedule(vif);
}

static void cm2_bh_cmu_vif_report_cmu_has_l2(cm2_bh_cmu_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.cmu_has_l2 == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: cmu_has_l2: %s -> %s", BOOL_CSTR(vif->report.cmu_has_l2), BOOL_CSTR(v)));
    vif->report.cmu_has_l2 = v;
    cm2_bh_cmu_vif_schedule(vif);
}

static void cm2_bh_cmu_vif_report_cmu_has_l3(cm2_bh_cmu_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.cmu_has_l3 == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: cmu_has_l3: %s -> %s", BOOL_CSTR(vif->report.cmu_has_l3), BOOL_CSTR(v)));
    vif->report.cmu_has_l3 = v;
    cm2_bh_cmu_vif_schedule(vif);
}

static bool cm2_bh_cmu_vif_derive_cmu_exists(cm2_bh_cmu_vif_t *vif)
{
    return vif->report.wvs_sta && vif->report.wvs_4addr && (vif->report.wvs_mld == false) && vif->enabled;
}

static bool cm2_bh_cmu_vif_derive_has_l2(cm2_bh_cmu_vif_t *vif)
{
    return cm2_bh_cmu_vif_derive_cmu_exists(vif) && vif->report.wms_active;
}

static bool cm2_bh_cmu_vif_derive_has_l3(cm2_bh_cmu_vif_t *vif)
{
    return cm2_bh_cmu_vif_derive_has_l2(vif);
}

static void cm2_bh_cmu_vif_recalc(cm2_bh_cmu_vif_t *vif)
{
    if (vif->work == false) return;
    if (ev_is_active(&vif->backoff)) return;

    vif->work = false;
    ev_timer_set(&vif->backoff, CM2_BH_CMU_BACKOFF_SEC, 0);
    ev_timer_start(vif->m->loop, &vif->backoff);

    const bool cmu_exists = cm2_bh_cmu_vif_derive_cmu_exists(vif);
    const bool has_l2 = cm2_bh_cmu_vif_derive_has_l2(vif);
    const bool has_l3 = cm2_bh_cmu_vif_derive_has_l3(vif);
    const bool cmu_changed = (cmu_exists != vif->report.cmu_exists);
    const bool l2_changed = (has_l2 != vif->report.cmu_has_l2);
    const bool l3_changed = (has_l3 != vif->report.cmu_has_l3);

    /* When enabled every CMU row for this if_name is ours to manage.
     * When not enabled, ownership is only kept until the row is gone,
     * so the disable transition can clean up, but rows inserted by an
     * external entity afterwards are never touched.
     */
    if (vif->report.cmu_exists == false)
        vif->owned = false;
    else if (vif->enabled)
        vif->owned = true;

    if (cmu_changed && cmu_exists)
    {
        LOGI(LOG_PREFIX_VIF(vif, "inserting"));
        vif->need_delete = false;
        const bool ok = cm2_bh_cmu_insert(vif->vif_name, SCHEMA_CONSTS_IF_TYPE_VIF, has_l2, has_l3);
        if (ok)
        {
            vif->owned = true;
            cm2_bh_cmu_vif_report_cmu_exists(vif, true);
        }
    }

    if (vif->report.cmu_exists && vif->owned)
    {
        if (l2_changed)
        {
            LOGI(LOG_PREFIX_VIF(vif, "set: has_L2: %s", BOOL_CSTR(has_l2)));
            cm2_bh_cmu_set_has_l2(vif->vif_name, has_l2);
        }

        if (l3_changed)
        {
            LOGI(LOG_PREFIX_VIF(vif, "set: has_L3: %s", BOOL_CSTR(has_l3)));
            cm2_bh_cmu_set_has_l3(vif->vif_name, has_l3);
        }
    }

    if (vif->owned && ((cmu_changed && (cmu_exists == false)) || (vif->report.cmu_exists && vif->need_delete)))
    {
        LOGI(LOG_PREFIX_VIF(vif, "deleting"));
        vif->need_delete = false;
        vif->owned = false;
        cm2_bh_cmu_delete(vif->vif_name);
        cm2_bh_cmu_vif_schedule(vif);
    }
}

static void cm2_bh_cmu_vif_recalc_cb(struct ev_loop *l, ev_idle *i, int mask)
{
    ev_idle_stop(l, i);
    cm2_bh_cmu_vif_t *vif = i->data;
    ev_timer_stop(vif->m->loop, &vif->deadline);
    cm2_bh_cmu_vif_recalc(vif);
    cm2_bh_cmu_update_idle(vif->m);
}

static void cm2_bh_cmu_vif_deadline_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_bh_cmu_vif_t *vif = t->data;
    LOGI(LOG_PREFIX_VIF(vif, "recalc deadline elapsed"));
    cm2_bh_cmu_vif_recalc(vif);
}

static void cm2_bh_cmu_vif_backoff_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_bh_cmu_vif_t *vif = t->data;
    cm2_bh_cmu_vif_recalc_arm(vif);
}

static void cm2_bh_cmu_gre_report_wic_exists(cm2_bh_cmu_gre_t *gre, bool v)
{
    if (gre == NULL) return;
    if (gre->report.wic_exists == v) return;
    LOGI(LOG_PREFIX_GRE(gre, "report: wic_exists: %s -> %s", BOOL_CSTR(gre->report.wic_exists), BOOL_CSTR(v)));
    gre->report.wic_exists = v;
    cm2_bh_cmu_gre_schedule(gre);
}

static void cm2_bh_cmu_gre_report_parent_wms_active(cm2_bh_cmu_gre_t *gre, bool v)
{
    if (gre == NULL) return;
    if (gre->report.parent_wms_active == v) return;
    LOGI(LOG_PREFIX_GRE(
            gre,
            "report: parent_wms_active: %s -> %s",
            BOOL_CSTR(gre->report.parent_wms_active),
            BOOL_CSTR(v)));
    gre->report.parent_wms_active = v;
    cm2_bh_cmu_gre_set_need_delete(gre, cm2_bh_cmu_wms_active_to_need_delete(v));
    cm2_bh_cmu_gre_schedule(gre);
}

static void cm2_bh_cmu_gre_report_wms_active(cm2_bh_cmu_gre_t *gre, bool v)
{
    if (gre == NULL) return;
    if (gre->report.wms_active == v) return;
    LOGI(LOG_PREFIX_GRE(gre, "report: wms_active: %s -> %s", BOOL_CSTR(gre->report.wms_active), BOOL_CSTR(v)));
    gre->report.wms_active = v;
    cm2_bh_cmu_gre_set_need_delete(gre, cm2_bh_cmu_wms_active_to_need_delete(v));
    cm2_bh_cmu_gre_schedule(gre);
}

static void cm2_bh_cmu_gre_report_cmu_exists(cm2_bh_cmu_gre_t *gre, bool v)
{
    if (gre == NULL) return;
    if (gre->report.cmu_exists == v) return;
    LOGI(LOG_PREFIX_GRE(gre, "report: cmu_exists: %s -> %s", BOOL_CSTR(gre->report.cmu_exists), BOOL_CSTR(v)));
    gre->report.cmu_exists = v;
    cm2_bh_cmu_gre_schedule(gre);
}

static void cm2_bh_cmu_gre_report_cmu_has_l2(cm2_bh_cmu_gre_t *gre, bool v)
{
    if (gre == NULL) return;
    if (gre->report.cmu_has_l2 == v) return;
    LOGI(LOG_PREFIX_GRE(gre, "report: cmu_has_l2: %s -> %s", BOOL_CSTR(gre->report.cmu_has_l2), BOOL_CSTR(v)));
    gre->report.cmu_has_l2 = v;
    cm2_bh_cmu_gre_schedule(gre);
}

static void cm2_bh_cmu_gre_report_cmu_has_l3(cm2_bh_cmu_gre_t *gre, bool v)
{
    if (gre == NULL) return;
    if (gre->report.cmu_has_l3 == v) return;
    LOGI(LOG_PREFIX_GRE(gre, "report: cmu_has_l3: %s -> %s", BOOL_CSTR(gre->report.cmu_has_l3), BOOL_CSTR(v)));
    gre->report.cmu_has_l3 = v;
    cm2_bh_cmu_gre_schedule(gre);
}

static bool cm2_bh_cmu_gre_derive_cmu_exists(cm2_bh_cmu_gre_t *gre)
{
    return gre->report.wic_exists && gre->enabled;
}

static bool cm2_bh_cmu_gre_derive_has_l2(cm2_bh_cmu_gre_t *gre)
{
    return cm2_bh_cmu_gre_derive_cmu_exists(gre) && gre->report.wms_active;
}

static bool cm2_bh_cmu_gre_derive_has_l3(cm2_bh_cmu_gre_t *gre)
{
    return cm2_bh_cmu_gre_derive_has_l2(gre);
}

static void cm2_bh_cmu_gre_recalc(cm2_bh_cmu_gre_t *gre)
{
    if (gre->work == false) return;
    if (ev_is_active(&gre->backoff)) return;

    gre->work = false;
    ev_timer_set(&gre->backoff, CM2_BH_CMU_BACKOFF_SEC, 0);
    ev_timer_start(gre->m->loop, &gre->backoff);

    const bool cmu_exists = cm2_bh_cmu_gre_derive_cmu_exists(gre);
    const bool has_l2 = cm2_bh_cmu_gre_derive_has_l2(gre);
    const bool has_l3 = cm2_bh_cmu_gre_derive_has_l3(gre);
    const bool cmu_changed = (cmu_exists != gre->report.cmu_exists);
    const bool l2_changed = (has_l2 != gre->report.cmu_has_l2);
    const bool l3_changed = (has_l3 != gre->report.cmu_has_l3);

    /* See the ownership comment in cm2_bh_cmu_vif_recalc(). */
    if (gre->report.cmu_exists == false)
        gre->owned = false;
    else if (gre->enabled)
        gre->owned = true;

    if (cmu_changed && cmu_exists)
    {
        LOGI(LOG_PREFIX_GRE(gre, "inserting"));
        gre->need_delete = false;
        const bool ok = cm2_bh_cmu_insert(gre->gre_name, SCHEMA_CONSTS_IF_TYPE_GRE, has_l2, has_l3);
        if (ok)
        {
            gre->owned = true;
            cm2_bh_cmu_gre_report_cmu_exists(gre, true);
        }
    }

    if (gre->report.cmu_exists && gre->owned)
    {
        if (l2_changed)
        {
            LOGI(LOG_PREFIX_GRE(gre, "set: has_L2: %s", BOOL_CSTR(has_l2)));
            cm2_bh_cmu_set_has_l2(gre->gre_name, has_l2);
        }

        if (l3_changed)
        {
            LOGI(LOG_PREFIX_GRE(gre, "set: has_L3: %s", BOOL_CSTR(has_l3)));
            cm2_bh_cmu_set_has_l3(gre->gre_name, has_l3);
        }
    }

    if (gre->owned && ((cmu_changed && (cmu_exists == false)) || (gre->report.cmu_exists && gre->need_delete)))
    {
        gre->need_delete = false;
        gre->owned = false;
        LOGI(LOG_PREFIX_GRE(gre, "deleting"));
        cm2_bh_cmu_delete(gre->gre_name);
        cm2_bh_cmu_gre_schedule(gre);
    }
}

static void cm2_bh_cmu_gre_recalc_cb(struct ev_loop *l, ev_idle *i, int mask)
{
    ev_idle_stop(l, i);
    cm2_bh_cmu_gre_t *gre = i->data;
    ev_timer_stop(gre->m->loop, &gre->deadline);
    cm2_bh_cmu_gre_recalc(gre);
    cm2_bh_cmu_update_idle(gre->m);
}

static void cm2_bh_cmu_gre_deadline_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_bh_cmu_gre_t *gre = t->data;
    LOGI(LOG_PREFIX_GRE(gre, "recalc deadline elapsed"));
    cm2_bh_cmu_gre_recalc(gre);
}

static void cm2_bh_cmu_gre_backoff_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_bh_cmu_gre_t *gre = t->data;
    cm2_bh_cmu_gre_recalc_arm(gre);
}

void cm2_bh_cmu_WVS(
        cm2_bh_cmu_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_VIF_State *old_row,
        const struct schema_Wifi_VIF_State *new_row)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;
    const char *if_name = CM2_OVS_COL(mon, old_row->if_name, new_row->if_name);
    cm2_bh_cmu_vif_t *vif = cm2_bh_cmu_lookup_vif(m, if_name);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, mode)))
    {
        const bool v = new_row && new_row->mode_exists && (strcmp(new_row->mode, "sta") == 0);
        cm2_bh_cmu_vif_report_wvs_sta(vif, v);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, wds)))
    {
        const bool v = new_row && new_row->wds_exists && new_row->wds;
        cm2_bh_cmu_vif_report_wvs_4addr(vif, v);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, mld_if_name)))
    {
        const bool v = new_row && new_row->mld_if_name_exists && (strlen(new_row->mld_if_name) > 0);
        cm2_bh_cmu_vif_report_wvs_mld(vif, v);
    }
}

void cm2_bh_cmu_WMS(
        cm2_bh_cmu_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Master_State *old_row,
        const struct schema_Wifi_Master_State *new_row)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;
    const char *if_name = CM2_OVS_COL(mon, old_row->if_name, new_row->if_name);
    cm2_bh_cmu_vif_t *vif = cm2_bh_cmu_lookup_vif(m, if_name);
    cm2_bh_cmu_gre_t *gre = cm2_bh_cmu_lookup_gre(m, if_name);
    cm2_bh_cmu_gre_t *gre_by_parent = cm2_bh_cmu_lookup_gre_by_parent(m, if_name);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, port_state)))
    {
        const bool v = new_row && new_row->port_state_exists && (strcmp(new_row->port_state, "active") == 0);
        cm2_bh_cmu_vif_report_wms_active(vif, v);
        cm2_bh_cmu_gre_report_wms_active(gre, v);
        cm2_bh_cmu_gre_report_parent_wms_active(gre_by_parent, v);
    }
}

void cm2_bh_cmu_CMU(
        cm2_bh_cmu_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Connection_Manager_Uplink *old_row,
        const struct schema_Connection_Manager_Uplink *new_row)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;
    const char *if_name = CM2_OVS_COL(mon, old_row->if_name, new_row->if_name);
    cm2_bh_cmu_vif_t *vif = cm2_bh_cmu_lookup_vif(m, if_name);
    cm2_bh_cmu_gre_t *gre = cm2_bh_cmu_lookup_gre(m, if_name);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, has_L2)))
    {
        const bool v = new_row && new_row->has_L2_exists && new_row->has_L2;
        cm2_bh_cmu_vif_report_cmu_has_l2(vif, v);
        cm2_bh_cmu_gre_report_cmu_has_l2(gre, v);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Connection_Manager_Uplink, has_L3)))
    {
        const bool v = new_row && new_row->has_L3_exists && new_row->has_L3;
        cm2_bh_cmu_vif_report_cmu_has_l3(vif, v);
        cm2_bh_cmu_gre_report_cmu_has_l3(gre, v);
    }

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            cm2_bh_cmu_vif_report_cmu_exists(vif, true);
            cm2_bh_cmu_gre_report_cmu_exists(gre, true);
            break;
        case OVSDB_UPDATE_MODIFY:
            break;
        case OVSDB_UPDATE_DEL:
            cm2_bh_cmu_vif_report_cmu_exists(vif, false);
            cm2_bh_cmu_gre_report_cmu_exists(gre, false);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

void cm2_bh_cmu_WIC(
        cm2_bh_cmu_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Inet_Config *old_row,
        const struct schema_Wifi_Inet_Config *new_row)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;
    const char *if_name = CM2_OVS_COL(mon, old_row->if_name, new_row->if_name);
    cm2_bh_cmu_gre_t *gre = cm2_bh_cmu_lookup_gre(m, if_name);

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            cm2_bh_cmu_gre_report_wic_exists(gre, true);
            break;
        case OVSDB_UPDATE_MODIFY:
            break;
        case OVSDB_UPDATE_DEL:
            cm2_bh_cmu_gre_report_wic_exists(gre, false);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

void cm2_bh_cmu_NC(
        cm2_bh_cmu_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Node_Config *old_row,
        const struct schema_Node_Config *new_row)
{
    if (m == NULL) return;
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;
    const char *module = CM2_OVS_COL(mon, old_row->module, new_row->module);
    const char *key = CM2_OVS_COL(mon, old_row->key, new_row->key);
    if (strcmp_null(module, CM2_BH_CMU_NODE_MODULE) != 0) return;
    if (strcmp_null(key, CM2_BH_CMU_NODE_CFG_DISABLE) != 0) return;

    cm2_bh_cmu_set_enabled(m, cm2_bh_cmu_node_config_enabled_get());
}

void cm2_bh_cmu_vif_drop(cm2_bh_cmu_vif_t *vif)
{
    if (vif == NULL) return;
    LOGI(LOG_PREFIX_VIF(vif, "dropping"));
    cm2_bh_cmu_t *m = vif->m;
    if (vif->owned) cm2_bh_cmu_delete(vif->vif_name);
    ev_idle_stop(vif->m->loop, &vif->recalc);
    ev_timer_stop(vif->m->loop, &vif->deadline);
    ev_timer_stop(vif->m->loop, &vif->backoff);
    ds_tree_remove(&vif->m->vifs, vif);
    FREE(vif->vif_name);
    FREE(vif);
    cm2_bh_cmu_update_idle(m);
}

cm2_bh_cmu_vif_t *cm2_bh_cmu_vif_alloc(cm2_bh_cmu_t *m, const char *vif_name)
{
    if (m == NULL) return NULL;

    const bool already_exists = (ds_tree_find(&m->vifs, vif_name) != NULL);
    if (already_exists) return NULL;

    cm2_bh_cmu_vif_t *vif = CALLOC(1, sizeof(*vif));
    ev_idle_init(&vif->recalc, cm2_bh_cmu_vif_recalc_cb);
    ev_timer_init(&vif->backoff, cm2_bh_cmu_vif_backoff_cb, 0, 0);
    ev_timer_init(&vif->deadline, cm2_bh_cmu_vif_deadline_cb, 0, 0);
    vif->recalc.data = vif;
    vif->backoff.data = vif;
    vif->deadline.data = vif;
    vif->m = m;
    vif->enabled = m->enabled;
    vif->vif_name = STRDUP(vif_name);
    ds_tree_insert(&m->vifs, vif, vif->vif_name);
    LOGI(LOG_PREFIX_VIF(vif, "allocated"));
    CM2_BH_OVS_INIT(m, cm2_bh_cmu_WVS, Wifi_VIF_State, if_name, vif->vif_name);
    CM2_BH_OVS_INIT(m, cm2_bh_cmu_WMS, Wifi_Master_State, if_name, vif->vif_name);
    CM2_BH_OVS_INIT(m, cm2_bh_cmu_CMU, Connection_Manager_Uplink, if_name, vif->vif_name);
    CM2_BH_OVS_INIT(m, cm2_bh_cmu_WIC, Wifi_Inet_Config, if_name, vif->vif_name);
    return vif;
}

void cm2_bh_cmu_gre_drop(cm2_bh_cmu_gre_t *gre)
{
    if (gre == NULL) return;
    LOGI(LOG_PREFIX_GRE(gre, "dropping"));
    cm2_bh_cmu_t *m = gre->m;
    if (gre->owned) cm2_bh_cmu_delete(gre->gre_name);
    ev_idle_stop(gre->m->loop, &gre->recalc);
    ev_timer_stop(gre->m->loop, &gre->backoff);
    ev_timer_stop(gre->m->loop, &gre->deadline);
    ds_tree_remove(&gre->m->gres, gre);
    ds_tree_remove(&gre->m->gres_by_parent, gre);
    FREE(gre->parent_name);
    FREE(gre->gre_name);
    FREE(gre);
    cm2_bh_cmu_update_idle(m);
}

cm2_bh_cmu_gre_t *cm2_bh_cmu_gre_alloc(cm2_bh_cmu_t *m, const char *gre_name, const char *parent_name)
{
    if (m == NULL) return NULL;

    const bool already_exists = (ds_tree_find(&m->gres, gre_name) != NULL);
    if (already_exists) return NULL;

    const bool parent_already_exists = (ds_tree_find(&m->gres_by_parent, parent_name) != NULL);
    if (parent_already_exists) return NULL;

    cm2_bh_cmu_gre_t *gre = CALLOC(1, sizeof(*gre));
    ev_idle_init(&gre->recalc, cm2_bh_cmu_gre_recalc_cb);
    ev_timer_init(&gre->backoff, cm2_bh_cmu_gre_backoff_cb, 0, 0);
    ev_timer_init(&gre->deadline, cm2_bh_cmu_gre_deadline_cb, 0, 0);
    gre->recalc.data = gre;
    gre->backoff.data = gre;
    gre->deadline.data = gre;
    gre->m = m;
    gre->enabled = m->enabled;
    gre->gre_name = STRDUP(gre_name);
    gre->parent_name = STRDUP(parent_name);
    ds_tree_insert(&m->gres, gre, gre->gre_name);
    ds_tree_insert(&m->gres_by_parent, gre, gre->parent_name);
    LOGI(LOG_PREFIX_GRE(gre, "allocated"));
    CM2_BH_OVS_INIT(m, cm2_bh_cmu_WVS, Wifi_VIF_State, if_name, gre->gre_name);
    CM2_BH_OVS_INIT(m, cm2_bh_cmu_WMS, Wifi_Master_State, if_name, gre->gre_name);
    CM2_BH_OVS_INIT(m, cm2_bh_cmu_CMU, Connection_Manager_Uplink, if_name, gre->gre_name);
    CM2_BH_OVS_INIT(m, cm2_bh_cmu_WIC, Wifi_Inet_Config, if_name, gre->gre_name);
    return gre;
}

cm2_bh_cmu_t *cm2_bh_cmu_alloc(void)
{
    cm2_bh_cmu_t *m = CALLOC(1, sizeof(*m));
    m->loop = EV_DEFAULT;
    ds_tree_init(&m->vifs, ds_str_cmp, cm2_bh_cmu_vif_t, node);
    ds_tree_init(&m->gres, ds_str_cmp, cm2_bh_cmu_gre_t, node);
    ds_tree_init(&m->gres_by_parent, ds_str_cmp, cm2_bh_cmu_gre_t, node_by_parent);
    ev_idle_init(&m->recalc, cm2_bh_cmu_recalc_cb);
    m->recalc.data = m;
    m->idle = true;
    LOGI(LOG_PREFIX(m, "allocated"));

    m->enabled = cm2_bh_cmu_node_config_enabled_get();
    if (m->enabled) cm2_bh_cmu_flush(m);
    cm2_bh_cmu_report_enabled(m);
    return m;
}

cm2_bh_cmu_t *cm2_bh_cmu_from_list(const char *list)
{
    cm2_bh_cmu_t *m = cm2_bh_cmu_alloc();
    char *entries = strdupa(list ?: "");
    char *entry;
    while ((entry = strsep(&entries, " ")) != NULL)
    {
        const char *phy_name = strsep(&entry, ":");
        const char *vif_name = strsep(&entry, ":");
        if (phy_name == NULL) continue;
        if (vif_name == NULL) continue;
        const char *gre_name = strfmta(CM2_BH_GRE_PREFIX "%s", vif_name);
        cm2_bh_cmu_vif_alloc(m, vif_name);
        cm2_bh_cmu_gre_alloc(m, gre_name, vif_name);
    }
    return m;
}

static void cm2_bh_cmu_drop_vifs(cm2_bh_cmu_t *m)
{
    cm2_bh_cmu_vif_t *vif;
    while ((vif = ds_tree_head(&m->vifs)) != NULL)
    {
        cm2_bh_cmu_vif_drop(vif);
    }
}

static void cm2_bh_cmu_drop_gres(cm2_bh_cmu_t *m)
{
    cm2_bh_cmu_gre_t *gre;
    while ((gre = ds_tree_head(&m->gres)) != NULL)
    {
        cm2_bh_cmu_gre_drop(gre);
    }
}

void cm2_bh_cmu_drop(cm2_bh_cmu_t *m)
{
    if (m == NULL) return;
    LOGI(LOG_PREFIX(m, "dropping"));
    cm2_bh_cmu_drop_gres(m);
    cm2_bh_cmu_drop_vifs(m);
    ev_idle_stop(m->loop, &m->recalc);
    FREE(m);
}

cm2_bh_cmu_vif_t *cm2_bh_cmu_lookup_vif(cm2_bh_cmu_t *m, const char *vif_name)
{
    if (m == NULL) return NULL;
    return ds_tree_find(&m->vifs, vif_name);
}

cm2_bh_cmu_gre_t *cm2_bh_cmu_lookup_gre(cm2_bh_cmu_t *m, const char *gre_name)
{
    if (m == NULL) return NULL;
    return ds_tree_find(&m->gres, gre_name);
}

cm2_bh_cmu_gre_t *cm2_bh_cmu_lookup_gre_by_parent(cm2_bh_cmu_t *m, const char *parent_name)
{
    if (m == NULL) return NULL;
    return ds_tree_find(&m->gres_by_parent, parent_name);
}
