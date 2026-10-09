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

/*
 * WANO STA module
 *
 * STA Backup uplink feature.
 *
 * Handles WAN_Config type=sta policies. When conditions to establish
 * a STA uplink are met, it OVSDB-configures everything needed to
 * bring up a STA uplink on the device: Wifi OVSDB configuration,
 * and starting a STA pipeline (wano_sta_ppline) on the STA VIF or on
 * the MLD bond interface if this is an MLO device.
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"
#include "memutil.h"
#include "ovsdb_table.h"
#include "ovsdb_sync.h"
#include "schema.h"
#include "ds_tree.h"
#include "ev.h"

#include "wano.h"
#include "wano_wan.h"
#include "wano_sta_ppline.h"

#define MODULE_ID LOG_MODULE_ID_MISC

#define WANO_STA_MAX_VIFS             8
#define WANO_STA_SCAN_TIMEOUT_DEFAULT 30

/* =========================================================================
 * Per-STA VIF tracking (STA VIF and MLD bond interfaces)
 * ======================================================================= */
struct wano_sta_vif
{
    char vs_ifname[C_IFNAME_LEN];
    char vs_radio[C_IFNAME_LEN];
    wano_sta_ppline_t *vs_ppline; /* STA pipeline, non-NULL when running */
    bool vs_configured;           /* credential_configs written on this VIF */
    ds_tree_node_t vs_tnode;
};

static ds_tree_t g_wano_sta_vifs = DS_TREE_INIT(ds_str_cmp, struct wano_sta_vif, vs_tnode);

struct wano_sta_entry
{
    char radio[C_IFNAME_LEN];
    char vif[C_IFNAME_LEN];
    char band[8];
    int band_prio;
};

/* A list of STAs available on this device. With info on the corresponding radio,
 * VIF name, band and band priority. Initialized once at init.
 */
static struct wano_sta_entry g_sta_list[WANO_STA_MAX_VIFS];
static int g_sta_list_len = 0;

/* =========================================================================
 * STA uplink connection status, reported into WAN_Config other_status
 * key "last_status".
 * ======================================================================= */
enum wano_sta_status
{
    WANO_STA_STATUS_UNKNOWN = 0,
    WANO_STA_STATUS_ERR_GENERAL,           /* General error not covered by any other error */
    WANO_STA_STATUS_ERR_WIFI,              /* General WiFi error */
    WANO_STA_STATUS_ERR_SSID_NOT_FOUND,    /* SSID/network not found */
    WANO_STA_STATUS_ERR_WRONG_KEY,         /* SSID found, but authentication failed */
    WANO_STA_STATUS_CONNECTED_L2,          /* WiFi assoc success + 4-way handshake success */
    WANO_STA_STATUS_L3_OK,                 /* We got DHCP addr/route */
    WANO_STA_STATUS_CONNECTED_INTERNET_OK, /* "Internet probe" test pass */
};

/* WAN_Config other_status "last_status" string value mappings */
static const char *wano_sta_status_str(enum wano_sta_status status)
{
    switch (status)
    {
        case WANO_STA_STATUS_ERR_GENERAL:
            return "err_general";
        case WANO_STA_STATUS_ERR_WIFI:
            return "err_wifi";
        case WANO_STA_STATUS_ERR_SSID_NOT_FOUND:
            return "err_ssid_not_found";
        case WANO_STA_STATUS_ERR_WRONG_KEY:
            return "err_wrong_key";
        case WANO_STA_STATUS_CONNECTED_L2:
            return "connected_l2";
        case WANO_STA_STATUS_L3_OK:
            return "connected_l3_ok";
        case WANO_STA_STATUS_CONNECTED_INTERNET_OK:
            return "connected_internet_ok";
        case WANO_STA_STATUS_UNKNOWN:
            break;
    }
    return NULL;
}

/* =========================================================================
 * STA WAN policies (from WAN_Config type=sta rows)
 * ======================================================================= */
struct wano_sta_policy
{
    char sp_uuid[40];    /* WAN_Config row UUID this policy belongs to */
    int sp_wan_priority; /* WAN_Config.priority */
    char sp_ssid[36 + 1];
    char sp_key[128 + 1];
    char sp_encryption[32];
    char sp_ifname[C_IFNAME_LEN];             /* restrict to single VIF, empty = all */
    char sp_connectivity_check[C_IFNAME_LEN]; /* optional Connectivity_Check */
    char sp_inet_role_str[64 + 1];            /* Wifi_Inet_Config:role for the STA uplink, empty = none */
    int sp_scan_timeout;

    /* Test connection: only test the STA uplink and report the status, never
     * switch to it. Such a policy is always unconditional (any configured
     * Connectivity_Check is ignored). */
    bool sp_test_connection;

    bool sp_attached; /* is this policy currently attached */
    ovs_uuid_t sp_cred_uuid;
    bool sp_cred_valid;

    /* Is this policy failed?
     * Policy is marked as failed when it has no STA candidates left to try.
     */
    bool sp_failed;

    /* If no pinned VIF, this index points to the currently enabled STA in g_sta_list */
    int sp_rotation_idx;
    bool sp_rotation_exhausted;

    /* Last connection status achieved by this policy */
    enum wano_sta_status sp_last_status;

    /* When sp_last_status is WANO_STA_STATUS_ERR_WIFI, this field may contain
     * the detail WiFi failure reason, if it is available
     * (verbatim copied from Wifi_VIF_State::state).
     */
    char sp_last_wifi_err[64];

    ds_tree_node_t sp_tnode;
};

static ds_tree_t g_sta_policies = DS_TREE_INIT(ds_str_cmp, struct wano_sta_policy, sp_tnode);
static bool g_sta_initialized = false;

/* =========================================================================
 * Attach state machine
 *
 * IDLE       : fresh start, credentials not yet set
 * CRED_SET   : credentials pushed, scan window running, pipelines starting
 * ======================================================================= */
typedef enum
{
    WSTA_S_IDLE = 0,
    WSTA_S_CRED_SET,
} wsta_state_t;

static wsta_state_t g_sta_state = WSTA_S_IDLE;

/* Have we ever associated this cycle? */
static bool g_sta_any_associated = false;

/* Are we currently associated? */
static bool g_sta_currently_associated = false;

/* =========================================================================
 * OVSDB tables
 * ======================================================================= */
static ovsdb_table_t table_Wifi_VIF_Config;
static ovsdb_table_t table_Wifi_Radio_Config;
static ovsdb_table_t table_Wifi_VIF_State;
static ovsdb_table_t table_Connectivity_Check;

/* =========================================================================
 * Timers
 * ======================================================================= */
static ev_timer g_sta_apply_timer;
static ev_timer g_sta_scan_timer;

static wano_sta_ppline_event_cb_fn_t wano_sta_ppline_event_cb;
static void wano_sta_apply_timer_fn(struct ev_loop *loop, ev_timer *w, int revent);
static void wano_sta_scan_timer_fn(struct ev_loop *loop, ev_timer *w, int revent);
static void wano_sta_on_scan_timeout_expire_or_failure(void);
static void wano_sta_schedule_apply(void);
static void callback_Wifi_VIF_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_VIF_State *old,
        struct schema_Wifi_VIF_State *new);
static void callback_Connectivity_Check(
        ovsdb_update_monitor_t *mon,
        struct schema_Connectivity_Check *old,
        struct schema_Connectivity_Check *new);
static void wano_sta_apply_state_dispatch(void);
static void wano_sta_apply(void);
static void wano_sta_set_credentials_and_configure(void);
static void wano_sta_clear_credentials_config(void);
static void wano_sta_start_pipelines(void);
static bool wano_sta_policy_should_attach(struct wano_sta_policy *sp);
static void wano_sta_policy_warn_if_ssid_shadowed(struct wano_sta_policy *sp);
static const char *wano_sta_selected_policy_get_ifname(void);
static int wano_sta_attached_policy_count(void);
static struct wano_sta_policy *wano_sta_attached_policy(void);
static struct wano_sta_policy *wano_sta_policy_get(const char *policy_uuid);
static struct wano_sta_policy *wano_sta_policy_get_or_add(const char *policy_uuid);
static void wano_sta_policy_set(
        struct wano_sta_policy *sp,
        int wan_priority,
        const struct wano_wan_config_sta *sta_cfg,
        bool *changed);
static void wano_sta_policy_active_status_set(
        struct wano_sta_policy *sp,
        const char *vif_name,
        const char *mld_if_name,
        const char *status);
static void wano_sta_policy_last_status_set(struct wano_sta_policy *sp);
static void wano_sta_policy_remove(struct wano_sta_policy *sp);
static bool wano_sta_policy_attach(struct wano_sta_policy *sp);
static void wano_sta_policy_detach(struct wano_sta_policy *sp);

/* Check if a STA VIF is part of an MLO group. If so, return the MLD VIF name. */
static bool wano_sta_vif_is_in_mlo(const char *ifname, char *mld_ifname, size_t mld_ifname_len)
{
    struct schema_Wifi_VIF_State vstate;
    MEMZERO(vstate);

    if (!ovsdb_table_select_one_where(
                &table_Wifi_VIF_State,
                ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_State, if_name), ifname),
                &vstate))
        return false;

    if (!vstate.mld_if_name_exists || vstate.mld_if_name[0] == '\0') return false;

    if (mld_ifname != NULL && mld_ifname_len > 0)
    {
        strncpy(mld_ifname, vstate.mld_if_name, mld_ifname_len - 1);
        mld_ifname[mld_ifname_len - 1] = '\0';
    }
    return true;
}

