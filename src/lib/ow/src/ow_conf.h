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

/* Can be used to auto-enable ap-vlan interfaces */
void ow_conf_ap_vlan_set_enabled(const bool *enable);

void ow_conf_phy_unset(const char *phy_name);
void ow_conf_phy_set_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_tx_chainmask(const char *phy_name, const int *tx_chainmask);
void ow_conf_phy_set_thermal_tx_chainmask(const char *phy_name, const int *tx_chainmask);
void ow_conf_phy_set_tx_power_dbm(const char *phy_name, const int *tx_power_dbm);
void ow_conf_phy_set_ap_wmm_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_ap_ht_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_ap_vht_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_ap_he_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_ap_eht_enabled(const char *phy_name, const bool *enabled);
void ow_conf_phy_set_ap_beacon_interval_tu(const char *phy_name, const int *tu);
void ow_conf_phy_set_ap_channel(const char *phy_name, const struct osw_channel *channel);

bool ow_conf_phy_is_set(const char *phy_name);
const bool *ow_conf_phy_get_enabled(const char *phy_name);
const int *ow_conf_phy_get_tx_chainmask(const char *phy_name);
const struct osw_channel *ow_conf_phy_get_ap_channel(const char *phy_name);
void ow_conf_phy_set_ap_supp_rates(const char *phy_name, const uint16_t *supp_rates);
void ow_conf_phy_set_ap_basic_rates(const char *phy_name, const uint16_t *basic_rates);
void ow_conf_phy_set_ap_beacon_rate(const char *phy_name, const enum osw_rate_legacy *beacon_rate);
void ow_conf_phy_set_ap_mcast_rate(const char *phy_name, const enum osw_rate_legacy *mcast_rate);
void ow_conf_phy_set_ap_mgmt_rate(const char *phy_name, const enum osw_rate_legacy *mgmt_rate);

