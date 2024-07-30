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
#include <ctype.h>

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

static void
osw_hostap_conf_set_rates_to_hapd(uint16_t rates, char *buf, size_t len)
{
    enum osw_rate_legacy rate;
    for (rate = 0; rates != 0 && rate < OSW_RATE_COUNT; rate++, rates >>= 1) {
        if (rates & 1) {
            const int halfmbps = osw_rate_legacy_to_halfmbps(rate);
            const int kbps = halfmbps * 5;
            /* 5.5 Mbps -> 11 -> 110 */
            if (kbps > 0) csnprintf(&buf, &len, "%d ", kbps);
        }
    }
}

static void
osw_hostap_conf_set_brate_to_hapd(const struct osw_beacon_rate *rate,
                                  char *buf,
                                  size_t len)
{
    const int halfmbps = osw_rate_legacy_to_halfmbps(rate->u.legacy);
    const int kbps = halfmbps * 5;

    switch (rate->type) {
        case OSW_BEACON_RATE_UNSPEC:
            return;
        case OSW_BEACON_RATE_ABG:
            if (osw_rate_is_invalid(rate->u.legacy)) return;
            csnprintf(&buf, &len, "%d", kbps);
            return;
        case OSW_BEACON_RATE_HT:
            csnprintf(&buf, &len, "ht:%d", rate->u.ht_mcs);
            return;
        case OSW_BEACON_RATE_VHT:
            csnprintf(&buf, &len, "vht:%d", rate->u.vht_mcs);
            return;
        case OSW_BEACON_RATE_HE:
            csnprintf(&buf, &len, "he:%d", rate->u.he_mcs);
            return;
    }
}

static bool
hapd_util_vif_enabled_to_osw(const char *state,
                             enum osw_vif_status *status)
{
    if (strstr(state, "ENABLED") || \
        strstr(state, "COUNTRY_UPDATE") || \
        strstr(state, "ACS") || \
        strstr(state, "HT_SCAN") || \
        strstr(state, "DFS") ) {
        osw_vif_status_set(status, OSW_VIF_ENABLED);
        return true;
    }

    if (strstr(state, "DISABLED") || \
        strstr(state, "UNINITIALIZED") || \
        strstr(state, "UNKNOWN") ) {
        /* fall through */
    }

    osw_vif_status_set(status, OSW_VIF_DISABLED);
    return false;
}

static void
hapd_util_wps_cred_append(struct osw_wps_cred_list *creds,
                          const char *psk)
{
    if (creds == NULL) return;

    creds->count++;
    const size_t idx = creds->count - 1;
    const size_t elem_size = sizeof(creds->list[0]);
    const size_t list_size = creds->count * elem_size;
    creds->list = REALLOC(creds->list, list_size);
    MEMZERO(creds->list[idx]);
    STRSCPY_WARN(creds->list[idx].psk.str, psk);
}

static void
hapd_util_ap_psk_list_append(struct osw_ap_psk_list *psks,
                             const char *psk,
                             const int key_id)
{
    if (psks == NULL) return;
    psks->count++;
    const size_t idx = psks->count - 1;
    const size_t elem_size = sizeof(psks->list[0]);
    const size_t list_size = psks->count * elem_size;
    psks->list = REALLOC(psks->list, list_size);
    MEMZERO(psks->list[idx]);
    psks->list[idx].key_id = key_id;
    STRSCPY_WARN(psks->list[idx].psk.str, psk);
}

static bool
hapd_util_hapd_psk_file_to_osw(const char *wpa_psk_file,
                               struct osw_ap_psk_list *psks,
                               struct osw_wps_cred_list *creds)
{
    char *local_wpa_psk_file = STRDUP(wpa_psk_file ?: "");
    char *lines = local_wpa_psk_file;

    FREE(psks->list);
    psks->list = NULL;
    psks->count = 0;

    FREE(creds->list);
    creds->list = NULL;
    creds->count = 0;

    char *line;
    while ((line = strsep(&lines, "\n")) != NULL) {
        int wps = 0;
        int oftag = 0;
        int keyid = 0;
        const char *psk = NULL;
        struct osw_hwaddr hwaddr = {0};

        /* ignore commented lines */
        if (line[0] == '#') continue;
        /* ignore empty lines */
        if (line[0] == '\0') continue;

        char *param;
        while ((param = strsep(&line, " ")) != NULL) {
            const bool is_attr = (strchr(param, '=') != NULL);
            if (is_attr) {
                const char *k = strsep(&param, "=");
                const char *v = strsep(&param, "");
                if (WARN_ON(k == NULL)) continue;
                if (WARN_ON(v == NULL)) continue;

                     if (strcmp(k, "oftag") == 0) sscanf(v, "home-%d", &oftag);
                else if (strcmp(k, "wps") == 0) wps = atoi(v);
                else if (strcmp(k, "keyid") == 0) sscanf(v, "key-%d", &keyid);
                else LOGW("unknown pskfile attribute: '%s' with value '%s'", k, v);
            }
            else {
                const bool valid = osw_hwaddr_from_cstr(param, &hwaddr);
                const bool not_valid = (valid == false);
                if (WARN_ON(not_valid)) break;

                const char *until_eol = strsep(&line, "");
                if (until_eol == NULL) break;

                const size_t size = sizeof(psks->list[0].psk.str);
                const size_t len = strnlen(until_eol, size);
                const bool too_long = (len == size);
                if (WARN_ON(too_long)) break;

                psk = until_eol;
            }
        }

        const bool usable = (psk != NULL);
        const bool not_usable = (usable == false);
        if (not_usable) continue;

        if (wps) {
            hapd_util_wps_cred_append(creds, psk);
        }

        hapd_util_ap_psk_list_append(psks, psk, keyid);
    }

    FREE(local_wpa_psk_file);

    return true;
}