/* Default priorities for STAs based on freq_band */
static int wano_sta_freq_band_priority(const char *band)
{
    if (band == NULL || band[0] == '\0') return 5;
    if (strcmp(band, "5GL") == 0) return 0;
    if (strcmp(band, "5GU") == 0) return 1;
    if (strcmp(band, "5G") == 0) return 2;
    if (strcmp(band, "2.4G") == 0) return 3;
    if (strcmp(band, "6G") == 0) return 4;
    return 5;
}

/*
 * For a STA VIF identified by its interface name "radio", identify the "freq_band" string.
 * i.e. the band on which the STA VIF is operating on
 */
static int wano_sta_lookup_freq_band(const char *radio, char *freq_band_buff, size_t freq_band_buff_len)
{
    struct schema_Wifi_Radio_Config rconf;
    MEMZERO(rconf);
    if (freq_band_buff_len > 0) freq_band_buff[0] = '\0';

    if (!ovsdb_table_select_one_where(
                &table_Wifi_Radio_Config,
                ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), radio),
                &rconf))
        return -1;

    if (!rconf.freq_band_exists || rconf.freq_band[0] == '\0') return -1;

    if (freq_band_buff != NULL && freq_band_buff_len > 0)
    {
        strncpy(freq_band_buff, rconf.freq_band, freq_band_buff_len - 1);
        freq_band_buff[freq_band_buff_len - 1] = '\0';
    }
    return 0;
}

/* Set Wifi_Radio_Config::allow_sta_roam_channels for "radio" */
static bool set_allow_sta_roam_channels(const char *radio, bool allow_sta_roam_channels)
{
    struct schema_Wifi_Radio_Config rconf;

    MEMZERO(rconf);
    rconf._partial_update = true;
    SCHEMA_SET_BOOL(rconf.allow_sta_roam_channels, allow_sta_roam_channels);

    if (ovsdb_table_update_where(
                &table_Wifi_Radio_Config,
                ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), radio),
                &rconf)
        != 1)
    {
        LOG(WARN, "wano_sta: %s: Failed to set allow_sta_roam_channels=%d", radio, allow_sta_roam_channels);
        return false;
    }
    return true;
}

/* qsort comparator for ordering STAs based on band: order by band_prio ascending (0=highest priority). */
static int wano_sta_entry_band_prio_cmp(const void *a, const void *b)
{
    const struct wano_sta_entry *ea = a;
    const struct wano_sta_entry *eb = b;
    return ea->band_prio - eb->band_prio;
}

/* Sort g_sta_list[] by freq_band priority (highest priority first). */
static void wano_sta_sort_list_by_band(void)
{
    int i;

    if (g_sta_list_len <= 1) return;

    /* First, lookup freq_band for each STA, cache it in the entry, and map
     * it to a priority. */
    for (i = 0; i < g_sta_list_len; i++)
    {
        if (wano_sta_lookup_freq_band(g_sta_list[i].radio, g_sta_list[i].band, sizeof(g_sta_list[i].band)) < 0)
        {
            LOG(WARN,
                "wano_sta: %s (radio %s): freq_band missing — placing at rotation tail",
                g_sta_list[i].vif,
                g_sta_list[i].radio);  // not really expected to happen, but ok
            g_sta_list[i].band_prio = 5;
        }
        else
        {
            g_sta_list[i].band_prio = wano_sta_freq_band_priority(g_sta_list[i].band);
            if (g_sta_list[i].band_prio == 5)
            {
                LOG(WARN,
                    "wano_sta: %s (radio %s): unrecognized freq_band '%s' — placing at rotation tail",
                    g_sta_list[i].vif,
                    g_sta_list[i].radio,
                    g_sta_list[i].band);
            }
        }
    }

    /* Sort by band priority */
    qsort(g_sta_list, g_sta_list_len, sizeof(g_sta_list[0]), wano_sta_entry_band_prio_cmp);

    LOG(NOTICE, "wano_sta: sorted g_sta_list by freq_band priority:");

    for (i = 0; i < g_sta_list_len; i++)
    {
        LOG(NOTICE,
            "wano_sta:   [%d] vif=%s radio=%s band=%s",
            i,
            g_sta_list[i].vif,
            g_sta_list[i].radio,
            g_sta_list[i].band[0] ? g_sta_list[i].band : "?");
    }
}

/* =========================================================================
 * Parse CONFIG_OVSDB_BOOTSTRAP_WIFI_STA_LIST
 * Format: "wifi0:bhaul-sta-24 wifi1:bhaul-sta-60 wifi2:bhaul-sta-50"
 * ======================================================================= */
static void wano_sta_parse_list(void)
{
#ifdef CONFIG_OVSDB_BOOTSTRAP_WIFI_STA_LIST
    char buf[] = CONFIG_OVSDB_BOOTSTRAP_WIFI_STA_LIST;
    char *token;
    char *saveptr;

    g_sta_list_len = 0;

    token = strtok_r(buf, " ", &saveptr);
    while (token != NULL && g_sta_list_len < WANO_STA_MAX_VIFS)
    {
        char *colon = strchr(token, ':');
        if (colon != NULL)
        {
            *colon = '\0';
            STRSCPY(g_sta_list[g_sta_list_len].radio, token);
            STRSCPY(g_sta_list[g_sta_list_len].vif, colon + 1);
            g_sta_list_len++;
        }
        token = strtok_r(NULL, " ", &saveptr);
    }

    LOG(INFO, "wano_sta: Parsed %d STA entries from bootstrap config", g_sta_list_len);
#else
    g_sta_list_len = 0;
    LOG(INFO, "wano_sta: No STA list configured");
#endif
    wano_sta_sort_list_by_band();
}

/*
 * CM backhaul CMU disabling knog and status checking.
 *
 * While at least one WAN_Config type==sta policy exists, CM must not
 * manage Connection_Manager_Uplink rows for STA VIFs -- wano_sta does.
 * The request is made via Node_Config module==CM key==bh_cmu_disable and
 * CM acks via Node_State module==CM key==bh_cmu_disable_status once it
 * has fully disarmed its CMU management. Policies are
 * attached only after the ack.
 * ======================================================================= */
#define WANO_STA_CM_NODE_MODULE  "CM"
#define WANO_STA_CM_CFG_DISABLE  "bh_cmu_disable"
#define WANO_STA_CM_STATE_STATUS "bh_cmu_disable_status"
#define WANO_STA_CM_CONFIRM_SEC  15.0

static ovsdb_table_t table_Node_State;
static ev_timer g_sta_cm_confirm_timer;
static int g_cm_bh_cmu_disable_req = -1;  /* last requested value, -1 = nothing requested yet */
static bool g_cm_bh_cmu_disabled = false; /* last status acked by CM */

/* Ack watchdog: fires when CM did not confirm the request in time. */
static void wano_sta_cm_confirm_timer_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    if ((int)g_cm_bh_cmu_disabled == g_cm_bh_cmu_disable_req) return;

    LOG(WARN,
        "wano_sta: CM did not ack %s=%s within %.0fs",
        WANO_STA_CM_CFG_DISABLE,
        g_cm_bh_cmu_disable_req == 1 ? "true" : "false",
        WANO_STA_CM_CONFIRM_SEC);
}

/* Request CM to disable/enable its backhaul CMU management. The request
 * is settled only once CM acks it via Node_State (callback below). */
static void wano_sta_set_cm_bh_cmu_disable(bool disable)
{
    /* Settled only when this value is both requested and acked by CM. */
    if (g_cm_bh_cmu_disable_req == (int)disable && g_cm_bh_cmu_disabled == disable) return;

    const bool req_changed = (g_cm_bh_cmu_disable_req != (int)disable);
    g_cm_bh_cmu_disable_req = disable;

    json_t *where = ovsdb_where_multi(
            ovsdb_where_simple(SCHEMA_COLUMN(Node_Config, module), WANO_STA_CM_NODE_MODULE),
            ovsdb_where_simple(SCHEMA_COLUMN(Node_Config, key), WANO_STA_CM_CFG_DISABLE),
            NULL);
    json_t *row = json_pack(
            "{s:s, s:s, s:s}",
            SCHEMA_COLUMN(Node_Config, module),
            WANO_STA_CM_NODE_MODULE,
            SCHEMA_COLUMN(Node_Config, key),
            WANO_STA_CM_CFG_DISABLE,
            SCHEMA_COLUMN(Node_Config, value),
            disable ? "true" : "false");
    if (!ovsdb_sync_upsert_where(SCHEMA_TABLE(Node_Config), where, row, NULL))
    {
        LOG(ERR, "wano_sta: Failed to upsert Node_Config %s=%s", WANO_STA_CM_CFG_DISABLE, disable ? "true" : "false");
        g_cm_bh_cmu_disable_req = -1; /* retry on the next request */
        return;
    }

    LOG(NOTICE, "wano_sta: Requested CM %s=%s", WANO_STA_CM_CFG_DISABLE, disable ? "true" : "false");

    if (g_cm_bh_cmu_disabled != disable)
    {
        if (req_changed || ev_is_active(&g_sta_cm_confirm_timer) == false)
        {
            ev_timer_stop(EV_DEFAULT, &g_sta_cm_confirm_timer);
            ev_timer_set(&g_sta_cm_confirm_timer, WANO_STA_CM_CONFIRM_SEC, 0.0);
            ev_timer_start(EV_DEFAULT, &g_sta_cm_confirm_timer);
        }
    }
    else
    {
        ev_timer_stop(EV_DEFAULT, &g_sta_cm_confirm_timer);
    }
}

