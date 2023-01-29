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

#include <util.h>
#include <const.h>
#include <log.h>
#include <osw_drv_dummy.h>
#include <osw_ut.h>
#include <memutil.h>

/* FIXME: rename _ops_ to _op_ in function names for osw_drv_ops */

static struct osw_drv_dummy_phy *
osw_drv_dummy_phy_lookup(struct osw_drv_dummy *dummy,
                         const char *phy_name)
{
    return ds_tree_find(&dummy->phy_tree, phy_name);
}

static struct osw_drv_dummy_vif *
osw_drv_dummy_vif_lookup(struct osw_drv_dummy *dummy,
                         const char *phy_name,
                         const char *vif_name)
{
    struct osw_drv_dummy_vif *vif = ds_tree_find(&dummy->vif_tree, vif_name);
    if (vif == NULL) return NULL;
    if (strcmp(vif->phy_name.buf, phy_name) != 0) return NULL;
    return vif;
}

static struct osw_drv_dummy_sta *
osw_drv_dummy_sta_lookup(struct osw_drv_dummy *dummy,
                         const char *phy_name,
                         const char *vif_name,
                         const struct osw_hwaddr *sta_addr)
{
    struct osw_drv_dummy_sta *sta;

    ds_dlist_foreach(&dummy->sta_list, sta)
        if (strlen(sta->phy_name.buf) == 0 ||
            phy_name == NULL ||
            strcmp(sta->phy_name.buf, phy_name) == 0)
            if (strcmp(sta->vif_name.buf, vif_name) == 0)
                if (memcmp(&sta->sta_addr, sta_addr, sizeof(*sta_addr)) == 0)
                    return sta;

    return NULL;
}

static const char *
osw_drv_dummy_vif_to_phy(struct osw_drv_dummy *dummy,
                         const char *vif_name)
{
    struct osw_drv_dummy_vif *vif = ds_tree_find(&dummy->vif_tree, vif_name);
    if (vif == NULL) return NULL;
    return vif->phy_name.buf;
}

static void
osw_drv_dummy_ops_init_cb(struct osw_drv *drv)
{
    const struct osw_drv_ops *ops = osw_drv_get_ops(drv);
    struct osw_drv_dummy *dummy = container_of(ops, struct osw_drv_dummy, ops);
    osw_drv_set_priv(drv, dummy);
    dummy->drv = drv;
    if (dummy->init_fn != NULL) dummy->init_fn(drv);
}

static void
osw_drv_dummy_ops_get_phy_list_cb(struct osw_drv *drv,
                                  osw_drv_report_phy_fn_t *report_phy_fn,
                                  void *fn_priv)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    struct osw_drv_dummy_phy *phy;
    struct osw_drv_dummy_phy *tmp;

    ds_tree_foreach_safe(&dummy->phy_tree, phy, tmp)
        report_phy_fn(phy->phy_name.buf, fn_priv);
}

static void
osw_drv_dummy_ops_get_vif_list_cb(struct osw_drv *drv,
                                  const char *phy_name,
                                  osw_drv_report_vif_fn_t *report_vif_fn,
                                  void *fn_priv)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    struct osw_drv_dummy_vif *vif;
    struct osw_drv_dummy_vif *tmp;

    ds_tree_foreach_safe(&dummy->vif_tree, vif, tmp)
        if (strcmp(vif->phy_name.buf, phy_name) == 0)
            report_vif_fn(vif->vif_name.buf, fn_priv);
}

static void
osw_drv_dummy_ops_get_sta_list_cb(struct osw_drv *drv,
                                  const char *phy_name,
                                  const char *vif_name,
                                  osw_drv_report_sta_fn_t *report_sta_fn,
                                  void *fn_priv)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    struct osw_drv_dummy_sta *sta;
    struct osw_drv_dummy_sta *tmp;

    ds_dlist_foreach_safe(&dummy->sta_list, sta, tmp)
        if (strlen(sta->phy_name.buf) == 0 || strcmp(sta->phy_name.buf, phy_name) == 0)
            if (strcmp(sta->vif_name.buf, vif_name) == 0)
                report_sta_fn(&sta->sta_addr, fn_priv);
}

static void
osw_drv_dummy_ops_get_phy_state_cb(struct osw_drv *drv,
                                   const char *phy_name)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    struct osw_drv_dummy_phy *phy = osw_drv_dummy_phy_lookup(dummy, phy_name);
    struct osw_drv_phy_state state = { .exists = false };

    if (phy != NULL)
        state = phy->state;

    osw_drv_report_phy_state(drv, phy_name, &state);
}

