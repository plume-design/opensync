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

#include "log.h"
#include "schema.h"
#include "schema_consts.h"
#include "internal-util.h"


bool
util_vif_wpa_key_mgmt_partial_match(const struct schema_Wifi_VIF_Config *vconf,
                                    const char *key_mgmt)
{
    int i = 0;

    for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
        if (strstr(vconf->wpa_key_mgmt[i], key_mgmt))
            return true;
    }

    return false;
}

bool
util_vif_wpa_key_mgmt_exact_match(const struct schema_Wifi_VIF_Config *vconf,
                                          const char *key_mgmt)
{
    int i = 0;

    for (i = 0; i < vconf->wpa_key_mgmt_len; i++) {
        if (strcmp(vconf->wpa_key_mgmt[i], key_mgmt) == 0)
            return true;
    }

    return false;
}

void
util_vif_get_wpa_pairwise(const struct schema_Wifi_VIF_Config *vconf,
                          char *buf,
                          size_t len)
{
    bool tkip = false, ccmp = false;
    memset(buf, 0, len);

    if (!vconf->wpa)
        return;

    if (util_vif_pairwise_supported(vconf)) {
        /* Pairwise supported - resolve what is given */
        if (vconf->wpa_pairwise_tkip) { csnprintf(&buf, &len, "TKIP "); tkip = true; }
        if (vconf->wpa_pairwise_ccmp) { csnprintf(&buf, &len, "CCMP "); ccmp = true; }
        if (vconf->rsn_pairwise_tkip && !tkip) csnprintf(&buf, &len, "TKIP ");
        if (vconf->rsn_pairwise_ccmp && !ccmp) csnprintf(&buf, &len, "CCMP ");
        return;
    }
    /* 'Old' controller - no support for xxx_pairwise_xxxx */
    if (util_vif_wpa_key_mgmt_partial_match(vconf, "wpa-"))
        csnprintf(&buf, &len, "TKIP ");
    if (util_vif_wpa_key_mgmt_partial_match(vconf, "wpa2-") ||
        util_vif_wpa_key_mgmt_partial_match(vconf, "sae") ||
        util_vif_wpa_key_mgmt_partial_match(vconf, "dpp"))
        csnprintf(&buf, &len, "CCMP ");

    WARN_ON(len == 1); /* likely buf was truncated */
}

bool
util_vif_pairwise_supported(const struct schema_Wifi_VIF_Config *vconf)
{
    if (vconf->wpa_pairwise_tkip_exists || vconf->wpa_pairwise_ccmp_exists ||
        vconf->rsn_pairwise_tkip_exists || vconf->rsn_pairwise_ccmp_exists)
        return true;

    /* None of the newly introduced fields are available
     * - controller must be an old version */
    return false;
}

