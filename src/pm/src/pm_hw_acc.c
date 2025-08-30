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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ev.h>

#include "os.h"
#include "log.h"
#include "kconfig.h"
#include "target.h"
#include "memutil.h"
#include "module.h"
#include "util.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "schema.h"

#include "hw_acc.h"
#include "pm_hw_acc_load.h"


#define PM_HW_ACC_MODULE            "pm_hw_acc"
#define KEY_HW_ACC_CFG              "enable"
#define KEY_HW_ACC_STATUS           "hw_acc_status"
#define LOG_PREFIX(m, fmt, ...)     PM_HW_ACC_MODULE": " fmt, ##__VA_ARGS__
#define LOG_PREFIX_LOAD(l, fmt, ...) LOG_PREFIX(l->m, "load: " fmt, ##__VA__ARGS__)


#define VAL_HW_ACC_OFF              "false"
#define VAL_HW_ACC_ON               "true"
#define PM_HW_ACC_FLUSH_SEC         10.0

enum pm_hw_acc_load_status {
    LOAD_STATUS_UNKNOWN,
    LOAD_STATUS_CPU_IDLE,
    LOAD_STATUS_CPU_LOADED,
};

enum pm_hw_acc_node_status {
    NODE_STATUS_UNDEFINED,
    NODE_STATUS_DISABLE_ACCEL,
    NODE_STATUS_ENABLE_ACCEL,
};

struct pm_hw_acc {
    ovsdb_table_t table_Node_Config;
    ovsdb_table_t table_Node_State;
    ovsdb_table_t table_Wifi_Stats_Config;
    enum pm_hw_acc_node_status node_status;
    enum pm_hw_acc_load_status load_status;
    hw_acc_ctrl_flags_t flags;
    struct pm_hw_acc_load *load;
    struct ev_loop *loop;
    uint32_t n_latency_stats;
    ev_timer flush;
};

static struct pm_hw_acc g_pm_hw_acc_data;

MODULE_DATA(pm_hw_acc, pm_hw_acc_load, pm_hw_acc_unload, &g_pm_hw_acc_data)

const char *pm_hw_acc_node_status_to_cstr(enum pm_hw_acc_node_status s)
{
    switch(s)
    {
        case NODE_STATUS_UNDEFINED: return "undefined";
        case NODE_STATUS_DISABLE_ACCEL: return "disabled accel";
        case NODE_STATUS_ENABLE_ACCEL: return "enabled accel";
    }
    return "";
}

const char *pm_hw_acc_load_status_to_cstr(enum pm_hw_acc_load_status s)
{
    switch(s)
    {
        case LOAD_STATUS_UNKNOWN: return "unknown";
        case LOAD_STATUS_CPU_IDLE: return "cpu idle";
        case LOAD_STATUS_CPU_LOADED: return "cpu loaded";
    }
    return "";
}

const char *pm_hw_acc_ctrl_flag_to_cstr(enum hw_acc_ctrl_flags s)
{
    switch (s)
    {
        case HW_ACC_F_PASS_XDP: return "pass_xdp";
        case HW_ACC_F_PASS_TC_INGRESS: return "pass_tc_ingress";
        case HW_ACC_F_PASS_TC_EGRESS: return "pass_tc_egress";
        case HW_ACC_F_DISABLE_ACCEL: return "disable_accel";
    }
    return "";
}

static bool pm_node_state_set(struct pm_hw_acc *m, const char *key, const char *value,bool persist)
{
    struct schema_Node_State node_state;
    json_t *where;

    where = json_array();
    json_array_append_new(where, ovsdb_tran_cond_single("module", OFUNC_EQ, PM_HW_ACC_MODULE));
    json_array_append_new(where, ovsdb_tran_cond_single("key", OFUNC_EQ, (char *)key));

    MEMZERO(node_state);
    SCHEMA_SET_STR(node_state.module, PM_HW_ACC_MODULE);
    SCHEMA_SET_STR(node_state.key, key);

    if (value != NULL)
    {
        SCHEMA_SET_STR(node_state.value, value);
        if (persist)
            SCHEMA_SET_BOOL(node_state.persist, persist);

        ovsdb_table_upsert_where(&m->table_Node_State, where, &node_state, false);
    }
    else
    {
        ovsdb_table_delete_where(&m->table_Node_State, where);
    }
    return true;
}

