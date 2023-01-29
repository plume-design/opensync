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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* opensync */
#include <log.h>
#include <memutil.h>
#include <os.h>
#include <util.h>

/* unit */
#include <osw_drv.h>
#include <osw_hostap_common.h>
#include <osw_hostap_conf.h>
#include <osw_types.h>

#define OSW_CONF_HAPD_DRIVER "nl80211"

/* Some Operating Systems, including at least some
 * Windows revisions at the time of writing, are known
 * to show up a WPS-PIN entry in the UI instead of
 * WPA-PSK/SAE passphrase entry when selecting a network.
 * This was confusing to users. One way to prevent that
 * is to use a very specific device_type string that gets
 * advertised through WPS IE.
 */
#define WPS_DEVICE_TYPE_STR "6-0050F204-1"

#ifdef HOSTSAPD_HANDLES_ACL
static enum osw_acl_policy
osw_hostap_conf_acl_policy_to_osw(enum osw_hostap_conf_acl_policy policy)
{
    switch (policy) {
        case OSW_HOSTAP_CONF_ACL_DENY_LIST:      return OSW_ACL_DENY_LIST;
        case OSW_HOSTAP_CONF_ACL_ACCEPT_LIST:    return OSW_ACL_ALLOW_LIST;
        case OSW_HOSTAP_CONF_ACL_USE_EXT_RADIUS: return OSW_ACL_NONE;
    }
    /* unreachable */
    return OSW_ACL_DENY_LIST;
}

static enum osw_hostap_conf_acl_policy
osw_hostap_conf_acl_policy_from_osw(enum osw_acl_policy policy)
{
    switch (policy) {
        /* not really able to reflect NONE in hostapd */
        case OSW_ACL_NONE:       return OSW_HOSTAP_CONF_ACL_DENY_LIST;
        case OSW_ACL_DENY_LIST:  return OSW_HOSTAP_CONF_ACL_DENY_LIST;
        case OSW_ACL_ALLOW_LIST: return OSW_HOSTAP_CONF_ACL_ACCEPT_LIST;
    }
    /* unreachable */
    return OSW_HOSTAP_CONF_ACL_DENY_LIST;
}
#endif /* HOSTAPD_HANDLES_ACL */

static const char*
osw_hostap_conf_hwmode_from_band(enum osw_band band)
{
    /* naive FIXME */
    switch (band) {
        case OSW_BAND_UNDEFINED: return "any";
        case OSW_BAND_2GHZ:      return "g";
        case OSW_BAND_5GHZ:      /* forward */
        case OSW_BAND_6GHZ:      return "a";
    }
    /* unreachable */
    return "any";
}

#if 0
static int
osw_hostap_conf_rate_from_osw(enum osw_rate_legacy rate)
{
    /* WARNING! This function returns values as defined
     * in 802.11-2020 spec - in units of 500kbps. Hostapd
     * excepts this value in units of 100kbps (multiply
     * return value by 5 before appending to hostpad.conf) */
    /* dot11 Supported Data Rates Tx Table */
    switch (rate) {
        case OSW_RATE_CCK_1_MBPS:   return 0x02;
        case OSW_RATE_CCK_2_MBPS:   return 0x04;
        case OSW_RATE_CCK_5_5_MBPS: return 0x0B;
        case OSW_RATE_CCK_11_MBPS:  return 0x16;

        case OSW_RATE_OFDM_6_MBPS:  return 0x0C;
        case OSW_RATE_OFDM_9_MBPS:  return 0x12;
        case OSW_RATE_OFDM_12_MBPS: return 0x18;
        case OSW_RATE_OFDM_18_MBPS: return 0x24;
        case OSW_RATE_OFDM_24_MBPS: return 0x30;
        case OSW_RATE_OFDM_36_MBPS: return 0x48;
        case OSW_RATE_OFDM_48_MBPS: return 0x60;
        case OSW_RATE_OFDM_54_MBPS: return 0x6C;
    }
    /* unreachable */
    return OSW_RATE_OFDM_6_MBPS;
}
#endif

#if 0
static enum osw_rate_legacy
osw_hostap_conf_rate_to_osw(int rate)
{
    /* dot11 Supported Data Rates Tx Table */
    if (rate == 0x02) return OSW_RATE_CCK_1_MBPS;
    if (rate == 0x04) return OSW_RATE_CCK_2_MBPS;
    if (rate == 0x0B) return OSW_RATE_CCK_5_5_MBPS;
    if (rate == 0x16) return OSW_RATE_CCK_11_MBPS;

    if (rate == 0x0C) return OSW_RATE_OFDM_6_MBPS;
    if (rate == 0x12) return OSW_RATE_OFDM_9_MBPS;
    if (rate == 0x18) return OSW_RATE_OFDM_12_MBPS;
    if (rate == 0x24) return OSW_RATE_OFDM_18_MBPS;
    if (rate == 0x30) return OSW_RATE_OFDM_24_MBPS;
    if (rate == 0x48) return OSW_RATE_OFDM_36_MBPS;
    if (rate == 0x60) return OSW_RATE_OFDM_48_MBPS;
    if (rate == 0x6C) return OSW_RATE_OFDM_54_MBPS;

    return OSW_RATE_OFDM_6_MBPS;
}
#endif

#if 0
static bool
osw_hostap_conf_set_rates_to_hapd(const struct osw_rateset_legacy *rateset,
                                  char *rates)
{
    size_t i;
    char *ptr = rates;
    size_t len = 128 * sizeof(char);

    if (rateset == NULL) return false;

    for (i = 0; i < rateset->n_rates; i++) {
        csnprintf(&ptr, &len, "%d ", osw_hostap_conf_rate_from_osw(rateset->rates[i]) * 5 );
    }

    return true;
}
#endif