static void
osw_drv_dummy_ops_get_vif_state_cb(struct osw_drv *drv,
                                   const char *phy_name,
                                   const char *vif_name)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    struct osw_drv_dummy_vif *vif = osw_drv_dummy_vif_lookup(dummy, phy_name, vif_name);
    struct osw_drv_vif_state state = { .exists = false };

    if (vif != NULL)
        state = vif->state;

    osw_drv_report_vif_state(drv, phy_name, vif_name, &state);
}

static void
osw_drv_dummy_ops_get_sta_state_cb(struct osw_drv *drv,
                                   const char *phy_name,
                                   const char *vif_name,
                                   const struct osw_hwaddr *sta_addr)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    struct osw_drv_dummy_sta *sta = osw_drv_dummy_sta_lookup(dummy, phy_name, vif_name, sta_addr);
    struct osw_drv_sta_state state = { .connected = false };

    if (sta != NULL)
        state = sta->state;

    osw_drv_report_sta_state(drv, phy_name, vif_name, sta_addr, &state);

    if (sta != NULL) {
        osw_drv_report_sta_assoc_ies(drv, phy_name, vif_name, sta_addr, sta->ies, sta->ies_len);
    }
}

static void
osw_drv_dummy_init_struct(struct osw_drv_dummy *dummy)
{
    const struct osw_drv_ops ops = {
        .name = dummy->name,
        .init_fn = osw_drv_dummy_ops_init_cb,
        .get_phy_list_fn = osw_drv_dummy_ops_get_phy_list_cb,
        .get_vif_list_fn = osw_drv_dummy_ops_get_vif_list_cb,
        .get_sta_list_fn = osw_drv_dummy_ops_get_sta_list_cb,
        .request_phy_state_fn = osw_drv_dummy_ops_get_phy_state_cb,
        .request_vif_state_fn = osw_drv_dummy_ops_get_vif_state_cb,
        .request_sta_state_fn = osw_drv_dummy_ops_get_sta_state_cb,
        .request_config_fn = dummy->request_config_fn,
        .request_stats_fn = dummy->request_stats_fn,
        .request_sta_deauth_fn = dummy->request_sta_deauth_fn,
        .push_frame_tx_fn = dummy->push_frame_tx_fn,
    };

    ds_tree_init(&dummy->phy_tree, ds_str_cmp, struct osw_drv_dummy_phy, node);
    ds_tree_init(&dummy->vif_tree, ds_str_cmp, struct osw_drv_dummy_vif, node);
    ds_dlist_init(&dummy->sta_list, struct osw_drv_dummy_sta, node);
    dummy->ops = ops;
}

void
osw_drv_dummy_init(struct osw_drv_dummy *dummy)
{
    osw_drv_dummy_init_struct(dummy);
    osw_drv_register_ops(&dummy->ops);
}

static void
osw_drv_dummy_fini_struct(struct osw_drv_dummy *dummy)
{
    struct osw_drv_dummy_phy *phy;
    struct osw_drv_dummy_vif *vif;
    struct osw_drv_dummy_sta *sta;

    while ((phy = ds_tree_head(&dummy->phy_tree)) != NULL)
        osw_drv_dummy_set_phy(dummy, phy->phy_name.buf, NULL);

    while ((vif = ds_tree_head(&dummy->vif_tree)) != NULL)
        osw_drv_dummy_set_vif(dummy, vif->phy_name.buf, vif->vif_name.buf, NULL);

    while ((sta = ds_dlist_head(&dummy->sta_list)) != NULL)
        osw_drv_dummy_set_sta(dummy, sta->phy_name.buf, sta->vif_name.buf, &sta->sta_addr, NULL);
}

void
osw_drv_dummy_fini(struct osw_drv_dummy *dummy)
{
    osw_drv_dummy_fini_struct(dummy);
    osw_drv_unregister_ops(&dummy->ops);
}

void
osw_drv_dummy_set_phy(struct osw_drv_dummy *dummy,
                      const char *phy_name,
                      struct osw_drv_phy_state *state)
{
    struct osw_drv_dummy_phy *phy = ds_tree_find(&dummy->phy_tree, phy_name);
    osw_drv_report_phy_changed(dummy->drv, phy_name);
    if (phy == NULL) {
        phy = CALLOC(1, sizeof(*phy));
        STRSCPY_WARN(phy->phy_name.buf, phy_name);
        ds_tree_insert(&dummy->phy_tree, phy, phy->phy_name.buf);
    } else {
        if (dummy->fini_phy_fn != NULL) {
            dummy->fini_phy_fn(dummy, &phy->state);
        }
    }
    if (state != NULL) {
        osw_drv_phy_state_report_free(&phy->state);
        phy->state = *state;
    }
    if (state == NULL) {
        ds_tree_remove(&dummy->phy_tree, phy);
        FREE(phy);
    }
}

