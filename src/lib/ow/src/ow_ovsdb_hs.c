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
#include <ds_tree.h>
#include <ovsdb_cache.h>

#include <osw_types.h>
#include <osw_module.h>
#include <ow_steer_hs.h>
#include "ow_ovsdb_hs.h"

#define LOG_PREFIX(fmt, ...)          "ow: ovsdb: hs: " fmt, ##__VA_ARGS__
#define LOG_PREFIX_VIF(vif, fmt, ...) LOG_PREFIX("vif: %s: " fmt, (vif)->uuid ?: "uuid_missing", ##__VA_ARGS__)

struct ow_ovsdb_hs
{
    ow_steer_hs_t *m_hs;
    ds_tree_t vifs;
    ovsdb_table_t table_Hotspot_Steering;
};

struct ow_ovsdb_hs_vif
{
    ds_tree_node_t node;
    ow_ovsdb_hs_t *m;
    ow_steer_hs_vif_t *vif;
    char *if_name;
    char *uuid;
};

typedef struct ow_ovsdb_hs_vif ow_ovsdb_hs_vif_t;

static void ow_ovsdb_hs_vif_set_if_name(ow_ovsdb_hs_vif_t *vif, const char *if_name)
{
    const bool changed = (STRSCMP(vif->if_name, if_name) != 0);
    if (changed)
    {
        LOGI(LOG_PREFIX_VIF(vif, "if_name: '%s' -> '%s'", vif->if_name ?: "(none)", if_name ?: "(none)"));

        FREE(vif->if_name);
        vif->if_name = if_name ? STRDUP(if_name) : NULL;

        ow_steer_hs_vif_drop(vif->vif);
        vif->vif = vif->if_name ? ow_steer_hs_vif_alloc(vif->m->m_hs, vif->if_name) : NULL;
    }
}

static ow_ovsdb_hs_vif_t *ow_ovsdb_hs_vif_alloc(ow_ovsdb_hs_t *m, const char *uuid)
{
    ow_ovsdb_hs_vif_t *vif = CALLOC(1, sizeof(*vif));
    vif->uuid = STRDUP(uuid);
    vif->m = m;
    ds_tree_insert(&m->vifs, vif, vif->uuid);
    LOGI(LOG_PREFIX_VIF(vif, "allocated"));
    return vif;
}

static void ow_ovsdb_hs_vif_drop(ow_ovsdb_hs_vif_t *vif)
{
    if (vif == NULL) return;
    if (WARN_ON(vif->m == NULL)) return;
    LOGI(LOG_PREFIX_VIF(vif, "dropping"));
    ds_tree_remove(&vif->m->vifs, vif);
    ow_steer_hs_vif_drop(vif->vif);
    FREE(vif->if_name);
    FREE(vif);
}

static uint8_t ow_ovsdb_hs_dbm_to_db(const int dbm)
{
    const int nf = osw_channel_nf_20mhz_fixup(0); /* -96 */
    const int db = dbm - nf;                      /* eg. -80 - (-96) = 16 */
    WARN_ON(db < 0);
    return db >= 0 ? db : 0;
}

static void ow_ovsdb_hs_vif_set(ow_ovsdb_hs_t *m, const char *uuid, const struct schema_Hotspot_Steering *rec)
{
    if (m == NULL) return;
    if (uuid == NULL) return;
    if (WARN_ON(strlen(uuid) == 0)) return;
    ow_ovsdb_hs_vif_t *vif = ds_tree_find(&m->vifs, uuid) ?: ow_ovsdb_hs_vif_alloc(m, uuid);
    ow_ovsdb_hs_vif_set_if_name(vif, rec->if_name_exists ? rec->if_name : NULL);

    const uint8_t soft = rec->soft_snr_dbm_exists ? ow_ovsdb_hs_dbm_to_db(rec->soft_snr_dbm) : 0;
    const uint8_t hard = rec->hard_snr_dbm_exists ? ow_ovsdb_hs_dbm_to_db(rec->hard_snr_dbm) : 0;
    ow_steer_hs_vif_set_soft_snr_db(vif->vif, soft);
    ow_steer_hs_vif_set_hard_snr_db(vif->vif, hard);
}

static void ow_ovsdb_hs_vif_del(ow_ovsdb_hs_t *m, const char *uuid)
{
    ow_ovsdb_hs_vif_t *vif = ds_tree_find(&m->vifs, uuid);
    ow_ovsdb_hs_vif_drop(vif);
}

static void ow_ovsdb_hs_table_cb(
        ovsdb_update_monitor_t *mon,
        const struct schema_Hotspot_Steering *old,
        const struct schema_Hotspot_Steering *rec,
        ovsdb_cache_row_t *row)
{
    ovsdb_table_t *table = mon->mon_data;
    ow_ovsdb_hs_t *m = container_of(table, typeof(*m), table_Hotspot_Steering);
    const char *uuid = mon->mon_uuid;

    switch (mon->mon_type)
    {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            ow_ovsdb_hs_vif_set(m, uuid, rec);
            break;
        case OVSDB_UPDATE_DEL:
            ow_ovsdb_hs_vif_del(m, uuid);
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }
}

static void ow_ovsdb_hs_init(ow_ovsdb_hs_t *m)
{
    OVSDB_TABLE_VAR_INIT(&m->table_Hotspot_Steering, Hotspot_Steering, _uuid);
    ds_tree_init(&m->vifs, ds_str_cmp, ow_ovsdb_hs_vif_t, node);
}

static void ow_ovsdb_hs_attach(ow_ovsdb_hs_t *m)
{
    m->m_hs = OSW_MODULE_LOAD(ow_steer_hs);
}

void ow_ovsdb_hs_start(ow_ovsdb_hs_t *m)
{
    ovsdb_cache_monitor(&m->table_Hotspot_Steering, (void *)ow_ovsdb_hs_table_cb, true);
}

OSW_MODULE(ow_ovsdb_hs)
{
    static struct ow_ovsdb_hs m;
    ow_ovsdb_hs_init(&m);
    ow_ovsdb_hs_attach(&m);
    return &m;
}