/* Node_State monitor callback: tracks the CM bh_cmu_disable_status ack. */
static void callback_Node_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Node_State *old,
        struct schema_Node_State *new)
{
    const struct schema_Node_State *row = (mon->mon_type == OVSDB_UPDATE_DEL) ? old : new;

    if (row == NULL) return;
    if (strcmp(row->module, WANO_STA_CM_NODE_MODULE) != 0) return;
    if (strcmp(row->key, WANO_STA_CM_STATE_STATUS) != 0) return;

    const bool disabled = (mon->mon_type != OVSDB_UPDATE_DEL) && (strcmp(row->value, "true") == 0);
    if (g_cm_bh_cmu_disabled == disabled) return;
    g_cm_bh_cmu_disabled = disabled;

    LOG(NOTICE, "wano_sta: CM acked %s: %s", WANO_STA_CM_STATE_STATUS, disabled ? "true" : "false");

    if (g_cm_bh_cmu_disable_req == (int)disabled) ev_timer_stop(EV_DEFAULT, &g_sta_cm_confirm_timer);

    /* A policy attach may be pending on this ack. */
    wano_sta_schedule_apply();
}

/* Current Node_Config bh_cmu_disable request value (missing row == false). */
static bool wano_sta_cm_bh_cmu_config_get(void)
{
    json_t *where = ovsdb_where_multi(
            ovsdb_where_simple(SCHEMA_COLUMN(Node_Config, module), WANO_STA_CM_NODE_MODULE),
            ovsdb_where_simple(SCHEMA_COLUMN(Node_Config, key), WANO_STA_CM_CFG_DISABLE),
            NULL);
    json_t *rows = ovsdb_sync_select_where(SCHEMA_TABLE(Node_Config), where);
    const char *value = json_string_value(json_object_get(json_array_get(rows, 0), SCHEMA_COLUMN(Node_Config, value)));
    const bool disable = (value != NULL) && (strcmp(value, "true") == 0);
    json_decref(rows);
    return disable;
}

static void wano_sta_cm_bh_cmu_init(void)
{
    OVSDB_TABLE_INIT_NO_KEY(Node_State);
    OVSDB_TABLE_MONITOR_F(Node_State, C_VPACK("+", "module", "key", "value"));
    ev_timer_init(&g_sta_cm_confirm_timer, wano_sta_cm_confirm_timer_fn, 0.0, 0.0);

    /* Make sure at init bh_cmu_disable is exactly as we want it
     * even in case there would be a stale disable left behind by a previous run. */
    json_t *rows = ovsdb_sync_select(SCHEMA_TABLE(WAN_Config), SCHEMA_COLUMN(WAN_Config, type), "sta");
    const bool want_disable = (rows != NULL) && (json_array_size(rows) > 0);
    json_decref(rows);

    if (want_disable || wano_sta_cm_bh_cmu_config_get())
    {
        wano_sta_set_cm_bh_cmu_disable(want_disable);
    }
}

static int wano_sta_attached_policy_count(void)
{
    struct wano_sta_policy *sp;
    int count = 0;

    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (sp->sp_attached) count++;
    }
    return count;
}

/* Returns the currently attached policy, or NULL if none. At most one policy
 * is attached at a time under the current single-selection model. */
static struct wano_sta_policy *wano_sta_attached_policy(void)
{
    struct wano_sta_policy *sp;

    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (sp->sp_attached) return sp;
    }
    return NULL;
}

/* Number of configured test_connection policies. The controller must ensure
 * at most one such policy exists at a time. */
static int wano_sta_test_connection_policy_count(void)
{
    struct wano_sta_policy *sp;
    int count = 0;

    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (sp->sp_test_connection) count++;
    }
    return count;
}

// This is a remnant of the old multi-selection model, don't roll your eyes too hard
static int wano_sta_wanted_policy_count(void)
{
    struct wano_sta_policy *sp;
    int count = 0;

    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (wano_sta_policy_should_attach(sp)) count++;
    }
    return count;
}

static int wano_sta_scan_timeout_get(void)
{
    struct wano_sta_policy *sp;
    int timeout = WANO_STA_SCAN_TIMEOUT_DEFAULT;

    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (!sp->sp_attached) continue;
        if (sp->sp_scan_timeout > timeout) timeout = sp->sp_scan_timeout;
    }

    return timeout;
}

/* Clears sp_failed on every policy and resets rotation index on all of them. */
static int wano_sta_reset_failed_all(void)
{
    struct wano_sta_policy *sp;
    int n = 0;

    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (sp->sp_failed)
        {
            sp->sp_failed = false;
            sp->sp_rotation_idx = 0;
            sp->sp_rotation_exhausted = false;
            n++;
        }
    }
    return n;
}

/* Returns true if at least one policy is currently eligible to attach
 * (passes should_attach, which already filters out sp_failed). */
static bool wano_sta_any_eligible(void)
{
    struct wano_sta_policy *sp;

    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (wano_sta_policy_should_attach(sp)) return true;
    }
    return false;
}

/* A policy is eligible when it has not been marked failed in the current cascade cycle and
 * its Connectivity_Check is NOK or if it has no Connectivity_Check configured. */
static bool wano_sta_policy_is_eligible(struct wano_sta_policy *sp)
{
    struct schema_Connectivity_Check cc_row;

    if (sp->sp_failed) return false;

    /* If no Connectivity_Check configured, eligible unconditionally */
    if (sp->sp_connectivity_check[0] == '\0') return true;

    MEMZERO(cc_row);
    if (!ovsdb_table_select_one_where(
                &table_Connectivity_Check,
                ovsdb_where_simple(SCHEMA_COLUMN(Connectivity_Check, name), sp->sp_connectivity_check),
                &cc_row))
        return true;

    if (!cc_row.status_exists || cc_row.status[0] == '\0') return false;

    return (strcmp(cc_row.status, "nok") == 0);
}

/* Does policy a outrank policy b */
static bool wano_sta_policy_outranks(const struct wano_sta_policy *a, const struct wano_sta_policy *b)
{
    if (a->sp_wan_priority != b->sp_wan_priority) return a->sp_wan_priority > b->sp_wan_priority;
    return strcmp(a->sp_uuid, b->sp_uuid) < 0; /* If equal priorities, compare by uuid only for determinism of choice */
}

/* Selects the next/current highest-priority eligible policy. */
static struct wano_sta_policy *wano_sta_selected_policy(void)
{
    struct wano_sta_policy *sp;
    struct wano_sta_policy *sel = NULL;

    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (!wano_sta_policy_is_eligible(sp)) continue;  // currently marked as failed or has Connectivity_Check=ok

        if (sel == NULL || wano_sta_policy_outranks(sp, sel)) sel = sp;
    }
    return sel;
}

/* Determines if the given policy should be attached right now.
 * Will be true if this is the next/current highest-priority eligible policy.
 */
static bool wano_sta_policy_should_attach(struct wano_sta_policy *sp)
{
    return sp == wano_sta_selected_policy();
}

/* WARN if this policy's SSID is shadowed by an outranking, non-failed sibling policy */
static void wano_sta_policy_warn_if_ssid_shadowed(struct wano_sta_policy *sp)
{
    struct wano_sta_policy *it;
    struct wano_sta_policy *owner = NULL;

    if (sp->sp_ssid[0] == '\0') return;

    /* Highest-ranked non-failed policy that owns this SSID (sp included). */
    ds_tree_foreach (&g_sta_policies, it)
    {
        if (it->sp_failed) continue; /* cascade: failed siblings yield */
        if (it->sp_ssid[0] == '\0') continue;
        if (strcmp(it->sp_ssid, sp->sp_ssid) != 0) continue;

        if (owner == NULL || wano_sta_policy_outranks(it, owner))
        {
            owner = it;
        }
    }

    if (owner == NULL || owner == sp) return;

    LOG(WARN,
        "wano_sta: Policy %s duplicate SSID '%s' blocked; owner policy is %s",
        sp->sp_uuid,
        sp->sp_ssid,
        owner->sp_uuid);
}

/* Get selected STA policy's ifname (if there is any selected STA policy)
 *  - selected policy = the one with highest priority among policies that should be attached
    - returns NULL if no policy should be attached or if selected policy does not have specific if_name defined
 */
static const char *wano_sta_selected_policy_get_ifname(void)
{
    struct wano_sta_policy *sp;
    struct wano_sta_policy *sel = NULL;

    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (!wano_sta_policy_should_attach(sp)) continue;
        if (sp->sp_ifname[0] == '\0') continue;

        if (sel == NULL || wano_sta_policy_outranks(sp, sel)) sel = sp;
    }

    return (sel != NULL) ? sel->sp_ifname : NULL;
}

static bool wano_sta_is_managed_vif(const char *if_name)
{
    int i;
    for (i = 0; i < g_sta_list_len; i++)
    {
        if (strcmp(g_sta_list[i].vif, if_name) == 0) return true;
    }
    return false;
}

/* Find the radio hosting `vif`. Returns NULL if `vif` is not in g_sta_list. */
static const char *wano_sta_radio_for_vif(const char *vif)
{
    int i;
    for (i = 0; i < g_sta_list_len; i++)
    {
        if (strcmp(g_sta_list[i].vif, vif) == 0) return g_sta_list[i].radio;
    }
    return NULL;
}

/* Returns the effective STA ifname for `sp`:
 *   - sp->sp_ifname when explicitly pinned (no rotation);
 *   - g_sta_list[sp->sp_rotation_idx].vif otherwise (sequential rotation).
 *
 * Returns NULL if no STAs available on this device.
 *
 */
