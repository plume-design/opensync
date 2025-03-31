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

#include <osw_thread.h>
#include <osw_state.h>
#include <osw_drv_common.h>
#include <osw_conf.h>
#include <osw_confsync.h>
#include <osw_module.h>
#include <osw_ut.h>
#include <osw_drv_dummy.h>
#include "ow_conf.h"
#include "osw_types.h"
#include <module.h>
#include <memutil.h>
#include <ds_tree.h>
#include <const.h>
#include <util.h>
#include <log.h>
#include <os.h>
#include <ev.h>

#define OW_CONF_DEFAULT_BEACON_INTERVAL_TU 100

struct ow_conf_phy {
    struct ds_tree_node node;
    char *phy_name;
    bool *enabled;
    bool *ap_wmm_enabled;
    bool *ap_ht_enabled;
    bool *ap_vht_enabled;
    bool *ap_he_enabled;
    bool *ap_eht_enabled;
    int *tx_chainmask;
    int *tx_power_dbm;
    int *thermal_tx_chainmask;
    int *ap_beacon_interval_tu;
    uint16_t *ap_supp_rates;
    uint16_t *ap_basic_rates;
    enum osw_rate_legacy *ap_beacon_rate;
    enum osw_rate_legacy *ap_mcast_rate;
    enum osw_rate_legacy *ap_mgmt_rate;
    struct osw_channel *ap_channel;
};

struct ow_conf_vif {
    struct ds_tree_node node;
    char *phy_name;
    char *vif_name;

    bool *enabled;
    enum osw_vif_type *type;
    int *tx_power_dbm;

    struct osw_channel *ap_channel;
    struct osw_ssid *ap_ssid;
    struct osw_ifname *ap_bridge_if_name;
    struct osw_nas_id *ap_nas_identifier;
    struct osw_ft_encr_key *ap_ft_encr_key;
    struct ds_tree ap_psk_tree; /* ow_conf_psk */
    struct ds_tree ap_acl_tree; /* ow_conf_acl */
    struct ds_tree ap_neigh_tree; /* ow_conf_neigh */
    struct ds_tree ap_neigh_ft_tree; /* ow_conf_neigh_ft */
    struct ds_dlist ap_radius_list; /* ow_conf_radius */
    struct ds_dlist ap_acct_list; /* ow_conf_radius */
    char *ap_passpoint_ref;
    bool *ap_beacon_protection;
    bool *ap_ssid_hidden;
    bool *ap_isolated;
    bool *ap_ht_enabled;
    bool *ap_vht_enabled;
    bool *ap_he_enabled;
    bool *ap_eht_enabled;
    bool *ap_ht_required;
    bool *ap_vht_required;
    bool *ap_wpa;
    bool *ap_rsn;
    bool *ap_pairwise_tkip;
    bool *ap_pairwise_ccmp;
    bool *ap_pairwise_ccmp256;
    bool *ap_pairwise_gcmp;
    bool *ap_pairwise_gcmp256;
    bool *ap_akm_eap;
    bool *ap_akm_eap_sha256;
    bool *ap_akm_eap_sha384;
    bool *ap_akm_eap_suite_b;
    bool *ap_akm_eap_suite_b192;
    bool *ap_akm_psk;
    bool *ap_akm_psk_sha256;
    bool *ap_akm_sae;
    bool *ap_akm_sae_ext;
    bool *ap_akm_ft_eap;
    bool *ap_akm_ft_eap_sha384;
    bool *ap_akm_ft_psk;
    bool *ap_akm_ft_sae;
    bool *ap_akm_ft_sae_ext;
    bool *ap_wps;
    bool *ap_wmm;
    bool *ap_wmm_uapsd;
    bool *ap_wnm_bss_trans;
    bool *ap_rrm_neighbor_report;
    bool *ap_mcast2ucast;
    bool *ap_ieee8021x;
    bool *ap_ft_over_ds;
    bool *ap_ft_pmk_r1_push;
    bool *ap_ft_psk_generate_local;
    int *ap_group_rekey_seconds;
    int *ap_ft_mobility_domain;
    int *ap_beacon_interval_tu;
    int *ap_ft_pmk_r0_key_lifetime_sec;
    int *ap_ft_pmk_r1_max_key_lifetime_sec;
    uint16_t *ap_supp_rates;
    uint16_t *ap_basic_rates;
    struct osw_beacon_rate *ap_beacon_rate;
    enum osw_pmf *ap_pmf;
    struct osw_multi_ap *ap_multi_ap;
    enum osw_acl_policy *ap_acl_policy;
    struct ds_tree sta_net_tree;
};

struct ow_radius {
    char *uuid;
    char *server;
    char *passphrase;
    int port;
};

struct ow_conf_net {
    struct ds_tree_node node;
    struct osw_ssid ssid;
    struct osw_psk psk;
    struct osw_hwaddr bssid;
    struct osw_wpa wpa;
    struct osw_ifname bridge_if_name;
    bool multi_ap;
    int priority;
};

struct ow_conf_acl {
    struct ds_tree_node node;
    struct osw_hwaddr mac_addr;
};

struct ow_conf_psk {
    struct ds_tree_node node;
    struct ow_conf_vif *vif;
    struct osw_ap_psk ap_psk;
};

struct ow_conf_neigh {
    struct ds_tree_node node;
    struct osw_neigh neigh;
};

struct ow_conf_neigh_ft {
    struct ds_tree_node node;
    struct osw_neigh_ft neigh_ft;
};

struct ow_conf_radius_ref
{
    struct ds_dlist_node node;
    const char *ref_id;
};

struct ow_conf_radius {
    struct ds_tree_node node;
    struct ow_radius radius;
};

struct ow_conf_passpoint {
    struct ds_tree_node node;
    char *uuid;
    bool *hs20_enabled;
    bool *adv_wan_status;
    bool *adv_wan_symmetric;
    bool *adv_wan_at_capacity;
    bool *osen;
    bool *asra;
    int *ant;
    int *venue_group;
    int *venue_type;
    int *anqp_domain_id;
    int *pps_mo_id;
    int *t_c_timestamp;
    char *t_c_filename;
    char *anqp_elem;

    struct osw_hwaddr hessid;
    struct osw_ssid osu_ssid;

    char **domain_list;
    size_t domain_list_len;

    char **nairealm_list;
    size_t nairealm_list_len;

    char **roamc_list;
    size_t roamc_list_len;

    char **oper_fname_list;
    size_t oper_fname_list_len;

    char **venue_name_list;
    size_t venue_name_list_len;

    char **venue_url_list;
    size_t venue_url_list_len;

    char **list_3gpp_list;
    size_t list_3gpp_list_len;

    int *net_auth_type_list;
    size_t net_auth_type_list_len;

    /* FIXME */
    //struct osw_osu_provider_list osu_list;
};

struct ow_conf {
    struct osw_conf_mutator conf_mutator;
    struct ds_tree phy_tree;
    struct ds_tree vif_tree;
    struct ds_tree rad_tree;
    struct ds_tree passpoint_tree;
    struct ds_dlist obs_list;
    bool *ap_vlan_enabled;
};

static int
ow_conf_acl_cmp(const void *a, const void *b)
{
    const struct osw_hwaddr *x = a;
    const struct osw_hwaddr *y = b;
    return memcmp(x, y, sizeof(*x));
}

static inline int ow_sta_net_cmp(const struct ow_conf_net *a, const struct ow_conf_net *b)
{
    int r;

    r = osw_ssid_cmp(&a->ssid, &b->ssid);
    if (r != 0) return r;

    r = osw_hwaddr_cmp(&a->bssid, &b->bssid);
    if (r != 0) return r;

    return 0;
}

static struct ow_conf_phy *
ow_conf_phy_alloc(struct ow_conf *self, const char *phy_name)
{
    struct ow_conf_phy *phy = CALLOC(1, sizeof(*phy));
    phy->phy_name = STRDUP(phy_name);
    ds_tree_insert(&self->phy_tree, phy, phy->phy_name);
    return phy;
}

static struct ow_conf_phy *
ow_conf_phy_get(struct ow_conf *self, const char *phy_name)
{
    return ds_tree_find(&self->phy_tree, phy_name) ?: ow_conf_phy_alloc(self, phy_name);
}

static struct ow_conf_phy *
ow_conf_phy_get_ro(struct ow_conf *self, const char *phy_name)
{
    return ds_tree_find(&self->phy_tree, phy_name);
}

