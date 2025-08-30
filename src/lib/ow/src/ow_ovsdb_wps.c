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

#include <log.h>
#include <const.h>
#include <memutil.h>
#include <ds_tree.h>
#include <ds_dlist.h>
#include <ovsdb_cache.h>
#include <schema_consts.h>
#include <schema_compat.h>
#include <osw_module.h>
#include <osw_time.h>
#include <osw_timer.h>
#include <osw_drv.h>
#include <osw_types.h>
#include "ow_ovsdb_wps.h"
#include "ow_wps.h"

/**
 * Purpose
 *
 * Hooks up OVSDB to ow_wps module to handle WPS PBC.
 *
 * Operation
 *
 * Due to OVSDB design the device self-clears wps_pbc column
 * in Wifi_VIF_Config when it respects it.
 *
 * As such there are 3 cases to handle:
 *  A. column is unset
 *  B. column is set to true
 *  C. column is set to false
 *
 * The (A) case means "do nothing".
 *  - If WPS PBC job is under way it is left running.
 *  - If WPS PBC job was not running nothing is done.
 *
 * The (B) case means "start".
 *  - If WPS PBC job is already under way it is cancelled
 *    and a new one is started.
 *  - If WPS PBC job was not running a new job is started.
 *  - Wifi_VIF_Config::wps_pbc is self-cleared
 *
 * The (C) case means "stop".
 *  - If WPS PBC job is already under way it is cancelled.
 *  - If WPS PBC job was not running nothing is done.
 *  - Wifi_VIF_Config::wps_pbc is self-cleared
 *
 * The module makes sure to defer scheduling a new wps job
 * if previous one has not been fully completed yet
 * (cancellation and dropping is async).
 *
 * The module always tries to report adequate wps job state
 * to Wifi_VIF_State.
 */

#define LOG_PREFIX(fmt, ...) \
    "ow: ovsdb: wps: " fmt, ## __VA_ARGS__

