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

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ev.h>

#include <log.h>
#include <const.h>
#include <memutil.h>
#include <ds_tree.h>
#include <ovsdb_sync.h>
#include <ovsdb_table.h>
#include <schema_consts.h>

#include "cm2_bh_gre.h"
#include "cm2_bh_macros.h"

#define CM2_BH_GRE_BACKOFF_SEC  3.0
#define CM2_BH_GRE_DEADLINE_SEC 3.0

#define LOG_PREFIX(m, fmt, ...) "cm2: bh: gre: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_TUN(tun, fmt, ...) \
    LOG_PREFIX((tun)->vif->m, "%s (%s): " fmt, (tun)->tun_name, (tun)->vif ? (tun)->vif->vif_name : "", ##__VA_ARGS__)

#define LOG_PREFIX_VIF(vif, fmt, ...) \
    LOG_PREFIX((vif)->m, "%s (%s): " fmt, (vif)->vif_name, (vif)->tun ? (vif)->tun->tun_name : "", ##__VA_ARGS__)

extern ovsdb_table_t table_Wifi_Inet_State;
extern ovsdb_table_t table_Wifi_Inet_Config;

struct cm2_bh_gre_tun
{
    char *tun_name; /* eg. g-bhaul-sta-24 */
    ev_idle recalc;
    ev_timer deadline;
    ev_timer backoff;
    ds_tree_node_t node;
    cm2_bh_gre_vif_t *vif;
    bool work;

    /* FIXME: This should really maintain all the
     * OVSDB params that are controlled: network,
     * enabled, mtu, etc. It's a lot of typing
     * though, and these are the bare minimum for
     * things to work. Without extra work it is
     * possible to get out-of-sync if something
     * starts poking at OVSDB.
     */
    struct
    {
        char *vif_name; /* eg. bhaul-sta-24 */
        struct in_addr local_ip;
        struct in_addr remote_ip;
    } report;
    struct
    {
        bool enable;
        struct in_addr local_ip;
        struct in_addr remote_ip;
    } config;
};

struct cm2_bh_gre_vif
{
    char *vif_name;
    ev_idle recalc;
    ev_timer deadline;
    ev_timer backoff;
    ds_tree_node_t node;
    cm2_bh_gre_t *m;
    cm2_bh_gre_tun_t *tun;
    bool work;

    struct
    {
        bool enabled;
        bool network;
        struct in_addr inet_addr;
        struct in_addr netmask;
    } report;
};

struct cm2_bh_gre
{
    struct ev_loop *loop;
    ds_tree_t vifs;
    ds_tree_t tuns;
};

static in_addr_t cm2_bh_gre_ip_zero(void)
{
    in_addr_t zero;
    MEMZERO(zero);
    return zero;
}

static bool cm2_bh_gre_ip_is_ll(in_addr_t addr)
{
    return (((ntohl(addr) >> 24) & 0xff) == 169) && (((ntohl(addr) >> 16) & 0xff) == 254);
}

static bool cm2_bh_gre_vif_derive_enable(cm2_bh_gre_vif_t *vif)
{
    if (vif->report.enabled == false) return false;
    if (vif->report.network == false) return false;
    if (vif->report.inet_addr.s_addr == cm2_bh_gre_ip_zero()) return false;
    if (vif->report.netmask.s_addr == cm2_bh_gre_ip_zero()) return false;
    if (WARN_ON(cm2_bh_gre_ip_is_ll(vif->report.inet_addr.s_addr) == false)) return false;
    return true;
}

static in_addr_t cm2_bh_gre_vif_derive_local_ip(cm2_bh_gre_vif_t *vif)
{
    return vif->report.inet_addr.s_addr;
}

static in_addr_t cm2_bh_gre_vif_derive_remote_ip(cm2_bh_gre_vif_t *vif)
{
    return (vif->report.inet_addr.s_addr & vif->report.netmask.s_addr) | htonl(1);
}

static void cm2_bh_gre_tun_insert(cm2_bh_gre_tun_t *tun)
{
    if (tun == NULL) return;
    LOGI(LOG_PREFIX_TUN(tun, "inserting"));
    struct schema_Wifi_Inet_Config row;
    MEMZERO(row);
    const char *local_ip = strdupa(inet_ntoa(tun->config.local_ip));
    const char *remote_ip = strdupa(inet_ntoa(tun->config.remote_ip));
    row._partial_update = true;
    SCHEMA_SET_BOOL(row.enabled, true);
    SCHEMA_SET_BOOL(row.network, true);
    SCHEMA_SET_INT(row.mtu, CONFIG_CM2_MTU_ON_GRE);
    SCHEMA_SET_STR(row.ip_assign_scheme, SCHEMA_CONSTS_INET_IP_SCHEME_NONE);
    SCHEMA_SET_STR(row.if_name, tun->tun_name); /* eg. g-bhaul-sta-24 */
    SCHEMA_SET_STR(row.if_type, SCHEMA_CONSTS_IF_TYPE_GRE);
    SCHEMA_SET_STR(row.gre_ifname, tun->vif->vif_name); /* eg. bhaul-sta-24 */
    SCHEMA_SET_STR(row.gre_local_inet_addr, local_ip);
    SCHEMA_SET_STR(row.gre_remote_inet_addr, remote_ip);
    const bool ok = ovsdb_table_upsert(&table_Wifi_Inet_Config, &row, false);
    WARN_ON(ok == false);
}

static void cm2_bh_gre_tun_delete(cm2_bh_gre_tun_t *tun)
{
    if (tun == NULL) return;
    LOGI(LOG_PREFIX_TUN(tun, "deleting"));
    ovsdb_table_delete_simple(&table_Wifi_Inet_Config, SCHEMA_COLUMN(Wifi_Inet_Config, if_name), tun->tun_name);
}

static void cm2_bh_gre_tun_recalc(cm2_bh_gre_tun_t *tun)
{
    if (tun == NULL) return;
    if (tun->work == false) return;
    if (ev_is_active(&tun->backoff)) return;

    tun->work = false;
    ev_timer_set(&tun->backoff, CM2_BH_GRE_BACKOFF_SEC, 0);
    ev_timer_start(tun->vif->m->loop, &tun->backoff);

    const char *vif_name = tun->config.enable ? tun->vif->vif_name : NULL;
    const bool enabled = (tun->report.vif_name != NULL);
    const bool changed = false || (tun->config.enable != enabled)
                         || (tun->config.local_ip.s_addr != tun->report.local_ip.s_addr)
                         || (tun->config.remote_ip.s_addr != tun->report.remote_ip.s_addr)
                         || (strcmp_null(vif_name, tun->report.vif_name) != 0);
    const bool complete = true && (tun->config.local_ip.s_addr != cm2_bh_gre_ip_zero())
                          && (tun->config.remote_ip.s_addr != cm2_bh_gre_ip_zero()) && (tun->config.enable == true);

    if (changed)
    {
        if (enabled)
        {
            cm2_bh_gre_tun_delete(tun);
        }

        if (complete)
        {
            cm2_bh_gre_tun_insert(tun);
        }
    }
}

static void cm2_bh_gre_tun_deadline_arm(cm2_bh_gre_tun_t *tun)
{
    if (ev_is_active(&tun->deadline)) return;
    ev_timer_set(&tun->deadline, CM2_BH_GRE_DEADLINE_SEC, 0);
    ev_timer_start(tun->vif->m->loop, &tun->deadline);
}

static void cm2_bh_gre_tun_recalc_arm(cm2_bh_gre_tun_t *tun)
{
    ev_idle_start(tun->vif->m->loop, &tun->recalc);
    cm2_bh_gre_tun_deadline_arm(tun);
}

static void cm2_bh_gre_tun_schedule(cm2_bh_gre_tun_t *tun)
{
    tun->work = true;
    cm2_bh_gre_tun_recalc_arm(tun);
}

static void cm2_bh_gre_tun_recalc_cb(struct ev_loop *l, ev_idle *i, int mask)
{
    ev_idle_stop(l, i);
    cm2_bh_gre_tun_t *tun = i->data;
    ev_timer_stop(tun->vif->m->loop, &tun->deadline);
    cm2_bh_gre_tun_recalc(tun);
}

static void cm2_bh_gre_tun_deadline_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_bh_gre_tun_t *tun = t->data;
    LOGI(LOG_PREFIX_TUN(tun, "recalc deadline elapsed"));
    cm2_bh_gre_tun_recalc(tun);
}

static void cm2_bh_gre_tun_backoff_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_bh_gre_tun_t *tun = t->data;
    cm2_bh_gre_tun_recalc_arm(tun);
}