static struct ow_conf_vif *
ow_conf_vif_alloc(struct ow_conf *self, const char *vif_name)
{
    struct ow_conf_vif *vif = CALLOC(1, sizeof(*vif));
    vif->vif_name = STRDUP(vif_name);
    ds_tree_insert(&self->vif_tree, vif, vif->vif_name);
    ds_tree_init(&vif->ap_psk_tree, ds_int_cmp, struct ow_conf_psk, node);
    ds_tree_init(&vif->ap_acl_tree, ow_conf_acl_cmp, struct ow_conf_acl, node);
    ds_tree_init(&vif->ap_neigh_tree, (ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_conf_neigh, node);
    ds_tree_init(&vif->ap_neigh_ft_tree, (ds_key_cmp_t *)osw_hwaddr_cmp, struct ow_conf_neigh_ft, node);
    ds_dlist_init(&vif->ap_radius_list, struct ow_conf_radius_ref, node);
    ds_dlist_init(&vif->ap_acct_list, struct ow_conf_radius, node);
    ds_tree_init(&vif->sta_net_tree, (ds_key_cmp_t *)ow_sta_net_cmp, struct ow_conf_net, node);
    return vif;
}

static struct ow_conf_vif *
ow_conf_vif_get(struct ow_conf *self, const char *vif_name)
{
    return ds_tree_find(&self->vif_tree, vif_name) ?: ow_conf_vif_alloc(self, vif_name);
}

static struct ow_conf_vif *
ow_conf_vif_get_ro(struct ow_conf *self, const char *vif_name)
{
    return ds_tree_find(&self->vif_tree, vif_name);
}

static void
ow_conf_conf_mutate_phy(struct ow_conf *self,
                        struct osw_conf_phy *osw_phy)
{
    const char *phy_name = osw_phy->phy_name;
    struct ow_conf_phy *ow_phy = ds_tree_find(&self->phy_tree, phy_name);

    if (ow_phy == NULL) return;

    if (ow_phy->enabled != NULL) osw_phy->enabled = *ow_phy->enabled;

    if (ow_phy->thermal_tx_chainmask != NULL) {
        osw_phy->tx_chainmask = *ow_phy->thermal_tx_chainmask;
    }
    else if (ow_phy->tx_chainmask != NULL) {
        osw_phy->tx_chainmask = *ow_phy->tx_chainmask;
    }
}

static void
ow_conf_conf_mutate_acl(struct ds_tree *in,
                        struct ds_tree *out)
{
    struct osw_conf_acl *p;
    struct ow_conf_acl *i;

    /* FIXME: differentiate between overwrite and append */
    while ((p = ds_tree_head(out)) != NULL) {
        ds_tree_remove(out, p);
        FREE(p);
    }

    ds_tree_foreach(in, i) {
        p = MALLOC(sizeof(*p));
        p->mac_addr = i->mac_addr;
        ds_tree_insert(out, p, &p->mac_addr);
    }
}

static void
ow_conf_conf_mutate_psk(struct ds_tree *in,
                        struct ds_tree *out)
{
    struct osw_conf_psk *p;
    struct ow_conf_psk *i;

    /* FIXME: differentiate between overwrite and append */
    while ((p = ds_tree_head(out)) != NULL) {
        ds_tree_remove(out, p);
        FREE(p);
    }

    ds_tree_foreach(in, i) {
        p = MALLOC(sizeof(*p));
        p->ap_psk = i->ap_psk;
        ds_tree_insert(out, p, &p->ap_psk.key_id);
    }
}

static void
ow_conf_conf_mutate_radius(struct ow_conf *self,
                           struct ds_dlist *in,
                           struct ds_dlist *out)
{
    struct osw_conf_radius *p;
    struct ow_conf_radius *rad;
    struct ow_conf_radius_ref *i;

    /* FIXME: differentiate between overwrite and append */
    while ((p = ds_dlist_remove_head(out)) != NULL)
        osw_conf_radius_free(p);

    ds_dlist_foreach(in, i) {
        if ((rad = ds_tree_find(&self->rad_tree, i->ref_id)) == NULL) {
            LOGW("ow: conf: mutate_radius tries to add an unknown ref_id");
            continue;
        }
        if (rad->radius.server == NULL ||
            rad->radius.passphrase == NULL ||
            rad->radius.port == 0) {
            LOGW("ow: conf: mutate_radius tries to add incomplete entry");
            continue;
        }
        p = CALLOC(1, sizeof(*p));
        p->radius.server = STRDUP(rad->radius.server);
        p->radius.passphrase = STRDUP(rad->radius.passphrase);
        p->radius.port = rad->radius.port;
        ds_dlist_insert_tail(out, p);
    }
}

static void
ow_conf_conf_mutate_neigh(struct ds_tree *in,
                          struct ds_tree *out)
{
    struct osw_conf_neigh *p;
    struct ow_conf_neigh *i;

    /* FIXME: differentiate between overwrite and append */
    while ((p = ds_tree_head(out)) != NULL) {
        ds_tree_remove(out, p);
        FREE(p);
    }

    ds_tree_foreach(in, i) {
        p = MALLOC(sizeof(*p));
        p->neigh = i->neigh;
        ds_tree_insert(out, p, &p->neigh.bssid);
    }
}

static void
ow_conf_conf_mutate_neigh_ft_add_local(struct osw_conf_vif *osw_conf_vif_self,
                                       struct ow_conf *self,
                                       struct ds_tree *in,
                                       struct ds_tree *out)
{
    /* Adding all local ap's that have FT wpa_key_mgmt
     * to each FT enabled ap's fast transition neighbors.
     * <vif_name>.rxkhs needs to be populated with all
     * local FT enabled interfaces.
    */
    struct ds_tree *phy_tree = osw_conf_vif_self->phy->phy_tree;
    struct osw_conf_phy *conf_phy;
    struct osw_conf_neigh_ft *p;

    ds_tree_foreach(phy_tree, conf_phy) {
        if (conf_phy == NULL) continue;

        struct osw_conf_vif *conf_vif;

        ds_tree_foreach(&conf_phy->vif_tree, conf_vif) {
            if (conf_vif == NULL) continue;
            if (conf_vif->vif_type != OSW_VIF_AP) continue;

            struct osw_conf_vif_ap *ap = &conf_vif->u.ap;
            if (ap == NULL) continue;

            if (!osw_wpa_is_ft(&ap->wpa)) continue;
            struct ow_conf_vif *ow_vif = ow_conf_vif_get(self, conf_vif->vif_name);

            /* Adding only interfaces that have the same ssid */
            if (osw_ssid_cmp(&conf_vif->u.ap.ssid, &osw_conf_vif_self->u.ap.ssid) != 0) continue;

            if (ow_vif->ap_ft_encr_key == NULL) continue;
            if (ow_vif->ap_nas_identifier == NULL) continue;

            p = MALLOC(sizeof(*p));

            memcpy(&p->neigh_ft.bssid, &conf_vif->mac_addr, sizeof(conf_vif->mac_addr));
            STRSCPY(p->neigh_ft.ft_encr_key.buf, ow_vif->ap_ft_encr_key->buf);
            STRSCPY(p->neigh_ft.nas_identifier.buf, ow_vif->ap_nas_identifier->buf);

            ds_tree_insert(out, p, &p->neigh_ft.bssid);
        }
    }
}

static void
ow_conf_conf_mutate_neigh_ft(struct osw_conf_vif *osw_vif,
                             struct ow_conf *self,
                             struct ds_tree *in,
                             struct ds_tree *out)
{
    struct osw_conf_neigh_ft *p;
    struct ow_conf_neigh_ft *i;

    while ((p = ds_tree_head(out)) != NULL) {
        ds_tree_remove(out, p);
        FREE(p);
    }

    ds_tree_foreach(in, i) {
        p = MALLOC(sizeof(*p));
        p->neigh_ft = i->neigh_ft;
        ds_tree_insert(out, p, &p->neigh_ft.bssid);
    }

    if (osw_wpa_is_ft(&osw_vif->u.ap.wpa)) {
        ow_conf_conf_mutate_neigh_ft_add_local(osw_vif, self, in, out);
    }
}

static void
ow_conf_conf_mutate_passpoint(struct ow_conf *self,
                              char *in_ref_id,
                              struct osw_passpoint *out)
{
    osw_passpoint_free_internal(out);
    if (in_ref_id == NULL) {
        return;
    }

    struct ow_conf_passpoint *p = ds_tree_find(&self->passpoint_tree, in_ref_id);
    size_t i;

    if (p == NULL) {
        LOGW("ow: conf: unknown ref_id, passpoint not found");
        return;
    }

    if (p->hs20_enabled != NULL) out->hs20_enabled = *p->hs20_enabled;
    if (p->adv_wan_status != NULL) out->adv_wan_status = *p->adv_wan_status;
    if (p->adv_wan_symmetric != NULL) out->adv_wan_symmetric = *p->adv_wan_symmetric;
    if (p->adv_wan_at_capacity != NULL) out->adv_wan_at_capacity = *p->adv_wan_at_capacity;
    if (p->osen != NULL) out->osen = *p->osen;
    if (p->asra != NULL) out->asra = *p->asra;
    if (p->ant != NULL) out->ant = *p->ant;
    if (p->venue_group != NULL) out->venue_group = *p->venue_group;
    if (p->venue_type != NULL) out->venue_type = *p->venue_type;
    if (p->anqp_domain_id != NULL) out->anqp_domain_id = *p->anqp_domain_id;
    if (p->pps_mo_id != NULL) out->pps_mo_id = *p->pps_mo_id;
    if (p->t_c_timestamp != NULL) out->t_c_timestamp = *p->t_c_timestamp;

    if (STRSLEN(p->t_c_filename) > 0)
        out->t_c_filename = STRDUP(p->t_c_filename);

    if (STRSLEN(p->anqp_elem) > 0)
        out->anqp_elem = STRDUP(p->anqp_elem);

    if (!osw_hwaddr_is_zero(&p->hessid))
        memcpy(&out->hessid, &p->hessid, sizeof(out->hessid));

    if (p->osu_ssid.len > 0)
        memcpy(&out->osu_ssid, &p->osu_ssid, sizeof(out->osu_ssid));

    if (p->domain_list_len > 0) {
        out->domain_list = CALLOC(p->domain_list_len, sizeof(*out->domain_list));
        for (i = 0; i < p->domain_list_len; i++) {
            out->domain_list[i] = STRDUP(p->domain_list[i]);
        }
        out->domain_list_len = p->domain_list_len;
    }

    if (p->nairealm_list_len > 0) {
        out->nairealm_list = CALLOC(p->nairealm_list_len, sizeof(*out->nairealm_list));
        for (i = 0; i < p->nairealm_list_len; i++) {
            out->nairealm_list[i] = STRDUP(p->nairealm_list[i]);
        }
        out->nairealm_list_len = p->nairealm_list_len;
    }

    if (p->roamc_list_len > 0) {
        out->roamc_list = CALLOC(p->roamc_list_len, sizeof(*out->roamc_list));
        for (i = 0; i < p->roamc_list_len; i++) {
            out->roamc_list[i] = STRDUP(p->roamc_list[i]);
        }
        out->roamc_list_len = p->roamc_list_len;
    }

    if (p->oper_fname_list_len > 0) {
        out->oper_fname_list = CALLOC(p->oper_fname_list_len, sizeof(*out->oper_fname_list));
        for (i = 0; i < p->oper_fname_list_len; i++) {
            out->oper_fname_list[i] = STRDUP(p->oper_fname_list[i]);
        }
        out->oper_fname_list_len = p->oper_fname_list_len;
    }

    if (p->venue_name_list_len > 0) {
        out->venue_name_list = CALLOC(p->venue_name_list_len, sizeof(*out->venue_name_list));
        for (i = 0; i < p->venue_name_list_len; i++) {
            out->venue_name_list[i] = STRDUP(p->venue_name_list[i]);
        }
        out->venue_name_list_len = p->venue_name_list_len;
    }

    if (p->venue_url_list_len > 0) {
        out->venue_url_list = CALLOC(p->venue_url_list_len, sizeof(*out->venue_url_list));
        for (i = 0; i < p->venue_url_list_len; i++) {
            out->venue_url_list[i] = STRDUP(p->venue_url_list[i]);
        }
        out->venue_url_list_len = p->venue_url_list_len;
    }

    if (p->list_3gpp_list_len > 0) {
        out->list_3gpp_list = CALLOC(p->list_3gpp_list_len, sizeof(*out->list_3gpp_list));
        for (i = 0; i < p->list_3gpp_list_len; i++) {
            out->list_3gpp_list[i] = STRDUP(p->list_3gpp_list[i]);
        }
        out->list_3gpp_list_len = p->list_3gpp_list_len;
    }

    if (p->net_auth_type_list_len > 0) {
        out->net_auth_type_list = CALLOC(p->net_auth_type_list_len, sizeof(*out->net_auth_type_list));
        memcpy(out->net_auth_type_list, p->net_auth_type_list,
                sizeof(*out->net_auth_type_list) * p->net_auth_type_list_len);
        out->net_auth_type_list_len = p->net_auth_type_list_len;
    }
}

static bool
ow_conf_is_ft_set(struct ow_conf_vif *ow_vif)
{
    bool is_ft_eap_set = (ow_vif->ap_akm_ft_eap != NULL && *(ow_vif->ap_akm_ft_eap));
    bool is_ft_psk_set = (ow_vif->ap_akm_ft_psk != NULL && *(ow_vif->ap_akm_ft_psk));
    bool is_ft_sae_set = (ow_vif->ap_akm_ft_sae != NULL && *(ow_vif->ap_akm_ft_sae));
    bool is_ft_sae_ext_set = (ow_vif->ap_akm_ft_sae_ext != NULL && *(ow_vif->ap_akm_ft_sae_ext));
    bool is_ft_eap_sha384_set = (ow_vif->ap_akm_ft_eap_sha384 != NULL && *(ow_vif->ap_akm_ft_eap_sha384));

    return is_ft_eap_set || is_ft_psk_set || is_ft_sae_set || is_ft_sae_ext_set || is_ft_eap_sha384_set;
}

static void
ow_conf_conf_mutate_fast_transition(struct ow_conf_vif *ow_vif,
                                    struct osw_conf_vif *osw_vif)
{
    if (ow_conf_is_ft_set(ow_vif)) {
        osw_vif->u.ap.ft_over_ds = (ow_vif->ap_ft_over_ds != NULL) ? *ow_vif->ap_ft_over_ds : 1;
        osw_vif->u.ap.ft_pmk_r0_key_lifetime_sec = (ow_vif->ap_ft_pmk_r0_key_lifetime_sec != NULL) ? *ow_vif->ap_ft_pmk_r0_key_lifetime_sec : 1209600;
        osw_vif->u.ap.ft_pmk_r1_max_key_lifetime_sec = (ow_vif->ap_ft_pmk_r1_max_key_lifetime_sec != NULL) ? *ow_vif->ap_ft_pmk_r1_max_key_lifetime_sec : 0;
        osw_vif->u.ap.ft_pmk_r1_push = (ow_vif->ap_ft_pmk_r1_push != NULL) ? *ow_vif->ap_ft_pmk_r1_push : 0;
        osw_vif->u.ap.ft_psk_generate_local = (ow_vif->ap_ft_psk_generate_local != NULL) ? *ow_vif->ap_ft_psk_generate_local : 0;

        return;
    }

    if (ow_vif->ap_ft_over_ds != NULL) osw_vif->u.ap.ft_over_ds = *ow_vif->ap_ft_over_ds;
    if (ow_vif->ap_ft_pmk_r0_key_lifetime_sec != NULL) osw_vif->u.ap.ft_pmk_r0_key_lifetime_sec = *ow_vif->ap_ft_pmk_r0_key_lifetime_sec;
    if (ow_vif->ap_ft_pmk_r1_max_key_lifetime_sec != NULL) osw_vif->u.ap.ft_pmk_r1_max_key_lifetime_sec = *ow_vif->ap_ft_pmk_r1_max_key_lifetime_sec;
    if (ow_vif->ap_ft_pmk_r1_push != NULL) osw_vif->u.ap.ft_pmk_r1_push = *ow_vif->ap_ft_pmk_r1_push;
    if (ow_vif->ap_ft_psk_generate_local != NULL) osw_vif->u.ap.ft_psk_generate_local = *ow_vif->ap_ft_psk_generate_local;
}

static void
ow_conf_conf_mutate_vif_ap(struct ow_conf *self,
                           struct ow_conf_phy *ow_phy,
                           struct ow_conf_vif *ow_vif,
                           struct osw_conf_vif *osw_vif)
{
    const char *vif_name = osw_vif->vif_name;

    if (ow_phy != NULL) {
        if (ow_phy->ap_wmm_enabled != NULL) osw_vif->u.ap.mode.wmm_enabled = *ow_phy->ap_wmm_enabled;
        if (ow_phy->ap_ht_enabled != NULL) osw_vif->u.ap.mode.ht_enabled = *ow_phy->ap_ht_enabled;
        if (ow_phy->ap_vht_enabled != NULL) osw_vif->u.ap.mode.vht_enabled = *ow_phy->ap_vht_enabled;
        if (ow_phy->ap_he_enabled != NULL) osw_vif->u.ap.mode.he_enabled = *ow_phy->ap_he_enabled;
        if (ow_phy->ap_eht_enabled != NULL) osw_vif->u.ap.mode.eht_enabled = *ow_phy->ap_eht_enabled;
        if (ow_phy->ap_beacon_interval_tu != NULL) osw_vif->u.ap.beacon_interval_tu = *ow_phy->ap_beacon_interval_tu;
        if (ow_phy->ap_channel != NULL) osw_vif->u.ap.channel = *ow_phy->ap_channel;
        if (ow_phy->ap_supp_rates != NULL) osw_vif->u.ap.mode.supported_rates = *ow_phy->ap_supp_rates;
        if (ow_phy->ap_basic_rates != NULL) osw_vif->u.ap.mode.basic_rates = *ow_phy->ap_basic_rates;
        if (ow_phy->ap_beacon_rate != NULL) {
            osw_vif->u.ap.mode.beacon_rate.type = OSW_BEACON_RATE_ABG;
            osw_vif->u.ap.mode.beacon_rate.u.legacy = *ow_phy->ap_beacon_rate;
        }

        if (ow_phy->ap_mcast_rate != NULL) osw_vif->u.ap.mode.mcast_rate = *ow_phy->ap_mcast_rate;
        if (ow_phy->ap_mgmt_rate != NULL) osw_vif->u.ap.mode.mgmt_rate = *ow_phy->ap_mgmt_rate;
        if (ow_phy->tx_power_dbm != NULL) osw_vif->tx_power_dbm = *ow_phy->tx_power_dbm;
    }
    if (ow_vif->ap_channel != NULL) osw_vif->u.ap.channel = *ow_vif->ap_channel;
    if (ow_vif->ap_ssid != NULL) osw_vif->u.ap.ssid = *ow_vif->ap_ssid;
    if (ow_vif->ap_bridge_if_name != NULL) osw_vif->u.ap.bridge_if_name = *ow_vif->ap_bridge_if_name;
    if (ow_vif->ap_nas_identifier != NULL) osw_vif->u.ap.nas_identifier = *ow_vif->ap_nas_identifier;
    if (ow_vif->ap_ft_encr_key != NULL) osw_vif->u.ap.ft_encr_key = *ow_vif->ap_ft_encr_key;
    if (ow_vif->ap_beacon_protection != NULL) osw_vif->u.ap.wpa.beacon_protection = *ow_vif->ap_beacon_protection;
    if (ow_vif->ap_ssid_hidden != NULL) osw_vif->u.ap.ssid_hidden = *ow_vif->ap_ssid_hidden;
    if (ow_vif->ap_isolated != NULL) osw_vif->u.ap.isolated = *ow_vif->ap_isolated;
    if (ow_vif->ap_ht_enabled != NULL) osw_vif->u.ap.mode.ht_enabled = *ow_vif->ap_ht_enabled;
    if (ow_vif->ap_vht_enabled != NULL) osw_vif->u.ap.mode.vht_enabled = *ow_vif->ap_vht_enabled;
    if (ow_vif->ap_he_enabled != NULL) osw_vif->u.ap.mode.he_enabled = *ow_vif->ap_he_enabled;
    if (ow_vif->ap_eht_enabled != NULL) osw_vif->u.ap.mode.eht_enabled = *ow_vif->ap_eht_enabled;
    if (ow_vif->ap_ht_required != NULL) osw_vif->u.ap.mode.ht_required = *ow_vif->ap_ht_required;
    if (ow_vif->ap_vht_required != NULL) osw_vif->u.ap.mode.vht_required = *ow_vif->ap_vht_required;
    if (ow_vif->ap_supp_rates != NULL) osw_vif->u.ap.mode.supported_rates = *ow_vif->ap_supp_rates;
    if (ow_vif->ap_basic_rates != NULL) osw_vif->u.ap.mode.basic_rates = *ow_vif->ap_basic_rates;
    if (ow_vif->ap_beacon_rate != NULL) osw_vif->u.ap.mode.beacon_rate = *ow_vif->ap_beacon_rate;
    if (ow_vif->ap_wpa != NULL) osw_vif->u.ap.wpa.wpa = *ow_vif->ap_wpa;
    if (ow_vif->ap_rsn != NULL) osw_vif->u.ap.wpa.rsn = *ow_vif->ap_rsn;
    if (ow_vif->ap_pairwise_tkip != NULL) osw_vif->u.ap.wpa.pairwise_tkip = *ow_vif->ap_pairwise_tkip;
    if (ow_vif->ap_pairwise_ccmp != NULL) osw_vif->u.ap.wpa.pairwise_ccmp = *ow_vif->ap_pairwise_ccmp;
    if (ow_vif->ap_pairwise_ccmp256 != NULL) osw_vif->u.ap.wpa.pairwise_ccmp256 = *ow_vif->ap_pairwise_ccmp256;
    if (ow_vif->ap_pairwise_gcmp != NULL) osw_vif->u.ap.wpa.pairwise_gcmp = *ow_vif->ap_pairwise_gcmp;
    if (ow_vif->ap_pairwise_gcmp256 != NULL) osw_vif->u.ap.wpa.pairwise_gcmp256 = *ow_vif->ap_pairwise_gcmp256;
    if (ow_vif->ap_akm_eap != NULL) osw_vif->u.ap.wpa.akm_eap = *ow_vif->ap_akm_eap;
    if (ow_vif->ap_akm_eap_sha256 != NULL) osw_vif->u.ap.wpa.akm_eap_sha256 = *ow_vif->ap_akm_eap_sha256;
    if (ow_vif->ap_akm_eap_sha384 != NULL) osw_vif->u.ap.wpa.akm_eap_sha384 = *ow_vif->ap_akm_eap_sha384;
    if (ow_vif->ap_akm_eap_suite_b != NULL) osw_vif->u.ap.wpa.akm_eap_suite_b = *ow_vif->ap_akm_eap_suite_b;
    if (ow_vif->ap_akm_eap_suite_b192 != NULL) osw_vif->u.ap.wpa.akm_eap_suite_b192 = *ow_vif->ap_akm_eap_suite_b192;
    if (ow_vif->ap_akm_psk != NULL) osw_vif->u.ap.wpa.akm_psk = *ow_vif->ap_akm_psk;
    if (ow_vif->ap_akm_psk_sha256 != NULL) osw_vif->u.ap.wpa.akm_psk_sha256 = *ow_vif->ap_akm_psk_sha256;
    if (ow_vif->ap_akm_sae != NULL) osw_vif->u.ap.wpa.akm_sae = *ow_vif->ap_akm_sae;
    if (ow_vif->ap_akm_sae_ext != NULL) osw_vif->u.ap.wpa.akm_sae_ext = *ow_vif->ap_akm_sae_ext;
    if (ow_vif->ap_akm_ft_eap != NULL) osw_vif->u.ap.wpa.akm_ft_eap = *ow_vif->ap_akm_ft_eap;
    if (ow_vif->ap_akm_ft_eap_sha384 != NULL) osw_vif->u.ap.wpa.akm_ft_eap_sha384 = *ow_vif->ap_akm_ft_eap_sha384;
    if (ow_vif->ap_akm_ft_psk != NULL) osw_vif->u.ap.wpa.akm_ft_psk = *ow_vif->ap_akm_ft_psk;
    if (ow_vif->ap_akm_ft_sae != NULL) osw_vif->u.ap.wpa.akm_ft_sae = *ow_vif->ap_akm_ft_sae;
    if (ow_vif->ap_akm_ft_sae_ext != NULL) osw_vif->u.ap.wpa.akm_ft_sae_ext = *ow_vif->ap_akm_ft_sae_ext;
    if (ow_vif->ap_pmf != NULL) osw_vif->u.ap.wpa.pmf = *ow_vif->ap_pmf;
    if (ow_vif->ap_group_rekey_seconds != NULL) osw_vif->u.ap.wpa.group_rekey_seconds = *ow_vif->ap_group_rekey_seconds;
    if (ow_vif->ap_ft_mobility_domain != NULL) osw_vif->u.ap.wpa.ft_mobility_domain = *ow_vif->ap_ft_mobility_domain;
    if (ow_vif->ap_beacon_interval_tu != NULL) osw_vif->u.ap.beacon_interval_tu = *ow_vif->ap_beacon_interval_tu;
    if (ow_vif->ap_acl_policy != NULL) osw_vif->u.ap.acl_policy = *ow_vif->ap_acl_policy;
    if (ow_vif->ap_wps != NULL) osw_vif->u.ap.mode.wps = *ow_vif->ap_wps;
    if (ow_vif->ap_wmm != NULL) osw_vif->u.ap.mode.wmm_enabled = *ow_vif->ap_wmm;
    if (ow_vif->ap_wmm_uapsd != NULL) osw_vif->u.ap.mode.wmm_uapsd_enabled = *ow_vif->ap_wmm_uapsd;
    if (ow_vif->ap_wnm_bss_trans != NULL) osw_vif->u.ap.mode.wnm_bss_trans = *ow_vif->ap_wnm_bss_trans;
    if (ow_vif->ap_rrm_neighbor_report != NULL) osw_vif->u.ap.mode.rrm_neighbor_report = *ow_vif->ap_rrm_neighbor_report;
    if (ow_vif->ap_mcast2ucast != NULL) osw_vif->u.ap.mcast2ucast = *ow_vif->ap_mcast2ucast;
    if (ow_vif->ap_multi_ap != NULL) osw_vif->u.ap.multi_ap = *ow_vif->ap_multi_ap;
    if (ow_vif->tx_power_dbm != NULL) osw_vif->tx_power_dbm = *ow_vif->tx_power_dbm;

    ow_conf_conf_mutate_acl(&ow_vif->ap_acl_tree, &osw_vif->u.ap.acl_tree);
    ow_conf_conf_mutate_psk(&ow_vif->ap_psk_tree, &osw_vif->u.ap.psk_tree);
    ow_conf_conf_mutate_neigh(&ow_vif->ap_neigh_tree, &osw_vif->u.ap.neigh_tree);
    ow_conf_conf_mutate_neigh_ft(osw_vif, self, &ow_vif->ap_neigh_ft_tree, &osw_vif->u.ap.neigh_ft_tree);
    ow_conf_conf_mutate_radius(self, &ow_vif->ap_radius_list, &osw_vif->u.ap.radius_list);
    ow_conf_conf_mutate_radius(self, &ow_vif->ap_acct_list, &osw_vif->u.ap.accounting_list);
    ow_conf_conf_mutate_passpoint(self, ow_vif->ap_passpoint_ref, &osw_vif->u.ap.passpoint);
    ow_conf_conf_mutate_fast_transition(ow_vif, osw_vif);

    if (osw_vif->u.ap.beacon_interval_tu == 0) {
        osw_vif->u.ap.beacon_interval_tu = OW_CONF_DEFAULT_BEACON_INTERVAL_TU;
        LOGD("%s: beacon interval undefined, setting default: %d",
             vif_name,
             osw_vif->u.ap.beacon_interval_tu);
    }
}

static void
ow_conf_conf_mutate_vif_ap_vlan(struct ow_conf *conf,
                                struct ow_conf_phy *ow_phy,
                                struct ow_conf_vif *ow_vif,
                                struct osw_conf_vif *osw_vif,
                                bool vif_eligible)
{
    const bool vif_enabled_is_defined = (vif_eligible)
                                     && (ow_vif->enabled != NULL);
    const bool vif_enabled_is_undefined = !vif_enabled_is_defined;
    const bool ap_vlan_enabled_can_override = vif_enabled_is_undefined;
    const bool ap_vlan_is_defined = (conf->ap_vlan_enabled != NULL);

    if (ap_vlan_is_defined && ap_vlan_enabled_can_override) {
        osw_vif->enabled = *conf->ap_vlan_enabled;
    }
}

static void
ow_conf_conf_mutate_vif_sta(struct ow_conf_phy *ow_phy,
                            struct ow_conf_vif *ow_vif,
                            struct osw_conf_vif *osw_vif)
{
    struct ds_dlist *list = &osw_vif->u.sta.net_list;
    struct osw_conf_net *osw_net;
    struct ow_conf_net *net;

    if (ow_phy != NULL) {
        if (ow_phy->tx_power_dbm != NULL) osw_vif->tx_power_dbm = *ow_phy->tx_power_dbm;
    }

    if (ow_vif->tx_power_dbm != NULL) osw_vif->tx_power_dbm = *ow_vif->tx_power_dbm;

    /* FIXME: It might be convenient to be able to declare
     * whether the list is exhaustive or additive. If it
     * additive it would only append to the list. Currently
     * it's exhaustive and replaces the found  list.
     */
    while ((osw_net = ds_dlist_remove_head(list)) != NULL) {
        FREE(osw_net);
    }

    ds_tree_foreach(&ow_vif->sta_net_tree, net) {
        struct osw_conf_net *n = CALLOC(1, sizeof(*n));
        memcpy(&n->ssid, &net->ssid, sizeof(n->ssid));
        memcpy(&n->bssid, &net->bssid, sizeof(n->bssid));
        memcpy(&n->psk, &net->psk, sizeof(n->psk));
        memcpy(&n->wpa, &net->wpa, sizeof(n->wpa));
        memcpy(&n->bridge_if_name, &net->bridge_if_name, sizeof(n->bridge_if_name));
        n->multi_ap = net->multi_ap;
        n->priority = net->priority;
        ds_dlist_insert_tail(list, n);
    }
}

static void
ow_conf_conf_mutate_vif(struct ow_conf *self,
                        struct osw_conf_vif *osw_vif)
{
    const char *phy_name = osw_vif->phy->phy_name;
    const char *vif_name = osw_vif->vif_name;
    struct ow_conf_phy *ow_phy = ds_tree_find(&self->phy_tree, phy_name);
    struct ow_conf_vif *ow_vif = ds_tree_find(&self->vif_tree, vif_name);
    enum osw_vif_type type = osw_vif->vif_type;
    const bool vif_defined = (ow_vif != NULL);
    const bool phy_undefined = (vif_defined)
                            && (ow_vif->phy_name == NULL);
    const bool phy_mismatch = (vif_defined)
                           && (phy_undefined || (strcmp(ow_vif->phy_name, phy_name) != 0));
    const bool vif_eligible = vif_defined && !phy_mismatch;

    if (phy_mismatch) {
        LOGN("ow: conf: %s: phy_name mismatch (configured %s, reported %s)",
             vif_name,
             ow_vif->phy_name,
             phy_name);
        return;
    }

    /* FIXME: Use macro and add tracing */
    if (vif_eligible) {
        if (ow_vif->enabled != NULL) {
            osw_vif->enabled = *ow_vif->enabled;
        }

        if (ow_vif->type != NULL) {
            type = *(ow_vif->type);
        }
    }

    switch (type) {
        case OSW_VIF_UNDEFINED:
            break;
        case OSW_VIF_AP:
            if (vif_eligible) {
                ow_conf_conf_mutate_vif_ap(self, ow_phy, ow_vif, osw_vif);
            }
            break;
        case OSW_VIF_AP_VLAN:
            ow_conf_conf_mutate_vif_ap_vlan(self, ow_phy, ow_vif, osw_vif, vif_eligible);
            break;
        case OSW_VIF_STA:
            if (vif_eligible) {
                ow_conf_conf_mutate_vif_sta(ow_phy, ow_vif, osw_vif);
            }
            break;
    }
}

static void
ow_conf_conf_mutate_cb(struct osw_conf_mutator *mutator,
                       struct ds_tree *phy_tree)
{
    struct ow_conf *self = container_of(mutator, struct ow_conf, conf_mutator);
    struct osw_conf_phy *osw_phy;
    struct osw_conf_vif *osw_vif;

    /* This only mutates entities that already exist in the
     * base config given. The base config is derived from
     * osw_state. The assumption is that it's not possible
     * to create PHY and VIF out of thin air. All possible
     * interfaces that are ever to be configured must be
     * pre-allocated at the system integration level and
     * exposed by osw_drv implementation(s).
     */
    ds_tree_foreach(phy_tree, osw_phy) {
        ow_conf_conf_mutate_phy(self, osw_phy);

        ds_tree_foreach(&osw_phy->vif_tree, osw_vif) {
            ow_conf_conf_mutate_vif(self, osw_vif);
        }
    }
}

static struct ow_conf g_ow_conf = {
    .phy_tree = DS_TREE_INIT(ds_str_cmp, struct ow_conf_phy, node),
    .vif_tree = DS_TREE_INIT(ds_str_cmp, struct ow_conf_vif, node),
    .rad_tree = DS_TREE_INIT(ds_str_cmp, struct ow_conf_radius, node),
    .passpoint_tree = DS_TREE_INIT(ds_str_cmp, struct ow_conf_passpoint, node),
    .obs_list = DS_DLIST_INIT(struct ow_conf_observer, node),
    .conf_mutator = {
        .name ="ow_conf",
        .mutate_fn = ow_conf_conf_mutate_cb,
        .type = OSW_CONF_HEAD,
    },
};

void
ow_conf_ap_vlan_set_enabled(const bool *enabled)
{
    struct ow_conf *self = &g_ow_conf;

    const bool setting = (self->ap_vlan_enabled == NULL)
                      && (enabled != NULL);
    const bool unsetting = (self->ap_vlan_enabled != NULL)
                        && (enabled == NULL);
    const bool changing = (self->ap_vlan_enabled != NULL)
                       && (enabled != NULL)
                       && (*self->ap_vlan_enabled != *enabled);

    if (setting) {
        LOGI("ow: conf: ap_vlan_enabled set to %d",
              *enabled);
    }
    else if (unsetting) {
        LOGI("ow: conf: ap_vlan_enabled unset from %d",
             *self->ap_vlan_enabled);
    }
    else if (changing) {
        LOGI("ow: conf: ap_vlan_enabled changed from %d to %d",
             *self->ap_vlan_enabled,
             *enabled);
    }

    FREE(self->ap_vlan_enabled);
    self->ap_vlan_enabled = NULL;
    if (enabled != NULL) {
        self->ap_vlan_enabled = MEMNDUP(enabled, sizeof(*enabled));
    }

    const bool invalidated = (setting || unsetting || changing);
    if (invalidated) {
        osw_conf_invalidate(&self->conf_mutator);
    }
}

void
ow_conf_register_observer(struct ow_conf_observer *obs)
{
    struct ow_conf *self = &g_ow_conf;

    LOGI("ow: conf: registering observer: name=%s", obs->name);
    ds_dlist_insert_tail(&self->obs_list, obs);
}

static void
ow_conf_phy_notify_changed(const char *phy_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_observer *obs;

    ds_dlist_foreach(&self->obs_list, obs)
        if (obs->phy_changed_fn != NULL)
            obs->phy_changed_fn(obs, phy_name);
}

static void
ow_conf_vif_notify_changed(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_observer *obs;

    ds_dlist_foreach(&self->obs_list, obs)
        if (obs->vif_changed_fn != NULL)
            obs->vif_changed_fn(obs, vif_name);
}

static void
ow_conf_passpoint_notify_changed(const char *ref_id)
{
    /* Function is required to satisfy a macro DEFINE_FIELD
     * that is used to define passpoint structure fields, but
     * it is not used */
}

const char *
ow_conf_vif_get_ap_passpoint_ref(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get_ro(self, vif_name);
    return vif != NULL ? vif->ap_passpoint_ref : NULL;
}

void
ow_conf_vif_set_ap_passpoint_ref(const char *vif_name,
                                 const char *ref_id)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    const bool changed = ((vif->ap_passpoint_ref == NULL) ||
                          (vif->ap_passpoint_ref != NULL && ref_id == NULL) ||
                          (strcmp(vif->ap_passpoint_ref, ref_id) != 0));

    if (changed == true && vif->ap_passpoint_ref == NULL && ref_id != NULL) {
        LOGI("ow: conf: %s: ref_id set to %s",
             vif_name, ref_id);
    }

    if (changed == true && vif->ap_passpoint_ref != NULL && ref_id == NULL) {
        LOGI("ow: conf: %s: ref_id unset from %s",
             vif_name, vif->ap_passpoint_ref);
    }

    if (changed == true && vif->ap_passpoint_ref != NULL && ref_id != NULL) {
        LOGI("ow: conf: %s: ref_id changed from %s from %s",
             vif_name, vif->ap_passpoint_ref, ref_id);
    }

    FREE(vif->ap_passpoint_ref);
    vif->ap_passpoint_ref = ref_id ? STRDUP(ref_id) : NULL;
    if (changed) {
        osw_conf_invalidate(&self->conf_mutator);
        ow_conf_vif_notify_changed(vif_name);
    }
}

