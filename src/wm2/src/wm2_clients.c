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

#define _GNU_SOURCE

#include <stdlib.h>
#include <sys/wait.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <inttypes.h>
#include <jansson.h>
#include <ctype.h>

#include "json_util.h"
#include "ds_list.h"
#include "ds_dlist.h"
#include "schema.h"
#include "log.h"
#include "target.h"
#include "wm2.h"
#include "wm2_dpp.h"
#include "ovsdb.h"
#include "ovsdb_sync.h"
#include "ovsdb_table.h"
#include "memutil.h"
#include "kconfig.h"

// Defines
#define MODULE_ID LOG_MODULE_ID_MAIN

// OVSDB constants
#define OVSDB_CLIENTS_TABLE                 "Wifi_Associated_Clients"
#define OVSDB_CLIENTS_PARENT                "Wifi_VIF_State"
#define OVSDB_OPENFLOW_TAG_TABLE            "Openflow_Tag"

#define OVSDB_CLIENTS_PARENT_COL            "associated_clients"

#define WM2_CLIENT_RETRY_SECONDS 3

struct wm2_client {
    struct ds_dlist_node list;
    struct ds_dlist vif_list;
    struct schema_Wifi_Associated_Clients ovsdb;
    char *mac_str;
    char *oftag_in_ovsdb;
    ev_timer retry;
    ev_async async;
};

struct wm2_client_vif {
    struct ds_dlist_node list;
    struct wm2_client *c;
    char *if_name;
    char *wpa_key_id;
    char *dpp_pk_hash;
    char *wpa_key_mgmt;
    char *pairwise_cipher;
    char *oftag;
    bool connected;
    bool in_ovsdb;
    bool print_when_removed;
    bool pmf;
};

enum wm2_client_transition {
    WM2_CLIENT_CONNECTED,
    WM2_CLIENT_DISCONNECTED,
    WM2_CLIENT_ROAMED,
    WM2_CLIENT_CHANGED,
    WM2_CLIENT_NO_OP,
};

static struct ds_dlist g_wm2_clients = DS_DLIST_INIT(struct wm2_client, list);

/******************************************************************************
 *  PRIVATE definitions
 *****************************************************************************/
static int
wm2_clients_oftag_from_key_id(const char *cloud_vif_ifname,
                              const char *key_id,
                              char *oftag,
                              int len)
{
    struct schema_Wifi_VIF_Config vconf;
    ovsdb_table_t table_Wifi_VIF_Config;
    bool ok;

    OVSDB_TABLE_INIT(Wifi_VIF_Config, if_name);
    ok = ovsdb_table_select_one(&table_Wifi_VIF_Config,
                                SCHEMA_COLUMN(Wifi_VIF_Config, if_name),
                                cloud_vif_ifname,
                                &vconf);
    if (!ok) {
        LOGW("%s: failed to lookup in table", cloud_vif_ifname);
        return -1;
    }

    if (vconf.security_len > 0) {
        /* Legacy impl based on deprecated Wifi_VIF_Config:security */
        char oftagkey[32];
        char *ptr;

        if (strlen(SCHEMA_KEY_VAL(vconf.security, "oftag")) == 0) {
            LOGD("%s: no main oftag found, assuming backhaul/non-home interface, ignoring",
                 cloud_vif_ifname);
            return 0;
        }

        if (strstr(key_id, "key-") == key_id)
            snprintf(oftagkey, sizeof(oftagkey), "oftag-%s", key_id);
        else
            snprintf(oftagkey, sizeof(oftagkey), "oftag");

        ptr = SCHEMA_KEY_VAL(vconf.security, oftagkey);
        if (!ptr || strlen(ptr) == 0)
            return -1;

        snprintf(oftag, len, "%s", ptr);
        return 0;
    }
    else {
        const char *ptr;

        if (vconf.wpa_oftags_len == 0 && !vconf.default_oftag_exists) {
            LOGD("%s: no main oftag found, assuming backhaul/non-home interface, ignoring",
                 cloud_vif_ifname);
            return 0;
        }

        ptr = SCHEMA_KEY_VAL(vconf.wpa_oftags, key_id);
        if (ptr && strlen(ptr) > 0) {
            snprintf(oftag, len, "%s", ptr);
            return 0;
        }

        if (vconf.default_oftag_exists && strlen(vconf.default_oftag) > 0) {
            snprintf(oftag, len, "%s", vconf.default_oftag);
            return 0;
        }

        LOGD("%s: Neither wpa_oftags or default_oftag contains oftag for keyid: '%s'",
             cloud_vif_ifname, key_id);

        return 0;
    }
}

