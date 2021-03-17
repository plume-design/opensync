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

#include <ev.h>
#include <net/if.h>
#include <linux/un.h>
#include <linux/limits.h>
#include <netinet/if_ether.h>
#include <stdbool.h>

#include "opensync-ctrl.h"
#include "opensync-hapd.h"
#include "module.h"
#include "ovsdb.h"
#include "ovsdb_update.h"
#include "ovsdb_table.h"
#include "sm.h"

#define CLIENT_SIZE_LIMIT CONFIG_SM_BACKEND_HAPD_CLIENTS_AUTH_FAILS_PER_VAP_LIMIT

struct sm_hapd_bss
{
    radio_type_t radio_type;
    ovs_uuid_t uuid;
    struct hapd *hapd;
    sm_client_auth_fails_bss_t report;

    ds_tree_node_t node;
};

struct sm_hapd_band
{
    radio_type_t radio_type;
    ev_timer report_timer;

    ds_dlist_node_t node;
};

typedef void (*vif_operation_f) (const struct schema_Wifi_Radio_State *rstate,
                                 const struct schema_Wifi_VIF_State *vstate,
                                 const sm_stats_request_t *request);

static const char backend_name[] = "hapd";
static ds_dlist_t bands = DS_DLIST_INIT(struct sm_hapd_band, node);
static ds_tree_t bsses = DS_TREE_INIT(ds_str_cmp, struct sm_hapd_bss, node); /*BSSes */
static ovsdb_table_t table_Wifi_Radio_State;
static ovsdb_table_t table_Wifi_VIF_State;
static ovsdb_table_t table_Wifi_Stats_Config;

static struct sm_hapd_bss *
sm_hapd_bss_create(const struct schema_Wifi_Radio_State *rstate,
                   const struct schema_Wifi_VIF_State *vstate);

static void
sm_hapd_bss_appeared(const struct schema_Wifi_Radio_State *rstate,
                     const ovs_uuid_t *vstate_uuid);

static void
sm_hapd_bss_vanished(struct sm_hapd_bss *bss);

static void
sm_hapd_bss_free(struct sm_hapd_bss *bss);

static void
sm_hapd_bss_clear_clients(struct sm_hapd_bss *bss);

static sm_client_auth_fails_client_t *
sm_hapd_bss_lookup_client(struct sm_hapd_bss *bss,
                          const char *mac);

static void
sm_hapd_hostapd_wpa_key_mismatch(struct hapd *hapd,
                                 const char *mac);

static void
sm_hapd_band_report_timer_cb(EV_P_ ev_timer *timer,
                             int events);

static struct sm_hapd_band *
sm_hapd_band_find(radio_type_t radio_type);

static struct sm_hapd_band *
sm_hapd_band_create(const sm_stats_request_t *request);

static void
sm_hapd_band_free(struct sm_hapd_band *band);

static bool
sm_hapd_lookup_vif_state(const ovs_uuid_t *uuid,
                         struct schema_Wifi_VIF_State *vstate);

static void
sm_hapd_handle_wifi_radio_state_update(const struct schema_Wifi_Radio_State *rstate);

static void
callback_Wifi_Radio_State(ovsdb_update_monitor_t *mon,
                          struct schema_Wifi_Radio_State *old_rstate,
                          struct schema_Wifi_Radio_State *rstate);

static void
sm_hapd_request_start(sm_report_type_t report_type,
                      const sm_stats_request_t *request);

static void
sm_hapd_request_update(sm_report_type_t report_type,
                       const sm_stats_request_t *request);
static void
sm_hapd_request_stop(sm_report_type_t report_type,
                     const sm_stats_request_t *request);

static struct sm_hapd_bss *
sm_hapd_bss_create(const struct schema_Wifi_Radio_State *rstate,
                   const struct schema_Wifi_VIF_State *vstate)
{
    struct sm_hapd_bss *bss = NULL;
    struct hapd *hapd = NULL;

    bss = calloc(1, sizeof(struct sm_hapd_bss));
    if (!bss) {
        LOGD("%s: Failed to alloc context for client auth fails reporting for if_name: %s",
             backend_name, vstate->if_name);
        return NULL;
    }

    STRSCPY_WARN(bss->report.if_name, vstate->if_name);
    memcpy(&bss->uuid, &vstate->_uuid, sizeof(bss->uuid));

    hapd = hapd_new(rstate->if_name, vstate->if_name);
    if (!hapd) {
        LOGD("%s: Failed to alloc hapd for client auth fails reporting for if_name: %s",
             backend_name, vstate->if_name);
        goto release_bss;
    }

    hapd->wpa_key_mismatch = sm_hapd_hostapd_wpa_key_mismatch;

    bss->radio_type = radio_get_type_from_name((char *)rstate->freq_band);
    bss->hapd = hapd;

    /*
     * ctrl_enable() will eventually connect to hostapd once WM will prepare
     * everything.
     */
    (void) ctrl_enable(&bss->hapd->ctrl);

    return bss;

release_bss:
    sm_hapd_bss_free(bss);
    return NULL;
}