static bool
hapd_util_hapd_mib_to_radius_list(const struct osw_hostap_conf_ap_state_bufs *bufs,
                                  struct osw_radius_list *radius_list,
                                  struct osw_radius_list *acct_list)
{
    if ((bufs->mib == NULL) || (strstr(bufs->mib, "radiusAuthServerAddress") == NULL)) {
        LOGT("osw: hostap: conf: MIB does not contain RADIUS information");
        return false;
    }

    if ((bufs->config == NULL) || (strstr(bufs->config, "auth_server_shared_secret") == NULL))
        LOGW("osw: hostap: conf: config does not contain RADIUS secret information");

    if (radius_list->list == NULL)
        radius_list->list = CALLOC(OSW_HOSTAP_CONF_RADIUS_MAX_COUNT,
                sizeof(struct osw_radius));

    if (acct_list->list == NULL)
        acct_list->list = CALLOC(OSW_HOSTAP_CONF_RADIUS_MAX_COUNT,
                sizeof(struct osw_radius));

    osw_radius_list_purge(radius_list);
    osw_radius_list_purge(acct_list);

    char *buf = strdupa(bufs->mib);
    char *conf_buf = strdupa(bufs->config);
    char *coursor = buf;
    char *coursor_conf = conf_buf;
    char *ip_addr;
    char *passphrase;
    while (((ip_addr = strstr(coursor, "radiusAuthServerAddress")) != NULL) &&
           (radius_list->count < OSW_HOSTAP_CONF_RADIUS_MAX_COUNT)) {
        char *ip = ini_geta(ip_addr, "radiusAuthServerAddress");
        char *port_ch = ini_geta(ip_addr, "radiusAuthClientServerPortNumber");
        if (ip != NULL && port_ch != NULL) {
            radius_list->list[radius_list->count].server = STRDUP(ip);
            /* MIB does not provide passphrase, so it has to come from the config instead */
            if ((passphrase = strstr(coursor_conf, "auth_server_shared_secret")) != NULL) {
                char *secret = ini_geta(passphrase, "auth_server_shared_secret");
                radius_list->list[radius_list->count].passphrase = STRDUP(secret);
                coursor_conf = passphrase + 1;
            } else {
                LOGW("osw: hostap: conf: failed to find Auth server secret in config");
                radius_list->list[radius_list->count].passphrase = STRDUP("secret_unknown");
            }
            radius_list->list[radius_list->count].port = atoi(port_ch);
            radius_list->count++;
        }
        coursor = ip_addr + 1;
    }

    coursor = buf;
    coursor_conf = conf_buf;
    while (((ip_addr = strstr(coursor, "radiusAccServerAddress")) != NULL) &&
           (acct_list->count < OSW_HOSTAP_CONF_RADIUS_MAX_COUNT)) {
        char *ip = ini_geta(ip_addr, "radiusAccServerAddress");
        char *port_ch = ini_geta(ip_addr, "radiusAccClientServerPortNumber");
        if (ip != NULL && port_ch != NULL) {
            acct_list->list[acct_list->count].server = STRDUP(ip);
            /* MIB does not provide passphrase, so it has to come from the config instead */
            if ((passphrase = strstr(coursor_conf, "acct_server_shared_secret")) != NULL) {
                char *secret = ini_geta(passphrase, "acct_server_shared_secret");
                acct_list->list[acct_list->count].passphrase = STRDUP(secret);
                coursor_conf = passphrase + 1;
            } else {
                LOGW("osw: hostap: conf: failed to find Accounting secret in config");
                acct_list->list[acct_list->count].passphrase = STRDUP("secret_unknown");
            }
            acct_list->list[acct_list->count].port = atoi(port_ch);
            acct_list->count++;
        }
        coursor = ip_addr + 1;
    }
    return true;
}

static bool
hapd_util_hapd_conf_to_passpoint(const struct osw_hostap_conf_ap_state_bufs *bufs,
                                 struct osw_passpoint *passpoint)
{
    const char *conf_buf = bufs->config;

    if (conf_buf == NULL) {
        LOGT("osw: hostap: conf: config buffer empty when filling passpoint");
        return false;
    }

    osw_passpoint_free_internal(passpoint);

    const char *hessid = ini_geta(conf_buf, "hessid");
    if (hessid != NULL) {
        STRSCPY_WARN(passpoint->hessid.buf, hessid);
        passpoint->hessid.len = strlen(hessid);
        passpoint->hs20_enabled = true;
    }
    const char *osu_ssid = ini_geta(conf_buf, "osu_ssid");
    if (osu_ssid != NULL) {
        STRSCPY_WARN(passpoint->osu_ssid.buf, osu_ssid);
        passpoint->osu_ssid.len = strlen(osu_ssid);
    }

    STATE_GET_BOOL(passpoint->hs20_enabled, conf_buf, "hs20");
    STATE_GET_BOOL(passpoint->osen, conf_buf, "osen");
    STATE_GET_BOOL(passpoint->asra, conf_buf, "asra");
    STATE_GET_INT(passpoint->ant, conf_buf, "access_network_type");
    STATE_GET_INT(passpoint->venue_group, conf_buf, "venue_group");
    STATE_GET_INT(passpoint->venue_type, conf_buf, "venue_type");
    STATE_GET_INT(passpoint->anqp_domain_id, conf_buf, "anqp_domain_id");
    /* FIXME this field doesn't exist in hostapd config
    STATE_GET_INT(passpoint->pps_mo_id, config, "??");
    */
    STATE_GET_INT(passpoint->t_c_timestamp, conf_buf, "hs20_t_c_timestamp");

    char *wan_metrics = ini_geta(conf_buf, "hs20_wan_metrics");
    if (wan_metrics != NULL) {
        char *wan_info = strsep(&wan_metrics, ":");
        if (wan_info != NULL) {
            unsigned int hex;
            sscanf(wan_info, "%2x", &hex);
            passpoint->adv_wan_status = (hex & OSW_PASSPOINT_ADV_WAN_STATUS);
            passpoint->adv_wan_symmetric = (hex & OSW_PASSPOINT_ADV_WAN_SYMMETRIC);
            passpoint->adv_wan_at_capacity = (hex & OSW_PASSPOINT_ADV_WAN_AT_CAP);
        }
    }

