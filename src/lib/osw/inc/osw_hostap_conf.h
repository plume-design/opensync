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

#ifndef OSW_HOSTAP_CONF_H_INCLUDED
#define OSW_HOSTAP_CONF_H_INCLUDED

#include <osw_hostap_common.h>

struct osw_hostap_conf_ap_config {
    OSW_HOSTAP_CONF_DECL_STR(interface, 32);
    OSW_HOSTAP_CONF_DECL_STR(bridge, 32);
    OSW_HOSTAP_CONF_DECL_STR(driver, 32);
    OSW_HOSTAP_CONF_DECL_INT(logger_syslog);
    OSW_HOSTAP_CONF_DECL_INT(logger_syslog_level);
    OSW_HOSTAP_CONF_DECL_INT(logger_stdout);
    OSW_HOSTAP_CONF_DECL_INT(logger_stdout_level);
    OSW_HOSTAP_CONF_DECL_STR(ctrl_interface, 64);
    /* IEEE 802.11 related configuration */
    OSW_HOSTAP_CONF_DECL_STR (ssid, OSW_IEEE80211_SSID_LEN + 1);
    OSW_HOSTAP_CONF_DECL_STR (country_code, 3);
    OSW_HOSTAP_CONF_DECL_BOOL(ieee80211d);
    OSW_HOSTAP_CONF_DECL_BOOL(ieee80211h);
    OSW_HOSTAP_CONF_DECL_STR (hw_mode, 4);
    OSW_HOSTAP_CONF_DECL_INT (channel);
    OSW_HOSTAP_CONF_DECL_INT (op_class);
    OSW_HOSTAP_CONF_DECL_INT (beacon_int);
    OSW_HOSTAP_CONF_DECL_STR (supported_rates, 128);
    OSW_HOSTAP_CONF_DECL_STR (basic_rates, 128);
    OSW_HOSTAP_CONF_DECL_STR (beacon_rate, 32);
    OSW_HOSTAP_CONF_DECL_INT (macaddr_acl);
    OSW_HOSTAP_CONF_DECL_STR (accept_mac_file, 64);
    OSW_HOSTAP_CONF_DECL_STR (deny_mac_file, 64);
    OSW_HOSTAP_CONF_DECL_INT (auth_algs);
    OSW_HOSTAP_CONF_DECL_INT (multi_ap);
    OSW_HOSTAP_CONF_DECL_BOOL(ignore_broadcast_ssid);
    OSW_HOSTAP_CONF_DECL_BOOL(wmm_enabled);
    OSW_HOSTAP_CONF_DECL_BOOL(uapsd_advertisement_enabled);
    OSW_HOSTAP_CONF_DECL_BOOL(ap_isolate);
    OSW_HOSTAP_CONF_DECL_BOOL(multicast_to_unicast);
    OSW_HOSTAP_CONF_DECL_BOOL(send_probe_response);
    OSW_HOSTAP_CONF_DECL_BOOL(noscan);
    OSW_HOSTAP_CONF_DECL_BOOL(use_driver_iface_addr);
    /* IEEE 802.11n related configuration */
    OSW_HOSTAP_CONF_DECL_BOOL(ieee80211n);
    OSW_HOSTAP_CONF_DECL_STR (ht_capab, 256);
    OSW_HOSTAP_CONF_DECL_BOOL(require_ht);
    /* IEEE 802.11ac related configuration */
    OSW_HOSTAP_CONF_DECL_BOOL(ieee80211ac);
    OSW_HOSTAP_CONF_DECL_STR (vht_capab, 256);
    OSW_HOSTAP_CONF_DECL_BOOL(require_vht);
    OSW_HOSTAP_CONF_DECL_INT (vht_oper_chwidth);
    OSW_HOSTAP_CONF_DECL_INT (vht_oper_centr_freq_seg0_idx);
    OSW_HOSTAP_CONF_DECL_INT (vht_oper_centr_freq_seg1_idx);
    /* IEEE 802.11ax related configuration */
    OSW_HOSTAP_CONF_DECL_BOOL(ieee80211ax);
    OSW_HOSTAP_CONF_DECL_INT (he_oper_chwidth);
    OSW_HOSTAP_CONF_DECL_INT (he_oper_centr_freq_seg0_idx);
    OSW_HOSTAP_CONF_DECL_INT (he_oper_centr_freq_seg1_idx);
    /* IEEE 802.11ax related configuration */
    OSW_HOSTAP_CONF_DECL_BOOL(ieee80211be);
    OSW_HOSTAP_CONF_DECL_INT (eht_oper_chwidth);
    OSW_HOSTAP_CONF_DECL_INT (eht_oper_centr_freq_seg0_idx);
    /* IEEE 802.1X-2004 related configuration */
    OSW_HOSTAP_CONF_DECL_BOOL(ieee8021x);
    OSW_HOSTAP_CONF_DECL_INT (eapol_version);
    /* IEEE 802.11u - 2011 configuration */
    OSW_HOSTAP_CONF_DECL_BOOL(interworking);
    OSW_HOSTAP_CONF_DECL_INT (access_network_type);
    OSW_HOSTAP_CONF_DECL_BOOL(asra);
    OSW_HOSTAP_CONF_DECL_INT (venue_group);
    OSW_HOSTAP_CONF_DECL_INT (venue_type);
    OSW_HOSTAP_CONF_DECL_STR (hessid, OSW_IEEE80211_SSID_LEN + 1);
    OSW_HOSTAP_CONF_DECL_STR_LIST(roaming_consortium, 8);
    OSW_HOSTAP_CONF_DECL_INT (roaming_consortium_len);
    OSW_HOSTAP_CONF_DECL_STR_LIST(venue_name, 16);
    OSW_HOSTAP_CONF_DECL_INT (venue_name_len);
    OSW_HOSTAP_CONF_DECL_STR_LIST(venue_url, 16);
    OSW_HOSTAP_CONF_DECL_INT (venue_url_len);
    OSW_HOSTAP_CONF_DECL_STR_LIST(anqp_3gpp_cell_net, 16);
    OSW_HOSTAP_CONF_DECL_INT (anqp_3gpp_cell_net_len);
    OSW_HOSTAP_CONF_DECL_INT_LIST(network_auth_type, 8);
    OSW_HOSTAP_CONF_DECL_INT (network_auth_type_len);
    OSW_HOSTAP_CONF_DECL_STR_LIST(domain_name, 16);
    OSW_HOSTAP_CONF_DECL_INT (domain_name_len);
    OSW_HOSTAP_CONF_DECL_STR_LIST(nai_realm, 16);
    OSW_HOSTAP_CONF_DECL_INT (nai_realm_len);
    /* Integrated EAP server */
    OSW_HOSTAP_CONF_DECL_BOOL(eap_server);
    /* Passpoint configuration */
    OSW_HOSTAP_CONF_DECL_BOOL(hs20);
    OSW_HOSTAP_CONF_DECL_STR (hs20_wan_metrics, 32);
    OSW_HOSTAP_CONF_DECL_BOOL(osen);
    OSW_HOSTAP_CONF_DECL_INT (anqp_domain_id);
    //OSW_HOSTAP_CONF_DECL_INT(pps_mo_id);
    OSW_HOSTAP_CONF_DECL_INT(hs20_t_c_timestamp);
    OSW_HOSTAP_CONF_DECL_STR (osu_ssid, OSW_IEEE80211_SSID_LEN + 1);
    OSW_HOSTAP_CONF_DECL_STR (hs20_t_c_filename, 128);
    OSW_HOSTAP_CONF_DECL_STR (anqp_elem, 1024);
    OSW_HOSTAP_CONF_DECL_STR_LIST(hs20_oper_friendly_name, 16);
    OSW_HOSTAP_CONF_DECL_INT(hs20_oper_friendly_name_len);