static void cm2_bh_gre_tun_set_enable(cm2_bh_gre_tun_t *tun, bool v)
{
    if (tun == NULL) return;
    if (tun->config.enable == v) return;
    LOGI(LOG_PREFIX_TUN(tun, "set: enable: %s -> %s", BOOL_CSTR(tun->config.enable), BOOL_CSTR(v)));
    tun->config.enable = v;
    cm2_bh_gre_tun_schedule(tun);
}

static void cm2_bh_gre_tun_set_local_ip(cm2_bh_gre_tun_t *tun, struct in_addr v)
{
    if (tun == NULL) return;
    if (tun->config.local_ip.s_addr == v.s_addr) return;
    const char *from = strdupa(inet_ntoa(tun->config.local_ip));
    const char *to = strdupa(inet_ntoa(v));
    LOGI(LOG_PREFIX_TUN(tun, "set: local_ip: %s -> %s", from, to));
    tun->config.local_ip.s_addr = v.s_addr;
    cm2_bh_gre_tun_schedule(tun);
}

static void cm2_bh_gre_tun_set_remote_ip(cm2_bh_gre_tun_t *tun, struct in_addr v)
{
    if (tun == NULL) return;
    if (tun->config.remote_ip.s_addr == v.s_addr) return;
    const char *from = strdupa(inet_ntoa(tun->config.remote_ip));
    const char *to = strdupa(inet_ntoa(v));
    LOGI(LOG_PREFIX_TUN(tun, "set: remote_ip: %s -> %s", from, to));
    tun->config.remote_ip.s_addr = v.s_addr;
    cm2_bh_gre_tun_schedule(tun);
}

