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
#include "ow_ovsdb_ms.h"
#include <ovsdb_cache.h>
#include <ev.h>

/**
 * OneWifi OVSDB Wifi_Master_State synchronization
 *
 * This takes care of keeping Wifi_Master_State synchronized
 * as per inputs from OVSDB and ow_ovsdb core (eventually
 * osw_state). It'll keep retrying as necessary and correct
 * deviations, even when Wifi_Master_State is altered from
 * the outside.
 */

#define OW_OVSDB_MS_RETRY_SECONDS 5.0

enum ow_ovsdb_ms_port_state {
    OW_OVSDB_MS_PORT_UNSPEC,
    OW_OVSDB_MS_PORT_ACTIVE,
    OW_OVSDB_MS_PORT_INACTIVE,
};

struct ow_ovsdb_ms {
    struct ds_tree_node node;
    struct ow_ovsdb_ms_root *root;
    char *vif_name;
    bool vif_exists;
    bool ovsdb_sync;
    bool ovsdb_exists;

    /* This is used to allow signalling the port_state:
     * <some_state> -> inactive -> active. This is necessary
     * to workaround issue in CM which doesn't properly
     * handle an unchanged port_state even when the
     * underlying link was reloaded.
     */
    bool signal_disconnect;

    enum ow_ovsdb_ms_port_state vif_port_state;
    enum ow_ovsdb_ms_port_state ovsdb_port_state;
    ev_timer work;
};

static const char *
ow_ovsdb_ms_port_to_str(const enum ow_ovsdb_ms_port_state state)
{
    switch (state) {
        case OW_OVSDB_MS_PORT_UNSPEC: return "unspec";
        case OW_OVSDB_MS_PORT_ACTIVE: return "active";
        case OW_OVSDB_MS_PORT_INACTIVE: return "inactive";
    }

    return "";
}

static bool
ow_ovsdb_ms_str_to_bool(const char *port_state)
{
    return strcmp(port_state, "active") == 0;
}

static bool
ow_ovsdb_ms_is_vif(const char *if_type)
{
    return strcmp(if_type, "vif") == 0;
}

static void
ow_ovsdb_ms_gc(struct ow_ovsdb_ms *ms)
{
    if (ms->vif_exists == true) return;
    if (ms->ovsdb_exists == true) return;

    LOGI("ow: ovsdb: ms: %s: freeing", ms->vif_name);

    ev_timer_stop(EV_DEFAULT_ &ms->work);
    ds_tree_remove(&ms->root->tree, ms);
    FREE(ms->vif_name);
    FREE(ms);
}

static void
ow_ovsdb_ms_port_to_schema(struct schema_Wifi_Master_State *row,
                           enum ow_ovsdb_ms_port_state state)
{
    switch (state) {
        case OW_OVSDB_MS_PORT_UNSPEC:
            row->port_state_present = true;
            row->port_state_exists = false;
            break;
        case OW_OVSDB_MS_PORT_ACTIVE:
        case OW_OVSDB_MS_PORT_INACTIVE:
            SCHEMA_SET_STR(row->port_state, ow_ovsdb_ms_port_to_str(state));
            break;
    }
}

