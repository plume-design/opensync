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

#include <memutil.h>
#include <schema_consts.h>
#include <log.h>
#include <ovsdb_table.h>
#include <ovsdb_sync.h>

#include "cm2_bh_dhcp.h"
#include "cm2_bh_gre.h"
#include "cm2_bh_cmu.h"
#include "cm2_bh_mlo.h"
#include "cm2_bh_macros.h"
#include "cm2_work.h"

#define CM2_BH_MLO_DEADLINE_SEC 3.0
#define CM2_BH_MLO_COOLDOWN_SEC 3.0

#define LOG_PREFIX(m, fmt, ...) "cm2: bh: mlo: " fmt, ##__VA_ARGS__

#define LOG_PREFIX_MLD(mld, fmt, ...) LOG_PREFIX((mld)->m, "mld: %s: " fmt, (mld)->mld_name, ##__VA_ARGS__)

#define LOG_PREFIX_VIF(vif, fmt, ...)               \
    LOG_PREFIX(                                     \
            (vif)->m,                               \
            "vif: %s [%s%s%s%s%s]: " fmt,           \
            (vif)->vif_name,                        \
            (vif)->report.wvs_sta ? "S" : " ",      \
            (vif)->report.wvs_4addr ? "4" : " ",    \
            (vif)->mld ? "M" : " ",                 \
            (vif)->mld ? ":" : "",                  \
            (vif)->mld ? (vif)->mld->mld_name : "", \
            ##__VA_ARGS__)

typedef struct cm2_bh_mlo_vif cm2_bh_mlo_vif_t;
typedef struct cm2_bh_mlo_mld cm2_bh_mlo_mld_t;

extern ovsdb_table_t table_Wifi_VIF_State;

struct cm2_bh_mlo
{
    cm2_bh_dhcp_t *dhcp;
    cm2_bh_gre_t *gre;
    cm2_bh_cmu_t *cmu;
    ds_tree_t vifs;
    ds_tree_t mlds;
};

struct cm2_bh_mlo_mld
{
    ds_tree_node_t node;
    cm2_bh_mlo_t *m;
    cm2_bh_cmu_gre_t *cmu_gre;
    cm2_bh_cmu_vif_t *cmu_vif;
    cm2_bh_dhcp_vif_t *dhcp;
    cm2_bh_gre_tun_t *gre;
    cm2_work_t *work;
    ds_tree_t vifs;
    char *mld_name;
};

struct cm2_bh_mlo_vif
{
    ds_tree_node_t node;
    ds_tree_node_t node_mld;
    cm2_bh_mlo_t *m;
    cm2_bh_mlo_mld_t *mld;
    char *vif_name;
    struct
    {
        bool wvs_sta;
        bool wvs_4addr;
    } report;
};

static bool cm2_bh_mlo_mld_is_4addr(cm2_bh_mlo_mld_t *mld)
{
    if (mld == NULL) return false;
    cm2_bh_mlo_vif_t *vif;
    size_t n = 0;
    size_t n_sta = 0;
    size_t n_4addr = 0;
    ds_tree_foreach (&mld->vifs, vif)
        n++;
    ds_tree_foreach (&mld->vifs, vif)
        if (vif->report.wvs_sta) n_sta++;
    ds_tree_foreach (&mld->vifs, vif)
        if (vif->report.wvs_4addr) n_4addr++;
    return (n > 0) && (n_sta == n) && (n_4addr == n);
}

static bool cm2_bh_mlo_mld_is_3addr(cm2_bh_mlo_mld_t *mld)
{
    if (mld == NULL) return false;
    cm2_bh_mlo_vif_t *vif;
    size_t n = 0;
    size_t n_sta = 0;
    size_t n_4addr = 0;
    ds_tree_foreach (&mld->vifs, vif)
        n++;
    ds_tree_foreach (&mld->vifs, vif)
        if (vif->report.wvs_sta) n_sta++;
    ds_tree_foreach (&mld->vifs, vif)
        if (vif->report.wvs_4addr) n_4addr++;
    return (n > 0) && (n_sta == n) && (n_4addr == 0);
}

static bool cm2_bh_mlo_mld_is_sta(cm2_bh_mlo_mld_t *mld)
{
    if (mld == NULL) return false;
    cm2_bh_mlo_vif_t *vif;
    size_t n = 0;
    size_t n_sta = 0;
    ds_tree_foreach (&mld->vifs, vif)
        n++;
    ds_tree_foreach (&mld->vifs, vif)
        if (vif->report.wvs_sta) n_sta++;
    return (n > 0) && (n_sta == n);
}

static void cm2_bh_mlo_mld_drop(cm2_bh_mlo_mld_t *mld)
{
    if (mld == NULL) return;
    LOGI(LOG_PREFIX_MLD(mld, "dropping"));
    ds_tree_remove(&mld->m->mlds, mld);
    cm2_bh_cmu_vif_drop(mld->cmu_vif);
    cm2_bh_cmu_gre_drop(mld->cmu_gre);
    cm2_bh_dhcp_vif_drop(mld->dhcp);
    cm2_bh_gre_tun_drop(mld->gre);
    cm2_work_drop(mld->work);
    FREE(mld->mld_name);
    FREE(mld);
}

static void cm2_bh_mlo_mld_gc(cm2_bh_mlo_mld_t *mld)
{
    if (mld == NULL) return;
    if (ds_tree_is_empty(&mld->vifs) == false) return;
    cm2_bh_mlo_mld_drop(mld);
}

static cm2_bh_gre_tun_t *cm2_bh_mlo_mld_alloc_gre(const cm2_bh_mlo_mld_t *mld)
{
    if (mld == NULL) return NULL;
    if (mld->m == NULL) return NULL;
    const char *gre_name = strfmta(CM2_BH_GRE_PREFIX "%s", mld->mld_name);
    cm2_bh_gre_vif_t *vif = cm2_bh_gre_vif_alloc(mld->m->gre, mld->mld_name);
    return cm2_bh_gre_tun_alloc(vif, gre_name);
}

static void cm2_bh_mlo_mld_update_gre(cm2_bh_mlo_mld_t *mld, bool is_sta, bool is_3addr)
{
    const bool needs_gre = (is_sta && is_3addr);
    const bool runs_gre = (mld->gre != NULL);
    if (needs_gre == runs_gre) return;
    LOGI(LOG_PREFIX_MLD(mld, "gre: %s -> %s", BOOL_CSTR(runs_gre), BOOL_CSTR(needs_gre)));
    cm2_bh_gre_tun_drop(mld->gre);
    mld->gre = needs_gre ? cm2_bh_mlo_mld_alloc_gre(mld) : NULL;
}

static void cm2_bh_mlo_mld_work_cb(void *priv)
{
    cm2_bh_mlo_mld_t *mld = priv;

    const bool is_4addr = cm2_bh_mlo_mld_is_4addr(mld);
    const bool is_3addr = cm2_bh_mlo_mld_is_3addr(mld);
    const bool is_sta = cm2_bh_mlo_mld_is_sta(mld);
    const bool wvs_sta = (is_sta && (is_3addr || is_4addr));
    const bool wvs_4addr = (is_sta && is_4addr);

    cm2_bh_mlo_mld_update_gre(mld, is_sta, is_3addr);
    cm2_bh_cmu_vif_report_wvs_sta(mld->cmu_vif, wvs_sta);
    cm2_bh_cmu_vif_report_wvs_4addr(mld->cmu_vif, wvs_4addr);
    cm2_bh_mlo_mld_gc(mld);
}

static void cm2_bh_mlo_mld_recalc(cm2_bh_mlo_mld_t *mld)
{
    if (mld == NULL) return;
    cm2_work_schedule(mld->work);
}

static void cm2_bh_mlo_vif_report_wvs_sta(cm2_bh_mlo_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.wvs_sta == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "wvs_sta: %s -> %s", BOOL_CSTR(vif->report.wvs_sta), BOOL_CSTR(v)));
    vif->report.wvs_sta = v;
    cm2_bh_mlo_mld_recalc(vif->mld);
}

