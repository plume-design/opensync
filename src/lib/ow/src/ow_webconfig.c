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

/* libc */
#include <stdlib.h>
#include <stddef.h>

/* 3rd party */
#include <ccsp/wifi_hal.h>

/* opensync */
#include <module.h>
#include <const.h>
#include <util.h>
#include <log.h>

/* osw */
#include <osw_types.h>
#include <osw_state.h>
#include "ow_conf.h"

/* Walkaround for this definition in hal:
 *
 *   #ifndef BOOL
 *   #define BOOL  unsigned char
 *   #endif
 *
 *   decode/encode library validates this field
 */
#define TO_BOOL(x) (x != 0 ? true : false)

static enum
osw_channel_width ow_webconfig_bandwidth2osw(wifi_channelBandwidth_t bandwidth)
{
    switch (bandwidth) {
        case WIFI_CHANNELBANDWIDTH_80_80MHZ: return OSW_CHANNEL_80P80MHZ;
        case WIFI_CHANNELBANDWIDTH_160MHZ:   return OSW_CHANNEL_160MHZ;
        case WIFI_CHANNELBANDWIDTH_80MHZ:    return OSW_CHANNEL_80MHZ;
        case WIFI_CHANNELBANDWIDTH_40MHZ:    return OSW_CHANNEL_40MHZ;
        case WIFI_CHANNELBANDWIDTH_20MHZ:    return OSW_CHANNEL_20MHZ;
    }
    LOGW("%s: unknown bandwidth: %d", __func__, bandwidth);
    return OSW_CHANNEL_20MHZ;
}

static int
ow_webconfig_ch2freq(wifi_freq_bands_t band, unsigned int chan)
{
    switch (band) {
        case WIFI_FREQUENCY_2_4_BAND:
            return 2407 + (chan * 5);
        case WIFI_FREQUENCY_5_BAND:
        case WIFI_FREQUENCY_5L_BAND:
        case WIFI_FREQUENCY_5H_BAND:
            return 5000 + (chan * 5);
        case WIFI_FREQUENCY_6_BAND:
            if (chan == 2) return 5935;
            else return 5950 + (chan * 5);
        case WIFI_FREQUENCY_60_BAND:
            return 0;
    }
    LOGW("%s: unknown band: %d", __func__, band);
    return 0;
}

static enum osw_pmf
ow_webconfig_mfp2pmf(wifi_mfp_cfg_t mfp)
{
    switch (mfp) {
        case wifi_mfp_cfg_disabled:
            return OSW_PMF_DISABLED;
        case wifi_mfp_cfg_optional:
            return OSW_PMF_OPTIONAL;
        case wifi_mfp_cfg_required:
            return OSW_PMF_REQUIRED;
    }
    LOGW("%s: unknown mfp: %d", __func__, mfp);
    return OSW_PMF_OPTIONAL;
}

static enum osw_acl_policy
ow_webconfig_macmode2aclpolicy(wifi_mac_filter_mode_t mac_filter_mode)
{
    switch (mac_filter_mode) {
        case wifi_mac_filter_mode_white_list:
            return OSW_ACL_ALLOW_LIST;
        case wifi_mac_filter_mode_black_list:
            return OSW_ACL_DENY_LIST;
    }
    return OSW_ACL_NONE;
}

static void
ow_webconfig_fill_security(const char *vif_name,
                           const wifi_vap_info_t *vap,
                           struct osw_wpa *wpa,
                           enum osw_vif_type vif_type)
{
    const wifi_front_haul_bss_t *bss = &vap->u.bss_info;
    const wifi_back_haul_sta_t *sta =  &vap->u.sta_info;

    const wifi_vap_security_t *sec = NULL;
    bool is_key = false;

    switch (vif_type) {
        case OSW_VIF_AP:
            sec = &bss->security;
            break;
        case OSW_VIF_STA:
            sec = &sta->security;
            break;
        default:
            LOGW("%d: Invalid VIF type ", vif_type);
            return;
    }