void
ow_conf_phy_unset(const char *phy_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_phy *phy = ow_conf_phy_get(self, phy_name);
    ds_tree_remove(&self->phy_tree, phy);
    FREE(phy->phy_name);
    FREE(phy->enabled);
    FREE(phy->ap_wmm_enabled);
    FREE(phy->ap_ht_enabled);
    FREE(phy->ap_vht_enabled);
    FREE(phy->ap_he_enabled);
    FREE(phy->ap_eht_enabled);
    FREE(phy->tx_chainmask);
    FREE(phy->tx_power_dbm);
    FREE(phy->thermal_tx_chainmask);
    FREE(phy->ap_beacon_interval_tu);
    FREE(phy->ap_channel);
    FREE(phy->ap_supp_rates);
    FREE(phy->ap_basic_rates);
    FREE(phy->ap_beacon_rate);
    FREE(phy->ap_mcast_rate);
    FREE(phy->ap_mgmt_rate);
    FREE(phy);
}

bool
ow_conf_phy_is_set(const char *phy_name)
{
    struct ow_conf *self = &g_ow_conf;
    return ds_tree_find(&self->phy_tree, phy_name) == NULL ? false : true;
}

void
ow_conf_vif_set_phy_name(const char *vif_name,
                         const char *phy_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    bool changed = ((vif->phy_name == NULL) ||
                    (vif->phy_name != NULL && phy_name == NULL) ||
                    (strcmp(vif->phy_name, phy_name) != 0));

    if (changed == true && vif->phy_name == NULL && phy_name != NULL) {
        LOGI("ow: conf: %s: phy_name set to %s",
             vif_name, phy_name);
    }

    if (changed == true && vif->phy_name != NULL && phy_name == NULL) {
        LOGI("ow: conf: %s: phy_name unset from %s",
             vif_name, vif->phy_name);
    }

    if (changed == true && vif->phy_name != NULL && phy_name != NULL) {
        LOGI("ow: conf: %s: phy_name changed from %s from %s",
             vif_name, vif->phy_name, phy_name);
    }

    FREE(vif->phy_name);
    vif->phy_name = phy_name ? STRDUP(phy_name) : NULL;
    if (changed) {
        osw_conf_invalidate(&self->conf_mutator);
        ow_conf_vif_notify_changed(vif_name);
    }
}