static const char *wano_sta_policy_current_link(const struct wano_sta_policy *sp)
{
    if (sp->sp_ifname[0] != '\0')
    {
        return sp->sp_ifname;
    }

    if (g_sta_list_len <= 0) return NULL;

    if (sp->sp_rotation_idx < 0 || sp->sp_rotation_idx >= g_sta_list_len)
    {
        return g_sta_list[0].vif;
    }

    return g_sta_list[sp->sp_rotation_idx].vif;
}

/* Determine the selected WANO policy and from that the current selected STA VIF */
static const char *wano_sta_compute_selected_sta(void)
{
    const char *selected_ifname = wano_sta_selected_policy_get_ifname();
    if (selected_ifname != NULL && selected_ifname[0] != '\0') return selected_ifname;

    if (g_sta_list_len <= 1) return NULL;

    struct wano_sta_policy *sp;
    struct wano_sta_policy *sel = NULL;
    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (!wano_sta_policy_should_attach(sp)) continue;
        if (sp->sp_ifname[0] != '\0') continue;
        if (sel == NULL || wano_sta_policy_outranks(sp, sel)) sel = sp;
    }
    return (sel != NULL) ? wano_sta_policy_current_link(sel) : NULL;
}

/* Determine the currently selected WANO policy. From that determine the
 * currently selected STA VIF. Then disable all other STA VIFs, except this one. */
static void wano_sta_disable_unselected_stas(void)
{
    const char *selected_sta = wano_sta_compute_selected_sta();
    int ii;

    if (selected_sta == NULL) return;

    for (ii = 0; ii < g_sta_list_len; ii++)
    {
        const char *vif = g_sta_list[ii].vif;
        if (strcmp(vif, selected_sta) == 0) continue;

        LOG(NOTICE, "wano_sta: Disabling non-selected STA VIF %s (selected_sta=%s)", vif, selected_sta);
        ovsdb_sync_delete_where(
                SCHEMA_TABLE(Wifi_VIF_Config),
                ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_Config, if_name), vif));
    }
}

/* =========================================================================
 * Phase 2: upsert Wifi_Credential_Config
 * ======================================================================= */
static bool wano_sta_upsert_credential_config(struct wano_sta_policy *sp)
{
    struct schema_Wifi_Credential_Config cconf;
    json_t *row;
    int idx = 0;

    MEMZERO(cconf);
    SCHEMA_SET_STR(cconf.ssid, sp->sp_ssid);
    SCHEMA_SET_INT(cconf.enabled, true);
    SCHEMA_SET_INT(cconf.priority, sp->sp_wan_priority);

    STRSCPY(cconf.security_keys[idx], "encryption");
    STRSCPY(cconf.security[idx], sp->sp_encryption[0] != '\0' ? sp->sp_encryption : "WPA-PSK");
    idx++;

    if (sp->sp_key[0] != '\0')
    {
        STRSCPY(cconf.security_keys[idx], "key");
        STRSCPY(cconf.security[idx], sp->sp_key);
        idx++;
    }
    cconf.security_len = idx;

    row = schema_Wifi_Credential_Config_to_json(&cconf, NULL);
    if (row == NULL)
    {
        LOG(ERR, "wano_sta: Failed to serialize Wifi_Credential_Config");
        return false;
    }

    if (!ovsdb_sync_upsert(
                SCHEMA_TABLE(Wifi_Credential_Config),
                SCHEMA_COLUMN(Wifi_Credential_Config, ssid),
                sp->sp_ssid,
                row,
                &sp->sp_cred_uuid))
    {
        LOG(ERR, "wano_sta: %s: Failed to upsert Wifi_Credential_Config ssid=%s", sp->sp_uuid, sp->sp_ssid);
        return false;
    }

    sp->sp_cred_valid = true;
    LOG(INFO,
        "wano_sta: %s: Upserted Wifi_Credential_Config ssid=%s uuid=%s",
        sp->sp_uuid,
        sp->sp_ssid,
        sp->sp_cred_uuid.uuid);
    return true;
}

/* Configure STA VIF Wifi_VIF_Config attached to Wifi_Radio_Config and link it with Wifi_Credential_Config */
static bool wano_sta_configure_vif_cred(struct wano_sta_policy *sp, const char *ifname, const char *radio)
{
    struct schema_Wifi_VIF_Config vconf;
    json_t *row;
    json_t *parent_where;

    /* Delete any existing Wifi_VIF_Config row first */
    ovsdb_sync_delete_where(
            SCHEMA_TABLE(Wifi_VIF_Config),
            ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_Config, if_name), ifname));

    /* Prepare Wifi_VIF_Config */
    MEMZERO(vconf);
    SCHEMA_SET_STR(vconf.if_name, ifname);
    SCHEMA_SET_STR(vconf.mode, "sta");
    SCHEMA_SET_INT(vconf.enabled, true);
    SCHEMA_SET_STR(vconf.ssid, ""); /* keep empty: Wifi_Credential_Config is used  */

    row = schema_Wifi_VIF_Config_to_json(&vconf, NULL);
    if (row == NULL) return false;

    parent_where = ovsdb_where_simple(SCHEMA_COLUMN(Wifi_Radio_Config, if_name), radio);

    /* Insert Wifi_VIF_Config for this STA with parent Wifi_Radio_Config->vif_configs */
    if (!ovsdb_sync_insert_with_parent(
                SCHEMA_TABLE(Wifi_VIF_Config),
                row,
                NULL,
                SCHEMA_TABLE(Wifi_Radio_Config),
                parent_where,
                SCHEMA_COLUMN(Wifi_Radio_Config, vif_configs)))
    {
        LOG(ERR, "wano_sta: %s: Failed to insert VIF with radio %s", ifname, radio);
        return false;
    }

    /* Mutate Wifi_VIF_Config->credential_configs with our Wifi_Credential_Config for this policy */
    if (ovsdb_sync_mutate_uuid_set(
                SCHEMA_TABLE(Wifi_VIF_Config),
                ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_Config, if_name), ifname),
                SCHEMA_COLUMN(Wifi_VIF_Config, credential_configs),
                OTR_INSERT,
                sp->sp_cred_uuid.uuid)
        <= 0)
    {
        LOG(ERR, "wano_sta: %s: Failed to insert credential_configs uuid", ifname);
        return false;
    }

    LOG(NOTICE, "wano_sta: %s: Configured mode=sta credential uuid=%s", ifname, sp->sp_cred_uuid.uuid);
    return true;
}

/* =========================================================================
 * Dynamic pipeline management
 * ======================================================================= */
static bool wano_sta_start_pipeline(struct wano_sta_vif *vif)
{
    if (vif->vs_ppline != NULL) return true;

    vif->vs_ppline = wano_sta_ppline_new(vif->vs_ifname, "vif");
    if (vif->vs_ppline == NULL)
    {
        LOG(ERR, "wano_sta: %s: Error creating STA pipeline", vif->vs_ifname);
        return false;
    }

    wano_sta_ppline_event_cb_init(vif->vs_ppline, wano_sta_ppline_event_cb);

    struct wano_sta_policy *sp = wano_sta_attached_policy();

    /* Set the controller-provisioned Wifi_Inet_Config role for the STA uplink, if any. */
    if (sp != NULL && sp->sp_inet_role_str[0] != '\0')
    {
        wano_sta_ppline_set_inet_role(vif->vs_ppline, sp->sp_inet_role_str);
    }

    /* Test connection: bring the uplink up and report its status, but never switch to it. */
    if (sp != NULL && sp->sp_test_connection)
    {
        wano_sta_ppline_set_test_connection(vif->vs_ppline, true);
    }

    if (!wano_sta_ppline_start(vif->vs_ppline))
    {
        LOG(ERR, "wano_sta: %s: Error starting STA pipeline", vif->vs_ifname);
        wano_sta_ppline_del(vif->vs_ppline);
        vif->vs_ppline = NULL;
        return false;
    }

    LOG(NOTICE, "wano_sta: %s: Started STA pipeline", vif->vs_ifname);
    return true;
}

static void wano_sta_stop_pipeline(struct wano_sta_vif *vif)
{
    if (vif->vs_ppline == NULL) return;

    /* Stops the pipeline (reverting inet config and removing the CMU row) and frees it */
    wano_sta_ppline_del(vif->vs_ppline);
    vif->vs_ppline = NULL;

    LOG(NOTICE, "wano_sta: %s: Stopped STA pipeline", vif->vs_ifname);
}

/* STA pipeline event reporting callback: log the event and update the
 * attached policy's last_status from the reported state. */
static void wano_sta_ppline_event_cb(
        wano_sta_ppline_t *ppline,
        enum wano_sta_ppline_event event,
        struct wano_sta_ppline_status status)
{
    LOG(NOTICE,
        "wano_sta: %s: STA pipeline event %s (has_L2=%d, has_L3=%d, conn_check_ok=%d)",
        wano_sta_ppline_if_name_get(ppline),
        wano_sta_ppline_event_str(event),
        status.has_L2,
        status.has_L3,
        status.conn_check_ok);

    struct wano_sta_policy *sp = wano_sta_attached_policy();
    enum wano_sta_status new_status;

    if (status.has_L3)
    {
        new_status = status.conn_check_ok ? WANO_STA_STATUS_CONNECTED_INTERNET_OK : WANO_STA_STATUS_L3_OK;
    }
    else if (status.has_L2)
    {
        new_status = WANO_STA_STATUS_CONNECTED_L2;
    }
    else
    {
        return;
    }