    switch (sec->mode) {
        case wifi_security_mode_none:
            return;
        case wifi_security_mode_wep_64:
            LOGW("%s: wep 64 not supported", vif_name);
            return;
        case wifi_security_mode_wep_128:
            LOGW("%s: wep 128 not supported", vif_name);
            return;
        case wifi_security_mode_wpa_personal:
            wpa->wpa = true;
            wpa->akm_psk = true;
            is_key = true;
            break;
        case wifi_security_mode_wpa2_personal:
            wpa->rsn = true;
            wpa->akm_psk = true;
            is_key = true;
            break;
        case wifi_security_mode_wpa_wpa2_personal:
            wpa->wpa = true;
            wpa->rsn = true;
            wpa->akm_psk = true;
            is_key = true;
            break;
        case wifi_security_mode_wpa_enterprise:
            LOGI("%s: wpa enterprise not supported", vif_name);
            break;
        case wifi_security_mode_wpa2_enterprise:
            LOGI("%s: wpa2 enterprise not supported", vif_name);
            break;
        case wifi_security_mode_wpa_wpa2_enterprise:
            LOGI("%s: wpa1+2 enterprise not supported", vif_name);
            break;
        case wifi_security_mode_wpa3_personal:
            wpa->rsn = true;
            wpa->akm_sae = true;
            is_key = true;
            break;
        case wifi_security_mode_wpa3_transition:
            wpa->rsn = true;
            wpa->akm_psk = true;
            wpa->akm_sae = true;
            is_key = true;
            break;
        case wifi_security_mode_wpa3_enterprise:
            LOGI("%s: wpa3 enterprise not supported", vif_name);
            break;
    }

    switch (sec->encr) {
        case wifi_encryption_none:
            break;
        case wifi_encryption_tkip:
            wpa->pairwise_tkip = true;
            break;
        case wifi_encryption_aes:
            wpa->pairwise_ccmp = true;
            break;
        case wifi_encryption_aes_tkip:
            wpa->pairwise_tkip = true;
            wpa->pairwise_ccmp = true;
            break;
    }

    wpa->pmf = ow_webconfig_mfp2pmf(sec->mfp);

    /* This prevents calling the ow_conf_vif_set_ap_psk() for AP case. This can be reworked. */
    if (vif_type == OSW_VIF_STA)
        return;

    /* FIXME: Is this expected? This isn't documented. */
    if (sec->wpa3_transition_disable &&
        wpa->akm_sae == true &&
        wpa->akm_psk == true) {
        wpa->akm_psk = false;
        LOGI("%s: overriding: disabling wpa3 transition", vif_name);
    }

    wpa->group_rekey_seconds = sec->rekey_interval;

    if (is_key == true) {
        size_t max = sizeof(sec->u.key.key);

        switch (sec->u.key.type) {
            case wifi_security_key_type_psk:
            case wifi_security_key_type_pass:
            case wifi_security_key_type_sae:
            case wifi_security_key_type_psk_sae:
                /* FIXME: What's the difference between PSK
                 * and PASS? PSK and SAE? Is one of these an
                 * actual PMK instead? Assume everything as
                 * ASCII for now. */

                /* FIXME: Call to ow_conf* function here is basically
                 * against the use of this helper. It shall only fill
                 * the structure and setting shall be handled outside.
                 */
                if (WARN_ON(strnlen(sec->u.key.key, max) >= max) == false)
                    ow_conf_vif_set_ap_psk(vif_name, -1, sec->u.key.key);
                break;
        }
    }

    /* FIXME: ft_mobility_domain and akm_ft_* aren't
     * implemented in Wifi HAL at this time.
     */
}

static void
ow_webconfig_set_vif_sta(const char *phy_name,
                        const char *vif_name,
                        const wifi_vap_info_t *vap)
{
    const wifi_back_haul_sta_t *sta = &vap->u.sta_info;

    /* FIXME handle ow_conf_vif_unset */
    ow_conf_vif_set_phy_name(vif_name, phy_name);

    const enum osw_vif_type vif_type = OSW_VIF_STA;
    ow_conf_vif_set_type(vif_name, &vif_type);

    const bool enabled = TO_BOOL(sta->enabled);
    ow_conf_vif_set_enabled(vif_name, &enabled);

    struct osw_ssid ssid = {0};
    STRSCPY_WARN(ssid.buf, sta->ssid);
    ssid.len = strlen(ssid.buf);

    struct osw_hwaddr bssid = {0};
    memcpy(bssid.octet, sta->bssid, sizeof(sta->bssid));

    struct osw_wpa wpa = {0};
    ow_webconfig_fill_security(vif_name, vap, &wpa, OSW_VIF_STA);

    struct osw_psk psk = {0};
    STRSCPY_WARN(psk.str, sta->security.u.key.key);

    ow_conf_vif_flush_sta_net(vif_name);
    ow_conf_vif_set_sta_net(vif_name, &ssid, &bssid, &psk, &wpa, NULL, NULL, NULL);
}