static void cm2_bh_gre_vif_recalc(cm2_bh_gre_vif_t *vif)
{
    if (vif == NULL) return;
    if (vif->work == false) return;
    if (ev_is_active(&vif->backoff)) return;

    vif->work = false;
    ev_timer_set(&vif->backoff, 1, 0);
    ev_timer_start(vif->m->loop, &vif->backoff);

    const bool enable = cm2_bh_gre_vif_derive_enable(vif);
    const struct in_addr local_ip = {.s_addr = cm2_bh_gre_vif_derive_local_ip(vif)};
    const struct in_addr remote_ip = {.s_addr = cm2_bh_gre_vif_derive_remote_ip(vif)};

    cm2_bh_gre_tun_set_enable(vif->tun, enable);
    cm2_bh_gre_tun_set_local_ip(vif->tun, local_ip);
    cm2_bh_gre_tun_set_remote_ip(vif->tun, remote_ip);
}

static void cm2_bh_gre_vif_deadline_arm(cm2_bh_gre_vif_t *vif)
{
    if (ev_is_active(&vif->deadline)) return;
    ev_timer_set(&vif->deadline, CM2_BH_GRE_DEADLINE_SEC, 0);
    ev_timer_start(vif->m->loop, &vif->deadline);
}

static void cm2_bh_gre_vif_recalc_arm(cm2_bh_gre_vif_t *vif)
{
    ev_idle_start(vif->m->loop, &vif->recalc);
    cm2_bh_gre_vif_deadline_arm(vif);
}

static void cm2_bh_gre_vif_schedule(cm2_bh_gre_vif_t *vif)
{
    vif->work = true;
    cm2_bh_gre_vif_recalc_arm(vif);
}

static void cm2_bh_gre_vif_recalc_cb(struct ev_loop *l, ev_idle *i, int mask)
{
    ev_idle_stop(l, i);
    cm2_bh_gre_vif_t *vif = i->data;
    ev_timer_stop(vif->m->loop, &vif->deadline);
    cm2_bh_gre_vif_recalc(vif);
}

static void cm2_bh_gre_vif_deadline_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_bh_gre_vif_t *vif = t->data;
    LOGI(LOG_PREFIX_VIF(vif, "recalc deadline elapsed"));
    cm2_bh_gre_vif_recalc(vif);
}