static void
osw_drv_dummy_update_sta(struct osw_drv_dummy *dummy)
{
    struct osw_drv_dummy_sta *sta;

    /* FIXME: This isn't optimal to run every time vif is
     * updated. This should have a dedicated pending list
     * for stations without phy name mapped to them.
     */
    ds_dlist_foreach(&dummy->sta_list, sta) {
        if (strlen(sta->phy_name.buf) > 0) continue;
        const char *phy_name = osw_drv_dummy_vif_to_phy(dummy, sta->vif_name.buf);
        LOGD("osw: drv: dummy: %s/"OSW_HWADDR_FMT": updating phy name to %s",
             sta->vif_name.buf,
             OSW_HWADDR_ARG(&sta->sta_addr),
             phy_name);
        osw_drv_dummy_set_sta(dummy, phy_name, sta->vif_name.buf, &sta->sta_addr, &sta->state);
    }
}

void
osw_drv_dummy_set_vif(struct osw_drv_dummy *dummy,
                      const char *phy_name,
                      const char *vif_name,
                      struct osw_drv_vif_state *state)
{
    struct osw_drv_dummy_vif *vif = ds_tree_find(&dummy->vif_tree, vif_name);
    osw_drv_report_vif_changed(dummy->drv, phy_name, vif_name);
    if (vif == NULL) {
        vif = CALLOC(1, sizeof(*vif));
        STRSCPY_WARN(vif->phy_name.buf, phy_name);
        STRSCPY_WARN(vif->vif_name.buf, vif_name);
        ds_tree_insert(&dummy->vif_tree, vif, vif->vif_name.buf);
        osw_drv_dummy_update_sta(dummy);
    } else {
        if (dummy->fini_vif_fn != NULL) {
            dummy->fini_vif_fn(dummy, &vif->state);
        }
    }
    if (state != NULL) {
        osw_drv_vif_state_report_free(&vif->state);
        vif->state = *state;
    }
    if (state == NULL) {
        ds_tree_remove(&dummy->vif_tree, vif);
        FREE(vif);
    }
}

void
osw_drv_dummy_set_sta(struct osw_drv_dummy *dummy,
                      const char *phy_name,
                      const char *vif_name,
                      const struct osw_hwaddr *sta_addr,
                      struct osw_drv_sta_state *state)
{
    struct osw_drv_dummy_sta *sta = osw_drv_dummy_sta_lookup(dummy, phy_name, vif_name, sta_addr);
    if (phy_name == NULL)
        phy_name = osw_drv_dummy_vif_to_phy(dummy, vif_name);
    if (phy_name != NULL)
        osw_drv_report_sta_changed(dummy->drv, phy_name, vif_name, sta_addr);
    if (sta == NULL) {
        sta = CALLOC(1, sizeof(*sta));
        STRSCPY_WARN(sta->vif_name.buf, vif_name);
        sta->sta_addr = *sta_addr;
        ds_dlist_insert_tail(&dummy->sta_list, sta);
    } else {
        if (dummy->fini_sta_fn != NULL) {
            dummy->fini_sta_fn(dummy, &sta->state);
        }
    }
    if (phy_name != NULL && strlen(sta->phy_name.buf) == 0) {
        STRSCPY_WARN(sta->phy_name.buf, phy_name);
    }
    if (state != NULL) {
        osw_drv_sta_state_report_free(&sta->state);
        sta->state = *state;
    }
    if (state == NULL) {
        ds_dlist_remove(&dummy->sta_list, sta);
        FREE(sta);
    }
}