    char *str = ini_geta(conf_buf, "hs20_t_c_filename");
    if (str != NULL) {
        passpoint->t_c_filename = STRDUP(str);
    }

    str = ini_geta(conf_buf, "anqp_elem");
    if (str != NULL) {
        passpoint->anqp_elem = STRDUP(str);
    }

    size_t list_len;
    char **list = ini_get_multiple_str_sep(conf_buf, "domain_name", ",", &list_len);
    if (list != NULL) {
        passpoint->domain_list = list;
        passpoint->domain_list_len = list_len;
    }
    list = ini_get_multiple_str(conf_buf, "nai_realm", &list_len);
    if (list != NULL) {
        passpoint->nairealm_list = list;
        passpoint->nairealm_list_len = list_len;
    }
    list = ini_get_multiple_str(conf_buf, "roaming_consortium", &list_len);
    if (list != NULL) {
        passpoint->roamc_list = list;
        passpoint->roamc_list_len = list_len;
    }
    list = ini_get_multiple_str(conf_buf, "hs20_oper_friendly_name", &list_len);
    if (list != NULL) {
        passpoint->oper_fname_list = list;
        passpoint->oper_fname_list_len = list_len;
    }
    list = ini_get_multiple_str(conf_buf, "venue_name", &list_len);
    if (list != NULL) {
        passpoint->venue_name_list = list;
        passpoint->venue_name_list_len = list_len;
    }
    list = ini_get_multiple_str(conf_buf, "venue_url", &list_len);
    if (list != NULL) {
        passpoint->venue_url_list = list;
        passpoint->venue_url_list_len = list_len;
    }
    list = ini_get_multiple_str_sep(conf_buf, "anqp_3gpp_cell_net", ";", &list_len);
    if (list != NULL) {
        passpoint->list_3gpp_list = list;
        passpoint->list_3gpp_list_len = list_len;
    }

    int *int_list = ini_get_multiple_int(conf_buf, "network_auth_type", &list_len);
    if (list != NULL) {
        passpoint->net_auth_type_list = int_list;
        passpoint->net_auth_type_list_len = list_len;
    }

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
hapd_util_hapd_nas_identifier_to_osw(const char *nas_identifier,
                                     struct osw_nas_id *nas_id)
{
    if (strlen(nas_identifier) < 1) return false;
    STRSCPY_WARN(nas_id->buf, nas_identifier);
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

static int
hapd_util_from_osw_multi_ap(const struct osw_multi_ap *multi_ap)
{
    const int flags = 0
                    | (multi_ap->backhaul_bss ? 1 : 0)
                    | (multi_ap->fronthaul_bss ? 2 : 0);
    return flags;
}

static bool
hapd_util_into_osw_multi_ap(const char *str,
                            struct osw_multi_ap *multi_ap)
{
    if (str == NULL) return false;
    if (multi_ap == NULL) return false;

    memset(multi_ap, 0, sizeof(*multi_ap));

    const int val = atoi(str);
    if (val & 1) multi_ap->backhaul_bss = true;
    if (val & 2) multi_ap->fronthaul_bss = true;