void ow_conf_vif_clear(const char *vif_name);
void ow_conf_vif_unset(const char *vif_name);
void ow_conf_vif_set_phy_name(const char *vif_name, const char *phy_name);
void ow_conf_vif_set_type(const char *vif_name, const enum osw_vif_type *type);
void ow_conf_vif_set_enabled(const char *vif_name, const bool *enabled);
void ow_conf_vif_set_tx_power_dbm(const char *vif_name, const int *tx_power_dbm);
void ow_conf_vif_set_ap_channel(const char *vif_name, const struct osw_channel *channel);
void ow_conf_vif_set_ap_ssid(const char *vif_name, const struct osw_ssid *ssid);
void ow_conf_vif_set_ap_bridge_if_name(const char *vif_name, const struct osw_ifname *bridge_if_name);
void ow_conf_vif_set_ap_nas_identifier(const char *vif_name, const struct osw_nas_id *nas_id);
void ow_conf_vif_set_ap_beacon_protection(const char *vif_name, const bool *enabled);
void ow_conf_vif_set_ap_ssid_hidden(const char *vif_name, const bool *hidden);
void ow_conf_vif_set_ap_isolated(const char *vif_name, const bool *isolated);
void ow_conf_vif_set_ap_ht_enabled(const char *vif_name, const bool *ht_enabled);
void ow_conf_vif_set_ap_vht_enabled(const char *vif_name, const bool *vht_enabled);
void ow_conf_vif_set_ap_he_enabled(const char *vif_name, const bool *he_enabled);
void ow_conf_vif_set_ap_eht_enabled(const char *vif_name, const bool *eht_enabled);
void ow_conf_vif_set_ap_ht_required(const char *vif_name, const bool *ht_required);
void ow_conf_vif_set_ap_vht_required(const char *vif_name, const bool *vht_required);
void ow_conf_vif_set_ap_supp_rates(const char *vif_name, const uint16_t *supp_rates);
void ow_conf_vif_set_ap_basic_rates(const char *vif_name, const uint16_t *basic_rates);
void ow_conf_vif_set_ap_beacon_rate(const char *vif_name, const struct osw_beacon_rate *rate);
void ow_conf_vif_set_ap_wpa(const char *vif_name, const bool *wpa_enabled);
void ow_conf_vif_set_ap_rsn(const char *vif_name, const bool *rsn_enabled);
void ow_conf_vif_set_ap_pairwise_tkip(const char *vif_name, const bool *tkip_enabled);
void ow_conf_vif_set_ap_pairwise_ccmp(const char *vif_name, const bool *ccmp_enabled);
void ow_conf_vif_set_ap_pairwise_ccmp256(const char *vif_name, const bool *ccmp256_enabled);
void ow_conf_vif_set_ap_pairwise_gcmp(const char *vif_name, const bool *gcmp_enabled);
void ow_conf_vif_set_ap_pairwise_gcmp256(const char *vif_name, const bool *gcmp256_enabled);
void ow_conf_vif_set_ap_akm_eap(const char *vif_name, const bool *eap_enabled);
void ow_conf_vif_set_ap_akm_eap_sha256(const char *vif_name, const bool *eap_sha256_enabled);
void ow_conf_vif_set_ap_akm_eap_sha384(const char *vif_name, const bool *eap_sha384_enabled);
void ow_conf_vif_set_ap_akm_eap_suite_b(const char *vif_name, const bool *eap_suite_b_enabled);
void ow_conf_vif_set_ap_akm_eap_suite_b192(const char *vif_name, const bool *eap_suite_b192_enabled);
void ow_conf_vif_set_ap_akm_ft_eap(const char *vif_name, const bool *ft_eap_enabled);
void ow_conf_vif_set_ap_akm_ft_eap_sha384(const char *vif_name, const bool *ft_eap_sha384_enabled);
void ow_conf_vif_set_ap_akm_ft_psk(const char *vif_name, const bool *ft_psk_enabled);
void ow_conf_vif_set_ap_akm_ft_sae(const char *vif_name, const bool *ft_sae_enabled);
void ow_conf_vif_set_ap_akm_ft_sae_ext(const char *vif_name, const bool *ft_sae_ext_enabled);
void ow_conf_vif_set_ap_akm_psk(const char *vif_name, const bool *psk_enabled);
void ow_conf_vif_set_ap_akm_psk_sha256(const char *vif_name, const bool *psk_sha256_enabled);
void ow_conf_vif_set_ap_akm_sae(const char *vif_name, const bool *sae_enabled);
void ow_conf_vif_set_ap_akm_sae_ext(const char *vif_name, const bool *sae_ext_enabled);
void ow_conf_vif_set_ap_pmf(const char *vif_name, const enum osw_pmf *pmf);
void ow_conf_vif_set_ap_multi_ap(const char *vif_name, const struct osw_multi_ap *multi_ap);
void ow_conf_vif_set_ap_group_rekey_seconds(const char *vif_name, const int *seconds);
void ow_conf_vif_set_ap_ft_mobility_domain(const char *vif_name, const int *mdid);
void ow_conf_vif_set_ap_beacon_interval_tu(const char *vif_name, const int *tu);
void ow_conf_vif_set_ap_acl_policy(const char *vif_name, const enum osw_acl_policy *policy);
void ow_conf_vif_set_ap_wps(const char *vif_name, const bool *wps_enabled);
void ow_conf_vif_set_ap_wmm(const char *vif_name, const bool *wmm_enabled);
void ow_conf_vif_set_ap_mbo(const char *vif_name, const bool *mbo_enabled);
void ow_conf_vif_set_ap_oce(const char *vif_name, const bool *oce_enabled);
void ow_conf_vif_set_ap_oce_min_rssi_dbm(const char *vif_name, const int *oce_min_rssi_dbm);
void ow_conf_vif_set_ap_oce_min_rssi_enable(const char *vif_name, const bool *oce_min_rssi_enable);
void ow_conf_vif_set_ap_oce_retry_delay_sec(const char *vif_name, const int *oce_retry_delay_sec);
void ow_conf_vif_set_ap_max_sta(const char *vif_name, const int *max_sta);
void ow_conf_vif_set_ap_wmm_uapsd(const char *vif_name, const bool *wmm_uapsd_enabled);
void ow_conf_vif_set_ap_wnm_bss_trans(const char *vif_name, const bool *wnm_bss_trans_enabled);
void ow_conf_vif_set_ap_rrm_neighbor_report(const char *vif_name, const bool *rmm_neighbor_report_enabled);
void ow_conf_vif_set_ap_mcast2ucast(const char *vif_name, const bool *mcast2ucast_enabled);
void ow_conf_vif_set_ap_ft_encr_key(const char *vif_name, const struct osw_ft_encr_key *ft_encr_key);
void ow_conf_vif_set_ap_ft_over_ds(const char *vif_name, const bool *ft_over_ds);
void ow_conf_vif_set_ap_ft_pmk_r1_push(const char *vif_name, const bool *ft_pmk_r1_push);
void ow_conf_vif_set_ap_ft_psk_generate_local(const char *vif_name, const bool *ft_psk_generate_local);
void ow_conf_vif_set_ap_ft_pmk_r0_key_lifetime_sec(const char *vif_name, const int *ft_pmk_r0_key_lifetime_sec);
void ow_conf_vif_set_ap_ft_pmk_r1_max_key_lifetime_sec(const char *vif_name, const int *ft_pmk_r1_max_key_lifetime_sec);