int
wm2_clients_oftag_set(const char *mac,
                      const char *oftag)
{
    json_t *result;
    json_t *where;
    json_t *rows;
    json_t *row;
    int cnt;

    if (strlen(oftag) == 0) {
        LOGD("%s: oftag is empty, expect openflow/firewall issues", mac);
        return 0;
    }

    LOGD("%s: setting oftag='%s'", mac, oftag);

    where = ovsdb_where_simple("name", oftag);
    if (!where) {
        LOGW("%s: failed to allocate ovsdb condition, oom?", mac);
        return -1;
    }

    row = ovsdb_mutation("device_value",
                         json_string("insert"),
                         json_string(mac));
    if (!row) {
        LOGW("%s: failed to allocate ovsdb mutation, oom?", mac);
        json_decref(where);
        return -1;
    }

    rows = json_array();
    if (!rows) {
        LOGW("%s: failed to allocate ovsdb mutation list, oom?", mac);
        json_decref(where);
        json_decref(row);
        return -1;
    }

    json_array_append_new(rows, row);

    result = ovsdb_tran_call_s(OVSDB_OPENFLOW_TAG_TABLE,
                               OTR_MUTATE,
                               where,
                               rows);
    if (!result) {
        LOGW("%s: failed to execute ovsdb transact", mac);
        return -1;
    }

    cnt = ovsdb_get_update_result_count(result,
                                        OVSDB_OPENFLOW_TAG_TABLE,
                                        "mutate");

    return cnt;
}

int
wm2_clients_oftag_unset(const char *mac,
                        const char *oftag)
{
    json_t *result;
    json_t *where;
    json_t *rows;
    json_t *row;
    int cnt;

    LOGD("%s: removing oftag='%s'", mac, oftag);

    where = ovsdb_tran_cond(OCLM_STR,
                            SCHEMA_COLUMN(Openflow_Tag, name),
                            OFUNC_EQ,
                            oftag);
    if (!where) {
        LOGW("%s: failed to allocate ovsdb condition, oom?", mac);
        return -1;
    }

    row = ovsdb_mutation("device_value",
                         json_string("delete"),
                         json_string(mac));
    if (!row) {
        LOGW("%s: failed to allocate ovsdb mutation, oom?", mac);
        json_decref(where);
        return -1;
    }

    rows = json_array();
    if (!rows) {
        LOGW("%s: failed to allocate ovsdb mutation list, oom?", mac);
        json_decref(where);
        json_decref(row);
        return -1;
    }

    json_array_append_new(rows, row);

    result = ovsdb_tran_call_s(OVSDB_OPENFLOW_TAG_TABLE,
                               OTR_MUTATE,
                               where,
                               rows);
    if (!result) {
        LOGW("%s: failed to execute ovsdb transact", mac);
        return -1;
    }

    cnt = ovsdb_get_update_result_count(result,
                                        OVSDB_OPENFLOW_TAG_TABLE,
                                        "mutate");

    return cnt;
}