    /* WPA/IEEE 802.11i configuration */
    OSW_HOSTAP_CONF_DECL_INT (wpa);
    OSW_HOSTAP_CONF_DECL_STR (wpa_psk_file , 64);
    OSW_HOSTAP_CONF_DECL_STR (wpa_key_mgmt, OSW_HOSTAP_CONF_WPA_KEY_MGMT_MAX_LEN);
    OSW_HOSTAP_CONF_DECL_STR (wpa_pairwise, 32);
    OSW_HOSTAP_CONF_DECL_INT (wpa_group_rekey);
    OSW_HOSTAP_CONF_DECL_BOOL(rsn_preauth);
    OSW_HOSTAP_CONF_DECL_INT (ieee80211w);
    OSW_HOSTAP_CONF_DECL_STR (sae_password, 128);
    OSW_HOSTAP_CONF_DECL_BOOL(sae_require_mfp);
    OSW_HOSTAP_CONF_DECL_INT (sae_pwe);
    /* IEEE 802.11r configuration */
    OSW_HOSTAP_CONF_DECL_INT (mobility_domain);
    OSW_HOSTAP_CONF_DECL_STR (nas_identifier, 64);
    OSW_HOSTAP_CONF_DECL_BOOL (ft_over_ds);
    OSW_HOSTAP_CONF_DECL_BOOL (pmk_r1_push);
    OSW_HOSTAP_CONF_DECL_BOOL (ft_psk_generate_local);
    OSW_HOSTAP_CONF_DECL_INT (ft_r0_key_lifetime);
    OSW_HOSTAP_CONF_DECL_INT (r1_max_key_lifetime);
    OSW_HOSTAP_CONF_DECL_STR (ft_encr_key, 64);
    OSW_HOSTAP_CONF_DECL_STR (rxkh_file, 64);
    OSW_HOSTAP_CONF_DECL_BOOL (ft_rrb_lo_sock);
    /* Wi-Fi Protected Setup (WPS) */
    OSW_HOSTAP_CONF_DECL_INT (wps_state);
    OSW_HOSTAP_CONF_DECL_STR (config_methods, 32);
    OSW_HOSTAP_CONF_DECL_STR (device_type, 16);
    OSW_HOSTAP_CONF_DECL_BOOL(pbc_in_m1);
    /* Device Provisioning Protocol */
    OSW_HOSTAP_CONF_DECL_STR (dpp_connector, 128);
    OSW_HOSTAP_CONF_DECL_STR (dpp_csign_hex, 128);
    OSW_HOSTAP_CONF_DECL_STR (dpp_netaccesskey_hex, 128);
    /* IEEE 802.11v-2011 */
    OSW_HOSTAP_CONF_DECL_BOOL(bss_transition);
    /* Radio measurements / location */
    OSW_HOSTAP_CONF_DECL_BOOL(rrm_neighbor_report);

    char conf_buf[4096];
    char psks_buf[4096];
    char acl_buf [4096];
    char radius_buf[4096];
    char rxkh_buf[4096];
    char extra_buf[1024];
};

void
osw_hostap_conf_list_free(struct osw_hostap_conf_ap_config *conf);

void
osw_hostap_conf_generate_ap_config_bufs(struct osw_hostap_conf_ap_config *conf);

bool
osw_hostap_conf_fill_ap_config(struct osw_drv_conf *drv_conf,
                               const char *phy_name,
                               const char *vif_name,
                               struct osw_hostap_conf_ap_config *conf);
void
osw_hostap_conf_fill_ap_state(const struct osw_hostap_conf_ap_state_bufs *bufs,
                              struct osw_drv_vif_state *vstate);

#endif /* OSW_HOSTAP_CONF_H_INCLUDED */