static bool
hapd_util_vif_enabled_to_osw(const char *state,
                             bool *enabled)
{
    if (strstr(state, "ENABLED") || \
        strstr(state, "COUNTRY_UPDATE") || \
        strstr(state, "ACS") || \
        strstr(state, "HT_SCAN") || \
        strstr(state, "DFS") ) {
        *enabled = true;
        return true;
    }

    if (strstr(state, "DISABLED") || \
        strstr(state, "UNINITIALIZED") || \
        strstr(state, "UNKNOWN") ) {
        *enabled = false;
        return true;
    }

    return false;
}

static bool
hapd_util_hapd_psk_file_to_osw(const char *wpa_psk_file,
                               struct osw_ap_psk_list *output_psk_list)
{
    int len;
    char *line;
    struct osw_ap_psk_list psk_list;
    struct osw_ap_psk psks[20];
    char *local_wpa_psk_file;

    if (wpa_psk_file == NULL || output_psk_list == NULL)
        return false;
    if (strlen(wpa_psk_file) < 1) return false;

    local_wpa_psk_file = STRDUP(wpa_psk_file);
    psk_list.list = psks;
    psk_list.count = 0;

    line = strtok(local_wpa_psk_file, "\n");

    do {
        int wps = 0;
        int oftag = 0;
        int keyid = 0;
        char *param = NULL;
        char psk[64] = {0};
        struct osw_hwaddr hwaddr = {0};

        /* ignore commented lines */
        if (line[0] == '#') continue;

        param = strsep(&line, " \n");

        do {
            if (strstr(param, "oftag=")) {
                len = sscanf(param, "oftag=home-%d", &oftag);
                continue;
            }
            if (strstr(param, "wps=")) {
                len = sscanf(param, "wps=%d", &wps);
                continue;
            }
            if (strstr(param, "keyid=")) {
                len = sscanf(param, "keyid=key-%d", &keyid);
                if (len != 1) return false;
                continue;
            }
            if ((len = sscanf(param, OSW_HWADDR_FMT, OSW_HWADDR_SARG(&hwaddr))) == 6) {
                continue;
            }
            /* On last iteration (we expect psk to be the last token
             * in a line) reversed if logic */
            if ((len = sscanf(param, "%64s", psk)) != 1) {
                /* handle error here */
                return false;
            }

            psk_list.list[psk_list.count].key_id = keyid;
            STRSCPY_WARN(psk_list.list[psk_list.count].psk.str, psk);
            psk_list.count++;
        } while ((param = strsep(&line, " \n")));
    } while ((line = strtok(NULL, "\n")));

    FREE(local_wpa_psk_file);
    /* Initialize memory - caller becomes the owner when this function returns! */
    output_psk_list->list = CALLOC(psk_list.count, sizeof(struct osw_ap_psk));
    memcpy(output_psk_list->list, psk_list.list, psk_list.count * sizeof(struct osw_ap_psk));
    output_psk_list->count = psk_list.count;

    return true;
}

static bool
hapd_util_hapd_sae_password_to_osw(const char *sae_password,
                                   struct osw_ap_psk_list *psk_list)
{
    if (strlen(sae_password) < 1) return false;

    if(psk_list->list == NULL) {
        psk_list->list = CALLOC(1, sizeof(struct osw_ap_psk));
        if (psk_list->list == NULL) return false;
    }

    STRSCPY_WARN(psk_list->list->psk.str, sae_password);
    psk_list->list->key_id = 0;
    psk_list->count = 1;

    return true;
}

static bool
hapd_util_bridge_name_to_osw(const char *bridge,
                             struct osw_ifname *brname)
{
    if (bridge == NULL || brname == NULL)
        return false;

    if (strlen(bridge) < 1) return false;

    STRSCPY_WARN(brname->buf, bridge);
    return true;
}

static bool
hapd_util_wps_status_to_osw(const char *wps,
                            bool *output_wps)
{
    if (wps == NULL || output_wps == NULL)
        return false;

    if (strlen(wps) < 1) return false;

    if (strcmp(wps, "disabled") == 0)
        *output_wps = false;

    /* strstr because it's either 'configured' or 'not configurad' */
    if (strstr(wps, "configured"))
        *output_wps = true;

    return true;
}

#if 0
static bool
hapd_util_supp_rates_to_osw(const char *rates_list,
                            struct osw_rateset_legacy *rates)
{
    /* TODO fix this mess*/
    unsigned int a;
    char *token;
    enum osw_rate_legacy *ptr;
    char *rates_list = STRDUP(rates_list);

    if (strlen(rates_list) < 2) return false;
    /* WARNING! The caller becomes owner of allocated memory! */
    rates->rates = CALLOC(12, sizeof(enum osw_rate_legacy));
    ptr = rates->rates;
    token = strtok(rates_list, " ");
    do {
        sscanf(token, "%x", &a);
        *(ptr++) = osw_hostap_conf_rate_to_osw(a);
        rates->n_rates++;
    } while ((token = strtok(NULL, " ")));

    FREE(rates_list);
    return true;
}
#endif

static bool
hapd_util_bssid_cstr_to_osw(const char *str,
                            struct osw_hwaddr *bssid)
{
    if (bssid == NULL)
        return false;

    if (osw_hwaddr_is_zero(bssid) == false) {
        /* don't override non-empty values */
        return true;
    }

    if (str == NULL)
        return false;

    return osw_hwaddr_from_cstr(str, bssid);
}


static bool
hapd_util_hapd_chwidth_to_osw(const char *oper_chwidth,
                              enum osw_channel_width *width)
{
    if (oper_chwidth == NULL || width == NULL)
        return false;

    *width = osw_hostap_conf_chwidth_to_osw(atoi(oper_chwidth));
    return true;
}

static void
osw_hostap_conf_hapd_wpa_to_osw(const int wpa,
                                struct osw_wpa *osw_wpa)
{
    if (osw_wpa == NULL)
        return;

    osw_wpa->wpa = ((wpa & 0x01) > 0 ? true : false);
    osw_wpa->rsn = ((wpa & 0x02) > 0 ? true : false);
}