static void
wm2_clients_isolate(const char *ifname, const char *sta, bool connected)
{
    struct schema_Wifi_VIF_Config vconf;
    const char *p;
    char sta_ifname[16];
    char path[256];
    char cmd[1024];
    int err;
    bool ok;

    snprintf(path, sizeof(path), "/.devmode.softwds.%s", ifname);
    if (access(path, R_OK))
        return;

    snprintf(path, sizeof(path), "/sys/module/softwds");
    if (access(path, R_OK)) {
        LOGW("%s: %s: isolate: softwds is missing", ifname, sta);
        return;
    }

    memset(sta_ifname, 0, sizeof(sta_ifname));
    STRSCPY(sta_ifname, "sta");
    for (p = sta; *p && strlen(sta_ifname) < sizeof(sta_ifname); p++)
        if (*p != ':')
            sta_ifname[strlen(sta_ifname)] = tolower(*p);

    if (strlen(sta_ifname) != (3 + 12)) {
        LOGW("%s: %s: isolate: failed to derive interface name: '%s'",
             ifname, sta, sta_ifname);
        return;
    }

    if (connected) {
        ok = ovsdb_table_select_one(&table_Wifi_VIF_Config,
                                    SCHEMA_COLUMN(Wifi_VIF_Config, if_name),
                                    ifname,
                                    &vconf);
        if (!ok) {
            LOGW("%s: %s: isolate: failed to get vconf", ifname, sta);
            return;
        }

        if (!vconf.bridge_exists || !strlen(vconf.bridge)) {
            LOGW("%s: %s: isolate: no bridge", ifname, sta);
            return;
        }

        if(kconfig_enabled(CONFIG_TARGET_USE_NATIVE_BRIDGE))
        {
            snprintf(cmd, sizeof(cmd), "brctl delif %s ;"
                                       "ip link add link %s name %s type softwds &&"
                                       "echo %s > /sys/class/net/%s/softwds/addr &&"
                                       "echo N > /sys/class/net/%s/softwds/wrap &&"
                                       "brctl addif %s %s &&"
                                       "ip link set %s up",
                                       ifname,
                                       ifname, sta_ifname,
                                       sta, sta_ifname,
                                       sta_ifname,
                                       vconf.bridge, sta_ifname,
                                       sta_ifname);
        }
        else
        {
            snprintf(cmd, sizeof(cmd), "ovs-vsctl del-port %s ;"
                                       "ip link add link %s name %s type softwds &&"
                                       "echo %s > /sys/class/net/%s/softwds/addr &&"
                                       "echo N > /sys/class/net/%s/softwds/wrap &&"
                                       "ovs-vsctl add-port %s %s &&"
                                       "ip link set %s up",
                                       ifname,
                                       ifname, sta_ifname,
                                       sta, sta_ifname,
                                       sta_ifname,
                                       vconf.bridge, sta_ifname,
                                       sta_ifname);
        }
        err = system(cmd);
        LOGI("%s: %s: isolating into '%s': %d (errno: %d)", ifname, sta, sta_ifname, err, errno);
    } else {
        if (kconfig_enabled(CONFIG_TARGET_USE_NATIVE_BRIDGE))
        {
            snprintf(cmd, sizeof(cmd), "brctl delif %s ;"
                                       "ip link del %s",
                                       sta_ifname,
                                       sta_ifname);
        }
        else
        {
            snprintf(cmd, sizeof(cmd), "ovs-vsctl del-port %s ;"
                                       "ip link del %s",
                                       sta_ifname,
                                       sta_ifname);
        }
        err = system(cmd);
        LOGI("%s: %s: cleaning up isolation of '%s': %d (errno: %d)", ifname, sta, sta_ifname, err, errno);
    }
}

static bool
wm2_clients_get_default_oftag(const char *ifname, char *oftag, int size)
{
    struct schema_Wifi_VIF_Config vconf;
    const char *column;
    bool ok;

    column = SCHEMA_COLUMN(Wifi_VIF_Config, if_name);
    ok = ovsdb_table_select_one(&table_Wifi_VIF_Config, column, ifname, &vconf);
    if (!ok) return false;
    if (!vconf.default_oftag_exists) return false;

    strscpy(oftag, vconf.default_oftag, size);
    return true;
}

static bool
wm2_client_is_connected(struct wm2_client *c)
{
    struct wm2_client_vif *v;

    ds_dlist_foreach(&c->vif_list, v)
        if (v->connected)
            return true;

    return false;
}

static bool
wm2_client_ovsdb_is_connected(struct wm2_client *c)
{
    return strlen(c->ovsdb._uuid.uuid) > 0 ? true : false;
}

static struct wm2_client_vif *
wm2_client_get_connected_vif_old(struct wm2_client *c)
{
    struct wm2_client_vif *v;

    ds_dlist_foreach(&c->vif_list, v)
        if (v->in_ovsdb == true) return v;

    return NULL;
}

static struct wm2_client_vif *
wm2_client_get_connected_vif_new(struct wm2_client *c)
{
    struct wm2_client_vif *v = ds_dlist_head(&c->vif_list);
    if (v->connected == true) return v;
    else return NULL;
}