static void cm2_bh_mlo_vif_report_wvs_4addr(cm2_bh_mlo_vif_t *vif, bool v)
{
    if (vif == NULL) return;
    if (vif->report.wvs_4addr == v) return;
    LOGI(LOG_PREFIX_VIF(vif, "wvs_4addr: %s -> %s", BOOL_CSTR(vif->report.wvs_4addr), BOOL_CSTR(v)));
    vif->report.wvs_4addr = v;
    cm2_bh_mlo_mld_recalc(vif->mld);
}

static void cm2_bh_mlo_vif_report_mld(cm2_bh_mlo_vif_t *vif, cm2_bh_mlo_mld_t *mld)
{
    if (vif == NULL) return;
    if (vif->mld == mld) return;
    LOGI(LOG_PREFIX_VIF(vif, "mld: '%s' -> '%s'", vif->mld ? vif->mld->mld_name : "", mld ? mld->mld_name : ""));
    if (vif->mld != NULL)
    {
        ds_tree_remove(&vif->mld->vifs, vif);
        cm2_bh_mlo_mld_recalc(vif->mld);
        vif->mld = NULL;
    }
    if (mld != NULL)
    {
        ds_tree_insert(&mld->vifs, vif, vif->vif_name);
        vif->mld = mld;
        cm2_bh_mlo_mld_recalc(vif->mld);
    }
}