const char *
ow_conf_vif_get_phy_name(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get_ro(self, vif_name);
    return vif != NULL ? vif->phy_name : NULL;
}

bool
ow_conf_vif_has_ap_acl(const char *vif_name, const struct osw_hwaddr *addr)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get_ro(self, vif_name);
    if (vif == NULL) return false;
    struct ow_conf_acl *acl = ds_tree_find(&vif->ap_acl_tree, addr);
    if (acl == NULL) return false;
    return true;
}

void
ow_conf_vif_set_ap_psk(const char *vif_name,
                       int key_id,
                       const char *str)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_psk *psk = ds_tree_find(&vif->ap_psk_tree, &key_id);

    if (psk == NULL) {
        psk = CALLOC(1, sizeof(*psk));
        psk->ap_psk.key_id = key_id;
        ds_tree_insert(&vif->ap_psk_tree, psk, &psk->ap_psk.key_id);
        LOGI("ow: conf: %s: psk: adding: key_id=%d", vif_name, key_id);
    }

    if (str != NULL)
        STRSCPY_WARN(psk->ap_psk.psk.str, str);

    if (str == NULL) {
        LOGI("ow: conf: %s: psk: removing: key_id=%d", vif_name, key_id);
        ds_tree_remove(&vif->ap_psk_tree, psk);
        FREE(psk);
    }
    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

void
ow_conf_vif_flush_ap_psk(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_psk *psk;

    while ((psk = ds_tree_head(&vif->ap_psk_tree)) != NULL)
        ow_conf_vif_set_ap_psk(vif_name, psk->ap_psk.key_id, NULL);
}

const char *
ow_conf_vif_get_ap_psk(const char *vif_name,
                       int key_id)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_psk *psk = ds_tree_find(&vif->ap_psk_tree, &key_id);
    return psk ? psk->ap_psk.psk.str : NULL;
}

void
ow_conf_vif_add_ap_acl(const char *vif_name,
                       const struct osw_hwaddr *addr)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_acl *acl = ds_tree_find(&vif->ap_acl_tree, addr);

    if (acl != NULL) return;

    LOGI("ow: conf: %s: acl: adding: " OSW_HWADDR_FMT,
         vif_name, OSW_HWADDR_ARG(addr));

    acl = MALLOC(sizeof(*acl));
    memcpy(&acl->mac_addr, addr, sizeof(*addr));
    ds_tree_insert(&vif->ap_acl_tree, acl, &acl->mac_addr);
    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

void
ow_conf_vif_del_ap_acl(const char *vif_name,
                       const struct osw_hwaddr *addr)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_acl *acl = ds_tree_find(&vif->ap_acl_tree, addr);

    if (acl == NULL) return;

    LOGI("ow: conf: %s: acl: removing: " OSW_HWADDR_FMT,
         vif_name, OSW_HWADDR_ARG(addr));

    ds_tree_remove(&vif->ap_acl_tree, acl);
    FREE(acl);
    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

void
ow_conf_vif_flush_ap_acl(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_acl *acl;

    while ((acl = ds_tree_head(&vif->ap_acl_tree)) != NULL)
        ow_conf_vif_del_ap_acl(vif_name, &acl->mac_addr);
}

void
ow_conf_vif_set_ap_neigh(const char *vif_name,
                         const struct osw_hwaddr *bssid,
                         const uint32_t bssid_info,
                         const uint8_t op_class,
                         const uint8_t channel,
                         const uint8_t phy_type)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_neigh *neigh = ds_tree_find(&vif->ap_neigh_tree, bssid);

    if (neigh == NULL) {
        neigh = CALLOC(1, sizeof(*neigh));
        neigh->neigh.bssid = *bssid;
        ds_tree_insert(&vif->ap_neigh_tree, neigh, &neigh->neigh.bssid);

        LOGI("ow: conf: %s: neigh: adding new neighbor"
             " bssid: "OSW_HWADDR_FMT
             " bssid_info: %02x%02x%02x%02x"
             " op_class: %u"
             " channel: %u"
             " phy_type: %u",
             vif_name,
             OSW_HWADDR_ARG(bssid),
             (bssid_info & 0xff000000) >> 24,
             (bssid_info & 0x00ff0000) >> 16,
             (bssid_info & 0x0000ff00) >> 8,
             (bssid_info & 0x000000ff),
             op_class,
             channel,
             phy_type);
    }
    else {
        char logbuf[1024] = {0};
        char *out = logbuf;
        size_t len = sizeof(logbuf);

        csnprintf(&out,
                  &len,
                  "ow: conf: %s: neigh: updating neighbor"
                  " bssid: "OSW_HWADDR_FMT,
                  vif_name,
                  OSW_HWADDR_ARG(bssid));

        if (neigh->neigh.bssid_info != bssid_info) {
            csnprintf(&out,
                      &len,
                      " bssid_info: %02x%02x%02x%02x"
                      " -> %02x%02x%02x%02x",
                      (neigh->neigh.bssid_info & 0xff000000) >> 24,
                      (neigh->neigh.bssid_info & 0x00ff0000) >> 16,
                      (neigh->neigh.bssid_info & 0x0000ff00) >> 8,
                      (neigh->neigh.bssid_info & 0x000000ff),
                      (bssid_info & 0xff000000) >> 24,
                      (bssid_info & 0x00ff0000) >> 16,
                      (bssid_info & 0x0000ff00) >> 8,
                      (bssid_info & 0x000000ff));
        }

        if (neigh->neigh.op_class != op_class) {
            csnprintf(&out,
                      &len,
                      " op_class: %u"
                      " -> %u",
                      neigh->neigh.op_class,
                      op_class);
        }
        if (neigh->neigh.channel != channel) {
            csnprintf(&out,
                      &len,
                      " channel: %u"
                      " -> %u",
                      neigh->neigh.channel,
                      channel);
        }
        if (neigh->neigh.phy_type != phy_type) {
            csnprintf(&out,
                      &len,
                      " phy_type: %u"
                      " -> %u",
                      neigh->neigh.phy_type,
                      phy_type);
        }

        LOGI("%s",logbuf);
    }

    neigh->neigh.bssid_info = bssid_info;
    neigh->neigh.op_class = op_class;
    neigh->neigh.channel = channel;
    neigh->neigh.phy_type = phy_type;

    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

void ow_conf_vif_set_ap_neigh_ft(const char *vif_name,
                                 const struct osw_hwaddr *bssid,
                                 const bool ft_enabled,
                                 const char *ft_encr_key,
                                 const char *nas_id)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *self_vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_vif *vif;
    bool ft_encr_key_changed = false;
    bool nas_id_changed = false;
    if (!ft_enabled) return;

    /* All FT neighbors have to be present in each .rxkhs file
     * This includes neighbors from different bands
    */

    ds_tree_foreach(&self->vif_tree, vif) {
        if (!ow_conf_is_ft_set(vif)) continue;

        /* Only add neighbors from the interfaces with the same ssid */
        if (strcmp(self_vif->ap_ssid->buf, vif->ap_ssid->buf) != 0) continue;

        struct ow_conf_neigh_ft *neigh_ft = ds_tree_find(&vif->ap_neigh_ft_tree, bssid);

        if (neigh_ft == NULL) {
            neigh_ft = CALLOC(1, sizeof(*neigh_ft));
            neigh_ft->neigh_ft.bssid = *bssid;
            ds_tree_insert(&vif->ap_neigh_ft_tree, neigh_ft, &neigh_ft->neigh_ft.bssid);

            LOGI("ow: conf: %s: neigh_ft: adding new neighbor fast transition to"
                " bssid: "OSW_HWADDR_FMT
                " ft_encr_key: %s"
                " nas_id: %s",
                vif->vif_name,
                OSW_HWADDR_ARG(bssid),
                ft_encr_key,
                nas_id);

            ft_encr_key_changed = true;
            nas_id_changed = true;
        }
        else {
            char logbuf[1024] = {0};
            char *out = logbuf;
            size_t len = sizeof(logbuf);

            ft_encr_key_changed = (strcmp(neigh_ft->neigh_ft.ft_encr_key.buf, ft_encr_key) != 0);
            nas_id_changed = (strcmp(neigh_ft->neigh_ft.nas_identifier.buf, nas_id) != 0);

            if (ft_encr_key_changed || nas_id_changed) {
                csnprintf(&out,
                        &len,
                        "ow: conf: %s: neigh: updating neighbor fast transition"
                        " bssid: "OSW_HWADDR_FMT,
                        vif->vif_name,
                        OSW_HWADDR_ARG(bssid));
            }

            if (ft_encr_key_changed) {
                csnprintf(&out,
                        &len,
                        " ft_encr_key: "OSW_FT_ENCR_KEY_FMT
                        " -> %s",
                        OSW_FT_ENCR_KEY_ARG(&neigh_ft->neigh_ft.ft_encr_key),
                        ft_encr_key);
            }

            if (nas_id_changed) {
                csnprintf(&out,
                        &len,
                        " nas_id: "OSW_NAS_ID_FMT
                        " -> %s",
                        OSW_NAS_ID_ARG(&neigh_ft->neigh_ft.nas_identifier),
                        nas_id);
            }

            LOGI("%s",logbuf);
        }

        STRSCPY_WARN(neigh_ft->neigh_ft.ft_encr_key.buf, ft_encr_key);
        STRSCPY_WARN(neigh_ft->neigh_ft.nas_identifier.buf, nas_id);

        if (ft_encr_key_changed || nas_id_changed) {
            osw_conf_invalidate(&self->conf_mutator);
            ow_conf_vif_notify_changed(vif->vif_name);
        }
    }
}

void
ow_conf_vif_del_ap_neigh(const char *vif_name,
                         const struct osw_hwaddr *bssid)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_neigh *neigh = ds_tree_find(&vif->ap_neigh_tree, bssid);

    if (neigh == NULL) return;

    LOGI("ow: conf: %s: neigh: removing neighbor"
         " bssid: "OSW_HWADDR_FMT,
         vif_name,
         OSW_HWADDR_ARG(bssid));

    ds_tree_remove(&vif->ap_neigh_tree, neigh);
    FREE(neigh);
    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

void
ow_conf_vif_del_ap_neigh_ft(const char *vif_name,
                         const struct osw_hwaddr *bssid)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_neigh *neigh_ft = ds_tree_find(&vif->ap_neigh_ft_tree, bssid);

    if (neigh_ft == NULL) return;

    LOGI("ow: conf: %s: neigh: removing neighbor fast transition"
         " bssid: "OSW_HWADDR_FMT,
         vif_name,
         OSW_HWADDR_ARG(bssid));

    ds_tree_remove(&vif->ap_neigh_ft_tree, neigh_ft);
    FREE(neigh_ft);
    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