    return true;
}

static void
hapd_util_wps_pbc_to_osw(const char *wps_get_status,
                         bool *pbc_active)
{
    /*
       PBC Status: Active
       Last WPS result: None
     */

    char *copy = strdupa(wps_get_status ?: "");
    char *line;

    *pbc_active = false;

    while ((line = strsep(&copy, "\n")) != NULL) {
        const char *k = strsep(&line, ":");
        const char *v = strsep(&line, "\n");

        if (k == NULL) continue;
        if (v == NULL) continue;
        if (v[0] != ' ') continue;
        v++;

        if (strcmp(k, "PBC Status") == 0) {
            if (strcmp(v, "Active") == 0) {
                *pbc_active = true;
            }
        }
    }
}

static bool
hapd_util_supp_rates_to_osw(const char *in, uint16_t *rates)
{
    /* eg. in="02 04 0b 16 0c 12 18 24 30 48 60 6c" */
    while (in != NULL && strlen(in) > 0) {
        const long halfmbps = strtol(in, NULL, 16);
        if (halfmbps != 0) {
            const enum osw_rate_legacy rate = osw_rate_legacy_from_halfmbps(halfmbps);
            if (rate < OSW_RATE_COUNT) {
                *rates |= (1 << rate);
            }
        }
        while (*in != 0 && !isspace(*in)) in++;
        while (*in != 0 && isspace(*in)) in++;
    }
    return true;
}

static bool
hapd_util_basic_rates_to_osw(const char *in, uint16_t *rates)
{
    /* eg. in="10 20 55 110" */
    while (in != NULL && strlen(in) > 0) {
        const long kbps = strtol(in, NULL, 10);
        if (kbps != 0) {
            const long halfmbps = kbps / 5;
            enum osw_rate_legacy rate = osw_rate_legacy_from_halfmbps(halfmbps);
            if (rate < OSW_RATE_COUNT) {
                *rates |= 1 << rate;
            }
        }
        while (*in != 0 && !isspace(*in)) in++;
        while (*in != 0 && isspace(*in)) in++;
    }
    return true;
}

static bool
hapd_util_beacon_rate_to_osw(const char *in, struct osw_beacon_rate *rate)
{
    /* Legacy (CCK/OFDM rates):
     *    beacon_rate=<legacy rate in 100 kbps>
     * HT:
     *    beacon_rate=ht:<HT MCS>
     * VHT:
     *    beacon_rate=vht:<VHT MCS>
     * HE:
     *    beacon_rate=he:<HE MCS>
     */
    const char *ht = "ht:";
    const char *vht = "vht:";
    const char *he = "he:";
    const int ht_max_mcs = 7;
    const int vht_max_mcs = 9;
    const int he_max_mcs = 11;
    const int mcs = atoi((strchr(in ?: "", ':') ?: "0") + 1);
    const int kbps = atoi(in);

    rate->type = OSW_BEACON_RATE_UNSPEC;

    if (strncmp(in, ht, strlen(ht)) == 0) {
        if (mcs <= ht_max_mcs) {
            rate->type = OSW_BEACON_RATE_HT;
            rate->u.ht_mcs = mcs;
        }
    }
    else if (strncmp(in, vht, strlen(vht)) == 0) {
        if (mcs <= vht_max_mcs) {
            rate->type = OSW_BEACON_RATE_VHT;
            rate->u.vht_mcs = mcs;
        }
    }
    else if (strncmp(in, he, strlen(he)) == 0) {
        if (mcs <= he_max_mcs) {
            rate->type = OSW_BEACON_RATE_HE;
            rate->u.he_mcs = mcs;
        }
    }
    else {
        const int halfmbps = kbps / 5;
        const enum osw_rate_legacy lrate = osw_rate_legacy_from_halfmbps(halfmbps);

        if (osw_rate_is_valid(lrate)) {
            rate->type = OSW_BEACON_RATE_ABG;
            rate->u.legacy = lrate;
        }
    }

    return true;
}

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

static bool
hapd_util_macaddr_acl_to_osw(const char *acl,
                            int *output_acl)
{
    if (acl == NULL || output_acl == NULL)
        return false;

    *output_acl = atoi(acl);
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
    if (ap->wpa.akm_eap)           STRSCAT(wpa_key_mgmt, "WPA-EAP ");
    if (ap->wpa.akm_ft_eap)        STRSCAT(wpa_key_mgmt, "FT-EAP ");
#if 0
    if (ap->wpa.akm_eap_sha256)    STRSCAT(wpa_key_mgmt, "WPA-EAP-SHA256 ");
    if (ap->wpa.akm_psk_sha256)    STRSCAT(wpa_key_mgmt, "WPA-PSK-SHA256 ");
    if (ap->wpa.akm_suite_b192)    STRSCAT(wpa_key_mgmt, "WPA-EAP-SUITE-B-192 ");
    if (ap->wpa.akm_ft_eap_sha384) STRSCAT(wpa_key_mgmt, "FT-EAP-SHA384 ");
#endif
    /* commit */
    const bool not_empty = (strlen(wpa_key_mgmt) > 0);
    if (not_empty) OSW_HOSTAP_CONF_SET_BUF(conf->wpa_key_mgmt, wpa_key_mgmt);
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
    const int auth_algs = osw_hostap_conf_auth_algs_from_osw(&ap->wpa);
    const bool any_alg_is_set = (auth_algs != 0);
    /* commit */
    if (any_alg_is_set) OSW_HOSTAP_CONF_SET_VAL(conf->auth_algs, auth_algs);
}

static void
osw_hostap_conf_osw_wpa_to_width(const struct osw_drv_vif_config_ap *ap,
                                 struct osw_hostap_conf_ap_config *conf)
{
    struct osw_channel pre_eht = ap->channel;
    struct osw_channel eht = ap->channel;

    osw_channel_downgrade_to(&pre_eht, OSW_CHANNEL_160MHZ);

    if (ap->mode.vht_enabled) {
        OSW_HOSTAP_CONF_SET_VAL(conf->vht_oper_chwidth,
                                osw_hostap_conf_chwidth_from_osw(pre_eht.width));
    }
    if (ap->mode.he_enabled) {
        OSW_HOSTAP_CONF_SET_VAL(conf->he_oper_chwidth,
                                osw_hostap_conf_chwidth_from_osw(pre_eht.width));
    }
    if (ap->mode.eht_enabled) {
        OSW_HOSTAP_CONF_SET_VAL(conf->eht_oper_chwidth,
                                osw_hostap_conf_chwidth_from_osw(eht.width));
    }
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
    const int channel = osw_freq_to_chan(ap->channel.control_freq_mhz);
    struct osw_channel pre_eht = ap->channel;
    struct osw_channel eht = ap->channel;

    osw_channel_downgrade_to(&pre_eht, OSW_CHANNEL_160MHZ);

    if (channel != 0) OSW_HOSTAP_CONF_SET_VAL(conf->channel, channel);

    if (ap->mode.vht_enabled) {
        const int center0 = osw_freq_to_chan(pre_eht.center_freq0_mhz);
        const int center1 = osw_freq_to_chan(pre_eht.center_freq1_mhz);
        if (center0 != 0) OSW_HOSTAP_CONF_SET_VAL(conf->vht_oper_centr_freq_seg0_idx, center0);
        if (center1 != 0) OSW_HOSTAP_CONF_SET_VAL(conf->vht_oper_centr_freq_seg1_idx, center1);
    }
    if (ap->mode.he_enabled) {
        const int center0 = osw_freq_to_chan(pre_eht.center_freq0_mhz);
        const int center1 = osw_freq_to_chan(pre_eht.center_freq1_mhz);
        if (center0 != 0) OSW_HOSTAP_CONF_SET_VAL(conf->he_oper_centr_freq_seg0_idx, center0);
        if (center1 != 0) OSW_HOSTAP_CONF_SET_VAL(conf->he_oper_centr_freq_seg1_idx, center1);
    }
    if (ap->mode.eht_enabled) {
        WARN_ON(eht.width == OSW_CHANNEL_80P80MHZ);
        const int center0 = osw_freq_to_chan(eht.center_freq0_mhz);
        if (center0 != 0) OSW_HOSTAP_CONF_SET_VAL(conf->eht_oper_centr_freq_seg0_idx, center0);
    }
}

static void
osw_hostap_conf_osw_wpa_to_ieee80211(const struct osw_drv_vif_config_ap *ap,
                                     struct osw_hostap_conf_ap_config *conf)
{
    char supp_rates[128] = {0};
    char basic_rates[128] = {0};
    char beacon_rate[128] = {0};

    osw_hostap_conf_set_rates_to_hapd(ap->mode.supported_rates, supp_rates, sizeof(supp_rates));
    osw_hostap_conf_set_rates_to_hapd(ap->mode.basic_rates, basic_rates, sizeof(basic_rates));
    osw_hostap_conf_set_brate_to_hapd(&ap->mode.beacon_rate, beacon_rate, sizeof(beacon_rate));

