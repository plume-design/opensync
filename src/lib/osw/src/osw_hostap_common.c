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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

/* opensync */
#include <memutil.h>
#include <util.h>
#include <os.h>
#include <log.h>

/* unit */
#include <osw_drv.h>
#include <osw_types.h>
#include <osw_hostap_common.h>

struct osw_drv_phy_config *
osw_hostap_conf_phy_lookup(const struct osw_drv_conf *drv_conf,
                           const char *phy_name)
{
    size_t i;

    if (drv_conf == NULL || phy_name == NULL)
        return NULL;

    for (i = 0; i < drv_conf->n_phy_list; i++) {
        if (drv_conf->phy_list[i].phy_name == NULL)
            continue;
        if (strcmp(drv_conf->phy_list[i].phy_name, phy_name) == 0) {
            return &drv_conf->phy_list[i];
        }
    }
    return NULL;
}

struct osw_drv_vif_config *
osw_hostap_conf_vif_lookup(const struct osw_drv_phy_config *phy,
                           const char *vif_name)
{
    size_t i;

    if (phy == NULL || vif_name == NULL)
        return NULL;

    for (i = 0; i < phy->vif_list.count; i++) {
        if (phy->vif_list.list[i].vif_name == NULL)
            continue;
        if (strcmp(phy->vif_list.list[i].vif_name, vif_name) == 0) {
            return &phy->vif_list.list[i];
        }
    }
    return NULL;
}

enum osw_hostap_conf_pmf
osw_hostap_conf_pmf_from_osw(const struct osw_wpa *wpa)
{
    switch (wpa->pmf) {
        case OSW_PMF_DISABLED: return OSW_HOSTAP_CONF_PMF_DISABLED;
        case OSW_PMF_OPTIONAL: return OSW_HOSTAP_CONF_PMF_OPTIONAL;
        case OSW_PMF_REQUIRED: return OSW_HOSTAP_CONF_PMF_REQUIRED;
    }
    /* unreachable */
    return OSW_HOSTAP_CONF_PMF_DISABLED;
}

enum osw_pmf
osw_hostap_conf_pmf_to_osw(enum osw_hostap_conf_pmf pmf)
{
    switch (pmf) {
        case OSW_HOSTAP_CONF_PMF_DISABLED: return OSW_PMF_DISABLED;
        case OSW_HOSTAP_CONF_PMF_OPTIONAL: return OSW_PMF_OPTIONAL;
        case OSW_HOSTAP_CONF_PMF_REQUIRED: return OSW_PMF_REQUIRED;
    }
    /* unreachable */
    return OSW_PMF_DISABLED;
};

enum osw_hostap_conf_chanwidth
osw_hostap_conf_chwidth_from_osw(enum osw_channel_width width)
{
    switch (width) {
        case OSW_CHANNEL_20MHZ:    return OSW_HOSTAP_CONF_CHANWIDTH_20MHZ_40MHZ;
        case OSW_CHANNEL_40MHZ:    return OSW_HOSTAP_CONF_CHANWIDTH_20MHZ_40MHZ;
        case OSW_CHANNEL_80MHZ:    return OSW_HOSTAP_CONF_CHANWIDTH_80MHZ;
        case OSW_CHANNEL_160MHZ:   return OSW_HOSTAP_CONF_CHANWIDTH_160MHZ;
        case OSW_CHANNEL_80P80MHZ: return OSW_HOSTAP_CONF_CHANWIDTH_80P80MHZ;
    }
    /* unreachable */
    return OSW_HOSTAP_CONF_CHANWIDTH_20MHZ_40MHZ;;
}

enum osw_channel_width
osw_hostap_conf_chwidth_to_osw(enum osw_hostap_conf_chanwidth width)
{
    switch (width) {
        /* No way to know if 20 or 40 based only on hapd_chanwidth */
        case OSW_HOSTAP_CONF_CHANWIDTH_20MHZ_40MHZ: return OSW_CHANNEL_40MHZ;
        case OSW_HOSTAP_CONF_CHANWIDTH_80MHZ:       return OSW_CHANNEL_80MHZ;
        case OSW_HOSTAP_CONF_CHANWIDTH_160MHZ:      return OSW_CHANNEL_160MHZ;
        case OSW_HOSTAP_CONF_CHANWIDTH_80P80MHZ:    return OSW_CHANNEL_80P80MHZ;
    }
    /* unreachable */
    return OSW_CHANNEL_40MHZ;
}

enum osw_hostap_conf_wpa
osw_hostap_conf_wpa_from_osw(const struct osw_wpa *wpa)
{
    enum osw_hostap_conf_wpa hostap_wpa = 0;

    if (wpa == NULL)
        return 0;

    hostap_wpa |= (wpa->wpa ? OSW_HOSTAP_CONF_WPA_WPA : 0);
    hostap_wpa |= (wpa->rsn ? OSW_HOSTAP_CONF_WPA_RSN : 0);
    return hostap_wpa;
}