static hw_acc_ctrl_flags_t pm_hw_acc_derive_ctrl_flags(const struct pm_hw_acc *m)
{
    hw_acc_ctrl_flags_t flags = 0;

    switch (m->node_status)
    {
        case NODE_STATUS_UNDEFINED:
            break;
        case NODE_STATUS_DISABLE_ACCEL:
            flags |= HW_ACC_F_DISABLE_ACCEL;
            break;
        case NODE_STATUS_ENABLE_ACCEL:
            break;
    }

    switch (m->load_status)
    {
        case LOAD_STATUS_UNKNOWN:
            break;
        case LOAD_STATUS_CPU_IDLE:
            flags |= HW_ACC_F_PASS_XDP;
            flags |= HW_ACC_F_PASS_TC_EGRESS;
            break;
        case LOAD_STATUS_CPU_LOADED:
            break;
    }

    return flags;
}

static void pm_hw_acc_flush_start(struct pm_hw_acc *m)
{
    ev_timer_stop(m->loop, &m->flush);
    ev_timer_set(&m->flush, PM_HW_ACC_FLUSH_SEC, PM_HW_ACC_FLUSH_SEC);
    ev_timer_start(m->loop, &m->flush);
}

static void pm_hw_acc_flush_stop(struct pm_hw_acc *m)
{
    ev_timer_stop(m->loop, &m->flush);
}

static bool pm_hw_acc_accel_is_enabled(const hw_acc_ctrl_flags_t flags)
{
    return !(flags & HW_ACC_F_DISABLE_ACCEL);
}

static void pm_hw_acc_flush_policy(struct pm_hw_acc *m, const hw_acc_ctrl_flags_t flags)
{
    switch (m->load_status)
    {
        case LOAD_STATUS_UNKNOWN:
            break;
        case LOAD_STATUS_CPU_IDLE:
            pm_hw_acc_flush_stop(m);
            break;
        case LOAD_STATUS_CPU_LOADED:
            /* No point in flushing flows when acc is disabled */
            if (pm_hw_acc_accel_is_enabled(flags))
                pm_hw_acc_flush_start(m);
            break;
    }
}

#define PM_HW_ACC_CTRL_FLAG_DEBUG(flag, old, new, changed) \
    if (changed & flag) { \
        LOGI("pm_hw_acc: load: flags: %s: %s -> %s", pm_hw_acc_ctrl_flag_to_cstr(flag), \
                                                     (old & flag) ? "1" : "0",  \
                                                     (new & flag) ? "1" : "0"); \
    }

static void pm_hw_acc_set_mode(struct pm_hw_acc *m, hw_acc_ctrl_flags_t flags)
{
    const hw_acc_ctrl_flags_t changed = (m->flags ^ flags);
    if (changed == 0) return;

    PM_HW_ACC_CTRL_FLAG_DEBUG(HW_ACC_F_PASS_XDP, m->flags, flags, changed);
    PM_HW_ACC_CTRL_FLAG_DEBUG(HW_ACC_F_PASS_TC_INGRESS, m->flags, flags, changed);
    PM_HW_ACC_CTRL_FLAG_DEBUG(HW_ACC_F_PASS_TC_EGRESS, m->flags, flags, changed);
    PM_HW_ACC_CTRL_FLAG_DEBUG(HW_ACC_F_DISABLE_ACCEL, m->flags, flags, changed);

    hw_acc_mode_set(flags);
    pm_hw_acc_flush_policy(m, flags);
    m->flags = flags;
}

static void
pm_hw_acc_recalc(struct pm_hw_acc *m)
{
    LOGD(LOG_PREFIX(m, "recalc"));
    hw_acc_ctrl_flags_t flags = pm_hw_acc_derive_ctrl_flags(m);
    pm_hw_acc_set_mode(m, flags);
}

static void
pm_hw_acc_set_node_status(struct pm_hw_acc *m, const enum pm_hw_acc_node_status status)
{
    if (m->node_status == status) return;
    LOGI(LOG_PREFIX(m, "node_status: %s -> %s",
                    pm_hw_acc_node_status_to_cstr(m->node_status),
                    pm_hw_acc_node_status_to_cstr(status)));
    m->node_status = status;
    pm_hw_acc_recalc(m);
}

static void
pm_hw_acc_set_load_status(struct pm_hw_acc *m, const enum pm_hw_acc_load_status status)
{
    if (m->load_status == status) return;
    LOGI(LOG_PREFIX(m, "load_status: %s -> %s",
                    pm_hw_acc_load_status_to_cstr(m->load_status),
                    pm_hw_acc_load_status_to_cstr(status)));
    m->load_status = status;
    pm_hw_acc_recalc(m);
}