static void cm2_bh_gre_vif_backoff_cb(struct ev_loop *l, ev_timer *t, int mask)
{
    cm2_bh_gre_vif_t *vif = t->data;
    cm2_bh_gre_vif_recalc_arm(vif);
}

static void cm2_bh_gre_vif_report_enabled(cm2_bh_gre_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.enabled == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: enabled: %s -> %s", BOOL_CSTR(vif->report.enabled), BOOL_CSTR(v)));
    vif->report.enabled = v;
    cm2_bh_gre_vif_schedule(vif);
}

static void cm2_bh_gre_vif_report_network(cm2_bh_gre_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.network == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "report: network: %s -> %s", BOOL_CSTR(vif->report.network), BOOL_CSTR(v)));
    vif->report.network = v;
    cm2_bh_gre_vif_schedule(vif);
}

static void cm2_bh_gre_vif_report_inet_addr(cm2_bh_gre_vif_t *vif, struct in_addr v)
{
    if (vif == NULL) return;
    if (vif->report.inet_addr.s_addr == v.s_addr) return;
    const char *from = strdupa(inet_ntoa(vif->report.inet_addr));
    const char *to = strdupa(inet_ntoa(v));
    LOGI(LOG_PREFIX_VIF(vif, "report: inet_addr: %s -> %s", from, to));
    vif->report.inet_addr.s_addr = v.s_addr;
    cm2_bh_gre_vif_schedule(vif);
}

static void cm2_bh_gre_vif_report_netmask(cm2_bh_gre_vif_t *vif, struct in_addr v)
{
    if (vif == NULL) return;
    if (vif->report.netmask.s_addr == v.s_addr) return;
    const char *from = strdupa(inet_ntoa(vif->report.netmask));
    const char *to = strdupa(inet_ntoa(v));
    LOGI(LOG_PREFIX_VIF(vif, "report: netmask: %s -> %s", from, to));
    vif->report.netmask.s_addr = v.s_addr;
    cm2_bh_gre_vif_schedule(vif);
}

static void cm2_bh_gre_tun_report_vif_name(cm2_bh_gre_tun_t *tun, const char *v)
{
    if (tun == NULL) return;
    if (strcmp_null(tun->report.vif_name, v) == 0) return;
    LOGI(LOG_PREFIX_TUN(tun, "report: vif_name: '%s' -> '%s'", tun->report.vif_name ?: "", v ?: ""));
    FREE(tun->report.vif_name);
    tun->report.vif_name = v ? STRDUP(v) : NULL;
    cm2_bh_gre_tun_schedule(tun);
}

static void cm2_bh_gre_tun_report_local_ip(cm2_bh_gre_tun_t *tun, struct in_addr v)
{
    if (tun == NULL) return;
    if (tun->report.local_ip.s_addr == v.s_addr) return;
    const char *from = strdupa(inet_ntoa(tun->report.local_ip));
    const char *to = strdupa(inet_ntoa(v));
    LOGI(LOG_PREFIX_TUN(tun, "report: local_ip: %s -> %s", from, to));
    tun->report.local_ip.s_addr = v.s_addr;
    cm2_bh_gre_tun_schedule(tun);
}

static void cm2_bh_gre_tun_report_remote_ip(cm2_bh_gre_tun_t *tun, struct in_addr v)
{
    if (tun == NULL) return;
    if (tun->report.remote_ip.s_addr == v.s_addr) return;
    const char *from = strdupa(inet_ntoa(tun->report.remote_ip));
    const char *to = strdupa(inet_ntoa(v));
    LOGI(LOG_PREFIX_TUN(tun, "report: remote_ip: %s -> %s", from, to));
    tun->report.remote_ip.s_addr = v.s_addr;
    cm2_bh_gre_tun_schedule(tun);
}