static bool
hapd_util_hapd_wpa_to_osw(const char *wpa,
                          struct osw_wpa *osw_wpa)
{
    if (wpa == NULL || osw_wpa == NULL)
        return false;

    const int tmp_wpa = atoi(wpa);
    osw_hostap_conf_hapd_wpa_to_osw(tmp_wpa, osw_wpa);
    return true;
}

static void
osw_hostap_conf_osw_vif_config_to_base(const struct osw_drv_vif_config *vconf,
                                       struct osw_hostap_conf_ap_config *conf)
{
    OSW_HOSTAP_CONF_SET_BUF(conf->interface, vconf->vif_name);
    OSW_HOSTAP_CONF_SET_BUF(conf->driver, OSW_CONF_HAPD_DRIVER);
    OSW_HOSTAP_CONF_SET_BUF(conf->bridge, vconf->u.ap.bridge_if_name.buf);
    OSW_HOSTAP_CONF_SET_VAL(conf->logger_syslog, -1);
    OSW_HOSTAP_CONF_SET_VAL(conf->logger_syslog_level, 3);
}

static void
osw_hostap_conf_osw_wpa_to_wpa(const struct osw_drv_vif_config_ap *ap,
                               struct osw_hostap_conf_ap_config *conf)
{
    OSW_HOSTAP_CONF_SET_VAL(conf->wpa, osw_hostap_conf_wpa_from_osw(&ap->wpa));
}

static void
osw_hostap_conf_osw_wpa_to_wpa_rekey(const struct osw_drv_vif_config_ap *ap,
                                     struct osw_hostap_conf_ap_config *conf)
{
    if (ap->wpa.group_rekey_seconds == OSW_WPA_GROUP_REKEY_UNDEFINED)
        return;

    OSW_HOSTAP_CONF_SET_VAL(conf->wpa_group_rekey, ap->wpa.group_rekey_seconds);
}

static void
osw_hostap_conf_osw_wpa_to_wpa_key_mgmt(const struct osw_drv_vif_config_ap *ap,
                                        struct osw_hostap_conf_ap_config *conf)
{
    char wpa_key_mgmt[128] = {0};
    if (ap->wpa.akm_psk)           STRSCAT(wpa_key_mgmt, "WPA-PSK ");
    if (ap->wpa.akm_sae)           STRSCAT(wpa_key_mgmt, "SAE ");
    if (ap->wpa.akm_ft_psk)        STRSCAT(wpa_key_mgmt, "FT-PSK ");
    if (ap->wpa.akm_ft_sae)        STRSCAT(wpa_key_mgmt, "FT-SAE ");
#if 0
    if (ap->wpa.akm_eap)           STRSCAT(wpa_key_mgmt, "WPA-EAP ");
    if (ap->wpa.akm_eap_sha256)    STRSCAT(wpa_key_mgmt, "WPA-EAP-SHA256 ");
    if (ap->wpa.akm_psk_sha256)    STRSCAT(wpa_key_mgmt, "WPA-PSK-SHA256 ");
    if (ap->wpa.akm_suite_b192)    STRSCAT(wpa_key_mgmt, "WPA-EAP-SUITE-B-192 ");
    if (ap->wpa.akm_ft_eap)        STRSCAT(wpa_key_mgmt, "FT-EAP ");
    if (ap->wpa.akm_ft_eap_sha384) STRSCAT(wpa_key_mgmt, "FT-EAP-SHA384 ");
#endif
    /* commit */
    OSW_HOSTAP_CONF_SET_BUF(conf->wpa_key_mgmt, wpa_key_mgmt);
}

static bool
hapd_util_hapd_pairwise_to_osw(const char *pairwise,
                               struct osw_wpa *osw_wpa)
{
    if (!pairwise) return false;
    if (strstr(pairwise, "TKIP")) osw_wpa->pairwise_tkip = true;
    if (strstr(pairwise, "CCMP")) osw_wpa->pairwise_ccmp = true;
    return true;
}

static void
osw_hostap_conf_osw_wpa_to_pairwise(const struct osw_drv_vif_config_ap *ap,
                                    struct osw_hostap_conf_ap_config *conf)
{
    char wpa_pairwise[16] = {0};

    if (ap->wpa.pairwise_tkip) STRSCAT(wpa_pairwise, "TKIP ");
    if (ap->wpa.pairwise_ccmp) STRSCAT(wpa_pairwise, "CCMP ");
    /* commit */
    if (strlen(wpa_pairwise)) OSW_HOSTAP_CONF_SET_BUF(conf->wpa_pairwise, wpa_pairwise);
}

static void
osw_hostap_conf_osw_wpa_to_pmf(const struct osw_drv_vif_config_ap *ap,
                               struct osw_hostap_conf_ap_config *conf)
{
    enum osw_hostap_conf_pmf pmf;
    pmf = osw_hostap_conf_pmf_from_osw(&ap->wpa);
    /* commit */
    OSW_HOSTAP_CONF_SET_VAL(conf->ieee80211w, pmf);
}

static void
osw_hostap_conf_osw_wpa_to_auth_algs(const struct osw_drv_vif_config_ap *ap,
                                     struct osw_hostap_conf_ap_config *conf)
{
    int auth_algs = osw_hostap_conf_auth_algs_from_osw(&ap->wpa);
    /* commit */
    OSW_HOSTAP_CONF_SET_VAL(conf->auth_algs, auth_algs);
}

static void
osw_hostap_conf_osw_wpa_to_width(const struct osw_drv_vif_config_ap *ap,
                                 struct osw_hostap_conf_ap_config *conf)
{
    enum osw_hostap_conf_chanwidth chanwidth;
    chanwidth = osw_hostap_conf_chwidth_from_osw(ap->channel.width);