enum osw_hostap_conf_auth_algs
osw_hostap_conf_auth_algs_from_osw(const struct osw_wpa *wpa)
{
    enum osw_hostap_conf_auth_algs auth_algs = 0;

    if (wpa == NULL)
        return 0;

    if (wpa->akm_psk) {
        auth_algs |= OSW_HOSTAP_CONF_AUTH_ALG_OPEN;
    }
    if (wpa->akm_sae) {
        auth_algs |= OSW_HOSTAP_CONF_AUTH_ALG_SAE;
    }
    if (wpa->akm_ft_psk) {
        auth_algs |= OSW_HOSTAP_CONF_AUTH_ALG_OPEN;
        auth_algs |= OSW_HOSTAP_CONF_AUTH_ALG_FT;
    }
    if (wpa->akm_ft_sae) {
        auth_algs |= OSW_HOSTAP_CONF_AUTH_ALG_OPEN;
        auth_algs |= OSW_HOSTAP_CONF_AUTH_ALG_SAE;
        auth_algs |= OSW_HOSTAP_CONF_AUTH_ALG_FT;
    }
    return auth_algs;
}

char*
osw_hostap_conf_wpa_key_mgmt_from_osw(const struct osw_wpa *wpa)
{
    size_t len = OSW_HOSTAP_CONF_WPA_KEY_MGMT_MAX_LEN;
    size_t *plen = &len;
    char *wpa_key_mgmt = CALLOC(len, sizeof(char));
    char *cpy_wpa_key_mgmt = wpa_key_mgmt;
    char **pbuf = &wpa_key_mgmt;

    if (wpa == NULL)
        return NULL;

    if (wpa->akm_psk)           csnprintf(pbuf, plen, "WPA-PSK ");
    if (wpa->akm_sae)           csnprintf(pbuf, plen, "SAE ");
    if (wpa->akm_ft_psk)        csnprintf(pbuf, plen, "FT-PSK ");
    if (wpa->akm_ft_sae)        csnprintf(pbuf, plen, "FT-SAE ");

    return cpy_wpa_key_mgmt;
}

char *
osw_hostap_conf_pairwise_from_osw(const struct osw_wpa *wpa)
{
    size_t len = 32;
    size_t *plen = &len;
    char *pairwise = CALLOC(len, sizeof(char));
    char *cpy_pairwise = pairwise;
    char **pbuf = &cpy_pairwise;

    if (wpa == NULL)
        return NULL;

    if (wpa->pairwise_tkip) csnprintf(pbuf, plen, "TKIP ");
    if (wpa->pairwise_ccmp) csnprintf(pbuf, plen, "CCMP ");

    return pairwise;
}

char *
osw_hostap_conf_proto_from_osw(const struct osw_wpa *wpa)
{
    size_t len = 16;
    size_t *plen = &len;
    char *proto = CALLOC(len, sizeof(char));
    char *cpy_proto = proto;
    char **pbuf = &cpy_proto;

    if (wpa == NULL)
        return NULL;

    if (wpa->wpa) csnprintf(pbuf, plen, "WPA ");
    if (wpa->rsn) csnprintf(pbuf, plen, "RSN ");

    return proto;
}

bool
osw_hostap_util_proto_to_osw(const char *proto,
                             struct osw_wpa *wpa)
{
    if (proto == NULL || wpa == NULL)
        return false;

    if (strstr(proto, "WPA")) wpa->wpa = true;
    if (strstr(proto, "RSN")) wpa->rsn = true;
    return true;
}

bool
osw_hostap_util_pairwise_to_osw(const char *pairwise,
                                struct osw_wpa *wpa)
{
    if (pairwise == NULL || wpa == NULL)
        return false;

    if (strstr(pairwise, "TKIP")) wpa->pairwise_tkip = true;
    if (strstr(pairwise, "CCMP")) wpa->pairwise_ccmp = true;
    return true;
}

bool
osw_hostap_util_ssid_to_osw(const char *ssid_str,
                            struct osw_ssid *ssid)
{
    if (ssid_str == NULL || ssid == NULL)
        return false;

    if (ssid->len > 0) {
        /* don't override non-empty values */
        return true;
    }

    if (strlen(ssid_str) < 1) {
        ssid->len = 0;
        return false;
    }

    STRSCPY(ssid->buf, ssid_str);
    ssid->len = strlen(ssid->buf);
    return true;
}

bool
osw_hostap_util_sta_freq_to_channel(const char *freq,
                                    struct osw_channel *channel)
{
    if (freq == NULL || channel == NULL)
        return false;

    const int int_freq = atoi(freq);
    if (int_freq == 0) return false;

    channel->control_freq_mhz = int_freq;
    return true;
}