static const char *
pm_hw_acc_node_config_get_module(
    ovsdb_update_monitor_t *mon,
    struct schema_Node_Config *old_rec,
    struct schema_Node_Config *new_rec)
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

static const char *
pm_hw_acc_node_config_get_key(
    ovsdb_update_monitor_t *mon,
    struct schema_Node_Config *old_rec,
    struct schema_Node_Config *new_rec)
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

static const char *
pm_hw_acc_node_config_get_value(
    ovsdb_update_monitor_t *mon,
    struct schema_Node_Config *old_rec,
    struct schema_Node_Config *new_rec)
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

static enum pm_hw_acc_node_status
pm_hw_acc_node_status_from_cstr(const char *str)
{
    if (str && strcmp(str, VAL_HW_ACC_ON) == 0) return NODE_STATUS_ENABLE_ACCEL;
    if (str && strcmp(str, VAL_HW_ACC_OFF) == 0) return NODE_STATUS_DISABLE_ACCEL;
    return NODE_STATUS_UNDEFINED;
}

static void
pm_hw_acc_ovsdb_enable_set(ovsdb_update_monitor_t *mon, const char *value)
{
    ovsdb_table_t *table = mon->mon_data;
    struct pm_hw_acc *m = container_of(table, typeof(*m), table_Node_Config);
    const enum pm_hw_acc_node_status mode = value
                                   ? pm_hw_acc_node_status_from_cstr(value)
                                   : NODE_STATUS_UNDEFINED;
    pm_hw_acc_set_node_status(m, mode);

    /* FIXME: It is unclear to me what to do here. This
     * is adapting prior iteration of the code that
     * automatically implied persistence reporting only
     * in the status with nothing to respect the Config
     * input.
     *
     * Hence, this is kept as is, including doing it
     * after setting the ovsdb-driven value only. If the
     * pm_hw_acc_recalc() arrives at a disabled
     * accelerator it probably makes sense to keep
     * reporting it as whatever Config holds to uphold
     * promises to the controller on "did my action
     * complete".
     */
    const bool persist = (mode == NODE_STATUS_ENABLE_ACCEL);
    pm_node_state_set(m, KEY_HW_ACC_STATUS, value, persist);
}

static void
pm_hw_acc_ovsdb_node_config_cb(
    ovsdb_update_monitor_t *mon,
    struct schema_Node_Config *old_rec,
    struct schema_Node_Config *new_rec)
{
    const char *module = pm_hw_acc_node_config_get_module(mon, old_rec, new_rec);
    if (module == NULL) return;

    const char *key = pm_hw_acc_node_config_get_key(mon, old_rec, new_rec);
    if (key == NULL) return;

    const char *value = pm_hw_acc_node_config_get_value(mon, old_rec, new_rec);
    /* NULL value means removal */

    if (strcmp(module, PM_HW_ACC_MODULE) == 0)
    {
        if (strcmp(key, KEY_HW_ACC_CFG) == 0)
        {
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Node_Config, value)))
            {
                pm_hw_acc_ovsdb_enable_set(mon, value);
            }
        }
    }
}

static void pm_hw_acc_load_changed_cb(void *priv)
{
    struct pm_hw_acc *m = priv;
    const enum pm_hw_acc_load_mode mode = pm_hw_acc_load_mode_get(m->load);
    switch (mode)
    {
        case PM_HW_ACC_LOAD_INIT:
            break;
        case PM_HW_ACC_LOAD_INACTIVE:
            pm_hw_acc_set_load_status(m, LOAD_STATUS_CPU_IDLE);
            break;
        case PM_HW_ACC_LOAD_ACTIVE:
            pm_hw_acc_set_load_status(m, LOAD_STATUS_CPU_LOADED);
            break;
        case PM_HW_ACC_LOAD_DEACTIVATING:
            break;
    }
}

static void pm_hw_acc_flush_cb(struct ev_loop *loop, ev_timer *t, int flags)
{
    const struct pm_hw_acc *m = t->data;
    (void)m;

    hw_acc_flush_all_flows();
}

static void
pm_hw_acc_attach_load(struct pm_hw_acc *m)
{
    if (WARN_ON(m->load != NULL)) return;
    LOGI(LOG_PREFIX(m, "load: attaching"));
    m->load = pm_hw_acc_load_alloc(pm_hw_acc_load_changed_cb, m);
}

