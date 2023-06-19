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
    OSW_HOSTAP_CONF_DECL_BOOL(mcast_to_ucast);
    OSW_HOSTAP_CONF_DECL_BOOL(send_probe_response);
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
    OSW_HOSTAP_CONF_DECL_INT (he_oper_chanwidth);
    OSW_HOSTAP_CONF_DECL_INT (he_oper_centr_freq_seg0_idx);
    OSW_HOSTAP_CONF_DECL_INT (he_oper_centr_freq_seg1_idx);
    /* IEEE 802.1X-2004 related configuration */
    OSW_HOSTAP_CONF_DECL_BOOL(ieee8021x);
    OSW_HOSTAP_CONF_DECL_INT (eapol_version);
    /* Integrated EAP server */
    OSW_HOSTAP_CONF_DECL_BOOL(eap_server);
    /* RADIUS client configuration */
    /* FIXME */
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
    char rxkh_buf[4096];
};

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