void
ow_conf_vif_flush_ap_neigh(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_neigh *neigh;

    while ((neigh = ds_tree_head(&vif->ap_neigh_tree)) != NULL)
        ow_conf_vif_del_ap_neigh(vif_name, &neigh->neigh.bssid);
}

void
ow_conf_vif_flush_ap_neigh_ft(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_neigh_ft *neigh_ft;

    while ((neigh_ft = ds_tree_head(&vif->ap_neigh_ft_tree)) != NULL)
        ow_conf_vif_del_ap_neigh_ft(vif_name, &neigh_ft->neigh_ft.bssid);
}


static void
ow_conf_radius_ref_free(struct ow_conf_radius_ref *ref)
{
    FREE(ref->ref_id);
    FREE(ref);
}

static void
ow_conf_radius_free(struct ow_conf_radius *rad)
{
    FREE(rad->radius.uuid);
    FREE(rad->radius.server);
    FREE(rad->radius.passphrase);
    FREE(rad);
}

void
ow_conf_radius_unset(const char *ref_id)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_radius *rad;

    if ((rad = ds_tree_find(&self->rad_tree, ref_id)) != NULL) {
        ds_tree_remove(&self->rad_tree, rad);
        ow_conf_radius_free(rad);
    }
}

void
ow_conf_radius_flush(void)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_radius *rad;

    while ((rad = ds_tree_remove_head(&self->rad_tree)) != NULL) {
        ow_conf_radius_free(rad);
    }
}

static struct ow_conf_radius*
ow_conf_radius_get(const char *ref_id)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_radius *rad = ds_tree_find(&self->rad_tree, ref_id);
    if (rad == NULL) {
        rad = CALLOC(1, sizeof(*rad));
        rad->radius.uuid = STRDUP(ref_id);
        ds_tree_insert(&self->rad_tree, rad, rad->radius.uuid);
    }
    return rad;
}

void
ow_conf_radius_set_ip_addr(const char *ref_id,
                           const char *ip_addr)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_radius *rad = ow_conf_radius_get(ref_id);
    FREE(rad->radius.server);
    rad->radius.server = ip_addr != NULL ? STRDUP(ip_addr) : NULL;
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_radius_set_secret(const char *ref_id,
                          const char *secret)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_radius *rad = ow_conf_radius_get(ref_id);
    FREE(rad->radius.passphrase);
    rad->radius.passphrase = secret != NULL ? STRDUP(secret) : NULL;
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_radius_set_port(const char *ref_id,
                        uint16_t port)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_radius *rad = ow_conf_radius_get(ref_id);
    rad->radius.port = port;
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_vif_add_radius_ref(const char *vif_name,
                           const char *ref_id)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_radius_ref *ref = CALLOC(1, sizeof(*ref));

    ref->ref_id = STRDUP(ref_id);
    ds_dlist_insert_tail(&vif->ap_radius_list, ref);

    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

void
ow_conf_vif_flush_radius_refs(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_radius_ref *ref;

    if (ds_dlist_is_empty(&vif->ap_radius_list)) return;

    while ((ref = ds_dlist_remove_tail(&vif->ap_radius_list)) != NULL) {
        ow_conf_radius_ref_free(ref);
    }
    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

void
ow_conf_vif_add_radius_accounting_ref(const char *vif_name,
                                      const char *ref_id)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_radius_ref *ref = CALLOC(1, sizeof(*ref));

    ref->ref_id = STRDUP(ref_id);
    ds_dlist_insert_tail(&vif->ap_acct_list, ref);

    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

void
ow_conf_vif_flush_radius_accounting_refs(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_radius_ref *ref;

    if (ds_dlist_is_empty(&vif->ap_acct_list)) return;

    while ((ref = ds_dlist_remove_tail(&vif->ap_acct_list)) != NULL) {
        ow_conf_radius_ref_free(ref);
    }
    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

static struct ow_conf_passpoint *
ow_conf_passpoint_alloc(struct ow_conf *self, const char *ref_id)
{
    struct ow_conf_passpoint *passp = CALLOC(1, sizeof(*passp));
    passp->uuid = STRDUP(ref_id);
    ds_tree_insert(&self->passpoint_tree, passp, passp->uuid);
    return passp;
}

static struct ow_conf_passpoint *
ow_conf_passpoint_get(struct ow_conf *self, const char *ref_id)
{
    return ds_tree_find(&self->passpoint_tree, ref_id) ?: ow_conf_passpoint_alloc(self, ref_id);
}

static struct ow_conf_passpoint *
ow_conf_passpoint_get_ro(struct ow_conf *self, const char *ref_id)
{
    return ds_tree_find(&self->passpoint_tree, ref_id);
}

void
ow_conf_passpoint_set_hessid(const char *ref_id,
                             const char *hessid)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    if (hessid != NULL) {
        WARN_ON(osw_hwaddr_from_cstr(hessid, &p->hessid) == false);
    } else {
        MEMZERO(p->hessid);
    }
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_osu_ssid(const char *ref_id,
                               const char *osu_ssid)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    if (osu_ssid != NULL) {
        STRSCPY_WARN(p->osu_ssid.buf, osu_ssid);
        p->osu_ssid.len = strlen(osu_ssid);
    } else {
        MEMZERO(p->osu_ssid);
    }
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_t_c_filename(const char *ref_id,
                                   const char *t_c_filename)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    FREE(p->t_c_filename);
    p->t_c_filename = t_c_filename ? STRDUP(t_c_filename) : NULL;

    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_anqp_elem(const char *ref_id,
                                const char *anqp_elem)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    FREE(p->anqp_elem);
    p->anqp_elem = anqp_elem ? STRDUP(anqp_elem) : NULL;

    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_domain_list(const char *ref_id,
                                  char **domain_list,
                                  const int domain_list_len)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    str_array_free(p->domain_list, p->domain_list_len);
    p->domain_list = NULL;
    p->domain_list_len = 0;

    if (domain_list != NULL && domain_list_len > 0) {
        p->domain_list = domain_list;
        p->domain_list_len = domain_list_len;
    }
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_nairealm_list(const char *ref_id,
                                  char **nairealm_list,
                                  const int nairealm_list_len)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    str_array_free(p->nairealm_list, p->nairealm_list_len);
    p->nairealm_list = NULL;
    p->nairealm_list_len = 0;

    if (nairealm_list != NULL && nairealm_list_len > 0) {
        p->nairealm_list = nairealm_list;
        p->nairealm_list_len = nairealm_list_len;
    }
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_roamc(const char *ref_id,
                            char **roamc_list,
                            const int roamc_list_len)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    str_array_free(p->roamc_list, p->roamc_list_len);
    p->roamc_list = NULL;
    p->roamc_list_len = 0;

    if (roamc_list != NULL && roamc_list_len > 0) {
        p->roamc_list = roamc_list;
        p->roamc_list_len = roamc_list_len;
    }
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_oper_fname_list(const char *ref_id,
                                      char **oper_fname_list,
                                      const int oper_fname_list_len)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    str_array_free(p->oper_fname_list, p->oper_fname_list_len);
    p->oper_fname_list = NULL;
    p->oper_fname_list_len = 0;
    if (oper_fname_list != NULL && oper_fname_list_len > 0) {
        p->oper_fname_list = oper_fname_list;
        p->oper_fname_list_len = oper_fname_list_len;
    }
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_venue_name_list(const char *ref_id,
                                      char **venue_name_list,
                                      const int venue_name_list_len)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    str_array_free(p->venue_name_list, p->venue_name_list_len);
    p->venue_name_list = NULL;
    p->venue_name_list_len = 0;
    if (venue_name_list != NULL && venue_name_list_len > 0) {
        p->venue_name_list = venue_name_list;
        p->venue_name_list_len = venue_name_list_len;
    }
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_venue_url_list(const char *ref_id,
                                      char **venue_url_list,
                                      const int venue_url_list_len)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    str_array_free(p->venue_url_list, p->venue_url_list_len);
    p->venue_url_list = NULL;
    p->venue_url_list_len = 0;
    if (venue_url_list != NULL && venue_url_list_len > 0) {
        p->venue_url_list = venue_url_list;
        p->venue_url_list_len = venue_url_list_len;
    }
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_list_3gpp_list(const char *ref_id,
                                      char **list_3gpp_list,
                                      const int list_3gpp_list_len)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    str_array_free(p->list_3gpp_list, p->list_3gpp_list_len);
    p->list_3gpp_list = NULL;
    p->list_3gpp_list_len = 0;
    if (list_3gpp_list != NULL && list_3gpp_list_len > 0) {
        p->list_3gpp_list = list_3gpp_list;
        p->list_3gpp_list_len = list_3gpp_list_len;
    }
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_set_net_auth_type_list(const char *ref_id,
                                      const int *net_auth_type_list,
                                      const int net_auth_type_list_len)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *p = ow_conf_passpoint_get(self, ref_id);

    FREE(p->net_auth_type_list);
    p->net_auth_type_list = NULL;
    p->net_auth_type_list_len = 0;
    if (net_auth_type_list != NULL && net_auth_type_list_len > 0) {
        p->net_auth_type_list = MEMNDUP(net_auth_type_list,
                                        sizeof(*net_auth_type_list) * net_auth_type_list_len);
        p->net_auth_type_list_len = net_auth_type_list_len;
    }
    osw_conf_invalidate(&self->conf_mutator);
}

void
ow_conf_passpoint_unset(char *ref_id)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_passpoint *passpoint = ow_conf_passpoint_get_ro(self, ref_id);
    if (passpoint == NULL) return;

    ds_tree_remove(&self->passpoint_tree, passpoint);
    FREE(passpoint->uuid);
    FREE(passpoint->hs20_enabled);
    FREE(passpoint->adv_wan_status);
    FREE(passpoint->adv_wan_symmetric);
    FREE(passpoint->adv_wan_at_capacity);
    FREE(passpoint->osen);
    FREE(passpoint->asra);
    FREE(passpoint->ant);
    FREE(passpoint->venue_group);
    FREE(passpoint->venue_type);
    FREE(passpoint->anqp_domain_id);
    FREE(passpoint->pps_mo_id);
    FREE(passpoint->t_c_timestamp);
    FREE(passpoint->t_c_filename);
    FREE(passpoint->anqp_elem);
    str_array_free(passpoint->domain_list, passpoint->domain_list_len);
    str_array_free(passpoint->nairealm_list, passpoint->nairealm_list_len);
    str_array_free(passpoint->roamc_list, passpoint->roamc_list_len);
    str_array_free(passpoint->oper_fname_list, passpoint->oper_fname_list_len);
    str_array_free(passpoint->venue_name_list, passpoint->venue_name_list_len);
    str_array_free(passpoint->venue_url_list, passpoint->venue_url_list_len);
    str_array_free(passpoint->list_3gpp_list, passpoint->list_3gpp_list_len);
    FREE(passpoint->net_auth_type_list);
    FREE(passpoint);

    osw_conf_invalidate(&self->conf_mutator);
}

static struct ow_conf_net *
ow_conf_sta_vif_lookup_sta_net(struct ds_tree *sta_net_tree,
                               const struct osw_ssid *ssid,
                               const struct osw_hwaddr *bssid)
{
    struct ow_conf_net tmp_net;
    memcpy(&tmp_net.ssid, ssid, sizeof(*ssid));
    memcpy(&tmp_net.bssid, bssid, sizeof(*bssid));

    return ds_tree_find(sta_net_tree, &tmp_net);
}

void
ow_conf_vif_set_sta_net(const char *vif_name,
                        const struct osw_ssid *ssid,
                        const struct osw_hwaddr *bssid,
                        const struct osw_psk *psk,
                        const struct osw_wpa *wpa,
                        const struct osw_ifname *bridge_if_name,
                        const bool *multi_ap,
                        const int *priority)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_net *net = ow_conf_sta_vif_lookup_sta_net(&vif->sta_net_tree, ssid, bssid);

    if (net == NULL) {
        net = CALLOC(1, sizeof(*net));
        memcpy(&net->ssid, ssid, sizeof(*ssid));
        memcpy(&net->bssid, bssid, sizeof(*bssid));
        ds_tree_insert(&vif->sta_net_tree, net, net);
        LOGI("ow: conf: %s: net: adding: ssid=" OSW_SSID_FMT " bssid=" OSW_HWADDR_FMT, vif_name, OSW_SSID_ARG(&net->ssid), OSW_HWADDR_ARG(&net->bssid));
    }

    if (wpa != NULL) {
        const struct osw_psk prev_psk = net->psk;
        const struct osw_wpa prev_wpa = net->wpa;
        const struct osw_ifname prev_bridge_if_name = net->bridge_if_name;
        const bool prev_multi_ap = net->multi_ap;
        const int prev_priority = net->priority;

        memset(&net->psk, 0, sizeof(net->psk));
        if (psk != NULL) memcpy(&net->psk, psk, sizeof(*psk));
        if (bridge_if_name != NULL) memcpy(&net->bridge_if_name, bridge_if_name, sizeof(*bridge_if_name));
        if (multi_ap != NULL) net->multi_ap = *multi_ap;
        if (priority != NULL) net->priority = *priority;
        memcpy(&net->wpa, wpa, sizeof(*wpa));
        net->bridge_if_name.buf[sizeof(net->bridge_if_name.buf) - 1] = '\0';

        const bool psk_changed = (memcmp(&prev_psk, &net->psk, sizeof(prev_psk)) != 0);
        const bool wpa_changed = (memcmp(&prev_wpa, &net->wpa, sizeof(prev_wpa)) != 0);
        const bool bridge_changed = (memcmp(&prev_bridge_if_name, &net->bridge_if_name, sizeof(prev_bridge_if_name)) != 0);
        const bool multi_ap_changed = (prev_multi_ap != net->multi_ap);
        const bool priority_changed = (prev_priority != net->priority);

        if (psk_changed) {
            const size_t old_len = strnlen(prev_psk.str, sizeof(prev_psk.str));
            const size_t new_len = strnlen(net->psk.str, sizeof(net->psk.str));
            LOGI("ow: conf: %s: net: " OSW_SSID_FMT": psk=%zu -> %zu",
                 vif_name,
                 OSW_SSID_ARG(&net->ssid),
                 old_len,
                 new_len);
        }

        if (wpa_changed) {
            char old_wpa[512];
            char new_wpa[512];
            osw_wpa_to_str(old_wpa, sizeof(old_wpa), &prev_wpa);
            osw_wpa_to_str(new_wpa, sizeof(new_wpa), &net->wpa);
            LOGI("ow: conf: %s: net: " OSW_SSID_FMT": wpa=%s -> %s",
                 vif_name,
                 OSW_SSID_ARG(&net->ssid),
                 old_wpa,
                 new_wpa);
        }

        if (bridge_changed) {
            LOGI("ow: conf: %s: net: " OSW_SSID_FMT": bridge_if_name=%s -> %s",
                 vif_name,
                 OSW_SSID_ARG(&net->ssid),
                 prev_bridge_if_name.buf,
                 net->bridge_if_name.buf);
        }

        if (multi_ap_changed) {
            LOGI("ow: conf: %s: net: " OSW_SSID_FMT": multi_ap=%d -> %d",
                 vif_name,
                 OSW_SSID_ARG(&net->ssid),
                 prev_multi_ap,
                 net->multi_ap);
        }
        if (priority_changed) {
            LOGI("ow: conf: %s: net: " OSW_SSID_FMT": priority=%d -> %d",
                 vif_name,
                 OSW_SSID_ARG(&net->ssid),
                 prev_priority,
                 net->priority);
        }

    }

    if (wpa == NULL) {
        LOGI("ow: conf: %s: net: removing: ssid=" OSW_SSID_FMT, vif_name, OSW_SSID_ARG(&net->ssid));
        ds_tree_remove(&vif->sta_net_tree, net);
        FREE(net);
        net = NULL;
    }

    osw_conf_invalidate(&self->conf_mutator);
    ow_conf_vif_notify_changed(vif_name);
}

