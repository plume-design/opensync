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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ev.h>
#include <jansson.h>

#include <ds_tree.h>
#include <os.h>
#include <log.h>
#include <memutil.h>
#include <schema.h>
#include <schema_consts.h>
#include <ovsdb.h>
#include <ovsdb_sync.h>

#include "cm2_bh_dhcp.h"
#include "cm2_bh_macros.h"

#define CM2_BH_DHCP_BACKOFF_SEC  3.0
#define CM2_BH_DHCP_DEADLINE_SEC 3.0

#define LOG_PREFIX(m, fmt, ...) "cm2: bh: dhcp: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_VIF(vif, fmt, ...)                                                                       \
    LOG_PREFIX(                                                                                             \
            (vif)->m,                                                                                       \
            "%s [%s%s%s%s%s%s%s%s%s]: " fmt,                                                                \
            (vif)->vif_name,                                                                                \
            (vif)->work ? "W" : " ",                                                                        \
            (vif)->state.is_active ? "A" : " ",                                                             \
            (vif)->state.is_4addr ? "4" : " ",                                                              \
            (vif)->state.is_sta ? "S" : " ",                                                                \
            (vif)->state.is_configurable ? "C" : " ",                                                       \
            (vif)->state.is_enabled ? "E" : " ",                                                            \
            (vif)->state.is_network ? "N" : " ",                                                            \
            strcmp_null((vif)->state.ip_assign_scheme, SCHEMA_CONSTS_INET_IP_SCHEME_DHCP) == 0 ? "D" : " ", \
            (vif)->state.ip == 0 ? "0" : " ",                                                               \
            ##__VA_ARGS__)

#define CM2_BH_DHCP_NO_IP "0.0.0.0"

struct cm2_bh_dhcp_vif_state
{
    bool is_active;          // Wifi_Master_State
    bool is_4addr;           // Wifi_VIF_State
    bool is_sta;             // Wifi_VIF_State
    bool is_configurable;    // Wifi_Inet_Config
    bool is_enabled;         // Wifi_Inet_State
    bool is_network;         // Wifi_Inet_State
    char *ip_assign_scheme;  // Wifi_Inet_State
    in_addr_t ip;            // Wifi_Inet_State
};

struct cm2_bh_dhcp_vif
{
    cm2_bh_dhcp_t *m;
    ds_tree_node_t node;
    char *vif_name;
    ev_idle recalc;
    ev_timer deadline;
    ev_timer backoff;
    bool work;
    struct cm2_bh_dhcp_vif_state state;
};

struct cm2_bh_dhcp
{
    struct ev_loop *loop;
    ds_tree_t vifs;
};

static void cm2_bh_dhcp_vif_renew(cm2_bh_dhcp_vif_t *vif)
{
    if (vif == NULL) return;
    LOGI(LOG_PREFIX_VIF(vif, "renewing"));

    json_t *trans = NULL;

    /* FIXME: This is a hack to convince CM's
     * internal state machine to handle rapid WLAN
     * re-connect event that is expected to
     * re-create GRE tunnel. Without this it is
     * possible to get CM into a state where it
     * enters LINK_SEL and never leaves, waiting
     * for an event that never comes, even though
     * it has all the data to operate.
     *
     * Easy way to test:
     *
     * $ wpa_cli disc
     */
    trans = ovsdb_tran_multi(
            trans,
            NULL,
            SCHEMA_TABLE(Wifi_Master_State),
            OTR_UPDATE,
            json_pack("[[s, s, s]]", SCHEMA_COLUMN(Wifi_Master_State, if_name), "==", vif->vif_name),
            json_pack("{s:s}", "inet_addr", "0.0.0.0"));
    trans = ovsdb_tran_multi(
            trans,
            NULL,
            SCHEMA_TABLE(Wifi_Inet_State),
            OTR_UPDATE,
            json_pack("[[s, s, s]]", SCHEMA_COLUMN(Wifi_Inet_State, if_name), "==", vif->vif_name),
            json_pack("{s:s}", "inet_addr", "0.0.0.0"));

    trans = ovsdb_tran_multi(
            trans,
            NULL,
            SCHEMA_TABLE(Wifi_Inet_Config),
            OTR_MUTATE,
            json_pack("[[s, s, s]]", SCHEMA_COLUMN(Wifi_Inet_Config, if_name), "==", vif->vif_name),
            json_pack("[[s, s, i]]", SCHEMA_COLUMN(Wifi_Inet_Config, dhcp_renew), "+=", 1));

    json_t *resp = ovsdb_method_send_s(MT_TRANS, trans);
    json_decref(resp);
}

static void cm2_bh_dhcp_vif_recalc(cm2_bh_dhcp_vif_t *vif)
{
    if (vif == NULL) return;
    if (vif->work == false) return;
    if (ev_is_active(&vif->backoff)) return;

    vif->work = false;
    ev_timer_set(&vif->backoff, CM2_BH_DHCP_BACKOFF_SEC, 0);
    ev_timer_start(vif->m->loop, &vif->backoff);

    const bool can_renew = vif->state.is_configurable && vif->state.is_active && vif->state.is_enabled
                           && vif->state.is_network && vif->state.is_sta && vif->state.is_4addr == false
                           && (strcmp_null(vif->state.ip_assign_scheme, SCHEMA_CONSTS_INET_IP_SCHEME_DHCP) == 0);

    /* This is unexpected - if the VIF is 4addr
     * (Multi-AP / WDS) then it shouldn't be
     * running a DHCP client. It's intended to only
     * ever to go into a bridge where LAN DHCP
     * client is running.
     */
    WARN_ON(vif->state.is_4addr && (strcmp_null(vif->state.ip_assign_scheme, SCHEMA_CONSTS_INET_IP_SCHEME_DHCP) == 0));

    if (can_renew)
    {
        cm2_bh_dhcp_vif_renew(vif);
    }
}

static void cm2_bh_dhcp_vif_deadline_arm(cm2_bh_dhcp_vif_t *vif)
{
    if (ev_is_active(&vif->deadline)) return;
    ev_timer_set(&vif->deadline, CM2_BH_DHCP_DEADLINE_SEC, 0);
    ev_timer_start(vif->m->loop, &vif->deadline);
}

static void cm2_bh_dhcp_vif_recalc_arm(cm2_bh_dhcp_vif_t *vif)
{
    cm2_bh_dhcp_vif_deadline_arm(vif);
    ev_idle_start(vif->m->loop, &vif->recalc);
}

static void cm2_bh_dhcp_vif_schedule(cm2_bh_dhcp_vif_t *vif)
{
    if (vif == NULL) return;
    cm2_bh_dhcp_vif_recalc_arm(vif);
}

static void cm2_bh_dhcp_vif_recalc_cb(struct ev_loop *l, ev_idle *i, int mask)
{
    ev_idle_stop(l, i);
    cm2_bh_dhcp_vif_t *vif = i->data;
    ev_timer_stop(vif->m->loop, &vif->deadline);
    cm2_bh_dhcp_vif_recalc(vif);
}

static void cm2_bh_dhcp_vif_deadline_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_bh_dhcp_vif_t *vif = t->data;
    LOGI(LOG_PREFIX_VIF(vif, "recalc deadline elapsed"));
    cm2_bh_dhcp_vif_schedule(vif);
}