    if (sp != NULL && sp->sp_last_status != new_status)
    {
        sp->sp_last_status = new_status;
        wano_sta_policy_last_status_set(sp);
    }
}

/* =========================================================================
 * Start pipelines — iterate tracked VIFs, start on MLD interface or STA interface, as appropriate
 * ======================================================================= */
static void wano_sta_start_pipelines(void)
{
    struct wano_sta_vif *vif;
    ds_tree_iter_t iter;

    if (!g_sta_currently_associated)
    {
        LOG(INFO, "wano_sta: No associated STA yet, deferring pipeline start");
        return;
    }

    for (vif = ds_tree_ifirst(&iter, &g_wano_sta_vifs); vif != NULL; vif = ds_tree_inext(&iter))
    {
        if (!vif->vs_configured) continue; /* skip entry; it has vs_configured=false */

        char mld_ifname[C_IFNAME_LEN];
        MEMZERO(mld_ifname);

        // mld_ifname will be set if this VIF is part of MLO
        if (wano_sta_vif_is_in_mlo(vif->vs_ifname, mld_ifname, sizeof(mld_ifname))) /* MLO case */
        {
            /* Remove standalone pipeline on VIF STA if it exists */
            if (vif->vs_ppline != NULL)
            {
                LOG(INFO, "wano_sta: %s: MLO detected (%s), dropping standalone pipeline", vif->vs_ifname, mld_ifname);
                wano_sta_stop_pipeline(vif);
            }

            /* Find or create the MLD entry */
            struct wano_sta_vif *mld_vif = ds_tree_find(&g_wano_sta_vifs, mld_ifname);
            if (mld_vif == NULL)
            {
                mld_vif = CALLOC(1, sizeof(*mld_vif));
                STRSCPY(mld_vif->vs_ifname, mld_ifname);
                STRSCPY(mld_vif->vs_radio, vif->vs_radio);
                ds_tree_insert(&g_wano_sta_vifs, mld_vif, mld_vif->vs_ifname);
            }

            /* Start the actual pipeline on the MLD interface, not the individual STA VIF */
            wano_sta_start_pipeline(mld_vif);
        }
        else /* Non-MLO case */
        {
            /* Non-MLO: pipeline on STA VIF directly. */
            wano_sta_start_pipeline(vif);
        }
    }
}

/* Delete this policy's Wifi_Credential_Config row (if any). */
static void wano_sta_delete_credential_config(struct wano_sta_policy *sp)
{
    if (!sp->sp_cred_valid) return;

    ovsdb_sync_delete_where(SCHEMA_TABLE(Wifi_Credential_Config), ovsdb_where_uuid("_uuid", sp->sp_cred_uuid.uuid));
    sp->sp_cred_valid = false;
}

/* Attach this WANO STA policy */
static bool wano_sta_policy_attach(struct wano_sta_policy *sp)
{
    if (sp->sp_attached) return true;

    /* Safety net to make sure we never touch STA VIF if somehow CM still manages backhaul CMU. */
    if (!g_cm_bh_cmu_disabled)
    {
        LOG(ERR,
            "wano_sta: %s: CM has not acked %s -- refusing to attach policy",
            sp->sp_uuid,
            WANO_STA_CM_CFG_DISABLE);
        return false;
    }

    /* Upsert Wifi_Credential_Config */
    if (!wano_sta_upsert_credential_config(sp))  // credentials for this policy
        return false;

    /* For this policy, get the current link (VIF) name we want to try */
    const char *targetVIFname = wano_sta_policy_current_link(sp);

    /* Find the radio hosting the target VIF */
    const char *radio = (targetVIFname != NULL) ? wano_sta_radio_for_vif(targetVIFname) : NULL;
    if (radio == NULL)
    {
        LOG(WARN, "wano_sta: %s: radio not found for the selected STA VIF for the selected policy", sp->sp_uuid);
        wano_sta_delete_credential_config(sp);
        return false;
    }

    /*
     * Allow the STA VIF to scan/roam on channels outside this radio's current operating channel.
     *
     * This way we don't have to temporarily disable any AP VIFs on the same radio
     * for the STA to be able to scan for arbitrary channel the backup hotspot SSID may be on.
     */
    if (!set_allow_sta_roam_channels(radio, true))
    {
        wano_sta_delete_credential_config(sp);
        return false;
    }

    /* Configure STA VIF Wifi_VIF_Config row attached to Wifi_Radio_Config and link it with Wifi_Credential_Config */
    if (!wano_sta_configure_vif_cred(sp, targetVIFname, radio))
    {
        /* Revert: don't leave allow_sta_roam_channels set if we're not actually attaching. */
        set_allow_sta_roam_channels(radio, false);
        wano_sta_delete_credential_config(sp);
        return false;
    }

    /* Track this STA VIF */
    struct wano_sta_vif *vif = ds_tree_find(&g_wano_sta_vifs, targetVIFname);
    if (vif == NULL)
    {
        vif = CALLOC(1, sizeof(*vif));
        STRSCPY(vif->vs_ifname, targetVIFname);
        STRSCPY(vif->vs_radio, radio);
        ds_tree_insert(&g_wano_sta_vifs, vif, vif->vs_ifname);
    }
    vif->vs_configured = true;

    sp->sp_attached = true;
    return true;
}

static void wano_sta_policy_detach(struct wano_sta_policy *sp)
{
    const char *ifname;

    if (!sp->sp_attached && !sp->sp_cred_valid) return;

    /* Get the current effective STA link for this policy: */
    ifname = wano_sta_policy_current_link(sp);

    /* Remove credential configuration for this STA link: */
    if (sp->sp_cred_valid)
    {
        if (ifname != NULL)
        {
            if (ovsdb_sync_mutate_uuid_set(
                        SCHEMA_TABLE(Wifi_VIF_Config),
                        ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_Config, if_name), ifname),
                        SCHEMA_COLUMN(Wifi_VIF_Config, credential_configs),
                        OTR_DELETE,
                        sp->sp_cred_uuid.uuid)
                <= 0)
                LOG(WARN, "wano_sta: %s: Failed to remove credential uuid=%s", ifname, sp->sp_cred_uuid.uuid);
        }

        ovsdb_sync_delete_where(SCHEMA_TABLE(Wifi_Credential_Config), ovsdb_where_uuid("_uuid", sp->sp_cred_uuid.uuid));
        sp->sp_cred_valid = false;
    }

    if (ifname != NULL)
    {
        /* Delete Wifi_VIF_Config row for this STA link: */
        struct wano_sta_vif *vif;

        if (!ovsdb_sync_delete_where(
                    SCHEMA_TABLE(Wifi_VIF_Config),
                    ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_Config, if_name), ifname)))
            LOG(WARN, "wano_sta: %s: Failed to delete Wifi_VIF_Config", ifname);
        else
            LOG(INFO, "wano_sta: %s: Deleted Wifi_VIF_Config", ifname);

        /* Untrack this STA VIF and make sure any STA pipeline is stopped */
        vif = ds_tree_find(&g_wano_sta_vifs, ifname);
        if (vif != NULL)
        {
            wano_sta_stop_pipeline(vif);
            ds_tree_remove(&g_wano_sta_vifs, vif);
            FREE(vif);
        }
    }

    if (ifname != NULL)
    {
        const char *radio = wano_sta_radio_for_vif(ifname);
        if (radio != NULL) set_allow_sta_roam_channels(radio, false);
    }

    /* Clear the STA uplink status in OVSDB (policy no longer active/attached). */
    wano_sta_policy_active_status_set(sp, NULL, NULL, NULL);

    /* Reset STA association state: */
    g_sta_any_associated = false;
    g_sta_currently_associated = false;

    sp->sp_attached = false;
}

/* Detach any policy that should no longer be attached.
 *
 * Returns true if on the other hand there is a policy that
 * should be attached but currently it is not.
 */
static bool wano_sta_detach_stale_policies(void)
{
    struct wano_sta_policy *sp;
    bool pending_attach = false;

    ds_tree_foreach (&g_sta_policies, sp)
    {
        const bool want_attach = wano_sta_policy_should_attach(sp);

        if (want_attach && !sp->sp_attached)
            pending_attach = true;
        else if (!want_attach && sp->sp_attached)
            wano_sta_policy_detach(sp);
    }

    return pending_attach;
}

static void wano_sta_clear_credentials_config(void)
{
    LOG(INFO, "wano_sta: Clearing Wifi_Credential_Config");
    ovsdb_sync_delete_where(SCHEMA_TABLE(Wifi_Credential_Config), NULL);
}

/* =========================================================================
 * Phase 2 entry: upsert credentials, configure VIFs, start scan timer
 * ======================================================================= */
static void wano_sta_set_credentials_and_configure(void)
{
    struct wano_sta_policy *sp;
    int scan_timeout;

    /* Reset all credentials before applying WAN STA credentials. */
    wano_sta_clear_credentials_config();

    /* Iterate through configured WAN STA policies,
     * find the one that should be attached, and attach it. */
    ds_tree_foreach (&g_sta_policies, sp)
    {
        if (!wano_sta_policy_should_attach(sp))
        {
            continue;
        }
        if (sp->sp_attached)
        {
            continue;
        }

        if (!wano_sta_policy_attach(sp))
        {
            LOG(WARN, "wano_sta: %s: failed attaching policy", sp->sp_uuid);
        }
    }

    g_sta_state = WSTA_S_CRED_SET;

    /* Re(start) scan window timer. Scan window is max time we give for the STA to associate */
    scan_timeout = wano_sta_scan_timeout_get();
    ev_timer_stop(EV_DEFAULT, &g_sta_scan_timer);
    ev_timer_set(&g_sta_scan_timer, (double)scan_timeout, 0.0);
    ev_timer_start(EV_DEFAULT, &g_sta_scan_timer);

    LOG(INFO, "wano_sta: Scan window started (%ds)", scan_timeout);
}

