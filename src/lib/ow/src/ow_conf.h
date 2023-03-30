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

#ifndef OW_CONF_H_INCLUDED
#define OW_CONF_H_INCLUDED

/**
 * @file ow_conf.h
 *
 * @brief
 * Defines API to access the OneWifi configuration database.
 *
 * @description
 * The configuration database is used to separate southbound
 * interface with OneWifi core.
 *
 * Each attribute is within an object group (phy, vif, ...) identified with a
 * key (phy_name, vif_name, ...). Each attribute can be in a state of being
 * defined or undefined. This eventually is translated to whether an attribute
 * will override the system state or not, when merging/applying configuration.
 *
 * Group relations are weak references - an attribute referencing other group
 * object's key, eg. ow_conf_vif_set_phy_name() will define phy-vif hierarchy.
 *
 * TODO:
 * - get API (not necessary for now)
 */

#include <osw_types.h>
#include <ds_dlist.h>

struct ow_conf_observer;

typedef void ow_conf_phy_changed_fn_t(struct ow_conf_observer *obs,
                                      const char *phy_name);
typedef void ow_conf_vif_changed_fn_t(struct ow_conf_observer *obs,
                                      const char *vif_name);

struct ow_conf_observer {
    /* public */
    const char *name;
    ow_conf_phy_changed_fn_t *phy_changed_fn;
    ow_conf_vif_changed_fn_t *vif_changed_fn;

    /* private */
    struct ds_dlist_node node;
};

void ow_conf_register_observer(struct ow_conf_observer *obs);

bool ow_conf_is_settled(void);

void ow_conf_phy_unset(const char *phy_name);
void ow_conf_phy_set_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_tx_chainmask(const char *phy_name, const int *tx_chainmask);
void ow_conf_phy_set_ap_wmm_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_ap_ht_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_ap_vht_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_ap_he_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_ap_beacon_interval_tu(const char *phy_name, const int *tu);
void ow_conf_phy_set_ap_channel(const char *phy_name, const struct osw_channel *channel);

bool ow_conf_phy_is_set(const char *phy_name);
const bool *ow_conf_phy_get_enabled(const char *phy_name);
const int *ow_conf_phy_get_tx_chainmask(const char *phy_name);

void ow_conf_vif_unset(const char *vif_name);
void ow_conf_vif_set_phy_name(const char *vif_name, const char *phy_name);
void ow_conf_vif_set_type(const char *vif_name, const enum osw_vif_type *type);
void ow_conf_vif_set_enabled(const char *vif_name, const bool *enabled);
void ow_conf_vif_set_ap_channel(const char *vif_name, const struct osw_channel *channel);
void ow_conf_vif_set_ap_ssid(const char *vif_name, const struct osw_ssid *ssid);
void ow_conf_vif_set_ap_bridge_if_name(const char *vif_name, const struct osw_ifname *bridge_if_name);
void ow_conf_vif_set_ap_ssid_hidden(const char *vif_name, const bool *hidden);
void ow_conf_vif_set_ap_isolated(const char *vif_name, const bool *isolated);
void ow_conf_vif_set_ap_ht_enabled(const char *vif_name, const bool *ht_enabled);
void ow_conf_vif_set_ap_vht_enabled(const char *vif_name, const bool *vht_enabled);
void ow_conf_vif_set_ap_he_enabled(const char *vif_name, const bool *he_enabled);
void ow_conf_vif_set_ap_ht_required(const char *vif_name, const bool *ht_required);
void ow_conf_vif_set_ap_vht_required(const char *vif_name, const bool *vht_required);
void ow_conf_vif_set_ap_supp_rates(const char *vif_name, const uint16_t *supp_rates);
void ow_conf_vif_set_ap_basic_rates(const char *vif_name, const uint16_t *basic_rates);
void ow_conf_vif_set_ap_beacon_rate(const char *vif_name, const struct osw_beacon_rate *rate);
void ow_conf_vif_set_ap_wpa(const char *vif_name, const bool *wpa_enabled);
void ow_conf_vif_set_ap_rsn(const char *vif_name, const bool *rsn_enabled);
void ow_conf_vif_set_ap_pairwise_tkip(const char *vif_name, const bool *tkip_enabled);
void ow_conf_vif_set_ap_pairwise_ccmp(const char *vif_name, const bool *ccmp_enabled);
void ow_conf_vif_set_ap_akm_psk(const char *vif_name, const bool *psk_enabled);
void ow_conf_vif_set_ap_akm_sae(const char *vif_name, const bool *sae_enabled);
void ow_conf_vif_set_ap_akm_ft_psk(const char *vif_name, const bool *ft_psk_enabled);
void ow_conf_vif_set_ap_akm_ft_sae(const char *vif_name, const bool *ft_sae_enabled);
void ow_conf_vif_set_ap_pmf(const char *vif_name, const enum osw_pmf *pmf);
void ow_conf_vif_set_ap_group_rekey_seconds(const char *vif_name, const int *seconds);
void ow_conf_vif_set_ap_ft_mobility_domain(const char *vif_name, const int *mdid);
void ow_conf_vif_set_ap_beacon_interval_tu(const char *vif_name, const int *tu);
void ow_conf_vif_set_ap_acl_policy(const char *vif_name, const enum osw_acl_policy *policy);
void ow_conf_vif_set_ap_wps(const char *vif_name, const bool *wps_enabled);
void ow_conf_vif_set_ap_wmm(const char *vif_name, const bool *wmm_enabled);
void ow_conf_vif_set_ap_wmm_uapsd(const char *vif_name, const bool *wmm_uapsd_enabled);
void ow_conf_vif_set_ap_wnm_bss_trans(const char *vif_name, const bool *wnm_bss_trans_enabled);
void ow_conf_vif_set_ap_rrm_neighbor_report(const char *vif_name, const bool *rmm_neighbor_report_enabled);
void ow_conf_vif_set_ap_mcast2ucast(const char *vif_name, const bool *mcast2ucast_enabled);

