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

#ifndef OSW_WPAS_CONF_H_INCLUDED
#define OSW_WPAS_CONF_H_INCLUDED

#include <osw_hostap_common.h>

struct osw_hostap_conf_sta_global_config {
    OSW_HOSTAP_CONF_DECL_BOOL(update_config);
    /* global configuration (shared by all network blocks */
    OSW_HOSTAP_CONF_DECL_STR (ctrl_interface, 64);
    OSW_HOSTAP_CONF_DECL_INT (eapol_version);
    OSW_HOSTAP_CONF_DECL_INT (ap_scan);
    OSW_HOSTAP_CONF_DECL_BOOL(passive_scan);
    OSW_HOSTAP_CONF_DECL_STR (country, 4);
    OSW_HOSTAP_CONF_DECL_INT (pmf);
    OSW_HOSTAP_CONF_DECL_BOOL(sae_check_mfp);
    OSW_HOSTAP_CONF_DECL_STR (sae_groups, 32);
    OSW_HOSTAP_CONF_DECL_INT (sae_pwe);
    OSW_HOSTAP_CONF_DECL_BOOL(scan_cur_freq);
    OSW_HOSTAP_CONF_DECL_BOOL(disallow_dfs);
    OSW_HOSTAP_CONF_DECL_BOOL(interworking);
};

struct osw_hostap_conf_sta_network_config {
    /* network block */
    OSW_HOSTAP_CONF_DECL_BOOL(disabled);
    OSW_HOSTAP_CONF_DECL_STR (id_str, 32);
    OSW_HOSTAP_CONF_DECL_STR (ssid, 32 + 2 + 1);
    OSW_HOSTAP_CONF_DECL_BOOL(scan_ssid);
    OSW_HOSTAP_CONF_DECL_STR (bssid, 18);
    OSW_HOSTAP_CONF_DECL_INT (ignore_broadcast_ssid);
    OSW_HOSTAP_CONF_DECL_INT (priority);
    OSW_HOSTAP_CONF_DECL_INT (mode);
    OSW_HOSTAP_CONF_DECL_INT (frequency);
    OSW_HOSTAP_CONF_DECL_BOOL(scan_freq);
    OSW_HOSTAP_CONF_DECL_STR (freq_list, 32);
    OSW_HOSTAP_CONF_DECL_STR (bgscan, 32);
    OSW_HOSTAP_CONF_DECL_STR (proto, 16);
    OSW_HOSTAP_CONF_DECL_STR (key_mgmt, 64);
    OSW_HOSTAP_CONF_DECL_INT (ieee80211w);
    OSW_HOSTAP_CONF_DECL_BOOL(ocv);
    OSW_HOSTAP_CONF_DECL_STR (auth_alg, 16);
    OSW_HOSTAP_CONF_DECL_STR (pairwise, 64);
    OSW_HOSTAP_CONF_DECL_STR (group, 16);
    OSW_HOSTAP_CONF_DECL_STR (group_mgmt, 48);
    OSW_HOSTAP_CONF_DECL_STR (psk, 64 + 2 + 1); /* 64 for hex, 2 for ", 1 for \0 */
    OSW_HOSTAP_CONF_DECL_BOOL(mem_only_psk);
    OSW_HOSTAP_CONF_DECL_STR (sae_password, 128 + 2 + 1);
    OSW_HOSTAP_CONF_DECL_STR (sae_password_id, 32);
    OSW_HOSTAP_CONF_DECL_BOOL(proactive_key_caching);
    OSW_HOSTAP_CONF_DECL_BOOL(ft_eap_pmksa_caching);
    OSW_HOSTAP_CONF_DECL_INT (group_rekey);
    /* Station inactivity limit */
    OSW_HOSTAP_CONF_DECL_INT (ap_max_inactivity);
    OSW_HOSTAP_CONF_DECL_INT (dtim_period);
    OSW_HOSTAP_CONF_DECL_INT (beacon_int);
    OSW_HOSTAP_CONF_DECL_BOOL(wps_disabled);
    OSW_HOSTAP_CONF_DECL_BOOL(beacon_prot);
    OSW_HOSTAP_CONF_DECL_BOOL(multi_ap_backhaul_sta);
    OSW_HOSTAP_CONF_DECL_INT (dot11RSNAConfigPMKLifetime);
    OSW_HOSTAP_CONF_DECL_INT (dot11RSNAConfigReauthThreshold);
    OSW_HOSTAP_CONF_DECL_INT (dot11RSNAConfigSATimeout);

    struct osw_hostap_conf_sta_network_config *next;
};

struct osw_hostap_conf_sta_config {
    struct osw_hostap_conf_sta_global_config global;
    struct osw_hostap_conf_sta_network_config *network;
    struct osw_ifname bridge_if_name;

    char conf_buf[4096];
    char extra_buf[1024];
};

void
osw_hostap_conf_generate_sta_config_bufs(struct osw_hostap_conf_sta_config *conf);

bool
osw_hostap_conf_fill_sta_config(struct osw_drv_conf *drv_conf,
                                const char *phy_name,
                                const char *vif_name,
                                struct osw_hostap_conf_sta_config *conf);

bool
osw_hostap_conf_free_sta_config(struct osw_hostap_conf_sta_config *conf);

void
osw_hostap_conf_fill_sta_state(const struct osw_hostap_conf_sta_state_bufs *bufs,
                               struct osw_drv_vif_state *vstate);

#endif /* OSW_WPAS_CONF_H_INCLUDED */