static void
sm_hapd_bss_appeared(const struct schema_Wifi_Radio_State *rstate,
                     const ovs_uuid_t *vstate_uuid)
{
    struct schema_Wifi_VIF_State vstate;
    struct sm_hapd_bss *bss = NULL;
    bool ok;

    ok = sm_hapd_lookup_vif_state(vstate_uuid, &vstate);
    if (!ok)
        goto err;

    bss = sm_hapd_bss_create(rstate, &vstate);
    if (!bss)
        goto err;

    ds_tree_insert(&bsses, bss, bss->report.if_name);

    LOGI("%s: Client auth fails reporting started on if_name: %s (%s)", backend_name,
         bss->report.if_name, radio_get_name_from_type(bss->radio_type));

    return;

err:
    LOGW("%s: Failed to start client auth fails reporting on newly created VIF on radio: %s",
         backend_name, rstate->if_name);
    sm_hapd_bss_free(bss);
}

static void
sm_hapd_bss_vanished(struct sm_hapd_bss *bss)
{
    LOGI("%s: Client auth fails reporting stopped on if_name: %s", backend_name,
         bss->report.if_name);

    sm_hapd_bss_free(bss);
}

static void
sm_hapd_bss_free(struct sm_hapd_bss *bss)
{
    if (!bss)
        return;
    if (bss->hapd)
        ctrl_disable(&bss->hapd->ctrl);

    free(bss->report.clients);
    free(bss);
}

static void
sm_hapd_bss_clear_clients(struct sm_hapd_bss *bss)
{
    free(bss->report.clients);
    bss->report.clients = NULL;
    bss->report.clients_len = 0;
}

static sm_client_auth_fails_client_t *
sm_hapd_bss_lookup_client(struct sm_hapd_bss *bss,
                          const char *mac)
{
    sm_client_auth_fails_client_t *client;
    size_t new_size;
    size_t i;

    for (i = 0; i < bss->report.clients_len; i++) {
        if (strcmp(bss->report.clients[i].mac, mac) == 0)
            return &bss->report.clients[i];
    }

    new_size = bss->report.clients_len + 1;
    if (new_size > CLIENT_SIZE_LIMIT) {
        LOGD("%s: Reached clients' counters array limit (%d) for client auth fails on if_name: %s",
             backend_name, CLIENT_SIZE_LIMIT, bss->report.if_name);
        return NULL;
    }

    bss->report.clients = (sm_client_auth_fails_client_t *)realloc(bss->report.clients,
                                                                   new_size * sizeof(*bss->report.clients));
    if (!bss->report.clients) {
        LOGD("%s: Failed to alloc clients array for client auth fails reporting on if_name: %s",
             backend_name, bss->report.if_name);

        sm_hapd_bss_clear_clients(bss);
        return NULL;
    }

    client = &bss->report.clients[bss->report.clients_len];
    memset(client, 0, sizeof(*client));
    STRSCPY_WARN(client->mac, mac);

    bss->report.clients_len = new_size;

    return client;

}

static void
sm_hapd_hostapd_wpa_key_mismatch(struct hapd *hapd,
                                 const char *mac)
{
    sm_client_auth_fails_client_t *client;
    struct sm_hapd_bss *bss;

    bss = ds_tree_find(&bsses, (char *)hapd->ctrl.bss);
    if (!bss)
        return;

    client = sm_hapd_bss_lookup_client(bss, mac);
    if (!client) {
        LOGD("%s: sta: "MAC_ADDRESS_FORMAT" key mismatch (ignored)", backend_name,
             MAC_ADDRESS_PRINT(mac));
        return;
    }

    client->invalid_psk++;

    LOGD("%s: sta: %s auth failed: key mismatch", backend_name, mac);
}