void ow_conf_vif_set_ap_psk(const char *vif_name, int key_id, const char *str);
void ow_conf_vif_set_sta_net(const char *vif_name,
                             const struct osw_ssid *ssid,
                             const struct osw_hwaddr *bssid,
                             const struct osw_psk *psk,
                             const struct osw_wpa *wpa,
                             const struct osw_ifname *bridge_if_name,
                             const bool *multi_ap,
                             const int *priority);
void ow_conf_vif_add_ap_acl(const char *vif_name, const struct osw_hwaddr *addr);
void ow_conf_vif_del_ap_acl(const char *vif_name, const struct osw_hwaddr *addr);
void ow_conf_vif_set_ap_neigh(const char *vif_name,
                              const struct osw_hwaddr *addr,
                              const uint32_t bssid_info,
                              const uint8_t op_class,
                              const uint8_t channel,
                              const uint8_t phy_type);
void ow_conf_vif_set_ap_neigh_ft(const struct osw_hwaddr *addr,
                                 const char *vif_name,
                                 const bool ft_enabled,
                                 const char *ft_encr_key,
                                 const char* nas_id);
void ow_conf_vif_del_ap_neigh(const char *vif_name,
                              const struct osw_hwaddr *addr);
void ow_conf_vif_del_ap_neigh_ft(const struct osw_hwaddr *addr,
                                 const char *vif_name);
void ow_conf_radius_add(const char *ref_id,
                        const char *ip_addr,
                        const char *secret,
                        uint16_t port);
void ow_conf_radius_unset(const char *ref_id);
void ow_conf_radius_flush(void);
void ow_conf_radius_set_ip_addr(const char *ref_id,
                                const char *ip_addr);
void ow_conf_radius_set_secret(const char *ref_id,
                               const char *secret);
void ow_conf_radius_set_port(const char *ref_id,
                             uint16_t port);

void ow_conf_vif_add_radius_ref(const char *vif_name,
                                const char *ref_id);
void ow_conf_vif_flush_radius_refs(const char *vif_name);

void ow_conf_vif_add_radius_accounting_ref(const char *vif_name,
                                           const char *ref_id);
void ow_conf_vif_flush_radius_accounting_refs(const char *vif_name);

void ow_conf_vif_flush_ap_psk(const char *vif_name);
void ow_conf_vif_flush_ap_acl(const char *vif_name);
void ow_conf_vif_flush_ap_neigh(const char *vif_name);
void ow_conf_vif_flush_ap_neigh_ft(const char *vif_name);
void ow_conf_vif_flush_sta_net(const char *vif_name);

