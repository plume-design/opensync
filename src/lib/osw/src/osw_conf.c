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

#include <stdint.h>
#include <osw_conf.h>
#include <ds_dlist.h>
#include <log.h>
#include <util.h>
#include <memutil.h>
#include <module.h>
#include <const.h>
#include <osw_state.h>
#include <osw_module.h>
#include <osw_ut.h>
#include <osw_etc.h>

/* FIXME: For this module to actually do the right thing it
 * needs to provide a changed_fn() observer callback that is
 * called whenever the resulting configuration tree is
 * different then last time it was generated. This means
 * that even osw_state-induced recalcs need to be respected
 * and considered as possible changed_fn(). However for that
 * to work the module needs to be able to compare old vs
 * new. With C that's not trivial, especially with nested
 * structures with a web of pointers.
 *
 * Until that comparison is possible changed_fn() is
 * impossible to do and would confuse osw_confsync because
 * it really needs to distinguish between true tree changes
 * and mere state updates.
 */

#define LOG_PREFIX(fmt, ...) "osw: conf: " fmt, ## __VA_ARGS__
#define LOG_PREFIX_MUT(fmt, ...) LOG_PREFIX("mutator: " fmt, ## __VA_ARGS__)

struct osw_conf {
    struct ds_dlist mutators;
    struct ds_dlist observers;
    char *ordering;
    struct osw_conf_mutator **ordered;
    size_t n_ordered;
};

static struct osw_conf g_osw_conf;

static bool
osw_conf_vif_status_to_conf(enum osw_vif_status status)
{
    switch (status) {
        case OSW_VIF_UNKNOWN: return false;
        case OSW_VIF_ENABLED: return true;
        case OSW_VIF_DISABLED: return false;
        case OSW_VIF_BROKEN: return false;
    }
    return false;
}

static void
osw_conf_build_vif_cb(const struct osw_state_vif_info *info,
                      void *ptr)
{
    struct osw_drv_vif_sta_network *snet;
    struct osw_conf_phy *phy = ptr;
    struct osw_conf_vif *vif = CALLOC(1, sizeof(*vif));
    size_t i;

    assert(info != NULL);
    assert(info->vif_name != NULL);
    assert(info->phy != NULL);
    assert(info->drv_state != NULL);

    vif->phy = phy;
    vif->mac_addr = info->drv_state->mac_addr;
    vif->vif_name = STRDUP(info->vif_name);
    vif->enabled = osw_conf_vif_status_to_conf(info->drv_state->status);
    vif->vif_type = info->drv_state->vif_type;
    vif->tx_power_dbm = info->drv_state->tx_power_dbm;
    switch (vif->vif_type) {
        case OSW_VIF_UNDEFINED:
            assert(0); /* driver bug, FIXME: dont use assert, be gentler and free memory */
            break;
        case OSW_VIF_AP:
            ds_tree_init(&vif->u.ap.acl_tree, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_conf_acl, node);
            ds_tree_init(&vif->u.ap.psk_tree, ds_int_cmp, struct osw_conf_psk, node);
            ds_tree_init(&vif->u.ap.neigh_tree, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_conf_neigh, node);
            ds_tree_init(&vif->u.ap.neigh_ft_tree, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_conf_neigh_ft, node);
            ds_dlist_init(&vif->u.ap.wps_cred_list, struct osw_conf_wps_cred, node);
            ds_dlist_init(&vif->u.ap.radius_list, struct osw_conf_radius, node);
            ds_dlist_init(&vif->u.ap.accounting_list, struct osw_conf_radius, node);
            vif->u.ap.acl_policy = info->drv_state->u.ap.acl_policy;
            vif->u.ap.ssid = info->drv_state->u.ap.ssid;
            vif->u.ap.channel = info->drv_state->u.ap.channel;
            vif->u.ap.mode = info->drv_state->u.ap.mode;
            STRSCPY_WARN(vif->u.ap.bridge_if_name.buf, info->drv_state->u.ap.bridge_if_name.buf ?: "");
            STRSCPY_WARN(vif->u.ap.nas_identifier.buf, info->drv_state->u.ap.nas_identifier.buf ?: "");
            STRSCPY_WARN(vif->u.ap.ft_encr_key.buf, info->drv_state->u.ap.ft_encr_key.buf ?: "");
            vif->u.ap.wpa = info->drv_state->u.ap.wpa;

            for (i = 0; i < info->drv_state->u.ap.acl.count; i++) {
                struct osw_conf_acl *acl = CALLOC(1, sizeof(*acl));
                acl->mac_addr = info->drv_state->u.ap.acl.list[i];
                ds_tree_insert(&vif->u.ap.acl_tree, acl, &acl->mac_addr);
            }

            for (i = 0; i < info->drv_state->u.ap.psk_list.count; i++) {
                struct osw_conf_psk *psk = CALLOC(1, sizeof(*psk));
                psk->ap_psk = info->drv_state->u.ap.psk_list.list[i];
                ds_tree_insert(&vif->u.ap.psk_tree, psk, &psk->ap_psk.key_id);
            }

            for (i = 0; i < info->drv_state->u.ap.neigh_list.count; i++) {
                struct osw_conf_neigh *neigh = CALLOC(1, sizeof(*neigh));
                neigh->neigh = info->drv_state->u.ap.neigh_list.list[i];
                ds_tree_insert(&vif->u.ap.neigh_tree, neigh, &neigh->neigh.bssid);
            }
            for (i = 0; i < info->drv_state->u.ap.neigh_ft_list.count; i++) {
                struct osw_conf_neigh_ft *neigh_ft = CALLOC(1, sizeof(*neigh_ft));
                neigh_ft->neigh_ft = info->drv_state->u.ap.neigh_ft_list.list[i];
                ds_tree_insert(&vif->u.ap.neigh_ft_tree, neigh_ft, &neigh_ft->neigh_ft.bssid);
            }
            for (i = 0; i < info->drv_state->u.ap.wps_cred_list.count; i++) {
                struct osw_conf_wps_cred *cred = CALLOC(1, sizeof(*cred));
                cred->cred = info->drv_state->u.ap.wps_cred_list.list[i];
                ds_dlist_insert_tail(&vif->u.ap.wps_cred_list, cred);
            }

            for (i = 0; i < info->drv_state->u.ap.radius_list.count; i++) {
                struct osw_conf_radius *radius = CALLOC(1, sizeof(*radius));
                radius->radius.server = STRDUP(info->drv_state->u.ap.radius_list.list[i].server);
                radius->radius.port = info->drv_state->u.ap.radius_list.list[i].port;
                radius->radius.passphrase = STRDUP(info->drv_state->u.ap.radius_list.list[i].passphrase);
                ds_dlist_insert_tail(&vif->u.ap.radius_list, radius);
            }

            for (i = 0; i < info->drv_state->u.ap.acct_list.count; i++) {
                struct osw_conf_radius *acct = CALLOC(1, sizeof(*acct));
                acct->radius.server = STRDUP(info->drv_state->u.ap.acct_list.list[i].server);
                acct->radius.port = info->drv_state->u.ap.acct_list.list[i].port;
                acct->radius.passphrase = STRDUP(info->drv_state->u.ap.acct_list.list[i].passphrase);
                ds_dlist_insert_tail(&vif->u.ap.accounting_list, acct);
            }

            osw_passpoint_copy(&info->drv_state->u.ap.passpoint, &vif->u.ap.passpoint);

            vif->u.ap.beacon_interval_tu = info->drv_state->u.ap.beacon_interval_tu;
            vif->u.ap.ssid_hidden = info->drv_state->u.ap.ssid_hidden;
            vif->u.ap.isolated = info->drv_state->u.ap.isolated;
            vif->u.ap.mcast2ucast = info->drv_state->u.ap.mcast2ucast;
            vif->u.ap.wps_pbc = info->drv_state->u.ap.wps_pbc;
            vif->u.ap.multi_ap = info->drv_state->u.ap.multi_ap;
            vif->u.ap.mbss_mode = info->drv_state->u.ap.mbss_mode;
            vif->u.ap.mbss_group = info->drv_state->u.ap.mbss_group;
            vif->u.ap.ft_over_ds = info->drv_state->u.ap.ft_over_ds;
            vif->u.ap.ft_pmk_r0_key_lifetime_sec = info->drv_state->u.ap.ft_pmk_r0_key_lifetime_sec;
            vif->u.ap.ft_pmk_r1_max_key_lifetime_sec = info->drv_state->u.ap.ft_pmk_r1_max_key_lifetime_sec;
            vif->u.ap.ft_pmk_r1_push = info->drv_state->u.ap.ft_pmk_r1_push;
            vif->u.ap.ft_psk_generate_local = info->drv_state->u.ap.ft_psk_generate_local;
            vif->u.ap.ft_mobility_domain = info->drv_state->u.ap.ft_mobility_domain;
            vif->u.ap.mbo = info->drv_state->u.ap.mbo;
            vif->u.ap.oce = info->drv_state->u.ap.oce;
            vif->u.ap.oce_min_rssi_enable = info->drv_state->u.ap.oce_min_rssi_enable;
            vif->u.ap.oce_min_rssi_dbm = info->drv_state->u.ap.oce_min_rssi_dbm;
            vif->u.ap.oce_retry_delay_sec = info->drv_state->u.ap.oce_retry_delay_sec;
            vif->u.ap.max_sta = info->drv_state->u.ap.max_sta;
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            ds_dlist_init(&vif->u.sta.net_list, struct osw_conf_net, node);
            for (snet = info->drv_state->u.sta.network; snet != NULL; snet = snet->next) {
                struct osw_conf_net *cnet = CALLOC(1, sizeof(*cnet));
                memcpy(&cnet->ssid, &snet->ssid, sizeof(snet->ssid));
                memcpy(&cnet->bssid, &snet->bssid, sizeof(snet->bssid));
                memcpy(&cnet->psk, &snet->psk, sizeof(snet->psk));
                memcpy(&cnet->wpa, &snet->wpa, sizeof(snet->wpa));
                memcpy(&cnet->bridge_if_name, &snet->bridge_if_name, sizeof(snet->bridge_if_name));
                cnet->multi_ap = snet->multi_ap;
                cnet->priority = snet->priority;
                ds_dlist_insert_tail(&vif->u.sta.net_list, cnet);
            }
            break;
    }
    ds_tree_insert(&phy->vif_tree, vif, vif->vif_name);
}