/* =========================================================================
 * Apply — state machine dispatch
 * ======================================================================= */
static void wano_sta_apply_state_dispatch(void)
{
    if (wano_sta_wanted_policy_count() <= 0) return;

    if (g_sta_list_len == 0)
    {
        LOG(WARN, "wano_sta: No STA interfaces configured in bootstrap");
        return;
    }

    switch (g_sta_state)
    {
        case WSTA_S_IDLE:
            /* Leave a single selected STA VIF enabled and configure its credentials.
             * Any enabled AP VIFs on the same radio are left up, the STA scans all
             * channels (scan_cur_freq=0)  */

            LOG(INFO, "wano_sta: apply: WSTA_S_IDLE: Configuring credentials and enabling selected STA VIF");
            wano_sta_disable_unselected_stas();
            wano_sta_set_credentials_and_configure();
            break;

        case WSTA_S_CRED_SET: {
            LOG(INFO,
                "wano_sta: apply: WSTA_S_CRED_SET: Credentials already set, attaching new policies / starting "
                "pipelines");

            /* Attach any newly added policies -- the scan window already started for the first policy. */
            struct wano_sta_policy *sp;
            ds_tree_foreach (&g_sta_policies, sp)  // TODO: Consider removing, remnant of the past
            {
                if (sp->sp_attached) continue;
                if (!wano_sta_policy_should_attach(sp)) continue;
                if (!wano_sta_policy_attach(sp))
                    LOG(WARN, "wano_sta: %s: failed attaching new policy in CRED_SET state", sp->sp_uuid);
            }

            /* Start WANO pipelines */
            wano_sta_start_pipelines();
            break;
        }

        default:
            LOG(WARN, "wano_sta: apply: Unknown state: %d", g_sta_state);
            break;
    }
}

/* =========================================================================
 * Teardown
 * ======================================================================= */
static void wano_sta_teardown(void)
{
    struct wano_sta_vif *vif;
    ds_tree_iter_t iter;

    /* Stop scan window timer */
    ev_timer_stop(EV_DEFAULT, &g_sta_scan_timer);

    /* Stop pipelines (this reverts the STA uplink interface inet dhcp config,
     * removes any CMU row and the policy routing rules). Untrack STA VIFs */
    for (vif = ds_tree_ifirst(&iter, &g_wano_sta_vifs); vif != NULL; vif = ds_tree_inext(&iter))
    {
        wano_sta_stop_pipeline(vif);

        ds_tree_iremove(&iter);
        FREE(vif);
    }

    /* Delete all mode==sta Wifi_VIF_Config rows
     * By now, detaching WANO policies have cleaned up their respective Wifi_VIF_Config rows,
     * but do a general cleanup at this point anyway. This is GW and usually AP mode,
     * so we don't want any leftover STA VIFs.
     */
    LOG(INFO, "wano_sta: teardown: Deleting all mode=sta Wifi_VIF_Config rows");
    ovsdb_sync_delete_where(
            SCHEMA_TABLE(Wifi_VIF_Config),
            ovsdb_where_simple(SCHEMA_COLUMN(Wifi_VIF_Config, mode), "sta"));

    /* Clear all credential configurations as well. */
    wano_sta_clear_credentials_config();

    /* Reset association state: */
    g_sta_any_associated = false;
    g_sta_currently_associated = false;

    /* Reset per-policy cascade and rotation state */
    {
        struct wano_sta_policy *sp;
        ds_tree_foreach (&g_sta_policies, sp)
        {
            sp->sp_failed = false;
            sp->sp_rotation_idx = 0;
            sp->sp_rotation_exhausted = false;
        }
    }

    g_sta_state = WSTA_S_IDLE;
    LOG(NOTICE, "wano_sta: Teardown complete");
}

/*
 * Scan window timer callback -- fires when the STA did not associate (or a
 * previously-associated link did not recover) within the scan window.
 */
static void wano_sta_scan_timer_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;

    LOG(INFO, "wano_sta: Scan window expired");

    struct wano_sta_policy *attached_policy = wano_sta_attached_policy();
    if (attached_policy != NULL && attached_policy->sp_last_status == WANO_STA_STATUS_UNKNOWN)
    {
        attached_policy->sp_last_status = WANO_STA_STATUS_ERR_WIFI;
        attached_policy->sp_last_wifi_err[0] = '\0';
        wano_sta_policy_last_status_set(attached_policy);
    }

    wano_sta_on_scan_timeout_expire_or_failure();
}

/*
 * To be called when the current STA VIF connection attempt has ended without an
 * association (either scan window timeout expired or a WiFi error occurred).
 *
 * Drives sequential rotation to the next STA candidate and the multi-policy
 * cascade.
 */
static void wano_sta_on_scan_timeout_expire_or_failure(void)
{
    /* Two cases
     *   (a) Initial scan window expired (or WiFi failure) without ever associating.
     *   (b) Post-association recovery: We WERE associated this cycle
     *       (g_sta_any_associated) but the link went away
     *       (g_sta_currently_associated is false) and did not recover within
     *       the scan window.
     *
     * Both are handled the same way: for each attached policy (normally only 1
     * is attached), advance sequential rotation to the next link if possible;
     * otherwise mark the policy failed and let the multi-policy cascade run. */
    const bool initial_scan_failed = !g_sta_any_associated;
    const bool post_assoc_recovery = g_sta_any_associated && !g_sta_currently_associated;

    if (initial_scan_failed || post_assoc_recovery)
    {
        struct wano_sta_policy *sp;
        bool any_advanced = false;
        bool any_failed = false;

        /* Post-assoc recovery of the same STA failed as well: reset any_associated -- for the next rotation*/
        if (post_assoc_recovery)
        {
            LOG(NOTICE,
                "wano_sta: post-association disassoc unrecovered within scan window -- restarting rotation cycle");
            g_sta_any_associated = false;
        }

        ds_tree_foreach (&g_sta_policies, sp)
        {
            if (!sp->sp_attached) continue;

            /* sp now points at the attached policy (for which the link attempt failed) */

            /* Either we never associated (with a known error or timeout) or we lost association
             * and did not recover: a WiFi connect error. */

            /* The currently attached policy:
             *  - No pinned VIF: Do sequential rotation to the next STA VIF candidate, if any
             *  - pinned VIF: NO rotation - the policy fails in one shot */
            if (sp->sp_ifname[0] == '\0' && sp->sp_rotation_idx + 1 < g_sta_list_len)
            {
                const char *cur_link = g_sta_list[sp->sp_rotation_idx].vif;
                const char *next_link = g_sta_list[sp->sp_rotation_idx + 1].vif;

                LOG(NOTICE,
                    "wano_sta: %s (ssid=%s): No association on %s (%s), rotating to %s",
                    sp->sp_uuid,
                    sp->sp_ssid,
                    cur_link,
                    sp->sp_last_wifi_err[0] != '\0' ? sp->sp_last_wifi_err : "scan window expired",
                    next_link);

                /* We will detach this policy first as detaching means we disable/deconfigure
                 * the previously configured underlying STA VIF */
                wano_sta_policy_detach(sp);

                /* Advance rotation index to the next STA VIF candidate. */
                sp->sp_rotation_idx++;

                /* Disable all but the currently selected STA VIF as determined
                 * by the just advanced rotation index */
                wano_sta_disable_unselected_stas();

                any_advanced = true;
                continue;
            }

            /* Out of rotation candidates (or pinned STA VIF) for this policy.
             *
             * Mark the policy failed, reset its rotation state, detach it.
             *
             * Let the multi-policy cascade move on to the next eligible policy.
             */
            sp->sp_rotation_exhausted = true;

            LOG(NOTICE,
                "wano_sta: %s (ssid=%s): no association on any link (priority=%d, ifname=%s) — marking failed, "
                "cascading",
                sp->sp_uuid,
                sp->sp_ssid,
                sp->sp_wan_priority,
                sp->sp_ifname[0] ? sp->sp_ifname : "(any)");

            sp->sp_failed = true;
            wano_sta_policy_detach(sp);
            sp->sp_rotation_idx = 0;
            sp->sp_rotation_exhausted = false;

            any_failed = true;
        }

        if (any_failed)
        {
            /* If no eligible policy remains, reset all sp_failed flags so the
             * cascade loops back to the highest priority. */
            if (!wano_sta_any_eligible())
            {
                const int n = wano_sta_reset_failed_all();
                LOG(NOTICE,
                    "wano_sta: cascade exhausted, reset %d sp_failed flag(s) — looping back to highest priority",
                    n);
            }
        }

        if (any_advanced || any_failed)
        {
            /* Schedule apply */
            g_sta_state = WSTA_S_IDLE;
            wano_sta_schedule_apply();
        }
    }
}

/* Schedule WANO STA apply */
static void wano_sta_schedule_apply(void)
{
    ev_timer_set(&g_sta_apply_timer, 0.0, 0.0);
    ev_timer_start(EV_DEFAULT, &g_sta_apply_timer);
}