void
osw_drv_dummy_set_sta_ies(struct osw_drv_dummy *dummy,
                          const char *phy_name,
                          const char *vif_name,
                          const struct osw_hwaddr *sta_addr,
                          const void *ies,
                          size_t ies_len)
{
    struct osw_drv_dummy_sta *sta = osw_drv_dummy_sta_lookup(dummy, phy_name, vif_name, sta_addr);

    /* FIXME: This currently requires that
     * osw_drv_dummy_set_sta() is called _before_
     * osw_drv_dummy_set_sta_ies(). This is unfortunate but
     * acceptable for now. Mark it down with WARN_ON() so
     * that if it happens to be called out of order by
     * mistake it is visible.
     */
    if (WARN_ON(sta == NULL)) return;

    if (phy_name == NULL) {
        phy_name = sta->phy_name.buf;
    }

    sta->ies = MEMNDUP(ies, ies_len);
    sta->ies_len = ies_len;

    if (strlen(phy_name) > 0) {
        /* osw_drv_report_sta_assoc_ies() is called when sta
         * state is requested. THat's the safest and easiest
         * way to propagate IEs with the dummy driver given
         * it has little control over when states become
         * valid.
         *
         * If phy_name isn't known yet then sta_changed()
         * and sta request will get called eventually.
         */
        osw_drv_report_sta_changed(dummy->drv, phy_name, vif_name, sta_addr);
    }
}

void
osw_drv_dummy_iter_sta(struct osw_drv_dummy *dummy,
                       osw_drv_dummy_iter_sta_fn_t *fn,
                       void *fn_data)
{
    struct osw_drv_dummy_sta *sta;
    struct osw_drv_dummy_sta *tmp;

    ds_dlist_foreach_safe(&dummy->sta_list, sta, tmp)
        fn(dummy, sta->phy_name.buf, sta->vif_name.buf, &sta->sta_addr, fn_data);
}

struct osw_drv_dummy_ut_1 {
    struct osw_drv_dummy dummy;
    int n_phy_fini;
    int n_vif_fini;
    int n_sta_fini;
};

static void
osw_drv_dummy_ut_1_fini_phy_cb(struct osw_drv_dummy *dummy,
                               struct osw_drv_phy_state *state)
{
    struct osw_drv_dummy_ut_1 *ut = container_of(dummy, struct osw_drv_dummy_ut_1, dummy);
    ut->n_phy_fini++;
}

static void
osw_drv_dummy_ut_1_fini_vif_cb(struct osw_drv_dummy *dummy,
                               struct osw_drv_vif_state *state)
{
    struct osw_drv_dummy_ut_1 *ut = container_of(dummy, struct osw_drv_dummy_ut_1, dummy);
    ut->n_vif_fini++;
}

static void
osw_drv_dummy_ut_1_fini_sta_cb(struct osw_drv_dummy *dummy,
                               struct osw_drv_sta_state *state)
{
    struct osw_drv_dummy_ut_1 *ut = container_of(dummy, struct osw_drv_dummy_ut_1, dummy);
    ut->n_sta_fini++;
}