    if (ap->mode.vht_enabled) OSW_HOSTAP_CONF_SET_VAL(conf->vht_oper_chwidth, chanwidth);
    if (ap->mode.he_enabled) OSW_HOSTAP_CONF_SET_VAL(conf->he_oper_chanwidth, chanwidth);
}

static void
osw_hostap_conf_osw_wpa_to_country_code(const struct osw_drv_phy_config *phy,
                                        struct osw_hostap_conf_ap_config *conf)
{
    if (strlen(phy->reg_domain.ccode) > 1) {
        OSW_HOSTAP_CONF_SET_BUF(conf->country_code, phy->reg_domain.ccode);
        OSW_HOSTAP_CONF_SET_VAL(conf->ieee80211d, 1);
        OSW_HOSTAP_CONF_SET_VAL(conf->ieee80211h, 1);
    }
}

static void
osw_hostap_conf_osw_wpa_to_hwmode(const struct osw_drv_vif_config_ap *ap,
                                  struct osw_hostap_conf_ap_config *conf)
{
    enum osw_band band = osw_freq_to_band(ap->channel.control_freq_mhz);
    const char* hwmode = osw_hostap_conf_hwmode_from_band(band);
    /* commit */
    OSW_HOSTAP_CONF_SET_BUF(conf->hw_mode, hwmode);
}

static void
osw_hostap_conf_osw_wpa_to_channel(const struct osw_drv_vif_config_ap *ap,
                                   struct osw_hostap_conf_ap_config *conf)
{
    int channel = osw_freq_to_chan(ap->channel.control_freq_mhz);
    int center0 = osw_freq_to_chan(ap->channel.center_freq0_mhz);
    int center1 = osw_freq_to_chan(ap->channel.center_freq1_mhz);

    if (channel != 0) OSW_HOSTAP_CONF_SET_VAL(conf->channel, channel);

    if (ap->mode.vht_enabled) {
        if (center0 != 0) OSW_HOSTAP_CONF_SET_VAL(conf->vht_oper_centr_freq_seg0_idx, center0);
        if (center1 != 0) OSW_HOSTAP_CONF_SET_VAL(conf->vht_oper_centr_freq_seg1_idx, center1);
    }
    if (ap->mode.he_enabled) {
        if (center0 != 0) OSW_HOSTAP_CONF_SET_VAL(conf->he_oper_centr_freq_seg0_idx, center0);
        if (center1 != 0) OSW_HOSTAP_CONF_SET_VAL(conf->he_oper_centr_freq_seg1_idx, center1);
    }
}


static void
osw_hostap_conf_osw_wpa_to_ieee80211(const struct osw_drv_vif_config_ap *ap,
                                     struct osw_hostap_conf_ap_config *conf)
{
    //char supp_rates[128] = {0};
    //char basic_rates[128] = {0};

    //osw_hostap_conf_set_rates_to_hapd(&ap->mode.supported_rates, supp_rates);
    //osw_hostap_conf_set_rates_to_hapd(&ap->mode.basic_rates, basic_rates);

    OSW_HOSTAP_CONF_SET_BUF(conf->ssid, ap->ssid.buf);
    OSW_HOSTAP_CONF_SET_VAL(conf->beacon_int, ap->beacon_interval_tu);
    //OSW_HOSTAP_CONF_SET_BUF(conf->supported_rates, supp_rates);
    //OSW_HOSTAP_CONF_SET_BUF(conf->basic_rates, basic_rates);
    /* conf->basic_rates_rates - FIXME */
    /* conf->beacon_rate - FIXME */
    OSW_HOSTAP_CONF_SET_VAL(conf->ignore_broadcast_ssid, ap->ssid_hidden);
    OSW_HOSTAP_CONF_SET_VAL(conf->wmm_enabled, ap->mode.wmm_enabled);
    OSW_HOSTAP_CONF_SET_VAL(conf->uapsd_advertisement_enabled, ap->mode.wmm_uapsd_enabled);
    OSW_HOSTAP_CONF_SET_VAL(conf->ap_isolate, ap->isolated);
    /* conf->multi_ap - FIXME - see commit hash: f1e5989ec4 */
    // this needs a is_X_supported guard; this is not gauranteed to be there
    // OSW_HOSTAP_CONF_SET_VAL(conf->mcast_to_ucast, ap->mcast2ucast);
}

static void
osw_hostap_conf_osw_wpa_to_op_class(const struct osw_drv_vif_config_ap *ap,
                             struct osw_hostap_conf_ap_config *conf)
{
    uint8_t op_class;
    if (osw_channel_to_op_class(&ap->channel, &op_class))
        OSW_HOSTAP_CONF_SET_VAL(conf->op_class, op_class);
}

static void
osw_hostap_conf_osw_wpa_to_ieee80211n(const struct osw_drv_vif_config_ap *ap,
                               struct osw_hostap_conf_ap_config *conf)
{
    if (ap->mode.ht_enabled) {
        OSW_HOSTAP_CONF_SET_VAL(conf->ieee80211n, ap->mode.ht_enabled);
        OSW_HOSTAP_CONF_SET_VAL(conf->require_ht, ap->mode.ht_required);
    }
}

static const char *
osw_hostap_conf_osw_chan_to_ht40_capab(const struct osw_channel *c)
{
    /* Any >20MHz channels will have different control and
     * center freqs..
     */
    if (c->center_freq0_mhz == c->control_freq_mhz) return "";

    /* .. but using their relation alone isn't sufficient
     * alone. It needs a bit more calculations to infer the
     * HT40+/-.
     */
    const enum osw_band band = osw_channel_to_band(c);
    const int pri_chan = osw_freq_to_chan(c->control_freq_mhz);
    const int *chans = osw_channel_sidebands(band, pri_chan, 40, 11);

    int center_chan = 0;
    size_t n = 0;
    while (*chans) {
        center_chan += *chans;
        n++;
        chans++;
    }
    if (n == 0) return "";

    center_chan /= n;
    if (center_chan < pri_chan) return "[HT40-]";
    if (center_chan > pri_chan) return "[HT40+]";
    return "";
}