cm2_bh_gre_vif_t *cm2_bh_gre_vif_alloc(cm2_bh_gre_t *m, const char *vif_name)
{
    if (m == NULL) return NULL;

    const bool already_exists = (cm2_bh_gre_lookup_vif(m, vif_name) != NULL);
    if (WARN_ON(already_exists)) return NULL;

    cm2_bh_gre_vif_t *vif = CALLOC(1, sizeof(*vif));
    ev_timer_init(&vif->backoff, cm2_bh_gre_vif_backoff_cb, 0, 0);
    ev_timer_init(&vif->deadline, cm2_bh_gre_vif_deadline_cb, 0, 0);
    ev_idle_init(&vif->recalc, cm2_bh_gre_vif_recalc_cb);
    vif->backoff.data = vif;
    vif->deadline.data = vif;
    vif->recalc.data = vif;
    vif->m = m;
    vif->vif_name = STRDUP(vif_name);
    ds_tree_insert(&m->vifs, vif, vif->vif_name);
    LOGI(LOG_PREFIX_VIF(vif, "allocated"));
    CM2_BH_OVS_INIT(vif->m, cm2_bh_gre_WIS, Wifi_Inet_State, if_name, vif->vif_name);
    return vif;
}

void cm2_bh_gre_vif_drop(cm2_bh_gre_vif_t *vif)
{
    if (vif == NULL) return;

    LOGI(LOG_PREFIX_VIF(vif, "dropping"));

    const bool bound_to_tun = (vif->tun != NULL);
    if (WARN_ON(bound_to_tun)) return;

    ev_timer_stop(vif->m->loop, &vif->backoff);
    ev_timer_stop(vif->m->loop, &vif->deadline);
    ev_idle_stop(vif->m->loop, &vif->recalc);
    ds_tree_remove(&vif->m->vifs, vif);
    FREE(vif->vif_name);
    FREE(vif);
}

/* vif is rust-moved */
cm2_bh_gre_tun_t *cm2_bh_gre_tun_alloc(cm2_bh_gre_vif_t *vif, const char *tun_name)
{
    if (vif == NULL) return NULL;
    if (vif->m == NULL) return NULL;

    const bool already_bound_to_tun = (vif->tun != NULL);
    if (WARN_ON(already_bound_to_tun)) return NULL;

    cm2_bh_gre_t *m = vif->m;
    const bool already_exists = (cm2_bh_gre_lookup_tun(m, tun_name) != NULL);
    if (WARN_ON(already_exists)) return NULL;

    cm2_bh_gre_tun_t *tun = CALLOC(1, sizeof(*tun));
    ev_timer_init(&tun->backoff, cm2_bh_gre_tun_backoff_cb, 0, 0);
    ev_timer_init(&tun->deadline, cm2_bh_gre_tun_deadline_cb, 0, 0);
    ev_idle_init(&tun->recalc, cm2_bh_gre_tun_recalc_cb);
    tun->backoff.data = tun;
    tun->deadline.data = tun;
    tun->recalc.data = tun;
    tun->vif = vif;
    tun->tun_name = STRDUP(tun_name);
    vif->tun = tun;
    ds_tree_insert(&m->tuns, tun, tun->tun_name);
    LOGI(LOG_PREFIX_TUN(tun, "allocated"));
    CM2_BH_OVS_INIT(tun->vif->m, cm2_bh_gre_WIS, Wifi_Inet_State, if_name, tun->tun_name);
    return tun;
}

void cm2_bh_gre_tun_drop(cm2_bh_gre_tun_t *tun)
{
    if (tun == NULL) return;
    LOGI(LOG_PREFIX_TUN(tun, "dropping"));
    cm2_bh_gre_tun_delete(tun);
    ev_timer_stop(tun->vif->m->loop, &tun->backoff);
    ev_timer_stop(tun->vif->m->loop, &tun->deadline);
    ev_idle_stop(tun->vif->m->loop, &tun->recalc);
    ds_tree_remove(&tun->vif->m->tuns, tun);
    tun->vif->tun = NULL;
    cm2_bh_gre_vif_drop(tun->vif);
    FREE(tun->tun_name);
    FREE(tun);
}

cm2_bh_gre_vif_t *cm2_bh_gre_lookup_vif(cm2_bh_gre_t *m, const char *vif_name)
{
    if (m == NULL) return NULL;
    return ds_tree_find(&m->vifs, vif_name);
}

cm2_bh_gre_tun_t *cm2_bh_gre_lookup_tun(cm2_bh_gre_t *m, const char *tun_name)
{
    if (m == NULL) return NULL;
    return ds_tree_find(&m->tuns, tun_name);
}