static void
ow_webconfig_set_vif_ap(const char *phy_name,
                        const char *vif_name,
                        const wifi_vap_info_t *vap)
{
    const wifi_front_haul_bss_t *bss = &vap->u.bss_info;

    /* FIXME handle ow_conf_vif_unset */
    ow_conf_vif_set_phy_name(vif_name, phy_name);

    const enum osw_vif_type vif_type = OSW_VIF_AP;
    ow_conf_vif_set_type(vif_name, &vif_type);
    const bool enabled = TO_BOOL(bss->enabled);
    ow_conf_vif_set_enabled(vif_name, &enabled);

    struct osw_ssid ssid = {0};
    STRSCPY_WARN(ssid.buf, bss->ssid);
    ssid.len = strlen(ssid.buf);
    ow_conf_vif_set_ap_ssid(vif_name, &ssid);

    const bool hidden = !TO_BOOL(bss->showSsid);
    ow_conf_vif_set_ap_ssid_hidden(vif_name, &hidden);

    struct osw_ifname name = {0};
    STRSCPY_WARN(name.buf, vap->bridge_name);
    ow_conf_vif_set_ap_bridge_if_name(vif_name, &name);

    const bool isolated = TO_BOOL(bss->isolation);
    ow_conf_vif_set_ap_isolated(vif_name, &isolated);

    struct osw_wpa wpa = {0};
    ow_webconfig_fill_security(vif_name, vap, &wpa, OSW_VIF_AP);
    ow_conf_vif_set_ap_wpa(vif_name, &wpa.wpa);
    ow_conf_vif_set_ap_rsn(vif_name, &wpa.rsn);
    ow_conf_vif_set_ap_pairwise_tkip(vif_name, &wpa.pairwise_tkip);
    ow_conf_vif_set_ap_pairwise_ccmp(vif_name, &wpa.pairwise_ccmp);
    ow_conf_vif_set_ap_akm_psk(vif_name, &wpa.akm_psk);
    ow_conf_vif_set_ap_akm_sae(vif_name, &wpa.akm_sae);
    ow_conf_vif_set_ap_pmf(vif_name, &wpa.pmf);
    ow_conf_vif_set_ap_group_rekey_seconds(vif_name, &wpa.group_rekey_seconds);

    const enum osw_acl_policy policy = ow_webconfig_macmode2aclpolicy(bss->mac_filter_mode);
    ow_conf_vif_set_ap_acl_policy(vif_name, &policy);

    /* FIXME WPS pin method not supported */
    //const bool wps_enabled = TO_BOOL(bss->wps.enable);
    //ow_conf_vif_set_ap_wps(vif_name, &wps_enabled);

    const bool wmm_enabled = TO_BOOL(bss->wmm_enabled);
    ow_conf_vif_set_ap_wmm(vif_name, &wmm_enabled);

    const bool wmm_uapsd_enabled = TO_BOOL(bss->UAPSDEnabled);
    ow_conf_vif_set_ap_wmm_uapsd(vif_name, &wmm_uapsd_enabled);
}

void
ow_webconfig_set_phy(const char *phy_name,
                     const wifi_radio_operationParam_t *oper)
{
    const bool enabled = TO_BOOL(oper->enable);
    ow_conf_phy_set_enabled(phy_name, &enabled);

    const struct osw_channel channel = {
        .control_freq_mhz = ow_webconfig_ch2freq(oper->band, oper->channel),
        .width = ow_webconfig_bandwidth2osw(oper->channelWidth)
    };
    ow_conf_phy_set_ap_channel(phy_name, &channel);
    const int tu = oper->beaconInterval;
    ow_conf_phy_set_ap_beacon_interval_tu(phy_name, &tu);

    const bool ht = (oper->variant & WIFI_80211_VARIANT_N) ? true : false;
    const bool vht = (oper->variant & WIFI_80211_VARIANT_AC) ? true : false;
    const bool he = (oper->variant & WIFI_80211_VARIANT_AX) ? true : false;
    ow_conf_phy_set_ap_ht_enabled(phy_name, &ht);
    ow_conf_phy_set_ap_vht_enabled(phy_name, &vht);
    ow_conf_phy_set_ap_he_enabled(phy_name, &he);
}

void
ow_webconfig_set_vif(const char *phy_name,
                     const char *vif_name,
                     const wifi_vap_info_t *vap)
{
    assert(phy_name != NULL);
    assert(vif_name != NULL);
    assert(vap != NULL);

    switch (vap->vap_mode) {
        case wifi_vap_mode_ap:
            ow_webconfig_set_vif_ap(phy_name, vif_name, vap);
            return;
        case wifi_vap_mode_sta:
            ow_webconfig_set_vif_sta(phy_name, vif_name, vap);
            return;
        case wifi_vap_mode_monitor:
            LOGW("%s: Monitor mode not supported", __func__);
            return;
    }
}

void
ow_webconfig_get_phy(const char *phy_name,
                     wifi_radio_operationParam_t *oper,
                     wifi_vap_info_map_t *map)
{
    /* FIXME: This should rely on osw_state to report actual
     * system state.
     */
}

OSW_MODULE(ow_webconfig)
{
    OSW_MODULE_LOAD(ow_conf);
    return NULL;
}