static void
osw_conf_build_phy_cb(const struct osw_state_phy_info *info,
                      void *ptr)
{
    struct ds_tree *phy_tree = ptr;
    struct osw_conf_phy *phy = CALLOC(1, sizeof(*phy));

    assert(info != NULL);
    assert(info->phy_name != NULL);
    assert(info->drv_state != NULL);

    phy->phy_tree = phy_tree;
    phy->phy_name = STRDUP(info->phy_name);
    phy->enabled = info->drv_state->enabled;
    phy->tx_chainmask = info->drv_state->tx_chainmask;
    phy->radar_next_channel = info->drv_state->radar_next_channel;
    phy->radar = info->drv_state->radar;
    phy->reg_domain = info->drv_state->reg_domain;
    ds_tree_init(&phy->vif_tree, ds_str_cmp, struct osw_conf_vif, phy_node);
    osw_state_vif_get_list(osw_conf_build_vif_cb, phy->phy_name, phy);
    ds_tree_insert(phy_tree, phy, phy->phy_name);
}

struct ds_tree *
osw_conf_build_from_state(void)
{
    struct ds_tree *phy_tree = CALLOC(1, sizeof(*phy_tree));
    ds_tree_init(phy_tree, ds_str_cmp, struct osw_conf_phy, conf_node);
    osw_state_phy_get_list(osw_conf_build_phy_cb, phy_tree);
    return phy_tree;
}

static void
osw_conf_order_mutators(struct osw_conf *m)
{
    const bool already_ordered = (m->ordered != NULL);
    if (already_ordered) return;

    struct osw_conf_mutator *mut;
    size_t n = 0;
    ds_dlist_foreach(&m->mutators, mut) n++;

    struct osw_conf_mutator **unordered = CALLOC(n, sizeof(*unordered));
    struct osw_conf_mutator **ordered = CALLOC(n, sizeof(*ordered));

    size_t i = 0;
    ds_dlist_foreach(&m->mutators, mut) {
        unordered[i] = mut;
        i++;
    }

    i = 0;
    if (m->ordering != NULL) {
        char *tokens = STRDUP(m->ordering);
        char *tokensp = tokens;
        char *token;

        while ((token = strsep(&tokensp, ", ")) != NULL) {
            size_t j;
            for (j = 0; j < n; j++) {
                if (unordered[j] == NULL) continue;
                const bool match = (strcmp(unordered[j]->name, token) == 0);
                const bool not_match = (match == false);
                if (not_match) continue;
                ordered[i] = unordered[j];
                unordered[j] = NULL;
                i++;
            }
        }

        FREE(tokens);
    }

    size_t j;
    for (j = 0; j < n; j++) {
        if (unordered[j] == NULL) continue;
        if (i >= n) break;
        ordered[i] = unordered[j];
        unordered[j] = NULL;
        i++;
    }

    ASSERT(i == j, "");
    ASSERT(i == n, "");

    FREE(unordered);
    m->ordered = ordered;
    m->n_ordered = n;

    LOGD(LOG_PREFIX_MUT("ordered: "));
    for (i = 0; i < n; i++) {
        LOGD(LOG_PREFIX_MUT("ordered: %s", ordered[i]->name ?: ""));
    }
}

static void
osw_conf_computed(struct ds_tree *phy_tree)
{
    struct osw_conf_observer *i;

    ds_dlist_foreach(&g_osw_conf.observers, i)
        if (i->conf_computed_fn != NULL) i->conf_computed_fn(i, phy_tree);
}

struct ds_tree *
osw_conf_build(void)
{
    struct ds_tree *phy_tree = osw_conf_build_from_state();

    osw_conf_order_mutators(&g_osw_conf);

    if (g_osw_conf.ordered != NULL) {
        size_t i;
        for (i = 0; i < g_osw_conf.n_ordered; i++) {
            struct osw_conf_mutator *mut = g_osw_conf.ordered[i];
            mut->mutate_fn(mut, phy_tree);
        }
    }

    osw_conf_computed(phy_tree);
    return phy_tree;
}

static void
osw_conf_free_vif_ap_acl(struct osw_conf_vif *vif,
                         struct osw_conf_acl *acl)
{
    ds_tree_remove(&vif->u.ap.acl_tree, acl);
    FREE(acl);
}

static void
osw_conf_free_vif_ap_psk(struct osw_conf_vif *vif,
                         struct osw_conf_psk *psk)
{
    ds_tree_remove(&vif->u.ap.psk_tree, psk);
    FREE(psk);
}

static void
osw_conf_free_vif_ap_neigh(struct osw_conf_vif *vif,
                           struct osw_conf_neigh *neigh)
{
    ds_tree_remove(&vif->u.ap.neigh_tree, neigh);
    FREE(neigh);
}

static void
osw_conf_free_vif_ap_neigh_ft(struct osw_conf_vif *vif,
                              struct osw_conf_neigh_ft *neigh_ft)
{
    ds_tree_remove(&vif->u.ap.neigh_ft_tree, neigh_ft);
    FREE(neigh_ft);
}

static void
osw_conf_free_vif_ap_radius(struct ds_dlist *dlist,
                            struct osw_conf_radius *rad)
{
    ds_dlist_remove(dlist, rad);
    FREE(rad->radius.server);
    FREE(rad->radius.passphrase);
    FREE(rad);
}

static void
osw_conf_free_vif_ap_wps(struct osw_conf_vif *vif,
                         struct osw_conf_wps_cred *wps)
{
    ds_dlist_remove(&vif->u.ap.wps_cred_list, wps);
    FREE(wps);
}

