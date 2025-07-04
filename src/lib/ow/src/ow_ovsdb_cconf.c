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
#include <ovsdb.h>
#include <ovsdb_table.h>
#include <ovsdb_cache.h>
#include <ovsdb_sync.h>
#include <schema_consts.h>
#include "ow_conf.h"

static ovsdb_table_t table_Wifi_Credential_Config;
static ovsdb_table_t *table_Wifi_Radio_Config;
static ovsdb_table_t *table_Wifi_VIF_Config;
static ev_timer g_ow_ovsdb_cconf_timer;

static void
ow_ovsdb_cconf_apply_on_vif(const struct schema_Wifi_Radio_Config *rconf,
                            const struct schema_Wifi_VIF_Config *vconf)
{
    const char *vif_name = vconf->if_name;

    LOGD("ow: ovsdb: cconf: applying: %s", vif_name);

    ow_conf_vif_flush_sta_net(vif_name);

    int i;
    for (i = 0; i < vconf->credential_configs_len; i++) {
        const char *uuid = vconf->credential_configs[i].uuid;
        ovsdb_cache_row_t *row = ovsdb_cache_find_row_by_uuid(&table_Wifi_Credential_Config, uuid);
        if (row == NULL) continue;

        const struct schema_Wifi_Credential_Config *c = (const void *)row->record;

        /* TODO: there might be potential benefit to propagate enablement flag
         * to put it in wpa supplicant network block.
         * It might allow dynamic (re-)enable, disable network for recovery mechanisms
         * For now we disabled network will be skipped in defining network blocks */
        if (!c->enabled) continue;

        const int priority = c->priority_exists ? c->priority : 0;
        const bool multi_ap = (strcmp(c->onboard_type, SCHEMA_CONSTS_ONBOARD_TYPE_MULTI_AP) == 0);
        struct osw_ifname bridge;
        MEMZERO(bridge);
        if (multi_ap) STRSCPY_WARN(bridge.buf, CONFIG_TARGET_LAN_BRIDGE_NAME);
        struct osw_wpa wpa;
        MEMZERO(wpa);
        struct osw_psk psk;
        MEMZERO(psk);
        struct osw_ssid ssid;
        MEMZERO(ssid);
        const char *pass = SCHEMA_KEY_VAL(c->security, "key");

        struct osw_hwaddr bssid;
        MEMZERO(bssid);
        if (osw_hwaddr_from_cstr(c->bssid, &bssid) != true) {
            /* bssid is not specified or malformed setting to "any" */
            MEMZERO(bssid);
        }

        /* FIXME: Wifi_Credential_Config doesn't support new
         * style wpa_key_mgmt etc.  columns like
         * Wifi_VIF_Config does now. It was always implied
         * that Wifi_Credential_Config entries are
         * wpa2-only, hence hardocding it for the time being
         * until Wifi_Credential_Config gets a revamp to
         * allow expressing, eg. SAE, or DPP.
         */
        wpa.rsn = true;
        wpa.pairwise_ccmp = true;
        wpa.akm_psk = true;

        /* Technically there's nothing wrong in allowing
         * WPA3-Transition mode. This actually makes it
         * possible for 6GHz to be used during onboarding.
         * 6GHz requires SAE and PMF.
         */
        wpa.akm_sae = true;
        wpa.pmf = OSW_PMF_OPTIONAL;

        /* EHT (and MLO) requires a different SAE AKM and
         * additional ciphers.
         */
        if (strcmp(rconf->hw_mode, "11be") == 0) {
            wpa.akm_sae_ext = true;
            wpa.pairwise_gcmp = true;
            wpa.pairwise_gcmp256 = true;
            wpa.beacon_protection = true;
        }

        STRSCPY_WARN(psk.str, pass);
        STRSCPY_WARN(ssid.buf, c->ssid);
        ssid.len = strlen(ssid.buf);

        ow_conf_vif_set_sta_net(vif_name, &ssid, &bssid, &psk, &wpa, multi_ap ? &bridge : NULL, &multi_ap, &priority);
    }
}

static const struct schema_Wifi_Radio_Config *
ow_ovsdb_cconf_rconf_for_vconf(struct ds_tree *r_rows,
                               const struct schema_Wifi_VIF_Config *v)
{
    ovsdb_cache_row_t *i;
    ds_tree_foreach(r_rows, i) {
        const struct schema_Wifi_Radio_Config *r = (const void *)i->record;
        int i;
        for (i = 0; i < r->vif_configs_len; i++) {
            if (strcmp(r->vif_configs[i].uuid, v->_uuid.uuid) == 0) {
                return r;
            }
        }
    }
    return NULL;
}

static void
ow_ovsdb_cconf_apply(struct ds_tree *r_rows, struct ds_tree *v_rows)
{
    LOGD("ow: ovsdb: cconf: applying");

    ovsdb_cache_row_t *i;
    ds_tree_foreach(v_rows, i) {
        const struct schema_Wifi_VIF_Config *v = (const void *)i->record;
        const bool is_sta = (strcmp(v->mode, "sta") == 0);
        const bool has_ssid = strlen(v->ssid) > 0;

        if (v->enabled == false) continue;
        if (is_sta == false) continue;
        if (has_ssid == true) continue;

        const struct schema_Wifi_Radio_Config *r = ow_ovsdb_cconf_rconf_for_vconf(r_rows, v);
        if (WARN_ON(r == NULL)) continue;

        ow_ovsdb_cconf_apply_on_vif(r, v);
    }
}

static void
ow_ovsdb_cconf_timer_cb(EV_P_ ev_timer *arg, int events)
{
    struct ds_tree *r_rows = &table_Wifi_Radio_Config->rows;
    struct ds_tree *v_rows = &table_Wifi_VIF_Config->rows;
    ow_ovsdb_cconf_apply(r_rows, v_rows);
}

void
ow_ovsdb_cconf_sched(void)
{
    struct ev_timer *t = &g_ow_ovsdb_cconf_timer;
    ev_timer_stop(EV_DEFAULT_ t);
    ev_timer_set(t, 0, 0);
    ev_timer_start(EV_DEFAULT_ t);
    LOGT("ow: ovsdb: cconf: scheduled");
}

static void
callback_Wifi_Credential_Config(ovsdb_update_monitor_t *mon,
                                struct schema_Wifi_Credential_Config *old,
                                struct schema_Wifi_Credential_Config *cconf,
                                ovsdb_cache_row_t *row)
{
    ow_ovsdb_cconf_sched();
}

void
ow_ovsdb_cconf_init(ovsdb_table_t *rconft, ovsdb_table_t *vconft)
{
    struct ev_timer *t = &g_ow_ovsdb_cconf_timer;
    ev_timer_init(t, ow_ovsdb_cconf_timer_cb, 0, 0);
    table_Wifi_Radio_Config = rconft;
    table_Wifi_VIF_Config = vconft;
    OVSDB_TABLE_INIT(Wifi_Credential_Config, _uuid);
    OVSDB_CACHE_MONITOR(Wifi_Credential_Config, true);
}

bool
ow_ovsdb_cconf_use_vconf(const struct schema_Wifi_VIF_Config *vconf)
{
    return strlen(vconf->ssid) > 0;
}