static bool
ow_ovsdb_ms_sync(struct ow_ovsdb_ms *ms)
{
    ovsdb_table_t *table = &ms->root->table;

    if (ms->ovsdb_sync == false) return true;
    if (ms->ovsdb_exists == false) return true;

    if (ms->vif_port_state == ms->ovsdb_port_state) {
        if (ms->signal_disconnect == false) return true;
    }

    LOGI("ow: ovsdb: ms: %s: setting from %s to %s",
         ms->vif_name,
         ow_ovsdb_ms_port_to_str(ms->ovsdb_port_state),
         ow_ovsdb_ms_port_to_str(ms->vif_port_state));

    struct schema_Wifi_Master_State state = {0};
    state._partial_update = true;
    SCHEMA_SET_STR(state.if_name, ms->vif_name);
    if (ms->signal_disconnect) {
        LOGI("ow: ovsdb: ms: %s: signalling disconnect through port_state blip",
             ms->vif_name);
        ms->signal_disconnect = false;
        ow_ovsdb_ms_port_to_schema(&state, OW_OVSDB_MS_PORT_INACTIVE);
        ovsdb_table_update(table, &state);
    }

    ow_ovsdb_ms_port_to_schema(&state, ms->vif_port_state);
    int cnt = ovsdb_table_update(table, &state);

    if (cnt == 1)
        return true;

    if (ms->vif_exists == false) {
        /* There won't be a reply, but that's fine. This
         * gives the opportunity to update the state column
         * on best-effort basis for dying breath case.
         */
        ms->vif_port_state = ms->ovsdb_port_state;
        return true;
    }

    LOGI("ow: ovsdb: ms: %s: failed to update to %s, cnt = %d, will retry",
         ms->vif_name, state.port_state, cnt);
    return false;
}

static void
ow_ovsdb_ms_work_cb(EV_P_ ev_timer *arg, int events)
{
    struct ow_ovsdb_ms *ms = container_of(arg, struct ow_ovsdb_ms, work);
    LOGT("ow: ovsdb: ms: %s: working", ms->vif_name);
    if (ow_ovsdb_ms_sync(ms) == false) return;
    ev_timer_stop(EV_A_ &ms->work);
    ow_ovsdb_ms_gc(ms);
}

static void
ow_ovsdb_ms_work_sched(struct ow_ovsdb_ms *ms)
{
    LOGT("ow: ovsdb: ms: %s: scheduling work", ms->vif_name);
    ev_timer_stop(EV_DEFAULT_ &ms->work);
    ev_timer_set(&ms->work, 0, OW_OVSDB_MS_RETRY_SECONDS);
    ev_timer_start(EV_DEFAULT_ &ms->work);
}

static struct ow_ovsdb_ms *
ow_ovsdb_ms_get(struct ow_ovsdb_ms_root *root,
                const char *vif_name)
{
    struct ow_ovsdb_ms *ms = ds_tree_find(&root->tree, vif_name);

    if (ms == NULL) {
        LOGT("ow: ovsdb: ms: %s: allocating", vif_name);
        ms = CALLOC(1, sizeof(*ms));
        ms->vif_name = STRDUP(vif_name);
        ms->root = root;
        ds_tree_insert(&root->tree, ms, ms->vif_name);
        ev_timer_init(&ms->work, ow_ovsdb_ms_work_cb, 0, 0);
    }

    return ms;
}

static void
ow_ovsdb_ms_set_row(struct ow_ovsdb_ms_root *root,
                    const char *vif_name,
                    const struct schema_Wifi_Master_State *row)
{
    struct ow_ovsdb_ms *ms = ow_ovsdb_ms_get(root, vif_name);

    ms->ovsdb_exists = (row != NULL);

    if (row) {
        ms->ovsdb_port_state = ow_ovsdb_ms_str_to_bool(row->port_state);
        ms->ovsdb_sync = ow_ovsdb_ms_is_vif(row->if_type);
    }

    ow_ovsdb_ms_work_sched(ms);

    LOGD("ow: ovsdb: ms: %s: row: exists=%d active=%d sync=%d",
         vif_name,
         ms->ovsdb_exists,
         ms->ovsdb_port_state,
         ms->ovsdb_sync);
}