    OSW_HOSTAP_CONF_SET_BUF(conf->ssid, ap->ssid.buf);
    OSW_HOSTAP_CONF_SET_VAL(conf->beacon_int, ap->beacon_interval_tu);
    if (strlen(supp_rates) > 0) OSW_HOSTAP_CONF_SET_BUF(conf->supported_rates, supp_rates);
    if (strlen(basic_rates) > 0) OSW_HOSTAP_CONF_SET_BUF(conf->basic_rates, basic_rates);
    if (strlen(beacon_rate) > 0) OSW_HOSTAP_CONF_SET_BUF(conf->beacon_rate, beacon_rate);
    OSW_HOSTAP_CONF_SET_VAL(conf->ignore_broadcast_ssid, ap->ssid_hidden);
    OSW_HOSTAP_CONF_SET_VAL(conf->wmm_enabled, ap->mode.wmm_enabled);
    OSW_HOSTAP_CONF_SET_VAL(conf->uapsd_advertisement_enabled, ap->mode.wmm_uapsd_enabled);
    OSW_HOSTAP_CONF_SET_VAL(conf->ap_isolate, ap->isolated);
    OSW_HOSTAP_CONF_SET_VAL(conf->multi_ap, hapd_util_from_osw_multi_ap(&ap->multi_ap));
    // this needs a is_X_supported guard; this is not gauranteed to be there
    // OSW_HOSTAP_CONF_SET_VAL(conf->multicast_to_unicast, ap->mcast2ucast);
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
    const int offset = osw_channel_ht40_offset(c);
    if (offset == 1) return "[HT40+]";
    if (offset == -1) return "[HT40-]";
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
osw_hostap_conf_osw_wpa_to_ieee80211be(const struct osw_drv_vif_config_ap *ap,
                                       struct osw_hostap_conf_ap_config *conf)
{
    if (ap->mode.eht_enabled) {
        OSW_HOSTAP_CONF_SET_VAL(conf->ieee80211be, ap->mode.eht_enabled);
        //OSW_HOSTAP_CONF_SET_VAL(conf->require_eht, ap->mode.eht_required);
    }
}

static void
osw_hostap_conf_osw_wpa_to_passpoint(const struct osw_drv_vif_config_ap *ap,
                                     struct osw_hostap_conf_ap_config *conf)
{
    char hs20_wan_metrics[16];
    unsigned char wan_info = 0;

    if (!ap->passpoint.hs20_enabled) return;

    OSW_HOSTAP_CONF_SET_VAL(conf->hs20, ap->passpoint.hs20_enabled);
    OSW_HOSTAP_CONF_SET_VAL(conf->interworking, true);

    if (ap->passpoint.hessid.len > 0)
        OSW_HOSTAP_CONF_SET_BUF(conf->hessid, ap->passpoint.hessid.buf);

    if (ap->passpoint.osu_ssid.len > 0)
        OSW_HOSTAP_CONF_SET_BUF(conf->osu_ssid, ap->passpoint.osu_ssid.buf);

    /* adv_wan legacy */
    wan_info |= (ap->passpoint.adv_wan_status ? OSW_PASSPOINT_ADV_WAN_STATUS : 0);
    wan_info |= (ap->passpoint.adv_wan_symmetric ? OSW_PASSPOINT_ADV_WAN_SYMMETRIC : 0);
    wan_info |= (ap->passpoint.adv_wan_at_capacity ? OSW_PASSPOINT_ADV_WAN_AT_CAP : 0);
    snprintf(hs20_wan_metrics, sizeof(hs20_wan_metrics), "%02X:0:0:0:0:0", wan_info);
    OSW_HOSTAP_CONF_SET_BUF(conf->hs20_wan_metrics, hs20_wan_metrics);

    OSW_HOSTAP_CONF_SET_VAL(conf->osen, ap->passpoint.osen);
    OSW_HOSTAP_CONF_SET_VAL(conf->asra, ap->passpoint.asra);
    OSW_HOSTAP_CONF_SET_VAL(conf->access_network_type, ap->passpoint.ant);
    OSW_HOSTAP_CONF_SET_VAL(conf->venue_group, ap->passpoint.venue_group);
    OSW_HOSTAP_CONF_SET_VAL(conf->venue_type, ap->passpoint.venue_type);
    OSW_HOSTAP_CONF_SET_VAL(conf->anqp_domain_id, ap->passpoint.anqp_domain_id);
    OSW_HOSTAP_CONF_SET_VAL(conf->hs20_t_c_timestamp, ap->passpoint.t_c_timestamp);

    if (IS_NULL_PTR(ap->passpoint.t_c_filename) == false)
        OSW_HOSTAP_CONF_SET_BUF(conf->hs20_t_c_filename, ap->passpoint.t_c_filename);

    if (IS_NULL_PTR(ap->passpoint.anqp_elem) == false)
        OSW_HOSTAP_CONF_SET_BUF(conf->anqp_elem, ap->passpoint.anqp_elem);

    OSW_HOSTAP_CONF_SET_BUF_ARRAY(conf->domain_name,
                                  ap->passpoint.domain_list,
                                  ap->passpoint.domain_list_len);
    OSW_HOSTAP_CONF_SET_VAL(conf->domain_name_len,
                            ap->passpoint.domain_list_len);

    OSW_HOSTAP_CONF_SET_BUF_ARRAY(conf->nai_realm,
                                  ap->passpoint.nairealm_list,
                                  ap->passpoint.nairealm_list_len);
    OSW_HOSTAP_CONF_SET_VAL(conf->nai_realm_len,
                            ap->passpoint.nairealm_list_len);

    OSW_HOSTAP_CONF_SET_BUF_ARRAY(conf->roaming_consortium,
                                  ap->passpoint.roamc_list,
                                  ap->passpoint.roamc_list_len);
    OSW_HOSTAP_CONF_SET_VAL(conf->roaming_consortium_len,
                            ap->passpoint.roamc_list_len);