static void
osw_conf_free_vif(struct osw_conf_vif *vif)
{
    struct osw_conf_acl *acl;
    struct osw_conf_psk *psk;
    struct osw_conf_neigh *neigh;
    struct osw_conf_neigh_ft *neigh_ft;
    struct osw_conf_wps_cred *wps;
    struct osw_conf_net *net;
    struct osw_conf_radius *rad;

    switch (vif->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            while ((acl = ds_tree_head(&vif->u.ap.acl_tree)) != NULL)
                osw_conf_free_vif_ap_acl(vif, acl);

            while ((psk = ds_tree_head(&vif->u.ap.psk_tree)) != NULL)
                osw_conf_free_vif_ap_psk(vif, psk);

            while ((neigh = ds_tree_head(&vif->u.ap.neigh_tree)) != NULL)
                osw_conf_free_vif_ap_neigh(vif, neigh);

            while ((neigh_ft = ds_tree_head(&vif->u.ap.neigh_ft_tree)) != NULL)
                osw_conf_free_vif_ap_neigh_ft(vif, neigh_ft);

            while ((rad = ds_dlist_head(&vif->u.ap.radius_list)) != NULL)
                osw_conf_free_vif_ap_radius(&vif->u.ap.radius_list, rad);

            while ((rad = ds_dlist_head(&vif->u.ap.accounting_list)) != NULL)
                osw_conf_free_vif_ap_radius(&vif->u.ap.accounting_list, rad);

            while ((wps = ds_dlist_head(&vif->u.ap.wps_cred_list)) != NULL)
                osw_conf_free_vif_ap_wps(vif, wps);

            osw_passpoint_free_internal(&vif->u.ap.passpoint);
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            while ((net = ds_dlist_remove_head(&vif->u.sta.net_list)) != NULL)
                FREE(net);
            break;
    }

    ds_tree_remove(&vif->phy->vif_tree, vif);
    FREE(vif->vif_name);
    FREE(vif);
}

static void
osw_conf_free_phy(struct osw_conf_phy *phy)
{
    struct osw_conf_vif *vif;
    struct osw_conf_vif *tmp;

    ds_tree_foreach_safe(&phy->vif_tree, vif, tmp)
        osw_conf_free_vif(vif);

    ds_tree_remove(phy->phy_tree, phy);
    FREE(phy->phy_name);
    FREE(phy);
}

void
osw_conf_free(struct ds_tree *phy_tree)
{
    struct osw_conf_phy *phy;
    struct osw_conf_phy *tmp;

    if (phy_tree == NULL)
        return;

    ds_tree_foreach_safe(phy_tree, phy, tmp)
        osw_conf_free_phy(phy);

    FREE(phy_tree);
}

void
osw_conf_radius_free(struct osw_conf_radius *rad)
{
    FREE(rad->radius.server);
    FREE(rad->radius.passphrase);
    FREE(rad);
}

static const char *
osw_conf_mutator_type_to_cstr(const enum osw_conf_type type)
{
    switch (type) {
        case OSW_CONF_TAIL: return "tail";
        case OSW_CONF_HEAD: return "head";
    }
    /* unreachable */
    return "";
}

static void
osw_conf_invalidate_mutator_order(struct osw_conf *m)
{
    if (m->ordered != NULL) {
        LOGD(LOG_PREFIX("mutator order invalidated"));
    }

    FREE(m->ordered);
    m->ordered = NULL;
}

void
osw_conf_register_mutator(struct osw_conf_mutator *mutator)
{
    LOGD(LOG_PREFIX_MUT("registering: %s (%s)",
                        mutator->name ?: "",
                        osw_conf_mutator_type_to_cstr(mutator->type)));

    switch (mutator->type) {
    case OSW_CONF_HEAD:
        ds_dlist_insert_head(&g_osw_conf.mutators, mutator);
        break;
    case OSW_CONF_TAIL:
        ds_dlist_insert_tail(&g_osw_conf.mutators, mutator);
        break;
    }

    osw_conf_invalidate_mutator_order(&g_osw_conf);
}

void
osw_conf_unregister_mutator(struct osw_conf_mutator *mutator)
{
    ds_dlist_remove(&g_osw_conf.mutators, mutator);
    osw_conf_invalidate_mutator_order(&g_osw_conf);
}

void
osw_conf_set_mutator_ordering(const char *comma_separated)
{
    osw_conf_invalidate_mutator_order(&g_osw_conf);
    FREE(g_osw_conf.ordering);
    g_osw_conf.ordering = NULL;
    if (comma_separated != NULL) {
        g_osw_conf.ordering = STRDUP(comma_separated);
    }
}

void
osw_conf_register_observer(struct osw_conf_observer *observer)
{
    ds_dlist_insert_tail(&g_osw_conf.observers, observer);
}

void
osw_conf_invalidate(struct osw_conf_mutator *mutator)
{
    struct osw_conf_observer *i;

    /* TBD: The list of mutators is ordered. If a mutator
     * expresses that it changed its mind it should be
     * possible to re-use prior (unchanged) mutator outputs
     * for performance.
     *
     * For now nothing is done. Only observers are notified
     * so they can take action. Typically call the sequence
     * of:
     *   - osw_conf_build()
     *   - osw_req_config()
     *   - osw_conf_free()
    */
    ds_dlist_foreach(&g_osw_conf.observers, i)
        if (i->mutated_fn != NULL) i->mutated_fn(i);
}

bool
osw_conf_ap_psk_tree_changed(struct ds_tree *a, struct ds_tree *b)
{
    struct osw_conf_psk *i;
    size_t n = 0;
    size_t m = 0;

    ds_tree_foreach(a, i) {
        struct osw_conf_psk *j = ds_tree_find(b, &i->ap_psk.key_id);
        if (j == NULL) return true;

        const char *x = i->ap_psk.psk.str;
        const char *y = j->ap_psk.psk.str;
        size_t l = sizeof(i->ap_psk.psk.str);
        if (strncmp(x, y, l) != 0) return true;

        n++;
    }

    ds_tree_foreach(b, i)
        m++;

    if (n != m) return true;

    return false;
}

bool
osw_conf_ap_acl_tree_changed(struct ds_tree *a, struct ds_tree *b)
{
    struct osw_conf_acl *i;
    size_t n = 0;
    size_t m = 0;

    ds_tree_foreach(a, i) {
        struct osw_conf_acl *j = ds_tree_find(b, &i->mac_addr);
        if (j == NULL) return true;
        n++;
    }

    ds_tree_foreach(b, i)
        m++;

    if (n != m) return true;

    return false;
}

void
osw_conf_ap_psk_tree_to_str(char *out, size_t len, const struct ds_tree *a)
{
    /* FIXME: ds_tree APIs don't have const variants necessary */
    struct ds_tree *b = (struct ds_tree *)a;
    struct osw_conf_psk *i;
    const size_t max = ARRAY_SIZE(i->ap_psk.psk.str);

    out[0] = 0;
    ds_tree_foreach(b, i) {
        csnprintf(&out, &len, "%d:len=%d,",
                  i->ap_psk.key_id,
                  strnlen(i->ap_psk.psk.str, max));
    }
    if (ds_tree_is_empty(b) == false && out[-1] == ',')
        out[-1] = 0;
}

void
osw_conf_ap_acl_tree_to_str(char *out, size_t len, const struct ds_tree *a)
{
    /* FIXME: ds_tree APIs don't have const variants necessary */
    struct ds_tree *b = (struct ds_tree *)a;
    struct osw_conf_acl *i;

    out[0] = 0;
    ds_tree_foreach(b, i) {
        csnprintf(&out, &len, OSW_HWADDR_FMT ",",
                  OSW_HWADDR_ARG(&i->mac_addr));
    }
    if (ds_tree_is_empty(b) == false && out[-1] == ',')
        out[-1] = 0;
}