bool
osw_hostap_util_sta_state_to_osw(const char *wpa_state,
                                 enum osw_drv_vif_state_sta_link_status *status)
{
    if (wpa_state == NULL || status == NULL)
        return false;

    if ((strcmp(wpa_state, "DISCONNECTED") == 0) ||
        (strcmp(wpa_state, "INACTIVE") == 0) ||
        (strcmp(wpa_state, "INTERFACE_DISABLED") == 0) ) {
        *status = OSW_DRV_VIF_STATE_STA_LINK_DISCONNECTED;
        return true;
    }

    if ((strcmp(wpa_state, "AUTHENTICATING") == 0) ||
        (strcmp(wpa_state, "ASSOCIATING") == 0) ||
        (strcmp(wpa_state, "ASSOCIATED") == 0) ||
        (strcmp(wpa_state, "SCANNING") == 0) ||
        (strcmp(wpa_state, "4WAY_HANDSHAKE") == 0) ||
        (strcmp(wpa_state, "GROUP_HANDSHAKE") == 0) ) {
        *status = OSW_DRV_VIF_STATE_STA_LINK_CONNECTING;
        return true;
    }

    if (strcmp(wpa_state, "COMPLETED") == 0) {
        *status = OSW_DRV_VIF_STATE_STA_LINK_CONNECTED;
        return true;
    }

    if (strcmp(wpa_state, "UNKNOWN") == 0) {
        *status = OSW_DRV_VIF_STATE_STA_LINK_UNKNOWN;
        return true;
    }

    return false;
}

bool
osw_hostap_util_ieee80211w_to_osw(const char *ieee80211w,
                                  struct osw_wpa *osw_wpa)
{
    if (ieee80211w == NULL || osw_wpa == NULL)
        return false;

    if (strlen(ieee80211w) < 1) return false;

    osw_wpa->pmf = osw_hostap_conf_pmf_to_osw(atoi(ieee80211w));
    return true;
}

bool
osw_hostap_util_wpa_key_mgmt_to_osw(const char *wpa_key_mgmt,
                                    struct osw_wpa *osw_wpa)
{
    if (wpa_key_mgmt == NULL || osw_wpa == NULL)
        return false;

    if (strstr(wpa_key_mgmt, "WPA-PSK")) osw_wpa->akm_psk = true;
    if (strstr(wpa_key_mgmt, "FT-PSK"))  osw_wpa->akm_ft_psk = true;
    if (strstr(wpa_key_mgmt, "SAE"))     osw_wpa->akm_sae = true;
    if (strstr(wpa_key_mgmt, "FT-SAE"))  osw_wpa->akm_ft_sae = true;
    /* only for wpa_supplicant */
    if (strstr(wpa_key_mgmt, "WPA2-PSK")) osw_wpa->akm_psk = true;

    return true;
}

bool
osw_hostap_util_key_mgmt_to_osw(const char *key_mgmt,
                                struct osw_wpa *wpa)
{
    if (key_mgmt == NULL || wpa == NULL)
        return false;

    /* FIXME: EAP, DPP, etc. */

    if (strcmp(key_mgmt, "WPA2-PSK+WPA-PSK") == 0) {
        wpa->wpa = true;
        wpa->rsn = true;
        wpa->akm_psk = true;
    }
    if (strcmp(key_mgmt, "WPA2-PSK") == 0) {
        wpa->rsn = true;
        wpa->akm_psk = true;
    }
    if (strcmp(key_mgmt, "WPA-PSK") == 0) {
        wpa->wpa = true;
        wpa->akm_psk = true;
    }
    if (strcmp(key_mgmt, "NONE") == 0) {
        /* nop */
    }
    if (strcmp(key_mgmt, "WPA-NONE") == 0) {
        /* nop */
    }
    if (strcmp(key_mgmt, "FT-PSK") == 0) {
        wpa->rsn = true;
        wpa->akm_ft_psk = true;
    }
    if (strcmp(key_mgmt, "FT-SAE") == 0) {
        wpa->rsn = true;
        wpa->akm_ft_sae = true;
    }
    if (strcmp(key_mgmt, "SAE") == 0) {
        wpa->rsn = true;
        wpa->akm_sae = true;
    }

    return true;
}

bool
osw_hostap_util_unquote(const char *original,
                        char *unquoted)
{
    if (original == NULL || unquoted == NULL)
        return false;

    char *orig = STRDUP(original);
    if (strlen(orig) < 2)  goto nop;
    if (orig[strlen(orig) - 1] == '"') {
        /* trailing */
        orig[strlen(orig) - 1] = '\0';
        /* remove leading while copying */
        memcpy(unquoted, &orig[1], strlen(orig));
        return true;
    }
nop:
    FREE(orig);
    memcpy(unquoted, original, strlen(original));
    return true;
}