static void
sm_hapd_band_report_timer_cb(EV_P_ ev_timer *timer,
                             int events)
{
    sm_client_auth_fails_report_t report;
    struct sm_hapd_band *band;
    struct sm_hapd_bss *bss;
    size_t i;

    band = CONTAINER_OF(timer, struct sm_hapd_band, report_timer);
    memset(&report, 0, sizeof(report));

    report.radio_type = band->radio_type;

    ds_tree_foreach(&bsses, bss) {
        sm_client_auth_fails_bss_t *report_bss;
        size_t bsses_size;

        LOGD("%s: Reporting client auth fails on if_name: %s (%s) clients_len: %d",
             backend_name, bss->report.if_name, radio_get_name_from_type(bss->radio_type),
             bss->report.clients_len);

        if (bss->radio_type != band->radio_type)
            continue;

        if (bss->report.clients_len == 0)
            continue;

        bsses_size = (report.bsses_len + 1) * sizeof(*report.bsses);
        report.bsses = (sm_client_auth_fails_bss_t *)realloc(report.bsses, bsses_size);
        if (!report.bsses) {
            LOGD("%s: Skipping client auth fails report on if_name: %s (%s) clients_len: %d due to memory alloc failure",
                 backend_name, bss->report.if_name, radio_get_name_from_type(bss->radio_type),
                 bss->report.clients_len);
            continue;
        }

        report_bss = &report.bsses[report.bsses_len];
        memcpy(report_bss, &bss->report, sizeof(*report_bss));

        report.bsses_len++;
    }

    if (report.bsses_len == 0)
        return;

    sm_client_auth_fails_report(&report);

    for (i = 0; i < report.bsses_len; i++) {
        struct sm_hapd_bss *bss = ds_tree_find(&bsses, (char *)report.bsses[i].if_name);
        if (!bss)
            return;

        sm_hapd_bss_clear_clients(bss);
    }

    free(report.bsses);
}

static struct sm_hapd_band *
sm_hapd_band_find(radio_type_t radio_type)
{
    struct sm_hapd_band *band = NULL;
    ds_dlist_foreach(&bands, band) {
        if (band->radio_type == radio_type)
            break;
    }
    return band;
}

static struct sm_hapd_band *
sm_hapd_band_create(const sm_stats_request_t *request)
{
    struct sm_hapd_band *band;

    band = calloc(1, sizeof(struct sm_hapd_band));
    if (!band) {
        LOGD("%s: Failed to alloc context client auth fails reporting on %s band",
             backend_name, radio_get_name_from_type(request->radio_type));
        return NULL;
    }

    band->radio_type = request->radio_type;
    ev_timer_init(&band->report_timer, sm_hapd_band_report_timer_cb,
                  request->reporting_interval, request->reporting_interval);
    ev_timer_start(EV_DEFAULT_ &band->report_timer);

    return band;
}

static void
sm_hapd_band_free(struct sm_hapd_band *band)
{
    ev_timer_stop(EV_DEFAULT_ &band->report_timer);
    free(band);
}

static bool
sm_hapd_lookup_vif_state(const ovs_uuid_t *uuid,
                         struct schema_Wifi_VIF_State *vstate)
{
    json_t *where;

    where = ovsdb_tran_cond(OCLM_UUID, "_uuid", OFUNC_EQ, uuid->uuid);
    return ovsdb_table_select_one_where(&table_Wifi_VIF_State, where, vstate);
}

static void
sm_hapd_handle_wifi_radio_state_update(const struct schema_Wifi_Radio_State *rstate)
{
    struct sm_hapd_bss *bss;
    ds_tree_iter_t iter;
    int i;

    /* Look for new VAPs */
    for (i = 0; i < rstate->vif_states_len; i++) {
        ds_tree_foreach(&bsses, bss) {
            if (strcmp(rstate->vif_states[i].uuid, bss->uuid.uuid) == 0)
                break;
        }

        if (bss)
            continue;

        sm_hapd_bss_appeared(rstate, &rstate->vif_states[i]);
    }

    /* Look up for deleted VAPs */
    ds_tree_foreach_iter(&bsses, bss, &iter) {
        bool bss_is_present = false;
        for (i = 0; i < rstate->vif_states_len; i++) {
            if (strcmp(rstate->vif_states[i].uuid, bss->uuid.uuid) == 0) {
                bss_is_present = true;
                break;
            }
        }

        if (bss_is_present)
            continue;

        ds_tree_iremove(&iter);
        sm_hapd_bss_vanished(bss);
    }
}

static void
callback_Wifi_Radio_State(ovsdb_update_monitor_t *mon,
                          struct schema_Wifi_Radio_State *old_rstate,
                          struct schema_Wifi_Radio_State *new_rstate)
{
    switch(mon->mon_type)
    {
        case OVSDB_UPDATE_MODIFY:
            sm_hapd_handle_wifi_radio_state_update(new_rstate);
            return;
        case OVSDB_UPDATE_DEL:
        case OVSDB_UPDATE_ERROR:
        case OVSDB_UPDATE_NEW:
            /* nop */
            return;
    }
}