void
osw_conf_neigh_tree_to_str(char *out, size_t len, const struct ds_tree *a)
{
    /* FIXME: ds_tree APIs don't have const variants necessary */
    struct ds_tree *b = (struct ds_tree *)a;
    struct osw_conf_neigh *i;

    out[0] = 0;
    ds_tree_foreach(b, i) {
        csnprintf(&out, &len,
                  " "OSW_HWADDR_FMT"/%08x/%u/%u/%u,",
                  OSW_HWADDR_ARG(&i->neigh.bssid),
                  i->neigh.bssid_info,
                  i->neigh.op_class,
                  i->neigh.channel,
                  i->neigh.phy_type);
    }
    if (ds_tree_is_empty(b) == false && out[-1] == ',')
        out[-1] = 0;
}

static void
osw_conf_clone_vif_wps_cred_list(struct ds_dlist *src, struct ds_dlist *dst)
{
    struct osw_conf_wps_cred *src_cred;
    ds_dlist_init(dst, struct osw_conf_wps_cred, node);

    ds_dlist_foreach(src, src_cred) {
        struct osw_conf_wps_cred *cred = CALLOC(1, sizeof(*cred));
        cred->cred = src_cred->cred;
        ds_dlist_insert_tail(dst, cred);
    }
}

static void
osw_conf_clone_vif_radius_list(struct ds_dlist *src, struct ds_dlist *dst)
{
    struct osw_conf_radius *src_radius;
    ds_dlist_init(dst, struct osw_conf_radius, node);

    ds_dlist_foreach(src, src_radius) {
        struct osw_conf_radius *radius = CALLOC(1, sizeof(*radius));
        radius->radius.server = STRDUP(src_radius->radius.server);
        radius->radius.port = src_radius->radius.port;
        radius->radius.passphrase = STRDUP(src_radius->radius.passphrase);
        ds_dlist_insert_tail(dst, radius);
    }
}

static void
osw_conf_clone_vif_net_list(struct ds_dlist *src, struct ds_dlist *dst)
{
    struct osw_conf_net *src_cnet;
    ds_dlist_init(dst, struct osw_conf_net, node);

    ds_dlist_foreach(src, src_cnet) {
        struct osw_conf_net *cnet = CALLOC(1, sizeof(*cnet));
        memcpy(&cnet->ssid, &src_cnet->ssid, sizeof(cnet->ssid));
        memcpy(&cnet->bssid, &src_cnet->bssid, sizeof(cnet->bssid));
        memcpy(&cnet->psk, &src_cnet->psk, sizeof(cnet->psk));
        memcpy(&cnet->wpa, &src_cnet->wpa, sizeof(cnet->wpa));
        memcpy(&cnet->bridge_if_name, &src_cnet->bridge_if_name, sizeof(cnet->bridge_if_name));
        cnet->multi_ap = src_cnet->multi_ap;
        cnet->priority = src_cnet->priority;
        ds_dlist_insert_tail(dst, cnet);
    }
}