static void
osw_hostap_conf_osw_wpa_to_ht_capab(const struct osw_drv_vif_config_ap *ap,
                                    struct osw_hostap_conf_ap_config *conf)
{
    if (ap->mode.ht_enabled) {
        const struct osw_channel *c = &ap->channel;
        char capab[256];
        char *ptr = capab;
        size_t len = sizeof(capab);

        MEMZERO(capab);
        csnprintf(&ptr, &len, "%s", osw_hostap_conf_osw_chan_to_ht40_capab(c));

        OSW_HOSTAP_CONF_SET_BUF(conf->ht_capab, capab);

        /* FIXME - add more; this need to be an intersection
         * of what the device is capable of. */
    }
}

static void
osw_hostap_conf_osw_wpa_to_ieee80211ac(const struct osw_drv_vif_config_ap *ap,
                                       struct osw_hostap_conf_ap_config *conf)
{
    if (ap->mode.vht_enabled) {
        OSW_HOSTAP_CONF_SET_VAL(conf->ieee80211ac, ap->mode.vht_enabled);
        OSW_HOSTAP_CONF_SET_VAL(conf->require_vht, ap->mode.vht_required);
    }
}

static void
osw_hostap_conf_osw_wpa_to_ieee80211ax(const struct osw_drv_vif_config_ap *ap,
                                       struct osw_hostap_conf_ap_config *conf)
{
    if (ap->mode.he_enabled) {
        OSW_HOSTAP_CONF_SET_VAL(conf->ieee80211ax, ap->mode.he_enabled);
        //OSW_HOSTAP_CONF_SET_VAL(conf->require_vht, ap->mode.he_required);
    }
}

static void
osw_hostap_conf_osw_wpa_to_vht_capab(const struct osw_drv_vif_config_ap *ap,
                                     struct osw_hostap_conf_ap_config *conf)
{
    if (ap->mode.vht_enabled) {
        /* FIXME - not implemented */
    }
}

#ifdef HOSTAPD_HANDLES_ACL
static void
osw_hostap_conf_osw_wpa_to_acl_policy(const struct osw_drv_vif_config_ap *ap,
                                      struct osw_hostap_conf_ap_config *conf)
{
    if (ap->acl_policy == OSW_ACL_NONE) return;
    OSW_HOSTAP_CONF_SET_VAL(conf->macaddr_acl, osw_hostap_conf_acl_policy_from_osw(ap->acl_policy));
}

static void
osw_hostap_conf_osw_wpa_to_acl_list(const struct osw_drv_vif_config_ap *ap,
                                    struct osw_hostap_conf_ap_config *conf)
{
    size_t i;
    const struct osw_hwaddr *acl_list = ap->acl.list;

    if (ap->acl_policy == OSW_ACL_NONE) return;

    if (ap->acl_policy == OSW_ACL_DENY_LIST)
        OSW_HOSTAP_CONF_SET_BUF(conf->deny_mac_file, "/var/run/hostapd-...deny");

    if (ap->acl_policy == OSW_ACL_ALLOW_LIST)
        OSW_HOSTAP_CONF_SET_BUF(conf->accept_mac_file, "/var/run/hostapd-...accept");

    for (i = 0; i < ap->acl.count; i++) {
        if (strlen(conf->acl_buf) != 0)
            STRSCAT(conf->acl_buf, ",");
        STRSCAT(conf->acl_buf, strfmta(OSW_HWADDR_FMT, OSW_HWADDR_ARG(&acl_list[i])));
    }
}
#endif

static void
osw_hostap_conf_osw_wpa_to_psks(const struct osw_drv_vif_config_ap *ap,
                                struct osw_hostap_conf_ap_config *conf)
{
    size_t i;
    struct osw_ap_psk *psk = ap->psk_list.list;

    if (!(ap->wpa.akm_psk || ap->wpa.akm_ft_psk)) return;

    OSW_HOSTAP_CONF_SET_BUF(conf->wpa_psk_file, "SOMETHING");
    for (i = 0; i < ap->psk_list.count; i++) {
        if (ap->mode.wps)
            STRSCAT(conf->psks_buf, strfmta("wps=%d ", ap->mode.wps));
        STRSCAT(conf->psks_buf, strfmta("keyid=key-%d ", psk[i].key_id));
        STRSCAT(conf->psks_buf, strfmta("%s ", "00:00:00:00:00:00"));
        STRSCAT(conf->psks_buf, strfmta("%s\n", psk[i].psk.str));
    }
}

void
osw_hostap_conf_osw_wpa_to_sae(const struct osw_drv_vif_config_ap *ap,
                               struct osw_hostap_conf_ap_config *conf)
{
    enum osw_band band = osw_freq_to_band(ap->channel.control_freq_mhz);

    if (!ap->wpa.akm_sae) return;
    if (ap->psk_list.list == NULL) return;
    if (ap->psk_list.count < 1) return;

    OSW_HOSTAP_CONF_SET_BUF(conf->sae_password, ap->psk_list.list[0].psk.str);
    OSW_HOSTAP_CONF_SET_VAL(conf->sae_require_mfp, 1);
    if (band == OSW_BAND_6GHZ) OSW_HOSTAP_CONF_SET_VAL(conf->sae_pwe, 2);
}

void
osw_hostap_conf_osw_wpa_to_wps(const struct osw_drv_vif_config_ap *ap,
                               struct osw_hostap_conf_ap_config *conf)
{
    if (!ap->mode.wps) return;

    OSW_HOSTAP_CONF_SET_VAL(conf->wps_state, 2);
    OSW_HOSTAP_CONF_SET_VAL(conf->eap_server, true);
    OSW_HOSTAP_CONF_SET_BUF(conf->config_methods, "virtual_push_button");
    OSW_HOSTAP_CONF_SET_BUF(conf->device_type, WPS_DEVICE_TYPE_STR);
    OSW_HOSTAP_CONF_SET_VAL(conf->pbc_in_m1, true);
}