    OSW_HOSTAP_CONF_SET_BUF_ARRAY(conf->hs20_oper_friendly_name,
                                  ap->passpoint.oper_fname_list,
                                  ap->passpoint.oper_fname_list_len);
    OSW_HOSTAP_CONF_SET_VAL(conf->hs20_oper_friendly_name_len,
                            ap->passpoint.oper_fname_list_len);

    OSW_HOSTAP_CONF_SET_BUF_ARRAY(conf->venue_name,
                                  ap->passpoint.venue_name_list,
                                  ap->passpoint.venue_name_list_len);
    OSW_HOSTAP_CONF_SET_VAL(conf->venue_name_len,
                            ap->passpoint.venue_name_list_len);

    OSW_HOSTAP_CONF_SET_BUF_ARRAY(conf->venue_url,
                                  ap->passpoint.venue_url_list,
                                  ap->passpoint.venue_url_list_len);
    OSW_HOSTAP_CONF_SET_VAL(conf->venue_url_len,
                            ap->passpoint.venue_url_list_len);

    OSW_HOSTAP_CONF_SET_BUF_ARRAY(conf->anqp_3gpp_cell_net,
                                  ap->passpoint.list_3gpp_list,
                                  ap->passpoint.list_3gpp_list_len);
    OSW_HOSTAP_CONF_SET_VAL(conf->anqp_3gpp_cell_net_len,
                            ap->passpoint.list_3gpp_list_len);

    OSW_HOSTAP_CONF_SET_VAR_ARRAY(conf->network_auth_type,
                                  ap->passpoint.net_auth_type_list,
                                  ap->passpoint.net_auth_type_list_len);

    OSW_HOSTAP_CONF_SET_VAL(conf->network_auth_type_len,
                            ap->passpoint.net_auth_type_list_len);
}

static void
osw_hostap_conf_osw_wpa_to_radius_list(const struct osw_drv_vif_config_ap *ap,
                                       struct osw_hostap_conf_ap_config *conf)
{
    const struct osw_radius_list *rad_list = &ap->radius_list;
    struct osw_radius *ap_radius = rad_list->list;
    size_t i;

    MEMZERO(conf->radius_buf);

    for (i = 0; i < rad_list->count; i++) {
        STRSCAT(conf->radius_buf, strfmta("auth_server_addr=%s\n", ap_radius->server));
        STRSCAT(conf->radius_buf, strfmta("auth_server_port=%d\n", ap_radius->port));
        STRSCAT(conf->radius_buf, strfmta("auth_server_shared_secret=%s\n", ap_radius->passphrase));
        ap_radius++;
    };

    rad_list = &ap->acct_list;
    ap_radius = rad_list->list;

    for (i = 0; i < rad_list->count; i++) {
        STRSCAT(conf->radius_buf, strfmta("acct_server_addr=%s\n", ap_radius->server));
        STRSCAT(conf->radius_buf, strfmta("acct_server_port=%d\n", ap_radius->port));
        STRSCAT(conf->radius_buf, strfmta("acct_server_shared_secret=%s\n", ap_radius->passphrase));
        ap_radius++;
    };
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
    struct osw_wps_cred *wps_cred = ap->wps_cred_list.list;

    if (!(ap->wpa.akm_psk || ap->wpa.akm_ft_psk)) return;