static void cm2_bh_mlo_vif_drop(cm2_bh_mlo_vif_t *vif)
{
    if (vif == NULL) return;
    LOGI(LOG_PREFIX_VIF(vif, "dropping"));
    cm2_bh_mlo_vif_report_mld(vif, NULL);
    FREE(vif->vif_name);
    FREE(vif);
}

static cm2_bh_mlo_vif_t *cm2_bh_mlo_vif_alloc(cm2_bh_mlo_t *m, const char *vif_name)
{
    if (m == NULL) return NULL;
    if (vif_name == NULL) return NULL;
    cm2_bh_mlo_vif_t *vif = CALLOC(1, sizeof(*vif));
    vif->m = m;
    vif->vif_name = STRDUP(vif_name);
    ds_tree_insert(&m->vifs, vif, vif->vif_name);
    LOGI(LOG_PREFIX_VIF(vif, "allocated"));
    CM2_BH_OVS_INIT(m, cm2_bh_mlo_WVS, Wifi_VIF_State, if_name, vif->vif_name);
    return vif;
}

static cm2_work_t *cm2_bh_mlo_mld_alloc_work(cm2_bh_mlo_mld_t *mld)
{
    cm2_work_t *w = cm2_work_alloc();
    cm2_work_set_deadline_sec(w, CM2_BH_MLO_DEADLINE_SEC);
    cm2_work_set_cooldown_sec(w, CM2_BH_MLO_COOLDOWN_SEC);
    cm2_work_set_fn(w, cm2_bh_mlo_mld_work_cb, mld);
    cm2_work_set_log_prefix(w, strfmta(LOG_PREFIX_MLD(mld, "")));
    return w;
}

static cm2_bh_mlo_mld_t *cm2_bh_mlo_mld_alloc(cm2_bh_mlo_t *m, const char *mld_name)
{
    if (m == NULL) return NULL;
    if (mld_name == NULL) return NULL;
    const char *gre_name = strfmta(CM2_BH_GRE_PREFIX "%s", mld_name);
    cm2_bh_mlo_mld_t *mld = CALLOC(1, sizeof(*mld));
    ds_tree_init(&mld->vifs, ds_str_cmp, cm2_bh_mlo_vif_t, node_mld);
    mld->m = m;
    mld->mld_name = STRDUP(mld_name);
    mld->work = cm2_bh_mlo_mld_alloc_work(mld);
    mld->cmu_gre = cm2_bh_cmu_gre_alloc(m->cmu, gre_name, mld_name);
    mld->cmu_vif = cm2_bh_cmu_vif_alloc(m->cmu, mld_name);
    mld->dhcp = cm2_bh_dhcp_vif_alloc(m->dhcp, mld_name);
    ds_tree_insert(&m->mlds, mld, mld->mld_name);
    LOGI(LOG_PREFIX_MLD(mld, "allocated"));
    return mld;
}