static void cm2_bh_dhcp_vif_backoff_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_bh_dhcp_vif_t *vif = t->data;
    cm2_bh_dhcp_vif_schedule(vif);
}

static void cm2_bh_dhcp_vif_report_is_active(cm2_bh_dhcp_vif_t *vif, const bool v)
{
    if (vif == NULL) return;
    if (vif->state.is_active == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: is_active: %s -> %s", BOOL_CSTR(vif->state.is_active), BOOL_CSTR(v)));
    vif->state.is_active = v;
    vif->work = true;
    cm2_bh_dhcp_vif_schedule(vif);
}

static void cm2_bh_dhcp_vif_report_is_4addr(cm2_bh_dhcp_vif_t *vif, const bool v)
{
    if (vif == NULL) return;
    if (vif->state.is_4addr == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: is_4addr: %s -> %s", BOOL_CSTR(vif->state.is_4addr), BOOL_CSTR(v)));
    vif->state.is_4addr = v;
    cm2_bh_dhcp_vif_schedule(vif);
}

static void cm2_bh_dhcp_vif_report_is_sta(cm2_bh_dhcp_vif_t *vif, const bool v)
{
    if (vif == NULL) return;
    if (vif->state.is_sta == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: is_sta: %s -> %s", BOOL_CSTR(vif->state.is_sta), BOOL_CSTR(v)));
    vif->state.is_sta = v;
    cm2_bh_dhcp_vif_schedule(vif);
}

void cm2_bh_dhcp_vif_report_is_configurable(cm2_bh_dhcp_vif_t *vif, const bool v)
{
    if (vif == NULL) return;
    if (vif->state.is_configurable == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: is_configurable: %s -> %s", BOOL_CSTR(vif->state.is_configurable), BOOL_CSTR(v)));
    vif->state.is_configurable = v;
    cm2_bh_dhcp_vif_schedule(vif);
}

void cm2_bh_dhcp_vif_report_is_enabled(cm2_bh_dhcp_vif_t *vif, const bool v)
{
    if (vif == NULL) return;
    if (vif->state.is_enabled == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: is_enabled: %s -> %s", BOOL_CSTR(vif->state.is_enabled), BOOL_CSTR(v)));
    vif->state.is_enabled = v;
    cm2_bh_dhcp_vif_schedule(vif);
}

void cm2_bh_dhcp_vif_report_is_network(cm2_bh_dhcp_vif_t *vif, const bool v)
{
    if (vif == NULL) return;
    if (vif->state.is_network == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: is_network: %s -> %s", BOOL_CSTR(vif->state.is_network), BOOL_CSTR(v)));
    vif->state.is_network = v;
    cm2_bh_dhcp_vif_schedule(vif);
}

static void cm2_bh_dhcp_vif_report_ip_assign_scheme(cm2_bh_dhcp_vif_t *vif, const char *v)
{
    if (vif == NULL) return;
    if (strcmp_null(vif->state.ip_assign_scheme, v) == 0) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: ip_assign_scheme: '%s' -> '%s'", vif->state.ip_assign_scheme ?: "", v ?: ""));
    FREE(vif->state.ip_assign_scheme);
    vif->state.ip_assign_scheme = v ? STRDUP(v) : NULL;
    cm2_bh_dhcp_vif_schedule(vif);
}

static void cm2_bh_dhcp_vif_report_ip(cm2_bh_dhcp_vif_t *vif, in_addr_t v)
{
    if (vif == NULL) return;
    if (vif->state.ip == v) return;
    struct in_addr addr;
    MEMZERO(addr);
    addr.s_addr = vif->state.ip;
    char *from = STRDUP(inet_ntoa(addr));
    addr.s_addr = v;
    char *to = STRDUP(inet_ntoa(addr));
    LOGI(LOG_PREFIX_VIF(vif, "report: ip: '%s' -> '%s'", from, to));
    FREE(from);
    FREE(to);
    vif->state.ip = v;
    cm2_bh_dhcp_vif_schedule(vif);
}

void cm2_bh_dhcp_WMS(
        cm2_bh_dhcp_t *dhcp,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Master_State *old_row,
        const struct schema_Wifi_Master_State *new_row)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;

    const char *if_name = CM2_OVS_COL(mon, old_row->if_name, new_row->if_name);
    cm2_bh_dhcp_vif_t *vif = cm2_bh_dhcp_lookup(dhcp, if_name);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Master_State, port_state)))
    {
        const bool is_active =
                (new_row != NULL) && (new_row->port_state_exists) && (strcmp(new_row->port_state, "active") == 0);
        cm2_bh_dhcp_vif_report_is_active(vif, is_active);
    }
}

void cm2_bh_dhcp_WVS(
        cm2_bh_dhcp_t *dhcp,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_VIF_State *old_row,
        const struct schema_Wifi_VIF_State *new_row)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;

    const char *if_name = CM2_OVS_COL(mon, old_row->if_name, new_row->if_name);
    cm2_bh_dhcp_vif_t *vif = cm2_bh_dhcp_lookup(dhcp, if_name);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, wds)))
    {
        const bool is_4addr = (new_row != NULL) && new_row->wds_exists && new_row->wds;
        cm2_bh_dhcp_vif_report_is_4addr(vif, is_4addr);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, mode)))
    {
        const bool is_sta = (new_row != NULL) && new_row->mode_exists && (strcmp(new_row->mode, "sta") == 0);
        cm2_bh_dhcp_vif_report_is_sta(vif, is_sta);
    }
}