    OSW_HOSTAP_CONF_SET_BUF(conf->wpa_psk_file, "SOMETHING");
    for (i = 0; i < ap->psk_list.count; i++) {
        bool is_wps_psk = false;
        size_t j = 0;
        for (j = 0; j < ap->wps_cred_list.count; j++) {
            is_wps_psk = strcmp(psk[i].psk.str, wps_cred[j].psk.str) == 0;
            if (is_wps_psk == true)
                break;
        }

        if (is_wps_psk == true) STRSCAT(conf->psks_buf, "wps=1 ");
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
osw_hostap_conf_osw_wpa_to_enterprise(const struct osw_drv_vif_config_ap *ap,
                                      struct osw_hostap_conf_ap_config *conf)
{
    if (!(ap->wpa.akm_eap ||
          ap->wpa.akm_ft_eap ||
#if 0
          ap->wpa.akm_eap_sha256 ||
          ap->wpa.akm_ft_eap_sha384 ||
#endif
          false))
        return;

    OSW_HOSTAP_CONF_SET_VAL(conf->ieee8021x, 1);
    OSW_HOSTAP_CONF_SET_VAL(conf->auth_algs, 1);
    OSW_HOSTAP_CONF_SET_BUF(conf->nas_identifier, ap->nas_identifier.buf);
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

void
osw_hostap_conf_list_free(struct osw_hostap_conf_ap_config *conf)
{
    int i;
    for (i = 0; i < conf->domain_name_len; i++) {
        FREE(conf->domain_name[i]);
        conf->domain_name[i] = NULL;
    }
    conf->domain_name_len = 0;

    for (i = 0; i < conf->nai_realm_len; i++) {
        FREE(conf->nai_realm[i]);
        conf->nai_realm[i] = NULL;
    }
    conf->nai_realm_len = 0;

    for (i = 0; i < conf->roaming_consortium_len; i++) {
        FREE(conf->roaming_consortium[i]);
        conf->roaming_consortium[i] = NULL;
    }
    conf->roaming_consortium_len = 0;

    for (i = 0; i < conf->hs20_oper_friendly_name_len; i++) {
        FREE(conf->hs20_oper_friendly_name[i]);
        conf->hs20_oper_friendly_name[i] = NULL;
    }
    conf->hs20_oper_friendly_name_len = 0;

    for (i = 0; i < conf->venue_name_len; i++) {
        FREE(conf->venue_name[i]);
        conf->venue_name[i] = NULL;
    }
    conf->venue_name_len = 0;

    for (i = 0; i < conf->venue_url_len; i++) {
        FREE(conf->venue_url[i]);
        conf->venue_url[i] = NULL;
    }
    conf->venue_url_len = 0;

    for (i = 0; i < conf->anqp_3gpp_cell_net_len; i++) {
        FREE(conf->anqp_3gpp_cell_net[i]);
        conf->anqp_3gpp_cell_net[i] = NULL;
    }
    conf->anqp_3gpp_cell_net_len = 0;
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
    if (vif->vif_type != OSW_VIF_AP) return true;

    memset(conf, 0, sizeof(*conf));
    if (vif->enabled == false) return true;

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

    /*  IEEE 802.11be related configuration  */
    osw_hostap_conf_osw_wpa_to_ieee80211be (ap, conf);

    /* Passpoint configuration */
    osw_hostap_conf_osw_wpa_to_passpoint(ap, conf);

    /*  WPA/IEEE 802.11i configuration       */
    osw_hostap_conf_osw_wpa_to_wpa         (ap, conf);
    osw_hostap_conf_osw_wpa_to_wpa_rekey   (ap, conf);
    osw_hostap_conf_osw_wpa_to_wpa_key_mgmt(ap, conf);
    osw_hostap_conf_osw_wpa_to_pairwise    (ap, conf);
    osw_hostap_conf_osw_wpa_to_pmf         (ap, conf);
    osw_hostap_conf_osw_wpa_to_psks        (ap, conf);
    osw_hostap_conf_osw_wpa_to_sae         (ap, conf);
    osw_hostap_conf_osw_wpa_to_wps         (ap, conf);

    /* WPA Enterprise/IEEE 802.1x configuration */
    osw_hostap_conf_osw_wpa_to_enterprise  (ap, conf);
    osw_hostap_conf_osw_wpa_to_radius_list (ap, conf);

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
    CONF_APPEND(supported_rates, "%s");
    CONF_APPEND(basic_rates, "%s");
    CONF_APPEND(beacon_rate, "%s");
    CONF_APPEND(accept_mac_file, "%s");
    CONF_APPEND(deny_mac_file, "%s");
    CONF_APPEND(auth_algs, "%d");
    CONF_APPEND(ignore_broadcast_ssid, "%d");
    CONF_APPEND(wmm_enabled, "%d");
    CONF_APPEND(uapsd_advertisement_enabled, "%d");
    CONF_APPEND(multi_ap, "%d");
    CONF_APPEND(ap_isolate, "%d");
    CONF_APPEND(multicast_to_unicast, "%d");
    CONF_APPEND(send_probe_response, "%d");
    CONF_APPEND(noscan, "%d");

    /* IEEE 802.11n related configuration */
    CONF_APPEND(ieee80211n, "%d");
    CONF_APPEND(ht_capab, "%s");
    CONF_APPEND(require_ht, "%d");

    /* IEEE 802.11ac related configuration */
    if (conf->ieee80211ac_exists) {
        CONF_APPEND(ieee80211ac, "%d");
        CONF_APPEND(vht_capab, "%s");
        CONF_APPEND(require_vht, "%d");
        CONF_APPEND(vht_oper_chwidth, "%d");
        CONF_APPEND(vht_oper_centr_freq_seg0_idx, "%d");
        CONF_APPEND(vht_oper_centr_freq_seg1_idx, "%d");
    }

    /* IEEE 802.11ax related configuration */
    if (conf->ieee80211ax_exists) {
        CONF_APPEND(ieee80211ax, "%d");
        CONF_APPEND(he_oper_chwidth, "%d");
        CONF_APPEND(he_oper_centr_freq_seg0_idx, "%d");
        CONF_APPEND(he_oper_centr_freq_seg1_idx, "%d");
    }

    /* IEEE 802.11ax related configuration */
    if (conf->ieee80211be_exists) {
        CONF_APPEND(ieee80211be, "%d");
        CONF_APPEND(eht_oper_chwidth, "%d");
        CONF_APPEND(eht_oper_centr_freq_seg0_idx, "%d");
    }

    /* IEEE 802.1X-2004 related configuration */
    CONF_APPEND(ieee8021x, "%d");
    CONF_APPEND(eapol_version, "%d");

    /* Integrated EAP server */
    CONF_APPEND(eap_server, "%d");

    /* RADIUS client configuration */
    /* FIXME  */

    /* Passpoint configuration */
    CONF_APPEND(hessid, "%s");
    CONF_APPEND(hs20, "%d");
    CONF_APPEND(interworking, "%d");
    CONF_APPEND(hs20_wan_metrics, "%s");
    CONF_APPEND(osen, "%d");
    CONF_APPEND(asra, "%d");
    CONF_APPEND_ARRAY_SEP(domain_name, "%s", ",");
    CONF_APPEND(access_network_type, "%d");
    CONF_APPEND(venue_group, "%d");
    CONF_APPEND(venue_type, "%d");
    CONF_APPEND(anqp_domain_id, "%d");
    CONF_APPEND(hs20_t_c_timestamp, "%d");
    CONF_APPEND(osu_ssid, "%s");
    CONF_APPEND(hs20_t_c_filename, "%s");
    CONF_APPEND(anqp_elem, "%s");
    CONF_APPEND_ARRAY(roaming_consortium, "%s");
    CONF_APPEND_ARRAY(nai_realm, "%s");
    CONF_APPEND_ARRAY(hs20_oper_friendly_name, "%s");
    CONF_APPEND_ARRAY(venue_name, "%s");
    CONF_APPEND_ARRAY(venue_url, "%s");
    CONF_APPEND_ARRAY_SEP(anqp_3gpp_cell_net, "%s", ";");
    CONF_APPEND_ARRAY(network_auth_type, "%d");

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
    CONF_APPEND(nas_identifier, "%s");
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

    CONF_APPEND(use_driver_iface_addr, "%d");
    CONF_APPEND_BUF(conf->radius_buf);
    CONF_APPEND_BUF(conf->extra_buf);
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

        neighbor = &neighbor_list->list[neighbor_list->count];
        MEMZERO(*neighbor);

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

static void
osw_hostap_conf_fill_ap_state_acl(const struct osw_hostap_conf_ap_state_bufs *bufs,
                                        struct osw_drv_vif_state *vstate)
{
    const char *accept_acl = bufs->show_accept_acl;
    const char *deny_acl = bufs->show_deny_acl;
    const char *get_config = bufs->get_config;
    struct osw_drv_vif_state_ap *ap = &vstate->u.ap;
    struct osw_hwaddr_list *acl_list;
    struct osw_hwaddr * acl;
    char *cpy_deny_acl = NULL;
    char *cpy_accept_acl = NULL;
    char *line;
    bool ok;
    int  macaddr_acl = 0;

    STATE_GET_BY_FN(macaddr_acl, get_config, "macaddr_acl", hapd_util_macaddr_acl_to_osw);

    if (macaddr_acl == 0)
        ap->acl_policy = OSW_ACL_DENY_LIST;
    else
        ap->acl_policy = OSW_ACL_ALLOW_LIST;

    if (deny_acl != NULL && strlen(deny_acl) > 0) {
        cpy_deny_acl = STRDUP(deny_acl);
    }

    if (accept_acl != NULL && strlen(accept_acl) > 0) {
        cpy_accept_acl = STRDUP(accept_acl);
    }

    if (cpy_deny_acl == NULL && cpy_accept_acl == NULL) {
        return;
    }

    acl_list = &ap->acl;
    acl_list->count = 0;
    acl_list->list = CALLOC(1, sizeof(struct osw_hwaddr));

    if (cpy_deny_acl != NULL) {
        char *tokens = cpy_deny_acl;
        while ((line = strsep(&tokens, "\n")) != NULL) {
            if (cpy_accept_acl == NULL ||
                (cpy_accept_acl != NULL && strstr(cpy_accept_acl, line) == NULL)) {
                acl_list->list = REALLOC(acl_list->list,
                                         (acl_list->count + 1) * sizeof(struct osw_hwaddr));
                acl = &acl_list->list[acl_list->count];
                MEMZERO(*acl);
                ok = osw_hwaddr_from_cstr(line, acl);
                if (ok == false) continue;
                acl_list->count++;
            }
        }
    } else if (cpy_accept_acl != NULL) {
        char *tokens = cpy_accept_acl;
        while ((line = strsep(&tokens, "\n")) != NULL) {
            acl_list->list = REALLOC(acl_list->list,
                                     (acl_list->count + 1) * sizeof(struct osw_hwaddr));
            acl = &acl_list->list[acl_list->count];
            MEMZERO(*acl);
            ok = osw_hwaddr_from_cstr(line, acl);
            if (ok == false) continue;
            acl_list->count++;
        }
    }

    FREE(cpy_deny_acl);
    FREE(cpy_accept_acl);
}


void
osw_hostap_conf_fill_ap_state(const struct osw_hostap_conf_ap_state_bufs *bufs,
                              struct osw_drv_vif_state *vstate)
{
    const char *config = bufs->config;
    const char *get_config = bufs->get_config;
    const char *status = bufs->status;
    //const char *mib = bufs->mib;
    const char *wps_get_status = bufs->wps_get_status;
    const char *wpa_psk_file = bufs->wpa_psk_file;
    struct osw_drv_vif_state_ap *ap = &vstate->u.ap;

    /* Fill in neighbors list */
    osw_hostap_conf_fill_ap_state_neighbors(bufs, vstate);

    STATE_GET_BY_FN(vstate->status,              status, "state",
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
    STATE_GET_BY_FN(ap->mode.supported_rates,    status, "supported_rates",
                    hapd_util_supp_rates_to_osw);
    STATE_GET_BY_FN(ap->mode.basic_rates,        config, "basic_rates",
                    hapd_util_basic_rates_to_osw);
    STATE_GET_BY_FN(ap->mode.beacon_rate,        config, "beacon_rate",
                    hapd_util_beacon_rate_to_osw);
    STATE_GET_BOOL(ap->mode.wnm_bss_trans,       config, "bss_transition");
    STATE_GET_BOOL(ap->mode.rrm_neighbor_report, config, "rrm_neighbor_report");
    STATE_GET_BOOL(ap->mode.wmm_enabled,         config, "wmm_enabled");
    STATE_GET_BOOL(ap->mode.wmm_uapsd_enabled,   config, "uapsd_advertisement_enabled");

    STATE_GET_BOOL(ap->mode.ht_enabled,          status, "ieee80211n");
    STATE_GET_BOOL(ap->mode.vht_enabled,         status, "ieee80211ac");
    STATE_GET_BOOL(ap->mode.he_enabled,          status, "ieee80211ax");
    STATE_GET_BOOL(ap->mode.eht_enabled,         status, "ieee80211be");
    STATE_GET_BOOL(ap->mode.ht_required,         config, "require_ht");
    STATE_GET_BOOL(ap->mode.vht_required,        config, "require_vht");

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

    STATE_GET_BY_FN(ap->multi_ap,                config, "multi_ap",
                    hapd_util_into_osw_multi_ap);
    STATE_GET_BY_FN(ap->mode.wps,                get_config, "wps_state",
                    hapd_util_wps_status_to_osw);

    hapd_util_wps_pbc_to_osw(wps_get_status, &ap->wps_pbc);

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

    /* Fill in acl list */
    osw_hostap_conf_fill_ap_state_acl(bufs, vstate);

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

    hapd_util_hapd_psk_file_to_osw(wpa_psk_file,
                                   &ap->psk_list,
                                   &ap->wps_cred_list);

    STATE_GET_BY_FN(ap->psk_list,                config, "sae_password",
                    hapd_util_hapd_sae_password_to_osw);

    STATE_GET_BY_FN(ap->nas_identifier,          config, "nas_identifier",
                    hapd_util_hapd_nas_identifier_to_osw);

    hapd_util_hapd_mib_to_radius_list(bufs, &ap->radius_list, &ap->acct_list);
    hapd_util_hapd_conf_to_passpoint(bufs, &ap->passpoint);
}

#include "osw_hostap_conf_ut.c.h"