void
ow_conf_vif_flush_sta_net(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get(self, vif_name);
    struct ow_conf_net *net;

    while ((net = ds_tree_head(&vif->sta_net_tree)) != NULL)
        ow_conf_vif_set_sta_net(vif_name, &net->ssid, &net->bssid, NULL, NULL, NULL, NULL, &net->priority);
}


#define DEFINE_FIELD(member, type, lookup_type, lookup, fn_set, fn_get, fn_notify, fmt, arg) \
    void fn_set(const char *key, const type *v) { \
        osw_thread_sanity_check(); \
        lookup_type *obj = lookup(&g_ow_conf, key); \
        bool changed = (obj->member == NULL && v != NULL) || \
                       (obj->member != NULL && v == NULL) || \
                       (obj->member != NULL && v != NULL && memcmp(obj->member, v, sizeof(*v)) != 0); \
        if (changed == true && obj->member != NULL && v != NULL) LOGI("ow: conf: %s: %s changed from " fmt " to " fmt, key, #member, arg(*obj->member), arg(*v)); \
        if (changed == true && v == NULL) LOGI("ow: conf: %s: %s unset from " fmt, key, #member, arg(*obj->member)); \
        if (changed == true && obj->member == NULL) LOGI("ow: conf: %s: %s set to " fmt, key ?: "", #member, arg(*v)); \
        FREE(obj->member); \
        obj->member = NULL; \
        if (v != NULL) { \
            obj->member = MALLOC(sizeof(*v)); \
            memcpy(obj->member, v, sizeof(*v)); \
        } \
        if (changed) osw_conf_invalidate(&g_ow_conf.conf_mutator); \
        if (changed) fn_notify(key); \
    } \
    const type *fn_get(const char *key) { \
        osw_thread_sanity_check(); \
        lookup_type *obj = lookup##_ro(&g_ow_conf, key); \
        return obj ? obj->member : NULL; \
    }

#define FMT_nop "%s"
#define ARG_nop(x) ""

#define FMT_phy_enabled "%d"
#define ARG_phy_enabled(x) x
#define FMT_phy_tx_chainmask "0x%04x"
#define ARG_phy_tx_chainmask(x) x
#define FMT_phy_tx_power_dbm "%d"
#define ARG_phy_tx_power_dbm(x) x
#define FMT_phy_thermal_tx_chainmask "0x%04x"
#define ARG_phy_thermal_tx_chainmask(x) x
#define FMT_phy_ap_wmm_enabled "%d"
#define ARG_phy_ap_wmm_enabled(x) x
#define FMT_phy_ap_ht_enabled "%d"
#define ARG_phy_ap_ht_enabled(x) x
#define FMT_phy_ap_vht_enabled "%d"
#define ARG_phy_ap_vht_enabled(x) x
#define FMT_phy_ap_he_enabled "%d"
#define ARG_phy_ap_he_enabled(x) x
#define FMT_phy_ap_eht_enabled "%d"
#define ARG_phy_ap_eht_enabled(x) x
#define FMT_phy_ap_beacon_interval_tu "%d"
#define ARG_phy_ap_beacon_interval_tu(x) x
#define FMT_phy_ap_supp_rates "%04x"
#define ARG_phy_ap_supp_rates(x) x
#define FMT_phy_ap_basic_rates "%04x"
#define ARG_phy_ap_basic_rates(x) x
#define FMT_phy_ap_beacon_rate "%d kbps"
#define ARG_phy_ap_beacon_rate(x) (osw_rate_legacy_to_halfmbps(x) * 500)
#define FMT_phy_ap_mcast_rate "%d kbps"
#define ARG_phy_ap_mcast_rate(x) (osw_rate_legacy_to_halfmbps(x) * 500)
#define FMT_phy_ap_mgmt_rate "%d kbps"
#define ARG_phy_ap_mgmt_rate(x) (osw_rate_legacy_to_halfmbps(x) * 500)
#define FMT_phy_ap_channel OSW_CHANNEL_FMT
#define ARG_phy_ap_channel(x) OSW_CHANNEL_ARG(&(x))

#define FMT_vif_enabled "%d"
#define ARG_vif_enabled(x) x
#define FMT_vif_type "%s"
#define ARG_vif_type(x) ((x) == OSW_VIF_UNDEFINED ? "undefined" : \
                         (x) == OSW_VIF_AP ? "ap" : \
                         (x) == OSW_VIF_AP_VLAN ? "ap_vlan" : \
                         (x) == OSW_VIF_STA ? "sta" : \
                         "")
#define FMT_vif_tx_power_dbm "%d"
#define ARG_vif_tx_power_dbm(x) x
#define FMT_vif_ap_channel OSW_CHANNEL_FMT
#define ARG_vif_ap_channel(x) OSW_CHANNEL_ARG(&(x))
#define FMT_vif_ap_ssid OSW_SSID_FMT
#define ARG_vif_ap_ssid(x) OSW_SSID_ARG(&(x))
#define FMT_vif_ap_bridge_if_name "%s"
#define ARG_vif_ap_bridge_if_name(x) (x).buf
#define FMT_vif_ap_nas_identifier "%s"
#define ARG_vif_ap_nas_identifier(x) (x).buf
#define FMT_vif_ap_beacon_protection "%d"
#define ARG_vif_ap_beacon_protection(x) x
#define FMT_vif_ap_ssid_hidden "%d"
#define ARG_vif_ap_ssid_hidden(x) x
#define FMT_vif_ap_isolated "%d"
#define ARG_vif_ap_isolated(x) x
#define FMT_vif_ap_ht_enabled "%d"
#define ARG_vif_ap_ht_enabled(x) x
#define FMT_vif_ap_vht_enabled "%d"
#define ARG_vif_ap_vht_enabled(x) x
#define FMT_vif_ap_he_enabled "%d"
#define ARG_vif_ap_he_enabled(x) x
#define FMT_vif_ap_eht_enabled "%d"
#define ARG_vif_ap_eht_enabled(x) x
#define FMT_vif_ap_ht_required "%d"
#define ARG_vif_ap_ht_required(x) x
#define FMT_vif_ap_vht_required "%d"
#define ARG_vif_ap_vht_required(x) x
#define FMT_vif_ap_supp_rates "%04x"
#define ARG_vif_ap_supp_rates(x) x
#define FMT_vif_ap_basic_rates "%04x"
#define ARG_vif_ap_basic_rates(x) x
#define FMT_vif_ap_beacon_rate OSW_BEACON_RATE_FMT
#define ARG_vif_ap_beacon_rate(x) OSW_BEACON_RATE_ARG(&(x))
#define FMT_vif_ap_wpa "%d"
#define ARG_vif_ap_wpa(x) x
#define FMT_vif_ap_rsn "%d"
#define ARG_vif_ap_rsn(x) x
#define FMT_vif_ap_pairwise_tkip "%d"
#define ARG_vif_ap_pairwise_tkip(x) x
#define FMT_vif_ap_pairwise_ccmp "%d"
#define ARG_vif_ap_pairwise_ccmp(x) x
#define FMT_vif_ap_pairwise_ccmp256 "%d"
#define ARG_vif_ap_pairwise_ccmp256(x) x
#define FMT_vif_ap_pairwise_gcmp "%d"
#define ARG_vif_ap_pairwise_gcmp(x) x
#define FMT_vif_ap_pairwise_gcmp256 "%d"
#define ARG_vif_ap_pairwise_gcmp256(x) x
#define FMT_vif_ap_akm_eap "%d"
#define ARG_vif_ap_akm_eap(x) x
#define FMT_vif_ap_akm_eap_sha256 "%d"
#define ARG_vif_ap_akm_eap_sha256(x) x
#define FMT_vif_ap_akm_eap_sha384 "%d"
#define ARG_vif_ap_akm_eap_sha384(x) x
#define FMT_vif_ap_akm_eap_suite_b "%d"
#define ARG_vif_ap_akm_eap_suite_b(x) x
#define FMT_vif_ap_akm_eap_suite_b192 "%d"
#define ARG_vif_ap_akm_eap_suite_b192(x) x
#define FMT_vif_ap_akm_psk_sha256 "%d"
#define ARG_vif_ap_akm_psk_sha256(x) x
#define FMT_vif_ap_akm_psk "%d"
#define ARG_vif_ap_akm_psk(x) x
#define FMT_vif_ap_akm_sae "%d"
#define ARG_vif_ap_akm_sae(x) x
#define FMT_vif_ap_akm_sae_ext "%d"
#define ARG_vif_ap_akm_sae_ext(x) x
#define FMT_vif_ap_akm_ft_eap "%d"
#define ARG_vif_ap_akm_ft_eap(x) x
#define FMT_vif_ap_akm_ft_eap_sha384 "%d"
#define ARG_vif_ap_akm_ft_eap_sha384(x) x
#define FMT_vif_ap_akm_ft_psk "%d"
#define ARG_vif_ap_akm_ft_psk(x) x
#define FMT_vif_ap_akm_ft_sae "%d"
#define ARG_vif_ap_akm_ft_sae(x) x
#define FMT_vif_ap_akm_ft_sae_ext "%d"
#define ARG_vif_ap_akm_ft_sae_ext(x) x
#define FMT_vif_ap_pmf "%s"
#define ARG_vif_ap_pmf(x) ((x) == OSW_PMF_DISABLED ? "disabled" : \
                           (x) == OSW_PMF_OPTIONAL ? "optional" : \
                           (x) == OSW_PMF_REQUIRED ? "required" : \
                           "")
#define FMT_vif_ap_multi_ap "%s%s"
#define ARG_vif_ap_multi_ap(x) (x).backhaul_bss ? "back" : "", \
                               (x).fronthaul_bss ? "front" : ""
#define FMT_vif_ap_wps "%d"
#define ARG_vif_ap_wps(x) x
#define FMT_vif_ap_wmm "%d"
#define ARG_vif_ap_wmm(x) x
#define FMT_vif_ap_wmm_uapsd "%d"
#define ARG_vif_ap_wmm_uapsd(x) x
#define FMT_vif_ap_wnm_bss_trans "%d"
#define ARG_vif_ap_wnm_bss_trans(x) x
#define FMT_vif_ap_rrm_neighbor_report "%d"
#define ARG_vif_ap_rrm_neighbor_report(x) x
#define FMT_vif_ap_mcast2ucast "%d"
#define ARG_vif_ap_mcast2ucast(x) x
#define FMT_vif_ap_group_rekey_seconds "%d"
#define ARG_vif_ap_group_rekey_seconds(x) x
#define FMT_vif_ap_ft_mobility_domain "0x%04x"
#define ARG_vif_ap_ft_mobility_domain(x) x
#define FMT_vif_ap_ft_encr_key "%s"
#define ARG_vif_ap_ft_encr_key(x) (x).buf
#define FMT_vif_ap_ft_over_ds "%d"
#define ARG_vif_ap_ft_over_ds(x) x
#define FMT_vif_ap_ft_pmk_r1_push "%d"
#define ARG_vif_ap_ft_pmk_r1_push(x) x
#define FMT_vif_ap_ft_psk_generate_local "%d"
#define ARG_vif_ap_ft_psk_generate_local(x) x
#define FMT_vif_ap_ft_pmk_r0_key_lifetime_sec "%d"
#define ARG_vif_ap_ft_pmk_r0_key_lifetime_sec(x) x
#define FMT_vif_ap_ft_pmk_r1_max_key_lifetime_sec "%d"
#define ARG_vif_ap_ft_pmk_r1_max_key_lifetime_sec(x) x
#define FMT_vif_ap_beacon_interval_tu "%d"
#define ARG_vif_ap_beacon_interval_tu(x) x
#define FMT_vif_ap_acl_policy "%s"
#define ARG_vif_ap_acl_policy(x) ((x) == OSW_ACL_NONE ? "none" : \
                                  (x) == OSW_ACL_ALLOW_LIST ? "allow" : \
                                  (x) == OSW_ACL_DENY_LIST ? "deny" : \
                                  "undefined")

#define FMT_ap_passpoint_hessid OSW_SSID_FMT
#define ARG_ap_passpoint_hessid(x) OSW_SSID_ARG(&(x))
#define FMT_passpoint_hs20_enabled "%d"
#define ARG_passpoint_hs20_enabled(x) x
#define FMT_passpoint_adv_wan_status "%d"
#define ARG_passpoint_adv_wan_status(x) x
#define FMT_passpoint_adv_wan_symmetric "%d"
#define ARG_passpoint_adv_wan_symmetric(x) x
#define FMT_passpoint_adv_wan_at_capacity "%d"
#define ARG_passpoint_adv_wan_at_capacity(x) x
#define FMT_passpoint_osen "%d"
#define ARG_passpoint_osen(x) x
#define FMT_passpoint_asra "%d"
#define ARG_passpoint_asra(x) x
#define FMT_passpoint_ant "%d"
#define ARG_passpoint_ant(x) x
#define FMT_passpoint_venue_group "%d"
#define ARG_passpoint_venue_group(x) x
#define FMT_passpoint_venue_type "%d"
#define ARG_passpoint_venue_type(x) x
#define FMT_passpoint_anqp_domain_id "%d"
#define ARG_passpoint_anqp_domain_id(x) x
#define FMT_passpoint_pps_mo_id "%d"
#define ARG_passpoint_pps_mo_id(x) x
#define FMT_passpoint_t_c_timestamp "%d"
#define ARG_passpoint_t_c_timestamp(x) x