void
osw_hostap_conf_osw_wpa_to_btm(const struct osw_drv_vif_config_ap *ap,
                               struct osw_hostap_conf_ap_config *conf)
{
    OSW_HOSTAP_CONF_SET_VAL(conf->bss_transition, ap->mode.wnm_bss_trans);
}

void
osw_hostap_conf_osw_wpa_to_rrm(const struct osw_drv_vif_config_ap *ap,
                               struct osw_hostap_conf_ap_config *conf)
{
    OSW_HOSTAP_CONF_SET_VAL(conf->rrm_neighbor_report, ap->mode.rrm_neighbor_report);
}

static int
compare(const char* a, const char* b)
{
    /* FIXME enhance this function to compare tokenized sub-string (space separated)
     * to be able to match 'SAE WPA-PSK' and 'WPA-PSK SAE' as equal */
    return strcmp(a, b);
}

bool
osw_hostap_conf_fill_ap_config(struct osw_drv_conf *drv_conf,
                               const char *phy_name,
                               const char *vif_name,
                               struct osw_hostap_conf_ap_config *conf)
{
    struct osw_drv_phy_config *phy;
    struct osw_drv_vif_config *vif;
    struct osw_drv_vif_config_ap *ap;

    phy = osw_hostap_conf_phy_lookup(drv_conf, phy_name);
    if (phy == NULL)
        return false;

    vif = osw_hostap_conf_vif_lookup(phy, vif_name);
    if (vif == NULL)
        return false;

    ap = &vif->u.ap;

    memset(conf, 0, sizeof(*conf));

    osw_hostap_conf_osw_vif_config_to_base (vif, conf);
    osw_hostap_conf_osw_wpa_to_ieee80211   (ap, conf);
    osw_hostap_conf_osw_wpa_to_country_code(phy, conf);
    osw_hostap_conf_osw_wpa_to_hwmode      (ap, conf);
    osw_hostap_conf_osw_wpa_to_channel     (ap, conf);
    osw_hostap_conf_osw_wpa_to_width       (ap, conf);
    osw_hostap_conf_osw_wpa_to_op_class    (ap, conf);
#ifdef HOSTAPD_HANDLES_ACL
    osw_hostap_conf_osw_wpa_to_acl_policy  (ap, conf);
    osw_hostap_conf_osw_wpa_to_acl_list    (ap, conf);
#endif
    osw_hostap_conf_osw_wpa_to_auth_algs   (ap, conf);

    /*  IEEE 802.11n related configuration   */
    osw_hostap_conf_osw_wpa_to_ieee80211n  (ap, conf);
    osw_hostap_conf_osw_wpa_to_ht_capab    (ap, conf);

    /*  IEEE 802.11ac related configuration  */
    osw_hostap_conf_osw_wpa_to_ieee80211ac (ap, conf);
    osw_hostap_conf_osw_wpa_to_vht_capab   (ap, conf);

    /*  IEEE 802.11ax related configuration  */
    osw_hostap_conf_osw_wpa_to_ieee80211ax (ap, conf);

    /* RADIUS client configuration */
    /* FIXME */

    /*  WPA/IEEE 802.11i configuration       */
    osw_hostap_conf_osw_wpa_to_wpa         (ap, conf);
    osw_hostap_conf_osw_wpa_to_wpa_rekey   (ap, conf);
    osw_hostap_conf_osw_wpa_to_wpa_key_mgmt(ap, conf);
    osw_hostap_conf_osw_wpa_to_pairwise    (ap, conf);
    osw_hostap_conf_osw_wpa_to_pmf         (ap, conf);
    osw_hostap_conf_osw_wpa_to_psks        (ap, conf);
    osw_hostap_conf_osw_wpa_to_sae         (ap, conf);
    osw_hostap_conf_osw_wpa_to_wps         (ap, conf);

    /*  WPA/IEEE 802.11kv configuration       */
    osw_hostap_conf_osw_wpa_to_btm         (ap, conf);
    osw_hostap_conf_osw_wpa_to_rrm         (ap, conf);

    return true;
}