void cm2_bh_dhcp_WIC(
        cm2_bh_dhcp_t *dhcp,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Inet_Config *old_row,
        const struct schema_Wifi_Inet_Config *new_row)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;

    const char *if_name = CM2_OVS_COL(mon, old_row->if_name, new_row->if_name);
    cm2_bh_dhcp_vif_t *vif = cm2_bh_dhcp_lookup(dhcp, if_name);

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
            cm2_bh_dhcp_vif_report_is_configurable(vif, true);
            break;
        case OVSDB_UPDATE_MODIFY:
            break;
        case OVSDB_UPDATE_DEL:
            cm2_bh_dhcp_vif_report_is_configurable(vif, false);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

void cm2_bh_dhcp_WIS(
        cm2_bh_dhcp_t *dhcp,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Inet_State *old_row,
        const struct schema_Wifi_Inet_State *new_row)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;

    const char *if_name = CM2_OVS_COL(mon, old_row->if_name, new_row->if_name);
    cm2_bh_dhcp_vif_t *vif = cm2_bh_dhcp_lookup(dhcp, if_name);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_Config, enabled)))
    {
        const bool enabled = new_row && new_row->enabled_exists && new_row->enabled;
        cm2_bh_dhcp_vif_report_is_enabled(vif, enabled);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_Config, network)))
    {
        const bool network = new_row && new_row->network_exists && new_row->network;
        cm2_bh_dhcp_vif_report_is_network(vif, network);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_Config, ip_assign_scheme)))
    {
        const char *ip_assign_scheme = new_row && new_row->ip_assign_scheme_exists ? new_row->ip_assign_scheme : NULL;
        cm2_bh_dhcp_vif_report_ip_assign_scheme(vif, ip_assign_scheme);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_State, inet_addr)))
    {
        const char *str = (new_row != NULL) && new_row->inet_addr_exists ? new_row->inet_addr : CM2_BH_DHCP_NO_IP;
        const in_addr_t ip = inet_addr(str);
        cm2_bh_dhcp_vif_report_ip(vif, ip);
    }
}