static void
ow_ovsdb_ms_table_cb(ovsdb_update_monitor_t *mon,
                     struct schema_Wifi_Master_State *old,
                     struct schema_Wifi_Master_State *rec,
                     ovsdb_cache_row_t *row)
{
    ovsdb_table_t *table = mon->mon_data;
    struct ow_ovsdb_ms_root *root = container_of(table, struct ow_ovsdb_ms_root, table);
    const struct schema_Wifi_Master_State *state = (void *)row->record;

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_ms_set_row(root, state->if_name, state);
            break;
        case OVSDB_UPDATE_DEL:
            ow_ovsdb_ms_set_row(root, state->if_name, NULL);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void
ow_ovsdb_ms_dump(struct ow_ovsdb_ms_root *root)
{
    struct ow_ovsdb_ms *ms;

    LOGI("ow: ovsdb: ms: dumping");

    ds_tree_foreach(&root->tree, ms) {
        LOGI("ow: ovsdb: ms: %s: exists=%d/%d active=%d/%d disconnect=%d sync=%d work=%d/%d",
             ms->vif_name,
             ms->vif_exists,
             ms->ovsdb_exists,
             ms->vif_port_state,
             ms->ovsdb_port_state,
             ms->signal_disconnect,
             ms->ovsdb_sync,
             ev_is_active(&ms->work),
             ev_is_pending(&ms->work));
    }
}

static void
ow_ovsdb_ms_sigusr1_cb(EV_P_ ev_signal *arg, int events)
{
    struct ow_ovsdb_ms_root *root = container_of(arg, struct ow_ovsdb_ms_root, sigusr1);
    ow_ovsdb_ms_dump(root);
}

void
ow_ovsdb_ms_init(struct ow_ovsdb_ms_root *root)
{
    ev_signal_init(&root->sigusr1, ow_ovsdb_ms_sigusr1_cb, SIGUSR1);
    ev_signal_start(EV_DEFAULT_ &root->sigusr1);
    ev_unref(EV_DEFAULT);
    ds_tree_init(&root->tree, ds_str_cmp, struct ow_ovsdb_ms, node);
    OVSDB_TABLE_VAR_INIT(&root->table, Wifi_Master_State, if_name);
    ovsdb_cache_monitor(&root->table, (void *)ow_ovsdb_ms_table_cb, true);
}

void
ow_ovsdb_ms_set_vif(struct ow_ovsdb_ms_root *root,
                    const struct osw_state_vif_info *vif)
{
    struct ow_ovsdb_ms *ms = ow_ovsdb_ms_get(root, vif->vif_name);

    ms->vif_exists = vif->drv_state->exists;

    switch (vif->drv_state->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
        case OSW_VIF_AP_VLAN:
            ms->vif_port_state = vif->drv_state->enabled
                               ? OW_OVSDB_MS_PORT_ACTIVE
                               : OW_OVSDB_MS_PORT_INACTIVE;
            break;
        case OSW_VIF_STA:
            ms->vif_port_state = (vif->drv_state->u.sta.link.status == OSW_DRV_VIF_STATE_STA_LINK_CONNECTED)
                               ? OW_OVSDB_MS_PORT_ACTIVE
                               : OW_OVSDB_MS_PORT_INACTIVE;
            break;
    }

    if (ms->vif_exists == false)
        ms->vif_port_state = OW_OVSDB_MS_PORT_INACTIVE;

    ow_ovsdb_ms_work_sched(ms);

    LOGD("ow: ovsdb: ms: %s: vif: exists=%d state=%s",
         ms->vif_name,
         ms->vif_exists,
         ow_ovsdb_ms_port_to_str(ms->vif_port_state));
}

void
ow_ovsdb_ms_set_sta_disconnected(struct ow_ovsdb_ms_root *root,
                                 const struct osw_state_sta_info *sta)
{
    const struct osw_state_vif_info *vif = sta->vif;
    struct ow_ovsdb_ms *ms = ow_ovsdb_ms_get(root, vif->vif_name);

    switch (vif->drv_state->vif_type) {
        case OSW_VIF_STA:
            ms->signal_disconnect = true;
            break;
        case OSW_VIF_UNDEFINED:
        case OSW_VIF_AP:
        case OSW_VIF_AP_VLAN:
            break;
    }

    ow_ovsdb_ms_work_sched(ms);

    LOGD("ow: ovsdb: ms: %s: disconnected",
         ms->vif_name);
}