static bool
wm2_client_ovsdb_is_synced(struct wm2_client *c)
{
    struct wm2_client_vif *v = wm2_client_get_connected_vif_new(c);
    const bool has_ovsdb = wm2_client_ovsdb_is_connected(c);
    const bool want_ovsdb = wm2_client_is_connected(c);

    if (has_ovsdb != want_ovsdb) return false;
    if (strcmp(v ? (v->wpa_key_id ?: "") : "", c->ovsdb.key_id) != 0) return false;
    if (strcmp(v ? (v->dpp_pk_hash ?: "") : "", c->ovsdb.dpp_netaccesskey_sha256_hex) != 0) return false;
    if (strcmp(v ? (v->wpa_key_mgmt ?: "") : "", c->ovsdb.wpa_key_mgmt) != 0) return false;
    if (strcmp(v ? (v->pairwise_cipher ?: "") : "", c->ovsdb.pairwise_cipher) != 0) return false;
    if (strcmp(v ? (v->oftag ?: "") : "", c->ovsdb.oftag) != 0) return false;
    if (strcmp(v ? (v->oftag ?: "") : "", c->oftag_in_ovsdb ?: "") != 0) return false;
    if (!v || v->pmf != c->ovsdb.pmf) return false;

    return true;
}

static enum wm2_client_transition
wm2_client_get_transition(struct wm2_client *c)
{
    struct wm2_client_vif *v_old = wm2_client_get_connected_vif_old(c);
    struct wm2_client_vif *v_new = wm2_client_get_connected_vif_new(c);
    const bool ovsdb_connected = wm2_client_ovsdb_is_connected(c);
    const bool client_connected = wm2_client_is_connected(c);

    if (ovsdb_connected == false && client_connected == true)
        return WM2_CLIENT_CONNECTED;
    if (ovsdb_connected == true && client_connected == false)
        return WM2_CLIENT_DISCONNECTED;
    if (ovsdb_connected == false && client_connected == false)
        return WM2_CLIENT_NO_OP;
    if (v_old != NULL && v_new != NULL && v_old != v_new)
        return WM2_CLIENT_ROAMED;
    if (wm2_client_ovsdb_is_synced(c) == false)
        return WM2_CLIENT_CHANGED;

    return WM2_CLIENT_NO_OP;
}

static void
wm2_client_ovsdb_set_oftag(struct wm2_client *c)
{
    struct wm2_client_vif *v = wm2_client_get_connected_vif_new(c);
    const char *oftag = v != NULL ? v->oftag : NULL;

    if (strcmp(oftag ?: "", c->oftag_in_ovsdb ?: "") == 0)
        return;

    if (c->oftag_in_ovsdb != NULL) {
        wm2_clients_oftag_unset(c->mac_str, c->oftag_in_ovsdb);
        FREE(c->oftag_in_ovsdb);
        c->oftag_in_ovsdb = NULL;
    }

    if (oftag != NULL) {
        switch (wm2_clients_oftag_set(c->mac_str, oftag)) {
            case 1:
                assert(c->oftag_in_ovsdb == NULL);
                c->oftag_in_ovsdb = STRDUP(oftag);
                break;
            case 0:
                LOGI("%s: %s: oftag not updated, ignoring",
                     c->mac_str, oftag);
                assert(c->oftag_in_ovsdb == NULL);
                c->oftag_in_ovsdb = STRDUP(oftag);
                /*
                 * This is what this case should be doing but the controller
                 * doesn't set up all the Openflow_Tag rows currently (for
                 * onboard/backhaul interfaces at least) and this would cause
                 * WM indefinitely retry updating Openflow_Tag, causing a lot
                 * of churn and warnings along the way.
                 *
                 * Until controller starts setting up Openflow_Tag will all the
                 * Wifi_VIF_Config oftags this needs to stay commented out.
                 */
#if 0
                LOGW("%s: %s: couldn't set oftag, probably because OpenflowTag doesn't exist, will retry",
                     c->mac_str, c->oftag);
                ev_timer_again(EV_DEFAULT_ &c->retry);
#endif
                break;
            default:
                /* This could be out-of-memory, contraint error or something
                 * else. No use retrying.
                 */
                LOGE("%s: %s: couldn't set oftag, unexpected error, aborting..",
                     c->mac_str, oftag);
                ev_break(EV_DEFAULT_ EVBREAK_ALL);
                break;
        }
    }
}