cm2_bh_mlo_t *cm2_bh_mlo_alloc(cm2_bh_dhcp_t *dhcp, cm2_bh_gre_t *gre, cm2_bh_cmu_t *cmu)
{
    cm2_bh_mlo_t *m = CALLOC(1, sizeof(*m));
    m->dhcp = dhcp;
    m->gre = gre;
    m->cmu = cmu;
    ds_tree_init(&m->vifs, ds_str_cmp, cm2_bh_mlo_vif_t, node);
    ds_tree_init(&m->mlds, ds_str_cmp, cm2_bh_mlo_mld_t, node);
    LOGI(LOG_PREFIX(m, "allocated"));
    return m;
}

void cm2_bh_mlo_drop(cm2_bh_mlo_t *m)
{
    if (m == NULL) return;

    LOGI(LOG_PREFIX(m, "dropping"));

    cm2_bh_mlo_vif_t *vif;
    while ((vif = ds_tree_head(&m->vifs)) != NULL)
    {
        cm2_bh_mlo_vif_drop(vif);
    }

    cm2_bh_mlo_mld_t *mld;
    while ((mld = ds_tree_head(&m->mlds)) != NULL)
    {
        cm2_bh_mlo_mld_drop(mld);
    }

    FREE(m);
}

static cm2_bh_mlo_vif_t *cm2_bh_mlo_lookup_vif(cm2_bh_mlo_t *m, const char *vif_name)
{
    if (m == NULL) return NULL;
    if (vif_name == NULL) return NULL;
    return ds_tree_find(&m->vifs, vif_name);
}

static cm2_bh_mlo_mld_t *cm2_bh_mlo_lookup_mld(cm2_bh_mlo_t *m, const char *mld_name)
{
    if (m == NULL) return NULL;
    if (mld_name == NULL) return NULL;
    return ds_tree_find(&m->mlds, mld_name);
}

void cm2_bh_mlo_WVS(
        cm2_bh_mlo_t *m,
        ovsdb_update_monitor_t *mon,
        const struct schema_Wifi_VIF_State *old_row,
        const struct schema_Wifi_VIF_State *new_row)
{
    if (mon->mon_type == OVSDB_UPDATE_DEL) new_row = NULL;
    const char *if_name = CM2_OVS_COL(mon, old_row->if_name, new_row->if_name);
    cm2_bh_mlo_vif_t *vif = cm2_bh_mlo_lookup_vif(m, if_name) ?: cm2_bh_mlo_vif_alloc(m, if_name);

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, mld_if_name)))
    {
        const char *mld_name = new_row && new_row->mld_if_name_exists && (strlen(new_row->mld_if_name) > 0)
                                       ? new_row->mld_if_name
                                       : NULL;
        cm2_bh_mlo_mld_t *mld = cm2_bh_mlo_lookup_mld(m, mld_name) ?: cm2_bh_mlo_mld_alloc(m, mld_name);
        cm2_bh_mlo_vif_report_mld(vif, mld);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, mode)))
    {
        const bool v = new_row && new_row->mode_exists && (strcmp(new_row->mode, SCHEMA_CONSTS_VIF_MODE_STA) == 0);
        cm2_bh_mlo_vif_report_wvs_sta(vif, v);
    }

    if (ovsdb_update_changed(mon, SCHEMA_COLUMN(Wifi_VIF_State, wds)))
    {
        const bool v = new_row && new_row->wds_exists && new_row->wds;
        cm2_bh_mlo_vif_report_wvs_4addr(vif, v);
    }
}