#define DEFINE_PHY_FIELD(name) \
    DEFINE_FIELD(name, \
                 typeof(*((struct ow_conf_phy *)NULL)->name), \
                 struct ow_conf_phy, \
                 ow_conf_phy_get, \
                 ow_conf_phy_set_##name, \
                 ow_conf_phy_get_##name, \
                 ow_conf_phy_notify_changed, \
                 FMT_phy_##name, ARG_phy_##name)

#define DEFINE_VIF_FIELD(name) \
    DEFINE_FIELD(name, \
                 typeof(*((struct ow_conf_vif *)NULL)->name), \
                 struct ow_conf_vif, \
                 ow_conf_vif_get, \
                 ow_conf_vif_set_##name, \
                 ow_conf_vif_get_##name, \
                 ow_conf_vif_notify_changed, \
                 FMT_vif_##name, ARG_vif_##name)

#define DEFINE_PASSPOINT_FIELD(name) \
    DEFINE_FIELD(name, \
                 typeof(*((struct ow_conf_passpoint *)NULL)->name), \
                 struct ow_conf_passpoint, \
                 ow_conf_passpoint_get, \
                 ow_conf_passpoint_set_##name, \
                 ow_conf_passpoint_get_##name, \
                 ow_conf_passpoint_notify_changed, \
                 FMT_passpoint_##name, ARG_passpoint_##name)

DEFINE_PASSPOINT_FIELD(hs20_enabled);
DEFINE_PASSPOINT_FIELD(adv_wan_status);
DEFINE_PASSPOINT_FIELD(adv_wan_symmetric);
DEFINE_PASSPOINT_FIELD(adv_wan_at_capacity);
DEFINE_PASSPOINT_FIELD(osen);
DEFINE_PASSPOINT_FIELD(asra);
DEFINE_PASSPOINT_FIELD(ant);
DEFINE_PASSPOINT_FIELD(venue_group);
DEFINE_PASSPOINT_FIELD(venue_type);
DEFINE_PASSPOINT_FIELD(anqp_domain_id);
DEFINE_PASSPOINT_FIELD(pps_mo_id);
DEFINE_PASSPOINT_FIELD(t_c_timestamp);

DEFINE_PHY_FIELD(enabled);
DEFINE_PHY_FIELD(tx_chainmask);
DEFINE_PHY_FIELD(tx_power_dbm);
DEFINE_PHY_FIELD(thermal_tx_chainmask);
DEFINE_PHY_FIELD(ap_wmm_enabled);
DEFINE_PHY_FIELD(ap_ht_enabled);
DEFINE_PHY_FIELD(ap_vht_enabled);
DEFINE_PHY_FIELD(ap_he_enabled);
DEFINE_PHY_FIELD(ap_eht_enabled);
DEFINE_PHY_FIELD(ap_beacon_interval_tu);
DEFINE_PHY_FIELD(ap_channel);
DEFINE_PHY_FIELD(ap_supp_rates);
DEFINE_PHY_FIELD(ap_basic_rates);
DEFINE_PHY_FIELD(ap_beacon_rate);
DEFINE_PHY_FIELD(ap_mcast_rate);
DEFINE_PHY_FIELD(ap_mgmt_rate);

DEFINE_VIF_FIELD(type);
DEFINE_VIF_FIELD(enabled);
DEFINE_VIF_FIELD(tx_power_dbm);

DEFINE_VIF_FIELD(ap_channel);
DEFINE_VIF_FIELD(ap_ssid);
DEFINE_VIF_FIELD(ap_bridge_if_name);
DEFINE_VIF_FIELD(ap_nas_identifier);
DEFINE_VIF_FIELD(ap_beacon_protection);
DEFINE_VIF_FIELD(ap_ssid_hidden);
DEFINE_VIF_FIELD(ap_isolated);
DEFINE_VIF_FIELD(ap_ht_enabled);
DEFINE_VIF_FIELD(ap_vht_enabled);
DEFINE_VIF_FIELD(ap_he_enabled);
DEFINE_VIF_FIELD(ap_eht_enabled);
DEFINE_VIF_FIELD(ap_ht_required);
DEFINE_VIF_FIELD(ap_vht_required);
DEFINE_VIF_FIELD(ap_supp_rates);
DEFINE_VIF_FIELD(ap_basic_rates);
DEFINE_VIF_FIELD(ap_beacon_rate);
DEFINE_VIF_FIELD(ap_wpa);
DEFINE_VIF_FIELD(ap_rsn);
DEFINE_VIF_FIELD(ap_pairwise_tkip);
DEFINE_VIF_FIELD(ap_pairwise_ccmp);
DEFINE_VIF_FIELD(ap_pairwise_ccmp256);
DEFINE_VIF_FIELD(ap_pairwise_gcmp);
DEFINE_VIF_FIELD(ap_pairwise_gcmp256);
DEFINE_VIF_FIELD(ap_akm_eap);
DEFINE_VIF_FIELD(ap_akm_eap_sha256);
DEFINE_VIF_FIELD(ap_akm_eap_sha384);
DEFINE_VIF_FIELD(ap_akm_eap_suite_b);
DEFINE_VIF_FIELD(ap_akm_eap_suite_b192);
DEFINE_VIF_FIELD(ap_akm_psk);
DEFINE_VIF_FIELD(ap_akm_psk_sha256);
DEFINE_VIF_FIELD(ap_akm_sae);
DEFINE_VIF_FIELD(ap_akm_sae_ext);
DEFINE_VIF_FIELD(ap_akm_ft_eap);
DEFINE_VIF_FIELD(ap_akm_ft_eap_sha384);
DEFINE_VIF_FIELD(ap_akm_ft_psk);
DEFINE_VIF_FIELD(ap_akm_ft_sae);
DEFINE_VIF_FIELD(ap_akm_ft_sae_ext);
DEFINE_VIF_FIELD(ap_group_rekey_seconds);
DEFINE_VIF_FIELD(ap_ft_mobility_domain);
DEFINE_VIF_FIELD(ap_ft_encr_key);
DEFINE_VIF_FIELD(ap_ft_over_ds);
DEFINE_VIF_FIELD(ap_ft_pmk_r1_push);
DEFINE_VIF_FIELD(ap_ft_psk_generate_local);
DEFINE_VIF_FIELD(ap_ft_pmk_r0_key_lifetime_sec);
DEFINE_VIF_FIELD(ap_ft_pmk_r1_max_key_lifetime_sec);
DEFINE_VIF_FIELD(ap_beacon_interval_tu);
DEFINE_VIF_FIELD(ap_pmf);
DEFINE_VIF_FIELD(ap_multi_ap);
DEFINE_VIF_FIELD(ap_acl_policy);
DEFINE_VIF_FIELD(ap_wps);
DEFINE_VIF_FIELD(ap_wmm);
DEFINE_VIF_FIELD(ap_wmm_uapsd);
DEFINE_VIF_FIELD(ap_wnm_bss_trans);
DEFINE_VIF_FIELD(ap_rrm_neighbor_report);
DEFINE_VIF_FIELD(ap_mcast2ucast);

void
ow_conf_vif_clear(const char *vif_name)
{
    ow_conf_vif_flush_ap_psk(vif_name);
    ow_conf_vif_flush_ap_acl(vif_name);
    ow_conf_vif_flush_ap_neigh(vif_name);
    ow_conf_vif_flush_ap_neigh_ft(vif_name);
    ow_conf_vif_flush_sta_net(vif_name);
    ow_conf_vif_flush_radius_refs(vif_name);
    ow_conf_vif_flush_radius_accounting_refs(vif_name);

    ow_conf_vif_set_type(vif_name, NULL);
    ow_conf_vif_set_tx_power_dbm(vif_name, NULL);
    ow_conf_vif_set_ap_channel(vif_name, NULL);
    ow_conf_vif_set_ap_ssid(vif_name, NULL);
    ow_conf_vif_set_ap_bridge_if_name(vif_name, NULL);
    ow_conf_vif_set_ap_nas_identifier(vif_name, NULL);
    ow_conf_vif_set_ap_beacon_protection(vif_name, NULL);
    ow_conf_vif_set_ap_ssid_hidden(vif_name, NULL);
    ow_conf_vif_set_ap_isolated(vif_name, NULL);
    ow_conf_vif_set_ap_ht_enabled(vif_name, NULL);
    ow_conf_vif_set_ap_vht_enabled(vif_name, NULL);
    ow_conf_vif_set_ap_he_enabled(vif_name, NULL);
    ow_conf_vif_set_ap_eht_enabled(vif_name, NULL);
    ow_conf_vif_set_ap_ht_required(vif_name, NULL);
    ow_conf_vif_set_ap_vht_required(vif_name, NULL);
    ow_conf_vif_set_ap_supp_rates(vif_name, NULL);
    ow_conf_vif_set_ap_basic_rates(vif_name, NULL);
    ow_conf_vif_set_ap_beacon_rate(vif_name, NULL);
    ow_conf_vif_set_ap_wpa(vif_name, NULL);
    ow_conf_vif_set_ap_rsn(vif_name, NULL);
    ow_conf_vif_set_ap_pairwise_tkip(vif_name, NULL);
    ow_conf_vif_set_ap_pairwise_ccmp(vif_name, NULL);
    ow_conf_vif_set_ap_pairwise_ccmp256(vif_name, NULL);
    ow_conf_vif_set_ap_pairwise_gcmp(vif_name, NULL);
    ow_conf_vif_set_ap_pairwise_gcmp256(vif_name, NULL);
    ow_conf_vif_set_ap_akm_eap(vif_name, NULL);
    ow_conf_vif_set_ap_akm_psk(vif_name, NULL);
    ow_conf_vif_set_ap_akm_sae(vif_name, NULL);
    ow_conf_vif_set_ap_akm_ft_eap(vif_name, NULL);
    ow_conf_vif_set_ap_akm_ft_psk(vif_name, NULL);
    ow_conf_vif_set_ap_akm_ft_sae(vif_name, NULL);
    ow_conf_vif_set_ap_group_rekey_seconds(vif_name, NULL);
    ow_conf_vif_set_ap_ft_mobility_domain(vif_name, NULL);
    ow_conf_vif_set_ap_beacon_interval_tu(vif_name, NULL);
    ow_conf_vif_set_ap_pmf(vif_name, NULL);
    ow_conf_vif_set_ap_multi_ap(vif_name, NULL);
    ow_conf_vif_set_ap_acl_policy(vif_name, NULL);
    ow_conf_vif_set_ap_wps(vif_name, NULL);
    ow_conf_vif_set_ap_wmm(vif_name, NULL);
    ow_conf_vif_set_ap_wmm_uapsd(vif_name, NULL);
    ow_conf_vif_set_ap_wnm_bss_trans(vif_name, NULL);
    ow_conf_vif_set_ap_rrm_neighbor_report(vif_name, NULL);
    ow_conf_vif_set_ap_mcast2ucast(vif_name, NULL);
    ow_conf_vif_set_ap_passpoint_ref(vif_name, NULL);
    ow_conf_vif_set_ap_ft_encr_key(vif_name, NULL);
    ow_conf_vif_set_ap_ft_over_ds(vif_name, NULL);
    ow_conf_vif_set_ap_ft_pmk_r1_push(vif_name, NULL);
    ow_conf_vif_set_ap_ft_psk_generate_local(vif_name, NULL);
    ow_conf_vif_set_ap_ft_pmk_r0_key_lifetime_sec(vif_name, NULL);
    ow_conf_vif_set_ap_ft_pmk_r1_max_key_lifetime_sec(vif_name, NULL);
}

void
ow_conf_vif_unset(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    struct ow_conf_vif *vif = ow_conf_vif_get_ro(self, vif_name);
    if (vif == NULL) return;

    ow_conf_vif_clear(vif_name);
    ow_conf_vif_set_enabled(vif_name, NULL);
    ow_conf_vif_set_phy_name(vif->vif_name, NULL);

    ds_tree_remove(&self->vif_tree, vif);

    FREE(vif->vif_name);
    FREE(vif);
    osw_conf_invalidate(&self->conf_mutator);
}

bool
ow_conf_vif_is_set(const char *vif_name)
{
    struct ow_conf *self = &g_ow_conf;
    return ds_tree_find(&self->vif_tree, vif_name) == NULL ? false : true;
}

bool
ow_conf_is_settled(void)
{
    if (osw_drv_work_is_settled() == false) return false;
    if (osw_confsync_get_state(osw_confsync_get()) != OSW_CONFSYNC_IDLE) return false;
    return true;
}

static void
ow_conf_init_priv(struct ow_conf *self)
{
    osw_conf_register_mutator(&self->conf_mutator);
}

static void
ow_conf_init(void)
{
    static bool initialized;
    if (initialized == true) return;
    ow_conf_init_priv(&g_ow_conf);
    initialized = true;
}

static void
ow_conf_ut_ev_idle_cb(EV_P_ ev_idle *arg, int events)
{
    ev_idle_stop(EV_A_ arg);
}

static void
ow_conf_ut_run(void)
{
    struct ev_idle idle;
    ev_idle_init(&idle, ow_conf_ut_ev_idle_cb);
    ev_idle_start(EV_DEFAULT_ &idle);
    ev_run(EV_DEFAULT_ 0);
    assert(ow_conf_is_settled() == true);
}

static void
ow_conf_ut_phy_enabled_op_request_config_cb(struct osw_drv *drv,
                                            struct osw_drv_conf *conf)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    size_t i;

    LOGI("ow: conf: ut: config");
    for (i = 0; i < conf->n_phy_list; i++) {
        struct osw_drv_phy_config *phy = &conf->phy_list[i];

        assert(phy->changed == true);
        LOGI("ow: conf: ut: config: %s: changing enabled: %d\n",
             phy->phy_name, phy->enabled);

        osw_drv_dummy_set_phy(dummy,
                              phy->phy_name,
                              (struct osw_drv_phy_state []){{
                                  .exists = true,
                                  .enabled = phy->enabled,
                              }});
    }

    osw_drv_conf_free(conf);
}

OSW_UT(ow_conf_ut_phy_enabled)
{
    struct osw_drv_dummy dummy = {
        .name = "phy_enabled",
        .request_config_fn = ow_conf_ut_phy_enabled_op_request_config_cb,
    };
    bool enabled = true;
    const char *phy_name = "phy1";

    osw_module_load_name("osw_drv");
    osw_drv_dummy_init(&dummy);
    ow_conf_init();
    ow_conf_ut_run();

    osw_drv_dummy_set_phy(&dummy, phy_name, (struct osw_drv_phy_state []){{
            .exists = true,
            .enabled = true,
            }});

    ow_conf_ut_run();
    assert(osw_state_phy_lookup(phy_name)->drv_state->enabled == true);

    LOGI("ow: conf: ut: phy_enabled: disabling, expecting true->false");
    enabled = false; ow_conf_phy_set_enabled(phy_name, &enabled);
    ow_conf_ut_run();
    assert(ow_conf_phy_get(&g_ow_conf, phy_name) != NULL);
    assert(ow_conf_phy_get(&g_ow_conf, phy_name)->enabled != NULL);
    assert(*ow_conf_phy_get(&g_ow_conf, phy_name)->enabled == enabled);
    assert(osw_state_phy_lookup(phy_name)->drv_state->enabled == enabled);

    LOGI("ow: conf: ut: phy_enabled: enabling, expecting false->true");
    enabled = true; ow_conf_phy_set_enabled(phy_name, &enabled);
    ow_conf_ut_run();
    assert(ow_conf_phy_get(&g_ow_conf, phy_name) != NULL);
    assert(ow_conf_phy_get(&g_ow_conf, phy_name)->enabled != NULL);
    assert(*ow_conf_phy_get(&g_ow_conf, phy_name)->enabled == enabled);
    assert(osw_state_phy_lookup(phy_name)->drv_state->enabled == enabled);

    LOGI("ow: conf: ut: phy_enabled: blipping, expecting no action");
    enabled = false; ow_conf_phy_set_enabled(phy_name, &enabled);
    enabled = true; ow_conf_phy_set_enabled(phy_name, &enabled);
    ow_conf_ut_run();
    assert(ow_conf_phy_get(&g_ow_conf, phy_name) != NULL);
    assert(ow_conf_phy_get(&g_ow_conf, phy_name)->enabled != NULL);
    assert(*ow_conf_phy_get(&g_ow_conf, phy_name)->enabled == enabled);
    assert(osw_state_phy_lookup(phy_name)->drv_state->enabled == enabled);

    LOGI("ow: conf: ut: phy_enabled: changing other attr, expecting no action");
    osw_drv_dummy_set_phy(&dummy, phy_name, (struct osw_drv_phy_state []){{
            .exists = true,
            .enabled = enabled,
            .tx_chainmask = 0x1,
            }});
    ow_conf_ut_run();
    assert(ow_conf_phy_get(&g_ow_conf, phy_name) != NULL);
    assert(ow_conf_phy_get(&g_ow_conf, phy_name)->enabled != NULL);
    assert(ow_conf_phy_get(&g_ow_conf, phy_name)->tx_chainmask == NULL);
    assert(*ow_conf_phy_get(&g_ow_conf, phy_name)->enabled == enabled);
    assert(osw_state_phy_lookup(phy_name)->drv_state->enabled == enabled);
    assert(osw_state_phy_lookup(phy_name)->drv_state->tx_chainmask == 0x1);

    LOGI("ow: conf: ut: phy_enabled: changing other attr again, expecting no action");
    osw_drv_dummy_set_phy(&dummy, phy_name, (struct osw_drv_phy_state []){{
            .exists = true,
            .enabled = enabled,
            .tx_chainmask = 0x2,
            }});
    ow_conf_ut_run();
    assert(ow_conf_phy_get(&g_ow_conf, phy_name) != NULL);
    assert(ow_conf_phy_get(&g_ow_conf, phy_name)->enabled != NULL);
    assert(ow_conf_phy_get(&g_ow_conf, phy_name)->tx_chainmask == NULL);
    assert(*ow_conf_phy_get(&g_ow_conf, phy_name)->enabled == enabled);
    assert(osw_state_phy_lookup(phy_name)->drv_state->enabled == enabled);
    assert(osw_state_phy_lookup(phy_name)->drv_state->tx_chainmask == 0x2);

    LOGI("ow: conf: ut: phy_enabled: changing state, expecting corrective action, false->true");
    osw_drv_dummy_set_phy(&dummy, phy_name, (struct osw_drv_phy_state []){{
            .exists = true,
            .enabled = false,
            }});
    ow_conf_ut_run();
}

static void
ow_conf_ut_vif_enabled_op_request_config_cb(struct osw_drv *drv,
                                            struct osw_drv_conf *conf)
{
    struct osw_drv_dummy *dummy = osw_drv_get_priv(drv);
    size_t i;

    LOGI("ow: conf: ut: config");
    for (i = 0; i < conf->n_phy_list; i++) {
        struct osw_drv_phy_config *phy = &conf->phy_list[i];
        size_t j;

        assert(phy->changed == false);
        LOGI("ow: conf: ut: config: %s: changing enabled: %d\n",
             phy->phy_name, phy->enabled);

        for (j = 0; j < phy->vif_list.count; j++) {
            struct osw_drv_vif_config *vif = &phy->vif_list.list[j];

            assert(vif->enabled_changed);
            LOGI("ow: conf: ut: config: %s/%s: changing enabled: %d\n",
                 phy->phy_name, vif->vif_name, vif->enabled);

            struct osw_drv_vif_state vif_state;
            MEMZERO(vif_state);
            vif_state.exists = true;
            vif_state.vif_type = vif->vif_type;
            vif_state.status = vif->enabled ? OSW_VIF_ENABLED : OSW_VIF_DISABLED;
            vif_state.u.ap.beacon_interval_tu = OW_CONF_DEFAULT_BEACON_INTERVAL_TU;
            vif_state.u.ap.channel.control_freq_mhz = 2412;
            vif_state.u.ap.channel.center_freq0_mhz = 2412;

            osw_drv_dummy_set_vif(dummy,
                                  phy->phy_name,
                                  vif->vif_name,
                                  &vif_state);
        }
    }

    osw_drv_conf_free(conf);
}

OSW_UT(ow_conf_ut_vif_enabled)
{
    struct osw_drv_dummy dummy = {
        .name = "vif_enabled",
        .request_config_fn = ow_conf_ut_vif_enabled_op_request_config_cb,
    };
    bool enabled = true;
    enum osw_vif_status status = OSW_VIF_ENABLED;
    const char *phy_name = "phy1";
    const char *vif_name = "vif1";

    struct osw_drv_vif_state vif_state;
    MEMZERO(vif_state);
    vif_state.exists = true;
    vif_state.vif_type = OSW_VIF_AP;
    vif_state.status = OSW_VIF_ENABLED;
    vif_state.u.ap.beacon_interval_tu = OW_CONF_DEFAULT_BEACON_INTERVAL_TU;
    vif_state.u.ap.channel.control_freq_mhz = 2412;
    vif_state.u.ap.channel.center_freq0_mhz = 2412;

    osw_module_load_name("osw_drv");
    osw_drv_dummy_init(&dummy);
    ow_conf_init();
    ow_conf_ut_run();

    osw_drv_dummy_set_phy(&dummy, phy_name, (struct osw_drv_phy_state []){{
            .exists = true,
            .enabled = true,
            }});

    osw_drv_dummy_set_vif(&dummy, phy_name, vif_name, &vif_state);

    ow_conf_ut_run();
    assert(osw_state_vif_lookup(phy_name, vif_name)->drv_state->status == status);

    LOGI("%s: disabling vif %s, expect disablement", __func__, vif_name);
    ow_conf_vif_set_phy_name(vif_name, phy_name);
    enabled = false; status = OSW_VIF_DISABLED; ow_conf_vif_set_enabled(vif_name, &enabled);
    ow_conf_ut_run();
    assert(ow_conf_vif_get(&g_ow_conf, vif_name) != NULL);
    assert(ow_conf_vif_get(&g_ow_conf, vif_name)->enabled != NULL);
    assert(*ow_conf_vif_get(&g_ow_conf, vif_name)->enabled == enabled);
    assert(osw_state_vif_lookup(phy_name, vif_name)->drv_state->status == status);

    LOGI("%s: enabling vif %s, expect enablement", __func__, vif_name);
    enabled = true; status = OSW_VIF_ENABLED; ow_conf_vif_set_enabled(vif_name, &enabled);
    ow_conf_ut_run();
    assert(ow_conf_vif_get(&g_ow_conf, vif_name) != NULL);
    assert(ow_conf_vif_get(&g_ow_conf, vif_name)->enabled != NULL);
    assert(*ow_conf_vif_get(&g_ow_conf, vif_name)->enabled == enabled);
    assert(osw_state_vif_lookup(phy_name, vif_name)->drv_state->status == status);

    LOGI("%s: re-enabling vif %s, expect no-op", __func__, vif_name);
    enabled = false; status = OSW_VIF_DISABLED; ow_conf_vif_set_enabled(vif_name, &enabled);
    enabled = true; status = OSW_VIF_ENABLED; ow_conf_vif_set_enabled(vif_name, &enabled);
    ow_conf_ut_run();
    assert(ow_conf_vif_get(&g_ow_conf, vif_name) != NULL);
    assert(ow_conf_vif_get(&g_ow_conf, vif_name)->enabled != NULL);
    assert(*ow_conf_vif_get(&g_ow_conf, vif_name)->enabled == enabled);
    assert(osw_state_vif_lookup(phy_name, vif_name)->drv_state->status == status);
}

OSW_UT(ow_conf_ut_is_set)
{
    const char *phy_name = "phy1";

    assert(ow_conf_phy_is_set(phy_name) == false);
    assert(ow_conf_phy_get_enabled(phy_name) == NULL);
    assert(ow_conf_phy_is_set(phy_name) == false);
}

OSW_MODULE(ow_conf)
{
    OSW_MODULE_LOAD(osw_conf);
    ow_conf_init();
    return NULL;
}

#define DEFINE_PHY_FIELD_UT(field, type, eq, ...) \
    static void \
    ow_conf_ut_field_phy_##field##_cb(void *data) { \
        ow_conf_init(); \
        assert(ow_conf_phy_get_##field("phy1") == NULL); \
        \
        const type values[] = { __VA_ARGS__ }; \
        size_t i; \
        for (i = 0; i < ARRAY_SIZE(values); i++) { \
           ow_conf_phy_set_##field("phy1", &values[i]); \
           assert(ow_conf_phy_get_##field("phy1") != NULL); \
           assert(eq(ow_conf_phy_get_##field("phy1"), &values[i])); \
        } \
        ow_conf_phy_set_##field("phy1", NULL); \
        assert(ow_conf_phy_get_##field("phy1") == NULL); \
    } \
    static void \
    ow_conf_ut_field_phy_##field##_module_init(void *data) { \
        osw_ut_register("ow_conf_ut_field_phy_" #field, \
                        ow_conf_ut_field_phy_##field##_cb, NULL); \
    } \
    static void \
    ow_conf_ut_field_phy_##field##_module_fini(void *data) { \
    } \
    MODULE(ow_conf_ut_field_phy_##field##_module, \
           ow_conf_ut_field_phy_##field##_module_init, \
           ow_conf_ut_field_phy_##field##_module_fini)

#define DEFINE_VIF_FIELD_UT(field, type, eq, ...) \
    static void \
    ow_conf_ut_field_vif_##field##_cb(void *data) { \
        ow_conf_init(); \
        assert(ow_conf_vif_get_##field("vif1") == NULL); \
        \
        const type values[] = { __VA_ARGS__ }; \
        size_t i; \
        for (i = 0; i < ARRAY_SIZE(values); i++) { \
           ow_conf_vif_set_##field("vif1", &values[i]); \
           assert(ow_conf_vif_get_##field("vif1") != NULL); \
           assert(eq(ow_conf_vif_get_##field("vif1"), &values[i])); \
        } \
        ow_conf_vif_set_##field("vif1", NULL); \
        assert(ow_conf_vif_get_##field("vif1") == NULL); \
    } \
    static void \
    ow_conf_ut_field_vif_##field##_module_init(void *data) { \
        osw_ut_register("ow_conf_ut_field_vif_" #field, \
                        ow_conf_ut_field_vif_##field##_cb, NULL); \
    } \
    static void \
    ow_conf_ut_field_vif_##field##_module_fini(void *data) { \
    } \
    MODULE(ow_conf_ut_field_vif_##field##_module, \
           ow_conf_ut_field_vif_##field##_module_init, \
           ow_conf_ut_field_vif_##field##_module_fini)


#define FIELD_EQ(x, y) (*(x) == *(y))
#define FIELD_MEM_EQ(x, y) (memcmp(x, y, sizeof(*(x))) == 0)
#define FIELD_STR_EQ(x, y) (strcmp(x, y) == 0)

DEFINE_PHY_FIELD_UT(enabled, bool, FIELD_EQ, true, false, true);
DEFINE_PHY_FIELD_UT(tx_chainmask, int, FIELD_EQ, 1, 2, 3, 1, 0);
DEFINE_VIF_FIELD_UT(enabled, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(type, enum osw_vif_type, FIELD_EQ, OSW_VIF_UNDEFINED, OSW_VIF_AP, OSW_VIF_AP_VLAN, OSW_VIF_STA);
DEFINE_VIF_FIELD_UT(ap_channel, struct osw_channel, FIELD_MEM_EQ,
                    { .control_freq_mhz = 5180, .width = OSW_CHANNEL_40MHZ, .center_freq0_mhz = 5190 },
                    { .control_freq_mhz = 5220, .width = OSW_CHANNEL_80MHZ, .center_freq0_mhz = 5210 },
                    { .control_freq_mhz = 5220, .width = OSW_CHANNEL_160MHZ, .center_freq0_mhz = 5250 },
                    { .control_freq_mhz = 2412, .width = OSW_CHANNEL_20MHZ, .center_freq0_mhz = 2412 });
DEFINE_VIF_FIELD_UT(ap_ssid, struct osw_ssid, FIELD_MEM_EQ, { .buf = "ssid1", .len = 5 }, { .buf = "ssid22", .len = 6 });
DEFINE_VIF_FIELD_UT(ap_bridge_if_name, struct osw_ifname, FIELD_MEM_EQ, { .buf = "br0" }, { .buf = "br1" });
DEFINE_VIF_FIELD_UT(ap_beacon_protection, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_ssid_hidden, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_group_rekey_seconds, int, FIELD_EQ, 1, 2, 60, 3600, 0);
DEFINE_VIF_FIELD_UT(ap_ft_mobility_domain, int, FIELD_EQ, 0x400, 0x1600, 0x1337);
DEFINE_VIF_FIELD_UT(ap_pmf, enum osw_pmf, FIELD_EQ, OSW_PMF_DISABLED, OSW_PMF_OPTIONAL, OSW_PMF_REQUIRED);
DEFINE_VIF_FIELD_UT(ap_akm_psk, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_psk_sha256, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_eap, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_eap_sha256, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_eap_sha384, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_eap_suite_b, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_eap_suite_b192, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_sae, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_sae_ext, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_ft_sae, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_ft_sae_ext, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_ft_psk, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_ft_eap, bool, FIELD_EQ, true, false, true);
DEFINE_VIF_FIELD_UT(ap_akm_ft_eap_sha384, bool, FIELD_EQ, true, false, true);