static void
wm2_client_ovsdb_upsert(struct wm2_client *c)
{
    struct wm2_client_vif *v = wm2_client_get_connected_vif_new(c);
    json_t *pwhere;
    json_t *where;
    bool update_uuid = true;

    assert(v != NULL);

    SCHEMA_SET_STR(c->ovsdb.mac, c->mac_str);
    SCHEMA_SET_STR(c->ovsdb.key_id, v->wpa_key_id ?: "");
    SCHEMA_SET_STR(c->ovsdb.state, "active");
    SCHEMA_SET_BOOL(c->ovsdb.pmf, v->pmf);
    if (v->oftag != NULL) SCHEMA_SET_STR(c->ovsdb.oftag, v->oftag);
    if (v->wpa_key_mgmt != NULL) SCHEMA_SET_STR(c->ovsdb.wpa_key_mgmt, v->wpa_key_mgmt);
    if (v->pairwise_cipher != NULL) SCHEMA_SET_STR(c->ovsdb.pairwise_cipher, v->pairwise_cipher);

    pwhere = ovsdb_where_simple("if_name", v->if_name);
    where = ovsdb_where_simple("mac", c->mac_str);

    assert(pwhere != NULL);
    assert(where != NULL);

    if (WARN_ON(ovsdb_table_upsert_with_parent_where(&table_Wifi_Associated_Clients,
                                                     where,
                                                     &c->ovsdb,
                                                     update_uuid,
                                                     NULL,
                                                     OVSDB_CLIENTS_PARENT,
                                                     pwhere,
                                                     OVSDB_CLIENTS_PARENT_COL)
                                                     == false)) {
        LOGW("%s: %s: couldn't upsert, probably because Wifi_VIF_State doesn't exist, will retry",
             v->if_name, c->mac_str);
        ev_timer_again(EV_DEFAULT_ &c->retry);
        return;
    }

    v->in_ovsdb = true;
}

static void
wm2_client_ovsdb_delete(struct wm2_client *c)
{
    struct wm2_client_vif *v;

    assert(ovsdb_table_delete(&table_Wifi_Associated_Clients, &c->ovsdb) == true);
    memset(&c->ovsdb, 0, sizeof(c->ovsdb));

    ds_dlist_foreach(&c->vif_list, v)
        v->in_ovsdb = false;
}

static void
wm2_client_vif_free(struct wm2_client_vif *v)
{
    LOGD("%s: %s: removing interface", v->c->mac_str, v->if_name);
    ds_dlist_remove(&v->c->vif_list, v);
    FREE(v->if_name);
    FREE(v->wpa_key_id);
    FREE(v->dpp_pk_hash);
    FREE(v->wpa_key_mgmt);
    FREE(v->pairwise_cipher);
    FREE(v->oftag);
    FREE(v);
}

static void
wm2_client_remove_disconnected_vifs(struct wm2_client *c)
{
    struct wm2_client_vif *tmp;
    struct wm2_client_vif *v;

    ds_dlist_foreach_safe(&c->vif_list, v, tmp)
        if (v->connected == false && v->in_ovsdb == false)
            wm2_client_vif_free(v);
}

static void
wm2_client_free(struct wm2_client *c)
{
    LOGD("%s: removing client", c->mac_str);

    ev_async_stop(EV_DEFAULT_ &c->async);
    ev_timer_stop(EV_DEFAULT_ &c->retry);
    ds_dlist_remove(&g_wm2_clients, c);
    FREE(c->oftag_in_ovsdb);
    FREE(c);
}

static void
wm2_client_flush(struct wm2_client *c)
{
    wm2_client_remove_disconnected_vifs(c);

    if (wm2_client_ovsdb_is_connected(c) == true) return;
    if (wm2_client_is_connected(c) == true) return;
    if (c->oftag_in_ovsdb != NULL) return;
    if (ev_is_active(&c->retry) == true) return;
    if (ev_is_pending(&c->async) == true) return;

    wm2_client_free(c);
}

