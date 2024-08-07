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
            ds_dlist_init(&vif->u.ap.radius_list, struct osw_conf_radius, node);
            ds_dlist_init(&vif->u.ap.accounting_list, struct osw_conf_radius, node);
            vif->u.ap.acl_policy = info->drv_state->u.ap.acl_policy;
            vif->u.ap.ssid = info->drv_state->u.ap.ssid;
            vif->u.ap.channel = info->drv_state->u.ap.channel;
            vif->u.ap.mode = info->drv_state->u.ap.mode;
            STRSCPY_WARN(vif->u.ap.bridge_if_name.buf, info->drv_state->u.ap.bridge_if_name.buf ?: "");
            STRSCPY_WARN(vif->u.ap.nas_identifier.buf, info->drv_state->u.ap.nas_identifier.buf ?: "");
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
        i->mutated_fn(i);
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
    const char *ordering = getenv("OSW_CONF_MUTATOR_ORDERING");
    if (ordering != NULL) {
        osw_conf_set_mutator_ordering(ordering);
    }
    osw_conf_init(&g_osw_conf);
    return NULL;
}