cm2_bh_gre_t *cm2_bh_gre_alloc(void)
{
    cm2_bh_gre_t *m = CALLOC(1, sizeof(*m));
    m->loop = EV_DEFAULT;
    ds_tree_init(&m->vifs, ds_str_cmp, cm2_bh_gre_vif_t, node);
    ds_tree_init(&m->tuns, ds_str_cmp, cm2_bh_gre_tun_t, node);
    return m;
}

cm2_bh_gre_t *cm2_bh_gre_from_list(const char *list)
{
    cm2_bh_gre_t *m = cm2_bh_gre_alloc();
    char *entries = strdupa(list ?: "");
    char *entry;
    while ((entry = strsep(&entries, " ")) != NULL)
    {
        const char *phy_name = strsep(&entry, ":");
        const char *vif_name = strsep(&entry, ":");
        if (phy_name == NULL) continue;
        if (vif_name == NULL) continue;
        const char *tun_name = strfmta("g-%s", vif_name);
        cm2_bh_gre_vif_t *vif = cm2_bh_gre_vif_alloc(m, vif_name);
        WARN_ON(vif == NULL);
        cm2_bh_gre_tun_t *tun = cm2_bh_gre_tun_alloc(vif, tun_name);
        WARN_ON(tun == NULL);
    }
    return m;
}

void cm2_bh_gre_drop(cm2_bh_gre_t *m)
{
    cm2_bh_gre_tun_t *tun;
    while ((tun = ds_tree_head(&m->tuns)) != NULL)
        cm2_bh_gre_tun_drop(tun);

    cm2_bh_gre_vif_t *vif;
    while ((vif = ds_tree_head(&m->vifs)) != NULL)
        cm2_bh_gre_vif_drop(vif);

    FREE(m);
}

void cm2_bh_gre_WIS(
        cm2_bh_gre_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_Inet_State *old_row,
        const struct schema_Wifi_Inet_State *new_row)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;

    const char *if_name = CM2_OVS_COL(mon, old_row->if_name, new_row->if_name);
    const char *zero_str = "0.0.0.0";

    cm2_bh_gre_vif_t *vif = cm2_bh_gre_lookup_vif(m, if_name);
    cm2_bh_gre_tun_t *tun = cm2_bh_gre_lookup_tun(m, if_name);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_State, enabled)))
    {
        const bool enabled = new_row && new_row->enabled_exists && new_row->enabled;
        cm2_bh_gre_vif_report_enabled(vif, enabled);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_State, network)))
    {
        const bool network = new_row && new_row->network_exists && new_row->network;
        cm2_bh_gre_vif_report_network(vif, network);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_State, inet_addr)))
    {
        const char *str = (new_row && new_row->inet_addr_exists) ? new_row->inet_addr : zero_str;
        const struct in_addr ip = {.s_addr = inet_addr(str)};
        cm2_bh_gre_vif_report_inet_addr(vif, ip);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_State, netmask)))
    {
        const char *str = (new_row && new_row->netmask_exists) ? new_row->netmask : zero_str;
        const struct in_addr ip = {.s_addr = inet_addr(str)};
        cm2_bh_gre_vif_report_netmask(vif, ip);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_State, gre_ifname)))
    {
        const char *gre_ifname = new_row && new_row->gre_ifname_exists ? new_row->gre_ifname : NULL;
        cm2_bh_gre_tun_report_vif_name(tun, gre_ifname);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_State, gre_local_inet_addr)))
    {
        const char *str = (new_row && new_row->gre_local_inet_addr_exists) ? new_row->gre_local_inet_addr : zero_str;
        const struct in_addr ip = {.s_addr = inet_addr(str)};
        cm2_bh_gre_tun_report_local_ip(tun, ip);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_Inet_State, gre_remote_inet_addr)))
    {
        const char *str = (new_row && new_row->gre_remote_inet_addr_exists) ? new_row->gre_remote_inet_addr : zero_str;
        const struct in_addr ip = {.s_addr = inet_addr(str)};
        cm2_bh_gre_tun_report_remote_ip(tun, ip);
    }
}