static void wano_sta_apply(void)
{
    /* We allow at most 1 test connection policy configured at a time. */
    const int test_connection_count = wano_sta_test_connection_policy_count();
    if (test_connection_count > 1)
    {
        LOG(ERR,
            "wano_sta: %d test_connection policies configured, at most 1 is allowed -- doing nothing",
            test_connection_count);
        return;
    }

    /* Detach policies that should no longer be attached; and check whether any
     * policy still needs attaching (which must go through the state machine). */
    const bool pending_attach = wano_sta_detach_stale_policies();

    const int wanted_count = wano_sta_wanted_policy_count();
    const int attached_count = wano_sta_attached_policy_count();

    if (wanted_count <= 0)
    {
        if (attached_count > 0 || g_sta_state != WSTA_S_IDLE)
        {
            wano_sta_teardown();  // will transition to WSTA_S_IDLE
        }
        return;
    }

    /* Hold off the attach until CM acks that its backhaul CMU management is disarmed. */
    if (!g_cm_bh_cmu_disabled)
    {
        wano_sta_set_cm_bh_cmu_disable(true); /* make sure the request is out */
        LOG(INFO, "wano_sta: Waiting for CM %s ack before attaching", WANO_STA_CM_CFG_DISABLE);
        return;
    }

    if (pending_attach || g_sta_state != WSTA_S_CRED_SET || attached_count < wanted_count)
    {
        wano_sta_apply_state_dispatch();
    }
    else
    {
        wano_sta_start_pipelines();
    }
}

static void wano_sta_apply_timer_fn(struct ev_loop *loop, ev_timer *w, int revent)
{
    (void)loop;
    (void)w;
    (void)revent;

    LOG(DEBUG, "wano_sta: Apply timer callback called");

    wano_sta_apply();
}

/* Determine whether the given Wifi_VIF_State::state represents a failure. */
static bool wano_sta_vif_state_is_failure(const char *state)
{
    if (state == NULL || state[0] == '\0') return false;
    if (strcmp(state, "scanning") == 0) return false;
    if (strcmp(state, "connecting") == 0) return false;
    if (strcmp(state, "connected") == 0) return false;
    if (strcmp(state, "disconnected") == 0) return false;
    return true;
}

/* Wifi_VIF_State monitor callback. */
static void callback_Wifi_VIF_State(
        ovsdb_update_monitor_t *mon,
        struct schema_Wifi_VIF_State *old,
        struct schema_Wifi_VIF_State *new)
{
    if (ds_tree_is_empty(&g_sta_policies)) return;
    if (mon->mon_type == OVSDB_UPDATE_DEL) return;
    if (new == NULL) return;
    if (!new->mode_exists) return;

    /* STA VIF config change: */
    if (strcmp(new->mode, "sta") == 0)
    {
        if (wano_sta_is_managed_vif(new->if_name)) /* If this is a STA VIF we care about */
        {
            const bool now_assoc = new->parent_exists &&new->parent[0] != '\0';

            /* Report the connection status of the currently attached policy. */
            struct wano_sta_policy *attached_policy = wano_sta_attached_policy();
            if (attached_policy != NULL)
            {
                wano_sta_policy_active_status_set(
                        attached_policy,
                        new->if_name,
                        new->mld_if_name,
                        now_assoc ? "connected" : "connecting");
            }
            else
            {
                /* Not really expected, log WARN */
                LOG(WARN, "wano_sta: %s: VIF state update but no attached policy to report status for", new->if_name);
            }

            /* STA VIF parent MAC appeared --> associated. */
            if (now_assoc)
            {
                /* We are associated */

                LOG(NOTICE, "wano_sta: %s: Associated (parent=%s)", new->if_name, new->parent);

                /* Assoc + 4-way handshake success: report last_status "connected".
                 * Upgrade only: A VIF state update while associated should
                 * not downgrade an already reported L3/internet status. */
                if (attached_policy != NULL && attached_policy->sp_last_status < WANO_STA_STATUS_CONNECTED_L2)
                {
                    attached_policy->sp_last_status = WANO_STA_STATUS_CONNECTED_L2;
                    wano_sta_policy_last_status_set(attached_policy);
                }

                g_sta_currently_associated = true;
                g_sta_any_associated = true;
                /* Associated -- stop the scan timer so we don't rotate away. */
                ev_timer_stop(EV_DEFAULT, &g_sta_scan_timer);
                wano_sta_schedule_apply();
                return;
            }

            /* We are NOT associated */

            if (g_sta_currently_associated)
            {
                /* We were associated via this STA, but later lost association and we are NOT associated at this point.
                 *
                 * Start/restart the scan timer to give it a chance to recover on the same STA VIF.
                 * If it doesn't, the scan-timer callback will handle rotation/failure.
                 */

                g_sta_currently_associated = false;

                const double timer_s = (double)wano_sta_scan_timeout_get();

                LOG(NOTICE,
                    "wano_sta: %s: Disassociated -- re-arming scan timer (%.0fs) for rotation if no recovery",
                    new->if_name,
                    timer_s);

                ev_timer_stop(EV_DEFAULT, &g_sta_scan_timer);
                ev_timer_set(&g_sta_scan_timer, timer_s, 0.0);
                ev_timer_start(EV_DEFAULT, &g_sta_scan_timer);
            }

            /* Wifi connection failure for the current STA VIF detected via Wifi_VIF_State::state: */
            if (attached_policy != NULL && new->state_exists && wano_sta_vif_state_is_failure(new->state)
                && ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, state)))
            {
                const char *cur_link = wano_sta_policy_current_link(attached_policy);

                if (cur_link != NULL && strcmp(cur_link, new->if_name) == 0)
                {
                    LOG(NOTICE, "wano_sta: %s: WiFi failure: %s", new->if_name, new->state);

                    /* Report the detail WiFi failure reason in OVSDB: */
                    attached_policy->sp_last_status = WANO_STA_STATUS_ERR_WIFI;
                    STRSCPY(attached_policy->sp_last_wifi_err, new->state);
                    wano_sta_policy_last_status_set(attached_policy);
                }
            }
        }

        /* mld_if_name became available - schedule apply for pipeline start */
        if (new->mld_if_name_exists &&new->mld_if_name[0] != '\0'
            && ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, mld_if_name)))
        {
            LOG(INFO, "wano_sta: %s: mld_if_name=%s updated, scheduling apply", new->if_name, new->mld_if_name);
            wano_sta_schedule_apply();
        }
    }
}

/* =========================================================================
 * Connectivity_Check monitor -- conditional failover
 * ======================================================================= */
static void callback_Connectivity_Check(
        ovsdb_update_monitor_t *mon,
        struct schema_Connectivity_Check *old,
        struct schema_Connectivity_Check *new)
{
    const char *name;
    const char *status;

    name = (mon->mon_type == OVSDB_UPDATE_DEL) ? old->name : new->name;
    status = (mon->mon_type == OVSDB_UPDATE_DEL || !new->status_exists) ? "deleted" : new->status;
    LOG(INFO, "wano_sta: CC '%s' status update: %s", name, status);

    wano_sta_schedule_apply();
}

/*
 * Set this policy's active STA uplink status into its WAN_Config->other_status
 * key "sta_uplink_active" (set only for a currently active/attached policy):
 *
 *   value = "<status>:<vif_name>[:<mld_if_name>]"
 *   e.g.    "connecting:bhaul-sta-50:mld0"  or  "connected:bhaul-sta-50" if mld_if_name is NULL
 *
 *  - `status` is "connecting" or "connected" or anything else: policy active (attached)
 *  - `status` == null --> policy not active (attached): sta_uplink_active key removed
 */
static void wano_sta_policy_active_status_set(
        struct wano_sta_policy *sp,
        const char *vif_name,
        const char *mld_if_name,
        const char *status)
{
    const char *wan_config_uuid = sp->sp_uuid;
    json_t *mutations;
    json_t *result;
    int count;

    mutations = json_array();

    /* Delete any existing sta_uplink_active entry first */
    json_array_append_new(
            mutations,
            ovsdb_mutation(
                    SCHEMA_COLUMN(WAN_Config, other_status),
                    json_string("delete"),
                    json_pack("[s,[s]]", "set", "sta_uplink_active")));

    if (status != NULL)
    {
        char value[C_IFNAME_LEN * 2 + 16];

        if (mld_if_name != NULL && mld_if_name[0] != '\0')
            snprintf(value, sizeof(value), "%s:%s:%s", status, vif_name, mld_if_name);
        else
            snprintf(value, sizeof(value), "%s:%s", status, vif_name);

        json_array_append_new(
                mutations,
                ovsdb_mutation(
                        SCHEMA_COLUMN(WAN_Config, other_status),
                        json_string("insert"),
                        json_pack("[s,[[s,s]]]", "map", "sta_uplink_active", value)));
    }

    result = ovsdb_tran_call_s(
            SCHEMA_TABLE(WAN_Config),
            OTR_MUTATE,
            ovsdb_where_uuid("_uuid", wan_config_uuid),
            mutations);
    count = ovsdb_get_update_result_count(result, SCHEMA_TABLE(WAN_Config), "mutate");
    json_decref(result);

    if (count <= 0)
    {
        LOG(WARN,
            "wano_sta: %s: Failed to update other_status:sta_uplink_active (status=%s)",
            wan_config_uuid,
            status != NULL ? status : "(cleared)");
        return;
    }

    LOG(INFO,
        "wano_sta: %s: other_status:sta_uplink_active = %s",
        wan_config_uuid,
        status != NULL ? status : "(cleared)");
}

/*
 * Report this policy's last connection status into its WAN_Config->other_status
 * key "last_status". The key is set for every policy that has ever been tried.
 *
 * Expects sp->sp_last_status to be set; WANO_STA_STATUS_UNKNOWN removes the
 * key.
 */