static void
wm2_client_dump_debug(struct wm2_client *c)
{
    struct wm2_client_vif *v_new = wm2_client_get_connected_vif_new(c);
    struct wm2_client_vif *v_old = wm2_client_get_connected_vif_old(c);
    struct wm2_client_vif *v;

    LOGD("%s: key=%s/%s/%s dpp=%s/%s/%s oftag=%s/%s/%s vif=%s->%s "
         "wpa_key_mgmt=%s/%s/%s pairwise_cipher=%s/%s/%s pmf=%s/%s/%s "
         "async=%d retry=%d connected=%d/%d sync=%d",
         c->mac_str,
         v_old != NULL ? (v_old->wpa_key_id ?: "") : "",
         v_new != NULL ? (v_new->wpa_key_id ?: "") : "",
         c->ovsdb.key_id,
         v_old != NULL ? (v_old->dpp_pk_hash ?: "") : "",
         v_new != NULL ? (v_new->dpp_pk_hash ?: "") : "",
         c->ovsdb.dpp_netaccesskey_sha256_hex,
         c->oftag_in_ovsdb ?: "",
         v_old != NULL ? (v_old->oftag ?: "") : "",
         v_new != NULL ? (v_new->oftag ?: "") : "",
         v_old ? v_old->if_name : "",
         v_new ? v_new->if_name : "",
         v_old != NULL ? (v_old->wpa_key_mgmt ?: "") : "",
         v_new != NULL ? (v_new->wpa_key_mgmt ?: "") : "",
         c->ovsdb.wpa_key_mgmt,
         v_old != NULL ? (v_old->pairwise_cipher ?: "") : "",
         v_new != NULL ? (v_new->pairwise_cipher ?: "") : "",
         c->ovsdb.pairwise_cipher,
         v_old != NULL ? (v_old->pmf ? "true" : "false") : "",
         v_new != NULL ? (v_new->pmf ? "true" : "false") : "",
         c->ovsdb.pmf ? "true" : "false",
         ev_is_pending(&c->async),
         ev_is_active(&c->retry),
         wm2_client_is_connected(c),
         wm2_client_ovsdb_is_connected(c),
         wm2_client_ovsdb_is_synced(c));

    ds_dlist_foreach(&c->vif_list, v) {
        LOGD("%s: %s: connected=%d ovsdb=%d",
             c->mac_str,
             v->if_name,
             v->connected,
             v->in_ovsdb);
    }
}

static void
wm2_client_work(struct wm2_client *c)
{
    struct wm2_client_vif *v_new = wm2_client_get_connected_vif_new(c);
    struct wm2_client_vif *v_old = wm2_client_get_connected_vif_old(c);
    struct wm2_client_vif *v;

    wm2_client_dump_debug(c);

    switch (wm2_client_get_transition(c)) {
        case WM2_CLIENT_CONNECTED:
            assert(v_new != NULL);
            LOGN("Client '%s' connected on '%s' with key '%s'"
                 ", wpa_key_mgmt '%s', pairwise_cipher '%s', pmf %s",
                 c->mac_str, v_new->if_name, v_new->wpa_key_id ?: "",
                 v_new->wpa_key_mgmt, v_new->pairwise_cipher, v_new->pmf ? "true" : "false");
            wm2_client_ovsdb_upsert(c);
            wm2_clients_isolate(v_new->if_name, c->mac_str, true);
            break;
        case WM2_CLIENT_DISCONNECTED:
            assert(v_old != NULL);
            LOGN("Client '%s' disconnected from '%s' with key '%s'",
                 c->mac_str, v_old->if_name, c->ovsdb.key_id ?: "");
            wm2_client_ovsdb_delete(c);
            wm2_clients_isolate(v_old->if_name, c->mac_str, false);
            break;
        case WM2_CLIENT_ROAMED:
            assert(v_old != NULL);
            assert(v_new != NULL);
            LOGN("Client '%s' roamed to '%s' with key '%s'",
                 c->mac_str, v_new->if_name, v_new->wpa_key_id ?: "");
            wm2_client_ovsdb_delete(c);
            wm2_client_ovsdb_upsert(c);
            wm2_clients_isolate(v_old->if_name, c->mac_str, false);
            wm2_clients_isolate(v_new->if_name, c->mac_str, true);
            v_old->print_when_removed = true;
            v_new->print_when_removed = false;
            break;
        case WM2_CLIENT_CHANGED:
            assert(v_new != NULL);
            LOGN("Client '%s' re-connected on '%s' with key '%s'",
                 c->mac_str, v_new->if_name, v_new->wpa_key_id ?: "");
            wm2_client_ovsdb_upsert(c);
            break;
        case WM2_CLIENT_NO_OP:
            LOGD("Client '%s' no-op", c->mac_str);
            break;
    }

    ds_dlist_foreach(&c->vif_list, v) {
        if (v->print_when_removed == true && v->connected == false) {
            LOGN("Client '%s' removed from '%s' with key '%s'",
                 c->mac_str, v->if_name, v->wpa_key_id ?: "");
            v->print_when_removed = false;
        }
    }

    wm2_client_ovsdb_set_oftag(c);
    wm2_client_flush(c);
}