#define LOG_PREFIX_VIF(vif_name, fmt, ...) \
    LOG_PREFIX("%s: " fmt, vif_name, ## __VA_ARGS__)

struct ow_ovsdb_wps {
    struct ow_ovsdb_wps_ops ops;
    struct ow_wps_ops *wps;
    struct ds_tree vifs;
    struct ds_dlist changed;
    ovsdb_table_t *table_Wifi_VIF_Config;
};

struct ow_ovsdb_wps_vif {
    struct ow_ovsdb_wps *wps;
    struct ds_tree_node node;
    struct schema_Wifi_VIF_Config vconf;
    char *vif_name;
    struct ow_wps_job *job;
    struct osw_wps_cred cred;
    bool exists;
    bool running;
    bool starting;
};

struct ow_ovsdb_wps_changed {
    struct ow_ovsdb_wps *wps;
    struct ds_dlist_node node;
    ow_ovsdb_wps_changed_fn_t *fn;
    void *fn_priv;
};

static  struct ow_ovsdb_wps_vif *
ow_ovsdb_wps_vif_alloc(struct ow_ovsdb_wps *wps,
                       const char *vif_name)
{
    struct ow_ovsdb_wps_vif *vif = CALLOC(1, sizeof(*vif));
    vif->vif_name = STRDUP(vif_name);
    vif->wps = wps;
    ds_tree_insert(&wps->vifs, vif, vif->vif_name);
    return vif;
}

static struct ow_ovsdb_wps_vif *
ow_ovsdb_wps_vif_get(struct ow_ovsdb_wps *wps,
                     const char *vif_name)
{
    return ds_tree_find(&wps->vifs, vif_name) ?: ow_ovsdb_wps_vif_alloc(wps, vif_name);
}

static void
ow_ovsdb_wps_vif_drop(struct ow_ovsdb_wps_vif *vif)
{
    ds_tree_remove(&vif->wps->vifs, vif);

    if (vif->job != NULL) {
        ow_wps_op_job_cancel(vif->wps->wps, vif->job);
        ow_wps_op_job_drop(vif->wps->wps, vif->job);
        vif->job = NULL;
    }

    FREE(vif->vif_name);
    FREE(vif);
}

static const char *
ow_ovsdb_wps_vconf_lookup_key_id(const struct schema_Wifi_VIF_Config *vconf,
                                 const int pbc_key_id)
{
    int i;
    for (i = 0; i < vconf->wpa_psks_len; i++) {
        const char *key_id_cstr = vconf->wpa_psks_keys[i];
        const char *psk = vconf->wpa_psks[i];
        int key_id;
        const bool key_id_ok = schema_key_id_from_cstr(key_id_cstr, &key_id);
        const bool key_id_not_ready = !key_id_ok;
        if (WARN_ON(key_id_not_ready)) continue;
        if (key_id == pbc_key_id) return psk;
    }
    return NULL;
}

static void
ow_ovsdb_wps_vif_unset_wps_pbc(struct ow_ovsdb_wps_vif *vif)
{
    struct ow_ovsdb_wps *wps = vif->wps;
    const char *vif_name = vif->vif_name;
    struct schema_Wifi_VIF_Config vconf;

    if (WARN_ON(wps->table_Wifi_VIF_Config == NULL)) return;

    MEMZERO(vconf);
    vconf._partial_update = true;
    SCHEMA_SET_STR(vconf.if_name, vif_name);
    SCHEMA_UNSET_FIELD(vconf.wps_pbc);
    ovsdb_table_update(wps->table_Wifi_VIF_Config, &vconf);
}

static void
ow_ovsdb_wps_vif_setup_pbc_creds(struct ow_ovsdb_wps_vif *vif)
{
    if (vif == NULL) return;
    if (vif->running) return;

    const struct schema_Wifi_VIF_Config *vconf = &vif->vconf;
    struct ow_wps_job *job = vif->job;

    if (vconf->wps_pbc_exists == false) {
        return;
    }

    int pbc_key_id;
    const bool pbc_key_id_ok = schema_key_id_from_cstr(vconf->wps_pbc_key_id, &pbc_key_id);
    const bool pbc_key_id_not_ready = !pbc_key_id_ok;
    if (pbc_key_id_not_ready) {
        ow_wps_op_job_set_creds(vif->wps->wps, job, NULL);
        return;
    }

    const char *psk = ow_ovsdb_wps_vconf_lookup_key_id(vconf, pbc_key_id);
    const bool psk_not_found = (psk == NULL);
    if (WARN_ON(psk_not_found)) {
        ow_wps_op_job_set_creds(vif->wps->wps, job, NULL);
        return;
    }

    MEMZERO(vif->cred);
    STRSCPY_WARN(vif->cred.psk.str, psk);

    const struct osw_wps_cred_list creds = {
        .list = &vif->cred,
        .count = 1,
    };

    ow_wps_op_job_set_creds(vif->wps->wps, job, &creds);
}

static void
ow_ovsdb_wps_vif_setup_pbc_start(struct ow_ovsdb_wps_vif *vif)
{
    if (vif == NULL) return;

    const struct schema_Wifi_VIF_Config *vconf = &vif->vconf;
    if (vconf->wps_pbc_exists == false) return;
    if (vconf->wps_pbc == false) return;

    ow_wps_op_job_start(vif->wps->wps, vif->job);
    ow_ovsdb_wps_vif_unset_wps_pbc(vif);
    vif->starting = true;
}

static void
ow_ovsdb_wps_vif_setup_pbc_stop(struct ow_ovsdb_wps_vif *vif)
{
    if (vif == NULL) return;

    const struct schema_Wifi_VIF_Config *vconf = &vif->vconf;
    if (vconf->wps_pbc_exists == false) return;
    if (vconf->wps_pbc == true) return;

    ow_wps_op_job_cancel(vif->wps->wps, vif->job);
    ow_ovsdb_wps_vif_unset_wps_pbc(vif);
}

static void
ow_ovsdb_wps_vif_notify_changed(struct ow_ovsdb_wps_vif *vif)
{
    struct ow_ovsdb_wps *wps = vif->wps;
    const char *vif_name = vif->vif_name;
    struct ow_ovsdb_wps_changed *w;
    ds_dlist_foreach(&wps->changed, w) {
        if (WARN_ON(w->fn == NULL)) continue;
        w->fn(w->fn_priv, vif_name);
    }
}

static void
ow_ovsdb_wps_notify_changed_init(struct ow_ovsdb_wps_changed *w)
{
    struct ow_ovsdb_wps_vif *vif;
    ds_tree_foreach(&w->wps->vifs, vif) {
        const char *vif_name = vif->vif_name;
        w->fn(w->fn_priv, vif_name);
    }
}

static void /* forward decl */
ow_ovsdb_wps_vif_setup(struct ow_ovsdb_wps_vif *vif);

static void
ow_ovsdb_wps_job_update(struct ow_wps_ops *ops,
                        struct ow_wps_job *job,
                        const bool running)
{
    struct ow_ovsdb_wps_vif *vif = ow_wps_op_job_get_priv(ops, job);
    vif->starting = false;
    vif->running = running;
    ow_ovsdb_wps_vif_notify_changed(vif);
    ow_ovsdb_wps_vif_setup(vif);
}

static void
ow_ovsdb_wps_job_started_cb(struct ow_wps_ops *ops,
                            struct ow_wps_job *job)
{
    ow_ovsdb_wps_job_update(ops, job, true);
}

static void
ow_ovsdb_wps_job_finished_cb(struct ow_wps_ops *ops,
                             struct ow_wps_job *job)
{
    ow_ovsdb_wps_job_update(ops, job, false);
}

static void
ow_ovsdb_wps_vif_setup_pbc_create(struct ow_ovsdb_wps_vif *vif)
{
    if (vif == NULL) return;
    if (vif->job != NULL) return;

    const struct schema_Wifi_VIF_Config *vconf = &vif->vconf;
    if (vconf->wps_pbc_exists == false) return;
    if (vconf->wps_pbc == false) return;

    vif->job = ow_wps_op_alloc_job(vif->wps->wps,
                                   vconf->if_name,
                                   OW_WPS_ROLE_ENROLLER,
                                   OW_WPS_METHOD_PBC);

    ow_wps_op_job_set_priv(vif->wps->wps,
                           vif->job,
                           vif);
    ow_wps_op_job_set_callbacks(vif->wps->wps,
                                vif->job,
                                ow_ovsdb_wps_job_started_cb,
                                ow_ovsdb_wps_job_finished_cb);
}

static void
ow_ovsdb_wps_vif_setup_pbc_destroy(struct ow_ovsdb_wps_vif *vif)
{
    if (vif->job == NULL) return;
    if (vif->starting) return;
    if (vif->running) return;

    ow_wps_op_job_drop(vif->wps->wps, vif->job);
    vif->job = NULL;
}

static void
ow_ovsdb_wps_vif_setup(struct ow_ovsdb_wps_vif *vif)
{
    if (WARN_ON(vif == NULL)) return;

    const bool exists = vif->exists;

    if (exists == false) {
        SCHEMA_SET_BOOL(vif->vconf.wps_pbc, false);
        SCHEMA_UNSET_FIELD(vif->vconf.wps_pbc_key_id);
    }

    ow_ovsdb_wps_vif_setup_pbc_create(vif);
    ow_ovsdb_wps_vif_setup_pbc_creds(vif);
    ow_ovsdb_wps_vif_setup_pbc_start(vif);
    ow_ovsdb_wps_vif_setup_pbc_stop(vif);
    ow_ovsdb_wps_vif_setup_pbc_destroy(vif);

    if (vif->job == NULL) {
        ow_ovsdb_wps_vif_drop(vif);
    }
}

static void
ow_ovsdb_wps_flush(struct ow_ovsdb_wps *wps)
{
    struct ow_ovsdb_wps_vif *vif;
    while ((vif = ds_tree_remove_head(&wps->vifs)) != NULL) {
        ow_ovsdb_wps_vif_drop(vif);
    }
}

static void
ow_ovsdb_wps_set_vconf_table_cb(struct ow_ovsdb_wps_ops *ops,
                                ovsdb_table_t *table)
{
    if (WARN_ON(ops == NULL)) return;

    struct ow_ovsdb_wps *wps = container_of(ops, struct ow_ovsdb_wps, ops);
    if (wps->table_Wifi_VIF_Config == table) return;

    if (table == NULL) {
        ow_ovsdb_wps_flush(wps);
    }

    wps->table_Wifi_VIF_Config = table;
}

static struct ow_ovsdb_wps_changed *
ow_ovsdb_wps_add_changed_cb(struct ow_ovsdb_wps_ops *ops,
                            ow_ovsdb_wps_changed_fn_t *fn,
                            void *fn_priv)
{
    if (WARN_ON(ops == NULL)) return NULL;

    struct ow_ovsdb_wps *wps = container_of(ops, struct ow_ovsdb_wps, ops);
    struct ow_ovsdb_wps_changed *c = CALLOC(1, sizeof(*c));
    c->wps = wps;
    c->fn = fn;
    c->fn_priv = fn_priv;
    ds_dlist_insert_tail(&wps->changed, c);
    ow_ovsdb_wps_notify_changed_init(c);

    return c;
}

static void
ow_ovsdb_wps_del_changed_cb(struct ow_ovsdb_wps_ops *ops,
                            struct ow_ovsdb_wps_changed *c)
{
    if (WARN_ON(ops == NULL)) return;
    if (c == NULL) return;

    struct ow_ovsdb_wps *wps = container_of(ops, struct ow_ovsdb_wps, ops);
    WARN_ON(wps != c->wps);
    ds_dlist_remove(&c->wps->changed, c);
    FREE(c);
}

static void
ow_ovsdb_wps_handle_vconf_update_cb(struct ow_ovsdb_wps_ops *ops,
                                    ovsdb_update_monitor_t *mon,
                                    struct schema_Wifi_VIF_Config *old,
                                    struct schema_Wifi_VIF_Config *new,
                                    ovsdb_cache_row_t *row)
{
    if (WARN_ON(ops == NULL)) return;

    struct ow_ovsdb_wps *wps = container_of(ops, struct ow_ovsdb_wps, ops);
    const struct schema_Wifi_VIF_Config *vconf = (const void *)row->record;
    const char *vif_name = new->if_name;
    struct ow_ovsdb_wps_vif *vif = ow_ovsdb_wps_vif_get(wps, vif_name);

    vif->vconf = *vconf;
    schema_vconf_unify(&vif->vconf);

    switch (mon->mon_type) {
        case OVSDB_UPDATE_NEW:
        case OVSDB_UPDATE_MODIFY:
            vif->exists = true;
            break;
        case OVSDB_UPDATE_DEL:
            vif->exists = false;
            break;
        case OVSDB_UPDATE_ERROR:
            break;
    }

    ow_ovsdb_wps_vif_setup(vif);
}

static const char *
ow_ovsdb_wps_get_key_id_str(const struct ow_ovsdb_wps_vif *vif)
{
    const char *cred_psk = vif->cred.psk.str;
    const size_t cred_psk_len = strnlen(cred_psk, sizeof(vif->cred.psk.str));
    int i;
    for (i = 0; i < vif->vconf.wpa_psks_len; i++) {
        const char *key_id_str = vif->vconf.wpa_psks_keys[i];
        const char *psk = vif->vconf.wpa_psks[i];
        const size_t psk_len = strnlen(psk, sizeof(vif->vconf.wpa_psks[i]));
        /* This is trying carefully to guarantee no
         * out-of-bounds even if buffers get corrupted.
         */
        const bool matching = (cred_psk_len == psk_len)
                           && (strncmp(cred_psk, psk, psk_len) == 0);
        if (matching) {
            return key_id_str;
        }
    }
    return "";
}

static void
ow_ovsdb_wps_fill_vstate_cb(struct ow_ovsdb_wps_ops *ops,
                            struct schema_Wifi_VIF_State *vstate)
{
    if (WARN_ON(ops == NULL)) return;

    const char *vif_name = vstate->if_name;
    struct ow_ovsdb_wps *wps = container_of(ops, struct ow_ovsdb_wps, ops);
    struct ow_ovsdb_wps_vif *vif = ow_ovsdb_wps_vif_get(wps, vif_name);
    const bool running = (vif != NULL && vif->running);

    if (running) {
        const char *key_id_str = ow_ovsdb_wps_get_key_id_str(vif);
        SCHEMA_SET_BOOL(vstate->wps_pbc, true);
        SCHEMA_SET_STR(vstate->wps_pbc_key_id, key_id_str);

        const bool key_id_missing = (strlen(key_id_str) == 0);
        if (key_id_missing) {
            LOGN(LOG_PREFIX_VIF(vif_name, "vstate: could not infer wps_pbc_key_id, aborting"));
            /* This is dangerous UX-wise. Chances are
             * enrollee would be given a randomly generate
             * passphrase. This means that upon AP
             * reconfiguration the client would stop being
             * able to connect. This would be confusing to
             * users. Abort the entire WPS then hoping to
             * cancel before anyone is let in.
             */
            ow_wps_op_job_cancel(vif->wps->wps, vif->job);
        }
    }
    else {
        SCHEMA_SET_BOOL(vstate->wps_pbc, false);
        SCHEMA_SET_STR(vstate->wps_pbc_key_id, "");
    }
}

static void
ow_ovsdb_wps_init(struct ow_ovsdb_wps *ovsdb_wps)
{
    static const struct ow_ovsdb_wps_ops ops = {
        .set_vconf_table_fn = ow_ovsdb_wps_set_vconf_table_cb,
        .add_changed_fn = ow_ovsdb_wps_add_changed_cb,
        .del_changed_fn = ow_ovsdb_wps_del_changed_cb,
        .handle_vconf_update_fn = ow_ovsdb_wps_handle_vconf_update_cb,
        .fill_vstate_fn = ow_ovsdb_wps_fill_vstate_cb,
    };
    ASSERT(ovsdb_wps != NULL, "");

    memset(ovsdb_wps, 0, sizeof(*ovsdb_wps));
    ovsdb_wps->ops = ops;
    ds_tree_init(&ovsdb_wps->vifs, ds_str_cmp, struct ow_ovsdb_wps_vif, node);
    ds_dlist_init(&ovsdb_wps->changed, struct ow_ovsdb_wps_changed, node);
}

static void
ow_ovsdb_wps_attach(struct ow_ovsdb_wps *ovsdb_wps)
{
    ovsdb_wps->wps = OSW_MODULE_LOAD(ow_wps);
}

OSW_MODULE(ow_ovsdb_wps)
{
    static struct ow_ovsdb_wps ovsdb_wps;
    ow_ovsdb_wps_init(&ovsdb_wps);
    ow_ovsdb_wps_attach(&ovsdb_wps);
    return &ovsdb_wps.ops;
}