void
osw_hostap_conf_generate_ap_config_bufs(struct osw_hostap_conf_ap_config *conf)
{
    CONF_INIT(conf->conf_buf);
    /* hostapd configuration file */
    CONF_APPEND(interface, "%s");
    CONF_APPEND(bridge, "%s");
    CONF_APPEND(driver, "%s");
    CONF_APPEND(logger_syslog, "%d");
    CONF_APPEND(logger_syslog_level, "%d");
    CONF_APPEND(ctrl_interface, "%s");

    /* IEEE 802.11 related configuration */
    CONF_APPEND(ssid, "%s");
    CONF_APPEND(country_code, "%s");
    CONF_APPEND(ieee80211d, "%d");
    CONF_APPEND(ieee80211h, "%d");
    CONF_APPEND(hw_mode, "%s");
    CONF_APPEND(channel, "%d");
    CONF_APPEND(op_class, "%d");
    CONF_APPEND(beacon_int, "%d");
    //CONF_APPEND(supported_rates, "%s");
    //CONF_APPEND(basic_rates, "%s");
    //CONF_APPEND(beacon_rate, "%s");
    CONF_APPEND(accept_mac_file, "%s");
    CONF_APPEND(deny_mac_file, "%s");
    CONF_APPEND(auth_algs, "%d");
    CONF_APPEND(ignore_broadcast_ssid, "%d");
    CONF_APPEND(wmm_enabled, "%d");
    CONF_APPEND(uapsd_advertisement_enabled, "%d");
    CONF_APPEND(multi_ap, "%d");
    CONF_APPEND(ap_isolate, "%d");
    CONF_APPEND(mcast_to_ucast, "%d");
    CONF_APPEND(send_probe_response, "%d");

    /* IEEE 802.11n related configuration */
    CONF_APPEND(ieee80211n, "%d");
    CONF_APPEND(ht_capab, "%s");
    CONF_APPEND(require_ht, "%d");

    /* IEEE 802.11ac related configuration */
    CONF_APPEND(ieee80211ac, "%d");
    CONF_APPEND(vht_capab, "%s");
    CONF_APPEND(require_vht, "%d");
    CONF_APPEND(vht_oper_chwidth, "%d");
    CONF_APPEND(vht_oper_centr_freq_seg0_idx, "%d");
    CONF_APPEND(vht_oper_centr_freq_seg1_idx, "%d");

    /* IEEE 802.11ax related configuration */
    CONF_APPEND(ieee80211ax, "%d");
    // FIXME: This (and a bunch of others) need to be guarded by "is_X_supported" flags; hostapd can be built with(out) certain bits
    // CONF_APPEND(he_oper_chanwidth, "%d");
    CONF_APPEND(he_oper_centr_freq_seg0_idx, "%d");
    CONF_APPEND(he_oper_centr_freq_seg1_idx, "%d");

    /* IEEE 802.1X-2004 related configuration */
    CONF_APPEND(ieee8021x, "%d");
    CONF_APPEND(eapol_version, "%d");

    /* Integrated EAP server */
    CONF_APPEND(eap_server, "%d");

    /* RADIUS client configuration */
    /* FIXME  */

    /* WPA/IEEE 802.11i configuration */
    CONF_APPEND(wpa, "%d");
    CONF_APPEND(wpa_psk_file, "%s");
    CONF_APPEND(wpa_key_mgmt, "%s");
    CONF_APPEND(wpa_pairwise, "%s");
    CONF_APPEND(wpa_group_rekey, "%d");
    CONF_APPEND(rsn_preauth, "%d");
    CONF_APPEND(ieee80211w, "%d");
    CONF_APPEND(sae_password, "%s");
    CONF_APPEND(sae_require_mfp, "%d");
    CONF_APPEND(sae_pwe, "%d");

    /* IEEE 802.11r configuration */
    CONF_APPEND(mobility_domain, "%04x");
    /* FIXME */

    /* Wi-Fi Protected Setup (WPS) */
    CONF_APPEND(wps_state, "%d");
    CONF_APPEND(config_methods, "%s");
    CONF_APPEND(device_type, "%s");
    CONF_APPEND(pbc_in_m1, "%d");

    /* Device Provisioning Protocol */
    CONF_APPEND(dpp_connector, "%s");
    CONF_APPEND(dpp_csign_hex, "%s");
    CONF_APPEND(dpp_netaccesskey_hex, "%s");

    /* IEEE 802.11v-2011 */
    CONF_APPEND(bss_transition, "%d");

    /* Radio measurements / location */
    CONF_APPEND(rrm_neighbor_report, "%d");

    /* send_probe_response   FIXME - see commit hash: e011ad1078 */
    /* use_driver_iface_addr FIXME - see commit hash: 470635f906 */
    /* osw_hwaddr_list (acl) - not handled by hostapd */

    CONF_FINI();
    return ;
}

int
compare_configs(const char* old_config, const char* new_config)
{
    /* FIXME - not used at the moment */
    char reload_required[][20] = {
        "wpa_key_mgmt",
        "wpa"
    };

    size_t i;
    int ret = 0;

    if (!old_config || !new_config) {
        printf("something is NULL!\n");
        return 255;
    }

    for (i = 0; i < (sizeof(reload_required)/sizeof(reload_required[0])); i++) {
        const char* _a = ini_geta(old_config, reload_required[i]);
        const char* _b = ini_geta(new_config, reload_required[i]);
        if (!_a) return 1;
        if (!_b) return -1;
        if ((ret = compare(_a, _b)) != 0) return ret;
    }

    /* full reload required */
    //CONF_COMPARE(old_config, new_config, "wpa_key_mgmt");

    return 0;
}

static void
osw_hostap_conf_fill_ap_state_neighbors(const struct osw_hostap_conf_ap_state_bufs *bufs,
                                        struct osw_drv_vif_state *vstate)
{
    const char *show_neighbor = bufs->show_neighbor;
    struct osw_drv_vif_state_ap *ap = &vstate->u.ap;
    struct osw_neigh_list *neighbor_list;
    struct osw_neigh *neighbor;
    uint32_t bssid_info[4];
    char *cpy_show_neighbor;
    char *line;
    int matched = 0;

    if (show_neighbor == NULL) return;

    MEMZERO(bssid_info);

    neighbor_list = &ap->neigh_list;
    neighbor_list->count = 0;
    neighbor_list->list = CALLOC(1, sizeof(struct osw_neigh));
    cpy_show_neighbor = STRDUP(show_neighbor);

    for (line = strtok(cpy_show_neighbor, "\n");
         line != NULL;
         line = strtok(NULL, "\n")) {

        neighbor_list->list = REALLOC(neighbor_list->list,
                                     (neighbor_list->count + 1) * sizeof(struct osw_neigh));
        if (neighbor_list->list == NULL) return;

        neighbor = &neighbor_list->list[neighbor_list->count];

        matched = sscanf(line,
                         "%*s ssid=%*s "
                         "nr=%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"
                         "%02x%02x%02x%02x"
                         "%02hhx%02hhx%02hhx",
                         OSW_HWADDR_SARG(&neighbor->bssid),
                         &bssid_info[0],
                         &bssid_info[1],
                         &bssid_info[2],
                         &bssid_info[3],
                         &neighbor->op_class,
                         &neighbor->channel,
                         &neighbor->phy_type);

        neighbor->bssid_info = ((bssid_info[0]) |
                                (bssid_info[1] << 8 ) |
                                (bssid_info[2] << 16 ) |
                                (bssid_info[3] << 24 ));

        if (matched != 13) continue; /* FIXME - print error message */
        neighbor_list->count++;
    }
    FREE(cpy_show_neighbor);
}