void ow_conf_vif_set_ap_psk(const char *vif_name, int key_id, const char *str);
void ow_conf_vif_set_sta_net(const char *vif_name,
                             const struct osw_ssid *ssid,
                             const struct osw_hwaddr *bssid,
                             const struct osw_psk *psk,
                             const struct osw_wpa *wpa);
void ow_conf_vif_add_ap_acl(const char *vif_name, const struct osw_hwaddr *addr);
void ow_conf_vif_del_ap_acl(const char *vif_name, const struct osw_hwaddr *addr);
void ow_conf_vif_set_ap_neigh(const char *vif_name,
                              const struct osw_hwaddr *addr,
                              const uint32_t bssid_info,
                              const uint8_t op_class,
                              const uint8_t channel,
                              const uint8_t phy_type);
void ow_conf_vif_del_ap_neigh(const char *vif_name,
                              const struct osw_hwaddr *addr);
void ow_conf_vif_flush_ap_psk(const char *vif_name);
void ow_conf_vif_flush_ap_acl(const char *vif_name);
void ow_conf_vif_flush_ap_neigh(const char *vif_name);
void ow_conf_vif_flush_sta_net(const char *vif_name);

bool ow_conf_vif_is_set(const char *vif_name);
const char *ow_conf_vif_get_phy_name(const char *vif_name);
const bool *ow_conf_vif_get_enabled(const char *vif_name);
const enum osw_acl_policy *ow_conf_vif_get_ap_acl_policy(const char *vif_name);
const struct osw_ssid *ow_conf_vif_get_ap_ssid(const char *vif_name);
const bool *ow_conf_vif_get_ap_wpa(const char *vif_name);
const bool *ow_conf_vif_get_ap_rsn(const char *vif_name);
const bool *ow_conf_vif_get_ap_pairwise_tkip(const char *vif_name);
const bool *ow_conf_vif_get_ap_pairwise_ccmp(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_psk(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_sae(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_ft_psk(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_ft_sae(const char *vif_name);
const enum osw_pmf *ow_conf_vif_get_ap_pmf(const char *vif_name);
const char *ow_conf_vif_get_ap_psk(const char *vif_name, int key_id);
bool ow_conf_vif_has_ap_acl(const char *vif_name, const struct osw_hwaddr *addr);

#endif /* OW_CONF_H_INCLUDED */