cm2_bh_dhcp_t *cm2_bh_dhcp_alloc(void)
{
    cm2_bh_dhcp_t *m = CALLOC(1, sizeof(*m));
    ds_tree_init(&m->vifs, ds_str_cmp, cm2_bh_dhcp_vif_t, node);
    m->loop = EV_DEFAULT;
    return m;
}

cm2_bh_dhcp_vif_t *cm2_bh_dhcp_vif_alloc(cm2_bh_dhcp_t *m, const char *vif_name)
{
    if (m == NULL) return NULL;
    if (cm2_bh_dhcp_lookup(m, vif_name) != NULL) return NULL;

    cm2_bh_dhcp_vif_t *vif = CALLOC(1, sizeof(*vif));
    ev_idle_init(&vif->recalc, cm2_bh_dhcp_vif_recalc_cb);
    ev_timer_init(&vif->backoff, cm2_bh_dhcp_vif_backoff_cb, 1, 1);
    ev_timer_init(&vif->deadline, cm2_bh_dhcp_vif_deadline_cb, 1, 1);
    vif->m = m;
    vif->vif_name = STRDUP(vif_name);
    vif->state.ip = inet_addr(CM2_BH_DHCP_NO_IP);
    vif->recalc.data = vif;
    vif->backoff.data = vif;
    vif->deadline.data = vif;
    ds_tree_insert(&m->vifs, vif, vif->vif_name);
    LOGI(LOG_PREFIX_VIF(vif, "allocated"));
    return vif;
}

void cm2_bh_dhcp_vif_drop(cm2_bh_dhcp_vif_t *vif)
{
    if (vif == NULL) return;
    LOGI(LOG_PREFIX_VIF(vif, "dropping"));
    ev_idle_stop(vif->m->loop, &vif->recalc);
    ev_timer_stop(vif->m->loop, &vif->backoff);
    ev_timer_stop(vif->m->loop, &vif->deadline);
    ds_tree_remove(&vif->m->vifs, vif);
    FREE(vif->state.ip_assign_scheme);
    FREE(vif->vif_name);
    FREE(vif);
}

static void cm2_bh_dhcp_drop_vifs(cm2_bh_dhcp_t *m)
{
    cm2_bh_dhcp_vif_t *vif;
    while ((vif = ds_tree_head(&m->vifs)) != NULL)
    {
        cm2_bh_dhcp_vif_drop(vif);
    }
}

void cm2_bh_dhcp_drop(cm2_bh_dhcp_t *m)
{
    if (m == NULL) return;
    cm2_bh_dhcp_drop_vifs(m);
    FREE(m);
}

cm2_bh_dhcp_t *cm2_bh_dhcp_from_list(const char *list)
{
    cm2_bh_dhcp_t *m = cm2_bh_dhcp_alloc();
    char *entries = strdupa(list ?: "");
    char *entry;
    while ((entry = strsep(&entries, " ")) != NULL)
    {
        const char *phy_name = strsep(&entry, ":");
        const char *vif_name = strsep(&entry, ":");
        if (phy_name == NULL) continue;
        if (vif_name == NULL) continue;
        cm2_bh_dhcp_vif_alloc(m, vif_name);
    }
    return m;
}

cm2_bh_dhcp_vif_t *cm2_bh_dhcp_lookup(cm2_bh_dhcp_t *m, const char *vif_name)
{
    return ds_tree_find(&m->vifs, vif_name);
}