static void wano_sta_policy_last_status_set(struct wano_sta_policy *sp)
{
    const char *status_str = wano_sta_status_str(sp->sp_last_status);
    json_t *mutations;
    json_t *result;
    int count;

    if (sp->sp_last_status == WANO_STA_STATUS_ERR_WIFI && sp->sp_last_wifi_err[0] != '\0')
    {
        status_str = sp->sp_last_wifi_err; /* Detail WiFi error reason available */
    }

    mutations = json_array();

    /* Delete any existing last_status entry first */
    json_array_append_new(
            mutations,
            ovsdb_mutation(
                    SCHEMA_COLUMN(WAN_Config, other_status),
                    json_string("delete"),
                    json_pack("[s,[s]]", "set", "last_status")));

    if (status_str != NULL)
    {
        json_array_append_new(
                mutations,
                ovsdb_mutation(
                        SCHEMA_COLUMN(WAN_Config, other_status),
                        json_string("insert"),
                        json_pack("[s,[[s,s]]]", "map", "last_status", status_str)));
    }

    result = ovsdb_tran_call_s(SCHEMA_TABLE(WAN_Config), OTR_MUTATE, ovsdb_where_uuid("_uuid", sp->sp_uuid), mutations);
    count = ovsdb_get_update_result_count(result, SCHEMA_TABLE(WAN_Config), "mutate");
    json_decref(result);

    if (count <= 0)
    {
        LOG(WARN,
            "wano_sta: %s: Failed to update other_status:last_status (status=%s)",
            sp->sp_uuid,
            status_str != NULL ? status_str : "(cleared)");
        return;
    }

    LOG(INFO,
        "wano_sta: %s: other_status:last_status = %s",
        sp->sp_uuid,
        status_str != NULL ? status_str : "(cleared)");
}

/* Get wano_sta policy from internal bookkeeping, create a new one, if needed */
static struct wano_sta_policy *wano_sta_policy_get_or_add(const char *policy_uuid)
{
    struct wano_sta_policy *sp = wano_sta_policy_get(policy_uuid);
    if (sp != NULL) return sp;

    sp = CALLOC(1, sizeof(*sp));
    STRSCPY(sp->sp_uuid, policy_uuid);
    sp->sp_scan_timeout = WANO_STA_SCAN_TIMEOUT_DEFAULT;
    ds_tree_insert(&g_sta_policies, sp, sp->sp_uuid);
    return sp;
}

/* Get/find wano_sta policy from internal bookkeeping */
static struct wano_sta_policy *wano_sta_policy_get(const char *policy_uuid)
{
    return ds_tree_find(&g_sta_policies, policy_uuid);
}

/* Copy config into the policy; *changed reports whether association-affecting fields changed. */
static void wano_sta_policy_set(
        struct wano_sta_policy *sp,
        int wan_priority,
        const struct wano_wan_config_sta *sta_cfg,
        bool *changed)
{
    *changed = false;

    sp->sp_wan_priority = wan_priority;

    *changed |= (strcmp(sp->sp_ssid, sta_cfg->wc_ssid) != 0);
    STRSCPY(sp->sp_ssid, sta_cfg->wc_ssid);

    *changed |= (strcmp(sp->sp_key, sta_cfg->wc_key) != 0);
    *changed |= (strcmp(sp->sp_encryption, sta_cfg->wc_encryption) != 0);
    *changed |= (strcmp(sp->sp_ifname, sta_cfg->wc_ifname) != 0);
    *changed |= (strcmp(sp->sp_inet_role_str, sta_cfg->wc_inet_role) != 0);
    *changed |= (sp->sp_test_connection != sta_cfg->wc_test_connection);

    sp->sp_key[0] = '\0';
    sp->sp_encryption[0] = '\0';
    sp->sp_ifname[0] = '\0';
    sp->sp_connectivity_check[0] = '\0';
    sp->sp_inet_role_str[0] = '\0';
    sp->sp_scan_timeout = WANO_STA_SCAN_TIMEOUT_DEFAULT;
    sp->sp_test_connection = sta_cfg->wc_test_connection;

    if (sta_cfg->wc_key[0] != '\0') STRSCPY(sp->sp_key, sta_cfg->wc_key);
    if (sta_cfg->wc_encryption[0] != '\0') STRSCPY(sp->sp_encryption, sta_cfg->wc_encryption);
    if (sta_cfg->wc_ifname[0] != '\0') STRSCPY(sp->sp_ifname, sta_cfg->wc_ifname);

    /* A STA test connection policy is always unconditional (except in route
     * switching sense as we do not switch traffic to it). The controller is
     * expected not to attach a Connectivity_Check (via main uplink) to it,
     * as it does not make sense. If it would, we will simply ignore that connectivity check. */
    if (sta_cfg->wc_connectivity_check[0] != '\0' && !sp->sp_test_connection)
    {
        STRSCPY(sp->sp_connectivity_check, sta_cfg->wc_connectivity_check);
    }
    else if (sta_cfg->wc_connectivity_check[0] != '\0')
    {
        LOG(NOTICE,
            "wano_sta: %s: test_connection policy: ignoring connectivity_check=%s",
            sp->sp_uuid,
            sta_cfg->wc_connectivity_check);
    }

    if (sta_cfg->wc_inet_role[0] != '\0') STRSCPY(sp->sp_inet_role_str, sta_cfg->wc_inet_role);
    if (sta_cfg->wc_scan_timeout_exists) sp->sp_scan_timeout = sta_cfg->wc_scan_timeout;
}

static void wano_sta_policy_remove(struct wano_sta_policy *sp)
{
    ds_tree_remove(&g_sta_policies, sp);
    FREE(sp);
}

/* Public API -- called from wano_wan on WAN_Config type=sta changes */
void wano_sta_config_update(const char *policy_uuid, int wan_priority, const struct wano_wan_config_sta *sta_cfg)
{
    struct wano_sta_policy *sp;
    bool changed;

    if (!g_sta_initialized)
    {
        LOG(WARN, "wano_sta: Config update received before init, ignoring.");
        return;
    }

    sp = wano_sta_policy_get_or_add(policy_uuid);
    if (sp == NULL) return;

    /* At least one STA policy present: CM backhaul CMU management must be off. */
    wano_sta_set_cm_bh_cmu_disable(true);

    const bool priority_changed = (sp->sp_wan_priority != wan_priority);

    wano_sta_policy_set(sp, wan_priority, sta_cfg, &changed);

    /* Invalidate last status for this policy */
    if (changed && sp->sp_last_status != WANO_STA_STATUS_UNKNOWN)
    {
        sp->sp_last_status = WANO_STA_STATUS_UNKNOWN;
        sp->sp_last_wifi_err[0] = '\0';
        wano_sta_policy_last_status_set(sp);
    }

    if (changed && sp->sp_attached)
    {
        wano_sta_policy_detach(sp);
        /* Credentials changed — restart rotation from link 0 on the next attach. */
        sp->sp_rotation_idx = 0;
        sp->sp_rotation_exhausted = false;
    }
    else if (priority_changed && sp->sp_cred_valid)
    {
        wano_sta_upsert_credential_config(sp);
    }

    wano_sta_policy_warn_if_ssid_shadowed(sp);

    LOG(INFO,
        "wano_sta: Policy %s update ssid=%s ifname=%s cc=%s priority=%d",
        sp->sp_uuid,
        sp->sp_ssid,
        sp->sp_ifname[0] != '\0' ? sp->sp_ifname : "(all)",
        sp->sp_connectivity_check[0] != '\0' ? sp->sp_connectivity_check : "(unconditional)",
        sp->sp_wan_priority);

    wano_sta_schedule_apply();
}

/* Public API -- called from wano_wan on WAN_Config type=sta removal */
void wano_sta_config_remove(const char *policy_uuid)
{
    struct wano_sta_policy *sp;

    if (!g_sta_initialized)
    {
        LOG(WARN, "wano_sta: Config remove received before init, ignoring.");
        return;
    }

    sp = wano_sta_policy_get(policy_uuid);
    if (sp == NULL) return;

    LOG(INFO, "wano_sta: Policy %s removed", policy_uuid);
    wano_sta_policy_detach(sp);
    wano_sta_policy_remove(sp);

    /* Last STA policy gone: hand backhaul CMU management back to CM. */
    if (ds_tree_is_empty(&g_sta_policies))
    {
        wano_sta_set_cm_bh_cmu_disable(false);
    }

    wano_sta_schedule_apply();
}

bool wano_sta_init(void)
{
    if (g_sta_initialized) return true;

    LOG(INFO, "wano_sta: Initializing");

    OVSDB_TABLE_INIT(Wifi_Radio_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_VIF_Config, if_name);
    OVSDB_TABLE_INIT(Wifi_VIF_State, if_name);
    OVSDB_TABLE_INIT(Connectivity_Check, name);

    ev_timer_init(&g_sta_apply_timer, wano_sta_apply_timer_fn, 0.0, 0.0);
    ev_timer_init(&g_sta_scan_timer, wano_sta_scan_timer_fn, 0.0, 0.0);

    /* Wifi_VIF_State monitoring */
    OVSDB_TABLE_MONITOR_F(Wifi_VIF_State, C_VPACK("+", "if_name", "mode", "enabled", "mld_if_name", "parent", "state"));

    /* Connectivity_Check status monitoring */
    OVSDB_TABLE_MONITOR_F(Connectivity_Check, C_VPACK("+", "name", "status"));

    /* CM backhaul CMU enable/disable init */
    wano_sta_cm_bh_cmu_init();

    wano_sta_parse_list();
    g_sta_initialized = true;

    return true;
}