static void
sm_hapd_request_start(sm_report_type_t report_type,
                      const sm_stats_request_t *request)
{
    struct schema_Wifi_Radio_State rstate;
    const char *radio_type_str;
    struct sm_hapd_band *band;
    json_t *where;
    int ret;
    int i;

    if (report_type != STS_REPORT_CLIENT_AUTH_FAILS)
        return;

    radio_type_str = radio_get_name_from_type(request->radio_type);
    where = ovsdb_tran_cond(OCLM_STR, SCHEMA_COLUMN(Wifi_Radio_State, freq_band),
                            OFUNC_EQ, radio_type_str);
    ret = ovsdb_table_select_one_where(&table_Wifi_Radio_State, where, &rstate);
    if (!ret) {
        LOGD("%s: Failed to find %s radio in Wifi_Radio_State",
             backend_name, radio_type_str);
        goto err;
    }

    for (i = 0; i < rstate.vif_states_len; i++) {
        struct schema_Wifi_VIF_State vstate;
        struct sm_hapd_bss *bss;

        where = ovsdb_tran_cond(OCLM_UUID, "_uuid", OFUNC_EQ, &rstate.vif_states[i]);
        ret = ovsdb_table_select_one_where(&table_Wifi_VIF_State, where, &vstate);
        if (!ret) {
            LOGD("%s: Failed to lookup %s VIF from Wifi_VIF_State with uuid: %s",
                 backend_name, radio_type_str, rstate.vif_states[i].uuid);
             continue;
        }

        bss = sm_hapd_bss_create(&rstate, &vstate);
        if (!bss)
            continue;

        ds_tree_insert(&bsses, bss, bss->report.if_name);

        LOGI("%s: Client auth fails reporting started on if_name: %s (%s)", backend_name,
             bss->report.if_name, radio_get_name_from_type(bss->radio_type));
    }

    band = sm_hapd_band_create(request);
    if (!band)
        goto err;

    ds_dlist_insert_head(&bands, band);
    return;

err:
    LOGW("%s: Failed to start reporting client auth fails on %s band", backend_name,
         radio_type_str);
}

static void
sm_hapd_request_update(sm_report_type_t report_type,
                       const sm_stats_request_t *request)
{
    struct sm_hapd_band *band;

    if (report_type != STS_REPORT_CLIENT_AUTH_FAILS)
        return;

    band = sm_hapd_band_find(request->radio_type);
    if (!band)
    {
        LOGW("%s: Failed to update reporting client auth fails on %s band (band not present)",
             backend_name, radio_get_name_from_type(request->radio_type));
        return;
    }

    band->report_timer.repeat = request->reporting_interval;
}

static void
sm_hapd_request_stop(sm_report_type_t report_type,
                     const sm_stats_request_t *request)
{
    ds_tree_iter_t iter;
    struct sm_hapd_band *band;
    struct sm_hapd_bss *bss;

    if (report_type != STS_REPORT_CLIENT_AUTH_FAILS)
        return;

    band = sm_hapd_band_find(request->radio_type);
    if (!band)
    {
        LOGW("%s: Failed to stop reporting client auth fails on %s band (band not present)",
             backend_name, radio_get_name_from_type(request->radio_type));
        return;
    }

    ds_dlist_remove(&bands, band);
    sm_hapd_band_free(band);

    ds_tree_foreach_iter(&bsses, bss, &iter) {
        if (bss->radio_type != request->radio_type)
            continue;

        LOGI("%s: Client auth fails reporting stopped on if_name: %s", backend_name,
             bss->report.if_name);

        ds_tree_iremove(&iter);
        sm_hapd_bss_free(bss);
    }
}

void
sm_hapd_start(void *data)
{
    static const sm_backend_funcs_t g_sm_hapd_ops = {
        .start = sm_hapd_request_start,
        .update = sm_hapd_request_update,
        .stop = sm_hapd_request_stop,
    };

    OVSDB_TABLE_INIT_NO_KEY(Wifi_Stats_Config);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_Radio_State);
    OVSDB_TABLE_INIT_NO_KEY(Wifi_VIF_State);

    OVSDB_TABLE_MONITOR_F(Wifi_Radio_State, C_VPACK("if_name", "freq_band", "vif_states"));

    sm_backend_register(backend_name, &g_sm_hapd_ops);
}

void
sm_hapd_stop(void *data)
{
    sm_backend_unregister(backend_name);
}

MODULE(sm_hapd, sm_hapd_start, sm_hapd_stop)