void
util_vif_get_wpa_key_mgmt(const struct schema_Wifi_VIF_Config *vconf,
                          char *buf,
                          size_t len)
{
    memset(buf, 0, len);

    if (!vconf->wpa)
        return;

    /* despite ambiguity in WPA/RSN, wpa-psk always resolves to WPA-PSK */
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_WPA_PSK))
        csnprintf(&buf, &len, "WPA-PSK ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_WPA_PSK_SHA256))
        csnprintf(&buf, &len, "WPA-PSK-SHA256 ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_WPA_EAP))
        csnprintf(&buf, &len, "WPA-EAP ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_WPA_EAP_SHA256))
        csnprintf(&buf, &len, "WPA-EAP-SHA256 ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_WPA_EAP_B_192))
        csnprintf(&buf, &len, "WPA-EAP-B-192 ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_FT_SAE))
        csnprintf(&buf, &len, "FT-SAE ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_FT_PSK))
        csnprintf(&buf, &len, "FT-PSK ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_FT_EAP))
        csnprintf(&buf, &len, "FT-EAP ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_FT_EAP_SHA384))
        csnprintf(&buf, &len, "FT-EAP-SHA384 ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_DPP))
        csnprintf(&buf, &len, "DPP ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_SAE))
        csnprintf(&buf, &len, "SAE ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_OWE))
        csnprintf(&buf, &len, "OWE ");
    /* legacy and deprecated */
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_WPA2_PSK))
        csnprintf(&buf, &len, "WPA-PSK ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_WPA2_EAP))
        csnprintf(&buf, &len, "WPA-EAP ");
    if (util_vif_wpa_key_mgmt_exact_match(vconf, SCHEMA_CONSTS_KEY_FT_WPA2_PSK))
        csnprintf(&buf, &len, "FT-PSK ");

    WARN_ON(len == 1); /* likely buf was truncated */
}

int
util_vif_get_wpa(const struct schema_Wifi_VIF_Config *vconf)
{
    int val;
    val  = (vconf->wpa_pairwise_tkip ? HOSTAP_CONF_WPA_WPA : 0);
    val |= (vconf->wpa_pairwise_ccmp ? HOSTAP_CONF_WPA_WPA : 0);
    val |= (vconf->rsn_pairwise_tkip ? HOSTAP_CONF_WPA_RSN : 0);
    val |= (vconf->rsn_pairwise_ccmp ? HOSTAP_CONF_WPA_RSN : 0);
    /* check schema wpa option for an OPEN network */
    val = (vconf->wpa ? val : HOSTAP_CONF_WPA_OPEN);

    return val;
}

void
util_vif_get_ieee80211w(const struct schema_Wifi_VIF_Config *vconf,
                        char *buf,
                        size_t len)
{
    bool need_11w = util_vif_wpa_key_mgmt_partial_match(vconf, "sae")
                 || util_vif_wpa_key_mgmt_partial_match(vconf, "dpp");
    bool non_11w = util_vif_wpa_key_mgmt_partial_match(vconf, "ft-wpa2-")
                || util_vif_wpa_key_mgmt_partial_match(vconf, "wpa2-");

    memset(buf, 0, len);

    if (vconf->pmf_exists) {
        csnprintf(&buf, &len, "%i", util_vif_pmf_schema_to_enum(vconf->pmf));
        return;
    }

    if (!vconf->wpa)
        return;

    if (need_11w) {
        if (non_11w)
            csnprintf(&buf, &len, "1"); /* optional, for mixed mode */
        else
            csnprintf(&buf, &len, "2");
    }
    /* For non-SAE configs leave ieee80211w empty ("") */

    WARN_ON(len == 1); /* likely buf was truncated */
}

/* type conversion helpers */

enum hostap_conf_pmf
util_vif_pmf_schema_to_enum(const char* pmf)
{
    if (!strcmp(pmf, SCHEMA_CONSTS_SECURITY_PMF_DISABLED)) return HOSTAP_CONF_PMF_DISABLED;
    if (!strcmp(pmf, SCHEMA_CONSTS_SECURITY_PMF_OPTIONAL)) return HOSTAP_CONF_PMF_OPTIONAL;
    if (!strcmp(pmf, SCHEMA_CONSTS_SECURITY_PMF_REQUIRED)) return HOSTAP_CONF_PMF_REQUIRED;

    LOGE("%s: Unknown PMF parameter value: %s. Fallback to 'optional'", __func__, pmf);
    return HOSTAP_CONF_PMF_OPTIONAL;
}

const char*
util_vif_pmf_enum_to_schema(enum hostap_conf_pmf pmf)
{
    switch (pmf) {
        case HOSTAP_CONF_PMF_DISABLED: return SCHEMA_CONSTS_SECURITY_PMF_DISABLED;
        case HOSTAP_CONF_PMF_OPTIONAL: return SCHEMA_CONSTS_SECURITY_PMF_OPTIONAL;
        case HOSTAP_CONF_PMF_REQUIRED: return SCHEMA_CONSTS_SECURITY_PMF_REQUIRED;
    }
    LOGE("%s: PMF value %d not recognized!", __func__, pmf);
    return SCHEMA_CONSTS_SECURITY_PMF_DISABLED;
}

enum hostap_conf_key_mgmt
util_vif_wpa_key_mgmt_to_enum(const char *key_mgmt)
{
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK))        return HOSTAP_CONF_KEY_MGMT_WPA_PSK;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_WPA_PSK_SHA256)) return HOSTAP_CONF_KEY_MGMT_WPA_PSK_SHA256;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP))        return HOSTAP_CONF_KEY_MGMT_WPA_EAP;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP_SHA256)) return HOSTAP_CONF_KEY_MGMT_WPA_EAP_SHA256;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_WPA_EAP_B_192))  return HOSTAP_CONF_KEY_MGMT_WPA_EAP_B_192;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_FT_SAE))         return HOSTAP_CONF_KEY_MGMT_FT_SAE;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_FT_PSK))         return HOSTAP_CONF_KEY_MGMT_FT_PSK;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_FT_EAP))         return HOSTAP_CONF_KEY_MGMT_FT_EAP;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_FT_EAP_SHA384))  return HOSTAP_CONF_KEY_MGMT_FT_EAP_SHA384;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_DPP))            return HOSTAP_CONF_KEY_MGMT_DPP;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_SAE))            return HOSTAP_CONF_KEY_MGMT_SAE;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_OWE))            return HOSTAP_CONF_KEY_MGMT_OWE;
    /* legacy and deprecated */
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_WPA2_PSK))       return HOSTAP_CONF_KEY_MGMT_WPA2_PSK;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_WPA2_EAP))       return HOSTAP_CONF_KEY_MGMT_WPA2_EAP;
    if (!strcmp(key_mgmt, SCHEMA_CONSTS_KEY_FT_WPA2_PSK))    return HOSTAP_CONF_KEY_MGMT_FT_WPA2_PSK;

    LOGE("%s: wpa_key_mgmt '%s' not recognized!", __func__, key_mgmt);
    return HOSTAP_CONF_KEY_MGMT_UNKNOWN;
}