bool ow_conf_vif_is_set(const char *vif_name);
const char *ow_conf_vif_get_phy_name(const char *vif_name);
const bool *ow_conf_vif_get_enabled(const char *vif_name);
const enum osw_acl_policy *ow_conf_vif_get_ap_acl_policy(const char *vif_name);
const struct osw_ssid *ow_conf_vif_get_ap_ssid(const char *vif_name);
const struct osw_channel *ow_conf_vif_get_ap_channel(const char *vif_name);
const bool *ow_conf_vif_get_ap_wpa(const char *vif_name);
const bool *ow_conf_vif_get_ap_rsn(const char *vif_name);
const bool *ow_conf_vif_get_ap_pairwise_tkip(const char *vif_name);
const bool *ow_conf_vif_get_ap_pairwise_ccmp(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_psk(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_psk_sha256(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_eap(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_eap_sha256(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_eap_sha384(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_eap_suite_b(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_eap_suite_b192(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_sae(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_sae_ext(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_ft_psk(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_ft_sae(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_ft_sae_ext(const char *vif_name);
const bool *ow_conf_vif_get_ap_akm_ft_eap_sha384(const char *vif_name);
const enum osw_pmf *ow_conf_vif_get_ap_pmf(const char *vif_name);
const char *ow_conf_vif_get_ap_psk(const char *vif_name, int key_id);
bool ow_conf_vif_has_ap_acl(const char *vif_name, const struct osw_hwaddr *addr);

void ow_conf_passpoint_set_hs20_enabled(const char *ref_id, const bool *enabled);
void ow_conf_passpoint_set_adv_wan_status(const char *ref_id, const bool *adv_wan_status);
void ow_conf_passpoint_set_adv_wan_symmetric(const char *ref_id, const bool *adv_wan_symmetric);
void ow_conf_passpoint_set_adv_wan_at_capacity(const char *ref_id, const bool *adv_wan_at_capacity);
void ow_conf_passpoint_set_osen(const char *ref_id, const bool *osen);
void ow_conf_passpoint_set_asra(const char *ref_id, const bool *asra);
void ow_conf_passpoint_set_ant(const char *ref_id, const int *access_network_type);
void ow_conf_passpoint_set_venue_group(const char *ref_id, const int *venue_group);
void ow_conf_passpoint_set_venue_type(const char *ref_id, const int *venue_type);
void ow_conf_passpoint_set_anqp_domain_id(const char *ref_id, const int *anqp_domain_id);
void ow_conf_passpoint_set_pps_mo_id(const char *ref_id, const int *pps_mo_id);
void ow_conf_passpoint_set_t_c_timestamp(const char *ref_id, const int *t_c_timestamp);
void ow_conf_passpoint_set_t_c_filename(const char *ref_id, const char *t_c_filename);
void ow_conf_passpoint_set_anqp_elem(const char *ref_id, const char *anqp_elem);
void ow_conf_passpoint_set_hessid(const char *ref_id, const char *hessid);
void ow_conf_passpoint_set_osu_ssid(const char *ref_id, const char *osu_ssid);
void ow_conf_passpoint_set_nairealm_list(const char *ref_id, char **nairealm_list, const int nairealm_list_len);
void ow_conf_passpoint_set_domain_list(const char *ref_id, char **domain_list, const int domain_list_len);
void ow_conf_passpoint_set_roamc(const char *ref_id, char **roamc_list, const int roamc_list_len);
void ow_conf_passpoint_set_oper_fname_list(const char *ref_id, char **oper_fname_list,
                                           const int oper_fname_list_len);
void ow_conf_passpoint_set_venue_name_list(const char *ref_id, char **venue_name_list,
                                           const int venue_name_list_len);
void ow_conf_passpoint_set_venue_url_list(const char *ref_id, char **venue_url_list,
                                          const int venue_url_list_len);
void ow_conf_passpoint_set_list_3gpp_list(const char *ref_id, char **list_3gpp_list,
                                          const int list_3gpp_list_len);
void ow_conf_passpoint_set_net_auth_type_list(const char *ref_id, const int *net_auth_type_list,
                                              const int net_auth_type_list_len);
void ow_conf_passpoint_unset(char *ref_id);
void ow_conf_vif_set_ap_passpoint_ref(const char *vif_name, const char *ref_id);
const char* ow_conf_vif_get_ap_passpoint_ref(const char *vif_name);

#endif /* OW_CONF_H_INCLUDED */