static void
osw_conf_clone_vif(struct osw_conf_vif *src, struct osw_conf_phy *phy)
{
    struct osw_conf_vif *vif = CALLOC(1, sizeof(*vif));
    struct osw_conf_acl *src_acl;
    struct osw_conf_psk *src_psk;
    struct osw_conf_neigh *src_neigh;

    vif->phy = phy;
    vif->mac_addr = src->mac_addr;
    vif->vif_name = STRDUP(src->vif_name);
    vif->enabled = src->enabled;
    vif->vif_type = src->vif_type;
    vif->tx_power_dbm = src->tx_power_dbm;

    switch (vif->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            ds_tree_init(&vif->u.ap.acl_tree, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_conf_acl, node);
            ds_tree_init(&vif->u.ap.psk_tree, ds_int_cmp, struct osw_conf_psk, node);
            ds_tree_init(&vif->u.ap.neigh_tree, (ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_conf_neigh, node);
            ds_dlist_init(&vif->u.ap.accounting_list, struct osw_conf_radius, node);
            vif->u.ap.acl_policy = src->u.ap.acl_policy;
            vif->u.ap.ssid = src->u.ap.ssid;
            vif->u.ap.channel = src->u.ap.channel;
            vif->u.ap.mode = src->u.ap.mode;
            STRSCPY(vif->u.ap.bridge_if_name.buf, src->u.ap.bridge_if_name.buf);
            STRSCPY(vif->u.ap.nas_identifier.buf, src->u.ap.nas_identifier.buf);
            vif->u.ap.wpa = src->u.ap.wpa;


            ds_tree_foreach(&src->u.ap.acl_tree, src_acl) {
                struct osw_conf_acl *acl = CALLOC(1, sizeof(*acl));
                acl->mac_addr = src_acl->mac_addr;
                ds_tree_insert(&vif->u.ap.acl_tree, acl, &acl->mac_addr);
            }

            ds_tree_foreach(&src->u.ap.psk_tree, src_psk) {
                struct osw_conf_psk *psk = CALLOC(1, sizeof(*psk));
                psk->ap_psk = src_psk->ap_psk;
                ds_tree_insert(&vif->u.ap.psk_tree, psk, &psk->ap_psk.key_id);
            }

            ds_tree_foreach(&src->u.ap.neigh_tree, src_neigh) {
                struct osw_conf_neigh *neigh = CALLOC(1, sizeof(*neigh));
                neigh->neigh = src_neigh->neigh;
                ds_tree_insert(&vif->u.ap.neigh_tree, neigh, &neigh->neigh.bssid);
            }
            osw_conf_clone_vif_wps_cred_list(&src->u.ap.wps_cred_list, &vif->u.ap.wps_cred_list);
            osw_conf_clone_vif_radius_list(&src->u.ap.radius_list, &vif->u.ap.radius_list);
            osw_conf_clone_vif_radius_list(&src->u.ap.accounting_list, &vif->u.ap.accounting_list);
            osw_passpoint_copy(&src->u.ap.passpoint, &vif->u.ap.passpoint);

            vif->u.ap.beacon_interval_tu = src->u.ap.beacon_interval_tu;
            vif->u.ap.ssid_hidden = src->u.ap.ssid_hidden;
            vif->u.ap.isolated = src->u.ap.isolated;
            vif->u.ap.mcast2ucast = src->u.ap.mcast2ucast;
            vif->u.ap.wps_pbc = src->u.ap.wps_pbc;
            vif->u.ap.multi_ap = src->u.ap.multi_ap;
            vif->u.ap.mbss_mode = src->u.ap.mbss_mode;
            vif->u.ap.mbss_group = src->u.ap.mbss_group;
            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            osw_conf_clone_vif_net_list(&src->u.sta.net_list, &vif->u.sta.net_list);
            break;
    }

    ds_tree_insert(&phy->vif_tree, vif, vif->vif_name);
}

static void
osw_conf_clone_phy(struct osw_conf_phy *src, struct ds_tree *phy_tree)
{
    struct osw_conf_vif *vif;

    struct osw_conf_phy *phy = CALLOC(1, sizeof(*phy));

    phy->phy_tree = phy_tree;
    phy->phy_name = STRDUP(src->phy_name);
    phy->enabled = src->enabled;
    phy->tx_chainmask = src->tx_chainmask;
    phy->radar_next_channel = src->radar_next_channel;
    phy->radar = src->radar;
    phy->reg_domain = src->reg_domain;
    ds_tree_init(&phy->vif_tree, ds_str_cmp, struct osw_conf_vif, phy_node);

    ds_tree_foreach(&src->vif_tree, vif)
        osw_conf_clone_vif(vif, phy);

    ds_tree_insert(phy_tree, phy, phy->phy_name);
}

struct ds_tree *osw_conf_clone(struct ds_tree *src)
{
    struct osw_conf_phy *phy;

    if (src == NULL)
        return NULL;

    struct ds_tree *phy_tree = CALLOC(1, sizeof(*phy_tree));
    ds_tree_init(phy_tree, ds_str_cmp, struct osw_conf_phy, conf_node);

    ds_tree_foreach(src, phy)
        osw_conf_clone_phy(phy, phy_tree);

    return phy_tree;
}

#define osw_check_null_exit(a, b) \
    if ((a) == NULL && (b) == NULL) return 0; \
    if ((a) == NULL && (b) != NULL) return -1; \
    if ((a) != NULL && (b) == NULL) return 1;

#define osw_check_null(r, a, b) \
    r = (a) == NULL ? -1 \
      : (b) == NULL ? 1 \
      : 0; \
    if (r != 0) return r;

/* This helper can be used to compare 2 trees
 * built in the same way and in the same order
 * it will return non equal 2 trees that contains
 * equal elements but in different order.
 */
#define osw_ds_tree_pair_each(ta, tb, pa, pb)       \
    osw_check_null(r, ta, tb) \
    if (ds_tree_is_empty(ta) == true) { \
        if(ds_tree_is_empty(tb) == false) \
            return -1;\
        /* both empty => nop */ \
    } else { \
        if(ds_tree_is_empty(tb) == true)\
            return 1;\
    } \
    for (pa = ds_tree_head(ta), pb = ds_tree_head(tb) ; pa != NULL && pb != NULL; pa = ds_tree_next(ta, pa), pb = ds_tree_next(tb, pb))

#define osw_ds_tree_pair_post(r, pa, pb)     \
    r = (pa) != NULL ? 1 \
      : (pb) != NULL ? -1 \
      : 0; \
    if (r != 0) return r;

#define osw_int_compare(r, a, b) \
     r = (a) < (b) ? -1 \
       : (a) > (b) ? 1 \
       : 0; \
       if (r != 0) return r;

#define osw_mem_compare(r, a, b) \
     osw_check_null(r, a, b) \
     r = memcmp(a, b, sizeof(*(a))); \
     if (r != 0) return r;

#define osw_str_compare(r, a, b) \
     osw_check_null(r, a, b) \
     r = STRSCMP(a, b); \
     if (r != 0) return r;

static int osw_conf_cmp_vif_wps_cred_list(struct ds_dlist *a, struct ds_dlist *b)
{
    struct ds_dlist wps_cred_list_tmp;
    struct osw_conf_wps_cred *src_cred;
    /* compare wps_cred_list, copy second side to remove checked elements
     * during iterations, this will catch duplicate items on one of sides.
     */
    if (ds_dlist_is_empty(a)) {
       if (ds_dlist_is_empty(b)) return 0;
       return -1;
    } else {
       if (ds_dlist_is_empty(b)) return 1;
    }

    osw_conf_clone_vif_wps_cred_list(b, &wps_cred_list_tmp);

    ds_dlist_foreach(a, src_cred) {
        struct osw_conf_wps_cred *cred = NULL;
        int tmp_r = 0;
        ds_dlist_iter_t  iter;
        cred = ds_dlist_ifirst(&iter, &wps_cred_list_tmp);
        if (cred == NULL) return -1; /* we run out of elements in b (b is empty), a has more elements => -1 */

        for(;cred != NULL;cred = ds_dlist_inext(&iter))
        {
            tmp_r = strncmp(src_cred->cred.psk.str, cred->cred.psk.str, sizeof(*src_cred->cred.psk.str));
            if(tmp_r == 0) {
                ds_dlist_iremove(&iter); /* item found marking jumping to next element from a */
                break;
            }
        }
        if (tmp_r != 0) {
            /* element from a not found in b, cleaning */
            while (!ds_dlist_is_empty(&wps_cred_list_tmp)) {
                struct osw_conf_net *cred;
                cred = ds_dlist_remove_tail(&wps_cred_list_tmp);
                FREE(cred);
            }
            return -1;
        }
        /* next element from a */
    }
    if (!ds_dlist_is_empty(&wps_cred_list_tmp)) {
        while (!ds_dlist_is_empty(&wps_cred_list_tmp)) {
            struct osw_conf_wps_cred *cred;
            cred = ds_dlist_remove_tail(&wps_cred_list_tmp);
            FREE(cred);
        }
        return 1; /* some leftovers in b => 1 */
    }

    return 0;
}

static int osw_conf_cmp_vif_radius(struct osw_conf_radius *a, struct osw_conf_radius *b)
{
    int r;
    osw_str_compare(r, a->radius.server, b->radius.server);
    osw_str_compare(r, a->radius.passphrase, b->radius.passphrase);
    osw_int_compare(r, a->radius.port, b->radius.port);

    return 0;
}

static void osw_conf_radius_list_free(struct ds_dlist *list)
{
    struct osw_conf_radius *radius;
    while ((radius = ds_dlist_remove_tail(list)) != NULL) {
        FREE(radius->radius.server);
        FREE(radius->radius.passphrase);
        FREE(radius);
    }
}

static int osw_conf_cmp_vif_radius_list(struct ds_dlist *a, struct ds_dlist *b)
{
    struct ds_dlist radius_list_tmp;
    struct osw_conf_radius *src_radius;
    /* compare radius_list, copy second side to remove checked elements
     * during iterations, this will catch duplicate items on one of sides.
     */
    if (ds_dlist_is_empty(a)) {
       if (ds_dlist_is_empty(b)) return 0;
       return -1;
    } else {
       if (ds_dlist_is_empty(b)) return 1;
    }

    osw_conf_clone_vif_radius_list(b, &radius_list_tmp);

    ds_dlist_foreach(a, src_radius) {
        struct osw_conf_radius *radius = NULL;
        int tmp_r = 0;
        ds_dlist_iter_t  iter;
        radius = ds_dlist_ifirst(&iter, &radius_list_tmp);
        if (radius == NULL) return -1; /* we run out of elements in b (b is empty), a has more elements => -1 */

        for(;radius != NULL;radius = ds_dlist_inext(&iter))
        {
            tmp_r = osw_conf_cmp_vif_radius(src_radius, radius);
            if(tmp_r == 0) {
                ds_dlist_iremove(&iter); /* item found marking jumping to next element from a */
                break;
            }
        }
        if (tmp_r != 0) {
            /* element from a not found in b, cleaning */
            osw_conf_radius_list_free(&radius_list_tmp);
            return -1;
        }
        /* next element from a */
    }
    if (!ds_dlist_is_empty(&radius_list_tmp)) {
        osw_conf_radius_list_free(&radius_list_tmp);
        return 1; /* some leftovers in b => 1 */
    }

    return 0;
}

static int osw_wpa_compare(struct osw_wpa *a, struct osw_wpa *b)
{
    int r;
    osw_int_compare(r, a->wpa, b->wpa);
    osw_int_compare(r, a->rsn, b->rsn);
    osw_int_compare(r, a->akm_eap, b->akm_eap);
    osw_int_compare(r, a->akm_eap_sha256, b->akm_eap_sha256);
    osw_int_compare(r, a->akm_eap_sha384, b->akm_eap_sha384);
    osw_int_compare(r, a->akm_eap_suite_b, b->akm_eap_suite_b);
    osw_int_compare(r, a->akm_eap_suite_b192, b->akm_eap_suite_b192);
    osw_int_compare(r, a->akm_psk, b->akm_psk);
    osw_int_compare(r, a->akm_psk_sha256, b->akm_psk_sha256);
    osw_int_compare(r, a->akm_sae, b->akm_sae);
    osw_int_compare(r, a->akm_sae_ext, b->akm_sae_ext);
    osw_int_compare(r, a->akm_ft_eap, b->akm_ft_eap);
    osw_int_compare(r, a->akm_ft_eap_sha384, b->akm_ft_eap_sha384);
    osw_int_compare(r, a->akm_ft_psk, b->akm_ft_psk);
    osw_int_compare(r, a->akm_ft_sae, b->akm_ft_sae);
    osw_int_compare(r, a->akm_ft_sae_ext, b->akm_ft_sae_ext);
    osw_int_compare(r, a->pairwise_tkip, b->pairwise_tkip);
    osw_int_compare(r, a->pairwise_ccmp, b->pairwise_ccmp);
    osw_int_compare(r, a->pairwise_ccmp256, b->pairwise_ccmp256);
    osw_int_compare(r, a->pairwise_gcmp, b->pairwise_gcmp);
    osw_int_compare(r, a->pairwise_gcmp256, b->pairwise_gcmp256);
    osw_int_compare(r, a->pmf, b->pmf);
    osw_int_compare(r, a->beacon_protection, b->beacon_protection);
    osw_int_compare(r, a->group_rekey_seconds, b->group_rekey_seconds);

    return 0;
}

static int osw_conf_cmp_vif_net(struct osw_conf_net *a, struct osw_conf_net *b)
{
    int r;
    r = osw_ssid_cmp(&a->ssid, &b->ssid);
    if (r != 0) return r;

    r = osw_hwaddr_cmp(&a->bssid, &b->bssid);
    if (r != 0) return r;

    osw_str_compare(r, a->psk.str, b->psk.str);
    r = osw_wpa_compare(&a->wpa, &b->wpa);
    if (r != 0) return r;

    osw_str_compare(r, a->bridge_if_name.buf, b->bridge_if_name.buf);
    osw_int_compare(r, a->multi_ap, b->multi_ap);
    osw_int_compare(r, a->priority, b->priority);
    return 0;
}

static int osw_conf_cmp_vif_net_list(struct ds_dlist *a, struct ds_dlist *b)
{
    struct osw_conf_net *src_net;
    struct ds_dlist net_list_tmp;
    /* compare net_list, copy second side to remove checked elements
     * during iterations, this will catch duplicate items on one of sides.
     */
    if (ds_dlist_is_empty(a)) {
       if (ds_dlist_is_empty(b)) return 0;
       return -1;
    } else {
       if (ds_dlist_is_empty(b)) return 1;
    }

    osw_conf_clone_vif_net_list(b, &net_list_tmp);

    ds_dlist_foreach(a, src_net) {
        struct osw_conf_net *net = NULL;
        int tmp_r = 0;
        ds_dlist_iter_t  iter;
        net = ds_dlist_ifirst(&iter, &net_list_tmp);
        if (net == NULL) return -1; /* we run out of elements in b (b is empty), a has more elements => -1 */

        for(;net != NULL;net = ds_dlist_inext(&iter))
        {
            tmp_r = osw_conf_cmp_vif_net(src_net, net);
            if(tmp_r == 0) {
                ds_dlist_iremove(&iter); /* item found marking jumping to next element from a */
                break;
            }
        }

        if (tmp_r != 0) {
            /* element from a not found in b, cleaning */
            while (!ds_dlist_is_empty(&net_list_tmp)) {
                struct osw_conf_net *net;
                net = ds_dlist_remove_tail(&net_list_tmp);
                FREE(net);
            }
            return -1;
        }
        /* next element from a */
    }
    if (!ds_dlist_is_empty(&net_list_tmp)) {
        while (!ds_dlist_is_empty(&net_list_tmp)) {
            struct osw_conf_net *net;
            net = ds_dlist_remove_tail(&net_list_tmp);
            FREE(net);
        }
        return 1; /* some leftovers in b => 1 */
    }

    return 0;
}
static int osw_channel_compare(struct osw_channel *a, struct osw_channel *b)
{
    int r;
    osw_int_compare(r, a->width, b->width);
    osw_int_compare(r, a->control_freq_mhz, b->control_freq_mhz);
    osw_int_compare(r, a->center_freq0_mhz, b->center_freq0_mhz);
    osw_int_compare(r, a->center_freq1_mhz, b->center_freq1_mhz);
    osw_mem_compare(r, &a->puncture_bitmap, &b->puncture_bitmap);

    return 0;
}

static int osw_beacon_rate_compare(struct osw_beacon_rate *a, struct osw_beacon_rate *b)
{
    int r;
    osw_int_compare(r, a->type, b->type);
    switch (a->type) {
        case OSW_BEACON_RATE_UNSPEC:
           break;
        case OSW_BEACON_RATE_ABG:
           osw_int_compare(r, a->u.legacy, b->u.legacy);
           break;
        case OSW_BEACON_RATE_HT:
           osw_int_compare(r, a->u.ht_mcs, b->u.ht_mcs);
           break;
        case OSW_BEACON_RATE_VHT:
           osw_int_compare(r, a->u.vht_mcs, b->u.vht_mcs);
           break;
        case OSW_BEACON_RATE_HE:
           osw_int_compare(r, a->u.he_mcs, b->u.he_mcs);
           break;
    }
    return 0;
}

static int osw_ap_mode_compare(struct osw_ap_mode *a, struct osw_ap_mode *b)
{
    int r;
    osw_int_compare(r, a->supported_rates, b->supported_rates);
    osw_int_compare(r, a->basic_rates, b->basic_rates);
    r = osw_beacon_rate_compare(&a->beacon_rate, &b->beacon_rate);
    if (r != 0) return r;
    osw_int_compare(r, a->mcast_rate, b->mcast_rate);
    osw_int_compare(r, a->mgmt_rate, b->mgmt_rate);
    osw_int_compare(r, a->wnm_bss_trans, b->wnm_bss_trans);
    osw_int_compare(r, a->rrm_neighbor_report, b->rrm_neighbor_report);
    osw_int_compare(r, a->wmm_enabled, b->wmm_enabled);
    osw_int_compare(r, a->wmm_uapsd_enabled, b->wmm_uapsd_enabled);
    osw_int_compare(r, a->ht_enabled, b->ht_enabled);
    osw_int_compare(r, a->ht_required, b->ht_required);
    osw_int_compare(r, a->vht_enabled, b->vht_enabled);
    osw_int_compare(r, a->vht_required, b->vht_required);
    osw_int_compare(r, a->he_enabled, b->he_enabled);
    osw_int_compare(r, a->he_required, b->he_required);
    osw_int_compare(r, a->eht_enabled, b->eht_enabled);
    osw_int_compare(r, a->eht_required, b->eht_required);
    osw_int_compare(r, a->wps, b->wps);

    return 0;
}

static int osw_neigh_compare(struct osw_neigh *a, struct osw_neigh *b)
{
    int r;
    r = osw_hwaddr_cmp(&a->bssid, &b->bssid);
    if (r != 0) return r;
    osw_int_compare(r, a->bssid_info, b->bssid_info);
    osw_int_compare(r, a->op_class, b->op_class);
    osw_int_compare(r, a->channel, b->channel);
    osw_int_compare(r, a->phy_type, b->phy_type);

    return 0;
}

static int osw_conf_cmp_vif(struct osw_conf_vif *a, struct osw_conf_vif *b)
{
    struct osw_conf_acl *a_acl, *b_acl;
    struct osw_conf_psk *a_psk, *b_psk;
    struct osw_conf_neigh *a_neigh, *b_neigh;
    struct osw_conf_neigh_ft *a_neigh_ft, *b_neigh_ft;
    int r;

    osw_check_null_exit(a, b);

    osw_int_compare(r, a->enabled, b->enabled);

    osw_str_compare(r, a->vif_name, b->vif_name);
    osw_int_compare(r, a->vif_type, b->vif_type);
    osw_int_compare(r, a->tx_power_dbm, b->tx_power_dbm);
    osw_mem_compare(r, &a->mac_addr, &b->mac_addr);

     switch (a->vif_type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            osw_int_compare(r, a->u.ap.acl_policy, b->u.ap.acl_policy);
            r = osw_ssid_cmp(&a->u.ap.ssid, &b->u.ap.ssid);
            if (r != 0) return r;

            r = osw_channel_compare(&a->u.ap.channel, &b->u.ap.channel);
            if (r != 0) return r;

            r = osw_ap_mode_compare(&a->u.ap.mode, &b->u.ap.mode);
            if (r != 0) return r;

            r = osw_wpa_compare(&a->u.ap.wpa, &b->u.ap.wpa);
            if (r != 0) return r;

            osw_str_compare(r, a->u.ap.bridge_if_name.buf, b->u.ap.bridge_if_name.buf);
            osw_str_compare(r, a->u.ap.nas_identifier.buf, b->u.ap.nas_identifier.buf);

            osw_ds_tree_pair_each(&a->u.ap.acl_tree, &b->u.ap.acl_tree, a_acl, b_acl) {
                osw_mem_compare(r, &a_acl->mac_addr, &b_acl->mac_addr);
            }
            osw_ds_tree_pair_post(r, a_acl, b_acl);

            osw_ds_tree_pair_each(&a->u.ap.psk_tree, &b->u.ap.psk_tree, a_psk, b_psk) {
                osw_int_compare(r, a_psk->ap_psk.key_id, b_psk->ap_psk.key_id);
                osw_str_compare(r, a_psk->ap_psk.psk.str, b_psk->ap_psk.psk.str);
            }
            osw_ds_tree_pair_post(r, a_psk, b_psk);

            osw_ds_tree_pair_each(&a->u.ap.neigh_tree, &b->u.ap.neigh_tree, a_neigh, b_neigh) {
                r = osw_neigh_compare(&a_neigh->neigh, &b_neigh->neigh);
                if (r != 0) return r;
            }
            osw_ds_tree_pair_post(r, a_neigh, b_neigh);

            osw_ds_tree_pair_each(&a->u.ap.neigh_ft_tree, &b->u.ap.neigh_ft_tree, a_neigh_ft, b_neigh_ft) {
                r = osw_neigh_ft_cmp(&a_neigh_ft->neigh_ft, &b_neigh_ft->neigh_ft);
                if (r != 0) return r;
            }
            osw_ds_tree_pair_post(r, a_neigh_ft, b_neigh_ft);

            osw_conf_cmp_vif_wps_cred_list(&a->u.ap.wps_cred_list, &b->u.ap.wps_cred_list);
            osw_conf_cmp_vif_radius_list(&a->u.ap.radius_list, &b->u.ap.radius_list);
            osw_conf_cmp_vif_radius_list(&a->u.ap.accounting_list, &b->u.ap.accounting_list);

            osw_int_compare(r, a->u.ap.beacon_interval_tu, b->u.ap.beacon_interval_tu);
            osw_int_compare(r, a->u.ap.ssid_hidden, b->u.ap.ssid_hidden);
            osw_int_compare(r, a->u.ap.isolated, b->u.ap.isolated);
            osw_int_compare(r, a->u.ap.mcast2ucast, b->u.ap.mcast2ucast);
            osw_int_compare(r, a->u.ap.wps_pbc, b->u.ap.wps_pbc);
            osw_int_compare(r, a->u.ap.multi_ap.fronthaul_bss, b->u.ap.multi_ap.fronthaul_bss);
            osw_int_compare(r, a->u.ap.multi_ap.backhaul_bss, b->u.ap.multi_ap.backhaul_bss);
            osw_int_compare(r, a->u.ap.mbss_mode, b->u.ap.mbss_mode);
            osw_int_compare(r, a->u.ap.mbss_group, b->u.ap.mbss_group);
            osw_int_compare(r, a->u.ap.ft_over_ds, b->u.ap.ft_over_ds);
            osw_int_compare(r, a->u.ap.ft_pmk_r0_key_lifetime_sec, b->u.ap.ft_pmk_r0_key_lifetime_sec);
            osw_int_compare(r, a->u.ap.ft_pmk_r1_max_key_lifetime_sec, b->u.ap.ft_pmk_r1_max_key_lifetime_sec);
            osw_int_compare(r, a->u.ap.ft_pmk_r1_push, b->u.ap.ft_pmk_r1_push);
            osw_int_compare(r, a->u.ap.ft_psk_generate_local, b->u.ap.ft_psk_generate_local);
            osw_int_compare(r, a->u.ap.ft_mobility_domain, b->u.ap.ft_mobility_domain);
            osw_int_compare(r, a->u.ap.mbo, b->u.ap.mbo);
            osw_int_compare(r, a->u.ap.oce, b->u.ap.oce);
            osw_int_compare(r, a->u.ap.oce_min_rssi_enable, b->u.ap.oce_min_rssi_enable);
            osw_int_compare(r, a->u.ap.oce_min_rssi_dbm, b->u.ap.oce_min_rssi_dbm);
            osw_int_compare(r, a->u.ap.oce_retry_delay_sec, b->u.ap.oce_retry_delay_sec);
            osw_int_compare(r, a->u.ap.max_sta, b->u.ap.max_sta);

            r = osw_ft_encr_key_cmp(&a->u.ap.ft_encr_key, &b->u.ap.ft_encr_key);
            if (r != 0) return r;

            /* FIXME: Currently there is only is equal funciton, there is no cmp function */
            r = osw_passpoint_is_equal(&a->u.ap.passpoint, &b->u.ap.passpoint) ? 0 : 1;
            if (r != 0)
                return r;

            break;
        case OSW_VIF_AP_VLAN:
            break;
        case OSW_VIF_STA:
            r = osw_conf_cmp_vif_net_list(&a->u.sta.net_list, &b->u.sta.net_list);
            if (r != 0) return r;
    }

    return 0;
}

static int osw_reg_domain_compare(struct osw_reg_domain *a, struct osw_reg_domain *b)
{
    int r;
    osw_str_compare(r, a->ccode, b->ccode);
    osw_int_compare(r, a->iso3166_num, b->iso3166_num);
    osw_int_compare(r, a->revision, b->revision);
    osw_int_compare(r, a->dfs, b->dfs);

    return 0;
}

static int osw_conf_cmp_phy(struct osw_conf_phy *a, struct osw_conf_phy *b)
{
    struct osw_conf_vif *va, *vb;
    int r;

    osw_check_null_exit(a, b);

    osw_str_compare(r, a->phy_name, b->phy_name);
    osw_int_compare(r, a->enabled, b->enabled);
    osw_int_compare(r, a->tx_chainmask, b->tx_chainmask);
    r = osw_channel_compare(&a->radar_next_channel, &b->radar_next_channel);
    if (r != 0) return r;
    osw_int_compare(r, a->radar, b->radar);
    r = osw_reg_domain_compare(&a->reg_domain, &b->reg_domain);
    if (r != 0) return r;

    osw_ds_tree_pair_each(&a->vif_tree, &b->vif_tree, va, vb) {
        r = osw_conf_cmp_vif(va,vb);
        if (r != 0) {
            LOGT("osw: conf: vif %s differs", va->vif_name);
            return r;
        }
    }
    osw_ds_tree_pair_post(r, va, vb);

    return 0;
}

/* FIXME: Due to missing compare function for passpoint
 * this compare function cannot fully compare.
 * It is still usuable for is_equal purpose
 */
static int osw_conf_cmp(struct ds_tree *a, struct ds_tree *b)
{
    struct osw_conf_phy *pa, *pb;
    int r = 0;

    osw_check_null_exit(a, b);
    osw_ds_tree_pair_each(a, b, pa, pb) {
        r = osw_conf_cmp_phy(pa,pb);
        if (r != 0) {
            LOGT("osw: conf: phy %s differs", pa->phy_name);
            return r;
        }
    }
    osw_ds_tree_pair_post(r, pa, pb);

    return 0;
}

bool osw_conf_is_equal(struct ds_tree *a, struct ds_tree *b)
{
    int r = osw_conf_cmp(a,b);
    if (r != 0) LOGT("osw: conf: osw_conf compare = %d", r);
    return r == 0;
}

void
osw_conf_ap_wps_cred_list_to_str(char *out, size_t len, const struct ds_dlist *a)
{
    /* FIXME: ds_tree APIs don't have const variants necessary */
    struct ds_dlist *b = (struct ds_dlist *)a;
    struct osw_conf_wps_cred *i;

    out[0] = 0;
    ds_dlist_foreach(b, i) {
        csnprintf(&out, &len,
                  "len=%u,",
                  strlen(i->cred.psk.str));
    }
    if (ds_dlist_is_empty(b) == false && out[-1] == ',')
        out[-1] = 0;
}

static void
osw_conf_init(struct osw_conf *conf)
{
    ds_dlist_init(&conf->mutators, struct osw_conf_mutator, node);
    ds_dlist_init(&conf->observers, struct osw_conf_observer, node);
}

OSW_UT(osw_conf_ut_psk_changed)
{
    struct ds_tree a = DS_TREE_INIT(ds_int_cmp, struct osw_conf_psk, node);
    struct ds_tree b = DS_TREE_INIT(ds_int_cmp, struct osw_conf_psk, node);
    struct osw_conf_psk psk1 = { .ap_psk = { .psk = { .str = "12345678" }, .key_id = 1 } };
    struct osw_conf_psk psk2 = { .ap_psk = { .psk = { .str = "87654321" }, .key_id = 1 } };
    struct osw_conf_psk psk3 = { .ap_psk = { .psk = { .str = "12345678" }, .key_id = 2 } };
    struct osw_conf_psk psk4 = psk1;

    assert(osw_conf_ap_psk_tree_changed(&a, &b) == false);

    ds_tree_insert(&a, &psk1, &psk1.ap_psk.key_id);
    ds_tree_insert(&b, &psk4, &psk4.ap_psk.key_id);
    assert(osw_conf_ap_psk_tree_changed(&a, &b) == false);
    ds_tree_remove(&a, &psk1);
    ds_tree_remove(&b, &psk4);

    ds_tree_insert(&a, &psk1, &psk1.ap_psk.key_id);
    ds_tree_insert(&b, &psk2, &psk2.ap_psk.key_id);
    assert(osw_conf_ap_psk_tree_changed(&a, &b) == true);
    ds_tree_remove(&a, &psk1);
    ds_tree_remove(&b, &psk2);

    ds_tree_insert(&a, &psk1, &psk1.ap_psk.key_id);
    ds_tree_insert(&b, &psk3, &psk3.ap_psk.key_id);
    assert(osw_conf_ap_psk_tree_changed(&a, &b) == true);
    ds_tree_remove(&a, &psk1);
    ds_tree_remove(&b, &psk3);

    ds_tree_insert(&a, &psk1, &psk1.ap_psk.key_id);
    ds_tree_insert(&a, &psk2, &psk2.ap_psk.key_id);
    ds_tree_insert(&b, &psk3, &psk3.ap_psk.key_id);
    assert(osw_conf_ap_psk_tree_changed(&a, &b) == true);
    ds_tree_remove(&a, &psk1);
    ds_tree_remove(&a, &psk2);
    ds_tree_remove(&b, &psk3);

    ds_tree_insert(&a, &psk1, &psk1.ap_psk.key_id);
    ds_tree_insert(&b, &psk2, &psk2.ap_psk.key_id);
    ds_tree_insert(&b, &psk3, &psk3.ap_psk.key_id);
    assert(osw_conf_ap_psk_tree_changed(&a, &b) == true);
    ds_tree_remove(&a, &psk1);
    ds_tree_remove(&b, &psk2);
    ds_tree_remove(&b, &psk3);
}

OSW_UT(osw_conf_ut_acl_changed)
{
    struct ds_tree a = DS_TREE_INIT((ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_conf_acl, node);
    struct ds_tree b = DS_TREE_INIT((ds_key_cmp_t *)osw_hwaddr_cmp, struct osw_conf_acl, node);
    struct osw_conf_acl acl1 = { .mac_addr = { .octet = { 0, 1, 2, 3, 4, 5 } } };
    struct osw_conf_acl acl2 = { .mac_addr = { .octet = { 0, 1, 2, 3, 4, 6 } } };
    struct osw_conf_acl acl3 = { .mac_addr = { .octet = { 0, 1, 2, 3, 4, 7 } } };
    struct osw_conf_acl acl4 = acl1;

    assert(osw_conf_ap_acl_tree_changed(&a, &b) == false);

    ds_tree_insert(&a, &acl1, &acl1.mac_addr);
    ds_tree_insert(&b, &acl4, &acl4.mac_addr);
    assert(osw_conf_ap_acl_tree_changed(&a, &b) == false);
    ds_tree_remove(&a, &acl1);
    ds_tree_remove(&b, &acl4);

    ds_tree_insert(&a, &acl1, &acl1.mac_addr);
    ds_tree_insert(&b, &acl2, &acl2.mac_addr);
    assert(osw_conf_ap_acl_tree_changed(&a, &b) == true);
    ds_tree_remove(&a, &acl1);
    ds_tree_remove(&b, &acl2);

    ds_tree_insert(&a, &acl1, &acl1.mac_addr);
    ds_tree_insert(&a, &acl2, &acl2.mac_addr);
    ds_tree_insert(&b, &acl3, &acl3.mac_addr);
    assert(osw_conf_ap_acl_tree_changed(&a, &b) == true);
    ds_tree_remove(&a, &acl1);
    ds_tree_remove(&a, &acl2);
    ds_tree_remove(&b, &acl3);
}

OSW_UT(osw_conf_ut_mutator_ordering_1)
{
    struct osw_conf_mutator m1 = { .name = "m1" };
    struct osw_conf_mutator m2 = { .name = "m2" };
    struct osw_conf_mutator m3 = { .name = "m3" };
    struct osw_conf_mutator m4 = { .name = "m4" };
    struct osw_conf *m = &g_osw_conf;

    osw_conf_init(m);

    osw_conf_register_mutator(&m1);
    osw_conf_register_mutator(&m2);
    osw_conf_register_mutator(&m3);
    osw_conf_register_mutator(&m4);

    osw_conf_order_mutators(m);
    ASSERT(m->n_ordered == 4, "");
    ASSERT(m->ordered[0] == &m1, "");
    ASSERT(m->ordered[1] == &m2, "");
    ASSERT(m->ordered[2] == &m3, "");
    ASSERT(m->ordered[3] == &m4, "");

    osw_conf_set_mutator_ordering("m3,m2,m4,m1");

    osw_conf_order_mutators(m);
    ASSERT(m->n_ordered == 4, "");
    ASSERT(m->ordered[0] == &m3, "");
    ASSERT(m->ordered[1] == &m2, "");
    ASSERT(m->ordered[2] == &m4, "");
    ASSERT(m->ordered[3] == &m1, "");

    osw_conf_set_mutator_ordering("m4,m2");

    osw_conf_order_mutators(m);
    ASSERT(m->n_ordered == 4, "");
    ASSERT(m->ordered[0] == &m4, "");
    ASSERT(m->ordered[1] == &m2, "");
    ASSERT(m->ordered[2] == &m1, ""); /* implied / leftovers */
    ASSERT(m->ordered[3] == &m3, ""); /* implied / leftovers */

    osw_conf_unregister_mutator(&m2);

    osw_conf_order_mutators(m);
    ASSERT(m->n_ordered == 3, "");
    ASSERT(m->ordered[0] == &m4, "");
    ASSERT(m->ordered[1] == &m1, "");
    ASSERT(m->ordered[2] == &m3, "");

    osw_conf_unregister_mutator(&m1);
    osw_conf_unregister_mutator(&m3);
    osw_conf_unregister_mutator(&m4);

    osw_conf_order_mutators(m);
    ASSERT(m->n_ordered == 0, "");

    osw_conf_register_mutator(&m1);
    osw_conf_order_mutators(m);
    ASSERT(m->n_ordered == 1, "");
    ASSERT(m->ordered[0] == &m1, "");

    osw_conf_register_mutator(&m2);
    osw_conf_order_mutators(m);
    ASSERT(m->n_ordered == 2, "");
    ASSERT(m->ordered[0] == &m2, "");
    ASSERT(m->ordered[1] == &m1, "");

    osw_conf_register_mutator(&m3);
    osw_conf_order_mutators(m);
    ASSERT(m->n_ordered == 3, "");
    ASSERT(m->ordered[0] == &m2, "");
    ASSERT(m->ordered[1] == &m1, "");
    ASSERT(m->ordered[2] == &m3, "");

    osw_conf_register_mutator(&m4);
    osw_conf_order_mutators(m);
    ASSERT(m->n_ordered == 4, "");
    ASSERT(m->ordered[0] == &m4, "");
    ASSERT(m->ordered[1] == &m2, "");
    ASSERT(m->ordered[2] == &m1, "");
    ASSERT(m->ordered[3] == &m3, "");
}

OSW_MODULE(osw_conf)
{
    OSW_MODULE_LOAD(osw_state);
    const char *ordering = osw_etc_get("OSW_CONF_MUTATOR_ORDERING");
    if (ordering != NULL) {
        osw_conf_set_mutator_ordering(ordering);
    }
    osw_conf_init(&g_osw_conf);
    return NULL;
}