OSW_UT(osw_drv_dummy_ut_1)
{
    struct osw_drv_dummy_ut_1 ut = {
        .dummy = {
            .name = "foo",
            .fini_phy_fn = osw_drv_dummy_ut_1_fini_phy_cb,
            .fini_vif_fn = osw_drv_dummy_ut_1_fini_vif_cb,
            .fini_sta_fn = osw_drv_dummy_ut_1_fini_sta_cb,
        },
    };
    struct osw_drv_phy_state phy1 = {0};
    struct osw_drv_vif_state vif1 = {0};
    struct osw_drv_sta_state sta1 = {0};
    struct osw_hwaddr sta1_addr = {0};

    osw_drv_dummy_init_struct(&ut.dummy);
    osw_drv_dummy_set_phy(&ut.dummy, "phy1", &phy1);
    osw_drv_dummy_set_vif(&ut.dummy, "phy1", "vif1", &vif1);
    osw_drv_dummy_set_sta(&ut.dummy, "phy1", "vif1", &sta1_addr, &sta1);
    assert(osw_drv_dummy_phy_lookup(&ut.dummy, "phy1") != NULL);
    assert(osw_drv_dummy_vif_lookup(&ut.dummy, "phy1", "vif1") != NULL);
    assert(osw_drv_dummy_sta_lookup(&ut.dummy, "phy1", "vif1", &sta1_addr) != NULL);
    assert(ds_tree_head(&ut.dummy.phy_tree) != NULL);
    assert(ds_tree_head(&ut.dummy.vif_tree) != NULL);
    assert(ds_dlist_head(&ut.dummy.sta_list) != NULL);
    assert(ut.n_phy_fini == 0);
    assert(ut.n_vif_fini == 0);
    assert(ut.n_sta_fini == 0);
    osw_drv_dummy_set_phy(&ut.dummy, "phy1", NULL);
    osw_drv_dummy_set_vif(&ut.dummy, "phy1", "vif1", NULL);
    osw_drv_dummy_set_sta(&ut.dummy, "phy1", "vif1", &sta1_addr, NULL);
    assert(ds_tree_head(&ut.dummy.phy_tree) == NULL);
    assert(ds_tree_head(&ut.dummy.vif_tree) == NULL);
    assert(ds_dlist_head(&ut.dummy.sta_list) == NULL);
    assert(ut.n_phy_fini == 1);
    assert(ut.n_vif_fini == 1);
    assert(ut.n_sta_fini == 1);
    assert(osw_drv_dummy_phy_lookup(&ut.dummy, "phy1") == NULL);
    assert(osw_drv_dummy_vif_lookup(&ut.dummy, "phy1", "vif1") == NULL);
    assert(osw_drv_dummy_sta_lookup(&ut.dummy, "phy1", "vif1", &sta1_addr) == NULL);
    osw_drv_dummy_set_phy(&ut.dummy, "phy1", &phy1);
    osw_drv_dummy_set_vif(&ut.dummy, "phy1", "vif1", &vif1);
    osw_drv_dummy_set_sta(&ut.dummy, "phy1", "vif1", &sta1_addr, &sta1);
    assert(ds_tree_head(&ut.dummy.phy_tree) != NULL);
    assert(ds_tree_head(&ut.dummy.vif_tree) != NULL);
    assert(ds_dlist_head(&ut.dummy.sta_list) != NULL);
    assert(ut.n_phy_fini == 1);
    assert(ut.n_vif_fini == 1);
    assert(ut.n_sta_fini == 1);
    osw_drv_dummy_fini_struct(&ut.dummy);
    assert(ds_tree_head(&ut.dummy.phy_tree) == NULL);
    assert(ds_tree_head(&ut.dummy.vif_tree) == NULL);
    assert(ds_dlist_head(&ut.dummy.sta_list) == NULL);
    assert(ut.n_phy_fini == 2);
    assert(ut.n_vif_fini == 2);
    assert(ut.n_sta_fini == 2);
    assert(osw_drv_dummy_phy_lookup(&ut.dummy, "phy1") == NULL);
    assert(osw_drv_dummy_vif_lookup(&ut.dummy, "phy1", "vif1") == NULL);
    assert(osw_drv_dummy_sta_lookup(&ut.dummy, "phy1", "vif1", &sta1_addr) == NULL);
    osw_drv_dummy_set_phy(&ut.dummy, "phy1", &phy1);
    osw_drv_dummy_set_vif(&ut.dummy, "phy1", "vif1", &vif1);
    osw_drv_dummy_set_sta(&ut.dummy, "phy1", "vif1", &sta1_addr, &sta1);
    assert(ds_tree_head(&ut.dummy.phy_tree) != NULL);
    assert(ds_tree_head(&ut.dummy.vif_tree) != NULL);
    assert(ds_dlist_head(&ut.dummy.sta_list) != NULL);
    assert(ut.n_phy_fini == 2);
    assert(ut.n_vif_fini == 2);
    assert(ut.n_sta_fini == 2);
    osw_drv_dummy_set_phy(&ut.dummy, "phy1", NULL);
    osw_drv_dummy_set_vif(&ut.dummy, "phy1", "vif1", NULL);
    osw_drv_dummy_set_sta(&ut.dummy, "phy1", "vif1", &sta1_addr, NULL);
    assert(ds_tree_head(&ut.dummy.phy_tree) == NULL);
    assert(ds_tree_head(&ut.dummy.vif_tree) == NULL);
    assert(ds_dlist_head(&ut.dummy.sta_list) == NULL);
    assert(ut.n_phy_fini == 3);
    assert(ut.n_vif_fini == 3);
    assert(ut.n_sta_fini == 3);
    osw_drv_dummy_set_phy(&ut.dummy, "phy1", NULL);
    osw_drv_dummy_set_vif(&ut.dummy, "phy1", "vif1", NULL);
    osw_drv_dummy_set_sta(&ut.dummy, "phy1", "vif1", &sta1_addr, NULL);
    assert(ds_tree_head(&ut.dummy.phy_tree) == NULL);
    assert(ds_tree_head(&ut.dummy.vif_tree) == NULL);
    assert(ds_dlist_head(&ut.dummy.sta_list) == NULL);
    assert(ut.n_phy_fini == 3);
    assert(ut.n_vif_fini == 3);
    assert(ut.n_sta_fini == 3);
}

OSW_MODULE(osw_drv_dummy)
{
    return NULL;
}