void
osw_hostap_conf_fill_ap_state(const struct osw_hostap_conf_ap_state_bufs *bufs,
                              struct osw_drv_vif_state *vstate)
{
    const char *config = bufs->config;
    const char *get_config = bufs->get_config;
    const char *status = bufs->status;
    //const char *mib = bufs->mib;
    //const char *wps_get_status = bufs->wps_get_status;
    const char *wpa_psk_file = bufs->wpa_psk_file;
    struct osw_drv_vif_state_ap *ap = &vstate->u.ap;

    /* Fill in neighbors list */
    osw_hostap_conf_fill_ap_state_neighbors(bufs, vstate);

    STATE_GET_BY_FN(vstate->enabled,             status, "state",
                    hapd_util_vif_enabled_to_osw);
    STATE_GET_BY_FN(vstate->mac_addr,            status, "bssid",
                    hapd_util_bssid_cstr_to_osw);
    STATE_GET_BY_FN(vstate->mac_addr,            status, "bssid[0]",
                    hapd_util_bssid_cstr_to_osw);

    STATE_GET_INT(ap->beacon_interval_tu,        status, "beacon_int");

    STATE_GET_BOOL(ap->isolated,                 config, "ap_isolate");
    STATE_GET_BOOL(ap->ssid_hidden,              config, "ignore_broadcast_ssid");
    STATE_GET_BOOL(ap->mcast2ucast,              config, "multicast_to_unicast");
    STATE_GET_BY_FN(ap->bridge_if_name,          config, "bridge",
                    hapd_util_bridge_name_to_osw);
    //STATE_GET_BY_FN(ap->mode.supported_rates,    status, "supported_rates",
                    //hapd_util_supp_rates_to_osw);
    //STATE_GET_BY_FN(ap->mode.basic_rates,        status, "basic_rates",
                    //hapd_util_supp_rates_to_osw);
    /* FIXME - add beacon rates */
    STATE_GET_BOOL(ap->mode.wnm_bss_trans,       config, "bss_transition");
    STATE_GET_BOOL(ap->mode.rrm_neighbor_report, config, "rrm_neighbor_report");
    STATE_GET_BOOL(ap->mode.wmm_enabled,         config, "wmm_enabled");
    STATE_GET_BOOL(ap->mode.wmm_uapsd_enabled,   config, "uapsd_advertisement_enabled");

    STATE_GET_BOOL(ap->mode.ht_enabled,          status, "ieee80211n");
    STATE_GET_BOOL(ap->mode.vht_enabled,         status, "ieee80211ac");
    STATE_GET_BOOL(ap->mode.he_enabled,          status, "ieee80211ax");

    if (osw_freq_to_band(ap->channel.control_freq_mhz) == OSW_BAND_6GHZ) {
        /* FIXME: It looks like hostapd mis-advertises what
         * is really in the beacons. I suspect this is an
         * internal implementation quirk where some stuff in
         * hostapd simply assumes ht_enabled for some code
         * paths, and instead of reworking them, ht_enabled
         * is getting enabled implicitly internally. This
         * needs to be checked thoroughly though. I just
         * want to get this working with osw_confsync ASAP.
         */
        ap->mode.ht_enabled = false;
    }

    STATE_GET_BY_FN(ap->mode.wps,                get_config, "wps_state",
                    hapd_util_wps_status_to_osw);

    /* Every call for width overwrites the previous one.
     * This is because I couldn't decide on logic bahind
     * which one to report if more than one is available. */
    //STATE_GET_BY_FN(ap->channel.width, status, "vht_oper_chwidth",
                      //hapd_util_hapd_chwidth_to_osw);
    //STATE_GET_BY_FN(ap->channel.width, status, " he_oper_chwidth",
                      //hapd_util_hapd_chwidth_to_osw);
    //STATE_GET_BY_FN(ap->channel.width, status, "eht_oper_chwidth",
                      //hapd_util_hapd_chwidth_to_osw);

    /* FIXME - Octavian reports values out of the blue in hostapd_cli status output */
    //STATE_GET_INT(ap->channel.control_freq_mhz, status, "freq");
    (void)hapd_util_hapd_chwidth_to_osw(NULL, NULL); // FIXME

    STATE_GET_BY_FN(ap->ssid,                    status, "ssid",
                    osw_hostap_util_ssid_to_osw);
    STATE_GET_BY_FN(ap->ssid,                    status, "ssid[0]",
                    osw_hostap_util_ssid_to_osw);

    /* FIXME implement ACL */

    STATE_GET_BY_FN(ap->wpa,                     get_config, "wpa",
                    hapd_util_hapd_wpa_to_osw);
    STATE_GET_BY_FN(ap->wpa,                     get_config, "key_mgmt",
                    osw_hostap_util_wpa_key_mgmt_to_osw);
    /* It's safe to call below twice as pairwise_to_osw only
     * sets. This will result in sum of the two in ap->wpa */
    STATE_GET_BY_FN(ap->wpa,                     get_config, "wpa_pairwise_cipher",
                    hapd_util_hapd_pairwise_to_osw);
    STATE_GET_BY_FN(ap->wpa,                     get_config, "rsn_pairwise_cipher",
                    hapd_util_hapd_pairwise_to_osw);

    STATE_GET_INT(ap->wpa.group_rekey_seconds,   config, "wpa_group_rekey");
    STATE_GET_INT(ap->wpa.ft_mobility_domain,    config, "mobility_domain");

    STATE_GET_BY_FN(ap->wpa,                     config, "ieee80211w",
                    osw_hostap_util_ieee80211w_to_osw);

    STATE_GET_BY_FN(ap->psk_list,                wpa_psk_file, "",
                    hapd_util_hapd_psk_file_to_osw);

    STATE_GET_BY_FN(ap->psk_list,                config, "sae_password",
                    hapd_util_hapd_sae_password_to_osw);
}