static void
pm_hw_acc_detach_load(struct pm_hw_acc *m)
{
    if (WARN_ON(m->load == NULL)) return;
    LOGI(LOG_PREFIX(m, "load: detaching"));
    pm_hw_acc_load_drop(m->load);
    m->load = NULL;
    pm_hw_acc_set_load_status(m, LOAD_STATUS_UNKNOWN);
}

static void
pm_hw_acc_reattach_load(struct pm_hw_acc *m)
{
    const bool running = (m->load != NULL);
    const bool desired = (m->n_latency_stats > 0);
    /* The current latency stats implementation relies on
     * XDP. This may require additional actions to be taken
     * by the system packet accelerator.
     *
     * If latency stats are not configured there's no point
     * in running the dynamic load-based adjustments.
     */
    if (running && !desired)
    {
        pm_hw_acc_detach_load(m);
    }
    else if (!running && desired)
    {
        pm_hw_acc_attach_load(m);
    }
}

static void
pm_hw_acc_ovsdb_wifi_stats_config_cb(
    ovsdb_update_monitor_t *mon,
    struct schema_Wifi_Stats_Config *old_rec,
    struct schema_Wifi_Stats_Config *new_rec)
{
    ovsdb_table_t *table = mon->mon_data;
    struct pm_hw_acc *m = container_of(table, typeof(*m), table_Wifi_Stats_Config);
    const bool old_is_latency = (old_rec->stats_type_exists && strcmp(old_rec->stats_type, SCHEMA_CONSTS_REPORT_TYPE_LATENCY) == 0);
    const bool new_is_latency = (new_rec->stats_type_exists && strcmp(new_rec->stats_type, SCHEMA_CONSTS_REPORT_TYPE_LATENCY) == 0);

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_ERROR:
            break;
        case OVSDB_UPDATE_DEL:
            if (old_is_latency) m->n_latency_stats--;
            break;
        case OVSDB_UPDATE_NEW:
            if (new_is_latency) m->n_latency_stats++;
            break;
        case OVSDB_UPDATE_MODIFY:
            if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Stats_Config, stats_type)))
            {
                if (old_is_latency) m->n_latency_stats--;
                if (new_is_latency) m->n_latency_stats++;
            }
            break;
    }

    if (old_is_latency || new_is_latency)
    {
        LOGD(LOG_PREFIX(m, "n_latency_stats: %"PRIu32, m->n_latency_stats));
    }

    pm_hw_acc_reattach_load(m);
}

void pm_hw_acc_init(struct pm_hw_acc *m)
{
    OVSDB_TABLE_VAR_INIT_NO_KEY(&m->table_Node_Config, Node_Config);
    OVSDB_TABLE_VAR_INIT_NO_KEY(&m->table_Node_State, Node_State);
    OVSDB_TABLE_VAR_INIT_NO_KEY(&m->table_Wifi_Stats_Config, Wifi_Stats_Config);
    ev_timer_init(&m->flush, pm_hw_acc_flush_cb, 0, 0);
    m->flush.data = m;
}

void pm_hw_acc_attach(struct pm_hw_acc *m)
{
    ovsdb_table_monitor(&m->table_Node_Config, (void *)pm_hw_acc_ovsdb_node_config_cb, true);
    ovsdb_table_monitor(&m->table_Wifi_Stats_Config, (void *)pm_hw_acc_ovsdb_wifi_stats_config_cb, true);
    m->loop = EV_DEFAULT;
}

void pm_hw_acc_detach(struct pm_hw_acc *m)
{
    ovsdb_update_monitor_cancel(&m->table_Node_Config.monitor,
                                m->table_Node_Config.table_name);
    ovsdb_update_monitor_cancel(&m->table_Wifi_Stats_Config.monitor,
                                m->table_Wifi_Stats_Config.table_name);
    pm_hw_acc_load_drop(m->load);
    ev_timer_stop(m->loop, &m->flush);
}

void pm_hw_acc_load(void *data)
{
    struct pm_hw_acc *m = data;
    pm_hw_acc_init(m);
    pm_hw_acc_attach(m);
    LOGI(LOG_PREFIX(m, "acceleration: loaded"));
}

void pm_hw_acc_unload(void *data)
{
    struct pm_hw_acc *m = data;
    pm_hw_acc_detach(m);
    LOGI(LOG_PREFIX(m, "acceleration: unloaded"));
}