static void
wm2_client_retry_cb(EV_P_ ev_timer *t, int event)
{
    struct wm2_client *c = container_of(t, struct wm2_client, retry);
    LOGI("%s: retrying work", c->mac_str);
    ev_timer_stop(EV_A_ t);
    wm2_client_work(c);
}

static void
wm2_client_async_cb(EV_P_ ev_async *a, int event)
{
    struct wm2_client *c = container_of(a, struct wm2_client, async);
    wm2_client_work(c);
}

struct wm2_client *
wm2_client_lookup(const char *mac_str)
{
    struct wm2_client *c;

    ds_dlist_foreach(&g_wm2_clients, c)
        if (strcasecmp(c->mac_str, mac_str) == 0)
            return c;

    return NULL;
}

static struct wm2_client *
wm2_client_new(const char *mac_str)
{
    struct wm2_client *c;

    LOGD("%s: creating client", mac_str);

    c = CALLOC(1, sizeof(*c));
    c->mac_str = STRDUP(mac_str);
    str_tolower(c->mac_str);
    ev_timer_init(&c->retry, wm2_client_retry_cb, 0, WM2_CLIENT_RETRY_SECONDS);
    ev_async_init(&c->async, wm2_client_async_cb);
    ev_async_start(EV_DEFAULT_ &c->async);
    ds_dlist_init(&c->vif_list, struct wm2_client_vif, list);
    ds_dlist_insert_head(&g_wm2_clients, c);

    return c;
}

static struct wm2_client *
wm2_client_lookup_or_new(const char *mac_str)
{
    struct wm2_client *c = wm2_client_lookup(mac_str);
    if (c != NULL) return c;
    return wm2_client_new(mac_str);
}

static struct wm2_client_vif *
wm2_client_vif_lookup(struct wm2_client *c, const char *if_name)
{
    struct wm2_client_vif *v;

    ds_dlist_foreach(&c->vif_list, v)
        if (strcmp(v->if_name, if_name) == 0)
            return v;

    return NULL;
}

static struct wm2_client_vif *
wm2_client_vif_new(struct wm2_client *c, const char *if_name)
{
    struct wm2_client_vif *v;

    LOGD("%s: %s: creating interface", c->mac_str, if_name);
    v = CALLOC(1, sizeof(*v));
    v->if_name = STRDUP(if_name);
    v->c = c;
    ds_dlist_insert_head(&c->vif_list, v);

    return v;
}

static struct wm2_client_vif *
wm2_client_vif_lookup_or_new(struct wm2_client *c, const char *if_name)
{
    struct wm2_client_vif *v = wm2_client_vif_lookup(c, if_name);
    if (v != NULL) return v;
    return wm2_client_vif_new(c, if_name);
}

static char *
wm2_client_get_oftag(const char *if_name,
                     const char *mac_str,
                     const char *wpa_key_id,
                     const char *dpp_pk_hash)
{
    char oftag[32] = {0};

    if (dpp_pk_hash != NULL) {
        if (!wm2_dpp_key_to_oftag(dpp_pk_hash, oftag, sizeof(oftag)))
            if (!wm2_clients_get_default_oftag(if_name, oftag, sizeof(oftag)))
                LOGN("%s: %s: could not map oftag", if_name, mac_str);
    }
    else {
        wm2_clients_oftag_from_key_id(if_name, wpa_key_id ?: "", oftag, sizeof(oftag));
    }

    if (strlen(oftag) > 0)
        return STRDUP(oftag);
    else
        return NULL;
}

static void
wm2_client_update(const char *if_name,
                  const char *mac_str,
                  const char *wpa_key_id,
                  const char *dpp_pk_hash,
                  const char *wpa_key_mgmt,
                  const char *pairwise_cipher,
                  bool pmf,
                  bool connected)
{
    struct wm2_client *c = wm2_client_lookup_or_new(mac_str);
    struct wm2_client_vif *v = wm2_client_vif_lookup_or_new(c, if_name);

    if (strlen(wpa_key_mgmt ?: "") == 0) wpa_key_mgmt = NULL;
    if (strlen(pairwise_cipher ?: "") == 0) pairwise_cipher = NULL;
    if (strlen(wpa_key_id ?: "") == 0) wpa_key_id = NULL;
    if (strlen(dpp_pk_hash ?: "") == 0) dpp_pk_hash = NULL;

    FREE(v->wpa_key_mgmt);
    FREE(v->pairwise_cipher);
    FREE(v->wpa_key_id);
    FREE(v->dpp_pk_hash);
    FREE(v->oftag);

    v->connected = connected;
    v->pmf = pmf;
    ds_dlist_remove(&c->vif_list, v);

    if (v->connected == true)
        ds_dlist_insert_head(&c->vif_list, v);
    else
        ds_dlist_insert_tail(&c->vif_list, v);

    v->wpa_key_mgmt = wpa_key_mgmt ? STRDUP(wpa_key_mgmt) : NULL;
    v->pairwise_cipher = pairwise_cipher ? STRDUP(pairwise_cipher) : NULL;
    v->wpa_key_id = wpa_key_id ? STRDUP(wpa_key_id) : NULL;
    v->dpp_pk_hash = dpp_pk_hash ? STRDUP(dpp_pk_hash) : NULL;
    v->oftag = wm2_client_is_connected(c) == true
             ? wm2_client_get_oftag(if_name, c->mac_str, wpa_key_id, dpp_pk_hash)
             : NULL;

    LOGD("%s: %s: set client as %s, key_id=%s dpp=%s oftag=%s pmf=%s",
         c->mac_str,
         v->if_name,
         v->connected ? "connected" : "disconnected",
         v->wpa_key_id ?: "",
         v->dpp_pk_hash ?: "",
         v->oftag ?: "",
         v->pmf ? "true" : "false");

    /* This is intended to debounce and coalesce multiple
     * client events across multiple vifs. This isn't
     * guaranteed to coalesce them all, but provides an easy
     * way of handling these nicely most of the time.
     */
    ev_async_send(EV_DEFAULT_ &c->async);
}

bool
wm2_clients_update(struct schema_Wifi_Associated_Clients *schema, char *ifname, bool status)
{
    LOGD("%s: %s: updating client as %s, key_id=%s, dpp=%s, pmf=%s",
         schema->mac,
         ifname,
         status ? "connected" : "disconnected",
         schema->key_id,
         schema->dpp_netaccesskey_sha256_hex,
         schema->pmf ? "true" : "false");

    wm2_client_update(ifname,
                      schema->mac,
                      schema->key_id,
                      schema->dpp_netaccesskey_sha256_hex,
                      schema->wpa_key_mgmt,
                      schema->pairwise_cipher,
                      schema->pmf,
                      status);
    return true;
}

void
wm2_clients_update_per_vif(const struct schema_Wifi_Associated_Clients *clients,
                           int n_clients,
                           const char *if_name)
{
    struct wm2_client_vif *v;
    struct wm2_client *c;
    int i;

    ds_dlist_foreach(&g_wm2_clients, c) {
        ds_dlist_foreach(&c->vif_list, v) {
            if (strcmp(v->if_name, if_name) == 0) {
                for (i = 0; i < n_clients; i++)
                    if (strcasecmp(clients[i].mac, c->mac_str) == 0)
                        break;

                if (i < n_clients)
                    continue;

                LOGI("%s: %s: flushing client", if_name, c->mac_str);
                wm2_client_update(if_name,
                                  c->mac_str,
                                  v->wpa_key_id,
                                  v->dpp_pk_hash,
                                  v->wpa_key_mgmt,
                                  v->pairwise_cipher,
                                  v->pmf,
                                  false);
            }
        }
    }

    for (i = 0; i < n_clients; i++) {
        wm2_client_update(if_name,
                          clients[i].mac,
                          clients[i].key_id,
                          clients[i].dpp_netaccesskey_sha256_hex,
                          clients[i].wpa_key_mgmt,
                          clients[i].pairwise_cipher,
                          clients[i].pmf,
                          true);
    }
}
